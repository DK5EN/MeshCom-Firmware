# Campaign GPS-11: position Kalman filter

Resume point for the multi-wave GPS-11 work. Spec: [`gps-11-position-kalman-filter.md`](gps-11-position-kalman-filter.md)
(written 2026-10-17). Branch `fork-dev`; the upstream PR follows only after the bench wave (spec 6.3).

## Operator decisions (2026-10-17)

| Topic          | Decision                                                                                                                                               |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Re-seed        | Time-based: `POS_KF_RESEED_MS 60000` replaces `POS_KF_RESEED_N 10`. A count is cadence-dependent (G5) and re-seeded the altitude filter onto outliers. |
| Grid flipping  | REVISED after the W1 advisor: output hysteresis 0.3 cell in FIXED mode (`src/pos_grid_hold.{h,cpp}`), in this campaign.                                |
| `--gpsfilter`  | Runtime switch only, not persisted, default on after boot. No settings-schema change.                                                                  |
| Bench node     | DK5EN-14 (T-Deck Plus): 2 h window sill, then a drive in TRACK. The operator flashes (auto mode blocks OTA); captures over the 2323 console.           |
| `dt` semantics | Time since the last processed sample (prediction also runs on rejected samples), not since the last accepted one.                                      |
| Manual set     | `gpsPosFilterReset()` exported from `gps_functions`, called by every manual position setter (command, phone/BLE, remote command, web).                 |

## Waves

| Wave | Content                                                                                                         | Owner files                                                                                                                 | State |
| ---- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- | ----- |
| W1a  | DONE: `pos_filter` module + host tests (rework rounds below)                                                    | `src/pos_filter.{h,cpp}`, `test/test_pos_filter/test_main.cpp`                                                              | done  |
| W1b  | DONE (not committed): offline evaluation script for bench captures (G1 cells after `DDMM.MM`, G3 lag, G4 shots) | `tools/gps/pos_kf_eval.py`, `tools/gps/test_pos_kf_eval.py`                                                                 | done  |
| W1 G | Orchestrator: register suite, test-suite map, BACKLOG; stage 1,2; advisor                                       | `platformio.ini`, `docs/test-suite-map.md`, `docs/BACKLOG.md`, this file                                                    | done  |
| W2   | Integration in `gps_functions.cpp`, setter resets, `--gpsfilter`, debug line                                    | `src/gps_functions.{h,cpp}`, setter files from the W2 recon, new `src/pos_pipeline.{h,cpp}` + test, `command_functions.cpp` | done  |
| W2 G | Stage 1,2; t_deck_plus, wiscore_rak4631, ttgo_tbeam builds; image string scan; advisor                          | n/a                                                                                                                         | done  |
| W2b  | RAK4631 (`getGPS`, `ENABLE_RAK_GPS`) and T-Deck Pro (`tdeck_get_gps`) feed the same pipeline                    | `src/nrf52/nrf52_main.cpp`, `src/t-deck-pro/tdeck_pro.cpp`                                                                  | open  |
| W3   | Bench on DK5EN-14, evaluation, constant tuning, bench report in `docs/`                                         | `docs/bench-gps11-*.md`, constants in `src/pos_filter.h`                                                                    | open  |

## W2 recon (2026-10-17, scout)

- Manual position setters: `nodeSetLat()` / `nodeSetLon()` in `src/command_functions.cpp` (~:754/:765) are the shared
  path for `--setlat/--setlon`, BLE frames and RM position writes. Board UIs write directly:
  `src/t-deck-pro/tdeck_pro.cpp` (~:672-691), `src/t-deck-pro/ui_deckpro.cpp` (~:2326-2335),
  `src/t-deck/event_functions.cpp` (~:546/:554, hemisphere chars). The web GUI only reads.
- `--gps on` runs `tg_post_gps_on()` (`gpsInitDone=false` then `init_loop_function()`), which reaches `WZ_GPS_Init()`,
  where `altFilterReset(&s_alt)` sits (:902). A position reset next to it covers on/off/re-init.
- `WZ_GPS_Loop()` reads location/speed/course/hdop at :1037-1051 without any `isUpdated()`; only altitude has it (:1046).
- `save_position()` (ESP32 only) is throttled at the call site (:1260) to once per 15 min.
- Native build: `pos_filter.cpp` registered in `[env:native]` `build_src_filter` next to `gps_filter.cpp` (W1 gate edit).
- **No test pins the `PositionToAPRS()` output text** (`test_pos_tag_nan` only mentions it). Acceptance item
  "wire format unchanged" needs a characterization test, written in W2 and run against the unchanged code first.

## W1b result (2026-10-17)

- `tools/gps/pos_kf_eval.py` + 7 pytest cases, registered in `tools/regression.sh` stage 2 (`tools/gps`). DDMM.MM
  cell conversion spot-checked against `PositionToAPRS()` (bConvPos branch) and `cround4abs()`.
- G4 counts `[POSINFO]...one-shot set - distance` lines; `setSMartBeaconing()` returns early without TRACK, so the
  G4 measurement needs TRACK on during the stationary capture. Course-triggered shots are not counted.
- For W2: extend the debug line with `spd:%.1f crs:%.0f` (GNSS speed m/s, course deg) so G3 does not derive
  speed from raw positions; update the parser in the same change.

## W1 advisor verdict and rework (2026-10-17)

- Advisor (Opus, float32 replay in the session scratchpad): math correct, but REWORK. (1) The spec's CV+ZUPT
  model is a ~17 s low-pass; against multipath correlated over 60-120 s the modal-cell share stays at 0.2-0.4
  (G1 needs 0.95) and no q tuning fixes it. (2) The d2 gate (radius ~11 m at HDOP 1) plus the 60 s hard
  re-seed locks onto 15 m multipath excursions (2 re-seeds per 120 s step at 1 s cadence, 0 at 3 s, so the
  gate is cadence-dependent). (3) Test gaps: reject_ms reset untested, ZUPT test vacuous, memcmp over padding.
- Orchestrator simulation (30 random spots inside one cell, sigma 8-10 m, tau 90 s, 2 h): 30 min average
  share 0.85-0.90 with 2-4 cell changes; plus 0.3-cell hysteresis 0.91-0.98 with 0 changes.
- Operator decision: FIXED mode = HDOP-weighted exponential average with a growing window up to 30 min
  (`POS_KF_FIXED_TAU_S 1800`); TRACK keeps CV+ZUPT; no reset on mode switch; `POS_KF_GATE_MIN_M 50`;
  tri-state return; vel_valid per fix with ZUPT allowed when course is invalid; grid hysteresis in FIXED.
- Rework writers: `pos_filter` rework (Sonnet high, pio slot) and `pos_grid_hold` (Sonnet medium,
  host-compiled tests, no pio). Gate then registers `test_pos_grid_hold`, regenerates
  `test/golden/native/variant-ini-effective.json`, reruns stage 1,2 and the advisor.
- Gate note: `variant-ini-effective` baseline must be regenerated after every `platformio.ini` edit
  (`python3 test/golden/variant_ini_effective.py --generate`).

## W1 advisor pass 2 and rework 2 (2026-10-17)

- Advisor pass 2 (Opus): previous items all resolved; held FIXED output over 30 seeds: modal-cell share mean
  0.92-0.97 (from 10 min), 0-2 cell changes per 2 h; unheld 0.83-0.85 with up to 20 changes. Slow drift of
  30 m over 1 h lags 12.9 m at the ramp end (inherent to the 30 min horizon). Rework: a gap > 30 s re-seeded
  FIXED (11.8 m mean jump); test G single-seed brittle; grid-hold bisection ~12-37 ms on nRF52; W2 contract.
- Rework 2 applied by the orchestrator (second rework round, skill rule): FIXED gaps age W instead of
  re-seeding; new `POS_KF_RESEED` result; mean over 10 seeds (0.263/0.264) plus 1 s vs 3 s subsample test
  (0.117 m rms); grid hold walks <= 4 quanta instead of bisecting; header contracts for W2. Suites 20/20 and
  10/10. Gap test before-run on the old behaviour: FAIL, 13.0 m step.
- W2 contract: see the section "W2 contract (current)" below.

## Operator review: walkers (2026-10-17)

- Question: does a slow walker (3 km/h, dog walk) get stuck as stationary? Simulation on the real filter
  (stand 10 min, then walk; 5 m multipath tau 60 s + 2 m white, HDOP 1.2): with `--track on` the lag stays
  under 10 m (10-20 m if the receiver reports speed 0 at walking pace), 0 re-seeds. With `--track off` the
  30 min average lags 46-99 m and jumps ~110 m every ~2 min (10 re-seeds in 30 min).
- Decision: `--track on` always uses the CV+ZUPT model. `--track off` gets automatic motion detection in the
  filter (speed evidence >= 0.5 m/s, or smoothed displacement > ~30-40 m for 15-30 s); moving uses CV+ZUPT,
  return to the average after ~120 s within ~15 m. Grid hold only while still.
- Advisor pass 3: rework 2 correct (hold walk identical to bisection on 2.0M pairs), one break: a FIXED gap
  counted fully toward the reject timer, so one outlier after a gap >= 60 s re-seeded onto it. Fixed in
  rework 3 (cap per sample at `POS_KF_DT_MAX_MS`).
- Gate 3 (before rework 3): stage 1,2 green, native 672/672, pytest 1087.

## Rework 3 and 4: motion detection (2026-10-17)

- Rework 3 (Sonnet high): FIXED request gets a still/moving sub-state; reject timer capped per sample.
- Advisor pass 4: REWORK. A false trigger got stuck in moving (CV output wanders > 15 m per 120 s under
  correlated multipath: episodes up to 69 min; one speed sample >= 0.5 m/s restarted the 120 s quiet time:
  moving 66-100 % of the time with speed noise |N(0,0.3-0.4)|). Held output at sigma 15 m: modal share
  0.52-0.56 with 66-94 cell changes per 2 h (rework 2: 0.76, 1.5). The earlier "86-107 false triggers per
  day" was a sum over 5 runs; per day 17-21.
- Rework 4 (orchestrator): adaptive move radius clamp(3.5 x innovation rms, 40, 60) m; still radius 30 m;
  leaky speed evidence in the still check; operator decision `POS_KF_MOVE_SPEED_MS 10000`, per-sample cap
  2000 ms. Found while testing: with 10 s evidence a car is 200 m away at detection and the CV gate rejected
  it (14 re-seeds per 15 min); the catch-up gate now uses the raw innovation when larger (0 re-seeds,
  catch-up step 30 m, then 3 m).
- Measured (suite, 38/38): walker with speed 9-12 s detect, static-hold walker 70-77 s, dog walk no return
  to still, stop -> still 117-121 s. False triggers per 24 h: sigma 10 m 0 (speed sd 0.2), ~1 (sd 0.3 at
  3 s, max 3); sigma 15 m 0.3-1 per day; moving share <= 0.6 %, longest episode <= 387 s. Before-run on
  rework 3: share 2.6 %, episode 2276 s.
- Open for W3: the stationary speed noise of the receivers is unknown; no existing capture logs speed. The
  W2 debug line logs `spd`/`crs`; retune `POS_KF_MOVE_SPEED_MS` from the bench.

## W2 contract (current)

- Call `posFilterUpdate()` once per new fix (`location.isUpdated()`, read before the accessors); advance
  the dt timestamp on every non-SKIP result.
- Mode argument = `--track` (`bDisplayTrack ? POS_KF_MODE_TRACK : POS_KF_MODE_FIXED`); the filter decides
  still/moving itself for FIXED.
- Grid hold only while `posFilterIsStill()`; reset it on every `n_moving` change, on `POS_KF_RESEED`, on
  `--gpsfilter off` and on a manual position set. N/S and E/W from the held output.
- Debug line: raw, kf, held, hdop, sig, rej, `spd`, `crs`, still/moving, `n_moving`.

## Advisor pass 5 and final W1 state (2026-10-17)

- Advisor pass 5: rework 4 items correct; two new defects, both fixed by the orchestrator with the advisor's
  measured variant: (1) the raw-innovation catch-up gate let an outlier on the trigger sample of a
  speed-noise burst pull a standing node's output 88 m -> bounded by speed x (evidence time + 5 s), now
  9-10 m (test T7, before-run 88.8 m); (2) the adaptive radius sat at its 40 m floor for 30-60 min after a
  seed (6-9 false triggers per 20 boots at sigma 15 m) -> bias-corrected EMA, now 0 (test T8, before-run 7).
  No sixth advisor pass: the fix is the advisor's own measured variant, ported verbatim.
- Cost of the 10 s speed rule (operator decision): at 3 s cadence with speed noise sd 0.3 about 0.7 false
  triggers per day; at sigma 10 m that raises grid-cell changes from ~3 to ~8.5 per day.
- Final suite: test_pos_filter 30 cases, test_pos_grid_hold 10 cases, all green.

## W2 result (2026-10-17)

- `src/pos_pipeline.{h,cpp}` (new, host-built): filter + grid hold + dt timestamp + runtime switch; `posPipelineStore()`
  derives node_lat/lon and N/S/E/W exactly as the old inline code; `posPipelineManualSet()` resets only on a change.
- `src/gps_functions.cpp` WZ_GPS_Loop: `location.isUpdated()` read before lat()/lng(), one `posPipelineFix()` per new
  fix, node_* from the (held) output, `setSMartBeaconing()` gets the stored (filtered) magnitudes. Reset in
  `WZ_GPS_Init`. Debug line at `--gpsdebug on`: `[GPS ]pos raw: kf: held: hdop: sig: rej: spd: crs: st: nm:`;
  `pos_kf_eval.py` parses it (old form still accepted).
- Manual sets (`nodeSetLat/Lon`, T-Deck and T-Deck Pro Setup save) call `gpsPosFilterManualSet()` with the old position.
- `--gpsfilter on|off`, runtime only; prints "no position filter on this board" where `ENABLE_GPS` is off.
- `test_pos_pipeline` (13 cases): characterization of the stored values and the APRS `DDMM.MM` text against a verbatim
  copy of the old code plus literal strings, disabled pass-through bit-identical, dt/SKIP/REJECT/rollover, hold.
- Advisor: REWORK, one item: every Setup save reset the filter even with the position unchanged (a callsign change
  threw away the 30 min average). Fixed by the orchestrator with compare-before-reset (test
  `test_manual_set_resets_only_on_change`, before-run failed at the unchanged-save assert). Notes taken: velValid is
  `isValid()` (sticky over a cycle, documented); the hold-reset branches overlap and no test separates them
  (documented in pos_pipeline.cpp).
- Gap found: RAK4631 and T-Deck Pro do not run WZ_GPS_Loop, so they get no filter yet -> W2b.
- Open (W3): if the phone app pushes its position (BLE 0x70/0x80) periodically while the node has GPS, every changed
  push resets the filter. Check on the bench.

## W3 bench 1: DK5EN-1 stationary (2026-10-17)

Report: `docs/bench-gps11-dk5en1-20261017.md`. Bench node DK5EN-1 (Heltec V3 + u-blox) instead of DK5EN-14 (operator).
2 h FIXED + 2 h TRACK, both stationary. Speed noise is small (FIXED max 0.44 m/s; TRACK 1 Hz max 0.87, 0.26 % >= 0.5).
One false still -> moving from a slow 36 m multipath drift (displacement path) cost the held output G1: 51 % in the
modal cell, 11 changes (raw 119); without it 2 changes. Offline replay (scratch tool, reproduces the device exactly):
radius floor 50 m removes the trigger, keeps 30/30 host tests, static-hold walker 83-90 s (was 70-77). Speed rule 5 s
fails T5 at sd 0.3 and gains 6 s only. Decision pending with the operator.

Decision (operator, 2026-10-17): keep the 40 m floor ("one jump is acceptable"). Tests added: T9 (real-capture replay,
`bench_dk5en1_20261017.h`) and T5b (hourly 25-45 m multipath bursts), pinning the accepted rate; the real event was a
~30 s step to ~40 m, not a slow drift. Projected at this site: ~1-2 false triggers per day.
