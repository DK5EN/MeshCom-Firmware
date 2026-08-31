> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**The complete field campaign since `v4.35p.08.28-stability`**: a stored XSS fix in the web UI, WiFi first-join on WPA2/WPA3 transition APs, asynchronous NTP that finally works on non-gateway nodes, HEY link-data parity for gateways, TX back-pressure to the sender, "no battery" detection, config backup/restore, a T-Deck that pans its map and no longer stalls on audio — 46 numbered changes (items 107–152), each verified on a four-board bench (Heltec V3, T-Beam v1.2, T-Deck Plus, RAK4631/Ethernet) with 438 native test cases across 12 host environments and soak runs up to 9.1 hours.

Flash version `20260831`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

## Changelog and engineering rationale

- **[MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.31-stability/docs/CHANGELOG-stability.md)** — the numbered list, items 107–152 for this release.
- **[Engineering write-up / upstream PR draft](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.31-stability/docs/pr-draft-20260831.md)** — every change with file references, measurements and the reasoning, in the structure the upstream submission will use (German).
- [MeshCom@ICSSW project page](https://icssw.org/en/meshcom/)

## Built for the field: debug logs from any node

This build deliberately ships the **full serial instrumentation** (`INSTRUMENT_ENABLED=1`, `MC_INJECT_HOOKS=1`), so a node in the field can produce machine-readable debug logs that we can analyze offline afterwards — that is how every finding in this release was measured, and it works the same on your kitchen-shelf gateway:

- Markers are compact `[TAG];key;value` lines: `[WIFI]`, `[ETH]`, `[GW]`, `[UDP]`, `[NTP]`, `[INSTR-LOOP]`, `[SPITRACE]` and more. No marker floods: high-rate candidates are rate-limited or bound to their own switch.
- Turn on what you need: `--debug on` (verbose), `--udplog on` (gateway UDP in/out), `--loradebug on` (RX/TX, dedup, TX-ring), `--wifistat`, `--ethstat`, `--udpstat`, `--instr`, `--heap`.
- Capture the USB serial console — or the **network console on TCP port 2323** (ESP32 boards), which serves the same log over WiFi so the node can stay in place: `nc <node-ip> 2323`, or long-term with `tools/meshlogger.py` from this repository.
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
7. Back-pressure notices (QRS/QRT/QTA/QRV) go **only** to the transport the message came from — never over the air.
8. `--maxhop 1..6` makes the text hop limit settable and persistent.
9. Server CONF frames (callsign/shortname assignment) are understood on ESP32 and actually applied on both platforms, with source-IP guard and validation.
10. **A gateway no longer self-uploads its own HEY beacon to the server.** The bare, report-less copy always arrived seconds before the neighbours' enriched copies of the same msg_id and could win over them server-side — the reason a gateway's neighbour data vanished from the server while `--gateway off` showed it. Measured against the server's interlink stream; with the fix, only enriched copies arrive, in both gateway states.

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean**; 438 native test cases green across 12 host environments.
- **Heltec V3** — WiFi/NTP/battery/OLED changes bench-proven; runs the GW-01 fix build, verified against the live server stream.
- **T-Beam v1.2** — WiFi soak (9.1 h, 55/55 reconnects), gateway observer in the GW-01 measurement.
- **T-Deck Plus** — full harness regression (boot, display CRC, map, nav, input, heap, trim, touch injection) PASS on this build.
- **RAK4631 (WisBlock, W5100S Ethernet)** — ETH-01/CTY-01/NTP paths bench-proven; bench gateway during the campaign.

### Built and shipped, not on our bench

These boards build cleanly from the same source and inherit every improvement, but had no bench time here:

- T-Beam Supreme (its L76K GPS branch is exercised by no test — our bench GPS modules are u-blox)
- E22-DevKitC, E22_1262-DevKitC, E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, E22_XML-DevKitC
- esp32-loraprs-e22, esp32-loraprs-ra01
- heltec_wifi_lora_32_V2, heltec_wifi_lora_32_V4, heltec_wireless_stick, heltec_wireless_tracker, wireless-paper
- vision-master-e213, vision-master-e290
- ttgo-lora32-v21, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, T-Beam-1W
- T3_S3_V13, t_connect_pro, t_deck, t_deck_pro, T-ETH-ELITE_1262
- heltec_t114, t_echo

(The T5 e-paper variant is not included: it does not build from the current tree for a pre-existing include-path reason unrelated to these changes.)

### Known gaps, stated plainly

- **The T-Deck ships with partial refresh (`full_refresh = 0`) plus the flush-fix NOP mitigation** for the shared-SPI-bus lost-flush defect. The clobbered register is now identified (`GPSPI2.clock`, via `--spitrace`); replacing the NOP with a targeted re-arm is a follow-up. Map panning works but is step-wise without a tile cache (0.33–0.79 s per step).
- **CONF coordinates** from the server are parsed and logged, not applied.
- The `blelen + 2` overflow exists generically (the Mheard path no longer reaches it); the BLE `I` register with six group-call slots filled is still over the limit; the negotiated ATT MTU is never read. All reported upstream.
- `--mheard` and the live BLE path still deliver two schemas under the same `TYP`.
- The battery zero point on a real 2S pack, the INA226 branch and L76K GPS remain unverified.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.31-stability/README.md#flashing-firmware) (`bootloader.bin` for classic ESP32, `bootloader-s3.bin` for ESP32-S3).
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`

## Upstream

Everything here is written up for upstream: the PR draft linked above carries the full submission text. Items 1–103 of the changelog are already in official MeshCom (PRs [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102), [#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103)). The MeshCom project deserves the finding **and** the fix, not a bug report thrown over the wall.
