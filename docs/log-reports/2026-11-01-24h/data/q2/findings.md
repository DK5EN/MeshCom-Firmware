# Q2 TX slot pool - findings (persisted by orchestrator from the agent's final report; numbers in results.json)

Verdicts: HL OK, WJ OK, DB0AIS OK; IA Beobachten; IR Beobachten (decode drops); OC Handlungsbedarf (policy: prio 4/5 starvation). Overall: pool mechanics OK; CSMA hold / prio 4/5 policy needs work.

## Pool mechanics (fact, OK)
- 0 RING_DROP_NEW / DROP_PRIO / REJECT / REARM in all six logs. Max depth 16/20 (OE3XIA, log-start backlog that drained); time at >=15/20: 0.5 min (IA only). Hourly mean slope -0.013..+0.012 slots/h: no build-up.
- RING_STATUS retrying = 0 in every sample: no stuck INFLIGHT/SENT slot. Entry-lifetime model reproduces logged queued= in 95.7-99.6 % of writes (rest = first 2 min, pre-log backlog).
- Lowest free slot always taken: 0 holes (OC 3, adjacent to garbled lines). STAT drop= sums equal RING_DROP_STALE counts on all 5 OE3 nodes.

## Fate accounting (closes to 100 %, 0 unexplained). Writes: OE3 22853, DB0AIS 2837
| Fate | OE3 total | per node |
|---|---|---|
| TX_READ + on air | 92.43 % | HL 87.7, IA 95.67, IR 78.44, OC 96.65, WJ 99.87, DB 99.82 |
| TX_READ but dropped at decode (no air) | 3.01 % | IR 20.8 |
| superseded in place | 0.97 % | |
| NBR-cancelled | 2.58 % | HL 11.82 (only node that cancels; 589/589 CANCEL matched an unsent entry) |
| STALE ttl | 0.99 % | IA 2.11, OC 1.91 |
| pending at log end | 0.01 % | |
| DROP_PRIO/NEW/CET | 0 | |

## Prio 4/5 starve at busy nodes, then hit TTL (policy gap) - Handlungsbedarf
- Own HEY: OE3XOC airs only 42 % of episodes (57/98 lost; aired gap p50 28 min, max 179 min); OE3XIA 79 % (40 lost).
- Own WX (written xx:00:14 and xx:30:44 on all OE3 nodes): lost OC 34/49, IA 11/49, IR 5/22, WJ 4/50, HL 0/49, DB 0/50.
- Code: csma_timing.h:99-112 age floor covers prio 1-3 only; txring_functions.cpp:213-255 strict priority in getNextTxSlot; :566-640 TTL sweep drops; prio 4/5 hold 5.5 s re-armed per received frame.
- Supersede resets ringEnqueueTime (txring_functions.cpp:1007): own-HEY chains live 537-902 s without going stale - benign, but TTL does not bound beacon age.

## ACK/relay wait 20-60 s per hop on IA/IR/OC (CSMA hold, not queue depth)
- ACK wait p50/p90/max s: IA 21.9/34.7/61, IR 18.8/31.2/60.5, OC 23.3/33.8/64.6; HL 6.6, WJ 9.6, DB 6.6 (p50). Share >=20 s: IA 55 %, IR 47 %, OC 65 %.
- Median wait tracks CAD_BUSY per TX: HL 0.8, WJ 1.2, OC 2.9, IR 3.8, IA 4.8. At queue depth 1 median wait still 12 s on IA/OC. Max ACK wait 64.6 s vs 90 s TTL.
- Correlation with channel util weak/inverted (Spearman +0.12 HL, -0.01 IA, -0.14 IR, -0.22 OC, +0.16 WJ, +0.29 DB).
- Risk (inference, not observed): DM ACK over three such hops can exceed the 40 s sender retry ladder; no DM retries in these logs.

## OE3XIR: 688 silent decode drops (20.8 % of writes, 06:47:55-18:15:30) - bug candidate
- phone_msg {ping} to destination "255." written every minute at prio 1, wins the pick, dropped by decodeAPRS ("Destination-CallSign Error [255.]", aprs_functions.cpp:369-373); doTX frees slot via TXRING_FIN_DROP (lora_functions.cpp:3270-3274). No [LOG] TX, no ACK, no client feedback; a CAD cycle + hold wasted each time. Orchestrator check: 689 error lines, 0 TX-LoRa {ping} on IR.
- OE3XWJ pings valid destination 2025: 1529 sent, all on air. Fix site: validate destination before enqueue.

## Sub-questions
- Occupancy queued p50/p90/max: HL 1/2/7, IA 0/5/15, IR 1/4/10, OC 2/5/12, WJ 0/1/8, DB 0/1/3. No day/night structure; OC util steady 23-31 %.
- Relay prio 3 latency p50/p90 s: HL 9.0/16.5, IA 12.4/42.8, IR 17.8/53.8, OC 13.9/53.3, WJ 9.2/13.9, DB 6.9/10.4. Prio 5 p99: IA 171.6, OC 170, HL 109.6. HL prio 4/5 p90 ~60 s = NBR case-B hold (inferred; 60 s cap txring_functions.cpp:385-455). 7 reads at 180-181 s = 2-s sweep granularity.
- Own double TX within 30 min: IA 12, IR 7, OC 5, WJ 7, HL 0, DB 0. Mostly GW-ACK pairs by design (GWACK-01, two writes 13 ms apart, lora_functions.cpp:2323/2334; all 12 of IA). 8 = same text id relayed again after 19-26 min on IR/OC/WJ (D90B900C, 4DCC80B6, BBC85361, E4BA22CC, 4913D32A) - inference: re-injection past dedup ring (100 entries ~20 min). None from pool or retry ladder.
- Supersede: HL 21, IA 94, IR 16, OC 93, WJ 1, DB 5; almost all HEY (OC 2 positions, DB 3 {CET}). 45 superseded chains ended without air (IA 17, OC 28), all TTL, due to prio-5 starvation. HL: 8 relay-HEY chains ended in NBR cancel (covered).
- Cross-node (2495 groups >=2 nodes, 1462 >=3): mean rank WJ 0.24 (fastest in 46 %), HL 0.41, IR 0.60, OC 0.62, IA 0.66 (slowest in 29 %). IA-WJ median +13 s; same ranking for prio 5 and prio 3-4. Inference: local channel occupancy, not firmware.

## Caveats
- Retry ladder / txRingRearmRetry never ran (0 RING_REARM, 0 retry>0); TXP "retry rearms parked slot" untested by this data.
- Build unknown; TXP W1b+ inferred from INFLIGHT/supersede markers. 5 RING_WRITE lines garbled by serial interleave (HL 2, OC 3).
- Only HL prints [NBR]|CANCEL - inference: cancel mode off on the other four.
- Clocks: OE3XIR [LOG] clock +3599 s vs logger; other OE3 -1 s. DB0AIS node clock +1 h until capture line 18015 (19:20:26 -> 18:20:31).
- RING_WRITE queued= is depth before the write; RING_TX_READ queued= and [LOG] TX q= include in-flight slot.

## Open questions
- Own HEY/WX loss at busy nodes acceptable? Options: age floor for prio 4/5 (or own origin), or longer TTL (TTL decision open in docs/campaign-tx-pool.md).
- Validate phone-ping destination at enqueue?
