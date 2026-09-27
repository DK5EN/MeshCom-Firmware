# Finder H — Test and Acceptance-Criteria Audit

Paper: MeshCom 5: Topologie als einzige Quelle (2026-09-24, feature-neighbour-matrix @ d9fc5c5a)

## Critical

### H1. "Replay byte-gleich zum dichten Build" cannot hold — capacity changes in the same step

- **Where**: §6 Rollout, Schritt 1 (Stufe 1): "Replay des Mitschnitts vom 24.09.: SNAP, ROW,
  NEED, CANCEL, REFUSE byte-gleich zum dichten Build".
- **Why it cannot pass as stated**: Schritt 1's own content column changes `NBR_MAX_ROWS` from
  13/21 to 64/128 in the same step. Row indices, `SNAP`/`ROW` dumps and `EVICT` occurrence are a
  direct function of capacity: `src/nbr_matrix.cpp` (`test_eviction_replaces_oldest_row_never_row_zero`,
  `test_eviction_prefers_two_hop_row_over_older_direct_rows` in
  `test/test_nbr_matrix/test_nbr_matrix.cpp`) shows eviction only fires once the fixed-size table
  fills up. At 64/128 rows on the very same 24.09. capture that filled 21 rows on the dense build,
  most evictions simply stop happening — rows that were replaced (and therefore absent, or present
  at a different index) in the dense build now persist, so `ROW` line counts per `SNAP`, row
  indices, and every mask (`NEED`/`CANCEL`/`REFUSE`, which are bitmasks _over row indices_) diverge
  by construction. This is not a corner case; it is guaranteed to diverge as soon as the input has
  more than `min(21, old_cap)` unique stations, which the 24.09. capture already does (Matrix
  21/21 Zeilen, live snapshot quoted in §2).
- **Contrast**: the source paper `~/Desktop/Nachbarschaftsmatrix-Kantenpool.html` makes the
  _same-sounding_ claim ("Replay des Feldlogs: SNAP-, ROW- und NEED-Zeilen muessen byte-identisch
  bleiben") but only for its Stufe 1, which explicitly keeps `NBR_MAX_ROWS` unchanged (its capacity
  bump to 64/128 is a separate, later step, "Stufe 2, nur S3 und nRF52"). This paper's Rollout
  folds the representation change (edge pool) and the capacity change (64/128 rows) into one
  Schritt and then reuses the edge-pool paper's acceptance wording without adjusting for that.
- **No harness exists today**: `tools/` has no script that feeds a captured `[NBR]` log back into
  `nbrNoteFrame()`/`nbrNotePos()` and diffs the resulting log against a second run (checked
  `tools/nbrsnap.py`, `tools/nbrhopcheck.py`, `tools/nbrlog.py`, `tools/nbrrelay.py` — all of them
  _consume_ an existing `[NBR]` log, none of them _drive_ the matrix code from one). Host tests
  build the matrix from literal C++ calls, not from a replayed capture. So even the achievable part
  of this criterion (representation-equivalence at unchanged capacity) has no existing instrument;
  it would have to be built from scratch.
- **Suggested replacement**: split into two criteria — (a) at _unchanged_ `NBR_MAX_ROWS`, edge-pool
  vs. dense-matrix representations replay byte-identical (needs a new replay harness); (b) at the
  new 64/128 capacity, define equivalence as "every row/edge the dense build produced is still
  produced, plus rows the dense build had to evict" — a coverage superset check, not byte identity.
- **Severity**: Critical — this is the acceptance gate for Rollout Schritt 1 and cannot be satisfied
  as written.

### H2. String-scan for `mheard.dat`/`mhpath.dat` cannot fail on 30 of 32 envs

- **Where**: §6 Rollout, Schritt 4: "String-Scan: 'mheard.dat' und 'mhpath.dat' fehlen im Image".
- **Evidence**: `src/mheard_functions.cpp:249-285` (`saveMHeardPersistence()`,
  `savePathPersistence()`) — both string literals sit inside
  `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`. They already do not exist in the
  built image of any of the other 30 platformio.ini envs today, before a single line of this
  concept is implemented. The check can only ever fail on T-Deck / T-Deck Plus, and on those two it
  will pass trivially anyway because §3 says `mheard_functions.cpp` itself is deleted in Stufe 3 —
  the whole file, guard and all, is gone, so the string can never survive to be found. The
  criterion cannot distinguish "we correctly removed persistence" from "we scanned the wrong 30
  images." Same pattern as the `instrument-guard-swept-field-diagnostics` finding from an earlier
  campaign (INS-01: a compiled-out guard swallowed several flags across most envs).
- **Suggested replacement**: scan only the T-Deck/T-Deck Plus images (where the strings were
  actually reachable), and add a positive check that `topo.dat` (the new format) round-trips there
  instead of only a negative absence check.
- **Severity**: High.

### H3. "resource_watch zeigt die Nettowerte mit höchstens 64 B Abweichung" measures the wrong quantity

- **Where**: §6 Rollout, Schritt 4.
- **Evidence**: `tools/resource_watch.py` (`parse_usage()`, `format_notice()`) parses PlatformIO's
  whole-image `RAM: ... (used N bytes from M bytes)` / `Flash: ...` summary lines and diffs the
  _total_ used-byte count against a stored baseline. It does not read `nm` symbol tables and has no
  concept of "the mheard/pathtable/nbrMatrix/ringNeed/ringAlone symbol group" that Table 4.7's
  "Nettowerte" (-377/-4.011/-899/-2.651 B) is built from (§4.7: "Gemessen ist heute die Summe der
  Datensymbole aus nm"). A whole-image RAM delta bundles in every other change that happens to be
  in the same build: any unrelated global added/removed in the same PR, any compiler/toolchain
  version drift, and — per this repo's own memory note — a documented finding that two clean builds
  of the _same_ unchanged source already differ by ~10% of flash bytes from pure address-layout
  shift (non-reproducible builds), plus the `resource_baseline.json` staleness note (+4.2 kB/+3.3 kB
  RAM drift observed on an unchanged tree). A whole-image ±64 B gate is tighter than the tool's own
  documented noise floor for _flash_, and there is no equivalent noise characterization for RAM in
  this repo to say 64 B is even meaningful there.
- **Suggested replacement**: measure the specific symbol group with `nm` (as §4.7 itself does for
  the paper's own numbers) and diff that against the computed target, not the whole-image
  `RAM:`/`Flash:` summary. If `resource_watch.py` is kept in the loop, use its `regions` subcommand
  (per-segment, not per-symbol) only as a coarse backstop, not the primary acceptance instrument.
- **Severity**: High.

## Significant

### H4. Rollout Schritt 1's test list validates none of the new internals it gates

- **Where**: §6 Rollout, Schritt 1: "native_nbr_matrix, native_nbr_report, test_txring grün".
- **Evidence**: `src/nbr_matrix.h` today has `struct NbrRow { char call[NBR_CALL_LEN]; ...}` (plain
  char array, no callsign-word codec) and `struct NbrMatrix { NbrRow rows[...]; NbrCell
cells[...][...]; }` (dense N×N, no edge pool, no `NbrMask`, no `nbrExt`, no horizon
  `hzCall`/`hzEntry`/`hzMeta`). None of Stufe 1's new machinery (§4.1's 6-bit callsign packing into
  `uint64_t`, `nbrBitsGet()`/`nbrBitsPut()`, the edge pool `NbrEdge[E]`) exists anywhere in the
  tree yet, and none of the three named tests touches it — they exercise the _current_ dense-matrix
  code. Passing `native_nbr_matrix`/`native_nbr_report` unmodified is necessary (no regression) but
  proves nothing about the new callsign codec's round-trip correctness (collision on the 37-symbol
  alphabet, off-by-one on the `0`-terminator, a rufzeichen exactly 9 characters long, a `-` at
  position 1) or about mask arithmetic at `NBR_MASK_WORDS=2` (128-bit `popcount`, OR across words,
  the `static_assert` boundary itself).
- **Additionally**: `test/test_txring/test_txring.cpp` hardcodes `ringNeed`/`ringAlone` as
  `uint32_t` and asserts with `TEST_ASSERT_EQUAL_UINT32` (lines 68-69, 474-537) against literal hex
  constants. §4.2 explicitly turns these into `NbrMask` (struct with a `w[]` array). The existing
  test cannot pass unmodified once that type changes — it will fail to compile, not fail an
  assertion. The Rollout's content column for Schritt 1 does not list "rewrite test_txring for the
  new mask type" as a deliverable, so "test_txring grün" is silently assuming a rewrite the plan
  never names.
- **Suggested replacement**: add explicit new host tests before Schritt 1 closes: callsign
  encode/decode round-trip (including the boundary cases above), mask popcount/OR at
  `NBR_MASK_WORDS=1` and `=2`, and an edge-pool insert/evict test at forced-small capacity (mirroring
  how `native_nbr_matrix` already uses `NBR_MAX_ROWS=5` to exercise eviction cheaply). List
  test_txring's rewrite as an explicit Schritt 1 deliverable, not an implicit side effect of "test_txring grün".
- **Severity**: Significant.

### H5. "MH-JSON feldgleich zu updateMheard()" — DIST field already disagrees with itself today

- **Where**: §6 Rollout, Schritt 2: "MH-JSON aus derselben Rahmenfolge feldgleich zu
  updateMheard() bis auf die neuen Felder und die REP-Mischung B2".
- **Evidence**: `src/mheard_record.h:60-72` documents, in the current code, that
  `updateMheard()`'s _live_ JSON path emits `mh_dist` **raw** (`mhdoc["DIST"] = mheardLine.mh_dist`,
  a `double`), while the _buffered_ path (`mheardRecords[]`, read back for the list-on-connect) goes
  through `mheardRoundDist()` and stores it rounded to one decimal place — the comment literally says
  "die beiden Wege widersprechen sich also schon heute." The new topology computes DIST via a third,
  independent path (§4.3: haversine from `0,0001`-Grad-quantized 21/22-bit lat/lon, i.e. ~11 m
  quantization before any distance math, vs. the old code's full-precision `float lat, lon` in
  `NbrRow` today). A "field-equal" comparison test has three candidate reference values to match
  against (raw double, rounded-to-0.1, or none of the above) and no stated tolerance; as specified,
  a bit-for-bit or even 0.1-precision comparison will show real, expected deltas from quantization
  that are not implementation bugs.
- **Suggested replacement**: name which of updateMheard()'s two existing DIST paths is the
  reference, and state an explicit tolerance (e.g. ±15 m) that accounts for the 0.0001° quantization
  chosen in §4.3, not a byte/field-equality claim.
- **Severity**: Significant.

### H6. "MHeard-Menge in mindestens 99% der Minuten gleich" — no tolerance stated for the same DIST/PL/quantization deltas as H5

- **Where**: §6 Rollout, Schritt 3.
- **Evidence**: builds on H5. Schritt 2's criterion explicitly excludes "die neuen Felder und die
  REP-Mischung B2" from the field-equality bar; Schritt 3's "gleich" carries no such exclusion list
  at all, and doesn't say whether "gleich" means same _set of callsigns_ or same _field values_ per
  entry. Given H5, a literal field-level "gleich" will register DIST/quantization mismatches as
  failures unless explicitly scoped out, the same way Schritt 2 scoped out the new fields and B2.
- **Suggested replacement**: reuse Schritt 2's exclusion list explicitly ("gleich bis auf DIST
  innerhalb ±X m, neue Felder, REP-Mischung B2") rather than a bare "gleich".
- **Severity**: Significant.

### H7. "Echo-Quote mit Via mindestens 86%" has no defined instrument or log line

- **Where**: §6 Rollout, Schritt 6; §4.8 Echo-Kontrolle.
- **Evidence**: the 86% baseline (§4.8, "56 von 65 eigenen Positionen") was computed by hand from a
  raw serial mitschnitt over ~32.5 hours — there is no existing tool (checked `tools/nbrsnap.py`,
  `nbrhopcheck.py`, `nbrlog.py`, `nbrrelay.py`) that computes an echo quote, and §4.8's own Regeln
  never define a log line for the echo-check outcome (contrast with the rest of the paper and
  `docs/nbr-logformat.md`, where every other new mechanism — EVICT, EVICT-H, SYM, NEED/CANCEL — gets
  an explicit `[NBR]|...` line format). Without a logged per-frame "echo received / not received"
  event carrying enough identity to attribute it to Via vs. flood mode, Schritt 6's "Echo-Quote mit
  Via" can only be recomputed the same manual, unscripted way the 86% baseline was — which is not
  reproducible across reviewers and doesn't scale to a tool-gated acceptance criterion the way
  Schritt 1-4's other criteria do.
- **Suggested replacement**: define a `[NBR]|ECHO|<up>|<msg_id>|<ok|fail>|<via_used>` (or similar)
  log line in §4.8's Regeln and a companion script (parallel to `nbrhopcheck.py`) before Schritt 6,
  not after.
- **Severity**: Significant.

### H8. "eigene Positionen erreichen die Gateways laut mcmap ... wie in den 24 h davor" is an uncontrolled before/after comparison

- **Where**: §6 Rollout, Schritt 6.
- **Evidence**: the comparison window ("24 h davor") sits immediately after Schritt 5's already-new
  Stufe-3 firmware (symmetric NCNT, topology-only MHeard) went live on the same nodes, so the
  "before" period is not a stable Stufe-0 baseline — multiple mechanisms change between the two
  windows, not just `--via on`. RF conditions and neighbourhood mix on DK5EN-98's link
  (`dg0opk-mesh-findings`, `esp32-channel-util-inflated` memory notes) are already documented in
  this repo as noisy day-to-day confounds independent of firmware. And per the
  `interlink-receiver-anonymous-since-aug` memory item, mcmap's INTERLINK feed no longer names the
  uploading gateway for gateway-to-gateway links — that item is about link attribution, not
  necessarily about a single node's own beacon reaching _some_ gateway, but it flags that mcmap's
  visibility into this exact fleet has known, recent gaps; the paper does not name which mcmap
  query/table it will read gateway-arrival counts from, so it is unclear whether the signal it
  needs is even present.
- **Suggested replacement**: name the specific mcmap query/metric to be read, and treat the
  comparison as directional evidence only (not a pass/fail gate) given the confounds, or gate on a
  same-day A/B (via on vs. off, alternating) instead of a before/after window.
- **Severity**: Significant.

### H9. "NCNT gegen den Schattenwert plausibel" (Schritt 5) has no defined threshold

- **Where**: §6 Rollout, Schritt 5.
- **Evidence**: every other numeric acceptance criterion in the table has a number (99%, 64 B, 86%,
  "höchstens 64 B"); this one says "plausibel" with no defined tolerance, unlike Schritt 3's
  analogous MHeard criterion which at least states 99%. Given §4.5 itself documents that the new
  NCNT (`D60 & (HM | SYM)`) is a _different quantity_ than the old one and will differ from the
  Schatten value (computed during Stufe 2 against the _old_ `getMheardCount()` semantics, per the
  Schritt 2/3 criteria) by design once SYM starts contributing — "plausibel" gives no way to fail
  this step, since any value can be argued plausible after the fact.
- **Suggested replacement**: define a numeric band (e.g., "within ±1 of D60∩HM, with SYM-only
  additions logged and counted separately") mirroring the rigor used elsewhere in the same table.
- **Severity**: Moderate.

## Minor / worth flagging

### H10. §5 Fehlerszenarien: two rows have no detection instrument, only "keine" reaction

- **Where**: §5, rows "Altknoten mit Präfix des Via-Rufzeichens" (Erkennung: "zusätzliche
  Wiederholung im Log", Reaktion: "keine") and "Gateway-Nachbar mit überhöhtem #X durch
  Serverzufuhr" (Erkennung: "#X gegen Reichweite", Reaktion: "keine"). Both detection columns
  describe a human-in-the-loop read of a log or dashboard, not an automated check, and unlike
  EVICT/SYM/DROP elsewhere in the same table, no `[NBR]` line is defined for either condition. Not
  necessarily wrong to leave unmitigated, but the "Erkennung" column implies an instrument that
  doesn't exist, and a real occurrence of either scenario would pass every automated Rollout gate
  silently.
- **Severity**: Minor — worth a note, not a blocker.

### H11. Rollout Schritt 1's "alle 32 Umgebungen bauen" gate is sound but should be read against a known baseline caveat

- Per the repo's own memory (`forkmain-pre-existing-red-envs`), `t5_epaper` and
  `esp32-external-radio` already fail to build at unchanged HEAD. "Alle 32 Umgebungen bauen" as an
  acceptance bar should be checked against that pre-existing baseline (do these two already-red
  envs count against Schritt 1, or are they carried as known-bad going in?) — not a defect in the
  paper, just a gap the reviewer running Schritt 1 needs to know before treating a 30/32 green count
  as a fail.
- **Severity**: Minor.

## Criteria that look sound

- **Schritt 1 "alle 32 Umgebungen bauen"** (modulo H11's baseline caveat) — a real, cheap,
  unambiguous gate; can genuinely fail.
- **§5 "Zeilen voll am Bergknoten" / EVICT, direkte Zeilen geschützt** — matches an existing,
  passing test (`test_eviction_prefers_two_hop_row_over_older_direct_rows`,
  `test/test_nbr_matrix/test_nbr_matrix.cpp:884`); the described protection already exists and is
  tested for the _current_ dense matrix, giving confidence the same invariant is realistic to keep
  under the edge-pool rewrite.
- **§5 "Rufzeichen außerhalb der Zeichenregel" / DROP TOK** — matches existing behavior
  (`nbrNoteFrame()` today already rejects out-of-alphabet tokens); a real, already-instrumented
  detection path.
- **§4.2's `static_assert(NBR_MAX_ROWS <= 64 * NBR_MASK_WORDS, ...)`** — a compile-time guard that
  genuinely replaces a documented silent-failure mode (`nbrBit()` returning 0 past index 32); this
  is exactly the kind of criterion that can fail loudly instead of silently, and is the strongest
  single test-design improvement in the paper.
- **Schritt 4 "App auf dem Telefon zeigt die Liste" / "T-Deck ... behält die Topologie über einen
  Neustart"** — concrete, physically observable, hard to game.

## Summary of what to fix before this paper is acceptance-ready

1. Rewrite the Schritt 1 replay criterion (H1) — either keep capacity fixed for the replay check
   and gate the 64/128-row bump separately, or redefine "byte-gleich" as a coverage superset.
2. Scope the string-scan (H2) to the envs where the strings can ever appear.
3. Replace whole-image `resource_watch` RAM deltas with symbol-level `nm` deltas (H3) to match what
   Table 4.7 actually computed.
4. Add the missing host tests for the new internals (callsign codec, masks, edge pool) and name
   `test_txring`'s rewrite explicitly (H4) before calling Schritt 1 done.
5. State DIST tolerance and exclusion lists explicitly wherever "gleich"/"feldgleich" appears (H5, H6).
6. Define a logged echo-outcome line and a companion script before Schritt 6 (H7); scope the mcmap
   gateway-arrival comparison to a named metric and acknowledge the confounds (H8).
7. Give Schritt 5's NCNT criterion a number (H9).
