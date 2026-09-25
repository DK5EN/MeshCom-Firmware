# Finder C — NCNT (topology-derived neighbour count), section 4.5 and all NCNT touchpoints

Scope: paper.txt lines 29 (Ergebnis row 4), 37/40 (Ausbaustufen), 46/59/60 (4.0 Uebersicht), 81
(NCNT-Deckel), 148/156 (4.3), 174-190 (4.5), 280/281 (Fehlerszenarien), 299/300 (Offene Punkte).

## Candidates

### C1 — SYM formula silently drops the HN-report veto that the codebase already implements (High)

Quote (paper.txt:179): `NCNT = popcount(D60 & (HM | SYM))` with
`SYM = { x in D60 : snr(x, ich) >= LORA_SNR_STABLE_MIN_DB }` (paper.txt:181-183).

Problem: the formula is a bare SNR-threshold test. But the codebase's existing symmetric-hearing
helper that this NCNT design is clearly modelled on, `nbrHearsSym()`
(src/nbr_matrix.cpp:1184-1213), does NOT stop at the SNR test. It adds a veto: if the candidate
neighbour `x` has sent a fresh, _complete_ HN report (`NBR_FLAG_RPT`, within
`NBR_REPORT_VALID_MIN`) that does not list me, that report is authoritative and the symmetry
assumption is refused even though the reverse SNR qualifies (src/nbr_matrix.cpp:1192-1206,
comment at 1163-1183: "faengt die Symmetrie-Annahme... traegt X einen gueltigen, VOLLSTAENDIGEN
HN-Bericht... gilt 'X hoert M nicht', nicht die Annahme"). The paper's 4.5 formula and its option
table (paper.txt:184-188) never mention this veto, and 4.5's own option row for the naive
D60&HM-only variant ("symmetrisch, nur gemessen... bricht bei Nachbarn ohne Mesh ein") does not
surface it either.

Failure scenario: a neighbour x that explicitly tells us via a complete HN report "I don't hear
you" (I'm just missing from its R<heard>;N<k>;... list) would still be counted into my NCNT via
SYM under the literal formula in 4.5, directly contradicting negative evidence the firmware
already knows how to honour. If the implementation instead silently reuses `nbrHearsSym()` (likely,
since it's the obvious code to call), the _documented_ formula in the paper is simply wrong/
incomplete as written and will mislead a reviewer or reimplementer.

Severity: High (correctness gap between the paper's own formula and the mechanism it explicitly
says it wants to reuse; not caught by the option-comparison table).

### C2 — New SNR-threshold SYM branch is a new noise source for trickle-interval reset; paper never assesses flap risk (Medium-High)

Quote (paper.txt:190): "Die Trickle-Ruecksetzung reagiert auf Aenderungen des neuen Werts."

Evidence: both trickle consumers compare `getMheardCount()` (today) resp. the future NCNT value
verbatim each period and reset to the fastest interval on ANY change:
`src/esp32/esp32_main.cpp:3480-3489` and `src/nrf52/nrf52_main.cpp:2018-2025`
(`if(trickle_last_neighbor_count >= 0 && current_neighbors != trickle_last_neighbor_count) { trickle_interval_ms = TRICKLE_IMIN_S * 1000UL; trickle_consistent_count = 0; }`).

Problem: today's getMheardCount() only depends on MHeard _presence_ (a callsign is or isn't in the
table within the hour window) — a comparatively stable input. The new SYM branch adds a
hard-threshold test on a raw per-frame SNR value (`snr(x,ich) >= LORA_SNR_STABLE_MIN_DB`, -16 dB
default, no hysteresis visible in the 4.5 formula or in `nbrHearsSym()`). A neighbour riding
exactly at that boundary (plausible — the paper's own DK5EN-98 example lists DL2JA-1 at -8 dB and
mentions "ein Nachbar, der nur mit -18 dB ankommt... zaehlt kuenftig nicht mehr", i.e. values close
to the -16 dB line are expected in the field) will flip in and out of NCNT every time its SNR
crosses -16 dB, on every received frame, not just once per hour like the D60 aging. Each flip
forces a full trickle-interval reset, defeating the point of RFC 6206 suppression (fewer redundant
HEYs) for exactly the marginal-link nodes where traffic reduction matters most.

Failure scenario: a borderline neighbour oscillating a few dB around -16 causes the trickle
interval to be reset far more often than under the current getMheardCount()-based trigger,
increasing channel load instead of the intended "Momentaufnahme" stability. Neither 4.5 nor
Fehlerszenarien nor Offene Punkte discuss hysteresis or dampening for this new trigger source.

Severity: Medium-High (behavioural regression risk on a design decision the paper presents as a
simple, uncontroversial consequence).

### C3 — B3 and B6 from the NCNT-Befund paper are neither carried over as fixed nor flagged as still open (Medium)

Quote (paper.txt:156): "Damit entfallen die Befunde B1, B2, B4 und B5 des NCNT-Papiers ohne eigene
Korrektur... " — no mention of B3 or B6 anywhere else in the paper (checked full text).

Evidence from ~/Desktop/Nachbarzahl-NCNT-Befund.html:

- B3: "Eine Nachbarzahl 0 kommt nur ueber HEY an: der Sender laesst /N0 weg, der Dekoder verlangt
  eine Ziffer 1 bis 9..." — this is a _wire-protocol_ limitation (the position beacon never emits
  /N0), not a storage-architecture bug. Confirmed still present:
  `src/loop_functions.cpp:4548-4554` only emits `/N<n>` when `incnt > 0` — an all-neighbours-stale
  node still sends no `/N` tag at all under the new design too, since 4.6/4.5 don't touch this
  encoding path. The topology redesign does not remove or fix this; the paper simply never brings
  it up.
- B6: "Der Ausloeser 'Nachbarzahl geaendert, Beacon senden'... kann nicht feuern, weil
  posinfo_timer_min in jeder Schleife ohne faelligen Beacon auf jetzt gesetzt wird." Confirmed the
  dead trigger and the offending reset are both still in the tree unchanged:
  `src/esp32/esp32_main.cpp:3462` and `:3467` reset `posinfo_timer_min = millis()` on every pass
  through the position-beacon block regardless of whether a beacon actually fired, so the
  change-triggered beacon branch this refers to (separate from the _trickle_ HEY reset the paper
  does discuss at :3480) can still never observe a stale enough timer to fire. Paper's Ergebnis/
  Ausbaustufen tables (rows for Stufe 2/3) list only the mheard/pfad/NCNT-storage removals, not this
  code path.

Problem: the task's own framing question ("is B3/B6 handled") is answered "no" for both — the
paper's claim of completeness ("Damit entfallen die Befunde B1, B2, B4 und B5... ") reads as if the
NCNT-Befund catalogue is otherwise closed by implication, but two items are left untouched and
unacknowledged.

Severity: Medium (omission, not a wrong claim — the four explicitly named findings do check out,
see below — but the silence on B3/B6 could be read as "everything else is fine").

### C4 — HM/HEY-relay-group ncnt mixing old (asymmetric) and new (symmetric) semantics is also a _local_ JSON exposure problem, not just an mcmap problem (Low-Medium)

Quote (paper.txt:190): "NCNT eines MeshCom-5-Knotens bedeutet etwas anderes als das eines
4.35-Knotens; mcmap muss beide trennen koennen, das ist ein offener Punkt."

Problem: line 156 states that R<n>, /N, and relay-group NCT values from _any_ sender (direct or
indirect, i.e. from old 4.35 firmware too) get written straight into `ncnt` of that sender's row —
confirmed by the wire format: `appendHeySignalReport(aprsmsg, rssi, snr, getMheardCount())`
(src/lora_functions.cpp:1736, :1863) is unchanged by this proposal and still carries the old,
asymmetric getMheardCount() semantics from any node that hasn't been upgraded. The paper only
raises the resulting semantic ambiguity as an _mcmap_ (network-aggregator) open point. But 4.6's
own MH-JSON `NCNT` field (paper.txt:148, "Kern ncnt... zuletzt gemeldete Nachbarzahl dieser
Station") exposes the very same unlabeled, semantically-mixed value straight to the phone app and
web UI of the _reporting_ node itself — a MeshCom-5 node's own neighbour list will show old- and
new-semantics NCNT values side by side for its rows with no field to tell them apart, independent
of whatever mcmap eventually does. This is the same open point, one layer earlier, and the paper
doesn't mention it there.

Severity: Low-Medium (same root cause as the already-acknowledged mcmap issue, but a second,
un-acknowledged surface for it).

### C5 — DK5EN-98 worked example (paper.txt:189) cites specific per-neighbour dB values with no traceable source (Low, methodological)

Quote: "Vier Nachbarn hoeren mich nachweislich, DL2JA-2 mit -13 dB, DK5EN-1 mit 6 dB, DL2UD-1 mit
-12 dB, DB0ED-99 mit -6 dB; DL2JA-1 leitet nie weiter, kommt aber mit -8 dB an und zaehlt ueber
SYM."

Problem: per the storage model, the "X hoert mich" dB values (four names) must come from
`cells[0][x].snr`, populated only via HN report (`src/nbr_matrix.cpp:928-929`) or a HEY relay
group naming pair (me,x) — i.e., a report authored by X (or a relay of X hearing me), reflecting
X's OWN reception of me. The DL2JA-1 value (-8 dB, via SYM) is explicitly the reverse — MY
reception of DL2JA-1 (`cells[x][0].snr`). Both directions are internally consistent with the code
(confirmed: cell[last_hop][0].snr is set unconditionally from `snr_here` at src/nbr_matrix.cpp:566
and :680, and cell[0][s].snr from a parsed HN report entry at :929/:944), so the example is
_plausible_, but no log line, `--nbrreport` dump, or `[NBR]` trace is cited to back the four
specific dB numbers or confirm they are fresh within the stated windows (D60 60 min, HM 12 h) at
08:55 on 24.09. As written the example cannot be independently verified from the paper alone.

Severity: Low (methodological gap, not a demonstrated error — flagging per the review brief's
instruction to check the example's inputs against what is actually knowable).

## Checks that came out fine

- **NCNT caps (paper.txt:81, "30/50/80/80 -> 63/63/127/127")**: matches the four `MAX_MHEARD`
  branches in src/configuration_global.h:301/312/322/333 (50/80/10/30 -- T-Beam dev variant
  correctly excluded from the paper's four-way table) mapped onto the two `NBR_MAX_ROWS` families
  from 4.2 (64 rows incl. E22_XML -> cap 63, 128 rows on S3/nRF52 -> cap 127). Internally
  consistent with the 4.2 decision text.
- **`/N` stays capped at 99**: confirmed, src/loop_functions.cpp:4544-4550
  (`if(incnt > 99) incnt = 99; snprintf(cncnt, ..., "/N%i", incnt);`).
- **R<n> in HEY = getMheardCount()**: confirmed, src/loop_functions.cpp:5095.
- **Clockless-node claim ("Topologie und NCNT arbeiten, Anzeige in Minuten")**: confirmed correct
  and a genuine improvement over today. `now_min` is derived purely from
  `(uint16_t)(millis()/60000UL)` (src/lora_functions.cpp:988), with no wall-clock dependency
  anywhere in nbr_matrix.cpp's freshness tests (`nbrFresh()`, src/nbr_matrix.cpp:471). Today's
  MHeard path, by contrast, really does gate entry creation on a valid wall-clock date
  (`if(mcSliceToLong(mheardLine.mh_date,0,4) < 2025) return;`, src/mheard_functions.cpp:301-302),
  so `getMheardCount()` is provably 0 forever on a clockless node today — the paper's line 56
  claim about the status quo is accurate, and 4.5's redesign genuinely fixes it.
- **SNR field is well-defined and always "latest within window", not stale/any**: `NbrCell.snr` is
  a single per-edge field overwritten unconditionally on every direct reception
  (src/nbr_matrix.cpp:566, :680) and reset to `NBR_SNR_UNKNOWN` (not 0) only when the cell had
  already expired past `NBR_WINDOW_MIN` (720 min) before the current hit
  (src/nbr_matrix.cpp:254-260). A fresh cell keeps its last known SNR even across frame types that
  carry no signal report (text/POS). This matches the paper's implicit assumption and is
  computable exactly as described.
- **HM evidence sources**: confirmed the paper's claim is accurate for the existing code paths —
  `cells[0][x]` ("x heard me") is written either (a) when I directly observe a relay of my own
  POS/HEY (ordinary path-pair edge write for consecutive pair (me,x) in a frame I hear), or (b) via
  a complete HN report naming me (src/nbr_matrix.cpp:919-930). HEY relay-group NCT/RSSI/SNR triples
  are not a separate, independent evidence channel — they are attached to the very same path-pair
  edges that already fall under "relayed HEY", so they don't create a hidden extra HM source as the
  attack angle suspected.
- **B1, B2, B4, B5 genuinely disappear under the proposed single-ncnt-per-row model**: B1 (two
  copies) and B4 (first-beacon /N loss due to two-step entry creation) are structurally impossible
  once there is exactly one `ncnt` field per row set directly from whichever report arrives first;
  B2 (REP-overwrite mixing new date with stale RSSI/SNR) cannot occur because the redesign no
  longer distinguishes a separate "direct" vs "relayed HEY" record for the same field; B5 (full
  path table evicting new far senders) is moot once there is no separate path table. This part of
  4.3's claim holds up against the actual B1/B2/B4/B5 descriptions in the NCNT-Befund paper.
- **"Without SYM, NCNT collapses with Stage 4" — internally coherent as a roadmap statement**: the
  causal chain (Auto-Via preferring specific vias reduces how often _other_ neighbours get to relay
  and thus prove hearing) is plausible given `via_functions.cpp` picks a via by highest NCNT
  (paper.txt:255, "die alte Regel: groesstes NCNT... verworfen"). It is explicitly scoped to a
  _future_ stage (Auto-Via is currently disabled per paper.txt:300/Offene Punkte), so it is not a
  claim about today's behaviour, and the paper does not misstate that scoping.
