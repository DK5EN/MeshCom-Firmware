# Review of the test gate: sound tests, slow by structure

- Date of review: 2026-11-23
- Tree: `fork-dev` at `799aa238`
- Scope: Unity host suites, golden lints, Python and Node tool suites, the runner `tools/regression.sh`, the bench stage
- Status: approved by the operator on 2026-11-23. Decisions 1, 2, 3 and 5 as recommended; decision 4 is no. The
  rework runs as [campaign-test-gate-20261123.md](campaign-test-gate-20261123.md).

**The tests themselves are mostly sound: 202 of 215 Unity suites execute real production code. The gate is slow
because of how it is run, not because of what it checks, and it can be cut to a fraction without losing coverage.**

**Recommendation: approve a five-wave campaign. First make the full gate fast and honest, then add the change-based
run, then repair the weak suites.**

## Decisions (answered 2026-11-23)

> **The operator follows the recommendations, except decision 4.** The host gate stays on the MacBook: GitHub
> runners are slow and the local machine makes more sense. No server-side run is built.

1. **When is the change-based run enough?** Recommended: for every wave commit. The full gate stays mandatory before a
   release, before an upstream PR, after an upstream sync, and once at the end of a campaign.
2. **May weak suites be deleted?** Recommended: yes for the four that protect nothing (section 4.3); repair the rest.
3. **May the repair move code out of the big source files?** Converting text pins into executed tests means extracting
   logic from `command_functions.cpp` and `loop_functions.cpp`. That grows the fork diff against upstream.
   Recommended: only for the three worst suites, and only where an upstream PR is planned anyway.
4. **Should GitHub run the host gate on every push?** Today no test runs anywhere but on this Mac. Recommended: yes,
   host gate only, no formal checks, no bench.
5. **Should the gate build ESP32 boards?** Today it links three nRF52 boards and no ESP32 board. Recommended: two
   ESP32 boards in the full gate, all boards stay with the tag build.

## Key facts

- **Size:** 130 native test environments, 215 Unity suites, 4245 cases, all green (run of 2026-11-23 21:01, `f6c6acd7`).
  Stage 2 adds 1793 pytest cases, 12 browser-page scripts, 6 log-tool scripts and 14 golden lints.
- **Warm full gate:** 696 s, about 11.6 min (same run, from log timestamps). Unity 289 s, pytest 261 s, nRF52 link
  builds 62 s, golden self-test 57 s, the rest 27 s.
- **Pytest waits, it does not compute:** 259 s wall time, 24 s CPU (measured 2026-11-23, `--durations`). Seven cases
  take 105 s. All seven test bench tools, none tests firmware.
- **Most of pytest is not about firmware:** 1234 of 1793 cases (69 %) test bench and analysis tools. About 75 cases
  (4 %) read `src/`, `variants/` or `platformio.ini`.
- **Cold runs are common and avoidable:** PlatformIO deletes the whole build directory when `platformio.ini` changes
  or a source file is added. Almost every fix wave does one of both.
- **Change-based running does not exist yet:** `docs/BACKLOG.md:66` allows "the affected `pio test -e native*`", but
  `tools/regression.sh` has only `--stage`. Choosing the affected envs is manual today.
- **The core is not executed on the host:** `command_functions.cpp`, `loop_functions.cpp`, `esp32_main.cpp`,
  `nrf52_main.cpp`, `lora_functions.cpp`, `web_functions.cpp` and `lv_obj_functions.cpp` compile in no native env.
  That is about 40 000 lines. They are covered by text pins, golden lints and the bench only.
- **No server runs any test:** `meshcom-ci.yml` builds on tags and runs no test. The gate holds by discipline alone.

## 1. Discovery: what exists

The gate is one script, `tools/regression.sh`, with three stages. The inventory document is `docs/test-suite-map.md`.

| Stage | What runs                                                                               | Size                                      | Warm time |
| ----- | --------------------------------------------------------------------------------------- | ----------------------------------------- | --------- |
| 1     | one `pio test` per native env                                                           | 130 envs, 215 suites, 4245 cases          | 289 s     |
| 1     | `test/golden/selftest.sh`: lints on the real tree, lint self-tests, tool self-tests     | 48 commands, 14 lints                     | 57 s      |
| 1     | `pio run` for `t_echo`, `heltec_t114`, `wiscore_rak4631` with a 95 % flash limit        | 3 board links                             | 62 s      |
| 2     | ruff, pytest over `tools/bench`, `tools/tests`, `tools/mock`, `tools/gps`               | 1793 cases                                | 261 s     |
| 2     | browser-page scripts (jsdom), Node tests, log-tool scripts, tool self-tests, model lint | 12 + 4 + 6 + 7 + 1 steps                  | 27 s      |
| 2     | TLA+ and bounded model checking                                                         | opt-in with `--formal`, not reviewed here | skipped   |
| 3     | bench fleet over USB: identity guard, per-board harness, OTA, mesh exchange             | 3 of 4 nodes attached on 2026-11-23       | not timed |

- **Layout of the Unity envs:** one large env `native` holds 71 suites. 121 envs hold exactly one suite. 77 envs are
  header-only and differ only in their name and `test_filter`.
- **Per-env cost:** a single-suite env takes about 2 s, of which the test itself needs 0.6 s. The rest is PlatformIO
  start-up, paid 130 times.
- **Seven suites run in two or three envs** on purpose (platform twins, table sizes, dev and prod channel).
- **Housekeeping:** `tools/bench/runs/` holds 476 regression run directories, 1.1 GB.

## 2. Verification: what the gate actually checks

Method: three agents read every Unity suite of one third each, a fourth read all non-Unity steps. I spot-checked their
sharpest claims against the code (section 8).

### 2.1 Unity suites by what they prove

| Class              |   Suites | Meaning                                                                             |
| ------------------ | -------: | ----------------------------------------------------------------------------------- |
| Real               |      202 | compiles or includes production code and asserts hand-written expected values       |
| Text pin           |        8 | reads source files as text and asserts that a string is present or in order         |
| Copy               |        3 | the logic under test is re-implemented inside the test                              |
| Tautology          |        2 | the assertion cannot fail on a production change                                    |
| Real, with caveats | about 40 | counted under Real above, but a noticeable share of cases is pin, copy or tautology |

- **Text pins are spread wider than eight suites:** about 32 suites contain pin cases. In the first third of the
  suites 142 of 1263 cases (11 %) are pins.
- **Compiled production code:** 67 of 267 `.cpp`/`.c` files under `src/` are compiled into a native env, 25 700 of
  212 000 lines. The total includes fonts, images and the vendored TLS and mDNS overrides.

### 2.2 The pattern behind the numbers

- **Extract, then test the extract.** Logic is moved into a small header and tested for real. This works well and
  explains the 202.
- **The call site stays unproven.** The big files that call the header compile in no native env. Whether production
  calls the helper correctly is proven by a text pin, or not at all.
- **A green suite proves the helper, not the feature.** Examples without any call-site check: `test_prod_lock`,
  `test_pos_tag_nan`, `test_wifi_start_gate`, `test_web_coord`.

### 2.3 Non-Unity steps

| Step                                   | Protects                                  | A failure means                 |
| -------------------------------------- | ----------------------------------------- | ------------------------------- |
| 14 golden lints on the real tree       | firmware `src/`, `variants/`              | firmware defect or lint drift   |
| 11 `webgui_*_test.js`, safeboot page   | the web pages shipped inside the firmware | firmware web UI defect          |
| pytest firmware subset, about 75 cases | `src/` text, build patches, web assets    | firmware defect or pin drift    |
| pytest build-patch tests, 129 cases    | scripts that patch libraries before build | a mis-patched firmware          |
| pytest tool tests, 1234 cases          | bench and log tools                       | tool defect, firmware untouched |
| log-tool scripts, tool self-tests      | tools                                     | tool defect                     |
| `specs/selftest.sh`                    | model lint, model drift against `src/`    | mostly tool defect              |

## 3. Why it is slow

| Cause                                                | Cost per warm run                     | Evidence                                                        |
| ---------------------------------------------------- | ------------------------------------- | --------------------------------------------------------------- |
| Real sleeps and timeouts in bench-tool tests         | 168 s in 38 cases                     | pytest `--durations`, 2026-11-23                                |
| 130 separate `pio test` starts                       | about 180 s of 289 s                  | 2 s per env against 0.6 s test time; assumption                 |
| The mock tool suite runs twice (stage 1 and stage 2) | part of the 57 s                      | `test/golden/selftest.sh:145`, `regression.sh:206`              |
| Tool tests run on every firmware change              | most of stage 2                       | 69 % of pytest cases                                            |
| A fixed 3.5 s timer in one page test                 | 10 s                                  | `tools/webgui_load_test.js:57`                                  |
| Build directory wiped on config or file-list change  | 20 to 40 min instead of 12            | PlatformIO `run/helpers.py:32-45`; time from `/full-regression` |
| Every wave runs everything                           | 13 full or partial runs on 2026-11-23 | `tools/bench/runs/regression-20261123-*`                        |

- **The two slowest cases:** `tools/bench/test_meshlogger.py` starts the real logger for 36 s and 15 s (`:154`, `:197`).
- **The wipe rule:** PlatformIO hashes the whole configuration and the list of source file names. One new header or
  one new env invalidates every build, native and board.
- **No object cache is configured:** `platformio.ini` sets no `build_cache_dir`. Whether a cache removes the cold-run
  penalty here is an assumption and must be measured.

## 4. Verdict

### 4.1 What is good

- **Real code, hand-written expectations.** 94 % of the Unity suites. Negative cases for untrusted input are present
  nearly everywhere.
- **Independent oracles in the strongest suites:**
  - `test_remote_cmd`: vectors from a separate Python implementation, built for dev and prod channel.
  - `test_tz_rule`: the host C library as oracle.
  - `test_hmac_sha256`, `test_applink_ws`: NIST and RFC vectors.
  - `test_dedup_replay`: 48 node-hours of field decisions under the address sanitizer.
  - `test_fw_update_assets`, `test_fw_update_http`: real GitHub answers, split at every byte.
- **Platform twins.** `test_udp_frame_twin` and `test_udp_send_twin` link the ESP32 and the nRF52 source into one
  binary and feed both the same input.
- **Fail-closed schema test.** `test_settings_members` enumerates the real settings struct; a new member that is
  neither stored nor listed fails.
- **Golden lints can fail.** Every lint has negative cases in its self-test and then runs on the real tree.
- **Page tests drive the shipped pages.** Most jsdom scripts unpack the real `web_assets.h` and fire real events.
- **The runner is careful.** Zero cases counts as a failure, a second `pio` process is refused, every step has a log.

### 4.2 What is missing

- **ESP32 builds.** The gate links three nRF52 boards. A change that breaks an ESP32 board is found at the tag build
  or on the bench. `REVIEW.md` rule R2 demands that every env builds.
- **Any automatic run.** No push, no pull request and no schedule triggers a test.
- **The call sites.** See 2.2. This is the largest gap in substance.
- **Sanitizers on parser envs.** Address and undefined-behaviour checks are off in `native_conf_frame`, `native_xml`,
  `native_parsers`, `native_aprs`, `native_config`, `native_dhcps_opt`, `native_blocked_calls` and `native`.
- **Literal wire bytes for AX.25 addresses.** `test_kiss_ax25` and `test_kiss_frame` check by encode-then-decode
  only. A symmetric bug passes both.
- **Coverage numbers.** Nothing measures which lines the suites execute. The figures in 2.1 are file counts.
- **Proof that a pin can fail.** Five pin tests have a hook to point them at a mutated tree; no gate uses it.
- **Honest reporting of skips.** The summary prints "1789 passed". The 4 skipped cases are invisible, and they depend
  on local state such as `.pio/libdeps`.
- **Tests on disk that no gate runs:**
  - `tools/test_pages_flasher.py` (outside the pytest paths; moved to `tools/tests/test_pages_flasher_args.py` in wave 0).
  - `test/golden/variant_macros_effective.py --self-test`.
  - `tools/bench/identity_guard.py --self-test`, `tools/bench/console_golden.py --self-test`.
  - `tools/webgui_tick_test.js` (needs a live node, in no stage at all).

### 4.3 What is tautological or protects nothing

| Suite or step                     | Finding                                                                                       | Evidence                                           |
| --------------------------------- | --------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `test_web_stop_gate`              | the production function returns its argument; the test counts its own model                   | `src/web_server_gate.h`, test `:16-22`             |
| `test_millis_rollover`            | includes no production code; both comparators are defined in the test                         | `test/test_millis_rollover/test_main.cpp:22-32`    |
| `test_compress`                   | the code under test exists only in the test directory; `src/` does not use it                 | `src/command_functions.cpp:136` (commented out)    |
| `test_gps_sky_layout`             | asserts relations between constants that the header defines by those relations                | `test_main.cpp:14-22`                              |
| `test_decodetinyxml`              | own copy of the settings struct with a 25-byte field; production has 150 bytes                | stub `WisBlock-API.h:27`, `meshcom_settings.h:194` |
| `test_ack_replay`                 | one case, asserts only that no frame is rejected; an always-true filter passes                | `test_ack_replay.cpp:91-105`                       |
| `test_ble_session` (race cases)   | the reconnect race is proven on a hand-written model of the callbacks, not tied to the source | `test_main.cpp:22-97`                              |
| `test_posinfo_timer` (8 of 11)    | the main-loop block is mirrored in the test                                                   | `:43-73`                                           |
| `test_external_radio_txq` (ring)  | test-local predicates that have already drifted from production                               | test `:30-39`, `src/txring_functions.cpp:549`      |
| `verify_captures.py`              | green when it checks zero captures                                                            | end of script                                      |
| `test_gw_legacy_capture.py:87`    | compares a generator with its own earlier output                                              | same file                                          |
| `test_gen_web_assets.py:25`       | `render() == render()`                                                                        | same file                                          |
| "old logic" cases in eight suites | a copy of the pre-fix code is asserted against itself                                         | e.g. `test_batt_pipeline/test_main.cpp:662-701`    |

### 4.4 What needs improvement

- **Text pins that are the whole suite.**
  - `test_command_setters`: 56 of 79 cases are pins, 760 `find(` calls.
  - `test_display_clock`: 25 of 42 cases.
  - `test_command_toggles`: 19 of 37 cases; the dispatcher is tested on a hand-copied table.
  - `test_drift_dr16_gates`, `test_batt_charge_pmu_pins`, `test_sset3_restore`: pins only.
  - Effect: a harmless reformat breaks them, a logic change next to the kept string passes.
- **Goldens produced by the code under test.** `test_aprs_corpus`, `test_country_twin`, the twin dumps. They detect
  change, not correctness. That is acceptable when labelled so.
- **Loose replay verdicts.** `test_nbr_replay` and `test_topo_shadow` pass while no difference lands in "other". A
  regression inside a known category is counted, not failed.
- **Stubbed inputs.** `test_country_twin` takes the radio macros from its own stub headers, not from `variants/`.
- **Page tests on invented markup.** `webgui_queue_test.js` and `webgui_au_sbwhy_test.js`.

### 4.5 What needs restructuring

- **130 envs in one file.** 77 header-only envs are identical except for the name. Each new one edits
  `platformio.ini` and so wipes all builds.
- **Hard-coded step lists.** The 12 page tests and 7 tool self-tests are named one by one in `regression.sh`, 48
  commands in `selftest.sh`. A new file is silently not run.
- **Firmware and tool tests in one step.** The pytest step mixes both, so neither can be skipped.
- **Five ways to find the repository root** in the pin tests. Two readers do not check that the file opened
  (`test_t114_theme.cpp:24`, `test_tdeck_pattern.cpp:112`).
- **Hand-copied settings struct** in `test/support/nrf52/WisBlock-API.h` and one suite-local copy, which has drifted.
- **The inventory document is prose.** `docs/test-suite-map.md` states 67 envs and 124 suites in its header; the tree
  has 130 and 215. It also states 115 mock cases (320) and 438 bench cases (692).
- **Stale comments in the runner.** "four --self-test tools" (seven), the pytest step name omits `tools/gps`.

## 5. What goes beyond the current concept

The current concept is: one local script, everything every time, plus the bench by hand. These items are outside it.

- **Change-based selection as a rule, not a habit.** A selector that maps changed paths to steps, with "unknown path
  means full gate" as the safe default (section 6).
- **A server-side run.** The host gate on GitHub for every push to `fork-dev`. It needs no hardware. One suite uses
  a macOS-only call (`test_udp_frame_twin`), so a Linux runner must be tried first.
- **Line coverage.** The native suites can be built with coverage instrumentation. That replaces file counts with
  executed lines and shows which extracted headers are tested only in part.
- **Mutation checks for pins and lints.** Run each pin against a tree with the pinned line removed and require red.
  Without that, "can this test fail" stays a belief.
- **Compiling the big files on the host.** One env that compiles `command_functions.cpp` against stubs would turn
  dozens of pins into executed tests. This is the largest single gain in substance and the largest effort.
- **Fuzzing beyond the APRS decoder.** The KISS deframer, the EXTUDP JSON input and the web form parser take
  untrusted input and have no fuzz corpus.
- **Bench results as a gate input.** Stage 3 skips a node that is not attached. The T-Deck was not attached on
  2026-11-23. Nothing records which board last passed on which commit.
- **Generated inventory.** The suite map produced by a script from the tree, so it cannot go stale.

## 6. Concept for the change-based run

- **Three levels instead of one:**
  1. **Touched** (target: under 2 min): only the steps whose inputs changed. For every wave commit.
  2. **Full host gate** (target: under 5 min warm): everything in stages 1 and 2.
  3. **Bench:** unchanged, with hardware.
- **How the selector decides:** it lists the changed files against a base commit plus the working tree, then looks up
  each file in a map from path patterns to steps.
- **Where the map comes from:**
  - Compiled dependencies: from the compiler's own dependency output per native env, not from a hand-kept list.
  - Text pins and lints: each declares the files it reads. A first table for all non-Unity steps exists from this
    review.
  - `test/support/**`, `platformio.ini`, the runner itself: always the full gate.
- **Safe default:** a changed path that no rule knows triggers the full gate. The selector prints why it chose what.
- **Safety net:** the full gate runs before release, PR and sync, and at the end of every campaign. A step that
  fails there but was not selected is a selector defect and gets a regression test in the selector's own suite.
- **Assumption:** the 2 min target is an estimate from the measured step times. It is measured in wave 2.

## 7. How to run the campaign

Five waves on `fork-dev`. Each wave ends with the full gate and one commit. Waves 1 and 2 deliver the speed; wave 3
delivers the substance.

| Wave | Content                                                                                                                                                                                                                 | Acceptance                                                                                  |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| 0    | Baseline and honest reporting: per-step time in the summary, skips shown, step lists by file pattern, `verify_captures` fails on zero, the four ungated tests wired in, old run directories pruned                      | same case counts as before plus the newly wired ones; summary shows time and skips          |
| 1    | Speed of the full gate: mock suite once; waits in the bench-tool tests made injectable; pytest split into firmware and tool steps; native envs started in fewer `pio` calls; object cache tried and measured            | warm full gate under 5 min, measured three times; no case lost (count compared with wave 0) |
| 2    | Change-based run: selector with dependency map, `--changed` in `regression.sh`, selector self-test, rules in `CLAUDE.md` and `/full-regression`                                                                         | for 10 past commits the selector picks every step that the commit's own tests belong to     |
| 3    | Test debt: delete or repair the suites of 4.3, real settings struct in `test_decodetinyxml`, one shared root finder, sanitizers on the parser envs, literal AX.25 bytes, exact category counts in the two replay suites | every repaired test seen red against the old defect; sanitizer findings fixed or listed     |
| 4    | Gaps: two ESP32 board builds in the full gate, coverage report, suite map generated by script                                                                                                                           | board builds green; coverage figure per file in the generated map                           |

- **Order matters.** Wave 0 comes first so that every later wave is measured against the same counts and times.
- **Dispatch.** Waves 0 to 2 touch the runner and the tool tests and can be split among parallel writers with
  disjoint files. Wave 3 touches `test/` and partly `src/` and needs one `pio` at a time.
- **Not in the campaign unless decided:** compiling the big source files on the host, mutation checks, fuzzing
  (section 5). Each is a campaign of its own.

<details>
<summary>The five decisions, with consequences</summary>

| Nr  | Decision                                | If yes                                                                                                                | If no                                                             | Recommendation  |
| --- | --------------------------------------- | --------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- | --------------- |
| 1   | Touched run is enough for a wave commit | waves gate in about 2 min; a selector gap is found only at the next full run                                          | waves keep the full gate, about 5 min after wave 1                | yes             |
| 2   | Delete suites that protect nothing      | `test_web_stop_gate`, `test_millis_rollover`, `test_compress`, `test_gps_sky_layout` go; case count drops by about 30 | they are relabelled as constants checks and stay                  | yes             |
| 3   | Extract code from the big source files  | pins become executed tests; fork diff against upstream grows in `src/`                                                | pins stay, made less brittle (comment stripping, one root finder) | limited         |
| 4   | Host gate on GitHub for each push       | a forgotten local run is caught; needs a Linux trial of the suites                                                    | the gate stays local and by discipline                            | yes             |
| 5   | ESP32 boards in the full gate           | plus about 1 min warm per board; ESP32 breakage found before the tag                                                  | ESP32 breakage found at the tag build or on the bench             | yes, two boards |

</details>

## 8. What was not checked

**Not run**

- No Unity suite was executed for this review. The 4245 green cases come from the run log of 2026-11-23 21:01.
- No test was mutated. "Cannot fail" in section 4.3 is from reading the code, confirmed by me for
  `test_web_stop_gate`, `test_millis_rollover`, `test_compress` and `test_decodetinyxml`.
- No cold run was timed. The 20 to 40 min figure is from `.claude/commands/full-regression.md`.

**Read in part**

- Large suites were sampled, not read line by line: header, helpers, env, pin sections and about five assertions each.
  The class of each suite is solid; "every case has a hand-written expectation" is not verified for them.
- Three golden lints were read only at the top (`producer_match`, `settings_persist`, `settings_schema`).

**Out of scope**

- The formal checks (TLA+, bounded model checking). They are opt-in and were excluded on purpose.
- The bench harnesses of stage 3 beyond their plan output. Whether each harness scenario can fail was not reviewed.
- Soak and field runs.

**Assumptions**

- That fewer `pio` starts save about 180 s. Derived from 2 s per env against 0.6 s test time, not measured.
- That an object cache survives the build-directory wipe.
- That the suites build on Linux.

<details>
<summary>For developers: per-suite findings worth keeping</summary>

**Suites with a noticeable share of pin cases**

`test_au_schedule` (6 of 45), `test_autosymbol` (3 of 22), `test_batt_label` (2 of 6), `test_cet_age` (2 of 36),
`test_config_json` (2 of 35), `test_csma_timing` (2 of 28), `test_ext_reg` (11 of 33), `test_gps_skyplot` (1 of 28),
`test_netif_mtu` (1 of 3), `test_nrf52_settings_paths` (1), `test_pin_status` (1 of 30), `test_ping_budget` (2),
`test_posinfo_next`, `test_rf_pa` (2 of 4), `test_safeboot_state` (3 of 67), `test_safeboot_ver` (1),
`test_settings_sanitize` (4), `test_t114_theme` (2 of 6), `test_tdeck_pattern` (4 of 9), `test_telemetry_app` (2 of 4),
`test_txring` (1 of 107), `test_udp_hold` (1), `test_web_guard` (1).

**Pins in pytest**

`test_nrf_eth_dns_sites.py`, `test_info_netmode_site.py`, `test_ina_web_site.py`, `test_msgstore_echo_sites.py`,
`test_mesh_policy_sites.py`, `test_smartbeacon_signed_sites.py`, `test_rm_rx_gate_lint.py`, `test_nrf52_gps_probe.py`,
`test_w5100s_patch.py:118-190`, `test_pages_flasher.py:302-303` (exact indentation, the most brittle).

**Slow pytest cases (2026-11-23)**

| Case                                           | Time   |
| ---------------------------------------------- | ------ |
| `test_meshlogger.py::test_zombie_erkannt_...`  | 39.4 s |
| `test_meshlogger.py::test_disk_low_pauses_...` | 20.1 s |
| `test_ota_regression.py` (5 cases)             | 46.1 s |
| `experiments/test_apreboot.py` (6 cases)       | 21.0 s |
| `tools/tests/test_gwrx_bench.py` (10 cases)    | 16.6 s |
| `test_ota_abort.py` (8 cases)                  | 13.1 s |

**Duplicated coverage, layered rather than redundant**

- `txring_functions.cpp`: `test_txring`, `test_txring_flood`, `test_unconfigured`, `test_txprio_replay`.
- `nbr_matrix.cpp`: five suites.
- `isPlausibleAckFrame`: `test_ack_replay`, `test_ack_validate`, `test_aprs_fuzz`.
- `test_esp32_flash_lifecycle.cpp:159` and `:188` have the same body.

**Step-to-path table for the selector (non-Unity steps)**

| Step                               | Needed when these change                                                                                 |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------- |
| golden lints on the real tree      | `src/**`, `variants/*/configuration.h`, `test/golden/*_lint.py`                                          |
| `variant_ini_effective`            | `platformio.ini`, `variants/*/platformio.ini`                                                            |
| pytest firmware subset             | `src/**`, `variants/t_deck*/lv_conf.h`, `test/golden/gw_servers_legacy.json`                             |
| pytest build patches               | `tools/*_patch.py`, `tools/lwip_wrap.py`, `platformio.ini`, the patched call sites in `src/`             |
| pytest `tools/bench`, `tools/mock` | `tools/bench/**`, `tools/mock/**`, `tools/meshlogger.py`, `tools/loganalyse.sh`, `tools/berglog*.py`     |
| pytest flasher and release         | `tools/pages_flasher.py`, `tools/make_autoupdate*`, `pages/flash/**`, `.github/workflows/meshcom-ci.yml` |
| `webgui_*_test.js`, safeboot page  | `src/web_functions/**`, `src/safeboot/ota.html`, `tools/webgui_*.js`, `tools/gen_web_assets.py`          |
| log-tool scripts                   | `tools/nbr*.py`, `tools/soakstatus.py`, `tools/meshlogger.py`, `test/test_nbrlog/**`                     |
| `specs/selftest.sh`                | `specs/**` and the `src/` files named in `specs/manifest.json`                                           |
| `wire_doc_html --check`            | `docs/architecture/11-wire-format.{md,html}`                                                             |

**Raw data of this review**

- Run log: `tools/bench/runs/regression-20261123-210135/`.
- PlatformIO wipe rule: `platformio/run/helpers.py:32-45`, `platformio/project/helpers.py:132`.

</details>
