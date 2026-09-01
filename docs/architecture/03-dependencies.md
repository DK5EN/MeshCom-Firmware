# 03 — Dependencies

> **What is outdated, what breaks on upgrade, and which hardware is at risk?**

Checked 2026-07-30 against upstream release tags.

## The headline

> **CORRECTED 2026-07-31.** The claim below that _every_ `espressif32` release pins
> `~3.20017.0` is false. Re-derived from the platform manifests: **6.5.0 and 6.6.0 pin
> `~3.20014.0` = Arduino 2.0.14 / IDF 4.4.5**; only 6.13.0 and 7.0.1 pin 2.0.17. So
> `t_deck` and `t_deck_plus` (`@ 6.6.0`) and `t_deck_pro` and `t5_epaper` (`@ 6.5.0`) run a
> **different Arduino core from the other 26 boards** — the very split this section denies.
> For those four, bumping the platform _is_ a real core update and belongs in the sequence.
> See [08 C-04](08-defect-catalogue.md#c-04--the-arduino-2017-headline-is-false-for-four-boards--verified).

Most of the firmware runs on **Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7**; four boards run
2.0.14 / 4.4.5 (see the correction above).

That is not a pin the project chose. The releases the project actually pins for the
majority of boards — 6.13.0 and 7.0.1 — both declare
`framework-arduinoespressif32: ~3.20017.0`. The official platform stopped tracking the
Arduino core at 2.0.17 and only advances the ESP-IDF used by `framework = espidf`.

Verified locally:

```
~/.platformio/platforms/espressif32@6.13.0  → arduino: ~3.20017.0
~/.platformio/platforms/espressif32 (7.0.1) → arduino: ~3.20017.0
~/.platformio/packages/framework-arduinoespressif32 → 3.20017.241212
```

**So "update the espressif32 platform from 6.6.0 to 7.0.1" buys nothing for this project.**
It changes toolchains and the IDF that Arduino 2.0.17 was already built against. The
Arduino core stays where it is.

Meanwhile the project _already_ runs Arduino 3.x — but only for the safeboot images:

```
[env:esp32-safeboot] / [env:esp32-S3-safeboot]
  platform = tasmota/platform-espressif32 @ 2026.02.30
    → framework-arduinoespressif32 v3.1.10
    → framework-espidf v5.3.4
```

The repository therefore maintains **two ESP32 toolchains simultaneously**: the
application on Arduino 2.0.17 / IDF 4.4.7, the bootloader on Arduino 3.1.10 / IDF 5.3.4.
That split is the single most consequential fact about the dependency state.

## Inventory

### Platforms

| Package                           | Declared                              | Resolves to                | Latest                        | Assessment                                                         |
| --------------------------------- | ------------------------------------- | -------------------------- | ----------------------------- | ------------------------------------------------------------------ |
| `espressif32` (official)          | `^6.13.0`, `^6.6.0`, `6.6.0`, `6.5.0` | Arduino 2.0.17 / IDF 4.4.7 | 7.0.1 (still Arduino 2.0.17)  | Frozen upstream. Version bump is cosmetic.                         |
| `tasmota/platform-espressif32`    | `2026.02.30` (safeboot only)          | Arduino 3.1.10 / IDF 5.3.4 | `2026.05.50`                  | Actively maintained fork.                                          |
| `pioarduino/platform-espressif32` | not used                              | Arduino 3.x / IDF 5.x      | `55.03.311` (2026-07-24)      | The de-facto successor to the official platform.                   |
| `nordicnrf52`                     | **unpinned**                          | latest at install time     | 10.12.0 (Adafruit core 1.7.0) | Reproducibility hazard — see [02, B-04](02-build-and-variants.md). |

### Registry libraries

| Library                                                                                   | Pinned                        | Latest           | Gap                    | Risk on upgrade                   |
| ----------------------------------------------------------------------------------------- | ----------------------------- | ---------------- | ---------------------- | --------------------------------- |
| `jgromes/RadioLib`                                                                        | **7.6.0** (2 variants: 7.1.2) | 7.7.1            | 1–6 minors             | **Medium**                        |
| `h2zero/NimBLE-Arduino`                                                                   | 2.2.3                         | 2.5.1            | 3 minors, ~14 releases | **Medium–High**                   |
| `bblanchon/ArduinoJson`                                                                   | ^7.4.3 (2 variants ^7.4.1)    | 7.4.3            | current                | none                              |
| `beegee-tokyo/SX126x-Arduino`                                                             | ^2.0.32                       | 2.0.32           | current                | none                              |
| `mathieucarbou/AsyncTCP`                                                                  | 3.2.14                        | 3.3.2            | 2 minors               | Low (safeboot only)               |
| `mathieucarbou/ESPAsyncWebServer`                                                         | 3.3.23                        | 3.6.0            | 3 minors               | Low–Medium (safeboot only)        |
| `mathertel/OneButton`                                                                     | ^2.6.1                        | 2.6.2            | 1 patch                | none                              |
| `mikalhart/TinyGPSPlus`                                                                   | ^1.1.0                        | 1.1.0 (registry) | current                | none                              |
| `olikraus/U8g2`                                                                           | ^2.36.5                       | 2.36.x           | current-ish            | none                              |
| `lewisxhe/SensorLib`                                                                      | ^0.2.6                        | 0.4.1            | 2 minors               | **High** — API changed in 0.3/0.4 |
| `marian-craciunescu/ESP32Ping`                                                            | ^1.7                          | 1.7              | current                | none                              |
| Adafruit sensor libs (BME680, CCS811, BMP3XX, AHTX0, RTClib, MCP23017, DHT, SHTC3, LPS2X) | caret pins                    | current-ish      | small                  | none                              |
| `sparkfun/SparkFun_u-blox_GNSS`                                                           | git HEAD                      | moving           | unpinned               | Low, but unpinned                 |
| `icssw-org/NTPClient`, `icssw-org/RAK13800-W5100S`, `RobTillaart/INA226`                  | git HEAD                      | moving           | unpinned               | Low, but unpinned                 |

### Vendored libraries (`lib/`)

These are checked into the repo and are **not** updated by PlatformIO.

| Library             | Vendored | Upstream latest | Assessment                                                                                                                      |
| ------------------- | -------- | --------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `lvgl`              | 8.3.11   | 9.5.0           | **Do not upgrade.** v9 is a full API break; all of `src/t-deck/` (~7.5k lines) would need rewriting for zero user-visible gain. |
| `TFT_eSPI`          | 2.5.22   | 2.5.43          | Low value. `t_deck`/`t_deck_plus` pin platform 6.6.0 specifically because of TFT_eSPI issue #3332 — verify that first.          |
| `GxEPD2`            | 1.5.5    | 1.6.9           | Medium value (e-paper fixes), medium risk. Affects `wireless-paper`, `vision-master-*`, `t_echo`.                               |
| `XPowersLib`        | 0.2.4    | 0.3.3           | Affects T-Beam/T-Deck PMU. Test on hardware or leave.                                                                           |
| `SensorLibTDECkpro` | 0.2.1    | 0.4.1           | Declares `"name": "SensorLib"` — collides with the registry `lewisxhe/SensorLib`.                                               |
| `epdiy`             | 2.0.0    | —               | T5 e-paper only.                                                                                                                |
| `ESP32-audioI2S`    | 2.1.0    | —               | T-Deck audio only.                                                                                                              |
| `TinyGSM`           | 0.11.7   | —               | T-Connect-Pro modem only.                                                                                                       |
| `AceButton`         | 1.3.3    | —               | Stable, effectively done.                                                                                                       |
| `Adafruit TCA8418`  | 1.0.2    | —               | T-Deck keyboard only.                                                                                                           |
| `es7210`, `Timeout` | —        | —               | Small local shims.                                                                                                              |

## Breaking changes in detail

### RadioLib 7.6.0 → 7.7.1

| Version | Change                                                                                                                      | Impact here                                                                |
| ------- | --------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| 7.6.0   | `getDataRate()` **removed**                                                                                                 | already on 7.6.0 — absorbed                                                |
| 7.7.0   | New `begin()` taking `ConfigLoRa_t`/`ConfigFSK_t`. **Old parameterised `begin()` deprecated, removal announced for 8.0.0.** | `src/lora_setchip.cpp` uses the old form. Compiles today, breaks at 8.0.0. |
| 7.7.0   | `[SX126x]` skip-reset-on-startup option added; `rxBw` configuration simplified                                              | verify chip init still behaves                                             |
| 7.7.1   | OOB read + leak fix in LoRaWAN `parseDownlink`                                                                              | not applicable — `RADIOLIB_EXCLUDE_LORAWAN=1`                              |

**Verdict:** 7.6.0 → 7.7.1 is low-risk source-wise, but it is an RF driver. Every change
here can shift CAD timing, TX ramp and IRQ latency, all of which `lora_functions.cpp`
depends on (`CSMA_SLOT_SIZE 35ms = 28ms CAD + 2ms TX-switch + 5ms safety`). Requires
on-air verification per radio chip family (SX1262, SX1268, SX1276/8), not per board.

**Do first, independently of the version bump:** move `t_deck_pro` and `t5_epaper` off
7.1.2 onto the common pin. Running the shared CSMA logic against two RadioLib generations
is a worse problem than being one minor behind.

### NimBLE-Arduino 2.2.3 → 2.5.1

Fourteen releases. The relevant ones:

| Version     | Change                                                                                                                                                                                                        |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2.3.2       | **Behaviour change:** BLE secure connections now **disabled by default**. FreeRTOS port switched to the legacy porting layer for all but the newest MCUs. Fixes build errors with IDF 4.x cores.              |
| 2.3.4–2.3.9 | esp32c6/c5/c2/h2 scan and init crashes; max-connections fixes for c3/s3                                                                                                                                       |
| 2.4.0       | GATT handle assignment reworked; **notification/indication payloads > 255 bytes with small ACL buffers were silently truncated** — fixed. Re-pairing after deleting all bonds fixed. Whitelist bounds checks. |
| 2.5.0       | Client connection-state tracking; `setValue` with `char` input length fix; connection retry                                                                                                                   |
| 2.5.1       | `whiteListRemove` **use-after-free**; scan-response timer crash on reinit; infinite recursion in tick→ms conversion                                                                                           |

**Why this matters here:** the firmware ships BLE as the primary phone interface
(`MAX_MSG_LEN_PHONE 300`, `addBLEOutBuffer`, `BLEtoPhoneBuff[MAX_RING][305]`). The 2.4.0
multi-mbuf truncation fix and the 2.5.1 use-after-free are exactly the class of bug that
produces "the phone app sometimes drops messages" reports. `docs/report-ble-tx-latency.md`
already documents work in this area.

**Risks:**

- `[esp32]` sets `CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4` and `MAX_CONNECTIONS=1` — tight
  buffer tuning that interacts directly with the 2.4.0 mbuf changes. Re-measure.
- The secure-connections default flip in 2.3.2 can change pairing behaviour against
  already-bonded phones. `PAIRING_PIN "000000"` in `configuration_global.h` suggests
  bonding is in use.
- Known constraint (recorded separately): arduino-esp32's prebuilt mbedtls does not
  provide `MBEDTLS_CMAC_C`, so `CONFIG_BT_NIMBLE_CRYPTO_STACK_MBEDTLS=1` fails to link.
  Keep NimBLE on its bundled tinycrypt.

**Verdict: the highest-value library upgrade available.** Test matrix: pair/unpair/re-pair
on iOS and Android, message > 255 bytes, long-running scan, reconnect after node reboot.

### Arduino-ESP32 2.0.17 → 3.x (the real decision)

This is not a library bump, it is a platform migration. It is also the only path to
security and toolchain updates for the application image.

**What changes:**

| Area                 | 2.0.17 (IDF 4.4)              | 3.x (IDF 5.x)                                  |
| -------------------- | ----------------------------- | ---------------------------------------------- |
| ADC                  | `analogRead` legacy driver    | new `esp_adc` driver; calibration API changed  |
| LEDC / PWM           | `ledcSetup` + `ledcAttachPin` | `ledcAttach(pin, freq, res)`                   |
| Timers               | `timerBegin(num, div, up)`    | `timerBegin(freq)`                             |
| WiFi                 | `WiFi.h` mostly compatible    | event enum renames, `WiFiClientSecure` changes |
| Ethernet             | `ETH.h`                       | reworked; `ETHClass2` shim used by T-ETH-ELITE |
| Hall sensor          | present                       | **removed**                                    |
| `Stream`/`Print`     | —                             | signature changes flagged by `-Wall -Wextra`   |
| Partition/bootloader | —                             | bootloader format differs → safeboot interplay |
| RAM footprint        | baseline                      | IDF 5 uses more static DRAM                    |

**The DRAM problem is the one to worry about.** `configuration_global.h` already carries
per-board ring sizing because DRAM ran out:

```c
// tight boards
#define MAX_MHEARD 10   // was 20, limited by DRAM
#define MAX_MHPATH 10   // was 30, limited by DRAM
#define MAX_DEDUP_RING 10  // was 60
```

Moving to IDF 5 will consume more static DRAM before your code runs. On the 4 MB /
no-PSRAM boards this can be the difference between booting and not. `tools/ram_snapshot.py`
and the `/ram-snapshot` skill exist precisely for this measurement — take a full baseline
across all 32 environments **before** touching the platform.

**Affected hardware, ranked by migration risk:**

| Risk       | Boards                                                                           | Why                                                                             |
| ---------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| **High**   | `t_deck`, `t_deck_plus`, `t_deck_pro`, `t5_epaper`                               | LVGL 8 + TFT_eSPI + epdiy + PSRAM; already pinned to old platforms for a reason |
| **High**   | `heltec_wifi_lora_32_V2`, `ttgo-lora32-v21`, `ttgo_tbeam*` (ESP32 classic, 4 MB) | DRAM headroom; oldest board support in the tree                                 |
| **Medium** | `wireless-paper`, `vision-master-e213/e290`                                      | GxEPD2 + power-control shims                                                    |
| **Medium** | `T-ETH-ELITE_1262`, `LilyGo_T_Connect_Pro`                                       | Ethernet / modem stacks changed most                                            |
| **Low**    | `heltec_wifi_lora_32_V3/V4`, `E22*_S3`, `LilyGo_T3_S3_V1_3`                      | S3, PSRAM, modern peripherals                                                   |
| **None**   | `wiscore_rak4631`, `heltec_t114`, `t_echo`                                       | nRF52 — different toolchain entirely                                            |

**Recommendation:** do not migrate as one change. Migrate one modern S3 board first
(`heltec_wifi_lora_32_V3` is the best candidate — most users, best-tested, low
peripheral surface), keep it on a separate env alongside the 2.0.17 one, and let the two
coexist until the S3 path is proven on air for a full release cycle.

### nRF52 side

Comparatively healthy. `SX126x-Arduino 2.0.32` is current. The one action item is pinning
`nordicnrf52` — currently unpinned, so CI and local builds can differ silently. Pin to
`10.12.0` (Adafruit nRF52 core 1.7.0) and bump deliberately.

## Recommended sequence

Each step is independently shippable and independently revertible. Do not batch them.

| Step | Change                                                                    | Blast radius     | Verification needed                              |
| ---- | ------------------------------------------------------------------------- | ---------------- | ------------------------------------------------ |
| 0    | **CI builds all 32 envs on PR** ([02, B-07](02-build-and-variants.md))    | none (CI only)   | —                                                |
| 0b   | **RAM baseline across all 32 envs** (`tools/ram_snapshot.py`)             | none             | —                                                |
| 1    | Pin `nordicnrf52 = 10.12.0`                                               | 3 nRF52 boards   | build + one flash                                |
| 2    | Converge RadioLib: `t_deck_pro`, `t5_epaper` 7.1.2 → 7.6.0                | 2 boards         | on-air TX/RX, CAD                                |
| 3    | ArduinoJson/OneButton caret alignment; drop duplicated variant `lib_deps` | all              | build only                                       |
| 4    | RadioLib 7.6.0 → 7.7.1 (all boards)                                       | all radio boards | on-air per chip family: SX1262 / SX1268 / SX127x |
| 5    | NimBLE 2.2.3 → 2.5.1                                                      | all ESP32 boards | full BLE matrix, RAM re-measure                  |
| 6    | AsyncTCP 3.3.2 + ESPAsyncWebServer 3.6.0 (safeboot)                       | safeboot images  | OTA upload end-to-end                            |
| 7    | GxEPD2 1.5.5 → 1.6.9                                                      | 4 e-paper boards | display on hardware                              |
| 8    | XPowersLib 0.2.4 → 0.3.3, SensorLib → 0.4.1                               | T-Beam/T-Deck    | PMU + battery on hardware                        |
| 9    | **Arduino 3.x pilot on one S3 board**, parallel env                       | 1 board, opt-in  | everything                                       |
| —    | **LVGL 8 → 9: do not.**                                                   | —                | —                                                |

Steps 4, 5, 7, 8 and 9 all need physical hardware. Steps 0–3 and 6 do not, and cover the
reproducibility and consistency problems, which are the ones currently costing time.

## What is missing to do any of this safely

There are **zero automated tests** ([06 — Test Strategy](06-test-strategy.md)). Every
verification column above currently reads "flash it and watch". Until at least the wire
format and the CSMA timing have golden-vector tests, every dependency bump is an
uninstrumented experiment on a live network.
