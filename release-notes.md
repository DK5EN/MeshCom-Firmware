> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

> [!WARNING]
> **If you are running official `dev` from 27 August 2026 or the previous build of this fork, your node's BLE `I` register is broken.** Upstream commit `82db3d41` pushed the `I` register past the frame size the firmware allows itself, so apps receive a truncated, unparseable object and lose the node's identity entirely — callsign, ID and hardware type. This release fixes it. Details below.

## What this release is

**A hotfix on top of `v4.35p.08.27.2-stability`.** Three defects in the BLE-to-phone frame path, one of them actively breaking node identity in the field, plus the regression tests that hold them down.

Flash version `20260828`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

## [MeshCom Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.28-stability/docs/CHANGELOG-stability.md)

## [MeshCom@ICSSW Projektseite](https://icssw.org/en/meshcom/)

## What changed since v4.35p.08.27.2-stability

### The `I` register stopped arriving (fixed)

Upstream's commit `82db3d41` changed the value of the BLE `I` register's `FWDATE` field from the integer `FLASH_VERSION` to the string `__DATE__ " " __TIME__`. That is 14 characters more, and it pushes the document past the limit the firmware sets for itself.

The limit is easy to miss, because the number checked in the register builder is not the one that applies. `command_functions.cpp` tests against `MAX_MSG_LEN_PHONE - 2` (298), while the clamp that actually bites sits one level down in `addBLEComToOutBuffer()` at 245 bytes — 244 characters of JSON once the type byte is subtracted. Past that the firmware cuts in the middle of a value, so the app does not get a shortened object, it gets a syntactically broken one. Every field is lost, not just the last: `CALL`, `ID` and `HWID` included.

Measured on our own gateway, the register was dropped **59 times in 9.5 hours**, once per reconciliation cycle, from the moment the new firmware came up. After this fix: zero.

The field is back to `FLASH_VERSION`. The key name is unchanged, only the type moves from string to number — and `FLASH_VERSION` is the value that is actually maintained per release, whereas `__DATE__`/`__TIME__` is the clock time of whichever compiler run produced the binary.

**This is not fully solved.** A node with all six group-call slots filled is still 13 characters over the limit, and was already over before `82db3d41`. The durable fix is to send `GCB0`…`GCB5` as one array, which saves 34–41 characters — but that is a change apps can see, so it belongs in a separate, coordinated step. It is written up and reported upstream rather than done quietly here.

### Mheard records could vanish silently (fixed)

The `PP` link chain added to the Mheard JSON grows with every relay — roughly 11 characters per hop. At the default hop setting the record sits around 214 characters and nothing happens. `{SET}` allows up to 7 hops, and there the path breaks in a way that leaves no trace: `addBLEOutBuffer()` clamps `'D'` frames at 255 rather than 245, and the write length is computed in a `uint8_t`. At 253 characters of JSON, `blelen + 2` wraps to 0 or 1 on ESP32 and the frame goes out as a zero- or one-byte write. The record is gone, with no log line. (nRF52 is unaffected: there the same expression is promoted to `int`.)

The builder now measures before serialising and drops the most expensive optional field — `PP` first, then `DIST`, which can be recomputed from the two stations' coordinates. A record without `PP` is still fully usable; a byte-truncated one is not parseable at all. The omission is logged rather than done silently.

Separately, the chain is now bounded where it is built: `appendHeySignalReport()` stops appending once the next group would exceed `HEY_PATH_PAYLOAD_MAX`. Normal operation is bounded by `MAX_HOP_LIMIT`, but an over-long `@` packet arriving off the air is not. The bound is set so that the longest **legitimate** chain never touches it.

### Also in this release

- Two named constants replace numbers that were previously scattered and unexplained: `BLE_JSON_PAYLOAD_MAX` (244) and `HEY_PATH_PAYLOAD_MAX`.
- Four new cases in `test_hey_report`. Two are red without the fix; two are guard tests that pin down what must **not** change — a chain at full hop depth, and the case exactly at the boundary.
- Two issue reports in `docs/`, one per defect, written for the upstream authors with the code references and the fix proposals.

## What changes on the air

**Nothing for normal operation.** The one on-air change is a bound: a HEY beacon's link chain stops growing once it would exceed the longest legitimate chain. No path that could occur in regular operation is affected — that is what the guard tests check.

## Supported Hardware

### Verification for this release

- **All 32 release environments build clean.**
- **Native test suites green**: 93 cases across `native`, `native_aprs`, `native_dedup`, `native_capture`.
- **Heltec V3 (`heltec_wifi_lora_32_V3`)** — flashed over WiFi OTA onto a node in live service, settings intact. The `I` register drops stopped at the reboot and have not recurred across subsequent reconciliation cycles.
- **WisBlock RAK4631** — builds clean; the `uint8_t` overflow this release removes never affected nRF52 in the first place.

### Not on our bench

The BLE fixes are verified on Heltec V3 only. Everything else in this release is a rebuild of `v4.35p.08.27.2-stability` with three targeted changes, so the hardware coverage and the gaps from that release carry over unchanged — including upstream's T-Deck SD-map and T-Echo BME280 code, which still has had no bench time here.

The remaining boards build cleanly from the same source and inherit every improvement, but we could not put them on our own bench:

- **T-Beam Supreme** — builds clean and is included, but still unverified. It carries an **L76K** GPS and both modules on our bench are u-blox, so the L76K branch of the probe is exercised by no test.
- E22-DevKitC, E22_1262-DevKitC, E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, E22_XML-DevKitC
- esp32-loraprs-e22, esp32-loraprs-ra01
- heltec_wifi_lora_32_V2, heltec_wifi_lora_32_V4, heltec_wireless_stick, heltec_wireless_tracker, wireless-paper
- vision-master-e213, vision-master-e290
- ttgo-lora32-v21, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, T-Beam-1W
- T3_S3_V13, t_connect_pro, t_deck, t_deck_plus, t_deck_pro, T-ETH-ELITE_1262
- heltec_t114, t_echo

(The T5 e-paper variant is not included: it does not build from the current tree for a pre-existing include-path reason unrelated to these changes.)

### Known gaps, stated plainly

- **The `I` register is still too large for a node with all six group-call slots filled** — 13 characters over. This release does not fix that case, and it was broken before the regression too. The fix requires an app-visible change to how group calls are sent.
- **The `blelen + 2` overflow itself is still there.** This release stops the Mheard path from reaching it; it does not remove it. Any other `'D'` frame of 253 characters or more still hits it on ESP32.
- **The firmware never reads the negotiated ATT MTU.** The write limits are hardcoded on the assumption of 247 bytes; the connection we measured negotiated 255. A peer that negotiates a smaller MTU will see large frames truncated regardless of anything in this release.
- **The Mheard table dump (`--mheard`) does not carry `PP`/`SRC`/`GW`** — only the live path does. The same `TYP` therefore yields two different schemas.
- Everything listed under "Known gaps" in `v4.35p.08.27.2-stability` still applies: the battery zero point on a real pack, the INA226 branch, `--txcapture` over a real radio, L76K GPS, and boot on battery with no USB host.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.28-stability/README.md#flashing-firmware) (`bootloader.bin` for classic ESP32, `bootloader-s3.bin` for ESP32-S3).
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`

## Reporting these upstream

Both defects are written up as issue reports for the authors of the code in question, with the arithmetic, the code references and constructive fix proposals rather than just a complaint:

- [`docs/issue-ble-i-register-mtu-20260828.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.28-stability/docs/issue-ble-i-register-mtu-20260828.md) — the `I` register and the shared frame-size infrastructure.
- [`docs/issue-mh-json-size-budget-20260828.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.28-stability/docs/issue-mh-json-size-budget-20260828.md) — the Mheard size budget, the schema asymmetry, and the `PP` wire format.

The MeshCom project deserves the finding **and** the fix, not a bug report thrown over the wall.
