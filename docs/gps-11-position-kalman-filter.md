# GPS-11: HDOP-weighted Kalman filter for the horizontal position

Feature specification for the MeshCom firmware fork (`MeshCom-Firmware-DEV-Main`, branch `fork-dev`).
Written 2026-10-17 for an AI coding agent that implements it later. Line numbers refer to `fork-dev`
at that date. Re-locate them by symbol before editing.

## 1. Summary

Today only the **altitude** is filtered (GPS-03, `src/gps_filter.{h,cpp}`). Latitude and longitude
go raw from TinyGPS++ into `meshcom_settings.node_lat/lon`, into smart beaconing and on air. HDOP
is used once, as a hard pass/fail gate (`HDOP < 6 && sats > 5`), and never to weight a sample.

GPS-11 adds a pure C, host-testable 2D Kalman filter for the horizontal position. Each position
sample is weighted by its HDOP, and the GNSS speed and course enter as a velocity measurement. A
stationary node then settles on one stable position instead of wandering over the coordinate grid.
A moving node gets a smoother track without visible lag.

## 1a. Revisions after wave W1 (2026-10-17)

The implementation deviates from sections 4.3-4.7 and 7 below; the code and this list are authoritative.
Rationale and simulation numbers: [`campaign-gps11-position-kf.md`](campaign-gps11-position-kf.md).

- **FIXED mode** (`!bDisplayTrack`) is no constant-velocity filter. Each axis is an HDOP-weighted exponential
  average whose window grows from the seed to `POS_KF_FIXED_TAU_S` (30 min); velocity is held at 0. Reason:
  the CV+ZUPT model is a ~17 s low-pass, and multipath is correlated over 60-120 s, so G1 was out of reach.
  TRACK keeps the CV+ZUPT model of 4.3/4.4. A mode switch never resets; the output stays continuous.
- **Motion detection with `--track off`** (operator review 2026-10-17: a walker with `--track off` lagged
  ~100 m and jumped). FIXED requests run a still/moving sub-state: moving after 10 s of speed evidence
  (>= 0.5 m/s) or when the smoothed raw samples stay beyond an adaptive radius (40-60 m) for 15 s; still
  again after 120 s within 30 m. Moving uses the TRACK model; `--track on` always does.
- **Gate:** innovations up to `POS_KF_GATE_MIN_M` (50 m) are always accepted; d2 applies beyond that.
  Re-seed is time-based (`POS_KF_RESEED_MS`, 60 s of rejects), not a sample count.
- **API:** `posFilterUpdate()` returns `enum PosFilterResult { POS_KF_SKIP, POS_KF_REJECT, POS_KF_OK }`;
  the caller advances its dt timestamp on every non-SKIP call. `posFilterSeed()` takes `hdop`.
  `vel_valid` means "speed is new from this fix"; the course is read only at speed >= `POS_KF_STILL_MPS`,
  so a ZUPT also runs when the receiver leaves the course empty.
- **Grid hysteresis** (new, `src/pos_grid_hold.{h,cpp}`): in FIXED mode the value handed to `cround4()` /
  `PositionToAPRS()` keeps its on-air `DDMM.MM` cell until the estimate is more than 0.3 cell beyond the
  border. This supersedes the "no change to precision" non-goal only in that the output is held, never
  made finer or coarser.
- **Tests:** spec case 3 is pinned on the 30 m outlier in TRACK (accepted at both HDOPs after the 50 m
  gate minimum); new cases: 15 m step held 120 s, reject-timer reset, correlated FIXED noise, mode switch.

## 2. Background

### 2.1 Field report

MeshCom Telegram group, topic General, 2026-09-15 (tgreader uids 448, 443, 441, 440, 439):

- DK1OM tracks positions with a T-Deck Plus. Positions received from a MeshCom node (DB0TEN-12)
  "hop" in **evenly spaced steps**, even with the receiver lying on a table. A plain LoRa-APRS
  station also jitters, but randomly.
- OE1KBC: the cause is GPS multipath (car body, neighbouring roofs). In LoRa tracking, position
  sending pauses while stationary (`POSINFO_INTERVAL`, 1800 s). He switches GPS off on fixed nodes.
  Positions are "rounded to 4 decimals".

### 2.2 Why the steps are even

The position is quantised twice before it goes on air:

1. `cround4abs()` / `cround4()` (`src/loop_functions.cpp:6203`) cut to 1e-4 degree, about 11.1 m
   north-south and about 7.4 m east-west at 48°N.
2. `PositionToAPRS()` (`src/loop_functions.cpp:4637`, format at `:4935`) writes the APRS
   `DDMM.MM` format. 0.01 arc minute is about 18.5 m north-south and about 12.4 m east-west at 48°N.

`extudp_functions.cpp:618` also truncates (not rounds) to 1e-4 degree for the extended UDP output.

So the receiver sees raw GPS wander (multipath, typically 5 to 30 m) sampled onto an 18.5 m by
12.4 m grid. That gives the regular steps. **A filter cannot remove the grid, and it is not meant
to.** It keeps the underlying estimate inside one grid cell while the node is stationary, so the
transmitted position stops flipping between cells.

### 2.3 What exists today

| Area               | State                                                                                                      | Where                                                   |
| ------------------ | ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| Altitude filter    | Scalar Kalman, dt-scaled Q, 30 m innovation gate, re-seed after 60 rejects, reset in TRACK                 | `src/gps_filter.{h,cpp}`, `gps_functions.cpp:1175-1247` |
| Altitude fusion    | Complementary filter of Kalman altitude and barometer (GPS-05b)                                            | `src/alt_fusion.h`                                      |
| Plausibility       | Rejects null island, out-of-range lat/lon/alt, impossible dates                                            | `gpsSamplePlausible()` in `gps_filter.cpp`              |
| Fix gate           | `fposinfo_hdop < 6.0 && posinfo_satcount > 5 && bPlausible` -> `posinfo_fix`                               | `gps_functions.cpp:1092`                                |
| Position           | Raw `gpsData.latitude/longitude` -> `cround4abs` -> `node_lat/lon`, hemisphere from `rawLat().negative`    | `gps_functions.cpp:1141-1173`                           |
| Smart beaconing    | Distance and course from successive raw positions; stationary when GNSS speed < 1 m/s and distance < 100 m | `setSMartBeaconing()`, `loop_functions.cpp:5976`        |
| Host tests         | Unity, `[env:native]`, `test_filter` list in `platformio.ini`                                              | `test/test_gps_filter/`, `test/test_alt_fusion/`        |
| Evaluation cadence | ESP32 about 3 s, nRF52 about 1 s (`gps_refresh_intervall`)                                                 | `esp32_main.cpp:3982-3991`                              |

Note: the commit message of `51c3c317` ("GPS-Kalmanfilter") says the filter covers "Position und
Höhe" (position and altitude). The code only ever filtered altitude. GPS-11 closes that gap.

## 3. Goals and non-goals

**Goals**

- G1: A stationary node with a valid fix keeps its transmitted position within one APRS grid cell
  for at least 95 % of transmissions over a 2 h capture with typical multipath (window sill, car).
- G2: HDOP weights each sample continuously. A sample at HDOP 4 counts much less than one at
  HDOP 1. The existing hard gate (`HDOP < 6`) stays as the outer limit.
- G3: A moving node (TRACK) shows no systematic lag: at a constant 50 km/h the filtered position is
  within 15 m of the raw position along track, measured after 30 s of steady motion.
- G4: Smart beaconing distance and course use the filtered position. Multipath no longer triggers
  false distance shots or course changes while stationary.
- G5: Same behaviour on ESP32 (3 s) and nRF52 (1 s): everything scales with real elapsed time.

**Non-goals**

- No change to the on-air or UDP wire format, and no change to the 1e-4 degree or `DDMM.MM`
  precision. More precision is a separate topic (see section 9).
- No change to the altitude filter or the barometer fusion.
- No change to the fix gate thresholds (HDOP 6, 5 satellites) or to `gpsSamplePlausible()`.
- No dead reckoning without GNSS. Without a fix, the filter does not propagate a position.

## 4. Design

### 4.1 Module

New files **`src/pos_filter.h`** and **`src/pos_filter.cpp`**, following `gps_filter.{h,cpp}`:

- Pure C-compatible API, no Arduino dependencies, builds on the host.
- A plain struct for the state, functions take a pointer. No globals, no `millis()` inside: the
  caller passes `dt_ms`.
- Constants as `#define` with a one-line rationale comment each, as in `gps_filter.h`.

### 4.2 Coordinates

The filter runs in a **local tangent plane in metres**, not in degrees:

- Anchor `(lat0, lon0)` in `double`, set at seed time.
- `x_east  = (lon - lon0) * cos(lat0_rad) * R_E * pi/180`
- `y_north = (lat - lat0) * R_E * pi/180`, with `R_E = 6371000.0`.
- Back-conversion is the inverse. Filter in signed degrees, so the hemisphere comes out correctly.
- **Re-anchor** when `|x|` or `|y|` exceeds 5 km: convert the estimate back to lat/lon, set it as
  the new anchor, set x and y to 0, keep velocity and covariance. This keeps the small-angle error
  negligible on long drives.
- Use `double` for anchor and conversion and `float` for the filter state. At a 1 Hz rate the
  software-double cost on ESP32 and nRF52 is irrelevant.

### 4.3 State and model

Two independent per-axis constant-velocity filters (east and north). Each has the state
`[p, v]` (position in m, velocity in m/s) and a 2x2 covariance `P`.

Prediction with `dt` in seconds (clamped, see 4.6):

```
p = p + v*dt
P = F P F^T + Q,   F = [[1, dt], [0, 1]]
Q = q * [[dt^3/3, dt^2/2], [dt^2/2, dt]]       (white-noise acceleration)
```

`q` (acceleration spectral density, m^2/s^3) depends on the mode:

| Mode                                     | `q`   | Reason                                              |
| ---------------------------------------- | ----- | --------------------------------------------------- |
| Not TRACK (fixed node, `!bDisplayTrack`) | 0.001 | Node does not move; only the slow drift is followed |
| TRACK, GNSS speed < `POS_KF_STILL_MPS`   | 0.05  | Standing at a traffic light: low, but able to start |
| TRACK, moving                            | 2.0   | Car and bike manoeuvres, about 1.4 m/s^2 over 1 s   |

Separate axes are a deliberate simplification. HDOP gives a horizontal error and no ellipse, so
an east-north cross-covariance cannot be fed in anyway.

### 4.4 Measurements

**Position (every new fix):**

```
sigma_axis = POS_KF_UERE_M * max(hdop, POS_KF_HDOP_FLOOR) / sqrt(2)
R_pos      = sigma_axis^2
```

- `POS_KF_UERE_M = 4.0` m (user equivalent range error of a consumer L1 receiver with multipath,
  same order as the 4 m that `ALT_KF_R` in `gps_filter.h` uses).
- `POS_KF_HDOP_FLOOR = 0.8`: receivers report optimistic HDOP values under 1 when many satellites
  are in view, but multipath does not shrink with them.
- `/ sqrt(2)`: HDOP describes the horizontal radius, split evenly across two axes.

**Velocity (every new RMC, when `gps.speed.isValid()` and `gps.course.isValid()`):**

```
v_east  = speed_mps * sin(course_rad)
v_north = speed_mps * cos(course_rad)
R_vel   = POS_KF_SIGMA_V^2,   POS_KF_SIGMA_V = 0.5 m/s
```

Doppler velocity is much less affected by multipath than position. Feeding it in is what removes
the lag in TRACK (goal G3) without making the filter jumpy.

**Zero-velocity update:** when `speed_mps < POS_KF_STILL_MPS` (0.5 m/s), the course is noise.
Feed `v_east = v_north = 0` with `R_vel = POS_KF_SIGMA_V0^2` (0.2 m/s) instead. This pins a
stationary node and is the main lever for goal G1.

The position and velocity measurements are applied as two sequential scalar updates per axis.
No matrix inversion is needed.

### 4.5 Innovation gate and re-seed

Same scheme as the altitude filter:

- Normalised innovation squared over both axes for the position update:
  `d2 = ix^2/Sx + iy^2/Sy`, with `S = P_pp + R_pos` per axis.
- Reject the position update when `d2 > POS_KF_GATE_D2` (13.8, chi-square 2 degrees of freedom,
  99.9 %). The prediction and the velocity update still run.
- `rejects` counts consecutive position rejects. When it reaches `POS_KF_RESEED_N` (10), seed again
  on the current sample: the node really moved (cold start, power-on far away, ferry).
- `posFilterUpdate()` returns `false` for a rejected sample, like `altFilterUpdate()`.

### 4.6 Time handling, seeding and reset

- `dt_ms` is the wall time since the last accepted update, measured by the caller with `millis()`
  and rollover-safe subtraction (`nowMs - lastMs` with `uint32_t`, as at `gps_functions.cpp:1198`).
- Clamp `dt_ms` to `POS_KF_DT_MAX_MS` (30000). A longer gap re-seeds instead (see below).
- **Seed** on the first accepted sample: `p = 0` at the anchor, `v` = the GNSS velocity if valid,
  otherwise 0. `P_pp = R_pos`, `P_vv = (2 m/s)^2`, `P_pv = 0`.
- **Reset** (next sample seeds) when:
  - there was no accepted fix for longer than `POS_KF_DT_MAX_MS`;
  - GPS is switched off (`--gps off`) or the GPS module is re-initialised (next to
    `altFilterReset(&s_alt)` at `gps_functions.cpp:902`);
  - the position is set manually (`--setlat`, `--setlon`, or the BLE/app position setter).
    Find the setters through the shared position setter added in `c3c87b45`.
- Do **not** reset when entering or leaving TRACK. Only `q` changes. This differs from the altitude
  filter on purpose: the horizontal filter is built for motion, the altitude filter is not.

### 4.7 API (proposal)

```c
struct PosFilterAxis { float p, v, Ppp, Ppv, Pvv; };

struct PosFilter
{
    double  lat0, lon0;      /* anchor, signed degrees              */
    double  cos_lat0;
    struct PosFilterAxis e;  /* east  */
    struct PosFilterAxis n;  /* north */
    uint8_t rejects;
    bool    init;
};

enum PosFilterMode { POS_KF_MODE_FIXED, POS_KF_MODE_TRACK };

void posFilterReset(struct PosFilter *f);
void posFilterSeed(struct PosFilter *f, double lat, double lon,
                   bool vel_valid, float speed_mps, float course_deg);

/* One fix. hdop <= 0 or NaN: sample skipped, returns false. */
bool posFilterUpdate(struct PosFilter *f, double lat, double lon, float hdop,
                     bool vel_valid, float speed_mps, float course_deg,
                     enum PosFilterMode mode, uint32_t dt_ms);

void  posFilterGet(const struct PosFilter *f, double *lat, double *lon);
float posFilterSigmaM(const struct PosFilter *f);   /* sqrt(Ppp_e + Ppp_n), for debug */
```

## 5. Integration in `gps_functions.cpp`

1. **Static state** next to `s_alt` (line 111): `static struct PosFilter s_pos;` and
   `static uint32_t s_posLastMs;`.
2. **New-sample detection:** read `gps.location.isUpdated()`, `gps.speed.isUpdated()` and
   `gps.course.isUpdated()` **before** the accessors at lines 1037-1051. In TinyGPS++, reading a
   value clears its updated flag. This is the same trap as GPS-03/F6 (comment at line 1041). Only
   a really new location goes into the filter.
3. **Update** in the fix branch (`WZ_GPS_HasFix() && has_gnss_location`, line 1114), before the
   position block at line 1140: compute `dt_ms`, call `posFilterUpdate()` with `gpsData.hdop`, the
   validity of speed and course, and `bDisplayTrack ? POS_KF_MODE_TRACK : POS_KF_MODE_FIXED`.
4. **Use the estimate:** take `dlat`/`dlon` from `posFilterGet()` instead of
   `gpsData.latitude/longitude` (lines 1143-1144). Derive `node_lat_c`/`node_lon_c` from the sign
   of the filtered value instead of `gps.location.rawLat().negative` / `rawLng().negative`
   (lines 1165-1173). Everything downstream (`cround4`, `save_position()`, `setSMartBeaconing()`,
   `PositionToAPRS()`) then uses the filtered value unchanged.
5. **No-fix branch** (line 1267): do not touch the filter. A fix lost for longer than
   `POS_KF_DT_MAX_MS` leads to a seed at the next fix (section 4.6).
6. **Debug output** at `iGPSDEBUG > 1`, in the style of the `alt fused` line (line 1225):
   `[GPS ]...pos raw:%.6lf,%.6lf kf:%.6lf,%.6lf hdop:%.1f sig:%.1fm rej:%u`.
7. **Not in scope:** `setSMartBeaconing()` itself does not change. With filtered input its
   100 m stationary threshold and distance triggers simply work better. Do not retune them in this
   change, so the effect stays measurable.

Optional, only when it fits the settings conventions (`src/settings_schema.{h,cpp}`) without
changing the flash layout: a runtime switch `--gpsfilter on|off` (default on) for field A/B tests.
If a persistent setting needs a schema change, leave it out and report that.

## 6. Tests

### 6.1 Host tests (required)

New suite `test/test_pos_filter/test_main.cpp` (Unity), added to `test_filter` in `[env:native]`
in `platformio.ini`, and a row in `docs/test-suite-map.md`. Cases:

1. **Seed:** the first sample is returned unchanged, `init` is true.
2. **Stationary noise:** 1 h of synthetic samples at 1 s, true point fixed, Gaussian noise
   sigma 8 m plus a slow random walk of 3 m. The filtered standard deviation is at most a quarter
   of the raw one. Use a seeded PRNG, so the test is deterministic.
3. **HDOP weighting:** the same outlier of 30 m at HDOP 1.0 moves the estimate more than at
   HDOP 5.0, by at least a factor of 3.
4. **HDOP floor and invalid HDOP:** HDOP 0.3 behaves like 0.8. HDOP 0, negative or NaN skips the
   sample and returns false.
5. **Gate:** a single 500 m jump is rejected and the estimate stays. Ten consecutive samples at the
   new point re-seed it.
6. **Constant velocity:** 50 km/h straight line with noise and a velocity measurement. After 30 s
   the along-track error is under 15 m (goal G3).
7. **Turn:** a 90° turn at 30 km/h. Overshoot past the corner stays under 20 m.
8. **Cadence independence:** the same trajectory sampled at 1 s and at 3 s ends within 2 m of
   each other (goal G5).
9. **dt clamp:** `dt_ms = 600000` does not let `P` grow beyond the clamp value.
10. **Re-anchor:** a 20 km drive crosses the 5 km re-anchor threshold several times without a jump
    over 0.5 m in the output.
11. **Hemispheres and anti-meridian:** seeds at S/W coordinates come back with the right sign.
    Crossing longitude ±180 is out of scope, but must not crash: document the behaviour in a test.
12. **Zero-velocity update:** stationary with speed 0.2 m/s and random course, the velocity
    estimate stays under 0.3 m/s.

### 6.2 Gate before commit

The repo's usual gate: `pio test -e native` green, plus a firmware build for at least one ESP32
target (T-Deck Plus) and one nRF52 target (RAK4631). Follow the repo `CLAUDE.md` for the exact
commands and the bench harness.

### 6.3 Bench validation (required before a PR)

- Two captures with `--gpsdebug 2`, each at least 2 h: a node on a window sill (multipath), and
  a drive in TRACK mode with a T-Deck Plus.
- Evaluate with the debug line from section 5 step 6: raw vs filtered spread while stationary
  (goal G1, counted in APRS grid cells after `DDMM.MM` formatting), along-track lag while driving
  (goal G3), and the number of smart-beaconing distance shots while stationary (goal G4).
- Store the result under `docs/` following the pattern of `docs/bug-baro-altitude-20260906.md`,
  and tune the constants in section 7 from it. The values there are starting values, not results.

## 7. Parameters (starting values)

| Constant             | Value | Unit    | Meaning                                                                      |
| -------------------- | ----- | ------- | ---------------------------------------------------------------------------- |
| `POS_KF_UERE_M`      | 4.0   | m       | Range error that HDOP is multiplied with                                     |
| `POS_KF_HDOP_FLOOR`  | 0.8   |         | Lower limit for HDOP                                                         |
| `POS_KF_SIGMA_V`     | 0.5   | m/s     | Noise of the Doppler velocity                                                |
| `POS_KF_SIGMA_V0`    | 0.2   | m/s     | Noise of the zero-velocity pseudo-measurement                                |
| `POS_KF_STILL_MPS`   | 0.5   | m/s     | Below this, zero-velocity update instead of course                           |
| `POS_KF_FIXED_TAU_S` | 1800  | s       | FIXED mode: horizon of the HDOP-weighted average (replaces `POS_KF_Q_FIXED`) |
| `POS_KF_Q_STILL`     | 0.05  | m^2/s^3 | Process noise, TRACK standing                                                |
| `POS_KF_Q_MOVING`    | 2.0   | m^2/s^3 | Process noise, TRACK moving                                                  |
| `POS_KF_GATE_D2`     | 13.8  |         | Chi-square 2 degrees of freedom, 99.9 %                                      |
| `POS_KF_RESEED_MS`   | 60000 | ms      | Rejected position updates for this long re-seed (replaces `POS_KF_RESEED_N`) |
| `POS_KF_DT_MAX_MS`   | 30000 | ms      | dt clamp, longer gap re-seeds                                                |
| `POS_KF_REANCHOR_M`  | 5000  | m       | Re-anchor distance of the local plane                                        |
| `POS_KF_GATE_MIN_M`  | 50    | m       | Innovations up to this distance are always accepted                          |
| `POS_KF_P_VV0`       | 4.0   | m^2/s^2 | Velocity covariance after a seed                                             |
| `POS_GRID_HYST`      | 0.3   | cell    | Output hysteresis on the `DDMM.MM` grid, only while still                    |

## 8. Acceptance criteria

- [ ] `src/pos_filter.{h,cpp}` exists, pure C API, builds on the host, no Arduino headers.
- [ ] `test/test_pos_filter` has the 12 cases from section 6.1, all green, registered in
      `platformio.ini` and `docs/test-suite-map.md`.
- [ ] `gps_functions.cpp` uses the filtered position for `node_lat/lon`, hemisphere,
      `save_position()` and `setSMartBeaconing()`. The `isUpdated()` flags are read before the
      accessors.
- [ ] The altitude path (`s_alt`, `s_altFusion`) is unchanged byte for byte.
- [ ] The wire format is unchanged: a golden or regression test that covers `PositionToAPRS()`
      still passes without updating its expected output.
- [ ] Firmware builds for an ESP32 and an nRF52 target without new warnings.
- [ ] Bench report in `docs/` with the numbers for goals G1, G3 and G4.
- [ ] `docs/BACKLOG.md` has an entry GPS-11 that references this specification.

## 9. Open points and later work

- **Precision on air:** `DDMM.MM` limits the position to about 18.5 m. APRS 1.2 has the `!DAO!`
  extension (one more digit, about 1.85 m) and the compressed format (about 0.3 m).
  `encodeLoRaAPRScompressed()` exists (`loop_functions.cpp:5055`). Whether MeshCom receivers and
  the server parse it is a separate decision. It needs upstream (OE1KBC) agreement, because it
  changes what other nodes see.
- **`extudp_functions.cpp:618`** truncates instead of rounding. That is a small bias of up to
  1e-4 degree towards zero. Fixing it is a one-liner, but it is a separate change with its own
  regression test.
- **HDOP versus accuracy:** some modules (u-blox) report a real accuracy estimate (`hAcc` in UBX,
  or GST sentences). If such a module is detected, `sigma_axis` could use it instead of
  `UERE * HDOP`. Out of scope for GPS-11.
- **Commit message of `51c3c317`:** it says "Position und Höhe". Mention in the GPS-11 PR text that
  the position part only arrives with GPS-11.
