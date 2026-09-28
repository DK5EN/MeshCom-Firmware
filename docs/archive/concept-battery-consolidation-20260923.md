# Concept: consolidate battery and ADC measurement

Date: 2026-09-23
Status: proposal, low priority, nothing implemented
Trigger: OE3WAS asked whether the fork advanced his ADC/BATT smoothing. It did not.
Base: `fork-main` and `upstream/dev` are byte-identical in `src/adc_functions.*`, `src/batt_functions.*`,
`src/batt_function_old.cpp`. Line numbers refer to `fork-neo-test` at `b221a57a`.

## Verdict

Consolidate, but only the parts that are the same problem. Reading one raw value is board-specific and stays
per board. Everything after that point (sampling schedule, filter, presence detection, percent curve,
publishing) is one problem that the code base solves up to three times, differently per file, and should be
one shared implementation.

Use a first-order filter with a time constant in seconds for the battery, not the second-order filter from
`adc_functions.cpp`. Keep the second-order filter for `--analog`, where it fits.

## Current state

| Path                                  | Boards                                                                                                                      | Sampling                                                                | Filter                                                            |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------- |
| `adc_functions.cpp` (`--analog`)      | ESP32 with `ANALOG_PIN`, only when configured                                                                               | 2-99 ms, output every 1-99 s                                            | Brown double exponential, one-step forecast, alpha configurable   |
| `batt_functions.cpp` (`USE_NEW_BATT`) | E22 family (5), loraprs-e22, T-Beam 1W, T3-S3, T-Deck, T-Deck Plus, lora32-v21, E213, Wireless Paper                        | one read per call, 500 ms on ESP32                                      | first-order EMA, `alpha = 0.05f` const (tau about 10 s at 500 ms) |
| `batt_function_old.cpp`               | Heltec V2/V3/V4, Wireless Stick, Tracker, E290, T-Deck Pro, T-Connect Pro, T5, T-ETH-Elite, loraprs-ra01, RAK, T114, T-Echo | 500 ms ESP32, 30 s nRF52; block average of 8 or 64 reads on some boards | none across calls                                                 |
| AXP PMU (`MODUL_FW_TBEAM`)            | T-Beam, T-Beam Supreme, T-Beam SX1262/SX1268                                                                                | 500 ms                                                                  | PMU value, no software filter                                     |

Details worth knowing:

- Heltec V3/V4/Stick take one single `analogRead()` per measurement, and only every second call, because the
  switched divider needs 100 ms to settle (arm on one call, read on the next). No averaging, no filter. This
  is the noisiest battery path in the fleet.
- The alpha in `batt_functions.cpp` assumes the 500 ms cadence Kurt introduced in `f8531695` (2026-06-09)
  together with the filter. Changing the call rate changes the smoothing.
- Two percent curves exist:
  - `batt_function_old.cpp`: piecewise, 0 % below `0.785 * max_batt`, then `(mv - 0.785 * max) / 30` up to
    `0.857 * max`, then `10 + 0.15 * (mv - 0.857 * max)`. The `/30` and `0.15` slopes are absolute mV, so
    they do not scale to 2S packs.
  - `batt_functions.cpp`: linear between `BAT_MIN_VOLTAGE` and `fBattMax`, and 100 % below 1 V (USB).
  - The same battery therefore shows a different percentage depending on the board.
- The BAT-01 presence detector (`battDetectReset/Update/Feed`) exists as a copy in both battery files.
- Low-voltage deep sleep in `batt_functions.cpp` is commented out (issue #1053).

### Regression on the neo branches

`fork-neo` and `fork-neo-test` run `loopInterval_battCheck()` at 30 s on both platforms
(`src/loop_scheduler.h:151`, commit `3b2fbb8c`). Upstream and `fork-main` run it at 500 ms on ESP32.
Consequences on ESP32:

- `USE_NEW_BATT` boards: EMA time constant goes from about 10 s to about 10 min.
- BAT-01 absent streak (6 samples) goes from 3 s to 3 min.
- Heltec V3: one fresh reading per 60 s instead of per second.

The comment in `src/esp32/loop_actions_esp32.cpp:192-197` calls the change behaviour-identical. That holds
for nRF52 only. This is independent of the consolidation and should be fixed first.

## Why consolidate

- Filter choice, cadence and percent curve depend on which of two files a board compiles, not on a
  decision. The Heltec V3 has no filter because it happens to sit on the old path.
- The filter is coupled to the call rate. The neo regression is exactly that coupling breaking.
- Duplicated detector and percent code drift apart over time.
- None of it is host-testable today.

## Target shape

### 1. Raw acquisition per board family

One function per hardware family, returning raw millivolts at the battery, nothing else:

```cpp
// returns true when a fresh sample is available, false while a divider is settling
bool batt_raw_mv(float *mv);
```

Families: fixed divider ESP32, switched divider (Heltec V3/V4/Stick, E213, Wireless Paper), nRF52
(reference and sample-time setup), AXP PMU. Pin, multiplier, offset and `--batt factor` stay here.

### 2. Shared pipeline (header-only, host-testable)

Pure C++, no Arduino, in the style of `src/command_match.h`:

- First-order EMA with a time constant in seconds: `alpha = 1 - exp(-dt / tau)` from the real elapsed
  time. Cadence changes then no longer change the smoothing. Default tau 30-60 s.
- Seed with the first valid sample instead of ramping from zero (today the new path seeds with `fBattMax`
  to avoid a boot deep sleep; the seed rule has to keep that protection).
- BAT-01 presence detection on the raw sample, one copy.
- Samples taken during TX are dropped (TX load sags the voltage).
- One percent curve, scaled to `node_maxv` and `BAT_MIN_VOLTAGE`, valid for 1S and 2S.

### 3. One schedule

Sample about every 1 s. Display, BLE, APRS `/B=`, web and the low-voltage logic read the filtered value
whenever they need it. Sampling and reporting are decoupled, which is the principle OE3WAS applied to
`--analog`.

### 4. `--analog` keeps its parameters

`loop_ADCFunctions()` keeps its configurable alpha, interval and show time, and moves onto the shared filter
building block, with the second-order stage as an option.

## Why not the second-order filter for the battery

Its advantage is lag compensation. A battery drains over hours; a lag of 30-60 s is irrelevant. The cost is
overshoot on steps (USB plugged in, TX load), and the battery value drives deep sleep and the `/B=` value on
air. The second-order filter fits `--analog`, where sensors change fast.

## Risks

- About 30 boards, four acquisition paths, and the output drives deep sleep. A wrong seed or multiplier puts
  field nodes to sleep.
- Percent values change on every board where the curve changes. Users will notice; the release notes must
  say so.
- Upstream acceptance is unlikely as one PR (OE3WAS: pushing such changes past Kurt is hard). It fits the
  neo campaign, whose scope is duplication removal.

## Verification plan

- Bench fleet covers each path once: Heltec-93 (switched divider), T-Beam-92 (PMU), RAK-90 (nRF52),
  T-Deck-14 (`USE_NEW_BATT`).
- Capture the unchanged image first and prove two captures agree before any before/after comparison.
- Log raw and filtered values over a full charge and discharge on each bench node; compare noise and step
  response against today.
- Host tests for the filter (dt-independence, seed, step response), the percent curve (1S and 2S) and the
  detector.

## Order

1. Fix the neo battCheck cadence (one line, independent).
2. Filter header and host tests.
3. Move the `USE_NEW_BATT` boards onto it (they already have a filter, lowest risk).
4. Split `batt_function_old.cpp` into per-family raw readers, starting with the Heltec V3.
5. Unify the percent curve, then move `--analog` onto the shared filter block.
