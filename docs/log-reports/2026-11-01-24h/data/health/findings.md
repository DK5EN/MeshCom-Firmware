# 24-h log baseline per node (captures 2026-10-30/31 -> 11-01)

Facts are counted from the logs by `scripts/node_baseline.py` (+ helper scripts); inferences are marked. Status words: OK / Beobachten / Handlungsbedarf (thresholds are in `scripts/build_report.py:verdicts`, mine, not firmware limits). Times are logger clock (CET = UTC+1; NTP epoch comparison 3601 +-1 s); DB0AIS times are derived from node millis + PuTTY start (+-5 s). `[LOG]` receptions are printed before dedup, so they are receptions, not accepted packets.

## Overview

| Node | HW | Span h | Util mean/peak % | Noise carpet p5 (dBm) | RX_ERROR share | CAD >=3 attempts | Pre-CAD events % | BEAT miss % |
|---|---|---|---|---|---|---|---|---|
| OE3XHL-12 | RAK4631 | 25.03 | 18.0/91 | -113 | 12.9% | 2.4% | 26.1% | 2.1% |
| OE3XIA-12 | RAK4631 | 24.95 | 16.5/82 | -109 | 55.0% | 39.8% | 19.4% | 0.03% |
| OE3XIR-12 | RAK4631 | 11.49 | 20.8/71 | -110 | 48.3% | 23.8% | 16.6% | 0.87% |
| OE3XOC-12 | RAK4631 | 25.35 | 28.5/85 | -112 | 43.3% | 22.5% | 16.4% | 0.07% |
| OE3XWJ-12 | RAK4631 | 25.51 | 19.6/69 | -106 | 41.4% | 5.9% | 23.8% | 1.41% |
| DB0AIS-12 | RAK4631 | 25.15 | 8.9/77 | -107 | 66.9% | 14.0% | 9.9% | 2.56% |

Cross-node facts:

- All six nodes are RAK4631 (nRF52840 + W5100S Ethernet), Internet gateways on build stamp 20260929, all booted within minutes before logging started (first STAT up=300 s, boot estimates in each Crashes row), none rebooted during the capture.
- ACK traffic: on the five OE3 nodes 97.4-98.9% of all 012-byte ACK receptions acknowledge msg_ids F5CE7xxx = the 1/min ping of OE3XWJ-12 (`OE3XWJ-12>2025:{ping}`). ACK frames are 23-41% of decoded airtime on the OE3 nodes (DB0AIS: 1.7%). Verdict: Handlungsbedarf unless the 1/min ping is an intended test (operator question).
- OE3XIR-12 emits one invalid ping per minute (`255.:{ping}`, 689x): firmware reads it from the TX ring and drops it at decode (`APRS decode - Destination-CallSign Error [255.]`, `CAD_FREE_NO_TX`). No airtime, but it burns a CAD cycle and a ring slot per minute. Verdict: Beobachten, suspected input-validation gap (see OE3XIR section).
- Common server-side event: all five OE3 gateways lost BEAT replies at 10-31 07:22:10 (episodes starting within 75 s on 5 nodes) -> OE server/uplink, not a node fault.
- OE3XIA-12 receive silences: 35 periods >5 min without a single OnRxDone/OnRxError (15678 s = 17.5% of the capture, longest 1030 s at 10-31 11:48:56) while every other OE3 node still logging kept receiving (16-309 events per window; OE3XIR's log ends 18:16); no other node has a silence >153 s. Verdict: Handlungsbedarf (see OE3XIA verdict row).
- Node clock vs logger: OE3XHL/XIA/XOC/XWJ +0 h; OE3XIR +1 h (whole capture); DB0AIS +1 h for the first 79 min, then a -1 h step at ~18:20:3x (no cause in the log). Suspect area: TZ-01 re-derivation in `src/clock.cpp:153-165` (`tzApplyNow()`), node_tz setting not in the capture. Verdict: Beobachten.
- {CET} beacons: median interval 607 s (OE3 server OE1XAR-33; DB0AIS' DL gateway receives OE1XAR-62 at 602 s). The beacon text carries UTC (stamp 05:51:20 received at logger 06:51:40) although tagged {CET}: fact from OE3XHL line `2026-10-31 06:51:41.167`. Verdict: Beobachten (naming/semantics, check with the time-signal owner).

## OE3XHL-12  (log: oe3xhl.log)

| Item | Value |
|---|---|
| Callsign / GW-ID | OE3XHL-12 / FC6619C0 (from `[KEEP]` line; `[GW];keep` itself carries no call) |
| Hardware | RAK4631 (HW id 9, src/configuration_global.h //Hardware Types) - from own `NEW-*`/`TX_FRAME` HW field |
| Role | Internet gateway + relay: keepalives 2999, uploads GWU 2721, server->mesh injections GWI 232, BEAT replies 2936 |
| Firmware | 4.40a (STAT `fw=` 40a/20260929 = build stamp 20260929; exact build otherwise unknown; repo HEAD stamp is 20261030) |
| LoRa | not logged (no boot block). SF11/BW250/CR4:6 inferred: ToA of the TX lines matches CHANNEL_UTIL tx= (2714 s vs 2714 s); frequency/power not in log (fleet standard 433.175 MHz) |
| Capture span | 2026-10-31 06:44:04 -> 2026-11-01 07:46:02 (25.03 h, 341884 lines; logger wall prefix) |
| Clock | node clock minus logger clock: +0 h (mode) (node on UTC+1 = logger) |

### Traffic (receptions, not accepted packets; FCS:0000 excluded)

- Heard frame events 11647 (+ 10179 ACK frames = 46.6% of all receptions; 12 FCS:0000 decoder bailouts excluded). New 3678 / duplicate copies 7969.
- Unique originators 210 (excl. time servers; includes own-call echoes), unique msg_ids 3547. Types: heartbeat/HEY(40) 7500 (64.4%), position(21) 2390 (20.5%), text(3A) 1757 (15.1%).
- Decoded airtime by type: {'@': 46.5, 'ACK': 23.0, '!': 19.2, ':': 11.4} (ACK frames are 012-byte, ~313 ms each).
- Hop: H field (remaining max_hop) {'H00': 26.6, 'H01': 26.9, 'H02': 24.9, 'H03': 15.7, 'H04': 5.7, 'H14': 0.0, 'H15': 0.1}; path length (callsigns) {'1': 5.3, '2': 24.2, '3': 36.4, '4': 19.2, '5': 14.7, '6': 0.3, '8': 0.0}; zero-relay (path length 1) 5.3%.
- Own TX 4375 (174.8/h, 2714.2 s airtime): own 149 / relay 3994 / gateway(server->mesh) 232; by type {'@': 1353, 'A': 2140, '!': 536, ':': 346}, by prio {'1': 2140, '3': 346, '4': 536, '5': 1353}; queue wait median 8519 ms, p95 61299 ms; CAD attempts at TX start {'0': 3168, '1': 954, '2': 182, '3': 38, '4': 9, '5': 16, '6': 5, '8': 2, '9': 1}.

Direct neighbours = last path element (9 distinct; RSSI/SNR belong to this last hop only; RSSI looks clamped at the carpet for weak links):

| Neighbour | n | RSSI med (p10..p90, max) dBm | SNR med | HW | dist km | Note |
|---|---|---|---|---|---|---|
| OE3GJC-12 | 2427 | -112 (-112..-111, -100) | -4 | RAK4631 | 11.9 | Gateway Traiskirchen |
| OE3MAG-10 | 1755 | -113 (-113..-112, -112) | -11 | TBEAM V1.1 | 4.4 |  |
| OE3HUS-01 | 1710 | -112 (-113..-112, -112) | -7 | TBEAM V1.1 | 23.5 |  |
| OE3XIA-12 | 1413 | -113 (-113..-113, -98) | -18 | RAK4631 | 28.2 | Exelberg |
| OE3HES-1 | 1381 | -98 (-99..-97, -96) | 5 | T-DECK + | 0.0 |  |
| OE3RDB-2 | 1131 | -113 (-113..-113, -112) | -15 | TLORA V2.1.6 | 7.1 | Bad Vöslau |
| OE1XAR-12 | 1042 | -113 (-113..-113, -112) | -16 | RAK4631 | n/a |  |
| OE3ZKW-12 | 781 | -113 (-113..-113, -113) | -16 | TLORA V2.1.6 | 61.3 | at Home |
| OE3MAG-3 | 7 | -113 (-113..-113, -113) | -19 | n/a | n/a |  |

### Channel / RF

- Noise: RX_ERROR RSSI p5/median -113/-112 dBm; RSSI-SNR (SNR>=0) median -104 dBm; RX_ERROR histogram {'-120': 1270, '-110': 20, '-100': 430}.
- CAD: 7900 scans, initial busy 24.3%, confirmed busy 83.4% of initial busy, false positive 16.6%. cad_attempt distribution (TX_GATE_ENTER): 0:73.2% 1:20.2% 2:4.2% >=3:2.4%.
- util by hour (CHANNEL_UTIL mean %): {'10-31 06': 20.1, '10-31 07': 21.0, '10-31 08': 19.7, '10-31 09': 20.2, '10-31 10': 18.9, '10-31 11': 19.6, '10-31 12': 17.3, '10-31 13': 17.2, '10-31 14': 20.5, '10-31 15': 18.0, '10-31 16': 19.1, '10-31 17': 19.1, '10-31 18': 19.5, '10-31 19': 17.7, '10-31 20': 19.6, '10-31 21': 16.6, '10-31 22': 15.9, '10-31 23': 14.2, '11-01 00': 15.7, '11-01 01': 15.0, '11-01 02': 15.7, '11-01 03': 18.4, '11-01 04': 16.6, '11-01 05': 17.6, '11-01 06': 18.1, '11-01 07': 17.3}

### Firmware census of heard originators

- Pre-CAD 19/209 nodes (9.1%), 26.1% of events, 23.7% of airtime. Versions: {'4.40a': 159, '4.35p': 22, '4.35k': 6, '4.35t': 4, '4.35d': 4, '4.35f': 3, '4.35h': 3, '4.0#': 2, '4.35i': 1, '4.35v': 1, '4.35s': 1, '4.34#': 1, '4.34v': 1, '4.40A': 1}. (4.40a counted as post-CAD; `#` letter = sub-version not transmitted; 4.34/4.30 are older than CAD.)

| Top pre-CAD talker | FW | events | msgs | share of node events | HW | place | dist km |
|---|---|---|---|---|---|---|---|
| OE3HUS-01 | 4.35k | 484 | 129 | 4.6% | TBEAM V1.1 | lat/lon 48.0852,16.429 | 23.5 |
| OE3HES-1 | 4.35d | 479 | 122 | 4.6% | T-DECK + | lat/lon 48.0075,16.1347 | 0.0 |
| OE3RDB-2 | 4.35f | 472 | 126 | 4.5% | TLORA V2.1.6 | Bad Vöslau | 7.1 |
| OE3HUS-02 | 4.35k | 261 | 86 | 2.5% | TBEAM V1.1 | lat/lon 48.0852,16.429 | 23.5 |
| OE3REI-14 | 4.35k | 185 | 63 | 1.8% | RAK4631 | lat/lon 48.1102,15.2323 | 68.0 |
| OE1NHU-22 | 4.35k | 167 | 54 | 1.6% | T-DECK | lat/lon 48.2843,16.3887 | 36.1 |
| OE3RDB-1 | 4.35f | 165 | 52 | 1.6% | TLORA V2.1.6 | Bad Vöslau | 7.1 |
| OE3XSI-13 | 4.35k | 158 | 58 | 1.5% | TBEAM V1.1 | Notfunkst. StP | 44.2 |

### Gateway / server path

- BEAT miss episodes 61 (63 keepalives = 2.1%); BEAT gaps >65 s: 2 (max 90.1 s); clusters of 'Server not responding': 58.
- Busiest hours for missing BEAT (logger clock): [('11-01 00', 6), ('10-31 07', 5), ('10-31 19', 5), ('10-31 18', 4), ('11-01 03', 4)]
- ETH: IP 44.143.95.43 -> server 44.143.8.143; hb_age_s median 14 / max 56; stalls {"dhcp_maintain/mcloop": [100, 65.0]}; events {'dhcp': 100}.

### Verdict table

| Aspect | Status | Comment |
|---|---|---|
| State machine | OK | 1603 x TX_PREPARE->IDLE rc=-1 (CAD found channel busy, normal); 0 other rc!=0 / ERROR; 4 console-interleaved lines ignored |
| Relay | OK | 3994 of 4375 TX are relays (91.3%); RING_OVERFLOW 0, RING_DROP_PRIO/NEW 0; TTL drops 3 (prio5 HEY/prio4 position, by design); ring peak 7/20; RLY refusals {'tx': 2462, 'gwcap': 464, 'hop0': 220, 'gwfilter': 12, 'nomesh': 6} |
| Hop counter | Beobachten | H field {'H00': 26.6, 'H01': 26.9, 'H02': 24.9, 'H03': 15.7, 'H04': 5.7, 'H14': 0.0, 'H15': 0.1}; 17 frames with H>=13 (wrap from foreign builds) |
| Loops | OK | RELAY_LOOP_BLOCKED 0 (gate working, not a fault); 2127 receptions (18.3%) carry own call in path (echo of own/relayed frames) |
| Channel util | OK | CHANNEL_UTIL mean 18.0%, median 17.0%, p95 39.0%, peak 91.0% (3 samples >=70%, 1 >=90%); 1st/2nd half 19.2/16.7%. nRF52 (RAK4631): ESP32 255-byte inflation not applicable, fingerprint test negative (22 of 8247 rx values are multiples of 2476 ms). Cross-check: own TX airtime 2714 s (ToA of TX lines) = 2714 s (CHANNEL_UTIL) = 2711 s (STAT); STAT rx 13843 s vs ToA of decoded frames 13853 s (within ~1%) -> util counts decoded frames only, undecodable energy is not in it |
| Retries | OK | RING_STATUS retrying max 0 over 2999 samples; pending max 0; ring high-water 8/20; RETRANSMIT_GIVEUP 0 |
| CRC / RX errors | OK | 1720 RX_ERROR (preamble/header seen, no valid frame) = 68.7/h = 12.9% of (errors + OnRxDone); median RSSI -112.0 dBm, median SNR -11.0 dB -> noise-limited. 0 errors above -85 dBm, 0 above -60 dBm. nRF52 limits: no freq-error (ferr always 0), no RX_IRQ_STALE markers (0) |
| Network (ETH/GW) | Beobachten | keepalive [GW];keep ok 2999 (expected ~3004, max gap 30.2 s); 63 keepalives (2.1%) without server BEAT reply, '[UDP] Server not responding for 35s' 61x; ETH tx_fail 0, DHCP renews 100, renew_fail 0, hw resets 0; NTP ok 100/timeouts 7 |
| Crashes / reboots | OK | uptime monotonic (300 s at first STAT .. 90015 s at last), 0 resets, 0 crash/panic/watchdog hits, boot banners in log: 0; node had booted 117 s before the first logged line (first STAT up=300 s at 2026-10-31 06:42:07 est.) |
| Heap | OK | STAT heap 92252 -> 92252 B (min 92252, max 92252), slope 0.0 B/h; no [HEAP] lines in the stream except boot; flat = no leak signal (nRF52 figure is coarse) |
| CSMA / CAD | OK | TX_GATE_ENTER 5979: attempt0 73.2%, >=3: 2.4%, max 9; CAD initial busy 24.3% of 7900 scans, false positive 16.6% of initial busy; 71 episodes with attempt>=3 (longest 2.1 s); CAD_SAFETY_TIMEOUT 0 (CadDone not seen within 100 ms, radio restarted to RX, no TX that round), CSMA_AGED 79 |
| Double send | OK | bug-type (same slot, READY then DONE) 0; 0 msg_ids read more than once; recurrences are >4 h apart or legit retransmit copies |
| Firmware without CAD | Handlungsbedarf | 19 of 209 originators (9.1%) on pre-CAD (4.35a-o, 4.34 and older); they carry 26.1% of heard frame events, 24.7% of unique msgs, 23.7% of decoded airtime (own-call echoes and time servers excluded) |
| Noise floor | OK | carpet = RX_ERROR RSSI p5 -113 dBm (median -112) -> 1 dB above the -114 dBm BW250 theory; RSSI-SNR estimator (SNR>=0 frames, n=1381) -104 dBm - the two disagree (RSSI looks like total in-band power, clamped at the carpet), see method note |
| Input level neighbours | OK | strongest decoded RSSI -96 dBm; 0 receptions above -20 dBm (overload limit), 0 above -40 dBm; 10-dB histogram {'-120': 19196, '-110': 65, '-100': 2565} |
| Receive silences | OK | median gap between receptions 4.52 s, longest 133 s (at 11-01 05:51:45); 0 silences >300 s |
| Time signal delivery | OK | Internet side ([CET];gw_rx from OE1XAR-33): 146 of ~148 (median gap 607 s), 2 missing; LoRa side: 148 distinct stamps heard, 0 gaps; coverage of OE3 union 100.0% |
| ACK plausibility | OK | 4 of 10179 ACK receptions (0.04%) have byte5 outside 0x80..0x84; firmware ACK_REJECT markers 1 |
| OnRxDone timing | OK | ONRXDONE_TIME mean 4.2 ms, median 4.0, p95 10.0, max 54.0 ms; 5 of 21818 above 50 ms (MC-WARN ONRXDONE_SLOW) |

## OE3XIA-12  (log: oe3xia.log)

| Item | Value |
|---|---|
| Callsign / GW-ID | OE3XIA-12 / D15DAD16 (from `[KEEP]` line; `[GW];keep` itself carries no call) |
| Hardware | RAK4631 (HW id 9, src/configuration_global.h //Hardware Types) - from own `NEW-*`/`TX_FRAME` HW field |
| Role | Internet gateway + relay: keepalives 2989, uploads GWU 2566, server->mesh injections GWI 282, BEAT replies 2999 |
| Firmware | 4.40a (STAT `fw=` 40a/20260929 = build stamp 20260929; exact build otherwise unknown; repo HEAD stamp is 20261030) |
| LoRa | not logged (no boot block). SF11/BW250/CR4:6 inferred: ToA of the TX lines matches CHANNEL_UTIL tx= (2494 s vs 2492 s); frequency/power not in log (fleet standard 433.175 MHz) |
| Capture span | 2026-10-31 06:40:04 -> 2026-11-01 07:36:46 (24.95 h, 464064 lines; logger wall prefix) |
| Clock | node clock minus logger clock: +0 h (mode) (node on UTC+1 = logger) |

### Traffic (receptions, not accepted packets; FCS:0000 excluded)

- Heard frame events 8989 (+ 14482 ACK frames = 61.7% of all receptions; 71 FCS:0000 decoder bailouts excluded). New 3615 / duplicate copies 5374.
- Unique originators 210 (excl. time servers; includes own-call echoes), unique msg_ids 3222. Types: heartbeat/HEY(40) 4874 (54.2%), text(3A) 2437 (27.1%), position(21) 1678 (18.7%).
- Decoded airtime by type: {'ACK': 36.2, '@': 33.4, ':': 15.5, '!': 14.9} (ACK frames are 012-byte, ~313 ms each).
- Hop: H field (remaining max_hop) {'H00': 23.5, 'H01': 26.3, 'H02': 23.4, 'H03': 20.5, 'H04': 6.2, 'H05': 0.2}; path length (callsigns) {'1': 5.1, '2': 28.9, '3': 34.3, '4': 19.2, '5': 11.7, '6': 0.7, '7': 0.0}; zero-relay (path length 1) 5.1%.
- Own TX 3999 (160.3/h, 2494.1 s airtime): own 187 / relay 3530 / gateway(server->mesh) 282; by type {'A': 1924, ':': 334, '!': 484, '@': 1257}, by prio {'1': 1924, '3': 334, '4': 484, '5': 1257}; queue wait median 21249 ms, p95 97354 ms; CAD attempts at TX start {'0': 1719, '1': 742, '2': 469, '3': 280, '4': 158, '5': 130, '6': 109, '7': 68, '8': 67, '9': 36, '10': 47, '11': 36, '12': 32, '13': 13, '14': 15, '15': 9, '16': 8, '17': 14, '18': 9, '19': 4, '20': 8, '21': 4, '22': 3, '23': 1, '24': 4, '25': 4, '26': 2, '27': 2, '28': 2, '31': 1, '32': 1, '36': 2}.

Direct neighbours = last path element (54 distinct; RSSI/SNR belong to this last hop only; RSSI looks clamped at the carpet for weak links):

| Neighbour | n | RSSI med (p10..p90, max) dBm | SNR med | HW | dist km | Note |
|---|---|---|---|---|---|---|
| OE1BSE-1 | 781 | -96 (-97..-95, -90) | 4 | TBEAM V1.2 | 12.0 | extern. antenna |
| OE3XPA-12 | 679 | -99 (-100..-98, -91) | 4 | RAK4631 | 56.5 | Meshcom iGate |
| OE1RIG-4 | 606 | -91 (-92..-90, -72) | 5 | HELTEC Tracker | 4.1 | QTH |
| OE3ALB-2 | 599 | -103 (-104..-101, -90) | 2 | TTGO LoRa32 V2.1 1.6 | n/a |  |
| OE3GJC-12 | 510 | -107 (-107..-105, -91) | -3 | RAK4631 | 26.5 | Gateway Traiskirchen |
| OE3GHB-1 | 467 | -98 (-99..-96, -90) | 4 | HELTEC V3 | 36.0 | Gaweinstal |
| OE3XOC-12 | 453 | -107 (-108..-106, -91) | -6 | RAK4631 | 22.6 | Buchberg |
| OE3XHQ-12 | 452 | -104 (-105..-103, -91) | 0 | TBEAM V1.1 | 20.6 |  |
| OE1XAR-12 | 352 | -104 (-105..-103, -91) | -3 | RAK4631 | n/a |  |
| OE3XIR-12 | 333 | -101 (-102..-100, -91) | 3 | RAK4631 | n/a |  |

### Channel / RF

- Noise: RX_ERROR RSSI p5/median -109/-104 dBm; RSSI-SNR (SNR>=0) median -103 dBm; RX_ERROR histogram {'-110': 7381, '-100': 3314, '-90': 347, '-80': 38}.
- CAD: 23049 scans, initial busy 43.7%, confirmed busy 89.3% of initial busy, false positive 10.7%. cad_attempt distribution (TX_GATE_ENTER): 0:30.8% 1:17.6% 2:11.9% >=3:39.8%.
- util by hour (CHANNEL_UTIL mean %): {'10-31 06': 28.6, '10-31 07': 19.9, '10-31 08': 16.1, '10-31 09': 18.4, '10-31 10': 14.5, '10-31 11': 14.6, '10-31 12': 8.3, '10-31 13': 12.3, '10-31 14': 16.2, '10-31 15': 11.2, '10-31 16': 17.9, '10-31 17': 18.9, '10-31 18': 14.3, '10-31 19': 15.0, '10-31 20': 11.7, '10-31 21': 18.0, '10-31 22': 20.7, '10-31 23': 18.7, '11-01 00': 17.2, '11-01 01': 15.8, '11-01 02': 15.0, '11-01 03': 20.8, '11-01 04': 17.3, '11-01 05': 19.6, '11-01 06': 19.0, '11-01 07': 19.1}

### Firmware census of heard originators

- Pre-CAD 21/209 nodes (10.0%), 19.4% of events, 19.4% of airtime. Versions: {'4.40a': 156, '4.35p': 23, '4.35k': 6, '4.35t': 4, '4.35d': 4, '4.35f': 3, '4.35h': 3, '4.0#': 2, '4.34v': 2, '4.30#': 1, '4.35i': 1, '4.35v': 1, '4.35s': 1, '4.34#': 1, '4.40A': 1}. (4.40a counted as post-CAD; `#` letter = sub-version not transmitted; 4.34/4.30 are older than CAD.)

| Top pre-CAD talker | FW | events | msgs | share of node events | HW | place | dist km |
|---|---|---|---|---|---|---|---|
| OE3REI-14 | 4.35k | 184 | 62 | 2.5% | RAK4631 | lat/lon 48.1102,15.2323 | 76.7 |
| OE3HUS-01 | 4.35k | 174 | 60 | 2.3% | TBEAM V1.1 | lat/lon 48.0852,16.429 | 22.8 |
| OE3RDB-2 | 4.35f | 162 | 53 | 2.2% | TLORA V2.1.6 | Bad Vöslau | 30.4 |
| OE3XWJ-94 | 4.35d | 159 | 49 | 2.1% | EBYTE E22 | lat/lon 48.469,15.6813 | 48.3 |
| OE3XSI-13 | 4.35k | 157 | 56 | 2.1% | TBEAM V1.1 | Notfunkst. StP | 46.1 |
| OE3HES-1 | 4.35d | 151 | 60 | 2.0% | T-DECK + | lat/lon 48.0075,16.1347 | 28.1 |
| OE1NHU-22 | 4.35k | 93 | 37 | 1.2% | T-DECK | lat/lon 48.2843,16.3887 | 11.3 |
| OE1KBC-90 | 4.35d | 91 | 30 | 1.2% | HELTEC E290 | Kurt | 13.0 |

### Gateway / server path

- BEAT miss episodes 1 (1 keepalives = 0.03%); BEAT gaps >65 s: 0 (max 0 s); clusters of 'Server not responding': 1.
- Busiest hours for missing BEAT (logger clock): [('10-31 07', 1)]
- ETH: IP 44.143.10.220 -> server 44.143.8.143; hb_age_s median 11 / max 43; stalls {"udp_rx/mcloop": [1, 497.0]}; events {}.

### Verdict table

| Aspect | Status | Comment |
|---|---|---|
| State machine | OK | 8986 x TX_PREPARE->IDLE rc=-1 (CAD found channel busy, normal); 0 other rc!=0 / ERROR; 0 console-interleaved lines ignored |
| Relay | OK | 3530 of 3999 TX are relays (88.3%); RING_OVERFLOW 0, RING_DROP_PRIO/NEW 0; TTL drops 93 (prio5 HEY/prio4 position, by design); ring peak 15/20; RLY refusals {'tx': 1677, 'gwfilter': 602, 'gwcap': 379, 'hop0': 128, 'nomesh': 32} |
| Hop counter | OK | H field {'H00': 23.5, 'H01': 26.3, 'H02': 23.4, 'H03': 20.5, 'H04': 6.2, 'H05': 0.2}; no H>=13 wrap |
| Loops | OK | RELAY_LOOP_BLOCKED 0 (gate working, not a fault); 3263 receptions (36.3%) carry own call in path (echo of own/relayed frames) |
| Channel util | OK | CHANNEL_UTIL mean 16.5%, median 15.0%, p95 45.0%, peak 82.0% (3 samples >=70%, 0 >=90%); 1st/2nd half 15.7/17.4%. nRF52 (RAK4631): ESP32 255-byte inflation not applicable, fingerprint test negative (33 of 5252 rx values are multiples of 2476 ms). Cross-check: own TX airtime 2494 s (ToA of TX lines) = 2492 s (CHANNEL_UTIL) = 2493 s (STAT); STAT rx 12642 s vs ToA of decoded frames 12546 s (within ~1%) -> util counts decoded frames only, undecodable energy is not in it |
| Retries | OK | RING_STATUS retrying max 0 over 2989 samples; pending max 0; ring high-water 15/20; RETRANSMIT_GIVEUP 0 |
| CRC / RX errors | Beobachten | 11080 RX_ERROR (preamble/header seen, no valid frame) = 444.2/h = 55.0% of (errors + OnRxDone); median RSSI -104.0 dBm, median SNR -5.0 dB -> noise-limited. 38 errors above -85 dBm, 0 above -60 dBm. nRF52 limits: no freq-error (ferr always 0), no RX_IRQ_STALE markers (0) |
| Network (ETH/GW) | OK | keepalive [GW];keep ok 2989 (expected ~2993, max gap 30.3 s); 1 keepalives (0.03%) without server BEAT reply, '[UDP] Server not responding for 35s' 1x; ETH tx_fail 1 (already in the first 60-s summary = boot-time first send, max 1.6 s), DHCP renews 0, renew_fail 0, hw resets 0; NTP ok 99/timeouts 0 |
| Crashes / reboots | OK | uptime monotonic (300 s at first STAT .. 89715 s at last), 0 resets, 0 crash/panic/watchdog hits, boot banners in log: 0; node had booted 170 s before the first logged line (first STAT up=300 s at 2026-10-31 06:37:14 est.) |
| Heap | OK | STAT heap 92268 -> 92268 B (min 92268, max 92268), slope 0.0 B/h; no [HEAP] lines in the stream except boot; flat = no leak signal (nRF52 figure is coarse) |
| CSMA / CAD | Handlungsbedarf | TX_GATE_ENTER 12993: attempt0 30.8%, >=3: 39.8%, max 36; CAD initial busy 43.7% of 23049 scans, false positive 10.7% of initial busy; 1069 episodes with attempt>=3 (longest 10.5 s); CAD_SAFETY_TIMEOUT 3 (CadDone not seen within 100 ms, radio restarted to RX, no TX that round), CSMA_AGED 3593 |
| Double send | OK | bug-type (same slot, READY then DONE) 0; 13 msg_ids read more than once; recurrences are >4 h apart or legit retransmit copies |
| Firmware without CAD | Beobachten | 21 of 209 originators (10.0%) on pre-CAD (4.35a-o, 4.34 and older); they carry 19.4% of heard frame events, 19.2% of unique msgs, 19.4% of decoded airtime (own-call echoes and time servers excluded) |
| Noise floor | OK | carpet = RX_ERROR RSSI p5 -109 dBm (median -104) -> 5 dB above the -114 dBm BW250 theory; RSSI-SNR estimator (SNR>=0 frames, n=4321) -103 dBm - the two disagree (RSSI looks like total in-band power, clamped at the carpet), see method note |
| Input level neighbours | OK | strongest decoded RSSI -72 dBm; 0 receptions above -20 dBm (overload limit), 0 above -40 dBm; 10-dB histogram {'-110': 16393, '-100': 6386, '-90': 659, '-80': 33} |
| Receive silences | Handlungsbedarf | median gap between receptions 1.89 s, longest 1030 s (at 10-31 11:48:56); 35 silences >300 s totalling 15678 s (17.5% of the capture); all 35 began 10-31 08:26 or later (none in the first 1 h 46 min), in every one each other OE3 node still logging kept receiving (16-309 events per window), no OnRxDone and no OnRxError at all, while CAD kept sensing the channel busy (77% of 265 scans vs 83% outside) and own TX continued -> receiver path stuck/desensitised, not a quiet band (inference) |
| Time signal delivery | OK | Internet side ([CET];gw_rx from OE1XAR-33): 148 of ~148 (median gap 607 s), 0 missing; LoRa side: 141 distinct stamps heard, 7 gaps; coverage of OE3 union 95.3% |
| ACK plausibility | OK | 144 of 14482 ACK receptions (0.99%) have byte5 outside 0x80..0x84; firmware ACK_REJECT markers 32 |
| OnRxDone timing | OK | ONRXDONE_TIME mean 3.9 ms, median 2.0, p95 13.0, max 56.0 ms; 15 of 23542 above 50 ms (MC-WARN ONRXDONE_SLOW) |

## OE3XIR-12  (log: oe3xir.log)

| Item | Value |
|---|---|
| Callsign / GW-ID | OE3XIR-12 / E2661FB8 (from `[KEEP]` line; `[GW];keep` itself carries no call) |
| Hardware | RAK4631 (HW id 9, src/configuration_global.h //Hardware Types) - from own `NEW-*`/`TX_FRAME` HW field |
| Role | Internet gateway + relay: keepalives 1377, uploads GWU 1401, server->mesh injections GWI 146, BEAT replies 1375 |
| Firmware | 4.40a (STAT `fw=` 40a/20260929 = build stamp 20260929; exact build otherwise unknown; repo HEAD stamp is 20261030) |
| LoRa | not logged (no boot block). SF11/BW250/CR4:6 inferred: ToA of the TX lines matches CHANNEL_UTIL tx= (1558 s vs 1558 s); frequency/power not in log (fleet standard 433.175 MHz) |
| Capture span | 2026-10-31 06:47:05 -> 2026-10-31 18:16:30 (11.49 h, 245490 lines; logger wall prefix) |
| Clock | node clock minus logger clock: +1 h (mode) (node on UTC+2 for the whole capture; logger = UTC+1 per NTP) |

### Traffic (receptions, not accepted packets; FCS:0000 excluded)

- Heard frame events 5091 (+ 9112 ACK frames = 64.2% of all receptions; 55 FCS:0000 decoder bailouts excluded). New 1937 / duplicate copies 3154.
- Unique originators 170 (excl. time servers; includes own-call echoes), unique msg_ids 2057. Types: heartbeat/HEY(40) 2365 (46.5%), text(3A) 1947 (38.2%), position(21) 779 (15.3%).
- Decoded airtime by type: {'ACK': 39.6, '@': 28.1, ':': 20.0, '!': 12.3} (ACK frames are 012-byte, ~313 ms each).
- Hop: H field (remaining max_hop) {'H00': 18.2, 'H01': 23.1, 'H02': 25.5, 'H03': 19.5, 'H04': 13.7, 'H15': 0.0}; path length (callsigns) {'1': 11.8, '2': 29.3, '3': 31.9, '4': 16.8, '5': 10.2, '6': 0.1, '7': 0.0}; zero-relay (path length 1) 11.8%.
- Own TX 2598 (226.1/h, 1558.4 s airtime): own 17 / relay 2435 / gateway(server->mesh) 146; by type {'A': 1421, '@': 696, ':': 215, '!': 266}, by prio {'1': 1421, '3': 215, '4': 266, '5': 696}; queue wait median 20081 ms, p95 62511 ms; CAD attempts at TX start {'0': 1299, '1': 535, '2': 339, '3': 124, '4': 99, '5': 63, '6': 30, '7': 32, '8': 18, '9': 18, '10': 12, '11': 8, '12': 5, '13': 4, '14': 5, '15': 1, '16': 1, '18': 3, '23': 2}.

Direct neighbours = last path element (32 distinct; RSSI/SNR belong to this last hop only; RSSI looks clamped at the carpet for weak links):

| Neighbour | n | RSSI med (p10..p90, max) dBm | SNR med | HW | dist km | Note |
|---|---|---|---|---|---|---|
| OE3XWJ-12 | 699 | -100 (-101..-99, -51) | 4 | RAK4631 | n/a | Jauerling |
| OE3XOC-12 | 532 | -104 (-106..-103, -50) | 1 | RAK4631 | n/a | Buchberg |
| OE3REI-14 | 458 | -101 (-102..-100, -52) | 4 | RAK4631 | n/a |  |
| OE3XPA-12 | 457 | -91 (-92..-90, -52) | 6 | RAK4631 | n/a | Meshcom iGate |
| OE3XSI-13 | 448 | -94 (-95..-94, -50) | 6 | TBEAM V1.1 | n/a | Notfunkst. StP |
| OE3KJO-62 | 375 | -108 (-108..-106, -56) | -4 | TLORA V2.1.6 | n/a |  |
| OE3XIA-12 | 329 | -103 (-103..-102, -99) | 3 | RAK4631 | n/a | Exelberg |
| OE1XAR-12 | 299 | -109 (-110..-107, -51) | -12 | RAK4631 | n/a |  |
| OE3WFA-12 | 159 | -107 (-108..-106, -105) | -5 | TBEAM V1.2 | n/a | Eichgraben |
| OE3ZKW-12 | 136 | -109 (-109..-107, -103) | -6 | TLORA V2.1.6 | n/a | at Home |

### Channel / RF

- Noise: RX_ERROR RSSI p5/median -110/-102 dBm; RSSI-SNR (SNR>=0) median -105 dBm; RX_ERROR histogram {'-110': 2762, '-100': 1564, '-90': 77, '-80': 68, '-70': 6, '-60': 237, '-50': 86}.
- CAD: 13261 scans, initial busy 40.7%, confirmed busy 85.0% of initial busy, false positive 15.0%. cad_attempt distribution (TX_GATE_ENTER): 0:41.8% 1:21.7% 2:12.7% >=3:23.8%.
- util by hour (CHANNEL_UTIL mean %): {'10-31 06': 22.2, '10-31 07': 22.1, '10-31 08': 21.9, '10-31 09': 21.7, '10-31 10': 19.6, '10-31 11': 19.8, '10-31 12': 20.2, '10-31 13': 20.8, '10-31 14': 21.5, '10-31 15': 20.5, '10-31 16': 20.5, '10-31 17': 20.8, '10-31 18': 19.6}

### Firmware census of heard originators

- Pre-CAD 17/169 nodes (10.1%), 16.6% of events, 17.1% of airtime. Versions: {'4.40a': 122, '4.35p': 22, '4.35k': 6, '4.35t': 4, '4.35d': 3, '4.35h': 3, '4.35f': 2, '4.0#': 2, '4.35i': 1, '4.35v': 1, '4.35s': 1, '4.34j': 1, '4.34v': 1}. (4.40a counted as post-CAD; `#` letter = sub-version not transmitted; 4.34/4.30 are older than CAD.)

| Top pre-CAD talker | FW | events | msgs | share of node events | HW | place | dist km |
|---|---|---|---|---|---|---|---|
| OE3REI-14 | 4.35k | 164 | 55 | 3.4% | RAK4631 | lat/lon 48.1102,15.2323 | n/a |
| OE3XSI-13 | 4.35k | 144 | 53 | 3.0% | TBEAM V1.1 | Notfunkst. StP | n/a |
| OE3RFA-7 | 4.35h | 65 | 19 | 1.4% | T-Deck | no master data | n/a |
| OE3LXF-71 | 4.35h | 60 | 20 | 1.3% | TBEAM V1.1 | 70cm Yagi southwest | n/a |
| OE1KBC-90 | 4.35d | 60 | 21 | 1.3% | HELTEC E290 | Kurt | n/a |
| OE3XWJ-94 | 4.35d | 59 | 24 | 1.2% | EBYTE E22 | lat/lon 48.469,15.6813 | n/a |
| OE3HUS-01 | 4.35k | 55 | 23 | 1.2% | TBEAM V1.1 | lat/lon 48.0852,16.429 | n/a |
| OE3RDB-2 | 4.35f | 46 | 21 | 1.0% | TLORA V2.1.6 | Bad Vöslau | n/a |

### Gateway / server path

- BEAT miss episodes 11 (12 keepalives = 0.87%); BEAT gaps >65 s: 1 (max 90.2 s); clusters of 'Server not responding': 11.
- Busiest hours for missing BEAT (logger clock): [('10-31 07', 2), ('10-31 09', 2), ('10-31 10', 2), ('10-31 12', 2), ('10-31 13', 2)]
- ETH: IP 44.143.12.136 -> server 44.143.8.143; hb_age_s median 13 / max 36; stalls {}; events {}.

### Verdict table

| Aspect | Status | Comment |
|---|---|---|
| State machine | OK | 4582 x TX_PREPARE->IDLE rc=-1 (CAD found channel busy, normal); 0 other rc!=0 / ERROR; 0 console-interleaved lines ignored |
| Relay | OK | 2435 of 2598 TX are relays (93.7%); RING_OVERFLOW 0, RING_DROP_PRIO/NEW 0; TTL drops 8 (prio5 HEY/prio4 position, by design); ring peak 10/20; RLY refusals {'tx': 1032, 'gwfilter': 552, 'gwcap': 177, 'hop0': 107, 'loop': 2} |
| Hop counter | Beobachten | H field {'H00': 18.2, 'H01': 23.1, 'H02': 25.5, 'H03': 19.5, 'H04': 13.7, 'H15': 0.0}; 1 frames with H>=13 (wrap from foreign builds) |
| Loops | OK | RELAY_LOOP_BLOCKED 2 (gate working, not a fault); 914 receptions (18.0%) carry own call in path (echo of own/relayed frames) |
| Channel util | OK | CHANNEL_UTIL mean 20.8%, median 20.0%, p95 43.3%, peak 71.0% (1 samples >=70%, 0 >=90%); 1st/2nd half 21.0/20.7%. nRF52 (RAK4631): ESP32 255-byte inflation not applicable, fingerprint test negative (16 of 3578 rx values are multiples of 2476 ms). Cross-check: own TX airtime 1558 s (ToA of TX lines) = 1558 s (CHANNEL_UTIL) = 1560 s (STAT); STAT rx 7259 s vs ToA of decoded frames 7213 s (within ~1%) -> util counts decoded frames only, undecodable energy is not in it |
| Retries | OK | RING_STATUS retrying max 0 over 1377 samples; pending max 0; ring high-water 11/20; RETRANSMIT_GIVEUP 0 |
| CRC / RX errors | Beobachten | 4800 RX_ERROR (preamble/header seen, no valid frame) = 417.7/h = 48.3% of (errors + OnRxDone); median RSSI -102.0 dBm, median SNR -4.0 dB -> noise-limited. 397 errors above -85 dBm, 323 above -60 dBm. nRF52 limits: no freq-error (ferr always 0), no RX_IRQ_STALE markers (0) |
| Network (ETH/GW) | OK | keepalive [GW];keep ok 1377 (expected ~1379, max gap 30.8 s); 12 keepalives (0.87%) without server BEAT reply, '[UDP] Server not responding for 35s' 11x; ETH tx_fail 1 (already in the first 60-s summary = boot-time first send, max 1.6 s), DHCP renews 0, renew_fail 0, hw resets 0; NTP ok 46/timeouts 1 |
| Crashes / reboots | OK | uptime monotonic (300 s at first STAT .. 41406 s at last), 0 resets, 0 crash/panic/watchdog hits, boot banners in log: 0; node had booted 89 s before the first logged line (first STAT up=300 s at 2026-10-31 06:45:37 est.) |
| Heap | OK | STAT heap 92252 -> 92252 B (min 92252, max 92252), slope 0.0 B/h; no [HEAP] lines in the stream except boot; flat = no leak signal (nRF52 figure is coarse) |
| CSMA / CAD | Beobachten | TX_GATE_ENTER 7873: attempt0 41.8%, >=3: 23.8%, max 23; CAD initial busy 40.7% of 13261 scans, false positive 15.0% of initial busy; 557 episodes with attempt>=3 (longest 6.5 s); CAD_SAFETY_TIMEOUT 0 (CadDone not seen within 100 ms, radio restarted to RX, no TX that round), CSMA_AGED 2439 |
| Double send | OK | bug-type (same slot, READY then DONE) 0; 8 msg_ids read more than once; recurrences are >4 h apart or legit retransmit copies |
| Firmware without CAD | Beobachten | 17 of 169 originators (10.1%) on pre-CAD (4.35a-o, 4.34 and older); they carry 16.6% of heard frame events, 15.8% of unique msgs, 17.1% of decoded airtime (own-call echoes and time servers excluded) |
| Noise floor | OK | carpet = RX_ERROR RSSI p5 -110 dBm (median -102) -> 4 dB above the -114 dBm BW250 theory; RSSI-SNR estimator (SNR>=0 frames, n=2974) -105 dBm - the two disagree (RSSI looks like total in-band power, clamped at the carpet), see method note |
| Input level neighbours | Beobachten | strongest decoded RSSI -50 dBm; 0 receptions above -20 dBm (overload limit), 0 above -40 dBm; 10-dB histogram {'-110': 9724, '-100': 4209, '-90': 217, '-80': 16, '-70': 6, '-60': 27, '-50': 4}; 323 RX_ERROR + 15 decoded receptions above -60 dBm, spread over every hour of the capture (SNR median -20 dB) -> persistent in-band interferer or strong non-LoRa carrier, not a neighbour |
| Receive silences | OK | median gap between receptions 2.43 s, longest 56 s (at 10-31 11:33:22); 0 silences >300 s |
| Time signal delivery | OK | Internet side ([CET];gw_rx from OE1XAR-33): 68 of ~68 (median gap 607 s), 0 missing; LoRa side: 69 distinct stamps heard, 0 gaps; coverage of OE3 union 100.0% |
| ACK plausibility | OK | 8 of 9112 ACK receptions (0.09%) have byte5 outside 0x80..0x84; firmware ACK_REJECT markers 36 |
| OnRxDone timing | OK | ONRXDONE_TIME mean 3.3 ms, median 2.0, p95 10.0, max 53.0 ms; 10 of 14258 above 50 ms (MC-WARN ONRXDONE_SLOW) |

### OE3XIR extras

- Capture ends 2026-10-31 18:16:30 (logger stop; node healthy until then: last STAT uptime 41406 s).
- Interferer evidence: RX_ERROR above -60 dBm 323 (levels cluster at -50/-51 and -55/-56 dBm, SNR -22..-13 dB), plus 15 decoded frames with inflated RSSI > -60 dBm (SNR median -6 dB), 323 events spread over 13 logger hours (21-35/h in full hours). No other node of the six has a single RX_ERROR above -60 dBm. Fact: persistent strong in-band source at the OE3XIR site. Inference: not LoRa (no freq-error info on nRF52 to confirm).
- Invalid `255.:{ping}`: 689 events, exactly 60/h, ring source `phone_msg:3A` 689. Evidence: logger 06:47:33.947 and following minutes in oe3xir.log.

## OE3XOC-12  (log: oe3xoc.log)

| Item | Value |
|---|---|
| Callsign / GW-ID | OE3XOC-12 / 1AA04497 (from `[KEEP]` line; `[GW];keep` itself carries no call) |
| Hardware | RAK4631 (HW id 9, src/configuration_global.h //Hardware Types) - from own `NEW-*`/`TX_FRAME` HW field |
| Role | Internet gateway + relay: keepalives 3037, uploads GWU 3743, server->mesh injections GWI 327, BEAT replies 3048 |
| Firmware | 4.40a (STAT `fw=` 40a/20260929 = build stamp 20260929; exact build otherwise unknown; repo HEAD stamp is 20261030) |
| LoRa | not logged (no boot block). SF11/BW250/CR4:6 inferred: ToA of the TX lines matches CHANNEL_UTIL tx= (3821 s vs 3818 s); frequency/power not in log (fleet standard 433.175 MHz) |
| Capture span | 2026-10-31 06:31:07 -> 2026-11-01 07:52:09 (25.35 h, 627812 lines; logger wall prefix) |
| Clock | node clock minus logger clock: +0 h (mode) (node on UTC+1 = logger) |

### Traffic (receptions, not accepted packets; FCS:0000 excluded)

- Heard frame events 15971 (+ 27675 ACK frames = 63.4% of all receptions; 104 FCS:0000 decoder bailouts excluded). New 4897 / duplicate copies 11074.
- Unique originators 216 (excl. time servers; includes own-call echoes), unique msg_ids 4586. Types: heartbeat/HEY(40) 7666 (48.0%), text(3A) 5943 (37.2%), position(21) 2362 (14.8%).
- Decoded airtime by type: {'ACK': 38.4, '@': 29.6, ':': 19.7, '!': 12.3} (ACK frames are 012-byte, ~313 ms each).
- Hop: H field (remaining max_hop) {'H00': 19.5, 'H01': 23.8, 'H02': 25.6, 'H03': 21.0, 'H04': 10.1, 'H15': 0.0}; path length (callsigns) {'1': 9.8, '2': 28.6, '3': 32.4, '4': 18.3, '5': 10.9, '6': 0.1, '7': 0.0}; zero-relay (path length 1) 9.8%.
- Own TX 6276 (247.6/h, 3821.2 s airtime): own 56 / relay 5893 / gateway(server->mesh) 327; by type {'A': 3378, ':': 397, '@': 1704, '!': 797}, by prio {'1': 3378, '3': 397, '4': 797, '5': 1704}; queue wait median 22354 ms, p95 70804 ms; CAD attempts at TX start {'0': 2979, '1': 1476, '2': 792, '3': 322, '4': 224, '5': 138, '6': 110, '7': 79, '8': 42, '9': 40, '10': 25, '11': 12, '12': 13, '13': 8, '14': 3, '15': 6, '16': 3, '18': 1, '19': 1, '20': 1, '30': 1}.

Direct neighbours = last path element (30 distinct; RSSI/SNR belong to this last hop only; RSSI looks clamped at the carpet for weak links):

| Neighbour | n | RSSI med (p10..p90, max) dBm | SNR med | HW | dist km | Note |
|---|---|---|---|---|---|---|
| OE3XPA-12 | 1369 | -100 (-101..-99, -77) | 5 | RAK4631 | 34.6 | Meshcom iGate |
| OE3WFA-12 | 1279 | -76 (-77..-76, -71) | 7 | TBEAM V1.2 | 5.1 | Eichgraben |
| OE3XWJ-12 | 1275 | -111 (-112..-108, -98) | -9 | RAK4631 | 46.6 | Jauerling |
| OE3BCB-10 | 1234 | -85 (-91..-85, -72) | 6 | HELTEC V3 | 3.9 | WX |
| OE3XIA-12 | 1026 | -109 (-109..-107, -103) | -2 | RAK4631 | 22.6 | Exelberg |
| OE3REI-14 | 1003 | -111 (-111..-107, -104) | -7 | RAK4631 | 54.1 |  |
| OE3KJO-64 | 862 | -109 (-109..-106, -80) | -1 | TBEAM V1.1 | 34.2 |  |
| OE3XIR-12 | 853 | -103 (-105..-102, -101) | 3 | RAK4631 | n/a |  |
| OE3KJO-62 | 739 | -111 (-112..-108, -76) | -12 | TLORA V2.1.6 | 34.2 |  |
| OE3XDC-12 | 640 | -108 (-109..-106, -97) | -1 | TTGO LoRa32 V2.1 1.6 | n/a |  |

### Channel / RF

- Noise: RX_ERROR RSSI p5/median -112/-107 dBm; RSSI-SNR (SNR>=0) median -101 dBm; RX_ERROR histogram {'-120': 3243, '-110': 4276, '-100': 2611, '-90': 798, '-80': 1311}.
- CAD: 24574 scans, initial busy 40.2%, confirmed busy 85.4% of initial busy, false positive 14.6%. cad_attempt distribution (TX_GATE_ENTER): 0:42.7% 1:22.4% 2:12.4% >=3:22.5%.
- util by hour (CHANNEL_UTIL mean %): {'10-31 06': 29.4, '10-31 07': 29.6, '10-31 08': 30.8, '10-31 09': 29.5, '10-31 10': 26.3, '10-31 11': 30.0, '10-31 12': 30.4, '10-31 13': 28.9, '10-31 14': 30.8, '10-31 15': 29.5, '10-31 16': 30.3, '10-31 17': 23.3, '10-31 18': 30.3, '10-31 19': 28.7, '10-31 20': 29.9, '10-31 21': 27.1, '10-31 22': 27.3, '10-31 23': 27.1, '11-01 00': 27.1, '11-01 01': 27.0, '11-01 02': 27.6, '11-01 03': 28.4, '11-01 04': 27.8, '11-01 05': 28.5, '11-01 06': 27.8, '11-01 07': 28.8}

### Firmware census of heard originators

- Pre-CAD 19/215 nodes (8.8%), 16.4% of events, 16.8% of airtime. Versions: {'4.40a': 159, '4.35p': 28, '4.35k': 6, '4.35t': 5, '4.35d': 4, '4.35f': 3, '4.35h': 3, '4.0#': 2, '4.35i': 1, '4.35v': 1, '4.35s': 1, '4.35n': 1, '4.34v': 1}. (4.40a counted as post-CAD; `#` letter = sub-version not transmitted; 4.34/4.30 are older than CAD.)

| Top pre-CAD talker | FW | events | msgs | share of node events | HW | place | dist km |
|---|---|---|---|---|---|---|---|
| OE3REI-14 | 4.35k | 545 | 123 | 3.7% | RAK4631 | lat/lon 48.1102,15.2323 | 54.1 |
| OE3XSI-13 | 4.35k | 437 | 113 | 3.0% | TBEAM V1.1 | Notfunkst. StP | 23.7 |
| OE3XWJ-94 | 4.35d | 355 | 96 | 2.4% | EBYTE E22 | lat/lon 48.469,15.6813 | 34.4 |
| OE1KBC-90 | 4.35d | 210 | 61 | 1.4% | HELTEC E290 | Kurt | 35.6 |
| OE3LXF-71 | 4.35h | 154 | 45 | 1.0% | TBEAM V1.1 | 70cm Yagi southwest | 34.4 |
| OE3HUS-01 | 4.35k | 125 | 53 | 0.8% | TBEAM V1.1 | lat/lon 48.0852,16.429 | 38.7 |
| OE3RDB-2 | 4.35f | 119 | 45 | 0.8% | TLORA V2.1.6 | Bad Vöslau | 33.3 |
| OE1NHU-22 | 4.35k | 99 | 38 | 0.7% | T-DECK | lat/lon 48.2843,16.3887 | 33.8 |

### Gateway / server path

- BEAT miss episodes 2 (2 keepalives = 0.07%); BEAT gaps >65 s: 0 (max 0 s); clusters of 'Server not responding': 1.
- Busiest hours for missing BEAT (logger clock): [('10-31 07', 1), ('11-01 03', 1)]
- ETH: IP 44.143.20.20 -> server 44.143.8.143; hb_age_s median 13 / max 30; stalls {"dhcp_maintain/mcloop": [304, 66.0]}; events {'dhcp': 304}.

### Verdict table

| Aspect | Status | Comment |
|---|---|---|
| State machine | OK | 8429 x TX_PREPARE->IDLE rc=-1 (CAD found channel busy, normal); 0 other rc!=0 / ERROR; 2 console-interleaved lines ignored |
| Relay | OK | 5893 of 6276 TX are relays (93.9%); RING_OVERFLOW 0, RING_DROP_PRIO/NEW 0; TTL drops 124 (prio5 HEY/prio4 position, by design); ring peak 11/20; RLY refusals {'tx': 2592, 'gwfilter': 1427, 'gwcap': 320, 'hop0': 175, 'cet': 2, 'nomesh': 1} |
| Hop counter | Beobachten | H field {'H00': 19.5, 'H01': 23.8, 'H02': 25.6, 'H03': 21.0, 'H04': 10.1, 'H15': 0.0}; 3 frames with H>=13 (wrap from foreign builds) |
| Loops | OK | RELAY_LOOP_BLOCKED 0 (gate working, not a fault); 3562 receptions (22.3%) carry own call in path (echo of own/relayed frames) |
| Channel util | OK | CHANNEL_UTIL mean 28.5%, median 28.0%, p95 49.0%, peak 85.0% (10 samples >=70%, 0 >=90%); 1st/2nd half 29.1/27.9%. nRF52 (RAK4631): ESP32 255-byte inflation not applicable, fingerprint test negative (74 of 8880 rx values are multiples of 2476 ms). Cross-check: own TX airtime 3821 s (ToA of TX lines) = 3818 s (CHANNEL_UTIL) = 3808 s (STAT); STAT rx 22655 s vs ToA of decoded frames 22571 s (within ~1%) -> util counts decoded frames only, undecodable energy is not in it |
| Retries | OK | RING_STATUS retrying max 0 over 3036 samples; pending max 0; ring high-water 12/20; RETRANSMIT_GIVEUP 0 |
| CRC / RX errors | Beobachten | 12239 RX_ERROR (preamble/header seen, no valid frame) = 482.8/h = 43.3% of (errors + OnRxDone); median RSSI -107.0 dBm, median SNR -4.0 dB -> noise-limited. 1390 errors above -85 dBm, 0 above -60 dBm. nRF52 limits: no freq-error (ferr always 0), no RX_IRQ_STALE markers (0) |
| Network (ETH/GW) | OK | keepalive [GW];keep ok 3037 (expected ~3042, max gap 32.6 s); 2 keepalives (0.07%) without server BEAT reply, '[UDP] Server not responding for 35s' 1x; ETH tx_fail 0, DHCP renews 303, renew_fail 0, hw resets 0; NTP ok 101/timeouts 0 |
| Crashes / reboots | OK | uptime monotonic (300 s at first STAT .. 91215 s at last), 0 resets, 0 crash/panic/watchdog hits, boot banners in log: 0; node had booted 83 s before the first logged line (first STAT up=300 s at 2026-10-31 06:29:44 est.) |
| Heap | OK | STAT heap 92268 -> 91200 B (min 91200, max 92268), slope -13.4 B/h; no [HEAP] lines in the stream except boot; flat = no leak signal (nRF52 figure is coarse); one step -1068 B at 10-31 07:59:45, then flat for 287 samples (not a trend) |
| CSMA / CAD | Beobachten | TX_GATE_ENTER 14711: attempt0 42.7%, >=3: 22.5%, max 30; CAD initial busy 40.2% of 24574 scans, false positive 14.6% of initial busy; 1029 episodes with attempt>=3 (longest 8.6 s); CAD_SAFETY_TIMEOUT 1 (CadDone not seen within 100 ms, radio restarted to RX, no TX that round), CSMA_AGED 7473 |
| Double send | OK | bug-type (same slot, READY then DONE) 0; 6 msg_ids read more than once; recurrences are >4 h apart or legit retransmit copies |
| Firmware without CAD | Beobachten | 19 of 215 originators (8.8%) on pre-CAD (4.35a-o, 4.34 and older); they carry 16.4% of heard frame events, 17.3% of unique msgs, 16.8% of decoded airtime (own-call echoes and time servers excluded) |
| Noise floor | OK | carpet = RX_ERROR RSSI p5 -112 dBm (median -107) -> 2 dB above the -114 dBm BW250 theory; RSSI-SNR estimator (SNR>=0 frames, n=6183) -101 dBm - the two disagree (RSSI looks like total in-band power, clamped at the carpet), see method note |
| Input level neighbours | OK | strongest decoded RSSI -71 dBm; 0 receptions above -20 dBm (overload limit), 0 above -40 dBm; 10-dB histogram {'-120': 12946, '-110': 14343, '-100': 10921, '-90': 2456, '-80': 2980} |
| Receive silences | OK | median gap between receptions 2.27 s, longest 133 s (at 10-31 17:50:55); 0 silences >300 s |
| Time signal delivery | OK | Internet side ([CET];gw_rx from OE1XAR-33): 151 of ~151 (median gap 607 s), 0 missing; LoRa side: 151 distinct stamps heard, 0 gaps; coverage of OE3 union 100.0% |
| ACK plausibility | OK | 36 of 27675 ACK receptions (0.13%) have byte5 outside 0x80..0x84; firmware ACK_REJECT markers 64 |
| OnRxDone timing | OK | ONRXDONE_TIME mean 3.6 ms, median 2.0, p95 11.0, max 142.0 ms; 38 of 43723 above 50 ms (MC-WARN ONRXDONE_SLOW) |

## OE3XWJ-12  (log: oe3xwj.log)

| Item | Value |
|---|---|
| Callsign / GW-ID | OE3XWJ-12 / 64FD739C (from `[KEEP]` line; `[GW];keep` itself carries no call) |
| Hardware | RAK4631 (HW id 9, src/configuration_global.h //Hardware Types) - from own `NEW-*`/`TX_FRAME` HW field |
| Role | Internet gateway + relay: keepalives 3055, uploads GWU 1959, server->mesh injections GWI 290, BEAT replies 3033 |
| Firmware | 4.40a (STAT `fw=` 40a/20260929 = build stamp 20260929; exact build otherwise unknown; repo HEAD stamp is 20261030) |
| LoRa | not logged (no boot block). SF11/BW250/CR4:6 inferred: ToA of the TX lines matches CHANNEL_UTIL tx= (2976 s vs 2973 s); frequency/power not in log (fleet standard 433.175 MHz) |
| Capture span | 2026-10-31 06:27:07 -> 2026-11-01 07:57:37 (25.51 h, 423181 lines; logger wall prefix) |
| Clock | node clock minus logger clock: +0 h (mode) (node on UTC+1 = logger) |

### Traffic (receptions, not accepted packets; FCS:0000 excluded)

- Heard frame events 10864 (+ 20166 ACK frames = 65.0% of all receptions; 67 FCS:0000 decoder bailouts excluded). New 6239 / duplicate copies 4625.
- Unique originators 215 (excl. time servers; includes own-call echoes), unique msg_ids 3796. Types: text(3A) 5134 (47.3%), heartbeat/HEY(40) 4432 (40.8%), position(21) 1298 (11.9%).
- Decoded airtime by type: {'ACK': 41.2, '@': 24.9, ':': 24.2, '!': 9.7} (ACK frames are 012-byte, ~313 ms each).
- Hop: H field (remaining max_hop) {'H00': 15.6, 'H01': 22.2, 'H02': 22.8, 'H03': 33.3, 'H04': 6.0, 'H15': 0.0}; path length (callsigns) {'1': 4.8, '2': 41.0, '3': 28.1, '4': 16.1, '5': 9.7, '6': 0.2}; zero-relay (path length 1) 4.8%.
- Own TX 3900 (152.9/h, 2976.1 s airtime): own 1575 / relay 2035 / gateway(server->mesh) 290; by type {'A': 50, ':': 1906, '!': 535, '@': 1409}, by prio {'1': 50, '2': 1529, '3': 377, '4': 535, '5': 1409}; queue wait median 9135 ms, p95 22300 ms; CAD attempts at TX start {'0': 2677, '1': 808, '2': 254, '3': 79, '4': 38, '5': 16, '6': 14, '7': 7, '8': 2, '9': 3, '10': 1, '14': 1}.

Direct neighbours = last path element (20 distinct; RSSI/SNR belong to this last hop only; RSSI looks clamped at the carpet for weak links):

| Neighbour | n | RSSI med (p10..p90, max) dBm | SNR med | HW | dist km | Note |
|---|---|---|---|---|---|---|
| OE3REI-14 | 2451 | -89 (-91..-89, -72) | 5 | RAK4631 | 26.8 |  |
| OE3XPA-12 | 1633 | -98 (-99..-97, -91) | 2 | RAK4631 | 34.3 | Meshcom iGate |
| OE3AJC-12 | 1287 | -104 (-106..-101, -89) | -11 | TLORA V2.1.6 | 61.4 | via MeshCom; QRV |
| OE3XSI-13 | 1079 | -104 (-105..-101, -94) | -10 | TBEAM V1.1 | 25.2 | Notfunkst. StP |
| OE3XIR-12 | 920 | -99 (-100..-98, -89) | 2 | RAK4631 | n/a |  |
| OE1XAR-12 | 875 | -104 (-105..-101, -94) | -9 | RAK4631 | n/a |  |
| OE3XOC-12 | 609 | -105 (-106..-102, -99) | -15 | RAK4631 | 46.6 | Buchberg |
| OE3MIF-12 | 523 | -103 (-104..-101, -94) | -5 | n/a | n/a |  |
| OE3XKN-1 | 431 | -105 (-106..-102, -94) | -13 | RAK4631 | 26.3 | Klangturm |
| OE3XWJ-94 | 303 | -105 (-106..-102, -101) | -15 | EBYTE E22 | 28.8 |  |

### Channel / RF

- Noise: RX_ERROR RSSI p5/median -106/-102 dBm; RSSI-SNR (SNR>=0) median -98 dBm; RX_ERROR histogram {'-110': 4530, '-100': 2141, '-90': 927, '-80': 85, '-70': 14}.
- CAD: 8529 scans, initial busy 31.0%, confirmed busy 75.0% of initial busy, false positive 25.0%. cad_attempt distribution (TX_GATE_ENTER): 0:66.4% 1:20.7% 2:7.0% >=3:5.9%.
- util by hour (CHANNEL_UTIL mean %): {'10-31 06': 22.0, '10-31 07': 23.4, '10-31 08': 24.1, '10-31 09': 23.8, '10-31 10': 18.5, '10-31 11': 17.6, '10-31 12': 17.7, '10-31 13': 20.4, '10-31 14': 21.8, '10-31 15': 21.2, '10-31 16': 21.7, '10-31 17': 22.6, '10-31 18': 21.1, '10-31 19': 12.4, '10-31 20': 17.3, '10-31 21': 15.9, '10-31 22': 18.6, '10-31 23': 16.8, '11-01 00': 16.7, '11-01 01': 17.1, '11-01 02': 18.4, '11-01 03': 19.7, '11-01 04': 18.5, '11-01 05': 20.9, '11-01 06': 21.2, '11-01 07': 21.4}

### Firmware census of heard originators

- Pre-CAD 19/214 nodes (8.9%), 23.8% of events, 22.3% of airtime. Versions: {'4.40a': 161, '4.35p': 25, '4.35k': 7, '4.35t': 5, '4.35f': 3, '4.35d': 3, '4.35h': 3, '4.0#': 2, '4.35i': 1, '4.35v': 1, '4.35s': 1, '4.35n': 1, '4.34v': 1}. (4.40a counted as post-CAD; `#` letter = sub-version not transmitted; 4.34/4.30 are older than CAD.)

| Top pre-CAD talker | FW | events | msgs | share of node events | HW | place | dist km |
|---|---|---|---|---|---|---|---|
| OE3REI-14 | 4.35k | 395 | 130 | 6.0% | RAK4631 | lat/lon 48.1102,15.2323 | 26.8 |
| OE3XSI-13 | 4.35k | 383 | 121 | 5.8% | TBEAM V1.1 | Notfunkst. StP | 25.2 |
| OE3XWJ-94 | 4.35d | 172 | 61 | 2.6% | EBYTE E22 | lat/lon 48.469,15.6813 | 28.8 |
| OE1KBC-90 | 4.35d | 106 | 44 | 1.6% | HELTEC E290 | Kurt | 79.5 |
| OE3DJB-12 | 4.35h | 86 | 36 | 1.3% | TLORA V2.1.6 | MeshCom | 33.2 |
| OE3RDB-2 | 4.35f | 73 | 40 | 1.1% | TLORA V2.1.6 | Bad Vöslau | 76.4 |
| OE3LXF-71 | 4.35h | 73 | 27 | 1.1% | TBEAM V1.1 | 70cm Yagi southwest | 57.2 |
| OE3HUS-01 | 4.35k | 63 | 34 | 1.0% | TBEAM V1.1 | lat/lon 48.0852,16.429 | 85.2 |

### Gateway / server path

- BEAT miss episodes 41 (43 keepalives = 1.41%); BEAT gaps >65 s: 2 (max 90.2 s); clusters of 'Server not responding': 37.
- Busiest hours for missing BEAT (logger clock): [('10-31 20', 7), ('10-31 21', 6), ('10-31 22', 5), ('10-31 23', 3), ('11-01 02', 3)]
- ETH: IP 44.143.72.20 -> server 44.143.8.143; hb_age_s median 13 / max 58; stalls {"udp_rx/mcloop": [2, 498.0], "ntp/mcloop": [1, 202.0]}; events {'link': 18}.

### Verdict table

| Aspect | Status | Comment |
|---|---|---|
| State machine | OK | 1984 x TX_PREPARE->IDLE rc=-1 (CAD found channel busy, normal); 0 other rc!=0 / ERROR; 0 console-interleaved lines ignored |
| Relay | OK | 2035 of 3900 TX are relays (52.2%); RING_OVERFLOW 0, RING_DROP_PRIO/NEW 0; TTL drops 4 (prio5 HEY/prio4 position, by design); ring peak 8/20; RLY refusals {'tx': 1986, 'gwcap': 290, 'hop0': 178, 'gwfilter': 83, 'cet': 6, 'loop': 5} |
| Hop counter | Beobachten | H field {'H00': 15.6, 'H01': 22.2, 'H02': 22.8, 'H03': 33.3, 'H04': 6.0, 'H15': 0.0}; 2 frames with H>=13 (wrap from foreign builds) |
| Loops | OK | RELAY_LOOP_BLOCKED 5 (gate working, not a fault); 5591 receptions (51.5%) carry own call in path (echo of own/relayed frames) |
| Channel util | OK | CHANNEL_UTIL mean 19.6%, median 19.0%, p95 38.0%, peak 69.0% (0 samples >=70%, 0 >=90%); 1st/2nd half 21.1/18.1%. nRF52 (RAK4631): ESP32 255-byte inflation not applicable, fingerprint test negative (65 of 8489 rx values are multiples of 2476 ms). Cross-check: own TX airtime 2976 s (ToA of TX lines) = 2973 s (CHANNEL_UTIL) = 2974 s (STAT); STAT rx 15399 s vs ToA of decoded frames 15346 s (within ~1%) -> util counts decoded frames only, undecodable energy is not in it |
| Retries | OK | RING_STATUS retrying max 0 over 3055 samples; pending max 0; ring high-water 9/20; RETRANSMIT_GIVEUP 0 |
| CRC / RX errors | Beobachten | 7697 RX_ERROR (preamble/header seen, no valid frame) = 301.7/h = 41.4% of (errors + OnRxDone); median RSSI -102.0 dBm, median SNR -13.0 dB -> noise-limited. 104 errors above -85 dBm, 0 above -60 dBm. nRF52 limits: no freq-error (ferr always 0), no RX_IRQ_STALE markers (0) |
| Network (ETH/GW) | Beobachten | keepalive [GW];keep ok 3055 (expected ~3061, max gap 31.0 s); 43 keepalives (1.41%) without server BEAT reply, '[UDP] Server not responding for 35s' 39x; ETH tx_fail 1 (already in the first 60-s summary = boot-time first send, max 1.6 s), DHCP renews 0, renew_fail 0, hw resets 0; 9 ETH link flaps (1-4 s); NTP ok 102/timeouts 4 |
| Crashes / reboots | OK | uptime monotonic (300 s at first STAT .. 91815 s at last), 0 resets, 0 crash/panic/watchdog hits, boot banners in log: 0; node had booted 184 s before the first logged line (first STAT up=300 s at 2026-10-31 06:24:04 est.) |
| Heap | OK | STAT heap 92264 -> 92264 B (min 92264, max 92264), slope 0.0 B/h; no [HEAP] lines in the stream except boot; flat = no leak signal (nRF52 figure is coarse) |
| CSMA / CAD | Beobachten | TX_GATE_ENTER 5909: attempt0 66.4%, >=3: 5.9%, max 14; CAD initial busy 31.0% of 8529 scans, false positive 25.0% of initial busy; 161 episodes with attempt>=3 (longest 3.7 s); CAD_SAFETY_TIMEOUT 24 (CadDone not seen within 100 ms, radio restarted to RX, no TX that round), CSMA_AGED 122 |
| Double send | OK | bug-type (same slot, READY then DONE) 0; 584 msg_ids read more than once; own ping ids recur ~16 h apart (e.g. F5CE712E at 06:27:25 and 22:37:25): ID reuse >4 h, not a defect |
| Firmware without CAD | Beobachten | 19 of 214 originators (8.9%) on pre-CAD (4.35a-o, 4.34 and older); they carry 23.8% of heard frame events, 22.4% of unique msgs, 22.3% of decoded airtime (own-call echoes and time servers excluded) |
| Noise floor | Beobachten | carpet = RX_ERROR RSSI p5 -106 dBm (median -102) -> 8 dB above the -114 dBm BW250 theory; RSSI-SNR estimator (SNR>=0 frames, n=4667) -98 dBm - the two disagree (RSSI looks like total in-band power, clamped at the carpet), see method note |
| Input level neighbours | OK | strongest decoded RSSI -72 dBm; 0 receptions above -20 dBm (overload limit), 0 above -40 dBm; 10-dB histogram {'-110': 16195, '-100': 11240, '-90': 3345, '-80': 250} |
| Receive silences | Beobachten | median gap between receptions 3.24 s, longest 153 s (at 10-31 17:03:38); 0 silences >300 s; the 153 s silence at 10-31 17:03:38 has 22 CAD_SAFETY_TIMEOUT (CadDone never fired, src/nrf52/nrf52_main.cpp:1790), 0 own TX, and two 498 ms `[ETH];stall;udp_rx` (17:04:54, 17:05:56); the radio recovered by itself at 17:06:06 (fact); shared SPI/IRQ starvation is a hypothesis |
| Time signal delivery | OK | Internet side ([CET];gw_rx from OE1XAR-33): 149 of ~151 (median gap 607 s), 2 missing; LoRa side: 147 distinct stamps heard, 4 gaps; coverage of OE3 union 97.4% |
| ACK plausibility | OK | 24 of 20166 ACK receptions (0.12%) have byte5 outside 0x80..0x84; firmware ACK_REJECT markers 47 |
| OnRxDone timing | OK | ONRXDONE_TIME mean 3.9 ms, median 3.0, p95 11.0, max 66.0 ms; 14 of 31097 above 50 ms (MC-WARN ONRXDONE_SLOW) |

### OE3XWJ extras

- OE3XWJ-12 sends a ping DM every 60 s (`[PING]...send Ping to 2025`, 1529 lines, ring source `phone_msg:3A` 1529, prio 2). Its msg_ids F5CE7xxx account for 98.9% of ACK receptions at this node and 97-99% at the other OE3 nodes (19946 receptions, 4293 distinct ACK ids for 1529 pings = ~2.8 ACKs per ping, each heard ~13x).
- ETH link flaps: 9 (1-4 s each, IP kept; no DHCP events or got_ip at all, inference: static addressing); all 9 flaps fall between 10-31 18:18 and 11-01 05:33 (none in the first 12 h); BEAT misses do not follow them (2 of 9 flaps have a miss within -90..+180 s, both before the flap; flap times in results.json eth.events_first).

## DB0AIS-12  (log: db0ais.log)

| Item | Value |
|---|---|
| Callsign / GW-ID | DB0AIS-12 / D69A080A (from `[KEEP]` line; `[GW];keep` itself carries no call) |
| Hardware | RAK4631 (HW id 9, src/configuration_global.h //Hardware Types) - from own `NEW-*`/`TX_FRAME` HW field |
| Role | Internet gateway + relay: keepalives 3013, uploads GWU 2135, server->mesh injections GWI 356, BEAT replies 2936 |
| Firmware | 4.40a (STAT `fw=` 40a/20260929 = build stamp 20260929; exact build otherwise unknown; repo HEAD stamp is 20261030) |
| LoRa | 433.175 MHz, BW 250 kHz, SF11, CR 4/6, preamble 8, TX 22 dBm (boot block `[LoRa]...RF_*`) |
| Capture span | 2026-10-30 17:01:11 -> 2026-10-31 18:09:56 (25.15 h, 312558 lines; derived from node millis + PuTTY start 17:01:11 (CET)) |
| Clock | node clock = UTC+2 from boot until ~18:20:3x logger time, then UTC+1 (step of exactly -1 h, no log line names a cause); logger = PuTTY PC clock UTC+1 |

### Traffic (receptions, not accepted packets; FCS:0000 excluded)

- Heard frame events 6395 (+ 310 ACK frames = 4.6% of all receptions; 10 FCS:0000 decoder bailouts excluded). New 3569 / duplicate copies 2826.
- Unique originators 239 (excl. time servers; includes own-call echoes), unique msg_ids 3616. Types: heartbeat/HEY(40) 3454 (54.0%), text(3A) 1770 (27.7%), position(21) 1171 (18.3%).
- Decoded airtime by type: {'@': 46.3, ':': 27.4, '!': 24.6, 'ACK': 1.7} (ACK frames are 012-byte, ~313 ms each).
- Hop: H field (remaining max_hop) {'H00': 21.9, 'H01': 21.8, 'H02': 23.8, 'H03': 20.4, 'H04': 12.0}; path length (callsigns) {'1': 7.4, '2': 31.0, '3': 39.2, '4': 15.0, '5': 7.0, '6': 0.4}; zero-relay (path length 1) 7.4%.
- Own TX 2832 (112.6/h, 2532.9 s airtime): own 219 / relay 2257 / gateway(server->mesh) 356; by type {'@': 1584, ':': 818, '!': 365, 'A': 65}, by prio {'1': 66, '3': 817, '4': 365, '5': 1584}; queue wait median 6923 ms, p95 12600 ms; CAD attempts at TX start {'0': 1356, '1': 874, '2': 336, '3': 89, '4': 52, '5': 43, '6': 31, '7': 11, '8': 15, '9': 10, '10': 6, '11': 4, '12': 1, '13': 2, '14': 2}.

Direct neighbours = last path element (21 distinct; RSSI/SNR belong to this last hop only; RSSI looks clamped at the carpet for weak links):

| Neighbour | n | RSSI med (p10..p90, max) dBm | SNR med | HW | dist km | Note |
|---|---|---|---|---|---|---|
| DB0HRF-12 | 2092 | -98 (-98..-75, -74) | 2 | TBEAM V1.2 | 27.8 | Großer Feldberg JO40 |
| DB0MW-12 | 1781 | -85 (-86..-72, -71) | 6 | TLORA V2.1.6 | 3.5 | Moerfelden JN49GX |
| DM9PT-20 | 1362 | -97 (-98..-75, -75) | 3 | EBYTE E22 | 28.0 | Solar Node JN49ET |
| DH2PC-20 | 173 | -102 (-104..-76, -75) | -3 | EBYTE E22 | 20.6 |  |
| DL2IAS-10 | 162 | -103 (-104..-78, -75) | -3 | EBYTE E22 | 21.8 | Mainz |
| DF6PC-20 | 162 | -103 (-104..-76, -75) | -3 | EBYTE E22 | 27.7 | Christian |
| DL0BOD-20 | 160 | -102 (-103..-78, -75) | -1 | EBYTE E22 | 21.6 | Meshcom Bodenheim |
| DK4RS-20 | 92 | -106 (-107..-88, -75) | -11 | EBYTE E22 | 25.1 | DK4RS-20 |
| DK6PR-20 | 90 | -105 (-106..-85, -75) | -7 | EBYTE E22 | 28.8 | K46 |
| DG6FBX-1 | 80 | -106 (-107..-101, -75) | -15 | TLORA V2.1.6 | 18.6 | Leeheim, JN49FU |

### Channel / RF

- Noise: RX_ERROR RSSI p5/median -107/-102 dBm; RSSI-SNR (SNR>=0) median -99 dBm; RX_ERROR histogram {'-110': 8380, '-100': 2401, '-90': 349, '-80': 1822}.
- CAD: 9236 scans, initial busy 38.2%, confirmed busy 81.7% of initial busy, false positive 18.3%. cad_attempt distribution (TX_GATE_ENTER): 0:49.6% 1:25.8% 2:10.5% >=3:14.0%.
- util by hour (CHANNEL_UTIL mean %): {'10-30 17': 9.3, '10-30 18': 11.4, '10-30 19': 10.9, '10-30 20': 9.7, '10-30 21': 9.8, '10-30 22': 10.9, '10-30 23': 8.0, '10-31 00': 8.1, '10-31 01': 8.3, '10-31 02': 8.4, '10-31 03': 8.4, '10-31 04': 9.4, '10-31 05': 8.4, '10-31 06': 8.7, '10-31 07': 8.8, '10-31 08': 8.1, '10-31 09': 9.2, '10-31 10': 8.9, '10-31 11': 7.6, '10-31 12': 8.5, '10-31 13': 7.9, '10-31 14': 9.0, '10-31 15': 8.8, '10-31 16': 8.8, '10-31 17': 6.6, '10-31 18': 9.5}

### Firmware census of heard originators

- Pre-CAD 20/238 nodes (8.4%), 9.9% of events, 10.2% of airtime. Versions: {'4.40a': 180, '4.35p': 29, '4.35f': 5, '4.35k': 5, '4.35o': 3, '4.35i': 2, '4.35c': 2, '4.35t': 2, '4.35v': 2, '4.0#': 2, '4.35s': 1, '4.35d': 1, '4.35e': 1, '4.35h': 1, '4.35u': 1, '4.40A': 1}. (4.40a counted as post-CAD; `#` letter = sub-version not transmitted; 4.34/4.30 are older than CAD.)

| Top pre-CAD talker | FW | events | msgs | share of node events | HW | place | dist km |
|---|---|---|---|---|---|---|---|
| DM1MT-1 | 4.35f | 135 | 117 | 2.7% | TBEAM V1.2 | Meshcom Node DM1MT | 29.4 |
| DG6FBX-1 | 4.35d | 128 | 67 | 2.5% | TLORA V2.1.6 | Leeheim, JN49FU | 18.6 |
| DM1MT-14 | 4.35o | 52 | 36 | 1.0% | HELTEC V3 | Meshcom Schoeneck | 29.4 |
| DL2MK-12 | 4.35k | 45 | 30 | 0.9% | HELTEC V3 | lat/lon 50.1035,8.9263 | 26.6 |
| DB5COA-21 | 4.35k | 44 | 33 | 0.9% | TBEAM SUPREME | lat/lon 49.8398,8.2297 | 31.3 |
| DF3GL-12 | 4.35c | 18 | 12 | 0.4% | TLORA V2.1.6 | Kassel | 158.3 |
| DK7WK-12 | 4.35f | 15 | 12 | 0.3% | TBEAM V1.2 | Georg JO40PS | 97.4 |
| DN7TMU-12 | 4.35e | 14 | 11 | 0.3% | TLORA V2.1.6 | lat/lon 48.6537,9.1975 | 155.7 |

### Gateway / server path

- BEAT miss episodes 66 (77 keepalives = 2.56%); BEAT gaps >65 s: 8 (max 150.4 s); clusters of 'Server not responding': 38.
- Busiest hours for missing BEAT (logger clock): [('10-31 01', 23), ('10-31 02', 20), ('10-31 00', 11), ('10-30 23', 10), ('10-30 22', 7)]
- ETH: IP 44.149.189.136 -> server 44.148.230.197; hb_age_s median 12 / max 59; stalls {"ntp/mcloop": [2, 404.0], "hw_init/loop": [1, 199.0], "dhcp_begin/loop": [1, 4171.0], "udp_tx/mcloop": [1, 613.0]}; events {'rng_seed': 1, 'got_ip': 1, 'link': 5}.

### Verdict table

| Aspect | Status | Comment |
|---|---|---|
| State machine | OK | 2879 x TX_PREPARE->IDLE rc=-1 (CAD found channel busy, normal); 0 other rc!=0 / ERROR; 0 console-interleaved lines ignored |
| Relay | OK | 2257 of 2832 TX are relays (79.7%); RING_OVERFLOW 0, RING_DROP_PRIO/NEW 0; TTL drops 0 (prio5 HEY/prio4 position, by design); ring peak 3/20; RLY refusals {'tx': 2196, 'gwcap': 612, 'hop0': 264, 'cet': 4, 'nomesh': 2} |
| Hop counter | OK | H field {'H00': 21.9, 'H01': 21.8, 'H02': 23.8, 'H03': 20.4, 'H04': 12.0}; no H>=13 wrap |
| Loops | OK | RELAY_LOOP_BLOCKED 0 (gate working, not a fault); 1547 receptions (24.2%) carry own call in path (echo of own/relayed frames) |
| Channel util | OK | CHANNEL_UTIL mean 8.9%, median 8.0%, p95 25.0%, peak 77.0% (1 samples >=70%, 0 >=90%); 1st/2nd half 9.3/8.4%. nRF52 (RAK4631): ESP32 255-byte inflation not applicable, fingerprint test negative (0 of 4975 rx values are multiples of 2476 ms). Cross-check: own TX airtime 2533 s (ToA of TX lines) = 2531 s (CHANNEL_UTIL) = 2521 s (STAT); STAT rx 5793 s vs ToA of decoded frames 5784 s (within ~1%) -> util counts decoded frames only, undecodable energy is not in it |
| Retries | OK | RING_STATUS retrying max 0 over 3012 samples; pending max 0; ring high-water 4/20; RETRANSMIT_GIVEUP 0 |
| CRC / RX errors | Beobachten | 12952 RX_ERROR (preamble/header seen, no valid frame) = 515.1/h = 66.9% of (errors + OnRxDone); median RSSI -102.0 dBm, median SNR -9.0 dB -> noise-limited. 1847 errors above -85 dBm, 0 above -60 dBm. nRF52 limits: no freq-error (ferr always 0), no RX_IRQ_STALE markers (0) |
| Network (ETH/GW) | Beobachten | keepalive [GW];keep ok 3013 (expected ~3018, max gap 30.4 s); 77 keepalives (2.56%) without server BEAT reply, '[UDP] Server not responding for 35s' 67x; ETH tx_fail 0, DHCP renews 0, renew_fail 0, hw resets 0; 2 ETH link flaps (1-4 s); NTP ok 101/timeouts 2 |
| Crashes / reboots | OK | uptime monotonic (300 s at first STAT .. 90317 s at last), 0 resets, 0 crash/panic/watchdog hits, boot banners in log: 3; single boot at capture start (RESETREAS=0x4) |
| Heap | OK | STAT heap 91648 -> 91604 B (min 91604, max 91648), slope -0.5 B/h; no [HEAP] lines in the stream except boot; flat = no leak signal (nRF52 figure is coarse) |
| CSMA / CAD | Beobachten | TX_GATE_ENTER 5711: attempt0 49.6%, >=3: 14.0%, max 14; CAD initial busy 38.2% of 9236 scans, false positive 18.3% of initial busy; 266 episodes with attempt>=3 (longest 3.6 s); CAD_SAFETY_TIMEOUT 0 (CadDone not seen within 100 ms, radio restarted to RX, no TX that round), CSMA_AGED 0 |
| Double send | OK | bug-type (same slot, READY then DONE) 0; 37 msg_ids read more than once; recurrences are >4 h apart or legit retransmit copies |
| Firmware without CAD | OK | 20 of 238 originators (8.4%) on pre-CAD (4.35a-o, 4.34 and older); they carry 9.9% of heard frame events, 12.0% of unique msgs, 10.2% of decoded airtime (own-call echoes and time servers excluded) |
| Noise floor | Beobachten | carpet = RX_ERROR RSSI p5 -107 dBm (median -102) -> 7 dB above the -114 dBm BW250 theory; RSSI-SNR estimator (SNR>=0 frames, n=3986) -99 dBm - the two disagree (RSSI looks like total in-band power, clamped at the carpet), see method note |
| Input level neighbours | OK | strongest decoded RSSI -71 dBm; 0 receptions above -20 dBm (overload limit), 0 above -40 dBm; 10-dB histogram {'-110': 989, '-100': 3187, '-90': 1443, '-80': 1086} |
| Receive silences | OK | median gap between receptions 3.41 s, longest 120 s (at 10-31 17:34:36); 0 silences >300 s |
| Time signal delivery | Beobachten | Internet side ([CET];gw_rx from OE1XAR-62): 145 of ~150 (median gap 602 s), 5 missing; LoRa side: 141 distinct stamps heard, 9 gaps |
| ACK plausibility | OK | 0 of 310 ACK receptions (0.00%) have byte5 outside 0x80..0x84; firmware ACK_REJECT markers 0 |
| OnRxDone timing | OK | ONRXDONE_TIME mean 7.9 ms, median 7.0, p95 13.0, max 75.0 ms; 6 of 6731 above 50 ms (MC-WARN ONRXDONE_SLOW) |

## DB0AIS-12 ETH-04 focus (RAK4631 + W5100S, Hamnet 44.149.189.136, server 44.148.230.197, path `hamnet`, server tag DL)

Context: ETH-04 = DHCP lease lost for hours on this node (MikroTik, lease 1 w, always-broadcast=no). Fix on fork-dev (F1 reopen UDP keeping IP, F2 W5100S hw reset after 2 DHCP fails, F3 RNG seed from SoftDevice). Inference: this capture runs a build WITH the fix - the boot line `[ETH];event;rng_seed;src;sd` (src/nrf52/nrf52_main.cpp:512) is the F3 marker. STAT still says build stamp 20260929 (not bumped).

| Question | Answer | Evidence |
|---|---|---|
| Boot / DHCP | OK: link up, DHCP lease obtained, `Ethernet.localIP()` 44.149.189.136, `got_ip` at uptime 20.1 s; `dhcp_begin` blocked the main loop 4171 ms once (boot), `hw_init` 199 ms | db0ais.log lines 42-56, 17:01:11-17:01:31 |
| DHCP / ETH stall in 25.2 h | No DHCP stall. got_ip_n stays 1, renews 0, renew_fail 0, W5100S hw resets 0, `reopen_udp`/`hw_reset` events 0. Only blocking calls after boot: ntp 202 ms (01:01:32) and 404 ms (01:16:29), udp_tx 613 ms (01:31:34), all inside the night disturbance window | `[ETH];link;up` 60-s summaries (1508), `[ETH];stall` lines |
| Link flaps | 2 (carrier loss 1.0 s at 10-30 22:14:57 and 2.0 s at 10-31 02:20:35); IP kept in both (`ip;1`), no DHCP re-run, UDP kept working | `[ETH];event;link;down/up` ms 18826775/18827782 and 33564381/33566397 |
| Gateway recovery | Yes, automatic, no reboot. Flap 1: last BEAT 22:14:44, silence 76 s after link-up (2 keepalives unanswered, BEAT back 22:16:14). Flap 2: server silent from 02:18:09 (145.6 s before the flap), first BEAT 2.7 s after link-up (02:20:40) -> the path recovered exactly with the flap (inference: upstream switch/router event, not the node) | db0ais_eth.json link_windows |
| Internet (Hamnet) path loss | Yes, intermittent: 77 of 3013 keepalives (2.56%) got no BEAT, '[UDP] Server not responding for 35s - ETH UP' 67x, hb_age_s>35 in 34 of 1508 minute summaries; 71 of the 77 misses fall between 22:00 and 02:59 (hourly: 22h 7, 23h 10, 00h 11, 01h 23, 02h 20), 6 isolated misses 03:00-18:00; longest silence 121 s (02:18:39-02:20:40). Internet-side {CET} downlink missed 5 beacons (gaps 1204 s after 22:14:56, 1807 s after 00:55:31 and 01:55:44) | db0ais_eth.json hourly, cet_beacons.json |
| Gateway function during loss | Local TX path intact: keepalive tx never failed (tx_fail 0, max gap 30.4 s), LoRa->server uploads (GWU 2135) and EXT UDP out (1878) continued, largest GWU gap 552 s, largest EXT gap 421 s (traffic-driven). Whether the server received them is unknowable from the node (no ACK on uploads) | db0ais_eth.json |
| [GW];srv / EXT / KEEP / NTP | `[GW];srv;DL;host;44.148.230.197;path;hamnet;ms;20114` (single line at boot); `[EXT]` listening 44.149.189.136:1799 -> 44.149.189.135:1799; `[KEEP]` D69A080A DB0AIS-12 4.40a every 30 s (3013); NTP ok 101, timeouts 2 (22:16:32 = 1.5 min after flap 1, 22:31:25), OWN-TX-CHECK-UDP {'-1': 357} (check:-1 each) | db0ais.log |
| Reboots | 1 (capture starts at boot, RESETREAS=0x4 soft reset, 17:01:11); none afterwards | `[BOOT]` lines, STAT up monotonic |
| What this capture does NOT test | DHCP lease renewal: renews = 0 in 25.2 h (lease 1 w -> T1 at ~3.5 d), so the original ETH-04 trigger (renewal / unicast OFFER to the known IP) never occurred; F2 (hw reset) never fired. OE3 gateways with short leases do renew (OE3XOC every 5 min, OE3XHL every 15 min, `[ETH];event;dhcp;rc;2`, 64 ms main-loop stall each, 0 fails) - they sit on a different network (44.143.x, lease length not logged) | `[ETH];event;dhcp` in oe3xoc/oe3xhl logs |

Verdict ETH-04: **Beobachten, not decisive.** Boot DHCP and both link flaps are handled cleanly by the fixed build (IP retained, no DHCP stall, no reboot, recovery without intervention). The DHCP-renewal path that caused ETH-04 was not exercised in this window; the night-time server silence is a path problem upstream of the node (flap 2 recovery coincides with the carrier drop), not an ETH-stack stall. Next evidence needed: a capture spanning a lease renewal (>3.5 d or a shortened lease), or `--ethdrop` on an INSTRUMENT build.

### Method notes and caveats

- Noise floor: RSSI on this firmware behaves like total in-band power (weak frames at SNR -19 read the carpet, e.g. OE3XHL: 718 RX_ERROR at exactly -113 dBm, none below). RSSI-SNR therefore overstates the floor for SNR<0 frames; the lower edge of the RX_ERROR RSSI distribution (p5) is used as the carpet, RSSI-SNR (SNR>=0) is shown beside it. Estimators disagree by 5-10 dB; treat absolute values as inference.
- Channel util counts decoded frames + own TX only (STAT rx = ToA sum of decoded frames within ~1%); RX_ERROR energy is not included, so the real busy fraction is higher than the util figure.
- ESP32 util-inflation caveat does not apply (all RAK4631); fingerprint test (rx= multiples of 2476 ms) negative on all six.
- 'Unique originators' use path[0]; for the logging node's own call this is an echo of own frames (excluded from the firmware census). Last-seen FW per call is replaced by the most frequent non-`#` value.
- Master data (hardware/place/distance) from `$S/master/master.tsv` and `distances.tsv`; OE3XIR has no own distances (position absent), `n/a` there.
- OE3XIR log stops 2026-10-31 18:16:30 (logger), so its percentages cover 11.5 h.
- Console interleaving: a handful of lines are spliced from two sources (e.g. `ONRXDONE_TIME ms=7000C880...`); regexes are anchored so they are dropped, not mis-counted.
