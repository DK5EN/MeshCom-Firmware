# Core findings HB9 mesh (after adversarial verification, verify/hb9.md: 15 confirmed, 5 corrected, 0 refuted)

Status 01.11.2026. Binding for the HB9 pages and the HB9 section of the overview. Separate mesh: never correlate
with OE3/DB0AIS. Same firmware family 4.40a; the OE3 core findings (../CORE-FINDINGS.md) apply where the mechanism
is the same.

## Setting
- HB9VQQ-4: Heltec WiFi LoRa 32 V3 (ESP32-S3), Internet gateway "Orn-Bachtel ZH", 47.2882 N 8.8813 E, 995 m.
  Capture 31.10. 09:39 - 01.11. 09:39, --setlog + --loradebug, no [NBR] lines (nbrdebug off).
- HB9VQQ-5: Heltec V3 (ESP32-S3), gateway "Tanzboden SG", 47.2215 N 9.1072 E, 1432 m. Capture 31.10. 10:03 -
  01.11. 10:03, loradebug only (no [LOG] lines): RX tail flags, relay reasons, STAT, GWU/GWI, ACK attribution and NEED/
  CANCEL are not measurable; TX source/prio/wait rebuilt from RING_WRITE + RING_TX_READ, RSSI/SNR from "Received packet".
- ~18.6 km apart; mutual decode 92.1 % (-5 hears -4) / 64.9 % (-4 hears -5) in the common window; RSSI ~-97/-100 dBm.
- Logger: tools/meshlogger.py over TCP 2323 (one-space prefix, canonicalised). HB9VQQ-5: 27 console takeovers of
  5.0-5.3 s each full hour (a second client logs in, net_console.cpp) - logger blind spots, counts are lower bounds.
- Master data: data/hb9/master (77 of 147 calls).

## Verdicts
- Health: both OK, no reboot, heap flat, keep-alives 100 % answered. HB9VQQ-5 Beobachten: 52.2 % of detected frames
  fail CRC, SNR p50 -17 dB (near the SF11 floor) - sensitivity edge at 1432 m, not interference (94 % of CRC errors
  with frequency error < 1 kHz). HB9LEK-12 transmits +4.5 kHz off frequency on 100 % of its 839 decoded frames and
  causes 19.4 % of the CRC errors at -4 (operator side). -4 sees a recurring local source at -72/-71 dBm (142
  preamble-only events).
- F2 TX pool: OK, no build-up, 0 RING_DROP_*, max depth 8/9 of 20; 85.6 % / 97.8 % of 3010 / 2224 writes aired.
  On -4, 383 slots (12.7 % of writes, 14.9 % of relay writes) are freed silently, every one after a foreign copy was
  heard: overhear-cancel (only the NBR cancel frees a DONE relay slot silently, lora_functions.cpp:1445; the log line at
  :1463 needs nbrLog) - implies --nbrrelay on. Cancel absent on -4 from 15:39 to 22:39 - cause open (no [NBR] lines).
- F3/F4 decay: 0 RING_DROP_STALE on both nodes - nothing lost, nothing measurably gained. HEY class peaks at 166.5 s of
  180 s (92.5 %) on -4 in a burst 31.10. 18:56-19:02 - Beobachten. CSMA age floor: 124 events / 26 episodes (-4),
  8 / 4 (-5), no collision signature. Own HEY waits 2.15x relayed HEY on -4, 1.47x on -5 (same asymmetry as OE3
  verify/rca_own_beacons.md, but no losses at this load: own HEY 99.0 / 99.2 % aired, WX 100 %).
- F5 time signal: OK. Only OE1XAR-33 by UDP (604-611 s cadence, no gap); OE1XAR-63 (121 telegrams, via DO1FE-19) and
  OE1XAR-28 (65, via HB9CMI-40 / HB9CF-40) by RF only; -62 never. Own {CET} age on air max 56 s (-4, 207 TX) / 40 s
  (-5, 219 TX); 0 gate drops. ~8 radiations per -33 telegram; both nodes air the same telegram in 138 of 146; the
  later node had already decoded the earlier copy in 75 % (129/172) - Beobachten (redundancy, case-A design gap,
  OE3 CORE item 5).
- Stale source: HB9HNO-49 (Heltec V3, firmware 4.35h = pre-CAD and without the {CET} gate, gateway, 47.1783 N
  8.9047 E, 490 m) re-radiates 53 distinct {CET} telegrams 975-1992 s old (46 frames decoded at -4, 50 at -5) and
  every frame type ~24 min late. Only one stale {CET} was accepted as new (6ADB5BE4 at -4, H00, 1492 s, hop0 exit,
  no ring write); neither node transmitted a {CET} older than 150 s - no firmware bug. Late HB9HNO-49 HEY/position
  frames were relayed (-5: 32 accepted, 14 relayed) - by design, these types have no origin-age gate. Operator side.
- F6 manipulations: no build-up, 0 loops, hop/path sums constant, TX duty 2.6 % / 2.2 %; ACK+ping load ~1 % of TX
  (OE3: 41 %). No auto-ping running (2 manual pings on -4). Mutual redundancy Beobachten: 1846 distinct ids aired by
  both nodes (1392 relay/relay pairs), later copy = 42.1 % of the pair's airtime (upper bound - only 5 shared direct
  neighbours). 31.2 % of -4's TX airtime is relays aired after a foreign copy (upper bound; OE3 29.6 %). Gateway inject
  slots never cancelled (case A design gap).
- Cosmetic log bugs (no data loss): `RLY q=full` printed for RING_SUPERSEDE_KEEP (lora_functions.cpp:2651,
  txring_functions.cpp:1035); `[MC-TRICKLE] SEND consistent=17<k=2` printed for gateway-forced sends
  (esp32_main.cpp:4825). Observability gap: silent relay cancel without nbrLog, stat_nbr_cancel in no stat line.
- Extractor artefacts (do not show as findings): health beat_miss for -4 (0 real misses); q34 cf replay invalid
  without a cancel path (285 predicted vs 0 real drops); q5 tx_age_gt150_n=1 (ids of -63/-28 repeat 21 h apart);
  q2 for -5 own_hey.aired_pct 0.0 / tx_noair_other (missing [LOG] TX lines); health ack_ping_attribution carries the
  OE3 key.
