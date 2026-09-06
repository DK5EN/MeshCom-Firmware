> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**A stability release on upstream's `4.35s`, with ten changes of this fork on top of `v4.35s.09.05`.** Three groups: `--deepsleep` now really sleeps on every board, with the wake-side fixes that the bench surfaced; the ACK status frames toward the phone carry the callsign of the acknowledging station and a gateway no longer books foreign messages as its own; and two web GUI additions, unread badges on the message tabs and a QRS forecast in the queue panel. One crash fix for ESP32-S3 boards on native USB.

Flash version `20260906`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

If you are coming from `v4.35s.09.05`, everything new is in changelog items 192–201. If you are coming from anything older, read that release's notes too — everything in it is in here.

## Changelog and engineering rationale

- **[MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06/docs/CHANGELOG-stability.md)** — the numbered list, items 192–201 for this release, 179–191 for the previous one.
- **[Issue 962 deep sleep verdict and plan](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06/docs/issue-962-deepsleep-verdict.md)** — what `--deepsleep` did before, the three options, the nRF52 System OFF plan, and the bench matrix we could and could not run (English).
- **[gpio-hold and HWCDC follow-ups](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06/docs/gpio-hold-and-hwcdc-followups.md)** — the two defects found while bench-testing deep sleep on the T-Deck Plus, one fixed with proof, one fixed blind.
- **[ACK attribution plan](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06/docs/ack-implementierungsplan.md)** and **[the gateway heard-frame fix](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06/docs/ack-heard-foreign-msgids-fix.md)** — items 192 and 193 (German).
- **[Engineering write-up of the back-pressure campaign](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06/docs/pr-draft-20260831.md)** — items 107–169, unchanged in this release.
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

Nothing in this release changes a LoRa frame. Two things change what a node tells its **phone app**:

1. **ACK status frames to the app carry the callsign of the station that acknowledged** (item 192). Node ACK carries the last hop, Peer ACK the partner; the frame is byte-identical to before when the callsign is unknown, so the official app is unaffected. A session flag `--ackinfo on` (McApp sets it on connect, reset on every BLE disconnect) lets every relaying neighbour produce its own heard frame for messages this node minted.
2. **A gateway no longer sends heard and gateway-ACK frames to the app for messages it only forwarded from the server** (item 193). McApp booked roughly 150 false `send_success` rows per day per gateway before.

## Deep sleep: what is in, what is not, where we need your help

Upstream issue #962 asked why the low-battery deep sleep does nothing. The verdict is in the linked document: it is not misbehaving, it was switched off entirely for issue #1053 (`e0043a56`, 4.35p.07.11), on every board, and the manual `--deepsleep` command never really slept either.

**What this release does** (items 197–200):

- `--deepsleep` is a real sleep on **every ESP32 board**: radio to sleep, display off, PMU rails off on the T-Beam family, the shared rail cut on the T-Deck and T-Deck Plus, a button wake armed on the configured button pin. Wake is the user button or RESET.
- `--deepsleep` on **RAK4631, Heltec T114 and T-Echo** is a real nRF52 System OFF (the T-Echo long-press too). Wake is the button, plugging in USB, or RESET.
- Boards wake up **with their rails and radio working**: the T-Deck, T-Deck Plus and T-Beam-1W left a gpio hold latched across the wake reset, which killed SD card, keyboard and LoRa until a power cycle (item 199). The same mechanism on Wireless Paper and Vision Master E213 is fixed blind (item 200, see below).

**What this release deliberately does not do:**

- **No automatic low-voltage shutdown.** Issue 962 Option B (a timer-wake loop with hysteresis, opt-in) is designed but not implemented. The old guard stays commented out as upstream left it.
- **No light sleep.** Option C (light sleep with LoRa wake) is advised against in the verdict and was not attempted.
- **No sleep-current figures.** We have no bench supply, no discharged packs and no inline current meter on the desk. The threshold, hysteresis, recovery-loop and 1053-regression rows of the bench matrix cannot be run here, and the "tens of microamps" and "about 2 µA" numbers are datasheet and core figures, not measurements. Implementing a shutdown that decides on its own when a node goes dark, without being able to test it, is not something we are willing to ship.

**We rely on the community for these tests.** If you own one of the boards below, please put it to sleep with `--deepsleep`, wake it, and post the boot log's reset-reason line and whether LoRa, display and GPS came back — in the issue 962 thread or as an issue on this repository:

- **Wireless Paper, Vision Master E213** (item 200 is a blind fix): two consecutive `--deepsleep` / button-wake cycles with `--mheard` still receiving afterwards.
- **Vision Master E290**: the e-ink keeps drawing its last frame through sleep; we have no unit to verify a clear.
- **T-Beam Supreme** (AXP2101 rail cut, compile-verified only), **T-Beam v1.2 / SX1262 / SX1268** (AXP192 rails), **T-Beam-1W** (radio LDO).
- **Heltec T114 and T-Echo** (System OFF, compile-verified only; the RAK4631 is the only nRF52 board on our bench).
- **E22-DevKitC, E22_XML-DevKitC, ttgo-lora32-v21**: the button wake pin has no external pull-up on these; the firmware keeps the internal one alive through sleep, which we could not test on those boards.
- **Anyone with a meter**: sleep current on the battery lead, on any board.

## Web GUI

- **The message tabs turn green and show a count when unread messages arrived on them** (item 195). A message counts as unread when it is inbound, matches the tab, and is newer than the tab's watermark; the watermark survives a reload, viewing All reads everything, messages that land while the browser window is hidden stay unread until you look. Own messages and ack updates never count. The badge lives on the Messages page only. The `{CET}` time beacons a gateway pulls from the server are no longer listed (item 196), so they cannot light the tabs.
- **The QRS marker in the LoRa queue panel forecasts the depth at which your next messages would actually raise QRS** (item 194) instead of sitting at the fixed line.

## Native USB on ESP32-S3

- **Opening the serial port during boot no longer crash-loops the T-Deck Plus and its siblings** (item 201). The 4 kB TX ring from the previous release was created a few lines too late, while the CDC interrupt was already live. Bench: 9 of 30 port-opens crashed before, 0 of 80 after. Affects every env with native USB CDC on boot.

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean**; 616 native test cases green across 12 host environments.
- **T-Deck Plus (DK5EN-14, bench)**: `--deepsleep` and button wake with the gpio-hold fix in place, rails and radio back after wake (items 197, 199); port-open crash loop 9 of 30 before, 0 of 80 after (item 201).
- **WisBlock RAK4631 (DK5EN-90, bench)**: `--deepsleep` System OFF and wake (item 198); harness regression run on the release code: boot, Ethernet, LoRa RX/TX and MHeard nominal.
- **Heltec V3 (bench, and DK5EN-98 running as the operator's gateway)**: `--deepsleep` and button wake (item 197); ACK attribution on the air against McApp (item 192); unread badges driven by a jsdom harness against the live node, 30 checks (item 195); `{CET}` filter checked in the browser (item 196).
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

- **No low-voltage shutdown and no light sleep** (see the deep sleep section). The low-battery guard stays disabled as upstream left it.
- **Item 200 (Wireless Paper / Vision Master E213 chip-select hold) is a blind fix.** Compile-verified only; nobody here has seen it work.
- **Deep sleep on T-Beam Supreme, T-Beam v1.2 family, T-Beam-1W, Heltec T114, T-Echo, Vision Master E290 and the E22 DevKitC boards is compile-verified only.** No sleep current was measured on any board.
- **Vision Master E290 keeps its last e-ink frame through sleep.**
- **The unread badge is a lower bound after a long absence**: the node's ring holds 20 slots shared with positions and acks, and a node without a valid clock stamps its messages near zero, so those count only within one browser session.
- **The QRS forecast (item 194) has not been eyeballed against a real burst in the browser.** Its getters are pinned by five native cases.
- **ACK attribution stage 4 (the wire appendix) is not in**; Gateway ACK stays anonymous.
- **TD-15 (filed, not fixed):** after a reboot the T-Deck map shows only stations whose position beacon arrived since boot.
- **MEM-04 (risk, not a defect):** `ttgo_tbeam`, `ttgo_tbeam_SX1262` and `ttgo_tbeam_SX1268` link with about 20 bytes of IRAM headroom, `E22_XML-DevKitC` with under 1 kB of DRAM. This release builds; any future feature that touches IRAM will not on those four.

Carried over from earlier releases, all still open:

- **The safeboot fail-closed gate (item 186) has no bench arm on a 4 MB board yet.**
- **The echo guard's ring-flood case (item 179) is still owed** on the T-Deck bench.
- **`--postime 0` no longer switches position beacons off.** `0` is below the 300-second floor and is clamped up to 300 s.
- **GPS-07 (filed, not fixed):** the altitude Kalman filter re-seeds onto raw outliers; `RESEED_N 60` or a 30 m gate fixes it in replay, neither is in this build.
- **The `--setlog` line set has no hardware run yet.**
- **The GPS two-hour comparison arms (A/B/C) have not been run.**
- **The T-Deck pin fallback is unverified on hardware.**
- **Key auto-repeat needs a keyboard controller with raw mode** (LilyGo controller firmware from 2025-06-12 on).
- **`--wx` still prints `ALT asl: 0 m`** on nodes without a base pressure.
- **The T-Deck ships with partial refresh plus the flush-fix NOP mitigation** for the shared-SPI-bus lost-flush defect.
- **CONF coordinates** from the server are parsed and logged, not applied.
- The `blelen + 2` length computation in `sendToPhone()` can still wrap on ESP32 for a JSON payload past 253 bytes; no current builder produces one.
- The battery zero point on a real 2S pack and the INA226 branch remain unverified.
- The three-own-messages rule for QRS (item 167) still has no hardware burst against it.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06/README.md#flashing). Use `bootloader-s3.bin` and `safeboot-s3.bin` for ESP32-S3 boards, `bootloader.bin` and `safeboot.bin` for classic ESP32.
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`
- **T-Deck without Plus with your own GPS module:** wire the module's TX to GPIO44 and its RX to GPIO43 (the T-Deck Plus assignment). A module wired the 4.35d way still works through the fallback, at the cost of 12 s at boot.

## Upstream

Everything here is written up for upstream: the documents linked above carry the submission text. Items 1–103 of the changelog are already in official MeshCom (PRs [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102) and [#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103)). The deep sleep work (items 197–200) answers issue #962 and is meant for a PR against upstream `dev` once the community tests above have come back; the gateway heard-frame fix (item 193) and the native-USB fixes (items 184, 201) are PR candidates on their own.
