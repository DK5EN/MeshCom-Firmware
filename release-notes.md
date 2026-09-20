> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**Official MeshCom `4.35t` plus ten changes, in one build.** Upstream released `4.35t` on 10 September 2026, and it already contains this fork's items 104–210 (the ICSSW maintainers merged [PR #1135](https://github.com/icssw-org/MeshCom-Firmware/pull/1135) that day, after [#1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102) and [#1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103) in August). What official `4.35t` does **not** have is changelog items 212–221: the APRS position parser repairs, a web GUI fix, three field fixes against `4.35t` that upstream merged into `dev` today ([PR #1140](https://github.com/icssw-org/MeshCom-Firmware/pull/1140)) but has not shipped in a release with assets yet, and the upstream sync itself. This build also carries the fork-only pieces that were never offered upstream: the safeboot OTA recovery and fail-closed completion gate (items 153, 154, 186), `--port` in every esptool upload command (151) and the host-side test environments.

This release **replaces `v4.35t.09.10`**, whose release object has been deleted; that tag stays in the repository.

**This build reports itself as `4.35t`, exactly like official `4.35t`.** The letter was this fork's marker in `v4.35t.09.10`; upstream moved to the same letter one day later (item 221), so it no longer tells the two apart. The flash stamp in `--info` does: `20260912` here, `20260909` in official `4.35t`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

**Why `.2` in the tag:** upstream tagged its own `dev` as `v4.35t.09.12` on the evening of 12 September (a release without assets at the time of writing). A fork tag of the same name would collide on every fetch, so this one carries `.2`. It is the only fork release of the day.

## What is new in this build

The numbered items are in the [MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/CHANGELOG-stability.md).

**APRS position parser: the decoder finally reads what the encoder writes** (212–216). A drift analysis against the firmware's own wire format ([the analysis](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/aprs-parser-drift-20260911.md)) showed the encoder emits 17 `/X=` position keys and the decoder understood 14. The group list `/R=`, INA226 bus voltage `/U=` and current `/I=` are decoded now (212); the `/Y=` scan no longer inherits digits from the preceding `/V=` value (213); the eight NaN guards in the encoder test their own buffer instead of the pressure buffer, so `/H=nan`, `/T=nan` and five more can no longer leave the node (214); the full 17-key grammar is written down as a contract (215); and the `#name` suffix the encoder appends to the position comment is split off into its own field, with `--setname` refusing `#` so the split stays unambiguous (216). The same `#name` rule is implemented in MCProxy and the mobile app.

**Web GUI: the group stays in the destination field after a send** (217). Selecting a group tab writes the group number into the destination field, but the send-ok handler cleared it after every send, so the next message typed in a hurry went out as a broadcast to `*`. Reported by DJ8MEH. A numeric destination is left alone now; a DM call sign is still cleared.

**Three field fixes against `4.35t`** (218–220), all in upstream `dev` since PR #1140:

- **Long press switches the node off again** (218). Since the deep-sleep rework the user button is armed as wake source, but the long-press handler fires while the button is still held — the wake condition was already true and the node rebooted at once. The firmware now waits for the release (bounded 10 s, then 100 ms debounce) before arming, on every long-press-to-sleep board, ESP32 and nRF52, and only when `--button` is on. Serial `--deepsleep` with the button untouched is unaffected.
- **T-Deck keyboard light stays dark while the keylock is engaged** (219). The display wake path tested the keylock flag instead of the keyboard-light setting and forced the keyboard to 150 whenever a message woke the locked panel.
- **T-Beam Supreme boots again** (220). The `4.35t` OLED change gave u8g2 the I2C pins explicitly; u8g2 then reconfigures both pins as plain outputs, detaching them from the I2C controller that already owns them, and the display init hangs after `Auto detecting display:`. Two field nodes were stuck there. Constructors take no pins now and the bus runs at 100 kHz like the sensor path on the same bus.

**Upstream sync** (221). Three plain merges: Kurt's own move to `4.35t`, OE1KFR's "RAK LEDs off in Deepsleep" (the green and blue LEDs of a RAK4631 are driven LOW before System OFF, so a sleeping node no longer shows a lit LED), and upstream's merge of PR #1140. `fork-main` is content-identical to upstream `dev` at `1cb2d9e6` except for items 212–217, the fork-only pieces named above and `FLASH_VERSION`.

**Field confirmation for item 200.** The Wireless Paper / Vision Master E213 chip-select release on wake was a blind fix in `v4.35t.09.10`. OE3LCR tested it on both boards on 11 September: EXT1 wake, `RESET_REASON=8`, SX1262 init and SPI traffic fine afterwards. OE3LCR also notes that pre-fix `4.35p` woke with working RX on the same boards, so the call is most likely a guard rather than a repair; it stays.

## Already in official `4.35t`

For orientation only — everything below is in the official firmware since 10 September and is described in the changelog: input hardening and web security (107–118); back-pressure honesty (156–167, 179); WiFi, NTP, Ethernet and gateway path (119–128, 131, 176); ACK attribution toward the phone (192, 193); deep sleep made real (197–200); GPS and altitude (174, 204–206); web GUI and web API (134–136, 189, 190, 194–196); T-Deck (140–147, 175, 181–185, 199, 201); telemetry on the air (187, 207); build, test and tooling (150, 152, 164, 166, 169, 170, 191, 202, 208); text and operator warnings (209, 210); the previous upstream sync (211).

## What changes on the air

Compared with official `4.35t`, two things:

1. **A NaN can no longer leave the node in a position beacon** (item 214). Seven of the eight encoder guards were comparing the wrong buffer; a sensor returning NaN could put `/H=nan` and friends on the air. Frames with valid readings are byte-identical to before.
2. **`--setname` drops `#` from the node name** (item 216). Nothing else about the frame changes; the name suffix was always there, the decoder now reads it.

Everything else on the air — `XX0XXX` frames dropped (111), the 30-second shot-path floor (132), `/D=` for MCP23017 nodes (207), CP1252 pass-through (210), the ACK call signs toward the app (192, 193) — is official behaviour now.

## Debug logs from a node in the field

**What ships in a normal board build:**

- **Markers**: compact `[TAG];key;value` lines — `[WIFI]`, `[ETH]`, `[GW]`, `[UDP]`, `[NTP]`, `[KBD]`, `[SAFEBOOT]` and more. High-rate candidates are rate-limited or bound to their own switch.
- **Log switches**: `--setlog on` (the per-message line set), `--gpsdebug on` (fix/position/reject/convergence every 3 s), `--debug on` (verbose), `--loradebug on` (RX/TX, dedup, TX-ring), and — on a gateway — `--udplog on/off`, `--udpstat`, `--wifistat` (ESP32) and `--ethstat` (nRF52).
- **Where to capture it**: the USB serial console, or the **network console on TCP port 2323** (ESP32 boards), which serves the same log over WiFi so the node can stay in place: `nc <node-ip> 2323`, or long-term with `tools/meshlogger.py` from this repository. Note for ESP32-S3 boards on native USB (T-Deck, T-Beam Supreme): output written before the port was opened is dropped after the first ~256 bytes, so a log that appears to start at boot may not — open the terminal first, then reboot.

**What is not in a normal build** (item 202): the bench and measurement surface — `[INSTR-LOOP]` loop-gap markers, `--instr`, `--heap`, `--injectmsg`, `--injectraw`, `--loratx`, `--ntpsync`, `--flashpoke`, `--disptest`, `--spitrace` and the T-Deck UI hooks. If you need them, build a measurement firmware: `PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1" pio run -e <env>`.

If you capture a log of misbehavior in the field, open an issue with the log attached — the markers are designed to be evaluated.

## Deep sleep: what is in, what is not, where we need your help

Upstream issue #962 asked why the low-battery deep sleep does nothing. The verdict is in [the linked document](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/archive/issue-962-deepsleep-verdict.md): it is not misbehaving, it was switched off entirely for issue #1053 (`e0043a56`, 4.35p.07.11), on every board, and the manual `--deepsleep` command never really slept either.

**What the firmware does** (items 197–200, 218, 221):

- `--deepsleep` is a real sleep on **every ESP32 board**: radio to sleep, display off, PMU rails off on the T-Beam family, the shared rail cut on the T-Deck and T-Deck Plus, a button wake armed on the configured button pin. Wake is the user button or RESET.
- `--deepsleep` on **RAK4631, Heltec T114 and T-Echo** is a real nRF52 System OFF (the T-Echo long-press too). Wake is the button, plugging in USB, or RESET. The RAK4631's LEDs are off while it sleeps (221).
- **The long press works again** (218): hold the button until the display goes dark, release, and the node stays asleep; the next press wakes it. With `--button off` the long press is not available and the firmware does not touch the pin.
- Boards wake up **with their rails and radio working**: the T-Deck, T-Deck Plus and T-Beam-1W left a gpio hold latched across the wake reset (199), and the same mechanism on Wireless Paper and Vision Master E213 is fixed and field-confirmed (200).

**What the firmware deliberately does not do:**

- **No automatic low-voltage shutdown.** Issue 962 Option B (a timer-wake loop with hysteresis, opt-in) is designed but not implemented. The old guard stays commented out as upstream left it.
- **No light sleep.** Option C (light sleep with LoRa wake) is advised against in the verdict and was not attempted.
- **No sleep-current figures.** We have no bench supply, no discharged packs and no inline current meter on the desk. The "tens of microamps" and "about 2 µA" numbers are datasheet and core figures, not measurements.

**We rely on the community for these tests.** If you own one of the boards below, please put it to sleep with `--deepsleep` or the long press, wake it, and post the boot log's reset-reason line and whether LoRa, display and GPS came back — in the issue 962 thread or as an issue on this repository. Two hints from OE3LCR's test: RST is a power-on reset (`wake: 0`), not a deep-sleep wake, so only the PRG/user button proves anything; on boards with a CP2102 USB bridge, pulsing DTR after `--deepsleep` wakes the node without a hand on it.

- **Vision Master E290**: the e-ink keeps drawing its last frame through sleep; we have no unit to verify a clear.
- **T-Beam Supreme** (AXP2101 rail cut, compile-verified only), **T-Beam SX1262 / SX1268** (AXP192 rails), **T-Beam-1W** (radio LDO).
- **Heltec T114 and T-Echo** (System OFF and the long-press release wait, compile-verified only; the RAK4631 is the only nRF52 board on our bench).
- **E22-DevKitC, E22_XML-DevKitC, ttgo-lora32-v21**: the button wake pin has no external pull-up on these; the firmware keeps the internal one alive through sleep, which we could not test on those boards.
- **Anyone with a meter**: sleep current on the battery lead, on any board.

## Changelog and engineering rationale

- **[MeshCom Stability Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/CHANGELOG-stability.md)** — the numbered list; items 212–221 are new since `v4.35t.09.10`, items 104–210 are in official `4.35t`.
- **[The three field fixes as offered upstream](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/pr-deepsleep-keylock-draft-20260912.md)** — items 218–220 with file references and the bench evidence (German), plus the [T-Beam Supreme hang report](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/bug-tbeam-supreme-435t-display-hang.md) and the [Heltec V3 long-press report](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/bugreport-heltec-v3-longpress-deepsleep.md).
- **[APRS parser drift analysis and `/X=` contract](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/aprs-parser-drift-20260911.md)** — items 212–216; the grammar itself is §1.8 of [the wire-format document](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/architecture/11-wire-format.md).
- **[Engineering write-up of the back-pressure campaign](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/archive/pr-draft-20260831.md)** — items 107–169, with per-change file references and measurements.
- **[Issue 962 deep sleep verdict and plan](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/archive/issue-962-deepsleep-verdict.md)** and **[gpio-hold and HWCDC follow-ups](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/gpio-hold-and-hwcdc-followups.md)**.
- **[ACK attribution plan](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/ack-implementierungsplan.md)** and **[the gateway heard-frame fix](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.12.2/docs/archive/ack-heard-foreign-msgids-fix.md)** — items 192 and 193 (German).
- [MeshCom@ICSSW project page](https://icssw.org/en/meshcom/)

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean**; 673 native test cases green across 12 host environments (17 new since `v4.35t.09.10`: the `/R= /U= /I=` decode, the `/Y=` reset, the per-buffer NaN guards and the `#name` split).
- **No board has run the published image itself.** Three boards ran the same source at earlier commits of this cycle; the release image differs from those builds only in `FLASH_VERSION`.
- **Heltec V3 (DK5EN-93, bench, flashed 12 September with this cycle's code)**: serial `--deepsleep` sleeps at once, no reboot within 15 s; long-press with `--button on`, two cycles — dark, staying dark after the release, wake on the next press with `RESET_REASON=8 DEEPSLEEP` (item 218).
- **Heltec V3 gateway (DK5EN-98, bench, WiFi OTA 11 September)**: item 217 checked with the jsdom harness against the live node — group kept, DM call sign cleared; the check fails on the previous firmware.
- **T-Deck Plus (DK5EN-14, bench, instrument build)**: harness scenario `keylock_kbl` — the old code writes 150 to the keyboard light on a message wake, the new code writes nothing; hand test on 12 September: keyboard light off, keylock on, a LoRa message wakes the display and the keyboard stays dark (item 219). The device runs an `INSTRUMENT_ENABLED=1` image, not the release image.
- **Wireless Paper V1.2 and Vision Master E213 (OE3LCR-10 and -11, field)**: item 200 confirmed by OE3LCR on upstream `dev` after #1137 — the same code path, not this image.
- **WisBlock RAK4631 (DK5EN-90)** and **T-Beam v1.2 (DK5EN-92)**: no bench time this cycle. The RAK's deep-sleep path changed (218, 221) and was not re-run; see the known gaps.

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

- **The T-Beam Supreme fix (item 220) is compile-verified only.** No Supreme on our bench; the two field nodes that hung have not yet reported back with the fix build.
- **The APRS decoder repairs (items 212–216) are proven by the native suite only.** No board has received a real position with `/R= /U= /I=` or a `#name` comment; nobody has eyeballed the result on serial or in the web GUI.
- **The RAK4631 deep-sleep path changed without a re-run.** Item 218 adds the release wait (gated on `--button`), item 221 switches the LEDs off; the bench RAK sits in System OFF and needs its reset button before it can be re-tested. Heltec T114 and T-Echo inherit both changes compile-verified only.
- **The letter `t` is shared with official `4.35t`.** Use the flash stamp in `--info` (`20260912`) to tell this build apart.

Carried over, still open:

- **A strict UTF-8 receiver may start rejecting frames it used to accept** (item 210, now official). A consumer on the EXTUDP sideband that decodes strictly will drop frames with CP1252 bytes instead of showing a mangled word. mc-chat handles it; other consumers have not been checked (CHR-03).
- **Five bytes that are undefined even in CP1252** (`0x81`, `0x8D`, `0x8F`, `0x90`, `0x9D`) arrive at the far end as `U+FFFD` (item 210).
- **The GPS/barometer fusion misses its own design gate.** Target 4 m standard deviation, bench 6.6 m; tau 2 h reaches 3.6 m in replay. The constant is deliberately left at 30 min.
- **No moving-node altitude proof.** TRACK mode bypasses both filters, and a TRACK-mode capture with pressure has not been recorded.
- **The MCP23017 transmit path is proven natively only** (item 207). No MCP23017 on the bench.
- **The bench and injection commands are gone from a normal build** (item 202). The injection machinery itself is still compiled in and costs flash without being reachable.
- **No low-voltage shutdown and no light sleep** (see the deep sleep section).
- **Deep sleep on T-Beam Supreme, the T-Beam SX1262/SX1268 variants, T-Beam-1W, Heltec T114, T-Echo, Vision Master E290 and the E22 DevKitC boards is compile-verified only.** No sleep current was measured on any board.
- **Vision Master E290 keeps its last e-ink frame through sleep.**
- **The unread badge is a lower bound after a long absence** (item 195): 20 ring slots shared with positions and acks, and messages stamped near zero on a node without a valid clock count only within one browser session.
- **The QRS forecast (item 194) has not been eyeballed against a real burst in the browser.**
- **ACK attribution stage 4 (the wire appendix) is not in**; Gateway ACK stays anonymous.
- **TD-15 (filed, not fixed):** after a reboot the T-Deck map shows only stations whose position beacon arrived since boot.
- **MEM-04 (risk, not a defect):** `ttgo_tbeam`, `ttgo_tbeam_SX1262` and `ttgo_tbeam_SX1268` link with about 20 bytes of IRAM headroom, `E22_XML-DevKitC` with under 1 kB of DRAM.
- **The safeboot fail-closed gate (item 186) has no bench arm on a 4 MB board yet** (TM-49).
- **The echo guard's ring-flood case (item 179) is still owed** on the T-Deck bench.
- **`--postime 0` no longer switches position beacons off** — clamped to the 300-second floor, from upstream's own `4.35s` fix (item 177); filed for an upstream PR rather than patched here.
- **The `--setlog` line set has no hardware run yet.**
- **The GPS two-hour comparison arms (A/B/C) have not been run.**

## Installing

Pick the asset for your board. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive. Coming from a build older than `v4.35p.08.22-stability`, check `--info` afterwards.

## Upstream

Everything here is offered back to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) as individual pull requests. Items 1–210 are in official `4.35t`; items 218–220 are in upstream `dev` since 12 September; items 212–217 are queued for a PR once a board has seen them work. Please report bugs that also exist in the official firmware over there.
