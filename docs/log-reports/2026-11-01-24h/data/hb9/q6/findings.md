# F6 - Manipulations of HB9VQQ-4 and HB9VQQ-5 and their effect on the HB9 mesh

Verdicts: OK / Beobachten / Handlungsbedarf. Effect on the network: **positive (+)**, **negative (-)**, **neutral (0)**.
"Fact" = read from the log; "Inference" = derived and labelled. Numbers re-measured with
`verify/hb9_net_checks/hb9_net_checks.py` (section in brackets; `out_all.json` has everything). `q6/results.json` was used as a cross-check only:
it treats HB9VQQ-5 as "left out of the cross-node steps", so its redundancy block (`redundancy_v1_nodes_per_id` = {1: 2572}) is single-node and is replaced by the
cross-node numbers below. Basis, clocks, console gaps and the `[NBR]` situation are in `q5/findings.md` section 0 (both nodes: 0 `[NBR]` lines because
`--nbrdebug` is off, src/lora_functions.cpp:694; HB9VQQ-5 has no `[LOG]` lines at all). Logs: `hb9vqq4.log` 31.10. 09:39:56 -> 01.11. 09:39:56,
`hb9vqq5.log` 31.10. 10:03:16 -> 01.11. 10:03:16, logger time CET.

## 0. Two nodes in numbers (24 h)

| Metric                                            | HB9VQQ-4                                                        | HB9VQQ-5                                                         | Source                                                 |
| ------------------------------------------------- | --------------------------------------------------------------- | ---------------------------------------------------------------- | ------------------------------------------------------ |
| Decoded frames (`MH-LoRa`)                        | 14449                                                           | 4440                                                             | [parse]                                                |
| Accepted as new (`RX-LoRa2`) / DUP verdicts       | 3126 / 10722 (2.58 DUP per NEW)                                 | 2303 / 1919 (0.67)                                               | [parse, dedup]                                         |
| CRC errors, share of receive attempts             | 1903 = 11.6 %                                                   | 4948 = 52.7 %                                                    | [rxerr]                                                |
| TX (ring reads) / airtime (model)                 | 2577 / 2259 s                                                   | 2183 / 1876 s                                                    | [redundancy]                                           |
| TX duty / RX duty / channel util mean (p90)       | 2.6 % / 17.1 % / 19.4 % (35 %)                                  | 2.2 % / 9.7 % / 11.6 % (25 %)                                    | [load]                                                 |
| Ring writes by source                             | relay 2575, gateway 253, own HEY 97, own pos 47, ACK 32, text 6 | relay 1778, gateway 253, own HEY 119, own pos 48, ACK 22, text 4 | [parse]                                                |
| Relay ring writes / share of accepted frames      | 2575 / 82.4 %                                                   | 1778 / 77.2 %                                                    | [relay]                                                |
| Ring drops (TTL, full, prio), CAD safety, TX fail | 0                                                               | 0                                                                | grep `RING_DROP_*`, `CAD_SAFETY`, `TX_DONE state=[^0]` |
| Direct neighbours (>= 5 last-hop frames)          | 16                                                              | 8 (5 shared with -4)                                             | [hops]                                                 |

- Fact: airtime model (SF11 / BW250 / CR4/6, preamble 8, src/country_profile.cpp:90) reproduces the node's own `CHANNEL_UTIL tx=` sums within 0.5 %
  (2259 s vs 2270.5 s at -4, 1876 s vs 1884.9 s at -5). Logger timestamps are NOT usable for per-frame airtime (console output arrives in bursts).
- Fact: CRC errors differ in kind. -4: RSSI p50 -99 dBm, SNR p50 -2 dB (strong, colliding frames). -5: RSSI p50 -118 dBm, SNR p50 -17 dB (weak, near the noise floor; also 4456
  `RX_IRQ_STALE` vs 898 at -4). Context only: -5 on 1432 m sees many marginal far signals; it decodes a third of the frames -4 decodes. Beobachten (same 40 % threshold the OE3 report used for DB0AIS).

## 1. Manipulation catalogue and verdict

| #   | Manipulation (code site)                                                                    | Count -4 / -5                                                   | Effect                                          | Verdict                        | Section |
| --- | ------------------------------------------------------------------------------------------- | --------------------------------------------------------------- | ----------------------------------------------- | ------------------------------ | ------- |
| 1   | Relay of new frames: hop budget -1, own call appended (lora_functions.cpp:2490-2520)        | 2575 / 1778 ring writes; 0 loops; hop sums constant             | + reach, - airtime                              | OK                             | 2.1     |
| 2   | Not relayed: `hop0`, `gwcap`, `full` (lora_functions.cpp:2383-2394, 2651, 2662)             | 197 / 351 / 3 at -4 (551 of 3126) ; -5 not separable            | 0                                               | OK (`full` label wrong, 3.1)   | 2.1     |
| 3   | Overhear-cancel of a queued relay (NBR CANCEL, lora_functions.cpp:1319-1470)                | 381 / 19 relays freed after a foreign copy (inference)          | + airtime saved                                 | Beobachten (diurnal gap at -4) | 2.2     |
| 4   | Parallel relay / inject by both nodes (no coordination)                                     | 1392 relay pairs, 220 gateway pairs, 1745 s second-copy airtime | - airtime, + reach in non-shared neighbourhoods | Beobachten                     | 2.3     |
| 5   | Gateway inject of server frames (`udp_rx`, gw_udp_ring.h)                                   | 253 / 253 (142 / 139 CET + 111 / 109 texts)                     | + Internet to RF                                | OK                             | 2.4     |
| 6   | Gateway upload RF to server (`TX-UDP`)                                                      | 3166 / 1911; 1426 ids uploaded by both                          | + topology data, 0 cost                         | OK                             | 2.4     |
| 7   | Dedup ring (100 slots, dedup_functions.cpp)                                                 | turn 42.5 / 54.5 min; 10722 / 1919 DUP                          | + stops floods                                  | OK; Beobachten for late echoes | 2.5     |
| 8   | Supersede of queued frames of the same origin / newer {CET} (txring_functions.cpp:882-1040) | 53 / 50 events                                                  | + freshness, - none                             | OK                             | 2.6     |
| 9   | TX pool, priority, CSMA hold, age floor                                                     | queue depth <= 7 / 9 of 20; 0 TTL drops; CSMA_AGED 124 / 8      | 0                                               | OK                             | 2.6     |
| 10  | ACK forward / DM ack, ping (`rx_ack_fwd`, `rx_dm_ack_*`)                                    | 32 / 22 TX = 1.2 % / 1.0 % of TX; 6 pings at -4                 | 0                                               | OK                             | 2.7     |
| 11  | Own beacons: HEY (prio 5, Trickle with gateway override), position (prio 4)                 | 96 + 47 / 118 + 48 sent, 1 / 1 superseded, 0 lost               | 0                                               | OK                             | 2.8     |
| 12  | RX-side drop of foreign junk (`ACK_REJECT`, `[RX];drop;unconfigured`)                       | 3 events at both nodes (same instants)                          | 0                                               | OK                             | 2.9     |

## 2. Detail

### 2.1 Relay decisions, path and hop handling

- Fact [hops]: hop invariant `H + (len(path) - 1)` takes exactly the values {4, 6} for HEY (`@`), {2} for positions (`!`) and {3, 4, 5, 6} for texts (`:`; H02 for {CET}, H04 for server
  texts, i.e. origin settings and server inject), over 14449 + 4440 frames. 0 frames with a call twice in the path, 0 with a flag bit in the H high nibble, longest path 7 (-4) / 6 (-5)
  tokens. Hop and path handling is correct. Verdict OK.
- Fact: relay decisions at -4 (`[LOG] RLY`): `tx` 2575, `gwcap` 351 (position frames at H00 on a gateway; `gwcap` is only the label because the same frames are also `hop0`),
  `hop0` 197 (137 HEY + 60 text), `full` 3. 2575 + 351 + 197 + 3 = 3126 = all `RX-LoRa2`. At -5 1800 of 2303 accepted frames were queued (`RELAY_QUEUED`), 22 of those were discarded by `RING_SUPERSEDE_KEEP` (1778 ring writes = 77 %); the other 503 are hop0/gwcap by inference.
- Fact: relay waits (ring write -> TX): -4 p50 9.7 s, p90 48.6 s, max 158.9 s; -5 p50 8.6 s, p90 13.4 s, max 153.8 s. By class p50 8.3-9.8 s for all relay classes at both nodes
  (the ~9 s CSMA hold); the long tail at -4 are HEY (prio 5, p90 58.7 s) and positions (prio 4, p90 36 s). Nothing beyond the TTLs (HEY 180 s, relay 300 s): 0 `RING_DROP_STALE`.
- Fact: loops: 0 relay writes for frames that already carry the own call (0 `RELAY_LOOP_BLOCKED`, 0 `q=loop`, 0 `RX-LoRa2` with own call in the path) although 5328 of the 14449
  frames at -4 (36.9 %) and 1090 of 4440 at -5 carry the own call (= the node's own relays coming back through neighbours). Verdict OK; compare OE3: also 0 loops.
- Effect: the relay chain is the actual mesh service of these nodes: positive for reach, negative for airtime. 2.2 and 2.3 weigh the second part.

### 2.2 Overhear-cancel (NBR CANCEL) - inferred, not logged

- Fact [relay]: relay ring writes with no TX, no supersede and no TX-read within 400 s ("unaccounted"): -4 383 of 2575 (14.9 %), -5 20 of 1778 (1.1 %). Among the relays that WERE sent,
  36.4 % (-4: 780) and 10.0 % (-5: 173) had a foreign copy decoded while queued. Among the unaccounted ones 381 of 383 (-4) and 19 of 20 (-5) had a foreign copy decoded while queued.
  The first foreign copy came after p50 6.8 s (p5 4.2 s, max 58.7 s) at -4; the unsent entry ends before its wait (p50 9.7 s) would have expired.
- Inference: these are overhear-cancels of the NBR relay mode (`bNBRCANCEL`: slot freed in lora_functions.cpp:1445-1447, no other code path frees a DONE relay slot, checked
  in lora_functions.cpp:3359-3427 / 3724-3805 = retry and external-radio only). The marker `[NBR]|CANCEL` is not printed because `nbrLog` is NULL. Alternative explanations (log
  gaps, supersede, TTL) are excluded: 0 of 383 sit next to a console gap at -4, 1 of 20 at -5; supersede is counted separately (49 / 23); TTL drops are 0.
- Fact [relay]: the cancel depends on WHO sent the first foreign copy, which fits a neighbour-coverage rule: at -4 a first copy from HB9VQQ-3 cancelled 65 of 123 (53 %), HB9VQQ-2 95 of 195 (49 %), HB9TVJ-12
  107 of 250 (43 %), HB9LEK-12 13 of 44 (30 %), DB0HRH-12 14 of 51 (27 %), DB0TOD-12 16 of 64 (25 %), HB9BBE-18 34 of 151 (23 %), HB9VQQ-5 10 of 54 (19 %), DB0SAC-12 24 of 226 (11 %). At -5 HB9VQQ-4's copy cancelled 11 of 59 (19 %),
  DB0SAC-12's 7 of 124 (6 %).
- Fact: at -4 the cancel has a diurnal hole [relay]: per hour (logger 09:39 + n h) cancelled relays 26, 24, 8, 2, 1, 3, then 0 for n = 6..12 (15:39-22:39), then 14, 14, 19, 9, 24, 38, 49, 54, 42,
  30, 26. "Sent with a foreign copy" stays 15-54 per hour all day, so the opportunity is there while the cancel is not. Inference: the neighbour state (who is currently listed as
  hearing the node) changes with time of day; cannot be proven without the NEED/CANCEL lines. Verdict Beobachten; effect when working: positive (14.9 % of -4's relay writes; modelled airtime not used 357 s = 15.8 % of the 2259 s that were sent; 18 s at -5).
- Fact: gateway slots are never cancelled (0 of 253 / 248), see q5 section 3 and 2.4.
- Recommendation: next capture with `--nbrdebug on` on both nodes; then NEED / CANCEL / CANCEL? / REFUSE and the case A/B split become measurable.

### 2.3 Redundancy between the two nodes

- Fact [mutual]: the nodes hear each other directly. -4 decodes 1408 frames whose last hop is HB9VQQ-5 (RSSI p50 -97 dBm, range -105..-95; SNR p50 +3 dB); -5 decodes 2322 frames with last hop
  HB9VQQ-4 (RSSI p50 -100 dBm, range -108..-96; SNR p50 0 dB). 29-78 frames per hour at -4 and 58-122 at -5, every hour of the day (no fading window).
- Fact [unheard]: reciprocity is lopsided. -5 decodes 90.0 % of -4's TX (2291 of 2545) within 3.5 s of the TX start; -4 decodes only 64.0 % of -5's TX (1382 of 2161; texts 54 %, positions 60 %, HEY 68 %).
  The 779 undecoded -5 frames at -4: no activity at -4 in the window for 433 (20 % of -5 TX), CRC error at -4 in the window for 284, -4 itself transmitting for 62. For the 254 undecoded -4 frames at -5:
  112 / 82 / 54 / 6 console gap. Inference: -4 is the busier receiver (RX duty 17.1 % vs 9.7 %, 3.3x the frames) and loses frames to collisions and capture; the link itself is stable and strong (-97 dBm).
- Fact [redundancy]: 2579 distinct ids were transmitted by at least one node; 1846 ids by both (pair = same id, both TX within 30 min): 1945 pairs. Classes: relay/relay 1392, gateway/gateway 220 (136 CET + 84 server
  texts), relay/own 136 and own/relay 136 (each node relays the other's beacon), ack/ack 19, relay/gateway 17, gateway/relay 13, rest 12. Gap between the two TX p50 9.3 s (p5 1.7 s, p90 30 s); first mover -4 1310x, -5 635x.
- Fact: the later node had decoded the earlier node's copy before its own TX in 1696 of 1945 pairs (87 %): relay/relay 1298 of 1392 (93 %), gateway/gateway 144 of 220 (65 %), ack/ack 0 of 19.
  Airtime of the later copies: 1745 s = 42 % of the pair's 4135 s TX airtime; 1537 s (37 %) when the earlier copy was heard first. Relay/relay without {CET}: 1357 pairs, 1284 s (1198 s heard first).
- Inference: this is an upper bound for avoidable airtime. The nodes share only 5 of their direct neighbours (DB0SAC-12, DB0TOD-12, HB9HNO-49, HB9BBE-18, HB9ZG-49; 29-30 % of each node's received frames come from those),
  -4 hears 11 senders -5 never hears (HB9VQQ-2/-3/-5/-6/-11/-12, DB0HRH-12, HB9TVJ-12, HB9LEK-12, DB0TOD-13, HB9WAD-51), -5 hears DO1FE-19 and HB9CMI-40 that -4 does not. 96.5 % of the ids heard at -5 are also heard at
  -4, but 709 of the 2861 ids first accepted at -4 are never heard at -5 and 83 of 2220 the other way. So -4's relay mostly serves a larger inner neighbourhood, -5's relay is a gateway to the east side.
  The second copy is useless only where every listener of the second node already heard the first, which neither the log nor this analysis can decide. Effect: negative for airtime, positive for reach; verdict Beobachten.
- Worked example (id `x45CB934F`, a server text from DO9RKO-99 to group 262, H04): UDP arrives at both nodes 10:14:11; -5 injects at 10:14:13.683 (+2.3 s); -4 decodes -5's copy at 10:14:14.265 and still injects its own at
  10:14:18.472 (4.2 s later), then 6+ relays follow (DB0HRH-12/DB0SAC-12 at 10:14:19, HB9VQQ-3 at :28, DB0TOD-13 at :31 ...) (hb9vqq5.log:2022-2091, hb9vqq4.log:10020-10163). The "moin" text the brief assigns to HB9VQQ-5 is this server
  text injected by both gateways; the 4 texts HB9VQQ-5 originated itself are `A3F15126/28/29/2A` at 31.10. 21:17-21:39 (one TX each, 4.5-16 s after the ring write, each relayed by 6-8 nodes; `A3F15129` never reached -4 directly,
  its first copy at -4 came through HB9BBE-18 after 24.7 s: the relays carried it).

### 2.4 Gateway inject and upload

- Fact [load, redundancy]: `udp_rx` ring writes 253 at -4 and 253 at -5 (src=`udp_rx`); sent 253 / 248 (+ 4 superseded + 1 at a console gap at -5). Of the ids, 220 went through both nodes, 33 only through -4, 28 only through -5
  (server-side routing per gateway; reason unknown, Inference: per-group or per-"heard by" subscription). CET share 142 / 139, other server texts 111 / 109 (origins OE1YEM-10 7, DM4SJ-10 6, DO2QG-1 5, OE5HWN-14 4 ...).
  Server-injected frames are not re-uploaded: they carry S1 and their ids are inserted into `own_msg_id` (`Insert own_msg_id` line), so echoes are `OWN:e`, no loops. Verdict OK.
- Fact: uploads (`TX-UDP`): -4 3166 (2302 distinct ids, 76 % HEY), -5 1911 (1580 distinct, 87 % HEY); 1426 ids were uploaded by both (58 % of the 2456 distinct ids). HEY frames are uploaded by every gateway on purpose
  (`bGATEWAY && (!msg_server || '@')`, lora_functions.cpp:2418) and each gateway appends its own signal report, so the server learns two views of the same HEY. Rate 2.2 / min and 1.3 / min. Effect 0 / + (topology data),
  negligible cost. Verdict OK.

### 2.5 Dedup, id reuse, late echoes

- Fact [dedup]: ring turn (100 ADDs) p50 42.5 min (33.5-52.2) at -4 and 54.5 min (40.3-76.1) at -5. DUP per NEW 2.58 / 0.67; copies per id p50 4 (p90 8, p99 22, max 76) / 1 (p90 3, max 21).
- Fact [dedup, idreuse]: ids accepted again after the ring forgot them: 408 / 233 (-4 / -5), all with gap 52-238 min; 245 / 81 of them were relayed again (relay write for an id already relayed > 52 min earlier, 0 within 5 min).
  Two sources: (a) HB9JAT originates 556 beacons (-4) / 349 (-5) from only 92 / 79 distinct ids (`4EBC7000..4EBC738E`, clusters at multiples of 100, FW 40:a); the gap between genuine reuses is p5 57 min,
  p50 105 min (-4) / 140 min (-5), minimum 34 min, i.e. mostly longer than the ring turn; 5 HB9JAT beacons at -5 (gaps 40-63 min) and 1 other frame at -4 were first-copy DUP and lost to the dedup. Verdict OK /
  Beobachten (HB9JAT's id generator is foreign, cause unknown; src/msgid_counter.cpp persists the counter every 100 and adds 100 after load, which does not by itself explain reuse). (b) HB9HNO-49 echoes 16-37 min late
  (q5 section 5): 392 later bursts at -4, 395 at -5; 30 / 43 of them were accepted as NEW (the original had been missed or aged out) and relayed again. Effect: negative-small and not caused by the pair.
- Fact: no id is accepted twice within one ring turn; no relay-twice within 5 min: 0 loop-type errors. Verdict OK.

### 2.6 Supersede and TX pool health

- Fact: 53 / 50 supersede events [cetrad]: -4: 30 HEY replaced by a newer HEY of the same origin, 1 position, 19 older {CET} replaced by a newer one (+ 3 `SUPERSEDE_KEEP` in the other direction); -5: 3 HEY, 25 {CET} replaced +
  22 kept-old. Effect positive: the older beacon of an origin is dropped for its newer one; freshness wins, ring depth stays small.
- Fact [load]: depth at ring write p50 0, p90 2 / 1, p99 4 / 3, max 7 / 9 of 20; hourly mean depth slope -0.0016 / -0.0061 slots per hour (no build-up); TX per hour 68-140 / 63-117, no daily trend. 0 TTL / full / prio drops.
  Waits by class (p50 / p90 / max, seconds): gateway text 9.0 / 11.3 / 60 and 8.7 / 11.6 / 27; ACK 23 / 35 / 43 and 16 / 37 / 43; relay text 8.6 / 16.6 / 62 and 8.9 / 12.7 / 49; relay position 8.9 / 36 / 67 and 8.3 / 11.5 / 61;
  relay HEY 9.8 / 58.7 / 159 and 8.7 / 16.1 / 154; own HEY 21.0 / 70.8 / 166.5 and 12.8 / 49.1 / 156.7; own position 13.2 / 40.4 / 80 and 11.2 / 42.7 / 78. 3 / 4 waits > 150 s (all HEY, TTL 180 s: margin 13.5 s at the maximum).
- Fact: CSMA: 1228 / 948 `CAD_BUSY`, 124 / 8 `CSMA_AGED` (stage 1, e.g. age 20.6 s and 25.4 s in the first events). No starvation as seen on the busy OE3 nodes: own beacons 96 of 97 and 47 of 47 aired at -4, 118 of 119 and 48 of 48 at -5
  (the missing ones were superseded by newer ones). TX duty 2.6 / 2.2 % against 4.2 % at OE3XOC. Verdict OK.

### 2.7 Ping and ACK load

- Fact: ACK frames transmitted: 32 / 22 (-4: 22 forwards + 5 + 5 DM acks; -5: 8 + 7 + 7), 1.2 % / 1.0 % of the TX count, ~8.5 s airtime. Pings: 6 `{ping}` frames from -4 to HB9ZG-49 at 31.10. 19:16-19:28 (manual test; no auto-ping),
  5 s airtime. ACK ids forwarded by both nodes: 19 pairs, none cancellable (ACK frames have no cancel path, CORE 4) - 8.5 s in total. Compare OE3: 41 % of TX count were ACK relays. Verdict OK; effect 0.
- Fact: `ACK_SKIP_READY` 6 times at -4 and 1 at -5: an own text still READY when its ACK/echo arrives is not released (TXP-01 behaviour, lora_functions.cpp:1090-1130) - intended. In one case (`A4029187`, 21:27:05) five
  copies of the text from other gateways arrived (1.6-16.0 s) before -4's own first TX at +27.7 s: the network (via the server) was faster than the node's own CSMA wait for a user text.

### 2.8 Own beacons and Trickle

- Fact [load]: own HEY (prio 5) -4 96 TX of 97 writes, -5 118 of 119; own position -4 47 / 47, -5 48 / 48 at a fixed 1830 s period; HEY period p50 900 s (60 s .. 1740 s). Trickle (esp32_main.cpp:4797-4830): -4 SEND 97 /
  SUPPRESS 44 / TOPO_CHANGE 15; -5 119 / 54 / 29. 71 of the 97 and 58 of the 119 SEND lines read `consistent=N<k=2` with N >= 2: the gateway rule (heyTrickleSuppress, loop_functions.cpp:5682: a gateway is not
  suppressed once its last HEY is >= Imax old) overrides Trickle, so both nodes effectively beacon every 15 min. Effect 0 (+ for topology freshness). Verdict OK; the log wording is wrong (3.2).

### 2.9 Foreign junk

- Fact: three times (31.10. 12:20:58, 14:53:13, 22:07:54) both nodes decode the same 12-byte frame with `FCS:0000`, `ACK_REJECT b5=2B/30`, `RX_DEDUP_NEW msg_id=33332D52` and `[RX];drop;unconfigured`
  (hb9vqq4.log 12:20:58.024-.025, hb9vqq5.log 12:20:58.073). Inference: a non-MeshCom or unconfigured sender on the frequency; the node drops it correctly. Effect 0; verdict OK.

## 3. Suspected defects (code sites)

1. **`RLY ... q=full` printed for `RING_SUPERSEDE_KEEP`** [confirmed, cosmetic]: 3 `q=full prio=0 slot=-1` lines at -4 (11:05:24.526 hb9vqq4.log:23185, 01.11. 06:18:28.990, 06:58:54.300) coincide exactly with the 3
   `RING_SUPERSEDE_KEEP` lines; ring depth was <= 3. Site: `addTxRingEntry` returns -1 for `supKeepNew` (src/txring_functions.cpp:1035) and the relay block labels every non-slot result "full" unless the {CET} gate fired
   (src/lora_functions.cpp:2651). Effect: a reader sees "ring full" where a newer {CET} was already queued; the OE3 analysis read `full` as a capacity signal. Fix: label `supersede_keep` (or `cet_newer`) - not implemented.
2. **`[MC-TRICKLE] SEND consistent=%d<k=%d` wrong text when the gateway rule forces the send** [confirmed, cosmetic]: 71/97 and 58/119 lines print e.g. `consistent=17<k=2` (hb9vqq4.log:152). Site src/esp32/esp32_main.cpp:4825
   (the nRF52 counterpart prints a different text, nrf52_main.cpp:2220). Fix: print the reason (`gateway_imax`).
3. **Missing proof of the overhear-cancel**: no defect, but the evidence in 2.2 is inferred only because `[NBR]` logging is off.
4. **No coordination of two gateways in mutual range** [design gap, shared with CORE 5]: 220 gateway pairs, 65 % of the later injects had heard the first. Sites src/gw_udp_ring.h:44-48 and src/nbr_matrix.cpp:1777-1852 (alone set
   never empty for a server slot). Beobachten.
5. **Operator-side**: HB9HNO-49 on `FW:35:h` re-radiates every frame ~24 min late (q5 section 5); HB9VQQ-5's console is kicked at every full hour (q5 section 0); HB9JAT's id pool.

No defect that loses data was found in the two nodes.

## 4. What stays unmeasurable at HB9VQQ-5

- Reasons for not relaying (hop0 / gwcap / ping / loop / full) of 525 accepted frames, the `OWN:e` echo flag, ACK receptions (12-byte frames print nothing), per-frame TX `src/prio/cad/q` fields (rebuilt from ring lines),
  `STAT` windows (replaced by `CHANNEL_UTIL`), NEED / CANCEL / REFUSE and case A / B (both nodes), the real build (both nodes: -4's `[LOG] STAT` shows `fw=40a/20261030`, i.e. a release stamp 30.10.2026, -5 shows nothing).
- What stays unknown for both: whether `bNBRRELAY` / `bNBRCANCEL` are really on (inferred from 2.2 only), the neighbours' firmware beyond `FW:` fields, the reason for HB9JAT's id pattern.

## 5. Comparison with the OE3 findings

| Mechanism                        | OE3 cluster (CORE)                                        | HB9 pair                                                           |
| -------------------------------- | --------------------------------------------------------- | ------------------------------------------------------------------ |
| ACK / ping load                  | 41 % of TX count (foreign gateways ACK pings)             | 1.0-1.2 % (6 manual pings)                                         |
| Overhear-cancel                  | acts at OE3XHL only (23.9 % of relays); other four case A | acts at -4 (14.9 %, inferred) and rarely at -5 (1.1 %)             |
| {CET} sole-provider case A       | 666 of 666                                                | gateway slots never cancelled (0 of 253 / 248); not provable       |
| Own-beacon starvation            | OC 34/49 WX lost                                          | none (0 lost)                                                      |
| TTL / stale gate                 | 8 + 4 stale relays stopped by the gate                    | 0 drops; nothing stale was new; HB9HNO-49 (4.35h) radiates stale   |
| Dedup re-relay after expiry      | 5 after 22-25 min                                         | 245 / 81 after 52-238 min (id reuse + late echoes), ring 42-55 min |
| Loops / hop growth               | none                                                      | none                                                               |
| Parallel relay between own nodes | 36 % upper bound                                          | 42 % upper bound (37 % when the first copy was heard)              |
| RX error share                   | 7-32 % (DB0AIS 66 %)                                      | -4 11.6 %, -5 52.7 % (weak far signals)                            |

## 6. Verdict per node

- **HB9VQQ-4**: stable. No build-up (depth <= 7, slope ~0), 0 drops, 0 loops, hop/path correct, cancel working (inferred) but absent 15:39-22:39. Positive: relay chain, supersede, cancel, ACK handling. Negative: 1745 s second-copy
  airtime shared with -5 (upper bound), late echoes of HB9HNO-49. Beobachten: diurnal hole in the cancel, redundancy with -5, `q=full` label. Handlungsbedarf: none.
- **HB9VQQ-5**: stable. Same, with 52.7 % CRC errors of weak far signals (marginal, not a fault), 20 cancels, 23 re-relays of HB9HNO-49's late frames. Beobachten: console kicks at every full hour, 22 `SUPERSEDE_KEEP` that
  silently discard -63 relays, redundancy with -4. Handlungsbedarf: none.
- **Network**: nothing builds up, nothing is lost by the two nodes, double transmissions are the visible cost (42 % upper bound between the pair, ~4 injects per {CET} and per server text network-wide).

## 7. Check index

`python3 -I verify/hb9_net_checks/hb9_net_checks.py <norm_dir> parse relay rxerr mutual unheard redundancy dedup idreuse stale load cetrad hops misc idtrace=45CB934F` -> every number above (idtrace prints the worked example
in 2.3 without payload). `out_all.json` in that directory is the output of `... all`.
