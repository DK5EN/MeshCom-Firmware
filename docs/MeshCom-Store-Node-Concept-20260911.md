# Decentralised store node for DM delivery (last-hop mailbox)

Concept, 2026-09-11. Status: draft, not advisor-reviewed, nothing in code.

Extends `docs/proposal-dm-transport-reliability-20260909.md` in the MeshCom firmware fork as
stage 2b. Scope: 1:1 direct messages (DM) on the pure-LoRa path, delivered to a destination that
is absent when the DM is sent.

## 1. Bottom line

The EMCOMM requirement "store and forward" is met without a central server by giving selected
nodes a mailbox role: a **store node** holds DMs for a configured set of callsigns and delivers
each one as a single non-relayed frame the moment it hears the destination directly.

- No new frame type, no wire-format change. The delivery is an ordinary DM frame with the
  original sender as source and the store node appended to the path, like a relay.
- Works with old firmware on both ends. One upgraded store node per area delivers the feature to
  every sender and receiver in range, including the classic-ESP32 fleet that cannot run it.
- Airtime per delivery is one frame of about 1.4 s, not a flood. Several store nodes holding for
  the same callsign cost several frames, never several floods.
- Off-grid by construction. No server, no gateway, no internet involved.
- RAM is not the constraint: 50 mailbox slots are 13 kB. Coordination and airtime are.

What is rejected: a general cache where every board with spare RAM stores every DM it hears and
re-floods on presence. That variant multiplies floods by the number of store nodes and puts
storage where presence detection is unreliable. Principle P5 of the transport proposal ("state
only at the ends") stands; the last-hop node is, for this purpose, the destination's end.

## 2. Why not the server mailbox

The transport proposal already rejects the server mailbox as primary mechanism: it makes MeshCom
partially server-dependent, which the protocol deck lists as a design principle to avoid, and it
does nothing in the off-grid case that EMCOMM is about. The gateway-custody rule (S4 in the
proposal) keeps using the server opportunistically. Nothing in this concept changes that.

## 3. The store node role

### 3.1 What is stored

Text frames addressed to a single callsign that carry the `{NNN` acknowledgement marker, i.e.
DMs. Not group messages, not positions, not HEY beacons, not ACKs themselves.

A DM is stored only when its destination is in the node's **store set** (3.2). The node stores
the stripped payload, source callsign, destination callsign, NNN, the original msg_id, the
receive time, and a delivery counter. About 260 B per slot, 50 slots by default.

### 3.2 Store set modes

| Mode    | Store set                                                    | Intended for                                         |
| ------- | ------------------------------------------------------------ | ---------------------------------------------------- |
| `own`   | All SSIDs of the node's own base callsign                    | Home node holding for the owner's handheld (default) |
| `list`  | Explicit callsigns, with or without SSID                     | Club and EMCOMM group nodes                          |
| `heard` | Callsigns heard directly (no relay hop) within the last 24 h | Area mailbox on a hilltop node (v2)                  |
| `off`   | Nothing                                                      | Default for every node without the switch            |

`own` and `list` ship first. `heard` is the unscoped idea with a bounded number of holders and
waits until the coordination rules in 3.5 are proven on the bench.

### 3.3 Lifecycle of a mailbox entry

1. **Store** on receiving a DM whose destination is in the store set. Dedup on (source, NNN): a
   re-flood of the same DM with a fresh msg_id refreshes the timestamp, it does not take a slot.
2. **Purge** on hearing the destination's `:ackNNN` for that (source, NNN). The store node is
   one hop from the destination, so it hears the ack in the normal case.
3. **Deliver** when a frame is received directly from the destination (the last sending node in
   the path is the destination callsign) and at least one round trip has passed since storing.
   Delivery is subject to the rules in 3.5.
4. **Drop** after `storetime` (default 24 h) or after the delivery cap, whichever comes first.
   Log the drop; there is no sender to report to.

### 3.4 The delivery frame

- Ordinary text frame. `msg_source_call` stays the original sender. The store node's callsign is
  appended to `msg_source_path` exactly as a relay does it (`src/lora_functions.cpp:1437-1480`).
- Fresh msg_id minted from the store node's node id and a **RAM counter or millis()**, never
  from the persisted `node_msgid`. The persisted counter writes flash on every increment.
- `max_hop` 0. The destination is a direct neighbour, nobody relays the frame. This is what
  keeps the cost at one frame.
- Payload unchanged, including `{NNN`, so the destination acks to the original sender with the
  original NNN.

### 3.5 Coordination rules

These are mandatory. Without the per-node ceiling the role is a runaway-node amplifier.

- One delivery per (source, NNN) per 10 minutes. At most 3 deliveries per entry in total.
- At most one mailbox action per 30 s per node, at most 20 per hour.
- No action while QRS or QRT is latched or the 5-minute channel utilisation exceeds 25 %.
- Random jitter of 5 to 60 s before delivery. If another store node's delivery for the same
  (source, NNN) is heard during the jitter, cancel. If it is heard while queued, stop the ring
  slot.
- A sender re-flood (fresh msg_id, same source and NNN) heard while an entry is pending
  refreshes the entry and resets its delivery timer; the sender is alive and handling it.
- All mailbox work runs in the loop task. On nRF52 nothing is done in the receive path.

### 3.6 What the store node does not do

- No custody acknowledgement to the sender. A 0x41 per store node per DM reintroduces the
  k-acks-per-hop problem the proposal rejects. The sender's own ladder (S2) runs unchanged and
  simply receives the destination's ack sooner or later.
- No storage in ordinary relays. Only nodes with the switch, only for their store set.
- No persistence in v1 (see section 6).

## 4. Why old firmware works on both ends (verified in code)

| Fact                                                                                              | Where                                                |
| ------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| The ack is addressed to `msg_source_call`, the original sender, not the last relay                | `src/loop_functions.cpp:4858-4885`                   |
| The sender matches an ack by rebuilding msg_id from its own node id and the NNN in the payload    | `src/lora_functions.cpp:1023-1060`                   |
| The 0x02 status frame goes to the phone even when the original id has rotated out of `own_msg_id` | same, `addBLEOutBuffer` after the `checkOwnTx` block |
| A relayed DM with a fresh msg_id passes the dedup gate at every node                               | `src/lora_functions.cpp:896`                         |
| An old destination acks every DM addressed to it that carries `{NNN`                              | `src/lora_functions.cpp:1072`                        |

Consequences:

- **Old sender.** Retries three times with the same msg_id, gives up silently after about 2 min,
  and hours later receives an ordinary `:ackNNN` it can match. The app shows delivered.
- **Old destination.** Receives an ordinary DM, displays it, acks it. If it had already received
  the original and only the ack was lost, it shows the DM twice. Accepted.
- **New destination.** Dedups on (source, NNN) per S2, re-acks, shows nothing twice.

## 5. Numbers

Modulation SF11 / BW250 / CR4:6, from the transport proposal.

| Item                                             | Value                       |
| ------------------------------------------------ | --------------------------- |
| One delivery frame, typical DM                   | 1.4 s on air                |
| One flood of the same DM, worked example         | about 240 s of channel time |
| Mailbox slot                                     | about 260 B                 |
| 50 slots                                         | 13 kB                       |
| Headroom ESP32-S3 and nRF52840                   | about 150 kB                |
| Headroom classic ESP32                           | about 6.6 kB, not eligible  |
| DMs network-wide, trailing 7 days (mcmap, 09-09) | 1,182 from 185 senders      |

Fleet by hardware string, mcmap nodes query on 2026-09-11:

| Class                                           | Nodes | Share |
| ----------------------------------------------- | ----- | ----- |
| ESP32-S3 (Heltec V3, Stick V3, T-Deck, Supreme) | 399   | 28 %  |
| nRF52840 (RAK4631)                              | 39    | 3 %   |
| Classic ESP32 (TLoRa, T-Beam V1.x, E22, V2.1)   | 926   | 65 %  |
| Reporting total                                 | 1414  |       |

About 30 % of the fleet can take the role. The RAK nodes are mostly powered hilltop gateways,
which are the natural mailbox hosts.

## 6. Consequences

- **A reboot empties the mailbox.** RAM only in v1. Flash is out: the msg_id counter already
  writes flash per message, LittleFS is loop-task-only on nRF52, and wear multiplies with
  retries. Persistence on the T-Deck SD card is a later stage. Say this plainly to the EMCOMM
  group.
- **The mesh becomes stateful at designated nodes.** Operators must know which node holds for
  whom. The `own` and `list` modes make that explicit; `heard` does not, which is why it is v2.
- **Depends on S1 and S2 of the transport proposal.** Re-ack on duplicate, no flash write in the
  ack path, stable NNN across attempts, destination dedup on (source, NNN). Without S2 a
  new-firmware destination that lost its ack shows the DM twice, like an old one.
- **Sender UI gets a fourth state.** "Delivered after reported failed" happens when the S2 ladder
  gave up and the mailbox delivered hours later.
- **Server and mcmap see deliveries as new messages.** Same fold-on-(source, NNN) requirement
  as S2. Where the server also holds DMs for absent destinations (open question in S4), both may
  deliver; display dedup absorbs it, the airtime cost is one frame.
- **Privacy.** DMs are plaintext on air already, but the mailbox must not be visible in the web
  GUI or rxlog to anyone but the owner. Scoping to own or listed callsigns removes most of the
  exposure; `heard` mode reopens it.
- **Not for classic ESP32.** Gate on `ENABLE_MSGSTORE` for ESP32-S3 and nRF52840 only. Classic
  boards still get the feature as senders and receivers.
- **Upstream.** A new node role is a bigger ask than stage 0. Fork-first; the upstream PR needs
  the role concept accepted before code.

## 7. Configuration

| Setting                         | Default | Notes                                       |
| ------------------------------- | ------- | ------------------------------------------- |
| `--store own\|list\|heard\|off` | `off`   | `heard` disabled until v2                   |
| `--storecall CALL1,CALL2,...`   | empty   | store set for `list` mode, up to 16 entries |
| `--storetime <h>`               | 24      | already planned for the S2 outbox           |
| `--storeslots <n>`              | 50      | build-time maximum 100                      |
| Web GUI mailbox view            | on      | owner only, behind the existing auth        |

## 8. Bench plan

Fleet on hand: RAK-90 as store node, Heltec-93 as sender, T-Beam-92 as relay, T-Deck-14 as
destination. Traffic only to a direct contact, never to the broadcast group.

1. Destination off during send, on later. Expect: delivery within one beacon interval, one frame
   with hop count 0, ack reaches the sender, sender UI shows delivered.
2. Destination on, ack suppressed at the store node. Expect: one re-delivery after 10 min,
   destination dedups and re-acks, no second display.
3. Two store nodes with the same store set. Expect: one delivery, the second cancelled in jitter
   or stopped in the ring.
4. Destination on upstream 4.35t. Expect: DM displayed, ack matched by an old sender.
5. Store node reboot with pending entries. Expect: mailbox empty, drop logged.
6. Burst of 25 DMs to an absent destination. Expect: caps and rate limits hold, no QRT, ring
   occupancy unchanged.

Counters for the setlog STAT line: stored, delivered, purged by ack, dropped by storetime,
dropped by cap, cancelled by peer.

## 9. Open questions

- What the EMCOMM group means by "store and forward": eventual delivery of a DM to an absent
  recipient (this concept), or a node that was off pulling the traffic it missed, BBS-style.
  The second is a different feature with a request frame, time sync, and real storage needs.
  Confirm before building.
- Whether the MeshCom server holds DMs for offline destinations. Decides how often server and
  mailbox double-deliver in gateway-dense areas.
- Whether upstream accepts a node role at all, or only the sender-side stages.
- Persistence on T-Deck SD: worth it, and in which stage.
- mcmap folding on (source, NNN), shared with S2.

## 10. Sources

`docs/proposal-dm-transport-reliability-20260909.md` (stages S1 to S6, principles P1 to P6,
numbers), `docs/concept-dm-store-and-forward.md` and
`docs/review/advisor-dm-store-and-forward-20260830.md` (outbox, flash and timer-task findings),
`docs/presentation/meshcom-protocol.html` (EMCOMM requirements), the firmware lines cited in
section 4, mcmap `fleet_firmware` and `nodes_query` on 2026-09-11.
