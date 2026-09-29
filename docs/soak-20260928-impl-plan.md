# Soak 2026-09-28 fixes -- implementation plan

Source: `docs/soak-20260928-verdict.md`. Operator decisions 2026-09-29: fix F1-F8 (no upstream
PR this round); gateway flag = "15-min refresh + expiry"; HatF moves to the safe side;
afterwards OTA DK5EN-98 only and soak until 2026-10-01 16:00.

## Wave status log

| Wave | Content                                                                           | Status                                                                                               |
| ---- | --------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| 1    | four parallel writers A/B/C/D (below)                                             | done                                                                                                 |
| 1g   | orchestrator hotspot edits in `src/lora_functions.cpp`, neo path lists, full gate | paused: gate run 1 green (46/46 native, 1464 tests, 4 boards) BEFORE the 1g additions; re-run needed |
| 1a   | advisor pass (fable) on the whole diff                                            | APPROVED (fable, code); must-do = re-run the board builds; 4 optional items below                    |
| 1c   | commits per finding group on fork-neo-test                                        | committed 2026-09-29 on the operator's call, final tree unbuilt (4 commits)                          |
| 2    | OTA DK5EN-98, `--info`, rpizero logger                                            | running: OTA 2026-09-29 10:07 (with the BLE/battery campaign), capture until 10-02 10:10             |

## Wave 1 ownership

| Writer | Findings                                 | Exclusive files                                                                                                                                                                                                                                                                                         |
| ------ | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A      | F1 DM ack/rtt, F4 att=, F2 site 3115     | `src/dm_stats.{h,cpp}`, `src/lora_functions.cpp` (only the F1/F4/F2 hunks), `src/esp32/udp_frame_esp32.cpp`, `src/nrf52/udp_frame_nrf52.cpp`, `test/test_dm_stats/`, `test/test_udp_frame_twin/`                                                                                                        |
| B      | F5 HatF share, F6 receiver, F7 helper    | `src/nbr_matrix.{h,cpp}`, `test/test_nbr_matrix/`, `test/test_nbr_replay/`, `test/test_nbr_views/`, `test/test_nbr_report/`, `docs/nbr-logformat.md`, `docs/meshcom5-topologie/`                                                                                                                        |
| C      | F2 other sites, F3 breadcrumb, F6 sender | `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/esp32/gateway_service_esp32.cpp`, `src/esp32/loop_actions_esp32.cpp`, `src/loop_functions.{cpp,h}`, `src/command_functions.cpp`, `src/web_functions/web_functions.cpp`, new breadcrumb/HEY-policy files, their new tests, `platformio.ini` |
| D      | F8 host tools                            | `tools/soakstatus.py`, `tools/nbrlog.py`, `tools/nbrsnap.py`, `tools/nbrhopcheck.py`, `test/test_nbrlog/`, `tools/tests/`                                                                                                                                                                               |

Orchestrator-owned (applied at 1g): `src/lora_functions.cpp` call sites for F6 (tri-state
gateway argument at the two `nbrNoteFrame` calls, own-row GW clear at the two
`nbrRowSetFlag(..., NBR_FLAG_GW)` sites) and F7 (`nbrCopyMask` in the cancel scan);
`tools/neo/paths/*.txt` for every new file.

## Gateway flag design (F6)

- Receiver: a HEY to "HG" sets `NBR_FLAG_GW` on the originator's row, a HEY to "H" clears it,
  anything else (HN report, text, POS) leaves it unchanged. The flag lapses 45 min
  (`NBR_GW_HOLD_MIN`, 3 x `TRICKLE_IMAX_S`) after the last HG. Row 0 follows the node's own
  `bGATEWAY` and never lapses. Every change is logged as `[NBR]|GW|...`.
- Sender: a gateway does not trickle-suppress a HEY when its last own HEY is at least
  `TRICKLE_IMAX_S` old, so at least one HG goes out every 15 min. `--gateway on/off` (serial,
  BLE, web) resets the trickle interval and sends a HEY at once.

## Open decisions (2026-09-29)

- None open. nRF52 DL internet server applied on the operator's go: `src/nrf52/nrf_eth.cpp`
  `startUDP()` and `startFIXUDP()` map `node_gwsrv == "DL"` (non-HAMNET) to
  `IPAddress(192, 68, 17, 26)` (A record of `meshcom.hamnet.network` on 2026-09-29), NTP
  162.159.200.1 like OE/IT. Not built yet (gate paused).

## Added during 1g

- `--info` prints `...BOOT RESET_REASON=<n> <name>[ LAST_LOOP_SECTION=<s> entered_ms=<n>]` on
  ESP32, so a net-console reader (rpizero logger) sees why the node last rebooted.
- Port of upstream v4.35v (`aac2e59a`, applied by the operator): `--gateway srv DL` without
  HAMNET uses `meshcom.hamnet.network`; HAMNET NTP falls back to 44.143.0.9 outside 44.148.
  Version letter `SOURCE_VERSION_SUB`/`SOURCE_VERSION_WEB_SUB` = "v". The nRF52 Ethernet path
  (`nrf_eth.cpp`, fixed IPs) got the DL internet entry as well (upstream lacks it; see Open
  decisions).
- nbrLoad() clears restored foreign gateway flags (no hold timer in the save image).

## Advisor optional items (not applied, operator to decide)

- F3 false positive: a deliberate reboot inside a section (`--reboot` via BLE or web,
  `command_functions.cpp` ~913/~988; config import `web_functions.cpp` ~539) leaves the crumb, so
  the next boot prints `LAST_LOOP_SECTION=web|ble_cmd` next to `RESET_REASON=3 SW`. Fix:
  `loopCrumbClear()` before those `ESP.restart()` calls.
- Pre-existing: a `:rej` reply for an own DM counts as `ack=`/RTT (`lora_functions.cpp` ~1632,
  `udp_frame_esp32.cpp` ~330, nRF52 twin) -- it enters the ACK branch.
- `--wifiap on` clears `bGATEWAY` without `heyGatewayChanged()`; harmless (AP mode is never a
  gateway, neighbours lapse after 45 min).
- Ruff on the tools: same rule classes as HEAD, counts equal or lower.

## Gate (1g)

All `env:native*` suites (one `pio` process at a time), then clean sequential builds of
`heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `ttgo_tbeam`, `t_deck_plus`; ruff on touched
Python; `prettier --check` on touched docs; string scan of the V3 image for the new
breadcrumb and `[NBR]|GW` markers.
