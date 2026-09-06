# GPS-05b/07/08/09 — the barometer is the only honest altitude on the node, and nothing uses it

**Status:** Root cause ESTABLISHED for all four items by a 2 h bench capture on `DK5EN-93` plus code
reading against the tree at `f3d07372`. **Measured, not fixed.** GPS-07 was already open on other
evidence; GPS-08 and GPS-09 are new; GPS-05b now has the measurement it was waiting for.
**Severity:** Medium-high — no crash, no mesh impact, but a stationary indoor node reported an
altitude spanning **72 m** over two hours, and the barometer that could have held it to **1.3 m**
is either switched off by default (GPS-08) or not wired to the reported value at all (GPS-05b).
**Class:** upstream defects (GPS-07/08/09 all predate this branch); GPS-05b is a design item.
**Measured:** 2026-09-06, 13:31–15:31 local, `DK5EN-93` (Heltec V3, `heltec_wifi_lora_32_V3`,
BME280 + L76K GPS, indoors, stationary throughout).
**Branch:** `fork-main`, working tree at `f3d07372` plus the uncommitted CTY-02 revert of
2026-09-06 (upstream issue #1133 refuted) · **Upstream merge-base:** `4e649eae`
**Destination:** fork-only for now. Not proposed upstream until bench-proven here.
**Related:** [`bug-GPS-uart-overflow-20260901.md`](bug-GPS-uart-overflow-20260901.md) (GPS-01..04,
the filter this run stresses), BACKLOG §3.8 rows `GPS-05b`, `GPS-07`, `GPS-08`, `GPS-09`.

> **Scope note for the implementer.** Every file:line below was read against the tree with the
> temporary instrumentation (Appendix A) reverted, and re-verified after the CTY-02 revert landed
> in the working tree — including `command_functions.cpp`, the one cited file that revert touched.
> They are valid as you find them. None of this doc's findings are related to CTY-02 or to upstream
> issue #1133.
>
> Read §9 before touching code. Two attractive fixes are explicitly rejected there: making
> `node_press_alt` a `float` in the settings struct is **not** the fix for GPS-09, and persisting
> `fBasePress` is **not** the obvious fix for GPS-08.
>
> Do the items in order 08 → 09 → 07 → 05b. GPS-08 is a prerequisite for verifying any of the
> others: until it is fixed the barometric channel reads zero, so you have nothing to compare
> against.

---

## 1. Verdict in one paragraph

On a node that did not move, the raw GPS altitude wandered over **91.6 m** and the altitude the
firmware actually reports wandered over **72 m**, while the barometer on the same board, sampling
the same two hours, moved **4.8 m** — and the 0.87 hPa pressure fall over the window accounts for
all of it, so the true altitude change was zero. The scalar Kalman filter added in GPS-03 removed
essentially none of this: its standard deviation was **17.16 m** against the raw sample's
**16.93 m**, because the re-seed guard fired **20 times in 1.9 hours** and each firing snapped the
estimate directly onto the raw outlier that triggered it (GPS-07, already open, now confirmed on a
second board and ten times more severe than the capture that filed it). The barometer's own
stability was **1.34 m** standard deviation, 0.10 m Allan deviation at 30 s — between one and two
orders of magnitude better than GPS at every averaging time measured. It contributes nothing to the
reported altitude today by design; worse, on a node whose operator has not typed `--setpress` since
its last boot it does not even produce a number, because `fBasePress` is written in exactly one
place in the whole firmware and that place is the `--setpress` handler (GPS-08).

---

## 2. How the measurement was taken

The firmware prints no altitude diagnostics that let raw, filtered and barometric values be compared
in one place: `[GPS ]...position` (`src/esp32/esp32_main.cpp:3239`) prints `gpsData.altitude`, the
**raw** sample, and nothing prints the filter state or `node_alt` alongside it. A temporary CSV
marker (`[ALTB]`, Appendix A) was added, emitting one row per GPS cycle with raw altitude, the
Kalman state (`x`, `P`, reject count, converged flag), the reported `node_alt`, QFE, barometric
altitude, QNH, temperature and both latched baro references.

The marker uses `snprintf` into a stack buffer plus `Serial.write`, deliberately **not**
`Serial.printf`: `Print::printf` mallocs for anything over 64 bytes, and this line runs every 3 s.

Capture procedure, reproducible with the scripts in Appendix B:

1. Flash the instrumented build, open `/dev/cu.usbserial-0001` (the CP2102 reboots the node on open,
   which is wanted — the run starts from a cold filter).
2. Wait for `conv;1` **and** the first BME280 sample (`qfe > 0`); the WX loop runs on a 60 s timer
   (`src/esp32/esp32_main.cpp:3686`), so this is never sooner than ~60 s.
3. Send `--setpress` once, to arm `fBasePress` at all — see GPS-08. Logged at t=255 s:
   `Base Press set to: 971.1 at 474.0 m`.
4. Record for 2 h and analyse.

**Run summary.** 2371 samples at a 3.1 s median cadence over 2.00 h. Fix present in 2370 of 2371
samples (100.0 %), 6–12 satellites, HDOP 0.8–3.2 — this was a _good_ indoor fix, not a marginal one.
Filter converged at t=255 s (`P` = 2.49, threshold `ALT_KF_P_CONV` = 2.5). QFE 970.26–971.22 hPa,
sensor temperature 25.3–25.7 °C.

---

## 3. Measurements

Stability over the 1.66 h window in which all channels are live, first 15 min dropped as
complementary-filter warm-up; 1973 samples. ADEV is the Allan deviation at the stated averaging
time, in metres.

| channel                           |    sd |   p2p |     drift | ADEV 30 s | ADEV 1 m | ADEV 5 m | ADEV 15 m | ADEV 30 m |
| --------------------------------- | ----: | ----: | --------: | --------: | -------: | -------: | --------: | --------: |
| GPS raw (`gpsData.altitude`)      | 16.93 | 91.60 | −10.82 /h |      4.97 |     6.92 |     9.31 |     10.17 |      6.48 |
| Kalman = reported `node_alt`      | 17.16 | 72.00 | −10.67 /h |      4.87 |     7.07 |    10.09 |     10.45 |      5.94 |
| barometric, as reported (`int`)   |  1.36 |  5.00 |  +2.66 /h |      0.20 |     0.25 |     0.30 |      0.55 |      1.02 |
| barometric, recomputed in `float` |  1.34 |  4.77 |  +2.70 /h |      0.10 |     0.13 |     0.27 |      0.54 |      1.02 |
| complementary filter, τ = 5 min   | 10.20 | 45.43 |  −7.98 /h |      0.94 |     1.70 |     5.96 |      8.70 |      6.01 |

Over the full 1.91 h armed window:

- Reported `node_alt` ranged **464 m to 536 m** — a 72 m span on a stationary node.
- QFE fell 971.13 → 970.26 hPa = **7.16 m of apparent altitude**, monotonically. This is weather.
- Detrended correlation between the GPS and barometric series: **−0.031**. The barometer
  corroborates **none** of the GPS excursion. (The undetrended figure, −0.366, is an artefact of the
  two channels having opposite linear trends and should not be quoted.)

Per half hour, mean altitude in metres — the GPS bounces, the barometer walks steadily with the
weather:

| window     | GPS raw | `node_alt` |   baro |
| ---------- | ------: | ---------: | -----: |
| 0–30 min   |  492.40 |     490.44 | 476.03 |
| 30–60 min  |  483.98 |     484.79 | 477.96 |
| 60–90 min  |  487.45 |     486.98 | 479.26 |
| 90–120 min |  481.90 |     482.86 | 480.38 |

**Bias is not measured.** No surveyed altitude for the site was available, so every figure above is
spread and drift, never accuracy. Do not read the barometric column as "correct" — see GPS-08 §5.2,
its absolute anchor is one arbitrary GPS sample.

---

## 4. GPS-07 — the re-seed guard fires constantly (already open, now confirmed)

**Site:** `src/gps_filter.h:16-17`, fired at `src/gps_filter.cpp:35-38`, called from
`src/gps_functions.cpp:1185`.

This item was filed on 2026-09-03 from an 8 h `DK5EN-14` capture that showed **10 re-seeds in 8 h**.
This run shows **20 re-seeds in 1.9 h** — roughly one every six minutes, an order of magnitude worse
rate, on a different board (Heltec V3, not T-Deck), in daylight, indoors, with a good fix.

The mechanism is exactly as filed. `ALT_KF_GATE_M` is 15 m; indoor multipath excursions exceed that
and last longer than `ALT_KF_RESEED_N` = 10 consecutive samples (30 s at the 3 s ESP32 cadence), so
the filter concludes it has lost track, discards its history, and re-seeds **onto the outlier that
triggered the rejection**. Every re-seed in this capture set the estimate to the raw sample exactly:

```
t=  9.9 min  raw=496.8  kf->496.8  sat=7  hdop=1.3
t= 20.9 min  raw=474.1  kf->474.1  sat=8  hdop=1.2
t= 24.8 min  raw=497.0  kf->497.0  sat=8  hdop=1.2
t= 27.4 min  raw=511.4  kf->511.4  sat=7  hdop=1.9
t= 33.5 min  raw=531.0  kf->531.0  sat=7  hdop=1.8
t= 36.2 min  raw=507.6  kf->507.6  sat=7  hdop=1.8
t= 39.6 min  raw=528.4  kf->528.4  sat=8  hdop=1.2
t= 41.4 min  raw=498.3  kf->498.3  sat=9  hdop=1.4
t= 43.2 min  raw=474.6  kf->474.6  sat=7  hdop=1.1
t= 49.1 min  raw=499.5  kf->499.5  sat=9  hdop=1.1
t= 53.0 min  raw=475.9  kf->475.9  sat=9  hdop=1.1
t= 65.8 min  raw=494.6  kf->494.6  sat=7  hdop=1.2
t= 70.0 min  raw=478.9  kf->478.9  sat=8  hdop=1.1
t= 72.9 min  raw=516.6  kf->516.6  sat=8  hdop=1.1
t= 74.7 min  raw=493.9  kf->493.9  sat=7  hdop=1.4
t= 77.7 min  raw=469.8  kf->469.8  sat=9  hdop=1.0
t= 83.5 min  raw=500.1  kf->500.1  sat=8  hdop=1.1
t= 86.5 min  raw=468.7  kf->468.7  sat=10 hdop=1.0
t= 95.3 min  raw=514.6  kf->514.6  sat=9  hdop=1.0
t=104.8 min  raw=471.1  kf->471.1  sat=9  hdop=1.7
```

Note the HDOP column: 1.0–1.9 throughout. These are not degraded fixes the filter could have
identified as such — which is independent confirmation of the earlier finding that HDOP-weighted `R`
buys nothing (BACKLOG §7.6, `GPS-05a`/`GPS-05b` rejection).

**Consequence, and this is the part worth stating plainly:** the filter delivers no measurable
benefit on this capture. sd 17.16 m against raw 16.93 m is not an improvement — those two numbers
are the same to within their own uncertainty. It does clip the worst single excursions (p2p 72.0 m
against 91.6 m) but the spread is untouched, and at 30 s averaging it improves ADEV by 2 %. GPS-03
measured 4.36 → 1.52 m RMS on its own corpus; on this one it delivers nothing, because re-seeding
throws away exactly the history the filter needs.

**Fix:** as already filed — one constant. `ALT_KF_RESEED_N` 60 (3 min) or `ALT_KF_GATE_M` 30. The
GPS-07 replay on the earlier corpus gave 0 re-seeds with either. Validate against **both** corpora
before choosing, then rerun `test_gps_filter`.

---

## 5. GPS-08 (new) — the barometric altitude is off by default and cannot survive a reboot

### 5.1 Nothing ever arms it

`fBasePress` is assigned in exactly two places in the entire firmware:

- `src/bmx280.cpp:28` — the definition, `float fBasePress = 0;`
- `src/command_functions.cpp:2287` — the `--setpress` handler.

There is no third. So on any node whose operator has not typed `--setpress`, `getPressALT()`
(`src/bmx280.cpp:308`) hits its guard at `src/bmx280.cpp:310`:

```c
if(fPress == 0.0 || fBasePress == 0.0)
    return 0;
```

and returns 0 forever. `DK5EN-93` demonstrated this before arming: `--info` reported `ALT asl: 0 m`
with a working, detected BME280 (`BMP280: off / BME280: on (found)`).

`fBasePress` is a plain RAM global — it does not appear in `esp32_flash.h` or `WisBlock-API.h`, so
**it is not persisted**. Every reboot returns the node to the unarmed state. Combined with the above, this
means the barometric altitude channel is dead on essentially every node in the field, permanently,
unless an operator re-types `--setpress` after every single boot.

Contrast with QNH: `getPressASL()` (`src/bmx280.cpp:320`) self-latches `fBaseAltidude` at
`src/bmx280.cpp:327` via `baroBaseLatchAllowed()`, so **QNH works and barometric altitude does not**,
despite being computed from the same sensor two lines apart. Before the latch, QNH is reported as
QFE — this run showed `qnh;971.13` equal to `qfe;971.13` until `--setpress`, then `qnh;1025.35`,
which is the correct reduction for 474 m. That pre-latch window is the GPS-04 failure mode, and
GPS-04 is closed, so it is expected here and is not a new defect: it lasted 255 s, until convergence.

### 5.2 The anchor is one arbitrary sample

Even armed, the absolute accuracy of the barometric altitude is inherited wholesale from a single
GPS sample at a single instant. `baroBaseRelatch()` (`src/gps_functions.cpp:1370`) is called from
`src/gps_functions.cpp:1195` the moment `P` first drops below `ALT_KF_P_CONV`.

In this run that instant gave 474.0 m. The GPS's own mean over the following two hours was 486.5 m.
Latching a few minutes earlier or later would have moved the entire barometric channel by tens of
metres — the per-half-hour table in §3 shows the GPS mean itself ranging over 10 m between windows,
and the raw sample ranging over 91 m within them.

So the barometric channel as it stands is **precise and arbitrarily offset**. That is a fine basis
for a complementary filter (GPS-05b, §7), where the GPS supplies the long-term reference and the
barometer only the short-term shape. It is a poor basis for reporting `node_press_alt` as an
absolute altitude, which is what the firmware does today.

### 5.3 Help text does not match behaviour

`src/command_functions.cpp:898` advertises:

```
--setpress 999.9  set QNH reference
```

The handler at `src/command_functions.cpp:2281` parses no argument. It latches the _current_ QFE and
the _current_ `node_alt`. A user following the help text and typing `--setpress 1013.2` gets the
same behaviour as bare `--setpress`, silently. Either parse the argument or fix the help line;
do not leave both.

### 5.4 Platform symmetry

The nRF52 path is identical and has the same defect: `src/nrf52/nrf52_main.cpp:2352` calls the same
`getPressALT()` on the same 60 s timer (`src/nrf52/nrf52_main.cpp:2334`). Any fix must land on both.

---

## 6. GPS-09 (new) — `node_press_alt` is an `int`, and that costs a factor of two

**Sites:** `src/esp32/esp32_flash.h:194` and `src/nrf52/WisBlock-API.h:361`, `:597` —
`int node_press_alt = 0;` (`node_press_asl` beside it is already a `float`).

One metre of altitude is about 0.12 hPa, well above what a BME280 resolves. Quantising the
barometric altitude to whole metres therefore throws away most of the sensor's resolution. Measured
on this capture, recomputing the same quantity in `float` from the logged QFE using the firmware's
own formula (`src/bmx280.cpp:308`) improves short-term stability by roughly 2×:

| quantity  | as reported (`int`) | recomputed (`float`) |
| --------- | ------------------: | -------------------: |
| ADEV 30 s |                0.20 |                 0.10 |
| ADEV 1 m  |                0.25 |                 0.13 |
| sd        |                1.36 |                 1.34 |

The long-term figures are unaffected, as expected — quantisation is a noise floor, not a drift.

This matters only once something consumes the value at sub-metre precision, i.e. it matters _for
GPS-05b_. As a display field, whole metres are fine. See §9 for why the fix is probably not to
change the struct field's type.

---

## 7. GPS-05b — the complementary filter now has its measurement

This item is filed Low with the note "Do not start before GPS-01 lands and TRACK-mode logs with
pressure exist". **GPS-01 landed and closed on 2026-09-03.** The second condition is still unmet:
this capture is stationary, not TRACK. What it does supply is the stationary-case number the item
was missing.

Simulated offline on the captured series — GPS low-passed, barometer high-passed, single-pole,
irregular-sample form (`out = LPF(gps) + (baro − LPF(baro))`, so any common reference cancels and
the barometer's arbitrary anchor from §5.2 drops out). First eighth of the window dropped as
warm-up:

| τ          |       sd |       p2p | ADEV 30 s | ADEV 5 m | ADEV 30 m |
| ---------- | -------: | --------: | --------: | -------: | --------: |
| 5 min      |    10.14 |     45.57 |      0.94 |     5.81 |      6.98 |
| 15 min     |     5.50 |     22.55 |      0.39 |     2.73 |      2.30 |
| **30 min** | **3.27** | **13.73** |  **0.23** | **1.57** |  **0.83** |
| 60 min     |     2.76 |     11.00 |      0.16 |     0.92 |      1.96 |
| 120 min    |     2.60 |     10.04 |      0.12 |     0.59 |      2.03 |
| 360 min    |     2.00 |      7.43 |      0.11 |     0.37 |      1.54 |

Against today's reported `node_alt` (sd 17.16 m, ADEV 30 m 5.94 m), τ = 30 min gives **5.2× better
spread and 7.2× better 30-minute stability**. τ = 30 min is the knee: below it the GPS wander leaks
through, above it sd keeps improving slowly but ADEV at 30 m gets _worse_ again (1.96 at 60 min,
2.03 at 120 min) as the filter starts tracking the barometer's own weather drift over exactly that
timescale. Do not simply pick the largest τ.

**Honest limits of this result, state them in any PR:**

- Stationary node, one board, one 2 h window, one weather situation (a steady 0.87 hPa fall).
- No truth altitude, so this is a stability result and not an accuracy result.
- Says nothing about TRACK mode, which is where GPS-05b's real difficulty lies and where the
  barometer's temperature dependence and cabin-pressure effects appear.
- The τ sweep is an offline replay of a filter that does not exist in the firmware yet. It shares no
  code with what an implementation would run.

**Runtime discriminator already exists:** `bBMPON` / `bBMEON` / `bBME680ON`
(`src/loop_functions_extern.h:96`, `:100-101`). Boards without a sensor keep the GPS-only path unchanged.

---

## 8. The three sensor families do not agree on what "barometric altitude" means

Not a defect to fix in this pass, but do not assume one code path.

| sensor | how `node_press_alt` is produced                               | site                 |
| ------ | -------------------------------------------------------------- | -------------------- |
| BMx280 | `−7990·ln(QFE/fBasePress) + fBaseAltidude`                     | `src/bmx280.cpp:308` |
| BME680 | `bme.readAltitude(SEALEVELPRESSURE_HPA + COMPENSATE_ALTITUDE)` | `src/bme680.cpp:159` |
| BMP390 | `bmp.readAltitude(SEALEVELPRESSURE_HPA)`                       | `src/bmp390.cpp:100` |

Only the BMx280 path uses the latched local reference. The other two reduce against a fixed
sea-level constant, which is a different quantity with a different error behaviour — and neither
consults `fBasePress`, so GPS-08 does not affect them the same way. Note also
`src/bme680.cpp:33`: `//float fBasePress680 = 0;  // currently not used`, a half-finished port of
the BMx280 approach.

GPS-05b must decide which of these it consumes, or normalise them first. **This capture only
exercises the BMx280 path.**

---

## 9. Rejected fixes — read before coding

**Do not make `node_press_alt` a `float` in the settings struct as the fix for GPS-09.** `int` and
`float` are both 4 bytes, so the struct size is unchanged and no `FLASH_STRUCT_VERSION` bump is
forced on size grounds — but a value already in flash would be silently reinterpreted as a float
bit pattern on the first boot after the update. It is recomputed within 60 s, so the damage is
transient, but it will be visible on the dashboard and in any beacon sent in that window. The
cheaper fix is to keep the persisted field as it is and carry `float` precision only on the path
that needs it, i.e. inside the GPS-05b filter, which consumes `getPressALT()`'s inputs rather than
its rounded output.

**Do not persist `fBasePress` reflexively as the fix for GPS-08.** Adding a field to
`meshcom_settings` bumps `FLASH_STRUCT_VERSION` (`src/configuration_global.h:83`, currently `20260724`; the rationale comment above it at
`:61-77` is worth reading first), and that **wipes settings on every node in the fleet** at
update. It also re-anchors on a stale pressure after a long power-off, which is worse than no
anchor. The strategy is deliberately left to the implementer, but the cheap option that needs no new
persisted state is to latch `fBasePress` from the current QFE at the same moment
`baroBaseRelatch()` already latches the altitude — filter convergence, `src/gps_functions.cpp:1195`
— rebuilding the reference a few minutes after every boot. Whatever you choose, write down why in
the commit.

**Do not add HDOP weighting to `R`.** Rejected on measurements in BACKLOG §7.6 and independently
confirmed here: §4's re-seed table shows HDOP 1.0–1.9 across excursions of 60 m.

**Do not treat the barometric channel as ground truth.** §5.2. It is precise and arbitrarily
offset.

---

## 10. Verification — what "fixed" has to mean

The instrumentation in Appendix A is the instrument for all four items; keep it out of any PR but
use it to prove the fix. `DK5EN-93` is currently still running the instrumented build.

| item    | gate                                                                                                                                                                                                                                                                  |
| ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GPS-07  | Replay the constant change against **both** corpora (`~/Downloads/meshlog-20260903/14.log` and this run) → 0 re-seeds on both. Rerun `test_gps_filter`. Then a fresh 2 h bench capture: sd of `node_alt` must fall clearly below the raw sd, which it does not today. |
| GPS-08  | Cold-boot a node with a BMx280, type nothing, wait for filter convergence plus one 60 s WX tick → `--info` reports a non-zero `ALT asl`. Reboot, repeat, same result. Verify on nRF52 too (`src/nrf52/nrf52_main.cpp:2352`).                                          |
| GPS-09  | ADEV at 30 s of the barometric channel halves, matching the `float` column in §6.                                                                                                                                                                                     |
| GPS-05b | A 2 h stationary capture in which the reported altitude has sd ≤ 4 m and 30-min ADEV ≤ 1 m, against the 17.16 m / 5.94 m baseline in §3. Plus a TRACK-mode capture with pressure before anything ships — see §7's limits.                                             |

Regression tests: every bug fix gets one that fails before and passes after. `gps_filter.cpp` is
Arduino-free and already builds on the host (`env:native`), so GPS-07 and any GPS-05b filter core
belong in `test_gps_filter` as replayable series, not as bench-only claims.

---

## Appendix A — the `[ALTB]` instrumentation

Reverted from the tree; kept here so the measurement is reproducible. Applies cleanly to `f3d07372`; it touches only GPS and
ESP32 main-loop code, so the CTY-02 revert does not conflict with it. Adds a read-only accessor for the filter state plus two counters, and one CSV line per
GPS cycle. Also at `~/Downloads/meshlog-20260906/altb-instrumentation.patch`.

Row format:

```
[ALTB];ms;<millis>;utc;HH:MM:SS;fix;<0|1>;sat;<n>;hdop;<x>;age;<ms>;raw;<m>;kf;<m>;P;<m^2>;
rej;<n>;init;<0|1>;conv;<0|1>;upd;<n>;rjt;<n>;nalt;<m>;qfe;<hPa>;balt;<m>;qnh;<hPa>;t;<degC>;
bp;<hPa>;ba;<m>
```

- `raw` = `gpsData.altitude`, the unfiltered sample · `kf` = filter state `x` · `nalt` = reported
  `meshcom_settings.node_alt`
- `bp`/`ba` = `fBasePress` / `fBaseAltidude`, the latched barometric reference
- A re-seed is visible as `P` jumping back to 400 with `kf` equal to `raw`.

Touched: `src/gps_filter.h` (nothing), `src/gps_functions.h` + `src/gps_functions.cpp`
(`WZ_GPS_AltState()`, counters), `src/esp32/esp32_main.cpp` (the marker, before the `iGPSDEBUG`
block). Marked `BENCH-ALTB (temporaer)` throughout.

## Appendix B — data and scripts

Not in the repo, following house practice for raw captures. `~/Downloads/meshlog-20260906/`:

| file                         | what                                                                  |
| ---------------------------- | --------------------------------------------------------------------- |
| `dk5en-93-altb.log`          | the 2 h capture, 2.6 MB, 2371 `[ALTB]` rows plus the full console log |
| `altlog.py`                  | capture driver; arms `--setpress` on `conv;1` + first QFE             |
| `analyze_alt.py`             | the §3 table — stats, drift, Allan deviation, complementary filter    |
| `extra.py`                   | the §4 re-seed list, §7 τ sweep, detrended correlation                |
| `altb-instrumentation.patch` | Appendix A                                                            |

Reproduce §3 and §7:

```sh
python3 analyze_alt.py dk5en-93-altb.log --skip 15
python3 extra.py dk5en-93-altb.log
```

`analyze_alt.py` takes `--truth <m>` to add a bias column, once a surveyed altitude for the site
exists.
