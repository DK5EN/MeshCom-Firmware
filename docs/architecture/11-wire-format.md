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

**msg_id composition.** The 32-bit msg_id is not opaque: firmware nodes build
it as `((gw_id & 0x3FFFFF) << 10) | (counter & 0x3FF)` — 22 bits of the
node's gateway id (MAC-derived), low 10 bits a per-node message counter
(`src/loop_functions.cpp:3119` and five sibling sites). The counter wraps at
**1000, not 1024**: every site that advances `node_msgid` clamps it to 999
(`loop_functions.cpp:3131–3133`), because the ack-request suffix `{NNN` is
rendered `%03i` from the same counter — 1000..1023 would have no 3-digit
representation and a peer's `:ackNNN` would match the wrong message. A mock
node must reproduce both the composition and the 0..999 wrap
(cross-validated against `mc-chat/meshcom_mock/protocol.py`, which documents
the same constraint independently).

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
- HEY beacons are `0x40` frames with destination `H` (`HG` from gateways) and
  payload like `R0;` (trickle neighbor discovery); weather frames share type
  `0x40` with other destinations. Each hop that handles a HEY appends a signal
  report `NCT,RSSI,SNR;` to the payload (`appendHeySignalReport`,
  `src/aprs_functions.cpp`) — mesh relays before re-transmitting, gateways
  before the UDP upload — e.g. `R0;5,118,7;` (5 neighbors, −118 dBm, +7 dB).

### 1.5 Acknowledgements

- A DM requests an ack by appending `{NNN` to its payload — **no closing
  brace** — where `NNN` is the sender's 3-digit message counter
  (`sendMessage()`, `src/loop_functions.cpp:3454`). One firmware path does
  emit a closing brace: `{pong}{NNN}` replies (`:3208`) — parsers should
  accept `{NNN` with an optional trailing `}`.
- The addressed node answers with a normal **text frame** (`0x3A`) whose
  payload is built `%-9.9s:ack%03i` (`src/loop_functions.cpp:4198`) — the
  destination callsign space-padded/truncated to **exactly 9 characters**,
  then `:ackNNN` (or `:rejNNN`), e.g.
  `DK5EN-98,DK5EN-91>DK5EN-90:DK5EN-90 :ack063` (the space is the padding).
  Parsers should match `:ack[0-9]+` anywhere in the payload rather than
  assume a separator.
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

### 1.7 Control payloads in text frames

Some `0x3A` text payloads are control traffic, not chat. The receiving node
consumes them in `loop_functions.cpp` and marks them no-retransmission
(`:3527`, ring status `0xFF`):

| Prefix                     | Meaning                                                                                                                                                                 |
| -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `{CET}YYYY-MM-DD HH:MM:SS` | Server time sync (`:2228`). Accepted only when the node has no RTC, no GPS fix and no valid NTP time; years ≤ 2023 are ignored.                                         |
| `{SET}N;M;`                | Server sets the node's hop budgets: `max_hop_text` and `max_hop_pos`, parsed `%d;%d;` (`:2219`).                                                                        |
| `{MCP}…` / `{mcp}…`        | Remote IO switching (`:2141`): payload carries a 3-digit sequence check derived from the frame's own `msg_id & 0x3FF`, a password checked against `node_passwd`, and `A | B<n> ON/OFF`mapped to`--setout`. |
| `{ping}` / `{pong}`        | Connectivity test with RSSI/SNR display echo (`:2115`).                                                                                                                 |
| `:ackNNN` / `:rejNNN`      | Text-level acknowledgement (§1.5).                                                                                                                                      |

---

## 2. Server UDP protocol (port 1990)

Gateway nodes exchange UDP datagrams with the MeshCom server
(`meshcom.oevsv.at` / OE and DL hamnet servers). Port: `UDP_PORT 1990`
(`src/configuration_global.h:86`). Implementations: `src/udp_functions.cpp`
(ESP32/WiFi) and `src/nrf52/nrf_eth.cpp` (nRF52/W5100S Ethernet). The
DRY-21 clone covers only the **server → node receive path** (`getUDP()`);
the encode side (`sendKEEP()`/`addNodeData()`) is compiled once for both
platforms (nRF52 calls into `udp_functions.cpp`, `nrf52_main.cpp:2963`).
The cloned receive paths are **not** feature-identical (see §2.2, CONF).

An upstream spec for this layer exists:
[icssw-org/MeshCom-Reflector](https://github.com/icssw-org/MeshCom-Reflector),
`protocolls/refelctor_connections.md` (typo in the upstream path). Treat it
as informational only — it contains known factual errors (DATA row labeled
`BEAT`; DATA byte 34 described as a 1-byte binary payload length where the
firmware actually writes two ASCII modulation digits; `LOGN` length given as
5 for a 4-char tag), catalogued with evidence in
`mc-chat/doc/proto-deviations.md`. Where spec, firmware and live traffic
disagree, the firmware wins. The spec's reflector side (`LOGN`/`CONN`,
port 6901, server ↔ reflector) is out of scope here — nodes never speak it.

This section is cross-validated against a second independent implementation:
`mc-chat/meshcom_mock/` (softnodes live-connected to the OE and DL servers;
`protocol.py` builds KEEP/DATA/positions, `decoder.py` parses everything the
servers send). Its empirical record: 800+ positions and 7000+ chat messages
decoded with the 36-byte DATA assumption, zero outer-framing failures.

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

The trailing two bytes are **ASCII modulation digits**, hardcoded to `"03"`
by the firmware (mode 3 = SF11/CR 4:6/BW 250 kHz; snprintf literal,
`udp_functions.cpp:1077–1079`) — not a binary payload-length byte as the
upstream spec claims. The LoRa frame always begins at offset 36; a decoder
that trusted the spec's length-byte reading would find `0x30` (`'0'`) where
the frame type belongs and decode nothing.

The node also sends its **own** transmissions to the server through the same
envelope (with rssi/snr 0).

### 2.2 Server → node

The first 4 bytes of each datagram are an indicator
(`UDP_MSG_INDICATOR_LEN 4`; parsing: `getUDP()`, `src/nrf52/nrf_eth.cpp:255`
and `src/udp_functions.cpp:144`):

- **`GATE`** + raw LoRa frame — a frame the gateway shall transmit on LoRa.
  The node decodes it (§1), appends itself to the source path, sets
  `msg_app_offline` (0x20) and re-encodes before transmitting; acks embedded
  in such frames (`:ackNNN`) are forwarded to the BLE app with ack level
  `0x02` when they confirm the node's own message. Server control payloads
  (§1.7: `{CET}`, `{SET}`, …) arrive as `GATE`-wrapped text frames.
- **`CONF`** + config TLV sequence — **nRF52 only** (`nrf_eth.cpp:497–587`;
  the ESP32 `getUDP()` mentions CONF in a comment but has no code branch for
  it — only GATE and BEAT are handled, `udp_functions.cpp:148–151,382`):

  ```
  0x00 <len> <callsign bytes>        assigned callsign ("longname")
  0x01 <len> <shortname bytes>
  0x02 <int32 LE>                    latitude
  0x03 <int32 LE>                    longitude
  0x04 <int32 LE>                    altitude
  ```

- **`BEAT`** — server heartbeat response; the node only checks the 4-byte
  indicator and refreshes its link-alive timestamp (`last_upd_timer`). If no
  server traffic arrives for `MAX_HB_RX_TIME` = 65 s, the gateway re-runs
  DHCP/reconnect. The datagram does carry structure beyond the indicator —
  observed on the OE/DL servers and parsed by mc-chat
  (`meshcom_mock/decoder.py:_decode_beat_struct`):

  ```
  "BEAT" + 0x00 + <call_len 1B> + <callsign>
         [+ 0x01 + <status_len 1B> + <status bytes>]     (optional)
  ```

  The firmware ignores everything after the indicator, but a mock **server**
  should emit the full form, and the server answers **every** KEEP with a
  BEAT — one datagram per 30 s heartbeat is the liveness signal mc-chat's
  softnodes build their connected-check on.

Datagrams whose **trailing** zero-run exceeds `MAX_ZEROS` = 6 bytes are
discarded as corrupt — with two precision caveats a mock must know
(`udp_functions.cpp:122–136`, clone `nrf_eth.cpp:210ff`): the counter scans
non-overlapping 2-byte-aligned pairs and **resets on any non-zero pair**, so
only a zero-run at the _end_ of the datagram (aligned to even offsets)
trips the check; a 7+ zero-byte run in the middle of an otherwise valid
datagram passes real firmware. The mock server
(`tools/mock/meshcom_server.py`) deliberately implements the stricter
any-run reading — stricter than firmware is safe for a test double.

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
(server-gated frames). Received LoRa frames are forwarded with their real
`rssi`/`snr`. Consumer-relevant edge rules (all `sendExtern()`):

- The position schema's longitude fields are **`long`/`long_dir`** —
  not `lon` (`:412–413`).
- Position frames additionally produce a `"type":"tele"` companion datagram
  with sensor fields — but only for `src_type` `node` and `lora`, never for
  server-gated (`udp`) frames (`:448,468`).
- Text frames addressed to group `100001` produce **no** msg datagram at all
  (`:495`).
- HEY/weather frames (`0x40`) are never forwarded — the type dispatch ends
  in `else return` (`:530`).

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

### 4.1 Hello handshake and app-layer PIN

After GATT connect the node stays silent — every notification path is gated
on `isPhoneReady` (`src/phone_commands.cpp:62,160`), which only a valid hello
sets. The phone opens with a **hello write** on `…0002`
(`readPhoneCommand()`, `phone_commands.cpp:307–362`):

```
open hello:  04 10 20 30                       (len, type 0x10, magic 0x20 0x30)
PIN hello:   24 10 20 30 <32-byte SHA-256>     (len 0x24 = 36; firmware accepts msg_len >= 35, phone_commands.cpp:321)
```

MCProxy's live implementation builds exactly these forms
(`build_hello_bytes()`, `MCProxy/ble_service/src/ble_adapter.py:335–345`;
the identical-looking constant in `mcapp/config_loader.py` is dead code —
cite the adapter). If the node has a BLE PIN
configured (`--btcode`, `meshcom_settings.bt_code` in 1..999999), the hello
must instead carry the SHA-256 hash of the PIN formatted as a zero-padded
6-digit decimal string (`hash_pin()`, `phone_commands.cpp:227`); a missing or
wrong hash makes the firmware drop the BLE connection
(`ble_disconnect_requested`). With `bt_code == 0` the open hello is accepted.

A valid hello sets `isPhoneReady = 1` and queues the **config burst**
(§4.2). The phone is expected to send its `0x20` timestamp write after the
hello (the node uses it to set its clock when it has no better source).

### 4.2 Post-hello config burst

On hello the main loop runs a fixed command list with BLE output enabled
(`config_cmds[]`, `src/nrf52/nrf52_main.cpp:268` /
`src/esp32/esp32_main.cpp:297`):

```
--info --seset --wifiset --nodeset --wx --pos --aprsset --io --tel [--analogset (ESP32 only)]
```

plus `sendMheard()` (one `MH` JSON per MHeard entry from the last 12 h).
Each command emits one or two `0x44` JSON notifications (§4.3). After a 3 s
settle and once both notification rings have drained, the node sends
`{"TYP":"CONFFIN"}` exactly once (`nrf52_main.cpp:1707–1748`,
`sendConfigFinish()`, `src/command_functions.cpp:5513`). The burst happens
once per genuine hello — a mock phone that reconnects without a new hello
gets no re-send, and a mock node must reproduce the burst-then-CONFFIN order
(MCProxy's cache hydration depends on it,
`MCProxy/src/mcapp/ble_hydration_tests.py`).

`0x44` JSON schemas (producers in `src/command_functions.cpp`; all objects
carry `"TYP"` as discriminator):

| TYP       | source command | fields                                                                                                                                                     |
| --------- | -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `I`       | `--info`       | FWVER, CALL, ID (gateway id), HWID, MAXV, BLE ("long"/"short"), BATP, BATV, GCB0…GCB5 (groups), CTRY, BOOST, BPIN                                          |
| `SE`+`S1` | `--seset`      | SE: BME, BMP, BMP3, BMP3F, AHT, AHTF, BMXF, 680, 680F, 811, 811F, SS, LPS33, OW, OWPIN, OWF, USERPIN · S1: INA226, SHUNT, IMAX, SAMP, SHT, SHTF, 226, 226F |
| `SW`+`S2` | `--wifiset`    | SW: SSID, IP, GW, AP, DNS, SUB · S2: OWNIP, OWNGW, OWNMS, OWNDNS, OWNNTP, EUDP, EUDPIP, TXPOW                                                              |
| `SN`      | `--nodeset`    | GW, WS, WSPWD, DISP, BTN, MSH, GPS, TRACK, UTCOF, TXP, MQRG, MSF, MCR, MBW, GWNPOS, NOALL, BLED, GWS, ASYM                                                 |
| `W`       | `--wx`         | TEMP, TOFFI, TOUT, TOFFO, HUM, PRES, QNH, ALT, GAS, CO2, VBUS, VSHUNT, VAMP, VPOW                                                                          |
| `G`       | `--pos`        | LAT, LON (signed decimal degrees), ALT, SAT, SFIX, HDOP, RATE, NEXT, DIST, DIRn, DIRo, DATE                                                                |
| `SA`      | `--aprsset`    | ATXT, SYMID, SYMCD, NAME                                                                                                                                   |
| `IO`      | `--io`         | MCP23017, AxOUT, AxVAL, BxOUT, BxVAL (bit strings)                                                                                                         |
| `TM`      | `--tel`        | PARM, UNIT, FORMAT, EQNS, VALES, PTIME                                                                                                                     |
| `AN`      | `--analogset`  | ESP32 only: APN, AFC, AK, AFL, ACK, ADC, ADCRAW, ADCE1, ADCE2, ADCSL, ADCOF, ADCAT                                                                         |
| `MH`      | (MHeard)       | CALL, DATE, TIME, PLT (payload type byte), HW, MOD, RSSI, SNR, DIST, PL, MESH, NCNT                                                                        |
| `CONFFIN` | `--conffin`    | no further fields                                                                                                                                          |

`MH` records are also pushed live as each frame updates the MHeard table
(`updateMheard()`, `src/mheard_functions.cpp:331`).

### 4.3 Node → phone notifications

Framing (`sendToPhone()`/`sendComToPhone()`, `src/phone_commands.cpp:50–220`):
the first ring byte is a status/type byte, then:

- **Data frames (text/position)**: prefix byte `0x40` (`'@'`) + the **raw
  LoRa frame of §1** + a 4-byte **BIG-endian** unix timestamp appended by
  the firmware (`addBLEOutBuffer()`, `src/loop_functions.cpp:563–568` —
  MSB first). So a notification starts `40 3A …` (text) or `40 21 …`
  (position) and ends `… 7E <time ×4 BE> <pad>`. A well-formed data
  notification has `0x7E` at offset −6 — a free integrity gate before
  trusting the trailer fields.
- **ACK frames**: `40 41 <orig_msg_id ×4 LE> <ack_level> 00 <time ×4 BE>
<pad>` — **13 bytes on the wire**. `ack_level`: `0x00` node-level ack,
  `0x01` heard/gateway ack, `0x02` "own message confirmed" — emitted both
  when a matched `:ack`/`:rej` arrives over RF
  (`src/lora_functions.cpp:868–896`) and via the server path
  (`src/udp_functions.cpp:277–284`).
- **`0x44`**: JSON message (§4.2 schemas), sent unprefixed: `44 {…}`.
- **Command replies**: responses to `--…` commands are **full §1 text
  frames** (`addBLECommandBack()`, `src/loop_functions.cpp:635–655`:
  source path `response`, destination `*`, encoded via `encodeAPRS()`) and
  travel the same `40 3A` data path — not bare ASCII.
- The senders still contain a `0x91` MHeard-binary branch, but no producer
  enqueues `0x91` records anymore — MHeard data travels as `MH` JSON.
  Treat `0x91` as legacy; a mock phone need not implement it.

**Trailing pad bytes — count depends on the branch.** Both senders transmit
`blelen + 2` bytes (`phone_commands.cpp:129,203`), but `blelen` means
different things: on the data/ACK path the ring length already includes the
4-byte timestamp and the `0x40` prefix is written into byte 0, so exactly
**one** pad byte follows the timestamp; on the `0x44` JSON path the payload
starts at byte 0, so **two** pad bytes follow the JSON. A mock node that
emits two pads on data frames shifts every trailer field by one.
(`sendComToPhone`'s non-JSON text branch frames differently again,
`phone_commands.cpp:188–193` — currently dead: every producer on that ring
is `0x44`.)

**Size ceilings** (a mock must respect all three): `0x44` register JSON is
producer-clamped at **245 bytes** (`addBLEComToOutBuffer`,
`src/loop_functions.cpp:607–611` — the binding limit, before any MTU);
data-path ring entries are clamped at `UDP_TX_BUF_SIZE − 4` = 251
(`:539`). ATT MTU is **not uniform across the bench**: nRF52 pins 250
(`Bluefruit.configPrphConn(250)`, `src/nrf52/nrf52_ble.cpp:91`), ESP32
NimBLE defaults to 255. The firmware never splits a notification across
writes — oversized content is truncated at the source, not fragmented.

**Which frames reach BLE**: from RF, only text (`0x3A`) and position
(`0x21`) frames are forwarded to the phone; HEY/weather (`0x40`) is not.
The server GATE path however forwards all three types
(`udp_functions.cpp:171`), so `40 40` notifications occur on gateway nodes
only.

### 4.4 Phone → node writes

Command frames are `[len][type][payload…]` (`phone_commands.cpp:248–300`):

```
0x10  hello (see §4.1)            0x20  timestamp from phone (4B LE unix)
0x50  callsign  (1B len + chars)  0x55  WiFi: 1B len SSID + 1B len PWD
0x70  latitude  (4B float)        0x80  longitude (4B float)
0x90  altitude  (4B int)          0x95  APRS symbols
0xA0  text message                0xF0  save settings to flash
```

Position frames (`0x70/0x80/0x90`) carry a save flag at offset 6
(`0x0A` = persist, `0x0B` = don't — periodic app positions use the latter).

**Text message syntax (0xA0)**: the phone sends `{dst}text` for a DM/group
message or bare `text` for broadcast — **without** any leading colon; the
firmware prepends the `:` itself when the payload does not start with `--`
(`phone_commands.cpp:583–588`). The `::…` double-colon form exists only on
the serial console. `--…` command strings are dispatched through the same
`commandAction()` as the serial console.

**Hard limits in `sendMessage()` a sender must pre-validate** (the firmware
enforces them silently — no error reaches the phone):

- Total `{dst}text` longer than **160 characters** → dropped
  (`src/loop_functions.cpp:3388`, debug-log only).
- The closing `}` must appear at index ≤ 10, i.e. **dst ≤ 9 characters**
  (`:3396`). A longer destination is not an error: the message goes out as
  a **broadcast** with the braces left in the payload — an intended DM
  becomes public.
- `0x95` accepts only symbol table ids `/` (0x2F) and `\` (0x5C)
  (`phone_commands.cpp:552`); anything else is silently ignored while the
  ASCII `--symid` path accepts more.

Settings written over BLE are staged and applied from the main loop
(`applyPendingBleSettings()`, CONC-17) — a mock phone must not assume the
settings notification arrives synchronously with the write.

---

## 5. Adjacent protocol: INTERLINK (not spoken by the firmware)

Listed here to prevent confusion, not as part of the firmware wire format.
**INTERLINK** is the icssw.org **server-to-server feed** on UDP port 1985 —
a consumer-facing alternative to running a BLE/EXTUDP connection to a
physical node. Framing:

```
register   client → server:  "DNCLOUD" + <code>, NUL-padded to 50 bytes, re-sent ~70 s
heartbeat  server → client:  "HBMASTER" (8 bytes)
data       server → client:  "DNCDATA" + <JSON string> + 0x00
bye        client → server:  "DNCBYE" + <code>, same 50-byte framing (ack: "HBGOODBYE")
```

The firmware has no code for any of this; nodes reach the servers only via
§2. Authoritative implementations: `mc-chat/meshcom_mock/interlink.py`
(origin) and `mcmap/proxy/src/interlink/` (port of it, with framing notes on
the server-side `DNCDATA-M`/`-D` mode suffix). mcmap consumes the mesh
exclusively through INTERLINK plus HTTP scrapers — it speaks none of the
four firmware layers directly, so its mocks need §1 (frame semantics inside
the JSON) but not §2–§4 framing.

---

## 6. Reference vectors

- `test/test_aprs_corpus/corpus.txt` — captured on-air frames (hex)
- `test/test_aprs_corpus/golden.txt` — frozen `decodeAPRS()` field output
- `test/test_aprs_decode/test_aprs_decode.cpp` — hand-verified interop
  vectors (expected values read from raw bytes, not from the decoder)
- `test/test_aprs_spec/test_aprs_spec.cpp` — vectors constructed from §1 of
  this document, independent of the encoder
- Regenerating golden after a deliberate decoder change:
  `APRS_GOLDEN_UPDATE=1 pio test -e native_aprs` — review the git diff of
  `golden.txt`; it _is_ the behavior change.

Provenance: frames captured 2026-08-21 on 433.175 MHz (EU8 preset,
BW 250 kHz, SF 11, CR 4/6) with the `MC_TEST_HOOKS` hook; encoders observed
on the air include this firmware (RAK4631, Heltec V3), other MeshCom 4.35
devices (T-Beam), and gateway-emitted server frames. Cross-validation:
§4 (BLE) against `MCProxy/src/mcapp/ble_protocol.py`; §2 (server UDP)
against `mc-chat/meshcom_mock/` (softnodes live on the OE and DL servers)
and the upstream MeshCom-Reflector spec, 2026-08-21.
