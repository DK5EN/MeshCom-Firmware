# DM stage 1 — Fable Verdict (2026-09-14)

Subject: commit `731e0ebc` (wave S1-1), diff `86974a8e..731e0ebc`. Spec: `docs/dm-stage1-plan-20260914.md`
sections 1-5, 8 and the S1-1 notes; traps T1/T2/T2b/T11 in
`docs/dm-reliability-and-store-node-verdict-20260913.md`. Native: `pio test -e native -f test_dm_outbox
-f test_dm_stats` = 29/29 green (22 + 7). Line numbers are the tree at `731e0ebc`.

## Verdict: APPROVED (after rework `1595542c`; the original `731e0ebc` verdict below was REWORK)

One High (T2 is not solved at the ack site — the outbox stop sits behind `checkOwnTx()`), two Medium
(server-path acks never reach the outbox; every fresh-id attempt is re-uploaded to the server), five
Low. The
core (`dm_outbox.cpp`), the glue frame rebuild, the settings module and the sendMessage hook are
correct as specified. Mode `off` is byte-identical to today.

## Finding 1: outbox ack stop is gated on `checkOwnTx(first_id)` — T2 not solved

- **File:** `src/lora_functions.cpp:1121-1144` (`int iackcheck = checkOwnTx(msg_counter); if(iackcheck >= 0) { ... dmOutboxOnAck(...); }`)
- **Severity:** High
- **Failure scenario:** `dmOutboxOnAck()` is the last statement inside `if(iackcheck >= 0)`.
  `own_msg_id[]` is `MAX_RING` = 20 slots (`src/loop_functions.cpp:429`); every fresh-id attempt
  burns one (`dm_outbox_glue.cpp:661`), positions burn one (`loop_functions.cpp:3403, :3505`), and a
  gateway inserts one per server frame forwarded to LoRa (`src/udp_functions.cpp:572`,
  `src/nrf52/nrf_eth.cpp:748`). Once `first_id` has rotated out, the destination's `:ackNNN` still
  produces the 0x02 phone frame (`:1144`, unconditional, `msg_counter == first_id` by
  construction) — the app shows "acked" — but the outbox is never told and transmits the remaining
  attempts to give-up, then `glueReport(0x03)` sends a failed frame for a message the app already
  saw acked (`checkOwnTx` misses there too, so the "never downgrade 0x02" guard at
  `dm_outbox_glue.cpp:702-704` cannot fire). This is exactly trap T2 and bench T-1.3 expects the
  opposite ("ACK still attributed via the outbox, ladder stops"). On a gateway sender with mode 9,
  20 server frames in nine minutes is the normal case, not the corner.
  Same gating pattern, lower stakes: `dmOutboxOnHeld()` at `:1152-1153` is behind
  `iStoCheck >= 0`, and `dmOutboxOnEcho()` at `:1123-1124` is behind `icheck >= 0` (degrades the
  echo gate to a same-id attempt 2, harmless).
- **Fix:** Move `dmOutboxOnAck(aprsmsg.msg_source_call.c_str(), (uint16_t)(iAckId & 0x3FF))` out of
  the `iackcheck >= 0` block, directly after `msg_counter` is built (`:1110`), and when it returns
  true with `iackcheck < 0` still do `dmstat_peer_ack`/`dmStatNoteAck()` and `stoHolderClear()`.
  Same move for `dmOutboxOnHeld(stoNnn)` (outbox keyed on NNN, `checkOwnTx` not needed). Native
  regression is not possible at this site; T-1.3 is the proof.

## Finding 2: an ack that arrives over the server never stops the ladder

- **File:** `src/udp_functions.cpp:428-437`, `src/nrf52/nrf_eth.cpp:594-603` (unchanged by the diff)
- **Severity:** Medium
- **Failure scenario:** Both server ack arms set `own_msg_id[][4] = 0x02` and emit the phone frame,
  but contain no `dmOutboxOnAck()`. A gateway sender whose destination answers through the server
  (destination out of RF range, or the RF copy of the ack lost) keeps laddering: up to eight more
  transmissions on air and (Finding 3) eight more uploads, then a 0x03 that is suppressed only while
  `first_id` is still in `own_msg_id[]`.
- **Fix:** `dmOutboxOnAck(aprsmsg.msg_source_call.c_str(), (uint16_t)(iAckId & 0x3FF))` in both
  server ack arms, unconditional on `iackcheck` (same rule as Finding 1).

## Finding 3: every fresh-id attempt is uploaded to the server as a new message

- **File:** `src/dm_outbox_glue.cpp:668-675` (`addNodeData()` / `sendExtern()` per attempt)
- **Severity:** Medium
- **Failure scenario:** The glue mirrors `sendMessage()`'s upload (`loop_functions.cpp:4308-4316`),
  which today happens once per DM — the ring's same-id retries in `doTX()` are never re-uploaded.
  With mode 9 on a gateway, the server, mcmap and every phone on every other gateway receive up to
  nine distinct-id copies of one DM; the server-side dedup is by msg_id and the stage 2.1 fold
  exists only on this firmware's ingress paths. Retries buy nothing on the server leg (attempt 1
  already reached it) and the plan says nothing about re-uploading (section 4 step 3 lists
  re-encode, `insertOwnTx`, enqueue). Same class as the "OE1XAR-62 BBS posts were gwflood" incident.
- **Fix:** Drop the two upload calls from `glueTransmit()`; the EXTUDP mirror too (a bench listener
  on 1799 would otherwise count retries as sends). Attempt 1's upload in `sendMessage()` stands.

## Finding 4: outbox give-ups are invisible to the stage 0 DM counters

- **File:** `src/dm_outbox_glue.cpp:685-709` vs `src/lora_functions.cpp:2461-2475`
- **Severity:** Low
- **Failure scenario:** In modes 3/9 the ring give-up path is unreachable (0xFF slots), and
  `glueReport()` neither increments `dmstat_giveup` nor `dmstat_giveup_held`; the core skips
  `report()` entirely when held (`dm_outbox.cpp:473`). The setlog `DM giveup=/giveuph=` fields go to
  zero for laddered DMs while `OUTBOX give=` carries the count — the log-analysis tooling keyed on
  the DM line under-reports.
- **Fix:** `dmstat_giveup.fetch_add(1)` in `glueReport()`; for the held subset either call
  `report()` unconditionally and let the glue suppress the frame on `e->held`, or add a counter
  hook. Adjust `test_held_suppresses_giveup_report_but_still_counts` accordingly.

## Finding 5: a queued fresh-id attempt is not stopped by the ack

- **File:** `src/lora_functions.cpp:1129` (`findAndStopRingSlot(msg_counter)`), plan section 4 step 4
- **Severity:** Low
- **Failure scenario:** The ring stop is by `first_id`; an attempt 3..9 slot still waiting in the
  ring (CSMA backoff, QRS latch) when the ack lands goes out afterwards — one extra transmission
  per ack. The plan's "the queued ring slot is stopped by id" is met only for attempts 1-2.
- **Fix (optional):** a `dmOutboxLastIdForNnn()` reader and a second `findAndStopRingSlot()` at the
  ack site; or document the one-extra-frame cost.

## Finding 6: doc claims that the tree does not match

- **File:** `docs/dm-stage1-plan-20260914.md` S1-1 notes ("echo, ack and held hooks only when the
  mode is not off"), commit message
- **Severity:** Low (documentation)
- **Failure scenario:** All three hooks are unconditional (`lora_functions.cpp:1123-1124`, `:1141`,
  `:1152-1153`) and `dmOutboxLoop()` runs regardless of mode (`esp32_main.cpp:2168, :4114`,
  `nrf52_main.cpp:1315`). That is harmless — no entry exists in mode `off` — but a ladder in flight
  keeps running after `--dmretry off`, and the note says otherwise.
- **Fix:** Correct the notes ("hooks are mode-independent no-ops; an in-flight ladder completes
  after switching off").

## Finding 7: two native tests cannot fail under the bug they name

- **File:** `test/test_dm_outbox/test_main.cpp:273-289`, `:386-399`
- **Severity:** Low
- **Failure scenario:** `test_echo_on_last_id_also_gates` echoes id 500, which is also `first_id`,
  so the `last_id` arm of `dmOutboxOnEcho()` (`dm_outbox.cpp:307`) is never exercised — deleting
  `&& last_id != msg_id` passes. `test_held_on_unknown_nnn_is_a_no_op` never asserts that the add
  succeeded, so its loop is vacuous if it did not. Not covered at all: "one enqueue per loop call"
  with two due entries, and the earliest-`next_ms` selection.
- **Fix:** echo a fresh `last_id` after a fresh attempt; assert the add slot; add a two-entry tick test.

## Finding 8: the QRT nack reuse has two side effects the plan did not list

- **File:** `src/loop_functions.cpp:4115` → `bpEmitNack()` `:3748-3805`
- **Severity:** Low
- **Failure scenario:** `bpEmitNack()` prints `[BP];nack;QRT;dst;...` (`:3778-3784`) — the bench
  tooling (`tools/loganalyse.sh`, `serial_monitor.py`) counts that as a back-pressure refusal, so an
  outbox-full refusal inflates the BP statistics; and it rewrites `bp_episode_origin/dst`
  (`:3798-3802`, BP-06 mirror), so a QRT episode opened for another transport gets its closing QRV
  routed to the outbox-refused sender. The app text is plan-approved; the marker and the episode
  routing are not.
- **Fix:** Emit the notice through `bpDeliver()` with the QRT prefix but without the `[BP];nack`
  marker and without touching the episode fields (a small `outboxEmitRefuse()` next to
  `bpEmitNack()`), keeping `[OUTBOX];refuse;full` as the only marker.

## Confirmations (per question)

1. **Mode off byte-identical — CONFIRMED.** `sendMessage()`: refuse check `:4111` short-circuits on
   `dmRetryMode()` before `dmOutboxHasRoom()`; `user_msg_status` takes the old `0x00` arm (`:4190-4193`);
   `dmOutboxAdd()` not reached (`:4290`); the frame is encoded at `:4170` before any S1 code; no
   print. Setlog `OUTBOX` line gated (`:3248`). Receive hooks are no-ops with an empty table. The
   `--info` line and the GUI select/warning are the only visible additions.
   **Peer-delivery tell — CONFIRMED against the three shapes:** `msg_source_call` is the first
   path element, `msg_source_last` the last (`aprs_functions.cpp:239-241`); relays append
   themselves (`lora_functions.cpp:1733, :1739`). Own hop-0 DM "SRC": no comma → `bMboxPathTwoCalls`
   false → never matches. Relayed "SRC,RELAY": `rly_hop > 0` normally → no match; hop exhausted
   (`max_hop_text` 1) → false positive, accepted by plan section 5. Store delivery "SRC,STORE"
   hop 0 → matches. UNVERIFIABLE-HERE: a gateway-emitted server frame is "SRC,GW"
   (`udp_functions.cpp:356-357`) with the server's hop value untouched (`:509` only ORs 0x20); the old
   tell excluded it via the msg_id low bits, the new one does not — if the server hands out hop 0
   that is a peer-cancel false positive. Bench item.
2. **Core — CONFIRMED.** Offsets `dm_outbox.cpp:189-210` (`k3`, `k9`), absolute from `first_ms`
   (`:275`, `:478-480`). Echo gate only at `next_attempt == 2` (`:434`), attempts 3+ fresh. One
   transmit per call, earliest `next_ms` wins (`:394-406`). bp blocks without advancing, counted per
   episode (`:414-424`). `transmit()` false returns before any state change (`:452-454`). Ack needs
   `nnn` and `strcmp(dst, from)` (`:322-327`), frees immediately. Give-up after `max_attempts`,
   `report(0x03)` unless held (`:467-475`), freed next tick (`:379-386`). Wrap-safe `(int32_t)`
   compares (`:399, :401`). Gen/re-entrancy `:449-457`. No leak path: every non-FREE state is LADDER
   (freed by ack/give-up) or DONE_GIVEUP (freed next tick); DONE_ACK is never assigned. NNN sharing
   needs 1000 DMs inside one 9-min ladder; consequence: `dmOutboxOnAck()` stops the first matching
   slot only, `OnHeld` marks both, `FirstIdForNnn` returns the older — bounded to a runaway sender.
3. **Glue — CONFIRMED except Finding 3.** Same `initAPRS(':')`, `msg_source_path = node_call`,
   destination path/call, `checkVia()`, `encodeAPRS()` (`dm_outbox_glue.cpp:627-644` vs
   `loop_functions.cpp:4128-4170`). `e->max_hop` is `aprsmsg.max_hop` BEFORE flag bits: flags are
   added inside `encodeAPRS()` from `msg_server/msg_track/msg_app_offline/bMESH`
   (`aprs_functions.cpp:1262-1274`), which the glue's `initAPRS` seeds identically → byte 5 on the
   wire is the same. Payload `"%03u"` vs `"%03i"` identical for 0..999; `e->payload` is the
   `{`→`(` escaped text (`:4147` runs before the `dmOutboxAdd` at `:4290`). `addTxRingEntry(buf,
len, 0xFF, "dm_retry")` (`:653`), `insertOwnTx()` (`:661`), `addLoraRxBuffer()` mirrored
   (`:663-666`). No `node_msgid++`, `save_settings()`, `dmStatNoteSent()`, no BLE echo of the text,
   no `tdeck_add_MSG()`. `glueReport()`: `buildAckPhoneFrame(first_id, status, dst)`, never
   downgrades 0x02, never overwrites 0x04 with 0x03 (`:698-708`).
4. **sendMessage hook — CONFIRMED.** Refuse at `:4111-4116` precedes the msg_id mint (`:4134`),
   `node_msgid++` (`:4161`), `save_settings()`, `insertOwnTx()`, `addLoraRxBuffer()`. QRT nack: the
   app receives "QRT NOT SENT - <text>" (`backpressure.h:102`), the console gets
   `[OUTBOX];refuse;full` plus `[BP];nack;QRT`; the app cannot tell outbox-full from channel-QRT.
   Plan section 3 / decision 3 chose this — accepted, but see Finding 8 for the two side effects
   the plan did not list. 0xFF slots transmit once:
   `getNextTxSlot()` selects READY or DONE (`txring_functions.cpp:191`), the same path
   `SendAckMessage()`'s 0xFF acks take. `nnn = msg_id & 0x3FF` equals the payload NNN: both derive
   from `node_msgid` before the increment (`:4134`, `:4151`, `:4161`).
5. **Receive hooks — REFUTED for T2, see Finding 1.** Echo at the HEARD site with the heard
   frame's id (`:1123-1124`), matches `first_id` or `last_id`. `dmOutboxOnAck()` inside
   `iackcheck >= 0` (`:1121`, `:1141`). Attempt 1 is minted `(_GW_ID<<10)|NNN` (`:4134`), so the
   reconstructed id always equals `first_id` — the mapping is right, the gate is the problem. Held
   hook `:1152-1153`, same gate. Fresh-id echoes never produce a phone HEARD frame
   (`ackMsgIdFromNode()` fails on a millis() id, `ack_attribution.h:90-93`) — good.
6. **Settings/commands — CONFIRMED.** `#ifndef NATIVE_BUILD` guard with no-op Load/Save
   (`dm_settings.cpp:778, :914-920`), `platformio.ini:229` defines it and `:249-250` link both
   modules. ESP32 own `Preferences` handle, key `dm_retry`, out-of-range → off (`:794-813`). nRF52
   `/dm.cfg` with magic, size check, memcmp guard (`:828-904`). Prefix order `"dmretry "` before
   `"dmretry"` (`command_functions.cpp:4644, :4661`), no other `dm*` command. Warning printed once on
   off→on (`:268-284`). `--info` line `:6339`. GUI select + `mbx-warn` sentence
   (`web_functions.cpp:2028-2047`), info page `:2418`. `setparam`/`getparam` through
   `commandAction()` like `store` (`web_setup.cpp:511-519, :981-984`). Slot macro matches the
   msgstore table (`configuration_global.h:246`). Rendering UNVERIFIABLE-HERE.
7. **Tests — see Finding 7.** The schedule test asserts absolute due times
   (`kExpectedMs[k] == e->next_ms` from `first_ms = 0`, `:215-217`) and the not-yet-due tick at
   `next_ms - 1`.
8. **Native run:** 29/29 green (`test_dm_outbox` 22, `test_dm_stats` 7).

## Refuted claims (do not re-investigate)

- **"`e->max_hop` carries flag bits or differs from the wire."** Refuted: flags are applied by
  `encodeAPRS()` from `aprsMessage` fields, not stored in `max_hop` (`aprs_functions.cpp:1262-1274`).
- **"The outbox NNN and the payload NNN can differ."** Refuted: same `node_msgid` read before the
  increment (`loop_functions.cpp:4134, :4151, :4161`).
- **"A 0xFF slot that was never sent is skipped by the ring."** Refuted: `getNextTxSlot()` accepts
  DONE (`txring_functions.cpp:191`); acks have always gone out this way.
- **"A relayed fresh-id attempt triggers a phone HEARD for an unknown id."** Refuted:
  `ackMsgIdFromNode()` rejects millis() ids (`ack_attribution.h:90-93`).
- **"An entry can be stuck forever."** Refuted: states are FREE/LADDER/DONE_GIVEUP only; LADDER
  ends in ack (freed) or give-up (freed next tick).
- **"`dmOutboxAdd()` races the LORA task on nRF52."** Refuted as new: every `sendMessage()` caller
  is loop-context (`nrf52_main.cpp:1695, :3019`, extudp, web); the loop-vs-hook race is the
  stage 3 Finding 4 pattern and is covered by the gen snapshot (`dm_outbox.cpp:449-457`, tested).
- **"The `Credentials` namespace handle collides."** Refuted in the stage 3 verdict; same
  begin/end-per-call pattern here.
- **"The sender's own hop-0 DM matches the peer-delivery tell."** Refuted: needs a comma in the
  path (`lora_functions.cpp:1314`); a sender's own frame has none.

## Bench-only items (map to T-1.1..T-1.9)

- T-1.1 / T-1.2: fresh-id attempts reach a two-hop destination, each re-acked; ack on attempt 2
  stops the rest — after Finding 1/2 are fixed, verify the `[RETX]`-free stop and `OUTBOX ack=`.
- T-1.3: the Finding 1 proof — sender beaconing so `first_id` leaves `own_msg_id[]`; ladder must
  stop on the ack. Also run the gateway variant (server frames rotate the table faster).
- T-1.4: echo gate with/without relay; note the echo hook is also behind `checkOwnTx()`.
- T-1.5: QRT latched — no attempt, `OUTBOX bp=` counts one episode.
- T-1.6: store node refresh on a fresh-id attempt, no peer cancel; add the "SRC,GW" hop-0 case from
  Confirmation 1 (server-emitted copy at a store node).
- T-1.7: `--dmretry off` byte identity — capture on the control node, diff against a pre-S1 build.
- T-1.8: upstream 4.35t destination shows three copies, acks matched here.
- T-1.9: outbox full — expect a refuse with the QRT wording and `[OUTBOX];refuse;full`, not "sent
  once as today" (the plan table still says the pre-decision-3 behaviour; update it).
- Finding 3 check: with a gateway sender in mode 9, count copies at the server (mcmap
  `messages_query`) before and after the fix.

## Re-check (1595542c)

Diff `731e0ebc..1595542c`. Native: `pio test -e native -f test_dm_outbox -f test_dm_stats` 32/32
(outbox 25, stats 7), `pio test -e native_aprs -f test_bp_notice_frame` 19/19 (the suite lives in
`native_aprs`, `platformio.ini:257-270`). Line numbers are the tree at `1595542c`.

- **F1 — CONFIRMED fixed.** `dmOutboxLastIdForNnn()` and `dmOutboxOnAck()` run at
  `src/lora_functions.cpp:1130-1131`, before and independent of `checkOwnTx()` at `:1141`; the
  bookkeeping block is `if(iackcheck >= 0 || dmAckStopped)` (`:1142`) with the `own_msg_id[]` write
  guarded on `iackcheck >= 0` (`:1144-1145`), `stoHolderClear`/`dmstat_peer_ack`/`dmStatNoteAck`/
  `findAndStopRingSlot(msg_counter)` inside it. The 0x02 frame: `print_buff` is built at `:1110`
  for `msg_counter` (`== first_id` by construction) and `addBLEOutBuffer(print_buff, plen)` at
  `:1178` sits after the block, unconditional — the writer's claim is correct, the phone gets 0x02
  even when `own_msg_id[]` evicted `first_id`. `dmOutboxOnHeld(stoNnn)` unconditional at `:1197`.
  F5: `dmLadderLastId` read before the free, second `findAndStopRingSlot()` at `:1133-1139` only
  when `dmAckStopped && last_id != 0 && last_id != msg_counter`. New native
  `test_f1_ack_on_nnn_dst_stops_ladder_after_several_fresh_ids` pins the core side; the call-site
  side stays bench (T-1.3).
- **F2 — CONFIRMED fixed.** `src/udp_functions.cpp:434-437` and `src/nrf52/nrf_eth.cpp:600-603`:
  `dmOutboxOnAck()` unconditional, `if(iackcheck >= 0 || dmAckStopped)`, `own_msg_id[]` write
  guarded, `ack_status`/`print_buff[5]` upgraded to 0x02 inside the block. No ring stop for
  `last_id` on the server twins — consistent with the pre-existing twins, which never stopped the
  ring for `msg_counter` either; one extra frame at most.
- **F3 — CONFIRMED fixed.** No `addNodeData()`/`sendExtern()` left in `src/dm_outbox_glue.cpp`
  (includes removed `:20-21`, comment only at `:104-115`).
- **F4 — CONFIRMED fixed.** `dm_outbox.cpp:365-366` calls `report()` for every give-up;
  `glueReport()` counts `dmstat_giveup` (+ `dmstat_giveup_held` when `e->held`) at
  `dm_outbox_glue.cpp:138-143`, then returns before the frame for held (`:147-148`), so no 0x03 on
  the wire and no `own_msg_id[]` 0x03 write for a held message. Test updated to assert the held
  report call (`test_main.cpp:553-556`).
- **F6 — CONFIRMED.** Plan S1-1 note corrected and an S1-2 rework section added
  (`docs/dm-stage1-plan-20260914.md`); code unchanged, as it should be.
- **F7 — CONFIRMED sharp.** `test_echo_on_last_id_also_gates` (`:275-305`) runs mode 9 to attempt 3
  (always fresh), asserts `last_id != first_id` and `echo_seen == false`, then echoes `last_id` —
  deleting the `last_id` disjunct at `dm_outbox.cpp:307` fails it. `test_held_on_unknown_nnn`
  asserts the add slot and reads the entry by slot (`:391-400`). New two-entry test (`:406-431`):
  A due 40000, B due 40100, one tick at 40200 must enqueue A only, then B, then nothing — a
  last-wins or all-due-at-once bug fails it.
- **F8 — CONFIRMED fixed.** `BP_NACK_OUTBOX_FULL` (`backpressure.h:85`), code `"OUTBOX"` (`:96`),
  prefix `"OUTBOX FULL NOT SENT - "` (`:118`, 23 bytes), added to the BP-11 echo-guard list
  (`:200`). `outboxEmitRefuse()` (`loop_functions.cpp:3820-3835`): `bpNackCompose()` into a
  `24 + BP_NACK_TEXT_MAX + 4` buffer (prefix 23 + NUL + 120 + "..." fits; static on nRF52 as
  `bpEmitNack()`), then `bpDeliver()` only — no `Serial.printf("[BP];nack…")`, no
  `bp_episode_origin/dst` write. Call site `:4149`, `[OUTBOX];refuse;full` stays the only marker.
  Wire length 23 + 120 + 3 = 146 < 160. `bpNackCode()`/`bpNackPrefix()` are the only `BpNack`
  switches in the tree, both with `default`. Native `test_nack_compose_outbox_full` green. Client
  guide paragraph present (`docs/client-integration-store-forward.md`).
- **Opened by the rework — foreign-DM ack at a gateway: CONFIRMED protected.** The LoRa ack arm
  sits inside the for-me branch (`strcmp(destination_call, node_call) == 0`, stage 2.1 verdict
  `:1022`); the server twins sit inside `(* && !bNoMSGtoALL) || for-me || group`
  (`udp_functions.cpp:386`, `nrf_eth.cpp:545`) with `*` zeroing `iAckPos`, and an `:ack` frame is
  always addressed to a callsign, so a foreign ack never reaches `dmOutboxOnAck()`. Even if it did,
  the core requires `dst == acker` and `nnn` (`dm_outbox.cpp:322-327`), i.e. this node's own live
  DM to that very acker with that NNN.
- **Pre-existing, not opened:** the arm also takes `:rej` (`iAckPos == -1` → `substring(3).toInt()`,
  usually 0), so a `:rej` from the destination could stop an entry with NNN 0 — the same parse feeds
  `stoHolderClear()` and `findAndStopRingSlot()` today; a reject from the destination stopping the
  ladder is the right outcome anyway.

Still open (bench, unchanged from the first pass): T-1.3 including the gateway variant as the
call-site proof of F1/F2; the "SRC,GW" hop-0 peer-cancel question (Confirmation 1); T-1.7 byte
identity capture; T-1.9 wording now `OUTBOX FULL NOT SENT - ` (the plan's T-1.9 row still says
"sent once as today").
