# MeshCom Stability Changelog

Release: `v4.35p.09.01-stability` (2026-09-01), based on official MeshCom
4.35p, upstream `dev` at `2cb6bb4d` — the state **after** upstream merged this
fork's changes, plus items 104-166 below. The full engineering rationale for
items 107-152, with per-change file references and measurements, is in the
upstream PR draft
[`docs/pr-draft-20260831.md`](pr-draft-20260831.md).

**Items 1-103 below are now in official MeshCom.** The ICSSW maintainers merged
[PR #1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102) (82 changes)
and [PR #1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103) into
upstream `dev` on 27 August 2026. This document is kept as the record of what
was done and why; it is no longer a list of differences from the official
firmware.

> **`v4.35p.08.21-stability` has been withdrawn and deleted.** It put any node
> with GPS enabled into a permanent boot loop (item 81 below). If you still have
> it installed, update — and note that an affected node can only be recovered
> over USB.

## About this release

MeshCom is a wonderful project, and this release exists because we use it every
day and want it to be as dependable as the idea deserves. Everything below is
the result of a long, careful quality pass over the 4.35p codebase: reading the
code, testing on real hardware (Heltec V3, T-Beam v1.2 and WisBlock RAK4631 on
the bench, soak tests over hours), and improving what we found — always in the
smallest way that works.

Three things this release is **not**:

- **It is not the official firmware.** The official MeshCom firmware is
  maintained by the ICSSW team at
  [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware),
  and that is where the project lives and grows.
- **It is not a feature release.** A node running this build interoperates with
  official 4.35p on the air and toward the apps. The changes are about
  stability, robustness, input hardening, and the test infrastructure to keep
  it that way; the additions are maintenance and diagnostic aids (a
  bootloader-entry command, a boot-time reset-reason log, a raw-frame capture
  switch, developer tooling). Three fixes do change what a node puts on the air
  or how it reports itself, and every one of them is a correction rather than a
  feature — they are listed under **What changes on the air** below, so that
  nobody meets them by surprise.
- **It is not a fork in spirit.** The changes are deliberately small and
  surgical, and they are offered back upstream as individual pull requests, as
  many of our earlier improvements already have been. This release simply
  makes the whole collection available in one place for field testing while
  that process runs its course.

The items below reference the engineering logs in this repository:
[docs/architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md)
(IDs like N-xx, SEC-xx, CONC-xx) and
[docs/code-audit-fixes-20260627.md](code-audit-fixes-20260627.md) (IDs like
A1, B2, C3), where the findings are documented with evidence. Every fix is one
focused commit in this repository's history.

## What changes on the air

Almost everything in this changelog is invisible from outside the node. Three
items in this release are not, and they are listed here so that nobody has to
discover them by surprise:

- **`/B=000` is now transmitted** when the battery is measured and empty (item
  90). Previously nothing was sent below one percent, which made "empty" and
  "no battery fitted" indistinguishable. A missing `/B=` tag now means the node
  has no battery hardware to report.
- **Implausible frames in the ACK path are no longer relayed** (item 91).
  Measured against 8741 field frames, this drops 5.7% of what reached that
  path — not one of which acknowledged a message the node had actually heard.
  They were previously re-transmitted into the mesh at priority 1.
- **ESP32 channel-utilisation figures drop sharply** (item 94), because they
  were computed from a fixed 255-byte length. Nothing about the radio changed;
  the number is simply correct now. Expect roughly 7% where the same node used
  to report 18%.

## Unreleased (since v4.35p.09.01.2-stability)

170. **Code Quality 2.0: the error patterns of the last two weeks, with detectors.**
     `docs/code-quality-2.0.md` distils 49 coding sessions (2026-08-27 to
     09-01), the fix wave since 2026-08-18 and the review documents into 28
     code-defect and 16 process patterns — each with why it slipped past
     review, a grep or check that finds the class, and the gap it exposes in
     `codequality-rules.md`; a 26-point review checklist (Part D) and a table
     of the bench pitfalls that cost the most time. Twelve of the detectors
     are now rules `CQ2-*` in `tools/code_audit_scan.py` (optional whitelist
     regex per rule), and `/code-audit` walks Part D in its contextual phase.
     The build skill no longer tells a session to run `pio` in parallel.
171. **ESP32 boot banner prints the reset cause** (TM-51). `[BOOT]
RESET_REASON=<n> <name>` right after `CLIENT SETUP`, raw `Serial.printf`
     so it survives `--debug off`; nRF52 has printed `RESETREAS` since
     2026-08-21. A field reboot (`OE5HWN-14`, proven by the `millis()`
     rollback, the heap watermark and a 32 s hole) was undiagnosable
     without it.
172. **Two sites the new checks found.** `PositionToAPRS()` concatenated
     nineteen tag strings with `strncat(dst, src, sizeof(dst)-1)`, which
     bounds the source and not the room left — the full tag set (about
     111 bytes) overran `strconcat[100]`, latent only because the length
     budget check runs afterwards; every call is now bounded by the
     remaining room. The T-Deck SD-map boundary timer compared
     `timer + 30000 < millis()`, the form that latches after 49.7 days
     (N-08); it uses the rollover-safe delta now. Neither file is in a
     native test environment (backlog CQ-06), so both are pinned by the
     scanner (`CQ2-C02a`, `CQ2-C12a` must return zero) and the builds only.
173. **`--setlog on` gained seven line kinds, closing the three questions the
     2026-09-02 OE3 mountain-gateway night report had to leave open (signal
     level, dedup window, queue latency).** The existing receive line now
     appends `RSSI:`/`SNR:`/`DUP:`/`OWN:`/`t=` (byte-identical prefix); new
     lines are `RLY` (relay decision with reason, ten codes), `TX` (own send
     with wait time, queue depth and CAD-attempt count), `ERR`
     (platform-independent RX error, previously ESP32-only under
     `--loradebug`), `STAT` (five-minute channel utilisation, dedup, ring
     high-water, drops and heap summary) and `GWI`/`GWU` (gateway inject and
     upload, so multiplication across several co-visible gateways and the
     upload-before-hop-decrement ordering are traceable per `msg_id`). All
     seven hang exclusively off `bDisplayLog` (`--setlog on`), none off
     `--loradebug`; RAM cost is under 64 bytes total (a handful of
     `std::atomic` counters plus one `ringSource[MAX_RING]` byte), no new
     buffers. Two behaviour notes: the 10-second channel-utilisation drain
     that now also feeds the `STAT` line runs independent of `--loradebug`
     (its own diagnostic prints are unchanged), and `stat_txn`/
     `stat_rx_err`/the dedup counters count on every transmission, error and
     reception regardless of the flag — only the line print depends on it.
     Field reference: `.claude/skills/logauswertung/SKILL.md` §
     "Zeilenreferenz `--setlog on`"; backlog entry §3.8v. Bench proof
     (Welle 3, RAK4631 + Heltec V3 over 30 minutes) is still open — no bench
     node was attached the evening this landed.

174. **The GPS altitude and QNH path (GPS-01..04, field report `OE5HWN-14`).** The
     GPS UART was drained only once every `gps_refresh_intervall` seconds while the
     module streams continuously into a 256-byte ring, discarding most of every
     cycle's NMEA and occasionally splicing two sentences into one that still
     passes the checksum (observed: a fix with `lon:0.000000` and `Date:
2015.14.00`); the node's reported altitude was a single unfiltered GPS sample
     with several metres of noise; and the barometric QNH reference latched to
     whichever fix happened to be first after boot and was never corrected. Fixed:
     `WZ_GPS_Feed()` now drains the UART on every loop pass while `WZ_GPS_Loop()`
     evaluates on its own timer; a plausibility gate (`gpsSamplePlausible()`,
     `gpsTimePlausible()`, `gpsDatePlausible()`) rejects null-island, out-of-range,
     and calendar-impossible samples before they reach persisted settings or the
     system clock; a scalar Kalman filter with dt-scaled process noise
     (`AltFilter`) replaces the raw altitude sample (measured 4.36→1.52 m RMS on
     the field capture); and the QNH reference re-latches once the filter
     converges, and on `--setalt` / `--setpress`, instead of only on the first fix.
     `--setalt` with an out-of-range value is now rejected instead of silently
     clamped to 0 m. Reviewed (`/fable-review`, ten findings, all fixed) before
     landing. Cost: ~16 B additional static RAM on boards with `ENABLE_GPS`, 0 B
     elsewhere. Bench verification (DK5EN-14) is still open — no bench node was
     attached at the time of this entry; see `gps-nmea-impl-plan-20260902.md` and
     `bug-GPS-uart-overflow-20260901.md`. Field logs from OE5HWN (T-Deck Plus and
     T-Beam Supreme, one hour each) then showed 2375 evaluations without a single
     corrupt sample, convergence after 88 samples as modelled, and the QNH
     re-latch after a reboot. The same logs exposed a fourth defect on the T-Deck
     **without** Plus (GPS-06): upstream `a672d18b` (v4.35p) swapped the GPS pin
     defines to the LilyGo assignment (`RX 44 / TX 43`), which is the reverse of
     what 4.35d's `SoftwareSerial(43, 44)` did on that board, so every self-wired
     module following the old assignment went silent. `detectBaudrate()` now
     re-scans once on the pre-4.35p pins when the variant defines a fallback
     (only `variants/t_deck`), keeps the effective pins for every later
     `begin()`, and logs a request to swap the wires.

175. **T-Deck key auto-repeat** (TD-10). Holding
     Backspace, Space, or any alpha key on the T-Deck / T-Deck Plus keyboard
     now repeats it through LVGL's existing keypad-repeat mechanism (400 ms
     delay, 100 ms rate) instead of inserting exactly one character no matter
     how long the key stays down — the keyboard MCU (I2C slave `0x55`)
     delivers one character per matrix transition and no release event of its
     own. A new state machine (`src/t-deck/kbd_repeat.h`) opens the stock
     keyboard firmware's raw-mode 5-byte live-matrix window (I2C `0x03`/`0x04`)
     for the duration of a hold, arming only when the frame shows the exact
     matrix cell of the key that was pressed so a fast key-to-key transition
     cannot bind the wrong key. A keyboard whose firmware predates raw mode
     (LilyGo commit `1eb6fb0e`, 2025-06-11) degrades to today's
     one-character-per-press behaviour automatically, at a cost of about 1 ms
     of extra I2C per key press and no other functional change. 39 native test
     cases (`test/test_kbd_repeat`); reviewed and fixed against six findings
     (`docs/review-verdict-tdeck-keyrepeat-20260902.md`). Bench proof on
     DK5EN-14 (T-Deck Plus): the keyboard answered `support;1`, hold windows of
     0.8–1.2 s for Backspace, Space and `d`, repeated deletion seen on screen.
     Field counter-example the same evening (OE5HWN, T-Deck Plus): every probe
     answered `00 00 00 00 00`, verdict stayed `unknown`, one character per
     press — the pre-2025-06 controller firmware, degrading exactly as designed.
     Because the probe line is capped at five presses per boot, `--info` now
     prints `...KBD raw-mode yes|no|unknown ...KEYLOCK on|off` on T-Deck builds
     (`docs/tdeck-keyrepeat-impl-plan-20260902.md` §3.6).

## New in v4.35p.09.01.2-stability

A same-day second cut on top of `v4.35p.09.01-stability`: one field-driven
back-pressure calibration (the "slow down" warning no longer fires on a
gateway's relay load), one battery-measurement fix from upstream for the
Heltec Wireless Stick V3, and the upstream `dev` sync that carries this
fork's own tooling PRs back in. `FLASH_VERSION` stays `20260901` (same-day
cut, 08.27.2 precedent); native suite 489 test cases across 12 host
environments. Only the live gateway (dk5en-98, Heltec V3) ran this build
before publishing.

167. **QRS ("slow down") needs the sender's third own message on a full
     ring, not just the ring depth.** `txRingDepth()` counts relay frames,
     ACKs and beacons, so a gateway idling at depth 4 handed the very first
     typed message a QRS for a queue the sender had not built — even with the
     BP-05 line at 5 the warning sat on top of the relay load and read as
     "too sensitive" in the field. `onSend()` is called exactly once per
     locally originated message and never for relay/ACK/beacon traffic, so
     the state machine now counts its own calls: QRS fires when
     `QRS_MIN_USER_MSGS` (3) own messages in a row find the ring at or above
     `qrsThreshold()`; a sighting below the line — in `onSend()` or in the
     drain `poll()` between two typed messages — restarts the count, so a
     sender whose messages drain between keystrokes never gets one. QRT and
     QTA are not gated by this (a refused or lost message is reported at
     once), and latch, hysteresis and QRV hold are unchanged. Pinned by four
     new host cases (third message fires, restart via `onSend()`, restart via
     `poll()`, QRT/QTA/`reset()` exemption); the integrated burst regression
     now expects QRS at depth 7.
168. **Heltec Wireless Stick V3 measures its battery again** (upstream PR
     #1119, OE3LCR, issue #1116). The variant carried `ADC_MULTIPLIER 4.9245`,
     copied from the Vision Master E213; a reference measurement (Metrahit
     TRMS, 4.093 V under load against a firmware reading of 4.88 V, 991 ADC
     counts = 798.6 mV, real divider 5.125) gives 4.13. With the old value the
     reading fell outside the `battDetectUpdate()` plausibility band
     (1.15 × MAXV), so the node reported "no battery hardware", `BATT 0.00 V`
     and omitted `/B=` from its APRS positions.
169. **PlatformIO upload works on hosts without `upload_port`; nRF52 core
     warnings no longer bury real ones** (upstream PR #1118, this fork).
     `upload_protocol = custom` never runs the espressif32 builder's port
     autodetect, so `$UPLOAD_PORT` in the variants' `upload_command` stayed
     empty and esptool aborted; with `esptool` the autodetect (or an explicit
     `--upload-port`) runs first and PlatformIO still substitutes our
     `upload_command`. `-Wall -Wextra` moved from `build_flags` to
     `build_src_flags` on the nRF52 and ESP32 bases: in `build_flags` they
     also hit the Adafruit nRF52 core and SoftDevice headers and produced over
     23,000 `-Wunused-parameter` warnings per full build. Upstream's
     `v4.35p compile` (#1117) dropped the flags from the remaining variants
     and the unused `--port` from the T-Deck upload commands; both trees are
     in sync.

## New in v4.35p.09.01-stability

Everything since `v4.35p.08.31-stability`, in its final form: the complete TX
back-pressure and receipt system (BP-01 through BP-10 — described here as it
now stands, not as it grew), the OTA-tooling wave (TM-46/47/48), a meshlogger
hardening from a real overnight incident (TM-50), and the removal of two
repairs whose upstream feature no longer exists. `FLASH_VERSION` is
`20260901`; native suite 485 test cases across 12 host environments;
bench-verified on Heltec V3, T-Beam v1.2, T-Deck Plus and RAK4631.

153. **Safeboot recovers from an aborted OTA upload** (TM-46). A central
     `abortActiveUpdate()` with an onDisconnect hook and a session generation
     counter clears the stale session state that turned every retry after a
     killed upload into HTTP 400 — plus a cross-task unsigned-underflow race
     in the stall watchdog that could abort healthy uploads (signed deltas +
     `volatile`). Abort-retry proven twice on the bench: kill 5 s into the
     upload, immediate retry completes without `--force`.
154. **The safeboot WiFi join uses the production join pattern** (TM-48):
     driver-picked AP selection and PMF-off (`esp_wifi_disable_pmf_config`),
     the same TM-34 fix that got the main firmware onto WPA2/WPA3 transition
     APs. `[SAFEBOOT];wifi;pmf_off;rc;0` marker proven, ~170 kB/s at a
     -74 dBm desk link.
155. **`tools/webflash.py` handles the T-Deck family without `--force`**
     (TM-47): hardware strings keep the `+` (expects `TDECK+`), a
     safeboot-resume path picks up a node already sitting in safeboot, and
     `--self-test` runs the parser checks offline.

### The TX back-pressure and receipt system (BP-01 through BP-10)

A node that cannot send tells the sender — honestly, in the right
conversation, with the text that was lost. The system stands on an honest
queue measurement, calibrated thresholds, and per-message receipts; it was
built against two field incidents (the DJ8MEH 8-minute phantom refusal and a
live-gateway baseline capture) and is pinned by integrated regression suites.

156. **TX-ring depth counts occupied slots, not index distance** (BP-02).
     `txRingDepth()` and every report site (`RING_WRITE`, `RING_STATUS`,
     `TX_GATE_ENTER`, `TX_START`, `RING_TX_READ`) previously used
     `iWrite - iRead`, which counted freed holes behind a priority-starved
     entry parked at the read pointer as still queued — the DJ8MEH field log
     showed `queued=19` with 3–4 slots really occupied, and a refusal
     episode ran 8 minutes on that phantom number. `RING_STATUS`
     additionally carries the old distance as a trailing `dist=` field, and
     the RING_ZOMBIE detectors in `tools/serial_monitor.py` and
     `tools/loganalyse.sh` key on `dist==0` (falling back to `queued==0` for
     older logs).
157. **Stale BACKGROUND entries age out of the TX ring** (BP-03). A 2-second
     main-loop sweep (`txRingAgeBackground()`) drops HEY entries (priority 5) older than `RING_BG_MAX_AGE_MS` (3 min) with a `RING_DROP_STALE`
     marker and advances the read pointer; the DJ8MEH blocker — a starved
     HEY relay pinned at the read pointer — sat for 10 minutes and only left
     via a chance priority eviction. Deliberately not in `getNextTxSlot()`,
     which also runs on the nRF52 timer task; on nRF52 each slot's
     check-and-drop is atomic under the ring lock, external-radio
     EXT_PENDING slots are exempt, and the age math survives the 49.7-day
     `millis()` rollover. Field-proven in the 2026-09-01 overnight soak: 13
     stale drops, zero stuck rings.
158. **A refusal episode ends when the load is really gone — and not
     before** (BP-04). Depth 0 closes immediately; depth 1 (the water band)
     closes after a 10-second uninterrupted hold (explicit armed flag, time
     injected so the state machine stays host-testable). A relay node with
     steady background traffic rarely reaches exactly 0 and previously kept
     refusing user messages long after the real backlog had drained. The
     2026-08-30 anti-flap case (depth 2 → 1 → 2 inside 400 ms) is pinned as
     a regression test.
159. **QRS fires at depth 5 (fixed across all boards), QRV only after a
     refusal** (BP-05). A 5.5-minute normal-operation capture on the live
     gateway showed the ring baseline sitting at depth 1–4 (mode 2) with
     three false QRS/QRV pairs and no burst anywhere — the old QRS rule
     (depth > 1) sat directly on that baseline, so every single message
     triggered "slow down". QRS now needs depth ≥ 5 (flat on every ring
     size, defensively clamped below the QRT threshold), the 2–4 band is
     silent, and QRV is only sent when the episode actually refused (QRT)
     or dropped (QTA) a message — a QRS-only episode closes silently. The
     recorded baseline traffic pattern is pinned as a regression test.
     Depth alone still counts relay frames, ACKs and beacons, and on a
     gateway idling at depth 4 the very first typed message got a QRS for a
     queue the sender had not built — so QRS additionally needs the sender's
     **third own message in a row** on a ring at/above the line
     (`QRS_MIN_USER_MSGS`); a dip below 5, seen in `onSend()` or the drain
     `poll()`, restarts that count. QRT and QTA are not gated by it.
160. **Back-pressure notices are normal messages from the node's own
     callsign, addressed to the conversation that triggered them** (BP-01,
     BP-06). The BLE/web frame carries the node call as sender instead of
     the pseudo-sender `response` (which McApp files under its spam class),
     and the EXTUDP datagram is shaped exactly like a text message received
     over LoRa (`src_type` `lora`, `type` `msg`, numeric firmware version)
     instead of a distinct `notice` type no client rendered. A notice for a
     message into group 20 arrives as a message to `20` and shows up in
     that group's chat; for a DM it arrives in that DM thread — visible only
     to the sender, since notice frames stay `msg_app_offline` (BLE) /
     app-only UDP datagrams and never go on air. Serial and the T-Deck GUI
     stay plain text. Both framings are extracted into testable headers and
     pinned natively (`test_bp_notice_frame`, `test_extern_notice_json`).
161. **Every refused or dropped message gets an app-visible receipt carrying
     its own text** (BP-07). A deliberately never-latched per-message
     vocabulary (`BpNack`), separate from the latched episode notice, sends
     one frame per lost message — `QRT NOT SENT - <text>` /
     `QTA NOT SENT - <text>` — truncated to 120 bytes on a UTF-8 codepoint
     boundary (`charset_utf8_safe_truncate()`) with quotes, backslashes and
     control bytes sanitised before JSON escaping, because an unsanitised
     text full of quotes could double in size on EXTUDP escaping and
     overflow the datagram. The refusal check runs after the message is
     decoded and the `{ZIEL}` destination prefix is stripped, so the
     receipt carries the real typed text — this deleted a ~60-line second
     destination parser and moved invalid-length and DM-to-self rejection
     ahead of the back-pressure check, since "TX buffer full" was never the
     right reason for a malformed message. A ring drop is classified as
     back-pressure only when the depth actually stands at the refuse
     threshold — a factory-fresh node's drop no longer forces a false QRT
     episode on an empty ring. Every back-pressure frame draws its `msg_id`
     from a shared monotonic counter (49.7-day-rollover-safe) instead of
     raw `millis()`, because the drop path emits two frames in the same
     millisecond that would otherwise collide in the app's dedup filter; a
     receipt also latches the episode, so the later all-clear (QRV) reaches
     everyone who was told about a loss. The console marker logs the
     sanitised text, gated on `bLORADEBUG`, keeping user content out of the
     always-on multi-day 2323 log capture.
162. **A message the TX ring drops no longer echoes as sent, and never
     reaches the backbone** (BP-08). The app echo for a locally typed
     message used to go out before the ring was even asked — a message the
     ring then discarded looked successfully sent in the chat, and on a
     gateway the UDP uplink to the central server ran independently of the
     ring outcome: a discarded message still reached the server without a
     single RF neighbour ever hearing it. The ring write now runs first; on
     a drop the function returns before the echo, before
     `insertOwnTx()`/`addLoraRxBuffer()`, and before the gateway uplink. A
     message enters the network whole or not at all; the sender gets
     `QTA NOT SENT - <text>` and knows to retry. Concurrency reviewed:
     `doTX()` and `sendMessage()` both run only from the main loop on both
     platforms.
163. **`sendMessage()` returns a result, and the operator's typed text
     survives a refusal on all three GUIs** (BP-09). `BpSendResult` (0
     accepted, -1 refused, -2 dropped, -3 invalid) comes back from all four
     `sendMessage()` signatures. The T-Deck and T-Deck Pro clear their
     input field only on success while still switching to the message list,
     so the receipt is visible either way; the web API answers
     `sendmessage refused`/`dropped`/`invalid` instead of a blanket `ok`,
     and the web GUI's JavaScript — which cleared both input fields
     immediately after `xhttp.send()` without ever reading the response,
     the identical text loss one layer up the stack — now waits for the
     response and clears only on `sendmessage ok`. A 140-byte frame buffer
     moved off the 4 KB nRF52 loop-task stack along the way.
164. **The whole incident class is pinned by integrated regression suites.**
     `test_bp_regression` (native_aprs) replays the DJ8MEH field incident
     end-to-end over the real ring plus the real state machine: burst → one
     QRS/one QRT → refusals → drain with a pinned prio-5 blocker → QRV
     within 10 s (field: 8 min), the blocker age-out path, and an
     over-correction guard proving QRT still fires at the 80 % threshold —
     each ring/state fix mutation-verified to turn the suite red on its
     own. `test_flood_13_into_10_yields_five_nacks` pins the receipt path:
     13 sends into a 10-slot ring, 8 accepted, 5 refused, 5 receipts. The
     gateway baseline capture and the anti-flap case are pinned as
     regression tests alongside.

### Housekeeping

165. **Two repairs left the tree together with the upstream feature they
     repaired.** Upstream reverted the extended Mheard JSON — the per-hop
     `PP` link chain (#1105), `SRC`/`GW` (#1106) and the serializeJson
     buffer guard (#1107) — and removed the string-typed `FWDATE` from the
     BLE `I` register entirely; this tree follows both reverts. The
     PP/DIST fail-soft in `updateMheard()` (item 105) removed a key that no
     builder sets any more and is gone as dead code, and the `FWDATE`
     buffer fix (item 102) and re-typing (item 104) are moot — the key no
     longer exists on either side, the register carries `FWVER` alone. What
     remains is only what still repairs live code: the HEY input bound
     (item 106) and the buffer-bounded BLE JSON framing.
166. **meshlogger detects a zombie TCP session after the target node drops
     off WLAN** (TM-50). In the 2026-09-01 overnight soak a router reboot
     killed the node side of the 2323 console session without a FIN; the
     logger sat 2.4 hours on `recv()` timeouts (reconnects=0) and could not
     restore the debug flags at the end (broken pipe, `loradebug` stayed
     on). `tools/meshlogger.py` now runs a silence watchdog
     (`--stall-timeout`, default 90 s) that lifts a mute connection into
     the existing reconnect path, sets SO_KEEPALIVE with platform tunables
     as a second line of defence, re-applies the flags idempotently after
     every reconnect, and retries the end-restore once on a fresh
     connection. `tools/bench/test_meshlogger.py` replays the incident
     against a fake console server, verified fails-before.

## New in v4.35p.08.31-stability

The complete field campaign since the 08.28 hotfix: two `/orchestrate-waves`
passes over a 4-board bench (Heltec V3, T-Beam v1.2, T-Deck Plus, RAK4631 with
W5100S Ethernet), 438 native test cases in 12 host environments, and soak runs
up to 9.1 h. Every item below is written up with file references and
measurements in [`docs/pr-draft-20260831.md`](pr-draft-20260831.md); the item
IDs (WEB-03, TM-35, GW-01 ...) match the fork's engineering backlog. This
release deliberately ships the full serial instrumentation (`[TAG];key;value`
markers, `--debug`, `--udplog`, `--wifistat`, injection commands), so a field
node can produce machine-readable debug logs for later analysis.

### Security and memory safety

107. **Stored XSS in the web message page fixed.** Received message texts,
     callsigns and paths were printed into the HTML raw and injected via
     `innerHTML` — any LoRa sender could run script in the operator's browser
     session, which also reaches the configuration pages. All six mesh-origin
     output sites are now HTML-escaped; the send path uses
     `encodeURIComponent`, so `&`, `#`, `+` no longer break the query (WEB-03).
108. **Three web JSON endpoints escaped nothing** — a `"` or `\` in a
     percent-decoded query broke the response. They now stream through
     `serializeJson()` (JSN-01).
109. **EXTUDP JSON: double-escaping removed and four `serializeJson()` calls
     re-bounded to the buffer size** — a too-long document previously overran
     the buffer, reachable from any unauthenticated LoRa peer (JSN-01).
110. **All 13 BLE register builders go through one fail-soft frame path**: it
     is buffer-bounded, and over the phone budget it omits whole fields instead
     of cutting mid-string into unparseable JSON. `BLE_JSON_PAYLOAD_MAX = 244`
     is derived from the `blelen + 2` `uint8_t` overflow that silently dropped
     frames at 253+ characters (JSN-01/UP-01).
111. **Frames from unconfigured nodes (`XX0XXX...`) are discarded on RX, and an
     unconfigured node does not transmit at all** — a factory callsign was
     observed relayed over four hops. Guards sit before mheard/display/BLE/
     gateway-upload/relay, plus a hard backstop in `doTX()` and the TX ring
     (RX-01/TX-01).
112. **Eight parser findings from the test campaign fixed** — among them:
     `decodeAPRSPOS()` accepted any byte as hemisphere and 11-digit latitudes;
     `decodeMHeard()` pulled NUL padding into the date field; the EXTUDP frame
     buffer was 3 bytes short; empty `<VT>` telemetry formatted an
     uninitialized stack float; timezone `-03:30` parsed as -3.30 (PT-01).
113. **One 255-byte datagram no longer kills EXTUDP receive permanently** — on
     arduino-esp32, a partially-read `rx_buffer` made `parsePacket()` return 0
     until reboot, silently. The remainder is now flushed and the incident
     reported (UDP-02).
114. **`addUdpOutBuffer()` no longer reads one byte past the caller's
     payload** (WF-01).
115. **Settings are sanity-checked after loading from flash** — a corrupt
     structure previously fed out-of-range values straight into
     `radio.setOutputPower()` and friends; on nRF52 35 char fields are
     additionally NUL-terminated (TM-32, upstream #661/#57).
116. **Classic-ESP32 ring buffers resized to fit their RAM segment**
     (`MAX_RING` 30→20, `MAX_RING_UDP` 25→20): at 30, the T-Beam had 0.5 kB of
     `dram0_0_seg` left. A 20-slot ring still saturates long before the radio
     drains (MEM-01).

### Character-set hygiene

117. **A UTF-8 allowlist filter runs at the two chokepoints that cover every
     path** — RX in `decodeAPRS()`, TX in `encodePayloadAPRS()`. Control
     characters, invalid/overlong UTF-8, encoded surrogates and bidi/zero-width
     format characters are removed; umlauts and emoji pass. The recorded
     third-party APRS interop corpus passes unchanged (CHR-01).
118. **The APRS free text (`node_atxt`) additionally loses the structure
     separators `{ } : ; , /` and is truncated UTF-8-safely** at the 25-byte
     receive limit — at frame build time, so web setup, T-Deck GUI and flash
     restore writers are all covered (CHR-02).

### Time, clock and MHeard

119. **NTP is asynchronous.** The stock client blocked the loop for up to 1 s
     per refresh (nRF52: 1.6 s every ~20 s) and discarded waiting gateway
     datagrams from the shared socket. The new client only sends; the regular
     receive path harvests the reply, with strict validation and a
     2.5 s / 5 s / 60 s retry ladder (TM-35).
120. **A non-gateway node actually reads the NTP reply now.** The harvest ran
     only under `--gateway on`; a gateway-off, GPS-off node sent one request
     per minute forever and never got a clock — measured 0 of 545 replies in a
     9.1-h soak. Both main loops now have a harvest-only branch (TM-45).
121. **`--ntpsync`** forces an immediate synchronization with honest result
     states, and works with an active GPS fix (NTP-01).
122. **MHeard ages on the monotonic clock, not the wall clock.** Without
     NTP/GPS, `getUnixClock()` returned garbage, every entry looked expired,
     and the `/N` NCNT tag stayed 0 forever. All aging comparisons now use
     rollover-safe `millis()` stamps; epoch stays for display (NC-01/NC-02).
     Dead eviction logic in the MHeard table is repaired (MH-02).

### Network

123. **WiFi first join on WPA2/WPA3 transition APs fixed** — SAE without PMF
     failed 24/24 boots on the bench AP (AUTH_EXPIRE); with the PMF-off policy
     the station authenticates WPA2-PSK 24/24. Driver-owned AP selection,
     event-driven `got_ip`, a staged watchdog, and async DNS instead of a
     31-s `hostByName()` on the loop task (TM-34).
124. **WiFi bring-up no longer blocks the main loop**; a healthy node is 4-5 s
     earlier online (TM-20/TM-16).
125. **An ESP32 gateway never relayed a UDP position frame to LoRa** — the
     dedup gate was evaluated after its own ring insert, so every frame
     deduplicated against itself: 0 of 30 injected frames radiated. The gate is
     now read first; 30/30 after the fix (TM-31, answers upstream #568).
126. **nRF52: link poll, heartbeat and DHCP renewal escaped the gateway
     block** — a node with gateway off never renewed its DHCP lease (ETH-01).
127. **nRF52 server choice: the DHCP path now honors the country split**
     instead of always falling back to the OE default (CTY-01).
128. **Server CONF frames are understood on ESP32 and actually applied on both
     platforms** — shared TLV parser with strict length checks, source-IP
     guard, regex validation, save, auto-reboot (TM-39/CONF-01).
129. **`--nopmother`**: opt-in filter that keeps foreign direct messages away
     from the EXTUDP peer; broadcast/group always passes. Default off — the
     existing fleet behaves unchanged (PM-01).
130. **A dropped outgoing message is no longer silent.** `sendMessage()` used
     to discard the TX-ring return value. New Q-code episode model: QRS as the
     queue grows, QRT at 80 % (locally originated user messages are refused
     before message-ID consumption), QTA on drop, QRV once when the queue
     clears — always on the transport the message came from, never on the air.
     Relay, ACK and beacon traffic is never refused (BP-01/TM-37).
131. **A gateway no longer self-uploads its own `'@'` HEY to the server.** The
     bare copy (rssi/snr 0, no signal report) always arrived seconds before the
     neighbours' enriched copies of the same msg_id; measured 2026-08-31
     against the server's interlink stream (which carries every copy, no dedup),
     a first-copy consumer loses the link data exactly when `--gateway on` is
     set. With the fix, only the enriched neighbour copies reach the server —
     parity with `--gateway off`; RF transmission and enrichment of foreign
     HEYs are unchanged (GW-01).
132. **Shot-path beacons have a 30-s floor** — `--sendpos`, the user button,
     EXTUDP telemetry injection and `--sendhey`. Field evidence: 25,146
     position frames in 21 minutes from one node. Periodic paths unchanged
     (FL-01/FL-02).

### Energy, web, commands

133. **"No battery" is detected on ADC boards** — a floating divider measured
     3,716-4,886 mV with 1.17 V jumps per half-second, shown as a jumping
     percentage and beaconed as `/B=`. A hysteresis detector on the raw sample
     now suppresses both; implemented in both battery implementations
     (BAT-01/BAT-02).
134. **`getparam()` — the read half of the web API — was completely dead**
     (searched for `"/setparam/?"` and cut from the wrong side of `=`); fixed
     (CS-04).
135. **Config backup/restore**: `GET /config.json` exports the full
     configuration, `POST /config` restores it all-or-nothing with per-value
     range checks, layout version check and CRC-32. The file contains secrets
     in plain text and the page says so (CS-03).
136. **Web info page: real NTP server, BSSID line, honest battery** ("USB (no
     battery)" instead of "0.000V (100%)") (WEB-01/WEB-02); `sendpos` from web
     setup no longer toggles `--nomsgall` (WEB-04); the no-IP log flood
     (~125 lines/s) prints once per state (TM-21).
137. **`--maxhop <1..6>`** makes the text hop limit settable and persistent,
     over serial and as a web-setup drop-down from the same rule source
     (CS-01/CS-02).
138. **`--help` matches reality**: parity from 95 to 145 commands, thematically
     grouped, board-gated like their handlers; the U+2212 copy-paste trap in
     `--settime` is gone (DOC-02).
139. **GUI-only switches are reachable over serial**: `--wifi on/off` (whose
     load-default bug booted freshly flashed nodes with WLAN off — fixed),
     `--mute` persists, `--persistsd`/`--persistflash`/`--immediatesave`/
     `--persiststat` (HL-01..04).
140. **`--display off/on` now really darkens the T-Deck TFT** — backlight off
     plus panel sleep, keys/touch wake as usual (TM-33b, upstream #690).

### T-Deck / T-Deck Plus

141. **Audio no longer blocks the main loop** — `play_cw()` stalled LVGL, LoRa
     and serial for ~1.1 s per message. Audio runs in its own task (prio 3)
     with a queue, and every SD access and `audio.loop()` holds a real SPI2 bus
     mutex (TM-01..04).
142. **GT911 touch init retries 5×** — the controller has no reset line, and a
     single failed `begin()` used to be final (TM-33a, upstream #64).
143. **The trackball counts edges in interrupts** — the old 10-ms level polling
     lost ~75 % of the motion (TM-18).
144. **The map pans**: `i/j/k/l` pan by a quarter viewport, `o` recenters,
     `g/h` zoom; `sdmap_refresh()` composes the viewport from all intersecting
     tiles (previously exactly one tile). Auto-recenter is gated on "user has
     panned". Known limit, documented: without a tile cache a pan step costs
     0.33-0.79 s (TD-07/TD-08).
145. **Message-list trim, zoom use-after-free, draw-buffer size**: the active
     tab's list view no longer grows unbounded (TD-03); a marker slot is nulled
     after `lv_obj_del()` (a reboot in the PSRAM heap otherwise, G01);
     `lv_disp_draw_buf_init()` gets pixels, not bytes (G07).
146. **Idle and boot cost**: header indicators write only on change (36.9 → 7.0
     invalidations/s), boot messages pump LVGL 100 ms instead of 2 s, SD clock
     800 kHz → 20 MHz. T-Deck boot 14.9 → 10.9 s over 24 boots
     (TM-08/TM-15/TM-16).
147. **The lost-flush mechanism is instrumented and its register is named.**
     The first TFT flush after an SD access on the shared SPI2 bus is lost; a
     NOP throw-away transaction re-arms the bus (default on). New `--spitrace`
     traces bus users and registers per flush; measurement 2026-08-31: the
     clobbered register is **`GPSPI2.clock`, exclusively** (SD leaves
     `00243002`/`00041001` behind, TFT runs `00001001`; `user`/`ctrl` never
     change, LoRa activity changes nothing) — the NOP can become a targeted
     clock re-arm (TM-05/TM-07).

### OLED boards, nRF52, build

148. **OLED: hardware I2C and a full-frame buffer** — software bit-banging cost
     579 ms of loop stall per frame; now `Wire1` at 400 kHz (TM-09/TM-22), and
     an unchanged frame is not re-sent (CRC-32 over the U8g2 buffer,
     TM-10/TM-27).
149. **nRF52 GPS version probe only in debug** — it cost every boot >0.5 s for
     one debug line; `SetupUBLOX()` 1.93 → 1.33 s (TM-16).
150. **`-Wformat=2 -Werror` for our own sources** on both platforms; libraries
     build as before.
151. **Every variant passes `--port "$UPLOAD_PORT"` to esptool** — with several
     boards connected, esptool used to guess and flash the wrong device; and
     safeboot builds repair their Tasmota-fork framework directory themselves.
152. **Field instrumentation ships enabled** (`INSTRUMENT_ENABLED=1`,
     `MC_INJECT_HOOKS=1`, fully compile-out-able): loop-period measurement with
     stall attribution (~35 probe points), `[WIFI]`/`[ETH]`/`[GW]`/`[UDP]`/
     `[NTP]` markers, `--wifistat`/`--ethstat`/`--udpstat`/`--udplog`,
     `--heap`, `--instr`, and the injection/test block (`--injectmsg`,
     `--injectpos`, `--injectraw` through the real RX path, `--loratx` bursts,
     T-Deck key/trackball/touch injection, `--disptest`, `--spitrace`). No
     marker is unthrottled; flood candidates are rate-limited or bound to their
     `--...log` switches. This is what makes a field node debuggable: turn on
     the relevant markers, capture the serial or TCP-2323 console, and the logs
     can be analyzed offline. One deliberate exception: the RAM-tightest
     variant `E22_XML-DevKitC` (classic ESP32 plus the soft-serial/XML stack,
     already built without the network console) compiles out the two
     bench-only modules — frame capture (`MC_CAPTURE=0`, ~1.4 kB static RAM)
     and the injection hooks (`MC_INJECT_HOOKS=0`) — because the campaign's
     additions had pushed that one link 648 bytes past `dram0_0_seg`. All
     field markers and log switches remain available there.

## New in v4.35p.08.28-stability

Three defects in the BLE-to-phone frame path. Item 104 was actively breaking
node identity in the field; 105 and 106 were latent. All three share one root
cause: the size limit a builder checks is not the limit that applies.

104. **The BLE `I` register no longer fitted in a frame, so apps lost the node's
     identity entirely.** Upstream `82db3d41` changed `FWDATE` from the integer
     `FLASH_VERSION` to the string `__DATE__ " " __TIME__` — 14 characters more.
     `command_functions.cpp:4996` checks the document against
     `MAX_MSG_LEN_PHONE - 2` (298), but the clamp that actually applies is in
     `addBLEComToOutBuffer()` (`loop_functions.cpp:608`) at 245 bytes, so 244
     characters of JSON. Past that the firmware cuts mid-value and the result is
     not a shortened object but an unparseable one: `CALL`, `ID` and `HWID` go
     with it. Measured on a gateway in service: 59 dropped registers in 9.5
     hours, one per reconciliation cycle, from the moment the firmware came up.
     `FWDATE` carries `FLASH_VERSION` again. The key name is unchanged; only the
     type moves from string to number. Note that a node with all six group-call
     slots filled is **still** 13 characters over — that case predates the
     regression and needs `GCB0`…`GCB5` sent as an array, which apps can see.

105. **Mheard records could be lost silently at high hop counts.** The `PP` link
     chain grows about 11 characters per relay. `addBLEOutBuffer()`
     (`loop_functions.cpp:537`) clamps `'D'` frames at 255 rather than 245, and
     `sendToPhone()` computes the write length in a `uint8_t`
     (`phone_commands.cpp:126`): at 253 characters of JSON, `blelen + 2` wraps
     to 0 or 1 on ESP32/ESP8266 and the frame goes out as a zero- or one-byte
     write, with no log line. nRF52 is unaffected — there the expression is
     promoted to `int`. At the default hop setting the record sits near 214
     characters, but `{SET}` allows up to `MAX_HOP_LIMIT`. The builder now
     measures before serialising and drops the most expensive optional field
     first — `PP`, then `DIST`, which is recomputable from both stations'
     coordinates — and logs the omission. A record without `PP` is still fully
     usable; a byte-truncated one is not parseable at all.

106. **The HEY link chain was unbounded on input.** `appendHeySignalReport()`
     (`aprs_functions.cpp:1127`) appended to whatever payload arrived off the
     air. Regular operation is bounded by `MAX_HOP_LIMIT`, a malformed or
     hostile `@` packet is not, and the re-encode downstream then truncates the
     frame on a byte boundary — mid-group, which `updateHeyPath()` cannot parse.
     The function now stops appending once the next group would exceed
     `HEY_PATH_PAYLOAD_MAX`, sized so the longest legitimate chain never reaches
     it. Ending the chain loses less than cutting it.

Supporting changes: two named constants (`BLE_JSON_PAYLOAD_MAX`,
`HEY_PATH_PAYLOAD_MAX`) replace numbers that were previously scattered and
unexplained; four new cases in `test_hey_report`, two red without the fix and
two guard tests pinning down what must not change; and two issue reports under
`docs/`, one per defect, addressed to the authors of the code in question with
the arithmetic, code references and fix proposals.

Not fixed here, and stated in the reports: the `blelen + 2` overflow itself
(this release only stops the Mheard path from reaching it), the 14
`serializeJson(doc, buf, measureJson(doc))` call sites that leave no room for
the terminator, the `MAX_MSG_LEN_PHONE - 2` check in every register builder,
the fact that the firmware never reads the negotiated ATT MTU, and the schema
asymmetry between the live Mheard builder and the `--mheard` table dump.

## New in v4.35p.08.27.2-stability

A rebase-and-repackage release. The base moved from merge-base `8114d7ae` to
upstream `dev` at `fc83554e`, so this build also carries everything upstream
added in between: SD-card offline map tiles for the T-Deck Plus, the T-Echo
BME280 fix, the extended Mheard JSON (per-hop RSSI/SNR in the HEY link chain,
originating callsign, gateway identifier), the build date in the Info JSON, and
the new BLE TYPE `I` field `FWDATE`. Two items are ours:

102. **`FWDATE` was truncated by one character.** Upstream's new BLE TYPE `I`
     field is built with `snprintf(cfwdate, sizeof(cfwdate), "%s %s", __DATE__,
__TIME__)` into a `char cfwdate[20]`. `__DATE__` is 11 characters
     (`Mmm dd yyyy`), the separator 1, `__TIME__` 8, the terminator 1 — 21
     bytes. `snprintf` truncated silently, so the last digit of the seconds was
     lost from every reported build timestamp. The buffer is now 24 bytes. Found
     by `-Wformat-truncation`, which the default warning level does not enable;
     returned upstream as PR #1103 and merged.

103. **A probe flag was declared outside the block that uses it.** In
     `batt_functions.cpp` the one-shot guard `battProbeDone` for the ADC_CTRL
     polarity probe (item 89) sat outside `#if defined(ADC_CTRL_PIN)`, while
     every use of it sits inside. On boards without that pin — E22-DevKitC,
     t_deck, t_deck_plus — it compiled as an unused variable. Declaration moved
     into the block. No behaviour change on any board.

## New in v4.35p.08.27-stability

Six defects fixed, one new diagnostic, and a test layer that replays real field
traces through the shipping code instead of through a re-implementation of it.

88. **Updating a node no longer erases its configuration.** The reset condition
    was `node_fversion != FLASH_VERSION`, and `FLASH_VERSION` is a date that is
    raised for every release. Every update therefore discarded the stored
    callsign, WiFi credentials, and sensor and network settings of every node —
    even when the layout of `struct s_meshcom_settings` had not changed at all.
    That is exactly what happened on the 20260724 → 20260821 step: the commit
    that raised the number touched neither flash header. Build identity and
    layout generation are now two separate things. `FLASH_VERSION` remains the
    release stamp shown in `--info`; `FLASH_STRUCT_VERSION` names the settings
    layout and is the only value `clear_flash()` looks at. It stands at
    20260724, the last real layout change. Nodes that stored 20260821 under the
    old semantics are grandfathered in and are not reset. Verified by updating
    three nodes — Heltec V3 and T-Beam over OTA, RAK4631 over DFU — each
    keeping its callsign and network configuration. `--flash-reset` still
    resets, as it should.
89. **The battery divider polarity is measured at boot instead of guessed at
    compile time.** The switch for the battery voltage divider was chosen by
    `#if defined(BOARD_HELTEC_V31) || defined(BOARD_WIRELESS_PAPER)` — and
    `BOARD_HELTEC_V31` is defined **nowhere** in the tree, neither in a
    variant's `configuration.h` nor in any build flag. The active-LOW branch
    was therefore unreachable in every image ever built, and every Heltec V3
    was driven active-HIGH regardless of whether the board is really a 3.0 or a
    3.1. The polarity is now probed once at boot. The signature is unambiguous
    and was measured on the device: enable=HIGH gives 902–906 counts,
    enable=LOW gives 1–4, and the threshold sits at 50. On a board that was
    already correct the change is a no-op, confirmed on a Heltec V3 with a real
    battery. The compile-time value stays as the seed and the fallback, so
    boards where the probe cannot run behave exactly as before.
90. **An empty battery is now distinguishable from no battery, on the air.**
    `mv_to_percent()` clamps to zero below `BAT_MIN_VOLTAGE`, and the `/B=` tag
    was only written `if(global_proz > 0)` — so a nearly empty pack reported
    nothing at all, and the battery graph went blank precisely when it
    mattered. A survey across 1230 stations found real nodes that report
    battery only in a six-hour window per day, purely because the pack crosses
    the 3.3 V line. The two `/B=` producers in `PositionToAPRS()` also
    disagreed with each other: the INA226 branch wrote `/B=%i` unchecked, the
    normal branch `/B=%03d` above zero. Both now write the tag whenever battery
    hardware is present. A transmitted `/B=000` means "measured, and the pack is
    empty"; a missing tag means "this node has no battery to report". Decoding
    is unaffected — the receiver reads the field with `sscanf("%d")`, so all
    three spellings yield the same integer. Fixed in the same pass: the PMU
    failure path on ESP32 zeroed both values without marking them unmeasurable,
    which would have made a T-Beam with a dead PMU claim an empty battery
    forever.
91. **Text fragments were being accepted as ACKs and flooded into the mesh.**
    `handleACK()` checked only `payload[0] == 0x41` and a minimum length. 0x41
    is the ASCII letter `A`, so any fragment of a text or position packet
    beginning with it ran through ACK processing: bytes 1–4 were taken as its
    message id, it entered the dedup ring, it was queued for transmission at
    priority 1 — evicting a heartbeat from a full queue — and it was relayed.
    Byte 5 is the reliable discriminator: radio ACKs are generated only as
    `0x80 | max_hop` and are only ever decremented on the relay path. Measured
    over 32.7 hours and 8741 frames on three field nodes, three independent
    criteria separate the populations identically, and **not one** of the 506
    implausible frames acknowledged a message the node had actually heard. The
    check lives as a pure function in `ack_functions.h` so it is testable
    without hardware, and a rejected frame is logged as `ACK_REJECT` rather
    than dropped silently, so the filter stays measurable in the field.
92. **`{SET}` range-checks max_hop.** `sscanf` wrote both values straight into
    the settings. A typo such as `{SET}44;2;` put 44 into the hop field of every
    packet the node sent — and the relay path only checks `(byte5 & 0x7F) > 0`
    before decrementing, so nothing bounded it from above. Values outside
    0…`MAX_HOP_LIMIT` now leave the previous setting standing. The old leniency
    is intact: each field is still applied as soon as `sscanf` has read it, so
    `{SET}4;` still sets only `max_hop_text`.
93. **New: raw frame capture at runtime (`--txcapture`).** Until now the log
    showed frames only **decoded** — the output of our own parser. A frame the
    decoder reads wrongly appears wrongly in the log, and nothing in it reveals
    what was actually on the channel. `captureFrame()` now copies raw frames
    into a 768-byte ring and `captureDrain()` prints them from the loop.
    Receive follows `--loradebug`; transmit has its own new switch,
    `--txcapture on/off`, which is persisted. The ring is not incidental:
    dumping directly from the radio callback needs about 900 B of stack — the
    nRF52 timer task has 1 KB — and would put roughly 48 ms of serial time into
    the RX path, or, on the transmit side, sit between the CAD "channel free"
    decision and `startTransmit()` and invalidate the very measurement the send
    timing rests on. Decoupled through the ring, capture costs one `memcpy`.
    Dropped frames are reported as `[MC-TEST] CAPTURE_DROPPED n= serial_bytes=`,
    because a capture goes patchy exactly when the channel is busy — the
    situation you switched it on for. Cost on RAK4631: +1400 B RAM,
    +1616 B flash.
94. **ESP32 channel utilisation was overstated by a factor of 1.8 to 4.1
    (N-29).** Found by the new capture on its first run on real hardware.
    `checkRX()` passed `UDP_TX_BUF_SIZE` (255) to `radio.readData()` as the
    length — but RadioLib takes that by value and never writes the real length
    back; it is an upper bound only, and the SX126x header says plainly that
    `getPacketLength()` must be called first. Every receive therefore reported
    255 bytes, with uninitialised stack content behind the actual frame, and
    `checkRX()` booked `getTimeOnAir(255)` into the channel-load statistic. The
    log showed exactly `rx=2476ms` after every single receive — the airtime of
    a 255-byte packet at SF11/BW250/CR4:6. The real frames ran 608 ms (48 B) to
    1394 ms (133 B). The counter-check sits in the same log line: `tx=701ms`
    matches the 60-byte transmit frame to the byte, because the transmit path
    knows its own length. A reported `util=18%` was really about 7%. Every
    ESP32 utilisation figure this firmware has ever printed was too high, and
    none of them were ever comparable with nRF52 figures, where the radio
    callback supplies the length. The length is now read from the chip before
    `readData()`, capped at `UDP_TX_BUF_SIZE`, and a zero-length read is no
    longer treated as a frame.
95. **`%%` was doubled in every log line containing a percent sign (N-30).**
    The `%%` branch wrote two percent signs and then fell through into the
    general copy, which appended the same one again; the next pass appended the
    second one once more. `util=18%%` and `BATT 100 %%` in the field were this.
    It broke `loganalyse.sh` and `logharvest.py` along with the logs. The
    rewrite logic now lives in `src/printfdeb_format.h` so it can be tested
    without Arduino — the same separation as `isPlausibleAckFrame()` — and a
    leading `;` no longer reads one byte before the buffer.
96. **`--info` printed passwords in clear text to the open network console
    (N-31).** `node_passwd`, `node_webpwd` and `node_pwd` were printed unmasked.
    That output goes through `printfdeb()` and therefore also over TCP port
    2323, which requires no authentication at all unless `node_passwd` is set.
    Reproduced end to end: `nc <node> 2323`, then `--info`, and the WiFi PSK is
    on screen — and in every log capture anyone shares. A set password now
    prints as `***`; empty stays empty, so it remains readable _whether_ one is
    set. The settings JSON sent to the app is untouched, since it needs the
    real value to display and change it. This does not replace `--passwd`; it
    takes the most rewarding find away from an open port.
97. **The shipping code is now replayed against real field traces.**
    `tools/traceharvest.py` harvests the decision sequence of running nodes from
    their debug output — 48 node-hours across four field stations — and feeds it
    to the actual functions rather than a model of them: `is_new_packet()` and
    `addLoraRxBuffer()` against 5647 verdicts and 6869 slot assignments,
    `getMessagePriority()` against 505 classifications covering all five
    classes, and `isPlausibleAckFrame()` against 30 ACKs the field nodes
    actually honoured. Zero deviations in all three. The ACK suite answers the
    question a retrofitted filter always raises — does it cut into healthy
    traffic? — by replaying frames whose _effect_ is logged, each of which
    closed a waiting ring slot. It does not cut. All three suites are
    mutation-checked, so it is demonstrated that they test anything at all.
98. **The dedup ring stays at 100 — measured, not assumed.** An analysis report
    recommended raising it to 300. The counter-check over the same 48 node-hours
    counted every message id that was evicted and then re-flooded: 112
    returners, of which exactly **one** was a genuine duplicate. 96 were id
    reuse — a different message carrying the same id, clustering at 180–210
    minutes apart. A larger ring buys that one frame; from 500 upwards it starts
    discarding legitimate messages (52 of them at 500, 96 at 1000). The
    diagnosis in the report was right — the window is about 38 minutes against a
    longest observed packet lifetime of 36.7 minutes, so there is no margin —
    but almost nothing falls through it, and 1 kB of RAM for one event in 48
    node-hours is not a trade. The measurement is written down where the
    constant is defined, and the replay test makes any change to the number fail
    loudly.
99. **An interop oracle and a fuzz corpus, both built from real traffic.**
    `tools/logharvest.py` harvested 8965 distinct CRC-rejected frames, 2981 ACK
    frames and 42110 re-encode vectors out of 2.83 million log lines from
    production nodes. `test_aprs_reencode` rebuilds frames from the logged
    fields and compares length and byte sum against the values computed by the
    **sender** — not circular, because the decoder reads that checksum out of
    the frame and rejects the frame if it does not match the wire bytes.
    `encodeAPRS()` reproduces the byte sum of 2422 distinct real frames without
    a single exception, across eleven hardware ids and two firmware
    generations. The one length deviation comes from a sender that omits the
    0x7E end marker; it is documented behaviour and frozen as the expected
    number. `test_aprs_fuzz` puts 500 genuinely corrupted frames through the
    decoder under AddressSanitizer and UndefinedBehaviorSanitizer, plus a fill
    differential that catches any read past the end of a frame.
100.  **`tools/meshlogger.py` records a node's network console to disk** for days
      at a time, so a rare event can be caught without sitting at a terminal. The
      console serves one client at a time, so the tool honours a `PAUSE` file and
      hands the port over on request.
101.  **`tools/loganalyse.sh` was itself audited and fixed (TOOL-01…06).** It
      counted CSMA backoff as a state-machine error, mis-attributed drop reasons
      to the wrong bucket, took the hop count from the wrong field, choked on
      stray bytes in its input, and could not read raw firmware logs without hand
      editing. It now reads them directly, and a regression suite covers the
      three counting bugs. A verdict from a broken instrument is worth nothing;
      several conclusions in this release rest on this tool.

## New in v4.35p.08.22-stability

This release exists to correct a defect we introduced ourselves in 08.21, plus
two sensor fixes and a build fix found while testing it. Everything below was
verified on real hardware for this release, not carried over.

81. **The GPS boot loop is fixed (N-25).** Arming the ESP32 task watchdog at
    the very start of `esp32setup()` — our own change in 08.21 — exposed a
    ~16-second block that upstream has always had but never watched: the GPS
    baud-rate scan, which runs from the main loop and never feeds the
    watchdog. Any node with `--gps on` aborted two or three baud steps in and
    rebooted, forever, because the setting is persisted before the crash.
    There was no command window, so the node could not be reached over the
    air, by BLE, or over the network console — only a USB reflash recovered
    it. Reproduced on a Heltec V3 and confirmed fixed there, with the crash
    backtrace decoded against its own ELF. Four parts: the watchdog is armed
    at the **end** of setup rather than the start (which also removes the
    USB-CDC boot wait, the display timeouts and eleven `while(true);` error
    paths from the watched region); the GPS init path feeds the watchdog
    behind a single helper that compiles to nothing on nRF52; the scan stops
    at the first NMEA sentence with a **valid checksum** instead of running
    all eight baud rates; and the baud table is ordered by likelihood.
82. **The baud scan no longer invents a baud rate (B-15).** It used to pick
    whichever rate produced the most characters, with no minimum — and the GPS
    RX pin is configured as an input with no pull-up. A single noise byte in
    the matched character set could therefore "detect" a rate on a board with
    no module attached, after which the probe ran against nothing. Detection
    now requires a complete, checksum-valid sentence; if none arrives, the
    node says so. Verified on a second Heltec V3 with no GPS fitted.
83. **GPS detection is dramatically faster.** Stopping at the first valid
    sentence, and trying 38400 before 9600 because `SetupUBLOX()` switches
    modules to 38400 permanently on first init, took detection from
    **12 000 ms to 321 ms** on a Heltec V3 and as low as 24 ms on a T-Beam.
84. **The dead second baud-detection implementation was removed (A-1…A-4).**
    `gps_functions.cpp` carried an unreachable interrupt-based variant, which
    is how the two implementations had silently drifted apart — it contained
    two `return -1` statements from a function returning `unsigned long`
    (wrapping to 4294967295, a value the caller's `> 0` test accepts) and a
    regression of our own. The reason it was unreachable turned out to be a
    single unconditional `#define` overriding a per-variant option in 15
    board configurations. Flash size is byte-identical before and after,
    which is the proof that only dead code went.
85. **The BME680 driver no longer mistakes an I2C acknowledgement for a chip
    ID (N-27).** BME280, BMP280 and BME680 share addresses 0x76 and 0x77, and
    only `begin()` reads the chip ID — but its return value was discarded and
    the sensor was marked present based on the address alone. A board with a
    BME280 reported `BME680: on (found)` and then logged a read failure on
    every cycle, forever. Verified both ways: on a board with a BME280 at
    0x76, and on one with nothing at either address.
86. **`--bmx off` now turns off the BME680 as its help text always
    promised (N-28).** It cleared BMP280, BME280 and BMP390 but never the
    BME680, so following the help and then enabling a BMx280 produced
    "can't be used together" and left the node with no sensor at all.
87. **Flashing targets the board you asked for.** The per-board upload
    commands carried no `--port`, so `esptool` chose one itself; with two
    boards attached, `pio run --target upload` could flash or disturb the
    wrong one. `--upload-port` had no effect because PlatformIO only forwards
    it through `$UPLOAD_PORT`, which was absent. All 27 commands now pass it.

## Input handling — RF, BLE, and network paths

A mesh node parses input from the air, from Bluetooth, and from the local
network. These changes make sure malformed or unexpected input in any of those
paths is length-checked and handled safely at every stage:

1. Received payload bytes are no longer interpreted as a printf format string
   in the debug logger (SEC-02).
2. The BLE WiFi-configuration message (0x55) now validates its length fields
   before copying SSID and password (SEC-03).
3. The URL-decode loop for messages can no longer write past its 200-byte
   buffer (SEC-04).
4. UDP receive: an off-by-one write at the buffer end was corrected (SEC-05).
5. UDP receive: the zero-byte scan can no longer read past the received length
   (SEC-06, BUG-12).
6. The BLE text-message handler (0xA0) rejects length underflows that produced
   oversized messages (BUG-07).
7. `handleACK` verifies a minimum frame length before its 12-byte copy
   (BUG-10).
8. APRS trailer and FCS fields are only read after checking they are actually
   inside the received frame (BUG-13).
9. The CONF configuration handler no longer zero-fills up to 251 bytes past a
   stack buffer (N-03).
10. A length underflow in the RF receive path could turn one short frame into
    an oversized `memcpy`; the whole chain is now length-checked (N-04,
    BUG-08), including the zero-length edge case in the phone-forwarding path.
11. An mheard path-update routine could read past a heap buffer; it is now
    bounds-checked (N-05).
12. A web-interface parameter was used as an unbounded array index (N-06).
13. The UDP transmit decode used the total frame length instead of the APRS
    payload length, over-reading its conversion buffer at two call sites.
14. Debug logs now mask passwords, and a legacy plaintext-comparison fallback
    in the authentication path was removed.
15. Oversized APRS frames are rejected at a single maximum-frame check before
    any parsing begins.
16. The `--symid` command validates the APRS symbol table character again.

## Crash and freeze fixes

These were the "node stopped responding" and "node rebooted" class of
findings, each reproduced (where hardware-dependent) on a real device before
and after the fix:

17. A stack overflow in the loop task on the message path could crash nRF52
    gateways; the buffer now lives off-stack (N-22).
18. `sendExtern()` allocated two 500-byte JSON buffers on a stack that is only
    4 kB on nRF52; they are static there now (ESP32 keeps its stack
    allocation), ending crashes on position sends.
19. On gateway-configured nodes without an Ethernet link, the W5100S UDP send
    and receive paths could stall the main loop for seconds to minutes — long
    enough to look like a dead node. Those paths now check the link state
    first, and a soak test with fault injection (link loss) verified the loop
    keeps running; further hardening of the remaining wait loops is tracked in
    the catalogue (N-20).
20. Enabling `--extudp` without an initialized network left the node frozen
    on every boot, because the setting persists across reflashes. EXTUDP now
    starts only once the node actually has an IP address (N-23).
21. `startNetwork()` could block longer than the task watchdog's first feed
    interval, producing a boot loop on gateway nodes (N-17).
22. Per-log-line heap allocations could starve the BLE stack while a phone
    connection was being established; logging on that hot path now avoids
    them, and connections are reliable again (N-18).
23. A 10-byte buffer was too small for the `{mcp}` message reformatting and
    corrupted adjacent memory (BUG-11).
24. If radio initialization fails at boot, the node now logs the cause and
    reboots instead of halting silently — a stuck node becomes a
    self-recovering one (A2).
25. The nRF52 `--dfu` command no longer hangs on its way into the UF2
    bootloader; it uses the GPREGRET register as the SoftDevice documentation
    intends (N-19).
26. A priority eviction in the TX ring could orphan an occupied slot,
    permanently losing one of the transmit slots (N-24) — found by the new
    TX-ring test suite, not in the field.
27. Clearing a TX-ring slot now clears the whole slot, closing a gap where
    stale bytes could survive into the next use.

## Concurrency correctness

The firmware runs radio, Bluetooth, and the main loop on more than one task on
nRF52. This group makes shared data crossings explicit and safe — and, just as
deliberately, removes synchronization where analysis showed none is needed:

28. The external-UDP queue is now a correct single-producer/single-consumer
    ring with the right memory ordering.
29. Phone commands received over BLE are queued to the main loop instead of
    executing inside the BLE callback (CONC-14).
30. The BLE outbound ring is written under a short lock, and the sender takes
    a consistent snapshot instead of re-reading a live slot (CONC-15,
    CONC-18).
31. The UDP outbound ring received the same treatment on both writer and
    reader side, verified live on a RAK4631 gateway with Ethernet (CONC-16).
32. BLE-received settings are staged in a private buffer and applied once per
    loop pass, instead of being copied live into the active configuration
    (CONC-17).
33. The network-console mutex could be re-created while held; ownership is now
    respected (CONC-19).
34. Enqueueing into the nRF52 TX ring is now a single operation under one
    lock — all sixteen call sites go through it, so two senders can no longer
    interleave in the same slot (N-14).
35. `Radio.Send()` is no longer called inside a FreeRTOS critical section
    (N-16).
36. The LoRa scan and CAD completion flags shared between ISR and loop are
    atomic (B2, B3); where ring buffers genuinely cross tasks, the locking
    work above covers them.
37. Where analysis proved single-task access on ESP32, unnecessary atomics and
    locks were removed — fourteen candidates plus the ring indices (N-13).

## Timing robustness

38. `millis()` comparisons throughout the codebase were converted to the
    subtraction idiom that survives the 49-day counter wraparound — in the
    ESP32 main loop, the nRF52 main loop, the LoRa/ring/GPS paths, and every
    deadline check found (N-08, A1).
39. A GPS ISR off-by-one was corrected (B5).
40. The ESP32 task watchdog is enabled and fed properly (C3); together with
    the `startNetwork()` fix above, a future hang recovers by reboot instead
    of by power cycle.
41. `AT_PRINTF` output is assembled with bounded `snprintf` (D3).

## Performance and resource use

42. NimBLE is configured server-only, saving 792 bytes of DRAM and 7.9 kB of
    flash on ESP32.
43. `read_batt()` no longer stops the main loop for ~100 ms every half second
    (BATT-01).
44. Debug output on nRF52 no longer blocks when the USB serial FIFO is full —
    a bounded wait, then the line is dropped, and the node keeps meshing
    (part of the N-20 hardening).
45. Interrupt-context LoRa logging is compiled out unless explicitly enabled
    (B1), and ring-overflow events are logged at the right severity (C2).

## Correctness and consistency

Smaller findings where two copies of the same logic had drifted apart:

46. The two HDOP variables are merged; display and web interface previously
    read different values (SIMP-30).
47. The I2C bus-reset guard is defined centrally; one board variant was
    missing it at exactly the two call sites that address the sensor
    (DRY-25).
48. "Node is unconfigured" is defined once and used by all three images
    (ALT-34).
49. Two byte-identical conditional branches were merged (ALT-33), and the
    display-refresh flag was separated from the button flag it had been
    overloaded with (ALT-35).
50. The nRF52 ACK handling now sets the same acknowledgement level for own
    messages as the ESP32 code path — the two copies had drifted (DRY-21).
51. The nRF52 serial command parser received the NUL-byte protection and
    self-healing check its ESP32 twin already had (DRY-22).
52. Gateways now deliver the HEY signal-quality report to the server over UDP
    as well, so coverage reporting works the same on both transport paths.
53. Board identity macros test for definedness instead of doing arithmetic on
    product names, which silently matched the wrong boards (N-10).
54. The nRF52 ADC and battery guards key on the actual platform instead of a
    "not RAK4630" exclusion, unblocking the other nRF52 boards.
55. The node's IP address is logged in dotted-decimal form instead of as a
    raw integer.
56. Duplicate source files and an editor artifact were removed from the
    compiled tree (SIMP-29).

## Build system

57. Continuous integration builds all board environments on every push and
    pull request — a change that breaks any board is caught before it lands
    (TEST-38).
58. `-Wall -Wextra -Werror` (with `-Wformat=2`) is enforced for the project's
    own sources on all 23 mainline ESP32 environments and on the RAK4631; the
    warnings this surfaced were fixed.
59. The nRF52 platform package is pinned to a known-good version, so builds
    are reproducible.
60. Three identically-named `[nrf52]` configuration sections silently
    overrode each other across board variants; each variant now has its own
    explicit base section (CFG-01).
61. The safeboot image builds reliably after mainline builds — the two
    platforms no longer fight over a shared framework directory — and the CI
    cache keys were tightened so stale artifacts cannot leak between jobs.
62. `--flash-reset` actually resets the persisted configuration now, and a
    size check protects against silent settings-layout drift (N-12; the
    catalogue tracks the remaining part of this finding — unifying the two
    settings layouts — as open, deliberately left for an upstream-coordinated
    change).

## Test infrastructure

The largest single investment of this effort: an automated safety net for the
firmware's core logic, so improvements — ours and anyone else's — stay
verified:

63. A native (host-side) test environment with the Unity framework runs the
    firmware's parsing code on a development machine, no hardware needed
    (TEST-37), and runs in the same CI gate as the builds.
64. A frame-capture hook records real LoRa frames from the air into a test
    corpus (oracle stage 1).
65. A differential runner replays that corpus of real captured frames through
    the native decoder and flags any change in behavior (oracle stage 2).
66. Specification-derived test vectors from the wire-format reference document
    check the decoder against what the format says, not just against what the
    code did yesterday (oracle stage 3).
67. The TX-ring core was made natively testable and covered with eleven unit
    tests — which promptly found N-24 above.
68. A mock MeshCom server provides a test double for the node↔server protocol
    (port 1990), so gateway logic can be exercised without a live server.
69. A resource watcher tracks RAM and flash usage per board against a baseline
    and reports the delta in CI, so size regressions are visible in every
    pull request.
70. A soak-test harness ran the RAK4631 gateway for hours under fault
    injection (Ethernet link loss) with a heartbeat watch — the N-20 fix is
    verified by endurance, not just by review.
71. The test suite itself was audited: eight weaknesses in the tests were
    fixed and the regression fence hardened, with three follow-up points
    closed in a second pass.

## Diagnostics and tooling

The developer toolbox in `tools/` that grew alongside this work:

72. `--dfu` reboots an nRF52 node into the UF2 bootloader on command — nodes
    in enclosures can be flashed without reaching the reset button.
73. The nRF52 logs its reset reason (RESETREAS) at boot, so an unexpected
    reboot tells you why.
74. `tools/loganalyse.sh` grew into a full log-analysis suite — 35 sections
    covering heap trends, CRC forensics, CSMA timing, and BLE-to-LoRa
    latency.
75. `tools/serial_monitor.py` watches a node's debug stream, tracks the LoRa
    state machine, and raises alerts with periodic summaries.
76. `tools/webflash.py` flashes a node over WiFi through the built-in OTA
    mechanism, end to end, with MD5 verification.
77. `tools/hmac_connect.py` connects to the network debug console with
    challenge-response authentication.

## Documentation

78. A complete architecture documentation set was written for this codebase —
    system overview, build matrix, dependencies, concurrency map, buffer
    inventory, and test strategy (`docs/architecture/`).
79. A byte-level wire-format reference documents the LoRa frame, the server
    UDP protocol, the EXTUDP JSON sideband, and the BLE phone protocol as
    actually implemented, with code anchors and captured example frames
    (doc 11).
80. Every finding above is written up in the engineering logs with evidence,
    status, and verification notes — including the claims we investigated and
    **refuted**, so nobody re-chases them (doc 08).

## Thank you

To the MeshCom maintainers and the ICSSW team: this project is a gift to the
amateur radio community, and all of this work happened because we think it is
worth polishing. We hope every one of these changes finds its way home.
