---
description: Build MeshCom firmware for Heltec V3, E22, TTGO T-Beam, T-Beam Supreme, T-Deck, T-Deck Plus, and WisBlock RAK4631 and copy to Desktop
allowed-tools: [Bash, Read, Edit, Glob]
---

Build MeshCom firmware using PlatformIO from the project root directory.

## Pre-build: Update FLASH_VERSION

Before building, update `FLASH_VERSION` in `src/configuration_global.h` to today's date in `YYYYMMDD` format.
The define looks like: `#define FLASH_VERSION 20260228`
Update the date to the current day. Do NOT change SOURCE_VERSION or SOURCE_VERSION_SUB.

## Build targets

The 7 standard targets and their PlatformIO environment names:

| Target | Environment |
|--------|-------------|
| Heltec V3 | `heltec_wifi_lora_32_V3` |
| E22 | `E22-DevKitC` |
| T-Beam | `ttgo_tbeam` |
| T-Beam Supreme | `ttgo_tbeam_supreme` |
| T-Deck | `t_deck` |
| T-Deck Plus | `t_deck_plus` |
| WisBlock RAK4631 | `wiscore_rak4631` |

## Build command

Use `pio run -e <environment>` from the project root. The root `platformio.ini` includes all variant configs via `extra_configs`.

Run the 7 targets **sequentially** (one `pio run` at a time). Parallel `pio run` invocations
corrupt `.pio/build` archives and wipe each other's outputs (memory note `pio-build-cache-race`,
docs/code-quality-2.0.md Part C). Check `pgrep -fl "pio run"` first; another session may be building.
```
pio run -e heltec_wifi_lora_32_V3 2>&1 | tail -5
pio run -e E22-DevKitC 2>&1 | tail -5
pio run -e ttgo_tbeam 2>&1 | tail -5
pio run -e ttgo_tbeam_supreme 2>&1 | tail -5
pio run -e t_deck 2>&1 | tail -5
pio run -e t_deck_plus 2>&1 | tail -5
pio run -e wiscore_rak4631 2>&1 | tail -5
```

## User arguments

- `--only heltec` / `--only e22` / `--only tbeam` / `--only supreme` / `--only tdeck` / `--only tdeck-plus` / `--only rak` to build a single target
- No arguments = build all 7 targets

## Post-build

1. Copy firmware binaries to `~/Desktop/MeshCom-Firmware/`:
   - `.pio/build/heltec_wifi_lora_32_V3/firmware.bin` -> `firmware_heltec_v3.bin`
   - `.pio/build/E22-DevKitC/firmware.bin` -> `firmware_e22.bin`
   - `.pio/build/ttgo_tbeam/firmware.bin` -> `firmware_tbeam.bin`
   - `.pio/build/ttgo_tbeam_supreme/firmware.bin` -> `firmware_tbeam_supreme.bin`
   - `.pio/build/t_deck/firmware.bin` -> `firmware_t_deck.bin`
   - `.pio/build/t_deck_plus/firmware.bin` -> `firmware_t_deck_plus.bin`
   - `.pio/build/wiscore_rak4631/firmware.hex` -> `firmware_rak4631.hex`
     (nRF52 produces `.hex`; if `.bin` exists instead, copy that as `firmware_rak4631.bin`)
2. Report which targets built successfully
3. Show the file sizes of the firmware binaries on the Desktop
4. If any build failed, show the relevant error output
