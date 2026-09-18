# Firmware handover: `hw_id` (and `lora_mod`, `max_hop`) on Extern-UDP text frames

Written 2026-09-16 for a coding agent working in
`/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main` (fork-main, last commit `2399e998` at
the time of writing). Nothing here is implemented yet. Downstream context: MCProxy
`doc/2026-09-16_0807-message-detail-popover-bugfix-report.md`, section "BUG-2, Cause B".

## 1. The user-visible symptom

In the McApp webapp's message detail popover, a text message that reached the proxy only over
Extern-UDP shows `Firmware` and `Signal` but **no `Hardware` row and no `Max hops` row**
(observed 2026-09-16 10:01, `DL1UDO-12` → group `26299`, `msg_id 920550A2`, received on LoRa
via `DO8RE-12,DO7GH-0,DB0HOB-12,DB0ED-99`). The stored row on mcapp.local:

| column     | value  |
| ---------- | ------ |
| `src_type` | `lora` |
| `hw_id`    | NULL   |
| `max_hop`  | NULL   |
| `firmware` | `35`   |
| `fw_sub`   | `t`    |
| `rssi`     | -121   |
| `snr`      | -10.0  |

The value IS known to the receiving node; it is just never serialized on this path.

## 2. Why the proxy cannot fill it from anywhere else

- One frame normally reaches the proxy twice: the Extern-UDP copy (firmware, fw_sub, rssi,
  snr) and the BLE GATT copy (hw_id, lora_mod, max_hop, mesh_info, ...). The proxy and the
  webapp merge both halves (fixed in McApp v2.0.8-dev.4/dev.5).
- The node only pushes a message over BLE when it would show it to the phone. On mcapp.local
  in the last 24 h: group `26299` had **13** UDP/LoRa copies and **0** BLE copies; group `9`
  had **0** BLE copies. Foreign DMs heard on RF: 324 rows in 7 days, 3 with a BLE half.
- So for every group the node is not subscribed to, and for every third-party DM heard on RF,
  the Extern-UDP text frame is the ONLY copy, and it carries no hardware id.
- Falling back to the sender's `station_positions.hw_id` was rejected on purpose: that row
  comes from position beacons and would present a different frame's hardware as this
  message's.

## 2b. Why there is no BLE copy even though the phone is connected

Not a BLE fault. A received group text is handed to the display and to BLE only if
`src/lora_functions.cpp:1185` accepts it:

```cpp
if((strcmp(destination_call, "*") == 0 && !bNoMSGtoALL) || CheckOwnGroup(destination_call))
```

`CheckOwnGroup` (`src/aprs_functions.cpp:53`) returns true when the group number is one of
the node's six `node_gcb[]` slots (`--setgrc`), or when NO slot is configured at all; as soon as
one slot is set, every other group is rejected. `*` is accepted unless `--nomsgall` is set.
Any other group (`26299` on mcapp.local) is still relayed on the mesh and forwarded to
Extern-UDP, but never pushed to the phone, so the BLE half with `hw_id`/`max_hop` does not
exist for it. The same is true for a DM between two third parties heard on RF. That is why the
UDP text frame has to carry the hardware id itself.

## 3. Root cause in the firmware

`src/extudp_functions.cpp`, the Extern-UDP JSON builder (`c_json[500]`, ArduinoJson,
`serializeJson(cJson, c_json, sizeof(c_json))`):

- **Position branch (`0x21`)**, around line 538-567: emits `src_type type src dst msg msg_id
lat lat_dir long long_dir aprs_symbol aprs_symbol_group hw_id alt ... firmware fw_sub rssi
snr`. `cJson["hw_id"] = aprsmsg.msg_source_hw;` is at **line 548**. This is the ONLY
  `cJson["hw_id"]` in the whole tree.
- **Text branch (`0x3A`)**, lines 640-663: emits `src_type type src dst msg msg_id firmware
fw_sub rssi snr` and nothing else. See the block starting `cJson["src_type"] = src_type;`
  at line 642 through `cJson["snr"] = snr;` at line 663.

The datum is available on the text path exactly as on the position path:

- `src/aprs_functions.cpp:112` — the originator sets `aprsmsg.msg_source_hw = BOARD_HARDWARE;`
  at message creation (line 114 sets `msg_source_mod`, line 99/101 set `max_hop`).
- `src/aprs_functions.cpp:421` — every received frame parses `msg_source_hw` back out of the
  epilogue (`:424` `msg_source_mod`, `:161` `max_hop`).

`msg_source_hw` therefore describes the **originator**, not the last hop — the same provenance
as `msg_source_fw_version`, which the text branch already sends as `firmware`.

## 4. The change

In the `0x3A` text branch of `src/extudp_functions.cpp`, next to the existing
`firmware`/`fw_sub` keys (lines 651-661), add:

```cpp
cJson["hw_id"]    = aprsmsg.msg_source_hw;          // originator board id, as on 0x21
cJson["lora_mod"] = aprsmsg.msg_source_mod & 0x0F;  // modulation nibble only, see note
cJson["max_hop"]  = aprsmsg.max_hop;                // remaining hop budget of this copy
```

Notes:

- `msg_source_mod` is a packed byte: low nibble modulation (3..8), high nibble country index
  (`aprs_functions.cpp:114`). MCProxy masks the BLE copy with `& 0x0F`; either mask here or
  leave it raw and rely on the proxy's mask (`ble_protocol.py` masks only the BLE path — check
  `storage/ingest.py` before relying on it for UDP). Masking in the firmware is the safer
  choice, and consistent with the `0xF` "not from last hop" ambiguity documented in
  `MCProxy/doc/2026-08-28_1700-firmware-mod-nibble-handover.md`.
- Do NOT add `mesh_info` here: on the BLE path it is a flag byte the proxy decodes; the
  Extern-UDP consumer has no decoder for it and the popover derives `Path` flags from the BLE
  copy only. Keep the change to the three fields above.
- Keys and types must match the BLE names the proxy already stores: `hw_id` (int),
  `lora_mod` (int), `max_hop` (int). MCProxy reads all three straight out of the UDP dict in
  `store_message` (`src/mcapp/storage/ingest.py`, the field extraction right after
  `src_type`), so **no proxy change is needed** once they are on the wire.

Also apply the same keys in the `0x21` position branch for `lora_mod`/`max_hop` if you want
parity there (`hw_id` is already present at line 548).

## 5. Budget check (do this, do not assume)

- The text-branch buffer is `c_json[500]`; `serializeJson` is bounded by `sizeof(c_json)`
  (JSN-01), so an overflow TRUNCATES the JSON rather than corrupting memory — which the proxy
  then fails to parse and drops the whole frame. The three new keys add about 45 bytes
  (`,"hw_id":43,"lora_mod":8,"max_hop":4`).
- Worst case today: `src` with a 5-hop path (~55 chars), a 150-char payload with escapes,
  `dst` up to 9 chars, plus the fixed keys — measure with `measureJson(cJson)` for the longest
  legal message and confirm it stays under 500 with the new keys. If it does not, raise the
  buffer (not the `static` one at line 485 by accident — check which of the two `c_json`
  declarations at 482/485 is live on this path) rather than dropping keys.
- The BLE register limit (`BLE_JSON_PAYLOAD_MAX`, 244) does NOT apply here; this is the UDP
  datagram, not a GATT frame.

## 6. Verification

- Bench: `docs/bench-extudp-regression.md` describes the existing Extern-UDP regression bench.
  Add a case: a received text frame (not own `node` traffic) must serialize `hw_id`,
  `lora_mod`, `max_hop` with the originator's values; an own-node text frame must carry
  `BOARD_HARDWARE`. Capture one real datagram with the proxy's
  `scripts/`-style AF_PACKET sniffer on the Pi (no tcpdump there) and paste it into the bench
  doc.
- End to end: after flashing the node that feeds mcapp.local, wait for a group message the node
  is NOT subscribed to (e.g. `26299`), then open its popover: `Hardware` and `Max hops` must
  now be present with no BLE copy in `messages` (`src_type='lora'` only). The DB query in the
  MCProxy popover report's "Evidence base" section shows how to read the row.
- Parity: mc-chat's `meshcom_mock` emits the same Extern-UDP shape for the mock; add the three
  keys there too or its contract parity test will diverge from the real node.

## 7. Related

- `docs/ack-wer-hat-quittiert.md`, `docs/client-integration-store-forward.md` — the two most
  recent client-facing wire additions; follow their documentation pattern for this one.
- MCProxy CLAUDE.md, "Key Gotchas", the Extern-UDP wire format bullet (no protocol version
  field: consumers detect by key presence, so an older node simply keeps omitting the keys).
