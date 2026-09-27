# Finder I — Deductive Consistency and Measurements

Paper: MeshCom5-Topologie-als-Quelle (paper.txt). Predecessors: Nachbarschaftsmatrix-Kantenpool.html (Kantenpool), Nachbarzahl-NCNT-Befund.html (NCNT).

## Candidates (issues found)

### C1 — SEVERE: Abb.6 Zubringer choice contradicts the paper's own rule and its own cited fixture (wrong edge direction, wrong number)

Section 4.8 (line 235, Abb. 6 caption, line 252): the paper picks **DL2UD-1** as Zubringer for DL2JA-2, citing "DL2JA-2 hat ihn [DL2UD-1] 159-mal gehört" and "er hört mich sicher, und DL2JA-2 hat ihn 159-mal gehört."

The paper's own selection rule (line 242): "für jedes schwache m: Zubringer F in K, nicht schwach, m hört F, meiste Treffer auf Kante (F, m)" — i.e. pick, among non-weak candidates F, the one with the most hits on the edge "m hears F".

Kantenpool Anhang B defines the fixture cell semantics explicitly: "X,Y,Summe … Zellsemantik 'Y hat X gehört'" (cell(X,Y) = Y heard X). With m = DL2JA-2 (id 2) and K = {DK5EN-1(3), DL2UD-1(4), DB0ED-99(5), DL2JA-1(11)}, "m hört F" = cell(F, m) = cell(F, 2):

- cell(3,2): no edge exists (0 recorded instances of DL2JA-2 hearing DK5EN-1)
- cell(4,2) = 15 (edge "4,2,15") → DL2JA-2 heard DL2UD-1 **15** times
- cell(5,2) = 66 (edge "5,2,66") → DL2JA-2 heard DB0ED-99 **66** times
- cell(11,2) = 23 (edge "11,2,23") → DL2JA-2 heard DL2JA-1 23 times

By the paper's own rule, the winner should be **DB0ED-99 (66 hits)**, not DL2UD-1 (15 hits) — a 4.4x margin, and DB0ED-99 is also "nicht schwach" (f=25, s=1, s/(f+s)=3.8% < 10%, f+s=26≥4), so it is eligible.

The "159" the paper actually cites is cell(2,4) = "2,4,159" = "Y(4) hat X(2) gehört" = **DL2UD-1 heard DL2JA-2**, the _reverse_ relation from what the rule needs, and not even the right pair for the direction the rule specifies (should be cell(F,m), the paper used cell(m,F)). The same reversal appears one line later for DB0ED-99: "2,5,138" is DB0ED-99 hearing DL2JA-2 (not vice versa).

This is not a rounding or wording slip: it flips the concrete Zubringer selection shown in Abb. 6 and used as the running example threaded through 4.8, and it contradicts the fixture (Anhang B) the paper explicitly says it builds on ("Baut auf: … Nachbarschaftsmatrix-Kantenpool.html").

Severity: high — a worked numeric example that is supposed to demonstrate the algorithm gets the algorithm's own output wrong when checked against the cited data.

### C2 — Section 4.8 measurement reproduces exactly, but the causal interpretation built on it does not hold

**Reproduction.** Following the paper's own method (own POS frames DK5EN-98→* sent 2026-09-23 00:12–2026-09-24 08:46; "erste Hand" = neighbour token directly after DK5EN-98 in an echo path; "nur zweite Hand" = neighbour appears later in the path but never directly after DK5EN-98, for the same own-POS msg_id), against `~/meshlog/dk5en-98/2026-09-23.log` + `2026-09-24.log` (filtered to the stated window, msg_ids read from `NEW-POS … DK5EN-98>` lines, echoes read from `[LOG] … DK5EN-98,…>` lines):

- 65 own POS frames, 56 with ≥1 echo, 9 with none — **matches exactly**.
- DK5EN-1 44/0 (0%), DB0ED-99 25/1 (4%), DL2UD-1 18/1 (5%), DL2JA-2 12/13 (52%) — **matches exactly**, including the derived "Anteil zweite Hand" percentages.

This is a clean, independently-reproducible measurement — flagging it as a check that came out fine, not an inconsistency.

**But the interpretation drawn from it is not supported:**

1. "Nur zweite Hand" is a property of _which copy of a duplicate frame triggered the node's logged relay_, not a property of _whether the node's radio decoded the direct transmission_. MeshCom's relay logic schedules a TX on first decode with a CSMA backoff, and a node that overhears another station already relaying the same msg_id during its own backoff cancels its own scheduled relay (documented in this repo's own findings: "Overhear-Cancel überspringt Relays" — the cancel path exists and is not gated by RING_STATUS_DONE; and separately "Der Super-Node DL2JA-2 wiederholt im Median 109 s nach der ersten Kopie" — i.e. DL2JA-2's relays are governed by a long, resettable backoff). A DL2JA-2 that _did_ hear the direct −13 dB transmission from DK5EN-98 but was still inside its backoff when DL2UD-1's relay arrived would abort its own pending relay and instead re-arm off the overheard copy — producing exactly the "DK5EN-98,DL2UD-1,DL2JA-2" signature the paper reads as "DL2JA-2 hat sie selbst nicht gehört." The log format cannot distinguish "never decoded the direct copy" from "decoded it but the relay got superseded/cancelled by an overheard duplicate." The paper does not consider this failure mode at all.
2. Because of (1), "DL2JA-2 hat meine Aussendung … in mindestens 13 von 65 Fällen … selbst nicht gehört" (line 235) is not established by the data; the 13/65 = 20% figure is real as a _count of second-hand-only echo events_, but its causal reading ("verschluckt", i.e. lost as sole via) inherits the ambiguity in (1) and is therefore not a valid lower bound on RF misses. It may overstate genuine misses.
3. Consequently "als einziger Via hätte er diese Rahmen … verschluckt" is a logically valid _conditional_ step (if DL2JA-2 really didn't receive the direct RF copy, and unnamed relays no longer flood, then yes, the frame is lost) built on the unproven premise from (2). The paper does correctly hedge the count itself as an "Untergrenze" (line 235) because of colliding retransmissions in the flood — but that hedge addresses a different problem (undercounting due to collisions) and does not cover the dedup/overhear-cancel conflation in (1)-(2).
4. The 86% flood echo rate (line 264, "In der Flut kam für 56 von 65 eigenen Positionen ein Echo zurück, 86 %. Annahme: mit Via ist die Quote nicht kleiner, weil sich weniger Wiederholungen stören.") is explicitly labelled "Annahme" by the paper itself — reasonable self-disclosure, and it is also listed again as an open point (line 301: "ob die Echo-Quote mit Via wirklich nicht sinkt, ist eine Annahme"). Still, the stated justification only addresses one mechanism (fewer collisions under Via) and ignores the opposite mechanism: the flood's 86% is an "any-of-N" statistic across all neighbours that heard/relayed, whereas Via restricts relaying to 2–3 named nodes (Pflicht + Zubringer). Fewer independent relay opportunities can lower the observed echo rate even with less interference, unless the named nodes are reliably the best hearers — which the paper doesn't verify quantitatively (and which C1's mis-selected Zubringer directly undermines for this very example). Given it's already flagged open, severity is medium, not high — but the specific justification given for the assumption is incomplete on its own terms.

Severity: high for the causal claim in 4.8 (undermines the "≥1/5 lost" number that motivates adding a Zubringer at all, and by extension the concrete example in Abb. 6, compounding with C1); medium for the 86% assumption (self-flagged, but its own justification is one-sided).

### C3 — checks that came out fine (worth recording so they aren't re-litigated)

- RAM arithmetic in 4.7 is internally exact: every per-family sub-item (Rufzeichen, Zeilenkern, Masken, Kanten, Direkt-Erweiterung, Horizont, Kopf, Echo-Tabelle, Ringseitenfelder) sums to the stated "Ziel gesamt" (5.612 klassisch; 13.924 S3/nRF52) to the byte, and the derived per-family row-count math (8 B/Ruf × 64 = 512, 12 B/Kern × 64 = 768, 2×8 B masks × 64 = 1.024, 256×6 B edges = 1.536, 48×12 B slots = 576, 40×19 B horizon = 760, etc., and doubled/128-row equivalents for S3/nRF52) checks out exactly.
- The netto figures (-377 / -4.011 / -899 / -2.651) are consistent between the Ergebnis (BLUF, line 22) and the 4.7 table, and the apparent mismatch between the Mengengerüst row 77 ("RAM der drei Speicher": 5.829/9.463/14.663/16.415) and 4.7's "heute gesamt" (5.989/9.623/14.823/16.575) is not a bug: row 77 explicitly scopes to "der drei Speicher" (MHeard+Pfad+Matrix only), while 4.7's total additionally includes the 160 B ringNeed/ringAlone fields (every value differs by exactly 160 B, consistently across all four families).
- Stage-4 RAM delta being blank in the Ausbaustufen table (line 41, "in Stufe 3 schon enthalten") is consistent with 4.7: the Echo-Tabelle (88/152 B) and Kopf's Via-Maske/Via-Zustand are already inside the Stage-3 "Ziel gesamt" figures verified above.
- The Anhang B fixture (Kantenpool) supports the paper's headline Auto-Via claims when read with the correct cell direction and correctly filtered to the direct-neighbour pool K={DL2JA-2, DK5EN-1, DL2UD-1, DB0ED-99, DL2JA-1}: DL2JA-2 is the sole in-K hearer for exactly 8 of the 15 two-hop stations (9,13,14,16,17,18,20,21), covers all 15, and DB0ED-99 is a hearer (alongside DL2JA-2) for exactly 7 (6,7,8,10,12,15,19). Both numbers reproduce exactly against the 67-edge Anhang B list.
- SNR values in the 4.5 worked example (DL2JA-2 -13 dB, DK5EN-1 6 dB, DL2UD-1 -12 dB, DB0ED-99 -6 dB, DL2JA-1 -8 dB) match the 4.8 Auto-Via table's "hört mich mit" column exactly, and the -16 dB SYM threshold is applied in the same direction in both places (DL2JA-1's -8 dB > -16 dB correctly counts as SYM).
- The 10% "schwach" threshold in the 4.8 formula matches its own stated justification (line 247) and the reproduced percentages: DK5EN-1 0%, DB0ED-99 4%, DL2UD-1 5%, DL2JA-2 52% — all reproduce exactly from the field log using the ratio second-hand-only/(first-hand+second-hand-only) per neighbour.
- Windows appearing in more than one place (D60=60 min, HM=12 h, Echo-Tabelle fold=300 s, flood-fallback=60 min, T-Deck snapshot=10 min) are used consistently everywhere they recur (4.5, 4.8, Ausbaustufen table, 4.9).
- Abb. numbering (1–7) is sequential and matches the section each figure sits in; the "Inhalt" outline (4.1–4.9) matches the actual section headers.
- NCNT-Befund cross-reference (line 156): the claim that B1, B2, B4, B5 "entfallen ohne eigene Korrektur" while B3 and B6 are left out is consistent with the source befunde (B3 needs an on-air /N0 format change the topology alone doesn't provide; B6 is dead code unrelated to storage). The Rollout table's explicit carve-out of "REP-Mischung B2" from the Stage-2 shadow-test field-equality requirement (line 291) is the expected signature of B2 no longer reproducing, not a contradiction of the 4.3 claim.

## Not pursued further (time-boxed)

- Full field-by-field diff of every MH-JSON bit-width claim (4.3/4.6) against Anhang A/kantenpool byte layouts.
- Independent re-derivation of the fleet p90/p99/max 2-hop numbers (Anhang D) — taken as given, sourced to mcmap snapshot, out of scope for a text-only consistency pass.
