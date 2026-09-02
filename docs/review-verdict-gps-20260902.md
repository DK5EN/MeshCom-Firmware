# GPS-01..04 — Fable Verdict (2026-09-02)

Review of `feat-gps-nmea-20260902` @ `2f8245b3` (six finders, one adversarial verifier
with TinyGPS++ 1.1.0 host builds and filter simulations). Fix wave: F1–F10 below.

## Finding 1: Reject path and fix path both write the clock without any time check

- **File:** `src/gps_functions.cpp:1036`, `:1123-1138`
- **Severity:** high
- **Failure scenario:** a spliced RMC whose date term survives but whose time term is
  damaged parses as `"1834"` → h=0 m=18 s=34, or `"999999.99"` → h=99; both branches call
  `MyClock.setCurrentTime()` guarded only by `year > 2023/2024`; `mktime()` accepts it.
  `gpsSamplePlausible()` never sees hour/minute/second.
- **Fix (F1):** `gpsTimePlausible(h, m, s)` in `gps_filter.*` (h<24, m<60, s<61) and
  `gpsDatePlausible(y, mo, d)`; guard both `setCurrentTime` calls with
  `gps.time.isValid() && gpsDatePlausible && gpsTimePlausible`.

## Finding 2: Filter time constant is 3x shorter on nRF52

- **File:** `src/nrf52/nrf52_main.cpp:1693` (hard 1000 ms), `src/gps_filter.cpp` (`P += Q` per update)
- **Severity:** high
- **Failure scenario:** nRF52 evaluates every 1 s, ESP32 every 3 s; with Q per update
  τ = 136 s on nRF52 vs 408 s on ESP32, inside the 60–120 s error-correlation time the
  measurements warn about.
- **Fix (F2):** `altFilterUpdate(f, meas, dt_ms)`: `P += ALT_KF_Q * (dt_ms / 3000.0f)`,
  dt clamped to 0..60000; caller keeps `s_altLastMs`.

## Finding 3: QNH reference never latches on a GPS node without a fix

- **File:** `src/gps_functions.cpp:1300-1313` (`baroBaseLatchAllowed`)
- **Severity:** high
- **Failure scenario:** first `getPressASL()` runs at t≈60 s, before TTFF and before the
  249 s convergence edge; a sheltered WX node that never fixes keeps `fBaseAltidude == 0`
  forever and reports QFE as QNH. Before the patch it latched the persisted `node_alt`.
- **Fix (F4):** withhold the latch only while a fix exists and the filter is not yet
  converged: `if (bGPSON && gpsDetected && !bDisplayTrack && WZ_GPS_HasFix()) return
  WZ_GPS_AltConverged(); return true;`. Pre-fix the persisted value latches as before; the
  convergence edge re-latches.

## Finding 4: Stale altitude re-fed into the filter

- **File:** `src/gps_functions.cpp:925`, `:1099`
- **Severity:** medium
- **Failure scenario:** `updateGPSdata` is set by any sentence; a GGA with fix quality 0 or
  an empty altitude term re-commits the old value while RMC keeps `age()` fresh, so the
  filter shrinks `P` on samples carrying no information and the QNH latch fires early.
- **Fix (F6):** read `gps.altitude.isUpdated()` before `meters()` (which clears it) and
  feed the filter only when true.

## Finding 5: Garbage altitude accepted at seed time

- **File:** `src/gps_filter.cpp:25-29`, `gpsSamplePlausible()`
- **Severity:** medium (self-heals in 10 samples, but seeds `node_alt` to e.g. 2.7 m)
- **Fix (F5):** `gpsSamplePlausible(lat, lon, alt, year, month, day)` rejects
  alt < −500 or > 10000 m.

## Finding 6: `--setalt` with an out-of-range value seeds 0 m and re-latches QNH to 0

- **File:** `src/command_functions.cpp:4123-4136`
- **Severity:** medium
- **Fix (F7):** print "alt out of range, ignored" and return; do not clamp to 0.

## Finding 7: `--setpress` bypasses the BME680 latch

- **File:** `src/command_functions.cpp:2296`
- **Severity:** low
- **Fix (F9):** route through `baroBaseRelatch(node_alt)`.

## Finding 8: Test weaknesses

- **File:** `test/test_gps_filter/test_main.cpp:153-173`, `:267-303`
- **Severity:** medium
- **Failure scenario:** the doc-sequence case passes with the gate deleted (max deviation
  0.23 m gated, 0.78 m ungated vs a ±3 m bound); `GPSDEBUG1_ALT` is never used.
- **Fix (F8):** assert the gate rejects exactly the four samples > 15 m below 280 and the
  state moves < 0.5 m; add the second field series (raw ≥ 3.5 m, whole ≤ 3.4 m, converged
  phase ≤ 0.9 m, measured 4.08 / 3.27 / 0.71); scan tool: exclude calendar-impossible
  dates from the month vote.

## Finding 9: Dead accessor

- **File:** `src/gps_functions.h:51`, `.cpp:1177` — `WZ_GPS_RejectCount()` has no caller.
- **Fix (F10):** remove the accessor, keep the counter for the `reject:` line.

## Refuted or declined (do not re-investigate)

- Re-seed dead zone (10 consecutive rejects; 15–16 m steps): under white 4 m noise the
  step never re-seeds, but the field trace is AR(1) with φ≈0.98 (step sd 0.92 m) and
  there the dead zone collapses to ~2.5 min; a leaky counter changes the false-reseed
  rate 3.6→3.9 %/24 h and buys nothing. Kept as is.
- Naming `baroBaseRelatch`/`baroBaseLatchAllowed` without `WZ_GPS_` prefix: deliberate,
  they exist on boards without GPS.
- Shared `TinyGPSPlus gps` with `src/t-deck-pro/peri_gps.cpp`'s task: the task has no
  caller (`gps_init()` unused); latent, out of scope, noted for T-Deck-Pro work.
- nRF52 `INSTR_SECTION` slot table (16) already over capacity before this change;
  `gps_feed` may not get a slot there. Pre-existing, ESP32 unaffected.
- `WZ_GPS_HasFix()` not gating a silent module (`age_ms` frozen): pre-existing.
- Float precision of the recursion: 240 ULP per update at 280 m, contraction; safe.
- Null-island exact compare: `parseDegrees` makes exact 0.0 require all-zero digits; safe.
- TRACK-from-boot latches the first raw fix: accepted trade (plan §3.4), stated in the PR.
- NMEA-echo helper duplication with `detectBaudrate()`: out of minimal scope.
