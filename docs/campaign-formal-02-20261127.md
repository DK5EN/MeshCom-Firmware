# Campaign FORMAL-02: verifiable parsers, settings round trip, BLE, web server, mesh model

Resume point of the campaign. Current state, next wave and open questions only; finished waves move to the
archive section at the end with one line each.

## Now (2026-11-27, resume here)

**State:** waves 1-7 committed on `fork-dev`, not pushed (wave 7 closed with `d9566bb4`). Full host gate green after
wave 7 (141 envs, 4,459 ok, 1 skipped, 0 failed; pytest 1,857; five board links). `fork-dev` contains all of `upstream/dev`
(checked 2026-11-27). Start a fresh session with `/orchestrate-waves` on this doc; next is wave 8 (web wave C, steps
9-11).

- **Wave 8 inputs (scout, 2026-11-27, design `docs/web-server-seams-20261127.md` 7.3 :428-430, B8/B9 :404-405, 6.7
  :329-334):**
  - Step 9 = relocations, one commit per move (web_functions.cpp -520, new files +450): spectral sweep out with B8 (sweep
    only on an explicit "Scan now", one step per loop pass, no `delay`; the page shows the last result until pressed),
    `eth_mss.cpp`, `mcRandomFill`, pin rows, `au_web.cpp`, mbox actions, `web_config.cpp` with B9 (config upload answers,
    then reboots 500 ms later from the loop instead of `delay(2000)`). Risk: RAK4631 build (eth_mss, RNG); bench look at the
    spectrum page and a config restore. `web_dispatch.cpp` / `web_api.cpp` are the 6.1 targets (dispatcher after the head,
    the four REST handlers).
  - Step 10 = optional `webGuardCheck(const WebReq &, ...)`, text scan kept as a second implementation for one wave,
    identical behaviour; `web_guard.h` is the most pinned file. Carried item: `web_guard.h` helpers move into a small
    shared header first (include cycle), keep the D-5 host/origin order.
  - Step 11 = docs (Web-API doc, test-suite-map, drift matrix, cbmc group, RESUME, BACKLOG); `specs/selftest.sh` lints the
    manifest in every gate.
  - Code today (`web_functions.cpp`): `sub_page_spectrum` ~:4105 (called ~:1497), step loop ~:4167, `delay(50)` + `yield()`
    per step ~:4199 (70-80 steps, at least 4 s, unmeasured; `spectral_scan.cpp` polls with `delay(10)`); shading bug ~:4184
    is an integer division `res[i] / spec_samples` (alpha always 0, clamped to 0.2; display only). `sub_config_upload`
    ~:873: answer ~:924, `delay(2000)` ~:930, `ESP.restart()` / `NVIC_SystemReset()` ~:933-938. The seams doc's sweep
    line refs (:4016-4133, D1 :93) are stale.
  - BACKLOG: TDECK-LAG-01 (:341, open: web server blocks GPS/display/LoRa on weak WiFi; plan web_txbuf pieces across loop
    passes plus B8), WEB-SPECTRUM-LEAK-01 (:347, leak fixed, blocking sweep and shading open). B8/B9 live only in the seams
    doc.
- **Wave 7b mechanics (still valid for wave 8):** `webRouteDecide` (web_functions.cpp) gathers inputs, `webRouteDecideCore`
  (web_route.h, pure) decides; feature mask `built` (WRB_*) pinned by `test_web_route`. Rig whitelists in
  `test/test_web_diff/test_web_diff.cpp` exact both ways; `WEB_DIFF_DUMP_DIFFS=<file>` lists differing requests; D14
  normalisation `d14Norm` maps the frozen/golden side only. Every relocated function the rig extracts needs its new file in
  `live/extract_live.py`. Run every `pio` through the lock; the gate takes the lock dir itself (`mkdir
/tmp/formal02/pio.lock` around `tools/regression.sh`). Before a full gate, run the source-pin consumers of a touched file
  (`node tools/webgui_*_test.js` with `NODE_PATH=$HOME/.cache/meshcom-jsdom/node_modules`, `native_command_setters`): they
  pin text in `web_functions.cpp` / `web_route.h`.
- **Wave 7a result (building blocks, not wired):** `src/web_functions/web_route.h` (43 routes, exact match; corpus:
  777 identical, 40 refused by the parser before routing (B3 19, B6 20, CORPUS 1), 136 deltas B1 59 / B2 39 / B3 21 /
  ENV 1 / CORPUS 16, all pinned), `web_query.h` (field extractor, streams through the new `urlDecodeTo`, no stack
  temp; tocall keeps today's `&message` stop), `src/num_parse.h` (strict int and 3-digit fixed point, `mcTrimSpan`,
  `mcFormatDec3`; B5 table against today's site parsers), `web_body.h` (body accumulator, 1:1 with `web_read_body` and
  `rmReadBody`). `webReqParse` now refuses targets without a leading `/` or with a raw blank (B3 shape rule).
  Proofs rerun: `url_decode` 6/6, `web_request` 28 (22 quick, 6 heavy up to 8.6 GB).
- **Wiring decisions for 7b (documented in `web_route.h`):** without a session a page-shaped request (`GET /?page=...`)
  gets the login page whether the page exists or not; HEAD gets 405 with `Allow` built from the row's method mask; the
  dispatcher wipes head and line on every exit; `/?nodepassword` is one route, the handler branches on the value; the
  setparam wiring trims space/tab before `mcParse*`; windcal/tempoff format back with `mcFormatDec3`.
- **B11 accepted (operator 2026-11-27):** `send_message` reads `tocall` by name, so a hand-made URL with `&message=`
  before `&tocall=` goes to the named station instead of a garbage destination; own commit in step 5.
- **Waiting on the operator:** relay candidate rule (RELAY-ORDER-01).
- **Step 5 recon (scout, 2026-11-27):** `web_query.h` API: `webQueryFirst/Get/Tail/Until` (bool, `WebSpan` out) and
  `webFieldDecode(in, out, cap, policy)` (0 empty, NOFIT when too long). Handlers take `String web_header` by value
  (`send_message`, `call_function`, `setparam`, `getparam`; setters `webSetup_setParam` / `webSetup_getParam` in
  `web_setup.cpp`); `manualcommand` rewrites a leading `&&` to `--` (`web_setup.cpp` ~:284). Today's guard
  (`webGuardSetparamAllowed`, `web_guard.h` ~:941) only demands X-MC for update/gpio values with `--`/`&&`, so B7 is a
  real change. No corpus row is tagged B4/B7/B11: step 5 adds its own requests.
- **Wave 6 result:** `src/web_functions/web_request.h` (head accumulator and request-line parser, `d528d071`) and
  `work_webpage` running on it (`d787c11b`, accumulator one heap block from `startWebserver()`, 503 when the heap is
  exhausted). The request rig has a live entry (`test/test_web_diff/live/`, cut from the working tree by function
  name): frozen and live agree on all 1,247 requests under every profile, whitelist empty. Proofs: `web_request` 27
  jobs with the expected verdict (23 quick, 4 heavy at 5.8-14.3 GB), `nimble_att_write` 3 jobs (one prepare, the leak
  mutation, `today_prepend_noleading` = F7). Parked over 15 GB: `web_request/parked/pass_parse_hdr`
  (FORMAL-WREQ-SPLIT), 12 `nimble_att_write/parked/` request-sequence jobs (FORMAL-ATTW-SPLIT).
- **Solver lesson (wave 6):** ESBMC's default Bitwuzla answers in seconds where `--z3` timed out at 900 s (all new
  jobs run without `--z3`). Harness rules: no pointer differences, no integer-to-pointer casts at symbolic indices, no
  division by variables, small byte alphabets through a constant table.
- **Last gate `--changed` (wave 6'):** 7 envs 186/186, board links (heltec V3, T-Beam, T-Echo, T114, RAK4631), pytest,
  inventory, specs self-test green. Board flash: heltec V3 +508 B, RAK4631 +592 B (static RAM +8 B).
- **Rig status:** `test/test_web_diff` compares the frozen copy (`frozen/`, sha256 pinned) with the live copy;
  regenerate the live copy after every edit of an extracted function: `python3 test/test_web_diff/live/extract_live.py`
  (`--check` runs in pytest, the suite's staleness case names the command). Corpus files keep CRLF.

### Next waves (dispatch plan)

Plan source for the web steps: `docs/web-server-seams-20261127.md` 6.2-6.8 (design) and 7.3 (steps). B1-B10 are
accepted (operator 2026-11-27); each B change gets its own commit and a counted `kWhitelist` row in the rig.

| Wave | Content                                                                                                                                                                                                                                                                                                                                                                          | Owner files (writer)                                                                                                                           | Parallel with                                  |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| 7    | Web wave B, steps 4-8: route table, REST handlers on `WebReq`, `num_parse.h`, body reader with WDT feed and 408, fixed response heads; B1-B7, B10. B1 needs no `mcau.js` change (checked in step 3, see the wave 7b step 3 archive line). B3/B6 (strict request line and Content-Length, already computed by `web_request.h`, act on nothing yet) land here as their own commits | `web_functions.cpp` is the hotspot: one writer at a time, or carve out new modules (`web_route.h`, `web_query.h`, `num_parse.h`, `web_body.h`) | full host gate after step 5                    |
| 8    | Web wave C, steps 9-11: spectrum sweep one step per loop pass on explicit action (B8) plus the shading bug, config upload reboot from the loop (B9), relocations, non-blocking send for TDECK-LAG-01, docs                                                                                                                                                                       | per relocation, one commit each                                                                                                                | re-measure TDECK-LAG-01 on DK5EN-14 afterwards |
| 9    | Heap work for NimBLE F3 (+600 B): candidates phone rings dynamic on S3 (~6 KB), UDP hold store only during outages (6 KB), raw LoRa log 20 to 10 slots (~2.6 KB); then F3                                                                                                                                                                                                        | to be mapped                                                                                                                                   |                                                |
| 10   | Large bench test (operator: at the end), full host gate, `regression.sh --formal` on request                                                                                                                                                                                                                                                                                     | orchestrator                                                                                                                                   |                                                |

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
| 6    | Web step 3: head parser `web_request.h`, `work_webpage` on it, rig live entry (`d528d071`, `d787c11b`); proofs `web_request` 27, `nimble_att_write` 3 (13 parked over 15 GB)                                    | done    |
| 7-10 | See "Next waves" under Now: web waves B and C, heap for F3, large bench test                                                                                                                                    | planned |

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
  408 on timeout). B1 needed the `mcau.js` fix first; step 3 found no failure case and lifted that gate (archive, wave 7b step 3).
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
- Web (wave 6 advisor): the rig always runs with `bDEBUG=false`, so the serial echo and the W2-1/WEB-AUTH-01 quiet
  logic were checked by reading only; a `bDEBUG=true` profile would test it. The 503 path (no heap for the
  accumulator) has no test; a rig case with a forced allocation failure would cover it.
- Web: `WebReq.host`/`origin` are the FIRST value, the guard keeps the LAST; unobservable only while the guard
  refuses `host_n > 1` / `origin_n > 1` before reading the value (D-5 in `web_request.h`). Step 10 must keep that
  order. `web_guard.h` helpers move into a small shared header before step 10 (include cycle otherwise).
- Web: NUL bytes follow ESP32 `String` semantics; the nRF52 core's `concat(char)` is a `strcpy` and its copies stop
  at a NUL, so bytes after a NUL can differ on nRF52 (no read past the buffer). The corpus proves ESP32 only (D-6).
- Web: a lone CR as the unterminated last line gives `WebReq.blank = 1` (inherited from `web_guard.h` ~:640,
  pinned in the suite and the proofs); BACKLOG WEB-CR-BLANK-01.
- Web: the server holds the 2.4 kB accumulator permanently on top of the 1 kB `web_header` reserve; measure with
  `/ram-snapshot` (ESP32 classic) during the heap wave 9.

## Open questions

## Archive

- Wave 7b end (2026-11-27, `d9566bb4`): `webRouteLegacy`, the disagreement counter, the unparsed no-session gate rules and
  `WRL_RM_SEND_GET` removed (dead since step 4: every refused parse is a 400); `webRouteDecideCore` without the legacy
  argument, ids renumbered. Rig exact with no new rows; three source pins moved to the `web_route.h` rows. Full host gate
  green. Wave 7 closed (steps 1-8: router as table, B1-B7, B10, D14, `web_io.*`). No advisor for this item (dead-code
  removal, rig byte-identity is the proof).
- Wave 7b step 8b (2026-11-27, `797ed2f2`): D14, `send_http_header` status line `"%u %s\r\n"` (was `" \n"` with a trailing
  space). Rig: `d14Norm`/`d14NormOut` map the old form on the frozen/golden side only (live compared raw; 3435 frozen
  records rewritten), `test_d14_norm_is_narrow` corpus-wide. Advisor approved; one-sided normalisation applied after it
  (old-form live answer: 11 cases red).
- Wave 7b step 8a (2026-11-27, `a624b296`): `webStatusText` (one reason table, +500/503) and `webSendFixedHead` in
  `web_io.*`; the 8 hand-built heads call it byte-identically except the config download (D14: CRLF, Cache-Control
  before Connection). Rig: old site bytes vs live helper, directory-wide scan for hand-built heads (mutation red).
  Advisor: firmware approved, scan widened to every file of `src/web_functions/`.
- Wave 7b step 7 (2026-11-27, `6ec0bfcd`): B10, `webReadBody` in new `web_io.cpp` (WDT feed per pass), `web_read_body`
  and `rmReadBody` are wrappers; stalled body 408 (config upload was 400, rm "short" was 422, bodies unchanged); stalled
  head 408 only if at least one byte arrived, zero-byte preconnect stays silent (orchestrator decision); head loop feeds
  the WDT. Rig `kWhitelistB10` 18 rows (frozen silent, live exactly the 408, rest of the record equal), source pins with
  10 mutations in `test_rm_web_parse`. Advisor approved src/, two test hardenings applied and mutation-checked.
- Wave 7b step 6 (2026-11-27, `56e99fe7`): B5, `num_parse.h` at the 9 sites (trim, strict whole input, site ranges;
  windcal/tempoff back through `mcFormatDec3`; no console line on a refusal). web_setup.cpp rungs checked by the real
  parser over their extracted ranges and source pins (console line from the parsed value inside the parse guard,
  mutation-checked); the mbox sites by rig runtime pins. Full gate green.

- Wave 7b step 5 (2026-11-27): the four API handlers read the parsed `WebReq` (`3199d5f2` setparam/getparam, `77b655be`
  callfunction/sendmessage; identical except B3 HTTP/1.0 values, `HTTP/1.1` inside a query, header-line `&tocall=` /
  `&message=` (B2), blanks before a raw NUL; all pinned frozen != live). B4 `fba81154` (setparam without `=` -> 422), B7
  `7d2cecaa` (`--`, leading `&&`, look-alike dashes after the setters' trim/lower-case, values over 200 bytes -> 422;
  manualcommand exempt from the dash rule), B11 (tocall by name). Refusals answer `{"returncode":1,"<name>":""}`.
  Advisor findings fixed with regression tests: NUL before `=`, the webhost/tz dash bypass of B7, and the pre-existing
  X-MC guard bypass with look-alike dashes (WEB-GUARD-DASH-01, `c22944b2`). WEB-WIPE-COPIES-01 done for the four
  handlers. Full host gate green.

- Wave 7b step 4 (2026-11-27): B3 (`d090c380`) and B6 answer 400 `{"ok":false,"err":"bad request"}` right after the
  guard 403, before AU-busy 503, assets, login and every route, with or without a session; the router decides once,
  before that check. B6 uses both judgements: the parser's (window) and the accumulator's `webHeadBadContentLength`
  (every head line, also past the 1023-byte window; advisor finding, regression test
  `test_b6_content_length_past_the_window`). Rig: B3 57 rows, B6 20 rows, all four profiles, 400. The `js-*-pageq`
  corpus rows are relative-URL generator artifacts (a browser sends `/?page=`). The legacy chain still runs for a
  refused parse but routes nothing; removal with the step-8 cleanup. B11 accepted by the operator.

- Wave 7b step 3 (2026-11-27): B1 wired. A parsed request with no built route gets 404 (JSON) or 405 with `Allow`
  from the rows' method masks; an unknown page 404 + the unknown page; no session: page-shaped -> login page, `GET /`
  -> login scaffold, other routes 401, no route -> the same 404/405 (the gate reads the parsed request line, so a
  Referer `/?page=` or HTTP/1.0 no longer gives the login page: 401). `GET /rmsendx` 404. A refused parse keeps the
  legacy chain until step 4. `web_header` and `wg_path` wiped on every exit after the head (`web_wipe_request`). Rig:
  77 B1 rows, every row names the profiles where it differs, B1 rows assert status and Allow per profile; leak probe
  over all corpus requests and profiles (before the fix 1,826 of 4,988 runs left request bytes). `mcau.js` gate
  lifted: the liveness probe hits the freshly installed image, and every ESP32 image with B1 routes `/austatus`
  (JSON) under the same guard that serves `/mcau.js`; an older image still answers the scaffold. Advisor: two rounds
  (per-profile rig check, wipes on all exits; docs); handler copies of secrets -> BACKLOG WEB-WIPE-COPIES-01.

- Wave 7b steps 1-2 (2026-11-27): router as classifier + switch with the table alongside (`ccdf9260`); B2 exact match
  (`34fb0a3b`) with build-feature mask (a route not built on a board counts as no route). Advisor caught two board-
  variant defects the rig cannot see (unbuilt route served the scaffold; include-order trap for OLED_SHOT_WEB); fixed
  with regression tests.

- Wave 7a (2026-11-27): route table, query extractor (+ `urlDecodeTo`, `URLD_TRUNC`), strict number parser, body
  accumulator, B3 request-line shape rule in `webReqParse`; four native envs. Advisor: rework on labels, stack temp,
  today-references for B5, sign of negative milli values; second round (refused requests pinned, B3/B11 in 7.2) by
  the orchestrator; approved. Gate `--changed` PASS (141 envs, 4,423 ok, 1 skipped).

- Wave 6 (2026-11-27): head parser and `work_webpage` on it (`d528d071`, `d787c11b`), rig live entry, proof groups
  `web_request` (27) and `nimble_att_write` (3 active, 12 parked). Advisor: three rounds on the parser (NUL semantics
  of the Cookie line, Content-Length as `long` with exact atol semantics), one on the head loop (include moved into
  the .cpp). Gate `--changed` PASS.

- Wave 1 (2026-11-27): ACK core, phone frame decoder with explicit length, shared id lookups, settings migration and
  round-trip proof jobs, nan/inf settings reject; advisor rework applied (harness newline loop, fixture generator
  frozen and month-proof, BLE stress fake node, empty nRF BLE queue items). Gate `--changed` PASS (132 envs, 4,311
  cases). App bug #1343.
