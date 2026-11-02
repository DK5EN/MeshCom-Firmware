# Campaign: backlog sweep 2026-11-02

Resume point. Cloud session on branch `claude/jolly-davinci-bnrz95`, no bench hardware. Every row of
`BACKLOG.md` section 3.2 was triaged with the operator; the host-only items run here as waves, bench
checks stay with the operator.

## Operator decisions (2026-11-02)

- GPS-05b: tau stays 30 min, row closed (archive).
- BL-05: accepted risk like `N-02`, no rate limit, row closed (archive).
- SENS-03: a detected DHT on the one-wire path is reported as plain T/H (`hasT`/`hasH`), no new wire field.
- CSMA-04: timestamps only in INSTRUMENT builds (fork-only), default build unchanged.
- TD-32: real BLE toggle on the T-Deck Pro UI (not a refusal of `--ble off` over BLE).
- No upstream PR in this campaign.

## Waves and file ownership

| Wave | Agent | Items | Exclusive files | Status |
|---|---|---|---|---|
| W0 | orchestrator | TD-31, BL-02, backlog hygiene (TD-24 status, UCH-01/GPS-05b/BL-05 archived, escaped `\|` in TD-30/WEB-LED-01/AU-21) | `src/t-deck/lv_obj_functions.cpp` (2 lines), `src/lora_functions.cpp` (RAK RX copy), docs | done |
| W1 | A | PF-T1 | `test/test_pos_filter/` | running |
| W1 | B | MCC-T1 | `tools/tests/test_mc_console.py` | running |
| W1 | C | WEB-DARK-02 | chart colours in `src/web_functions/web_functions.cpp`, `tools/webgui_theme_test.js` | running |
| W1 | D | TZ-02 | `src/config_json.cpp`, its native test | running |
| W1 | E | SENS-03 | `src/rm_exec_read.cpp`, `src/rm_format.h`, one-wire driver, `test/test_rm_*` | running |
| W1 | F | TD-32 | `src/t-deck-pro/ui_deckpro.cpp`, `src/t-deck/event_functions.cpp` | running |
| W1 | G | CSMA-04 | `src/lora_functions.cpp`, `src/nrf52/nrf52_main.cpp` | running |

Gate: `tools/regression.sh --stage 1,2` (known red in the container: PF-T1 until W1-A lands), builds of the
touched envs sequentially through a pio lock, artifact markers, Opus advisor on the W1 diff.

## Wave log

- W0 (2026-11-02): TD-31 (`setup_name`/`setup_comment` padding on their own fields), BL-02 (RAK RX double
  buffer: tail zeroed after the copy), backlog hygiene. PlatformIO installed in the container.
