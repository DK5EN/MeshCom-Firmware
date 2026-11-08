# RCA CSMA-03: why the consumed CAD result is 60-97 ms old (2026-11-08)

BLUF: the age is the nRF52 main-loop period, not the radio. `nrf52loop()` ends every pass with `delay(100)` (`src/nrf52/nrf52_main.cpp:2914`), and the CAD state machine does one step per pass: start in pass N, consume in pass N+1. The CAD itself finishes about 43 ms after its start, so the result waits `pass period - 43 ms` = 60-97 ms. The stale-CAD recheck cannot help: the fresh CAD is consumed one pass later again, so the blind window from CAD done to TX stays 63.5 ms (43 of 43 TX lines). With the default `CSMA_CAD_MAX_AGE_US=5000` every free CAD (45 of 45) pays one extra pass (~105 ms) for no gain. Recommendation: do not ship the age check as is; fix the structure (shorten the pass while a CAD is pending), then re-bench the threshold (section 6).

Scope: code reading plus the bench log `tools/bench/runs/harness/csma03-rak90-20261108-151137.log` (DK5EN-90, RAK4631, EU8, SF11, BW250, CAD 4 symbols). No source, tool or test file was changed.

## 1. Findings

| #   | Question                                   | Verdict                                                                                                                     | Evidence                                                                                                                                                                                                      |
| --- | ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Start and consume in different passes?     | CONFIRMED. One branch per pass; start and consume are separate `if`/`else if` arms on the same flags.                       | Start `nrf52_main.cpp:1801-1829`, consume `:1830-1870` (`else if(_cad_df)`), recheck start `:1850-1864`.                                                                                                      |
| 2   | What separates two passes?                 | CONFIRMED: `delay(100)` + `yield()` at the end of `nrf52loop()`, no early `return` in 1366-2917.                            | `nrf52_main.cpp:2913-2916`. Core `delay()` = `vTaskDelay(ms2tick(100))`, 1024 Hz tick = 102 ticks = 99.6 ms (`cores/nRF5/delay.c:33-48`, `FreeRTOSConfig.h:55`).                                              |
| 3   | Fixed wait/timeout before reading result?  | REFUTED. Only a 100 ms safety timeout when no `CadDone` arrives.                                                            | `nrf52_main.cpp:1951` (`millis() - cad_start_time > 100`, `CAD_SAFETY_TIMEOUT`). Log has no such line.                                                                                                        |
| 4   | Is `OnCadDone` ISR or task?                | TASK. ISR only gives a semaphore; the SX126x-Arduino task "LORA" runs `BgIrqProcess` -> `CadDone`.                          | ISR `radio.cpp:1334-1346` (`xSemaphoreGiveFromISR`); task `board.cpp:474-486`, created `:498`; callback `radio.cpp:1430-1437`; registered `nrf52_main.cpp:1201`; stamp `nrf52_main.cpp:505-515`.              |
| 5   | CAD duration                               | MEASURED 42-44 ms. Theory: 4 x 8.19 ms = 32.8 ms plus chip CAD processing and IRQ/task latency (the ~10 ms split is open).  | Tsym = 2^11 / 250 kHz; SF `country_profile.cpp:90` + `configuration_default.h:83`; 4 symbols `nrf52_main.cpp:1283`. Measured: `free=+41992..+43946` in 43 TX lines (CSMA-04, `lora_functions.cpp:2917-2918`). |
| 6   | Pass period                                | MEASURED ~105 ms (device clock): 99.6 ms delay + 4-10 ms body, spikes to ~40 ms body.                                       | TX lines `t - cad/1000` = min 103.5, median 105.3, max 110.1 ms (n=43, recheck start -> TX log). First-CAD age + 43 ms = 104-140 ms.                                                                          |
| 7   | `micros()` coarse or wrapping?             | COARSE, wrap-safe. Without DWT `micros()` = `tick2us(xTaskGetTickCount())`: 976.56 us steps. Ages are exact tick multiples. | `delay.h:56-61`, `rtos.h:66`, `FreeRTOSConfig.h:55`. 60546 = 62 ticks, 61523 = 63, 69336 = 71, 70312 = 72, 94726 = 97, 96679 = 99. Subtraction wrap-safe: `csma_timing.h:157-160`.                            |
| 8   | Does the recheck reduce the blind window?  | NO. Window CAD done -> TX start after the recheck is 63.5 ms (63476-64454 us, 43 of 43; one 68359).                         | `tx=+63476` etc. in the TX lines (`lora_functions.cpp:2917-2918`); `g_csma04_free_t` is restamped by the recheck CAD in `OnCadDone` (`nrf52_main.cpp:508-510`).                                               |
| 9   | Why ~200 ms between recheck and next scan? | Host artefact, not firmware. Device clock says ~105 ms.                                                                     | Host stamps of lines from two different passes are equal (e.g. TX_GATE_ENTER and CAD_SCAN at `...107.615`); device `t`/`cad` fields give 103-110 ms (row 6).                                                  |

## 2. Mechanism

Pass N, `lora_sm` section: `Radio.StartCad()` at `nrf52_main.cpp:1823` (t = 0). The LORA task runs `OnCadDone` at t ~ 43 ms and sets `cad_done_flag` (`:513-514`). The loop task finishes pass N, sleeps in `delay(100)`, runs the top of pass N+1 and reaches `:1830` -> the flag is consumed at t = T (pass period). `age = T - 43 ms`.

The recheck (`:1850-1864`) restarts the same cycle: start at T, done at T + 43, consumed at 2T. The final blind window is `T - 43 = 63.5 ms` again (row 8). The age check measures the wrong thing for the fix it triggers: it can only detect "the loop is slow", and the fix (another CAD) is subject to the same slowness.

Why the age is below `delay(100)` = 99.6 ms: the CAD (43 ms) runs inside the pass period, so age = T - X, not T.

## 3. Inherent versus avoidable

- Inherent lower bound: ~1-2 ms. SPI status read, LORA-task wake, one tick (976 us) of `micros()` quantisation, SPI + setup time of `doTX()`. Not separately measured (hypothesis H2).
- Avoidable: essentially all of the 60-97 ms. It is `delay(100)` plus pass body between `CadDone` and the consume, not radio behaviour.
- A threshold of 5 ms is below the quantisation-plus-pass floor of the current structure (min age 60 ms): the check can only ever say "stale".

## 4. Is the threshold check meaningful?

Not in the current structure. With the 5 ms default it fires on 100 % of free CADs (`over_5000us: 45` of 45 in the `.summary`), costs one pass (~105 ms) plus a second CAD, and leaves the blind window at 63.5 ms. It also lengthens TX latency of every message on RAK4631 without a safety gain, and shortens nothing.

Platforms: the code is shared nRF52 source. `heltec_t114` and `t_echo` inherit `-D BOARD_RAK4630` and the variant filter (`platformio.ini:98-104`), so they run the same `nrf52_main.cpp` loop and `delay(100)`; T-Echo/T114 additionally have a `delay(60000)` deep-sleep branch (`nrf52_main.cpp:1394-1397`). Bench-verified only on RAK4631 (DK5EN-90); the other two are UNVERIFIED on hardware.

ESP32 for comparison: `radio.scanChannel()` blocks (`esp32_main.cpp:4123`, second scan `:4148`); `g_csma04_free_t` is stamped right after (`:4165`) and TX follows in the same pass, so the blind window is a few ms (not measured here, hypothesis H4). There is no equivalent of the nRF52 pass gap, and no age check is needed.

## 5. Correct CSMA-03 design (options)

1. Shorten the pass while a CAD is pending (smallest diff, recommended): at `nrf52_main.cpp:2914` use `delay((cad_in_progress || cad_done_flag) ? 1 : 100)`. CadDone is then consumed within one short pass (body 4-10 ms, so age ~0-10 ms, spikes up to the body spike of ~40 ms). The other loop work is not starved (a pending CAD lasts ~43 ms per TX attempt). Needs a read of the flags under the same critical section as `:1792-1799`.
2. Wait in the same pass: after `Radio.StartCad()` poll `cad_done_flag` with `delay(1)` up to the 100 ms safety timeout and then consume and `doTX()` in the same pass. Blind window ~ms, equivalent to ESP32; blocks the loop ~43 ms per attempt (ESP32 `scanChannel()` blocks the same way).
3. Measure from the right point: if the check stays, the correct age is "CadDone to start of `doTX()`" (the CSMA-04 `tx=` value), not "CadDone to consume". The difference is only the `doTX()` prelude (hypothesis H2).

After option 1 or 2 the stale recheck is mostly redundant. Keep it only as a guard against a slow pass (display, ETH, web, GPS); it stays bounded by `recheck_used` (`csma_timing.h:162-165`).

## 6. Recommendation

- Now: do not leave `CSMA_CAD_MAX_AGE_US` at 5000 (`csma_timing.h:154`). Either disable the age check (e.g. `0xFFFFFFFF`, keeps tests and API) or revert to the plain consume path until the structure is fixed.
- With option 1: re-run the same bench with `-DCSMA_CAD_MAX_AGE_US=0` and read the new age distribution; set the default to about p99 of that distribution plus margin (expected ~15-20 ms, not 5 ms), so the recheck only fires on genuinely slow passes. If p99 stays above ~20 ms, drop the age check and rely on the shortened pass.
- Success criterion: `tx=+` (CAD done -> TX) from 63.5 ms to below ~15 ms in the CSMA-04 TX line; `CAD_STALE_RECHECK` on well under 10 % of TXs.

## 7. Open hypotheses and the bench check for each

| #   | Hypothesis                                                                                                                                                | Bench test that proves it                                                                                                          |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| H1  | The LORA task priority (`board.cpp:44-46` falls back to 1 = loop priority, `rtos.h:58`) does not delay CadDone. Measured `free=+43 ms` says it is prompt. | Log `millis()` in `OnCadDone` vs `cad_start_time` in an INSTRUMENT build; expect X ~ 43 ms, not ~100 ms.                           |
| H2  | The `doTX()` prelude (consume -> send) is < 2 ms, so age-at-consume ~ blind window.                                                                       | Compare `age_us` (recheck result) with the CSMA-04 `tx=+` of the same TX in one run with the recheck result logged.                |
| H3  | The ~10 ms of CAD time beyond 4 symbols is chip CAD processing, not task latency.                                                                         | Time `IRQ_CAD_DONE` on a scope/logic analyser (DIO1) against the SPI `SetCad` command.                                             |
| H4  | The ESP32 blind window is a few ms.                                                                                                                       | Same CSMA-04 TX line on an ESP32 node (`tx=+` after `free=+`).                                                                     |
| H5  | The 200 ms gap in the host log is capture/USB-CDC burst quantisation.                                                                                     | Add `millis()` to the `CAD_SCAN` / `CAD_STALE_RECHECK` debug lines (or read `serial_capture.py` chunking, `serial_capture.py:62`). |
| H6  | T114 and T-Echo behave like the RAK4631 (same loop, same `delay(100)`).                                                                                   | Same `-DCSMA_CAD_MAX_AGE_US=0` build and bench on one of each.                                                                     |

Commit check for the cited feature: `68ee6dcf` (CSMA-03 implementation) is contained in `fork-dev`; no revert of it in `git log --grep=Revert`.
