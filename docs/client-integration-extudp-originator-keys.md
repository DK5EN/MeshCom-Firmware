# Client integration: originator keys on Extern-UDP `msg` and `pos` datagrams

For MCProxy, mc-chat and anyone consuming the node's Extern-UDP JSON (port 1799). Firmware
state: fork-main `6cdfe4f0` (2026-09-18), changelog item 225. Handover with the analysis:
`docs/2026-09-16_firmware-extudp-hw-id-on-text-frames.md`.

## 1. What changed on the wire

| Datagram | Before                                                                   | Now                                                            |
| -------- | ------------------------------------------------------------------------ | -------------------------------------------------------------- |
| `msg`    | `src_type type src dst msg msg_id firmware fw_sub rssi snr`              | same ten keys, same order, then `hw_id`, `lora_mod`, `max_hop` |
| `pos`    | `... aprs_symbol aprs_symbol_group hw_id msg_id alt [batt] firmware ...` | `lora_mod` and `max_hop` inserted after `alt`                  |

All three are JSON integers. There is no protocol version field on this link; detect the
keys by presence. An older node keeps omitting them.

| Key        | Meaning                                                                                                     | Range |
| ---------- | ----------------------------------------------------------------------------------------------------------- | ----- |
| `hw_id`    | Board id of the **originator** (`BOARD_HARDWARE` of the node that created the frame, from the epilogue)     | 0-255 |
| `lora_mod` | Modulation of the originator, low nibble of the epilogue mod byte; the country index is **not** included    | 0-15  |
| `max_hop`  | Remaining hop budget of **this copy** of the frame (the received value, before this node's relay decrement) | 0-15  |

Provenance is the same as `firmware`: originator, not last hop. The BLE copy of the same
frame carries the same three values under the same names; `mesh_info`, `last_hw_id` and the
path flags remain BLE-only.

## 2. Captured datagrams (DK5EN-93, build 2026-09-18)

Received text (injected corpus frame, epilogue `hw 0x2B mod 0x88`, hop nibble 4):

```
{"src_type":"lora","type":"msg","src":"DK5EN-98","dst":"9999","msg":"Korpus-Testframe via MCProxy DK5EN-98","msg_id":"EEFFC09A","firmware":35,"fw_sub":"p","rssi":-50,"snr":10,"hw_id":43,"lora_mod":8,"max_hop":4}
```

Own text (`src_type` `node`; note `firmware` stays a string here, an integer for `lora`):

```
{"src_type":"node","type":"msg","src":"DK5EN-1","dst":"9","msg":"after hw_id","msg_id":"EA25A328","firmware":"4.35","fw_sub":"t","rssi":0,"snr":0,"hw_id":43,"lora_mod":8,"max_hop":2}
```

Received position (on air, DC2MAC-1 via DL2JA-2):

```
{"src_type":"lora","type":"pos","src":"DC2MAC-1,DL2JA-2","msg":"","lat":48.207,"lat_dir":"N","long":12.0593,"long_dir":"E","aprs_symbol":"-","aprs_symbol_group":"/","hw_id":12,"msg_id":"D0167118","alt":1686,"lora_mod":8,"max_hop":1,"batt":100,"firmware":35,"fw_sub":"p","rssi":-127,"snr":-19}
```

## 3. Datagram size

The worst legal `msg` datagram is 636 bytes (5-character source, `*` destination, 231
quote characters, every number at its widest): the node's buffer is 700, so consumers must
accept UDP payloads above 512 bytes. Anything that still assumes 500 (an old `c_json`
mirror, a fixed receive buffer) truncates and loses the frame.

## 4. Per-client checklist

- **MCProxy:** nothing. `store_message` (`src/mcapp/storage/ingest.py`) already reads
  `hw_id`, `lora_mod`, `max_hop` from the UDP dict; there is no mask on the UDP path, and the
  firmware delivers the nibble already. Verified on mcapp.local 2026-09-18: the first
  lora-only text row after the flash stores `43 / 8 / 2` from the datagram, and the popover
  derives `Hardware` and `Max hops` from those columns.
- **mc-chat:** nothing. It has no Extern-UDP emitter; its mock speaks the binary gate
  protocol and its webapp-shaped dicts already carry the three keys.
- **Anyone else:** treat the keys as optional integers; never infer `hw_id` from the sender's
  last position beacon when the key is absent (that presents another frame's hardware).
