# Battery "no cell" detection on switched-divider boards -- campaign 2026-10-01

Source: Finding 1 of `docs/soak-20260929-verdict.md`. A Heltec V3 without a cell reports
`/B=100` again since the divider is switched on for only 100 ms every 30 s (DK5EN-1: 95/104
beacons, live 4.21-4.68 V). Operator decisions 2026-10-01: measure first, then choose the
discriminator; a Heltec with a real cell is available for the reference run.

Ruled out: a divider on/off step check. The GPIO37 switch sits between battery terminal and
ADC; DK5EN-1 without a cell shows the full step at every boot (`high=886-991 low=1`).

## Wave status log

| Wave | Content                                                                    | Status  |
| ---- | -------------------------------------------------------------------------- | ------- |
| 0    | `--battprobe` instrument command (burst / pairs / long), build             | done    |
| 0h   | flash DK5EN-1 instrument image, capture no-cell, then with-cell (operator) | done    |
| 1    | window-spread rule: pipeline API + tests (orchestrator), writers B and C   | running |
| 1g   | gate: all native envs, board builds, advisor pass, commit                  | pending |
| 2    | flash bench, verify `/B=` absent without cell and present with cell        | pending |

## Wave 0 ownership

| Writer | Files                                                                            |
| ------ | -------------------------------------------------------------------------------- |
| A      | `src/batt_function_old.cpp`, `src/batt_functions.h`, `src/command_functions.cpp` |

## Wave 1 ownership

| Writer       | Files                                                                          |
| ------------ | ------------------------------------------------------------------------------ |
| orchestrator | `src/batt_pipeline.h`, `test/test_batt_pipeline/` (API + 8 tests, 67/67 green) |
| B            | `src/batt_function_old.cpp` (Heltec V3/V4/Stick reader, detector feed)         |
| C            | `src/batt_functions.cpp`, `src/batt_functions.h`, `test/test_batt_detect/`     |

Rule (BAT-03): after the 100 ms settle, 8 reads 2 ms apart; a spread above 300 mV marks the
sample implausible; the reported value stays the first read. Streaks unchanged (absent after 6
samples = 3 min, present after 10 = 5 min).

`--battprobe` is instrument-only (`INSTRUMENT_ENABLED`) and is not part of a release image.

## Measurements

Raw logs: `~/meshlog/battprobe-20261001/` on the Mac. Factor `ADC_MULTIPLIER` 4.18 mV/count.

**DK5EN-1, no cell, USB (2026-10-01 20:30, instrument build 20:27:17), 3 cycles:**

| Mode                               | min mV                     | max mV | mean mV   | sd mV   |
| ---------------------------------- | -------------------------- | ------ | --------- | ------- |
| BURST, 2 ms grid, t 100-300 ms     | 3632                       | 4907   | 4192-4206 | 364-369 |
| LONG, 100 ms grid, divider on 3 s  | 3703                       | 4974   | 4256-4275 | 347-363 |
| PAIRS, 4 reads 1 ms apart / window | means 3912-4342 per window |        |           |         |

The charger output without a cell is a sawtooth with a ~6 ms period (BURST: 985, 887, 1070,
970, 879, 1068 ...), 3.6 to 4.9 V. Inside ONE 100 ms window, 8 reads 2 ms apart span > 1 V.
A single read lands at a random phase, mostly inside the 2.31-4.83 V band, which is why the
detector says "present".

**DK5EN-1 with a LiPo attached (same image), 3 + 10 cycles:**

| Mode                               | min mV                                                      | max mV | mean mV   | sd mV |
| ---------------------------------- | ----------------------------------------------------------- | ------ | --------- | ----- |
| BURST, 2 ms grid, t 100-300 ms     | 3929                                                        | 3979   | 3956-3959 | 4-5   |
| LONG, 100 ms grid, divider on 3 s  | 4009                                                        | 4046   | 4035-4038 | 4-6   |
| PAIRS, 4 reads 1 ms apart / window | spread 16-27 counts (67-113 mV), first read ~22 counts high |

Spread of 8 reads 2 ms apart from t >= 100 ms: no cell 819-1267 mV, cell 0-50 mV. One
transient right after plugging the cell in (first run, cycle 3) showed the sawtooth for ~1.5 s;
the absent streak (6 windows = 3 min) absorbs it. The first read after an idle ADC reads ~2 %
higher than back-to-back reads (sample-and-hold on the 390k/100k divider), so the threshold is
set well above the 113 mV worst case and well below the 819 mV no-cell minimum: 300 mV.

## Gate notes (1g)

- Advisor (Fable): APPROVED for src/. Must-fix M1/M2 were test-comment honesty items, applied
  (kNoCellSeries is LONG cycle 1 on a 100 ms grid; kCellWindowMv now reproduces the 113 mV worst
  case). O1 applied: `--battprobe` resets the battery scheduler so a READ pending from before the
  probe does not read the released divider. Open optional: O2 T114 has the same topology but no
  detector (not in scope); O3 the threshold rests on one Heltec V3, V4/Stick/E213/Wireless Paper
  unmeasured; O4 the `[BATT];` csv line gains a `spread` column on ADC_CTRL_PIN boards.
- Two pre-existing golden findings from 50fdfe65/0713f47a fixed on the operator's call:
  `--ethmtu` rung and its web `/setparam` producer are now RAK-only like the help line;
  `test/golden/native/variant-ini-effective.json` regenerated (adds `test_tft_backlight` only).

Gate 2 (after the advisor fixes): 48/48 native envs, 1614 cases; golden selftest OK; builds OK
for heltec_wifi_lora_32_V3/V4, heltec_wireless_stick, wiscore_rak4631, vision-master-e213,
wireless-paper, t_deck, E22-DevKitC, T-ETH-ELITE_1262, ttgo_tbeam. Markers: V3 image carries
`window spread mV`, E213 image `spread:;`, the V3 release image has no `BATTPROBE`.

## Hardware result (wave 2)

DK5EN-1 on the release-type image (build 2026-10-01 21:08:31), 2 dBm, `--debug on` +
`--setcont on` for the run (both switched off afterwards). Logs:
`~/meshlog/battprobe-20261001/dk5en-1-fix-{cell,nocell}.txt`.

| Case          | Production samples (30 s apart) | Window spread mV | `--info` BATT                                                    |
| ------------- | ------------------------------- | ---------------- | ---------------------------------------------------------------- |
| LiPo attached | 13                              | 75-100           | 4.04 V / 80-81 % throughout                                      |
| cell removed  | 14                              | 861-970          | 4.18-4.75 V for the 6-sample streak, then 0.00 V / 0 % and stays |

PASS on the Heltec V3. Not measured on hardware: Heltec V4, Wireless Stick, E213, Wireless
Paper (same rule, no bench board).
