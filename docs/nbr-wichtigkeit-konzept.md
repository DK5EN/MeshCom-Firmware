# Nachbarschaftsmatrix, Stufe 2: Wichtigkeit, Rang und prioritaetsgesteuertes Meshen

**Status:** Konzept, nichts davon ist Code. Aufgenommen 2026-09-22 aus der Betreiber-Sitzung zum
Produktions-Screenshot von DK5EN-98, **ueberarbeitet am selben Tag gegen den laufenden Feldlauf**
(11,9 h Mitschnitt, Abschnitt 2). Zwei Aussagen der Erstfassung sind damit widerlegt, siehe
Abschnitt 2.4.

**Vorgaenger:** `docs/nachbarschaftsmatrix-campaign.md` (Stufe 1, was gebaut ist),
`docs/nachbarschaftsmatrix-verdict.md` (Advisor-Befunde), `docs/nbr-logformat.md` (Logformat),
`docs/adr-nc-importance-backoff.md` (ADR 02: dieselbe Zielrichtung, aber aus dem HEY-`NC` statt aus
der Matrix hergeleitet), `docs/Relay-Prioritaeten-Verdict.html` (Overhear-Cancel, MPR-Kostenrechnung),
`~/Desktop/1941-nbr-feldlauf-auswertung.md` (Rezept des laufenden Feldlaufs).

---

## 1. BLUF

Stufe 1 beantwortet eine binaere Frage: **muss ich meshen, weil jemand exklusiv von mir abhaengt?**
Das reicht als Indikation, verschenkt aber die eigentliche Information. Stufe 2 macht daraus eine
**dreiwertige Sendeentscheidung** und eine **Rangordnung unter den direkten Nachbarn**:

1. **Muss meshen** (exklusive Abhaengigkeit) → Relay mit hoher Prioritaet.
2. **Muss nicht meshen** (alles redundant gedeckt) → Relay mit sehr niedriger Prioritaet, und
   **Abbruch aus dem Sendepuffer**, sobald waehrend der Wartezeit ein anderer Knoten dasselbe Frame
   gemesht hat. Meine Aussendung waere dann zu 100 % redundant.
3. **Rang unter den Nachbarn** entscheidet, wer von mehreren "muss meshen"-Knoten zuerst darf: der
   netzbildendste Knoten wird Super-Node der Umgebung und mesht mit hoher Prioritaet, die kleineren
   warten und brechen ab, wenn er es schon getan hat.

**Die entscheidende Erkenntnis aus dem Feldlauf:** Der Rang ist **lokal berechenbar, eine Election
ist nicht noetig** — die Information liegt bereits in der Matrixlogik. Was fehlt, ist nicht Wissen,
sondern **Platz**: die Matrix war in 43 von 46 Schnappschuessen randvoll (21 von 21 Zeilen) und hat
in 11,9 h 56 Zeilen verdraengt. Der Engpass ist die quadratische Zellenmatrix, nicht die
Beobachtbarkeit. Abschnitt 5 schlaegt dafuer eine lineare Ablage vor.

---

## 2. Feldevidenz: der laufende Mitschnitt DK5EN-98

Ausgewertet am 2026-09-22 07:17 aus `martin@rpizero.local:~/meshlog/dk5en-98/` mit
`tools/nbrlog.py --since 2026-09-21`, Zwischenstand abgelegt als
`~/Desktop/nbr-feldlauf-20260922-zwischenstand.md`. Der Lauf ist zu diesem Zeitpunkt **11,9 von 24 h
gelaufen und lief weiter** — alle Zahlen hier sind ein Zwischenstand, kein Endergebnis. Die
Endauswertung steht nach 19:20 an.

| Groesse                                         | Wert                                                |
| ----------------------------------------------- | --------------------------------------------------- |
| Zeitraum                                        | 21.09. 19:20 bis 22.09. 07:17, 0 Reboots            |
| `[NBR]`-Zeilen                                  | 11.212                                              |
| Direkte Nachbarn (ME-Zeilen)                    | 6                                                   |
| Knoten in 2 Hop, die ich **nicht** direkt hoere | 39                                                  |
| Schnappschuesse                                 | 46, davon **43 mit `rows == maxrows == 21`**        |
| EVICT-Ereignisse                                | 56 (rund 5/h), 33 verschiedene Rufzeichen betroffen |
| Frames durch den 2-Hop-Schnitt gekuerzt         | 1.875 (2.733 verworfene Pfad-Token, `ntok` bis 6)   |

### 2.1 Der Rang ist eindeutig und stabil

Aus dem Mitschnitt, je direktem Nachbarn: wie viele Knoten hoert er, und wie viele davon hoert
**sonst niemand** in meiner Reichweite (das ist genau die Rechnung, die `nbrRowMeshNeed()` heute
schon anstellt — sie wirft das Ergebnis nur als `MESH`/`RED` weg, statt es zu zaehlen):

| Nachbar  | hoert n Knoten | davon exklusiv | Urteil | Reichweite (Screenshot) |
| -------- | -------------- | -------------- | ------ | ----------------------- |
| DL2JA-2  | 41             | **29**         | MESH   | 82,6 km @ DB0HOB-12     |
| DB0ED-99 | 10             | **1**          | MESH   | 61,9 km @ DB0HOB-12     |
| DK5EN-1  | 5              | 1 (unsicher)   | RED\*  | —                       |
| DL2UD-1  | 3              | 0              | RED    | 2,4 km @ DL2JA-2        |
| DL2JA-1  | 0              | 0              | RED    | 82,5 km @ DB0HOB-12     |
| DK5EN-92 | 0              | 0              | RED    | —                       |

\* Einzige Abweichung zwischen der Nachrechnung aus dem Log und dem Firmware-Urteil. Ursache:
DK5EN-1s exklusives Blatt `IS0QLX-11` wurde im Beobachtungszeitraum aus der Tabelle verdraengt.
**Das ist kein Logikfehler, sondern Tabellendruck** — und damit selbst ein Beleg fuer Abschnitt 2.3.

Die Rangfolge ist unmissverstaendlich: **DL2JA-2 ist der Super-Node dieser Umgebung**, DB0ED-99 ein
schwacher Zweiter, der Rest traegt nichts Exklusives bei. Und sie ist **stabil**: ueber 46
Schnappschuesse aendert kein Nachbar sein Urteil (DL2UD-1 einmal ab Schnappschuss 3). Eine
Rangberechnung auf dieser Basis wuerde also nicht flattern.

### 2.2 Warum die reine Nachbarzahl nichts taugt — gemessen

Der Betreiber-Einwand ist im Datensatz direkt belegt: **wer 20 Nachbarn hat, aber lauter Nachbarn
mit denselben 20 Nachbarn, hebt sich heraus.** DB0ED-99 hoert 10 Knoten — beeindruckend, bis man
nachrechnet: 9 davon hoert DL2JA-2 auch. Sein exklusiver Beitrag ist **ein** Knoten (DB0FHR-12).

| Nachbar  | rohe Nachbarzahl | exklusiver Beitrag | Anteil   |
| -------- | ---------------- | ------------------ | -------- |
| DL2JA-2  | 41               | 29                 | **71 %** |
| DB0ED-99 | 10               | 1                  | **10 %** |

Die rohe Zahl ueberschaetzt DB0ED-99 um den Faktor 10. **Der Mehrwert steckt ausschliesslich in der
exklusiven Versorgung, nicht in der Nachbarzahl.** Damit ist `NCT` aus dem HEY-Bericht (die
selbstgemeldete `getMheardCount()` jedes Hops) als Steuergroesse **entwertet** — es bleibt eine
Anzeigegroesse, kein Eingang in die Wichtigkeit. Die Erstfassung dieses Dokuments hat `NCT` zu hoch
gehaengt; das ist hiermit korrigiert.

### 2.3 Der echte Engpass: die Tabelle ist zu klein, nicht zu blind

Das Universum aus dem Mitschnitt: 1 eigener Knoten + 6 direkte Nachbarn + 39 Knoten in 2 Hop = **46
Identitaeten**. `NBR_MAX_ROWS` ist 21. Der Knoten sieht also alles, kann aber nie mehr als knapp die
Haelfte gleichzeitig halten — daher 43 volle Schnappschuesse, 56 Verdraengungen, und daher das eine
falsche Urteil aus 2.1.

**Die Zahl 41 fuer DL2JA-2 ist eine Vereinigung ueber 11,9 h aus dem Logstrom, kein Zustand des
Geraets.** Die Auswertung (`heard_sets()` in `tools/nbrlog.py`, aus den `EDGE`-Zeilen) kann das, das
Geraet nicht. Genau diese Luecke — nicht fehlende Beobachtung — ist der Grund, warum der lokale
Rang heute unvollstaendig waere.

Aufstocken hilft nicht, weil die Zellen quadratisch wachsen:

| `NBR_MAX_ROWS` | Zeilen (24 B) | Zellen (6 B) | gesamt      |
| -------------- | ------------- | ------------ | ----------- |
| 21 (heute, S3) | 504 B         | 2.646 B      | **3,1 kB**  |
| 32             | 768 B         | 6.144 B      | 6,9 kB      |
| 46 (Universum) | 1.104 B       | 12.696 B     | **13,8 kB** |

13,8 kB fuer einen einzigen Nachbarschaftsblick sind auf klassischem ESP32 nicht zu rechtfertigen
(vgl. `docs/`-Kampagne RAM-Reclaim). Abschnitt 5.3 schlaegt darum eine **lineare** Ablage vor.

### 2.4 Was die Erstfassung dieses Dokuments falsch hatte

| Aussage (Erstfassung)                                                       | Befund                                                                                                                                |
| --------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| "Weg 1 unterschaetzt systematisch die wichtigsten Knoten, deshalb Election" | **Zu stark.** Die 2-Hop-Kanten kommen ueber relayte Pfade an; der exklusive Beitrag ist lokal rechenbar. Es fehlt Platz, nicht Sicht. |
| "`NCT` ist der billige Gewinn fuer die Wichtigkeit"                         | **Entwertet.** Rohe Nachbarzahl ueberschaetzt hier um Faktor 10 (2.2). Bleibt reine Anzeige.                                          |

---

## 3. Ausgangslage im Code

| Baustein                      | Ort                                                                | Aussage                                                                                                     |
| ----------------------------- | ------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| Kreuzmatrix "Y hat X gehoert" | `src/nbr_matrix.{h,cpp}`, `cells[X][Y]`                            | 2-Hop-Fenster, 12 h Verfall, N = 21/13/11 je Board                                                          |
| Urteil `nbrExclusive()`       | `src/nbr_matrix.cpp`                                               | Zeilen, die im Fenster **nur ich** hoere                                                                    |
| Urteil `nbrRowMeshNeed()`     | `src/nbr_matrix.cpp`                                               | Muss **der Nachbar** selbst meshen? (NA/MESH/RED) — **zaehlt intern schon, gibt aber nur ein Wort zurueck** |
| Reichweite `nbrReach()`       | `src/nbr_matrix.cpp`                                               | groesste Distanz einer Zeile zu einem Partner, mit Partnername                                              |
| Flags `GW` / `MESH` / `POS`   | `NbrRow.flags`                                                     | GW aus HEY-Ziel `HG`, MESH aus dem `msg_mesh`-Bit eines POS-Frames                                          |
| Web-Seite (die zwei Tabellen) | `src/web_functions/web_functions.cpp:1574` `sub_page_neighbours()` | Kreuztabelle + Zeilentabelle, Quelle des Screenshots                                                        |
| Overhear-Cancel im TX-Ring    | `src/lora_functions.cpp:654-698`                                   | **existiert**, scannt nach `msg_id` und gibt den Slot frei                                                  |
| CSMA-Prioritaeten 1..5        | `src/txring_functions.cpp:46-73`, `src/configuration_global.h`     | ACK/DM 3000, Gruppe 3000, Text-Relay 4500, POS 5500, HEY 5500 ms                                            |

Ein Punkt daraus darf nicht neu "entdeckt" werden: **der Overhear-Cancel muss nicht gebaut werden.**
Er ist da, nur **strukturell fuer Relays gesperrt** — die Schleife ueberspringt Eintraege mit
`RING_STATUS_DONE` und `RING_STATUS_READY`, und genau `RING_STATUS_DONE` bekommt ein Relay beim
Einreihen (`addTxRingEntry()` mit `"rx_relay"`). Die Aufgabe in Abschnitt 5 heisst also nicht
"Cancel bauen", sondern "den bestehenden Scan matrixgesteuert auch Relay-Eintraege sehen lassen".

---

## 4. Anzeige: was die zwei Tabellen zusaetzlich zeigen muessen

**Alle Beschriftungen und Erklaerungen auf Englisch**, wie der Rest der Web-Oberflaeche.

### 4.1 Kreuztabelle (Tabelle 1)

**Heute:** Kopfzeile sind nackte Nummern 1..21, die linke Spalte ist `<Nr> <Call>`.

**Gefordert:**

1. **Spaltenkopf zusaetzlich mit Rufzeichen inkl. SSID, vertikal gesetzt**, damit die Tabelle nicht
   in die Breite laeuft (`writing-mode: vertical-rl; transform: rotate(180deg)`, Nummer bleibt
   darueber stehen). Der CSS-Fallstrick dieser Seite bleibt zu beachten: das Scaffold-CSS greift nur
   auf `#content_inner > table`, ein Wrapper-`div` wuerde die Matrix aus dem Selektor werfen (Bench
   2026-09-20). Die Tabelle scrollt deshalb selbst als Block.
2. **Zusaetzliche Spalten links**, je Zeile — **jeder Einzelbuchstabe braucht einen Hover mit
   Klartext**, sonst ist die Spalte unlesbar:

| Spalte | Kopf-Hover (`title` auf `<th>`)                                       | Zell-Hover (`title` auf `<td>`)                                  | Quelle                                                     |
| ------ | --------------------------------------------------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------- |
| `D/I`  | "Direct: heard by me over the air. Indirect: only via a neighbour."   | "Heard directly, RSSI -114 dBm" / "Indirect, via DL2JA-2"        | `cells[X][0]` frisch und gesetzt → `D`, sonst `I`          |
| `G`    | "Gateway: node uploads to the network via internet."                  | "Gateway seen 3x in window" / "No gateway frame observed"        | `NBR_FLAG_GW`                                              |
| `M`    | "Mesh: node relays foreign frames (from its position frame)."         | "Mesh enabled" / "Mesh disabled" / "No position frame in window" | `NBR_FLAG_MESH`                                            |
| `#N`   | "Neighbours: how many nodes this node hears."                         | "Hears 41 nodes (as far as this table can hold them)"            | Spaltenzaehlung ueber `cells[*][X]`                        |
| `#X`   | "Exclusive: neighbours that ONLY this node hears. This is the value." | "29 of 41 heard only by this node"                               | Innenleben von `nbrRowMeshNeed()`, als Zahl herausgefuehrt |

**Wichtig fuer den Hover:** `title=` funktioniert auf dem Telefon nicht (kein Hover). Die Seite
braucht deshalb **zusaetzlich eine Legendenzeile unter der Tabelle**, die dieselben fuenf
Erklaerungen als Text fuehrt. Das `title`-Attribut ist die Bequemlichkeit am Desktop, die Legende
die Barrierefreiheit. Die Matrixzellen nutzen `title` bereits (`T:%u P:%u H:%u rssi:%d`), das Muster
ist also gesetzt.

**Einschraenkungen, die in den Hover-Text gehoeren:**

- `G = no` heisst "not observed", nicht "no gateway": das Flag wird nur gesetzt, wenn ein HEY mit
  Ziel `HG` ankam **und** der Absender eine aufgeloeste Zeile hat.
- `M` kommt ausschliesslich aus dem `msg_mesh`-Bit eines POS-Frames. Ohne frisches POS keine
  Aussage.
- `#N` und `#X` sind **durch die Tabellengroesse gekappt** (Abschnitt 2.3). Der Hover muss das
  sagen, sonst liest man eine 10 als Tatsache, wo eine 41 die Wahrheit ist.

### 4.2 Zeilentabelle (Tabelle 2)

**Heute:** die Spalte `Hearers` waechst unbegrenzt nach rechts (im Screenshot bei DL2JA-1 sechs
Rufzeichen in einer Zeile). **Gefordert:** Umbruch innerhalb der Zelle — feste Maximalbreite plus
`overflow-wrap`, oder Umbruch nach n Eintraegen. Die Tabelle waechst dann in der Hoehe statt in der
Breite.

Gleich mitzunehmen: `Hearers` nach Wichtigkeit statt nach Zeilenindex sortieren, sobald Abschnitt 5
eine Rangzahl liefert.

---

## 5. Die Sendeentscheidung: von binaer zu dreiwertig

### 5.1 Der Gedankengang

- **Fall A — ich versorge jemanden exklusiv.** Mein Relay ist die einzige Chance dieses Knotens.
  → hohe Prioritaet, kein Abbruch.
- **Fall B — alles, was ich hoere, hoert auch jemand anders.** Mein Relay ist reine Verdopplung.
  → sehr niedrige Prioritaet (langer Backoff), und waehrend ich auf den Sendeslot warte, hoere ich
  mit: wird dasselbe Frame zwischenzeitlich von einem anderen gemesht, **loesche ich meinen Eintrag
  aus dem Sendepuffer**.

Fall B ist der Hebel. Er kostet nichts an Zustellwahrscheinlichkeit (Abbruch nur nach bewiesener
Weiterleitung) und nimmt Last vom Kanal. **Im Messdatensatz betrifft er 4 von 6 Nachbarn** — DL2UD-1,
DL2JA-1, DK5EN-92 und (mit Vorbehalt) DK5EN-1 tragen nichts Exklusives bei.

### 5.2 Die Gateway-Ausnahme

**Ein exklusiv von mir versorgter Knoten, der `GW = on` hat, muss von mir nicht versorgt werden** —
er bekommt ueber seine Gateway-Anbindung die vollstaendigere Information als ueber meinen einen
HF-Hop. `nbrExclusive()` bleibt unveraendert (es beantwortet die Beobachtungsfrage); die
**Sendeentscheidung** filtert die Ergebnismenge zusaetzlich um alle Zeilen mit `NBR_FLAG_GW`.

Vorsicht: `GW` ist beobachtet, nicht verbuergt (4.1). Ein Fehlurteil faellt in die unsichere Richtung
— ich mesche nicht fuer jemanden, der doch kein Gateway ist. Das spricht dafuer, das Flag fuer
**diesen** Zweck nur zu glauben, wenn es mehrfach im Fenster gesehen wurde. Offener Punkt.

### 5.3 Rang ohne Election: zaehlen statt speichern

Nach Abschnitt 2 ist die Election **nicht noetig**, solange wir den exklusiven Beitrag eines
Nachbarn zaehlen koennen. Das Hindernis ist die quadratische Zellenmatrix. Vorschlag:

**Asymmetrische Ablage.** Volle Zeilen nur noch fuer das, was benannt werden muss. Fuer die Frage
"wie viele exklusive Blaetter hat mein Nachbar" genuegt eine **Mengen-Naeherung pro direktem
Nachbarn**: ein Bitfeld fester Groesse, in das jedes gehoerte Rufzeichen per Hash ein Bit setzt.

- Zaehlung: `popcount(fingerprint)` schaetzt `#N`.
- Exklusivbeitrag: `popcount(fp[X] & ~(fp[andere] | fp[ich]))` schaetzt `#X` — genau die Rechnung
  aus 2.1, ohne eine einzige Zeile dafuer zu belegen.
- Kosten bei 256 Bit und den 6 gemessenen direkten Nachbarn: **6 × 32 B = 192 B**, linear statt
  quadratisch. Gegenrechnung: eine Matrix, die die 46 beobachteten Identitaeten halten koennte,
  braucht 13,8 kB (2.3).
- Genauigkeit: 39 Elemente in 256 Buckets, erwartete Kollisionen im niedrigen einstelligen Bereich.
  Fuer eine **Rangordnung** reicht das — der gemessene Abstand ist 29 zu 1, nicht 29 zu 27.
  Fuer die **Namensliste** auf der Web-Seite reicht es nicht; die bleibt an den Zeilen haengen und
  bleibt gekappt.

Das ist ein Vorschlag, keine Entscheidung: Hashfunktion, Bitbreite und Verfall (ein Bitfeld altert
nicht von selbst — vermutlich zwei alternierende Felder mit Fensterwechsel) sind offen und gehoeren
am Feldlaufdatensatz simuliert, bevor jemand sie baut.

**Die Election bleibt als Rueckfallebene im Konzept**, fuer den Fall, dass die Naeherung im Feld
nicht traegt. Dann waere der Weg eine gemeldete Wichtigkeitszahl im HEY-Bericht — mit der Warnung,
dass `HEY_PATH_PAYLOAD_MAX` die Kette schon heute deckelt, jedes Byte sich mit der Hopzahl
multipliziert und `appendHeySignalReport()` die Kette bei Ueberlaenge abbricht. Airtime-Rechnung vor
Formatentscheidung. Sie ist aber nach heutiger Datenlage **nicht der naechste Schritt**.

### 5.4 Die Wichtigkeitsformel

```
Wichtigkeit(X) = ExklusiveBlaetter(X) x f(Reichweite(X))
```

mit saettigendem `f` (log oder Kappung), damit Reichweite skaliert, aber nicht dominiert — sonst
gewinnt der Bergknoten mit einem einzigen sehr weit entfernten Partner. **Parameter sind bewusst
offen** und am Feldlaufdatensatz zu kalibrieren. ADR 02 ist diesen Weg mit `IMP_CAP` schon gegangen
und hat den Wert nach Produktionsevidenz von 8 auf 4 korrigiert; die Erfahrung ist
wiederzuverwenden, nicht zu wiederholen.

Gegenprobe am Datensatz: DL2JA-2 (29 Blaetter, 82,6 km) gegen DB0ED-99 (1 Blatt, 61,9 km) — die
Rangfolge steht unabhaengig von jeder Wahl von `f`. Der Datensatz taugt also zur Plausibilisierung,
**nicht zur Kalibrierung**: er enthaelt keinen Grenzfall.

### 5.5 Wo das im Code ansetzt

- **Prioritaet:** `getMessagePriority()` (`src/txring_functions.cpp:46-73`) vergibt Relay = Prio 3.
  **Die Scope-Warnung aus ADR 02 gilt unveraendert:** relayte HEY landen in Prio 5, relayte POS in
  Prio 4, nur relayter **Text** in Prio 3 — und Text ist unter 1 % des Relay-Volumens. Wer die
  Kanallast senken will, muss die Absenkung an POS und HEY haengen. Der Feldlauf bestaetigt die
  Groessenordnung: unter den 6 Nachbarn dominieren H- und P-Frames, T ist die Minderheit.
- **Abbruch:** die Cancel-Schleife `src/lora_functions.cpp:654-698` so oeffnen, dass sie
  `RING_STATUS_DONE`-Eintraege **dann** sieht, wenn die Matrix Fall B sagt. Der Gate muss eng sein:
  ein Relay fuer einen exklusiv versorgten Knoten darf nie abgebrochen werden.

### 5.6 Falsifizierbare Messung

Der `--nbrdebug`-Mitschnitt liefert die Zeitreihe bereits. Vor und nach der Aenderung zu messen:
Zahl der eigenen Relays, Zahl der abgebrochenen Relays, und als Gegenprobe die Zustellrate zu den
Knoten, die zwischenzeitlich als exklusiv gemeldet waren. **Rueckgang der Relays ohne Rueckgang der
Zustellung** ist das Erfolgskriterium.

Der jetzige Lauf ist die Vorher-Messung. Er darf dafuer nur benutzt werden, soweit die Tabelle nicht
voll war — in 43 von 46 Schnappschuessen war sie es (2.3), das ist beim Vergleich zu beruecksichtigen.

---

## 6. Offene Punkte

| #   | Frage                                                                                                                                      | Blockiert          |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------ |
| 1   | Traegt die Bitfeld-Naeherung (5.3)? Hashfunktion, Bitbreite, Verfall — am Feldlaufdatensatz simulieren, bevor jemand sie baut.             | Rang ohne Election |
| 2   | Reicht `NBR_MAX_ROWS = 21` ueberhaupt noch, oder ist der 2-Hop-Schnitt zu grosszuegig? 46 Identitaeten auf 21 Zeilen (2.3).                | alles              |
| 3   | Wie belastbar ist `NBR_FLAG_GW` nach dem 2-Hop-Schnitt? Mehrfachbeobachtung als Bedingung?                                                 | Gateway-Ausnahme   |
| 4   | Saettigungsfunktion `f(Reichweite)`: der Datensatz enthaelt keinen Grenzfall, also kein Kalibrierdatensatz. Woher kommt einer?             | Wichtigkeitsformel |
| 5   | An welchem Prio-Band haengt die Absenkung wirklich? Prio 3 traegt unter 1 % des Volumens (ADR 02, Rev. 3).                                 | Sendeentscheidung  |
| 6   | Tiebreak bei gleicher Wichtigkeit — Rufzeichen ist deterministisch, aber auf Dauer unfair.                                                 | Rang               |
| 7   | Mobile Knoten: Zeile 0 wird nur einmal aus den Settings gesetzt (Verdict-Finding L3). Ein bewegter Knoten rechnet mit falscher Reichweite. | Wichtigkeitsformel |
| 8   | Bestaetigt die 24-h-Endauswertung (ab 22.09. 19:20) die Rangstabilitaet aus 2.1, oder kippt sie ueber den Tag?                             | alles ab Stufe 2c  |

---

## 7. Vorgeschlagene Ausbaustufen

| Stufe | Inhalt                                                                                                                    | Risiko | Sendeverhalten |
| ----- | ------------------------------------------------------------------------------------------------------------------------- | ------ | -------------- |
| 2a    | Nur Anzeige: vertikale Spaltenkoepfe, Spalten `D/I`, `G`, `M`, `#N`, `#X` mit Hover + Legende; Zeilenumbruch in `Hearers` | keins  | unveraendert   |
| 2b    | `nbrRowMeshNeed()` gibt zusaetzlich die **Zahl** der unabgedeckten Knoten zurueck (speist `#X`)                           | gering | unveraendert   |
| 2c    | Wichtigkeitszahl und Rang lokal berechnen und anzeigen                                                                    | gering | unveraendert   |
| 2d    | Bitfeld-Naeherung (5.3), damit `#N`/`#X` nicht mehr an `NBR_MAX_ROWS` haengen                                             | mittel | unveraendert   |
| 2e    | Gateway-Ausnahme in die Exklusivitaetsrechnung der **Sendeentscheidung**                                                  | mittel | greift ein     |
| 2f    | Overhear-Cancel fuer Relays oeffnen, matrixgesteuert (Fall B)                                                             | mittel | greift ein     |
| 2g    | Prioritaetsabsenkung fuer redundante Relays                                                                               | hoch   | greift ein     |
| 2h    | Election ueber HEY — **nur falls 2d im Feld nicht traegt**                                                                | hoch   | greift ein     |

2a bis 2d aendern kein Funkverhalten und sind ohne Feldrisiko baubar; sie liefern zugleich die Daten,
an denen 2e bis 2g kalibriert werden. Ab 2e gilt: nichts ohne Vorher/Nachher-Messung aus 5.6.
