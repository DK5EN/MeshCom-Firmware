# RX-01 soak evaluation (DK5EN-98, SX1262), 2026-10-04 11:02 to 2026-10-05 11:02

Backlog item RX-01: does the RX-timeout defer logic in `src/esp32/esp32_main.cpp` leak, i.e. does
`RX_TIMEOUT_FIRE` -> `RX_RESTART src=timeout` restart the radio while a direct neighbour's packet is
on air, so only relayed copies arrive?

## Verdict

- **SX1262 (DK5EN-98): inconclusive, no evidence of a leak.** The candidate-to-restart join has no
  statistical power (relay delay spread is wider than the matching window); the match count is at
  or below chance. The miss rate of strong direct neighbours (3.5 %) is fully explained by the node's
  own half-duplex TX (4 to 5 % overlap probability), leaving no excess to attribute to the defer
  logic.
- A leak of up to roughly 1 % of received packets (30 to 120 packets in 24 h, exposure model below)
  can neither be shown nor excluded from this data.
- **SX127x (T-Beam): needs its own run with ground-truth TX times** (design in the last section).
  This soak is only the SX1262 baseline, as noted in the backlog.

## Data and method

| Item          | Value                                                                                                                                    |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Console logs  | `2026-10-04.log`, `2026-10-05.log` (359 862 lines, 30 MB), flags `nbrdebug,loradebug,txcapture`                                          |
| Sniff         | `dk5en-98-20261004.jsonl`, 3 277 datagrams (2 625 lora, 500 udp, 116 node, 36 ack)                                                       |
| Script        | `rx01_eval.py` in the scratch dir `scratchpad/rx01/` (not in the repo); run `python3 -I rx01_eval.py <dir>`, output kept as `out.txt`    |
| Excluded      | 2026-10-04 20:38:50 to 20:39:30: node rebooted (uptime counter reset to 12 s at 20:39:22, logger `Connection reset` at 20:38:59)         |
| Time base     | logger prefix time (local); RX frame end = the `receiveflag` line; frame start = end minus LoRa airtime (SF11, BW250, CR4/6, 8 preamble) |
| Airtime model | Checked against 2 815 own TX (`TX_FRAME` to `transmittedFlag`, 144 distinct lengths): model is 32 ms longer than measured (median)       |

Console is the primary source: it holds every LoRa RX with path, RSSI and DUP state (the sniff only
carries the subset the gateway forwards: 1 609 unique lora msg_ids vs 3 062 first copies in the
console).

**Direct neighbour (definition used):** an originator that was received at least once as a LoRa
frame whose path has length 1 (originator = last hop) during the window. 8 callsigns qualify: DK5EN-90,
DK5EN-92, DK5EN-1, DK5EN-14 (bench nodes, median RSSI -64 to -76 dBm, SNR +6, "strong") and DL2JA-1,
DL2JA-2, DB0ED-99, DL2UD-1 (median RSSI -108 to -121 dBm, "weak"). Every one of the 8 has at least 38
direct frames, so thresholds of 1, 3 and 10 direct frames give the same set.

**Candidate:** first copy of a msg_id (`DUP:n`, `OWN:-`, copies of one msg_id more than 60 s apart
count as separate groups) from such an originator whose path length is at least 2 (a via is present).
Copies carrying `DK5EN-98` in the path are our own relays echoed back and are excluded.

## 1. Counts

Totals over the window (both files):

| Event                                   |  Count |
| --------------------------------------- | -----: |
| `RX_TIMEOUT_FIRE`                       | 13 842 |
| `RX_TIMEOUT_DEFERRED src=header_valid`  |  1 367 |
| `RX_TIMEOUT_DEFERRED src=preamble_only` |  1 386 |
| `RX_TIMEOUT_DEFERRED src=receiveFlag`   |     14 |
| `RX_RESTART src=timeout`                | 11 075 |
| `RX_IRQ_STALE forced restart`           |    319 |
| LoRa frames decoded                     |  9 361 |
| `OnRxError`                             |    720 |

FIRE = DEFERRED + RESTART exactly (13 842 = 2 767 + 11 075). 20 % of fires are deferred. The 319
forced restarts (318 x preamble-only after one deferral, 1 x header-valid after three) happen with a
latched IRQ flag at rssi -110 to -112 dBm and are not distinguishable from stale flags.

Per hour (local time, `2026-10-04` / `2026-10-05`):

| Hour (start) | FIRE | hdr | pre | rxFlag | RESTART | forced | frames |
| ------------ | ---: | --: | --: | -----: | ------: | -----: | -----: |
| 04 11        |  523 |  83 |  57 |      2 |     381 |      9 |    424 |
| 04 12        |  555 |  59 |  61 |      0 |     435 |     10 |    385 |
| 04 13        |  614 |  59 |  46 |      0 |     509 |     13 |    329 |
| 04 14        |  596 |  66 |  52 |      2 |     476 |     15 |    434 |
| 04 15        |  596 |  54 |  64 |      0 |     478 |     14 |    433 |
| 04 16        |  582 |  81 |  47 |      0 |     454 |      8 |    458 |
| 04 17        |  572 |  53 |  60 |      1 |     458 |     12 |    353 |
| 04 18        |  500 |  59 |  69 |      0 |     372 |      9 |    521 |
| 04 19        |  558 |  56 |  56 |      1 |     445 |     11 |    370 |
| 04 20        |  501 |  79 |  55 |      1 |     366 |     11 |    512 |
| 04 21        |  554 |  79 |  83 |      0 |     392 |     14 |    450 |
| 04 22        |  562 |  44 |  60 |      0 |     458 |     13 |    371 |
| 04 23        |  576 |  53 |  57 |      1 |     465 |      9 |    398 |
| 05 00        |  589 |  64 |  67 |      0 |     458 |      7 |    552 |
| 05 01        |  595 |  45 |  47 |      0 |     503 |     13 |    373 |
| 05 02        |  600 |  36 |  33 |      0 |     531 |      7 |    306 |
| 05 03        |  623 |  53 |  42 |      0 |     528 |     13 |    315 |
| 05 04        |  606 |  55 |  53 |      1 |     497 |     21 |    312 |
| 05 05        |  605 |  46 |  57 |      1 |     501 |     21 |    317 |
| 05 06        |  588 |  43 |  65 |      1 |     479 |     23 |    327 |
| 05 07        |  592 |  49 |  62 |      0 |     481 |     18 |    343 |
| 05 08        |  575 |  48 |  58 |      2 |     467 |     18 |    335 |
| 05 09        |  589 |  44 |  67 |      1 |     477 |     15 |    366 |
| 05 10        |  571 |  56 |  67 |      0 |     448 |     15 |    364 |
| 05 11 (part) |   20 |   3 |   1 |      0 |      16 |      0 |     13 |

Rates are flat around the clock (about 560 fires and 450 restarts per hour, one restart every
7.8 s); the hour 04 20 loses about 25 s to the reboot.

## 2. Candidates

First copies (`DUP:n`, `OWN:-`): 3 062, of which 1 140 direct (path length 1) and 1 922 relayed.
Candidates (direct neighbour, first copy has a via): **67** (62 with exactly one via hop, 5 with
more). Per originator:

| Originator | Class  | Median RSSI / SNR | First copy direct | Candidates | Miss share |
| ---------- | ------ | ----------------- | ----------------: | ---------: | ---------: |
| DK5EN-90   | strong | -76 / +6          |               386 |          8 |      2.0 % |
| DK5EN-92   | strong | -64 / +6          |               177 |          4 |      2.2 % |
| DK5EN-1    | strong | -69 / +6          |               150 |         12 |      7.4 % |
| DK5EN-14   | strong | -74 / +6          |                78 |          5 |      6.0 % |
| DL2JA-1    | weak   | -121 / -10        |               179 |         24 |     11.8 % |
| DL2JA-2    | weak   | -116 / -5         |                78 |          9 |     10.3 % |
| DB0ED-99   | weak   | -114 / -4         |                54 |          2 |      3.6 % |
| DL2UD-1    | weak   | -108 / 0          |                38 |          3 |      7.3 % |

Strong neighbours: 29 of 820 first copies came via a relay (**3.5 %**, 95 % interval 2.3 to 4.8 %).
Weak neighbours: 38 of 387 (9.8 %), which is link margin at -116 to -121 dBm and says nothing about
the defer logic. Cross-check with the sniff: 34 sniff first copies with a via from these originators,
33 of them are among the 67 console candidates (the sniff does not forward all frame types).

## 3. Join with `RX_RESTART src=timeout`

The originator's TX time is not observed. It is estimated from the relayed copy's end time minus the
relay delay `delta`. `delta` was measured on 462 msg_ids heard first direct and then as a one-hop
relayed copy:

| p5    | p25    | p50    | p75    | p95    |
| ----- | ------ | ------ | ------ | ------ |
| 6.3 s | 10.6 s | 12.6 s | 20.9 s | 56.3 s |

A match is a restart inside `[TXend - airtime - w, TXend + w]` with `TXend = copy end - delta`. Base
rate = share of random positions of a window of the same length that contain a restart
(200 000 draws over the soak, reboot gap excluded; restarts occur every 7.8 s).

| Set (n)              | delta | w = 1.5 s: matched | base   | w = 3 s: matched | base   |
| -------------------- | ----- | -----------------: | ------ | ---------------: | ------ |
| All candidates (67)  | p25   |          14 (21 %) | 47.4 % |        36 (54 %) | 72.0 % |
| All candidates (67)  | p50   |          25 (37 %) | 47.4 % |        37 (55 %) | 72.0 % |
| All candidates (67)  | p75   |          31 (46 %) | 47.4 % |        47 (70 %) | 72.0 % |
| One via hop (62)     | p50   |          23 (37 %) | 47.3 % |        34 (55 %) | 72.0 % |
| Strong, one hop (28) | p25   |           6 (21 %) | 47.8 % |        14 (50 %) | 72.1 % |
| Strong, one hop (28) | p50   |          11 (39 %) | 47.8 % |        16 (57 %) | 72.1 % |
| Strong, one hop (28) | p75   |          11 (39 %) | 47.8 % |        20 (71 %) | 72.1 % |

At no `delta` or `w` does the match share exceed the base rate (binomial tail P(X >= matched) between
0.6 and 1.0). **The join cannot discriminate anyway**: a 3.8 s window against a relay delay with a
10 s interquartile range and a 47 % chance coverage leaves no power to see a signal of a few events.
(Beacon timing is no better: the 30-minute position beacons of the strong nodes jitter by about +-5 s, with outliers to +-12 s.)

Candidate table (strong neighbours, one hop, sorted by distance of the nearest restart to the
estimated on-air interval at `delta` = p50; 20 of 28 shown):

| Time (local)   | msg_id   | Path (orig, last hop) | Type | Len | RSSI / SNR of copy | Nearest restart (s) |
| -------------- | -------- | --------------------- | ---- | --: | ------------------ | ------------------: |
| 10-05 07:24:45 | 4106E075 | DK5EN-92, DK5EN-1     | `!`  | 104 | -68 / +6           |                0.00 |
| 10-05 09:39:48 | EA25A1F6 | DK5EN-1, DK5EN-92     | `@`  |  45 | -56 / +6           |                0.00 |
| 10-05 10:46:29 | EA25A389 | DK5EN-1, DK5EN-92     | `!`  | 108 | -55 / +6           |                0.00 |
| 10-04 12:03:15 | B5978064 | DK5EN-14, DK5EN-92    | `!`  | 102 | -65 / +6           |                0.01 |
| 10-04 23:19:08 | 2F05C0DB | DK5EN-14, DK5EN-90    | `:`  |  61 | -76 / +5           |                0.14 |
| 10-04 19:55:22 | B5978080 | DK5EN-14, DK5EN-92    | `!`  | 102 | -57 / +6           |                0.21 |
| 10-05 01:14:04 | EA25A1A8 | DK5EN-1, DL2JA-2      | `:`  |  56 | -114 / -3          |                0.22 |
| 10-05 00:59:39 | EA25A264 | DK5EN-1, DK5EN-92     | `@`  |  45 | -69 / +6           |                0.29 |
| 10-04 21:30:07 | EA25A2BC | DK5EN-1, DK5EN-14     | `@`  |  44 | -82 / +6           |                0.85 |
| 10-04 21:30:50 | EA25A320 | DK5EN-1, DK5EN-92     | `@`  |  45 | -60 / +6           |                1.01 |
| 10-04 23:26:29 | 91A436BC | DK5EN-90, DK5EN-92    | `@`  |  46 | -67 / +6           |                1.03 |
| 10-05 00:06:34 | EA25A0CE | DK5EN-1, DK5EN-92     | `!`  | 108 | -68 / +6           |                1.65 |
| 10-04 20:03:41 | 91A4347B | DK5EN-90, DK5EN-14    | `@`  |  45 | -88 / +5           |                2.04 |
| 10-04 15:20:32 | B5978326 | DK5EN-14, DK5EN-92    | `!`  | 102 | -59 / +6           |                2.13 |
| 10-04 15:09:40 | 91A43A65 | DK5EN-90, DK5EN-92    | `:`  |  54 | -57 / +6           |                2.19 |
| 10-05 00:57:07 | EA25A25A | DK5EN-1, DK5EN-92     | `:`  |  57 | -68 / +6           |                2.56 |
| 10-05 00:53:31 | 91A434F2 | DK5EN-90, DK5EN-92    | `:`  |  57 | -69 / +6           |                3.06 |
| 10-04 23:22:53 | 2E657C70 | DK5EN-14, DK5EN-90    | `:`  |  61 | -77 / +7           |                3.75 |
| 10-05 00:39:42 | EA25A1A7 | DK5EN-1, DK5EN-92     | `:`  |  57 | -69 / +6           |                3.99 |
| 10-04 23:36:02 | EA25A0CD | DK5EN-1, DK5EN-92     | `!`  | 108 | -67 / +6           |                4.21 |

(Type is the frame type character as logged: `@`, `!`, `:`.) "Nearest restart" is the distance from the
estimated on-air interval, 0.00 meaning a restart inside it. With a 47 % base rate such zeros are
expected by chance; the table shows no exceptional rows. 8 of the 28 strong one-hop candidates fall
into the 40 minutes 10-05 00:33 to 01:14, i.e. they cluster in a traffic burst (collision-prone) and
not evenly over the day. In 19 of the 29 strong candidates the relaying last hop was DK5EN-92.

## 4. Other measurements ("measure first" check)

Peer `OnTXDone` times are not in this data (only DK5EN-98's console was logged), so the backlog
check "restart within 100 ms before a peer TX" cannot be done as specified. Proxies from the
received frames (frame end = peer TX done, start estimated by airtime model, +-30 to 50 ms accuracy):

| Measure                                                          | Result                                                |
| ---------------------------------------------------------------- | ----------------------------------------------------- |
| Restarts in the 100 ms before a received frame's end             | 2 of 9 358 frames                                     |
| Restarts in the 100 ms before a received frame's estimated start | 182, chance expectation 120                           |
| `FIRE` inside a received frame's on-air interval                 | 1 787, of which deferred 1 710 (95.7 %)               |
| Undeferred restarts inside a received frame's on-air interval    | 65 (47 with a 50 ms guard), all frames still decoded  |
| Their position in the frame (ms after est. start, bins)          | <25: 20, <50: 13, <100: 12, <200: 9, <300: 8, >300: 3 |
| Preamble-only deferrals by position (ms)                         | <25: 75, <50: 54, <100: 139, <200: 152, >200: 38      |
| Header-valid deferrals by position (ms)                          | <100: 14, <200: 150, <300: 168, >300: 878             |

Reading: the defer logic works when it has something to see (95.7 % of fires landing in a received
frame were deferred). The 65 undeferred restarts fall almost entirely into the first 300 ms of a
frame, i.e. before the header, and these frames were decoded anyway; with the timing accuracy of the
estimate this cannot be separated from clock slop. Restarts in the unprotected first symbols are the
only way for the logic to leak, and the lost packets are by definition invisible here.

Exposure model (restarts x received-frame rate 0.108 /s x unprotected window): 30 packets
(0.3 %) at 25 ms, 60 (0.6 %) at 50 ms, 120 (1.3 %) at 100 ms over 24 h. That is the order of magnitude
a leak could have; it is below the resolution of this soak.

Own-TX deafness budget: DK5EN-98 transmitted 2 815 frames (2 330 s, 2.7 % duty). The probability that
an incoming packet of 0.45, 0.70 or 1.10 s airtime overlaps one of our TX is 4.1, 4.9 or 6.0 %
(100 000 random draws). The strong-neighbour miss share of 3.5 % (2.3 to 4.8 %) is thus consistent
with half-duplex deafness alone.

## 5. Caveats

- Direct neighbours are defined by having been heard direct, so a node that is always lost cannot
  appear (survivor bias; irrelevant for the 4 strong bench nodes).
- Candidates can also arise from relays with a configured `--via` or collisions with other nodes
  (not observable here).
- `DUP:n` relies on the 100-entry dedup ring; a msg_id older than the ring would be marked new again.
- The node rebooted at 2026-10-04 20:38:59 (no panic text in the console; the TCP connection was
  reset). Not analysed here.

## 6. Recommendation for the SX127x (T-Beam) follow-up

The DK5EN-98 data shows that a statistical join over relayed copies cannot answer the question; the
originator TX time must be known exactly. Proposed design:

1. T-Beam with `--loradebug on` on its 2323 console (meshlogger), DK5EN-98 console logged in parallel
   (both NTP-synced, `txcapture` on). DK5EN-98's own TX start/end (`TX_FRAME` to `transmittedFlag`,
   2 815 per day, ms resolution) are the ground-truth stimulus; no extra air traffic needed (bench
   rules stay untouched).
2. Per DK5EN-98 TX: did the T-Beam decode it (direct frame with DK5EN-98 as last hop) or not, and
   was there an `RX_RESTART src=timeout` in `[TX start - 0.05 s, TX end]`. The 2x2 table answers the
   leak question directly and also gives the "restart within 100 ms before a peer TX" count of the
   backlog. Expect >= 24 h (about 2 800 stimuli).
3. If the T-Beam loses direct DK5EN-98 frames in the restart cases: defer on RSSI rise or lengthen
   `csma_timeout` on SX127x as the backlog proposes. If it loses them independently of restarts,
   the cause is elsewhere (SX127x sensitivity, own TX, collisions).
