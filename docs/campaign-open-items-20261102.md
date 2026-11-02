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
| W1 | A | TD-25 helper + T-Deck Pro, TD-26 | `src/msg_input_budget.h` (new), `src/t-deck-pro/ui_deckpro.cpp`, `test/native_msgbudget/`, env in `platformio.ini` (gate) | planned |
| W1 | B | TD-27, TD-28, TD-29 | `src/t-deck/lv_obj_functions.cpp` (layout only) | planned |
| W1 | C | WEB-DARK-01, WEB-A11Y-01 | CSS / nav in `src/web_functions/web_functions.cpp`, `web_login_page.cpp`, `web_rm_page.cpp` | planned |
| W1 | D | BAT-05, TXP-02 | `src/batt_pipeline.h`, dedup / UDP path, native tests | planned |
| W2 | E | TD-25 T-Deck side, TD-30 | `src/t-deck/lv_obj_functions.cpp` (+ BLE/WiFi runtime helpers) | planned |
| W2 | F | BAT-04b | T-Deck Pro / T5 poll hook | planned |
| W2 | G | WEB-PWD-01 | `web_functions.cpp` Setup card + route, `web_auth.cpp` / `web_setup.cpp`, tests | planned |
| W3 | H | WEB-LED-01 | `web_functions.cpp`, `web_setup.cpp`, `command_functions.cpp`, LED + settings, native test | planned |

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

## Bench checks left for the operator

- BOOT-01: boot screen on Heltec V3 OLED, a TFT board and the T-Deck Pro shows "MeshCom 4.4".
