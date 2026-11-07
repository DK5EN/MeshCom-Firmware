# GPIO pin map: verified input for `src/pin_policy.h`

Campaign GPIO-01, wave W0. Analysis only, no firmware change. Input for W1 (`src/pin_policy.h`), design in [concept-gpio-pins-20261106.md](concept-gpio-pins-20261106.md). Status 2026-11-07, tree `fork-dev`.

**How to read the evidence tags**

- **repo:** read in this tree (`file:line`).
- **framework:** read in `~/.platformio/packages/framework-arduinoespressif32` (version 3.20017.241212, the package of platform `espressif32@^6.13`). `t_deck` and `t_deck_plus` build on platform 6.6.0 (framework 3.20014.231204), `t_deck_pro` and `t5_epaper` on 6.5.0; those packages were **not** opened, the pin facts below were read in 3.20017 only.
- **data sheet, not checked here:** from general knowledge of the chip or board, not confirmed in this tree. Treat as a review item, not a fact.

**Env count:** 31 ESP32 envs (10 classic ESP32, 21 ESP32-S3), safeboot excluded. Three nRF52 envs (RAK4631, T114, T-Echo) are in section 4 only.

## 1. Chip rules

Families used by any env: classic ESP32 (10 envs), ESP32-S3 (21 envs, quad or octal PSRAM by board), nRF52840 (3 envs). No C3, S2 or C6 env (`boards/*.json` and the platform board files: `mcu` is `esp32`, `esp32s3` or `nrf52840`).

### Classic ESP32

| Rule                           | Pins                  | Evidence                                                                                                                                                                                    |
| ------------------------------ | --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SPI flash                      | 6-11                  | repo `src/wx_pins.h:19`, `test_wx_pins`; framework `io_mux_reg.h:161-203` (GPIO6 = SD_CLK, GPIO11 = SD_CMD). Endpoints confirmed, 7-10 follow from the range                                |
| Input-only                     | 34-39                 | framework `soc_caps.h:133` (`SOC_GPIO_VALID_OUTPUT_GPIO_MASK` clears BIT34-39)                                                                                                              |
| Not present                    | 24, 28-31             | framework `soc_caps.h:131` (`SOC_GPIO_VALID_GPIO_MASK`). The repo comment `command_functions.cpp:796` also names 20; the framework mask keeps 20 valid, so 20 is "not bonded" by data sheet |
| Not bonded on common modules   | 20, 37, 38            | data sheet, not checked here. The T-Beam uses 38 (button) and the module has it, so 37/38 are module-dependent                                                                              |
| Strapping                      | 0, 2, 5, 12, 15       | data sheet, not checked here. GPIO12 (MTDI) selects the flash voltage: a high level at reset can stop the boot                                                                              |
| Console UART0                  | TX 1, RX 3            | framework `variants/tbeam`, `esp32`, `ttgo-lora32-v21new`, `heltec_wifi_lora_32_V2` `pins_arduino.h` (`TX = 1`, `RX = 3`)                                                                   |
| PSRAM on WROVER modules        | 16, 17                | data sheet, not checked here. Repo: `variants/ttgo_tbeam/platformio.ini:25-46` says PSRAM is present on T-Beam V1.2 and the fleet is mixed                                                  |
| ADC2 unusable while Wi-Fi runs | 0, 2, 4, 12-15, 25-27 | data sheet, not checked here. Matters for analog assignments, not for the policy classes                                                                                                    |

### ESP32-S3

| Rule                      | Pins               | Evidence                                                                                                                                                                                                                         |
| ------------------------- | ------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Highest pin, holes        | 0-48 without 22-25 | framework `esp32s3/.../soc_caps.h:113-114`; no input-only pin (`:115-116`)                                                                                                                                                       |
| Flash / PSRAM lines       | 26-32              | repo `src/wx_pins.h:20`; framework `esp32s3/.../io_mux_reg.h:117-123` (GPIO26 = SPICS1, 27-32 = SPIHD, SPIWP, SPICS0, SPICLK, SPIQ, SPID)                                                                                        |
| Octal PSRAM / flash       | 33-37              | repo `wx_pins.h:21`, `command_functions.cpp:790-794` (detect: `CONFIG_SPIRAM_MODE_OCT`); framework `esp32s3/qio_opi/include/sdkconfig.h:307` sets it, `qio_qspi` sets `CONFIG_SPIRAM_MODE_QUAD` instead                          |
| USB D-, D+                | 19, 20             | framework `esp32s3/.../io_mux_reg.h:166-167` (`USB_DM_GPIO_NUM`, `USB_DP_GPIO_NUM`)                                                                                                                                              |
| Strapping                 | 0, 3, 45, 46       | data sheet, not checked here (framework only has the `GPIO_STRAPPING` register, `gpio_reg.h:166-168`)                                                                                                                            |
| Console UART0             | TX 43, RX 44       | framework `variants/esp32s3`, `heltec_wifi_lora_32_V3` (`TX = 43`, `RX = 44`). Whether `Serial` is UART0 or USB-CDC follows `ARDUINO_USB_CDC_ON_BOOT` in the env flags (column below); Arduino core behaviour itself not checked |
| 47, 48 differential clock | 47, 48             | framework `io_mux_reg.h:138-139` names them `SPICLK_P/N`. On 1.8 V octal-PSRAM modules these pins sit in the VDD_SPI domain: data sheet, not checked here                                                                        |

**Octal PSRAM per env (board JSON `memory_type`, repo `boards/*.json`):** `qio_opi` for E22 S3 (2), T-ETH-ELITE, T-Beam-1W, T-Deck, T-Deck Plus, T5 ePaper; `T_Connect_Pro` sets `qio_opi` in its own ini (`LilyGo_T_Connect_Pro/platformio.ini:12`). Quad (`qio_qspi` or none): T3 S3, T-Beam Supreme, T-Deck Pro, all Heltec boards, Vision Master, Wireless Paper. For these 33-37 are ordinary pins; T-Deck Pro uses 33-37 as SPI and e-paper pins (`src/t-deck-pro/utilities.h:58-71`), which agrees. Its board JSON says `psram_type: opi` next to `memory_type: qio_qspi` (`boards/T-Deck-Pro.json:6,22`); the code evidence says quad.

**Console routing by env (`-DARDUINO_USB_CDC_ON_BOOT`, repo):** CDC on (UART0 pins 43/44 are free of the console): V4, Tracker, T-ETH-ELITE, T3 S3, T-Beam-1W, T-Connect-Pro, Supreme, T-Deck, T-Deck Plus, T-Deck Pro, T5, E290, E213. CDC off or unset (console on UART0 43/44, a CP210x or CH340 bridge): Heltec V3, Wireless Stick, Wireless Paper, E22 S3 (explicit `=0`). Classic boards: always UART0 1/3.

### nRF52840

`P0.00`/`P0.01` carry the 32 kHz crystal and `P0.18` is nRESET: repo comment `src/settings_sanitize.h:80-83` (the failure it describes, a hung FreeRTOS tick, was seen on hardware per that comment); the chip pin functions themselves are data sheet, not checked here. `P0.09`/`P0.10` are the NFC pads: data sheet, not checked here, not reserved today. Arduino pin = absolute port pin on all three envs (identity `g_ADigitalPinMap`, P1.xx = 32 + xx): repo `variants/wiscore_rak4631/variant.cpp:31-41`, identical in `heltec_t114` and `t_echo`; all three envs compile the RAK `variant.cpp` (`platformio.ini:98-104`).

## 2. Pins per env

Groups share a variant or a pin set; every line was read per board (configuration.h, build flags, framework variant file), not copied from a sibling. `-1` or an absent macro means "not wired / not defined". `(fw)` = comes from the framework variant, not from the repo config. Line references are in the board's `variants/<env>/configuration.h` unless stated.

### Classic ESP32

**ttgo_tbeam, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268** (board `ttgo-t-beam`, same variant `tbeam`; only the macro set differs)

- **Radio:** SX1276 (`ttgo_tbeam`): SCK 5, MISO 19, MOSI 27, CS 18, RST 23, DIO0/IRQ 26, DIO1 33, DIO2 32 (fw `tbeam/pins_arduino.h:7-15`; config `:25-28`). SX1262/SX1268: NSS 18, RESET 23, DIO1 33 (IRQ), DIO2 32 (BUSY), DIO0 26 (comment "no connect on the module"), `LORA_DIO3` empty (`ttgo_tbeam_SX1262:83`, `SX1268:68`). Same pins, different macro names (`SX1262X_*` versus `SX1268_*`).
- **SPI:** same pins, from the framework constants `SS 18, MOSI 27, MISO 19, SCK 5` (fw `:29-32`), `SPI.begin()` at `src/main.cpp:51`.
- **I2C:** SDA 21, SCL 22 (`SDA_PIN`/`I2C_SDA`), shared by OLED, PMU, sensors.
- **GPS:** RX 34, TX 12 (`:80-81`).
- **Display:** SSD1306 OLED on I2C, no own pins.
- **Console / button / LED:** UART0 1/3; button 38; `BOARD_LED` 4, active low; `OneWire_GPIO` 4 (the same pad as the LED, `ttgo_tbeam/configuration.h:47,52`); `ANALOG_PIN` 36.
- **PMU / other:** AXP192 selected by `XPOWERS_CHIP_AXP192` (`:37`) on the I2C bus; the code disables all PMU IRQs (`src/esp32/esp32_pmu.cpp:138`), so no PMU IRQ pin is claimed. The PMU IRQ line itself (GPIO35) and the GPS PPS line (GPIO37) are data sheet, not checked here. PSRAM pins 16/17 are not driven by the firmware (`-DBOARD_HAS_PSRAM` removed, `platformio.ini:25-46` of the variant).
- **Macro traps in the alias envs:** `BOARD_TBEAM` is defined only by `ttgo_tbeam`; the aliases define `BOARD_SX1262` / `BOARD_SX1268` instead (variant `platformio.ini:54`). `ttgo_tbeam_SX1262:76` and `SX1268:61` carry `BATTERY_PIN 1`, `RESET_OLED RST_OLED` and `VEXT_ENABLE Vext` copied from Heltec V3: GPIO1 is the console TX here, and `Vext`/`RST_OLED` do not exist in the `tbeam` framework variant.

**ttgo-lora32-v21** (board `ttgo-lora32-v21`, fw variant `ttgo-lora32-v21new`)

- **Radio (SX1276):** SCK 5, MISO 19, MOSI 27, CS 18, RST 23, IRQ/DIO0 26, `LORA_D1` 33, `LORA_D2` 32 (fw `pins_arduino.h:15-25`, `LORA_*` and `LORA_D1/D2`); config `:70-71`.
- **I2C / OLED:** 21/22 (`:77-78`), `OLED_RST` 16 (fw `:15`).
- **SD (fw only):** `SD_SCK` 14, `SD_MISO` 2, `SD_MOSI` 15, `SD_CS` 13 (fw `:28-31`). The config puts GPS on RX 13 / TX 15 (`:82-83`), the same pads as `SD_CS` and `SD_MOSI`.
- **Button / LED / other:** button 12 (strapping); LED 25; `BATTERY_PIN` 35; `OneWire_GPIO` 4.

**heltec_wifi_lora_32_V2** (board `heltec_wifi_lora_32_V2`)

- **Radio (SX1276):** CS = `SS` 18, SCK 5, MISO 19, MOSI 27, RST = `RST_LoRa` 14, DIO0 = `DIO0` 26, DIO1 = `DIO1` 35, DIO2 34 (fw; config `:47-50` maps `LORA_RST`, `LORA_DIO0/1`, `LORA_CS` to the runtime constants).
- **I2C:** sensors on SDA 4 / SCL 15 (`:52-53`, the OLED bus pins `SDA_OLED`/`SCL_OLED`); `RST_OLED` 16 (fw); `Vext` 21 (fw).
- **GPS:** RX 13, TX 12 (strapping). **Button** 0, **LED** 25, `BATTERY_PIN` 37, `OneWire_GPIO` 17.

**E22-DevKitC (SX1268), E22_1262-DevKitC (SX1262), E22_XML-DevKitC (SX1268, no net console)** (board `az-delivery-devkit-v4`, fw variant `esp32`)

- **Radio (E22):** CS 5, RST 27, BUSY 26 (named `LORA_DIO0`), DIO1 33, RXEN 14, TXEN 13 (`:62-84`). SPI default VSPI from the framework constants: SCK 18, MISO 19, MOSI 23 (`SS` 5).
- **I2C:** 21/22. **GPS:** RX 16, TX 17. **Button** 12 (strapping). **LED** 2 (strapping; `E22_XML` names it `LED`, not `BOARD_LED`). `OneWire_GPIO` 25. `BATTERY_PIN` 32. Soft-serial is enabled in `E22_XML` (`:18`).

**esp32-loraprs-e22 (SX1268), esp32-loraprs-ra01 (SX1276)** (same board)

- **Radio:** CS 5, RST 27, `LORA_DIO0` 14 (BUSY on the E22), DIO1 12 (strapping), E22 only: RXEN 32, TXEN 33. SPI 18/19/23 (framework constants).
- **I2C** 21/22, **GPS** 16/17, **button** 13, **LED** 2, `OneWire_GPIO` 25, `BATTERY_PIN` 36.

### ESP32-S3: Heltec family (board JSON `heltec_wifi_lora_32_V3`)

Radio and SPI are the same on all of them (fw `heltec_wifi_lora_32_V3/pins_arduino.h`: `SS` 8, `MOSI` 10, `MISO` 11, `SCK` 9): NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14.

**heltec_wifi_lora_32_V3**

- **Radio:** as above (`RF95_*`, `LORA_RESET`, `LORA_DIO1/2`, `SX1262X_*`, `:56-72`); `LORA_DIO0 -1`, `LORA_DIO3` empty (`:58,62`).
- **I2C:** two buses. OLED on `SDA_PIN` 17 / `SCL_PIN` 18 via `Wire1` (`:74-75`, `src/esp32/esp32_functions.cpp:207`); sensors on `I2C_SDA` 41 / `I2C_SCL` 42 (`:47-48`).
- **Display:** SSD1306 on 17/18, reset `RESET_OLED` = `RST_OLED` 21 (fw `:66`).
- **Power / ADC:** `Vext` 36, driven LOW at boot with a literal number (`src/batt_functions.cpp:497-499` (guard `BOARD_HELTEC_V3`, `BOARD_HELTEC_V4`, `BOARD_STICK_V3`), no macro in the config); `ADC_CTRL_PIN` 37 (`src/batt_function_old.cpp:44-64`, family `BATT_FAMILY_HELTEC_SWITCHED`); `BATTERY_PIN` 1.
- **GPS:** RX 47, TX 48 (`:94-95`, compile-time, board has no GPS). **Button** 0. **LED** 35 (`BOARD_LED`). `OneWire_GPIO -1`. Console UART0 43/44.

**heltec_wireless_stick**: as V3 (same `Vext` 36 / ADC 37 / RST_OLED 21 / LED 35 / I2C pairs), `BOARD_STICK_V3`; GPS 47/48 (`:98-99`); console UART0.

**heltec_wifi_lora_32_V4**: as V3 plus PA control `P_LORA_PA_POWER` 7, `P_LORA_PA_EN` 2, `P_LORA_PA_TX_EN` 46 (variant `platformio.ini` build flags); `ADC_CTRL_PIN` 37 (`:55`); `VEXT_ENABLE Vext` (`:50`); GPS RX 38, TX 39 (`:92-93`); CDC console, so 43/44 are not the console.

**heltec_wireless_tracker**: radio same pins via `RADIO_*_PIN` (`:57-63`); I2C 7 (SDA) / 6 (SCL) for both `I2C_*` and `SDA_PIN`/`SCL_PIN`; TFT ST7735 from build flags: MOSI 42, SCLK 41, CS 38, DC 40, RST 39, BL 21; GPS RX 33, TX 34; `VEXT_CTRL` 3 (GPS and TFT power), `ADC_CTRL` 2; `BATTERY_PIN` 1; button 0; no LED macro. CDC console.

### ESP32-S3: Vision Master and Wireless Paper (e-paper; no `esp32.build_flags`, so no `ENABLE_WIND`)

Radio on all three (config `PIN_LORA_*`): NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14.

| Env                                            | Display (header `src/Platforms/...`)                                                             | Other                                                                                                                                                                                                                    |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| vision-master-e290, vision-master-e290-otp     | DC 4, CS 3, BUSY 6, RST 5, SDI 1, CLK 2; `PIN_PCB_VEXT` 18 (`VisionMasterE290.h:20-37`)          | `VEXT_ENABLE_1` 18, `VEXT_ENABLE_2` 46 (`:73-74`); I2C 39/38; GPS 44/43, `GPS_SWITCH` 42; button 21 (+ `BUTTON_1` 0); `ANALOG_PIN` 0; `BATTERY_PIN` 7; no LED; the `ADC_CTRL_PIN 19` block is commented out (`:105-122`) |
| vision-master-e213, vision-master-e213-preview | DC 2, CS 5, BUSY 1, RST 3, SDI 6, CLK 4; `PIN_PCB_VEXT` 18 (`VisionMasterE213.h:27-41`)          | I2C 39/38; GPS 44/43, `GPS_SWITCH` 42; button 0; `BOARD_LED` 45 (strapping); `ADC_CTRL_PIN` 46; `BATTERY_PIN` 7; `ANALOG_PIN` 0                                                                                          |
| wireless-paper                                 | DC 5, CS 4, BUSY 7, RST 6, SDI 2, CLK 3; `PIN_PCB_VEXT` 45, active LOW (`WirelessPaper.h:19-36`) | I2C 41/42; GPS 47/48 "unused, no GPS" (`:83-84`); button 0; `BOARD_LED` 18; `ADC_CTRL_PIN` 19 (active LOW); `BATTERY_PIN` 20; console UART0                                                                              |

`DEFAULT_MISO 33` in the three platform headers is "arbitrary, not connected": it is not a claimed pin.

### ESP32-S3: DevKit based

**E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8** (octal PSRAM, console UART0)

- **Radio (E22):** NSS = `SS` 10, SCK 12, MISO 13, MOSI 11 (macros `:90-93`, equal to the framework constants), RXEN 4, TXEN 5, DIO1 6, BUSY 7, NRST 17.
- **I2C:** 8/9. **GPS:** TX 15, RX 16 (`:111-112`). **Button** 0, `BUTTON_EXT` 21. **LED:** NeoPixel `LED_PIN` 38 (`LED_PIXEL`, no `BOARD_LED`). `OneWire_GPIO` 47. `BATTERY_PIN` 1.

**T-ETH-ELITE_1262** (octal PSRAM, CDC console)

- **Radio:** SCK 10, MISO 9, MOSI 11, CS 40, IRQ 8, RST 46, BUSY 16 (`:62-72`); `SPI.begin(RADIO_SCK, ...)` at `src/main.cpp:46`.
- **Ethernet W5500:** CS 45, INT 14, RST 16, SCLK 48, MISO 47, MOSI 21 (`:54-59`, used by `src/esp32/esp32_eth.cpp:17-26`). `ETH_RST_PIN` 16 equals `RADIO_BUSY_PIN` 16 (see findings).
- **I2C:** 17/18. **GPS:** TX 42, RX 39. **Button** 0. **LED** 38. `ANALOG_PIN` 7, `BATTERY_PIN` 2, `OneWire_GPIO -1`.

### ESP32-S3: LilyGo

**LilyGo_T-Beam-1W** (octal PSRAM, CDC console)

- **Radio:** SPI SCK 13, MISO 12, MOSI 11 (`SPI_*`, `:38-40`); CS 15, RST 3, IRQ (DIO1) 1, BUSY 38, `RADIO_CTRL` 21, `RADIO_LDO_EN` 40 (`:48-53`). `SX1262_IRQ` expands to the undefined `RADIO_DIO1_PIN` (`:59`), unused.
- **SD:** CS 10 on the shared SPI (`SDCARD_CS`, `:43,101`).
- **I2C:** 8/9. **GPS:** RX 5, TX 6, PPS 7, EN 16 (`:25-28`). **Button** 17 (`BUTTON_PIN`) and 0 (`BUTTON0_PIN`, not a policy-visible macro name). **LED** 18. `ADC_PIN`/`BATTERY_PIN`/`ANALOG_PIN` 4. `NTC_PIN` 14, `FAN_CTRL` 41.

**LilyGo_T3_S3_V1_3** (quad PSRAM, CDC console)

- **Radio:** SCLK 5, MISO 3 (strapping pin), MOSI 6, CS 7, DIO1/IRQ 33, RST 8, BUSY 34 (`:30-38`). `SX1262_CTL` expands to the undefined `RADIO_CTRL` (`:46`), unused.
- **I2C:** SDA 18, SCL 17. **GPS:** RX 44, TX 43. **Buttons** 0 and 38. **LED** 4. `ADC_PIN`/`BATTERY_PIN`/`ANALOG_PIN` 1. OLED on I2C, no own pins.

**LilyGo_T_Connect_Pro** (octal PSRAM, CDC console; pins also in `pin_config.h`, included by `loop_functions.cpp:102`, `esp32_main.cpp:100`)

- **Radio:** SCLK 12, MISO 13, MOSI 11, CS 14, DIO1/IRQ 45, RST 42, BUSY 38 (`:44-52`).
- **Ethernet W5500 (same SPI):** CS 10, INT 9, RST 48 (`:28-33`).
- **TFT (build flags + `pin_config.h`):** MOSI 11, MISO 13, SCLK 12, CS 21, DC 41, RST 42, BL 46; touch RST 47, INT 3 (`pin_config.h:25-37`); `PIN_TFT_VDD_CTL` 3, `PIN_TFT_LEDA_CTL` 15 (`:58-60`).
- **I2C:** 39/40. **GPS:** RX 5, TX 4. **Button** 0. `RELAY_SWITCH` 8. `ADC_PIN`/`ANALOG_PIN` 4.

**ttgo_tbeam_supreme** (quad PSRAM, CDC console)

- **Radio:** NSS 10, RESET 5, DIO1 1, BUSY (`LORA_DIO2`) 4; `LORA_DIO0` and `LORA_DIO3` empty macros (`:59,63`). SPI: framework `esp32s3` defaults SCK 12, MISO 13, MOSI 11 via `SPI.begin()` (`src/main.cpp:51`; not in the exclusion list).
- **I2C:** bus 0 SDA 17 / SCL 18 (`SDA_PIN`/`SCL_PIN`, `I2C_*`); bus 1 `I2C1_SDA` 42 / `I2C1_SCL` 41 for the PMU (`PMU_USE_WIRE1`, `:77-79`).
- **GPS:** RX 9, TX 8, `GPS_WAKEUP` 7. **Button** 0. `BATTERY_PIN` 1. `OneWire_GPIO -1`. PMU by `XPOWERS_CHIP_AXP192` (`:24`). Other Supreme peripherals (SD, IMU, magnetometer, RTC, PMU IRQ): not defined in the repo, data sheet, not checked here.

### ESP32-S3: T-Deck family

**t_deck, t_deck_plus** (octal PSRAM, CDC console; `configuration.h` identical except `MODUL_HARDWARE` and the GPS fallback pins 43/44 on `t_deck`)

- **Radio:** CS 9, RST 17, BUSY (`LORA_DIO0`) 13, DIO1 45; `LORA_DIO2 32` (a flash pin, unused).
- **SPI shared by TFT, SD, radio:** MOSI 41, MISO 38, SCK 40 (`TDECK_SPI_*`, `SPI.begin` at `src/t-deck/tdeck_main.cpp:173`).
- **Display ST7789:** CS 12, DC 11, BL 42 (`TDECK_TFT_*`, `tft_setup.h`); `TDECK_BL_PIN` 42.
- **SD:** CS 39 (`TDECK_SDCARD_CS`).
- **I2C:** SDA 18, SCL 8. **GPS:** RX 44, TX 43.
- **Power / input:** `TDECK_POWERON` 10 (driven HIGH at boot, `tdeck_main.cpp:159-160`); boot/trackball click 0; trackball G01 3, G02 2, G03 15, G04 1 (`:177-185`); touch INT 16; keyboard INT 46.
- **Audio:** I2S BCLK 7, LRC 5, DOUT 6; ES7210 mic MCLK 48, LRCK 21, SCK 47, DIN 14.
- **Other:** `BATTERY_PIN` 4; `LED 2` (macro `LED`, collides with trackball G02); `OneWire_GPIO 25` (not an S3 pin, see findings).

**t_deck_pro** (quad PSRAM, CDC console; no `ENABLE_WIND`). The firmware uses the `BOARD_*` set of `src/t-deck-pro/utilities.h`; the `TDECK_*` macros in `configuration.h` are a stale copy and differ (`TDECK_SDCARD_CS` 39 is `BOARD_GPS_EN`; real SD CS is 48).

- **Radio:** CS 3, BUSY 6, RST 4, INT 5, enable `BOARD_LORA_EN` 46 (`utilities.h:84-87,112`).
- **SPI (shared):** SCK 36, MOSI 33, MISO 47 (`:58-60`).
- **E-paper:** DC 35, CS 34, BUSY 37, RST -1 (`:69-72`). **SD:** CS 48.
- **I2C:** 13/14. **Keyboard / touch / sensors:** keyboard INT 15, keyboard LED 42, touch INT 12, touch RST 45, ALS INT 16, gyro INT 21; `BOARD_1V8_EN` 38.
- **GPS:** RX 44, TX 43, PPS 1, EN 39. **Modem A7682E:** RI 7, ITR 8, RST 9, RXD 10, TXD 11, PWRKEY 40, `BOARD_6609_EN` 41. **Audio:** I2S BCLK 7, DOUT 8, LRC 9; mic data 17, clock 18. **Motor** 2. **Boot** 0.

**esp32-external-radio** extends `t_deck_pro` (`platformio.ini:1907-1917`), not in `default_envs`, red before W0.2 (comment there). Same table as `t_deck_pro`; whether the radio pins are free with `EXTERNAL_RADIO=1` is not checked.

**t5_epaper** (octal PSRAM, CDC console; no `ENABLE_WIND`; `configuration.h` defines only `BUTTON_PIN 0`; pins in `src/t5-epaper/utilities.h`)

- **Radio:** CS 46, IRQ 10, RST 1, BUSY 47. **SPI (shared with SD):** MISO 21, MOSI 13, SCLK 14. **SD:** CS 12.
- **I2C:** SDA 39, SCL 40. **Touch:** INT 3, RST 9. **RTC IRQ** 2. **GPS** RX 44, TX 43. **PCA9535 expander INT** 38. **Backlight enable** 11. **Boot** 0.
- The e-paper data bus is driven through an `epdiy` parallel interface and the PCA9535 expander; its GPIOs are **not derived here**. The row is incomplete for the display class.

## 3. `wxWindPinOk()` and `test_wx_pins`: what they miss

`wxWindPinOk()` (`src/command_functions.cpp:524-800`) builds a reserved list from about 90 macros and the four framework constants `SS`, `SCK`, `MISO`, `MOSI`, then calls `wxPinAllowed()` (`src/wx_pins.h`) for flash, octal and input-only rules. Verified properties:

- **Compiled in only 23 of 31 ESP32 envs.** The feature (`ENABLE_WIND`) comes with `${esp32.build_flags}`; `t_deck_pro`, `esp32-external-radio`, `t5_epaper`, `vision-master-e213` (+preview), `vision-master-e290` (+otp) and `wireless-paper` do not extend it. The new policy needs tables for those 8 anyway.
- **Not host-testable.** `test/test_wx_pins/test_wx_pins.cpp` exercises `wxPinAllowed()` with hand-made reserved arrays and the two bool flags. The macro list, `GPIO_NUM_MAX` and `GPIO_IS_VALID_GPIO` exist only in the firmware build; no test covers any board's real list.
- **Strapping is not refused.** `test_flash_pins_classic` asserts that 12 is allowed (and 26 on classic). Pins 0 (accepted as "off"), 2, 5, 12, 15 on classic and 3, 45, 46 on S3 pass unless a macro happens to name them. W1 must change this test on purpose.
- **Console and USB are not refused.** Classic 1 and 3, S3 43/44 (when the console is UART0) and S3 19/20 are not on any list.
- **`SS/SCK/MISO/MOSI` protect the wrong pins on S3 boards with their own SPI.** On the S3 variant they are 10/12/13/11, and on T-Deck Plus they reserve `TDECK_POWERON`, TFT CS, TFT DC and the radio BUSY pin only by coincidence.

**Missed per board, by macro or pin class** (all other classes of the board are covered):

| Board / group                                     | Not caught                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| T-Beam, SX1262, SX1268                            | `BOARD_LED` 4 and `OneWire_GPIO` 4; `ANALOG_PIN` 36 (input-only, so refused anyway); console 1/3; strapping 2, 15; PSRAM 16, 17; (SX1262/68) `BATTERY_PIN` 1. A windpin of 4, 1, 3, 2, 15, 16 or 17 is accepted                                                                                                                                                                                                                                  |
| ttgo-lora32-v21                                   | framework macros `SD_SCK/MISO/MOSI/CS` (14, 2, 15, 13; the list knows `SDCARD_*`, not `SD_*`), `LORA_D2` 32, `OLED_RST` 16, `BOARD_LED` 25, console 1/3                                                                                                                                                                                                                                                                                          |
| heltec_wifi_lora_32_V2                            | `RST_OLED` 16, `Vext` 21, `LORA_DIO2`/`DIO2` 34, `BOARD_LED` 25, console 1/3, strapping 2, 15                                                                                                                                                                                                                                                                                                                                                    |
| E22 classic (3) and loraprs (2)                   | `BOARD_LED`/`LED` 2, `ANALOG`/`BATTERY_PIN` 32 or 36, `OneWire_GPIO` 25, strapping 2, 5 (SCK is covered by constant), 15, console 1/3                                                                                                                                                                                                                                                                                                            |
| Heltec V3, Stick                                  | `RESET_OLED`/`RST_OLED` 21 (macro resolves to a framework constant that is not in the list), `Vext` 36 (literal in `batt_functions.cpp:498`), `ADC_CTRL_PIN` 37 (defined in `batt_function_old.cpp`, not in the config), `BATTERY_PIN` 1, `BOARD_LED` 35, console 43/44, USB 19/20, strapping 3, 45, 46                                                                                                                                          |
| Heltec V4                                         | as V3 plus `P_LORA_PA_POWER` 7, `P_LORA_PA_EN` 2, `P_LORA_PA_TX_EN` 46                                                                                                                                                                                                                                                                                                                                                                           |
| Heltec Tracker                                    | `TFT_MOSI/SCLK/CS/DC/RST/BL` (42, 41, 38, 40, 39, 21), `VEXT_CTRL` 3, `ADC_CTRL` 2, `BATTERY_PIN` 1, USB 19/20                                                                                                                                                                                                                                                                                                                                   |
| E22 S3 (2)                                        | `LED_PIN` 38, `BUTTON_EXT` 21, `OneWire_GPIO` 47, `BATTERY_PIN` 1, console 43/44, USB, strapping 3, 45, 46                                                                                                                                                                                                                                                                                                                                       |
| T-ETH-ELITE                                       | `BOARD_LED` 38, `ANALOG_PIN` 7, `BATTERY_PIN` 2, USB, strapping 3, 45 (ETH CS, covered), 46 (radio RST, covered)                                                                                                                                                                                                                                                                                                                                 |
| T-Beam-1W                                         | `BUTTON0_PIN` 0 (0 is "off" in the setter), `BOARD_LED` 18, `NTC_PIN` 14, `FAN_CTRL` 41, `ADC_PIN` 4, USB                                                                                                                                                                                                                                                                                                                                        |
| T3 S3                                             | `BUTTON_PIN1` 38, `BOARD_LED` 4, `ADC_PIN` 1, USB; MISO 3 is covered through `RADIO_MISO_PIN`                                                                                                                                                                                                                                                                                                                                                    |
| T-Connect-Pro                                     | `TFT_*` build flags (11, 13, 12, 21, 41, 42, 46), touch RST 47 / INT 3, `PIN_TFT_VDD_CTL` 3, `PIN_TFT_LEDA_CTL` 15, `RELAY_SWITCH` 8, `ADC_PIN` 4, USB                                                                                                                                                                                                                                                                                           |
| T-Beam Supreme                                    | `GPS_WAKEUP` 7, `BATTERY_PIN` 1, USB; PMU bus covered by `I2C1_SDA/SCL`; an empty `LORA_DIO0` becomes 0 (harmless)                                                                                                                                                                                                                                                                                                                               |
| T-Deck, T-Deck Plus                               | `TDECK_POWERON` 10, `TDECK_TFT_CS` 12, `TDECK_TFT_DC` 11 (all three covered only by `SS/SCK/MOSI` coincidence); `TDECK_TFT_BACKLIGHT`/`TDECK_BL_PIN` 42; trackball `TDECK_TBOX_G01..G04` (3, 2, 1, 15); `TDECK_TOUCH_INT` 16; `TDECK_KEYBOARD_INT` 46; `I2S_BCLK/LRC/DOUT` 7, 5, 6; `TDECK_ES7210_*` 48, 21, 47, 14; `BATTERY_PIN` 4; `LED` 2; USB 19/20. **The 17 pins it still accepts on Plus (1-7, 14-16, 19-21, 42, 46-48) are all in use** |
| T-Deck Pro, T5 ePaper, E213, E290, Wireless Paper | not compiled (no `ENABLE_WIND`). On paper the list would miss every `BOARD_*` pin of `utilities.h` (T-Deck Pro, T5), `PIN_DISPLAY_*`, `DEFAULT_SDI/CLK`, `PIN_PCB_VEXT`, `VEXT_ENABLE_*`, `ADC_CTRL_*`; `PIN_LORA_*` is covered because the configs define it                                                                                                                                                                                    |

**Class summary.** Missed on essentially every board: console UART, USB (S3), strapping, display reset and display control pins, power-enable pins (`Vext`, `VEXT_*`, `ADC_CTRL*`, `*_EN`, `TDECK_POWERON`, PA control), LED and button variants (`BOARD_LED`, `LED`, `LED_PIN`, `BUTTON0_PIN`, `BUTTON_EXT`), battery and analog pins, framework-only names (`Vext`, `RST_OLED`, `SD_*`, `OLED_RST`, `LORA_D1/D2`), audio (`I2S_*`, ES7210), trackball and touch/keyboard interrupts, relay and NTC/fan. PMU IRQ pins are not defined anywhere in the repo, so no macro can name them.

## 4. Free-pin candidates (T-Beam, Heltec V3, T-Deck Plus only)

A pin is listed only if it is **not** used by any class above for that board, is not flash/PSRAM/USB/strapping/console/input-only, and has no unresolved doubt. Whether a candidate is brought out to a header is **data sheet, not checked here** for all three boards; the repo cannot show it. The repo evidence is negative only: no macro and no code line uses the pin on that board. The policy default is refuse, so an exclusion costs nothing.

### T-Beam V1.2 (ttgo_tbeam, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268; identical set)

**Free: 13, 25.**

| Class                | Pins                          | Note                                                                                                                                                          |
| -------------------- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Flash                | 6-11                          |                                                                                                                                                               |
| Console              | 1, 3                          |                                                                                                                                                               |
| Radio                | 5, 18, 19, 23, 26, 27, 32, 33 | 26 stays radio on the SX1262/68 alias even though the module leaves DIO0 open                                                                                 |
| I2C / PMU            | 21, 22                        |                                                                                                                                                               |
| GPS                  | 34 (RX), 12 (TX)              |                                                                                                                                                               |
| Strapping            | 0, 2, 12, 15 (5 is also SCK)  |                                                                                                                                                               |
| Claimed              | 4, 36, 38                     | 4 = LED and default OneWire; 36 = `ANALOG_PIN`; 38 = button                                                                                                   |
| Input-only, unused   | 35, 37, 39                    | Left out: 35 is the PMU IRQ line and 37 the GPS PPS line on this board (data sheet, not checked); 39 is `KEY_BUILTIN` in the framework variant                |
| PSRAM on WROVER      | 16, 17                        | Fleet is mixed (`variants/ttgo_tbeam/platformio.ini:25-46`)                                                                                                   |
| **Unsure, left out** | 14                            | The framework `tbeam` variant names GPIO14 `LED_BUILTIN` (`pins_arduino.h:19`); the repo moved the LED to 4 for V1.1+, but a V1.2 trace on 14 is not excluded |
| Not bonded           | 20                            | data sheet                                                                                                                                                    |

### Heltec WiFi LoRa 32 V3 (heltec_wifi_lora_32_V3)

**Free: 2, 4, 5, 6, 7.**

| Class                 | Pins                                                                 | Note                                                                                                                                                |
| --------------------- | -------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| Radio                 | 8, 9, 10, 11, 12, 13, 14                                             |                                                                                                                                                     |
| I2C                   | 17, 18 (OLED), 41, 42 (sensors)                                      |                                                                                                                                                     |
| Display / power / ADC | 21 (OLED reset), 36 (`Vext`), 37 (`ADC_CTRL_PIN`), 1 (`BATTERY_PIN`) | 36 and 37 are written by literal number in the code                                                                                                 |
| LED, button           | 35, 0                                                                |                                                                                                                                                     |
| Console, USB          | 43, 44, 19, 20                                                       | Console is UART0 on V3 (no CDC on boot)                                                                                                             |
| Flash                 | 26-32                                                                | S3FN8 in-package flash (data sheet); also forbidden by the chip rule                                                                                |
| Strapping             | 0, 3, 45, 46                                                         | 3 is therefore not offered although it is an ADC pin                                                                                                |
| GPS (compile-time)    | 47, 48                                                               | `GPS_RX_PIN`/`GPS_TX_PIN`; the firmware may configure them for a GPS probe, so they are not free                                                    |
| **Unsure, left out**  | 15, 16, 33, 34, 38, 39, 40                                           | No repo use, but no evidence they are brought out (15/16 may carry a 32.768 kHz crystal, 39/40 are default JTAG pins): data sheet, not checked here |

### T-Deck Plus (t_deck_plus)

**Free: none.** Every chip pin of the S3 (0-21, 26-48) is either used or chip-restricted: 0 boot, 1/2/3/15 trackball, 4 battery, 5/6/7 I2S, 8/18 I2C, 9/17/13/45 radio, 10 power-on, 11/12 TFT, 14/21/47/48 ES7210, 16 touch INT, 19/20 USB, 26-37 flash and octal PSRAM, 38/40/41/39 SPI and SD, 42 backlight, 43/44 GPS, 46 keyboard INT. The sibling `t_deck` has the same result.

**Other boards:** no free list, by decision (`docs/campaign-gpio-01-20261107.md`).

## 5. nRF52: how `mcPinReservedNrf52840()` joins the policy (Part A only)

- **Today:** `mcPinReservedNrf52840(pin)` is `pin == 0 || 1 || 18` (`src/settings_sanitize.h:84-87`). Callers: `--button` (`command_functions.cpp:2413`, only when the pin is not 0), `--analog` (`:2455`, no zero guard, 99 means off), and the load-time sanitizer (`src/nrf52/nrf52_flash.cpp:389-399`: button, analog, `owgpio`). `--owgpio` at the console is checked separately (`<= 2`, `command_functions.cpp:~1984` per `docs/CHANGELOG-neo.md:599`).
- **Pin numbering:** Arduino pin = port pin on all three envs (section 1), so the three numbers are P0.00, P0.01, P0.18 on RAK4631, T114 and T-Echo.
- **Proposal:** `pin_policy.h` keeps the function as the nRF52 rule and calls it from the same two places; no per-board nRF52 table in W1. For `chip == NRF52840` `classify()` returns Forbidden for 0, 1, 18 and Allowed-to-assign for everything else 0-47, keeps the existing "0/99 means off" sentinels in the callers, and answers "not on this board" for every Part B request. Reason for no table: the repo has no verified RAK/T114/T-Echo radio, display or crystal map beyond these three pins (not collected in W0), and refusing every unlisted pin would remove assignments that work today (RAK `WB_IO*`).
- **Open point:** radio and display pins of the three nRF52 boards could be added later as Forbidden; they sit in each `variant.h`. P0.09/P0.10 (NFC) are not reserved today and not evaluated here.

## 6. Macro traps and the compile-time table

| Trap                                       | Where (repo)                                                                                                                                                                               | Effect on a pure table                                                                                                     |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------- |
| Macro defined **empty**                    | `LORA_DIO0` on Supreme (`:59`); `LORA_DIO3` on Supreme `:63`, V3 `:62`, V4 `:63`, Stick `:70`, T-Beam SX1262 `:83`, SX1268 `:68`                                                           | `(int)(x + 0)` (the `WX_RES` trick) turns it into 0, which also means "off"; a table must not read it                      |
| Macro is a **runtime expression**          | Heltec V2 `LORA_CS SS`, `LORA_RST RST_LoRa`, `LORA_DIO0 DIO0`, `LORA_DIO1 DIO1` (`:47-50`); `RESET_OLED RST_OLED`, `VEXT_ENABLE Vext` on V3, V4, Stick, Tracker, Supreme, T-Beam SX1262/68 | `SS`, `Vext`, `RST_OLED`, `DIO0` are `static const uint8_t` in the framework `pins_arduino.h`: unavailable without Arduino |
| **Arduino constants, not macros**          | `SS`, `SCK`, `MISO`, `MOSI`, `SDA`, `SCL`, `TX`, `RX`, `Vext`, `LED_BUILTIN`, `KEY_BUILTIN`, `A0..`                                                                                        | `#if defined()` is false for them; the E22 S3 configs `#define SCK 12` etc. over them                                      |
| Macro expands to an **undefined name**     | T-Beam-1W `SX1262_IRQ RADIO_DIO1_PIN` (`:59`); T3 and T-Connect-Pro `SX1262_CTL RADIO_CTRL` (`:46`, `:66`)                                                                                 | Compiles only because nothing expands it; a table must not read these                                                      |
| **Negative / zero sentinels**              | `LORA_DIO0 -1` (V3, V4, Stick), `OneWire_GPIO -1`, `BUTTON_EXT -1`, `TOUCH_CS=-1`; `UNUSED_PIN (0)`; settings treat 0 as "off" (`wx_pins.h:10-13`)                                         | -1 is "not wired"; GPIO 0 is a real strapping pin that the settings cannot address                                         |
| **Eight naming families** for one role     | `LORA_*`, `RF95_*`, `RADIO_*_PIN`, `RADIO_*`, `PIN_LORA_*`, `E22_*`, `SX1262X_*`/`SX1268_*`/`SX126x_*`/`SX1262_*`, `LORA_NSS`/`LORA_CS`/`LORA_RESET`/`LORA_RST`                            | Deriving a table from macro names is error-prone; write literals by hand                                                   |
| **Same macro, different meaning**          | `LORA_DIO0` is BUSY on E22 and T-Deck, DIO0 on SX127x; `LORA_DIO2` is BUSY on T-Beam SX1262/68 and Heltec; `SDA_PIN` (display bus) versus `I2C_SDA` (sensors) on Heltec                    | Role comes from the board, not from the macro name                                                                         |
| **Pins only in the framework**             | T-Beam `LORA_*`, LoRa32 `SD_*`/`OLED_RST`/`LORA_D1/D2`, V2 `Vext`/`RST_OLED`/`SDA_OLED`                                                                                                    | Not visible to the repo config, so not in `wxWindPinOk`                                                                    |
| **Pins only in a platform / board header** | `PIN_DISPLAY_*`, `PIN_PCB_VEXT`, `DEFAULT_SDI/CLK` (`src/Platforms/*`); `BOARD_*` (`src/t-deck-pro/utilities.h`, `src/t5-epaper/utilities.h`); `TFT_*` (build flags)                       | Visible only in the translation units that include them; `command_functions.cpp` does not                                  |
| **Pins by literal number in code**         | `pinMode(36, OUTPUT)` for Heltec V3/V4/Stick (`batt_functions.cpp:498`), `pinMode(23, OUTPUT)` for LoRa32 (`:503`)                                                                         | No macro exists to read; the table must carry them                                                                         |
| **Alias envs differ in the board macro**   | `BOARD_TBEAM` only on `ttgo_tbeam`; `BOARD_SX1262`, `BOARD_SX1268` on the others                                                                                                           | A table keyed by `#if defined(BOARD_TBEAM)` misses two envs                                                                |
| **Config copies that disagree with HW**    | T-Deck Pro `TDECK_*` versus `BOARD_*`; T-Beam SX1262/68 `BATTERY_PIN 1`                                                                                                                    | Tables take pins from the code that drives them, not from stale macros                                                     |

**How a compile-time table holds them:** as integer literals, written and reviewed per board; empty, runtime and unwired entries are recorded as `-1` or omitted. The pure header never includes a variant header or Arduino. The table is selected by env name (`MC_ENV_NAME`, `src/mc_board_id.h`) or by a firmware-only glue file that picks it with `#if defined(BOARD_...)`; a firmware-only check can compare the table with the integer-valued macros (wrapped like `WX_RES`) and skip the runtime ones. An env without a table gets the empty default, which is "no free pin".

## 7. Proposal for `src/pin_policy.h` (15 lines)

```cpp
// pure C++11: <stdint.h> only, no Arduino, no variant macros
enum PinClass : uint8_t { PIN_FORBIDDEN = 0, PIN_STRAPPING, PIN_INPUT, PIN_CLAIMED, PIN_FREE }; // 0 = refuse
enum Chip : uint8_t { CHIP_NONE = 0, CHIP_ESP32, CHIP_ESP32S3, CHIP_NRF52840 };
struct BoardPins {                // literals, hand-reviewed per board, see docs/gpio-pin-map.md
    const char *env;              // MC_ENV_NAME; also the key the host test iterates
    Chip        chip;
    bool        octal;            // ESP32-S3 octal PSRAM/flash: 33-37 forbidden
    uint64_t    used;             // bit n: pin n carries a radio, SPI, I2C, GPS, display, ETH, SD, UART or power function
    uint64_t    freeOk;           // bit n: reviewed free; must be disjoint from `used` and allowed by the chip
};
extern const BoardPins kBoards[];      // one entry per env that has a reviewed table
extern const unsigned  kBoardCount;
const BoardPins *boardFor(const char *env);                         // NULL: unknown env, no table
bool chipAllows(Chip c, bool octal, int pin, bool *inputOnly, bool *strap); // flash, USB, holes, strapping, input-only
PinClass classify(const BoardPins *b, int pin, uint64_t claimedMask); // NULL board, pin < 0 or > 48, bit not in freeOk: PIN_FORBIDDEN
inline bool pinCanOutput(const BoardPins *b, int pin, uint64_t claimed) { return classify(b, pin, claimed) == PIN_FREE; }
inline bool pinCanAssign(const BoardPins *b, int pin, uint64_t claimed) { PinClass k = classify(b, pin, claimed); return k == PIN_FREE || k == PIN_INPUT; }
```

- **New board:** no `kBoards` entry, so `boardFor()` returns NULL and `classify()` returns `PIN_FORBIDDEN` for every pin.
- **Host test:** loop over `kBoards`; assert `used & freeOk == 0`, every `freeOk` bit passes `chipAllows()`, the T-Deck and T-Deck Plus entries have `freeOk == 0`, the T-Beam entries equal `{13, 25}`, Heltec V3 equals `{2, 4, 5, 6, 7}`, and an unknown env yields no free pin. The nRF52 chip rule is `mcPinReservedNrf52840()` called inside `classify()` for `CHIP_NRF52840`, with `freeOk == 0` (Part B off).

## 8. Findings outside this task (reported, not fixed)

- **T-ETH-ELITE_1262** `configuration.h:56,72`: `ETH_RST_PIN` and `RADIO_BUSY_PIN` are both 16. `esp32_eth.cpp:17-26` hands 16 to the W5500 driver as reset. Whether the board really shares the line is not checked.
- **T-Deck / T-Deck Plus:** `OneWire_GPIO 25` (`t_deck_plus/configuration.h:99`) is not an S3 pin (`soc_caps.h:113`); `LORA_DIO2 32` is a flash pin; `LED 2` is also trackball G02.
- **T-Connect-Pro** `configuration.h:37,72`: `GPS_TX_PIN` 4 and `ADC_PIN`/`ANALOG_PIN` 4 are the same pad; `PIN_TFT_VDD_CTL` 3 equals touch INT 3 (`pin_config.h:37`); `TFT_RST` 42 equals the LoRa reset 42. Intent not checked.
- **ttgo-lora32-v21** `configuration.h:82-83`: GPS RX 13 / TX 15 are the SD card pads `SD_CS`/`SD_MOSI` in the framework variant.
- **Alias envs:** `BOARD_TBEAM` gates the OneWire remap (`onewire_functions.cpp:49,211`, `loop_functions.cpp:216`) and part of the battery code (`batt_functions.cpp:484` also lists SX1262/68, `batt_function_old.cpp:168,643` lists only `BOARD_TBEAM`/`BOARD_SX1268`). The three T-Beam envs therefore behave differently on the same hardware; whether that is intended is not checked.
- **`wxWindPinOk` comment** (`command_functions.cpp:796`) says classic ESP32 lacks GPIO20; the framework valid mask keeps it (section 1).
- **Fifth assignable pin pair:** soft-serial RX/TX (`node_ss_rx_pin`, `node_ss_tx_pin`, `src/softser_functions.cpp:33`) is user-set and not among the concept's four; the setter scout owns it.
- **`t_deck_pro/configuration.h`** keeps a stale T-Deck macro set (section 2); it is read only by code that is not compiled there.

## 9. Claims left unverified

- Header exposure of every free candidate (T-Beam 13, 25; Heltec V3 2, 4-7): data sheet, not checked here.
- Classic strapping set, WROVER PSRAM pins 16/17, ADC2 limits, GPIO 20/37/38 bonding, T-Beam PMU IRQ 35 and GPS PPS 37: data sheet, not checked here.
- S3 strapping set, 47/48 voltage domain, Heltec V3 pins 15/16/33/34/38-40, Supreme peripherals: data sheet, not checked here.
- Framework packages 3.20014 (platform 6.6.0, T-Deck family) and the 6.5.0 platform packages: not opened.
- T5 ePaper parallel display bus pins; RAK/T114/T-Echo radio and display pins: not collected.
- `esp32-external-radio`: radio pin state with `EXTERNAL_RADIO=1`.
- No build was run; nothing here was tested on hardware.
