# LED and charge LED: feature/bug matrix and parity fix plan (2026-10-18, rev 2)

Audience: an AI coding agent running `/orchestrate-waves` on `fork-dev`. Read-only audit of the
working tree at `5a5fc7f7` (the parallel session's uncommitted diff does not touch any LED line;
checked with `git diff HEAD | grep -i led`). Rev 2 records the operator decisions (section 6) and the
hardware facts verified against vendor and reference sources (section 3.1).

## BLUF

- **Charge LED (AXP T-Beams) is in good shape** after CHG-01..04: the console setting persists, boot
  applies it, RM override works. Three defects remain: the RM override survives deep sleep (LED-01),
  no surface shows the effective state (LED-02), and a BLE `--charge led` change does not refresh the
  phone (LED-03).
- **Board LED has no parity.** `--board led` exists only on ESP32 variants that define `BOARD_LED`.
  Heltec V3/V4 have an RM-only LED, several boards with a real LED have no macro, and all nRF52 boards
  have no LED feature. On T-Beam the heartbeat drives GPIO4 even when OneWire uses it (LED-04).
- **Hardware check found wrong pins in four variants.** T-Echo (LED-15) maps its "LEDs" to the touch
  pin and a power-enable pin, with inverted polarity; the real LEDs are probably lit from boot.
  S3 DevKitC-1 (`BOARD_LED 38`) and T-ETH-Elite (`BOARD_LED 1`) point at the wrong pins (LED-13).
  T114 polarity is probably inverted (LED-16).
- **NVRAM:** only the two console settings persist (`node_sset3` 0x0080, `node_sset3` 0x0001 plus
  the `node_sset2` 0x8000 marker). Every RM LED state is RAM or PMU register only, by design.
- **Plan:** 4 waves (core helper, surfaces, platform roll-out, gate and bench), 5 implementer briefs,
  a regression test per bug. Do not start while the parallel session holds uncommitted edits in
  `src/command_functions.cpp`, `src/esp32/esp32_main.cpp` and `src/web_functions/`.

## 1. Parity target (definition of done)

| Capability       | Board LED (every board with a fitted user LED)                  | Charge LED (every AXP board)                                      |
| ---------------- | --------------------------------------------------------------- | ----------------------------------------------------------------- |
| Console set      | `--board led on/off`, persisted                                 | `--charge led on/off`, persisted                                  |
| Console readback | `--info` line `...BOARD LED: on/off/not fitted`                 | `--info` line `...CHARGE LED: auto/off[ (RM <mode>)]/not fitted`  |
| BLE              | `BLED` in SN, phone refreshed after change; not fitted reported | `CHGLED` in SN, phone refreshed after change; not fitted reported |
| Web GUI          | setup control + status line; "not fitted" when absent           | setup control + status line (also without battery)                |
| RM               | `led on/off/flash`, status letter `L`                           | `chgled auto/on/off`; `chgled` alone reads the effective mode     |
| Boot             | applies stored setting, LED dark until then                     | applies stored setting                                            |
| Deep sleep       | LED forced dark, RM hold released                               | RM override reverted to the stored setting before sleep           |
| Not fitted       | command absent from help, RM `err unsupported`                  | same                                                              |

## 2. Surface inventory (where the code lives)

| Surface                       | Board LED                                                                                                | Charge LED                                                                           |
| ----------------------------- | -------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| Console toggle rows           | `src/command_functions.cpp:588-591` (`#ifdef BOARD_LED`), post hook `:444-450`                           | `src/command_functions.cpp:584-587` (AXP guard), post hooks `:435-439`               |
| Boot load                     | `src/esp32/esp32_main.cpp:1389`; nRF52: none                                                             | `src/esp32/esp32_main.cpp:1382`, applied `:1667-1669`                                |
| Boot pin init                 | `src/esp32/esp32_main.cpp:1117-1124`; Heltec RM pin `:2324-2328`; nRF52 `nrf52_main.cpp:544-551`         | `setupPMU()` then `setChargeLedOn()`                                                 |
| Runtime driver                | flasher in `esp32loop` `src/esp32/esp32_main.cpp:3117-3162` (30 ms / 2 s); NeoPixel `:3044-3110`         | `src/esp32/esp32_pmu.cpp:384-404` (`setChargeLedOn`, `setChargeLedMode`)             |
| RM                            | `src/rm_runtime.cpp:278-291`, hold `src/loop_functions.cpp:94-118`, rule `loop_functions_extern.h:31-62` | `src/rm_runtime.cpp:293-310`                                                         |
| RM status                     | `src/rm_runtime.cpp:208-211` (`L` = RM hold only)                                                        | none                                                                                 |
| BLE export                    | `src/command_functions.cpp:7465` `BLED`                                                                  | `src/command_functions.cpp:7466` `CHGLED`                                            |
| Web status                    | none                                                                                                     | `src/web_functions/web_functions.cpp:4063` (battery branch only)                     |
| Web setup                     | none (`src/web_functions/web_setup.cpp`)                                                                 | none                                                                                 |
| Web RM page                   | `src/web_functions/web_rm_page.cpp` Light / Light flash tiles                                            | Charge LED select                                                                    |
| Deep sleep                    | `src/esp32/esp32_sleep.cpp:235-237`, `src/command_functions.cpp:1733-1735` (RM hold only)                | none                                                                                 |
| Help                          | `src/command_functions.cpp:1511-1513`                                                                    | `:1514-1516`                                                                         |
| Decision helper + native test | none                                                                                                     | `src/charge_led_mode.h`, `test/test_charge_led_mode`, `[env:native_charge_led_mode]` |

## 3. Feature matrix per board

Legend: Y = works, - = absent, (Y) = present but defective. "Target" is the variant state after W3.

| Variant / env                                                                               | Fork today                          | `--board led` | RM `led` | AXP | Target (verified, see 3.1)                                                    | ID        |
| ------------------------------------------------------------------------------------------- | ----------------------------------- | ------------- | -------- | --- | ----------------------------------------------------------------------------- | --------- |
| `ttgo_tbeam`, `ttgo_tbeam_SX1262`                                                           | `BOARD_LED 4`, active low           | (Y)           | -        | Y   | keep pin; runtime OneWire guard; RM `led` when the pin is free                | LED-04    |
| `ttgo_tbeam_SX1268`                                                                         | no `BOARD_LED`                      | -             | -        | Y   | `BOARD_LED 4`, `BOARD_LED_ACTIVE_LOW 1` (same T-Beam 1.1+ PCB)                | LED-05    |
| `ttgo_tbeam_supreme`                                                                        | none                                | -             | -        | Y   | unchanged (charge LED only)                                                   | -         |
| `LilyGo_T-Beam-1W`                                                                          | `BOARD_LED 18`                      | Y             | Y        | -   | unchanged                                                                     | -         |
| `heltec_wifi_lora_32_V3`                                                                    | `REMOTE_LED_PIN 35`                 | -             | Y        | -   | `BOARD_LED 35` (Q1 yes)                                                       | LED-06    |
| `heltec_wifi_lora_32_V4`                                                                    | `REMOTE_LED_PIN 35`                 | -             | Y        | -   | `BOARD_LED 35` for V4 (S3R2); V4R8 has the LED on GPIO46, GPIO35 = PSRAM      | LED-06/17 |
| `heltec_wifi_lora_32_V2`                                                                    | none                                | -             | -        | -   | `BOARD_LED 25`                                                                | LED-14    |
| `heltec_wireless_stick` (WSL V3)                                                            | none                                | -             | -        | -   | `BOARD_LED 35`                                                                | LED-14    |
| `heltec_wireless_tracker`                                                                   | none                                | -             | -        | -   | unchanged: sources disagree (GPIO18 vs RGB on 48), defer                      | LED-14    |
| `E22-DevKitC`, `E22_1262-DevKitC`, `esp32-loraprs-*`                                        | `BOARD_LED 2`                       | Y             | Y        | -   | unchanged                                                                     | -         |
| `E22_12xx_S3-DevKitC-1-N16R8`                                                               | `BOARD_LED 38`, NeoPixel on 48      | (Y)           | (Y)      | -   | no `BOARD_LED`; NeoPixel on 38 (v1.1) by default, 48 by build flag (v1.0)     | LED-13    |
| `T-ETH-ELITE_1262`                                                                          | `BOARD_LED 1`, NeoPixel 38 x 0      | (Y)           | (Y)      | -   | `BOARD_LED 38` (plain LED); drop `LED_PIN`/`LED_PIXEL`                        | LED-13    |
| `LilyGo_T3_S3_V1_3`                                                                         | `BOARD_LED 4`                       | Y             | Y        | -   | unchanged                                                                     | -         |
| `ttgo-lora32-v21`                                                                           | `BOARD_LED 25`                      | Y             | Y        | -   | unchanged                                                                     | -         |
| `wireless-paper`, `vision-master-e213`                                                      | `BOARD_LED 18` / `45`               | Y             | Y        | -   | unchanged                                                                     | -         |
| `wiscore_rak4631`                                                                           | green 35, blue 36, active high      | -             | -        | -   | `BOARD_LED 35` (green), active high                                           | LED-07    |
| `heltec_t114`                                                                               | 35 + fake blue 36, "active high"    | -             | -        | -   | `BOARD_LED 35`, active low; drop the fake blue pin                            | LED-07/16 |
| `t_echo`                                                                                    | 11 / 13 / 14, "active high" (wrong) | -             | -        | -   | green 33 (P1.01), red 35 (P1.03), blue 14 (P0.14), active low; `BOARD_LED 33` | LED-07/15 |
| `vision-master-e290`, T-Deck family, `t5_epaper`, `LilyGo_T_Connect_Pro`, `E22_XML-DevKitC` | none                                | -             | -        | -   | unchanged; report "not fitted"                                                | LED-10    |

### 3.1 Hardware verification (Q4)

| Board                       | Fact                                                                                                                                  | Sources                                                                                                                                                                                                                                                                                          | Confidence                    |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------- |
| ESP32-S3-DevKitC-1          | Only user LED is the addressable RGB: GPIO48 on v1.0, GPIO38 on v1.1. No plain LED besides power                                      | [Espressif user guide v1.1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)                                                                                                                                                          | high                          |
| T-ETH-Elite                 | Plain user LED on IO38 (`LED_PIN 38` in LilyGO's sketches; Meshtastic blinks it as `LED_POWER 38`); GPIO1 is `MODEM_RI_PIN`           | LilyGO `LilyGO-T-ETH-Series/examples/ETHOTA/utilities.h`; Meshtastic `variants/esp32s3/t-eth-elite/variant.h`; [CNX](https://www.cnx-software.com/2024/07/30/lilygo-t-eth-elite-esp32-s3-ethernet-poe-board-supports-stackable-lorawan-and-cellular-expansion-modules/?amp=1)                    | high                          |
| T-Echo                      | Green P1.01 (33), red P1.03 (35), blue P0.14 (14), active low (factory sketch writes HIGH = off). P0.11 = touch, P0.13 = power enable | LilyGO `T-Echo/examples/Factory/utilities.h` + `Factory.ino`; fork's own `src/nrf52/t_echo_utilities.h:39-66`; RNode `Boards.h`                                                                                                                                                                  | high (3 sources)              |
| Heltec T114                 | One user LED, green, P1.03 (35); active low per Meshtastic; 2 NeoPixels on P0.14 (out of scope)                                       | [Zephyr T114 v2 docs](https://nrfconnectdocs.nordicsemi.com/ncs/latest/zephyr/boards/heltec/heltec_t114_v2/doc/index.html); Meshtastic `heltec_mesh_node_t114/variant.h`                                                                                                                         | pin high, polarity one source |
| RAK4631                     | Green 35, blue 36, active high                                                                                                        | Meshtastic `rak4631/variant.h`, fork variant                                                                                                                                                                                                                                                     | high                          |
| T-Beam (incl. SX1268 build) | v1.1+ has an extra LED on GPIO4, active low                                                                                           | Meshtastic `variants/esp32/tbeam/variant.h`                                                                                                                                                                                                                                                      | high                          |
| Heltec V2.1                 | White LED GPIO25                                                                                                                      | arduino-esp32 `heltec_wifi_lora_32_V2/pins_arduino.h`; Meshtastic `heltec_v2.1`                                                                                                                                                                                                                  | high                          |
| Wireless Stick Lite V3      | LED GPIO35                                                                                                                            | arduino-esp32 `heltec_wireless_stick_lite_v3/pins_arduino.h`; Meshtastic `heltec_wsl_v3`                                                                                                                                                                                                         | high                          |
| Heltec V4                   | White LED GPIO35 on V4 (S3R2); V4R8 uses GPIO46 because GPIO35 is octal PSRAM                                                         | [Heltec V4.2.0 datasheet](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf), [V4R8 datasheet](https://resource.heltec.cn/download/WiFi_LoRa_32_V4-R8/datasheet/WiFi_LoRa_32_V4R8.pdf), [espboards](https://www.espboards.dev/esp32/heltec-wifi-lora-32-v4/) | high                          |
| Heltec Wireless Tracker     | Meshtastic `LED_POWER 18`, arduino-esp32 `LED_BUILTIN` = RGB on 48: conflict                                                          | both repos                                                                                                                                                                                                                                                                                       | unresolved, deferred          |

No T-Echo, T114, T-ETH-Elite or DevKitC is on the bench. Their target states rest on the sources
above and need a field confirmation (ask a user with the board) before an upstream PR.

## 4. NVRAM retention

| State                              | Stored where                                                   | Persisted                 | Boot behaviour                                           | Deep sleep                         |
| ---------------------------------- | -------------------------------------------------------------- | ------------------------- | -------------------------------------------------------- | ---------------------------------- |
| `--board led on/off`               | `node_sset3` bit 0x0080                                        | yes (TG_SAVE)             | ESP32 loads it (`esp32_main.cpp:1389`); nRF52 ignores it | flasher not forced off (LED-12)    |
| `--charge led on/off`              | `node_sset3` bit 0x0001 + `node_sset2` bit 0x8000 (CFG marker) | yes (TG_SAVE, post first) | `chargeLedAutoFromSettings()`; marker clear = auto       | PMU keeps the stored mode          |
| RM `led on/off/flash`              | RAM `bRemoteLed`, `bRemoteLedFlash`                            | no (by design)            | off                                                      | released (`remoteLedForceOff`)     |
| RM `chgled auto/on/off`            | AXP register only                                              | no (by design)            | stored setting re-applied                                | **override stays active** (LED-01) |
| NeoPixel status colours (`bLED_*`) | RAM                                                            | no                        | dark                                                     | n/a                                |

Persistence mechanics (verified): ESP32 `save_settings()` (`src/esp32/esp32_flash.cpp:613`) writes
every schema field to Preferences namespace `Credentials`; nRF52 uses its keyed settings store
(`src/nrf52/nrf52_flash.cpp:689`). `node_sset2`/`node_sset3` are in the config JSON export with clamp
0..65535 (`src/config_json.h:270,306`), so both bits survive export/import and downgrade. No toggle row
or direct writer clears `node_sset2` bit 15 (all masks checked; `0xFFFFxxxx` style). `node_sset3`
bit use: 0x0001 CHGLED, 0x0080 BLED, 0x8000 NOPMOTHER; no collision. No new setting bit is needed for
this plan; nRF52 starts reading the existing 0x0080.

## 5. Defect and gap matrix

| ID     | Kind   | Sev | Finding                                                                                                                                                                                                                                                                      | Evidence                                                                                             | Fix                                                                                                                                                                                   | Regression test                                                                             |
| ------ | ------ | --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| LED-01 | bug    | M   | RM `chgled on` held through `--deepsleep`: steady LED for the whole sleep, against the "not persisted" contract; battery drain                                                                                                                                               | `esp32_sleep.cpp` has `remoteLedForceOff()` only                                                     | before sleep, revert to the stored mode when an override is active                                                                                                                    | native: override state machine (LED-02 helper); golden lint pins the call in the sleep path |
| LED-02 | bug    | L-M | Effective charge-LED mode invisible: `bCHARGE_LED` stays at the console value during an RM override; web/BLE/console show stale state; RM has no read                                                                                                                        | `rm_runtime.cpp:298-304` never records the override                                                  | override variable + `effectiveChargeLedMode()` in `charge_led_mode.h`; surfaces show `auto (RM on)`; RM `chgled` without argument returns `ok chgled=<effective>`                     | native `test_charge_led_mode`: set/clear/boot-reset; RM parse test for the bare form        |
| LED-03 | bug    | L   | `--charge led on/off` from the phone does not resend SN, so the app's `CHGLED` stays stale                                                                                                                                                                                   | `command_functions.cpp:585-586` `TG_DIRTY_NONE`                                                      | `TG_DIRTY_NODE` on both rows (they keep `TG_BRETURN`; the tail sends SN)                                                                                                              | `test_command_toggles` pins the dirty class; `toggle_table_lint.py` check 9 stays green     |
| LED-04 | bug    | M   | T-Beam: `--board led on` toggles GPIO4 every 2 s while OneWire/DHT uses GPIO4; boot also forces GPIO4 to OUTPUT                                                                                                                                                              | `ttgo_tbeam/configuration.h:47,52`; flasher `esp32_main.cpp:3122-3162`                               | `boardLedPinFree(onewireOn, owPin, ledPin)`; flasher, `--board led on`, boot init and RM `led` refuse or skip while OneWire owns the pin; replaces the compile-time OneWire exclusion | native helper test (shared/free, onewire on/off, owgpio fallback)                           |
| LED-05 | parity | L   | `ttgo_tbeam_SX1268` lacks `BOARD_LED 4` / `BOARD_LED_ACTIVE_LOW 1`                                                                                                                                                                                                           | variant tabulation, 3.1                                                                              | add both macros                                                                                                                                                                       | `variant_macros_lint.py` golden (fails before regeneration)                                 |
| LED-06 | parity | L   | Heltec V3/V4: RM `led` only, no `--board led` heartbeat                                                                                                                                                                                                                      | `heltec_wifi_lora_32_V3/configuration.h:90-92`                                                       | Q1 yes: `BOARD_LED 35` (default off), drop the `REMOTE_LED_PIN && !BOARD_LED` boot special case                                                                                       | golden variant macros; bench DK5EN-1                                                        |
| LED-07 | parity | M   | nRF52 (RAK4631, T114, T-Echo): no `--board led`, no RM `led` (`!defined(NRF52_SERIES)`), green LED unused                                                                                                                                                                    | `loop_functions_extern.h:35`, `nrf52_main.cpp:544-551,2715-2730`                                     | `BOARD_LED` per variant, shared flasher from W1 in the nRF52 loop, boot load of 0x0080, force off in `nrf52_sleep.cpp`                                                                | native helper tests; build three envs; bench DK5EN-90                                       |
| LED-08 | parity | L   | Web GUI: no setup control for either LED; charge LED status hidden on USB power; board LED state never shown                                                                                                                                                                 | `web_functions.cpp:4058-4066`, `web_setup.cpp`                                                       | setup handlers `--board led %s`, `--charge led %s`; status rows outside the battery branch, "not fitted" when absent                                                                  | jsdom case in `tools/webgui_*`; `test_web_guard`; Chrome check                              |
| LED-09 | parity | L   | Console: no LED readback in `--info`                                                                                                                                                                                                                                         | `command_functions.cpp:6907` block                                                                   | two lines using the effective mode, "not fitted" when absent                                                                                                                          | golden selftest output pin                                                                  |
| LED-10 | gap    | L   | BLE SN sends `BLED`/`CHGLED` as `false` on boards without the hardware; "off" and "not fitted" look the same                                                                                                                                                                 | `command_functions.cpp:7465-7466`                                                                    | Q3: report not fitted (design in 6.1)                                                                                                                                                 | BLE golden for SN/SN1 on a fitted and an unfitted env                                       |
| LED-11 | defect | L   | `bool bLED = BOARD_LED_ACTIVE_LOW;` mixes "lit" state with pin level                                                                                                                                                                                                         | `esp32_main.cpp:213`                                                                                 | `false` in the shared helper                                                                                                                                                          | covered by the helper test                                                                  |
| LED-12 | defect | L   | Board LED flasher not forced dark before ESP32 deep sleep on boards without `REMOTE_LED_PIN`                                                                                                                                                                                 | `esp32_sleep.cpp:235`                                                                                | `boardLedForceOff()` next to `remoteLedForceOff()`                                                                                                                                    | golden lint source pin                                                                      |
| LED-13 | bug    | M   | S3 DevKitC-1: `BOARD_LED 38` bit-bangs the WS2812 data line on v1.1, while the NeoPixel driver sits on 48 (v1.0 pin). T-ETH-Elite: `BOARD_LED 1` drives `MODEM_RI`, and the real LED (38) is set up as a NeoPixel with 0 pixels                                              | 3.1                                                                                                  | DevKitC: remove `BOARD_LED`, NeoPixel default 38, `-DLED_PIN=48` for v1.0; heartbeat and RM `led` use the NeoPixel (white). T-ETH-Elite: `BOARD_LED 38`, drop `LED_PIN`/`LED_PIXEL`   | golden variant macros; NeoPixel RM path in the helper test                                  |
| LED-14 | parity | L   | Heltec V2.1 (GPIO25) and Wireless Stick Lite V3 (GPIO35) have a user LED with no macro; Tracker unresolved                                                                                                                                                                   | 3.1                                                                                                  | add `BOARD_LED`; Tracker deferred to BACKLOG                                                                                                                                          | golden variant macros                                                                       |
| LED-15 | bug    | M   | T-Echo variant: `LED_GREEN 11` = touch pin, `LED_RED 13` = power enable, `LED_STATE_ON 1` but the LEDs are active low. Boot `pinMode(OUTPUT)` latches LOW, so the real blue LED, and probably all three, stay lit; touch and power enable are driven LOW until `boardInit()` | `variants/t_echo/variant.h:69-82`, `nrf52_main.cpp:544-551,1014-1017`, `nrf52_functions.cpp:155-183` | correct the pins and polarity; LED init writes the off level; the BLE-off path writes the off level                                                                                   | variant macros golden; source pin for the off-level writes; field confirm                   |
| LED-16 | bug    | L   | T114: `LED_STATE_ON 1` likely inverted (Meshtastic: 0); `LED_BLUE 36` does not exist on the board                                                                                                                                                                            | `variants/heltec_t114/variant.h:69-79`                                                               | `LED_STATE_ON 0`, drop `PIN_LED2`/fake blue; field confirm before upstream                                                                                                            | golden variant macros                                                                       |
| LED-17 | risk   | L   | Heltec V4R8: GPIO35 is octal PSRAM, LED on GPIO46. The fork's V4 env uses the V3 board definition (no PSRAM), so today RM `led` drives a PSRAM line on an R8 board                                                                                                           | `variants/heltec_wifi_lora_32_V4/platformio.ini:3`, 3.1                                              | document: the V4 env supports V4 (S3R2) only; an R8 env is out of scope                                                                                                               | n/a                                                                                         |

Not a defect (checked): charge-LED mask rows, marker bit 15 preservation, AXP192/AXP2101 support of
all three `XPOWERS_CHG_LED_*` modes (`lib/XPowersLib`), toggle fall-through for `board`/`charge` (no
later rung matches), RM LED polarity (LED-POL), WP_DISP sleep path (no AXP there).

## 6. Operator decisions (2026-10-18)

| Q   | Decision                                                                               |
| --- | -------------------------------------------------------------------------------------- |
| Q1  | Heltec V3/V4 get `--board led` (default off).                                          |
| Q2  | RM `chgled` without argument reads the effective mode: `ok chgled=auto\|on\|off[ rm]`. |
| Q3  | BLE reports when `BLED`/`CHGLED` are not fitted (design 6.1).                          |
| Q4  | Checked online, results in 3.1. Tracker stays deferred.                                |
| Q5  | (not asked again) nRF52 blue LED stays dark; out of scope.                             |

### 6.1 "Not fitted" reporting design (Q3)

- A capability helper `ledFitted()` returns two flags: board LED (`BOARD_LED` defined, or a NeoPixel
  heartbeat) and charge LED (AXP compiled in and `PMU != NULL` at runtime).
- **SN:** on a fitted board, `BLED`/`CHGLED` stay booleans as today (no change for the app). On a board
  where one is not fitted, the key is left out of SN. That also frees bytes in SN (228 of 244).
- **SN1:** a new key `"LEDNF"` lists what is not fitted, for example `"LEDNF":"BLED,CHGLED"`; left out
  when both are fitted. Check the SN1 worst case (167 bytes plus 22 = 189, under 244).
- **Console, web, RM:** "not fitted" in `--info` and on the web status page; RM keeps the stable token
  `err unsupported`.
- Open risk: how the phone app reacts to a missing key. The app is a separate project; before merge,
  check one app build against an unfitted node (Heltec V2 without the W3 change, or a T-Deck), and add
  a note to the upstream PR body.

## 7. Waves

Hotspots: `src/command_functions.cpp`, `src/esp32/esp32_main.cpp`, `platformio.ini`, `variants/*`.
Each gets exactly one owner per wave; `platformio.ini` and `variants/*` belong to the orchestrator
(carve-out).

### W1 Core helper (1 writer)

Brief A, `implementer`, Sonnet 5.5 medium. About 25 min, about $3.

- Exclusive files: `src/charge_led_mode.h`, new `src/board_led.h` (header-only, Arduino-free logic),
  `src/loop_functions.cpp`, `src/loop_functions_extern.h`, `test/test_charge_led_mode/test_main.cpp`,
  new `test/test_board_led/test_main.cpp`.
- Content: `effectiveChargeLedMode(stored, override)` and override set/clear (LED-01/02);
  `boardLedPinFree()` (LED-04); `ledFitted()` flags (6.1); flasher phase function
  `boardLedPhase(now, timer, lit)` (LED-11); Arduino side `boardLedTick()`, `boardLedForceOff()` in
  `loop_functions.cpp`, with a NeoPixel branch for boards that have `LED_PIN` and no `BOARD_LED`
  (LED-13). Keep the `NRF52_SERIES` exclusion until W3.
- Tests first where a bug exists (the LED-02 override case must fail on the unchanged header).
- The orchestrator adds `[env:native_board_led]` to `platformio.ini` at the gate.
- Verify: `pio test -e native_charge_led_mode`, then `-e native_board_led`, through `piolock.sh`.
  Never bare `pio test`.

### W2 Surfaces (3 writers, parallel, disjoint)

Brief B, `implementer`, Sonnet 5.5 medium. About 25 min. Owns `src/command_functions.cpp`,
`test/test_command_toggles/test_command_toggles.cpp`, `test/golden/toggle_table_lint.py` (read-only
unless a new check is needed).

- LED-03 `TG_DIRTY_NODE` on the charge rows; LED-04 refusal in `--board led on`; LED-09 `--info`
  lines; LED-10 SN/SN1 per 6.1; WP_DISP sleep path calls `boardLedForceOff()` (LED-12).
- Before-run: the row-dirty assertion fails on the unchanged table.

Brief C, `implementer`, Sonnet 5.5 medium. About 25 min. Owns `src/esp32/esp32_main.cpp`,
`src/esp32/esp32_sleep.cpp`, `src/esp32/esp32_pmu.cpp`, `src/esp32/esp32_pmu.h`, `src/rm_runtime.cpp`,
`src/rm_commands.h`, `test/test_remote_cmd` (or the RM parse test that owns the `chgled` shape).

- Replace the inline flasher with `boardLedTick()`; skip the boot `pinMode(BOARD_LED, OUTPUT)` while
  OneWire owns the pin (LED-04); deep sleep: `boardLedForceOff()` and charge-LED revert (LED-01/12);
  RM `chgled` records the override and accepts the bare read form (Q2); RM `led` refuses when the pin
  is not free and drives the NeoPixel on NeoPixel-only boards.
- Artifact proof: string scan of the T-Beam image for the new refusal message and the `chgled=`
  read reply, and of the Heltec V3 image for the unchanged `ok led=` reply.

Brief D, `implementer`, Sonnet 5.5 medium. About 20 min. Owns `src/web_functions/web_setup.cpp`,
`src/web_functions/web_functions.cpp`, `src/web_functions/web_rm_page.cpp`, `tools/webgui_*` test
files, `tools/remote_cmd.py` (allowlist for the bare `chgled`).

- LED-08 setup controls and status rows with "not fitted"; the RM page reads the bare `chgled` reply.

Shared resources: B builds a native env only, C builds `ttgo_tbeam`, D runs jsdom and pytest only.
The full gate belongs to the orchestrator.

### W3 Platform roll-out (1 writer + orchestrator)

Brief E, `implementer`, Sonnet 5.5 medium. About 25 min. Owns `src/nrf52/nrf52_main.cpp`,
`src/nrf52/nrf52_sleep.cpp`, `src/nrf52/loop_actions_nrf52.cpp`, `src/nrf52/nrf52_functions.cpp`.

- LED-07: boot load of `node_sset3` 0x0080 into `bUSER_BOARD_LED`, `boardLedTick()` and
  `remoteLedTick()` in the nRF52 loop, force off in the sleep path.
- LED-15: LED init writes the off level (`!LED_STATE_ON`) right after `pinMode`; the BLE-off path
  (`nrf52_main.cpp:1014-1017`) writes the off level instead of `LOW`; T114 no longer excluded once its
  variant is right.
- Orchestrator in the same wave (carve-out), all `variants/*`: T-Echo pins and polarity (LED-15),
  T114 polarity (LED-16), RAK/T114/T-Echo `BOARD_LED`, `ttgo_tbeam_SX1268` (LED-05), Heltec V3/V4
  `BOARD_LED 35` (LED-06), Heltec V2.1 and WSL V3 (LED-14), DevKitC and T-ETH-Elite (LED-13);
  remove the `NRF52_SERIES` exclusion; regenerate `test/golden/native/variant-macros*.txt`.
- Builds one at a time: `wiscore_rak4631`, `heltec_t114`, `t_echo` (Rosetta needed for nRF52),
  `E22_1262_S3-DevKitC-1-N16R8`, `T-ETH-ELITE_1262`, `heltec_wifi_lora_32_V2`, `heltec_wireless_stick`.

### W4 Gate, advisor, bench

- Gate (orchestrator): `tools/regression.sh --stage 1,2`, plus builds of every env touched in W2/W3.
  Compare RAM/flash regions, not hashes.
- Advisor gate: a fresh Opus 5.5 medium agent reads the full wave diff before each commit.
- Bench (single agent, serialized, `identity_guard.py` first, TX at most 2 dBm, RM traffic only
  between own nodes, over the 2323 console):
  - DK5EN-92 (T-Beam): `--board led on` with OneWire off blinks; with OneWire on GPIO4 it is
    refused; `chgled on`, then `chgled` reads `on rm`; `--deepsleep`: LED dark during sleep, stored
    mode after wake (LED-01).
  - DK5EN-1 (Heltec V3): `--board led on` heartbeat (Q1); RM `led on/flash/off` still correct.
  - DK5EN-90 (RAK4631): `--board led on` green heartbeat, survives reboot, RM `led on/off` from DK5EN-1.
  - DK5EN-14 (T-Deck Plus, no LED): phone shows "not fitted" (6.1); SN still parses in the app.
  - Web GUI controls checked in Chrome on DK5EN-1 and DK5EN-92.
- Field confirmation (no bench board): T-Echo (LED-15), T114 (LED-16), DevKitC v1.0/v1.1 and
  T-ETH-Elite (LED-13). List them in the upstream PR body as "not tested on hardware".
- Docs per wave: `docs/architecture/08-defect-catalogue.md` (LED-01..17), new
  `docs/campaign-led-parity-20261018.md` as the resume point, `docs/RESUME.md`, `docs/BACKLOG.md`
  for the Tracker LED and a V4R8 env.

## 8. Cost and wall-clock estimate

| Wave | Agents                   | Wall-clock               | Cost (approx.) |
| ---- | ------------------------ | ------------------------ | -------------- |
| W1   | 1 implementer            | 30 min + gate            | $3             |
| W2   | 3 implementers parallel  | 30 min + gate            | $9             |
| W3   | 1 implementer + orch.    | 30 min + 7 serial builds | $5             |
| W4   | advisor x3, bench serial | 60-90 min                | $6             |

## 9. Brief boilerplate (restate in every brief)

- Exclusive file set as listed; never create, stub or delete a file another brief owns.
- No git commands, no worktrees, no whole-repo formatter.
- No bare `pio test`; every `pio` call through `piolock.sh`; one target per writer.
- No flashing, no serial port, no RF transmission, no RM sends: hardware belongs to W4.
- Each bug fix: a regression test with a pasted failing before-run against the unchanged code.
- Prove the change is in the artifact (string or symbol scan), since every LED path sits behind
  `#ifdef BOARD_LED` / AXP / `REMOTE_LED_PIN` / `LED_PIN` guards.
- Report at most 15 lines: files, verification lines, deviations, open questions.
