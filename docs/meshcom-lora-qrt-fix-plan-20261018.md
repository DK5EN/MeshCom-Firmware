# LoRa QRT fix campaign: implementation plan

Date: 2026-10-18. Branch: `fork-dev` at `612796ed` (verdict base `d5d0f3fc`, no `src/` change since).
Operator: DK5EN. Execution: `/orchestrate-waves` with `implementer` subagents.

Source documents (read before dispatch):

- `docs/lora-qrt-dk5en98-db0taw14-20261018.md`: the capture analysis (DK5EN-98, 2026-10-18 19:14-19:38)
- `docs/lora-qrt-dk5en98-db0taw14-20261018-verdict.md`: findings with file:line, seven finders, seven verifiers
- `docs/production-locked-settings.md`: the production setting lock (campaign W4, `e4edffc8`, unreleased)
- `docs/backpressure-protocol.md`: QRS/QRT/QTA/QRV states and notice wording

Step 0 for the executing agent: copy this file to `docs/campaign-lora-qrt-20261018.md`. That copy is the
resume point; update it after every wave (orchestrate-waves section 6), then `docs/RESUME.md`.

## 1. Operator decisions (2026-10-18, final)

| Decision                                                      | Verdict item | Operator ruling                                                                                             |
| ------------------------------------------------------------- | ------------ | ----------------------------------------------------------------------------------------------------------- |
| No operator-side bench changes before firmware                | M3           | Rejected. Many nodes with mesh on and redundant gateways is the real field. The firmware must cope with it. |
| `--nbrrelay on` as default, locked                            | M3           | Approved. Production builds boot with it on and refuse to change it.                                        |
| Own group message released by an echo while queued            | Finding 1    | Approved, "huge lever, do it now".                                                                          |
| Own message reflected by the server is not re-sent            | Finding 7    | Approved, "a real bug".                                                                                     |
| QRT release below 16, one QRV, nack at any depth              | M1 + F5      | Approved.                                                                                                   |
| Queue card label and ring condition line                      | Finding 11   | Approved.                                                                                                   |
| `cad=` in the TX log line is always 0                         | Finding 9    | Approved, fold in (wave 2).                                                                                 |
| The hold must escalate with waiting time                      | F9 + M2      | Approved in principle; the proposal in section 4 wants a ruling on the open points in section 8.            |
| Findings 3, 4, 6, 8, remaining Finding 9 items, M2 as drafted | -            | Not in this campaign. Go to `docs/BACKLOG.md` with the verdict reference.                                   |

Risk on record: `--nbrrelay on` as a locked default means every production node cancels or delays relays by
its own neighbour matrix. Field evidence so far is DK5EN-98 alone (1011 case-A decisions in the capture).
The operator accepted that.

## 2. What the approved set does and does not fix

The approved wave 1 attacks the inflow (redundant copies, retries, reflected own messages) and the refusal
(QRT held for 8 min while 15 slots were free). It does not change the send gate. On the same evening with the
same traffic the ring would fill slower and QRT would last about one minute, but DK5EN-98 would still send
3-4 frames per minute into a channel that is idle three quarters of the time. The gate is wave 2.

Why the existing escalation never fired: the gate counts only CAD scans that find the channel busy
(`cad_attempt`). The gate opens after a full hold of silence, so the channel is usually free at that moment:
77 gate entries at attempt 0, 8 at 1, 1 at 2, none at 3 in 24 minutes. The starvation came from the other
side. Every received frame re-arms the hold at the current attempt without incrementing it (`lora_functions.cpp`
after `OnRxDone`, `esp32_main.cpp` after `checkRX`), and the 94 preamble deferrals do not increment it
either. 316 interruptions, each one a full hold again. The counter measures "busy when I try"; the problem
was "never allowed to try". The only age-based escape today is the neighbour-matrix case-A relay (150 ms
after 8 s in the queue, `txring_functions.cpp` `txringCaseBackoffSlot()`), and only with `--nbrrelay on`.

## 3. Wave 1: approved fixes (six implementers, parallel, disjoint files)

All six: `implementer` agent, Sonnet 5.5, effort medium. Brief hygiene per orchestrate-waves section 2:
exclusive file set, no git, no flashing, no transmitting, no files or stubs at another agent's path, no
whole-repo formatting, out-of-scope bugs reported not patched, terse report of at most 15 lines. Every bug fix
ships with a test that fails before and passes after, with the before-run output in the report; no before-run
means "unverified".

### Shared resources

- One `pio` process at a time (shared build cache, global package store). Every `pio` call goes through the
  mkdir-lock wrapper the orchestrator creates in the scratchpad before dispatch:

  ```sh
  #!/bin/sh
  # piolock.sh: serialise pio across writers
  LOCK=/tmp/meshcom-pio.lock
  while ! mkdir "$LOCK" 2>/dev/null; do sleep 2; done
  trap 'rmdir "$LOCK"' EXIT
  exec pio "$@"
  ```

- Writers run only their own host env (`-e <env> -f <suite>`). Never bare `pio test` (walks the board envs
  and flashes attached hardware).
- Board build targets: B owns `heltec_v3`, C owns `ttgo_tbeam`, nobody else builds a board. The gate builds
  the rest.
- A build or test failure in a file outside the exclusive set is a shared-resource race until proven
  otherwise: check `pgrep -fl pio`, clean that target, rebuild once, report.

### Ownership table

| Brief | Exclusive files                                                                                                               | Host env and suite                                                               |
| ----- | ----------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| A     | `src/prod_lock.h`, `test/test_prod_lock/`, `test/test_settings_sanitize/`, `docs/production-locked-settings.md`               | `native_prod_lock` / `test_prod_lock`; `native` / `test_settings_sanitize`       |
| B     | `src/lora_functions.cpp`, `src/pn_retry.h`, `test/test_pn_retry/`                                                             | `native_pnretry` / `test_pn_retry`; `pio run -e heltec_v3`                       |
| C     | `src/esp32/udp_frame_esp32.cpp`, `src/nrf52/udp_frame_nrf52.cpp`, `test/test_udp_frame_twin/`                                 | `native_udp_frame_twin` / `test_udp_frame_twin`; `pio run -e ttgo_tbeam`         |
| D     | `src/txring_functions.cpp`, `src/loop_functions.h`, `src/loop_functions.cpp`, `test/test_txring/`, `test/test_bp_regression/` | `native_aprs` / `test_txring`, `test_bp_regression`                              |
| E     | `src/backpressure.h`, `test/test_backpressure/`, `docs/backpressure-protocol.md`                                              | `native` / `test_backpressure`                                                   |
| F     | `src/web_functions/web_functions.cpp`, new `tools/webgui_queue_test.js`                                                       | `node tools/webgui_queue_test.js` (jsdom, same harness as `webgui_tick_test.js`) |

Cross-reads that are not conflicts: C reads `pn_retry.h` only if it uses `pnFrameSourceIs()`; prefer
`strcmp(aprsmsg.msg_source_call, meshcom_settings.node_call)` on the already decoded frame so C has no
dependency on B's header. D changes a return code that `test_bp_regression` (also `native_aprs`) exercises;
E changes `backpressure.h`, which `test_bp_regression` compiles. Both are gate-level checks, not brief-level.

### Brief A: `--nbrrelay on` as the locked production default

Situation. `--nbrrelay` controls the neighbour-matrix relay suppression: with `on` the node cancels its own
relay when every neighbour that needs the frame is already covered, cancels a queued relay when a better
placed copy arrives, and lets a case-A relay drop to a 150 ms hold after 8 s in the queue. Two bits in
`node_sset4` (0x0800 count, 0x1000 cancel; `on` sets both). The production lock (`src/prod_lock.h`,
`prodLockNormalise()`) clears all five NBR bits (mask 0x3B00) on every boot in non-INSTRUMENT builds and
`prodLockRefuses()` refuses the commands. Today's locked value is `off`.

Problem. A node with `off` relays everything once and waits the full 4.5 s hold after every received frame.
In the capture, four gateways and two relays put every message on air (3.8 copies per server message, 6.4 per
own message). DB0TAW-14 (hilltop, 41 % receive load, `--nbrrelay off`) sat with 16-19 relays queued and sent
nothing for minutes.

Change. In `prodLockNormalise()` the production branch sets bits 0x1800 instead of clearing them; 0x0100,
0x0200 and 0x2000 stay cleared. Split `PRODLOCK_SSET4_MASK_NBR` into a clear mask (0x2300) and a set mask
(0x1800). INSTRUMENT builds stay unchanged (changeable, bits as stored). `prodLockRefuses()` is unchanged.
Doc: both language sections of `docs/production-locked-settings.md` say fixed value `on`, bits 0x1800 = 1,
with the reason (QRT analysis 2026-10-18). Boot derives `bNBRRELAY`/`bNBRCANCEL` from the bits
(`esp32_main.cpp:1445`, `nrf52_main.cpp:737`), so no runtime change is needed.

Tests. `test_prod_lock`: a struct with `node_sset4` 0 gets 0x1800 and counts 1; 0x3B00 becomes 0x1800;
idempotent second call returns 0; `instrument = true` leaves 0 and 0x3B00 alone; 0x0400 (`--nbrdebug`) and
foreign bits survive. `test_settings_sanitize` (in env `native`): the same through the boot sanitize path.
Before-run: the new assertions fail on the current header.

### Brief B: Finding 1 (echo on a queued own message) and Finding 2 (no retry while refusing)

Situation. A typed group message enters the ring READY, is sent, becomes SENT, and the retry ladder waits 40 s
for an echo. An echo on a SENT slot releases it; no echo means a retry copy in a new READY slot, three times.
On a gateway the message also goes to the server at once.

Problem. The echo match (`lora_functions.cpp:1018`) skips READY slots on purpose, logging `ACK_SKIP_READY`.
On a gateway another gateway puts the server copy on air within seconds, while our copy still waits. That echo
is ignored; we send anyway, see no echo in the 40 s window (the echoes came before), queue a retry, and the
next echo lands on the READY retry copy. Capture: 20 ignored echoes, 5 messages ran all 3 retries, one message
on air 7 times. Finding 2: retries bypass the QRT check (`refusing()` is tested only in `sendMessage()`,
`loop_functions.cpp:4515`) and evicted 7 queued entries during the QRT window, 4 of them server texts.

Change.

1. READY branch (`:1027-1034`): when the received frame is TEXT and the READY slot is TEXT and the slot is not
   an own PN (`pnFrameIsOwnPn`) and not an own RM1 command (`pnFrameIsOwnRm1Cmd`), release the slot
   (`ringBuffer[i][1] = RING_STATUS_DONE; ringBuffer[i][0] = 0; retryCount[i] = 0;`). Keep the debug line
   for the excluded cases. On RAK4630 this runs in the LORA task while `doTX()` runs in `loop()`: use the
   same lock `doTX()` uses for its READY takeover (`:2901` and the N-16 notes around `:2965-3083`; it is not
   plain `taskENTER_CRITICAL` there, read before choosing). The implementer states in the report which lock
   and why it matches.
2. Retry threshold (`:3222`, next to the PN M1 exception): for a TEXT slot that is not own PN and not own
   RM1, look up `checkOwnTx(msg_id)`; if `own_msg_id[idx][4]` is 0x01 (heard) or 0x02 (acked), release the
   slot instead of queuing a retry copy, with a `[RETX] already heard, stop` debug line.
3. Finding 2: in `updateRetransmissionStatus()`, at the threshold, if `bp_state.refusing()` is true, defer
   the retry: do not queue the copy this tick, keep the slot at the threshold (reset the tick counter by one
   step so it re-checks next tick). Log once per slot.
4. The two predicates ("releasable group text", "retry-eligible group text") go into `pn_retry.h` as
   `static inline` functions next to `pnFrameIsOwnPn()`, so `test_pn_retry` can pin them: own group text
   -> releasable; own PN -> not; own RM1 command -> not; KISS-client PN (foreign source call) -> releasable
   (today's behaviour for those: release at first echo).

Behaviour change to document: on a gateway, the first echo is usually another gateway's server copy, so the
gateway may never put its own group message on radio. Web GUI ticks are unaffected (they read `own_msg_id`).

Verification. `native_pnretry` on the predicates (before-run fails: predicates absent). No host env builds
`lora_functions.cpp`, so the implementer compiles `pio run -e heltec_v3` through the lock and proves the
change is in the image: `strings .pio/build/heltec_v3/firmware.bin | grep -c 'already heard'` equals 1.
Integrity: `grep -c ACK_SKIP_READY src/lora_functions.cpp` still 1.

### Brief C: Finding 7 (own message reflected by the server)

Situation. A gateway puts every server frame on air. To avoid re-sending its own messages it checks each
server frame against a 20-entry ring of recently seen own ids (`checkOwnTx()`), which on a gateway also holds
every server message it forwarded.

Problem. At 44 server messages in 24 minutes that ring wraps in about ten minutes. DK5EN-98's own message
`1AE1E2CD` came back from the server later than that (through another gateway's upload), was no longer in the
ring, and was queued as server traffic and sent 311 s later with path `DK5EN-98,DK5EN-98`.

Change. In both handlers (`src/esp32/udp_frame_esp32.cpp` around `:542-559`,
`src/nrf52/udp_frame_nrf52.cpp` around `:553`), before `insertOwnTx()` and `addTxRingEntry()`: if the decoded
source call equals `meshcom_settings.node_call` (case-insensitive, the frame is already decoded into
`aprsmsg`), skip the enqueue and the own-tx bookkeeping, keep the dedup entry, and log `[GW] own msg
reflected, not queued` under `bDisplayLog`. Everything else in the handler (ACK handling, phone, EXTUDP)
is untouched.

Verification. `test_udp_frame_twin`: a server frame whose source call is the own callsign reaches no
`addTxRingEntry` and no `insertOwnTx` on either platform, a foreign-source frame still does. Before-run
fails. `pio run -e ttgo_tbeam` compiles (ESP32 classic). nRF52 compiles at the gate.

### Brief D: Finding 5 (a dropped own message always gets a nack)

Situation. `addTxRingEntryCore()` (`txring_functions.cpp:622`) returns -1 for three reasons: unconfigured
callsign, invalid length, and "ring full, nothing of lower priority to evict". `sendMessage()`
(`loop_functions.cpp:4666-4700`) cannot tell them apart and uses the depth as a proxy: at depth >= 16 the
operator gets "QTA NOT SENT", below that only `[BP];invalid`.

Problem. The ring counts as full by index span (`:745-747`); holes behind a frame waiting at the read index
are never reused. In the capture 11 of 19 "queue full" drops fired at 11 or fewer occupied slots, 4 of them at
depth 5. A typed message dropped there is lost without any notice.

Change. A distinct return code for the no-evict overflow (`RING_ENQUEUE_FULL`, -2, declared in
`loop_functions.h` next to `addTxRingEntry()`); the two validity failures keep -1. `sendMessage()` maps -2 to
the QTA path regardless of depth and keeps the depth proxy for -1. Audit all 67 call sites of
`addTxRingEntry()`/`addTxRingEntryOnce()`: every test must be `< 0`, never `== -1`; the report lists any
other form found.

Verification. `test_txring`: ring full of priority-1 entries plus a priority-2 own text returns -2; a
length-0 frame still returns -1. `test_bp_regression`: the nack fires at depth 5 on a -2. Before-run fails.

### Brief E: M1 (QRT release below the line)

Situation. `BackPressure` (`src/backpressure.h`) enters QRT at depth >= 16 of 20 and leaves it only at
depth 0, or depth 1 held for 10 s (`QRV_HOLD_MS`). Hysteresis against flapping.

Problem. QRT at 19:29:00, depth below 16 from 19:30:00, ring stalled at 4-6 because server inflow matched
the outflow, QRV only at 19:36:57 when the server paused. 7 min 57 s refused, 6 min 57 s of it with 15 free
slots.

Change (verified on a host build during the review).

- `onSend()` and `poll()`: when `state_ == BP_QRT` and `depth < refuseThreshold()`, release: `state_` to
  `BP_QRS` (depth is still in or above the QRS band) and return `BP_NOTICE_QRV` if no release QRV was emitted
  in this episode or the last one is older than 60 s (`QRV_RELEASE_REPEAT_MS`); otherwise `BP_NOTICE_NONE`.
  Mark `released_` in the episode.
- Re-entry at >= 16 inside the same episode: `state_ = BP_QRT`, no notice (the latch already sits at QRT,
  `latchIfHigher` stays silent). The per-message nack informs.
- Quiet-band close (`enterQuiet()`): QRV only if the episode reached QRT and no release QRV was emitted.
  Clear `released_` with the latch.
- Replace the two tests that encode the old rule: `test_backpressure.cpp:304`
  (`test_qrt_holds_until_the_quiet_band`) and `:540` (`test_qrv_via_onsend_when_the_queue_drained`); fix
  the stale comment at `:513`. New tests: release at 15 emits one QRV; quiet close after it is silent;
  re-entry at 16 is silent and refuses; second release within 60 s silent, after 60 s one QRV; a drop at
  > = 16 still gives QTA; `poll()` releases as well as `onSend()`.
- `docs/backpressure-protocol.md`: the release rule and the notice sequence.
- Notice wording: see open point 3 in section 8. Default if unanswered: keep the text.

Verification. `native` / `test_backpressure` (39 tests today; two replaced, about six added).
`test_bp_regression` at the gate.

### Brief F: Finding 11 (queue card)

Situation. The LoRa Queue card on the web GUI (`web_functions.cpp:1517-1560` script, `:1981-1990` data
attributes) shows ring occupancy, the QRS/QRT lines and "Back-pressure: QUIET/QRS/QRT" from
`bpCurrentState()`.

Problem. The state machine only moves on the node's own typed messages. A relay with no local user never
leaves QUIET. DB0TAW-14 showed "Back-pressure: QUIET" at 19/20 queued and 0.0 % TX; the operator read it as
"no congestion".

Change. Label the state for what it is: "Your messages: accepted" (QUIET, green), "slow down" (QRS,
orange), "refused" (QRT, red). Add a ring condition line from counters the node already has: drops in the
last completed 5-min window (`stat_last_window`, `setlogStatFields`; if the struct has no drop field, add one
fed from `stat_drop_count[]` in `setlogFillStat()`, which is `loop_functions.cpp`: then that one function is
added to F's set and D is told it must not touch it) and TX % next to RX % (`data-tx`, `data-rx` already
exist; `data-rx`/`data-tx` are ms of the window, `data-int` the window length). Example rendering:
"Ring: 19/20 queued, 7 dropped in the last 5 min, TX 0.0 % / RX 43.4 %".

Verification. New `tools/webgui_queue_test.js` renders the card from a fixture `#mcq` element with
`data-bp=0`, 19 queued, drops 7, tx 0, rx 130000 and asserts the three strings; a second case with
`data-bp=2`. Registered where `webgui_tick_test.js` is registered in `tools/regression.sh` stage 2 (read
the runner first; if registration is a list in the runner, the orchestrator adds the line at the gate).
Before-run fails on "Back-pressure:". Chrome check at the gate on DK5EN-1.

### Gate 1 (orchestrator, after all six returned)

1. `ls` every expected file; `git status` shows only the owned files.
2. `tools/regression.sh --stage 1,2` (all `[env:native*]`, golden selftest, pytest, tool suites).
3. Clean sequential board builds: `heltec_v3`, `ttgo_tbeam`, `wiscore_rak4631` (needs Rosetta;
   `softwareupdate --install-rosetta --agree-to-license` if "Bad CPU type"), `E22_XML-DevKitC`,
   `tdeck_plus`. Compare RAM/flash regions with the previous commit (`tools/heap_baseline.json` gate is in
   `/release-firmware` step 2).
4. Advisor pass: fresh `general-purpose`, Opus 5.5, medium, `fable-review` advisor mode on the wave diff
   against the acceptance criteria above. Verify its items yourself, rework via a fresh implementer at high.
5. Bench on DK5EN-1 (standing flash permission; INSTRUMENT build for the flood, production build for the
   lock): QRT release with the ring flood (`tools/bench/bench_suite.py` back-pressure scenario), `--nbrrelay
off` refused with `[ERR];locked;nbrrelay` on the production build and `--info` showing on, queue card in
   Chrome (open the root page and navigate, `/?page=` is a fragment). Finding 1 and 7 cannot be forced on
   the bench; their proof is the host tests plus the DK5EN-98 night in section 6.
6. One commit on `fork-dev`, explicit paths, after reading the diff per brief. Then update the campaign doc
   and `docs/RESUME.md`, end the wave report with the compaction marker.

Estimate: 15-25 min per brief, about 35 min wall-clock for the writers, 25-35 USD; gate and bench about
60 min orchestrator time.

## 4. Wave 2: the send gate (one implementer, after commit 1)

### Brief G: honest `cad=` and an age-escalated hold

Exclusive files: `src/csma_timing.h`, `src/configuration_global.h` (new constants only),
`src/lora_functions.cpp` (the CSMA block `csma_compute_timeout_slot()` to `csma_reset()`, around
`:3835-3875`, and the TX log call at `:2898`), `src/esp32/esp32_main.cpp` (`CAD_FREE` block `:3795-3800`,
`[MC-HWM]` line `:3397`), `src/nrf52/nrf52_main.cpp` (`CAD_FREE` block `:1708-1712`, its HWM line),
`src/loop_functions.cpp` and `src/loop_functions_extern.h` (new stat counters only),
`test/test_csma_timing/`. Sonnet 5.5, medium. Wave 1 must be committed first: B owned `lora_functions.cpp`.

Situation. The hold before a transmission is `csma_timeout`, recomputed at every re-arm by
`csma_compute_timeout(cad_attempt)`, which resolves the next slot (`getNextTxSlot()`) and its priority. Base
3.0 s (prio 1-2), 4.5 s (3), 5.5 s (4-5), plus 0-10 slots of 35 ms jitter. `cad_attempt` rises only on
`CAD_BUSY`; at 1 the base is 5/6, at 2 it is 2/3, at 3 the gate goes to rapid fire (100 ms preamble check
windows). `csma_reset()` sets it to 0 after a transmission and, at `CAD_FREE`, right before `doTX()`.
Re-arm sites after a received frame: `lora_functions.cpp:953, 972, 994, 1226, 2664` and `esp32_main.cpp:3604`
(`checkRX`); the deferral sites (`esp32_main.cpp:3480-3517`, `nrf52_main.cpp:1618-1625`) re-arm the timer
and keep the current `csma_timeout`.

Problem 1 (cad=). `doTX()` prints `cad_attempt` in the TX log line after `csma_reset()` already zeroed it:
78 of 78 lines show `cad=0`. The high-water mark `csma_hwm` in `[MC-HWM]` is tracked inside `csma_reset()`
and is correct, but it measures escalation depth, not waiting time.

Problem 2 (no escalation by age). A received frame re-arms the full hold at the current attempt. A node whose
head frame has waited 60 s gets exactly the same hold as one that queued it a second ago. With a median gap
of 2.7 s between foreign frames only 28 % of the gaps fit a 4.5 s hold; the node used 2 of 46 gaps between
3.0 and 4.5 s. The case-A relay escape (150 ms after 8 s) exists for one traffic class only.

Change.

1. `cad=`: at `CAD_FREE` capture `int tx_cad_attempt = cad_attempt;` before `csma_reset()` on both
   platforms and hand it to `doTX()` (new parameter with default, or a global `last_tx_cad_attempt` set
   there and read in the log call). The TX line format is unchanged; only the value becomes honest.
2. Age stage, arithmetic in `csma_timing.h` (pure, host-tested):

   ```c
   #define CSMA_AGE_STAGE1_MS     20000UL   // head frame queued this long: short hold after a foreign frame
   #define CSMA_AGE_STAGE2_MS     60000UL   // this long: rapid hold after a foreign frame
   #define CSMA_AGE_ACK_WINDOW_MS 2850      // receivers of the previous frame hold 3.0 s; end before that
   #define CSMA_AGE_GUARD_MS      175
   #define CSMA_AGE_SHORT_MIN_MS  500
   #define CSMA_AGE_SHORT_MAX_MS  1500
   #define CSMA_AGE_SLOTS         5         // jitter 0..5 slots of CSMA_SLOT_SIZE (0-175 ms)

   static inline int csmaAgeStage(uint32_t age_ms);   // 0, 1, 2
   // stage 1: clamp(ACK_WINDOW - own_airtime_ms - GUARD, SHORT_MIN, SHORT_MAX) + jitter_slot*SLOT
   // stage 2: CSMA_RAPID_RX_MS + jitter_slot*SLOT
   static inline unsigned long csmaAgedHoldMs(int stage, unsigned long own_airtime_ms, long jitter_slot);
   ```

3. Apply it as a floor at the end of `csma_compute_timeout_slot()`: compute today's result (all branches,
   including case A/B), then if the slot's age (`millis() - ringEnqueueTime[slot]`) gives stage > 0 and the
   slot's priority is 1-3, return `min(today, aged)`. `min` keeps the case-A 150 ms escape and covers case-B
   text relays, server text and own messages. Priority 4-5 (position, HEY) never escalate; they age out.
4. Never after an own transmission. `csma_reset()` computes the post-TX hold without the age floor (a flag
   or a separate entry point `csma_compute_timeout_after_tx()`); every re-arm after a received frame keeps
   `csma_compute_timeout(cad_attempt)` and gets the floor. A node with a deep queue therefore still waits
   the full hold after each of its own frames, so neighbours can ACK and relay it, and no node can capture
   the channel.
5. Own airtime for stage 1: `radio.getTimeOnAir(len)` (RadioLib, microseconds) on ESP32,
   `Radio.TimeOnAir(MODEM_LORA, len)` (ms) on nRF52; wrap as `lora_time_on_air_ms(len)` in
   `lora_functions.cpp`. Stage 1 then ends our frame before the previous frame's receivers finish their
   3.0 s hold (the verdict measured: a flat 2.0 s hold would land 46 % of sends inside that window).
6. Metrics: `[MC-DBG] CSMA_AGED slot=%d age=%lu stage=%d hold=%lu` at each re-arm with stage > 0
   (`bLORADEBUG`); window counters `stat_csma_aged[3]` (sends that went out under stage 0/1/2, cleared with
   the 5-min window); the `[MC-HWM]` line gains `aged1=%u aged2=%u wait_hwm=%lu` (max queue latency over all
   priorities since boot). The TX log line already carries the queue latency (`latency`), so the per-frame
   waiting time is in every TX line today.

What it does on the capture (replay in the verdict, M2 variant): 11.2 sends per minute instead of 4.0, no
send inside a 3.0 s ACK window. A replay cannot show how other stations react; that is the bench run.

Rejected variants (verdict): a flat 1.5-2.0 s after 15-20 s for all traffic (jumps ahead of every
neighbour's ACK and relay, captures the channel after own TX); shorter global base for priority 1-2 (a whole
congested cell shifts together); placing the age check before the NBR branch (aged case-A relays would go
from 150 ms to 1.5 s). Letting re-arms count toward `cad_attempt` was considered and dropped: it reuses the
ladder but reaches rapid fire after three foreign frames, about 10 s, with no ACK-window arithmetic.

Risk. Every node in a congested cell escalates its aged frames. The jitter (0-175 ms), the CAD scan before
each transmission and the "never after own TX" rule limit it; the stage thresholds are constants for the
bench to tune. Stage 2 can be disabled by setting `CSMA_AGE_STAGE2_MS` to 0 (meaning never) if the bench
says so.

Verification. `test_csma_timing` (`native` env, 10 tests today): stage boundaries, stage-1 clamp at 60 B and
120 B airtime (0.71 s and 1.25 s at SF11 BW250: holds 1500 and 1425 ms before jitter), stage 2 value, the
floor against a case-A 150 ms result, priority 4-5 excluded. Before-run fails (functions absent).
`pio run -e heltec_v3` through the lock; image scan `strings ... | grep -c CSMA_AGED` equals 1. Integrity:
the TX log format string in `setlog_lines.cpp` unchanged (`git diff --stat` must not list it; the golden
selftest pins it at the gate).

### Gate 2

Same as gate 1 steps 1-4 and 6, plus a differential bench before the commit:

- Two runs on DK5EN-1 (INSTRUMENT build), previous commit vs wave 2, same script: DK5EN-92 and DK5EN-14
  generate foreign traffic to group TEST at 2 dBm every 2-4 s for 10 min (bench rules: `TEST` group or own
  DM only, never broadcast, at most 2 dBm, `identity_guard.py` first, over the 2323 console not USB);
  DK5EN-1 gets 12 frames queued by `--injectmsg` to `TEST` at t=0.
- Measure: sends per minute, queue latency distribution (TX log `latency`), `aged1`/`aged2` counts,
  collisions (CRC errors seen by the other two nodes), and whether DK5EN-92/14 still ACK and relay DK5EN-1's
  frames (no channel capture).
- Pass: sends per minute at least 2x the baseline run with no rise in CRC errors at the two listeners beyond
  noise, and no send of DK5EN-1 inside 3.0 s after a foreign frame that was longer than its own hold
  (`CSMA_AGED` line vs the next `TX_START`). Recipe to reuse: `logs/dk5en98-lora-20261018/review/
r2-airgap-replay.md` (local, gitignored) and `tools/bench/onrx_matrix.py` for the node keys and flash.

Estimate: one brief 20-30 min, 8-12 USD, bench 60-90 min.

## 5. Wave 3: docs and backlog (orchestrator, or one Sonnet implementer at medium)

- `docs/campaign-lora-qrt-20261018.md`: status per brief, bench results, commits.
- `docs/RESUME.md`: new top section.
- `docs/BACKLOG.md`: GWRX-01 updated with the trace (Finding 3), new items for Findings 3, 4, 6, 8, the
  remaining Finding 9 items (starved hold never escalates is closed by G; stale preamble guard at -126 dBm,
  hold not recomputed on enqueue stay open), Finding 10, the dedup ring aging by time (about 45 min), and the
  "relay variant of M2" note. Each with the verdict reference.
- `docs/architecture/08-defect-catalogue.md`: IDs for the shipped fixes.
- `docs/lora-qrt-dk5en98-db0taw14-20261018-verdict.md`: a status column or a "shipped in" note per finding.
- Run `npx --yes prettier@3 --write` on every touched `.md`.

## 6. Field path

1. After commit 2 the operator decides on `/release-firmware` (section 8, point 4). DK5EN-98 is the
   production gateway and stays read-only for the agent; the operator updates it.
2. One night on DK5EN-98 with `meshlogger.py` on rpizero, `--loradebug` and `--txcapture` on, then
   `analyse.py` and `gaps.py` from `logs/dk5en98-lora-20261018/` on the new capture. Compare: copies per
   message, ACK_SKIP_READY count (expected 0 for group text), sends per minute, drops, QRT duration.
3. DG0OPK: the release note tells DB0TAW-13/-14 what changed (relay suppression on, QRT release, card
   label) and asks for the reset reason from `--info` for the reboot question.

## 7. Not in this campaign (backlog with verdict reference)

Finding 3 (gateway cancels a queued server frame heard on air, GWRX-01), Finding 4 (ring compaction, only
after Finding 6), Finding 6 (server text max age 90 s), Finding 8 (server text classified as relay),
Finding 9 stale-preamble guard and enqueue recompute, Finding 10 (KISS/RM count toward back-pressure),
dedup ring aging by time, the relay variant question if G's bench shows pure relays still starving.

## 8. Open decisions (ask the operator; defaults apply if the executing agent gets no answer)

1. Finding 2 in brief B: default yes.
2. INSTRUMENT builds and `--nbrrelay`: default changeable (bits as stored); the orchestrator sets
   `--nbrrelay on` on DK5EN-1, -92, -14 by hand after the wave. Alternative: INSTRUMENT also forces on,
   which kills differential bench runs.
3. QRV wording at release ("QRV - ready again, TX buffer clear" is false at 15 of 20): default keep the text;
   proposal "QRV - accepting messages again". App-visible string, check `tools/` and bench harness greps
   for the old text before changing.
4. Release after wave 1 alone, after wave 2, or both: default one release after wave 2, but a DK5EN-98
   night on wave 1 alone first gives a clean before/after for the gate change. Recommendation: release
   after wave 1, one night, then wave 2 and a second release.
5. Age thresholds: default stage 1 at 20 s, stage 2 at 60 s. Stage 2 included by default; set
   `CSMA_AGE_STAGE2_MS` to 0 to ship stage 1 only.
6. Priorities covered by the age floor: default 1-3 (ACK, DM, own group, relay, server text). Positions
   and HEY never escalate.
