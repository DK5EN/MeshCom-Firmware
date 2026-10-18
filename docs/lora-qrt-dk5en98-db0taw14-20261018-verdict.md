# DK5EN-98 QRT episode: Fable Verdict

Review of `docs/lora-qrt-dk5en98-db0taw14-20261018.md` against firmware tag `v4.40a.10.18` (running on DK5EN-98) and
fork-dev HEAD `d5d0f3fc` (line numbers below), and against the capture in `logs/dk5en98-lora-20261018/` (gitignored, local). Seven finders (CSMA gate,
back-pressure, echo/retry ladder, gateway server path, measurement instrument, heap, architecture fit), then
seven independent verifiers that tried to refute each claim. Only confirmed or partly confirmed findings are
listed. Report only: no source was changed.

Severity is the verifiers' rating after refutation, not the finders'.

## Bug fixes

### Finding 1: Own group messages ignore echoes while queued, so they are re-sent up to 4 times

- **File:** `src/lora_functions.cpp:1018` (match only on non-READY, non-DONE slots), `:1027-1034`
  (`ACK_SKIP_READY`, print only), `:3454`/`:3482` (retry copies to a new READY slot and frees the original),
  `:1660` (`own_msg_id` 0x01 HEARD set, never read by the ladder for group messages)
- **Severity:** high (for the 5 messages that ran the full ladder)
- **Failure scenario:** a gateway's own group message goes to the server at once; another gateway puts it
  on air within 1-3 s, while DK5EN-98's copy is still queued (READY). The echo is dropped. After the 40 s
  window a retry copy is queued READY again, and later echoes hit that copy. `1AE1E2F6`: 4 own TX, 3 echoes
  ignored, give-up after retry 3. Capture: 20 `ACK_SKIP_READY`; 5 messages (E5, E6, E7, F6, F8) ran all
  3 retries, 5 others were released later by an echo on a SENT slot.
- **Fix:** (a) in the READY branch, release a TEXT slot when the received frame is TEXT and the slot is not
  an own PN (`pnFrameIsOwnPn`) and not an own RM1 command (`pnFrameIsOwnRm1Cmd`); under
  `taskENTER_CRITICAL` on RAK. Only `sendMessage` user messages and their retry copies are READY text, so no
  relay or server slot is reachable. (b) At the retry threshold (next to M1, `:3222`), release a non-PN,
  non-RM1 text slot whose `own_msg_id` status is 0x01 or 0x02 instead of retrying. Put both predicates in
  `pn_retry.h` and test them in `test/test_pn_retry` (no native env builds `lora_functions.cpp`).
- **Behaviour change:** on a gateway, the first echo is usually another gateway's server copy, so a group
  message may be released before DK5EN-98 sent it itself. Web GUI ticks are not affected (they read
  `own_msg_id`, not ring state).

### Finding 2: Retries bypass QRT and evict server traffic

- **File:** `src/loop_functions.cpp:4515` (the only `refusing()` check, in `sendMessage`);
  `updateRetransmissionStatus()` in `src/lora_functions.cpp` has none
- **Severity:** high (QRT bypass), medium (eviction)
- **Failure scenario:** during QRT (19:29:00-19:36:57) retry 3 of `1AE1E2F6` was queued at 19:30:44.
  Retries keep priority 1-2 and evicted 7 entries, 4 of them server texts that had waited 4-8 min.
- **Fix:** skip or defer a retry while the back-pressure tracker is refusing; with Finding 1(b) most of
  these retries disappear anyway.

### Finding 3: Gateway keeps and sends server messages that are already on air (GWRX-01)

- **File:** `src/esp32/udp_frame_esp32.cpp:559` (enqueue, status 0xFF DONE, ringSource `'g'`),
  `src/esp32/udp_frame_nrf52.cpp:553`; `src/lora_functions.cpp:1016-1018` and `:381` skip DONE slots;
  NBR cancel `:1244`, `:1274` needs `bNBRRELAY` and `RING_KIND_RELAY`
- **Severity:** medium
- **Failure scenario:** 44 server messages queued, 41 already on air from another station before DK5EN-98
  sent them, 18 sent anyway (e.g. `71A490E3` after 198 s and three heard copies, `5E45D39D` after 218 s),
  25 dropped unsent. Open as `docs/BACKLOG.md:350` GWRX-01 ("not traced"); this traces it. The reverse
  order (RF copy first) is deduplicated at `udp_frame_esp32.cpp:220`/`:542`.
- **Fix:** in the `heardIcheck >= 0` branch (`lora_functions.cpp:1620`), when a frame with the same msg_id
  arrives on RF with a path of at least two tokens, free slots with `ringSource == 'g'`, `len > 0`, status
  DONE and the same msg_id (match by id, not slot index; reuse the NBR cancel's three writes and its RAK
  lock). Let any copy count; an SNR/RSSI floor is not justified (here the strong copies came from co-located
  2 dBm bench nodes with the smallest coverage). Optional refinement: `nbrCopyMask` coverage when
  `bNBRRELAY` is on.

### Finding 4: The ring is "full" by index span; free slots are not reused

- **File:** `src/txring_functions.cpp:745-747` (`nextWrite == r && ringBuffer[r][0] > 0`), writer always
  at `iWrite` (`:673`), `advanceIReadPastEmpty()` `:268-279` only skips empty slots at the head
- **Severity:** medium
- **Failure scenario:** a priority-3 frame waits at `iRead` for airtime (`E4EBD085` held slot 6 for
  11.5 min). Holes behind it (served priority 1-2 frames, retry give-ups, evictions) are never reused. 11 of
  19 `RING_DROP_NEW` ("queue full, no lower prio to evict") fired at 11 or fewer occupied slots, 4 of them at
  5 (19:33:52-19:34:56), while back-pressure (`txRingDepth()`, `:410-426`) saw depth 5. Neither the `[RING]
full` heads-up (`loop_functions.cpp:4631`) nor a depth-based QRT release or slot reserve sees this.
- **Fix:** when index-full but occupied < MAX_RING-1, compact before writing: shift entries `r..h-1` one
  slot into the first hole `h`, with all side arrays (`ringPriority`, `ringEnqueueTime`, `ringSource`,
  `ringKind`, `ringNeed`, `ringAlone`, `retryCount`); never across an `EXT_PENDING` slot; audit bookkeeping
  keyed by slot index first. Only together with Finding 6, otherwise the extra room stores stale frames.

### Finding 5: An own message dropped below depth 16 is lost without notice

- **File:** `src/loop_functions.cpp:4666-4700` (QTA path only when depth >= 16, else `BP_SEND_INVALID`)
- **Severity:** medium
- **Failure scenario:** with Finding 4, the ring can refuse a new frame at depth 4-5. An own text (prio 2)
  is dropped only if nothing of priority > 2 can be evicted, which is rare but reachable; the operator then
  gets no nack (web GUI shows only "sendmessage invalid").
- **Fix:** return a distinct overflow code from `addTxRingEntryCore` and map it to QTA regardless of depth.

### Finding 6: No maximum age for server text

- **File:** `src/txring_functions.cpp:501-520` (`txRingAgeBackground()`, 180 s, priority 5 only),
  constant next to `RING_BG_MAX_AGE_MS` (`configuration_global.h:439`)
- **Severity:** medium
- **Failure scenario:** server text went out after up to 437 s (`E4EBD082`); 18 frames went out after other
  stations had already sent them.
- **Fix:** extend the existing sweep with a server-frame age (`ringSource == 'g'`, about 90 s), keeping its
  `EXT_PENDING` skip (`:527-529`). Risk: a server DM to a local node can be dropped on a congested ring; the
  sender retries.

### Finding 7: DK5EN-98 re-sends its own message reflected by the server

- **File:** server enqueue path (`udp_frame_esp32.cpp:542-559`)
- **Severity:** low
- **Failure scenario:** `1AE1E2CD` (DK5EN-98's own message) came back from the server, was queued as server
  traffic and sent 311 s later with path `DK5EN-98,DK5EN-98`.
- **Fix:** do not enqueue a server frame whose originator is the own callsign.

### Finding 8: Server text is classified as relay (priority 3)

- **File:** `src/txring_functions.cpp:128-130` (any DONE text slot -> priority 3)
- **Severity:** low-medium
- **Failure scenario:** server group text gets 3 instead of 2 and a 4.5 s hold; server DMs for other nodes
  get 3 instead of 1 and are the first evicted.
- **Fix:** classify server slots by source and destination (the classify/store split at `:719-724`).
  Caution: foreign server DMs at priority 1 would compete with own ACKs.

### Finding 9: CSMA gate defects

- **Starved hold never escalates (high):** `cad_attempt` rises only at `src/esp32/esp32_main.cpp:3846`
  (`CAD_BUSY`); every received frame re-arms the hold twice (`lora_functions.cpp:2663-2664`,
  `esp32_main.cpp:3603-3604`). Gate entries: 77 at attempt 0, 8 at 1, 1 at 2; rapid-fire (attempt 3) never
  reached. The 94 deferrals (`esp32_main.cpp:3480-3517`) never touch `cad_attempt`.
- **Stale-preamble guard is dead (low-medium):** `esp32_main.cpp:3485` tests RSSI < -126 dBm; the noise
  floor here is -107 to -111 dBm (0 `RX_IRQ_STALE_EARLY`), so a false preamble re-arms a full hold
  (9 preamble-only deferrals without a frame). Likely dead at BW250 everywhere. Fix: re-check after about
  400 ms instead of re-arming the full hold.
- **Hold not recomputed on enqueue (low):** the "Nothing to send" branch (`esp32_main.cpp:3869-3873`) keeps
  the old priority-3 hold, so a DM or ACK can wait 4.85 s instead of 3.35 s once.
- **`cad=` in the TX log is always 0 (low):** `csma_reset()` runs before `doTX()`
  (`esp32_main.cpp:3797`, field at `lora_functions.cpp:2898`); 78 of 78 lines show 0.

### Finding 10: KISS and RM traffic count toward back-pressure

- **File:** `src/kiss_functions.cpp:349`, `src/rm_runtime.cpp:181`, `:916` (untagged `sendMessage`
  callers); `loop_functions.cpp:4745`
- **Severity:** low
- **Failure scenario:** never refused, but they reach `onSend`, can open QRS/QRT/QTA and count toward
  `QRS_MIN_USER_MSGS`; their nack goes to `ORIGIN_NONE`.
- **Fix:** tag them or exempt them in `onSend`.

### Finding 11: The LoRa Queue card shows QUIET on a full relay ring

- **File:** `src/web_functions/web_functions.cpp:1554-1555` and `:1983-1984` (`data-bp` from
  `bpCurrentState()`)
- **Severity:** low (UI)
- **Failure scenario:** DB0TAW-14 (relay, no local user) showed "Back-pressure: QUIET" with 19/20 and 16/20
  slots queued and 0.0 % TX. The state machine only moves on the node's own typed messages (`onSend`); a
  relay-only node never leaves QUIET. Operators read it as "no congestion".
- **Fix:** label it as the own-message state ("Your messages: accepted") and show the ring condition
  separately (drops in the last 5 min, TX % next to RX %).
- **Source:** DG0OPK email 2026-10-18 19:19 and saved pages in `dg0opk-db0taw14/` (orchestrator check,
  not part of the seven-verifier pass).

## Mechanism improvements

### M1: QRT release below 16 (operator decision)

- **File:** `src/backpressure.h` `poll()` (`:428`) and `onSend()` (`:353`)
- **Design (verified on a host build of `test_backpressure.cpp`):** release at depth < 16 in both
  functions. Emit **one QRV at release** (the "you can send again" signal the operator needs) and mark the
  episode released so the quiet-band close stays silent. Re-entry at >= 16 in the same episode: state QRT,
  no new QRT notice (the per-message "QRT NOT SENT" nacks inform). A further release QRV only after 60 s.
  The episode still ends at the quiet band, as today.
- **Why not the silent variant:** without a release notice the operator's last message is "stopping to
  accept" for up to 7 min while the node accepts again (here 19:30:00 to 19:36:57).
- **Tests:** exactly two fail and must be replaced: `test/test_backpressure/test_backpressure.cpp:304`
  (`test_qrt_holds_until_the_quiet_band`) and `:540` (`test_qrv_via_onsend_when_the_queue_drained`); stale
  comment at `:513`. New: release QRV at 15, silence at the quiet close after it, re-release silent within
  60 s and one QRV after, drop at >= 16 still QTA. `test_bp_regression` passes unchanged.
- **Gain here:** depth first below 16 at 19:30:00.4 (`TX_START qlen=15`), 6 min 57 s before the QRV.

### M2: Send in the gaps, age-gated short hold (revised)

- **File:** `csma_compute_timeout_slot()` (`src/lora_functions.cpp:3835`), after the NBR case-A branch
  (`:3841`), with the arithmetic in `src/csma_timing.h` and a test in `test_csma_timing`
- **Prior art:** NBR case-A relays already drop to 150 ms after waiting 8 s (`txring_functions.cpp:332-334`,
  base 3500 ms, `configuration_global.h:538-541`); server and own traffic never get it (24 server sends and
  30 of 48 own sends waited over 8 s).
- **Design:** only for a head slot queued >= 20 s **and** QRS/QRT active; exclude `RING_KIND_RELAY`. After
  another station's frame, hold `clamp(2850 - own_airtime - 175, 500, 1500)` ms plus 0-5 slots of 35 ms,
  so our frame ends before the 3.0 s ACK window of the previous frame's receivers. After our own TX keep the
  normal priority hold (neighbours relay and ACK our frame; prevents channel capture). Pair with the stale
  preamble fix (Finding 9) and Finding 6.
- **Replay on this capture:** 11.2 sends/min (8.6 clean) vs today 4.0 (3.2 clean), inflow 5-8/min, no send
  in the 3.0 s ACK window. A replay in which other stations do not react; bench before field.
- **Rejected variants:** a flat 1.5-2.0 s after 15-20 s for all traffic (jumps ahead of every neighbour's
  ACK and relay, captures the channel after own TX, 2.0 s lands 46 % of sends in the ACK window, and a
  whole congested cell would escalate together); placing the check before the NBR branch (would move aged
  case-A relays from 150 ms to 1.5 s).

### M3: Operator rules for the site

- One gateway per site: DK5EN-1 and DK5EN-92 are gateways to the same server as DK5EN-98
  (`--gateway off` is the only lever; `--nomsgall` and `--gateway nopos` do not cover server text).
- `--nbrrelay on` on the bench nodes, so the matrix suppresses redundant relays (all three have it off).

### Not recommended

- **Reserved slots for own messages:** priority eviction already lets own messages in; a reserve would have
  to key on index span (Finding 4) and would drop server frames earlier.
- **Multi-gateway election:** conflicts with the NBR design ("rank needs no election"), and Finding 3 gives
  the same effect.
- **Shorter global base for priority 1-2:** a whole congested cell would shift together.

## Corrections to the findings doc

All applied to the report (scripts fixed and outputs regenerated):

| Doc said                                           | Corrected                                                                                                 |
| -------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| Airtime 363 ms + 7.26 ms/B fitted                  | Semtech formula, SF11 BW250 CR4/6, preamble 8, LDRO off: 60 B 0.71 s, 120 B 1.25 s (fit had 7 junk pairs) |
| Busy 19-23 %, idle 77 %                            | 25.8 % incl. CRC-error frames and own TX (node STAT util 24-28 %)                                         |
| Gaps p25/med/p75/p90 1.3/2.8/4.8/9.2 s; 45/29/22 % | 1.2/2.7/4.7/8.8 s; 43/28/19 % for 3.0/4.5/5.5 s holds (incl. CRC-error frames)                            |
| 38 drops, first at 19:20                           | 41 (3 `RING_DROP_STALE` missed), first at 19:16:28                                                        |
| 42 of 45 server msgs on air first, 19 sent anyway  | 41 of 44, 18 (`1AE1E2CD` is DK5EN-98's own message reflected by the server)                               |
| Own messages 4.3 copies per id                     | 18 messages (27 ids incl. 9 DM retry ids), 6.4 copies per message                                         |
| Bench nodes 51 % of all airtime                    | 47 % of attributed airtime incl. CRC errors; ACK/position frames unattributed                             |
| Bench RSSI -56 to -75 dBm                          | -56 to -79 dBm                                                                                            |
| "Operator refused for 7 min 57 s"                  | QRT window 7 min 57 s; no refusal was logged (`[BP];refuse` reaches 2323); next own message 19:37:40      |
| QRS at depth >= 5                                  | depth >= 5 and the 3rd own message on that ring (`QRS_MIN_USER_MSGS`)                                     |
| Release below 16 at 19:30:11, 6 min 46 s earlier   | 19:30:00.4, 6 min 57 s earlier                                                                            |
| "3.2 sends/min matches the 4.5 s line"             | Partly coincidence: 48 of 78 sends were own priority 1-2 on the 3.0 s hold                                |
| "From 19:31:41 the ring held 9 or fewer, 11+ free" | Depth 4-10, but the ring was index-full (Finding 4): no 11 free slots                                     |
| Trigger table cites `lora_functions.cpp:1225`      | Main re-arm `lora_functions.cpp:2663-2664` (1225 is the NBR-report exit)                                  |
| DB0TAW-14: operator reading "it is the heap"       | "TX buffer full" is the back-pressure notice (QRT/QTA) to the typing operator; restart cause unknown      |

## Refuted claims (do not re-investigate)

- **"`RING_OVERFLOW buf=tx` is the source of the 'tx puffer überlauf' quote"**: refuted. It prints only
  with `--loradebug` (`txring_functions.cpp:870`). The user-visible text is the back-pressure notice
  (`backpressure.h:154-157`), delivered to the app the message came from (`loop_functions.cpp:4103-4148`).
- **"Heap exhaustion gives PANIC, never TASK_WDT"**: overstated. A failed `new` panics; failed String/JSON
  allocations fail silently; a WiFi/TCP/TLS call blocked by low heap can end in TASK_WDT.
- **"No coredump because sdkconfig lacks flash core dump"**: wrong reason. The S3 sdkconfig enables ELF
  core dump to flash; the 4 KB partition in `partitions-4MB-safeboot.csv` is too small. Outcome stands.
- **"`[BP];refuse` is not mirrored to 2323"**: refuted. `debugconf.h:6` maps `Serial` to `MSerial` on
  ESP32; `[BP];notice` lines are in the capture.
- **"DM retry id came back from the server via DK5EN-1 (L3738)"**: refuted. `H03` = hop decremented, a
  LoRa relay; server-to-LoRa keeps the hop count (`udp_frame_esp32.cpp:323-326`).
- **"Relay round trip 47/66/147 s"**: only 147 s is a relay of our own TX; 47 s is DK5EN-1's server copy,
  66 s DK5EN-14 relaying DK5EN-92's server copy. The conclusion (echoes after the 40 s window) holds.
- **"nRF52 dedup window is specific to the server path"**: refuted. `dedup_functions.cpp` has no lock at
  all and the RF path has the same check-then-add gap; ESP32 has no window. Low.
- **"Ring 7-14 frames in 19:25-19:28"**: refuted by the auditor's own check; `queued=15` at 19:27:45 and
  19:27:59, so 7-15 stands.
- **"Drops are index-full because a priority-starved entry pins iRead"**: mechanism corrected. The pinned
  entry was first in line for priority 3 and waiting for airtime, not starved by priority.

## Suggested order

1. Finding 1 (echo on READY) and Finding 2: smallest change, biggest airtime saving for own traffic.
2. Finding 3 (GWRX-01) and Finding 6, then Finding 7.
3. M1 (QRT release, operator decision) with Finding 5.
4. Finding 4 (ring compaction), only after Finding 6.
5. M2 (age-gated short hold) with Finding 9, own concept and bench run.
6. Operator rules M3 now, no firmware needed; for DB0TAW-14 `--nbrrelay on` (case-A relays get the 150 ms
   escape after 8 s) and the reset reason from `--info`.
7. Finding 11 (UI label) with the next web GUI change.
