# How to read MeshCom firmware logs

Reference for radio amateurs and developers. Covers the console output of MeshCom firmware 4.40a
(fork `fork-dev`, state 2026-10-12) with the debug switches on, and assesses the protocol
interpretation in the webapp "Monitor" section.

Every line format below cites its emitter as `file:line` in the firmware repo
(`MeshCom-Firmware-DEV-Main`); `webapp:` and `MCProxy:` prefixes mean the other two repos. Example
lines are real, copied from `~/meshlog/rpizero/dk5en-98/2026-10-05.log` (ESP32 Heltec V3 gateway,
4.40a, setlog + loradebug + txcapture), `~/meshlog/dk5en-90/serial-20261001.log` (RAK4631,
nRF52840) and `logs/dj8meh/2026-08-23-dj8meh-41-putty-1.log` (PuTTY capture, build of 22 Aug).

## BLUF

- A log is four stacked layers: the meshlogger prefix (host receive time), the node's own `HH:MM:SS`,
  the line family tag (`[MC-DBG]`, `[LOG]`, `MH-LoRa`, ...), and the fields. Which families appear
  depends on three persistent switches (`--loradebug`, `--setlog`, `--txcapture`) and on the
  platform: the TCP console on port 2323 and `[MC-STAT]`/`[MC-PRIO]`/`[MC-HWM]`/`[MC-WDT]` exist on
  ESP32 only.
- Packet lines (`MH-LoRa`, `RX-LoRa2`, `TX-LoRa`, `RX-UDP`, `TX-UDP`) are one grammar; `[LOG]` lines
  are the same body plus an RSSI/SNR/DUP/OWN tail, and add `RLY`, `TX`, `ERR`, `STAT`, `DM`, `MBOX`,
  `GWI`, `GWU` kinds. `[LOG] STAT` every 300 s is the best single health record.
- A hex dump (`[MC-TEST] RX_FRAME`) can be decoded by hand in a minute (section 5); the FCS is a
  plain 16-bit byte sum, so you can prove whether a frame was received intact.
- Biggest traps: pre-`f2d6532f` ESP32 builds bill every reception as a 255-byte frame; timestamps in
  captures are batch receive times, not event times; the 2323 console takes exactly one client;
  `docs/loradebug-serial-output.md` is wrong in several places (appendix A).
- The webapp Monitor does not interpret firmware log lines. Its DBG rows show the console text with a
  five-class highlighter (`tokenizeConsoleLine`). On real firmware lines the highlighter misses the
  dominant forms and produces three kinds of false positives (section 8).

## 0. Reading conventions

| Notation      | Meaning                                                           |
| ------------- | ----------------------------------------------------------------- |
| `x1AE1E034`   | message id, 8 hex digits, printed with a leading `x`              |
| `H03`         | remaining hop budget (low nibble of frame byte 5)                 |
| `ms`, `s`     | milliseconds, seconds (all `*_ms` fields are milliseconds)        |
| `t=`, `ts=`   | node `millis()` since boot, not wall time                         |
| ESP32 / nRF52 | Heltec V3, T-Beam, T-Deck (ESP32) / RAK4631, T114, T-Echo (nRF52) |
| "observed"    | measured on the three logs above, not derived from source         |

## 1. Where logs come from

### 1.1 Sources

| Source                                                                                | Platforms  | Timestamp prefix                                                                      | Notes                                                                                                                                                                                                                                                                |
| ------------------------------------------------------------------------------------- | ---------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| USB serial (`tools/serial_monitor.py`, `tools/bench/serial_capture.py`, any terminal) | both       | none from the node; capture tools add `YYYY-MM-DD hh:mm:ss.mmm  `                     | Opening the port resets an ESP32; the RAK4631 stays mute without DTR. On nRF52 `printfdeb` waits at most 20 ms for FIFO space and then drops bytes (`src/printfdeb_functions.cpp`, `cdcReady`), counted as `serial_bytes=` in `CAPTURE_DROPPED`.                     |
| Net console, TCP 2323                                                                 | ESP32 only | none                                                                                  | `src/net_console.cpp` (`#if defined(ESP32)`). HMAC login when `--passwd` is set. Exactly one client. Writes are non-blocking best effort: when the TCP send buffer is full the rest is dropped (`raw_write_all`, `net_console.cpp:99-110`).                          |
| `tools/meshlogger.py` (records 2323)                                                  | ESP32 only | `YYYY-MM-DD hh:mm:ss.mmm  ` = host receive time (`meshlogger.py:106`)                 | Appends to `<outdir>/YYYY-MM-DD.log` (local date), `status.txt` every minute, `PAUSE` file releases the console, own `[LOGGER]` lines. Sets `--loradebug on` and `--txcapture on` (`DEFAULT_FLAGS`, line 83) and restores the prior state at the end. About 200 B/s. |
| PuTTY session log                                                                     | both       | one header `=~=~=~= PuTTY log 2026.08.23 13:12:52 =~=~=` at the top, nothing per line | Only the node's own `HH:MM:SS` inside packet lines and `[HEAP]` lines carries time.                                                                                                                                                                                  |
| Webapp Monitor, DBG button                                                            | ESP32 only | arrival time at the backend, second resolution                                        | MCProxy opens port 2323, sets `loradebug` and `txcapture`, forwards each line as a `wire:frame` (section 8).                                                                                                                                                         |

### 1.2 Timestamp layers

```
2026-10-05 00:00:56.634  00:00:56 MH-LoRa 064 @ xA183638F H02 S0 T0 M01 DF8RD-1,...
|----- meshlogger -----| |-node-| |--------------- firmware line ---------------|
```

- Meshlogger time is when the logger read the TCP chunk. In the 20,000 first lines of the dk5en-98
  log there are 8,979 distinct millisecond stamps (2.2 lines per stamp); in the RAK serial capture
  4,635 stamps (4.3 lines per stamp). Lines of one burst share a stamp. Example from the RAK log:
  node clock `23:36:14` on the packet lines, capture stamp `23:36:17.677` on all of them.
- Node time (`getTimeString()`, `src/loop_functions.cpp:3348`) is `HH:MM:SS` from the node clock,
  without date. Before a time source is set it reads `00:00:00` (PuTTY log: `[HEAP] 00:00:00 ...
(init)`).
- For intervals use the in-line counters: `t=` in `[LOG]` lines, `ts=` in `[MC-DBG]` lines, `lat=`
  and `wait=` in TX lines. All are `millis()` of the node and share one clock.
- Some firmware lines have no node time at all (`[MC-DBG]`, `[MC-SM]`, `OnRxDone`).

### 1.3 Switches that decide which families appear

| Command                  | Flag variable                                  | Persisted in flash                                          | Enables                                                                                                                                                                    |
| ------------------------ | ---------------------------------------------- | ----------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--loradebug on` / `off` | `bLORADEBUG` + `bDisplayInfo` + `bDisplayRetx` | yes (`node_sset` 0x0200)                                    | all `[MC-DBG]`, `[MC-SM]`, `[MC-STAT]`, `[MC-PRIO]`, `[MC-TRICKLE]`, `OnRxDone` lines, `RX_FRAME` capture, plus the packet lines below (`command_functions.cpp:2906-2935`) |
| `--setinfo on` / `off`   | `bDisplayInfo`                                 | no (reset from `bLORADEBUG` at boot, `esp32_main.cpp:1418`) | `MH-LoRa`, `RX-LoRa2`, `TX-LoRa`, `RX-UDP`, `TX-UDP`, `NEW-HEY/POS/TXT/PING` without the debug trace                                                                       |
| `--setlog on` / `off`    | `bDisplayLog`                                  | yes (`node_sset4` 0x0004)                                   | `[LOG]` lines (section 3). Also silences `[HEAP]` and `[MC-HWM]` (heap moves into `STAT heap=`)                                                                            |
| `--setlog <CALL>`        | `LogCallsign` filter + `bDisplayLog`           | yes                                                         | `[LOG]` RX lines only for frames whose source is `<CALL>` (`lora_functions.cpp:1088-1095`); other kinds stay                                                               |
| `--txcapture on` / `off` | `bTXCAPTURE`                                   | yes (`node_sset4` 0x0008)                                   | `[MC-TEST] TX_FRAME`. The RX side (`RX_FRAME`) follows `--loradebug`                                                                                                       |
| `--nbrdebug on` / `off`  | `bNBRDEBUG`                                    | yes (`node_sset4` 0x0400)                                   | `[NBR]\|...` neighbour-matrix lines (format: `docs/nbr-logformat.md`)                                                                                                      |
| `--setcont on` / `off`   | `bDisplayCont`                                 | yes (`node_sset` 0x4000)                                    | continuous extras such as `[LORA-ERROR]...NNN RCV:` (`lora_functions.cpp:1343`)                                                                                            |
| `--viadebug on` / `off`  | `bDisplayVia`                                  | no                                                          | prefixes `TX-UDP` lines with `[MESHu]...`                                                                                                                                  |
| `--info`                 | none (prints a page)                           | -                                                           | node status, flag states `...LORADEBUG on ...TXCAPTURE off`, boot reason `...BOOT RESET_REASON=1 POWERON` (`command_functions.cpp:6750,6764`)                              |

`--info` is not a log switch: it dumps the settings page. The switch for the packet lines is
`--setinfo` (or implicitly `--loradebug`). The command table is `src/command_functions.cpp:438-540`.

### 1.4 ESP32 and nRF52 differ

| Item                                             | ESP32                                                                                     | nRF52 (RAK4631)                                                                                          |
| ------------------------------------------------ | ----------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| Net console 2323                                 | yes                                                                                       | no                                                                                                       |
| CRC failure                                      | `CRC_ERROR` + `CRC_PAYLOAD[n]` dump (`esp32_main.cpp:4863-4868`)                          | `RX_ERROR rssi= snr= ts=` only, no dump (`lora_functions.cpp:2675`)                                      |
| `CAD_SCAN result=`                               | RadioLib code: `-15` free, `-702` busy                                                    | synthetic: `0` free, `-702` busy (`nrf52_main.cpp:1663`)                                                 |
| `RING_STATUS`                                    | has `dedup=used/MAX` and `dist=`                                                          | no `dedup=` (`nrf52_main.cpp:1491`)                                                                      |
| `[MC-STAT]`, `[MC-PRIO]`, `[MC-HWM]`, `[MC-WDT]` | yes                                                                                       | not emitted (no emitter in `src/nrf52/`)                                                                 |
| `[MC-TRICKLE]`                                   | `SEND`, `SUPPRESS`, `TOPO_CHANGE` under `bLORADEBUG`                                      | `SUPPRESS` only, under `bDisplayInfo`, without `k=`/`neighbors=` (`nrf52_main.cpp:2174`)                 |
| `RX_BUF_RELEASE buf=`                            | no                                                                                        | yes (double RX buffer, `lora_functions.cpp:2609`, `#if BOARD_RAK4630`)                                   |
| `RX_TIMEOUT_DEFERRED`                            | `src=receiveFlag`, `header_valid`, `preamble_only`                                        | `src=is_receiving` only (`nrf52_main.cpp:1596`)                                                          |
| `[HEAP]` line                                    | `[HEAP];HH:MM:SS;free;minfree;maxblock;(init\|mon)` via `printfdeb` (`;` becomes a space) | `HH:MM:SS;[HEAP];free;min;maxblock;(init\|mon)` via raw `Serial.printf` (`;` kept), `nrf52_main.cpp:571` |
| Boot reason                                      | `[BOOT] RESET_REASON=<n> <name>` (`esp32_main.cpp:1108`)                                  | `[BOOT] RESETREAS=0x%08lX` (`nrf52_main.cpp:1107`)                                                       |
| `OnRxDone` duration                              | median 57 ms on the observed gateway (see 4.1)                                            | 3-24 ms observed                                                                                         |

## 2. Packet lines

### 2.1 Grammar

Emitter for everything except ACK frames: `printBuffer_aprs()`, `src/loop_functions.cpp:3478-3483`.

```
HH:MM:SS <TAG> <len> <t> x<msgid> H<hop> S<s> T<t> M<m> <path>><dest><t><payload> HW:<hw> MOD:<c>/<m> FCS:<fcs> FW:<ver>:<sub> LH:<lh>
```

Real examples (meshlogger prefix removed):

```
00:00:01 MH-LoRa 115 ! x3BAC51B8 H00 S1 T0 M01 DD7MH-55,DB0MMR-12,DL2JA-2>*!4809.90N/01237.00EyPowerNode JN68HD#Hans/A=001535/N10/O=20.4/R=9;26386; HW:39 MOD:8/8 FCS:1BCC FW:40:a LH:AB
00:00:41 NEW-HEY 030 @ x1AE1E034 H04 S0 T0 M00 DK5EN-98>HG@R6; HW:43 MOD:8/8 FCS:06DB FW:40:a LH:AB
00:00:45 TX-LoRa  030 @ x1AE1E034 H04 S0 T0 M01 DK5EN-98>HG@R6; HW:43 MOD:8/8 FCS:06DB FW:40:a LH:AB
00:03:22 RX-UDP  051 : x6AC291FC H02 S1 T0 M00 OE1XAR-33>*:{CET}2026-10-04 22:03:22 HW:00 MOD:0/3 FCS:0BF9 FW:00:# LH:00
```

| Token            | Printf          | Meaning                                                                                                                                                                                                                                                                                            |
| ---------------- | --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `HH:MM:SS`       | `%s`            | node clock, section 1.2                                                                                                                                                                                                                                                                            |
| `<TAG>`          | `%s`            | table in 2.2. Since `c78adcc5` (2026-08-03) the tag has no colon: `MH-LoRa 105 !` instead of `MH-LoRa: 105 !`; logs older than that differ                                                                                                                                                         |
| `<len>`          | `%03i`          | `msg_len`: bytes the decoder consumed, including the trailer. In the 115-byte example it equals the hex dump length                                                                                                                                                                                |
| `<t>`            | `%c`            | payload type: `!` position (0x21), `@` HEY / weather (0x40), `:` text (0x3A). Repeated after the destination in the string, as on air                                                                                                                                                              |
| `x<msgid>`       | `x%08X`         | 32-bit id, little endian on air. Built as `((gw_id & 0x3FFFFF) << 10) \| counter`: the upper 22 bits identify the sender node (all DK5EN-98 ids start `1AE1E0..`), the low 10 bits are a counter that wraps at 1000 (`docs/architecture/11-wire-format.md` 1.1). `x1AE1E034` is counter 0x034 = 52 |
| `H<hop>`         | `H%02X`         | remaining hop budget, low nibble of byte 5 (`aprs_functions.cpp:180`). Each relay decrements it. Own HEY at `H04`, heard after one relay at `H03`                                                                                                                                                  |
| `S<n>`           | `S%i`           | server flag, byte 5 bit 7 (0x80): the frame has passed a gateway/server. A gateway sets it when it relays (see 5.3: `S0` on RX, `S1` on its own TX of the relayed copy). Not "sent by server"                                                                                                      |
| `T<n>`           | `T%i`           | track flag, bit 6 (0x40)                                                                                                                                                                                                                                                                           |
| `M<nn>`          | `M%02X`         | mesh flag, bit 4 (0x10): relaying allowed. Printed as `00`/`01`. The `NEW-*` print is taken before the encoder sets the bit, so `NEW-HEY` shows `M00` and the `TX-LoRa` line of the same frame `M01`                                                                                               |
| (not printed)    |                 | bit 5 (0x20) `msg_app_offline` is decoded (`aprs_functions.cpp:185`) but never printed                                                                                                                                                                                                             |
| `<path>`         | `%s`            | comma list, **originator first**, each relay appends its own call. The receiver's call is absent on `MH-LoRa`/`RX-LoRa2`; a `TX-LoRa` of a relayed frame has the own call appended at the end                                                                                                      |
| `><dest>`        | `%s`            | `*` broadcast, a group number (`26298`, `9`), a callsign for a DM, or `H` / `HG` for a HEY (`HG` from a gateway)                                                                                                                                                                                   |
| payload          | `%s`            | everything up to the 0x00 terminator. Position: APRS position plus `/A=` altitude, `/N..` etc. DM: ends in `{NNN` (sequence number, **no closing brace**; ack request). HEY: `R<n>;` + one signal group per hop                                                                                    |
| `HW:<hw>`        | `HW:%02i`       | hardware id of the originator, decimal. Seen: 43 Heltec V3, 9 RAK4631, 12 T-Beam v1.2 (own bench nodes), 0 = server/unknown                                                                                                                                                                        |
| `MOD:<c>/<m>`    | `MOD:%01X/%01i` | high nibble (country code, hex) / low nibble (modulation, decimal) of the MOD byte (`(country << 4) \| modulation`). `8/8` = country 8 (EU8 in `11-wire-format.md`), modulation 8. Server frames show `0/3`                                                                                        |
| `FCS:<fcs>`      | `FCS:%04X`      | 16-bit byte sum, big endian on air; verified by the decoder (section 5)                                                                                                                                                                                                                            |
| `FW:<ver>:<sub>` | `FW:%02i:%c`    | firmware version as one byte (40 = 4.40) and a sub-version letter. `FW:00:#` means "not present" (server frames). The on-air field is 1 byte + 1 char; the 5-character string `4.40a` (`%-4.4s%-1.1s`) only exists in node settings and the `KEEP`/`DATA` frames to the server                     |
| `LH:<lh>`        | `LH:%02X`       | "last hop hardware": 0x80 ("last heard" marker, set by the encoder) \| hardware id of the last transmitting node (`11-wire-format.md` 1.1). `AB` = 0x80\|43 Heltec V3, `89` = 0x80\|9 RAK4631, `8C` = 0x80\|12 T-Beam                                                                              |

### 2.2 Tags and where they come from

| Tag                     | Emitter                                                          | Printed when                                                                                               |
| ----------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| `MH-LoRa`               | `lora_functions.cpp:1504`                                        | every decoded frame (duplicates too), `bDisplayInfo`, unless the last relay in the path is the node itself |
| `RX-LoRa2`              | `lora_functions.cpp:1659`                                        | frame accepted after dedup (new msg id)                                                                    |
| `TX-LoRa ` / `TX-Lora ` | `lora_functions.cpp:3070` / `:3066`                              | at TX start; lower-case `Lora` is the ACK variant (section 2.3)                                            |
| `RX-UDP `               | `esp32/udp_frame_esp32.cpp:281`, `nrf52/udp_frame_nrf52.cpp:167` | frame received from the MeshCom server over UDP (internet side)                                            |
| `TX-UDP  `              | `esp32/udp_drain_esp32.cpp:162`, `nrf52/udp_drain_nrf52.cpp:143` | LoRa frame uploaded to the server; shows the frame **before** the hop decrement (5.3)                      |
| `NEW-HEY/POS/TXT/PING`  | `loop_functions.cpp:5472 / 5205,5292 / 4406 / 3612`              | the node builds its own frame; printed before encoding, so `M00`                                           |
| `[LOG]`                 | `lora_functions.cpp:1087-1095`, `:532`                           | `--setlog on`, section 3                                                                                   |

Companion lines in the same RX sequence (all `bDisplayInfo`/`bLORADEBUG`):

```
00:00:01 MESH    : <no via info>return MESH=true          via_functions.cpp:114
00:00:17 checkOwnTx:1AE1E033 own_msg_id:1AE1E033 <1AE1E033> 00   loop_functions.cpp:821 (own-message echo check; last byte = status)
00:00:41 Insert own_msg_id:1AE1E034 <1AE1E034>            loop_functions.cpp:875 (own id remembered for echo detection)
```

### 2.3 ACK frames

ACKs have no path and no trailer, so they use `printBuffer_ack()` (`loop_functions.cpp:3485-3490`):

```
07:52:01 [LOG] 012 A x00F76491 H81 x03B50343 01 00 RSSI:-116 SNR:-4 DUP:n OWN:- t=40371047
08:02:52 TX-Lora  012 A x083DC0E9 H83 xC05311B0 01 00
```

| Field         | Meaning                                                                                              |
| ------------- | ---------------------------------------------------------------------------------------------------- |
| `012` / `007` | frame length (a 7-byte short form exists)                                                            |
| `A`           | type 0x41                                                                                            |
| `x00F76491`   | the ACK frame's own id                                                                               |
| `H81`         | **the whole byte 5**, not only the hop nibble: `0x80` server flag + 1 hop left. Here `H8n` is normal |
| `x03B50343`   | id of the message being acknowledged                                                                 |
| `01 00`       | bytes 10 and 11; `01` = ACK by gateway/node                                                          |

DM acknowledgement at text level is separate: the receiver answers with a normal text frame whose payload is
`<callsign padded to 9>:ackNNN`, for example `DK5EN-1  :ack400` (`11-wire-format.md` 1.5).

## 3. `[LOG]` setlog lines (`--setlog on`)

All go through `setlogPrint()` (`loop_functions.cpp:3358`): `HH:MM:SS [LOG] <body>`. Formatters:
`src/setlog_lines.cpp`.

### 3.1 RX line

The `printBuffer_aprs` body (section 2.1) with the tag `[LOG]` plus a tail (`setlog_lines.cpp:33`):

```
00:00:17 [LOG] 049 @ x1AE1E033 H03 S1 T0 M01 DK5EN-98,DL2JA-2>HG@R6;17,114,-18; HW:43 MOD:8/8 FCS:0B58 FW:40:a LH:AB RSSI:-114 SNR:-2 DUP:n OWN:e t=12066854
```

| Tail    | Meaning                                                                                               |
| ------- | ----------------------------------------------------------------------------------------------------- |
| `RSSI:` | dBm of this reception                                                                                 |
| `SNR:`  | dB. Typical LoRa floor at SF11/BW250 is about -17.5 dB demodulation limit; negative values are normal |
| `DUP:`  | `n` new msg id, `d` duplicate (second copy of an id already in the dedup ring)                        |
| `OWN:`  | `e` the node's own call is in the path (echo of its own transmission), `-` otherwise                  |
| `t=`    | node `millis()`                                                                                       |

Printed by `lora_functions.cpp:1087` (ACK frames at `:532`, where `OWN:` is always `-`). Neighbour reports (`...>HN@R6;N7;DK5EN-90,6;...`, the
neighbour matrix) get a `[LOG]` RX line and, when sent by this node, a `TX-LoRa` line, but no `MH-LoRa`/`RX-LoRa2` line: `OnRxDone` leaves
right after them (`lora_functions.cpp:1100-1135`; observed 71 `[LOG]` and 0 `MH-LoRa` lines with `>HN@`). On nRF52 the RX tail carries RSSI/SNR as well.

### 3.2 `RLY` - what happened to a new frame

`[LOG] RLY x3BAC51B8 ! H00 q=gwcap prio=0 slot=-1` (`setlog_lines.cpp:46`, call sites `lora_functions.cpp:1358,2519`).
One line per **new** frame. `H` is the hop nibble as received, `prio` the TX-ring priority (0 = not queued), `slot` the TX-ring slot
(-1 = not queued; since the QRT campaign 2026-10-18 a `q=full` line shows `slot=-2`, the ring-full return code
`RING_ENQUEUE_FULL`).

| `q=`       | Meaning                                                                                                                                                                                   | Source                    |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------- |
| `tx`       | queued for relay                                                                                                                                                                          | `lora_functions.cpp:2454` |
| `hop0`     | hop budget exhausted, not relayed                                                                                                                                                         | `:2505`                   |
| `full`     | relay wanted but TX ring full; alarming if frequent (observed 13 per hour at a busy site); since TXP W1c also a relayed `{CET}` older than the one already queued (`RING_SUPERSEDE_KEEP`) | `:2494`                   |
| `cet`      | `{CET}` beacon older than 150 s, or the node has no trusted clock (GPS, NTP, RTC): not relayed (TXP W1a); a `[CET];drop;where;relay` line follows                                         | `cetFrameStale()`         |
| `loop`     | own call already in the path                                                                                                                                                              | `:2367`                   |
| `pathfull` | path plus "," and own call would exceed 119 characters (decoder limit), not relayed                                                                                                       | `lora_functions.cpp`      |
| `nomesh`   | mesh relay switched off (`--mesh off`) or `checkMesh()` refused                                                                                                                           | `:2332`                   |
| `gwcap`    | gateway does not put frames beyond the text/position hop cap back on air                                                                                                                  | `:2248,2254`              |
| `gwfilter` | destination never goes to mesh on a gateway (for example `APRS2SOTA`, `100001`)                                                                                                           | `:2243,2340`              |
| `self`     | addressed to this node                                                                                                                                                                    | `:2320`                   |
| `aprs`     | node runs in LoRa-APRS mode                                                                                                                                                               | `:2326`                   |
| `ping`     | `{ping}` to `100001`, never relayed                                                                                                                                                       | `:2266`                   |
| `unconf`   | source still has the factory callsign, dropped before any processing                                                                                                                      | `:1355`                   |

### 3.3 `TX`

`[LOG] TX x1AE1E034 @ H04 prio=5 src=o wait=3770 q=1 cad=0 len=30 t=12094643` (`setlog_lines.cpp:59`, `lora_functions.cpp:2829`)

| Field  | Meaning                                                                                                                                                                    |
| ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `H`    | hop nibble of the frame being sent                                                                                                                                         |
| `prio` | 1 ACK + DM, 2 group and broadcast, 3 relay, 4 position, 5 HEY (`configuration_global.h:426-430`)                                                                           |
| `src`  | `o` own, `r` relay, `g` injected from the server by a gateway (`ringSource`)                                                                                               |
| `wait` | ms from queueing to air (same as `lat=` in `RING_TX_READ`). Observed in one hour: averages own 14 s, relay 61 s, gateway-fed 70 s; medians 11 s, 34 s, 12 s; maximum 251 s |
| `q`    | TX-ring depth after taking this entry                                                                                                                                      |
| `cad`  | CAD attempts used before this transmission (0 = channel free at first scan)                                                                                                |
| `len`  | bytes on air                                                                                                                                                               |

Printed when the transmission has started, not at `TX_DONE`; a watchdog abort therefore shows as a TX line without a `TX_DONE` and
counts in `STAT txfail=`.

### 3.4 `ERR`

`[LOG] ERR rssi=-69 snr=1 len=100 ferr=-24 t=12118562` (`setlog_lines.cpp:72`; ESP32 `esp32_main.cpp:4855`, nRF52 `lora_functions.cpp:2705`).
A frame that failed CRC or was otherwise lost. `ferr` is the frequency error in Hz (ESP32 only; nRF52 prints `len=0 ferr=0`). Pairs with
`[MC-DBG] CRC_ERROR` one millisecond later.

### 3.5 `STAT` (every 300 s)

```
00:04:11 [LOG] STAT util=6 rx=15541 tx=4030 newid=7 dup=3 err=2 txn=5 txfail=0 ringmax=1/20 drop=0/0/0/0/0 mh=6 heap=128904 trk=240/3 fw=40a/20260929 up=12300 t=12300155
```

Emitter: `setlogFormatStat()` (`setlog_lines.cpp:83`), values from `setlogFillStat()` (`loop_functions.cpp:3379`), tick `esp32_main.cpp:2875` /
`nrf52_main.cpp:1574`. All interval counters reset after each print.

| Field             | Unit / meaning                                                                                                                                                         | Normal / alarming                                                                                               |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `util`            | % channel occupancy over the 300 s window: `100*(rx+tx)/300000`, capped at 100                                                                                         | observed 6-31 % per window at a busy site; sustained above 50 % means congestion                                |
| `rx`, `tx`        | summed airtime in ms. On ESP32 builds before `f2d6532f` `rx` is inflated, see 6.1                                                                                      | `tx` should be small against `rx`                                                                               |
| `newid`           | new msg ids seen (dedup verdict NEW), including frames a gateway receives from the server                                                                              | with `dup` gives the copies per frame                                                                           |
| `dup`             | duplicate receptions                                                                                                                                                   | observed ratio dup/(newid+dup) 58 %; a mesh produces several copies per frame                                   |
| `err`             | CRC and other RX errors                                                                                                                                                | about 5-6 % of receptions at the observed site; bursts mean collisions or interference                          |
| `txn`             | own transmissions (own + relay + gateway-fed), equals the `[LOG] TX` count                                                                                             |                                                                                                                 |
| `txfail`          | transmissions aborted by the TX watchdog (15 s, `TX_WATCHDOG_MS`)                                                                                                      | must be 0                                                                                                       |
| `ringmax`         | high-water mark of TX-ring depth / `MAX_RING` (20 on ESP32 and RAK4631)                                                                                                | 17/20 seen in a busy hour; at `MAX_RING` frames are dropped                                                     |
| `drop=a/b/c/d/e`  | TX-ring drops per priority class 1..5 (ACK/DM, group, relay, position, HEY)                                                                                            | p1 or p2 non-zero is serious (user traffic lost); p3 and p5 drops are load shedding (22 and 7 seen in one hour) |
| `mh`              | neighbour count from the neighbour matrix (`nbrNcnt`); the plan document `docs/setlog-instrumentation-impl-plan-20260902.md` still says MHeard count                   | also appears as the `R<n>` in the node's own HEY                                                                |
| `heap`            | free heap in bytes                                                                                                                                                     | falling trend over days = leak; Heltec V3 gateway ~125-129 kB, RAK4631 ~104 kB                                  |
| `trk=i/c`         | Trickle HEY interval in seconds / consistent HEYs heard since reset. Interval doubles from 30 s up to 900 s; a topology change resets it to 30 s; suppression at `k=2` | `900/24` on a stable quiet node; permanently `30/0` would suggest topology flapping (inference, not seen)       |
| `fw=40a/20260929` | firmware 4.40a and `FLASH_VERSION` (settings layout / build id)                                                                                                        |                                                                                                                 |
| `up`              | uptime in s; a decrease between two STAT lines means a reboot                                                                                                          |                                                                                                                 |
| `t`               | `millis()`                                                                                                                                                             |                                                                                                                 |

### 3.6 `DM` and `MBOX` (printed right after `STAT`, same tick)

```
00:04:11 [LOG] DM sent=0 echo=0 gwack=0 ack=0 giveup=0 giveuph=0 att=0 reack=0/0 rtt=0/0/0/0/0/0 ring=enq:5 ovw:0
00:04:11 [LOG] MBOX mode=heard used=0/50 act=1/20 stored=1 refr=0 deliv=0 ack=1 dropt=0 dropc=0 drops=0 peer=0 blk=0 sto=1/0
```

`DM` (`dm_stats.cpp:77`, counters documented in `dm_stats.h`): `sent` DMs handed to the ring, `echo` own DM heard relayed, `gwack`
gateway acknowledged, `ack` first `:ackNNN` per DM, `giveup` retries exhausted (`giveuph` of them held in the mailbox), `att`
transmissions including retries, `reack=a/b` duplicate-for-me re-acked / suppressed by the 30 s limiter, `rtt=` six buckets (<15 s, <40 s,
<2 min, <9 min, <30 min, older), `ring=enq:` TX-ring enqueues, `ovw:` enqueues that overwrote a slot parked for retransmit.

`MBOX` (`msgstore.cpp:942`, only when the store-and-forward mode is not off): `used=a/b` slots, `act=n/20` actions in the last hour against the
20/h ceiling, `stored refr deliv ack` lifecycle counters, `dropt dropc drops` dropped by store time / cap / no free slot, `peer`
cancelled because another holder took over, `blk` blocked by the four node-wide gates, `sto=notified/notice_blocked`.

### 3.7 `GWI` and `GWU` (gateways)

```
00:03:22 [LOG] GWI x6AC291FC : H02 from=OE1XAR-33 t=12251795
00:00:56 [LOG] GWU xA183638F @ H02 t=12105725
```

`GWI` (`esp32/udp_frame_esp32.cpp:557`, `nrf52/udp_frame_nrf52.cpp:551`): a frame from the server was queued for the mesh;
`from=` is the server-side sender. `GWU` (`lora_functions.cpp:1606,2300`): a received LoRa frame is uploaded to the server, printed
**before** the relay hop decrement. A frame counted in `STAT newid` but with a `GWI` is not a radio reception.

## 4. Debug families

All `[MC-*]` lines need `--loradebug on` unless noted. Counts are from the 11-hour dk5en-98 log (154,511 lines).

### 4.1 `[MC-DBG]` reception and errors

| Line                                                                                                                       | Emitter                                      | Meaning                                                                                                                                                                                            | Normal / alarming                                                                                                                                  |
| -------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CHANNEL_UTIL rx=1198ms tx=0ms util=11%`                                                                                   | `esp32_main.cpp:2846`, `nrf52_main.cpp:1540` | every 10 s: airtime billed in the window. **Only printed with `--loradebug on`** although the accumulation always runs. `rx=1198` is the airtime of one 115-byte frame (146.25 symbols x 8.192 ms) | `rx` values that are all multiples of 2476 ms = old 255-byte billing (6.1). `util` 4-30 % normal, observed max 61 %; loganalyse flags >= 70 / 90 % |
| `ONRXDONE_TIME ms=91`                                                                                                      | `lora_functions.cpp:950`                     | duration of the receive handler                                                                                                                                                                    | RAK 3-24 ms observed. ESP32 gateway with EXTUDP and server upload: median 57 ms observed (cause not profiled)                                      |
| `ONRXDONE_STATS max=91ms warn=1 (>50ms)`                                                                                   | `esp32_main.cpp:2849`, `nrf52_main.cpp:1543` | per 10 s: slowest handler and count over `ONRXDONE_WARN_MS` = 50                                                                                                                                   |                                                                                                                                                    |
| `[MC-WARN] ONRXDONE_SLOW ms=91 threshold=50`                                                                               | `lora_functions.cpp:2594`                    | one handler exceeded 50 ms                                                                                                                                                                         | 2,362 of 3,973 receptions (59 %) on the observed ESP32 gateway: on that setup the line is background, not an alarm. Cause not investigated here    |
| `RX_FLAG_PROCESS ts=` / `receiveflag`                                                                                      | `esp32_main.cpp:3105`, `:2762`               | main loop saw the radio flag                                                                                                                                                                       |                                                                                                                                                    |
| `RX_RESTART src=timeout state=0` / `RX_RESTARTED src=after_readData\|after_tx\|after_crc_error\|after_other_error state=0` | `esp32_main.cpp:3077,3205,4841,4896`         | radio put back into RX; `state` is the RadioLib return code                                                                                                                                        | `state` other than 0 is a radio fault                                                                                                              |
| `RX_TIMEOUT_FIRE ts= wait= delta=`                                                                                         | `esp32_main.cpp:2972`, `nrf52_main.cpp:1586` | the CSMA/RX timer elapsed. `wait` is the back-off in ms (4570-4780 when idle: the default priority-3 base 4500 plus 0-10 slots of 35 ms), `delta` the measured time                                | `delta` much larger than `wait` = the loop was blocked                                                                                             |
| `RX_TIMEOUT_DEFERRED src=receiveFlag\|header_valid\|preamble_only rssi= cnt=`                                              | `esp32_main.cpp:2983,3015,3022`              | a timer fire was postponed because a packet was arriving (header valid: up to 3 deferrals, preamble only: 1)                                                                                       |                                                                                                                                                    |
| `RX_IRQ_STALE rssi= flags=PRE\|HDR cnt=` / `RX_IRQ_STALE_EARLY`                                                            | `esp32_main.cpp:3028,3008`                   | deferral budget used up (forced restart) / flags set but RSSI below -126 dBm, so stale                                                                                                             | 342 in 11 h observed; harmless unless it climbs                                                                                                    |
| `CRC_ERROR rssi= snr= freq_err=-24.2 size=100 ts=`                                                                         | `esp32_main.cpp:4863`                        | frame failed the radio CRC. `freq_err` in Hz, `size` bytes read                                                                                                                                    | about 6 % of attempts observed. `size=255` on every line = old build, 6.1                                                                          |
| `CRC_PAYLOAD[100]: 40 8F 63 ...`                                                                                           | `esp32_main.cpp:4868`                        | hex of the damaged bytes (max 255). Bit errors anywhere, so no field of a dump is trustworthy, including the `,` and `>` separators                                                                |                                                                                                                                                    |
| `RX_OTHER_ERROR code= rssi= snr= size= ts=` / `ERR_PAYLOAD[n]:`                                                            | `esp32_main.cpp:4900,4907`                   | RadioLib error that is not a CRC error. **Always printed**, also with loradebug off                                                                                                                | any occurrence is worth a look                                                                                                                     |
| `RX_ERROR rssi= snr= ts=`                                                                                                  | `lora_functions.cpp:2675`                    | nRF52 CRC error, no dump                                                                                                                                                                           |                                                                                                                                                    |
| `CHECKRX zero_length`, `CHECKRX missed_edge recovery`                                                                      | `esp32_main.cpp:4814,4771`                   | radio flag with an empty packet / DIO1 edge missed and recovered                                                                                                                                   | 5 `zero_length` in 11 h observed                                                                                                                   |
| `HDR_DETECT cad_attempt=`                                                                                                  | `lora_functions.cpp:3743`                    | header detected while a TX attempt was pending (nRF52 callback)                                                                                                                                    |                                                                                                                                                    |
| `ACK_RECEIVED retid= msg_id=`, `ACK_SKIP_READY`, `ACK_REJECT b5= len=`, `ACK_APPENDIX hash= len=`                          | `lora_functions.cpp:1047,985,511,522`        | ACK processing: own message acknowledged / slot skipped / frame failed the plausibility test (byte 5, `ack_functions.h`) / 4.40 appendix hash                                                      | `ACK_REJECT` b5 values >0x84 are fragments starting with the letter `A`                                                                            |
| `RX_BUF_SWITCH`, `RX_BUF_OVERWRITE buf=`, `CAD_ABORT_BY_RX`, `RX_RESTART_EARLY`                                            | `lora_functions.cpp:859-892`                 | **compiled only with `LORA_ISR_DEBUG`**; absent in release builds                                                                                                                                  | `RX_BUF_OVERWRITE` = RX buffer still in use, frame lost                                                                                            |

### 4.2 `[MC-DBG]` dedup, ring, relay, retransmit

| Line                                                                                                           | Emitter                                      | Meaning                                                                                                                                                                                                                                               |
| -------------------------------------------------------------------------------------------------------------- | -------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `RX_DEDUP_NEW msg_id=`                                                                                         | `dedup_functions.cpp:49`                     | id not in the ring                                                                                                                                                                                                                                    |
| `RX_DEDUP_DUP msg_id= slot=`                                                                                   | `dedup_functions.cpp:36`                     | id found at ring slot                                                                                                                                                                                                                                 |
| `RX_DEDUP_ADD msg_id= srv=1 slot=78/100`                                                                       | `dedup_functions.cpp:64`                     | id stored; `srv` = frame had the server flag; `slot/MAX_DEDUP_RING` (60 ESP32 classic, 100 S3 and RAK, 70 other). The ring being full (`dedup=100/100` in `RING_STATUS`) is the steady state                                                          |
| `RING_STATUS queued= pending= retrying= done= iW= iR= dedup=100/100 dist=`                                     | `esp32_main.cpp:2817`, `nrf52_main.cpp:1491` | every 30 s, TX ring: `queued` occupied slots = `pending` (status READY) + `retrying` (1..0x14 aging counter) + `done` (0xFF, no retransmit); `iW/iR` write and read index; `dist` the old index distance (kept for comparison with logs before BP-02) | `queued` near 20 for several samples = ring saturated (observed max 17) |
| `RING_WRITE slot= type=40 status=FF len= msg_id= queued=0/20 src=auto_pos`                                     | `txring_functions.cpp:872`                   | frame enqueued. `type` = payload type byte in hex (40 `@`, 3A `:`, 21 `!`). `src` observed: `auto_pos`, `nbr_report`, `user_wx`, `rx_relay`, `udp_rx`, `rx_ack_fwd`, `sto`, `mbox`, `phone_msg`, `rx_dm_ack_new`, `rx_dm_ack_gw`                      |
| `RING_PRIO slot= prio=`                                                                                        | `txring_functions.cpp:875`                   | priority class assigned                                                                                                                                                                                                                               |
| `RING_DROP_NEW slot= prio= type= msg_id= (queue full, no lower prio to evict)`                                 | `txring_functions.cpp:882`                   | **the new frame was refused**                                                                                                                                                                                                                         |
| `RING_DROP_PRIO ... replaced_by_prio=`                                                                         | `txring_functions.cpp:878`                   | an older, lower-priority frame was evicted for a higher one                                                                                                                                                                                           |
| `RING_DROP_STALE slot= age_s= msg_id=`                                                                         | `txring_functions.cpp:558`                   | a background-class frame (HEY, neighbour report) waited longer than `RING_BG_MAX_AGE_MS` (3 min) and was dropped                                                                                                                                      |
| `RING_OVERFLOW buf=tx`, `RING_REJECT len= src=`                                                                | `txring_functions.cpp:886,655`               | ring overflow / frame length 0 or above 255 refused (a caller bug)                                                                                                                                                                                    |
| `RING_TX_READ slot= prio= type= status= len= msg_id= retry= queued=1/20 lat=3770ms`                            | `lora_functions.cpp:2811`                    | entry taken for transmission; `lat` = ms since enqueue                                                                                                                                                                                                |
| `RELAY_QUEUED msg_id= type=40 len=82`                                                                          | `lora_functions.cpp:2431`                    | a received frame was queued for relay                                                                                                                                                                                                                 |
| `RELAY_LOOP_BLOCKED own_call_in_path`                                                                          | `lora_functions.cpp:2366`                    | relay refused, own call already in the path                                                                                                                                                                                                           |
| `RETRANSMIT retry=1 after_sec=30 msg_id=`, `RETRANSMIT_GIVEUP retries=3`, `RETRANSMIT_GIVEUP_DM msg_id= dest=` | `lora_functions.cpp:3309,3298,3288`          | DM retry ladder; `GIVEUP_DM` = a user DM was not acknowledged                                                                                                                                                                                         |

### 4.3 `[MC-DBG]` transmit path and CSMA

Canonical sequence for one own HEY (real, abbreviated; every line from the same second block):

```
00:00:41 NEW-HEY 030 @ x1AE1E034 H04 S0 T0 M00 DK5EN-98>HG@R6; HW:43 MOD:8/8 FCS:06DB FW:40:a LH:AB
[MC-DBG] RING_WRITE slot=18 type=40 status=FF len=30 msg_id=1AE1E034 queued=0/20 src=auto_pos
[MC-DBG] RING_PRIO slot=18 prio=5
[MC-TRICKLE] SEND consistent=0<k=2 interval=120000ms neighbors=6
[MC-DBG] RX_TIMEOUT_FIRE ts=12094573 wait=4605 delta=4609      <- back-off elapsed (this is the CSMA wait)
[MC-SM] IDLE -> TX_PREPARE rc=0
[MC-DBG] TX_GATE_ENTER qlen=1 cad_attempt=0
[CHECK] radio.scanChannel() / 1
[MC-DBG] CAD_SCAN result=-15                                    <- -15 = channel free (ESP32)
[MC-DBG] CAD_FREE attempt=0
[MC-DBG] RING_TX_READ slot=18 prio=5 type=40 status=FF len=30 msg_id=1AE1E034 retry=0 queued=1/20 lat=3770ms
[MC-DBG] RADIO_TX len=30 kind=msg
00:00:45 [LOG] TX x1AE1E034 @ H04 prio=5 src=o wait=3770 q=1 cad=0 len=30 t=12094643
00:00:45 TX-LoRa  030 @ x1AE1E034 H04 S0 T0 M01 DK5EN-98>HG@R6; HW:43 MOD:8/8 FCS:06DB FW:40:a LH:AB
[MC-SM] TX_PREPARE -> TX_ACTIVE rc=0
[MC-DBG] TX_START qlen=0
[MC-TEST] TX_FRAME len=30 hex=4034E0E11A14444B35454E2D39383E48474052363B002B8806DB28AB617E
transmittedFlag
[MC-SM] TX_ACTIVE -> TX_DONE rc=0
[MC-DBG] TX_DONE state=0 ts=12095114
OnTXDone
[MC-SM] TX_DONE -> RX_LISTEN rc=0
[MC-DBG] RX_RESTARTED src=after_tx state=0
[MC-DBG] CHANNEL_UTIL rx=0ms tx=454ms util=4%                   <- 30-byte frame: formula gives 461 ms
```

| Line                                                | Emitter                                      | Meaning                                                                                                                                      |
| --------------------------------------------------- | -------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `TX_GATE_ENTER qlen= cad_attempt=`                  | `esp32_main.cpp:3241`, `nrf52_main.cpp:1640` | TX attempt starts; `qlen` ring depth                                                                                                         |
| `CAD_SCAN result=`                                  | `esp32_main.cpp:3284`                        | Channel Activity Detection. ESP32: `-15` free, `-702` busy (`RADIOLIB_CHANNEL_FREE`, `RADIOLIB_LORA_DETECTED`). nRF52: `0` free, `-702` busy |
| `CAD_BUSY_1 attempt=, double-check...`              | `esp32_main.cpp:3295`                        | first scan busy, second scan follows                                                                                                         |
| `CAD_FALSE_POSITIVE`                                | `esp32_main.cpp:3306`                        | second scan free: first was a false alarm, TX proceeds (147 of 207 `CAD_BUSY_1` observed)                                                    |
| `CAD_FREE attempt=`                                 | `esp32_main.cpp:3314`                        | channel free, transmit                                                                                                                       |
| `CAD_BUSY attempt=1 next_timeout=2916`              | `esp32_main.cpp:3371`                        | both scans busy: back-off. `next_timeout` from the table below                                                                               |
| `CAD_FREE_NO_TX restoring RX`, `CAD_SAFETY_TIMEOUT` | `esp32_main.cpp:3348`, `nrf52_main.cpp:1748` | `doTX()` declined / CAD never completed                                                                                                      |
| `IRQ_POLL hdr/pre=0x0200 -> TX_ABORT`               | `esp32_main.cpp:3259`                        | header/preamble IRQ flag seen at the last moment: TX cancelled so a packet is not destroyed                                                  |
| `RADIO_TX len= kind=`                               | `lora_functions.cpp:2758`                    | radio told to send                                                                                                                           |
| `TX_START qlen=`, `TX_DONE state= ts=`              | `esp32_main.cpp:3335,3138`                   | `state=0` = success                                                                                                                          |

CSMA back-off (`csma_timing.h:40-63`, constants `configuration_global.h:388-453`): `base(prio)` = 3000 ms for prio 1 and 2, 4500 for 3, 5500
for 4 and 5; times 5/6 on attempt 1, 2/3 on attempt 2; plus 0-10 random slots of 35 ms; from attempt 3 on the wait is 100 ms
(rapid-fire CAD). Observed `next_timeout` values: 100 for attempt >= 3, 3890 (= 4500 x 5/6 + 4 slots, prio 3 attempt 1). Values such as 2916, 2951 and 150-220 do not fit the table: relay frames with `--nbrrelay on` use a different back-off (`txringCaseBackoffSlot`, `lora_functions.cpp:3783`).

### 4.4 `[MC-SM]` state machine

`[MC-SM] <from> -> <to> rc=<n>`; `rc` is the RadioLib return code, 0 = ok (`esp32_main.cpp:3076-3370`, `lora_functions.cpp:900-3715`).

| Transition                                     | Meaning                                              |
| ---------------------------------------------- | ---------------------------------------------------- |
| `RX_LISTEN -> RX_PROCESS`                      | header detected / receive flag, packet being handled |
| `RX_PROCESS -> RX_LISTEN`                      | handler done                                         |
| `IDLE -> RX_LISTEN`                            | radio restarted after the CSMA timer                 |
| `IDLE -> TX_PREPARE`                           | TX gate entered                                      |
| `TX_PREPARE -> TX_ACTIVE`                      | channel free, sending                                |
| `TX_PREPARE -> IDLE rc=-1`                     | channel busy, back to idle (back-off)                |
| `TX_ACTIVE -> TX_DONE`, `TX_DONE -> RX_LISTEN` | sent; `rc=-1` on the TX_DONE edge = failed send      |

Any `rc` other than the expected one is an error edge (`serial_monitor.py` alerts on it; loganalyse `MC_SM_ERRORS`).

### 4.5 `[MC-TEST]` raw frames

Needs `--loradebug on` (RX) / `--txcapture on` (TX); emitter `capture_functions.cpp:165-188`, printed from the main loop one frame per pass.

```
[MC-TEST] RX_FRAME len=115 rssi=-114 snr=-2 hex=21B851AC3B90444437...
[MC-TEST] TX_FRAME len=30 hex=4034E0E11A14444B35454E2D39383E48474052363B002B8806DB28AB617E
[MC-TEST] CAPTURE_DROPPED n=3 serial_bytes=512
```

`CAPTURE_DROPPED` reports the **increment** since the previous report: `n` frames that did not fit the capture ring, `serial_bytes` bytes
`printfdeb` threw away (nRF52 only). A capture without this line claims completeness it may not have. Cost: about 550 characters per line, about
48 ms of serial time at 115200 baud (`docs/loradebug-serial-output.md`). Observed here: 5,144 frames, 205 characters on average, 404 maximum.

### 4.6 Periodic statistics

| Line                                                                    | Emitter               | Interval         | Meaning                                                                                                                             |
| ----------------------------------------------------------------------- | --------------------- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `[MC-STAT] t=300s qmax=5/20`                                            | `esp32_main.cpp:2882` | 300 s            | window length, TX-ring high water / `MAX_RING`                                                                                      |
| `  tx: p1=0 p2=0 p3=2 p4=0 p5=3 preempt=0`                              | `esp32_main.cpp:2884` | 300 s            | **untagged continuation line (two leading spaces)**: transmissions per priority class and the number of preemptions                 |
| `  drop: p1=0 p2=0 p3=0 p4=0 p5=0`                                      | `esp32_main.cpp:2887` | 300 s            | **untagged continuation**: drops per class (same numbers as `STAT drop=`)                                                           |
| `[MC-PRIO] p5_lat_avg=3010ms p5_lat_max=5101ms p5_cnt=3`                | `esp32_main.cpp:2898` | 300 s            | queue-to-air latency per class that sent something in the window. Single windows observed: `p1_lat_avg=9266ms`, `p5_lat_avg=3010ms` |
| `[MC-HWM] uptime=3600s queue_hwm=3/8 csma_hwm=4 trickle=120000ms`       | `esp32_main.cpp:2916` | 1800 s           | long-term high water marks. **Not printed when `--setlog on`** (`if(!bDisplayLog)`); absent from the 2026-10-05 log                 |
| `[MC-TRICKLE] SEND consistent=0<k=2 interval=120000ms neighbors=6`      | `esp32_main.cpp:4189` | per HEY interval | own HEY sent (fewer than k=2 consistent HEYs heard)                                                                                 |
| `[MC-TRICKLE] SUPPRESS consistent=3>=k=2 interval=240000ms neighbors=6` | `esp32_main.cpp:4182` |                  | own HEY skipped because enough neighbours already said the same                                                                     |
| `[MC-TRICKLE] TOPO_CHANGE neighbors=6->7 interval_reset=30000ms`        | `esp32_main.cpp:4172` |                  | neighbour count changed, interval back to 30 s                                                                                      |
| `[MC-WDT] TX_WATCHDOG fired after 15000ms - forcing RX recovery`        | `esp32_main.cpp:2931` | on event         | TX-done interrupt never arrived. **Always alarming**; also `STAT txfail`                                                            |

### 4.7 Other tagged families seen in the logs

| Line                                                                                       | Emitter                                                                           | Meaning                                                                                                                                                                                    |
| ------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `[LoRa]...Received packet: RSSI: -114 dBm / SNR: -2 dB / Frequency error: 175.34 Hz`       | `esp32_main.cpp:4783`                                                             | per reception (tabs inside). The only line with "-114 dBm" style units                                                                                                                     |
| `[LoRa]...Receive Timeout, startReceive again with sucess`                                 | `esp32_main.cpp:3084`                                                             | RX timer restart (typo "sucess" is in the firmware)                                                                                                                                        |
| `[HEAP] 00:00:00 249432 244284 212980 (init)` / `(mon)`                                    | `esp32_main.cpp:1140`, `loop_actions_esp32.cpp:111`                               | free, minimum free, largest free block in bytes. `(mon)` only when the value changed. **Not printed with `--setlog on`**                                                                   |
| `[KEEP]...KEEP0406B878DK5EN-98 4.40a20;232;262;26244;9;`                                   | `udp_functions.cpp:1406`                                                          | keep-alive to the server: `KEEP` + gateway id + call padded to 9 + 5-char version `4.40a` + group list                                                                                     |
| `[DATA]...DATA0406B878DK5EN-98 4.40a-113  -203`                                            | `udp_functions.cpp:1449`                                                          | data frame to the server with RSSI and SNR (width 4 each)                                                                                                                                  |
| `[BEAT]...Heartbeat from server received`                                                  | `esp32/udp_frame_esp32.cpp:684`                                                   | server liveness                                                                                                                                                                            |
| `[EXT] Out: {...json...} Len: 300`                                                         | `extudp_functions.cpp:785`                                                        | EXTUDP JSON sideband (port 1799). Fields incl. `src_type`, `type`, `src`, `msg_id`, `rssi`, `snr`, `hw_id`, `lora_mod`, `max_hop`, `firmware`, `fw_sub`                                    |
| `[EXT] Tele-Out: ...`, `[EXT];tx;len;300;stack_hwm;4912;ms;...`                            | `extudp_functions.cpp:812,842`                                                    | telemetry JSON; EXTUDP task stack high water                                                                                                                                               |
| `[WIFI];link;up;rssi;-47;bssid;..;chan;3;age_s;..;got_ip_n;1;ip;1;ms;..`, `[ETH];link;...` | `udp_functions.cpp:848-891` (`wifiLinkLog`, WiFi), `nrf52/nrf_eth.cpp` (Ethernet) | 60 s link heartbeat; `;` kept because it is raw `Serial.printf`                                                                                                                            |
| `[NTP];ok;epoch;<s>;rtt;<ms>`, `TimeClient now (UTC): 22:09:18`                            | `ntp_async.cpp:145`, `udp_functions.cpp:974`                                      | time sync                                                                                                                                                                                  |
| `[NBR]\|...`                                                                               | `nbr_matrix.cpp`                                                                  | `--nbrdebug on`; see `docs/nbr-logformat.md`                                                                                                                                               |
| `[BOOT] RESET_REASON=1 POWERON` / `...BOOT RESET_REASON=1 POWERON`                         | `esp32_main.cpp:1108` / `command_functions.cpp:6764`                              | why the node last restarted; `LAST_LOOP_SECTION=` is appended when a loop breadcrumb survived (`loop_breadcrumb.h`). Reasons that mean trouble: `PANIC`, `INT_WDT`, `TASK_WDT`, `BROWNOUT` |
| `[GW];keep;tx;ok;1;ms;<millis>`, `[GW];rx;type;BEAT;len;20;ms;<millis>`                    | `udp_functions.cpp:1414`, `esp32/udp_frame_esp32.cpp:252`                         | gateway-to-server exchange markers (also `[GW];srv;<tld>;host;...`, `udp_functions.cpp:1221`)                                                                                              |

## 5. Decoding a hex dump by hand

### 5.1 Frame layout

```
offset  size  field
0       1     type            21 '!' position | 3A ':' text | 40 '@' HEY/weather | 41 'A' ack
1       4     msg_id          little endian: bytes B1 B2 B3 B4 -> 0xB4B3B2B1
5       1     flags + hops    bit7 server | bit6 track | bit5 app_offline | bit4 mesh | bits3-0 hops left
6       ...   source path     ASCII, comma separated, ends at '>'
...     ...   destination     ASCII, ends at a repeat of the type byte
...     ...   payload         ASCII, ends at 00
        1     HW id           originator hardware
        1     MOD             (country << 4) | modulation
        2     FCS             big endian, sum of ALL preceding bytes, mod 65536
        1     FW version      decimal as one byte (0x28 = 40)
        1     LH              0x80 | hardware id of the transmitter of this copy
        1     FW sub          ASCII letter
        1     7E              end marker
```

Decoder: `decodeAPRS()`, `aprs_functions.cpp:122-540`. Frames shorter than 16 bytes are rejected (ACKs are handled before that check). The FCS is the
sum of bytes `0 .. HW, MOD` inclusive (`aprs_functions.cpp:417-428`).

### 5.2 Worked example: the 115-byte position beacon

`[MC-TEST] RX_FRAME len=115 rssi=-114 snr=-2 hex=` (2026-10-05 00:00:01):

```
21                                  type '!' position
B8 51 AC 3B                         msg_id little endian -> 0x3BAC51B8        (log: x3BAC51B8)
90                                  1001 0000: server=1, track=0, mesh=1, hops=0   (log: H00 S1 T0 M01)
44 44 37 4D 48 2D 35 35 2C          "DD7MH-55,"
44 42 30 4D 4D 52 2D 31 32 2C       "DB0MMR-12,"
44 4C 32 4A 41 2D 32 3E             "DL2JA-2>"     originator DD7MH-55, relays DB0MMR-12 then DL2JA-2
2A 21                               "*" destination, "!" repeat of the type byte
34 38 30 39 2E 39 30 4E ...         "4809.90N/01237.00E...R=9;26386;"
00                                  payload end
27                                  HW 0x27 = 39                              (log: HW:39)
88                                  MOD: country 8, modulation 8              (log: MOD:8/8)
1B CC                               FCS = 0x1BCC                              (log: FCS:1BCC)
28                                  FW 0x28 = 40                              (log: FW:40)
AB                                  LH = 0x80 | 0x2B (43, Heltec V3)          (log: LH:AB)
61                                  FW sub 'a'
7E                                  end marker
```

Check: the sum of the first 109 bytes (type through MOD) is 7116 = 0x1BCC = the FCS in the frame. A damaged frame fails this sum, which is why it
never reaches `MH-LoRa`. The airtime of 115 bytes at SF11, BW250 kHz, CR 4/6, preamble 8 is 1198 ms, and the next `CHANNEL_UTIL` line says
`rx=1198ms`.

### 5.3 Worked example: a relayed HEY (full trace)

Same message through this gateway, from the log (`xA183638F`):

```
RX   00:00:56 MH-LoRa 064 @ xA183638F H02 S0 T0 M01 DF8RD-1,DB0ISM-1,DB0ED-99>HG@R3;8,99,4;9,122,-14; ... LH:AB
     [LOG] ... RSSI:-113 SNR:-2 DUP:n OWN:- t=12105658
UP   [LOG] GWU xA183638F @ H02 t=12105725                       uploaded to the server first
     TX-UDP   073 @ xA183638F H02 S0 ... >HG@R3;8,99,4;9,122,-14;6,113,-2;     (this node's group "6,113,-2;" already appended)
     [LOG] RLY xA183638F @ H02 q=tx prio=5 slot=19              queued for relay
TX   00:01:01 [LOG] TX xA183638F @ H01 prio=5 src=r wait=5101 q=1 cad=0 len=82 t=12110851
     TX-LoRa  082 @ xA183638F H01 S1 T0 M01 DF8RD-1,DB0ISM-1,DB0ED-99,DK5EN-98>HG@R3;8,99,4;9,122,-14;6,113,-2; ... FCS:1292
```

What changed on the relay: hops 2 -> 1; `S0` -> `S1` (this node is a gateway); own call `,DK5EN-98` appended to the path (+9 bytes); one HEY signal group
`6,113,-2;` appended (+9 bytes: neighbour count 6, which matches `STAT mh=6`; -113 dBm shown without sign and SNR -2, the values of this node's RX line); length 64 -> 82; the FCS is
recomputed. A HEY payload `R<n>;` therefore reads: `n` neighbours of the originator, then one `neighbours,|RSSI|,SNR;` group per relay and per gateway that handled it (`11-wire-format.md` 1.4).

### 5.4 Worked example: an ACK

`[MC-TEST] RX_FRAME len=12 rssi=-116 snr=-4 hex=419164F700814303B5030100`

```
41              type 'A'
91 64 F7 00     this ACK's own id        -> 0x00F76491   (log: x00F76491)
81              server flag + 1 hop left                 (log: H81)
43 03 B5 03     acknowledged message id  -> 0x03B50343   (log: x03B50343)
01 00           ack by gateway/node, terminator
```

No path, no FCS, no trailer: the whole frame is 12 bytes.

## 6. Pitfalls

| #    | Trap                                                                                                                                                                                                                                                                                                                                                                                               | How to recognise it                                                                                                                                                                        |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 6.1  | **ESP32 builds before `f2d6532f` (2026-08-25) bill every reception as a 255-byte frame.** `checkRX()` passed a fixed length to the radio read; `CHANNEL_UTIL rx=` and `STAT rx=` are 1.8-4.1 times too high, `CRC_ERROR size=` is always 255 and `CRC_PAYLOAD` carries stack garbage after the real bytes. nRF52 was always correct                                                                | every nonzero `rx=` is a multiple of 2476 ms (PuTTY log: 4952, 2476, 7428, 9904); all `CRC_ERROR size=255`. `tx=` stays correct, a built-in cross-check. loganalyse prints `BILLING_CHECK` |
| 6.2  | **`printfdeb` rewrites the format string** (`src/printfdeb_format.h`): outside `--debug csv` a `;` in the format becomes a space. Raw `Serial.printf` lines keep it. So the same record looks different per path: `[HEAP] 22:11:00 130976 ...` vs `[WIFI];link;up;...`. Parsers must accept `[; ]`                                                                                                 | `[HEAP]` followed by space or `;`                                                                                                                                                          |
| 6.3  | **Doubled percent sign** on old builds: `util=49%%` (fixed in `f2d6532f`)                                                                                                                                                                                                                                                                                                                          | PuTTY log of 23 Aug: `CHANNEL_UTIL rx=4952ms tx=0ms util=49%%`                                                                                                                             |
| 6.4  | **`printfdeb` copies the format string into a 300-byte buffer** (`printfdeb_functions.cpp`, `nformat[300]`); a longer format string is cut, the substituted output is not. `STAT` was kept under the limit on purpose (`setlog_lines.cpp:83` comment)                                                                                                                                              | a line that stops mid-field                                                                                                                                                                |
| 6.5  | **Old-build `CRC_PAYLOAD` lines have no newline**, so the next line is glued to the dump (`... 56 2B [MC-DBG] RX_TIMEOUT_FIRE ...`), fixed later (F2)                                                                                                                                                                                                                                              | PuTTY log                                                                                                                                                                                  |
| 6.6  | **DM sequence number is `{NNN`** with an opening brace and no closing brace; `{ping}` and `{pong}` do have closing braces. An ack is the separate text `:ackNNN`. Retries of a DM flip bits 10-11 of the msg id, so a retry has a different `x...` than the original                                                                                                                               | `...Gute Nacht{123`, `DK5EN-1  :ack400`                                                                                                                                                    |
| 6.7  | **CRC dumps are damaged by definition.** Any byte can be wrong, including `,` and `>`. Counting callsigns in a dump as "senders" overcounts: a four-hop path is one sender. `loganalyse.sh` counts one sender per path head since `d6b1dcd4`; before that its collision share was inflated (73 % vs a real 1.6-2.8 %)                                                                              | `COLLISION_ANALYSIS: unreliable` when all dumps are 255 bytes                                                                                                                              |
| 6.8  | **Capture timestamps are batch times**, section 1.2. Do not compute inter-event gaps from the meshlogger stamps; use `t=`, `ts=`, `wait=`, `lat=`                                                                                                                                                                                                                                                  | many lines with the same millisecond                                                                                                                                                       |
| 6.9  | **Net console is single-client and best effort.** A second login closes the first; the logger reconnects after 5 s and closes yours. Use `touch <outdir>/PAUSE`. Under load the node silently drops bytes when the TCP buffer is full, so a line can be missing or cut                                                                                                                             | `[LOGGER] ... reconnects=3`; `status.txt`                                                                                                                                                  |
| 6.10 | **Opening USB resets an ESP32** and the RAK4631 prints nothing without DTR; native-USB nodes lose the first lines until the port is open. Prefer the 2323 console for ESP32 benches                                                                                                                                                                                                                | boot banner at the start of a capture                                                                                                                                                      |
| 6.11 | **`MH-LoRa:` vs `MH-LoRa `.** Logs before `c78adcc5` (2026-08-03) have a colon after the tag and a different ACK layout; tools must accept both                                                                                                                                                                                                                                                    | grep the first packet line                                                                                                                                                                 |
| 6.12 | **`[LOG]` kinds only exist with `--setlog on`, `[HEAP]`/`[MC-HWM]` vanish with it.** A log from a node with setlog on has no `[MC-HWM]` and no `(mon)` heap lines; read `STAT heap=` instead                                                                                                                                                                                                       | `[MC-HWM]` count 0 although the log runs for hours                                                                                                                                         |
| 6.13 | **`CHANNEL_UTIL` and `ONRXDONE_TIME` need `--loradebug on`.** With only `--setlog on` the 10 s lines are absent while `STAT util=` still works, and so do the STAT `onrx_n/avg/p95/p99/max/warn` fields (OnRxDone time per 5 min window, every exit counted, since `c7cb2c1d`; `onrx_warn` counts handlers over 50 ms). Read OnRxDone health from STAT; `ONRXDONE_TIME` is only for a single frame |                                                                                                                                                                                            |
| 6.14 | **`dedup=100/100` is normal**, the ring is always full once it wraps. `RX_DEDUP_NEW` for an id already seen means the ring wrapped (id evicted); size is settled, do not raise `MAX_DEDUP_RING`                                                                                                                                                                                                    |                                                                                                                                                                                            |
| 6.15 | **Bench rules** for anyone reproducing a trace: test traffic only to group `TEST` or as a DM to an own node, never a foreign callsign, at most 2 dBm; `tools/bench/identity_guard.py` first                                                                                                                                                                                                        | project `CLAUDE.md`                                                                                                                                                                        |

## 7. `tools/loganalyse.sh`

Batch report over a recorded log. Pure bash/awk, no dependencies; about 2.5 s for 20,000 lines.

```
bash tools/loganalyse.sh <logfile>                 # one log
bash tools/loganalyse.sh <log-A> <log-B>           # adds CROSS_CORRELATION for two co-sited nodes
bash tools/loganalyse.sh slice.log > report.txt    # sections are "=== NAME ===" blocks, easy to grep
```

Input: meshlogger / `serial_monitor.py` format (`YYYY-MM-DD hh:mm:ss.mmm  line`) or the raw `[EPOCHDAY.HHh.MMm.SSs.mmm]` format (converted, then
byte-sanitised under `LC_ALL=C`). A capture without timestamps (PuTTY) works: the tool reports `CAPTURE_START` from the header and prints `n/a` for first-seen times (checked on the 23 Aug log). The tool accepts both `MH-LoRa:` and
`MH-LoRa `. With `--setlog` data and no `MH-LoRa` lines (`--info off`) it uses the `[LOG]` RX lines as the packet source. The section list below is
the working tree of 2026-10-12 (uncommitted edits were in progress, so a later version may differ); the claim of "15 sections" in
`docs/loradebug-serial-output.md` is out of date.

| Section                                                                                              | What it tells you                                                                                                                                              | Read it for                                                      |
| ---------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| `OVERVIEW`                                                                                           | boot info, first/last line, `TOTAL_LINES`, `SETLOG: on (n lines)`                                                                                              | sanity: is the log what you think it is, which sources exist     |
| `NODES`                                                                                              | per callsign: count, RSSI range, SNR average, hop values seen, `HW`, `FW`, first seen                                                                          | who you hear, with which board and firmware                      |
| `MESSAGE_TYPES`, `HOP_DISTRIBUTION`                                                                  | type shares; `H0x` histogram separately for `MH-LoRa` / `RX-LoRa2` / `TX-LoRa`                                                                                 | hops left at reception                                           |
| `CHANNEL_UTIL`                                                                                       | from the 10 s lines: avg, max, trend, 10-min buckets, `BILLING_CHECK` (6.1)                                                                                    | congestion and old-build detection                               |
| `CRC_ERRORS`, `CRC_DETAIL`, `CRC_FORENSICS`                                                          | error counts, frequency-error classes (`COLLISION (<1kHz)`, `MEDIUM`, `OFF_FREQUENCY (>3kHz)`), collision estimate from path heads                             | interference; the collision label is a frequency-error heuristic |
| `RING_STATUS`, `RING_OVERFLOW`, `DROPPED_PACKETS`                                                    | queue depth histograms, overflows, drops by class                                                                                                              | TX queue health                                                  |
| `DEDUP`                                                                                              | explicit vs implicit dedup, top repeated ids, copies distribution                                                                                              | how many copies of one frame the mesh produces                   |
| `SETLOG`                                                                                             | `RX_TAIL` (RSSI, SNR, `DUP_RATE`, `OWN_ECHO`), `RLY_REASONS` with `FINDING:` lines, `TX_SPLIT`, `STAT` series, `GATEWAY`, `DEDUP_BALANCE`, `TX_WAIT` per class | the primary section for 4.40a logs                               |
| `STATE_MACHINE`, `CAD_ATTEMPT_DISTRIBUTION`, `CAD_STORM`, `CSMA_TIMING`, `CAD_ENHANCED`              | `[MC-SM]` error edges, CAD attempts, storms                                                                                                                    | access-time problems                                             |
| `PRIORITY_DISTRIBUTION`, `TRICKLE_HEY`, `HIGH_WATER_MARKS`, `HEAP_MONITORING`, `STARVATION_EVENTS`   | priority classes, HEY suppression, high water, heap rating (`n/a` without `(mon)` samples)                                                                     | whether the node is healthy                                      |
| `SERVER_CONNECTION`, `NTP_SYNC`, `ACK_ANALYSIS`, `ACK_STORM`, `MISSING_ACKS`, `BLE_TX_LATENCY`       | gateway link, time sync, ACK flow, retransmit ladder                                                                                                           | delivery problems                                                |
| `SIGNAL_PROFILING`, `FREQUENCY_DRIFT`, `INTERFERER_DETECTION`, `TOP_TALKERS`, `CHANNEL_UTIL_DIAGRAM` | RSSI/SNR profile, frequency error, loudest senders                                                                                                             | RF environment                                                   |
| `CROSS_CORRELATION`                                                                                  | only with two logs                                                                                                                                             | hearing asymmetry between two nodes                              |
| `END`                                                                                                | marker                                                                                                                                                         |                                                                  |

How to read a run, in this order:

1. `OVERVIEW`: dates and `SETLOG` state tell you which sections can be filled; a pre-`f2d6532f` ESP32 log shows its age in `CHANNEL_UTIL BILLING_CHECK`.
2. `SETLOG`/`STAT`: `util`, `err`, `drop=`, `ringmax`, `txfail`, `REBOOTS (up= decreasing)`. Any `FINDING:` line is a rule that fired (`RLY q=full`, `STAT drop p3`).
3. `SETLOG`/`DEDUP_BALANCE`: all four `*_BALANCE: OK` means the counters reconcile (`RX_DEDUP_NEW - GWI = DUP:n`, tolerance max(5, 1 %)); a failure points at a missing
   or double-counted source.
4. `CRC_ERRORS` and `CRC_FORENSICS` only after step 3; trust the collision share only without the `unreliable` flag.
5. `NODES` and `HOP_DISTRIBUTION` last: they describe the neighbourhood, not the node.

Example from the first hour of the dk5en-98 log: `UNIQUE_NODES: 42`, `DUP_RATE: 58.5%`, `RLY_REASONS` led by `q=tx: 159` with `q=full: 13`, `STAT drop p3 = 22`.

## 8. Webapp Monitor: what it does with this data

### 8.1 Data path (established from the code)

```
node ESP32 --EXTUDP JSON (UDP 1799) --+
node       --BLE notifications -------+--> MCProxy / mc-chat --SSE "wire:frame"--> webapp /monitor (decoded rows)
node ESP32 --TCP 2323 console --------------> MCProxy node_console.py -----------^   (DBG rows, link "console")
```

- The webapp never opens serial or 2323 itself. Normal rows come from `wire:frame` envelopes carrying the **decoded frame dict** as `mesh:message` would
  carry it (`webapp:src/types/wireFrame.ts:27-50`, `webapp:docs/rf-monitor-plan.md` "Wire contract v1"). The firmware side is the EXTUDP JSON (section 4.7)
  or BLE; the Monitor never sees raw LoRa bytes, `H/S/T/M`, `FCS`, `LH` or `MOD`.
- Console lines (DBG) exist only with MCProxy (not mc-chat) and only for ESP32 nodes: `MCProxy:src/mcapp/node_console.py` docstring. The session
  logs in with the HMAC handshake, probes `--info`, sets `--loradebug on` and `--txcapture on` unless already on (`FLAG_NAMES`, line 106) and
  restores them at the end with confirmation. It does **not** set `--setlog`. Each line becomes `{type:"con", msg, src:<host>}`; empty lines are
  dropped, lines are cut at 1000 characters (`node_console.py:90,351-361`). The probe's `--info` reply is emitted as DBG rows too.
- `webapp:docs/protocol.md` has no section for `wire:frame`; the contract lives in `rf-monitor-plan.md`.

### 8.2 What is recognised

| Layer                   | Where                                                 | Behaviour                                                                                                                                                                                                                                                       |
| ----------------------- | ----------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Kind of a decoded frame | `wireFrameNormalizer.ts:55-105` (`kindOf`)            | `con`->DBG, `sys`->SYS, `src==='response'`->CMD, `transformer==='mh'`->MH, `msg`->MSG (or ACK when the text is `:ackNNN`), `pos`->POS, `tele`->TEL, `ack`/`msg_status`->ACK, anything else (for example mc-chat `hey`)->SYS with a `type= src= dst=` summary    |
| Fields shown            | `MonitorLine.vue:172-255`                             | time, kind pill, RX/TX, origin pill (`lora`, `node`, `server`, `ble`, `www`), source, relay chain (`via`), destination, text or position/battery/telemetry chips, hardware and firmware badges, RSSI/SNR with signal bars, link badges, `xN` copy count, msg id |
| Fields dropped          | `wireFrame.ts:103-165`                                | `max_hop`, `lora_mod`, FCS, LH, S/T/M flags, `hop` budget: not in `MonitorRow`/`MonitorCopy`. The chat bubble does show `max_hop` (`ChatBubble.vue:527`)                                                                                                        |
| Merging                 | `wireFrameNormalizer.ts:200-330`                      | copies of one frame fold by `KIND:src:msg_id` within 60 min; MSG also by content; MH by station within a fixed 10 min; TEL by readings (60 s). SYS and DBG never merge                                                                                          |
| Verdict labels          | `monitorFormat.ts:215-240`                            | `dropped`/`redirected`/`failed`/`suppressed` with a reason become operator sentences                                                                                                                                                                            |
| DBG lines               | `monitorFormat.ts:401-459`, `MonitorLine.vue:123-141` | one row per console line: time, `DBG` pill, text through `tokenizeConsoleLine`; no expand; italic when MCProxy marked it a session notice                                                                                                                       |
| DBG retention           | `monitorStore.ts:68,223-230`                          | own cap of 500 rows (FIFO), independent of the 5000-row general cap                                                                                                                                                                                             |
| DBG filtering           | `monitorStore.ts:751-760`                             | bypasses the kind chips; only the free-text search applies (matches `text`)                                                                                                                                                                                     |

`tokenizeConsoleLine` is the whole "interpreter" for firmware log text. It recognises:

1. a leading `[TAG]` (`LEADING_TAG_RE = /^\[[^\]]*\]/`, `monitorFormat.ts:403`), only at position 0;
2. then, case-insensitive (`/gi`, line 414): `signal` = `SNR <n>`, `<n> dBm`, `<n> dB`; `rxtx` = the words `RX`/`TX`; `msgid` = any 8 hex digits at word
   boundaries; `callsign` = `[A-Za-z]{1,2}\d[A-Za-z]{1,4}(-\d{1,3})?`.

I ran a copy of these two regular expressions in `node` over real lines of this guide:

| Real line (abbreviated)                                                    | Highlighted                                       | Verdict                                                      |
| -------------------------------------------------------------------------- | ------------------------------------------------- | ------------------------------------------------------------ |
| `[MC-DBG] RX_DEDUP_NEW msg_id=3BAC51B8`                                    | tag, msgid                                        | correct                                                      |
| `[LoRa]...Received packet: RSSI: -114 dBm / SNR: -2 dB / ...`              | tag, two signals                                  | correct (the only such line)                                 |
| `00:00:01 [LOG] RLY x3BAC51B8 ! H00 q=gwcap prio=0 slot=-1`                | nothing                                           | `[LOG]` not recognised: the line starts with the node time   |
| `00:00:01 MH-LoRa 115 ! x3BAC51B8 H00 ... DD7MH-55,DB0MMR-12,... FCS:1BCC` | three callsigns                                   | id `x3BAC51B8` missed; callsigns right                       |
| `00:00:45 TX-LoRa  030 @ x1AE1E034 H04 ... DK5EN-98>HG@R6; ... FCS:06DB`   | `TX` as direction, callsign, **`06DB` as signal** | `FCS:06DB` read as a dB value (case-insensitive `dB`)        |
| `[MC-DBG] CHANNEL_UTIL rx=1198ms tx=0ms util=11%`                          | tag, **`rx` and `tx` as direction**               | field names mistaken for RX/TX                               |
| `[MC-DBG] CRC_ERROR rssi=-69 snr=1 freq_err=-24.2 size=100 ts=12118563`    | tag, **`12118563` as msg id**                     | 8-digit decimal counter; rssi/snr not recognised (`=` form)  |
| `[MC-DBG] RX_TIMEOUT_FIRE ts=12055196 wait=4850 delta=4854`                | tag, **`12055196` as msg id**                     | same; any `ts=` between 2.8 h and 27.8 h uptime has 8 digits |
| `00:04:11 [LOG] STAT util=6 ... fw=40a/20260929 up=12300 t=12300155`       | `rx`, `tx`, **`20260929`**, **`12300155`**        | build id and `t=` as msg ids; `[LOG]` no tag                 |
| `[LOG] 012 A x00F76491 H81 ... RSSI:-116 SNR:-4 DUP:n`                     | **nothing**                                       | `SNR:-4` has a colon, the regex needs whitespace             |
| `  tx: p1=0 p2=0 p3=2 ...` (`[MC-STAT]` continuation)                      | `tx` as direction                                 | it is the TX counter line                                    |

### 8.3 Gaps and misreadings with debug on

| ID  | Gap                                                                                                                                                                                                                                                                                                                                                                                                                                                | Where (webapp)                                                                         | Firmware line it mishandles                                                       |
| --- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| G1  | **No structural decoding of any firmware line.** Packet lines, `[LOG]` kinds, `STAT`, `RLY` reasons, `[MC-*]` fields are plain text with colour on four patterns. Hops, flags, path order, FCS, board and firmware, units and thresholds are not surfaced; the help page has one sentence for DBG                                                                                                                                                  | `MonitorLine.vue:123-141`; `monitorFormat.ts:105`; `monitor-help.html:786-792,985-995` | all of sections 2-4                                                               |
| G2  | **Tag recognition only at column 0.** All packet lines and all `[LOG]` lines begin with the node's `HH:MM:SS`, so their tag is never highlighted and the line family is not visible at a glance                                                                                                                                                                                                                                                    | `monitorFormat.ts:403,435`                                                             | `HH:MM:SS [LOG] ...`, `HH:MM:SS MH-LoRa ...` (`loop_functions.cpp:3364,3479`)     |
| G3  | **Highlighter misses the real forms**: `x<8hex>` ids (no word boundary between `x` and the hex), `RSSI:-114 SNR:-2`, `rssi=-69 snr=1`, `freq_err=`, `H03`, `FCS:`                                                                                                                                                                                                                                                                                  | `monitorFormat.ts:413-414`                                                             | every packet line, `[LOG]` RX tail, `CRC_ERROR`, `RX_FRAME`                       |
| G4  | **False positives**: `rx=`/`tx=` field names as direction; 8-digit decimals (`ts=`, `t=`, `up=`, build id) as msg ids; `FCS:<digits>DB` as a dB reading                                                                                                                                                                                                                                                                                            | `monitorFormat.ts:413-414` (`i` flag, no `=` guard)                                    | `CHANNEL_UTIL`, `STAT`, `RX_TIMEOUT_FIRE`, `CRC_ERROR`, `FCS:06DB`                |
| G5  | **Volume with no control.** 500 rows is about 2 minutes at the 3.9 lines/s of the observed gateway (loradebug + txcapture + setlog); roughly half of all lines are state-machine, timer and utilisation noise (`[MC-SM]`, `RX_TIMEOUT_FIRE`, `RX_RESTART*`, `RX_FLAG_PROCESS`, blank lines, quick tag count, 53 %); a raw frame row of 200-400 characters wraps over several visual lines; there is no per-tag mute or pause, only the text search | `monitorStore.ts:68,751-760`; `MonitorLine.vue:508-513` (`pre-wrap`)                   | `RX_TIMEOUT_FIRE` (6,553 in 11 h), `[MC-SM]` (18,258), `[MC-TEST]` frames (5,144) |
| G6  | **Time column is the backend arrival second.** Milliseconds and the node's own clock and `t=`/`ts=` are not used, yet TX events (CAD to `TX_START`) happen within 80 ms and arrival stamps are batch times (section 1.2)                                                                                                                                                                                                                           | `MonitorLine.vue:135`; `formatters.ts:64-72`                                           | the whole TX block of 4.3                                                         |
| G7  | **Flags and platform are invisible.** DBG sets only `loradebug` and `txcapture`; `[LOG]`/`STAT` appear only if the node already has setlog on, and with setlog on `[HEAP]` and `[MC-HWM]` are suppressed. RAK4631/nRF52 nodes cannot use DBG at all. None of this is stated in the UI or the help                                                                                                                                                  | `MCProxy:node_console.py:106`; `monitor-help.html:990-995`                             | section 1.3 and 1.4                                                               |
| G8  | **Decoded rows drop wire fields** the firmware sends in the EXTUDP JSON (`max_hop`, `lora_mod`), so the Monitor cannot answer "how many hops were left" or "which preset". HEY beacons never appear as `[EXT] Out` in the 11-hour log (only `msg`, `pos`, `tele` types seen), so HEYs are absent from decoded rows unless BLE delivers them                                                                                                        | `wireFrame.ts:103-165`; `wireFrameNormalizer.ts:56-105`                                | `[EXT] Out: {...,"lora_mod":8,"max_hop":0,...}`                                   |
| G9  | **Tests use a line the firmware never prints** (`[MC-DBG] RX DK5EN-98 -117 dBm SNR -8 id=1AE1E057`), so the tokenizer suite stays green while real lines misrender                                                                                                                                                                                                                                                                                 | `webapp:src/components/monitor/__tests__/monitorFormat.spec.ts:182-193`                | -                                                                                 |

### 8.4 Improvement list, prioritised

Recommendations, not facts. Effort: S small, M medium, L large.

| Prio | Change                                                                                                                                                                                                                                                                                                                    | Fixes  | Effort |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | ------ |
| P1   | Make the tokenizer match real lines: allow a leading `HH:MM:SS ` before the tag; add `x[0-9A-F]{8}` ids; accept `RSSI:`/`SNR:` and `rssi=`/`snr=`; make `dB` case-sensitive and require a non-hex left boundary; drop `rx`/`tx` when followed by `=` or `:`; restrict `msgid` to `msg_id=` and the `x` form               | G2-G4  | S      |
| P1   | Replace the synthetic test line with fixtures copied from section 2-4 of this guide (or from `tools/testdata/`), and add the false-positive cases                                                                                                                                                                         | G9     | S      |
| P2   | Pick the line family before colouring: a small table of recognisers for `MH-LoRa`/`RX-LoRa2`/`TX-LoRa`/`RX-UDP`/`TX-UDP`/`NEW-*`, `[LOG]` kinds, `STAT`, `CHANNEL_UTIL`, `CRC_ERROR`, `RING_*`. Render packet lines as chips: hops left, S/T/M, path with the `via` renderer already used for decoded rows, FCS, LH/HW/FW | G1     | M      |
| P2   | Per-family mute / "hide noise" toggle (state machine, timers, restart lines, blank lines) and a pause button; raise or make the 500-row cap adjustable, or add a ring that keeps the last N minutes                                                                                                                       | G5     | M      |
| P3   | Show milliseconds in DBG rows, and optionally the node `HH:MM:SS` / `t=`; keep arrival time as the sort key                                                                                                                                                                                                               | G6     | S      |
| P3   | State the session's flags in the toolbar note (`loradebug, txcapture; setlog is off, so no STAT lines`), and offer an opt-in `--setlog on` for the session (same save/restore path as the other two flags). Say "ESP32 only" in the help                                                                                  | G7     | S-M    |
| P3   | Tooltips from a glossary generated from the emitter tables (field, unit, normal range): `ONRXDONE_SLOW`, `CAD_SCAN` codes per platform, `STAT` fields, `RLY q=` reasons                                                                                                                                                   | G1     | M      |
| P4   | Carry `max_hop` and `lora_mod` through `MonitorRow`/`MonitorCopy` and show "hops left" on RX rows; confirm with the backend owners whether HEY frames can be delivered                                                                                                                                                    | G8     | M      |
| P4   | A "decode" popover for `RX_FRAME`/`TX_FRAME` rows that applies section 5.1 and shows the FCS check                                                                                                                                                                                                                        | G1     | M-L    |
| P5   | Add a DBG chapter to `monitor-help.html` (line families, platform differences, the single-client rule, what the flag restore does)                                                                                                                                                                                        | G1, G7 | S      |

### 8.5 Not established

- How MCProxy splits a `CRC_PAYLOAD` dump that the firmware writes as many small `printfdeb` calls, or whether two console writes can interleave into one row. `_split_lines`
  splits on newline only (`node_console.py:629`); I did not run it.
- Row order for several lines inside one second: rows are sorted by `firstTs` (arrival, ms); tie handling was not checked.
- Whether BLE delivers HEY frames to MCProxy (the EXTUDP JSON does not, per the 11-hour log).
- How the Monitor renders in a browser. All statements come from reading the code and from running the two regular expressions in `node`; I did not open the app.
- The node clock value before any time source is set, beyond the `00:00:00` seen in `[HEAP] ... (init)`.
- Why the ESP32 gateway's `OnRxDone` takes a median of 57 ms (not profiled; the node is a gateway with EXTUDP and server upload).

## Appendix A. Inaccuracies found in existing documents (not fixed)

| Document                                            | Statement                                                                                          | Reality                                                                                                                                                                    |
| --------------------------------------------------- | -------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `docs/loradebug-serial-output.md`                   | `CHANNEL_UTIL` is printed every 10 s "independent of `bLORADEBUG`"                                 | the accumulation is independent, the print is inside `if(bLORADEBUG)` on both platforms (`esp32_main.cpp:2845`, `nrf52_main.cpp:1539`)                                     |
| same                                                | `CAD_SCAN result=0` is free, `-702` busy                                                           | that is the nRF52 synthetic value; ESP32 prints `-15` for free (observed 1,171 times in one log)                                                                           |
| same                                                | typical `wait` 4675 / 3087 / 2087 / 35 ms by attempt                                               | old scheme; current: priority base 3000/3000/4500/5500/5500 x (1, 5/6, 2/3), +0..350 ms, then 100 ms (`csma_timing.h`)                                                     |
| same                                                | lists `ACK_FAST_QUEUED`, `ACK_FAST_TX`, `ACK_RX_CANCEL`, `ACK_FWD_DEDUP`, `GW_ACK_DEDUP`           | no emitter for any of these in `src/`                                                                                                                                      |
| same                                                | `RING_STATUS ... dedup=7/16`                                                                       | format is `dedup=<used>/<MAX_DEDUP_RING> dist=<n>` (MAX 60/70/100 by board); nRF52 omits `dedup=`                                                                          |
| same                                                | `RX_BUF_SWITCH`, `RX_BUF_OVERWRITE`, `CAD_ABORT_BY_RX`, `RX_RESTART_EARLY` as ordinary debug lines | compiled only with `LORA_ISR_DEBUG` (`lora_functions.cpp:858-901`)                                                                                                         |
| same                                                | `[MC-HWM]` every 30 minutes                                                                        | not printed while `--setlog on`; ESP32 only                                                                                                                                |
| same                                                | `loganalyse.sh`: "15 Abschnitte"                                                                   | about 38 sections in the current script (section 7)                                                                                                                        |
| `docs/setlog-instrumentation-impl-plan-20260902.md` | `mh=` is `getMheardCount()`                                                                        | code comment `loop_functions.cpp:3405-3408`: `nbrNcnt()` of the neighbour matrix since the MeshCom 5 work                                                                  |
| `docs/architecture/11-wire-format.md` 1.5           | ACK byte 10: "`0x00` node, `0x01` gateway"                                                         | `ack_functions.h` says `0x01` = ack from GW / node, and all 48 received ACK lines in the 11-hour dk5en-98 log carry `01 00`; the 0x00 case is not backed by what I checked |
