> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**A stability release on upstream's `4.35s`, with thirteen changes of this fork on top of `v4.35s.09.03-stability`.** Two of them change what a node puts on the air or how it reports itself (a back-pressure echo guard and a RAK4631 TX power correction), one closes a safeboot OTA hole, five are T-Deck fixes with bench proof, and the rest are web GUI, telemetry and tooling corrections. The tag name drops the `-stability` suffix from this release on; the line and its rules are unchanged.

Flash version `20260905`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

If you are coming from `v4.35s.09.03-stability`, everything new is listed under "What changes on the air" and in changelog items 179–191. If you are coming from anything older, read that release's notes too — everything in it is in here.

## Changelog and engineering rationale

- **[MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.05/docs/CHANGELOG-stability.md)** — the numbered list, items 179–191 for this release, 177–178 for the previous one.
- **[Back-pressure echo guard evidence](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.05/docs/node-msg.md)** — the field capture and the reasoning behind item 179 (German).
- **[GPS PR draft](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.05/docs/pr-gps-draft-20260902.md)** and **[T-Deck key-repeat PR draft](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.05/docs/pr-tdeck-keyrepeat-draft-20260902.md)** — the reasoning behind the `v4.35p.09.02` subsystems, in the structure the upstream submissions use (German). The `--setlog` design is in [`docs/setlog-instrumentation-impl-plan-20260902.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.05/docs/setlog-instrumentation-impl-plan-20260902.md).
- **[Engineering write-up of the back-pressure campaign](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.05/docs/pr-draft-20260831.md)** — items 107–169, unchanged in this release.
- [MeshCom@ICSSW project page](https://icssw.org/en/meshcom/)

## Built for the field: debug logs from any node

This build deliberately ships the **full serial instrumentation** (`INSTRUMENT_ENABLED=1`, `MC_INJECT_HOOKS=1`), so a node in the field can produce machine-readable debug logs that we can analyze offline afterwards — that is how every finding in the previous releases was measured, and it works the same on your kitchen-shelf gateway:

- Markers are compact `[TAG];key;value` lines: `[WIFI]`, `[ETH]`, `[GW]`, `[UDP]`, `[NTP]`, `[INSTR-LOOP]`, `[SPITRACE]`, `[KBD]`, `[SAFEBOOT]` and more. No marker floods: high-rate candidates are rate-limited or bound to their own switch.
- Turn on what you need: `--setlog on` (the per-message line set), `--gpsdebug on` (fix/position/reject/convergence every 3 s), `--debug on` (verbose), `--udplog on` (gateway UDP in/out), `--loradebug on` (RX/TX, dedup, TX-ring), `--wifistat`, `--ethstat`, `--udpstat`, `--instr`, `--heap`.
- Capture the USB serial console — or the **network console on TCP port 2323** (ESP32 boards), which serves the same log over WiFi so the node can stay in place: `nc <node-ip> 2323`, or long-term with `tools/meshlogger.py` from this repository. Note for ESP32-S3 boards on native USB (T-Deck, T-Beam Supreme): output written before the port was opened is dropped after the first ~256 bytes, so a log that appears to start at boot may not — open the terminal first, then reboot.
- Reproduce without a second station: `--injectmsg`/`--injectpos` push messages into the display pipeline, `--injectraw <hex>` runs a raw frame through the **real** receive path (decode, dedup, mheard, relay), `--loratx <n> <ms>` generates bounded TX bursts. T-Deck additionally: key, trackball and touch injection, `--disptest`, `--spitrace`.
- Everything is off by default at runtime and can be compiled out entirely. (One exception: the RAM-tightest board, `E22_XML-DevKitC`, ships without the frame-capture ring and the injection hooks — its classic-ESP32 RAM segment cannot fit them. All markers and log switches are still there.)

If you capture a log of misbehavior in the field, open an issue with the log attached — the markers are designed to be evaluated.

## What changes on the air

New in this release:

1. **A node no longer radiates `QRT NOT SENT - QRT NOT SENT - ...`** when an attached client (an app, or a second node bridged over EXTUDP) hands the node's own back-pressure notice or receipt back in as a new message. The node recognises its own wording exactly at the point of submission and drops it silently: no transmission, no further receipt, no stacked prefixes. Field evidence was a stack five prefixes deep, nine frames inside three seconds (item 179).
2. **A RAK4631 transmits at its 22 dBm default again after a flash reset**, and the app shows that value instead of -20 dBm. Since 4.35p the "power not set" marker is -20; the nRF52 boot path never normalised it, so the radio was clamped to 2 dBm until someone set `--txpower` (upstream [#1132](https://github.com/icssw-org/MeshCom-Firmware/issues/1132), item 180).
3. **Safeboot OTA is fail-closed**: a partial upload whose connection died before the last frame can no longer switch the boot partition (item 186). On 16 MB boards the bootloader masked this; on 4 MB single-slot boards it could boot a half-written image.
4. **The Extern-UDP `tele` datagram for relayed nodes carries the station pressure under `qfe`** instead of the `/F=` altitude; the altitude moves to a new key `pressure_alt` (item 187). Dashboards that read `qfe` from relayed BME680 nodes see hectopascal now.
5. **Analog input with an unset GPIO** no longer floods the serial log with `Pin 99 is not ADC pin!`, and `--info` says `GPIO not set, measurement paused` (item 188).

On the T-Deck, with bench proof on DK5EN-14:

6. The trackball button fires one click per press, not two (item 181). Send and Save Setting return to the message tab without the half-killed animation that froze the screen on a split frame (item 182). Picking the map tab composes the SD map once, not twice (item 185).
7. **ESP32-S3 boards on native USB no longer stall the main loop once the USB host is gone** (item 184). The Arduino core's HWCDC keeps a 100 ms TX timeout after the first host read; with the cable pulled, every print that missed the 256-byte ring blocked the loop, and on the T-Deck the cursor and touch froze in the rhythm of the GPS log. The firmware now asks for a zero timeout and a 4 kB ring.

Web GUI and tooling:

8. **The web messages page shows messages while a phone is connected**, keeps them across refreshes in the browser, and filters by group tab (item 190). Before, a connected app drained the ring window the page was reading, so the page was empty exactly when the node was in use.
9. **A LoRa queue panel on the RX-log page** shows the TX ring per priority, the back-pressure state, the dedup window and the last channel-utilisation window (item 189). Flash +7 kB, RAM unchanged.
10. The memory guard in `tools/` checks IRAM as well as DRAM, which is what actually overflowed in upstream CI on the merge of PR #1114 (item 191).

Everything below was already true in `v4.35s.09.03-stability` and is unchanged here (details in the changelog):

11. `--postime <seconds>` sets the periodic position interval; any value of 300 s or more is kept.
12. Message texts are filtered on RX and TX: control characters, invalid UTF-8 and bidi/zero-width characters are removed. Umlauts and emoji pass unchanged.
13. Frames from unconfigured nodes (`XX0XXX…`) are discarded, and an unconfigured node does not transmit at all.
14. Shot-path beacons (`--sendpos`, button, EXTUDP injection, `--sendhey`) have a 30-second floor. Periodic beacons are unchanged.
15. `/B=` is omitted when no battery is detected — instead of a percentage invented from ADC noise.
16. Back-pressure notices and receipts (QRS/QRT/QTA/QRV) go **only** to the transport the message came from — never over the air.
17. `--maxhop 1..6` makes the text hop limit settable and persistent.
18. Server CONF frames (callsign/shortname assignment) are understood on ESP32 and applied on both platforms, with source-IP guard and validation.
19. A gateway no longer self-uploads its own HEY beacon to the server. Queued HEY frames older than 3 minutes are dropped instead of transmitted; text, position and ACK traffic never ages out.
20. Position beacons of a stationary GPS node carry the filtered altitude; a spliced or implausible fix never becomes a beacon or a clock set. WX telemetry QNH is computed against the converged altitude estimate.

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean**; 591 native test cases green across 12 host environments.
- **T-Deck Plus (DK5EN-14, bench)**: trackball click 5 of 5 correct with the fix, 5 of 5 wrong without (item 181); map tab pick one 294×182 rebuild with a 718 ms loop gap instead of 1337 ms (item 185); USB-host-gone loop-gap counters carried across the port-open reset: 7 gaps up to 1.8 s in 44 s without the CDC fix, none attributable to prints with it (item 184); tab switch after Send/Save no longer freezes (item 182).
- **WisBlock RAK4631 (DK5EN-90, bench)**: after `--cleanflash` and reboot the boot log reads `RF_POWER: 22 dBm` and `--info` `TXPWR 22 dBm` where the previous build read 2 dBm; `--txpower 15/10/5/2` read back exactly, `1`, `0` and `-20` refused, a stored 2 dBm survives a reboot (item 180).
- **Heltec V3 (DK5EN-98, bench + running as the operator's gateway since 2026-09-04)**: back-pressure echo guard, four of five bench cases; the ring-flood case is still owed (item 179).
- **T-Beam v1.2** — build only; its last bench time was the `v4.35p.09.01` back-pressure campaign.

### Built and shipped, not on our bench

These boards build cleanly from the same source and inherit every improvement, but had no bench time here:

- T-Deck without Plus, t_deck_pro
- E22-DevKitC, E22_1262-DevKitC, E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, E22_XML-DevKitC
- esp32-loraprs-e22, esp32-loraprs-ra01
- heltec_wifi_lora_32_V2, heltec_wifi_lora_32_V4, heltec_wireless_stick, heltec_wireless_tracker, wireless-paper
- vision-master-e213, vision-master-e290
- ttgo-lora32-v21, ttgo_tbeam, ttgo_tbeam_supreme, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, T-Beam-1W
- T3_S3_V13, t_connect_pro, T-ETH-ELITE_1262
- heltec_t114, t_echo

(The T5 e-paper variant is not included: it does not build from the current tree for a pre-existing include-path reason unrelated to these changes.)

### Known gaps, stated plainly

New with this release:

- **The safeboot fail-closed gate (item 186) has no bench arm on a 4 MB board yet.** The logic is read and unit-reasoned; the disconnect-mid-upload case has not been driven against a single-slot board.
- **The echo guard's ring-flood case (item 179) is still owed** on the T-Deck bench; four of five cases ran on the Heltec V3.
- **The web GUI changes (items 189, 190) were used interactively during development, not driven by a bench scenario.** The ten-minute ring-overflow case from the plan has not been executed as a measured arm. The data getters behind the queue panel are pinned by native cases.
- **TD-15 (filed, not fixed):** after a reboot the T-Deck map shows only stations whose position beacon arrived since boot; the marker arrays are RAM-only and the persisted position table does not restore them. This is the existing design, not a regression.
- **MEM-04 (risk, not a defect):** `ttgo_tbeam`, `ttgo_tbeam_SX1262` and `ttgo_tbeam_SX1268` link with about 20 bytes of IRAM headroom, `E22_XML-DevKitC` with under 1 kB of DRAM. This release builds; any future feature that touches IRAM will not on those four.

Carried over from `v4.35s.09.03-stability`, all still open:

- **`--postime 0` no longer switches position beacons off.** `0` is below the 300-second floor and is clamped up to 300 s.
- **GPS-07 (filed, not fixed):** an overnight capture on 2026-09-03 shows the altitude Kalman filter re-seeding onto raw outliers ten times in one night — the re-seed guard is sized too small. `RESEED_N 60` or a 30 m gate fixes it in replay; neither is in this build.
- **The `--setlog` line set has no hardware run yet.** Formatters and counters are pinned by native cases; the 30-minute two-node bench has not been executed.
- **The GPS two-hour comparison arms (A/B/C) have not been run.**
- **The T-Deck pin fallback is unverified on hardware.**
- **Key auto-repeat needs a keyboard controller with raw mode** (LilyGo controller firmware from 2025-06-12 on). Older controllers cannot be updated from the main firmware.
- **`--wx` still prints `ALT asl: 0 m`** on nodes without a base pressure. There is no pressure-offset setting comparable to the temperature offset.
- **The T-Deck ships with partial refresh plus the flush-fix NOP mitigation** for the shared-SPI-bus lost-flush defect. Map panning is step-wise without a tile cache.
- **CONF coordinates** from the server are parsed and logged, not applied.
- The `blelen + 2` length computation in `sendToPhone()` can still wrap on ESP32 for a JSON payload past 253 bytes; no current builder produces one, but the arithmetic is unfixed.
- The battery zero point on a real 2S pack and the INA226 branch remain unverified.
- The three-own-messages rule for QRS (item 167) still has no hardware burst against it.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.05/README.md#flashing). Use `bootloader-s3.bin` and `safeboot-s3.bin` for ESP32-S3 boards, `bootloader.bin` and `safeboot.bin` for classic ESP32.
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`
- **T-Deck without Plus with your own GPS module:** wire the module's TX to GPIO44 and its RX to GPIO43 (the T-Deck Plus assignment). A module wired the 4.35d way still works through the fallback, at the cost of 12 s at boot.

## Upstream

Everything here is written up for upstream: the PR drafts linked above carry the submission text. Items 1–103 of the changelog are already in official MeshCom (PRs [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102) and [#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103)); the RAK TX power fix (item 180) is meant for the next PR against upstream `dev`, answering issue #1132.
