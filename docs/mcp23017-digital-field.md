# MCP23017 digital inputs on the air: the `/D=` field and the `T#` digital slot

Status 2026-09-11: implemented in fork commit `b179fdff`, changelog item 207,
BACKLOG `TLM-05`, and **upstream in PR #1135 (section 6), merged 2026-09-10** — the standalone
German PR text that was prepared for it is archived unused
([`archive/pr-draft-mcp17-din-20260906.md`](archive/pr-draft-mcp17-din-20260906.md)). Still open on
`TLM-05`: the transmit path has never run on hardware (no MCP23017 on this bench); server and app
support are not ours.

This document is the reference for what a node sends, in which order, why the
letter `D` was chosen, and what a receiver may assume. Read it before
touching any of the three places that produce or consume the string.

## 1. The request, and the two places it lands

Upstream issue [1076](https://github.com/icssw-org/MeshCom-Firmware/issues/1076)
(DB1MIC, 2026-08-03): the APRS telemetry frame already carries a digital
status byte, but it is always empty; map inputs onto it, for example port A
of an MCP23017. A second request from the field asked for the same eight
bits inside the position beacon, so they reach the MeshCom server and the
apps without the APRS telemetry detour.

Before this change `loopMCP23017()` (`src/io_functions.cpp`) read all 16 pins
every 5 s into `meshcom_settings.node_mcp17in`, and nothing consumed the
value except `--io`, `--info` and the web GUI. The `T#` builder in
`sendTelemetry()` wrote the digital byte as the literal `00000000`.

Both wishes are served from one source string:

| Place                         | Where it is built                              | Wire form                       |
| ----------------------------- | ---------------------------------------------- | ------------------------------- |
| Position beacon, sensor tail  | `PositionToAPRS()` in `src/loop_functions.cpp` | `/D=01001100`                   |
| APRS `T#` frame, digital slot | `sendTelemetry()` in `src/loop_functions.cpp`  | `T#123,v1,v2,v3,v4,v5,01001100` |

## 2. The string

`src/mcp17_bits.h`, one Arduino-free `static inline` function:

```
void mcp17PortABits(uint16_t in, uint16_t io_mask, char out[9])
```

- `in` is `meshcom_settings.node_mcp17in` as the driver fills it (bit n is
  pin n; the driver only sets bits for pins it read as INPUT).
- `io_mask` is `meshcom_settings.node_mcp17io` (bit set means the pin is
  configured as OUTPUT by `--setio`).
- `out` receives exactly eight characters `0` or `1` plus a NUL.

Rules, in order of precedence:

1. **Order:** `out[0]` is GPA0, `out[7]` is GPA7. This is the APRS `BITS`
   convention (bit 1 first) so that the `T#` slot and the beacon field carry
   the identical string and nobody has to explain two orders.
2. **Output pins read `0`,** whatever `in` says. A stale input word after a
   `--setio` change cannot leak into a frame.
3. **Port B is not sent.** Bits 8 to 15 of `in` are ignored. A later extension
   is either a second letter or a sixteen-character string; both are
   backward compatible with the eight-character parser below.
4. **Presence, not configuration, decides whether the field is sent.** The
   sender checks `bMCP23017`, which `setupMCP23017()` sets only when
   `begin_I2C(0x20)` succeeded at boot. There is no runtime switch: a new
   setting would have meant a `FLASH_STRUCT_VERSION` bump on nRF52 and with
   it a configuration wipe of every RAK in the field.

With the MCP23017 inputs in `INPUT_PULLUP`, an open input reads `1` and a
contact to ground reads `0`. The string reports the raw pin level; there is
no inversion and no `BITS.` sense line to say otherwise (see 4.2).

## 3. Why `/D=` and not `/I=`

The field request proposed `/I=11111111`. `/I=` is taken: `PositionToAPRS()`
sends the INA226 bus current in amperes as `/I=` inside the `/V=5` block
(`src/loop_functions.cpp`, the `bINA226ON` branch). A bit string there would
have broken every parser that reads the current today, including the MeshCom
server.

Letters in use in the beacon sensor tail at the time of the decision:

| Letter | Meaning           | Letter | Meaning              |
| ------ | ----------------- | ------ | -------------------- |
| `A`    | altitude          | `O`    | OneWire temperature  |
| `B`    | battery percent   | `P`    | station pressure     |
| `C`    | CO2               | `Q`    | QNH                  |
| `F`    | pressure altitude | `R`    | group-call list      |
| `G`    | gas resistance    | `T`    | temperature          |
| `H`    | humidity          | `U`    | INA226 bus voltage   |
| `I`    | INA226 current    | `V`    | sensor block version |
| `N`    | MHeard count      | `Y`    | telemetry flag       |

Free were `D`, `E`, `J`, `K`, `L`, `M`, `S`, `W`, `X`, `Z`. `D` for "digital"
is the only one with an obvious mnemonic and no history in any log parser or
document of this repository (`tools/`, `docs/`, the `loganalyse` skill). The
value is fixed width, so a receiver can length-check it, and a parser that
does not know the letter skips it like any other unknown token (see 4.1).

## 4. Sender details

### 4.1 Position beacon

`PositionToAPRS()` fills a `cdigital[15]` buffer with `/D=` plus the string
when `bMCP23017` is true, outside the `node_parm` / `bINA226ON` branch so it
does not depend on the INA state. In the bounded `strncat` chain that
assembles the sensor tail it sits directly after `/I=` and before `/Y=`.
The tail buffer is 100 bytes; the field costs 11. A node with every sensor,
an INA226 and a long group list can already exceed the budget today, in
which case the last fields are dropped in chain order; `/D=` is second to
last, `/Y=` last. That is unchanged behaviour for a longer tail, not a new
truncation.

Nodes without the chip produce a byte-identical beacon.

### 4.2 `T#` telemetry frame

`sendTelemetry()` builds the string once at the top of the
`iNextTelemetry >= 4` branch, preset to `00000000` and overwritten when the
chip is present. There are two builder paths:

- **`T:` path** (values supplied with a `T:` prefix in `node_values`): the
  literal `,00000000,` becomes `,<bits>,`. Same length, same slots.
- **List path** (`node_values` as a comma list): this path never sent a
  digital byte. With the chip present it now pads the analog slots to five
  (`,0`) and then appends `,<bits>`. The padding matters: APRS reads the
  digital byte from slot 6, so a node with three configured values would
  otherwise ship its bits as analog value 4. Without the chip the frame is
  byte-identical to before.

The `BITS.` definition line is untouched. Per the APRS specification it
carries the bit sense (which level counts as active) and a project title,
not values; it is only sent on the SOFTSER app path anyway.

## 5. Receiver and export

### 5.1 `decodeAPRSPOS()`

`struct aprsPosition` gained `char din[9]`, initialised to `""` by
`initAPRSPOS()`, which `decodeAPRSPOS()` calls itself, so every decoded
position has a defined value. A new token loop in `src/aprs_functions.cpp`,
shaped like the existing `/P=` and `/H=` loops, searches `/D=` from the
start of the comment and copies until `/`, a space or the end of the payload:

- Accepted only if the token is exactly eight characters, each `0` or `1`.
- Seven or nine characters, a letter, or an empty token leave `din` empty.
- The ninth and later data bytes set an overflow flag instead of being
  copied, so a long garbage token cannot overrun `decode_text[25]`.

Older firmware ignores `/D=` because each of its token loops looks only for
its own letter. No `/V=` version bump is needed.

### 5.2 EXTUDP `tele` datagram

`externTeleJsonNode()` and `externTeleJsonLora()` in
`src/extern_tele_json.h` take a trailing `const char *din`. The key `din` is
written only when the string is non-empty; senders without an MCP23017
produce byte-identical JSON. `sendExtern()` passes the node's own string for
`src_type "node"` and `aprspos.din` for `src_type "lora"`. The contract is in
`ext_udp_telemetry.md` section 6.

This is the only consumer of parsed sensor fields in the firmware. The phone
app does not receive foreign sensor fields today, so nothing was added
there. The MeshCom server and the official app do not know `/D=`; making
them learn it is what the upstream PR is for.

## 6. What is verified, and how

| Layer                               | Evidence                                                                                                                                                                                                     |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Formatter                           | `test/test_mcp17_bits`, 8 cases: order, output mask, port B ignored, mixed patterns                                                                                                                          |
| Parser                              | `test/test_decodeaprspos`, +6 cases: present, absent, 7 and 9 chars, letter, last token, empty comment                                                                                                       |
| EXTUDP builder                      | `test/test_extern_tele_json`, +5 cases: key present and absent for both shapes, `nullptr` safe                                                                                                               |
| Whole native envs                   | `native` 296/296, `native_parsers` 36/36, `native_extern` 32/32                                                                                                                                              |
| Builds                              | `heltec_wifi_lora_32_V3`, `wiscore_rak4631`, `E22_XML-DevKitC`, all clean                                                                                                                                    |
| Receive and export path on hardware | `DK5EN-93`: corpus frame f003 with `/D=01001100` inserted and FCS recomputed, fed through `--injectraw`; the UDP listener received `"din":"01001100"`; a real on-air frame in the same minute carried no key |
| Transmit path on hardware           | **not done**, no MCP23017 on the bench                                                                                                                                                                       |

The bench recipe (frame crafting, the `[BOOT];ready` timing trap, listener
setup) is in the project memory note `injectraw-extudp-bench-recipe`.

Whoever has the chip: leave one port A pin as input with `--setio`, pull it
to ground, wait for the next beacon; `/D=` must show `0` at that position and
`1` at the open pins.

## 7. Deliberately not done

- **No beacon on pin change.** The bits ride the regular beacon interval;
  air time stays predictable. If a use case needs it, it is a separate
  decision with its own rate limit.
- **No port B.** See rule 3 in section 2.
- **No on/off setting.** See rule 4 in section 2.
- **No change to `/I=`, `/V=`, the web GUI or the phone protocol.**

## 8. Open before the upstream merge

- The bit order is a convention, not a necessity. If the server parser
  wants GPA7 first, it must be said before firmware ships the field; after
  that it is in the field for good.
- Transmit-side hardware proof (section 6).
