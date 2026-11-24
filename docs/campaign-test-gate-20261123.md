# Campaign: test gate rework (TESTGATE-01)

- Started: 2026-11-23
- Branch: `fork-dev`
- Source: [review-test-gate-20261123.md](review-test-gate-20261123.md) (findings, evidence, the five decisions)
- Resume point: this file. Update the status table after every wave.

**The full host gate is green in 3.1 min warm (5.7 min when the two new ESP32 builds recompile), against 11.6 min before. The change-based run
for wave commits is verified: 5 s for a suite edit, 53 s for a leaf header. Everything is committed.**

## What to do next

1. Continue with the steps marked open in waves 3 and 4.
2. Full host gate at the end of the campaign.

## Operator decisions (2026-11-23)

| Nr  | Decision                               | Answer                                                               |
| --- | -------------------------------------- | -------------------------------------------------------------------- |
| 1   | Change-based run for a wave commit     | yes; full gate before release, upstream PR, after sync, campaign end |
| 2   | Delete suites that protect nothing     | yes: the four of review section 4.3; repair the rest                 |
| 3   | Extract code from the big source files | limited: the three worst pin suites, where an upstream PR is planned |
| 4   | Host gate on GitHub for each push      | no: GitHub is slow, the gate stays on the MacBook                    |
| 5   | ESP32 boards in the full gate          | yes, two boards                                                      |

## Status

| Wave | Content                                            | State                                      | Gate                                         |
| ---- | -------------------------------------------------- | ------------------------------------------ | -------------------------------------------- |
| 0    | Baseline and honest reporting                      | done, committed                            | green together with wave 1                   |
| 1    | Speed of the full gate                             | done, committed                            | full gate green 2026-11-24                   |
| 2    | Change-based run (`tools/regression.sh --changed`) | done, committed                            | two change-based runs 2026-11-24, see wave 2 |
| 3    | Test debt                                          | steps 3.1, 3.2, 3.4 to 3.6 done; rest open | full gate green 2026-11-24                   |
| 4    | Gaps: ESP32 builds, coverage, generated suite map  | steps 4.1, 4.2, 4.5 done; 4.3 and 4.4 open | full gate green 2026-11-24                   |

## Measured

| Run                      | Full host gate | Unity              | pytest | Lint self-test | Result                   |
| ------------------------ | -------------- | ------------------ | ------ | -------------- | ------------------------ |
| Before, 2026-11-23 21:01 | 696 s          | 289 s              | 261 s  | 57 s           | green, 4245 Unity cases  |
| Wave 1, run 1            | 160 s          | 107 s of test time | 67 s   | 3 s            | green, 4245 + 1792 cases |
| Wave 1, run 2            | 323 s          | 2 envs red         |        |                | red, see below           |
| Wave 1, run 3            | 165 s          |                    |        |                | green, 4245 Unity cases  |
| All waves, 2026-11-24    | 339 s          |                    | 67 s   | 3 s            | green, 4236 + 1828 cases |
| Second run, 2026-11-24   | 188 s          |                    | 65 s   | 4 s            | green, 4236 + 1829 cases |

- **339 s is the first build of the two ESP32 boards on that commit, 188 s the warm repeat.**
- **The 339 s run is the first with the two ESP32 builds** (53 s and 45 s) and the sanitizers. It ran on `e73f7db8`
  with a clean tree: 129 Unity envs, 4236 cases, 1828 pytest cases, five board builds, no skips except the opt-in
  formal checks.
- **The first run of the blind edits was red in two steps**, both faults of the edits and fixed in `e2ad38c8`: a
  compile error in `test_tdeck_pattern` and a flag parser in `test_regression_select.py` that made its closure test
  check nothing.
- **The sanitizers found nothing in the six parser envs.** The runtime is linked into all six binaries, and a
  deliberate heap overflow in `test_dhcps_opt` turned the env red.

- **Run 2 is not a test failure.** The linker reported object files of the `native` env as missing
  (`stage1-unity.log`: "no such file or directory: .pio/build/native/..."). A second session built in the same tree
  at that time and PlatformIO wiped the build directory. The rule "one `pio` at a time" was broken from outside.
- **Where the time went:** the mock tool suite inside the lint self-test was 55 s and ran a second time in pytest.
  Pytest waits on timeouts, so eight workers cut it from 259 s to 67 s. One `pio` call for all envs saves the
  project parsing of 129 starts. Stage 2 runs alongside stage 1.
- **Cold runs:** not measured. Native builds are as fast cold as warm (282 s right after a wipe against 289 s warm,
  old runner). The cold penalty sits in the board links only.

## What stands (waves 0 to 2)

- **Summary with seconds and skips.** Every step line carries its duration; pytest shows "N passed, M skipped".
- **One `pio test` call for all native envs.** The verdict per env comes from PlatformIO's JSON report
  (`tools/regression_unity.py`, with a self-test for missing envs, zero cases, build errors).
- **Stage 2 alongside stage 1.** `REGRESSION_SERIAL=1` restores the old order. `PYTEST_WORKERS` sets the worker
  count, default 8.
- **Mock suite once per gate.** `selftest.sh` leaves it to pytest when stage 2 is part of the run; standalone it
  still runs it (release step 2).
- **Page tests by file pattern.** Every `tools/webgui_*_test.js` on disk runs, except the two that need a live node.
- **Four ungated tests wired in:** `tools/tests/test_pages_flasher_args.py` (moved from `tools/`),
  `variant_macros_effective.py --self-test`, `identity_guard.py --self-test`, `console_golden.py --self-test`.
- **`verify_captures.py` fails on zero captures.**
- **Old run directories are pruned.** The newest 20 stay, and every run with a tracked file or a mention under
  `docs/`. 453 directories were removed on the first run.
- **Change-based run.** `tools/regression.sh --changed [<commit>]` asks `tools/regression_select.py`:
  - Unity envs by include closure, computed from the tree on every call, including source-text pins.
  - Other steps by path rules.
  - `platformio.ini`, the runner, or a path no rule knows: full gate.
  - A leaf header selects 1 env instead of 130; `configuration_global.h` selects 39.

## Deviations from the plan

- **Waves 0 and 1 share one verification.** The wave 0 run failed on my own wiring of the stray pytest file; the fix
  went into the wave 1 runs.
- **Waits in the bench-tool tests were not made injectable.** Parallel workers reached the target without touching
  `meshlogger.py` and the OTA tools.
- **Pytest was not split into two steps.** The change-based run uses a firmware file list instead
  (`PYTEST_FIRMWARE` in the selector).
- **Object cache not tried.** It only matters for board builds; step 4.3.
- **Waves 3 and 4 were written without any run** (operator 2026-11-23: implement straight, no test runs) and
  verified one day later.
- **A parallel session committed one of my renames.** `tools/test_pages_flasher.py` to
  `tools/tests/test_pages_flasher_args.py` is part of `0e2fefd9`; the content fix of that file is still uncommitted.

## Wave 2, verified 2026-11-24

| Change                                | Steps selected                                                                                                   | Time | Result            |
| ------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | ---- | ----------------- |
| `test/test_dhcps_opt/test_main.cpp`   | Unity `native_dhcps_opt`, lint self-test, suite inventory                                                        | 5 s  | red, as intended  |
| `src/rtc_offset.h` (comment appended) | Unity `native_rtc_offset`, lint self-test, two ESP32 and three nRF52 builds, firmware pytest subset, model drift | 53 s | green, as planned |

- **The suite edit was the deliberate heap overflow**; the red is the sanitizer report.
- **The selector tests run in the gate** (`tools/tests/test_regression_select.py`): the include closure against the
  compiler's own dependency list, every suite file selects its envs, every native env has a suite.
- **Not built:** the replay of 10 past commits. The structural checks replace it; a replay over git history breaks
  on files that no longer exist.
- **Defect found and fixed 2026-11-24:** a change to suites of several envs ran every suite in every env, because
  a suite filter on the `pio` command line replaces the filter of each env. Three suites in five envs gave ten false
  failures. The selector now emits one `pio` call per filter group; the selector tests hold the case.
- **Known limit:** a dirty tree selects more than the wave. Files of a parallel session count as changed.

## Wave 3 (test debt)

| Step | Content                                                                                                               | State                                                                                       |
| ---- | --------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| 3.1  | Delete `test_web_stop_gate`, `test_millis_rollover`, `test_gps_sky_layout` and our `test_compress.cpp`                | done; `platformio.ini` and the resolved-config baseline updated                             |
| 3.2  | `test_decodetinyxml`: field size as in production (150 bytes), truncation case adjusted                               | done; the stub is still a hand copy, only its size is right                                 |
| 3.3  | One shared repository-root finder for all pin tests                                                                   | open; only the two readers that ignored an unreadable file now fail on it                   |
| 3.4  | ASan/UBSan on `native_conf_frame`, `native_xml`, `native_parsers`, `native_aprs`, `native_config`, `native_dhcps_opt` | done; no findings, and a planted overflow is caught                                         |
| 3.5  | Literal AX.25 address bytes                                                                                           | done in `test_kiss_ax25`; bytes worked out by hand from the AX.25 rules, not from a capture |
| 3.6  | Exact category counts in `test_nbr_replay`, `test_topo_shadow`, `test_ack_replay`                                     | done: exact counts of every category and of the fixture sizes, in all three suites          |
| 3.7  | Remove "old logic asserted against itself" cases in eight suites                                                      | open                                                                                        |
| 3.8  | Extraction for `test_command_setters`, `test_display_clock`, `test_command_toggles`                                   | open: changes `src/`, not responsible without a build and a bench look                      |

- **`test/test_compress/` is not gone.** `compress_functions.cpp` and `.h` are upstream files and stay as inert
  copies, as before 2026-10-03. Only our suite file against them was removed. Deleting the directory would be a
  permanent fork diff.
- **The resolved-config baseline was regenerated** (`test/golden/variant_ini_effective.py --generate`). Its diff is
  exactly the removed env, the three removed filter entries and the sanitizer flags of the six envs.
- **`native_blocked_calls` and `native` were left without sanitizers.** `native` holds 71 suites.

## Wave 4 (gaps)

| Step | Content                                                                                | State                                                                               |
| ---- | -------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| 4.1  | Two ESP32 board builds in the full gate: `heltec_wifi_lora_32_V3`, `ttgo_tbeam`        | done: 53 s and 45 s in the full gate; no flash percentage limit of our own          |
| 4.2  | Selector: `src/nrf52/` selects only the nRF52 builds, ESP32-only directories the ESP32 | done; covered by the selector self-test                                             |
| 4.3  | Try `build_cache_dir`; measure one cold board build with and without                   | open: needs runs                                                                    |
| 4.4  | Line coverage of the native envs as an opt-in report                                   | open                                                                                |
| 4.5  | Suite inventory generated from the tree                                                | done and executed: `tools/suite_map.py`, `docs/test-suite-inventory.md`, gate check |

- **The inventory after the removals:** 129 native envs, 211 suites, 3924 `RUN_TEST` cases counted once per suite.
  48 suites read source files as text. 40 suites run under ASan/UBSan in every env.
- **A new suite, env or case now needs** `python3 tools/suite_map.py --write`, otherwise the gate step
  "suite inventory up to date" is red.
- **The ESP32 builds make the full gate longer:** 339 s against 160 s without them and without the sanitizers.

## Left alone on purpose

- **Waits in the bench-tool tests** (`test_meshlogger.py`, `test_ota_regression.py`). Stage 2 runs alongside stage 1
  and finishes first, so shorter waits would not shorten the gate. Changing timing inside `meshlogger.py` without a
  run risks the soak logger.
