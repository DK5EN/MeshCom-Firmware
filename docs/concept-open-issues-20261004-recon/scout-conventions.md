<!-- Recon report by a read-only Sonnet scout, 2026-10-04, fork-dev a9e64cf6. Line references are a snapshot; re-verify before editing. -->

# scout-conventions (fork-dev, HEAD at scan: 5899e12b; read-only audit, nothing in repo touched)

BLUF: a campaign paper is a plain-markdown file `docs/<topic>-wave-plan.md` (or `campaign-<topic>-YYYYMMDD.md`), prettier-formatted,
registered in docs/BACKLOG.md (header Stand block, 3.2 rows, 5.x index) and docs/test-suite-map.md; every feature ships a native Unity env +
suite-map row; upstream PRs carry src/ only. Paths below relative to repo root.

## Findings

### 1. Wave-plan document conventions

Reference docs (all exist): docs/ntp-tz-rtc-wave-plan.md (166 l, best template), docs/campaign-tdeck-w02-20261004.md (117 l),
docs/campaign-backlog-sweep-20261003.md (95 l), docs/dm-stage3-wave-plan-20260914.md (253 l). Language English (DM stage 3 mixes in German code comments only).

- ntp-tz-rtc-wave-plan.md structure: H1 :1; meta line `Date / Base: fork-dev at <sha> / Findings: <doc>` :3; `Status:` one-liner incl. DONE/commit/PR decision :4;
  `## BLUF` :6 (bullets: waves, no-struct-bump decision, forbidden approach, "no upstream PR"); `## Corrections to the findings paper` :17 (table Item|Paper says|Code says,
  re-checked at sha); `## Decisions (defaults; confirm or change)` :29 (table ID D1..D6|Question|Default, rows say **Decided:**);
  `## Waves` :40 with a model-tier + orchestrator-hotspot preamble :42-44 ("writers are `implementer` (Sonnet, high)"; hotspot = platformio.ini);
  `### W1: <title> (n writers)` :46 ... `### W4: full gate, bench, docs (orchestrator, serialized)` :110; `## Status` :125; status table Wave|State|Commit :141; `## W4 bench result` :148.
- File-ownership table, per wave: `| Owner | Files |` (:48, :95) or `| Owner | Files | Verification |` (:77). Owners A1/B1/C1 + a row `orch` for hotspot files.
  Files are exact paths, `new` prefix for created files, `{h,cpp}` brace shorthand, one-clause purpose in parentheses.
  Alternative form (dm-stage3 :110): `| Owner | Exclusive files |` + prose "Carve-outs the orchestrator applies before dispatch" (:118-122: build flag, native env, interface header).
  Alternative form (tdeck-w02 :26-32): `| Wave | Agent | Item | Exclusive files | Verification |`; hotspots as prose :34 (harness scenarios, BACKLOG.md, the campaign file itself, bench doc).
  Sweep (:27-37): `| Writer | Exclusive files | Verification (scoped) |`; hotspots :39 (platformio.ini test_filter, golden variant-ini-effective.json regenerated, .gitignore, BACKLOG.md).
- Acceptance/verification: bullet "Agent verification:" with exact commands in fixed order, "never in parallel" (:68-70: `pio test -e native -f test_rtc_offset`, then board builds);
  "Regression proof, before/after" bullet: extract helper with old arithmetic, suite must be RED, then fix, GREEN, both outputs in the report (:62-65);
  "Artifact check" bullet: `strings firmware.elf | grep -c settz` >= 1 on named envs (:105); "Gate:" bullet per wave = stage 1+2, named board builds (flash % vs baseline),
  jsdom harness if web JS touched, advisor pass (fable-review) yes/no, commit message pattern `tz(TZ-01): ...` (:72, :90, :106-108).
- Bench plan per wave lives in the last wave (:110-123): identity_guard first, per-node command list with the expected outputs, what is NOT bench-testable (no RTC chip) and how that is declared in the commit message.
- Status tracking: wave table with State + commit sha (:141-146), state text records advisor verdict and fixes ("advisor REWORK ... fixed"); deviations paragraph above it (:127-130).
  tdeck-w02 uses a dated bullet log (`2026-10-04 ~12:00: ...`, :43-117); sweep uses same + "Wave N (next)" prose (:46-68).
- Resume point: there is no dedicated heading. It is (a) the `Status:` line :4 that names DONE/open, (b) the "Wave N (next)" bullet in the dated log (sweep :51-54),
  (c) dm-stage3 "Wave status log" table Wave|Content|Status with `not started`/`in progress` (:9-13). Global rule: ~/.claude/skills/orchestrate-waves/SKILL.md section 6 (:108) "write the campaign state down, per wave".
- Memory/RESUME rule: docs/RESUME.md and the BACKLOG rows are updated per wave (memory "keep-docs-current-per-wave").
- NOTE: ~/.claude/skills/konzeptpapier/SKILL.md is a different artefact (German HTML on Desktop, Minto, BLUF box, Ausbaustufen). A docs/*.md wave plan is not that; if the operator wants a
  Konzeptpapier it is HTML on ~/Desktop, referenced from docs/ (see docs/snf-gateway-concept-20261002.md for the markdown concept form: `Status: DRAFT`, `## 1. Bottom line`, numbered sections).

### 2. BACKLOG.md conventions (docs/BACKLOG.md, 488 l)

- Open-items table = section 3.2 "Open work" at :262, header `| Id | Area | Item |` (3 columns only, no status column; status is prose inside Item, "closed" rows are deleted and
  live in docs/archive/backlog-journal-20260818-20260929.md). Rows :266-324. Item cell is one long line: symptom, fix, file refs, commit sha, "Open:/Bench:" tail.
- Id scheme: `<AREA>-<nn>` (TZ-01, RTC-04, BL-07, WEB-04, EXT-03, T5-01), sub-letters (GPS-05b), ranges (`OPT-W4..W7`), slash combos (`DR-16 / REG-03`), named ids (SNF-GW, MC5-TOPO, HASH-TAG, STRUCT, ADR-IMP).
  New ids per campaign are appended at the bottom of the table by wave (TZ/RTC rows :298-305 are the latest block).
- Header block `## Stand 2026-10-03` :7-30: one bullet per campaign ("NTP/TZ/RTC campaign (2026-10-04, fork only): ... Papers: [`..-findings.md`](..), [`..-wave-plan.md`](..)") :20-23.
- Document index = section 5 "Where to read what" :364. Subsections: 5.1 architecture set :371, 5.2 protocol/format refs :395, 5.3 "Open defects and analyses - the reading behind live backlog rows"
  (table `| Row(s) | Document |`) :413, 5.4 bench runbooks (`| Test | Document |`) :432, 5.5 reports :444, 5.6 "Proposals and designs not yet decided" (`| Subject | Document |`) :459, 5.7 journals :472.
  Register a new concept/campaign doc: a row in 5.6 (undecided concept) or 5.3 (rows `ID..ID` | doc), plus the Stand bullet, plus the 3.2 rows. NOTE: ntp-tz-rtc-*.md are NOT in 5.x (only linked from the Stand bullet) -
  register yours in 5.x as well. Existing concept row for S&F gateway: 5.6 `Store node as gateway (concept, 2026-10-02) | snf-gateway-concept-20261002.md`.
- Existing rows that overlap the five features: `SNF-GW` (3.2, "Store node as gateway, taking PMs from the central server"), `BL-05` (`{SET}` from any mesh node, security), `ADR-TOTP` (TOTP remote LED, docs/adr-totp-remote-led.md),
  `SB-05/06/07` (safeboot slot/app_valid/upstream PR). Section 6 gap table :480: "No boot/OTA document beyond the safeboot contract; one ota_0 slot means no rollback; five boards have no remote update at all."
  Prior art: `--ethmtu` / node_ethmtu (command_functions.cpp:3579, config_json.h:349, web_functions.cpp:2889, issue #1183), docs/issue-ble-i-register-mtu-20260828.md (BLE frame size), docs/safeboot-ota-contract.md.
- Prettier: run `npx --yes prettier@3 --write <file>` (global CLAUDE.md); no repo .prettierrc found; tables get padded.

### 3. Gates

- `tools/regression.sh` (248 l): Stage 1 (:123-153) = `pio test -e <env>` for every `^[env:native*]` in platformio.ini (51 sections today; 48-50 in older run logs) then `sh test/golden/selftest.sh`;
  before that it ensures the topo_shadow raw window (4 logs, copy from ~/meshlog or scp rpizero). Fails if "another pio process" runs (pio_busy :119). Stage 2 (:158-185) = ruff syntax gate
  (`ruff check --select E9,F63,F7,F82 tools test/golden test/test_nbrlog`, ruff.toml), `pytest -q tools/bench tools/tests tools/mock`, `uv run test/test_nbrlog/test_*.py`, `node --test tools/tests/*.mjs`,
  `node tools/safeboot_page_test.js` (jsdom in ~/.cache/meshcom-jsdom), `--self-test` of tools/{nbrlog,soakstatus,webflash,resource_watch}.py. Stage 3 (:189-210) = `tools/bench/bench_suite.py`.
  Duration: ~5 min stage 1+2 warm, 20-40 min cold (.claude/commands/full-regression.md, Arguments + Step 1). A PLATFORMIO_BUILD_FLAGS change (instrument build) wipes .pio/build -> next stage 1 is cold.
  Last known counts (docs/RESUME.md:35-42, stale): 48 envs, 1698 cases, 553 pytest; docs/test-suite-map.md:12-15 says 106 suites, 1663 cases, 51 native envs.
- Native env patterns in platformio.ini: `[env:native]` :192 shared env with `test_filter =` list of small suites (:199-215; add header-only suites here);
  own-env pattern (:692 native_rtc_offset header-only: `test_build_src = no`; :709 native_tz_rule: `test_build_src = yes` + `build_src_filter = -<*> +<tz_rule.cpp>`; flags
  `-std=gnu++17 -D NATIVE_BUILD=1 -D UNIT_TEST=1 -I src -Wall -Wextra`). Each env carries a comment naming the plan doc + wave. Envs list: lines 192-1741 (native, _aprs, _parsers, _batt__, \_conf_frame, \_extern, *country*_,
  _udp__\_twin, \_nrf52_settings_paths, \_ble_settings_v1, *settings*_, _command__, \_rtc_offset, *tz*_, _esp32__, \_gateway_twin, *serial*_, _config, _xml, _aprs_fuzz, _capture, _dedup, _safeboot, _extradio, _kiss_frame,
  _ble_phone_frame, _ble_phone_harness, _nbr_*, _mh_phone, _topo_shadow, _mc_text, _loop_scheduler, _byte_fifo, _batt_pipeline, _pnretry). Orchestrator owns platformio.ini edits (hotspot, wave plan :42-44).
- tests/: `test/test_<suite>/test_<suite>.cpp` (Unity, one dir per suite; stubs under test/<suite>/stubs or test/support/parser_stubs), 106 suites. Fixtures in test/golden/{corpus,native,nodes}.
- Golden selftest: test/golden/selftest.sh runs 17 lint scripts (+ --self-test each), twin_diff, drift_matrix_lint, verify_captures, `python3 -m unittest discover tools/mock`; closing line "selftest: N commands run".
  Lints a new setting/command/web field must satisfy: help_parity_lint (command must be in help), command_ladder_lint, toggle_table_lint, producer_match_lint (web element -> parser rung),
  info_switch_lint (web info row per setup switch), settings_schema_lint + settings_persist_lint + persist_readback_lint (`--persiststat` read-back), nano_printf_lint (no %lld on nRF52), variant_macros_lint.
  `variant-ini-effective.json` baseline (test/golden/native/) goes RED on every new native env or test_filter edit; orchestrator regenerates with `python3 test/golden/variant_ini_effective.py --generate`
  after reading the diff (done in 614f1b36, 9242f693, bcf8f083, eda23163, 8c905df4: each commit includes the json, +16..+20 lines per env).
- docs/test-suite-map.md (217 l): :9-52 section 1 (counts table by category Unit/Regression/Contract/Twin/Integration/Oracle/Fuzz + "Added with <campaign>" paragraph naming suite, what it pins, env);
  :54 section 2 gates; :72 section 3 inert files; :100 section 4; :117-217 section 5 per-suite table `| Suite | Category | Cases | Env(s) |` (alphabetical). A new suite = one row in section 5, header counts bumped, one paragraph in section 1.
- ruff: ruff.toml, py311, line-length 100, select E9,F63,F7,F82 only (syntax gate), scope tools + test/golden + test/test_nbrlog; docs and lib excluded. pytest: any `tools/bench/test_*.py`, `tools/tests/`, `tools/mock/` is auto-collected.
  PEP-723 `uv run` scripts for host tools (bleak ones: ble_cycle/ble_stress/ble_golden/pong_ble_check in tools/bench, NOT in stage 3).
- What a feature must add to stay in the gate: (a) a native Unity env OR a name in `[env:native]` test_filter for any new pure logic (header-only `src/<x>.h` / small `.cpp` carve-out, since
  esp32_main/nrf52_main/clock.cpp/lora_functions.cpp are not host-compilable); (b) golden baseline regeneration; (c) suite-map row; (d) pytest in tools/bench/test_*.py for any new bench tool/script;
  (e) for a setting: schema row (config_json.h X(), meshcom_settings.h A/M) so test_settings_members + settings_schema_lint stay green; BLE contract size pin in test_ble_phone_harness if a BLE frame grows.

### 4. End-to-end feature template (TZ-01/RTC), files grouped by concern

- 614f1b36 (9 files): setting schema = src/meshcom_settings.h (:199 `A(char, node_tz, [40], {0})`), src/config_json.h (:350 `X("node_tz",...)`); flash default src/esp32/esp32_main.cpp (flash-clear branch);
  pure module src/tz_rule.{h,cpp}; native tests test/test_tz_rule/, test/test_config_json/ (+65 l); platformio.ini (+19, new env); test/golden/native/variant-ini-effective.json.
- 9242f693 (7 files, RTC fix, no setting): pure header src/rtc_offset.h; call sites src/esp32/esp32_main.cpp, src/nrf52/nrf52_main.cpp, src/loop_functions.cpp; test/test_rtc_offset/; platformio.ini; golden json.
- bcf8f083 (17 files, integration): CLI src/command_functions.cpp (+91: `--settz` handler ~:745-775, `tzRejectReason`, D1 clear in `--utcoff` :723); core src/clock.cpp (+110) + src/clock.h (+12) + src/tz_anchor.h;
  BLE = src/command_functions.cpp:6740-6751 (SN1 frame, `nsetdoc1["TZ"]`; comment :6742 size budget, SN at 228/244 B so new keys go to SN1) - no separate ble file; pin in
  test/test_ble_phone_harness/test_ble_phone_harness.cpp (`{"SN1", 99}`); web = src/web_functions/web_setup.cpp (+32: param handler :173-195 routes via `--settz` through commandAction, getter :851) and
  src/web_functions/web_functions.cpp (+10: `_create_setup_textinput_element("tz",...)` :2848, info row :3403, htmlEscape; advisor found markup injection); other writers touched
  (src/tinyxml_functions.cpp, src/t-deck/event_functions.cpp, src/loop_functions.cpp); native tests test/test_tz_anchor/, test/test_decodetinyxml/ (+29, stub WisBlock-API.h); lint test/golden/settings_schema_lint.py (+7);
  docs/d1-04-settings-field-triage-20260912.md; platformio.ini; golden json.
- 10da4b82 + a9e64cf6 + 5899e12b (docs commits): docs/BACKLOG.md (rows + Stand), docs/test-suite-map.md, docs/settings-registers.md (register reference, German), docs/architecture/11-wire-format.md (+ .html twin, a9e64cf6),
  docs/architecture/07-verification-infrastructure.md, the wave plan (status table), tools/bench/fleet.json (host fix).
- Other templates: 8c905df4 WEB-04 (web GUI, 19 files: src/own_msg_status.{h,cpp}, web_functions.cpp, two udp_frame twins, test/test_own_msg_status + twin test, new tools/webgui_tick_test.js, docs/architecture/08-defect-catalogue.md,
  docs/RESUME.md); eda23163 EXT-03 (9 files: header-only src/extudp_target.h + test, hooks in command_functions.cpp/extudp_functions.cpp, platformio.ini, golden json, BACKLOG row, RESUME, campaign doc).
- Pattern: (1) pure header/cpp carve-out + native env + golden json, (2) schema row, (3) CLI `--cmd`, (4) web via commandAction, (5) BLE JSON key (SN1 first), (6) twin-test pin, (7) docs commit last.
  Commit-message prefix: `<area>(<ID>): ...`. Settings: NO FLASH_STRUCT_VERSION bump (keyed stores both platforms: nRF52 settings_store_nrf52.cpp:351, ESP32 schema walk esp32_flash.cpp:388); wave plan :11-12.

### 5. Upstream PR rule

- CLAUDE.md "PR Workflow" + docs/BACKLOG.md 2.7 :220-229: branch `pr/<topic>` from `upstream/dev`, take only firmware files from fork-dev (src/, variants/, platformio.ini hunks that are not native envs), squash to ONE commit;
  no docs, tools, tests, debug code in a PR (debug goes into src/instrument._, src/test_inject._, src/t-deck/tdeck_debug.* with one-line hooks). German description: what changed (files, functions, logic), why, tested-on, reviewer notes.
- Stays fork-only: test/ (native envs, suites, golden), tools/, docs/, .github, `[env:native*]` sections of platformio.ini, comments referencing docs/test paths.
- Four couplings to cut when building the branch (memory firmware-only-pr-coupling): native envs in platformio.ini, `pre:tools/...py` extra_scripts, `-Werror` policy flags, `--port "$UPLOAD_PORT"` in variants.
  `git checkout fork-dev -- src` copies whole files and can revert un-synced upstream commits: after checkout grep the staged diff for identifiers of the upstream delta (memory pr-branch-checkout-reverts-upstream).
- .claude/commands/submit-pr.md is RETIRED (banner :8-13: describes fork-main flow; ask the operator first). German structure it prescribes: `## Was wurde geaendert` / `## Warum` / `## Getestet` / `## Hinweise fuer den Reviewer`,
  ASCII umlauts (ae/oe/ue/ss), title <= 70 chars conventional-commit. Worked example: docs/pr-draft-n36-20261003.md (Titelvorschlag, Kurzfassung with file/line counts, Problem, Reproduktion).
- Consequence for the paper: each feature wave plan should mark which files are "PR slice" (src/, variants/) and which are "fork-only" (test/, tools/, docs/, native envs). Upstream merge is by Kurt (DK5EN never self-merges).
  The TZ campaign decided "no upstream PR" (operator); decide per feature. Safeboot has its own upstream-PR row SB-07 (src/safeboot/ only).

### 6. Bench constraints

- tools/bench/fleet.json (SoT; CLAUDE.md table hosts are stale): heltec-1 DK5EN-1 Heltec V3 (esp32-S3) usb 0001 host .68.71 | t-deck-14 DK5EN-14 T-Deck Plus (S3) usb MAC E0:72:A1:AD:65:E0 host .68.70 |
  t-beam-92 DK5EN-92 ttgo_tbeam (classic ESP32, SX127x) usb 573C000584 host .68.75 | rak-90 DK5EN-90 RAK4631 (nRF52840, Ethernet W5100S, no WiFi) usb 230D6EBB3266D20E host .68.77. max_txpower_dbm 2.
  docs/RESUME.md:49-59 lists DK5EN-1 at .76 (conflict; 5899e12b set .71 in fleet.json, trust fleet.json). taken_ssids block pins callsign SSIDs (1, 14, 90, 92, 93, 98 etc.; 11-26, 63 are smokeping/HAMnet probes).
- Capabilities: WiFi = Heltec V3, T-Deck, T-Beam (web GUI, OTA, net console 2323, NTP); Ethernet = RAK-90 only (web at IP, no mDNS, no OTA path: DFU/UF2 only); BLE = all four boards
  (host tools use bleak via uv, tools/bench/ble_{cycle,stress,golden}.py, pong_ble_check.py; none is a stage-3 step; BLE needs a Mac with Bluetooth, node name `MC-<id>-<call>`); store-node capable (ENABLE_MSGSTORE,
  src/configuration_global.h:319, S3 + RAK4631 only): Heltec V3, T-Deck, RAK-90 yes, T-Beam no. No RTC chip on any bench node. Deep sleep test only on Heltec V3 (CP2102 DTR = PRG button).
  Only 2 gateway-capable WiFi path nodes; no bench node as real gateway vs central server today (BL-07 note, snf-gateway concept doc).
- bench_suite.py stage-3 case definition: tools/bench/bench_suite.py `@dataclass Step(name,node,argv,timeout_s,gate,env)` :73-80; ordered in `plan()` :189-281 per attached node:
  identity (gate) -> [flash steps] -> prepare (gate, prepare_node.py) -> per-board harness (tdeck_harness.py / rak_harness.py / oled_harness.py, `--scenario all`) -> wait_http + ota_regression.py
  -> wait + webgui_badge_test.js -> [deepsleep] ; final `mesh_exchange.py`. Opt-in flags --flash --extudp --deepsleep --soak-seconds (:405-423). Scenario registry: `SCENARIOS = {...}` in rak_harness.py:874
  (functions `scenario_<name>(session, args)`), same pattern in tdeck_harness.py (campaign adds scenarios there: map_persist, msg_ack). A new bench case = new `scenario_*` in the board harness (or a new tools/bench/<x>.py
  with `--port --node --out`) + a Step in plan() + pytest in tools/bench/test_bench_suite.py (the plan() test) and test_<x>.py. Timeouts constants :65-70. Results: <out>/bench-summary.json.
- Identity guard: tools/bench/identity_guard.py (docstring :2-30): refuses XX0XXX/empty callsign, callsign != fleet.json entry, TX power > max_txpower_dbm (2), ESP32 without WiFi IP/clock/web server;
  library `require(info_text, node)`, CLI `--node --host|--port|--info-file`, exit 0/1/2. Runs as gate step in stage 3 and inside ota_regression.py.
- Hard rules (CLAUDE.md "Bench Rules (hard)"): test traffic only to group TEST (or 9/9999 per memory) or DM to an own node, never broadcast; never a foreign callsign (OE1XAR, XX0XXX); TX <= 2 dBm;
  identity_guard before any test; `lsof <port>` before opening, no port while serial_capture.py holds it; opening a port reboots every ESP32; RAK needs DTR; resolve ports by USB serial; one pio at a time;
  never bare `pio test`; flashing only after operator go (TZ bench: "flashed after the operator's go", wave plan :150). BLE Put via 0xA0 needs single colon `:{9}text` (memory ble-message-form-broadcast-incident).
  Bench beacons from these nodes show up as neighbours in the DK5EN-98 soak (campaign-tdeck-w02 :38-39). Hub can drop USB (tdeck-w02 :102-105).
- Consequence per feature: auto update = ota_regression.py / ota_abort.py / tools/webflash.py exist (ESP32 only, single ota_0 slot SB-05, no rollback); RAK has no OTA (DFU only, `rak-dfu` memory);
  HMAC-over-LoRa and S&F-from-server need >= 2 own nodes + RF at 2 dBm in group TEST or DM only; S&F-from-server needs a gateway node and the central server, neither is in stage 3 today (a host-side mock is: tools/mock UDP 1990);
  MTU = RAK (W5100S, node_ethmtu exists) and/or BLE MTU on any node; BLE reconnect = bleak host tools on any node.

### 7. Build envs (platformio.ini `[platformio] default_envs` :16-; board envs live in variants/*/platformio.ini, 31 dirs; no ESP32-C3 env exists, grep clean)

Families: `[esp32_classic]` :1196, `[esp32_s3]` :1192, `[nrf52_base]` :104. WiFi+BLE(NimBLE) on every ESP32 env; nRF52 = BLE only (bluefruit), no WiFi; Ethernet: RAK4631 via RAK13800 W5100S, T-ETH-ELITE_1262 (S3, BOARD_T_ETH_ELITE).

- ESP32 classic (extends esp32_classic): E22_1262-DevKitC, E22-DevKitC, E22_XML-DevKitC, esp32-loraprs-e22, esp32-loraprs-ra01, heltec_wifi_lora_32_V2, ttgo_tbeam, ttgo_tbeam_SX1262, ttgo_tbeam_SX1268, ttgo-lora32-v21.
  Tight IRAM/DRAM (E22_XML iram 4028 B headroom, MEM-04); no ENABLE_MSGSTORE (src/configuration_global.h:~296-319: classic gets small rings).
- ESP32-S3 (extends esp32_s3): E22_1262_S3-DevKitC-1-N16R8, E22_1268_S3-DevKitC-1-N16R8, heltec_wifi_lora_32_V3, _V4, heltec_wireless_stick, heltec_wireless_tracker, LilyGo_T_Connect_Pro, LilyGo_T-Beam-1W, LilyGo_T3_S3_V1_3,
  t_deck, t_deck_plus, ttgo_tbeam_supreme, T-ETH-ELITE_1262 (Ethernet); standalone (no `extends = esp32_*`, own build_src_flags): t_deck_pro, t5_epaper, vision-master-e213 (+ -e290 via extends), wireless-paper;
  esp32-external-radio (extends t_deck_pro, platformio.ini:1323, opt-in).
- nRF52840 (extends nrf52_base): wiscore_rak4631 (flash 96.1 % as shipped, tight; -Ofast is the lever), heltec_t114, t_echo.
- Safeboot: `[env:esp32-safeboot]` :1200 (board esp32dev, classic, `-D MC_SAFEBOOT=1`, partitions-4MB-safeboot.csv) and `[env:esp32-S3-safeboot]` :1240 (board heltec_wifi_lora_32_V3); both in default_envs;
  host env `[env:native_safeboot]` :1289 (test_safeboot_state). Single app slot ota_0 (SB-05). Build safeboot BEFORE the board env (memory safeboot-bin-rebuild-before-v3-flash). Contract: docs/safeboot-ota-contract.md.
- Not default envs: native*, esp32-external-radio. Platform scope sentence for the paper should name: classic ESP32 / S3 / nRF52 (RAK4631, T114, T-Echo) and the standalone display boards.

## Template skeleton for a per-feature wave plan (derived from ntp-tz-rtc-wave-plan.md + dm-stage3 + sweep)

```
# <Feature> (<ID>): wave plan
Date / Base: fork-dev at <sha> / Concept: <doc> / Backlog rows: <IDs>
Status: <OPEN | W1 done | DONE date>. Upstream PR: <yes/no, operator decision>
## BLUF                      (waves, headline decisions, what is explicitly NOT done, upstream slice)
## Platform scope            (table Board family | WiFi | Eth | BLE | in scope? | bench node)
## Decisions                 (table ID Dn | Question | Default/Decided)       -- no re-opening after dispatch
## Recon corrections         (table Item | Concept says | Code says at sha:line)
## Interfaces fixed up front (header names, function signatures, setting key, command name, BLE register/key, byte budget)
## Waves                     (preamble: writers = implementer/Sonnet, orchestrator hotspots = platformio.ini, golden json, BACKLOG.md, RESUME.md, wave plan file)
### W0 recon (scouts, read-only)  [optional]
### W1 <title> (n writers, parallel, disjoint)
| Owner | Files (exact, "new" marker) | Verification (scoped, no pio unless pio slot owner) |
- Behaviour/spec bullets; regression proof RED->GREEN where it is a bug fix
- Agent verification: ordered commands (pio test -e native_<x> -f test_<x>; one board build at a time)
- Artifact check: strings firmware.elf | grep -c <token> on <envs>; ineligible env must show 0
- Gate: stage 1+2, builds <envs> (flash % vs baseline for RAK), jsdom if web JS, advisor pass yes/no, commit `<area>(<ID>): ...`
### Wn integration / Wlast gate + bench + docs (orchestrator, serialized)
- identity_guard, per-node bench cases (expected output), what is host-only and declared so in the commit
- Docs commit: BACKLOG rows + Stand + 5.x index, test-suite-map row, settings-registers.md, 11-wire-format.md(+html), RESUME.md, prettier
## Upstream slice            (src/ files that go into the PR, fork-only list, 4 couplings to cut)
## Status                    (table Wave | State | Commit; deviations paragraph; bench result table Check | node A | node B)
```

Resume point to add (not present in templates): a `Next:` line under Status naming the next wave, its owners and the hotspot edits the orchestrator must apply before dispatch.

## Taken ID prefixes (docs/BACKLOG.md + archive journal + docs/*.md token scan; count = occurrences)

Heavily used: TM(725) N(382) TD(304) GPS(291) BP(277) DR(209) D1..D6(per-doc) MEM(144) T(123) CONC(75) DM(70) ETH(66) TOOL(65) SL(63) NET(61) INS(61) CS(61) DRY(60) SEC(57) RAK(57) HL(52) TLM(48) BUG(48) CQ(47)
MH(46) WEB(44) RF(44) RACE(41) OPT(41) GW(41) EXT(41) UDP(39) UP(38) RX(38) CHR(38) REG(34) ALT(33) BAT(32) APRS(30) SIMP(29) RTC(28) FL(27) DS(27) DOC(27) CTY(26) PT(25) BUF(24) BL(23) WF(22) NC(21) CONF(21) CDC(21)
BND(21) WLNK(20) TX(19) STAB(19) JSN(18) BOF(18) SHA(16) GRD(16) TZ(15) WQ(13) PN(13) NTP(13) H6(13) DISP(13) PM(12) E22(12) COMP(12) ISR(11) ACK(11) CFG(10) STATE(9) SPI(9) PRES(9) TRK(8) TIME(8) PTR(8) ADC(8)
SB(6) T5(6) STK(6) ADR(6) BLD(5) BATT(4) GLD(7) CRC(7) MC(7) RC(3; docs/code-review.md) TS(3) IDF(3) NCNT NCON MHD MESH PING INFO HTTP DEF-(DEF-ARD, DEF-HN) HASH-TAG MC5-TOPO STRUCT W0.x OPT-D/E/W OLED-FLAKY SOAK-440A SNF-GW ADR-IMP ADR-TOTP
Also taken as wave/stage labels: W1..W11, S1..S4, S3-0..S3-2, A1/B1/C1/orchestrator owner letters, D1..D6 decision ids (reused per paper, not global), T-3.1..T-3.9 (DM bench cases).
Checked FREE (0 hits in docs/_.md + docs/archive/_.md): AU, MTU, RM, BLE, SNF (only SNF-GW exists), HMAC, OTA, UPD, AUTH, FWU. "HM" unchecked. Suggest: AU-nn (auto update), MTU-nn, RM-nn (remote management)
or HMAC-nn, SNFGW-nn / SNF-GW-nn (extend the existing SNF-GW row to SNF-GW-01..; keep SNF-GW as parent), BLE-nn. Caution: `BLE` alone is free as id prefix but "BLE" appears as a word everywhere; use `BLC-nn`
if a distinct token is wanted (BLC not checked, run a grep first). Re-grep `grep -rnE '\bAU-[0-9]' docs src tools test` before registering.
Re-grep result: `MTU-` only hits the prose "MTU-3" (BLE notify payload = MTU minus 3; docs/archive/meshcom5-topologie/evidence/verify-V4.md:75, src/ble_phone_drain.h:23,67,
src/command_functions.cpp:6324). No `AU-n`, `RM-n`, `BLC-n`, `HM-n` ids found. `MTU-nn` is formally free but reads like "MTU-3"; prefer `NMTU-nn` or `MTUS-nn`.
Git tree check at end of scan: only untracked tools/bench/runs/* dirs (other session's bench output); no file of mine in the repo.
