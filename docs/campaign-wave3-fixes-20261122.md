# Campaign: upstream wave 3 fixes (2026-11-22)

Implement on `fork-dev` every open upstream bug assigned to DK5EN that is not fixed yet, so the wave 3 fix PRs can go
to icssw-org `dev` by Thu 2026-11-26. Internal deadline: gated and benched by Tue 2026-11-24 evening; what misses it is
reported, not rushed. PR submission is not part of this campaign. Base: `fork-dev` `987ab0fd`.

Scope source: `gh issue list -R icssw-org/MeshCom-Firmware --assignee DK5EN --state open --label bug` (20 issues on
2026-11-21, matches the brief).

## Scope

| Issue | Backlog id(s)                       | Plan (docs/)                                | Wave | State           |
| ----- | ----------------------------------- | ------------------------------------------- | ---- | --------------- |
| #1291 | SEC-DUMP-01                         | plan-nrf52-dumpsettings-secrets-20261121.md | 1    | done `c22cb3d2` |
| #1289 | RAK-ETH-DNS-01, RAK-NETMODE-01      | plan-rak-eth-dns-netmode-20261121.md        | 1    | done `c22cb3d2` |
| #1290 | GW-GROUP-01, GW-DM-01, GW-CALL-01   | plan-gateway-routing-20261121.md            | 1    | done `c22cb3d2` |
| #1296 | WEB-TEMPOFF-01                      | plan-web-tempoffsetoutdoor-20261121.md      | 1    | done `c22cb3d2` |
| #1301 | TBEAM1W-PWR-01                      | plan-tbeam1w-txpower-20261121.md            | 1    | done `c22cb3d2` |
| #1295 | GPS-AUTOSYM-01..03, POSINFO-NEXT-01 | plan-gps-autosymbol-nextpos-20261121.md     | 2    | done `1830d8ed` |
| #1300 | OTA-SAFEBOOT-01                     | plan-ota-safeboot-gate-20261121.md          | 2    | done `1830d8ed` |
| #1302 | DHT-PROBE-01                        | plan-dht-probe-20261121.md                  | 2    | done `1830d8ed` |
| #1293 | APRS-TXT-01..03                     | plan-aprs-text-limits-20261121.md           | 3    | done `45732840` |
| #1312 | TDECK-VOLT-01                       | plan-tdeck-volt-20261121.md                 | 3    | done `45732840` |
| #1294 | DISP-PAGE-01..03, BTN-OFF-01        | plan-display-paging-20261121.md             | 4    | done `45732840` |

Out of scope, already on `fork-dev` (all ancestors of `987ab0fd`, none reverted; pin tests): #1287 ETH-06 `e8188e3c`
(`test_extudp_target`, env native); #1288 `632aa9b3` temp fix (no pin test, golden `toggle_table_lint.py` check 9);
#1292 CMD-DASH-01 `f4af55b0` (`native_command_match`); #1297 TD-LOCK-01 `7e553d12` (`native_tdeck_pattern`); #1299
AU-27 `a43dd28e` (`native_fw_update`); #1304 RM-LOSTREPLY-01 `a91d0e1d` (`native_aprs`, wave 4); #1306 wave 4; #1310
CET-SCATTER-01 `b08c5ea5`, `885fa3b3` (`native_cet_age`); #1311 wave 7.

## Wave design

Hot files: `src/command_functions.cpp`, `src/loop_functions.cpp`, `test/test_command_setters/test_command_setters.cpp`
have at most one owning writer per wave; other writers send exact patch requests that the orchestrator applies at the
gate. `platformio.ini`, `docs/BACKLOG.md`, `docs/test-suite-map.md`, `docs/RESUME.md` and this doc are orchestrator
only. New native envs are added by the orchestrator before the wave. All pio calls go through the scratchpad
`piolock.sh`.

| Wave | Writer  | Exclusive files (main)                                                                                        | Carve-outs to orchestrator            |
| ---- | ------- | ------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| 1    | SEC     | `settings_store_nrf52.*`, `settings_dump_mask.h`, `command_functions.cpp` (dump branch, help), its tests      | -                                     |
| 1    | ETH     | `nrf_eth.cpp`, `eth_dns.h`, `netmode_label.h`, `test_eth_dns`, `test_netmode_label`, 2 pytest pins            | `command_functions.cpp` --info        |
| 1    | GW      | `udp_frame_esp32.cpp`, `udp_frame_nrf52.cpp`, `gw_phone_route.h`, `regex_functions.*`, twin + regex tests     | `command_functions.cpp` D5 hints      |
| 1    | TEMPOFF | `web_setup.cpp`, `test_command_setters.cpp`                                                                   | -                                     |
| 1    | PWR     | `rf_pa.h`, `lora_setchip.cpp`, `esp32_main.cpp` boot PA, `test_rf_pa`                                         | -                                     |
| 2    | GPS     | `loop_functions.cpp`, `nrf52_main.cpp`, `command_functions.cpp`, NEXT sites, `autosymbol.h`, `posinfo_next.h` | -                                     |
| 2    | OTA     | `fw_update.h`, `test_fw_update`, `test_command_setters.cpp`, Safeboot docs                                    | `command_functions.cpp` ota branch    |
| 2    | DHT     | `onewire_functions.cpp`, `settings_sanitize.*`, `loop_functions_extern.h`, `esp32_main.cpp`, its tests        | cmd, `loop_functions.cpp`, nrf52 gate |
| 3    | APRS    | `aprs_functions.cpp`, `charset_filter.*`, `pos_text_budget.h`, `loop_functions.cpp`, `command_functions.cpp`  | -                                     |
| 3    | TDECK   | `t-deck/lv_obj_functions.cpp`, `tdeck_batt_label.h`, `test_tdeck_batt_label`                                  | -                                     |
| 4    | DISP    | `onebutton_functions.*`, `page_ring.h`, `loop_functions.cpp`, `command_functions.cpp`, display tests          | -                                     |

Gate per wave: `tools/regression.sh --stage 1,2`, board builds of every touched env (`heltec_wifi_lora_32_V3`,
`wiscore_rak4631`, `ttgo_tbeam`, `t-deck-plus` where relevant), Opus advisor on the diff, commit per wave.

Bench after the code waves: SEC-DUMP and RAK-ETH on DK5EN-90, display paging on DK5EN-1, TDECK-VOLT on DK5EN-14,
gateway routing with the local mock server, OTA-SAFEBOOT warning text. Field only (stage C): TBEAM1W-PWR above 2 dBm,
DHT with a real sensor.

## Plan drift found at HEAD (wave 0)

- Plans name envs `rak4631`/`heltec_v3`; the envs are `wiscore_rak4631`/`heltec_wifi_lora_32_V3`.
- OTA plan sections 4.2, 5.x, 6.3, 8 still describe refusal + `force`; operator D1 (b) warn and proceed wins.
- TBEAM plan section 7 (conducted steps to 22 dBm) void by operator decision.
- GPS plan section 4 says pick after the TRACK gate; operator decision (pick before) wins, so the speed calculation
  moves up.
- Gateway plan: CONF regex citations (`udp_frame_esp32.cpp:729`, nrf52 `:713`) not found at HEAD.
- TDECK-VOLT plan had no operator decisions. Operator 2026-11-21: `%.1fV` on the T-Deck header, one helper shared with
  the OLED volt display, OLED keeps two decimals. Recorded in the plan. The OLED sites sit in `loop_functions.cpp`
  (APRS owns it in wave 3): carve-out for the orchestrator.
- All plans' "fails today" is derived from code; each writer records a real before-run.

## Waves done

Wave 1 (2026-11-21, `c22cb3d2`, on top of a parallel session's docs commit `c0691390`): SEC-DUMP-01, RAK-ETH-DNS-01,
RAK-NETMODE-01, GW-GROUP-01, GW-DM-01, GW-CALL-01, WEB-TEMPOFF-01, TBEAM1W-PWR-01. Each writer recorded a real
before-run (assertion red, except pure new-header unit tests which were compile red). Gate: Unity 119 envs 4047/4047,
pytest 1639, golden selftest 48 green, builds `heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `ttgo_tbeam`,
`LilyGo_T-Beam-1W`, `heltec_t114`, `t_echo`, `t_deck_plus` (`tools/bench/runs/harness/gate-wave3fix-w1.summary`).
Opus advisor APPROVED; follow-ups done: over-long dump line test (revert-probed red), D4 ack twin pin. Gate side
effects: golden u1 replay, u1 twin-diff record and variant-ini baseline regenerated, wire doc HTML regenerated,
`test_rm_rx_gate_lint.py` accepts the new `&& bForNode`. Backlog rows moved to "Bench test and measure"
(TBEAM1W-PWR-01 next step field). Desk: link_fix refused (`no commit c22cb3d2 in firmware`: the desk only knows pushed commits); link P-0380, P-0154,
P-0163, P-0381, P-0382, P-0210, P-0385, P-0030 after the operator allows a push.

Wave 2 (2026-11-21, `1830d8ed`, on top of parallel-session commits `f65ad7b2`, `ee7a8e56`): GPS-AUTOSYM-01..03,
POSINFO-NEXT-01, OTA-SAFEBOOT-01, DHT-PROBE-01, plus a wave 1 follow-up: `t_deck_pro` (RadioLib 7.1.2) did not build
with the two-argument `setOutputPower` of `rf_pa.h`; it takes the one-argument table path again (the wave 1 gate had not
built `t_deck_pro`; every later gate does). Advisor: two rework rounds. Round 1: the hoisted TRACK speed read a stale
`posinfo_last` on RAK with TRACK off; the autosymbol got its own estimator and the TRACK path is back to its HEAD form.
Round 2: 1 s spacing let RAK jitter (3 s calls) read as bike; now 10 s, (0,0) skipped. OTA warning carries `nocap`.
Gate: Unity 119 envs 4085/4085, pytest 1639, 8 board envs (`tools/bench/runs/harness/gate-wave3fix-w2c.summary`).
Desk links pending a push (P-0383, P-0166, P-0167, P-0081, P-0209, P-0351).

Waves 3+4 (2026-11-22, `45732840`, batched by operator decision: implement everything, then one sweep):
APRS-TXT-01..03, TDECK-VOLT-01, DISP-PAGE-01..03, BTN-OFF-01. Operator decisions on the way: TDECK `%.1fV`, one helper
shared with OLED (OLED unchanged); OLED rank newest = highest (the plan's "1 = newest" line superseded). APRS D4 needed
the `--info` line as well (added). Advisor: W3 approved (three nits applied), W3 follow-ups + W4 approved. Sweep gate:
Unity 121 envs 4129/4129, pytest 1639, 13 board envs (`tools/bench/runs/harness/gate-wave3fix-w34.summary`).
Deferred to the PR cut (comment-only nits): `loop_functions.cpp` WP arrival comment "(= first click's rank)";
`charset_filter.h` `charset_wire_field` doc should name the dst clip; WP track revert rank not limited to the browse
window (pre-existing); `wpLiveNewest` extern belongs in `loop_functions.h`; `oled_harness.py` `depth` comment; PR body
line: T-Deck click with an empty ring no longer sets `pageHold`. Desk links pending a push (P-0044, P-0386, P-0179,
P-0021, P-0384, P-0059, P-0345, TDECK-VOLT-01 ticket).

Bench (2026-11-22, `45732840`, `tools/bench/runs/harness/bench-wave3fix-20261122.summary`): PASS on hardware for
SEC-DUMP-01, RAK-NETMODE-01, RAK-ETH-DNS-01, GPS-AUTOSYM-01 (DK5EN-90), WEB-TEMPOFF-01, APRS-TXT-02 notice, DISP-PAGE-01,
DISP-PAGE-02 (DK5EN-1, OLED harness on an INSTRUMENT image), GW-GROUP-01 display path (DK5EN-1 as gateway on the local
mock under `--airgap on`). Not shown: TDECK-VOLT-01 (DK5EN-14 on external power shows USB). Not benchable here: GW-DM-01
phone path, GW-CALL-01, OTA-SAFEBOOT-01 warning (needs an old Safeboot), BTN-OFF-01 (physical button), DISP-PAGE-03,
APRS on-air frames, DHT-PROBE-01, GPS-AUTOSYM-02/03, TBEAM1W-PWR-01. All nodes restored (normal images, settings
reverted, DK5EN-1 gateway off). Seven rows archived in `archive/backlog-closed-20261122.md`.

## Next

Operator: put DK5EN-14 on battery for the `--volt` header check (and confirm its `--volt` setting; it is off now); allow
a push so the desk links (P-numbers listed per wave above) can be made. Then `/submit-pr` (one PR per operator
2026-11-22; fix the deferred comment nits at the cut).
