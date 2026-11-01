# M5 verdict: OE3XIA-12 RF path vs stuck receiver (adversarial check of `lrf_m5.md`)

Verdict: every M5 number reproduces from independent scripts, and the last-hop attribution is correct. The RF-path
conclusion holds. One point is CORRECTED, and in the direction of a stronger M5 result: the residual "RX-only
front-end fault (LNA, RF switch) that leaves TX and CAD intact" contradicts itself. SX1262 CAD runs through the same
RX chain, and CAD detection inside the silences fell no more than at the neighbour nodes. A gross RX front-end loss is
therefore already unlikely. What the logs cannot separate is a chip-internal stuck state from a firmware RX-arming or
IRQ fault.

Scripts are stdlib only, run with `python3 -I`, and take paths via argv; none imports the M5 scripts. Set
`V=docs/log-reports/2026-11-01-24h/verify/lrf_checks/m5_verify`, `N=logs/20261101-24h-log/.logreport-work/norm`,
`$O` = scratch dir. `v1.json` (the output of v1) feeds v2 to v6.

| #   | Claim (M5)                                                                                   | Verdict                     | Own number                                                                                                                                                                                                                                                                                                       | Command                                                                                                     |
| --- | -------------------------------------------------------------------------------------------- | --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| 1   | 35 silences, 15620 s, longest 1024 s, first 10-31 08:26:28                                   | CONFIRMED                   | 35 / 15619.9 s / 1023.8 s / 08:26:28. The logger was alive throughout: the longest gap between any two log lines inside a silence is 4.9 s. All 35 silences contain CAD scans; 9 contain no TX. With ONRXDONE_TIME alone the count is 37 / 17135 s / 1455 s                                                      | `v1_silences.py $N/oe3xia.log $O/v1.json`                                                                   |
| 2a  | Last hop = last callsign of the path                                                         | CONFIRMED                   | The LH byte (`msg_last_hw` = 0x80 or BOARD_HARDWARE of the last sender, `src/loop_functions.cpp:3739`) is 0x89 on 1413/1413 (XHL), 329/329 (XIR) and 1026/1026 (XOC) frames whose path ends in OE3XIA-12. At XHL every last-hop callsign maps to exactly one LH value. The originator is the first path element  | `v2_snr.py` (LH counters); `grep -o` at XHL, see below                                                      |
| 2b  | SNR/RSSI unchanged inside vs outside at XHL/XIR/XOC                                          | CONFIRMED                   | XHL: in n=34, SNR median -19, out n=1369, median -18, p=0.420; RSSI -113/-113, p=0.824. XIR: n=10/319, SNR 3/3, p=0.056, means 2.2/3.08. XOC: n=27/987, SNR -2/-2, p=0.055, means -2.15/-2.52. XWJ, VQQ4, VQQ5: 0 frames. Clock skew between loggers: median 0.91 s, p95 1.29 s                                  | `v2_snr.py $O/v1.json $N/oe3xia.log $O/v2.json xhl=$N/oe3xhl.log xir=... xoc=... xwj=... vqq4=... vqq5=...` |
| 3   | Heard share of non-ACK TX: 40/44 inside vs 1710/2031 outside                                 | CONFIRMED                   | 40/44 = 90.9 % (Wilson 78.8..96.4) vs 1710/2031 = 84.2 %, Fisher p=0.295. Window with all 4 loggers: 22/24 vs 772/927, p=0.405. Identical with a 10 s window instead of 30 s                                                                                                                                     | `v3_heard.py $O/v1.json $N/oe3xia.log $O/v3.json 30 xhl=... xir=... xoc=... xwj=...` (repeat with `10`)     |
| 3b  | ACK exclusion is fair                                                                        | CONFIRMED                   | Ambiguous ACK-id match (same id at any logger within ±3 s): 13/13 inside vs 1867/1911 outside, p=1.0. Including ACKs would not change the verdict. Inside TX by source: 25 relay (all 25 queued before the silence began, `t - wait < start`), 15 `:`/g, 17 own beacons                                          | as 3                                                                                                        |
| 4   | CAD busy 161/218 = 73.9 % vs 82.7 %; other nodes drop the same way                           | CONFIRMED, metric caveat    | 161/218 (Wilson 67.6..79.2) vs 18889/22831. Within 30 min of a silence edge: 82.5 %, so time of day is no confounder. Inside/outside ratio: XIA 0.894; XHL 0.791, XIR 0.937, XOC 0.955, XWJ 0.991. Caveat: OE3XIA has exactly one `result=0` per TX (3999 = 3999 TX), so the share is a CSMA retry ratio         | `v4_cad.py $O/v1.json $O/v4.json xia=$N/oe3xia.log xhl=... xir=... xoc=... xwj=...`                         |
| 5   | Earlier capture (09-01 night): 1 gap of 300.5 s, next 174.5 s; on 31.10: 37 / 17135 / 1455 s | CONFIRMED                   | 1 gap of 300.5 s at 09-01 22:36:15; top 3: 300.5, 174.5, 144.2 s. Robust: the old build prints 308 own-path `[LOG]` lines (no RSSI field), and leaving them out changes nothing; the old log has no ACK lines. 31.10: 37 / 17134.6 / 1455.0 s. Same 22:09-06:17 night window on 31.10/11-01: 10 silences, 4112 s | `v7_earlier.py logs/berg-node/oe3xia_meshcom_2026-09-01_220932.log $N/oe3xia.log`                           |
| 6   | mcmap has no wind data                                                                       | CONFIRMED                   | The `wx_telemetry_series` field enum has no wind or gust field. `wx_stations` returns only temp/temp2/hum (total 507). `telemetry_channels OE3XIA-12` returns an empty list                                                                                                                                      | mcmap-prod MCP, read-only                                                                                   |
| 7   | Inference: "RX-only fault (LNA, RF switch) not excluded"                                     | CORRECTED (overly cautious) | See below                                                                                                                                                                                                                                                                                                        | `v5_edges.py $O/v1.json $N/oe3xia.log $O/v5.json`, `v6_echo.py $O/v1.json $N/oe3xia.log $O/v6.json`         |

LH check at XHL:
`grep -o 'M[0-9A-F][0-9A-F] [^ >]*>.* LH:[0-9A-F][0-9A-F]' $N/oe3xhl.log | sed -E 's/^M.. ([^>]*)>.* (LH:..)$/\1 \2/' | awk '{n=split($1,a,","); print a[n], $2}' | sort | uniq -c`

## Inference

- **RF path (antenna, coax, TX): conclusion holds.** Neighbour SNR stays within 1 dB, the heard share does not drop,
  and the attribution is verified via the LH byte. A loss of 3 dB or more on the shared path would show at all three
  loggers.
- **The RX front end is already disfavoured.** On the SX1262, CAD uses the RX chain: RF switch in RX position, LNA,
  mixer and ADC. OE3XIA normally receives its neighbours at up to -99 dBm / SNR +4. Silencing every frame would take
  roughly 20 dB or more of RX-only loss. That loss would collapse CAD detection, yet the CAD ratio of 0.894 lies inside
  the 0.79..0.99 range of the other nodes. A small RX loss of a few dB stays possible but cannot explain total silence.
  M5's closing bullet should read "a gross RX-only front-end fault is unlikely (CAD intact)" rather than "not
  excluded".
- **Supporting stuck-state signatures that M5 does not list:**
  - The periodic `RX_RESTART src=timeout` continues inside the silences (775/h vs 842/h) without restoring reception.
  - 12 of 35 silences end 0.4..2.6 s after a TX_DONE. At 13.1 TX/h, chance would give about 0.4. In 5 of these the
    ending frame is the relay echo of OE3XIA's own TX.
  - 45 of the 57 TX inside the silences did not end the silence.
  - Outside the silences OE3XIA hears the echo of 50.2 % of its non-ACK TX.
  - Together: the TX-to-RX transition sometimes clears the state, and periodic re-arming never does.
- **What remains open:** a chip-internal state (SX1262 stuck in a non-receiving mode) and a firmware fault (RX
  restart not reaching the chip, RxDone IRQ not serviced) look the same in these logs. Only the planned `RX_SILENCE`
  snapshot separates them: `SX126xGetRssiInst()`, op mode, IRQ flags and device errors.
- **Minor overstatements in M5:**
  - "CAD works" rests on a TX-conditional retry ratio with n=218.
  - The heard share alone (CI down to 78.8 %) cannot exclude a few-dB TX loss; the SNR comparison does that job.
  - XIR (n=10) and XOC p≈0.055 point in opposite directions and are not significant. Neither p-value is corrected for
    multiple tests.

Artifact note: 9 `RX_PROCESS -> RX_LISTEN` lines fall inside silences. They are the tail of the reception that opens
each silence (logged after `ONRXDONE_TIME`), not hidden receptions.
