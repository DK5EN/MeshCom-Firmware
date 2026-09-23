# NBR-Dauertest -- Auswertung

> **ARCHIVED 2026-09-23.** Nachtlauf mit `--nbrsym` in `count` (22.09. 23:12 bis 23.09. 07:38). Abgeloest durch den `on`-Betrieb (Welle 4-6, `../nbr-stage2-campaign.md`). Enthaelt noch Text-Kanten aus Server-Einspeisung.

## BLUF

- Zeitraum 2026-09-22 23:12:02.795 bis 2026-09-23 07:38:02.556, 5 direkte Nachbarn: 2 eigenes Urteil **exklusiv** (muessen selbst meshen, == Firmware-`MESH`), 3 **redundant** (== `RED`).
- Eigenes Urteil und Firmware-`<meshneed>` stimmen ueberall dort ueberein, wo verglichen werden konnte (5 von 5 Nachbarn).
- **ACHTUNG: Tabellenueberlauf erkannt** (29 Snapshot(s) mit rows == maxrows) -- das Urteil aus Abschnitt 4 ist fuer diese Zeitpunkte NICHT haltbar, weil die Matrix nicht mehr alle 2-Hop-Nachbarn hielt.
- 75 EVICT-Ereignisse insgesamt -- Tabellendruck ist real.
- 491 `--nbrsym`-Symmetrie-Annahme(n) haben eine Stufe-2-Relay-Entscheidung veraendert -- Aufschluesselung in Abschnitt 10.

## 1. Rahmen

- Zeitraum: 2026-09-22 23:12:02.795 bis 2026-09-23 07:38:02.556
- Ausgewertete Dateien: /private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/232134e1-d55b-4ed7-ab3b-e14c35f025a7/scratchpad/run3/w.log
- Zeilen gesamt: 96742
- [NBR]-Zeilen: 7861
- Verworfene Zeilen: 88881
- Reboots: 0
- Mitschnitt-Luecken (> 2 min): 0
- Eigenes Rufzeichen (aus SNAP): DK5EN-98

Verworfen nach Grund:

| Grund                   | Anzahl |
| ----------------------- | ------ |
| endsnap_without_snap    | 3      |
| foreign_line            | 88093  |
| garbled_prefix          | 3      |
| row_without_snap        | 63     |
| unknown_subtype:CANCEL? | 97     |
| unknown_subtype:NEED    | 616    |
| unknown_subtype:REFUSE  | 6      |

## 2. Nachbarschaft

5 direkte Nachbarn (Quelle: ME-Zeilen).

| Rufzeichen | Anzahl | Typen                          | RSSI median | RSSI min | RSSI max | RSSI ohne Bericht | SNR median | SNR min | SNR max | SNR ohne Bericht | erste Sichtung          | letzte Sichtung         |
| ---------- | ------ | ------------------------------ | ----------- | -------- | -------- | ----------------- | ---------- | ------- | ------- | ---------------- | ----------------------- | ----------------------- |
| DB0ED-99   | 847    | {'H': 410, 'P': 342, 'T': 95}  | -112        | -115     | -110     | 0                 | -1         | -9      | 0       | 0                | 2026-09-22 23:12:03.207 | 2026-09-23 07:37:50.580 |
| DL2JA-2    | 614    | {'P': 338, 'T': 106, 'H': 170} | -117.0      | -119     | -110     | 0                 | -5.0       | -13     | 1       | 0                | 2026-09-22 23:14:49.098 | 2026-09-23 07:37:11.466 |
| DK5EN-1    | 387    | {'H': 253, 'T': 71, 'P': 63}   | -65         | -69      | -63      | 0                 | 6          | 5       | 8       | 0                | 2026-09-22 23:13:14.277 | 2026-09-23 07:37:28.157 |
| DL2UD-1    | 217    | {'H': 98, 'T': 44, 'P': 75}    | -119        | -121     | -114     | 0                 | -7         | -13     | -3      | 0                | 2026-09-22 23:12:29.242 | 2026-09-23 07:36:28.447 |
| DL2JA-1    | 55     | {'P': 15, 'H': 40}             | -120        | -121     | -117     | 0                 | -8         | -15     | -5      | 0                | 2026-09-22 23:36:44.759 | 2026-09-23 07:38:02.478 |

## 3. Kreuzmatrix

Je direktem Nachbar, was er gehoert hat:

| Nachbar  | Anzahl gehoert | gehoert                                                                                                                                                                                                                                                                                                                                                              |
| -------- | -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DB0ED-99 | 11             | DB0FHR-12, DB0HOB-12, DB0ISM-1, DC2MAC-1, DD7MH-55, DK5EN-98, DL2JA-1, DL2JA-2, DL3NCU-1, DO7GH, OE7XWT-12                                                                                                                                                                                                                                                           |
| DK5EN-1  | 9              | DK5EN-98, DL1HRU-7, DO2QG-1, DO2QG-99, F6JON-13, HB3XTK-12, IS0QLX-11, OE1XAR-33, OE6DJG-2                                                                                                                                                                                                                                                                           |
| DL2JA-1  | 0              | (nichts)                                                                                                                                                                                                                                                                                                                                                             |
| DL2JA-2  | 36             | DB0ED-99, DB0HOB-12, DB0ISM-1, DB0MMR-12, DC2MAC-1, DD7MH-55, DF2AP-99, DF2KX-12, DF8RD-1, DG3MNF-4, DG7RJ-12, DG7RJ-8, DJ9DQ-1, DK1JZ-12, DK3ACH-12, DK5EN-98, DK8JP-12, DL1HRU-7, DL2JA-1, DL2JA-3, DL2UD-1, DL3NCU-1, DL5RAS-2, DM2FK-86, DO2QG-1, DO2QG-99, DO5DMF-99, DO7GH, F6JON-13, HB3XTK-1, HB3XTK-12, IS0QLX-11, OE1KFR-1, OE1XAR-33, OE6DJG-2, OE7XWT-12 |
| DL2UD-1  | 3              | DK5EN-98, DL2JA-1, DL2JA-2                                                                                                                                                                                                                                                                                                                                           |

Umgekehrt, wer diesen Knoten gehoert hat:

| Knoten    | gehoert von                         |
| --------- | ----------------------------------- |
| DB0ED-99  | DL2JA-2                             |
| DB0FHR-12 | DB0ED-99                            |
| DB0HOB-12 | DB0ED-99, DL2JA-2                   |
| DB0ISM-1  | DB0ED-99, DL2JA-2                   |
| DB0MMR-12 | DL2JA-2                             |
| DC2MAC-1  | DB0ED-99, DL2JA-2                   |
| DD7MH-55  | DB0ED-99, DL2JA-2                   |
| DF2AP-99  | DL2JA-2                             |
| DF2KX-12  | DL2JA-2                             |
| DF8RD-1   | DL2JA-2                             |
| DG3MNF-4  | DL2JA-2                             |
| DG7RJ-12  | DL2JA-2                             |
| DG7RJ-8   | DL2JA-2                             |
| DJ9DQ-1   | DL2JA-2                             |
| DK1JZ-12  | DL2JA-2                             |
| DK3ACH-12 | DL2JA-2                             |
| DK5EN-98  | DB0ED-99, DK5EN-1, DL2JA-2, DL2UD-1 |
| DK8JP-12  | DL2JA-2                             |
| DL1HRU-7  | DK5EN-1, DL2JA-2                    |
| DL2JA-1   | DB0ED-99, DL2JA-2, DL2UD-1          |
| DL2JA-2   | DB0ED-99, DL2UD-1                   |
| DL2JA-3   | DL2JA-2                             |
| DL2UD-1   | DL2JA-2                             |
| DL3NCU-1  | DB0ED-99, DL2JA-2                   |
| DL5RAS-2  | DL2JA-2                             |
| DM2FK-86  | DL2JA-2                             |
| DO2QG-1   | DK5EN-1, DL2JA-2                    |
| DO2QG-99  | DK5EN-1, DL2JA-2                    |
| DO5DMF-99 | DL2JA-2                             |
| DO7GH     | DB0ED-99, DL2JA-2                   |
| F6JON-13  | DK5EN-1, DL2JA-2                    |
| HB3XTK-1  | DL2JA-2                             |
| HB3XTK-12 | DK5EN-1, DL2JA-2                    |
| IS0QLX-11 | DK5EN-1, DL2JA-2                    |
| OE1KFR-1  | DL2JA-2                             |
| OE1XAR-33 | DK5EN-1, DL2JA-2                    |
| OE6DJG-2  | DK5EN-1, DL2JA-2                    |
| OE7XWT-12 | DB0ED-99, DL2JA-2                   |

## 4. Urteil: muss der Nachbar selbst meshen?

Zwei Spalten, zwei entgegengesetzte Fragen an dieselbe Kante (docs/nbr-logformat.md) -- sie duerfen nicht verwechselt werden: **Firmware-`<verdict>`** fragt, ob AUSSER MIR jemand diesen Nachbarn hoert (Sicht: Nachbar als Gehoerter); **eigenes Urteil / Firmware-`<meshneed>`** fragt, ob DIESER Nachbar Knoten hoert, die sonst niemand hoert (Sicht: Nachbar als Hoerer). Nur die zweite Spalte wird unten verglichen, `<verdict>` steht nur zur Information daneben.

| Rufzeichen | eigenes Urteil | unabgedeckte Knoten                                                                                                                                                     | Firmware-`<meshneed>` | Uebereinstimmung | Firmware-`<verdict>` (nur Info) |
| ---------- | -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- | ---------------- | ------------------------------- |
| DB0ED-99   | exklusiv       | DB0FHR-12                                                                                                                                                               | MESH                  | ja               | RED                             |
| DK5EN-1    | redundant      | -                                                                                                                                                                       | RED                   | ja               | EXCL                            |
| DL2JA-1    | redundant      | -                                                                                                                                                                       | RED                   | ja               | RED                             |
| DL2JA-2    | exklusiv       | DB0MMR-12, DF2AP-99, DF2KX-12, DF8RD-1, DG3MNF-4, DG7RJ-12, DG7RJ-8, DJ9DQ-1, DK1JZ-12, DK3ACH-12, DK8JP-12, DL2JA-3, DL5RAS-2, DM2FK-86, DO5DMF-99, HB3XTK-1, OE1KFR-1 | MESH                  | ja               | RED                             |
| DL2UD-1    | redundant      | -                                                                                                                                                                       | RED                   | ja               | RED                             |

### Abweichungen eigenes Urteil <-> Firmware-`<meshneed>`

Keine.

### Nicht vergleichbar

Keine.

## 4b. Deckungsmenge und wechselseitig redundante Gruppen

Die paarweise Rechnung aus Abschnitt 4 beantwortet nur "ist DIESER eine Nachbar verzichtbar, wenn alle anderen bleiben?". Hoeren zwei Nachbarn exakt dieselbe (sonst von niemandem gehoerte) Menge, gilt in dieser Rechnung jeder fuer sich als redundant -- schaltet man aber beide ab, fehlt die Menge. Dieser Abschnitt macht das explizit.

- Universum (von irgendeinem Nachbarn gehoert, von mir nicht direkt): 33 Knoten.
- Minimale Deckungsmenge (Greedy, **nicht beweisbar minimal** -- Set Cover ist NP-schwer, das ist eine obere Schranke): DB0ED-99, DL2JA-2 (2 Nachbar(n)).

### Wechselseitig redundante Gruppen

Keine.

## 5. Stabilitaet des Urteils ueber die Zeit

| Rufzeichen | Verlauf                                                                                                                                                                                                                                      | Anzahl Snapshots | stabil ab Snapshot | letztes Urteil |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------- | ------------------ | -------------- |
| DB0ED-99   | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED                               | 30               | 1                  | RED            |
| DK5EN-1    | EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL | 30               | 1                  | EXCL           |
| DL2JA-1    | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED                                      | 29               | 1                  | RED            |
| DL2JA-2    | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED                               | 30               | 1                  | RED            |
| DL2UD-1    | EXCL -> EXCL -> EXCL -> EXCL -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED                           | 30               | 5                  | RED            |

## 6. Tabellendruck

- rows max: 21, rows median: 21.0
- EVICT gesamt: 75
- **Tabellenueberlauf in 29 Snapshot(s)** -- Urteil aus Abschnitt 4 dort nicht haltbar.

| Zeitpunkt               | up  | rows | maxrows | voll |
| ----------------------- | --- | ---- | ------- | ---- |
| 2026-09-22 23:26:52.174 | 15  | 17   | 21      |      |
| 2026-09-22 23:41:52.178 | 30  | 21   | 21      | JA   |
| 2026-09-22 23:56:52.177 | 45  | 21   | 21      | JA   |
| 2026-09-23 00:11:52.170 | 60  | 21   | 21      | JA   |
| 2026-09-23 00:26:52.169 | 75  | 21   | 21      | JA   |
| 2026-09-23 00:41:52.179 | 90  | 21   | 21      | JA   |
| 2026-09-23 00:56:52.176 | 105 | 21   | 21      | JA   |
| 2026-09-23 01:11:52.173 | 120 | 21   | 21      | JA   |
| 2026-09-23 01:26:52.200 | 135 | 21   | 21      | JA   |
| 2026-09-23 01:41:52.183 | 150 | 21   | 21      | JA   |
| 2026-09-23 01:56:52.181 | 165 | 21   | 21      | JA   |
| 2026-09-23 02:11:52.181 | 180 | 21   | 21      | JA   |
| 2026-09-23 02:26:52.186 | 195 | 21   | 21      | JA   |
| 2026-09-23 02:41:52.164 | 210 | 21   | 21      | JA   |
| 2026-09-23 02:56:52.167 | 225 | 21   | 21      | JA   |
| 2026-09-23 03:26:52.289 | 255 | 21   | 21      | JA   |
| 2026-09-23 03:41:52.202 | 270 | 21   | 21      | JA   |
| 2026-09-23 03:56:52.198 | 285 | 21   | 21      | JA   |
| 2026-09-23 04:11:52.220 | 300 | 21   | 21      | JA   |
| 2026-09-23 04:41:52.189 | 330 | 21   | 21      | JA   |
| 2026-09-23 04:56:52.193 | 345 | 21   | 21      | JA   |
| 2026-09-23 05:11:52.198 | 360 | 21   | 21      | JA   |
| 2026-09-23 05:26:52.201 | 375 | 21   | 21      | JA   |
| 2026-09-23 05:41:52.202 | 390 | 21   | 21      | JA   |
| 2026-09-23 05:56:52.198 | 405 | 21   | 21      | JA   |
| 2026-09-23 06:11:52.199 | 420 | 21   | 21      | JA   |
| 2026-09-23 06:26:52.209 | 435 | 21   | 21      | JA   |
| 2026-09-23 06:41:52.212 | 450 | 21   | 21      | JA   |
| 2026-09-23 06:56:52.215 | 465 | 21   | 21      | JA   |
| 2026-09-23 07:26:52.219 | 495 | 21   | 21      | JA   |

EVICT je Stunde:

| Stunde        | Anzahl |
| ------------- | ------ |
| 2026-09-22 23 | 2      |
| 2026-09-23 00 | 9      |
| 2026-09-23 01 | 12     |
| 2026-09-23 02 | 9      |
| 2026-09-23 03 | 3      |
| 2026-09-23 04 | 17     |
| 2026-09-23 05 | 4      |
| 2026-09-23 06 | 17     |
| 2026-09-23 07 | 2      |

Verdraengte Rufzeichen:

| Rufzeichen | Anzahl verdraengt |
| ---------- | ----------------- |
| DG3MNF-4   | 8                 |
| DC2MAC-1   | 8                 |
| DL5RAS-2   | 7                 |
| DG7RJ-12   | 7                 |
| DL2JA-3    | 5                 |
| OE7XWT-12  | 5                 |
| DL3NCU-1   | 5                 |
| DO2QG-1    | 4                 |
| DF2KX-12   | 4                 |
| DF8RD-1    | 3                 |
| DG7RJ-8    | 2                 |
| DO2QG-99   | 2                 |
| DB0MMR-12  | 2                 |
| DL1HRU-7   | 2                 |
| DO7GH      | 1                 |
| DJ9DQ-1    | 1                 |
| DB0ISM-1   | 1                 |
| DO5DMF-99  | 1                 |
| DK3ACH-12  | 1                 |
| DM2FK-86   | 1                 |
| IS0QLX-11  | 1                 |
| DK1JZ-12   | 1                 |
| OE1KFR-1   | 1                 |
| DK8JP-12   | 1                 |
| HB3XTK-12  | 1                 |

## 7. Wirkung des 2-Hop-Schnitts

- Frames gekuerzt: 1333
- Pfad-Token insgesamt verworfen: 1995

Verteilung nach ursprueglicher Pfadlaenge (`<ntok>`):

| ntok | Anzahl |
| ---- | ------ |
| 3    | 857    |
| 4    | 294    |
| 5    | 180    |
| 6    | 1      |
| 8    | 1      |

## 8. Verworfene Frames (DROP)

| Grund | Anzahl |
| ----- | ------ |
| LOOP  | 2      |

Je Stunde und Grund:

| Stunde        | LOOP |
| ------------- | ---- |
| 2026-09-23 04 | 1    |
| 2026-09-23 06 | 1    |

## 9. Positionen

| Rufzeichen | lat      | lon      | mesh | hw  | letzte Sichtung         | Entfernung (km) |
| ---------- | -------- | -------- | ---- | --- | ----------------------- | --------------- |
| DB0ED-99   | 48.286   | 12.03367 | ja   | 43  | 2026-09-23 07:15:41.821 | 25.709          |
| DB0FHR-12  | 47.86033 | 12.13183 | ja   | 9   | 2026-09-23 07:12:16.506 | 67.531          |
| DB0HOB-12  | 47.748   | 12.25    | ja   | 12  | 2026-09-23 07:20:26.800 | 82.643          |
| DB0ISM-1   | 48.22066 | 11.6665  | ja   | 43  | 2026-09-23 07:21:46.985 | 21.474          |
| DB0MMR-12  | 48.13933 | 12.66867 | ja   | 39  | 2026-09-23 07:21:22.831 | 75.069          |
| DC2MAC-1   | 48.20716 | 12.05933 | ja   | 12  | 2026-09-23 07:28:41.600 | 32.599          |
| DD7MH-55   | 48.165   | 12.61667 | ja   | 39  | 2026-09-23 07:11:35.338 | 70.397          |
| DF2KX-12   | 48.46667 | 11.92283 | ja   | 43  | 2026-09-23 05:25:22.541 | 15.124          |
| DF8RD-1    | 48.22417 | 11.683   | ja   | 43  | 2026-09-23 04:03:37.912 | 20.823          |
| DG3MNF-4   | 48.5505  | 11.86833 | ja   | 43  | 2026-09-23 06:36:40.421 | 18.546          |
| DG7RJ-12   | 48.302   | 11.6275  | ja   | 43  | 2026-09-23 04:43:59.904 | 14.323          |
| DG7RJ-8    | 48.302   | 11.62767 | ja   | 44  | 2026-09-23 06:46:26.874 | 14.316          |
| DK5EN-1    | 48.41633 | 11.75533 | ja   | 43  | 2026-09-23 07:20:32.654 | 1.59            |
| DK5EN-98   | 48.40783 | 11.738   | ja   | 43  | 2026-09-23 06:49:29.351 |                 |
| DL2JA-1    | 48.4225  | 11.7865  | ja   | 43  | 2026-09-23 07:14:00.447 | 3.934           |
| DL2JA-2    | 48.423   | 11.78667 | ja   | 43  | 2026-09-23 07:14:32.388 | 3.968           |
| DL2JA-3    | 48.535   | 11.67233 | ja   | 43  | 2026-09-23 05:29:36.262 | 14.946          |
| DL2UD-1    | 48.40883 | 11.76133 | ja   | 43  | 2026-09-23 07:14:24.506 | 1.726           |
| DL3NCU-1   | 48.3015  | 11.91433 | ja   | 3   | 2026-09-23 07:37:50.584 | 17.594          |
| DL5RAS-2   | 48.07117 | 11.68767 | ja   | 12  | 2026-09-23 06:48:01.206 | 37.62           |
| DO7GH      | 47.97733 | 11.96    | ja   | 51  | 2026-09-23 07:13:11.596 | 50.619          |
| OE7XWT-12  | 47.57767 | 12.2095  | ja   | 42  | 2026-09-23 06:07:20.551 | 98.752          |

## 10. Symmetrie-Annahmen (SYM, `--nbrsym`)

Nur geloggte Annahmen, die das Ergebnis einer Stufe-2-Relay-Entscheidung tatsaechlich veraendert haben (docs/nbr-logformat.md) -- keine Zaehlung aller Pruefungen. `HASF`: X gilt als schon im Besitz des Frames, weil es M hoert. `ALT`: X gilt nicht als alleiniger Traeger, weil ein Versorger M angenommen wird. `COVER`: X gilt durch die gehoerte Wiederholung von M als abgedeckt.

- Annahmen insgesamt: 491

Je Rolle:

| Rolle | Anzahl |
| ----- | ------ |
| ALT   | 34     |
| COVER | 20     |
| HASF  | 437    |

Je Paar (x hoert m angenommen):

| x       | m         | Anzahl | SNR median |
| ------- | --------- | ------ | ---------- |
| DL2JA-1 | DB0ED-99  | 323    | -11        |
| DL2JA-1 | DL2JA-2   | 97     | 7          |
| DL2JA-1 | DL2UD-1   | 34     | -15.0      |
| DL2JA-1 | DB0ISM-1  | 22     | 5.0        |
| DL2JA-1 | OE7XWT-12 | 11     | -12        |
| DL2JA-1 | DL3NCU-1  | 4      | -14.5      |
