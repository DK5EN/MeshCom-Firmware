# MeshCom Wire Format — LoRa Frames, Server UDP, EXTUDP JSON, BLE Phone Protocol

Status: **descriptive reference**, written 2026-08-21 against firmware 4.35p
(branch `v4.35p_prio`). This document describes what the code actually does,
with `file:line` anchors into this repository and byte-level examples taken
from frames captured on the air (433.175 MHz, raw-frame capture hook in
`OnRxDone()`, see doc 08 §4). It is not an upstream-normative specification;
where the wire format is ambiguous, the decoder in `src/aprs_functions.cpp`
is the authority this document follows.

Addendum: §1.8 (the `/X=` position-comment key space) was added 2026-09-11
against firmware `fork-main` `c6ac16bd` (v4.35t base); the rest of this
document is unchanged from the 4.35p baseline above.

Addendum 2026-10-05: §1.9 (RM1 command DM) and §2.3 (`:sto` and the store node / `STOR`
datagram) were added against fork-dev `1cab9347` (firmware 4.40a).

Purpose: precise enough to implement **mock services and test doubles** for
`mc-chat`, `MCProxy` and `mcmap` — a fake node, a fake gateway, a fake server,
and a fake BLE peripheral. Machine-readable companion vectors live in
`test/test_aprs_corpus/corpus.txt` (raw frames) and `golden.txt` (decoded
fields), regression-fenced by `pio test -e native_aprs`. A larger field corpus
harvested from production logs (`tools/logharvest.py`) lives in
`test/test_aprs_fuzz/` and `test/test_aprs_reencode/`, fenced by
`pio test -e native_aprs_fuzz`.

The re-encode vectors are worth singling out as evidence for §1.1 and §1.3:
across 2.422 distinct frames heard in 32,7 h from eleven different hardware ids
and two firmware generations, `encodeAPRS()` reproduces the sender's byte-sum
**every time**, and the frame length in all but one case. That one is a sender
that omits the `0x7E` end marker — which is why the trailer is described below
as "parsed if present".

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

**FW sub-version: two reserved values, one of them by decision, not by
accident (DR-27).** `0x00` and `0x7E` both read back as `'#'`, but they get
there by different routes. `0x00` is substituted by the encoder itself —
`encodeAPRS()` writes `0x23` ('#') whenever `msg_source_fw_sub_version` is
0x00 (`aprs_functions.cpp:1356`) — so the wire never carries a literal
`0x00` in this slot. `0x7E` is written **verbatim** by the same encoder: no
substitution branch catches it, so a node whose sub-version byte is
literally `0x7E` ('~') puts two consecutive `0x7E` bytes on the wire. The
decoder then reads the first of that pair back as `'#'`
(`aprs_functions.cpp:469–473`) and separately consumes the second as the
frame's own end marker — the value survives one hop, then silently becomes
`'#'` on every receiver, regardless of platform.

This is decided as **`0x7E` is an illegal, reserved sub-version value, and
the encoder is deliberately not being changed to escape it.** Why it is
safe to leave as-is: `SOURCE_VERSION_SUB` is a single, human-picked version
letter (currently `"t"`, `src/configuration_global.h:2`), incremented by a
person choosing an ASCII letter for a release — no shipped firmware has
ever used `'~'` and nothing about the value's role would lead anyone to
pick it. Why the encoder is not being changed instead: doing so would mean
every node on the network re-agreeing on how this byte is written — a
fleet-coordination window — purely to protect a value nobody picks. A
mock/test implementation should treat `0x7E` here as a value it must never
emit, and should not "fix" the encoder to escape it if it notices the
collision; `test/test_aprs_epilogue` pins the current (lossy) behaviour on
purpose, as evidence the value is reserved rather than as a bug report.

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
| `RM1 <ctr> <cmd> <tag>`    | Authenticated remote-management command DM to the exact own call (§1.9). Never shown, never stored.                                                                     |
| `:stoNNN`                  | Store-node custody notice (§2.3.1).                                                                                                                                     |

### 1.8 Position comment tail: the `/X=` key space

Encoder: `PositionToAPRS()` (`src/loop_functions.cpp:4200-4495`); decoder:
`decodeAPRSPOS()` (`src/aprs_functions.cpp:556-1087`). This section covers
only the comment/tag portion of a position payload — the lat/lon head is
already covered by §1.1/§1.6.

#### 1.8.1 Grammar

The on-air position payload (frame type byte `0x21` from §1.1, followed by
the string `PositionToAPRS()` returns) is:

```
! ddmm.mm <N|S> <symbol-table-char> dddmm.mm <E|W> <symbol-char> [atxt] [#name] [tail]
```

- `ddmm.mm`/`dddmm.mm`: `%07.2lf`/`%08.2lf` degrees-minutes
  (`loop_functions.cpp:4492`).
- `atxt`: free-text comment, optional, ≤ 25 bytes, UTF-8-safe truncated
  (`charset_utf8_safe_truncate`, `:4280`) and CHR-02-filtered — the bytes
  `{ } : ; , /` are stripped before truncation because each is structure
  elsewhere in the wire format (`src/charset_filter.h:71-89`; path
  `PositionToAPRS():4271-4284`).
- `#name`: optional, `#` + `node_name` (`:4288`), only when a name is
  configured.
- `tail`: the `/X=` key sequence, §1.8.2.

**App-relevant fact**: `tail` (if present) starts immediately after the
symbol char only when both `atxt` and `#name` are absent — with either one
present, a `split('/')`-style consumer must skip past it first, not assume a
fixed offset. See §1.8.5 for the consequence.

**Decode rule** (`decodeAPRSPOS()`, `aprs_functions.cpp:642-699`, field
`pos_name` next to `pos_atxt` in `aprsPosition`): the comment/name region is
everything from right after the symbol char up to the first `/X=`-style
token — `/` + an uppercase letter + `=`, or `/N` + a digit `1`-`9` — never a
bare space or `/`. Within that region, the text after the **last** `#`
becomes `pos_name` and everything before it becomes `pos_atxt`; no `#` at
all leaves `pos_name` empty and the whole region is the comment. `node_name`
can never contain `#` (`--setname`, `command_functions.cpp` strips it before
truncation), so the last-`#` split is unambiguous for names this firmware
writes — spaces in the name are fine.

#### 1.8.2 The 17 keys

One `snprintf` per key into its own small buffer, then concatenated in a
fixed order (§1.8.3) into a single tail string. "Branch" is which half of
`PositionToAPRS()` computes the value: the `bINA226ON` branch (`:4309-4333`),
the plain-sensor `else` branch (`:4336-4419`), or code outside both (`/R=`,
`/Y=`, `/D=` — computed unconditionally on their own gates).

| Key   | printf format                                                             | Unit / meaning                                                                                                                                                               | Emit condition                                                       | Branch       | Origin                                           |
| ----- | ------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- | ------------ | ------------------------------------------------ |
| `/A=` | `%06i` feet (`%05i` metres only if `bFuss=false`, no call site uses that) | altitude, `conv_fuss(alt)` when `bFuss` (always true today)                                                                                                                  | `alt > 0`                                                            | both         | upstream (`mcp23017-digital-field.md` §3)        |
| `/B=` | `%03d`                                                                    | battery percent, sent at 0 % too (empty ≠ "flat", it means "no battery")                                                                                                     | `battHardwarePresent()`                                              | both         | upstream                                         |
| `/P=` | `%.1f`                                                                    | station pressure (QFE), hPa                                                                                                                                                  | `press > 0`                                                          | sensor only  | upstream                                         |
| `/H=` | `%.1f`                                                                    | humidity, %                                                                                                                                                                  | `hum > 0`                                                            | sensor only  | upstream                                         |
| `/T=` | `%.1f`                                                                    | temperature, °C                                                                                                                                                              | `temp != 0`                                                          | sensor only  | upstream                                         |
| `/O=` | `%.1f`                                                                    | second/OneWire temperature, °C                                                                                                                                               | `temp2 != 0`                                                         | sensor only  | upstream                                         |
| `/F=` | `%i`                                                                      | **pressure altitude in metres — not a pressure** (`extern_tele_json.h:12-17`)                                                                                                | `qfe > 0`                                                            | sensor only  | upstream                                         |
| `/Q=` | `%.1f`                                                                    | QNH, hPa                                                                                                                                                                     | `qnh > 0 && !bMCU811ON && !bBME680ON`                                | sensor only  | upstream                                         |
| `/G=` | `%.1f`                                                                    | BME680 gas resistance                                                                                                                                                        | `gasres > 0 && bBME680ON`; also sets `/V=3`                          | sensor only  | upstream                                         |
| `/C=` | `%.0f`                                                                    | CO2, ppm                                                                                                                                                                     | `co2 > 0 && bMCU811ON`; also sets `/V=2`                             | sensor only  | upstream                                         |
| `/V=` | literal `2`, `3` or `5`                                                   | sensor-block version: `2`=MCU811/CO2, `3`=BME680/gas, `5`=INA226                                                                                                             | set by whichever of `/G=`/`/C=`/INA226 fired                         | both         | upstream (meaning undocumented elsewhere)        |
| `/N`  | `"/N%i"` — **no `=`**                                                     | on-air neighbour count NCNT (concept 4.8 of `docs/meshcom5-topologie/`: neighbours heard within 60 min that are HM or SYM and not vetoed), capped at `NBR_NCNT_AIR_MAX` = 99 | `nbrNcntAir(nbrMatrix, now_min) > 0` (`src/loop_functions.cpp:4652`) | sensor only  | upstream key, fork-redefined content (MeshCom 5) |
| `/R=` | `%i;` repeated, up to 6                                                   | configured group-call list                                                                                                                                                   | at least one non-zero `node_gcb[]` entry                             | outside both | upstream                                         |
| `/Y=` | literal `1`                                                               | telemetry-beacon flag                                                                                                                                                        | `bSsendTele`                                                         | outside both | upstream                                         |
| `/D=` | `%s`, 8 chars of `0`/`1`                                                  | MCP23017 port A input bits, GPA0 first                                                                                                                                       | `bMCP23017` (chip answered at boot)                                  | outside both | **fork-only**, commit `b179fdff`, item 207       |
| `/U=` | `%.2f`                                                                    | INA226 bus voltage, V                                                                                                                                                        | inside the INA226 branch                                             | INA226 only  | upstream                                         |
| `/I=` | `%.1f`                                                                    | INA226 current, A                                                                                                                                                            | inside the INA226 branch                                             | INA226 only  | upstream                                         |

Origin follows the letter table in `docs/mcp23017-digital-field.md` §3
("letters in use in the beacon sensor tail at the time of the decision"),
against which `D` was the only free, newly-assigned letter.

#### 1.8.3 Concatenation order, separator rule, and the 100-byte budget

There is no explicit separator between keys — **the leading `/` of each key
is the separator** — so a decoder must scan for `/X=` (or `/N` + digit)
tokens, never split on `/` as a delimiter (that would also split inside
`/R=232;2321;`, which itself has no trailing `/`).

Fixed emission order, the `strncat` chain at `loop_functions.cpp:4458-4478`:

```
B  A  N  P  H  T  O  F  Q  G  C  R  (4 dead buffers, never written)  V  U  I  D  Y
```

Four buffers (`csfpegel`, `csfpegel2`, `csftemp`, `csfbatt`,
`loop_functions.cpp:4259-4262,4470-4473`) are concatenated but never
populated by any code path — reserved slots, currently always empty.

The tail plus `atxt` plus `#name` share a 100-byte budget
(`strconcat[100]`, `:4455`). If the combined length exceeds 100 bytes, the
encoder drops fields in this order: **`atxt` first** (`:4481-4484`), then
**`#name`** (`:4486-4489`) — the `/X=` tail itself is never truncated or
reordered, only the free-text comment and the node name are sacrificed.

#### 1.8.4 Decoder tolerance

`decodeAPRSPOS()` reads **14 of the 17** keys — `/R=`, `/U=`, `/I=` have no
scan loop and are silently ignored on receive, even though the same
firmware emits them. Scan order (independent of the encoder's emission
order, since each key has its own forward scan over the whole payload):

```
B  A  P  H  T  O  F  Q  G  N  C  V  Y  D
```

Per-key notes:

- Each numeric key's value is read into a 25-byte buffer with a 6-or-7
  character cap (`ipt > 6` cuts the scan; `decode_text[25]` is never
  overrun) — `/A=`, `/B=`, `/P=`, `/H=`, `/T=`, `/O=`, `/F=`, `/Q=`, `/G=`,
  `/C=`, `/V=` all share this shape.
- `/N` is matched only as `/N` immediately followed by a digit `1`-`9`
  (`:921`) — a leading `0` (never emitted by the encoder, since it only
  writes `/N` when `incnt > 0`) would not match at all. The value itself is
  capped at 3 characters.
- `/D=` requires **exactly 8 characters**, each `0` or `1`
  (`:1028-1076`); 7, 9+, or any non-binary character leaves `aprspos.din`
  empty rather than accepting a partial value. 9+ data bytes set an
  overflow flag instead of being copied, so a long garbage token cannot
  overrun the 25-byte scratch buffer.
- `/Y=`'s scan loop (`:999-1022`) is not preceded by a `memset` of
  `decode_text` the way every other loop is — it can read a stale digit
  left over from the `/V=` scan directly above it if `/Y=` itself is absent
  or short. Not a §1.8 fix, just a decode caveat worth knowing before
  trusting `aprspos.telemetry`.
- All 14 decoded keys are found independent of their position in the
  string and independent of each other's presence — the 14 loops each
  scan the full payload from `istarttext` on.

#### 1.8.5 Consumer notes

- **Never `split('/')` the comment.** A free-text `atxt` may legitimately
  contain digits after a `/` that is not a key marker once CHR-02's filter
  is bypassed by an older firmware or a foreign encoder; anchor on the
  known key letters (§1.8.2), not on the separator byte.
- **`/A=` is 6-digit feet.** The 5-digit metre form (`%05i`, `bFuss=false`)
  exists in the encoder but no call site passes `bFuss=false` today — a
  consumer that branches on digit count to pick feet vs. metres is
  defending against a form that cannot currently occur on this firmware.
- **`/Q=` can be legitimately absent** even on a node with a barometric
  sensor: it is suppressed whenever `bMCU811ON` or `bBME680ON` is set
  (`:4387`), so "no `/Q=`" does not mean "no pressure sensor".
- **`/F=` is metres, not hPa** — despite sitting next to `/P=` (hPa) and
  `/Q=` (hPa) in the tail, treating it as a pressure produces a value off
  by roughly three orders of magnitude.

For the state of MCProxy's, the phone app's, and the firmware's own
decoder's handling of each key relative to this grammar — including which
keys each consumer drops, mis-types, or reads at the wrong offset — see
`docs/archive/aprs-parser-drift-20260911.md` §2 (per-key drift matrix) and §3
(frame/trailer drift).

### 1.9 RM1 command DM

Authenticated remote management (issue #1189). A normal DM (`0x3A` text frame) to the **exact own
call** whose text starts with `RM1 `. Decision record: `docs/adr-remote-hmac.md`. Reference
implementation and vector generator: `tools/remote_cmd.py`; firmware: `src/remote_cmd.{h,cpp}`,
`src/rm_runtime.cpp`, `src/rm_queue.h`, receive hook `rmTryQueue()` in `src/lora_functions.cpp`.

**Plaintext plus tag.** Command and reply are readable on the air. The tag authenticates, it does
not encrypt (amateur-radio law).

#### 1.9.1 Grammar

```
command  RM1 <ctr> <cmd>[ <args>] <tag>
reply    RM1 <ctr> ok <status> <rtag>
         RM1 <ctr> err <reason> <rtag>
```

- `ctr`: decimal 1..4294967295, no leading zeros, strictly greater than the node's persisted
  high-water mark. `0` is reserved for `sync`.
- `cmd` at most 15 characters, `args` at most 23, all printable ASCII, **lower case** (only the
  literal `RM1` is upper case), single spaces, no leading or trailing space.
- `tag` / `rtag`: 16 lower-case hex characters, the **last** token.
- `ok`/`err` as the third token identify a reply (`rmIsReply()`); no command is called `ok` or `err`.
  A reply carries a result text of at most 63 characters.
- A command that does not parse is dropped without a reply.

#### 1.9.2 Canonical strings and tag

```
K          = SHA-256(node_passwd with trailing spaces stripped)             32 bytes
canonical  = "RM1|"  + dst + "|" + src + "|" + ctr + "|" + cmd [+ " " + args]
tag        = first 8 bytes of HMAC-SHA256(K, canonical), 16 lower-case hex
reply canonical = "RM1R|" + dst + "|" + src + "|" + ctr + "|" + result      (result = "ok ..." / "err ...")
reply tag  = same construction over the reply canonical
```

`dst` is the managed node's full call as configured, `src` is the sender call from the frame
(both with SSID, upper case). In a reply `dst` and `src` keep their command roles (managed node,
sender), so the same pair of calls appears in the same order. `node_passwd` is at most 14 characters.

#### 1.9.3 Allowlist

| Command                                      | Arguments                     | Reply status text                                              |
| -------------------------------------------- | ----------------------------- | -------------------------------------------------------------- |
| `reboot`                                     | none                          | `rebooting` (the node restarts 8 s after the reply)            |
| `status`                                     | none                          | `v=<ver> up=<min> bat=<%> heap=<kB> gw=<0/1> mesh=<0/1>`       |
| `sendpos`, `sendtrack`                       | none                          | `sent`                                                         |
| `gps`, `track`, `display`, `gateway`, `mesh` | `on` or `off`                 | `<name>=<on/off>`                                              |
| `txpower`                                    | `<n>`, 0 to the board maximum | `txpower=<n>`                                                  |
| `setout`                                     | `<a0..a7\|b0..b7> <on\|off>`  | `<pin>=<on/off>`; `err not output` if the pin is not an output |
| `sync`                                       | none, ctr 0                   | `ctr=<hwm> v=<ver>`                                            |

Everything else is rejected, whatever the tag. Hard-blocked: `cleanflash`, `ota-update`, `dfu`,
`deepsleep`, `setcall`, `passwd`, `webpwd`, `btcode`, `setssid`, `setpwd`, `wifiset`, `updrepo`,
`updchan`, `autoupdate`, `rm`, `stor`, and any argument containing `--`, `;`, `{` or `%`. Error
reasons in a reply: `failed` (the setting did not take effect), `not output`, `storage` (the
high-water mark could not be persisted, the command was **not** executed).

#### 1.9.4 Examples

Test vectors from `tools/tests/remote_cmd_vectors.json` (generated by `tools/remote_cmd.py --gen-vectors`, reproduced by the C++ host test). Password `secret`, managed node `DK5EN-90`,
sender `DK5EN-1`:

| ctr | cmd + args     | canonical                                  | DM text                                |
| --- | -------------- | ------------------------------------------ | -------------------------------------- |
| 1   | `reboot`       | `RM1\|DK5EN-90\|DK5EN-1\|1\|reboot`        | `RM1 1 reboot 18f287b79c551022`        |
| 0   | `sync`         | `RM1\|DK5EN-90\|DK5EN-1\|0\|sync`          | `RM1 0 sync 4edaf48f44b63f31`          |
| 43  | `setout a2 on` | `RM1\|DK5EN-90\|DK5EN-1\|43\|setout a2 on` | `RM1 43 setout a2 on b9f9d30f5a4c89ed` |
| 44  | `txpower 2`    | `RM1\|DK5EN-90\|DK5EN-1\|44\|txpower 2`    | `RM1 44 txpower 2 8f627761e4db39ea`    |
| 46  | `display off`  | `RM1\|DK5EN-90\|DK5EN-1\|46\|display off`  | `RM1 46 display off 4d0dac9eaf89d168`  |

Key for `secret`: `2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b`. Reply
vectors (the first two for `DK5EN-90` answering `DK5EN-1` with `secret`; the third with the
password `secret` followed by trailing spaces, which are stripped before hashing; the last for
`DK5EN-92` answering `DK5EN-14` with the password `abcdefghijklmn`):

```
RM1 1 ok rebooting facf04f881d89cde
RM1 44 err range ade99d27f3001d3c
RM1 0 ok ctr=42 v=4.40a c639dacb86c267de
RM1 2 ok v=4.40a up=125 bat=87 heap=212 gw=0 mesh=1 6ba7ba274709c895
```

(The `err range` vector exercises the reply grammar; the firmware itself refuses an out-of-range
`txpower` silently as `blocked`.)

#### 1.9.5 Behaviour a mock or client must know

- **Silent on failure.** A bad tag, a command off the allowlist or a counter at or below the
  high-water mark gets no reply (no oracle). The node prints `[RM];reject;<verdict>`.
- **Lost-reply recovery.** The same `ctr` with the same valid tag within 10 min re-sends the cached
  reply and does not execute again; at most one cached reply per 10 s.
- **Rate limit.** One accepted command (or `sync`) per 10 s. A rate reject needs a valid tag and
  does not count toward the lockout.
- **Lockout.** Three counted rejects (`tag`, `replay`, `blocked`, in-check `format`) within 90 s lock
  RM1 for 5 min. Reachable without the key, accepted by design (ADR).
- **Counter.** The mark is persisted before execution. `sync` returns it; the client stores the
  counter per target.
- **Accepted only** over LoRa, as a DM to the exact own call, with `--remotemgmt on` and a non-empty
  `node_passwd`; otherwise `RM1 ` is ordinary text. `RM1 ` DMs are never taken into store-node
  custody (§2.3).
- The reply is a DM to `src` sent through `sendMessage()`; an operator's node shows it as a
  normal DM.

---

## 2. Server UDP protocol (port 1990)

Gateway nodes exchange UDP datagrams with the MeshCom server
(`meshcom.oevsv.at` / OE and DL hamnet servers). Port: `UDP_PORT 1990`
(`src/configuration_global.h:165`). Implementations: `src/udp_functions.cpp`
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
- **`CONF`** + config TLV sequence — **both platforms** (`nrf_eth.cpp:497–587`,
  `udp_functions.cpp:515–600`).

  > **Correction 2026-09-11.** This section previously said CONF was nRF52
  > only and that the ESP32 `getUDP()` "mentions CONF in a comment but has no
  > code branch for it". That was true of the 4.35p baseline this document was
  > written against; TM-39 added the ESP32 branch, which parses the frame,
  > checks the source against the configured gateway server, bounds the size,
  > validates the callsign against `checkRegexCall()` and then **applies**
  > callsign and shortname. Found by replaying the corpus CONF at Heltec-93
  > during the G0 capture: the node logged
  > `[CONF] Call:DK5EN-1 Short:BNCH set from server` and was renamed until it
  > was restored. lat/lon/alt are parsed and deliberately not applied on the
  > ESP32 side.
  >
  > **A CONF frame provisions the node.** Anything replaying one at live
  > hardware must restore the configuration afterwards.

  ```
  0x00 <len> <callsign bytes>        assigned callsign ("longname")
  0x01 <len> <shortname bytes>
  0x02 <int32 LE>                    latitude
  0x03 <int32 LE>                    longitude
  0x04 <int32 LE>                    altitude
  ```

  **The TLV order is a hard wire-contract, not a convention** — the firmware
  parser is offset arithmetic, not a general TLV scanner
  (`nrf_eth.cpp:532–581`): the callsign TLV (`0x00`) must come **first** or
  the entire datagram is discarded; the shortname TLV must immediately
  follow it, and it is **effectively mandatory whenever coordinates are
  sent** — the coordinate offset is computed as
  `2 + call_len + short_len + 2` (`:557`), which adds the two shortname
  header bytes even when the TLV is absent, so omitting it misaligns the
  `0x02/0x03/0x04` reads by two bytes and the coordinates are silently
  dropped; lat/lon/alt must then appear contiguously in exactly that
  order, 5 bytes each. A mock server must emit all five TLVs in the listed
  order (`tools/mock/meshcom_server.py` does).

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

### 2.3 :sto and the store node / STOR datagram

A store node (`--store own|list|heard`, boards built with `ENABLE_MSGSTORE`: ESP32-S3 and
RAK4631) holds DMs for stations it cannot reach right now and delivers them when the destination is
heard directly. Two wire elements belong to it: the `:sto` custody notice (LoRa text frame, with a
server upload on gateways) and the `STOR` datagram (gateway to server). Design:
`docs/snf-gateway-concept-20261002.md`, `docs/concept-open-issues-20261004.md` section 7.

#### 2.3.1 The `:sto` custody notice

When the store node takes a DM into custody it tells the original sender with an ordinary text
frame. Payload, built by `stoNoticeBuild()` (`src/sto_notice.cpp`) as `"%-9.9s:sto%03u %s"`:

```
<sender call, space padded to exactly 9>:sto<NNN> <held destination call>
DK5EN-93 :sto017 DK5EN-14
```

`NNN` is the 3-digit counter of the held DM (the `{NNN` of the original), the trailing call is the
held destination in readable form for old firmware. No `{`, no `:ack`, no `:rej`: old firmware shows
it as a short DM and nothing acks or stores it. The tag sits at byte 9 like the `:ack` payload (§1.5);
`stoNoticeParse()` anchors there and rejects payloads containing `{`, `:ack` or `:rej`.

Frame as `glueNotify()` (`src/msgstore_glue.cpp`) builds it: type `:`, source call and source path
the store node's own call, destination call and path the original sender, `msg_id` from `millis()`,
queued once into the TX ring (kind "other", not a relay slot). The notice has no `{NNN` of its own
and is not retried by the TX ring; the mailbox's own ladder decides when it is sent again.

**Server upload (SNF-GW-05).** On a gateway with an IP address (`bGATEWAY` and
`node_hasIPaddress`) the same frame is also sent to the server as a `DATA` datagram with rssi 0 and
snr 0 (§2.1, "own transmission"), and registered as own TX (`insertOwnTx`, `addLoraRxBuffer`) so the
server's reflection of it is not taken for a new foreign frame. The LoRa copy is kept. Bench:
`:sto124` uploaded to the mock server on UDP 1990.

**Server to store node.** A PM that the server delivers as `GATE` to a gateway store node for a call
not equal to its own exact call goes through the same store decision as a frame heard on LoRa
(`mboxClassify`, `src/msgstore_hook.h`). A server-side `:ackNNN` for a held PM purges the slot. An
`RM1 ` DM is never stored.

The sender keeps a 16-slot holder table (`stoHolderNote()`), one state update per (holder, NNN) per
hour, so a messages page can show `held by <call>`.

#### 2.3.2 The `STOR` datagram

A gateway-to-server datagram that announces which calls this node holds a mailbox for. Same header
as `KEEP` (§2.1), NUL-terminated, one chunk per UDP datagram:

```
"STOR" + %08X gateway_id + %-9.9s gateway call + %-4.4s version + %-1.1s sub
       + <hold_h> + ";" + <seq> + "/" + <total> + ";" + CALL1 + ";" + CALL2 + ";" ... + 0x00

STOR48A4690DDK5EN-90 4.40a24;1/1;DK5EN-1;DK5EN-92;DK5EN-98;      (bench, DK5EN-90, 3 calls)
STOR48A4690DDK5EN-90 4.40a24;1/1;                                (empty set: withdraws everything)
```

| Field        | Meaning                                                 |
| ------------ | ------------------------------------------------------- |
| `gateway_id` | node id of the store node, as in `KEEP`/`DATA`          |
| `hold_h`     | `--storetime` in hours (0..999); how long custody lasts |
| `seq/total`  | chunk counter, 1-based                                  |
| `CALLn`      | the announce set, each call followed by `;`             |

- **Announce set (SNF-D6).** Store set (calls the active `--store` mode accepts) intersected with the
  calls heard **directly on LoRa within 12 h** (neighbour matrix, `NBR_WINDOW_MIN`). The exact own
  call (call and SSID) is never announced, it is delivered directly; the own base call with another
  SSID is announced when heard. Sorted by `strcmp`, de-duplicated; at most 64 calls (the 64
  `strcmp`-smallest are kept). A call longer than 9 characters, empty, or containing `;` is dropped,
  never truncated (a truncated call would name another station).
- **Chunking.** A datagram stays at or below 255 bytes including the NUL. The plan reserves the worst
  case header (3-digit `hold_h`, 2-digit `seq` and `total`, 36 bytes), leaving 218 bytes of calls
  per chunk, so every chunk of a snapshot is planned the same way (about 20 calls per datagram).
- **Snapshot semantics.** Every send is a full snapshot, not a delta. An empty set is one chunk with
  no calls. A snapshot that is not refreshed for 3 periods (45 min) expires on the server side;
  disabling the feature sends no withdrawal.
- **Timer (`storTick()`, `src/udp_functions.cpp`).** Evaluated with every `KEEP` (30 s) on a
  gateway: first send, then every 15 min, and once when the set changed and stayed stable for 60 s.
  A further change restarts the 60 s debounce. Marker `[STOR];tx;calls;<n>;chunks;<k>`.
- **Off by default.** `--stor on|off` (`node_stor`, default 0), only effective on a gateway and only
  on `ENABLE_MSGSTORE` builds. **The server operator has not approved the datagram:** nothing may be
  sent to the real server until they do. Bench traffic goes to `tools/mock`, which parses `STOR`,
  assembles chunks and expires a snapshot after 45 min. `stor` is not on the RM1 allowlist.
- **Expected server behaviour (not implemented anywhere yet).** A PM for a listed call is sent to
  this gateway in addition to whatever the server does today, never instead of it.

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

**Outbound type contract (DR-18).** `sendExtern()` (`src/extudp_functions.cpp:446`)
decodes the frame itself and then has exactly two payload branches:
position `0x21` (`:501`) and text `0x3A` (`:600`). Every other decoded type
— including ACK `0x41` and hey/weather `0x40` — falls through to
`else return;` (`:664–669`, before the actual `beginPacket()` send) and
produces no datagram. There is no `0x40` branch and no `0x41` branch today;
this is not "unrecognised frames get dropped", it is that only these two
types were ever given a serializer.

The drift-matrix review considered widening this to a four-type set
(`0x21`/`0x3A`/`0x40`/`0x41`) and withdrew it (`DR-18`, re-decided
2026-09-12): routing a binary ACK through `sendExtern()`'s existing branches
would decode fine (`decodeAPRS()` returns `0x41` for an ACK before parsing
anything, `src/aprs_functions.cpp:130–131`) but then fall through the same
`else return;` and emit nothing, so gating it in ships no new behaviour by
itself. What is actually decided for implementation (not yet done):

1. **Shape parity**, not a new type set: the nRF52 handler adopts ESP32's
   gate-then-forward ordering — the EXTUDP forward decided by frame type
   before whatever the relay branch does — for the same two types the
   platforms already agree on. This is a structural/ordering fix on the
   inbound frame-handler side (`src/udp_frame.h`, see that file's carve
   comment), not a change to which types `sendExtern()` itself emits.
2. **A separate JSON ack**, specified in §6.3 of
   `docs/ack-wer-hat-quittiert.md`: a `{"type":"ack",...}` status datagram
   emitted via `queueExtern()` from the BLE-ack call sites (where
   `addBLEOutBuffer(print_buff, ...)` is called for a `0x41` frame today).
   This is **not** the same mechanism as forwarding a raw `0x41` frame
   through `sendExtern()` — it is a purpose-built status object, queued
   from the main loop like the existing text/position datagrams, and it is
   the only planned way an ACK's existence reaches an EXTUDP peer. Do not
   conflate "sendExtern() gains a 0x41 branch" (rejected) with "an ack
   datagram gets added via queueExtern()" (decided, pending W6).

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
(`config_cmds[]`, `src/nrf52/nrf52_main.cpp:296` /
`src/esp32/esp32_main.cpp:306`):

```
--info --seset --wifiset --nodeset --wx --pos --aprsset --io --tel [--analogset (ESP32 only)]
```

plus the MH list burst (`mhPhoneListStart()`, `mhPhoneListPending()`,
`mhPhoneListStep()`, `src/mh_phone.cpp:250–318`): one `MH` JSON (schema
below) per MHeard entry from the last 12 h (`NBR_WINDOW_MIN` = 720 min,
`src/nbr_matrix.h:106`), newest first, queued through the same command ring
as the command replies once that ring is empty. Each command emits one or
two `0x44` JSON notifications (§4.3). After a 3 s settle and once the
command ring, the MH list and the data ring have all drained, the node
sends `{"TYP":"CONFFIN"}` exactly once (`src/nrf52/nrf52_main.cpp:1859–1902`,
`sendConfigFinish()`, `src/command_functions.cpp:6249`). The burst happens
once per genuine hello — a mock phone that reconnects without a new hello
gets no re-send, and a mock node must reproduce the burst→MH-list→CONFFIN
order (MCProxy's cache hydration depends on it,
`MCProxy/src/mcapp/ble_hydration_tests.py`).

`0x44` JSON schemas (producers in `src/command_functions.cpp`; all objects
carry `"TYP"` as discriminator):

| TYP       | source command                                    | fields                                                                                                                                                                                                         |
| --------- | ------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `I`       | `--info`                                          | FWVER, CALL, ID (gateway id), HWID, MAXV, BLE ("long"/"short"), BATP, BATV, GCB0…GCB5 (groups), CTRY, BOOST, BPIN                                                                                              |
| `SE`+`S1` | `--seset`                                         | SE: BME, BMP, BMP3, BMP3F, AHT, AHTF, BMXF, 680, 680F, 811, 811F, SS, LPS33, OW, OWPIN, OWF, USERPIN · S1: INA226, SHUNT, IMAX, SAMP, SHT, SHTF, 226, 226F                                                     |
| `SW`+`S2` | `--wifiset`                                       | SW: SSID, IP, GW, AP, DNS, SUB · S2: OWNIP, OWNGW, OWNMS, OWNDNS, OWNNTP, EUDP, EUDPIP, TXPOW                                                                                                                  |
| `SN`      | `--nodeset`                                       | GW, WS, DISP, BTN, MSH, GPS, TRACK, UTCOF, TXP, MQRG, MSF, MCR, MBW, GWNPOS, NOALL, NOPMOTHER, BLED, GWS (228 of 244 bytes: no room left, see `SN1`)                                                           |
| `SN1`     | `--nodeset` (second frame, sent right after `SN`) | VIA, VIACALL, WSPWD, ASYM, TZ (TZ-01: `node_tz`, POSIX rule string, `""` when unset, last key; 65 B before TZ-01, 99 B with the ESP32 default rule `CET-1CEST,M3.5.0,M10.5.0/3`, worst case 168 B)             |
| `W`       | `--wx`                                            | TEMP, TOFFI, TOUT, TOFFO, HUM, PRES, QNH, ALT, GAS, CO2, VBUS, VSHUNT, VAMP, VPOW                                                                                                                              |
| `G`       | `--pos`                                           | LAT, LON (signed decimal degrees), ALT, SAT, SFIX, HDOP, RATE, NEXT, DIST, DIRn, DIRo, DATE                                                                                                                    |
| `SA`      | `--aprsset`                                       | ATXT, SYMID, SYMCD, NAME                                                                                                                                                                                       |
| `IO`      | `--io`                                            | MCP23017, AxOUT, AxVAL, BxOUT, BxVAL (bit strings)                                                                                                                                                             |
| `TM`      | `--tel`                                           | PARM, UNIT, FORMAT, EQNS, VALES, PTIME                                                                                                                                                                         |
| `AN`      | `--analogset`                                     | ESP32 only: APN, AFC, AK, AFL, ACK, ADC, ADCRAW, ADCE1, ADCE2, ADCSL, ADCOF, ADCAT                                                                                                                             |
| `MH`      | `mhJsonBuild()` (neighbour matrix, not a command) | 13 old fields, old order — CALL, DATE, TIME, PLT (payload type byte), HW, MOD, RSSI, SNR, DIST, PL, MESH, NCNT — then 7 new (concept 4.9, appended, dropped first on overflow): AGE, HM, ROLE, EX, NB, GW, VIA |
| `CONFFIN` | `--conffin`                                       | no further fields                                                                                                                                                                                              |

**The `MH` frame (concept 4.9 of `docs/meshcom5-topologie/`).** Built by
`mhJsonBuild()` (`src/mh_phone.cpp:73–199`) from the neighbour-matrix MH view
(`NbrMhView`, `src/nbr_views.h:27–50`), with `bleJsonFrameFailSoft()` and a
`BLE_JSON_PAYLOAD_MAX` of 244 bytes (`src/configuration_global.h:562`) — on
overflow the 7 new trailing fields drop first, never the 13 old ones. There
is no MHeard ring buffer any more; `mheard_functions.cpp` is gone.
`DIST` is rounded to 0.1 km (`mhRoundDist()`, `src/mh_phone.cpp:42–49`), `-1`
when either the node's own position or the neighbour's is unknown. `DATE`/
`TIME` are local time — the node epoch is `getUnixClock()` (raw UTC) plus
`node_utcoff*3600` (`mhNodeEpoch()`, `:65–70`) — and no `MH` frame is built
at all without a valid clock (year < 2025 rejected at the entry, `:90–94`).

Records reach the phone two ways:

- **Live**: `mhPhoneLive()` (`src/mh_phone.cpp:207–222`), called from
  `OnRxDone` (`src/lora_functions.cpp:634`) at most once per neighbour and
  minute, over the same BLE output the old live path used
  (`addBLEOutBuffer()`).
- **List on connect**: the post-hello burst (§4.2 above) — newest first, a
  12 h window, one snapshot per hello.

**The `PLT` contract (DR-29).** `PLT` is still the raw wire payload-type
byte, `(uint8_t)` cast, not a decoded string — `mhJsonBuild()` emits
`doc["PLT"] = (uint8_t)v.plt` (`src/mh_phone.cpp:162`), so a position frame
(`'!'`, 0x21) serializes as `PLT:33`, never `"POS"`. This is unchanged from
before the MeshCom 5 rework and still deliberate: JSON/BLE — the
machine-readable feed — carries the raw byte, while the human-facing serial
`[MH]` line (`src/command_functions.cpp:5106–5109`) and the web/T-Deck views
(`src/web_functions/web_functions.cpp:1524`,
`src/t-deck/lv_obj_functions.cpp:4636`,
`src/t-deck-pro/ui_deckpro.cpp:1349`) carry decoded text via
`nbrPayloadTypeName()` (`"TXT"`/`"POS"`/`"HEY"`/`"???"`,
`src/nbr_views.cpp:434`). The two renderers are intentionally inconsistent
with each other and **must stay that way**: do not "fix" `mhJsonBuild()` to
emit `"POS"` instead of a number — MCProxy and the app both read `PLT` as
numeric (`ble_protocol.py` `_coerce_mh_payload_type()` fails closed on a
non-numeric value; the app's `AppInterfaces.ts` declares `PLT: number`), so
changing the JSON shape would break every existing client silently.

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

**Time zone over BLE (TZ-01).** There is no new frame type. The app sets
the POSIX TZ rule as a `--settz <rule>` command in a `0xA0` text message
(e.g. `--settz CET-1CEST,M3.5.0,M10.5.0/3`); it starts with `--`, so it is
dispatched through `commandAction()` like any other command
(`src/command_functions.cpp:743–787`). `--settz none` (or a bare `--settz`)
clears the rule. A rejected rule (parse error, `Jn`/`n` day rules, more than
39 characters) changes nothing. On success the node
re-sends `SN` + `SN1` (`sendNodeSetting()`), so the new rule arrives as the
`TZ` key of `SN1` (`src/command_functions.cpp:6751`) and the derived offset
as `UTCOF` of `SN`. While a rule is set, `UTCOF` is a derived value: it
follows the DST switches by itself, and a `--utcoff` from the app clears the
rule again (the fixed offset wins). `TZ` is the last key of `SN1`, so
`bleJsonFrameFailSoft()` drops it first on overflow; the worst case (39-char
rule) stays far below the 244-byte limit. `node_tz` is written to flash
under the keyed setting `node_tz` (`char[40]`), without a
`FLASH_STRUCT_VERSION` bump; the frozen BLE settings v1 layout
(`ble_settings_v1.h`) does not carry it.

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
BW 250 kHz, SF 11, CR 4/6) with the raw-frame capture hook
(`src/capture_functions.cpp`, `--loradebug on` / `--txcapture on`); encoders observed
on the air include this firmware (RAK4631, Heltec V3), other MeshCom 4.35
devices (T-Beam), and gateway-emitted server frames. Cross-validation:
§4 (BLE) against `MCProxy/src/mcapp/ble_protocol.py`; §2 (server UDP)
against `mc-chat/meshcom_mock/` (softnodes live on the OE and DL servers)
and the upstream MeshCom-Reflector spec, 2026-08-21.
