# NBR-Logformat (Nachbarschaftsmatrix, 24-h-Dauertest)

Verbindlicher Vertrag zwischen Firmware (`src/nbr_matrix.*`, `src/lora_functions.cpp`) und
Parser (`tools/nbrlog.py`). Wer eine Zeile aendert, aendert beide Seiten und diese Datei.

## Trennzeichen ist `|`, nicht `;`

`printfdeb()` entfernt Semikolons aus dem Format-String, solange `--debug csv` nicht aktiv ist
(`src/printfdeb_format.h`, Kommentar oben in der Datei). Ein `;`-getrenntes Format kaeme auf der
Netz-Konsole als zusammengeklebter Text an. Darum `|`.

Zahlenfelder sind vorzeichenbehaftete Dezimalzahlen ohne Auffuellung. Kein `%lld`/`%llu`
(auf nRF52 gibt die nano-printf den Formatbuchstaben woertlich aus).

`<up>` ist immer die Minute seit Boot (`millis()/60000`, 16 Bit, laeuft nach 45,5 Tagen um) --
dieselbe Zeitbasis wie `NbrRow.last_min`. Nicht die Wanduhr.

`<type>` ist `T` (Text `:`), `P` (Position `!`) oder `H` (HEY `@`).

**Text erzeugt seit 2026-09-23 abends nur noch `ME`-Zeilen** (Welle 6, `docs/nbr-stage2-campaign.md`):
kein `EDGE` mit `<type>` `T`, kein `CUT` fuer Text, keine Zeile fuer ein Pfad-Token ausser dem
letzten Hop. Grund: Gateways mit Mesh an senden vom Server eingespeiste Texte mit
`<Server-Pfad>,<Gateway>` auf LoRa, das Paar davor war nie ein Funkempfang. Mitschnitte aelterer
Firmware enthalten solche `EDGE|...|T`-Zeilen noch; wer alte und neue Laeufe vergleicht, filtert
Text-`EDGE` aus dem alten Lauf vorher heraus -- die Skripte haben dafuer keinen Schalter:
`grep -vE '\[NBR\]\|EDGE\|[^|]*\|[^|]*\|[^|]*\|T\|' alt.log > alt-ohne-text.log`.

`<rssi>` ist dBm als negative Ganzzahl, `0` heisst "kein Bericht". `<snr>` (wo vorhanden) ist die
in der Matrix gespeicherte SNR in dB, vorzeichenbehaftet, oder das Literal `NA`, wenn keine
bekannt ist -- dieselbe "kein Bericht"-Semantik wie `<rssi>` == `0`, nur eben als Text statt als
Zahl, weil `0` dB eine gueltige SNR ist.

## Zeilen

`<snr>` bei `EDGE` und `ME` ist ein NEUES, trailendes Feld -- Mitschnitte aelterer Firmware haben
es nicht. `tools/nbrlog.py`, `tools/nbrsnap.py` und `tools/nbrrelay.py` muessen beide Feldzahlen
(mit und ohne `<snr>`) klaglos parsen; wer eine dieser Zeilen aendert, aendert alle drei Skripte
und diese Datei.

| Zeile                                                                               | Wann                                                                                                                                                                                                                                                                                                          |
| ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[NBR]\|EDGE\|<up>\|<from>\|<to>\|<type>\|<rssi>\|<cnt>\|<snr>`                     | Jede eingetragene Hoerbeziehung "`<to>` hat `<from>` gehoert". `<rssi>` ist in neuen Mitschnitten IMMER `0` -- die Zelle speichert keine RSSI mehr, nur noch SNR (siehe unten). `<snr>` ist die fuer "`<to>` hat `<from>` gehoert" gespeicherte SNR (aus HEY-Berichten) und kann aelter sein als diese Zeile. |
| `[NBR]\|ME\|<up>\|<from>\|<type>\|<rssi>\|<cnt>\|<snr>`                             | Letzter Hop von mir direkt gehoert (Zelle `[last][0]`). `<rssi>` unveraendert die empfangene RSSI dieses Frames. `<snr>` die fuer "ich habe `<from>` gehoert" gespeicherte SNR.                                                                                                                               |
| `[NBR]\|CUT\|<up>\|<ntok>\|<kept>\|<path>`                                          | Pfad war laenger als das 2-Hop-Fenster; `<ntok>-<kept>` Token verworfen.                                                                                                                                                                                                                                      |
| `[NBR]\|DROP\|<up>\|<reason>\|<path>`                                               | Frame komplett verworfen. `<reason>`: `TOK`, `LOOP`, `FULL`, `TYPE`, `RPT` (fehlerhafter HN-Bericht, siehe Stufe 3 unten -- `<path>` ist dort nur `<x>`, der Absender).                                                                                                                                       |
| `[NBR]\|EVICT\|<up>\|<idx>\|<old>\|<new>`                                           | Zeile `<idx>` verdraengt, `<old>` war das Opfer.                                                                                                                                                                                                                                                              |
| `[NBR]\|POS\|<up>\|<call>\|<lat>\|<lon>\|<mesh>\|<hw>`                              | Positionscache gefuellt (`<mesh>` 0/1, `<lat>`/`<lon>` mit 5 Nachkommastellen).                                                                                                                                                                                                                               |
| `[NBR]\|SNAP\|<up>\|<own>\|<rows>\|<maxrows>\|<cells>`                              | Alle 15 min, eroeffnet einen Snapshot-Block.                                                                                                                                                                                                                                                                  |
| `[NBR]\|ROW\|<up>\|<idx>\|<call>\|<flags>\|<age>\|<hearers>\|<verdict>\|<meshneed>` | Eine Zeile je belegter Matrixzeile, direkt nach `SNAP`.                                                                                                                                                                                                                                                       |
| `[NBR]\|ENDSNAP\|<up>`                                                              | Schliesst den Snapshot-Block.                                                                                                                                                                                                                                                                                 |

Damit speichert eine `EDGE`-Zelle seit dieser Aenderung keine RSSI mehr, nur noch eine SNR (fuer
den `--nbrsym`-Symmetrie-Fallback, siehe unten) -- `<rssi>` bleibt im Format stehen, ist in neuen
Mitschnitten aber wertlos (immer `0`) und darf von keiner Auswertung mehr als Messwert verwendet
werden. `tools/nbrlog.py` ignoriert `EDGE`-`<rssi>` in seinen RSSI-Statistiken entsprechend (dort
zaehlte `0` ohnehin schon immer als "kein Bericht").

`<flags>` ist die rohe `NbrRow.flags`-Zahl (dezimal), `<age>` das Zeilenalter in Minuten,
`<hearers>` die Anzahl Knoten, die diese Zeile hoeren.

Es gibt ZWEI Urteile, und sie beantworten entgegengesetzte Fragen an denselben Kanten. Sie
duerfen nicht miteinander verglichen werden -- genau das war der Fehler, den der Advisor-Pass
am 2026-09-21 gefunden hat.

- `<verdict>` -- Konzept 4.3, die Sicht auf den Knoten als GEHOERTEN: `EXCL` heisst "ausser mir
  hoert diese Zeile niemand", also ist MEIN Meshen fuer sie noetig. Sonst `RED`. Zeile 0 und
  Zeilen ohne Hoerbeziehung: `LEAF` bzw. `UNK`.
- `<meshneed>` -- die Betreiberfrage, die Sicht auf den Knoten als HOERER: `MESH` heisst "dieser
  direkt gehoerte Nachbar hoert mindestens einen Knoten, den kein anderer meiner direkten
  Nachbarn und ich selbst nicht hoere", er muss also selbst meshen. `RED` heisst, seine gehoerte
  Menge ist bereits abgedeckt. `NA` steht fuer jede Zeile, die kein direkt gehoerter Nachbar ist
  (inklusive Zeile 0) -- fuer sie ist die Frage nicht gestellt.

`tools/nbrlog.py` rechnet `<meshneed>` aus den `EDGE`/`ME`-Zeilen selbst nach und stellt es dem
Firmware-Wert gegenueber. `<verdict>` wird nur berichtet, nicht verglichen.

## Stufe 2: Relay-Entscheidung (`--nbrrelay count|on`, docs/nbr-wichtigkeit-konzept.md 5)

Diese Zeilen kommen nur mit `--nbrdebug on` UND `--nbrrelay count` oder `on`. `<need>`, `<alone>`
und das neue `<inferred>` sind Bitmasken ueber Zeilenindizes (Bit i = Zeile i der Matrix),
hexadezimal mit acht Stellen; `<msg_id>` ebenso. `<typ>` wie oben, `<relayer>` ist der letzte Hop
der gehoerten fremden Wiederholung.

`<inferred>` ist NEU und traegt bei `NEED`, `CANCEL?` und `CANCEL` (nicht bei `REFUSE`) nach: die
Teilmenge der jeweils betroffenen Zeilenindizes, deren Ergebnis auf einer `--nbrsym`-Annahme
beruht statt auf einer beobachteten Kante. Mitschnitte aelterer Firmware haben dieses Feld nicht
-- Parser muessen `NEED`/`CANCEL?`/`CANCEL` sowohl mit als auch ohne `<inferred>` akzeptieren.

| Zeile                                                                                | Wann                                                                                                                                                                                                                                                                                           |
| ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[NBR]\|NEED\|<up>\|<msg_id>\|<typ>\|<A\|B\|U>\|<need>\|<alone>\|<slot>\|<inferred>` | Relay eingereiht; `A` heisst `alone != 0` (nie Abbruch), `B` heisst Nachrang und Abbruch moeglich, `U` heisst kein Wissen (leere Matrix, ungueltiger Pfad): Relay laeuft wie heute, zaehlt in keinem Fall.                                                                                     |
| `[NBR]\|CANCEL?\|<up>\|<msg_id>\|<typ>\|<relayer>\|<vorher>\|<nachher>\|<inferred>`  | `count`: die gehoerte Wiederholung von `<relayer>` deckte den Restbedarf; nichts passiert. Einmal je Slot.                                                                                                                                                                                     |
| `[NBR]\|CANCEL\|<up>\|<msg_id>\|<typ>\|<relayer>\|<vorher>\|<nachher>\|<inferred>`   | `on`: dasselbe, und der Ring-Slot wurde freigegeben.                                                                                                                                                                                                                                           |
| `[NBR]\|REFUSE\|<up>\|<msg_id>\|<typ>\|<relayer>\|<alone>`                           | Fremde Wiederholung gehoert, aber `alone != 0`: Abbruch verweigert. Einmal je Slot. Kein `<inferred>`. Mit `--nbrsym on` kann der Relayer auch nur per Annahme als Deckender gelten; die zugehoerige `COVER`-Annahme wird dann nicht als `SYM`-Zeile geloggt (Advisor 2026-09-22, akzeptiert). |

`<vorher>`/`<nachher>` sind die Bedarfsmaske vor und nach Abzug der Hoerer des Relayers. Die
Zaehler dazu stehen in `--info` (`...NBRRELAY <off|count|on> ...relays A <n> B <n> ...cancelled
<n> ...possible <n> ...refused <n>`).

### `[NBR]|SYM` -- protokollierte Symmetrie-Annahme

```
[NBR]|SYM|<up>|<msg_id 8 hex>|<role>|<x>|<m>|<snr>
```

Wird nur geschrieben, wenn eine `--nbrsym`-Annahme das Ergebnis der Relay-Entscheidung tatsaechlich
veraendert hat (nicht bei jeder blossen Pruefung). Lesart: "angenommen, `<x>` hoert `<m>`, weil
`<m>` `<x>` mit `<snr>` dB gehoert hat". `<role>` ist eine von:

| `<role>` | Bedeutung                                                                                                          |
| -------- | ------------------------------------------------------------------------------------------------------------------ |
| `HASF`   | `<x>` zaehlt als "hat den Frame bereits", weil angenommen wird, dass es Pfadteilnehmer `<m>` hoert.                |
| `ALT`    | `<x>` zaehlt nicht als alleiniger Traeger, weil angenommen wird, dass es Versorger `<m>` hoert, der den Frame hat. |
| `COVER`  | `<x>` zaehlt als durch die gehoerte Wiederholung des Relayers `<m>` abgedeckt.                                     |

## `--nbrsym on|off` -- Symmetrie-Fallback fuer die Relay-Entscheidung

Neuer Konsolenschalter, persistiert, Default AN (`node_sset4`-Bit `0x0080`, seit 2026-09-25 `0x2000`, invertiert
gespeichert: gesetzt heisst AUS -- dasselbe Muster wie `--mesh`, damit jeder bestehende Knoten
ohne Migration mit AN startet).
Mit `--nbrsym on` faellt die Stufe-2-Relay-Entscheidung ("hat X den Frame schon", "gibt es einen
alternativen Versorger", "deckt eine gehoerte Wiederholung ab") auf Symmetrie zurueck, wenn die
Matrix "X hat M gehoert" nie beobachtet hat, aber "M hat X gehoert" mit einer SNR >= -16 dB
(einschliesslich) kennt. Eine beobachtete Kante gewinnt IMMER gegen eine Annahme. Die
Stufe-1-Anzeigeurteile (`<verdict>`, `<meshneed>`, `#N`/`#X` auf der Web-Seite) bleiben ausschliesslich
beobachtungsbasiert -- der Fallback gilt nur fuer die Relay-Entscheidung. Jede Annahme, die das
Ergebnis tatsaechlich veraendert, wird als `[NBR]|SYM`-Zeile protokolliert, damit sie sich
nachtraeglich pruefen laesst. Die `--info`-Zeile mit `NBRRELAY` bekommt ` ...NBRSYM on|off`
angehaengt.

Der Schwellwert -16 dB ist ein Erfahrungswert des Betreibers von seinem Heltec DK5EN-98: DF2SI-12
kommt dort mit einem SNR-Median von -16 dB an und faellt regelmaessig ins Rauschen. Es gibt keine Beruecksichtigung von
Endstufen (EBYTE E22, T-Beam 1 W, Nachruest-PAs): die beobachtete SNR wird genommen wie sie ist,
ohne Urteil ueber die Sendeleistung -- Symmetrie wird schlicht angenommen, auch fuer diese Knoten,
und jede Annahme wird per `SYM` protokolliert, damit das auditierbar bleibt. Messbasis (zitierbar):
unter 26 in beiden Richtungen beobachteten Verbindungen liegt die SNR-Differenz im Median bei
2,5 dB, maximal 12 dB; die grossen Ausreisser waren Sendeleistungs-Unterschiede (externe PA / 1-W-
Boards).

`tools/nbrlog.py` traegt die geloggten `SYM`-Zeilen im Bericht unter "Symmetrie-Annahmen"
zusammen (Anzahl je `<role>`, je Paar `(<x>, <m>)` mit Median-SNR) -- eine unerwartet hohe Zahl
zeigt, wie sehr sich die Stufe-2-Relay-Entscheidung auf den Fallback statt auf Beobachtung
stuetzt. Die Rolle `VETO` (siehe unten) zaehlt dort NICHT mit -- sie ist das Gegenteil einer
angewendeten Annahme und gehoert zum HN-Bericht.

## Stufe 3: HN-Nachbarschaftsmeldung (`--nbrreport off|auto|on`)

### Warum ein eigener Frame

Ein Knoten, der nie relayed (`--mesh off`, kein Gateway), taucht in keinem gehoerten Pfad eines
anderen Knotens auf und bleibt fuer die Nachbarschaftsmatrix unsichtbar -- die Matrix lernt
Kanten nur aus Pfaden relayter Frames. Drei Alternativen wurden verworfen, bevor die HN-Meldung
als eigener Frame entstand:

- **Ein ACK je Frame** haette die Beobachtungsluecke ebenfalls geschlossen, kostet aber pro
  Endpunkt in der Groessenordnung 60 s Airtime/h -- gegen ~4 s/h fuer die HN-Meldung (siehe
  unten) ein Vielfaches.
- **Huckepack auf dem regulaeren HEY** waere billiger gewesen, haette aber die
  Relayer-eigenen `NCT,RSSI,SNR`-Positionsgruppen im selben Frame verschoben (Format-Bruch fuer
  aeltere Firmware), waere netzweit relayt worden (die Meldung ist aber nur fuer die direkten
  Hoerer gedacht) und waere der Trickle-Unterdrueckung unterworfen gewesen -- ein Knoten, dessen
  HEY laengst von genuegend Nachbarn gehoert und deshalb unterdrueckt wird, haette dann auch nie
  eine HN-Meldung gesendet.

Der eigene Frame umgeht alle drei Punkte: eigener Typ mit fester Ziel-Kennung `HN`, `max_hop 0`
(nie relayt, nur der direkte Hoererkreis empfaengt ihn), fester Takt ausserhalb der
Trickle-Unterdrueckung.

### Frame

HEY-Rahmen (Typ `@`), Ziel `HN`, `max_hop 0`. Payload:

```
R<heard>;N<k>[+];<CALL>,<snr>;<CALL>,<snr>;...;
```

`<heard>` ist dieselbe Zahl wie im `R<n>` des regulaeren HEY: die Eintraege der MHeard-Tabelle des
Senders der letzten Stunde, unabhaengig vom SNR (`getMheardCount()`). Sie ist NICHT die Zahl der
Listeneintraege und kein Kappungssignal. `<k>` ist die Anzahl der GELISTETEN Eintraege: direkt
gehoerte Stationen der letzten 60 min mit SNR >= `LORA_SNR_STABLE_MIN_DB`
(`src/configuration_default.h`, an SF11/BW250/CR4/6 gebunden, aktuell -16 dB), absteigend nach SNR
sortiert, hoechstens 8. Das `+` ist das EINZIGE Kappungssignal: es steht genau dann, wenn mehr als 8
Stationen die Bedingung erfuellt haben. Ohne `+` ist die Liste vollstaendig: eine Station, die darin
fehlt, ist der Beleg, dass der Sender sie nicht mit >= -16 dB hoert. `<heard>` - `<k>` darf nicht als
"ungelistete Stationen" gelesen werden (MHeard zaehlt auch schwache und nicht mehr frische Stationen).

Beispiel, vollstaendig (kein `+`, obwohl `<heard>` groesser ist):

```
R12;N5;DL2JA-2,7;DB0ISM-1,5;DK5EN-98,-8;DB0ED-99,-11;DL2UD-1,-12;
```

Beispiel, gekuerzt (mehr als 8 qualifiziert, `N8+`):

```
R11;N8+;DL2JA-2,9;DB0ISM-1,6;DK5EN-98,3;DB0ED-99,1;DL2UD-1,-2;OE1AAA-1,-5;OE2BBB-9,-9;OE3CCC-3,-13;
```

Beispiel, leer (kein direkter Nachbar mit >= -16 dB gehoert -- `N0` bleibt vollstaendig, kein `+`):

```
R0;N0;
```

Aktuelle und aeltere Firmware ohne dieses Feature verwerfen `HN`-Frames beim Dekodieren
kommentarlos -- sie werden weder relayt noch hochgeladen noch angezeigt.

### Takt (`NBR_REPORT_INTERVAL_S`, `src/configuration_global.h`)

Alle `NBR_REPORT_INTERVAL_S` (== `TRICKLE_IMAX_S`, 15 min), erste Meldung `NBR_REPORT_FIRST_S`
(5 min) nach dem Start, danach `NBR_REPORT_JITTER_S` (0..30 s) Zufallsversatz je Meldung. Die
Meldung unterliegt zu keinem Zeitpunkt der Trickle-Unterdrueckung -- die eigene Hoerliste ist je
Knoten einmalig und nie redundant zu einem anderen Sender.

### Schalter `--nbrreport off|auto|on`

Persistiert in `node_sset4`: `0x0100` gesetzt heisst `off`, `0x0200` gesetzt heisst `on`, keins
von beiden heisst `auto` (Default). `auto` sendet nur, wenn Mesh AUS UND Gateway AUS sind -- genau
die Situation, in der der Knoten sonst unsichtbar bliebe. `--info` bekommt das Feld
` ...NBRREPORT off|auto|on` angehaengt.

### Empfaenger: gelistete Eintraege werden zu Kanten

Jeder gelistete Eintrag `<CALL>,<snr>` eines empfangenen `HN`-Berichts von `<x>` wird als
beobachtungsgleiche Kante "`<x>` hat `<CALL>` gehoert" mit dieser SNR eingetragen -- dieselbe
Semantik wie eine `EDGE` aus einem relayten Pfad, nur direkt aus dem Bericht statt aus einem
Pfad-Token. Ein vollstaendiger (kein `+`), frischer (innerhalb der letzten 45 min empfangener)
Bericht von `<x>` **verwirft** (veto) die `--nbrsym`-Symmetrie-Annahme fuer `<x>`, sobald diese
angewendet wuerde -- eine tatsaechlich beobachtete Abwesenheit (die Station fehlt in der
vollstaendigen Liste) sticht die Annahme "angenommen symmetrisch". Prioritaet, hoechste zuerst:
per Relay beobachtete Kante == per HN-Bericht gemeldete Kante > `--nbrsym`-Annahme.

### Neue Log-Zeilen

| Zeile                                                       | Wann                                                                                                                                                                                                                                                                                                                                                                     |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `[NBR]\|RPT\|<up>\|<x>\|<m>\|<snr>\|<status>`               | Ein gelisteter Eintrag aus einem empfangenen `HN`-Bericht von `<x>` verarbeitet. `<status>`: `ok` (Kante "`<x>` hat `<m>` gehoert" eingetragen), `self` (`<m>` bin ich selbst: "`<x>` hat mich gehoert"), `norow` (`<m>` hat keine Matrixzeile, ignoriert).                                                                                                              |
| `[NBR]\|RPTSUM\|<up>\|<x>\|<heard>\|<k>\|<full>\|<applied>` | Zusammenfassung EINES empfangenen Berichts, direkt bei Erhalt. `<heard>`/`<k>` wie im Frame oben. `<full>` ist `1`, wenn der Bericht vollstaendig war (kein `+`), sonst `0`. `<applied>` ist die Anzahl der tatsaechlich geschriebenen Kanten: Eintraege mit `<status>` `ok` ODER `self` (auch `self` schreibt eine Kante, "Absender hoert mich"); `norow` zaehlt nicht. |
| `[NBR]\|RPTTX\|<up>\|<len>\|<payload>`                      | Eigener Bericht gesendet. `<len>` ist die tatsaechliche On-Air-Laenge des Payloads, `<payload>` der Frame-Inhalt wie oben (die `;` darin sind Nutzdaten, kein `printfdeb`-Formatstring -- siehe "Trennzeichen" oben, sie ueberleben unangetastet).                                                                                                                       |
| `[NBR]\|DROP\|<up>\|RPT\|<x>`                               | Neuer `<reason>`-Wert fuer die bestehende `DROP`-Zeile (siehe oben): ein `HN`-Bericht von `<x>` war fehlerhaft (Parsing, Grammatik), nichts daraus wurde angewendet.                                                                                                                                                                                                     |

### `[NBR]|SYM|...|VETO` -- blockierte Annahme

Zusaetzlich zu `HASF`/`ALT`/`COVER` (oben) gibt es die Rolle `VETO`, mit derselben Zeilenform
`[NBR]|SYM|<up>|<msg_id>|VETO|<x>|<m>|<snr>`: eine `--nbrsym`-Annahme, die das Ergebnis der
Stufe-2-Relay-Entscheidung veraendert HAETTE, wurde durch einen vollstaendigen, frischen
`HN`-Bericht von `<x>` blockiert. Lesart: "`<x>` haette (angenommen) `<m>` gehoert, aber `<x>`s
eigener Bericht sagt, dass es das NICHT tut". `VETO` ist das Gegenteil einer angewendeten
Annahme -- `tools/nbrlog.py` zaehlt es deshalb nicht unter "Symmetrie-Annahmen" (Abschnitt 10),
sondern unter "HN-Nachbarschaftsmeldungen" (Abschnitt 11).

`tools/nbrlog.py` fasst die Stufe-3-Zeilen im Bericht unter "HN-Nachbarschaftsmeldungen"
zusammen: empfangene Berichte je Sender (Anzahl, vollstaendig/gekuerzt, Median-`<k>`), angewendete
Kanten je `(<x>, <m>)` mit Median-SNR, `self`-/`norow`-Zaehler, selbst gesendete Berichte (Anzahl,
Median-Laenge), und `VETO`-Zaehler je `(<x>, <m>)`.

## Beispiel

```
[NBR]|CUT|132|4|2|OE1AAA-1,OE1BBB-2,OE3CCC-3,DK5EN-93
[NBR]|EDGE|132|OE3CCC-3|DK5EN-93|T|0|7|-9
[NBR]|ME|132|DK5EN-93|T|-91|41|-11
[NBR]|SNAP|135|DK5EN-98|9|21|24
[NBR]|ROW|135|0|DK5EN-98|1|0|3|UNK|NA
[NBR]|ROW|135|1|DK5EN-93|5|2|1|EXCL|MESH
[NBR]|ENDSNAP|135
[NBR]|SYM|136|1A2B3C4D|HASF|DK5EN-95|DK5EN-93|-10
[NBR]|NEED|136|1A2B3C4D|T|B|00000002|00000000|1|00000020
[NBR]|CANCEL?|137|1A2B3C4D|T|DK5EN-93|00000006|00000000|00000020
[NBR]|RPTTX|300|58|R5;N5;DL2JA-2,7;DB0ISM-1,5;DK5EN-98,-8;DB0ED-99,-11;DL2UD-1,-12;
[NBR]|RPT|315|DL2JA-2|DB0ISM-1|6|ok
[NBR]|RPT|315|DL2JA-2|DK5EN-98|-9|self
[NBR]|RPT|315|DL2JA-2|OE9ZZZ-9|-14|norow
[NBR]|RPTSUM|315|DL2JA-2|3|3|1|1
[NBR]|SYM|316|2B3C4D5E|VETO|DK5EN-95|DK5EN-93|-11
[NBR]|DROP|317|RPT|OE1XXX-1
```

Aeltere Mitschnitte ohne `<snr>` (bei `EDGE`/`ME`) und ohne `<inferred>` (bei `NEED`/`CANCEL?`/
`CANCEL`) bleiben gueltig -- siehe die Ruckwaertskompatibilitaets-Hinweise oben je Feld. `RPT`,
`RPTSUM`, `RPTTX`, `DROP|RPT` und `SYM|...|VETO` (Stufe 3) fehlen in jedem Mitschnitt vor dieser
Aenderung vollstaendig -- kein Feld, keine Zeile, kein Ersatz.
