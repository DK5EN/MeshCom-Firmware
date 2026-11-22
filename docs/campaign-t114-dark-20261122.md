# Campaign: T114 dark theme and display stays off on messages (#1314, T114-DARK-01)

Upstream issue #1314 (enhancement, filed 2026-11-21 from a T114 user report, discussion #815). Base `fork-dev`
`cb446de2`.

## Operator decisions 2026-11-22

- D1 Theme: the T-Deck command `--theme dark|light` also on the T114; default light (today's look); `--theme auto`
  means light on the T114 (no light sensor). Stored as bit `node_sset2` 0x2000 (free, only commented-out readers).
- D2 Wake: the option applies to every display board (OLED, TFT, T-Echo, e-paper, T114): one gate in the shared wake
  path `sendDisplayText()` (`loop_functions.cpp` ~:3176). Default on = today (a message lights the display).
- D3 Command name `--msgwake on|off`; stored as `node_sset` 0x8000 set = msgwake off (so the default bit 0 keeps
  today's behaviour). The message is still stored and readable by button or app.

## Operator decisions 2026-11-22 (after the first advisor pass)

- D4 Legacy bit: `node_sset2` 0x2000 was `--checkmesh` in v4.35p; a T114 upgraded from 4.35p with checkmesh on boots
  dark. Accepted; a note in the PR body, no code.
- D5 `--msgwake` gets a toggle on the web setup page (ESP32 and RAK web); the phone app follows later (no BLE work
  here).

## Wave 1

| Writer            | Exclusive files                                                                                                                                                                                                                                                               | Does                                                                  |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| A theme           | `src/t114_theme.h` (new), `src/tft_display_functions.cpp` (T114 branch), `src/nrf52/t114_gps_page.cpp`, `test/test_t114_theme/`                                                                                                                                               | palette light/dark, every T114 colour site reads it                   |
| B wake + settings | `src/loop_functions.cpp`, `src/loop_functions_extern.h`, `src/display_clock_gate.h`, `src/command_functions.cpp`, `src/nrf52/nrf52_main.cpp`, `src/esp32/esp32_main.cpp`, `src/config_json.h`, `test/test_display_clock/`, `test/test_command_setters/`, `test/golden/*lint*` | globals, wake gate, `--theme` on T114, `--msgwake`, persistence, help |

Interface: `bool bDisplayDark` (T114 dark, default false) and `bool bMsgWake` (default true), defined in
`loop_functions.cpp`, extern in `loop_functions_extern.h`. Orchestrator: `[env:native_t114_theme]` in `platformio.ini`.

Gate: `tools/regression.sh --stage 1,2`, builds heltec_t114, heltec_wifi_lora_32_V3, t_echo, wiscore_rak4631,
t_deck_plus, wireless-paper; Opus advisor. Bench: `--msgwake off` on DK5EN-1 (OLED); T114 colours not benchable (no
T114).

## State

Done 2026-11-22: `57acc7c3`. Two writers (palette; settings + wake gate), orchestrator gated the T-Deck wake site
(`msg_focus_and_alert`) and added the web setup toggle (D5) and the info-page line. Advisor round 1 (REWORK: OLED blink,
e-paper double refresh, Wireless Paper first click) -> fresh Sonnet-high rework: store-only mode `bDispStoreOnly`
(no push, no update(), no backlight; ring filled by `dispStoreRingLines`); round 2 (REWORK: WP pending status step) ->
orchestrator: `WpBrowseState.arrived` / `wpArrived`; final check APPROVED. Gate: Unity 122 envs 4149/4149, pytest 1639,
selftest 48, 11 board envs (`tools/bench/runs/harness/gate-wave3fix-wt114.summary`). Bench: `--msgwake off` PASS on
DK5EN-1 (`bench-t114dark-20261122.summary`). Field: T114 colours, Wireless Paper store-only, T-Deck panel.
PR notes: D4 (old `--checkmesh` bit), T5_EPAPER and T-Deck Pro bypass the gate (no display-off there), BLE/app not
exposed yet (app follows).
