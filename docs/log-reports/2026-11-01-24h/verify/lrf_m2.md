# M2: own-beacon replay for D2 (A1, A2, A3), OE3XOC and OE3XIA

Capture 2026-10-31 06:31 to 11-01 07:5x (25.4 h OC, 25.0 h IA). Data: `data/extra/lrf_m2.json`. Code: `verify/lrf_checks/m2/`.
Constants checked in `src/`: TTL position/WX 120 s (`RING_TTL_POS_MS`), HEY 180 s (`RING_BG_MAX_AGE_MS`);
stage 1 = `clamp(2850 - airtime - 175, 500, 1500)` + 0..5 x 35 ms; own WX/HEY airtime always gives the 1500 ms clamp.

## Definitions

- **Entry:** own prio-4/5 pool entry = WX (type 21, src `user_wx`, 49 per node, no other own position class) or HEY (type 40, src `auto_pos`).
  HEY 136 (OC) / 241 (IA) in scope; 5 per node dropped (first 300 s of the log, or open at log end).
- **Head:** lowest (prio, enqueue time) among alive entries, rebuilt from all `RING_*` lines (as `getNextTxSlot()`; all statuses are DONE).
- **Arm event at head:** end of every decoded frame (`ONRXDONE_TIME`, floor applies) and end of every own TX (`csma_reset`, no floor).
  **Silence** = arm event to start of the next decoded frame (end minus airtime from `RX_FRAME len`, formula fitted to measured TX airtime).
  RX errors (`OnRxError`) do not re-arm the timer in firmware; they enter only through the CAD result.
- **Model:** after hold H a CAD gate (0.12 s) must finish before the next frame starts. `ideal` = CAD always free, first fitting gap wins.
  `cad` = CAD free with the measured P by cad_attempt (OC 0.48/0.45/0.44/0.31, IA 0.44/0.33/0.31/0.21), busy raises the attempt (ladder,
  rapid fire at 3), 200 seeded runs. `worst` = max jitter (variants +175 ms, baseline +350 ms), `mean` = mean jitter.
- **recovered gross** = sum of P(variant airs before TTL) over entries measured as TTL-lost. **net** = same minus the baseline model's own P
  on those entries (removes model false positives). **extra other** = superseded entries that would now air (the successor HEY still arrives).
  **extra airtime** = (recovered + extra other) x measured mean own airtime (WX 937 ms OC / 994 ms IA, HEY 444 / 452 ms).
- **rapid (floor)** = predicted TX at hold 100-275 ms where today's hold is above 275 ms (stage 2 only). **rapid (any)** adds cad_attempt >= 3 rapid fire.

## Commands

```
M=docs/log-reports/2026-11-01-24h/verify/lrf_checks/m2; O=docs/log-reports/2026-11-01-24h/data/extra/lrf_m2.json; N=logs/20261101-24h-log/.logreport-work/norm
python3 -I $M/replay.py $N $O --runs 200; python3 -I $M/relays.py $N $O
python3 -I $M/table.py $O cad/worst       # also: --agree $O, --totals $O
```

## Baseline agreement (the model must reproduce today's outcome)

| node/class | measured aired | pred cad/mean | pred ideal/mean | mean P cad/mean (aired / lost) | TP / FN / FP (cad/mean) |
| ---------- | -------------: | ------------: | --------------: | -----------------------------: | ----------------------- |
| OC WX      |       15 of 49 |          11.3 |            18.0 |                    0.57 / 0.08 | 8.6 / 6.4 / 2.7         |
| OC HEY     |      41 of 136 |          37.6 |            59.0 |                    0.58 / 0.15 | 23.8 / 17.2 / 13.8      |
| IA WX      |       38 of 49 |          31.2 |            41.0 |                    0.73 / 0.30 | 27.9 / 10.1 / 3.3       |
| IA HEY     |     149 of 241 |         144.9 |           189.0 |                    0.76 / 0.34 | 113.3 / 35.7 / 31.5     |

**Moderate agreement.** The model ranks entries correctly (P 0.57-0.76 for aired, 0.08-0.34 for lost) and `cad/mean` is 3-25 % low on counts;
`ideal` over-predicts HEY by 1.3-1.4x (it ignores CAD busy). Timing is weak: ideal/mean hits the real TX time within 1 s for 5/13 (OC WX), 12/34 (OC HEY),
6/35 (IA WX), 26/133 (IA HEY). Variant numbers are therefore indicative (order of +-20 %), not exact; ideal and cad bracket them.

## Variants, cad/worst (both nodes, 25 h)

| node/class | var | recovered gross (net) | extra other | extra airtime s | rapid floor / any |
| ---------- | --- | --------------------: | ----------: | --------------: | ----------------: |
| OC WX      | A1  |           27.7 (25.2) |           0 |            26.0 |       17.7 / 22.6 |
| OC WX      | A2  |           24.8 (22.3) |           0 |            23.2 |         0.0 / 3.2 |
| OC WX      | A3  |           23.2 (20.7) |           0 |            21.7 |         0.0 / 2.9 |
| OC HEY     | A1  |           52.2 (43.0) |        25.9 |            34.7 |       38.7 / 49.0 |
| OC HEY     | A2  |           47.9 (38.7) |        23.9 |            31.9 |         0.0 / 7.7 |
| OC HEY     | A3  |           45.0 (35.8) |        20.9 |            29.3 |         0.0 / 8.0 |
| IA WX      | A1  |            10.2 (7.2) |           0 |            10.1 |       10.5 / 18.4 |
| IA WX      | A2  |             9.8 (6.8) |           0 |             9.7 |         0.0 / 7.2 |
| IA WX      | A3  |             8.5 (5.6) |           0 |             8.4 |         0.0 / 5.9 |
| IA HEY     | A1  |           38.8 (22.0) |        28.9 |            30.6 |       49.2 / 83.7 |
| IA HEY     | A2  |           36.1 (19.3) |        28.1 |            29.0 |        0.0 / 28.9 |
| IA HEY     | A3  |           35.5 (18.7) |        23.2 |            26.5 |        0.0 / 27.1 |

Measured TTL-lost: OC WX 34, OC HEY 57 (+38 superseded), IA WX 11, IA HEY 40 (+52 superseded). Totals over both nodes (`--totals`, cad/worst):

- A1: recovered 128.9, extra other 54.8, extra own airtime 101 s, rapid (floor) 116.1 TX, rapid (any) 173.7.
- A2: recovered 118.6, extra other 52.0, extra own airtime 94 s, rapid (floor) 0.0, rapid (any) 47.0 (existing cad_attempt >= 3 rapid fire).
- A3: recovered 112.2, extra other 44.1, extra own airtime 86 s, rapid (floor) 0.0, rapid (any) 43.9. ideal/worst: A1 134 / A2 130 / A3 123 recovered.
- A2 extra own airtime = 94 s on 6170 s of own TX (+1.5 %; OC 3713 s, IA 2456 s). With each beacon relayed as often as the measured mean relay count (x (1 + count)): about 266 s on air (rough).

## Measured relays per aired own beacon (`relays.py`)

Match: neighbour log has `RING_TX_READ` with the same `msg_id` (relays keep it) within [t-2, t+300] s. 6 usable loggers per origin;
`db0ais` has no timestamps or ring lines. `hb9vqq4/5` start 10-31 09:39/10:03, never relayed an OC/IA beacon (0/12..0/136), `oe3xir` ends 10-31 18:16;
each beacon counts only loggers whose log covers its window. The count is a lower bound for the whole neighbourhood.

| node/class | aired | mean relays | median | zero relays | histogram 0..4        |
| ---------- | ----: | ----------: | -----: | ----------: | --------------------- |
| OC WX      |    15 |        0.87 |      0 |           8 | 8 / 2 / 4 / 1 / 0     |
| OC HEY     |    41 |        1.83 |      2 |           9 | 9 / 6 / 13 / 9 / 4    |
| IA WX      |    38 |        1.95 |      2 |           5 | 5 / 8 / 14 / 6 / 5    |
| IA HEY     |   149 |        2.57 |      3 |           4 | 4 / 23 / 34 / 60 / 28 |

## Caveats

- First-order: other entries do not shift when a variant airs earlier; a variant TX would also occupy the channel and change the later gaps.
  Time behind prio-1..3 blockers gets no benefit (head share of entry life: OC 31-33 %, IA 41-45 %). Held case-B relays are not modelled (all NEED lines are case A).
- RX errors are 22 % (OC, 12239 vs 43723 good) and 32 % (IA, 11080 vs 23542) of all receptions; they only enter through P(CAD free). A2/A3 `rapid (any)` > 0 is the existing cad_attempt >= 3 rapid fire, not the floor.
- Recovered beacons are the hard cases (lost to a busy channel); the net column and the ideal/cad bracket show the sensitivity.

## Findings for the campaign doc

- Section 2.2 says "median gap 3.48 s, most gaps qualify". `rx_gaps.py` counts only `OnRxDone` print lines (16012 of 43723 frames).
  With all frames: end-to-end median 1.42 s (OC), silence median 0.88 s; silence >= 1.8 s in 28.6 %, >= 5.8 s in 4.5 %. At the head: 28 % of arm events allow
  a stage-1 hold, 3 % the baseline hold. A2 still works because an entry sees 17-21 arm events at the head on average (OC HEY: 2912 arm events, 494 head intervals).
- Section 2.2 uses 0.72 s per beacon; measured own airtime is 0.94-0.99 s (WX) and 0.44-0.45 s (HEY).
