# MeshCom Firmware - Development Guidelines

## Project Context

Open-source MeshCom Firmware. We contribute via PRs against the **upstream DEV branch** of icssw-org/MeshCom-Firmware.

## Start Here

- `docs/RESUME.md`: current state, last release, open items. Read it first in a fresh session.
- `docs/BACKLOG.md`: open work and operator decisions. `docs/architecture/08-defect-catalogue.md`: defect IDs.
- Campaign docs (`docs/campaign-*.md`) are the resume points of multi-wave work; keep them and `RESUME.md` current per wave.

## Branch Model

- `fork-dev` is the only working branch: `upstream/dev` plus the fork's docs, tests, tools, GitHub Pages **and fork-only firmware** (`src/`, `platformio.ini`, `variants/`) that is not upstream yet. New firmware lands on `fork-dev` first; upstream PRs are cut from `upstream/dev` later.
- `fork-main`, `fork-neo`, `fork-neo-test` and `master` are retired (tag `archive/fork-neo-test-20261002`). Do not build on them.
- Sync upstream by cherry-pick; never merge fork history into upstream.
- After icssw-org squash-merges a PR of ours, **merge** `upstream/dev` (not rebase) to re-sync.
- All PRs target upstream `dev`, with the description in German. DK5EN never merges upstream himself; the maintainers review and merge.
- Temporary fork commit `389ace0d` (OE Internet server lost in upstream #1209): revert it once the upstream fix lands.

## Releases

- `/release-firmware` is the recipe. CI builds and publishes since `v4.40a.10.11`: `MeshCom CI` (`.github/workflows/meshcom-ci.yml`) fires on `v*` tags (release, Latest) and `ci-proof-*` tags (draft; delete draft and tag afterwards). `ci-build.yml` was removed 2026-10-17 (disabled since 09-20; history in git).
- Every `gh` call needs `-R DK5EN/MeshCom-Firmware` (no `gh` default repo is set; with two remotes it may pick upstream).

## PR Workflow

1. **Sync upstream first.** Before any coding, sync against the latest upstream DEV branch.
2. **Branch from `upstream/dev`**, not from `fork-dev`. Port the minimum out of `fork-dev`. No rewrites, no large refactors. Every change is targeted and justified.
3. **PR description in German, detailed**, prepared before submitting:
   - exactly which code changed (files, functions, logic)
   - why each change was made (motivation, bug fix rationale, improvement reason)
4. **Target:** the **DEV branch** of the upstream repository (not main).

The `/submit-pr` command walks this flow; `/sync-upstream` brings upstream into `fork-dev` (merge or cherry-pick, never rebase).

## Testing and Gates

- Host gate: `tools/regression.sh --stage 1,2` (all `[env:native*]` Unity envs, golden selftest, pytest and tool suites).
- With bench hardware: `tools/regression.sh --stage all` (adds stage 3, `tools/bench/bench_suite.py`; `--flash`, `--extudp`, `--deepsleep` are opt-in, passed after `--`).
- The `/full-regression` skill wraps the runner (`.claude/commands/full-regression.md`, tracked).
- **Never run bare `pio test`**: it walks the board envs and flashes attached hardware.
- **One `pio` process at a time** (shared build cache and global package store; `selftest.sh` shells out to `pio project config`). Parallel writers route every `pio` call through a mkdir-lock wrapper (`piolock.sh` in the scratchpad).
- Suite inventory: `docs/test-suite-map.md`.

## Toolchain Pitfalls

- nRF52 builds fail with make "Error 126" / "Bad CPU type": the pinned gcc-arm is x86_64 and Rosetta is missing. Run `softwareupdate --install-rosetta --agree-to-license`; do not bump the toolchain.
- Harness and bench logs go to `tools/bench/runs/harness/`, never the repo root.
- Raw `*.log` there is gitignored; the summaries (`.json`, `.jsonl`, `.summary`) are not. A wave's commit includes the
  summaries its docs cite and deletes the rest, so `git status` is clean after every wave.
- ESP32 app builds are not byte-reproducible; compare RAM/flash regions, not image hashes.

## Hardware and Flashing

### Bench fleet

Ports move between sessions. **Resolve the port by USB serial number** (`ioreg` or pyserial), never by device name. Registry: `tools/bench/fleet.json` (USB serials and last seen hosts).

| Node     | Board       | USB serial              | Net console 2323         |
| -------- | ----------- | ----------------------- | ------------------------ |
| DK5EN-90 | RAK4631     | 230D6EBB3266D20E        | none (nRF52, Ethernet)   |
| DK5EN-14 | T-Deck Plus | E0:72:A1:AD:65:E0 (MAC) | yes (`dk5en-14.local`)   |
| DK5EN-92 | T-Beam v1.2 | 573C000584              | yes                      |
| DK5EN-1  | Heltec V3   | 0001                    | yes (off unless enabled) |

DK5EN-98 is the production Heltec V3 (gateway, logger on rpizero); it is not a bench node.

**All addresses come from DHCP and change between sessions.** Never trust `fleet.json` hosts blindly:
confirm the callsign on the node's start page before any flash or test (or sweep the /24 for start
pages). Every 2323 client logs in through `tools/mc_console.py`: the password is `MC_PW_<CALL>` in the
gitignored, mode-600 `.env` at the repo root (template `.env.example`), picked by callsign (`--call`, a fleet.json
node, or a `dk5en-NN.local` host). Never put a password on a command line. Bench tests run over the 2323 console,
not USB (opening USB resets an ESP32).

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
- WiFi OTA: `tools/webflash.py`.

## Bench Rules (hard)

- Test traffic only to group `TEST` or as a direct message to an own node. Never broadcast.
- Never use a foreign callsign (never OE1XAR, never XX0XXX). Every callsign SSID is unique.
- Bench TX power at most 2 dBm.
- Run `tools/bench/identity_guard.py` before any test.
- Run `lsof <port>` before opening a port; no port open while `serial_capture.py` holds it (also Chrome Web Serial).
- Opening a serial port reboots every ESP32. The RAK4631 needs DTR to talk.
- To take a node off the air without powering it down, use `--airgap on` (INSTRUMENT builds, `docs/bench-airgap.md`).
