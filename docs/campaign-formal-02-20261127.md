# Campaign FORMAL-02: verifiable parsers, settings round trip, BLE, web server, mesh model

Resume point of the campaign. Current state, next wave and open questions only; finished waves move to the
archive section at the end with one line each.

## Now (2026-11-27, resume here)

**State:** waves 1-5 committed and pushed (`fork-dev`, last code commit `d8aa2576`), working tree clean, `fork-dev`
contains all of `upstream/dev` (checked 2026-11-27). Start a fresh session with `/orchestrate-waves` on this doc;
the next wave is 6.

- **Results so far:** meshrelay 15 of 15 cfgs as expected (today fails DeafNotFirst and RedundantRelayBound, the
  candidate rule holds both). `nimble_gatts_write` 7 of 7, `nimble_attvalue` 6 of 6. `url_decode` LEGACY 24 B (3 s),
  STRICT 20 B (113 s). Last gate `--changed`: 4,360 of 4,362 cases, 2 skipped, board links, ruff, pytest, formal
  self-test green. Advisor on web wave A: ship, rework applied.
- **Rig status:** `test/test_web_diff` is a baseline snapshot of the frozen request path (`frozen/`, sha256 in
  `SHA256SUMS`) until wave 6 wires the live entry (TODO `web-diff-live` at the top of `test_web_diff.cpp`). Decoder
  equivalence rests on `test_url_decode`, `test_rm_web_parse`, `test_hex_digit`. Corpus files keep CRLF
  (`.gitattributes`).
- **Waiting on the operator:** relay candidate rule (RELAY-ORDER-01): firmware change only after a yes.

### Next waves (dispatch plan)

Plan source for the web steps: `docs/web-server-seams-20261127.md` 6.2-6.8 (design) and 7.3 (steps). B1-B10 are
accepted (operator 2026-11-27); each B change gets its own commit and a counted `kWhitelist` row in the rig.

| Wave | Content                                                                                                                                                                                                                                                                                                                            | Owner files (writer)                                                                                                                                   | Parallel with                                  |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------- |
| 6a   | Web step 3: `web_request.h` (`WebHeadAcc`, `webHeadPush`, `webReqParse`, explicit lengths, constant loop bounds, allocated once at server start, not on the loop stack); `work_webpage` head loop uses it; router and guard unchanged; live entry of the rig wired, frozen vs live must agree on all 1,247 requests (no whitelist) | `src/web_functions/web_request.h` (+ `.cpp`), `src/web_functions/web_functions.cpp`, `test/test_web_request/`, `test/test_web_diff/` (live entry only) | 6b                                             |
| 6b   | `nimble_att_write`: prepare/execute write queue of `ble_att_svr.c` (NimBLE 2.2.3 patched copy), same pattern as `nimble_gatts_write` (text extraction into `gen/`, chain length fixed per job, data as a position pattern)                                                                                                         | `specs/cbmc/nimble_att_write/`                                                                                                                         | 6a                                             |
| 6c   | Proof group `specs/cbmc/web_request` for the head parser (orchestrator runs it after 6a)                                                                                                                                                                                                                                           | `specs/cbmc/web_request/`                                                                                                                              | after 6a                                       |
| 7    | Web wave B, steps 4-8: route table, REST handlers on `WebReq`, `num_parse.h`, body reader with WDT feed and 408, fixed response heads; B1-B7, B10. B1 needs the `mcau.js` change first (scaffold-as-liveness, seams doc 7.4)                                                                                                       | `web_functions.cpp` is the hotspot: one writer at a time, or carve out new modules (`web_route.h`, `web_query.h`, `num_parse.h`, `web_body.h`)         | full host gate after step 5                    |
| 8    | Web wave C, steps 9-11: spectrum sweep one step per loop pass on explicit action (B8) plus the shading bug, config upload reboot from the loop (B9), relocations, non-blocking send for TDECK-LAG-01, docs                                                                                                                         | per relocation, one commit each                                                                                                                        | re-measure TDECK-LAG-01 on DK5EN-14 afterwards |
| 9    | Heap work for NimBLE F3 (+600 B): candidates phone rings dynamic on S3 (~6 KB), UDP hold store only during outages (6 KB), raw LoRa log 20 to 10 slots (~2.6 KB); then F3                                                                                                                                                          | to be mapped                                                                                                                                           |                                                |
| 10   | Large bench test (operator: at the end), full host gate, `regression.sh --formal` on request                                                                                                                                                                                                                                       | orchestrator                                                                                                                                           |                                                |

### How to run (rules for every wave)

- Agents write code and proof/model files only. Every ESBMC/TLC run is started by the orchestrator, one at a time, in
  the foreground or as one background task, never as polling loops (agents spawned 30+ loops twice). Budget 6 GB
  quick, up to 15 GB; a job over that means refactor the code or the harness (fixed lengths per job, data patterns
  where the code does not branch on the data).
- Lock wrappers in the scratch dir `/tmp/formal02/` (recreate after a reboot): `piolock.sh` (mkdir lock around `pio`,
  exports `MC_AU_DEV_CHANNEL=1`), `esbmclock.sh <group>/<job>.check` (mkdir lock, runs `specs/cbmc/run_cbmc.sh` from the
  repo root with `MEM_MB=6000`).
- Gate per wave: `python3 tools/suite_map.py --write`; `python3 test/golden/variant_ini_effective.py --generate` after
  env changes; `tools/regression.sh --changed`; `specs/selftest.sh` and `specs/drift.py --check` when specs change
  (`drift.py --accept <model>` only after reading the source diff). Advisor (Opus, fresh agent) before each commit.
- Bench: DK5EN-14 (T-Deck Plus, USB serial E0:72:A1:AD:65:E0) runs the INSTRUMENT image of `acc07d31` in WiFi STA
  mode; load `/bench` first; 2 dBm, test traffic only to group 9999.

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

| Wave | Content                                                                                                                                                                                                         | State   |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- |
| 1    | Seams: `handleACK` core, `readPhoneCommand` with explicit length, settings round trip + migration; UDP twin analysis, web_functions map                                                                         | done    |
| 1b   | UDP receive consolidated into `src/udp_frame.cpp` (part 1 `f4f541d5`; part 2 applies operator decisions D1/D2/D3/D5)                                                                                            | done    |
| 2    | Bounded proofs: ACK core, phone frame (`ce013112`), UDP parse core (`5b692faa`); differential strips tightened                                                                                                  | done    |
| 3    | BLE host task vs main loop model `specs/tla/bletask.tla`; fixed BLE-QDROP-01 and BLE-STALE-01 (wave 3b)                                                                                                         | done    |
| 4    | 4a NimBLE fixes F1/F2/F6/F10 + `nimble_attvalue` (`acc07d31`); web server: analysis, B1-B10 accepted, corpus captured, wave A steps 0-2 (`28eb83bf`, `1a6c3c63`); `nimble_gatts_write` 7 of 7 (`d8aa2576`)      | done    |
| 5    | Mesh model `specs/tla/meshrelay.tla` (asymmetric nodes, co-located pair, dedup, ACK TTL): today fails DeafNotFirst/RedundantRelayBound, candidate rule holds (`4f6ab492`); firmware rule waits for the operator | done    |
| 6-10 | See "Next waves" under Now: web step 3 + `nimble_att_write`, web waves B and C, heap for F3, large bench test                                                                                                   | planned |

## Wave 5 input: asymmetric nodes (operator 2026-11-27)

- Model the node that "talks a lot, is heard a lot, but is nearly deaf": a 1 W or 2 W PA (E22) on a long, thin coax,
  so what it hears is 10 to 20 dB weaker than what the other side hears from it. Such nodes are rough and must not
  become super nodes that relay first and fast. Measurement documents (a German node, an HB node) are the input;
  Found: HB9ZG-49 (`docs/log-reports/2026-11-01-24h/HB9ZG-49-Empfangspruefung.html`, verification `verify/hb9zg49.md`):
  heard by 24 stations, hears 9; partners reach it at -105..-119 dBm (SNR -0.4..-8 dB), it reaches them at -125..-127
  dBm (SNR -12.6..-20 dB); gap 12..15 dB on the strong links, control pairs 0.2..5.9 dB (median 3.7). German side:
  only the Freising pairs (`docs/archive/verdict-auto-via-routing-20260916.de.md` section 3.2, 5..12 dB, no PA); the
  German node named by the operator: DO1FE-19 (gateway, digipeater, T-Beam 1 W, near Laupheim). 7-day link data
  (2026-11-27): it hears HB9CU-5 at -84 dBm with SNR +1.2 dB, HB9CU-5 hears it at -86 dBm with SNR -17.1 dB; it hears
  DO2RS-20 at -107 dBm with SNR -17.9 dB, DO2RS-20 hears it at -127 dBm (SNR -17.6); HB9SWR-3 both ways about -101
  dBm / -11..-13 dB (symmetric). Reading: with SNR roughly RSSI minus noise, its own receiver sits at a noise level of about -85 to -90 dBm (normal
  is around -115 to -120 dBm at 125 kHz): 25 to 30 dB deaf, while its 1 W transmitter is heard well. The HB9CU-5 side
  (strong RSSI, SNR -17) is not explained by this. Not verified on site.
- Second Swiss example named by the operator: HB9MM-2 (digipeater, T-Beam 1 W, firmware 4.35p, via gateway HB9TOB-12).
  7-day link data (2026-11-27): it hears its partners at -103..-108 dBm with SNR -12..-16 dB, i.e. a noise level of
  about -87..-92 dBm at its receiver; partners hear it with normal noise levels (-110..-116 dBm). Pairs: HB9TOB-12 to
  it -85 dBm / +2.1 dB, it to HB9TOB-12 -103.5 dBm / +7.2 dB (18 dB stronger at HB9MM-2, yet 5 dB worse SNR); HB9DOZ-1
  to it -105.5 / -13.6, it to HB9DOZ-1 -119 / -3.6; HB9TJM-4 to it -105 / -13.7, it to HB9TJM-4 -121 / -10.3. About 20
  to 25 dB effectively deaf, same pattern as DO1FE-19.
- Third case named by the operator: DB0TAW-14 (repeater tower, Heltec Wireless Stick V3, 835 m, Rhoen, gateway
  DK3RW-12), 7-day link data (2026-11-27), noise level = RSSI minus SNR:
  - Its own receiver sits at about -103 dBm (10 to 15 dB above a quiet site) for every partner, from -93 dBm
    (DB0KH-11) down to -125 dBm; it hears about 35 stations, most of them at the edge (-120..-125 dBm, SNR -17..-20).
  - It is co-located with DB0TAW-13 (both ways about -40 dBm, SNR +6): two nodes on one tower, each the other's
    strongest neighbour; while one transmits the other is blocked.
  - Towards its own gateway DK3RW-12 it is the weak side: it hears DK3RW-12 at -104 / -1.2, DK3RW-12 hears it at
    -119 / -14.7 (15 dB weaker, 13.5 dB worse SNR), the reverse of the 1-2 W pattern.
  - Several partners have very noisy receivers themselves (DG0OPK-10, DM9PT-20, DL2IAS-10, DB0DOL-1: noise level -87
    to -93 dBm), so links look asymmetric from both ends.
  - Log recordings in ~/meshlog are being scanned for its relay behaviour. Related design note: mheard measures who I
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

## Operator decisions for wave 4 (2026-11-27)

- Web server (`docs/web-server-seams-20261127.md` section 7.2): behaviour changes B1 to B10 accepted (404/405 for
  unknown paths, exact route match, strict request line, numbers and Content-Length; B7 values with `--`/`&&` refused
  except manualcommand, over 200 bytes refused; spectrum scan step-wise on request; config upload reboot after 500 ms;
  408 on timeout). B1 needs the `mcau.js` fix first.
- Browser request capture for the differential corpus: on bench node DK5EN-14, flash current `fork-dev` and record.
- NimBLE F3 (pre-allocate 300 B per characteristic, ~600 B heap): deferred; first find where ESP32 heap can be won
  (operator: there are large BLE buffers of about 9 KB already).
- Heap scan for F3 (scout, 2026-11-27, estimates marked in the report): the BLE-side buffers the operator meant are the
  two phone byte rings (`phoneStore` 3,072 B on S3 / 2,048 B classic, `phoneComStore` 3,072 B; `src/loop_functions.cpp`
  ~:835-838) plus the NimBLE host stack (3,072 B) and MSYS pool (~1,168 B). On classic ESP32 the rings are already
  allocated only while BLE is up (`PHONE_RINGS_DYNAMIC`); on S3 (T-Deck) and RAK they are static. Candidates: phone rings
  dynamic on S3 (~6 KB), UDP hold store only during a server outage (6,144 B on S3), raw LoRa log 20 -> 10 slots on
  classic (~2.6 KB). Latest T-Deck Plus measurement: free 60 KB, largest block 51 KB
  (`docs/campaign-ota-lowheap-01-20261122.md`).
- NimBLE F10 FIFO size: the nRF52 RX FIFO stays at 256 B (overflow now counted); a 512 B FIFO (+256 B RAM on RAK4631)
  is deferred until the RAK RAM headroom is checked. An overflow still hands a cut frame to `readPhoneCommand`, which
  rejects it by its length check.
- Bench 2026-11-27: browser request corpus captured on DK5EN-14 (34 requests, all 13 pages, scripts, status, RM,
  getparam, /config, one message to group 9999) in `tools/bench/runs/harness/webcap-dk5en14-20261127.jsonl`.
  DK5EN-14 now runs the INSTRUMENT image of `acc07d31` (BACKLOG TDECK-LAG-01 has the loop measurements).
- NimBLE F6: drop the WRITE property of the node's TX characteristic (6E400003); the app only subscribes to it.

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
