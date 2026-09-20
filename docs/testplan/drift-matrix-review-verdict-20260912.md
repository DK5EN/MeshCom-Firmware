# Drift-matrix decisions DR-01..DR-29 — Fable review verdict (2026-09-12)

Reviewed: `docs/testplan/drift-matrix.csv` at `07478ff2` (all 29 rows decided). Eight finder
angles (cross-row dependencies, U1 claims, U2 ring policy, C4/U3/U6 claims, test-suite audit,
verification gates, wire/consumer compatibility, altitude/scope). Every load-bearing claim below
was re-derived against the tree by the session model; refuted finder claims are listed at the end
so they are not re-found.

Verdict in one line: the per-row facts are sound (26 of 29 rows verified line for line), but four
decisions rest on a wrong premise (DR-24/21, DR-18, DR-03/15, DR-20) and the cluster DR-21..26 is
under-specified in ways that would ship a policy that does not do what the rows say. Re-decide
those; the rest need bookkeeping.

## A. Decisions to re-decide

### Finding 1 — DR-24 / DR-21: the ESP32 failure mode the spec is written against does not exist

- **File:** `src/esp32/udp_drain_esp32.cpp:69-99`, `src/udp_functions.cpp:225-233`,
  arduino-esp32 `WiFiUdp.cpp:178-205`
- **Severity:** critical (the `spec` field is the acceptance criterion)
- **Evidence:** `WiFiUDP::write()` appends to a 1460-byte buffer and returns the byte count; it
  cannot fail for `msg_len > 0`. The real send is `endPacket()` (`sendto`). Its result lands in
  `tx_ok` at `:93`, is logged and counted by `udpCountTx()`, then discarded. `err_cnt_udp_tx` is
  never incremented on a real failure, `resetMeshComUDP()` never fires on one, and the slot is
  advanced and dropped at `:146`. Even the write-fail branch cannot wedge: `err_cnt_udp_tx = 0` at
  `:85` precedes the `return` at `:88`, so the next pass drops the slot. The twin's own assertion
  proves it (`test_udp_send_twin.cpp:539`: `udpRead == MAX_ERR_UDP_TX-1`, nine of ten dropped)
  while its comment says "wedge".
- **Today, correctly stated:** ESP32 drops on every real failure with no counter and no reset.
  nRF52 drops on every real failure, counts, and resets DHCP after 10 (W5100S `endPacket()` does
  fail on ARP timeout, `socket.cpp:518-524`). Both drop; only the counting differs.
- **Consequence:** "infinite retry vs immediate drop" in DR-24's `spec`, DR-21's text and the
  twin comment is false. Before any retry policy can work on ESP32, the failure predicate must
  move onto `endPacket()`; the twin's failure knob (`g_write_fails`, `:161-172`) fails write and
  end jointly, so no test today covers the only real ESP32 failure.

### Finding 2 — DR-21 / DR-24: cap-3 retries change the unit of MAX_ERR_UDP_TX

- **File:** `src/configuration_global.h:243`, `src/nrf52/udp_drain_nrf52.cpp:54-65`
- **Severity:** high
- `err_cnt_udp_tx` counts failed attempts. With three attempts per slot the DHCP reset fires
  after ~3.3 frames instead of 10. On nRF52 `resetDHCP()` blocks inside `bSPI_ETH_Active`
  (`gateway_service_nrf52.cpp:30,43`), deferring the radio re-arm, the cost DR-14 chose to avoid.
- **Fix:** count drops (at the cap), not attempts, or scale the limit. DR-21's "reuse
  err_cnt_udp_tx" for retry visibility is not viable: it is the recovery trigger and is zeroed by it.

### Finding 3 — DR-03 / DR-15 / DR-21: nRF52 `hasIPaddress` is not a link signal, and the only path that clears it on a cable pull is the drain's error limit

- **File:** `src/nrf52/nrf_eth.cpp:101-121` (ethLinkPoll), `:169`, `:281`, `:294`, `:567`;
  `src/nrf52/udp_drain_nrf52.cpp:59`; `src/nrf52/gateway_service_nrf52.cpp:66-68`
- **Severity:** high
- `ethLinkPoll()` reads the PHY and updates `s_ethLinkState` only. `hasIPaddress` goes false at
  five sites, all failure paths: `ethDrop()` (a command), two DHCP start failures, the
  `resetDHCP()` retry, and the drain's `MAX_ERR_UDP_TX` branch. The gateway recovery branch at
  `:66` requires it already false. So on a cable pull the recovery is reached only after 10
  failed sends, i.e. through the code Finding 2 changes.
- **Consequences:** (a) DR-03's ported "link down / link up" diagnostic must read
  `s_ethLinkState`, not `hasIPaddress`, or it reports "link up, server unresponsive" on a dead
  cable. (b) DR-15's "same policy as ESP32" holds only via this indirect path. (c) Re-basing the
  error limit changes how fast a cable pull is detected. The three decisions are coupled and
  none says so.
- **Also owed by DR-03 (wire finder, verified):** keep the literal `[UDP] Heartbeat timeout`
  prefix, or `tools/serial_monitor.py:73` (`RE_HB_TIMEOUT`) never raises SERVER UNREACHABLE on a
  RAK; the ESP32 lines print `%lus` unkeyed (`gateway_service_esp32.cpp:39,60,66`) and
  `normalize.py:82` masks only `hb_age_s;N`, so emit the keyed form; use raw `Serial.printf`
  rather than `printfdeb()` if the line carries `;`; and `printfdeb()` carries ~900 B of locals
  (`printfdeb_functions.cpp:82,86`) on a loop task measured at 276 B minimum free
  (`docs/bench-extudp-regression.md:335`), which DR-03, DR-09 and DR-21 all add to without a
  re-measure.

### Finding 4 — DR-18: as written it ships nothing, and the contract it says is unwritten exists

- **File:** `src/extudp_functions.cpp:501,600,664-669`; `docs/ack-wer-hat-quittiert.md:280-304`;
  MCProxy `src/mcapp/udp_handler.py:199-240,567`
- **Severity:** high
- `sendExtern()` has exactly two payload branches (0x21 at `:501`, 0x3A at `:600`) and
  `else return;` at `:664-665` before `beginPacket()` at `:669`. Widening either handler's gate
  to 0x41 (or 0x40, already in ESP32's triple) adds a call that returns four statements later.
  The row's "an EXTUDP app sees ACKs from an nRF52 gateway and not from an ESP32 one" is false:
  both emit nothing. The asserting test drives type 0xFF garbage through a stubbed `sendExtern()`
  (`test_udp_frame_twin.cpp:748`), the DR-07 shape the row itself warns about.
- The outbound ACK contract is already specified in `docs/ack-wer-hat-quittiert.md` §6.3 as a
  `{"type":"ack",...}` JSON emitted via `queueExtern()` at the BLE-ack call sites, and MCProxy has
  accepted that shape since 2026-09-05. Routing a binary 0x41 through `sendExtern()` conflicts
  with it; `decodeAPRS()` returns 0x41 before parsing anything (`aprs_functions.cpp:131`), so a
  copied 0x3A branch would emit an empty identity.
- A node never uploads an ACK to the server (`handleACK()` returns before `addNodeData()` at
  `lora_functions.cpp:1415`), so a GATE-wrapped 0x41 is a bench artefact, not field traffic.
- **Neighbour bug, outside the matrix, in the block DR-18 touches:** a 0x3A frame with
  destination path `100001` (telemetry text) builds no JSON, skips the `else return;`, reaches
  `UdpExtern.write(c_json, 0)` at `:685`, which returns 0 and triggers `resetExternUDP()`. Any
  `--extudp on` node that hears a telemetry text frame (`queueExtern("lora")` at
  `lora_functions.cpp:950`) tears its EXTUDP socket down. Code-read only; not bench-confirmed.

### Finding 5 — DR-20: the nRF52 shape being mirrored is misidentified

- **File:** `src/nrf52/nrf_eth.cpp:442-465`
- **Severity:** medium
- The DHCP reset on "too many zeros" is in `NrfETH::getUDP()`, the socket-read wrapper
  (analogue of `getMeshComUDP()`), not in "sendUDP()/gateway loop". `getUDP()` returns 1 for both
  "no packet" and "too many zeros", so the drain runs right after a reset. The cited evidence
  lines (`udp_frame_nrf52.cpp:456,458`) show only the returns.
- **Fix:** the W6 contract should read: handler returns status, the read wrapper acts on it
  (`getMeshComUDP()` on ESP32), not `gatewayService_esp32()`.

### Finding 6 — DR-28: the spec cannot be implemented as written

- **File:** `src/mheard_functions.cpp:35-56,227-235,274,690,765,909`; `src/lora_functions.cpp:874`
- **Severity:** high
- Eight slot-parallel arrays; persistence writes them as raw blocks per array; `updateMheard()`
  writes by index from `OnRxDone` (the LORA task on nRF52) while `sendMheard()` walks from the
  loop task, no lock. "Sort on `mheardMillis[]` descending" read as an in-place sort corrupts
  the table and the saved image. A third renderer, `showMHeardTDECK()` at `:909`, is not named.
- **Fix:** reword the spec to "render in `mheardMillis[]` order by sorting a local index array
  (`uint8_t[MAX_MHEARD]`); storage arrays are never permuted; all three renderers". Consumers
  (app `MessageHandler.ts:1188-1206`, MCProxy `ble_protocol.py:998-1075`) key on `CALL`, so the
  order change is display-only.

### Finding 7 — DR-06: the accidental RX-01 protection covers relay only

- **File:** `src/esp32/udp_frame_esp32.cpp:168-177`
- **Severity:** medium
- A GATE frame with raw first byte 0x21 that `decodeAPRS()` rejects still runs
  `sendDisplayPosition()` on a zeroed struct and inserts msg_id 0 into the dedup ring; neither is
  gated by `bSrcUnconfigured`. Verdict stands; the owed test must assert display and ring-insert
  suppression, not only `bUDPtoLoraSend`.

### Finding 8 — DR-26: "cosmetic" hides a gating change

- **File:** `src/nrf52/udp_drain_nrf52.cpp:88-100` vs `src/esp32/udp_drain_esp32.cpp:130-133`
- **Severity:** medium
- On nRF52 `bDisplayVia` is tested first and bypasses `bDisplayInfo`; on ESP32 the print is
  gated on `bDisplayInfo` alone. Adopting "nRF52's form" gives ESP32 a line with `--via on` even
  when `--info` is off. The shared LoRa precedent (`lora_functions.cpp:1108,1122`) treats via as
  an additive extra line, so neither existing shape is the obvious target. Also: no G1 capture
  contains `TX-UDP`, so the EXPECTED-DIFF entry the row promises predicts a diff that cannot occur.

## B. Co-dependencies not recorded

### Finding 9 — DR-07, DR-18, DR-19 rewrite the same call site without cross-reference

`src/nrf52/udp_frame_nrf52.cpp:98-99`, `src/esp32/udp_frame_esp32.cpp:98-112`. DR-18 lifts the
forward out of the relay branch; DR-19 removes the cast on that line; DR-07 ports an outer check
into a branch DR-18 dissolves. Apply as one edit; DR-07 is moot after DR-18 unless restated.
Lifting must stay ahead of `is_new_packet()` (`:146`) or duplicates stop reaching EXTUDP, a
change no twin case would catch.

### Finding 10 — five decisions converge on one ~50-line block; only three are bundled

DR-21/22/24 declare "one change"; DR-25 (leak counter, the if-branch) and DR-26 (print prefix,
the else-branch) edit the same if/else in `udp_drain_{esp32,nrf52}.cpp` and name only DR-02 or
nothing. Bundle all five.

### Finding 11 — DR-22 + DR-25 + DR-21 as written produce per-attempt log lines and a 3x leak count

The decode/print/leak block runs once per pass; with retries a frame passes up to three times.
ESP32 already counts a "leak" for a frame whose send failed (decode at `:115` is after the failure
branch); DR-22 spreads that to nRF52, the outcome DR-25 rejects. Fix: one drop event at the cap,
leak check after final disposition. After DR-22, `TX-UDP` in a log means "encoded", not "sent".

### Finding 12 — DR-21 prerequisites missing from the decision

- The nRF52 reader takes no lock (`udp_drain_nrf52.cpp:43,107`, `/*BISECT*/`) while the writer
  does (`udp_functions.cpp:1197-1216`); a cross-pass per-slot counter needs the lock first.
- `addRingPointer()` excludes `"udp"` from its RING_OVERFLOW log (`loop_functions.cpp:5772`), so
  the loss that moves to the writer is silent by construction.
- Precondition (1) on nRF52 is unreachable: the only caller already gates on
  `neth.hasIPaddress` (`gateway_service_nrf52.cpp:28-34`); the AP-mode analogue is vacuous.
- `MAX_RING_UDP` is 10 on `ENABLE_TBEAM` (`configuration_global.h:224`), not 20.
- The counter must be cleared when the writer reuses a slot (CONC-16 eviction); the twin cannot
  observe that: `addUdpOutBuffer()` is not in `native_udp_send_twin`'s source filter, and
  `drain_all()` caps at 40 passes (`:276`), fewer than a 19-slot ring needs at cap 3.
- The cap is in loop passes, sub-millisecond on ESP32, so "a transient outage retries" is not
  what an attempt cap delivers; a time-based cap would.

### Finding 13 — DR-03's widened scope has no wave and lands in the D1-09 exclusion

`gateway_service_nrf52.cpp` is the file D1-09 owns, listed in audit §7.2 as left out with a 24 h
soak gate. DR-15 says "must not survive as a phantom item in W3"; W3 is the settings wave. No wave
owns the C4 diagnostic port. Likewise DR-16 lives in the main loops (D1-10, also left out), and
the six U2 drain rows DR-21..26 are in no wave at all: §7.1 wave 6 is the frame handler.

## C. Gate contract

### Finding 14 — §9.3 invariants contradict DR-12/DR-13, and a frozen BLE contract is in no gate

- §9.3 demands "ESP32 struct layout identical; nRF52 appended fields only; NVS key set
  identical". The D1-04 target's acceptance criteria (`BACKLOG.md:4395` and the zero
  esp32-only/nrf52-only rule) demand a merged, reorderable struct. Nothing says which wins.
- `src/nrf52/nrf52_ble.cpp:297,302,339,387,390` ship `sizeof(s_meshcom_settings)` raw bytes as
  the settings characteristic and reject a write of any other length. A struct merge breaks the
  phone app's settings read and write. Not in §9's invariants, not in any H-step, not in the
  BLE corpus.
- Measured: 132 NVS keys, 109 X() keys, 25 NVS keys with no X() row (the whole T-Deck UI state
  and the sensor cache), 2 X() keys with no NVS key; `send_repeat_time` is 16 characters against
  ESP-IDF's 15-character NVS limit.
- §9.4 tests an upgrade from 20260724, which is read through the live struct guarded by a
  `sizeof` check that wipes (`nrf52_flash.cpp:313-341`). The compat struct DR-13 retires
  (`0x57`) is an older generation. After W3 the image needs a frozen copy of today's layout, the
  thing the target forbids, and the migration code must stay in the image indefinitely.

### Finding 15 — G2 has no baseline for six of eleven H-steps, and the expected-diff chain has no artefacts

- G1 captured only H4, H6, H8 (and H11); `protocol-after.md` pre-seeds H1/H2/H3/H5/H7/H9 and
  rules that a cell compared to anything but G1 is not a G2 result. §9.2 is unsatisfiable there.
- No `<uut>-twin-diff-before.txt` or `-after-expected.txt` exists for U1/U2 (20 of 29 rows);
  nothing reads `after_expect` except the enum check in `drift_matrix_lint.py`. There is no
  defined way to flip a drift test into an agreement test.
- Twelve `asserting_test` cells name characterization tests that fail when the decision is
  implemented (DR-02, 04, 05, 07, 18, 19, 20, 22, 24, 25, 26, 28); ten rows have none (kept green
  by `--phase implementation`). Three C4 rows (DR-03, 14, 16) have no seam at all.
- DR-28 flags "H3 on every node", which has no G1 baseline; the BLE `--mheard` reply is excluded
  as volatile. DR-26 flags a diff no capture can show. DR-18, if implemented with a real branch,
  moves `extudp-received.txt` on all four nodes and is not flagged. No path or owner for the G2
  `EXPECTED-DIFF.md`.

## D. Bookkeeping

### Finding 16 — BACKLOG.md contradicts five decided rows

OPT-D5 and OPT-D6 still "BUG, open, needs soak (D1-09)" (DR-14/15 rejected the premise);
OPT-D9 "open" (DR-16 scoped it); OPT-D15 "OPEN, owed a decision" (DR-27 decided never); OPT-D16
"OPEN" (DR-28/29 decided). The Phase C stand (`BACKLOG.md:4665+`) still describes the matrix as
pre-M2.

### Finding 17 — column consistency

- `tie_break` is populated on 20 of 21 non-both-valid rows against its own §5.1 rule; on DR-06,
  DR-20, DR-26 it says ESP32 beside `nrf52-correct`.
- DR-18, DR-21, DR-25 are tagged `esp32-correct` while ESP32's own behaviour is changed or
  rejected; DR-21 and DR-24 describe one change under two verdicts.
- `class`: DR-27 `platform-api` for shared code (DR-28/29 say `cosmetic`); DR-03 `platform-api`
  for a ported feature stage.
- DR-20's tally of `src/udp_frame.h:33-41` credits DR-02/05/20 for bullet 4, which is DR-18's.
- DR-27's "4/4 mutations caught" has no artefact in the tree.
- `test/test_udp_frame_twin/stubs/nrf_eth.h` says the twin is not in `twin_stub_lint.py`; it is.

## E. Scope observation (operator decision, not a defect)

20 of 29 rows change behaviour on at least one platform, 7 on both. DR-21/22/24/25, DR-18,
DR-03, DR-28 and DR-12/13 are new design, not a pick between two existing behaviours. The PR's
identity drifts from "Vereinheitlichung" toward a behaviour bundle that Kurt reviews personally.

## Decisions taken 2026-09-12 (operator interview after the review)

| Question                                  | Decision                                                                                                                                                                                                                                     |
| ----------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DR-21/DR-24 ring policy                   | Narrow to parity: ESP32 keys on `endPacket()`, counts, resets, drops (nRF52's shape); no retry counter; nRF52 gets the unresolved-destination return only; `udp` ring overflow un-silenced. DR-24 becomes `nrf52-correct` / `esp32-changes`. |
| DR-18 EXTUDP ACK                          | Shape parity (three types, gate-then-forward on both) plus the section 6.3 JSON ack via `queueExtern()` at the BLE-ack sites, in W6. One edit with DR-07 and DR-19. EXT-01 bench-confirmed first, then fixed in W6.                          |
| DR-03 heartbeat diagnostic (DR-15 folded) | Keep, corrected: reads `s_ethLinkState`, keeps the `[UDP] Heartbeat timeout` tag, keyed `hb_age_s`, raw `Serial.printf`, watermark re-measured. Own mini-wave C4-diag after W6 with DR-14's comment and DR-16, exempt from the D1-09 soak.   |
| DR-26 TX-UDP print                        | Info gates, via decorates, on both drains (`both-wrong`, `both-change`).                                                                                                                                                                     |
| Settings gate (DR-12/13 vs testplan 9.3)  | Section 9.3 rewritten to the D1-04 target; the nRF52 BLE settings characteristic becomes a frozen byte-compare contract served from a versioned snapshot; 9.4 records the frozen-copy and permanent-migration consequences.                  |
| PR shape                                  | One-shot PR kept; the German description presents the behaviour changes as such.                                                                                                                                                             |
| Gate tooling                              | Lint extended now (verdict/after_expect coherence, tie_break rule, G2 EXPECTED-DIFF coverage); G2 file seeded; fixture generator and drift-to-agreement flip filed under M3.                                                                 |

Applied the same day: CSV rows DR-03, 05, 06, 07, 09, 12, 13, 14, 15, 16, 18, 19, 20, 21, 22,
23, 24, 25, 26, 27, 28, 29 (`tie_break` blanked everywhere); BACKLOG OPT-D5/D6/D9/D15/D16 and
the new EXT-01; testplan 9.2-9.4; `drift_matrix_lint.py`; `test/golden/hw/G2/EXPECTED-DIFF.md`;
the misleading comment in `test_udp_send_twin.cpp` and the stale one in
`test_udp_frame_twin/stubs/nrf_eth.h`. Not applied (implementation-time): `src/udp_frame.h:33-41`,
`docs/ext_udp_telemetry.md`, `docs/ack-wer-hat-quittiert.md:266`, `docs/architecture/11-wire-format.md`.

## Refuted claims (do not re-investigate)

- "ESP32 wedges forever on a permanently failing slot" (DR-24 spec, twin comment): refuted,
  Finding 1.
- "An EXTUDP app sees ACKs from an nRF52 gateway" (DR-18): refuted, `sendExtern()` has no 0x41
  branch.
- "No written outbound EXTUDP contract" (DR-18): refuted, `docs/ack-wer-hat-quittiert.md` §6.3.
- "OPT-D15 / OPT-D16 were updated in BACKLOG.md" (altitude finder): refuted, `BACKLOG.md:4639-4640`.
- "drift_matrix_lint only checks the directory exists" (review brief): refuted, it indexes case
  names (`build_case_index`, `:124`).
- "hb_age is already masked by normalize.py" would be wrong: only the keyed `hb_age_s;N` form is.
- DR-06 empty-call coupling: confirmed (`initAPRS()` first, `String` members default empty,
  `isUnconfiguredCall("")` true).
- DR-07 no observable difference: confirmed (`extudp_functions.cpp:453-458`).
- DR-08, DR-09 (base 7 bytes identical, both consumers parse n>0), DR-10, DR-11, DR-14 SPI
  deferral, DR-16 ordering and 15 s cost, DR-17, DR-27 (no runtime path sets SUB; the decoder's
  0x7E to '#' is the real guard), DR-29 (MCProxy fails closed on a non-numeric PLT): all
  confirmed as decided.
