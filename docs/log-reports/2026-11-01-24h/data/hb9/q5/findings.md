# F5 - {CET} time signal in the HB9 mesh (HB9VQQ-4, HB9VQQ-5)

Verdicts: OK / Beobachten / Handlungsbedarf. "Fact" = read from the log; "Inference" = derived and labelled.
All numbers were re-measured with `verify/hb9_net_checks/hb9_net_checks.py` (section name in brackets; `out_all.json`
holds the full output); `q5/results.json` served only as a cross-check. Code read at HEAD 651e0920; the field build is
unknown on both nodes (no boot block). Logs: `logs/20261101-24h-log/.logreport-work/norm/hb9vqq4.log`
(362785 lines, 31.10. 09:39:56 -> 01.11. 09:39:56) and `hb9vqq5.log` (242548 lines, 31.10. 10:03:16 -> 01.11. 10:03:16).
Timestamps are logger time (CET, UTC+1).

## 0. Data basis and what HB9VQQ-5 can and cannot give

- Fact: HB9VQQ-4 has `--setlog` + `--loradebug`; HB9VQQ-5 has loradebug only. Both print the `[CET];gw_rx|rx|tx` lines
  (src/lora_functions.cpp:2988, src/esp32/udp_frame_esp32.cpp:293), so the time-signal question is fully answerable for both.
  `q5/results.json` marks HB9VQQ-5 "unavailable" (age, copies, TX wait) because it looks for `[LOG]` lines; the values are
  recovered here from `[CET];*`, `MH-LoRa`, `TX-LoRa`, `RING_*`, `RX-UDP` and `TX-UDP` lines.
- Fact: no `[NBR]` line in either log (0 of 362785 / 242548). `nbrLog` is NULL unless `--nbrdebug` is on
  (src/lora_functions.cpp:694): NEED/CANCEL/REFUSE markers are absent by configuration, not by behaviour.
- Fact: HB9VQQ-5 loses ~5 s of console every hour: 27 `connection lost (vom Node geschlossen)` events (24 at hh:00:01-02, 3 at
  21:37:53 / 21:38:14 / 21:38:38), each preceded by `[CON ]...s_password:<***>` (hb9vqq5.log:9557-9558 at 11:00:01). Inference: another
  console client connects at the full hour and takes the single session. Cost: ~140 s of 86400 s (0.16 %); 2 of 21 unaccounted ring writes
  (F6 ledger) sit next to such a gap.
- Fact [clock]: `TimeClient now (UTC)` lines (96 per node, every 15 min, floor seconds): logger minus node 1.05-2.09 s (-4, p50 1.59) and
  0.91-2.02 s (-5, p50 1.60); no step over 24 h. True offset about 1.1 s +- 0.5 s for both; the two nodes agree within 0.1 s. Inference: the Mac
  logger runs ~1 s ahead of node NTP (same sign as the OE3 nodes, -1 s). Cross-node timing (-4 vs -5) is good to ~0.2 s.
- Not measurable at HB9VQQ-5 (and why): the `q=` reason of non-relayed frames (503 of 2303 accepted frames were not queued; hop0 / gwcap /
  ping / loop cannot be separated without `[LOG] RLY`); ACK receptions (12-byte ACK frames print no `OnRxDone`/`MH-LoRa` line and `[LOG] 012 A`
  is absent); the `OWN:e` echo flag (replaced by "own call inside the path"); `[LOG] STAT` (replaced by `CHANNEL_UTIL` and `[MC-STAT]`
  drop lines, all 0); `GWI/GWU` (replaced by `RX-UDP`, `TX-UDP`, `RING_WRITE src=udp_rx`). RSSI/SNR per frame come from the
  `[LoRa]...Received packet` line before `MH-LoRa`.

## 1. Which server SSIDs reach this mesh?

| Server    | Route into the HB9 mesh                                                                      | Distinct telegrams (24 h, both logs) | At 600-607 s cadence | Verdict    |
| --------- | -------------------------------------------------------------------------------------------- | -----------------------------------: | -------------------: | ---------- |
| OE1XAR-33 | UDP from the Internet server to both gateways (`[CET];gw_rx;src;OE1XAR-33`)                  |    146 (142 pushed at -4, 143 at -5) |             ~142-144 | OK         |
| OE1XAR-63 | RF only, injected by a foreign gateway (DO1FE-19 in 93 % of the telegrams, DL1GWN-12 in two) |                                  121 |          ~142 (85 %) | Beobachten |
| OE1XAR-28 | RF only, injected by HB9CMI-40 / HB9CF-40 / DO1FE-19                                         |                                   65 |           144 (45 %) | Beobachten |
| OE1XAR-62 | never seen (the string does not occur in either log)                                         |                                    0 |                    - | neutral    |

- Fact [cet]: only -33 arrives over UDP (`rx_udp_cet_src` = -33 only: 142 / 143 frames). -63 and -28 arrive only as LoRa frames:
  `[CET];rx;src;OE1XAR-63` 85 / 115 and `OE1XAR-28` 63 / 28 (-4 / -5). 326 distinct telegrams (146 + 121 + 65 content times) were on air in 24 h.
  Server-origin frames carry `HW:00 ... FW:00:#`.
- Fact: `{CET}` content is UTC although labelled "CET" (hb9vqq4.log:1487-1489, 09:44:21.588: `RX-UDP ... {CET}2026-10-31 08:44:20` while the node
  prefix reads 09:44:20 local). Same as OE3.
- Fact: -63 and -28 share one msg_id counter family (`6ADB5B69..6ADB5BFD` for -63, `6ADB5BED..6ADB5C79` for -28; offset ~132 ids = ~21 h). Six ids occur for
  both servers, always 75843-76438 s apart (e.g. `6ADB5BED`: -28 content 31.10. 09:15:03 UTC, -63 content 01.11. 06:29:01). Verdict OK: far outside the
  ~4 h uniqueness window and the dedup rings (42-55 min). Beobachten only because correctness relies on the offset staying > 4 h. -33 uses a separate
  range (`6AE58942..6AE589DB`).
- Artefact: `q5/results.json` `tx_age_gt150_n = 1` / `tx_age_s_logger_based.max = 76486.9 s` is this id reuse (the extractor joined a -63 TX with the -28 content
  of 21 h earlier). The node-based `[CET];tx age_s` maximum is 56 s (-4) / 40 s (-5).

## 2. Cadence

- Fact [cet]: UDP push of OE1XAR-33 at the gateways: p50 606.9 s, range 603.96-610.63 s (-4, n=141 gaps) and 603.96-610.62 s (-5, n=142). No gap above 611 s,
  none above 900 s: no server outage inside the captures. Content-time gap of -33 telegrams 606-609 s (p50 607 s). -63: 606 s between consecutive heard
  telegrams, otherwise multiples (p90 1213 s = telegrams not heard); -28: 600 s base cadence (p50 gap 1201 s = every second telegram unheard).
- Fact: both gateways receive each -33 push at the same moment: arrival difference -4 minus -5 p50 +0.02 s, p5 -0.06 s, p90 +0.12 s, max 0.88 s (140 telegrams on
  both; 2 only at -4, 3 only at -5 = the 24 min start offset of the captures).
- Fact [cetrad]: gap between the first radiations of consecutive telegrams (all servers, common window, n=314): p50 207.6 s, p90 600.3 s, max 611.1 s; 33 gaps
  > 600 s. An RF-only listener in reach of the pair never waited longer than ~11 min for a time signal, and that bound comes from OE1XAR-33 alone.
  > Verdict OK.

## 3. Radiations per telegram (what the pair can see; lower bound)

[cetrad], union of both logs; a radiation = distinct (msg_id, transmitting call):

| Server    | Telegrams | Radiations p50 / p90 / max | Gateways p50 / max | Relays p50 / max | Airtime per telegram p50 / max |                                Total airtime |
| --------- | --------: | -------------------------- | ------------------ | ---------------- | ------------------------------ | -------------------------------------------: |
| OE1XAR-33 |       146 | 8 / 10 / 12                | 4 / 6              | 4 / 6            | 6.1 s / 9.4 s                  |                                        848 s |
| OE1XAR-63 |       115 | 3 / 5 / 7                  | 1 / 1              | 2 / 6            | 2.4 s / 6.0 s                  |                                        299 s |
| OE1XAR-28 |        65 | 2 / 4 / 7                  | 0 / 1              | 2 / 6            | 1.7 s / 6.1 s                  |                                        126 s |
| all       |       326 | 1612 radiations known      |                    |                  |                                | 1272 s (1.5 % of a channel-day, lower bound) |

(-63: 115 of the 121 telegrams had a parsable radiation record in this table; airtime = SF11 / BW250 / CR4/6 time on air from the frame length, model
reproduces the node's own `CHANNEL_UTIL tx=` sums within 0.5 %: 2259 s vs 2270.5 s at -4, 1876 s vs 1884.9 s at -5.)

- Fact: -33 gateway presence per telegram: HB9VQQ-4 98.6 %, HB9VQQ-5 95.2 %, DB0SAC-12 69.9 %, HB9VQQ-11 58.2 %, DB0HRH-12 47.9 %, HB9HNO-49 32.2 % (stale, see 5).
  So each -33 telegram is injected by ~4 gateways within seconds; relays: HB9VQQ-2 88 %, HB9VQQ-3 62 %, HB9TVJ-12 58 %, DB0TOD-12 45 %.
- Fact: the two nodes contribute 426 of the 1612 known radiations (26 %) and 322 s of 1272 s airtime (25 %). HB9VQQ-4: 207 CET TX (142 gateway + 65 relay) = 8.0 %
  of its TX count, 159 s = 7.0 % of its TX airtime. HB9VQQ-5: 219 CET TX (139 gateway + 79 relay + 1 unassigned) = 10.0 %, 163 s = 8.7 %. CET is 6.8 % of the decoded
  RX airtime at -4 (q5 rawpass).
- Fact [redundancy, cetrad]: both nodes radiate the same -33 telegram in 138 of 146 telegrams; first radiator -5 73x, -4 65x; gap p50 4.2 s, p90 8.2 s, max 16.9 s.
  In 172 CET id pairs (136 gateway/gateway, 35 relay/relay) the later node had already decoded the earlier node's copy in 129 (75 %). The later copies cost 128.5 s =
  40 % of the pair's CET TX airtime (upper bound for the avoidable part: the nodes share only 5 of 16 / 8 direct neighbours, so the second copy is not worthless).
- Inference: no overhear/cancel acted on the gateway slots (253 / 253 udp_rx slots sent at -4; 248 sent, 4 superseded, 1 at a console gap at -5). Fits the OE3 "sole
  provider, case A" finding (CORE 5; `nbrServerNeed`, src/gw_udp_ring.h:44-48): the alone set is never empty for a server slot. Not provable (no `[NBR]` lines).
  Verdict Beobachten, design question, not a defect of this pair.
- Fact: supersede keeps the pool tidy: a newer queued {CET} replaces an older one regardless of server (src/txring_functions.cpp:882-910): -4 19 replaced + 3
  kept-old (`RING_SUPERSEDE_KEEP`); -5 25 replaced + 22 kept-old, 21 of them discarding a -63 relay in favour of the queued -33 inject (so -5 relays fewer -63
  telegrams, 79 relay TX, than it hears). Effect: positive (no doubled or stale time signal in the queue); 0 `RING_DROP_STALE` at both nodes.

## 4. Age on air and the 150 s gate

- Fact [cet]: own radiations `[CET];tx;age_s`: -4 p50 10 s, p90 31 s, p99 48 s, max 56 s (n=207); -5 p50 9 s, p90 18 s, p99 35 s, max 40 s (n=219).
  0 of 426 above 150 s (src/cet_age.h:16, CET_MAX_AGE_S); margin >= 94 s. Queue wait (`wait_ms`, prio 3 throughout): p50 8.9 / 8.6 s, p90 11.3 / 11.6 s, max 25.2 / 27.2 s.
  Gateway slots (253 / 248) wait p50 9.05 / 8.68 s, p90 11.3 / 11.6 s: the same ~9 s floor as OE3, so an injected telegram is ~9 s old on air even in an idle channel.
- Fact: server push age at the node (`[CET];gw_rx age_s`): -4 {-1: 56, 0: 81, 1: 2, 2: 1, 3: 2}, -5 {-1: 66, 0: 73, 1: 1, 2: 1, 3: 2}. The 39 % / 46 % of "-1" are the ~1 s node
  clock lag plus floor seconds; harmless: the gate rejects only |age| > 150 s (src/cet_age.h:133).
- Fact [cet]: age of every heard RF copy (content time vs logger-UTC), HB9HNO-49 excluded: -4 p50 17.6 s, p90 38.2 s, p99 70.8 s, max 140.1 s (hb9vqq4.log:197067, 22:18:27,
  OE1XAR-28 via DB0TOD-12 / DB0HRH-12, 10 s margin to the gate); -5 p50 11.2 s, p90 26.3 s, max 58.1 s. Only 2 of 1052 copies at -4 exceed 100 s. Verdict OK.
- Fact: gate activity: 0 `[CET];drop` lines, 0 `RING_DROP_STALE`, 0 `RLY q=cet` at both nodes. Nothing stale was new for them (section 5), so the gate had nothing to do.
  One stale {CET} was accepted as NEW at -4 (age 1492 s, hb9vqq4.log:313926, 01.11. 06:22:57, -63 via HB9HNO-49, H00): not relayed (`q=hop0`), and `trusted;1`
  means the node has its own clock source, so it cannot be moved by the telegram (D2 rule, src/cet_age.h:77-82). Verdict OK.

## 5. Stale relays on air

- Fact [cet, stale]: the only transmitter that radiates stale {CET} is HB9HNO-49: 46 distinct telegrams at -4, 50 at -5 (53 in the union), all aged 975-1993 s (p50 1511 s =
  25 min), 100 % of its {CET} radiations; 47 are -33 injects (path `OE1XAR-33,HB9HNO-49`, H02), 6 are -63 re-radiations (H00). It carries 32 % of the -33 telegrams.
  Example hb9vqq4.log:40660 (12:20:39.307): content 10:55:54 UTC, age 1485 s.
- Fact: it is not specific to {CET}: every duplicate HB9HNO-49 sent (363 of 363 at -4, 355 of 355 at -5) arrived > 300 s after the first copy of the same id (median 1455 s /
  1452 s) for all types. 448 / 495 frames a day, 391 s / 430 s RX airtime = 3.0 % / 11.4 % of decoded RX airtime. Its own HEY frames carry `FW:35:h` (hb9vqq5.log:5360,
  10:31:45.031: `HB9HNO-49>HG@R ... HW:43 ... FW:35:h`). Inference: 4.35h build without the TX-ring ageing and the {CET} gate (gate is 4.40a.10.23+); the near-constant ~24 min
  delay points to a long backlog in its TX queue. Operator side (contact HB9HNO-49); not a defect of the two nodes.
- Effect on the pair: negative-small. The dedup rings turn over in 42.5 min (-4, p50, range 33.5-52.2) and 54.5 min (-5, 40.3-76.1) [dedup], longer than the 16-37 min
  lag, so almost all copies end as DUP. -5 accepted 24 + 19 late bursts as NEW (it had missed the original) and relayed them: 23 copies from HB9VQQ-5 reached -4 more than
  300 s after the first (median 1488 s; e.g. 31.10. 17:07:09 `4EBC7190`, path `...,HB9HNO-49,HB9VQQ-5`). Inference: nodes with a shorter ring (OE3 rings 17.6-26.4 min,
  others unknown) would re-accept and re-relay HB9HNO-49's frames.
- Fact: the other relays are fresh: DB0TOD-12, HB9VQQ-2, HB9TVJ-12, DB0SAC-12 carry {CET} at 8-70 s age (p99 70.8 s).

## 6. Clock offsets

- Fact: node vs logger ~ +1.1 s (section 0); node vs server content 0..1 s (age_s above); node vs node 0.0-0.2 s. No clock step in 24 h. Verdict OK. Nothing comparable
  to the DB0AIS +1 h step.

## 7. Verdict per node

| Question                       | HB9VQQ-4                                              | HB9VQQ-5                                                            |
| ------------------------------ | ----------------------------------------------------- | ------------------------------------------------------------------- |
| Server SSIDs reaching the node | -33 via UDP (142), -63 / -28 via RF (85 / 63)         | -33 via UDP (143), -63 / -28 via RF (115 / 28)                      |
| UDP stream cadence             | OK (604-611 s, no gap)                                | OK (604-611 s, no gap)                                              |
| Own radiations / age on air    | OK: 207 TX, p90 31 s, max 56 s                        | OK: 219 TX, p90 18 s, max 40 s                                      |
| Gate drops                     | OK: 0 (nothing stale was new)                         | OK: 0                                                               |
| Redundancy with the other node | Beobachten: 136 of 142 gateway TX also radiated by -5 | Beobachten: same pairs                                              |
| Stale relays seen              | Beobachten: HB9HNO-49, 46 telegrams (operator side)   | Beobachten: HB9HNO-49, 50 telegrams; 43 late bursts accepted as new |

## 8. Suspected firmware defects from this question

None confirmed. Weak candidates:

1. Gateway inject slots are not cancelled by an overheard copy although the pair hears each other (75 % of CET pairs): design gap shared with CORE finding 5
   (src/gw_udp_ring.h:44-48, src/nbr_matrix.cpp:1777-1852). Needs `--nbrdebug on` on both nodes for proof.
2. The ~9 s CSMA hold in front of every idle-channel gateway TX is the dominant part of the on-air age (wait p50 9.05 / 8.68 s of age p50 10 / 9 s). Latency floor, not a defect.

## 9. Check index

`python3 -I verify/hb9_net_checks/hb9_net_checks.py <norm_dir> clock cet cetrad stale dedup redundancy` -> sections 0-6 above.
