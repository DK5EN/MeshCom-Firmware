# NBR-Dauertest -- Auswertung

## BLUF

- Zeitraum 2026-09-20 00:00:00.000 bis 2026-09-20 08:00:02.000, 3 direkte Nachbarn: 1 eigenes Urteil **exklusiv** (muessen selbst meshen), 2 **redundant**.
- **1 Abweichung(en)** zwischen eigenem Urteil und dem Firmware-`<verdict>` -- Tabelle in Abschnitt 4, das ist das interessanteste Ergebnis des Tests.
- **ACHTUNG: Tabellenueberlauf erkannt** (1 Snapshot(s) mit rows == maxrows) -- das Urteil aus Abschnitt 4 ist fuer diese Zeitpunkte NICHT haltbar, weil die Matrix nicht mehr alle 2-Hop-Nachbarn hielt.
- 1 Reboot(s) im Mitschnitt erkannt (Sitzung dort getrennt).
- 2 Mitschnitt-Luecke(n) > 2 min.
- 3 EVICT-Ereignisse insgesamt -- Tabellendruck ist real.
- **1 DROP|FULL** -- Alarmsignal, die Matrix war voll und hat Frames verworfen statt sie einzutragen.

## 1. Rahmen

- Zeitraum: 2026-09-20 00:00:00.000 bis 2026-09-20 08:00:02.000
- Ausgewertete Dateien: tools/testdata/nbr/nbr_sample_24h.log
- Zeilen gesamt: 54
- [NBR]-Zeilen: 46
- Verworfene Zeilen: 8
- Reboots: 1
- Mitschnitt-Luecken (> 2 min): 2
- Eigenes Rufzeichen (aus SNAP): DK5EN-98

Verworfen nach Grund:

| Grund | Anzahl |
| --- | --- |
| foreign_line | 8 |

Reboot-Ereignisse:

| Zeitpunkt | up vorher | up nachher |
| --- | --- | --- |
| 2026-09-20 03:10:20.010 | 195 | 0 |

Luecken:

| von | bis | Dauer (s) |
| --- | --- | --- |
| 2026-09-20 00:00:07.000 | 2026-09-20 03:09:55.000 | 11388.0 |
| 2026-09-20 03:10:20.070 | 2026-09-20 08:00:00.010 | 17379.9 |

## 2. Nachbarschaft

3 direkte Nachbarn (Quelle: ME-Zeilen).

| Rufzeichen | Anzahl | Typen | RSSI median | RSSI min | RSSI max | ohne Bericht | erste Sichtung | letzte Sichtung |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| DK5EN-93 | 4 | {'T': 4} | -87.5 | -91 | -83 | 0 | 2026-09-20 00:00:05.010 | 2026-09-20 08:00:00.010 |
| DK5EN-95 | 3 | {'P': 3} | -88 | -89 | -86 | 0 | 2026-09-20 00:00:05.020 | 2026-09-20 08:00:00.020 |
| DK5EN-97 | 2 | {'H': 2} | -95 | -95 | -95 | 1 | 2026-09-20 00:00:05.030 | 2026-09-20 03:10:05.020 |

## 3. Kreuzmatrix

Je direktem Nachbar, was er gehoert hat:

| Nachbar | Anzahl gehoert | gehoert |
| --- | --- | --- |
| DK5EN-93 | 2 | OE1AAA-1, OE9ZZZ-5 |
| DK5EN-95 | 1 | OE1AAA-1 |
| DK5EN-97 | 0 | (nichts) |

Umgekehrt, wer diesen Knoten gehoert hat:

| Knoten | gehoert von |
| --- | --- |
| OE1AAA-1 | DK5EN-93, DK5EN-95 |
| OE9ZZZ-5 | DK5EN-93 |

## 4. Urteil: exklusiv oder redundant

| Rufzeichen | eigenes Urteil | exklusive Knoten | Firmware-Urteil | Snapshot up | Uebereinstimmung |
| --- | --- | --- | --- | --- | --- |
| DK5EN-93 | exklusiv | OE9ZZZ-5 | EXCL | 290 | ja |
| DK5EN-95 | redundant | - | RED | 290 | ja |
| DK5EN-97 | redundant | - | (nicht in Tabelle) |  | n/a |


### Abweichungen eigenes Urteil <-> Firmware-`<verdict>`

| Rufzeichen | eigenes Urteil | Firmware-Urteil |
| --- | --- | --- |
| DK5EN-97 | redundant | (nicht in Tabelle) |

## 5. Stabilitaet des Urteils ueber die Zeit

| Rufzeichen | Verlauf | Anzahl Snapshots | stabil ab Snapshot | letztes Urteil |
| --- | --- | --- | --- | --- |
| DK5EN-93 | EXCL -> EXCL -> EXCL | 3 | 1 | EXCL |
| DK5EN-95 | RED -> RED -> RED | 3 | 1 | RED |
| DK5EN-97 | (keine Snapshot-Daten) | 0 |  |  |

## 6. Tabellendruck

- rows max: 21, rows median: 3
- EVICT gesamt: 3
- **Tabellenueberlauf in 1 Snapshot(s)** -- Urteil aus Abschnitt 4 dort nicht haltbar.

| Zeitpunkt | up | rows | maxrows | voll |
| --- | --- | --- | --- | --- |
| 2026-09-20 00:00:06.000 | 1 | 3 | 21 |  |
| 2026-09-20 03:10:06.000 | 195 | 21 | 21 | JA |
| 2026-09-20 08:00:01.000 | 290 | 3 | 21 |  |

EVICT je Stunde:

| Stunde | Anzahl |
| --- | --- |
| 2026-09-20 03 | 3 |

Verdraengte Rufzeichen:

| Rufzeichen | Anzahl verdraengt |
| --- | --- |
| OE2QQQ-1 | 2 |
| OE6FFF-6 | 1 |

## 7. Wirkung des 2-Hop-Schnitts

- Frames gekuerzt: 2
- Pfad-Token insgesamt verworfen: 5

Verteilung nach ursprueglicher Pfadlaenge (`<ntok>`):

| ntok | Anzahl |
| --- | --- |
| 4 | 1 |
| 5 | 1 |

## 8. Verworfene Frames (DROP)

**ALARM: 1x DROP|FULL -- die Tabelle war voll.**

| Grund | Anzahl |
| --- | --- |
| FULL | 1 |
| LOOP | 1 |
| TOK | 1 |
| TYPE | 1 |

Je Stunde und Grund:

| Stunde | FULL | LOOP | TOK | TYPE |
| --- | --- | --- | --- | --- |
| 2026-09-20 00 | 0 | 0 | 1 | 0 |
| 2026-09-20 03 | 1 | 1 | 0 | 1 |

## 9. Positionen

| Rufzeichen | lat | lon | mesh | hw | letzte Sichtung | Entfernung (km) |
| --- | --- | --- | --- | --- | --- | --- |
| DK5EN-98 | 48.21 | 16.31 | ja | 9 | 2026-09-20 00:00:05.100 |  |
| OE1AAA-1 | 48.2005 | 16.3005 | nein | 43 | 2026-09-20 08:00:00.060 | 1.269 |

