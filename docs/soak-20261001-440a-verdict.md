# Soak 2026-10-01 .. 10-02, v4.40a on DK5EN-98 -- Verdict

Evaluated 2026-10-03. Review only: nothing in `src/` was changed. Method: `tools/soakstatus.py`
(single node, `--nodes DK5EN-98 --dk1-dir none`), `tools/nbrhopcheck.py`, `tools/nbrrelay.py`, one
metric sweep over the capture (`[LOG] STAT`, `[MC-DBG]`, `[NBR]`, `[GW]` lines) and a read-only look
at the mcmap hub (gateway availability, node record). No command was sent to a node and nothing was
transmitted. Logs: `~/meshlog/soak-20261001-440a/` on the Mac (`2026-10-01.log`, `2026-10-02.log`,
`status.txt`); originals on rpizero `~/meshlog/dk5en-98/`.

## BLUF

- **Node stable, one open item on the server link.** 8.52 h, 0 resets, 0 crashes, 0 log gaps over
  120 s (longest 7.2 s), 0 logger reconnects, no heap trend (first quartile 129568, last quartile
  129588, min 125580). Uptime counter ran through: `up=30600` s.
- **The OTA came back clean.** Build `4.40a (Oct 1 2026 / 22:31:23)`, `RESET_REASON=3 SW`, same
  callsign DK5EN-98, same IP, gateway on, MESH on, 22 dBm, RX boost on, `maxv 3.5`, same SSID.
  WiFi came up 3 s after the capture started and never dropped again.
- **Radio path healthy.** 0 TX failures (838 `txn`, `txfail=0` in all 102 STAT lines), 0 ring
  drops, ring depth at most 4/20, own-relay wait median 3.7 s, p95 10.1 s.
- **Neighbour matrix clean.** 0 consistency errors in 512 `CHECK` minutes, hop check PASS, 0
  text-borne edges.
- **Open item: no downstream datagram from the server for the whole window (Finding 1).** 0
  `[GW];rx;type;BEAT`, 0 `type;CET`, 0 `RX-UDP` in 8.5 h. The same node received a BEAT every 30 s
  and 8 to 50 `RX-UDP` per hour before the OTA, and 6850 BEATs in the 09-29 soak. The upstream
  path looks alive (1023 keepalives sent, hub shows the node as gateway). Cause not determined from
  the capture; needs one live check on the node.
- **Not exercised:** DM traffic (`sent=0`), store-and-forward (`MBOX used=0/50` throughout), the
  millis wrap, BLE (no client), `--wifiap`.

## Window and build

| Item           | Value                                                                               |
| -------------- | ----------------------------------------------------------------------------------- |
| Node           | DK5EN-98, Heltec V3, WiFi gateway, 22 dBm, RX boost on, `maxv 3.5`, no cell         |
| Build          | `4.40a (build: Oct 1 2026 / 22:31:23)`, `fw=40a/20260929`, `Flash-Version 20260724` |
| OTA            | 2026-10-01 about 23:27, first console line at 23:28:45 (node uptime 53 s)           |
| Capture        | 2026-10-01 23:28:45 -> 2026-10-02 08:00:09, 117782 lines, net console, rpizero      |
| Logger status  | `finished: time elapsed`, 0 reconnects                                              |
| Not in capture | 19:20 -> 23:28 on 10-01 (the logger of the previous soak had ended, see 09-29)      |

## Metrics

| Metric                            | This soak (8.5 h)                               | 09-29 soak, DK5EN-98 (57 h) |
| --------------------------------- | ----------------------------------------------- | --------------------------- |
| Resets / log gaps > 120 s         | 0 / 0                                           | 0 / 0                       |
| Heap min / first-q / last-q       | 125580 / 129568 / 129588                        | 124684 / 129400 / 129392    |
| Frames received (RX lines)        | 3180 (1274 new, 1906 dup)                       | 7412 new                    |
| New frames per hour               | 150                                             | 130                         |
| TX frames LoRa / UDP              | 839 / 812                                       | 6186 / --                   |
| TX failures                       | 0                                               | 0                           |
| Ring depth max / drops            | 4/20 / 0                                        | 4/20 / 0                    |
| Own relay wait p50 / p95 / max    | 3.7 / 10.1 / 14.2 s (not prio 5)                | 3.6 / 10.5 / 77.6 s (all)   |
| Prio-5 HEY wait max               | 32.0 s                                          | 77.6 s                      |
| Channel use (5-min avg / max)     | 11.8 % / 17 %                                   | 9.2 % / 16 %                |
| CRC errors                        | 93 (10.9 per h)                                 | 949 (16.6 per h)            |
| `ONRXDONE_SLOW` (> 50 ms)         | 2126 (0.67 per frame), max 128 ms               | 1.6 per frame               |
| `RX_IRQ_STALE`                    | 194 (23 per h)                                  | 2446 (43 per h)             |
| NBR CHECK minutes / errors        | 512 / 0                                         | 3430 / 0                    |
| NBR rows / edges max              | 22 / 79                                         | 34 / 87                     |
| Server heartbeats (BEAT) received | **0**                                           | 6850, max gap 90 s          |
| Gateway keepalives sent / max gap | 1023 / 30.3 s                                   | -- / --                     |
| Own gateway HEY                   | every 15 min (+ trickle after topology changes) | median 15.0 min             |
| WiFi link-down events             | 0 (512 `link;up` lines, rssi -48..-51)          | --                          |
| Own position beacons with `/B=`   | 1 of 17 (`B=100`, first beacon after boot)      | 7 of 113                    |
| DM `sent` / MBOX used             | 0 / 0 of 50                                     | 0 / --                      |

The window is mostly night (23:28 to 08:00), which explains the lower CRC rate and the different
channel use; per-hour numbers are therefore not a like-for-like comparison with a 57 h soak.

## Finding 1: no downstream datagram from the server (needs a live check)

- **Evidence:** in the 8.5 h on 4.40a the capture holds 1023 `[GW];keep;tx;ok` lines (one per 30 s,
  max gap 30.3 s) and not a single `[GW];rx;type;BEAT`, `[GW];rx;type;CET` or `RX-UDP` line. In the
  19 h before the OTA, `2026-10-01.log` holds 2321 BEAT lines (one per 30 s, 70 ms after each
  keepalive), 114 CET lines and 312 `RX-UDP` lines. The first BEAT after the 09-29 flash arrived
  60 s after boot.

  ```
  2026-10-01 19:20:20.080  [GW];keep;tx;ok;1;ms;205835855
  2026-10-01 19:20:20.094  [GW];rx;type;BEAT;len;20;ms;205835884     <- 4.35v, BEAT 14 ms later
  2026-10-01 23:28:52.914  [GW];keep;tx;ok;1;ms;60010                <- 4.40a, first keepalive
  (no BEAT, CET or RX-UDP line in the following 8.5 h)
  ```

- **Why it is not a logging artefact:** the `[GW];rx;type;BEAT` print in
  `src/esp32/udp_frame_esp32.cpp:636` is unconditional (TM-39) and that file was last changed in the
  4.40a delta commit `6966c527`, so the running image has it. The `RX-UDP` absence alone would be
  weaker evidence (display flags), the BEAT absence is not.
- **Why the node did not complain:** the heartbeat watchdog in
  `src/esp32/gateway_service_esp32.cpp:30` only runs when `last_upd_timer > 0`, and that timer is
  set by the first BEAT. With no BEAT after the boot it stays 0, so no `[UDP] Server not responding`
  warning, no socket reset, no visible symptom. The `--info` field `UDP-HBeat : 23115` is
  `millis() - node_last_upd_timer`, where that setting is written from `hb_timer`, so it does not
  prove a heartbeat either.
- **What the hub says:** `gateway_availability DK5EN-98` over 48 h reports level 1 (active) and 100 %
  uptime with no outage across the window; `nodes_query` shows `firmware 4.40a`, `isGateway true`,
  `lastSeen` current. So the upstream path (keepalive, uplinked frames) works, and the hub's roster
  lists the node. The hub had a restart at 2026-10-02 12:23 local (after this window).
- **Possible causes (not decided):** the hub did not answer keepalives from a `4.40a` version string
  (the keepalive reads `KEEP0406B878DK5EN-98 4.40a20;232;262;26244;9;`, previously `4.35v20;...`);
  the BEAT is sent to a different address or port than the node listens on; a downstream filter
  in the 4.40a UDP receive path. Each is testable.
- **Consequences if real:** the node would not receive server-side messages (gateway to mesh
  direction), CET time sync, or server ACKs, while uplink looks healthy. DM statistics show no DM
  traffic in the window, so this soak cannot show the effect on delivery.
- **Check to run (needs the node, not part of this review):** read the TM-31 UDP counters
  (`s_udpRxCount`, last remote IP/port) on the live node, or enable `bUDPLOG` and watch the console
  for a few minutes; compare with a second 4.40a gateway node if one exists. If the counter is 0,
  take a packet capture on the router for UDP to 192.168.68.63.
- **Severity:** medium to high until explained (silent half-gateway). Severity drops to a
  non-event if the live counters show datagrams arriving and only the log print is missing.

## Finding 2: foreign gateways flap in the NBR gateway flag (unchanged, by design)

- 17 `[NBR]|GW|` changes in 8.5 h (11 to `1|HG`, 6 to `0|EXP`), all for stations on other firmware
  (DL2JA-2, DL2JA-3, DO1MH-12, DB0FHR-12, DF2KX-12). Same pattern as Finding 2 of the 09-29
  verdict (HG HEYs further apart than the 45 min hold). No action.

## Other checks

| Check                                   | Result                                                                                           |
| --------------------------------------- | ------------------------------------------------------------------------------------------------ |
| `rst:`, `Guru`, `Backtrace`, `TASK_WDT` | 0 hits                                                                                           |
| `[BOOT]` / reboot marker                | only `BOOT RESET_REASON=3 SW` in the first `--info` (the OTA)                                    |
| `[BP]` back-pressure markers            | 0                                                                                                |
| Ring overflow, `ovw`, evictions         | 0 (`drop=0/0/0/0/0` in all 102 STAT lines, `ovw:0`)                                              |
| `ERROR` lines                           | 93, all `CRC_ERROR` dumps, matching the 93 `[LOG] ERR` lines                                     |
| `MC-WARN`                               | 2126, all `ONRXDONE_SLOW`; none of another class                                                 |
| `[INFO]...Check GRC`                    | 15 (140 in the 10-01 file before the OTA); known behaviour, not new                              |
| Store-and-forward / MBOX                | `mode=heard used=0/50 stored=0 deliv=0` in every line; not exercised                             |
| NBR hop check (`nbrhopcheck.py`)        | PASS, 0 text-borne edges, 0 server frames                                                        |
| NBR relay baseline (`nbrrelay.py`)      | 748 own relays (67 text, 186 pos, 495 HEY), foreign copy heard before own TX in 3 to 5 %         |
| Battery on a node without cell          | `--info` 3.88 V / 100 % at `maxv 3.5` (09-29: 4.12 V); 1 of 17 beacons `/B=100`; mcmap shows 0 % |

## What this does not prove

- **Capture length:** 8.5 h, mostly night, against 57 h for the 09-29 soak. Heap trend and rare
  events (the WDT of 09-28 happened after many hours) are only bounded, not excluded. The millis
  wrap at 49.7 days is out of reach.
- **One node:** only DK5EN-98 was recorded. Nothing here speaks for a RAK4631, a node without
  gateway role, or a node in a different RF environment. The DK5EN-1 and DK5EN-90 captures of this
  period were not part of this task.
- **No DM, no S&F, no BLE traffic** in the window; those fixes remain unverified by this soak.
- **Window starts 53 s after boot:** the first minute of the 4.40a boot is not in the capture, and
  the 19:20 to 23:28 span before it belongs to the previous firmware.
- **Finding 1 is a log-based inference.** The capture cannot tell a silent server from a dropped
  datagram; only the node's own UDP counters or a packet capture can.

## Addendum 2026-10-03 21:39: live check of the silent downlink

A passive 75 s listen on the node's net console (`tools/bench/netlurk.py dk5en-98.local`,
nothing sent) showed `[GW];rx;type;BEAT` at 21:39:44 and 21:40:14, i.e. the usual 30 s cadence,
with `ms` 166312357, an uptime of 46.2 h: the node has not rebooted since the 4.40a boot on
2026-10-01 23:28. So the downlink silence seen in the capture (no BEAT, CET or RX-UDP between the
last BEAT at 19:20 on 10-01 and the end of the capture at 08:00 on 10-02, at least 8.5 h after
the OTA plus the 4 h logger gap before it) was transient and cleared without a reboot. The
capture cannot say when it cleared or whether it started before the OTA. Two things remain:

- The cause is undetermined (server side, NAT mapping, or the node's receive socket); only a
  packet capture or the TM-31 UDP counters during a recurrence can tell.
- The heartbeat-staleness diagnostic in `gatewayService_esp32()` arms only after the first BEAT
  has set `last_upd_timer`, so a node that never receives a BEAT after boot never warns. That
  is a defect in the watchdog, not in the downlink, and is tracked as BL-07 in `docs/BACKLOG.md`.
