# Campaign GPSV4-01: Heltec V4 GNSS

Plan: [plan-heltec-v4-gps-20261107.md](plan-heltec-v4-gps-20261107.md). Started 2026-11-07 on `fork-dev`.

## Current state

- W0 done (2 scouts). W1 implemented: pins swapped back, I2C on 4/3, V4 power branch in `WZ_GPS_Reset()` /
  `WZ_GPS_Deactivate()` (`HV4_GPS_*` macros, V4-scoped because `GPS_EN_PIN`/`GPS_PPS_PIN` are used outside the T-Beam
  1W branch), static_assert regression in `pin_board.cpp` (red before, green after), V4 build RAM 41.9 %, flash 51.6 %.
- W1 advisor round 1: REWORK, applied by the orchestrator:
  - Deep sleep and runtime off now release TX 38 and reset 42 (no back-feed into the unpowered kit). `gpio_deep_sleep_hold_en()` latches every
    S3 digital pad, which also keeps V4 Vext off in deep sleep.
  - `--gps reset` with GPS off no longer powers the kit.
  - Boot with GPS off drives the enable inactive.
- No hardware test possible (no V4 on the bench); the upstream PR says so.
- Next: W1 gate, advisor, commit, then W2 `/submit-pr`.

## W0 answers

1. **R8 detection: not possible.** The installed IDF 4.4.7 eFuse table for the S3 has no PSRAM_CAP field. The
   `PKG_VERSION` decoder (`efuse_ll.h:85-88`) is a stub. **Decision 2026-11-07: the plan's fallback, R2 only, no
   runtime guard**, with a comment in the variant. On an R8 board GPIO34 is an octal-PSRAM line, but this env never
   initialises PSRAM.
2. **GPIO 3/4:** no V4 macro uses them. In `pin_policy.h:76-80`, GPIO3 is classed as strapping and GPIO4 as
   unreviewed. `pinBoardFor()` has no V4 entry, so V4 gets no pin output control (out of scope).
3. **I2C:** every V4 path uses the `I2C_SDA`/`I2C_SCL` macros (`esp32_main.cpp:1373`). The OLED is on Wire1 (17/18) and
   is unaffected. `MC_I2C_NEEDS_BUS_RESET` is off on V4.
4. **Power path:** the V4 build drives no GPS enable or reset line. `WZ_GPS_Reset()` runs at boot and on
   `--gps reset`, `--gps off` calls `WZ_GPS_Deactivate()`. No new V4 macro name collides with an existing one.
5. **L76K:** the scan tries 38400 first, then 9600, and `SetupL76K()` then switches to 38400. No V4 baud code is needed.
6. **Goldens:** `variant-macros.txt` is edited by hand and checked by `variant_macros_lint.py`.
   `variant-macros-effective.txt` was already stale before this campaign (900+ lines, unrelated) and no gate checks it,
   so the campaign leaves it alone (BACKLOG).
7. **Upstream:** the V4 variant is identical in both trees, and `WZ_GPS_*` differs only by a move. `pin_board.cpp` is
   fork-only.
8. **Deep sleep:** GPIO34/40/42 are not RTC GPIOs on the S3, so holding a level needs `gpio_hold_en` plus
   `gpio_deep_sleep_hold_en`. W1 holds the GNSS enable line inactive. Separate finding: V4 Vext (GPIO36) is not held
   OFF in deep sleep (BACKLOG).
