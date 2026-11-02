# Size-Review Teil A/B: HW-Gating und Compiler/Lib-Flags (fork-dev f0a81c8)

**Methode.** Statisch gelesen. Das Haiku-Explore-Agent-Tool stand nicht zur Verfügung, ich habe selbst gelesen. Zusätzlich habe ich V3 (S3), T-Deck (S3) und Heltec T114 (nRF52) ohne `pio` mit den lokalen Toolchains nachgebaut (Flags aus den PIO-Buildskripten, Tool-Skripte in `scratchpad/m/`) und Delta-Linkläufe gemessen. Abweichung zu den Release-Assets (v4.40a.10.25): V3 1 895 157 B (PIO-Metrik) gegen 1 896 832 B `.bin`, T-Deck 2 530 637 gegen 2 519 760, T114 650 740 gegen 650 496 (UF2/2). Die Zahlen unten sind Bytes Flash (text+rodata+iram+data), gemessen am genannten Env. Auf klassische ESP32 (LX6) übertragen sie sich nur näherungsweise (keine LX6-Toolchain lokal). Alte `tools/resource_baseline.json` ist vom 09-11 und um ca. 30 % veraltet.

## 1. Matrix Env × Feature (HW vorhanden → kompiliert)

| Env (Gruppe) | OLED U8g2 | anderes Display | GPS | ETH | Sensoren (von 9) | Besonderes |
|---|---|---|---|---|---|---|
| E22-DevKitC, E22_1262, esp32-loraprs-e22/ra01 (classic) | nein → **ja** | – | optional → ja | – | 9 | |
| E22_XML (classic) | nein → **ja** | – | optional → ja | – | 9 | MC_DIAG=0, NET_CONSOLE aus, XML, SOFTSER |
| E22_1262_S3, E22_1268_S3 | nein → **ja** | – | optional → ja | – | 9 | |
| heltec V2, ttgo-lora32-v21 | ja → ja | – | optional → ja | – | 9 | |
| ttgo_tbeam, _SX1262, _SX1268 | ja → ja | – | ja → ja | – | 9 | PMU; IRAM 21 B frei |
| ttgo_tbeam_supreme, T-Beam-1W | ja → ja | – | ja → ja | – | 9 | PMU bzw. SD |
| LilyGo_T3_S3 | ja → ja | – | optional → ja | – | 9 | |
| LilyGo_T_Connect_Pro | nein → nein | Arduino_GFX ST7796 | opt. → ja | ja → ja | 9 | |
| T-ETH-ELITE | nein → **ja** | – | opt. → ja | ja → ja | 1 | |
| heltec V3, V4, wireless_stick | ja → ja | – | optional → ja | – | 8 | INA226 aus |
| heltec_wireless_tracker | nein → nein | TFT_eSPI ST7735 | ja → ja | – | 8 | |
| vision-master-e213, wireless-paper | nein → nein | E-Ink (Displays/*) | WP aus | – | 0 | ohne `esp32.build_flags` |
| vision-master-e290 | nein → nein | E-Ink | opt. → ja | – | 8 | SOFTSER; ohne `esp32.build_flags` |
| t_deck, t_deck_plus | nein → nein | TFT_eSPI + LVGL, Audio, SD | ja → ja | – | 2 | SOFTSER |
| t_deck_pro | nein → nein | E-Ink + LVGL, GxEPD2, TinyGSM | ja → ja | – | 0 | weder `esp32.` noch `common.build_flags` |
| wiscore_rak4631 | opt. → ja | – | RAK12500 → ENABLE_RAK_GPS | ja → ja | 8 | |
| heltec_t114 | nein → nein | Adafruit ST7735 | ja → ja | **nein → ja** | 0 | erbt `-D BOARD_RAK4630` |
| t_echo | nein → nein | E-Ink (GxEPD) | ja → ja | **nein → ja** | 2 | erbt `-D BOARD_RAK4630` |
| esp32-safeboot, -S3-safeboot | – | – | – | – | – | 713 776 B von 720 896 B erlaubt (7,1 KB Luft) |

Für alle ESP32-Apps kompiliert: KISS-TCP, mDNS, TLS/Auto-Update, Netzkonsole (außer E22_XML), MC_DIAG/INJECT (außer E22_XML), alle Sensor-/Radio-/BLE-Pfade. Der Radiochip ist pro Env Compile-Zeit (kein Laufzeit-Mix). Zur Laufzeit gewählt, aber komplett gelinkt: SSD1306 und SH1106 (zwei Vollpuffer-Objekte, `loop_functions.cpp:616-659`), L76K und u-blox, alle Sensorpfade. Das Hardware-Gating ist insgesamt weit fortgeschritten (HAS_*, `ENABLE_*_DISABLED`, MC_HAS_U8G2, build_src_filter). Die Lücken stehen unten.

## 2. Befunde

Konfidenz: H = Delta gemessen, M = Objektgrößen gemessen oder Schätzung aus Map, N = Schätzung.

| ID | Envs | Stelle | Was unnötig gelinkt wird | Ersparnis | Gating | Risiko | Konf. |
|---|---|---|---|---|---|---|---|
| SIZE-05 | heltec_t114, t_echo | `nrf52_main.cpp:25-28,108,2663-2707`, `gateway_service_nrf52.cpp`, `nrf_eth.cpp`, `web_functions/*`; Ursache `platformio.ini:122` (`-D BOARD_RAK4630` geerbt) | Ethernet (W5100S) und die komplette Web-Oberfläche, die nur über Ethernet erreichbar ist. T114-Map: web_functions.o 112,8 K, web_rm_page 47,4 K, web_setup 7,3 K, rm_handlers 2,6 K, nrf_eth 7,3 K, RAK13800-Lib 9,9 K | bis ca. **187 KB je Env** (29 % von 651 KB, Obergrenze) | neues `HAS_ETHERNET` auch für nRF52, nur in `wiscore_rak4631/configuration.h` | mittel/hoch: viele Aufrufstellen; `heltec_t114/configuration.h:49-50` zeigt Bastel-Umbau mit ETH → Betreiber entscheidet | M |
| SIZE-01 | alle 27 ESP32-Apps | `platformio.ini:1631` `[esp32] build_flags` | C++-Exceptions-Tabellen/Unwinder (`-fexceptions` im Framework). Kein `try/catch/throw` in src/lib (grep) | V3 **110,2 KB** (5,8 %), T-Deck **136 KB** (5,4 %) | `build_unflags = -fexceptions`, `build_flags = -fno-exceptions` | niedrig/mittel: ungefangene Exceptions enden wie bisher in `abort()`. In ttgo_tbeam* (eigenes `build_unflags`) und in den Envs ohne `${esp32.build_flags}` explizit ergänzen. Alle 30 Envs übersetzen, OOM-Pfad auf Bench | H |
| SIZE-02 | alle ESP32 | `platformio.ini:1631` | `log_e`-Strings und -Aufrufe in Core und Arduino-Libs | V3 **35,7 KB** | `-DCORE_DEBUG_LEVEL=0` (t_deck*, t5 setzen 1, t_deck_pro schon 0) | niedrig: Arduino-`[E][...]`-Zeilen fehlen in Feldlogs | H |
| SIZE-03 | alle ESP32 | `[esp32]` | Canary-Code in jeder Funktion mit char-Puffer | V3 **21,8 KB** | `build_unflags = -fstack-protector` | Sicherheitsabwägung (RX-Parser mit char[]): Betreiber | H |
| (01-03 zusammen) | | | | V3 **160,2 KB** (8,5 %) | | | H |
| SIZE-04 | alle ESP32 | `esp32/fw_update_net.cpp:44` (`FWNET_STUB`), `rm_au_none.cpp:4` | TLS, HTTPClient, mbedtls für Auto-Update | V3 **161,3 KB** (8,5 %) | vorhandener Messschalter `FWNET_STUB`, bräuchte produktive Fassung (z. B. `MC_NO_AU`) | hoch: Produktfunktion | H |
| SIZE-06 | alle mit 9 Sensoren (Matrix) | `configuration_default.h:123-161` | BME680, BMP3XX, BMx280, CCS811, AHT, SHT, MCP23017+BusIO, RTC (+ Loop-/Web-/Command-Pfade) | V3 **38,2 KB** für 8 Sensoren, im Mittel 4,8 KB je Sensor | vorhandenes `ENABLE_<X>_DISABLED` je Variante, kein Code nötig | Feature-Verlust für DIY-Nutzer, braucht Feldnutzungsdaten | H |
| SIZE-07a | t_deck, t_deck_plus | `variants/t_deck*/lv_conf.h:439-530,618` | ungenutzte LVGL-Widgets (Theme referenziert die Klassen): arc, chart, colorwheel, meter, roller, calendar, spinbox, menu, led, keyboard, tileview, … (Nutzung per grep in `src/t-deck` = 0) | **38,1 KB** | `LV_USE_ARC/ANIMIMG/CALENDAR/CHART/COLORWHEEL/IMGBTN/KEYBOARD/LED/MENU/METER/MSGBOX/ROLLER/SPAN/SPINBOX/SPINNER/TILEVIEW/WIN/GIF 0` | niedrig | H |
| SIZE-07b | t_deck, t_deck_plus | `lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h:34-43` | TFT_eSPI-Fonts 2/4/6/7/8, GFXFF, SMOOTH_FONT (T-Deck nutzt TFT_eSPI nur als Treiber) | **18,6 KB** | eigene Setup via `-DUSER_SETUP_LOADED` mit nur `LOAD_GLCD` | niedrig | H |
| SIZE-07c | t_deck, t_deck_plus | `lib/ESP32-audioI2S/src` | AAC-Decoder 68,1 K + FLAC 9,2 K, Webradio-Teile; `data/` enthält nur mp3 | bis ca. **77 KB** | Lib hat keinen Schalter, `#ifdef` in der vendored Lib nötig | mittel: Nutzer-Sounds auf SD | M |
| SIZE-08 | vision-master-e213/e290, wireless-paper, t_deck_pro | `variants/*/platformio.ini` (`build_flags` ohne `${esp32.build_flags}`) | NimBLE mit Central/Observer-Rolle, 3 Verbindungen | **8,9 KB** + 0,9 KB RAM | `${esp32.build_flags}` bzw. die NimBLE-Zeilen ergänzen | niedrig/mittel (Verhalten MAX_CONNECTIONS 3→1) | H |
| SIZE-09 | Envs ohne GPS-HW (Betreiber nennt sie) | `loop_functions.cpp:3259,6227` | GPS-Stack (gps_functions, TinyGPS++, UART-Setup) | V3 **24,5 KB** | `ENABLE_GPS_DISABLED`; Blocker: `gps` ist ohne `ENABLE_GPS` nicht deklariert, `loop_functions.cpp:3259` bricht den Compile ab (extern-Deklaration herausziehen) | niedrig; GPS_RX_PIN steht in 29 Varianten auch ohne Modul, also nicht ableitbar | H |
| SIZE-12 | E22 (3), loraprs (2), E22_S3 (2), T-ETH-ELITE | `configuration_global.h:226-234` (MC_HAS_U8G2) | U8g2-Lib, 2 Treiber, OLED-Seiten, 2 Fonts | ca. **19,8 KB** (Symbolsumme, E22_S3) | `MC_HAS_U8G2 0` für BOARD_E22*, BOARD_T_ETH_ELITE; Blocker `loop_functions.cpp:1361` (`#else u8g2->`) | mittel: DIY-OLED | M |
| SIZE-14 | wiscore_rak4631, heltec_t114, t_echo | `platformio.ini` nrf52_base | TinyUSB MSC/HID/MIDI/Vendor/Video/WebUSB (nur CDC gebraucht) | T114 **11,0 KB** | `-DCFG_TUD_MSC=0 -DCFG_TUD_HID=0 -DCFG_TUD_MIDI=0 -DCFG_TUD_VENDOR=0 -DCFG_TUD_VIDEO=0 -DCFG_TUD_VIDEO_STREAMING=0` | niedrig (UF2-Update macht der Bootloader); Bench | H |
| SIZE-11 | alle ESP32 | `web_functions.cpp:274,283,337`, `web_commonServer.h:12` | libmdns + ESPmDNS | ca. **23,8 KB** | neues `MC_NO_MDNS` | Verlust `<call>.local` | M |
| SIZE-10 | alle ESP32 | `configuration_global.h:279` | KISS-TCP | V3 **10,7 KB** | vorhandenes `-DDISABLE_KISS_TCP` (kein Env setzt es) | Feature für LoRa-APRS-Boards | H |
| SIZE-13 | heltec_t114, t_echo | `nrf52_main.cpp:172-173` | `SFE_UBLOX_GNSS myGPS;` unbedingt, nur unter `ENABLE_RAK_GPS` genutzt | 2,2 KB + 392 B RAM | `#if defined(ENABLE_RAK_GPS)` | sehr niedrig | M |
| SIZE-15 | 3× nRF52 | `variants/*/platformio.ini:25-28` | `lib_archive = no` | T114 **4,3 KB** bei `yes` | `lib_archive` entfernen | mittel: ISR-Überschreibungen aus Libs, Bench | H |
| SIZE-16 | alle | `configuration_global.h:264` | MC_DIAG/INJECT-Pfade | V3 3,8 KB, T114 2,8 KB | `-DMC_DIAG=0 -DMC_INJECT_HOOKS=0` | Betreiberentscheid 09-16 (Default an) | H |
| SIZE-17 | ESP32 | `net_console.cpp:22` | Netzkonsole | V3 4,9 KB | vorhandenes `DISABLE_NET_CONSOLE` | Feature | H |

Nicht aufgeführt, weil null: GPS-Libs, SparkFun u-blox, ESP32Ping, `Heltec_ESP32_LoRa_v3` (6 Envs, nirgends eingebunden) landen auf ESP32 nicht im Image (Archiv + gc). Das ist reine Build-Zeit-Hygiene, ebenso `#include <Adafruit_SSD1306.h>` in `mcu811.cpp:16` und `SparkFun`-Include in `esp32_pmu.cpp:13`.

## 3. Flag-Hebel

| Flag | aktuell | Vorschlag | Effekt (gemessen) | Risiko |
|---|---|---|---|---|
| Optimierung | ESP32 `-Os` (Framework), nRF52 `-Os` statt `-Ofast` (rak/t114/t_echo), safeboot `-Oz` | beibehalten | – | – |
| LTO | keins (`-fno-lto` im ESP32-Link) | nicht anfassen. nRF52 (GCC 7.2.1) scheitert im Assembler (`offset out of range`), gemessen | – | – |
| `-ffunction/-fdata-sections`, `--gc-sections` | überall an | – | – | – |
| `-fexceptions` | an (ESP32, 2.x-Framework `platformio-build-esp32*.py`); nRF52 schon `-fno-exceptions` | aus | −110 KB V3, −136 KB T-Deck | niedrig/mittel |
| `-fstack-protector` | an (ESP32) | aus | −21,8 KB | Security |
| `CORE_DEBUG_LEVEL` | Default ERROR (sdkconfig), T-Deck/T5 =1 | 0 | −35,7 KB | niedrig |
| `-fno-threadsafe-statics` | ESP32 aus (nRF52 an) | nicht lohnend | −1,2 KB | Task-Races |
| `--param max-inline-insns-single=500` (nRF52-Core) | an | egal | 0 B gemessen | – |
| `ARDUINOJSON_USE_DOUBLE=0` | nicht gesetzt | nein | −3,2 KB, aber lat/lon nur Float | Präzision |
| `RADIOLIB_CHECK_PARAMS` | 1 | nein | −0,4 KB | Bereichsprüfung weg |
| `RADIOLIB_EXCLUDE_*` | 24 gesetzt (t_deck_pro: keine) | – | wirkungslos wegen gc | – |
| NimBLE-`CONFIG_BT_NIMBLE_*` | nur in `[esp32]` | auf 4 Envs ergänzen | −8,9 KB | niedrig |
| TinyUSB `CFG_TUD_*` (nRF52) | alle Klassen an | nur CDC | −11,0 KB T114 | niedrig |
| `lv_conf.h`, TFT_eSPI-Setup | alles an | siehe SIZE-07 | −38 / −18,6 KB | niedrig |
| `lib_archive` (nRF52) | no | yes | −4,3 KB | ISR |
| newlib printf/scanf | Float-Varianten plus Int-Varianten (vfiprintf, svfiprintf, svfiscanf = ca. 30 KB) | Alias-Trick bringt nur −0,4 KB, nicht umsetzbar | – | – |
| Prebuilt SDK (LWIP_IPV6=y, ESP_COREDUMP, BTDM beim classic ESP32) | fix in Arduino 2.0.x | nur über pioarduino/`custom_sdkconfig`; nicht gemessen | Größenordnung 100+ KB (N) | große Migration |
| ARDUINO_USB_CDC_ON_BOOT, `-std`-Doppelung `c++17/gnu++17` | – | – | ≈0 | – |

safeboot (tasmota, Arduino 3.1.10): lokal nicht installiert, nicht gemessen. `-D CORE_DEBUG_LEVEL=0` und `-fno-exceptions` wären die ersten Kandidaten (N), weil nur 7,1 KB Luft bleiben.

Außerhalb des Auftrags, aber größter Einzelposten: String-Literale (V3, vor gc) web_functions 91,6 KB, web_rm_page 45,4 KB, command_functions 23,4 KB = 160 KB (8,5 %). Ein anderer Reviewer sollte gzip-Einbettung prüfen.

## 4. Gezielt in der Map suchen

- Exceptions: Sections `.eh_frame*`, `.gcc_except_table`, Symbole `__gxx_personality_v0`, `_Unwind_*`, `__cxa_*`; je Env vorher/nachher.
- Canaries: `__stack_chk_fail`, `__stack_chk_guard`; Log-Strings `[E][` und `log_printf`.
- TLS/AU: `fwNet*`, `HTTPClient::`, `WiFiClientSecure::`, `mbedtls_ssl_*`, `mbedtls_x509_*`, `mbedtls_ecp_*`, `FW_UPDATE_ROOTS_PEM`. mDNS: `mdns_*`, `MDNSResponder`. KISS: `kiss*`, `KissAckEntry`.
- Sensoren: `Adafruit_BME680::`, `Adafruit_BMP3XX::`, `BMx280MI::`, `Adafruit_CCS811::`, `Adafruit_AHTX0::`, `SHT`, `INA226::`, `Adafruit_MCP23X17`, `RTC_*`, `DHT_Unified`, `OneWire::`.
- GPS: `TinyGPSPlus::`, `SFE_UBLOX_GNSS::`, `myGPS`, `SetupL76K`, `SetupUBLOX`.
- OLED: `u8g2_*`, `u8x8_d_ssd1306*`, `u8x8_d_sh1106*`, `u8g2_font_10x20_mf`, `u8g2_1`, `u8g2_2`.
- LVGL: `lv_chart_class`, `lv_arc_class`, `lv_colorwheel_class`, `lv_meter_class`, `lv_roller_class`, `lv_calendar*`, `lv_gif*`, `lodepng*`, `lv_font_montserrat_16`. TFT_eSPI: `chrtbl_f16`, `chrtbl_f32`, `chr_f64`, `widtbl_f72`, `TFT_eSPI::drawChar`, `loadFont`.
- Audio: `Audio::`, `AACDecode`, `FLACDecode`, `MP3Decode`, `aac_decoder`. FS: `esp_spiffs_*`, `SPIFFSFS`.
- NimBLE: `ble_gattc_*`, `ble_gap_disc*`, `NimBLEClient`, `NimBLEScan`, `ble_sm_*`.
- libc: `lib_a-vfprintf/svfprintf/vfiprintf/svfiprintf/svfscanf/svfiscanf/strtod/dtoa/strftime`.
- nRF52: `NrfETH::*`, `work_webpage`, `sub_page_*`, `web_client_html`, `Ethernet*`, `socket`, `W5100`, `myGPS`, `lfs_*`, TinyUSB `class_video/msc/hid/midi/vendor`, `commandAction` (25,8 KB).
- Größte Einzelobjekte src (V3 nach gc): web_functions 149 K, command_functions 80 K, web_rm_page 48 K, loop_functions 38 K, esp32_main 34 K, lora_functions 25 K, nbr_matrix 22 K, fw_update_net 21 K.
