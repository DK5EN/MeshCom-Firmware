# RM: TX power clamp, RM1 repeats, wire-format HTML: plan (2026-10-11)

Status: wave 1 done (1a + 1b, one commit), wave 2 next. Base: fork-dev 6bd41daa.

## Progress

| Wave | State                                                                                                                                                                                                                                                                                                                                   |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1a   | Done 2026-10-11. `RM_REFUSED` (valid tag, allowlist decided last, answered, no strike, counter consumed); target txpower range `max(0, TX_POWER_MIN)..TX_POWER_MAX`; `rmRefusalText`; refusals counted in `rej_blocked`; a valid-tag `sync` with args stays a `blocked` strike. Sender still passes min 0. Advisor APPROVED.            |
| 1b   | Done 2026-10-11. `tools/wire_doc_html.py` + `tools/wire_doc_template.html`; stage 2 runs `--check` and `--self-test`, pytest gets `--with markdown-it-py==3.0.0`.                                                                                                                                                                       |
| 2a   | Next. Recon: `p=` built in `src/rm_format.h:319` (radio) and `src/rm_sender_policy.h:484` (status, parser `rmStatusParse` :491, `p=` :528); sender has no per-target range store yet (candidates `RmProof` `rm_sender_policy.h:583`, `PeerHwm` `rm_runtime.cpp:81`); local refusal token table `rmTokenTable` `rm_sender_policy.h:333`. |
| 2b   | Next. Run again: button `web_rm_page.cpp:252`, `rmAgain` :360, confirm helpers :159/:209, dispatch :390; jsdom checks `tools/webgui_rm_test.js` :956, :1324-1389 and later hits. GUI words errors via the server's `msg` (`rmErrText` :168).                                                                                            |
| 3    | Open. Settled: the reply-cache lookup in `rmCheck` precedes the rate check, so a later copy of the same `ctr` is `RM_CACHED`, never `RM_REJ_RATE`, never a strike.                                                                                                                                                                      |

Bench to-do from wave 1: confirm `[RM];refused;ctr;<n>` and the `err range` reply on hardware (the `rmDrain` glue has no host test).

## Decisions (operator, 2026-10-11)

| Question         | Decision                                                                                                     |
| ---------------- | ------------------------------------------------------------------------------------------------------------ |
| Out-of-range fix | Target and sender: target answers `err range <min>..<max>` without a strike; sender clamps                   |
| Wire form        | `p=<cur>/<min>/<max>` in the status and radio replies, e.g. `p=2/2/20`                                       |
| GUI              | No TX power field returns; the page only words the `err range` / `range` result                              |
| HTML doc         | Generator from the `.md` plus a stage-2 staleness check                                                      |
| RM1 repeats      | PN pattern: send once, 3 repeats on the 40 s ladder, msg_id bits 10-11 XOR-rotated; a verified reply cancels |
| Repeat test      | `--airgap on/off` (INSTRUMENT, RAM only: drops all RX and own TX) on target or sender, never a power-off     |
| Bench suite      | gwbench twice: LoRa only, then both nodes as gateways (internet path expected to be fast)                    |
| Stop rule        | Only the verified reply stops the repeats; relay echo restarts the wait as for PMs, hop ACKs do not stop     |
| Reply repeats    | None: a lost reply is recovered by the next command copy hitting the reply cache                             |
| GW-run criterion | Measure only: reply time per command, winning path, late copies; no time threshold                           |
| Run again        | Button removed from the Messages card                                                                        |
| Loopback         | Stays refused (`dst` = own call); accepted as is                                                             |

## Facts the plan rests on

- Sender checks `txpower` against its OWN `TX_POWER_MAX` (`src/rm_runtime.cpp:881`, `rmCommandAllowed`).
- Target allowlist accepts `0 <= v <= TX_POWER_MAX`, 1-3 digits, no sign (`src/remote_cmd.cpp:173-176`); the
  console then enforces `TX_POWER_MIN` (`src/command_functions.cpp:5022`), so `txpower 0` to a RAK gives
  `err failed` (no strike).
- `RM_REJ_BLOCKED` / `RM_REJ_FORMAT` are decided before the tag check (`rmCheck`, `remote_cmd.cpp:572`): a
  valid-tag out-of-range command is a strike (BACKLOG RM-STRIKE).
- RM1 frames go out once, no `{NNN`, no ack, no DM statistics (`src/loop_functions.cpp:4359`, W0c). The
  sender marks a command "no answer" after `RM_NOANSWER_MS` 75 s (`src/rm_sender_policy.h:38`).
- Target: the same `ctr` with the same valid tag within 10 min re-sends the cached reply and does not
  execute again (wire doc 1.9.5 "Lost-reply recovery"). To verify in wave 3: whether `RM_RATE_MS` (10 s)
  rejects such a duplicate before the cache lookup, and whether it can ever count as a strike.
- PN retry (`src/pn_retry.h`) rotates msg_id by XOR of bits 10-11 with k = 1..3, always from the original id;
  `msg_id >> 12` stays stable. The ring retransmit fires on a fixed 40 s ladder (`lora_functions.cpp:3055`,
  threshold 0x15 ticks of 2 s): copies at 0, 40, 80, 120 s. Eligibility today needs a PN (`pnFrameIsOwnPn`,
  payload ending `{NNN`); RM1 frames have no `{NNN`, so they are never retried. Stop today: `:ackNNN`, a
  gateway binary ACK, or the relay echo (restarts the wait).
- `sendMessage()` can return the frame's msg_id (`out_msg_id`, `loop_functions.cpp:4050`).
- gwbench (`tools/bench/gwbench/`) runs every RM command both ways between DK5EN-1 and DK5EN-90 as gateways
  against a mock server on rpizero (INSTRUMENT image for `--srvip`, about 40 min). It has no LoRa-only mode.
  The 2026-10-07 run measured about 2.2 s per RM command (`docs/rm-gui/gateway-bench-20261007.md`).
- Board ranges: default -9..22; RAK4631, Wireless Paper, Vision Master floor 2; T-Beam and LoRa32 v2.1 max 20,
  floor -4; Heltec V2 max 15, floor -4. Remote floor = `max(0, TX_POWER_MIN)`.

## Design

1. **Target (closes RM-STRIKE).** Verify the tag before the allowlist. A valid tag with a blocked or malformed
   command is answered (`err range <min>..<max>` for `txpower`, `err blocked` otherwise), counts no strike, and
   consumes the counter like any answered command. A bad tag stays a strike as today.
2. **Wire.** Status and radio replies carry `p=<cur>/<min>/<max>`; `<min>` is the remote floor
   `max(0, TX_POWER_MIN)`. Worst-case status length re-checked against the 108-character wire cap.
3. **Sender.** Learns `min`/`max` per target in RAM from every verified status or radio reply. A `txpower`
   outside the learned range is refused locally with `range` (GUI, HTTP endpoint, console). Unknown range
   (fresh boot): accept 0..22 and rely on step 1, so no Re-sync is needed.
4. **GUI.** Words `err range a..b` as "Refused: <call> accepts a..b dBm." and the local `range` refusal the same
   way. No input field.
5. **RM1 repeats (wave 3), same pattern as PNs.** RM1 frames become eligible for the existing ring retransmit:
   copy 1, then 3 repeats on the 40 s ladder, each with msg_id bits 10-11 XOR-rotated by `pnRetryId(orig, k)`
   (never a new msg_id; `msg_id >> 12` unchanged). The RM1 payload of every copy is byte-identical (same
   `ctr`, same tag). Cancel: the RM layer stops the slot (`pnRetryCore` match on the original id) as soon as a
   reply for that `ctr` verifies; also on forget/delete of the target. No `{NNN`, no `:ack`, no DM statistics,
   as today. "No answer" moves from 75 s to the end of the ladder (about 160 s); `RM_POLICY_WINDOW_MS` is
   re-derived so the unanswered-send budget still covers the whole ladder. All copies of one `ctr` are ONE
   send in the budget and the sent book. Target: a later copy is answered from the reply cache, never executed
   twice, never a strike; the repeats therefore also cover a lost reply.
6. **Run again removed (wave 2b).** Button, its arm/confirm logic and its jsdom checks go; the row keeps time,
   command, state and reply.
7. **HTML.** `tools/wire_doc_html.py` (uv, type hints) renders `11-wire-format.md` into the existing page shell
   and CSS, writes `<meta name="source-sha256">`; stage 2 regenerates and fails on a diff.

## Waves and file ownership

| Wave | Content                                          | Agent                       | Exclusive files                                                                                                                                                                                                                                                                                        |
| ---- | ------------------------------------------------ | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1a   | Target: tag before allowlist                     | implementer, Sonnet, medium | `src/remote_cmd.cpp/.h`, receiver part of `src/rm_runtime.cpp`, `test/test_remote_cmd/`, `test/test_rm_policy_rx/`, receiver side of `tools/remote_cmd.py`, `tools/tests/remote_cmd_vectors.json`                                                                                                      |
| 1b   | HTML generator (parallel to 1a)                  | implementer, Sonnet, medium | `tools/wire_doc_html.py` (new), its test, `docs/architecture/11-wire-format.html`                                                                                                                                                                                                                      |
| 2a   | Wire `<min>`, sender clamp                       | implementer, Sonnet, medium | `src/rm_sender_policy.h`, `src/rm_format.h`, `src/rm_exec_read.cpp`, sender part of `src/rm_runtime.cpp/.h`, `test_rm_sender_policy`, `test_rm_format`, `test_radio_units`, Python model + vectors                                                                                                     |
| 2b   | GUI wording, Run again removed                   | implementer, Sonnet, medium | `src/web_functions/web_rm_page.cpp`, `tools/webgui_rm_test.js`                                                                                                                                                                                                                                         |
| 3    | RM1 repeats (after wave 2)                       | implementer, Sonnet, medium | `src/pn_retry.h`, the RM1 branch of `src/loop_functions.cpp` (~4359), retry eligibility and stop in `src/lora_functions.cpp`, `src/rm_runtime.cpp/.h`, `src/rm_sender_policy.h`, `src/remote_cmd.cpp` (duplicate path only), `test_pn_retry`, `test_rm_sender_policy`, `test_remote_cmd`, Python model |
| 3t   | gwbench LoRa-only mode, analyzer (parallel to 3) | implementer, Sonnet, medium | `tools/bench/gwbench/*.py`, `tools/bench/gwbench/README.md`; `--airgap` steps for the repeat cases                                                                                                                                                                                                     |
| Gate | Hooks, docs, gate, bench                         | orchestrator                | `tools/regression.sh` hook, `11-wire-format.md`, `docs/BACKLOG.md` (RM-STRIKE closed), regenerated HTML                                                                                                                                                                                                |

- Opus advisor (medium) after 1a, 2a and 3; all change behaviour. Wave 3 shares files with 1a and 2a, so it
  runs after wave 2 is committed.
- Regression tests: valid-tag `txpower 21` to a 20 dBm target answers `err range 0..20` and leaves the strike
  count at 0 (fails today); sender refuses `txpower 1` once a RAK range is learned; a verified reply after
  copy 2 leaves no copy 3 in the ring; copy k carries `pnRetryId(orig, k)` and the same payload; four
  copies of one `ctr` use one budget slot; a later copy at the target gets the cached reply, no second
  execution, no strike; a reply arriving twice (server and RF) verifies once.

## Bench (stays at or below 2 dBm)

- `identity_guard.py` first; flash the four bench nodes, DK5EN-98 only on request.
- DK5EN-92 to DK5EN-90 (RAK, floor 2): `radio` read shows `p=2/2/22`; `txpower 1` refused locally with `range`;
  `txpower 2` answers `ok txpower=2`.
- Repeats, lost command: `--airgap on` on the target, send, `--airgap off` after copy 1; copy 2 (40 s, id with
  bits 10-11 flipped) executes and verifies, no copy 3 follows.
- Repeats, lost reply: `--airgap on` on the sender right after copy 1 left; the target executes and replies
  into the gap; `--airgap off` before 40 s; copy 2 gets the cached reply (no second execution in the target
  log) and verifies.
- gwbench, run 1, LoRa only (DK5EN-1 and DK5EN-90, gateways off): every RM command both ways, repeats only
  where a frame was lost.
- gwbench, run 2, both nodes as gateways against the mock: reply via the server, expected fast (2.2 s per
  command on 2026-10-07); measured only, no threshold. Late LoRa copies of command or reply must be absorbed: no second
  execution, no strike, no second "verified" row, no stray repeat after the reply. The analyzer reports per
  command the reply latency, the path that won and every late copy.
- Both runs need the INSTRUMENT image on the pair (run 2 for `--srvip`); the stock image goes back afterwards.
- Upper limit tested on the host only: a real out-of-range send would risk a node transmitting above 2 dBm.

## Estimate

About $30-40, 4.5-5 h wall-clock: waves about 2.5 h, gate 20 min, two gwbench runs about 80 min plus flashing.
