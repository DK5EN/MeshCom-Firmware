# Test-Suite Audit — Fable Verdict (2026-08-22)

Scope: all native Unity suites, `test/support/` shims, `tools/mock/` tests.
Six finder angles (cannot-fail, shim fidelity, golden fence, isolation,
mock circularity, coverage lies), every load-bearing claim adversarially
verified against the code before it landed here.

> **STATUS: RESOLVED 2026-08-22.** Both fix waves implemented, independently
> advisor-reviewed (Fable tier, APPROVED — byte literals recomputed by hand,
> gate re-run fresh, non-regression vs N-24/golden confirmed), committed.
> Advisory notes from the review (no rework required) are recorded at the
> bottom.

## Wave status

- Wave F1 (C++ suites + shims + txring clamp): done, advisor-APPROVED
- Wave F2 (tools/mock): done, advisor-APPROVED

## Advisor notes (post-review, informational)

- The txring len clamp rejects silently; a debug log would aid diagnosis
  of the (currently unreachable) path.
- `APRS_GOLDEN_UPDATE=0` still counts as update mode (getenv != nullptr) —
  harmless since update mode now fails loud.
- The micros() shim does not wrap like hardware micros — pre-existing.
- Firmware CONF parser assumes shortname TLV precedes lat/lon/alt
  (nrf_eth.cpp:558 offset arithmetic) — pre-existing quirk, doc-11 note
  candidate.

## Finding 1: APRS_GOLDEN_UPDATE neuters the fence silently

- **File:** test/test_aprs_corpus/test_aprs_corpus.cpp:139–163
- **Severity:** high
- **Failure scenario:** the env var leaks into a dev shell or CI matrix →
  golden.txt is regenerated from the current decoder output and compared
  against itself → the differential fence reports green forever while also
  rewriting the checked-in golden file.
- **Fix:** in update mode, write the file and then `TEST_FAIL` with an
  explicit "golden regenerated — review the diff, re-run without the env
  var" message. A regeneration run must never be green.

## Finding 2: canonical()/canonicalCore() field gaps blind the fence

- **File:** test/test_aprs_corpus/test_aprs_corpus.cpp:82–105
- **Severity:** high
- **Failure scenario:** a decoder change to `msg_app_offline` (live in
  corpus frames f004/f005), `msg_source_call`, `msg_source_last` (separate
  comma-split algorithm from msg_source_path) or `msg_last_path_cnt`
  passes the differential fence unseen. Roundtrip: an encoder regression
  in byte-5 flags (server/track/app_offline/hop) or the destination call
  passes canonicalCore.
- **Fix:** extend canonical() with appoff/srccall/srclast/pathcnt
  (deliberate golden regeneration, diff reviewed); extend canonicalCore()
  with hop/server/track/appoff/dstcall. The mesh bit is deliberately
  masked in the roundtrip: `encodeAPRS` stamps it from the global `bMESH`,
  not from the struct (documented semantic, doc 11 §1.2) — comment this.

## Finding 3: addTxRingEntry trusts caller-side length bounds

- **File:** src/txring_functions.cpp:241–243 (memcpy), slot [0] uint8 store
- **Severity:** medium (verified NOT currently reachable: every caller is
  bounded ≤ 255 via encodeAPRS's internal clamp, the radio's 255-byte RX
  max, or the uint8 slot-length source in retransmit — but the invariant
  lives entirely in the callers and is unchecked at the single choke point)
- **Failure scenario:** a future caller passes len > 255 → memcpy past the
  slot into the neighbor slot, and `(uint8_t)len` truncation can disguise
  the entry as short/empty.
- **Fix:** reject len == 0 or len > UDP_TX_BUF_SIZE at entry (return -1
  before touching the ring); test both edges.

## Finding 4: BOARD_HARDWARE ODR violation across native test TUs

- **File:** test_aprs_decode.cpp:27, test_aprs_corpus.cpp:38,
  test_aprs_spec.cpp:36, test_hey_report.cpp:26 (uint8_t) vs
  loop_functions_extern.h:21 `extern int` read by aprs_functions.cpp:111
  which is linked into every native_aprs binary
- **Severity:** medium (UB, currently masked by uint8_t assignment targets)
- **Fix:** define `int BOARD_HARDWARE = 9;` in all four files (as
  test_txring already does).

## Finding 5: mock CONF test is a tautology; DATA header never checked against a literal

- **File:** tools/mock/test_mock_server.py:263–272 (received == sent, both
  the same bytes object round-tripped over loopback); :185–211 (DATA tests
  build via mock_client and parse via the server — encoder/decoder can
  drift together)
- **Severity:** high (for the mock's fitness as a test double)
- **Fix:** assert the CONF datagram against a hand-written literal byte
  string derived from doc 11 §2.2 arithmetic; add one test with a
  hand-written 36-byte DATA header literal (and one hand-written BEAT
  literal already exists — keep). Registry expiry (`_expire_clients`) gets
  a real test with a short TTL.

## Finding 6: millis() shim cannot roll over

- **File:** test/support/Arduino.h:48 (`unsigned long` = 8 bytes native)
- **Severity:** medium
- **Failure scenario:** test_millis_rollover claims the shim models a real
  2^32 wrap; it does not — passes come from TEST_ASSERT_EQUAL_UINT32
  truncation. Any future natively-tested deadline logic using
  `unsigned long` near the wrap would be tested wrongly.
- **Fix:** make the shim clock uint32_t so the wrap is physical; re-run
  the rollover suite.

## Finding 7: corpus loader silently truncates at MAX_FRAMES

- **File:** test/test_aprs_corpus/test_aprs_corpus.cpp:107,120
- **Severity:** low (13/64 used today)
- **Fix:** fail the test if corpus.txt contains more lines than MAX_FRAMES;
  resolve corpus.txt and golden.txt via the SAME discovered prefix (single
  openRel resolution reused) so they can never come from different roots.

## Finding 8: minor hygiene

- test_regex_call leaves the millis shim at 1500 (restore at test end).
- mock server stop() latency: 0.5 s recv-poll × ~12 instances ≈ the whole
  suite runtime; drop poll to 0.1 s.
- README.md overclaim ("matched firmware byte-for-byte" for BEAT — firmware
  parses only the 4-byte indicator): soften to name mc-chat as the shape
  source.
- String::substring(from>to): shim returns "", real Arduino swaps bounds —
  align the shim (landmine, no live caller).

## Documented risks, no code change (with reasoning)

- **test_compress/**: not a test (no unity, no main, in no test_filter) —
  upstream-deposited file; inert by design, left untouched (minimal-diff
  rule vs upstream).
- **test_external_radio_txq mirror logic**: the EXT_PENDING-skip tests
  exercise a hand-copied mirror of getNextTxSlot()/
  updateRetransmissionStatus(), not the production code — upstream-authored
  suite; noted as risk. The txring extraction now makes the real
  getNextTxSlot natively linkable, so a future pass can rewire the suite;
  not done here to avoid rewriting upstream test files in a QA wave.
- **Mock server routing policy** (DATA→GATE broadcast to all others) is an
  assumption beyond doc 11 §2 (which specifies wire shape, not routing);
  README documents it as such. Real-server routing semantics (dedup, group
  filtering) remain unmocked — acceptable for a wire-shape double.

## Refuted claims (do not re-investigate)

- "addTxRingEntry len overflow is reachable today" — refuted: all callers
  bounded ≤255 (encodeAPRS clamps at UDP_TX_BUF_SIZE internally +
  udp_functions.cpp:232 clamp; OnRxDone size ≤ radio max 255; retransmit
  len source is a uint8). Finding 3 keeps the defensive fix.
- "encodeAPRS mesh-bit from bMESH is a hidden bug" — refuted: documented
  semantic (doc 11 §1.2 states the encoder sets 0x10 from bMESH);
  fence-blindness remains Finding 2.
- "test/support/configuration.h pins the wrong board profile" — refuted:
  CONFIG_IDF_TARGET_ESP32S3 selects the same #elif branch as BOARD_RAK4630
  (configuration_global.h:131); ring sizes match the bench hardware.
- "ring_index_t native tests exercise the ESP32 struct variant" — refuted
  (earlier recon had it backwards): native takes the std::atomic branch,
  same as nRF52/RAK4631; the ESP32 plain-struct variant is the uncovered
  one, and divergences fail loud at compile time.
- "test_aprs_corpus file-scope frame arrays create order dependence" —
  refuted: guarded reload, read-only coupling, all RUN_TEST orders safe.
- "resetRing() misses txring globals" — refuted by cross-grep: the reset
  set exactly matches the globals the TU defines/mutates.
