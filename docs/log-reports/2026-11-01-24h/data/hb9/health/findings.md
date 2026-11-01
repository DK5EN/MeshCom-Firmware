# hb9 mesh - health and radio environment (HB9VQQ-4, HB9VQQ-5)

Scope: two Heltec WiFi LoRa 32 V3 (ESP32-S3) gateways of a separate Swiss mesh, never correlated with OE3/DB0AIS.
Captures (logger clock, CET): HB9VQQ-4 "Orn-Bachtel ZH" 31.10. 09:39:56 -> 01.11. 09:39:56 (24.0 h, `--setlog` +
`--loradebug`); HB9VQQ-5 "Tanzboden SG" 31.10. 10:03:15 -> 01.11. 10:03:16 (24.0 h, `--loradebug` only, no `[LOG]`).
Distance 18.59 km (master data). Check scripts: `verify/hb9_analysis_checks/` (outputs in `out/`, rerun with
`run_all.sh`). "Fact" = measured in the log, "inference" = labelled as such. Verdicts: OK / Beobachten / Handlungsbedarf.

## 0. Verdict at a glance

| Question                            | HB9VQQ-4                                                          | HB9VQQ-5                                                                                  |
| ----------------------------------- | ----------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| Node health (reboots, heap, server) | OK                                                                | OK                                                                                        |
| Radio environment                   | OK, two Beobachten items (off-frequency neighbour, local source)  | Beobachten: 52 % of detected frames fail CRC (sensitivity-edge signals, not interference) |
| Auto-ping                           | none running (2 manual pings)                                     | none                                                                                      |
| Handlungsbedarf                     | none                                                              | none                                                                                      |

## 1. Build and stability

1. **Both nodes ran the whole capture without a reboot.** HB9VQQ-4: `[LOG] STAT up=` 3900 s -> 90001 s, no reset
   (results.json `stat.uptime_resets` []); boot about 08:37:54 (3722 s before capture start). HB9VQQ-5: `[MC-HWM]
   uptime=` 3600 s -> 88200 s, 48 samples strictly increasing (`out/node5.json` mc_hwm); boot about 09:06:39. No
   `Guru Meditation`, `rst:0x`, `Backtrace`, `abort()`, `ESP-ROM`, `Brownout` line in either log (0/0). Verdict OK.
2. **Build: 4.40a on both, same TX-pool feature set; exact build unknown.** `[KEEP]` shows 4.40a on both; HB9VQQ-4 STAT
   carries `fw=40a/20261030` = release stamp of the fork release `v4.40a.10.30` (docs/RESUME.md:3), so HB9VQQ-4 most
   likely runs that release (inference; FLASH_VERSION is a release stamp, not a build date). Both logs show
   `RING_SUPERSEDE`, `RING_SUPERSEDE_KEEP`, `CSMA_AGED`, `[CET];tx`, `[MC-HWM] aged1=`, `preempt=`, i.e. the TXP
   campaign features and the CET gate are present on both (fact). HB9VQQ-5 prints no STAT line, so its stamp is not
   readable (unmeasurable).
3. **Heap flat, no leak.** HB9VQQ-4 `[LOG] STAT heap=` 105768 .. 110492 B, slope +72 B/h (results.json `stat.heap`).
   HB9VQQ-5 `[HEAP] (mon)` 367 samples 107916 .. 112480 B, first 112480, last 111924, slope +25 B/h
   (`out/node5.json` heap); min-free-ever 26240 B and max-alloc 73716 B constant in all 367 samples. OK.
4. **Node clocks correct.** HB9VQQ-4: node minus logger 0 s (raw delta -3..0 s, 17026 samples, no step). HB9VQQ-5: node
   `HH:MM:SS` on MH-LoRa lines equals the logger second minus 1 (truncation), e.g. logger 10:03:16.385 vs `10:03:15`.
   `TimeClient` fires every 15 min on both (96/96). OK.
5. **WiFi and server link stable.** HB9VQQ-4: 1440/1440 `[WIFI];link;up`, one BSSID, channel 5, RSSI -64..-55 dBm.
   HB9VQQ-5: 1440/1440 up, one BSSID, channel 6, RSSI -46..-32 dBm. No flap on either. Gateway keep-alive: 2879/2879
   (-4) and 2878/2878 (-5) answered by `BEAT` within 1.1 s (p99 0.11 s; `out/node*.json` gateway.beat_delay_s). One 60.7 s
   keep-alive spacing on HB9VQQ-5 (21:37:45 -> 21:38:46) is a logger blind spot (item 7 of section 2), not a stall
   (`[KEEP]` resumes at 21:38:46.250). OK.
6. **ESP32 loop healthy.** `ONRXDONE_TIME` p50 5 / 4 ms, p99 13 / 13 ms, max 29 / 37 ms, 0 values > 50 ms on both;
   `CAD_SAFETY_TIMEOUT` 0/0; the only state-machine error-like transition is `TX_PREPARE -> IDLE rc=-1` (1228 / 948 =
   exactly the confirmed CAD busy back-offs). OK.

## 2. Traffic, neighbours and the radio environment

| Measure (24 h)                     | HB9VQQ-4                            | HB9VQQ-5                                 |
| ---------------------------------- | ----------------------------------- | ---------------------------------------- |
| Decoded frames (MH-LoRa)           | 14449 (601/h), 3121 distinct ids    | 4440 (185/h), 2471 distinct ids          |
| ACK receptions (no OnRxDone line)  | 171 (`[LOG]` ACK events)            | 81 (inference: ONRXDONE_TIME - OnRxDone) |
| Type mix (HEY / position / text)   | 64.4 / 20.9 / 14.7 %                | 61.4 / 19.9 / 18.7 %                     |
| Dup flag (frames that were copies) | 73.2 %                              | 42.1 %                                   |
| Zero relay tokens (direct)         | 5.7 %                               | 10.8 %                                   |
| Distinct last hops                 | 16                                  | 9                                        |
| CRC errors (`CRC_ERROR`)           | 1903 (79/h), 11.5 % of good + error | 4948 (206/h), 52.2 % of good + error     |
| CHANNEL_UTIL mean / p95 / max      | 19.4 / 40 / 75 %                    | 11.6 / 30 / 64 %                         |

ACK count for HB9VQQ-5: the identity holds on HB9VQQ-4 (ONRXDONE_TIME 14624 - OnRxDone 14453 = 171 = the 171 `[LOG]`
ACK events); the same subtraction on HB9VQQ-5 gives 4529 - 4448 = 81. 12-byte ACK frames print no `OnRxDone` line
(lora_functions.cpp:1018-1034), so any OnRxDone-based RX count misses them.

1. **Channel utilisation is real (no ESP32 inflation).** No `rx=` value is a multiple of 2476 ms (0 of 7447 / 5931
   non-zero samples), and the airtime books close: HB9VQQ-4 decoded 12912 s + CRC frames 1849 s + 171 ACK x 0.31 s =
   14814 s vs `CHANNEL_UTIL` rx sum 14810 s; HB9VQQ-5 decoded 3783 s + CRC 4585 s + 81 ACK = 8393 s vs 8399 s. TX: frame
   lengths give 2249 s (-4) / 1869 s (-5) (+ ACK frames) vs `CHANNEL_UTIL` tx sum 2271 s / 1885 s. The assumption
   SF11/BW250/CR4:6 (MOD 8) reproduces all four sums within 0.1 %. No trend on -4 (first/second half mean 19.5 / 19.3 %).
   TX duty 2.63 % (-4), 2.18 % (-5). OK.
2. **HB9VQQ-5 detects far more weak signals than it can decode (Beobachten, environment, not a fault).** 4948 CRC
   errors vs 4529 good receptions; CRC RSSI p50 -118 dBm, SNR p50 -17 dB, none above -98 dBm; |freq_err| < 1000 Hz for
   94.4 % (> 3000 Hz only 1.0 %), i.e. no off-frequency interferer. Signature of signals at the SF11 demodulation limit
   (decoded RSSI max only -96 dBm, p50 -103 dBm). 54.6 % of its RX airtime (4585 of 8399 s) is such undecodable energy.
   Rate is flat over the day (162..259/h, no diurnal pattern). Fact: the numbers. Inference: sensitivity-edge frames
   from distant stations, not local noise. Side effect: 38.9 % of initial CAD hits are false positives (604 of 1552) vs
   29.9 % on -4 (524 of 1752). It costs RX time and CAD back-offs, not TX.
3. **RX_IRQ_STALE (anchored counting).** `RX_IRQ_STALE rssi=`: 449 (-4, 18.7/h) and 2228 (-5, 92.9/h);
   `RX_IRQ_STALE_EARLY`: 0/0; `forced restart`: 449/2228 (exactly one per event). A plain `grep -c RX_IRQ_STALE` would
   report 898/4456 (double). HB9VQQ-5: RSSI p50 -102 dBm, 3 events above -80 dBm = noise-level preamble detects (flags
   PRE 2198, HDR 30). Not packet loss. OK.
4. **HB9VQQ-4: one neighbour transmits 4.5 kHz off frequency (Beobachten).** HB9LEK-12 (TTGO LoRa32 V2.1 1.6, 4.40a,
   18.8 km) has |freq error| median 4525 Hz on 839 decoded frames (100 % > 3000 Hz). The other 12 neighbours with >= 40
   frames have median |ferr| <= 590 Hz, only HB9WAD-51 is high (2260 Hz) (`out/ferr4.json`). 369 of the 380 CRC errors with
   |ferr| > 3000 Hz sit in the 4500-Hz histogram bin, 133 of them carry HB9LEK-12 as last readable path token
   (`out/crcsrc4.json`, `out/intf4.json`). So about 369 of 1903 CRC errors (19.4 %) and about 31 % of that node's frames
   (369 failed vs 839 decoded) are lost; its link is also weak (RSSI -117 dBm, SNR -13 dB), so the offset is a
   contributing, not a proven sole cause. Inference: crystal offset of about 5 ppm in that foreign node
   (4.5 kHz / 868 MHz). Neighbour side, no action on our nodes.
5. **HB9VQQ-4: a recurring strong local source (Beobachten, low).** 153 of 449 stale-IRQ events are above -80 dBm; 142 of
   them sit at -72/-71 dBm (92 + 50), 152 of 153 flag PRE (preamble only, no header), clustered in hours 14 (17), 16
   (22) and 20 (27); inter-arrival p50 238 s, no fixed period. Only 6 coincide with the +4.5 kHz CRC errors. 161 CRC
   errors above -85 dBm, none above -60 dBm. Source unknown, not a decodable MeshCom frame (inference). No traffic effect
   visible (no RX silence, item 6). Compare OE3XIR (323 errors above -60 dBm every hour): much weaker here.
6. **No RX silence.** Longest gap between RX events 110.8 s (-4, 31.10. 20:18:01) and 81.0 s (-5, 01.11. 01:17:10); 0 gaps
   > 300 s on both (the OE3XIA deaf-receiver pattern is absent). During the 110 s gap HB9VQQ-5 heard 8 frames, so the
   gap is a quiet channel, not a deaf radio (fact: other node's RX events in the same window). OK.
7. **Logger blind spots on HB9VQQ-5 (instrument note).** 27 `[LOGGER] connection lost (vom Node geschlossen)` events:
   24 at HH:00:01..02 from 11:00 to 10:00, plus 3 at 21:37:53 / 21:38:14 / 21:38:38. Every one is preceded by a
   `[CON ]...s_password` line (27/27): another client logs into the single net-console session and takes it over
   (net_console.cpp takeover path). Each reconnect costs about 5 s of capture (about 135 s, 0.16 % of the day).
   Consequences: counts on -5 are lower bounds; 8 `RING_TX_READ` lines have no matching `RING_WRITE` because the write
   fell into a gap (13:00:14, 14:00:14, 15:00:15, 17:00:13, 17:00:14, 18:00:22, 22:00:08, 03:00:15). HB9VQQ-4 was
   reached over another route and has 0 reconnects. Neutral (operator-side script).
8. **Neighbourhood firmware (HB9VQQ-4 only).** 148 nodes seen, 11 pre-CAD builds (7.4 % of nodes) cause 16.4 % of heard
   events and 15.0 % of RX airtime (DB0SAC-12 4.35h 675 events, HB9TVJ-12 4.35i 570, HB9HEH-40 4.35h 430, HB9HNO-49 4.35h
   338; results.json `firmware_census`). Neighbourhood, not node health: Beobachten, never Handlungsbedarf for the
   logging node. HB9VQQ-5 was not censused (MH-LoRa carries a `FW:` token, not extracted).
9. **CAD / CSMA.** HB9VQQ-4: 5557 scans, 1752 initial busy (31.5 %), 1228 confirmed, 524 false positive; attempt 0 on
   67.7 % of gate entries, >= 3 attempts 11.8 %, max 17 (23:33:32, 3.2 s storm). HB9VQQ-5: 4683 scans, 1552 initial busy
   (33.1 %), 948 confirmed, 604 false positive; attempt 0 on 69.7 %, >= 3 attempts 4.9 %, max 7. Gate entries per TX
   1.48 / 1.43. OK.

## 3. Comparison with the OE3 cluster (same firmware family, other mesh)

| Mechanism                       | OE3 / RAK4631                              | hb9 / Heltec V3                                                                    |
| ------------------------------- | ------------------------------------------ | ---------------------------------------------------------------------------------- |
| Auto-ping                       | OE3XWJ 1529 pings, OE3XIR 689 ("255.")     | none; 2 manual pings on -4 (19:16:12, 19:27:47 to HB9ZG-49), no periodic TX        |
| ACK relays                      | 41.3 % of cluster TX count                 | -4: 32 of 2577 (1.2 %), -5: 22 of 2183 (1.0 %); ACK airtime share 0.4 %            |
| RX_ERROR share                  | 7.3 .. 32.0 % (DB0AIS 65.8 %)              | 11.5 % (-4); 52.2 % (-5, sensitivity edge, item 2.2)                               |
| Own-beacon starvation           | OC 41.8 % / IA 78.8 % own HEY aired        | 99.0 % / 99.2 % own HEY aired (-4 / -5), own WX 100 % (q2, q34)                    |
| Deaf receiver                   | OE3XIA 17.4 % of the day                   | none (longest gap 111 s / 81 s)                                                    |
| Pre-CAD share of heard events   | HL 26 %                                    | 16.4 % (-4)                                                                        |
| Stale {CET} on air from foreign | foreign relays 62/64 etc. older than 150 s | 1 of 148 foreign `[CET];rx` older than 150 s (-4; see q34 section 4)               |
| ESP32 channel-util inflation    | n/a (nRF52)                                | absent (item 2.1)                                                                  |

The auto-ping platform difference (RAK loop has no count, ESP32 loop sends only while `node_pingcount > 0`,
esp32_main.cpp:5264/:5285 vs nrf52_main.cpp:2717-2737) is not exercised here: both `{ping}` frames (`NEW-TXT`,
`src=user_msg`, prio 1, status FF) are single operator actions; the `--pingcall` setting is not visible in a capture, the
observable fact is 24 h without a repeating ping TX. Of the 171 ACK receptions at -4, 86 (50.3 %, 16 distinct ack ids) are
acknowledgements for HB9VQQ-5's range-test texts (`A3F15xxx` is HB9VQQ-5's own msg-id range, texts 21:17-21:39) and 22 (6 ids)
answer the 19:16 ping. The extractor's note for this key ("F5CE7xxx = OE3XWJ ping") is an OE3 leftover (section 5).

## 4. What cannot be measured on HB9VQQ-5 (no `[LOG]` lines)

- `[LOG]` RX tail flags (`DUP:`, `OWN:`) and the per-frame firmware census; replaced by `RX_DEDUP_NEW/DUP` markers. RSSI/SNR
  per frame come from the `[LoRa]...Received packet` line before each MH-LoRa line (used above, fact).
- `[LOG] STAT` (heap, dup/err counters, `onrx_*`); replaced by `[HEAP]`, `[MC-STAT]`, `[MC-HWM]`, `CHANNEL_UTIL`,
  `ONRXDONE_STATS`. The extractor's `stat.rows 0` for -5 is wrong: 288 `[MC-STAT]` blocks exist.
- `[LOG] TX ... src= prio= wait= cad=`: reconstructed from `RING_WRITE src=` + `RING_TX_READ lat=` (exact join on slot +
  msg_id). ACK TX frames print no `TX-LoRa` line on either node (2545 vs 2577, 2161 vs 2183).
- `[LOG] RLY` relay decisions (gwcap / hop0 / full / cet / gwfilter). Only `RELAY_QUEUED` (1800 = 1778 relay writes + 22
  SUPERSEDE_KEEP) is available.
- `[LOG] GWU/GWI`, `DM`, `MBOX` lines; ACK-to-origin attribution.
- `[NBR]` markers are absent on BOTH nodes (`bNBRDEBUG` off, `nbrLog == NULL`, lora_functions.cpp:688-694): relay cancel /
  NEED / case A cannot be read and `--nbrrelay on|count|off` is not provable from the log (q2 section 3). The counters
  live on the node: the info command prints `...NBRRELAY on|count|off ... cancelled N` (command_functions.cpp:7311).

## 5. results.json / extractor caveats found while checking (not edited)

- `health/results.json` HB9VQQ-4 `gateway.beat_miss_*` (116 episodes, 4.38 % of keep-alives) is an artefact: BEAT arrives
  0..0.1 s after every KEEP (2879/2879); the extractor counts same-millisecond order as a miss.
- `network.ack_ping_attribution` key `acked_prefix_F5CE7_share_pct_of_012_acks` and its OE3XWJ note are OE3 leftovers; the
  hb9 top prefix is `A3F15` (50.3 %).
- `q2/results.json` HB9VQQ-5 `fate.tx_noair_other: 2175`, `hey_chains.own_hey.aired_pct: 0.0`, `tx_lines.n: 0` and the
  `network.oe3_*` keys are artefacts of the missing `[LOG] TX` lines; real values in q2/findings.md (own HEY 118 of 119
  aired).
- `q34/results.json` HB9VQQ-4 `cf` (counterfactual replay) is not valid here: baseline TTL drops 285 vs 0 real, pick match
  68.9 %, because the replay has no overhear-cancel path while the node frees 383 slots that way (q2 section 3).

## 6. Suspected bugs / escalations (code sites)

- **None that need action on health.**
- Observability gap (Beobachten): relay cancel is silent without `--nbrdebug` (lora_functions.cpp:1446 frees the slot,
  :1463 logs only `if(... && nbrLog != NULL)`), and `stat_nbr_cancel` is part of neither `[MC-STAT]` nor `[LOG] STAT`. A counter in
  `[MC-STAT]` would make the 12.7 % of ring writes (-4) that leave the pool by cancel visible in every normal capture
  (suggestion, untested).
- The only non-relay slot that "vanished" on HB9VQQ-5 (server text 5E45D03F, 31.10. 21:38:31.7) was aired inside a logger blind
  spot (`CHANNEL_UTIL` 21:38:47.6 `tx=860ms`, q2 section 3); no loss.
