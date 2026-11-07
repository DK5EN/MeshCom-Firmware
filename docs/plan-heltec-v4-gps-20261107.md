# Plan: Heltec V4 GNSS fix (GPSV4-01)

Run with `/orchestrate-waves`. Start **after GPIO-01 W5 and its gate are committed** (operator decision
2026-11-07). Field report: groups.io meshcom #1557 "Heltec V4 GPS Module" (IS0HHA, GM4MCV, OE1KFR).

## BLUF

GNSS on the Heltec V4 has never worked, in `fork-dev` or `upstream/dev` (the variant file is identical in
both). There are three defects in `variants/heltec_wifi_lora_32_V4/configuration.h` and
`src/gps_functions.cpp`. W1 fixes them on `fork-dev`; W2 ports the fix to an upstream PR against `dev`,
marked as untested on hardware.

| #   | Defect                                                                                                             | Fix                                                                  |
| --- | ------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------- |
| D1  | UART RX and TX are swapped: `GPS_RX_PIN 38` / `GPS_TX_PIN 39`; the board has 38 = ESP to GNSS and 39 = GNSS to ESP | `GPS_RX_PIN 39`, `GPS_TX_PIN 38`                                     |
| D2  | The GNSS kit is never powered or woken: enable GPIO34 (active LOW), wake GPIO40 and reset GPIO42 are never driven  | V4 branch in `WZ_GPS_Reset()` / `WZ_GPS_Deactivate()`                |
| D3  | Sensor I2C (`Wire`) runs on GPIO41/42, which are the GNSS PPS and RESET lines                                      | Move the sensor bus to `I2C_SDA 4` / `I2C_SCL 3` (Meshtastic layout) |

Reference pin map: Meshtastic `variants/esp32s3/heltec_v4/variant.h` (develop), which works on the
reporter's board: `GPS_TX_PIN 38` (towards GNSS), `GPS_RX_PIN 39` (towards CPU), `PIN_GPS_EN 34` with
`GPS_EN_ACTIVE LOW`, `PIN_GPS_RESET 42` (LOW for more than 100 ms resets the L76K), `PIN_GPS_STANDBY 40`
(HIGH = force wake), `PIN_GPS_PPS 41`, module L76K (CM121 kit, 9600 baud factory default),
`I2C_SDA1 4` / `I2C_SCL1 3`.

## Operator decisions (2026-11-07)

- **I2C moves to 4/3.** This is a breaking change for anyone with a BME/AHT/SHT/RTC on header pins 41/42.
  The release notes and the PR body must say so.
- **R2 only, R8 deferred.** The fix targets the ESP32-S3R2 board (the reporters' version). R8 (octal
  PSRAM: GPIO34 is a PSRAM line, GNSS enable reportedly on 42) gets a runtime guard and a BACKLOG row,
  merged with `LED-V4R8` / LED-17.
- **Upstream PR right after the fork-dev gate**, with "auf Hardware ungetestet" in the German body. No V4
  is in the bench fleet.
- **Sequencing:** after GPIO-01 W5.

## Flow

W0 (read-only recon), then W1 (one writer on `fork-dev`), W1 gate + advisor + commit, then W2 (`/submit-pr`),
then W3 (field follow-up). The waves run one after another, with one writer at a time. Campaign doc:
`docs/campaign-gpsv4-01-<date>.md`, updated per wave together with `docs/RESUME.md`.

## W0 recon (scout, read-only)

Answer each item with `file:line`; W1 does not start until every item has an answer.

1. **R8 detection:** find the eFuse field in the installed arduino-esp32 / IDF that tells S3R2 from S3R8
   without PSRAM enabled in the build (candidate: `ESP_EFUSE_PSRAM_CAP` in `esp_efuse_table.h` for
   ESP32-S3; record the exact value meanings). The env uses `board = heltec_wifi_lora_32_V3`, so PSRAM is
   not initialised.
2. **GPIO 3 and 4 on the V4 env:** confirm that no V4 macro, `pin_board.cpp` list or GPIO-01 table
   (`docs/gpio-pin-map.md`, `src/pin_policy.h`) claims them after W5. GPIO3 is an S3 strapping pin (JTAG
   source, only relevant if the eFuse is burned); note the class GPIO-01 gives it.
3. **I2C callers:** list every `Wire.begin(I2C_SDA, I2C_SCL)` and every `MC_I2C_NEEDS_BUS_RESET` path that
   the V4 build reaches. Confirm that all of them go through the macros (no hard-coded 41/42).
4. **GPS power path:** confirm that `--gps on` reaches `WZ_GPS_Reset()` (`gps_functions.cpp` ~842) and
   `--gps off` reaches `WZ_GPS_Deactivate()` (`command_functions.cpp` ~2523). List the boot and deep-sleep
   paths (`esp32_sleep.cpp`, `onebutton_functions.cpp:310`) that should power the GNSS down.
5. **L76K path:** confirm that the detection, then `SetupL76K()`, then the 38400 switch works on 9600 factory modules
   with the swapped pins (no V4-specific baud handling needed).
6. **Golden baselines:** the generator command for `test/golden/native/variant-macros*.txt` and
   `variant-ini-effective.json`, and which lines change.
7. **Upstream delta:** diff `upstream/dev:src/gps_functions.cpp` against `fork-dev` around
   `WZ_GPS_Reset()` / `WZ_GPS_Deactivate()` (upstream ~860-912). Confirm that `src/pin_board.cpp` is
   fork-only (it is not in `upstream/dev`), so the W2 port is variant + `gps_functions.cpp` only.
8. **Deep sleep / Vext:** check whether GPIO34/40 need RTC hold or explicit levels before deep sleep, so the
   kit does not draw current while the node sleeps.

## W1 implement on fork-dev (one implementer)

**Exclusive files:** `variants/heltec_wifi_lora_32_V4/configuration.h`, `src/gps_functions.cpp`,
`src/pin_board.cpp`, `test/golden/native/variant-macros.txt`, `test/golden/native/variant-macros-effective.txt`,
`test/golden/native/variant-ini-effective.json` (regenerate only, never hand-edit), plus a sleep TU only if
W0 item 8 requires it.

**Orchestrator-owned docs:** `docs/architecture/08-defect-catalogue.md` (new row GPSV4-01, D1-D3),
`docs/BACKLOG.md` (V4R8 row: GNSS enable plus LED, merged with `LED-V4R8`), `docs/gpio-pin-map.md` (V4
row: 3/4 = I2C, 34/40/41/42 = GNSS), the campaign doc, `docs/RESUME.md`.

Changes:

1. **Variant** (`configuration.h`):
   - `GPS_RX_PIN 39`, `GPS_TX_PIN 38`, with a comment naming the direction (RX = towards the ESP).
   - `GPS_EN_PIN 34` and `GPS_EN_ACTIVE LOW`; new `GPS_WAKE_PIN 40`, `GPS_RST_PIN 42` and `GPS_PPS_PIN 41`.
     Use whatever names W0 finds least collision-prone with T-Beam 1W (`GPS_EN_PIN`) and T114/T-Echo
     (`RST_GPS`), so their branches stay untouched.
   - `I2C_SDA 4`, `I2C_SCL 3`.
   - Keep the R8 note next to the existing LED-17 note.
2. **`WZ_GPS_Reset()`**, new `BOARD_HELTEC_V4` branch:
   - Unless the R8 guard trips: drive enable to active LOW, set wake HIGH, wait for power-up (Meshtastic uses
     1000 ms peripheral warm-up; W0 decides the value), then hold reset LOW for 150 ms and release it HIGH.
   - **R8 guard:** if the eFuse reports octal PSRAM, touch neither 34 nor 42 and print once:
     `[GPS ]...Heltec V4 R8 erkannt: GNSS-Steuerung nicht unterstuetzt`.
3. **`WZ_GPS_Deactivate()`:** drive enable to inactive (HIGH) and wake LOW, under the same R8 guard. Add the
   deep-sleep handling only if W0 item 8 calls for it.
4. **`pin_board.cpp`:** add the new wake and reset macros to `kSoft`, next to `GPS_EN_PIN` and `GPS_PPS_PIN`.
5. **Regression test (fails before, passes after):** a `static_assert` block under `BOARD_HELTEC_V4` in
   `pin_board.cpp`, in the style of the GPIO-01 W2 checks: RX 39, TX 38, enable 34, reset 42, and
   `I2C_SDA`/`I2C_SCL` not in {41, 42}. Show it failing on the pre-fix variant by building the V4 env.
   Add the regenerated golden macro baselines as the host-side lock.

**Scoped checks (implementer):**

- Build the V4 env with `MC_AU_DEV_CHANNEL=1`, through the pio lock.
- Report RAM/flash against the pre-W1 commit.

**Orchestrator gate:**

- Read the diff.
- `tools/regression.sh --stage 1,2`.
- Builds: Heltec V4, Heltec V3 (shared I2C/OLED code), T-Beam (GPS shared code), T-Beam 1W
  (`WZ_GPS_Reset` branch), T114 and RAK4631 (nRF52 `gps_functions.cpp`), Wireless Tracker.
- Compare RAM/flash regions, not hashes.

**Advisor:** this is a behaviour change, so an Opus re-review before the commit. Points to check:

- The pin directions against the Meshtastic map.
- The R8 guard: an R8 board must never get a write on GPIO34.
- GPIO 3/4 against GPIO-01 policy.
- The reset timing.
- No other board's branch changed.

**Commit** on `fork-dev`: `fix(gps): GPSV4-01 Heltec V4 GNSS pins, power and I2C off the GNSS lines`.
Include the harness summaries the docs cite.

## W2 upstream PR (`/submit-pr`)

1. Sync upstream, then cut `pr/heltec-v4-gps` from `upstream/dev` (not `fork-dev`).
2. Port the minimum:
   - Port the variant hunk from W1.
   - Port the `WZ_GPS_Reset()` / `WZ_GPS_Deactivate()` V4 branches and the R8 guard, adapted to upstream's
     `gps_functions.cpp` context (W0 item 7).
   - Leave out `pin_board.cpp`, the golden files, fork docs and any refactor.
3. Gate on the PR branch: build Heltec V4, Heltec V3, T-Beam and T-Beam 1W. Do not use bare `pio test`.
4. German PR body, which must contain:
   - Symptom and thread link.
   - The three defects with `file:line`, plus what changed and why.
   - Reference to the Meshtastic V4 pin map.
   - **Breaking change:** I2C sensors move from GPIO41/42 to GPIO4/3.
   - R8 not supported (guard, not fixed).
   - **"Auf Hardware ungetestet"**, with a request for V4 owners to test.
5. Show the operator the body and diff summary, then `gh pr create -R icssw-org/MeshCom-Firmware --base dev`.
   Record the PR number in the campaign doc and BACKLOG.

## W3 field follow-up (operator)

- **Reply draft for groups.io #1557 (English):** confirms the firmware bug, names the fix and the I2C move,
  links the PR, asks IS0HHA / GM4MCV to test. The operator posts it; Claude does not send anything.
- **Optional, on request:** a `ci-proof-*` draft build for the testers. The next `v*` release follows the
  normal `/release-firmware` flow, with the I2C move in the release notes.
- **When a tester reports back:** update the PR, the campaign doc and BACKLOG. If D2 timing or the R8 guard
  is wrong, a follow-up wave fixes it on `fork-dev` first, then on the PR branch.

## Out of scope

- R8 env (`[env:heltec_wifi_lora_32_V4R8]`, LED 46, GNSS enable 42): BACKLOG, needs hardware or a schematic.
- V4 433 MHz without PA (`HELTEC_V4_HAS_PA` handling), V4.2 vs V4.3 FEM differences (GC1109 / KCT8103L).
- Any GPS rework beyond the V4 branch.

## Risks

- **No hardware validation.** D1 is certain from the reference map. D2's timing and active levels come
  from Meshtastic, not from a Heltec schematic.
- **I2C move:** users with sensors on 41/42 lose them until they rewire. The release notes are the only
  mitigation.
- **R8 detection:** if the eFuse field cannot tell R2 from R8 reliably, W0 escalates to the operator before
  W1 starts. The fallback is R2 only with no guard, documented.
