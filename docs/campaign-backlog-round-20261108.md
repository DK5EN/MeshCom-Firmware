# Campaign BACKLOG-ROUND: CLK-01 fix, web controls and the eight follow-ups

Opened 2026-11-08 on `fork-dev`. Operator decisions 2026-11-08 (interview, one item at a time): everything below now;
release only after the backlog is complete; upstream PRs decided later as a bundle (visible frontend plus backend).

## Scope

| Item          | Decision                                                                                        |
| ------------- | ----------------------------------------------------------------------------------------------- |
| CLK-01 W2     | Fix F1 (OLED clock frozen on non-clock pages), F2, F3 (free clock loses ticks); red tests green |
| WEB-TD-01     | Web setup: T-Deck font size, theme, display mode, brightness; web GUI contrast audit            |
| BL-01-FU      | Stale `nbrreport` mentions and retired `node_sset4` bits                                        |
| BLE-SET-01-FU | Remove the dead nRF52 BLE settings path and its pinning scrapes                                 |
| GPIO-08       | T-Deck Pro SD CS 48, touch INT 12; `static_assert` that GPS_EN stays reserved                   |
| SB-07         | Remove the dead `SAFEBOOT_APP_VALID_VIA_SET_BOOT_PARTITION` path                                |
| CLK-TOOL-01   | Harden `clock_drift.py`, refusal tests                                                          |
| RM-TXT-02     | RM waiting text "up to 3 minutes"                                                               |
| CSMA-05       | CSMA-01 leftovers a-d                                                                           |
| SENS-05       | DHT type sanitised on load, invariant comment, web 1-Wire row, help padding                     |
| CSMA-03       | nRF52 CAD result acted on before TX; bench proof on DK5EN-90 in the bulk bench                  |

Web contrast targets (stray light on user LCD/TFT panels): body text >= 7:1, secondary >= 5.5:1, outlines and
controls >= 3:1. Pairs below target are reported to the operator before the web palette changes.

## Wave 1: five writers in parallel

| Writer | Items                                             | Exclusive files (build env)                                                                                                                                                                                                                                                                                                                                                                                                   |
| ------ | ------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CLK    | CLK-01 W2, BL-01-FU in `loop_functions.cpp`       | `src/clock.cpp`, `src/clock.h`, `src/clock_tick.h`, `src/display_clock_gate.h`, `src/clock_instrument.cpp`, `src/loop_functions.cpp`, `test/test_display_clock/`, `test/test_clock_tick/`, `tools/tests/test_mesh_policy_sites.py` (`heltec_wifi_lora_32_V3`)                                                                                                                                                                 |
| RADIO  | CSMA-03, CSMA-05, BL-01-FU comments in both mains | `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/txring_functions.cpp`, `src/lora_functions.cpp`, `src/csma_timing.h`, `test/test_txring/`, `test/test_csma_timing/`, new CSMA-03 test (`wiscore_rak4631`, `ttgo_tbeam`)                                                                                                                                                                                          |
| FW     | BLE-SET-01-FU, GPIO-08, SB-07, RM-TXT-02          | `src/nrf52/nrf52_ble.cpp`, `src/nrf52/WisBlock-API.h`, `src/nrf52/ble_settings_v1.h`, `src/nrf52/ble_settings_stage.h`, `test/test_nrf52_settings_paths/`, `test/test_command_setters/`, `variants/t_deck_pro/configuration.h`, `src/pin_board.cpp`, pin tests, `src/safeboot/main.cpp`, `docs/safeboot-ota-contract.md`, `src/rm_sender_policy.h`, `tools/webgui_rm_test.js` (`heltec_t114`, `t_deck_pro`, `esp32-safeboot`) |
| WEB    | WEB-TD-01, SENS-05 (c), BL-01-FU web text         | `src/web_functions/web_functions.cpp`, `src/web_functions/web_setup.cpp`, `src/web_functions/web/*`, `src/web_functions/web_assets.h` (generated), `tools/webgui_*_test.js` (`E22-DevKitC`)                                                                                                                                                                                                                                   |
| MISC   | SENS-05 (a, b, d), BL-01-FU rest, CLK-TOOL-01     | `src/settings_sanitize.*`, `src/esp32/esp32_flash.cpp`, `src/nrf52/nrf52_flash.cpp`, `src/onewire_functions.cpp`, `src/command_functions.cpp`, `src/prod_lock.h`, `tools/nbrlog.py`, `tools/testdata/nbr/`, `docs/presentation/Nachbarschaftsmatrix.html`, `docs/nbr-*.md`, `tools/bench/clock_drift.py`, `tools/bench/test_clock_drift.py` (`ttgo_tbeam_supreme`)                                                            |

Orchestrator: `platformio.ini`, goldens and lints, `docs/BACKLOG.md`, `docs/RESUME.md`, this doc; hotspot edit: the
`applyPendingBleSettings` call in `nrf52_main.cpp` (~:2011) at the gate, after RADIO returns.

Every `pio` call goes through the scratchpad lock wrapper; each writer runs only its own native suites and build envs.

## Gate

`ls` of new files, diff read, `tools/regression.sh --stage 1,2`, every board env, Opus advisor, commit per writer
group. Bench parts (CLK-01 W1 rest and W3 after-run, CSMA-03 on DK5EN-90, GPIO-06/08 on a T-Deck Pro) join the bulk
bench.

## Status

| Writer | Status  |
| ------ | ------- |
| all    | planned |
