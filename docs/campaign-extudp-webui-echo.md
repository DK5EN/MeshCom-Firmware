# Campaign UDP-05 -- EXTUDP messages missing from the local WebUI chat

**Status:** W1 and W2 done 2026-10-30 (`2b1a9dc4`); W3 done: upstream [#1244](https://github.com/icssw-org/MeshCom-Firmware/pull/1244) (branch `pr/extudp-webui-echo` from `upstream/dev` 10203967, worktree `../MeshCom-PR-extudp`, helper + call site only; builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam`, `t_deck_plus`, `wiscore_rak4631`, `ttgo-lora32-v21` green). DK5EN/map#3 answered and closed. Open: maintainer review, then `/sync-upstream` (merge) and worktree cleanup. Source: DK5EN/map#3 (filed in the wrong repo; it is a
firmware change). Spec: operator note `extudp-webui-echo.md` (Desktop), reproduced in short below.

## BLUF

- **Bug:** a message sent through EXTUDP (JSON on UDP 1799) goes out on LoRa but never shows in the node's own WebUI
  chat, because `sendMessage()` echoes into the phone ring only when `hasMsgFromPhone` is set.
- **Fix:** echo the outgoing frame into the phone ring for `ORIGIN_EXTUDP` as well. The gateway "server reached" ack
  frame stays phone/WebUI-only (recommended, see decision D1).
- **Shape:** one small writer wave, gate with advisor, one serialized bench wave, then an upstream PR from
  `upstream/dev`.

## Root cause (verified on `72cc7f33`)

| Where                                                                          | What                                                                                                                                        |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/loop_functions.cpp:4757`                                                  | `if(hasMsgFromPhone) { addBLEOutBuffer(msg_buffer, ...); <gateway ack frame> }` -- the only echo into the phone ring                        |
| `src/web_functions/web_functions.cpp:4696`                                     | WebUI send sets `hasMsgFromPhone = true` and `ORIGIN_WEB`                                                                                   |
| `src/esp32/esp32_main.cpp:4465`, `nrf52/nrf52_main.cpp:1893`                   | BLE app send sets `ORIGIN_BLE` (`hasMsgFromPhone` set by `phone_commands.cpp`)                                                              |
| `src/extudp_functions.cpp:415-419`                                             | `getExtern()` sets `ORIGIN_EXTUDP` around `sendMessage()`, never `hasMsgFromPhone`                                                          |
| `src/esp32/serial_command_esp32.cpp:138`, `nrf52/serial_command_nrf52.cpp:117` | serial `::` send sets `ORIGIN_SERIAL`; already echoed by `sendMessage()`'s `bConsoleText` branch (`loop_functions.cpp:4825`) -- no gap (D2) |
| `src/web_functions/web_functions.cpp:3875`                                     | WebUI Messages page renders `phoneRing` -- no echo, nothing to render                                                                       |

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
  - `ORIGIN_SERIAL`, not from phone: echo no (pins the no-double-echo rule, D2).
  - Before-run: the helper first lands with today's semantics (`return fromPhone;`), the suite runs and fails on the
    EXTUDP case; output pasted in the report. Then the fix, suite passes.
- **Call-site integrity:** `grep -c "phoneEchoWanted" src/loop_functions.cpp` = 1; the ack frame stays under
  `hasMsgFromPhone` (advisor checks the diff).
- **Gate:** stage 1,2 PASS with the new suite counted; three board builds green.
- **Bench (W2):** EXTUDP JSON to UDP 1799 of the bench node shows in the WebUI Messages page within one refresh; a
  WebUI-typed message appears exactly once; a relayed frame from another node does not appear as own.

## Results

- **W1:** orchestrator inline. Advisor REWORK (serial already echoed via `bConsoleText`), applied, re-review
  APPROVED. Gate stage 1,2: 83 envs 3084/3084; golden selftest 48 green after the `variant-ini-effective` baseline
  gained the new suite; builds `ttgo-lora32-v21`, `heltec_wifi_lora_32_V3`, `wiscore_rak4631` green.
- **W2 (DK5EN-1, INSTRUMENT build of `2b1a9dc4`, USB flash, 2 dBm, identity guard ok):** `--extudpip <Mac>` and
  `--extudp on` (the node refuses `--extudp on` without a peer IP). `{"type":"msg","dst":"9999",...}` to UDP 1799
  showed once as an own message (`DK5EN-1>9999`, later with the server tick) after one Update click. A message typed
  in the WebUI to 9999 showed exactly once, also after a further Update. Frames from other stations relayed through
  the gateway still showed as received. EXTUDP restored to off, peer none.

## Bench rules for W2

- `/bench` loaded, `tools/bench/identity_guard.py` run, `lsof` before any port, TX power at most 2 dBm.
- Destination per D3; never broadcast, never a foreign callsign, never DK5EN-98.

## Decisions (operator)

- **D1 ack frame:** echo only, ack stays phone/WebUI-only. _Decided 2026-10-30._
- **D2 `ORIGIN_SERIAL`:** operator said cover it; the advisor then showed serial `::` sends are already echoed (`bConsoleText`), so the spec's gap does not exist and covering it would double the echo. Left out; the test pins `ORIGIN_SERIAL` = no echo.
- **D3 bench node and destination:** DK5EN-1, 2 dBm; destination TEST vs. 9999 open (`CheckGroup()` accepts digits only, so `{TEST}` is a PN in the firmware).
- **Q4 W1 dispatch:** orchestrator inline. _Decided._
- **Q5 DK5EN/map#3:** reply and close after the upstream PR is up. _Approved._
