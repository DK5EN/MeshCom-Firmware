# Fix: Node ACK (heard) frames to the phone for foreign msg_ids

Status 2026-09-05 19:30, firmware fork-main `fbadd2bb`, measured on DK5EN-98 / mcapp.local
running McApp v2.0.3-dev.1. Follow-up to `docs/ack-wer-hat-quittiert.md` and commit
`fbadd2bb` ("--ackinfo lifts the first-only gates for own msg_ids only").

Reviewed and corrected 2026-09-05 (relay claim, snippet/prose mismatch, peer-site inventory);
fix implemented in the same commit as this revision.

## 1. Summary

`fbadd2bb` fixed the repeats but not the first frame. The node still sends **one** Node ACK
(status 0x00, "heard") to the phone for every foreign message it pulled from the server and
forwarded to LoRa, the moment a neighbour relays that message. This is a gateway-only bug: it fires for
foreign `own_msg_id[]` entries, and only a gateway's server-to-LoRa forwarding inserts those
(see Cause below); a plain node never emits these frames, which is why the evidence below comes
from DK5EN-98, a gateway. This is not new: it is the legacy `own_msg_id[..][4] == 0x00`
clause, which has fired for forwarded foreign frames for as long as `own_msg_id[]` has held
them. McApp has been recording `send_success = 1` on roughly 150 foreign message rows per day
since before 2026-08-26 because of it. With attribution the frame now also carries a callsign,
so every one of those lands as a row in McApp's `message_acks` ledger.

The fix is one condition in two places: a status frame to the phone must require an
**own-originated** msg_id, on the first frame as well as on the repeats.

## 2. Evidence

Ledger rows on mcapp.local after the `fbadd2bb` flash (node reconnected 19:09, commit 19:14),
foreign originators only, one row per foreign message, never two:

```
19:14:31 134EF391 DK8VW-99  heard by DL2JA-2
19:15:29 687B423B DF4ND-99  heard by DK5EN-14
19:16:20 687B423C DF4ND-99  heard by DB0ED-99
19:16:30 134EF393 DK8VW-99  heard by DF2SI-12
19:17:51 687B423D DF4ND-99  heard by DK5EN-14
19:18:18 134EF394 DK8VW-99  heard by DF2SI-12
... 19 foreign messages in 14 minutes, each exactly one heard frame
```

Before the fix the same query showed up to 4 distinct relays per foreign message. So the
repeat gate now holds; the first-frame gate does not exist.

Legacy proof, `messages` rows with a foreign `src` and `send_success = 1` per day:

```
2026-08-26   37
2026-08-29  134
2026-09-01  176
2026-09-04  150
2026-09-05  158
```

`send_success` is set by McApp only from a 0x41 status frame to the phone. A foreign row can
only get it if the node sent a status frame for a foreign msg_id. That has been happening on
every firmware in this window, so the official phone app has been receiving these frames all
along and evidently ignores them (it has no bubble for a msg_id it never sent).

## 3. Cause

`own_msg_id[]` is not only populated with this node's own originated msg_ids. The LoRa relay
path itself (rx_relay enqueue, `src/lora_functions.cpp:1472`) never calls `insertOwnTx()`, so a
plain relay or uplink over LoRa does not insert a foreign entry — the comment previously here,
and the matching comment on `ackMsgIdFromNode` in `src/ack_attribution.h`, both claimed
otherwise and are corrected in the same commit as this fix. What does insert a foreign msg_id is
a gateway's server-to-LoRa forwarding: `src/udp_functions.cpp:476` (ESP32) and
`src/nrf52/nrf_eth.cpp:655` (RAK Ethernet). That forwarding path is gateway-only, which is why
the bug only reproduces on a gateway such as DK5EN-98. Both status-frame emit sites test
membership in that table and then gate only the _repeat_ on origin:

`src/lora_functions.cpp:369` (Gateway ACK to phone):

```cpp
if((bAckInfo && ackMsgIdFromNode(msg_id, _GW_ID)) || own_msg_id[itxcheck][4] < 2)
```

`src/lora_functions.cpp:844` (Node ACK / heard to phone):

```cpp
if(msg_type_b_lora == MSG_TYPE_TEXT
   && ((bAckInfo && ackMsgIdFromNode(aprsmsg.msg_id, _GW_ID)) || own_msg_id[icheck][4] == 0x00))
```

The right-hand side of the `||` is the legacy path. For a forwarded foreign frame with state
`0x00` it is true once, so the first heard (and the first gateway ACK) still goes to the phone
with the foreign msg_id. `bAckInfo` plays no role in that branch.

## 4. Fix

Operator decision 2026-09-05: gate only the BLE emit to the phone on origin. The outer
condition and the state-machine writes below it are untouched — they stay exactly as they were,
unconditional on origin. An earlier draft of this fix proposed moving the origin check to gate
the whole expression, which would also have frozen the state write for foreign msg_ids at
`0x00` forever and made the web rxlog (`src/web_functions/web_functions.cpp:1840`) lose its
heard/ACK ticks for server-forwarded messages on gateways. That draft is superseded by the
shape actually implemented:

`src/lora_functions.cpp` gateway ACK site:

```cpp
if(bAckInfo || own_msg_id[itxcheck][4] < 2)
{
    if(ackMsgIdFromNode(msg_id, _GW_ID))
    {
        // build + addBLEOutBuffer, unchanged
    }
    own_msg_id[itxcheck][4] = 0x02;
}
```

`src/lora_functions.cpp` heard site:

```cpp
if(msg_type_b_lora == MSG_TYPE_TEXT && (bAckInfo || own_msg_id[icheck][4] == 0x00))
{
    if(ackMsgIdFromNode(aprsmsg.msg_id, _GW_ID))
    {
        // build + addBLEOutBuffer, unchanged
    }
    if(own_msg_id[icheck][4] != 0x02)
        own_msg_id[icheck][4] = 0x01;
}
```

The state machine is untouched: `own_msg_id[..][4]` still advances to `0x01`/`0x02` for
relayed and forwarded foreign frames exactly as before, so the web rxlog's heard/ACK ticks
survive, and the retransmit-stop and dedup logic keeps reading the same state. Only the
`ackMsgIdFromNode` check moved inward, so it now gates solely whether the BLE frame is built
and sent to the phone. For own msg_ids the observable behaviour is unchanged in every
`--ackinfo` combination: the first frame goes out with `--ackinfo` off, every repeat goes out
with `--ackinfo` on. The previous formulation, `(bAckInfo && ackMsgIdFromNode(...)) ||
own_msg_id[..][4] < threshold`, was also the only place in this code where `bAckInfo` and
origin were coupled together; with this fix the two conditions are independent — `bAckInfo`
governs first-frame-vs-repeat, `ackMsgIdFromNode` governs whether the phone hears about it at
all.

Do not touch the Peer ACK sites (`src/lora_functions.cpp:1013`, `src/udp_functions.cpp:412`,
`src/nrf52/nrf_eth.cpp:576`). A text `:ackNNN` is matched against `own_msg_id` by the 3-digit
counter, and that counter is only ever minted for own messages, so those are already
origin-bound.

Separate observation, not part of this fix: both UDP peer ACK sites
(`src/udp_functions.cpp:412`, `src/nrf52/nrf_eth.cpp:576`) push the frame to the phone even when
`checkOwnTx()` fails, with status `0x01` instead of `0x02`. Since the msg_id there is built from
this node's own hash and the 3-digit counter, the only way McApp could mismatch it is a counter
collision.

## 5. Why origin, not "did the phone send it"

Messages injected over extUDP and messages typed on the node's own web UI get a msg_id from
this node, so they pass `ackMsgIdFromNode` and keep their status frames. Frames a gateway
pulled from the server for LoRa carry another node's hash and are exactly the ones to suppress
(frames the node only relayed over LoRa never enter `own_msg_id[]` in the first place). The hash test is the same one `fbadd2bb` already introduced;
this change only extends it to the legacy branch.

## 6. Tests

Operator decision 2026-09-05: no new automated tests for this change; verification is manual
via McApp.

Native, pure-function level, no globals: both assertions below already exist in
`test/test_ack_phone_frame/test_ack_phone_frame.cpp:177-195`, including the real foreign id
`0x134EF38A`:

- `ackMsgIdFromNode(((gw & 0x3FFFFF) << 10) | 7, gw)` is true.
- `ackMsgIdFromNode(0x134EF38A, gw_of_DK5EN_98)` is false (a real foreign id from the bench).

Bench, with McApp as the instrument, both on DK5EN-98:

1. Send a group message from McApp. Expect: gateway ACK, then one heard row per relaying
   neighbour, all under the own msg_id. This is the 19:09 to 19:13 `acktest heard` pattern and
   must not regress.
2. Wait for any foreign group-20 message and its relay. Expect: **no** ledger row for it. This
   only reproduces on a gateway (see Cause) — a plain node has no server-forwarded foreign
   msg_ids to test against.
   Query on the Pi (no sqlite3 CLI there; python3, timestamps in ms):

```python
import sqlite3
c = sqlite3.connect("file:/var/lib/mcapp/messages.db?mode=ro", uri=True)
print(c.execute("""SELECT COUNT(*) FROM message_acks a JOIN messages m
  ON m.msg_id = a.msg_id AND m.type = 'msg'
  WHERE m.src NOT LIKE 'DK5EN-98%' AND a.timestamp > ?""",
  (int(__import__('time').time() * 1000) - 30 * 60 * 1000,)).fetchone())
```

Zero after 30 minutes of normal group traffic is the pass condition; before the fix this
is about 20.

3. `--ackinfo off` (or reconnect without McApp): the legacy single frame per own message must
   still arrive. The change must not make the phone app lose its one status frame.

## 7. McApp side

McApp will add a guard of its own so a node that reports foreign msg_ids cannot set
`send_success` or write ledger rows for messages this box did not send, and will scrub the
existing foreign rows. That is defence in depth, not a substitute: without the firmware fix the
node keeps spending BLE frames on status nobody can display.
