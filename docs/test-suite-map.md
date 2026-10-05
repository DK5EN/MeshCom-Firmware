# Test suite map

Inventory of every automated test in this fork, by category and by gate, as
of 2026-10-04. It is the reference behind `tools/regression.sh` (the
end-to-end regression, three stages) and the `/full-regression` skill. Counts
are from the tree at that date; `tools/regression.sh` prints the live numbers
on every run and those win over anything written here.

## 1. Categories and counts

### Unity suites under `test/` (host, `pio test -e native*`)

115 suites with tests, 1877 `RUN_TEST` cases, 60 native environments. Five
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
`tools/tests/test_make_zz.py` (11 cases, release `.bin.zz` assets).

The per-suite sums were recomputed from the tree on 2026-10-04 (every
`RUN_TEST` under `test/test_*`). The totals before that date were stale: the
old header said 96 suites / 1563 cases, while the table below already listed
97 suites / 1577 cases.

### Everything else

| Category                                                                                            | Size                                                            | Runs in                                                            |
| --------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- | ------------------------------------------------------------------ |
| Golden lints (static gates on `src/`, e.g. toggle table, help parity, settings schema, variant ini) | 17 lint scripts, 115 self-test checks                           | `test/golden/selftest.sh` (stage 1)                                |
| Mock server suite (UDP 1990 protocol)                                                               | 115 unittest cases in `tools/mock`                              | `selftest.sh` and the stage 2 pytest                               |
| Bench tool suites (parsers and logic of the bench scripts)                                          | 267 pytest cases in `tools/bench/test_*.py`                     | stage 2                                                            |
| Pages/flasher                                                                                       | 28 pytest + 17 node cases                                       | stage 2, release step 5                                            |
| Web GUI JS                                                                                          | 7 node cases (charsleft), `safeboot_page_test.js` (jsdom, disk) | stage 2; `webgui_badge_test.js` needs a live node and is not gated |
| NBR log tools                                                                                       | 6 PEP-723 scripts in `test/test_nbrlog`                         | stage 2                                                            |
| Tool self-tests (`--self-test`)                                                                     | 36 scripts; 32 inside `selftest.sh`, 4 in stage 2               | stages 1 and 2                                                     |
| Hardware bench regressions                                                                          | harnesses per board, OTA (TM-40/49), EXTUDP (TM-43), DS-03      | stage 3 (`tools/bench/bench_suite.py`)                             |
| Soak and field runs (24 h+ captures, `soakstatus.py`)                                               | no counter                                                      | manual, see `docs/automation-runner-runbook.md`                    |

## 2. Gates

| Gate                                 | What it runs                                                                                              |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------- |
| CI `ci-build.yml`                    | `pio test -e native` and `native_aprs` only (2 of 51 envs), board builds, resource delta                  |
| Release (`/release-firmware` step 2) | every `[env:native*]` sequentially plus `selftest.sh`; step 5 adds the pages/flasher tests                |
| `tools/regression.sh` stage 1        | same as the release gate; also fetches the topo_shadow raw window from rpizero when absent                |
| `tools/regression.sh` stage 2        | pytest over tools/, nbrlog scripts, node tests, jsdom page test, the four stray self-tests                |
| `tools/regression.sh` stage 3        | bench fleet over USB: identity guard, per-board harness, OTA; `--flash`, `--extudp`, `--deepsleep` opt-in |

Stages run strictly one after another. `pio` must never run twice at once
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

| Suite                        | Category      | Cases | Env(s)                                                       |
| ---------------------------- | ------------- | ----: | ------------------------------------------------------------ |
| test_ack_phone_frame         | Unit          |    13 | native                                                       |
| test_ack_replay              | Oracle/Replay |     1 | native                                                       |
| test_ack_validate            | Unit          |    17 | native                                                       |
| test_alt_fusion              | Unit          |     7 | native                                                       |
| test_aprs_corpus             | Oracle/Replay |     2 | native_aprs                                                  |
| test_aprs_decode             | Oracle/Replay |    13 | native_aprs                                                  |
| test_aprs_epilogue           | Contract      |     8 | native_aprs                                                  |
| test_aprs_fuzz               | Fuzz          |     4 | native_aprs_fuzz                                             |
| test_aprs_reencode           | Oracle/Replay |     1 | native_aprs_fuzz                                             |
| test_aprs_spec               | Contract      |     8 | native_aprs                                                  |
| test_backpressure            | Unit          |    39 | native                                                       |
| test_batt_detect             | Regression    |    31 | native_batt_detect                                           |
| test_batt_pipeline           | Unit          |    67 | native_batt_pipeline                                         |
| test_beacon_rate             | Regression    |     9 | native                                                       |
| test_ble_json_frame          | Regression    |     8 | native                                                       |
| test_ble_phone_frame         | Contract      |     8 | native_ble_phone_frame                                       |
| test_ble_session             | Regression    |    14 | native_ble_session                                           |
| test_ble_phone_harness       | Integration   |    25 | native_ble_phone_harness                                     |
| test_ble_settings_v1         | Contract      |    10 | native_ble_settings_v1                                       |
| test_bp_echo_guard           | Regression    |    10 | native                                                       |
| test_bp_notice_frame         | Contract      |    18 | native_aprs                                                  |
| test_bp_regression           | Regression    |     4 | native_aprs                                                  |
| test_byte_fifo               | Unit          |    19 | native_byte_fifo                                             |
| test_capture_ring            | Unit          |    10 | native_capture                                               |
| test_charset_filter          | Unit          |    29 | native                                                       |
| test_checkvia                | Unit          |    13 | native_parsers                                               |
| test_command_match           | Unit          |    11 | native_command_match                                         |
| test_command_setters         | Unit          |    20 | native_command_setters                                       |
| test_command_toggles         | Unit          |    33 | native_command_toggles                                       |
| test_compress                | Unit          |    12 | native                                                       |
| test_conf_frame              | Unit          |    12 | native_conf_frame                                            |
| test_config_json             | Contract      |    19 | native_config                                                |
| test_country_twin            | Twin          |     5 | native_country_esp32, native_country_nrf52                   |
| test_csma_timing             | Unit          |    10 | native                                                       |
| test_decodeaprspos           | Unit          |    22 | native_parsers                                               |
| test_decodetinyxml           | Unit          |    14 | native_xml                                                   |
| test_dedup_replay            | Oracle/Replay |     2 | native_dedup                                                 |
| test_dm_dedup                | Unit          |    12 | native                                                       |
| test_dm_stats                | Unit          |    13 | native                                                       |
| test_dm_text_escape          | Unit          |    11 | native                                                       |
| test_drift_dr16_gates        | Regression    |     7 | native                                                       |
| test_esp32_flash_lifecycle   | Regression    |    13 | native_esp32_flash_lifecycle                                 |
| test_esp32_settings_nvs      | Integration   |     6 | native_esp32_settings_nvs                                    |
| test_extern_msg_json         | Contract      |     6 | native                                                       |
| test_extern_notice_json      | Contract      |     7 | native                                                       |
| test_extern_tele_json        | Contract      |     8 | native                                                       |
| test_external_radio_protocol | Unit          |    66 | native_extradio                                              |
| test_external_radio_tcp      | Unit          |    37 | native_extradio                                              |
| test_external_radio_txq      | Unit          |    23 | native_extradio                                              |
| test_extudp_target           | Unit          |     7 | native                                                       |
| test_gateway_service_twin    | Twin          |    12 | native_gateway_twin                                          |
| test_getextern               | Unit          |    32 | native_extern                                                |
| test_gps_filter              | Regression    |    17 | native                                                       |
| test_gwflood_frames          | Contract      |     6 | native_aprs                                                  |
| test_hey_policy              | Unit          |     7 | native                                                       |
| test_hey_report              | Unit          |     8 | native_aprs                                                  |
| test_kbd_repeat              | Unit          |    39 | native                                                       |
| test_kiss_ax25               | Unit          |    23 | native_extradio                                              |
| test_fw_update               | Unit          |    62 | native_fw_update                                             |
| test_fw_update_assets        | Unit          |    22 | native_fw_update_assets                                      |
| test_fw_update_http          | Unit          |    13 | native_fw_update_http                                        |
| test_hmac_sha256             | Unit          |    20 | native_hmac_sha256                                           |
| test_kiss_frame              | Unit          |    18 | native_kiss_frame                                            |
| test_loop_breadcrumb         | Unit          |    11 | native                                                       |
| test_loop_scheduler          | Twin          |    26 | native_loop_scheduler                                        |
| test_lora_aprs_encode        | Regression    |     4 | native_aprs_fuzz                                             |
| test_mask_secret             | Unit          |     6 | native                                                       |
| test_maxhop                  | Unit          |     7 | native                                                       |
| test_mc_text                 | Unit          |    21 | native_mc_text                                               |
| test_mcp17_bits              | Unit          |     8 | native                                                       |
| test_mh_phone                | Unit          |    12 | native_mh_phone                                              |
| test_millis_rollover         | Regression    |     4 | native                                                       |
| test_msgid_counter           | Unit          |     6 | native                                                       |
| test_msgstore                | Unit          |    60 | native                                                       |
| test_msgstore_hook           | Unit          |     6 | native_msgstore_hook                                         |
| test_nbr_matrix              | Unit          |    88 | native_nbr_matrix                                            |
| test_nbr_replay              | Oracle/Replay |    15 | native_nbr_replay, native_nbr_replay64, native_nbr_replay128 |
| test_nbr_report              | Unit          |     4 | native_nbr_report                                            |
| test_nbr_views               | Unit          |    28 | native_nbr_views, native_nbr_views64                         |
| test_netif_mtu               | Unit          |     3 | native_netif_mtu                                             |
| test_nrf52_settings_paths    | Integration   |    26 | native_nrf52_settings_paths                                  |
| test_ntp_async               | Regression    |    10 | native                                                       |
| test_ntp_harvest             | Regression    |     4 | native                                                       |
| test_own_msg_status          | Regression    |    13 | native                                                       |
| test_pn_retry                | Unit          |    34 | native_pnretry                                               |
| test_pos_persist             | Unit          |     9 | native                                                       |
| test_pos_tag_nan             | Regression    |     7 | native_parsers                                               |
| test_printfdeb_format        | Regression    |    13 | native                                                       |
| test_radio_units             | Regression    |    14 | native                                                       |
| test_reack_limiter           | Unit          |     8 | native                                                       |
| test_regex_call              | Unit          |    13 | native                                                       |
| test_remote_cmd              | Unit          |    32 | native_remote_cmd                                            |
| test_rm_nodes_store          | Unit          |    23 | native_rm_nodes_store                                        |
| test_rm_sender_policy        | Unit          |    24 | native_rm_sender_policy                                      |
| test_rm_web_parse            | Unit          |     8 | native_rm_web_parse                                          |
| test_web_guard               | Unit          |    28 | native_web_guard                                             |
| test_rtc_offset              | Regression    |     8 | native_rtc_offset                                            |
| test_safeboot_state          | Unit          |    17 | native_safeboot                                              |
| test_safeboot_ver            | Unit          |    16 | native_safeboot                                              |
| test_serial_command_twin     | Twin          |     6 | native_serial_esp32, native_serial_nrf52                     |
| test_setlog_lines            | Unit          |    29 | native                                                       |
| test_settings_members        | Contract      |     5 | native_settings_members_esp32, native_settings_members_nrf52 |
| test_settings_roundtrip      | Contract      |     9 | native_settings_roundtrip                                    |
| test_settings_sanitize       | Regression    |    15 | native                                                       |
| test_settings_store          | Unit          |    24 | native_settings_store                                        |
| test_stor_announce           | Unit          |    22 | native_stor_announce                                         |
| test_sto_notice              | Unit          |    26 | native                                                       |
| test_tft_backlight           | Regression    |     4 | native                                                       |
| test_tile_cache              | Unit          |    10 | native                                                       |
| test_topo_shadow             | Oracle/Replay |     2 | native_topo_shadow                                           |
| test_txprio_replay           | Oracle/Replay |     1 | native_aprs                                                  |
| test_txring                  | Unit          |    43 | native_aprs                                                  |
| test_txring_flood            | Regression    |    11 | native_aprs                                                  |
| test_tz_anchor               | Unit          |     8 | native_tz_anchor                                             |
| test_tz_rule                 | Unit          |    11 | native_tz_rule                                               |
| test_udp_frame_twin          | Twin          |    37 | native_udp_frame_twin                                        |
| test_udp_send_twin           | Twin          |    18 | native_udp_send_twin                                         |
| test_unconfigured            | Unit          |    14 | native_aprs                                                  |
| test_url_decode              | Regression    |    14 | native                                                       |
| test_wifi_start_gate         | Regression    |     8 | native                                                       |
