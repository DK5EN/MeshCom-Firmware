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
| `[NBR]\|DROP\|<up>\|<reason>\|<path>`                                               | Frame komplett verworfen. `<reason>`: `TOK`, `LOOP`, `FULL`, `TYPE`.                                                                                                                                                                                                                                          |
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

Neuer Konsolenschalter, persistiert, Default AN (`node_sset4`-Bit `0x0080`, invertiert
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
stuetzt.

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
```

Aeltere Mitschnitte ohne `<snr>` (bei `EDGE`/`ME`) und ohne `<inferred>` (bei `NEED`/`CANCEL?`/
`CANCEL`) bleiben gueltig -- siehe die Ruckwaertskompatibilitaets-Hinweise oben je Feld.
