# Asymmetric nodes: measurements for the wave-5 TLA+ model (2026-11-27)

Scope: FORMAL-02 wave 5 input (`docs/campaign-formal-02-20261127.md`, section "Wave 5 input: asymmetric nodes").
Question: what do loud-but-deaf or noisy-receiver nodes (DB0TAW-14 and the 1-2 W PA cases) do to relay order,
duplicates and ACK delivery, and which firmware rule limits it. Numbers are measured unless marked **(est.)**.

## BLUF

- DB0TAW-14 is a super node today. It sent 198 TX/h against DB0TAW-13's 45 TX/h on the same tower with the same
  hardware and build, while hearing about the same number of frames (936 vs 1025 RX/h). Its relay delay is a median
  10.9 s; DB0TAW-13 needs 345 s. Cause in the logs: DB0TAW-14 runs `--nbrrelay on`, classes 99.997 % of its relays as
  case A (`relays A 67740 B 2`) and waits 150 ms after a foreign frame, where DB0TAW-13 (`NBRRELAY off`) waits 3.5 to 5 s.
- On the DB0AIS side DB0TAW-14 is in the first-arriving copy of 96 % of the frames it appears in (14 days) against 40 to
  70 % for the other big relays. Survivor bias applies (see Q3), but the direction matches the TX counts.
- Nothing in the relay path looks at the node's own receive quality. No RSSI or SNR gate, no hop- or SNR-based delay.
  A loud-but-deaf node relays as fast as any other (Q6).
- DB0TAW-14's receiver is noisy and lossy: noise level (RSSI minus SNR) median -104 dBm; 25.2 % of receive events are
  CRC errors (DB0TAW-13: 14.9 %); 63 % of its CRC errors happen at SNR of -9 dB or better, so loss is not only a weak-signal
  effect.
- The "already relayed" cancel never fires: `cancelled 0` against 67740 case-A relays. 24.1 % of DB0TAW-14's relays
  were sent after it had already heard another node's copy of the same frame while its own was queued.

## Sources and method

| Source                                                                                                      | Content used                                                                                                                    |
| ----------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `logs/20261103-dg0opk/db0taw-14_{1nov2026_2,_3,2nov2026_1..6}` (8 PuTTY sessions)                           | DB0TAW-14 debug console (MC-DBG lines: RX with RSSI/SNR, CRC_ERROR, RELAY_QUEUED, RING_TX_READ lat, TX_DONE, CAD, CHANNEL_UTIL) |
| `logs/20261103-dg0opk/db0taw-13_{1nov2026_2..4,2nov2026_1..6}` (9 sessions)                                 | same for DB0TAW-13                                                                                                              |
| `logs/20261116-db0ais-14d/healthy/db0ais.log` (14 days, 110455 parsed `[LOG] ... DUP:` lines, 52238 frames) | DB0AIS receive view, path, RSSI/SNR, DUP:n/d, ms clock                                                                          |
| `logs/20261101-24h-log/DB0AIS 24 Stnuden.log` (6410 lines, 3632 frames)                                     | replication of the DB0AIS 14-day result                                                                                         |
| `logs/20261101-24h-log/HB9VQQ-4_loradebug_20261031-0939.log` (14449 lines)                                  | HB9VQQ-4 receive view (HB9ZG-49, DO1FE-19)                                                                                      |
| `logs/dg0opk/20261018-DG0OPK/DB0TAW-14 - Meshcom-mh.htm`, `_liste.htm`, `_matrix.htm`                       | DB0TAW-14's own heard / path / neighbour tables, 2026-10-18 19:24                                                               |
| `logs/dg0opk/lora_logfiles_dg0opk_*`, `logs/20261019-dg0opk/`                                               | no DB0TAW-14 line (grep -c = 0); only the `.htm` exports above                                                                  |

- Parsers: `/tmp/formal02/taw/parse_taw.py` (MC-DBG console format), `ais.py`, `ais_an*.py`, `hb9.py`, `an1..an4.py`
  (scratch, stdlib only). The report generators under `docs/log-reports/*/generators/` parse the `[LOG]` line of the
  DB0AIS format only for ring statistics, not RX paths with RSSI/SNR, and nothing in the MC-DBG console format, so the
  parsers above are new; the `[LOG]` field layout follows `db0ais_14d_measure.py`.
- Wall clock: from the last `HH:MM:SS` line, 1 s resolution. Relay delay is the `lat=` of `RING_TX_READ`
  (ring write to TX start); it equals the RX-to-TX delta of the `ts=` clock within 0.35 s (checked on 44 ACKs).
- Windows differ: DB0TAW-14 11.3 h of receive span, DB0TAW-13 16.8 h (1 to 2 Nov 2026). Rates are per hour.
- The two towers run the same build (4.40a, Oct 2 2026) on Heltec Stick V3, but not the same configuration:
  DB0TAW-14 `NBRRELAY on`; DB0TAW-13 `NBRRELAY off` and gateway into the HAMNET (`udp_rx` ring writes). Antennas per
  `logs/20261103-dg0opk/mail.txt`: DB0TAW-13 X510 vertical, about 600 mW, north-east; DB0TAW-14 LPDA 9 dBi, about 800 mW,
  west. The comparison is therefore configuration and antenna, not hardware.

## Q1. DB0TAW-14 hearing

Receive events, all 8 sessions: 10574 decoded, 3571 CRC errors (25.2 %). DB0TAW-13: 17256 decoded, 3014 CRC errors
(14.9 %). Median RSSI -112 / SNR -7 dB. Noise level = RSSI minus SNR per frame.

| Quantity                         | DB0TAW-14           | DB0TAW-13                          |
| -------------------------------- | ------------------- | ---------------------------------- |
| noise level, median (RSSI - SNR) | -104 dBm            | -101 dBm                           |
| noise level p10 / p90            | -105 / -98          | -103 / -43 (strong sibling frames) |
| CRC share of receive events      | 25.2 %              | 14.9 %                             |
| CRC errors with SNR >= -9 dB     | 63 % (2251 of 3571) | 54 % (1613 of 3014)                |
| headerless frames (ACK type)     | 1161                | 1886                               |

CRC share per SNR bin of the frame (CRC / (decoded + CRC)):

| SNR bin    | DB0TAW-14 | DB0TAW-13 |
| ---------- | --------- | --------- |
| > 0        | 4.9 %     | 1.0 %     |
| -4 .. 0    | 28.3 %    | 21.2 %    |
| -9 .. -5   | 27.0 %    | 13.9 %    |
| -14 .. -10 | 31.8 %    | 26.2 %    |
| <= -18     | 47.5 %    | 38.9 %    |

Top 20 senders heard by DB0TAW-14 (last path element = the station transmitting the copy; `dup %` = share of copies
that were duplicates for DB0TAW-14). Noise is not meaningful where SNR is at the +5/+6 dB ceiling (DB0KH-11,
DB0TAW-13).

| Sender    | n    | RSSI med | SNR med | SNR p10 | noise med | dup % |
| --------- | ---- | -------- | ------- | ------- | --------- | ----- |
| DB0HRF-12 | 1000 | -103     | 0       | 0       | -103      | 48    |
| DL9GX-12  | 975  | -105     | -1      | -4      | -104      | 68    |
| DB0KH-11  | 956  | -92      | 6       | 5       | (-98)     | 63    |
| DB0NH-3   | 746  | -114     | -9      | -9      | -105      | 51    |
| DB0PET-15 | 649  | -112     | -8      | -8      | -105      | 81    |
| DB8ZP-99  | 626  | -103     | -2      | -3      | -102      | 95    |
| DK3RW-12  | 579  | -105     | 0       | -2      | -104      | 68    |
| DM9PT-20  | 474  | -119     | -14     | -17     | -105      | 50    |
| DB0RVB-99 | 457  | -119     | -14     | -15     | -105      | 21    |
| DB0FTS-1  | 395  | -112     | -8      | -9      | -105      | 62    |
| DL1ALP-1  | 370  | -121     | -16     | -17     | -105      | 74    |
| DB0DOL-1  | 366  | -113     | -9      | -9      | -105      | 72    |
| DL2IAS-10 | 320  | -122     | -17     | -19     | -105      | 55    |
| DB0TAW-13 | 314  | -39      | 6       | 5       | (-45)     | 73    |
| DK9MS-12  | 223  | -121     | -16     | -17     | -105      | 85    |
| DG1FBP-12 | 187  | -120     | -15     | -18     | -105      | 59    |
| DB0KT-15  | 187  | -112     | -8      | -9      | -105      | 67    |
| DG0OPK-14 | 140  | -116     | -10     | -11     | -105      | 83    |
| DB0IBH-90 | 130  | -123     | -18     | -19     | -105      | 22    |
| DG0OPK-11 | 81   | -120     | -14     | -16     | -105      | 81    |

Reading: the implied noise is flat at -102 to -105 dBm for every sender (the receiver, not the senders, sets it),
about 12 to 15 dB above a quiet site (matches the campaign doc's -103 dBm). 34 senders in total. The mirror image:
DB0TAW-13 hears DB0TAW-14 2774 times (median -37 dBm) while DB0TAW-14 hears DB0TAW-13 314 times, a 9 : 1 ratio that
follows the TX ratio.

Co-location (DB0TAW-13 vs DB0TAW-14, matched by msg id and `last hop`, +-10 s, only inside the other node's log coverage;
upper bound on loss because log gaps and headerless frames are not matched):

| Direction                       | TX with path | decoded by the other tower | share |
| ------------------------------- | ------------ | -------------------------- | ----- |
| DB0TAW-13 TX heard by DB0TAW-14 | 272          | 220                        | 81 %  |
| DB0TAW-14 TX heard by DB0TAW-13 | 1514         | 1440                       | 95 %  |

CRC errors at sibling level (RSSI > -60 dBm): 51 at each tower (1.4 % of DB0TAW-14's CRC errors). The co-location blocks
by half-duplex (the receiver of one tower is busy while the other sends), not mainly by corrupted frames. CAD found the
channel busy before 30 % of DB0TAW-14's TX and 42 % of DB0TAW-13's. CHANNEL_UTIL (10 s window): median 38 % (p90 54 %) for
DB0TAW-14, 32 % (p90 50 %) for DB0TAW-13.

## Q2. DB0TAW-14 relaying

| Quantity                                                            | DB0TAW-14                                                     | DB0TAW-13                         |
| ------------------------------------------------------------------- | ------------------------------------------------------------- | --------------------------------- |
| receive rate                                                        | 936 /h                                                        | 1025 /h                           |
| relays queued (`RELAY_QUEUED`)                                      | 2603                                                          | 3858                              |
| TX total / per hour                                                 | 2244 / 198.5                                                  | 759 / 45.1                        |
| TX of relays (`rx_relay`)                                           | 2049                                                          | 326                               |
| queued relays later sent                                            | 79 % (2059)                                                   | 8 % (326)                         |
| TX / RX                                                             | 0.21                                                          | 0.044                             |
| airtime duty, mean `tx=` per 10 s window **(est.)**                 | 5.4 %                                                         | 0.9 %                             |
| relay delay p10 / median / p90 / p99 / max                          | 8.4 / 10.9 / 138.9 / 339 / 704 s                              | 67.5 / 345 / 1731 / 2602 / 5105 s |
| relays sent within 15 s of queueing                                 | 69 %                                                          | 1 %                               |
| relays waiting more than 60 s                                       | 19 %                                                          | 91 %                              |
| gap between two TX, median                                          | 10.9 s                                                        | 51.7 s                            |
| hold after a foreign frame (`RX_TIMEOUT_FIRE wait`, session 2nov_6) | 52 % under 200 ms, 17 % under 1 s, 18 % 3-4 s                 | 4 % under 200 ms, 95 % 3-5 s      |
| ring full (`qmax=20/20`, drops `RING_DROP_NEW`, session 2nov_6)     | 85 drops (+97 stale, 56 prio)                                 | 438 drops (+112 prio)             |
| `--info` counters                                                   | NBRRELAY on, relays A 67740 / B 2, cancelled 0, refused 13376 | NBRRELAY off, all 0               |

Sources: `db0taw-14_2nov2026_6` line `...NBRRELAY on ...relays A 67740 B 2 ...cancelled 0 ...possible 0 ...refused 13376`
(same in all 8 sessions; A rises 62542 -> 67740, B stays 2); `db0taw-13_2nov2026_6` `NBRRELAY off`.

Reading:

- The 150 ms hold is `NBR_RELAY_CASE_A_SHORT_MS` (`src/configuration_global.h:622`): after 8 s waiting
  (`NBR_RELAY_CASE_A_MAX_WAIT_MS`, `:621`) a case-A relay only waits 150 ms plus jitter after a foreign frame. 220 ms
  also appears (150 + 2 slots of 35 ms). DB0TAW-13 re-arms 3.5 to 5 s after every foreign frame and hears one every
  3.6 s, so its ring (20 slots) stays full and relays go stale.
- DB0TAW-14's own tables think it is almost always alone: `Meshcom_matrix.htm` (2026-10-18) lists 17 stations as
  "Exclusive to me (only I hear them directly)", `Meshcom-mh.htm` shows "Hears me: not reported" for all 24 stations of
  the 3 h list, `Meshcom_liste.htm` shows "Via / Covered by: only me" for most one-hop stations. Not verified against the
  matrix code; this is the likely source of "case A for everything".

Duplicates and ordering (DB0TAW-14, own log, ms clock; 2049 relays with a queue record):

| Check                                                                              | Result                                                                                                                  |
| ---------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| relay sent although a copy of the same id from another node was heard while queued | 493 of 2049 (24.1 %); median 8.9 s before own TX, p90 166 s                                                             |
| of these, the copy came from DB0TAW-13                                             | 5                                                                                                                       |
| same, by SNR of the first-heard copy: >= -5 / -10..-5 / -15..-10 / < -15           | 257/818 (31 %), 100/493 (20 %), 64/413 (15.5 %), 72/325 (22 %)                                                          |
| relays whose first-heard copy had SNR < -15 dB                                     | 325 of 2049 (16 %); median delay 11.0 s (same as strong frames)                                                         |
| relays queued from frames heard at RSSI <= -120 dBm                                | 534 of 2603 (21 %)                                                                                                      |
| copies of the same id heard within 120 s after own TX                              | 3327 copies after 1364 relays; 2270 carry DB0TAW-14 in the path (neighbours re-relaying), 1057 do not (parallel relays) |
| relays of frames first heard from DB0TAW-13 (-40 dBm)                              | 55                                                                                                                      |
| own relay TX whose path already contains DB0TAW-13                                 | 58 of 2049; DB0TAW-13's relay TX containing DB0TAW-14: 50 of 326                                                        |
| ids relayed by both towers                                                         | 159; DB0TAW-14 first in 153, DB0TAW-13 first in 6; median gap 349 s (1 s resolution)                                    |

The firmware's own cancel (`src/lora_functions.cpp:1345-1470`) needs `--nbrrelay on` and only acts on case-B slots
(`nbrMaskEmpty(ringAlone[...])`, `:1440`); case-A slots are never cancelled by a foreign copy. DB0TAW-14 relays are
case A, so `cancelled 0` is expected, not a bug in the counter. No min-SNR gate: frames at SNR -17 are relayed
(`msg_id 682029A1` from DB0AIS-12,DL2IAS-10 at -121 dBm / -17 dB in `db0taw-14_2nov2026_6`).

## Q3. Who hears DB0TAW-14

DB0AIS, 14 days (`db0ais.log`), 52238 frames, 110455 copies; replication in `DB0AIS 24 Stnuden.log` (3632 frames).

| Quantity                                                                              | 14 days                                                                        | 24 h                   |
| ------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ | ---------------------- |
| copies where DB0TAW-14 is the last hop (heard directly by DB0AIS)                     | 17                                                                             | 1                      |
| RSSI median / SNR median of those                                                     | -105 dBm / -19 dB (p10..p90 RSSI -106..-77; SNR -20..-18)                      | -107 / -19             |
| copies with DB0TAW-14 anywhere after the origin                                       | 8191 (5840 frames)                                                             | 400 frames             |
| DB0TAW-14 in the first-arriving copy of the frame                                     | 5578 = 96 %                                                                    | 382 = 96 %             |
| DB0TAW-14 only in later copies                                                        | 262 (4 %); median 45.9 s after the first copy, p90 258 s, 61 % later than 30 s | 18; median 42.8 s      |
| copies per frame in which it appears                                                  | 1.40                                                                           | 1.23                   |
| for comparison, in first copy: DB0HRF-12 / DB0MW-12 / DB0KT-15 / DM9PT-20 / DL0TRZ-20 | 70 / 57 / 43 / 40 / 50 %                                                       | 73 / 60 / - / 39 / - % |
| all copies later than 30 s after the first copy (any relay)                           | 26980 of 110455 (24.4 %)                                                       | 780 of 6410 (12.2 %)   |
| DB0TAW-13 vs DB0TAW-14 in paths                                                       | 482 vs 8191 copies; both in one path 543 (536 times 13 then 14)                |                        |

Reading: DB0AIS practically never hears DB0TAW-14 directly; it sees it as a relay inside paths
(top last hops carrying it: DB0HRF-12 3329, DB0KT-15 1578, DB0MW-12 1338, DM9PT-20 1154). The 96 % is biased upwards:
a late DB0TAW-14 relay is usually killed by the downstream nodes' dedup and never reaches DB0AIS, so it shows only in the
DB0TAW-14 log (Q2: 1364 relays met a parallel copy). Treat 96 % as "when its relay is useful at all it is mostly the
first", not as "it wins 96 % of all races". DB0TAW-14 is the first relay after the origin in the commonest path shapes
(position 1 of 3: 2120 copies). Origins most often relayed by it: DB0PET-15 524, DK3RW-12 476, DG7FDL-12 303.

Late copies that cost airtime at the 14-day scale: the 262 late DB0TAW-14 copies are small next to DB0HRF-12 (8221 late
copies, 67 % later than 30 s) and DB0KT-15 (8592, 80 %); on this metric DB0TAW-14 is not the main source of late
duplicates seen at DB0AIS. Its cost sits near the tower (Q2), not at DB0AIS.

DB0TAW-14's own tables, 2026-10-18 (`Meshcom-mh.htm`, 24 stations in 3 h): median RSSI -119.5 dBm, median SNR -14.5 dB,
12 of 24 at SNR <= -15 dB, "Hears me: not reported" for all 24, relay role Redundant 17 / Needed 6 / Super 1
(DB0HRF-12). `DK3RW-12` (its gateway): -104 dBm / -1 dB at DB0TAW-14.

## Q4. ACKs

| Quantity                                                                   | DB0TAW-14                             | DB0TAW-13                |
| -------------------------------------------------------------------------- | ------------------------------------- | ------------------------ |
| headerless (ACK-type, 12-byte) frames received                             | 1156 (270 new, 886 duplicates = 77 %) | 1885 (474 new, 1411 dup) |
| of those from the sibling tower (RSSI > -60)                               | 145                                   | 201                      |
| other headerless frames, median RSSI / SNR                                 | -113 / -8                             | -108 / -6                |
| ACK frames sent (`rx_ack_fwd`, type 41, prio 1)                            | 150                                   | 273                      |
| first heard from mesh / from sibling tower                                 | 129 / 20                              | 175 / 65                 |
| ACK delay, queue to TX: median / p90 / max                                 | 264 / 788 / 1379 s                    | 280 / 611 / 1216 s       |
| ACK delay above 90 s (`RING_TTL_ACK_MS`, `src/configuration_global.h:513`) | 77 %                                  | 76 %                     |
| TX with `retry > 0` / `retrying != 0` in `RING_STATUS`                     | 0 of 2244 / never                     | 0 of 759                 |

- ACK delay by session, DB0TAW-14 (median): 1 Nov 3 to 5 s (4 ACKs); 2 Nov 68 to 525 s (146 ACKs) while its relays went
  in 10 to 11 s in every session. A prio-1 ACK waiting minutes behind prio-5 relays is the opposite of the intended order;
  likely cause (not verified in code): the ACK is plain `RING_KIND_OTHER` with base 3000 ms plus jitter and is re-armed
  by every foreign frame, while case-A relays only wait 150 ms (Q2). The logs were recorded with a build of 2 Oct 2026;
  the current TTL of 90 s would drop most of these ACKs (stale), not send them late.
- Retries: none visible. Both towers relay only; relays are fire-and-forget (`src/lora_functions.cpp:2597`); DM retries
  live at the originators. The DB0AIS side shows ACK-bearing texts (`:ackNNN`): 811 copies in 14 days, 540 (67 %)
  with DB0TAW-14 in the path, none with it as last hop.

## Q5. HB9ZG-49, DO1FE-19, HB9MM-2

| Station  | Where it appears                                                         | Result                                                                                                                                                                                                   |
| -------- | ------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| HB9ZG-49 | HB9VQQ-4 log (8613 lines), HB9VQQ-5 log (2017), no DB0AIS line           | heard directly by HB9VQQ-4: 192 frames, RSSI -120 / SNR -16 (p10 -19), noise -104 dBm, 93 % DUP:n; as relay in 1058 frames, in the first copy of 98 % (1038), 20 late copies (median 15.1 s, p90 38.3 s) |
| DO1FE-19 | HB9VQQ-4 (2261), HB9VQQ-5 (1889), DB0AIS 14 d (45 lines, 15 path copies) | never the last hop at HB9VQQ-4; as relay in 78 frames, first copy 85 % (66), 12 late (median 8.9 s, p90 18.1 s); at DB0AIS only in the middle of paths                                                   |
| HB9MM-2  | no node log (grep -c = 0 in all DB0TAW, DB0AIS, HB9VQQ, DG0OPK logs)     | only 6 lines in `logs/snr-distance-report-2026-08-25.html`; not analysed. The campaign doc's 7-day link data is the only input.                                                                          |

HB9VQQ-5's log has no per-line RSSI, so only HB9VQQ-4 is parsed. Same pattern as DB0TAW-14: when these nodes relay,
their copy is almost always the first one that reaches the logger (survivor bias as in Q3). Their receive side is not
visible in these logs except through HEY `R` groups, which `docs/log-reports/2026-11-01-24h/verify/hb9zg49.md` already
covers (HB9ZG-49 heard by 24 stations, hears 9; gap 12 to 15 dB on the strong links).

## Q6. Model input

### Parameters the model should take

| Parameter                 | Meaning                                                                                            | Values from the logs                                                                                                                                             |
| ------------------------- | -------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `noise[n]` per node (dBm) | receiver noise level; a link works if `rssi(link) - noise[n] >= snr_min`                           | quiet node -115..-120 (campaign doc), DB0TAW-14 -104, DB0TAW-13 -101, deaf PA nodes -85..-92 (campaign doc), DB0AIS -86..-99 (RSSI minus SNR per last hop, 14 d) |
| `snr_min`                 | decode threshold                                                                                   | -17..-18 dB at the edge (DB0TAW-14: 464 of 10574 decodes at <= -18); CRC errors appear from SNR -4 on                                                            |
| `tx_rssi[a][b]`           | directed link strength; a->b and b->a independent                                                  | DB0TAW-14 <-> DK3RW-12 -104 / -119 dBm (campaign doc); DB0TAW pair -40 both ways                                                                                 |
| `p_crc(snr)` extra loss   | loss not explained by SNR                                                                          | DB0TAW-14: 28 % at -4..0 dB, 27 % at -9..-5; DB0TAW-13: 21 % / 14 % (est.: model as 10..30 % flat for interfered sites)                                          |
| co-located pair `(A,B)`   | each blocks the other's RX while transmitting (half-duplex), both hear each other at about -40 dBm | 81 % / 95 % of the sibling's TX decoded                                                                                                                          |
| relay delay distribution  | queue write to TX start                                                                            | super node: 69 % <= 15 s, median 10.9 s, 19 % > 60 s; starved node: median 345 s, 91 % > 60 s; ring 20 slots                                                     |
| hold after foreign frame  | CSMA wait re-armed per received frame                                                              | 3.0 to 5.5 s base + 0..350 ms jitter (prio base 3000/4500/5500); case A: 3500 + 0..70 ms, 150 ms after 8 s waiting                                               |
| duplicate rate            | relay sent although a copy was already heard while queued                                          | 24 % (DB0TAW-14); 77 % of ACK-type frames heard are duplicates; 55 to 95 % of heard copies per sender are duplicates                                             |
| ring capacity and TTL     | queue limits                                                                                       | 20 slots (`MAX_RING`, `configuration_global.h:338`); TTL ACK 90 s, relay 300 s, position 120 s (`:513-519`)                                                      |
| queue-to-air ratio        | share of queued relays that are sent                                                               | 79 % vs 8 %                                                                                                                                                      |

### Firmware rules that decide who relays first today

| Rule                                                                                                                                                                                                                                                      | Where                                                                                                                                                           | Effect on a deaf-but-loud node                                                                                                                                      |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Relay guard: hop nibble > 0, own call not in path (`RELAY_LOOP_BLOCKED`), path not full                                                                                                                                                                   | `src/lora_functions.cpp:2515`, `:2528`, `:2581`                                                                                                                 | none; no RSSI/SNR condition                                                                                                                                         |
| Queue entry `addTxRingEntry(..., "rx_relay", ...)`, kind RELAY only with NBR on                                                                                                                                                                           | `src/lora_functions.cpp:2623`                                                                                                                                   | none                                                                                                                                                                |
| TX slot choice: lowest priority number first, FIFO by enqueue time within a class                                                                                                                                                                         | `src/txring_functions.cpp:232` (`getNextTxSlot`)                                                                                                                | none                                                                                                                                                                |
| CSMA backoff: priority base (ACK/DM 3000, group 3000, relay 4500, position/HEY 5500 ms) + 0..10 slots x 35 ms, re-armed after every received frame                                                                                                        | `src/csma_timing.h:43`, `src/configuration_global.h:523-534`, `:431`                                                                                            | same for every node; a node that hears fewer frames is re-armed less often and so sends sooner                                                                      |
| Age floor: after 20 s / 60 s waiting the hold shrinks (stage 1 / 2)                                                                                                                                                                                       | `src/configuration_global.h:439-451`, `src/lora_functions.cpp:4096`                                                                                             | not in the 2 Oct build of the logs (added 2026-10-18); would shorten DB0TAW-13's 3.5 to 5 s holds for old entries; effect not measured                              |
| NBR case A / B (only with `--nbrrelay on`, default off): case A (relay covers someone no other neighbour covers) 3500 ms + 0..2 slots, 150 ms after 8 s; case B 20 s one-time hold then 60 s cap; case B is cancelled when a foreign copy covers its need | `src/txring_functions.cpp:404-480`, `src/configuration_global.h:619-625`, `src/lora_functions.cpp:1345-1470`, `:4054`, default `src/loop_functions.cpp:396-397` | decides by the node's neighbour table ("who I hear"), not by receive quality; a node with a thin or unknown table classes itself as alone (case A) and relays first |
| SNR use in NBR: edges below `NBR_SYM_MIN_SNR` (= `LORA_SNR_STABLE_MIN_DB`, -16 dB) are not "symmetric"                                                                                                                                                    | `src/nbr_matrix.h:186`, `src/configuration_default.h:97-98`, `src/nbr_matrix.cpp:1568`                                                                          | filters edges in the cover calculation; no effect on the node's own timing                                                                                          |

State plainly: nothing today keeps a loud-but-deaf node (or a noisy-receiver node like DB0TAW-14) from relaying first.
No rule uses the node's own RSSI or SNR, a receive-quality estimate or a hop count for the delay. The only differentiator
is the NBR case, which depends on the neighbour table, and the field data shows it pushes DB0TAW-14 the wrong way:
case A everywhere, 150 ms holds, 5x the TX rate of the neighbour tower, cancel never used.

### What the model should show

- A node `D` with `noise[D]` 20 to 30 dB above the others and `tx_rssi[D][*]` normal: it hears few frames, so its
  hold is re-armed rarely, its ring is empty and its queue-to-air delay is short; it sends first for every frame it
  does hear, and its copy reaches the others first. In the model, check that first-relay share and duplicate count
  rise with `noise[D]` unless a rule counters it.
- Co-located pair with mutual blocking: one tower node with case-A short holds starves the other (here 45 vs 198 TX/h).
- ACK delivery: prio-1 frames delayed by re-armed holds while case-A relays overtake (Q4); the model should include
  the TTL drop (90 s) and count ACKs lost to it.
- Retries do not occur at relays; model them at the originator only.

## Caveats

- Two sessions per tower on 1 to 2 Nov 2026 only; no log of the other asymmetric nodes' own receivers.
- DB0TAW-13 and DB0TAW-14 differ in configuration (NBR on/off, gateway, antenna); the 4.5x TX ratio is the sum of those.
- Collision share is an upper bound (matching by id and +-10 s, gaps in log coverage).
- Duty cycle 5.4 % / 0.9 % assumes a 10 s CHANNEL_UTIL window (1866 samples in 5.2 h = 10.0 s per sample, `db0taw-14_2nov2026_6`).
- The DB0AIS first-copy share has survivor bias (Q3).
- The log build is 4.40a of 2 Oct 2026; the current `src/` may differ (TTL, age floor and NBR constants are quoted from
  `src/` as of 2026-11-27; the age floor, Brief G of 2026-10-18, is newer than the build). The 150/220 ms holds in the
  DB0TAW-14 log match `NBR_RELAY_CASE_A_SHORT_MS` plus 0 or 2 slots, so the case-A timing was in the build.
