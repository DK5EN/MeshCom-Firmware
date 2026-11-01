# F3 / F4 - what the time-based decay costs and what it gains (hb9: HB9VQQ-4, HB9VQQ-5)

F3 "what do we lose through the time-based decay in the TX slots" (TTL and `{CET}` gate; CSMA age floor as section 5),
F4 "what did we gain". Source: `RING_DROP_STALE` (printed under `--loradebug` alone, txring_functions.cpp:636-641, so
complete on HB9VQQ-5 as well), `RING_WRITE/RING_TX_READ lat=`, `[CET];tx age_s/wait_ms`, `[CET];rx`, `CSMA_AGED`,
`[MC-HWM]`. TTL rules: `src/txring_ttl.h` (prio 5 HEY any status 180 s; DONE only: ACK prio 1 90 s, position prio 4
120 s, relay/server text prio 3 300 s; prio 2 and own READY texts never age), `{CET}` gate 150 s (`src/cet_age.h`,
CET_MAX_AGE_S). CSMA floor: csma_timing.h:71-110, constants configuration_global.h:405-411 (stage 1 20 s, stage 2 60 s).
`q34/results.json` has no HB9VQQ-5 section (it needs `[LOG]`); HB9VQQ-5 is measured here with `decay_checks.py`
(`out/decay5.json`). TTL ages from `ringEnqueueTime`, which a supersede resets (txring_functions.cpp:1007).

## 0. Verdict

| Question                              | HB9VQQ-4                                                      | HB9VQQ-5                                        |
| ------------------------------------- | ------------------------------------------------------------- | ----------------------------------------------- |
| F3 loss through TTL / CET gate        | none measured (0 drops); HEY tail at 92.5 % of TTL: Beobachten | none measured (0 drops); HEY tail 87.0 %: Beobachten |
| F3 loss through CSMA age floor        | OK (26 episodes, no collision signature)                       | OK (4 episodes)                                 |
| F4 gain from decay                    | not demonstrable on this load (nothing to decay); insurance only | same                                          |
| Handlungsbedarf                       | none                                                           | none                                            |

## 1. Drops: zero on both nodes (fact)

`RING_DROP_STALE` 0 (reason ttl and cet), `[MC-STAT] drop: p1..p5` summed over 288 windows 0, `RING_DROP_PRIO/NEW` 0 on both
nodes. `q34/results.json` HB9VQQ-4 `drops_total 0`, `own_dropped 0`, `drop_toa_s 0.0` (0.0 % of 2259 s TX airtime) agree.
OE3 cluster for comparison: 232 drops, 153 own beacons lost, 167 s = 1.23 % of TX airtime. So on this mesh the answer to
F3 is "nothing was lost to the decay on 31.10. - 01.11.", with the caveat of section 2: the margin on the HEY class is
thin and the load had two bursts.

## 2. How close did entries get to their TTL (headroom, fact; entries that were AIRED, `lat=` / TTL)

| Class (TTL)                         | -4 n | p50 s | p90 s | p99 s | max s | max/TTL | >= 50 % / >= 80 % | -5 n | max s | max/TTL | >= 50 % / >= 80 % |
| ----------------------------------- | ---- | ----- | ----- | ----- | ----- | ------- | ----------------- | ---- | ----- | ------- | ----------------- |
| prio 5 own HEY (180 s)              | 96   | 20.9  | 64.7  | 159.4 | 166.5 | 92.5 %  | 7 / 2             | 118  | 156.7 | 87.0 %  | 3 / 1             |
| prio 5 relayed HEY (180 s)          | 1404 | 9.7   | 58.6  | 75.4  | 158.9 | 88.3 %  | 8 / 1             | 1202 | 153.8 | 85.4 %  | 4 / 3             |
| prio 4 own WX position (120 s)      | 47   | 13.2  | 42.6  | 78.0  | 80.4  | 67.0 %  | 2 / 0             | 48   | 78.0  | 65.0 %  | 3 / 0             |
| prio 4 relayed position (120 s)     | 583  | 8.9   | 35.9  | 64.4  | 67.4  | 56.2 %  | 18 / 0            | 383  | 61.1  | 51.0 %  | 1 / 0             |
| prio 3 relayed text (300 s)         | 156  | 8.6   | 15.8  | 59.3  | 61.9  | 20.6 %  | 0 / 0             | 150  | 48.6  | 16.2 %  | 0 / 0             |
| prio 3 server text incl. {CET} (300 s) | 253 | 9.0  | 11.2  | 13.5  | 60.0  | 20.0 %  | 0 / 0             | 248  | 27.2  | 9.1 %   | 0 / 0             |
| prio 1 ACK (90 s)                   | 32   | 22.7  | 34.9  | 42.2  | 43.3  | 48.1 %  | 0 / 0             | 22   | 42.5  | 47.3 %  | 0 / 0             |

(`out/decay4.json`, `out/decay5.json`; the HB9VQQ-4 rows agree with `q34/results.json` `headroom`, p5 3 entries >= 80 %.
Own HEY in a READY state ages like a relay HEY, TTL prio 5 is status-independent: txring_ttl.h:55.)

1. **Only the HEY class comes close.** 3 entries on -4 and 4 on -5 aired at >= 80 % of 180 s (15 and 7 at >= 50 %) (HB9VQQ-4:
   03:25:27 own HEY 166.5 s, 18:56:26 own HEY 159.0 s, 11:13:06 relay 158.9 s; HB9VQQ-5: 18:59:02 own HEY 156.7 s, relay 153.8 s, 152.9 s, 152.6 s).
   Both nodes are in the same burst 18:56-19:02 (q2 section 7: RX 14-19 events/min, TX 1-2/min, queue 4-8). Margin to
   the TTL in the worst cases: 13.5 s (-4) and 23.3 s (-5).
2. **TTL sensitivity: what a shorter TTL would have cost (aired entries older than X, own in brackets).**

| TTL X  | prio 5 HEY -4 | prio 5 HEY -5 | prio 4 position -4 | prio 4 position -5 | prio 1 ACK -4 | prio 1 ACK -5 |
| ------ | ------------- | ------------- | ------------------ | ------------------ | ------------- | ------------- |
| 180 s  | 0             | 0             | -                  | -                  | -             | -             |
| 150 s  | 3 (2)         | 4 (1)         | -                  | -                  | -             | -             |
| 120 s  | 6 (4)         | 5 (2)         | 0                  | 0                  | -             | -             |
| 90 s   | 15 (7)        | 7 (3)         | 0                  | 0                  | 0             | 0             |
| 60 s   | 146 (10)      | 27 (6)        | 20 (2)             | 4 (3)              | 0             | 0             |
| 45 s   | -             | -             | 46 (5)             | 7 (5)              | 0             | 0             |
| 30 s   | 326 (35)      | 94 (24)       | 81 (8)             | 12 (10)            | 5 (0)         | 3 (0)         |

   Reading (labelled as a model, because the entries would have been dropped before waiting longer): with today's TTLs
   nothing is lost; the HEY TTL would have to fall to 150 s before the first own HEY is lost (2 on -4, 1 on -5), to 120 s
   for 4 of 96 own HEYs (4.2 %) on -4 and 2 of 118 (1.7 %) on -5. An own HEY lost means the next Trickle cycle (about
   15 min, `[MC-TRICKLE] interval=900000ms`) is the next chance (inference). Position and ACK classes have wide margin.
   Verdict: TTL is sized correctly for this load (OK) and the HEY class is the one to watch under heavier load (Beobachten,
   because the OE3 cluster lost 99 own HEYs and 54 own WX to exactly this class).
3. **No stuck entry.** Longest wait of anything aired: 166.5 s; `[MC-HWM] wait_hwm` on -5 156660 ms (matches). Chains
   (supersede resets the age): longest own-HEY chain 2, on -4 no own-HEY chain spans more than 180 s
   (`q2/results.json` hey_chains.own_hey `span_over_180s` 0).

## 3. The `{CET}` gate (TTL-like gate on payload age, 150 s)

| Measure                                 | HB9VQQ-4                          | HB9VQQ-5                          |
| --------------------------------------- | --------------------------------- | --------------------------------- |
| `[CET];tx` events (all prio 3)          | 207 (142 server telegrams + 65 relayed LoRa copies, inference) | 219 (143 + 76) |
| payload age at TX p50 / p99 / max       | 10 / 47.9 / 56 s                  | 9 / 33.9 / 40 s                   |
| wait in the pool p50 / p99 / max        | 8.9 / 23.6 / 25.2 s               | 8.6 / 14.3 / 27.2 s               |
| TX older than 120 s / 150 s             | 0 / 0                             | 0 / 0                             |
| `RING_DROP_STALE reason=cet`            | 0                                 | 0                                 |
| time source for the gate                | `trusted;1` on every line         | `trusted;1` on every line         |

1. **Fact: the gate never fired and had nothing to stop.** Every `{CET}` we radiated was at most 56 s (-4) / 40 s (-5) old, i.e.
   94 s / 110 s inside the 150 s limit; waits are far below the 180-300 s ring TTLs. Server telegrams enter at age 0
   (`[CET];gw_rx ... age_s;0`), OE1XAR-33 every 607 s without a miss on both (142/142 and 143/143).
2. **One stale telegram on air in 24 h, from a foreign node (Beobachten).** At 01.11. 06:22:56 HB9HNO-49 (fw 4.35h per census,
   pre-CAD generation, no gate) re-radiated `{CET}` id 6ADB5BE4 (OE1XAR-63, payload time 04:58:05 UTC) 1492 s after the
   telegram was made. HB9VQQ-5 had relayed the same telegram fresh at 05:58:10.9 (age 5 s, hb9vqq5.log line 203224); the copy
   HB9HNO-49 sent 24.9 min later carries `...,HB9VQQ-5,HB9HNO-49` and reached HB9VQQ-4 as new (hb9vqq4.log line 313921-313928:
   `[CET];rx;src;OE1XAR-63;age_s;1492;...hop;0`, `[LOG] RLY ... q=hop0`). It was not relayed by us because the hop budget was
   0, so the gate was not exercised. It is 1 of 148 foreign `[CET];rx` events on -4 (0 of 143 on -5, max age 57 s). In OE3 four foreign relays
   radiated 62/64, 47/47, 46/46 and 44/49 of their telegrams stale. Operator/neighbour side; inference: the delay sits in
   that node's queue or dedup, not in ours.
3. **F3 for the gate: no loss.** No fresh `{CET}` was blocked (0 drops) and no stale one of ours radiated. F4: see section 6.

## 4. Loss test (relay drops that nobody else carried)

Not applicable: 0 drops, so 0 relay drops to test against other carriers (OE3: 74 relay drops, 55 carried elsewhere, 19
potential losses). The only entries that left the pool without a TX are the cancelled/superseded ones of q2 sections 2-3;
they are not decay: each had a newer or foreign copy first. HB9VQQ-5 cannot be classified further without `[LOG]`
(unmeasurable), but with 0 drops there is nothing to classify.

## 5. Subsection: CSMA age floor (Brief G, prio 1-3 only, after a received frame)

| Measure                                              | HB9VQQ-4                          | HB9VQQ-5                       |
| ---------------------------------------------------- | --------------------------------- | ------------------------------ |
| `CSMA_AGED` events / slot episodes                   | 124 / 26                          | 8 / 4                          |
| stage 1 / stage 2                                    | 124 / 0                           | 8 / 0 (`aged2=0` in MC-HWM)    |
| age at the first event p50 / max                     | 21.8 / 38.2 s (floor 20.0 s)      | 33.2 / 40.9 s                  |
| hold applied                                         | 1500-1675 ms (p50 1570)           | 1500-1640 ms                   |
| episodes by source/prio                              | ACK forward 11, GW-ACK 5, relay text 7, server text 1, own group text 2 | GW-ACK 3, ACK forward 1 |
| episode ended with a TX                              | 26 of 26                          | 4 of 4                         |
| remaining wait after the first event p50 / p90 / max | 3.2 / 7.8 / 10.9 s                | 1.7 / 9.1 / 12.3 s             |
| prio 1-3 TX waiting >= 20 s / >= 60 s                | 39 of 447 (8.7 %) / 3             | 13 of 424 (3.1 %) / 0          |

(`out/decay4.json`, `out/decay5.json` csma_aged; events joined to the live ring entry by slot and time, 0 unmatched. The
`[MC-HWM] aged1` counter on -5 rose from 2 to 15 = 13, equal to the 13 TX with wait >= 20 s - cross-check passes.)

1. **Cost: no collision signature (fact, small n).** HB9VQQ-4 (`q34/results.json` csma probes): after an aged prio-1 TX 0.0 % CRC
   errors within 3 s (n = 21) vs 0.0 % after a fresh one (n = 13); CAD attempts at the aged TX mean 0.19 vs 0.31 fresh; echo heard
   within 60 s 85.7 % vs 69.2 %; prio 3 aged (n = 15) 0.0 % vs 0.0 %. The floor hold (1.5-1.7 s) is chosen to end before the
   3.0 s the receivers of the previous frame hold (configuration_global.h:405-411), the design reason for no collisions.
   Verdict OK.
2. **Gain: shorter tail, modelled.** The model saves 3.1 s median per episode versus the attempt-0 hold (sum 106 s over 26
   episodes) and 1.1 s versus the attempt-2 hold (`q34/results.json` `csma.replay`); this is a replay, not a measurement. The
   measured part is the remaining wait after the first aged event: median 3.2 s. The floor only matters for 3-9 % of prio 1-3 TX
   here (OE3: 13709 events in about 3670 episodes; the DB0AIS "0 events" case, trigger never met).
3. **Own beacons are outside the floor** (prio 4/5 and own, csma_timing.h:101, lora_functions.cpp:4030-4043): the
   mechanism behind the own-HEY tail in section 2 (see OE3 `rca_own_beacons.md`). No loss at this load.

## 6. F4 - what did we gain from the decay

Honest answer: on this capture the decay produced no event, so the gain cannot be shown by an avoided harm; it can only be bounded.

1. **No ring-full protection was needed.** Peak occupancy 8 / 9 of 20 slots (q2 section 1); the oldest entry was 166.5 s old.
   Without any TTL the pool would have behaved identically on every one of the 2577 / 2183 transmissions, because no entry
   exceeded its TTL. The `q34/results.json` HB9VQQ-4 `cf` (no-TTL replay: depth max 26, 285 baseline TTL drops, 34.7 % of
   events at depth >= 10) is **not valid for this node**: the replay has no overhear-cancel path, the node frees 383 slots
   that way, the baseline pick matches only 68.9 % and predicts 285 TTL drops where 0 happened. Do not quote it.
2. **Freshness:** 15 of 1500 HEY (1.0 %, -4) and 7 of 1320 (0.5 %, -5) aired older than 90 s, 6 / 5 older than 120 s. These
   are the entries a TTL of 120 s would have removed instead of airing a 2-3 min old neighbourhood report (the stated BP-03
   motivation, configuration_global.h comment above RING_BG_MAX_AGE_MS). Whether airing them late is a gain or a loss is a
   policy question; HEY content is refreshed by the next Trickle cycle either way (inference).
3. **`{CET}`:** our radiations are at most 56 s / 40 s old, so the gate is not what keeps them fresh here; the pool wait is. The
   gate's protective value (stale `{CET}` never leaves the ring) is not observable in this data: the single stale telegram
   (section 3.2) reached us over the air and was not ours to relay. It matters for nodes with deeper queues (OE3 gate stopped
   8 + 4 stale relays).
4. **Net:** loss 0, measured gain 0, exposure: the HEY class sits at 87-93 % of its TTL in a burst. This is the profile of a
   safety net that has not been needed yet; the OE3 cluster shows what it does under load (232 drops: freshness bought with 153
   lost own beacons, 1.23 % of airtime saved).

## 7. Unmeasurable / caveats

- HB9VQQ-5: no drop-table classification, no counterfactual replay (needs `[LOG] TX/RLY`); both would be empty/meaningless with
  0 drops. `[CET];tx age_s/wait_ms`, `RING_*`, `CSMA_AGED`, `[MC-HWM]` are complete and used.
- Logger blind spots on -5 (about 135 s of 24 h) could hide a `RING_DROP_STALE` line (health section 2.7); the 8 unmatched
  reads show the same entries, no hole in the age statistics beyond them (latency comes from the read line).
- TTL is aged from `ringEnqueueTime`; a supersede restarts it (txring_functions.cpp:1007), so `lat` is a lower bound on the
  age of the information; chains are at most 2 here.
- The 180 s HEY TTL applies to own READY HEYs as well (txring_ttl.h:55); no own HEY was lost on either node.

## 8. Suspected bugs and code sites

- None in the decay code on this capture (0 drops, gate not exercised, CSMA floor behaves as designed: stage 1 only, hold
  1.5-1.7 s, every episode ends in a TX).
- Tooling: `q34/results.json` HB9VQQ-4 `cf` is invalid for a node with silent overhear-cancel (see section 6.1); the replay
  would need the cancel path or an HB9VQQ-4 exclusion like `HL excluded` in `meta.cf_model`.
- Design watch item carried over from OE3: own prio-4/5 entries are not covered by the age floor (csma_timing.h:101) and keep the
  5.5 s hold re-armed per received frame (lora_functions.cpp:2746-2747); under heavier load this turns the 180 s HEY TTL into
  a drop of own beacons. Not a defect at hb9 load.
