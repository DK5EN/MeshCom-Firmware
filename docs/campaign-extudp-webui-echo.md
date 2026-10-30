# Campaign UDP-05 -- EXTUDP messages missing from the local WebUI chat

**Status:** plan, awaiting operator approval (2026-10-30). Source: DK5EN/map#3 (filed in the wrong repo; it is a
firmware change). Spec: operator note `extudp-webui-echo.md` (Desktop), reproduced in short below.

## BLUF

- **Bug:** a message sent through EXTUDP (JSON on UDP 1799) goes out on LoRa but never shows in the node's own WebUI
  chat, because `sendMessage()` echoes into the phone ring only when `hasMsgFromPhone` is set.
- **Fix:** echo the outgoing frame into the phone ring for `ORIGIN_EXTUDP` as well. The gateway "server reached" ack
  frame stays phone/WebUI-only (recommended, see decision D1).
- **Shape:** one small writer wave, gate with advisor, one serialized bench wave, then an upstream PR from
  `upstream/dev`.

## Root cause (verified on `72cc7f33`)

| Where                                                                          | What                                                                                                                 |
| ------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------- |
| `src/loop_functions.cpp:4757`                                                  | `if(hasMsgFromPhone) { addBLEOutBuffer(msg_buffer, ...); <gateway ack frame> }` -- the only echo into the phone ring |
| `src/web_functions/web_functions.cpp:4696`                                     | WebUI send sets `hasMsgFromPhone = true` and `ORIGIN_WEB`                                                            |
| `src/esp32/esp32_main.cpp:4465`, `nrf52/nrf52_main.cpp:1893`                   | BLE app send sets `ORIGIN_BLE` (`hasMsgFromPhone` set by `phone_commands.cpp`)                                       |
| `src/extudp_functions.cpp:415-419`                                             | `getExtern()` sets `ORIGIN_EXTUDP` around `sendMessage()`, never `hasMsgFromPhone`                                   |
| `src/esp32/serial_command_esp32.cpp:138`, `nrf52/serial_command_nrf52.cpp:117` | serial `::` send sets `ORIGIN_SERIAL`, never `hasMsgFromPhone` -- same gap (decision D2)                             |
| `src/web_functions/web_functions.cpp:3875`                                     | WebUI Messages page renders `phoneRing` -- no echo, nothing to render                                                |

`bp_origin` (`getMsgOrigin()`, `loop_functions.cpp:4067`) is set by the caller immediately before `sendMessage()` and
cleared right after; relays, ACKs and beacons never set it, so an origin-keyed echo cannot fire for them.

## Design

- New pure inline helper next to `enum MsgOrigin` in `src/backpressure.h`, so the decision is host-testable without
  linking `loop_functions.cpp` (no native suite links `sendMessage()`; `test_getextern` stubs it):
  - `phoneEchoWanted(bool fromPhone, MsgOrigin origin)` -- true for `fromPhone`, or `origin == ORIGIN_EXTUDP` (plus
    `ORIGIN_SERIAL` if D2 = yes).
  - The ack frame keeps its own condition `hasMsgFromPhone` (D1 = echo only).
- `sendMessage()` call site (`loop_functions.cpp:4756-4772`): split the block -- the echo under `phoneEchoWanted()`,
  the gateway ack under `hasMsgFromPhone` as today.
- No double echo: WebUI and BLE set `hasMsgFromPhone`, so their path is unchanged (one `addBLEOutBuffer` call).

### Side effects, stated in the commit

- **BLE app** connected at the time also shows the EXTUDP message (shared ring). Desired.
- **Ring pressure:** `MAX_RING` is 20 (10 on one board class), shared with positions and acks. A chatty automation
  pushes older frames out sooner; the browser's own `mcHistory` (cap 200) hides this until a page reload. Accepted.

## Waves

| Wave | Work                                                                                                                    | Who                                   | Model / effort            | Est. cost | Est. wall-clock |
| ---- | ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------- | ------------------------- | --------- | --------------- |
| W0   | Recon and this plan                                                                                                     | orchestrator                          | Opus 5.5 medium           | done      | done            |
| W1   | Helper, call site, regression test (fail before, pass after)                                                            | 1 `implementer` (or orchestrator, Q4) | Sonnet 5.5 medium         | ~$1-2     | ~15 min         |
| W1g  | Gate: `tools/regression.sh --stage 1,2`; builds `ttgo-lora32-v21`, `heltec_wifi_lora_32_V3`, `wiscore_rak4631`; advisor | orchestrator + 1 advisor              | Opus 5.5 medium (advisor) | ~$2       | ~40 min         |
| W2   | Bench: flash one node, EXTUDP send, WebUI check, WebUI-typed message renders once                                       | orchestrator, serialized              | Opus 5.5 medium           | ~$1       | ~25 min         |
| W2c  | Docs (this file, RESUME, BACKLOG, defect catalogue 2m), commit on `fork-dev`                                            | orchestrator                          | --                        | small     | ~10 min         |
| W3   | Upstream PR via `/submit-pr`: branch `pr/extudp-webui-echo` from `upstream/dev`, helper + call site only, German body   | orchestrator                          | Opus 5.5 medium           | ~$1-2     | ~30 min         |

Total roughly $6-9 and 2-2.5 h wall-clock, dominated by the stage 1,2 gate and the bench wave.

## File ownership

| File                                                     | Owner (W1)             | Change                                         |
| -------------------------------------------------------- | ---------------------- | ---------------------------------------------- |
| `src/backpressure.h`                                     | implementer            | add `phoneEchoWanted()` after `enum MsgOrigin` |
| `src/loop_functions.cpp` (lines ~4756-4772 only)         | implementer            | split echo / ack condition                     |
| `test/test_phone_echo_origin/test_phone_echo_origin.cpp` | implementer (new)      | Unity suite, see acceptance                    |
| `platformio.ini` (`[env:native]` `test_filter`)          | orchestrator (hotspot) | register the new suite                         |
| `docs/*`                                                 | orchestrator           | W2c                                            |

Shared resources: the `native` build tree and the one `pio` process belong to the implementer during W1 (only
`pio test -e native -f test_phone_echo_origin`, through `piolock.sh`); the full gate, board builds and the bench node
belong to the orchestrator afterwards.

## Acceptance

- **W1 regression test** `test_phone_echo_origin` pins:
  - `ORIGIN_EXTUDP`, not from phone: echo yes.
  - `ORIGIN_NONE`, not from phone (relay, beacon, ACK): echo no.
  - `fromPhone` with `ORIGIN_WEB` / `ORIGIN_BLE`: echo yes (unchanged).
  - `ORIGIN_GUI`: echo no (T-Deck has its own `tdeck_add_MSG`).
  - `ORIGIN_SERIAL`: per D2.
  - Before-run: the helper first lands with today's semantics (`return fromPhone;`), the suite runs and fails on the
    EXTUDP case; output pasted in the report. Then the fix, suite passes.
- **Call-site integrity:** `grep -c "phoneEchoWanted" src/loop_functions.cpp` = 1; the ack frame stays under
  `hasMsgFromPhone` (advisor checks the diff).
- **Gate:** stage 1,2 PASS with the new suite counted; three board builds green.
- **Bench (W2):** EXTUDP JSON to UDP 1799 of the bench node shows in the WebUI Messages page within one refresh; a
  WebUI-typed message appears exactly once; a relayed frame from another node does not appear as own.

## Bench rules for W2

- `/bench` loaded, `tools/bench/identity_guard.py` run, `lsof` before any port, TX power at most 2 dBm.
- Destination per D3; never broadcast, never a foreign callsign, never DK5EN-98.

## Decisions (operator)

- **D1 ack frame:** echo only, ack stays phone/WebUI-only -- recommended. _Open._
- **D2 `ORIGIN_SERIAL`:** cover in the same change or leave out of scope. _Open._
- **D3 bench node and destination.** _Open._
- **Q4 W1 dispatch:** implementer agent or orchestrator inline (~15 changed lines). _Open._
- **Q5 DK5EN/map#3 reply and close** after the upstream PR is up. _Open._
