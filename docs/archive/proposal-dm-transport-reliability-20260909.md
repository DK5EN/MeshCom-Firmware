# Transport reliability for personal messages over LoRa RF

Proposal, 2026-09-09. Scope: 1:1 direct messages (DM) on the pure-LoRa path. The gateway/server
path is stable today and is only used here as an opportunistic helper.

## 1. Bottom line

MeshCom already has an end-to-end acknowledgement (`{NNN` in the DM, `:ackNNN` from the
destination). That is the right architecture for a flooding mesh. What is broken is not the
idea but three mechanics:

1. A retry re-floods the **same msg_id**, and every node that already heard the original,
   including the destination, drops it in the dedup ring. So the retry can only repair a loss on
   the very first hop, can never repair a lost ACK, and is pure waste on every other path.
2. The retry timer is a constant 40 s, but the round trip is 1 min on a quiet network and
   4 min median (p99 far above) on a loaded one. All three retries fire before the original
   could possibly have been acknowledged.
3. Nothing is protected on the way back and nothing is reported: the ACK is a single
   fire-and-forget frame, and giving up after three retries is silent.

Recommendation: a staged programme of six building blocks, in this order:

| Stage | Block                                       | New bytes on air | RAM (classic ESP32 / S3, nRF52) |
| ----- | ------------------------------------------- | ---------------- | ------------------------------- |
| 0     | S1 Repair the existing ARQ                  | none             | 0 / 0                           |
| 1     | S2 End-to-end ARQ with two identifiers      | 7 B per receipt  | 0 (parked slots) / ~1.2 kB      |
| 2     | S3 Presence-triggered delivery + S4 Custody | none             | ~0.2 kB / ~0.2 kB               |
| 3     | S6 Precedence classes, S5 App-side outbox   | marker in text   | 0 / 0                           |

Stage 0 alone cuts today's retry traffic and closes the silent-failure hole with a change that is
upstream-PR-sized and needs no wire-format change. Stage 1 is the actual transport layer. Stages 2
and 3 answer "offline" and "temporarily unavailable" without probe traffic.

## 2. What happens today (verified in code)

| Step         | Where                                                          | Behaviour                                                                                                                                                                |
| ------------ | -------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| DM send      | `src/loop_functions.cpp:4035-4088`                             | msg_id = 22-bit node id + 10-bit counter (wraps at 1000). DM payload gets `{NNN` where NNN is that counter. Persisted to flash on every increment.                       |
| Retry        | `src/lora_functions.cpp:2044-2081`                             | Status byte ticks every 2 s; at ~40 s the same frame (same msg_id) is re-enqueued. `MAX_RETRANSMIT 3`, then the slot is freed with only a debug line.                    |
| Dedup gate   | `src/lora_functions.cpp:653` and `:896`                        | `is_new_packet()` on the msg_id gates all processing of foreign frames, including the "DM is for me" branch and the ACK emission at `:1072`.                             |
| ACK emission | `src/loop_functions.cpp:4858-4922`                             | `SendAckMessage()` builds a text frame `DEST     :ackNNN` with a fresh msg_id, writes settings to flash, enqueues it and forces `RING_STATUS_DONE`: never retransmitted. |
| ACK matching | `src/lora_functions.cpp:1023-1060`                             | Sender rebuilds msg_id from its own node id and NNN, marks `own_msg_id[][4] = 0x02`, stops the ring slot.                                                                |
| Own echo     | `src/lora_functions.cpp:649`, `:869-893`                       | Hearing its own msg_id relayed sets `own_msg_id[][4] = 0x01` (heard) and sends status 0x00 to the phone. Already exists, unused for retry decisions.                     |
| Gateway ACK  | `src/lora_functions.cpp:1206-1287`, relay `:420-430`           | A gateway that uploads the DM emits a 12-byte binary 0x41 with status 0x01; it is meshed back but never uploaded to the server.                                          |
| Ping/pong    | `src/via_functions.cpp:56`, `src/lora_functions.cpp:1108-1118` | Text markers `{ping}`/`{pong}`, one hop only by design. Not a multi-hop reachability probe.                                                                              |

Consequences, stated once:

- A same-msg_id retry is invisible to any node that already holds the id. On a multi-hop path the
  first relay that heard the original swallows it. If the DM arrived but the ACK was lost, the
  destination swallows it too and never re-acks. The existing ARQ therefore only covers "no
  neighbour heard my first transmission".
- The retransmit holds keep the sender's own TX ring occupied (bench: 11 of 20 slots for 251 s
  after a burst), which is what pushes the sender into QRT for its other traffic.
- The sender cannot distinguish "DM lost" from "ACK lost". Both look like silence.

## 3. Numbers the design must respect

Modulation SF11 / BW250 / CR4:6, 32-symbol preamble, symbol time 8.2 ms.

| Frame                     | Bytes | Time on air |
| ------------------------- | ----- | ----------- |
| Binary gateway ACK (0x41) | 12    | 0.51 s      |
| Text ACK `:ackNNN`        | ~50   | 0.85 s      |
| Typical DM (frame)        | ~115  | 1.4 s       |
| Maximum DM (frame)        | ~200  | 2.2 s       |

Flood multiplier: every node hears each frame 2.1 to 2.6 times (measured DUP/NEW 1.57 to 2.09).
The deck's worked example is 10 nodes times 4 hops, roughly 240 s of channel time for one frame.
**The cost of a retry is the flood, not the sender's 1.4 s.** Budget retries in flood-equivalents.

Round trip: per-hop enqueue-to-air is 3.9 s on a quiet bench plus 3 to 4.9 s CSMA backoff. On a
loaded site (DG0OPK-11) the per-hop TX-queue latency is 30 s median, 286 s p99, 985 s max. A
4-hop DM plus 4-hop ACK is therefore about 1 min quiet, about 4 min median loaded, tens of minutes
at the tail. Channel-busy on CAD was 66 % in the 5-node BergLog, with up to 27 CAD retries per
frame.

Volume (mcmap server, trailing 7 days to 2026-09-09): 1,182 direct messages from 185 senders,
against 2,601 group messages and 3,600 to 4,600 HEY beacons per hour network-wide. DMs are well
under one percent of frames. Even tripling DM airtime is invisible in the aggregate; the risk is
local bursts and runaway loops (a single node has been seen at 21 frames/s), never the average.

RAM: classic ESP32 (E22, T-Beam) has about 6.6 kB DRAM headroom after the planned fixes;
ESP32-S3 and nRF52840 have about 150 kB. The TX ring is 20 slots of 260 B and already holds every
DM for up to 120 s. The dedup ring must stay at its current size (measured corridor 39.6 to
48.5 min).

Flash: each DM and each ACK already costs one settings write because the msg_id counter is
persisted. Any ladder multiplies that; an outbox must not add its own writes.

## 4. Design principles derived from the numbers

- **P1 End-to-end only.** On a flood there is no "next hop" to ack to. Hop-by-hop ARQ would
  need k ACKs per hop for k relays and state in every relay. Keep the destination's `:ackNNN`
  as the only proof of delivery.
- **P2 Two identifiers, two jobs.** The msg_id is the loop-suppression key and must be fresh on
  every re-flood. NNN is the transport sequence number and must be stable across re-floods so the
  destination can deduplicate and the sender can match. Today they are the same ten bits, which
  is the root cause of defect 1. Both fields already exist on the wire.
- **P3 Budget in flood-equivalents.** Space attempts by the measured round trip, not by a
  constant. Gate every attempt on the sender's own channel-utilisation and back-pressure state.
- **P4 Use the signals already on the air.** Own echo means "a relay has it". The gateway 0x41
  means "the server has it". `:ackNNN` means "the destination has it". The destination's own
  beacons mean "it is reachable now". No new frame type is needed for any of them.
- **P5 State only at the ends.** Sender-side outbox, never storage in relays or gateways (deck
  decision, RAM, duplicate explosion). Durable storage belongs to the app or the T-Deck SD card,
  not to the node's flash.
- **P6 Tell the user the truth.** Three states: delivered (with integrity check), pending,
  failed. Plus "RF-only" versus "via server" when known.

## 5. Options

### S1 Repair the existing ARQ (zero new bytes, zero RAM)

Mechanism:

- **Echo-gated first-hop retry.** After sending, wait about 2x the local echo delay (10 to 15 s).
  Retransmit with the same msg_id only if `own_msg_id[][4]` is still 0x00 (nobody relayed it)
  and no ACK arrived. If it was heard, skip all same-id retries: they cannot help. This is the
  only case in which a same-id retry ever does anything, and it removes the three blind retries in
  the common case.
- **Re-ACK on duplicate-for-me.** Split the gate at `src/lora_functions.cpp:896`: a duplicate
  frame that is addressed to me and carries `{NNN` still triggers `SendAckMessage()`
  (rate-limited to one per minute per source and NNN) but is neither displayed nor relayed. This
  repairs ACK loss for one-hop DMs, which are common (handheld to handheld in one area, node to
  its own gateway).
- **Report failure.** When the budget is exhausted, send a status frame to the app (0x41 with a
  new status value, BLE only) and mark the message in the web GUI. Today the deck's complaint
  stands: failure after three attempts is never reported.
- **Move the flash write out of the ACK path.** `SendAckMessage()` runs in the nRF52 timer task
  and calls `save_settings()` there (advisor finding M5). Mint ACK ids from `millis()` and drop
  the write.

Fixes: wasted retries, silent failure, one-hop ACK loss. Does not fix: loss beyond the first hop,
multi-hop ACK loss, offline destination. Effort: about 100 lines. Compatibility: complete, old
nodes see byte-identical frames. Upstream-PR candidate.

### S2 End-to-end ARQ with two identifiers (the transport layer)

Mechanism:

- **Outbox.** Classic ESP32: park the existing TX-ring slot (new status `RING_STATUS_PARKED`,
  at most 2 parked) so the DM survives beyond 120 s with zero extra RAM. ESP32-S3 and nRF52: a
  dedicated 5-slot outbox (about 1.2 kB, 3 per destination) so parked DMs never eat the ring.
- **Re-flood attempt.** Same payload, same `{NNN`, **fresh msg_id** (registered with
  `insertOwnTx()` so echo and ACK recognition keep working), same max_hop. Relays treat it as a
  new frame and carry it all the way.
- **Destination dedup on (source_call, NNN).** A 16-entry cache of about 12 B each, aged after an
  hour (NNN wraps at 1000 per node and the counter is shared with ACKs). Duplicate content is not
  displayed, not forwarded to the app or the server a second time, but is **always re-acked**.
  Hook both RX paths (LoRa and server GATE), otherwise the server path re-displays.
- **Receipt v2.** `:ackNNN;H=xxxx` with a 16-bit CRC of the stripped payload. Old parsers stop
  at the first non-digit (`toInt()` semantics, advisor-verified), so this is backward compatible in
  both directions. It gives the sender an integrity proof (EMCOMM requirement) for 7 bytes.
  Optionally `;F=R` when the DM arrived without the server flag, for the "RF-only" badge.
- **Schedule.** L1 echo-gated retry (S1) within 15 s. Then end-to-end attempts at +2 min on a
  quiet channel, stretched by the 5-minute channel-utilisation average, then +10, +30, +60 min,
  then hourly until `storetime` (default 24 h). Caps: at most 6 blind re-floods per DM per day,
  at most one outbox action per 30 s per node, no action while QRS/QRT is latched or the 5-minute
  utilisation exceeds 25 %.
- **Sender UI.** Pending / delivered (hash matched) / failed.

Fixes: multi-hop loss, ACK loss, the "lost or unacked" ambiguity. Offline only as far as the
ladder reaches. Effort: 400 to 600 lines. Compatibility: old destinations answer plain
`:ackNNN` and never re-ack a re-flood if they hold the same content already (they show it
twice). Side effect: the server and mcmap see re-floods as new messages because the msg_id is
fresh; dashboards must learn to fold on (source, NNN).

Why this differs from `docs/concept-dm-store-and-forward.md`: the concept minted a **new NNN per
attempt** and needed a content hash plus a set of minted NNNs per slot to match late ACKs
(advisor issue M2). Keeping NNN stable removes that machinery entirely and lets the destination
dedup on two fields that are already on the wire. The concept's outbox, ladder and receipt v2
survive; its `{prb}` probe is replaced by S3.

### S3 Presence-triggered delivery (offline and temporarily unavailable)

Insight: every MeshCom node announces itself already. HEY beacons run on a trickle from 30 s up to
15 min, positions and any text carry the source callsign, and `mheard` records the last-heard
epoch per callsign. A pending DM therefore does not need a probe: when any received frame's
source callsign matches a pending destination, the node is reachable right now, and the outbox
sends immediately (still under the 30 s tick and the channel gate).

- A node that returns from sleep or power-off is served within one beacon interval of its return,
  typically within a minute, at zero probe cost.
- A destination that stays silent for longer than `storetime` is marked failed and reported.
- Do not add a probe in the first version. A re-flood of the DM itself is the cheapest probe
  there is (1.4 s versus 0.76 s probe plus 1.4 s DM); a separate probe only pays off when DMs are
  long and destinations are usually absent, which the field data does not show. Revisit with
  counters from stage 0.
- Do not repurpose ping/pong: it is one-hop by design and the local echo already calibrates the
  timers. Keep it as the operator's manual link check.

Caveat: HEY and position frames have their own hop limits, so a destination four hops away may
never appear directly. Then the S2 ladder still applies. Effort: about 80 lines on top of S2.

### S4 Custody handover to the server (use the existing gateway ACK)

Today a gateway that hears a DM emits the binary 0x41 with status 0x01 and uploads the DM; the
server delivers it to the destination's gateway, which re-transmits it. The sender's firmware
already receives that 0x41 and shows it to the phone, but ignores it for retry decisions.

Rule: on a gateway ACK for a pending DM, mark it "in custody", suspend blind re-floods, and wait
for the destination's `:ackNNN` (which arrives via mesh or via the server path). If none arrives
within about 15 min, resume the slow ladder, because the destination may be off-grid. In
gateway-dense areas, which is most of AT and DE, this removes most re-floods at zero bytes.

Caveats: a gateway ACK proves "the server has it", not delivery; whether the server holds DMs for
offline destinations must be confirmed with the server team; off-grid EMCOMM use is unaffected
because without a gateway there is no 0x41 and the normal ladder runs. This keeps "LoRa is all
you need" intact while using the backbone when it happens to be there.

### S5 App-side or T-Deck-side outbox (state lives where the RAM is)

The node stays stateless beyond S1. The app (McApp, mc-chat, MCProxy, or the T-Deck with PSRAM and
SD) holds the outbox, survives node reboots, keeps 24 h without a byte of node RAM, and drives
re-sends through the existing "send text" path. The node's status frames (heard, gateway, peer)
already tell the app which state each message is in.

Firmware needs: the S2 destination-side dedup and receipt v2 anyway (so a re-send from the app
gets a stable NNN and a fresh msg_id), plus the failure status frame. In effect S5 is S2 with the
outbox moved out of the node. Downside: the ladder only runs while the phone is connected, and
the official McApp is outside this repository's control, so it lands first in mc-chat and the
T-Deck. Upside: classic ESP32 gets the full feature with zero RAM.

### S6 Precedence-aware proactive redundancy (EMCOMM classes)

The deck lists precedence (Emergency, Priority, Welfare, Routine) as an EMCOMM requirement and the
message schema already knows an EMERGENCY type. For flagged messages only:

- Send the DM twice up front with fresh msg_ids, spaced by one echo delay. Temporal diversity
  beats a single shot against collisions and CAD loss, and costs one extra flood only for rare
  traffic.
- Grant a larger retry budget and a shorter ladder; optionally max_hop 5 to 6 (limit is 7).
- Routine stays single-shot.

Requires a precedence marker in the payload (a `{E}`-style prefix like the existing `{CET}` and
`{MCP}` markers), which old nodes relay and display as text. No state beyond S2.

### Rejected

- **Hop-by-hop ACK or custody in relays.** k relays per hop means k ACKs per hop, RAM in every
  relay, and a duplicate explosion. Contradicts the flood design the deck argues for.
- **AX.25 or TCP-style windows.** Connection state at both ends for a channel that carries one
  message per minutes with a multi-minute round trip. Nothing to pipeline.
- **Server mailbox as the primary mechanism.** Infrastructure dependency (the deck's stated
  principle). S4 uses the server opportunistically instead.
- **Bigger dedup ring or same-id retry after the ring rotates.** The ring size is measured and
  settled; a 40 min blind wait is not a transport layer.
- **Outbox in flash in version 1.** The msg_id counter already writes flash per message, LittleFS
  is loop-task-only on nRF52, and wear multiplies with every ladder step. Durable storage is S5.

## 6. Comparison

| Criterion                    | S1 repair | S2 two-id ARQ | S3 presence | S4 custody | S5 app outbox | S6 precedence |
| ---------------------------- | --------- | ------------- | ----------- | ---------- | ------------- | ------------- |
| First-hop loss               | yes       | yes           | n/a         | n/a        | yes           | yes           |
| Loss beyond first hop        | no        | yes           | n/a         | via server | yes           | yes           |
| ACK loss                     | 1 hop     | yes           | n/a         | partly     | yes           | yes           |
| Temporarily unavailable dest | no        | ladder        | yes         | via server | ladder        | no            |
| Offline dest (hours)         | no        | 24 h ladder   | yes         | unknown    | days          | no            |
| Silent failure               | fixed     | fixed         | fixed       | n/a        | fixed         | n/a           |
| Integrity proof              | no        | CRC-16        | n/a         | no         | CRC-16        | n/a           |
| Airtime versus today         | lower     | RTT-spaced    | zero        | lower      | RTT-spaced    | +1 flood/flag |
| RAM classic / S3+nRF52       | 0 / 0     | 0 / 1.2 kB    | 0.2 kB      | 0          | 0 / 0         | 0             |
| Wire change                  | none      | receipt v2    | none        | none       | receipt v2    | text marker   |
| Old-node compatibility       | full      | full          | full        | full       | full          | full          |
| Off-grid ("LoRa only")       | yes       | yes           | yes         | no         | yes           | yes           |
| Effort                       | ~100 LoC  | ~500 LoC      | ~80 LoC     | ~60 LoC    | app work      | ~100 LoC      |

## 7. Answers to the six questions

1. **Presentation requirements.** Confirmed delivery: S2 receipt. Precedence: S6. Off-grid, no
   single point of failure: S1 to S3 need no server. Store-and-forward: sender-side outbox only,
   as the deck already argues. Integrity: CRC in receipt v2. Compatibility both ways: no new frame
   type anywhere.
2. **Concept papers.** The store-and-forward concept and its advisor verdict remain the base for
   S2. Changes: NNN stays stable across attempts (drops the minted-NNN set and hash matching),
   presence replaces probes, classic ESP32 is included through parked ring slots, custody rule
   added, flash write removed from the ACK path.
3. **Ping/pong.** One hop only, so it cannot establish a multi-hop path. It stays the manual link
   check. Reachability comes for free from the destination's own frames (S3), and timer
   calibration comes from the sender's own echo delay.
4. **Other party offline.** Sender outbox up to `storetime`, then a reported failure. If a
   gateway heard the DM, the server may hold it (verify). Durable storage beyond a node reboot is
   the app's job (S5), not the node flash.
5. **Temporarily unavailable.** S3 delivers within one beacon interval of the node's return with
   no probe traffic. The ladder is the fallback for destinations whose beacons never reach the
   sender.
6. **Not overwhelming the network.** Echo gating removes the blind first-hop retries (three today,
   zero in the common case). Attempts are spaced by round trip, gated on channel utilisation and
   QRS/QRT, capped per DM and per node, suspended on gateway custody, and replaced by presence
   triggers where possible. A per-node ceiling on outbox actions is also the defence against a
   runaway node.

## 8. Measure before and after

The network has no measurement of DM outcome today; per-hop relay success of 99.2 to 99.7 % is
known from one quiet cluster, end-to-end DM success is not. Stage 0 should add counters to the
setlog STAT line: DMs sent, echo heard, gateway ACK, peer ACK, gave up, and the median time to
peer ACK. Without them the value of stages 1 to 3 cannot be shown.

Bench checks per stage: same-id retry count per DM before and after echo gating; re-ACK on a
replayed one-hop DM; a two-hop DM with the first relay's copy of the ACK suppressed; a re-flood
arriving at an old-firmware destination; ring occupancy under a 25-message burst with two parked
slots.

## 9. Risks and open points

- A user `{` in DM text already breaks NNN parsing today. Strip or escape it at the sender.
- The receipt v2 suffix passes through the server as text; confirm the server and the apps
  tolerate `;` in ACK payloads.
- Whether the server stores DMs for offline destinations decides how strong S4 is.
- mcmap must fold re-floods on (source, NNN) or its message counts inflate.
- The (source, NNN) cache must age faster than the NNN wrap for very chatty senders.
- The nRF52 receive path runs in the timer task; all outbox and ACK work must be deferred to the
  loop task.

## 10. Sources

`docs/presentation/meshcom-protocol.html` (EMCOMM requirements, constraints),
`docs/concept-dm-store-and-forward.md` and `docs/review/advisor-dm-store-and-forward-20260830.md`,
`docs/ack-wer-hat-quittiert.md`, `docs/ack-implementierungsplan.md`, `docs/ping-fix.md`,
`docs/architecture/10-buffer-inventory.md` and `11-wire-format.md`,
`docs/adr-nc-importance-backoff.md`, `docs/prio-talk-flood-networking.md`,
`docs/backpressure-protocol.md`, `docs/report-2026-09-02-adaptiver-relay-slot.htm`,
`docs/hey-storm-analysis-20260827.md`, `docs/mem-headroom-classic-esp32-20260905.md`,
DG0OPK field campaigns (Aug 2026), mcmap `messages_stats` (2026-09-09), and the firmware lines
cited in section 2.
