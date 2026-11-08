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

## Status (2026-11-08, wave 1)

Wave 1 implemented by five writers, first advisor pass REWORK (blocking: F1 overwrote the message-page sender
header), operator decisions taken, rework by two fresh writers at high effort; second advisor pass and final gate at
the wave commit. Goldens regenerated with `MC_AU_DEV_CHANNEL=1` (only the two T-Deck Pro lines change).

Second advisor pass: all first-pass items fixed; two new findings. (1) The web clock re-sync fetched the current
page, so on the spectrum page it would start a radio scan: now always `?page=info`, pinned in `webgui_look_test.js`
(failed before, passes after). (2) "Track mode overwrites a pong page" rejected by the orchestrator: every
`DisplayPong()` caller sets `DisplayOffWait` (`loop_functions.cpp` ~:2828, :4192, :4207) and the track branch runs
only at `DisplayOffWait == 0` (~:2718).

Operator decisions during the wave:

- Message page: the clock stays frozen (type0 skip back); redraw on the page change.
- Track page: clock every second, page rebuild every 15 s. Ping: the clock must stay current (see next wave).
- Web colours lifted to the stray-light targets: light `--mcok` `#1A7733` (5.63:1), dark `--mcredbtn` `#AB1F36`
  (7.04:1); no KNOWN-LOW exemptions left.
- Web extras: ticking page clock (synced at render, re-sync after laptop sleep, PC clock only detects), lighter
  toggle OFF state (`#C8C8C8` track with `#767676` outline).

## Wave 2 (2026-11-08)

Operator interview: GPIO-09 remove; CSMA-03 threshold decided by the bench; SENS-05 schema tightened now;
`clock_drift.py` two reboots per run accepted.

| Item        | Result                                                                                                              |
| ----------- | ------------------------------------------------------------------------------------------------------------------- |
| CLK-PING-01 | A configured ping no longer freezes the clock. The clock line skips (`hold`) only while a drawn held frame is shown |
|             | (`dispClkHeldFrame()`: `DisplayOffWait` running and `bHeldFrameDrawn`, set by `DisplayPong` and the hold text).     |
|             | `sendDisplayTime()` and `mainStartTimeLoop()` unconditional; ESP32 hold-expiry `#C` clear no longer ping-gated.     |
| SENS-05     | Schema allowed set: `node_dht` outside {11, 22} decodes to 11 (`FieldDescriptor::allowed_values`); nRF52 logs and   |
|             | rewrites the corrected store (`values_substituted`). ESP32 NVS does not use `decode()`; the load sanitiser stays.   |
|             | `node_dht` is persist-only, so JSON config import ignores it (pinned by a test).                                    |
| GPIO-09     | The eight T-Deck copies removed from the Pro variant; `#error` guard in `pin_board.cpp`; golden updated.            |

Two Sonnet writers, advisor REWORK twice (pageHold countdown frozen by a call-site gate; boards whose `DisplayPong`
draws nothing), first rework by two fresh writers at high, second by the orchestrator. Every fix has a test that fails
before and passes after.

## Wave 3: bulk bench (2026-11-08, running)

INSTRUMENT builds of `90ece48c` on DK5EN-1, -92, -14 (OTA) and DK5EN-90 (`-DCSMA_CAD_MAX_AGE_US=0`). Operator
decisions: W1 "before" S2-S4 skipped; T-Deck clock run over USB after CSMA-03 (RAK unplugged); GPIO-06/08/09 stay
open (no T-Deck Pro); S4 runs with a new explicit `--offline-ok` waiver (`identity_guard.py`, `clock_drift.py`:
WiFi/web/clock waived, callsign and TX power still checked, `identity_waiver` in the summary).

| Run                                      | Result                                                                                                                                                                                |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| B2 S1+S2 Heltec/T-Beam (`145740`)        | Clock drawn every second, no `gate15`; message page frozen by design (`type0`, ~150 s), redrawn on page change; drift < 0.5 s                                                         |
| A4 T-Beam                                | OLED frame 37.6 ms at 1 Hz = 3.8 % loop time (limit 5 %)                                                                                                                              |
| CLK-PING (`151342`)                      | Pongs arrived (4/4 shown); 5th ping dropped by DK5EN-90 as `RX_DEDUP_DUP` -> PONG FAILED. Clock frozen 158 s because ping/pong frames are held `pingtime` and repeat every `pingtime` |
| CSMA-03 (`csma03-rak90-20261108-151137`) | Free CAD result always 60-97 ms old; cause `delay(100)` per nRF52 loop pass, see `docs/rca-csma03-cad-age-20261108.md`                                                                |
| Web (DK5EN-14)                           | Operator review: segments one row auto-light-dark on the label line, green selection/slider, toggles keep outline, short T-Deck labels; advisor rework (pins page) applied            |
| S3 T-Beam / T-Deck, S4 Heltec            | running                                                                                                                                                                               |

Rejected: CLK-PING-02 (clear hold on a ping without redraw) -- on ESP32 that ping follows `PongFail()`'s fresh
FAILED frame; diff kept out of the tree. RCA `docs/rca-pong-rx-01-20261108.md`: pong premise false.

Operator decisions after the bench findings: ping/pong frames held 10 s then clock (PING-HOLD-01); nRF52 ping budget
(`--ping stop`, `--pingmax`, PongFail; PING-NRF-01); CSMA-03 loop `delay(1)` while a CAD is pending, then re-bench
DK5EN-90 with threshold 0 and set the threshold from the new p99; dedup of ping id ...803: bench check
(`--msgid` across a reboot on DK5EN-1).

Wave 4 writers (running): PING-HOLD-01 (`loop_functions.cpp` ping/pong display, `display_clock_gate.h`,
`test_display_clock`); PING-NRF-01 + CSMA-03 delay (`ping_budget.h`, `esp32_main.cpp` ping scheduler,
`nrf52_main.cpp` ping section and final delay, `csma_timing.h`, `test_ping_budget`, `test_csma_timing`).
Orchestrator: `platformio.ini` (`test_ping_budget` registered), web files, tools, docs.

## Next wave (resume here)

1. **Bench (bulk bench):** CLK-01 W1 rest (S2, S3, S4) and W3 after-run incl. A4 OLED frame time with
   `--oledlog on`; clock on a pinged ESP32 OLED node (CLK-PING-01, `[DISPCLK];skip;hold` only during the pong text);
   CSMA-03 on DK5EN-90 (count `CAD_STALE_RECHECK`, then set `CSMA_CAD_MAX_AGE_US`); GPIO-06/08/09 on a T-Deck Pro;
   web GUI look in Safari (T-Deck controls, page clock, toggles, theme/font switch); T-Deck green and setup page on
   DK5EN-14.
2. Release only after the backlog is complete (operator); upstream PRs later as a bundle.
