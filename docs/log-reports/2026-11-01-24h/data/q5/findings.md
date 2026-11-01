# Q5 - The {CET} time signal (24-h logs 2026-10-31/11-01)

Scope: OE3 cluster (oe3xhl, oe3xia, oe3xir, oe3xoc, oe3xwj; logger clock = Mac, CET = UTC+1) and DB0AIS-12 (separate mesh/day, node-clock time base, 1-s resolution).
Numbers: `results.json`; scripts: `scripts/` (order: extract_cet.py per node, clock_offsets.py, clock_sawtooth.py, db_clock.py, analyze_oe3.py, oe3_extra.py, analyze_db.py, db_phase.py, rawpass.py, build_results.py).
Conventions: ages = reception/TX time (UTC) minus content time. "Radiation" = distinct (msg_id, via path) seen in any of our five logs (lower bound; unheard radiations are invisible). Airtime model SF11/BW250/CR4-6: 61 B = 724 ms measured (683-783 ms, 662 TX), 70/71 B = 768-782 ms.

## BLUF

- The server beacon is regular and correct: OE1XAR-33 every 607 s (p50; min 606, p90 608), content is UTC (label "CET" is a misnomer), 151 of 151 telegrams reached the OE3 cluster, no wrong-time `{CET}<`, no CEST/CET slip after the DST change. OK.
- All five OE3 nodes are gateways (UDP push), so every telegram is radiated by all five of ours plus a median 8 foreign gateways (13 gateways total, range 9-17) plus about 9 relay paths: about 20 radiations, about 15 s airtime per telegram (lower bound). 65 % of our own gateway TX follow an already overheard gateway radiation and are never cancelled (sole-provider veto, case A in 100 % of NEED lines). Beobachten.
- Our nodes emit no stale time: 0 of 666 own TX older than 150 s (max 148.8 s at oe3xir, a 2-s margin). The gate fired only at the relay decision (8 OE3 + 4 DB0AIS, ages 302-2601 s); 0x `RING_DROP_STALE reason=cet`, 0x gateway-injection drop. The stale time on air comes from four foreign relays. Handlungsbedarf outside our firmware.
- OE1XAR-62 and -63 are never heard in OE3; there the second SSID causes no extra airtime. In DB0AIS (DL) three SSIDs (-33, -62, -63) triple the load: 11 % of own TX airtime, 12 % of decoded RX airtime. Beobachten.
- Clocks: OE3XIR runs UTC+2 (node clock +3599 s vs logger), DB0AIS ran UTC+2 until the operator stepped it to +1 h (capture line 18015, 19:20:26 -> 18:20:31 node time, about 17:20 UTC 10-30). Both still compute the CET age correctly (UTC base). Display/LOG time wrong only. Beobachten.

## 1. Cadence and missing telegrams

| Fact                              | Value                                                                                                                                 | Evidence                                  | Verdict                                  |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------- | ---------------------------------------- |
| OE3 sources heard                 | only OE1XAR-33 (151 ids); 0x OE1XAR-62, 0x -63 in any of the five logs (UDP and RF)                                                   | grep `OE1XAR-62` = 0 in all 5             | neutral                                  |
| Content gap, OE1XAR-33            | n=150, p50 607, p90 608, min 606, max 674 s; 600 s + 6-8 s per cycle                                                                  | results.json network.content_gap_s        | OK                                       |
| The 674-s gap                     | server restart between 06:21:39 and 06:32:53 UTC 10-31; msg_id counter jumps 6ADB5B6C -> 6AE58936                                     | analyze_oe3 id_step                       | neutral                                  |
| id steps 2/3                      | at 09:24-09:45 and 15:49-16:29 UTC; content spacing stays 607 s, so the counter is shared with other server messages, not beacon loss |                                           | neutral                                  |
| Telegrams reaching the cluster    | 151 of 151 expected (span/median gap = 151); 0 absent from all five nodes                                                             |                                           | OK                                       |
| Arrival cadence per gateway (UDP) | p50 607.0, p90 607.4 s                                                                                                                | per node udp_arrival_gap_s                | OK                                       |
| UDP misses per node               | oe3xhl 2 (6AE5897E 17:20:27 UTC, 6AE5898D 19:52:14), oe3xwj 2 (6AE5899D 22:34:10, 6AE589C5 05:18:25); oe3xia, oe3xir, oe3xoc 0        | gaps 1215/1214 s (xhl), 1213/1212 s (xwj) | Beobachten (1.4 % UDP loss on two nodes) |
| Effect of the miss                | all 4 heard fresh on RF (age 2-9 s, `[CET];rx` hop 2 path 2) and relayed (`RLY q=tx`); no hole in the time signal                     | oe3xhl 18:20:29/20:52:24 CET, oe3xwj      | OK                                       |

DB0AIS (own mesh, 10-30 16:02 -> 10-31 17:01 UTC):

| Source    | Distinct ids | Cadence p50                    | UDP                   | RF                  |
| --------- | ------------ | ------------------------------ | --------------------- | ------------------- |
| OE1XAR-62 | 149          | 602 s (max 1204 s once)        | 145 injected (97.3 %) | 141 ids, 313 frames |
| OE1XAR-33 | 141          | 607 s (8 gaps of about 1214 s) | 0                     | 141 ids, 251 frames |
| OE1XAR-63 | 142          | 606 s (7 gaps of about 1213 s) | 0                     | 142 ids, 297 frames |

- Fact: -62 UDP gaps of 1204 s (21:15 UTC, coincides with the only `[ETH];event;link;down` at ms 18826775), 1806 s (23:55) and 1807 s (00:55); 4 -62 telegrams missed on UDP, 3 recovered on RF, 1 content gap of 1204 s (00:05 UTC) in no feed at all.
- Fact: -33 and -63 msg_ids collide numerically (same counter family, e.g. 6ADB5B1A is both); per-source key (source, id) is mandatory. Anyone deduplicating on msg_id alone across SSIDs will merge different telegrams.
- Fact: the three SSIDs drift against each other (602/607/606 s), so their phase rotates; merged gap p50 138 s, p90 474 s, max 601 s (432 telegrams). The D1 assumption "one beacon about every 5 min" only holds on average.

## 2. Path, copies, double transmission

Path (fact): every OE3 node receives the beacon first via UDP (gateway), 98.6-100 % of telegrams. RF only ever yields duplicates (DUP:d 94-99 %). `RX-UDP` display lines are absent on oe3xhl/oe3xir; their UDP receipt is shown by `[GW];rx;type;CET` / `[CET];gw_rx` (146 / 70) - no loss of information.

RF copies heard per telegram (every RF copy has a distinct via path):

| Node   | mean | p50 | p90 | max | vias 1 / 2 / 3+ (share of copies) |
| ------ | ---- | --- | --- | --- | --------------------------------- |
| oe3xhl | 4.6  | 5   | 6   | 8   | 50 % / 48 % / 2 %                 |
| oe3xia | 5.5  | 6   | 8   | 13  | 48 % / 48 % / 5 %                 |
| oe3xir | 4.35 | 4   | 8   | 9   | 63 % / 36 % / 1 %                 |
| oe3xoc | 6.5  | 7   | 9   | 14  | 50 % / 42 % / 8 %                 |
| oe3xwj | 4.0  | 4   | 6   | 8   | 56 % / 41 % / 3 %                 |

H field: gateway radiation H02 (1 via), first relay H01, second relay H00 (not relayed further). DB0AIS: -33 2 copies p50 (max 4), -62 2 (max 4), -63 similar.

Cluster funnel (window with all five logging, 67 telegrams, 05:51-17:12 UTC; lower bound):

| Stage per telegram                | min | p50 | p90 | max            |
| --------------------------------- | --- | --- | --- | -------------- |
| our gateways radiating            | 5   | 5   | 5   | 5 (335 of 335) |
| foreign gateways radiating        | 4   | 8   | 10  | 12             |
| gateways total (33 distinct seen) | 9   | 13  | 15  | 17             |
| relay radiations                  | 3   | 9   | 13  | 15             |
| radiations total                  | 15  | 20  | 24  | 28             |

- Fact: one telegram -> 5 own + about 8 foreign gateway radiations -> about 9 relay radiations; 1376 distinct radiations in 67 telegrams; 15.4 s airtime per telegram; 90.6 s/h = 2.5 % of one hour (as seen from five sites; sum of all heard radiations).
- Fact: our five spread over time: first own radiation p50 4.8 s after the UDP push (max 9.5 s); first-to-last p50 25 s, p90 81 s. 30 adjacent pairs (of 67 telegrams) started within 0.75 s of each other (airtime overlap), 119 within 3 s. Whether those collide depends on mutual reception: share of a node's gateway radiations heard by another of ours is 0-73 % (oe3xhl heard by none of the four, oe3xir -> oe3xoc 73 %).
- Fact: 436 of 666 own gateway TX (65.5 %) were issued after the node had already heard another gateway's radiation of the same id; per node xhl 46.6 %, xia 66.9 %, xir 76.5 %, xoc 86.8 %, xwj 56.3 % (lead p50 6-19 s). The in-between `[NBR]|REFUSE` appears 386/676/408/1098/423 times (all ids), `[NBR]|NEED` for every server slot is case A (666 of 666 OE3, 349 of 349 DB0AIS), `[NBR]|CANCEL` for {CET} ids: 0 (xhl has 589 CANCEL lines in total, none for {CET}). Documented defect class: GWRX-01 (BACKLOG, `docs/rca-gwrx01-20261023.md`); DB0AIS shows the same 23 % (33 of 145 `src=g`, RCA reports 21 %).
- Inference: build contains the GWRX-01 server-slot NEED emission (a `[NBR]|NEED|...|T|A|mask|mask|slot|inferred` line follows every `RING_WRITE src=udp_rx`, `gw_udp_ring.h` lines 48-66), i.e. at least 257e5e2b; STAT `fw=40a/20260929` is no build discriminator.
- Value of our radiations (observational, biased toward our own visibility): 63 % of our gateway radiations were followed by a relay of exactly that path (foreign gateways 21 %); per node xhl 88 %, xwj 78 %, xia 61 %, xir 52 %, xoc 36 %. So the veto is not baseless for xhl/xwj; for xoc more than 60 % of TX produce no observed follower.
- Verdict: double transmission is real (up to 65 % of own gateway TX redundant with a heard radiation, 13 gateways per telegram), cost small in absolute terms (see airtime). Beobachten; fix scope = decide whether case A for a server slot is correct (suspected bug below).

Airtime (fact):

| Node   | CET RX s/h (computed) | CET TX s/h | CET share of decoded RX airtime | CET share of own TX airtime | CET share of TX events |
| ------ | --------------------- | ---------- | ------------------------------- | --------------------------- | ---------------------- |
| oe3xhl | 20.5                  | 4.3        | 3.9 %                           | 4.2 %                       | 3.4 %                  |
| oe3xia | 23.6                  | 4.3        | 5.0 %                           | 4.5 %                       | 3.7 %                  |
| oe3xir | 19.6                  | 4.3        | 3.3 %                           | 3.3 %                       | 2.6 %                  |
| oe3xoc | 29.3                  | 4.3        | 3.5 %                           | 3.0 %                       | 2.4 %                  |
| oe3xwj | 17.4                  | 4.3        | 3.1 %                           | 3.8 %                       | 3.9 %                  |
| db0ais | 26.9                  | 10.7       | 12.0 %                          | 11.0 %                      | 12.3 %                 |

Per node CET occupies 0.6-0.9 % of the hour (RX+TX) in OE3. Total busy time is 15-25 % of the hour (decoded RX airtime 475-529 s/h on xia/xhl; STAT rx matches decoded airtime within 5 %, so STAT util is not strongly inflated here); ACK frames are the largest class (xia 34 % of decoded RX airtime). Verdict: CET is a minor but permanent 3-5 % tax in OE3, 11-12 % at DB0AIS. Beobachten.

## 3. Age of the time on air and the 150-s gate

| Fact                                       | Value                                                                                                                                                              | Evidence                                               |
| ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------ |
| Age at UDP injection                       | logger-based p50 0.7 s, p90 1.04 s, max 4.06 s; node-based -1..3 s                                                                                                 | results network.udp_age_s_logger_based, gw_rx_age_dist |
| Server-side delay                          | 5 telegrams (3.3 %) arrived 2.2-3.9 s after their stamp at all five gateways simultaneously (e.g. 14:38:32 UTC 3.9 s)                                              | oe3_extra scan                                         |
| Age at own TX (logger)                     | p50 9-22 s, p90 13-55 s, max 32 s (xwj) / 122 s (xia) / 130 s (xoc) / 148.8 s (xir) / 25 s (xhl)                                                                   | per node tx_age_s_logger_based                         |
| Own TX > 100 s / > 150 s                   | xia 4, xir 1, xoc 1 / 0 of 666                                                                                                                                     | tx_age_gt100_n                                         |
| Queue wait of own CET (p50 / p90 / max, s) | xhl 8.1/11.7/24; xia 11.2/49.5/122; xir 20.8/40/148; xoc 13.1/54.7/129; xwj 9.1/12.9/25                                                                            | txmeta wait                                            |
| Node-based vs logger age                   | node age is 1.1 s (0.2-2.1) smaller than logger age, constant                                                                                                      | tx_age_node_based_join                                 |
| Gate drops                                 | `RING_DROP_STALE reason=cet` 0 in all six logs; `[CET];drop;where;gw` 0; `where;relay`: oe3xoc 2 (ages 2011, 2508 s), oe3xwj 6 (1565-2601 s), DB0AIS 4 (302-832 s) | `[CET];drop` lines                                     |
| Gate trusted flag                          | `trusted;1` on every `[CET]` line (OE3 and 783/783 DB0AIS); NTP ok 549, timeout 14 over 5 nodes (xhl 7, xwj 4, xir 1, DB0AIS 2)                                    | rawpass                                                |
| DB0AIS own TX age                          | p50 12 s, p90 36 s, max 117 s; queue wait max 14 s                                                                                                                 | `[CET];tx`                                             |

Verdict own nodes: OK. Nothing stale left our radios. The gate never had to cut at TX because the ring wait stays under 150 s, but the margin is thin on busy nodes (oe3xir 147.9 s wait; oe3xia/xoc p90 50-55 s with CAD attempts up to 20). Prio-3 CET waits behind prio-1 ACK relays (1421-3378 ACK TX per node per day vs 68-151 CET TX).

Age on air, union of the five logs (1376 distinct radiations):

| Class            | n    | p50  | p90  | > 150 s                                                                       |
| ---------------- | ---- | ---- | ---- | ----------------------------------------------------------------------------- |
| our gateways     | 335  | 12.1 | 37.8 | 0 (0 %)                                                                       |
| foreign gateways | 436  | 11.8 | 62.7 | 13 (3.0 %)                                                                    |
| relays           | 605  | 26.9 | 307  | 94 (15.5 %)                                                                   |
| all              | 1376 | 18.4 | 88.5 | 107 (7.8 %); > 300 s 4.8 %, > 600 s 1.6 %, > 1200 s 12 radiations, max 2339 s |

- Fact: 54 of 67 telegrams had at least one radiation older than 150 s. Per node, share of received copies older than 150 s: xhl 0.9 %, xia 5.9 %, xir 12.0 %, xoc 18.2 %, xwj 18.4 %.
- Fact: stale relays are systematic, not random (share of that call's radiations older than 150 s): OE3MTC-10 19/19 (p50 691 s, up to 2012 s), OE3NRA-1 21/21 (p50 327 s), OE3MIF-12 24/25 (p50 328 s), OE3XDC-12 18/21 (p50 389 s), OE3XPA-12 9/56. These four deliver 76.6 % of the stale radiations. The age distribution is tight (p50-p90 327-400 s), so a fixed 5-6 min hold, not random loss.
- Inference: those relays run firmware without the {CET} gate and with a CET queue wait of minutes (consistent with prio-3 relay behind ACK storms). Over the whole day: 250 distinct stale (id, path) pairs in 125 ids. Also long-tail 20-48 min copies (OE3MTC-10 12, OE3XWJ-94 3, OE3KJO-64/62, OE3BCB-10, OE3XHU-22): they arrive as DUP:n because the dedup ring has forgotten the id (xwj: 14 of 16 `[CET];rx` lines aged 1371-2601 s). They were stopped by the new gate: `RLY q=cet` 8x, `q=loop` 4x (own call in path), `q=hop0` 7x; none relayed.
- Verdict: gate works (positive, 12 relay-gate stops, 0 stale own TX). Handlungsbedarf: identify/upgrade OE3MIF-12, OE3NRA-1, OE3MTC-10, OE3XDC-12 (operator action, not firmware here). Without the gate those copies would have been re-flooded.

## 4. Redundancy of the two (three) server SSIDs

- OE3: not applicable. Only -33 exists on air in OE3 (0 hits for -62/-63 in 5 logs including RF), so no doubled airtime from two SSIDs there. Positive/neutral.
- DB0AIS: -62 arrives by UDP only at DB0AIS (145x); -33 and -63 arrive only by RF via other gateways (first via DB0HRF-12 207x, DK4RS-20, DM0MTK-12, DL0BOD-20). 131 of 151 ten-minute slots carry all three SSIDs. DB0AIS airs 346 CET in 25 h (13.8/h): -62 145 (own gateway, 5.96/h) + relays of -33 126 (89 % of its telegrams) + -63 71 (50 %) + -62 4. That is 10.7 s/h TX airtime (11.0 % of its TX airtime, 12.3 % of its TX events) and 26.9 s/h RX (12.0 % of decoded RX airtime).
- Fact: a node near DB0AIS hears three telegrams per 10 min that all say the same time (UTC, within seconds). Nodes without UDP get ample redundancy; the cost is roughly 3x the OE3 load.
- Verdict: Beobachten. A single SSID per region (or a gate that drops a beacon while a newer one is already queued, D3 only compares equal frames) would cut it; no data in the logs shows a node that depended on the second/third SSID (each is individually regular; -62 missed 4 times and the other two covered).

## 5. Clock accuracy

| Fact                                        | Value                                                                                                                                                                                               | Evidence              |
| ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- |
| Content time base                           | UTC (logger CET - 3600 s matches content to 0.06-4.06 s, p50 0.7 s). Label "CET" is wrong; firmware treats it as UTC (`cet_age.h:63`, `loop_functions.cpp:2814` comment, field evidence 2026-09-26) | clock vs content      |
| Wrong-time `{CET}<`                         | 0 in 6 logs                                                                                                                                                                                         | grep                  |
| After DST change (10-25)                    | no slip: content stays UTC, Mac CET = UTC+1, content = logger - 3600 s throughout                                                                                                                   |                       |
| Server jitter                               | stamp second-truncated; arrival p50 0.66 s, p90 1.0 s; outliers 2.2-3.9 s on all five gateways at once (server-side)                                                                                |                       |
| Cross-gateway push skew                     | UDP arrival spread across the 5 gateways p50 75 ms, max 496 ms                                                                                                                                      |                       |
| NTP vs logger                               | `[NTP];ok;epoch` minus logger median -0.6 s (epoch integer-truncated), min -1.1, max -0.1; rtt p50 105 ms                                                                                           | clock_offsets.json    |
| Node [LOG] clock vs logger, constant offset | logger - node 0.12-2.2 s (median 1.2 s; includes 0..1 s truncation, so about 0.7 s node lag); no sawtooth over the 15-min NTP cycle (0-3 min 1.18 s, 12-15 min 1.21 s on xhl)                       | clock_sawtooth.json   |
| oe3xir                                      | node [LOG] clock = logger + 3599 s (UTC+2, constant over the whole log); gate ages identical to peers (gw_rx -1..3 s; logger minus node age 1.1 s), so UTC base correct                             | clock_offsets.json    |
| oe3xhl/xia/xoc/xwj                          | node clock = logger -1 s (UTC+1)                                                                                                                                                                    |                       |
| DB0AIS                                      | node clock UTC+2 from capture start until the 19:20:26 node-clock line (file line 18015, preceded by `[SETST];save`), then UTC+1; -62 UDP age 0 s (n=145) confirms content = UTC                    | db_clock.json         |
| UTC offset semantics                        | `cetNowUtc()` subtracts `node_utcoff`, so a fixed wrong `utcoff` (xir, DB0AIS before the step) changes only displayed local time                                                                    | `cet_age.h` cetNowUtc |

Verdict: server time OK; node time bases OK for the gate. Beobachten: OE3XIR still on fixed UTC+2 (set `--tz` rule or `utcoff 1`), affects LOG times, mheard and any local-time feature; operator action. Inference: xir/DB0AIS carry a fixed utcoff of 2 from summer (no TZ rule).

## 6. Our time-signal radiators and their cost

Gateways (fact): all five OE3 nodes (UDP push) and DB0AIS for -62. Only 2 relay TX each at oe3xhl and oe3xwj (the missed-UDP telegrams); everything else is `src=g`, prio 3, status DONE.

| Cost                                              | oe3xhl     | oe3xia      | oe3xir      | oe3xoc      | oe3xwj     | db0ais |
| ------------------------------------------------- | ---------- | ----------- | ----------- | ----------- | ---------- | ------ |
| CET TX per day                                    | 148        | 148         | 68 (11.4 h) | 151         | 151        | 346    |
| CET queue slot-seconds per hour (avg slots of 20) | 46 (0.013) | 120 (0.033) | 158 (0.044) | 139 (0.039) | 54 (0.015) | -      |
| ring depth at CET enqueue p90 / max               | 2 / 5      | 5 / 14      | 4 / 9       | 5 / 11      | 1 / 4      | -      |
| ringmax p50 / p90 / max (STAT)                    | 3/5/8      | 5/9/15      | 5/8/11      | 5/9/12      | 2/4/9      | 2/2/4  |
| STAT util median                                  | 18 %       | 18 %        | 21 %        | 29 %        | 20 %       | 9 %    |
| prio 4 + 5 drops by TTL over run                  | 3          | 93          | 8           | 124         | 4          | 0      |

- Fact: CET occupies on average 0.013-0.044 of 20 ring slots; the ring never overflowed (STAT drop counters prio 1-3: 0 on all nodes; ringmax peak 15 of 20).
- Fact: CET (prio 3) only outranks position (4) and HEY (5). On the busy nodes xia/xoc those TTL-drop (prio 4: 15/43, prio 5: 78/81) with p90 waits of 60-116 s. CET is 3-4.5 % of their TX airtime; the TTL drops are mostly the ACK-relay load (prio 1: 1924/3378 TX), not CET. No per-event causation shown; not a CET problem. Neutral.
- Latency of other traffic: CET adds about 4.3 s TX per hour (6 x 0.72 s) per node. Verdict OK; the measurable cost is RX (every node hears 4-6.5 copies per telegram, 17-29 s/h).

## Suspected firmware issues (candidates, no code change made)

1. Server slots never cancel: case A in 100 % of `[NBR]|NEED` lines for `udp_rx` slots (OE3 666, DB0AIS 349 incl. relays), 0 `CANCEL` for {CET}, 65 % of own gateway TX after an overheard gateway radiation. Sites: `src/gw_udp_ring.h` (gwUdpRxEnqueue, NEED line), `src/nbr_matrix.h:795-800` (nbrServerNeed contract), `src/lora_functions.cpp:1380` (REFUSE for non-empty `alone`). Question for the NBR owners: is `alone` correct for the server slot (need == alone in every logged line, e.g. 00..053FEC both)? Not verifiable from logs. Beobachten.
2. Dedup ring forgets telegrams after about 20-48 min (DUP:n on 1207-2601 s old copies). The CET gate, loop check and hop0 contain the damage; no fix needed for CET, general property of the ring (`RX_DEDUP_ADD slot=x/100`).
3. Gate is evaluated at dequeue (`doTX()` -> `txRingDropStaleCet`, `lora_functions.cpp:2913`); the age at air is dequeue age plus CSMA/CAD time (CAD attempts up to 20). A 147.9-s dequeue (oe3xir) airs at about 149 s. Edge, OK.
4. `{CET}` payload is UTC although labelled CET (server side); document it. Third-party clients showing it as CET are off by 1 h (winter) / 2 h (summer).

## Open questions and caveats

- Who is OE1XAR-63 (third SSID, DL region, ids collide with -33's counter family)? Not in the brief.
- DB0AIS has no wall clock: ages carry about 1-s error; start/end times derived from NTP epoch and node clock.
- Radiation counts are lower bounds (only what our five receivers decoded); the "followed by relay" metric favours our own radiations (we hear their relays).
- Build is unknown; inferred only from log shape. oe3xhl/xir lack `RX-UDP`/`TX-LoRa` display lines; recovered via `[GW]`, `[CET]`, `[LOG] TX`.
- msg_id key: OE3 uses msg_id alone (single source, unique within 4 h per telegram); DB0AIS uses (source, msg_id).
