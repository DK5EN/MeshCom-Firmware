# Bug report: Web flasher does not auto-detect T-Beam 1W (and other native-USB ESP32-S3 boards)

- **Date:** 2026-10-18
- **Component:** MeshCom Web Flasher (`https://dk5en.github.io/MeshCom-Firmware/flash/`), firmware
  board identity
- **Hosted version:** `origin/gh-pages` 0fe93d83, byte-identical to `pages/flash/` on fork-dev
- **Release:** v4.40a.10.18
- **Reporter:** field user via Signal (screenshot), board LilyGO T-Beam 1W (ESP32-S3, native USB)
- **Review:** Fable review, 6 finders + 3 adversarial verifiers; every finding below independently
  verified against source, release images or published gh-pages files. No hardware reproduction (no
  native-USB S3 board on the bench at the time).

## Summary

"Board erkennen" on a T-Beam 1W showed `Knoten: keine MeshCom-Antwort auf --info`, `Chip: ESP32-S3`
and 17 candidate boards. The board answered nothing the flasher could parse. On native USB the flasher
has exactly one identification channel (`--info`), discards whatever it received, never resets the
board and cannot tell "Safeboot", "ROM download mode", "foreign firmware" and "hung app" apart. At the
same time the firmware already reports its exact build env (`...AU env=<env>` in `--info`, env string
in the flash image), which the flasher never uses.

Side finding: the published T-Deck Pro web-flash manifest writes Safeboot into the app partition; a
web-flashed T-Deck Pro does not boot MeshCom.

## Observed

| Field    | Value                                                                      |
| -------- | -------------------------------------------------------------------------- |
| Knoten   | keine MeshCom-Antwort auf --info                                           |
| Chip     | ESP32-S3                                                                   |
| Funkchip | (no line)                                                                  |
| Verdict  | 17 Boards passen (Chip ESP32-S3) — the full set of S3 envs in v4.40a.10.18 |

17 (not 16 SX1262 or 1) proves the page parsed neither a `NODE` line nor a radio line: it received
nothing usable.

## Expected

The page identifies `LilyGo_T-Beam-1W`, or, if the board cannot answer, says why (Safeboot, ROM,
foreign firmware) and offers a way out.

## How detection works today (trace)

1. `usbKind()` (`detect.js:23`): VID 0x303a -> `esp-native`; `familyFromUsb()` -> "ESP32-S3".
2. `listen()` (`flasher.js:529-590`), native branch: `setSignals({DTR:1, RTS:0})`, no reset; sends
   `--info\n` every 2 s for 20 s.
3. `parseInfo()` (`detect.js:38`) looks only for `...NODE <hwid> <name>`; `parseBoot()` for the ROM
   banner and `[LoRa]...<chip> ... Initializing ... success|failed`.
4. `candidates()` (`detect.js:117`) filters by hwid, else by chip family, then by radio.

On native USB step 3 can only ever see the `--info` reply: the page does not reset the board, so the
boot log (radio line) was printed long before the port opened.

## How a board can identify itself

| Channel                                | Works without running app | Names the exact env                     | Used by flasher        |
| -------------------------------------- | ------------------------- | --------------------------------------- | ---------------------- |
| `--info` reply over serial             | no                        | yes on fork builds (`...AU env=<env>`)  | only `NODE <hwid>`     |
| App image on flash (ROM bootloader)    | yes                       | yes (env string; `.pio/libdeps/<env>/`) | no                     |
| Chip facts (ROM: flash/PSRAM, USB VID) | yes                       | no (1W in a group of 8-10 boards)       | only VID -> "ESP32-S3" |
| Boot log radio/PMU line                | needs reset               | no (radio class only)                   | bridge boards only     |

Without MeshCom on the board (LilyGO factory, Meshtastic) only the chip group is knowable; manual
choice remains.

## What the firmware carries today

- `MC_ENV_NAME` = PlatformIO env, set for every env by `tools/mc_build_defines.py`
  (`platformio.ini:144`). Printed by `--info` as `...AU env=<env> tag=<tag>`
  (`src/command_functions.cpp:7114`, ESP32 only) and on the web start page
  (`src/web_functions/web_functions.cpp:4121`). **Fork only**; upstream/dev has neither.
- Env string in every fork ESP32 image within the first 32 KB, directly after the build tag (1W
  0x6b00, Heltec V3 0x696a, T-Deck 0x6ef8). Not in the RAK uf2.
- `.pio/libdeps/<env>/` paths 11-13 times per image, one env each, also in upstream images (from
  ~0x24000; T-Deck ~0x13000).
- `esp_app_desc_t.project_name` = `arduino-lib-builder` in every image: useless.
- Hardware ID is numeric and shared: 12 (three T-Beam images), 39 (E22 classic), 48 (E22 S3).
- No marker guarantees which env string is the image's own: `fwAssetBase()` (`src/fw_update_assets.h`)
  puts `LilyGo_T-Beam-1W`, `LilyGo_T3_S3_V1_3`, `LilyGo_T_Connect_Pro` into every image.

## Findings

### F1 — Native-USB board that does not answer `--info` is undiagnosable (high)

- **Where:** `pages/flash/flasher.js:529-590` (`listen`), `:700-745` (result handling)
- **Failure:** a native S3 board in Safeboot (no command parser, prints only at boot;
  `src/safeboot/*`), in ROM download mode, running foreign firmware or with a hung loop yields exactly
  the reported screen. `heard.text` is never displayed or logged; `listen()` never resets a native
  board, so the page cannot get it out of the ROM or capture its boot log.
- **Likely concrete cause here:** Safeboot. Field 1W OE5HWN-17 stayed in Safeboot after Auto Update
  on 2026-10-16 and 10-17 (BACKLOG AU-16, closed without root cause). Not proven to be the reporter's
  board — ask the reporter.
- **Fix:** show byte count + last ~2 KB received (collapsible) and log it to the console; recognise
  `[SAFEBOOT];` lines and say "Board steht in Safeboot"; on native silence offer "Neustart in die App"
  (esptool HardReset: DTR0, RTS1 200 ms, RTS0; re-acquire the port after re-enumeration) and listen
  again, which also brings the boot log with the radio line.

### F2 — Flasher ignores the env name the node already reports (medium)

- **Where:** `pages/flash/detect.js:38` (`parseInfo`), `:117` (`candidates`)
- **Failure:** fork 4.40a nodes answer `...AU env=LilyGo_T-Beam-1W`; the page keys on hwid only, so
  shared IDs (12, 39, 48) still leave several candidates.
- **Fix:** parse `\.\.\.AU env=(\S+) tag=`; if the env is in the selected release's `detect.json`,
  use it as the single candidate; fall back to hwid + radio line, then chip family.

### F3 — `listen()` arms the S3 download flag and keeps it armed (medium)

- **Where:** `pages/flash/flasher.js:563`, `heldPort` handoff
- **Failure:** on the S3 USB-Serial/JTAG, DTR1/RTS0 sets the "download mode at next reset" flag (TRM
  table: RTS0/DTR0 clear, RTS0/DTR1 set, RTS1/DTR0 reset, RTS1/DTR1 no-op). Any reset while the page
  holds the port (RST button, brownout, watchdog) boots the ROM, which never answers `--info`. macOS
  clears it on close (HUPCL); Windows `usbser.sys` behaviour unverified.
- **Fix:** after listening set DTR0/RTS0 (clears the flag, no reset since RTS stays 0) before holding
  the port.

### F4 — No firmware-independent identity from flash (feature)

- **Where:** `pages/flash/flasher.js` (new), firmware marker (new)
- **Failure:** a board whose app crashes or sits in Safeboot cannot answer, although its image on
  flash names the env.
- **Fix (flasher):** in the ROM session read the partition table at 0x8000, then the first 32 KB of
  ota_0 (0xC0000) and match the marker. The vendored ESP Web Tools bundle does not export `ESPLoader`
  (minified as `Es`, reachable only via `window.esploader`): vendor esptool-js instead.
- **Fix (firmware):** one tagged constant `MCBOARD:v1:<hwid>:<env>` kept in the image, and `--id`
  printing the same line on ESP32 and nRF52 (today `--info`'s env line is ESP32-only). Needs
  `mc_build_defines.py` + its `platformio.ini` hook in the upstream PR, since upstream lacks both.

### F5 — T-Deck Pro web flash writes Safeboot into app0 (high, side finding)

- **Where:** `tools/pages_flasher.py:357-364` (manifest fallback), `variants/t_deck_pro/platformio.ini:58`
  (`board_build.partitions` commented out, env does not extend `[esp32]`)
- **Failure:** published `flash/v4.40a.10.18/t_deck_pro/partitions.bin` is `default_16MB`
  (nvs 0x9000, otadata 0xE000, app0 0x10000 size 0x640000, app1 0x650000, spiffs, coredump; no
  safeboot). The manifest writes `safeboot-s3.bin` at 0x10000 and `firmware.bin` at 0xC0000. The
  bootloader boots Safeboot as app0; the MeshCom app lies inside app0 and never runs. Verified on the
  published files, not on hardware.
- **Fix:** give t_deck_pro the 16 MB safeboot table, or derive manifest offsets from the env's actual
  partition table in `pages_flasher.py` and fail the build on a mismatch.

### F6 — Detection tests miss the native-USB and silent cases (medium)

- **Where:** `tools/tests/test_flasher_detect.mjs`
- **Failure:** fixtures are Heltec V3, RAK, T-Beam v1.2 only (bridge / nRF). No native S3 `--info`, no
  Safeboot banner, no foreign firmware, no `candidates()` test with usb family ESP32-S3; `listen()` /
  `detect()` I/O untested; the hwid table check cannot fail (it reads the same `configuration.h`). A
  dead radio on a single-candidate board filters to "Kein Board passt" (untested); radio-ok with no
  match (LLCC68) falls through silently.
- **Fix:** add fixtures from `logs/oe5hwn/` (T-Beam Supreme, T-Deck, T-Deck Plus parse correctly), a
  Safeboot capture, an `env=` line, and the empty-pool cases.

### F7 — Misleading comments (low)

- `detect.js:15-16`: "native USB stays mute until DTR is asserted" — true for nRF52, false for S3
  HWCDC, which ignores DTR.
- `flasher.js:560-562`: "RTS resets the chip" — only with DTR=0.

## Corrections to the first RCA

- 1W prints `[LoRa]...SX1262 V3 chip` (`src/esp32/esp32_main.cpp:1856`), not `SX1262 chip`; the
  regex still matches.
- "Chip: ESP32-S3" can come from the VID or the ROM banner; the page cannot say which.
- T-Deck / T-Deck Plus run arduino-esp32 2.0.14, other S3 envs 2.0.17; no effect on this bug.

## Refuted claims (do not re-investigate)

- "Safeboot on S3 is mute on USB" — `SafebootTee` (`src/safeboot/safeboot_log.h`, a59b74d5) mirrors
  output to USB; PuTTY captured OE5HWN-17 Safeboot over native USB. It is silent after boot only.
- "Opening the port in Chrome resets a native S3 (rst:0x15)" — the rst:0x15 came from pyserial
  clearing DTR before RTS (`tools/bench/tdeck_cdc_portopen.py:87-92`); Chromium never touches the
  lines on open and sets before clearing in `setSignals`. Windows inferred only.
- "The 1W has a USB-UART bridge" — LilyGO lists GPIO43/44 as free; board JSON 303a:1001.
- "`esp_app_desc_t` identifies the board" — `arduino-lib-builder` in every image.
- "1W firmware console is not on USB" — release image links HWCDC; `CDC_ON_BOOT=1` via
  `boards/esp32-s3-wroom-1-n16r8.json`, no override.
- "Pin / I2C probing from the ROM can identify boards" — rejected: blind pin driving (1W PA, rails),
  peripherals unpowered in download mode.

## Open questions for the reporter

1. What was running on the board: MeshCom (version?), LilyGO factory firmware, Meshtastic?
2. Exact status line above the result ("Keine Antwort ..." or "Die Verbindung ist abgerissen ...").
3. Is it OE5HWN-17? Is the `MeshCom-OTA` access point (192.168.4.1) visible, i.e. Safeboot?
4. OS and browser (screenshot looks like Windows).

## Proposed feature request: board auto-detect

- **Flasher:** F1 (show received text, Safeboot recognition, reset-into-app), F2 (parse `env=`), F3
  (clear download flag), F4 (read env marker from flash in the ROM session), chip facts only as a
  safety filter (never offer a 16 MB image to a 4 MB chip) and to pre-filter manual choice.
- **Firmware:** `MCBOARD:v1:<hwid>:<env>` marker + `--id` command on ESP32 and nRF52; upstream PR
  carries `mc_build_defines.py` and its hook.
- **Tests:** F6.
- **Separate fix:** F5 (T-Deck Pro manifest).
