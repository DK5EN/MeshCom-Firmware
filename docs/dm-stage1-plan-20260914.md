# DM stage 1 — outbox and the retry ladder, behind a tri-state switch

Draft for review, 2026-09-14. Not approved, nothing in code. Derived from stage 1 in
`docs/dm-transport-impl-plan-20260913.md` (D1, 1.1, 1.2, T-1.1..T-1.5), traps T1/T2/T2b/T11 in
`docs/dm-reliability-and-store-node-verdict-20260913.md`, and the operator decision of
2026-09-14 to ship the feature switched off.

## Status

| Item                | State                                               |
| ------------------- | --------------------------------------------------- |
| Plan                | draft, awaiting operator review                     |
| Decisions 1-4 below | open                                                |
| M0-1                | not run; the outbox is built regardless (section 3) |

## 1. What the operator gets

One setting, three values, on every board:

| `--dmretry` | GUI label "Enhanced message transport protection" | Behaviour                                                                 |
| ----------- | ------------------------------------------------- | ------------------------------------------------------------------------- |
| `off`       | off (default)                                     | exactly today: three same-id retries 40 s apart, byte-identical frames    |
| `3`         | 3 attempts                                        | attempt 1 as today; attempts 2 and 3 with a fresh msg_id; stop all on ACK |
| `9`         | 9 attempts                                        | the D5 ladder: (3 x 40 s + 1 min) x 3, nine transmissions in nine minutes |

Applies to user-originated DMs only (destination is a callsign, payload carries `{NNN`). Groups,
broadcasts, positions and ACKs are untouched (D6). Default off means the release changes nothing
for anyone who does not opt in.

Why fresh ids matter: every relay's dedup ring already holds the first attempt's msg_id, so a
same-id retry is dropped at the first relay and only ever helps a one-hop neighbour. A fresh id
crosses the mesh like a new message. Stage 2.1 is what makes that safe: every receiver on this
firmware folds the attempts on (source, NNN), displays once and re-acks each.

**What the GUI must say next to the select:** "Requires the receiving node to run this firmware
or newer. Older nodes show every retry as a new message." That is the whole risk of opting in and
the reason the default is off.

## 2. The setting

- `--dmretry off|3|9`, bare `--dmretry` prints the state; `[DMRETRY];<value>` output; shown in
  `--info` as `DMRETRY mode=<value>`.
- Web GUI: a select on the setup page, group "Messages", with the warning sentence above; reaches
  the command through `/setparam/?dmretry=`.
- Persisted per T13 outside `struct s_meshcom_settings`: ESP32 NVS key `dm_retry` (u8); nRF52
  a small own file `/dm.cfg` (magic `DMC1`, one byte). New module `src/dm_settings.{h,cpp}`,
  compiled on every board (this is sender-side, not `ENABLE_MSGSTORE`).
- Read at boot after `init_flash()`; a RAM copy `dmRetryMode()` is what the sender consults.

## 3. The outbox (1.1)

`src/dm_outbox.{h,cpp}`, platform-neutral, native-tested, same env-struct pattern as
`msgstore` (clock, transmit, echo and bp callbacks injected).

Built regardless of M0-1 (plan recommendation, operator 2026-09-14): a parked ring slot's
survival depends on ambient relay load, which no bench can reproduce; the outbox is deterministic
for ~1.3 kB.

| Board              | Slots | Bytes   |
| ------------------ | ----- | ------- |
| ESP32-S3, nRF52840 | 5     | ~1.3 kB |
| classic ESP32      | 3     | ~0.8 kB |

Entry: `nnn`, `dst[10]`, `payload[161]` (stripped), `first_id` (attempt 1's msg_id),
`last_id` (the current attempt's msg_id), `first_ms`, `next_ms`, `attempt`, `state`
(FREE, WAIT_ECHO, LADDER, DONE_ACK, DONE_GIVEUP), `gen`.

**Keyed on NNN, never on a reconstructed msg_id (T1).** The `:ackNNN` arrives with the NNN; the
outbox matches on it and stops the entry whatever id the current attempt carried.
`findAndStopRingSlot()` keeps matching the ring by 32-bit id for the frame that may still be
queued, as today.

**`own_msg_id` is not the authority (T2)** — 20 slots, beacons consume them — but every attempt
still calls `insertOwnTx()` so echo recognition, the HEARD mark and the stage 4 held mark keep
working on the current id. The outbox additionally records `first_id` so the app's message row
(keyed on attempt 1's id) receives the 0x02/0x03/0x04 frames: every phone frame for an entry is
emitted with `first_id`, not with the attempt's id.

Outbox full: the DM is **refused** with a notice on the originating transport, the same way a
QRT refusal is (`bpEmitNack()` path), and `dmstat_outbox_full++`. Full is full: the channel and
the outbox have to clear before the next DM is accepted. Nothing is sent, nothing is silently
dropped, the user sees why.

## 4. The ladder (1.2)

Driven from the loop task next to `updateRetransmissionStatus()`, gated on
`bpCurrentState() == 0` (no attempt while QRS or QRT is latched). **The TX ring stays the only
transmit queue:** every attempt that is due is enqueued into the ring as an ordinary slot, one
per tick, and the ring's own scheduling, priority and CSMA decide when it goes on air. The outbox
holds nothing the ring could hold and adds no rate limit of its own.

Mode `off`: the outbox is not used at all; `sendMessage()` enqueues exactly as today.

Mode `3` and `9`:

1. **Attempt 1** goes out as today (same code path, status READY) but the ring slot is enqueued
   with retransmission **disabled** (`0xFF`) — the ring's own 3 x 40 s retry is replaced by the
   ladder. `dmStatNoteSent()` fires here, once per DM.
2. **Echo gate, 15 s.** If `own_msg_id[][4]` for `first_id` is still 0x00 after 15 s, nobody
   relayed it and a same-id retry is the only kind that can help: send the same frame again once
   (attempt 2 keeps `first_id`). If it was heard, skip straight to the fresh-id attempts. This
   removes the one wasted transmission per DM without changing the rate.
3. **Fresh-id attempts** mint `msg_id = millis()`, keep NNN, payload, destination and `max_hop`,
   re-encode, `insertOwnTx()`, enqueue with `0xFF`. Schedule: `3` = one block, attempts at 0,
   40, 80 s; `9` = three blocks with a minute between them, attempts at 0, 40, 80, 180, 220,
   260, 360, 400, 440 s (D5). The echo gate decides at attempt 2 whether that attempt keeps
   `first_id` (nothing relayed attempt 1) or takes a fresh id.
4. **Stop** on the destination's `:ackNNN` (outbox match on NNN): state DONE_ACK, the queued ring
   slot is stopped by id, phone frame 0x02 for `first_id`, `dmStatNoteAck()`.
5. **Give-up** after the last attempt: DONE_GIVEUP, the stage 0.3 report for `first_id` (0x03,
   or nothing if the message is held, stage 4 decision 3).
6. A `:sto` held notice (stage 4) does not stop the ladder (D3).

Ring interplay: attempts are enqueued `0xFF`, so `updateRetransmissionStatus()` never retries or
gives up on them; the give-up path in that function is unreachable for outbox-driven DMs and the
stage 0.3 report moves to the outbox for those. In mode `off` nothing moves.

## 5. Interactions to settle in code

- **Stage 3 peer-delivery tell.** The store node's hook currently recognises another store
  node's delivery by `hop 0 + comma in path + (msg_id & 0x3FF) != NNN`. A fresh-id attempt from a
  sender also has `(msg_id & 0x3FF) != NNN`, but it arrives with the sender's own path (no store
  node appended) and, after relays, hop > 0 in the common case; a direct fresh-id attempt at
  hop 0 is only possible when the sender itself sits at hop 0 next to the store node with
  `max_hop_text` 1. Replace the tell with: path holds exactly two calls **and the last one is not
  the frame's source** (a store node appends itself; a sender's own frame never has a second
  call unless relayed, and a relayed frame is hop > 0 or exhausted with the relay as last call —
  the exhausted case stays a false positive and is accepted, it only cancels one ladder cycle).
- **Stage 2.1 refresh.** A fresh-id attempt at a store node refreshes the mailbox entry (sender
  alive) — already implemented; verify with T-1.6.
- **Stage 0 counters.** `dmstat_attempts` counts every text-slot transmission, so ladder attempts
  are included; add `dmstat_outbox_full`.
- **T11.** Nine ids per DM raise the distinct-id rate at every relay; measure the dedup ring's
  rotation window on the bench node before and after (stage 0 counters), record in the plan.

## 6. Wave plan

| Owner | Exclusive files                                                                                                                                                                                                   | Content                                                   |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| A     | `src/dm_outbox.{h,cpp}` (core, replaces the stub), `src/dm_outbox_glue.cpp`, `src/loop_functions.cpp` (sendMessage hook), `src/lora_functions.cpp` (ack/echo/give-up hooks, stage 3 tell), `test/test_dm_outbox/` | outbox, ladder, hooks, native tests                       |
| B     | `src/web_functions/*`                                                                                                                                                                                             | the select + warning on the setup page, setparam mapping  |
| C     | `src/dm_settings.{h,cpp}`, `src/command_functions.cpp`, `docs/commands-dm-retry.md`                                                                                                                               | `--dmretry`, persistence on both platforms, `--info` line |

Orchestrator carve-outs before dispatch: `src/dm_outbox_api.h` (contract) with a link stub, the
`dmRetryMode()` declaration in `dm_settings.h` with a stub returning off, the loop-task call site
and boot-time load in both platform mains at the gate, native env entries.

## 7. Gate and bench

Native suite, sequential builds (Heltec V3, RAK4631, T-Beam as the 3-slot board, T-Deck Plus),
advisor pass, then on the bench with `--dmretry 9` on the sender and `off` on a control node:

| ID    | Test                                                                | Expect                                                                   |
| ----- | ------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| T-1.1 | Two-hop DM, destination's ACK suppressed (`--airgap` after receipt) | 9 attempts reach the destination (fresh ids), each re-acked, one display |
| T-1.2 | Same, ACK allowed on attempt 2                                      | every remaining attempt stops (T1)                                       |
| T-1.3 | Sender beaconing hard so `first_id` rotates out of `own_msg_id`     | ACK still attributed via the outbox, ladder stops (T2)                   |
| T-1.4 | Echo gate: once with a relay in range, once without                 | with relay: no same-id retry; without: exactly one                       |
| T-1.5 | QRT latched during a ladder                                         | no attempt while latched; resumes or expires cleanly                     |
| T-1.6 | Fresh-id attempt heard by a store node holding the DM               | entry refreshed, no second slot, no peer cancel                          |
| T-1.7 | `--dmretry off`                                                     | frames byte-identical to today, three same-id retries                    |
| T-1.8 | Destination on upstream 4.35t, `--dmretry 3`                        | three copies displayed there (documented cost), acks matched here        |
| T-1.9 | Outbox full (6 DMs in flight)                                       | 6th sent once as today, `outbox_full` counter, no drop                   |

## 8. Decisions (operator, 2026-09-14)

1. **Echo gate stays.** Attempt 2 keeps the first id when nobody relayed attempt 1, otherwise
   it takes a fresh id.
2. **Schedule:** blocks of three attempts 40 s apart with a minute between blocks. `3` is one
   block, `9` is three blocks.
3. **Outbox full refuses the DM** with a notice on the originating transport. Full is full; the
   channel and the outbox have to clear first.
4. **No separate outbox rate limit.** The TX ring is the transmit queue; attempts are folded into
   it one by one as ordinary slots, the ring decides when they go on air.
5. **Mode `off` means no outbox at all**, legacy path untouched.

## S1-1 implementation notes (2026-09-14)

- Modules: `src/dm_outbox.{cpp}` behind `src/dm_outbox_api.h` (22 native cases), glue
  `src/dm_outbox_glue.cpp` (5 slots on S3/nRF52840, 3 on classic ESP32), settings
  `src/dm_settings.{h,cpp}` (NVS key `dm_retry`, nRF52 file `/dm.cfg`, native no-op).
- `sendMessage()`: in mode 3/9 a DM is refused with the QRT-style notice and `[OUTBOX];refuse;full`
  when no slot is free, else attempt 1 is enqueued with ring retransmission disabled and the entry
  registered. Mode off touches nothing.
- Receive side: echo, ack and held hooks only when the mode is not off; the ack site's own 0x02
  frame stands, the outbox only stops the ladder. The stage 3 peer-delivery tell is now the path
  shape (two calls, last differs from the source).
- The echo gate decides at attempt 2 (due at 40 s), so the 15 s constant in the contract header is
  documentary only.
- Loop: `dmOutboxLoop()` next to `updateRetransmissionStatus()` in both platform mains (ESP32: the
  `bRadio` tick and the external-radio tick), `OUTBOX` setlog line after `MBOX` when the mode is on.
