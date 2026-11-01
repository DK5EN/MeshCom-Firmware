# Q6 - Which manipulations does a node perform, and what do they do to the network?

Scope: 5 OE3 logs (2026-10-31 06:27 -> 2026-11-01 07:57; OE3XIR ends 10-31 18:16 = 11.5 h), DB0AIS standalone.
All numbers: `results.json`; scripts: `scripts/` (parse.py -> work/*.jsonl -> analysis scripts -> aggregate.py).
Conventions: **fact** = counted in the log or read in source; **inference** = my reading, marked. Airtime computed from TX `len=` at
SF11/BW250/CR4:6 (MOD 8/8); check: cluster sum 13563 s vs. sum of `[LOG] STAT tx=` 13546 s (0.1 % off).
Clocks: logger clocks of the five logs agree (TX at A -> RX at B median lag 0.8-0.96 s = airtime). OE3XIR node clock is +3599 s
ahead of the logger (+1 h); the other four are within 1 s. DB0AIS: node clock only, never correlated with OE3.

## 0. Headline

1. The relay is a flood with brakes, but the brakes only work at one of five nodes. Overhear-cancel fired 587 times, all on
   OE3XHL (23.9 % of its 2461 relays). The other four never cancel: their relay need is "sole provider" (case A) in 100 % of
   their 8325 NEED lines (IA 1959, IR 1178, OC 2912, WJ 2276; server-inject slots included). The absence of CANCEL there is fully explained by case A; whether
   `--nbrcancel` is on or off on those four **cannot be told from the log** (REFUSE lines exist on all five, written in count and on mode).
2. 29.5 % of the cluster's TX airtime (4006 of 13563 s) is relay traffic sent although a foreign copy of the same frame was heard
   while the relay waited in the queue. Upper bound, not saving: the heard copy may not cover the same listeners. ACK relays 1680 s,
   HEY/POS/text relays 2326 s.
3. 98 % of all ACK relays (8727 of 8913 TX) acknowledge OE3XWJ's own `{ping}` stream (1 per 60 s, 1529 pings to "2025", hop 4,
   prio 2). That test traffic alone is 48.5 % of the cluster's TX count and 25.9 % of its TX airtime. ACK relays have no cancel path at all.
4. Every server text injected by the gateways (GWI, 1277 events: 662 {CET} + 615 other) is radiated by every gateway that gets it:
   231 of 362 frames by 4-5 of the 5 gateways within a median 19 s. 735 s redundant airtime (5.4 % of cluster TX).
5. No loops, no ping-pong, no hop growth. Loop guard (own call in path) fired 7 times, dedup-window re-relays 4 times (22-25 min
   gaps). Real losses are small: 196 of 9748 queued relays (2.0 %) were dropped by supersede/TTL, all HEY/POS; own beacons
   at OE3XOC are starved (91 of 190 own HEY/POS dropped by TTL).

## 1. Inventory of manipulations (what the node does, in plain words)

Each item: mechanism (read at the emit site), count, verdict. Rates per hour in 1.2.

### 1.1 Mechanisms

**M1 Dedup drop** (`RX_DEDUP_DUP`, `DUP:d`; `src/dedup_functions.cpp:27-66`, call in `OnRxDone`). The node keeps the ids of the last 100
frames it accepted in a round-robin list. A frame whose id is in the list is "heard before": it is not displayed, not uploaded, not relayed.
Positive: stops every echo and parallel relay from being re-processed (dup/new ratio 2.3-3.8 per node, i.e. each new frame is heard
3.3-4.8 times). Limit: the list is count-based, not time-based. At 3.8-5.7 accepted ids per minute (ACKs included) it covers only
**17.7-26.4 min** (fact: dedup_adds/min x 100 slots). Own originated frames are not in the list, so their echoes arrive as "new" (DUP:n with OWN:e;
OE3XWJ 3668 of those, from its own pings).

**M2 Relay or not** (`[LOG] RLY ... q=`; `lora_functions.cpp:2460-2679`). For every new text/position/HEY the node decides:
relay (`q=tx`), or drop with a reason: `hop0` (hop budget used up, end of the flood), `gwcap` (gateway only: path already as long as the hop limit,
`:2385-2396`), `gwfilter` (gateway only: `{ping}`/`{pong}`, WLNK-1, APRS2SOTA, `:2166-2174,2373-2408`), `nomesh`, `loop`, `cet` (stale time signal), `full`.
ACK frames take a separate shorter road (M10).

**M3 Hop count -1 and path append** (`:2499`, `:2545-2562`). A relay lowers the hop counter by one and appends its own call to the path;
on HEY it also appends its own signal report (RSSI/SNR/neighbour count, `:2564`). Effect on air: a HEY starts at 31 B (461 ms) and the
median relayed HEY is 70-73 B (805-854 ms), a position 108-117 B (1149-1247 ms). Neutral: the path is what makes loop detection, the neighbour matrix and the
HEY report chain possible; the price is ~1.85x airtime per HEY relay.

**M4 Loop guard** (`RELAY_LOOP_BLOCKED`, `q=loop`; `:2504-2515`): if the node's own call is already in the path, it does not relay again.

**M5 NEED computation** (`[NBR]|NEED`; `:2538-2543`, `nbr_matrix.cpp:1737-1852`). Before relaying, the node asks its neighbour matrix:
"which of the stations I hear (or that reported hearing me) do not yet have this frame (need), and for which of those am I the only known
provider (alone)?" Case **A** = alone is not empty: relay for sure, never cancelled. Case **B** = alone is empty: relay is "second rank" and may be
withdrawn. Case **U** = matrix empty: relay as before (no U in these logs). Knowledge window of the matrix is **12 h**
(`nbr_matrix.h:125`); "dependents" are every row I hear directly plus every row known to hear me (`nbr_matrix.cpp:1777-1781`).

**M6 Case-B hold** (`txring_functions.cpp:353-440`, `configuration_global.h:583-585`). Position/HEY relays in case B wait an extra 20 s
once (up to 60 s) so that neighbours go first; texts are not delayed (they keep the normal priority backoff, so the cancel window for texts is short).

**M7 Overhear-cancel** (`[NBR]|CANCEL`; `lora_functions.cpp:1327-1483`). While a case-B relay waits, the node listens. If a neighbour
transmits the same frame and that neighbour's known listeners (plus the neighbour itself) cover everything still needed, the node
withdraws its own queued relay. Frame by frame it also may add listeners by symmetry assumption (`[NBR]|SYM`, reciprocal link if SNR >= -16 dB).

**M8 Refuse** (`[NBR]|REFUSE`; `:1380`). Same situation, but case A: a neighbour's copy is heard, the node would have to
leave a station without a provider, so it keeps its relay. Logged once per slot. Positive for "nothing lost", negative for duplicates.

**M9 Supersede and TTL drop** (`RING_SUPERSEDE`, `RING_DROP_STALE`; `txring_functions.cpp:850-912,639,1068`). A queued HEY/position of
the same originator is replaced by a newer one (also a newer `{CET}`); an entry waiting past its time-to-live is thrown away (HEY 180 s, position/relay
120/300 s, `{CET}` 150 s). Positive for stale data, negative for delivery of the dropped content.

**M10 ACK forward** (`rx_ack_fwd`; `lora_functions.cpp:653-662`). A new 12-byte ACK with hops left is queued with priority 1 and forwarded,
unless the acknowledged message was heard with the gateway flag (`checkServerRx`, `dedup_functions.cpp:91`) or is the node's own. No NEED, no cancel.

**M11 GW ACK pair** (`rx_dm_ack_gw`/`rx_dm_ack_new`; `:2319-2334`, GWACK-01): by design two copies of one ACK id (IA 12, IR 3, OC 4, WJ 4 pairs).
Matches the 24 relay-TX pairs with the same id 2.5-16 s apart (IA 12, WJ 4 .. checked for IA only: type A, hop 4; inference for the rest).

**M12 Gateway upload** (`[LOG] GWU`; `:2413-2449`): a gateway that hears a new frame not yet carrying the server flag (and every HEY) sends it
to the server before the hop decrement. **M13 Gateway download/inject** (`[LOG] GWI`, `TX src=g`; `gw_udp_ring.h:38-70`): a frame from the server is
queued for radio with its own NEED calculation (`nbrServerNeed`). **M14 {CET}**: time-signal texts from OE1XAR-33 take the same inject path; a stale one
(>150 s) is dropped in the gate (`cet_age.h`, `:2597`); `{CET}<` (wrong time) is never relayed (`:2208`).

**M15 CSMA / CAD** (`CAD_BUSY`, `CSMA_AGED`; `lora_functions.cpp:4030-4045`, `csma_timing.h:71-110`): listen-before-talk with growing backoff; after
20 s/60 s in the queue the hold is shortened (age floor). **M16 Echo table** (`[NBR]|ECHO`; `nbr_matrix.cpp:366`): the node remembers its last 4 own
beacons and notes who repeated them (first/second hand). Diagnostic only. **M17 HN neighbour report** (`[NBR]|RPT`): reports received (IA 22, IR 104, OC 398, WJ 191);
no `RPTTX` seen, so none of the five transmits them (inference: `--nbrreport` off/auto without trigger).

### 1.2 Counts per node (mean per hour; total in brackets; OE3XIR 11.5 h, others 25 h)

| manipulation                         | HL          | IA          | IR         | OC          | WJ         |
| ------------------------------------ | ----------- | ----------- | ---------- | ----------- | ---------- |
| M1 new frames (text/pos/HEY)         | 127 (3170)  | 116 (2880)  | 166 (1902) | 185 (4687)  | 101 (2571) |
| M1 dup, foreign                      | 254 (6350)  | 114 (2846)  | 198 (2275) | 305 (7722)  | 106 (2702) |
| M1 dup, own echo                     | 65 (1619)   | 101 (2528)  | 77 (879)   | 132 (3352)  | 75 (1923)  |
| ACK new / dup                        | 114 / 293   | 103 / 478   | 138 / 655  | 149 / 943   | 170 / 621  |
| CRC errors (`RX_ERROR`)              | 69 (1720)   | 444 (11080) | 418 (4800) | 483 (12239) | 302 (7697) |
| M2 relay queued (`q=tx`)             | 98 (2461)   | 67 (1677)   | 90 (1032)  | 102 (2592)  | 78 (1986)  |
| M2 hop0 / gwcap                      | 8.8 / 18.5  | 5.1 / 15.2  | 9.3 / 15.4 | 6.9 / 12.6  | 7.0 / 11.4 |
| M2 gwfilter (ping etc.)              | 0.5 (12)    | 24 (602)    | 48 (552)   | 56 (1427)   | 3.3 (83)   |
| M4 loop (`q=loop`)                   | 0           | 0           | 0.2 (2)    | 0           | 0.2 (5)    |
| M5 NEED case A                       | 62 (1541)   | 79 (1959)   | 103 (1178) | 115 (2912)  | 89 (2276)  |
| M5 NEED case B                       | 46 (1150)   | 0           | 0          | 0           | 0          |
| M8 REFUSE                            | 15 (386)    | 27 (676)    | 36 (408)   | 43 (1094)   | 17 (423)   |
| M7 CANCEL                            | 23.4 (587)  | 0           | 0          | 0           | 0          |
| M9 supersede                         | 0.8 (21)    | 3.8 (94)    | 1.4 (16)   | 3.7 (93)    | 0 (1)      |
| M9 TTL drop                          | 0.1 (3)     | 3.7 (93)    | 0.7 (8)    | 4.9 (124)   | 0.2 (4)    |
| M10 ACK forwarded                    | 85.5 (2140) | 77 (1924)   | 124 (1421) | 133 (3378)  | 2.0 (50)   |
| TX relay HEY / POS / text            | 50/19.5/4.6 | 44/18/2.1   | 61/22/6.0  | 66/31/2.8   | 55/19/3.4  |
| TX inject (src=g, GWI)               | 9.3 (232)   | 11.3 (282)  | 12.7 (146) | 12.9 (327)  | 11.4 (290) |
| TX own HEY / POS / text              | 4.0/2.0/0   | 6.0/1.5/0   | 0/1.5/0    | 1.6/0.6/0   | 0/1.8/60   |
| M12 GWU uploads                      | 109 (2721)  | 103 (2566)  | 122 (1401) | 148 (3743)  | 77 (1959)  |
| M15 CSMA_AGED / CAD busy (confirmed) | 3 / 64      | 144 / 360   | 212 / 399  | 295 / 332   | 5 / 78     |
| M16 ECHO lines                       | 6.2 (154)   | 11.9 (297)  | 1.9 (22)   | 7.5 (190)   | 2.0 (50)   |

Per-hour series for every row: `results.json` nodes.<node>.per_hour. STAT (5-min windows): channel util mean/max HL 17.9/26, IA 16.4/35, IR 20.8/28,
OC 28.5/37, WJ 19.5/31 %; own TX duty 3.0/2.8/3.8/4.2/3.2 %; TX ring peak 8/15/11/12/9 of 20 (IA 15 in 3 windows); drops prio 1-3 = 0 on all nodes,
prio 4/5 = HL 0/3, IA 15/78, IR 8/0, OC 43/81, WJ 4/0 (HEY drops are the priority system, per brief).
Queue wait of relays: median HL 10.3 s, IA 22.5, IR 18.3, OC 17.2, WJ 9.6 s; ACK relays wait 22-23 s at IA/IR/OC (6.6 s HL). IA needed >= 10 CAD
attempts for 221 of 3999 TX (5.5 %); OC 74 (1.2 %), IR 41 (1.6 %).

### 1.3 Classification (positive / negative / neutral)

| manipulation                       | class    | number / evidence                                                                                                                          | verdict         |
| ---------------------------------- | -------- | ------------------------------------------------------------------------------------------------------------------------------------------ | --------------- |
| M1 dedup drop                      | positive | 2.3-3.8 dup per new frame suppressed; window only 17.7-26.4 min                                                                            | OK              |
| M1 dedup window too short          | negative | 4 relay TX of an id already relayed 22-25 min earlier: IR 1330/1426 s, OC 1515 s, WJ 1332 s; OC accepted 38 foreign ids again (7 > 10 min) | Beobachten      |
| M2 relay (flood)                   | positive | 9748 relays queued: 8965 sent (92.0 %), 587 cancelled (6.0 %, HL), 196 dropped (2.0 %); the only way beacons/texts leave a valley          | OK              |
| M2 hop0 / gwcap                    | positive | 7 % hop0, 12-18 % gwcap: bounds the flood (gateway path cap)                                                                               | OK              |
| M2 gwfilter `{ping}` (IA/IR/OC)    | positive | 602/552/1427 pings not relayed (`lora_functions.cpp:2166`)                                                                                 | OK              |
| M3 path append + HEY report        | neutral  | HEY 31 B -> ~72 B (461 -> 854 ms) per relay chain; HEY relays are 39 % of cluster airtime (5334 s)                                         | Beobachten      |
| M4 loop guard                      | positive | 7 hits (IR 2, WJ 5): WJ 18:39:07.858, 01:02:19.830, 03:43:58.891 = {CET} aged 1649-1727 s coming back with WJ in path                      | OK              |
| M5/M8 sole-provider veto (case A)  | mixed    | 4 of 5 nodes: 100 % case A, 0 cancel; 884 parallel relay pairs (non-ACK) sent although the later node had heard the first                  | Handlungsbedarf |
| M6 case-B hold                     | positive | at HL: cancel median 12.0 s after queueing (p90 37.6 s); 51 % of case-B slots end in a cancel                                              | OK              |
| M7 overhear-cancel (HL)            | positive | 587 cancels = ~537 s HL airtime saved (HEY 457, POS 117, text 13); 20 of 20 cancels naming OE3XIA-12 match an IA TX within 5 s             | OK / Beobachten |
| M7 inferred cover (HL)             | negative | 194 of 587 (33 %) rely on a symmetry assumption; 187 of 1027 relayer->listener pairs have no observed edge, 41 only 1-2 hits               | Beobachten      |
| M9 supersede                       | positive | 120 replaced HEY/POS: newer beacon of the same originator goes out                                                                         | OK              |
| M9 TTL drop                        | negative | 74 relays expired in the queue (IA 37, OC 33); plus own beacons: OC 91, IA 51                                                              | Beobachten      |
| M10 ACK forward                    | negative | 8913 TX, 8727 for WJ pings; 36-74 % of ACK relays at HL/IA/IR/OC sent after hearing a duplicate while queued; no cancel path               | Handlungsbedarf |
| M11 GW ACK pair                    | neutral  | designed repetition (23 pairs)                                                                                                             | OK              |
| M12 GWU upload                     | neutral  | 12390 uploads for 5515 distinct ids (55 % server-side duplicates); no LoRa cost                                                            | OK              |
| M13/M14 server inject, 5x radiated | mixed    | positive: server text reaches 5 valleys; negative: 231 of 362 frames by 4-5 gateways, 735 s redundant airtime; all case A                  | Beobachten      |
| M15 CSMA/CAD                       | positive | 0 collisions provable; negative at IA/IR/OC: waiting 17-27 s median, 221 TX with >= 10 CAD tries at IA                                     | Beobachten      |
| M16 echo table                     | neutral  | OC: 135 of 190 own beacons never echoed (71 %), HL 7 of 154, DB0AIS 8 of 215                                                               | Beobachten      |
| IR: 689 own `{ping}` ring entries  | neutral  | `phone_msg` entries read from the ring (688 `RING_TX_READ`) with `CAD_FREE_NO_TX` 689 and no TX; orchestrator: discarded at decode         | Beobachten      |

## 2. Redundancy (sub-question 2)

**Fact:** per msg_id (window <= 30 min, 9684 clusters), number of the five nodes that transmitted it (own, relay or inject):
1 node 3963 (41 %), 2 nodes 2018 (21 %), 3 nodes 2074 (21 %), 4 nodes 1249 (13 %), 5 nodes 380 (4 %).
By type: ACK 1/2/3/4/5 = 798/1004/1297/547/1; position 920/388/198/62/16; HEY 703/603/549/467/199; text 1542/23/30/173/164
(texts: 1529 single-node ids are WJ's own pings; 164 ids by all five = server injects).

Chain vs parallel: the same id at different hop values is the next hop (chain, by design); the same id at the same hop value from two nodes is a
parallel double. 4938 of 19133 relay/inject TX (25.8 %, 3345 s of 12466 s airtime) are the second or later node of a parallel group.

| type        | TX   | parallel extra TX | airtime of the extras |
| ----------- | ---- | ----------------- | --------------------- |
| ACK         | 8890 | 1912              | 599 s                 |
| HEY         | 6129 | 1466              | 1272 s                |
| position    | 2453 | 520               | 611 s                 |
| text/inject | 1661 | 1040              | 863 s                 |

Link audibility (fact, `linkp.json`; share of A's non-ACK TX that B logged with last hop A within 3 s): IA->HL 0.68, IA->OC 0.49, IA->IR 0.16, IR->WJ 0.76,
IR->OC 0.71, IR->IA 0.28, OC->WJ 0.21, OC->IA 0.16, OC->IR 0.18, WJ->OC 0.33, WJ->IR 0.18. **OE3XHL's transmissions are heard by none of the other four
(p = 0.000-0.002)**; HL hears IA only. The links are one-way and marginal.

Parallel pairs (6792, ACK included) by reason:

| reason                                                        | pairs | share  | avoidable?                  |
| ------------------------------------------------------------- | ----- | ------ | --------------------------- |
| A not audible at B (hidden; almost all involve HL)            | 1931  | 28.4 % | no (explained)              |
| simultaneous (< 1.5 s, neither could know)                    | 715   | 10.5 % | no                          |
| pair audible, but B did not receive A's copy                  | 1672  | 24.6 % | mostly no (link p 0.16-0.5) |
| B heard A while queued, sent anyway: ACK (no cancel logic)    | 1590  | 23.4 % | yes in principle            |
| B heard A while queued, case A, REFUSE logged (sole-provider) | 529   | 7.8 %  | by design (veto)            |
| B heard A while queued, case A, no REFUSE line (counted once) | 343   | 5.0 %  | by design (veto)            |
| B heard A while queued, case B, cover insufficient            | 12    | 0.2 %  | by design                   |

Which pairs double up most (ordered "first -> later", parallel): WJ->OC 669, HL->OC 649, IR->OC 611, IA->OC 610, OC->IA 594, OC->IR 494, HL->IA 409,
WJ->IA 376, WJ->IR 325, HL->IR 304. Unordered over all hops (v1): HL+OC 2819, IA+OC 2815, HL+IA 2276, IR+OC 2144, OC+WJ 1560.
All HL pairs and WJ->IA are hidden or simultaneous. The pairs among IA/IR/OC/WJ are the avoidable ones: 36 % of all parallel pairs
(2474) were "B heard a copy while queued" - 1590 of them ACKs, 884 text/pos/HEY under the case-A veto.

**Verdict:** double relays are mostly explained by hidden/one-way links (HL) and marginal links; the avoidable remainder is the ACK flood and the case-A veto.

## 3. Overhear-cancel effectiveness (sub-question 3)

- **Fact:** CANCEL 587 (OE3XHL only), CANCEL? 0 everywhere, REFUSE 386 at HL. Relayers named: OE3GJC-12 292, OE3MAG-10 217, OE3XIA-12 20, OE3RDB-2 16,
  OE3HUS-01 14, OE3HES-1 11, OE3ZKW-12 10, OE1XAR-12 7. Types: HEY 457, POS 117, text 13. About 23 per hour.
  Of 2461 relay slots at HL: 1852 sent, 587 cancelled, 19 superseded, 1 stale, 2 unaccounted (fate by msg_id; matches orchestrator's 589/589).
- **Fact:** case split at HL (relay NEEDs only, GWI excluded): A 1309, B 1150 of 2459 (46.8 %); 587 cancels = 51 % of B slots. Decision flips
  between consecutive relays in 31 % of cases; B share per hour ranges 4 % ... 100 %. No sign of two nodes oscillating against each other (HL's TX is inaudible to the others).
- **Why 0 elsewhere (fact):** case A on every NEED line. The sole-provider set is large: median alone-set size IA ~20, OC ~7, WJ ~9, IR ~8 rows; HL 0-1.
  Four of the five nodes claim the same stations as "only I serve them": OE3MIF-12 and OE3NRA-1 are in the alone set of IA, IR, OC and WJ in 95-100 % of
  their NEEDs, while each of the other nodes has them as a direct reception (>= 3 `[NBR]|ME` lines). Code: a neighbour counts as alternative provider only
  with an observed edge "station heard that neighbour" (`nbr_matrix.cpp:1816-1852`); who hears whom is learned from paths and HEY reports only, so one-way
  receptions never produce that edge. **Inference:** the veto is an observation artefact (no exchange of neighbour reports: no RPTTX seen), not four
  independent sole-provider situations. Not a code bug; a parametrisation/design consequence of 12-h window + "everything I hear is a dependent".
- **Did a cancelled frame fail to reach a region?** Cannot be shown with these logs; HL's listeners are not logging. Indicators: 65 % of cancels (379) have
  every removed listener supported by an observed edge with >= 3 hits and nothing inferred; 33 % involve symmetry-inferred cover; 41 relayer->listener pairs have 1-2 hits.
  Median edge count 22, median SNR -8 dB. Only 206 of 1027 removed listeners appear later in a path of the same frame (absence of proof, not loss). Verdict: Beobachten.
- **Known gap (memory note) - does it show?** Yes, in two places. (a) ACK relays are queued as kind OTHER with no NEED (`lora_functions.cpp:661`), so the cancel scan
  (`:1358-1361` requires RING_KIND_RELAY) never sees them: 766/1137/947/2494 of 2140/1924/1421/3378 ACK relays at HL/IA/IR/OC were sent after a duplicate had been heard while queued.
  (b) case A blocks the scan by design (`:1366-1386`). Case U (empty matrix, also kind OTHER) did not occur.

## 4. Losses (sub-question 4)

- **Fact:** relay fate of 9748 queued relays: sent 8965 (92.0 %), cancelled 587 (6.0 %, HL only), dropped 196 (2.0 %). Per node (sent/cancel/supersede/TTL/unaccounted):
  HL 1852/587/19/1/2, IA 1604/0/36/37/0, IR 1013/0/16/3/0, OC 2511/0/48/33/0, WJ 1985/0/1/0/0. Dropped without a neighbour copy: supersede 120 + TTL 74 + unaccounted 2 = 196 (2.0 %), all HEY/POS except 3.
  `RLY q=full`: 0; `RING_DROP_PRIO/NEW`: 0; text/ACK/relay drops (STAT prio 1-3): 0.
- **Fact (delivery test):** for relays whose NEED mask contained another logging node with a usable link (p >= 0.15), did that node log the frame?
  relay sent: 1917 of 2722 (70.4 %); relay superseded: 13 of 47 (27.7 %); relay TTL-dropped: 8 of 29 (27.6 %). So the dropped relays cost about
  55 deliveries to cluster neighbours out of ~2800 needed (~2 %), all of type HEY/POS. Lower bound view (only the 5 logging nodes visible).
- **Starved own beacons (fact, `ownfate.json`):** OC sent 41 of 141 own HEY (43 superseded, 57 TTL) and 15 of 49 own position beacons (34 TTL);
  IA 149/246 HEY (57 superseded, 40 TTL), 38/49 POS (11 TTL); IR 17/22 POS; HL, WJ, DB0AIS (168/168 + 50/50) fine. Cause **inference**: priority-1 ACK relays
  (3378 at OC) and priority-3 relays precede priority 4/5 beacons in a queue where median relay wait is 17-27 s. Own beacons expire after 120/180 s.
- Frames never relayed by design: hop0 (7 %), gwcap (12-18 %), gwfilter (ping etc.). Not counted as loss.
- IR 689 own pings discarded (see 1.3), 2 frames "unaccounted" at HL (log gap), 0 elsewhere.
- Unprovable: frames heard by exactly one node and never relayed whose NEED rows were not logging nodes.

## 5. Feedback and build-up (sub-question 5)

| check                                   | result (fact)                                                                                                                                                                                                                          | verdict    |
| --------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- |
| same id accepted as new twice (foreign) | HL 1, IA 4, IR 3, OC 38 (gaps median 35 s, 7 > 10 min), WJ 7 within 4 h; own-origin echoes excluded (they are "new" by construction)                                                                                                   | OK         |
| same id relayed twice by one node       | 28 (IA 12, WJ 6, IR 5, OC 5, HL 0): 24 within 2.5-16 s (GW ACK pair, type A hop 4 verified at IA); 4 after 1330-1515 s (IR 2, OC 1, WJ 1; dedup window 17.7-26.4 min). Orchestrator's Q2 list counts 8 re-relay occurrences by RX view | OK         |
| hop counter growth / wrap (H >= 13)     | 24 receptions (HL 17, OC 3, WJ 2, IR 1), texts, last hop OE3HUS-01 (FW 40:A) x16, OE3KJO-62, OE3MIF-12: a foreign build relaying at H0. Our nodes: gwfilter/gwcap, 0 relay TX                                                          | OK         |
| path length                             | max 8 tokens (HL 1), 6-7 tokens < 100 frames per node                                                                                                                                                                                  | OK         |
| ping-pong A<->B                         | none: HL's TX is not heard by the others; for IA/IR/OC/WJ no id TX'd twice at the same hop                                                                                                                                             | OK         |
| oscillating relay decisions             | HL case A/B flips between consecutive relays in 31 % (different originators, 12-h matrix churn); no per-frame flapping seen                                                                                                            | Beobachten |
| ring build-up                           | `ringmax` >= 15 of 20 in 3 windows (IA); 0 `RING_DROP_NEW`; waits 17-27 s median                                                                                                                                                       | Beobachten |
| stale {CET} circulating                 | 14 LoRa-received {CET} older than 150 s at WJ (1565-2601 s), IA 1, IR 2, OC 2; relay-dropped 8x (`[CET];drop;where;relay`) at OC/WJ; DB0AIS 4x. Source is outside the five                                                             | Beobachten |

**Inference:** a frame of age 27-48 min re-entering the mesh points to a node outside the logs that holds a frame in a queue without TTL and radiates it late
(not one of these builds: TXP gate drops at 150 s). Open question for the operator.

## 6. Gateway effect (sub-question 6)

- **Fact:** 1277 GWI events (HL 232, IA 282, IR 146, OC 327, WJ 290); 662 from OE1XAR-33 (hop 2, {CET}), 615 other origins (hop 4). All 1277 were
  radiated (100 % at each node, no cancel on any inject slot, `NEED` always case A for inject).
- 362 distinct frames: radiated by 1 node 64, 2 nodes 32, 3 nodes 35, 4 nodes 111, 5 nodes 120. {CET}: 151 ids, 143 (95 %) by 4-5 gateways.
  Spread first to last radiator median 19 s, p90 57 s. Airtime: 1037 s radiated vs 302 s if one gateway each: 735 s redundant = 5.4 % of cluster TX.
- Later radiators (915): 679 (74 %) had heard a copy of the same frame while their slot was queued and sent anyway (case A veto).
  Since HL is inaudible to the others and IA/IR/OC/WJ partly hear each other (p 0.16-0.76), part of this is real coverage (different valleys) and part is redundant.
- {CET} per node: TX 148/148/68/151/151 (HL/IA/IR/OC/WJ), TX age median 8-21 s, > 60 s: IA 11, IR 5, OC 9, max 147 s (IR) - the 150 s gate holds.
- Uploads: 12390 GWU for 5515 distinct ids: seen by 1 gateway 2730, 2 -> 1096, 3 -> 632, 4 -> 615, 5 -> 442. Internet-side duplicates only.
- **Verdict:** Beobachten. Positive for coverage, but the five gateways do not coordinate server frames. The designed mechanism (`gw_has_frame`, `nbrServerNeed`) is
  neutralised by case A, same cause as section 3.

## 7. Suspected firmware/design defects (with code sites read)

1. **Sole-provider veto unreachable-to-clear (design/parametrisation, Handlungsbedarf).** `nbr_matrix.cpp:1777-1781,1816-1852`: dependents = all directly heard
   rows + rows known to hear me; alternative provider needs an observed reverse edge; window 12 h (`nbr_matrix.h:125`). Result: case A on 100 % of NEEDs at four of five
   nodes, cancel never possible. Not a bug in the sense of wrong code; a policy that disables the feature in dense gateway clusters.
2. **ACK relays have no overhear-cancel** (`lora_functions.cpp:653-662` ring kind OTHER; cancel scan `:1358-1361` RELAY-only). 8913 TX; at HL/IA/IR/OC 36/60/67/74 % were sent after a duplicate had been heard while queued.
3. **Dedup is count-based, 100 slots** (`dedup_functions.cpp:18-66`): 17.7-26.4 min window at this load, matching the 22-25 min re-relays. Known corridor from the memory note is 40-48 min.
4. **Server inject has no suppression effect** (`gw_udp_ring.h:38-70` + case A): 100 % radiation, 5x at the cluster.
5. **Own-beacon starvation** (priority order ACK 1 > relay 3 > position 4 > HEY 5, TTL 120/180 s, `configuration_global.h:465-480`): OC loses 91 of 190 own beacons.
6. **IR `phone_msg` entries consume ring/CAD cycles without TX** (689 `CAD_FREE_NO_TX`): minor, per orchestrator the frames are discarded at decode.
7. Not a bug: `NEED` lines also exist for server-inject slots (232-327 per node); do not count them as relays (relay NEED = total - GWI).

## 8. DB0AIS-12 (standalone, 2026-10-30 17:01 -> 10-31 18:09, 25 h; node clock only)

Fact: util mean 8.7 %, max 15 %, TX duty 2.8 %, dup/new 0.65 (OE3: 2.3-3.8). Relays queued 2196 (HEY 1416, text 461, POS 315), all case A (NEED 2551 incl. 356 GWI),
REFUSE 263, CANCEL 0, supersede 4, TTL 0. ACK relays 65 only (126 new ACKs: 58 hop0, 3 server-flag blocked). Own beacons 168 HEY + 50 POS all sent. Echo: 207 of 215 own beacons
echoed. gwcap 612 (POS), hop0 264. CET TX 346 (age median 11 s, 15 > 60 s, max 117 s). Re-relay of 37 ids at a constant 2.9 h gap = server {CET} ids (6ADB5B..),
**inference** id reuse by the sender, not a loop (dedup window here ~112 min at 0.89 ids/min). Nothing to correlate with the OE3 cluster.

## 9. Caveats and open questions

- Cancel mode on the four silent nodes is unknown (log cannot separate `on` from `count`); case A explains the silence either way.
- Audibility p is a lower bound (B may be transmitting or in CAD when A sends). "Heard while queued" is any foreign copy, not necessarily one covering the same listeners; the 29.5 % is an upper bound.
- "Neighbour got frame" uses only the five logging nodes and p >= 0.15 links; cancelled HL frames cannot be checked at all.
- ACK flood attribution relies on id matching of acked ids to WJ ping ids within 30 min; 98 % match. Who produces 2.8 ACKs per ping is not visible in these logs (inference: other gateways without the `{ping}` filter).
- Dedup-window estimate = 100 slots / accepted ids per minute from `RX_DEDUP_ADD`.
- Build unknown (inferred): REFUSE/NEED/SYM lines present -> NBR stage 2 build on all five; TXP gate evidenced (RING_DROP_STALE reason=ttl/cet, `[CET];drop`).
