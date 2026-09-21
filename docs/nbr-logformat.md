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

| Zeile                                                                   | Wann                                                                            |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| `[NBR]\|EDGE\|<up>\|<from>\|<to>\|<type>\|<rssi>\|<cnt>`                | Jede eingetragene Hoerbeziehung "`<to>` hat `<from>` gehoert".                  |
| `[NBR]\|ME\|<up>\|<from>\|<type>\|<rssi>\|<cnt>`                        | Letzter Hop von mir direkt gehoert (Zelle `[last][0]`).                         |
| `[NBR]\|CUT\|<up>\|<ntok>\|<kept>\|<path>`                              | Pfad war laenger als das 2-Hop-Fenster; `<ntok>-<kept>` Token verworfen.        |
| `[NBR]\|DROP\|<up>\|<reason>\|<path>`                                   | Frame komplett verworfen. `<reason>`: `TOK`, `LOOP`, `FULL`, `TYPE`.            |
| `[NBR]\|EVICT\|<up>\|<idx>\|<old>\|<new>`                               | Zeile `<idx>` verdraengt, `<old>` war das Opfer.                                |
| `[NBR]\|POS\|<up>\|<call>\|<lat>\|<lon>\|<mesh>\|<hw>`                  | Positionscache gefuellt (`<mesh>` 0/1, `<lat>`/`<lon>` mit 5 Nachkommastellen). |
| `[NBR]\|SNAP\|<up>\|<own>\|<rows>\|<maxrows>\|<cells>`                  | Alle 15 min, eroeffnet einen Snapshot-Block.                                    |
| `[NBR]\|ROW\|<up>\|<idx>\|<call>\|<flags>\|<age>\|<hearers>\|<verdict>` | Eine Zeile je belegter Matrixzeile, direkt nach `SNAP`.                         |
| `[NBR]\|ENDSNAP\|<up>`                                                  | Schliesst den Snapshot-Block.                                                   |

`<flags>` ist die rohe `NbrRow.flags`-Zahl (dezimal), `<age>` das Zeilenalter in Minuten,
`<hearers>` die Anzahl Knoten, die diese Zeile hoeren, `<verdict>` einer aus
`EXCL` (exklusive Blaetter, Mesh noetig), `RED` (redundant), `LEAF`, `UNK` -- die Urteile aus
Konzept 4.3.

## Beispiel

```
[NBR]|CUT|132|4|2|OE1AAA-1,OE1BBB-2,OE3CCC-3,DK5EN-93
[NBR]|EDGE|132|OE3CCC-3|DK5EN-93|T|0|7
[NBR]|ME|132|DK5EN-93|T|-91|41
[NBR]|SNAP|135|DK5EN-98|9|21|24
[NBR]|ROW|135|0|DK5EN-98|1|0|3|UNK
[NBR]|ROW|135|1|DK5EN-93|5|2|1|EXCL
[NBR]|ENDSNAP|135
```
