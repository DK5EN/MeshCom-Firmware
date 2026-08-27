> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

> [!NOTE]
> **Every code change in this fork is now part of official MeshCom.** On 27 August 2026 the ICSSW maintainers merged both of our pull requests into upstream `dev`:
>
> - **[PR #1102](https://github.com/icssw-org/MeshCom-Firmware/pull/1102)** — 82 stability and memory-safety changes from five months of field operation. 64 files, +3,213 / −1,113.
> - **[PR #1103](https://github.com/icssw-org/MeshCom-Firmware/pull/1103)** — a buffer-size fix in upstream's own new FWDATE field, found by our build gate.
>
> That is what this fork was for. The improvements are no longer "ours" — they are in the official tree, and the next official 4.35p release will carry them whether or not you install this build.

## What this release is

**Official MeshCom `dev` as of 27 August 2026 (`fc83554e`), built and packaged.**

The previous stability release was based on upstream merge-base `8114d7ae` (18 August) with our changes layered on top. This one is based on upstream `dev` **after** it absorbed those changes, so it also carries everything the ICSSW team and other contributors added in between — T-Deck SD-card offline maps, the T-Echo BME280 fix, the extended Mheard JSON, and the new BLE FWDATE field.

Flash version `20260827`. `FLASH_STRUCT_VERSION` stands at `20260724` and only moves when the settings layout really changes — **your configuration survives this update.**

## [MeshCom Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.27.2-stability/docs/CHANGELOG-stability.md)

## [MeshCom@ICSSW Projektseite](https://icssw.org/en/meshcom/)

## What changed since v4.35p.08.27-stability

Almost nothing in our own code — and that is the point. The delta is upstream's.

**From upstream (new since merge-base `8114d7ae`):**

- **SD-card offline map tiles for T-Deck Plus**, with dynamic map sets, per-map-set zoom range detection, auto-zoom-out fallback for missing tiles, tile-boundary auto-reload and a batch of marker fixes. The five compiled-in map blobs (Europe, Germany, Austria, Vienna, Vienna surroundings — about 3,850 lines of generated C) are gone from the source tree in favour of tiles on the card.
- **T-Echo BME280 support fixed** — correct I2C pins, conditional address handling for BMP280 vs BME280, and the unused second I2C bus removed from the variant.
- **Mheard JSON extended** — the HEY beacon's link chain now carries RSSI/SNR per hop, plus originating callsign and gateway identifier. The buffer size is passed to `serializeJson` instead of a measured length.
- **Info JSON carries the build date** (`FLASH_VERSION`).
- **New BLE TYPE `I` field `FWDATE`** so apps can tell sub-releases apart; `FWVER` is unchanged for compatibility.

**From us:**

- **`FWDATE` was being truncated.** Upstream's new field used a 20-byte buffer for `__DATE__ " " __TIME__`, which needs 21. `snprintf` silently cut the last digit of the seconds. Fixed and sent upstream as PR #1103.
- **A build-hygiene fix**: a probe flag in the battery code was declared outside the `#if` block that uses it, so boards without `ADC_CTRL_PIN` compiled it unused.

## What changes on the air

Nothing, relative to `v4.35p.08.27-stability`. The three on-air changes announced in that release (the `/B=000` report, dropping implausible ACK-path frames, and corrected ESP32 utilisation figures) are unchanged and now also in official upstream.

## Supported Hardware

### Verification for this release

This is a rebase-and-repackage release, not a new development cycle. What was verified here:

- **All 32 release environments build clean**, and the seven main targets build with `-Werror` on `src/` with no warnings from our own code.
- The tree that produced these binaries is **bit-identical** to `v4.35p.08.27-stability` in everything except upstream's additions listed above — verified by tree hash, not by inspection.

The bench results behind the fork's own changes are those of `v4.35p.08.27-stability` and still apply: OTA on **Heltec V3** with settings intact, OTA on **T-Beam v1.2** with callsign and WiFi preserved, DFU on **WisBlock RAK4631** with callsign and Ethernet configuration preserved.

### Not on our bench

**Upstream's new T-Deck and T-Echo code in this release is untested by us.** We have neither a T-Deck Plus with an SD card nor a T-Echo with a BME280. Those changes come from the ICSSW team and other contributors, they build clean, and they ship here — but no one on this side has watched them run. Reports welcome.

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

- **Upstream's T-Deck SD-map and T-Echo BME280 changes have had no bench time here.** They are new in this release and we cannot vouch for them.
- **The battery zero point on a real pack** is proven from the logic and from tests, but no battery on our bench was flat enough to trigger the `/B=000` report in the field.
- **The INA226 branch** of the position encoder is untested — no board on the bench carries one.
- **The `--txcapture` transmit side over a real radio** is not systematically verified against a second receiver.
- **L76K GPS modules** remain untested, as in the previous release.
- **Boot on battery with no USB host** is still not verifiable on this bench.

## Installing

- **First install / full flash:** flash bootloader, partitions, otadata, safeboot, and firmware at the addresses listed in the [README](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35p.08.27.2-stability/README.md#flashing-firmware) (`bootloader.bin` for classic ESP32, `bootloader-s3.bin` for ESP32-S3).
- **Already running MeshCom 4.x with safeboot:** just OTA the `firmware.bin` for your board — via the node's OTA web page, or scripted: `python3 tools/webflash.py <YOUR-CALLSIGN>.local`
- **RAK4631:** copy the `.uf2` onto the bootloader volume (double-tap reset), or `adafruit-nrfutil --verbose dfu serial --package wiscore_rak4631.zip -p <PORT> --singlebank --touch 1200`

## Thank you

To the MeshCom maintainers and the ICSSW team: thank you for taking the whole set. Reviewing 3,200 lines from an outside fork and merging it the same day is a lot of trust, and we do not take it lightly. This project is a gift to the amateur radio community — every one of these changes has now found its way home.
