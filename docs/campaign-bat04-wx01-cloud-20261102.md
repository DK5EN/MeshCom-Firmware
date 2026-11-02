# Cloud campaign BAT-04 + WX-01 (2026-11-02)

Resume point for one cloud session that implements and build-tests two campaigns with `/orchestrate-waves`:

- **BAT-04** battery charge state and current, upstream #1073: [campaign-bat04-charge-state-20261102.md](campaign-bat04-charge-state-20261102.md)
- **WX-01** UV, wind and gust telemetry, ESP32 only, upstream #999: [campaign-wx01-uv-wind-20261102.md](campaign-wx01-uv-wind-20261102.md)

The two campaign docs hold design, acceptance criteria and their own wave logs. This doc holds the joint
schedule, the cloud setup and the rules a cloud session cannot read from the operator's machine.

**State:** C0-C3 committed; next C4 (WX-W3), then C5 (BAT-04 BLE register `IS2`, operator decision 2026-11-02).

## Operator decisions (2026-11-02)

- BAT-04 Q1-Q4: all recommendations accepted (surfaces `--info`, web status, BLE `I`; BQ boards included if W0 finds
  a clean hook; no OLED/TFT marker; signed mA, absent when not measurable).
- WX-01: `gust` added; no averaging of existing values; one upstream PR. ESP32 only, no RAK.
- **Commits go straight to `fork-dev`**, one commit per wave after the gate, pushed after each commit.
- **No bench in the cloud.** BAT-04 W3 (T-Beam charge transitions) and the WX-01 "sensor absent" check on DK5EN-1
  run later on the operator's machine. WX-01 is marked **field test necessary** (no sensor hardware at all).
- Not in the cloud session: upstream PRs, comments on #1073 / #999, releases, tags. They stay open in the wave
  logs.

## Cloud setup (once, before C0)

1. Toolchain: `pip install -U platformio` (or `uv tool install platformio`), `uv`, `node`. Linux x86_64 runs the
   pinned nRF52 gcc-arm natively (the Rosetta note in `CLAUDE.md` is macOS only). First builds download the ESP32
   and nRF52 platforms (several GB).
2. `export MC_AU_DEV_CHANNEL=1` for every board build (fork convention since `6c84a46c`).
3. Agents and skill are tracked for this campaign: `.claude/agents/scout.md`, `.claude/agents/implementer.md`,
   `.claude/skills/orchestrate-waves/SKILL.md`. Everything else under `.claude/` stays private (gitignored).
4. Every `pio` call goes through `tools/piolock.sh` (mkdir lock, one `pio` at a time):
   `tools/piolock.sh pio test -e native_batt_charge`. Never bare `pio test` (it walks the board envs).
5. Smoke check before C0: `tools/regression.sh --stage 1,2` green on the untouched tree, and one board build
   (`heltec_wifi_lora_32_V3`). Record both baselines (pass counts, flash/RAM regions) in the wave log below.

## Rules from the operator's global instructions (not visible in the cloud otherwise)

- English in code, commits and docs; no emojis. Final reports BLUF: outcome first, then what stands, deviations.
- Not verified until lint, format-check and the real test suite have run (`tools/regression.sh --stage 1,2`).
  Unity passed must equal collected; a harness error voids the verdict.
- Never accept a subagent's self-report: read the diff, re-run verification, spot-check its sharpest claim.
- Stage explicit paths, never `git add -A` / `git add .`. Writers run no git at all.
- Never merge, push to main/master, tag or set tracking. Pushing `fork-dev` after each wave commit is allowed
  (operator decision above).
- Any `.md` gets `npx --yes prettier@3 --write <file>` before commit (prettier turns `*x*` into `_x_`).
- Every Agent call names model and effort: `scout` = Haiku 5.5 low; `implementer` = Sonnet 5.5 medium (first
  dispatch, code); advisor = `general-purpose`, `model: "opus"`, `effort: "medium"`, fresh agent; rework = fresh
  `implementer` at high. Deviation from the BAT-04 doc: its agents B and C were planned at Sonnet high; the skill
  rule (medium on first dispatch) wins.
- Logs to `tools/bench/runs/harness/` or the scratchpad, never the repo root. `git status` clean after each wave.

## Joint schedule

BAT-04 and WX-01 both touch `src/command_functions.cpp` and `src/web_functions/web_functions.cpp`, so those
writers run in different waves. Orchestrator-owned in every wave: `platformio.ini`, `src/loop_functions_extern.h`,
`src/batt_functions.{h,cpp}`, `test/golden/`, all docs.

| Wave | Agents                                                            | Exclusive files                                                                                                                                                                                                                                                                                                                           | Own build / test slot                                                                                                                                                    | Estimate                   |
| ---- | ----------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------- |
| C0   | orchestrator; 2 scouts                                            | BAT-04 W0 contract (`src/batt_charge.h` stubs, battery global); `[env:native_batt_charge]`, `[env:native_ltr390_calc]`, `[env:native_wx_avg]`, `ENABLE_LTR390` / `ENABLE_WIND` in ESP32 sections; scouts: BAT-04 W0 item 3 and WX-01 W0 (read only)                                                                                       | -                                                                                                                                                                        | 20 min, ~$2                |
| C1   | BAT-A, BAT-B, BAT-C, WX-W1a, WX-W1b (5 implementers, one message) | BAT-A `src/batt_charge.h` bodies, `test/test_batt_charge/`; BAT-B `src/esp32/loop_actions_esp32.cpp`, `test/test_batt_charge_pmu_pins/`; BAT-C the two BQ files named by the C0 scout; WX-W1a `src/ltr390.{h,cpp}`, `src/ltr390_calc.h`, `test/test_ltr390_calc/`; WX-W1b `src/wind_counter.{h,cpp}`, `src/wx_avg.h`, `test/test_wx_avg/` | A `native_batt_charge`; B `ttgo_tbeam`; C `t_deck_pro`, `t5_epaper`; W1a `native_ltr390_calc` + `heltec_wifi_lora_32_V2`; W1b `native_wx_avg` + `heltec_wifi_lora_32_V3` | 30 min + 40 min gate, ~$25 |
| C2   | BAT-D, BAT-E (2 implementers)                                     | BAT-D `src/command_functions.cpp` (`--info` printf and `idoc` block only), its test; BAT-E `src/web_functions/web_functions.cpp` (Battery row only)                                                                                                                                                                                       | D `native_command_setters`; E compile-only `ttgo_tbeam_supreme`                                                                                                          | 25 + 40 min, ~$10          |
| C3   | WX-W2 (1 implementer)                                             | `src/meshcom_settings.h`, `src/command_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/loop_functions.cpp` (globals only), `test/test_command_toggles/`, `test/test_command_setters/`, settings-member suites                                                                                                                            | own envs                                                                                                                                                                 | 25 + 40 min, ~$5           |
| C4   | WX-W3 (1 implementer)                                             | `src/loop_functions.cpp` (`sendTelemetry()` only), `src/web_functions/web_functions.cpp`, `src/web_functions/web_setup.cpp`, `src/rm_exec_read.cpp`                                                                                                                                                                                       | own envs                                                                                                                                                                 | 20 + 40 min, ~$4           |
| C5   | BAT-04 follow-up (1 implementer or orchestrator)                  | `src/command_functions.cpp` (`bInfo && ble` block only), its host test, phone-harness pins                                                                                                                                                                                                                                                | own envs                                                                                                                                                                 | added 2026-11-02           |

Advisor (Opus, medium) after C1+C2 on the BAT-04 diff, after C1+C3 on the WX-01 diff so far, and after C4; about
$4 each. Total about $60 to $80, 5 to 6 h wall-clock.

### Commits

- After C1: WX-01 W1 (new modules and host tests, not yet linked into behaviour) as its own commit. The BAT-04 C1
  files are committed together with C2 once the BAT-04 advisor approves (BAT-04 doc W2: one firmware commit).
- After C2: BAT-04 firmware commit. After C3 and C4: one WX-01 commit each.
- Each commit: `git status` and `git log -1` first, explicit paths, campaign docs and this wave log updated in the
  same commit, then `git push origin fork-dev`.

### Gate (after each wave, orchestrator only)

1. `ls` every file the wave should have produced.
2. Hotspot edits (`platformio.ini`, globals), then `tools/regression.sh --stage 1,2`.
3. Sequential clean builds with `MC_AU_DEV_CHANNEL=1`: `ttgo_tbeam`, `ttgo_tbeam_supreme`,
   `heltec_wifi_lora_32_V2`, `heltec_wifi_lora_32_V3`, `t_deck_pro`, `t5_epaper`, `wiscore_rak4631`. Flash/RAM
   regions against the C0 baseline in the wave log; the RAK image must not grow beyond the shared BAT-04 code.
4. Artifact proof per campaign doc (strings scan: `LTR390` and `[WIND]` in the V2/V3 images, absent in the RAK
   image; the BAT-04 charge-state strings in the T-Beam image).
5. Advisor pass where the schedule names one; verified rework to a fresh high implementer.

## Open after the cloud session (operator's machine)

- BAT-04 W3 bench (AXP2101 T-Beam, AXP192 if in the fleet, BQ board, Heltec V3 byte-identical check).
- WX-01 bench step 6 (DK5EN-1, `--uv on` / `--wind on` without hardware).
- Upstream PRs via `/submit-pr` (German bodies), comments on #1073 and #999 including the WX-01 field-test
  debugging section.

## Wave log

| Wave  | Commit                                                     | Gate                                                                                                                                                                                                                                          | Advisor                                                                                                                                                                                                                       | Notes                                                                                                                                                                                                                                                                                                                                                                |
| ----- | ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| setup | -                                                          | stage 1,2 PASS: 89 Unity envs, 3336/3336 cases, pytest 1478, golden selftest 48; all 7 board builds green                                                                                                                                     | -                                                                                                                                                                                                                             | Ran on the operator's Mac (PlatformIO 6.1.18 already installed, Rosetta present), not a cloud VM. Baseline at `8681fcb4`, see table below                                                                                                                                                                                                                            |
| C0    | - (committed with C1)                                      | contract compiles (`-fsyntax-only`); no gate on its own                                                                                                                                                                                       | -                                                                                                                                                                                                                             | `src/batt_charge.h` stubs, `global_batt_charge` in both battery files (`batt_function_old.cpp` too: T-Beam, Heltec, RAK and the BQ boards build the old file), three native envs, `ENABLE_LTR390`/`ENABLE_WIND` in `[esp32]` build_flags (`t_deck_pro` and `t5_epaper` do not inherit `[esp32]` flags, so they stay without WX-01). Scout findings in the BAT-04 doc |
| C1+C2 | BAT-04 firmware commit and WX-01 W1 commit (see `git log`) | stage 1,2 PASS: 92 Unity envs, 3383/3383, pytest 1478, golden regenerated (3 new native envs, `ENABLE_*` flags); 7 clean builds green                                                                                                         | BAT-04: REWORK (AXP192 VBUS ADC enable dropped, 4095.5 mA I2C-failure bound, exact guard pin), then APPROVED. WX-01 W1: REWORK (PCNT cannot debounce a reed contact, LTR390 rate/resolution, `ltr390End`/wfac), then APPROVED | BAT rework applied by the orchestrator (few lines, fail-before/pass-after pins); WX rework by a fresh Sonnet-high implementer. Deltas in the table below                                                                                                                                                                                                             |
| C3    | WX-01 W2 commit (see `git log`)                            | stage 1 Unity 93 envs 3395/3395, golden selftest clean after the fixes below, pytest 1478; 7 clean gate builds green; R2 sweep 24/24 further ESP32 envs green (`esp32-external-radio` excluded: pre-existing `#error`, needs a build overlay) | REWORK (empty `LORA_DIO0` macro broke T-Beam Supreme, radio/SPI pins missing from `--windpin`, `SE` JSON over the BLE budget), then APPROVED with the R2-sweep condition                                                      | Rework by a fresh Sonnet-high implementer. Orchestrator fixes for the golden lints: one rung per `--windpin`/`--windcal` with an `#if` body, rungs moved out of the `#ifdef OneWire_GPIO` block, "not supported" for `--uv`/`--wind` on/off via `commandMatches()` (the toggle lint ignores `#if` guards), `--help` lines, toggle-lint row pin 84 -> 88              |

### Baseline board builds (`8681fcb4`, `MC_AU_DEV_CHANNEL=1`, clean)

| Env                      | Flash (bytes)      | RAM (bytes)                                          |
| ------------------------ | ------------------ | ---------------------------------------------------- |
| `ttgo_tbeam`             | 2031089 / 3403776  | 104728 / 1310720                                     |
| `ttgo_tbeam_supreme`     | 1947441 / 3403776  | 129764 / 327680                                      |
| `heltec_wifi_lora_32_V2` | 2000961 / 3403776  | 104600 / 327680                                      |
| `heltec_wifi_lora_32_V3` | 1911961 / 3403776  | 129428 / 327680                                      |
| `t_deck_pro`             | 2318765 / 12582912 | 154528 / 327680                                      |
| `t5_epaper`              | 4833757 / 6553600  | 129832 / 327680                                      |
| `wiscore_rak4631`        | 715008 / 815104    | 99404 / 248832 (linker line, incl. heap reservation) |

Strings proof baseline: `LTR390` and `[WIND]` count 0 in every image.

### Deviations from the schedule

- **C2 folded into C1.** BAT-C dropped (no clean BQ hook, see BAT-04 doc), and BAT-D/BAT-E depend only on the C0
  contract with files disjoint from every C1 writer, so they ran in C1. Saves one gate cycle.
- **BAT-B owns `src/esp32/esp32_pmu.cpp` too:** the AXP192 branch of `setupPMU()` never enabled the battery
  current ADC (the doc assumed it did).

### C1+C2 board builds (clean, `MC_AU_DEV_CHANNEL=1`), delta to the C0 baseline

| Env                      | Flash (bytes) | Delta | RAM (bytes) | Delta | Strings                                |
| ------------------------ | ------------- | ----- | ----------- | ----- | -------------------------------------- |
| `ttgo_tbeam`             | 2033225       | +2136 | 104736      | +8    | `Charge: ` 1                           |
| `ttgo_tbeam_supreme`     | 1949445       | +2004 | 129772      | +8    | `Charge: ` 1                           |
| `heltec_wifi_lora_32_V2` | 2002097       | +1136 | 104608      | +8    | `LTR390` 0, `[WIND]` 0 (not wired yet) |
| `heltec_wifi_lora_32_V3` | 1913053       | +1092 | 129428      | 0     | `LTR390` 0, `[WIND]` 0 (not wired yet) |
| `t_deck_pro`             | 2319653       | +888  | 154536      | +8    | -                                      |
| `t5_epaper`              | 4834417       | +660  | 129840      | +8    | -                                      |
| `wiscore_rak4631`        | 715552        | +544  | 99404       | 0     | `LTR390` 0, `[WIND]` 0                 |

The RAK image grows only by the shared BAT-04 code (formatter, `--info` line, web row); the WX-01 modules compile to
nothing there. WX-01 W1 modules are not referenced yet, so the linker drops them from the ESP32 images; the strings
proof for `LTR390` / `[WIND]` moves to C3/C4.

### C3 board builds (clean), delta to C1

| Env                      | Flash (bytes) | Delta to C1 | Delta to C0 | RAM (bytes) | Strings                |
| ------------------------ | ------------- | ----------- | ----------- | ----------- | ---------------------- |
| `ttgo_tbeam`             | 2036937       | +3712       | +5848       | 104848      | `LTR390` 3, `[WIND]` 1 |
| `ttgo_tbeam_supreme`     | 1953437       | +3992       | +5996       | 129868      | `LTR390` 3, `[WIND]` 1 |
| `heltec_wifi_lora_32_V2` | 2006133       | +4036       | +5172       | 104720      | `LTR390` 3, `[WIND]` 1 |
| `heltec_wifi_lora_32_V3` | 1917073       | +4020       | +5112       | 129524      | `LTR390` 3, `[WIND]` 1 |
| `t_deck_pro`             | 2320509       | +856        | +1744       | 154544      | `LTR390` 0             |
| `t5_epaper`              | 4835073       | +656        | +1316       | 129840      | `LTR390` 0             |
| `wiscore_rak4631`        | 716008        | +456        | +1000       | 99404       | `LTR390` 0, `[WIND]` 0 |

RAK growth since C1 is the "not supported on this board" answers and the `--windpin`/`--windcal` help lines.
