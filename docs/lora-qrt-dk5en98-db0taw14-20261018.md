# DK5EN-98: QRT on a quarter-busy channel, 2026-10-18 19:14-19:38

Capture of the production gateway DK5EN-98 (Heltec V3, fork tag `v4.40a.10.18`, 433.175 MHz, EU8:
SF11 BW250 CR4/6) over the 2323 net console with `--loradebug` and `--txcapture` on, recorded by
`meshlogger.py` on rpizero. Reviewed with `/fable-review` (seven finders, seven verifiers); the verdict
with file:line fixes is in `lora-qrt-dk5en98-db0taw14-20261018-verdict.md`.

## Verdict

**This is not acceptable performance over LoRa.** The channel was busy 26 % of the time (the node's own
STAT util says 24-28 %), and the ring still filled to QRT. A node that cannot drain its queue into a
channel that is idle three quarters of the time has to learn to send in the gaps.

Four mechanisms stack up:

1. **The send gate cannot use the gaps.** Every received frame restarts a fixed 3.0-5.5 s hold, and a hold
   restarted by received frames never gets shorter. The median gap between two frames of other stations is
   2.7 s, so only 28 % of the gaps are long enough for the 4.5 s hold of server text.
2. **The same message goes on air 4-6 times.** Four gateways in range (DK5EN-98, DK5EN-1, DK5EN-92,
   DF2SI-12) all put every server message on air, and the bench nodes relay everything. Server messages
   averaged 3.8 copies on air; own messages 6.4 copies per message.
3. **DK5EN-98 does not notice that its queued frames are already on air.** 41 of 44 server messages were on
   air from another station before DK5EN-98 sent them, and it sent 18 of them anyway. 20 echoes of its own
   messages arrived while the copy was queued and were ignored, so 5 messages ran the full 3-retry ladder.
4. **The ring counts as full by index, not by content.** Free slots behind a waiting frame are not reused,
   so frames were dropped as "queue full" with only 5 of 20 slots occupied.

The same evening the relay DB0TAW-14 in the Rhön showed the same starvation, worse: 16-19 of 20 slots
queued, 41-43 % receive load, and 0.0-0.1 % transmit (section "DB0TAW-14").

The ring then reached QRT (16/20 at 19:29:00), and the release rule (depth <= 1 for 10 s) kept the QRT
state until 19:36:57, although the depth was below 16 from 19:30:00.

## Files

The verdict is next to this report: `docs/lora-qrt-dk5en98-db0taw14-20261018-verdict.md`. The data lives
in `logs/dk5en98-lora-20261018/`, which is gitignored and stays on the analysis machine; paths below are
relative to that folder.

| File                                     | Content                                                                                              |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `logs/dk5en-98_2026-10-18_1914-1938.log` | Full console capture, 8,912 lines, 0 reconnects                                                      |
| `logs/status.txt`                        | meshlogger end state                                                                                 |
| `analyse.py`                             | Airtime, copies, server/own message fate, per-minute table                                           |
| `analysis.txt`                           | Output of `analyse.py <log> 19:25:00 3`                                                              |
| `gaps.py`                                | Busy share and idle-gap distribution vs CSMA hold                                                    |
| `depth.txt`                              | TX ring depth every 30 s with back-pressure notices                                                  |
| `dg0opk-db0taw14/`                       | DG0OPK's email (PDF) and four saved DB0TAW-14 web GUI pages                                          |
| `review/`                                | Finder and verifier notes, RAM inventory (`r1-ram-growth.md`), replay design (`r2-airgap-replay.md`) |

Airtime per frame is the Semtech LoRa formula for the EU8 profile (SF11, BW250, CR4/6, preamble 8,
explicit header, CRC on, LDRO off): 60 B = 0.71 s, 120 B = 1.25 s. It matches the node's own
TX_ACTIVE -> TX_DONE times to within 9-16 ms. 255 of 288 decoded frames carry a `[LOG]` line with the path;
the other 33 (22 ACKs, 7 positions, 4 telemetry) and 28 CRC-error frames count as busy time but are not
attributed to a sender.

## The four questions

### 1. What triggers that my node waits again?

The send gate (`esp32_main.cpp`, "Check transmit now") opens only when `iReceiveTimeOutTime == 0`. Each of
these sets it again, so the full hold starts over:

| Trigger                                                | Where (HEAD `d5d0f3fc`)                                                               |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------- |
| End of every received frame, any frame, CRC errors too | `lora_functions.cpp:2663-2664` and again `esp32_main.cpp:3603-3604`                   |
| End of its own transmission                            | `esp32_main.cpp` after `OnTxDone()`, `csma_reset()` `:3702`                           |
| Hold expired while a preamble/header is detected       | `RX_TIMEOUT_DEFERRED`, up to 3x (header) or 1x (preamble), `esp32_main.cpp:3480-3517` |
| Channel check before TX finds the channel busy         | `CAD_BUSY`, `cad_attempt++`, `esp32_main.cpp:3846`                                    |

In this capture: 288 decoded and 28 CRC-error frames (each restarts the hold), 94 deferrals, 8 `CAD_BUSY`.
Of the deferrals, 85 were followed by a real frame that restarted the hold anyway; 9 were false preambles
that cost a full extra hold, because the stale-signal guard (RSSI below -126 dBm) never fires at a noise
floor of -107 to -111 dBm.

### 2. How long does it wait when it gets triggered?

`csmaTimeoutPrio()` (`csma_timing.h`): base by the priority of the next queued frame, plus 0-10 random
slots of 35 ms.

| Priority | Traffic                            | Base  | With jitter |
| -------- | ---------------------------------- | ----- | ----------- |
| 1        | ACK, own DM                        | 3.0 s | 3.00-3.35 s |
| 2        | own group / broadcast              | 3.0 s | 3.00-3.35 s |
| 3        | relay, and all server -> LoRa text | 4.5 s | 4.50-4.85 s |
| 4        | position                           | 5.5 s | 5.50-5.85 s |
| 5        | HEY                                | 5.5 s | 5.50-5.85 s |

The base shrinks (5/6, 2/3, then 100 ms rapid-fire) only when `cad_attempt` rises, and it rises only on
`CAD_BUSY`. A received frame resets the hold to the full base of the current attempt. Gate entries here:
77 at attempt 0, 8 at attempt 1, 1 at attempt 2; rapid-fire was never reached. A node starved by short gaps
waits the full hold again after every frame, indefinitely.

One exception exists already: neighbour-matrix case-A relays drop to 150 ms once they have waited 8 s
(`txring_functions.cpp:332-334`). Server text and own messages never get it: all 24 server sends and 30 of
48 own sends waited longer than 8 s.

### 3. Why do my redundant bench nodes fill up the air?

"Redundant" in DK5EN-98's neighbour table is DK5EN-98's view of the topology; it changes nothing on the
bench nodes. Their settings, read over 2323 at 19:48:

| Node     | Board       | Gateway        | Mesh | NBRRELAY | TX power | Effect                           |
| -------- | ----------- | -------------- | ---- | -------- | -------- | -------------------------------- |
| DK5EN-1  | Heltec V3   | on (OE server) | on   | off      | 2 dBm    | sends server traffic, relays all |
| DK5EN-92 | T-Beam      | on (OE server) | off  | off      | 2 dBm    | sends server traffic             |
| DK5EN-14 | T-Deck Plus | off            | on   | off      | 2 dBm    | relays all                       |
| DK5EN-98 | Heltec V3   | on (OE server) | on   | on       | 22 dBm   | 1011 case-A, 1 case-B relays     |

- **Two of them are gateways to the same server.** The server sends every message to every gateway, and
  each puts it on air. Example `71A490E5` ("Bin ich auch Sascha"): server -> DK5EN-98 queue 19:26:53;
  DK5EN-92 on air 19:26:54; DK5EN-1 on air 19:28:34; DK5EN-14 relays DK5EN-92's copy 19:28:49; DK5EN-98
  keeps it queued until a beacon evicts it at 19:30:58.
- **NBRRELAY is off on all three**, so the relay suppression that refuses redundant relays on DK5EN-98 does
  not run on them.
- **2 dBm does not help here.** They share the house with DK5EN-98, which hears them at -56 to -79 dBm.

| Transmitter         | Frames | Airtime s | % of time | % of attributed air |
| ------------------- | ------ | --------- | --------- | ------------------- |
| DK5EN-98 (own TX)   | 78     | 78        | 5.3 %     | 21.6 %              |
| DK5EN-1             | 64     | 61        | 4.2 %     | 17.0 %              |
| DK5EN-14            | 58     | 58        | 4.0 %     | 16.2 %              |
| DK5EN-92            | 51     | 49        | 3.3 %     | 13.5 %              |
| CRC error (unknown) | 28     | 32        | 2.2 %     | 8.8 %               |
| DF2SI-12            | 29     | 26        | 1.8 %     | 7.3 %               |
| DB0ED-99            | 21     | 23        | 1.5 %     | 6.3 %               |
| DL2UD-1             | 20     | 21        | 1.4 %     | 5.9 %               |
| DL2JA-1             | 10     | 10        | 0.7 %     | 2.9 %               |
| DL2JA-2             | 3      | 2         | 0.2 %     | 0.7 %               |
| total               | 362    | 361       | 24.6 %    | 100 %               |

The three bench nodes produced **47 %** of the attributed airtime and 173 of the 316 frames by other
stations (55 %). The 33 unattributed ACK, position and telemetry frames add about 1 % of the time.

Copies of the same message on air (heard plus own TX):

| Traffic                       | Messages | Transmissions | Copies per message | Max |
| ----------------------------- | -------- | ------------- | ------------------ | --- |
| server -> LoRa (udp_rx at 98) | 43       | 164           | 3.8                | 6   |
| own messages of DK5EN-98      | 18       | 116           | 6.4                | 9   |

Own DMs get a new msg_id for every retry (the XOR retry scheme, `pn_retry.h`), so the 18 own messages
appear as 27 ids; the copies are counted per message.

### 4. Why can't I send when the air is only a quarter filled?

Because the idle time is chopped into gaps that are shorter than the hold. Gaps between two frames of other
stations (decoded and CRC-error frames, n = 312):

| Percentile | Gap   |
| ---------- | ----- |
| p25        | 1.2 s |
| median     | 2.7 s |
| p75        | 4.7 s |
| p90        | 8.8 s |

| Hold                        | Gaps long enough | Send chances |
| --------------------------- | ---------------- | ------------ |
| 3.0 s (own msgs, ACK, DM)   | 43 %             | 5.6 / min    |
| 4.5 s (server text, relays) | 28 %             | 3.6 / min    |
| 5.5 s (position, HEY)       | 19 %             | 2.4 / min    |

DK5EN-98 sent 78 frames in 24.5 min (3.2 / min): 48 own frames on the 3.0 s hold, 24 server frames on the
4.5 s hold, 6 NBR case-A relays. It used 52 of the 59 gaps longer than 5.5 s, but only 2 of 28 gaps of
4.5-5.5 s and 2 of 46 gaps of 3.0-4.5 s. Inflow in the busy minutes was 5-8 frames per minute, so the ring
had to fill.

## Channel timeline

19:25:00-19:28:00, 1 character = 1 second, as heard at DK5EN-98. The `98 gate` row shows the send gate for
a priority-3 frame (server text, 4.5 s hold): `w` = nobody transmits, but DK5EN-98 is still inside its hold
after the last frame; `_` = idle long enough to send; blank = someone transmits. For own messages (3.0 s
hold) the gate is open about three times as often. The ring held 7-15 frames the whole time.

```text
19:25:00  |         |         |         |         |         |
98 TX     ..............#+.......+#...................................
DK5EN-1   +..............................+#..................#+....#+.
DK5EN-92  ..........+#..............+#.............++.................
DK5EN-14  .....................................#+.......+#+...........
others    .........#+........#+..............##.............#.........
98 gate   wwwww____ w ww wwww wwww ww wwww ww   wwwwwwwww ww  wwwww ww
ring      8 7

19:26:00  |         |         |         |         |         |
98 TX     .................................+#+........................
DK5EN-1   ..+#..........#............................#................
DK5EN-92  ............................#........+#..............+#....#
DK5EN-14  ........++..............#.......................++..........
others    .....#+..#+...+#....+##+................##..................
98 gate   www w www wwww  wwww_  w www wwww_ www w  w wwww_wwwww wwww
ring      8 11

19:27:00  |         |         |         |         |         |
98 TX     ......#+............................................+#......
DK5EN-1   ................#....+#........+#........................++.
DK5EN-92  +..........+##............................++................
DK5EN-14  ...........................+#........#......................
others    .........+#+.....#+..............#....++....................
98 gate   wwwww_ www w  ww  wwww wwww_ www  www wwwwwwwwww_____ wwwwww
ring      13 14
```

Legend: `#` = transmitting for at least 0.5 s of that second, `+` = less than 0.5 s, `.` = silent.
`others` includes CRC-error frames.

In 19:26:00-19:27:00 the channel is silent most of the time (the `w` cells), yet the priority-3 gate opens
at only three single seconds (`_`). DK5EN-98 sent one frame in that minute while 8 entered the ring.

## QRS/QRT scenario

### Thresholds (`backpressure.h`, ring of 20)

| State | Enters at                                                                                           | Effect                                      |
| ----- | --------------------------------------------------------------------------------------------------- | ------------------------------------------- |
| QRS   | depth >= 5 (`QRS_MIN_DEPTH`) and the 3rd own message on that ring (`QRS_MIN_USER_MSGS`)             | notice "slow down"; messages still accepted |
| QRT   | depth >= 16 (80 % of the ring) when an own message is sent, or an own message dropped at that depth | own typed messages refused ("QRT NOT SENT") |
| QRV   | depth 0, or depth 1 held 10 s (`QRV_HOLD_MS`)                                                       | QRT released, notice "QRV"                  |

QRT refuses only messages typed on BLE, web, serial console, EXTUDP or the T-Deck GUI
(`loop_functions.cpp:4515`). Relays, server traffic, ACKs, beacons, NBR reports and retransmits are never
refused; retransmits of own messages are even queued during QRT. QRT is not released on the way down
through 15...2; that hysteresis is deliberate (no flapping), but the release then depends on the ring
nearly emptying.

### What happened

```text
19:14:12  ##########.....|....  10      (logger connects, ring already at 10)
19:15:55                        <-- QRS at depth 11
19:16:28                                (first drops: 2 stale background frames)
19:16:41  ##..:..........|....   2
19:20:41  ###########....|....  11
19:24:41  #######........|....   7
19:26:41  ###########....|....  11      (5 server msgs + retries arrive)
19:27:41  ##############.|....  14
19:28:41  ###############|....  15
19:29:00                        <-- QRT at depth 16
19:29:41  ################....  16
19:30:41  ##############.|....  14      (below 16 since 19:30:00)
19:31:41  #########......|....   9
19:32:41  ######.........|....   6
19:33:41  #####..........|....   5      ("queue full" drops at depth 5)
19:34:41  #####..........|....   5      (inflow = outflow, ring stalls at 4-6)
19:35:41  #####..........|....   5
19:36:41  #...:..........|....   1
19:36:57                        <-- QRV at depth 0
```

`:` marks the QRS line (5), `|` the QRT line (16). Full series in `depth.txt`; per-minute table for every
minute in `analysis.txt`.

| Minute | Depth | Writes | Server | Own+retry | Sends | Drops | Notice |
| ------ | ----- | ------ | ------ | --------- | ----- | ----- | ------ |
| 19:15  | 7 9   | 6      | 2      | 3         | 2     | 0     | QRS@11 |
| 19:16  | 10 2  | 3      | 1      | 2         | 7     | 2     |        |
| 19:20  | 9 11  | 7      | 2      | 4         | 2     | 2     |        |
| 19:21  | 10 9  | 6      | 2      | 4         | 3     | 4     |        |
| 19:26  | 8 11  | 8      | 3      | 1         | 1     | 1     |        |
| 19:27  | 13 14 | 7      | 5      | 1         | 2     | 3     |        |
| 19:28  | 15 15 | 5      | 1      | 2         | 3     | 1     |        |
| 19:29  | 16 16 | 6      | 2      | 1         | 1     | 5     | QRT@16 |
| 19:30  | 15 14 | 8      | 2      | 3         | 6     | 8     |        |
| 19:31  | 10 9  | 5      | 3      | 2         | 5     | 3     |        |
| 19:33  | 5 5   | 4      | 4      | 0         | 0     | 2     |        |
| 19:34  | 4 5   | 3      | 2      | 0         | 1     | 2     |        |
| 19:36  | 4 1   | 1      | 0      | 0         | 6     | 0     | QRV@0  |

Findings:

- **QRT lasted 7 min 57 s** (19:29:00-19:36:57). The capture shows no refused message: `[BP];refuse` lines
  reach the 2323 console, and there are none. The next own message entered the ring at 19:37:40, after the
  QRV. The app showed QRT, and no send attempt reached the node until the QRV.
- **Depth was below 16 from 19:30:00.4.** With the operator's release rule (below the QRT line) the node
  would have accepted messages again 6 min 57 s earlier.
- **Depth is not free space.** The ring counts as full when the write index reaches the read index; free
  slots behind a frame waiting at the read index are not reused. 11 of the 19 "queue full, no lower prio
  to evict" drops happened with 11 or fewer frames queued, 4 of them with 5 (19:33:52-19:34:56). The frame
  at the read index (`E4EBD085`, server relay) waited 11.5 min for airtime until a beacon evicted it.
- **41 frames were dropped** in 24.5 min: 19 evicted by higher priority, 19 refused as "queue full", 3
  stale background frames. 25 server messages never went out. Server messages that did go out had waited up
  to 437 s.
- **Refusing own messages protects little.** Own messages have priority 1-2 and already evict queued server
  traffic when the ring is full. But an own message the ring refuses below depth 16 is lost without a
  notice (verdict Finding 5).
- **The ring stalled at 4-6 from 19:32:41 to 19:35:41** because server inflow matched the outflow. QRV came
  only when the server traffic paused (19:36, 0 server writes).

### Release rule: operator decision

Release QRT as soon as the depth drops **below the QRT line (< 16)**; most aggressive, accepted until a
better mechanism exists. The review checked it on a host build of the back-pressure tests: emit one QRV at
release so the operator learns they can send again, stay silent if the ring returns to 16 in the same
episode (the per-message "QRT NOT SENT" covers it), and allow a further release QRV only after 60 s.
Exactly two existing tests encode the old rule and must be replaced (verdict M1).

## Own message retries

| msg_id   | Own TX | Heard copies | Echoes ignored (slot queued) |
| -------- | ------ | ------------ | ---------------------------- |
| 1AE1E2E5 | 4      | 5            | 4                            |
| 1AE1E2E6 | 4      | 1            | 1                            |
| 1AE1E2E7 | 4      | 4            | 4                            |
| 1AE1E2F6 | 4      | 3            | 3                            |
| 1AE1E2F8 | 4      | 2            | 2                            |
| 1AE1E2EE | 1      | 6            | 1                            |
| 1AE1E2E2 | 1      | 4            | 2                            |

20 `ACK_SKIP_READY` in total. Five messages ran the full ladder; five others had a queued echo ignored but
were released later by an echo after they were sent. Example `1AE1E2F6` (group 262, "Es ist Kuh-Tee Zeit"):

```text
19:26:56  queued (user_msg), ring 11
19:26:59  heard DK5EN-98,DK5EN-92>262     echo ignored: slot queued (DK5EN-98 has not sent it yet;
                                          DK5EN-92 got it from the server and sent it first)
19:27:06  TX #1 (waited 9.4 s)
19:27:45  no echo in 40 s -> retry 1 queued, TX #2 19:27:52
19:28:31  retry 2 queued
19:28:39  heard via DK5EN-1 (server copy)  echo ignored: retry slot queued
19:28:58  heard via DK5EN-92,DK5EN-14      echo ignored: retry slot queued
19:30:04  TX #3 (waited 92.6 s)
19:30:56  TX #4 (retry 3, queued during QRT), 19:31:38 give-up
```

The message was on air 7 times. The echoes came from other gateways' server copies, not from neighbours
relaying DK5EN-98's own transmissions; those were already deduplicated. Every echo arrived while the next
copy sat queued, because the queue was slow, and the extra copies made the queue slower again.

Own DMs to DG0OPK-11 (`1AE1E2EB`, `1AE1E2F0`) and DM4SJ-10 (`1AE1E2F9`) went out 4 times each over RF with a
new retry id per attempt, were relayed by the bench nodes, and all three gave up (`RETRANSMIT_GIVEUP_DM`).
The DMs had already reached the server, because DK5EN-98 is a gateway (`TX-UDP` at 19:20:24 for
`1AE1E2EB`).

DK5EN-98 also re-sent one of its own messages that came back from the server (`1AE1E2CD`, queued as server
traffic and sent 311 s later with path `DK5EN-98,DK5EN-98`).

## DB0TAW-14: the same starvation on a hilltop, worse

**Source.** DG0OPK sent an email at 19:19 with a web GUI screenshot from 19:12:30, plus four saved web GUI
pages from 19:23-19:24 (`dg0opk-db0taw14/`). DG0OPK and DB8ZP updated DB0TAW-13 and DB0TAW-14 to 4.40a
over the air today. Since then DB0TAW-14 is reported to have rebooted several times. Their question: "Too
much signal received and the node is overloaded? TX ring buffer full?"

**Node.** DB0TAW-14 runs a fork build (the LoRa Queue card and the Path and Neighbours pages exist only
there). It is a relay (Mesh Y, Gateway N, `Relay mode: off`) at the Rhön site, next to the gateway
DB0TAW-13 (Heltec Stick V3, JO50AN, 835 m). Its neighbour matrix holds 47 stations from the last 12 h, 24 of them
heard directly, at up to 122 km; its path list reaches 54 stations over up to 4 hops. 17 direct neighbours are reached only through DB0TAW-14 ("Mesh needed").

**What the snapshots show:**

| Time     | TX ring               | Back-pressure | RX     | TX    | Dedup window | New ids / dups (5 min) |
| -------- | --------------------- | ------------- | ------ | ----- | ------------ | ---------------------- |
| 19:12:30 | 19/20, all priority 3 | QUIET         | 43.4 % | 0.0 % | about 20 min | 25 / 61                |
| 19:23:40 | 16/20, all priority 3 | QUIET         | 40.9 % | 0.1 % | about 17 min | 30 / 52                |

- **Yes, the TX ring was full, and the node did not transmit.** With 16-19 relays queued, it sent nothing in
  the 5 minutes before 19:12:30 (0.0 %) and at most one short frame before 19:23:40 (0.1 % = 0.3 s). The
  channel was 41-43 % busy with receptions, much more than DK5EN-98's 26 %.
- **This is the DK5EN-98 mechanism, worse.** All queued frames are priority 3 (relays), so each waits for a
  4.5 s gap after every received frame. At 41-43 % receive load, on a hilltop hearing 24 direct neighbours
  plus their weak, distant preambles, those gaps practically never come. DK5EN-98 at 26 % still sent
  3.2 frames per minute; DB0TAW-14 sent none. A full ring then drops every further relay as "queue full",
  and the 17 neighbours that only DB0TAW-14 reaches lose their link.
- **Back-pressure QUIET at 19/20 is not a contradiction, but it is misleading.** The card shows the state
  machine for the node's own typed messages, which only changes when someone sends from that node. A pure
  relay never leaves QUIET, however full the ring is (verdict Finding 11).
- **The traffic is mostly remote.** Most text lines in both RX log snapshots come from originators hundreds
  of km away (OE5HWN-12 in Austria, DM4SJ-10 near Heidelberg, DK5EN-98 in Freising). A gateway
  put them on air and they were relayed up to four hops (`DK5EN-98,DJ1JR-99,DB0IBH-90,DB0DOL-1,DG0OPK-12`);
  the same id often arrives twice within seconds (`2EFB018E`, `E4EBD085`, `47DE0422`). This is the
  every-gateway-sends-everything pattern from DK5EN-98, seen from a relay on the receiving side; DB0TAW-13
  is one of those gateways and sits next to DB0TAW-14.
- **Dedup window far too short.** At this load the 100-entry dedup ring covers only 17-20 min. The
  2026-08 DG0OPK analysis measured packet lifetimes up to 40 min and set the safe corridor at 40-48 min, so
  late copies can be accepted as new and relayed again. Tonight's queue waits of up to 11.5 min make
  lifetimes longer, not shorter. The earlier conclusion stands: age the dedup ring by time (about 45 min),
  do not just raise the count (`dedup_functions.h`).

**Reboots: most likely the task watchdog in the web server, not the heap.**

The operator hypothesis was: the node hears a lot, something piles up in RAM (for example the BLE out
buffer when no phone is connected), the heap runs out, the node reboots. Checked against tag
`v4.40a.10.18` (env `heltec_wireless_stick`, ESP32-S3, no PSRAM; the same holds for .16 and .17):

| Store that takes an entry per heard frame or station | Memory                          | When full                         |
| ---------------------------------------------------- | ------------------------------- | --------------------------------- |
| TX ring                                              | static, 20 x 260 B              | drops or evicts by priority       |
| Dedup ring, own-id ring                              | static, 100 x 5 B, 20 x 5 B     | wraps                             |
| BLE out buffer (`addBLEOutBuffer`), phone or not     | static 3072 B byte ring         | evicts the oldest frame           |
| BLE command buffer                                   | static                          | not filled without BLE or web     |
| UDP out (gateway), external UDP, display queue       | static, 3072 B / 2 / 1          | stays empty (gateway off) / wraps |
| Neighbour matrix (MHeard, path and matrix pages)     | static, about 13.9 KB, 128 rows | 47 stations = 37 %                |
| Message store                                        | static, 50 slots                | refuses new entries               |
| Web RX log                                           | heap, 2.6 KB once               | allocated on first view, wraps    |

Nothing allocates per heard frame and nothing grows without bound; the receive path has no growing
container. Per frame, under 300 B of heap is borrowed and returned in the same call. A Heltec V3 on the
same code shows 119-129 KB free at runtime. **The pile-up hypothesis does not hold**, and no code reboots
a node on low heap.

What does fit, and has happened before:

- **The web server runs inside the main loop** (`loopWebserver()`, loop section `web`), one client per
  pass. Pages are streamed line by line; `WiFiClient::write` waits up to 10 x 1 s per write, and nothing
  in the page code feeds the 5 s task watchdog. The MHeard (79 KB) and path (54 KB) pages reload
  themselves every 30 s while their tab is open.
- **Same failure on record:** DK5EN-1 on 2026-09-28 had a TASK_WDT reset 42 s after an OTA boot, with a
  web GUI tab open and a weak WiFi link; the web serve path was the only candidate left
  (`docs/soak-20260928-verdict.md`, Finding 3).
- **DB0TAW-14 matches that pattern:** OTA update today, web GUI open (DG0OPK saved four pages and a
  screenshot), a hilltop WiFi link of unknown quality, and 41-43 % receive load on the same loop.
- **Other candidates, lower:** a watchdog in `lora_rx` during an auto-update download (only with update
  mode 2), brownout on the site supply.

**The decisive check is one line:** `--info` after the next reboot. `BOOT RESET_REASON=6 TASK_WDT
LAST_LOOP_SECTION=web` confirms the web path; 4 PANIC with a falling heap would bring the heap back; 9
or 1 points to power. The remote-management status reply carries `up=` and `heap=` too, so the heap
trend can be read without the web GUI. A further hint: reboots that stop when no MHeard or path tab is
left open.

**Reproducing it on the bench, airgapped.** Feasible, but it needs four small INSTRUMENT-only changes
first (details in `review/r2-airgap-replay.md`):

1. **`--airgap inj`.** Today `--airgap on` drops every injected frame before it is processed
   (`lora_functions.cpp:963`), because `--injectraw` runs through the same OnRxDone. The new mode keeps
   dropping real RX and all own TX but lets injected frames through the full path (decode, dedup,
   neighbour matrix, BLE buffer, relay enqueue). About 15 lines.
2. **Airtime booking (`--airgap inj toa`).** A TX dropped by the airgap costs no airtime and frees its
   slot at once, so the ring would never fill as on DB0TAW-14. Adding the frame's time on air to the hold
   after a dropped TX, and booking injected RX airtime into channel use, fixes that.
3. **Longer frames.** The console command buffer limits `--injectraw` to 143-byte frames (11 of the 288
   captured frames are longer); an INSTRUMENT intercept in the outer command handler (600-byte buffer)
   lifts it.
4. **One sample line** with current and minimum free heap, the largest free block (fragmentation), TX
   ring depth and loop-task stack high water.

Then a replay tool (`tools/bench/rx_replay.py`, stdlib) feeds tonight's 288 captured frames, or a
synthetic mix (frames per minute, duplicate ratio, number of sources), over the 2323 console. Duplicates
get the path token appended, the hop count decremented and the checksum recomputed; msg_ids are
rewritten per round so the dedup ring cannot swallow repeats. The console takes one byte per loop pass,
so injection costs about 1-1.5 s per average frame; that covers DB0TAW-14's average rate (one frame per
3.7 s), not its fastest duplicate bursts.

Bench runs on DK5EN-1 (INSTRUMENT image, 2 dBm, gateway and EXTUDP off so injected frames cannot leave
through the server, a second node as witness that DK5EN-1 sends nothing; after any reboot the airgap must
be re-armed before injecting again, because it is RAM-only):

| Arm | Load                                                                                | Duration |
| --- | ----------------------------------------------------------------------------------- | -------- |
| A   | idle baseline                                                                       | 30 min   |
| B   | DB0TAW-14 profile: 16 frames/min, 60 % duplicates, 40 sources, airtime booking on   | 2 h      |
| C   | stress: 35 frames/min, 120 sources                                                  | 1 h      |
| D   | B plus MHeard, path, neighbour and RX log pages polled every 30 s, BLE up, no phone | 2 h      |
| E   | only if B-D show nothing: gateway on against the local mock server                  | 2 h      |

Pass: no reboot, heap slope under 1 KB/h, minimum free at least 20 KB above the idle minimum, largest
free block not falling, no loop gap over 1 s, zero inject errors (otherwise the run is void), witness
hears nothing. Arm D is the one expected to reproduce the watchdog; to provoke the weak-link case, put
the browser on a poor WiFi link or throttle it.

**What DB0TAW-14 needs (operator, no firmware change):**

1. **`--nbrrelay on`** (off by default; switches on the relay classification and the overhear cancel).
   It drops nothing at the door: every relay is still queued, but classified by the neighbour matrix.
   - **Case A** (a neighbour that needs the frame hears only this node): 3.5 s base, and 150 ms after
     8 s of waiting (`txring_functions.cpp:332-334`), which no other traffic gets today. Never cancelled.
     With 17 of 24 direct neighbours exclusive to DB0TAW-14, many of its relays should be case A.
   - **Case B** (every neighbour that needs it also hears someone else): text keeps the normal 4.5 s
     hold; position and HEY wait an extra 20 s first. When another node's copy is heard and its listeners
     cover everyone who still needed it, the queued copy is removed (`cancelled`).
   - **Unknown** (empty matrix or unparsable path): handled as today.
   - **Counters on the Neighbours page:** `relays A/B` are queued relays per case; `cancelled` are queued
     copies removed because another copy covered them; `refused (sole provider)` counts the opposite,
     copies kept although someone else was heard sending, because a neighbour depends on this node alone.
     DK5EN-98 tonight: A 1011, B 1, cancelled 0, refused 130.

   This attacks the 0 % TX through the case-A shortcut, and the queue through cancels. Trial with the web
   GUI open and watch TX %, ring depth and these counters.

2. **`--info` after the next reboot** for `BOOT RESET_REASON` (6 TASK_WDT with `LAST_LOOP_SECTION`,
   4 PANIC, 3 SW, 15 BROWNOUT).
3. **`--setlog on`** for the 5-min STAT line (`util`, `txn`, `ringmax`, `drop`, `heap`).
4. If possible, a `--loradebug on` console capture of 10-20 min (`meshlogger.py`) for the gate counters
   (`TX_GATE_ENTER`, `RX_TIMEOUT_DEFERRED`, `CAD_BUSY`, `RING_DROP_NEW`). That separates CSMA starvation
   from a blocked TX path and shows whether the dead stale-preamble guard (verdict Finding 9) costs extra
   holds at this site.

### DB0TAW-14 restarts: heap hypothesis checked

**Source.** DG0OPK-11 to DK5EN-98, 19:07:17 (via server): "tx puffer bei DB0TAW-14 schon mehrfach im
überlauf...node startet dann neu" (TX buffer at DB0TAW-14 overflowed several times, then the node
restarts). DB0TAW-14 is not on the map; its sibling DB0TAW-13 is a "HELTEC STICK V3" gateway on 4.40a
(Rhön, JO50AN), probably running upstream firmware.

**Operator hypothesis.** The node runs out of heap because it hears too much.

**What the review found:**

- **"TX buffer full" is the back-pressure notice**, not a memory message. "QRT - stopping to accept new
  messages, TX buffer full" and "QTA - message discarded, TX buffer full" (`backpressure.h:154-157`, same
  upstream) go to the app of an operator who sends while 16 of 20 slots are taken. They report overload.
- **A full ring neither allocates nor reboots.** The TX ring and its side arrays are static; a drop only
  sets flags and prints. Nothing restarts the node on a full ring or on low heap.
- **Under the same overload, DK5EN-98's heap stayed flat** at 110.5-111.7 KB in every 5-min STAT line, no
  restart (uptime 11 h). STAT `heap=` is a snapshot, not the minimum.
- **Restart cause candidates:**
  - **Task watchdog:** armed at `esp32_main.cpp:2389`, 5 s, panic on. OnRxDone runs in the loop task, so a
    loop stalled for more than 5 s under heavy RX trips it. Upstream lacks the fork's console TX buffer
    (`CONSOLE_UART_TXBUF`), so every console print in the RX path blocks the loop at 115200 baud there;
    that adds to a stall but cannot cause one alone.
  - **Panic:** heap exhaustion is one possible cause; a failed `new` panics, failed String/JSON allocations
    fail silently.
  - **Unrelated reset:** brownout, update, command.
- **No coredump on the Stick V3:** the 4 KB coredump partition is too small for an ELF dump; the console
  panic text is the only forensic channel.

**Test plan:**

1. Ask the owner for `--info` after the next restart: `BOOT RESET_REASON` 6 TASK_WDT = loop stall (with
   `LAST_LOOP_SECTION`), 4 PANIC = crash, 3 SW = deliberate restart, 15 BROWNOUT = power.
2. Ask which app showed the text and its exact wording (QRS/QRT/QTA, "NOT SENT").
3. `--setlog on`: STAT lines with `ringmax`, `drop` and `heap` every 5 min.
4. Capture the console until the next restart; look for "Task watchdog got triggered", "Guru Meditation",
   "abort() was called".
5. Bench: Heltec V3, fork vs upstream build, flood with RX from own nodes (group TEST, at most 2 dBm) while
   sending own messages; compare loop gap, ringmax, QRT count and reset reason. A fork build with
   `-DCONSOLE_UART_TXBUF=0` reproduces the upstream console behaviour.

## Recommendations

Details, file:line and test plans are in the verdict.

### A. Operator, now

0. **DB0TAW-14: `--nbrrelay on`**, then `--info` after the next reboot, `--setlog on` and a short
   `--loradebug` capture (section "DB0TAW-14").

1. **One gateway per site.** `--gateway off` on DK5EN-1 and DK5EN-92 while DK5EN-98 is the site gateway
   (`--nomsgall` and `--gateway nopos` do not cover server text), or take them off the air.
2. **`--nbrrelay on` on the bench nodes**, so the matrix suppresses their redundant relays.

### B. Mechanism improvements (firmware)

1. **QRT release below 16** with one QRV at release, rate-limited to one per 60 s (operator decision).
2. **Send in the gaps, age-gated.** For a frame queued 20 s or more while QRS/QRT is active, and not a
   neighbour-matrix relay: after another station's frame, hold only so long that our frame ends before the
   3.0 s ACK window of that frame's receivers (500-1500 ms plus jitter); after our own TX keep the normal
   hold. Replay on this capture: 11.2 sends/min (8.6 without overlap) vs 4.0 today, inflow 5-8/min. Needs
   its own concept and a bench run.
3. **Maximum age for server text** (about 90 s), by extending the existing 180 s sweep for background
   frames.
4. **Reuse free ring slots** (compact the ring when full by index but not by content), only together with
   the maximum age.

### C. Bug fixes

1. **Echo during queue is ignored:** release a queued own group/broadcast copy on a heard echo, and stop the
   retry ladder once `own_msg_id` says HEARD. DMs keep their ACK-only rule.
2. **Retries ignore QRT and evict server traffic.**
3. **Gateway sends server messages already on air** (BACKLOG GWRX-01): free the queued server copy when the
   same msg_id is heard on RF; any copy counts.
4. **Own message dropped below depth 16 gets no notice.**
5. **Own message reflected by the server is queued again.**
6. **Server text classified as relay (priority 3).**
7. **CSMA:** starved hold never escalates; stale-preamble guard dead at this noise floor; hold not
   recomputed on enqueue; `cad=` in the TX log always 0.
8. **KISS and RM traffic count toward back-pressure.**

Rejected after review: a slot reserve for own messages, a multi-gateway election, a shorter global hold for
priority 1-2, and a flat 1.5-2.0 s hold for all aged traffic (see the verdict).

## Open questions

- Should own DMs that already reached the server (gateway node) still run the full RF retry ladder, or send
  once unless the destination was heard on RF?
- Do the bench nodes need to stay gateways for testing, or can DK5EN-98 be the only site gateway?
- Order of work: the verdict suggests C1-C2 first, then C3 with B3, then B1, then B4, then B2.
