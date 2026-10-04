# Store node as gateway: taking PMs from the central server

Status: stage 1 and option 3b (`STOR`) IMPLEMENTED on fork-dev 2026-10-04 (`--stor`, default off,
pending the server operator's approval); option 3a (echo-HEY) dropped (SNF-D6). Original text,
2026-10-02: Line numbers refer to `fork-dev` at
`42dbf03a`.

## 1. Bottom line

A store node (S&F, `ENABLE_MSGSTORE`) that also runs as a gateway does not hold PMs that reach it
from the central server today. Three things are missing:

1. **Store hook on the server path.** The mailbox hooks sit only in the RF receive path. A PM that
   arrives as a `GATE` datagram is put on LoRa once and forgotten.
2. **Feedback to the server.** The `:sto` custody notice is sent on LoRa only. A sender on the
   internet side never learns that the message is held.
3. **Routing.** The server has to send the PM to the store node in the first place. How the server
   picks gateways for a PM is unknown. If it only serves gateways that recently uploaded a frame
   from the destination, the store node drops out of that set as soon as the destination goes
   quiet, which is exactly when the mailbox is needed.

Proposal in three stages:

| Stage | Content                                                                    | Depends on the server           |
| ----- | -------------------------------------------------------------------------- | ------------------------------- |
| 1     | Store hook in both `GATE` handlers, `:sto` upload, echo guard              | no                              |
| 2     | Measure how the server routes PMs (section 4)                              | no, observation only            |
| 3a    | Echo-HEY: re-announce directly heard nodes to the server as `DATA` uploads | no, works with today's server   |
| 3b    | `STOR` datagram: explicit "I take PMs for these calls"                     | yes, server must learn the type |

Stage 1 is useful on its own and may already be sufficient: if stage 2 shows that the server hands
PMs to every gateway regardless of who heard the destination, stages 3a/3b are not needed. 3a and
3b are not exclusive. Both sit behind one setting so the node can switch when the server changes.

## 2. How it works today

### 2.1 Gateway to server (UDP 1990)

| Datagram | Built in                                  | Content                                                                           |
| -------- | ----------------------------------------- | --------------------------------------------------------------------------------- |
| `KEEP`   | `sendKEEP()`, `udp_functions.cpp:1261`    | `KEEP` + gateway ID (8 hex) + call (9) + version (4+1) + group list `20;232;262;` |
| `DATA`   | `addNodeData()`, `udp_functions.cpp:1307` | `DATA` + gateway ID + call + version + RSSI (4) + SNR (4) + `03` + raw mesh frame |

There is no datagram that says "I hear node X". The server can only infer it from `DATA` uploads:
every frame received on LoRa that no other gateway has touched yet, and every HEY, is uploaded with
the receiving gateway's RSSI/SNR (`lora_functions.cpp:2142-2174`). A HEY gets the gateway's signal
report group `ncnt,rssi,snr;` appended to its payload first (`appendHeySignalReport()`).

### 2.2 Server to gateway

`GATE` + raw mesh frame. The handler (`esp32/udp_frame_esp32.cpp`, twin
`nrf52/udp_frame_nrf52.cpp`) decodes the frame, appends the own call to the source path, sets the
server flag, and for a text frame that is not addressed to this node enqueues it for LoRa exactly
once (`addTxRingEntry(..., "udp_rx")`, ESP32 line 502, nRF52 line 492). No retransmission, no
mailbox.

### 2.3 Mailbox

- Store set (`msgstoreEligible()`, `msgstore.cpp:279`): `own` (own base call), `list`
  (`--storecall`), `heard` (direct edge in the neighbour matrix younger than 12 h,
  `glueHeardAgeMs()`; this is direct reception only, relayed sightings do not count).
- Hooks, all in `OnRxDone` only: store (`lora_functions.cpp:1851`), purge on `:ackNNN` (`:1841`),
  peer cancel (`:1870`), presence (`:1414`, a frame heard directly from the destination arms the
  delivery ladder).
- Delivery: hop-0 frame, source = original sender, path `SENDER,OWNCALL`, fresh `msg_id`
  (`glueDeliver()`, `msgstore_glue.cpp`). Not uploaded to the server.
- `:sto` notice: built by `glueNotify()`, LoRa only, relayed at `max_hop_text`.

## 3. Stage 1: store hook and feedback

### 3.1 Store hook in the `GATE` handlers

In the text branch of both handlers, for a frame that is new to the dedup ring and whose
destination is not this node, apply the same three decisions the RF path makes:

| Case                                 | Action                                                  |
| ------------------------------------ | ------------------------------------------------------- |
| payload contains `:ackNNN`           | `msgstoreOnAck(source, destination, nnn)`               |
| DM with `{NNN`, destination eligible | `msgstoreStore(source, destination, nnn, payload, len)` |
| anything else                        | nothing                                                 |

Exclusions are identical to `lora_functions.cpp:1844-1851`: no broadcast `*`, no group, no `:rej`,
no control frame starting with `{`, no PN repeat copy. The one-shot LoRa transmission stays as it
is: it is the first delivery attempt, and the existing 60 s rule in `msgstorePresence()` keeps the
ladder from competing with it.

To keep the two paths from drifting, the condition moves into one helper shared by `OnRxDone` and
both `GATE` handlers; the peer-delivery test stays RF-only (it needs `rly_hop`).

### 3.2 Echo guard (new, required)

The store node's own hop-0 delivery can be heard by another gateway, uploaded, and come back as a
`GATE` frame with a `msg_id` this node has never seen (the delivery is deliberately not put in the
own-TX table). Without a guard the hook would treat it as a fresh sighting and refresh
`stored_ms`, pushing the hold time out on every delivery attempt.

Rule: a `GATE` text frame whose source path already contains the own call is never stored.

### 3.3 Feedback to the server

| Event           | Today                                  | Stage 1                                                       |
| --------------- | -------------------------------------- | ------------------------------------------------------------- |
| `:sto` notice   | LoRa only                              | additionally `addNodeData(buf, len, 0, 0)` when gateway + IP  |
| delivery `:ack` | uploaded by the normal RF path already | unchanged; verify on the bench                                |
| hop-0 delivery  | not uploaded                           | unchanged (uploading it would re-inject the PM into the mesh) |

The `:sto` upload uses the shape the node already uses for its own text frames
(`loop_functions.cpp:4470`). Open: whether the LoRa copy of `:sto` is suppressed when the stored PM
came in via `GATE` (the sender is then by definition not in RF range of this node's region). Default
proposal: keep both, the notice is one short frame.

### 3.4 Points to verify

- **Task context on nRF52.** `OnRxDone` runs in the LORA task, the `GATE` handler in the loop task.
  The mailbox core already tolerates the LORA task mutating a slot during a loop-task delivery
  (`gen` counter). A second writer from the loop task is a new combination and needs a look at
  `msgstoreStore()` before coding.
- **Same PM via RF and via server.** Same `(src, nnn, payload)` refreshes the existing slot
  instead of taking a second one; the dedup ring normally stops the second copy earlier. Covered by
  a native test.
- **Twin test.** `test/test_udp_frame_twin` gets an agreement case per row of the table in 3.1.

## 4. Stage 2: measure the server

The server is a black box. Before choosing between 3a and 3b, three questions are answered on the
bench with `--udplog` on two own gateways A and B and one own node X (own callsigns only, PM from
an internet-side client):

| ID  | Question                                                   | Method                                                           |
| --- | ---------------------------------------------------------- | ---------------------------------------------------------------- |
| M1  | Does a PM for X reach only gateways that uploaded X?       | X heard by A only; check whether B receives the `GATE` frame     |
| M2  | How long after X's last upload does A still get PMs for X? | switch X off, send a PM every 10 min, note when `GATE` stops     |
| M3  | Which upload counts as "heard": any frame type or HEY/POS? | X sends only text; repeat M1                                     |
| M4  | What does the server do with an unknown 4-byte indicator?  | send one `STOR` datagram, watch for disconnect or missing `BEAT` |

Outcome decides the rest:

- M1 = "all gateways get it": stage 1 is the whole feature. Stop.
- M1 = "only gateways that uploaded X", M2 short: stage 3a or 3b is needed; M2 gives the refresh
  interval for 3a.
- M4 = "ignored": `STOR` can ship ahead of server support without harm.

## 5. Stage 3: telling the server which nodes we take PMs for

### 5.1 The announce set (both options)

A call is announced only if **both** hold:

1. it is in the store set of the active mode, and
2. it was heard **directly** on LoRa within the last 12 h (live edge `(x, 0)` in the neighbour
   matrix, `nbrMhRows()` with `NBR_WINDOW_MIN`).

The own call is not announced; `KEEP` already identifies the gateway.

Condition 2 is a security rule, not an optimisation. Without it `--storecall` would let anybody
list a foreign callsign and draw that station's PMs to their gateway. With it, `list` mode can only
narrow the set, never widen it beyond what the node really hears. Consequence: a node that has been
silent for more than 12 h drops out and its PMs stop being routed here; mail already held stays
until `--storetime` expires.

Size: at most the direct slots of the matrix (`NBR_EXT_SLOTS`: 64 on S3/nRF52), typically far
fewer.

### 5.2 Option 3a: echo-HEY (works with today's server)

For each call in the announce set the gateway uploads a synthetic HEY as a `DATA` datagram. It is
**never** transmitted on LoRa.

| Field         | Value                                                                              |
| ------------- | ---------------------------------------------------------------------------------- |
| payload type  | `@`                                                                                |
| source / path | the heard node's call                                                              |
| destination   | `H` (never `HG`: we must not claim the node is a gateway)                          |
| payload       | `R<ncnt>;` with the node's last reported neighbour count, plus our report group    |
| RSSI / SNR    | last values of the direct edge (matrix), in the `DATA` header and the report group |
| hw / mod      | last values from the matrix row                                                    |
| `msg_id`      | see below                                                                          |

Cadence: one echo per call when the last real upload from that call is older than the refresh
interval (from M2; starting value 30 min), jittered, at most one echo per 10 s per gateway. A real
frame from the node resets its timer, so an active node is never echoed.

No position echoes: a stale position on the map is worse than none, and HEY is enough to mark a
node alive.

`msg_id`: the upper 22 bits of a real HEY are the originator's hardware ID, which the matrix does
not keep. Two ways:

- keep the upper bits of the last real `msg_id` per direct slot (4 B x 64 slots) and count the
  lower 10 bits ourselves; the echo is then indistinguishable from a real HEY for the server, or
- use our own ID bits; cheaper, but a server that checks ID against callsign may drop it.

Proposal: the first, unless M-series tests show the server does not care.

Risks, stated plainly:

- **It is a forged frame.** The node did not send it. The server, the map and every statistic built
  on server data will show the node as alive for up to 12 h after it went off the air.
- **Routing theft.** If the server routes to the last gateway that uploaded a node, our echo can
  pull PMs away from a gateway that currently hears the node better. The delivery ladder only
  starts on direct presence, so such a PM waits in our mailbox while the other gateway could have
  delivered it. The one-shot LoRa send on arrival limits the damage but does not remove it.
- **Detectability.** Other operators cannot tell an echo from a real HEY. If that is unacceptable,
  the echo carries a marker in the payload (for example a trailing `S;` group); whether today's
  server tolerates it is part of M3.
- **Upstream acceptance.** This is a workaround that a reviewer of the upstream PR is likely to
  question. It should ship behind a setting that defaults to off.

### 5.3 Option 3b: `STOR` datagram (clean, needs the server)

A new gateway-to-server datagram with the same header as `KEEP`:

```
STOR<gwid:8 hex><call:9><ver:4><sub:1><hold_h>;<seq>/<total>;<CALL1>;<CALL2>;...;\0
```

Example: `STOR1A2B3C4DDK5EN-90 4.40a24;1/1;DK5EN-92;DK5EN-93;`

| Field       | Meaning                                                            |
| ----------- | ------------------------------------------------------------------ |
| `gwid`      | node ID of the store node (`_GW_ID`), same as in `KEEP`/`DATA`     |
| `call`      | gateway call, padded to 9                                          |
| `hold_h`    | `--storetime` in hours, so the server knows how long custody lasts |
| `seq/total` | chunk counter; a datagram stays below `UDP_TX_BUF_SIZE` (255 B)    |
| `CALLn`     | the announce set from 5.1; about 20 calls fit into one datagram    |

Semantics for the server:

- The list is a full snapshot, not a delta. An empty list withdraws everything.
- Sent every 15 min together with the `KEEP` rhythm and once (debounced 60 s) when the set changes.
- A snapshot that is not refreshed for 3 intervals (45 min) expires.
- Expected server behaviour: a PM for a listed call is sent to this gateway **in addition to**
  whatever the server does today, never instead of it.

Compared with 3a nothing is forged, the map stays truthful, and the server can show "mailbox at
DK5EN-90". The cost is that it does nothing until the server implements it, and that is outside
our control.

### 5.4 Setting

One setting, `--storeannounce off|hey|stor|both`, default `off`. `both` is the migration mode: the
echo keeps today's server routing alive while `STOR` is already sent for a server that understands
it. Once the server honours `STOR`, the node is switched to `stor` and the echo stops.

The setting needs a free settings bit pair; allocation is checked against the existing `node_sset*`
use before coding (there has been a bit collision in `node_sset4` before).

### 5.5 Comparison

| Criterion                         | 3a echo-HEY                    | 3b `STOR`                   |
| --------------------------------- | ------------------------------ | --------------------------- |
| Works with today's server         | yes, if M1/M3 confirm          | no                          |
| Truthful toward server and map    | no                             | yes                         |
| Can divert PMs from a better path | yes                            | no, if the server adds only |
| UDP load                          | one datagram per call per 30 m | one datagram per 15 min     |
| RAM                               | 256 B for the ID bits          | none                        |
| Upstream acceptance               | doubtful                       | needs server maintainers    |

## 6. Recommendation

1. Build stage 1 now. It has no dependency and fixes two real gaps.
2. Run stage 2 on the bench. It costs an evening and decides whether stage 3 is needed at all.
3. If routing does depend on uploads: propose `STOR` (3b) to the server maintainers, and ship 3a
   behind the setting as the bridge until they answer.

## 7. Open questions

1. Is the forged HEY acceptable without a marker, or must echoes be recognisable?
2. Should the LoRa copy of `:sto` be suppressed when the PM arrived via `GATE`?
3. Who talks to the server maintainers about `STOR`, and is the field list in 5.3 what they need
   (for example: do they want the node ID per listed call, which the firmware does not have today)?
4. Is 12 h the right window for the announce set, or should announcing use a shorter one than
   storing?
5. Boards: mailbox exists on ESP32-S3 and RAK4631 only; a RAK gateway means Ethernet. Is that the
   intended store-gateway hardware?

## 8. Tests

- Native: shared eligibility helper; twin agreement cases for the `GATE` hook; echo guard;
  announce-set builder (store set intersected with direct-heard, 12 h edge, `list` cannot widen);
  `STOR` encoder incl. chunking; echo-HEY encoder and refresh timer.
- Bench (own callsigns only, PMs to own nodes, no broadcast): server PM to a silent node is held,
  `:sto` arrives at the internet-side sender, node comes back, PM is delivered, `:ack` reaches the
  sender, slot is purged; then M1-M4.
