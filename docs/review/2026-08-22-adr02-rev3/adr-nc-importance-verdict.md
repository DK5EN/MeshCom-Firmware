# ADR NC-Importance-Backoff (Rev. 2) — Fable Verdict

> **Status: RESOLVED 2026-08-22.** Alle Befunde V1-V21 sind in ADR Rev. 3
> eingearbeitet (`docs/adr-nc-importance-backoff.md`, Commit `ab0160a5`,
> Advisor-Abnahme APPROVED nach einer REWORK-Runde — siehe `advisor-rev3.md`).

Review 2026-08-22: 8 Finder (code-claims, math, evidence-fit, data-honesty, storm/starvation,
rollout, protocol, validation) + 3 adversarial Verifier (Fable) + Orchestrator-Spot-Checks.
Evidence: 24h Produktionsdaten meshmap.oevsv.at (96.074 HEY-Frames, 1.430 Nodes) via MCP.
Alle Befunde unten sind verifiziert (CONFIRMED durch unabhängige Reproduktion).

## Kritisch (ändern Entscheidungen)

### V1: Stufe-1-Scope — Prio-3-Band trägt nur Text-Relays (<1% des Relay-Volumens)

- Beleg: getMessagePriority() txring_functions.cpp:46-73 (HEY→BACKGROUND, POS→LOW vor der
  Relay-Erkennung); Enqueue typblind lora_functions.cpp:1283. Produktion: msg 3-41/h vs.
  hey 3400-4400/h + pos ~3800/h.
- Konsequenz: Das ADR-Motivbeispiel (HEY-Beacon, 7 Kopien) läuft im Prio-5-Band, außerhalb
  von Stufe 1. Rev. 3 muss Scope explizit machen + Ausweitungsentscheidung treffen.

### V2: Single-Origin-Sturm ungeadressiert; Rapid-Fire und Anchor-Reset verschärfen

- Beleg: IU4KCH-26 5912 HEY/h gemessen. Anchor-Reset bei JEDEM RX-Ende auf unverändertem
  attempt (lora 452/1338, esp32_main 2291); attempt++ nur nach CAD-busy (esp32_main:2532)
  → TX-Starvation ohne Eskalation unter Dauerlast. Rapid-fire 100 ms fix ohne Jitter
  (lora:2153-2155), Preamble-Callback unbenutzt.
- Rev. 3: Per-Origin-Relay-Rate-Limit, Rapid-Fire-Jitter, Relay-Age-Drop, Sturm-Logzeile.

### V3: Stufe-2-Guardrail — Sole-Provider-Veto nötig, aber NC=1 reicht nicht

- Pair-Cancel-Gegenbeispiel (NC=2-Leaf, beide Provider canceln deterministisch dieselbe
  Runde). Rev. 3: Veto bei NC_reported ≤ 2 oder unbekannt + randomisierter Cancel p<1 +
  k≥2 + Suppression-Log. ADR:809/:1192 setzen "niedrige Importance = redundant" — falsch.
- Rich-get-richer-Loop (Finder F4) REFUTED: updateMheard refresht auf jedem Frame,
  30-min-Positionen (98,2% der Nodes) halten Präsenz; Restklasse HEY-only ~2%.
  Metriken: Relay-Konzentration + HEY-only-Zähler (NICHT median nct).

### V4: IMP_CAP — 8 fast wirkungslos, 3 gefährlich, Korridor 4 mit harter Untergrenze

- Messung: Importance p50 0,95 / p95 3,95 / p99 6,52 / max 9,24 (508 beobachtete Knoten).
  CAP=8: 0,8% Front, Konkordanz Top-20-Relays 8/20. CAP=3: 14,2% Front, 17/20, aber
  44-65% Front-Fenster-Kollisionen in Hub-Clustern. Untergrenze: Front-Anteil ≤ ~5-7%.
- Rev. 3: Default 4 als Flash-Setting; Konkordanz+Front-Anteil als Falsifikationskriterien.

### V5: ENABLE_TBEAM=10 ist toter Code — reale Klassen 30/70 (64,4%) und 80/100 (33,8%)

- Kein platformio-Env definiert ENABLE_TBEAM; klassische T-Beams = Fallback 30/40/30/70,
  Supreme/1W = S3 80/100/20/100. Feldbeweis: beobachtete NC-Decke exakt 30; DB0HOB-12
  (TBEAM V1.2) nct=26. Sättigung real: 10 Knoten ≥29 (5 bei exakt 30), 0 in der 80er-Klasse.
- Storm-Finder-These "T-Beam-Schwachflanke (Dedup 10)" REFUTED; bleibt: klassenweite
  30er-Kappe + Dedup 70 vs 100.

### V6: Kaltstart — Gate öffnet in deflationierte NC-Werte (~10-20 min Fenster)

- Bei CAP≤4 Front-Crowding zur Lastspitze. Fix: Uptime-Gate millis()≥1h im 3.1-Gate
  (auf Boot-leeren Boards äquivalent zu Datenalter; Präzedenz bPosFirst).
- Zusätzlich: Gate muss bei getMheardCount()==MAX_MHEARD schließen (Sättigungs-Blindheit).

### V7: Metriken nicht falsifizierbar wie formuliert

- DUP/NEW: BergLog 1,57 RF-seitig vs. Feed 0,70 nicht kommensurabel (Feed hat Same-Path-
  Mehrfachreports UND Race-Blindspot — Bias-Richtung unbekannt, keine Unterschranke).
  Kein Live-Zähler in FW (nur bLORADEBUG-printfdeb-Zeilen).
- CAD-Busy: zwei verschiedene Signale (CAD_BUSY/FREE vs. CHANNEL_UTIL), keine Baseline.
- Zombie: keine operationale Definition; msg_id-Wrap bei 999 (867 Spannen >1h = Wraps;
  1368 Kandidaten 10-60 min). Slot-Ordnung selbst: nur synchronisierte Multi-Node-Captures.

## Hoch (Fakten-/Rechenkorrekturen)

- V8: Persistenz mheardNCount[]/Pfadtabelle nur T-Deck(+SD-Setting) — Risiko "Stale NC
  nach QTH-Wechsel" ist Board-Nische (code-claims F1).
- V9: Dedup-Rotation 60 Slots / 4,2 msg/min = 14,3 min, nicht 6,5 (math F1; 2 Stellen).
- V10: "0..315 ms" falsch → real max 280 ms (Slot 8); 4.7a-Spreizung 490-630 ms statt
  560-700; +1,1 s statt +1,4 s (math F2/F3).
- V11: 4.5 Node B: Imp 1,5 → Slots 5..7 (nicht 6..8); Timeline unsortiert + Busy-Kanal
  ignoriert (math F4/F8). "immer ~210 ms" → min 140 ms (math F5). 3.1-Flow: Differenzierung
  nach Grad, nicht "alle in 0..2" (math F6). Integer-Variante: systematischer Abwärts-Bias
  (math F7).
- V12: R0 existiert (sendHey ohne Schranke; /N-Parser strukturell ohne 0) — ADR-Begründung
  "Fall existiert nicht" falsch, Fallback 1.0 bleibt richtig (protocol F4).
- V13: /N-Kanal erst ab v4.35p.06.11 (Kommentar lora 644/647/666) — strengere Grenze als
  HEY 4.35n; 81%-Zahl ist HEY-only (protocol F1).
- V14: mheardEpoch = Aktivität, nicht NC-Frische (bis ~170 min alt bei Trickle-Kadenz)
  (protocol F2). Direktnachbar-Ingest self-sufficient (updateMheard vor updateHeyPath,
  gleicher Durchlauf) (protocol F5).
- V15: /N-Kappung-Zitat: real loop_functions.cpp:3760-3767 (3753ff ist CO2) (code F2);
  MAX_MHPATH-Kommentar-Attribution (code F3).
- V16: [MC-IMP] via Serial.printf erreicht Netzkonsole 2323 nicht (net_console.h:76
  #define Serial MSerial nur wo inkludiert; lora_functions.cpp inkludiert nicht) →
  printfdeb() verwenden (rollout 1a).

## Mittel (Ehrlichkeit der Evidenz)

- V17: Importance-Population = 508 als Empfänger beobachtete Knoten; 889/1323 Origins (67%)
  nie als Empfänger sichtbar — Gate-Pass 98,6% gilt nur für diese Teilpopulation (data F3).
- V18: SHORTPATH auf dem Draht nicht erkennbar → Pfadlängen/Importance-Simulation
  unbekannt verzerrt (protocol F6). rssi-Segment-Zuordnung ~2,6% unsicher (data F1).
  24h-Union-Graph × End-Snapshot-nct (data F4). Min/h=1340 = Randstunden-Artefakt (data F6).
- V19: Gemischte Flotte historisch (81% ≥4.35n); Gate-Restzweck: Kaltstart, Alt-FW-Taschen,
  (T-Deck-)QTH-Wechsel; 50→60% kostet 1,4pp (evidence F2/F4).
- V20: Rollout-Lücken: keine Stufe 0, kein Kill-Switch (--impslot nach --shortpath-Muster,
  node_sset-Bits frei), kein Beta-Cluster (OE3/Toskana-Kandidaten), keine Abbruchschwellen,
  Stufe 3 unabhängig → vorziehen, PR-Split fehlt (rollout 1-6).
- V21: Attempt-2-Kompression der Hubs auf ACK-Band-Boden 3000-3070 (storm F6a);
  Zombie-Beschleunigung durch Front-Slots → Dedup-Erhöhung als Voraussetzung von Stufe 1
  (storm F6c); Erwartungsmanagement: Stufe 1 flottenweit fast No-op (storm F6f).

## Refuted (nicht erneut untersuchen)

- "T-Beam hat MAX_MHEARD/DEDUP 10" — toter Code, Feldbeweis Decke 30 (verifier-tbeam).
- "Rich-get-richer-Runaway Stufe 1+2" — Positionen halten Präsenz; nur HEY-only-Minderheit
  (~2%) betroffen (verifier-stufe2-logic).
- "mheard-Refresh-Fix nötig" — bereits Shipped-Verhalten (verifier-stufe2-logic).
- "IU5CZN-10 widerlegt die Formel" — bestätigt Stadt-Node-Design; Nachbarquellen haben
  7-31 Alternativhörer (evidence F7).
- "median nct als Loop-Frühwarnmetrik" — bewegt sich nicht solange Baken laufen.
- "Pfadlänge 6 = Hop-Budget-Verstoß" — 4 Mesh-Hops + Server-Segment, konsistent (protocol F7).
- "Kaltstart-Deflation dauert ~60 min" — real ~10-20 min (verifier-stufe2-logic F5).

Umsetzung: direkt als Rev. 3 in docs/adr-nc-importance-backoff.md (Operator-Entscheidung).
