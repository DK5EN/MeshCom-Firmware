# Core findings v2 (after adversarial verification) - binding for all seven pages

Status 01.11.2026. Every item below was re-measured independently by an Opus verifier (verify/q2.md, q34.md,
q5.md, q6.md, health.md) or by the orchestrator ([x]). Where v1 numbers were wrong, the corrected number is
given and v1 must not appear on any page. "Vermutung" = inference, must be labelled as such on the page.
Field build unknown on all six nodes; code facts are read at HEAD 651e0920. "Build stamp 20260929" is
FLASH_VERSION (release stamp, informative), not a build date.

## Setting
- Six RAK4631 (HW 9) with Ethernet (chip W5100S inferred, not in log), Internet gateway + relay, 4.40a, no reboot.
  nRF52: ESP32 channel-util inflation caveat does not apply.
- OE3 cluster OE3XHL-12, OE3XIA-12, OE3XIR-12 (log ends 31.10. 18:16, ~11.5 h), OE3XOC-12, OE3XWJ-12;
  31.10. ~06:30 - 01.11. ~07:50. DB0AIS-12: separate mesh (Hessen), 30.10. 17:01 - 31.10. 18:09, never correlated.
- Logger clock (Mac) is CET; logger minus (NTP epoch + 3600) = +0.6 s p50. Node clocks: OE3XIR +3599 s (UTC+2),
  others -1 s; DB0AIS ran +1 h until the step between capture line 18005 (19:20:26) and 18015 (18:20:31), preceded
  by a real settings save - cause Vermutung (suspect src/clock.cpp:153-165).
- Counting trap (affects every RX statistic): 12-byte ACK frames print no "OnRxDone" line
  (lora_functions.cpp:1018-1034), only ONRXDONE_TIME / [LOG] 012 A. Any RX count from OnRxDone lines misses ACKs.

## Firmware bugs and design gaps (confirmed in code)
1. **Auto-ping: platform difference, decision open** [x code; operator ruling 01.11.2026: record as a difference,
   not as a bug]: the ESP32 loop sends only while node_pingcount > 0 and decrements it per ping, so it stops after
   node_pingmax pings (src/esp32/esp32_main.cpp:5264, :5285); the nRF52/RAK4631 loop has no count at all and pings
   every node_pingtime s for as long as node_pingcall is set (src/nrf52/nrf52_main.cpp:2717-2737). Consequence on
   RAK: `--ping stop` (sets node_pingcount=0, command_functions.cpp:3886) has no effect; only clearing --pingcall
   stops it. Upstream dev 147812e0 has the same code. Which behaviour is intended is an operator decision (page
   wording: "Unterschied ESP32 / RAK4631, Entscheidung offen", never "Bug"). Effect in this capture: OE3XWJ-12
   pings group 2025 every 60 s (1529x), OE3XIR-12 pings "255." every 60 s (689x). Handlungsbedarf = decision.
2. **No validation of the ping target** [x]: OE3XIR-12's own auto-ping (node_pingcall = "255.") is written to the
   ring every minute (src=phone_msg is only the ring label of sendPing), wins the prio-1 pick and is dropped at
   decode ("Destination-CallSign Error [255.]", aprs_functions.cpp:369-373; slot freed lora_functions.cpp:3270-3274):
   688 of 3307 ring writes (20.8 %), 0 aired, each costing a CAD cycle (689 CAD_FREE_NO_TX). Neither `--pingcall`
   (command_functions.cpp:3806-3831) nor sendPing (loop_functions.cpp:3820) validates the call. How "255." got into
   the setting: Vermutung (config import / older build). TX_FRAME is printed even for these dropped frames.
3. **Own beacons starve at busy nodes** (OE3XOC, OE3XIA): own HEY aired 41.8 % (OC) / 78.8 % (IA) of episodes
   (written minus superseded; of all written HEY: 29 % / 61 % - always name the denominator); own WX lost to TTL
   OC 34/49, IA 11/49, IR 5/22, WJ 4/50, HL 0/49, DB0AIS 0/50; OE3XOC own WX off RF for 7.64 h
   (31.10. 21:14 - 01.11. 04:52). Mechanism (verified, three parts): strict priority pick blocks 64-72 % of a lost
   beacon's head-of-queue time (txring_functions.cpp:213-251); the 5.5 s hold for prio 4/5 is re-armed by every
   received frame and the CSMA age floor skips prio 4/5 (csma_timing.h:99-111) - OC made only 21 CAD attempts in
   2924 s at head of queue; on IA CAD_BUSY failed 62/62. TX duty at OC is only 4.2 % - channel-access limit, not
   airtime. Own vs relayed of the same prio/type differ strongly (own WX 69 % lost vs relayed positions 1.1 %;
   own HEY wait p50 58.9 s vs relay 19.7 s on OC). Root cause (verify/rca_own_beacons.md, code checked [x]): at the
   head of the queue an own beacon gets the 5.5 s prio-4/5 hold, re-armed by every received frame
   (lora_functions.cpp:2746-2747), while a relay of the same prio takes the NBR path (lora_functions.cpp:3998, only
   with bNBRCANCEL) and in case A gets a 150-220 ms hold after 8 s of waiting (txring_functions.cpp:385-405). Own
   beacons are excluded from that path and from the age floor (csma_timing.h:101). OE3XOC: own beacon at head sends
   0.44-0.63x/min, relay at head 13-16x/min. Ruled out: pick order, READY/DONE, cancel, duty gate, WX phase.
   Design gap (QRT plan "prio 4-5 never escalate" vs case A already escalating prio-4/5 relays). Consequence: since
   relays take the case-A path, bNBRCANCEL is on at OE3XOC (inferred) - the missing CANCEL lines follow from item 5.
   Also ~70 % of a dropped own beacon's life is spent behind prio-1 ACK forwards (3367 rx_ack_fwd in 25 h).
   Smallest fix candidate (not implemented, untested): extend the age floor to prio 4/5 for non-relay slots
   (csmaApplyAgeFloor or scoped in csma_compute_timeout_slot, lora_functions.cpp:4030-4043). A per-class TTL alone
   would only let own beacons wait longer. Open: OE3XOC losses cluster at night (24/26 between 21:44 and 07:24)
   while hourly load is flat - unexplained.
4. **ACK relays have no overhear-cancel path** [verified]: kind OTHER (lora_functions.cpp:661), the cancel scan runs
   only for ':'/'!'/'@' frames in RELAY slots (:1327, :1359). 29.6 % of cluster TX airtime (4013 s) is relay
   traffic sent after a foreign copy was heard while queued (upper bound).
5. **{CET} server slots always "sole provider"** [verified]: every [NBR]|NEED for a {CET} id is case A (666/666 OE3,
   349/349 DB0AIS), 0 CANCEL. v1 claim "need == alone" is REFUTED (holds in 3/666); correct question: why is the
   alone set never empty for a server slot. OE3MIF-12 / OE3NRA-1 sit in the alone set in 95.6-100 % of NEEDs.
   v1 explanation "12-h window" is WRONG (case A is 100 % already in the first 60 uptime minutes, and symmetry-
   inferred edges can supply the reverse link, nbr_matrix.cpp:1829-1848). Fix target: dependents rule and
   alternative-provider test (nbr_matrix.cpp:1777, :1816-1852). Beobachten/design.
6. **OE3XIA deaf receiver** [verified]: 35 silences > 5 min, 15620 s (17.4 %), longest 1024 s (from 31.10. 11:49:01),
   counting ACK receptions; other nodes' longest gap <= 153 s (OE3XWJ stall) / 68-88 s otherwise. Not a logger drop
   (max line gap 4.9 s, millis continuous), firmware itself reports CHANNEL_UTIL rx=0 in all 1511 samples inside the
   silences; not desensitisation (CAD sees busy 161/218, own TX heard by others 40/57). 30/35 silences end within 3 s
   after a Standby+CAD cycle (base rate 20.6 %); the ~4.8 s RX watchdog re-issue (startRadioReceive -> Radio.Rx
   without Standby, src/nrf52/nrf52_main.cpp:1631 [x code]) does not recover it. Bench test hypothesis: Radio.Standby()
   before the watchdog restart. Handlungsbedarf, status Verdacht.

## Q2 TX pool
- Mechanics OK: 0 RING_DROP_NEW/PRIO/REARM, max depth 16/20, slope -0.038..+0.014 slots/h (no build-up), fate
  accounting reproduces exactly (92.43 / 3.01 / 0.97 / 2.58 / 0.99 / 0.01 %).
- ACK wait (real ACKs, pings excluded): IA p50 21.9 s, OC 23.3 s, IR 20.9/31.7/60.5 s (53 % >= 20 s); at an empty
  queue still 16-18 s on IA/OC; cause = hold re-armed per received frame (inference, consistent with histogram step
  after the 20 s age floor). Max ACK wait 64.6 s vs 90 s TTL.
- Supersede resets ringEnqueueTime (txring_functions.cpp:1007) - confirmed; every TTL drop comes 180-182 s after the
  last supersede; "45 chains" = 45 entries in 28 chains.
- Retry ladder never ran (0 RING_REARM) - untested by this data.

## Q3 / Q4 time decay
- 232 RING_DROP_STALE, all ttl (162 HEY at 180-182 s, 70 positions at 120-122 s); 0 ACK, 0 text, 0 {CET} ring drops;
  none radiated before the drop (DONE = final, not sent; txring_functions.cpp:299-321).
- 153 own beacons lost (99 HEY, 54 WX); none received by any other node. 74 relay drops: 55 carried elsewhere
  (wording: "anderweitig übertragen", not "redundant"), 19 potential losses, all refreshable types.
- Gain = freshness: dropped airtime 167 s = 1.23 % of cluster TX airtime. Counterfactual numbers (no TTL: +66-94 s
  delay, ~200 s stale, max 644 s; longer TTL -79 % / -97 % drops) are MODEL upper bounds (first-order replay,
  219/224 drops had another prio 3-5 frame waiting) - label them as model, not measurement.
- CSMA_AGED: 13709 events (v1 "15709" WRONG), ~3670 slot episodes; prio 3 only 38 episodes, OE3XWJ's 62 are prio 2;
  no collision signature (RX_ERROR after aged vs fresh ACK equal); DB0AIS 0 events = trigger never met (max prio<=3
  wait 20.2 s), not a build difference. Saving per episode (0.6-1.6 s) is modelled, not measured.

## Q5 time signal
- OE1XAR-33 every 607 s, 151/151 telegrams reached the cluster; content is UTC labelled "CET". "Server restart" at the
  674 s gap is Vermutung.
- All five OE3 nodes inject as gateways ([LOG] GWI from=OE1XAR-33) [x]. Per telegram: 5 own + ~6 foreign gateways
  heard directly (+~2 only inferred from relay paths; v1 "8 foreign" mixes both) + ~9 relays = ~20 radiations,
  ~15 s airtime. 64.6 % (430/666) of our gateway TX follow an already overheard radiation.
- Our nodes: 0 of 666 own TX older than 150 s (max 148.8 s at OE3XIR, 1.2 s margin); gate stopped 8 (OE3) + 4
  (DB0AIS) stale relays. The gate covers the gateway inject path too (gw_udp_ring.h:39, since 05a18b36 / v4.40a.10.23,
  upstream via #1238).
- Stale time on air from foreign relays (full run): OE3MIF-12 62/64, OE3NRA-1 47/47, OE3MTC-10 46/46 (p50 817 s),
  OE3XDC-12 44/49 (p50 261 s) older than 150 s; together 79.6 % of stale radiations. Plus OE3GJC-12 radiating stale
  {CET} as gateway (5 copies 154-356 s at OE3XHL) - Vermutung: older build without the gate. Operator side.
- Only -33 in OE3; DB0AIS mesh carries -33/-62/-63 (OE1XAR-63 unknown origin), {CET} = 11 % of DB0AIS TX airtime.

## Q6 manipulations
- Ping load: ACK relays alone = 41.3 % of cluster TX count (8727/21148) and 20.2 % of TX airtime (2735/13564 s);
  with OE3XWJ's own 1529 pings = 48.5 % / 25.9 % (v1 mixed these - always say which). 97.9 % of ACK relays belong
  to OE3XWJ pings. The ping ACKs come from FOREIGN gateways (gateway-ACK format, ~4.6 distinct ACK ids per ping):
  at HEAD a gateway never ACKs {ping} (lora_functions.cpp:2169-2173, also upstream) - so the ACKing gateways run
  other builds (Vermutung). Fix has three sides: RAK ping limit (bug 1), the test pattern, foreign gateway firmware.
- Overhear-cancel acts on OE3XHL only: 589 CANCEL lines (587 matched relays, 23.9 % of 2462 relays); other four case
  A on 100 % of NEEDs; whether cancel mode is on there is not provable from the log.
- OE3XHL is effectively unheard by the cluster (IA 4 receptions, others 0); HL hears only IA.
- Parallel relay pairs: "36 % avoidable" is an upper bound (ACK pairs count any copy of the ACK id); hidden pairs
  78.5 % involve HL, 21.5 % are WJ<->IA.
- Server inject radiated by 4-5 gateways for 231/362 frames, 735 s redundant airtime (5.4 %).
- No loops/ping-pong/hop growth; loop guard 7x; 5 dedup-expiry re-relays after 22.2-25.3 min (> that node's dedup
  window 17.6-26.4 min). GW-ACK pairs twice by design (GWACK-01).

## Health
- RX_ERROR shares (corrected denominator incl. ACK receptions): HL 7.3 %, IA 32.0 %, IR 25.2 %, OC 21.9 %,
  WJ 19.8 %, DB0AIS 65.8 %. Threshold 40 %: only DB0AIS Beobachten (v1 55 %/43 % etc. WRONG).
- Noise floor p5 of RX_ERROR RSSI confirmed (independent estimator within 1 dB); caveat: RSSI may be stale for
  header errors.
- Pre-CAD share of heard events (4.40a counted as CAD-capable): HL 26 % highest. This is the neighbourhood, not the
  node's health: Beobachten, never Handlungsbedarf for the logging node.
- OE3XIR strong local source: 323 RX_ERROR above -60 dBm every hour, none on other nodes, not own-TX echo; type
  unknown (it raises a preamble detect, so "not LoRa" is not supported).
- OE3XWJ 153 s radio stall: 22 CAD_SAFETY_TIMEOUT, 0 TX, last RX 17:03:38.8, next 17:06:11.9; self-recovered.
- Server outage on all five at 31.10. 07:22, ~39 s (server side).
- DB0AIS ETH-04: Beobachten. Boot DHCP + two link flaps handled, heartbeat loss upstream (77/3013, mostly 22-03 h),
  lease renewal never happened -> original trigger untested. rng_seed marker = F3 fix present; F1/F2 inferred.

## Side finding (tooling)
- tools/berglog.py verify(): shell-side counts include ACK lines and non-[LOG] {CET} lines -> false mismatch
  warnings; parse itself correct (HL 11659 parsed + 10179 ACK = 21838).

## Late corrections (rework wave)
- DB0AIS dedup window: q6 `loops.dedup_window_min_est` = 111.9 min is wrong for DB0AIS; raw RX_DEDUP_ADD slot=N/100
  counter gives median 42 min (31-54 min). OE3 windows 17.6-26.4 min were verified independently (verify/q6.md).
- DB0AIS 37 "double relays": distinct telegrams - the same msg_id used by OE1XAR-33 and ~2.9 h later by OE1XAR-63
  (different payload times); 81 of 142 -63 ids collide with a -33 id. No extra airtime; risk (model): a node with a
  dedup memory longer than ~2.4-2.9 h would drop the -63 telegram (dedup compares only the 4-byte msg_id,
  src/dedup_functions.cpp:25-31).
- ACK ids per OE3XWJ ping: ~2.9 per ping event (median 3, max 8; data/extra/oe3xwj_ping.json). The verifier's 4.6
  grouped by msg_id, but ping ids wrap over 24 h (987 distinct ids for 1529 pings) - do not use 4.6.
