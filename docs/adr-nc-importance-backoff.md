# ADR 02: Netzwichtigkeits-basierter Relay-Backoff (NC-Importance)

**Status:** Draft (Rev. 3)

**Datum:** 2026-03-16, ueberarbeitet 2026-08-22 (Rev. 2 und Rev. 3)

**Autor:** Martin DK5EN

---

## Revisionen

| Rev | Datum      | Aenderung                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| --- | ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | 2026-03-16 | Erstfassung: Importance-Formel, Slot-Mapping, Deployment-Stufen                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| 2   | 2026-08-22 | Codestand-Abgleich gegen v4.35p (inkl. Korrektur der Base-Angabe in 5.3: 4500 statt 4000 ms, und Zeitfenster in 5.2: 1 h statt 12 h): Abschnitt "Datenqualitaet des NC" (Kap. Kontext), Qualitaetsgate (3.1), Slot-Spreizung und Zeitanker (4.7), Ausbaustufe "Importance v2" aus der HEY-Pfadtabelle (Kap. 8), vier neue Risiken, verworfene Alternative 7, korrigierter Status von Stufe 2 und 3                                                                                                                                                                                                                                                                                                         |
| 3   | 2026-08-22 | Produktionsevidenz-Abgleich: 24 h Live-Daten der zentralen Serverinstanz (meshmap.oevsv.at via MCP, 96.074 HEY-Frames, 1.430 Nodes), Review mit 8 unabhaengigen Findern und adversarialer Verifikation. Neu: Kap. "Produktionsevidenz", Scope-Klarstellung (Prio-3-Band traegt nur Text-Relays), Parameterrevision (IMP_CAP 8 -> 4, Gate 50 -> 60 % plus Saettigungs- und Uptime-Bedingung), Sturm-Schutzmechanismen, Eskalationsstufen-Rollout (Stufe 0 und 0.5 neu, Dedup-Erhoehung vorgezogen), verbindliche Stufe-2-Guardrails, falsifizierbare Messprotokolle, Faktenkorrekturen (ENABLE_TBEAM ist toter Code, Persistenz nur T-Deck, Dedup-Rotation 14,3 min, R0 existiert, /N erst ab v4.35p.06.11) |

**Rev. 2 aendert keine Entscheidung aus Rev. 1.** Formel, Slot-Mechanik und
Deployment-Reihenfolge bleiben unveraendert. Ergaenzt werden ausschliesslich
der Abgleich mit dem tatsaechlichen Codestand, Absicherungen fuer den Rollout
und eine spaetere Ausbaustufe.

**Rev. 3 aendert erstmals Entscheidungen** — auf Basis gemessener Produktionsdaten,
nicht neuer Meinung: der Anwendungsbereich der Slot-Mechanik wird praezisiert
(und seine Grenzen benannt), drei Parameter werden revidiert, die
Deployment-Reihenfolge wird umgebaut, und der Sturm-/Starvation-Schutz wird
zur eigenen Entscheidung. Formel (Kap. 1) und Slot-Mechanik (Kap. 4) selbst
bleiben unveraendert.

---

## Begriffe

In diesem Dokument werden zwei verschiedene NC-Werte verwendet, die klar
unterschieden werden muessen:

| Begriff           | Kurzform    | Quelle             | Bedeutung                                                                                                                                                    |
| ----------------- | ----------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Eigener NC**    | NC_self     | `getMheardCount()` | Wie viele Stationen hoere **ich** direkt? Lokale Berechnung aus der mheard-Tabelle. Immer verfuegbar.                                                        |
| **Gemeldeter NC** | NC_reported | `mheardNCount[i]`  | Wie viele Stationen hoert **mein Nachbar i**? Wird vom Nachbarn via HEY-Payload (`R<NC>;`) mitgeteilt. Nur verfuegbar wenn der Nachbar ein HEY gesendet hat. |

**Beispiel:** Ich habe NC_self=6 (ich hoere 6 Stationen). Einer meiner Nachbarn,
Station "OE1XYZ", meldet via HEY seinen NC_reported=12 — er hoert 12 Stationen.
Das heisst: OE1XYZ ist ein groesserer Hub als ich.

Die **Netzwichtigkeit** (Importance) wird aus den NC_reported-Werten **meiner Nachbarn**
berechnet — nicht aus meinem eigenen NC_self.

---

## Kontext

### Problem: Kleine Nodes dominieren den Kanal

Im aktuellen MeshCom-Netz berechnet `csma_compute_timeout_prio()` den Backoff
ausschliesslich anhand der Nachrichten-Prioritaet und der Retry-Stufe — unabhaengig
davon, wie viele Nachbarn ein Node erreicht oder wie wichtig er fuer die Netzstruktur ist.

**Aktuelle CSMA-Parameter** (`configuration_global.h`):

| Prio           | Typ               | Base (ms) | Slots | Jitter (ms) | Backoff-Fenster |
| -------------- | ----------------- | --------- | ----- | ----------- | --------------- |
| 1 (Critical)   | ACK, DM           | 3000      | 10    | 0..350      | 3000..3350      |
| 2 (High)       | Broadcast, Gruppe | 3000      | 10    | 0..350      | 3000..3350      |
| 3 (Normal)     | **Relay**         | 4500      | 10    | 0..350      | 4500..4850      |
| 4 (Low)        | Position          | 5500      | 10    | 0..350      | 5500..5850      |
| 5 (Background) | HEY               | 5500      | 10    | 0..350      | 5500..5850      |

Slot-Groesse: 35ms (28ms CAD + 2ms TX-Switch + 5ms Safety)
Jitter: `random(0, slots + 1) × 35ms`

**Scope-Korrektur (Rev. 3, verifiziert):** "Prio 3 = Relay" ist als
**Text-Relay** zu lesen. `getMessagePriority()` (`txring_functions.cpp:46-73`)
prueft den Payload-Typ VOR der Relay-Erkennung: ein relaytes HEY (`@`) wird
**MSG_PRIO_BACKGROUND** (Prio 5), eine relayte Position (`!`)
**MSG_PRIO_LOW** (Prio 4); nur relayter Text (`:` mit `RING_STATUS_DONE`)
und unbekannte Typen landen in Prio 3. Der Relay-Enqueue selbst ist typblind
(`lora_functions.cpp:1283`). Im Produktionsnetz sind Text-Nachrichten
3-41/h gegenueber ~3.800 HEY/h und ~3.800 POS/h — **das Prio-3-Band, das
dieses ADR differenziert, traegt unter 1 % des Relay-Volumens.** Das
Motivbeispiel "Ein HEY-Beacon durchs Netz" (unten) laeuft vollstaendig im
Prio-5-Band und wird von Stufe 1 nicht beruehrt. Konsequenzen in Kap.
"Produktionsevidenz" (E1) und im Rollout-Kapitel.

**Retry-Reduktion** (`attempt` = Anzahl fehlgeschlagener CAD-Versuche):

| Attempt         | Reduktion          | Relay-Base | Relay-Fenster          |
| --------------- | ------------------ | ---------- | ---------------------- |
| 0 (Erstversuch) | keine              | 4500ms     | 4500..4850ms           |
| 1 (1. Retry)    | base × 5/6 (~17%)  | 3750ms     | 3750..4100ms           |
| 2 (2. Retry)    | base × 2/3 (~33%)  | 3000ms     | 3000..3350ms           |
| ≥3 (Rapid-fire) | → CSMA_RAPID_RX_MS | 100ms      | 100ms (Preamble-Check) |

**Problem:** Innerhalb des Relay-Bandes (Prio 3) gibt es keine Differenzierung.
Ein Blatt-Node (NC=1) hat die gleiche Backoff-Verteilung wie ein zentraler Hub (NC=12).
In der Praxis bedeutet das:

```
Nachricht M wird von 8 Nodes empfangen:

  Node A (NC=12, Hub)     → Backoff: 4637ms
  Node B (NC=3, klein)    → Backoff: 4522ms  ← sendet zuerst
  Node C (NC=1, Blatt)    → Backoff: 4589ms  ← sendet als zweiter

  B sendet → erreicht 3 Nodes → Kanal belegt
  C sendet → erreicht 1 Node  → Kanal belegt
  A sendet → erreicht 12 Nodes, aber Kanal war schon 2x belegt
```

**Resultat:** Der Hub, der mit EINER Transmission 12 Nodes versorgen koennte, kommt
erst dran, nachdem kleine Nodes den Kanal bereits mit Transmissions geringer Reichweite
belegt haben. Die bestehende Duplikat-Suppression kann nicht greifen, weil die kleinen
Nodes VOR dem Hub senden.

### Felddaten: BergLog 2026-03-13/14

> **Einordnung (Rev. 3):** Stand Maerz 2026, 5 Knoten, damalige Firmware mit
> `MAX_MHEARD = 20` — seither nicht erneut vermessen. Die qualitativen Befunde
> (Hub-Dominanz, Relay-Kaskaden, Kanallast) bestaetigt die Produktionsevidenz
> vom 2026-08; die quantitativen Zahlen (66 % CAD-Busy, NC=20-Ueberlauf,
> DUP/NEW 1,57) sind historisch und RF-seitig gemessen — sie sind mit den
> Feed-seitigen Zahlen aus Kap. "Produktionsevidenz" **nicht** direkt
> vergleichbar (Messprotokolle in Kap. "Rollout", M1/M2).

Analyse von 5 Knoten (OE1KBC, OE1XIR, OE3MAG, OE3XOC, OE3XWJ) ueber ~16 Stunden,
856.000 Log-Zeilen, 138 eindeutige Rufzeichen im Netz.

#### Netzstruktur

| Knoten | NC (Neighbor Count) | Relay-Rate | Relay-TX Anzahl |
| ------ | ------------------- | ---------- | --------------- |
| OE3MAG | 20*                 | 83%        | 3.885           |
| OE3XOC | 20*                 | 82%        | 2.742           |
| OE3XWJ | 14                  | 79%        | 2.145           |
| OE1KBC | 9                   | 71%        | 1.037           |
| OE1XIR | 20*                 | 74%        | 472             |

\* **Achtung: NC=20 ist ein Pufferueberlauf.** In dieser Firmware-Version war
`MAX_MHEARD=20`, daher ist NC=20 der Maximalwert den der Ringpuffer speichern konnte.
Die tatsaechliche Nachbar-Anzahl dieser Knoten liegt deutlich hoeher — aus der
Log-Analyse lassen sich bis zu 150 eindeutige Nodes im Netz identifizieren. Die
NC-Werte in dieser Tabelle sind daher Untergrenzen, nicht exakte Werte.

**Hub-Knoten mit NC≥20 relayen 80% des gesamten Traffics.**

#### Relay-Lawine in Zahlen

| Metrik                            | Wert                                                                                                                                                                            |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Unique Nachrichten (RX_DEDUP_NEW) | 16.032                                                                                                                                                                          |
| Duplikate (RX_DEDUP_DUP)          | 25.089                                                                                                                                                                          |
| **DUP/NEW-Ratio**                 | **1,57** (jede Nachricht wird im Schnitt 2,57x pro Node empfangen)                                                                                                              |
| CAD-Busy-Rate                     | **66%** — zwei Drittel aller TX-Versuche finden den Kanal belegt                                                                                                                |
| Max. CAD-Retries beobachtet       | 27 Versuche bis Kanal frei                                                                                                                                                      |
| Raw-RX-Buffer-Overflows           | **47.376** — betrifft nur den Webserver-Anzeigepuffer fuer Rohpakete, kein Datenverlust im Mesh-Processing. Logging mittlerweile entfernt, da es ein falsches Bild vermittelte. |
| RELAY_SUPPRESS Events             | **0** — Suppression war auf diesen Nodes nicht aktiv                                                                                                                            |

#### Zombie-Nachrichten durch Dedup-Ueberlauf

Der Dedup-Ring hatte 60 Slots. Bei 4,2 neuen Nachrichten/Minute rotiert die Tabelle
alle ~14,3 Minuten (Rev. 3: Rechenfehler korrigiert, vorher stand hier 6,5 min).
Langsame Multi-Hop-Relays (bis zu 13 Minuten beobachtet) liegen damit in derselben
Groessenordnung wie eine Rotationsperiode: die Rotation ist ein gleitendes Fenster,
ein frueher Eintrag kann also schon nach wenigen Minuten Restlebensdauer verdraengt
sein — spaete Kopien werden dann als "neu" akzeptiert → erneut relayed.

**Worst Case:** ACK-Nachricht `32312D47` wurde 46x relayed und 52x als "neu" akzeptiert,
weil der Dedup-Ring sie vergessen hatte bevor sie aufhoerte zu bounzen.

#### Beispiel: Ein HEY-Beacon durchs Netz

OE3MAG sendet HEY (msg `9867002B`) um 21:02:13:

```
+4s    zurueck via OE3AOG (1 Hop)
+8s    zurueck via OE3GJC (1 Hop)
+10s   zurueck via OE1UTW (1 Hop)
+17s   zurueck via OE1KBC→OE3MIF (2 Hops)
+6:39  zurueck via OE3CZC (1 Hop, langsamer Pfad)
+13:01 zurueck via OE3CZC→OE3MIF→OE3CZC→OE1MVA (4 Hops!)
```

Ein einziges HEY-Beacon erzeugt mindestens 7 Relay-Kopien die ueber 13 Minuten
zum Originator zurueckkommen. 21% aller Nachrichten erschoepfen alle 4 Hops (H00).

#### Hop-Count-Verteilung

| Hops verbraucht  | Anteil    |
| ---------------- | --------- |
| 0 (Origin)       | 10,5%     |
| 1 Hop            | 19,0%     |
| 2 Hops           | 26,8%     |
| 3 Hops           | 23,7%     |
| 4 Hops (Maximum) | **20,7%** |

(Summe 100,7 % — Rundungsartefakt der Einzelzeilen.)

#### Fazit der Felddaten

1. **Hub-Knoten (NC=20) sind die groessten Relay-Verstaerker** — 80% Relay-Rate
2. **Ohne Importance-Differenzierung relayen alle Nodes gleich schnell** — Hubs
   gewinnen den Kanal nicht oefter als Blaetter
3. **Relay-Suppression war nicht aktiv** — die Kombination aus Importance-Backoff
   UND Suppression wurde noch nie im Feld getestet
4. **Dedup-Ring zu klein** — 60 Slots bei 4,2 msg/min erzeugt Zombie-Nachrichten
5. **66% CAD-Busy bestaetigt CSMA-Relevanz** — in einem so belasteten Kanal entscheidet
   die Slot-Position, welcher Node den Kanal zuerst belegt

### Warum NC_self allein nicht reicht

Ein Node mit NC_self=10 kann zwei voellig verschiedene Rollen haben:

- **Hub auf dem Berg:** NC_self=10, aber die Nachbarn melden NC_reported=1-2 →
  der Hub ist deren einzige Verbindung zum Netz
- **Node in der Stadt:** NC_self=10, aber die Nachbarn melden NC_reported=8-12 →
  der Node ist voellig redundant, seine Nachbarn erreichen sich problemlos ohne ihn

NC_self sagt nur "ich hoere X Stationen". Erst die **NC_reported-Werte der Nachbarn**
(via HEY-Payload empfangen) zeigen, ob diese Stationen auf uns angewiesen sind oder nicht.

### Datenqualitaet des NC (Codestand v4.35p, geprueft 2026-08-22)

Die Importance-Formel ist nur so gut wie die NC-Werte, die in sie hineinlaufen.
Abgleich gegen den tatsaechlichen Code:

| Aspekt                       | Befund                                                                                                                                                                                                                                                                                                                                                                                                              | Fundstelle                                                                                              |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| NC_self                      | Anzahl Rufzeichen mit Aktivitaet in der **letzten Stunde**, Schluessel ist `msg_source_last` (= letzter Hop) — also echte Direktnachbarn, keine Mehr-Hop-Stationen                                                                                                                                                                                                                                                  | `mheard_functions.cpp:556`, `lora_functions.cpp:568`                                                    |
| Saettigung                   | `MAX_MHEARD` = 80 (ESP32-S3/nRF52840-Klasse, 33,8 % der Flotte), **30 (Fallback-Klasse: TLORA, klassische T-Beams, E22, Heltec V2.1 — 64,4 % der Flotte)**, 50 (nur XML-DevBoard). Der 10er-Zweig (`ENABLE_TBEAM`) ist **toter Code** — in keinem Build-Env definiert (Defekt C-11); Feldbeweis: beobachtete NC-Decke exakt 30, kein Wert darueber in der 30er-Klasse. Ueber dem Limit ist NC systematisch zu klein | `configuration_global.h:169-201`, `docs/architecture/08-defect-catalogue.md` C-11                       |
| Kappung im Positions-Payload | `/N` wird bei **99** gekappt (praktisch unerreichbar: `MAX_MHEARD <= 80`)                                                                                                                                                                                                                                                                                                                                           | `loop_functions.cpp:3760-3767`                                                                          |
| Kanal 1: Position            | `/N<nn>` wird als `aprspos.ncnt` geparst und in `mheardNCount[]` abgelegt — **erst ab v4.35p.06.11** (strengere Versionsgrenze als HEY!)                                                                                                                                                                                                                                                                            | `aprs_functions.cpp:905`, `lora_functions.cpp:644-648`/`:666-676`                                       |
| Kanal 2: HEY                 | `R<NC>;` (ab v4.35n) wird in `updateHeyPath()` geparst — nur wenn der HEY-Originator bereits in `mheardCalls[]` steht. **Fuer Direktnachbarn ist das immer erfuellt:** `updateMheard()` (`lora_functions.cpp:687`) legt den Eintrag im selben Verarbeitungsdurchlauf VOR `updateHeyPath()` an; ein einzelnes direktes HEY genuegt                                                                                   | `mheard_functions.cpp:420-446`                                                                          |
| Wer meldet nie?              | Knoten ohne Positionsaussendung **und** ohne HEY melden keinen NC. Fuer sie bleibt `mheardNCount[i] == 0`                                                                                                                                                                                                                                                                                                           | —                                                                                                       |
| 0 ist mehrdeutig             | `0` heisst "nicht bekannt" **oder** echtes `R0;` — beides existiert (Rev. 3, siehe Kap. 3). Behandlung ist fuer beide identisch konservativ (Beitrag 1.0)                                                                                                                                                                                                                                                           | siehe Kap. 3                                                                                            |
| Fenster-Inkonsistenz         | `getMheardCount()` nutzt **1 h**, der Entwurf von `getNetImportance()` nutzte in Rev. 1 **12 h** — zwei verschiedene Nachbar-Populationen fuer zwei Werte, die zusammengehoeren. In Rev. 2 auf 1 h korrigiert                                                                                                                                                                                                       | `mheard_functions.cpp:556`, Kap. 5.2                                                                    |
| Frische des NC-Werts         | `mheardEpoch[]` misst **Aktivitaet** (jeder empfangene Frame refresht), nicht NC-Frische: `mheardNCount[]` aendert sich nur bei HEY/Position. Ein gespraechiger Nachbar bleibt "aktiv", waehrend sein NC-Wert bei Trickle-Steady-State bis ~170 min alt sein kann — aelter als das 1-h-Fenster                                                                                                                      | `mheard_functions.cpp:303`, `docs/hey-supp.md`                                                          |
| Persistenz                   | `mheardNCount[]`/Pfadtabelle ueberleben Reboots **nur auf T-Deck/T-Deck Plus** (SD-Karte, zusaetzlich Setting `node_persist_to_sd`). Alle anderen Boards booten mit leerer Tabelle                                                                                                                                                                                                                                  | `mheard_functions.cpp:151-181` (`#if BOARD_T_DECK`), einziger Load-Aufrufer `t-deck/tdeck_main.cpp:158` |
| Symmetrie                    | mheard misst, **wen ich hoere** — nicht, wer mich hoert. Asymmetrische Links (Hub mit guter Antenne, schwacher Leaf-TX) ueberschaetzen die eigene Wichtigkeit                                                                                                                                                                                                                                                       | —                                                                                                       |
| Vertrauen                    | NC_reported ist eine unauthentifizierte **Selbstauskunft** des Nachbarn                                                                                                                                                                                                                                                                                                                                             | —                                                                                                       |

**Konsequenzen fuer die Implementierung (verbindlich):**

1. **Ein Zeitfenster fuer beide Werte.** `getNetImportance()` verwendet dasselbe
   1-h-Fenster wie `getMheardCount()`. Andernfalls traegt ein Nachbar zur
   Importance bei, der im eigenen gemeldeten NC gar nicht mehr auftaucht.
2. **Saettigung als Datenfehler behandeln.** Erreicht `getMheardCount()` den
   Wert `MAX_MHEARD`, ist der NC eine Untergrenze, kein Messwert. Betroffen ist
   real die 30er-Fallback-Klasse (64,4 % der Flotte): in 24 h Produktionsdaten
   melden 10 Knoten NC >= 29, davon 5 exakt am Cap — darunter Top-Relays wie
   DK7WK-12 und DB0KH-11. **Wichtig: das Qualitaetsgate aus 3.1 erkennt
   Saettigung NICHT von selbst** (es misst `known_ratio` der Nachbarn, nicht
   die eigene Tabellenfuelle) — deshalb prueft das Gate in Rev. 3 zusaetzlich
   explizit `getMheardCount() == MAX_MHEARD` (siehe 3.1).
3. **Die Kappung bei 99 ist unkritisch**, weil `1/99` ohnehin gegen null geht.
4. **Asymmetrie bleibt unbehandelt** in Stufe 1. Eine Absicherung waere, einen
   Nachbarn nur dann zu zaehlen, wenn belegt ist, dass er uns ebenfalls hoert
   (eigenes Rufzeichen in seinem HEY-Pfad bzw. Signal-Report). Siehe Kap. 8.

---

## Produktionsevidenz: 24 h Live-Netz (Rev. 3)

### Datenbasis und Methode

24-h-Harvest der zentralen MeshCom-Serverinstanz (meshmap.oevsv.at, mcmap-Proxy)
via MCP-API am 2026-08-21 14:12 bis 2026-08-22 14:20 UTC: **96.074 eindeutige
HEY-Frames** (INTERLINK-RAW-Feed, Zeilen-dedupliziert), dazu die vollstaendige
Node-DB (1.430 Eintraege) und die netzweite Link-Load-Sicht (2.897 gerichtete
Segmente/24 h). Jede Feed-Zeile traegt `path` (Origin, Relays, letzter Sender),
`rssi` (Hop-Segmente inkl. mheard-Count der Relays ab 4.35n), `nct`
(= `R<NC>;` des Origins) und `gw`-Flag — das ist exakt der Datenkanal, den die
Importance-Formel konsumiert, von aussen beobachtet.

**Was der Feed NICHT ist (gilt fuer jede Zahl in diesem Kapitel):**

| Blindspot                                                                                                                                                                                                 | Konsequenz                                                                                                                           |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| Gateway-Race: pro RF-Frame ueberlebt meist nur ein Report — zugleich existieren Same-Path-Mehrfachreports (42 % der Extra-Kopien)                                                                         | Feed-Duplikatquote (0,70) ist **weder Ober- noch Unterschranke** der RF-Duplikation; nicht mit BergLog 1,57 (RF-seitig) vergleichbar |
| 63,8 % der Frames sind Gateway-Selbstuploads (Pfadlaenge 1, i. d. R. ohne RF-Messung)                                                                                                                     | Pfadlaengen beschreiben die Server-Sicht, nicht die Luft                                                                             |
| Trickle unterdrueckt HEYs gerade gut vernetzter Knoten; `S1`-Flag stoppt weitere Uploads                                                                                                                  | Traffic-Zahlen sind keine Airtime/Occupancy                                                                                          |
| SHORTPATH-Relays sind auf dem Draht von echten 1-Hop-Pfaden nicht unterscheidbar                                                                                                                          | Pfadlaengen-Histogramm und Simulations-Graph in unbekanntem Anteil verzerrt (Default ist SHORTPATH aus)                              |
| Die Simulation verknuepft einen 24-h-Union-Graphen mit einem End-of-Window-nct-Snapshot; die rssi-Segment-zu-Hop-Zuordnung ist fuer ~2,6 % der Segmente nicht verifizierbar (Alt-Firmware-Segmentluecken) | Importance-Werte sind eine eingefrorene Momentaufnahme, keine 24-h-Messung; Relay-mheard-Tabelle "~97 % positionssicher" lesen       |
| Importance-Simulation umfasst nur die 508 Knoten, die im Fenster als **Empfaenger** auftauchen; 889 von 1.323 Origins (67 %) sind reine Blaetter ohne Empfaenger-Evidenz                                  | Prozentzahlen zu Gate/Slots gelten fuer die Relay-/Gateway-Population, nicht fuers Gesamtnetz                                        |
| Feed-Ausfall 2026-08-21 ~17:22-18:15 UTC (3 kurze INTERLINK-Outages, Uptime 97,5 %)                                                                                                                       | Der 17-Uhr-Einbruch ist Instrument, nicht Netz                                                                                       |

### Netzueberblick

| Groesse                              | Wert (24 h)                                           |
| ------------------------------------ | ----------------------------------------------------- |
| Nodes in der DB                      | 1.430                                                 |
| davon aktiv (3-h-Fenster, Tagesgang) | ~1.335-1.443 (5-min-Gauge)                            |
| HEY-Origins im Fenster               | 1.323                                                 |
| Gateways registriert                 | 505                                                   |
| HEY-Frames im Feed                   | ~3.800/h (Spitze 9.377)                               |
| POS-Frames im Feed                   | ~3.800/h                                              |
| Text-Nachrichten                     | 3-41/h                                                |
| Firmware >= 4.35n (sendet `R<NC>;`)  | ~81 % (4.35p allein: 78,2 %)                          |
| Board-Klassen                        | 64,4 % Fallback (MAX_MHEARD 30), 33,8 % S3/nRF52 (80) |

### NCNT-Verteilung: was die Formel heute als Eingabe bekommt

`nct` der 1.323 Origins (letzter Frame je Origin):

```
    0 (unbek.) | ##############################              202  (15.3%)
             1 | ########################################    265  (20.0%)
             2 | ############################                188  (14.2%)
             3 | #########################                   167  (12.6%)
           4-5 | ##########################                  174  (13.2%)
           6-8 | ######################                      145  (11.0%)
          9-12 | ##############                               94  ( 7.1%)
         13-20 | ########                                     53  ( 4.0%)
         21-30 | #####                                        34  ( 2.6%)
  >30 (max 36) |                                               1  ( 0.1%)
```

Median 3, p75 7, p90 11, Maximum 36. **Ein Fuenftel aller Origins meldet
genau eine gehoerte Station** — die Blatt-Population, um deren Versorgung es
in diesem ADR geht, ist real und gross. Die beobachtete Decke von exakt 30 in der
Fallback-Klasse ist der Feldbeweis fuer die `MAX_MHEARD`-Kappe (10 Knoten
melden >= 29, 5 davon exakt 30).

### Pfadlaengen: was die HEY-Pfade heute schon discovern

```
 1 (Selbstupload GW) | ########################################  61264  (63.8%)
 2 (direkt gehoert)  | ############                              19058  (19.8%)
 3 (1 Relay)         | ######                                     9404  ( 9.8%)
 4 (2 Relays)        | ###                                        4357  ( 4.5%)
 5 (3 Relays)        | #                                          1982  ( 2.1%)
 6 (4 Relays + GW)   |                                               9  ( 0.0%)
```

36 % der Frames tragen mindestens einen RF-Hop und damit verwertbare
Topologie-Evidenz: wer wen hoert (Pfadpaare), mit welchem mheard-Count die
Relays arbeiten (38.767 3-Wert-Segmente von 397 Relays), und wo Gateways
sitzen. **Die HEY-Pfade liefern heute schon die Zwei-Hop-Sicht, die Kap. 8
(Importance v2) als Datengrundlage annimmt** — netzweit, taeglich, ohne
Firmware-Aenderung auswertbar.

### Leaderboard: reale Relay-Arbeit vs. simulierte Importance

Simulation: Hoer-Graph aus den Pfadpaaren (X vor Y im Pfad -> "Y hoert X"),
Importance = Sigma(1/nct) ueber die gehoerten Nachbarn (unbekannt = 1,0),
exakt nach Kap. 1-3. Top-Relays nach Zwischenhop-Auftreten:

| Relay                   | Zwischenhop-Auftritte | eigener NC | Imp (Sim) | Imp-Rang /508 | Slots (CAP=8) | Slots (CAP=4) |
| ----------------------- | --------------------: | ---------: | --------: | ------------: | ------------- | ------------- |
| DB0HOB-12 (T-Beam V1.2) |                 1.164 |         26 |      6,74 |             5 | 1..3          | 0..2          |
| DB0TVI-1 (E22)          |                 1.120 |         14 |      3,64 |            33 | 3..5          | 0..2          |
| OE3XIA-12 (T-Beam V1.1) |                   803 |         27 |      9,24 |             1 | 0..2          | 0..2          |
| IQ5ARI-13 (TLORA)       |                   705 |         22 |      5,63 |            10 | 2..4          | 0..2          |
| DB0FTS-1 (Heltec Stick) |                   635 |         12 |      5,64 |             9 | 2..4          | 0..2          |
| DB0IBH-90 (TLORA)       |                   609 |         14 |      3,82 |            29 | 3..5          | 0..2          |
| IR5UDV-10 (TLORA)       |                   607 |         12 |      3,27 |            44 | 4..6          | 1..3          |
| DB0RVB-99 (Heltec V3)   |                   588 |         14 |      2,85 |            57 | 4..6          | 2..4          |
| IU5CZN-10 (TLORA)       |                   489 |          — |      0,66 |           304 | 6..8          | 5..7          |
| IQ5ARI-11 (Heltec V3)   |                   481 |         15 |      5,02 |            14 | 2..4          | 0..2          |

Die Formel trifft die reale Arbeitsverteilung gut (Spitzenlast = Spitzen-Rang),
mit dem lehrreichen Ausreisser **IU5CZN-10**: Last-Rang 9, Importance-Rang 304.
Nachbarschaftsanalyse: alle Quellen bis auf eine (verkehrslose) haben 7-31
Alternativhoerer im dichten Toskana-Cluster — das ist exakt der "Stadt-Node"
aus Kap. 2.6, kein Formelfehler. Seine Last ist Verkehrsdichte, nicht
Abhaengigkeit; faellt er aus, bleibt niemand unversorgt.

Konzentration: Top-10 % der Knoten tragen 44 % der Segment-Traversals
(7-Tage-Sicht: 50 % — die Schieflage ist stabil bis leicht steigend, kein
Tagesartefakt).

### Importance-Verteilung und die IMP_CAP-Frage

Perzentile der simulierten Importance (508 Knoten): p50 **0,95**, p75 1,77,
p90 3,11, p95 **3,95**, p99 6,52, max 9,24. Nur 4 Knoten erreichen den
Rev.-1-Cap von 8.

Slot-Startverteilung nach Cap-Wahl (Mapping aus 4.2):

(`start N` = `slot_start = N`, also Sendefenster Slots N..N+2; die Zeile
`start 1-3` fasst drei Startwerte zusammen)

```
IMP_CAP = 8 (Rev. 1):
 start 0 (0..2) | #                                             4  ( 0.8%)
      start 1-3 | ######                                       34  ( 6.7%)
        start 4 | #######                                      52  (10.2%)
        start 5 | ##############                              111  (21.9%)
 start 6 (6..8) | ########################################    307  (60.4%)

IMP_CAP = 4 (Rev. 3, Default):
 start 0 (0..2) | ########                                     38  ( 7.5%)
      start 1-3 | ###################                          96  (18.9%)
        start 4 | ##############                               67  (13.2%)
        start 5 | ########################                    116  (22.8%)
 start 6 (6..8) | ########################################    191  (37.6%)

IMP_CAP = 3 (verworfen):
 start 0 (0..2) | ####################                         72  (14.2%)  <- Crowding
```

| CAP | Front-Anteil (slot_start <= 2) | Top-20-Last-Relays vorn | Bewertung                                                                                                                                       |
| --- | -----------------------------: | ----------------------: | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| 8   |                          0,8 % |                    8/20 | fail-safe, aber fast wirkungslos: 60 % aller Knoten teilen sich Slot 6..8                                                                       |
| 4   |                          7,5 % |                   16/20 | Arbeitspunkt: Workhorses vorn, Front-Population an der Vertraeglichkeitsgrenze                                                                  |
| 3   |                         14,2 % |                   17/20 | verworfen: in Hub-Clustern (Ko-Hoerer-Gruppen ~10, Clustering-Faktor 2-3) kollidieren 44-65 % der Front-Fenster — schlechter als der Status quo |
| 2   |                         26,4 % |                       — | verworfen: Front-Slots verlieren jede Prioritaetsbedeutung                                                                                      |

**Harte Untergrenze (verbindlich): IMP_CAP darf nur so weit gesenkt werden,
dass der Flotten-Anteil mit `slot_start <= 2` unter ~5-7 % bleibt** — pro
Release gegen die aktuellen Importance-Perzentile nachzupruefen. Wer mehr
Front-Aufloesung will, verbreitert `RELAY_TOTAL_SLOTS` (4.7a), statt den Cap
weiter zu senken. (Zur Einordnung der 7,5 % bei CAP=4: das ist der Anteil an
der beobachteten Empfaenger-Population; auf die Gesamtflotte gerechnet — die
889 unbeobachteten Blatt-Origins haben strukturell niedrige Importance —
liegt der echte Front-Anteil deutlich darunter, grob ~3 %. Die Untergrenze
ist damit eingehalten, bleibt aber die zu ueberwachende Groesse.)

### Qualitaetsgate: die Datenlage ist da

`known_ratio` (Anteil der gehoerten Nachbarn mit bekanntem nct), 508 Knoten:

```
            0% | #                                             7  ( 1.4%)
        25-49% |                                               0  ( 0.0%)
        50-74% | ####                                         32  ( 6.3%)
        75-99% | ############                                110  (21.7%)
          100% | ########################################    359  (70.7%)
```

98,6 % passieren das 50-%-Gate schon heute, 97,6 % auch ein 60-%-Gate
(Vorsicht: Empfaenger-Population, s. o.). Das Kollaps-Szenario aus 3.1
("alle Nachbarn unbekannt") ist mit 81 % Firmware-Durchdringung
**historisch** — das Gate bleibt trotzdem, schuetzt aber jetzt die
Randfaelle: Kaltstart, lokale Alt-Firmware-Taschen, (T-Deck-)QTH-Wechsel.
Die Anhebung auf 60 % kostet gemessen rund einen Prozentpunkt (5-7 der 508
beobachteten Knoten fallen zusaetzlich auf den vollen Jitter zurueck).

### Sturm, Zombies, Schleifen: die Pathologien sind real

**Der gemessene Sturm:** 2026-08-22, 04:00-05:00 UTC —

```
Stunde (UTC)  HEY-Frames
02:00 | ##############                                 3418
03:00 | ###############                                3616
04:00 | ########################################       9377   <- IU4KCH-26: 5912 allein
05:00 | ################                               3787
06:00 | #################                              3991
```

Ein einzelner Origin (IU4KCH-26) sendete 5.912 HEYs in einer Stunde (1,6/s;
in der Vorstunde: 9). Kein Mechanismus dieses ADRs beruehrt diesen Fall — und
zwei bestehende Firmware-Eigenschaften verschaerfen ihn (Anker-Reset,
jitterloser Rapid-Fire; Details im Risiko-Kapitel). Daraus folgt E4.

**Zombies und Schleifen:** 1.368 msg_ids tauchen nach 10-60 min erneut auf
(Zombie-Kandidaten; die 867 Spannen > 1 h sind dagegen fast sicher
msg_id-Zaehler-Wraps — der Zaehler wrappt bei 999). 104 Pfade enthalten ein
Rufzeichen doppelt (echte Schleifen, z. B.
`DB0MGN-1,DB0DOL-1,DB0TVI-1,DB0FTS-1,DB0TVI-1`). Der Dedup-Ring ist heute
nachweislich poroes — unabhaengig von Stufe 1/2. Daraus folgt die
Vorziehung der Dedup-Erhoehung (Stufe 0.5).

### Evidenzbasierte Anpassungen (E1-E8)

| #   | Anpassung                                                                                                                                                                                                                                                                                                                                     | Beleg                                                                         | Wo umgesetzt                    |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- | ------------------------------- |
| E1  | **Scope klarstellen und erweitern:** Stufe 1 wirkt nur im Text-Relay-Band (< 1 % Volumen). Entscheidung: Stufe 1 bleibt zunaechst Text-only (die Nachrichten, auf die Menschen warten, bekommen die korrekte Reihenfolge); Ausweitung auf Position-Relays (Prio 4) ist Stufe-1c-Option nach Messung; HEY-Relays (Prio 5) erst zusammen mit E4 | Verifikation getMessagePriority; Traffic-Split                                | Kontext (Scope-Box), Rollout    |
| E2  | **IMP_CAP 8 -> 4**, als Flash-Setting statt #define, mit harter Untergrenze Front-Anteil <= 5-7 %                                                                                                                                                                                                                                             | Perzentile, Konkordanz-/Crowding-Tabelle                                      | 5.1, Rollout                    |
| E3  | **Gate verschaerfen:** `RELAY_IMP_MIN_KNOWN_PCT` 50 -> 60; zusaetzlich Saettigungs-Bedingung (`getMheardCount() == MAX_MHEARD` -> Gate zu) und Uptime-Hold-down (`millis() >= 1 h` -> vorher Gate zu)                                                                                                                                         | Gate-Pass 98,6 %; Saettigungsblindheit; Kaltstart-Deflation (~10-20 min)      | 3.1, 5.1-5.3                    |
| E4  | **Sturm-Schutz als eigene Entscheidung:** Per-Origin-Relay-Rate-Limit (Token-Bucket, z. B. min. 2 s Abstand pro Origin fuer HEY/POS-Relays), Jitter auf Rapid-Fire (`100 ms + random(0,3)*35 ms`), Alters-Drop fuer Relay-Queue-Eintraege (~30 s), Sturm-Logzeile pro Origin                                                                  | IU4KCH-26; Anker-Reset-/Konvoi-Verifikation                                   | Risiko, Rollout (eigenes Paket) |
| E5  | **Stufe-2-Guardrails verbindlich:** Cancel nur bei k >= 2 Duplikaten UND eigener Importance unter Schwelle UND Sole-Provider-Veto mit **NC_VETO = 2** (kein aktiver Nachbar mit NC_reported <= 2 oder unbekannt) UND randomisiertem Cancel (p < 1, z. B. 0,75) UND Log pro Suppression                                                        | Pair-Cancel-Gegenbeispiel; Feldausfall 60ea7d8                                | Stufe-2-Kapitel                 |
| E6  | **Rollout als Eskalationsstufen** mit Stufe 0 (Beobachtung), Stufe 0.5 (Dedup vorziehen), Beta-Cluster, Abbruchschwellen                                                                                                                                                                                                                      | Rollout-Review                                                                | Rollout-Kapitel                 |
| E7  | **Kill-Switch** `--impslot on/off/auto` nach `--shortpath`-Muster (node_sset-Bit, save_settings)                                                                                                                                                                                                                                              | Stufe-2-Historie: Revert brauchte Reflash-Zyklus                              | Rollout                         |
| E8  | **Metriken falsifizierbar machen** (Definition, Messseite, Fenster, Baseline, Abbruchschwelle je Metrik)                                                                                                                                                                                                                                      | Validierungs-Audit: keine der vier Rev.-2-Metriken war messbar wie formuliert | Rollout (Messprotokolle)        |

---

## Entscheidung

### 1. Metrik: Netzwichtigkeit (Net Importance)

Die Netzwichtigkeit eines Knotens wird aus den **NC_reported-Werten seiner Nachbarn**
berechnet:

```
Wichtigkeit = Σ (1 / NC_reported[i])   fuer alle aktiven Nachbarn i
```

Nicht der eigene NC_self bestimmt die Wichtigkeit, sondern: **Wie abhaengig sind
meine Nachbarn von mir?** Das erfahren wir aus deren NC_reported — je kleiner deren
NC_reported, desto weniger Alternativen haben sie, desto mehr sind sie auf uns angewiesen.

#### Was die Formel ausdrueckt

`1 / NC_reported` ist der **Verantwortungsanteil** den ich fuer diesen Nachbarn trage:

| NC_reported des Nachbarn | Beitrag `1/NC_reported` | Bedeutung                                           |
| ------------------------ | ----------------------- | --------------------------------------------------- |
| 1                        | 1.000                   | Nachbar hoert NUR mich → 100% Verantwortung         |
| 2                        | 0.500                   | Nachbar hoert 1 weitere Station → 50% Verantwortung |
| 3                        | 0.333                   | Nachbar hat 2 Alternativen → 33% Verantwortung      |
| 5                        | 0.200                   | Gut angebunden → 20% Verantwortung                  |
| 10                       | 0.100                   | Hochredundant → 10% Verantwortung                   |
| 20                       | 0.050                   | Sehr redundant → kaum Verantwortung                 |

Die Summe ergibt: **Wie viele "voll-abhaengige Knoten-Aequivalente" haengen an mir.**

Ein Wert von 5.0 bedeutet: "Es haengen effektiv 5 Knoten vollstaendig an mir."
Ein Wert von 0.5 bedeutet: "Meine Nachbarn haben genug Alternativen."

#### Nicht-Linearitaet ist gewuenscht

Der Sprung von NC=1 auf NC=2 (1.0 → 0.5, -50%) ist riesig — korrekt, denn der
Unterschied zwischen "einziger Weg" und "eine Alternative" ist fundamental.

Der Sprung von NC=10 auf NC=11 (0.100 → 0.091, -9%) ist vernachlaessigbar — korrekt,
denn bei 10 Alternativen macht eine mehr keinen Unterschied.

### 2. Topologie-Validierung

#### 2.1 Stern (Hub mit abhaengigen Blaettern)

```
B   C  D  E   F          (NC_self=1, melden NC_reported=1 via HEY)
 \  |  |  |  /
       A                (NC_self=5)
```

| Node | NC_self | Nachbarn und deren NC_reported | Importance          | Rolle              |
| ---- | ------- | ------------------------------ | ------------------- | ------------------ |
| A    | 5       | B→1, C→1, D→1, E→1, F→1        | 1+1+1+1+1 = **5.0** | Hub, sendet zuerst |
| B    | 1       | A→5                            | 1/5 = **0.2**       | Blatt, wartet      |

Korrekt: A versorgt 5 abhaengige Knoten (alle melden NC_reported=1 → haengen nur an A).
B braucht nicht zu relayen, A hat das erledigt.

#### 2.2 Lineare Kette

```
          A --- B --- C --- D --- E
NC_self:  1     2     2     2     1
```

| Node | NC_self | Nachbarn → NC_reported | Importance           | Reihenfolge      |
| ---- | ------- | ---------------------- | -------------------- | ---------------- |
| A    | 1       | B→2                    | 1/2 = **0.50**       | 4.               |
| B    | 2       | A→1, C→2               | 1/1 + 1/2 = **1.50** | 1. (teilt mit D) |
| C    | 2       | B→2, D→2               | 1/2 + 1/2 = **1.00** | 3.               |
| D    | 2       | C→2, E→1               | 1/2 + 1/1 = **1.50** | 1. (teilt mit B) |
| E    | 1       | D→2                    | 1/2 = **0.50**       | 4.               |

Korrekt: B und D sind die Knoten direkt neben den isolierten Endpunkten (A meldet
NC_reported=1, E meldet NC_reported=1). B und D tragen die hoechste Verantwortung.

#### 2.3 Dichtes Mesh (alle sehen alle)

```
A--B--C
|\/|\/|        alle NC_self=5, alle melden NC_reported=5
|/\|/\|
D--E--F
```

| Node  | NC_self | Nachbarn → NC_reported | Importance          |
| ----- | ------- | ---------------------- | ------------------- |
| jeder | 5       | 5 Nachbarn, alle →5    | 5 × (1/5) = **1.0** |

Korrekt: Alle gleichwertig, maximale Redundanz. Kein Node ist wichtiger als ein anderer.

#### 2.4 Bridge zwischen zwei Clustern

```
Cluster 1:               Cluster 2:
A---B           X           D---E
 \ /           / \           \ /
  C       A,B,C   D,E,F       F

NC_self:  A,B,C=3    X=6    D,E,F=3
NC_reported (via HEY): alle melden ihren NC_self
```

| Node        | NC_self | Nachbarn → NC_reported       | Importance                 |
| ----------- | ------- | ---------------------------- | -------------------------- |
| A (Cluster) | 3       | B→3, C→3, X→6                | 1/3 + 1/3 + 1/6 = **0.83** |
| X (Bridge)  | 6       | A→3, B→3, C→3, D→3, E→3, F→3 | 6 × (1/3) = **2.0**        |

Korrekt: X ist die Bridge und hat die hoechste Importance. Alle 6 Nachbarn von X
melden NC_reported=3 — sie haben wenig Alternativen und sind auf X angewiesen.

#### 2.5 Zwei Bridges (Redundanz)

Gleiche Topologie, aber mit X und Y als parallele Bridges:

```
Cluster-Nodes: NC_self=4 (2 Cluster-Kollegen + X + Y), melden NC_reported=4
X und Y:       NC_self=7 (3+3+1), melden NC_reported=7
```

| Node        | Nachbarn → NC_reported            | Importance                       |
| ----------- | --------------------------------- | -------------------------------- |
| A (Cluster) | B→4, C→4, X→7, Y→7                | 1/4 + 1/4 + 1/7 + 1/7 = **0.79** |
| X (Bridge)  | A→4, B→4, C→4, D→4, E→4, F→4, Y→7 | 6×(1/4) + 1/7 = **1.64**         |

Korrekt: X ist immer noch die wichtigste, aber von 2.0 auf 1.64 gesunken.
Die Nachbarn melden jetzt NC_reported=4 statt 3 (sie haben ja durch Y eine
zusaetzliche Verbindung). Die Formel erkennt automatisch die neue Redundanz.

#### 2.6 Entscheidender Vergleich: Berg-Hub vs. Stadt-Node

Beide Nodes haben den gleichen NC_self=10. Der Unterschied liegt in den
**NC_reported-Werten ihrer Nachbarn**:

**Berg-Hub:** NC_self=10, Nachbarn sind Taeler/Almen die wenige Stationen hoeren

```
Nachbarn melden via HEY: NC_reported = 1, 1, 2, 1, 3, 2, 1, 2, 1, 3

Importance = 1/1 + 1/1 + 1/2 + 1/1 + 1/3 + 1/2 + 1/1 + 1/2 + 1/1 + 1/3
           = 1 + 1 + 0.5 + 1 + 0.33 + 0.5 + 1 + 0.5 + 1 + 0.33
           = 7.16
```

**Stadt-Node:** NC_self=10, Nachbarn sind alle gut vernetzt und melden hohe NC_reported

```
Nachbarn melden via HEY: NC_reported = 8, 10, 12, 9, 11, 8, 10, 9, 12, 10

Importance = 1/8 + 1/10 + 1/12 + 1/9 + 1/11 + 1/8 + 1/10 + 1/9 + 1/12 + 1/10
           = 0.125 + 0.1 + 0.083 + 0.111 + 0.091 + 0.125 + 0.1 + 0.111 + 0.083 + 0.1
           = 1.03
```

|            | NC_self | NC_reported der Nachbarn           | Importance | Backoff              |
| ---------- | ------- | ---------------------------------- | ---------- | -------------------- |
| Berg-Hub   | 10      | 1, 1, 2, 1, 3, 2, 1, 2, 1, 3       | **7.16**   | kurz → sendet zuerst |
| Stadt-Node | 10      | 8, 10, 12, 9, 11, 8, 10, 9, 12, 10 | **1.03**   | lang → wartet        |

Gleicher NC_self, aber 7x verschiedene Importance. Der Unterschied kommt ausschliesslich
aus den NC_reported-Werten. Der Berg-Hub hat Nachbarn die auf ihn angewiesen sind
(niedrige NC_reported), der Stadt-Node hat Nachbarn die genug Alternativen haben
(hohe NC_reported).

### 3. Mixed Mode: Nachbarn ohne NC_reported

#### Das Problem

Nicht alle Nachbarn haben bereits ein HEY gesendet. In `mheardNCount[i]` steht
dann `0` — das heisst: wir kennen deren NC_reported nicht. Das betrifft:

- Nodes mit alter Firmware (kein HEY-Support → senden kein NC_reported)
- Neue Nodes die gerade erst aufgetaucht sind (noch kein HEY empfangen)
- Nodes deren letztes HEY laenger als ein Trickle-Zyklus zurueckliegt

Wir koennen nicht unterscheiden zwischen "NC_reported=0 weil unbekannt" und
"NC_reported=0 weil (noch) keine Nachbarn". **Korrektur Rev. 3: der zweite Fall
existiert sehr wohl** — `sendHey()` hat keine `getMheardCount() > 0`-Schranke
(`loop_functions.cpp:4257ff`), ein frisch gebooteter oder wirklich isolierter
Knoten sendet ein echtes `R0;`, und `updateHeyPath()` schreibt die 0 ungefiltert
in `mheardNCount[]` (anders als der Positions-Kanal, dessen Parser `/N0`
strukturell nicht akzeptiert). Die Behandlung bleibt trotzdem dieselbe: auch ein
echt isolierter Nachbar haengt maximal an uns — Beitrag 1.0 ist fuer beide
Lesarten die richtige konservative Wahl. Nur die alte Begruendung ("der Fall
existiert nicht") war falsch; kuenftige Implementierungen duerfen
`mheardNCount[i] == 0` also nicht als "sicher befuellbar aus anderer Quelle"
interpretieren.

#### Strategie: Konservativ (unbekannt = abhaengig)

```
Beitrag eines Nachbarn zur Wichtigkeit:
  NC_reported bekannt (> 0):    1 / NC_reported
  NC_reported unbekannt (== 0): 1.0  (Annahme: koennte isoliert sein, NC_self=1)
```

**Begruendung:** Im Zweifel nehmen wir an, dass der Nachbar auf uns angewiesen ist.
Das fuehrt zu einer erhoehten Importance → kuerzerer Backoff → wir relayen lieber
einmal zu viel als einmal zu wenig.

**Selbstkorrektur:** Sobald HEY-Daten eintreffen (spaetestens nach 30s bis 15min
durch Trickle), korrigiert sich der Wert nach unten.

#### Zahlenbeispiel

Node X hat 6 Nachbarn (NC_self=6). Von 3 hat er ein HEY empfangen, von 3 nicht:

```
NC_reported bekannt:   A→5, B→3, C→8
NC_reported unbekannt: D→?, E→?, F→?

Importance = 1/5 + 1/3 + 1/8 + 1.0 + 1.0 + 1.0
           = 0.2 + 0.33 + 0.125 + 1.0 + 1.0 + 1.0
           = 3.66   (konservativ hoch)

Spaeter, nach HEY-Empfang von D, E, F: D→7, E→4, F→10

Importance = 1/5 + 1/3 + 1/8 + 1/7 + 1/4 + 1/10
           = 0.2 + 0.33 + 0.125 + 0.143 + 0.25 + 0.1
           = 1.15   (korrigiert)
```

Der Wert sinkt von 3.66 auf 1.15 sobald die NC_reported-Daten vorliegen.
Im Uebergangszustand sendet der Node "zu frueh" — das ist sicher, nur nicht optimal.

#### 3.1 Qualitaetsgate: differenzieren nur bei belastbarer Datenlage

Die konservative Annahme "unbekannt = 1.0" ist pro Nachbar richtig, kollabiert
aber, wenn sie fuer **alle** Nachbarn gleichzeitig gilt — genau der Zustand einer
gemischten Flotte am Rollout-Tag:

```
Alle Nachbarn unbekannt  →  jeder Beitrag 1.0
                         →  Importance = NC_self
                         →  Slot-Position folgt dem rohen Grad (NC_self),
                            nicht der Abhaengigkeit — und mit dem
                            Rev.-3-Cap 4 landet bereits JEDER Node mit
                            NC_self >= 4 im Front-Fenster 0..2
                         →  Differenzierung nach der falschen Groesse
                         →  UND: Jitterfenster nur noch 3 Slots statt 10
                         →  mehr Kollisionen als im Ist-Zustand
```

(Praezisierung Rev. 3: unter dem alten Cap 8 war "jeder halbwegs vernetzte
Node landet in Slots 0..2" uebertrieben — `slot_start = 0` erforderte
Importance >= 7, typische Nodes lagen bei Startwerten 2..5. Unter dem neuen
Cap 4 stimmt das Bild fast woertlich: NC_self >= 4 reicht fuers
Front-Fenster. Der kleinere Cap macht das Gate also WICHTIGER, nicht
obsolet.)

Das ist die Konstellation, in der Stufe 1 **schlechter** ist als der
Status quo: die Ordnungswirkung faellt aus, die Entzerrungswirkung des vollen
10-Slot-Jitters aber auch. Deshalb ein explizites Gate — in Rev. 3 mit drei
Bedingungen, alle muessen erfuellt sein, sonst gilt die alte Berechnung mit
vollem Jitter:

```cpp
// Gate (Rev. 3): Importance-Slots nur wenn ALLE drei Bedingungen halten:
//  (1) known_ratio >= RELAY_IMP_MIN_KNOWN_PCT  — genug Nachbarn melden NC
//  (2) getMheardCount() < MAX_MHEARD           — eigene Tabelle nicht gesaettigt
//                                                (Saettigung = NC ist Untergrenze,
//                                                Importance nicht belastbar; das
//                                                misst known_ratio NICHT mit)
//  (3) millis() >= RELAY_IMP_UPTIME_MS         — Uptime-Hold-down: nach Boot/
//                                                Netz-Kaltstart sind gemeldete
//                                                NC-Werte ~10-20 min systematisch
//                                                deflationiert -> Importance
//                                                transient ueberhoeht
#define RELAY_IMP_MIN_KNOWN_PCT  60           // Prozent (Rev. 3: 50 -> 60)
#define RELAY_IMP_UPTIME_MS      (60UL*60UL*1000UL)  // 1 h, Praezedenz: bPosFirst
```

Damit ist der Rollout **selbst-gatend**: solange zu wenige Nachbarn NC melden,
verhaelt sich der Node exakt wie heute. Erst wenn genug Nachbarn neue Firmware
haben, schaltet die Differenzierung von selbst zu — ohne koordinierten
Stichtag, und ohne dass ein einzelner frueher Node sich selbst faelschlich in
die vorderen Slots setzt. (Ein zusaetzlicher **operator-seitiger** Schalter
`--impslot on/off/auto` kommt in Rev. 3 trotzdem dazu — das Gate misst
Datenverfuegbarkeit, nicht Ordnungs-Korrektheit, und kann einen fachlich
misslungenen Rollout nicht abbrechen. Siehe Rollout-Kapitel.)

Produktionsstand 2026-08: 98,6 % der beobachteten Empfaenger-Population
passieren das 50-%-Gate, 97,6 % auch ein 60-%-Gate — die Anhebung kostet
rund einen Prozentpunkt (5-7 von 508 Knoten) und kauft Schutzmarge fuer die
Faelle, fuer die das Gate heute noch da ist: Kaltstart, lokale
Alt-Firmware-Taschen, QTH-Wechsel.

### 4. Slot-basierte Importance-Differenzierung

#### 4.1 Grundprinzip: Base bleibt, Slots differenzieren

Zwei getrennte Achsen bestimmen den Relay-Backoff:

| Achse             | Bestimmt durch          | Zweck                                                     |
| ----------------- | ----------------------- | --------------------------------------------------------- |
| **Base** (ms)     | Retry-Attempt (0, 1, 2) | Fairness: wer laenger wartet, kommt frueher dran          |
| **Slot-Position** | Importance              | Netzwichtigkeit: wer wichtiger ist, bekommt vordere Slots |

**Die Base wird NICHT veraendert.** Sie bleibt bei 4500ms fuer alle Relays.
Die bestehende Retry-Reduktion (×5/6, ×2/3) bleibt der Mechanismus fuer Nodes
die laenger warten mussten.

**Nur die Slot-Position** wird durch die Importance bestimmt:

- Wichtiger Node → vordere Slots (0..2) → kuerzerer Jitter → sendet frueher
- Unwichtiger Node → hintere Slots (7..9) → laengerer Jitter → sendet spaeter

#### 4.2 Slot-Bereich-Berechnung

Innerhalb des Relay-Bandes stehen 10 Slots zur Verfuegung. Jeder Node bekommt
ein Fenster von 3 aufeinanderfolgenden Slots, dessen Position von der Importance
abhaengt:

```
RELAY_TOTAL_SLOTS  = 10   // Gesamtanzahl Slots (0..9)
RELAY_JITTER_WIDTH = 3    // Jeder Node waehlt aus 3 aufeinanderfolgenden Slots
RELAY_IMP_CAP      = 4.0  // Importance-Obergrenze (Rev. 3; Rev. 1: 8.0 —
                          // Herleitung und harte Untergrenze im Kap.
                          // Produktionsevidenz)

imp_ratio  = min(importance, RELAY_IMP_CAP) / RELAY_IMP_CAP   // 0.0 .. 1.0
slot_start = (int)((1.0 - imp_ratio) * (RELAY_TOTAL_SLOTS - RELAY_JITTER_WIDTH))
             //    ↑ invertiert: hohe Importance → niedriger slot_start

slot = slot_start + random(0, RELAY_JITTER_WIDTH)         // 3 moegliche Slots
backoff = base + slot × CSMA_SLOT_SIZE                    // base = 4500ms (Attempt 0)
```

#### 4.3 Slot-Zuordnung nach Importance

> **Hinweis (Rev. 3):** Die Zahlenbeispiele in 4.3-4.5 rechnen mit dem
> historischen Rev.-1-Cap **8**, damit die Rechenwege der Erstfassung
> nachvollziehbar bleiben. Mit dem Rev.-3-Default **4** verschieben sich die
> Fenster nach vorn — die Gegenueberstellung beider Caps fuer alle
> Topologie-Klassen steht in der Tabelle "Topologie-Sicherheit"
> (Risiko-Kapitel); die Mechanik selbst ist identisch.

| Importance         | imp_ratio | slot_start | Slot-Bereich | Jitter (ms) | Backoff (Att.0) |
| ------------------ | --------- | ---------- | ------------ | ----------- | --------------- |
| 8.0 (max/cap)      | 1.00      | 0          | 0, 1, 2      | 0..70       | 4500..4570      |
| 7.2 (Berg-Hub)     | 0.90      | 0          | 0, 1, 2      | 0..70       | 4500..4570      |
| 5.0 (Stern-Hub)    | 0.63      | 2          | 2, 3, 4      | 70..140     | 4570..4640      |
| 2.0 (Bridge)       | 0.25      | 5          | 5, 6, 7      | 175..245    | 4675..4745      |
| 1.0 (Cluster)      | 0.13      | 6          | 6, 7, 8      | 210..280    | 4710..4780      |
| 0.5 (kleiner Node) | 0.06      | 6          | 6, 7, 8      | 210..280    | 4710..4780      |
| 0.2 (Blatt)        | 0.03      | 6          | 6, 7, 8      | 210..280    | 4710..4780      |

**Beobachtungen:**

- **Berg-Hub vs. Bridge:** Minimum 105ms Vorsprung (Slot 2 vs. Slot 5). Der Hub
  startet sein CAD bevor die Bridge ueberhaupt ihren Timer abgelaufen hat.
- **Hubs unter sich:** Berg-Hub und Stern-Hub haben leicht ueberlappende Bereiche
  (Slot 2 ist in beiden). Das ist gewollt — aehnlich wichtige Nodes sollen sich
  per Zufall entzerren, nicht deterministisch blockieren.
- **Kleine Nodes:** Cluster, kleine Nodes und Blaetter landen alle in Slots 6..8.
  Das ist korrekt — sie sind alle "unwichtig" und sollen alle erst senden nachdem
  die Hubs fertig sind. Untereinander entzerren sie sich per Zufall.

#### 4.4 Interaktion mit Retry-Reduktion

Die Retry-Reduktion veraendert **nur die Base**, die Slot-Position bleibt gleich:

```
Attempt 0:  base = 4500ms                (Erstversuch)
Attempt 1:  base = 4500 × 5/6 = 3750ms   (1. Retry, ~17% schneller)
Attempt 2:  base = 4500 × 2/3 = 3000ms   (2. Retry, ~33% schneller)
Attempt ≥3: 100ms                         (Rapid-fire, wie bisher)
```

**Vollstaendige Backoff-Tabelle (Relay, mit Slot-Bereich):**

| Importance      | Slots | Attempt 0    | Attempt 1 (base 3750) | Attempt 2 (base 3000) |
| --------------- | ----- | ------------ | --------------------- | --------------------- |
| 7.2 (Berg-Hub)  | 0..2  | 4500..4570ms | 3750..3820ms          | 3000..3070ms          |
| 5.0 (Stern-Hub) | 2..4  | 4570..4640ms | 3820..3890ms          | 3070..3140ms          |
| 2.0 (Bridge)    | 5..7  | 4675..4745ms | 3925..3995ms          | 3175..3245ms          |
| 1.0 (Cluster)   | 6..8  | 4710..4780ms | 3960..4030ms          | 3210..3280ms          |
| 0.2 (Blatt)     | 6..8  | 4710..4780ms | 3960..4030ms          | 3210..3280ms          |

**Wichtige Eigenschaft:** Die Slot-Differenzierung bleibt bei Retries **konstant**.
Der Vorsprung des Hubs gegenueber dem Blatt betraegt **mindestens 140 ms**
(Worst Case: Hub wuerfelt Slot 2, Blatt Slot 6) und **typisch ~210 ms**
(beste Slots beider Fenster, Slot 0 vs. Slot 6) — unabhaengig vom Attempt.
(Rev. 3: die alte Formulierung "immer ~210 ms" war der Best-Best-Fall, nicht
die Garantie; die Worst-Case-Rechnung folgt derselben Disziplin wie das
"Minimum 105 ms" in 4.3.) Das ist der entscheidende Unterschied zum alten
Ansatz mit variabler Base — dort schrumpfte die Differenzierung bei Retries.

**Band-Separation:** Bei Attempt 2 liegt der Hub bei 3000..3070ms. Das ist
am unteren Rand des Broadcast-Bandes (3000ms), aber das ist **bestehendes Verhalten** —
schon heute landen Relays bei Attempt 2 in diesem Bereich (3000..3350ms).
Die Importance-Aenderung verschaerft das nicht.

#### 4.5 Zahlenbeispiel: Kaskadeneffekt

```
Nachricht M wird von 5 Nodes empfangen (Attempt 0):

Node A: NC_self=10, NC_reported der Nachbarn=1-2 → Imp=7.2 → Slots 0..2  → 4500..4570ms
Node B: NC_self=6,  NC_reported=3-5              → Imp=1.5 → Slots 5..7  → 4675..4745ms
Node C: NC_self=3,  NC_reported=5-8              → Imp=0.5 → Slots 6..8  → 4710..4780ms
Node D: NC_self=1,  NC_reported=10               → Imp=0.1 → Slots 6..8  → 4710..4780ms
Node E: NC_self=2,  NC_reported=3,8              → Imp≈0.46 → Slots 6..8 → 4710..4780ms

t=0ms      Nachricht M empfangen (alle 5 Nodes)
t=4500ms   Node A (Slot 0): CAD frei → beginnt Relay-TX (Paketdauer 300-1000ms)
t=4745ms   Node B (Slot 7): CAD → Kanal BUSY (A sendet noch) → Attempt 1
t=4780ms   Nodes C,D,E: CAD → Kanal BUSY → Attempt 1
t≈4900ms   A beendet TX; B,C,D,E haben A's Relay inzwischen als Duplikat
           empfangen → (kuenftige) Suppression kann ihre Relays stornieren
t≈8500ms   ohne Suppression: B sendet als naechster (Attempt-1-Backoff),
           C/D/E hoeren das zweite Duplikat

Ergebnis: 1-2 Transmissions statt 5
```

(Rev. 3: Node-B-Zeile korrigiert — Imp 1,5 ergibt `slot_start = 5`, nicht 6;
Timeline chronologisch sortiert und mit dem Busy-Kanal-Mechanismus aus dem
naechsten Abschnitt konsistent gemacht: B's CAD um t=4745 faellt in A's
laufende Transmission.)

**Slot-Backoff und Duplikat-Suppression verstaerken sich gegenseitig:**

1. Slot-Position sorgt dafuer, dass die wichtigen Nodes **zuerst** senden
2. Ihre Transmissions erzeugen Duplikate bei den weniger wichtigen Nodes
3. Duplikat-Suppression storniert die Relays der spaeter sendenden Nodes —
   das ist nur mit den E5-Guardrails sicher: auch ein spaet sendender Node
   kann Sole Provider eines versteckten Blatts sein (Stufe-2-Kapitel)
4. Ohne Importance-Slots senden die kleinen Nodes zuerst → Suppression greift beim
   Hub → falsche Richtung!

#### Warum 210ms im CSMA-Kontext ausreichen

Der Slot-Vorsprung von 210ms (6 Slots) wirkt nicht wie ein TDMA-Zeitfenster, sondern
als **CSMA/CAD-Prioritaet**. Der entscheidende Mechanismus:

```
t=4500ms   Hub (Slot 0) macht CAD → Kanal FREI → startet Transmission
t=4535ms   Hub sendet (Paket dauert 300-1000ms, Kanal belegt)
t=4710ms   Blatt (Slot 6) macht CAD → Kanal BUSY → wartet (Attempt 1)
t=4800ms   Hub beendet Transmission
t=4810ms   Blatt macht CAD erneut → Kanal FREI
           ABER: Blatt hat inzwischen Hub's Relay als Duplikat empfangen
           → Suppression storniert das eigene Relay → kein TX
```

Der Hub muss seinen TX nicht innerhalb der 210ms **abschliessen**. Er muss nur
innerhalb der 210ms sein CAD durchfuehren und mit der Transmission **beginnen**.
Ab diesem Zeitpunkt blockiert CSMA/CAD automatisch alle spaeter startenden Nodes.

**Felddaten bestaetigen dies:** 66% CAD-Busy-Rate bedeutet, dass Nodes den Grossteil
ihrer Zeit damit verbringen, auf einen freien Kanal zu warten. Ein struktureller
Vorsprung von 6 CAD-Slots (210ms) gibt dem Hub praktisch eine Garantie, den Kanal
vor den Blaettern zu belegen.

Die Slot-Differenzierung ist damit **kein Wettrennen auf Millisekunden**, sondern
ein **Vorfahrts-System**: der Hub geht als erster an die Kreuzung, und CSMA regelt
den Rest.

#### 4.6 Review: Slot-Anzahl der anderen Priority-Baender

Die Einfuehrung von 10 Relay-Slots erfordert eine Ueberpruefung der Slot-Anzahlen
aller Priority-Baender:

| Prio           | Typ       | Slots aktuell | Begruendung                                                                      |
| -------------- | --------- | ------------- | -------------------------------------------------------------------------------- |
| 1 (Critical)   | ACK, DM   | 10            | Einheitlich 10 Slots = 11 Positionen. Ausreichend fuer ACK-Kollisionsvermeidung. |
| 2 (High)       | Broadcast | 10            | Einheitlich 10 Slots.                                                            |
| 3 (Normal)     | Relay     | 10            | Durch Importance-Fenster (3er-Breite) in Subbereiche unterteilt.                 |
| 4 (Low)        | Position  | 10            | Einheitlich 10 Slots.                                                            |
| 5 (Background) | HEY       | 10            | Einheitlich 10 Slots.                                                            |

Alle Priority-Baender verwenden einheitlich 10 Slots. Die Differenzierung
erfolgt ausschliesslich ueber die Base-Timeouts (3000/3000/4500/5500/5500ms).

#### 4.7 Spreizung und Zeitanker (Rev. 2)

Zwei Punkte, die den erwartbaren Effekt der Slot-Differenzierung begrenzen und
in Rev. 1 fehlten.

**a) Ist die Spreizung breit genug?**

6 Slots Vorsprung sind 210 ms. Dem stehen gegenueber: ein CAD-Scan von ~28 ms
(so ist `CSMA_SLOT_SIZE = 35` in `configuration_global.h:221` begruendet),
TX-Switch, Timer-Granularitaet der Hauptschleife und Paketlaufzeiten von
300–1000 ms. Die Argumentation aus 4.5 bleibt richtig — der Hub muss nur
**beginnen**, nicht fertig werden — aber der Sicherheitsabstand ist duenn.

Option fuer die Feldvalidierung: `RELAY_TOTAL_SLOTS` **nur fuer das Relay-Band**
auf 16–20 erhoehen (erreichbare Spreizung 490–630 ms; Rev. 3: der hoechste
erreichbare Slot ist `TOTAL - 2`, nicht `TOTAL`, siehe Fenster-Mechanik in
4.2), Base und alle anderen Baender unveraendert. Kosten: das
Relay-Backoff-Fenster waechst von 4500..4850 ms auf bis zu 4500..5130 ms, im
4-Hop-Fall also bis zu +1,1 s Ende-zu-Ende-Latenz.

Das ist eine echte Abwaegung, keine freie Verbesserung — deshalb als
Tuning-Parameter im Feldtest zu messen, nicht vorab zu setzen.

**b) Der Backoff-Timer hat keinen globalen Nullpunkt.**

Der Backoff laeuft gegen `iReceiveTimeOutTime`, das bei **jedem** RX-Ende neu
gesetzt wird (`lora_functions.cpp:1338`, ESP32-Pendants in `esp32_main.cpp`).
Daraus folgt:

- Alle Nodes, die dieselbe Nachricht M gehoert haben, starten ihren Timer am
  selben Ereignis (Ende von M) — genau dort wirkt die Slot-Ordnung wie gedacht.
- Nodes, die dazwischen **anderen** Verkehr hoeren, verschieben ihren Anker.
  In einem Kanal mit 66 % CAD-Busy passiert das haeufig.
- Bei Hidden-Node-Konstellationen (A und B hoeren M, aber nicht einander)
  wuerfelt sich die Reihenfolge ohnehin neu.

Die Slot-Differenzierung ist damit ein **statistischer** Vorteil ueber viele
Nachrichten, keine Garantie pro Einzelnachricht. Das ist fuer den Zweck
ausreichend — Suppression (Stufe 2) muss nur haeufiger die Blaetter treffen als
die Hubs, nicht immer. Die Feldmetrik muss entsprechend ueber Stunden mitteln,
nicht Einzelfaelle bewerten.

### 5. Betroffener Code

#### 5.1 Neue Konstanten

**Datei:** `src/configuration_global.h`

```cpp
// NC-Importance Relay-Slot-Steuerung (Werte Rev. 3, siehe Kap. Produktionsevidenz)
#define RELAY_IMP_CAP            4    // Importance-Obergrenze fuer Slot-Mapping
                                      // (Rev. 1: 8 — gemessen fast wirkungslos;
                                      // 3 verworfen: Front-Crowding. Harte
                                      // Untergrenze: Front-Anteil <= 5-7 %)
#define RELAY_TOTAL_SLOTS       10    // Gesamtanzahl Relay-Slots (0..9), siehe 4.7a
#define RELAY_JITTER_WIDTH       3    // Breite des Slot-Fensters pro Node
#define RELAY_IMP_MIN_KNOWN_PCT 60    // Qualitaetsgate (3.1): Mindestanteil der
                                      // aktiven Nachbarn mit bekanntem NC_reported
                                      // (Rev. 1: 50)
#define RELAY_IMP_UPTIME_MS (60UL*60UL*1000UL)  // Uptime-Hold-down (3.1, Kaltstart)
```

`RELAY_IMP_CAP` und `RELAY_IMP_MIN_KNOWN_PCT` sind fuer die Feldvalidierung
als **Flash-Settings mit diesen Compile-Defaults** auszufuehren (Muster:
bestehende `meshcom_settings`-Tunables), nicht als harte `#define`s — beide
sind erklaertermassen zu validierende Werte, und ein Tuning-Zyklus darf keinen
flottenweiten Reflash erfordern. `RELAY_TOTAL_SLOTS`/`RELAY_JITTER_WIDTH`
bleiben Compile-Konstanten (CSMA-Timing).

#### 5.2 Importance-Berechnung

**Datei:** `src/mheard_functions.cpp` (neue Funktion)

```cpp
/**
 * Berechne Netzwichtigkeit als Summe der inversen NC_reported-Werte.
 * Hoher Wert = viele abhaengige Nachbarn = Node ist wichtig fuers Netz.
 *
 * NC_reported = mheardNCount[i], vom Nachbarn via HEY gemeldet.
 * NC_reported == 0 bedeutet: kein HEY empfangen → konservativ als 1 behandeln.
 */
float getNetImportance()
{
    float importance = 0.0f;
    unsigned long now = getUnixClock();

    for(int i = 0; i < MAX_MHEARD; i++)
    {
        if(mheardCalls[i][0] != 0x00)
        {
            if((mheardEpoch[i] + 60*60) > now)      // aktiv (letzte Stunde,
                                                   // gleiches Fenster wie
                                                   // getMheardCount())
            {
                int nc_reported = mheardNCount[i];   // NC_reported des Nachbarn
                if(nc_reported > 0)
                    importance += 1.0f / (float)nc_reported;
                else
                    importance += 1.0f;              // unbekannt → konservativ
            }
        }
    }
    return importance;
}

/**
 * Anteil der aktiven Nachbarn (letzte Stunde) mit bekanntem NC_reported,
 * in Prozent. Basis fuer das Qualitaetsgate aus 3.1.
 */
int getNetImportanceKnownPct()
{
    int total = 0, known = 0;
    unsigned long now = getUnixClock();

    for(int i = 0; i < MAX_MHEARD; i++)
    {
        if(mheardCalls[i][0] != 0x00 && (mheardEpoch[i] + 60*60) > now)
        {
            total++;
            if(mheardNCount[i] > 0)
                known++;
        }
    }
    if(total == 0)
        return 0;          // keine Nachbarn -> kein Grund zu differenzieren
    return (known * 100) / total;
}
```

#### 5.3 Anpassung CSMA-Backoff

**Datei:** `src/lora_functions.cpp` — Funktion `csma_compute_timeout_prio()`

Nur der `MSG_PRIO_NORMAL`-Case wird angepasst — das sind **Text-Relays und
unbekannte Payload-Typen** (Scope-Korrektur im Kontext-Kapitel; relayte
HEYs/Positionen laufen in Prio 5/4 und bleiben in Stufe 1 unveraendert).
Die Base bleibt `CSMA_PRIO_BASE_3` (4500ms), nur die Slot-Berechnung aendert sich.
Das Qualitaetsgate aus 3.1 steht vor der Importance-Rechnung: ist die Datenlage
zu duenn, gesaettigt oder zu jung, bleibt alles beim bisherigen Verhalten.

```cpp
case MSG_PRIO_NORMAL:   // Text-Relay (und unbekannte Typen)
{
    // Qualitaetsgate (3.1, Rev. 3 — drei Bedingungen):
    //  zu wenige bekannte NC_reported, eigene mheard-Tabelle gesaettigt,
    //  oder Uptime < 1 h  ->  alte Berechnung mit vollem 0..10-Jitter
    if(getNetImportanceKnownPct() < RELAY_IMP_MIN_KNOWN_PCT
       || getMheardCount() >= MAX_MHEARD
       || millis() < RELAY_IMP_UPTIME_MS)
    {
        base = CSMA_PRIO_BASE_3;
        if(attempt >= 2) base = base * 2 / 3;
        else if(attempt >= 1) base = base * 5 / 6;
        return base + (unsigned long)random(0, CSMA_PRIO_SLOTS_3 + 1) * CSMA_SLOT_SIZE;
    }

    float imp = getNetImportance();
    float imp_capped = (imp > (float)RELAY_IMP_CAP) ? (float)RELAY_IMP_CAP : imp;
    float imp_ratio = imp_capped / (float)RELAY_IMP_CAP;   // 0.0 .. 1.0

    base = CSMA_PRIO_BASE_3;   // 4500ms — unveraendert!

    // Importance bestimmt Slot-Position: hohe Imp → vordere Slots
    int slot_start = (int)((1.0f - imp_ratio)
                     * (float)(RELAY_TOTAL_SLOTS - RELAY_JITTER_WIDTH));
    int slot = slot_start + (int)random(0, RELAY_JITTER_WIDTH);

    if(bDisplayInfo || bLORADEBUG)
        printfdeb("[MC-IMP] imp=%.2f known_pct=%d slot_start=%d slot=%d sat=%d\n",
            imp, getNetImportanceKnownPct(), slot_start, slot,
            (getMheardCount() >= MAX_MHEARD) ? 1 : 0);
    // printfdeb, NICHT Serial.printf: nur printfdeb wird auf die Netzkonsole
    // (Port 2323) gespiegelt (net_console.h definiert Serial->MSerial nur dort,
    // wo es inkludiert ist — lora_functions.cpp gehoert nicht dazu). Ein rohes
    // Serial.printf waere fuer alle nicht per USB erreichbaren Nodes unsichtbar.

    // Retry-Reduktion auf Base (wie bisher)
    if(attempt >= 2) base = base * 2 / 3;
    else if(attempt >= 1) base = base * 5 / 6;

    return base + (unsigned long)slot * CSMA_SLOT_SIZE;
}
```

**Alle anderen Priority-Cases bleiben unveraendert.** ACKs, eigene Nachrichten,
Position, HEY — alles wie bisher. Die Retry-Reduktion und Rapid-fire-Logik
(Attempt ≥3 → 100ms) bleiben ebenfalls unveraendert.

### 6. Zusammenspiel mit bestehenden Mechanismen

| Mechanismus                  | Funktion                                             | Aenderung                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ---------------------------- | ---------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CSMA/CAD                     | Channel-Sensing vor TX                               | Keine                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Priority-Queue               | ACK > Text > Relay > Pos > HEY                       | Keine                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Dedup-Ring                   | Duplikat-Erkennung                                   | Keine                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Relay-Suppression            | Relay stornieren nach N Duplikaten                   | Keine, aber **zwingende Abhaengigkeit**: Suppression darf erst NACH Importance-Backoff aktiviert werden. Ohne korrekte Sendereihenfolge storniert Suppression die Hubs (hoechste Duplikat-Rate) statt die Blaetter → abhaengige Nachbarn werden ausgehungert. Mit Importance-Backoff senden Hubs zuerst → Suppression trifft die spaeter sendenden Nodes — **sicher aber erst mit den E5-Guardrails** (Sole-Provider-Veto): "sendet spaeter" heisst NICHT "redundant", siehe Stufe-2-Kapitel. |
| Trickle-HEY                  | Adaptive HEY-Intervalle                              | Keine                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| **Neu: NC-Importance-Slots** | Relay-Slot-Position proportional zur Netzwichtigkeit | Nur `csma_compute_timeout_prio()`, Base unveraendert                                                                                                                                                                                                                                                                                                                                                                                                                                          |

### 7. Laufzeitkosten

| Operation            | Aufwand                                           | Haeufigkeit                            |
| -------------------- | ------------------------------------------------- | -------------------------------------- |
| `getNetImportance()` | O(MAX_MHEARD) = 30-80 Iterationen, Float-Addition | Pro Relay-Entscheidung                 |
| Float-Berechnung     | 3 Multiplikationen, 1 Division                    | Pro Relay-Entscheidung                 |
| Gesamt               | <1ms auf ESP32                                    | Nicht zeitkritisch (vor Backoff-Timer) |

RAM: Keine zusaetzlichen Variablen. Nutzt bestehende `mheardNCount[]` und `mheardEpoch[]`.

### 8. Ausbaustufe: Importance v2 aus der HEY-Pfadtabelle (Rev. 2)

**Status:** Ausbaustufe, **nicht** Teil von Stufe 1. Erst umsetzen, wenn Stufe 1
im Feld vermessen ist.

#### 8.1 Warum ueberhaupt eine zweite Quelle

`NC_reported` ist eine **Selbstauskunft** des Nachbarn und nur verfuegbar, wenn
der Nachbar Position oder HEY sendet. Die HEY-Pfadtabelle dagegen ist **lokal
beobachtete Evidenz** und braucht kein Vertrauen: sie zeigt, ueber welche
Nachbarn wir welche entfernten Knoten tatsaechlich erreichen.

#### 8.2 Was in der Pfadtabelle steht (`updateHeyPath()`, `mheard_functions.cpp:382-553`)

| Eigenschaft        | Wert                                                                                                                              |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------- |
| Schluessel         | **Originator** der HEY-Bake (`mh_sourcecallsign`), nicht der letzte Hop                                                           |
| Inhalt             | Relay-Kette **ohne** Originator: `"r1,r2,…,rLast"` — `rLast` ist der Direktnachbar, ueber den der Knoten erreicht wird            |
| Aufnahmekriterium  | nur **relayte** HEYs (`if(ips <= 0) return;` — direkt gehoerte Baken haben keine Kette)                                           |
| Konfliktregel      | der **kuerzeste** bekannte Pfad gewinnt (Init `0x7F`, laengere werden verworfen)                                                  |
| Gateway-Markierung | Bit `0x80` in `mheardPathLen[]`, gesetzt wenn `destination_path == "HG"`                                                          |
| Alterung           | 12 h                                                                                                                              |
| Persistenz         | nur T-Deck/T-Deck Plus (SD-Karte, Setting-gated; `mheard_functions.cpp:205-208`) — alle anderen Boards booten leer                |
| Groesse            | `MAX_MHPATH` = 100 (S3/nRF52), 40 (Fallback-Klasse inkl. realer T-Beams), 50 (XML-DevBoard); der 10er-Zweig ist toter Code (C-11) |

Wichtige Konsequenz aus dem Aufnahmekriterium: Direktnachbarn stehen in
`mheardCalls[]`, Mehr-Hop-Knoten in `mheardPathCalls[]`. Die beiden Tabellen
ueberschneiden sich nicht — genau deshalb ist die Pfadtabelle die Zwei-Hop-Sicht,
die der Importance-Formel heute fehlt.

#### 8.3 v2a — Verifizierte Redundanz statt gemeldeter

Fuer jeden Direktnachbarn A:

```
Kommt A in irgendeiner gespeicherten Kette als Zwischenhop vor,
deren letzter Hop NICHT A ist?

  ja   →  A ist auch ueber einen anderen meiner Nachbarn erreichbar
          →  A haengt nicht an mir  →  kleiner Beitrag
  nein →  A erreiche ich nur direkt  →  A haengt wahrscheinlich an mir
          →  voller Beitrag
```

Das ist inhaltlich `Sigma(1/NC_reported)` — aber gemessen statt geglaubt, und es
funktioniert auch fuer Nachbarn, die **nie** einen NC melden (alte Firmware,
Position abgeschaltet). Genau die Luecke aus Kap. 3.

#### 8.4 v2b — Bridge-Erkennung (Artikulationspunkt)

Alle Originatoren der Pfadtabelle nach ihrem **letzten Hop** gruppieren:

- Zwei oder mehr grosse, **disjunkte** Gruppen → wir sitzen zwischen Clustern,
  die sich sonst nicht sehen → netztragend im eigentlichen Sinn.
- Stark **ueberlappende** Gruppen → wir sind redundant, unabhaengig davon, wie
  viele Pfade wir gelernt haben.

Das ist die Metrik, die "netztragendes Element" tatsaechlich abbildet —
Betweenness statt Grad.

#### 8.5 v2c — Gateway-Gewichtung

Ueber Bit `0x80` ist bekannt, welche Originatoren Gateways sind. Ein Nachbar,
fuer den wir der einzige bekannte Weg zu einem Gateway sind, wiegt schwerer als
einer mit drei Gateway-Pfaden. Im Flutnetz ist die Gateway-Richtung die, in der
Verlust am meisten kostet.

#### 8.6 Kaskade mit Fallback

Pro Nachbar, in dieser Reihenfolge:

1. In fremder Kette gefunden (v2a) → verifiziert redundant, Beitrag klein.
2. Sonst `NC_reported > 0` → `1/NC_reported` wie in Kap. 1.
3. Sonst konservativ `1.0` wie in Kap. 3, plus Qualitaetsgate aus 3.1.

Optional oben drauf: Bridge-Bonus (v2b) und Gateway-Gewicht (v2c).
Die Slot-Mechanik aus Kap. 4 bleibt unveraendert — v2 ersetzt nur die Quelle
des Importance-Werts, nicht seine Verwendung.

#### 8.7 Grenzen (vor der Umsetzung zu klaeren)

- **Wir drosseln die Datenquelle selbst.** Trickle unterdrueckt laut
  `docs/hey-supp.md` im Mittel 53 % der eigenen HEYs (bis 75 % bei OE3XWJ).
  Ausgerechnet gut vernetzte Knoten senden am seltensten Baken — ihre Pfade
  lernt niemand. Die Pfadtabelle ist dort duenn, wo das Netz dicht ist.
- **SHORTPATH zerstoert v2a.** Default ist `bSHORTPATH = false`
  (`loop_functions.cpp:166`), aber pro Knoten per `--shortpath` schaltbar
  (`command_functions.cpp:581`). Dann bleibt nur `Origin,letztesRelay` — die
  Bridge-Gruppierung (v2b) funktioniert weiter, die Zwischenhop-Verifikation
  (v2a) nicht. v2a braucht deshalb zwingend den Fallback aus 8.6.
- **Der kuerzeste Pfad klebt 12 Stunden.** Ein einmal gelernter kurzer Pfad wird
  nie durch einen laengeren ersetzt, nur durch Ablauf. Nach Ausfall eines Relais
  rechnen wir bis zu 12 h mit einem Weg, den es nicht mehr gibt. Plus
  Flash-Persistenz ueber Reboots und Standortwechsel hinweg.
- **Rechenkosten.** Ein String-Scan ueber 100 x 50 Byte pro Relay-Entscheidung
  ist inakzeptabel. Der Wert wird **einmal pro Pfadtabellen-Update** gerechnet
  und als Integer gecached — die Topologie aendert sich in Minuten, nicht in
  Millisekunden.
- **Saettigung auch hier.** `MAX_MHPATH` liegt auf der Fallback-Klasse (40)
  und dem XML-Board (50) unter den 85–124 beobachteten H00-Knoten (die Zahl
  stammt aus dem MAX_MHEARD-Sizing-Kommentar, `configuration_global.h:180`);
  nur die S3/nRF52-Klasse (100) reicht an den unteren Rand heran. Die
  Gruppierung in v2b arbeitet also meist auf einer Stichprobe, nicht auf der
  vollstaendigen Nachbarschaft.
- **SHORTPATH verzerrt auch die HEUTIGE Evidenz, nicht nur v2a (Rev. 3):**
  ein SHORTPATH-Relay schreibt `Origin,EigenesCall` aufs Draht — von einem
  echten 1-Hop-Pfad nicht unterscheidbar. Das betrifft die
  Pfadlaengen-Verteilung und die Importance-Simulation im Kap.
  Produktionsevidenz in unbekanntem (vermutlich kleinem, Default ist aus)
  Anteil.

---

## Risiko-Analyse

### Topologie-Sicherheit

Base = 4500ms fuer alle. Slot-Position bestimmt Jitter (0..280 ms: hoechster
erreichbarer Slot ist 8, da `slot_start <= 6` — Rev. 3, vorher stand hier
faelschlich 315 ms; das heutige System jittert 0..350 ms).

Slot-Spalten fuer beide Cap-Werte (Rev. 3: Default ist 4; die 8er-Spalte
entspricht Rev. 1):

| Topologie   | NC_self | NC_reported         | Imp | Slots (CAP=4) | Backoff (Att.0, CAP=4) | Slots (CAP=8) | Verhalten                            |
| ----------- | ------- | ------------------- | --- | ------------- | ---------------------- | ------------- | ------------------------------------ |
| Berg-Hub    | 10      | 1,1,2,1,3,2,1,2,1,3 | 7.2 | 0..2          | 4500..4570ms           | 0..2          | Sendet zuerst — korrekt              |
| Stern-Hub   | 5       | alle →1             | 5.0 | 0..2          | 4500..4570ms           | 2..4          | Sendet zuerst — korrekt (Cap greift) |
| Bridge      | 6       | alle →3             | 2.0 | 3..5          | 4605..4675ms           | 5..7          | Nach Hubs, vor Blaettern — korrekt   |
| Full-Mesh   | 10      | alle →10            | 1.0 | 5..7          | 4675..4745ms           | 6..8          | Wartet — korrekt                     |
| Kette-Mitte | 2       | →2, →2              | 1.0 | 5..7          | 4675..4745ms           | 6..8          | Wie Full-Mesh — korrekt              |
| Stadt-Node  | 10      | alle →8-12          | 1.0 | 5..7          | 4675..4745ms           | 6..8          | Wartet — korrekt                     |
| Stern-Blatt | 1       | →5                  | 0.2 | 6..8          | 4710..4780ms           | 6..8          | Wartet — korrekt                     |

Mit CAP=4 teilen sich Berg-Hub und Stern-Hub das Front-Fenster (beide ueber
dem Cap) — gewollt: aehnlich wichtige Hubs entzerren sich per Zufall. Der
Preis der besseren Spreizung ist genau das Crowding-Risiko, das die harte
Untergrenze im Kap. Produktionsevidenz begrenzt (Front-Anteil <= 5-7 %).

### Risiko: Float auf ESP32

ESP32 hat Hardware-FPU. `getNetImportance()` fuehrt maximal 30-80 Float-Additionen
durch. Laufzeit <0.1ms. Kein Risiko.

**Alternative (Integer-only):** Falls Float unerwuenscht, kann die Berechnung
ganzzahlig approximiert werden:

```cpp
// Integer-Variante: importance × 100 als Festkomma
int getNetImportanceFixed()
{
    int importance_x100 = 0;
    for(...) {
        if(nc > 0)
            importance_x100 += 100 / nc;   // ganzzahlige Division
        else
            importance_x100 += 100;
    }
    return importance_x100;
}
```

(Rev. 3: die Integer-Variante ist **nicht** numerisch aequivalent — die
ganzzahlige Division trunkiert jeden Summanden nach unten, der Fehler ist
systematisch negativ (bei nc=13 ~9 %, nc=17 ~15 %, Worst Case nc=51 ~49 %)
und kann an Slot-Grenzen die Slot-Position kippen. Falls Integer gewuenscht:
gerundete Division `(200 + nc) / (2 * nc)` verwenden.)

### Risiko: Importance-Wert veraltet

Nachbar-NC-Werte koennen bis zu 15 Minuten alt sein (Trickle-Imax).
In der Praxis aendert sich die Topologie selten so schnell, dass dies ein
Problem darstellt. Bei Topologie-Aenderungen resettet Trickle auf 30s, sodass
neue NC-Werte schnell verbreitet werden.

### Risiko: Gemischte Flotte kollabiert die Differenzierung (historisch)

Beim Rollout-Entwurf (Maerz 2026) meldeten die meisten Nachbarn noch keinen
NC_reported; alle Beitraege waeren `1.0` gewesen, die Slot-Ordnung haette dem
rohen Grad statt der Abhaengigkeit gefolgt — bei auf 3 Slots verengtem Jitter.

**Stand 2026-08 (gemessen):** 81 % der Flotte melden NC, 98,6 % der
beobachteten Empfaenger-Population passieren das Gate. Das flaechendeckende
Kollaps-Szenario ist damit **historisch**. Das Gate bleibt trotzdem —
sein Restzweck sind lokale Alt-Firmware-Taschen (die verbleibenden ~19 % sind
raeumlich ungleich verteilt), Kaltstart und QTH-Wechsel.

**Gegenmassnahme:** Qualitaetsgate aus 3.1 (Rev. 3: drei Bedingungen,
Schwelle 60 %). Damit ist das Risiko konstruktiv ausgeschlossen, nicht nur
unwahrscheinlich.

### Risiko: NC-Saettigung durch MAX_MHEARD

In der 30er-Fallback-Klasse (TLORA, klassische T-Beams, E22, Heltec V2.1 —
64,4 % der Flotte) ist NC_self bei gut angebundenen Knoten eine Untergrenze:
in 24 h Produktionsdaten melden 10 Knoten NC >= 29 (5 exakt am Cap), darunter
Spitzen-Relays. Betroffene Knoten unterschaetzen sowohl den eigenen gemeldeten
NC als auch die Anzahl der Summanden ihrer Importance. Die BergLog-Daten
zeigen denselben Effekt historisch bei `MAX_MHEARD = 20`. (Rev. 3: die
frueher hier genannte T-Beam-10er-Klasse existiert nicht — `ENABLE_TBEAM` ist
toter Code, Defekt C-11.)

**Gegenmassnahmen:** (1) Das Gate schliesst bei eigener Saettigung
(`getMheardCount() == MAX_MHEARD`, 3.1 Bedingung 2) — ein gesaettigter Knoten
differenziert nicht auf Basis einer nachweislich unvollstaendigen Tabelle.
(2) Saettigungs-Flag im `[MC-IMP]`-Log fuer die Feldauswertung. Eine
automatische Korrektur ist nicht moeglich — der Wert oberhalb des Puffers ist
schlicht nicht bekannt.

### Risiko: Stale NC-Werte nach Standortwechsel (nur T-Deck)

Rev. 3: `mheardNCount[]` und Pfadtabelle ueberleben Reboots **nur auf
T-Deck/T-Deck Plus** (SD-Karte, Setting `node_persist_to_sd`; einziger
Load-Aufrufer `tdeck_main.cpp:158`). Alle anderen Boards booten mit leerer
Tabelle — fuer sie existiert dieses Risiko nicht; stattdessen greift dort der
Kaltstart-Fall (unten).

**Gegenmassnahme:** Das Uptime-Hold-down aus 3.1 (Bedingung 3) deckt auch den
T-Deck-Fall ab: in der ersten Stunde nach Reboot laeuft ohnehin die alte
Berechnung, danach ist die 1-h-Aktivitaetsalterung ueber die alten Eintraege
hinweggegangen. Ein zusaetzliches Verwerfen bei signifikanter
Positionsaenderung bleibt optional.

### Risiko: Link-Asymmetrie

mheard misst, wen wir hoeren — nicht, wer uns hoert. Ein Knoten mit sehr guter
Empfangslage, aber schwacher Abstrahlung, ueberschaetzt seine Wichtigkeit und
belegt zu Unrecht die vorderen Slots. Der Schaden ist begrenzt (er sendet zu
frueh, nicht zu oft), verschiebt aber die Ordnung.

**Gegenmassnahme:** Bidirektionalitaetsnachweis — einen Nachbarn nur voll
zaehlen, wenn das eigene Rufzeichen in dessen HEY-Pfad oder Signal-Report
auftaucht. Gehoert in die Ausbaustufe (Kap. 8), nicht in Stufe 1.

### Risiko: Kaltstart — das Gate oeffnet in deflationierte NC-Werte

Der harmlose Teil: direkt nach dem Einschalten sind alle Nachbar-NCs unbekannt
(== 0), jeder traegt `1.0` bei, Importance = NC_self — konservativ, akzeptabel.

Der tueckische Teil (Rev. 3, verifiziert): nach einem **regionalen**
Stromausfall booten viele Nodes gleichzeitig. Trickle startet bei Imin=30 s
(gemessen: 6-17 eigene HEYs pro Node in der ersten Stunde), die erste Position
geht bei millis 100-130 s raus — die mheard-Tabellen und damit `known_ratio`
fuellen sich in **~5-15 Minuten**. Das Gate oeffnet also schnell, aber die
**gemeldeten NC-Werte sind noch systematisch zu klein** (jeder meldet seinen
eigenen, gerade erst anwachsenden `getMheardCount()`): Sigma(1/klein) ist
transient ueberhoeht, und mit CAP=4 erreichen fuer ~10-20 Minuten viele
gewoehnliche Nodes `slot_start = 0` — Front-Crowding exakt zur Stunde der
hoechsten Kanallast (Restart-Burst). Das Gate aus Rev. 2 konnte das nicht
erkennen: es prueft Bekanntheit, und die Daten SIND bekannt — nur transient
falsch.

**Gegenmassnahme:** Uptime-Hold-down (3.1, Bedingung 3): in der ersten Stunde
nach Boot gilt die alte Berechnung mit vollem Jitter. Kostenlos (das Verhalten
ist exakt der Status quo), grosszuegig gegen das ~10-20-min-Fenster, und auf
allen Boards ohne Persistenz identisch mit "Datenalter >= 1 h".

### Risiko: Front-Slot-Crowding bei zu kleinem IMP_CAP (neu, Rev. 3)

Die Konkordanz-Daten (17/20 Top-Relays vorn bei CAP=3) laden dazu ein, den Cap
immer weiter zu senken. Aber Hubs clustern raeumlich: im CAP=3-Front-Fenster
liegen 17 der Top-20-Relays, und die hoeren einander (Toskana-, OE3-Cluster).
Bei Ko-Hoerer-Gruppen um 10 und Clustering-Faktor 2-3 kollidieren bei CAP=3
**44-65 %** der Front-Fenster-Erstversuche in einem 3-Slot-Fenster — mehr als
die 21-39 % des heutigen 11-Positionen-Jitters. Zwei kollidierende Hubs
verlieren beide Transmissions; unter Stufe 2 wuerden die Blaetter zusaetzlich
durch die Retry-Kopien supprimiert → regionaler Nachrichtenverlust.

**Gegenmassnahme:** harte Untergrenze aus dem Kap. Produktionsevidenz —
Flotten-Anteil mit `slot_start <= 2` muss unter ~5-7 % bleiben (bei heutiger
Verteilung: CAP >= 4). Pro Release gegen frische Perzentile pruefen. Mehr
Front-Aufloesung nur ueber breitere `RELAY_TOTAL_SLOTS` (4.7a), nie ueber
weiteres Cap-Senken. Feldmetrik: Anteil der Relay-TX mit Slot <= 2.

### Risiko: Single-Origin-Sturm — von keiner Stufe adressiert (neu, Rev. 3)

Gemessen am 2026-08-22, 04:00 UTC: IU4KCH-26 sendete 5.912 HEYs in einer
Stunde (1,6/s; Vorstunde: 9). Drei verifizierte Firmware-Eigenschaften machen
daraus ein Netzproblem:

1. **Stufe 1 greift nicht:** HEY-Relays laufen im Prio-5-Band, ausserhalb der
   Importance-Slots (Scope-Korrektur im Kontext).
2. **Anker-Reset verhindert Eskalation:** `iReceiveTimeOutTime` wird bei JEDEM
   RX-Ende neu gesetzt und `csma_timeout` beim UNveraenderten `attempt` neu
   gewuerfelt (`lora_functions.cpp:452/1338`, `esp32_main.cpp:2291`);
   `cad_attempt++` passiert nur nach einem CAD-Busy-Scan
   (`esp32_main.cpp:2532`), und ein CAD-Scan laeuft erst nach ungestoertem
   Timer-Ablauf. Unter Dauerlast (mittlere Luecke ~0,3 s) erreicht ein Node
   seinen ersten CAD nie — **alle Nodes in Hoerweite des Sturms sind
   TX-verhungert, inklusive ACKs, und koennen nicht einmal eskalieren.**
3. **Rapid-Fire ist ein Konvoi:** ab `attempt >= 3` liefert
   `csma_compute_timeout_prio()` fixe 100 ms **ohne Jitter**
   (`lora_functions.cpp:2153-2155`); der Preamble-Callback ist unbenutzt, der
   IRQ-Poll vor dem CAD erkennt nur bereits laufende Aussendungen. Wird der
   Kanal frei, feuern alle aufgestauten Rapid-Fire-Nodes im selben
   ~100-ms-Fenster ineinander (Rest-Skew wenige ms bis einige 10 ms — in der
   Groessenordnung des 28-ms-CAD-Fensters, also innerhalb der
   CAD-Blindzone).

**Gegenmassnahmen (E4, eigenes Paket, unabhaengig von Stufe 1):**

- **Per-Origin-Relay-Rate-Limit:** Token-Bucket je Origin-Rufzeichen, z. B.
  min. 2 s Abstand zwischen Relays desselben Origins fuer HEY/POS (Dedup kann
  das nicht leisten — jeder Sturm-Frame hat eine frische msg_id). Das ist der
  einzige Mechanismus, der einen defekten Beacon am ersten Relay-Ring
  de-amplifiziert.
- **Jitter auf Rapid-Fire:** `CSMA_RAPID_RX_MS + random(0, 3) * CSMA_SLOT_SIZE`.
- **Alters-Drop:** Relay-Queue-Eintraege aelter ~30 s verwerfen (Zeitstempel
  existiert als `ringEnqueueTime`) — ein 30 s altes Beacon-Relay ist
  Verstaerkung, kein Dienst.
- **Sturm-Logzeile:** Origin-Frames/min ueber Schwelle → `[MC-STORM]`-Zeile
  via printfdeb, damit das Netz seine IU4KCH-26s findet.
- Serverseitige Rate-Limits fuer Sturm-Origins sind **out-of-scope dieses ADRs**
  (Gateway-/mcmap-Politik), aber ausdruecklich empfohlen.

### Risiko: Attempt-2-Kompression auf den ACK-Band-Boden (neu, Rev. 3)

Heute verteilen sich Relays bei Attempt 2 gleichmaessig auf 3000..3350 ms.
Mit Importance-Slots landen genau die aktivsten Relays (Front-Fenster)
deterministisch bei 3000..3070 ms — dem Boden des ACK/DM/Broadcast-Bandes.
In einem belasteten Netz mit vielen Attempt-2-Relays konkurriert das
systematisch mit frischen ACKs. **Gegenmassnahme-Kandidaten (Feldtest):**
Attempt-2-Relay-Base auf >= 3150 ms klemmen oder Slots < 2 ab Attempt 2
ausschliessen; ACK-Laufzeit als Gate-Metrik (M5) beobachten.

### Risiko: Front-Slots beschleunigen Zombies (neu, Rev. 3)

1.368 msg_ids tauchen im 24-h-Feed nach 10-60 min wieder auf, 104 Pfade
enthalten Schleifen. Ein Zombie, den ein Hub erneut als "neu" akzeptiert,
bekommt mit Importance-Slots **bevorzugte** Bedienung — die Slot-Ordnung ist
zombie-blind und beschleunigt ihre Wiederverbreitung ueber die
reichweitenstaerksten Knoten. **Konsequenz: die Dedup-Ring-Erhoehung (frueher
"Stufe 3") ist eine VORAUSSETZUNG von Stufe 1, kein Nachtrag** — im
Rollout-Kapitel als Stufe 0.5 vorgezogen.

---

## Alternativen (verworfen)

### 1. Backoff nur auf NC_self

Jeder Node kennt seinen NC_self immer. Kein Mixed-Mode-Problem.

**Verworfen weil:** NC_self unterscheidet nicht zwischen Hub-auf-Berg (NC_self=10,
Nachbarn melden NC_reported=1) und redundantem Stadt-Node (NC_self=10, Nachbarn melden
NC_reported=10). Genau dafuer haben wir NC_reported via HEY eingefuehrt.

### 2. Inverse Backoff (niedrigster NC sendet zuerst)

Blatt-Knoten als Bridges priorisieren.

**Verworfen weil:** Bridges haben typischerweise hohe NC-Werte, nicht niedrige.
Eine Bridge zwischen 2 Clustern mit je 5 Nodes hat NC=10, nicht NC=2.
Blaetter mit NC=1 sind keine Bridges — sie sind Endpunkte.

### 3. Rollenbasierte Adaption (Hub/Edge auto-detection)

Vergleich eigener NC mit Durchschnitt der Nachbar-NCs.

**Verworfen weil:** Ueberkomplex. Die Importance-Formel liefert die gleiche
Information direkt und ohne kuenstliche Rollenklassifikation. Edge-Detection
ist zudem instabil bei wenig NC-Daten.

### 4. Suppression-Schwellen anpassen statt Backoff

Anstatt die Sendereihenfolge zu aendern, die Suppression aggressiver machen.

**Verworfen weil:** Suppression ist reaktiv (wartet auf Duplikate). Wenn die
falschen Nodes zuerst senden, greifen Duplikate beim Hub statt bei den Blaettern.
Die richtige Reihenfolge muss VOR der Suppression hergestellt werden.

### 5. Probabilistisches Relay (Wuerfeln statt Ordnen)

Jeder Node entscheidet per Zufall ob er relayed: `P(relay) = k / NC_self`.
Bei NC=20 und k=3 relayed ein Node mit 15% Wahrscheinlichkeit. Erwartung: 3 statt 20
Relays pro Nachricht.

**Verworfen weil:** Probabilistisches Relay ist **topologie-blind** und kann
abhaengige Knoten aushungern:

```
Ich (NC=20, P(relay) = 15%)
  ├── 19 Nachbarn mit NC=15-20 — brauchen mein Relay nicht
  └── 1 Blatt-Node (NC=1) — hoert NUR mich

Ich wuerfle: 85% Chance dass ich NICHT relay → Blatt bekommt nichts.
```

Schlimmer noch: Der "Blatt-Node" mit NC=1 koennte eine **Bridge** sein, die auf der
anderen Seite 10 weitere Knoten versorgt. Wenn ich nicht relay, verliert ein ganzer
Cluster die Nachricht.

Das Grundproblem: `P = k/NC` behandelt alle Nachbarn als austauschbar und gleichwertig
redundant. In der Realitaet haengt an einem einzigen Nachbarn mit NC_reported=1 eine
Verantwortung von 100%, waehrend 19 Nachbarn mit NC_reported=20 zusammen nur 5%
Verantwortung tragen. Probabilistisches Relay kann das nicht abbilden — die
Importance-Formel `Σ(1/NC_reported)` dagegen schon.

### 6. Importance veraendert die Base statt die Slots

Wichtige Nodes bekommen eine niedrigere Base (z.B. 3200ms statt 4000ms).

**Verworfen weil:** Die Base-Reduktion bei Retries (×5/6, ×2/3) ist der
Fairness-Mechanismus fuer Nodes die laenger warten mussten. Wenn die Importance
die Base verschiebt, vermischt das zwei verschiedene Konzepte:

- **Base** = "wie dringend" (Wartezeit, Retry-Fairness)
- **Slot** = "wie wichtig" (Netzwichtigkeit)

Die saubere Trennung: Base bleibt fuer alle gleich, nur die Slot-Position
wird durch Importance bestimmt. So bleibt die Retry-Reduktion wirksam und
die Importance-Differenzierung ist bei jedem Attempt gleich stark.

### 7. Anzahl gelernter HEY-Pfade als Wichtigkeitsmass

Naheliegend, weil die Groesse der Pfadtabelle (`mheardPathCalls[]`) ohne
Zusatzaufwand verfuegbar ist: "wer viele Pfade gelernt hat, traegt das Netz".

**Verworfen weil:** das exakt derselbe Denkfehler ist wie NC_self, nur eine
Ebene hoeher. Viele gelernte Pfade messen **Empfangsguete** — Antenne, Lage,
Verkehrsaufkommen in Hoerweite. Der maximal redundante Stadtknoten lernt die
meisten Pfade und soll gerade **spaet** senden; der Bergknoten mit drei
abhaengigen Talstationen lernt wenige und ist das netztragende Element.

Dazu kommt Saettigung: `MAX_MHPATH` liegt auf der Fallback-Klasse (40) klar
unter den 85–124 beobachteten H00-Knoten (Zahl aus dem MAX_MHEARD-Kommentar,
`configuration_global.h:180`; nur S3/nRF52 mit 100 reicht an den unteren Rand).
Ein Wert, der bei den interessantesten Knoten am Anschlag steht, kann sie
nicht unterscheiden.

**Was stattdessen zaehlt:** nicht die Anzahl der Eintraege, sondern ihre
**Struktur** — ueber welche Nachbarn die Pfade laufen und ob sich diese Mengen
ueberlappen. Das ist Kap. 8 (Importance v2), und es ist ein Betweenness-Mass,
kein Grad-Mass.

---

## Voraussetzungen und Deployment: Eskalationsstufen (Rev. 3)

Rev. 1/2 kannten drei Stufen (Importance → Suppression → Dedup). Rev. 3 baut
das auf Basis der Produktionsevidenz um: **die Dedup-Erhoehung hat keine
Abhaengigkeit von den anderen beiden und wird vorgezogen** (sie fixt ein
heute messbares Problem und ist zugleich Voraussetzung von Stufe 1, siehe
Risiko "Zombie-Beschleunigung"); vor jeder Verhaltensaenderung steht eine
reine **Beobachtungsstufe**; und jede Stufe bekommt Messprotokoll und
Abbruchkriterium. Die zwingende Reihenfolge "Importance VOR Suppression"
bleibt unveraendert bestehen.

### Warum Suppression OHNE Importance gefaehrlich ist

Die NC-basierte Relay-Suppression (commit 60ea7d8) storniert ein gequeuetes Relay
wenn genuegend Duplikate von anderen Relayern gehoert werden. Ohne Importance-Backoff
haben alle Nodes die gleiche Backoff-Verteilung — der Bergknoten mit NC=20 hoert
**mehr Duplikate als jeder andere**, weil er die meisten Nachbarn hat:

```
Nachricht M wird von 20 Nodes empfangen (ohne Importance):

  Berg-Hub (NC=20):   Backoff 4137ms (zufaellig)
  Tal-Node A (NC=3):  Backoff 4022ms → sendet zuerst
  Tal-Node B (NC=5):  Backoff 4089ms → sendet als zweiter
  Tal-Node C (NC=2):  Backoff 4105ms → sendet als dritter

  Berg-Hub hoert 3 Duplikate → Suppression storniert sein Relay!

  ABER: Auf der anderen Seite des Berges haengt ein Blatt-Node (NC=1),
  der NUR den Berg-Hub hoert. Dieses Blatt bekommt die Nachricht nie.
  Schlimmer: Das "Blatt" koennte eine Bridge mit 10 Nodes dahinter sein.
```

**Das Problem:** Der Bergknoten ist das wahrscheinlichste Opfer der Suppression,
weil er am schnellsten genuegend Duplikate sammelt. Aber er ist gleichzeitig der
Knoten, dessen Relay am wichtigsten ist — abhaengige Nachbarn haben keine Alternative.

Suppression allein ist **topologie-blind**: sie sieht nur "ich habe genug Duplikate
gehoert", aber nicht "wer von meinen Nachbarn hat die Nachricht noch NICHT".

### Stufenplan

| Stufe                       | Inhalt                                                                                                                                                                                                                                 | Verhalten aendert sich? | Weiter zur naechsten Stufe wenn                                                                                                            |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| **0 — Beobachtung**         | `getNetImportance()`, `known_pct`, Soll-Slot und Saettigungs-Flag berechnen und loggen (`[MC-IMP] mode=obs ...` via `printfdeb`), Rueckgabewert von `csma_compute_timeout_prio()` **unveraendert**. Baseline-Messungen M1-M5 aufsetzen | nein                    | >= 2 Wochen Daten ueber gemischte Nachbarschaften, inkl. mind. einer Anomalie (Sturm-Stunde); Baseline dokumentiert                        |
| **0.5 — Dedup-Ring**        | `MAX_DEDUP_RING` erhoehen (Ziel 200-256 fuer S3/nRF52; Fallback-Klasse nach `ram-snapshot`-Pruefung, RAM-Kosten je Slot 4 Byte). Eigenstaendiger, minimaler Upstream-PR — der risikoaermste Gewinn im ganzen Plan                      | ja (risikoarm)          | Zombie-Kandidaten-Rate (M3) sinkt messbar; kein RAM-Regressions-Befund                                                                     |
| **S — Sturm-Schutz (E4)**   | Per-Origin-Relay-Rate-Limit, Rapid-Fire-Jitter, Alters-Drop, `[MC-STORM]`-Logzeile. Unabhaengig von Importance; eigener PR                                                                                                             | ja                      | Sturm-Replay (Beobachtung naechster realer Sturm): Relay-Verstaerkung des Sturm-Origins sinkt, Text/ACK-Latenz in Hoerweite bleibt nutzbar |
| **1a — Beta-Cluster**       | Text-Relay-Importance-Slots (Kap. 4/5) mit `--impslot on` in EINEM koordinierten Cluster (Kandidaten: OE3 oder Toskana/ARI — dicht, firmware-homogen, mcmap-instrumentiert, erreichbare Betreiber); Rest der Flotte unveraendert       | ja (Cluster)            | M4-Ordnungsmetrik zeigt Wirkung, keine Abbruchschwelle gerissen ueber >= 2 Wochen                                                          |
| **1b — Fleet-Release**      | Default `--impslot auto` (Gate 3.1 entscheidet), Release-Notes mit Kill-Switch-Hinweis                                                                                                                                                 | ja                      | M1/M4/M5 ueber 4 Wochen stabil; Front-Anteil <= 7 % bestaetigt                                                                             |
| **1c — Option: POS-Relays** | Ausweitung der Slot-Mechanik auf Prio 4 (Position-Relays) — erst hier wird die Mechanik fuer nennenswertes Volumen wirksam                                                                                                             | ja                      | eigene Beta wie 1a                                                                                                                         |
| **2 — Suppression**         | Neuimplementierung mit den verbindlichen Guardrails (unten). Separater Beta-Cluster-Durchlauf                                                                                                                                          | ja                      | Leaf-Zustellrate (M6) ohne Regression                                                                                                      |

**Erwartungsmanagement (verbindlich):** Stufe 1 ordnet das Text-Relay-Band —
unter 1 % des Relay-Volumens. Sie wird DUP/NEW und Kanallast des Gesamtnetzes
**nicht messbar senken**; das duerfen die Stufen-Gates von ihr auch nicht
verlangen (dafuer sind 0.5, S, 1c und 2 da). Der Nutzen von Stufe 1 ist die
korrekte Reihenfolge fuer die Nachrichten, auf die Menschen warten — und das
Fundament, ohne das Stufe 2 nachweislich Blaetter aushungert.

### Kill-Switch und Tunables

`--impslot on/off/auto` nach dem `--shortpath`-Muster
(`command_functions.cpp:568-588`: Bit in `meshcom_settings.node_sset*`,
`save_settings()`, Anzeige in `--info`):

- `auto` (Default ab 1b): Gate 3.1 entscheidet — heutiges Selbst-Gating.
- `off`: harter Fallback auf alte Berechnung, unabhaengig vom Gate. **Das ist
  der eigentliche Kill-Switch** — das Gate misst Datenverfuegbarkeit, nicht
  Ordnungs-Korrektheit, und kann einen fachlich misslungenen Rollout nicht
  stoppen. Die Stufe-2-Historie (Revert nur per Reflash-Zyklus ueber Wochen)
  darf sich nicht wiederholen.
- `on`: erzwingt Slots auch unterm Gate — nur fuer Beta-Cluster (1a).

`RELAY_IMP_CAP` und `RELAY_IMP_MIN_KNOWN_PCT` als Flash-Settings (5.1), damit
Tuning-Iterationen im Feldtest keinen Reflash brauchen.

### Messprotokolle (M1-M6) — jede Zahl mit Definition, Seite und Fenster

Rev. 2 nannte Zielwerte (DUP/NEW < 0,5, CAD-Busy < 30 %, "keine Zombies")
ohne Messprotokoll; keines davon war falsifizierbar formuliert. Rev. 3:

| #   | Metrik           | Definition (verbindlich)                                                                                                                                                                                                                                                                                                                        | Messseite                                                                                                            | Protokoll                                                                                                                                                                                                                          |
| --- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M1  | DUP/NEW          | `RX_DEDUP_DUP / RX_DEDUP_NEW` — **RF-seitig, node-lokal**. Feed-Quoten (0,70) sind KEINE Vergleichsgroesse (Same-Path-Mehrfachreports vs. Race-Blindspot, Bias-Richtung unbekannt)                                                                                                                                                              | Node (heute nur `bLORADEBUG`-Zeilen; fuer Stufe 0 zwei `uint32_t`-Zaehler + periodische `[MC-STAT]`-Zeile ergaenzen) | gleiche Nodes, gleiches Zeitfenster (>= 16 h), vor/nach je Stufe; BergLog-1,57 nur als historischer Kontext                                                                                                                        |
| M2  | CAD-Busy         | `CAD_BUSY / (CAD_BUSY + CAD_FREE)` — Versuchs-Quote, NICHT `CHANNEL_UTIL` (Airtime-%); beide existieren und divergieren                                                                                                                                                                                                                         | Node                                                                                                                 | Baseline in Stufe 0 auf heutiger Firmware zwingend — die 66 % aus BergLog sind 5 Monate alt und nicht uebertragbar. Achtung Messrueckwirkung: `bLORADEBUG`-printf-Last (vgl. NimBLE-Heap-Befund), Capture ohne aktiven BLE-Central |
| M3  | Zombie-Rate      | msg_id-Wiederauftreten als `RX_DEDUP_NEW` **nach 10-60 min** UND mit niedrigerem Hop-Budget als beim Erstauftritt. Spannen > 1 h sind Zaehler-Wraps (msg_id wrappt bei 999) und zaehlen NICHT; Sturm-Origins pro Origin ausschliessen                                                                                                           | Feed (Volumen) + Node (Stichprobe)                                                                                   | 24-h-Fenster; Baseline heute: 1.368 Kandidaten                                                                                                                                                                                     |
| M4  | Ordnungswirkung  | Direkter Mechanik-Nachweis: synchronisierte Multi-Node-Captures einer Ko-Hoerer-Gruppe (BergLog-Set), Korrelation der TX-Start-Zeitstempel pro gemeinsamer msg_id: sendet der hoeher-importante Relay zuerst? Feed-Proxy (welcher Relay landet im ueberlebenden Pfad) nur als schwaches Zweitsignal — der Feed kollabiert konkurrierende Kopien | Node (mehrere, synchron)                                                                                             | ueber Stunden mitteln (4.7b); Ziel: Ordnung in > 70 % der Faelle mit Importance-Differenz >= 2                                                                                                                                     |
| M5  | ACK-Laufzeit     | p95 der ACK-Roundtrip-Zeit (Stat existiert)                                                                                                                                                                                                                                                                                                     | Node                                                                                                                 | Waechter fuer die Attempt-2-Band-Kompression                                                                                                                                                                                       |
| M6  | Leaf-Zustellrate | Zustellquote an Knoten mit NC_reported <= 2 (die 34-%-Population)                                                                                                                                                                                                                                                                               | Feed (`messages_query`-ACKs) + Node                                                                                  | Pflicht-Gate fuer Stufe 2 — exakt die Metrik, deren Fehlen 60ea7d8 das Feld gekostet hat                                                                                                                                           |

### Abbruchkriterien (gegen die eigene Cluster-Baseline, nicht den Fleet-Schnitt)

| Stufe | Abbruch wenn                                                                                                             | Reaktion                                                                      |
| ----- | ------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------- |
| 0.5   | RAM-Headroom unter Projekt-Marge (`ram-snapshot` VOR Release)                                                            | Board-Klassen-Ziel senken                                                     |
| S     | Fehlklassifikation legitimer Origins (Rate-Limit trifft normale Baken)                                                   | Schwellen anheben, Log-only-Modus                                             |
| 1a/1b | Cluster-DUP/NEW (M1) > +20 % ueber eigener 7-Tage-Baseline, 48 h anhaltend; ODER M5-p95 > +30 %; ODER Front-Anteil > 7 % | `--impslot off` im Cluster ausrufen; Parameter-Iteration ueber Flash-Settings |
| 2     | M6-Drop > 5 % bei irgendeinem Leaf im Beta-Cluster                                                                       | Suppression aus (eigener Schalter), Post-Mortem vor zweitem Versuch           |

Wache: Cluster-Maintainer (Beta) taeglich in den ersten 2 Wochen, danach
woechentlich; ADR-Autor als zweite Instanz fuer Cluster-Vergleiche.

### PR-Aufteilung (Upstream-Realitaet)

| Paket | Inhalt                                                     | PR-faehig                                                                                                                          |
| ----- | ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| A     | Stufe 0.5 (Dedup-Konstanten)                               | sofort — kleinster, unstrittigster PR, nicht mit Stufe 1 buendeln                                                                  |
| B     | Stufe 0 (Telemetrie `[MC-IMP]`, M1/M2-Zaehler)             | sofort — reine Beobachtung                                                                                                         |
| C     | Sturm-Schutz (E4)                                          | nach Stufe-0-Baseline                                                                                                              |
| D     | Stufe 1 (Formel, Gate, Slots, `--impslot`, Flash-Settings) | nach Stufe-0-Daten; ein PR, nur `configuration_global.h` + `mheard_functions.cpp` + `lora_functions.cpp` + `command_functions.cpp` |
| E     | Stufe 2                                                    | erst nach vermessener Stufe 1b                                                                                                     |
| —     | Serverseitiges Sturm-Origin-Limit                          | kein Firmware-PR — an mcmap-/Gateway-Betreiber adressieren                                                                         |

### Warum die Reihenfolge Importance VOR Suppression bleibt

Importance-Backoff sorgt dafuer, dass der Bergknoten (hohe Importance) in den
vorderen Slots landet und den Kanal via CSMA/CAD belegt, **bevor** er
Duplikate hoert. Sein Relay wird gesendet, nicht storniert.

**Allein bringt Importance-Backoff** bereits einen Nutzen: die wichtigsten
Text-Relays passieren zuerst, der Hub versorgt seine abhaengigen Nachbarn
zuverlaessig. Die weniger wichtigen Nodes senden etwas spaeter — die
Gesamtzahl der Relays sinkt noch nicht, aber die **Reihenfolge** ist korrekt.

### Stufe 2: NC-basierte Relay-Suppression (commit 60ea7d8)

**Status:** Implementiert und wieder entfernt. Feldtest zeigte: Leaf Nodes mit nur
einer Gegenstelle haben oft keine Nachrichten erhalten. Bestaetigt die Analyse oben —
Suppression ohne Importance-Backoff storniert die falschen Nodes (Hubs statt Blaetter),
wodurch abhaengige Knoten ausgehungert werden.

**Codestand 2026-08-22 (geprueft):** Im aktuellen Baum existiert **kein**
Suppressionspfad mehr. `is_new_packet()` (`lora_functions.cpp:1427`) filtert
ausschliesslich den RX-Pfad; ein bereits in den TX-Ring eingereihtes Relay wird
bei Duplikat-Empfang nirgends storniert (`txring_functions.cpp` kennt keinen
Cancel-Pfad). Die in `docs/hey-supp.md` beschriebene Trickle-Suppression betrifft
nur **eigene HEY-Beacons**, keine Relays. Stufe 2 ist damit keine Reaktivierung
eines Schalters, sondern eine Neuimplementierung — inklusive der Frage, wie ein
Slot im Ring sicher storniert wird, ohne mit dem laufenden CAD/TX-Zustand oder
einem `RING_STATUS_EXT_PENDING`-Slot zu kollidieren.

**Stufe 1 ist notwendig, aber NICHT hinreichend fuer sichere Suppression
(Korrektur Rev. 3).** Rev. 2 argumentierte: "Der Hub sendet zuerst, die
Blaetter stornieren — korrekte Richtung." Das setzt stillschweigend
"niedrige Importance = redundant" — und das ist falsch. Verifiziertes
Gegenbeispiel:

```
        S ── H (Imp≈7, Slots 0..2, Hub)
        │
        ├── M (Imp≈2)
        │
        └── R (Imp≈1.4, hoert H und M) ── L (Blatt, hoert NUR R;
                                             fuer H und M unsichtbar)

Reihenfolge funktioniert wie entworfen: H zuerst, dann M, dann R.
R hoert H's Relay (Duplikat 1) und M's Relay (Duplikat 2)
  → Cancel bei k=2 → L bekommt die Nachricht NIE.
```

R's Importance ist **korrekt** niedrig (in Summe), enthaelt aber einen
1.0-Beitrag von L — R ist Sole Provider. Genau dieses Muster war der
Feldausfall von 60ea7d8 ("Leaf Nodes mit nur einer Gegenstelle haben oft
keine Nachrichten erhalten"): Stufe 1 aendert, wer zuerst sendet — nicht,
wer von wem abhaengt. Der Zeitanker-Effekt (4.7b) verschaerft das: in einem
busy Kanal driftet R systematisch nach hinten und sammelt erst recht >= k
Duplikate vor dem eigenen Versuch.

**Verbindliche Guardrails fuer jede Stufe-2-Neuimplementierung (E5):**
Ein gequeuetes Relay darf nur storniert werden, wenn ALLE Bedingungen halten:

1. **k >= 2 Duplikate** gehoert (nie 1);
2. **eigene Importance unter Schwelle** (die Slot-Logik lieferte ohnehin einen
   hinteren Slot);
3. **Sole-Provider-Veto mit NC_VETO = 2:** kein aktiver mheard-Nachbar mit
   `NC_reported <= 2` oder unbekannt (== 0). NC=1 allein reicht NICHT: im
   Pair-Cancel-Fall (Blatt hoert genau R1 und R2, meldet NC=2; beide Provider
   hoeren dieselben Hub-Duplikate) wuerden sonst beide Provider deterministisch
   in derselben Runde stornieren. O(MAX_MHEARD)-Scan ueber `mheardNCount[]`,
   gleiche Kosten wie `getMheardCount()`;
4. **randomisierter Cancel** (p < 1, z. B. 0,75): korrelierte Same-Round-Cancels
   der wenigen Provider eines gemeinsamen Abhaengigen duerfen nie
   deterministisch sein;
5. **Log-Zeile pro Suppression** (Rufzeichen, Grund) — in BergLog stand der
   RELAY_SUPPRESS-Zaehler auf 0, weil er nicht existierte; ohne Zaehler kein
   Feld-Gate.

Ehrliche Grenzen (bewusst akzeptiert): (a) bei ~15 % nct=0-Origins und ~19 %
Alt-Firmware feuert das Veto haeufig — Suppression wird in gemischten Regionen
wenig einsparen, bis die Flotte nachzieht (fail-safe in die richtige
Richtung); (b) reine Empfangsstationen tauchen in niemandes mheard auf und
sind durch kein lokales Veto schuetzbar; (c) das Veto ist eine Heuristik —
die exakte Bedingung ("jeder Nachbar hat >= 1 anderen Provider, der empfangen
hat UND nicht storniert") ist lokal nicht entscheidbar.

Ohne Stufe 1 wuerde Suppression zusaetzlich die Hubs stornieren (falsche
Richtung) — die Reihenfolge-Abhaengigkeit bleibt bestehen.

### Stufe 0.5: Dedup-Ring vergroessern (vorgezogen, frueher "Stufe 3")

**Aktuell (Stand 2026-08-22, teilweise erledigt):** `MAX_DEDUP_RING` wurde
boardabhaengig bereits angehoben — 100 (ESP32-S3/nRF52840), 70
(Fallback-Klasse inkl. aller realen T-Beams), 60 (XML-DevBoard), siehe
`configuration_global.h:175ff` (der 10er-T-Beam-Zweig ist toter Code, C-11).
Bei 4,2 msg/min rotiert der 100er-Ring nach ~24 Minuten, der 70er nach
~17 Minuten.

**Problem (heute messbar, unabhaengig von Stufe 1/2):** Multi-Hop-Relays
koennen bis zu 13 Minuten brauchen (BergLog); im 24-h-Produktionsfeed tauchen
1.368 msg_ids nach 10-60 min erneut auf, 104 Pfade enthalten Schleifen. Nach
Dedup-Rotation wird die Nachricht als "neu" akzeptiert → Zombie-Relay.

**Empfehlung:** auf 200-256 erhoehen (~48-61 Minuten Speicher bei 4,2 msg/min),
soweit der RAM des jeweiligen Boards es zulaesst — fuer die Fallback-Klasse
(klassischer ESP32, 64 % der Flotte) vorher per `ram-snapshot` verifizieren,
ggf. board-spezifisch niedrigeres Ziel. Kostet ~0,6-1 KB RAM. **Vorgezogen,
weil:** (a) kein logischer Zusammenhang mit Importance/Suppression, (b) fixt
ein aktives Problem sofort, (c) ist Voraussetzung von Stufe 1 (Front-Slots
beschleunigen sonst Zombie-Verbreitung, siehe Risiko-Kapitel), (d) kleinster,
unstrittigster Upstream-PR.

### Zusammenfassung der Abhaengigkeiten

```
Stufe 0:   Beobachtung + Baselines   → keine Verhaltensaenderung
Stufe 0.5: Dedup-Ring erhoehen       → Zombies eliminiert (eigenstaendig,
           ↓ Voraussetzung fuer         heute schon noetig)
Stufe S:   Sturm-Schutz (E4)         → eigenstaendig, parallel moeglich
Stufe 1:   Importance-Backoff        → Text-Relay-Hubs senden zuerst
           ↓ notwendige (nicht hinreichende) Voraussetzung fuer
Stufe 2:   Relay-Suppression         → nur mit E5-Guardrails
                                       (Sole-Provider-Veto NC_VETO=2,
                                       randomisierter Cancel, Log)
```

**Stufe 2 DARF NICHT ohne Stufe 1 aktiviert werden** — und auch mit Stufe 1
nur mit den E5-Guardrails: korrekte Reihenfolge allein schuetzt Sole-Provider
nachweislich nicht (Gegenbeispiel oben, Feldausfall 60ea7d8).

---

## Offene Punkte (Stand Rev. 3)

### Deployment (blockierend, Reihenfolge im Rollout-Kapitel)

- [ ] **Stufe 0 — Beobachtung:** `[MC-IMP]`-Telemetrie via `printfdeb`, M1/M2-Zaehler, >= 2 Wochen Baseline. PR-Paket B.
- [ ] **Stufe 0.5 — Dedup-Ring auf 200-256** (Fallback-Klasse nach `ram-snapshot`-Pruefung). Vorgezogen; PR-Paket A, sofort machbar.
- [ ] **Stufe S — Sturm-Schutz (E4):** Per-Origin-Rate-Limit, Rapid-Fire-Jitter, Alters-Drop, `[MC-STORM]`. PR-Paket C.
- [ ] **Stufe 1a/1b — Importance-Backoff** (dieses ADR) mit Kill-Switch `--impslot`: Beta-Cluster, dann Fleet. Muss VOR Suppression aktiv sein.
- [ ] **Stufe 2 — Relay-Suppression** erst NACH vermessener Stufe 1 UND nur mit den E5-Guardrails (Sole-Provider-Veto NC_VETO=2, randomisierter Cancel, k>=2, Log). Der Feldausfall von 60ea7d8 war Sole-Provider-Starvation — Reihenfolge allein haette ihn NICHT verhindert.

### Datenqualitaet

- [x] **Zeitfenster vereinheitlicht:** `getNetImportance()` nutzt dasselbe 1-h-Fenster wie `getMheardCount()`.
- [x] **Saettigung behandelt (Rev. 3):** Gate-Bedingung 2 (`getMheardCount() == MAX_MHEARD` → alte Berechnung) plus `sat`-Flag im `[MC-IMP]`-Log.
- [ ] **Qualitaetsgate implementieren** (3.1, drei Bedingungen: known_ratio >= 60 %, keine Saettigung, Uptime >= 1 h).
- [ ] **NC-Frische:** `mheardEpoch[]` misst Aktivitaet, nicht NC-Alter (bis ~170 min bei Trickle-Kadenz). Fuer Stufe 1 akzeptiert (stale positive Werte bleiben konservativ nutzbar); bei Bedarf eigenes Feld messen, nicht annehmen.

### Parameter-Tuning

- [x] **IMP_CAP festgelegt (Rev. 3): Default 4** als Flash-Setting; harte Untergrenze Front-Anteil <= 5-7 %; Falsifikationskriterien: Top-20-Last-Relay-Konkordanz (heute 8/20 bei Cap 8, Ziel >= 14/20) UND Front-Anteil, beide pro Release aus Feed-Daten nachrechenbar.
- [x] **`RELAY_IMP_MIN_KNOWN_PCT` festgelegt (Rev. 3): 60 %** (gemessen: kostet ~1pp Fallback-Knoten gegenueber 50 %, 5-7 von 508). Feld-Check: False-Negative-Rate direkt aus `known_pct`-Telemetrie.
- [ ] RELAY_JITTER_WIDTH: 3 Slots Fensterbreite — gut fuer Entzerrung? Oder 2 fuer schaerfere Trennung? (Messbar erst mit M4.)
- [ ] Float vs. Integer: ESP32 hat Hardware-FPU; falls Integer, dann mit gerundeter Division (systematischer Trunkierungs-Bias, siehe Risiko-Kapitel).
- [ ] `RELAY_TOTAL_SLOTS` fuer das Relay-Band: bei 10 belassen oder auf 16-20 erhoehen? Erreichbare Spreizung 490-630 ms gegen bis zu +1,1 s Ende-zu-Ende-Latenz bei 4 Hops (4.7a). Entscheid braucht M4-Daten; bevorzugter Hebel fuer mehr Front-Aufloesung (statt Cap-Senkung).

### Funktionale Erweiterungen

- [ ] **Position-Relays (Prio 4) in die Slot-Mechanik aufnehmen** — Stufe-1c-Option; erst dort wirkt die Mechanik auf nennenswertes Volumen. HEY-Relays (Prio 5) nur zusammen mit Stufe S.
- [ ] **Importance v2** (Kap. 8): verifizierte Redundanz aus der HEY-Pfadtabelle, Bridge-Erkennung, Gateway-Gewichtung — erst nach Vermessung von Stufe 1 (Gate: M4 zeigt Ordnungswirkung). Die Produktionsdaten zeigen: die noetige Zwei-Hop-Evidenz ist im Feed heute schon vorhanden (36 % der Frames mit >= 1 RF-Hop).
- [ ] **Bidirektionalitaetsnachweis** gegen Link-Asymmetrie: Nachbar nur voll zaehlen, wenn das eigene Rufzeichen in dessen HEY-Pfad/Signal-Report auftaucht.
- [x] **Stale NC nach Standortwechsel (Rev. 3):** nur T-Deck-Nische; vom Uptime-Hold-down mit abgedeckt. Optionales Verwerfen bei Positionssprung bleibt moeglich, ist aber nicht mehr blockierend.
- [x] **Logging (Rev. 3):** `[MC-IMP]` via `printfdeb` → Serial UND Netzkonsole 2323. Web-UI-Anzeige bleibt offen (kein bestehendes mheard-Web-UI gefunden).
- [x] **Critical-Slots erhoehen:** Erledigt — alle Priority-Baender verwenden jetzt einheitlich 10 Slots.
- [x] **Background-Slots reduzieren:** Erledigt — alle Priority-Baender verwenden jetzt einheitlich 10 Slots.

### Feld-Validierung (Protokolle M1-M6 im Rollout-Kapitel)

- [ ] **Zeitanker-Effekt messen** (4.7b): Wie oft starten co-hoerende Nodes ihren Backoff wirklich am selben Ereignis? Bestimmt, wie stark die Ordnungswirkung in der Praxis ueberhaupt sein kann. (Teil von M4.)
- [ ] **M4 — Ordnungswirkung direkt nachweisen:** synchronisierte Multi-Node-Captures; der Feed kann den Mechanismus strukturell nicht sehen (Gateway-Race kollabiert konkurrierende Kopien).
- [ ] **M1/M2-Baseline auf heutiger Firmware** VOR jeder Verhaltensaenderung — die BergLog-Zahlen (Maerz, MAX_MHEARD=20) sind als Baseline unbrauchbar.
- [ ] **BergLog-Wiederholung mit aktiver Suppression + Importance** — erst ein erneuter Test mit beiden Mechanismen zeigt den kombinierten Effekt.
- [ ] **Loop-Monitoring statt Runaway-Angst:** Relay-Konzentration (Top-Dezil-Anteil der Zwischenhop-Auftritte; heute Top-20/397 ≈ 35 %) und Zahl der HEY-only-Knoten (~2 % laut DB-Proxy) als Fruehwarnsignale; Median-nct ist dafuer ungeeignet (bewegt sich nicht, solange Baken laufen). Hub-SPOF/Batterie/serielle Kapazitaet als statische Folgen der Konzentration beobachten.
- [x] **Metriken definiert (Rev. 3):** M1-M6 mit Definition, Messseite, Fenster und Abbruchschwellen im Rollout-Kapitel. Die alten Punktziele (DUP/NEW < 0,5, CAD-Busy < 30 %) gelten NUR fuer die dort definierten RF-seitigen Messgroessen und NICHT fuer Stufe 1 allein (Erwartungsmanagement im Stufenplan).
