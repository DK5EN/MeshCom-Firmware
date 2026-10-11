# HEY echo upload: gateway pushes its own HG echoes to the server

Status: concept, 2026-10-11. Not implemented. Written for an Opus orchestrator running
`/orchestrate-waves`. Code references are against `fork-dev` at 6d058a5f; upstream `dev` (1a9932e9)
has the same logic at shifted line numbers, so this is an upstream PR candidate.

## 1. BLUF

A gateway never uploads the signal reports that its neighbours append to its own HG HEY. The echo
is recognised as an own frame and only feeds the "heard" status; the server upload sits in the
other branch. Fix: in the own-echo branch, a gateway uploads every HEY echo it hears, unchanged,
with its own RSSI/SNR in the `DATA` header. No new msg_id, no change to the relay or dedup logic,
no self-upload of the bare HG (GW-01 stays).

## 2. Current behaviour (verified by code reading, not on the bench)

| Step                        | Where                                     | What happens                                                                                                                                                                                |
| --------------------------- | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Gateway sends HG            | `src/loop_functions.cpp:5420` `sendHey()` | `'@'` frame to `HG`, payload `R<n>;`, LoRa only. `insertOwnTx(msg_id)` at `:5469`.                                                                                                          |
| No self-upload (GW-01)      | `src/loop_functions.cpp:5478`             | Deliberate: the bare copy (rssi/snr 0) reached the server before the enriched copies of the same msg_id and could win there. BACKLOG 3.8i, measured 2026-08-31. **Keep.**                   |
| Neighbour relays, appends   | `src/lora_functions.cpp:2387`             | `appendHeySignalReport()` adds `NCT,RSSI,SNR;` (format `src/aprs_functions.cpp:1146-1160`), only on the relay path. A server-connected neighbour appends before its own upload (`:2245`).   |
| Gateway hears the echo      | `src/lora_functions.cpp:1054`             | `checkOwnTx(msg_id)` hits; `heardIcheck = icheck` (`:1528`).                                                                                                                                |
| Own-echo branch             | `src/lora_functions.cpp:1543`             | Only HEARD/ACK status work for `MSG_TYPE_TEXT`. A HEY falls through and does nothing.                                                                                                       |
| Upload branch (not reached) | `src/lora_functions.cpp:1588`, `:2246`    | `else if(setlogCountDedup(rx_is_new))` → `if(bGATEWAY && (!msg_server \|\| '@'))` → `addNodeData(RcvBuffer, size, rssi, snr)` at `:2278`. Sibling of the own-echo branch, so never reached. |

Consequence: the link data about the gateway's own neighbourhood only reaches the server if a
neighbour is itself a server-connected gateway, or another gateway hears the neighbour's relay. A
lone gateway among plain nodes contributes nothing about its own links.

Side case: `own_msg_id[]` is a 20-slot ring shared by all own frames (BACKLOG WEB-04). An echo
that arrives after its slot rotated out is treated as a foreign new frame and already goes through
the upload path at `:2246` today (with a second, self-appended report). Out of scope; note it in
the PR, do not fix.

## 3. Decisions (operator, 2026-10-11)

1. **msg_id stays the same.** Every echo is uploaded under the original HG msg_id. Re-IDing would
   break mesh dedup and cause re-flooding. Several server rows per msg_id are intended.
2. **Echoes from further away are uploaded too.** Whatever echo the gateway hears (first hop,
   second hop, ...) goes to the server. No "first echo per neighbour" filter.
3. **GW-01 stays.** The bare HG is still not self-uploaded; the echoes carry the HG content plus
   the reports.

## 4. Target behaviour

In the `heardIcheck >= 0` branch (`src/lora_functions.cpp:1543`), add: when

- `bGATEWAY` is set, and
- `msg_type_b_lora == MSG_TYPE_HEY` (payload `'@'`, destination `HG`), and
- the frame is not HN (already diverted at `:1104`, so implicit), and
- the last hop is not the own call (`aprsmsg.msg_source_last` != `meshcom_settings.node_call`;
  defensive, an echo always carries a relay call)

then call `addNodeData(RcvBuffer, size, rssi, snr)` with the frame **as received**: no report
appended by the gateway (the gateway's own view of the last hop is already in the `DATA` header's
rssi/snr), no re-encode, no `max_hop` change. Log it via the existing `setlogFormatGwu()` line like
the upload at `:2268-2275`, so `--log` shows it.

Do not change: relay decision (the echo is never relayed: own-echo branch, plus the loop check at
`:2331`), dedup ring, `is_new_packet()`, HEARD status, `own_msg_id[]`, GW-01, the HEY format.

Keep the decision testable: put the predicate in a small header-only helper, matching the
`src/hey_policy.h` idiom, e.g. `src/hey_echo_upload.h`:

```cpp
// true: a gateway uploads this own-HEY echo to the server unchanged
inline bool heyEchoShouldUpload(bool isGateway, uint8_t msgType, const char* lastHop, const char* ownCall);
```

The call site in `OnRxDone` stays a few lines.

## 5. Open checks for wave 0 (read-only)

- **Server keeps several rows per msg_id.** The operator decided this is wanted; confirm the
  server does not drop them. `docs/code-quality-2.0.md:1036` records an earlier false "server
  dedups" claim. Check via `mcmap-prod` (`messages_query` / `logs_grep` on a known HG msg_id from
  a node that already has a server-connected neighbour). If the server drops them, stop and report:
  the firmware fix is still right but then needs a server change too.
- **Upload load.** Per own HEY the gateway adds one UDP frame per echo heard. HEY runs on Trickle
  (`docs/hey-supp.md`, ≤ one per 15 min); with ~5 relaying neighbours that is ≤ 5 extra frames per
  15 min. Confirm `udpOutRing` size is not a concern (`src/udp_functions.cpp:1261`).
- **nRF52.** `addNodeData()` and `OnRxDone` are shared code; confirm the RAK (ETH) gateway path
  needs nothing extra (`src/nrf52/udp_drain_nrf52.cpp`).
- **Test harness.** Decide where the regression test lives: a new `native_hey_echo_upload` env
  for the helper (cheap), plus an agreement case in `test/test_udp_frame_twin` (env
  `native_udp_frame_twin`, `platformio.ini:1167`) if that harness can drive `OnRxDone` with
  `bGATEWAY` and capture `addNodeData()`. Existing HEY suites: `test_hey_report` (env
  `native_aprs`), `test_hey_policy`.

## 6. Wave plan

| Wave | Agent(s)                                         | Exclusive files                                                                                                   | Model / effort     | Est. wall-clock |
| ---- | ------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------- | ------------------ | --------------- |
| 0    | 2 scouts, parallel: (a) server rows, (b) harness | none (read-only)                                                                                                  | Haiku 5.5, medium  | ~5 min          |
| 1    | 1 implementer                                    | `src/hey_echo_upload.h` (new), `src/lora_functions.cpp` (call site only), new test dir + its `platformio.ini` env | Sonnet 5.5, medium | ~20 min         |
| 1g   | orchestrator gate + advisor                      | none                                                                                                              | Opus 5.5, medium   | ~20 min         |
| 2    | orchestrator, bench (serialized, single agent)   | none                                                                                                              | Opus 5.5, medium   | ~30 min         |
| 3    | orchestrator: German upstream PR draft           | PR text in scratchpad                                                                                             | Opus 5.5, medium   | ~10 min         |

One writer only: `src/lora_functions.cpp` is a hotspot. **Before wave 1, check `git status`: parallel
sessions work on this tree.** If `src/lora_functions.cpp` is dirty, wait for that work to be
committed; do not dispatch onto a dirty hotspot.

### Wave 1 brief essentials

- Exclusive files as in the table; `platformio.ini` only for adding the one new env block.
- Regression test that fails before and passes after: helper returns true for gateway + HEY +
  foreign last hop; false for non-gateway, for `MSG_TYPE_TEXT`/`POSITION`, for last hop == own
  call. If the twin harness allows it: an agreement case "gateway hears own HG echo → exactly one
  `addNodeData()` with the unchanged frame; non-gateway → none; the relay ring stays empty".
- Verification: `pio test -e <new env>` and `-e native_aprs -f test_hey_report`, never bare
  `pio test`. One `pio` process at a time.
- Prove it is in the artifact: `pio run -e heltec_wifi_lora_32_V3` and `-e wiscore_rak4631`, then
  check the call site is not behind a guard that compiles it out (string-scan for the setlog `GWU`
  marker path or a symbol check).
- No git, no flashing, no transmitting.

### Gate (orchestrator)

`tools/regression.sh --stage 1,2`, read the diff, advisor pass (`fable-review` advisor mode, Opus
5.5 medium) against section 4 of this file.

### Wave 2 bench

- Gateway: DK5EN-92 (T-Beam) or DK5EN-1 (Heltec V3), `--gateway on`, server connected.
  Neighbour: another bench node, `--gateway off`, mesh on. Resolve ports by USB serial, confirm
  callsigns on the start pages, run `tools/bench/identity_guard.py`, TX power ≤ 2 dBm.
- Trigger `--sendhey` on the gateway (HEY is not a test message to a group, but keep the bench
  rules: no foreign callsigns).
- Expect in the 2323 console / `meshlogger`: own HG TX, echo RX from the neighbour, one `GWU`
  upload line for the echo with the same msg_id.
- Expect on the server (`mcmap-prod` `logs_grep` on the msg_id): a row from the gateway carrying
  the neighbour's `NCT,RSSI,SNR;` group. Before the fix: none.

## 7. Upstream PR

Minimal diff: helper header, a few lines in `OnRxDone`, the test. German description: what
changed (`OnRxDone`, own-echo branch, new helper), why (the gateway's own link reports never
reached the server; GW-01 removed the only other path), what stays unchanged (msg_id, relay, dedup,
GW-01). Target upstream `dev`; the maintainers merge.
