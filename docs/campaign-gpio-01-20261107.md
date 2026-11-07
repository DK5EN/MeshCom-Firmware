# Campaign: GPIO-01 hardware pins on the Pins page and over RM1

Resume point. Design and decisions: [concept-gpio-pins-20261106.md](concept-gpio-pins-20261106.md). Backlog row: `GPIO-01`.

## Operator decisions (2026-11-07)

- **No stop after W0:** the pin table is a configuration item; real-world use will show its errors.
- **No bench sitting, no jumper tests, no soak:** implementation only. OMs test it in the field after a release.
- **End of this campaign:** W5 committed, host gate green, local builds of the default envs. CI build and release only on a separate request.
- **Part B boards:** only reviewed tables offer free pins: T-Beam (13, 25) and Heltec V3 (2, 4, 5, 6). T-Deck Plus has none (W0), every other ESP32 board offers none (default refuse), nRF52 answers "not on this board".

## Flow

W0 to W5 run one after the other on `fork-dev`, one writer at a time. Each wave: implementer, scoped checks, orchestrator gate, advisor for behaviour changes, commit, this file updated.

## Wave status

| Wave | Work                                                  | Status |
| ---- | ----------------------------------------------------- | ------ |
| W0   | Per-board pin table, setter audit, restart needs      | done   |
| W1   | `pin_policy.h`, sanitizer and setter checks           | done   |
| W2   | Part A: `/pinassign`, Pins page section               | done   |
| W3   | Part B: `--gpio`, `--gpiocontrol`, runtime            | open   |
| W4   | Part B: web table, guard, `/gpiostatus`               | open   |
| W5   | RM1 `pins` and `gpio`, RM page, sender tool, vectors  | open   |
| Gate | `tools/regression.sh --stage 1,2`, default envs built | open   |

## Wave log

- **W0 (2026-11-07, `dd6f3523`):** `docs/gpio-pin-map.md`, 31 ESP32 envs. Setter audit: the nRF52 `--onewire gpio` path has no reserved-pin check; `--button gpio` re-inits the old pin, so a new button pin needs a reboot (`command_functions.cpp:2432`, left as is); `node_wind_pin` is not in the config import (`config_json.h`).
- **W1a (2026-11-07, `62fa55c4`):** `src/pin_policy.h`, `native_pin_policy` 9/9. Opus review of the free pins: Heltec V3 GPIO 7 refuted (ADC1 channel 6 in `init_batt()`), dropped. Free: T-Beam 13, 25; Heltec V3 2, 4, 5, 6.
- **W1b (2026-11-07):** the four console setters (onewire, button, analog, windpin) ask `pinCanAssign()` and print `<fn> gpio <n> refused: <class>`; `src/pin_board.h` builds the board context. Board mask split: HARD (radio, its SPI macros, Ethernet, SD) is repaired at load on ESP32 (`sanitize_loaded_settings`, not in the host build), SOFT (I2C, GPS, button, display reset, Vext, ADC control, battery, the Arduino SPI constants) is refused only when set, so existing setups keep working. Advisor: two rework rounds (alias macros that broke the T-Beam SX1262/68 and Supreme builds; GPS/battery and SPI-constant pins wiped at load). Gate: Unity 102 envs 3623/3623, stage 2 pass; builds of 12 envs green, RAK flash 82.6 %.
- **W2 (2026-11-07):** `GET /pinassign` (`src/pin_assign_json.h`, 512 B buffer, stack on ESP32, static on nRF52) and a "Pin assignments" section on the Pins page (per-row Set, refusal text, restart note; the nav button shows when any assignment is available). The `ok` mask mirrors the setters (`pinBoardSetterRefused()`: analog refuses 0, 1-Wire 0-2 under `BOARD_RAK4630`; button 99 = no button). The reserved lists moved out of `pin_board.h` into one TU, `src/pin_board.cpp` (ODR fix: `batt_functions.h` pulled the T-Echo pin header into every nRF52 TU, so since W1b the RAK4631 console refused WisBlock pins 17, 19-27). T114 radio pins (17, 19, 20, 22-25, hard-wired in `lora_t114_init()`) now explicit in `kHard`; T5 `BOARD_LORA_IRQ` added. `static_assert` regression checks for RAK pin 17 and the T114 radio pins (each seen failing before the fix). Advisor: two rework rounds. Gate: Unity 102 envs 3632/3632, stage 2 136 steps OK; T-Beam static RAM 107384 B, unchanged against `e64fe34e`, flash +2.5 KB; RAK flash 82.9 %; builds green: T-Beam, Heltec V3, RAK4631, T-Echo, T114, T5, E22 S3 N16R8, T-Beam Supreme. Deferred UI nits: a refused write shows two messages; analog stored 99 shows as 0. Follow-ups in BACKLOG: GPIO-02 (button setter), GPIO-03 (T5/RAK radio pins).

## Next: W3 (Part B console and runtime), recon 2026-11-07

Concept "Part B" and "Settings and contracts". Exclusive files: `src/gpio_control.*` (new), `src/command_functions.cpp`, `src/meshcom_settings.h`, `src/settings_schema.h`, `test/test_gpio_control/`, `platformio.ini` (new env only).

- **Setting:** `node_gpioctl` like `node_wind_pin`: `M(...)` row in the ESP32 block of `src/meshcom_settings.h` (~225-233), `X(...)` row in `src/settings_schema.h` (~133-139), asserts in `test/test_command_setters` (~2263); config import/export follows the schema. No free slot, no size assert.
- **Console on/off:** toggles table `COMMAND_TOGGLES` `command_functions.cpp` ~599, applied by `toggleApply` ~1173; `--help` text ~1637-1800 (checked by `tools/help_parity_lint.py`).
- **Policy:** `pinCanOutput()` `src/pin_policy.h:140`; `pinBoardContext()`, `pinBoardClaimed()` now in `src/pin_board.cpp`.
- **Test env:** copy `[env:native_pin_policy]` (platformio.ini ~2553) as `native_gpio_control`.
- **Runtime:** ESP32 periodic work via `loopAction_*` in `src/esp32/loop_actions_esp32.cpp`; Part B compiles away on nRF52 (symbol scan).
