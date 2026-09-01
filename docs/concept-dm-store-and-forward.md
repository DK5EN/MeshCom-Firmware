# Concept: Assured Delivery for Personal Messages (Store-and-Forward Outbox)

Status: DRAFT v2 — advisor findings incorporated (see §11 and
`docs/review/advisor-dm-store-and-forward-20260830.md`)
Date: 2026-08-30
Audience: EMCOMM operators; MeshCom core dev team
Related: `docs/presentation/meshcom-protocol.html` (slides "Der Vorschlag · Transport",
"EMCOMM · Abgleich"), `docs/architecture/`, DG0OPK campaign reports

## 1. Goal and scope

A personal (direct) message must reach its destination whenever the destination is
reachable at any point within a configurable holding window (default 24 h), and the
sender must end up in exactly one of three user-visible states: **delivered**,
**failed**, or **still open** — with proof of what was delivered, and whether it
traveled RF-only or through an Internet gateway.

Scope of that guarantee: it holds across **continuous uptime** of the sender node.
v1 keeps the outbox in RAM only (§8), so a reboot (watchdog, power cycle, deep
sleep) discards open entries; the node then shows a one-time "outbox lost on
reboot" notice at boot when the store is enabled — it cannot know _what_ was lost,
but it must never pretend nothing was.

In scope:

- Sender-side store-and-forward: up to 3 pending messages per destination callsign,
  held on the **originating node** until acknowledged or expired.
- A slow retry layer on top of the existing fast retransmit, with timing derived from
  measured mesh latency (LoRa multi-hop is slow; Internet/gateway is fast).
- Reachability probing before retries (directed probe, not HEY flooding).
- A delivery receipt that survives single packet loss and carries (a) an integrity
  hash over the delivered content and (b) an "RF-only" path flag.
- Visibility: outbox in the WebGUI and over serial USB; delivered entries kept
  (content truncated) with sent-at / delivered-at timestamps.
- Configuration of holding time via WebGUI and serial.
- Hardware gating: only boards with RAM headroom get the feature.

Explicitly out of scope (deliberate non-goals, see §10):

- Storage on intermediate hops — the mesh stays ephemeral. Only the originator holds
  the message.
- A server-side mailbox for absent recipients. The presentation calls this a
  "Grundsatzentscheidung" (it makes MeshCom partially server-dependent); it needs its
  own discussion and is not part of this concept.
- Encryption. Confidentiality is not achievable on amateur bands (encryption is not
  permitted) and the protocol is plaintext. What we deliver is **integrity and
  delivery proof** ("signieren, nicht verschlüsseln" — sign, don't encrypt).
  Full authenticity (keyed signatures) is future work because it requires key
  management; the hash stamp here is a checksum against corruption/truncation, not
  a defense against a deliberate forger.
- Sequence numbers / sliding windows (AX.25-style). The presentation lists this as
  step 5, "erst danach" — after the cheap steps.

## 2. Current state (what the firmware does today)

All references against branch `tdeck-partial-refresh-trace` (2026-08-30).

**Message TX path.** `sendMessage()` (`src/loop_functions.cpp:3457`) builds the frame:
msg_id = `((_GW_ID & 0x3FFFFF) << 10) | (node_msgid & 0x3FF)` (`:3675`), appends a
`{NNN}` ACK-request marker for DMs (`:3683-3688`), increments `node_msgid` and calls
`save_settings()` — **one flash write per originated message** (`:3690-3695`). The
frame goes to the LoRa TX ring (`addTxRingEntry`, prio CRITICAL for DMs) and, on a
gateway with IP, additionally to the server via UDP (`:3790-3794`). LoRa and Internet
are not either/or — a gateway sends on both.

**Fast retransmit (L1).** `updateRetransmissionStatus()`
(`src/lora_functions.cpp:1788-1884`): text frames only, ticked every 2 s, resends the
**byte-identical frame** (same msg_id) every ~40 s, `MAX_RETRANSMIT 3`, then
`RETRANSMIT_GIVEUP` and the slot is freed. Total protection window: ~120 s. Give-up is
logged but **not reported to the user** — the message silently dies.

**ACK infrastructure.**

- The destination node answers a DM carrying `{NNN}` with a normal `0x3A` text frame
  whose payload is `<origin_call>:ackNNN` (`SendAckMessage()`,
  `src/loop_functions.cpp:4441-4522`; trigger `src/lora_functions.cpp:947-975`).
- The binary `0x41` ACK (12 bytes, layout in `src/ack_functions.h:13-20`) is emitted
  only by **gateways** and only for broadcast/group traffic
  (`src/lora_functions.cpp:1124-1173`).
- The originator stops L1 retransmission on: binary ACK, text `:ackNNN`, or
  overhearing its own frame being relayed (implicit "heard",
  `src/lora_functions.cpp:508-551`).
- Per-message sender state is `own_msg_id[MAX_RING][5]` (`0x00` none / `0x01` heard /
  `0x02` ACK) — a 20-deep displacement ring with **no destination, no timestamps, no
  persistence**; a late ACK whose entry has been displaced finds nothing
  ("Späte Quittungen finden ihren Eintrag nicht mehr").
- The ACK itself is sent exactly once (fire-and-forget, ring status DONE): a single
  lost packet destroys the proof of delivery even though the message arrived.

**Deduplication.** `ringBufferLoraRX[MAX_DEDUP_RING][5]` (`src/dedup_functions.cpp`),
100 entries on ESP32-S3/nRF52, 70 on classic ESP32, pure displacement, no aging.
Field-measured turnover 28.8–52.3 min (DG0OPK). Consequence: a resend with the
**same** msg_id is swallowed by every node that still holds it; a resend with a
**new** msg_id is a fully new message everywhere.

**Path/Internet marker.** Byte 5 bit 7 of every frame (`msg_server`) is set when a
server-connected gateway injects or relays the frame
(`src/udp_functions.cpp:329`, `src/lora_functions.cpp:1264-1265`) and is **sticky** —
never cleared. `msg_server == false` on a received frame therefore means: this copy
traveled an all-RF path. Caveat: a gateway's own originated message goes on air with
`msg_server = false` even though it is simultaneously uploaded via UDP.

**Directed probe.** `{ping}`/`{pong}` exists (`sendPing()`
`src/loop_functions.cpp:3201`, reply `src/lora_functions.cpp:865-881`) and is
unicast-addressed — but it is **explicitly excluded from meshing**
(`src/via_functions.cpp:55-60`, `src/lora_functions.cpp:996-1000`) and therefore
works only 1 hop today. HEY (`0x40`) is unsuitable as a probe: flooded broadcast, no
reply semantics, storm history (`docs/hey-storm-analysis-20260827.md`), flash write
per send, lowest priority.

**Measured latency (DG0OPK campaign, 47.6 h, 5 nodes).** Per-hop RX→relay TX-queue
latency for text: median 30 s, p99 286 s, max 985 s under load. A 4-hop outbound path
plus a 4-hop receipt can legitimately take **tens of minutes** end to end. Any retry
timer that fires earlier than that produces duplicates, not deliveries.

**RAM reality.** Classic ESP32 (E22, T-Beam v1.x) has ~1.1 kB DRAM headroom (E22) and
28 B free IRAM (T-Beam) — MEM-01 already had to shrink `MAX_RING` there. ESP32-S3
boards have ~200 kB DRAM free; RAK4631 has ~170 kB RAM free but only a **28 KB**
internal filesystem, written as one settings blob, and LittleFS writes are forbidden
outside the loop task (documented boot-loop, `docs/BACKLOG.md:2139`).

## 3. Architecture overview

Five building blocks, all sender- or receiver-side — nothing new on intermediate hops,
no new frame type, no change to the 0x3A wire format.

```
sender node                                            destination node
┌───────────────────────────┐                          ┌──────────────────────────┐
│ outbox (new)              │   0x3A DM + {NNN}        │ ack v2 (extended payload)│
│  5 slots, per-dest cap 3  │ ───────────────────────► │  :ackNNN;H=xxxxxxxx;F=R  │
│  states: OPEN/DELIVERED/  │ ◄─────────────────────── │  sent 3×40 s (was: 1×)   │
│          FAILED           │                          │                          │
│ L2 scheduler (new)        │   {ping} probe (meshed)  │ content-dedup cache (new)│
│  probe-gated slow retry   │ ◄──────{pong}─────────── │  re-ACK, don't re-show   │
└───────────────────────────┘                          └──────────────────────────┘
        │ existing underneath: TX ring, L1 retransmit 3×40 s, CSMA, dedup,
        │ backpressure, gateway UDP path — all unchanged
```

### 3.1 Sender outbox (new module `src/msgstore_functions.{h,cpp}`)

A small fixed table, independent of the 20-slot TX ring (which stays a radio queue,
not a mailbox):

- `MSG_STORE_SLOTS = 5` entries total (RAM-class gated, see §8), at most
  **3 OPEN entries per destination callsign**. A 4th message to the same destination
  while 3 are open is refused with an explicit notice on the originating transport
  (same pattern as backpressure QRT notices, `src/backpressure.h`).
- Entry lifecycle: `OPEN → DELIVERED` (receipt matched) or `OPEN → FAILED` (holding
  time expired). DELIVERED/FAILED entries stay visible as history until a new message
  needs the slot (evict oldest non-OPEN first). On DELIVERED the payload is truncated
  to ~24 chars + `…` to save RAM; sent-at and delivered-at epochs are kept.
- Written only from the loop task (both platforms) — no locking needed on ESP32, and
  on nRF52 it never touches the timer-task context. The RX path (`handleACK`, ack
  text match) only deposits (msg_id, NNN, flags) into a small **4-deep receipt ring**
  (not a single variable: the loop task can stall for seconds — `save_settings()`
  delays, synchronous WebGUI page streaming, N-20 W5100S stalls — and a returning
  destination typically re-ACKs a burst); the outbox state transition happens in the
  loop tick.

Entry layout (per slot, ~232 B):

| field                    | size  | note                                       |
| ------------------------ | ----- | ------------------------------------------ |
| dest_call                | 10 B  | target callsign                            |
| payload                  | 161 B | user text (max 160 + NUL); truncated later |
| minted NNN set           | ~8 B  | ALL ack-ids minted across attempts (§3.5)  |
| current msg_id           | 4 B   | of the most recent attempt                 |
| content hash             | 4 B   | see §5.2, computed once at enqueue         |
| state                    | 1 B   | OPEN / DELIVERED / FAILED                  |
| created_at, delivered_at | 8 B   | epoch (0 if unknown)                       |
| created_millis           | 4 B   | monotonic fallback for expiry (see §10)    |
| attempt count            | 1 B   | L2 attempts so far                         |
| next_action_at           | 4 B   | millis of next probe/retry                 |
| flags                    | 1 B   | rf_only_out, rf_only_back, hash_verified,  |
|                          |       | legacy_ack, probe_outstanding              |

5 slots ≈ 1.2 kB plus a 16-entry receiver dedup cache (§3.4, ~300 B): total well
under 2 kB. Affordable on S3/nRF52; **not** on classic ESP32 (§8).

### 3.2 Two-layer retry

- **L1 (unchanged):** the existing 3×40 s byte-identical retransmit protects against
  loss to the first-hop neighborhood. Same msg_id — mesh-wide dedup prevents
  re-flooding, which is correct at this timescale.
- **L2 (new):** when L1 gives up (`RETRANSMIT_GIVEUP`) or no receipt has arrived
  after the L1 window, the outbox scheduler takes over. Each L2 attempt is a **fresh
  send with a new msg_id and new `{NNN}`** (mandatory: after 2 min the old msg_id
  may still sit in dedup rings for up to ~50 min and would be silently swallowed).
  The resend reuses the normal `sendMessage()` path so priority, backpressure,
  gateway upload and the `{NNN}` machinery all behave identically to a manual send.

L2 schedule (defaults, rationale in §6):

| attempt | at (after enqueue) | preceded by probe?      |
| ------- | ------------------ | ----------------------- |
| initial | 0                  | no (just send)          |
| 2       | +10 min            | yes                     |
| 3       | +30 min            | yes                     |
| 4       | +60 min            | yes                     |
| 5…n     | hourly             | yes                     |
| expiry  | holding time (24h) | → FAILED, user notified |

Fast path: on a node with `bGATEWAY && node_hasIPaddress`, attempt 2 moves up to
+3 min (server round trip is seconds, not minutes) — but attempts 3+ stay on the slow
ladder because the last mile to the destination may still be multi-hop LoRa.

Guards on every L2 action: skip the cycle if `bp_state.refusing()` (backpressure), if
TX-ring depth is above the QRT threshold, or if channel-util TX accumulator is high —
a retry layer must never contribute to the congestion it is trying to outlive. All
L2 sends carry normal DM priority (CRITICAL in the ring) but are throttled by their
own ladder, never bursty: at most one outbox action (probe or resend) per scheduler
tick (30 s), across all slots.

### 3.3 Probe-gated retries (reachability discovery)

From attempt 2 on, the scheduler does not blindly resend 180+ byte frames. It first
sends a **directed probe** and only transmits the stored message when the probe is
answered:

- **Probes only to destinations known v2-capable.** The sender probes a callsign
  only after having ever seen a v2 receipt (or `{prb-ack}`) from it — remembered in
  the outbox entry / a tiny capability cache. For unknown or old destinations the
  ladder runs **unprobed**: an old-firmware destination would otherwise render
  `{prb}` as a junk DM on its display and phone app (the RX path shows any
  non-marker text addressed to it, `src/lora_functions.cpp:976-987`) — up to 27
  times per stored message. The "zero behavior change for old nodes" property holds
  for _relaying_ nodes, not for old _destinations_; hence this gate.
- **Late answers count.** The reachability signal is "a `{prb-ack}` — or any
  receipt — has arrived since the probe was sent", evaluated at the next ladder
  step; the probe window equals the ladder interval, not a fixed 5 min. Rationale:
  the doc's own latency data (30 s median, 286 s p99 **per hop per direction**)
  makes multi-hop pong round trips slower than 5 min routine under load; a short
  window would misclassify reachable destinations and degrade the probe layer to
  pure added airtime. A positive signal at any time promotes the slot to "send now".
- **Required change for multi-hop:** `{ping}` is excluded from meshing
  (`src/via_functions.cpp:55-60`, `src/lora_functions.cpp:996-1000`) and thus 1-hop
  only. Proposal: new `{prb}`/`{prb-ack}` markers riding the standard text relay
  path — intermediate nodes (old or new) relay any 0x3A unchanged; only sender and
  destination interpret the marker.
- **Probe priority is below DM.** `getMessagePriority()` classifies DM text as
  CRITICAL, which under a full ring **evicts** relay/beacon slots
  (`src/txring_functions.cpp:134, 361-370`). A congestion-avoidance probe must not
  preempt real traffic: classify `{prb}`/`{prb-ack}` at NORMAL (payload check, same
  spot where `{CET}`-class markers are special-cased).
- **Probes mint msg_ids from `millis()`** (precedent: gateway ACK path,
  `src/lora_functions.cpp:1127`) + `insertOwnTx()` — a fresh id per probe (same-id
  probes die in dedup rings for ~40 min) with **zero** `node_msgid++`/
  `save_settings()` flash writes. This is normative, not optional.
- Probe rate limit: minimum 5 min between probes to the same destination, minimum
  30 s between any two probes (reuse `beaconShotAllowed()` pattern from
  `src/beacon_rate.h` — millis-rollover-safe, unit-tested). `{prb-ack}` replies are
  rate-limited per source (30 s floor) plus a global reply budget, and answered only
  for sources present in mheard (dampens spoofed-source reflection, §10.2).
- Interaction to untangle: `sendHey()` is suppressed while `node_pingtime > 0`
  (`src/loop_functions.cpp:4527`) — the probe path must not permanently park the
  node in ping-pending state.

### 3.4 Receiver-side content dedup + receipt v2

Because L2 retries use fresh msg_ids, the msg_id dedup cannot catch them — by design.
The duplicate suppression moves to the destination, at content level:

- New small cache on the receiver: 16 entries × (source call hash 2 B + content hash
  4 B + epoch 4 B) ≈ 160 B. Key: source callsign + hash over the payload **stripped
  at the first `{`** — exactly the strip rule the receiver already applies when
  extracting the ack id (`src/lora_functions.cpp:962`), so both ends compute the
  same input by construction (the `{NNN}` marker changes per attempt).
- The cache hooks **both** RX paths: LoRa (`OnRxDone` text branch) and UDP/server
  (`src/udp_functions.cpp:406-437` — which today displays and acks **before** any
  dedup check, so server-side duplicates already re-display; fixing that branch is
  part of this work).
- On receiving a DM addressed to me whose (source, content-hash) is in the cache:
  **acknowledge again** (with the new NNN — this is the whole point: the first ACK
  was lost) but do **not** display/forward to phone/app a second time.
- Cache entries age out with the holding-time horizon (24 h) or by displacement.

**ACK generation moves to the loop task and off the flash.** Today a DM addressed
to me triggers `SendAckMessage()` straight from the RX path — on nRF52 that is the
FreeRTOS timer task — and `SendAckMessage()` does `node_msgid++` +
`save_settings()` (`src/loop_functions.cpp:4463-4468`): a LittleFS write from the
timer task, the documented boot-loop class (`docs/BACKLOG.md`, N-20/N-21 lesson),
plus one flash write on the **destination** per received DM (27/day per stored
message once retries re-ACK). Both problems are fixed together, as part of phase 1:
the RX path only deposits an ack request into a small queue; the loop task builds
and sends the ack, minting its msg_id from `millis()` (precedent:
`src/lora_functions.cpp:1127`) with no `node_msgid++` and no settings write.

**Receipt v2** (payload extension of the existing text ack, no new frame type):

```
old:  DK5EN-14:ack123
new:  DK5EN-14:ack123;H=1A2B3C4D;F=R
```

- `H=` — 8 hex chars: truncated hash (see §5.2) computed by the **receiver** over
  what it actually received (source call + dest call + payload without `{NNN}`).
  The sender compares against the hash stored at enqueue: match ⇒ "delivered,
  content verified". This is the availability + integrity stamp: proof that exactly
  this content arrived, surviving corruption/truncation on the way.
- `F=R` — set by the receiver iff the received DM had `msg_server == false`
  (traveled RF-only to the destination). The sender combines it with the
  `msg_server` bit of the **receipt frame itself**: both clean ⇒ the round trip was
  RF-only ⇒ the WebGUI shows the **"HF only"** badge. Any gateway involvement on
  either leg clears the badge. (Given the sticky-bit caveat in §2, "HF only" is a
  conservative claim: it can under-report but not over-report — exactly what EMCOMM
  needs.)
- **The receipt is repeated like the original**: `SendAckMessage()` output changes
  from fire-and-forget (`0xFF`) to retransmit-eligible (3×40 s, the existing L1
  machinery — it already handles text frames). This is step 1 of the presentation's
  "Lösungsrichtung" and alone removes the most common false-negative: message
  arrived, single receipt lost.
- Backward compatibility: old destinations send plain `:ackNNN` → sender marks the
  entry "delivered (legacy — unverified, path unknown)". Old senders receiving a v2
  ack still match it — **verified in code**: the fielded parser is
  `substring(iAckPos+4).toInt()` (`src/lora_functions.cpp:913`,
  `src/udp_functions.cpp:376`), i.e. `atol()`, which stops at `;` — `:ack123;H=…`
  parses as 123 on every fielded build. Phone apps never see the ack text at all:
  both RX paths convert it to the 7-byte binary 0x41 BLE notification before
  forwarding. Residual risk sits outside this repo (server and web clients that
  consume the ack text via UDP) and is checked during phase 3 rollout.

### 3.5 Matching receipts to the outbox

Receipt matching moves from the 20-deep `own_msg_id` displacement ring to the outbox
for stored messages. Each slot keeps **the full set of NNNs minted across its
attempts** (compressed — NNNs from one slot are near-consecutive; a handful of bytes
per slot). Latest-only would be wrong: with attempt spacing +10/+30/+60 min and
legitimate round trips of 4–40 min, an ack referencing the **previous** attempt is
the common case, and a plain legacy `:ackNNN` carries no content hash to fall back
on — a latest-only outbox would keep retrying against a destination that already
has the message. An ack matching _any_ minted NNN of a slot proves delivery of that
content. If the shared `node_msgid` counter wraps within the holding window and one
legacy NNN becomes ambiguous between two slots, the matcher refuses the ambiguous
match rather than guessing (v2 receipts disambiguate via the hash). This design
also fixes "late receipts find no entry after `own_msg_id[]` wraps": the outbox
entry lives 24 h, not 20-messages-worth of time. `own_msg_id` stays untouched for
non-stored traffic.

### 3.6 Sender-side UI reconciliation

The app and local displays must see **one** message with an evolving state, not one
message per attempt:

- The one-shot BLE echo of a sent message (`hasMsgFromPhone` guard) fires only for
  the original send; L2 resends are **not** echoed to the phone, and the
  unconditional `tdeck_add_MSG()` call in `sendMessage()`
  (`src/loop_functions.cpp:3733-3735`) is suppressed for outbox-originated resends —
  otherwise every retry adds a duplicate row to the T-Deck message view.
- When a receipt matches an outbox entry, the BLE ack notification (and the
  `own_msg_id` status update feeding the WebGUI messages page checkmarks) is
  synthesized for the **original** msg_id — the only one the app has ever seen. An
  ack carrying the msg_id of attempt N would otherwise confirm a message the app
  doesn't know, leaving the user's view unconfirmed forever while the outbox says
  "delivered".
- `sendMessage()` grows a result-returning variant (`{msg_id, status}` — enqueued /
  refused-backpressure / rejected) with the existing void signature kept as a
  wrapper. The outbox must know the minted msg_id/NNN of each attempt and must see
  refusals; today the function returns void and can silently do nothing
  (backpressure refuse, length reject, ring drop).

## 4. States as the user sees them

| state              | meaning                                    | display                                      |
| ------------------ | ------------------------------------------ | -------------------------------------------- |
| open — queued      | in outbox, before/between attempts         | attempt count, next action countdown         |
| open — probing     | probe sent, awaiting pong                  | "checking reachability"                      |
| open — heard       | own frame relayed at least once (implicit) | single gray check (as today's messages page) |
| delivered ✓✓       | receipt matched, hash verified             | sent-at + delivered-at, HF-only badge if set |
| delivered (legacy) | plain `:ackNNN` from old firmware          | as above, "unverified" tag, no badge         |
| failed             | holding time expired without receipt       | red, kept in history, user notified via BLE  |

The failure notification (BLE to app + display + WebGUI) is deliberately part of the
concept: today `RETRANSMIT_GIVEUP` is invisible, and "sender cannot distinguish never
arrived from receipt lost" is the core EMCOMM gap.

## 5. Protocol details

### 5.1 What changes on the wire — nothing structural

- Message frame `0x3A`: unchanged. `{NNN}` marker: unchanged.
- Receipt: same `0x3A` text-ack frame, payload extended after the digits (§3.4).
- Probe: same `0x3A` text frame with a `{prb}` payload marker; relayed by old nodes
  as ordinary text.
- No new payload type, no new flag bits, no change intermediate nodes can even
  detect. This keeps the "cheap steps need no coordination with other
  implementations" property from the presentation.

### 5.2 The hash

- Input: `source_call + '|' + dest_call + '|' + payload`, where `payload` is the
  **wire payload stripped at the first `{`** — the exact rule the receiver already
  uses to separate text from the ack marker (`src/lora_functions.cpp:962`). Both
  ends apply the same rule; the hash is computed over the post-escape wire text.
  Note the pre-existing bug this rule surfaces: a user `{` in the text already
  truncates display and produces a garbage ack NNN **today**; stored DMs either
  escape `{` at enqueue or reject it, otherwise the outbox retries 27 times against
  an unmatchable ack.
- Function: **truncated SHA-256 (first 4 bytes)**, as one small platform-neutral
  software implementation compiled on both platforms. Not the platform crypto
  stacks: mbedtls is linked only in the ESP32-only net console
  (`src/net_console.cpp:32`), and the Adafruit nRF52 core ships nRFCrypto/CC310
  with a different API (and hardware-crypto-from-RX-context questions). The hash is
  wire-visible and must match bit-for-bit across platforms — one shared software
  routine, hashing ≤200 B in microseconds, removes the whole dependency question.
  8 hex chars in the receipt.
- Honest labeling: this is an **integrity** stamp, not authentication. Anyone can
  compute it. A forged "delivered" receipt is possible for an attacker who hears the
  message — same trust level as every other MeshCom frame today (callsigns are
  unauthenticated). The concept documents this openly; keyed authenticity is the
  named follow-up, not smuggled in half-done.

### 5.3 Compatibility matrix

| sender | destination | result                                                         |
| ------ | ----------- | -------------------------------------------------------------- |
| new    | new         | full feature set: verified receipt, HF badge, no dup display   |
| new    | old         | delivery works; retries may display duplicates at destination; |
|        |             | plain receipt → "delivered (legacy)"; **no probes** (§3.3 —    |
|        |             | destination not v2-known), ladder runs unprobed                |
| old    | new         | unchanged behavior; v2 receipt suffix verified harmless (§3.4) |
| old    | old         | unchanged                                                      |

## 6. Retry timing — why these numbers

- L1 covers 0–120 s with the same msg_id: correct for first-hop loss, and dedup
  keeps it cheap mesh-wide.
- DG0OPK measured per-hop queue latency under load: text median 30 s, p99 286 s.
  Worst-plausible legitimate round trip at 4 hops out + receipt back:
  ~8 × p50…p99 ⇒ 4–40 min. An L2 retry before ~10 min mostly produces duplicates.
  Hence: +10 / +30 / +60 min, then hourly — 27 attempts max in 24 h, each preceded
  by a cheap probe, each individually skippable under congestion.
- Internet leg: node→server→gateway is seconds. The +3 min fast second attempt on
  server-connected nodes covers "UDP frame lost" without waiting 10 min.
- Airtime cost ceiling per stored message per day: ≤27 probes (~30 B) + a handful of
  full resends ≈ well under one position beacon's daily budget; bounded, and zero
  when the first attempt succeeds (the common case — text delivery is 99.2–99.7 %
  in the field data; this feature exists for the tail and for offline recipients).

## 7. Configuration and UI

### 7.1 Settings (persisted)

| setting       | default | range  | struct field     |
| ------------- | ------- | ------ | ---------------- |
| store enabled | on*     | on/off | `node_msgstore`  |
| holding time  | 24 h    | 1–72 h | `node_storetime` |

*on where compiled in (§8); the command reports "not supported on this hardware"
elsewhere (same pattern as `--spiffs`).

Plumbing — **without a `FLASH_STRUCT_VERSION` bump.** The advisor pass caught that
the originally drafted recipe ("bump the version") is, by design, a settings wipe:
`flashLayoutCompatible()` failure clears NVS on ESP32 (`src/esp32/esp32_main.cpp:742`
→ `clear_flash()`) and formats the filesystem on nRF52 — and on nRF52 any `sizeof`
change already hard-resets via the N-12 size guard
(`src/nrf52/nrf52_flash.cpp:333-346`) before version logic even runs. A feature
release must not cost the fleet its callsigns and WiFi credentials. Instead:

- ESP32: NVS is key-value — add the two keys with defaults in load
  (`preferences.getInt("node_msgstore", 1)` pattern) and matching puts in save.
  Absent keys read defaults; no version change needed.
- nRF52: append the new fields at the **end** of `s_meshcom_settings` (not above
  the `// nicht im Flash` line — an insertion shifts the tail and breaks prefix
  compatibility of the blob), and extend `init_flash()` with a size-tolerant read:
  if `stored_size` equals the known previous `sizeof`, read that many bytes,
  default the tail, `save_settings()` once. The `MESHCOM_COMPAT_MARKER` migration
  path is the precedent.
- Both: `CFG_FIELD_LIST` rows in `src/config_json.cpp`,
  `sanitize_loaded_settings()` clamps, defaults in both struct copies.
- `FLASH_STRUCT_VERSION` stays untouched.

### 7.2 Serial USB

- `--storetime <1..72>` — set holding time in hours (echo `[MSGSTORE];time;24`).
- `--msgstore on|off`, `--msgstore stat` — enable/status (model: `--persist*` block,
  `src/command_functions.cpp:4906-4953`).
- `--outbox` — dump the table: state, dest, attempt, timestamps, flags.
- `--outbox clear` — drop all non-OPEN history entries.
- `--help` and `--info`/`sendNodeSetting()` (`TYP:"SN"` JSON) extended accordingly.

### 7.3 WebGUI

- New page `/?page=outbox` + AJAX partial `/?getoutbox`, cloned from the
  `sub_page_messages()` / `sub_content_messages()` pair
  (`src/web_functions/web_functions.cpp:1254/:1545`) which already demonstrates
  timestamps, delivery-state glyphs and the auto-refresh poller. Columns: state
  glyph, destination, message (truncated once delivered), sent-at, delivered-at,
  attempts, **HF-only badge**, hash-verified mark.
- Nav button (conditionally rendered only when the feature is compiled in — same
  pattern as the `bMCP23017` button), entry in the `autorefresh()` JS list, and the
  two settings exposed via `webSetup_setParam()`/`getParam()` on the setup page.
- `Web-API_documentation.txt` updated.

The WebGUI exists on RAK4631 too (Ethernet via RAK13800), so the page works there
unchanged; note the `bSPI_ETH_Active` guard — the outbox page handler must stay
cheap and allocation-light like the existing pages.

## 8. Hardware gating and persistence

Compile-time capability define `ENABLE_MSGSTORE`, set in the RAM-class ladder at
`src/configuration_global.h:195-235` (where `MAX_RING`/`MAX_DEDUP_RING` already
branch):

| platform                               | feature | why                                       |
| -------------------------------------- | ------- | ----------------------------------------- |
| ESP32-S3 (Heltec V3, T-Deck, Supreme…) | yes     | ~200 kB DRAM free                         |
| nRF52840 (RAK4631)                     | yes     | ~170 kB RAM free                          |
| classic ESP32 (E22, T-Beam v1.x)       | **no**  | 1.1 kB DRAM / 28 B IRAM headroom (MEM-01) |
| `ENABLE_TBEAM` dev builds              | no      | reduced-buffer dev class                  |

**Persistence across reboot: v1 is RAM-only.** Reasons: nRF52 has 28 KB LittleFS
written as a single settings blob, LittleFS writes are loop-task-only (documented
boot-loop otherwise), and per-state-change writes are flash wear on both platforms
(NVS on ESP32 likewise). A node reboot therefore loses the outbox — documented
limitation, honest trade-off. v2 option, T-Deck/SD only: piggyback on the existing
`node_persist_to_sd` machinery (mheard already does `/mheard.dat`). PSRAM is not the
answer here (only T-Deck family has it, and 1.2 kB doesn't need it).

## 9. Implementation phasing

Aligned with the presentation's "Lösungsrichtung" order — each phase independently
shippable and independently valuable:

1. **Receipt hardening** (cheapest, biggest win): `SendAckMessage()` output becomes
   retransmit-eligible (3×40 s); `RETRANSMIT_GIVEUP` reported to app/display/WebGUI —
   **scoped to user-originated DMs only** (unscoped, every node in a sparse topology
   would report bogus failures about its own acks and broadcasts, which legitimately
   burn all 3 retransmits since nothing acks an ack). Same PR: ack generation moves
   to the loop task with `millis()`-minted msg_ids (§3.4) — the timer-task
   flash-write hazard must not be multiplied by ack retransmission. Verified safe
   against the installed base: retransmitted acks are byte-identical and swallowed
   by the msg_id dedup ring at old senders (`src/lora_functions.cpp:786,1871`).
2. **Outbox + L2 ladder + states + UI reconciliation + WebGUI/serial/config**: the
   core of this concept (§3.1, §3.2, §3.5, §3.6, §7). Receipts still plain
   `:ackNNN`; matching via the outbox NNN sets.
3. **Receipt v2** (`H=`, `F=R`) + receiver content-dedup cache (both RX paths) +
   `{prb}` probe with the v2-capability gate. Requires updated firmware on both
   ends to shine; degrades gracefully.
4. (separate discussion, not this concept) server-side mailbox; keyed authenticity.

Each phase lands as its own upstream PR with German description, targeting `dev`,
minimal diff (project rules).

## 10. Known issues, border cases, open questions

Updated after the advisor pass — resolved items moved into the design sections
above; what remains here is genuinely open or accepted-with-caveat.

### 10.1 Verified during the advisor pass (no longer open)

- **Old-sender tolerance of receipt v2:** verified tolerant —
  `substring(iAckPos+4).toInt()` stops at `;` (§3.4). Phone apps only ever receive
  the 7-byte binary 0x41 conversion, never the ack text.
- **`checkVia()`/escaping on re-sends:** unfounded — `sendMessage()` builds a fresh
  `aprsMessage` per call and `checkVia()` writes only the destination path. Store
  the raw pre-escape text; hash the post-escape wire payload (§5.2).
- **Fresh msg_ids break nothing in-firmware** (dedup, `checkOwnTx`, relay) — each
  attempt pre-registers via `insertOwnTx()`/`addLoraRxBuffer()`. Remaining exposure
  is analytics-side only (see 10.2).
- **`node_pingtime` side effects:** confirmed; the probe uses its own state, not
  `bPingSend`/`node_pingtime` (which suppresses `sendHey()` while set).

### 10.2 Real design tensions

- **Flash wear (sender side):** every L2 attempt goes through `sendMessage()` and
  thus `node_msgid++` + `save_settings()` — up to ~27 extra writes per stored
  message per day. (Probe and ack writes are already eliminated by the
  `millis()`-minting rules in §3.3/§3.4.) NVS wear-leveling on ESP32 tolerates it;
  the nRF52 whole-blob rewrite is the concern. Option: persist `node_msgid` lazily
  (every 16 increments, +16 slack on boot) — a separate small fix with its own risk
  (msg_id reuse after crash collides with mesh-wide dedup for ~40 min). **Open.**
- **Duplicates at old destinations** (compat matrix): unavoidable by design. The
  per-dest cap (3) and the ladder bound the annoyance. Communicate in release notes.
- **Content-dedup false positive:** user legitimately sends the identical text twice
  to the same destination within 24 h ("ok" … "ok"). The receiver cache would
  suppress the second display. Mitigation: include the outbox `created_at` (minute
  granularity) in the hash? — but then a retry after the minute boundary changes the
  hash. Alternative: cache keys on (source, content-hash, **first-seen NNN window**).
  Needs a decision; currently the concept accepts the limitation for identical texts
  and documents it (EMCOMM messages are rarely byte-identical; "ok" arguably is).
  **Open.**
- **`{prb}` as a tracking/reflection primitive:** v2 destinations answer probes
  silently — reachability of any callsign becomes a mesh-wide invisible query, and
  spoofed-source probes reflect `{prb-ack}`s at a third party (amplification ≈1:1).
  Mitigations in §3.3 (per-source 30 s floor, global reply budget, answer only
  mheard-known sources); a source-rotating spoofer sidesteps the per-source limit —
  accepted and documented, same trust level as today's unauthenticated callsigns.
- **Analytics skew:** every L2 attempt uploads to the server as a brand-new message
  (fresh msg_id) and fans out to all gateways; server-side dedup is not verifiable
  from this repo — assume none at content level. mcmap message counts and
  delivery-rate studies (DG0OPK-class) must learn to fold retries by content hash,
  or the feature skews the numbers used to justify it. Release-notes item.
- **Epoch quality:** on BLE-only nodes time may be `CPU`/`INIT`. Expiry therefore
  runs on `created_millis` (monotonic) with the epoch fields display-only
  (`> 1000000000` guard, as `loadTimePersistence()` does). millis rollover at 49.7
  days must be handled with the subtraction idiom, not comparison (the
  `timer + interval < millis()` overflow bug class is documented in the HEY storm
  analysis).
- **"HF only" is conservative, not exact:** the sticky `msg_server` bit means a
  frame that merely passed a server-connected gateway acting as RF relay is marked —
  the badge under-claims. Also the gateway-origin caveat (§2). Fine for EMCOMM
  (never falsely claims RF-only) but must be documented in the GUI.
- **Sender offline vs. destination offline is indistinguishable** to the user
  looking at "open": the outbox shows attempts and probe results, which is the best
  available signal. No fix planned.
- **Group messages / broadcast:** out of scope; the outbox accepts DMs only
  (`bDM == true` path). The presentation's missing "confirmed delivery for
  group/broadcast" stays a gap — different problem (ACK implosion).

## 11. Advisor review outcome (2026-08-30)

An independent adversarial advisor pass was run against draft v1 with full code
access; every finding carries verified file:line evidence. Full report:
`docs/review/advisor-dm-store-and-forward-20260830.md`. All findings are folded
into the sections above:

| finding | severity | disposition                                                    |
| ------- | -------- | -------------------------------------------------------------- |
| B1      | BLOCKER  | `FLASH_STRUCT_VERSION` bump = fleet-wide settings wipe → §7.1  |
|         |          | rewritten: no bump; NVS defaults; nRF52 append + tolerant read |
| M1      | MAJOR    | 5-min probe window self-defeating → §3.3: late answers count,  |
|         |          | window = ladder step, probe priority NORMAL not CRITICAL       |
| M2      | MAJOR    | latest-only NNN misses acks for prior attempts → §3.5: keep    |
|         |          | all minted NNNs per slot; refuse ambiguous legacy matches      |
| M3      | MAJOR    | app never learns L2 outcome; T-Deck duplicate rows → new §3.6  |
| M4      | MAJOR    | `{prb}` renders as junk DM on old destinations → §3.3:         |
|         |          | v2-capability gate; unknown destinations run unprobed          |
| M5      | MAJOR    | DM-ack path runs in nRF52 timer task and writes flash there    |
|         |          | today → §3.4/§9: ack deferred to loop task, millis-minted ids  |
| M6      | MAJOR    | dedup cache must hook the UDP RX path too; re-ACK flash cost   |
|         |          | on destination → §3.4                                          |
| M7      | MAJOR    | tri-state promise vs RAM-only store → §1 scoped to continuous  |
|         |          | uptime + boot-loss notice; §8 persistence option unchanged     |
| m1–m10  | MINOR    | folded into §3.1 (receipt ring), §3.3 (millis probe ids, reply |
|         |          | budget), §5.2 (software SHA-256, first-`{` strip rule, the     |
|         |          | pre-existing `{`-in-text bug), §9 (DM-scoped give-up notices), |
|         |          | §10.2 (NNN wrap, tracking/reflection, analytics skew), §7.3    |
|         |          | (keep the synchronous page tiny)                               |

The advisor confirmed as sound: the L2 fresh-msg_id decision (same-id resends are
provably swallowed mesh-wide), the §2 description of the current machinery (every
spot-checked reference accurate), `{prb}` relayability through old intermediates,
the hardware-gating ladder, the conservative "HF only" semantics, and phase 1's
safety against the installed base.
