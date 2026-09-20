# Finder: ADR-Parameter vs. Produktionsdaten (Fit-Check)

Ziel: `docs/adr-nc-importance-backoff.md` (Rev. 2). Evidenz: `evidence-pack.md`,
`heys_analysis.txt`, `deep_analysis.txt`, plus eigene Python-Queries gegen `heys.jsonl`,
`fleet.json`, `linkload24.json` und die Codebasis (`configuration_global.h`,
`docs/architecture/10-buffer-inventory.md`, `docs/architecture/08-defect-catalogue.md`).
Alle Zahlen unten sind mit eigenem `python3`-Query reproduziert, nicht nur aus den
vorbereiteten Analysen kopiert.

---

## F1 — IMP_CAP=8 ist gegen die gemessene Verteilung zu hoch angesetzt

**ADR:** §4.2/§5.1, `RELAY_IMP_CAP = 8`; Offener Punkt bestätigt: "8.0 ist Schätzung".

**Daten:** Simulierte Importance auf 508 beobachteten Empfängern: p50=0.95, p75=1.77,
p90=3.11, **p95=3.95, p99=6.52, max=9.24**. Nur 4/508 (0.8%) erreichen/übersteigen 8.
Slot-Verteilung (front = slot_start 0..2, Anteil aller Gate-passierenden Knoten):

| CAP | Front (Slot 0..2) | Back (Slot 6..8) | Top-20-Real-Load-Relays in Front |
| --- | ----------------- | ---------------- | -------------------------------- |
| 8   | 0.8%              | 60.4%            | 8/20                             |
| 4   | 7.5%              | 37.6%            | –                                |
| 3   | 14.2%             | 28.9%            | **17/20**                        |
| 2   | 26.4%             | 18.7%            | –                                |

Mit CAP=8 landen 3 der wichtigsten realen Relays nach Traffic-Last (Top-20 Auftreten als
Zwischenhop) in mittleren/hinteren Slots, weil ihre simulierte Importance (2.85–5.64) weit
unter dem Cap bleibt — z.B. `DB0TVI-1` (Last-Rang 2 von 508, imp=3.64) landet bei CAP=8 in
Slot 3..5 statt vorne. Mit CAP=3 rutschen 17/20 der Top-Load-Relays in Slot 0..2; die
Frontpopulation wächst dabei nur auf 14.2% (kein Crowding-Kollaps: bei CAP=2 beginnt die
Front überzubevölkern, 26.4%, was den Sinn "knappe vordere Slots = klare Priorität"
untergräbt).

**Bewertung:** CAP=8 ist um etwa das 2-2.5-fache zu hoch für die _heute_ beobachtete
Netz-Topologie (p99=6.52 ist bereits fast der Cap-Wert; die Masse der Werte liegt weit
darunter). Das führt dazu, dass die Slot-Differenzierung praktisch nur die hinteren 2-3
Slot-Fenster benutzt (60% aller Knoten in Slot 6..8) und echte Workhorses nicht konsequent
nach vorne gezogen werden.

**Schweregrad:** Mittel-Hoch (Kernparameter der Slot-Mechanik, direkt messbar falsch
kalibriert).

**Empfehlung Rev. 3:** `RELAY_IMP_CAP` auf **3–4** senken, nicht auf 8 belassen. CAP=3
liefert die beste gemessene Konkordanz mit realer Relay-Last (17/20) bei moderater
Frontbevölkerung (14.2%). Sensitivitätshinweis: Der optimale Wert hängt vom Nachbarschafts-
Fenster und der Netzdichte ab, die sich mit Firmware-Durchdringung (mehr `NC_reported`
gemeldet) verschieben wird — als Feldtest-Parameter mit p90..p99-Messung pro Rollout-Stufe
festhalten, nicht einmalig hart setzen.

---

## F2 — RELAY_IMP_MIN_KNOWN_PCT=50: Gate ist heute fast nie zu (98.6-99% Pass-Rate)

**ADR:** §3.1, `RELAY_IMP_MIN_KNOWN_PCT = 50`.

**Daten:** `known_ratio`-Verteilung über 508 Knoten: 0%: 7 (1.4%), 50-74%: 32 (6.3%),
75-99%: 110 (21.7%), 100%: 359 (70.7%). Schwellwert-Sensitivität (eigene Neuberechnung):

| Schwelle | Pass-Rate        | Fällt auf alten Voll-Jitter zurück |
| -------- | ---------------- | ---------------------------------- |
| 50%      | 503/508 (99.0%)* | 5 (1.0%)                           |
| 60%      | 496/508 (97.6%)  | 12 (2.4%)                          |
| 70%      | 481/508 (94.7%)  | 27 (5.3%)                          |

(*Leichte Abweichung von den 98.6% im evidence-pack durch minimale Line-Dedup-Varianz;
beide Größenordnungen bestätigen sich.)

**Bewertung:** Das Gate schützt heute fast niemanden mehr vor dem eigentlichen
Kollaps-Szenario (Rev. 2, §3.1: "alle Nachbarn unbekannt"), weil >81% der Flotte bereits

> =4.35n fährt und NC meldet. Eine Erhöhung auf 60% kostet real nur ~1.4 Prozentpunkte
> zusätzliche Fallback-Rate (12 statt 5 von 508), auf 70% ~4.3 Punkte mehr (27 statt 5) — beides
> günstig erkauft für mehr Robustheit gegen die tatsächlich noch bestehenden Restrisiken:
> Kaltstart (frisch gebootete Node), QTH-Wechsel (persistente `mheardNCount[]` vom alten
> Standort, siehe Risiko "Stale NC"), und isolierte Alt-Firmware-Taschen (die verbleibenden
> 17-19% <4.35n, ungleich verteilt — lokal können sie einen Cluster dominieren, auch wenn
> global selten).

**Schweregrad:** Niedrig (Parameter tut, was er soll; Frage ist nur ob der historische
50%-Wert noch die richtige Sicherheitsmarge ist).

**Empfehlung Rev. 3:** Gate bleibt sinnvoll (nicht entfernen — er schützt punktuelle
Alt-Firmware-Cluster und Kaltstart/QTH-Wechsel, nicht die globale Fleet-Quote). Wert auf
**60-70%** anheben ist evidenzbasiert günstig: Kosten sind klein (1-5pp mehr Fallback-Knoten),
Nutzen ist ein strengerer Schutz gerade in den Randfällen, für die das Gate ursprünglich
gedacht war. 50% war beim Entwurf (März) eine Schätzung für eine deutlich jüngere,
gemischtere Flotte — die Prämisse hat sich verschoben, der Mechanismus nicht.

---

## F3 — RELAY_JITTER_WIDTH=3 / RELAY_TOTAL_SLOTS=10: Entropie reicht für die Mehrheit,

aber nicht für den Long Tail — und CAP=8 verschärft das Problem stärker als die Slot-Breite

**ADR:** §4.2/§4.7a, `RELAY_JITTER_WIDTH = 3`, `RELAY_TOTAL_SLOTS = 10`.

**Daten:** Fan-out pro Sender (aus `linkload24.json`, distinct `toCall` je `fromCall`,
n=879 aktive Sender/24h) als Proxy für "wie viele Knoten hören dieselbe Transmission und
konkurrieren potenziell um denselben Relay-Slot":

min=1, p25=1, **median=2, p75=4, p90=7, p95=10, max=32**.
Verteilung: 1 Hörer 40.8%, 2 19.5%, 3 10.8%, 4-5 13.2%, 6-8 8.2%, 9-12 4.3%, 13-20 2.4%,
21+ 0.8%.

Kollisionswahrscheinlichkeit bei k=3 Slots (Geburtstagsproblem, uniform):
n=2 Hörer: 33% Kollisionschance. n=3: 78%. **n>=4: 100% garantiert** (Schubfachprinzip) —
und das betrifft 28.9% der Sender-Nachbarschaften (4-5, 6-8, 9-12, 13-20, 21+ zusammen).

**Bewertung:** Für die Mehrheit der Relay-Entscheidungen (Median-Fan-out=2) ist ein
3-Slot-Fenster grenzwertig, aber nicht aussichtslos (67% Kein-Kollisions-Chance). Für das
obere Viertel (p75=4 aufwärts) garantiert die Geometrie selbst _ohne_ Importance-Gleichstand
schon eine Kollision im selben Fenster. Wichtiger als die reine Fensterbreite ist aber die
**Konzentration durch CAP=8** (F1): 60% aller Knoten teilen sich exakt Slot 6..8, unabhängig
von ihrem tatsächlichen Fan-out — dort treffen sich also nicht nur "echte" Co-Hörer eines
einzelnen Frames, sondern praktisch die gesamte hintere Netzhälfte. Eine Verbreiterung von
`RELAY_TOTAL_SLOTS` auf 16-20 bei gleicher `RELAY_JITTER_WIDTH=3` (ADR-Vorschlag 4.7a) spreizt
zwar die _Abstände zwischen_ Importance-Stufen, ändert aber nichts an der Kollisionsdichte
_innerhalb_ der Slot-6..8-Masse, solange CAP=8 diese Masse dorthin drückt.

**Schweregrad:** Mittel (Slot-Breite selbst ist im gemessenen Median-Fall brauchbar; das
eigentliche Entropieproblem ist strukturell mit F1 gekoppelt, nicht unabhängig).

**Empfehlung Rev. 3:**

1. Erst CAP senken (F1) — das entzerrt die 60%-Masse in Slot 6..8 spürbar (bei CAP=3 nur noch
   28.9% dort) und reduziert damit die Kollisionsdichte direkter als jede Breitenänderung.
2. `RELAY_TOTAL_SLOTS` auf 16-20 bleibt ein sinnvoller _zusätzlicher_ Hebel für den Long-Tail
   (p90=7, p95=10 Fan-out) — als Feldtest-Parameter wie im ADR vorgeschlagen, mit der
   akzeptierten Kosten-Abwägung (+bis zu 1,4s E2E-Latenz bei 4 Hops).
3. Messplan: Fan-out ist ein 24h-aggregiertes Oberschranken-Proxy (nicht "Hörer derselben
   Einzeltransmission"). Für eine belastbare Zahl: Zeitanker-Messung aus 4.7b (wie oft starten
   Co-Hörer den Backoff-Timer wirklich am selben RX-Ende-Ereignis) mit Fan-out kombinieren.

---

## F4 — "Gemischte Flotte" ist als Hauptrisiko historisch, nicht mehr aktuell

**ADR:** §3.1 Risiko "Gemischte Flotte kollabiert die Differenzierung"; Kontext-Absatz.

**Daten:** Firmware: 4.35p 78.2%, >=4.35n (sendet R<NC>) ≈81%. `known_ratio`-Gate-Pass-Rate
98.6-99% (F2). Nur 7/508 Knoten (1.4%) haben `known_ratio=0%` (der eigentliche Kollaps-Fall
aus §3.1: "alle Nachbarn unbekannt").

**Bewertung:** Das im ADR beschriebene Kollaps-Szenario ("beim Rollout melden die meisten
Nachbarn noch keinen NC_reported") beschreibt den Zustand von März 2026, nicht den heutigen.
Mit 81% Firmware-Durchdringung und 98.6%+ Gate-Pass-Rate ist "Mixed Fleet" heute eine
Randbedingung (die verbleibenden 17-19% Alt-Firmware, lokal ungleich verteilt), kein
netzweites Hauptrisiko mehr. Das Gate bleibt trotzdem korrekt konstruiert und nötig — es
adressiert jetzt eher Kaltstart/QTH-Wechsel/lokale Alt-Firmware-Taschen (siehe F2) als eine
flächendeckende Kollaps-Gefahr.

**Schweregrad:** Niedrig (keine Korrektur der Entscheidung nötig, nur der Risiko-Einordnung).

**Empfehlung Rev. 3:** Risiko-Abschnitt umformulieren: "Gemischte Flotte" von
"aktuelles Hauptrisiko" zu "historisch relevant, heute Randfall (98.6-99% Gate-Pass-Rate)"
herabstufen; die verbleibende Restgefahr explizit als "lokale Alt-Firmware-Cluster,
Kaltstart, QTH-Wechsel" benennen (deckt sich mit F2-Empfehlung, Schwelle moderat
anzuheben statt das Gate für obsolet zu erklären).

---

## F5 — BergLog-Zahlen (März 2026): teils noch tragfähig, teils überholt oder nicht

nachmessbar aus dem Feed

**ADR:** Kontext "Felddaten: BergLog 2026-03-13/14".

| BergLog-Zahl (2026-03)               | Heute (2026-08, Feed)                                                                                                                                                               | Einordnung                                                                                                                                                                                                                                                                                                                      |
| ------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| NC=20-Überlauf (MAX_MHEARD war 20)   | MAX_MHEARD heute 80 (S3/nRF52), **30 (ESP32-klassisch/reale T-Beam-Boards, korrigiert — siehe F6)**, 50 (XML)                                                                       | **Überholt.** Sättigungsgrenze liegt heute 1.5-4x höher; die BergLog-Beobachtung "NC=20 ist Pufferüberlauf" trifft auf den aktuellen Codestand nicht mehr direkt zu.                                                                                                                                                            |
| DUP/NEW = 1,57 (RF-seitig, 5 Knoten) | DUP/NEW = 0,70 (Feed-seitig, netzweit)                                                                                                                                              | **Nicht direkt vergleichbar**, wie das Evidence-Pack korrekt anmerkt: Feed ist eine Unterschranke (max. 1 Report/Frame netzweit durch GW-Internet-Race, Trickle unterdrückt HEYs). Kein Widerspruch, aber auch keine Bestätigung — echte RF-seitige DUP/NEW-Messung fehlt für heute.                                            |
| 66% CAD-Busy-Rate                    | Keine aktuelle Messung möglich (Feed ist kein Airtime-/Occupancy-Signal, siehe Blind-Spots-Hinweis)                                                                                 | **Nicht nachmessbar aus diesen Daten.** Der ADR stützt Kap. 4.5/4.7a explizit auf diese 66%-Zahl ("Felddaten bestätigen dies") — das ist weiterhin eine 5-Monate alte Einzelmessung auf 5 Knoten, nicht erneut verifiziert.                                                                                                     |
| 21% H00 (Hop-Erschöpfung)            | Pfadlängen-Verteilung 2026-08: Pfad 1: 63.8%, 2: 19.8%, 3: 9.8%, 4: 4.5%, 5: 2.1%                                                                                                   | **Nicht direkt vergleichbar.** Feed-Pfadlänge misst die beim Gateway sichtbare Empfangskette, nicht ob ein Frame seine Hop-Budget-Grenze erreicht hat (H00). Path-5-Anteil (2.1%) ist strukturell niedriger als BergLog's H00 21%, aber beide Zahlen messen nicht dasselbe — kein Beleg für Verbesserung oder Verschlechterung. |
| Hub-Knoten relayen 80% des Traffics  | Nicht direkt reproduziert; TOP-25-Relay-Liste zeigt starke Konzentration (Top-1 `DB0HOB-12` 1164 von insgesamt 38.767 3-Wert-Segmenten = 3%, Top-25 zusammen deutlich konzentriert) | **Grundsätzlich noch plausibel** (Verteilung ist weiterhin stark rechtsschief), aber die exakte 80%-Zahl ist nicht neu vermessen.                                                                                                                                                                                               |

**Bewertung:** Der grundsätzliche _qualitative_ Befund von BergLog (Hub-Dominanz, Relay-
Kaskaden, Kanal-Belastung) bleibt plausibel, aber die _quantitativen_ Zahlen, die das ADR als
Begründung für konkrete Parameter zitiert (66% CAD-Busy für 4.7a; NC=20-Überlauf für die
Sättigungs-Diskussion), stammen aus einer 5-Monate alten 5-Knoten-Stichprobe auf einer
inzwischen überholten Firmware-Version (MAX_MHEARD=20 damals vs. 30-80 heute).

**Schweregrad:** Mittel (betrifft die Begründungskette für 4.7a und die
Sättigungs-Diskussion, nicht die Kern-Entscheidung).

**Empfehlung Rev. 3:** BergLog-Zahlen im Kontext-Kapitel explizit als "Stand März 2026,
nicht erneut vermessen" kennzeichnen (nicht nur implizit über das Datum). Vor der
Parametrisierung von 4.7a (Slot-Breite) einen frischen CAD-Busy-Rate-Messlauf einplanen —
die 66%-Zahl trägt aktuell die gesamte Begründung für "der Sicherheitsabstand ist dünn"
und ist nicht durch aktuelle Daten gestützt oder widerlegt.

---

## F6 — T-Beam MAX_MHEARD=10: Faktenfehler im ADR — die reale Grenze ist 30, nicht 10

**ADR:** Kontext "Datenqualität des NC" Tabelle, Zeile "Sättigung": _"MAX_MHEARD = 80
(ESP32-S3/nRF52840), 50 (XML/SBUFFER), 30 (ESP32 klassisch), **10 (T-Beam)**... Fundstelle:
`configuration_global.h:172ff`"_, sowie Risiko-Abschnitt "NC-Sättigung durch MAX_MHEARD":
_"Auf Boards mit kleinem MAX_MHEARD (T-Beam: 10, ESP32 klassisch: 30)..."_

**Daten/Code-Befund:** Der `#elif defined(ENABLE_TBEAM)`-Zweig mit `MAX_MHEARD=10`
(`configuration_global.h:186-192`, Kommentar _"very smal version only for developer
tests"_) ist laut `docs/architecture/10-buffer-inventory.md` §1.2 und
`docs/architecture/08-defect-catalogue.md` (Defekt **C-11**) **toter Code**: `ENABLE_TBEAM`
wird in **keiner** `platformio.ini` je gesetzt (eigener `grep -rn ENABLE_TBEAM
variants/*.ini` bestätigt: 0 Treffer). Die realen T-Beam-Boards
(`variants/ttgo_tbeam`, `ttgo_tbeam_SX1262`, `ttgo_tbeam_SX1268`) setzen `-D
BOARD_TBEAM="tbeam"` und landen dadurch im `#else`-Fallback-Zweig ("ESP32 original") mit
**`MAX_MHEARD=30`** — derselben Klasse wie TLORA/EBYTE-E22-Classic/Heltec V2.1.

Eigene Neuberechnung mit korrektem Board→Cap-Mapping (357 Relays mit Hardware-Match in
`fleet.json`, 3-Wert-`mheard`-Segmente aus `heys.jsonl`):

| Cap-Klasse                                                   | Population (Relays) | Sättigend (mheard>=cap-1) | Genau am Cap |
| ------------------------------------------------------------ | ------------------- | ------------------------- | ------------ |
| 30 (TLORA, TBEAM V1.1/V1.2, E22-Classic, Heltec V2.1)        | 248                 | 4 (1.6%)                  | 4            |
| 80 (S3/nRF52: Heltec V3, T-Deck, RAK4631, T-Beam Supreme...) | 109                 | 0                         | 0            |

**Gesamt: 4/357 (1.1%)** aktive Relays sind an ihrer echten Board-Grenze gesättigt — nicht
die ~96/397 (24%), die eine Cap=10-Annahme suggerieren würde (`deep_analysis.txt` Zeile 3
prüft `mheard>=9`, ohne Hardware-Zuordnung — das ist die 96-Zahl, die bei korrekter
Zuordnung irrelevant ist, da kein reales Board mit Cap=10 existiert).

Fleet-weit: 66.8% der 1430 Knoten liegen in der Cap=30-Klasse (deutlich mehr als "nur
T-Beam" — dominiert von TLORA V2.1.6, 32% der Flotte), 33.2% in Cap=80. Keine Population
in einer Cap=10-Klasse.

**Bewertung:** Dies ist kein Interpretationsstreit, sondern ein durch die eigene
Projekt-Dokumentation (`08-defect-catalogue.md` C-11) bereits belegter Faktenfehler, der
unverändert von Rev. 1 in Rev. 2 übernommen wurde. Er verzerrt die Risikoeinschätzung in
zwei Richtungen gleichzeitig: (a) er suggeriert eine dramatische Sättigung bei "T-Beam"
(die es so nicht gibt — echte Sättigung ist mit 1.1% selten), und (b) er lenkt den Fokus
weg von der tatsächlich größten Cap=30-Population (67% der Flotte, angeführt von TLORA,
nicht T-Beam), bei der Sättigung zwar ebenfalls selten ist (1.6%), aber wegen der schieren
Größe der Gruppe in absoluten Zahlen relevanter wäre als eine T-Beam-spezifische Lesart nahelegt.

**Schweregrad:** Hoch (Faktenfehler, der eine ADR-Risikoaussage direkt falsch begründet;
niedrige praktische Auswirkung auf die Kern-Entscheidung, da die Sättigung ohnehin selten
ist — aber die _Begründung_ im Dokument ist falsch und sollte vor Rev. 3 korrigiert werden,
zumal das Projekt selbst diesen Fehler bereits katalogisiert hat).

**Empfehlung Rev. 3:**

1. Tabelle "Datenqualität des NC" korrigieren: `MAX_MHEARD` = 80 (S3/nRF52), 50 (XML,
   1 Variante), 30 (Fallback-Klasse: TLORA, TBEAM V1.1/V1.2, E22-Classic, Heltec V2.1,
   t_echo, heltec_t114 — 11 Boardvarianten, ~67% der Flotte). Der `ENABLE_TBEAM=10`-Zweig
   ist toter Code und gehört nicht in die Tabelle (oder nur mit explizitem
   "nicht kompiliert, siehe C-11"-Vermerk).
2. Risiko-Abschnitt "NC-Sättigung durch MAX_MHEARD" entsprechend entschärfen: gemessene
   Sättigungsrate 1.1% (4/357), nicht die im Text suggerierte breite T-Beam-Betroffenheit.
3. Falls künftig doch ein echtes "sehr kleines Board" mit Cap~10 gebraucht wird (die
   Kommentar-Intention "developer tests"), das als expliziten offenen Punkt behandeln,
   nicht als bestehendes Feldrisiko.

---

## F7 — IU5CZN-10: bestätigt die ADR-Designabsicht, ist kein Gegenbeispiel

**ADR:** §2.6 "Entscheidender Vergleich: Berg-Hub vs. Stadt-Node".

**Daten:** `IU5CZN-10` — Last-Rang 9/508 (489 Zwischenhop-Auftreten), simulierte Importance
0.66 (Rang 304/508, Slot 6..8 bei CAP=8). Eigene Nachbarschaftsanalyse aus
`linkload24.json`: 13 Quellen, von denen `IU5CZN-10` hört. Für jede dieser Quellen wurde
geprüft, von wie vielen _anderen_ Knoten sie ebenfalls gehört wird:

- `IZ5IOT-10`: auch gehört von 7 anderen (u.a. `IQ5ARI-11`, `IQ5ARI-13`, `IZ5TRQ-11`)
- `IZ5TRQ-11`: auch gehört von 7 anderen
- `IU5ATN-12`: auch gehört von **31** anderen
- `IQ5ARI-13`: auch gehört von 15 anderen
- `IU5VKF-10`: auch gehört von 10 anderen
- `IU5PSY-13`: 0 andere (einziger schwacher Punkt, aber `traversals=0` — kein aktiver Verkehr)

Alle Quellen außer einer haben 7-31 alternative Hörer im selben Cluster (italienisches
IU5/IQ5/IZ5-Ballungsgebiet). `IU5CZN-10` selbst hat zudem keinen eigenen gemeldeten NC in
diesem Fenster (kein originierter HEY beobachtet) — sein hoher Load-Rang stammt aus reiner
Verkehrsdichte in einem dichten, redundanten Stadtcluster, nicht aus Abhängigkeit von
Nachbarn.

**Bewertung:** Das ist exakt das ADR-Szenario "Stadt-Node" (§2.6): hohe Roh-Last durch
Verkehrsdichte, aber niedrige _Netzwichtigkeit_, weil die Nachbarn zahlreiche Alternativen
haben. Die Formel arbeitet wie vorgesehen — sie bestraft nicht "dichte Cluster-Workhorses"
grundsätzlich, sondern erkennt korrekt, dass diese spezifische Knoten-Last redundant ist
und im Ausfall keine Abhängigen zurücklässt. Kein Beleg für einen Formelfehler.

**Schweregrad:** Niedrig / informativ (bestätigt Design, keine Änderung nötig).

**Empfehlung Rev. 3:** `IU5CZN-10` als reales Fallbeispiel für §2.6 aufnehmen (echte Daten
statt nur synthetisches Beispiel) — stärkt die Dokumentation, ändert aber keine
Entscheidung. Einzige Restfrage: sein fehlender eigener `NC_reported`-Wert (kein HEY im
Fenster beobachtet) sollte im Qualitätsgate (3.1) als "unbekannt, konservativ 1.0"
behandelt werden — das ist bereits die spezifizierte Fallback-Regel, hier nur exemplarisch
bestätigt.

---

## Zusammenfassung nach Schweregrad

| #   | Finding                                                         | Schweregrad        | Kernzahl                                                    |
| --- | --------------------------------------------------------------- | ------------------ | ----------------------------------------------------------- |
| F6  | T-Beam-Cap=10 ist toter Code, real 30                           | **Hoch**           | 1.1% echte Sättigung statt suggerierter ~24%                |
| F1  | IMP_CAP=8 zu hoch kalibriert                                    | Mittel-Hoch        | p99=6.52, CAP=3 gibt 17/20 statt 8/20 Konkordanz            |
| F3  | Jitter-Width/CAP-Kopplung, Long-Tail-Kollisionen                | Mittel             | 28.9% Sender mit Fan-out>=4 (garantierte Kollision bei k=3) |
| F5  | BergLog-Zahlen teils überholt/nicht nachmessbar                 | Mittel             | 66%-CAD-Busy nicht erneut vermessen                         |
| F2  | Gate-Schwelle 50% zu niedrig für heutige Flotte                 | Niedrig            | 98.6-99% Pass-Rate, 60%→nur +1.4pp Kosten                   |
| F4  | "Gemischte Flotte" ist historisches, kein aktuelles Hauptrisiko | Niedrig            | 81% Firmware-Durchdringung                                  |
| F7  | IU5CZN-10 bestätigt Design, kein Gegenbeispiel                  | Niedrig/informativ | 7-31 Alternativhörer pro Nachbarquelle                      |
