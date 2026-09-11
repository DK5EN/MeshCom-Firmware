# Optimization audit 2026-09-10: appendix

Verbatim scout and analyst reports behind `docs/optimization-audit-20260910.md`. HEAD `c51c5881`. Line references are to that commit. Agent claims corrected by the orchestrator are listed in the gate notes at the end; the main report supersedes any conflict.

## Measured baseline, all envs

| env                         | built       | .data+.bss (B) | IRAM text (B) | flash text+rodata (B) |
| --------------------------- | ----------- | -------------- | ------------- | --------------------- |
| E22_1262_S3-DevKitC-1-N16R8 | 09-10 09:08 | 115764         | 89862         | 1355255               |
| E22_1262-DevKitC            | 09-10 09:06 | 113112         | 126099        | 1411299               |
| E22_1268_S3-DevKitC-1-N16R8 | 09-10 09:02 | 115764         | 89862         | 1355219               |
| E22_XML-DevKitC             | 09-10 09:09 | 123416         | 127615        | 1627775               |
| E22-DevKitC                 | 09-10 09:00 | 113112         | 126099        | 1411191               |
| esp32-loraprs-e22           | 09-10 09:01 | 113000         | 126099        | 1411015               |
| esp32-loraprs-ra01          | 09-10 09:06 | 113968         | 126099        | 1400063               |
| heltec_t114                 | 09-10 09:06 | 87236          | 0             | 587848                |
| heltec_wifi_lora_32_V2      | 09-10 09:05 | 112832         | 126099        | 1402075               |
| heltec_wifi_lora_32_V3      | 09-10 09:05 | 116244         | 85114         | 1343727               |
| heltec_wifi_lora_32_V4      | 09-10 09:04 | 116380         | 85866         | 1342887               |
| heltec_wireless_stick       | 09-10 09:09 | 116244         | 85114         | 1340583               |
| heltec_wireless_tracker     | 09-10 09:04 | 113924         | 85866         | 1366759               |
| LilyGo_T_Connect_Pro        | 09-10 09:03 | 114676         | 92562         | 1414147               |
| LilyGo_T-Beam-1W            | 09-10 09:05 | 115668         | 89434         | 1359979               |
| LilyGo_T3_S3_V1_3           | 09-10 09:04 | 115744         | 87778         | 1357639               |
| t_deck_plus                 | 09-10 09:02 | 135156         | 93934         | 2062727               |
| t_deck_pro                  | 09-10 09:09 | 189664         | 90594         | 1877563               |
| t_deck                      | 09-10 09:01 | 135156         | 93934         | 2063011               |
| t_echo                      | 09-10 09:08 | 92676          | 0             | 613832                |
| T-ETH-ELITE_1262            | 09-10 09:01 | 117276         | 93742         | 1378923               |
| ttgo_tbeam_supreme          | 09-10 09:08 | 116572         | 87658         | 1376207               |
| ttgo_tbeam_SX1262           | 09-10 09:07 | 114392         | 131051        | 1469611               |
| ttgo_tbeam_SX1268           | 09-10 09:03 | 114384         | 131051        | 1469275               |
| ttgo_tbeam                  | 09-10 09:06 | 114264         | 131051        | 1460747               |
| ttgo-lora32-v21             | 09-10 09:07 | 112880         | 126099        | 1400143               |
| vision-master-e213          | 09-10 09:03 | 115964         | 85866         | 1336327               |
| vision-master-e290          | 09-10 09:00 | 117740         | 86810         | 1380223               |
| wireless-paper              | 09-10 09:07 | 115596         | 85114         | 1321759               |
| wiscore_rak4631             | 09-10 08:59 | 88844          | 0             | 657280                |

---

<!-- source: S1-build-matrix.md -->

## MeshCom Firmware Build Matrix

## 1. Build Environment Configurations

| Environment                            | Board MCU | Platform                    | Build Filter Summary                                                                       | Key Defines                          | Notes                                     |
| -------------------------------------- | --------- | --------------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------ | ----------------------------------------- |
| **ESP32 Classic (160KB DRAM)**         |           |                             |                                                                                            |                                      |                                           |
| E22-DevKitC                            | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_E22                            | Standard ESP32                            |
| E22_1262-DevKitC                       | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_E22                            | Standard ESP32                            |
| esp32-loraprs-e22                      | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_E22_LORAPRS                    | Standard ESP32                            |
| esp32-loraprs-ra01                     | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_ESP32_LORAPRS                  | Standard ESP32                            |
| heltec_wifi_lora_32_V2                 | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_HELTEC                         | Standard ESP32 + U8g2 OLED                |
| ttgo_tbeam                             | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_TBEAM_V3                       | Standard ESP32, MC_I2C_NEEDS_BUS_RESET    |
| ttgo_tbeam_SX1262                      | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_TBEAM_1262                     | Standard ESP32, MC_I2C_NEEDS_BUS_RESET    |
| ttgo_tbeam_SX1268                      | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_TBEAM_1268                     | Standard ESP32                            |
| ttgo-lora32-v21                        | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | (inherit esp32)                      | Standard ESP32                            |
| **ESP32-S3 (320KB SRAM + PSRAM)**      |           |                             |                                                                                            |                                      |                                           |
| heltec_wifi_lora_32_V3                 | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_HELTEC_V3                      | U8g2 OLED on Wire1                        |
| heltec_wifi_lora_32_V4                 | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | (inherit esp32)                      | Maps to V3 board                          |
| heltec_wireless_stick                  | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | (inherit esp32)                      | Maps to V3 board                          |
| heltec_wireless_tracker                | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | (inherit esp32)                      | Maps to V3 board                          |
| E22_1268_S3-DevKitC-1-N16R8            | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_E22_S3, MC_I2C_NEEDS_BUS_RESET | 16MB Flash S3                             |
| E22_1262_S3-DevKitC-1-N16R8            | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_E22_S3_1262                    | 16MB Flash S3                             |
| E22_XML-DevKitC                        | ESP32     | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | ENABLE_XML=1                         | Standard ESP32, XML tinyxml2 enabled      |
| LilyGo_T-Beam-1W                       | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_TBEAM_1W                       | S3-based T-Beam variant                   |
| LilyGo_T3_S3_V1_3                      | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | (inherit esp32)                      | Basic S3 board                            |
| LilyGo_T_Connect_Pro                   | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_TCONNECT_PRO                   | S3 board                                  |
| T-ETH-ELITE_1262                       | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_ETH_ELITE                      | Ethernet capable                          |
| ttgo_tbeam_supreme                     | ESP32-S3  | espressif32@6.13.0          | -Displays, -Fonts, -GFX, -Platforms, -t-deck*, -t5-epaper                                  | BOARD_TBEAM_SUPREME                  | S3 T-Beam Supreme                         |
| **T-Deck Display Variants (ESP32-S3)** |           |                             |                                                                                            |                                      |                                           |
| t_deck                                 | ESP32-S3  | espressif32@6.6.0           | +t-deck/_, -t-deck-pro/_, -t5-epaper/*                                                     | BOARD_T_DECK, BOARD_HAS_PSRAM=1      | LVGL + TFT display, 16MB partition        |
| t_deck_plus                            | ESP32-S3  | espressif32@6.6.0           | +t-deck/_, -t-deck-pro/_, -t5-epaper/*                                                     | BOARD_T_DECK_PLUS, BOARD_HAS_PSRAM=1 | LVGL + TFT display, 16MB partition        |
| t_deck_pro                             | ESP32-S3  | espressif32@6.5.0           | +t-deck-pro/_, -t-deck/_, -t5-epaper/*, -gps_l76k.cpp, -esp32/esp32_gps.cpp                | BOARD_T_DECK_PRO, BOARD_HAS_PSRAM    | LVGL + display, native USB, SIM7672 modem |
| **E-Paper Display Boards**             |           |                             |                                                                                            |                                      |                                           |
| t5_epaper                              | ESP32-S3  | espressif32@6.5.0           | +t5-epaper/_, -Displays/_, -Fonts/_, -GFX/_, -Platforms/_, -t-deck_, -nrf52/*              | BOARD_T5_EPAPER                      | T5 e-paper S3, custom build filter        |
| wireless-paper                         | ESP32-S3  | espressif32@6.6.0           | +Displays/_, +Fonts/_, +GFX_Root/_, +Platforms/_, -t-deck*, -t5-epaper/_, -nrf52/_         | BOARD_WIRELESS_PAPER, WIRELESS_PAPER | E-paper + Heltec v3                       |
| vision-master-e290                     | ESP32-S3  | espressif32@6.6.0           | +Displays/_, +Fonts/_, +GFX_Root/_, +Platforms/_, -t-deck*, -t5-epaper/_, -nrf52/_         | BOARD_E290                           | E-Ink 2.9" Vision Master E290             |
| vision-master-e213                     | ESP32-S3  | espressif32@6.6.0           | +Displays/_, +Fonts/_, +GFX_Root/_, +Platforms/_, -t-deck*, -t5-epaper/_, -nrf52/_         | BOARD_E213, VISION_MASTER_E213       | E-Ink 2.13" Vision Master E213            |
| **nRF52840 (256KB RAM)**               |           |                             |                                                                                            |                                      |                                           |
| wiscore_rak4631                        | nRF52840  | nordicnrf52@10.12.0         | -esp32/_, -Displays/_, -Fonts/_, -GFX/_, -Platforms/_, -safeboot/_, -t-deck*, -t5-epaper/* | BOARD_RAK4630                        | RAK4631 WisBlock with W5100S Ethernet     |
| t_echo                                 | nRF52840  | nordicnrf52@10.12.0         | -esp32/_, -Displays/_, -Fonts/_, -GFX/_, -Platforms/_, -safeboot/_, -t-deck*, -t5-epaper/* | BOARD_T_ECHO, SERIAL_BUFFER_SIZE=250 | T-Echo nRF52 board                        |
| heltec_t114                            | nRF52840  | nordicnrf52@10.12.0         | -esp32/_, -Displays/_, -Fonts/_, -GFX/_, -Platforms/_, -safeboot/_, -t-deck*, -t5-epaper/* | BOARD_HELTEC_T114, TFT ST7735 pins   | nRF52 with TFT display                    |
| **Safeboot (OTA Bootloader)**          |           |                             |                                                                                            |                                      |                                           |
| esp32-safeboot                         | ESP32     | tasmota/platform@2026.02.30 | +safeboot/_, +esp32/esp32_flash._                                                          | MC_SAFEBOOT=1                        | OTA bootloader for classic ESP32          |
| esp32-S3-safeboot                      | ESP32-S3  | tasmota/platform@2026.02.30 | +safeboot/_, +esp32/esp32_flash._                                                          | MC_SAFEBOOT=1, ARDUINO_USB_MODE=1    | OTA bootloader for ESP32-S3               |

## 2. Platform-Conditional Constants in src/configuration_global.h

### Ring Buffer Size Constants

| Constant       | ESP32-S3 / nRF52840 | ESP32 Classic | ENABLE_XML/SBUFFER | ENABLE_TBEAM | Line     |
| -------------- | ------------------- | ------------- | ------------------ | ------------ | -------- |
| MAX_MHEARD     | 80                  | 30            | 50                 | 10           | 212, 227 |
| MAX_MHPATH     | 100                 | 40            | 50                 | 10           | 213, 228 |
| MAX_RING       | 20                  | 20            | 20                 | 10           | 214, 236 |
| MAX_DEDUP_RING | 100                 | 70            | 60                 | 10           | 215, 237 |
| MAX_LOG        | 10                  | 20            | 20                 | 10           | 216, 238 |
| MAX_RING_UDP   | 20                  | 20            | 20                 | 10           | 217, 239 |

**Trigger Logic (line 201-240):**

- `ENABLE_XML || ENABLE_SBUFFER` → Small buffers (line 201-209)
- `CONFIG_IDF_TARGET_ESP32S3 || BOARD_RAK4630` → Large buffers (line 210-217)
- `ENABLE_TBEAM` → Minimal buffers, developer-only (line 218-224)
- Default (ESP32 classic, ~160KB DRAM) → Reduced buffers (line 225-239)

### Other Size-Related Constants

| Constant             | Value                                                     | Line       | Notes                                                                           |
| -------------------- | --------------------------------------------------------- | ---------- | ------------------------------------------------------------------------------- |
| MAX_CALL_LEN         | 20                                                        | 162        | Callsign max length                                                             |
| MAX_MSG_LEN_PHONE    | 300                                                       | 358        | BLE message to phone                                                            |
| BLE_JSON_PAYLOAD_MAX | 244                                                       | 369        | Effective BLE JSON limit (due to uint8_t overflow)                              |
| UDP_TX_BUF_SIZE      | 255                                                       | 169        | Outgoing UDP buffer                                                             |
| MAX_APRS_FRAME_SIZE  | 340                                                       | (external) | RX buffer never cleared between receives (RX buffer lead: RX_BUFFER_STALE_TAIL) |
| LONGNAME_MAXLEN      | 20                                                        | 159        | Longname max length                                                             |
| HEY_PATH_PAYLOAD_MAX | 8 + MAX_HOP_LIMIT * HEY_REPORT_GROUP_MAX (8 + 7*14 = 106) | 262        | HEY link chain upper bound                                                      |

### Board-Conditional Compilation

| Define                 | Enabled For                          | File    | Notes                                                     |
| ---------------------- | ------------------------------------ | ------- | --------------------------------------------------------- |
| WP_DISP                | BOARD_WIRELESS_PAPER \|\| BOARD_E213 | 152-154 | Shared 2.13" e-ink display code path                      |
| MC_I2C_NEEDS_BUS_RESET | BOARD_TBEAM_V3 \|\| BOARD_E22_S3     | 192     | I2C bus reset requirement (workaround for 9 sensor files) |

**Platform Detection (line 210):**

- `CONFIG_IDF_TARGET_ESP32S3`: PlatformIO platform builtin (esp32 platform sets it)
- `BOARD_RAK4630`: nRF52840 (set in nrf52_base, line 123)

## 3. Source Directory Compilation Map

| Directory           | Compiled For                                                | Excluded For                                         | Uses                                                                    |
| ------------------- | ----------------------------------------------------------- | ---------------------------------------------------- | ----------------------------------------------------------------------- |
| src/esp32/*         | All ESP32 variants (classic, S3, e-paper, etc.)             | nRF52840                                             | ESP32-specific peripherals: WiFi, BLE (NimBLE), flash, ADC, audio, GPIO |
| src/nrf52/*         | nRF52840 only (RAK4631, T-Echo, Heltec T114)                | All ESP32                                            | nRF52-specific: UART, I2C, SPI, BLE (built-in), GPIO                    |
| src/Displays/*      | vision-master-e290, vision-master-e213, wireless-paper      | t-deck*, t-deck-pro, t5-epaper, classic ESP32, nRF52 | GxEPD2 e-paper display drivers                                          |
| src/Fonts/*         | vision-master-e290, vision-master-e213, wireless-paper      | t-deck*, t-deck-pro, t5-epaper, classic ESP32, nRF52 | Font data for e-paper displays                                          |
| src/GFX_Root/*      | vision-master-e290, vision-master-e213, wireless-paper      | t-deck*, t-deck-pro, t5-epaper, classic ESP32, nRF52 | Graphics root/utilities for e-paper                                     |
| src/Platforms/*     | vision-master-e290, vision-master-e213, wireless-paper      | t-deck*, t-deck-pro, t5-epaper, classic ESP32, nRF52 | Platform abstractions for e-paper                                       |
| src/safeboot/*      | esp32-safeboot, esp32-S3-safeboot only                      | All normal firmware builds                           | OTA bootloader (AsyncTCP, webserver)                                    |
| src/t-deck/*        | t_deck, t_deck_plus                                         | t_deck_pro, t5-epaper, classic ESP32, nRF52          | LVGL GUI + TFT display for T-Deck (900x450 color)                       |
| src/t-deck-pro/*    | t_deck_pro only                                             | t_deck, t_deck_plus, t5-epaper, classic ESP32, nRF52 | T-Deck Pro specific: LVGL GUI, AMOLED display (SIM7672 modem)           |
| src/t5-epaper/*     | t5_epaper only                                              | t-deck*, classic ESP32, nRF52                        | T5 e-paper board specific initialization                                |
| src/web_functions/* | All ESP32 variants (not compiled for nRF52 per base filter) | nRF52840                                             | Web server functions (ESP32 only due to WiFi/LAN)                       |

## 4. Excluded and Dead Files in src/ Root

### Explicitly Excluded by Filters

| File                  | Excluded From                                       | Status | Notes                                                             |
| --------------------- | --------------------------------------------------- | ------ | ----------------------------------------------------------------- |
| Regexp.cpp            | All hardware builds (native only in native env)     | 232    | Used only in native tests                                         |
| tinyxml_functions.cpp | All hardware builds (native only in native_xml env) | 423    | E22_XML variant has ENABLE_XML=1, but file not in standard filter |
| batt_function_old.cpp | Unknown (appears unused)                            | Dead?  | Old implementation, batt_functions.cpp is current                 |
| test_inject.cpp       | Unknown (appears production-excluded)               | Dead?  | Test injection, likely disabled in production                     |
| spectral_scan.cpp     | Unknown (appears production-excluded)               | Dead?  | Spectral analysis feature                                         |

### Native Test Environment Filters (lines 188-660)

| Test Environment   | Included Files                                                                                                                              | Purpose                                             |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------- |
| native             | Regexp.cpp, regex_functions.cpp, settings_sanitize.cpp, ntp_async.cpp, charset_filter.cpp, setlog_lines.cpp, gps_filter.cpp, alt_fusion.cpp | Core unit tests (regex, NTP, charset, filter logic) |
| native_aprs        | Regexp.cpp, regex_functions.cpp, aprs_functions.cpp, charset_filter.cpp, txring_functions.cpp                                               | APRS codec + TX ring buffer tests                   |
| native_parsers     | Regexp.cpp, regex_functions.cpp, aprs_functions.cpp, charset_filter.cpp, mheard_functions.cpp, via_functions.cpp                            | Protocol parsers (mheard aging, via routing)        |
| native_batt_detect | batt_functions.cpp                                                                                                                          | Battery detection algorithm                         |
| native_conf_frame  | conf_frame.cpp                                                                                                                              | Config frame parsing (server provisioning)          |
| native_extern      | Regexp.cpp, regex_functions.cpp, aprs_functions.cpp, charset_filter.cpp, extudp_functions.cpp                                               | External UDP interface                              |
| native_config      | config_json.cpp                                                                                                                             | Settings JSON serialization                         |
| native_xml         | tinyxml_functions.cpp                                                                                                                       | XML parsing (tinyxml2 library)                      |
| native_aprs_fuzz   | Regexp.cpp, regex_functions.cpp, aprs_functions.cpp, charset_filter.cpp                                                                     | APRS fuzzing + sanitizers                           |
| native_capture     | capture_functions.cpp                                                                                                                       | Capture ring buffer                                 |
| native_dedup       | dedup_functions.cpp                                                                                                                         | Dedup ring replay                                   |

**Note:** Native test envs are NOT in default_envs (line 16-59), run via `pio test` only.

---

## 5. Summary of Findings

### Dead or Potentially Unused Code

1. **batt_function_old.cpp** — Old implementation superseded by batt_functions.cpp; not in any standard build_src_filter
2. **test_inject.cpp** — Test injection utility; excluded from production builds
3. **spectral_scan.cpp** — Spectral analysis feature; excluded from standard builds
4. **Regexp.cpp** — Header-only regex tests; only in native envs (not hardware)

### Surprise Findings

1. **E22_XML is standard build, not specialized** — E22_XML-DevKitC has ENABLE_XML=1 flag but uses standard esp32 filter, so tinyxml_functions.cpp is EXCLUDED from the binary despite the feature flag. Native test native_xml tests it separately.

2. **nRF52 display code present but variant-driven** — t_echo includes GxEPD (e-paper) library, heltec_t114 includes ST7735 (TFT) library, but both compile the same nrf52_base filter. Display drivers are NOT in src/Displays/* (those are vision-master/wireless-paper only).

3. _*Vision-Master and Wireless-Paper share Displays/* but differ on BOARD guards_* — WP_DISP preprocessor macro (line 152-154) unifies code for BOARD_WIRELESS_PAPER or BOARD_E213; separate BOARD_* guards differentiate them where needed.

4. **T-Deck three-way split is strict** — t-deck/_, t-deck-pro/_, and main code are mutually exclusive via negative filters; no board compiles more than one T-Deck variant directory.

5. **Web functions only for ESP32** — src/web_functions/* compiled for all ESP32 (classic + S3), excluded from nRF52 by base filter. ESP32 WiFi/LAN only, nRF52 has no web server.

6. **ENABLE_TBEAM is developer-only** — ENABLE_TBEAM defines minimal buffer sizes (line 218) but is NOT a -D flag in any variant env; appears to be a manual override for local testing, not a shipped configuration.

7. **lvgl compiled away from disk, not filtered** — t-deck* do NOT include lvgl in lib_ignore; lvgl is compiled from .pio/libdeps but the lvgl/* source tree directory is NOT in src/ (only t-deck/* and t-deck-pro/* are). Filtering of lvgl/* in base [esp32] (line 532) suppresses src/lvgl/* if it existed; it doesn't.

## ORCHESTRATOR CORRECTION (verified against platformio.ini:108-120 and :517-531)

The claim "batt_function_old.cpp, test_inject.cpp, spectral_scan.cpp: no env compiles them" is WRONG.
Both the [esp32] src_filter and the [nrf52_base] build_src_filter start with `+<*>` and only subtract
directories, so every src/*.cpp root file IS compiled for every hardware env. Those three files are
emptied by internal preprocessor guards instead:

- src/batt_function_old.cpp:6 `#ifndef USE_NEW_BATT` (whole body)
- src/spectral_scan.cpp:27 `#if defined(BOARD_T_DECK_PRO)`
- src/test_inject.cpp:34 `#if MC_INJECT_HOOKS` (default compiled IN, see capture_functions.h:45)
  Analysts: check which envs define USE_NEW_BATT before calling batt_function_old.cpp dead.

---

<!-- source: S2-global-storage.md -->

## Global RAM Object Inventory – MeshCom Firmware

**Scope**: src/_.cpp, src/_.h, src/esp32/_, src/nrf52/_, src/web_functions/*  
**Analysis Date**: 2026-09-10  
**Primary Baseline**: ESP32-S3 (320 KB SRAM)

## Configuration Constants

Configuration values from `src/configuration_global.h`:

| Platform                   | MAX_MHEARD | MAX_MHPATH | MAX_RING | MAX_DEDUP_RING | MAX_LOG | MAX_RING_UDP |
| -------------------------- | ---------- | ---------- | -------- | -------------- | ------- | ------------ |
| ESP32-S3 / nRF52           | 80         | 100        | 20       | 100            | 10      | 20           |
| Classic ESP32              | 30         | 40         | 20       | 70             | 20      | 20           |
| Small boards (XML/SBUFFER) | 50         | 50         | 20       | 60             | 20      | 20           |

UDP_TX_BUF_SIZE = 255, MAX_MSG_LEN_PHONE = 300

---

## Global RAM Objects – Sorted by Size (ESP32-S3/nRF52)

| File:Line                             | Name                                            | Type                               | Dimensions         | Element Size | Total Bytes (S3)     | Total (Classic ESP32) | Total (nRF52) |
| ------------------------------------- | ----------------------------------------------- | ---------------------------------- | ------------------ | ------------ | -------------------- | --------------------- | ------------- |
| loop_functions_extern.h:267           | `BLEtoPhoneBuff`                                | `unsigned char[MAX_RING][]`        | 20 × 305           | 305          | **6100**             | 6100                  | 6100          |
| loop_functions_extern.h:272           | `BLEComToPhoneBuff`                             | `unsigned char[MAX_RING][]`        | 20 × 305           | 305          | **6100**             | 6100                  | 6100          |
| loop_functions_extern.h:260           | `ringBufferUDPout`                              | `uint8_t[MAX_RING_UDP][]`          | 20 × 275           | 275          | **5500**             | 5500                  | 5500          |
| loop_functions_extern.h:197           | `ringBuffer`                                    | `unsigned char[MAX_RING][]`        | 20 × 260           | 260          | **5200**             | 5200                  | 5200          |
| mheard_functions.cpp:66               | `mheardPathBuffer1`                             | `unsigned char[MAX_MHPATH][]`      | 100 × 52           | 52           | **5200**             | 2080                  | 5200          |
| loop_functions_extern.h:439           | `mheardBuffer`                                  | `unsigned char[MAX_MHEARD][]`      | 80 × 60            | 60           | **4800**             | 1800                  | 4800          |
| loop_functions_extern.h:255           | `ringbufferRAWLoraRX`                           | `unsigned char[MAX_LOG][]`         | 10 × 260           | 260          | **2600**             | 5200                  | 2600          |
| extudp_functions.cpp:73               | `externQueue`                                   | `externQueueEntry[2]`              | 2 × 514            | 514          | **1028**             | 1028                  | 1028          |
| mheard_functions.cpp:35               | `mheardCalls`                                   | `char[MAX_MHEARD][]`               | 80 × 10            | 10           | **800**              | 300                   | 800           |
| mheard_functions.cpp:67               | `mheardPathCalls`                               | `char[MAX_MHPATH][]`               | 100 × 10           | 10           | **1000**             | 400                   | 1000          |
| loop_functions_extern.h:441           | `mheardEpoch`                                   | `unsigned long[MAX_MHEARD]`        | 80 × 4             | 4            | **320**              | 120                   | 320           |
| mheard_functions.cpp:68               | `mheardPathEpoch`                               | `unsigned long[MAX_MHPATH]`        | 100 × 4            | 4            | **400**              | 160                   | 400           |
| mheard_functions.cpp:55               | `mheardMillis`                                  | `uint32_t[MAX_MHEARD]`             | 80 × 4             | 4            | **320**              | 120                   | 320           |
| mheard_functions.cpp:75               | `mheardPathMillis`                              | `uint32_t[MAX_MHPATH]`             | 100 × 4            | 4            | **400**              | 160                   | 400           |
| mheard_functions.cpp:37               | `mheardLat`                                     | `double[MAX_MHEARD]`               | 80 × 8             | 8            | **640**              | 240                   | 640           |
| mheard_functions.cpp:38               | `mheardLon`                                     | `double[MAX_MHEARD]`               | 80 × 8             | 8            | **640**              | 240                   | 640           |
| mheard_functions.cpp:39               | `mheardAlt`                                     | `int[MAX_MHEARD]`                  | 80 × 4             | 4            | **320**              | 120                   | 320           |
| loop_functions_extern.h:442           | `mheardNCount`                                  | `int[MAX_MHEARD]`                  | 80 × 4             | 4            | **320**              | 120                   | 320           |
| mheard_functions.cpp:76               | `mheardPathLen`                                 | `uint8_t[MAX_MHPATH]`              | 100 × 1            | 1            | **100**              | 40                    | 100           |
| dedup_functions.cpp:18                | `ringBufferLoraRX`                              | `uint8_t[MAX_DEDUP_RING][]`        | 100 × 5            | 5            | **500**              | 350                   | 500           |
| loop_functions_extern.h:201           | `retryCount`                                    | `uint8_t[MAX_RING]`                | 20 × 1             | 1            | **20**               | 20                    | 20            |
| loop_functions_extern.h:202           | `ringPriority`                                  | `uint8_t[MAX_RING]`                | 20 × 1             | 1            | **20**               | 20                    | 20            |
| loop_functions_extern.h:203           | `ringEnqueueTime`                               | `uint32_t[MAX_RING]`               | 20 × 4             | 4            | **80**               | 80                    | 80            |
| txring_functions.cpp:48               | `ringSource`                                    | `uint8_t[MAX_RING]`                | 20 × 1             | 1            | **20**               | 20                    | 20            |
| loop_functions_extern.h:166           | `own_msg_id`                                    | `uint8_t[MAX_RING][]`              | 20 × 5             | 5            | **100**              | 100                   | 100           |
| loop_functions_extern.h:164           | `RcvBuffer`                                     | `uint8_t[UDP_TX_BUF_SIZE*2]`       | 510 × 1            | 1            | **510**              | 510                   | 510           |
| loop_functions_extern.h:238           | `msg_text`                                      | `char[600]`                        | 600 × 1            | 1            | **600**              | 600                   | 600           |
| udp_functions.cpp:79                  | `incomingPacket`                                | `unsigned char[UDP_TX_BUF_SIZE]`   | 255 × 1            | 1            | **255**              | 255                   | 255           |
| udp_functions.cpp:81                  | `convBuffer` (ESP32)                            | `uint8_t[UDP_TX_BUF_SIZE+50]`      | 305 × 1            | 1            | **305**              | 305                   | —             |
| nrf52_main.cpp:115                    | `convBuffer` (nRF52)                            | `uint8_t[UDP_TX_BUF_SIZE+50]`      | 305 × 1            | 1            | —                    | —                     | **305**       |
| extudp_functions.cpp:60               | `incomingExtPacket`                             | `unsigned char[UDP_TX_BUF_SIZE]`   | 255 × 1            | 1            | **255**              | 255                   | 255           |
| capture_functions.cpp:34              | `cap_ring` (static)                             | `uint8_t[768]`                     | 768 × 1            | 1            | **768**              | 768                   | 768           |
| loop_functions.cpp:288 (WP_DISP)      | `dzeile`                                        | `int[maxdisplines]`                | 7 × 4              | 4            | **28**               | 28                    | 28            |
| loop_functions.cpp:493                | `pageLastLine`                                  | `int[PAGE_MAX][maxdisplines][3]`   | 10×7×3 = 210 × 4   | 4            | **840**              | 840                   | 840           |
| loop_functions.cpp:494                | `pageLastText`                                  | `char[PAGE_MAX][maxdisplines][25]` | 10×7×25 = 1750 × 1 | 1            | **1750**             | 1750                  | 1750          |
| loop_functions.cpp:495                | `pageLastTextLong1`                             | `char[PAGE_MAX][25]`               | 10×25 = 250 × 1    | 1            | **250**              | 250                   | 250           |
| loop_functions.cpp:496                | `pageLastTextLong2`                             | `char[PAGE_MAX][200]`              | 10×200 = 2000 × 1  | 1            | **2000**             | 2000                  | 2000          |
| udp_functions.cpp:55–59               | `IPAddress` vars (node_ip, node_gw, etc.)       | 5 × IPAddress                      | 5 × 4              | 4            | **20**               | 20                    | 20            |
| udp_functions.cpp:66,67               | `s_node_ip`, `s_node_hostip`                    | String (2×)                        | 2 × 16 heap        | 16           | **32+heap**          | 32+heap               | 32+heap       |
| loop_functions.cpp:214,219–224        | `strSOFTSER_BUF`, `strSOFTSERAPP_*` (7 Strings) | String[7]                          | 7 × 16             | 16           | **112+heap**         | 112+heap              | 112+heap      |
| loop_functions.cpp:224                | `strTelemetry`                                  | String                             | 1 × 16             | 16           | **16+heap**          | 16+heap               | 16+heap       |
| loop_functions.cpp:250–252            | `strTime`, `strDate`, `str`                     | String[3]                          | 3 × 16             | 16           | **48+heap**          | 48+heap               | 48+heap       |
| loop_functions.cpp:255                | `strText`                                       | `char[600]`                        | 600 × 1            | 1            | **600**              | 600                   | 600           |
| esp32_main.cpp:251–252 (ESP32 only)   | `strTime`, `strDate`                            | String[2]                          | 2 × 16             | 16           | **32+heap**          | 32+heap               | —             |
| nrf52_main.cpp:163–164 (nRF52 only)   | `strTime`, `strDate`                            | String[2]                          | 2 × 16             | 16           | —                    | —                     | **32+heap**   |
| web_functions.cpp:37                  | `web_header`                                    | String                             | 1 × 16             | 16           | **16+heap**          | 16+heap               | 16+heap       |
| web_functions.cpp:45                  | `web_ip`                                        | `char[10][20]`                     | 10×20 = 200 × 1    | 1            | **200**              | 200                   | 200           |
| web_functions.cpp:46                  | `web_ip_passwd_time`                            | `long[10]`                         | 10 × 4             | 4            | **40**               | 40                    | 40            |
| loop_functions_extern.h:486           | `pageLine`                                      | `int[maxdisplines][3]`             | 7×3 = 21 × 4       | 4            | **84**               | 84                    | 84            |
| loop_functions_extern.h:487           | `pageText`                                      | `char[maxdisplines][25]`           | 7×25 = 175 × 1     | 1            | **175**              | 175                   | 175           |
| loop_functions_extern.h:488           | `pageTextLong1`                                 | `char[25]`                         | 25 × 1             | 1            | **25**               | 25                    | 25            |
| loop_functions_extern.h:489           | `pageTextLong2`                                 | `char[200]`                        | 200 × 1            | 1            | **200**              | 200                   | 200           |
| esp32_flash.cpp or nrf52_flash.cpp    | `meshcom_settings`                              | `s_meshcom_settings`               | 1 struct           | ~1900        | **~1900**            | ~1900                 | **~1900**     |
| esp32/esp32_main.cpp:369–370 (HELTEC) | `u8g2_1`, `u8g2_2`                              | U8G2 instances                     | —                  | ~256 each    | ~512                 | ~512                  | —             |
| instrument.cpp:47                     | `s_sect` (INSTRUMENT)                           | `SectionStat[16]`                  | 16 × 20            | 20           | **320** (if enabled) | 320                   | 320           |
| instrument.cpp:71 (ESP32 only)        | `s_rtc`                                         | `InstrRtcCarry` (RTC_NOINIT)       | 1 × 28             | 28           | **28**               | 28                    | —             |
| loop_functions_extern.h:515           | `pendingDisplayMsg`                             | `aprsMessage`                      | 1 struct           | ~500         | **~500**             | ~500                  | ~500          |

---

## Struct Size Estimates

### `aprsMessage` (src/aprs_structures.h)

Members: uint16_t, char, unsigned int, uint8_t×5, bool×4, String×6, unsigned int, uint8_t×2, char  
**Estimated size**: ~500 B (6 Strings @ 16 B each = 96, + other fields ~400)

### `s_meshcom_settings` (esp32_flash.h or nrf52/WisBlock-API.h)

Major members:

- Markers, EUI: 10 B
- Call strings (node_call, node_short): 16 B
- Position (lat, lon, alt, sym): 25 B
- WiFi (ossid, opwd, ssid, pwd): 177 B
- Radio params: 24 B
- Sensor strings (atxt, extern, etc.): 280 B
- Network (ownip, owngw, ownms, owndns, ownntp, etc.): 220 B
- Misc (mcp17t[16][16], gcb[6], parm/unit/format/eqns/values, etc.): ~500 B
- Non-flash runtime fields (node_ip, node_dns, node_gw, node_subnet, node_ntp, node_update, node_parm_1/t/id): ~550 B
- T-Deck-only fields (2 Strings + bools): 32+ B (conditional)

**Estimated total**: ~1900 B

### `externQueueEntry` (extudp_functions.cpp)

Members: `uint8_t buffer[500]` + `uint16_t buflen` + `int16_t rssi` + `int8_t snr` + `char src_type[8]` + `std::atomic<bool> used`  
**Size**: ~514 B per entry

---

## Candidates for Flash Relocation (const/static lookup tables)

No const-qualified lookup tables were found in the scope; all major arrays are runtime ringbuffers or configuration structs that legitimately occupy RAM for initialization and modification.

---

## Static Local Variables (Occupy .bss)

| File:Line                         | Name                                                          | Type                                | Size         |
| --------------------------------- | ------------------------------------------------------------- | ----------------------------------- | ------------ |
| loop_functions.cpp:312 (WP_DISP)  | `wpMsgFont()::f`                                              | static GFXfont                      | ~48          |
| loop_functions.cpp:313 (WP_DISP)  | `wpMsgFont()::ready`                                          | static bool                         | 1            |
| capture_functions.cpp:44          | `cap_serial_last`                                             | static unsigned long                | 4            |
| capture_functions.cpp:57          | `cap_writing`                                                 | static std::atomic_flag             | 1            |
| capture_functions.cpp:39–41       | `cap_head`, `cap_tail`, `cap_dropped`                         | static std::atomic                  | 4+4+4 = 12   |
| instrument.cpp:34–36 (INSTRUMENT) | `s_flush_n`, `s_flush_us`, `s_flush_max`                      | static                              | 4+8+4 = 16   |
| instrument.cpp:38–41 (INSTRUMENT) | `s_loop_n`, `s_loop_us`, `s_loop_max`, `s_loop_last`          | static                              | 4+8+4+4 = 20 |
| instrument.cpp:48–51 (INSTRUMENT) | `s_iter_worst_*`, `s_gap_reports`                             | static                              | 4+8+4+4 = 20 |
| net_console.cpp:47                | `s_password`                                                  | static char[15]                     | 15           |
| web_functions.cpp:68              | `s_noIpLogged`                                                | static bool (in startWebserver)     | 1            |
| txring_functions.cpp:65–67        | `s_have_marker`, `s_last_marker_ms`, `s_refused_since_marker` | static (in logTxRefuseUnconfigured) | 1+4+4 = 9    |
| udp_functions.cpp:103–110         | `s_udpRxCount`, `s_udpTxCount`, `s_udpTxFail`, etc.           | static uint32_t[5], IPAddress       | ~32          |

---

## Heap-at-Boot Allocations

| Source                         | Description                        | Typical Size          | Notes                                                                                                         |
| ------------------------------ | ---------------------------------- | --------------------- | ------------------------------------------------------------------------------------------------------------- |
| FreeRTOS Queue (bleQueue)      | NimBLE RX payload queue            | Variable              | Created in esp32_main.cpp (line 293, ESP32) or nrf52_ble.cpp (nRF52); queue size is not static, managed by OS |
| Arduino_GFX (ESP32 T-Connect)  | Display framebuffer                | ~8 KB (480×272×1 bpp) | Allocated via `new` in esp32_main.cpp:97–104 (T_CONNECT_PRO only)                                             |
| U8g2 display buffer            | OLED framebuffer (1-page or full)  | 128–1024 B            | Allocated within U8g2_* constructor; varies by board                                                          |
| NimBLE/BLE device buffers      | BLE stack allocation               | ~20–50 KB             | Depends on stack depth and active connections                                                                 |
| WiFi/Ethernet drivers          | Platform network stack             | ~50–150 KB            | Managed by ESP-IDF / nRF52 SDK; not owned by firmware                                                         |
| UPD socket buffers             | Socket I/O (WiFiUDP / EthernetUDP) | ~2–4 KB per socket    | Typically 2–3 sockets (UDP, EXTERN, etc.)                                                                     |
| LittleFS / SD card cache       | File system buffers                | ~4 KB+                | Only on boards with storage                                                                                   |
| TinyGPSPlus object             | GPS parser state                   | ~200 B                | Global `gps` instance in gps_functions.cpp                                                                    |
| mheardPathLen[MAX_MHPATH] ring | Dedup/mheard management            | ~100 B                | Accounted in static table above                                                                               |

---

## Summary Totals (ESP32-S3/nRF52)

| Category                                             | Total Bytes |
| ---------------------------------------------------- | ----------- |
| **Ringbuffers** (RX, TX, BLE)                        | ~27,800     |
| **Mheard buffers** (calls, paths, epoch/lat/lon/alt) | ~9,800      |
| **Dedup ring**                                       | 500         |
| **Capture ring**                                     | 768         |
| **Display buffers** (page arrays)                    | ~5,115      |
| **Config/message buffers**                           | ~2,700      |
| **IP/network globals**                               | ~50         |
| **String objects (stack only; heap separate)**       | ~224        |
| **meshcom_settings struct**                          | ~1,900      |
| **Instrument/debug statics**                         | ~100        |
| **Misc/small globals**                               | ~500        |
| **SUBTOTAL (core firmware)**                         | **~48,500** |

_Note: This excludes:_

- _Heap-allocated strings (dynamic payload)_
- _FreeRTOS task stacks (typically 4–16 KB per task)_
- _Network driver buffers (50–150 KB)_
- _Display framebuffers (board-dependent)_
- _Third-party library heaps (NimBLE, RadioLib, etc.)_

**Estimated firmware-owned static RAM (data + bss): ~48–50 KB on ESP32-S3 at initialization.**

The three largest contributors are:

1. **BLE output buffers** (~12.2 KB): `BLEtoPhoneBuff` + `BLEComToPhoneBuff`
2. **Ringbuffer network I/O** (~15.2 KB): main TX/RX rings + UDP out
3. **Mheard storage** (~9.8 KB): heard/path rings + epoch/coordinate arrays

Classic ESP32 savings: ~20 KB (smaller ring/mheard sizes).

---

<!-- source: S3-callsign-storage.md -->

## Callsign Storage Audit: MeshCom Firmware

**Scope:** All RAM storage locations for amateur radio callsigns (own call, source, destination, path, mheard, ACK attribution, dedup, contact/group/gateway lists) across ESP32-S3, classic ESP32, and nRF52840.

---

## 1. Persistent Settings Struct

### ESP32 (esp32_flash.h)

| File:Line         | Container          | Field              | Bytes per slot | Slots (Platform) | Total Bytes | Write Method                            | Compare Method                     |
| ----------------- | ------------------ | ------------------ | -------------- | ---------------- | ----------- | --------------------------------------- | ---------------------------------- |
| esp32_flash.h:17  | s_meshcom_settings | node_call[10]      | 10             | 1                | 10          | memcpy (nrf52_flash.cpp:memcpy at L~56) | strcmp (lora_functions.cpp:L~2261) |
| esp32_flash.h:143 | s_meshcom_settings | node_lora_call[10] | 10             | 1                | 10          | (not found, storage only)               | strcmp (loop_functions.cpp:L~1107) |
| esp32_flash.h:180 | s_meshcom_settings | node_via[40]       | 40             | 1                | 40          | strlen+concat (via_functions.cpp:L~101) | strlen (via_functions.cpp:L~101)   |
| esp32_flash.h:187 | s_meshcom_settings | node_pingcall[10]  | 10             | 1                | 10          | (not found, storage only)               | (not found)                        |

### nRF52 (WisBlock-API.h — three definitions for different board combos)

| File:Line          | Container          | Field              | Bytes per slot | Slots (Platform) | Total Bytes | Write Method                            | Compare Method                     |
| ------------------ | ------------------ | ------------------ | -------------- | ---------------- | ----------- | --------------------------------------- | ---------------------------------- |
| WisBlock-API.h:187 | s_meshcom_settings | node_call[10]      | 10             | 1                | 10          | memcpy (nrf52_flash.cpp:L~56)           | strcmp (nrf52/nrf_eth.cpp:L~1189)  |
| WisBlock-API.h:305 | s_meshcom_settings | node_lora_call[10] | 10             | 1                | 10          | memcpy (nrf52_flash.cpp:L~62)           | strcmp (loop_functions.cpp:L~1107) |
| WisBlock-API.h:341 | s_meshcom_settings | node_via[40]       | 40             | 1                | 40          | strlen+concat (via_functions.cpp:L~101) | strlen (via_functions.cpp:L~101)   |
| WisBlock-API.h:348 | s_meshcom_settings | node_pingcall[10]  | 10             | 1                | 10          | (not found)                             | (not found)                        |

**Subtotal (Settings):** 70 bytes per platform instance

---

## 2. Mheard Ringbuffer (Heard Callsigns)

### Main Mheard Cache (loop_functions_extern.h:439–442)

| File:Line                   | Container    | Field                        | Bytes per slot    | Slots (Config)   | Total Bytes | Write Method                               | Compare Method                             |
| --------------------------- | ------------ | ---------------------------- | ----------------- | ---------------- | ----------- | ------------------------------------------ | ------------------------------------------ |
| loop_functions_extern.h:440 | Global array | mheardCalls[MAX_MHEARD][10]  | 10                | 30–80 (platform) | 300–800     | memcpy (mheard_functions.cpp:L~221)        | (read-only in mheard)                      |
| loop_functions_extern.h:441 | Global array | mheardEpoch[MAX_MHEARD]      | 8 (unsigned long) | 30–80            | 240–640     | direct assign (mheard_functions.cpp:L~207) | compare (mheard_functions.cpp:L~228)       |
| loop_functions_extern.h:439 | Global array | mheardBuffer[MAX_MHEARD][60] | 60                | 30–80            | 1800–4800   | memcpy (mheard_functions.cpp:L~218)        | memcpy decode (mheard_functions.cpp:L~154) |

Metadata: mheardLat, mheardLon, mheardAlt, mheardMillis (uint32_t), mheardNCount per slot, but **no callsigns in metadata**.

### Mheard Path Cache (loop_functions_extern.h:444–447)

| File:Line                   | Container    | Field                             | Bytes per slot | Slots (Config)    | Total Bytes | Write Method                        | Compare Method                              |
| --------------------------- | ------------ | --------------------------------- | -------------- | ----------------- | ----------- | ----------------------------------- | ------------------------------------------- |
| loop_functions_extern.h:444 | Global array | mheardPathCalls[MAX_MHPATH][10]   | 10             | 40–100 (platform) | 400–1000    | memcpy (mheard_functions.cpp:L~257) | (read-only path cache)                      |
| loop_functions_extern.h:446 | Global array | mheardPathBuffer1[MAX_MHPATH][52] | 52             | 40–100            | 2080–5200   | memcpy (mheard_functions.cpp:L~258) | substring compare (via_functions.cpp:L~124) |
| loop_functions_extern.h:445 | Global array | mheardPathEpoch[MAX_MHPATH]       | 8              | 40–100            | 320–800     | direct assign                       | compare (mheard_functions.cpp:L~323)        |

Metadata: mheardPathMillis (uint32_t), mheardPathLen (uint8_t) per slot.

**mheardBuffer format:** 60 bytes per slot, binary encoded with embedded source/destination callsigns (format: `callsign|date|time|sourcecall|sourcepath|destpath|type|hw|mod|rssi|snr|dist|pathlen|mesh|ncount`). Callsigns are variable-length within the 60-byte limit.

**Subtotal (Mheard):**

- Explicit callsigns: (300–800) + (400–1000) = 700–1800 bytes
- Embedded in buffers: 1800–4800 + 2080–5200 = 3880–10000 bytes
- Total: 4580–11800 bytes (platform-dependent, MAX_MHEARD 30/80, MAX_MHPATH 40/100)

---

## 3. TX Ring (Outgoing Message Queue)

### Main TX Ring (loop_functions_extern.h:197)

| File:Line                   | Container    | Field                                   | Bytes per slot | Slots (Config)   | Total Bytes | Write Method                               | Compare Method                    |
| --------------------------- | ------------ | --------------------------------------- | -------------- | ---------------- | ----------- | ------------------------------------------ | --------------------------------- |
| loop_functions_extern.h:197 | Global array | ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5] | 260 (255+5)    | 10–20 (platform) | 2600–5200   | memcpy frame (txring_functions.cpp:L~501)  | memcmp (lora_functions.cpp:L~597) |
| txring_functions.cpp:25     | Global array | ringSource[MAX_RING]                    | 1              | 10–20            | 10–20       | direct assign (txring_functions.cpp:L~530) | (read-only)                       |

**ringBuffer format:** Byte 0 = length, Byte 1 = status, Byte 2 = msg_type, Bytes 3–6 = msg_id, Bytes 8+ = encoded path+frame. Callsigns are embedded at variable offsets within the encoded payload (format `SOURCE>DEST:...`).

**Path encoding at ringBuffer[slot][8+]:** SOURCE callsign (variable, typically 6–9 chars) + '>' + DEST callsign (variable) + ':' + payload.

**Subtotal (TX Ring):** 2600–5200 bytes

---

## 4. Dedup Ring (Receive Deduplication)

### RX Dedup Ring (dedup_functions.h:38)

| File:Line            | Container    | Field                               | Bytes per slot  | Slots (Config)    | Total Bytes | Write Method                                | Compare Method                    |
| -------------------- | ------------ | ----------------------------------- | --------------- | ----------------- | ----------- | ------------------------------------------- | --------------------------------- |
| dedup_functions.h:38 | Global array | ringBufferLoraRX[MAX_DEDUP_RING][5] | 5 (msg_id+flag) | 60–100 (platform) | 300–500     | direct assign (dedup_functions.cpp:L~66–70) | memcmp (dedup_functions.cpp:L~28) |

**Format:** Bytes 0–3 = msg_id (little-endian), Byte 4 = server flag.  
**NO callsigns stored.** Dedup uses msg_id only.

**Subtotal (Dedup):** 300–500 bytes (no callsigns)

---

## 5. BLE Ringbuffers (Phone Communication)

### BLE to Phone (loop_functions_extern.h:267)

| File:Line                   | Container    | Field                                         | Bytes per slot | Slots (MAX_RING) | Total Bytes | Write Method             | Compare Method          |
| --------------------------- | ------------ | --------------------------------------------- | -------------- | ---------------- | ----------- | ------------------------ | ----------------------- |
| loop_functions_extern.h:267 | Global array | BLEtoPhoneBuff[MAX_RING][MAX_MSG_LEN_PHONE+5] | 249 (244+5)    | 10–20            | 2490–4980   | memcpy (addBLEOutBuffer) | (not directly compared) |

**Format:** Contains encoded frames including mheardLine structures. Callsigns embedded in JSON or binary encoding.

### BLE Commands to Phone (loop_functions_extern.h:272)

| File:Line                   | Container    | Field                                            | Bytes per slot | Slots (MAX_RING) | Total Bytes | Write Method                       | Compare Method |
| --------------------------- | ------------ | ------------------------------------------------ | -------------- | ---------------- | ----------- | ---------------------------------- | -------------- |
| loop_functions_extern.h:272 | Global array | BLEComToPhoneBuff[MAX_RING][MAX_MSG_LEN_PHONE+5] | 249            | 10–20            | 2490–4980   | (various, e.g., addBLECommandBack) | (not compared) |

**ACK Attribution (Inline):** ack_attribution.h:122, buildAckPhoneFrame() writes up to ACK_ATTR_CALL_MAX (10 bytes) callsign at offset 7 in a 17-byte frame.

**Subtotal (BLE):** 4980–9960 bytes (platform-dependent)

---

## 6. UDP Ringbuffer (UDP TX from LoRa RX)

### UDP Out (loop_functions_extern.h:260)

| File:Line                   | Container    | Field                                              | Bytes per slot | Slots (Config)   | Total Bytes | Write Method                      | Compare Method              |
| --------------------------- | ------------ | -------------------------------------------------- | -------------- | ---------------- | ----------- | --------------------------------- | --------------------------- |
| loop_functions_extern.h:260 | Global array | ringBufferUDPout[MAX_RING_UDP][UDP_TX_BUF_SIZE+20] | 275 (255+20)   | 10–20 (platform) | 2750–5500   | memcpy (udp_functions.cpp:L~1750) | (not compared in this file) |

**Format:** Same as TX ring; embedded callsigns in encoded frames.

**Subtotal (UDP):** 2750–5500 bytes

---

## 7. Ephemeral Structures (Per-Frame Processing)

### aprsMessage (aprs_structures.h:8–34)

| File:Line            | Container          | Field                | Type   | Write Method                         | Compare Method       |
| -------------------- | ------------------ | -------------------- | ------ | ------------------------------------ | -------------------- |
| aprs_structures.h:21 | struct aprsMessage | msg_source_call      | String | snprintf/decode (lora_functions.cpp) | == operator (String) |
| aprs_structures.h:24 | struct aprsMessage | msg_destination_call | String | snprintf/decode                      | == operator          |
| aprs_structures.h:20 | struct aprsMessage | msg_source_path      | String | snprintf/decode                      | indexOf/==           |
| aprs_structures.h:23 | struct aprsMessage | msg_destination_path | String | snprintf/decode                      | indexOf/==           |
| aprs_structures.h:26 | struct aprsMessage | msg_gateway_call     | String | snprintf/decode                      | == operator          |

**Allocations:** Each String holds a callsign decoded from the wire frame. Not persistent; created per RX event, destroyed after processing.

### mheardLine (aprs_structures.h:68–86)

| File:Line            | Container         | Field              | Type   | Write Method                            | Compare Method |
| -------------------- | ----------------- | ------------------ | ------ | --------------------------------------- | -------------- |
| aprs_structures.h:70 | struct mheardLine | mh_callsign        | String | = operator (mheard_functions.cpp:L~180) | == operator    |
| aprs_structures.h:73 | struct mheardLine | mh_sourcecallsign  | String | = operator                              | == operator    |
| aprs_structures.h:74 | struct mheardLine | mh_sourcepath      | String | = operator                              | indexOf        |
| aprs_structures.h:75 | struct mheardLine | mh_destinationpath | String | = operator                              | == operator    |

**Allocations:** Created during mheard encode/decode, ephemeral.

---

## 8. Own Message Tracking

### Own TX ID Ring (loop_functions_extern.h:166)

| File:Line                   | Container    | Field                   | Bytes per slot  | Slots (MAX_RING) | Total Bytes | Write Method                       | Compare Method             |
| --------------------------- | ------------ | ----------------------- | --------------- | ---------------- | ----------- | ---------------------------------- | -------------------------- |
| loop_functions_extern.h:166 | Global array | own_msg_id[MAX_RING][5] | 5 (msg_id+flag) | 10–20            | 50–100      | direct assign (loop_functions.cpp) | memcmp (ack_attribution.h) |

**Format:** Same structure as dedup ring; tracks own messages for ACK matching.  
**NO callsigns stored.**

---

## 9. Configuration Value Length Enforcement

| File:Line                  | Symbol                    | Value        | Usage                                                                          |
| -------------------------- | ------------------------- | ------------ | ------------------------------------------------------------------------------ |
| configuration_global.h:162 | MAX_CALL_LEN              | 20           | Placeholder; not actually enforced in storage (fields are fixed 10/40 bytes)   |
| ack_attribution.h:32       | ACK_ATTR_CALL_MAX         | 10           | Max callsign length in ACK attribution frame; enforced at buildAckPhoneFrame() |
| ack_attribution.h:103      | check in ackAttrCallLen() | 10-byte scan | Validates caller-supplied string, rejects if > 10 or non-[A-Z0-9-]             |

**Actual lengths:**

- **Stored:** node_call[10], node_lora_call[10], node_pingcall[10], mheardCalls[10], mheardPathCalls[10]
- **Via:** node_via[40] (list of semicolon-separated callsigns)
- **Embedded:** ringBuffer/mheardBuffer/mheardPathBuffer entries hold variable-length callsigns in encoded frames

---

## 10. String (Arduino Heap) Usage

**Files where `String` callsigns are allocated per-frame:**

| File:Line                    | String Variable                                        | Type               | Lifecycle                                                                          |
| ---------------------------- | ------------------------------------------------------ | ------------------ | ---------------------------------------------------------------------------------- |
| aprs_structures.h:20–26      | aprsMessage.msg___call / msg___path / msg_gateway_call | String             | Per RX: allocated at frame decode, destroyed after processing                      |
| aprs_structures.h:70, 73–75  | mheardLine.mh_*                                        | String             | Per mheard update: allocated, then memcpy'd to mheardBuffer/mheardCalls, destroyed |
| aprs_functions.cpp (various) | strPath, strSourceCall, strDest                        | String             | Local scope (temporary)                                                            |
| via_functions.cpp:L~103      | aprsmsg.msg_destination_path                           | String             | Per message (not separate alloc; part of aprsMessage)                              |
| loop_functions.cpp (various) | pageText*, pageLastText*                               | String or char[25] | Display cache (callsigns for UI)                                                   |

**Heap impact:** Callsigns are allocated as Arduino String heap objects during frame processing. On a node with heavy traffic, dozens of String allocations per second occur at frame RX/decode and are freed immediately after processing. No persistent callsign heap storage beyond the ringbuffers and settings struct.

---

## 11. Structure Layouts (Byte Estimates)

### aprsMessage (estimated sizeof)

```
uint16_t msg_len                  2
char payload_type                 1
uint32_t msg_id                   4
uint8_t max_hop + bools           8
String msg_source_path            24 (pointer + heap allocation)
String msg_source_call            24
String msg_source_last            24
String msg_destination_path       24
String msg_destination_call       24
String msg_payload                24
String msg_gateway_call           24
uint32_t msg_fcs                  4
uint8_t msg_source_hw + others   16
------
Estimated total:                ~228 bytes (+ heap for 6 String objects)
```

### mheardLine (estimated sizeof)

```
String mh_callsign                24 (pointer + heap)
String mh_date                    24
String mh_time                    24
String mh_sourcecallsign          24
String mh_sourcepath              24
String mh_destinationpath         24
String mh_path_payload            24
char mh_payload_type              1
uint8_t mh_hw, mh_mod             2
int16_t mh_rssi                   2
int8_t mh_snr                     1
double mh_dist                    8
uint8_t mh_path_len, mh_mesh      2
uint8_t mh_ncount                 1
------
Estimated total:                ~208 bytes (+ ~6×24 B heap for Strings)
```

### s_meshcom_settings (partial, callsign-relevant fields)

```
char node_call[10]               10
char node_lora_call[10]          10
char node_via[40]                40
char node_pingcall[10]           10
------
Callsign fields:                 70 bytes
(Full struct ~1200 bytes, but only 70 relevant to callsigns)
```

---

## 12. Redundant Storage (Same Callsign Across Multiple Tables)

### High Redundancy Patterns

1. **Mheard → TX Ring → BLE:** A heard callsign (mheardCalls[i]) is typically:
   - Stored in mheardCalls[i] (10 B)
   - Appears in mheardBuffer[i] at variable offset (embedded in 60 B slot)
   - When relayed, encoded into ringBuffer[slot] as source callsign (embedded in 260 B slot)
   - When sent to phone, re-encoded into BLEtoPhoneBuff (embedded in 249 B slot)
   - **Redundancy: ~4 copies of the same 10-byte callsign across three ringbuffers**

2. **ACK Attribution:** Same callsign stored in:
   - Original frame (source_call in ringBuffer)
   - mheardCalls[i] (if heard locally)
   - own_msg_id[ring][5] (only msg_id, not callsign)
   - ACK phone frame (10 B max at ack_attribution.h:135)

3. **Via List (node_via[40]):** Stores semicolon-separated list of relay callsigns:
   - Encoded in node_via (40 B)
   - Used in path construction (checkVia, via_functions.cpp:L~101)
   - Re-encoded into msg_destination_path (String) per message
   - Embedded in ringBuffer per message (encoded frame)

### Conclusion on Redundancy

A single "heard" callsign (10 B) can be stored 3–5 times across independent ringbuffers within a 5-minute window if the message is relayed and echoed to the phone. **No deduplication attempted**; each table independently copies the full callsign.

---

## 13. Callsign Comparison Operations

| File:Line                  | Operation                                            | Pattern                     | Scope               |
| -------------------------- | ---------------------------------------------------- | --------------------------- | ------------------- |
| lora_functions.cpp:L~2261  | strcmp(destination_call, meshcom_settings.node_call) | exact match                 | routing decision    |
| lora_functions.cpp:L~2261  | strcmp(destination_call, "*")                        | broadcast check             | routing decision    |
| via_functions.cpp:L~82     | indexOf(meshcom_settings.node_call)                  | substring search            | path parsing        |
| via_functions.cpp:L~124    | strncpy(cMH, mheardCalls[iset], 10)                  | copy + NUL-term             | local var init      |
| ack_attribution.h:95–111   | ackAttrCallLen(call) loop                            | char-by-char scan [A-Z0-9-] | validation          |
| ack_attribution.h:135      | memcpy(out+7, call, n)                               | buffer copy                 | frame build         |
| dedup_functions.cpp:L~28   | memcmp(compBuffer, ringBuffer[ib], 4)                | 4-byte msg_id               | duplicate detection |
| mheard_functions.cpp:L~221 | memcpy(mheardCalls[ipos], call.c_str(), size)        | C-string to array           | cache write         |
| configuration_global.h:22  | memcmp(call, "XX0XXX", 6)                            | prefix check                | factory test        |

---

## 14. Summary: Total Callsign RAM Usage Per Platform

### Sizes Used (from configuration_global.h)

| Platform      | MAX_MHEARD | MAX_MHPATH | MAX_RING | MAX_DEDUP_RING | MAX_RING_UDP |
| ------------- | ---------- | ---------- | -------- | -------------- | ------------ |
| ESP32-S3      | 80         | 100        | 20       | 100            | 20           |
| Classic ESP32 | 30         | 40         | 20       | 70             | 20           |
| nRF52840      | 80         | 100        | 20       | 100            | 20           |
| XML/SBUFFER   | 50         | 50         | 20       | 60             | 20           |

### Calculated Totals (Explicit Callsign Buffers Only; Excludes Embedded in Payload Frames)

#### ESP32-S3 (320 KB SRAM)

```
Settings:
  node_call + node_lora_call + node_pingcall + node_via:  70 B

Mheard (explicit callsigns):
  mheardCalls[80][10]:                                   800 B
  mheardPathCalls[100][10]:                            1,000 B
  mheardBuffer[80][60] (embedded):                     4,800 B
  mheardPathBuffer1[100][52] (embedded):               5,200 B

TX Ring:
  ringBuffer[20][260]:                                 5,200 B
  ringSource[20][1]:                                      20 B

BLE Ringbuffers:
  BLEtoPhoneBuff[20][249]:                             4,980 B
  BLEComToPhoneBuff[20][249]:                          4,980 B

UDP Ringbuffer:
  ringBufferUDPout[20][275]:                           5,500 B

Dedup (no callsigns):
  ringBufferLoraRX[100][5]:                              500 B

Own Message ID (no callsigns):
  own_msg_id[20][5]:                                      100 B

Ephemeral Strings per frame:
  aprsMessage Strings (6×~24):                        ~144 B
  mheardLine Strings (4×~24):                          ~96 B

TOTAL CALLSIGN STORAGE:          ~33.9 KB
```

#### Classic ESP32 (~160 KB DRAM, ~75 KB firmware code buffer)

```
Settings:                                                70 B
Mheard:
  mheardCalls[30][10]:                                   300 B
  mheardPathCalls[40][10]:                               400 B
  mheardBuffer[30][60]:                                1,800 B
  mheardPathBuffer1[40][52]:                           2,080 B

TX Ring:
  ringBuffer[20][260]:                                 5,200 B
  ringSource[20][1]:                                      20 B

BLE:                                                   9,960 B

UDP:                                                   5,500 B

Dedup:                                                   350 B

Own IDs:                                                 100 B

Ephemeral:                                            ~240 B

TOTAL CALLSIGN STORAGE:          ~25.8 KB
```

#### nRF52840 (256 KB RAM, 64 KB + 64 KB app regions)

```
(Identical to ESP32-S3, as nRF52 uses same MAX_* config as S3)

TOTAL CALLSIGN STORAGE:          ~33.9 KB
```

---

## 15. Findings & Observations

### Callsign Length Enforcement

- **Declared max:** `MAX_CALL_LEN` = 20 (configuration_global.h:162), but **never used** for buffer sizing.
- **Actual storage:** Fixed 10 bytes (node_call, node_lora_call, node_pingcall, mheardCalls, mheardPathCalls).
- **Via list:** Fixed 40 bytes (node_via); semicolon-separated list of variable-length callsigns.
- **Encoded frames:** Callsigns embedded as variable-length strings within fixed-size frame buffers (ringBuffer, mheardBuffer, etc.).
- **ACK attribution:** Explicitly limited to 10 bytes (ACK_ATTR_CALL_MAX, ack_attribution.h:32).
- **Enforcement:** Validation occurs at ackAttrCallLen() (ack_attribution.h:95–111) and at isNodeUnconfigured() (configuration_global.h:18–27); both assume/enforce 6–10 byte prefixes.

### String Usage

- **No persistent String storage** for callsigns in ringbuffers or settings.
- **Ephemeral String objects** are created per RX frame (aprsMessage), per mheard update (mheardLine).
- **Heap churn:** Typical frame processing involves 6–7 String allocations (source_call, dest_call, paths, payload, gateway_call) and immediate deallocation; no memory leak observed, but high allocation rate under traffic.

### Redundancy

- Same callsign copied 3–5 times across independent ringbuffers (mheardCalls → mheardBuffer → ringBuffer → BLEtoPhoneBuff).
- No cross-table deduplication (design intent: each consumer has independent full copy for O(1) access).

### Encoding Format

- Callsigns are **never stored as simple null-terminated strings in ringbuffer slots**; instead, they are **embedded in wire-format frames** (SOURCE>DEST: structure).
- Decoding happens at frame RX and at web display time (no preprocessing).

### Platforms with Reduced Buffers

- XML/SBUFFER: Smallest config (50 mheard, 50 path, 60 dedup).
- ENABLE_TBEAM (developer test only): Smallest (10 mheard, 10 path, 10 dedup, 10 ring).
- Both significantly reduce callsign storage due to buffer size reduction.

---

## 16. Numbers at a Glance

```
ESP32-S3 / nRF52840:     ~33.9 KB callsign storage (MAX_MHEARD=80, MAX_MHPATH=100)
Classic ESP32:           ~25.8 KB callsign storage (MAX_MHEARD=30, MAX_MHPATH=40)
XML/SBUFFER variant:     ~18.0 KB callsign storage (MAX_MHEARD=50, MAX_MHPATH=50, MAX_DEDUP_RING=60)
ENABLE_TBEAM (test):     ~5.2  KB callsign storage (MAX_MHEARD=10, MAX_MHPATH=10)
```

---

---

<!-- source: S4-prior-work.md -->

## S4 — Prior-Work Digest (RAM / DRY audit prep)

Sources: docs/archive/ram-opti.md, docs/archive/ram-comparison-20260514.md, docs/archive/ram-comparison-20260517.md,
docs/code-quality-2.0.md (C06/C16/C20/C26), docs/code-audit-20260712.md, docs/resume.md,
src/configuration_global.h:190-260. HEAD c51c5881, branch fork-main.
Note: `docs/release-notes.md` exists but has no RAM/DRAM/dedup/DRY hits worth citing separately —
the same items are covered via resume.md and the ram-* docs.

## 1. RAM proposals (ram-opti.md #1-12) — verified against current source

| #   | Proposal                                           | Status                                                       | Evidence                                                                                                                                                                                                                           | Remaining potential                                                                                                                                                                                                                       |
| --- | -------------------------------------------------- | ------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | `MAX_LOG` 20→10                                    | DONE                                                         | `configuration_global.h:216` `MAX_LOG 10` (ESP32-S3/RAK4630 branch)                                                                                                                                                                | none, shipped                                                                                                                                                                                                                             |
| 2   | `MAX_LOG` 20→5 (aggressive)                        | REJECTED / not taken                                         | Line 216 stopped at 10, not 5                                                                                                                                                                                                      | could still cut −1.3 kB, forensic tradeoff, never pursued                                                                                                                                                                                 |
| 3   | `MAX_RING` 30→20 (needs telemetry)                 | DONE                                                         | `configuration_global.h:214` `MAX_RING 20`; also line 236 classic-ESP32 branch `MAX_RING 20 (was 30, MEM-01)`                                                                                                                      | none — MEM-01 (resume.md 2026-08-30) did the telemetry-free version directly, both S3 and classic now at 20                                                                                                                               |
| 4   | `MAX_RING_UDP` 30→20 (needs telemetry)             | DONE                                                         | `configuration_global.h:217` `MAX_RING_UDP 20`; classic branch line 239 `MAX_RING_UDP 20 (was 25, MEM-01)`                                                                                                                         | none                                                                                                                                                                                                                                      |
| 5   | `web_header` String → `char[]`                     | DONE                                                         | `web_functions.cpp:536-587` — `static char web_header_collect[1024]` fills via bounded loop, only converted to `String web_header` (line 37, 585) once complete for the small number of `indexOf/substring` call sites that remain | the `String web_header` object itself still exists downstream (line 37, 590-634) — only the per-character accumulation loop was fixed, not full String elimination; could still shrink further but low value                              |
| 6   | `MAX_MHEARD` 120→80                                | DONE                                                         | `configuration_global.h:212` `MAX_MHEARD 80` (was 20/120, comment "85-124 H00 nodes observed")                                                                                                                                     | none                                                                                                                                                                                                                                      |
| 7   | `MAX_MHPATH` 150→100                               | DONE                                                         | `configuration_global.h:213` `MAX_MHPATH 100`                                                                                                                                                                                      | superseded partially by #12's 37-byte-trim recommendation, see below (OPEN)                                                                                                                                                               |
| 8   | `MAX_EXTERN_QUEUE` 4→2                             | DONE                                                         | `extudp_functions.cpp:64` `#define MAX_EXTERN_QUEUE 2`                                                                                                                                                                             | none                                                                                                                                                                                                                                      |
| 9   | APRS path concat de-stringed                       | OPEN                                                         | `lora_functions.cpp` ACK/DM path still uses `aprsmsg.msg_payload.substring(...)` / `String` ops (e.g. ~line 1032, 1066-1075) — no `char[]`/`snprintf` rewrite found                                                                | still on the table, ~50-line diff, medium risk per doc                                                                                                                                                                                    |
| 10  | U8g2 fonts forced into Flash                       | OPEN                                                         | fonts (`u8g2_font_10x20_mf`, `u8g2_font_6x10_mf`) still referenced via stock U8g2 API in `loop_functions.cpp`, `esp32/esp32_functions.cpp`, `nrf52/nrf52_functions.cpp` — no wrapper/PROGMEM extraction found                      | −6.6 kB DRAM / +6.6 kB flash still open, medium effort                                                                                                                                                                                    |
| 11  | IDF Kconfig bloat (FTM/coredump/err_msg)           | REJECTED                                                     | doc itself recommends against it ("nicht angehen") — prebuilt ESP-IDF, needs custom IDF build                                                                                                                                      | do-not-do, constrains new audit: don't re-propose                                                                                                                                                                                         |
| 12  | mheardPathBuffer1 hash-encoding / 37-byte trim     | PARTIAL — trim not applied, hashing rejected                 | `mheard_functions.cpp:66` still `unsigned char mheardPathBuffer1[MAX_MHPATH][52]` (52 B, not shrunk to 40 B); no `ipc > 37` truncation-based trim found (grep empty)                                                               | the doc's own recommended cheap win (shrink 52→40 B array, ~−1.0 kB S3/RAK, −0.4 kB classic) was never applied — candidate for new audit. Hashing/interning explicitly rejected (collision risk, no wire-format hash) — do not re-propose |
| T1  | T-Deck `audio` conditional link (−10 kB if unused) | OPEN                                                         | `esp32_audio.cpp` still declares global `Audio audio;` unconditionally (no `#ifdef ENABLE_AUDIO` found)                                                                                                                            | open, needs "is audio actually used" decision first                                                                                                                                                                                       |
| T2  | T-Deck `persisted_msgs` PSRAM allocator            | OPEN (documented later in resume.md as separately addressed) | `t-deck/lv_obj_functions.cpp` message-view trimming (TD-03/H1, resume.md line 962) capped view at 50 bubbles, but the underlying PSRAM-allocator rewrite for `persisted_msgs` itself was not confirmed in source scan              | partial mitigation shipped (view trim caps growth), full PSRAM allocator not verified done                                                                                                                                                |
| T5  | T-Deck `strMaps` String→const char*                | OPEN                                                         | `t-deck/tdeck_extern.cpp:21` still `String strMaps[MAX_MAP] = {...}`                                                                                                                                                               | trivial, ~60 B, never applied                                                                                                                                                                                                             |

**Phase-1 total claimed (−7.6 kB) is effectively DONE** (items 1,6,8, plus MAX_RING/MAX_RING_UDP
folded in via MEM-01 rather than the doc's own phase plan). Item 12's cheap 52→40 B trim and item 9
(APRS char[] rewrite) and item 10 (U8g2 fonts) are the standing OPEN items with the best
risk/effort ratio for a new pass.

## 2. Latest measured RAM/Flash (docs/archive/ram-comparison-20260517.md, dated 2026-05-17, HEAD e9edf0df — the newer of the two ram-comparison docs; no more recent snapshot exists in docs/)

| Target                            | RAM used                   | RAM total                                                             | %                                                                       | Flash used        | Flash total  | %     | Cliff?                                                                                                |
| --------------------------------- | -------------------------- | --------------------------------------------------------------------- | ----------------------------------------------------------------------- | ----------------- | ------------ | ----- | ----------------------------------------------------------------------------------------------------- |
| E22-DevKitC (classic ESP32)       | 123.448 B                  | 130.596 B (dram0_0_seg 124.580 B, 99.09%)                             | 23.2% linker-summary / **99.09% dram0_0_seg**                           | 1.614.061 B       | 3.403.776 B  | 47.4% | **YES — DRAM 99.09%, 1.128 B free**                                                                   |
| ttgo_tbeam (classic ESP32)        | 123.272 B                  | —                                                                     | 9.4% linker-summary / **99.98% iram0_0_seg**                            | 1.662.273 B       | 3.403.776 B  | 48.8% | **YES — IRAM 99.98%, 28 B free**                                                                      |
| heltec_wifi_lora_32_V3 (ESP32-S3) | 113.408 B                  | 327.680 B                                                             | 34.6%                                                                   | 1.492.589 B       | 3.403.776 B  | 43.9% | no, ~213 kB DRAM free                                                                                 |
| ttgo_tbeam_supreme (ESP32-S3)     | 113.776 B                  | 327.680 B                                                             | 34.7%                                                                   | 1.529.473 B       | 3.403.776 B  | 44.9% | no                                                                                                    |
| t_deck (ESP32-S3, 16MB flash)     | 131.852 B                  | 327.680 B                                                             | 40.2%                                                                   | 2.943.737 B       | 12.582.912 B | 23.4% | no headline %, but dram0_0_seg is 60.62% (209.648/345.856) — highest DRAM occupancy of all S3 targets |
| t_deck_plus                       | 131.852 B                  | 327.680 B                                                             | 40.2%                                                                   | 2.943.801 B       | 12.582.912 B | 23.4% | dram0_0_seg 60.62%, same as t_deck                                                                    |
| wiscore_rak4631 (nRF52840)        | 80.048 B (static BSS+Data) | 248.832 B (linker "RAM" region reports 99.14% incl. heap reservation) | 32.2% static / 99.14% linker-region (misleading, heap headroom present) | 566.832 B (.text) | 815.104 B    | 69.5% | no — static usage is fine, the 99.14% figure is heap-inclusive noise                                  |

**Cliffs (>85%) as of this snapshot:** E22-DevKitC `dram0_0_seg` 99.09%, `ttgo_tbeam` `iram0_0_seg`
99.98%. Both are classic-ESP32 (160 KB DRAM) targets — any new static allocation touching shared
code risks breaking their build. resume.md (2026-09-03, "MEM-04") reports this got _worse_ since:
`ttgo_tbeam` / `_SX1262` / `_SX1268` down to **20 bytes** IRAM free, `E22_XML-DevKitC` at **920
bytes** DRAM free (3.456 B IRAM) — filed as MEM-04, open, unresolved as of last mention. Treat
classic-ESP32/E22 as the hard constraint for any new RAM work; S3/RAK/nRF52 have headroom.

Note: this snapshot predates ~4 months of subsequent commits (through c51c5881); treat the
percentages as directionally right but stale — a fresh `ram-snapshot` skill run is advisable before
finalizing any new RAM proposal, since MEM-04 (resume.md, 2026-09-03) already shows classic-ESP32
degraded further since this doc.

## 3. C06 duplicate/copy-drift pair list (code-quality-2.0.md:166-197) + status

Verbatim detector pair list (`code-quality-2.0.md:186-193`):

| Pair (A ↔ B)                                | File:location A                 | File:location B                 |
| ------------------------------------------- | ------------------------------- | ------------------------------- |
| `getMeshComUDPpacket` ↔ `NrfETH::getUDP`    | `src/udp_functions.cpp`         | `src/nrf52/nrf_eth.cpp`         |
| `sendMeshComUDP` ↔ `sendUDP`                | `src/udp_functions.cpp`         | `src/nrf52/nrf52_main.cpp`      |
| `checkSerialCommand` ↔ `checkSerialCommand` | `src/esp32/esp32_main.cpp:4307` | `src/nrf52/nrf52_main.cpp:2895` |
| `startNetwork` ↔ `startETH`                 | (ESP32)                         | (nRF52)                         |
| `read_batt`/`battDetectUpdate` ↔ old impl   | `src/batt_functions.cpp`        | `src/batt_function_old.cpp`     |
| gateway block of esp32loop ↔ nrf52loop      | (ESP32)                         | (nRF52)                         |

Status of each, checked against current source:

- **DRY-20 (batt dual impl):** still OPEN/unresolved as a structural matter — both `batt_functions.cpp` (`#if defined(USE_NEW_BATT)`, line 13) and `batt_function_old.cpp` (`#ifndef USE_NEW_BATT`, line 6) are still live and board-partitioned exactly as code-audit-20260712.md found (12 vs 15 variants). Not unified. **DO NOT propose "delete the dead one" — neither is dead.**
- **DRY-21 (nrf_eth.cpp UDP-RX handler vs udp_functions.cpp):** still OPEN. `nrf_eth.cpp:570` sets `print_buff[5]=0x01` for the first ACK branch (and `0x02` later at line 581 for the ACK-of-DM case), while `udp_functions.cpp:419` only logs `print_buff[5]` without the earlier divergent-assignment pattern being visibly unified — the two paths were not merged into one shared handler. Treat as still-open duplication; needs a closer diff before reproposing the exact divergence claim, but the shared-handler refactor was not done.
- **DRY-22 (`checkSerialCommand` duplicated ESP32/nRF52):** still OPEN — two separate definitions confirmed (`esp32_main.cpp:4307`, `nrf52_main.cpp:2895`), not merged into `command_functions.cpp`.
- **C06 catalogue overall:** marked `[x19]` occurrences seen in the doc; this is a _known, tracked, still-largely-open_ category. Any new audit revisiting C06 should treat DRY-20/21/22 as still valid findings, not re-discoveries needing re-justification.

Other duplicate/dead-code findings from code-audit-20260712.md (Track B), status verified:

| ID       | Finding                                                       | Status                                           | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                               |
| -------- | ------------------------------------------------------------- | ------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DRY-23   | msg_id composition + ring-enqueue triple pasted 9×/16×        | OPEN                                             | no `enqueueTx(` or `nextMsgId(` helper found anywhere in `src/*.cpp`/`*.h` (grep empty)                                                                                                                                                                                                                                                                                                                                                |
| DRY-24   | 59 on/off command toggles duplicate block                     | OPEN                                             | no `handleToggle(` found in `command_functions.cpp`                                                                                                                                                                                                                                                                                                                                                                                    |
| DRY-25   | I2C bus-reset workaround pasted ~9×                           | **DONE**                                         | `configuration_global.h:189-196` now centrally defines `MC_I2C_NEEDS_BUS_RESET` with explicit comment "DRY-25" citing the fix (was pasted per-sensor-file with a `defined()` bug)                                                                                                                                                                                                                                                      |
| SIMP-26  | `commandAction()` ~4860-line function                         | OPEN                                             | not verified reduced; out of scope of this digest to re-measure line count, but no table-driven dispatch marker found                                                                                                                                                                                                                                                                                                                  |
| SIMP-27  | `loop_functions.cpp` mixes 5 subsystems                       | OPEN                                             | no `display_*.cpp`/`txring.cpp`/`beacon.cpp`/`strutils.cpp` split found                                                                                                                                                                                                                                                                                                                                                                |
| STATE-28 | boolean globals mirror `node_sset*` bitmasks                  | OPEN                                             | no typed-accessor settings module found                                                                                                                                                                                                                                                                                                                                                                                                |
| SIMP-29  | dead files `idf_component.yml.orig`, `src/code_review/`       | **DONE**                                         | neither file exists anymore (`ls` confirms absent)                                                                                                                                                                                                                                                                                                                                                                                     |
| SIMP-30  | `strSOFTSERAPP_ID` declared twice; int/float HDOP twins       | PARTIAL                                          | only one `strSOFTSERAPP_ID` extern remains (`loop_functions_extern.h:459`) — duplicate removed; HDOP int/float twin status not verified in this pass                                                                                                                                                                                                                                                                                   |
| ALT-31   | retransmit state packed in one byte + parallel `retryCount[]` | PARTIAL/OPEN                                     | `RING_STATUS_DONE`/`RING_STATUS_READY`/`RING_STATUS_EXT_PENDING` now named enum-like constants (`lora_functions.cpp:234,259,266,281`) instead of raw magic hex — some cleanup happened, but still one-byte-multiplexed status + separate `retryCount[]` array (line 283), not the proposed per-slot struct                                                                                                                             |
| ALT-32   | display capability as negative `#if` chains                   | OPEN                                             | no `HAS_U8G2_OLED`/`HAS_EPD_WP`/`HAS_TFT` capability-macro convention found in `loop_functions.cpp`; some variants define ad-hoc `HAS_TFT`/`HAS_TFT_114` (not the proposed unified positive-capability scheme)                                                                                                                                                                                                                         |
| ALT-33   | ring-size constants duplicated across 5-branch `#if` ladder   | **PARTIALLY MITIGATED, still a 4-branch ladder** | `configuration_global.h:198-240` still has the branch ladder (ENABLE_XML/SBUFFER merged into one branch per comment line 201-203, so down from 5 to 4 effective branches), but now has an explicit compile-time guard comment ("MUSS alle sechs Konstanten setzen... Compile-Fehler") reducing the _silent-inheritance_ risk the finding warned about. Not centralized to single-baseline+override as recommended, but risk mitigated. |
| ALT-34   | "node not configured" sentinel duplicated                     | OPEN                                             | no `isNodeConfigured()` helper found                                                                                                                                                                                                                                                                                                                                                                                                   |
| ALT-35   | `bOneButton` hijacked for display invalidation                | not verified                                     | out of scope of this digest                                                                                                                                                                                                                                                                                                                                                                                                            |

C16/C20/C26 (code-quality-2.0.md) are pattern catalogues, not single findings — no item-by-item
"done" list exists for them; they describe _classes_ of bug (copy-paste literal / dead code / DRAM
cliff) with detector scripts, still current as review checklists. C26 in particular directly names
`MAX_RING 30` in the classic-ESP32 branch as the historical DRAM-cliff root cause (now fixed via
MEM-01, see above) and the `resource_watch.py` DRAM-headroom gate as the standing mitigation
(confirmed still referenced in resume.md 2026-09-03 as "MEM-03 done" / "regions" subcommand).

## 4. Explicit rejections / "do not do" constraints for the new audit

- **ram-opti.md #11 (IDF Kconfig/custom sdkconfig rebuild):** explicitly rejected — "Nicht angehen,
  bevor Vorschläge 1–10 ausgeschöpft sind." Requires custom ESP-IDF build, breaks CI. Do not
  re-propose without exhausting cheaper options first.
- **ram-opti.md #12 hashing/interning of mheardPathBuffer1 callsigns:** explicitly rejected — no
  hop-hash exists in the wire format, hashing would need a live dictionary and risks silent
  collisions on a 2-byte hash across thousands of callsigns. Only the 37-byte physical trim (52→40
  B) survives as a recommendation, and that trim was never applied (see §1 item 12) — it's still a
  legitimate small win, just not the hashing idea.
- **MEMORY.md (session memory) "Dedup ring size settled":** do not raise `MAX_DEDUP_RING` — the
  safe window is a fixed 9-minute corridor (39.6–48.5 min); age by clock, not by count. Current
  values: 100 (S3/RAK4630, `configuration_global.h:215`), 70 (classic ESP32, line 237), 60
  (XML/SBUFFER, line 207), 10 (T-Beam dev-test, line 222). Do not propose changing these.
- **MEMORY.md "No repairs for reverted upstream":** drop fork fixes when upstream reverts the
  underlying feature; don't propose fixing dead code that exists only because upstream backed out a
  feature.
- **DRY-20 (batt dual implementation):** explicitly _not_ a dead-code removal candidate — both
  files are live across disjoint board sets (12 vs 15 variants). A DRY pass must unify, not delete
  either side.
- **MEMORY.md "resource_baseline.json stale":** +4.2 kB/+3.3 kB RAM deltas seen against an
  unchanged tree are known baseline drift, not real regressions — don't treat stale baseline diffs
  as new findings.
- **code-audit-20260712.md DRY-21/22 note:** any fix touching one side of a known ESP32/nRF52 pair
  (§3 table) must also be applied to (or explicitly justified against) the other side — this is a
  process constraint (C06 "Rule 1.0" gap), not a specific rejection, but it bears directly on how a
  DRY-fix wave should be scoped/gated.

## 5. Wire-contract documentation inventory (docs describing on-wire/on-air formats, not the audit's job to re-derive)

- **BLE JSON frame format:** `docs/ble-review.md` (full doc — MTU complex §4, app-source-code
  cross-check §5, three-stage test architecture §6) is the primary source-of-truth discussion;
  implementation lives in `src/*/ble_json_frame.h` referenced by C27 (`code-quality-2.0.md:895`,
  "the reference fix (`ble_json_frame.h`) existed and had been applied to one path"). Also see
  `docs/issue-ble-i-register-mtu-20260828.md` (I-register/MTU edge case) and
  `docs/issue-mh-json-size-budget-20260828.md` (mheard JSON size budget).
- **Net console protocol (port 2323):** `src/net_console.cpp` (header comment line 2: "Port 2323,
  single client, plaintext data, no TLS overhead"); command surface documented inline in
  `src/command_functions.cpp:818,2453,2472,5952` (`--netconsole on/off`). Client tool:
  `tools/hmac_connect.py` (per MEMORY.md "Net debug console port 2323"). No dedicated protocol-spec
  doc beyond the source comment; `docs/net-console` topic not present as its own file.
- **Mesh UDP (port 1799) / EXTUDP frame format:** `EXTERN_PORT 1799` defined at
  `src/configuration_global.h:167`. Format/behavior documented across `docs/ext_udp_telemetry.md`,
  `docs/bench-extudp-regression.md`, `docs/backpressure-protocol.md` (TX-side backpressure over the
  same path), and `docs/bug-N25-gps-baud-scan-watchdog.md` / `docs/mcp23017-digital-field.md`
  (peripheral-specific EXTUDP fields). Source of truth: `src/extudp_functions.cpp` (queue, framing,
  escaping — see C27 finding "EXTUDP escaped `\"`/`\\` by hand before ArduinoJson escaped them
  again").
- **Mesh LoRa/APRS on-air frame format** (not asked for by name but adjacent): `src/lora_functions.cpp`
  (`decodeAPRS`, `OnRxDone`) is the parser; no single spec doc found in this digest's read window —
  `docs/BACKLOG.md` and `docs/CHANGELOG-stability.md` reference frame-format changes piecemeal
  (item-numbered), not as a consolidated spec.
- **Reliability/transport-layer proposal (DM store-and-forward):** `docs/dm-store-and-forward-concept.md`
  and the 2026-09-09 HTML report (per MEMORY.md "DM transport concept HTML") describe a _proposed_
  extension to the wire contract (stable NNN, fresh msg_id per re-flood, presence-based delivery,
  custody via 0x41) — not yet implemented, relevant only if the new audit touches DM/ACK code paths.

No single consolidated "wire format spec" document exists; the contract is reconstructed from
source (`ble_json_frame.h`, `net_console.cpp`, `extudp_functions.cpp`, `lora_functions.cpp`) plus
scattered per-feature docs above. A DRY/RAM audit should not need to touch wire format at all —
flagged here only so the new audit doesn't accidentally propose a format-breaking buffer-size change
without checking these.

---

<!-- source: S5-wire-contract.md -->

## Wire Contract Audit: MeshCom Firmware v4.35t

Frozen symbols that must be byte-identical across any refactor. All offsets are 0-indexed, message types are hexadecimal unless noted.

---

## (a) BLE Protocol to Phone App

### Outbound (device → phone)

| File:Line                  | Function            | Direction | Buffer + Size                                                             | Frozen Format Details                                                                                                                |
| -------------------------- | ------------------- | --------- | ------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| src/phone_commands.cpp:50  | `sendToPhone()`     | OUT       | `toPhoneBuff[MAX_MSG_LEN_PHONE]` (300 bytes)                              | Type byte: 0x40 (text/position), 0x44 (JSON D), 0x91 (mheard). JSON starts at buf[1]. See line 110.                                  |
| src/phone_commands.cpp:148 | `sendComToPhone()`  | OUT       | `ComToPhoneBuff[MAX_MSG_LEN_PHONE]` (300 bytes)                           | Same framing as `sendToPhone()`                                                                                                      |
| src/loop_functions.cpp:582 | `addBLEOutBuffer()` | OUT       | `BLEtoPhoneBuff[MAX_RING][MAX_MSG_LEN_PHONE+5]` (20 slots × 305 bytes)    | Slot [0]: length, [1]: status byte (0x91, 0x44, or frame type), [2+]: payload. MTU 245 limit enforced at line 365 (config_global.h). |
| src/loop_functions.cpp:582 | `addBLEOutBuffer()` | OUT       | `BLEComToPhoneBuff[MAX_RING][MAX_MSG_LEN_PHONE+5]` (20 slots × 305 bytes) | Same structure                                                                                                                       |

### Inbound (phone → device)

| File:Line                    | Function                             | Direction | Buffer + Size                                                   | Frozen Format Details                                                                                                                                                                                                                                                   |
| ---------------------------- | ------------------------------------ | --------- | --------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| src/phone_commands.cpp:248   | `readPhoneCommand()`                 | IN        | `conf_data[MAX_MSG_LEN_PHONE]` (300 bytes)                      | **Message IDs (Msg Type byte [1]):** 0x10=Hello, 0x20=Timestamp, 0x50=Callsign, 0x55=WiFi SSID/PW, 0x70=Latitude, 0x80=Longitude, 0x90=Altitude, 0x95=APRS Symbols, 0xA0=Text Message, 0xF0=Save Settings. Structure: `[length 1B][Msg ID 1B][payload len 1B][payload]` |
| src/esp32/esp32_main.cpp:369 | `CharacteristicCallbacks::onWrite()` | IN        | Item enqueued to `bleQueue`, processed via `readPhoneCommand()` | Queue: 5 × `BleQueueItem{data[MAX_MSG_LEN_PHONE], length}`                                                                                                                                                                                                              |
| src/nrf52/nrf52_ble.cpp:264  | `bleuart_rx_callback()`              | IN        | Same as ESP32: queued, then `readPhoneCommand()`                | Queue: same structure                                                                                                                                                                                                                                                   |

### JSON Keys (BLE Data Messages, type 0x44)

Keys used in serialized JSON (src/command_functions.cpp):

- **"TYP"** — always present, identifies message type: "I" (info), "TM" (telemetry), "W" (weather), "IO" (IO), "SE" (sensor enum), "S1" (sensor 1), "S2" (sensor 2), "SW" (software), "SN" (settings new), "AN" (analog), "SA" (APRS settings), "G" (GPS), "MH" (mheard), "CONFFIN" (config finish)
- "FWVER", "CALL", "PARM", "UNIT", "TEMP", "TOFFI", "BME", "BMP", "SHUNT", "OWNIP", "OWNGW", "GW", "WS", "NOALL", "BLED", "APN", "AFC", "ATXT", "SYMID", "LAT", "LON", "DATE", "TIME", "RSSI", "SNR", "PATH", "HOP"
- Constraint: JSON payload max = BLE_JSON_PAYLOAD_MAX (244 bytes, src/configuration_global.h:369)

---

## (b) Net Debug Console (TCP Port 2323)

### Protocol

| File:Line                | Protocol Details                                                                                 |
| ------------------------ | ------------------------------------------------------------------------------------------------ |
| src/net_console.cpp:1-21 | **HMAC-SHA256 challenge-response authentication** (optional, if password configured)             |
| src/net_console.cpp:145  | Challenge: `"NONCE: <32 hex chars>\r\n"`                                                         |
| src/net_console.cpp:202  | Success: `"OK\r\nMeshCom Console\r\nType --help for commands\r\n"`                               |
| src/net_console.cpp:194  | Failure: `"FAIL\r\n"` + disconnect                                                               |
| src/net_console.cpp:271  | `MeshSerialClass::write()` — passthrough mirror of all Serial output to authenticated TCP client |

### Buffers & Serialization

| File:Line               | Function                   | Direction | Buffer + Size                | Frozen Format Details                                                                   |
| ----------------------- | -------------------------- | --------- | ---------------------------- | --------------------------------------------------------------------------------------- |
| src/net_console.cpp:42  | `s_hwSerial` (real Serial) | OUT       | Hardware serial + TCP mirror | Single-client TCP 2323; non-blocking write after auth; plaintext mirrored byte-for-byte |
| src/net_console.cpp:146 | Challenge buffer           | OUT       | `chalBuf[48]`                | Fixed: `"NONCE: " + 32 hex + "\r\n"` = 46 bytes                                         |
| src/net_console.cpp:156 | Response buffer            | IN        | `respBuf[72]`                | Reads up to 64 hex chars (HMAC) + \r or \n; timeout 30 s                                |

---

## (c) Mesh UDP Link to Server (Port 1799 / Local Port 1990)

### Outbound (gateway → server)

| File:Line                 | Function                  | Direction | Buffer + Size                                                     | Frozen Format Details                         |
| ------------------------- | ------------------------- | --------- | ----------------------------------------------------------------- | --------------------------------------------- |
| src/udp_functions.cpp:136 | `getMeshComUDP()` / `Udp` | OUT       | `incomingPacket[UDP_TX_BUF_SIZE]` (255 bytes) + optional trailing | No outbound from this function; inbound only. |

### Inbound (server → gateway)

| File:Line                 | Function                | Direction | Buffer + Size                                 | Frozen Format Details                                                                                                                                                                                                         |
| ------------------------- | ----------------------- | --------- | --------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| src/udp_functions.cpp:205 | `getMeshComUDPpacket()` | IN        | `inc_udp_buffer[UDP_TX_BUF_SIZE]` (255 bytes) | **4-byte indicators (lines 228-239):** "GATE" (0x47,0x41,0x54,0x45) = LoRa frame to send, "BEAT" (0x42,0x45,0x41,0x54) = heartbeat, "CONF" (0x43,0x4F,0x4E,0x46) = config provisioning. Payload after indicator at offset +4. |
| src/udp_functions.cpp:250 | Payload type dispatch   | —         | —                                             | After indicator, byte [4] is APRS payload type: 0x3A (text ':'), 0x21 (position '!'), 0x40 (HEY '@'), 0x41 (ACK 'A')                                                                                                          |

### CONF Frame Format (after "CONF" indicator, TM-39)

| Offset | Tag  | Length   | Content                                       | Frozen Details              |
| ------ | ---- | -------- | --------------------------------------------- | --------------------------- |
| +0     | 0x00 | 1        | Callsign length (max CONF_FRAME_CALL_MAX=9)   | Mandatory, must be first    |
| +1     | —    | variable | Callsign bytes                                | Null-terminated on parse    |
| —      | 0x01 | 1        | Shortname length (max CONF_FRAME_SHORT_MAX=5) | Optional                    |
| —      | —    | variable | Shortname bytes                               | Null-terminated on parse    |
| —      | 0x02 | 4        | Latitude (4 bytes little-endian, int32_t)     | Optional (src/conf_frame.h) |
| —      | 0x03 | 4        | Longitude (4 bytes little-endian, int32_t)    | Optional                    |
| —      | 0x04 | 4        | Altitude (4 bytes little-endian, int32_t)     | Optional                    |

---

## (d) EXTUDP Link to External Apps (Port 1799 for outbound, configurable for inbound)

### Outbound (gateway → external app)

| File:Line                    | Function               | Direction                       | Buffer + Size                    | Frozen Format Details                                                                                                                                                                                             |
| ---------------------------- | ---------------------- | ------------------------------- | -------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| src/extudp_functions.cpp:240 | `getExtern()`          | IN (receives from external app) | `incoming[]` (user-supplied)     | **JSON keys frozen:** "type" (string: "msg", "tele"), "dst" (string), "msg" (string, max 150 B), "temp", "hum", "press", "temp2", "qnh", "gasres", "co2" (floats for tele). See lines 286-315.                    |
| src/extern_notice_json.h:28  | `externNoticeJson()`   | OUT (sends notice frame)        | `out[out_len]` (caller-supplied) | **JSON keys (frozen):** "src_type" (literal "lora"), "type" (literal "msg"), "src", "dst", "msg", "msg_id" (hex string %08X), "firmware" (uint8_t), "fw_sub", "rssi", "snr" (lines 44-55)                         |
| src/extern_tele_json.h:30    | `externTeleJsonNode()` | OUT (gateway's own sensors)     | `out[out_len]` (caller-supplied) | **JSON keys (frozen):** "src_type" (literal "node"), "type" (literal "tele"), "src", "temp1", "temp2", "hum", "qfe", "qnh", "gas", "co2", conditional "din" (MCP23017 bits, 8-char string) (lines 42-54)          |
| src/extern_tele_json.h:59    | `externTeleJsonLora()` | OUT (relayed node's sensors)    | `out[out_len]` (caller-supplied) | **JSON keys (frozen):** "src_type" (literal "lora"), "type" (literal "tele"), "src", "batt" (int %), "temp1", "temp2", "hum", "qfe", "qnh", "pressure_alt" (int m), "gas", "co2", conditional "din" (lines 72-86) |

### Inbound (external app → gateway)

| File:Line                    | Function         | Direction | Buffer + Size                                    | Frozen Format Details                                                                                                      |
| ---------------------------- | ---------------- | --------- | ------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------- |
| src/extudp_functions.cpp:348 | `getExternUDP()` | IN        | `incomingExtPacket[UDP_TX_BUF_SIZE]` (255 bytes) | JSON deserialized (ArduinoJson); keys above must be recognized. Malformed JSON or NUL in strings rejected (lines 310-314). |

---

## (e) LoRa Frame Format (Wire on air)

### Frame Structure (after APRS decode/encode)

| Offset | Bytes    | Field                      | Frozen Details                                                                                                                                               |
| ------ | -------- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0      | 1        | Payload type               | 0x3A (text ':'), 0x21 (position '!'), 0x40 (HEY '@'), 0x41 (ACK 'A'). See aprs_functions.cpp:153-155.                                                        |
| 1–4    | 4        | Message ID                 | Little-endian uint32_t. Decoded at aprs_functions.cpp:159.                                                                                                   |
| 5      | 1        | Flags byte                 | Bits 0–3: max_hop (0–15). Bit 7 (0x80): server flag. Bit 6 (0x40): track. Bit 5 (0x20): app_offline. Bit 4 (0x10): mesh. See lines 161–173.                  |
| 6+     | variable | Source path                | Null-terminated ASCII, ends with '>'. Format: `CALL,relay1,relay2>...`                                                                                       |
| —      | variable | Destination path           | Null-terminated ASCII, ends with payload type (0x3A, 0x21, or 0x40).                                                                                         |
| —      | variable | Payload                    | Null-terminated UTF-8/ASCII, ends with 0x00. CHR-01: C0/C1 controls, invalid UTF-8, bidi/zero-width stripped (charset_filter_apply, aprs_functions.cpp:387). |
| —      | 1        | Source hardware ID         | BOARD_HARDWARE constant. See line 421.                                                                                                                       |
| —      | 1        | Source modulation          | Lower nibble: modulation (getMOD() & 0xF), upper nibble: country (meshcom_settings.node_country << 4). Line 424.                                             |
| —      | 2        | FCS (big-endian)           | Byte sum of all preceding bytes. Big-endian uint16_t at lines 427, 431–434.                                                                                  |
| —      | 1 (opt)  | Firmware version (numeric) | Only if additional payload follows. shortVERSION() → numeric value, e.g., 35 for "4.35". Line 458.                                                           |
| —      | 1 (opt)  | Last hardware ID           | msg_last_hw field. Line 464.                                                                                                                                 |
| —      | 1 (opt)  | Firmware sub-version       | Character (e.g., 't'), or 0x7e if encoded as NUL. Lines 470–482.                                                                                             |

**Max frame size:** MAX_APRS_FRAME_SIZE = 340 bytes (aprs_functions.cpp:10, also config_global.h via UDP_TX_BUF_SIZE=255 for relayed, but LoRa native allows 340).

### Serialization Functions

| File:Line                                      | Function       | Direction      | Frozen Details                                                                                                                                     |
| ---------------------------------------------- | -------------- | -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| src/aprs_functions.cpp:123                     | `decodeAPRS()` | IN (parse RX)  | Decode buffer `RcvBuffer[UDP_TX_BUF_SIZE]`, parse into `struct aprsMessage`. Validates all fields above; FCS mismatch logs and rejects (line 438). |
| src/aprs_functions.cpp + aprs_functions.h:9-10 | `encodeAPRS()` | OUT (build TX) | Encode `struct aprsMessage` into buffer `RcvBuffer[UDP_TX_BUF_SIZE]`, produces wire frame in format above.                                         |

---

## (f) Serial Command/Console Output

### Key Format Markers (always plaintext, no binary)

| Prefix           | Type                   | Details                                                                                                               |
| ---------------- | ---------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `[INIT]...`      | Initialization log     | Boot diagnostics, e.g., flash init, BLE start                                                                         |
| `[FLASH]...`     | Flash/settings log     | Load/save/sanitize operations                                                                                         |
| `[INFO]...`      | Info message           | Operational info, e.g., group checks                                                                                  |
| `[BLE]`          | BLE log                | BLE connection events, message sends                                                                                  |
| `[GW];rx;type;…` | Gateway RX raw         | High-rate gateway frame receive (format: `[GW];rx;type;{SET\|CET\|DATA};len;…;ms;…`, line 328, src/udp_functions.cpp) |
| `[UDP];rx;…`     | UDP RX (with --udplog) | Optional detailed UDP receive log (format: `[UDP];rx;ip;…;port;…;len;…;head;…`, line 170)                             |
| `[UDPSTAT]`      | UDP statistics         | `[UDPSTAT];bind;port;ip;…;rx;count;…;tx;count;…` (line 122, udp_functions.cpp)                                        |
| `[EXT]`          | EXTUDP log             | External UDP events                                                                                                   |
| `[CON ]`         | Net console log        | net_console.cpp line 114 "Client disconnected" format                                                                 |
| `[RX-UDP …`      | UDP RX debug           | Decoded frame type (line 346, loop_functions.cpp). No exact format frozen.                                            |

### Command Output Format (not frozen, informational)

- `--info` command outputs node config as newline-delimited text (not wire format)
- `--help`, `--debug`, `--setlog`, etc. are human-readable console commands with no wire-protocol guarantees

---

## Settings Struct: `struct s_meshcom_settings`

| File:Line                 | Struct Name          | FLASH_STRUCT_VERSION          | Persists Via                                                                                                                                                       | Layout Frozen?                                                                                         |
| ------------------------- | -------------------- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------ |
| src/esp32/esp32_flash.h:8 | `s_meshcom_settings` | 20260724 (config_global.h:83) | **ESP32:** NVS (field-by-field keys, see init_flash() at esp32_flash.cpp:61). **nRF52:** raw memcpy via BLE char 0xF0A1 (nrf52_ble.cpp:302, sizeof() at line 297). | **ESP32: NO** (field-by-field load, safe to reorder). **nRF52: YES** (raw memcpy, byte layout frozen). |

### Critical BLE Settings Characteristic (nRF52 only)

| File:Line                  | UUID   | Properties     | Payload                            | Frozen Details                                                                                                                                                                                     |
| -------------------------- | ------ | -------------- | ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| src/nrf52/nrf52_ble.cpp:59 | 0xF0A0 | Service        | —                                  | LoRa settings service UUID                                                                                                                                                                         |
| src/nrf52/nrf52_ble.cpp:61 | 0xF0A1 | Characteristic | `sizeof(s_meshcom_settings)` bytes | **Raw binary struct write (line 297, setFixedLen). Sent on notify (line 390).** Markers checked: valid_mark_1=0xAA, valid_mark_2=0x55 (MESHCOM_DATA_MARKER). Any size mismatch rejects (line 339). |

### FLASH_STRUCT_VERSION Check

| File:Line                         | Check                     | Behavior                                                                                                                                                           |
| --------------------------------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| src/configuration_global.h:97–109 | `flashLayoutCompatible()` | Returns true if stored version matches FLASH_STRUCT_VERSION (20260724) or is in FLASH_STRUCT_LEGACY[1] = {20260821}. If false, device wipes flash (clear_flash()). |
| src/configuration_global.h:73–82  | Version semantics         | FLASH_VERSION (20260910) is release timestamp, informational only. FLASH_STRUCT_VERSION bumps only on field add/remove/reorder.                                    |

---

## Consolidated FROZEN SYMBOLS

### Structs (byte layout)

- `struct s_meshcom_settings` (nRF52 BLE raw write only; ESP32 field-by-field safe to reorder)
  - **nRF52:** exact layout src/esp32/esp32_flash.h:8–241 must not change
  - **Markers:** valid_mark_1=0xAA, valid_mark_2=0x55
  - **BLE size check:** sizeof(s_meshcom_settings) at nrf52_ble.cpp:339

- `struct aprsMessage` (src/aprs_structures.h:8)
  - Only runtime struct, never serialized as-is to wire; fields listed in decode/encode functions
  - msg_len, payload_type, msg_id, max_hop, msg_server/track/app_offline/mesh flags are frozen field semantics

- `struct ConfFrame` (src/conf_frame.h:44)
  - Runtime struct only; wire format is TLV (tag-length-value) at conf_frame.cpp

### Buffer Size Constants

- **BLE:** MAX_MSG_LEN_PHONE = 300 (config_global.h:358)
  - Effective BLE JSON payload: BLE_JSON_PAYLOAD_MAX = 244 (line 369)
  - MTU overhead: 2 bytes lost, MTU=247 total, 245 usable
  - Ring: BLEtoPhoneBuff[MAX_RING][MAX_MSG_LEN_PHONE+5], MAX_RING=20

- **UDP/LoRa:** UDP_TX_BUF_SIZE = 255 (config_global.h:169)
  - LoRa native max: MAX_APRS_FRAME_SIZE = 340 (aprs_functions.cpp:10)
  - Ring: ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5], MAX_RING=20

- **Config:** UDP_CONF_BUFF_SIZE = UDP_TX_BUF_SIZE = 255 (config_global.h:170)

- **External:** EXTERN_PORT = 1799 (config_global.h:167), LOCAL_PORT = 1990 (config_global.h:166)

### Message Type Constants

- **APRS payload types (LoRa):** 0x3A (':' text), 0x21 ('!' position), 0x40 ('@' HEY), 0x41 ('A' ACK)
- **BLE frame status bytes:** 0x40 (text/position), 0x44 ('D' JSON), 0x91 (mheard)
- **BLE command IDs (from phone):** 0x10 (Hello), 0x20 (Timestamp), 0x50 (Callsign), 0x55 (WiFi), 0x70 (Lat), 0x80 (Lon), 0x90 (Alt), 0x95 (APRS Symbols), 0xA0 (Text), 0xF0 (Save)
- **UDP indicators:** "GATE", "BEAT", "CONF" (4-byte ASCII strings)
- **CONF TLV tags:** 0x00 (call), 0x01 (shortname), 0x02 (lat), 0x03 (lon), 0x04 (alt)

### JSON Keys (EXTUDP, BLE Data Messages)

- **externNoticeJson():** src_type, type ("msg"), src, dst, msg, msg_id, firmware, fw_sub, rssi, snr
- **externTeleJsonNode():** src_type ("node"), type ("tele"), src, temp1, temp2, hum, qfe, qnh, gas, co2, (optional) din
- **externTeleJsonLora():** src_type ("lora"), type ("tele"), src, batt, temp1, temp2, hum, qfe, qnh, pressure_alt, gas, co2, (optional) din
- **getExtern() inbound:** type ("msg" or "tele"), dst, msg (required for "msg"); temp, hum, press, temp2, qnh, gasres, co2 (optional for "tele")
- **BLE JSON (command_functions.cpp):** TYP (always present), FWVER, CALL, PARM, UNIT, TEMP, TOFFI, BME, BMP, SHUNT, OWNIP, OWNGW, GW, WS, NOALL, BLED, APN, AFC, ATXT, SYMID, LAT, LON, DATE, TIME, RSSI, SNR, PATH, HOP

### FLASH_STRUCT_VERSION

- **Current:** 20260724 (src/configuration_global.h:83)
- **Legacy:** 20260821 (FLASH_STRUCT_LEGACY[0], line 93) — no longer accepted, is rejected, flash wiped
- **Check:** flashLayoutCompatible() at config_global.h:97–109
- **Bump rule:** Change struct field add/remove/reorder → bump FLASH_STRUCT_VERSION; DO NOT bump for NVS-only field additions (ESP32)

---

## Notes

1. **Refactor Safety:** ESP32 settings are loaded field-by-field from NVS (no byte-layout requirement). nRF52 settings MUST preserve struct layout (raw BLE write at 0xF0A1).

2. **JSON Constraints:** EXTUDP and BLE JSON messages must preserve all key names and types. ArduinoJson escapes strings on serialize; bpNackCompose() (bp_notice_frame.h) replaces `"`, `\`, and `< 0x20` with spaces to prevent overflow.

3. **LoRa Frame Immutable:** Payload type byte, message ID, flags byte, FCS calculation, all parsing logic are wire-frozen. Any change to encodeAPRS() or decodeAPRS() signature/semantics breaks compatibility.

4. **Net Console:** 2323 is hardcoded (NET_CONSOLE_PORT, net_console.cpp). HMAC challenge is "NONCE: " + 32 hex + "\r\n". No changes to banner or auth flow.

5. **Config Frame TLV:** CONF_FRAME_CALL_MAX=9, CONF_FRAME_SHORT_MAX=5 (conf_frame.h). Wire length fields are 1 byte each. Order must be 0x00, then optional 0x01–0x04.

6. **UDP Indicators:** 4-byte ASCII literals "GATE", "BEAT", "CONF" start every inbound datagram. Offset +4 is the APRS payload type.

---

Generated: 2026-09-10 via wire-contract audit (read-only, no edits to repo)

---

<!-- source: S6-clones.md -->

## S6 -- Mechanical Duplicate-Code Detection (DRY Audit Input)

Script: `S6-clones.py` (stdlib-only Python 3). Full unabridged output:
`S6-clones-full.txt`. Repo: `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`.

## Method

- Walked `src/` for `*.cpp/.h/.c`, skipping `Fonts`, `t5-epaper/t5-src`,
  `t-deck-pro/src`, `Platforms`, `GFX_Root`, `Displays`,
  `nrf52/t_echo_images.h`, `t-deck/lv_conf.h`, and any file >4000 lines that
  is >80% numeric-table content (hex/digit/comma-only lines). 227 files
  scanned; 2 files skipped by the numeric-table heuristic:
  `t5-epaper/firasans_12.h`, `t5-epaper/firasans_20.h`.
- Normalization: comments stripped (newlines preserved so line numbers stay
  aligned), whitespace collapsed, string literals -> `"S"`, numeric literals
  -> `N`. Pass 2 additionally blanks identifiers (not C/C++ keywords) -> `I`.
- Rolling-hash windows (pass 1: >=8 lines, pass 2: >=12 lines) are extended
  to maximal length, then classes whose copy ranges are fully interval-
  contained in an already-accepted larger class are dropped (merge into
  maximal blocks, no sub-window duplicates left in the ranked lists).
- Classes with >20 copies are split out as "data-artifact" classes (font /
  binary tables produce combinatorial self-matches, e.g. `nrf52/glcdfont.c`)
  and excluded from the ranked top-N tables; counted separately below.
- Ranking: `score = (copies - 1) * lines`, descending.
- Location lists below are truncated to the first 5 copies + a `(+N more)`
  count to keep this file short; full, untruncated copy lists for every
  class are in `S6-clones-full.txt`.

Pass 1 code classes: 242 (+7 data-artifact classes, 26-2086 copies).
Pass 2 code classes: 336 (+13 data-artifact classes, 22-2068 copies).

## Pass 1 -- literal-normalized clones, >=8 lines (top 60)

| rank | copies | lines | score | locations                                                                                                                                                                                                             |
| ---- | ------ | ----- | ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | 19     | 9     | 162   | src/mheard_functions.cpp:854-862; src/mheard_functions.cpp:856-864; src/mheard_functions.cpp:858-866; src/mheard_functions.cpp:860-868; src/mheard_functions.cpp:862-870; ... (+14 more)                              |
| 2    | 20     | 8     | 152   | src/mheard_functions.cpp:853-860; src/mheard_functions.cpp:855-862; src/mheard_functions.cpp:857-864; src/mheard_functions.cpp:859-866; src/mheard_functions.cpp:861-868; ... (+15 more)                              |
| 3    | 18     | 8     | 136   | src/t-deck/tdeck_main.cpp:1010-1017; src/t-deck/tdeck_main.cpp:1011-1018; src/t-deck/tdeck_main.cpp:1012-1019; src/t-deck/tdeck_main.cpp:1013-1020; src/t-deck/tdeck_main.cpp:1014-1021; ... (+13 more)               |
| 4    | 2      | 111   | 111   | src/t-deck-pro/peri_gps.cpp:267-416; src/t5-epaper/peri_gps.cpp:230-378                                                                                                                                               |
| 5    | 12     | 9     | 99    | src/esp32/esp32_flash.cpp:427-435; src/esp32/esp32_flash.cpp:429-437; src/esp32/esp32_flash.cpp:431-439; src/esp32/esp32_flash.cpp:433-441; src/esp32/esp32_flash.cpp:435-444; ... (+7 more)                          |
| 6    | 13     | 8     | 96    | src/esp32/esp32_flash.cpp:426-433; src/esp32/esp32_flash.cpp:428-435; src/esp32/esp32_flash.cpp:430-437; src/esp32/esp32_flash.cpp:432-439; src/esp32/esp32_flash.cpp:434-441; ... (+8 more)                          |
| 7    | 13     | 8     | 96    | src/t-deck/mouse_cursor_icon.c:6-13; src/t-deck/mouse_cursor_icon.c:7-14; src/t-deck/mouse_cursor_icon.c:8-15; src/t-deck/mouse_cursor_icon.c:9-16; src/t-deck/mouse_cursor_icon.c:10-17; ... (+8 more)               |
| 8    | 13     | 8     | 96    | src/t-deck/mouse_cursor_icon.c:29-36; src/t-deck/mouse_cursor_icon.c:30-37; src/t-deck/mouse_cursor_icon.c:31-38; src/t-deck/mouse_cursor_icon.c:32-39; src/t-deck/mouse_cursor_icon.c:33-40; ... (+8 more)           |
| 9    | 13     | 8     | 96    | src/t-deck/mouse_cursor_icon.c:51-58; src/t-deck/mouse_cursor_icon.c:52-59; src/t-deck/mouse_cursor_icon.c:53-60; src/t-deck/mouse_cursor_icon.c:54-61; src/t-deck/mouse_cursor_icon.c:55-62; ... (+8 more)           |
| 10   | 12     | 8     | 88    | src/aprs_functions.cpp:677-695; src/aprs_functions.cpp:705-723; src/aprs_functions.cpp:733-751; src/aprs_functions.cpp:761-779; src/aprs_functions.cpp:789-807; ... (+7 more)                                         |
| 11   | 8      | 12    | 84    | src/lora_setchip.cpp:203-219; src/lora_setchip.cpp:243-259; src/lora_setchip.cpp:360-376; src/lora_setchip.cpp:379-395; src/lora_setchip.cpp:398-414; ... (+3 more)                                                   |
| 12   | 8      | 12    | 84    | src/lora_setchip.cpp:255-269; src/lora_setchip.cpp:275-289; src/lora_setchip.cpp:352-366; src/lora_setchip.cpp:372-385; src/lora_setchip.cpp:391-404; ... (+3 more)                                                   |
| 13   | 10     | 9     | 81    | src/aprs_functions.cpp:682-702; src/aprs_functions.cpp:710-730; src/aprs_functions.cpp:738-758; src/aprs_functions.cpp:766-786; src/aprs_functions.cpp:794-814; ... (+5 more)                                         |
| 14   | 10     | 9     | 81    | src/lora_setchip.cpp:201-209; src/lora_setchip.cpp:241-249; src/lora_setchip.cpp:261-269; src/lora_setchip.cpp:281-289; src/lora_setchip.cpp:358-366; ... (+5 more)                                                   |
| 15   | 6      | 16    | 80    | src/lora_setchip.cpp:248-269; src/lora_setchip.cpp:365-385; src/lora_setchip.cpp:384-404; src/lora_setchip.cpp:403-423; src/lora_setchip.cpp:422-439; ... (+1 more)                                                   |
| 16   | 9      | 10    | 80    | src/lora_setchip.cpp:199-209; src/lora_setchip.cpp:259-269; src/lora_setchip.cpp:279-289; src/lora_setchip.cpp:356-366; src/lora_setchip.cpp:376-385; ... (+4 more)                                                   |
| 17   | 9      | 10    | 80    | src/t-deck/tdeck_main.cpp:968-977; src/t-deck/tdeck_main.cpp:971-980; src/t-deck/tdeck_main.cpp:974-983; src/t-deck/tdeck_main.cpp:977-986; src/t-deck/tdeck_main.cpp:980-989; ... (+4 more)                          |
| 18   | 11     | 8     | 80    | src/aprs_functions.cpp:680-697; src/aprs_functions.cpp:708-725; src/aprs_functions.cpp:736-753; src/aprs_functions.cpp:764-781; src/aprs_functions.cpp:792-809; ... (+6 more)                                         |
| 19   | 2      | 78    | 78    | src/nrf52/WisBlock-API.h:233-362; src/nrf52/WisBlock-API.h:469-598                                                                                                                                                    |
| 20   | 7      | 13    | 78    | src/lora_setchip.cpp:253-269; src/lora_setchip.cpp:350-366; src/lora_setchip.cpp:370-385; src/lora_setchip.cpp:389-404; src/lora_setchip.cpp:408-423; ... (+2 more)                                                   |
| 21   | 10     | 8     | 72    | src/t-deck/tdeck_main.cpp:967-974; src/t-deck/tdeck_main.cpp:970-977; src/t-deck/tdeck_main.cpp:973-980; src/t-deck/tdeck_main.cpp:976-983; src/t-deck/tdeck_main.cpp:979-986; ... (+5 more)                          |
| 22   | 9      | 8     | 64    | src/config_json.cpp:117-124; src/config_json.cpp:118-125; src/config_json.cpp:119-126; src/config_json.cpp:120-127; src/config_json.cpp:121-128; ... (+4 more)                                                        |
| 23   | 9      | 8     | 64    | src/lora_setchip.cpp:207-219; src/lora_setchip.cpp:226-238; src/lora_setchip.cpp:247-259; src/lora_setchip.cpp:364-376; src/lora_setchip.cpp:383-395; ... (+4 more)                                                   |
| 24   | 9      | 8     | 64    | src/nrf52/nrf52_flash.cpp:193-200; src/nrf52/nrf52_flash.cpp:194-202; src/nrf52/nrf52_flash.cpp:195-203; src/nrf52/nrf52_flash.cpp:196-204; src/nrf52/nrf52_flash.cpp:197-205; ... (+4 more)                          |
| 25   | 4      | 21    | 63    | src/loop_functions.cpp:1336-1363; src/loop_functions.cpp:1429-1456; src/loop_functions.cpp:1539-1566; src/loop_functions.cpp:2779-2806                                                                                |
| 26   | 8      | 8     | 56    | src/command_functions.cpp:822-830; src/command_functions.cpp:824-833; src/command_functions.cpp:826-836; src/command_functions.cpp:828-839; src/command_functions.cpp:831-842; ... (+3 more)                          |
| 27   | 8      | 8     | 56    | src/command_functions.cpp:823-831; src/command_functions.cpp:825-834; src/command_functions.cpp:827-837; src/command_functions.cpp:830-840; src/command_functions.cpp:833-843; ... (+3 more)                          |
| 28   | 3      | 27    | 54    | src/esp32/esp32_flash.h:91-132; src/nrf52/WisBlock-API.h:260-302; src/nrf52/WisBlock-API.h:496-538                                                                                                                    |
| 29   | 7      | 9     | 54    | src/loop_functions.cpp:3326-3340; src/loop_functions.cpp:3403-3417; src/loop_functions.cpp:4051-4065; src/loop_functions.cpp:4740-4754; src/loop_functions.cpp:4817-4831; ... (+2 more)                               |
| 30   | 7      | 9     | 54    | src/t-deck/event_functions.cpp:67-75; src/t-deck/event_functions.cpp:70-78; src/t-deck/event_functions.cpp:73-81; src/t-deck/event_functions.cpp:76-84; src/t-deck/event_functions.cpp:79-87; ... (+2 more)           |
| 31   | 7      | 9     | 54    | src/t-deck/event_functions.cpp:109-117; src/t-deck/event_functions.cpp:112-120; src/t-deck/event_functions.cpp:115-123; src/t-deck/event_functions.cpp:118-126; src/t-deck/event_functions.cpp:121-129; ... (+2 more) |
| 32   | 6      | 10    | 50    | src/t-deck/event_functions.cpp:69-78; src/t-deck/event_functions.cpp:72-81; src/t-deck/event_functions.cpp:75-84; src/t-deck/event_functions.cpp:78-87; src/t-deck/event_functions.cpp:81-90; ... (+1 more)           |
| 33   | 6      | 10    | 50    | src/t-deck/event_functions.cpp:111-120; src/t-deck/event_functions.cpp:114-123; src/t-deck/event_functions.cpp:117-126; src/t-deck/event_functions.cpp:120-129; src/t-deck/event_functions.cpp:123-132; ... (+1 more) |
| 34   | 2      | 48    | 48    | src/t-deck-pro/ui_scr_mrg.c:191-264; src/t5-epaper/scr_mrg.cpp:191-264                                                                                                                                                |
| 35   | 3      | 24    | 48    | src/loop_functions.cpp:1425-1459; src/loop_functions.cpp:1535-1569; src/loop_functions.cpp:2775-2810                                                                                                                  |
| 36   | 6      | 9     | 45    | src/esp32/esp32_flash.cpp:131-139; src/esp32/esp32_flash.cpp:133-141; src/esp32/esp32_flash.cpp:135-143; src/esp32/esp32_flash.cpp:137-145; src/esp32/esp32_flash.cpp:139-148; ... (+1 more)                          |
| 37   | 6      | 9     | 45    | src/loop_functions.cpp:5693-5701; src/loop_functions.cpp:5696-5704; src/loop_functions.cpp:5699-5707; src/loop_functions.cpp:5702-5710; src/loop_functions.cpp:5705-5713; ... (+1 more)                               |
| 38   | 5      | 10    | 40    | src/command_functions.cpp:3940-3956; src/command_functions.cpp:3983-3999; src/command_functions.cpp:4008-4024; src/command_functions.cpp:4034-4050; src/command_functions.cpp:4060-4076                               |
| 39   | 5      | 10    | 40    | src/loop_functions.cpp:5695-5704; src/loop_functions.cpp:5698-5707; src/loop_functions.cpp:5701-5710; src/loop_functions.cpp:5704-5713; src/loop_functions.cpp:5707-5716                                              |
| 40   | 6      | 8     | 40    | src/i2c_scanner.cpp:110-117; src/i2c_scanner.cpp:111-118; src/i2c_scanner.cpp:112-119; src/i2c_scanner.cpp:113-120; src/i2c_scanner.cpp:114-121; ... (+1 more)                                                        |
| 41   | 6      | 8     | 40    | src/regex_functions.cpp:24-34; src/regex_functions.cpp:27-37; src/regex_functions.cpp:30-40; src/regex_functions.cpp:33-43; src/regex_functions.cpp:36-46; ... (+1 more)                                              |
| 42   | 2      | 37    | 37    | src/t-deck-pro/ui_scr_mrg.h:4-51; src/t5-epaper/scr_mrg.h:5-53                                                                                                                                                        |
| 43   | 3      | 18    | 36    | src/esp32/esp32_flash.h:141-168; src/nrf52/WisBlock-API.h:304-329; src/nrf52/WisBlock-API.h:540-565                                                                                                                   |
| 44   | 5      | 9     | 36    | src/loop_functions.cpp:5648-5656; src/loop_functions.cpp:5651-5659; src/loop_functions.cpp:5654-5662; src/loop_functions.cpp:5657-5665; src/loop_functions.cpp:5660-5668                                              |
| 45   | 5      | 9     | 36    | src/regex_functions.cpp:25-37; src/regex_functions.cpp:28-40; src/regex_functions.cpp:31-43; src/regex_functions.cpp:34-46; src/regex_functions.cpp:37-49                                                             |
| 46   | 2      | 35    | 35    | src/esp32/at_cmd.h:13-53; src/nrf52/at_cmd.h:13-53                                                                                                                                                                    |
| 47   | 2      | 35    | 35    | src/t-deck-pro/ui_scr_mrg.c:32-93; src/t5-epaper/scr_mrg.cpp:32-93                                                                                                                                                    |
| 48   | 2      | 31    | 31    | src/mheard_functions.cpp:922-971; src/t-deck-pro/ui_deckpro.cpp:1300-1352                                                                                                                                             |
| 49   | 4      | 10    | 30    | src/loop_functions.cpp:5650-5659; src/loop_functions.cpp:5653-5662; src/loop_functions.cpp:5656-5665; src/loop_functions.cpp:5659-5668                                                                                |
| 50   | 2      | 28    | 28    | src/batt_function_old.cpp:70-120; src/batt_functions.cpp:71-120                                                                                                                                                       |
| 51   | 2      | 27    | 27    | src/t-deck-pro/ui_deckpro.cpp:2182-2218; src/t-deck/lv_obj_functions.cpp:4340-4376                                                                                                                                    |
| 52   | 2      | 27    | 27    | src/t-deck-pro/ui_scr_mrg.c:117-151; src/t5-epaper/scr_mrg.cpp:117-151                                                                                                                                                |
| 53   | 4      | 9     | 27    | src/command_functions.cpp:558-569; src/command_functions.cpp:3602-3613; src/command_functions.cpp:3634-3645; src/command_functions.cpp:3660-3671                                                                      |
| 54   | 3      | 13    | 26    | src/gps_functions.cpp:686-699; src/gps_functions.cpp:694-707; src/gps_functions.cpp:702-715                                                                                                                           |
| 55   | 3      | 13    | 26    | src/lora_functions.cpp:1854-1873; src/lora_functions.cpp:1902-1916; src/lora_functions.cpp:1967-1981                                                                                                                  |
| 56   | 2      | 25    | 25    | src/loop_functions.cpp:1424-1459; src/loop_functions.cpp:1534-1569                                                                                                                                                    |
| 57   | 2      | 24    | 24    | src/t-deck-pro/ui_deckpro_port.cpp:310-337; src/t5-epaper/ui_port.cpp:443-470                                                                                                                                         |
| 58   | 4      | 8     | 24    | src/command_functions.cpp:1292-1305; src/command_functions.cpp:1311-1324; src/command_functions.cpp:1334-1347; src/command_functions.cpp:1357-1370                                                                    |
| 59   | 4      | 8     | 24    | src/command_functions.cpp:3947-3958; src/command_functions.cpp:4015-4026; src/command_functions.cpp:4041-4052; src/command_functions.cpp:4067-4078                                                                    |
| 60   | 4      | 8     | 24    | src/esp32/esp32_flash.cpp:150-157; src/esp32/esp32_flash.cpp:152-159; src/esp32/esp32_flash.cpp:154-161; src/esp32/esp32_flash.cpp:156-163                                                                            |

Data-artifact classes excluded above (>20 copies, pass 1): `nrf52/glcdfont.c` +
`safeboot/ota.h` (2086 copies, 8 lines); `web_functions/web_functions.cpp`
(208 and 35 copies, 8 lines); an 8-file cross-file class at 36 copies/8
lines spanning `command_functions.cpp`, `esp32/esp32_main.cpp`,
`loop_functions.cpp`, `lora_functions.cpp`, `nrf52/nrf52_main.cpp`,
`safeboot/ElegantOTA.h`, `t-deck-pro/ui_deckpro.cpp`, `t5-epaper/ui_port.cpp`;
6 classes at 23-24 copies (8-10 lines) in `esp32/esp32_audio.cpp`.

## Pass 2 -- identifier-blanked clones, >=12 lines (top 40)

`[CROSS-FILE]` = copies span >=2 of {esp32, nrf52, t-deck, t-deck-pro,
t5-epaper, udp_functions, nrf_eth}.

| rank | copies | lines | score | locations                                                                                                                                                                                                                                      |
| ---- | ------ | ----- | ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | 19     | 12    | 216   | src/command_functions.cpp:2-13; src/command_functions.cpp:3-14; src/command_functions.cpp:4-15; src/command_functions.cpp:5-16; src/command_functions.cpp:6-17; ... (+14 more)                                                                 |
| 2    | 19     | 12    | 216   | src/t-deck-pro/bq27220_data_memory.h:24-35; src/t-deck-pro/bq27220_data_memory.h:25-36; src/t-deck-pro/bq27220_data_memory.h:26-37; src/t-deck-pro/bq27220_data_memory.h:27-38; src/t-deck-pro/bq27220_data_memory.h:28-39; ... (+14 more)     |
| 3    | 17     | 13    | 208   | src/mheard_functions.cpp:854-866; src/mheard_functions.cpp:856-868; src/mheard_functions.cpp:858-870; src/mheard_functions.cpp:860-872; src/mheard_functions.cpp:862-874; ... (+12 more)                                                       |
| 4    | 18     | 12    | 204   | src/mheard_functions.cpp:853-864; src/mheard_functions.cpp:855-866; src/mheard_functions.cpp:857-868; src/mheard_functions.cpp:859-870; src/mheard_functions.cpp:861-872; ... (+13 more)                                                       |
| 5    | 16     | 12    | 180   | src/loop_functions.cpp:123-135; src/loop_functions.cpp:124-136; src/loop_functions.cpp:125-137; src/loop_functions.cpp:127-138; src/loop_functions.cpp:128-139; ... (+11 more)                                                                 |
| 6    | 10     | 19    | 171   | src/aprs_functions.cpp:674-715; src/aprs_functions.cpp:702-743; src/aprs_functions.cpp:730-771; src/aprs_functions.cpp:758-799; src/aprs_functions.cpp:786-827; ... (+5 more)                                                                  |
| 7    | 15     | 12    | 168   | src/t-deck-pro/bq27220_data_memory.h:8-19; src/t-deck-pro/peripheral.h:14-25; src/t-deck-pro/peripheral.h:15-26; src/t-deck-pro/ui_deckpro.h:50-61; src/t-deck-pro/ui_deckpro.h:51-62; ... (+10 more) **[CROSS-FILE]**                         |
| 8    | 14     | 12    | 156   | src/esp32/esp32_main.cpp:828-839; src/esp32/esp32_main.cpp:829-840; src/esp32/esp32_main.cpp:869-883; src/esp32/esp32_main.cpp:872-884; src/esp32/esp32_main.cpp:873-885; ... (+9 more) **[CROSS-FILE]**                                       |
| 9    | 14     | 12    | 156   | src/t-deck/tdeck_main.cpp:1010-1021; src/t-deck/tdeck_main.cpp:1011-1022; src/t-deck/tdeck_main.cpp:1012-1023; src/t-deck/tdeck_main.cpp:1013-1024; src/t-deck/tdeck_main.cpp:1014-1025; ... (+9 more)                                         |
| 10   | 13     | 12    | 144   | src/web_functions/web_setup.cpp:79-92; src/web_functions/web_setup.cpp:95-108; src/web_functions/web_setup.cpp:192-205; src/web_functions/web_setup.cpp:232-245; src/web_functions/web_setup.cpp:240-253; ... (+8 more)                        |
| 11   | 9      | 17    | 136   | src/web_functions/web_setup.cpp:58-76; src/web_functions/web_setup.cpp:251-269; src/web_functions/web_setup.cpp:424-442; src/web_functions/web_setup.cpp:432-450; src/web_functions/web_setup.cpp:440-458; ... (+4 more)                       |
| 12   | 10     | 15    | 135   | src/web_functions/web_setup.cpp:713-738; src/web_functions/web_setup.cpp:718-743; src/web_functions/web_setup.cpp:730-748; src/web_functions/web_setup.cpp:810-828; src/web_functions/web_setup.cpp:815-833; ... (+5 more)                     |
| 13   | 12     | 12    | 132   | src/loop_functions.cpp:4242-4254; src/loop_functions.cpp:4244-4255; src/loop_functions.cpp:4245-4256; src/loop_functions.cpp:4246-4257; src/loop_functions.cpp:4247-4259; ... (+7 more)                                                        |
| 14   | 12     | 12    | 132   | src/nrf52/t_echo_utilities.h:10-23; src/nrf52/t_echo_utilities.h:11-24; src/nrf52/t_echo_utilities.h:12-25; src/nrf52/t_echo_utilities.h:13-26; src/nrf52/t_echo_utilities.h:31-51; ... (+7 more)                                              |
| 15   | 11     | 13    | 130   | src/aprs_functions.cpp:667-695; src/aprs_functions.cpp:695-723; src/aprs_functions.cpp:723-751; src/aprs_functions.cpp:751-779; src/aprs_functions.cpp:779-807; ... (+6 more)                                                                  |
| 16   | 8      | 18    | 126   | src/web_functions/web_setup.cpp:249-269; src/web_functions/web_setup.cpp:422-442; src/web_functions/web_setup.cpp:430-450; src/web_functions/web_setup.cpp:438-458; src/web_functions/web_setup.cpp:446-466; ... (+3 more)                     |
| 17   | 10     | 14    | 126   | src/aprs_functions.cpp:664-695; src/aprs_functions.cpp:692-723; src/aprs_functions.cpp:720-751; src/aprs_functions.cpp:748-779; src/aprs_functions.cpp:776-807; ... (+5 more)                                                                  |
| 18   | 9      | 15    | 120   | src/web_functions/web_setup.cpp:233-249; src/web_functions/web_setup.cpp:313-329; src/web_functions/web_setup.cpp:321-337; src/web_functions/web_setup.cpp:329-345; src/web_functions/web_setup.cpp:337-353; ... (+4 more)                     |
| 19   | 9      | 15    | 120   | src/web_functions/web_setup.cpp:688-706; src/web_functions/web_setup.cpp:790-808; src/web_functions/web_setup.cpp:795-813; src/web_functions/web_setup.cpp:867-885; src/web_functions/web_setup.cpp:872-890; ... (+4 more)                     |
| 20   | 11     | 12    | 120   | src/aprs_functions.cpp:663-687; src/aprs_functions.cpp:691-715; src/aprs_functions.cpp:719-743; src/aprs_functions.cpp:747-771; src/aprs_functions.cpp:775-799; ... (+6 more)                                                                  |
| 21   | 11     | 12    | 120   | src/aprs_functions.cpp:671-697; src/aprs_functions.cpp:699-725; src/aprs_functions.cpp:727-753; src/aprs_functions.cpp:755-781; src/aprs_functions.cpp:783-809; ... (+6 more)                                                                  |
| 22   | 11     | 12    | 120   | src/config_json.cpp:117-128; src/config_json.cpp:118-129; src/config_json.cpp:119-130; src/config_json.cpp:120-131; src/config_json.cpp:121-132; ... (+6 more)                                                                                 |
| 23   | 11     | 12    | 120   | src/esp32/esp32_flash.cpp:426-437; src/esp32/esp32_flash.cpp:428-439; src/esp32/esp32_flash.cpp:430-441; src/esp32/esp32_flash.cpp:432-444; src/esp32/esp32_flash.cpp:434-446; ... (+6 more)                                                   |
| 24   | 11     | 12    | 120   | src/web_functions/web_setup.cpp:63-76; src/web_functions/web_setup.cpp:248-261; src/web_functions/web_setup.cpp:256-269; src/web_functions/web_setup.cpp:421-434; src/web_functions/web_setup.cpp:429-442; ... (+6 more)                       |
| 25   | 10     | 13    | 117   | src/esp32/esp32_flash.cpp:427-439; src/esp32/esp32_flash.cpp:429-441; src/esp32/esp32_flash.cpp:431-444; src/esp32/esp32_flash.cpp:433-446; src/esp32/esp32_flash.cpp:435-448; ... (+5 more)                                                   |
| 26   | 9      | 14    | 112   | src/esp32/esp32_pmu.cpp:270-287; src/esp32/esp32_pmu.cpp:274-291; src/esp32/esp32_pmu.cpp:278-295; src/esp32/esp32_pmu.cpp:282-299; src/esp32/esp32_pmu.cpp:286-303; ... (+4 more)                                                             |
| 27   | 2      | 111   | 111   | src/t-deck-pro/peri_gps.cpp:267-416; src/t5-epaper/peri_gps.cpp:230-378 **[CROSS-FILE]**                                                                                                                                                       |
| 28   | 10     | 12    | 108   | src/esp32/esp32_pmu.cpp:269-283; src/esp32/esp32_pmu.cpp:273-287; src/esp32/esp32_pmu.cpp:277-291; src/esp32/esp32_pmu.cpp:281-295; src/esp32/esp32_pmu.cpp:285-299; ... (+5 more)                                                             |
| 29   | 10     | 12    | 108   | src/t-deck/lv_obj_functions.cpp:852-863; src/t-deck/lv_obj_functions.cpp:876-887; src/t-deck/lv_obj_functions.cpp:932-943; src/t-deck/lv_obj_functions.cpp:1068-1079; src/t-deck/lv_obj_functions.cpp:1082-1093; ... (+5 more)                 |
| 30   | 6      | 20    | 100   | src/lora_setchip.cpp:244-269; src/lora_setchip.cpp:361-385; src/lora_setchip.cpp:380-404; src/lora_setchip.cpp:399-423; src/lora_setchip.cpp:418-439; ... (+1 more)                                                                            |
| 31   | 8      | 14    | 98    | src/lora_setchip.cpp:201-219; src/lora_setchip.cpp:241-259; src/lora_setchip.cpp:358-376; src/lora_setchip.cpp:377-395; src/lora_setchip.cpp:396-414; ... (+3 more)                                                                            |
| 32   | 8      | 14    | 98    | src/t-deck/tdeck_main.cpp:967-980; src/t-deck/tdeck_main.cpp:970-983; src/t-deck/tdeck_main.cpp:973-986; src/t-deck/tdeck_main.cpp:976-989; src/t-deck/tdeck_main.cpp:979-992; ... (+3 more)                                                   |
| 33   | 8      | 14    | 98    | src/web_functions/web_functions.cpp:668-689; src/web_functions/web_functions.cpp:673-694; src/web_functions/web_functions.cpp:678-699; src/web_functions/web_functions.cpp:683-704; src/web_functions/web_functions.cpp:688-709; ... (+3 more) |
| 34   | 9      | 12    | 96    | src/loop_functions.cpp:4459-4470; src/loop_functions.cpp:4460-4471; src/loop_functions.cpp:4461-4472; src/loop_functions.cpp:4462-4473; src/loop_functions.cpp:4463-4474; ... (+4 more)                                                        |
| 35   | 9      | 12    | 96    | src/t-deck/mouse_cursor_icon.c:6-17; src/t-deck/mouse_cursor_icon.c:7-18; src/t-deck/mouse_cursor_icon.c:8-19; src/t-deck/mouse_cursor_icon.c:9-20; src/t-deck/mouse_cursor_icon.c:10-21; ... (+4 more)                                        |
| 36   | 9      | 12    | 96    | src/t-deck/mouse_cursor_icon.c:29-40; src/t-deck/mouse_cursor_icon.c:30-41; src/t-deck/mouse_cursor_icon.c:31-42; src/t-deck/mouse_cursor_icon.c:32-43; src/t-deck/mouse_cursor_icon.c:33-44; ... (+4 more)                                    |
| 37   | 9      | 12    | 96    | src/t-deck/mouse_cursor_icon.c:51-62; src/t-deck/mouse_cursor_icon.c:52-63; src/t-deck/mouse_cursor_icon.c:53-64; src/t-deck/mouse_cursor_icon.c:54-65; src/t-deck/mouse_cursor_icon.c:55-66; ... (+4 more)                                    |
| 38   | 9      | 12    | 96    | src/web_functions/web_functions.cpp:666-684; src/web_functions/web_functions.cpp:671-689; src/web_functions/web_functions.cpp:676-694; src/web_functions/web_functions.cpp:681-699; src/web_functions/web_functions.cpp:686-704; ... (+4 more) |
| 39   | 8      | 13    | 91    | src/web_functions/web_setup.cpp:191-205; src/web_functions/web_setup.cpp:239-253; src/web_functions/web_setup.cpp:319-333; src/web_functions/web_setup.cpp:327-341; src/web_functions/web_setup.cpp:335-349; ... (+3 more)                     |
| 40   | 6      | 18    | 90    | src/command_functions.cpp:2687-2720; src/command_functions.cpp:2719-2752; src/command_functions.cpp:2751-2784; src/command_functions.cpp:2801-2839; src/command_functions.cpp:2881-2914; ... (+1 more)                                         |

Data-artifact classes excluded above (>20 copies, pass 2): `nrf52/glcdfont.c`

- `safeboot/ota.h` (2068 copies); `web_functions/web_functions.cpp` (205 and
  27 copies); `configuration_global.h` + `t-deck-pro/utilities.h` cross-file
  (63 copies, 12 lines); `t-deck/lv_obj_functions.cpp` (38 copies);
  `t-deck/lv_obj_functions_extern.h` (33 copies); `t-deck-pro/bq27220_data_memory.c`
  (4 classes, 24-25 copies, 12-15 lines); `loop_functions_extern.h` (26 copies);
  `esp32/esp32_audio.cpp` (4 classes, 22-23 copies, 12-14 lines).

## Pairwise similarity (SequenceMatcher ratio on normalized line lists)

| pair                                                                           | ratio  | detail                                     |
| ------------------------------------------------------------------------------ | ------ | ------------------------------------------ |
| `t-deck-pro/peri_gps.cpp` vs `t5-epaper/peri_gps.cpp`                          | 0.8701 | 264 vs 244 normalized lines                |
| `t5-epaper/t5-src/img_start.c` vs `t-deck-pro/src/img_start.c`                 | 0.0144 | 3877 vs 1265 normalized lines              |
| `t5-epaper/t5-src/Font_Mono_Bold_20.c` vs `t-deck-pro/src/Font_Mono_Bold_20.c` | 0.9979 | 956 vs 960 normalized lines                |
| `t5-epaper/ui_port.cpp` vs `t-deck-pro/ui_deckpro_port.cpp`                    | 0.2132 | 281 vs 235 normalized lines                |
| `t5-epaper/ui.cpp` vs `t-deck-pro/ui_deckpro.cpp`                              | 0.1633 | 1716 vs 1933 normalized lines              |
| `t5-epaper/t5epaper_main.cpp` vs `t-deck-pro/tdeck_pro.cpp`                    | 0.1267 | 367 vs 375 normalized lines                |
| `esp32/esp32_flash.cpp` vs `nrf52/nrf52_flash.cpp`                             | 0.0644 | 411 vs 241 normalized lines                |
| `batt_functions.cpp` vs `batt_function_old.cpp`                                | 0.2185 | 244 vs 470 normalized lines                |
| `udp_functions.cpp` vs `nrf52/nrf_eth.cpp`                                     | 0.2206 | 970 vs 653 normalized lines                |
| `esp32/esp32_main.cpp` vs `nrf52/nrf52_main.cpp`                               | 0.2599 | 2361 vs 1571 normalized lines              |
| `t-deck/tdeck_main.cpp` vs `t-deck-pro/tdeck_pro.cpp`                          | 0.0311 | 848 vs 375 normalized lines                |
| `t5-epaper/peri_keypad.cpp` vs `t-deck-pro/peri_keypad.cpp`                    | n/a    | `t5-epaper/peri_keypad.cpp` does not exist |
| `t5-epaper/bq27220.cpp` vs `t-deck-pro/bq27220.cpp`                            | n/a    | `t5-epaper/bq27220.cpp` does not exist     |

### `ls src/t5-epaper` vs `ls src/t-deck-pro`: filenames present in both

t5-epaper: 18 files. t-deck-pro: 20 files. Intersection = 4 filenames:

| filename      | ratio  | detail                                               |
| ------------- | ------ | ---------------------------------------------------- |
| peri_gps.cpp  | 0.8701 | 244 (t5-epaper) vs 264 (t-deck-pro) normalized lines |
| peri_lora.cpp | 0.3172 | 119 (t5-epaper) vs 26 (t-deck-pro) normalized lines  |
| peripheral.h  | 0.4694 | 39 (t5-epaper) vs 59 (t-deck-pro) normalized lines   |
| utilities.h   | 0.0566 | 34 (t5-epaper) vs 72 (t-deck-pro) normalized lines   |

`img_start.c` and `Font_Mono_Bold_20.c` are not in this intersection because
they live under the excluded `t5-src` / `src` subdirectories, not directly
in `t5-epaper` / `t-deck-pro`.

## Function-level: functions > 150 lines

Method: top-level (column-0) function signatures located by regex; each
function's body bounded from its signature line to one line before the next
column-0 signature (or EOF) -- a naive brace-depth-only scan misfires in
these files because `#ifdef`-guarded `else if` branches unbalance raw brace
counts.

### `src/command_functions.cpp`

| name          | start-end | length |
| ------------- | --------- | ------ |
| commandAction | 238-6172  | 5935   |

### `src/loop_functions.cpp`

| name                | start-end | length |
| ------------------- | --------- | ------ |
| sendDisplayText     | 2208-2722 | 515    |
| sendMessage         | 3771-4198 | 428    |
| sendDisplay1306     | 941-1305  | 365    |
| sendTelemetry       | 5044-5353 | 310    |
| sendDisplayPosition | 2762-3070 | 309    |
| sendPosition        | 4497-4794 | 298    |
| PositionToAPRS      | 4200-4495 | 296    |
| wpMsgFont           | 310-554   | 245    |
| mainStartTimeLoop   | 1969-2205 | 237    |
| setSMartBeaconing   | 5367-5571 | 205    |

## Top 15 repeated 3-line normalized snippets in `src/command_functions.cpp`

| rank | occurrences | snippet                                                                                                         | example lines                                                               |
| ---- | ----------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| 1    | 140         | `return; / else / if(commandCheck(msg_text+N, (char*)"S") == N)`                                                | 408, 424, 486, 495, 504, 517, 530, 544, 558, 585, 594, 603, ...             |
| 2    | 51          | `save_settings(); / return; / else`                                                                             | 315, 422, 452, 468, 484, 515, 528, 542, 556, 583, 614, 627, ...             |
| 3    | 36          | `else / if(commandCheck(msg_text+N, (char*)"S") == N) / snprintf(_owner_c, sizeof(_owner_c), "S", msg_text+N);` | 560, 1282, 1301, 1320, 1343, 1366, 1484, 1509, 2411, 2520, 3233, 3360, ...  |
| 4    | 32          | `bReturn = true; / save_settings(); / else`                                                                     | 1452, 1644, 1685, 1730, 1746, 1995, 2052, 2088, 2104, 2121, 2138, 2154, ... |
| 5    | 28          | `delay(N); / printlndeb("S"); / delay(N);`                                                                      | 768, 771, 774, 779, 781, 789, 791, 795, 797, 822, 824, 826, ...             |
| 6    | 28          | `save_settings(); / else / if(commandCheck(msg_text+N, (char*)"S") == N)`                                       | 1646, 1687, 1732, 2090, 2106, 2123, 2140, 2156, 2176, 2207, 2267, 2335, ... |
| 7    | 26          | `#include "S" / #include "S" / #include "S"`                                                                    | 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, ...                                 |
| 8    | 25          | `if(ble) / addBLECommandBack((char*)"S"); / save_settings();`                                                   | 968, 986, 2694, 2710, 2726, 2742, 2758, 2774, 2792, 2813, 2829, 2847, ...   |
| 9    | 25          | `addBLECommandBack((char*)"S"); / save_settings(); / return;`                                                   | 970, 988, 2696, 2712, 2728, 2744, 2760, 2776, 2794, 2815, 2831, 2849, ...   |
| 10   | 24          | `printlndeb("S"); / delay(N); / printlndeb("S");`                                                               | 770, 773, 780, 790, 796, 823, 825, 827, 830, 833, 836, 839, ...             |
| 11   | 21          | `if(ble) / bNodeSetting=true; / bReturn = true;`                                                                | 1020, 1046, 1196, 1214, 1584, 1601, 1619, 1639, 1655, 1680, 1708, 1725, ... |
| 12   | 21          | `bReturn = true; / else / if(commandCheck(msg_text+N, (char*)"S") == N)`                                        | 1125, 1299, 1318, 1341, 1364, 1388, 1404, 1420, 1482, 1507, 1532, 1606, ... |
| 13   | 21          | `if(ble) / bSensSetting = true; / bReturn = true;`                                                              | 1537, 1555, 1827, 1858, 1889, 1923, 1942, 1966, 1990, 2005, 2024, 2083, ... |
| 14   | 17          | `#endif / return; / else`                                                                                       | 651, 705, 949, 1791, 1811, 3632, 3786, 3815, 3953, 4021, 4047, 4073, ...    |
| 15   | 16          | `#endif / else / if(commandCheck(msg_text+N, (char*)"S") == N)`                                                 | 752, 1009, 1036, 1059, 1188, 1852, 1883, 1912, 2957, 2981, 3347, 3910, ...  |

---

<!-- source: A-R1-ringbuffers.md -->

## A-R1 — Message rings and queues (structural RAM audit)

Scope: `BLEtoPhoneBuff`, `BLEComToPhoneBuff`, `ringBuffer` (TX), `ringBufferUDPout`,
`ringbufferRAWLoraRX`, dedup ring, `externQueue`, backpressure state, ACK tables,
capture/instrument buffers. HEAD `c51c5881`, branch `fork-main`. AUDIT ONLY — nothing changed.

## Measured slot occupancy (basis for every number below)

| Ring                  | decl                          | slot bytes                  | max bytes a producer can write                                            | dead bytes/slot |
| --------------------- | ----------------------------- | --------------------------- | ------------------------------------------------------------------------- | --------------- |
| `BLEtoPhoneBuff`      | `loop_functions_extern.h:267` | `MAX_MSG_LEN_PHONE+5` = 305 | 1 len + 255 payload + 4 ts = **260** (clamp `loop_functions.cpp:586-589`) | **45**          |
| `BLEComToPhoneBuff`   | `loop_functions_extern.h:272` | 305                         | 1 len + 245 = **246** (clamp `loop_functions.cpp:651-656`)                | **59**          |
| `ringBufferUDPout`    | `loop_functions_extern.h:260` | `UDP_TX_BUF_SIZE+20` = 275  | 1 len + 255 = **256** (clamp `udp_functions.cpp:1729-1750`)               | **19**          |
| `ringBuffer` (TX)     | `loop_functions_extern.h:197` | `UDP_TX_BUF_SIZE+5` = 260   | 1 len + 1 status + 255 = **257** (guard `txring_functions.cpp:466-468`)   | 3               |
| `ringbufferRAWLoraRX` | `loop_functions_extern.h:255` | 260                         | `UDP_TX_BUF_SIZE-1` = **254** (`loop_functions.cpp:3235`)                 | 6               |
| `externQueue`         | `extudp_functions.cpp:65-73`  | 514 (`buffer[500]`)         | source is a `UDP_TX_BUF_SIZE`-bounded frame + JSON wrap                   | ≈244            |

Typical, not max: `capture_functions.cpp:26` documents field frames at **45–130 B**; 255 is only
reachable by a full-length text frame. Every ring above is dimensioned for the 255-byte worst case
in **every** slot, i.e. ~2–5× the working set. `cap_ring` (`capture_functions.cpp:34`) is the one
ring in the tree that already uses a byte pool with a 6-byte record header — the existence proof for
proposal R1-07.

`MAX_RING` = 20 on all three classes; `MAX_LOG` = 20 classic / 10 S3 / 10 nRF52; `MAX_RING_UDP` = 20
on all three (`configuration_global.h:203-240`). ACK tables (`ack_functions.h`,
`ack_attribution.h:29-33`) are pure `#define` + `static inline` — **zero RAM**, nothing to save.
`backpressure.h` state is a handful of scalars plus `static const` prefix tables (`:84-190`) — same.
`instrument.cpp:47` `s_sect[16]` = 16×24 B = 384 B, only compiled under `INSTRUMENT_ENABLED`.

## Proposals

| ID    | Title                                                                                      | files:lines                                                                                         | Change                                                                                                                                                            | RAM saved classic/S3/nRF52 (B)                         | Flash | LOC  | Risk | Wire contract | Effort | Verification                                                                                                                                                 |
| ----- | ------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------ | ----- | ---- | ---- | ------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| R1-01 | Size the two BLE slots to what the producers clamp to                                      | `loop_functions_extern.h:267,272`, `loop_functions.cpp:441,446`                                     | Replace `MAX_MSG_LEN_PHONE+5` by two new derived constants `BLE_TX_SLOT` (260) and `BLE_COM_SLOT` (246).                                                          | **2080 / 2080 / 2080**                                 | 0     | +6   | L    | none          | S      | `pio run -e <3 envs>` map diff; `--bledebug` frame dump byte-identical for a 255-B text, a 244-B JSON and an mheard frame                                    |
| R1-02 | Merge `BLEComToPhoneBuff` into `BLEtoPhoneBuff` with a routing tag                         | `loop_functions.cpp:441-450,580-676`, `phone_commands.cpp:60-210`, `web_functions.cpp:1843-1870`    | One 20-slot ring, byte 1 already carries the type (0x91/0x44/text); add a 1-byte `dest` column `bleSlotIsCom[MAX_RING]` so the web history skips command frames.  | **4980 / 4980 / 4980** (after R1-01)                   | −250  | −70  | M    | none          | M      | drain-order test on a live node: config reply + text burst interleaved; web rxlog shows text only                                                            |
| R1-03 | Trim `ringBufferUDPout` `+20` padding to `+1`                                              | `loop_functions_extern.h:260`, `loop_functions.cpp:436`                                             | `UDP_TX_BUF_SIZE+20` → `UDP_TX_BUF_SIZE+1`; the `+20` was reserve for the `len+1` overread already fixed by WF-01.                                                | **380 / 380 / 380**                                    | 0     | +2   | L    | none          | S      | `--udplog on`, compare UDP hex on the server for a 255-B frame; `sizeof(ringBufferUDPout[0])` uses at `udp_functions.cpp:673`, `nrf52_main.cpp:3029` recheck |
| R1-04 | `ringbufferRAWLoraRX` behind a runtime pointer, allocated on first web/rxlog use           | `loop_functions.cpp:428-430,3216-3236`, `lora_functions.cpp:697-709`, `web_functions.cpp:1302-1344` | Static array → `static unsigned char (*ringbufferRAWLoraRX)[260]`, `malloc` on the first `bWEBSERVER` rxlog request, writer no-ops on `NULL`.                     | **5200 / 2600 / 2600**                                 | +120  | +25  | M    | none          | M      | boot heap print with/without webserver; rxlog page content unchanged; nRF52 soak with webserver off for 12 h (no writer NULL-deref)                          |
| R1-05 | One refcounted frame pool shared by TX ring and UDP-out ring                               | `txring_functions.cpp:442-520`, `udp_functions.cpp:1727-1760`, `loop_functions_extern.h:197,260`    | 24-slot `uint8_t pool[24][257]` + `uint8_t refcnt[24]`; both rings store a 1-byte pool index instead of the payload; the RX path that feeds both stores one copy. | **4128 / 4128 / 4128** (vs. R1-01/03 baseline)         | +400  | +130 | H    | none          | L      | `test/test_txring` extended with pool invariants (refcnt≥1 for every referenced slot, 0 leaks after full drain); 24 h dual-role gateway soak                 |
| R1-06 | Shrink `externQueueEntry.buffer` 500 → 264                                                 | `extudp_functions.cpp:65-73`                                                                        | The queued frame is a `UDP_TX_BUF_SIZE`-bounded LoRa frame; 500 is unjustified headroom.                                                                          | **472 / 472 / 472**                                    | 0     | +1   | L    | none          | S      | EXTUDP injection of a max-length frame, JSON on port 1799 byte-identical                                                                                     |
| R1-07 | Byte-pool (slab) for the BLE ring instead of fixed slots                                   | `loop_functions.cpp:580-676`, `phone_commands.cpp:60-210`                                           | Replace the `[20][260]` matrix by `uint8_t ble_pool[2048]` + head/tail, records `len(2) type(1) payload(n)`, exactly the `cap_ring` layout.                       | **3132 / 3132 / 3132** (on top of R1-01, before R1-02) | +260  | +90  | H    | none          | L      | `cap_ring`-style native unit test on wrap/short-write/overflow; 245-B frames at ring-wrap boundary                                                           |
| R1-08 | Fold `retryCount` / `ringPriority` / `ringSource` / `ringEnqueueTime` into one slot struct | `loop_functions_extern.h:200-203`, `txring_functions.h:31`                                          | Four parallel `[MAX_RING]` arrays → `struct TxSlotMeta meta[MAX_RING]` (10 B, packed).                                                                            | **0 / 0 / 0** (same bytes; removes 3 relocation sites) | −80   | −30  | L    | none          | M      | `test/test_txring` unchanged and green; eviction/relocation path (`txring_functions.cpp` N-24 block) copies one struct                                       |

Totals if R1-01..04 + R1-06 are taken (mutually compatible, R1-05/07 excluded):
**13 112 B classic / 10 512 B S3 / 10 512 B nRF52**.
R1-02 supersedes R1-07; R1-05 supersedes nothing but conflicts with neither.

## Design notes (top 5)

**R1-01 — slot right-sizing.** `MAX_MSG_LEN_PHONE` (300, `configuration_global.h:358`) is a _phone
message_ limit and was never the ring's real bound. `addBLEOutBuffer()` clamps at
`UDP_TX_BUF_SIZE-4` / `UDP_TX_BUF_SIZE` (`loop_functions.cpp:586-589`) and appends 4 timestamp bytes
(`:616-618`), so 1+255+4 = 260 is the hard maximum a slot can ever hold. `addBLEComToOutBuffer()`
clamps at 245 (`:651-656`, the reason documented at `configuration_global.h:361-368`), so 246. Propose
`#define BLE_TX_SLOT (UDP_TX_BUF_SIZE + 5)` and `#define BLE_COM_SLOT 246` next to
`BLE_JSON_PAYLOAD_MAX`, and use them in both the definition and the extern. The consumers'
`uint8_t toPhoneBuff[MAX_MSG_LEN_PHONE]` / `ringSnapshot[MAX_MSG_LEN_PHONE]`
(`phone_commands.cpp:65,76,161`) stay at 300 — they are stack, sized by the same wrong constant, and
are out of scope here. Single-writer discipline untouched: no index or lock changes.

**R1-02 — one BLE ring, two destinations.** `sendToPhone()` (`phone_commands.cpp:60-140`) and
`sendComToPhone()` (`:145-210`) are near-verbatim clones: same 0x91 / 0x44 / text dispatch on byte 1,
same `blelen+2` write. Byte 1 already _is_ the type tag, so no new in-band field is needed on the BLE
side — only a side array `uint8_t bleSlotIsCom[MAX_RING]` so that the web message history
(`web_functions.cpp:1843-1870`, whose slot-shortage is already noted at `:874`) keeps showing text
frames only. The merge is a net concurrency _improvement_: `addBLEOutBuffer()` runs its slot fill
under `taskENTER_CRITICAL()` and uses `addRingPointer()` (`loop_functions.cpp:594-628`), while
`addBLEComToOutBuffer()` has **no lock and a raw `ComToPhoneWrite++` wrap** (`:660-676`) that
overwrites unread slots silently — C15 bookkeeping, C10 multi-writer. After the merge there is one
writer entry point, one lock, one overflow policy. Wire bytes to the phone are unchanged; only the
interleaving of config replies and text frames becomes strictly FIFO instead of two independent
drains, which is the correct behaviour on a single BLE link.

**R1-04 — RAW ring on demand.** `ringbufferRAWLoraRX` has exactly one consumer,
`web_functions.cpp:1342` (the rxlog page). Its single writer is `charBuffer_aprs()`
(`loop_functions.cpp:3216-3236`), called unconditionally from the RX path (`lora_functions.cpp:699`)
with **no `bWEBSERVER` guard** — every received frame gets an `snprintf` of ~250 B into a ring nobody
reads on a node without a web server. Proposal: pointer + lazy `malloc(MAX_LOG*260)` on the first
rxlog request, writer early-returns while `NULL`. The writer runs on the nRF52 timer-service task
(C10) and the allocator does not: allocate from the loop side only (the web handler), publish with a
release store, writer reads with acquire — the same discipline `cap_head`/`cap_tail` already use
(`capture_functions.cpp:37-39`). Never free. Biggest single win on classic ESP32 because `MAX_LOG` is
20 there and 10 elsewhere (`configuration_global.h:216,238`).

**R1-05 — shared frame pool.** S3's "3–5 copies" resolves on inspection to **two byte-identical
copies plus three re-renderings**: `ringBuffer` (raw frame + 2-byte header,
`txring_functions.cpp:498-500`) and `ringBufferUDPout` (raw frame + 1-byte len,
`udp_functions.cpp:1744-1750`) hold the same bytes; `BLEtoPhoneBuff` holds a JSON/text rendering,
`ringbufferRAWLoraRX` an ASCII log line, `externQueue` a JSON — those three cannot be deduplicated.
So the pool covers exactly the first two: `uint8_t framePool[24][257]`, `uint8_t frameRef[24]`, both
rings become `uint8_t slotRef[MAX_RING]`. 24 < 40 is deliberate (both rings full of _distinct_
frames is not a reachable steady state; the relay-and-gateway case shares), and pool exhaustion maps
onto the existing BP-01 back-pressure path rather than a new failure mode. Risk is H and effort L
because the TX ring's eviction path relocates the `iRead` entry with `memcpy` and mutates
`ringPriority`/`ringEnqueueTime` in the same critical section (C10's explicit warning); refcount
decrements must join that section, and the UDP drain
(`udp_functions.cpp:665-762`, `nrf52_main.cpp:3029-3095`) runs on the other task. Single-writer
discipline is _not_ preserved for `frameRef[]` — it becomes two-writer under the existing
`taskENTER_CRITICAL()`. Recommend only after R1-01..04 have banked the cheap bytes.

**R1-07 — BLE slab.** `cap_ring` (`capture_functions.cpp:29-90`) is the working template: a flat
byte array, a 6-byte record header, `head`/`tail` as `std::atomic<uint16_t>`, a `capFree()` that
distinguishes the wrap cases explicitly instead of a modulo (`:61-74`) — that comment records a bug
already paid for once. A 2048-byte BLE pool holds ~16 typical 130-B frames or 7 maximal ones, versus
today's 20 fixed slots; the trade is depth-in-bytes instead of depth-in-frames, which is the honest
metric for a BLE link. Listed but **not recommended over R1-02**: R1-02 saves more, is O(1) per
frame, and does not put a variable-length record layout inside the nRF52 critical section that
`addBLEOutBuffer()` already holds.

## Rejected

- **Plain `MAX_*` count cuts** on any ring — done in MEM-01 (S4 items 1/3/4); out of scope by brief.
- **`MAX_DEDUP_RING` change** — settled by the DG0OPK field measurement recorded at
  `dedup_functions.h:18-38`; the ring is 5 B/slot (350/500/500 B total), not worth touching either way.
- **Variable-length slab for the TX ring** (`ringBuffer`) — the slot _index_ is the identity key for
  `retryCount`, `ringPriority`, `ringEnqueueTime`, `ringSource` and for the priority-eviction
  relocation (`txring_functions.cpp` N-24 block). A slab breaks index stability and would require
  re-implementing eviction as compaction inside a critical section. C10/C15 territory; no.
- **Slab for `ringBufferUDPout`** — superseded by R1-05, which removes the whole array rather than
  compacting it.
- **Merging `ringbufferRAWLoraRX` into the capture ring** — different lifecycle (`cap_ring` is a
  short live trace drained by the loop within seconds; the RAW ring is a 10–20 frame history the web
  page walks on demand) and different content (binary frame vs. formatted ASCII). R1-04 is cheaper.
- **Dropping the `+5` on `ringBuffer`** (260 → 257) — 60 B/class, and `sizeof(ringBuffer[0])` is
  load-bearing in the `clearSlotFirst` memset that closed TXRING-CLEARSLOT-GAP
  (`txring_functions.cpp:494-497`). Not worth re-opening.
- **Shrinking `instrument.cpp:47 s_sect[16]`** — 384 B and only present under `INSTRUMENT_ENABLED`;
  guard-change blast radius is documented in memory (INS-01 compiled out four unrelated commands).
- **ACK-table compaction** — `ack_functions.h` / `ack_attribution.h` allocate no RAM at all.
- **`backpressure.h` state compaction** — scalars plus `static const` tables in flash.
- **Making `externQueue` dynamic** — 2 entries; R1-06 recovers 472 B for one constant change, a
  pointer would cost more in code than it saves.

---

<!-- source: A-R2-callsigns.md -->

## A-R2 — Callsign Interning & mheard Row Compaction

Baseline HEAD c51c5881. Sizes from `src/configuration_global.h:198-240`:
S3/nRF52 `MAX_MHEARD 80` / `MAX_MHPATH 100`; classic ESP32 `30` / `40`; XML/SBUFFER `50` / `50`.

**Headline correction to S3:** `mheardBuffer[][60]` contains **no callsign**. Its sole writer is the
`snprintf` at `src/mheard_functions.cpp:396` and `:525`, format
`"%s|%s|%c|%i|%u|%i|%i|%.1lf|%i|%i|%i|"` = date|time|payload_type|hw|mod|rssi|snr|dist|path_len|mesh|ncount.
Every field is a scalar or a fixed-width timestamp. The callsign lives only in the parallel
`mheardCalls[][10]`. So the owner's "index instead of callsign" idea does **not** apply to the
largest buffer — the win there is binary-row-plus-render-on-demand (R2-01), which is independent of
any intern table. The one place many callsigns really are stored as text is
`mheardPathBuffer1[][52]` (`:612`), a comma-joined hop list — that is where interning pays.

## Proposal table

| ID    | Title                                                                | files:lines                                                                                                                           | Change                                                                                                                             | RAM saved classic / S3 / nRF52 (B)                                                      | Flash                                  | LOC  | Risk | Wire | Effort                                                                                                                                                                                             | Verification |
| ----- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- | -------------------------------------- | ---- | ---- | ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------ |
| R2-01 | mheard row → binary struct, text rendered on demand                  | mheard_functions.cpp:35,56,138-203,395-398,524-527,690-803; web_functions.cpp:1441-1443; t-deck-pro/ui_deckpro.cpp:1327               | Replace `mheardBuffer[N][60]` + `mheardNCount[N]` (int) by `MheardRow[N]` (16 B), format the pipe/console/JSON text at output time | **1560 / 3840 / 3840**                                                                  | −180 (parser dropped, formatter added) | −60  | M    | none | `pio test -e native_parsers -f test_decodemheard -f test_mheard_aging`; golden byte-diff of `--mheard` console dump and of the 0x44 "MH" BLE JSON before/after on DK5EN-90                         |
| R2-02 | path row → hop-index array (needs R2-03)                             | mheard_functions.cpp:66,590-613; web_functions.cpp:1501; mheard_functions.cpp:824                                                     | Replace `mheardPathBuffer1[M][52]` by `{uint8_t n; uint8_t idx[7];}` (8 B), re-join `"A,B,C"` from the intern table at print time  | **1760 / 4400 / 4400**                                                                  | +260                                   | +45  | H    | none | golden byte-diff of `--path` dump + web `/mheard` page over a 12 h bench capture; new native test replaying 200 recorded HEY source paths through split→join                                       |
| R2-03 | callsign intern table + index-ify the two call arrays                | new `src/callsign_table.h`; mheard_functions.cpp:36,67,368-372,603-607; lora_functions.cpp:765-767,832-834; via_functions.cpp:124-131 | One `CsEntry` table (N=96 classic / 192 S3+nRF52), `uint8_t` index in mheard/path rows, 0 = none                                   | **−426 / −492 / −492** (net loss standalone; bundled with R2-02: +1334 / +3908 / +3908) | +620                                   | +130 | H    | none | native unit test for intern/lookup/evict + refcount invariant; assert-on-dangling-index build flag run for 24 h on bench                                                                           |
| R2-04 | `aprsMessage` / `mheardLine` callsign+path `String` → fixed `char[]` | aprs_structures.h:20-26,70-76; aprs_functions.cpp:123ff; mheard_functions.cpp:119-136                                                 | 6 of 7 `String`s become `char msg_source_call[10]`, `char msg_*_path[64]`; `msg_payload` stays `String`                            | 0 static; **heap peak −450…−550 B/frame, 12-14 fewer malloc/free per RX**               | +150                                   | +120 | M    | none | `test_aprs_decode`, `test_aprs_reencode`, `test_aprs_corpus`, `test_aprs_fuzz` unchanged-green; `--heap` watermark over 1 h bench traffic; BLE connect success rate (MEMORY: printf-malloc/NimBLE) |
| R2-05 | `mheardPathBuffer1` 52 → 40 B (fallback if R2-02 rejected)           | mheard_functions.cpp:66,593-594,613; loop_functions_extern.h:446                                                                      | Physical trim only; truncation bound `ipc > 51` → `> 39`                                                                           | 480 / 1200 / 1200                                                                       | 0                                      | ~6   | L    | none | `--path` dump on a node with 4-hop paths; confirm no path >39 chars in the field corpus                                                                                                            |
| R2-06 | `mheardNCount[]` `int` → `uint8_t` (subsumed by R2-01)               | mheard_functions.cpp:56                                                                                                               | ncount is 0-99, printed `%3i`                                                                                                      | 90 / 240 / 240                                                                          | 0                                      | 1    | L    | none | `--mheard` column diff                                                                                                                                                                             |
| R2-07 | `HardWare[36]` `String[]` → `const char* const[]`                    | mheard_functions.cpp:84-86                                                                                                            | Static string table; `getHardwareLong()` returns `const char*`                                                                     | ~430 static + ~450 heap, all platforms                                                  | +140 (literals to .rodata)             | 12   | L    | none | `--mheard` hardware column byte-diff across all 36 ids                                                                                                                                             |
| R2-08 | settings callsign fields — no change                                 | esp32_flash.h:17,143,180,187; WisBlock-API.h:187,305,341,348                                                                          | Explicitly leave as-is                                                                                                             | 0                                                                                       | 0                                      | 0    | —    | none | —                                                                                                                                                                                                  | n/a          |

**Bundle totals (R2-01 + R2-02 + R2-03 + R2-06 + R2-07):**
classic ≈ **3.9 kB**, S3 ≈ **9.0 kB**, nRF52 ≈ **9.0 kB** DRAM.
Classic ESP32 is the constrained platform (MEM-04: E22_XML 920 B DRAM free), and R2-01 alone
already buys it 1.5 kB at L/M risk without touching any callsign representation.

---

## R2-01 — mheard row as a binary struct

`decodeMHeard()` (`:138-203`) is a hand-written pipe-field parser that exists only because the row
was stored as text; `sendMheard()` (`:705-733`) re-parses the same row a second way via
`getValue()`. Both disappear. The date/time pair is the only subtle field: it is written from
`getDateString()`/`getTimeString()` at `lora_functions.cpp:742`, i.e. local time at RX. Do **not**
re-derive it from `mheardEpoch[]` (that is `getUnixClock()`, a different clock on a node with no
NTP, and `showPath()` at `:820` shows the codebase applies `node_utcoff` by hand elsewhere) — store
an explicit local stamp in the row. Render with the identical `snprintf`/`printfdeb` formats so the
console table, the web card (`web_functions.cpp:1443`) and the `MH` JSON stay byte-identical.

```c
struct MheardRow {            // 16 B, replaces mheardBuffer[i][60] + mheardNCount[i]
    uint32_t stamp_local;     // local-time epoch, source of "YYYY-MM-DD" / "HH:MM:SS"
    int16_t  rssi;            // was %i from int16_t
    int16_t  dist_dkm;        // distance * 10, keeps "%.1lf"; -10 == "unknown" sentinel
    char     payload_type;    // ':' '!' '@'
    uint8_t  hw;
    uint8_t  mod;
    int8_t   snr;
    uint8_t  path_len;
    uint8_t  mesh;
    uint8_t  ncount;
    uint8_t  _pad;            // explicit; sizeof == 16, alignof == 4
};
```

Risk M, not L: `%.1lf` of a `double` and `%.1f` of `dist_dkm/10.0f` must be shown equal over the
field corpus (half-way values), and truncated legacy rows (the case `:175-188` documents) have no
counterpart in a fixed struct — a fresh boot is fine, a T-Deck `/mheard.dat` restore
(`:227-228`) is not, so `loadMHeardPersistence()` needs a format tag or a one-time discard.

## R2-02 — path row as hop indices

`mheardPathBuffer1[i]` holds `mheardLine.mh_sourcepath.substring(ips)` (`:612`), i.e. the source
path after the first comma — a comma-separated relay list, truncated at 51 chars, hop count already
kept separately in `mheardPathLen[i]` and bounded by `MAX_HOP_LIMIT 7`
(`configuration_global.h:~248`). 52 B of text for at most 7 callsigns.

```c
struct MhPathRow {            // 8 B, replaces mheardPathBuffer1[i][52]
    uint8_t n;                // hop count actually stored, 0..7
    uint8_t hop[7];           // intern index per hop, 0 == empty
};
```

Byte-identical output means reproducing two artefacts of the text form: the 51-char truncation
(`:593-594`) and any hop token that is not a plausible callsign. Rule: if a token exceeds the
entry width or fails the `[A-Z0-9-]` test (`ack_attribution.h:95-111`), the whole row falls back to
`n = 0` and is not shown — that changes output. Safer variant: keep one shared 51-byte overflow
slot for such rows. This is why the risk is H and why R2-02 must ship behind a bench diff over a
12 h capture, not a unit test alone.

## R2-03 — the intern table

Interning is _not_ the rejected idea from `docs/archive/ram-opti.md` #12 (S4 §4). That proposal hashed a
callsign into a short tag and accepted collisions because no hop-hash exists on the wire. Interning
stores the callsign **verbatim, exactly once**, and hands out an index that never leaves RAM — no
hash, no collision, nothing derived from or written to the wire. The wire path
(`encodeAPRS`/`decodeAPRS`) keeps handling full ASCII callsigns.

```c
typedef struct { char base[7]; uint8_t ssid; } CsEntry;   // 8 B, ssid 0..99, 255 == no SSID
static CsEntry  cs_tab[CS_N];        // CS_N = 96 classic, 192 S3/nRF52
static uint8_t  cs_ref[CS_N];        // refcount: mheard row + path row + each hop slot
static uint8_t  cs_lru[CS_N];        // tick, bumped on lookup hit
// index 0 is reserved as "none" and never allocated
```

Sizing: `MAX_MHEARD + MAX_MHPATH` distinct owners plus relay hops that are usually already
neighbours, so 192 covers S3/nRF52 with headroom and `uint8_t` suffices (no `uint16_t` needed at
these MAX_* values — that alone saves 100 B per referencing array). Splitting base+SSID to 8 B is
worth it (−2 B/entry, no code cost beyond one `snprintf("%s-%u")`); 6-bit packing to 5 B is **not**
(−576 B on S3 for a pack/unpack pair, a hard 36-char alphabet, and it corrupts any call that is not
`[A-Z0-9]` — see `APRS2SOTA`/`WLNK-1` at `lora_functions.cpp:1237`). A 7-char base is needed, not
6: `100001` and gateway pseudo-calls appear in the same code paths.

Eviction, 10 lines: allocation happens only in `updateMheard()` (`:368-372`) and `updateHeyPath()`
(`:603-612`, plus one per hop). Each store `cs_ref[i]++`, each row overwrite/prune `cs_ref[old]--`.
A slot is reusable only at `cs_ref == 0`; the victim among free slots is the largest `cs_lru` age,
i.e. tied to the same `mheardMillis[]`/`mheardPathMillis[]` 12 h prune (`MHEARD_PRUNE_WINDOW_MS`,
`:64`) that already frees rows. Failure modes: (a) **slot reuse while a path hop still points at
it** — a path row is freed by `mheardPathCalls[iset][0] = 0x00` at `:548` and `:828` _without_
touching the hop array, so those two sites must decrement all `n` hops or every stale index silently
renames a relay; (b) **table full with every entry referenced** — must fail closed (return 0 =
"none", drop the hop) rather than steal a live slot; (c) **index 0** must be unallocatable, else an
uninitialised row renders a real callsign; (d) refcount overflow at >255 references — clamp and
never decrement a clamped entry.

## R2-04 — String fields in `aprsMessage` / `mheardLine`

`aprs_structures.h:20-26` has 7 `String`s per `aprsMessage` and `:70-76` 7 more per `mheardLine`;
both are constructed per RX frame (`decodeAPRS`, then `initMheardLine()` at `:119-136`, then
`updateMheard()` which copies right back out to `char` at `:372`). That is ~14 malloc/free pairs
and ~450-550 B of transient heap per received frame, on the same allocator that MEMORY.md
("printf malloc starves NimBLE") already records as the cause of BLE connection aborts.

```c
struct aprsMessage {
    /* … scalars unchanged … */
    char   msg_source_path[64];
    char   msg_source_call[10];
    char   msg_source_last[10];
    char   msg_destination_path[64];
    char   msg_destination_call[10];
    String msg_payload;          // stays String: up to 300 B, genuinely variable
    char   msg_gateway_call[10];
};
```

Static RAM is unchanged (these live on the stack/one global), `sizeof` grows ~60 B, and the nRF52
`OnRxDone` stack is 16 kB (MEMORY: "nRF52 OnRxDone in LORA task") so the extra frame is affordable.
A `(ptr,len)` view into `RcvBuffer` would be cheaper still but is unsafe here: the RX buffer is
reused before the frame is done being processed (MEMORY: "RX buffer stale tail"). Fixed arrays, not
views.

---

## Rejected

- **`uint16_t` intern index** — `MAX_MHEARD + MAX_MHPATH` ≤ 180 everywhere, `uint8_t` is enough; a
  16-bit index costs 100 B extra on S3/nRF52 for nothing.
- **6-bit charset packing (5 B/entry)** — saves 576 B on S3, costs pack/unpack code, and cannot
  represent `WLNK-1` / `APRS2SOTA` / `100001` which flow through the same comparison sites
  (`lora_functions.cpp:1237,1324-1330`).
- **Interning `mheardBuffer[][60]`** — no callsign is stored there (`:396`); nothing to intern.
- **Deriving mheard date/time from `mheardEpoch[]`** — different clock source than the stored
  strings; would silently shift displayed times on clockless nodes (NC-01, `:42-54`).
- **R2-03 as a standalone item** — net **loss** of 426 B (classic) / 492 B (S3, nRF52); the table
  plus refcounts costs more than the two `[10]` arrays it replaces. Only ship it with R2-02.
- **`(ptr,len)` views into the RX frame for R2-04** — RX buffer is not cleared between receives
  (MEMORY: "RX buffer stale tail"); a view can outlive its backing bytes.
- **Any change to `s_meshcom_settings` callsign fields, incl. reinterpreting `node_via[40]` as an
  index list** — the nRF52 path is a raw `memcpy` (S5: BLE char 0xF0A1,
  `nrf52_ble.cpp:297,339`), so layout is frozen and a `FLASH_STRUCT_VERSION` bump would wipe every
  fielded node. Even holding the layout, `node_via` cannot be reinterpreted: the phone app and
  `web_setup.cpp:69` write and `strcmp` it as ASCII text, and `checkVia()`
  (`via_functions.cpp:100-105`) concatenates it straight into `msg_destination_path`, which goes on
  the wire. It must stay a text list.
- **Raising/lowering `MAX_MHEARD` / `MAX_MHPATH` / `MAX_DEDUP_RING`** — count cuts are done (S4 §1
  items 6,7) and the dedup ring is settled; this audit changes bytes-per-row only.
- **Hashing hop callsigns (ram-opti.md #12)** — already rejected upstream of this audit; not
  re-proposed. R2-03 is verbatim storage, not hashing.

## Comparison hot paths

Cheaper after R2-03: the per-RX `is_equ()` linear scans — `mheard_functions.cpp:314` (O(MAX_MHEARD)
per RX), `:471` (a second full O(MAX_MHEARD) scan in `updateHeyPath()`), `:552` (O(MAX_MHPATH)),
`lora_functions.cpp:767` and `:834` — become `uint8_t` compares once the index is in hand. That is
5 of the 20 `is_equ()` sites in `src/` and the only ones on the RX path.

More expensive: the intern lookup itself. Interning a callsign is an O(CS_N) `strcmp` over 192
entries on every RX (twice, for source and path owner) — strictly worse than today's single
O(MAX_MHEARD) scan unless a 256-entry `uint8_t` hash-head bucket array (+256 B, first-char ⊕ length)
is added. Budget that 256 B inside R2-03; without it R2-03 trades RAM for CPU on the hottest path
and should not ship.

Untouched: the 110 `strcmp` sites in `src/` are dominated by literal/destination comparisons
(`destination_call` vs `"*"`, `"WLNK-1"`, `"100001"`, `node_call` — `lora_functions.cpp:983,1159,
1188,1237,1324-1330,1400`, `udp_functions.cpp:381-423`, `nrf_eth.cpp:539-592`). None of those
operands is an interned callsign; they stay `strcmp` and are unaffected.

---

<!-- source: A-R3-heap-iram.md -->

## A-R3 — Heap churn, static locals, classic-ESP32 IRAM/DRAM levers, task stacks

Measured against `.pio/build/ttgo_tbeam/firmware.map` (2026-09-10 09:06, clean `fork-main`).
Live guard reading on that map: `dram0_0_seg 114268/124580` (headroom 10 312 B),
`iram0_0_seg 131052/131072` (**headroom 20 B**). On the T-Beam family IRAM is the binding region
and **our own `src/*.o` contribute 0 bytes of `.iram0.text`** — every IRAM lever below is a
library or board-flag lever. On `E22_XML-DevKitC` DRAM (848 B) is the binding region.
Sign convention: positive = bytes freed.

## Proposals

| ID    | Title                                             | files:lines                                                                                                     | Change                                                                                                                                                    | static DRAM saved classic / S3 / nRF52         | heap / stack delta                                                                                                                | IRAM delta (classic) | flash delta        | risk | wire contract                                       | effort | verification                                                                                                                                       |
| ----- | ------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | -------------------- | ------------------ | ---- | --------------------------------------------------- | ------ | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| R3-01 | `adc_chars` array sized by `sizeof`               | `src/batt_function_old.cpp:198`                                                                                 | Make `esp_adc_cal_characteristics_t adc_chars` a scalar and pass `&adc_chars`; today it is an array of `sizeof(struct)` elements.                         | **+1 260 / +1 260 / 0**                        | 0                                                                                                                                 | 0                    | 0                  | L    | none                                                | S      | `nm -S firmware.elf \| grep adc_chars` (0x510 → 0x24); `resource_watch.py regions` on `E22_XML-DevKitC`                                            |
| R3-02 | T-Beam PSRAM unflag                               | `variants/ttgo_tbeam{,_SX1262,_SX1268}/platformio.ini`                                                          | `build_unflags = -DBOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue` so `spiram_psram.c` is never linked.                                                    | +72 / n/a / n/a                                | −4 MB PSRAM heap **iff** PSRAM actually inits                                                                                     | **+4 952**           | ~+4 400            | M    | none                                                | S      | bench T-Beam-92 `[PSRM]` line first (0 ⇒ free); then `regions --env ttgo_tbeam`                                                                    |
| R3-03 | Drop OneWire on the T-Beam family                 | `variants/ttgo_tbeam*/configuration.h` (`OneWire_GPIO 4`)                                                       | Remove `OneWire_GPIO` (or gate it) on the three IRAM-tight envs; `libOneWire.a` costs 1 121 B of IRAM there.                                              | +103 / 0 / 0                                   | 0                                                                                                                                 | **+1 121**           | ~+2 600            | M    | none                                                | S      | map attribution: `libOneWire.a(OneWire.cpp.o)` absent from `.iram0.text`                                                                           |
| R3-04 | Vendor tinyxml2 without `contrib/`                | `variants/E22_XML-DevKitC/platformio.ini:lib_deps`, new `lib/tinyxml2/library.json`                             | `srcFilter +<tinyxml2.cpp>` so `html5-printer.cpp`'s `main()` stops pulling `iostream`/`locale_init`.                                                     | +5 768 (E22_XML only) / 0 / 0                  | 0                                                                                                                                 | +572                 | ~−40 000           | L    | none                                                | M      | zero `_ZSt4cout` / `locale_init.o` in the map; `regions --env E22_XML-DevKitC`                                                                     |
| R3-05 | `HardWare[36]` `String` → `const char* const[]`   | `src/mheard_functions.cpp:84,86,850`; 8 call sites                                                              | Flash pointer table; `getHardwareLong()` returns `const char*`. `.bss.HardWare` measured 0x240.                                                           | +576 / +576 / +576                             | −36 boot mallocs ≈ −900 B heap; −1 `String` temporary per call site                                                               | 0                    | ~+150              | L    | none                                                | M      | `nm -S \| grep HardWare` gone from `.bss`; `[HEAP]` boot line                                                                                      |
| R3-06 | `strCountry[17]` `String` → `const char* const[]` | `src/lora_setchip.cpp:62,64,71,78`; `src/settings_sanitize.cpp:99`                                              | Same pattern. `.bss.strCountry` measured 0x110.                                                                                                           | +272 / +272 / +272                             | −17 boot mallocs ≈ −420 B heap                                                                                                    | 0                    | ~+70               | L    | none                                                | S      | `nm -S \| grep strCountry`; native `test_settings_sanitize`                                                                                        |
| R3-07 | `decodeAPRS` three 255-B stack scratch buffers    | `src/aprs_functions.cpp:184-192`                                                                                | `cConcat2`/`cConcat3` hold a single callsign (≤ 20 B) and `cConcat1` a path/payload; size them 255/24/24 and drop the five full-width `memset`s.          | 0 / 0 / 0                                      | **stack −462 B per RX frame** (765 → 303); ~1 275 fewer bytes memset per frame                                                    | 0                    | ~−40               | L    | none                                                | S      | `-fstack-usage` on `decodeAPRS`; nRF52 `uxTaskGetStackHighWaterMark` on the LORA task; native `test_aprs_decode`/`test_aprs_corpus` byte-identical |
| R3-08 | By-value `String` parameters on the RX path       | `src/aprs_functions.cpp:28,53,556` (+ headers `aprs_functions.h:15,23,24`)                                      | `decodeAPRSPOS(String)`, `CheckGroup(String)`, `CheckOwnGroup(String)` → `const String&`.                                                                 | 0                                              | −3 malloc/free and ≈ −140 B transient heap per position frame (payload ~64 B block + 12 B header, two callsign copies 16+12 each) | 0                    | ~−200              | L    | none                                                | S      | native `test_aprs_decode`; heap trace or `[HEAP]` delta over a 100-frame inject                                                                    |
| R3-09 | `getTimeString()` heap alloc in log lines         | `src/loop_functions.cpp:3143`; 75 call sites                                                                    | Add `getTimeStr(char*, size_t)` (the `setlogPrint()` pattern at :3151 already exists) and convert the hot RX/TX call sites.                               | 0                                              | −1 malloc/free ≈ −28 B per log line; RX paths print 2–4 lines per frame ⇒ −56…−112 B churn per frame                              | 0                    | ~−300              | L    | none                                                | M      | grep for remaining `getTimeString().c_str()`; BLE-connect soak (see the `printf`-malloc/NimBLE precedent)                                          |
| R3-10 | Softserial `String` tables                        | `src/softser_functions.cpp:334-339`                                                                             | `strPARM/strPARM_ID/strUNIT[10][5]` (150 `String` = 2 400 B) + `strSID/strSNAME[10]` (320 B) → fixed `char` arrays.                                       | +2 720 (E22_XML/`ENABLE_SOFTSER` only) / 0 / 0 | −up to 170 heap blocks ≈ −4 kB heap                                                                                               | 0                    | ~+200              | M    | none                                                | M      | `regions --env E22_XML-DevKitC`; softserial XML bench frame                                                                                        |
| R3-11 | `MC_CAPTURE` default on IRAM/DRAM-tight envs      | `src/capture_functions.h:49-50`, `src/capture_functions.cpp:34,222`                                             | Default `MC_CAPTURE 0` on classic ESP32 (or follow `INSTRUMENT_ENABLED`): `cap_ring[768]` + `captureDrain()::line[606]` + 14 B = 1 388 B `.bss` measured. | +1 388 / 0 / 0                                 | 0                                                                                                                                 | 0                    | ~+1 100            | M    | none                                                | S      | map: `capture_functions.cpp.o` DRAM 1 388 → 1; `--capture` command must still answer "compiled out"                                                |
| R3-12 | `mheardLat`/`mheardLon` `double` → `float`        | `src/mheard_functions.cpp:37,38`                                                                                | Positions are ≤ 6 decimals; `float` halves both arrays.                                                                                                   | +240 / +640 / +640                             | 0                                                                                                                                 | 0                    | ~−400 (soft-float) | M    | none                                                | S      | native mheard test; compare printed lat/lon on a bench frame to 5 decimals                                                                         |
| R3-13 | `mheardPathBuffer1[100][52]` → `[…][40]`          | `src/mheard_functions.cpp:66`                                                                                   | `archive/ram-opti.md` #12's own cheap recommendation (trim only; hashing stays rejected).                                                                 | +480 / +1 200 / +1 200                         | 0                                                                                                                                 | 0                    | 0                  | M    | none                                                | S      | inject a max-length path frame, check MHeard path output not truncated                                                                             |
| R3-14 | `pendingDisplayMsg` global `aprsMessage`          | `src/lora_functions.cpp:147`, `src/aprs_structures.h:18-24`                                                     | Six `String` members held live forever; replace with fixed `char` fields (display truncates to 25 chars anyway).                                          | +~96 / +~96 / +~96                             | −6 permanently-held heap blocks ≈ −180 B                                                                                          | 0                    | ~+100              | M    | none                                                | M      | `[HEAP]` free after 1 h idle vs. baseline                                                                                                          |
| R3-15 | `JsonDocument` per EXTUDP datagram                | `src/extern_tele_json.h:41`, `src/extern_notice_json.h`, called from `src/extudp_functions.cpp:474-478,810-812` | ArduinoJson v7 `JsonDocument` heap-allocates its pool per call; reserve once or move to `snprintf`.                                                       | 0                                              | ~−600…−1 000 B transient heap per EXTUDP frame (12 keys, v7 pool block growth) — **magnitude unquantified without a heap trace**  | 0                    | −(unquantified)    | M    | none (byte-identical output is the acceptance test) | M      | native `test_extern_tele_json`/`test_extern_notice_json` must stay byte-identical; heap trace on the EXTUDP bench recipe                           |

## Design notes (top 5)

**R3-01** is the single best byte-per-risk item in this audit and it is a plain defect:
`esp_adc_cal_characteristics_t adc_chars[sizeof(esp_adc_cal_characteristics_t)]` declares 36
copies of a 36-byte struct because the array extent was written as the struct's size. The map
confirms `.bss.adc_chars 0x510` = 1 296 B where 36 B are used. Both users
(`esp_adc_cal_characterize` at :486, `esp_adc_cal_raw_to_voltage` at :799) already take a
pointer, so the fix is `adc_chars` → scalar plus `&adc_chars` at two sites. It is compiled on
every classic ESP32 and ESP32-S3 env except `BOARD_TRACKER`/`BOARD_HELTEC` — including
`E22_XML-DevKitC`, whose entire DRAM headroom is 848 B. This one line multiplies that headroom
by 2.5 and needs no hardware decision.

**R3-02** is the only lever that meaningfully moves the T-Beam IRAM number, and it is already
measured (`docs/mem-headroom-classic-esp32-20260905.md` §2): `spiram_psram.c` is 4 427 B of the
4 952 B recovered, and it is in IRAM because it runs with the cache disabled. It is blocked on
one bench observation, not on code: read `[PSRM]` on T-Beam-92 right after `[HEAP] (init)`. The
only field log available reads `[PSRM] 0`, i.e. PSRAM init already fails silently under our
build, in which case the unflag removes dead code. Do not ship it on the field evidence alone —
about a quarter of the fleet is on these three envs.

**R3-07** is the nRF52 C13 item on the RX path. `decodeAPRS` opens three `char[UDP_TX_BUF_SIZE]`
buffers (765 B) inside the callback chain, but `cConcat2` collects the last path element and
`cConcat3` the source callsign — both bounded by the regex check to a callsign, ≤ 20 bytes.
Sizing them 24 B each frees 462 B of stack on a 16 KB LORA task on nRF52 and on every ESP32
loop task, and removes five 255-byte `memset`s per frame. The bounds are already enforced by
the surrounding `(ib - 6) < 120` loop guards, so the change is a narrowing that the existing
`test_aprs_corpus` vectors prove byte-for-byte.

**R3-05 / R3-06** are the same shape: file-scope `String` tables of compile-time literals.
`HardWare[36]` costs 576 B of `.bss` plus 36 boot-time mallocs (Arduino `String` has no SSO;
every non-empty string mallocs, rounded to a 16-byte block plus ~12 B allocator header), and
`strCountry[17]` costs 272 B plus 17 allocations. As `const char* const[]` the pointer table
lands in `.flash.rodata` and the payload with it. `getHardwareLong()` must change its return
type to `const char*`; all eight call sites already consume it through `%s`/`.c_str()`.

**R3-04** is measured in the same headroom doc and is the whole answer for `E22_XML-DevKitC`:
the tinyxml2 GitHub tarball ships `contrib/html5-printer.cpp`, which defines `main()`; `crt0.o`
resolves `main` from that archive and drags in `std::cout`, `ios`, and `std::locale` —
3 512 + 1 368 + 1 248 + 544 B of DRAM plus stream objects. Vendoring `tinyxml2.cpp` alone under
`lib/tinyxml2/` with an explicit `srcFilter` removes all of it and ~40 kB of flash.

## Rejected

- **Custom IDF / `sdkconfig` rebuild** (`FREERTOS_PLACE_FUNCTIONS_INTO_FLASH`,
  `CONFIG_BTDM_CTRL_MODE_BLE_ONLY`, coredump off, WiFi FTM off, `CONFIG_ESP_ERR_TO_NAME_LOOKUP`).
  Explicitly rejected by `archive/ram-opti.md` #11 and out of scope for an upstream PR. Ceiling is large
  (`libbtdm_app.a` 30 874 B, `libfreertos.a` 15 521 B, `libc.a` 15 039 B of IRAM) but unreachable
  with prebuilt arduino-esp32 libs; `-DCONFIG_*` on the command line does not re-cook them.
- **ram-opti.md #10 — "U8g2 fonts sit in `.data`, move them to flash".** _Refuted._ On the
  current tree the fonts are already in flash-mapped `.rodata`: `u8g2_font_10x20_mf` at
  `0x3f41ece4` (0x1062 = 4 194 B) and `u8g2_font_6x10_mf` at `0x3f41fd46` (0x959 = 2 393 B),
  6 587 B total in the DROM window, 0 B in `dram0_0_seg`. The claimed −6.6 kB DRAM does not
  exist. Close the item; only _dropping_ the 10x20 face would save anything, and that is flash.
- **`IRAM_ATTR` review of our code.** Only two hits (`src/t5-epaper/io_extend.c:24`,
  `src/t-deck/tdeck_main.cpp:100`), neither compiled into any classic-ESP32 env. Our `src/*.o`
  contribute 0 B to `.iram0.text` on `ttgo_tbeam` — nothing to reclaim.
- **`-ffunction-sections -fdata-sections -Wl,--gc-sections`, `-Os`.** Already the espressif32
  Arduino default; adding them explicitly changes nothing measurable. Only
  `variants/t5_epaper/platformio.ini:26` restates `--gc-sections`.
- **`-fno-rtti -fno-exceptions -fmerge-all-constants -fno-threadsafe-statics`.** Exception
  metadata lands in `.flash.rodata_noload` (15 923 B, not flashed, not in RAM), so the RAM/IRAM
  yield is zero; any flash yield is unquantified and the flags risk breaking third-party libs
  that catch. Not worth a PR against a 20-byte IRAM problem.
- **Additional `RADIOLIB_EXCLUDE_*`.** `platformio.ini:139-166` already excludes 24 modules;
  the remaining SX126x/SX127x families are the ones in use. No further candidates.
- **NimBLE flag tightening.** `platformio.ini` `[esp32]` already sets
  `MAX_CONNECTIONS=1`, `MAX_BONDS=1`, `MAX_CCCDS=2`, both role-disables, `HOST_TASK_STACK_SIZE=3072`,
  `MSYS1_BLOCK_COUNT=4`. `libNimBLE-Arduino.a` holds 104 B of IRAM — negligible. Note the standing
  memory entry: mbedtls CMAC is unavailable, so the crypto-stack swap is not an option either.
- **`DISABLE_TLS_CONSOLE` / mbedtls trimming.** No TLS console exists in this tree (`net_console.cpp`
  is plain TCP with optional HMAC); nothing to disable.
- **`web_header_collect[1024]` static** (`src/web_functions/web_functions.cpp:536`). Deliberate
  ("BSS statt Heap") and load-bearing for CS-03. Moving it back to heap trades 1 024 B of `.bss`
  for a 1 kB allocation on every HTTP request on the most fragmented boards. Leave it.
- **`mheardPathBuffer1` callsign hashing/interning** (ram-opti.md #12) — previously rejected on
  collision risk; not re-proposed. Only the 52→40 trim survives as R3-13.
- **`MAX_DEDUP_RING` reduction** — settled elsewhere; the safe window is time-based, not
  count-based. Out of scope here.

## Task stacks (D) — no proposal

No `SET_LOOP_TASK_STACK_SIZE` or `CONFIG_ARDUINO_LOOP_STACK_SIZE` override exists in the tree;
ESP32 loop runs on the framework default 8 192 B (`docs/architecture/09-concurrency-map.md:33`),
nRF52 on the Adafruit-core 4 KB loop / 1 KB timer (C13). Our own tasks:
`src/net_console.cpp:422` `con_auth` 3 072 B, `src/t-deck-pro/peri_gps.cpp:78`,
`src/t-deck-pro/tdeck_pro.cpp:381`, `src/t5-epaper/peri_gps.cpp:77`,
`src/t5-epaper/peri_lora.cpp:171,176`, `src/t5-epaper/t5epaper_main.cpp:671` all 3 072 B —
none on a classic-ESP32 env. The only high-water-mark instrument in the tree is
`src/extudp_functions.cpp:434,731` (`[EXT];rx/tx;stack_hwm`), and it covers the nRF52 EXTUDP
chain only. **No measured high-water mark exists for any of these stacks**, so no shrink can be
justified: unquantified. The actionable item is the reverse direction — R3-07 and R3-09 reduce
the demand rather than the supply. If stack reclamation is ever wanted, add
`uxTaskGetStackHighWaterMark()` reporting to the `con_auth` and loop tasks first
(`docs/codequality-rules.md:203` already requires it and it has been open since the 2026-05 audit).

---

<!-- source: A-R4-display-ram.md -->

## A-R4 — Display / UI RAM Audit

Method: static grep + ELF ground truth via `xtensa-esp32-elf-nm -S` on existing `.pio/build/*/firmware.elf`
(E22-DevKitC = classic-ESP32 DRAM cliff, ttgo_tbeam = classic-ESP32 IRAM cliff, heltec_wifi_lora_32_V3/V4,
vision-master-e290, wireless-paper, t_deck, t_deck_pro). Address ranges: `0x3ffc0000-0x3fffffff` = internal
DRAM (`B`/`b` = `.bss`), `0x3f400000-0x3f7fffff` = flash-mapped DROM (`D`/`d` = `.rodata`/`.data` in flash).
No `-ffunction-sections -fdata-sections --gc-sections` found in `platformio.ini` (grep empty) — those flags
are Arduino-ESP32/ESP-IDF platform-package defaults, not repo-visible; dead-code-stripping behavior is
assumed but not repo-verifiable, so "unreferenced → free" claims below are marked accordingly.

## 1. Findings table

| ID    | Title                                                                                                               | Files:lines                                                                                                                                | Change                                                                                                                                                                                                                                                       | RAM saved                                                                                                                                                                                                                                                                                                                                                                                                                       | Flash delta                                                          | LOC delta                                                                    | Risk                                                                                                                                                                                | Wire contract | Effort | Verification                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ----- | ------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- | ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------- | ------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| R4-01 | T-Deck Pro: LVGL heap forced into internal DRAM despite PSRAM                                                       | `variants/t_deck_pro/platformio.ini:33` (`-include config/lv_conf.h`); `config/lv_conf.h:49-52` (`LV_MEM_CUSTOM 0`, `LV_MEM_SIZE 48*1024`) | Point `t_deck_pro`'s LVGL config at a PSRAM-backed config (`LV_MEM_CUSTOM 1` + `ps_malloc`/`ps_realloc`, same pattern already used by `src/t-deck/lv_conf.h:49-67` and `variants/t5_epaper/lv_conf.h:49-67`) instead of the generic `config/lv_conf.h`       | **48.0 KB static DRAM, t_deck_pro only** (confirmed: `.bss` symbol `work_mem_int$3696`, size `0xc000`=49152B, in `.pio/build/t_deck_pro/firmware.elf`)                                                                                                                                                                                                                                                                          | ~0 (PSRAM absorbs it; t_deck_pro already declares `BOARD_HAS_PSRAM`) | ~5-10 (new/adjusted lv_conf variant)                                         | M                                                                                                                                                                                   | none          | S      | rebuild t_deck_pro, `nm -S` confirm `work_mem_int` gone/moved off internal-SRAM address range, boot-test LVGL UI on hardware                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| R4-02 | U8g2 dual full-buffer objects + shared framebuffer allocated on boards with no physical OLED                        | `src/loop_functions.cpp:366-403` (`U8G2 *u8g2; U8G2_..._1(...); U8G2_..._2(...)`)                                                          | Gate the `#if` block at `loop_functions.cpp:361` with a per-board `HAS_OLED_U8G2` capability macro so E22-family boards (no OLED silkscreen/pins) skip both constructors entirely                                                                            | **1.40 KB DRAM** on boards without OLED: `u8g2_1`+`u8g2_2` = 188B+188B=376B (confirmed `nm`, both `.bss`, size `0xbc` each) + shared full-frame buffer `buf$xxxx`=1024B (confirmed: only **one** 1024B `.bss` symbol exists even though two `_F_` constructors are present — U8g2's `u8g2_m_16_8_1()` helper uses a function-local `static` buffer shared by same-size setups, so this is NOT 2×1KB, contrary to naive reading) | 0                                                                    | ~5 (macro + guard)                                                           | M (needs per-board hardware confirmation of "no OLED"; E22-DevKitC confirmed no OLED refs in `variants/E22-DevKitC/configuration.h`, but list of affected boards not fully audited) | none          | S      | `nm -S` before/after on E22-DevKitC elf; confirm `u8g2_1`/`u8g2_2`/`buf$` gone; physical no-op test (E22 already runs `bDisplayOff=true` at runtime, `esp32_functions.cpp:196`)                                                                                                                                                                                                                                                                                                                                                                                                                |
| R4-03 | Page-history ring allocated unconditionally, including on no-OLED boards                                            | `src/loop_functions.cpp:775-779`; sizes from `src/loop_functions_extern.h:475-484`                                                         | Same `HAS_OLED_U8G2`/display-capability guard as R4-02 around `pageLastLine`/`pageLastText`/`pageLastTextLong1`/`pageLastTextLong2` (declared unconditionally, `loop_functions_extern.h:493-496`, but only meaningful for page-cycling on a physical screen) | **2.90 KB DRAM** on no-OLED boards (confirmed via `nm`: `pageLastLine`=504B, `pageLastText`=1050B, `pageLastTextLong1`=150B, `pageLastTextLong2`=1200B, total 2904B, both E22-DevKitC and ttgo_tbeam elves match exactly)                                                                                                                                                                                                       | 0                                                                    | ~10 (guard placement, `pageLastPointer`/`iPage` call sites need no-op stubs) | M (same board-list risk as R4-02; ~15 call sites in `loop_functions.cpp` touch these arrays, all under existing `u8g2`-guarded code so likely already dead when `bDisplayOff`)      | none          | M      | `nm -S`; grep call sites stay inside existing `if(u8g2 != NULL)`/OLED-only blocks (spot-checked lines 1023-1247, all already inside u8g2 draw paths)                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| R4-04 | `src/t5-epaper/firasans_12.h` — 110 KB const array never `#include`d anywhere                                       | `src/t5-epaper/firasans_12.h:1-8087` (`const uint8_t FiraSans_12Bitmaps[110473]`)                                                          | Delete the file (or its dead symbol) — no TU includes it (only `firasans_20.h` is used, `t5epaper_main.cpp:33`)                                                                                                                                              | **0** (never compiled into any TU — not a linker/gc-sections question, the header is simply orphaned; costs nothing today)                                                                                                                                                                                                                                                                                                      | 0                                                                    | -8087 (pure deletion)                                                        | L                                                                                                                                                                                   | none          | S      | grep confirms zero includes outside its own file; safe delete, verify t5_epaper still builds                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| R4-05 | S4 prior-work item 10 ("U8g2 fonts forced into Flash, −6.6 kB DRAM potential") is stale — refute, do not re-propose | `src/loop_functions.cpp:977,1154,3281,3287,3291,4652,4654` (font refs); confirmed via ELF                                                  | No code change — close the open item                                                                                                                                                                                                                         | **0** (already correct)                                                                                                                                                                                                                                                                                                                                                                                                         | 0                                                                    | 0                                                                            | —                                                                                                                                                                                   | none          | —      | `nm -S .pio/build/E22-DevKitC/firmware.elf` shows `u8g2_font_6x10_mf` at `0x3f41f206` (D, DROM/flash-mapped) and `u8g2_font_10x20_mf` at `0x3f41e1a4` (D, flash) — **not** in the `0x3ffc-0x3fff` DRAM range. `U8X8_FONT_SECTION` (u8x8.h:169-170) is a no-op on ESP32 (not AVR), so plain `const` already lands in `.rodata`→flash via the standard Xtensa/ESP-IDF linker script. Font data is already flash-resident on all measured targets; the −6.6 kB claim in `docs/archive/ram-opti.md` #10 is incorrect for current toolchain/ESP32 target and should be marked closed, not reopened. |
| R4-06 | `u8g2_font_6x10_tf` referenced in source but absent from every measured ELF                                         | `src/loop_functions.cpp:1154,3281`; `src/esp32/esp32_functions.cpp:346`                                                                    | No action — flag for a follow-up dead-branch check, not a RAM item                                                                                                                                                                                           | 0 (nothing to save; symbol isn't linked)                                                                                                                                                                                                                                                                                                                                                                                        | 0                                                                    | 0                                                                            | —                                                                                                                                                                                   | none          | —      | `nm` on both E22-DevKitC and ttgo_tbeam elves shows no `u8g2_font_6x10_tf` symbol, only `u8g2_font_6x10_mf`/`u8g2_font_10x20_mf` (D, flash). Call sites likely sit behind a compile-time-false branch (`iDisplayType`/`bNeu` guard) that GCC eliminates — a dead-code audit (not RAM audit) should confirm and either delete the branch or explain it                                                                                                                                                                                                                                          |
| R4-07 | T-Deck Pro / T-Deck fonts and images already flash-correct                                                          | `src/t-deck-pro/src/Font_Mono_Bold_14..20.c`, `img_*.c` (11 files); `src/t-deck/mouse_cursor_icon.c`                                       | No action                                                                                                                                                                                                                                                    | 0                                                                                                                                                                                                                                                                                                                                                                                                                               | 0                                                                    | 0                                                                            | —                                                                                                                                                                                   | none          | —      | every `img_*.c` uses `const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST ... uint8_t x_map[]` (flash-forcing LVGL attributes); fonts use `static const lv_font_fmt_txt_*` structs (flash); `mouse_cursor_icon.c` is `const`. All referenced from `tdeck_main.cpp`/`ui_deckpro.cpp`. No savings available — already optimal                                                                                                                                                                                                                                                                  |
| R4-08 | T5-epaper images and `Font_Mono_Bold_90.c` already flash-correct and in use                                         | `src/t5-epaper/t5-src/img_*.c` (10 files), `Font_Mono_Bold_90.c` (15534 lines)                                                             | No action                                                                                                                                                                                                                                                    | 0                                                                                                                                                                                                                                                                                                                                                                                                                               | 0                                                                    | 0                                                                            | —                                                                                                                                                                                   | none          | —      | all 10 `img_*.c` are `const LV_ATTRIBUTE_...`, each referenced once in `ui.cpp:250-2497`; `Font_Mono_Bold_90` used at `ui.cpp:635,1562` for the clock/VCOM screens — genuinely needed, not dead                                                                                                                                                                                                                                                                                                                                                                                                |
| R4-09 | `t_echo_images.h` bitmaps — flash-correct, in use                                                                   | `src/nrf52/t_echo_images.h:11,327,631,947`                                                                                                 | No action                                                                                                                                                                                                                                                    | 0                                                                                                                                                                                                                                                                                                                                                                                                                               | 0                                                                    | 0                                                                            | —                                                                                                                                                                                   | none          | —      | `MeshCom_BitMap`, `T_Echo_OFF`, `Batterie_Vide` are `const ... PROGMEM` (5000B each = 200×200/8, matches board's 200×200 e-ink panel), all three called via `epaper_display.drawExampleBitmap()` in `nrf52_functions.cpp:108-136`. On nRF52 (Cortex-M4, unified address space) `PROGMEM` from `avr/pgmspace.h` compat shim is a no-op; `const` alone places these in flash-mapped `.rodata`, same mechanism as R4-05. `BitmapCallSign` sized as 0B by the byte-count script — needs a follow-up spot check (declaration style differs), not itself a RAM finding                               |
| R4-10 | T-Deck / T-Deck Plus / T5-epaper LVGL heap already PSRAM-backed                                                     | `src/t-deck/lv_conf.h:49-67`, `variants/t5_epaper/lv_conf.h:49-67`                                                                         | No action                                                                                                                                                                                                                                                    | 0                                                                                                                                                                                                                                                                                                                                                                                                                               | 0                                                                    | 0                                                                            | —                                                                                                                                                                                   | none          | —      | both set `LV_MEM_CUSTOM 1` with `LV_MEM_CUSTOM_ALLOC ps_malloc` / `ps_realloc` from `esp32-hal-psram.h`; `t_deck`/`t_deck_plus` both declare `BOARD_HAS_PSRAM=1` (`S1-build-matrix.md`). Confirms these two targets are already correct; only `t_deck_pro` (R4-01) regressed to the internal-DRAM default                                                                                                                                                                                                                                                                                      |

## 2. Design notes (top 5)

**R4-01 (T-Deck Pro PSRAM LVGL heap, top priority).** `variants/t_deck_pro/platformio.ini:33` does
`-include config/lv_conf.h`, the repo's generic/native fallback config with `LV_MEM_CUSTOM 0` and a
48 KB static pool. `src/t-deck/lv_conf.h` already solves this exact problem for `t_deck`/`t_deck_plus`
with `LV_MEM_CUSTOM_ALLOC ps_malloc`. The fix is a one-line `-include` path change to a T-Deck-Pro-specific
lv_conf (or reuse `src/t-deck/lv_conf.h` if its other settings — resolution, color depth, DPI — are
compatible with the AMOLED T-Deck Pro display; they likely are not identical, so cloning the file with the
PSRAM block swapped in is the safer S-effort path). This is a single confirmed 48 KB win — larger than
every other finding in this audit combined — on a board that already has PSRAM wired up but isn't using it
for LVGL. No wire-format contact.

**R4-02 + R4-03 (no-OLED board display overhead, second priority, do together).** Both findings share the
same root cause and the same risk: `loop_functions.cpp` unconditionally builds the OLED object pair and its
history ring for every board except a short exclusion list (`E290`, `WP_DISP`, `E213`, `TRACKER`,
`HELTEC_T114`, `T_ECHO`, `T_DECK*`, `T5_EPAPER`, `T_CONNECT_PRO`) — E22-family bare LoRa devkits fall
through to the "has OLED" path even though they ship no display. Combined saving is 4.3 KB DRAM on those
boards specifically. Given `E22-DevKitC` sits at 1128 B DRAM free and `ttgo_tbeam`'s sibling `E22_XML-DevKitC`
at 920 B free (S4 §2, MEM-04), this is proportionally the highest-value fix in the whole audit for cliff
relief — but it requires a defensible board list (not "any board that happens to compile this file today"),
so effort is S for the code change but risk is M until each candidate board is hardware-confirmed OLED-less.
Do not ship without that confirmation; a board wrongly excluded loses its screen silently.

**R4-05 (close stale item, no code change).** The prior-work digest (S4-prior-work.md §1 item 10) carries
forward a −6.6 kB DRAM claim for "forcing U8g2 fonts into flash" that direct ELF inspection refutes: the
fonts are already flash-resident (`D` symbols at DROM addresses) on every measured ESP32 target. This should
be marked DONE/MOOT in the tracking doc, not re-attempted — a "fix" here would be a no-op change reviewed
for zero benefit.

**R4-04 (dead firasans_12.h, low priority, still worth doing).** Genuinely zero-byte in any current binary
(it's never `#include`d, so never compiled), but it is 110 KB / 8087 lines of confusing dead weight in the
tree that could mislead a future contributor into thinking it's the active 12pt font (paired visually with
the used `firasans_20.h`). Pure hygiene, S effort, zero measurable RAM/flash delta claimed.

## 3. Assets inventory

| File                                                   | Symbol                                                                | const?                           | Referenced by                                                                       | Approx bytes                                                                   | Section                                    |
| ------------------------------------------------------ | --------------------------------------------------------------------- | -------------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ | ------------------------------------------ |
| `src/loop_functions.cpp:369-402` (via U8g2 lib)        | `u8g2_1`, `u8g2_2`                                                    | n/a (class instances)            | `esp32_functions.cpp:202,206`, `nrf52_functions.cpp:39,43`                          | 188 + 188 = 376 B                                                              | `.bss` (DRAM)                              |
| U8g2 lib internal (`u8g2_m_16_8_1`)                    | `buf$<n>` (shared, one instance)                                      | n/a                              | both `u8g2_1`/`u8g2_2` setup calls                                                  | 1024 B                                                                         | `.bss` (DRAM)                              |
| `.pio/libdeps/*/U8g2/src/clib/u8g2_fonts.c`            | `u8g2_font_6x10_mf`                                                   | yes                              | `loop_functions.cpp:977`, `esp32_functions.cpp:354`, `nrf52_functions.cpp:58,64`    | 2393 B (confirmed, `nm -S`)                                                    | `.rodata`→flash (DROM)                     |
| `.pio/libdeps/*/U8g2/src/clib/u8g2_fonts.c`            | `u8g2_font_10x20_mf`                                                  | yes                              | `loop_functions.cpp:3287,4652`, `esp32_functions.cpp:352`, `nrf52_functions.cpp:62` | 4194 B (confirmed, `nm -S`)                                                    | `.rodata`→flash (DROM)                     |
| same                                                   | `u8g2_font_6x10_tf`                                                   | yes (declared)                   | `loop_functions.cpp:1154,3281`, `esp32_functions.cpp:346`                           | not linked in measured elves                                                   | n/a — dead branch, see R4-06               |
| `src/loop_functions.cpp:775-779`                       | `pageLastLine`/`pageLastText`/`pageLastTextLong1`/`pageLastTextLong2` | no (mutable ring)                | ~15 call sites, `loop_functions.cpp:1023-2470`                                      | 504+1050+150+1200 = 2904 B (else-branch, `PAGE_MAX=6, maxdisplines=7`)         | `.bss` (DRAM)                              |
| `config/lv_conf.h:52`                                  | `work_mem_int$3696` (LVGL internal pool)                              | no                               | LVGL core (`lv_mem_init`)                                                           | **49152 B** (confirmed `nm -S` on `t_deck_pro` elf)                            | `.bss` (internal DRAM, S3)                 |
| `src/t-deck/mouse_cursor_icon.c`                       | `mouse_cursor_icon_map[]`                                             | yes                              | `tdeck_main.cpp:79,529,1275-1293`                                                   | not measured (small cursor bitmap)                                             | `.rodata`→flash                            |
| `src/t-deck-pro/src/img_*.c` (11 files)                | `img_*_map[]`                                                         | yes (`LV_ATTRIBUTE_LARGE_CONST`) | `ui_deckpro.cpp` (LVGL screens)                                                     | not individually measured, all flash-forced                                    | `.rodata`→flash                            |
| `src/t-deck-pro/src/Font_Mono_Bold_14..20.c` (7 files) | `glyph_dsc[]`, `cmaps[]`                                              | yes (`static const`)             | LVGL font structs in same files                                                     | not individually measured                                                      | `.rodata`→flash                            |
| `src/t5-epaper/t5-src/img_*.c` (10 files)              | `img_*_map[]`                                                         | yes (`LV_ATTRIBUTE_LARGE_CONST`) | `ui.cpp:250-2497`                                                                   | not individually measured                                                      | `.rodata`→flash                            |
| `src/t5-epaper/t5-src/Font_Mono_Bold_90.c`             | LVGL font struct                                                      | yes                              | `ui.cpp:635,1562`                                                                   | 15534-line source, not measured                                                | `.rodata`→flash                            |
| `src/t5-epaper/firasans_12.h`                          | `FiraSans_12Bitmaps[110473]`                                          | yes                              | **none** (never `#include`d)                                                        | 110473 B declared, 0 B compiled                                                | not linked into any TU                     |
| `src/t5-epaper/firasans_20.h`                          | `FiraSans_20Bitmaps[195206]`                                          | yes                              | `t5epaper_main.cpp:33,400,470`                                                      | 195206 B                                                                       | `.rodata`→flash                            |
| `src/nrf52/t_echo_images.h`                            | `MeshCom_BitMap`, `T_Echo_OFF`, `Batterie_Vide`                       | yes (`PROGMEM`)                  | `nrf52_functions.cpp:108,116,125,136` (drawExampleBitmap)                           | 5000 B each (200×200/8)                                                        | `.rodata`→flash (nRF52 unified addr space) |
| `src/nrf52/t_echo_images.h`                            | `BitmapCallSign`                                                      | yes (`PROGMEM`)                  | `nrf52_functions.cpp:108`                                                           | byte-count script returned 0 — declaration style differs, needs manual recheck | `.rodata`→flash (assumed)                  |

## 4. Rejected / not proposed

- **Doubling the U8g2 buffer estimate (2×1024B).** Verified false — U8g2's `u8g2_m_16_8_1()` uses one
  function-local `static` buffer shared by every same-size `_F_` constructor in the same link unit; only
  one 1024B `.bss` symbol exists per firmware image regardless of how many `_F_` display types are
  instantiated. Do not propose "remove the second buffer" as if 2 KB were at stake — real number is 1.4 KB
  total (object structs + one shared buffer).
- **Re-proposing S4/ram-opti.md item 10 (U8g2 fonts → flash, −6.6 kB).** Refuted by ELF evidence (R4-05).
  Fonts are already flash-resident. Do not reopen.
- **Deep-diving GxEPD2 frame buffers for vision-master-e290/e213/wireless-paper.** `nm -S` on both built
  elves shows no static frame-buffer symbol of the expected W×H/8 size in `.bss` — GxEPD2 allocates its
  buffer dynamically (`malloc`/`new`) at `begin()`, not as a static global, so it doesn't show up in a
  static-symbol RAM audit and any proposed static-to-heap conversion is already the status quo. Left out of
  the findings table as "nothing to fix," not omitted by oversight.
- **T-Deck `Audio audio;` global (S4 item T1, "−10 kB if unused").** Re-checked for `t_deck_pro`
  specifically: `audio.setPinout()`/`audio.loop()`/`audio.connecttoFS()` are called unconditionally in
  `tdeck_pro.cpp:390-546` and `ui_deckpro_port.cpp:451` (voice-call playback feature) — not dead on this
  board, so not re-proposed here. The original T1 finding was scoped to `t-deck`/`t-deck-pro` general
  audio-unused question, which this audit did not re-litigate; left to whoever owns that decision.
- **firasans_12.h flash savings.** Correctly zero — it's already excluded from every build (never
  `#include`d), so `--gc-sections` isn't even relevant; there is no flash byte to reclaim, only the delete
  itself (LOC only, see R4-04).
- **Changing `PAGE_MAX`/`maxdisplines` values for boards that do have a display.** Out of scope — those
  arrays are load-bearing for the page-history/back-cycling UI feature on real OLED/e-ink hardware; R4-03
  proposes gating by board capability, not shrinking the feature for boards that use it.

---

<!-- source: A-D1-platform-pairs.md -->

## A-D1 — ESP32/nRF52 platform-pair merge audit (C06)

Repo `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`, HEAD c51c5881, branch fork-main.
Read-only. Line counts from `diff -w -B` of the two function bodies; `changed` = lines emitted as
`<` or `>`, `identical` = `(A+B-changed)/2`.

## Proposals

| ID    | title                                                             | files:lines                                                                                                                                                                                                                                                                                                                                                                                            | change                                                                                                                                                                                                                                                                                                                  | LOC removed                                                                          | drift bugs closed                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | RAM/flash                                                                                                   | risk                                                             | wire contract                                                                                                                                                              | effort | verification                                                                                                                                                                                          |
| ----- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1-01 | Shared GATE/CONF/BEAT frame handler                               | `src/udp_functions.cpp:205-638` (435 L) vs `src/nrf52/nrf_eth.cpp:352-824` (473 L); identical 189, divergent 530                                                                                                                                                                                                                                                                                       | Extract everything after "a datagram is in `inc_udp_buffer`" into new `src/udp_frame.cpp::handleMeshComUdpFrame(uint8_t*, int len, IPAddress src, const McNetOps&)`; both platforms keep only socket read + their own link-reset hook                                                                                   | ~350                                                                                 | **8** — (a) nRF52 zero-scan `for(i=0;i<packetSize;i+=2)` reads `buf[i+1]` past the datagram on odd sizes, ESP32 has `i+1<packetSize` (`nrf_eth.cpp:400` vs `udp_functions.cpp:214`); (b) RX-01 unconfigured-source guard ESP32-only (`udp_functions.cpp:287`) — an nRF52 gateway radiates XX0XXX GATE frames to LoRa; (c) `hb_warn_logged=false` set in all four ESP32 branches, absent on nRF52 → warn latch never clears; (d) `bGATEWAY_NOPOS` honoured ESP32-only (`udp_functions.cpp:339`); (e) `sendDisplayPosition()` + TM-31 early-dedup 0x21 branch ESP32-only → nRF52 shows no server position frames and can double-insert; (f) `decodeAPRS()` return value checked nRF52-only (`nrf_eth.cpp:468`) — ESP32 processes undecodable frames; (g) `hasExternIPaddress` gate on `sendExtern()` ESP32-only (`udp_functions.cpp:270`); (h) CONF guard has no zero-address check on nRF52 (`nrf_eth.cpp:719`) vs `node_hostip==0` on ESP32 (`udp_functions.cpp:541`) | −0.3 kB flash net (one copy of ~350 L); nRF52 loses a 260 B stack array (`nrf_eth.cpp:357`)                 | M                                                                | none                                                                                                                                                                       | L      | byte-compare of UDP 1799 TX and BLE out-frames before/after on RAK4631 + Heltec V3, driven by `tools/injectraw` EXTUDP recipe; `--udplog`/`[GW];rx;type;` line-for-line equal                         |
| D1-02 | Fold nRF52 hand-built ACK phone frame into `buildAckPhoneFrame()` | `src/nrf52/nrf_eth.cpp:563-581` vs `src/ack_attribution.h:122-137` (already used 6× in shared code)                                                                                                                                                                                                                                                                                                    | Replace the 7 literal `print_buff[n]=` assignments + `addBLEOutBuffer(...,7)` with the shared builder + its returned length                                                                                                                                                                                             | ~14                                                                                  | **1** — nRF52 emits a fixed 7-byte frame with `out[6]=0x00`, so the ACK-attribution callsign suffix (`ACK_PHONE_BASE_LEN + n`) that ESP32 sends since ack_attribution.h is never sent by an nRF52 gateway                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | none                                                                                                        | L                                                                | none (superset frame; length byte already 0 today)                                                                                                                         | S      | byte-compare BLE 0x41 frames on both platforms; assert identical bytes when `call==""`                                                                                                                |
| D1-03 | One `checkSerialCommand()` in `command_functions.cpp`             | `src/esp32/esp32_main.cpp:4307-4434` (128 L) vs `src/nrf52/nrf52_main.cpp:2895-3007` (113 L); identical ~102, divergent 37                                                                                                                                                                                                                                                                             | Move the shared body to `src/serial_command.cpp`; the ESP32-only net-console reader (`esp32_main.cpp:4330-4349`) becomes a `#ifndef DISABLE_NET_CONSOLE` block inside it                                                                                                                                                | ~102                                                                                 | **2** — N-22's `static char msg_buffer[600]` landed nRF52-only (`nrf52_main.cpp:2949`); ESP32 still puts 600 B on the loopTask stack (`esp32_main.cpp:4382`). Net console input works on ESP32 only, so `--` commands over TCP 2323 are silently dead on RAK4631                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | −600 B ESP32 stack peak; +600 B BSS                                                                         | L                                                                | none                                                                                                                                                                       | S      | send `::9999 x` and `--pos` over USB and over 2323 on both platforms, compare emitted LoRa frame bytes                                                                                                |
| D1-04 | Single `s_meshcom_settings` definition                            | `src/esp32/esp32_flash.h:8-242` (235 L) vs `src/nrf52/WisBlock-API.h:178-392` (215 L); identical ~182, divergent 85                                                                                                                                                                                                                                                                                    | Move the struct to one `src/settings_struct.h`; keep genuinely board-scoped members (T-Deck audio/keyboard/backlight/map) behind `#if defined(BOARD_T_DECK…)` inside it                                                                                                                                                 | ~215                                                                                 | **6** — fields present on ESP32 and missing from the nRF52 struct: `node_ntp` (TM-32, already named in C06), `node_immediate_save`, `node_modus`, `node_mute`, `node_persist_to_flash`, `node_disp_rot`. Shared `.cpp` touching any of them cannot compile for nRF52, which is the mechanism behind "fix landed on one side". nRF52 also still carries dead LoRaWAN `auto_join`/`send_repeat_time` (`WisBlock-API.h:186,240`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | +≈40 B nRF52 BSS for the six added fields                                                                   | M                                                                | none — but see design note: `nrf52_ble.cpp:296` does `setFixedLen(sizeof(s_meshcom_settings)+1)`, so field _order and size_ are a BLE contract; append only, never reorder | M      | dump the BLE settings characteristic before/after on RAK4631 and byte-compare; `FLASH_STRUCT_VERSION` must be bumped and the fleet-wipe consequence accepted                                          |
| D1-05 | Settings schema table drives both flash backends                  | `src/config_json.cpp:88-230` already holds a 109-entry `X(name,type,member,min,max,esc)` table, file-local; enumerated again by hand in `src/esp32/esp32_flash.cpp` (112 distinct fields, 322 references = load + save), `src/nrf52/nrf52_flash.cpp` (106 fields, 146 refs), `src/command_functions.cpp` (119 fields, 679 refs), `src/web_functions/web_setup.cpp` (35), `src/phone_commands.cpp` (19) | Promote the existing table to `src/settings_schema.h`, add a `persist` column, generate ESP32 NVS get/put and the nRF52 struct-migration copy from it                                                                                                                                                                   | ~450 (≈220 in `esp32_flash.cpp`, ≈110 in `nrf52_flash.cpp`, ≈120 in `web_setup.cpp`) | **≥1 systemic, count unbounded** — a field added to the struct today needs six hand edits; the divergence in D1-04 is exactly what one missed edit looks like. `settings_sanitize.cpp` already has its own third range table (`settings_sanitize.cpp:50-102`) whose bounds are not the table's min/max                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | −4…8 kB flash on ESP32 (table + loop replaces 224 inlined Preferences calls); +≈2.6 kB rodata for the table | M                                                                | none — NVS key strings and the on-flash struct layout are taken verbatim from the table                                                                                    | L      | full settings round-trip: save, reboot, dump `--info`/config JSON and byte-compare against the pre-change dump on Heltec V3 and RAK4631; byte-compare BLE settings characteristic and UDP 1799 output |
| D1-06 | Delete `s_meshcomcompat_settings`, migrate via schema             | `src/nrf52/WisBlock-API.h:413-609` (197 L) vs `:178-392`; identical ~183 (S6 rank 19: 78 normalized lines) — used only at `src/nrf52/nrf52_flash.cpp:119-121`                                                                                                                                                                                                                                          | Replace the whole legacy struct + its ~106-line field-by-field copy with a schema-driven, version-tagged migration                                                                                                                                                                                                      | ~300                                                                                 | **1** — the compat struct is a frozen snapshot that has silently drifted from the live one; a field added to the live struct is copied from uninitialised memory on migration                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | −≈700 B nRF52 stack (`old_struct` is a stack local at `nrf52_flash.cpp:119`)                                | M                                                                | none (flash layout only)                                                                                                                                                   | M      | flash an old-format image, upgrade, byte-compare `--info` output and the BLE settings characteristic against pre-change upgrade                                                                       |
| D1-07 | One `at_cmd.h`                                                    | `src/esp32/at_cmd.h:1-52` vs `src/nrf52/at_cmd.h:1-52`; identical 51, divergent 1 (line 11, a commented-out `#include "WisBlock-API.h"`)                                                                                                                                                                                                                                                               | Delete both, add `src/at_cmd.h`; the includer supplies the platform header                                                                                                                                                                                                                                              | 52                                                                                   | 0                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | none                                                                                                        | L                                                                | none                                                                                                                                                                       | S      | build both platforms; `nm` symbol-set equality; byte-compare UDP/BLE output unchanged                                                                                                                 |
| D1-08 | Share the battery-detect state machine                            | `src/batt_function_old.cpp:70-120` vs `src/batt_functions.cpp:71-120`; identical 48 of 51, divergent 3 (comment wording only)                                                                                                                                                                                                                                                                          | Move `battDetectState`/`battDetectUpdate`/`battHardwarePresent` into `src/batt_detect.h`, included by both TUs. **Neither file is deleted** (12 vs 15 live board variants)                                                                                                                                              | ~50                                                                                  | 0 today, but this is the pair C06 names as the one where a fix landed in the TU the reporting board does not link                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | none                                                                                                        | L                                                                | none                                                                                                                                                                       | S      | `--batt` output byte-compare on a `USE_NEW_BATT` board (Heltec V3) and a legacy board                                                                                                                 |
| D1-09 | Common gateway service block                                      | `src/esp32/esp32_main.cpp:3847-3914` (68 L) vs `src/nrf52/nrf52_main.cpp:2060-2153` (94 L)                                                                                                                                                                                                                                                                                                             | One `gatewayService(const McNetOps&)` in `src/gateway_loop.cpp` carrying the two-stage heartbeat watchdog; platform supplies rx/tx/reset/link-status                                                                                                                                                                    | ~60                                                                                  | **3** — (a) nRF52 calls `sendUDP()` only when `getUDP()==1`, i.e. only when _no_ packet arrived (`nrf52_main.cpp:2070-2073`), so on a busy gateway the TX ring is starved every pass that receives; ESP32 always does both (`esp32_main.cpp:3849`); (b) nRF52 has no HB_WARN_TIME stage-1 and no `hb_warn_logged`, only the MAX_HB_RX_TIME reset; (c) that reset is nested inside `if(!neth.hasIPaddress)` (`nrf52_main.cpp:2104`), so an nRF52 gateway with a live link and a silent server never recovers — ESP32's stage 2 does `resetMeshComUDP()`                                                                                                                                                                                                                                                                                                                                                                                                                | negligible                                                                                                  | **H** — touches the live gateway recovery path on both platforms | none                                                                                                                                                                       | M      | 24 h soak on RAK-90 and Heltec-93 with `tools/meshlogger.py`; byte-compare `[GW];rx;type;` and `[UDP];tx;` lines and the UDP 1799 payloads against a pre-change capture                               |
| D1-10 | Common loop scheduler with platform hooks                         | `esp32loop` `src/esp32/esp32_main.cpp:1921-4082` (2162 L) vs `nrf52loop` `src/nrf52/nrf52_main.cpp:1152-2588` (1437 L)                                                                                                                                                                                                                                                                                 | Extract only the ~18 shared _timer predicates_ (boot-ready marker, `bpPollDrain`, `INSTR_LOOPTICK`, deep-sleep, clock/NTP, `bMyClock`, retransmit, posinfo, heyinfo, telemetry, `mcp_refresh_timer`, `ncnt_hold`, phone-ready) into `src/mainloop_sched.cpp` returning a due-flag set; both loops keep their own bodies | ~120                                                                                 | **2** — nRF52 telemetry uses `bHeyFirst` where ESP32 uses `bTeleFirst` (`nrf52_main.cpp:2018` vs `esp32_main.cpp:3462`): a copy-paste of the HEY predicate, so the first telemetry burst fires on the HEY flag; nRF52 heyinfo omits `extra_hey_time` (`nrf52_main.cpp:1974` vs `esp32_main.cpp:3409`), so nRF52 has no HEY spreading                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | negligible                                                                                                  | M                                                                | none                                                                                                                                                                       | M      | 6 h bench capture on RAK-90 + Heltec-93; compare posinfo/hey/telemetry emission timestamps and the emitted frame bytes against pre-change                                                             |

## Design — top 5 (D1-01, D1-05, D1-04, D1-03, D1-09)

The platform layer is one header plus one struct of function pointers, not a class hierarchy:
`src/mc_net.h` declares `struct McNetOps { bool (*sendDatagram)(const uint8_t*, uint16_t); void
(*resetLink)(void); bool (*linkUp)(void); IPAddress (*serverAddr)(void); };` with
`extern const McNetOps mcNet;` defined once per platform (`src/esp32/esp32_net_ops.cpp` wrapping
`WiFiUDP`/`resetMeshComUDP`/`WiFi.status`/`node_hostip`, `src/nrf52/nrf52_net_ops.cpp` wrapping
`NrfETH`/`resetDHCP`/`Ethernet.linkStatus`/`udp_dest_addr`). It is a plain struct so the linker
keeps one instance and the calls stay direct — no vtable, which matters for the classic-ESP32 DRAM
cliff. `src/udp_frame.h` then declares only `void handleMeshComUdpFrame(uint8_t *buf, int len,
IPAddress src)`, implemented in `src/udp_frame.cpp` and reading `mcNet` for the two things it
actually needs (link reset on the zero-flood path, server address for the CONF guard); the ESP32
`getMeshComUDP()` and `NrfETH::getUDP()` shrink to socket read + call. `src/gateway_loop.h`
declares `void gatewayService(void)` on the same `mcNet`. `src/settings_schema.h` holds the X-macro
table lifted out of `config_json.cpp:88-230` with a `persist` column added; `esp32_flash.cpp`,
`nrf52_flash.cpp`, `web_setup.cpp` and `config_json.cpp` all expand it, and `src/settings_struct.h`
holds the one struct definition — appended to only, because `nrf52_ble.cpp:296` fixes the BLE
characteristic length at `sizeof(s_meshcom_settings)+1`. `src/serial_command.h` declares
`checkSerialCommand()`; `src/at_cmd.h` and `src/batt_detect.h` are plain moves.
Sequencing: D1-07/D1-08/D1-02 first (no behaviour), then D1-03, then D1-04+D1-05+D1-06 as one
FLASH_STRUCT_VERSION bump, then D1-01, and D1-09/D1-10 last behind a soak.

## Rejected

- **`startNetwork()` (`src/udp_functions.cpp:1099-1376`) ↔ `NrfETH::startETH()`
  (`src/nrf52/nrf_eth.cpp:967-1067`).** Named in the C06 detector list, but there is no shared body
  to merge: ESP32 is an asynchronous, event-driven WiFi state machine (scan policy, SAE/PMF policy,
  `WifiStall` instrumentation, watchdog stages); nRF52 is a blocking W5100S link check plus
  `Ethernet.begin(mac, 10000)`. The only overlap is the ~15-line tail that writes
  `node_ip`/`node_gw`/`node_dns`/`node_subnet`, emits `got_ip` and calls `commandAction("--wifiset")`
  (`nrf_eth.cpp:1030-1050`, `udp_functions.cpp` equivalent). Extract that as
  `netApplyLocalIp(IPAddress ip, gw, dns, mask)` and stop there.
- **Merging `esp32setup()` (`src/esp32/esp32_main.cpp:637-1885`) with `nrf52setup()`
  (`src/nrf52/nrf52_main.cpp:426-1150`) wholesale.** The two share an _ordering contract_
  (`init_loop_function` → `initMheard` → callsign default + `save_settings` → `init_onebutton` →
  `lora_setcountry` → radio init) but almost no statements: sensor, display and GPS bring-up are
  per-chip. Only the settings/callsign/mheard phase (~40 L) is worth a shared
  `mcSetupCommonConfig()`; the rest would become an `#if` thicket, which is the C06 mechanism, not
  a fix for it.
- **Merging the BLE callbacks (`src/esp32/esp32_main.cpp:318-377` ↔ `src/nrf52/nrf52_ble.cpp`).**
  Already converged: both callbacks do nothing but fill a `BleQueueItem` and `xQueueSend` to the
  same queue (`esp32_main.cpp:369-376`, `nrf52_ble.cpp:264-286`), and the frame building around them
  is already shared (`ack_attribution.h`, `phone_commands.cpp`). What remains is NimBLE vs Bluefruit
  API surface. The only real duplicate left is covered by D1-02.
- **Deleting either `batt_functions.cpp` or `batt_function_old.cpp`.** Both are live across disjoint
  board sets (S4 §3, DRY-20). D1-08 shares, does not delete.
- **Merging the `#if` ladders in `configuration_global.h:198-240`.** Out of this topic and already
  mitigated (S4 §3, ALT-33).
- **Reordering `s_meshcom_settings` to pack it.** `nrf52_ble.cpp:296` makes the struct's size a BLE
  contract and `nrf52_flash.cpp` writes it raw; any reorder is a fleet-wide settings wipe for no
  DRY gain.

---

<!-- source: A-D2-commands.md -->

## A-D2 — Command dispatch (`commandAction`) audit

Scope: `src/command_functions.cpp` (6331 L), `src/phone_commands.cpp` (670), `src/config_json.cpp`
(776), `src/setlog_lines.cpp` (145), `src/settings_sanitize.cpp` (136). Read-only audit.

## 0. Shape

- `commandAction(char*, bool)` — `src/command_functions.cpp:238-6172`, one function, 5935 lines.
  Local frame: `char msg_text[300]` + `char _owner_c[300]` + an Arduino `String sVar` (:242-249) —
  ~600 B stack plus heap for `sVar`, live for the whole dispatch.
- **297 distinct `if(commandCheck(...))` branch heads** (322 `commandCheck` call sites total; the
  extra 25 are second alternatives inside one head, e.g. `:3467` `"weather" || "wx"`, and
  re-checks inside a body, e.g. `:2076`, `:4756`, `:5164`).
- 76 of the 297 sit inside `#if INSTRUMENT_ENABLED` (`:4786`-`:5361`); ~221 are in a normal build,
  and board `#ifdef`s cut that further per target.
- Nine outer dispatch flags (`bInfo bPos bShowPos bWeather bTelemetry bIO bSensSetting
bWifiSetting bNodeSetting bAnalogSetting bReturn`, :253-265) are set by branches and consumed by
  the printer/BLE-register tail at `:5497-6174`.

## 1. `commandCheck()` — `src/command_functions.cpp:157-168`

```c
int commandCheck(char *msg, char *command)   // copies msg into a 100-byte stack buffer,
{ ... vmsg[strlen(command)] = 0x00; return casecmp(vmsg,command)==0 ? 0 : -1; }
```

It is a **case-insensitive prefix match** (`casecmp`, :127-141), not an equality test. Consequences:

- **Evaluation order is load-bearing.** The whole chain is one `else if` ladder; the first prefix
  that matches wins. A table-driven dispatcher changes evaluation order and must therefore either
  (i) preserve source order in the table and scan linearly, or (ii) sort longest-name-first.
  Longest-first is equivalent to the current ladder **only** where the shorter name is a strict
  prefix of the longer one; it is _not_ equivalent where two names merely share a prefix of
  different lengths, so option (i) is the safe migration and option (ii) needs a proof per pair.
- Order-dependent pairs found (short name would swallow the long one if reordered):
  `heap ` :4794 / `heap` :4800 (already commented at :4791-4793) · `setlog on|off` :533,547 /
  `setlog ` :561 · `gpsdebug 2|on|0|off` :3051,3066 / `gpsdebug ` :3081 · `netconsole on|off`
  :2446,2458 / `netconsole` :2469 · `passwd ` :3361 / `passwd` :3389 · `maxhop ` :4454 / `maxhop`
  :4485 · `pingmax max` :3738 / `pingmax ` :3747 · `tab list` :4988 / `tab ` :4994 ·
  `instreset` :5243 / `instr` :5354 · `loradebug on|off` :2784,2839 / `lora` :5364 ·
  `sethamnet on`/`setinet off` :4079 / `setinet` :4099 · `posshot` :427, `postime ` :434 /
  `pos` :3427 · `mheard` / `mh` :5381 (same head).
- `vmsg[strlen(command)] = 0`: if `strlen(command) > strlen(msg)` this writes past the terminator
  but inside the 100-byte buffer — benign, but it means a _short_ input can never match a long
  command, which is what keeps `balledges` :5033 safe next to `balledge on` :5021.
- Hard cap: a command line longer than 99 chars is truncated inside `commandCheck` for matching
  purposes only.

## 2. Families

| Family                                     | Count | Line ranges (first 5 of N)                                                                  | Typical body                                                        |
| ------------------------------------------ | ----: | ------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| (a) `--set<X> <value>` store+save+echo     |    71 | 321 `rotate `, 434 `postime `, 561 `setlog `, 1128 `contrast `, 1225 `button gpio ` (+66)   | 25 L median, 1845 L total                                           |
| (b) `--<flag> on\|off` boolean toggles     |   120 | 457 `volt on`, 473 `proz on`, 489 `setinfo off`, 498 `setinfo on`, 507 `setcont off` (+115) | 16 L median, ~1900 L total                                          |
| (c) `--info` / `--show` printers           |    30 | 955 `info`, 2469 `netconsole`, 3389 `passwd`, 3427 `pos`, 3467 `weather`/`wx` (+25)         | 7 L in the ladder; the real bodies are the 700-line tail :5497-6174 |
| (d) actions (reboot, sendpos, bench hooks) |    43 | 427 `posshot`, 667 `dfu`, 683 `reboot`, 710 `spectrum`, 730 `ota-update` (+38)              | 10 L median, 722 L total                                            |
| (e) special parsers                        |    33 | 301 `utcoff`, 355 `settime`, 632 `cleanflash`, 4231 `setout `, 4298 `setio `, 5415 `setgrc` | 16 L median, 566 L total                                            |

(b) includes 4 toggles that are not spelled `on|off`: `gps autosymbol`/`gps fixsymbol` :1719,1735
and `gateway pos`/`gateway nopos` :2380,2396. **60 `... on` heads and 62 `... off` heads** form
**58 matched pairs** plus 6 combined-head toggles that test both spellings inside one branch
(:4752, :4776, :4868, :5160, :5170, :5185) and 2 cross-named pairs (:4079 `sethamnet on`/
`setinet off`, :4099 `setinet`/`sethamnet off`).

Bit-flag mechanics inside (b) are already uniform: 184 `save_settings()` calls, and every toggle is
one of `node_sset | node_sset2 | node_sset3 | node_sset4` `|= MASK` / `&= ~MASK` — but three
spellings of the clear coexist (`&= ~0x0020` :1657, `= x & 0x7FDF` :1704, `= x & 0x7FFB` :987) and
the `& 0x7Fxx` form additionally clears bit 15 as a side effect. That is a latent behavioural
difference, not just style.

## 3. Numeric parsing — inconsistent

`sscanf` is the dominant reader (68 sites, both directly on `msg_text+N` and on a `_owner_c` copy),
with `%d %i %f %lf` all in use. Exceptions: `atoi` once (`contrast` :1130), `String::toInt()` twice
(`settime` :359-366, `setgrc` :5432), `IPAddress::fromString` once (`srvip` :5259), raw indexing
(`symid` :3523 `msg_text[8]`, `symcd` :3553). `strtol` is never used, so **no branch detects a
non-numeric argument**: `--txpower abc` leaves `iVar` uninitialised-or-stale and then range-checks
it. Every offset (`msg_text+7`, `+9`, `+11`, `+14`, `+16`…) is a hand-counted literal that must
match `strlen("--" + name)`; `pingcall` :3690 already carries a copy/paste defect
(`sizeof(meshcom_settings.node_call)` used to bound a write into `node_pingcall`).

## 4. Echo / print styles — 6 distinct, so the table needs a style column

| Style                     | Sites | Form                                                                                                                                                                       |
| ------------------------- | ----: | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| S1 raw echo               |    15 | `addBLECommandBack((char*)msg_text)` — echoes exactly what the user typed                                                                                                  |
| S2 literal echo           |    38 | `addBLECommandBack((char*)"--<canonical>")` — 26 distinct literals (`"--posted"` ×4, `"--reboot now"` ×2, `"--gpsdebug on"` ×2, rest ×1)                                   |
| S3 deferred JSON register |    79 | sets `bNodeSetting`/`bSensSetting`/`bWifiSetting`/`bAnalogSetting`/`bInfo`, the tail :5497-6174 emits the frozen BLE JSON (TYP `TM W IO I SE S1 SW S2 G SN AN SA CONFFIN`) |
| S4 serial confirmation    |  ~254 | `printfdeb(...)` ×185 / `printlndeb(...)` ×69 — free-form, ~140 distinct format strings                                                                                    |
| S5 bench marker           |    37 | raw `Serial.printf`/`Serial.println` with `[TAG];k;v` (semicolons survive only outside `printfdeb`'s CSV stripping, :4479)                                                 |
| S6 silent                 |   ~90 | no echo at all                                                                                                                                                             |

Byte-identity therefore **cannot** be recovered from a single format string. The table must carry
(style, echo-payload) per entry, where the payload is either `ECHO_RAW`, a literal pointer, a
register id, or a `const char *fmt` + argument recipe. S4's ~140 distinct formats are the reason a
"one printf in the dispatcher" design fails: it would change wording on ~250 lines.

## 5. Interaction with the other command surfaces

- **BLE**: `phone_commands.cpp:248` `readPhoneCommand()` is a **separate binary opcode path**
  (`0x10 0x20 0x50 0x55 0x70 0x80 0x90 0x95 0xA0 0xF0`, :307-668) that writes `meshcom_settings`
  directly and **never calls `commandAction`**. Text commands reach `commandAction` through
  `esp32_main.cpp:3081` / `nrf52_main.cpp:1667` with `iphone=1` → `ble=true`. So **every one of the
  297 branches is BLE-reachable**; the `ble` flag only selects the response channel. `0x50`
  (callsign) and `0x55` (SSID/PWD) duplicate `--setcall` :3605 and `--setssid`/`--setpwd`
  :3763,3792 with _different_ validation: the binary path skips `checkRegexCall()`.
- **Web GUI**: `web_setup.cpp` funnels ~60 form fields back through `commandAction(message_text,…)`
  — that is the good pattern and must not regress.
- **`config_json.cpp:89-197`** already holds `CFG_FIELD_LIST`, a **109-entry X-macro table**
  (`name, CfgType, member, min, max, escape`) used by both export and import. It is a third
  enumeration of the same settings, and a fourth exists in `settings_sanitize.cpp` (range clamps
  for `sf cr bw power freq country`, :7-136) with its own `POWER_NOT_SET = -20` sentinel duplicated
  at `config_json.cpp:48`.

**Target picture** — one schema table shared by D1's flash backends, `config_json`, and this
dispatcher. Columns: `key` (JSON/flash name) · `cmd` (serial name, NULL if not settable) ·
`CfgType` · `member offset` · `min,max` · `escape sentinel` · `apply flags` (SAVE, REBOOT_5S,
REBOOT_15S, RECONFIG_LORA, RECONFIG_NET) · `ble_register` (S3 id) · `echo_style` + `echo_fmt`.
`config_json.cpp`'s existing X-macro is the natural host: it already covers 109 of the ~130
settable fields.

## 6. Dead / duplicate / mis-guarded

| #   | Finding                                                                                                                                                                                                                                                                          | Site            |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- |
| 1   | `--setowndns ` handled **twice**; the second copy is unreachable (identical body, different offset `+11` vs `+12` — so the live one is :3959 with `+12`)                                                                                                                         | :3959 and :4027 |
| 2   | `--softser app0` unreachable: `"softser app"` :3245 is a prefix and returns first                                                                                                                                                                                                | :3254           |
| 3   | `else` missing between `setowngw` and `setownms`, and between `setl76k` and the softser block — chain silently splits into fresh `if`s (currently harmless, every predecessor `return`s)                                                                                         | :4001, :3116    |
| 4   | Commented-out branches still in the file: `compress ` :288-298, `softser test0`/`softser test` :3267-3288, `softser xml` :3351-3358                                                                                                                                              | 4 blocks, ~35 L |
| 5   | `--spectrum` :710 body has two dead `#ifdef` blocks whose contents are commented out                                                                                                                                                                                             | :710-728        |
| 6   | Bench-only commands **outside** `INSTRUMENT_ENABLED`: `--specstart/--specend/--specstep/--specsamples` :4654-4741 (and `--specstep` writes `node_specsamples` while `--specsamples` writes `node_specstep` — swapped)                                                            | :4696, :4717    |
| 7   | `--ethstat/--wifistat/--udpstat/--udplog` are deliberately kept **outside** the guard (comment :4728-4732) — that is the **INS-01 lesson** (S4): a previous guard change swept these field diagnostics out of shipping builds. Any re-guarding must string-scan the built image. | :4745-4785      |
| 8   | `--gps reset` :1752 returns with no BLE acknowledgement (documented as accepted)                                                                                                                                                                                                 | :1752-1774      |

## 7. Proposals

| ID    | Title                                                                            | files:lines                                                                           | Change                                                                                                                            |                    LOC removed | Flash delta (derivation)                                                                                                                                                                                                                                                                                                                             | RAM delta                                                                                                                                                                                                | Risk                                                                                  | Wire contract                                                                                                                         | Effort | Verification                                                                                                                                                             |
| ----- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | -----------------------------: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| D2-01 | Delete the unreachable `--setowndns` duplicate                                   | command_functions.cpp:4027-4051                                                       | Remove the second `setowndns` block; keep :3959                                                                                   |                             25 | −120 B (1 `commandCheck` call ≈ 24 B + 11 B literal + ~85 B body)                                                                                                                                                                                                                                                                                    | 0                                                                                                                                                                                                        | L                                                                                     | none (branch is unreachable today)                                                                                                    | S      | full-command capture diff (§8)                                                                                                                                           |
| D2-02 | Fix `--softser app0` shadowing                                                   | command_functions.cpp:3245-3266                                                       | Move the `app0` head above `app`                                                                                                  |                              0 | 0                                                                                                                                                                                                                                                                                                                                                    | 0                                                                                                                                                                                                        | L                                                                                     | **changes behaviour**: `--softser app0` currently silently does `app`. Treat as a bug fix, not a refactor; needs an operator decision | S      | §8 capture + `--info` before/after                                                                                                                                       |
| D2-03 | Fix the swapped `--specstep`/`--specsamples` targets                             | command_functions.cpp:4696-4741                                                       | Swap the two `meshcom_settings` members                                                                                           |                              0 | 0                                                                                                                                                                                                                                                                                                                                                    | 0                                                                                                                                                                                                        | L                                                                                     | none (print strings unchanged); stored value changes                                                                                  | S      | §8 capture + read back via `config_json` export                                                                                                                          |
| D2-04 | Delete commented-out branches                                                    | :288-298, :3267-3288, :3351-3358, :710-728                                            | Remove dead text                                                                                                                  |                            ~55 | 0 (already not compiled)                                                                                                                                                                                                                                                                                                                             | 0                                                                                                                                                                                                        | L                                                                                     | none                                                                                                                                  | S      | build all 32 envs, byte-compare `.bin`                                                                                                                                   |
| D2-05 | Normalise bit-clear spelling to `&= ~MASK`                                       | ~40 sites across :507-5195                                                            | Replace `= x & 0x7Fxx` with `&= ~MASK`                                                                                            |                              0 | −0.2 kB (drops one 16-bit load per site)                                                                                                                                                                                                                                                                                                             | 0                                                                                                                                                                                                        | **M** — the `& 0x7Fxx` form also clears bit 15; auditing each mask is the work        | none                                                                                                                                  | M      | native unit test over `node_sset*` transitions + §8                                                                                                                      |
| D2-06 | Table-drive family (b), the 120 boolean toggles                                  | :457-5195 → new `src/command_table.cpp`                                               | One `const CmdEntry` row per toggle: name, target bool*, settings word + mask, echo style/payload, optional post-hook fn          |                          ~1500 | **−6.0 kB** on a full-featured ESP32 build (≈90 in-build toggles × ~100 B of per-branch code — `commandCheck` call 24 B, bool store 10 B, RMW on the settings word 22 B, `if(ble)` + echo 20 B, `save_settings()` 4 B, epilogue 20 B — less 90 × 24 B of table rows, less ~1.4 kB of new dispatcher/handler code); **−3.5 kB** on a lean nRF52 build | **0** (table is `const` → `.rodata`/flash; keep it out of `.data` by declaring the row array `static const`, and on ESP32 give string literals no `PROGMEM` treatment — they are already flash-resident) | **M**                                                                                 | must be **none** — S1/S2/S3/S4/S5 payloads move verbatim into the row                                                                 | L      | §8, plus `--info`, `--seset`, `--wifiset`, `--nodeset`, `--analogset`, `--wx`, `--pos`, `--io`, `--tel`, `--aprsset` JSON captured over BLE before/after and byte-diffed |
| D2-07 | Table-drive family (a), the 71 `--set<X> <value>` branches                       | :321-5606 → same table                                                                | Row carries type, member offset, min/max, `save` flag, reboot flag, echo fmt; the ladder body shrinks to a parse+range+store call |                          ~1100 | **−2.6 kB** (≈60 in-build entries × ~110 B saved on parse/range/store boilerplate, less 60 × 28 B rows; the per-branch `printfdeb` format strings stay, so no literal savings)                                                                                                                                                                       | 0                                                                                                                                                                                                        | **M/H** — each branch's exact `sscanf` offset and clamp semantics must be transcribed | must be **none**                                                                                                                      | L      | §8 + a native golden test on the parse/clamp helper alone (env:native)                                                                                                   |
| D2-08 | Fold the range/clamp column into `config_json.cpp`'s `CFG_FIELD_LIST`            | config_json.cpp:89-197, settings_sanitize.cpp:7-136, command_functions.cpp (a)-family | One schema table with the columns listed in §5; `config_json`, `settings_sanitize` and the D2-07 rows all read it                 | ~200 (the sanitize duplicates) | −0.8 kB (one copy of 109 min/max pairs instead of three)                                                                                                                                                                                                                                                                                             | 0                                                                                                                                                                                                        | **H** — couples three subsystems; do it only after D2-07 lands                        | none                                                                                                                                  | L      | `test/test_config_json` + `test/test_settings_sanitize` (native, already exist) + §8                                                                                     |
| D2-09 | Move `--specstart/end/step/samples` behind `INSTRUMENT_ENABLED`                  | :4654-4741                                                                            | Add the guard                                                                                                                     |       0 (guarded, not deleted) | −0.6 kB in shipping builds                                                                                                                                                                                                                                                                                                                           | 0                                                                                                                                                                                                        | **M** — INS-01: guard changes have swept field diagnostics before                     | none in an instrument build; the four commands **disappear** from a shipping build → operator decision required                       | S      | build both variants and `strings firmware.bin \| grep -c specstart` on each; §8 on an INSTRUMENT build                                                                   |
| D2-10 | Give `commandCheck` an exact-match variant and mark the 13 order-dependent pairs | :157-168 + the 13 sites in §1                                                         | Add `flags & CMD_PREFIX`; exact-match everything else, so table order stops being load-bearing                                    |                              0 | +0.1 kB                                                                                                                                                                                                                                                                                                                                              | 0                                                                                                                                                                                                        | **M**                                                                                 | none, if the 13 pairs keep `CMD_PREFIX`                                                                                               | M      | §8 with a deliberately adversarial input set (`--info`, `--infoX`, `--lora`, `--loradebug on`, `--heap`, `--heap tag`, `--pos`, `--posshot`)                             |

Prerequisite for D2-06..D2-10: **D2-V** below must land first — there is no golden capture today.

## 8. Verification method (mandatory for every proposal above)

1. Build the target env at the pre-change commit; flash a bench node (RAK4631 `/dev/cu.usbmodem2101`
   or Heltec-93, per MEMORY "Bench fleet ports").
2. Drive **every** command name in the table through `tools/bench/serial_session.py` with a canned
   argument, capturing raw bytes off USB **and** off the net console (TCP 2323, `tools/hmac_connect.py`)
   — the two channels take different code paths (`printfdeb` vs raw `Serial.printf`, §4 S5).
   Include for each command: the canonical form, an out-of-range value, a non-numeric value, and the
   adversarial prefix set from D2-10. Exclude the destructive ones (`--reboot`, `--cleanflash`,
   `--dfu`, `--deepsleep`, `--ota-update`, `--spiffs reset`) into a separate, manually-run list.
3. Capture the BLE side in the same run: the 13 JSON registers via `--info --seset --wifiset
--nodeset --analogset --wx --pos --io --tel --aprsset --conffin`.
4. Repeat at the post-change commit on the same hardware, same order, and `diff` the two captures.
   **A non-empty diff outside a deliberately-changed line is a failure.**
5. Add a `test/test_command_table` native suite (env:native, `platformio.ini:188`) that links only
   the new table + parse/clamp helper (Arduino-free, the way `setlog_lines.cpp` and
   `settings_sanitize.cpp` already are) and asserts (i) no two rows shadow each other under
   `commandCheck` semantics, (ii) every row's offset matches `strlen("--"+name)`, (iii) the echo
   payload for each row matches a golden file generated from the pre-change tree.
6. `pio run` all 32 envs; compare `.pio/build/*/firmware.elf` section sizes against a **same-base**
   build (MEMORY: `resource_baseline.json` drifts — never compare across bases).

## 9. Design sketch

```c
enum CmdKind : uint8_t { CK_BOOL, CK_INT, CK_FLT, CK_STR, CK_ACTION };
enum CmdStyle: uint8_t { CS_SILENT, CS_RAW, CS_LIT, CS_REG, CS_FMT, CS_MARK }; // §4 S6,S1,S2,S3,S4,S5
enum CmdFlag : uint8_t { CF_SAVE=1, CF_PREFIX=2, CF_REBOOT5=4, CF_REBOOT15=8, CF_LORA=16, CF_NET=32 };

struct CmdEntry {
    const char *name;       // without "--", exactly as commandCheck() sees it
    uint8_t     kind, style, flags;
    void       *target;     // bool*, int*, float*, char[] inside meshcom_settings
    uint16_t   *word;       // node_sset..node_sset4, NULL if the flag is RAM-only
    uint16_t    mask;       // bit to set (CK_BOOL) or 0
    int16_t     lo, hi;     // inclusive range for CK_INT; ignored otherwise
    const char *echo;       // CS_LIT literal, or CS_FMT/CS_MARK format string
    void      (*fn)(const char *arg, bool ble);   // NULL unless the branch has a post-hook
};
static const CmdEntry kCmds[] PROGMEM_IF_NRF52 = { /* source order preserved */ };

void commandAction(char *umsg, bool ble) {
    /* ... unchanged preamble :240-286 (the "--" check and the "wrong command" echo) ... */
    for (const CmdEntry *e = kCmds; e != kCmds + ARRAY_LEN(kCmds); ++e) {
        if (commandCheck(msg_text + 2, (char *)e->name) != 0) continue;
        const char *arg = msg_text + 2 + strlen(e->name);
        if (!cmdApply(e, arg, ble)) return;      // parse + range + store + word/mask
        if (e->flags & CF_SAVE) save_settings();
        cmdEcho(e, msg_text, ble);               // dispatches on e->style, byte-identical output
        if (e->fn) e->fn(arg, ble);              // init_onebutton(), setupINA226(), lora_setchip...
        cmdPost(e->flags);                       // reboot timers, lora/net reconfig
        return;
    }
    /* fall through to the legacy ladder for families (c)(d)(e), then the tail :5497-6174 */
}
```

Three migrated entries, one per family:

```c
/* (b) — was :2586-2601 "--mesh on" */
{ "mesh on",  CK_BOOL, CS_REG, CF_SAVE, &bMESH, &meshcom_settings.node_sset2, 0x0020,
  0,0, /*reg*/ (const char*)BLE_REG_NODE, NULL },      // note: node_sset2 CLEARS on "on" -> CF_INVERT row variant
/* (a) — was :4494-4521 "--txpower " */
{ "txpower ", CK_INT,  CS_FMT, CF_SAVE|CF_LORA, &meshcom_settings.node_power, NULL, 0,
  TX_POWER_MIN, TX_POWER_MAX, "set txpower to %i dBm\n", NULL },
/* (c) — was :5364-5379 "--lora" */
{ "lora",     CK_ACTION, CS_SILENT, 0, NULL, NULL, 0, 0, 0, NULL, &cmdShowLora },
```

`--mesh on` is deliberately shown as the awkward case: it _clears_ its mask where every other
toggle sets it, so the row needs a `CF_INVERT` flag. Roughly 12 of the 120 toggles are inverted
(`mesh`, `gateway pos`, `proz on`, `display on`, …) — enumerate them before writing the table, not
after.

## 10. Rejected

- **Split `commandAction` into per-topic functions without a table.** Moves 5935 lines around,
  saves no flash, and multiplies the diff that §8 has to verify. The clone count is the symptom;
  the ladder is the disease.
- **`std::map` / `unordered_map` / any hash dispatch.** Puts the key table in RAM and pulls in
  `<map>`; RAM is the binding constraint on the classic ESP32 targets (MEMORY: "Classic ESP32
  IRAM/DRAM levers").
- **Generating the echo strings from one format string.** §4: ~140 distinct `printfdeb` formats
  across 254 sites. Any unification changes bytes on the wire. Explicitly out of bounds.
- **Merging the `phone_commands.cpp` binary opcodes into the text dispatcher.** The BLE frame
  format is frozen (S5-wire-contract) and the opcode path predates `commandAction`; unifying it
  is a protocol change, not a refactor.
- **Deleting `--specstart/end/step/samples` outright** (as opposed to D2-09's guard). INS-01: a
  previous guard sweep removed working field diagnostics. Guard, do not delete, and string-scan
  the image afterwards.
- **Removing the `bReturn`/`bNodeSetting`-style outer flags in the same pass.** They are the input
  to the 700-line printer tail (:5497-6174); untangling them is a separate audit item and would
  make the §8 diff unreadable if bundled with the table migration.

---

<!-- source: A-D3-core-clones.md -->

## A-D3: Intra-file / intra-core duplicate blocks

Scope: core `src/*.cpp` (root), `src/esp32`, `src/nrf52` — excludes
`command_functions.cpp`, esp32-vs-nrf52 mirror pairs, and display/UI dirs
(owned by other analysts). Audit only, no edits made.

## Proposals

| ID    | title                                                                                                                                  | files:lines                                                                                                                                                                                           | change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | LOC removed                                                                                | flash delta (rough)                                                                              | RAM delta                                                                                      | risk                                                                                      | wire contract touched                                                                | effort | verification                                                                                                                                                                                                                   |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| D3-01 | Field-extractor loop for `/X=value/` APRS-comment tags                                                                                 | src/aprs_functions.cpp:667-1041 (`decodeAPRSPOS`, ~11 blocks: bat /B=, alt /A=, press /P=, hum /H=, temp /T=, temp2 /O=, qfe /F=, qnh /Q=, gasres /G=, ncnt /N#, co2 /C=, version /V=, telemetry /Y=) | Replace each ~15-line "find tag, copy digits into `decode_text`, sscanf into field" block with one helper called 11x with (tag-char2, tag-char3-or-digit-range, fmt, dest-ptr)                                                                                                                                                                                                                                                                                                                                                                                                                                                    | ~140 (11 blocks x ~15 lines -> 11 one-line calls + 1 ~20-line helper)                      | ~1.1-1.7 kB (rough, 140 lines x 8-12 B/line)                                                     | none (no new statics; helper uses same locals)                                                 | L                                                                                         | none                                                                                 | S      | bit-for-bit compare of `struct aprsPosition` produced from a corpus of real `/B=/A=/P=.../V=.../Y=` payloads, before/after                                                                                                     |
| D3-02 | `sendPing`/`sendAPPPosition`/`sendHey`/`SendAckMessage`/... "finalize and encode APRS message" epilogue                                | src/loop_functions.cpp:3326-3340 (sendPing), 4740-4754 (sendPosition), 4817-4831 (sendAPPPosition), 4969-4983, 5307-5321 (+2 more per S6 rank 29, 7 copies total)                                     | Extract `msgid++; wrap at 999; save_settings(); checkVia(aprsmsg); encodeAPRS(msg_buffer, aprsmsg); if(bDisplayInfo) printBuffer_aprs(tag, aprsmsg)` into `void finalizeAndSendAPRS(struct aprsMessage &aprsmsg, uint8_t *msg_buffer, const char *dbgtag)` — caller still does its own addTxRingEntry/addNodeData/addBLEOutBuffer after, since those vary                                                                                                                                                                                                                                                                         | ~55 (7 copies x ~9 lines -> 7x2-line calls + 1 ~11-line helper)                            | ~440-660 B (rough)                                                                               | none                                                                                           | L                                                                                         | none — helper produces byte-identical `msg_buffer` via unchanged `encodeAPRS()` call | S      | diff `msg_buffer[0..msg_len]` + `aprsmsg.msg_id` across all 7 call sites for identical inputs before/after                                                                                                                     |
| D3-03 | `addBLEOutBuffer` vs `addBLEComToOutBuffer` — two near-identical BLE ring-insert helpers                                               | src/loop_functions.cpp:582-645 (`addBLEOutBuffer`), 650-681 (`addBLEComToOutBuffer`)                                                                                                                  | Do NOT merge into one function — S5 freezes `BLEtoPhoneBuff` (0x91/0x44/type framing + 4-byte timestamp) and `BLEComToPhoneBuff` (plain length+payload, no timestamp) as two different wire shapes with different overflow-clamp values (`UDP_TX_BUF_SIZE-4`/`245`) and different write-pointer advance (`addRingPointer()` vs manual `ComToPhoneWrite++`). Only the manual advance in `addBLEComToOutBuffer` (663-680) is worth extracting: replace with the existing `addRingPointer(ComToPhoneWrite, ComToPhoneRead, MAX_RING, "com")` helper already used by the other function, dropping the duplicated overflow-reset block | ~10                                                                                        | ~80-120 B                                                                                        | none                                                                                           | L                                                                                         | none (only the internal pointer-advance mechanics change, not the buffer bytes)      | S      | unit-check `ComToPhoneWrite` sequence for MAX_RING+3 calls matches old manual logic                                                                                                                                            |
| D3-04 | GPS UBX/PCAS command send — same `#if HELTEC_T114\|\|T_ECHO ... Serial1.write / else GPSSerial.write` guard repeated per command       | src/gps_functions.cpp:684-716 (`SetupL76K`, 4 commands: PCAS04, PCAS02, PCAS11, PCAS03)                                                                                                               | `static void gpsSendCmd(const char *cmd)` wrapping the write + the `#if` port-select once; callers pass the literal NMEA string, printf log line stays inline (format differs per call only in trailing `\n` vs `\r\n`, harmless)                                                                                                                                                                                                                                                                                                                                                                                                 | ~24 (4 blocks x ~6 lines -> 4x1-line calls + ~8-line helper)                               | ~190-290 B                                                                                       | none                                                                                           | L                                                                                         | none (console log lines only, not S5-frozen)                                         | S      | byte-compare bytes written to `GPSSerial`/`Serial1` mock before/after for the 4 commands                                                                                                                                       |
| D3-05 | `lora_setcountry()` per-country frequency/bw/cr/sf table                                                                               | src/lora_setchip.cpp:193-503 (14 `case` blocks, each ~15-20 lines setting `node_freq/node_bw/node_cr/node_sf/node_track_freq/node_preamplebits`, doubled per `#if BOARD_RAK4630\|\|...` vs `#else`)   | Data-drive: `static const struct { double freq_nrf,freq_esp; uint8_t bw_nrf; float bw_esp; uint8_t cr_nrf,cr_esp; uint8_t sf; double track; uint16_t preamble; } country_table[]` indexed by country code, one small loop replaces the switch. Case 7 (MAN, has validation logic) and default stay special-cased                                                                                                                                                                                                                                                                                                                  | ~230 (14 cases avg 16 lines -> ~14-row table (~20 lines) + ~25-line lookup+apply function) | ~1.8-2.8 kB (rough; table itself costs ~14x20B=280B flash, offsetting most of the code-size win) | +280 B flash (const table)                                                                     | M                                                                                         | none (runtime values only; not wire-frozen)                                          | M      | for every country code 1-15, dump `node_freq/bw/cr/sf/track_freq/preamplebits` before/after on both an ESP32 and nRF52 define-set build                                                                                        |
| D3-06 | Radio TX critical-section pattern repeated 3x in one function                                                                          | src/lora_functions.cpp:1854-1873 (track), :1902-1927 (aprs branch after track early-send), :1967-1990 (normal path)                                                                                   | `static bool radioSendNow(const uint8_t *buf, uint16_t len, const char *errtag)` wrapping the `#if BOARD_RAK4630: vTaskSuspendAll/Radio.Send/xTaskResumeAll #else: RADIO_CTRL/enablePATransmit/radio.startTransmit + error rollback #endif` block; caller still sets `bLED_RED`/`bLED_ORANGE` and does its own ringBuffer rollback since those differ slightly (rollback fields, `iRead` reset only in 2 of 3 call sites)                                                                                                                                                                                                         | ~55 (3 copies x ~20 lines -> 3x3-line calls + ~22-line helper)                             | ~440-660 B                                                                                       | none                                                                                           | M (real-time radio path; N-16 vTaskSuspendAll comment shows this code is already fragile) | none (Radio.Send/startTransmit call and args unchanged)                              | M      | bench: verify TX actually keys the radio and completes for track, position, and text-message sends after refactor (existing bench harness, tools/*); no software-only diff suffices here given prior nRF52 hang history (N-16) |
| D3-07 | `i2c_scanner.cpp` address-to-device-name if-ladder                                                                                     | src/i2c_scanner.cpp:110-122                                                                                                                                                                           | Data-drive with `static const struct {uint8_t addr; const char *name;} i2c_names[] = {{0x38,"AHT20"},...}` + linear lookup, keep the special-cased 0x3C/0x3D OLED sub-decode (uses register read) outside the table                                                                                                                                                                                                                                                                                                                                                                                                               | ~13                                                                                        | ~100-150 B                                                                                       | +~140 B flash (const table, ~14 rows)                                                          | L                                                                                         | none (console-only diagnostic string)                                                | S      | run against bench fleet I2C bus, compare printed device list before/after                                                                                                                                                      |
| D3-08 | `regex_functions.cpp` `checkRegexCall()` hardcoded allowlist ladder                                                                    | src/regex_functions.cpp:24-49                                                                                                                                                                         | `static const char *call_allowlist[] = {"*","H","HG","BOT GATE","TEST","TESTER","WLNK-1","APRS2SOTA","OE2YOTA-1"};` + loop with `compareTo`, keeping `"DE"` reject and the final regex match as-is                                                                                                                                                                                                                                                                                                                                                                                                                                | ~18                                                                                        | ~140-200 B                                                                                       | +~60 B flash (9 short string literals become 1 array of pointers to same literals — near wash) | L                                                                                         | none                                                                                 | S      | run `checkRegexCall()` over the 9 allowlisted + "DE" + 5 random valid/invalid callsigns, compare bool before/after                                                                                                             |
| D3-09 | `config_json.cpp:117-128` X-macro row repetition                                                                                       | src/config_json.cpp:96-150ish (`X(...)` table rows for `node_mcp170`..`node_mcp1715`, `node_gcb0`..`node_gcb5`, etc.)                                                                                 | **No action** — this is already the data-driven end state (an X-macro table expanded once per settings field by multiple consumer macros). The "9 copies, 8 lines" S6 hit is sliding-window self-similarity across adjacent `X(...)` rows of the same shape, not duplicated logic                                                                                                                                                                                                                                                                                                                                                 | 0                                                                                          | 0                                                                                                | 0                                                                                              | —                                                                                         | none                                                                                 | —      | n/a — false positive, see below                                                                                                                                                                                                |
| D3-10 | esp32_flash.cpp / nrf52_flash.cpp per-field NVS get/set and memcpy repetition (`node_mcp170`..`node_mcp1715`, similar for `node_gcb*`) | src/esp32/esp32_flash.cpp:426-458 (also :131-148), src/nrf52/nrf52_flash.cpp:193-209                                                                                                                  | Note only — D1 owns the flash-layout design. Both are straight-line unrolled per-array-index NVS put/get (ESP32) or `memcpy(...,16)` (nRF52) for `node_mcp17t[0..15]`; a `for(i=0;i<16;i++)` loop with `snprintf("node_mcp17%d",i)` (ESP32) or a single `memcpy(...,16*16)` (nRF52, if layout is contiguous) would remove ~26 lines total but touches the frozen nRF52 struct-copy path (S5 (a): `s_meshcom_settings` raw BLE write) — D1's call whether to touch it                                                                                                                                                              | ~26 (informational)                                                                        | ~200-300 B (informational)                                                                       | none                                                                                           | —                                                                                         | none if kept field-by-field; struct-layout risk if nRF52 loop changes copy order     | —      | defer to D1                                                                                                                                                                                                                    |

## Design notes (top 6)

**D3-01** `decodeAPRSPOS` field extractors. All 11 blocks share: find `'/' + tagchar1 + ('=' or digit)`, copy up to 7 (or 6, or 3 for ncnt) chars into a local buffer until `/`, space, or end, then `sscanf` into a typed field. Signature:
`static void aprsExtractTag(String &buf, unsigned int start, char tag1, char tag2lo, char tag2hi, const char *fmt, void *dest, int maxchars)` — `tag2lo==tag2hi=='='` covers the normal case; `ncnt` needs `tag2lo='1', tag2hi='9'` and a 2-char skip instead of 3. `dest`+`fmt` cover `%d`/`%f`/`%i` targets via `sscanf(decode_text, fmt, dest)` — `dest` is `&aprspos.bat` etc., all are `int*` or `float*`, so `void*` + format string is safe here since the call site already knows the type.

**D3-02** Finalize-and-encode epilogue. Signature: `void finalizeAndSendAPRS(struct aprsMessage &aprsmsg, uint8_t msg_buffer[UDP_TX_BUF_SIZE], const char *dbgtag)`. Varying part is only the `dbgtag` string passed to `printBuffer_aprs()`; everything else (`msgid` wrap, `save_settings()`, `checkVia()`, `encodeAPRS()`) is byte-for-byte identical across the 7 sites, confirmed by direct read of 3 of the 7 (sendPing:3326, sendPosition:4740, sendAPPPosition:4817).

**D3-03** Only the `ComToPhoneWrite++` / overflow-reset block (loop_functions.cpp:670-680) is real duplication — it reimplements `addRingPointer()` (already defined at loop_functions.cpp:5756 and already used by `addBLEOutBuffer` via line 625). Swap in the existing helper; do not touch the buffer-framing code above it (S5 (a): `BLEtoPhoneBuff` vs `BLEComToPhoneBuff` have different frozen byte layouts).

**D3-04** `gpsSendCmd(const char *cmd)`: wraps the `#if defined(USE_HELTEC_T114) or defined(BOARD_T_ECHO)  Serial1.write(cmd); #else GPSSerial.write(cmd); #endif` port-select, called once per NMEA command string (already null-terminated literals like `"$PCAS04,D,D,9*10\r\n"`). The `Serial.printf("[GPS ]>>> ...")` log-echo lines stay at the call site (2 of 4 drop the trailing `\r\n` from the echo, cosmetic, not touched).

**D3-05** `lora_setcountry()` — the whole switch is a country-code -> (freq, bw, cr, sf, track_freq, preamble) lookup table with two variants (RAK/T114/T_ECHO branch vs ESP32/others), currently unrolled per case. Recommend a `struct CountryRfProfile` table with one row per country and the two variants as two columns, then `meshcom_settings.node_* = table[iCtry].field_nrf_or_esp` behind the existing `#if` once. `case 7` (MAN, validates existing settings rather than assigning literals) and `default` (EU, same shape as most rows) stay as explicit code around the table lookup.

**D3-06** `radioSendNow()` mirrors the exact 3 repeats of the `#if BOARD_RAK4630 ... #else RADIO_CTRL/startTransmit ... #endif` TX block already flagged by S6 (score 26, lines 1854-1981). Because this is the live radio TX path with prior fragility (see CLAUDE.md project memory: N-16 vTaskSuspendAll fix for a real hang), this is the one proposal in this file rated risk M rather than L even though it touches no wire bytes — verify on bench hardware, not by diffing buffers.

## False positives (S6 classes dismissed)

- `src/mheard_functions.cpp:853-898` (S6 pass1 rank 1/2, "19-20 copies x 8-9 lines") — single function `getHardwareLong()` (850-903), a monotonic `if(ihw==N) ihw=M;` remap ladder. Sliding-window self-overlap over one long if-chain, not repeated logic elsewhere. Real opportunity is data-driving (39->13, 40->22, ... as a lookup table/array), not de-duplication — noted informally, not in the proposal table since it's a single 50-line function, below the "family" bar the other analysts' brief implies; flag for whoever owns mheard_functions.cpp broadly.
- `src/mheard_functions.cpp:922-971` vs `src/t-deck-pro/ui_deckpro.cpp:1300-1352` (S6 pass1 rank 48) — cross-file with a display/UI file (`t-deck-pro/ui_deckpro.cpp`), out of this analyst's scope per brief (display/UI dirs owned by others). The mheard-only side (`showMHeardTDECK`, 909-980ish) does reimplement the same `':'/'!'/'@'/else` payload-type-to-string switch as `getPayloadType()` (same file, line 836), but with different string lengths / lvgl calls per branch — not worth a shared helper given the differing snprintf widths (`"TXT"` vs `"HY"` vs `getPayloadType()`'s `"TXT"`/`"POS"`/`"HEY"`), true duplication is thin.
- `src/config_json.cpp:117-128` (S6 pass1 rank 22, pass2 rank 22) — X-macro table rows, already the canonical data-driven form; see D3-09 above.
- `src/loop_functions.cpp:5648-5716` (S6 pass1 rank 37/39/44, "5-6 copies x 8-10 lines") — `utf8ascii(char)` switch-case mapping UTF-8 continuation bytes to extended-ASCII, and `utf8ascii(String)` mapping extended-ASCII back to ASCII digraphs (`ae`/`oe`/`ue`...). Both are short (7-8 entry) lookup tables written as if-ladders; sliding-window self-overlap within each function, not cross-copy duplication. Below the size threshold to bother turning into a table (~8 entries, ~2-4 lines saved per entry after table overhead) — no proposal.
- `src/lora_setchip.cpp` sub-ranges at rank 11-16, 20, 23, 30-31 (8-16 line, 6-9 copy classes) — all fully contained inside the same `lora_setcountry()` function already covered by D3-05; not separate families, they are the sliding-window decomposition of the one big switch (confirmed by reading 193-503 directly).
- `src/esp32/esp32_flash.cpp:131-148` and `:426-444`, `src/nrf52/nrf52_flash.cpp:193-205` (S6 pass1 rank 5/6/24/36) — real repetition (per-index NVS/memcpy unrolling) but explicitly out of scope for this analyst per the D1 ownership note; captured as D3-10 (note only).
- `src/gps_functions.cpp:686-715` extra pass2 rank (692-707/700-715, "2 copies x 14 lines") — same underlying blocks as D3-04, not a separate family.
- `src/lora_functions.cpp:1867-1884 vs 1910-1927` (S6 pass1 rank 97, "2 copies x 16 lines") — sub-range fully contained in the D3-06 family (the `#else` RADIO_CTRL/startTransmit half of 2 of the 3 repeats); not separate.

---

<!-- source: A-D4-ui-clones.md -->

## A-D4: UI Clones (src/t-deck, src/t-deck-pro, src/t5-epaper)

Key framing fact (from S1-build-matrix.md): `t_deck`/`t_deck_plus`, `t_deck_pro`,
and `t5_epaper` are mutually exclusive per env (`+t-deck/*,-t-deck-pro/*,-t5-epaper/*`
and permutations). **No single binary ever contains two of these UIs**, so
cross-UI merges are DRY/maintenance wins (fewer source lines to keep in sync,
one bug fixed once), not flash-size wins for either board — except where the
duplicate is genuinely dead code in one of the two copies (D4-05), which does
shrink that target's flash.

Bench reality: T-Deck Plus (DK5EN-14) is on the bench. **T-Deck Pro and T5
e-paper have no bench hardware available** — any change touching
`src/t-deck-pro/*` or `src/t5-epaper/*` can only be verified by clean build +
`ram-snapshot`/`.bin` diff, never by a flashed functional test, until that
hardware exists.

## Proposals

| ID    | title                                                                                                         | files:lines                                                                                                                                                                                                                                                                             | change                                                                                                                                                                                                                                                                                                                                                                                                   | LOC removed            | flash delta                                                                                                                                                                                                 | RAM delta                      | risk                                                                                                                     | wire contract                                                                                                                                                                                        | effort                                                                                                                                                                                                                                         | verification                                                                                                                                                                                                                                            |
| ----- | ------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D4-01 | Merge the two `peri_gps.cpp` (TinyGPS++/u-blox M10Q + L76K driver) into one shared file                       | `src/t-deck-pro/peri_gps.cpp:1-417` vs `src/t5-epaper/peri_gps.cpp:1-379` (0.87 similarity, 152-line diff)                                                                                                                                                                              | Move to `src/ui_common/peri_gps.cpp`, gate the ~15 real deltas (extern vs static `TinyGPSPlus gps`, `gps_fix`/`gps_day` fields present only on pro, `setupGPS()` call commented out on pro, `Serial→SerialGPS` passthrough only on t5) behind `#ifdef BOARD_T_DECK_PRO`                                                                                                                                  | ~370 (796→~430)        | ~0 (mutually exclusive envs; each target still gets exactly the code it has today)                                                                                                                          | ~0                             | L                                                                                                                        | none                                                                                                                                                                                                 | build-only for both `t_deck_pro` and `t5_epaper` (clean rebuild, diff `.bin` size); **no bench for either board**                                                                                                                              |
| D4-02 | Merge `scr_mrg` screen-manager pair (near-byte-identical)                                                     | `src/t-deck-pro/ui_scr_mrg.c:1-267`+`.h:1-57` vs `src/t5-epaper/scr_mrg.cpp:1-275`+`.h:1-63` (ranks 34/42/47/52, 48+37+35+27=147 identical lines out of ~267)                                                                                                                           | Move to `src/ui_common/scr_mrg.c`+`.h`; only real diffs are the header include name, `default_bg_color` (`0xFF0000` pro vs `0x000000` t5, `ui_scr_mrg.c:17`/`scr_mrg.cpp:17`) and one commented-out `lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE)` (`scr_mrg.cpp:31`) — expose bg color via `scr_mgr_set_bg_color()` (already exists, just call it once at init per board) and drop the commented line | ~330 (542+120→~280+60) | ~0                                                                                                                                                                                                          | ~0                             | L                                                                                                                        | none                                                                                                                                                                                                 | build-only, both targets; no bench for either                                                                                                                                                                                                  |
| D4-03 | Extract shared mheard-row-decode helper (call/time/payload-type ladder)                                       | `src/mheard_functions.cpp:909-992` (`showMHeardTDECK`) vs `src/t-deck-pro/ui_deckpro.cpp:1269-1390` (`ui_mheard_disp`) — rank 48, 31 identical lines at `mheard_functions.cpp:922-971`/`ui_deckpro.cpp:1300-1352`                                                                       | Extract `decodeMHeard()`-driven row fill (call, time, TXT/POS/HY/??? ladder) into one helper e.g. `fillMheardRow(mheardLine&, char *callBuf, char *timeBuf, char *typeBuf)` in `mheard_functions.cpp`/`.h`; each board's `lv_table_set_cell_value` column mapping (T-Deck: 7 cols incl. HW/SSI/SNR/NC; Pro: 4 cols, rssi only) stays board-local                                                         | ~50                    | ~0 (t-deck build already excludes mheard_functions.cpp's TDECK branch when not T-Deck; no cross-inclusion)                                                                                                  | ~0                             | M                                                                                                                        | none — display-only rendering, S5-wire-contract.md confirms this isn't a wire path                                                                                                                   | M (column-count parameterization)                                                                                                                                                                                                              | Bench: T-Deck Plus DK5EN-14 — verify MHeard screen still renders 7 cols correctly after refactor. T-Deck Pro not available; build-only there                                                                                                            |
| D4-04 | Extract shared "TRACK no-fix" text formatter                                                                  | `src/t-deck-pro/ui_deckpro.cpp:2182-2218` vs `src/t-deck/lv_obj_functions.cpp:4340-4376` — rank 51, 27 identical lines, byte-identical `snprintf` format string and argument list                                                                                                       | New `formatTrackNoFix(char *buf, size_t n, ...)` in `src/ui_common/track_text.h` (header-only inline, avoids adding a `.cpp` to two independent filters)                                                                                                                                                                                                                                                 | ~25                    | ~0                                                                                                                                                                                                          | ~0                             | L                                                                                                                        | none                                                                                                                                                                                                 | Bench: T-Deck Plus DK5EN-14 — verify TRACK screen no-fix text unchanged (`--pos` shows GPS, per bench-pitfalls memory). T-Deck Pro build-only, no bench                                                                                        |
| D4-05 | Delete two dead font files (declared, never referenced)                                                       | `src/t-deck-pro/src/Font_Mono_Bold_20.c` (960 lines, declared `src/t-deck-pro/src/assets.h:33`, zero call sites in any `t-deck-pro/*.cpp`/`.h`) and `src/t5-epaper/t5-src/Font_Geist_Light_20.c` (declared `assets.h:26`, zero call sites in `t5-epaper/*.cpp`)                         | Remove both `.c` files and their `LV_FONT_DECLARE` lines                                                                                                                                                                                                                                                                                                                                                 | ~1960 (960+~1000)      | **real reduction this time** — rough estimate 3-5 KB per target (bpp4 20px Latin subset glyph table), on `t_deck_pro` and `t5_epaper` respectively, since these are dead-but-compiled data, not shared code | ~0 (flash-resident const data) | L                                                                                                                        | none                                                                                                                                                                                                 | Bench: neither T-Deck Pro nor T5 e-paper available — build-only, confirm `.bin` shrinks and app still renders (no font symbol used should mean no functional change); flag for functional bench check whenever that hardware becomes available |
| D4-06 | Hoist matching LoRa RF parameters (freq/BW/SF/CR/preamble/syncword) into one header, leave init flow separate | `src/t-deck-pro/peripheral.h:30` (`LORA_FREQ 433.175`, others hardcoded inline in `peri_lora.cpp:47,53`) vs `src/t5-epaper/peripheral.h:13-19` (`LORA_FREQUENNCY`, `LORA_BANDWIDTH 250.0`, `LORA_SPREAD_FACTOR 11`, `LORA_CODING_RATE 6`, `LORA_PREAMBLE_LENGTH 8`, `SYNC_WORD_SX127x`) | New `src/ui_common/lora_rf_defaults.h` with the 6 constants (values already match: 433.175/250.0/11); both `peri_lora.cpp` keep their own init/task code, just `#include` the shared constants                                                                                                                                                                                                           | ~10                    | 0                                                                                                                                                                                                           | 0                              | M — values must be re-verified as truly identical before merging, a silent constant drift here changes on-air parameters | wire contract touched: **LoRa RF constants only, not the MeshCom air-frame layout** — confirm these constants don't already diverge from `meshcom_settings.node_*` runtime overrides before touching | S                                                                                                                                                                                                                                              | Bench: T-Deck Plus doesn't use this file (t-deck uses RA-01/SX126x via `lora_functions.cpp`, not `peri_lora.cpp`) — **no bench possible for either affected board**; build-only, treat as higher-risk until a T-Deck Pro or T5 unit exists to RF-verify |
| D4-07 | Data-drive `tdeck_main.cpp` keyboard-type-3/4 remap ladders                                                   | `src/t-deck/tdeck_main.cpp:967-1021` (`iKeyBoardType==3` char-remap `if/else if` chain and `iKeyBoardType==4` 26-entry `sym_map[26]` table already, but the surrounding key-code lookup is still ladder-shaped)                                                                         | Replace the `iKeyBoardType==3` remap `if/else if` chain (`act_key==0x77→0x31` etc., ~18 branches) with a small `static const struct {char from,to;}` table + loop                                                                                                                                                                                                                                        | ~30                    | ~0 (compiler already collapses short constant-compare chains similarly)                                                                                                                                     | ~0                             | M — **this is a physical-keyboard remap table for actual user text entry**, a table-index bug silently breaks typing     | S                                                                                                                                                                                                    | Bench: T-Deck Plus DK5EN-14 — mandatory, type on all 3 non-default keyboard-type settings and confirm every character maps correctly (per bench-serial-message memory, use `::{GRP}text` injection to compare before/after)                    |
| D4-08 | Data-drive `event_functions.cpp` APRS-symbol dropdown string↔char mapping                                     | `src/t-deck/event_functions.cpp:67-132` — `LV_EVENT_VALUE_CHANGED` `strcmp` ladder (Runner/Car/Cycle/.../Node → symcd char) and `LV_EVENT_CLICKED` reverse `switch` (char → `isel` index)                                                                                               | Single `static const struct {const char *name; char symcd;} APRS_SYM_TABLE[]` used by both directions (forward `strcmp` loop, reverse linear scan for `isel`)                                                                                                                                                                                                                                            | ~40                    | ~0                                                                                                                                                                                                          | ~0                             | L                                                                                                                        | none — this only affects the local dropdown UI, not the APRS symbol byte written to `meshcom_settings.node_symcd`/wire                                                                               | S                                                                                                                                                                                                                                              | Bench: T-Deck Plus DK5EN-14 — open Settings→APRS symbol dropdown, cycle all 9 entries, confirm selection and saved symcd round-trip (reopen screen, correct entry pre-selected)                                                                         |

## Rejected / no action

- **GPS core delegation** (`src/gps_functions.cpp:1-1450`, `WZ_GPS_*` API) vs the
  two `peri_gps.cpp` files: **not mergeable, don't attempt it.**
  `gps_functions.cpp` is a from-scratch NMEA + UBX-binary driver (soft baudrate
  detection via `gpsScanBauds()`/`detectBaudrate()` at
  `gps_functions.cpp:238-427`, custom UBX frame builders) written against a raw
  `HardwareSerial`, feeding `gps_filter.h`/`alt_fusion.h`. The two `peri_gps.cpp`
  files are a different, vendored driver built on the `TinyGPS++` library
  (`#include <TinyGPS++.h>`, `t-deck-pro/peri_gps.cpp:3`) with LilyGo-example
  `GPS_Recovery()`/`getAck()` UBX ack-polling boilerplate
  (`t-deck-pro/peri_gps.cpp:322-416`) that doesn't exist in the core driver at
  all. Making the LVGL boards delegate to `gps_functions.cpp` would be a
  library swap and rewrite of the LVGL boards' GPS stack — exactly the kind of
  large rewrite CLAUDE.md's "Minimal Changes Only" rule forbids. D4-01 (merge
  the two `peri_gps.cpp` copies with each other) is the only in-scope move.
- **`peripheral.h` / `utilities.h` full-file merge** (S6 ratios 0.32-0.47):
  rejected beyond D4-06. The differences are real hardware — T-Deck Pro has
  touch (CST328), keypad (TCA8418), gyro (BHI260AP), light sensor (LTR-553ALS),
  fuel gauges (BQ25896/BQ27220) that T5 e-paper simply doesn't have
  (`t-deck-pro/peripheral.h:12-28` `E_PERI_*` enum vs no such enum in
  `t5-epaper/peripheral.h`); pin maps differ per board (`utilities.h` SPI/I2C/
  GPS pins). Forcing a shared header would just become an `#ifdef` maze with no
  LOC win.
- **`peri_lora.cpp` full-file merge**: rejected. `t-deck-pro/peri_lora.cpp:34`
  (`lora_init`, called synchronously, no task) vs `t5-epaper/peri_lora.cpp:36-45`
  (`lora_sx1262_init` + a dedicated FreeRTOS `lora_task`, `lora_sleep()`/
  `lora_recv_suspend()`/`lora_recv_resume()` power-management API the pro board
  doesn't have) are genuinely different control-flow designs, not copy-paste
  drift. 0.32 similarity is mostly shared RadioLib API call shape, not
  duplicated logic.
- **`img_start.c` (t5-src) vs `img_start.c` (t-deck-pro/src)**: confirmed not a
  duplicate (S6 ratio 0.014, 3877 vs 1265 normalized lines) — two different
  images with the same filename. No action.
- **T5 e-paper icon set (`img_battery.c` … `img_wifi.c`, 11 files)**: all 11 are
  referenced from `icon_buf`/`icon_buf2` in `t5-epaper/ui.cpp:250-263`. The one
  `img_refresh` symbol found by grep is inside a commented-out line
  (`t5-epaper/ui.cpp:258`) and `img_refresh.c` doesn't exist — stale comment,
  not dead compiled code. No action.
- **`ui_deckpro.cpp` vs `lv_obj_functions.cpp` further widget-builder clones**:
  grepped `lv_obj_create`/`lv_label_create` call counts (46 in `ui_deckpro.cpp`,
  73 in `lv_obj_functions.cpp`, 66 in `t5-epaper/ui.cpp`) but found no further
  matching multi-line blocks beyond the one already covered by D4-04 (rank 51).
  The high counts are each board's own repeated widget-construction idiom
  (self-similarity within one file), not cross-file duplication — S6's ranked
  clone list already surfaces every cross-file block ≥8 lines, and only rank 51
  crosses the `t-deck`/`t-deck-pro` boundary.
- **`tdeck_main.cpp`/`event_functions.cpp` "sliding repeat" ranks 3/17/21/30-33**:
  these are S6 rolling-hash windows of the _same_ two ladders already captured
  whole in D4-07/D4-08 (the ranked table lists overlapping 8-14 line windows of
  one longer chain, not distinct duplicate blocks) — no additional proposal
  beyond D4-07/D4-08.
- **`src/tft_display_functions.cpp` (432 lines) vs the LVGL UIs**: no overlap.
  Guarded by `#if defined(HAS_TFT)` / `HAS_TFT_CONNECT` / `HAS_TFT_114`
  (`tft_display_functions.cpp:9,256,345`) — this is the `TFT_eSPI`-direct-draw
  path for `heltec_t114` and similar small-TFT boards, a completely different
  rendering stack (immediate-mode sprite drawing) from LVGL's retained-mode
  widget tree used by `t-deck`/`t-deck-pro`/`t5-epaper`. Per S1-build-matrix.md
  these boards are never in the same `build_src_filter` as the LVGL dirs. No
  shared logic to extract.
- **`Font_Mono_Bold_14..19` (t-deck-pro) / `Font_Mono_Bold_25/30/90`,
  `Font_Geist_Bold_20` (t5-epaper)**: all confirmed referenced (grep counts:
  14→4, 15→3, 16→2, 17→2, 18→2, 19→2 call sites in t-deck-pro; 20→1, 25→25,
  30→10, 90→2, Geist_Bold_20→1 in t5-epaper). No shared _size_ is actually used
  by both boards (t-deck-pro's dead `Font_Mono_Bold_20` doesn't count, see
  D4-05), so a `src/ui_fonts/` shared-size directory isn't warranted — nothing
  to put in it once the two dead files are removed.

## Design notes (top 5: D4-01, D4-02, D4-03, D4-04, D4-05)

New directory `src/ui_common/` holds only code genuinely shared by ≥2 of the
three mutually-exclusive LVGL UIs: `peri_gps.cpp`/`.h` (D4-01), `scr_mrg.c`/`.h`
(D4-02), and header-only `track_text.h` (D4-04). D4-03 stays in
`mheard_functions.cpp` since it's core-to-T-Deck already and T-Deck-Pro's
`ui_deckpro.cpp` can `#include "mheard_functions.h"` and call the new helper
directly rather than moving core mheard code out of core. D4-05 needs no new
directory, just deletion.

`platformio.ini` filter change: `src/ui_common/*` must be **excluded by
default** and only **re-included** for the three LVGL envs, or every other
hardware env (classic ESP32, nRF52, e-paper `Displays/*` boards) would try to
compile LVGL-dependent code it can't link:

- base `[esp32]` and `[nrf52_base]` `build_src_filter`: add `-<ui_common/*>`
  alongside the existing `-<t-deck*>`, `-<t5-epaper/*>` entries.
- `t_deck` / `t_deck_plus` env filters: add `+<ui_common/*>` next to existing
  `+<t-deck/*>, -<t-deck-pro/*>, -<t5-epaper/*>`.
- `t_deck_pro` env filter: add `+<ui_common/*>` next to `+<t-deck-pro/*>, ...`.
- `t5_epaper` env filter: add `+<ui_common/*>` next to `+<t5-epaper/*>, ...`.

Each `peri_gps.cpp`/`scr_mrg.c` keeps its board delta behind `#ifdef
BOARD_T_DECK_PRO` (that macro is already defined per-env per S1-build-matrix.md,
no new `-D` flags needed). Given neither T-Deck Pro nor T5 e-paper hardware is
on the bench, land D4-01/02/03/04/05 as a single low-risk wave, verify with
clean rebuilds of `t_deck_pro` and `t5_epaper` (`.bin` size diff via the
existing `ram-snapshot` skill) plus the T-Deck Plus bench pass for D4-03/D4-04's
shared-with-T-Deck pieces, and explicitly flag in the PR description that
Pro/T5 functional verification is deferred until that hardware is available —
consistent with "Minimal Changes Only" and the PR-description rule to state
why something is deferred.

---

<!-- source: A-D5-periph-web-net.md -->

## D5 — Peripheral / Web / Network / Time / Logging / Regex Duplication Audit

Read-only. Repo `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`, HEAD c51c5881. No edits made.

## Proposals

| ID    | Title                                                                                                                           | Files:lines                                                                                                                                                                                                                                                                                          | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | LOC removed                                                                                                        | Flash Δ                                            | RAM Δ                                                                                                 | Risk | Wire touched                                                                                          | Effort | Verification                                                                                                                                             |
| ----- | ------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | ---- | ----------------------------------------------------------------------------------------------------- | ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D5-01 | `ota_html[]` non-const → const                                                                                                  | `src/safeboot/ota.h:1` (24318-byte array), used by `src/safeboot/ElegantOTA.cpp:25,27,40`                                                                                                                                                                                                            | Add `const` to `unsigned char ota_html[]` (and `const unsigned int ota_html_len`); all 3 call sites already only read it via `beginResponse`/`beginResponse_P`/`send_P` (the `_P` variants already assume flash-resident data)                                                                                                                                                                                                                                                                                                                                                               | 0 (declaration-only)                                                                                               | ~0 (may shrink slightly, better section placement) | **−~24 KB DRAM on classic ESP32 safeboot** (`.data`→`.rodata`, array no longer copied to RAM at boot) | L    | none                                                                                                  | S      | `ram-snapshot` skill on `esp32-safeboot`/`esp32-S3-safeboot` before/after; diff `.map` section for `ota_html`                                            |
| D5-02 | Shared `battDetectState`/`battDetectReset`/`battDetectUpdate`/`battDetectFeed`/`battDetected` helper                            | `src/batt_functions.cpp:51-121` vs `src/batt_function_old.cpp:51-121` (confirmed S6 clone class #50, 28 lines × 2 copies)                                                                                                                                                                            | Move the pure state-machine (struct `batt_detect_state_t` + 3 functions, no Arduino calls per the file's own comment at `batt_function_old.cpp:34-44`) into a new tiny header e.g. `batt_detect.h`, included by both TUs regardless of `USE_NEW_BATT`. Both `read_batt()` call sites and both `battHardwarePresent()` bodies stay in their own file/board partition — only the detector core is shared.                                                                                                                                                                                      | ~60 (one copy of the struct+3 fns removed)                                                                         | ~0                                                 | ~0 (same code, one copy instead of two, negligible on boards where only one TU links)                 | L    | none                                                                                                  | S      | native `test_batt_detect` (already exists per S4) run unchanged against the extracted helper; diff generated object code for one board of each partition |
| D5-03 | Table-driven `webSetup_setParam`/`webSetup_getParam`                                                                            | `src/web_functions/web_setup.cpp:18-657` (`setParam`, ~640 lines) and `:658-933` (`getParam`, ~275 lines); 109 `paramName.equals(...)` branches total (grep count)                                                                                                                                   | Replace the two giant if/else-if chains with one `struct WebParamDescriptor{name, cmdFmt, get()}` table + one dispatch loop. Each entry already reduces to: build `"--cmd %s"`, call `commandAction()`, compare/return current settings value — the shape `_create_setup_textinput_element`/`_create_setup_switch_element` already made explicit for rendering exists on the parse/format side too.                                                                                                                                                                                          | ~350-450 (out of ~915 total across both functions; heterogeneous branches with extra validation stay hand-written) | small − (fewer duplicated string literals)         | ~0                                                                                                    | M    | none (command strings sent to `commandAction()` must stay byte-identical; not on air/BLE/EXTUDP wire) | L      | diff `--info`/web-setup page output before/after for every settings field (scripted), `webgui_badge_test.js` (per MEMORY.md) against a live node         |
| D5-04 | I2C-bus-reset-on-entry wrapper function                                                                                         | `src/bmx280.cpp:132-137,148-153,178-181,220-223`; `src/bmp390.cpp:55-58,85-88`; `src/sht21.cpp:41-44,68-71`; `src/aht20.cpp:46-49,71-74` (10 total `#if MC_I2C_NEEDS_BUS_RESET` call sites, S4 confirms the _macro_ DRY-25 is done but not the 3-line use-site pattern)                              | Add `static inline void i2cBusResetIfNeeded(void){ #if MC_I2C_NEEDS_BUS_RESET Wire.end(); Wire.begin(I2C_SDA,I2C_SCL); #endif }` next to the macro definition; replace all 10 call sites with one call                                                                                                                                                                                                                                                                                                                                                                                       | ~25                                                                                                                | 0                                                  | 0                                                                                                     | L    | none                                                                                                  | S      | build all 7 targets, confirm identical `Wire.end()/begin()` call sequence (same instruction count) via disassembly or just visual diff                   |
| D5-05 | Sensor descriptor table                                                                                                         | `src/bmx280.cpp`, `bme680.cpp`, `bmp390.cpp`, `sht21.cpp`, `aht20.cpp`, `mcu811.cpp`, `ina226_functions.cpp` (7 files)                                                                                                                                                                               | **Rejected as a single table** (see design note below) — the 7 files share only the _shape_ (setup/loop/getters + `bWDEBUG` print + `_found` bool), not a uniform read/format signature: BME680 needs a two-phase begin/endReading, INA226 has 4 outputs with no found-print, MCU811 has an availability-poll loop, BMx280 derives a custom `Wire` subclass. A literal descriptor table (name/addr/probe fn/read fn) would need a union of incompatible function-pointer signatures and buys little. Propose instead only D5-04 (bus-reset) + D5-06 (found/debug print) as targeted helpers. | 0 (rejected)                                                                                                       | —                                                  | —                                                                                                     | —    | —                                                                                                     | —      | —                                                                                                                                                        |
| D5-06 | Shared "found/not found" + `--info` line formatter                                                                              | `src/bme680.cpp:66-79` (found/not found ×2), `src/bmx280.cpp:170,187,204-207`, `src/bmp390.cpp:62,72`, `src/sht21.cpp:48,54`, `src/aht20.cpp:53,57`, `src/mcu811.cpp:41,52,62`; 29 `_found`-related `command_functions.cpp` printf sites for `--info`/`--wx` output                                  | Add one `static inline void sensorInitLog(const char*name, bool found, uint8_t addr)` used at the tail of each `setupXXX()`; keeps each sensor's existing distinct wording where it differs (BME680's German diagnostic text, `command_functions.cpp`'s `--info` block) untouched — this only unifies the terse `"[INIT]...XXX started"` / `"[INIT]...Could not find a valid XXX sensor"` one-liners that are already near-identical text                                                                                                                                                    | ~15-20                                                                                                             | 0                                                  | 0                                                                                                     | L    | none (console text, not frozen per S5 §f)                                                             | S      | grep before/after Serial output strings match byte-for-byte for the unchanged cases                                                                      |
| D5-07 | `i2c_scanner.cpp` address→name table                                                                                            | `src/i2c_scanner.cpp:74-122` (S6 clone class #40, 6×8-line copies at :110-117..)                                                                                                                                                                                                                     | **Rejected** — already the most compact form (12 sequential `if(address==X)strDev="Y";` lines); S6's mechanical detector flags this only because the _shape_ of each `if` repeats, not because logic is duplicated. Converting to a real `{addr,name}[]` array + loop saves ~0 net lines and adds indirection for zero benefit. Leave as-is.                                                                                                                                                                                                                                                 | 0                                                                                                                  | —                                                  | —                                                                                                     | —    | —                                                                                                     | —      | —                                                                                                                                                        |
| D5-08 | `printfdeb`/`printdeb`/`printlndeb` overload consolidation                                                                      | `src/printfdeb_functions.cpp:160-233` (12 near-identical 3-line overloads: `cdcReady(budget)` guard → `Serial.print/println(v)`)                                                                                                                                                                     | Collapse the 8 numeric/char overloads into one `template<typename T> static inline int printdeb_impl(T v, size_t budget, bool nl)`; keep the 2 `String`/`const char*` overloads separate (different `cdcReady` budget formula using `.length()`/`strlen()`)                                                                                                                                                                                                                                                                                                                                  | ~20                                                                                                                | ~0                                                 | ~0                                                                                                    | L    | none (debug console only)                                                                             | S      | native build of `printfdeb_format` tests unaffected; visual diff of generated calls                                                                      |
| D5-09 | `externTeleJsonNode`/`externTeleJsonLora` shared-field helper                                                                   | `src/extern_tele_json.h:30-56` vs `:58-89` (8 shared keys: temp1,temp2,hum,qfe,qnh,gas,co2,din)                                                                                                                                                                                                      | **Rejected / low value** — both already use `ArduinoJson` (`JsonDocument`+`serializeJson`), already minimal (~25 lines each); extracting a `fillTeleCommon(JsonDocument&,...)` risks disturbing ArduinoJson's insertion-order (S5: key order is part of the frozen wire bytes) for a ~10-line saving. Not worth the wire-adjacent risk.                                                                                                                                                                                                                                                      | 0                                                                                                                  | —                                                  | —                                                                                                     | —    | none touched, but risk of touching it is why it's rejected                                            | —      | —                                                                                                                                                        |
| D5-10 | Three JSON writers (`ble_json_frame.h`, `extern_notice_json.h`, `extern_tele_json.h`) vs `config_json.cpp`'s hand-rolled writer | `src/ble_json_frame.h`, `src/extern_notice_json.h`, `src/extern_tele_json.h` vs `src/config_json.cpp:229-441` (`CfgField` table + `out_add`/`out_add_jsonstr`/`canon_*`)                                                                                                                             | **No action** — the first three already converged on one shared pattern (`ArduinoJson` + a thin per-shape wrapper); `config_json.cpp`'s custom writer exists specifically for deterministic CRC canonicalization (`canon_kv`/`canon_end`) that `ArduinoJson`'s escaping can't guarantee byte-for-byte — merging would risk the CRC contract. Task's premise ("three hand-built JSON writers") does not hold; only `config_json.cpp` is genuinely hand-rolled and it is hand-rolled _for a reason_.                                                                                           | 0                                                                                                                  | —                                                  | —                                                                                                     | —    | —                                                                                                     | —      | —                                                                                                                                                        |
| D5-11 | Scattered `%02i:%02i:%02i` time formatting                                                                                      | `src/rtc_functions.cpp:117`, `src/loop_functions.cpp:1676,1678`, `src/command_functions.cpp:6045`, `src/phone_commands.cpp:428` (5 total call sites; task's grep estimate of "many" does not hold — `getTimeString()` at `src/time_functions.cpp` already has 16 callers and covers the common case) | Add one `static inline void fmtHms(char*buf,size_t n,int h,int m,int s)` for the 3 byte-identical `"%02i:%02i:%02i"` sites (`rtc_functions.cpp:117` is `"%02i.%02i.%i %02i:%02i:%02i"` — different, leave it; `loop_functions.cpp:1676/1678` are the true duplicate pair)                                                                                                                                                                                                                                                                                                                    | ~4                                                                                                                 | 0                                                  | 0                                                                                                     | L    | none (display/console text)                                                                           | S      | display-string snapshot diff on T-Deck/T5-epaper battery-line rendering                                                                                  |
| D5-12 | Regexp.cpp / regex_functions.cpp hardware-usage correction                                                                      | `src/Regexp.cpp` (512, vendored Lua-pattern engine), `src/regex_functions.cpp` (57); consumers `src/aprs_functions.cpp`, `src/udp_functions.cpp`, `src/command_functions.cpp`, `src/nrf52/nrf_eth.cpp`, `src/t-deck-pro/ui_deckpro.cpp` (all production, not test-only)                              | **No change — correcting a false premise from the audit brief.** `grep -rn 'Regexp.h\|MatchState' src` shows 5 non-test consumers. `platformio.ini`'s base `[env:esp32]` (`:108-115`) excludes only `-<safeboot/*>`; hardware `variants/*/platformio.ini` envs `extends = esp32` and add no further filter, so `Regexp.cpp`/`regex_functions.cpp` compile into every hardware build, not just `native_*` test envs. It is not dead weight on hardware.                                                                                                                                       | 0                                                                                                                  | —                                                  | —                                                                                                     | —    | none                                                                                                  | —      | grep confirmed above; not a candidate for removal or #ifdef-gating                                                                                       |

## Design notes (top 5, with helper signatures)

**D5-01** (`ota_html` const): one-line fix. `const unsigned char ota_html[] PROGMEM = {...}; const unsigned int ota_html_len = 24318;` — `PROGMEM` is a no-op on ESP32/ESP32-S3 (flash is already memory-mapped) but documents intent; the load-bearing change is `const`, which the linker uses to place the array in `.rodata`/flash rather than `.data` (RAM-resident, flash-backed init copy). `ElegantOTA.cpp`'s three call sites take `const uint8_t*`-compatible params already (the `_P` suffix APIs expect PROGMEM-style const data), so no signature changes needed downstream.

**D5-02** (shared batt detector): `bool battDetectUpdate(batt_detect_state_t *state, float rawMv, float minPlausibleMv, float maxPlausibleMv);` `void battDetectReset(batt_detect_state_t *state);` move verbatim into `src/batt_detect.h` (header-only, `static inline`, no Arduino types — matches the file's own claim that it's "pure … natively testable"). Both `.cpp` files `#include "batt_detect.h"` unconditionally (drop the `#if defined(USE_NEW_BATT)` guard around just this piece). `battDetectFeed()`/`battDetected()` stay file-local `static` wrappers around a file-local `static batt_detect_state_t` instance, since each TU needs its own one-instance-per-node state and the two TUs are never linked together (board-partitioned).

**D5-03** (table-driven web setup): `struct WebParamDescriptor { const char *name; const char *cmdFmt; /* printf-style, one %s */ std::function<String()> getValue; };` populated as `static const WebParamDescriptor kWebParams[] = { {"setcall","--setcall %s", []{return String(meshcom_settings.node_call);}}, ... };` Dispatch: linear scan by `paramName`, `snprintf(cmd, sizeof cmd, d.cmdFmt, paramValue.c_str())`, `commandAction(cmd,bPhoneReady)`, then `returnValue = d.getValue()`. Branches with non-uniform shape (`onewire`/`button` on/off toggles compare bool state, `netmode` is board-guarded) either get a small `cmpFn` field too, or stay as pre/post exceptions bracketing the table loop — do not force every branch in to avoid subtle behavior changes.

**D5-04** (bus-reset wrapper): `static inline void i2cBusResetIfNeeded(void)` placed in `configuration_global.h` next to `MC_I2C_NEEDS_BUS_RESET`'s definition, so all 4 sensor files get it via their existing include of `configuration.h`.

**D5-06** (found/init log): `static inline void sensorInitLog(const char *name, bool found, uint8_t addr)` prints `"[INIT]...%s started\n"` or `"[INIT]...Could not find a valid %s sensor, check wiring\n"`; only applied to the 4 files (`bmp390.cpp`, `sht21.cpp`, `aht20.cpp`, and the found branch of `bmx280.cpp`) whose message text is already textually identical modulo the sensor name — `bme680.cpp`'s richer German diagnostic and `mcu811.cpp`'s retry-count message stay untouched.

## Rejected

- **D5-05**: uniform sensor descriptor table across 7 files — heterogeneous read/probe shapes, not worth a function-pointer abstraction (see table).
- **D5-07**: `i2c_scanner.cpp` address table — already minimal, S6 false positive from its normalization pass.
- **D5-09**: shared tele-JSON field filler — wire-adjacent (key order), ~10-line saving not worth the risk.
- **D5-10**: unifying `config_json.cpp`'s writer with the ArduinoJson-based ones — `config_json.cpp`'s custom writer exists for CRC canonicalization, a different job; task's "three hand-built JSON writers" premise doesn't hold, only one is hand-rolled and for a reason.
- **D5-12**: Regexp.cpp/regex_functions.cpp as dead hardware weight — false; confirmed compiled and used in 5 production files, not native-test-only. No action.
- Batt dual implementation itself (`batt_functions.cpp` vs `batt_function_old.cpp`) — per S4, both stay alive; only the ~50-line shared detector core (D5-02) is a legitimate shared-helper candidate, not a merge of the whole file.
- DRY-25 (I2C bus-reset macro) — already done per S4, not re-proposed; D5-04 only wraps the existing macro's _use-site pattern_, does not touch the macro itself.
- `net_console.cpp`, `ntp_async.cpp`, `softser_functions.cpp`, `external_radio_glue.cpp` — surveyed, no mechanical duplication found beyond generic Serial-print boilerplate already covered by D5-08; each solves a genuinely distinct protocol (HMAC auth console, minimal NTP client, softserial telemetry decode). No proposal.
- `clock.cpp`/`clock.h` (vendored `Clock` class) vs `time_functions.cpp`/`rtc_functions.cpp` — no overlapping leap-year/date-math logic found (`grep` for leap-year handling in `clock.cpp` is empty); `getTimeString()` already has 16 call sites covering the common formatting need. No proposal beyond D5-11.
- `udp_functions.cpp` vs `extudp_functions.cpp` shared socket/stat helper — surveyed; `udpCountTx()`/`udpPrintStat()` (`udp_functions.cpp:100-131`) have no counterpart in `extudp_functions.cpp` (different counters, different frozen `[UDPSTAT]` format per S5) — not duplicated, no proposal.

---

<!-- source: A-D6-dead-code-variants.md -->

## D6 — Dead Code, Unused Assets, Duplication in Build Configuration

Audit only, no edits. All flash/RAM deltas assume `-ffunction-sections -fdata-sections` +
`-Wl,--gc-sections` are on by default for **both** frameworks used here — confirmed at
`~/.platformio/packages/framework-arduinoespressif32/tools/platformio-build-esp32*.py:58-82` and
`~/.platformio/packages/framework-arduinoadafruitnrf52/platform.txt:36-52`. Neither `platformio.ini`
nor any `variants/*/platformio.ini` overrides these. Consequence: deleting an unreferenced function
or header typically yields **0 flash/RAM delta** — the linker already drops it. The value of D6
proposals is compile-time hygiene, repo size, and audit-signal, not memory.

## Proposals

| ID                                   | Title                                                                              | Files:lines                                                                                                                                                                                                                                                                                  | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | LOC removed                                                                      | Flash/RAM delta                                                                                                                                                            | Risk | Wire contract | Effort | Verification                                                                                                                                            |
| ------------------------------------ | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---- | ------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D6-01                                | Variant `configuration.h` → common base + per-board delta                          | `variants/*/configuration.h` (30 files, 3397 lines total)                                                                                                                                                                                                                                    | Introduce `variants/configuration_default.h` with the shared `#define` set; each `variants/<board>/configuration.h` keeps only the `#define`s that differ from default (verified clusters: E22 family ratio 0.92-0.98, heltec V3/wireless_stick 0.93, t_deck/t_deck_plus 0.98)                                                                                                                                                                                                                                                                                                                                                                                                                                         | ~1200-1500 (est., see matrix below — clusters >0.85 average ~75% shared content) | 0 — headers, no codegen                                                                                                                                                    | M    | none          | L      | build all 32 envs unchanged, `diff` each resolved macro set pre/post via `-E` preprocessor dump                                                         |
| D6-02                                | `platformio.ini` `extends=` for near-duplicate variant envs                        | `variants/{E22-DevKitC,E22_1262-DevKitC,esp32-loraprs-e22,esp32-loraprs-ra01}/platformio.ini` (ratio 0.93-0.97 pairwise), `variants/{ttgo_tbeam,ttgo_tbeam_SX1262,ttgo_tbeam_SX1268}/platformio.ini` (ratio 0.92), `variants/{E22_1262_S3,E22_1268_S3}-DevKitC*/platformio.ini` (ratio 0.97) | Add one `[e22_base]` / `[ttgo_tbeam_base]` section per cluster in the root `platformio.ini` (pattern already used at line 107/520/673 for `upload_settings` and `t_deck_pro`) and `extends=` it from each member's `variants/*/platformio.ini`, keeping only per-board `-D BOARD_*` / `board=` lines                                                                                                                                                                                                                                                                                                                                                                                                                   | ~120-150                                                                         | 0 — ini only                                                                                                                                                               | M    | none          | M      | `pio project config --json-output -e <env>` diff pre/post per affected env                                                                              |
| D6-03                                | Remove vestigial `lv_conf.h` copies from non-LVGL variant dirs                     | 21 files under `variants/{E22*,esp32-loraprs-*,heltec_*,t_echo,T-ETH-ELITE_1262,t5_epaper,ttgo*}/lv_conf.h` (15,010 lines total)                                                                                                                                                             | Delete — LVGL is in every one of these envs' `lib_ignore` list (verified `heltec_wifi_lora_32_V3/platformio.ini:11-19`: `lib_ignore` includes `lvgl`), so `lv_conf.h` is never picked up by the `-I variants/${this.__env__}` include path since `lvgl.h` (the only consumer of `LV_CONF_INCLUDE_SIMPLE`) is never compiled for these envs. Only `t_deck`, `t_deck_plus`, `t_deck_pro` actually build LVGL and keep a real `lv_conf.h`/`src/t-deck/lv_conf.h`                                                                                                                                                                                                                                                          | 15,010                                                                           | 0 — never compiled/read                                                                                                                                                    | L    | none          | S      | `grep -L lvgl <env>/platformio.ini` cross-check + build all 32 envs, confirm no `lv_conf.h not found` regression                                        |
| D6-04                                | Delete unused `src/nrf52/glcdfont.c`                                               | `src/nrf52/glcdfont.c` (142 lines)                                                                                                                                                                                                                                                           | Delete — a second, byte-different copy of the Adafruit GFX 5x7 font exists at `src/GFX_Root/glcdfont.c`; `src/GFX_Root/GFX.cpp:35` does `#include "glcdfont.c"` which resolves relative to `GFX_Root/`, so the nRF52 copy is never `#include`d anywhere in the tree (`grep -rn '#include.*glcdfont' src/` finds only the GFX_Root include)                                                                                                                                                                                                                                                                                                                                                                             | 142                                                                              | 0 (never compiled since never included; even as a standalone `.c` TU it defines nothing externally visible under its header guard `FONT5X7_H`)                             | L    | none          | S      | grep confirms zero includes; build nRF52 envs, confirm no missing-symbol regression                                                                     |
| D6-05                                | Delete dead `init_scan()` stub                                                     | `src/spectral_scan.cpp:190-193`                                                                                                                                                                                                                                                              | Delete 4-line stub `int init_scan(float freq){return RADIOLIB_ERR_WRONG_MODEM;}` — distinct from the real, used `sx126x_spectral_init_scan()` (same file, line 50, called from line 152); `init_scan` doesn't even match its own header's declared name (`src/spectral_scan.h:27` declares `sx126x_spectral_init_scan`), zero callers anywhere in `src/`+`variants/`                                                                                                                                                                                                                                                                                                                                                   | 4                                                                                | 0                                                                                                                                                                          | L    | none          | S      | grep `init_scan\b` tree-wide before/after                                                                                                               |
| D6-06                                | Delete dead `direction_parse()` + write-only globals                               | `src/nrf52/nrf52_main.cpp:2663-2688` (function), `:313-314` (globals `direction_S_N`, `direction_E_W`)                                                                                                                                                                                       | Delete function and the two globals it exclusively writes — zero callers of `direction_parse`, zero readers of either global outside the function itself                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | ~28                                                                              | 0                                                                                                                                                                          | L    | none          | S      | grep confirms zero refs outside definition                                                                                                              |
| D6-07                                | Delete dead `LilyGo_logo()` / `Veille_logo()`                                      | `src/nrf52/nrf52_functions.cpp:103-118`                                                                                                                                                                                                                                                      | Delete both e-paper splash-screen helpers — sibling `Batterie_Vide_logo()` (line 121) and `MeshCom_Image()` (line 130) in the same file **are** called (from `nrf52_sleep.cpp:61` and `nrf52_functions.cpp:198`); these two are not, anywhere                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | 16                                                                               | 0                                                                                                                                                                          | L    | none          | S      | grep confirms zero refs                                                                                                                                 |
| D6-08                                | Delete dead `ble_log_settings()`                                                   | `src/nrf52/nrf52_flash.cpp:434-440`                                                                                                                                                                                                                                                          | Delete debug-print helper, zero callers; not gated by `DO_DEBUG`/`INSTRUMENT_ENABLED` (checked per INS-01 lesson — it sits inside a plain `#ifdef NRF52_SERIES` that wraps the whole file, not a diagnostics toggle), so this is a genuine orphan, not a swept field-diagnostic                                                                                                                                                                                                                                                                                                                                                                                                                                        | 7                                                                                | 0                                                                                                                                                                          | L    | none          | S      | grep confirms zero refs                                                                                                                                 |
| D6-09                                | Remove commented-out `WZ_L76Kreset()` corpse                                       | `src/gps_functions.cpp:1358-1360+`                                                                                                                                                                                                                                                           | The entire function body is inside a `/* ... */` block already (dead text, not dead code — never compiled); delete the stale comment block per normal cleanup                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | ~15                                                                              | 0 (already not compiled)                                                                                                                                                   | L    | none          | S      | visual confirm still inside `/* */`                                                                                                                     |
| D6-10                                | Remove unused `#include <SD.h>`                                                    | `src/esp32/esp32_main.cpp:45`                                                                                                                                                                                                                                                                | No `SD.`/`\bSD\b` usage in this TU (only a comment mentioning "SD-Kartenmodus" at line 3281); `SD.h` is genuinely used elsewhere (`command_functions.cpp:82`, `mheard_functions.cpp:22`) so the feature stays, just this file's include is dead weight                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | 1                                                                                | 0 (SdFat class instantiation is extern; nothing pulled in without a call site)                                                                                             | L    | none          | S      | compile esp32 envs after removal                                                                                                                        |
| D6-11                                | Prune vendored AVR/SAMD/ESP8266 backends never reachable on this project's targets | `src/Platforms/{M328P,M1280,M2560,SAMD21G18A,ESP8266}/*` (462 lines, 5 `.cpp` + headers)                                                                                                                                                                                                     | These 5 directories are the heltec-eink-modules library's backends for boards this fork never builds (no `__AVR__`, no `_SAMD21_`, no `ESP8266` env exists in `platformio.ini`); `src/Platforms/platforms.h:12-16` unconditionally `#include`s all of them, each guarded internally by `#ifdef __AVR_ATmegaXXXX__` / `#ifdef __SAMD21G18A__` / `ESP8266`-only macros that are never true here. Only `ESP32`, `VisionMasterE213`, `VisionMasterE290`, `WirelessPaper` backends are ever selected (the 3 e-paper envs). Delete the 5 dead dirs and their `#include`s in `platforms.h`                                                                                                                                    | 462                                                                              | 0 — internal guards already empty these TUs for every real build (same pattern as `batt_function_old.cpp`/`spectral_scan.cpp`)                                             | M    | none          | M      | build `vision-master-e290`, `vision-master-e213`, `wireless-paper` (the only 3 envs compiling `Platforms/*`) after removal                              |
| D6-12                                | Prune unreferenced `src/Fonts/*.h`                                                 | `src/Fonts/*.h` (47 of 48 files, ~20,871 of ~21,700 lines)                                                                                                                                                                                                                                   | Only `FreeMonoBold9pt7b.h` is referenced (`src/t-deck-pro/tdeck_pro.cpp:19,65`); the other 47 Adafruit-GFX font tables (`FreeSans*`, `FreeSerif*`, `Org_01`, `Picopixel`, `TomThumb`, etc.) have zero references in `src/Displays`, `src/t-deck-pro`, `src/t5-epaper`, or `variants/`                                                                                                                                                                                                                                                                                                                                                                                                                                  | ~20,871                                                                          | 0 — pure header data tables, `-fdata-sections`+`--gc-sections` already drops any accidentally-instantiated PROGMEM array; effectively this is disk/compile-time bloat only | L    | none          | M      | grep confirms per-font zero refs (see script output below); build the 4 envs that touch `Fonts/*` (vision-master-e290/e213, wireless-paper, t-deck-pro) |
| D6-13 (informational, not an action) | Correct S1's `tinyxml_functions.cpp` classification                                | `src/tinyxml_functions.cpp:1-9`, `src/softser_functions.cpp:151-198`                                                                                                                                                                                                                         | S1's finding table calls `tinyxml_functions.cpp` "excluded from all hardware builds", same wrong claim the orchestrator already corrected for `batt_function_old.cpp`/`spectral_scan.cpp`/`test_inject.cpp`. It is a root `src/*.cpp` file, compiled for every ESP32 env by the `+<*>` filter, but its whole body (after the includes) sits behind `#if defined(ENABLE_XML)` (line 9) — empty TU except when `ENABLE_XML=1` (only `E22_XML-DevKitC`, `platformio.ini:414`). The real call site (`softser_functions.cpp:191`, `decodeTinyXML()`) is correctly guarded by the same macro (line 151), so there is **no** link error and the XML feature is correctly wired, not silently absent. No code change proposed. | 0                                                                                | n/a                                                                                                                                                                        | —    | none          | —      | read-only, confirmed by source inspection                                                                                                               |

## Variant similarity matrix summary (difflib, normalized lines, max 40 lines)

`configuration.h` (30 files) — nearest-neighbour ratio, clusters >0.9:

```
E22_1262_S3-DevKitC-1-N16R8 <-> E22_1268_S3-DevKitC-1-N16R8   0.983
t_deck                      <-> t_deck_plus                   0.981
esp32-loraprs-e22           <-> esp32-loraprs-ra01             0.934
heltec_wifi_lora_32_V3      <-> heltec_wireless_stick          0.930
E22-DevKitC                 <-> E22_XML-DevKitC                0.918
```

Next tier (0.6-0.9, still delta-header candidates): E22_1262-DevKitC↔E22-DevKitC 0.879,
ttgo_tbeam_SX1262↔ttgo_tbeam_SX1268 0.829, heltec_wifi_lora_32_V4↔V3 0.778. Outliers with no close
match (genuinely distinct boards, keep standalone): T-ETH-ELITE_1262 (0.21 to nearest), t_deck_pro
(0.67), vision-master-e290 (0.55), wiscore_rak4631 (0.60 — nRF52, structurally different family).

`platformio.ini` (31 files) — clusters >0.9:

```
E22_1262_S3-DevKitC-1-N16R8 <-> E22_1268_S3-DevKitC-1-N16R8   0.970
E22-DevKitC                 <-> E22_1262-DevKitC               0.966
ttgo_tbeam                  <-> ttgo_tbeam_SX1262               0.920
ttgo_tbeam                  <-> ttgo_tbeam_SX1268               0.920
ttgo_tbeam_SX1262           <-> ttgo_tbeam_SX1268                0.920
E22-DevKitC                 <-> esp32-loraprs-e22/ra01           0.931 (both)
```

Six-member overlapping cluster: {E22-DevKitC, E22_1262-DevKitC, esp32-loraprs-e22,
esp32-loraprs-ra01} all pairwise ≥0.93 — single best `extends=` candidate.

`lv_conf.h`: 24 total copies (21 outside `t_deck*`, all byte-near-identical to each other, ~712-770
lines each) vs. the 2 real ones (`src/t-deck/lv_conf.h` 770 lines vs
`variants/heltec_wifi_lora_32_V3/lv_conf.h` 712 lines — diff shows only an LVGL-version-bump drift,
`LV_COLOR_16_SWAP`, `LV_COLOR_SCREEN_TRANSP`). **heltec_wifi_lora_32_V3 has one because it was
copy-pasted from a template variant dir that predates the `lib_ignore: lvgl` line** — it is add-only
disk clutter, never read (see D6-03).

**Upstream ownership / merge-conflict risk**: `variants/` is upstream-controlled (icssw-org
contributors add/modify board variants directly). D6-01/D6-02/D6-03 restructure or delete files in
every subdirectory of `variants/` in one shot — any upstream PR merged in the same window that adds
a 32nd variant or touches an existing `configuration.h`/`platformio.ini` will conflict on nearly
every hunk. Per `CLAUDE.md` PR-workflow rule ("cherry-pick the absolute minimum"), D6-01/02/03
should ship as their own dedicated PR, synced immediately before submission, not bundled with
unrelated fork work — and ideally proposed upstream rather than fork-only, since the duplication is
upstream's, not the fork's.

## Dead-function list (verified, zero references outside own definition)

| name               | file:line                           | evidence                                                                                                                                                                  |
| ------------------ | ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `init_scan`        | `src/spectral_scan.cpp:190`         | 4-line stub, name mismatch vs. header's `sx126x_spectral_init_scan`; zero callers                                                                                         |
| `direction_parse`  | `src/nrf52/nrf52_main.cpp:2665`     | zero callers; only writer of `direction_E_W`/`direction_S_N` (also zero readers)                                                                                          |
| `LilyGo_logo`      | `src/nrf52/nrf52_functions.cpp:103` | zero callers; sibling `Batterie_Vide_logo`/`MeshCom_Image` in same file are called                                                                                        |
| `Veille_logo`      | `src/nrf52/nrf52_functions.cpp:112` | zero callers                                                                                                                                                              |
| `ble_log_settings` | `src/nrf52/nrf52_flash.cpp:434`     | zero callers; not behind `DO_DEBUG`/`INSTRUMENT_ENABLED` (checked — plain `#ifdef NRF52_SERIES` whole-file wrap, not a diagnostics gate, so not an INS-01 false positive) |
| `WZ_L76Kreset`     | `src/gps_functions.cpp:1358-1360`   | entire body already inside `/* */` block — dead comment text, not compiled code                                                                                           |

82 other candidates from the initial mechanical scan (functions with ≤1 non-definition occurrence
tree-wide) were **rejected as false positives** on manual verification — each has exactly one real
caller elsewhere in the tree (e.g. `boardInit`, `getSOFTSER`, `resetReasonName`,
`interruptHandle1/2/3`, `audio_info`, the whole `api_functions.cpp`/`web_functions.cpp` batch) and a
naive "≤1 reference" threshold conflated "1 real caller" with "0 callers". Only strict
zero-reference names are listed above.

## Never-defined macros (tested via `#ifdef`/`#if defined()`, no `#define` and no `-D` anywhere in `src/`, `variants/`, `platformio.ini`)

| macro                                             | file:line tested                                                                                                                                     | notes                                                                                                                                                                                                                                |
| ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `ENABLE_TBEAM`                                    | `src/configuration_global.h:218`                                                                                                                     | confirmed by S1: dead branch (lines 218-224), never set as `-D` in any variant env, developer-only override                                                                                                                          |
| `ENABLE_SBUFFER`                                  | `src/configuration_global.h:201`                                                                                                                     | paired with `ENABLE_XML` in the same `#if`; `ENABLE_XML` **is** set (E22_XML), so the branch isn't fully dead, but `ENABLE_SBUFFER` alone never triggers it — same class of finding as ENABLE_TBEAM                                  |
| `BOARD_E220`                                      | `src/lora_functions.{cpp,h}`, `src/esp32/esp32_main.cpp` (9 sites), `src/esp32/esp32_sleep.cpp:46`                                                   | never a `-D` flag in any `variants/*/platformio.ini`; likely a planned-but-unshipped E220 module variant (dev/manual-override macro, same pattern)                                                                                   |
| `BOARD_HELTEC_V2`                                 | `src/onebutton_functions.cpp:260`                                                                                                                    | never defined; note the shipped board is `BOARD_HELTEC` (no V2 suffix, `heltec_wifi_lora_32_V2` env sets plain `BOARD_HELTEC`) — dead `#elif` arm                                                                                    |
| `GPS_SOFTWARE_SERIAL`                             | `src/gps_functions.cpp:62`                                                                                                                           | dev-only alternate GPS wiring path, never shipped                                                                                                                                                                                    |
| `HEAP_TEST`                                       | `src/command_functions.cpp:84,1174`, `src/mheard_functions.cpp:430`, `src/t-deck/lv_obj_functions.cpp:2795,2847`, `src/t-deck/tdeck_helpers.cpp:182` | diagnostics toggle, never wired to a `-D` — same shape as the INS-01 lesson (a field-diagnostic macro that's compiled out everywhere); do **not** delete the guarded code, only note it's currently unreachable in any shipped build |
| `LORA_ISR_DEBUG`                                  | `src/lora_functions.cpp` (5 sites)                                                                                                                   | diagnostics toggle, never wired — same INS-01 caution as above                                                                                                                                                                       |
| `MC_TEST_HOOKS`                                   | `src/extudp_functions.cpp:361`                                                                                                                       | comment at `capture_functions.h:13` and `lora_functions.cpp:452` explicitly documents this was a real, formerly-wired flag now orphaned — historical, not accidental                                                                 |
| `DISABLE_TLS_CONSOLE`                             | `src/command_functions.cpp:5951`                                                                                                                     | never set; net-console TLS toggle stays always-on in every shipped build                                                                                                                                                             |
| `BENCH_BLE_ADV_LATE`                              | `src/esp32/esp32_main.cpp:1802,1945`                                                                                                                 | bench-only macro, never a shipped `-D`, documented inline as a diagnostic print label                                                                                                                                                |
| `EXTERNAL_RADIO_PASSWORD` / `EXTERNAL_RADIO_PORT` | `src/esp32/external_radio_glue.cpp:47,138,145`                                                                                                       | explicitly documented ("from untracked overlay only" / "from the build overlay") — intentionally never in tracked `platformio.ini`, not a defect                                                                                     |
| `SW_VERSION_1` / `_2` / `_3`                      | `src/nrf52/api_functions.cpp:20,25,30`                                                                                                               | API version macros, never set — the BLE API-version-reporting branch is currently dead in every shipped build                                                                                                                        |
| `T_DECK_SPIFFS`                                   | `src/t-deck/lv_obj_functions.cpp` (6 sites)                                                                                                          | alternate storage backend for T-Deck, never wired — SPIFFS path currently unreachable                                                                                                                                                |
| `SX1268_V3`                                       | `src/spectral_scan.{cpp,h}`                                                                                                                          | never set; the T3_S3/ttgo boards use `SX1262_V3`/`SX126X` etc., not this exact token                                                                                                                                                 |
| `SX126X_V3`                                       | `src/command_functions.cpp:847,2936`                                                                                                                 | never set; likely a typo/alias for `SX126X` — worth a targeted check, not proposed as an edit here (touches radio chip selection, out of scope for a dead-code-only audit)                                                           |

Not flagged (framework/compiler/library builtins, correctly excluded per the task's own list):
`__AVR__`, `__AVR_ATmega{1280,2560,328P}__`, `__cplusplus`, `__has_include`, `__IMXRT105{2,6}2__`,
`__INT_MAX__`, `__SAMD21G18A__`, `_VARIANT_ISP4520_`, `ARDUINO_ARCH_ESP32`, `ARDUINO_ARCH_RP2040`,
`ASYNCWEBSERVER_VERSION`, `BMP280_I2C_ADDRESS`, `CONFIG_IDF_TARGET_ESP32{,S2,S3}`, `ESP`,
`ESP_ARDUINO_VERSION_MAJOR`, `ESP32`, `ESP8266`, `ISP4520`, `NRF52_SERIES`, `NRF52840_XXAA`,
`TARGET_RP2040`, `XPOWERS_CHIP_AXP2101` — all either compiler-defined, IDF/Arduino-core-defined, or
defined inside a vendored library's own headers (out of this project's control). Also discarded:
`ladder` — false positive, matched `defined(ladder)`-looking text inside a prose comment
(`lora_functions.cpp:92`, `lora_functions.h:12`), not a real preprocessor token.

## Rejected

- **DRY-20 batt dual implementation** — per S4, both `batt_functions.cpp` and `batt_function_old.cpp`
  are live across disjoint board sets (12 vs 15 variants); not a dead-code candidate, explicitly
  excluded per prior-work digest.
- **`src/nrf52/WisBlock-API.h:233-362` vs `:469-598` (S6 rank 19, 78 lines × 2 copies)** —
  investigated and **not** duplication to remove. These are two separate struct definitions,
  `s_meshcom_settings` (line 178) and `s_meshcomcompat_settings` (line 413), i.e. the current flash
  layout and a legacy-compat layout kept for reading old on-flash settings during upgrade. Their
  field lists are near-identical by design (flash-format versioning). Touches the flash/wire
  contract — out of scope for D6, and must not be merged/deleted.
- **`src/esp32/esp32_flash.h:91-132` vs `WisBlock-API.h:260-302/496-538` (S6 rank 28/43)** — same
  category: ESP32/nRF52 flash-struct field lists, cross-referenced from S4's DRY-21/DRY-22 as
  already-tracked, still-open C06 items outside this audit's build-configuration scope; not
  reproposed here to avoid duplicate findings across audit tracks.
- **`ENABLE_TBEAM`'s dead branch in `configuration_global.h:218-224`** — flagged as informational
  (never-defined-macro list) but **not** proposed for deletion: S4 explicitly treats
  `ALT-33`/ring-size branches as a guarded, intentional 4-branch ladder with a compile-time
  completeness check; removing a branch outright risks silently mis-sizing buffers if `ENABLE_TBEAM`
  is ever re-enabled for dev testing. Leave as dead-but-documented.
- **`MC_TEST_HOOKS`** — not proposed for deletion despite being unreferenced by any `-D`: comments at
  `capture_functions.h:13` and `lora_functions.cpp:452` show this was a real, working flag that was
  deliberately retired in favor of `capture_functions.h`'s hex-dump path; the guarded code itself
  already documents its own supersession. No action needed beyond noting it.
- **`HEAP_TEST` / `LORA_ISR_DEBUG` guarded code blocks** — not proposed for deletion. Per the INS-01
  lesson (field diagnostics swept away by a guard-change), these are live, useful debug
  instrumentation that simply isn't wired to a shipped `-D` flag. Deleting the guarded bodies would
  destroy working diagnostics; only the _absence of a way to turn them on_ is worth flagging, not
  the code itself.
- **`src/Regexp.cpp`** — confirmed used only by native test envs (`native`, `native_aprs`, etc.), not
  in any hardware `src_filter`; already correctly identified as native-only in S1, no further action
  — it is not dead, it's test-only code, different category from D6's scope.
- **variants/{heltec_t114,t_echo,wiscore_rak4631}/{variant.cpp,variant.h,WVariant.h}** — investigated,
  not duplication: these are the only three variant dirs with custom nRF52 board-definition files
  (PlatformIO's stock `nordicnrf52` boards don't ship variant files for these three boards), so their
  presence is necessary, not copy-paste bloat. Not proposed for any change.

---

## Orchestrator gate notes

### Orchestrator gate notes (verified against leftover release builds of 2026-09-10 09:00-09:09, HEAD c51c5881)

- Toolchains: both ESP32 (platformio-build-esp32.py) and nRF52 (nrf5.py / platform.txt) build with -Os,
  -ffunction-sections -fdata-sections, -Wl,--gc-sections by default. Unreferenced functions/const data
  cost zero flash. Any "delete dead X saves flash" claim is void unless X is referenced.
- D4-05 (delete Font_Mono_Bold_20.c in t-deck-pro): nm on .pio/build/t_deck_pro/firmware.elf shows only
  Font_Mono_Bold_14/15/19 linked. 16/17/18/20 are already dropped by gc-sections -> source hygiene only, 0 B flash.
- ram-opti item 10 (U8g2 fonts "in .data"): on E22_XML-DevKitC u8g2_font_10x20_mf sits at 0x3f41e824 and
  u8g2_font_6x10_mf at 0x3f41f886, inside .flash.rodata (0x3f400020 + 361804). nm prints 'D' because the
  section is writable-flagged, but it is flash-mapped drom, not DRAM. Item 10 is VOID on classic ESP32.
  (S3 check pending: 0x3c... addresses are drom on S3.)
- S1 claim "batt_function_old/test_inject/spectral_scan not compiled" is wrong (filters are +<*>); guarded internally.
- Measured static RAM (.data+.bss): classic E22 113,112 / E22_XML 123,416 / tbeam 114,264 / V3 116,244 /
  t_deck 135,156 / t_deck_pro 189,664 / rak4631 88,844 / t_echo 92,676. IRAM classic tbeam 131,051 of 131,072.
- Top DRAM symbols E22_XML (B): BLEtoPhoneBuff 6100, BLEComToPhoneBuff 6100, ringBufferUDPout 5500,
  ringbufferRAWLoraRX 5200, ringBuffer 5200, g_cnxMgr 3800 (IDF), mheardBuffer 3000, mheardPathBuffer1 2600,
  vflash_mem 2048, meshcom_settings 2032, packet$9066 1460 (static local), pageLastTextLong2 1200,
  pageLastText 1050, externQueue 1028, buf$11788 1024, web_header_collect 1024, strUNIT/strPARM_ID/strPARM 800 each,
  strText 600, msg_text 600, HardWare 576, RcvBuffer 510, pageLastLine 504, mheardPathCalls 500.
- R3-01 adc_chars[sizeof(...)] CONFIRMED: batt_function_old.cpp:198, ttgo_tbeam ELF .bss adc_chars = 0x510 (1296 B).
  NOT present in E22_XML-DevKitC ELF (that env uses USE_NEW_BATT) -> R3's "including E22_XML" claim is wrong;
  the win is on the batt_function_old boards (T-Beam family etc.), not on the DRAM-cliff E22_XML.
- D1-09 CONFIRMED: nrf52_main.cpp:2069-2073 sendUDP() only in the getUDP()==1 (no packet) branch; :2104 recovery under if(!neth.hasIPaddress). nrf52_ble.cpp:296 setFixedLen(sizeof(s_meshcom_settings)+1).
- D2 defects CONFIRMED: command_functions.cpp:3959 and :4027 both handle "setowndns " (second unreachable);
  :3245 "softser app" precedes :3254 "softser app0" (prefix match -> app0 shadowed);
  :4696-4708 specstep writes node_specsamples, :4717-4729 specsamples writes node_specstep (swapped members).
- R4-01 CONFIRMED: t_deck_pro ELF .bss work_mem_int$3696 = 0xc000 (49,152 B); root config/lv_conf.h has LV_MEM_CUSTOM 0,
  while src/t-deck/lv_conf.h:49-67 routes LVGL to ps_malloc. Largest single RAM item of the audit (S3 board with PSRAM).
- R4-02/03 on E22-DevKitC ELF: u8g2_1 + u8g2_2 objects 188 B each, pageLastTextLong2 1200 + pageLastText 1050 +
  pageLastLine 504 + pageLastTextLong1 150 + pageLastLineAnz 24 = 2,928 B page ring. The claimed extra 1 KB U8g2 frame
  buffer is not identifiable by name in nm (library-internal static) -> count 3.3 kB confirmed, 4.3 kB unverified.
- Stale git worktrees under .claude/worktrees/ (6 dirs) are NOT part of the audit scope; exclude from any find/grep.
- D5-01 CONFIRMED by source: src/safeboot/ota.h:1 `unsigned char ota_html[] = {` (no const) -> .data -> DRAM copy at boot
  in the safeboot image (~24 kB). No safeboot ELF in .pio to measure; safeboot is a separate small image, so this
  does not relieve the main-firmware cliffs.
- D5 corrected a brief premise: Regexp.cpp + regex_functions.cpp ARE compiled into hardware builds (checkRegexCall on all targets).
