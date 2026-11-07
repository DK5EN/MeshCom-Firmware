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
| W2   | Part A: `/pinassign`, Pins page section               | open   |
| W3   | Part B: `--gpio`, `--gpiocontrol`, runtime            | open   |
| W4   | Part B: web table, guard, `/gpiostatus`               | open   |
| W5   | RM1 `pins` and `gpio`, RM page, sender tool, vectors  | open   |
| Gate | `tools/regression.sh --stage 1,2`, default envs built | open   |

## Wave log

- **W0 (2026-11-07, `dd6f3523`):** `docs/gpio-pin-map.md`, 31 ESP32 envs. Setter audit: the nRF52 `--onewire gpio` path has no reserved-pin check; `--button gpio` re-inits the old pin, so a new button pin needs a reboot (`command_functions.cpp:2432`, left as is); `node_wind_pin` is not in the config import (`config_json.h`).
- **W1a (2026-11-07, `62fa55c4`):** `src/pin_policy.h`, `native_pin_policy` 9/9. Opus review of the free pins: Heltec V3 GPIO 7 refuted (ADC1 channel 6 in `init_batt()`), dropped. Free: T-Beam 13, 25; Heltec V3 2, 4, 5, 6.
- **W1b (2026-11-07):** the four console setters (onewire, button, analog, windpin) ask `pinCanAssign()` and print `<fn> gpio <n> refused: <class>`; `src/pin_board.h` builds the board context. Board mask split: HARD (radio, its SPI macros, Ethernet, SD) is repaired at load on ESP32 (`sanitize_loaded_settings`, not in the host build), SOFT (I2C, GPS, button, display reset, Vext, ADC control, battery, the Arduino SPI constants) is refused only when set, so existing setups keep working. Advisor: two rework rounds (alias macros that broke the T-Beam SX1262/68 and Supreme builds; GPS/battery and SPI-constant pins wiped at load). Gate: Unity 102 envs 3623/3623, stage 2 pass; builds of 12 envs green, RAK flash 82.6 %.

## Next: W2 (Part A web), recon 2026-11-07

- **Pins page:** `sub_page_pins()` `src/web_functions/web_functions.cpp:4317-4341` emits only the frame between `PINS-HTML-BEGIN/END`; rows come from `src/web_functions/web/mc.js:238-316` (`PINS-JS-BEGIN/END`: `pnPoll` 5 s, `pnSet` posts `/setparam/?<param>=<v>`, `X-MC` added by the fetch wrapper at `mc.js:4`).
- **Routes:** `/?page=pins` `:1326`, `GET /pinstatus` `:1189` -> `sub_pin_status()` `:4350-4387` (pure `src/pin_status_json.h`, `PIN_STATUS_JSON_MAX` 2200, static buffer on nRF52). Add `GET /pinassign` the same way, own pure JSON header and buffer.
- **Setters:** `setparam` `web_functions.cpp ~:4776`; `onewiregpio` `web_setup.cpp:58`, `buttongpio` `:65`, `angpio` `:95` (`#if ANALOG_PIN`), `windpin` `:502-513` (`ENABLE_WIND`). Reply 200/422 with `{"returncode":N}`.
- **Restart needs (W0 audit):** 1-Wire, analog and wind apply live; button needs a reboot.
- **Tests:** `tools/webgui_pins_test.js` (jsdom, extracts the PINS blocks; stage 2 `tools/regression.sh:231`), `test/test_pin_status`. Assets: `python3 tools/gen_web_assets.py` regenerates `src/web_functions/web_assets.h` (`--check` in stage 2). API doc: `src/web_functions/Web-API_documentation.txt`.
- **Flags:** `bONEWIRE`, `bButtonCheck`, `bAnalogCheck`, `bWINDON` (`loop_functions.cpp`); wind only under `ENABLE_WIND` (ESP32).
