# Nachbarschaftsmatrix, Stufe 2: Wichtigkeit, Rang und prioritaetsgesteuertes Meshen

**Status:** Konzept, nichts davon ist Code. Aufgenommen 2026-09-22 aus der Betreiber-Sitzung zum
Produktions-Screenshot von DK5EN-98 (Web-Seite `?page=neighbours`, 21 frische Zeilen).

**Vorgaenger:** `docs/nachbarschaftsmatrix-campaign.md` (Stufe 1, was gebaut ist),
`docs/nachbarschaftsmatrix-verdict.md` (Advisor-Befunde), `docs/nbr-logformat.md` (Logformat des
24-h-Feldlaufs), `docs/adr-nc-importance-backoff.md` (ADR 02: dieselbe Zielrichtung, aber aus dem
HEY-`NC` statt aus der Matrix hergeleitet), `docs/Relay-Prioritaeten-Verdict.html` (Overhear-Cancel
und MPR-Kostenrechnung).

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
   netzbildendste Knoten (viele exklusive Blaetter, grosse Reichweite) wird Super-Node der Umgebung
   und mesht mit hoher Prioritaet, die kleineren warten.

Dazu drei Arbeitspakete: **UI** (Abschnitt 3, sofort machbar, reine Anzeige), **Sendeentscheidung**
(Abschnitt 4, greift in den TX-Ring ein), **Wichtigkeit und Wahl** (Abschnitt 5, braucht neue Daten
oder ein Protokollfeld).

---

## 2. Ausgangslage: was heute schon da ist

| Baustein                      | Ort                                                                | Aussage                                                            |
| ----------------------------- | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| Kreuzmatrix "Y hat X gehoert" | `src/nbr_matrix.{h,cpp}`, `cells[X][Y]`                            | 2-Hop-Fenster, 12 h Verfall, N = 21/13/11 je Board                 |
| Urteil `nbrExclusive()`       | `src/nbr_matrix.cpp`                                               | Zeilen, die im Fenster **nur ich** hoere                           |
| Urteil `nbrRowMeshNeed()`     | `src/nbr_matrix.cpp`                                               | Muss **der Nachbar** selbst meshen? (NA/MESH/RED)                  |
| Reichweite `nbrReach()`       | `src/nbr_matrix.cpp`                                               | groesste Distanz einer Zeile zu einem Partner, mit Partnername     |
| Flags `GW` / `MESH` / `POS`   | `NbrRow.flags`                                                     | GW aus HEY-Ziel `HG`, MESH aus dem `msg_mesh`-Bit eines POS-Frames |
| Web-Seite (die zwei Tabellen) | `src/web_functions/web_functions.cpp:1574` `sub_page_neighbours()` | Kreuztabelle + Zeilentabelle, Quelle des Screenshots               |
| Overhear-Cancel im TX-Ring    | `src/lora_functions.cpp:654-698`                                   | **existiert**, scannt nach `msg_id` und gibt den Slot frei         |
| CSMA-Prioritaeten 1..5        | `src/txring_functions.cpp:46-73`, `src/configuration_global.h`     | ACK/DM 3000, Gruppe 3000, Text-Relay 4500, POS 5500, HEY 5500 ms   |
| `NCT` im HEY-Bericht          | `appendHeySignalReport()`, `src/aprs_functions.cpp:1134`           | jeder Hop haengt seine eigene `getMheardCount()` an                |

Zwei Punkte daraus sind fuer alles Weitere entscheidend und duerfen nicht neu "entdeckt" werden:

- **Der Overhear-Cancel muss nicht gebaut werden.** Er ist da. Er ist nur **strukturell fuer Relays
  gesperrt**: die Schleife ueberspringt Eintraege mit `RING_STATUS_DONE` und `RING_STATUS_READY`,
  und genau `RING_STATUS_DONE` bekommt ein Relay beim Einreihen (`addTxRingEntry()` mit
  `"rx_relay"`). Die Aufgabe in Abschnitt 4 heisst also nicht "Cancel bauen", sondern "den
  bestehenden Scan matrixgesteuert auch Relay-Eintraege sehen lassen".
- **`NCT` wird heute weggeworfen.** `nbrApplyGroup()` (`src/nbr_matrix.cpp:308`) zerlegt die Gruppe
  `NCT,RSSI,SNR`, benutzt aber nur `RSSI`. Die Nachbarzahl, die der anhaengende Hop **selbst**
  gezaehlt hat, liegt also bereits auf dem Draht und kostet kein einziges Byte Neuentwurf.

---

## 3. Anzeige: was die zwei Tabellen zusaetzlich zeigen muessen

### 3.1 Kreuztabelle (Tabelle 1)

**Heute:** Kopfzeile sind nackte Nummern 1..21, die linke Spalte ist `<Nr> <Call>`.

**Gefordert:**

1. **Spaltenkopf zusaetzlich mit Rufzeichen inkl. SSID, vertikal gesetzt**, damit die Tabelle nicht
   in die Breite laeuft. Umsetzung: `writing-mode: vertical-rl; transform: rotate(180deg)` auf der
   Kopfzelle, Nummer bleibt darueber oder darunter stehen. Achtung auf den bestehenden CSS-Fallstrick
   dieser Seite: das Scaffold-CSS greift nur auf `#content_inner > table`, ein Wrapper-`div` wuerde
   die Matrix aus dem Selektor werfen (Bench 2026-09-20). Die Tabelle scrollt deshalb selbst als
   Block — das bleibt so.
2. **Zusaetzliche Spalten links**, je Zeile:

| Spalte | Bedeutung                                     | Quelle heute                                                                      | Aufwand                            |
| ------ | --------------------------------------------- | --------------------------------------------------------------------------------- | ---------------------------------- |
| `D/I`  | **D**irekt gehoert oder nur **I**ndirekt      | `cells[X][0]` frisch und gesetzt → D, sonst I                                     | vorhanden, nur Anzeige             |
| `G`    | Knoten ist Gateway                            | `NBR_FLAG_GW`                                                                     | vorhanden (Einschraenkung s. u.)   |
| `M`    | Knoten hat Mesh eingeschaltet                 | `NBR_FLAG_MESH`                                                                   | vorhanden (Einschraenkung s. u.)   |
| `#N`   | Anzahl Nachbarn dieses Knotens                | heute nur als **Untergrenze** aus der Matrix zaehlbar; korrekt aus `NCT` (s. 3.3) | neue Zaehlfunktion bzw. NCT-Ablage |
| `#X`   | Anzahl **exklusiver** Nachbarn dieses Knotens | Innenleben von `nbrRowMeshNeed()`, das heute nur MESH/RED zurueckgibt             | neue Zaehlfunktion                 |

**Einschraenkungen, die mit angezeigt gehoeren:**

- `G` wird nur gesetzt, wenn ein HEY mit Ziel `HG` ankam **und** der Absender eine aufgeloeste Zeile
  hat. Nach dem 2-Hop-Schnitt verfaellt das fuer weiter entfernte Absender still. `G = no` heisst
  also "nicht beobachtet", nicht "kein Gateway".
- `M` kommt ausschliesslich aus dem `msg_mesh`-Bit eines POS-Frames. Ein Knoten ohne frisches POS in
  der Zeile hat schlicht keine Aussage.
- `#N` aus der eigenen Matrix ist strukturell eine **Untergrenze**: ich sehe nur, was ich selbst
  beobachtet habe. Die Matrix ist meine Sicht, nicht die des Nachbarn.

### 3.2 Zeilentabelle (Tabelle 2)

**Heute:** die Spalte `Hearers` waechst unbegrenzt nach rechts (im Screenshot bei DL2JA-1 sechs
Rufzeichen in einer Zeile). **Gefordert:** Umbruch innerhalb der Zelle nach n Eintraegen bzw. per
CSS (`word-break`/`overflow-wrap` plus feste Maximalbreite der Spalte), sodass die Tabelle auf dem
Telefon in der Breite stehenbleibt und stattdessen in der Hoehe waechst.

Sinnvoll gleich mitzunehmen: `Hearers` nach Wichtigkeit statt nach Zeilenindex sortieren, sobald
Abschnitt 5 eine Rangzahl liefert.

### 3.3 Der billige Gewinn: `NCT` aufheben

`nbrApplyGroup()` parst `NCT` bereits und verwirft es. Ein `uint8_t nct` in `NbrRow` (die Zeile ist
heute 24 Byte, das Feld passt in vorhandenes Padding oder kostet 1 Byte × N) macht aus der
Untergrenze `#N` die **selbstgemeldete Nachbarzahl** des Knotens.

Zuordnung, bevor das jemand implementiert: Gruppe `i` gehoert zum Paar `(p[i-1], p[i])`, und
angehaengt hat sie der Knoten, der das Frame **empfangen** hat, also `p[i]` — er schreibt seine
eigene `getMheardCount()` hinein (`src/lora_functions.cpp:1533,1646`). `NCT` gehoert damit in die
Zeile von `p[i]`, nicht von `p[i-1]`. Das ist am Host-Test festzunageln, bevor eine Anzeige darauf
baut.

---

## 4. Die Sendeentscheidung: von binaer zu dreiwertig

### 4.1 Der Gedankengang

Heute ist Meshen ein Schalter. Die Matrix erlaubt eine feinere Staffelung:

- **Fall A — ich versorge jemanden exklusiv.** Mein Relay ist die einzige Chance dieses Knotens.
  → hohe Prioritaet, kein Abbruch.
- **Fall B — alles, was ich hoere, hoert auch jemand anders.** Mein Relay ist reine Verdopplung.
  → sehr niedrige Prioritaet (langer Backoff). Und waehrend ich auf den Sendeslot warte, hoere ich
  mit: wird dasselbe Frame in der Zwischenzeit von einem anderen Knoten gemesht, **loesche ich
  meinen Eintrag aus dem Sendepuffer**. Er bringt nichts mehr, das Frame ist unterwegs.

Fall B ist der eigentliche Hebel. Er kostet nichts an Zustellwahrscheinlichkeit (der Abbruch
passiert nur nach bewiesener Weiterleitung) und nimmt Last vom Kanal — und zwar genau dort, wo
heute die meiste Last entsteht.

### 4.2 Die Gateway-Ausnahme

**Ein exklusiv von mir versorgter Knoten, der `GW = on` hat, muss von mir nicht versorgt werden.**
Er bekommt ueber seine Gateway-Anbindung ohnehin die vollstaendigere Information als ueber meinen
einen HF-Hop. Der Knoten faellt also aus der Exklusivitaetsrechnung heraus, bevor sie zur
Sendeentscheidung wird.

Sauber formuliert: `nbrExclusive()` bleibt, wie es ist (es beantwortet die Beobachtungsfrage). Die
**Sendeentscheidung** filtert die Ergebnismenge zusaetzlich um alle Zeilen mit `NBR_FLAG_GW`. Bleibt
danach nichts uebrig, gilt Fall B.

Vorsicht dabei: wegen der Einschraenkung aus 3.1 ist `GW` beobachtet, nicht verbuergt. Ein
Fehlurteil hier faellt in die unsichere Richtung (ich mesche nicht fuer jemanden, der doch kein
Gateway hat). Das spricht dafuer, das GW-Flag fuer diesen Zweck **mit eigenem Verfall** zu fuehren
oder es nur zu glauben, wenn es mehrfach im Fenster gesehen wurde. Offener Punkt.

### 4.3 Wo das im Code ansetzt

- **Prioritaet:** `getMessagePriority()` (`src/txring_functions.cpp:46-73`) vergibt Relay = Prio 3.
  Eine matrixabhaengige Absenkung auf ein neues Prio-Band unterhalb von HEY ist der Eingriff. **Die
  Scope-Warnung aus ADR 02 gilt hier unveraendert:** relayte HEY landen in Prio 5, relayte POS in
  Prio 4, nur relayter **Text** in Prio 3 — und Text ist unter 1 % des Relay-Volumens. Wer die
  Kanallast wirklich senken will, muss die Absenkung an POS und HEY haengen, nicht an Prio 3.
- **Abbruch:** die Cancel-Schleife `src/lora_functions.cpp:654-698` so oeffnen, dass sie
  `RING_STATUS_DONE`-Eintraege **dann** sieht, wenn die Matrix Fall B sagt. Der Gate muss eng sein:
  ein Relay eines Knotens, den ich exklusiv versorge, darf nie abgebrochen werden.

### 4.4 Falsifizierbare Messung, bevor das ins Feld geht

Der Feldlauf-Mitschnitt (`--nbrdebug`, `tools/nbrlog.py`) liefert bereits die Zeitreihe der Urteile.
Vor und nach der Aenderung zu messen: Zahl der eigenen Relays, Zahl der abgebrochenen Relays, und
als Gegenprobe die Zustellrate zu den Knoten, die zwischenzeitlich als exklusiv gemeldet waren. Ein
Rueckgang der Relays ohne Rueckgang der Zustellung ist das Erfolgskriterium; alles andere ist eine
Fehlmessung oder ein Fehlurteil der Matrix.

---

## 5. Wichtigkeit und Rang: wer ist der Super-Node?

### 5.1 Die Kriterien des Betreibers

| Kriterium                                             | Wirkung auf die Wichtigkeit                                    |
| ----------------------------------------------------- | -------------------------------------------------------------- |
| Viele exklusive Blaetter                              | hoch                                                           |
| Viele exklusive Blaetter, die **weit weg** sind       | sehr hoch — netzbildend, verbindet Gebiete                     |
| Viele exklusive Blaetter, aber **geringe Reichweite** | gedaempft — soll dem weitreichenden Knoten den Vortritt lassen |
| Knoten haengt selbst an einem Gateway                 | senkt den Bedarf, fuer ihn zu meshen (4.2)                     |

Das laeuft auf eine gewichtete Groesse hinaus, ungefaehr:

```
Wichtigkeit(X) = ExklusiveBlaetter(X) x f(Reichweite(X))
```

mit einer saettigenden Funktion `f` — Reichweite soll skalieren, aber nicht dominieren, sonst gewinnt
immer der Bergknoten mit einem einzigen sehr weit entfernten Partner. Kandidaten fuer `f`: `log`,
oder eine Kappung auf einen Maximalwert. **Die Parameter sind hier bewusst nicht festgelegt** — sie
gehoeren aus dem Feldlauf-Datensatz kalibriert, nicht geraten. ADR 02 hat mit `IMP_CAP` genau diesen
Weg schon einmal genommen (und den Wert nach Produktionsevidenz von 8 auf 4 korrigiert); die
Erfahrung ist wiederzuverwenden, nicht zu wiederholen.

### 5.2 Zwei Wege zum Rang

**Weg 1 — aus der eigenen Kreuzmatrix ableiten (heute moeglich).** Ich rechne die Wichtigkeit fuer
jede meiner direkten Zeilen aus und sortiere. Kein neues Protokollfeld, kein Byte Luft.
**Grenze:** Ich sehe nur zwei Hops und nur, was ich selbst beobachtet habe. Die exklusiven Blaetter
meines Nachbarn kenne ich nur, soweit ich sie auch gehoert habe — mein `#X` fuer ihn ist eine
Untergrenze, und zwar eine systematisch schiefe: Blaetter hinter ihm, die ich nicht hoere, fehlen
vollstaendig. Genau die machen ihn aber wichtig. **Weg 1 unterschaetzt strukturell die Knoten, die
am wichtigsten sind.**

**Weg 2 — Wahl unter den Nachbarn (Election).** Jeder Knoten rechnet seine Wichtigkeit aus **seiner
eigenen** Matrix (wo sie vollstaendig ist) und meldet sie. Wer die hoechste meldet, ist Super-Node
der Umgebung und mesht mit hoher Prioritaet; die anderen staffeln sich darunter ein. Das behebt die
Schieflage von Weg 1, kostet aber ein Feld auf dem Draht.

**Empfehlung: Weg 2, aber ueber den bestehenden HEY-Bericht.** Die Gruppe `NCT,RSSI,SNR` traegt
bereits eine selbstgemeldete Kennzahl pro Hop. Eine Wichtigkeitszahl gehoert in dieselbe Familie —
entweder als viertes Feld (bricht die Formatpruefung "genau zwei Kommas" in `nbrApplyGroup()`, also
versionierter Umbau) oder, sparsamer, indem `NCT` durch eine kombinierte Kennzahl ersetzt bzw.
begleitet wird. **Vorher zu klaeren:** `HEY_PATH_PAYLOAD_MAX` deckelt die Kette schon heute, jedes
zusaetzliche Byte pro Hop multipliziert sich mit der Hopzahl, und `appendHeySignalReport()` bricht
die Kette ab, wenn sie zu lang wird. Airtime-Rechnung vor Formatentscheidung.

### 5.3 Was ein Rang dann steuert

Der Rang `r` (0 = Super-Node der Umgebung) mappt auf das Backoff-Fenster: hoher Rang = kurzer
Backoff = darf zuerst. Die niedrigeren Raenge hoeren waehrend ihres laengeren Wartens den Super-Node
meshen und brechen ab (Abschnitt 4.1, Fall B) — **Rang und Overhear-Cancel sind dasselbe System, aus
zwei Richtungen betrachtet.** Ohne den Cancel ist die Rangordnung nur ein Reihenfolgetrick, mit ihm
wird sie zur Lastreduktion.

Genau das ist die Slot-Mechanik aus ADR 02, Kapitel 4 — mit dem Unterschied, dass die Eingangsgroesse
nicht mehr der gemeldete `NC` (wie viele hoert er) ist, sondern die Wichtigkeit (wie viele haengen
exklusiv von ihm ab, und wie weit traegt er). Das ist die bessere Groesse: `NC` belohnt den Knoten im
Ballungsraum, Wichtigkeit belohnt den Knoten, ohne den etwas abreisst.

---

## 6. Offene Punkte

| #   | Frage                                                                                                                                            | Blockiert                |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------ |
| 1   | Gehoert `NCT` zu `p[i]` oder `p[i-1]`? Am Host-Test festnageln.                                                                                  | Spalte `#N` (3.3)        |
| 2   | Wie belastbar ist `NBR_FLAG_GW` nach dem 2-Hop-Schnitt? Eigener Verfall, Mehrfachbeobachtung?                                                    | Gateway-Ausnahme (4.2)   |
| 3   | Saettigungsfunktion `f(Reichweite)` und ihre Parameter — aus dem Feldlauf kalibrieren.                                                           | Wichtigkeitsformel (5.1) |
| 4   | Protokollformat fuer die gemeldete Wichtigkeit: viertes Feld vs. Ersatz fuer `NCT`; Airtime-Rechnung gegen `HEY_PATH_PAYLOAD_MAX`.               | Election (5.2)           |
| 5   | An welchem Prio-Band haengt die Absenkung wirklich? Prio 3 traegt unter 1 % des Volumens (ADR 02, Rev. 3).                                       | Sendeentscheidung (4.3)  |
| 6   | Was passiert bei zwei Knoten mit gleicher Wichtigkeit? Tiebreak ueber Rufzeichen ist deterministisch, aber unfair auf Dauer.                     | Election (5.2)           |
| 7   | Mobile Knoten: Zeile 0 wird heute nur einmal aus den Settings gesetzt (Verdict-Finding L3). Ein bewegter Knoten rechnet mit falscher Reichweite. | Wichtigkeitsformel (5.1) |

---

## 7. Vorgeschlagene Ausbaustufen

| Stufe | Inhalt                                                                                                | Risiko | Sendeverhalten |
| ----- | ----------------------------------------------------------------------------------------------------- | ------ | -------------- |
| 2a    | Nur Anzeige: vertikale Spaltenkoepfe, Spalten `D/I`, `G`, `M`, `#N`, `#X`; Zeilenumbruch in `Hearers` | keins  | unveraendert   |
| 2b    | `NCT` in `NbrRow` aufheben, `#N` wird selbstgemeldet statt geschaetzt                                 | gering | unveraendert   |
| 2c    | Wichtigkeitszahl lokal berechnen und anzeigen (Weg 1), Rang nur als Anzeige                           | gering | unveraendert   |
| 2d    | Gateway-Ausnahme in die Exklusivitaetsrechnung der **Sendeentscheidung**                              | mittel | greift ein     |
| 2e    | Overhear-Cancel fuer Relays oeffnen, matrixgesteuert (Fall B)                                         | mittel | greift ein     |
| 2f    | Prioritaetsabsenkung fuer redundante Relays                                                           | hoch   | greift ein     |
| 2g    | Election ueber HEY, Super-Node mit hoher Prioritaet                                                   | hoch   | greift ein     |

2a bis 2c aendern kein Funkverhalten und sind ohne Feldrisiko baubar — sie liefern zugleich die
Daten, an denen 2d bis 2g kalibriert werden. Ab 2d gilt: nichts ohne Vorher/Nachher-Messung aus
Abschnitt 4.4.
