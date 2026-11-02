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
| W1 | A | PF-T1 | `test/test_pos_filter/` | done |
| W1 | B | MCC-T1 | `tools/tests/test_mc_console.py` | done |
| W1 | C | WEB-DARK-02 | chart colours in `src/web_functions/web_functions.cpp`, `tools/webgui_theme_test.js` | done |
| W1 | D | TZ-02 | `src/config_json.cpp`, its native test | done |
| W1 | E | SENS-03 | `src/rm_exec_read.cpp`, `src/rm_format.h`, one-wire driver, `test/test_rm_*` | done |
| W1 | F | TD-32 | `src/t-deck-pro/ui_deckpro.cpp`, `src/t-deck/event_functions.cpp` | done |
| W1 | G | CSMA-04 | `src/lora_functions.cpp`, `src/nrf52/nrf52_main.cpp` (+ orchestrator: ESP32 stamps in `src/esp32/esp32_main.cpp`) | done |

Gate: `tools/regression.sh --stage 1,2` (known red in the container: PF-T1 until W1-A lands), builds of the
touched envs sequentially through a pio lock, artifact markers, Opus advisor on the W1 diff.

## Wave log

- W0 (2026-11-02): TD-31 (`setup_name`/`setup_comment` padding on their own fields), BL-02 (RAK RX double
  buffer: tail zeroed after the copy), backlog hygiene. PlatformIO installed in the container.

- W1 (2026-11-02): PF-T1, MCC-T1, WEB-DARK-02, TZ-02, SENS-03, TD-32, CSMA-04 (7 Sonnet implementers in parallel).
  Orchestrator at the gate: `+<tz_rule.cpp>` in `[env:native_config]`, ESP32 CSMA-04 stamps, WiFi header icon through
  `tdeck_wifi_set()`, `ds18_found` extern into `loop_functions_extern.h`, PF-T1 bound changed from "mean distance < 4 m"
  (a raised limit) to "systematic offset < 2 m" (the original limit on the right statistic, 1.08 m observed).
  Advisor (Opus): 1 blocking (theme test red: `border-color:'+colors` counted as a text colour, light segments judged
  against the page) + 2 optional (MCC-T1 bare line end, SENS-03 `dht_found` is static type info) -> all three fixed by
  the orchestrator (few lines each: regex anchored, chart segments judged against `--mcempty` in dark, `dht_ok`).
  Gate 1: Unity 95 envs 3443 cases all green (PF-T1 green in the container for the first time); selftest extern lint
  caught the two-declarator CSMA-04 extern (split); pytest red only from the container (shallow clone, no de_DE locale,
  both fixed). New: SENS-04 (DHT22/21 never detected).
  Lesson: one pio lock for 7 agents plus a cold platform download serialised the wave for about 45 min; two agents
  handed back mid-queue. Next cloud wave: warm the cache (one build per platform) before dispatch.

## Bench checks for the operator

- TD-32: T-Deck Pro BLE checkbox (position x=158 next to SAVE, off/on), T-Deck WiFi header icon off/on.
- WEB-DARK-02: Neighbours/queue chart in Chrome dark mode.
- SENS-03: RM `sens` with a DHT11 and with a DS18B20 on the one-wire pin.
- CSMA-04: INSTRUMENT images on the three bench nodes, `--setlog on`, matched run with LORADEBUG off.
