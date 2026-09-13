# Verdict: DM retry ladder, ACK dedup, and the store node

Review, 2026-09-13. Reviews `docs/MeshCom-Store-Node-Concept-20260911.md` and the four changes
proposed in session, against `docs/proposal-dm-transport-reliability-20260909.md`,
`docs/concept-dm-store-and-forward.md`, `docs/review/advisor-dm-store-and-forward-20260830.md`
and the firmware as of `7427f425`. Nothing in code.

Reviewed by eight independent finders with adversarial verification; findings and refuted claims
in `docs/review/fable-dm-store-node-verdict-20260913.md`. This document incorporates them.

## 1. Bottom line

The four DM changes are the right diagnosis and are already the core of the existing proposal
(S1 + S2). Build them. The store node is sound as a role, but **three of its stated mechanics do
not survive contact with the code**, and one of them fails silently against the fleet that is on
the air today:

1. **A custody ACK cannot be "display only".** The binary `0x41` ACK is matched by `msg_id` at
   `src/lora_functions.cpp:412` and calls `findAndStopRingSlot()`. The gate is `checkOwnTx()`
   alone; byte 10, the GW/node flag, is never consulted — `isPlausibleAckFrame()`
   (`src/ack_functions.h:60`) skips bytes 10 and 11 by design. Every node that owns the msg_id
   therefore **stops its retry ladder** and marks the message acknowledged. A store node emitting
   `0x41` silently converts "I stored it" into "it was delivered", and a new byte-10 value cannot
   prevent it on the installed fleet.
2. **There is no gateway custody ACK for DMs at all.** `src/lora_functions.cpp:1236` gates the
   `0x41` emission on destination `*`, `WLNK-1`, `APRS2SOTA` or a group. A DM to a callsign never
   produces one, and the server/UDP ingress path never emits one either. Stage S4 of the transport
   proposal is written on a false premise. Section 11.
3. **Every fresh msg_id costs a flash write.** All eight `node_msgid++` sites in
   `src/loop_functions.cpp` are immediately followed by `save_settings()`. On nRF52 that re-reads
   the whole struct from LittleFS, compares it, and — because `node_msgid` always differs —
   performs a full remove and rewrite **every call**; on ESP32 it is about 20 NVS `put*()` calls
   per send with no change detection at all. Neither platform batches or rate-limits. Idea 4 —
   repeated `:ackNNN`, each with a new id — multiplies that by the repeat count. Minting from
   `millis()` is a **prerequisite**; the gateway ACK path at `:1241` already does exactly that and
   is the precedent to copy.

The ladder is settled: **(3x40 s + 1 min) x 3 — 9 transmissions over 9 minutes, then report
failure.** Group messages are never stored but get the same repair capped at 3 transmissions, and
that one needs a dedup key groups do not have today (section 3.5). Section 10 lists what is left.

## 2. Decisions taken

| #   | Question                   | Decision                                                                                                                                                                                                                                                                                                                   |
| --- | -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | DM retry ladder            | **(3x40 s + 1 min) x 3 = 9 transmissions over 9 minutes, then stop and report failure.** That is the whole ladder; there is no 24 h sender-side ladder. The `via` argument that supported the fast rate does not hold (section 7), but the 9-send cap makes the airtime objection small — section 7 redoes the arithmetic. |
| D2  | Store-node delivery reach  | Direct neighbour only. **`max_hop` 0 on the raw wire field** — `--maxhop` clamps to 1..6 (`src/maxhop.h:17-18`), so "minimal" via the normal setter would still allow one relay hop.                                                                                                                                       |
| D3  | Custody ACK                | Display only — the sender's ladder keeps running. **In v1 this means no on-air notice at all** (T9 option A): the store node logs it and shows it on its own mailbox page, and the sender sees nothing. A sender-visible state is stage 4.                                                                                 |
| D4  | Store set                  | `heard` mode against the existing 12 h mheard window. No mheard change needed — see T7, now resolved in this design's favour.                                                                                                                                                                                              |
| D5  | Store-node delivery ladder | The same 9-send ladder per presence trigger, then **a one-hour cooldown** before the next cycle, until `storetime` expires or the `:ackNNN` arrives.                                                                                                                                                                       |
| D6  | Group messages             | **Never stored** — the mailbox is for personal messages only. But group sends get the same fresh-msg_id repair, capped at **one group of 3x40 s, 3 transmissions, no ladder**. Section 3.5.                                                                                                                                |
| D7  | Pull model                 | Rejected. A returning node is never asked to request its mail; the holder hears it and delivers unprompted. Operator decision 2026-09-13, against the mechanism in upstream #224. Section 12.                                                                                                                              |

## 3. The four DM changes, judged

### 3.1 Same msg_id on retry — correct, and already the proposal's root cause

Confirmed in code. `updateRetransmissionStatus()` (`src/lora_functions.cpp:2070-2163`) resends by
`addTxRingEntry(&ringBuffer[ircheck][2], size, ...)` — a raw memcpy of the already-encoded frame.
`encodeAPRS()` and `aprsmsg.msg_id` are never touched, so the msg_id is byte-identical across all
attempts. `MAX_RETRANSMIT 3`, threshold `0x15` = 20 ticks x 2 s = 40 s. Every node that heard the
original — the destination included — drops it at the dedup gate (`:653`, `:896`). Your reading is
exactly right: the current ARQ can only repair a loss on the very first hop, never a lost ACK.

The fix is P2 of the proposal: **msg_id fresh on every attempt, NNN stable across attempts**. Both
fields are already on the wire. No format change.

What this costs, stated plainly: today's retries are mostly swallowed and therefore nearly free.
Once they are fresh they propagate for real. The traffic removed is the useless kind; the traffic
added is the useful kind, and it is larger. Section 7.

### 3.2 The ladder — buildable as specified, with two gates and a cap

Your ladder: three attempts 40 s apart, pause, three more, repeat. Decision D1 keeps it. Three
things must ride along or it is a runaway-node design:

- **The cap (D1).** Three groups of three, 40 s inside a group, 1 min between groups: **9
  transmissions over 9 minutes**, then the sender stops and reports failure. This replaces
  `MAX_RETRANSMIT 3`'s 120 s bound. A DM to a node that is switched off costs 9 frames and ends; it
  does not transmit for as long as the sender is up. Everything past those 9 minutes is the store
  node's job, not the sender's.
- **The existing back-pressure gates.** No attempt while QRS/QRT is latched or while the 5-minute
  channel utilisation is above threshold; at most one outbox action per 30 s per node. These
  already exist in the fork (`bp_state.refusing()` at `src/loop_functions.cpp:4070`) and cost
  nothing to reuse. They are what makes a fixed 40 s rate safe on a site having a bad day.
- **The dedup-ring budget.** See T11. This is a fleet-wide cost the earlier documents never
  counted.

One free improvement that does not slow the ladder: **attempt 1 stays same-msg_id and is
echo-gated**. The node already knows whether a neighbour relayed its frame
(`own_msg_id[][4] == 0x01`, set at `src/lora_functions.cpp:890`). If nobody relayed within ~15 s, a
same-id retry is the only kind that can help and it is cheap. If somebody did, skip straight to the
first fresh-id attempt. This deletes the one genuinely wasted transmission from every DM without
touching the 40 s rate.

### 3.3 Receiver dedup on (source, NNN) — correct, with a collision caveat

Right call, and it is S2's destination dedup. Two constraints from the code:

- **NNN is not unique per DM over 24 h.** `node_msgid` is a single 0..999 counter shared by DMs,
  positions, HEY, pings and ACKs. A chatty node wraps it fast — the DL6MDF-11 incident burned 1000
  ids in about 50 s (`src/beacon_rate.h:22-30`). The cache must age by **time**, not by count:
  entries older than ~1 h are dropped, and a match must also compare payload length (or a 16-bit
  CRC of the stripped payload) before suppressing display. The same applies to the store node's own
  mailbox dedup, over a far longer window — T12.
- **Hook both receive paths.** LoRa and the server/GATE path, or a DM arriving twice by two routes
  still displays twice (advisor M6).

Size: 16 entries x ~14 B = ~224 B. Fits everywhere including classic ESP32.

### 3.4 Repeated ACKs, each with a fresh msg_id — correct, but not via the ring

Right in principle. Three code facts shape it:

- **ACKs are never retransmitted today.** `SendAckMessage()` enqueues with status `0x00` so the
  priority classifier sees a real destination frame, then forces `0xFF` = `RING_STATUS_DONE`
  (`src/loop_functions.cpp:4972-4975`). ACK repeats need their own small scheduler; they cannot be
  handed to `updateRetransmissionStatus()`.
- **Stop on evidence, or a 2-node link burns every repeat.** Nothing acks an ACK. The only
  available evidence is hearing the ACK relayed (own-echo, `own_msg_id[][4] == 0x01`). In a sparse
  link there is no relay, so the echo never comes and every repeat fires. Cap hard (2 repeats) and
  stop on echo.
- **The flash write.** Section 1 point 3. Mint from `millis()`.

Also required, and this is what makes re-ACK work at all: **split the dedup gate**. Today a
duplicate frame is dropped wholesale at `src/lora_functions.cpp:896`, which takes the ACK emission
at `:1072` with it. Needed: duplicate + addressed to me + carries `{NNN` -> re-ACK, do not display,
do not relay. This is S1's "re-ACK on duplicate" and it repairs one-hop ACK loss on its own, before
anything else ships.

### 3.5 Group messages — the same repair, a third of the ladder (D6)

Current state, verified: a group send gets **no** `{NNN` (the marker is added only under `bDM`,
`src/loop_functions.cpp:4096-4101`), but it **is** retransmit-eligible — status `0x00` at
`:4128-4133` — so it already re-sends 3 times, 40 s apart, with the same msg_id. It is therefore
broken in exactly the way DMs are: every node that heard the original swallows the repeat. The only
thing that can stop that ladder early is a neighbouring **gateway's** `0x41`, which is emitted for
groups (`src/lora_functions.cpp:1237`, `CheckGroup() > 0`) though never for DMs. Off-grid, with no
gateway in range, all three transmissions always fire and nothing is learned from them.

So the repair is the same — fresh msg_id per attempt — capped at **one group: 3 transmissions,
40 s apart, no further groups** (D6). One thing does not carry over, and it is the catch:

**A group message has no NNN, so there is nothing to dedup on.** The (source, NNN) cache in 3.3
cannot be used. Without a substitute, a fresh-msg_id repeat makes every node in the network
**display the same group message three times**. That is worse than the bug being fixed.

The fix is receive-side dedup on **(source callsign, CRC-16 of the payload)**, time-aged like the
DM cache. No wire change, and it also suppresses today's duplicate displays from multipath
re-floods. Adding `{NNN` to group frames instead is wrong twice over: nobody would ack it — the ack
branch is gated on the destination matching the node's own callsign exactly
(`src/lora_functions.cpp:983`), so a group destination never reaches it — and old nodes would
render the trailing `{123` as visible text, because the strip happens in that same branch.

Consequence to accept: for groups there is no delivery proof at all, with or without this change.
The 3 transmissions are blind redundancy, which is the right trade at 3 frames and the wrong one at
9 — which is why D6 caps it where it does.

## 4. Code facts this design rests on

| Fact                                                                                   | Where                                                            |
| -------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| Retry re-enqueues the identical encoded frame, 3x, 40 s apart                          | `src/lora_functions.cpp:2070-2163`                               |
| One dedup verdict per frame gates display, relay, and ACK emission                     | `src/lora_functions.cpp:653`, `:896`                             |
| The dedup ring is count-based with no per-entry timestamp                              | `src/dedup_functions.cpp:24-52`                                  |
| Destination ACKs any DM to it carrying `{NNN`                                          | `src/lora_functions.cpp:1072`                                    |
| Sender matches a text ACK by rebuilding msg_id from its own `_GW_ID` and NNN           | `src/lora_functions.cpp:1023-1060`                               |
| Text ACK also stops the ring slot, by that rebuilt msg_id                              | `src/lora_functions.cpp:1050`, def `:262`                        |
| Binary `0x41` stops the ring slot by the msg_id in bytes 6-9; byte 10 is never read    | `src/lora_functions.cpp:412`, `src/ack_functions.h:60`           |
| Gateway `0x41` is emitted only for `*`, `WLNK-1`, `APRS2SOTA`, groups — never for a DM | `src/lora_functions.cpp:1236`                                    |
| Gateway `0x41` already mints its msg_id from `millis()`, no flash write                | `src/lora_functions.cpp:1241`                                    |
| Every one of the eight `node_msgid++` sites is followed by `save_settings()`           | `src/loop_functions.cpp:3353,3455,4103,4792,4869,4932,5021,5359` |
| ACK is enqueued then forced `RING_STATUS_DONE`, never retransmitted                    | `src/loop_functions.cpp:4972-4975`                               |
| `own_msg_id` holds only `MAX_RING` (20) entries on every shipping env                  | `src/loop_functions.cpp:423`                                     |
| Beacons write `own_msg_id` **inline**, bypassing `insertOwnTx()`                       | `src/loop_functions.cpp:4829-4831`, `:4902`                      |
| nRF52 RX runs in the dedicated 16 kB `_lora_task`, not the timer-service task          | `src/boards/mcu/board.cpp:498`                                   |
| mheard keys on `msg_source_last` — the last hop, not the originator                    | `src/lora_functions.cpp:729`                                     |
| mheard prunes at 12 h and evicts the oldest under pressure                             | `src/mheard_functions.cpp:64`, `:298-330`                        |
| mheard capacity: 80 (S3/nRF52), 30 (every other shipping env)                          | `src/configuration_global.h:240-263`                             |
| Path fields are populated only for payload types `:`, `!`, `@`                         | `src/aprs_functions.cpp:155`                                     |
| A relay overwrites `msg_destination_path` and re-runs `checkVia()` from its own config | `src/lora_functions.cpp:1460-1462`                               |
| `checkVia()`'s two auto-derive branches are commented out (2026-07-22)                 | `src/via_functions.cpp:109-146`                                  |
| `max_hop` 0 is valid on the wire and relays skip it                                    | `src/lora_functions.cpp:1431`, `:1526`                           |
| `--maxhop` clamps to 1..6                                                              | `src/maxhop.h:17-18`                                             |
| A `FLASH_STRUCT_VERSION` bump wipes every updating node's settings                     | `src/configuration_global.h:102-119`                             |

Dead branches, so they must not be used for sizing: `ENABLE_TBEAM` and `ENABLE_SBUFFER` are defined
nowhere in the build, and `ENABLE_XML` only by the native unit-test env (`platformio.ini:415`).

## 5. Traps

**T1. A fresh msg_id breaks the ACK-to-ring match.** The incoming text ACK rebuilds
`msg_id = (_GW_ID << 10) | NNN` and calls `findAndStopRingSlot()`, which compares the full 32-bit
msg_id read from bytes 3-6 of the stored frame. Attempts 2..n carry a fresh msg_id whose low 10
bits are no longer NNN, so the ACK stops **nothing** and the ladder runs to its cap after delivery.
The binary `0x41` path uses the identical rule. _Fix:_ index the outbox by NNN, not by a
reconstructed msg_id, and stop every attempt sharing the NNN.

**T2. The own-TX table is too small to hold a long ladder.** `own_msg_id` is 20 slots. Beacons
consume one per transmission — `sendPosition()` writes the ring **inline** at
`src/loop_functions.cpp:4829-4831`, not through `insertOwnTx()`, so a call-site grep of the helper
misses it (this refutation was tried and failed; see the verdict's refuted-claims list). Each fresh
attempt burns another. The original rotates out and `checkOwnTx()` fails — the ACK is then not
attributed and the ring is not stopped. _Fix:_ the outbox is a separate structure (S2's 5 slots,
~1.2 kB) that owns the NNN and survives independently; `insertOwnTx()` is still called per attempt
for echo recognition, but the outbox is the authority.

**T3. NNN collides.** See 3.3. Age by time, compare payload.

**T4. Flash wear.** Section 1 point 3. A prerequisite. Note the justification is **wear only** —
advisor M5's "the ACK path runs in the 1 kB timer-service task" is false: the DIO IRQ gives a
semaphore that wakes a dedicated 16 kB `_lora_task` (`src/boards/mcu/board.cpp:498`), which calls
`Radio.BgIrqProcess()` -> `OnRxDone()` -> `SendAckMessage()`. That has always been the architecture
of the vendored SX126x-Arduino library. A stale comment at `src/nrf52/nrf52_main.cpp:3040` still
asserts the old belief and should be corrected when that file is next touched.

**T5. The dedup gate must be split** before re-ACK or store-node re-delivery can work. See 3.4.

**T6. ACK repeats have no stop condition in a sparse link.** See 3.4.

**T7. The `heard` store set — resolved, in the design's favour.** `updateMheard()`'s only caller
sets `mheardLine.mh_callsign = aprsmsg.msg_source_last` (`src/lora_functions.cpp:729`), the **last
hop**. mheard is therefore already a directly-heard table by construction: `heard` mode needs no new
state and no mheard change, and the role is intrinsically limited to RF neighbours, which is
exactly what D2 wants. Two caveats remain: the table **evicts the oldest entry when full**
(`:298-330`), so on a hilltop seeing 85-124 nodes against 80 slots the effective window is well
under 12 h and varies with traffic — the store set is "recently heard", not "heard in the last
12 h", and the mailbox page must show the real last-heard age per entry; and mheard cannot name the
true originator of a relayed frame, which is harmless here.

**T8. "Direct neighbour" needs one guard the concept omits.** Workable test: `msg_source_path`
contains exactly one callsign, i.e. no relay appended. Path fields are populated only for payload
types `:`, `!`, `@` (`src/aprs_functions.cpp:155`), and relays always append before retransmit, so
for those types path length 1 does mean "not yet relayed". **But nothing checks `msg_server` /
gateway injection first**, so server-injected traffic presents path length 1 without ever having
been heard over RF — the OE1XAR-62 gwflood confound. A store node that is also a gateway would
otherwise learn phantom neighbours and hold mail for nodes it cannot reach.

**T9. A `0x41` custody ACK is not display-only.** This is what constrains D3. Every node that owns
the msg_id sets `own_msg_id[][4] = 0x02` and calls `findAndStopRingSlot()`
(`src/lora_functions.cpp:394-415`) — the ladder stops and the app shows the message acknowledged.
Byte 10 is never consulted, so a new value there does not help. Three ways out:

| Option                                        | Behaviour on current firmware                                                                                    | Cost                                                           |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| **A. No on-air custody notice in v1** (taken) | Nothing changes for the sender                                                                                   | Zero. The store node logs it and shows it on its mailbox page. |
| **B. Text notice `:stoNNN`**                  | Verified to contain no `:ack`, `:rej` or `{`, so it falls through both the LoRa and UDP parsers to plain display | ~0.85 s per store node per DM, and chat noise on old nodes     |
| **C. `0x41` with a new status**               | Ladder stops, message shows delivered — wrong                                                                    | Not viable until the fleet has updated                         |

Option A ships in v1; option B or C belongs to stage 4, together with a real app status
(`0x03` in `buildAckPhoneFrame`, which already carries the attributing callsign field).

**T10. Two ladders on the same DM.** After a store node takes custody the sender's ladder keeps
running (D3). The destination gets the DM from the sender's re-floods _and_ from the store node's
deliveries, and answers both. With the (source, NNN) dedup this is display-correct but doubles ACK
traffic. Acceptable; worth a counter.

**T11. Fresh msg_ids shorten the dedup ring's safe window, fleet-wide.** `is_new_packet()`
(`src/dedup_functions.cpp:24-52`) is a linear scan of a count-based ring with **no timestamp**. The
measured safe corridor of 39.6-48.5 min is therefore a function of arrival rate at today's traffic.
A ladder minting a fresh msg_id per attempt raises the distinct-id arrival rate at **every relay
and gateway on the path**, not only at the destination, pushing busy nodes' rotation window below
the measured corridor and degrading dedup for unrelated HEY and position traffic on nodes that are
not party to the DM. No earlier document budgets this. It is the strongest technical argument for
spacing attempts, and it is independent of round-trip time.

**T12. The mailbox needs the same payload check as the destination cache.** The store node dedups
on (source, NNN) over a hold window of up to 24 h — far longer than the destination's ~1 h cache —
with no payload comparison. A sender that wraps NNN inside the window can have two unrelated
messages merged into one slot, and the wrong one delivered.

**T13. New settings must not touch the settings struct.** Adding `--store`, `--storecall`,
`--storetime` or `--storeslots` as fields of `struct s_meshcom_settings` forces a
`FLASH_STRUCT_VERSION` bump, which runs `clear_flash()` on every updating node — callsign, WLAN,
sensors. That incident is recorded in-tree at `src/configuration_global.h:102-108`. This is advisor
B1. _Fix:_ use the established pattern documented at `:113-118` — own NVS keys or spare bits in
existing fields, layout unchanged, exactly as `max_hop_text` did.

**T14. Failure modes none of the documents cover.** A store node that is also a gateway, replaying
a stale DM to the server hours later. A store node with a wrong clock computing age and hold time
(the known NC-01 bad-clock class). A destination that changes SSID — delivery stalls permanently
with no operator signal. **Two store nodes in a hidden-terminal geometry**, where jitter
cancellation cannot work by physics rather than by implementation; bench expectation 5 in both
documents assumes they hear each other. QRT latched for hours, stalling both delivery paths at once
and dropping silently at `storetime`, precisely during the congestion EMCOMM cares about. A group
message that parses as a DM — the bug class of the 2026-09-11 broadcast incident.

## 6. The store node, revised

Changes against `docs/MeshCom-Store-Node-Concept-20260911.md`:

- **3.2 store set:** `heard` moves from v2 to v1, scoped to directly-heard callsigns inside the
  existing 12 h mheard window, with T7's honesty requirement on the GUI and T8's gateway-injection
  guard.
- **3.4 delivery frame:** original sender as `msg_source_call`, store node appended to the path,
  fresh msg_id from `millis()`, **`max_hop` 0 on the raw field** (D2), payload including `{NNN`
  untouched.
- **3.5 delivery schedule (D5).** On a presence trigger the store node runs the same 9-send
  ladder — (3x40 s + 1 min) x 3 — then waits **one hour** before it may start another cycle for
  that entry, until the `:ackNNN` arrives or `storetime` expires. A delivery is one frame at
  `max_hop` 0, ~1.4 s, not a flood, which is what makes a ladder affordable here at all.
  The concept's other §3.5 caps stay and are what bound the role: at most one mailbox action per
  30 s and **20 per hour per node**, no action while QRS/QRT is latched or 5-minute utilisation
  exceeds 25 %, jitter 5-60 s with cancellation on hearing a peer's delivery for the same
  (source, NNN).
  Two numbers to keep in view: a destination that is heard but never acks costs 9 frames per hour
  per entry, up to ~216 frames over a 24 h hold — ~5 min of airtime for one message, tolerable
  alone. But with a full 50-slot mailbox the same situation is ~10,800 frames, which is why the
  20-per-hour node ceiling is the real cap, not the per-entry one. Note T14: jitter cancellation
  fails in a hidden-terminal geometry, so between two store nodes that cannot hear each other the
  node ceiling is again the only thing that holds.
- **3.6:** "no custody acknowledgement" is a **T9 constraint**, not a preference. v1 emits nothing
  on air (option A).
- **New: the switch.** `--store heard|own|list|off`, default `off`, stored per T13. Turning it on
  in the web GUI raises a confirmation: _this node must run 24/7 on continuous power; stored
  messages live in RAM only and a reboot discards all of them without notice._ Advisor M7 requires
  the second half of that sentence to be visible to the operator, not only in a doc.
- **New: the mailbox page.** `/?page=mailbox`, a `sub_page_mailbox()` in the existing `/?page=xxx`
  dispatch (`src/web_functions/web_functions.cpp:681`). Columns: destination, source, NNN, age,
  remaining hold time, delivery attempts, and the destination's real last-heard age (T7). Counters:
  stored, delivered, purged by ACK, dropped by `storetime`, **dropped by delivery cap and dropped
  by slot pressure as two separate counters**, cancelled by peer. Add a manual purge and a manual
  force-deliver action; without them the operator can only reboot. Advisor m10 applies — the server
  is synchronous, the page must stay small and must not allocate per row.
- **Privacy.** `heard` mode in v1 is a regression against the concept's own §6, which scoped the
  mailbox to `own`/`list` partly because `heard` "reopens" DM metadata exposure. The page shows
  source, destination and NNN for third-party DMs to an operator who is not a party to them. The
  owner-only auth gate is therefore mandatory, and the page must never show payload text.
- **Retention:** 24 h default, oldest-out when full. Both drops get a log line and a counter; there
  is nobody to report them to.

Unchanged and still right: RAM only in v1, S3/nRF52 only (`ENABLE_MSGSTORE`), all mailbox work in
the loop task.

## 7. The cost question, and how to settle it

The objection I raised and you overruled: a fresh msg_id makes each retry a real flood, and the
proposal's worked example puts a flood at ~240 s of channel time (10 nodes x 4 hops), against a
measured DM round trip of ~1 min quiet and ~4 min median on the loaded DG0OPK-11 site.

**Your counter — that `--via` makes the retry a directed path — does not hold, and I was wrong to
concede on it.** `checkMesh()` does return false for a node not named in `msg_destination_path`
(`src/via_functions.cpp:82`), but every relay **overwrites** `msg_destination_path` with
`msg_destination_call` and re-runs `checkVia()` from **its own** `bVIA`/`node_via`
(`src/lora_functions.cpp:1460-1462`). The two branches that would auto-derive a next hop — the
`HG,` gateway branch and the mheard/NCT branch — are block-commented out, dated 2026-07-22
(`src/via_functions.cpp:109-146`). Consequences:

- Sender-side `--via` constrains **hop 1 only**. From hop 2 the frame floods normally unless every
  intermediate node is separately via-configured. A via corridor is an infrastructure property, not
  a sender setting.
- If the named via node is down, nobody relays at all — no fallback to flood. The ladder then fails
  silently and permanently, which is worse than the flood it was meant to avoid.

### The cost, recomputed under the 9-send cap

With D1's cap the worry I raised is much smaller than I made it sound, and the arithmetic should be
on the record rather than the scary aggregate:

- Worst case per DM: **9 transmissions over 9 minutes**, versus 4 today (1 + 3 retries) of which 3
  are swallowed and useless. So the honest comparison is 1 useful flood today against 9 useful ones,
  not "1,170 frames".
- Cost at any one node: each node hears a frame 2.1 to 2.6 times (measured DUP/NEW), each ~1.4 s,
  so one flood costs a given node ~3.4 s of receive time and 9 cost ~31 s. Spread over 9 minutes
  that is **roughly 6 % of the channel at one node, for one DM taken to full failure**.
- Network-wide volume makes this a non-issue on the average: 1,182 DMs in 7 days across 185 senders
  (mcmap, 09-09). Even if every one of them failed all 9 times, it disappears against 3,600 to
  4,600 HEY beacons per hour.
- Where it still bites is a **local burst** — several senders in one area retrying at once — which
  is what the QRT/utilisation gate and the one-action-per-30 s ceiling are for. Those are not
  optional.

So: the ladder is affordable, and the reason is the cap, not `via`. What remains genuinely unknown
is whether 40 s is long enough to be _useful_ — if the round trip is longer than 9 minutes on a
given path, all 9 transmissions fire before an ACK could arrive and the ladder is merely expensive
noise. That is the measurement below, and it is a question about effectiveness, not about damage.

### Measure two things

1. **The round trip.** Add to the setlog STAT line: DMs sent, echo heard, peer ACK, attempts per
   DM, and send-to-`:ackNNN` time as a histogram. There is no end-to-end DM outcome measurement in
   the network today at all. One week decides whether 9 minutes is the right window — and your "the
   4 minutes is a misconfigured site" becomes a testable claim rather than one data point against
   another.
2. **The dedup-ring side effect (T11)**, which no round-trip argument covers: distinct msg_ids per
   minute at a busy relay, before and after. Nine fresh ids per DM instead of one raises the
   distinct-id arrival rate everywhere on the path. If a busy node's rotation window drops below
   the measured 39.6-48.5 min corridor, the ladder is damaging traffic that has nothing to do with
   DMs. This is now the main open risk of D1, and the only one the cap does not settle.

## 8. Build order

| Stage | Content                                                                                                                                                                                                                                                                                   | Depends on |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- |
| 0     | Mint ACK/attempt msg_ids from `millis()`, drop `save_settings()` from the ACK path (T4). Split the dedup gate, re-ACK on duplicate-for-me (T5). Report give-up to the app, scoped to user DMs only (advisor m6). STAT counters, the RTT histogram and the dedup-rate counter (section 7). | —          |
| 1     | Outbox keyed on NNN (T1, T2). Fresh msg_id per attempt, NNN stable. The 9-send ladder (D1) and the QRT/utilisation gates. Echo-gated first attempt.                                                                                                                                       | 0          |
| 2     | Destination dedup on (source, NNN) with time ageing and payload compare, on both RX paths (3.3). Bounded ACK repeats with echo stop (3.4).                                                                                                                                                | 1          |
| 3     | Store node: store set with the T8 gateway guard, `max_hop` 0 delivery, §3.5 caps, mailbox page, switch with the RAM warning, settings via T13, counters.                                                                                                                                  | 2          |
| 4     | Sender-visible custody notice with a real app status, once a capability signal exists (T9 option B or C).                                                                                                                                                                                 | 3          |

Stage 0 verifies as genuinely self-contained and is upstream-PR-sized; it repairs real defects on
its own. Stage 1's parameters are now fixed (D1), so it no longer waits on the measurement — but
stage 0 should still land first, because without the counters there is no way to show stages 1 to 3
worked. Group repair (D6) rides with stage 2, since it needs the same receive-side dedup machinery
keyed on a CRC instead of NNN. Stage 3 is fork-first; a new node role needs the concept accepted
before code.

## 9. Bench plan

Fleet: RAK-90 store node, Heltec-93 sender, T-Beam-92 relay, T-Deck-14 destination. Group 9/9999 or
a direct contact only, never `*` (the broadcast incident of 2026-09-11).

1. Two-hop DM, ACK suppressed at the destination. Expect: each attempt reaches the destination
   (fresh msg_id), destination re-ACKs each time, no second display.
2. Same, ACK allowed. Expect: the ACK stops **every** attempt sharing the NNN (T1 regression).
3. Sender beaconing hard so the original rotates out of `own_msg_id`. Expect: the ACK is still
   attributed and the ladder still stops (T2 regression).
4. Destination off during send, on later. Expect: one store-node delivery at `max_hop` 0, ACK
   reaches the sender, nothing relayed (check for the `hop0` skip reason at the relay).
5. Two store nodes that hear each other, same store set. Expect: one delivery, the other cancelled
   in jitter. **Then repeat with the two store nodes unable to hear each other** — expect two
   deliveries, and confirm the per-entry cap of 3 still bounds it (T14).
6. Destination on upstream 4.35t. Expect: DM displayed, plain `:ackNNN` matched by the sender.
7. Store node reboot with pending entries. Expect: mailbox empty, drop logged, warning honoured.
8. 25 DMs to an absent destination. Expect: caps and rate limits hold, no QRT, ring occupancy flat.
9. Confirm no `0x41` is emitted for a DM by a store node (T9), by string-scanning the image and by
   watching the sender's ladder continue.
10. Store node that is also a gateway, fed a server-injected frame for an unknown callsign. Expect:
    no phantom neighbour learned (T8), no stale replay to the server (T14).

## 10. Open questions

**Settled 2026-09-13** (these were blocking stage 1): the gap between groups is **1 minute**; the
total cap is **9 transmissions over 9 minutes**, then failure is reported; the store node repeats
that ladder after a **one-hour cooldown** (D5); group messages are **never stored** and get **3
transmissions only** (D6); the pull model is **rejected** (D7).

**Still open:**

- **Does the evidence gate go in?** A much smaller question than it was — with the 9-send cap the
  worst case is ~6 % of one node's channel for one DM, so the gate is no longer a safety measure,
  only an optimisation. Recommendation: **leave it out**. A fixed, predictable ladder is easier to
  reason about in the field than one that changes rate from a utilisation average, and the
  QRT/utilisation gate already refuses transmission when the channel is genuinely in trouble.
  Revisit only if the stage 0 measurement shows the fast rate firing into round trips it cannot
  beat.
- **Group dedup needs a decision on the CRC** (3.5): which bytes it covers, cache size and ageing,
  and whether it shares the DM cache or gets its own.
- mcmap and the server must fold on (source, NNN) — and for groups on (source, payload CRC) — or
  message counts inflate once attempts carry fresh ids.
- `{` in user text still breaks NNN parsing (advisor m4). Pre-existing, and it gets worse once NNN
  is load-bearing for dedup. Strip or escape at the sender in stage 0.
- Advisor M3, m1 and m2 — the app never learns a per-attempt outcome, `sendMessage()` has no return
  channel, and a single-variable receipt mailbox loses acks — are dropped here with no successor.
  All three concern reconciling several fresh-msg_id attempts into one app-visible state. They
  return in stage 2.

## 11. Corrections to the existing documents

- `docs/proposal-dm-transport-reliability-20260909.md` §2 and §5 S4: "a gateway that hears a DM
  emits the binary 0x41 with status 0x01" is **false for DMs**. Emission is gated on `*`, `WLNK-1`,
  `APRS2SOTA` or a group at `src/lora_functions.cpp:1236`, and the UDP/server ingress path never
  emits an on-air `0x41` either. S4 has no signal to trigger on as written, and its "removes most
  re-floods at zero bytes" claim does not hold. Either S4 adds the DM case to the gateway emission
  — a change every gateway must take, colliding with T9 on old senders — or S4 is withdrawn.
- The same proposal's §2 "ACK matching" row is right but incomplete: the match also stops the ring
  slot, which is what T1 breaks.
- The same proposal's §5 S2 schedule ("at most 6 blind re-floods per DM per day") contradicts its
  own ladder ("+2, +10, +30, +60 min, then hourly"). Both are superseded by D1, but the
  inconsistency should be fixed if that document is kept.
- `docs/review/advisor-dm-store-and-forward-20260830.md` M5: the nRF52 ACK path does **not** run in
  the FreeRTOS timer-service task. See T4. The flash finding in M5 stands; the stack-overflow
  hazard does not.
- `docs/MeshCom-Store-Node-Concept-20260911.md` §3.6 "no custody acknowledgement" is stated as a
  design preference; it is a hard constraint (T9). §3.2 `heard` moves to v1 per D4, with the T8
  guard and the privacy gate in section 6. §3.4 "max_hop 0" was the correct phrasing and is
  restored.
- P5 of the proposal ("state only at the ends", storage never in relays) is **bent, not met**: a
  store node is not an endpoint. The concept redefines "the ends" to include the last hop. That
  reinterpretation is deliberate and is recorded here rather than left implicit — it is the single
  largest architectural departure in this design, and the §3.5 caps are what keep it from becoming
  the general relay cache that P5 forbids.

## 12. Against upstream issue #224

`icssw-org/MeshCom-Firmware#224`, "[Feature] store messages in Mesh for later retrieval"
(pweichsel, 2025-03-16, open, labelled **MeshCom 5.0** — parked for the next protocol step).

The request: a node A is told to hold a message for an absent node B; when B returns it **asks**
"any message for me" and A delivers. Explicitly "for nodes or even groups", and explicitly
"across the mesh, provided only that the retrieval request from node B reaches node A".

That is the **pull** model, and it is the reading this design's predecessor parked as "a different
feature with a request frame, time sync and real storage needs". This design is the **push** model.
Four differences, stated so a PR does not overclaim:

| #224 asks for                           | This design delivers                                           | Gap                                                                                                                                                                                                                         |
| --------------------------------------- | -------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| B requests its mail on return           | A delivers automatically when it hears B (S3 presence trigger) | **Mechanism differs — outcome is met, and met better.** No request frame, no new frame type, no time sync, and it works with unmodified firmware on both ends. B needs no support at all.                                   |
| Messages for nodes **and groups**       | DMs only (concept §3.1 excludes group messages)                | **Not delivered.** A group message has no single destination to trigger on and no `{NNN`/`:ackNNN` to purge on, so neither the presence trigger nor the lifecycle applies. Fan-out is unbounded. This needs its own design. |
| Works **across the mesh**, multi-hop    | Direct RF neighbours only (D2, `max_hop` 0)                    | **Not delivered.** And it is structural, not a setting: mheard keys on `msg_source_last` (T7), so a store node can only ever learn, and hold for, its own neighbours.                                                       |
| Retrieval reaches a store node far away | The holder must be in RF range of B when B returns             | **Not delivered.** This is the pull model's one genuine advantage over presence: B can be served by a node it is not a neighbour of.                                                                                        |

**Two of the four gaps are now closed by decision, not by code (2026-09-13):**

- **The pull model is rejected outright (D7).** Not deferred: a returning node should never have to
  ask. The holder heard it, the holder knows it has something, the holder sends it. That is a
  defensible stance — it needs no new frame type, no time sync, and no support whatsoever at the
  returning end, which is exactly why it works with the entire existing fleet as receivers. The
  issue's mechanism is declined; its use case is met.
- **Group storage is rejected outright (D6).** The mailbox is for personal messages. Technically
  this is also the right call: a group message has no single destination to trigger delivery on and
  no acknowledgement to purge on, so every mechanism in §3.3 would have to be reinvented, with
  unbounded fan-out. Groups instead get the cheap half of the fix — the fresh-msg_id repair, capped
  at 3 transmissions (section 3.5).

What remains genuinely undelivered is the third row alone: **multi-hop retrieval**. A store node
holds for its RF neighbours and no one else, structurally (T7).

**Verdict on the framing.** A PR headed "#224 delivered" would still overclaim. What is defensible:
_"#224 delivered for direct-neighbour direct messages, without a wire-format change. Group storage
and multi-hop retrieval are deliberately out of scope, and the retrieval-request mechanism the issue
proposes is replaced by presence-triggered delivery, which needs no support at the returning node."_
That is worth saying plainly, because the honest version is still a strong claim — the issue has been open since March 2025 and parked behind
a protocol step, and this design needs no protocol step at all. Landing the 80 % case now, with old
firmware on both ends, is a better outcome than waiting for MeshCom 5.0; it just has to be labelled
as the 80 % case.

**The case for D7, to have ready when the issue author asks.** A request frame has one real
advantage — B can be served by a holder it is not a neighbour of — against four costs: a new frame
type, so old firmware cannot participate at either end; a request storm exactly when the channel is
recovering, because every node returning from an outage asks every neighbour at once; a flood per
request in the multi-hop case, with every holder answering, which is the amplifier the store node
was designed to avoid; and time sync. Presence-triggered delivery buys the same outcome for one
frame at `max_hop` 0 and needs nothing at the returning end. The honest trade is coverage
(neighbours only) against cost and compatibility, and D7 takes compatibility.

**Upstream consequence.** The `MeshCom 5.0` label means the maintainers parked this for a protocol
step. A fork-first store node that needs no wire change is a different proposition from what that
label anticipates, and the PR should say so rather than letting the issue reference imply the
parked design was built.
