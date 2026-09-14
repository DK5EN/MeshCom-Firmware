# DM stage 3 — store node wave plan

Prepared 2026-09-14 from `docs/dm-transport-impl-plan-20260913.md` (stage 3, D2-D8),
`docs/MeshCom-Store-Node-Concept-20260911.md` (§3, §7, §8) and the verdict's §3.5/T7/T8/T12/T13/T14.
Dispatch mechanics per `orchestrate-waves`. Fork-first: no upstream PR until the role is accepted.

## Wave status log

| Wave | Content                                                        | Status      |
| ---- | -------------------------------------------------------------- | ----------- |
| S3-0 | Recon, GUI design mockup, this plan                            | in progress |
| S3-1 | A mailbox core, B mailbox page, C settings/commands (parallel) | not started |
| S3-2 | Integration edits, gate, advisor pass, bench T-3.1..T-3.9      | not started |

## Decisions carried in (do not re-open)

| ID  | Decision                                                                                   |
| --- | ------------------------------------------------------------------------------------------ |
| D2  | Delivery at `max_hop` 0 on the raw wire field; never through the `--maxhop` clamp (1..6).  |
| D3  | No on-air custody notice. Never emit a `0x41` for a stored DM.                             |
| D4  | Store set `heard` = the existing 12 h mheard table. `own` and `list` are the alternatives. |
| D5  | Per presence trigger: 9-send ladder, then 1 h cooldown; node-wide 1 action / 30 s, 20 / h. |
| D6  | Group messages and broadcasts are never stored.                                            |
| D7  | No pull model, no retrieval request.                                                       |
| T13 | No new field in `struct s_meshcom_settings`. Own NVS keys / spare bits only.               |

## Data model (owner A)

`src/msgstore.h`, `src/msgstore.cpp`, everything behind `#if ENABLE_MSGSTORE` (ESP32-S3 and
nRF52840 only; classic ESP32 compiles nothing).

```
struct MsgStoreEntry {
    char     src[10];        // original sender
    char     dst[10];        // destination, must be in the store set at store time
    uint16_t nnn;            // {NNN transport sequence number
    uint16_t plen;           // stripped payload length
    uint16_t pcrc;           // low 16 bits of crc32 over the stripped payload (T12)
    char     payload[MSGSTORE_PAYLOAD_MAX]; // stripped, re-attached with "{NNN" on delivery
    uint32_t stored_ms;      // first sighting
    uint32_t next_ms;        // next allowed delivery attempt (ladder step or cooldown end)
    uint8_t  attempt;        // 0..9 within the current ladder cycle
    uint8_t  cycles;         // completed ladder cycles (informational, page column)
    uint8_t  state;          // FREE, HELD, ARMED (jitter running), LADDER, COOLDOWN
};
```

Slot count `--storeslots` (default 50 on S3/nRF52, build-time max 100). Payload cap 160 B
(the DM text limit), so one slot is ~190 B and 50 slots ~9.5 kB.

State machine per entry:

```
HELD ──presence trigger (frame heard DIRECTLY from dst, T8 guard)──> ARMED (jitter 5-60 s)
ARMED ──peer delivery heard for (src,nnn)──> HELD            ARMED ──jitter over──> LADDER
LADDER: attempt k at D5 spacing (40/40/40, +60, 40/40/40, +60, 40/40/40), one frame each
LADDER ──9 attempts done──> COOLDOWN (1 h) ──> HELD (waits for the next presence trigger)
any ──:ackNNN from dst for (src,nnn) heard──> FREE (purged by ack)
any ──storetime elapsed──> FREE (dropped by storetime)
any ──sender re-flood (fresh msg_id, same src,nnn,payload)──> refresh stored_ms, back to HELD
```

Node-wide gate before every frame: `bp_state.refusing()` false, 5-min utilisation <= 25 %,

> = 30 s since the last mailbox action, < 20 actions in the trailing hour. A blocked attempt is
> retried on the next loop tick, it does not advance the ladder.

Counters (all `std::atomic<uint32_t>`, printed as one `MBOX` setlog line): stored, refreshed,
delivered, purged_ack, dropped_storetime, dropped_cap (20/h ceiling hit at storetime), dropped_slots
(no free slot at store time), cancelled_peer, blocked_bp.

Hooks in `src/lora_functions.cpp` (owner A, small and labelled):

1. Store: in `OnRxDone()`'s is-new text branch, after the destination check fails for this node
   (i.e. the DM is for someone else) and before the relay decision: if `msgstoreEligible(dst)` then
   `msgstoreStore(...)`. Never for `msg_server` frames (T8 guard).
2. Purge: where `:ackNNN` frames are parsed for other destinations (the relay path sees them),
   `msgstoreOnAck(src_of_ack, nnn)`.
3. Presence: where `updateMheard()` runs, if the path holds exactly one callsign and the frame is
   not `msg_server`, `msgstorePresence(call)`.
4. Peer cancel: a text frame with `max_hop` 0 whose (src, nnn) matches an ARMED/LADDER entry.
5. Delivery: from the loop task only (`msgstoreLoop()` called next to `updateRetransmissionStatus`),
   builds the frame with `encodeAPRS`, source path = original sender + own call appended as a relay
   does, fresh `millis()` msg_id, raw `max_hop` 0, and enqueues with `RING_STATUS_DONE` so the TX
   ring never retransmits it (the ladder is the mailbox's own).

## Mailbox page (owner B)

`/?page=mailbox` in `src/web_functions/web_functions.cpp`, owner-only, never shows payload text.

Columns: destination, last heard (real mheard age, T7), source, NNN, age, hold left, state,
attempts (cycle.attempt), actions (deliver now, purge). Header: role state, store set mode, slots
used/total, the nine counters, RAM note. Footer actions: purge all. Rendered like the messages
page: no per-row heap allocation, fixed buffers, server is synchronous (advisor m10).

Switch confirmation when enabling the role from the GUI: "This node must run 24/7 on continuous
power. Stored messages live in RAM only; a reboot discards all of them without notice." (M7).

Design mockup: `docs/design/mailbox-page-mockup.html` (S3-0 deliverable, matches the existing GUI
palette).

## Settings and commands (owner C)

`--store off|own|list|heard` (default off), `--storecall CALL1,CALL2,…` (list mode, <= 16),
`--storetime <h>` (default 24, 1..168), `--storeslots <n>` (default 50, 1..100). Persisted per T13
through own keys, not struct fields. Printed in `--info`. Enabling from the console prints the same
RAM/24-7 warning as the GUI confirmation. On an ineligible board every setter answers
"store node not available on this hardware" and stores nothing.

## Ownership map (S3-1, one message, three implementers)

| Owner | Exclusive files                                                                      |
| ----- | ------------------------------------------------------------------------------------ |
| A     | `src/msgstore.{h,cpp}`, `src/lora_functions.cpp` (hooks only), `test/test_msgstore/` |
| B     | `src/web_functions/web_functions.cpp`, `src/web_functions/*.h` as needed             |
| C     | `src/command_functions.cpp`, the settings files found in recon, `docs/commands*`     |

Carve-outs the orchestrator applies before dispatch: `ENABLE_MSGSTORE` build flag on the eligible
variants, `+<msgstore.cpp>` and the test in the native env, an interface header
`src/msgstore_api.h` (declarations only) so B and C compile against A's module during the wave.
Loop-task call site (`msgstoreLoop()` in the two platform mains) is applied by the orchestrator at
the gate.

## Gate

Native suite, sequential builds (Heltec V3, RAK4631, T-Beam as the ineligible board, T-Deck
Plus), string scan of the T-Beam image for `mailbox`/`MBOX` (must be absent), advisor pass, then
bench T-3.1..T-3.9 from the plan.

## Recon findings (S3-0, 2026-09-14)

Interface in place before dispatch: `src/msgstore_api.h` (the contract), `src/msgstore.cpp` (link
stub, owner A replaces it), `mheardAgeMs(call)` in `src/mheard_functions.{h,cpp}`,
`ENABLE_MSGSTORE` in `src/configuration_global.h` under the S3/RAK4630 buffer block, native env
entries `test_msgstore` + `+<msgstore.cpp>`.

- **mheard** keys on `msg_source_last` (`mheardCalls[MAX_MHEARD][10]`, `mheardMillis[]`,
  `src/mheard_functions.cpp:36-55`), MAX_MHEARD 80 on S3/RAK, pruned at 12 h
  (`MHEARD_PRUNE_WINDOW_MS`, `:64`, `:307`), evicts oldest by millis. `updateMheard()` is called
  only from `OnRxDone()` (`src/lora_functions.cpp:875`); the server paths never call it, so a
  gateway cannot learn a phantom neighbour through mheard. The presence hook still guards
  `msg_server` (T8) because it lives in OnRxDone before that fact is re-derived.
- **Direct neighbour test:** `aprsmsg.msg_source_path` is the comma-joined hop list
  (`src/aprs_functions.cpp:195-241`); a frame heard straight from its originator has no comma:
  `msg_source_path.indexOf(',') < 0 && msg_source_call == msg_source_last`. Path fields exist only
  for `:` `!` `@` (`:155`).
- **max_hop on the wire:** byte 5, low nibble = hops, high nibble = flags 0x80 server, 0x40 track,
  0x20 app-offline, 0x10 mesh (`src/aprs_functions.cpp:1262-1274`). `initAPRS(':')` seeds
  `max_hop = meshcom_settings.max_hop_text` (`:99-101`); nothing floors it on encode, so the
  delivery sets `aprsmsg.max_hop = 0` (keep the flag bits the relay path would set) after
  `initAPRS`. The relay skips `max_hop == 0` (`src/lora_functions.cpp:1511`, reason `hop0`).
- **Relay re-encode precedent** for the delivery frame: `src/lora_functions.cpp:1511-1568` —
  `msg_source_path` gets `,OWNCALL` appended, `msg_last_hw`, `msg_source_mod` set, then
  `encodeAPRS`. The mailbox mints a fresh `msg_id = millis()` instead of keeping the original.
- **Back-pressure:** `bpCurrentState()` (`src/loop_functions_extern.h:227`, 0 QUIET 1 QRS 2 QRT);
  the mailbox refuses at >= 1. `bp_state.refusing()` is QRT only. 5-minute utilisation:
  `stat_last_window.util_pct` (`:367`).
- **Loop-task call site:** next to `updateRetransmissionStatus()` at `src/esp32/esp32_main.cpp:4091`
  and `src/nrf52/nrf52_main.cpp:1296` (orchestrator applies at the gate).
- **ACK frames seen by a third node:** the `:ackNNN` text frame's source is the acker (the DM's
  destination), its destination the original sender; parse in the relay path where
  `indexOf(":ack")` already runs for non-own destinations.
- **Settings (T13):** ESP32 stores per-key NVS (`preferences.getInt("max_hop_text", …)`,
  `src/esp32/esp32_flash.cpp:181,396`) — own keys `store_mode`, `store_time`, `store_slots`,
  `store_list`. nRF52 writes the whole struct to one LittleFS file
  (`src/nrf52/nrf52_flash.cpp:389`) and has no per-key store, so the store settings get their own
  small file (`/msgstore.cfg`, versioned header, defaults on absence). `max_hop_text` is NOT a
  clean precedent on nRF52 (it is a struct field there).
- **Command patterns:** on|off via `commandCheck(msg_text+2, "mesh on")` + `save_settings()`
  (`src/command_functions.cpp:2602-2631`); numeric with clamp `--maxhop` (`:4481-4519`,
  `maxHopTextValid`, `[MAXHOP];…` print). Web setparam reaches `commandAction()` through
  `webSetup_setParam()` (`src/web_functions/web_setup.cpp:18`).
- **Web GUI:** page dispatch chain `src/web_functions/web_functions.cpp:666-720`, scaffold
  `deliver_scaffold()` `:840`, subheader `_create_meshcom_subheader()` `:2375`, CSS block
  `:1042-1152`, nav buttons `:1173-1180`, actions via `callfunction()` -> `/callfunction/?`
  (`call_function()` `:2522`) and `setvalue()` -> `/setparam/` (`:2558`), all behind the single
  password session (`:590-627`); no per-page owner gate exists, the password IS the owner gate.
  Confirmation precedent: native `confirm()` in onclick (`:1191` reboot). Mockup:
  `docs/design/mailbox-page-mockup.html`.
- **RAM headroom** (tools/resource_baseline.json): Heltec V3 ~211 kB free, RAK4631 ~163 kB. 50
  slots x ~190 B = 9.5 kB, 100 slots 19 kB.
- **Bench:** `--injectraw` replays through `OnRxDone()`; `--airgap` (stage 0) simulates the absent
  destination; RAK-90 store node, Heltec-93 sender, T-Beam-92 relay/ineligible, T-Deck-14
  destination.
