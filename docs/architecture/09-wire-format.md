# MeshCom Wire Format — LoRa Frames, Server UDP, EXTUDP JSON, BLE Phone Protocol

Status: **descriptive reference**, written 2026-08-21 against firmware 4.35p
(branch `v4.35p_prio`). This document describes what the code actually does,
with `file:line` anchors into this repository and byte-level examples taken
from frames captured on the air (433.175 MHz, `MC_TEST_HOOKS` capture hook in
`OnRxDone()`, see doc 08 §4). It is not an upstream-normative specification;
where the wire format is ambiguous, the decoder in `src/aprs_functions.cpp`
is the authority this document follows.

Purpose: precise enough to implement **mock services and test doubles** for
`mc-chat`, `MCProxy` and `mcmap` — a fake node, a fake gateway, a fake server,
and a fake BLE peripheral. Machine-readable companion vectors live in
`test/test_aprs_corpus/corpus.txt` (raw frames) and `golden.txt` (decoded
fields), regression-fenced by `pio test -e native_aprs`.

Layers covered:

1. [LoRa on-air frame](#1-lora-on-air-frame) — what nodes exchange over radio
2. [Server UDP protocol](#2-server-udp-protocol-port-1990) — gateway ↔ MeshCom server (OE/DL)
3. [EXTUDP JSON sideband](#3-extudp-json-sideband-port-1799) — node ↔ local consumer (MCProxy et al.)
4. [BLE phone protocol](#4-ble-phone-protocol) — node ↔ app (Nordic UART service)

---

## 1. LoRa on-air frame

Decoder: `decodeAPRS()` (`src/aprs_functions.cpp:122`); encoder:
`encodeStartAPRS()`/`encodePayloadAPRS()`/`encodeAPRS()`
(`src/aprs_functions.cpp:1012–1122`). Maximum frame size is 255 bytes
(`UDP_TX_BUF_SIZE`); frames shorter than 16 bytes are rejected.

### 1.1 Layout

```
offset  size  field
0       1     payload type: 0x3A ':' text | 0x21 '!' position | 0x40 '@' hey/weather
1       4     msg_id, unsigned 32-bit LITTLE-ENDIAN
5       1     flags + hop nibble (see 1.2)
6       var   source path, printable ASCII, terminated by '>'   (max 120 bytes)
..      var   destination, printable ASCII, terminated by a REPEAT of the type byte
..      var   payload, ASCII, terminated by 0x00
..      1     HW id of the ORIGINATING node (hardware table, e.g. 9=RAK4631, 43=Heltec V3)
..      1     MOD byte: (modulation & 0x0F) | (country_code << 4)
..      2     FCS, BIG-ENDIAN 16-bit (see 1.3)
..      1     FW version of originator (decimal, e.g. 35 = 4.35; values 1..34 are rejected)
..      1     LASTHW: bit7 = "last sender is gateway/… flag", bits 0..6 = HW id of the LAST hop
..      1     FW sub-version, ASCII char (e.g. 'p'); 0x00 and 0x7E are read back as '#'
..      1     0x7E end marker
```

The trailer fields after the FCS (FW, LASTHW, FW-sub, 0x7E) are parsed
"if present" by the decoder (`aprs_functions.cpp:446–481`) — robust decoders
must tolerate their absence, encoders must always write all of them
(`encodeAPRS()` does).

### 1.2 Byte 5 — flags + hop

`src/aprs_functions.cpp:160–172` (decode), `:1025–1037` (encode):

```
bit 7  0x80  msg_server   frame has passed a gateway/server
bit 6  0x40  msg_track    tracking flag
bit 5  0x20  msg_app_offline  set by a gateway when re-emitting a server frame
bit 4  0x10  msg_mesh     mesh (relay) enabled — encoder sets it from bMESH
bits 0-3     max_hop      remaining hop budget (decrements on relay)
```

Defaults: text messages start with `max_hop` 4, positions with 2
(`MAX_HOP_TEXT_DEFAULT`/`MAX_HOP_POS_DEFAULT`, `src/configuration_global.h:162`).

### 1.3 FCS

Plain 16-bit **byte sum** (not a CRC): sum of all frame bytes from offset 0
up to and **including** the HW and MOD bytes, stored big-endian
(`aprs_functions.cpp:417–428` decode, `:1086–1097` encode). Frames failing
the FCS check are discarded silently unless they originate from the node
itself.

### 1.4 Path and destination

- Source path: comma-separated callsign list, **originator first**; each relay
  appends its own callsign. `msg_source_call` = first element,
  `msg_source_last` = last element. Both must pass the callsign regex
  (`checkRegexCall`, `src/regex_functions.cpp`); frames failing it are dropped.
- Destination: `*` = broadcast; a numeric group (`9999`, `20`, …); or a
  callsign for a direct message (DM). Non-group destinations must pass the
  callsign regex too.
- HEY beacons are `0x40` frames with destination `H` and payload like `R0;`
  (trickle neighbor discovery); weather frames share type `0x40` with other
  destinations.

### 1.5 Acknowledgements

- A DM requests an ack by appending `{NNN` to its payload, where `NNN` is the
  sender's 3-digit message counter (`sendMessage()`,
  `src/loop_functions.cpp:3445–3450`).
- The addressed node answers with a normal **text frame** (`0x3A`) whose
  payload is `<destcall> :ackNNN` (or `:rejNNN`), e.g.
  `DK5EN-98,DK5EN-91>DK5EN-90:DK5EN-90 :ack063`.
- Additionally, gateways emit a **compact 12-byte binary ack** for
  broadcast/group/WLNK/SOTA messages (`src/lora_functions.cpp:1078–1095`,
  captured on air as corpus frames `f008`/`f009`):

  ```
  [0]     0x41  MSG_TYPE_ACK
  [1..4]  this ack frame's own msg_id, LE (derived from millis())
  [5]     0x80 (server flag) | max_hop nibble
  [6..9]  the ACKED message's msg_id, LE
  [10]    ack level: 0x00 node, 0x01 gateway
  [11]    0x00 terminator
  ```

  Note: no path, no FCS, no trailer. `decodeAPRS()` classifies these by
  returning `0x41` without filling any fields (`aprs_functions.cpp:130`);
  the actual processing happens in the RX path of `lora_functions.cpp`.

- `0x3C` (`<`, LoRa-APRS) frames are passed through undecoded.

### 1.6 Annotated example (captured on air)

Corpus frame `f001` — position beacon, DL2JA-1 relayed by DL2JA-2, received
at RSSI −109 dBm:

```
21                                       type '!' position
AB 13 F1 E9                              msg_id = 0xE9F113AB (LE)
91                                       flags: 0x80 server | 0x10 mesh; hop = 1
44 4C 32 4A 41 2D 31 2C                  "DL2JA-1,"
44 4C 32 4A 41 2D 32 3E                  "DL2JA-2>"
2A                                       "*" destination (broadcast)
21                                       '!' destination terminator (= type byte)
34 38 32 35 2E 33 35 4E 5C ...           payload "4825.35N\01147.19E-Marzling#Werner/R=9;"
00                                       payload terminator
2B                                       HW id 0x2B = 43 (Heltec V3)
88                                       MOD: mod 8, country 8 (EU8)
13 2F                                    FCS = 0x132F (big-endian byte sum)
23                                       FW version 0x23 = 35 (4.35)
AB                                       LASTHW: 0x80 flag | hw 0x2B
70                                       FW sub 'p'
7E                                       end marker
```

---

## 2. Server UDP protocol (port 1990)

Gateway nodes exchange UDP datagrams with the MeshCom server
(`meshcom.oevsv.at` / OE and DL hamnet servers). Port: `UDP_PORT 1990`
(`src/configuration_global.h:86`). Implementations: `src/udp_functions.cpp`
(ESP32/WiFi) and `src/nrf52/nrf_eth.cpp` (nRF52/W5100S Ethernet) — the two
are a documented DRY-21 code clone.

### 2.1 Node → server

**KEEP** (heartbeat, every `HEARTBEAT_INTERVAL` = 30 s; `sendKEEP()`,
`src/udp_functions.cpp:1113`): ASCII, NUL-terminated —

```
"KEEP" + %08X gateway_id + %-9.9s callsign + %-4.4s version + %-1.1s sub + <grc_ids> + 0x00
```

`gateway_id` is derived from the MAC (`macaddr[5..2]`), `grc_ids` is the
node's group list as `NNN;NNN;…`. Example from the bench:
`KEEP48A4690DDK5EN-90 4.35p20;232;262;9;26244;26244;`.

**DATA** (LoRa frame forwarded to the server; `addNodeData()`,
`src/udp_functions.cpp:1154`): a fixed **36-byte ASCII header** followed by
the raw LoRa frame of §1 —

```
"DATA" + %08X gateway_id + %-9.9s callsign + %-4.4s version + %-1.1s sub
       + %4i rssi + %4i snr + "03"          (4+8+9+4+1+4+4+2 = 36 bytes)
<raw LoRa frame bytes>
```

The node also sends its **own** transmissions to the server through the same
envelope (with rssi/snr 0).

### 2.2 Server → node

The first 4 bytes of each datagram are an indicator
(`UDP_MSG_INDICATOR_LEN 4`; parsing: `getUDP()`, `src/nrf52/nrf_eth.cpp:212` /
`src/udp_functions.cpp` equivalent):

- **`GATE`** + raw LoRa frame — a frame the gateway shall transmit on LoRa.
  The node decodes it (§1), appends itself to the source path, sets
  `msg_app_offline` (0x20) and re-encodes before transmitting; acks embedded
  in such frames (`:ackNNN`) are forwarded to the BLE app with ack level
  `0x02` when they confirm the node's own message.
- **`CONF`** + config TLV sequence (`nrf_eth.cpp:505–587`):

  ```
  0x00 <len> <callsign bytes>        assigned callsign ("longname")
  0x01 <len> <shortname bytes>
  0x02 <int32 LE>                    latitude
  0x03 <int32 LE>                    longitude
  0x04 <int32 LE>                    altitude
  ```

- **`BEAT`** — server heartbeat response; the node only refreshes its
  link-alive timestamp (`last_upd_timer`). If no server traffic arrives for
  `MAX_HB_RX_TIME` = 65 s, the gateway re-runs DHCP/reconnect.

Datagrams with more than `MAX_ZEROS` = 6 consecutive zero bytes are
discarded as corrupt.

---

## 3. EXTUDP JSON sideband (port 1799)

A local JSON-over-UDP feed for consumers on the LAN (MCProxy's UDP leg,
telemetry sinks). Port `EXTERN_PORT 1799`; implementation
`src/extudp_functions.cpp`. Enabled with `--extudp on`, peer set with
`--extudpip <ip>`.

**Node → peer** (`sendExtern()`, `:321`): one JSON object per datagram, no
terminator. Message example (captured):

```json
{
  "src_type": "node",
  "type": "msg",
  "src": "DK5EN-90",
  "dst": "9999",
  "msg": "…",
  "msg_id": "91A436A5",
  "firmware": "4.35",
  "fw_sub": "p",
  "rssi": 0,
  "snr": 0
}
```

`src_type` is `node` (own traffic), `lora` (received frames) or `udp`
(server-gated frames). Position frames additionally produce a
`"type":"tele"` datagram with sensor fields. Received LoRa frames are
forwarded with their real `rssi`/`snr`.

**Peer → node** (`getExtern()`, `:218`): JSON commands —

```json
{"type":"msg","dst":"*","msg":"text"}          send a message (dst ≤ 9 chars, msg ≤ 150)
{"type":"tele","temp":23.3,"hum":60,...}       inject telemetry (only if the node has
                                               no real sensor hardware; triggers a beacon)
```

---

## 4. BLE phone protocol

GATT: Nordic UART Service, `SERVICE_UUID 6E400001-B5A3-F393-E0A9-E50E24DCCA9E`,
phone→node writes on `…0002`, node→phone notifications on `…0003`
(`src/esp32/esp32_main.cpp:1644`; nRF52 uses the Adafruit BLE UART service
with the same layout). Device name: `MC-<id>-<CALLSIGN>`. An independent,
field-tested implementation of this layer exists in
`MCProxy/src/mcapp/ble_protocol.py` and was used to cross-check this section.

### 4.1 Node → phone notifications

Framing (`sendToPhone()`, `src/phone_commands.cpp:95–130`): the first ring
byte is a status/type byte, then:

- **Data frames (text/position)**: prefix byte `0x40` (`'@'`) + the **raw
  LoRa frame of §1** + a 4-byte little-endian unix timestamp appended by the
  firmware. So a notification starts `40 3A …` (text) or `40 21 …`
  (position). MCProxy footer view (little-endian):
  `zero, hw, mod, fcs(H), fw, lasthw, fw_sub, 0x7E, time_ms(I)`.
- **ACK frames**: `40 41 <orig_msg_id ×4 LE> <ack_level> 00 <time ×4>`.
  `ack_level`: `0x00` node-level ack, `0x01` heard/gateway ack, `0x02`
  "own message confirmed" (server/gateway confirmed a message this node
  originated — see `src/udp_functions.cpp:277–284`).
- **`0x44`**: JSON data message to the app (passed through unprefixed).
- **`0x91`**: MHeard list records (unprefixed, binary).

### 4.2 Phone → node writes

Command frames are `[len][type][payload…]` (`phone_commands.cpp:255–300`):

```
0x10  hello/config request        0x20  timestamp from phone
0x50  callsign  (1B len + chars)  0x55  WiFi: 1B len SSID + 1B len PWD
0x70  latitude  (4B float)        0x80  longitude (4B float)
0x90  altitude  (4B int)          0x95  APRS symbols
0xA0  text message                0xF0  save settings to flash
```

Position frames (`0x70/0x80/0x90`) carry a save flag at offset 6
(`0x0A` = persist, `0x0B` = don't — periodic app positions use the latter).
Plain ASCII `--…` command strings are also accepted over BLE and dispatched
through the same `commandAction()` as the serial console; text messages
(`::…`/`{call}…` syntax) go through `sendMessage()`.

Settings written over BLE are staged and applied from the main loop
(`applyPendingBleSettings()`, CONC-17) — a mock phone must not assume the
settings notification arrives synchronously with the write.

### 4.3 Coverage note

The hello/config handshake (`0x10` response sequence: node sends its settings
blob and group list to the app) and the `0x44` JSON schemas are only
partially described here; `MCProxy/src/mcapp/ble_protocol.py` and
`src/phone_commands.cpp:255ff` are the sources to consult when mocking those
flows in depth.

---

## 5. Reference vectors

- `test/test_aprs_corpus/corpus.txt` — captured on-air frames (hex)
- `test/test_aprs_corpus/golden.txt` — frozen `decodeAPRS()` field output
- `test/test_aprs_decode/test_aprs_decode.cpp` — hand-verified interop
  vectors (expected values read from raw bytes, not from the decoder)
- Regenerating golden after a deliberate decoder change:
  `APRS_GOLDEN_UPDATE=1 pio test -e native_aprs` — review the git diff of
  `golden.txt`; it _is_ the behavior change.

Provenance: frames captured 2026-08-21 on 433.175 MHz (EU8 preset,
BW 250 kHz, SF 11, CR 4/6) with the `MC_TEST_HOOKS` hook; encoders observed
on the air include this firmware (RAK4631, Heltec V3), other MeshCom 4.35
devices (T-Beam), and gateway-emitted server frames.
