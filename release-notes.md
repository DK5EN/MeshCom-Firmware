> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**A maintenance release: the fork now sits on upstream's `4.35s`.** The entire firmware delta against `v4.35p.09.02-stability` is upstream's own cut ([#1126](https://github.com/icssw-org/MeshCom-Firmware/pull/1126), commit `c908a4dd`) — two lines of version identity and one `--postime` fix. **Not one line of this delta comes from this fork.** Everything the fork added since the previous release is documentation and tooling metadata.

Two things change for you. **`--postime` works at all now**: the command used to clamp values below 300 s up to 300 s and then throw away every value at or above 300 s (it fell through to the compiled-in default), so no interval could actually be set. Upstream removed the offending `else` branch. And the firmware **identifies as `4.35s`** in the serial banner, `--info`, the web interface and the beaconed version string.

Flash version `20260903`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

If you are coming from `v4.35p.09.02-stability`, this update buys you the `--postime` fix and a version string that matches the upstream generation. If you are coming from anything older, read that release's notes too — everything in it is in here.

## Changelog and engineering rationale

- **[MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.03-stability/docs/CHANGELOG-stability.md)** — the numbered list, items 177–178 for this release, 170–176 for the previous one.
- **[GPS PR draft](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.03-stability/docs/pr-gps-draft-20260902.md)** and **[T-Deck key-repeat PR draft](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.03-stability/docs/pr-tdeck-keyrepeat-draft-20260902.md)** — the reasoning behind the previous release's subsystems, in the structure the upstream submissions use (German). The `--setlog` design is in [`docs/setlog-instrumentation-impl-plan-20260902.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.03-stability/docs/setlog-instrumentation-impl-plan-20260902.md).
- **[Engineering write-up of the back-pressure campaign](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.03-stability/docs/pr-draft-20260831.md)** — items 107–169, unchanged in this release.
- [MeshCom@ICSSW project page](https://icssw.org/en/meshcom/)

## Built for the field: debug logs from any node

This build deliberately ships the **full serial instrumentation** (`INSTRUMENT_ENABLED=1`, `MC_INJECT_HOOKS=1`), so a node in the field can produce machine-readable debug logs that we can analyze offline afterwards — that is how every finding in the previous releases was measured, and it works the same on your kitchen-shelf gateway:

- Markers are compact `[TAG];key;value` lines: `[WIFI]`, `[ETH]`, `[GW]`, `[UDP]`, `[NTP]`, `[INSTR-LOOP]`, `[SPITRACE]`, `[KBD]` and more. No marker floods: high-rate candidates are rate-limited or bound to their own switch.
- Turn on what you need: `--setlog on` (the per-message line set), `--gpsdebug on` (fix/position/reject/convergence every 3 s), `--debug on` (verbose), `--udplog on` (gateway UDP in/out), `--loradebug on` (RX/TX, dedup, TX-ring), `--wifistat`, `--ethstat`, `--udpstat`, `--instr`, `--heap`.
- Capture the USB serial console — or the **network console on TCP port 2323** (ESP32 boards), which serves the same log over WiFi so the node can stay in place: `nc <node-ip> 2323`, or long-term with `tools/meshlogger.py` from this repository. Note for ESP32-S3 boards on native USB (T-Deck, T-Beam Supreme): output written before the port was opened is dropped after the first ~256 bytes, so a log that appears to start at boot may not — open the terminal first, then reboot.
- Reproduce without a second station: `--injectmsg`/`--injectpos` push messages into the display pipeline, `--injectraw <hex>` runs a raw frame through the **real** receive path (decode, dedup, mheard, relay), `--loratx <n> <ms>` generates bounded TX bursts. T-Deck additionally: key, trackball and touch injection, `--disptest`, `--spitrace`.
- Everything is off by default at runtime and can be compiled out entirely. (One exception: the RAM-tightest board, `E22_XML-DevKitC`, ships without the frame-capture ring and the injection hooks — its classic-ESP32 RAM segment cannot fit them. All markers and log switches are still there.)

If you capture a log of misbehavior in the field, open an issue with the log attached — the markers are designed to be evaluated.

## What changes on the air

New in this release:

1. **`--postime <seconds>` actually sets the periodic position interval.** Any value of 300 s or more is kept. Before, it was silently replaced by the compiled-in default, so nodes beaconed on the default period no matter what you configured. If you had given up on the command, set it again — and check with `--info` that the value stuck.
2. The version string your node announces reads `4.35s` instead of `4.35p`.

Everything below was already true in `v4.35p.09.02-stability` and is unchanged here (details in the changelog):

3. Message texts are filtered on RX and TX: control characters, invalid UTF-8 and bidi/zero-width characters are removed. Umlauts and emoji pass unchanged.
4. Frames from unconfigured nodes (`XX0XXX…`) are discarded, and an unconfigured node does not transmit at all.
5. Shot-path beacons (`--sendpos`, button, EXTUDP injection, `--sendhey`) have a 30-second floor. Periodic beacons are unchanged.
6. `/B=` is omitted when no battery is detected — instead of a percentage invented from ADC noise.
7. The `/N` neighbour-count tag is correct even without a valid wall clock.
8. HEY link chains are bounded on ingress (`HEY_PATH_PAYLOAD_MAX`).
9. Back-pressure notices and receipts (QRS/QRT/QTA/QRV) go **only** to the transport the message came from — never over the air.
10. `--maxhop 1..6` makes the text hop limit settable and persistent.
11. Server CONF frames (callsign/shortname assignment) are understood on ESP32 and actually applied on both platforms, with source-IP guard and validation.
12. A gateway no longer self-uploads its own HEY beacon to the server, so the neighbours' enriched copies of the same msg_id win server-side.
13. Queued HEY frames older than 3 minutes are dropped instead of transmitted. Text, position and ACK traffic never ages out.
14. A message the TX queue drops never reaches the backbone.
15. Heltec Wireless Stick V3 positions carry `/B=` again (corrected ADC multiplier, upstream #1119 and #1124).
16. Position beacons of a stationary GPS node carry the filtered altitude; a spliced or implausible fix never becomes a beacon or a clock set. TRACK mode beacons the raw altitude.
17. WX telemetry QNH on nodes with GPS and a pressure sensor is computed against the converged altitude estimate, not against the first fix after boot.

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean**; 568 native test cases green across 12 host environments; 250 tool tests green.
- **No hardware ran against this tree.** That is defensible only because the source outside upstream's two hunks is byte-identical to `v4.35p.09.02-stability`: `git diff` between the two tags touches exactly two files under `src/` — `command_functions.cpp` (the `--postime` `else` branch, removed) and `configuration_global.h` (the version letter and the `FLASH_VERSION` stamp). Nothing else. The bench and field results below are therefore carried over verbatim from that release and are **not** new evidence.
- **Carried over — T-Deck Plus (DK5EN-14, bench)**: GPS run of 6.5 minutes, 128 fixes, 0 rejects, 0 corrupt samples, altitude converged after 256 s; keyboard raw mode confirmed, repeated deletion seen on screen.
- **Carried over — T-Deck Plus and T-Beam Supreme (OE5HWN, field, one hour each)**: 1214 and 1161 evaluations, 0 rejects, 0 corrupt samples; Supreme converged after 88 samples at 280 m and re-latched at 276 m after a reboot.
- **Heltec V3, T-Beam v1.2, RAK4631** — builds only, as in the previous cycle. Their last bench time was the `v4.35p.09.01` back-pressure campaign.

### Built and shipped, not on our bench

These boards build cleanly from the same source and inherit every improvement, but had no bench time here:

- T-Deck without Plus
- E22-DevKitC, E22_1262-DevKitC, E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, E22_XML-DevKitC
- esp32-loraprs-e22, esp32-loraprs-ra01
- heltec_wifi_lora_32_V2, heltec_wifi_lora_32_V4, heltec_wireless_stick (both ADC fixes are OE3LCR's reference measurements, not ours), heltec_wireless_tracker, wireless-paper
- vision-master-e213, vision-master-e290
- ttgo-lora32-v21, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, T-Beam-1W
- T3_S3_V13, t_connect_pro, t_deck_pro, T-ETH-ELITE_1262
- heltec_t114, t_echo

(The T5 e-paper variant is not included: it does not build from the current tree for a pre-existing include-path reason unrelated to these changes.)

### Known gaps, stated plainly

New with this release:

- **`--postime 0` no longer switches position beacons off.** With the `else` branch gone, `0` is below the 300-second floor and is clamped up to 300 s. Turning periodic positions off is not reachable through this command any more. The fork does not patch this on its own — it belongs upstream as its own change, and diverging over it would only make the next sync harder.
- **The `--postime` fix itself has not been measured on a node.** It is read and understood; no node was watched against a changed beacon period for this cut.
- **GPS-07 (filed, not fixed):** an overnight capture on 2026-09-03 shows the altitude Kalman filter re-seeding onto raw outliers ten times in one night — the re-seed guard is sized too small. `RESEED_N 60` or a 30 m gate fixes it in replay; **neither is in this release.** A stationary node can therefore still publish an altitude step it should have rejected.

Carried over from `v4.35p.09.02-stability`, all still open:

- **The `--setlog` line set has no hardware run yet.** Formatters and counters are pinned by native cases and the log parser by tool tests; the 30-minute two-node bench has not been executed.
- **The GPS two-hour comparison arms (A/B/C) have not been run.**
- **The T-Deck pin fallback is unverified on hardware.**
- **Key auto-repeat needs a keyboard controller with raw mode** (LilyGo controller firmware from 2025-06-12 on). Older controllers cannot be updated from the main firmware; `--info` shows `KBD raw-mode unknown` after typing on such a unit.
- **`--wx` still prints `ALT asl: 0 m`** on nodes without a base pressure. There is no pressure-offset setting comparable to the temperature offset; a BMP280 that reads 1–2 hPa high shows in the QNH.
- **The T-Deck ships with partial refresh (`full_refresh = 0`) plus the flush-fix NOP mitigation** for the shared-SPI-bus lost-flush defect. Map panning works but is step-wise without a tile cache (0.33–0.79 s per step).
- **CONF coordinates** from the server are parsed and logged, not applied.
- The `blelen + 2` length computation in `sendToPhone()` can still wrap on ESP32 for a JSON payload past 253 bytes; no current builder produces one, but the arithmetic is unfixed. The BLE `I` register with six group-call slots filled is still over the frame limit, and the negotiated ATT MTU is never read. All reported upstream.
- The battery zero point on a real 2S pack and the INA226 branch remain unverified.
- **TM-49 (open):** the safeboot OTA completion handler can read a success status after a disconnect whose final frame never arrived, and switch boot partitions after a partial write — benign on 16-MB boards (slot validation catches it), risky on 4-MB single-slot boards. Until the guard lands: on 4-MB boards prefer USB flashing over OTA when the link is marginal.
- The three-own-messages rule for QRS (item 167) still has no hardware burst against it.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.03-stability/README.md#flashing-firmware) (`bootloader.bin` for classic ESP32, `bootloader-s3.bin` for ESP32-S3).
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`
- **T-Deck without Plus with your own GPS module:** wire the module's TX to GPIO44 and its RX to GPIO43 (the T-Deck Plus assignment). A module wired the 4.35d way still works through the fallback, at the cost of 12 s at boot, and the log asks you to swap.

## Upstream

Everything here is written up for upstream: the PR drafts linked above carry the submission text. Items 1–103 of the changelog are already in official MeshCom (PRs [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102), [#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103)); items 170–176 followed in [#1125](https://github.com/icssw-org/MeshCom-Firmware/pull/1125). The MeshCom project deserves the finding **and** the fix, not a bug report thrown over the wall.
