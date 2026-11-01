# F2 - how well does the TX buffer with slots work (hb9: HB9VQQ-4, HB9VQQ-5)

Source: `RING_WRITE / RING_PRIO / RING_TX_READ / RING_STATUS / RING_SUPERSEDE(_KEEP) / TX_GATE_ENTER / CAD_*`,
`[MC-STAT] / [MC-PRIO] / [MC-HWM]`, all `[MC-DBG]` and available WITHOUT `--setlog`, so HB9VQQ-5 is measured with the same
join as HB9VQQ-4 (slot + msg_id, exact; `ring_fate.py`). Pool: MAX_RING 20, TTLs and priorities per
`src/configuration_global.h:465-487`, `src/txring_ttl.h`. Both nodes carry the TXP campaign code (q34 / health section 1.2).
Check outputs: `verify/hb9_analysis_checks/out/` (fate*.json, node*.json, svc*.json, pair.json, slow*.json). Mesh never
correlated with OE3; OE3 numbers are quoted from `CORE-FINDINGS.md` for comparison only.

## 0. Verdict

| Aspect                                   | HB9VQQ-4                                                         | HB9VQQ-5                                         |
| ---------------------------------------- | ---------------------------------------------------------------- | ------------------------------------------------ |
| Build-up, overflow, drops                | OK (max depth 8 of 20, 0 drops)                                  | OK (max depth 9 of 20, 0 drops)                  |
| Nothing lost                             | OK (0 TTL/ring drops; supersede 1.7 %; silent cancel 12.7 %)     | OK (0 drops; supersede 1.3 %; cancel 0.9 %)      |
| Few double transmissions                 | Beobachten: 67 % of server injects and 36 % of relays aired after a foreign copy was heard | Beobachten: 49 % / 10 % |
| Pair HB9VQQ-4 / HB9VQQ-5 double airing   | Beobachten: 1392 relay ids aired by BOTH gateways (92 % of the ids both queued) | (see -4) |
| Priority order / service time            | Beobachten: ACK (prio 1) waits longer than HEY relays; own HEY 2x relay wait | same, milder                  |
| Channel-access headroom                  | Beobachten: busy minutes write 2.9/min vs TX 2.3/min             | OK                                               |
| Handlungsbedarf                          | none                                                             | none                                             |

## 1. Occupancy: nothing builds up (fact)

| Measure                                 | HB9VQQ-4                  | HB9VQQ-5                       |
| --------------------------------------- | ------------------------- | ------------------------------ |
| `RING_STATUS` samples (30 s)            | 2879                      | 2877                           |
| queued mean / p50 / p90 / p99 / max     | 0.55 / 0 / 2 / 4 / 6      | 0.27 / 0 / 1 / 3 / 8           |
| share >= 3 / >= 5 slots                 | 4.69 % / 0.56 %           | 1.04 % / 0.24 %                |
| depth after write, max                  | 8                         | 9                              |
| `TX_GATE_ENTER qlen` p50 / p99 / max    | 1 / 5 / 8                 | 1 / 4 / 9                      |
| `[MC-STAT] qmax` (since-boot latch)     | 10/20 all 288 windows     | 5 -> 6 (15:46:39) -> 9 (19:01:40) |
| hourly-mean slope                       | -0.0018 slots/h           | -0.0030 slots/h                |
| `RING_DROP_*`, `RING_OVERFLOW`, REARM   | 0 / 0 / 0                 | 0 / 0 / 0                      |
| `[MC-STAT] drop: p1..p5` summed         | 0                         | 0                              |

1. The pool never exceeded 45 % of its 20 slots (HB9VQQ-5 peak 9 on 31.10. 19:01) and drains back to 0-1.
   No slope over the day (hourly means 1.00 -> 0.91 / 0.35 -> 0.30). Evidence: out/node4.json, out/node5.json
   (`occupancy`), `[MC-STAT]` series hb9vqq5.log 10:06:39 .. 19:01:40. Verdict OK.
2. `qmax` on HB9VQQ-4 is a since-boot latch (10/20 reached before the capture, never exceeded in 24 h): it carries no
   information about the capture. The measured peak is the 6 in `RING_STATUS` and 8 after a write.
3. The `consistency` block in `q2/results.json` (queued minus model, exact 81 %) only checks the extractor's slot model;
   not a pool property.

## 2. Slot fate: where do the ring writes go (fact, sums 100 %)

| Fate of a write                          | HB9VQQ-4 (3010 writes)  | HB9VQQ-5 (2224 writes, + 8 unseen) |
| ---------------------------------------- | ----------------------- | ---------------------------------- |
| aired (RING_TX_READ, same slot + id)     | 2577 (85.61 %)          | 2175 (97.80 %)                     |
| superseded in place (RING_SUPERSEDE)     | 50 (1.66 %)             | 28 (1.26 %)                        |
| left the pool silently ("vanished")      | 383 (12.72 %)           | 21 (0.94 %)                        |
| TTL / {CET} gate drop (RING_DROP_STALE)  | 0                       | 0                                  |
| ring full (DROP_NEW / DROP_PRIO)         | 0                       | 0                                  |
| SUPERSEDE_KEEP (new frame not queued)    | 3                       | 22                                 |

- Per source (aired / superseded / vanished): HB9VQQ-4 rx_relay 2143 / 49 / 383, own HEY (auto_pos) 96 / 1 / 0, own WX 47 / 0 / 0,
  server text (udp_rx) 253 / 0 / 0, ACK forward 22, GW-ACK 5 + 5, own text 6. HB9VQQ-5: rx_relay 1735 / 23 / 20, own HEY
  118 / 1 / 0, own WX 48, udp_rx 248 / 4 / 1, ACK forward 8, GW-ACK 7 + 7, own text 4.
- By priority, vanished on HB9VQQ-4: prio 5 (HEY relay) 280, prio 4 (position relay) 93, prio 3 (text relay) 10;
  prio 1/2 never. On HB9VQQ-5: 18 / 2 / 1.
- The 8 unseen HB9VQQ-5 writes (reads without a write) fall into the hourly logger blind spots (health section 2.7); the
  true HB9VQQ-5 total is 2232 writes, 2183 TX (= `RING_TX_READ` = `TX_START` = `[MC-STAT]` tx sum 2183, exact).
- Cross-check HB9VQQ-4: `RING_TX_READ` 2577 vs `[MC-STAT]` tx sum 2580 (3 = window edges at capture start/end).
- SUPERSEDE_KEEP (3 / 22) and SUPERSEDE (50 / 28) are the TXP W1c rule (txring_functions.cpp:1033/:1068): a newer
  HEY/position of the same originator (or a newer `{CET}`) replaces the queued one in place; `RING_SUPERSEDE_KEEP` is the
  reverse case, the queued `{CET}` carries the newer timestamp and the arriving copy is discarded
  (txring_functions.cpp:880-906; ids 6AE5xxxx = OE1XAR-33 vs 6ADBxxxx = OE1XAR-63). Chains: 41 of 49 superseded relays ended aired. Not a loss: the
  replaced frame is obsolete by construction. Verdict OK.

## 3. The 12.7 % that vanish on HB9VQQ-4: silent overhear-cancel (inference, strong)

Fact: 383 relay slots (prio 5/4/3, types 40/21/3A) leave the pool without a TX line, a drop line, a
supersede line or a `[NBR]` line. Nothing logs them: `[NBR]` markers are off on both nodes (`bNBRDEBUG` off,
`nbrLog == NULL`, lora_functions.cpp:694), and the cancel path frees the slot first and prints only if
`nbrLog != NULL` (lora_functions.cpp:1446, :1463).

Evidence that it is the NBR overhear-cancel (`--nbrrelay on`, `bNBRCANCEL`):

| Test (ring_fate.py)                                              | HB9VQQ-4        | HB9VQQ-5        |
| ---------------------------------------------------------------- | --------------- | --------------- |
| vanished relays that had a foreign copy while queued             | 383 of 383 (100 %) | 19 of 20 (95 %) |
| aired relays that had a foreign copy while queued                | 780 of 2143 (36.4 %) | 173 of 1735 (10.0 %) |
| first foreign copy after the write: p50 / p90 / max              | 6.8 / 23.3 / 63.2 s | 11.1 / 30.2 / 35.0 s |
| P(vanish given a foreign copy was heard while queued)            | 383/1163 = 33 % | 19/192 = 10 %   |

"Foreign copy" = `RX_DEDUP_DUP` of the same msg_id whose MH-LoRa path has >= 2 tokens and last hop != own call (the
condition of lora_functions.cpp:1327-1331). A copy is a necessary condition for a cancel, and it is met by every vanished
slot; the other code sites that free a slot do not apply to DONE relay entries (lora_functions.cpp:438 ACK scan frees
SENT slots, :1107 PN release needs READY, :1176 own echo, :3359-3427 own retry ladder, txring_functions.cpp:318 TX consume,
:615 TTL sweep, which logs). Not proven: the mode itself (`--nbrrelay on` vs `count`) is not readable from the log; the
info command prints it with the cancel counter (command_functions.cpp:7311). Three of the 21 HB9VQQ-5 vanished entries
overlap a logger blind spot (21:38:38, 01:00:01, 10:00:01) and may simply have been aired unseen: one is proven aired,
the server text 5E45D03F (written 21:38:31.7, `CHANNEL_UTIL` 21:38:47.6 shows `tx=860ms` = 0.85 s for a 73-byte frame, no
other TX in that window).

Effect (upper bounds, airtime from the written length, SF11/BW250/CR4:6):

- HB9VQQ-4: the 383 cancelled relays would have cost 357 s of airtime, 13.6 % on top of the real 2271 s TX airtime.
- HB9VQQ-5: 18.6 s (1.0 %).
- Still aired although a foreign copy was heard while queued (cover not complete / case A veto, cannot be told apart
  without NEED lines): HB9VQQ-4 780 relays = 708.6 s = 31.2 % of its TX airtime; HB9VQQ-5 173 relays = 152.1 s = 8.1 %.
  OE3 cluster: 29.6 % (upper bound, same definition).
- Server injects (`udp_rx`): HB9VQQ-4 253 aired, 193 s (8.5 % of TX airtime), 170 (67 %) after a foreign copy;
  HB9VQQ-5 248 aired, 193 s (10.2 %), 122 (49 %). They are never cancelled (0 vanished), consistent with OE3 finding 5
  ("server slots always sole provider", NEED case A) - Beobachten/design, unchanged from the OE3 verdict; unprovable
  here without NEED lines.

Verdict for "nothing lost": OK on the evidence (every vanished slot had a foreign copy first). Beobachten for
observability: 12.7 % of all ring writes on HB9VQQ-4 are invisible in a standard capture.

## 4. Double transmissions

1. **Same id twice from one node within the dedup window: only the GW-ACK pairs.** HB9VQQ-4: 5 pairs, HB9VQQ-5: 7 pairs
   (all `rx_dm_ack_gw -> rx_dm_ack_new`, prio 1, type 41, written 0.001-0.06 s apart, aired 3.5-23.7 s / 3.6-15.0 s apart);
   0.19 % / 0.32 % of TX, about 0.31 s airtime each. This is the documented gateway-ACK-plus-ACK behaviour
   (CORE-FINDINGS: "GW-ACK pairs twice by design, GWACK-01"). `double_send_bug_type_00_then_FF_same_slot` = 0 on both.
2. **58 / 49 ids appear twice in `RING_TX_READ` (all other pairs >= 59 min apart): not re-relays.** Every one of the 53 / 42
   candidates (> 5 min apart) carries a different payload at its two receptions and all 53 / 42 originate from HB9JAT
   (msg-id range 4EBC7xxx): msg-id reuse by the sender, `out/idreuse*.json`. Minimum gap 63.2 min (-4) and 59.3 min (-5).
   Dedup window (100 entries / `RX_DEDUP_ADD` rate): 3419 adds = 142/h -> about 42 min (-4), 2598 adds = 108/h -> about
   55 min (-5). Inference/Beobachten: HB9JAT's id cycle (>= 59 min) is only 4 min longer than the -5 window; a faster cycle
   would drop a fresh HEY as a duplicate (not observed).
3. **Both gateways air nearly everything both queue (Beobachten).** Ids written to the ring on both nodes (msg_id within
   30 min): 2080. Aired by both: 1929 (92.7 %); relays only: 1511 pairs, 1392 aired by both (92.1 %), prio 5: 1015, prio 4:
   288, prio 3: 89; server injects: 234 aired by both. Gap between the two air times: p50 9.3 s, 1735 of 1929 within 30 s;
   HB9VQQ-4 first in 1301 (67 %). Write delta (-5 minus -4) p50 +4.4 s. Cancels caused by the partner are rare: only 56
   (-4) / 51 (-5) aired relays followed a copy of the partner heard while queued, and 15 / 11 vanished slots had a partner
   copy. The two nodes hear each other (HB9VQQ-5 as last hop at -4: 1292 frames, p50 -97 dBm; reverse 2181 frames, p50
   -100 dBm), so a copy is available; whether the partner's coverage made the second airing unnecessary needs the `[NBR]`
   NEED/CANCEL lines. Inference: the nodes cover different areas (18.6 km, different elevation), so most second airings
   carry a unique listener; no evidence of a defect, only of cost. Operator test: 1-2 h capture with `--nbrdebug on`.

## 5. Latency by priority (RING_TX_READ `lat=`, fact)

| Prio (class)       | -4 n | p50 s | p90 s | p99 s | max s | -5 n | p50 s | p90 s | p99 s | max s |
| ------------------ | ---- | ----- | ----- | ----- | ----- | ---- | ----- | ----- | ----- | ----- |
| 1 ACK / ping       | 34   | 22.4  | 34.3  | 42.1  | 43.3  | 22   | 15.5  | 36.0  | 42.5  | 42.5  |
| 2 group text (own) | 4    | 23.1  | 26.8  | 27.3  | 27.3  | 4    | 7.5   | 14.4  | 15.9  | 16.1  |
| 3 text relay / gw  | 409  | 9.0   | 11.9  | 32.7  | 61.9  | 398  | 8.7   | 11.9  | 21.5  | 48.6  |
| 4 position         | 630  | 8.9   | 36.7  | 65.1  | 80.4  | 431  | 8.4   | 12.5  | 52.2  | 78.0  |
| 5 HEY              | 1500 | 9.8   | 58.8  | 85.8  | 166.5 | 1320 | 8.8   | 21.3  | 66.9  | 156.7 |

(`[MC-PRIO]` weighted means agree: -4 p3 8.7 s, p4 13.5 s, p5 19.4 s; -5 p3 8.2 s, p4 9.0 s, p5 11.6 s. `[MC-HWM] wait_hwm`
on -5 = 156660 ms = the 156.7 s above.)

1. Text/position/HEY relays wait about one hold (median 8-10 s), the tail is the strict-priority queue. Strict priority
   overtakes constantly: `preempt` (pick was not the oldest) 302 of 2580 TX (11.7 %) on -4, 112 of 2183 (5.1 %) on -5.
   Verdict OK.
2. **ACK (prio 1) waits longer than HEY relays (Beobachten).** p50 22.4 s (-4) / 15.5 s (-5) against 9.8 / 8.8 s for HEY
   relays, although ACK has the highest priority and the shortest base hold (3.0 s vs 5.5 s, configuration_global.h:483-487).
   Queue empty at write: p50 6.5 s (n = 13) / 12.2 s (n = 11); queue not empty: 25.5 s (n = 21) / 20.5 s (n = 11). Small
   samples; the OE3 cluster shows the same direction (ACK p50 21.9 / 23.3 / 20.9 s). Mechanism not re-verified here (OE3
   inference: hold re-armed by every received frame, the 20 s age floor then shortens it, see q34 section 5). Max ACK wait
   43.3 s / 42.5 s = 48 % / 47 % of the 90 s ACK TTL.
3. Own HEY waits about twice as long as a relayed HEY of the same prio (see section 6).

## 6. Own beacons (same mechanism as OE3 rca_own_beacons, much milder here)

| Measure                          | HB9VQQ-4             | HB9VQQ-5             |
| -------------------------------- | -------------------- | -------------------- |
| own HEY written (= Trickle SEND) | 97                   | 119                  |
| aired / superseded by next own   | 96 / 1               | 118 / 1              |
| own WX (position) aired          | 47 of 47             | 48 of 48             |
| own HEY wait p50 / p90 / p99 / max | 20.9 / 64.7 / 159.4 / 166.5 s | 12.8 / 49.2 / 126.2 / 156.7 s |
| relayed HEY wait p50 / p90 / max | 9.7 / 58.6 / 158.9 s | 8.7 / 16.1 / 153.8 s |
| own HEY wait > 120 s             | 4                    | 2                    |
| own HEY wait, queue empty / not  | p50 12.3 s (n 61) / 36.4 s (n 35) | 9.1 s (98) / 34.6 s (20) |

- Own HEY and WX are never lost here (99-100 % of episodes aired; OE3: OC 41.8 %, IA 78.8 %, OC own WX lost 34 of 49). The
  `[MC-TRICKLE]` lines explain the HEY cadence: SEND 97 / 119, SUPPRESS 44 / 54 (cycles skipped on purpose), TOPO_CHANGE 15
  / 29 (interval reset to 30 s, hence the 60 s write gaps); a gap > 20 min between own HEYs (10 / 13) is suppression, not loss.
- Same asymmetry as OE3, in miniature: at an empty queue a relayed HEY waits p50 8.4 s (-4) / 7.3 s (-5), consistent with the
  case-A escalation after about 8 s (txring_functions.cpp:385-405), an own HEY 12.3 / 9.1 s. The own beacon is excluded
  from that path and from the age floor (csma_timing.h:101, lora_functions.cpp:4030-4043), so it waits longer. Inference:
  the OE3 root cause is the same; here the load is too low for it to cost beacons. Verdict Beobachten (tail up to 92 % / 87 %
  of the 180 s TTL, section q34 2).
- Slowest examples (`slow4.json`): 03:25:27 own HEY 166.5 s behind 3x prio 3, 1x prio 4, 1x prio 5 (7 gate entries, 2
  confirmed CAD busy, 30 frames received meanwhile); 18:56:26 own HEY 159.0 s (queue 4-6, 8 gate entries, 44 frames
  received).

## 7. Channel access is the limit, not the pool (Beobachten, HB9VQQ-4)

TX service per minute against RX load (`service_rate.py`; RX events = OnRxDone + CRC errors, each re-arms the CSMA hold):

| RX events per minute | -4 minutes | -4 writes/min | -4 TX/min | -4 queue mean | -5 minutes | -5 writes/min | -5 TX/min |
| -------------------- | ---------- | ------------- | --------- | ------------- | ---------- | ------------- | --------- |
| 0-5                  | 177        | 0.97          | 0.90      | 0.16          | 523        | 0.78          | 0.84      |
| 6-10                 | 402        | 1.67          | 1.44      | 0.33          | 808        | 1.82          | 1.76      |
| 11-15                | 595        | 2.31          | 2.02      | 0.59          | 108        | 3.15          | 2.94      |
| 16-20                | 244        | 2.92          | 2.34      | 0.91          | 2          | 3.50          | 2.50      |
| 21+                  | 23         | 3.30          | 3.00      | 1.39          | 0          | -             | -         |

- On HB9VQQ-4, 29.5 % of RX gaps are >= 5.85 s (the prio 4/5 hold plus jitter) and the mean is 11.4 RX events/min; in the
  busiest 17 % of the day the node writes 2.9 frames/min and sends 2.3. The deficit is closed by cancel and supersede (432 of
  3010 writes), which is why the pool does not grow. Without them the write rate (125/h) would sit within about 15-30 %
  of the observed service rate (inference from the per-minute table).
- The burst 31.10. 18:56-19:02 shows it on both nodes at once: -4 RX 14-19 per minute, 1-2 TX per minute, queue 4-6, own HEY
  159 s; -5 queue 7-8, four HEY entries waiting 153-157 s (A3F15113 own HEY, A4029176, 4B11C313, E9F69234). `qmax` 9 on -5.
  Nothing was dropped because the longest wait stayed 13-27 s under the 180 s TTL (q34).
- Same conclusion as OE3: TX duty is only 2.6 % / 2.2 %, the limit is the hold re-armed by received frames
  (lora_functions.cpp:2746-2747), not airtime. Handlungsbedarf: none at this load; the margin is what q34 section 2 measures.

## 8. Unmeasurable / caveats

- HB9VQQ-5: no `[LOG] TX`, so no `src=/wait=/cad=` per frame (reconstructed, section 2), no `[LOG] RLY` reasons, no
  `[NBR]`. The extractor's q2 numbers for -5 (`fate.tx_noair_other`, `own_hey.aired_pct 0.0`) are artefacts (health
  section 5); this file replaces them.
- `RING_WRITE ... status=FF` is the normal state for relays/ACKs/server texts (DONE = final, no retransmit); own texts
  are `00` (READY). The retry ladder never ran: `RING_REARM` 0, `retry>0` 0 on both - untested by this data.
- Logger blind spots on -5 (27 x about 5 s) hide at most 8 writes, 8 reads and make 3 of 21 "vanished" ambiguous.
- Mode of `--nbrrelay` not readable from the log (see section 3).

## 9. Suspected bugs and code sites

- **No firmware defect found in the pool itself** (0 overflow, 0 double-send bug type, exact accounting).
- Observability (Beobachten): silent cancel (lora_functions.cpp:1446/:1463) and no cancel counter in `[MC-STAT]`.
- Design observation (Beobachten, same as OE3 rca_own_beacons): own prio-4/5 entries at the head get the 5.5 s hold re-armed
  by every received frame while relays of the same priority escalate (txring_functions.cpp:385-405,
  csma_timing.h:99-111). No loss at this load.
