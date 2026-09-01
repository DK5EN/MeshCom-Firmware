# MeshCom Firmware - Development Guidelines

## Project Context

This is an open-source project (MeshCom Firmware). We contribute via PRs against the **upstream DEV branch** of the icssw-org repository.

## PR Workflow

### 1. Sync Upstream First

Before any coding, always sync/rebase against the latest upstream DEV branch to incorporate all upstream changes.

### 2. Minimal Changes Only

We **cherry-pick the absolute minimum** of code changes. We do NOT rewrite or refactor large parts of the project. Every change must be targeted and justified.

### 3. PR Description (German, Detailed)

Every PR **must** include a detailed description written in **German**:

- Describe exactly which code was changed (files, functions, logic)
- Explain **why** each change was made (motivation, bug fix rationale, improvement reason)
- This description must be prepared **before** submitting the PR

### 4. PR Target

All PRs target the **DEV branch** of the upstream repository (not main).

## Hardware & Flashing

### RAK4631 (nRF52840)

- **Serial port:** `/dev/cu.usbmodem2101`
- **Bootloader:** WisBlock RAK4631 UF2 Bootloader v0.4.2, SoftDevice S140 6.1.1
- **Build:** `pio run -e wiscore_rak4631`
- **Flash method (UF2):**
  1. Double-tap the reset button to enter UF2 bootloader mode (volume `RAK4631` appears under `/Volumes/`)
  2. Convert hex to UF2: `python3 ~/.platformio/packages/framework-arduinoadafruitnrf52/tools/uf2conv/uf2conv.py .pio/build/wiscore_rak4631/firmware.hex -c -f 0xADA52840 -o .pio/build/wiscore_rak4631/firmware.uf2`
  3. Copy UF2 to volume: `cp .pio/build/wiscore_rak4631/firmware.uf2 /Volumes/RAK4631/`
  4. Device reboots automatically after flashing (macOS may show an I/O error — this is cosmetic)
- **Flash method (PlatformIO):** `pio run -e wiscore_rak4631 --target upload` — uses `adafruit-nrfutil` DFU serial, requires the device to be running (not in UF2 mode)
- **Note:** If the serial port is busy (e.g. Chrome Web Serial), close the connection first. Check with `lsof /dev/cu.usbmodem2101`.
- **Web GUI: no mDNS — reach it by IP only.** `dk5en-90.local` does not resolve, and that is by
  design rather than a fault. The mDNS responder is ESP32-only: `ESPmDNS.h` is included only in
  the ESP32 half of `src/web_functions/web_commonServer.h`, and the whole `MDNS.begin()` /
  `MDNS.end()` block in `src/web_functions/web_functions.cpp` (~101-140, 175) sits inside the
  same guard. The nRF52 half takes the `RAK13800_W5100S` Ethernet path, which ships no mDNS
  responder — nothing is compiled in, so there is no failed start to debug. The web server
  itself runs fine over Ethernet; look the DHCP address up in the boot log
  (`Ethernet.localIP():`) or on the router. Accepted as-is 2026-09-01: a responder would mean
  writing a UDP multicast service on 224.0.0.251:5353 from scratch for one node with a stable
  lease.

### ESP32 boards (Heltec V3, T-Beam, T-Deck, etc.)

- Flash via `esptool` using custom `upload_command` defined in each variant's `platformio.ini`
- Use `pio run -e <env> --target upload`
