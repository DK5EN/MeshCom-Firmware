# Campaign GWACK-01: the gateway's double ACK (2026-10-24)

Status: **closed on fork-dev 2026-10-24.** Operator chose C; fix `c462d018`, benched (W3). Released in
v4.40a.10.25; upstream since 2026-10-25 inside #1238 (all fork firmware, merged). Bench driver `tools/bench/gwack_bench.py`, test
`tools/bench/test_gwack_bench.py`.

## 1. Bottom line

- Both ACK copies of a gateway almost always carry **the same id** today. They differ only when the
  millisecond ticks between the two `millis()` reads, and `DisplayInfo` on forces that tick: its
  print sits between the reads.
- **A copy with the same id is a working repeat**, not a defect. A listener that missed copy 1 takes
  copy 2 as new (8/8 on the bench with `--airgap`), and relays forward an id only once.
- **A copy with a distinct id is a second frame.** Every relay forwards both copies (bench and field).
  On delivery it adds nothing over the same-id repeat.
- **Dropping the second copy (B) removes the repeat.** Per copy, listeners miss about 25 % of gateway
  ACKs on the bench (6 % with two copies). A group text without an ACK is retransmitted up to 3 times.
- Recommendation: **option C**. Take the id once and send both copies with it. This costs nothing,
  makes today's majority behaviour deterministic, and takes the `DisplayInfo` dependency out.

## 2. Code facts

- Block: `src/lora_functions.cpp` `OnRxDone`, `if(bGATEWAY)` -> `bSendAckGateway`, ring tags `rx_dm_ack_gw` and
  `rx_dm_ack_new`. Upstream commit `076d6874` "double ACK" (2023-04-10), comment "ACK mit neuer msg_id versenden".
- Between the two `msg_counter=millis()` reads: `addTxRingEntry()`, the `bDisplayInfo` line "ACK from LoRa GW ...",
  `addLoraRxBuffer()`. INSTRUMENT adds nothing in that window.
- `DisplayInfo` defaults to off and is not persisted (`loop_functions.cpp:356`; ESP32 boots it from `bLORADEBUG`).
- Receivers: `handleACK()` runs `is_new_packet()` on the frame id. `rx_ack_fwd` (relay) only fires for a new id,
  only when the acked text is not the relay's own (`itxcheck < 0`) and not from another gateway (`checkServerRx`).
- The originator matches on bytes 6-9 (acked text id), so the frame id plays no part in the match.
- Group texts are retransmitted until an ACK arrives (`dmTextNoRetransmit()` exempts only `{CET}`/`{MCP}`/`{SET}`
  and RM1 replies), at most `MAX_RETRANSMIT` 3 more times. Each retry copy carries a new id (bits 10-11 flipped),
  so relays forward it again.
- Airtime at SF11 / BW250 / CR4/6 / preamble 32: ACK (12 B) **510 ms**, 60-byte text 903 ms.

## 3. Measurements

Bench 2026-10-24, all nodes at 2 dBm, `identity_guard` passed, group 9999 only. Gateways DK5EN-1 (Heltec V3, S3),
DK5EN-92 (T-Beam, classic ESP32) and DK5EN-90 (RAK4631, nRF52), INSTRUMENT images moved to the mock server.
DK5EN-14 (T-Deck Plus, stock, no gateway) sent. DK5EN-98 (production gateway, mesh on) stood in as the natural
relay and as a fourth gateway. All nodes were restored to stock images and their original settings afterwards.

### 3.1 Same or distinct id

| Source                                       | DisplayInfo | Pairs | Same id | Distinct | Id delta              |
| -------------------------------------------- | ----------- | ----- | ------- | -------- | --------------------- |
| Bench P1 round 1 (3 gateways)                | off         | 23    | 22      | 1        | 1 ms (classic ESP32)  |
| Bench P1 round 2 (3 gateways)                | on          | 28    | 3       | 25       | 1-2 ms, all platforms |
| Bench P2 (DK5EN-1, DK5EN-90)                 | off         | 16    | 16      | 0        | -                     |
| DK5EN-98 (production, current fork release)  | off         | 17    | 17      | 0        | -                     |
| Field + earlier bench logs (own TX), info on | on          | 136   | 1       | 135      | p50 22 ms             |

The 135 distinct pairs are DK5EN-98 and DJ8MEH logs from before; the one same-id pair is DK5EN-1 in BL-01 W2 (2323 console). Before the fork's 2048 B UART TX ring (`8a1b2564`, 10-13) the blocking
UART print stretched the gap to about 22 ms. With the ring it is 1-2 ms, still enough to tick.

### 3.2 What listeners and relays make of it

| Measurement                                                   | Result                                                       |
| ------------------------------------------------------------- | ------------------------------------------------------------ |
| P2: listener airgapped through copy 1, released before copy 2 | **8/8** took copy 2 as new (`H84 DUP:n`), relay copy `DUP:d` |
| Listener cells, same-id pairs, direct copies (round 1)        | both 37, one 25, none 4 (of 66), fits per-copy loss p 0.25   |
| Listener cells, distinct pairs, direct copies (round 2)       | both 36, one 33, none 6 (of 75), both accepted as new (`nn`) |
| Relay TX per pair at DK5EN-98, same id (all runs)             | 1 relay in 40 of 41, never 2                                 |
| Relay TX per pair at DK5EN-98, distinct id (round 2)          | 2 relays in 10, 1 in 15 (one copy missed or suppressed)      |
| Field listener logs, distinct pairs                           | both copies heard relayed in 64 of 118 relayed pairs         |
| Originator without any ACK (round 1, text 5/8)                | retransmitted the text after 50 s                            |

Caveat: three to four gateways acked each text at once, so collisions on the bench are worse than at a typical
field site. p = 0.25 is an upper-end figure.

## 4. Decision matrix

Per event "one gateway hears one group text". R = relays in range of the gateway, p = per-copy loss
(bench 0.25). Airtime in seconds of channel time.

| Criterion                             | Today (id follows DisplayInfo)  | A: distinct id          | B: one ACK only                              | **C: same id, deterministic**                         |
| ------------------------------------- | ------------------------------- | ----------------------- | -------------------------------------------- | ----------------------------------------------------- |
| Gateway TX                            | 2 x 0.51                        | 2 x 0.51                | 1 x 0.51                                     | 2 x 0.51                                              |
| Relay TX per relay                    | 1 (info off), up to 2 (info on) | up to 2 (bench 1.4)     | 1                                            | 1                                                     |
| Listener misses the ACK               | p^2 (6 %)                       | p^2 (6 %)               | **p (25 %)**                                 | p^2 (6 %)                                             |
| Originator retries a text (no ACK)    | rare                            | rare                    | about 4x more often, up to 3 x 0.90 + relays | rare                                                  |
| Second relayed copy beyond hop 1      | only when ids differ            | yes                     | no                                           | no (a relay that missed copy 1 still forwards copy 2) |
| Expected airtime, model               | between A and C                 | 1.02 + 0.77 R           | 0.87 + 0.64 R (incl. retry risk)             | **1.02 + 0.48 R**                                     |
| Behaviour depends on a debug flag     | **yes**                         | no                      | no                                           | no                                                    |
| Change                                | -                               | 1 line + test           | delete about 20 lines + test                 | 2 lines + test                                        |
| Fit with upstream intent (`076d6874`) | -                               | literal ("neue msg_id") | reverses "double ACK"                        | keeps "double ACK" as a repeat                        |

Model: C = 2 x 0.51 + R x 0.51 x (1 - p^2); A = 2 x 0.51 + R x 2 x 0.51 x (1 - p); B = 0.51 + R x 0.51 x (1 - p)
plus (p - p^2) x one retry (0.90 text + 0.90 R relays + one more C-type ACK event). B undercuts C only with no
relay in range (R < 1); A is the most expensive from the first relay on. B's retry term disappears when a second
gateway in range acks the same text.

## 5. Decision and fix

- **Operator decision 2026-10-24: C.** Fix `c462d018`: `gwAckBuildPair()` (`src/gw_ack_frame.h`) reads the clock
  once, and the second copy is a byte copy of the first. The wire layout is unchanged. `OnRxDone` queues the same
  two ring entries (`rx_dm_ack_gw`, `rx_dm_ack_new`) and calls `addLoraRxBuffer()` once for the one id.
- Regression test `test/test_gw_ack_frame` (env `native`): fails on the two-read algorithm (`Expected 0x0002286C
Was 0x0002286D`), passes on the fix. Golden `variant-ini-effective.json` regenerated for the new filter entry.
- Gate: `tools/regression.sh --stage 1,2` PASS (native 743/743, golden selftest, board links, ruff, pytest 1323).
  Builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam` and `wiscore_rak4631` OK. Opus advisor: APPROVED, no defects.
- **W3 bench** (INSTRUMENT images of `c462d018`, same setup as P1 round 2, DisplayInfo on): **28/28 pairs same id**
  (before: 25/28 distinct). Relay per pair `same:1` in 28/28. Summary
  `tools/bench/runs/harness/gwack-w3fix-20261024-110548.json`. The nodes run stock images of the fixed tree again.
- Out of scope: ACK ids are bare uptime milliseconds (top bits zero), so two gateways can collide across the
  fleet. That is upstream design.

## 6. Reproduce

```sh
python3 tools/bench/gwack_bench.py --phase timing --info-mode on --tag p1on \
    --gw DK5EN-1=tcp:<ip> --gw DK5EN-92=tcp:<ip> --gw DK5EN-90=serial:230D6EBB3266D20E \
    --sender DK5EN-14=tcp:<ip> --mock-host <mac-ip> --count 10 --interval 50
python3 tools/bench/gwack_bench.py --phase missfirst --info-mode off --tag p2miss \
    --gw DK5EN-1=tcp:<ip> --gw DK5EN-90=serial:230D6EBB3266D20E --gw DK5EN-92=tcp:<ip> \
    --listener DK5EN-92=tcp:<ip> --watch DK5EN-1 --sender DK5EN-14=tcp:<ip> --mock-host <mac-ip>
python3 tools/bench/gwack_bench.py --reanalyse <stamp> --tag <tag> --gw DK5EN-1=- ... --sender DK5EN-14=-
```

Gateways and the listener need INSTRUMENT images (`--srvip`, `--airgap`); the sender must not be a gateway.
Summaries: `tools/bench/runs/harness/gwack-p1off-20261024-095414-pairs.json`,
`gwack-p1on-20261024-100405-pairs.json`, `gwack-p2miss-20261024-101545.json` (trials) and `-pairs.json`.
