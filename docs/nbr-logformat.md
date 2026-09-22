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
`<rssi>` ist dBm als negative Ganzzahl, `0` heisst "kein Bericht".

## Zeilen

| Zeile                                                                               | Wann                                                                            |
| ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| `[NBR]\|EDGE\|<up>\|<from>\|<to>\|<type>\|<rssi>\|<cnt>`                            | Jede eingetragene Hoerbeziehung "`<to>` hat `<from>` gehoert".                  |
| `[NBR]\|ME\|<up>\|<from>\|<type>\|<rssi>\|<cnt>`                                    | Letzter Hop von mir direkt gehoert (Zelle `[last][0]`).                         |
| `[NBR]\|CUT\|<up>\|<ntok>\|<kept>\|<path>`                                          | Pfad war laenger als das 2-Hop-Fenster; `<ntok>-<kept>` Token verworfen.        |
| `[NBR]\|DROP\|<up>\|<reason>\|<path>`                                               | Frame komplett verworfen. `<reason>`: `TOK`, `LOOP`, `FULL`, `TYPE`.            |
| `[NBR]\|EVICT\|<up>\|<idx>\|<old>\|<new>`                                           | Zeile `<idx>` verdraengt, `<old>` war das Opfer.                                |
| `[NBR]\|POS\|<up>\|<call>\|<lat>\|<lon>\|<mesh>\|<hw>`                              | Positionscache gefuellt (`<mesh>` 0/1, `<lat>`/`<lon>` mit 5 Nachkommastellen). |
| `[NBR]\|SNAP\|<up>\|<own>\|<rows>\|<maxrows>\|<cells>`                              | Alle 15 min, eroeffnet einen Snapshot-Block.                                    |
| `[NBR]\|ROW\|<up>\|<idx>\|<call>\|<flags>\|<age>\|<hearers>\|<verdict>\|<meshneed>` | Eine Zeile je belegter Matrixzeile, direkt nach `SNAP`.                         |
| `[NBR]\|ENDSNAP\|<up>`                                                              | Schliesst den Snapshot-Block.                                                   |

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

Diese Zeilen kommen nur mit `--nbrdebug on` UND `--nbrrelay count` oder `on`. `<need>` und
`<alone>` sind Bitmasken ueber Zeilenindizes (Bit i = Zeile i der Matrix), hexadezimal mit acht
Stellen; `<msg_id>` ebenso. `<typ>` wie oben, `<relayer>` ist der letzte Hop der gehoerten fremden
Wiederholung.

| Zeile                                                                    | Wann                                                                                                                                                                                                       |
| ------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[NBR]\|NEED\|<up>\|<msg_id>\|<typ>\|<A\|B\|U>\|<need>\|<alone>\|<slot>` | Relay eingereiht; `A` heisst `alone != 0` (nie Abbruch), `B` heisst Nachrang und Abbruch moeglich, `U` heisst kein Wissen (leere Matrix, ungueltiger Pfad): Relay laeuft wie heute, zaehlt in keinem Fall. |
| `[NBR]\|CANCEL?\|<up>\|<msg_id>\|<typ>\|<relayer>\|<vorher>\|<nachher>`  | `count`: die gehoerte Wiederholung von `<relayer>` deckte den Restbedarf; nichts passiert. Einmal je Slot.                                                                                                 |
| `[NBR]\|CANCEL\|<up>\|<msg_id>\|<typ>\|<relayer>\|<vorher>\|<nachher>`   | `on`: dasselbe, und der Ring-Slot wurde freigegeben.                                                                                                                                                       |
| `[NBR]\|REFUSE\|<up>\|<msg_id>\|<typ>\|<relayer>\|<alone>`               | Fremde Wiederholung gehoert, aber `alone != 0`: Abbruch verweigert. Einmal je Slot.                                                                                                                        |

`<vorher>`/`<nachher>` sind die Bedarfsmaske vor und nach Abzug der Hoerer des Relayers. Die
Zaehler dazu stehen in `--info` (`...NBRRELAY <off|count|on> ...relays A <n> B <n> ...cancelled
<n> ...possible <n> ...refused <n>`).

## Beispiel

```
[NBR]|CUT|132|4|2|OE1AAA-1,OE1BBB-2,OE3CCC-3,DK5EN-93
[NBR]|EDGE|132|OE3CCC-3|DK5EN-93|T|0|7
[NBR]|ME|132|DK5EN-93|T|-91|41
[NBR]|SNAP|135|DK5EN-98|9|21|24
[NBR]|ROW|135|0|DK5EN-98|1|0|3|UNK|NA
[NBR]|ROW|135|1|DK5EN-93|5|2|1|EXCL|MESH
[NBR]|ENDSNAP|135
```
