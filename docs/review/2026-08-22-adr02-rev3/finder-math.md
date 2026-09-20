# Math/Consistency Findings — adr-nc-importance-backoff.md

All arithmetic recomputed in Python (`verify.py` in this directory). Formula reference:
`imp_ratio = min(imp,CAP)/CAP`, `slot_start = int((1-imp_ratio)*(RELAY_TOTAL_SLOTS-RELAY_JITTER_WIDTH))`,
`slot = slot_start + random(0,WIDTH)` i.e. `{slot_start, slot_start+1, slot_start+2}`,
`backoff = base + slot*35`. CAP=8, RELAY_TOTAL_SLOTS=10, RELAY_JITTER_WIDTH=3, CSMA_SLOT_SIZE=35.

---

## F1: Dedup-ring rotation time wrong by ~2.2x (60-slot ring)

**Section:** "Zombie-Nachrichten durch Dedup-Ueberlauf", line 132-134; repeated in "Offene Punkte", line 1237.

**Stated:** "Der Dedup-Ring hat 60 Slots. Bei 4,2 neuen Nachrichten/Minute rotiert die Tabelle alle ~6,5 Minuten." (and again at line 1237: "Aktuell rotiert der Ring alle ~6,5 Minuten").

**Recomputed:** 60 slots / 4.2 msg/min = **14.3 minutes**, not 6.5 minutes.

This is not a one-off slip — the _same_ rate (4.2 msg/min) is applied correctly two other times in the same document: line 1203 "100 Slots ... rotiert ... nach ~24 Minuten" (100/4.2 = 23.8 ✓) and "der ESP32-Ring nach ~17 Minuten" (70/4.2 = 16.7 ✓). Only the 60-slot figure is wrong, and it is wrong by more than a factor of 2.

**Why it matters:** This number is used to argue the zombie-message problem is severe ("Langsame Multi-Hop-Relays (bis zu 13 Minuten beobachtet) kommen nach der Dedup-Rotation an" — i.e. 13 min > stated 6.5 min rotation, so basically every slow relay becomes a zombie). With the correct ~14.3 min rotation, a 13-minute relay is _within_ one rotation period, not past it — the argument as framed overstates how often this happens. The qualitative conclusion (dedup ring too small) likely still holds given ring behavior isn't strictly periodic, but the quantitative claim as written is wrong and the severity framing is inflated.

**Severity:** High (arithmetic error, feeds a headline risk claim, used twice).

**Correction:** Replace "~6,5 Minuten" with "~14,3 Minuten" in both locations, and adjust the surrounding argument (13 min is now close to, not far past, one rotation).

---

## F2: "0..315ms bei 10 Slots × 35ms" contradicts the document's own slot mechanism

**Section:** Risiko-Analyse / Topologie-Sicherheit intro, line 932.

**Stated:** "Base = 4500ms fuer alle. Slot-Position bestimmt Jitter (0..315ms bei 10 Slots × 35ms)."

**Recomputed:** 315ms = 9×35, i.e. this treats slot 9 as reachable. But under the actual 3-slot-window mechanism (4.2), `slot_start` can be at most 6 (proof: `slot_start=7` requires `imp_ratio=0` exactly, i.e. `importance=0` exactly; but `importance=0` only occurs when there are zero active neighbors, and in that case `getNetImportanceKnownPct()` returns 0, which is `< RELAY_IMP_MIN_KNOWN_PCT`, so the code takes the _old-jitter fallback branch_ instead of ever computing a slot_start — the degenerate case is routed away by the 3.1 quality gate before it can reach `slot_start=7`). With `slot_start` capped at 6, the maximum reachable slot is `6+2=8`, giving max jitter **8×35 = 280ms**, matching every worked backoff example in the document itself (4.3, 4.4, 4.5 all cap at `..4780ms` = 4500+280).

So 315ms matches neither the new system's true max (280ms) nor the old/current system's max (350ms — confirmed via `random(0, CSMA_PRIO_SLOTS_3+1)` = 11 values 0..10, ×35 = 0..350, which is exactly what the "Aktuelle CSMA-Parameter" table at the top states: 4500..4850). 315 appears to come from naively computing `(RELAY_TOTAL_SLOTS-1)*35` without accounting for the window mechanism.

A related, milder instance of the same slot-counting confusion: the 3.1 pseudocode comment (line 461) labels the fallback as "voller 0..9 Jitter" — but the actual fallback code (5.3) uses `random(0, CSMA_PRIO_SLOTS_3+1)`, which produces 11 distinct values (0..10), not 10 (0..9). This matches the "10 Slots = 11 Positionen" phrasing correctly used elsewhere (4.6), so "0..9 Jitter" undercounts the fallback's own range by one step.

**Severity:** High (headline number in the Risk section, directly contradicted by the document's own detailed tables).

**Correction:** "0..280ms" (new-scheme achievable max, matching 4.3/4.4/4.5), or if the intent was to state the _old_ system's range for comparison, "0..350ms".

---

## F3: 4.7a "Spreizung 560–700 ms" for RELAY_TOTAL_SLOTS 16–20 overestimates the achievable spread (same root cause as F2)

**Section:** 4.7a, line 651-654; repeated in "Offene Punkte" Parameter-Tuning, line 1249.

**Stated:** "`RELAY_TOTAL_SLOTS` nur fuer das Relay-Band auf 16–20 erhoehen (Spreizung 560–700 ms) ... waechst von 4500..4850 ms auf bis zu 4500..5200 ms, im 4-Hop-Fall also bis zu +1,4 s."

**Recomputed:** 560 = 16×35 and 700 = 20×35 — again computed as `RELAY_TOTAL_SLOTS × 35` with no window correction. Using the same `max_slot = RELAY_TOTAL_SLOTS - RELAY_JITTER_WIDTH - 1 + (RELAY_JITTER_WIDTH-1) = RELAY_TOTAL_SLOTS - 2` bound established in F2:

| RELAY_TOTAL_SLOTS | naive (×35) | actual achievable max   |
| ----------------- | ----------- | ----------------------- |
| 16                | 560ms       | **490ms** (max_slot=14) |
| 20                | 700ms       | **630ms** (max_slot=18) |

Note this also self-contradicts F2's methodology: F2's "0..315" used `(TOTAL-1)×35`, this one uses `TOTAL×35` — two different naive approximations for the same underlying quantity, appearing in the same document, neither matching the true `(TOTAL-2)×35`.

**Downstream effect:** the "4500..5200ms" backoff window (4500+700) and the derived "+1.4s over 4 hops" (= (5200-4850)×4 = 1400ms) both inherit the overestimate. With the corrected max (630ms → window 4500..5130ms), the per-hop delta vs. today's 4850ms max is 280ms, giving **+1.12s over 4 hops**, not +1.4s.

**Severity:** Medium (affects a tuning trade-off number cited twice, cascades to a latency claim, but is explicitly framed as a "Feldtest zu messen" estimate rather than a hard spec).

**Correction:** "Spreizung 490–630 ms" and "+1,1 s" (4-hop latency), or recompute precisely once RELAY_JITTER_WIDTH is finalized.

---

## F4: Section 4.5, Node B — Imp=1.5 does not map to "Slots 6..8"

**Section:** 4.5 Zahlenbeispiel: Kaskadeneffekt, line 572.

**Stated:** "Node B: NC_self=6, NC_reported=3-5 → Imp=1.5 → Slots 6..8 → 4710..4780ms"

**Recomputed:** `imp_ratio = 1.5/8 = 0.1875`; `slot_start = int((1-0.1875)*7) = int(5.6875) = 5`. That gives slots **{5,6,7}**, i.e. window **4675..4745ms**, not slots 6..8 / 4710..4780ms.

Cross-check against the correctly-computed table 4.3, which has no exact entry for 1.5 but brackets it: Imp=2.0→slot_start=5, Imp=1.0→slot_start=6. Imp=1.5 sits between those, and the formula puts it with the _lower_ (2.0/Bridge) bracket, not the _upper_ (1.0/Cluster) one — the ADR's own table would have flagged this if it included a 1.5 row.

Nodes C (Imp=0.5→slot_start=6 ✓), D (Imp=0.1→slot_start=6 ✓), E (Imp≈0.5→slot_start=6 ✓) are all correctly placed in slots 6..8. Only Node B is wrong.

**Severity:** Medium-High (this is a formula misapplication in a worked example that is supposed to demonstrate the mechanism, not just a rounding artifact).

**Correction:** Node B: Imp=1.5 → Slots 5..8, wait — Slots **5,6,7** → **4675..4745ms**. (Coincidentally the narrative's own t=4745ms/"Slot 7" line for Node B is still inside the corrected window, so only the summary row needs fixing, not the narrative timestamp.)

---

## F5: 4.4 "Der Vorsprung ... ist immer ~210ms" is not a guaranteed worst case

**Section:** 4.4, line 557-559.

**Stated:** "Der Vorsprung des Hubs gegenueber dem Blatt ist immer ~210ms (Slot 0 vs. Slot 6), unabhaengig vom Attempt."

**Recomputed:** Hub occupies slots {0,1,2}, Blatt occupies slots {6,7,8}. 210ms (=6×35) is only the gap between the Hub's _best-case_ slot (0) and the Blatt's _best-case_ slot (6) — i.e. both draw their most favorable random outcome. The **guaranteed** (worst-case) gap — Hub draws its worst slot (2), Blatt draws its best slot (6) — is `(6-2)×35 = 140ms`. The _maximum possible_ gap (Hub best, Blatt worst) is `(8-0)×35 = 280ms`.

So "immer ~210ms" overstates the guarantee: the true floor is 140ms, not 210ms. Contrast with section 4.3's own treatment of the Berg-Hub/Bridge pair, which correctly uses the worst-case framing ("Minimum 105ms Vorsprung (Slot 2 vs. Slot 5)" = Hub's worst slot vs. Bridge's best slot = `(5-2)×35=105`, confirmed correct). 4.4 does not apply the same worst-case discipline to the Hub/Blatt comparison it makes a stronger ("immer") claim about.

**Severity:** Medium (the qualitative argument in 4.5's "Warum 210ms ausreichen" still works at 140ms, since CSMA only needs the Hub to _start_ first — but the specific number and the word "immer" are not supported).

**Correction:** State the guaranteed minimum as "mindestens 140ms (Slot 2 vs. Slot 6, worst case), typischerweise ~210ms" rather than "immer ~210ms".

---

## F6: 3.1 Qualitaetsgate ASCII-flow — "jeder halbwegs vernetzte Node landet in Slots 0..2" overstates the effect

**Section:** 3.1, line 447-453.

**Stated:** "Alle Nachbarn unbekannt → ... → Importance = NC_self → jeder halbwegs vernetzte Node landet in Slots 0..2"

**Recomputed:** "Importance = NC_self" is correct given the doc's own consequence #1 (same 1h window for both). But `slot_start=0` (the literal "Slots 0..2" window) only occurs for `NC_self ≥ 7` (need `(1-NC_self/8)*7 < 1` ⇒ `NC_self > 48/7 ≈ 6.86`). Using the evidence pack's real fleet nct distribution (median 3, p75 7, p90 11), a _typical_ "halbwegs vernetzt" node (NC_self≈3) actually lands at `slot_start=4` (slots 4..6), and only nodes at roughly the p75+ percentile (NC_self≥7) reach slots 0..2. So the collapse isn't "everyone piles into slots 0..2" — it's "slot position tracks raw NC_self (degree) instead of true dependency," with most nodes spread across slot_start 0..6 according to their degree. The subsequent "keine Differenzierung" claim is also imprecise for the same reason: there _is_ still differentiation, just of the wrong kind (by degree, not by dependency) — the real problem the paragraph is trying to describe.

**Severity:** Medium (doesn't invalidate the gate's rationale — a narrower 3-slot jitter with degree-driven ordering is still worse than the status quo's full 10-slot randomization for the reasons given — but the specific "0..2 for everyone" mental picture is not what the formula produces).

**Correction:** Rephrase to something like "slot position tracks raw NC_self instead of true dependency; well-connected nodes (NC_self ≳ 7) land in slots 0..2, typical nodes spread across slots 0..6 — differentiation still exists, but by the wrong variable, while the jitter window it happens in has shrunk from 10 slots to 3."

---

## F7: Integer fixed-point variant (`getNetImportanceFixed`) has an undocumented, systematic downward bias

**Section:** Risiko-Analyse / "Risiko: Float auf ESP32", line 949-965.

**Stated:** presented as a drop-in integer alternative (`importance_x100 += 100 / nc`) with no precision caveat.

**Recomputed:** Integer division truncates every term downward, never upward, so `getNetImportanceFixed()` is a strictly ≤ the float version, never equal-or-above. Per-term relative error:

| nc  | float 100/nc | int 100/nc | rel. error                  |
| --- | ------------ | ---------- | --------------------------- |
| 3   | 33.33        | 33         | 1.0%                        |
| 7   | 14.29        | 14         | 2.0%                        |
| 8   | 12.50        | 12         | 4.0%                        |
| 12  | 8.33         | 8          | 4.0%                        |
| 13  | 7.69         | 7          | 9.0%                        |
| 17  | 5.88         | 5          | 15.0%                       |
| 51  | 1.96         | 1          | 49.0% (worst case in 1..99) |

For neighbors with mid-teens to double-digit NC_reported (common per evidence-pack nct distribution: median 3, p75 7, p90 11 — so most active contributions come from small nc where the bias is only 1-4%, but the tail toward nc≈13-20 loses 9-15% per term), the accumulated bias can shift `importance_x100` enough to move `slot_start` by one slot near a boundary (e.g. an importance of 5.0 computed in float sits exactly on the slot_start=2/3 boundary — `ratio=0.625, (1-ratio)*7=2.625"`; losing even a few percent of accumulated importance can flip which side of a `.0`-boundary the truncated `int()` lands on). This tradeoff (float never overestimates the fallback's fairness, integer always underestimates true importance) is not mentioned anywhere near the integer-variant code.

**Severity:** Low-Medium (the alternative is explicitly marked optional/for later discussion, but as written it is silently _not_ numerically equivalent to the float version, which the doc implies by calling it "ganzzahlig approximiert").

**Correction:** Note that integer truncation introduces a monotonic downward bias (worst case per-term ~49% for nc in 1..99, more realistically single digits to low teens % for typical nc), and that a rounding variant (`(200+nc)/(2*nc)` or similar) would remove most of the bias if integer arithmetic is adopted.

---

## F8: 4.5 Kaskadeneffekt timeline — event listed out of chronological order and one event ignores the channel-busy mechanism explained two paragraphs later

**Section:** 4.5, line 577-583.

**Stated (as printed):**

```
t=4500ms   Node A (Slot 0) sendet Relay → erreicht 10 Nachbarn
t=4900ms   Nodes B,C,D,E hoeren Duplikat → bestehende Suppression kann greifen
t=4745ms   Node B: wenn nicht supprimiert → sendet Relay (Slot 7)
t=4780ms   Nodes C,E: fast sicher supprimiert (2+ Duplikate gehoert)
```

**Issue 1 (ordering):** t=4900ms is listed _before_ t=4745ms and t=4780ms, even though 4900 > 4745 and 4900 > 4780. The lines are not in chronological order.

**Issue 2 (consistency with the rest of 4.5):** the "t=4745ms Node B ... sendet Relay" line implicitly assumes the channel is free at 4745ms. But Node A started transmitting at 4500ms and (per the very next subsection, "Warum 210ms im CSMA-Kontext ausreichen", line 599-606) a packet takes 300-1000ms to transmit, and a node whose backoff expires while A is still transmitting finds the channel **busy** and must retry (that subsection's own worked example has the Blatt's CAD at t=4710 find "Kanal BUSY → wartet (Attempt 1)"). By the same logic, Node B's CAD at t=4745 (during A's ongoing transmission, which per that later example runs at least to ~4800-4900ms) should also find the channel busy, not send. The 4.5 timeline's "wenn nicht supprimiert → sendet Relay" glosses over this — it should read "CAD findet Kanal BUSY -> Attempt 1", consistent with how the doc explains the mechanism immediately afterward.

**Severity:** Low-Medium (doesn't change the ADR's conclusion, but the illustrative example is internally inconsistent with the more careful mechanism description right next to it, and the timestamps are unordered).

**Correction:** Reorder the timeline by timestamp, and correct the t=4745 line to reflect a busy-channel retry rather than an unconditional send, per the CAD mechanism described in the following subsection.

---

## F9 (minor): Hop-Count-Verteilung percentages sum to 100.7%, not 100.0%

**Section:** "Hop-Count-Verteilung" table, line 157-163.

10.5 + 19.0 + 26.8 + 23.7 + 20.7 = **100.7**, not 100.0.

**Severity:** Low (almost certainly independent rounding of each row from the underlying counts; flagged for completeness since the task asked to recompute every number).

---

## F10 (minor): "200–256 erhoehen (~50 Minuten Speicher)" understates the top of the range

**Section:** Stufe 3, line 1209.

200 slots / 4.2 msg/min = 47.6 min; 256 / 4.2 = **61.0 min**. "~50 Minuten" is a reasonable single figure for 200 but noticeably undersells 256 (61 min, ~22% higher). Minor since it's a rough planning number, but "~50–60 Minuten" would be more accurate for the stated 200–256 range.

**Severity:** Low.

---

## F11 (minor, informational): Node E in 4.5 — "Imp=0.5" vs. its own inputs

**Section:** 4.5, line 575.

"NC_self=2, NC_reported=3,8 → Imp=0.5": `1/3 + 1/8 = 0.3333 + 0.125 = 0.4583`, not exactly 0.5 (≈8% off). Does not change the slot outcome (`slot_start(0.4583)=6`, same as the stated `slot_start(0.5)=6`), so purely cosmetic.

**Severity:** Low (no downstream effect).

---

## Verified correct

- **2.1 Stern:** A = 5×(1/1) = 5.0 ✓; B = 1/5 = 0.2 ✓.
- **2.2 Lineare Kette:** A=0.50, B=1.50, C=1.00, D=1.50, E=0.50 — all recomputed exactly, ranking (B=D > C > A=E) correct.
- **2.3 Dichtes Mesh:** each node = 5×(1/5) = 1.0 ✓.
- **2.4 Bridge:** A = 1/3+1/3+1/6 = 0.833 (shown as 0.83 ✓); X = 6×(1/3) = 2.0 ✓.
- **2.5 Zwei Bridges:** A = 1/4+1/4+1/7+1/7 = 0.7857 (shown as 0.79 ✓); X = 6×(1/4)+1/7 = 1.6429 (shown as 1.64 ✓).
- **2.6 Berg-Hub vs. Stadt-Node:** Berg-Hub Σ = 7.1667 (shown as 7.16, and rounded to 7.2 elsewhere — consistent) ✓; Stadt-Node Σ = 1.0298 (shown as 1.03 ✓); ratio ≈ 6.96×, doc's "7x" is a fair round figure.
- **Section 3 mixed-mode example:** 3 known + 3 unknown = 0.2+0.333+0.125+1+1+1 = 3.6583 (shown as 3.66 ✓); all 6 known after correction = 0.2+0.333+0.125+0.143+0.25+0.1 = 1.1512 (shown as 1.15 ✓).
- **Retry-Reduktion arithmetic:** 4500×5/6 = 3750 exactly ✓ (~16.7%≈"17%" ✓); 4500×2/3 = 3000 exactly ✓ (33.3%≈"33%" ✓). No integer-division rounding artifacts since these divide evenly.
- **Table 4.3 slot mapping (all 7 rows):** imp_ratio, slot_start, slot range, jitter ms, and backoff window all recomputed exactly matching the printed table for Imp = 8.0, 7.2, 5.0, 2.0, 1.0, 0.5, 0.2.
- **Table 4.4 retry table (Berg-Hub, Stern-Hub, Bridge, Cluster, Blatt rows):** all Attempt-0/1/2 windows recomputed exactly matching (base × slot arithmetic consistent throughout).
- **105ms "Minimum Vorsprung" claim (4.3):** Berg-Hub worst slot (2) vs Bridge best slot (5) = 3×35 = 105ms ✓ — this one _does_ correctly use worst-case framing (contrast with F5).
- **Edge case — can `slot_start` ever reach 7 ("Blatt" spilling into slots 7..9)?** No. Mathematically `slot_start=int((1-ratio)*7)` reaches 7 only when `ratio=0` exactly, i.e. `importance=0` exactly. But `importance=0` only arises when there are zero active neighbors, and in that case the 3.1 quality gate's `getNetImportanceKnownPct()` returns 0 (< RELAY_IMP_MIN_KNOWN_PCT), routing the node to the old-jitter fallback _before_ the importance-slot branch is ever reached. So whenever the importance branch actually runs, `importance>0` strictly, `slot_start≤6` always, and the table's "Blatt → Slots 6..8" claim holds for every reachable case (the failure mode flagged in F2/F3 is a different bug — max jitter of the _achieved_ slots, not this edge case).
- **`RELAY_TOTAL_SLOTS - RELAY_JITTER_WIDTH = 7` (line 505, 651):** consistent arithmetic (10-3=7), used correctly as the multiplier in the slot_start formula throughout.
- **Today's Prio-3 jitter (0..350ms, 4500..4850ms):** consistent with `random(0, CSMA_PRIO_SLOTS_3+1)×35` = `random(0,11)×35` = 11 values × 35 = 0..350, matching the "Aktuelle CSMA-Parameter" table and the "10 Slots = 11 Positionen" note (4.6).
- **Risk table (Topologie-Sicherheit, line 934-942):** Berg-Hub (7.2→0..2), Stern-Hub (5.0→2..4), Bridge (2.0→5..7), Full-Mesh (1.0→6..8), Kette-Mitte (1.0→6..8), Stadt-Node (rounded 1.0, exact 1.03, both →6..8, no boundary crossed), Stern-Blatt (0.2→6..8) — all recomputed correctly against the formula.
- **Dedup rotation for 100 and 70 slots (Stufe 3, line 1203):** 100/4.2=23.8≈"24 Minuten" ✓; 70/4.2=16.7≈"17 Minuten" ✓ (only the 60-slot figure elsewhere in the doc is wrong — see F1).
- **DUP/NEW ratio (BergLog):** 25089/16032 = 1.5651 ≈ "1,57" ✓; total receptions per message = 1+1.57 = 2.57 ✓ matches "2,57x" claim.
- **Kaltstart example (Risiko-Analyse):** NC_self=5, all unknown → Importance=5.0 → slot_start(5.0)=2 → Slots 2..4 ✓ matches table 4.3's Imp=5.0 row.
- **4.6 slot-count table:** base timeouts (3000/3000/4500/5500/5500) and uniform 10-slot claim consistent with the "Aktuelle CSMA-Parameter" table at the top of the document.
