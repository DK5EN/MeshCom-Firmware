# RM: TX power clamp, RM1 repeats, wire-format HTML: plan (2026-10-11)

Status: planned, not started. Base: fork-dev 6bd41daa.

## Decisions (operator, 2026-10-11)

| Question         | Decision                                                                                       |
| ---------------- | ---------------------------------------------------------------------------------------------- |
| Out-of-range fix | Target and sender: target answers `err range <min>..<max>` without a strike; sender clamps     |
| Wire form        | `p=<cur>/<min>/<max>` in the status and radio replies, e.g. `p=2/2/20`                         |
| GUI              | No TX power field returns; the page only words the `err range` / `range` result                |
| HTML doc         | Generator from the `.md` plus a stage-2 staleness check                                        |
| RM1 repeats      | Send once, then repeat 4 times with a new msg_id each, until timeout; a verified reply cancels |
| Run again        | Button removed from the Messages card                                                          |
| Loopback         | Stays refused (`dst` = own call); accepted as is                                               |

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
- PN retry (`src/pn_retry.h`) rotates msg_id by XOR on bits 10-11: only 3 retry ids exist, and the ring-buffer
  retransmit stops on a heard relay or ack (hop level), not on an end-to-end reply.
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
5. **RM1 repeats (wave 3).** The RM layer, not the ring-buffer retransmit, owns the repeats: copy 1 at t=0,
   copies 2-5 at 15, 30, 45 and 60 s; "no answer" stays at 75 s. Every copy is byte-identical in its RM1
   payload (same `ctr`, same tag) and leaves through `sendMessage()` under a fresh msg_id, so relay dedup
   lets it through. Why not the PN XOR form: it has only 3 retry ids (4 are needed), and its stop condition is
   hop level. A verified reply for that `ctr` cancels the remaining copies; so do forget/delete of the target
   and a newer command to the same target. All copies of one `ctr` count as ONE send in the sender budget
   and the sent book. Target: a duplicate is answered from the reply cache, at most once per 10 s per `ctr`,
   never executed twice, never a strike. The repeats also cover a lost reply.
6. **Run again removed (wave 2b).** Button, its arm/confirm logic and its jsdom checks go; the row keeps time,
   command, state and reply.
7. **HTML.** `tools/wire_doc_html.py` (uv, type hints) renders `11-wire-format.md` into the existing page shell
   and CSS, writes `<meta name="source-sha256">`; stage 2 regenerates and fails on a diff.

## Waves and file ownership

| Wave | Content                         | Agent                       | Exclusive files                                                                                                                                                                                    |
| ---- | ------------------------------- | --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1a   | Target: tag before allowlist    | implementer, Sonnet, medium | `src/remote_cmd.cpp/.h`, receiver part of `src/rm_runtime.cpp`, `test/test_remote_cmd/`, `test/test_rm_policy_rx/`, receiver side of `tools/remote_cmd.py`, `tools/tests/remote_cmd_vectors.json`  |
| 1b   | HTML generator (parallel to 1a) | implementer, Sonnet, medium | `tools/wire_doc_html.py` (new), its test, `docs/architecture/11-wire-format.html`                                                                                                                  |
| 2a   | Wire `<min>`, sender clamp      | implementer, Sonnet, medium | `src/rm_sender_policy.h`, `src/rm_format.h`, `src/rm_exec_read.cpp`, sender part of `src/rm_runtime.cpp/.h`, `test_rm_sender_policy`, `test_rm_format`, `test_radio_units`, Python model + vectors |
| 2b   | GUI wording, Run again removed  | implementer, Sonnet, medium | `src/web_functions/web_rm_page.cpp`, `tools/webgui_rm_test.js`                                                                                                                                     |
| 3    | RM1 repeats (after wave 2)      | implementer, Sonnet, medium | `src/rm_runtime.cpp/.h`, `src/rm_sender_policy.h`, `src/remote_cmd.cpp` (duplicate path only), `test_rm_sender_policy`, `test_remote_cmd`, new `test_rm_repeat` if needed, Python model            |
| Gate | Hooks, docs, gate, bench        | orchestrator                | `tools/regression.sh` hook, `11-wire-format.md`, `docs/BACKLOG.md` (RM-STRIKE closed), regenerated HTML                                                                                            |

- Opus advisor (medium) after 1a, 2a and 3; all change behaviour. Wave 3 shares files with 1a and 2a, so it
  runs after wave 2 is committed.
- Regression tests: valid-tag `txpower 21` to a 20 dBm target answers `err range 0..20` and leaves the strike
  count at 0 (fails today); sender refuses `txpower 1` once a RAK range is learned; a verified reply after
  copy 2 leaves no copy 3 in the TX queue; five copies of one `ctr` use one budget slot; a duplicate within
  10 s gets no second reply and no strike.

## Bench (stays at or below 2 dBm)

- `identity_guard.py` first; flash the four bench nodes, DK5EN-98 only on request.
- DK5EN-92 to DK5EN-90 (RAK, floor 2): `radio` read shows `p=2/2/22`; `txpower 1` refused locally with `range`;
  `txpower 2` answers `ok txpower=2`.
- Repeats: DK5EN-92 sends `radio` to DK5EN-1 with DK5EN-1 briefly unreachable (WiFi off is not enough, so
  power it off for 20 s): the 2323 console of DK5EN-92 shows copies with different msg_ids and no copy after
  the verified reply. Both nodes at 2 dBm.
- Upper limit tested on the host only: a real out-of-range send would risk a node transmitting above 2 dBm.

## Estimate

About $22-30, 3-3.5 h wall-clock including the gate and bench.
