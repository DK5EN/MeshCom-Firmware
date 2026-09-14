# Client integration: DM delivery status and the store node

For mc-chat, MCProxy and Meshcom-MobileApp. Describes what the node now sends and what each
client must implement to show DM delivery honestly and to understand a store-and-forward node.
Firmware state: fork-main 66dea241 (2026-09-14), stages 0, 2.1, 3 and 4 of
`docs/dm-transport-impl-plan-20260913.md`.

## 1. What changed on the node

| Stage | Node behaviour a client can observe                                                                                                                                                        |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0     | A DM whose retries all fail now reports **failed** to the app (status `0x03`). ACKs no longer cost a flash write. A duplicate DM for this node is re-acked instead of dropped.             |
| 2.1   | The same DM arriving twice (relay copy, sender retry, LoRa plus server) is shown **once**; the second copy is acked silently. Keyed on source callsign plus the `{NNN` tag, not on msg_id. |
| 3     | A node with `--store` on keeps DMs for neighbours it heard directly in the last 12 h and delivers them at hop 0 when the neighbour is heard again. Nothing on air tells the sender.        |
| 4     | The store node tells the sender with a text `:stoNNN`. New firmware turns that into status **held** (`0x04`) with the holder's callsign; old firmware shows it as a short chat line.       |

Nothing here changes how a client sends a DM. Everything a client needs arrives on the existing
`0x41` status frame plus one new text form it may see on old nodes.

## 2. The `0x41` status frame

Emitted by the node to the phone for one of its own sent messages. Layout as the firmware builds
it (`src/ack_attribution.h`, `buildAckPhoneFrame()`); the BLE transport prepends one length
byte, so on the client side every offset below is **+1** (MCProxy's `_decode_ack_frame()` reads
msg_id at 2..5 and status at 6 for exactly that reason).

| Offset (node) | Content                                                |
| ------------- | ------------------------------------------------------ |
| 0             | `0x41`                                                 |
| 1..4          | msg_id of the sent message, little endian              |
| 5             | status, see table below                                |
| 6             | n = length of the attribution callsign, 0 = old format |
| 7..7+n-1      | callsign, `[A-Z0-9-]`, no NUL, n <= 9                  |
| then          | 4 bytes time, appended by the transport (as before)    |

| Status | Name    | Meaning                                                         | Attribution     | Since      |
| ------ | ------- | --------------------------------------------------------------- | --------------- | ---------- |
| `0x00` | heard   | a neighbour repeated the frame                                  | that neighbour  | existing   |
| `0x01` | gateway | a gateway or the server took the message                        | gateway call    | existing   |
| `0x02` | acked   | the addressee's own `:ackNNN` matched                           | the addressee   | existing   |
| `0x03` | failed  | all retries exhausted, nobody acked; user DMs only              | the destination | 2026-09-13 |
| `0x04` | held    | a store node holds the DM for the absent destination; not final | the holder      | 2026-09-14 |

Rules a client must follow:

- **Walk the frame by length.** Read n and skip n bytes. Never assume a fixed length.
- **Unknown status values are not errors.** Log and ignore; do not treat as acked. The mobile app
  already does this; MCProxy labels them `unknown(...)`; mc-chat does not parse `0x41` at all.
- **Precedence for one message:** `acked` is final. `failed` is final unless `acked` arrives
  later (the ACK still wins). `held` is a waiting state: it never overrides `acked` or `failed`,
  it replaces `sent`/`heard`/`gateway`, and a later `acked` replaces it. Several `held` frames
  with different holders are legitimate; show the most recent holder, keep the others if you have
  room.
- **The node rate-limits `held`** to one frame per holder and message per hour, so a client
  never sees a storm; it may see the same holder again after an hour.
- A `failed` frame is never sent for a message that is `held`. The message stays `held` until
  the destination acks. There is no "dropped by the store node" signal; if a message stays held
  for longer than the store node's hold time (default 24 h), the client may show it as stale.

Suggested client state per sent DM:

```
sent -> heard -> gateway -> held(holder) -> acked
                        \-> failed
```

## 3. The `:stoNNN` text (only visible on old firmware)

The store node sends the sender an ordinary text frame:

```
source       DK5EN-90            store node
destination  DK5EN-93            the DM's sender
payload      "DK5EN-93 :sto017 DK5EN-14"   sender padded to 9, the NNN, the held destination
```

New firmware consumes it (no display, no forward to the phone, no ack) and emits the `0x04`
frame instead. A node on **upstream firmware** forwards it to the phone as a normal DM from the
store node. Clients therefore see it only behind old nodes. Recommended: treat a DM text matching
`^\S{1,9}\s*:sto\d{3}( \S+)?$` from a node that never sends `0x04` as informational ("DK5EN-90
holds your message 017 for DK5EN-14"), do not answer it, and never filter it silently, because on
old nodes it is the only signal the operator gets.

The same padded-callsign layout exists for `:ackNNN` and `:rejNNN` texts; new firmware consumes
those as well.

## 4. Per-client checklist

### MCProxy (`src/mcapp/ble_protocol.py`, `push_delivery.py`)

- `ACK_KIND_BY_TYPE` (line ~62): add `0x03: "failed"` and `0x04: "held"`. `ack_type_text()`
  likewise.
- `transform_ack()` / the `msg_status` SSE event: it already passes the callsign appendix through;
  make sure the `held` event carries it as `holder` so mc-chat and the web app can show "held by
  DK5EN-90". For `failed` the appendix is the destination.
- Push notifications (`push_delivery.py`): `failed` deserves a push ("not delivered to X");
  `held` is optional and should be quiet by default; `acked` after `held` is the interesting one
  ("delivered to X after being held by Y").
- Message store: persist the last status and the holder per msg_id; apply the precedence rules
  in section 2 on ingest, not in the UI.
- Text filter at `push_delivery.py:249` (`:ack` substring): extend to `:rej` and `:sto` for the
  push suppression only; keep the message itself.

### mc-chat (`meshcom_mock/decoder.py`, models, UI)

- `_decode_aprs_struct()` (~843-851) treats `0x41` as an APRS packet and only extracts
  `ascii_payload`. Implement the frame from section 2: msg_id, status, n, callsign. Either
  parse it locally or rely on MCProxy's `msg_status` SSE event with `ack_kind` and `holder`.
- Data model: add `status` (sent, heard, gateway, held, acked, failed) and `holder` to a sent
  message; apply the precedence rules on update.
- UI: one glyph per state; `held` shows the holder on hover or inline ("held by DK5EN-90");
  `failed` is visibly distinct from `acked`. Age the `held` state visually after 24 h.
- No client-side filter on `:ack`/`:rej`/`:sto` texts exists today; section 3 applies.

### Meshcom-MobileApp (`src/hooks/MessageHandler.ts`, `src/pages/Chat.tsx`, `src/utils/AppInterfaces.ts`)

- `MessageHandler.ts` (~693-726) already reads status, n and the callsign. `ackTxtMsg()`
  (~340-368) maps `0x00 -> ack 1`, `0x01/0x02 -> ack 2` and ignores the rest. Add
  `0x03 -> ack 3 (failed)` and `0x04 -> ack 4 (held)` and store the callsign in a new
  `ack_by` field on the message row. Never let 3 or 4 overwrite 2; never let 4 overwrite 3;
  let 2 overwrite everything.
- `Chat.tsx` (~1318-1328): icons for ack 3 (failed, e.g. `cloudOfflineOutline`) and ack 4
  (held, e.g. `mailOutline` with the holder as title/subtitle). Keep 1 and 2 as they are.
- Database migration for the new column; default 0.
- Section 3 texts: the app shows any DM text; a small parser for `:sto` can render it as an
  info line when the connected node is on old firmware.
- `NodeSettings` (AppInterfaces.ts) has no store field; see section 5.

## 5. Store node configuration surfaces

The store role is fork-only for now (ESP32-S3 and nRF52840 nodes). A client does not need any
of this to show delivery status; it needs it only to configure or inspect a store node.

Console and BLE commands (`docs/commands-store-node.md`):

| Command                         | Effect                                                                                   |
| ------------------------------- | ---------------------------------------------------------------------------------------- |
| `--store off\|own\|list\|heard` | role and store set; enabling prints a 24/7-power and RAM warning                         |
| `--storecall CALL1,CALL2,...`   | store set for `list` mode, up to 16                                                      |
| `--storetime <h>`               | hold time, 1..168, default 24                                                            |
| `--storeslots <n>`              | 1..50, default 50                                                                        |
| `--storenotice on\|off`         | send the `:sto` notice to senders, default on                                            |
| `--mbox`                        | one `[MBOX];slot;dst;src;nnn;state;cycle.attempt;age;s` line per held DM, never the text |
| `--info`                        | contains `STORE mode=<m> used=<n>/<slots> time=<h>h notice=<on\|off>`                    |

Console lines a client may parse: `[STORE];mode;<m>;slots;<n>;time;<h>;notice;<on|off>`,
`[STORE];warning;...`, `[STORE];heap;<bytes>`, `[STORE];unavailable` (ineligible hardware),
`[STORE];notice;<on|off>`, `[STORE];list;<csv>`.

Setlog lines under `--setlog on` (every 5 minutes, next to `STAT`):

```
DM sent=.. echo=.. gwack=.. ack=.. giveup=.. giveuph=.. att=.. reack=../.. rtt=a/b/c/d/e/f ring=enq:.. ovw:..
MBOX mode=.. used=../.. act=../20 stored=.. refr=.. deliv=.. ack=.. dropt=.. dropc=.. drops=.. peer=.. blk=.. sto=../..
```

Web GUI: `/?page=mailbox` (owner password), setup card "Store node", and the `/setparam/` names
`store`, `storecall`, `storetime`, `storeslots`, `storenotice`.

**Not implemented, proposal for the apps:** a `STORE` field in the node-settings JSON frame
(`SN`) so the mobile app and MCProxy can show the role without a console round trip, e.g.
`"STORE":"heard","STON":1`. Say if you want it; it is a small firmware change once the app side
agrees on the names.

## 6. Test vectors

Frames as the node hands them to the transport (before the length byte and the 4 time bytes).
msg_id `0x12345678`.

```
failed, destination DK5EN-14:
41 78 56 34 12 03 08 44 4B 35 45 4E 2D 31 34
                    ^^ n=8  D  K  5  E  N  -  1  4

held, holder DK5EN-90:
41 78 56 34 12 04 08 44 4B 35 45 4E 2D 39 30

acked, old format (n = 0), still valid:
41 78 56 34 12 02 00
```

A sequence to test precedence: `held(DK5EN-90)` then `acked(DK5EN-14)` must end as acked;
`acked` then `held` must stay acked; `held` then `failed` never happens, `failed` then `acked`
ends as acked.

## 7. Compatibility summary

| Node firmware            | What the sender's client sees                                 |
| ------------------------ | ------------------------------------------------------------- |
| upstream 4.35t           | heard / gateway / acked as before; `:sto` texts as chat lines |
| fork with stage 0        | plus `failed`                                                 |
| fork with stages 3 and 4 | plus `held` with the holder; `failed` suppressed while held   |

A client that implements section 2 fully works against all three.
