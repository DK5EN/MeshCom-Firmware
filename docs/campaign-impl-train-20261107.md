# Campaign IMPL-TRAIN: the 12 impl rows of the release train

Opened 2026-11-07 on `fork-dev`. Source: [BACKLOG.md](BACKLOG.md) 3.2 "Release train", step 1. Approved by the
operator 2026-11-07.

**Scope:** pure coding run. Proof is host tests and board builds only. No flash, no serial port, no bench; every
bench step stays an open `bench` row for the bulk bench later.

## Decisions (operator, 2026-11-07)

- SENS-04: DHT type goes into a spare byte of the settings struct if one exists (no nRF52 blob layout change);
  otherwise the writer reports and stops.
- CSMA-03: not in this train (nRF52 only, not provable without a bench). Stays an `impl` row.
- SB-06: stricter check = chip id, segment count, app-desc magic word. No SHA-256. Project-name check dropped
  after the W1 advisor (every Arduino build carries "arduino-lib-builder"; a framework switch would make old
  Safeboots refuse every new app). Chip and segment checks stay for the readable log reason.
- CLK-01: W0 only. Its native tests land deactivated (`TEST_IGNORE`) until the fix wave.
- GPIO-06: T-Deck Pro only; `t_deck` and `t_deck_plus` keep their define.

## Waves

| Wave | Rows                                                                                       | Status |
| ---- | ------------------------------------------------------------------------------------------ | ------ |
| W1   | BL-01, IDEE-04, BLE-SET-01, GPIO-06, RM-NOANS-01, TEST-OTA-01, SB-06, CLK-01 host tool     | done   |
| W2   | SENS-04, CSMA-01, CLK-01 core (tick and display-gate helpers, deactivated tests)           | done   |
| W3   | CLK-01 instrumentation (`[CLOCK]`, `[DISPCLK]`, `--timesrc`, `--clockstat`, shots)         | done   |
| W4   | GOLDEN-MACROS-01 (orchestrator: regenerate golden, add check to `test/golden/selftest.sh`) | open   |

Per wave: `ls` of expected files, diff read, `tools/regression.sh --stage 1,2`, every touched board env built
sequentially, Opus advisor pass (W1-W3), commit with explicit paths, this doc and BACKLOG updated.

## File ownership

| Wave | Writer           | Exclusive files                                                                                                                                                                                                                                                                                    |
| ---- | ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W1   | BL-01            | `src/command_functions.cpp`, `src/prod_lock.h`, `src/loop_functions.cpp`, `src/loop_functions_extern.h`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/web_functions/web_functions.cpp`, `test/test_command_toggles/`, `test/test_prod_lock/`, nbrreport docs and identity testdata |
| W1   | IDEE-04          | `src/maxhop.h` (or new `src/hop_clamp.h`), `src/lora_functions.cpp` (relay gate only), `test/test_maxhop/`                                                                                                                                                                                         |
| W1   | BLE-SET-01       | `src/nrf52/nrf52_ble.cpp`, `test/test_nrf52_settings_paths/`                                                                                                                                                                                                                                       |
| W1   | GPIO-06          | `variants/t_deck_pro/configuration.h`, `src/pin_board.cpp`, `src/pin_board.h`, `src/pin_policy.h`, `test/test_pin_status/`, `test/test_pin_policy/`, `test/golden/native/variant-macros.txt`                                                                                                       |
| W1   | RM-NOANS-01      | `src/rm_sender_policy.h`, `src/rm_client_cmd.h`, `test/test_rm_sender_policy/`, `tools/webgui_rm_test.js`, `docs/rm-gui/impl-plan.md`                                                                                                                                                              |
| W1   | TEST-OTA-01      | `tools/bench/test_ota_abort.py`, `tools/bench/ota_abort.py`                                                                                                                                                                                                                                        |
| W1   | SB-06            | `src/safeboot/main.cpp`, `docs/safeboot-ota-contract.md`                                                                                                                                                                                                                                           |
| W1   | CLK-01 host tool | new `tools/bench/clock_drift.py`, new `tools/bench/test_clock_drift.py`                                                                                                                                                                                                                            |
| all  | orchestrator     | `platformio.ini`, `tools/regression.sh`, `test/golden/selftest.sh`, `test/golden/native/variant-macros-effective.txt`, `docs/BACKLOG.md`, `docs/RESUME.md`, this doc                                                                                                                               |

Board env per writer (one pio at a time through a lock wrapper): BL-01 `heltec_wifi_lora_32_V3`, BLE-SET-01
`wiscore_rak4631`, GPIO-06 `t_deck_pro`, SB-06 `esp32-safeboot` and `esp32-S3-safeboot`. Everything else builds at
the gate.

## Bench rows this train leaves open

SB-06 (wrong image on one board), CLK-01 W1/W3 (before/after drift), CSMA-01 (repeat of run C), GPIO-06 (T-Deck Pro
keyboard), SENS-04 (DHT22 sensor), BLE-SET-01 (nRF Connect shows no 0xF0A1).

## Bench session after W3 (operator, 2026-11-07)

One flash after W3, INSTRUMENT build, USB: Heltec V3 = DK5EN-1, T-Deck = DK5EN-14, T-Beam = DK5EN-92 (all with
WLAN/NTP; `identity_guard.py` and `/bench` first). Checks: CLK-01 W1 before-measurement (`clock_drift.py`, OLED
shots), TD-THEME-01 W4 (light/dark, lock glyph, boot logo), boot screen (operator by eye). F1 needs one DM between
own bench nodes, at most 2 dBm, approved; no broadcast.

## W1 result (2026-11-07)

Host gate green after two pin updates (toggle table 89 to 86 rows, `bMESH` sites in `loop_functions.cpp` 3 to 2),
all 33 board envs build. Advisor: first pass NOT APPROVED (SB-06 name check, `clock_drift.py` chunked read, locked
count in docs), rework applied by the orchestrator, second pass APPROVED. SB-06 name check dropped by the operator.
Follow-ups found by the advisor are backlog rows BL-01-FU, BLE-SET-01-FU, GPIO-08, SB-07, CLK-TOOL-01, RM-TXT-02.

## W2 result (2026-11-08)

Host gate green after regenerating `test/golden/native/variant-ini-effective.json` for the two new native envs, all
board envs build. Advisor: first pass REWORK (the CLK refactor had removed the 15 s redraw gate of `sendDisplayTrack`,
the track page; restored by the orchestrator with a source-pin test that failed before and passes after), second gate
run green. Follow-ups found by the advisor are backlog rows CSMA-05 and SENS-05.

Plan deviation: SENS-04 stores the DHT type in a new keyed member (`node_dht`) instead of a spare byte, because none
exists; the keyed nRF52 store and ESP32 NVS are layout-independent and the frozen BLE v1 blob is untouched.

## W3 result (2026-11-08)

CLK-01 W0 part 2, INSTRUMENT builds only (normal builds unchanged; RAM identical, flash within the known ESP32
non-reproducibility). Advisor first pass REWORK: `--timesrc cet` never applied `{CET}` (RTC/NTP flags blocked it),
`delta_ms` overflowed a 32-bit print, `--tdshot` did not feed the task watchdog, skip-reason doc mismatch. A fresh
writer at high effort fixed these plus three bench-relevant points (no RTC write-back while the gate is active,
free-running clock forced on RTC boards with GPS fix, info lines not counted as malformed by `clock_drift.py`).
Second pass APPROVED. Build INSTRUMENT images with `PLATFORMIO_BUILD_FLAGS="-DINSTRUMENT_ENABLED=1"`.
