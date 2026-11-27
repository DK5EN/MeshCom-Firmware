# Campaign FORMAL-02: verifiable parsers, settings round trip, BLE, web server, mesh model

Resume point of the campaign. Current state, next wave and open questions only; finished waves move to the
archive section at the end with one line each.

## Operator decisions (2026-11-27)

- Order: FORMAL01-SEAMS (parsers verifiable) and settings round trip first, then BLE task vs main loop, then NimBLE and
  the production web server, then the mesh model (relay, dedup, ACK across nodes).
- Power loss during an update: deferred, not important now (bricking is not the main risk for us).
- UDP receive: the two platform copies (`src/esp32/udp_frame_esp32.cpp`, `src/nrf52/udp_frame_nrf52.cpp`) are merged
  into one; high priority. Duplicated code contradicts DRY, maintainability, modularity, determinism.
- Phone commands: the BLE buffer size is negotiated by the stack and then fixed; reading past it caused overflows that
  made the BLE link unstable. Every read gets an explicit length.
- Web server: the production web server (`src/web_functions/web_functions.cpp`, own HTTP server) is the target, not
  safeboot. Safeboot (AsyncWebServer) is deferred, low priority.
- Upstream: all refactors go upstream. Splitting functions so they become testable is approved.
- Settings migration: every version since the keyed store (2026-10-01) plus the old nRF52 blob must load cleanly; older
  may fall back to defaults.
- Library bugs (NimBLE, AsyncWebServer): fix by pre-script patch at the pinned version, approved.
- Commit permission for the waves on `fork-dev`. Bench test only at the end of the code work. No RF, no flashing.

## Waves

| Wave | Content                                                                                                                                 | State       |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| 1    | Seams: `handleACK` core, `readPhoneCommand` with explicit length, settings round trip + migration; UDP twin analysis, web_functions map | done        |
| 1b   | UDP receive consolidated into `src/udp_frame.cpp` (part 1 `f4f541d5`; part 2 applies operator decisions D1/D2/D3/D5)                    | done        |
| 2    | Bounded proofs: ACK core and phone frame done (`ce013112`); UDP parse core after 1b part 2                                              | partly done |
| 3    | BLE host task vs main loop model `specs/tla/bletask.tla`; fixed BLE-QDROP-01 and BLE-STALE-01 (wave 3b)                                 | done        |
| 4    | NimBLE boundary and library; production web server seams and proofs                                                                     | open        |
| 5    | Mesh model: relay, dedup ring, ACK and retries across nodes                                                                             | open        |

## Wave 5 input: asymmetric nodes (operator 2026-11-27)

- Model the node that "talks a lot, is heard a lot, but is nearly deaf": a 1 W or 2 W PA (E22) on a long, thin coax,
  so what it hears is 10 to 20 dB weaker than what the other side hears from it. Such nodes are rough and must not
  become super nodes that relay first and fast. Measurement documents (a German node, an HB node) are the input;
  Found: HB9ZG-49 (`docs/log-reports/2026-11-01-24h/HB9ZG-49-Empfangspruefung.html`, verification `verify/hb9zg49.md`):
  heard by 24 stations, hears 9; partners reach it at -105..-119 dBm (SNR -0.4..-8 dB), it reaches them at -125..-127
  dBm (SNR -12.6..-20 dB); gap 12..15 dB on the strong links, control pairs 0.2..5.9 dB (median 3.7). German side:
  only the Freising pairs (`docs/archive/verdict-auto-via-routing-20260916.de.md` section 3.2, 5..12 dB, no PA); the
  German 1-2 W node document is not in the repo yet (asked the operator). Related design note: mheard measures who I
  hear, not who hears me (`docs/adr-nc-importance-backoff.md`).
- The mesh model (relay, dedup, ACK and retries across nodes) therefore needs asymmetric links (A hears B, B does not
  hear A, or only with a large margin loss) and must show what such a node does to relay choice, ACK delivery and
  retries, and which firmware rule keeps it from dominating.

## Rules for this campaign

- Writers run `pio` only through `/tmp/formal02/piolock.sh` (one pio at a time), never bare `pio test`.
- Bounded proof and TLC runs only by the orchestrator, one at a time, 8 GB default, up to 15 GB.
- Every refactor is identical to a frozen copy of the old code (differential host test) except where a defect is fixed;
  each fixed defect has a regression test red before, green after.

## Wave 4 input: production web server map (scout, 2026-11-27, unverified line refs)

- `work_webpage` (`web_functions.cpp:1039`, 509 lines): request loop and router, ~40 `indexOf` branches over the whole
  header (a Referer can match a route; see `web_guard.h:44`). Request line via String `indexOf`/`substring` (:1113).
- Unguarded parsing: `Content-Length` `toInt()` without range check (:1478); `call_function` (:5031) and `setparam`
  (:5106) `substring(indexOf(...)+n, ...)` without `-1` checks; `send_message` (:4875); `web_setup.cpp` `sscanf`/`atoi`
  (:563, :578, :764, :852, :868).
- Duplicates: route `mcp23017` twice (:1443, :1448); body readers `web_read_body` (:619) vs `rmReadBody`
  (`web_rm_handlers.cpp` ~45); three percent decoders (`url_decode.cpp`, `web_rm_parse.h:51`, `web_rm_util.h:31`); five
  hex decoders; ~25 `send_http_header` + page pairs.
- Blocking: `delay(2000)` in `sub_config_upload` (:942), spectrum sweep `delay(50)` per step in the HTTP handler
  (:4108); 929 `client.print*` calls.
- Out of place: spectrum sweep, GPS, GPIO, Ethernet MSS register write, AU status, message store purge in the web file.
- No host test covers `web_functions.cpp`, `web_setup.cpp`, `web_rm_handlers.cpp`. Already pure and tested:
  `web_guard.h`, `web_rm_parse.h`, `web_txbuf`, `web_auth`, `url_decode`.

## Operator decisions for the UDP merge (2026-11-27)

- D3: nRF gateways show server DM/group text and answer `{pong}`/`{MCP}`, like ESP32 (the `if(!bGATEWAY)` guard goes).
- D2: option A, a server position (0x21) enters the dedup ring at once on receipt, like ESP32.
- D1: every GATE frame from the server stamps server liveness, like nRF.
- D5: datagrams under 4 bytes are class OTHER; a 4-byte GATE frame carries no type.
- Not asked, taken: UDP gets its own receive buffer (no shared LoRa `RcvBuffer`, D6); entry points renamed at once.

## Carried items (from the advisor passes)

- UDP differential: narrow the D4 strip (only a text GATE frame to a group or `*` with `:ack` and `{`) and the D3
  strip (only the one extra display call, keep comparing `display_dest_path`) in `test/test_udp_frame_diff`.
- UDP: printfdeb is a no-op natively, so trace changes (C10, C13, new `bDEBUG` lines on ESP32) are checked by reading
  only.
- BLE: on nRF52 with an exhausted heap Bluefruit runs the write callback inline and could overtake a queued connect
  callback (false stale drop); not reproduced. `test_ble_queue_sites.py` checks that the generation bump is called,
  not that it is the first statement.
- Old nRF52 UDP handler crashed (negative memcpy length) on "GAT" plus a stale byte; fixed by D5 in the merge.

## Open questions

## Archive

- Wave 1 (2026-11-27): ACK core, phone frame decoder with explicit length, shared id lookups, settings migration and
  round-trip proof jobs, nan/inf settings reject; advisor rework applied (harness newline loop, fixture generator
  frozen and month-proof, BLE stress fake node, empty nRF BLE queue items). Gate `--changed` PASS (132 envs, 4,311
  cases). App bug #1343.
