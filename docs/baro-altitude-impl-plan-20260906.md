# Implementation plan — GPS-08 / GPS-09 / GPS-07 / GPS-05b (barometer vs GPS altitude)

**Source:** [`bug-baro-altitude-20260906.md`](bug-baro-altitude-20260906.md) · **Branch:** `fork-main` at
`4ed8bfd2` (tree clean) · **Written:** 2026-09-06 · **Status:** APPROVED 2026-09-06 (operator). Waves 1-2 committed on `fork-main`: `531d66b4` (GPS-07),
`49769eda` (GPS-08/09), `bc3ee68b` (GPS-05b). Wave 3 (docs) follows the bench verdict; see the bug doc
for the numbers.

Scouting done against the tree: three read-only recon passes (filter + host test, barometer latch
paths, `node_alt` consumers + docs conventions) plus an offline replay of the GPS-07 constant
candidates against both corpora (§2.3 below). All file:line references are current.

---

## 1. Decisions, one paragraph each

**GPS-08 — self-latch `fBasePress`, no persistence.** Mirror what QNH already does two lines away:
`getPressASL()` self-latches `fBaseAltidude` (`src/bmx280.cpp:327`); `getPressALT()` gets the same
treatment for `fBasePress`: `if(fBasePress == 0 && fBaseAltidude != 0 && fPress != 0) fBasePress = fPress;`
before the existing guard at `:310`. `fBaseAltidude` is set by `baroBaseRelatch()`
(`src/gps_functions.cpp:1370`) at filter convergence, so the pressure reference is rebuilt within one
60 s WX tick after every boot on both platforms — the nRF52 call site (`src/nrf52/nrf52_main.cpp:2352`)
goes through the same function, no second edit. No new persisted field, no `FLASH_STRUCT_VERSION`
bump, no stale-pressure re-anchor after a long power-off (the §9 rejection). `--setpress` keeps
working as the manual override. Help text at `src/command_functions.cpp:898` is corrected to
`--setpress  latch QNH reference at current altitude` — the handler parses no argument today and we
do not add one (minimal change; the "fix the help line" branch of §5.3).

**GPS-09 — keep the `int` field, add a `float` accessor.** New `float getPressALTf()` in
`src/bmx280.cpp` holding the existing formula in float; `getPressALT()` becomes `lround(getPressALTf())`
so the reported/persisted `node_press_alt` is byte-for-byte what it is today (§9 rejection of the
struct-type change). The float value is consumed only by the GPS-05b fusion. The §10 gate for GPS-09
(ADEV 30 s halves) is therefore measured on the fused output, not on `node_press_alt`.

**GPS-07 — change both constants: `ALT_KF_GATE_M` 15 → 30 and `ALT_KF_RESEED_N` 10 → 60.** The bug
doc says "60 or gate 30, validate against both corpora". Validated (§2.3): either change alone still
re-seeds five times on the `DK5EN-93` corpus; only the combination gives zero re-seeds on both.
Cost: a node carried more than 30 m vertically outside TRACK mode now takes 3 min (ESP32) / 1 min
(nRF52 at 1 s cadence) to follow instead of 30 s / 10 s. Acceptable for a stationary-mode filter;
TRACK mode bypasses it entirely (`src/gps_functions.cpp:1171`). `ALT_KF_RESEED_N` is a `uint8_t`
counter (`AltFilter::rejects`), 60 fits.

**GPS-05b — complementary filter in a new Arduino-free module, enabled only when a BMx280 is armed.**
`src/alt_fusion.{h,cpp}`: `out = LPF_τ(gps) + (baro − LPF_τ(baro))`, single-pole, irregular-sample
form with `dt` from `millis()`, τ = 30 min (the knee in §7 of the bug doc). Inputs: the Kalman
estimate `s_alt.x` (already gated) and `getPressALTf()`. Both low-passes seed on the first sample so
enabling the fusion never steps the output. Integration point: `src/gps_functions.cpp:1186`, after
the Kalman update, guarded by `(bBMPON || bBMEON) && fBasePress != 0` — boards without a sensor and
nodes whose pressure reference is not armed yet keep today's path bit-for-bit. Reset together with
the Kalman filter on TRACK entry (`:1167`). BME680 and BMP390 paths are **not** fused (bug doc §8:
they reduce against a sea-level constant, a different quantity); only the BMx280 path is measured.
Fusion output is rounded into `node_alt` as today; `node_press_alt` is untouched.

**Not done in this pass:** persisting `fBasePress`; HDOP weighting; making `node_press_alt` a
float; normalising the three sensor families; parsing an argument for `--setpress`; the nRF52-only
legacy EMA at `src/nrf52/nrf52_main.cpp:2871` (lives in `getGPS()`, the `ENABLE_RAK_GPS` build
path, not the `WZ_GPS_Loop()` path the bench nodes run).

---

## 2. Evidence gathered during scouting

### 2.1 Where the barometric reference lives

- `fBasePress` / `fBaseAltidude`: defined `src/bmx280.cpp:27-28`, extern in
  `src/loop_functions_extern.h:148-150`. Writers of `fBasePress`: definition and the `--setpress`
  handler (`src/command_functions.cpp:2286-2302`) only — confirmed. `src/bme680.cpp:158` also assigns
  `fBasePress` on every BME680 read, which is why GPS-08 does not bite that sensor family.
- `baroBaseLatchAllowed()` (`src/gps_functions.cpp:1385-1402`): returns the filter's converged
  flag while GPS is on, has a fix and is not in TRACK; otherwise `true` (manual `--setalt` nodes latch
  immediately).
- `getPressALT()` / `getPressASL()` are called in that order on the 60 s WX tick on both platforms
  (`src/esp32/esp32_main.cpp:3704-3705`, `src/nrf52/nrf52_main.cpp:2352-2353`). With the self-latch
  in `getPressALT()`, a GPS node reports a non-zero `ALT asl` on the first tick after convergence; a
  no-GPS node one tick later (its `fBaseAltidude` latches in `getPressASL()` the same tick).

### 2.2 Filter and test infrastructure

- Constants `src/gps_filter.h:13-20`; re-seed at `src/gps_filter.cpp:33-41`.
- `test/test_gps_filter/test_main.cpp`: 16 Unity cases, two embedded field series in
  `test/support/traces/gpsdebug_alt_series.h`. Run: `pio test -e native -f test_gps_filter`.
  `test_zehn_ausreisser_seeden_neu` asserts the old `RESEED_N` = 10 and must be rewritten, not
  deleted.
- `env:native` (`platformio.ini:188-237`) lists tests in `test_filter` and sources in
  `build_src_filter` explicitly — both need a line for the new fusion module and its test.

### 2.3 GPS-07 replay — both corpora, filter model identical to `gps_filter.cpp`

Offline replay (Q 0.01, R 185, P0 400, dt-scaled; same as the firmware) of the raw altitude
series. `14.log` = 8 h `DK5EN-14` overnight capture (9791 samples, raw sd 8.11 m);
`dk5en-93-altb.log` = the 2 h capture behind the bug doc (2370 samples, raw sd 16.12 m).

| gate m | reseed N | 14.log re-seeds | 14.log kf sd | 93 re-seeds | 93 kf sd |   93 p2p |
| -----: | -------: | --------------: | -----------: | ----------: | -------: | -------: |
|     15 |       10 |              10 |         6.51 |          20 |    16.11 |     71.6 |
|     15 |       60 |               0 |         4.91 |           5 |    11.05 |     47.6 |
|     30 |       10 |               0 |         5.36 |           5 |    12.89 |     63.0 |
| **30** |   **60** |           **0** |     **5.36** |       **0** | **7.69** | **33.7** |
|     45 |       10 |               0 |         5.36 |           0 |     9.07 |     39.6 |

The current constants (row 1) deliver essentially nothing on the 93 corpus, as the bug doc measured.
Gate 30 + N 60 is the only row with zero re-seeds on both and the best 93 spread among the safe rows.
(Never re-seeding at all gives sd 3.89 on 93 but is unsafe — a node that moves would be stuck.)

---

## 3. Waves and file ownership

Every writer gets an exclusive file set; nothing overlaps within a wave. Orchestrator owns
`platformio.ini` and `docs/` throughout (hotspot carve-out) and applies those edits at the gate.

### Wave 1 — two writers in parallel, both `implementer` (Sonnet, high)

| agent | item            | exclusive files                                                                                                                                                                            |
| ----- | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1A    | GPS-07          | `src/gps_filter.h`, `test/test_gps_filter/test_main.cpp`, `test/support/traces/gpsdebug_alt_series.h` (append a third series: the 93 corpus excerpt that re-seeds under the old constants) |
| 1B    | GPS-08 + GPS-09 | `src/bmx280.cpp`, `src/bmx280.h`, `src/command_functions.cpp` (help line only)                                                                                                             |

1A regression test: replay the new series → under the new constants 0 re-seeds and kf sd < raw sd;
plus rewrite `test_zehn_ausreisser_seeden_neu` as "60 rejects re-seed, 59 do not". Must fail
before / pass after (agent proves both by running once with the old header).

1B has no host test (bmx280 needs the sensor lib). Verification: `pio run -e heltec_wifi_lora_32_V3`
and `pio run -e wiscore_rak4631` build clean; grep proves `fBasePress` now has exactly three writers.
Regression proof is the bench gate in §4.

Gate 1 (orchestrator): full `pio test -e native`, both firmware builds, diff review, bench proof of
GPS-08 on `DK5EN-93` (cold boot, no `--setpress`, `--info` after convergence + one WX tick shows
`ALT asl` ≠ 0; reboot, repeat) and on `DK5EN-90` (nRF52). Commit: one commit per item (GPS-07,
GPS-08/09) so upstream cherry-picks stay clean.

### Wave 2 — one writer (`implementer`, Sonnet high) + orchestrator integration

| agent | item                | exclusive files                                                                                                            |
| ----- | ------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| 2A    | GPS-05b fusion core | `src/alt_fusion.h`, `src/alt_fusion.cpp`, `test/test_alt_fusion/test_main.cpp`, `test/support/traces/altb_fusion_series.h` |

The core is pure C: `AltFusion` state, `altFusionReset()`, `altFusionUpdate(f, gpsX, baroAlt, dtMs)`,
constant `ALT_FUSION_TAU_MS` = 1 800 000. Host test replays the 93 corpus (gps kf, baro float, timestamps)
and asserts sd ≤ 4 m and 30-min Allan deviation ≤ 1 m against the raw series (the §10 gate, computed
in the test), plus: first-sample seeding gives `out == gps`, a step in baro passes through unattenuated
at t=0 and decays with τ, a step in gps is attenuated to 1−e⁻¹ after τ, dt clamp.

Orchestrator (after 2A lands): the ~15-line integration in `src/gps_functions.cpp` (`:1167` reset,
`:1186` fused write under the `(bBMPON || bBMEON) && fBasePress != 0` guard), `platformio.ini`
(`test_filter` + `build_src_filter` lines), a one-line `[GPS ]...alt fused` debug print under
`iGPSDEBUG > 0`. Kept in the orchestrator because `gps_functions.cpp` is the file 1A/1B verification
is measured through and its GPS-03 block is the trickiest code on the path.

Gate 2: full native suite, both builds, then the 2 h stationary bench capture on `DK5EN-93` with the
Appendix-A `[ALTB]` instrumentation re-applied locally (never committed): `node_alt` sd ≤ 4 m and 30-min
ADEV ≤ 1 m via `~/Downloads/meshlog-20260906/analyze_alt.py`. That capture is also the GPS-07 and GPS-09
bench proof. Commit: one commit for GPS-05b.

### Wave 3 — docs, orchestrator only

`docs/BACKLOG.md` rows GPS-07/08/09/05b (`:2864-2870`), status block of `bug-baro-altitude-20260906.md`,
`docs/CHANGELOG-stability.md` items 202+ (numbered format), this plan's status line. `prettier` on
each. One docs commit.

---

## 4. Verification gates, restated

| item    | host test                                | bench                                                                                          |
| ------- | ---------------------------------------- | ---------------------------------------------------------------------------------------------- |
| GPS-07  | new series 0 re-seeds, old constants ≥ 5 | 2 h capture: `node_alt` sd clearly below raw sd                                                |
| GPS-08  | none possible                            | cold boot, no command, `ALT asl` ≠ 0 after convergence + 60 s; reboot, repeat; ESP32 and nRF52 |
| GPS-09  | fusion test consumes float               | fused-channel ADEV 30 s ≈ 0.1 m                                                                |
| GPS-05b | replay sd ≤ 4 m, ADEV 30 min ≤ 1 m       | same 2 h capture on the fused build                                                            |

**Shipping caveat (bug doc §7, unchanged):** GPS-05b needs a TRACK-mode capture with pressure before it
ships in a release. TRACK mode bypasses both filters, so the fusion cannot alter TRACK behaviour, but
the transition TRACK → stationary (fusion re-seeds from the Kalman seed) is untested on a moving node.
The commit lands on `fork-main`; the operator decides whether a release waits for that capture.

---

## 5. What the operator is asked to approve

1. GPS-07: both constants (gate 30 m, re-seed after 60 rejects), per §2.3.
2. GPS-08: self-latch in `getPressALT()`; help text corrected, no argument parsing added.
3. GPS-09: float accessor only; struct field stays `int`.
4. GPS-05b: implemented and committed now with τ = 30 min, guarded to BMx280 boards with an armed
   reference; release-gated on the TRACK capture.
5. Commits per wave on `fork-main` by the orchestrator, one commit per item, no push.
