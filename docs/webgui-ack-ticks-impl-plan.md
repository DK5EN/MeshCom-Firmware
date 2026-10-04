# Web GUI delivery ticks — implementation plan (2026-10-04)

Source: `docs/webgui-ack-ticks-verdict-20261004.md` (Findings 1-3, plan B1-B4). Fork only, no
upstream PR. Bench: DK5EN-92 (T-Beam) + DK5EN-14 (T-Deck Plus); DK5EN-1 was offline, operator
approved the swap.

## Wave status log

- Wave 1 (B1 module, B2 setter + 9 sites, B3 + B4 web): done 2026-10-04
- Advisor (Fable): one must-fix — late ACK on a recycled ring slot never reached the table (held DMs
  are acked hours later). Fixed at all four ACK sites (UDP path only for acks addressed to us),
  twin case `test_regression_server_late_ack_upgrades_status_row_on_both` red before, green after.
  Nice-to-haves taken: vacuous churn test dropped, dead `MAX_RING` fallback removed, suite map
  updated. Also: the twin harness needed a `setOwnMsgStatus` stub (link error in the first gate).
- Gate: done — `tools/regression.sh --stage 1,2` PASS (48 envs, 1698/1698 Unity cases, golden,
  stage 2); heltec_v3, ttgo_tbeam, t_deck_plus, wiscore_rak4631 build clean, no warnings in touched
  files, `mcTickKeep`/`mctick`/`ownMsgStatus*` present in every image.
- Bench: done — viewer DK5EN-92, DK5EN-14 pings it (each pong takes a ring slot on -92, nothing
  enters its message list), DMs between own nodes, 2 dBm, identity guard before and after OTA.
  Old image (16:29 build): ACK tick after 10 s, lost after 22 pongs with the DM still listed. New
  image (17:52 build): ACK tick unchanged after 25 pongs. `tools/webgui_tick_test.js` all pass on
  both nodes. Note: `PING_MAX` is 5, a first run with `--pingmax 22` sent only 5 pings and was void.

- Wave 2 (received-tick proof on a gateway, 2026-10-04 evening): done. Before = DK5EN-98
  (production gateway, 4.40a): 7 of 9 received messages carried a tick. After = DK5EN-92 (new
  image) as temporary gateway (`--gateway on`, `--setinfo on`, both off again), DK5EN-14 relaying:
  three server-forwarded DM3KS-12 messages `0x2efb0088..8a` were heard back (checkOwnTx hit, ring
  state 01) and render without tick on -92; the same ids on -98 render with a tick. Eyeballed and
  screenshotted in Chrome (`~/Desktop/web04-evidence/`). Script lesson: the gateway logs
  `[GW];rx`, not `[UDP]`.

## Fixed API (all agents code against this)

`src/own_msg_status.h` / `.cpp` (pure, native-testable, no Arduino includes):

- `OWN_MSG_STATUS_SLOTS` — `MAX_RING` when defined, else 20
- `void ownMsgStatusReset(void)`
- `void ownMsgStatusRegister(uint32_t msg_id)` — own text sent; state 0x00; round-robin; 0 ignored;
  re-register of a resident id resets nothing
- `void ownMsgStatusSet(uint32_t msg_id, uint8_t state)` — no-op for unknown ids; rank-monotone
- `int ownMsgStatusGet(uint32_t msg_id)` — -1 unknown, else the state
- `uint8_t ownMsgStatusRank(uint8_t state)` — 0x00→0, 0x01→1, 0x04→2, 0x03→3, 0x02→4, else 0

`void setOwnMsgStatus(int idx, uint8_t state)` (`loop_functions.cpp/.h`): writes
`own_msg_id[idx][4]`, calls `ownMsgStatusSet`, and on T-Deck/T-Deck Plus calls
`tdeck_set_msg_status` when the ring state changed. `sendMessage()` calls `ownMsgStatusRegister`
next to its `insertOwnTx`.

## Ownership (wave 1)

| Agent | Files                                                                                                                                        |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| W1-A  | `src/own_msg_status.h`, `src/own_msg_status.cpp`, `test/test_own_msg_status/test_main.cpp`                                                   |
| W1-B  | `src/loop_functions.cpp`, `src/loop_functions.h`, `src/lora_functions.cpp`, `src/esp32/udp_frame_esp32.cpp`, `src/nrf52/udp_frame_nrf52.cpp` |
| W1-C  | `src/web_functions/web_functions.cpp`, `tools/webgui_tick_test.js`                                                                           |
| orch  | `platformio.ini` (env:native registration), `test/golden/native/variant-ini-effective.json`, docs                                            |
