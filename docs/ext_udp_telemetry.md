# Extern-UDP interface extension: external sensor values in the position beacon

Extends the existing JSON UDP interface (port `1799`, `src/extudp_functions.cpp`)
with a new incoming message type `"tele"`, which lets an external client
supply sensor values for a node that has **no physical sensor hardware of
its own**. The values are written directly into the node's
sensor variables (`meshcom_settings.node_temp`, `node_hum`, `node_press`, ...)
and therefore flow automatically into the next position beacon — exactly like
a node with real sensor hardware (e.g. `/T=23.9/H=34.3/Q=1013.6` in the APRS
comment). To any receiving station (and any monitor/dashboard), they are
indistinguishable from genuine sensor data.

Implemented in `src/extudp_functions.cpp`, function `handleExternTelemetry()`
(called from `getExtern()`).

> **History note:** An earlier version of this extension sent the values as a
> separate APRS telemetry packet (`T#...`, destination address `100001`). That
> was dropped because common monitors/dashboards (demonstrably including the
> MeshCom firmware itself via `sendExtern()`) only ever evaluate the sensor
> fields embedded in the position beacon, never standalone `T#` packets. This
> version therefore writes directly into the sensor variables instead.

---

## 1. Message format (client → node)

UDP packet to the node, port `1799`, JSON payload — all fields optional, only
the fields present are applied:

```json
{
  "type": "tele",
  "temp": 23.3,
  "hum": 60,
  "press": 1018.5,
  "temp2": 0,
  "qnh": 1018.5,
  "gasres": 0,
  "co2": 0
}
```

| Field    | Target variable                   | APRS comment field in `PositionToAPRS()` |
| -------- | --------------------------------- | ---------------------------------------- |
| `temp`   | `meshcom_settings.node_temp`      | `/T=`                                    |
| `hum`    | `meshcom_settings.node_hum`       | `/H=`                                    |
| `press`  | `meshcom_settings.node_press`     | `/P=`                                    |
| `temp2`  | `meshcom_settings.node_temp2`     | `/O=`                                    |
| `qnh`    | `meshcom_settings.node_press_asl` | `/Q=`                                    |
| `gasres` | `meshcom_settings.node_gas_res`   | `/G=`                                    |
| `co2`    | `meshcom_settings.node_co2`       | `/C=`                                    |

There is no channel-name/unit field anymore — the meaning of each value is
fixed by its APRS comment field (identical to real sensor hardware).

### Example

```json
{ "type": "tele", "temp": 23.3, "hum": 60, "press": 1018.5 }
```

Sets `node_temp=23.3`, `node_hum=60`, `node_press=1018.5`. `temp2`/`qnh`/`gasres`/`co2`
remain unchanged (no value in the JSON = no change to that variable).

---

## 2. What the node does afterwards

1. **Immediate position beacon:** The node immediately triggers a position
   beacon (`sendPosition(0x9999, ...)`, the same mechanism as the `--sendpos`
   console command), instead of waiting for the next scheduled beacon
   (default every 30 minutes, `POSINFO_INTERVAL`).
2. **On-air format:** The values appear as part of the normal `!` position
   packet, e.g.
   `DH1FR-2>*!5051.09N/00906.45E#.../B=100/A=000827/P=1018.5/H=60.0/T=23.3/N5/R=...`
   — no separate telemetry packet, no `PARM.`/`UNIT.` header lines needed.
3. **Persistent, not one-shot:** Unlike the earlier `"T:"` version, the values
   are **not** reset after being sent — they stay set and flow into **every**
   subsequent position beacon (including the regular ones every 30 minutes)
   until overwritten by a new `"tele"` message or the node restarts.

---

## 3. Limitations (please note in client implementations)

- **Protects real sensor hardware.** If the node detects physically installed
  sensor hardware (`bmx_found`/`bmp3_found`/`aht20_found`/`sht21_found` —
  BME280/BMP3xx/AHT20/SHT21), the external message is **completely ignored**
  and logged (`"tele ignored: real sensor hardware detected on this node"`).
  This prevents an injection from colliding with the values of real, onboard
  sensor hardware — on a node with no sensor hardware at all (the intended
  use case for this extension), this guard does not trigger.
- **No acknowledgement over UDP.** The protocol is "fire and forget" — there
  is no response/ACK on port 1799. Success or rejection is only visible in
  the node's serial log (`[EXT] tele accepted: ...` resp.
  `[EXT] tele ignored: ...` / `[EXT] tele missing recognized fields ...`).
- **No sender validation.** Like the existing `dst`/`msg` message, this
  channel is reachable by any device on the same LAN (no IP allowlist). No
  new security risk compared to the status quo, but no additional protection
  either.
- **No free-form channel-name field anymore.** Values that don't correspond
  to one of the 7 fixed fields (e.g. rainfall) currently cannot be
  transmitted via this interface — `PositionToAPRS()` has no matching comment
  field for them.

---

## 4. Error cases (log messages on the node)

| Log message                                                                  | Cause                                                             |
| ---------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| `[EXT] tele ignored: real sensor hardware detected on this node`             | Node detected real sensor hardware — external values are rejected |
| `[EXT] tele missing recognized fields (temp/hum/press/temp2/qnh/gasres/co2)` | None of the known fields were present in the JSON                 |

---

## 5. Unchanged behavior

- The existing `{"type":"msg","dst":"...","msg":"..."}` message keeps working
  unchanged (separate code path, untouched).
- The outgoing `sendExtern()` telemetry (`"type":"tele"` from the node to the
  client, on received position packets) is a separate, unchanged mechanism.
  After a successful injection it will automatically reflect the new values
  (since it now reads the real `node_temp`/`node_hum`/... variables).
- Nodes that never receive a `"tele"` message behave exactly as before — the
  new code only becomes active when a matching UDP message actually arrives.
- `--gateway`/`--track` settings are **not** relevant to this mechanism
  (unlike the earlier `"T:"` version) — the position-beacon code path
  (`sendPosition()`) always places packets in the local LoRa TX buffer
  regardless of those settings.

---

## 6. Outgoing `tele` datagram (node → client)

For every position frame the node forwards to the Extern-UDP peer it also sends a
`{"type":"tele",...}` datagram, built in `src/extern_tele_json.h`. Both shapes carry the
same keys with the same physical meaning:

| Key            | `src_type:"node"` (own sensor)    | `src_type:"lora"` (relayed node) | Unit / Q-group                                    |
| -------------- | --------------------------------- | -------------------------------- | ------------------------------------------------- |
| `qfe`          | `meshcom_settings.node_press`     | APRS `/P=`                       | hPa, station pressure (QFE)                       |
| `qnh`          | `meshcom_settings.node_press_asl` | APRS `/Q=`                       | hPa, reduced to MSL per ISA (QNH)                 |
| `pressure_alt` | not present                       | APRS `/F=`                       | m, pressure altitude vs 1013.25                   |
| `din`          | own MCP23017 port A               | APRS `/D=`                       | 8-char bit string GPA0..GPA7, '0' for output pins |

`din` is omitted entirely (no key, not an empty string) when the sender has no MCP23017.

**Fixed 2026-09-05 (TLM-04):** firmware up to and including 4.35p wrote the `/F=` value under
`qfe` for `src_type:"lora"`, so a relayed BME680 node (which always sends `/F=` and suppresses
`/Q=`) showed its pressure altitude in metres as "hPa" on a dashboard, e.g. `"qfe":191`. A client
that talks to a gateway on older firmware can recognise the case by `src_type:"lora"`,
`qnh` = 0 and a `qfe` below about 850. Since the fix `qfe` is the `/P=` pressure and the altitude
moved to its own key.

---

## 7. Outbound datagram type contract (node → client)

Sections 1–6 above document only the **inbound** direction (`"msg"`, `"tele"`). Everything the
node sends back out on port `1799` also carries a `"type"` key, but until now that outbound
contract had no single written answer — this section is it (DR-18,
`docs/testplan/drift-matrix.csv`).

| `type`   | Built in                                                                 | Sent from                                                                                                                                                              | Trigger                                                                                         |
| -------- | ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `"pos"`  | `sendExtern()` (`src/extudp_functions.cpp`, 0x21 branch)                 | `sendExtern()` directly, or deferred via `queueExtern()`                                                                                                               | A GATE frame carrying a position (`msg_type_b`/`msg_type_b_lora` 0x21) reaches EXTUDP           |
| `"msg"`  | `sendExtern()` (0x3A branch)                                             | same                                                                                                                                                                   | A GATE frame carrying text (0x3A) reaches EXTUDP                                                |
| `"tele"` | `externTeleJsonNode()`/`externTeleJsonLora()` (`src/extern_tele_json.h`) | `sendExtern()`, alongside a `"pos"` datagram, for `src_type "node"`/`"lora"`                                                                                           | See §6 above                                                                                    |
| `"ack"`  | `buildExternAckJson()` (`src/udp_frame.h`)                               | `queueExternAck()`/`sendExternJson()` (`src/extudp_functions.cpp`), deferred through the same ring buffer as `queueExtern()`, NOT through `sendExtern()`'s type switch | A UDP GATE-relayed text frame carrying `:ack`/`:rej` triggers the BLE ack echo (both platforms) |

`"ack"` is the DR-18 part 2 addition (implemented 2026-09-17, wave W6):

```json
{
  "type": "ack",
  "msg_id": "1A2B3C4D",
  "status": 1,
  "from": "OE1XYZ-12",
  "via": "udp"
}
```

`status` matches `ack_attribution.h`'s BLE ack status byte verbatim (0 Node ACK, 1
Gateway/Server, 2 Peer ACK). Full contract, including why it is a separate sender rather than a
widened `sendExtern()` type switch, and what is and is not covered yet:
`docs/ack-wer-hat-quittiert.md` §6.3.

Every outbound type above is sent unauthenticated to whatever IP `meshcom_settings.node_extern`
resolves to — same "no sender/receiver validation" caveat as §3 for the inbound side.
