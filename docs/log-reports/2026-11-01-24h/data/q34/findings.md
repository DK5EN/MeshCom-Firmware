# Q3/Q4 - Time-based decay in the TX slots (24-h OE3 logs, 2026-10-31 06:30 -> 2026-11-01 07:50)

Scope: (a) TTL per priority + {CET} gate (`RING_DROP_STALE`), (b) CSMA age floor (`CSMA_AGED`). Time base = logger wall clock.
Numbers: `results.json`; scripts: `scripts/` (parse.py -> drops.py -> analyze_a.py -> cf.py -> ttl_sens.py -> csma.py -> merge.py -> final.py).
Build per node unknown; "inferred" marks conclusions drawn from behaviour.

## 0. Mechanism (code, fact)

- Sweep: `txRingAgeBackground()` runs in the main-loop tick (`src/esp32/loop_actions_esp32.cpp:74`, `src/nrf52/loop_actions_nrf52.cpp:61`); body `txRingSweep()` at `src/txring_functions.cpp:567-645`. Observed drop ages 180-182 s (prio 5) and 120-122 s (prio 4): strict `age > ttl` plus ~2 s granularity.
- Which statuses age (`src/txring_ttl.h:46-64`): prio 5 (HEY) in any status, 180 s; DONE-only: ACK frame prio 1 90 s, relay prio 3 300 s, position prio 4 120 s. Prio 2, own READY/SENT texts, EXT_PENDING, INFLIGHT never age.
- DONE does NOT mean "sent". `RING_STATUS_DONE` = "final, no retransmission" (`configuration_global.h:436`). It is set at enqueue for relays/ACKs/udp_rx and forced for all non-text frames in the retry loop (`lora_functions.cpp:3316`). Own HEY (`src=auto_pos`) and own WX position (`src=user_wx`) are written `status=FF` as well. When doTX sends a DONE/non-text slot, `txRingFinishTx()` frees it (`txring_functions.cpp:299-321`). So a STALE drop of a DONE entry means this node never radiated it.
- Log check of that: 232/232 drops have no `RING_TX_READ` and no `[LOG] TX` for the same id between write and drop (`drops.json: own_txread_before_drop=0`). Fact: no "sent, then aged" case exists - the question (i) is answered "never sent".
- Selection is strict priority, FIFO inside a priority, no anti-starvation (`getNextTxSlot`, `txring_functions.cpp:213-255`); TTL is the only bound. Supersede (HEY/POS, same originator) replaces an unsent entry in place and resets the enqueue time (Q2).
- {CET} gate: `cetFrameStale()` (`cet_age.h:120-135`, 150 s) runs in the same sweep and again before each send (`lora_functions.cpp:2915`).
- `RING_DROP_STALE` carries slot/prio/age/id/reason only; type and source need the RING_WRITE line (5 drops at OE3XIA 06:41:14-24 predate the log: unclassifiable).

## 1. Every STALE drop (232 total, all `reason=ttl`)

Fact. No prio 1 (ACK), no prio 3 (relay text), no `reason=cet`, no text/ACK frame dropped. Only HEY (prio 5, age 180-182 s) and positions (prio 4, age 120-122 s).

| node | drops | p4 own WX pos | p4 relay pos | p5 own HEY | p5 relay HEY | pre-log |
| --- | --- | --- | --- | --- | --- | --- |
| OE3XHL | 3 | 0 | 0 | 2 | 1 | 0 |
| OE3XIA | 93 | 11 | 2 | 40 | 35 | 5 |
| OE3XIR (11.5 h) | 8 | 5 | 3 | 0 | 0 | 0 |
| OE3XOC | 124 | 34 | 9 | 57 | 24 | 0 |
| OE3XWJ | 4 | 4 | 0 | 0 | 0 | 0 |
| total | 232 | 54 | 14 | 99 | 60 | 5 |

- Sizes (ring len): own HEY 31-32 B, relay HEY 70-106 B, own WX pos 88-107 B, relay pos 109-117 B.
- Drops are spread evenly over the day (XOC 1-9/h, XIA 0-10/h); ring depth at write mean ~2, max 15 - drops are starvation, not overflow (no `RING_DROP_PRIO/NEW` in any log).
- OE3XHL has 3 drops but 589 `[NBR]|CANCEL` lines; the other four have none (inferred: HL runs `--nbrrelay on`, `lora_functions.cpp:3998`, which removes relays a neighbour already sent; the others do not).
- Per-drop table: `scripts/` output `work/drops_classified.json` (not in repo); counts above are in `results.json` `nodes.*.drop_table`.

## 2. LOSS test (evidence window write-30 min .. drop+30 min, five nodes)

### 2a. Own beacons: 153 drops, real RF loss of that instance (Handlungsbedarf at XOC/XIA)

Fact. Own HEY (99) and own WX position (54) were never radiated; no other node ever received those ids (`rx_nodes_any` empty for all 153). Effective RF delivery = sent/written:

| node/kind | written | sent | dropped | superseded | sent % | longest RF silence |
| --- | --- | --- | --- | --- | --- | --- |
| XOC WX pos | 49 | 15 | 34 | 0 | 31 | 7.64 h (21:14:16 -> 04:52:36) |
| XOC HEY | 141 | 41 | 57 | 43 | 29 | 3.0 h |
| XIA WX pos | 49 | 38 | 11 | 0 | 78 | 2.06 h |
| XIA HEY | 246 | 149 | 40 | 57 | 61 | 1.0 h |
| XIR WX pos | 22 | 17 | 5 | 0 | 77 | 2.0 h |
| XWJ WX pos | 50 | 46 | 4 | 0 | 92 | 1.03 h |
| XHL HEY / WX | 103 / 49 | 99 / 49 | 2 / 0 | 2 / 0 | 96 / 100 | 0.53 h |

(HEY superseded = new HEY replaced the unsent older one, by design, not counted as TTL loss.)
- Mitigation fact: WX positions at XIA/XOC/XWJ were also uplinked by `TX-UDP` at write time (54 of 54 matched at nodes that have TX-UDP; XIR/XHL show no TX-UDP lines). So Internet visibility survived; the RF copy (neighbours, other RF-only users) was lost.
- Example (XOC 2026-10-31 14:37:44.43): WX 81125FC5 written into an empty ring (`queued=0/20`), `TX-UDP` at 14:37:44.46, never on air; in the 120 s XOC sent 5 higher-prio frames (2 prio-3 texts, 3 ACKs; CAD-busy cascades attempt 1..6 at 14:37:58 and 14:38:44); `RING_DROP_STALE ... prio=4 age_s=120` at 14:39:45.27.
- Verdict: Handlungsbedarf (operator decision). Cause is design: own beacons share prio 4/5 with foreign relays and strict priority has no aging; XOC runs at its TX ceiling (~245 TX/h, flat all day).

### 2b. Relayed HEY / positions: 74 drops, mostly redundant

Classification (hierarchy; window 30 min), n=74 (HEY 60, POS 14):

| class | evidence | HEY | POS | total | % |
| --- | --- | --- | --- | --- | --- |
| redundant_cluster_tx | another of the five nodes transmitted the id after our write | 32 | 5 | 37 | 50 |
| redundant_heard_lower_hop | we re-heard a copy with lower hop (went through another relay) | 13 | 5 | 18 | 24 |
| weak | cluster node sent it before our write / same-hop re-hear, no onward proof | 8 | 2 | 10 | 14 |
| none | nobody (five nodes) carried or re-sent it | 7 | 2 | 9 | 12 |

- Redundant elsewhere 55/74 = 74 %; potential real loss 19 (26 %), of which 9 with no evidence at all (OE3XIA 4, OE3XOC 5).
- All 19 are refreshable types; 13 of 19 (5 of the 9 "none") got a newer frame from the same originator within 30 min; median refresh of all relay drops 707 s.
- No text (3A) and no ACK was dropped: nothing in the "matters most" class was lost to TTL.
- Examples: redundant - XIA 2026-10-31 07:11:25.0 HEY 86C8D169 (OE3REI-14): XHL TX 160.7 s and XIR TX 172.3 s before the drop, all five nodes received it. Lower hop - XIA 06:52:59.4 HEY 4DCBE2CC (OE3HES-1): XHL TX at -131.6 s, XIA re-heard H3 at -133.7 s and H0 at -101.2/-97.7 s. Potential loss - XOC 11:35:09.5 HEY 1AFB2190 (OE3KJO-64, H04, RSSI -108, direct): written 11:32:09.13, never heard by another node; in those 3 min XOC sent 10 ACKs (waits 13-41 s); originator's next HEY 1AFB2192 at 11:43:46.
- Inference: in builds with overhear-cancel many of the 55 redundant entries would have been cancelled earlier instead of aging (HL: 589 cancels). Fact: XIA/XOC had no cancel markers.
- Verdict: OK (relay side: noise, refreshable).

## 3. {CET} drops

- Fact: 0 `RING_DROP_STALE reason=cet` on all five nodes; no sent-CET was untrusted.
- Fact: 666 CET frames radiated (`[CET];tx`): payload age at TX p50 8-22 s; max XIR 147 s (queue wait 147.9 s, 3 s below the 150 s gate), XOC 129 s, XIA 121 s, HL 24 s, WJ 32 s; frames >120 s: 3. Nothing >= 150 s left the radio. A CET frame can therefore still radiate a time up to ~2.5 min old (within the configured tolerance).
- Not a ring drop (different path, for Q5): `[CET];drop;where;relay` at admission on XWJ (6x) and XOC (2x), payload age 1565-2601 s.
- Verdict: OK (gate never needed; it is within 3 s of firing once).

## 4. GAIN

### 4a. Airtime (fact + stated assumptions)
ToA: SF11, BW250, CR4/6, preamble 8, explicit header, CRC, LDRO off (MOD 8 = fastslow, country 8 = EU8; `lora_setchip.cpp:181-203`, `country_profile.cpp:90`). Check: observed TX_START->OnTXDone is a constant ~40 ms below the formula over 12..153 B. Bytes = ring len.

| node | dropped ToA | node TX ToA | share | per hour |
| --- | --- | --- | --- | --- |
| XOC | 88.7 s | 3821 s | 2.32 % | 3.5 s |
| XIA | 63.0 s | 2494 s | 2.53 % | 2.5 s |
| XIR | 8.7 s | 1558 s | 0.56 % | 0.8 s |
| XWJ | 4.6 s | 2976 s | 0.15 % | 0.2 s |
| XHL | 2.1 s | 2714 s | 0.08 % | 0.1 s |
| all | 167 s | 13564 s | 1.23 % | |

- Verdict: airtime gain is small (about 1.2 % of cluster TX airtime, 0.1 % channel time per node). Neutral.

### 4b. Slots freed / counterfactual "no TTL" (model, first order)
Model (`scripts/cf.py`): strict-priority replay of the observed prio 3-5 arrivals on the observed prio 3-5 service instants, supersede applied. Validation: with TTL on it reproduces the real pick in 99.0 % (XIA), 99.6 % (XOC), 100 % (XIR, XWJ). XHL not modelled (cancel path, match 56 %). Limit: extra idle TX instants are not added, so stale air-age is an upper bound.

| node | stale frames sent in sim | stale air-age p50 / p90 / max | fresh prio 3-5 frames delayed | median delay | time-weighted depth with TTL -> no TTL | max depth |
| --- | --- | --- | --- | --- | --- | --- |
| XIA | 82 of 88 (6 superseded) | 201 / 521 / 644 s | 45 % | 94 s | 1.10 -> 2.84 | 15 -> 17 |
| XOC | 120 of 124 | 199 / 308 / 596 s | 38 % | 66 s | 1.31 -> 2.62 | 11 -> 15 |
| XIR | 8 of 8 | 134 / 149 / 175 s | 14 % | 54 s | 0.85 -> 1.12 | 8 -> 8 |
| XWJ | 4 of 4 | 123 / 129 / 132 s | 1.4 % | 5 s | 0.82 -> 0.84 | 9 -> 9 |

- Slot-seconds freed (real, sum of drop ages): XOC 19801 (0.22 slot avg of 20), XIA 15110 (0.17), XIR 964, XWJ ~480, HL 542.
- Fact: ring never exceeded 15/20 in the real run; the model's no-TTL run does not overflow either (max 17). So the gain is not overflow protection at this load.
- Inference (model): the gain is freshness. Each stale frame that stays consumes a TX instant; with TTL off, 38-45 % of fresh prio 4/5 frames at XIA/XOC would go out later (median +1-1.5 min, p90 +4-6 min) and stale ones would be radiated 2-10 min old (p50 ~200 s).
- Verdict: OK (positive: freshness, bounded queue; cost see section 2).

## 5. Threshold check (TTL headroom, sent frames, 5 nodes)

| prio / TTL | sent | max sent wait | >= 80 % TTL | reached TTL (dropped) |
| --- | --- | --- | --- | --- |
| 1 ACK / 90 s | 8902 | 64.6 s (XOC) | 0 (>= 50 %: 106) | 0 |
| 3 relay / 300 s | 1669 | 163.9 s (XWJ) | 0 | 0 |
| 4 pos / 120 s | 2614 | 120.2 s | 42 (1.6 %) | 70 (2.6 %) |
| 5 HEY / 180 s | 6409 | 181.4 s | 133 (2.1 %) | 162 (2.5 %) |

- Fact: ACK and relay TTL were never exercised; ACK headroom is only 1.4x (XOC p99 47.9 s, max 64.6 s) - it is the next TTL to bite if the ACK load grows ~40 %. Relay TTL has 1.8x.
- Fact: sent waits of 181.4 s / 120.2 s exceed the TTL by ~1 s: pick races the 2-s sweep; harmless.
- TTL sensitivity (pooled XIA/XIR/XOC/XWJ, `ttl_sens.json`; exact for TTL <= current, model for longer):
  - prio 5 (n=5215): TTL 120 s -> 404 drops (7.7 %); 150 -> 263 (5.0 %); 180 -> 156 (3.0 %, 94 own); 240 -> 50 (1.0 %); 300 -> 33 (0.6 %).
  - prio 4 (n=2147): 60 s -> 221 (10.3 %); 90 -> 120 (5.6 %); 120 -> 69 (3.2 %, 55 own); 180 -> 13 (0.6 %); 240 -> 2.
- Inference for the open decision: a shorter TTL multiplies drops (5-10 % of frames) and mainly costs the same starved own beacons; a longer TTL (prio 4 240 s, prio 5 300 s) removes 97 %/79 % of drops at the price of frames radiated up to 4-5 min old. The own-vs-relay split (own = 60 % of p5 and 80 % of p4 drops) points to a per-class rule (own beacons longer or exempt, relays unchanged) rather than a global change. Decision stays with the operator.

## 6. (b) CSMA age floor `CSMA_AGED`

Mechanism (fact): `lora_functions.cpp:4030-4045`, `csma_timing.h:71-110`. Logged on every RX re-arm while the head slot is prio 1-3 and older than 20 s (stage 1) / 60 s (stage 2): events are re-arms, not slots (3.9 events per slot episode). Hold = min(today, aged): stage 1 = clamp(2850 - ToA - 175, 500, 1500) + 0-175 ms; stage 2 = 100 + jitter. Today = 3000 (ACK/prio 2) / 4500 (relay) ms scaled 5/6 and 2/3 on retries, +0-350 ms; rapid mode 100 ms from attempt 3 (floor moot).

| node | events | slot episodes | stage 2 events | prio<=3 TX waiting >= 20 s | prio 1 ACK wait p50 | CAD_BUSY_1 per TX | RX/h | RX_ERROR/h |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| XOC | 7476 | 1937 | 4 | 62.7 % | 23.3 s | 1.57 | 1722 | 483 |
| XIA | 3593 | 846 | 1 | 51.1 % | 21.9 s | 2.52 | 942 | 445 |
| XIR | 2439 | 777 | 5 | 52.1 % | 18.8 s | 2.08 | 1238 | 418 |
| XWJ | 122 | 64 | 1 | 6.2 % | 9.6 s | 0.68 | 1217 | 302 |
| XHL | 79 | 50 | 0 | 4.0 % | 6.6 s | 0.44 | 872 | 69 |

- Fact: nearly all episodes are prio 1 ACKs (XOC 1932/1937); prio 3 only ~140. Stage 2 is practically unreachable (10 events/node-day): it needs a prio<=3 head waiting >= 60 s, and ACK TTL is 90 s.
- Why XIA/XOC/XIR high, XWJ/XHL low (inference): the high nodes see a much busier channel at their site (CAD busy 1.6-2.5 per TX vs 0.4-0.7; RX errors 420-480/h vs 69-300/h) and forward 1.4-3.4k ACKs/day, so 50-63 % of prio<=3 frames pass 20 s. XWJ forwards almost no ACKs (49); XHL has low CAD-busy and cancels relays. Event count scales with RX/h x time-in-aged-state (XOC 2x XIA).
- DB0AIS (other mesh/day): 0 `CSMA_AGED`, 0 `RING_DROP_STALE`. Fact: of 883 prio<=3 TX only one waited > 20 s (max 20.2 s); prio 4 max 24.5 s, prio 5 max 91.8 s (TTL 180 s). It never met the trigger; this does not show a build difference (build unknown, cannot tell).
- Did the floor shorten time to TX? Not measurable directly (no run without the floor; DB0AIS never aged). Quiet-gap replay on real RX instants (`csma.py`): after the first aged event the slot is sent p50 4.1-5.9 s / p90 9.9-12.8 s later; the shortened hold saves a median 1.6 s (if the unfloored hold were attempt 0) down to 0.6 s (attempt 2) per episode = 63-78 % resp. 13-19 % of the remaining wait. The attempt is not logged, so the true value lies in between (inferred). Sim fire precedes real TX by p50 0.2-3 s (CAD busy / other).
- Collision signs (aged prio 1 vs not aged, same node): RX_ERROR within 3 s after own TX 17.0 vs 16.3 % (XOC), 21.4 vs 25.3 (XIA), 13.3 vs 14.8 (XIR) - no difference (random 3-s window 30-36 %). CAD attempts at TX are higher for aged (XOC 1.67 vs 1.02) - confounded, aged ones sit in busier periods. "Echo" (neighbour re-sent the id within 60 s) lower for aged (83.7 vs 92.7 %, 79.6 vs 82.6, 76.0 vs 88.7): consistent with older frames already seen by neighbours; not proof of collisions.
- Double transmissions: same ACK id twice within 120 s: XIA 12, XOC 4, XIR 3 pairs, second always aged (wait > 20 s, H4 both). 0.3-2 % of aged ACKs vs 0.1-0.35 % of fresh. Two ring entries for one ACK id are queued in parallel (ACK forward has no overhear-cancel); the floor does not create them but shorter holds do not prevent them. Beobachten, low volume.
- Verdict: neutral to positive. It acts on a regime that is the normal case at three of five nodes (median ACK wait 19-23 s, above the 20 s trigger), so stage 1 is effectively permanent there; ACK waits still reach 61-65 s. It does nothing for prio 4/5 (own beacons), the actual TTL victims.

## 7. Suspected issues / suggestions (none is a proven firmware bug)

1. Own beacons (prio 4 WX, prio 5 HEY) starve behind relays/ACKs at saturated nodes: `getMessagePriority()` (`txring_functions.cpp:~120-150`) and `txRingTtlMs()` (`txring_ttl.h:46-64`) do not distinguish own from relayed frames. Observation, design question.
2. `CSMA_AGED` omits attempt and "today" value and prints even when `min()` leaves today unchanged, so its effect cannot be measured from logs (`lora_functions.cpp:4040`). Low priority.
3. `RING_DROP_STALE` lacks type/source (`txring_functions.cpp:639`); needs the RING_WRITE line, impossible for entries older than the log start.

## Caveats

- Counterfactual is first order (no extra idle TX instants, XHL excluded); TTL values above the current one are model-based.
- Loss test sees only five nodes plus what they hear; "no evidence" is not proof that nobody else carried a frame.
- Msg_id reuse (~4 h) handled by 30-min windows; OE3XIR ends 18:16 (11.5 h).
- Pre-log entries: 5 drops (XIA 06:41:14-24) unclassified.
