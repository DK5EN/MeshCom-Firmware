# Test suite map

Inventory of every automated test in this fork, by category and by gate, as
of 2026-10-04. It is the reference behind `tools/regression.sh` (the
end-to-end regression, three stages) and the `/full-regression` skill. Counts
are from the tree at that date; `tools/regression.sh` prints the live numbers
on every run and those win over anything written here.

## 1. Categories and counts

### Unity suites under `test/` (host, `pio test -e native*`)

124 suites with tests, 2031 `RUN_TEST` cases, 67 native environments. Five
suites run in two or three environments (size or platform variants), which is
why the gate reports more cases than the table sums to.

| Category      | Suites | Cases | What it is                                                                                       |
| ------------- | -----: | ----: | ------------------------------------------------------------------------------------------------ |
| Unit          |     62 |  1321 | one function or class, no fixture (nbr_matrix 88, batt_pipeline 67, external_radio\_\* 126)      |
| Regression    |     23 |   242 | pins one past incident or bug id (N-08, BAT-01, BP-11, #1173, #1174, #1182, DJ8MEH log)          |
| Contract      |     12 |   112 | wire formats and schemas (BLE settings v1, EXTUDP JSON keys, settings_members, config_json)      |
| Twin          |      6 |   104 | ESP32-vs-nRF52 dumps (udp_frame, udp_send, country, serial_command, gateway_service, loop_sched) |
| Integration   |      3 |    57 | several modules on a fake platform (BLE harness on real rings, ESP32 NVS, nRF52 settings paths)  |
| Oracle/Replay |      8 |    37 | field captures replayed through the real code (aprs_corpus, dedup/ack/txprio, nbr_replay, topo)  |
| Fuzz          |      1 |     4 | aprs_fuzz against frames the radio rejected on air (crc/capture/ack corpora)                     |

The category is the header comment's own claim. Many unit suites also cite a
defect id; "Regression" is used only where the incident is the suite's sole
reason to exist.

Added with the AppLink campaign (2026-11-21, `docs/campaign-applink-20261121.md`; source unbuilt, counts are
`RUN_TEST` counts): `test_applink_ws` (RFC 6455 handshake, frame codec, SHA-1/base64), `test_applink_auth` (HMAC
login), `test_applink_conn` (connection engine on a fake io: upgrade, login, heartbeat, timeouts, output buffer),
`test_phone_link` (the BLE/NET phone-link selector). Extended: `test_ble_phone_harness` (+3 NET sink tests: mtu 0 never
counts truncated, busy keeps the frame and resends, link down never pops), `test_ble_session` (+3 `keepPhone`
session-reset tests), `test_config_json` (+3 `test_applink_*`: the persisted `node_sset2` 0x0010 bit and 0x2000
CONFIGURED marker survive normalise and the config round trip and toggle rows, a stale `--gpsdebug` bit is off),
`test_charge_led_mode` (the `node_sset2` in-use bit list gains 0x0010 and 0x2000; no new case); pytest
`tools/tests/test_applink_client.py` (12 cases, the `tools/bench/applink_client.py` bench client).

Added with the NTP/TZ/RTC campaign (2026-10-04, `docs/ntp-tz-rtc-wave-plan.md`):
`test_rtc_offset` (RTC-1..3: UTC round trip, refresh gate; Regression),
`test_tz_rule` (TZ-01 POSIX rule parser against the host `localtime_r` oracle;
Unit) and `test_tz_anchor` (TZ-01 clock re-anchor arithmetic; Unit), each in its
own env (`native_rtc_offset`, `native_tz_rule`, `native_tz_anchor`). Extended:
`test_config_json` (+2: a file without `node_tz` keeps the current value, empty
value and the 39-character limit; the round trip itself is pinned inside the
existing round-trip case), `test_decodetinyxml` (+2: station offset kept while a TZ rule is
set, applied when not) and `test_ble_phone_harness` (SN1 frame size pinned at
99 bytes, case count unchanged).

Added with TD-23 (2026-10-25, T-Deck day/night backlight): `test_daylight` (sun
elevation of `src/daylight.h` against astral/PyEphem sunrise and sunset for Munich,
Sydney, New York and Tromsoe within 3 min, polar day and night, hysteresis, the
backlight policy and the SYM+B step table; Unit, own env `native_daylight`).
Extended: `test_settings_sanitize` (+1, backlight level clamp) and
`test_esp32_settings_nvs` (round trip covers `node_bl_night`/`node_bl_auto`).

Added with the #1187-#1191 campaign (2026-10-04, `docs/concept-open-issues-20261004.md`):
`test_netif_mtu` (NMTU-01: MTU clamp of `src/esp32/netif_mtu.h` and a source pin
that the apply hooks avoid `AP_START` and precede both web servers; Unit, own
env `native_netif_mtu`). Extended: `test_command_setters` (source pin: one
`--mtu`/`--ethmtu` rung, no board guard, default 1280). `test_ble_session` (BLC-01: ESP32
reconnect-within-one-tick race against `src/ble_session.h`, disconnect classes,
counter line; Regression, own env `native_ble_session`); `tools/bench/test_ble_cycle.py`
(38 pytest cases, pure parts of `ble_cycle.py`). `test_msgstore_hook` (SNF-GW-01: store/ack decision
shared by OnRxDone and the GATE handlers, `RM1 ` exclusion, path element match;
own env `native_msgstore_hook`). Extended: `test_msgstore` (+9: lock pairing on
every entry point, no env callback under the lock, `msgstoreSameBaseCall`). `test_udp_frame_twin` (+6, SNF-GW-03/04: server PM to
a sibling SSID stored on both twins, echo guard, server `:ack` purge, PN repeat,
frames outside the mailbox incl. an unconfigured source, PM to the exact own call). `test_stor_announce` (SNF-GW W3: STOR announce set,
encoder byte-exact against the concept example, chunking, 15 min / 60 s timer;
own env `native_stor_announce`). Extended: `test_command_setters` (+2, `--stor`
rung and ladder collisions), `test_config_json` (+2, `node_stor` range and absent
key); `tools/mock/test_mock_server.py` (STOR parser, chunk assembly, expiry,
routing). `test_hmac_sha256` (RM-01: portable SHA-256 / HMAC
against FIPS 180-4 and RFC 4231) and `test_remote_cmd` (RM-02: RM1 parse, tag,
counter, allowlist, rate limit, lockout; reproduces all 21 vectors of
`tools/tests/remote_cmd_vectors.json`, the Python reference `tools/remote_cmd.py`,
pytest `tools/tests/test_remote_cmd.py`). `test_fw_update` (AU-01: tag compare with the MM.DD
half-year window, staging layout incl. the inflated bound, stage record FWS2,
policy timer, reinstall guard, millis-wrap latch) and `test_fw_update_assets`
(AU-02: asset names and the streaming release-JSON scanner on live GitHub
fixtures); `tools/tests/test_flash_map.py` (14 cases). `test_fw_update_http` (AU-04: chunked transfer
decoder of the download, split at every byte, malformed sizes, sink refusal);
`tools/tests/test_make_autoupdate.py` (13 cases, release `.autoupdate` assets incl. env name length).
`tools/tests/test_logreport_*.py` (22 cases, LOGREP-01: the `tools/logreport/` runner incl. the render-only
regression, txpool/decay, timesignal/manipulations/health on fixture logs, and the HB9/ESP32 set incl. the
ACK-aware RX counting regression).
`test_command_setters` also holds `test_dio_bootloader_blocks_safeboot` (AU-16: source pin, DIO bootloader
refuses the Safeboot handover with `bootloader_dio`; fails on the pre-fix `command_functions.cpp`).
`tools/tests/test_nimble_patch.py` (5 cases, AU-19: the build-time NimBLE `ble_hs_timer_reset()` patch of
`tools/nimble_patch.py` -- original to stop-only, idempotent, unknown or doubled text raises, path and `lib_deps` check).

Added 2026-11-21 (wave 3 fixes W1, `c22cb3d2`, campaign-wave3-fixes-20261122.md): new envs `native_eth_dns`
(`test_eth_dns`, 7 cases, RAK-ETH-DNS-01), `native_netmode_label` (`test_netmode_label`, 4, RAK-NETMODE-01) and
`native_rf_pa` (`test_rf_pa`, 3, TBEAM1W-PWR-01); pytest site pins `tools/tests/test_nrf_eth_dns_sites.py` (4) and
`tools/tests/test_info_netmode_site.py` (3). Extended: `test_nrf52_settings_paths` 32 -> 37 and `test_mask_secret`
6 -> 13 (SEC-DUMP-01), `test_udp_frame_twin` +4 (GW-GROUP-01, GW-DM-01, D2, D4), `test_regex_call` to 20
(GW-CALL-01 hint), `test_command_setters` +1 (WEB-TEMPOFF-01). Gate after the wave: Unity 119 envs, 4047 cases.

Added 2026-11-21 (wave 3 fixes W2, `1830d8ed`): `test_sset3_restore` (2), `test_autosymbol` (24) and
`test_posinfo_next` (6) in env `native` (GPS-AUTOSYM-01..03, POSINFO-NEXT-01). Extended: `test_fw_update` +2
(OTA-SAFEBOOT-01, both fw_update envs), `test_command_setters` +1 (OTA pin), `test_settings_sanitize` +5 -1
(DHT-PROBE-01), `test_rf_pa` +1 (T-Deck Pro one-arg path). Gate after the wave: Unity 119 envs, 4085 cases.

Added 2026-11-22 (wave 3 fixes W3+W4, `45732840`): `test_pos_text_budget` (9, env `native`, APRS-TXT-03), new envs
`native_batt_label` (`test_batt_label`, 6, TDECK-VOLT-01) and `native_page_ring` (`test_page_ring`, 12, DISP-PAGE-01/03).
Extended: `test_charset_filter` (+ `charset_wire_field` cases), `test_lora_aprs_encode` (+3, Track 25 bytes),
`test_display_clock` (DISP-PAGE-01..03 pins), `test_command_setters` (atxt, `--info` drop line, display off,
`--button off`). Gate after the wave: Unity 121 envs, 4129 cases.

Added 2026-11-22 (WEB-INA-01, telegram P-0391): pytest site pin `tools/tests/test_ina_web_site.py` (2): the web UI
INA226 block has no stray `<` in its labels (`vBUS<:`) and keeps the units V, mV, mA, mW.

Added 2026-11-24 (WEB-WX-01, telegram P-0394): pytest site pin `tools/tests/test_wx_web_site.py` (2): the web UI
WX table has a row for BMP390, AHT20, SHT21 and INA226 (each under its `ENABLE_` define, on and found flags), and
the temperature rows name their source (I2C, 1-Wire).

The per-suite sums were recomputed from the tree on 2026-10-04 (every
`RUN_TEST` under `test/test_*`). The totals before that date were stale: the
old header said 96 suites / 1563 cases, while the table below already listed
97 suites / 1577 cases.

Added 2026-10-10 (field report, AHT20 offset): `test_temp_offset` (5 cases,
Regression, own env `native_temp_offset`) compiles `src/aht20.cpp` and
`src/sht21.cpp` unchanged against the library stubs in
`test/support/sensor_stubs/` and pins that the AHT20 adds `node_tempi_off` and
the SHT21 adds `node_tempo_off`. The nRF52 SHTC3 read in `nrf52_main.cpp`
received the same fix but has no host test.

### Everything else

| Category                                                                                            | Size                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | Runs in                                                            |
| --------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| Golden lints (static gates on `src/`, e.g. toggle table, help parity, settings schema, variant ini) | 17 lint scripts, 115 self-test checks                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | `test/golden/selftest.sh` (stage 1)                                |
| Mock server suite (UDP 1990 protocol)                                                               | 115 unittest cases in `tools/mock`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               | `selftest.sh` and the stage 2 pytest                               |
| Bench tool suites (parsers and logic of the bench scripts)                                          | 438 pytest cases in `tools/bench/test_*.py` (2026-10-19, incl. `web_stall_client`), plus `tools/bench/gwbench/test_gwbench.py` (gateway bench toolbox, offline)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | stage 2                                                            |
| Net console client (`tools/mc_console.py`: 2323 login, `.env` lookup, clients on it)                | 16 pytest cases in `tools/tests/test_mc_console.py` (2026-10-23)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | stage 2                                                            |
| Pages/flasher                                                                                       | 28 pytest + 17 node cases                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | stage 2, release step 5                                            |
| Web GUI JS                                                                                          | 7 node cases (charsleft), `safeboot_page_test.js`, `webgui_rm_test.js` (Remote page), `webgui_settings_test.js` (Settings, Remote Admin card), `webgui_theme_test.js` (WEB-DARK-01/WEB-A11Y-01: light and dark palette, WCAG AA contrast pairs, at most 4 font sizes, nav icons on currentColor), `webgui_au_sbwhy_test.js` (AU-16: DIO-bootloader notice and disabled button, 17 checks), `webgui_login_test.js` (WEB-AUTH-01: login form, SHA-256/HMAC vs Node crypto, nonce login, 27 checks) (jsdom, disk; the jsdom and node cases read the gzip blobs of `web_assets.h` via `tools/webgui_assets.js`; `tools/tests/test_gen_web_assets.py` fails when `web_assets.h` drifts from `src/web_functions/web/`) | stage 2; `webgui_badge_test.js` needs a live node and is not gated |
| NBR log tools                                                                                       | 6 PEP-723 scripts in `test/test_nbrlog`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | stage 2                                                            |
| Tool self-tests (`--self-test`)                                                                     | 36 scripts; 32 inside `selftest.sh`, 4 in stage 2                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | stages 1 and 2                                                     |
| Hardware bench regressions                                                                          | harnesses per board, OTA (TM-40/49), EXTUDP (TM-43), DS-03                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | stage 3 (`tools/bench/bench_suite.py`)                             |
| Soak and field runs (24 h+ captures, `soakstatus.py`)                                               | no counter                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | manual, see `docs/automation-runner-runbook.md`                    |

## 2. Gates

| Gate                                 | What it runs                                                                                                                                  |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| CI `meshcom-ci.yml`                  | tags only: full `pio run`, UF2/.autoupdate packaging, release publish; no tests (`ci-build.yml` removed 2026-10-17)                           |
| Release (`/release-firmware` step 2) | every `[env:native*]` sequentially plus `selftest.sh`; step 5 adds the pages/flasher tests                                                    |
| `tools/regression.sh` stage 1        | every native env in one `pio test` call, `selftest.sh`, three nRF52 board links; fetches the topo_shadow raw window when absent               |
| `tools/regression.sh` stage 2        | ruff, pytest over tools/ (8 workers), nbrlog scripts, node tests, every jsdom page test on disk, the stray self-tests; runs alongside stage 1 |
| `tools/regression.sh --changed`      | the steps of stages 1 and 2 that the changed files can affect (`tools/regression_select.py`); gate for a wave commit                          |
| `tools/regression.sh` stage 3        | bench fleet over USB: identity guard, per-board harness, OTA; `--flash`, `--extudp`, `--deepsleep` opt-in                                     |

Counts in section 1 are stale since the 2026-11-23 review (tree: 130 envs, 215 suites, 4245 cases, 1792 pytest
cases); see `docs/review-test-gate-20261123.md` and `docs/campaign-test-gate-20261123.md` (wave 4 replaces this file
by a generated one; section 5 already is).

Every `pio` step sits in the sequential part of the run. `pio` must never run twice at once
(shared build cache), and `selftest.sh` itself shells out to
`pio project config`. Never run bare `pio test`: it walks the board envs and
flashes whatever is attached.

## 3. Inert and special files

- `test/test_invariant_TinyGsmClientSequansMonarch.h` comes from
  icssw-org/dev and is never compiled. Kept to avoid a permanent fork diff;
  not counted above. (`test/test_compress/` was inert too until the W0.6 sweep
  gave it 12 cases in env `native`; it is counted above now.)
- `test/support/parser_stubs/` (until 2026-10-03 `test/test_decodemheard/stubs/`)
  is the link-stub set for `native_parsers` and `native_topo_shadow`. The
  `test_decodemheard` suite itself went with R2-01; its stubs stayed.
- `test_topo_shadow`'s full-window case reads four raw DK5EN-98 logs
  (2026-09-21..24, 54 MB) from `~/Downloads/dk5en-98-nbr/`. They are too big
  to commit; the master copy is `rpizero:~/meshlog/dk5en-98/`. Without them
  the case is `TEST_IGNORE`d, with them it runs in ~40 s and passes.
- `TEST_IGNORE` in `test_nbr_matrix` and `test_nbr_views` sits in `#else`
  branches of size guards; every configured env takes the `#if` branch, so
  none of them fires today. `test_aprs_fuzz`'s capture-corpus guard is dead
  since `capture_corpus.txt` was committed.
- `docs/testplan/drift-matrix.csv` row DR-16 (nRF52 `bAllStarted` gating) was
  closed on 2026-10-04 as `both-valid` and is pinned by `test_drift_dr16_gates`;
  `selftest.sh` no longer runs `drift_matrix_lint.py --phase implementation`.

## 4. Review of 2026-10-03 (retire candidates)

| Candidate                                  | Verdict | Why                                                                                           |
| ------------------------------------------ | ------- | --------------------------------------------------------------------------------------------- |
| `test_compress`, TinyGsm header            | keep    | upstream files; `test_compress` has run since 2026-10-03, the TinyGsm header is inert         |
| `test_decodemheard/stubs`                  | moved   | to `test/support/parser_stubs`; the empty suite directory is gone                             |
| `test_gateway_service_twin`                | keep    | not tautological: pins DR-14/15 call parity so a "unifying" change fails a test, not a review |
| always-firing `TEST_IGNORE` (3 reported)   | 1 real  | only topo_shadow's full window; now fed from rpizero by stage 1                               |
| `drift_matrix_lint --phase implementation` | keep    | DR-03 now names `test_gateway_service_twin`; DR-16 still has no test, the flag stays for it   |
| `test_nbrlog`, bench pytest, node tests    | gated   | were in no gate at all; stage 2 runs them                                                     |

## 5. Per-suite inventory

The per-suite table is generated: [test-suite-inventory.md](test-suite-inventory.md) (`tools/suite_map.py --write`,
checked by the host gate). It lists cases, envs, the production files a suite compiles or includes, whether it reads
source files as text, and whether it runs under ASan/UBSan. The hand-kept table that stood here until 2026-11-23 was
stale (67 envs and 124 suites in its header against 130 and 215 in the tree).
