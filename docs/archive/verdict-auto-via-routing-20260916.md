# Verdict: automatic VIA routing from HEY/PATH data (Discussion #1146)

Date 2026-09-16. Trigger: [icssw-org/MeshCom-Firmware discussion #1146](https://github.com/icssw-org/MeshCom-Firmware/discussions/1146)
by s-m-arty (14:13 UTC), answered by OE1KBC at 15:28 UTC ("still having internal discussions
about the use of HEY PATH and also about using a new message ID ... leaning towards keeping the
existing message ID and using an additional sequential numbering system"). Every code line below
is verified against `fork-main`/`dry-unification` and `upstream/dev` (`4058b25b`, 2026-09-14);
every field number is from the sources in section 9.

Part A (sections 1 to 6) is the routing verdict. Part B (section 7) is the verdict on the
meshmap gateway-link question, which turned out to have a different binding cause than the
dedup ring. Section 8 lists the questions whose answers would change something here.

---

## 1. Short answer

### 1.1 For the discussion (English, 5 sentences)

The building blocks are real, but the PATH table is a trace of where one HEY beacon happened
to arrive, not a route: it holds only relayed HEYs, keeps the shortest path of the last 12
hours rather than the freshest, and knows nothing about the reverse direction, which in the
field differs from the forward direction by 5 to 12 dB. Today's firmware also strips the VIA
list at the first relay, so a multi-hop source route first needs new relay behaviour in the
whole fleet, and the one AUTO-VIA retry with the same msg_id runs into the same dedup defect
as today's 40 s retry (every node that heard the first attempt drops it, including the
destination). A chain of named relays multiplies loss and sums queue latency (30 s median,
286 s p99 per hop at a loaded site) where the flood takes the fastest of many relays. DMs are
under one percent of frames, so the airtime prize is invisible while the risk sits on the one
traffic class users watch. We would keep the goal and reach it differently: a soft relay
preference on top of the flood (named or well-connected relays go first, the rest only if no
copy was heard) plus the two-identifier ARQ (fresh msg_id per retransmission, stable sequence
number) that our DM transport work already implements.

### 1.2 Fuer das Gespraech mit Kurt (Deutsch, kurz)

Die Idee ist verstaendlich, aber die PATH-Tabelle ist keine Route: sie enthaelt nur relayte
HEYs, behaelt zwoelf Stunden lang den kuerzesten statt den frischesten Pfad und weiss nichts
ueber die Gegenrichtung, die im Feld 5 bis 12 dB anders ist. Die heutige Firmware wirft die
VIA-Liste beim ersten Relay weg, ein Mehrhop-Source-Routing braucht also zuerst neues Verhalten
in jedem Relay der Flotte. Der eine AUTO-VIA-Retry mit derselben msg_id laeuft in denselben
Dedup-Fehler wie der heutige 40-s-Retry, und die Kette aus benannten Relais multipliziert
Verluste und summiert Warteschlangen, waehrend die Flut den schnellsten Weg nimmt. DMs sind unter
einem Prozent der Frames, der Gewinn ist also klein, das Risiko sitzt genau dort, wo Nutzer
hinschauen. Unser Gegenvorschlag: weiche Relay-Bevorzugung statt harter Routen, und die
Zwei-Kennungen-Quittung (frische msg_id je Wiederholung, stabile Sequenznummer), die in der
DM-Transportsicherung schon gebaut ist. Zu Kurts Satz "bestehende msg_id behalten plus
Sequenznummer": entscheidend ist, welche der beiden Kennungen bei einer Wiederholung neu ist,
sonst schluckt jeder Altknoten den Retry.

### 1.3 Talking points for the call (17 points)

The operator's own list of seven, checked and completed. Section references point to the
evidence.

**Why the PATH table cannot carry a route**

1. **The HEY path is the receive direction, we want to send, and links are asymmetric.** A
   path records who heard whom with the receiver's RSSI; reversing it assumes symmetry. Measured
   in Freising: 5 to 12 dB between the two directions (DC2MAC-1/DL2JA-2: 11.6 dB). At SF11 that
   is the difference between a link that works and one that does not (3.2).
2. **The table keeps the shortest path of the last 12 hours, not the freshest.** A 2-hop path
   from 11 hours ago blocks a fresh 3-hop path. And it holds only relayed HEYs, so direct
   neighbours are never in it: a DM to a neighbour has no "route" at all (3.1).
3. **Today's firmware discards the multi-hop VIA list at the first relay.** From hop 2 on
   everything floods. Preserving the list is a relay-path change on all 1,449 reporting nodes,
   762 of them on 4.35p; the mixed-fleet transition is the risk, not the sender (3.3).
4. **It is source routing: if any named relay does not hear or does not get on air, the packet
   is gone.** Not only gateways, every relay in the chain. The chain sums queues (30 s median,
   286 s p99 per hop at a loaded site) where the flood takes the fastest of 2 to 4 relays. The
   flood's redundancy is exactly what the proposal spends (3.4).
5. **Keeping the msg_id means the retry dies in the dedup filter.** Every node that heard the
   first attempt drops it, including the destination, so a lost ACK can never be repaired. The
   40 s timer fires before the round trip (1 min quiet, 4 min median loaded), the route is
   blocked for a timing reason, and the flood with a new msg_id comes on top: VIA plus flood,
   more airtime than the flood alone (3.5).
6. **The table size differs per board and wraps today.** 100 (S3, nRF52), 40 (classic ESP32,
   64 % of the fleet), 10 (one class). Round-robin overwrite; a well-placed node sees far more
   origins in 12 hours (3.1).
7. **No persistence, RAM only.** SD card on the T-Deck only. After every reboot the table is
   empty and refills at trickle pace (up to 15 min per origin) while trickle suppresses 29 to
   75 % of HEYs (3.1).

**What else weighs against it**

8. **The prize is invisible.** DMs are under 1 % of frames (1,182 in 7 days against about
   3,800 HEY and 3,800 positions per hour). Network airtime changes by an unmeasurable amount;
   the risk sits on the one frame whose loss the user sees (3.6).
9. **The ACK floods anyway.** `:ackNNN` is a normal text frame; the saving is on the forward
   half only (3.4).
10. **The VIA list costs bytes.** About 20 bytes per named hop, roughly 0.24 s at SF11 per copy
    (3.6).
11. **Gateways drop out.** A gateway not named in the list does not relay, and those are usually
    the best-sited nodes. The server upload still happens, which is the good part (3.7).
12. **Kurt tried it himself.** The mheard-based auto-VIA branch was in the code and was removed
    "zum Test" on 2026-07-22 (`c47e4993`). Asking why is the best opening (3.1, 5).
13. **Two small defects in the existing gate:** substring match (`OE1KBC-7` matches inside
    `OE1KBC-71`) and an unordered list, every named node relays as soon as it hears (3.3).
14. **Route state costs RAM where there is none:** classic ESP32 has about 6.6 kB headroom
    (3.5).

**What to offer instead**

15. **Soft relay preference instead of hard routes.** The flood stays; named or well-connected
    relays get the early CSMA slot, everyone else the late one and cancels on hearing a copy.
    Same saving in the good case, full redundancy in the bad case, no wire change, old nodes
    flood as today (4.2).
16. **The one identifier question.** Kurt says "keep the msg_id, add a sequence number". What
    decides is which of the two is fresh on a retransmission. Fresh msg_id plus stable
    sequence number works with every old node, and the sequence number already exists as
    `{NNN`. Same msg_id with a new sequence number is swallowed by every old node until the
    whole fleet has upgraded (6).
17. **Measure first.** Nobody measures DM outcome today. Stage 0 of the DM transport work adds
    the counters (sent, echo, gateway ACK, peer ACK, gave up). Without before/after numbers no
    PoC can be judged (4.3).

---

## 2. What the proposal gets right

- **Scope discipline.** DMs only, no new over-the-air protocol, flood as the fallback,
  broadcasts untouched. That is the right shape for anything that touches the relay path.
- **The dedup consequence is seen.** "A new msg_id for the flood fallback is necessary because
  deduplication is based on the message ID" is exactly the root cause of our defect 1 in
  `docs/proposal-dm-transport-reliability-20260909.md` section 2. Few people spot it.
- **The building blocks exist.** VIA gate (`src/via_functions.cpp`), PATH table
  (`src/mheard_functions.cpp`, `updateHeyPath()`), end-to-end ACK (`{NNN` / `:ackNNN`),
  originator retransmission. Nothing has to be invented to try it.
- **Where it would pay.** In a dense urban cluster every relay of a DM is redundant (see
  `docs/prio-talk-flood-networking.md`, scenario "fully meshed"): a flood costs 2 to 5
  transmissions per hop layer, a source route costs one. On a 66 % CAD-busy channel that is a
  real local saving for that one message.
- **Route quality from RSSI, SNR, hop count and ACK history** is the right long-term input.
  It is also exactly what our "ant trail" proposal (deck slide 15, "Der Vorschlag: Routing")
  wants to feed into relay priority rather than into path selection.

---

## 3. Why source routing from the PATH table does not hold

### 3.1 The table is a trace, not a route

What `updateHeyPath()` actually stores (`src/mheard_functions.cpp:460-640`):

| Property                | Code                                                                | Consequence for routing                                                       |
| ----------------------- | ------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Only relayed HEYs       | `if(ips <= 0) return;` (no comma in the source path means no entry) | Direct neighbours are never in the table. A DM to a neighbour has no "route". |
| Keyed by origin         | one slot per HEY origin callsign                                    | One path per destination, no alternatives, no per-link state.                 |
| Shortest path wins      | `if((mheardPathLen[ipos] & 0x7F) < mheardLine.mh_path_len) return;` | A 2-hop path seen 11 h ago beats a 3-hop path seen 1 min ago.                 |
| 12 h ageing             | `MHEARD_PRUNE_WINDOW_MS`, pruned only when a HEY arrives            | "Fresh" in the proposal has to be defined first; today it means "under 12 h". |
| 51-character path field | `mheardPathBuffer1[MAX_MHPATH][52]`                                 | About five callsigns; longer chains are truncated silently.                   |
| Size per board          | `MAX_MHPATH` 100 (S3, nRF52), 40 (classic ESP32), 10 (one class)    | Classic ESP32 is 64 % of the fleet and can hold 40 destinations.              |
| No persistence          | SD card on T-Deck only                                              | Every reboot starts with an empty table.                                      |
| Fed by trickled beacons | `TRICKLE_IMIN_S` 30 s to `TRICKLE_IMAX_S` 15 min, k = 2             | A node suppresses 29 to 75 % of its own HEYs (`docs/hey-supp.md`).            |
| `--shortpath` nodes     | source path cut to "origin, last relay" before relaying             | Their paths cannot be reversed; the table cannot tell them apart.             |
| Never read by TX logic  | all readers are console, T-Deck display, web GUI, SD (deck slide 8) | Kurt's own dynamic branch was removed "zum Test" on 2026-07-22 (`c47e4993`).  |

The deck says it in one line: "Ein Pfad, den ein Paket gestern genommen hat, ist keine Zusage,
dass er morgen noch existiert."

### 3.2 The reverse direction is a different measurement

The proposal reverses a heard path ("Known path D-C-B-A, transmission from A: B,C,D"). A HEY
path records who heard whom, in one direction, with the receiver's RSSI. Live data from the
map for the Freising cluster, 7-day window, both directions of the same physical pair:

| Direction 1 (receiver first)                         | Direction 2 (receiver first)                        | Delta RSSI / SNR |
| ---------------------------------------------------- | --------------------------------------------------- | ---------------- |
| DL2JA-2 hears DC2MAC-1: -110.0 dBm / -7.6 dB (282)   | DC2MAC-1 hears DL2JA-2: -121.6 dBm / -15.3 dB (8)   | 11.6 / 7.7 dB    |
| DL2JA-2 hears DL3NCU-1: -115.3 dBm / -11.9 dB (146)  | DL3NCU-1 hears DL2JA-2: -115.3 dBm / -4.7 dB (3)    | 0.0 / 7.2 dB     |
| DL2JA-2 hears DB0HOB-12: -119.6 dBm / -14.6 dB (135) | DB0HOB-12 hears DL2JA-2: -124.3 dBm / -9.2 dB (497) | 4.7 / 5.4 dB     |
| DL2JA-2 hears DB0ED-99: -97.8 dBm / +1.6 dB (334)    | DB0ED-99 hears DL2JA-2: -91.6 dBm / +5.3 dB (69)    | 6.2 / 3.7 dB     |
| DK5EN-98 hears DB0ED-99: -120.0 dBm / -9.0 dB (268)  | DB0ED-99 hears DK5EN-98: -115.3 dBm / -6.4 dB (67)  | 4.7 / 2.6 dB     |

Sample counts are also shaped by the feed problem in section 7, so read the dB columns as the
evidence. At SF11 the decode margin is a few dB; a 5 to 12 dB asymmetry is the difference
between a link that works and one that does not. The DG0OPK site adds the reason: the noise
floor there is -86 to -94 dBm against a theoretical -114 dBm, and it is site-specific, so the
node with the quiet site hears far more than it is heard.

### 3.3 Today's VIA mechanism only survives one hop

`checkMesh()` (`src/via_functions.cpp:50-93`) lets a node relay a VIA frame only if its own
callsign is in the destination path. But the relay itself rewrites the frame
(`src/lora_functions.cpp:1486-1488`, upstream `:1460-1462`):

```cpp
aprsmsg.msg_destination_path = aprsmsg.msg_destination_call;
checkVia(aprsmsg);   // re-adds a path only from the static --setvia setting
```

So `A>B,C,D:` leaves B as `A,B>D:` with no VIA list. From the second hop on, every node
floods as usual. The proposal's three-hop example is therefore not possible with any firmware
in the field today; it needs every relay to preserve and consume the remaining list. That is
a behavioural change in the relay path of all 1,449 reporting nodes (fleet today: 304 on
4.35t, 122 on 4.35s, 762 on 4.35p, 261 older), and the mixed-fleet transition is the risky
part, not the sender.

Two smaller defects in the existing gate: the membership test is a substring match
(`indexOf(node_call)`, so a path naming `OE1KBC-71` also triggers `OE1KBC-7`), and the list is
unordered, so every named node that hears the frame relays it whether or not it is the next
hop.

### 3.4 A chain multiplies loss and sums latency; a flood takes the minimum

Per-hop relay success in the quiet DG0OPK cluster is 99.2 to 99.7 % given reception. What
decides delivery is not that number but the queue in front of each relay:

| Measured at DG0OPK-11 (loaded site, 47.6 h) | Text relay | Position relay |
| ------------------------------------------- | ---------- | -------------- |
| RX to own relay on air, median              | 30.0 s     | 182 s          |
| p99                                         | 286 s      |                |
| max                                         | 985 s      | 2197 s         |
| CSMA windows won                            | 21.4 %     |                |

Siblings at the same site were 2 to 4 times faster. In a flood the DM takes whichever of the
2 to 4 relays that heard it gets on air first; the slow node's queue is invisible. In a source
route through B, C, D the DM waits in B's queue, then C's, then D's, with no alternative, and
one busy relay is the delivery time. With loss p per hop, a 3-relay chain delivers with
(1-p)^3; the flood delivers if any of the parallel relays succeeds. The flood's redundancy is
the property the proposal spends.

The ACK does not benefit either: `:ackNNN` is a normal text frame and floods back through the
mesh (`docs/ack-wer-hat-quittiert.md`). The saving is on the forward path only, half of the
round trip.

### 3.5 The failure ladder inherits the two defects we are fixing

- **Same msg_id retry.** "One AUTO-VIA retry" with the original msg_id is dropped by every
  node whose dedup ring holds the id, which is every node that heard the first attempt,
  including the destination (`is_new_packet()` at `src/lora_functions.cpp:679`; ring
  window 28.8 to 52.3 min, `src/dedup_functions.h`). The retry only ever helps if the first
  hop never heard the DM. That is defect 1 of the DM transport proposal, and it is why every
  re-flood there gets a fresh msg_id and a stable sequence number (`{NNN`).
- **Timer versus round trip.** The existing retry fires at about 40 s (`MAX_RETRANSMIT` 3),
  the measured round trip is about 1 min on a quiet network and 4 min median on a loaded one.
  A route that is "temporarily blocked" because the ACK did not arrive in 40 s is blocked for
  a timing reason, not a routing reason. The fallback then floods with a new msg_id, so the
  common case on a loaded network is VIA plus flood: more airtime than the flood alone.
- **Route state on classic ESP32.** Blocked routes, retry counters and alternates need RAM
  on boards with about 6.6 kB DRAM headroom after the planned fixes
  (`docs/mem-headroom-classic-esp32-20260905.md`).

### 3.6 The prize is small and the risk sits on the visible traffic

Volumes from the map server, trailing 7 days to 2026-09-09: 1,182 direct messages from 185
senders, against 2,601 group messages and 3,600 to 4,600 HEY beacons plus about 3,800
positions per hour network-wide. DMs are under one percent of frames. Even a perfect DM
routing scheme changes network airtime by a number nobody can measure, while the DM is the one
frame whose loss a user sees. The VIA list itself costs about 20 bytes per named hop on the
wire (0.24 s at SF11/BW250 per transmission), so a three-hop list adds roughly half a
second per copy.

### 3.7 Interaction with gateways and the server

The gateway upload happens before the mesh decision (`src/lora_functions.cpp:1390-1415`),
so a VIA-routed DM is still uploaded by every gateway that hears it and the server path keeps
working. But a gateway that is not named in the list does not relay, so the internet-bridged
half of the network loses the RF relay of exactly the nodes that usually have the best sites.

---

## 4. What we would do instead

### 4.1 Reliability first: the two-identifier ARQ (already built, not yet bench-tested)

`docs/proposal-dm-transport-reliability-20260909.md` and the campaign state in
`docs/dm-transport-impl-plan-20260913.md`: stage 0 (echo-gated same-id retry, re-ACK on
duplicate-for-me, failure report to the app), stage 1 (outbox and RTT-spaced ladder behind
`--dmretry`, fresh msg_id per re-flood, stable `{NNN`), stage 2.1 (destination dedup on
source plus NNN), stages 3 and 4 (store node, custody notice). None of it needs a new frame
type; old nodes see byte-identical frames. This is what "ACK failure handling" in the
proposal needs underneath it, whatever the forward path is.

### 4.2 Airtime second: soft relay preference instead of hard routes

Keep the flood, change who goes first. Two inputs already exist on every node: the learned
PATH (which relays carried this destination's beacon) and the neighbours' reported neighbour
counts (`R<NC>;`, ADR-02 `docs/adr-nc-importance-backoff.md`). A relay that is named in the
learned path to the DM's destination, or that has a high network importance, takes an early
CSMA slot; everyone else takes a late slot and drops its queued copy if it hears one first
(the "cancel queued relay when a copy is heard" step analysed in
`docs/prio-talk-flood-networking.md`). Result: in the good case the DM travels the learned
path with one transmission per hop layer, exactly the proposal's saving; in the bad case the
other relays still carry it. No wire change, no route state, no failure ladder, old nodes
simply keep flooding as today. This is the deck's ant trail: local rules, global order.

### 4.3 Measure before either

The network has no measurement of DM outcome today. Stage 0 adds DM counters to the setlog
STAT line (sent, echo heard, gateway ACK, peer ACK, gave up, time to ACK). A PoC that cannot
show a before/after on those numbers cannot be judged, and we would say so in the discussion.

---

## 5. Answers to the four questions, as we would give them

1. **Is automatic VIA routing already planned?** Not in the fork. The dynamic branch in
   `checkVia()` was Kurt's, tried and removed in July; the upstream answer is his.
2. **Is the PATH table intended for routing decisions?** It is a diagnostic trace (section
   3.1). It could feed a relay preference; it cannot carry a route.
3. **Does the approach fit MeshCom's direction?** The goal does (reliable, cheap DMs). Hard
   source routing does not fit a flood network in a shared ISM band; a soft preference on top
   of the flood does.
4. **Would a PoC/PR be welcome?** A PoC that first proves the relay path can preserve a VIA
   list across a mixed fleet, and that measures DM outcome before and after, would be worth
   reading. A PR that adds route state and a same-msg_id retry to the sender would not.

---

## 6. Kurt's "keep the msg_id, add a sequential number": the one question that matters

The wire already carries two identifiers per DM: the 32-bit msg_id (22 bits node id, 10 bits
counter, `docs/architecture/11-wire-format.md` section 1.1) and the 3-digit `{NNN` counter in
the payload. Today they are the same ten bits. Whatever the new scheme is called, one question
decides whether it works on the existing fleet:

| Scheme                                                       | Old node that heard attempt 1   | Destination that heard attempt 1 but whose ACK was lost | Server / map                        |
| ------------------------------------------------------------ | ------------------------------- | ------------------------------------------------------- | ----------------------------------- |
| Retry keeps the msg_id, sequence number changes              | drops the retry (dedup ring)    | drops the retry, never re-ACKs                          | sees one message                    |
| Retry gets a fresh msg_id, sequence number stays (our P2)    | relays the retry as a new frame | re-ACKs, shows nothing twice (dedup on source + NNN)    | sees a new msg_id, must fold on NNN |
| Retry keeps the msg_id, dedup key becomes (msg_id, sequence) | drops the retry until upgraded  | same until upgraded                                     | sees one message                    |

Only the second row works before the whole fleet has upgraded, and the "additional sequential
numbering system" it needs already exists as `{NNN`. If Kurt means the third row, the
transition is the same fleet-wide relay change as section 3.3, and the benefit arrives only
when the last 4.35p node is gone. The question to put to him is therefore not "new msg_id or
not" but "which of the two identifiers is fresh on a retransmission".

---

## 7. The meshmap gateway-link question

### 7.1 The hypothesis, tested

"The answer to a HEY path request has the same msg_id and runs into the dedup ring." There is
no request/answer pair in the HEY mechanism; a HEY is a beacon, and the receivers append their
report to the copy they upload or relay. The same-msg_id idea itself was tested twice:

| Date       | Where the copy dies                                                                                                                      | Result                                                                                                                                                                                                                             |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-08-18 | Node dedup ring (server pushes the HEY to the gateway first, RF copy is a duplicate)                                                     | **Refuted.** The server pushed 0 HEYs to DK5EN-98 in 105 min (86 pushes, all text). 9 of 9 directly heard gateway HEYs were `RX_DEDUP_NEW` and uploaded (`TX-UDP`). mcmap `docs/findings/data-gateway-link-blindspot.md` 9.7, 10.1 |
| 2026-08-18 | Server keeps one report per msg_id, the gateway's bare self-upload arrives 3 to 8 s earlier                                              | Observed then: 0 of 9 published; a copy 707 s late was published.                                                                                                                                                                  |
| 2026-08-31 | Same, dual-node bench DK5EN-93 / DK5EN-92 with the interlink log as reference                                                            | The server distributed **every** copy (bare self-upload plus two enriched). The loss was in whatever consumer keeps the first record per msg_id. Fix GW-01: no self-upload of the own HEY. `docs/BACKLOG.md` 3.8i                  |
| 2026-09-16 | Feed today: DL2JA-1 msg `E9F1108C` appears with four different receiver reports in the same second, then relayed copies 13 to 22 s later | No per-msg_id dedup in the feed today. GW-01 is in upstream 4.35t; DK5EN-98's own HEYs show 0 bare self-uploads and 11 frames with receivers in the first 50 MiB of the current log.                                               |

So: the ring is not it, the self-upload race was real and is fixed for 4.35t nodes, and the
map still shows no link for DL2JA-2 to DK5EN-98 (0 samples in 7 days, 0 frames
`"path":"DL2JA-2,DK5EN-98"` in the whole current log) although DK5EN-98 hears DL2JA-2 at
about -111 dBm every few minutes. Something else is binding now.

### 7.2 What the feed shows today: the receiver is anonymous

Control pair from the August finding, OE5HWN-14 heard by the gateway OE5HWN-12, both on
4.35t now:

```
2026-08-18   {"type":"hey","path":"OE5HWN-14,OE5HWN-12","rssi":"61,6", ...}
2026-09-16   {"type":"hey","path":"OE5HWN-14","rssi":"2,50,6;", ...}     (every 15 min, six checked)
```

The same physical reception. In August the path named the receiving gateway and the hub
appended its RSSI/SNR from the UDP header. Today the frame carries the receiver's report as the
firmware's own three-value group (neighbour count, RSSI, SNR), and no callsign. Counts in the
first 50 MiB of the live interlink log (622,690 lines):

| Frame shape                                                       | Count today | Count 2026-08-26                    |
| ----------------------------------------------------------------- | ----------- | ----------------------------------- |
| One-element path, report present, receiver unnamed (network-wide) | 9,028       | 209                                 |
| Same shape, origin DL2JA-1 only                                   | 90          | 27 (08-30), 83 (09-13)              |
| One-element path, no report (bare)                                | 34,718      |                                     |
| Two or more path elements (relayed copies)                        | 27,485      |                                     |
| Last segment a hub-appended two-value pair (receiver named)       | **0**       | **0**                               |
| Last segment a firmware three-value group                         | 29,567      |                                     |
| `"path":"DL2JA-1,DK5EN-98"` (DK5EN-98 named as receiver)          | 0           | 0 (also 08-28, 08-30, 09-06, 09-13) |

The growth of the first row from 209 to 9,028 tracks the 4.35t rollout: only nodes with the
in-payload report (fork since 2026-08-21, upstream since 4.35t) produce a one-element frame
with values; a 4.35p gateway's direct reception arrives bare, without values and without a
name.

Since some day between 2026-08-18 and 2026-08-26 the INTERLINK feed has not named the
uploading gateway for any upload, old firmware or new. A direct reception is now a
one-element path with a report, and mcmap only builds an edge when the path has two elements
(`proxy/src/interlink/ingest.ts:1209`, `pathNodes.length >= 2`); the single-path case is
handled there as "about 215 per day of flattened paths", which was true on the old feed and is
now about 9,000 per 50 MiB. The report is there, the receiver is not, so no map can draw the
link.

This is why it hits gateway-to-gateway pairs hardest: a gateway only appears in a path when it
relays, and a gateway with mesh off (DK5EN-98) or a gateway whose relays lose the CSMA race
never appears at all. DK5EN-98's report of DL2JA-1 is visible in the feed as
`"path":"DL2JA-1","rssi":"7,118,-12;"` (7 neighbours, -118 dBm, matches DK5EN-98 exactly), and
it is unattributable.

Two side effects worth knowing:

- A bare one-element frame is now ambiguous: a self-upload from pre-GW-01 firmware, or an
  upload by one of the 762 4.35p gateways that append nothing to the payload. The 94 bare
  DL2JA-2 frames per day (35 plus 59 in the current log) are most likely the latter; one
  console line on DL2JA-2 would settle it. mcmap's `selfReportShape` heuristic
  (`ingest.ts:1162`) marks the origin as a gateway on that shape and now misfires (3 bare
  frames today for DL2JA-1, which is not a gateway).
- The 2026-08-18 "one report per msg_id" rule is not visible in the feed today; either the
  hub changed in the same window, or the rule lived in the WSS pipeline that mcmap stopped
  receiving in mid-August (heydata archives end 2026-08-14).

### 7.3 Verdict

Your hypothesis in its node-ring form is refuted by two measurements. In its server form it
was right until GW-01 went into 4.35t. The binding cause today is that the reporting gateway is
no longer named in the feed, on any upload, since late August. Whether that is a server change
(the hub stopped appending callsign and header RSSI/SNR) or a server rule triggered by the
new in-payload report format is the one thing only Kurt can answer, and it is cheap for him.

Fix candidates, in order:

1. **Hub:** append the reporting gateway's callsign again (as before 2026-08-18), whether or
   not the payload already carries the values. No firmware change, whole fleet fixed at once.
2. **Firmware:** in the upload branch (`src/lora_functions.cpp:1390-1400`) append the own
   callsign to the source path next to the report, mirroring the relay branch. One line, an
   upstream PR, but it must be agreed with the hub owner first so the callsign is not appended
   twice once the hub does it again.
3. **mcmap alone cannot fix it**; the receiver is not in the data. The node-local scrape
   (finding option E3) remains the workaround for our own nodes.

---

## 8. Questions for you

1. **Post now or after the call?** Section 1.1 is written for the discussion thread. Kurt has
   already answered there; posting our position before you two talk could pre-empt him.
2. **Which DM stage goes upstream first?** Stage 0 was the PR-sized candidate. Do you want to
   offer it in the discussion as the concrete alternative, or keep it for the call?
3. **Soft relay preference (4.2): fork first, or proposal only?** It needs the per-neighbour
   counters the deck lists as the missing step from stage 0. Nothing is built.
4. **The map fix (7.3): who owns it?** Ask Kurt about the hub change, or send the one-line
   firmware PR and accept a possible double callsign later? My recommendation is to ask first.
5. **DL2JA-2 bare frames:** can you get one console line from Werner (DL2JA-2, 4.35t) to
   confirm it no longer self-uploads? That decides whether the 94 bare frames per day are old
   gateways hearing it.
6. **Do you want the OE5HWN-14/-12 control pair and today's counts written back into the mcmap
   finding**, so the August document stops saying the ring or the msg_id is the cause?

---

## 9. Sources

- Discussion #1146 body and OE1KBC's reply (GitHub GraphQL, 2026-09-16 16:00 UTC).
- Firmware: `src/via_functions.cpp`, `src/lora_functions.cpp` (RX gate :679, upload :1390-1415,
  relay rewrite :1486-1488), `src/mheard_functions.cpp` (`updateHeyPath()` :460-640),
  `src/loop_functions.cpp` (`sendHey()` :4949), `src/dedup_functions.h`,
  `src/configuration_global.h` (ring sizes, trickle, `HEY_PATH_PAYLOAD_MAX`); upstream
  `4058b25b` for the same sites; `git log upstream/dev -- src/via_functions.cpp`.
- Concept papers: `docs/presentation/meshcom-protocol.html` (deployed at
  https://dk5en.github.io/MeshCom-Firmware/), `docs/proposal-dm-transport-reliability-20260909.md`,
  `docs/concept-dm-store-and-forward.md`, `docs/adr-nc-importance-backoff.md`,
  `docs/prio-talk-flood-networking.md`, `docs/hey-supp.md`, `docs/hey-storm-analysis-20260827.md`,
  `docs/dm-transport-impl-plan-20260913.md`, `docs/BACKLOG.md` 3.8i (GW-01).
- Field data: DG0OPK campaigns (memory `dg0opk-mesh-findings`, `dedup-ring-size-settled`);
  BergLog 2026-03 in ADR-02.
- Map server (mcmap-prod MCP, 2026-09-16 15:40 to 16:30 UTC): `link_quality_overview` for
  DK5EN-98 and DL2JA-2 (7 d), `nodes_query`, `fleet_firmware`, `logs_grep` on `interlink`
  (live file 92 MB, archives 2026-08-26/28/30, 09-06, 09-13), resources `docs://hey-format`
  and `docs://feature-link-quality`; mcmap repo `docs/findings/data-gateway-link-blindspot.md`,
  `docs/findings/gw-hey-rx-report-baseline-20260818.md`, `proxy/src/interlink/ingest.ts`,
  `proxy/src/interlink/heyDedupe.ts`, `proxy/src/live/frameDedup.ts`.
