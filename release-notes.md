> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**Three field-driven subsystems on top of `v4.35p.09.01.2-stability`**, each from its own review-gated worktree and merged tonight: the **GPS altitude and QNH path** (the UART is drained on every loop pass instead of every 3 s, implausible fixes are rejected before they reach settings or the clock, the beaconed altitude is a Kalman estimate instead of one raw sample, and the barometric QNH reference re-latches when that estimate has converged); the **`--setlog on` instrumentation** (seven new log line kinds that make signal level, relay decisions, dedup, TX wait, errors and gateway inject/upload traceable per message id, so a mountain gateway's night can be read offline); and **T-Deck key auto-repeat** (Backspace, Space and the alpha keys repeat while held, on keyboards whose controller firmware has raw mode). Plus the Code Quality 2.0 pattern catalogue with detectors, the ESP32 reset-cause banner, and the upstream `dev` sync. Items 170–176 in the changelog; 568 native test cases across 12 host environments.

**What the field said the same evening**: two nodes at OE5HWN ran the GPS build for an hour each — 2375 evaluations, not one corrupt sample (the old build produced a spliced sentence about once per 256), convergence after 88 samples as modelled, QNH re-latched after a reboot. The same logs exposed a fourth GPS defect on the **T-Deck without Plus** (a 4.35p pin swap that muted self-wired GPS modules, item 174/GPS-06), fixed in this release with a scan fallback; and one T-Deck Plus whose keyboard controller predates raw mode, which degrades to one character per press exactly as designed and now says so in `--info`.

Flash version `20260902`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

### GPS: altitude, QNH and the T-Deck pins (item 174)

- **Nothing is lost between evaluations.** `WZ_GPS_Feed()` drains the GPS UART on every loop pass; the 256-byte ring used to overflow by ~165 bytes per 3-second cycle, and about one in 256 of the resulting spliced sentences passed the checksum as a "fix" with `lon:0.000000` or `Date: 2015.14.00`.
- **A plausibility gate** rejects null island, out-of-range angles and altitudes, impossible calendar dates and clock times before a sample can reach persisted settings, the position beacon or the system clock. Rejects are counted and logged (`--gpsdebug on`).
- **The beaconed altitude is an estimate, not a sample**: a scalar Kalman filter with time-scaled process noise (time constant about 7 minutes) replaces the raw value on a stationary node; TRACK mode bypasses it. `--setalt` seeds the filter and rejects out-of-range values instead of clamping to 0 m.
- **QNH re-latches on convergence** and on `--setalt`/`--setpress`, not on whichever fix happened first after boot. A node that never fixes latches its persisted altitude as before.
- **T-Deck without Plus, self-wired GPS module**: since upstream v4.35p the board receives on GPIO44 and sends on GPIO43 (the LilyGo and T-Deck Plus assignment); 4.35d did the reverse on this board, so modules wired for 4.35d went silent. The baud scan now retries once on the old pins and logs a request to swap the wires. Cost: up to 12 s at boot on a T-Deck with `--gps on` and no module.

### `--setlog on`: seven line kinds for offline analysis (item 173)

The receive line gains `RSSI:`/`SNR:`/`DUP:`/`OWN:`/`t=`; new lines `RLY` (relay decision with reason), `TX` (own send with wait time, queue depth, CAD attempts), `ERR` (RX error, both platforms), `STAT` (five-minute channel utilisation, dedup, ring high-water, drops, heap) and `GWI`/`GWU` (gateway inject and upload per `msg_id`). All hang off `--setlog on` only; under 64 bytes of RAM, no new buffers. `tools/berglog.py` parses them.

### T-Deck key auto-repeat (item 175)

Holding Backspace, Space or an alpha key repeats it (400 ms delay, then every 100 ms) through LVGL's keypad-repeat mechanism, using the keyboard controller's raw-mode live-matrix window for the duration of the hold. Arms only when the raw frame shows the exact matrix cell of the pressed key. Controllers without raw mode (LilyGo firmware before 2025-06-12) keep today's one-character-per-press behaviour; `--info` prints `...KBD raw-mode yes|no|unknown` so a tester can tell which case a unit is.

## Changelog and engineering rationale

- **[MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.09.02-stability/docs/CHANGELOG-stability.md)** — the numbered list, items 170–176 for this release.
- **[GPS PR draft](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.09.02-stability/docs/pr-gps-draft-20260902.md)** and **[T-Deck key-repeat PR draft](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.09.02-stability/docs/pr-tdeck-keyrepeat-draft-20260902.md)** — file references, measurements and reasoning in the structure the upstream submissions will use (German). The `--setlog` design is in [`docs/setlog-instrumentation-impl-plan-20260902.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.09.02-stability/docs/setlog-instrumentation-impl-plan-20260902.md).
- **[Engineering write-up of the previous campaign](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.09.02-stability/docs/pr-draft-20260831.md)** — items 107–169 (back-pressure, WiFi, NTP, OTA), unchanged in this release.
- [MeshCom@ICSSW project page](https://icssw.org/en/meshcom/)

## Built for the field: debug logs from any node

This build deliberately ships the **full serial instrumentation** (`INSTRUMENT_ENABLED=1`, `MC_INJECT_HOOKS=1`), so a node in the field can produce machine-readable debug logs that we can analyze offline afterwards — that is how every finding in this release was measured, and it works the same on your kitchen-shelf gateway:

- Markers are compact `[TAG];key;value` lines: `[WIFI]`, `[ETH]`, `[GW]`, `[UDP]`, `[NTP]`, `[INSTR-LOOP]`, `[SPITRACE]`, `[KBD]` and more. No marker floods: high-rate candidates are rate-limited or bound to their own switch.
- Turn on what you need: `--setlog on` (the per-message line set of this release), `--gpsdebug on` (fix/position/reject/convergence every 3 s), `--debug on` (verbose), `--udplog on` (gateway UDP in/out), `--loradebug on` (RX/TX, dedup, TX-ring), `--wifistat`, `--ethstat`, `--udpstat`, `--instr`, `--heap`.
- Capture the USB serial console — or the **network console on TCP port 2323** (ESP32 boards), which serves the same log over WiFi so the node can stay in place: `nc <node-ip> 2323`, or long-term with `tools/meshlogger.py` from this repository. Note for ESP32-S3 boards on native USB (T-Deck, T-Beam Supreme): output written before the port was opened is dropped after the first ~256 bytes, so a log that appears to start at boot may not — open the terminal first, then reboot.
- Reproduce without a second station: `--injectmsg`/`--injectpos` push messages into the display pipeline, `--injectraw <hex>` runs a raw frame through the **real** receive path (decode, dedup, mheard, relay), `--loratx <n> <ms>` generates bounded TX bursts. T-Deck additionally: key, trackball and touch injection, `--disptest`, `--spitrace`.
- Everything is off by default at runtime and can be compiled out entirely. (One exception: the RAM-tightest board, `E22_XML-DevKitC`, ships without the frame-capture ring and the injection hooks — its classic-ESP32 RAM segment cannot fit them. All markers and log switches are still there.)

If you capture a log of misbehavior in the field, open an issue with the log attached — the markers are designed to be evaluated.

## What changes on the air

Each on-air-visible change, stated plainly (details in the changelog):

1. Message texts are filtered on RX and TX: control characters, invalid UTF-8 and bidi/zero-width characters are removed. Umlauts and emoji pass unchanged.
2. Frames from unconfigured nodes (`XX0XXX…`) are discarded, and an unconfigured node does not transmit at all.
3. Shot-path beacons (`--sendpos`, button, EXTUDP injection, `--sendhey`) have a 30-second floor. Periodic beacons are unchanged.
4. `/B=` is omitted when no battery is detected — instead of a percentage invented from ADC noise.
5. The `/N` neighbour-count tag is correct even without a valid wall clock.
6. HEY link chains are bounded on ingress (`HEY_PATH_PAYLOAD_MAX`).
7. Back-pressure notices and receipts (QRS/QRT/QTA/QRV) go **only** to the transport the message came from — never over the air.
8. `--maxhop 1..6` makes the text hop limit settable and persistent.
9. Server CONF frames (callsign/shortname assignment) are understood on ESP32 and actually applied on both platforms, with source-IP guard and validation.
10. **A gateway no longer self-uploads its own HEY beacon to the server.** The bare, report-less copy always arrived seconds before the neighbours' enriched copies of the same msg_id and could win over them server-side — the reason a gateway's neighbour data vanished from the server while `--gateway off` showed it. Measured against the server's interlink stream; with the fix, only enriched copies arrive, in both gateway states.
11. **Queued HEY frames older than 3 minutes are dropped instead of transmitted**: a neighbourhood report that could not get on air for that long has been superseded by fresher copies anyway. Text, position and ACK traffic never ages out. (For log readers: `RING_STATUS queued=` now reports really-occupied slots; the old index-distance value moved to the new `dist=` field.)
12. **A message the TX queue drops never reaches the backbone**: a gateway uploads a locally typed message to the central server only when the queue accepted it for RF transmission.
13. **Heltec Wireless Stick V3 positions carry `/B=` again**: with the corrected ADC multiplier (4.13 instead of 4.9245, upstream #1119) the battery reading is back inside the plausibility band, so the node no longer reports "no battery" and omits the tag. This release adds upstream #1124, which stops the divider probe from misreading the board's permanently connected divider as "no divider".
14. **Position beacons of a stationary GPS node carry the filtered altitude**, and a spliced or implausible fix (null island, impossible date, altitude outside −500…10000 m) never becomes a beacon or a clock set. In TRACK mode the raw altitude is beaconed as before.
15. **WX telemetry QNH on nodes with GPS and a pressure sensor** is computed against the converged altitude estimate (and against `--setalt`/`--setpress`), not against the first fix after boot. Expect a one-time QNH step of the size of your first-fix error, and the QNH to appear only after convergence (about 4–5 minutes after the first fix; QFE is shown as QNH until then).

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean**; 568 native test cases green across 12 host environments; 102 tool tests green.
- **The merged tree itself has only run through the gates.** Every hardware result below is from the pre-merge test build of the GPS and key-repeat branches (`test-helmut-gps-kbd-20260902`, `df9d407e`); the delta to this release is the `--setlog` line set, the T-Deck pin fallback, the `--info` verdict line and upstream #1124.
- **T-Deck Plus (DK5EN-14, bench)** — GPS run of 6.5 minutes: 128 fixes, 0 rejects, 0 corrupt samples, altitude converged after 256 s (model: 249 s); keyboard raw mode confirmed (`support;1`), hold windows of 0.8–1.2 s for Backspace, Space and `d`, repeated deletion seen on screen.
- **T-Deck Plus and T-Beam Supreme (OE5HWN, field, one hour each)** — 1214 and 1161 evaluations, 0 rejects, 0 corrupt samples; Supreme converged after 88 samples at 280 m, re-latched at 276 m after a reboot; QNH 1020.2 hPa against Linz airport 1018.5 hPa, the residual consistent with a BMP280 offset, not an altitude error. The OE5HWN T-Deck Plus keyboard controller has no raw mode: it typed one character per press, as designed.
- **Heltec V3, T-Beam v1.2, RAK4631** — builds only in this cycle; the `--setlog` bench (RAK4631 + Heltec V3, 30 minutes) is still open. Their last bench time was the `v4.35p.09.01` back-pressure campaign.

### Built and shipped, not on our bench

These boards build cleanly from the same source and inherit every improvement, but had no bench time here:

- T-Deck without Plus (the pin fallback build went to OE5HWN's self-wired unit; the result was not in before this cut)
- E22-DevKitC, E22_1262-DevKitC, E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, E22_XML-DevKitC
- esp32-loraprs-e22, esp32-loraprs-ra01
- heltec_wifi_lora_32_V2, heltec_wifi_lora_32_V4, heltec_wireless_stick (both ADC fixes are OE3LCR's reference measurements, not ours), heltec_wireless_tracker, wireless-paper
- vision-master-e213, vision-master-e290
- ttgo-lora32-v21, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, T-Beam-1W
- T3_S3_V13, t_connect_pro, t_deck_pro, T-ETH-ELITE_1262
- heltec_t114, t_echo (the nRF52 GPS path evaluates at 1 s; the filter's time-scaled process noise covers it, no nRF52 GPS node was on the bench)

(The T5 e-paper variant is not included: it does not build from the current tree for a pre-existing include-path reason unrelated to these changes.)

### Known gaps, stated plainly

- **The `--setlog` line set has no hardware run yet.** Formatters and counters are pinned by 23 native cases and the log parser by 39 tool tests; the 30-minute two-node bench from the plan has not been executed.
- **The GPS two-hour comparison arms (A/B/C) have not been run.** The proof so far is 6.5 minutes on the bench and two one-hour field logs; the claim "no more spliced samples" rests on 2500 evaluations, not on the planned 2-hour arms.
- **The T-Deck pin fallback is unverified on hardware.** It builds and the scan logic is unchanged apart from the pin variables; the one unit that needs it is in the field.
- **Key auto-repeat needs a keyboard controller with raw mode** (LilyGo controller firmware from 2025-06-12 on). Older controllers cannot be updated from the main firmware; `--info` shows `KBD raw-mode unknown` after typing on such a unit.
- **`--wx` still prints `ALT asl: 0 m`** on nodes without a base pressure: pre-existing, untouched. There is no pressure-offset setting comparable to the temperature offset; a BMP280 that reads 1–2 hPa high shows in the QNH.
- **The T-Deck ships with partial refresh (`full_refresh = 0`) plus the flush-fix NOP mitigation** for the shared-SPI-bus lost-flush defect. The clobbered register is identified (`GPSPI2.clock`, via `--spitrace`); replacing the NOP with a targeted re-arm is a follow-up. Map panning works but is step-wise without a tile cache (0.33–0.79 s per step).
- **CONF coordinates** from the server are parsed and logged, not applied.
- The `blelen + 2` length computation in `sendToPhone()` can still wrap on ESP32 for a JSON payload past 253 bytes; no current builder produces one, but the underlying arithmetic is unfixed. The BLE `I` register with six group-call slots filled is still over the frame limit, and the negotiated ATT MTU is never read. All reported upstream.
- The battery zero point on a real 2S pack and the INA226 branch remain unverified. The L76K GPS branch (T-Beam Supreme) has now seen an hour of field data on this release's GPS code.
- **TM-49 (open):** the safeboot OTA completion handler can read a success status after a disconnect whose final frame never arrived, and switch boot partitions after a partial write — benign on 16-MB boards (slot validation catches it), risky on 4-MB single-slot boards. Until the guard lands: on 4-MB boards prefer USB flashing over OTA when the link is marginal.
- The back-pressure system is proven native and the receipt build ran on all four bench boards plus the live gateway in the previous release; the three-own-messages rule for QRS (item 167) still has no hardware burst against it.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.09.02-stability/README.md#flashing-firmware) (`bootloader.bin` for classic ESP32, `bootloader-s3.bin` for ESP32-S3).
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`
- **T-Deck without Plus with your own GPS module:** wire the module's TX to GPIO44 and its RX to GPIO43 (the T-Deck Plus assignment). A module wired the 4.35d way still works through the fallback, at the cost of 12 s at boot, and the log asks you to swap.

## Upstream

Everything here is written up for upstream: the PR drafts linked above carry the submission text. Items 1–103 of the changelog are already in official MeshCom (PRs [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102), [#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103)). The MeshCom project deserves the finding **and** the fix, not a bug report thrown over the wall.
