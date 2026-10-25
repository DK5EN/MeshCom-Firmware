# Campaign: T-Deck Plus heap loss and WiFi starvation (TD-20, TD-21)

Started 2026-10-25. Node DK5EN-14 (T-Deck Plus, bench, `dk5en-14.local`, USB attached). Problem statement:
`~/Desktop/RCA-DK5EN-14-heap-wifi-20261024.md` (24 h meshlogger run, WiFi/console lost twice with free
internal heap below 30 KB). Operator scope: measure, reproduce, RCA, fix, re-test. No PR, no release.

## Defects

| ID    | Defect                                                                                                                                                         | State       |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| TD-20 | `persisted_msgs` (`src/t-deck/lv_obj_functions.cpp:195`) keeps a RAM-only copy of every non-System message until reboot (cap 1000, ~220 B internal RAM each) | Open        |
| TD-21 | `checkWifiPing()` (`src/udp_functions.cpp:901`) returns early on `WL_CONNECTED`; a starved IP stack on an associated link is never restarted (47 min outage)   | Open        |
| P-1   | Ping RTT 5-300 ms on the LAN; candidates: WiFi modem sleep (default `WIFI_PS_MIN_MODEM`, never set), low heap, console stream load                            | Measurement |

## Waves

| Wave | Content                                                                                                                       | Owner                  | State   |
| ---- | ----------------------------------------------------------------------------------------------------------------------------- | ---------------------- | ------- |
| 0    | Recon: injectmsg path, alloc-fail slot, WiFi PS/coex, watchdog, overnight log                                                 | 3 scouts               | Done    |
| 1    | INSTRUMENT: periodic `[INSTR-HEAP]` line, alloc-fail capture, `--wifips`                                                      | implementer            | Running |
| 2    | Bench: OTA INSTRUMENT image, USB capture held, `--injectmsg` drain until WiFi loss, ping A/B                                  | orchestrator           | Open    |
| 3    | Fix TD-20 + TD-21 (+ PS if P-1 confirms), host regression tests, largest internal block in STAT                               | implementer + advisor  | Open    |
| 4    | Bench re-test (same drain + ping), flash release build of the fix, docs                                                       | orchestrator           | Open    |

## Wave 0 findings

- `--injectmsg` is INSTRUMENT-only; it reaches `msg_tabs_add_message()` with no dedup and lands on a tab named after
  the source call. One injection per main-loop flush (`[INJECT];err;queue full` otherwise).
- The heap failed-alloc callback has one global slot; `fw_update_net.cpp` arms it per update job only.
- WiFi PS is never set (driver default MIN_MODEM). No runtime BLE stop on S3 (deinit crashes, see AU-20).
- `checkWifiPing()` every 30 s, only acts while not `WL_CONNECTED` (grace 180 s reconnect, 360 s reset). No
  "last successful IP traffic" stamp exists.
- STAT format is pinned by `test/test_setlog_lines/test_main.cpp:203-250`; parser `tools/soakstatus.py:108-115`.
- Overnight 2026-10-24 23:49 to 10-25 08:09 (43 messages): internal heap 67.1 KB -> 51.6 KB, no WiFi loss.
