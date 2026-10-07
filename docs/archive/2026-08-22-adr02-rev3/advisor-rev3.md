# Advisor Verdict — ADR 02 Rev. 3 (docs/adr-nc-importance-backoff.md)

## VERDICT: REWORK

8 blocking defects. The document is close: the Rev.-3 substance (scope correction,
parameter revision, storm package, Stufe-2 guardrails, measurement protocols) is in and
almost all numbers check out against the evidence pack. What blocks release is a cluster
of internal contradictions — chiefly Chapter 4 still running on the Rev.-1 cap — plus
three wrong numbers.

Line numbers refer to the CURRENT working-tree file.

## Blocking defects

1. **Kap. 4.2-4.5 still normatively use IMP_CAP=8 — contradicts the Rev.-3 default of 4
   (5.1: `RELAY_IMP_CAP 4`).** Line 785 declares `IMP_CAP = 8.0` as the constant of the
   mechanism; the 4.3 table (797-805), the 4.4 backoff table (831-837) and the 4.5
   example (856-862) are all computed with CAP=8 without saying so (e.g. Imp 5.0 ->
   Slots 2..4; under the Rev.-3 default it is 0..2; Imp 2.0 -> 5..7 vs. real 3..5;
   Imp 1.0 -> 6..8 vs. real 5..7 — compare the correct dual-column table at 1266-1274).
   Fix: either recompute Kap. 4 with CAP=4, or mark 4.2-4.5 explicitly as
   "Zahlenbeispiele mit dem Rev.-1-Cap 8; Default seit Rev. 3: 4, siehe 5.1 und
   Risiko-Kapitel" and change line 785 to reference RELAY_IMP_CAP.

2. **Kap. 6 table, line 1128: "Mit Importance-Backoff senden Hubs zuerst -> Suppression
   storniert korrekt die redundanten Blaetter."** This is verbatim the refuted Rev.-2
   claim (V3: "niedrige Importance = redundant" is false — sole-provider counterexample,
   field failure 60ea7d8) that the Stufe-2 chapter itself now refutes ("Stufe 1 ist
   notwendig, aber NICHT hinreichend", 1719-1744). The surviving sentence contradicts
   the Rev.-3 correction. Fix: "... storniert die spaeter sendenden Nodes — sicher nur
   mit den E5-Guardrails (Sole-Provider-Veto), siehe Stufe-2-Kapitel." Same softening
   recommended for the synergy list at 883-887.

3. **Gate-cost arithmetic is self-contradictory in one sentence.** Lines 752-753:
   "98,6 % ... 50-%-Gate, 97,6 % auch ein 60-%-Gate — die Anhebung kostet 1,4
   Prozentpunkte." 98,6 - 97,6 = 1,0 pp (501 vs. 496 of 508 nodes = 5 Knoten,
   finder-evidence-fit:63). The "1,4 pp" (which is actually the 50 %-gate FAIL rate,
   7/508) also appears at 427-428 and 1839. Fix: "kostet 1,0 Prozentpunkte (5 Knoten)"
   in all three places.

4. **NCNT histogram double-counts one node (lines 313-314).** "21-30 | 35 (2.6%)" plus
   ">30 | 1 (0.1%)" sums the histogram to 1.324, but there are 1.323 origins; the
   evidence bucket is "21-40 | 35" TOTAL including the max=36 node (heys_analysis:43).
   Fix: "21-30 | 34" (and re-derive the bar/percent) or restore the evidence bucketing
   "21-40 | 35" without a separate >30 row.

5. **Stufen-Umbenennung inkonsistent: E1 vs. Stufenplan.** Line 460 (E1): "Ausweitung
   auf Position-Relays (Prio 4) ist Stufe-1b-Option" — but 1b is the Fleet-Release
   (line 1626); POS-Relays are Stufe **1c** (lines 1627, 1846). Fix line 460:
   "Stufe-1c-Option".

6. **Aktive Stationen "~1.340-1.440" (line 291) contradicts the evidence** ("~1370-1440
   stations active", evidence-pack:44). 1.340 is the partial-trailing-hour FRAME count
   artifact (finder-data-honesty F6, heys_analysis: last bucket covers ~20 min) — the
   very artifact this review flagged as not-a-real-minimum. Fix: "~1.370-1.440".

7. **"O(MAX_MHEARD) = 30-120 Iterationen" (line 1136) / "30-120 Float-Additionen"
   (line 1283) contradict the document's own cap table** (line 230: MAX_MHEARD is
   30 / 50 / 80; no class has 120). Rev.-1 leftover. Fix: "30-80".

8. **V18 only partially incorporated, no stated omission reason.** The blindspot table
   (277-285) covers SHORTPATH and the 508/889 population, but two verified honesty
   caveats are silently dropped: (a) the importance simulation joins a 24-h union graph
   with end-of-window nct snapshots (storm-hour edges weighted with post-storm values);
   (b) rssi-to-segment attribution is ~2,6 % uncertain. Either add one blindspot row
   ("Simulation = 24-h-Union-Graph x End-Snapshot-nct; rssi-Segment-Zuordnung ~2,6 %
   unsicher") or state in the Methode paragraph why they are omitted.

## V-item checklist (acceptance criterion 1)

- V1 Scope Text-only: IN (73-84, 460, 1066-1068, 1630-1635)
- V2 Sturm/Anker/Rapid-Fire: IN (1416-1454, E4, Stufe S)
- V3 Stufe-2-Guardrails, NC_VETO=2, imp~1.4: IN (1719-1771) — residual defect 2
- V4 IMP_CAP 4 + Untergrenze: IN (371-409, 983-986, 1399-1414) — defect 1 (Kap. 4)
- V5 ENABLE_TBEAM tot, Klassen 30/80: IN (230, 1335-1342)
- V6 Kaltstart/Uptime-Gate/Saettigung: IN (727-739, 1376-1397)
- V7 Metriken M1-M6 falsifizierbar: IN (1654-1666)
- V8 Persistenz nur T-Deck: IN (238, 1351-1363)
- V9 Dedup-Rotation 14,3 min: IN (161-163)
- V10 280 ms / 490-630 / +1,1 s: IN (1259-1261, 946-951)
- V11 Node B 5..7, Timeline, min 140 ms, 3.1-Praezisierung, Integer-Bias: IN (859,
  876-879, 839-846, 715-718, 1304-1308)
- V12 R0 existiert: IN (649-658)
- V13 /N ab v4.35p.06.11: IN (232)
- V14 mheardEpoch=Aktivitaet, Direktnachbar-Ingest: IN (237, 233)
- V15 /N-Kappung 3760-3767, MAX_MHEARD-Kommentar-Attribution: IN (231, 1241-1243)
- V16 printfdeb statt Serial.printf: IN (1100-1107)
- V17 508er-Population, 889/67 %: IN (283, 751-754)
- V18 SHORTPATH/rssi/Union/min-h: PARTIAL — defects 6 and 8
- V19 Gate historisch, Restzweck, 60 %-Kosten: IN (424-428, 1317-1331) — defect 3 (Zahl)
- V20 Stufe 0, Kill-Switch, Beta-Cluster, Abbruch, PR-Split: IN (1618-1689)
- V21 Attempt-2-Kompression, Zombie->Dedup-Voraussetzung, Erwartungsmanagement: IN
  (1456-1474, 1630-1635)

REFUTED-claims check (criterion 5): clean. No T-Beam-cap-10 as fact (dead code stated
throughout), rich-get-richer only as monitored concern ("Loop-Monitoring statt
Runaway-Angst", 1860), median-nct only as rejected metric, no mheard-refresh fix
proposal, path-length 6 correctly explained, IU5CZN-10 framed as design confirmation,
cold-start window ~10-20 min (735-736, 1388-1389), sole-provider imp ~1,4 (1730),
NC_VETO=2 pair-cancel rationale (1752-1757). All three verifier-corrected numbers made
it in.

Number spot-checks passed (criterion 2), >15 verified: 96.074/1.430/1.323/505/2.897;
nct-Median 3/p75 7/p90 11/max 36; Pfadlaengen 63,8/19,8/9,8/4,5/2,1 (Summe 96.074);
Importance p50 0,95/p95 3,95/p99 6,52/max 9,24; 4 Knoten >= Cap 8; Slot-Verteilungen
CAP 8/4/3/2 inkl. zusammengefasster 1..5-Buckets (34/96 = 4+8+22 bzw. 17+35+44);
Konkordanz 8/20 und 17/20 (CAP=4-Spalte des Leaderboards komplett nachgerechnet, exakt
16/20); known_ratio-Histogramm 7/0/32/110/359; Sturm-Stunden 3418/3616/9377/3787/3991;
IU4KCH-26 5.912 (Vorstunde 9, 1,6/s); Zombies 1.368/Wraps 867/Loops 104; Dedup 14,3/
~17/~24/~48-61 min; Flotte 64,4/33,8 %, 78,2 %/81 %; 10 Knoten >= 29, 5 exakt 30;
42 % Same-Path-Extra-Kopien (eigene Nachrechnung aus heys.jsonl: 42,4 %); Top-10 % ->
44 % (nachgerechnet 44,2 %); Top-20/397 ~= 35 %; 34-%-Population (20,0+14,2);
44-65 %/21-39 %-Kollisionsbaender; ~0,3 s Luecke; 6-17 HEYs/1. Stunde; 490-630 ms/
+1,1 s; Slot-Fenster-Arithmetik slot_start=int((1-min(imp,4)/4)*7) in Risiko-Tabelle
und Leaderboard korrekt.

## Cosmetic nits (non-blocking)

- Line 45 "groeßerer": single ß in an ae/oe/ue document — "groesserer".
- Line 317 "Ein Fuenftel aller meldenden Knoten": 20,0 % ist der Anteil an ALLEN
  Origins (inkl. nct=0); unter den meldenden sind es 23,6 %. "aller Origins" schreiben.
- Line 401 "~15-16/20": exakt nachrechenbar aus deep_analysis-Imps: 16/20.
- Lines 368-369 "7-Tage-Sicht praktisch identisch": 7-d-Top-10 %-Anteil ist 50,3 % vs.
  44,2 % (24 h) — Konzentration ist stabil bis steigend, nicht identisch.
- Line 363 "alle seine Quellen haben 7-31 Alternativhoerer": Finder sagt "alle ausser
  einer".
- Lines 1437-1438 "Rest-Skew wenige ms bis einige 10 ms, kleiner als das
  28-ms-CAD-Fenster": "einige 10 ms" ist nicht strikt kleiner als 28 ms; Verifier
  formulierte "in der Groessenordnung des CAD-Fensters".
- Lines 707-708 "typische Nodes (NC 2-5) in 3..6": bei CAP=8 sind die slot_starts
  2..5 (Fenster 2..7).
- Front-Anteil-Spannung: CAP=4 misst 7,5 % auf der Empfaenger-Population, die harte
  Untergrenze sagt "unter ~5-7 %", das 1b-Gate "<= 7 %". Ein Satz, dass der
  FLOTTEN-Anteil durch die 889 nicht beobachteten Blatt-Origins deutlich darunter
  liegt (~3 %), wuerde den scheinbaren Selbstwiderspruch aufloesen.
- Slot-Histogramm-Labels mischen Fenster-Notation ("slot 0..2" = ein Fenster) mit
  Sammel-Buckets ("slot 1..5" = drei Fenster) — kurz als "slot_start 1-3" labeln.

---

# Re-Verdict (nach Rework, prettier-formatierte Fassung): APPROVED

Alle 8 Blocking-Defekte behoben, nachgeprueft an der aktuellen Datei:

1. FIXED — 4.2 deklariert `RELAY_IMP_CAP = 4.0` (Zeile 797, mit Rev.-1-Verweis);
   Hinweisblock vor 4.3 (811-816) kennzeichnet 4.3-4.5 als Cap-8-Rechenwege und
   verweist auf die Dual-Cap-Tabelle "Topologie-Sicherheit" (existiert). Mathematik
   der Beispiele unter Cap 8 unveraendert korrekt.
2. FIXED — Kap.-6-Zeile (1151) jetzt "sicher aber erst mit den E5-Guardrails ...
   'sendet spaeter' heisst NICHT 'redundant'"; 4.5-Synergie-Punkt 3 ebenso
   umformuliert (Sole-Provider-Hinweis). Konsistent mit dem Stufe-2-Kapitel.
3. FIXED — drei Stellen (436, 763-764, 1863): "rund einen Prozentpunkt (5-7 von
   508)". Exakter Messwert ist 5 Knoten (501 vs. 496); die Hedge-Spanne ist
   vertretbar.
4. FIXED — NCNT-Histogramm 21-30 = 34, ">30 (max 36)" = 1 (Zeilen 314-315), Summe
   1.323; "Ein Fuenftel aller Origins" (318).
5. FIXED — E1 (469): "Stufe-1c-Option", konsistent mit Stufenplan und Offenen
   Punkten.
6. FIXED und VERIFIZIERT KORREKT — "~1.335-1.443 (5-min-Gauge)" (292): live gegen
   activity_series (48 h) nachgeprueft: nodeSamples-Minimum 1335 (atUnix
   1787367300 = 02:55 UTC, Stunde vor dem Sturm; Koordinator-Angabe "~03:35" ist
   um ~40 min daneben, der Wert stimmt), Maximum 1443 (13:50 UTC). Der Gauge misst
   per Definition die im trailing-3-h-Fenster aktiven Stationen — das Zeilenlabel
   "(3-h-Fenster, Tagesgang)" passt. Kein Frame-Artefakt mehr. (Hinweis, nicht
   blockierend: der Gauge ist forward-only und deckt erst ab ~22:50 UTC 08-21 ab,
   also nicht die ersten ~~8,5 h des Harvest-Fensters — das "~~" traegt das.)
7. FIXED — "30-80" an beiden Stellen (1159, 1306).
8. FIXED — neue Blindspot-Zeile (283): 24-h-Union-Graph x End-Snapshot-nct plus
   rssi-Zuordnung ~2,6 % unsicher.

Zusatzpruefung Mathematik:

- Neuer 3.1-Flow (712-729): korrekt. Cap 4: imp = NC_self (alle unbekannt),
  NC_self >= 4 -> ratio 1 -> slot_start 0 (Front); NC_self = 3 -> int(0.25*7) = 1.
  Rueckblick Cap 8: slot_start = 0 ab imp > 48/7 ~= 6,86 ("erforderte >= 7" ok),
  NC 2-5 -> Startwerte 5/4/3/2 ("2..5" ok). Aussage "kleinerer Cap macht das Gate
  wichtiger" folgt daraus sauber.
- 4.2-Block: Formel unveraendert generisch, Konstante jetzt konsistent mit 5.1.

Keine neuen Widersprueche durch das Rework gefunden. Die verbliebenen Punkte aus
der Nit-Liste sind erledigt oder kosmetisch. Freigabe.
