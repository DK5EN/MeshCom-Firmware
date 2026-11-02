# Campaign: open items of the 2026-11-02 bench session

Resume point for the multi-wave campaign started 2026-11-02 on branch
`claude/blissful-hamilton-vd8mbd` (cloud session, no bench hardware). Every wave is
committed and pushed; the bench checks listed at the end are done later by the operator.

## Operator decisions (2026-11-02)

- **WEB-LED-01:** steady-on is a **persisted** mode: three-way off / flash / steady,
  console `--board led off|on|steady` (`on` stays flash), Setup page, `--info`, Node Information.
- **TD-30:** BLE and WiFi icons toggle **live, without reboot**, both the same way, persisted.
  BLE must not be deinitialised at runtime (S3 crash, AU-19): "off" stops advertising and
  drops connections, the stack stays up.
- Everything lands on this branch; no upstream PR in this campaign.

## Waves and file ownership

| Wave | Agent | Items | Exclusive files | Status |
|---|---|---|---|---|
| W0 | orchestrator | BOOT-01, UCH-01 | `src/configuration_global.h` (macro), 9 title strings, web logo; `tools/webgui_rm_test.js` | done (`59b41a2`, `ffe097aa`) |
| W1 | A | TD-26 (TD-25 was already done, `e6b74074`) | `src/msg_input_budget.h`, `src/t-deck-pro/ui_deckpro.cpp`, `test/test_msg_input_budget/` | done |
| W1 | B | TD-27, TD-28, TD-29 | `src/t-deck/lv_obj_functions.cpp` (layout only) | done |
| W1 | C | WEB-DARK-01, WEB-A11Y-01 | CSS / nav in `src/web_functions/web_functions.cpp`, `web_login_page.cpp`, `web_rm_page.cpp`, `tools/webgui_theme_test.js` | done (1 rework round) |
| W1 | D | BAT-05, TXP-02 | `src/batt_pipeline.h`, `src/{esp32,nrf52}/udp_frame_*.cpp`, native tests | done |
| W2 | E | TD-30 | `src/t-deck/lv_obj_functions.cpp`, `src/t-deck/event_functions.cpp`, `src/esp32/esp32_main.cpp` (BLE on/off without deinit), `src/meshcom_settings.h` + settings store, `src/command_functions.cpp` | done (1 rework round) |
| W2 | F | BAT-04b | `src/esp32/loop_actions_esp32.cpp` (BQ branch in `loopAction_battCheck`), `src/batt_charge.h`, `test/test_batt_charge/` | done (1 rework round) |
| W2 | G | WEB-PWD-01 | `web_functions.cpp` Setup card + route, `web_rm_handlers.cpp` / `web_auth.cpp`, `tools/webgui_settings_test.js`, `test/test_web_auth/` | done (1 rework round) |
| W3 | H | WEB-LED-01 | `web_functions.cpp`, `web_setup.cpp`, `command_functions.cpp`, LED + settings, native test | done (1 rework round) |

Gate per wave: `tools/regression.sh --stage 1,2`, firmware builds of the touched envs
(sequential, through `piolock.sh`), artifact string check, Opus advisor for behavioural waves,
then commit + push and update this doc, `docs/RESUME.md`, `docs/BACKLOG.md`.

## Wave log

- W0 (2026-11-02): BOOT-01 (`MESHCOM_TITLE`, static_assert against `SOURCE_VERSION`), UCH-01
  (flag-0 path UCH-00; before: `FAIL UCH-01` + harness TypeError, after: all passed in both flags).
  Container gate baseline at `3bf9af0`: 5 red steps. Fixed: two GCC-only test build errors
  (`test_msgid_counter` missing `<cstdio>`, `test_udp_frame_twin` address of a temporary array).
  ENV, fixed in the container: de_DE locale, shallow clone (`fe8f10d0`), espressif32 platform.
  Open as PF-T1: `test_pos_filter` cadence bound under glibc; the cloud gate keeps it as the one known red.

- W1 (2026-11-02): TD-26, TD-27/28/29, WEB-DARK-01 + WEB-A11Y-01, BAT-05, TXP-02. Advisor sent back
  2 LVGL ordering/layout bugs (orchestrator fixed), the dark-mode inline colours and the theme-test registration
  (fresh implementer at high); re-pass APPROVED. Gate: Unity 95 envs 3409 cases, 1 failed = PF-T1 only;
  builds t_deck, t_deck_pro, heltec_wifi_lora_32_V3, wiscore_rak4631 green (clean, sequential); artifact markers
  present ("Invalid call - not sent", `prefers-color-scheme`, `nbr-me`, "MeshCom 4.4"). New: WEB-DARK-02, TD-31.
  Lesson: three agents and the orchestrator wrote `while pgrep -f "<pattern>"` waits that match themselves and
  never end; one held the pio lock and stalled the wave (rule 14 in the brief now).

- W2 (2026-11-02): TD-30, BAT-04b, WEB-PWD-01. Advisor: 3 must-fix (console `--wifi`/`--ble` only in
  INSTRUMENT builds, `/webpwd` outside the RM guard -> renamed `/rmwebpwd`, hand-written `save_settings`
  declaration wrong on nRF52) plus BATTPRES presence, AP-mode tap, BLE-off disconnect retry -> fresh
  implementer at high; re-pass APPROVED with one fix (0xFFFF failed read counted as a cell), done by the
  orchestrator with fail-before/pass-after. Gate: Unity 95 envs 3421 cases, only PF-T1; pytest 1 flake
  (MCC-T1, passes in isolation); builds t_deck, t_deck_plus, t_deck_pro, t5_epaper, heltec V3, ttgo_tbeam,
  wiscore_rak4631 green. New: TD-32, MCC-T1.

- W3 (2026-11-02): WEB-LED-01. Advisor: core correct; must-fix NeoPixel-only steady (one dark frame per
  second), missing steady-refusal assertion, two lints no longer saw `boardled` -> fresh implementer at high
  (fail-before/pass-after on `boardLedSteadyRearm`); re-pass APPROVED. Gate: Unity 95 envs 3427 cases, only PF-T1;
  golden selftest 48/48 after the toggle-table row count 88 -> 89; builds heltec V3, E22_1262_S3 (NeoPixel-only),
  wiscore_rak4631, t_deck, ttgo_tbeam green. Follow-ups noted in the row (BLE `BLED`
  bool, getparam value `flash` instead of `on`).

**Campaign complete 2026-11-02:** all 15 items done host-side and pushed; only the bench checks below remain.

## Bench checks left for the operator

- BOOT-01: boot screen on Heltec V3 OLED, a TFT board and the T-Deck Pro shows "MeshCom 4.4".
- TD-26: 9-char call DM on the T-Deck Pro, invalid destination shows the notice.
- TD-27/28/29: photos of the position, MHeard and Path tables, the DM field while typing 1-9 chars, the GPS page (DK5EN-14).
- WEB-DARK-01/WEB-A11Y-01: every page in Chrome with OS light and dark (Info, Setup, Remote, login, Neighbours, Spectrum).
- BAT-05: hot-plug a cell into a running Heltec V3 (DK5EN-1), expect "present" within about 2 min.
- TXP-02: UDP DM to the gateway call, then the LoRa copy: no second `GWU`.
- TD-30: tap BLE and WiFi icons on DK5EN-14: live off/on, phone drops on BLE off, state survives reboot, AU with BLE off does not re-enable it.
- BAT-04b: T-Deck Pro and T5 with and without a cell, on USB and on battery: `--info` charge state and current, BLE `IS2`.
- WEB-PWD-01: set, change, clear the web password in Chrome; the open session lands on the login page after a change.
- WEB-LED-01: Heltec V3 board LED off / flash / steady from the Setup page and console, survives reboot; an E22 S3 NeoPixel board stays white in steady; RM1 `led on/off` still overrides.
