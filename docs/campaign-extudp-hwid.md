# Campaign: hw_id / lora_mod / max_hop on Extern-UDP frames

Resume point for the wave campaign started 2026-09-18. Handover:
`docs/2026-09-16_firmware-extudp-hw-id-on-text-frames.md`. Branch `fork-main`, base
`0138eaeb` (upstream/dev merged, ini-only delta).

## Decisions (operator, 2026-09-18)

- Header helper `src/extern_msg_json.h` + native test (pattern `extern_tele_json.h`), not an
  inline three-liner.
- Same three keys on the `0x21` pos branch (`hw_id` already there, add `lora_mod`, `max_hop`).
- `lora_mod` masked `& 0x0F` in firmware.
- mc-chat parity: **moot**. mc-chat has no Extern-UDP emitter; `meshcom_mock/bridge.py` builds
  webapp-shaped dicts from the binary gate protocol and already carries all three keys.
- MCProxy: verify only. `storage/ingest.py:1508-1510` reads `hw_id`/`lora_mod`/`max_hop`
  straight from the UDP dict, no mask on the UDP path, no type validation. No proxy change.
- Hardware: bench on DK5EN-93 (Heltec V3, `/dev/cu.usbserial-0001`, configured as callsign
  `DK5EN-1`, mesh off, gateway off, extudp on to 192.168.68.58), then OTA flash of DK5EN-98
  (feeds mcapp.local) and a popover check on a group the node is not subscribed to.

## Golden "before" datagram (DK5EN-93, build Sep 18 2026 13:24, own-node text to group 9)

```
{"src_type":"node","type":"msg","src":"DK5EN-1","dst":"9","msg":"golden before hw_id","msg_id":"EA25A384","firmware":"4.35","fw_sub":"t","rssi":0,"snr":0}
```

## Waves

| Wave | Owner | Files | Status |
| ---- | ----- | ----- | ------ |
| 0 | orchestrator | merge upstream, scout, golden capture, this doc | DONE |
| 1 | implementer A | `src/extern_msg_json.h` (new), `test/test_extern_msg_json/` (new), `platformio.ini` env:native filter line | DONE |
| 1 gate | orchestrator | `src/extudp_functions.cpp` (0x3A -> helper, 0x21 + two keys), native suites (835/835, 13 envs), clean Heltec + RAK builds, ELF string scan, advisor (REWORK: buffer 500->700, air bound 636 B), commit | DONE |
| 2 | orchestrator, hardware | bench DK5EN-93 PASS (`docs/bench-extudp-regression.md` §9); DK5EN-98 OTA to build Sep 18 22:11 done; mcapp.local row for an unsubscribed group | bench DONE, popover row open |
| 3 | orchestrator | client wire doc, release-notes, BACKLOG, close this doc | open |

## Gate facts (wave 1)

- Buffer: the 150-char/9-char case measures 530 B, the air-side bound (encodeAPRS 239 B
  `src>dst:payload`, 5-char src, dst `*`, 231 quote characters) measures 636 B. `c_json` is
  `EXTERN_MSG_JSON_BUF` = 700 (was 500). Both pinned in `test/test_extern_msg_json`.
- Sizes (clean builds): Heltec V3 RAM 129436 B / Flash 1498357 B; RAK4631 RAM 102140 B
  (+104 B vs the 500 buffer) / Flash 696920 B.
- mcapp.local "before" evidence: 2026-09-18 20:01:13 UTC, DL1UDO-12 -> 26299, src_type lora,
  hw_id/lora_mod/max_hop NULL. DK5EN-98 flashed 20:13 UTC.
