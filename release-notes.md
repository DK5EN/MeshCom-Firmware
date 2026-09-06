> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**Everything this fork has on top of official MeshCom `4.35s`, in one build.** Upstream released `4.35s` on 3 September 2026; it already contains 103 changes of ours that the ICSSW maintainers merged on 27 August (PRs [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102) and [#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103)). What is **not** in official `4.35s` is changelog items 104–208 — the work of the four release cycles since, collected here so it can be flashed and field-tested as one firmware while the individual pull requests make their way upstream.

If you run official `4.35s` today, this document describes your whole upgrade. If you run one of this fork's earlier builds, the newest items are 202–208 and the per-release breakdown is in the changelog.

Flash version `20260906`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

## What you get, by theme

The numbered items are in the [MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/CHANGELOG-stability.md).

**Input hardening and web security** (107–118). Stored XSS in the web message page; three web JSON endpoints that escaped nothing; double-escaped EXTUDP JSON; all 13 BLE register builders on one fail-soft frame path; frames from unconfigured `XX0XXX` nodes dropped on RX and never sent; eight parser findings from the test campaign; a single 255-byte datagram that killed EXTUDP receive permanently; a one-byte over-read in `addUdpOutBuffer()`; settings sanity-checked after loading from flash; a UTF-8 allowlist at the two chokepoints that see every text.

**Back-pressure: a full node no longer lies to you** (156–167, 179). The TX ring counts occupied slots instead of index distance, stale BACKGROUND entries age out, a refusal episode ends when the load is really gone, QRS fires at depth 5 and QRV only after real recovery, every refused or dropped message gets an app-visible receipt, and a dropped message never echoes as sent. Item 179 closes the loop that made a node radiate `QRT NOT SENT - QRT NOT SENT - ...` when a client fed the node's own wording back in.

**Network path: WiFi, NTP, Ethernet, gateway** (119–128, 131, 176). Asynchronous NTP instead of a 1-second blocking client; a non-gateway node actually reads the NTP reply; MHeard ages on the monotonic clock; the WPA2/WPA3 transition-AP first join fixed (SAE without PMF); WiFi bring-up off the main loop; an ESP32 gateway relays UDP position frames to LoRa again; nRF52 link poll, heartbeat and DHCP renewal no longer escape the gateway guard; server CONF frames understood and applied on both platforms; a gateway no longer self-uploads its own `'@'` HEY.

**ACK attribution toward the phone** (192, 193). ACK status frames carry the callsign of the station that acknowledged — node ACK the last hop, peer ACK the partner — byte-identical to before when the callsign is unknown, so the official app is unaffected. A gateway no longer books messages it merely forwarded from the server as its own; McApp was recording roughly 150 false `send_success` rows per gateway per day.

**Deep sleep, made real** (197–200). `--deepsleep` on every ESP32 board and a real System OFF on RAK4631, Heltec T114 and T-Echo; boards come back from the wake reset with their rails and radio working. Details and limits below.

**GPS and altitude** (174, 204–206). The GPS altitude and QNH path from the `OE5HWN-14` field report; the barometric reference now arms itself instead of returning `ALT asl: 0 m` until someone types `--setpress`; the altitude filter stops re-seeding onto raw outliers; boards with a BMx280 fuse GPS and barometer. Measured on the bench: reported altitude standard deviation **6.6 m**, against 17.2 m on the previous firmware and 12.2 m raw.

**Web GUI and web API** (134–136, 189, 190, 194–196). `getparam()` — the read half of the web API — was completely dead and works; config backup and restore over `GET /config.json`; an honest info page; a LoRa queue panel on the RX-log page with a QRS forecast that reflects what would really happen; unread badges with counts on the message tabs; `{CET}` time beacons no longer listed as messages.

**T-Deck** (140–147, 175, 181–185, 199, 201). Backlight really off on `--display off`; audio off the main loop; GT911 touch init retries; trackball on interrupts; a map that pans; key auto-repeat; the trackball push button firing two clicks per press; the map tab composing the SD map twice; and the ESP32-S3 native-USB crash loop when the host opens the port during boot — 9 of 30 port-opens crashed before, 0 of 80 after.

**Telemetry on the air** (187, 207). The EXTUDP `tele` datagram for relayed nodes carries the station it came from; MCP23017 port A inputs go on the air as `/D=` in the position beacon and in the digital slot of the APRS `T#` frame (upstream issue 1076). Nodes without the chip produce byte-identical frames.

**Safeboot and OTA** (153–155, 186). Recovery from an aborted OTA upload, the production WiFi join pattern in safeboot, `tools/webflash.py` handling the T-Deck family, and a fail-closed OTA completion check.

**Build, test and tooling** (150–152, 164, 166, 169, 170, 191, 202, 208). `-Wformat=2 -Werror` for our own sources on both platforms; every variant passing `--port` to esptool; PlatformIO upload on hosts without `upload_port`; integrated regression suites that pin the whole back-pressure incident class; and the bench instrumentation no longer shipping in a normal board build.

## What changes on the air

Almost nothing. Three items change what a node transmits or reports, and each is a correction or an opt-in addition:

1. **Frames from unconfigured nodes (`XX0XXX…`) are discarded on RX and never sent** (item 111).
2. **Shot-path beacons have a 30-second floor** (item 132) — `--sendpos`, the user button and the boot beacon can no longer be fired back to back.
3. **`/D=` in the position beacon and the `T#` digital slot** (item 207) — only on a node that actually has an MCP23017 answering at boot. Every other node's frames are byte-identical to before. The MeshCom server and the official app do not know the field yet; that is what the upstream PR is for.

Toward the **phone app**, two things change: ACK status frames carry the acknowledging station's callsign (item 192), and a gateway stops sending heard and gateway-ACK frames for messages it only forwarded from the server (item 193).

## Debug logs from a node in the field

Two of the previous release's claims in this section were wrong, and this is the corrected version.

**What ships in a normal board build:**

- **Markers**: compact `[TAG];key;value` lines — `[WIFI]`, `[ETH]`, `[GW]`, `[UDP]`, `[NTP]`, `[KBD]`, `[SAFEBOOT]` and more. High-rate candidates are rate-limited or bound to their own switch.
- **Log switches**: `--setlog on` (the per-message line set), `--gpsdebug on` (fix/position/reject/convergence every 3 s), `--debug on` (verbose), `--loradebug on` (RX/TX, dedup, TX-ring), and — on a gateway — `--udplog on/off`, `--udpstat`, `--wifistat` (ESP32) and `--ethstat` (nRF52).
- **Where to capture it**: the USB serial console, or the **network console on TCP port 2323** (ESP32 boards), which serves the same log over WiFi so the node can stay in place: `nc <node-ip> 2323`, or long-term with `tools/meshlogger.py` from this repository. Note for ESP32-S3 boards on native USB (T-Deck, T-Beam Supreme): output written before the port was opened is dropped after the first ~256 bytes, so a log that appears to start at boot may not — open the terminal first, then reboot.

**What is no longer in a normal build** (item 202, changed in this release): the bench and measurement surface — `[INSTR-LOOP]` loop-gap markers, `--instr`, `--heap`, `--injectmsg`, `--injectraw`, `--loratx`, `--ntpsync`, `--flashpoke`, `--disptest`, `--spitrace` and the T-Deck UI hooks. Field nodes were printing `[INSTR-LOOP]` lines unprompted and users reported them as error messages. If you need them, build a measurement firmware: `PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1" pio run -e <env>`.

If you capture a log of misbehavior in the field, open an issue with the log attached — the markers are designed to be evaluated.

## Deep sleep: what is in, what is not, where we need your help

Upstream issue #962 asked why the low-battery deep sleep does nothing. The verdict is in [the linked document](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/issue-962-deepsleep-verdict.md): it is not misbehaving, it was switched off entirely for issue #1053 (`e0043a56`, 4.35p.07.11), on every board, and the manual `--deepsleep` command never really slept either.

**What this release does** (items 197–200):

- `--deepsleep` is a real sleep on **every ESP32 board**: radio to sleep, display off, PMU rails off on the T-Beam family, the shared rail cut on the T-Deck and T-Deck Plus, a button wake armed on the configured button pin. Wake is the user button or RESET.
- `--deepsleep` on **RAK4631, Heltec T114 and T-Echo** is a real nRF52 System OFF (the T-Echo long-press too). Wake is the button, plugging in USB, or RESET.
- Boards wake up **with their rails and radio working**: the T-Deck, T-Deck Plus and T-Beam-1W left a gpio hold latched across the wake reset, which killed SD card, keyboard and LoRa until a power cycle (item 199). The same mechanism on Wireless Paper and Vision Master E213 is fixed blind (item 200).

**What this release deliberately does not do:**

- **No automatic low-voltage shutdown.** Issue 962 Option B (a timer-wake loop with hysteresis, opt-in) is designed but not implemented. The old guard stays commented out as upstream left it.
- **No light sleep.** Option C (light sleep with LoRa wake) is advised against in the verdict and was not attempted.
- **No sleep-current figures.** We have no bench supply, no discharged packs and no inline current meter on the desk. The threshold, hysteresis, recovery-loop and 1053-regression rows of the bench matrix cannot be run here, and the "tens of microamps" and "about 2 µA" numbers are datasheet and core figures, not measurements. Implementing a shutdown that decides on its own when a node goes dark, without being able to test it, is not something we are willing to ship.

**We rely on the community for these tests.** If you own one of the boards below, please put it to sleep with `--deepsleep`, wake it, and post the boot log's reset-reason line and whether LoRa, display and GPS came back — in the issue 962 thread or as an issue on this repository:

- **Wireless Paper, Vision Master E213** (item 200 is a blind fix): two consecutive `--deepsleep` / button-wake cycles with `--mheard` still receiving afterwards.
- **Vision Master E290**: the e-ink keeps drawing its last frame through sleep; we have no unit to verify a clear.
- **T-Beam Supreme** (AXP2101 rail cut, compile-verified only), **T-Beam SX1262 / SX1268** (AXP192 rails), **T-Beam-1W** (radio LDO).
- **Heltec T114 and T-Echo** (System OFF, compile-verified only; the RAK4631 is the only nRF52 board on our bench).
- **E22-DevKitC, E22_XML-DevKitC, ttgo-lora32-v21**: the button wake pin has no external pull-up on these; the firmware keeps the internal one alive through sleep, which we could not test on those boards.
- **Anyone with a meter**: sleep current on the battery lead, on any board.

## Changelog and engineering rationale

- **[MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/CHANGELOG-stability.md)** — the numbered list; items 104–208 are the delta against official `4.35s`, items 202–208 are new since `v4.35s.09.06`.
- **[Engineering write-up of the back-pressure campaign](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/pr-draft-20260831.md)** — items 107–169, with per-change file references and measurements.
- **[Issue 962 deep sleep verdict and plan](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/issue-962-deepsleep-verdict.md)** — what `--deepsleep` did before, the three options, the nRF52 System OFF plan, and the bench matrix we could and could not run (English).
- **[gpio-hold and HWCDC follow-ups](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/gpio-hold-and-hwcdc-followups.md)** — the two defects found while bench-testing deep sleep on the T-Deck Plus.
- **[ACK attribution plan](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/ack-implementierungsplan.md)** and **[the gateway heard-frame fix](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35s.09.06.2/docs/ack-heard-foreign-msgids-fix.md)** — items 192 and 193 (German).
- [MeshCom@ICSSW project page](https://icssw.org/en/meshcom/)

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean**; 643 native test cases green across 12 host environments.
- **Heltec V3 (DK5EN-93 and the gateway DK5EN-98, bench)**: barometric reference self-latch, altitude filter and GPS/barometer fusion measured over two hours (items 204–206); MCP23017 `/D=` receive path through `--injectraw` to the EXTUDP listener (item 207, transmit path natively only — no MCP23017 here); the restored field-diagnostic switches (item 202) on a stock build; ACK attribution on the air against McApp (item 192); unread badges via a jsdom harness against the live node, 30 checks (item 195).
- **T-Deck Plus (DK5EN-14, bench)**: `--deepsleep` and button wake with the gpio-hold fix, rails and radio back after wake (items 197, 199); port-open crash loop 9 of 30 before, 0 of 80 after (item 201); the restored diagnostic switches on a stock build.
- **WisBlock RAK4631 (DK5EN-90, bench)**: `--deepsleep` System OFF and wake (item 198); `--ethstat` and `--udplog` on a stock build (item 202); harness regression run on the release code — boot, Ethernet, LoRa RX/TX and MHeard nominal.
- **T-Beam v1.2 (DK5EN-92, bench)**: boot and radio init on the release build.

### Built and shipped, not on our bench

These boards build cleanly from the same source and inherit every improvement, but had no bench time here:

- T-Deck without Plus, t_deck_pro
- E22-DevKitC, E22_1262-DevKitC, E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, E22_XML-DevKitC
- esp32-loraprs-e22, esp32-loraprs-ra01
- heltec_wifi_lora_32_V2, heltec_wifi_lora_32_V4, heltec_wireless_stick, heltec_wireless_tracker, wireless-paper
- vision-master-e213, vision-master-e290
- ttgo-lora32-v21, ttgo_tbeam_supreme, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, T-Beam-1W
- T3_S3_V13, t_connect_pro, T-ETH-ELITE_1262
- heltec_t114, t_echo

(The T5 e-paper variant is not included: it does not build from the current tree for a pre-existing include-path reason unrelated to these changes.)

### Known gaps, stated plainly

New with this release:

- **The GPS/barometer fusion misses its own design gate.** The target was a reported-altitude standard deviation of 4 m or better; the two-hour bench run gave 6.6 m, because the GPS mean itself walked 30 m over one hour and a 30-minute low-pass follows it. An offline replay of the same capture reaches 3.6 m at tau 2 h and 2.4 m at tau 4 h. Tau is one constant (`ALT_FUSION_TAU_MS`) and the choice is deliberately left open.
- **No moving-node altitude proof.** TRACK mode bypasses both filters, and a TRACK-mode capture with pressure has not been recorded.
- **The MCP23017 transmit path is proven natively only** (item 207). There is no MCP23017 on the bench; the receive side and the EXTUDP `din` key were proven on hardware, the `/D=` a real chip would produce was not.
- **The T-Beam Supreme OLED change (item 203) is unverified on hardware.** No Supreme board here.
- **The bench and injection commands are gone from a normal build** (item 202). `--injectraw`, `--injectmsg`, `--loratx`, `--instr`, `--heap` and the T-Deck UI hooks need an `INSTRUMENT_ENABLED=1` build. The injection machinery itself is still compiled in and costs flash without being reachable — a size cleanup that has not been done.
- **No low-voltage shutdown and no light sleep** (see the deep sleep section). The low-battery guard stays disabled as upstream left it.
- **Item 200 (Wireless Paper / Vision Master E213 chip-select hold) is a blind fix.** Compile-verified only; nobody here has seen it work.
- **Deep sleep on T-Beam Supreme, the T-Beam SX1262/SX1268 variants, T-Beam-1W, Heltec T114, T-Echo, Vision Master E290 and the E22 DevKitC boards is compile-verified only.** No sleep current was measured on any board.
- **Vision Master E290 keeps its last e-ink frame through sleep.**
- **The unread badge is a lower bound after a long absence**: the node's ring holds 20 slots shared with positions and acks, and a node without a valid clock stamps its messages near zero, so those count only within one browser session.
- **The QRS forecast (item 194) has not been eyeballed against a real burst in the browser.** Its getters are pinned by five native cases.
- **ACK attribution stage 4 (the wire appendix) is not in**; Gateway ACK stays anonymous.
- **CTY-02 is not in this release.** A gateway fix for upstream issue #1133 was written and bench-proven on both platforms in this cycle, then reverted after the issue was refuted. Nothing of it ships.

Carried over from earlier releases, all still open:

- **TD-15 (filed, not fixed):** after a reboot the T-Deck map shows only stations whose position beacon arrived since boot.
- **MEM-04 (risk, not a defect):** `ttgo_tbeam`, `ttgo_tbeam_SX1262` and `ttgo_tbeam_SX1268` link with about 20 bytes of IRAM headroom, `E22_XML-DevKitC` with under 1 kB of DRAM. This release builds; any future feature that touches IRAM will not on those four.
- **The safeboot fail-closed gate (item 186) has no bench arm on a 4 MB board yet.**
- **The echo guard's ring-flood case (item 179) is still owed** on the T-Deck bench.
- **`--postime 0` no longer switches position beacons off.** `0` is below the 300-second floor and is clamped up to 300 s. This comes from upstream's own `4.35s` fix (item 177) and is filed for an upstream PR rather than patched here.
- **The `--setlog` line set has no hardware run yet.**
- **The GPS two-hour comparison arms (A/B/C) have not been run.**

## Installing

Pick the asset for your board. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive. Coming from a build older than `v4.35p.08.22-stability`, check `--info` afterwards.

## Upstream

Everything here is offered back to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) as individual pull requests; 103 of our changes are already in official `4.35s`. Please report bugs that also exist in the official firmware over there.
