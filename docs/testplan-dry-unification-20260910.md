# Test plan: DRY unification before/after (draft 2026-09-10)

Status: planning draft. Nothing in this document has been executed. No code, no test, no hardware
was touched for it. It plans the evidence chain around the one-shot refactor described in
`docs/optimization-audit-20260910.md` section 5.1 (structural) and 5.2 (helpers).

Decisions already taken (from the planning session):

| Topic              | Decision                                                                                                                                                             |
| ------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Test layers        | Native characterization tests where a seam exists or is cheap, hardware golden captures for every wire surface, same corpus for both                                 |
| Scope              | Audit section 5.1 structural items plus 5.2 helpers; high-risk left-outs (D1-09, D1-10, R1-05, R2-02/03) excluded                                                    |
| Drift decisions    | `docs/testplan/drift-matrix.csv` is the source of truth; an `.xlsx` is generated for the review session; all rows decided before coding starts; default winner ESP32 |
| Artifacts          | Golden captures and protocols committed under `test/golden/`                                                                                                         |
| Bench              | All four bench nodes may be reflashed; settings backed up and restored via config JSON; a local stub server drives UDP 1799                                          |
| Tests upstream     | Fork-only; the upstream PR carries firmware only                                                                                                                     |
| nRF52 upgrade test | Runs on RAK-90 with a JSON backup; a wipe is acceptable as a test outcome                                                                                            |
| Baseline           | Fresh upstream sync, then freeze and tag; goldens captured once; one re-sync before submission with a targeted re-run                                                |
| PR evidence        | The protocol tables are written so they paste into the German PR description; template in section 9                                                                  |

## 1. The evidence chain in one picture

```
base (tagged)            carve-out               unification              submit
    |                        |                        |                       |
 G0 hardware goldens ----> G1 hardware goldens ====> G2 hardware goldens     re-sync
 (all 4 nodes,           (must equal G0 byte      (must equal G1 except     targeted
  all surfaces)           for byte)                expected-diff rows)      re-run
    |                        |                        |
 N0 native suites        N1 characterization      N2 same suites +
 (existing, green)       tests written against    expected-diff fixtures
                          the carved functions,   + schema/layout/order
                          twin-differential runs  invariants
                              |
                          drift-matrix.csv filled and signed (HITL)
```

- **G0 to G1 proves the carve-out changed nothing.** Extracting a block into a named function is a
  code change; it happens before the native tests can exist, so it is guarded by the hardware
  goldens alone.
- **N1 is where the twins are run side by side** on the same corpus. Their output diff is the raw
  material of the drift matrix. This turns "23 named drift bugs" from a reading into a measured list.
- **G2 and N2 assert the decisions**: byte-identical where the matrix says "keep", and exactly the
  expected diff where it says "ESP32 wins" or "nRF52 wins".

## 2. Units under test and their seams

The unification items map onto nine units. For each: where it lives today, how it becomes callable
natively (the seam), what has to be stubbed, what is observed.

| UUT | Today                                                                                                                                         | Seam for native tests                                                                                                                                                                              | Stubs needed                                                                                                                                                                                                                      | Observed outputs                                                                                   | Existing tests to build on                                                           |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| U1  | UDP frame handler: `getMeshComUDPpacket()` `udp_functions.cpp:205-638`, `NrfETH::getUDP()` `nrf_eth.cpp:352-824`                              | Carve the body after "datagram is in the buffer" into `handleUdpFrame_esp32(buf,len,src)` and `handleUdpFrame_nrf52(...)`, no other change; both compile into one native binary                    | `test/support/Udp.h`, `IPAddress.h` exist (used by `test_getextern`); recording sinks for `addTxRingEntry`, `addBLEOutBuffer`, `addUdpOutBuffer`, `sendDisplayPosition`, `sendExtern`, `resetMeshComUDP`/`resetDHCP`, `printfdeb` | ordered list of sink calls with their bytes; log lines; return value                               | `test_getextern` (EXTUDP inbound), `test_gwflood_frames`, `test_aprs_corpus` vectors |
| U2  | `sendMeshComUDP()` `udp_functions.cpp` vs `sendUDP()` `nrf52_main.cpp`                                                                        | Same carve-out; input is the UDP-out ring state, output is datagrams                                                                                                                               | ring globals compiled in (`loop_functions_extern.h` symbols provided by a test TU), UDP send sink                                                                                                                                 | datagram bytes and order, ring pointer movement                                                    | `test_txring`, `test_txring_flood`                                                   |
| U3  | `checkSerialCommand()` `esp32_main.cpp:4307-4434` vs `nrf52_main.cpp:2895-3007`                                                               | Already a function on both sides; compile both with a `Serial` stub that replays a byte script                                                                                                     | `Serial` stream stub, `commandAction` recording sink, `sendMessage` sink, net-console reader stub                                                                                                                                 | sequence of `commandAction` strings and message sends per input script                             | none                                                                                 |
| U4  | Settings: `esp32_flash.cpp` load/save, `nrf52_flash.cpp` load/save/migrate, `config_json.cpp` table, `settings_sanitize.cpp`, `web_setup.cpp` | No behaviour test possible natively for NVS/InternalFS; test the invariants instead: struct layout, NVS key set, schema completeness, sanitize ranges                                              | struct header compiles on host with `test/test_config_json/stubs` (exists)                                                                                                                                                        | `offsetof`/`sizeof` table of every member; sorted NVS key list; schema row per member; range table | `test_config_json`, `test_settings_sanitize`                                         |
| U5  | Command ladder `commandAction()` `command_functions.cpp:238-6172`                                                                             | Not compilable natively; two proxies: (a) hardware golden of every command, (b) a source-level test that extracts the ordered command-name list from the ladder (before) or from the table (after) | none                                                                                                                                                                                                                              | ordered name list; the 13 prefix-order pairs; echo style per command                               | none; `tools/bench/serial_session.py` drives commands                                |
| U6  | `lora_setcountry()` `lora_setchip.cpp:193-503`                                                                                                | Callable after stubbing the radio globals; run per country code 1..15 on both `#if` sides via two native envs (`-D BOARD_RAK4630` and not)                                                         | `meshcom_settings` stub, radio object stub                                                                                                                                                                                        | tuple `freq,bw,cr,sf,track_freq,preamble` per country per side                                     | none                                                                                 |
| U7  | `decodeAPRSPOS()` tag extractors `aprs_functions.cpp:667-1041`                                                                                | Already native                                                                                                                                                                                     | none                                                                                                                                                                                                                              | `aprsPosition` struct bit-compare                                                                  | `test_decodeaprspos`, `test_aprs_corpus`                                             |
| U8  | mheard row and hop list (`mheard_functions.cpp`) for R2-01/R3-13, and the seven APRS send epilogues (D3-02)                                   | Already native for mheard; epilogue via a recording `encodeAPRS`/`checkVia` pair                                                                                                                   | `test/test_decodemheard/stubs` exists                                                                                                                                                                                             | `--mheard` text rows, MH JSON, `msg_buffer` bytes per epilogue site                                | `test_decodemheard`, `test_mheard_aging`, `test_checkvia`                            |
| U9  | UI trees (`ui_common`) and variants (`configuration.h`, `platformio.ini`)                                                                     | No behaviour test; build-level invariants: per-env preprocessor macro set, per-env resolved `pio project config`, `.bin` size                                                                      | none                                                                                                                                                                                                                              | macro-set dump per env, config dump per env, section sizes                                         | `tools/resource_watch.py regions`                                                    |

Twin-differential is the pattern for U1, U2, U3, U6: both sides linked into one native binary,
fed the same corpus, outputs diffed. That diff is committed as
`test/golden/native/<uut>-twin-diff-before.txt` and is the pre-filled evidence column of the drift
matrix.

## 3. Phase 0: preparation (no product code)

| Step | What                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Output                                                        | Est.  |
| ---- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- | ----- |
| P0.1 | Merge `upstream/dev` into `fork-main` (merge, not rebase, per project memory), full 32-env build, tag `dry-base-YYYYMMDD`                                                                                                                                                                                                                                                                                                                                                                     | tag, `tools/resource_baseline.json` refreshed from that build | 0.5 d |
| P0.2 | Freeze the audit line references: regenerate the appendix line numbers against the tag if upstream moved them (script over the cited `file:line` pairs)                                                                                                                                                                                                                                                                                                                                       | updated appendix or a note that nothing moved                 | 0.5 d |
| P0.3 | Backup bench node settings: `--export` config JSON for RAK-90, Heltec-93, T-Beam-92, T-Deck-14; store under `test/golden/nodes/<node>/settings-base.json`                                                                                                                                                                                                                                                                                                                                     | four JSON files                                               | 0.5 h |
| P0.4 | Corpus assembly: (a) LoRa frames from `test/support/gwflood_frames.txt` and the `test_aprs_corpus` vectors, (b) UDP 1799 datagrams: GATE frames wrapping (a), one BEAT, one CONF with tags 0x00..0x04, malformed cases (odd length, zero-flood, oversize), (c) EXTUDP inbound JSON set from `test_getextern`, (d) command script: every command name from the ladder with canonical, out-of-range, non-numeric and the adversarial prefix set; destructive commands in a separate manual list | `test/golden/corpus/{lora,udp1799,extudp,commands}/`          | 1.5 d |
| P0.5 | Foreign callsigns in corpus frames rewritten to `DK5EN-*` before anything can transmit (hard rule 2026-09-06)                                                                                                                                                                                                                                                                                                                                                                                 | corpus lint script                                            | 0.5 d |
| P0.6 | Stub server spec for UDP 1799 (`tools/bench/mc_server_stub.py`, planned): binds 1799, answers BEAT, replays corpus GATE frames on a schedule or on command, records every datagram with a monotonic timestamp, never forwards to the real network                                                                                                                                                                                                                                             | spec in this doc, section 10; implementation in phase 1       | 1 d   |
| P0.7 | Normalization rules for every capture (section 7)                                                                                                                                                                                                                                                                                                                                                                                                                                             | `test/golden/normalize.py` spec                               | 0.5 d |
| P0.8 | BLE capture path: a host BLE client that connects as the phone, subscribes to notifications, records raw frames and can write the 10 opcodes. No such tool exists in `tools/`; needs `bleak` on macOS                                                                                                                                                                                                                                                                                         | tool spec; implementation in phase 1                          | 1 d   |
| P0.9 | Protocol templates (section 8) created empty so the before-run only fills cells                                                                                                                                                                                                                                                                                                                                                                                                               | `docs/testplan/protocol-before.md`, `protocol-after.md`       | 0.5 h |

## 4. Phase 1: carve-out and test creation

Order matters: hardware goldens first, then the carve-out, then goldens again, then native tests.

### 4.1 G0: hardware goldens at the tag

Run the full before-protocol (section 6) on all four nodes at the tagged base. This is the only
capture that exists before any code moves. Commit as `test/golden/hw/G0/`.

### 4.2 Carve-out commits (behaviour-neutral by construction)

| Carve | Change                                                                                                                                                                                                      | Proof it is neutral                                                    |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| C1    | U1: body of `getMeshComUDPpacket()` after the socket read into `handleUdpFrame_esp32()`; same for `NrfETH::getUDP()` into `handleUdpFrame_nrf52()`                                                          | G1 equals G0 on all surfaces; `.bin` diff limited to call/ret wrappers |
| C2    | U2: `sendMeshComUDP()` / `sendUDP()` bodies unchanged, but the datagram write goes through one `udpSendRaw()` per platform so a sink can replace it                                                         | G1 equals G0                                                           |
| C3    | U3: no carve needed; add `#ifdef UNIT_TEST` seams for `Serial` only if the stub cannot inject otherwise                                                                                                     | build only                                                             |
| C4    | Gateway block (D1-09) and scheduler (D1-10) are out of scope for unification, but carving them into `gatewayService_esp32()` / `_nrf52()` now costs nothing and makes their drift measurable for the matrix | G1 equals G0                                                           |
| C5    | U6: `lora_setcountry()` gets a pure inner function `countryProfile(code, side, out&)` that fills a struct; the outer function applies it                                                                    | G1 equals G0; country dump identical                                   |

Each carve is one commit, each followed by a 32-env build and the region gate (no IRAM growth on
the T-Beam family, no DRAM growth on E22_XML).

### 4.3 G1: hardware goldens after the carve-out

Same protocol. Pass criterion: every normalized capture identical to G0. Any diff stops the plan
until explained.

### 4.4 N1: characterization tests

| Test                            | Env                                            | Asserts                                                                                                                                          | Fixture written                                                         |
| ------------------------------- | ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------- |
| `test_udp_frame_twin`           | new `native_udp_twin`                          | for every corpus datagram: sink call sequence of the ESP32 side; sink call sequence of the nRF52 side; both recorded, diff reported per datagram | `native/u1-esp32.txt`, `native/u1-nrf52.txt`, `u1-twin-diff-before.txt` |
| `test_udp_send_twin`            | same                                           | ring with N entries drains to identical datagrams and order on both sides; pointer movement per pass                                             | `u2-*.txt`                                                              |
| `test_serial_command_twin`      | new `native_serial_twin`                       | byte scripts (line, `::` message, `--` command, overlong line, CR/LF variants, net-console input) produce the same `commandAction` call list     | `u3-*.txt`                                                              |
| `test_country_twin`             | `native_country_esp32`, `native_country_nrf52` | tuple per country code per side; committed as a table                                                                                            | `u6-country-before.csv`                                                 |
| `test_settings_layout`          | `native_config` (exists)                       | `sizeof(s_meshcom_settings)` and `offsetof` of every member equal the committed table for the ESP32 header and for the nRF52 header separately   | `u4-layout-esp32.csv`, `u4-layout-nrf52.csv`                            |
| `test_settings_nvs_keys`        | source-level (Python, `tools/`)                | sorted set of NVS key strings in `esp32_flash.cpp` equals the committed list; every key appears in both load and save                            | `u4-nvs-keys.txt`                                                       |
| `test_settings_schema_complete` | `native_config`                                | every struct member is either a `CFG_FIELD_LIST` row or on an explicit exemption list; sanitize ranges equal the schema min/max where both exist | `u4-exemptions.txt`                                                     |
| `test_command_ladder_order`     | source-level (Python)                          | ordered list of `commandCheck` names from the ladder; the 13 prefix-order pairs in the documented order; echo style per command (S1..S6)         | `u5-ladder-order.txt`, `u5-echo-style.csv`                              |
| `test_aprs_tags`                | `native_aprs` (exists)                         | `aprsPosition` bit-equal for the corpus and for synthetic tag permutations (every tag alone, all tags, malformed tags)                           | `u7-aprspos.bin`                                                        |
| `test_aprs_epilogue`            | `native_parsers`                               | for the seven send sites, `msg_buffer` bytes and `msg_id` sequence per site for fixed inputs                                                     | `u8-epilogue.txt`                                                       |
| `test_mheard_render`            | `native_parsers` (exists)                      | `--mheard` row text, `--path` text, MH JSON for a fixed mheard state                                                                             | `u8-mheard.txt`                                                         |
| `test_variant_macros`           | source-level (Python)                          | per env: `-dM -E` macro set of `configuration.h` with that env's flags; per env: `pio project config --json-output`                              | `u9-macros/<env>.txt`, `u9-config/<env>.json`                           |

All twin tests are written so that "diff is empty" is not the pass condition at N1; the pass
condition is "diff equals the committed before-diff". The before-diff is the evidence.

Estimate for 4.4: 6 to 8 days, U1 dominates (stubbing the sinks for both platforms in one binary).

## 5. Phase 2: HITL decision matrix

### 5.1 Source of truth

`docs/testplan/drift-matrix.csv`, one row per observed difference. Columns:

| Column                     | Meaning                                                                                                                                                    |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `id`                       | `DR-01`..                                                                                                                                                  |
| `uut`                      | U1..U9                                                                                                                                                     |
| `pair`                     | function pair                                                                                                                                              |
| `esp32_behaviour`          | one sentence, with `file:line`                                                                                                                             |
| `nrf52_behaviour`          | one sentence, with `file:line`                                                                                                                             |
| `evidence`                 | corpus item and line in the twin-diff fixture that shows it                                                                                                |
| `class`                    | `bug-one-side`, `platform-api`, `feature-one-side`, `cosmetic`                                                                                             |
| `tie_break`                | `ESP32`; applied only when the verdict is `both-valid` and one implementation must be chosen                                                               |
| `recommendation`           | analyst recommendation with reason                                                                                                                         |
| `verdict`                  | `esp32-correct`, `nrf52-correct`, `both-wrong` (correct behaviour stated in `spec`), `both-valid` (platform difference, stays split); filled in the review |
| `spec`                     | for `both-wrong`: one sentence stating the correct behaviour                                                                                               |
| `decided_by`, `decided_on` |                                                                                                                                                            |
| `after_expect`             | `identical`, `nrf52-changes`, `esp32-changes`, `both-change`; derived from the verdict, drives the expected-diff fixture                                   |
| `asserting_test`           | test name that will fail if the decision is not implemented                                                                                                |

### 5.2 Pre-filled rows (from the audit, to be confirmed by the twin-diff)

| id      | pair                                             | difference                                                               | recommended verdict                                                                |
| ------- | ------------------------------------------------ | ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------- |
| DR-01   | U1 zero-scan                                     | nRF52 reads `buf[i+1]` past the datagram on odd sizes                    | ESP32                                                                              |
| DR-02   | U1 RX-01 guard                                   | unconfigured-source guard ESP32 only                                     | ESP32                                                                              |
| DR-03   | U1 `hb_warn_logged`                              | reset in all four ESP32 branches, absent on nRF52                        | ESP32                                                                              |
| DR-04   | U1 `bGATEWAY_NOPOS`                              | honoured ESP32 only                                                      | ESP32                                                                              |
| DR-05   | U1 server position frames                        | `sendDisplayPosition()` and TM-31 early-dedup ESP32 only                 | ESP32                                                                              |
| DR-06   | U1 `decodeAPRS()` return                         | checked nRF52 only                                                       | nrf52-correct                                                                      |
| DR-07   | U1 `sendExtern()` gate                           | `hasExternIPaddress` gate ESP32 only                                     | ESP32                                                                              |
| DR-08   | U1 CONF guard                                    | no zero-address check on nRF52                                           | ESP32                                                                              |
| DR-09   | U1 ACK phone frame                               | nRF52 sends fixed 7 bytes, no attribution suffix                         | ESP32 via `buildAckPhoneFrame()`                                                   |
| DR-10   | U3 `msg_buffer`                                  | static on nRF52 (N-22), stack on ESP32                                   | nrf52-correct                                                                      |
| DR-11   | U3 net-console input                             | ESP32 only                                                               | ESP32, guarded by `DISABLE_NET_CONSOLE`                                            |
| DR-12   | U4 struct fields                                 | six fields missing on nRF52                                              | ESP32 (append on nRF52, migration)                                                 |
| DR-13   | U4 compat struct                                 | frozen snapshot drifted from live struct                                 | both-new (schema-driven migration)                                                 |
| DR-14   | C4 gateway TX pass                               | nRF52 sends only when nothing was received                               | ESP32; implemented only if D1-09 is admitted, otherwise a standalone fix with soak |
| DR-15   | C4 recovery nesting                              | nRF52 recovery under `!hasIPaddress`                                     | ESP32; same condition as DR-14                                                     |
| DR-16   | C4 telemetry gate                                | `bHeyFirst` vs `bTeleFirst && bAllStarted`; no `extra_hey_time` on nRF52 | ESP32                                                                              |
| DR-17   | U6 country table                                 | per-side value differences beyond the chip API                           | to be measured; expect `keep-split` where the radio API differs                    |
| DR-18.. | reserved for twin-diff findings not in the audit |                                                                          |                                                                                    |

### 5.3 Review session

- Input: the CSV pre-filled from the twin-diff fixtures, plus the `.xlsx` generated by a small
  `openpyxl` script (`tools/testplan/matrix_xlsx.py`, planned) with one sheet per UUT, filters and
  the evidence column linked to the fixture line.
- The matrix is a correctness verdict per observed difference, not a platform contest: every
  `esp32-correct` or `nrf52-correct` row is a confirmed bug on the other side, every `both-wrong`
  row a bug on both. That list is the sign-off result and feeds the defect section of the German
  PR description. The ESP32 tie-break applies only to `both-valid` rows.
- Rule: every row gets a verdict before the first unification commit. Rows left open block the
  UUT they belong to, not the whole PR.
- Output: CSV updated, committed; the `.xlsx` is a view, never the source.
- A native test `test_drift_matrix_complete` (Python) fails if any row has an empty `verdict`,
  a `both-wrong` row without `spec`, or an `asserting_test` that does not exist.

## 6. Hardware protocol (used for G0, G1, G2)

Nodes: RAK-90 (nRF52, Ethernet gateway), Heltec-93 (S3, WiFi gateway), T-Beam-92 (classic, WiFi
gateway, also the IRAM cliff board), T-Deck-14 (S3, UI, keyboard). Ports per project memory.

| Step | Surface                | Driver                                                                                                                                                                                          | Capture                                                               | Nodes                                                    |
| ---- | ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- | -------------------------------------------------------- |
| H1   | Boot log               | power cycle, `serial_session.py --wait-boot`                                                                                                                                                    | `boot.txt` up to `[BOOT];ready`                                       | all                                                      |
| H2   | Settings dump          | `--info`, `--seset`, `--wifiset`, `--nodeset`, `--analogset`, `--wx`, `--pos`, `--io`, `--tel`, `--aprsset`, config JSON export                                                                 | `settings.txt`, `settings.json`                                       | all                                                      |
| H3   | Command golden         | the corpus command script over USB, then the same over TCP 2323 (`tools/hmac_connect.py`); repeated once on an instrument image of one node; command-name string scan of all 32 shipping images | `cmd-usb.txt`, `cmd-2323.txt`, `cmd-instr.txt`, `cmd-names-<env>.txt` | all except T-Deck (USB only); instrument run on one node |
| H4   | BLE golden             | host BLE client: connect, subscribe, send the 10 opcodes, request the 13 JSON registers, trigger an mheard and a text frame                                                                     | `ble-frames.bin` with per-frame length prefix                         | RAK-90, Heltec-93                                        |
| H5   | LoRa RX path           | `--injectraw` of the LoRa corpus after `[BOOT];ready` (mesh and gateway off first, per the injectraw recipe)                                                                                    | `rx-log.txt`, resulting BLE frames, EXTUDP datagrams                  | all                                                      |
| H6   | UDP 1799 gateway path  | stub server replays the UDP corpus; node in gateway mode against the stub                                                                                                                       | `udp-rx-log.txt`, LoRa TX ring dump, BLE frames                       | RAK-90, Heltec-93, T-Beam-92                             |
| H7   | UDP 1799 upstream path | inject LoRa corpus with gateway on; stub records the datagrams the node sends                                                                                                                   | stub log `udp-tx.bin`                                                 | same                                                     |
| H8   | EXTUDP                 | `tools/bench/extudp_peer.py` sends the EXTUDP corpus, records replies                                                                                                                           | `extudp.txt`                                                          | RAK-90, Heltec-93                                        |
| H9   | Settings round trip    | import the exported JSON, reboot, export again                                                                                                                                                  | `settings-roundtrip.json`                                             | all                                                      |
| H10  | Region gate            | `tools/resource_watch.py regions` for all 32 envs from the same base build                                                                                                                      | `regions.csv`                                                         | build only                                               |
| H11  | T-Deck UI checklist    | manual: MHeard screen (7 columns), TRACK no-fix text, keyboard types 1..4 full character map, APRS symbol dropdown round trip                                                                   | `tdeck-checklist.md` with pass/fail per line                          | T-Deck-14                                                |

Per node and step the capture is normalized (section 7) and committed under
`test/golden/hw/<G>/<node>/<step>.*`. A run is complete when every cell of the protocol table in
section 8 is filled.

Estimated bench time per G-run: one working day for the four nodes, plus the stub and BLE client
once they exist.

## 7. Normalization rules

Captured text is compared after these substitutions, applied by one script to before and after:

| Field                          | Rule                                                          |
| ------------------------------ | ------------------------------------------------------------- |
| date and time strings          | replaced by `<TS>`; format is checked separately by regex     |
| `msg_id` values                | replaced by a per-run sequence index (`<ID1>`, `<ID2>`, ...)  |
| RSSI, SNR, frequency error     | replaced by `<RF>`                                            |
| uptime, heap, stack watermarks | replaced by `<NUM>` but the presence of the line is kept      |
| IP addresses, MAC, BLE address | replaced by `<ADDR>`                                          |
| firmware version strings       | kept, they must change deliberately at the final release only |
| order of lines                 | kept; ordering differences are real diffs                     |

Binary captures (BLE frames, UDP datagrams) are compared byte for byte after masking the
timestamp bytes at their known offsets.

## 8. Protocol tables (templates)

`docs/testplan/protocol-before.md` and `protocol-after.md` carry the same table, filled per run:

| Node | Step | File | Lines/bytes | Normalized hash | Compared to | Result | Note |
| ---- | ---- | ---- | ----------- | --------------- | ----------- | ------ | ---- |

And one summary per run:

| Item | Native suites green | Twin-diff equals fixture | HW goldens equal (steps) | Expected diffs matched | Region gate | Open |
| ---- | ------------------- | ------------------------ | ------------------------ | ---------------------- | ----------- | ---- |

Rules for filling: hash and result cells are pasted from tool output, never typed; an empty cell
means the step did not run and the run is incomplete.

## 9. After-run and pass criteria

After the unification (audit section 7.1 waves), on the same hardware, same corpus, same tools:

1. Native: all pre-existing suites unchanged and green; every N1 test green against its fixture or,
   for rows with `after_expect != identical`, against the expected-diff fixture generated from the
   matrix (`test/golden/native/<uut>-twin-diff-after-expected.txt`).
2. Hardware G2: every step identical to G1 except the lines the expected-diff files enumerate.
   Any extra diff is a failure, including a "better" one.
3. Invariants: NVS key set identical; ESP32 struct layout identical; nRF52 struct layout identical
   in the existing prefix, appended fields only; command name order equal to the ladder order for
   the 13 prefix pairs; per-env macro sets identical; per-env resolved config identical.
4. Migration: RAK-90 flashed with the base image and the backed-up settings, then upgraded to the
   post image: settings survive (`settings.json` equal after normalization). A wipe is a failing
   result, recorded, and the backup is re-imported.
5. Region gate: no env grows in IRAM; DRAM deltas match the audit's predictions within 5 %.
6. T-Deck checklist all pass.

German evidence summary template for the PR description (filled from the tables above):

```
Nachweis: Vorher/Nachher-Protokoll (docs/testplan/protocol-before.md, -after.md)
- Native Suiten: <n> Suiten, alle gruen, unveraendert; <m> neue Charakterisierungstests.
- Zwillingsvergleich ESP32/nRF52: <k> Abweichungen gemessen, alle in drift-matrix.csv
  entschieden (<e> ESP32, <r> nRF52, <b> neu), Nachher-Lauf entspricht den Entscheidungen.
- Hardware-Golden auf 4 Knoten, <s> Schritte: byte-identisch ausser den <d> erwarteten Zeilen.
- Ressourcen: IRAM T-Beam unveraendert, DRAM E22_XML +<x> B frei, alle 32 Envs gebaut.
- Migration nRF52: Einstellungen bleiben beim Upgrade von 20260724 erhalten.
```

## 10. Stub server specification (UDP 1799, planned tool)

- Binds `0.0.0.0:1799`, logs every received datagram with monotonic timestamp, source, length,
  hex, and the parsed indicator (`GATE`/`BEAT`/`CONF`/other).
- Replies to BEAT with the same bytes the real server sends (to be captured once from a real
  session and stored in the corpus; until then, the node's own expectations from
  `udp_functions.cpp` are the spec).
- `--replay corpus/udp1799/` sends each datagram once, in file order, with a configurable gap;
  `--repeat` for dedup tests.
- Never forwards anything; a node pointed at the stub is off the real network for the run.
- Output is the `udp-tx.bin` / `udp-rx-log.txt` capture of steps H6/H7.

## 11. What is still missing or assumed (for the next planning round)

- **BLE golden: resolved in section 12.** The host client is a planned tool (about 1 day); the
  write corpus is derived from the app source, and the app itself is used once to calibrate it.
- **Hardware covers 4 of 30 envs.** Classic E22 without OLED, the e-paper boards, T-Deck Pro and
  T5 are build-only. The audit's R4-02/03 board list decision needs hardware confirmation that the
  plan cannot give.
- **The carve-out is itself a change in the wire-adjacent files.** It is accepted because G0 to G1
  guards it, but it means the "before" native tests run on code that is one commit past the tag.
- **Command golden over BLE** (all 297 branches are BLE-reachable) is only as good as the BLE
  client; over USB and 2323 it is complete.
- **INSTRUMENT_ENABLED builds** have 76 more command branches (`command_functions.cpp:4786-5361`).
  Resolved: the command golden runs on the shipping image of all four nodes, once more on one
  instrument image (RAK-90 or Heltec-93), and a string scan of all 32 shipping images checks the
  full command-name list before and after (INS-01 lesson). The four `--spec*` commands that sit
  outside the guard are a matrix row.
- **Time budget.** Phase 0 about 5 days, phase 1 about 8 to 10 days including two bench days,
  phase 2 one review session, after-run 2 days. The unification itself is not in this plan.
- **Who signs the matrix.** The plan assumes one reviewer; if Kurt or another upstream maintainer
  should see the drift rows before the merge, the CSV plus xlsx is the artifact to send.
- **Frames with foreign callsigns** in captured corpora must be rewritten before injection; the
  corpus lint in P0.5 is the guard, but it needs a list of which fixtures are injection-capable.
- **Flakiness policy** for hardware steps: rerun the step alone, then 3 consecutive identical runs
  before a diff counts as real (project memory on flaky triage).

## 12. BLE capture design (from the app source)

Source of truth for the phone side: `/Users/martinwerner/WebDev/Meshcom-MobileApp` (Ionic/Capacitor,
`@capacitor-community/bluetooth-le` 8.3.0), files `src/hooks/BleHandler.ts`, `src/pages/Connect.tsx`,
`src/hooks/MessageHandler.ts`. Firmware side: `src/phone_commands.cpp`, `src/esp32/esp32_main.cpp`,
`src/nrf52/nrf52_ble.cpp`.

### 12.1 The link as both ends implement it

| Item                | Value                                                                                                                      | Where                                                                  |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| Profile             | Nordic UART Service (NUS)                                                                                                  | `BleHandler.ts:8-10`                                                   |
| Service             | `6e400001-b5a3-f393-e0a9-e50e24dcca9e`                                                                                     |                                                                        |
| Phone writes to     | `6e400002-...` (TX char, write with response)                                                                              | `BleHandler.ts:66`                                                     |
| Phone notifies from | `6e400003-...` (RX char, `startNotifications`)                                                                             | `Connect.tsx:607`                                                      |
| Connections         | one central at a time (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`; nRF52 `configPrphConn(250,...)`)                              | `platformio.ini`, `nrf52_ble.cpp:91-93`                                |
| Pairing             | passkey: ESP32 `BLE_HS_IO_DISPLAY_ONLY` with `bt_code`; nRF52 `Bluefruit.Security.setPIN(PAIRING_PIN)`                     | `esp32_main.cpp:1728-1732`, `nrf52_ble.cpp:98`                         |
| App-level PIN       | optional: hello carries SHA-256 of the 6-digit PIN, verified in firmware with mbedtls                                      | `Connect.tsx:681-703`, `phone_commands.cpp:232-236`                    |
| MTU                 | 247 negotiated, 244 usable JSON payload (`BLE_JSON_PAYLOAD_MAX`)                                                           | `configuration_global.h:358-369`                                       |
| Write frame         | `[len][opcode][payload]`, `len` = total length                                                                             | `BleHandler.ts:84-93,105-113`                                          |
| Notify frame        | `[flag][type][payload]`, flag 0x40 text/pos (type `:` `!` `@` or 0x41 ack), 0x44 JSON (`D` + register letter), 0x91 mheard | `MessageHandler.ts:1-7,71-100,935,977-985`, `phone_commands.cpp:47-70` |

Opcodes the current app actually writes (everything else in `phone_commands.cpp:307-668` is
firmware-only legacy and stays out of the golden corpus unless a decision says otherwise):

| Opcode | Frame                                              | Sent when                                 |
| ------ | -------------------------------------------------- | ----------------------------------------- |
| 0x10   | `04 10 20 30` or `23 10 20 30 <32 B SHA-256(pin)>` | once after `startNotifications` (hello)   |
| 0x20   | `06 20 <int32 LE unix seconds>`                    | time sync after connect                   |
| 0xA0   | `<n+2> A0 <utf-8 text>`                            | every text message and every `--` command |

### 12.2 Capture options considered

| Option | Mechanism                                                                                                                                                                              | Use in this plan                                                                                                          |
| ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| A      | Mac host client (Python, `bleak`): scan by NUS UUID, connect, pair via the macOS passkey dialog once per node, subscribe, replay the write corpus, record every notification           | **golden driver for H4**, deterministic and scriptable                                                                    |
| B      | Real iPhone app with Apple's Bluetooth logging profile installed; `sysdiagnose` yields a PacketLogger `.pklg`; ATT writes and notifications extracted with Wireshark's btatt dissector | **one-time calibration**: proves that A sends the same bytes the app sends and that the node answers the app the same way |
| C      | macOS PacketLogger (Xcode Additional Tools) on the Mac while A runs                                                                                                                    | debugging aid when A and the node disagree on MTU or pairing                                                              |
| D      | Instrumented app build with a frame logger, sideloaded with a development profile                                                                                                      | fallback if A cannot pair on macOS; changes the app, so last                                                              |

Option A is the plan. The user can install profiles on the iPhone, so B is available for the
calibration run without app changes.

### 12.3 Host client specification (planned: `tools/bench/ble_golden.py`)

- Arguments: node name or address, PIN (optional), corpus directory, output directory, listen
  seconds after the last write.
- Sequence: connect, subscribe to `6e400003`, send hello (with PIN hash if given), send timesync
  with a **fixed** timestamp from the corpus (not `now`, for reproducibility), then each corpus
  line as an `0xA0` frame with the same gap the app uses (`INIT_CONN_WAIT` 2 s after connect,
  then one write per 500 ms), then listen.
- Records: every notification as `<uint16 len><bytes>` in `ble-frames.bin` plus a hex text
  rendering `ble-frames.txt` with one frame per line and the flag/type decoded, and every write
  with its timestamp in `ble-writes.txt`.
- Corpus (`test/golden/corpus/ble/writes.txt`): hello, timesync, the 13 register requests
  (`--info`, `--seset`, `--wifiset`, `--nodeset`, `--analogset`, `--wx`, `--pos`, `--io`,
  `--tel`, `--aprsset`, `--conffin`, `--mheard`, `--path`), one text message to group 9
  (bench rule: never `*`), one direct message to a bench callsign, and the adversarial prefix
  set from the command golden.
- Normalization: the 0x44 JSON frames go through the section 7 rules; 0x40 frames mask the
  4 timestamp bytes and the msg_id; 0x91 mheard frames mask time and RF fields.
- Pass criteria at G1 and G2: identical frame sequence after normalization, identical frame count,
  identical per-frame length. Length is asserted separately because the MTU budget
  (`BLE_JSON_PAYLOAD_MAX 244`) is a wire contract.
- Exclusions: firmware opcodes 0x50..0xF0 are not driven; they are not on the app's path and the
  audit marks them as a separately validated legacy surface.

### 12.4 Calibration run with the real app (option B, once at G0)

1. Install Apple's Bluetooth logging profile on the iPhone, reproduce the connect and register
   sequence in the app against RAK-90 and Heltec-93.
2. Trigger `sysdiagnose`, transfer to the Mac, open the `.pklg` in PacketLogger or Wireshark.
3. Extract the ATT write values and notification values for the NUS characteristics into the same
   `ble-frames.txt` format.
4. Diff against the option A capture of the same sequence. Differences in write bytes are a bug in
   the client; differences in notification bytes are timing or state and must be explained.
5. Commit both under `test/golden/hw/G0/<node>/ble-app-calibration/`.

### 12.5 Practical constraints on the Mac

- The node holds one connection: the phone must be disconnected before the client connects, and
  the client must disconnect cleanly before any phone test.
- macOS handles passkey pairing with a system dialog; the bond persists per node MAC. A firmware
  erase invalidates it and the node must be removed from the Mac's Bluetooth list, the same
  symptom the app reports as "removed pairing" (`Connect.tsx:748`).
- Random address rotation is not used by the firmware; scanning by the NUS service UUID plus the
  advertised name is enough to select the node.
- `bleak` on macOS needs Bluetooth permission for the terminal application on first use.
