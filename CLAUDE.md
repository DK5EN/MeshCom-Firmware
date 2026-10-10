# MeshCom Firmware - Development Guidelines

## Project Context

Open-source MeshCom Firmware. We contribute via PRs against the **upstream DEV branch** of icssw-org/MeshCom-Firmware.

## Branch Model

- `fork-dev` is the only working branch: `upstream/dev` plus one port commit carrying fork-only docs, tests, tools and GitHub Pages.
- `fork-main`, `fork-neo`, `fork-neo-test` and `master` are retired (tag `archive/fork-neo-test-20261002`). Do not build on them.
- Sync upstream by cherry-pick; never merge fork history into upstream.
- After icssw-org squash-merges a PR of ours, **merge** (not rebase) to re-sync.
- All PRs target upstream `dev`, with the description in German. DK5EN never merges upstream himself; the maintainers review and merge.
- Releases are manual (`/release-firmware`); both GitHub Actions workflows are disabled.

## PR Workflow

1. **Sync upstream first.** Before any coding, sync against the latest upstream DEV branch.
2. **Minimal changes only.** Cherry-pick the absolute minimum. No rewrites, no large refactors. Every change is targeted and justified.
3. **PR description in German, detailed**, prepared before submitting:
   - exactly which code changed (files, functions, logic)
   - why each change was made (motivation, bug fix rationale, improvement reason)
4. **Target:** the **DEV branch** of the upstream repository (not main).

## Testing and Gates

- Host gate: `tools/regression.sh --stage 1,2` (all `[env:native*]` Unity envs, golden selftest, pytest and tool suites).
- With bench hardware: `tools/regression.sh --stage all` (adds stage 3, `tools/bench/bench_suite.py`; `--flash`, `--extudp`, `--deepsleep` are opt-in).
- The `/full-regression` skill wraps the runner (local only, `.claude/commands/` is gitignored).
- **Never run bare `pio test`**: it walks the board envs and flashes attached hardware.
- **One `pio` process at a time** (shared build cache; `selftest.sh` shells out to `pio project config`).
- Suite inventory: `docs/test-suite-map.md`.
- Open points of the regression work: `~/Desktop/regression-offene-punkte.md` (outside the repo).

## Hardware and Flashing

### Bench fleet

Ports move between sessions. **Resolve the port by USB serial number** (`ioreg` or pyserial), never by device name. Registry: `tools/bench/fleet.json`.

| Node     | Board       | USB serial        | Net console 2323 | Last seen IP (2026-10-10) |
| -------- | ----------- | ----------------- | ---------------- | ------------------------- |
| DK5EN-90 | RAK4631     | 230D6EBB3266D20E  | none (nRF52)     | 192.168.68.71 (Ethernet)  |
| DK5EN-14 | T-Deck Plus | none (native USB) | none             | none                      |
| DK5EN-92 | T-Beam v1.2 | 573C000584        | yes              | 192.168.68.69             |
| DK5EN-1  | Heltec V3   | 0001              | yes              | 192.168.68.63             |

DK5EN-98 is the production Heltec V3 (logger on rpizero, last seen at 192.168.68.62); it is not a bench node.

**All addresses come from DHCP and change between sessions.** Never trust the IP column or `fleet.json`
blindly: confirm the callsign on the node's start page before any flash or test (or sweep the /24 for
start pages). Consoles with a password need the HMAC login; `identity_guard.py` reads it from
`MC_CONSOLE_PW`.

### RAK4631 (nRF52840)

- **Bootloader:** WisBlock RAK4631 UF2 Bootloader v0.4.2, SoftDevice S140 6.1.1
- **Build:** `pio run -e wiscore_rak4631`
- **Flash method (UF2):**
  1. Double-tap the reset button to enter UF2 bootloader mode (volume `RAK4631` appears under `/Volumes/`)
  2. Convert hex to UF2: `python3 ~/.platformio/packages/framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py .pio/build/wiscore_rak4631/firmware.hex -c -f 0xADA52840 -o .pio/build/wiscore_rak4631/firmware.uf2`
  3. Copy UF2 to volume: `cp .pio/build/wiscore_rak4631/firmware.uf2 /Volumes/RAK4631/`
  4. Device reboots automatically after flashing (macOS may show an I/O error, this is cosmetic)
- **Flash method (PlatformIO):** `pio run -e wiscore_rak4631 --target upload` uses `adafruit-nrfutil` DFU serial; the device must be running (not in UF2 mode).
- **Web GUI: no mDNS, reach it by IP.** `dk5en-90.local` does not resolve, by design: the mDNS responder is ESP32-only (`ESPmDNS.h`, `MDNS.begin()` in `src/web_functions/`), and the nRF52 `RAK13800_W5100S` Ethernet path ships none. Get the DHCP address from the boot log (`Ethernet.localIP():`) or the router.

### ESP32 boards (Heltec V3, T-Beam, T-Deck, etc.)

- Flash via `esptool` using custom `upload_command` defined in each variant's `platformio.ini`
- Use `pio run -e <env> --target upload`
- T-Beam flashes at 460800 (921600 fails).

## Bench Rules (hard)

- Test traffic only to group `TEST` or as a direct message to an own node. Never broadcast.
- Never use a foreign callsign (never OE1XAR, never XX0XXX). Every callsign SSID is unique.
- Bench TX power at most 2 dBm.
- Run `tools/bench/identity_guard.py` before any test.
- Run `lsof <port>` before opening a port; no port open while `serial_capture.py` holds it (also Chrome Web Serial).
- Opening a serial port reboots every ESP32. The RAK4631 needs DTR to talk.
