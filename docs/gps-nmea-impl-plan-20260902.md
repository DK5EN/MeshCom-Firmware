# Implementation plan GPS-01..GPS-04 — lossless NMEA drain, plausibility gate, altitude estimate, QNH re-latch

**Status: IMPLEMENTED 2026-09-02 (waves 1–2 incl. review fixes F1–F10), bench wave 3
open (no node attached).** Operator decisions of 2026-09-02 are
recorded in §1. Execution with `/orchestrate-waves` in its own worktree. Analysis and
measurements: [`bug-GPS-uart-overflow-20260901.md`](bug-GPS-uart-overflow-20260901.md)
(§4 root cause, §7 fix, §7.6 filter measurements, §8 verification owed). Backlog rows:
[`BACKLOG.md`](BACKLOG.md) §3.8r. Code sites verified against `v4.35p_prio` @ `16c0733f`
(= `upstream/dev` @ `6a613547` plus fork commits; every GPS/QNH file named below is
byte-identical to upstream, only `esp32_main.cpp` carries the TM-51 banner, +27 lines
above the GPS call site).

## 1. Decisions (operator, 2026-09-02)

| Topic                | Decision                                                                                                                                                                                                            |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Worktree base        | `v4.35p_prio` (fork), not `upstream/dev`. The fork has the native test env, `test/support`, `tools/ram_snapshot.py`. The PR is cut afterwards as a per-feature patch onto `upstream/dev` (§8).                      |
| Scope                | GPS-01, GPS-02, GPS-03, GPS-04. **TM-52 dropped** from this campaign (probable cause is recorded in §9 for a later ticket). GPS-05a/b out (no field evidence). TM-51 already fixed (`e108fa98`).                    |
| `--setalt` semantics | **Manual value seeds the filter only**: `--setalt N` writes `node_alt = N`, sets the filter state `x = N`, resets `P = P0`, and re-latches the QNH reference. GPS keeps refining afterwards. No new persisted flag. |
| PR packaging         | **One PR** with all four items; this plan is attached to the PR text for Kurt.                                                                                                                                      |
| Bench node           | **DK5EN-14** (T-Deck Plus, L76K — same GNSS module as the field node). Baseline 2 h at 3 s cadence, TRACK 2 h at 1 s cadence (falsification, doc §10.1), 2 h after the fix.                                         |

## 2. Binding constraints for every agent

1. **Zero new static RAM beyond the filter state** (two floats, one counter, one flag).
   No `setRxBufferSize`, no line buffers. The E22-DevKitC has ~1.7 kB DRAM headroom
   (`CS-03`); `tools/ram_snapshot.py` against `tools/resource_baseline.json` is part of
   the gate.
2. **Upstream-able.** Nothing in `src/` may depend on fork-only paths; comments must
   not reference `docs/` or `test/`. The PR is cut as a patch (§8).
3. **No behaviour change without GPS.** Boards without `ENABLE_GPS`, or with `--gps off`,
   or with `gpsDetected == false`, must compile and behave exactly as today. Every
   new call is guarded the way the existing call site is (`#ifdef ENABLE_GPS`, `bGPSON`,
   `gpsDetected`).
4. **Platform symmetry.** ESP32 and nRF52 get the same feed split (doc §7.2).
5. **Pure logic lives in a new Arduino-free file** `src/gps_filter.h` / `src/gps_filter.cpp`
   so it links in `env:native` (`-D NATIVE_BUILD=1`, `test/support/Arduino.h` stubs).
   No `Serial`, no `millis()`, no `meshcom_settings` in that file.
6. **Existing log lines stay byte-identical.** `[GPS ]...fix:` / `...position  :` /
   `...Time <UTC>:` are what `tools/bench/gpsdebug_scan.py` (§6) and the field
   reporter's captures parse. New diagnostics are new lines.
7. `-Werror` is on for esp32 and nrf52_base. Unused-variable warnings from the split
   are build failures.

## 3. Design

### 3.1 GPS-01 — split the UART drain from the evaluation

Today `WZ_GPS_Loop()` (`src/gps_functions.cpp:871-1053`) drains the UART
(`:885-909`) and evaluates the fix (`:911-1050`) in one call that runs every
`gps_refresh_intervall` seconds (3 s, `configuration_global.h:178`; 1 s in TRACK,
`esp32_main.cpp:3101-3104`). The 256-byte Arduino ring overflows every cycle.

- New `void WZ_GPS_Feed(void)` in `gps_functions.cpp`, declared in `gps_functions.h`
  next to `WZ_GPS_Loop` (`:33`). Body: the `while (GPSSerial.available())` loop only
  (`Serial1` under `USE_HELTEC_T114` / `BOARD_T_ECHO`, as today), `gps.encode()` sets
  `updateGPSdata`. Early return when `!gpsDetected`.
- `WZ_GPS_Loop()` loses the drain loop and keeps everything from `if (updateGPSdata)`
  onward. It still returns `igps`.
- **NMEA echo (`iGPSDEBUG > 2`).** Today the loop does `memset(msg_text, 0, maxNMEAline)`
  at `:883` on every call and prints the collected bytes once per call. `msg_text` is the
  **global command buffer** (`loop_functions.cpp:235`), so a per-loop memset is not
  acceptable. New behaviour: only when `iGPSDEBUG > 2`, the feed appends each byte to
  `msg_text[NMEAlineIndex++]`; on `'\n'` (or index at `maxNMEAline-2`) it prints
  `[GPS ]...NMEA: %s` once and resets `NMEAlineIndex = 0`. No memset in the hot path;
  `msg_text[NMEAlineIndex] = 0` before printing is enough. When `iGPSDEBUG <= 2` the
  feed touches neither `msg_text` nor `NMEAlineIndex`. `NMEAlineIndex` is also used by
  `detectBaudrate()` (`:227-272`), which runs at init before `gpsDetected` is set —
  the implementer confirms that ordering and states it in the report.
- **Call sites.** ESP32 `src/esp32/esp32_main.cpp`: immediately before the
  `gps_refresh_intervall` block (fork `:3092-3106`), inside `#ifdef ENABLE_GPS`, guarded
  by `bGPSON && gpsDetected`: `{ INSTR_SECTION("gps_feed"); WZ_GPS_Feed(); }`. The
  existing `{ INSTR_SECTION("gps"); igps = WZ_GPS_Loop(); }` (fork `:3136`) stays where
  it is. nRF52 `src/nrf52/nrf52_main.cpp`: same call directly before
  `if ((millis()-gps_refresh_timer) >= 1000)` (`:1691`), inside the existing
  `ENABLE_GPS` / `gKeyNum == 2` scope; the legacy `ENABLE_RAK_GPS` path (`:1662-1685`)
  is not touched.
- Cost: one `available()` on an empty ring per loop pass in the common case.

### 3.2 GPS-02 — plausibility gate

The commit gate at `gps_functions.cpp:959` is `hdop < 6.0 && sats > 5`; `WZ_GPS_HasFix()`
(`:1064`) adds `isValid() && age_ms < 5000`. Neither rejects a spliced sentence that
happened to pass the NMEA checksum (doc §3.1: `lon:0.000000`, `Date: 2015.14.00`).

- New pure function in `gps_filter.h`. **As implemented, after the review fix wave
  (F5, §3.6), it also takes `alt` and rejects `alt < GPS_ALT_MIN_M (-500) || alt >
GPS_ALT_MAX_M (10000)`** — not part of the original draft below, added because a
  garbage altitude on an otherwise plausible sample reached the filter seed unchecked:

  ```c
  bool gpsSamplePlausible(double lat, double lon, double alt, int year, int month, int day);
  ```

  Returns `false` when `lat == 0.0 || lon == 0.0`, `|lat| > 90.0`, `|lon| > 180.0`,
  `month < 1 || month > 12`, `day < 1 || day > 31`, `year < 2024 || year > 2099`, or `alt`
  outside its range. Null island, impossible calendar, out-of-range angle or altitude.
  Nothing else — HDOP and satellite count stay where they are. **Also added (F1, §3.6):**
  `gpsTimePlausible(int hour, int minute, int second)` and
  `gpsDatePlausible(int year, int month, int day)` guard the clock-write call sites, which
  this gate does not reach.

- Integration: the gate at `:959` becomes `hdop < 6.0 && sats > 5 &&
gpsSamplePlausible(gpsData.latitude, gpsData.longitude, gpsData.year, gpsData.month,
gpsData.day)`. **As implemented, the call also passes `gpsData.altitude`** (F5, §3.6) — the
  signature above already reflects that. A rejected sample therefore takes today's no-fix
  branch (`:1030-1050`): no time set, no position, no altitude, `posinfo_fix = false`.
  Additionally a `static uint16_t gpsRejectCount` increments and, under `iGPSDEBUG > 0`, one
  line `[GPS ]...reject: lat:%.6lf lon:%.6lf date:%04d.%02d.%02d n:%u` is printed with
  `printfdeb`. **As implemented, the counter is not exposed to a caller** — the drafted
  `uint16_t WZ_GPS_RejectCount(void)` accessor was added, found dead in review (F10, §3.6)
  and removed; the counter stays only for the `reject:` log line.

### 3.3 GPS-03 — scalar Kalman filter on the altitude

Doc §7.4 / §7.6 measured: scalar Kalman with constant `R`, `q = 0.01`, `R = 185 m²`,
`P0 = 400 m²`, τ ≈ 410 s, 4.36 → 1.52 m RMS, worst sample 25.4 → 2.6 m, cold start to
±2 m in 282 s. Innovation gate 15 m improves RMS to 1.43 m on the first log.

- `gps_filter.h`:

  ```c
  #define ALT_KF_Q          0.01f
  #define ALT_KF_R          185.0f
  #define ALT_KF_P0         400.0f
  #define ALT_KF_GATE_M     15.0f
  #define ALT_KF_RESEED_N   10
  #define ALT_KF_P_CONV     2.5f

  struct AltFilter { float x; float P; uint8_t rejects; bool init; };

  void  altFilterReset(struct AltFilter *f);                     /* init = false          */
  void  altFilterSeed(struct AltFilter *f, float alt);           /* x = alt, P = P0       */
  bool  altFilterUpdate(struct AltFilter *f, float meas);        /* false = rejected      */
  bool  altFilterConverged(const struct AltFilter *f);           /* init && P < P_CONV    */
  ```

  **As implemented, after the review fix wave (F2, §3.6), `altFilterUpdate` takes a third
  `uint32_t dt_ms` argument** — `bool altFilterUpdate(struct AltFilter *f, float meas,
uint32_t dt_ms);` — and scales the process noise by wall time
  (`P += ALT_KF_Q * dt_ms / ALT_KF_DT_REF_MS`, `dt_ms` clamped to `ALT_KF_DT_MAX_MS`)
  instead of adding a fixed `ALT_KF_Q` per call as drafted below. Without it the nRF52's
  1 s evaluation cadence gave the filter a 136 s time constant against the ESP32's 408 s
  for the same `ALT_KF_Q` — see §3.6.

  `altFilterUpdate`: if `!init` → seed and return `true`. Else `innov = meas - x`;
  if `fabsf(innov) > ALT_KF_GATE_M` → `rejects++`; when `rejects >= ALT_KF_RESEED_N`
  → seed with `meas` (the node really moved, e.g. carried without TRACK) and return
  `true`; otherwise return `false`. Accepted: `rejects = 0; P += Q; K = P/(P+R);
x += K*innov; P *= (1-K)`.
  Convergence: `P` falls from 400 toward the fixed point ≈ 1.36; `P < 2.5` is reached
  after roughly 75 accepted samples (≈ 225 s at 3 s cadence), consistent with the
  measured 282 s to ±2 m. All constants are `#define`s so a reviewer can retune without
  touching logic.

- Integration in `gps_functions.cpp`: `static struct AltFilter s_alt;` (reset in
  `WZ_GPS_Init` and on fix loss). At `:1022-1024`:
  - `bDisplayTrack` true (TRACK, 1 s cadence, moving node): `altFilterReset(&s_alt)`,
    `node_alt = (int)gpsData.altitude` exactly as today. A 400 s time constant on a
    moving node is a defect (doc §7.4).
  - otherwise: `if (altFilterUpdate(&s_alt, gpsData.altitude)) node_alt = lroundf(s_alt.x)`;
    a rejected sample leaves `node_alt` unchanged. Clamp `< 0 → 0` stays.
  - Convergence edge: `static bool s_altConvergedOnce`; when `altFilterConverged()`
    turns true for the first time after seed → call `WZ_GPS_BaroRelatch(s_alt.x)` (§3.4)
    and print `[GPS ]...alt converged: %d m (P=%.1f)` under `iGPSDEBUG > 0`.
- New accessors in `gps_functions.h`:

  ```c
  void  WZ_GPS_AltSeed(float alt);      /* --setalt: seed filter, P = P0, re-latch QNH */
  bool  WZ_GPS_AltConverged(void);      /* for the QNH latch condition                  */
  ```

- `--setalt` (`src/command_functions.cpp:4123-4138`): after `meshcom_settings.node_alt = iVar`
  add `#ifdef ENABLE_GPS WZ_GPS_AltSeed((float)iVar); #else WZ_GPS_BaroRelatch((float)iVar); #endif`
  — the helper must exist on both branches, see §3.4. Help text at `:777`: append
  `--setalt <m>  set altitude; with GPS: seeds the altitude filter, GPS keeps refining`.
- `nrf52_main.cpp:2829` (legacy `ENABLE_RAK_GPS` path) keeps its own 10:1 IIR; out of
  scope.

### 3.4 GPS-04 — QNH reference altitude: latch on convergence, re-latch on `--setalt`

Three sensor files, three schemes:

- `src/bmx280.cpp:326-327` (`getPressASL`): `if (fBaseAltidude == 0) fBaseAltidude = current_alt;`
  — first call after boot wins, i.e. the first raw fix.
- `src/bme680.cpp:209-210` (`getPressASL680`): same with file-static `fBaseAltidude680`.
- `src/bmp390.cpp`: no latch, uses `bmp.readAltitude(SEALEVELPRESSURE_HPA)` — **out of
  scope**, nothing to re-latch.

Change:

- New helper, always compiled, in `src/gps_functions.cpp` (it is the one place that
  knows both worlds) — or in `src/loop_functions.cpp` if `gps_functions.cpp` is not
  compiled on a board with a pressure sensor but no GPS (implementer checks the
  `build_src_filter`s and reports):

  ```c
  void WZ_GPS_BaroRelatch(float alt);   /* sets every existing base-altitude latch */
  ```

  **As implemented, named `baroBaseRelatch()` / `baroBaseLatchAllowed()` (no `WZ_GPS_`
  prefix) — deliberate, kept through review: both functions are compiled on every board,
  including a board with a pressure sensor and no GPS at all, so a `WZ_GPS_` prefix would
  misdescribe them.** Body as drafted: `#if defined(ENABLE_BMX280) fBaseAltidude = alt;
#endif` `#if defined(ENABLE_BMX680) fBaseAltidude680 = alt; #endif` (`fBaseAltidude680`
  is `extern`-visible via `loop_functions_extern.h`, next to `fBaseAltidude`; the guard
  macro is `ENABLE_BMX680` as at `bme680.cpp:4`).

- Latch condition in both `getPressASL*()`: latch `current_alt` only when
  `fBaseAltidude == 0 && baroBaseLatchAllowed()`, where

  ```c
  bool WZ_GPS_BaroLatchAllowed(void);   /* true when no GPS is active, or the filter converged */
  ```

  **As implemented (F4, §3.6), tightened with a fix check**: `if (bGPSON && gpsDetected &&
!bDisplayTrack && WZ_GPS_HasFix()) return WZ_GPS_AltConverged(); return true;` — the
  draft below (`!ENABLE_GPS || !bGPSON || !gpsDetected` else `WZ_GPS_AltConverged()`)
  would withhold the latch forever on a GPS-equipped node that never gets a fix (a
  sheltered WX station), since `WZ_GPS_AltConverged()` can only become true after a fix
  exists. Until a fix converges, the function returns `fPress` unreferenced (base 0 m),
  exactly the value it returns today before the first fix.

- `--setalt` and the convergence edge call `baroBaseRelatch()` (§3.3). `--setpress`
  (`command_functions.cpp:2296`) **also now routes through `baroBaseRelatch()` (F9,
  §3.6)** rather than writing `fBaseAltidude` directly as originally drafted — the direct
  write left the BME680's `fBaseAltidude680` un-relatched.

### 3.5 What is deliberately not done

- No `setRxBufferSize` (doc §7.1, operator decision 2026-09-01).
- No HDOP weighting (doc §7.6, measured useless), no median (2–5 %).
- No moving-node filtering (GPS-05a), no baro/GPS fusion (GPS-05b).
- No change to `posinfo_*`, smart beaconing, or the beacon interval.

### 3.6 Review outcome

`/fable-review` (wave 2, six finders plus an adversarial verifier) found ten issues on the
wave-1 implementation, all fixed in an uncommitted wave since folded into the branch.
Full writeup: [`review-verdict-gps-20260902.md`](review-verdict-gps-20260902.md). What
changed against §3 above:

- **`altFilterUpdate()` gained a `dt_ms` parameter** (F2). §3.3's `P += Q` is now
  `P += ALT_KF_Q * (dt_ms / ALT_KF_DT_REF_MS)`, `ALT_KF_DT_REF_MS = 3000`, `dt_ms` clamped
  to `ALT_KF_DT_MAX_MS = 60000`. Without it the nRF52's 1 s evaluation cadence injected
  three times the process noise per second of the ESP32's 3 s cadence, pulling the filter's
  time constant inside the 60–120 s error-correlation window the field measurements warn
  about (doc §7.6.C). The caller tracks `s_altLastMs` and passes `ALT_KF_DT_REF_MS` when
  the gap is unknown (seed, TRACK re-entry, `--setalt`).
- **`gpsSamplePlausible()` gained an `alt` parameter** (F5) and rejects `alt < GPS_ALT_MIN_M
(-500)` or `alt > GPS_ALT_MAX_M (10000)` — §3.2 as specified only checked lat/lon/date, so
  a garbage altitude on a spliced-but-plausible sentence could seed the filter (and
  `node_alt`) to a nonsense value at fix time, one call before the filter itself would have
  gated it.
- **New `gpsTimePlausible()` / `gpsDatePlausible()`** guard the clock write (F1). §3.2 only
  gated position commit; both `MyClock.setCurrentTime()` call sites (fix and no-fix branch)
  now additionally require `gps.time.isValid()` and range-check h/m/s — a spliced RMC can
  leave the date term intact while the time term parses as `"1834"` → 00:18:34 or
  `"999999.99"` → h=99, and `mktime()` accepted either unchecked.
- **`isUpdated()` gating** (F6): `gps.altitude.isUpdated()` is read before `.meters()`
  (which clears the flag) so a GGA with fix quality 0 or an empty altitude term no longer
  re-feeds the filter's last value as if it were fresh — that shrank `P` on samples with
  zero information and triggered the convergence edge early.
- **QNH latch rule tightened with `WZ_GPS_HasFix()`** (F4). §3.4's `baroBaseLatchAllowed()`
  (named without the `WZ_GPS_` prefix on purpose — it exists on boards without GPS) withholds
  the latch only while a fix currently exists and the filter has not converged:
  `if (bGPSON && gpsDetected && !bDisplayTrack && WZ_GPS_HasFix()) return
WZ_GPS_AltConverged(); return true;`. As specified in §3.4, a GPS node that never gets a
  fix (sheltered WX station) would have held `fBaseAltidude == 0` forever and reported QFE
  as QNH; pre-fix, the persisted `node_alt` latches as before.
- **`--setalt` rejects out-of-range input instead of clamping to 0** (F7):
  `command_functions.cpp` now prints `alt out of range, ignored` and returns rather than
  seeding the filter and re-latching QNH to 0 m on a typo.
- **`--setpress` routed through `baroBaseRelatch()`** (F9) instead of writing
  `fBaseAltidude` directly, so it also relatches the BME680's `fBaseAltidude680` — the
  direct write in §3.4's read of the existing code left that one behind.
- **`WZ_GPS_RejectCount()` accessor dropped** (F10): the counter (§3.2) stays for the
  `reject:` log line but is not exposed to a caller, since none exists.
- **Naming**: the QNH helpers are `baroBaseRelatch()` / `baroBaseLatchAllowed()`, not
  `WZ_GPS_BaroRelatch()` / `WZ_GPS_BaroLatchAllowed()` as drafted in §3.4 — deliberate,
  reviewed and kept: both functions are compiled on every board, including ones with a
  pressure sensor and no GPS at all.
- Two findings were investigated and refuted, listed in the verdict doc's "Refuted or
  declined" section rather than repeated here (re-seed dead zone under the field trace's
  AR(1) noise; float precision of the recursion).

## 4. File ownership and waves

Worktree: `/Users/martinwerner/WebDev/mc-gps`, branch `feat-gps-nmea-20260902`, pinned
to the `v4.35p_prio` SHA that carries this plan. Agents get absolute paths and run
`pio` with `-d /Users/martinwerner/WebDev/mc-gps`. No agent runs git.

### Wave 0 — orchestrator

1. Create the worktree, confirm `git -C … log -1` equals the pin.
2. If DK5EN-14 is attached (`/dev/cu.usbmodem1101`): start the **baseline** capture
   (§6, arm A) with the firmware already on the node — no build needed for the
   before-run. Otherwise record "bench blocked: node not attached" and continue.

### Wave 1 — four implementers in parallel (Sonnet/high), disjoint files

| Agent | Exclusive files                                                                                                                                                                                   | Task                                                                                                                                                                                                                                                          |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A     | `src/gps_filter.h`, `src/gps_filter.cpp`, `test/test_gps_filter/test_main.cpp`, `test/support/traces/gpsdebug_alt_*.h`, `platformio.ini` (only `[env:native]` `test_filter` + `build_src_filter`) | §3.2 `gpsSamplePlausible`, §3.3 `AltFilter` API exactly as specified; native tests (§5). Altitude fixture extracted from `~/Downloads/gpsdebug.txt` / `gpsdebug1.txt` (`alt:` values of `position` lines, as a `static const float[]`).                       |
| B     | `src/gps_functions.cpp`, `src/gps_functions.h`                                                                                                                                                    | §3.1 feed split + NMEA echo, §3.2 gate call + reject counter/line, §3.3 filter integration + `WZ_GPS_AltSeed` / `WZ_GPS_AltConverged`, §3.4 `WZ_GPS_BaroRelatch` / `WZ_GPS_BaroLatchAllowed`. Codes against the API in this plan; A's file may not exist yet. |
| C     | `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/command_functions.cpp`, `src/bmx280.cpp`, `src/bme680.cpp`, `src/loop_functions_extern.h` (only the `fBaseAltidude680` extern)       | §3.1 call sites, §3.3 `--setalt` + help text, §3.4 latch conditions. Codes against the API in this plan.                                                                                                                                                      |
| D     | `tools/bench/gpsdebug_scan.py`, `tools/mock/test_gpsdebug_scan.py`, `tools/testdata/gpsdebug_sample.txt`                                                                                          | §6 scan tool with unit tests; fixture is the first ~200 lines of `~/Downloads/gpsdebug.txt` including the corrupt sample from doc §3.1.                                                                                                                       |

Agent-side verification: A `pio test -e native -f test_gps_filter`; B and C
`pio run -e heltec_wifi_lora_32_V3` and `-e wiscore_rak4631` (B additionally
`-e t_deck_plus`); D `uvx ruff check --select E9,F,W tools/bench/gpsdebug_scan.py`,
`uvx mypy tools/bench/gpsdebug_scan.py`, `python3 -m unittest tools.mock.test_gpsdebug_scan`.
Integrity check for all: `git -C /Users/martinwerner/WebDev/mc-gps status --porcelain`
lists only the agent's own files.

**Gate (orchestrator):** read the diffs; `pio test` on all native envs (489 cases is
the current count — must not shrink); the seven standard boards clean-built
sequentially (`heltec_wifi_lora_32_V3`, `E22-DevKitC`, `ttgo_tbeam`,
`ttgo_tbeam_supreme`, `t_deck`, `t_deck_plus`, `wiscore_rak4631`) — note the
build-cache race, never two `pio run` in one `.pio`; `tools/ram_snapshot.py` against
`tools/resource_baseline.json`, every board within budget; grep that no `src/` comment
references `docs/` or `test/`. Commit per wave.

### Wave 2 — review

`/fable-review` on `git diff v4.35p_prio...feat-gps-nmea-20260902`. Fix wave if needed.
Commit.

### Wave 3 — bench, docs, PR text (orchestrator + one docs implementer)

- Bench §6 arms B and C (needs DK5EN-14 attached; blocked otherwise, recorded as such).
- Docs implementer (exclusive: `docs/BACKLOG.md` §3.8r rows, `docs/CHANGELOG-stability.md`,
  `docs/bug-GPS-uart-overflow-20260901.md` §8 status lines, this plan's status line):
  results in, wave statuses, bench numbers.
- PR text in German: `docs/pr-gps-draft-20260902.md` via `/submit-pr --dry-run`,
  with this plan's §3 as the "Was/Warum" body and the §6 numbers as evidence.

## 5. Tests (native, `test/test_gps_filter/test_main.cpp`)

**As implemented: 16 cases**, all passing (`pio test -e native -f test_gps_filter`), up
from the 8 sketched below at plan time — the review fix wave (§3.6) added dt-scaling and
time-plausibility coverage and F8 tightened the gate/field-series assertions.

1. `test_plausible_akzeptiert_echten_fix` — accepts the OE5HWN-14 fix
   (48.2479, 14.2577, 280 m, 2026-09-01).
2. `test_plausible_verwirft_nullinsel_und_kalender` — rejects the doc §3.1 splice
   verbatim (`lon == 0.0`, month 14, day 0, year 2015), plus `lat == 0.0`, `lat 91`,
   `lon -181`.
3. `test_plausible_verwirft_hoehen_ausserhalb_des_bereichs` — **new (F5)**: rejects
   `alt < -500` / `alt > 10000`; the range edges themselves and `alt == 0` stay valid.
4. `test_datum_plausibilitaet` — **new (F1)**: `gpsDatePlausible()` boundary cases
   (2023/2100 reject, 2024/2099 accept, month/day out of range).
5. `test_zeit_plausibilitaet` — **new (F1)**: `gpsTimePlausible()` boundary cases,
   including the leap-second value 60 and the doc's `"1834"`/`"999999.99"` splice shapes.
6. `test_steady_state_haelt_rauschen_klein` — 1000 samples of `280 + N(0, 4)`
   (deterministic LCG) → `|x - 280| < 1.0`, `P < 1.5`.
7. `test_kaltstart_seedet_exakt` — first sample seeds exactly (`x == meas`,
   `P == ALT_KF_P0`, `rejects == 0`).
8. `test_doc_sequenz_gate_verwirft_genau_vier_samples` — doc §3.2 sequence from a
   converged 280 m state: **tightened by F8** from a ±3 m bound (which passed even with
   the gate deleted) to asserting the exact per-sample accept/reject decision — the four
   samples >15 m below 280 must reject, state moves <0.5 m (measured 0.23 m).
9. `test_einzelner_ausreisser_wird_verworfen` — a single +25 m outlier is rejected,
   state and covariance unchanged.
10. `test_zehn_ausreisser_seeden_neu` — 10 consecutive +50 m samples re-seed the filter.
11. `test_konvergenz_flag_kippt_innerhalb_100_samples` — `altFilterConverged()` false
    at seed, true within ≤ 100 accepted samples.
12. `test_drei_updates_bei_1s_wie_eines_bei_3s` — **new (F2)**: three 1 s-`dt_ms`
    updates land within 5% of one 3 s-`dt_ms` update covering the same wall time.
13. `test_dt_skaliert_das_prozessrauschen` — **new (F2)**: the process-noise
    contribution scales with `dt_ms / ALT_KF_DT_REF_MS`, not a fixed per-call constant.
14. `test_dt_wird_geklemmt` — **new (F2)**: `dt_ms` beyond `ALT_KF_DT_MAX_MS` does not
    inject more noise than the clamp; `dt_ms == 0` injects none.
15. `test_feldserie_rms_verbessert_sich` — full `gpsdebug.txt` altitude series
    (fixture `GPSDEBUG_ALT`): raw RMS ≥ 3.5 m (measured 4.36 m), whole-series filtered
    RMS ≤ 2.2 m, converged-phase RMS ≤ 1.6 m (measured 2.16 / 1.50 m).
16. `test_feldserie2_rms_verbessert_sich` — **new (F8)**: second field series from
    `gpsdebug1.txt` (`GPSDEBUG1_ALT`, previously an unused fixture): raw RMS ≥ 3.5 m,
    whole-series ≤ 3.4 m, converged-phase ≤ 0.9 m (measured 4.08 / 3.27 / 0.71 m).

Fails-before-fix (original defect): the plausibility function did not exist, and the
raw path committed the doc §3.2 sequence's 257.7 m sample unfiltered. Fails-before-review
(F1/F2/F5/F8): a spliced RMC's damaged time term reached `MyClock.setCurrentTime()`
unchecked, the nRF52 cadence gave a 3x-shorter time constant, a garbage altitude reached
the filter seed, and the doc-sequence/field-series assertions passed with the gate logic
deleted. GPS-01 (the UART drain split) has no native seam (`HardwareSerial`); its proof
is bench arm C, open per the status line above.

## 6. Bench protocol (DK5EN-14, `/dev/cu.usbmodem1101`)

The node reboots on port open (memory: T-Deck Plus). Capture with
`tools/bench/serial_session.py` or `tools/serial_monitor.py` into
`tools/bench/runs/gpsdebug_<arm>_<YYYYMMDD-HHMMSS>.log`. Commands over serial:
`--gps on`, `--gpsdebug on` (= level 2, prints `fix:`, `Time/Date`, `position` every
evaluation), `--track off` (arm A/C) or `--track on` (arm B). GPS indoors on this node
needs several minutes for a 3D fix; the scan tool ignores the pre-fix phase.

| Arm | Firmware                            | Cadence            | Duration | Prediction (doc §4.1, §8)                                                                          |
| --- | ----------------------------------- | ------------------ | -------- | -------------------------------------------------------------------------------------------------- |
| A   | as flashed today (`v4.35p.09.01.2`) | 3 s, `--track off` | 2 h      | ~5 corrupt samples (`lon:0.000000` or impossible `Date:`), altitude RMS ~4 m                       |
| B   | same                                | 1 s, `--track on`  | 2 h      | near-zero corrupt samples. If B ≈ A, doc §4.1 is wrong and the module is the suspect               |
| C   | `feat-gps-nmea-20260902` build      | 3 s, `--track off` | 2 h      | 0 corrupt samples, 0 `reject:` lines with `lon:0.000000` committed, altitude RMS ≤ 2 m after 5 min |

`tools/bench/gpsdebug_scan.py <log> [--json]` reports: evaluations, fixes, samples
with `lon == 0.0` or `lat == 0.0`, dates outside the capture month, `reject:` lines,
altitude min/median/max/RMS-vs-median, first-fix time, and per-30-min buckets. Arm A vs
C is the GPS-01/02 proof; the RMS of A vs C is the GPS-03 proof (§7.6 numbers are the
reference).

## 7. Risks

- **`msg_text` sharing.** The NMEA echo reuses the global command buffer; the feed must
  not write it unless `iGPSDEBUG > 2`. Wave-1 B's report names the line where the guard
  sits.
- **Loop-rate call cost.** `available()` on `HardwareSerial` takes the UART mutex on
  ESP32; one call per loop pass is what the nRF52 already does at 1 s and what
  `Serial.available()` does for the command console every pass. `INSTR_SECTION("gps_feed")`
  makes the cost visible in `[INSTR-LOOP]`.
- **Filter on a node carried without TRACK.** Covered by the re-seed rule (10 rejects
  ≈ 30 s), documented in `--help`.
- **QNH until convergence.** For ~4 min after boot `getPressASL()` returns the station
  pressure (base 0 m), as it does today before the first fix. Web/BLE consumers see a
  QFE value briefly; acceptable and stated in the PR text.
- **Bench blocked.** No bench node is attached at plan time (2026-09-02 evening). Wave 3
  arms A–C run when DK5EN-14 is plugged in; code waves do not wait for it.

## 8. PR cut

The worktree is fork-based. The PR branch is built as
`git checkout -b pr-gps-nmea-<date> upstream/dev && git diff v4.35p_prio...feat-gps-nmea-20260902 -- src platformio.ini | git apply --3way`,
then the `platformio.ini` hunk (native `test_filter`) is dropped, the seven boards are
rebuilt, and `git diff upstream/dev --stat` must list only `src/gps_filter.*`,
`src/gps_functions.*`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`,
`src/command_functions.cpp`, `src/bmx280.cpp`, `src/bme680.cpp`,
`src/loop_functions_extern.h`. Description in German per `/submit-pr`.

## 9. Parked: TM-52 finding for a later ticket

Scout result 2026-09-02, not acted on: the T-Beam Supreme builds its OLED as
`U8G2_SSD1306_128X64_NONAME_1_SW_I2C` (`src/loop_functions.cpp:380-381`, selected by
`BOARD_TBEAM_V3="tbeam_supreme_l76k"`), i.e. bit-banged I2C with a one-page buffer —
the configuration the Heltec V3 comment at `:368-373` records as 579 ms per frame
before it moved to `_F_…HW_I2C`. `setBusClock(400000)` at
`src/esp32/esp32_functions.cpp:214` is Heltec-only. That matches the 570 ms
`display_rx` sections in the OE5HWN-14 log. A fix is a constructor change plus bus
clock on SDA 17 / SCL 18 (`variants/ttgo_tbeam_supreme/configuration.h:89-90`); it
needs a Supreme on a bench or a field tester with `--oledlog on`.
