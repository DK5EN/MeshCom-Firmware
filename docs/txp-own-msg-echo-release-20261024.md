# DK5EN-98 TXP logger: own messages released before their first TX

Date: 2026-10-24. Source: rpizero `~/meshlog/dk5en-98-txp-w1c/2026-10-24.log` (gate build `3e07f894`, 16:21 to
23:40, 7.3 h) against the W0 baseline `~/meshlog/dk5en-98-txp-w0/2026-10-24.log` (12:30 to 16:19, 3.8 h). The 24 h
logger was stopped on 2026-10-25 11:24 (operator: the campaign found its bug). Sections 6-8 (2026-10-25) add the
full logger window, the bench reproduction and the PN behaviour. Defect ID **TXP-01**, campaign
[`campaign-tx-pool.md`](campaign-tx-pool.md) W1d.

## Bottom line

- **Reproduced on the bench 2026-10-25 (section 7):** DK5EN-98 to group 9999 with the bench gateways subscribed:
  **9 of 10 own texts never left DK5EN-98 on LoRa** (released 3 s after the ring write by the server copy from
  DK5EN-92 or -90). Same run with the bench gateways airgapped: 10 of 10 sent. Only 1 of the 9 released texts
  reached a field node; with own TX, 10 of 10 did.
- **Scope confirmed:** own group texts and `*` only. Own PNs are never released (0 of 20 on the bench, 0 of 26 in
  the log); a PN is sent once even when the server ACK arrives before its own TX.
- **The TX pool does not evict too fast. It does not evict at all:** 0 TTL drops, 0 priority drops, 1 supersede in 7.3 h.
  The queue stays shallow because about 140 frames/h arrive and leave within seconds.
- **Real finding: 45 of 103 own group texts (44 %) never left DK5EN-98 on LoRa.** The echo release from the QRT
  campaign (10.19, `fe3f01e2`) freed the READY slot because another node put the server copy on air first.
- **Rule to restore:** an own message goes out on LoRa from our own node at least once. An echo may stop
  retries, never the first transmission.
- **Timing is the root of it (section 4):** DK5EN-98 sends the own text to the server at once, but on LoRa
  it waits for the remainder of the running CSMA window, which is not shortened for an own message. Other gateways
  get the server copy within a second and often transmit first. They don't have higher priority; they have a
  head start.

## 1. Queue load and eviction

| Metric                         | W0 (12:30-16:19) | W1c (16:21-23:40) |
| ------------------------------ | ---------------- | ----------------- |
| `RING_WRITE`                   | 130/h            | 141/h             |
| `RING_TX_READ` / `TX_DONE`     | 129/h            | 137/h             |
| `RING_DROP_STALE` (TTL or CET) | n/a              | 0                 |
| `RING_DROP_PRIO` / `_NEW`      | 1 / 0            | 0 / 0             |
| `RING_SUPERSEDE`               | n/a              | 1                 |
| `RING_REARM` (DM retry)        | n/a              | 26                |
| `CSMA_AGED`                    | 2.1/h            | 14.9/h            |
| `CAD_BUSY`                     | 6.8/h            | 11.1/h            |

Queue depth at TX (`queued=` in `RING_TX_READ`):

| Depth | W0  | W1c |
| ----- | --- | --- |
| 0     | 394 | 708 |
| 1     | 456 | 878 |
| 2     | 78  | 263 |
| 3     | 26  | 112 |
| 4     | 13  | 47  |
| 5     | 6   | 21  |
| 6     | 4   | 6   |
| 7     | 5   | 1   |
| 8-9   | 8   | 0   |

Same shape before and after the pool. The evening window has more own and server traffic (`user_msg` 103,
`udp_rx` 299 writes) and more channel activity (`CAD_BUSY`, `CSMA_AGED` up).

Longest queue wait at TX against the TTL:

| Prio | Frames   | n   | p50   | p90    | max    | TTL   |
| ---- | -------- | --- | ----- | ------ | ------ | ----- |
| 1    | ACK etc. | 114 | 5.6 s | 16.9 s | 26.1 s | 90 s  |
| 2    | own text | 42  | 3.6 s | 9.9 s  | 22.3 s | none  |
| 3    | relay    | 376 | 9.4 s | 23.6 s | 67.6 s | 300 s |
| 4    | position | 162 | 4.5 s | 31.5 s | 75.5 s | 120 s |
| 5    | HEY      | 311 | 3.6 s | 23.2 s | 65.9 s | 180 s |

No frame reached its TTL; the closest was a position at 76 s of 120 s. TTL values need no change on this data.

## 2. Own group texts released before their first TX

Of 103 own texts (`src=user_msg`, status READY) 58 were sent, 45 never reached `RING_TX_READ`.

Example `1AE1E070` (DK5EN-98>20):

```
17:12:43.115  RING_WRITE slot=0 type=3A status=00 ... src=user_msg
17:12:43.116  TX-UDP 111 : x1AE1E070 ... DK5EN-98>20       (server copy out at once)
17:12:45.969  [RETX] echo on READY slot, released retid:0   (2.9 s later)
17:12:45.970  [LOG] ... DK5EN-98,DK5EN-90>20                (heard from DK5EN-90, RSSI -86)
```

No `TX_START` for this id on DK5EN-98. DK5EN-90 transmitted it, so it most likely got it as the server copy.

Mechanism: `src/lora_functions.cpp:1090` (QRT Finding 1). An echo of an own group text while the slot is still
READY sets the slot DONE/len 0 ("the message is on air"). It does not check whether the own node has sent it
yet.

Who supplied the echo that released the slot:

| Echo heard from | Released | Sent anyway (echo after own TX) |
| --------------- | -------- | ------------------------------- |
| DK5EN-90        | 14       | 9                               |
| DK5EN-92        | 12       | 9                               |
| DK5EN-1         | 11       | 9                               |
| DF2SI-12        | 8        | 8                               |
| DL2UD-1         | 0        | 9                               |
| DB0ED-99        | 0        | 8                               |
| no echo         | 0        | 5                               |

- 37 of 45 releases came from the bench nodes DK5EN-1/-90/-92 (2 dBm, same site). In those cases the text only reached
  the mesh from a 2 dBm bench node, not from the gateway antenna. 8 came from DF2SI-12, a real field node.
- Echo delay after `RING_WRITE` for the released texts: p50 4.7 s, max 16.8 s.
- Logging gap: the release prints only `[RETX] echo on READY slot` under `bDisplayRetx`, no `[MC-DBG] RING_*` line.
  A ring-event evaluation does not see it.
- The W0 window had no own texts, so there is no before/after comparison for this effect.

## 3. Delay: own messages and server frames on DK5EN-98

Firmware latency (`lat=`, enqueue to TX) at the first transmission, by source:

| Source         | n   | p10   | p50    | p90    | max    |
| -------------- | --- | ----- | ------ | ------ | ------ |
| `rx_relay`     | 497 | 3.6 s | 3.6 s  | 17.0 s | 75.5 s |
| `udp_rx`       | 305 | 4.7 s | 9.8 s  | 24.5 s | 67.6 s |
| `user_msg`     | 58  | 0.6 s | 4.8 s  | 14.5 s | 22.3 s |
| `rx_ack_fwd`   | 53  | 3.1 s | 6.3 s  | 17.0 s | 26.1 s |
| `auto_pos`     | 30  | 3.1 s | 12.7 s | 42.1 s | 46.9 s |
| `user_wx`      | 14  | 1.6 s | 16.4 s | 45.1 s | 51.5 s |
| `rx_dm_ack_gw` | 10  | 3.2 s | 6.6 s  | 20.4 s | 20.4 s |
| `beacon`       | 9   | 0.1 s | 4.1 s  | 12.0 s | 12.0 s |

Observations:

- **Own texts wait about as long as the echo takes:** own `user_msg` p50 4.8 s, releasing echo p50 4.7 s. Each own
  text is a race against the server round trip plus another node's TX. DK5EN-98 loses it in 44 % of cases.
- **Server frames wait longest of the text sources at DK5EN-98** (`udp_rx` p50 9.8 s, twice the relay median).
  Other gateways seem to send the same server frame faster, so their copy is on air first.
- **`rx_relay` sits at a fixed 3.6 s floor** (p10 = p50 = 3.6 s; many `lat=3629ms`). That is the NBR case-A
  relay window (3500 ms + 0-70 ms jitter, re-armed when the frame is received), not queueing. See section 4.
- Wall-clock enqueue-to-TX matches `lat=` except for a few outliers from reused msg ids (ignored).

## 4. Firmware timing (code check, `fork-dev` `43aaaabd`)

### How a TX gets its turn

- **TX starts only when the RX window expires:** `esp32_main.cpp:3590`,
  `millis() - iReceiveTimeOutTime >= csma_timeout`, then `TX_GATE_ENTER`, IRQ poll, CAD, `doTX()`.
- **Every received frame and every RX restart re-arms the window** (`iReceiveTimeOutTime = millis()`). A busy
  channel keeps pushing the TX out until it has been quiet for one full window.
- **The window length is fixed when it is armed,** from the priority of `getNextTxSlot()` at that moment
  (`csma_compute_timeout()` / `csma_reset()`, `lora_functions.cpp:4040-4067`).
- **Queuing a frame does not touch the window.** No `addTxRingEntry()` path assigns `csma_timeout` (all 11
  assignments are in RX, CAD and TX code). A frame queued into a running window waits for its remainder,
  whatever its priority.
- **With an empty queue the window is the relay window:** `getNextTxSlot() == -1` maps to `MSG_PRIO_NORMAL`, so
  an idle node runs 4500 + 0-350 ms windows back to back (`RX_TIMEOUT_FIRE wait=4570`/`4815` in the log).

### Window per frame class

| Class                                   | Prio  | Window after RX (attempt 0)                           | Source                                                      |
| --------------------------------------- | ----- | ----------------------------------------------------- | ----------------------------------------------------------- |
| ACK, own DM                             | 1     | 3000 + 0-350 ms                                       | `CSMA_PRIO_BASE_1`                                          |
| Own group text / broadcast (`user_msg`) | 2     | 3000 + 0-350 ms                                       | `CSMA_PRIO_BASE_2`, `classifyFrame()`                       |
| Relay case A (a neighbour needs us)     | 3     | 3500 + 0-70 ms; after 8 s: 150 + 0-70 ms              | `NBR_RELAY_CASE_A_*`                                        |
| Relay text case B (nobody needs us)     | 3     | 4500 + 0-350 ms                                       | `CSMA_PRIO_BASE_3` (text keeps the normal base)             |
| Server frame (`udp_rx`)                 | 3     | as relay (case A/B if `--nbrrelay` and NBR known)     | status DONE, so `classifyFrame()` returns `MSG_PRIO_NORMAL` |
| Position / HEY                          | 4 / 5 | 5500 + 0-350 ms; POS/HEY relay case B: hold 20 s once | `CSMA_PRIO_BASE_4/5`, `NBR_RELAY_CASE_B_EXTRA_MS`           |
| Empty queue (idle)                      | (3)   | 4500 + 0-350 ms                                       | `getNextTxSlot() == -1`                                     |
| Any prio 1-3 waiting >= 20 s / >= 60 s  | 1-3   | 500-1500 ms / 100 ms (+ 0-175 ms), after RX only      | `CSMA_AGE_STAGE1/2_MS`                                      |
| CAD busy, attempt 1 / 2 / >= 3          | all   | base x 5/6 / x 2/3 / 100 ms rapid-fire                | `csmaTimeoutPrio()`                                         |

Retries scale the base (5/6, 2/3); from attempt 3 it is rapid-fire CAD.

### Which window actually fired before the first TX (DK5EN-98, `RX_TIMEOUT_FIRE wait=` before `RING_TX_READ retry=0`)

| Source     | n   | < 1 s | 1-3 s | 3.0-3.5 s | 3.5-3.6 s | 3.6-4.5 s | 4.5-4.85 s | > 4.85 s |
| ---------- | --- | ----- | ----- | --------- | --------- | --------- | ---------- | -------- |
| `user_msg` | 58  | 0     | 8     | 31        | 6         | 0         | 11         | 2        |
| `udp_rx`   | 305 | 113   | 37    | 0         | 80        | 6         | 68         | 1        |
| `rx_relay` | 497 | 61    | 6     | 0         | 404       | 0         | 1          | 25       |

- **11 of 58 own texts went out on a 4.5 s idle or relay window**, not their 3.0 s prio-2 window: they were queued
  into a running window and waited for its remainder.
- **81 % of relays fired on the 3.5 s case-A window,** which explains the 3629 ms floor.
- **Server frames are spread out:** 113 on rapid-fire or aged windows (< 1 s, they had waited 20-60 s), 68 on the
  4.5 s window. Server frames are prio 3 behind every own text and ACK, and they restart their wait on every
  received frame.

### Why other nodes put our message on air first

Timeline for an own group text at the gateway DK5EN-98:

1. `t0`: the text goes into the ring (READY, prio 2) **and is sent to the server at once** (`TX-UDP`, same
   millisecond in the log). LoRa waits for the remainder of the running window (0-4.85 s), and longer if frames
   arrive in between.
2. `t0 + server round trip` (well under 1 s): every other gateway queues the server copy as `udp_rx` (prio 3). It
   also waits only for the remainder of _its_ running window. On a quiet node that window is often almost over.
3. The first node whose window expires transmits (airtime 1.1 s for 100-120 bytes, measured TX_START to TX_DONE). DK5EN-98 hears the echo at
   the end of that transmission: p50 4.7 s after `t0`.
4. The echo release (`lora_functions.cpp:1090`) frees DK5EN-98's READY slot. The received echo also re-arms DK5EN-98's
   window, so DK5EN-98 would have waited a further 3.0 s anyway.

Two delays race, and both are "remainder of a window" plus re-arms. Own text p50 4.8 s against echo p50 4.7 s means
roughly even odds. The other node does not have higher priority, since a server frame is prio 3. It has
a head start: DK5EN-98 hands the text to the server before its own LoRa turn, and its own TX gets no shorter
window for being an own message.

## 5. Proposed changes (not implemented)

1. **At least one own TX:** the echo release must not free an own text that has not been sent yet (status READY,
   retry 0). The echo may still stop retries of a sent text (status SENT), as it already does. Regression test:
   own group text READY plus an echo from another node, the slot must still reach `doTX()` once.
2. **Re-arm on enqueue:** when a frame is queued with a higher priority than the one the running window was
   computed for, shorten the window to `min(remaining, prio window)`. Own texts then get their 3.0 s instead of the
   remainder of a 4.5 s idle window. Same rule for an empty-queue window: idle should not mean relay timing.
3. **Server upload after the own LoRa TX (gateway, own texts only):** upload the own text to the server when the
   LoRa TX is done, or after a short fixed delay, so other gateways cannot overtake it with the server copy. Needs
   a decision: it delays the server and app copy by about 3-5 s.
4. **Log line:** `[MC-DBG] RING_RELEASE_ECHO slot= msg_id= retry= from=` under `bLORADEBUG`, so the night
   evaluation counts it.
5. **Measure on air:** TX time per node for the same server frame (same msg id) on DK5EN-98 and a second
   gateway, to confirm step 2 of the timeline (server round trip, remaining window on the other node).
6. **Bench setup:** confirm whether DK5EN-1/-90/-92 transmit server frames (gateway on). If so they distort the
   field picture for DK5EN-98. Consider `--airgap on` or gateway off for the rest of the logger night.
7. **TTL values:** unchanged. Evaluate the full 24 h, but this window gives no reason to shorten or lengthen them.

## 6. Full logger window (2026-10-24 16:21 to 2026-10-25 11:24)

Own texts of DK5EN-98 by destination (organic traffic, no test sends):

| Destination                                  | Own texts | Sent on LoRa | Released before own TX | Echo that released it                            |
| -------------------------------------------- | --------- | ------------ | ---------------------- | ------------------------------------------------ |
| `*`                                          | 4         | 3            | 1                      | DK5EN-1                                          |
| group 20                                     | 82        | 33           | 49                     | DK5EN-90 17, DK5EN-1 12, DK5EN-92 12, DF2SI-12 8 |
| other groups (2, 9, 2322, 2325, 2628, 26298) | 6         | 6            | 0                      | -                                                |
| PN                                           | 26        | 26           | 0 (1 `ACK_SKIP_READY`) | -                                                |

Released echo delay after the ring write: p50 4.7 s, max 16.8 s. Field relays heard on DK5EN-98 after its own TX
(distinct msg ids): DL2JA-1 26, DL2JA-2 30, DB0ED-99 33, DL2UD-1 23. Server frames DK5EN-98 put on LoRa: `*` 175,
group 20 229, group 262 63, PN to OE5PYB-1 8; 47 PNs to DK5EN-98 itself arrived over UDP, none went on LoRa.

## 7. Bench reproduction (2026-10-25)

Setup: DK5EN-1/-90/-92 on INSTRUMENT images of `eb246050` (for `--airgap`), gateways on, 2 dBm; DK5EN-14 gateway
off; DK5EN-98 production image, 22 dBm. All five subscribed to group 9999 for the runs (DK5EN-98 restored
afterwards). Driver [`tools/bench/own_echo_bench.py`](../tools/bench/own_echo_bench.py), `--loradebug on` +
`--txcapture on` on every node, 10 marked texts per case. Summaries: `tools/bench/runs/harness/own-echo-{A,B,C,D}-*.json`.
Destinations restricted to 9999, TEST and PNs between own nodes; `*`, 20, 232, 262 were never sent.

| Case | Sender   | Destination        | Bench state                   | Released before own TX | Own TX | Released by       | Texts with a field echo |
| ---- | -------- | ------------------ | ----------------------------- | ---------------------- | ------ | ----------------- | ----------------------- |
| E1   | DK5EN-98 | 9999               | bench gateways on             | **9/10**               | 1/10   | DK5EN-92 7, -90 2 | 1/10                    |
| E2   | DK5EN-98 | 9999               | DK5EN-1/-90/-92 `--airgap on` | 0/10                   | 10/10  | -                 | 10/10                   |
| E3   | DK5EN-98 | TEST (a PN, below) | normal                        | 0/10                   | 10/10  | -                 | 8/10                    |
| E4   | DK5EN-98 | PN DK5EN-1         | normal                        | 0/10                   | 10/10  | -                 | 10/10                   |
| E5   | DK5EN-1  | PN DK5EN-98        | normal                        | 0/10                   | 10/10  | -                 | 6/10                    |
| E6   | DK5EN-1  | 9999               | normal                        | **3/10**               | 7/10   | DK5EN-98 3        | 7/10                    |
| E7   | DK5EN-1  | TEST (a PN, below) | normal                        | 0/10                   | 10/10  | -                 | 6/10                    |
| E8   | DK5EN-14 | 9999               | gateway off (control)         | 0/10                   | 10/10  | -                 | 8/10                    |

- **E1 trace (`1AE1E170`):** ring write and server upload 11:40:09, `[RETX] echo on READY slot, released` at
  11:40:12 on the copy `DK5EN-98,DK5EN-90>9999` (RSSI -78), no `RING_TX_READ`. The text reached the air only from
  a 2 dBm bench node.
- **E2 proves the mechanism:** the airgapped gateways queued the server copy (`src=udp_rx`) and the airgap dropped
  their TX (`[AIRGAP] tx-dropped`); DK5EN-98 heard nothing and sent every text itself, 1-11 s after the write.
- **E6:** a 2 dBm gateway loses the race against a 22 dBm gateway's server copy too (3/10).
- **E8:** a node without gateway has no server head start; it always sends first.

## 8. PN behaviour and side findings

- **Own PN, both directions (E4, E5):** never released, sent once, one ACK per PN. The server delivers the PN
  over UDP to the destination gateway within a second; that gateway displays and ACKs it (UDP and LoRa) and does
  not put it on LoRa. The server ACK reaches the sender about 4 s later, while the PN is still READY; the sender
  still transmits it once (`findAndStopRingSlotImpl()` skips READY slots). The later LoRa copy is a `[DMDUP]` on
  the receiver, no second ACK. Other gateways got the PN from the server once in 20 (DK5EN-90, E5 #9).
- **TEST is not a group (TXP-03):** `CheckGroup()` (src/aprs_functions.cpp:39) accepts digits only, so `::{TEST}`
  is a PN to the callsign `TEST`: it gets a `{NNN` number, nobody ACKs it, the server forwards it to no gateway,
  and the retry ladder sends every text 4 times (E3/E7: 69 and 73 LoRa TX for 10 texts each). Bench rule: use
  group 9 or 9999 for group tests; TEST only where a PN-to-nobody is wanted.
- **Second upload of a PN the gateway already received over UDP (TXP-02, low):** the UDP receive path of a PN to
  the gateway's own call logs `RX_DEDUP_NEW` but does not add the id to the dedup ring, so the LoRa copy that
  arrives later is new (`DUP:n`) and is uploaded to the server again (`GWU` + `TX-UDP`, DK5EN-1 in E4). The
  display and ACK path dedups correctly (`[DMDUP]`).

## 9. Fix (TXP-01, proposal 1 above, implemented in section 10)

Operator rule: a message from my node goes out on LoRa at least once; if its echo was already heard, no repeat
is needed. The TX buffer already has the second half: every echo of an own text sets `own_msg_id[][4] = 0x01`
(src/lora_functions.cpp:1712-1750), and at the 40 s retry point an own group text that was heard stops
(`[RETX] already heard, stop`, :3396-3410). The fix is therefore one rule in the READY branch at :1096: an own
text (source = own call) is not released while READY; it goes out once and the existing heard check suppresses
the repeat. Regression test: own group text READY plus a foreign echo -> slot still reaches `doTX()` once and no
retry follows. Open for the operator: whether a text sent for a KISS client (foreign source) keeps the
release-on-echo it has always had.

## 10. Fix and bench verification (2026-10-25, `556b997d`)

- **Fix:** `pnFrameIsReleasableGroupText()` (src/pn_retry.h) excludes every frame with our own callsign as
  source, so an own text stays READY when the server copy of another gateway is heard first and goes out once.
  `pnFrameIsRetryEligibleGroupText()` keeps its truth table, so the 40 s heard check still stops the repeat. Texts
  sent for a KISS client (foreign source) keep the release.
- **Regression test:** `test_pn_retry` own group text and own `*` text not releasable (failed before: 2 of 55,
  test_main.cpp:875/:883), still retry-eligible; foreign-source group text stays releasable. Gate stage 1+2 PASS
  (81 envs, 2928/2928, golden 48, pytest 1410). Advisor (Opus) approved.
- **Bench, same setup before and after** (DK5EN-1 and DK5EN-90 send to 9999; DK5EN-90/-92 and DK5EN-1 compete
  as gateways with the server copy; INSTRUMENT images):

| Run        | Firmware            | DK5EN-1 -> 9999           | DK5EN-90 -> 9999          |
| ---------- | ------------------- | ------------------------- | ------------------------- |
| V0 (13:12) | `eb246050`, unfixed | 4 of 5 released, 1 sent   | 2 of 5 released, 3 sent   |
| V1 (13:27) | `556b997d`, fixed   | 0 of 10 released, 10 sent | 0 of 10 released, 10 sent |

- **Echo before own TX is now harmless:** in 5 of the 20 fixed texts the competitor's copy arrived first
  (`ACK_SKIP_READY` 4 on DK5EN-1, 1 on DK5EN-90), the slot stayed READY and was sent once. Example `EA25A38E`:
  echoes 13:33:22 (DK5EN-92) and 13:33:26 (DK5EN-90), own TX 13:33:29, gateway ACK 13:33:54, no repeat.
- **One-time send:** 19 of 20 texts went out exactly once. The one repeat (DK5EN-90 `91A434CE`) was not heard by
  anyone within 40 s (heard flag `00`), so the retry is the intended behaviour.
- **Not yet in the field:** DK5EN-98 still runs the unfixed `3e07f894` image; the fix reaches it with an OTA flash
  (operator OK) or the next release. Summaries `tools/bench/runs/harness/own-echo-V0-*.json`, `own-echo-V1-*.json`.
