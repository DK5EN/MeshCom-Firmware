# NBR-Dauertest -- Auswertung

> **ARCHIVED 2026-09-23.** Zwischenstand von Feldlauf 1 (bis 07:17), abgeloest durch den Endbericht `nbr-feldlauf-20260922.md` daneben.

## BLUF

- Zeitraum 2026-09-21 00:00:00.073 bis 2026-09-22 07:17:31.166, 6 direkte Nachbarn: 3 eigenes Urteil **exklusiv** (muessen selbst meshen, == Firmware-`MESH`), 3 **redundant** (== `RED`).
- **1 Abweichung(en)** zwischen eigenem Urteil und Firmware-`<meshneed>` -- Tabelle in Abschnitt 4, das ist das interessanteste Ergebnis des Tests.
- **ACHTUNG: Tabellenueberlauf erkannt** (43 Snapshot(s) mit rows == maxrows) -- das Urteil aus Abschnitt 4 ist fuer diese Zeitpunkte NICHT haltbar, weil die Matrix nicht mehr alle 2-Hop-Nachbarn hielt.
- 1 Mitschnitt-Luecke(n) > 2 min.
- 56 EVICT-Ereignisse insgesamt -- Tabellendruck ist real.

## 1. Rahmen

- Zeitraum: 2026-09-21 00:00:00.073 bis 2026-09-22 07:17:31.166
- Ausgewertete Dateien: /private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/49fc2bae-f84c-4432-bf0f-0b9e98ebf65b/scratchpad/2026-09-21.log, /private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/49fc2bae-f84c-4432-bf0f-0b9e98ebf65b/scratchpad/2026-09-22.log
- Zeilen gesamt: 107214
- [NBR]-Zeilen: 11212
- Verworfene Zeilen: 96002
- Reboots: 0
- Mitschnitt-Luecken (> 2 min): 1
- Eigenes Rufzeichen (aus SNAP): DK5EN-98

Verworfen nach Grund:

| Grund        | Anzahl |
| ------------ | ------ |
| foreign_line | 96002  |

Luecken:

| von                     | bis                     | Dauer (s) |
| ----------------------- | ----------------------- | --------- |
| 2026-09-21 07:56:23.116 | 2026-09-21 19:20:44.217 | 41061.1   |

## 2. Nachbarschaft

6 direkte Nachbarn (Quelle: ME-Zeilen).

| Rufzeichen | Anzahl | Typen                          | RSSI median | RSSI min | RSSI max | ohne Bericht | erste Sichtung          | letzte Sichtung         |
| ---------- | ------ | ------------------------------ | ----------- | -------- | -------- | ------------ | ----------------------- | ----------------------- |
| DB0ED-99   | 1232   | {'H': 641, 'T': 214, 'P': 377} | -114.0      | -123     | -111     | 0            | 2026-09-21 19:20:59.111 | 2026-09-22 07:17:10.455 |
| DL2UD-1    | 893    | {'P': 171, 'H': 494, 'T': 228} | -112        | -120     | -108     | 0            | 2026-09-21 19:21:25.231 | 2026-09-22 07:17:27.976 |
| DL2JA-2    | 889    | {'H': 292, 'T': 209, 'P': 388} | -118        | -127     | -110     | 0            | 2026-09-21 19:21:37.203 | 2026-09-22 07:16:34.229 |
| DL2JA-1    | 71     | {'P': 22, 'H': 49}             | -121        | -124     | -117     | 0            | 2026-09-21 19:39:47.177 | 2026-09-22 06:49:44.154 |
| DK5EN-1    | 26     | {'H': 15, 'P': 3, 'T': 8}      | -57.0       | -77      | -48      | 0            | 2026-09-21 21:10:44.319 | 2026-09-21 23:00:54.090 |
| DK5EN-92   | 1      | {'H': 1}                       | -63         | -63      | -63      | 0            | 2026-09-21 21:22:42.964 | 2026-09-21 21:22:42.964 |

## 3. Kreuzmatrix

Je direktem Nachbar, was er gehoert hat:

| Nachbar  | Anzahl gehoert | gehoert                                                                                                                                                                                                                                                                                                                                                                                                         |
| -------- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DB0ED-99 | 10             | DB0FHR-12, DB0HOB-12, DB0ISM-1, DC2MAC-1, DD7MH-55, DK4YU-77, DK5EN-98, DL2JA-1, DL2JA-2, DL3NCU-1                                                                                                                                                                                                                                                                                                              |
| DK5EN-1  | 5              | DK5EN-98, DL2JA-2, IS0QLX-11, OE1XAR-33, SP6TSE-7                                                                                                                                                                                                                                                                                                                                                               |
| DK5EN-92 | 0              | (nichts)                                                                                                                                                                                                                                                                                                                                                                                                        |
| DL2JA-1  | 0              | (nichts)                                                                                                                                                                                                                                                                                                                                                                                                        |
| DL2JA-2  | 41             | DA6CH-10, DB0ED-99, DB0HOB-12, DB0ISM-1, DB0MMR-12, DC2MAC-1, DD1GR-2, DD7MH-55, DF2AP-99, DF2KX-12, DF8RD-1, DG2NPE-10, DG3MNF-4, DG7RJ-8, DH1IS-12, DJ9DQ-1, DJ9DQ-11, DK4DO-1, DK4YU-77, DK5EN-98, DL2JA-1, DL2JA-3, DL2UD-1, DL3NCU-1, DL5AZZ-99, DL5RAS-2, DL9UW-01, DM3KS-12, DM3KS-7, DM6CS-12, DO1AU-1, DO1PIT-10, DO2QG-1, DO5NE-1, DO7GH, OE1XAR-33, OE3LCR-55, OE5HWN-6, OE6DJG-2, SP3GJ-9, SP6TSE-7 |
| DL2UD-1  | 3              | DK5EN-98, DL2JA-1, DL2JA-2                                                                                                                                                                                                                                                                                                                                                                                      |

Umgekehrt, wer diesen Knoten gehoert hat:

| Knoten    | gehoert von                         |
| --------- | ----------------------------------- |
| DA6CH-10  | DL2JA-2                             |
| DB0ED-99  | DL2JA-2                             |
| DB0FHR-12 | DB0ED-99                            |
| DB0HOB-12 | DB0ED-99, DL2JA-2                   |
| DB0ISM-1  | DB0ED-99, DL2JA-2                   |
| DB0MMR-12 | DL2JA-2                             |
| DC2MAC-1  | DB0ED-99, DL2JA-2                   |
| DD1GR-2   | DL2JA-2                             |
| DD7MH-55  | DB0ED-99, DL2JA-2                   |
| DF2AP-99  | DL2JA-2                             |
| DF2KX-12  | DL2JA-2                             |
| DF8RD-1   | DL2JA-2                             |
| DG2NPE-10 | DL2JA-2                             |
| DG3MNF-4  | DL2JA-2                             |
| DG7RJ-8   | DL2JA-2                             |
| DH1IS-12  | DL2JA-2                             |
| DJ9DQ-1   | DL2JA-2                             |
| DJ9DQ-11  | DL2JA-2                             |
| DK4DO-1   | DL2JA-2                             |
| DK4YU-77  | DB0ED-99, DL2JA-2                   |
| DK5EN-98  | DB0ED-99, DK5EN-1, DL2JA-2, DL2UD-1 |
| DL2JA-1   | DB0ED-99, DL2JA-2, DL2UD-1          |
| DL2JA-2   | DB0ED-99, DK5EN-1, DL2UD-1          |
| DL2JA-3   | DL2JA-2                             |
| DL2UD-1   | DL2JA-2                             |
| DL3NCU-1  | DB0ED-99, DL2JA-2                   |
| DL5AZZ-99 | DL2JA-2                             |
| DL5RAS-2  | DL2JA-2                             |
| DL9UW-01  | DL2JA-2                             |
| DM3KS-12  | DL2JA-2                             |
| DM3KS-7   | DL2JA-2                             |
| DM6CS-12  | DL2JA-2                             |
| DO1AU-1   | DL2JA-2                             |
| DO1PIT-10 | DL2JA-2                             |
| DO2QG-1   | DL2JA-2                             |
| DO5NE-1   | DL2JA-2                             |
| DO7GH     | DL2JA-2                             |
| IS0QLX-11 | DK5EN-1                             |
| OE1XAR-33 | DK5EN-1, DL2JA-2                    |
| OE3LCR-55 | DL2JA-2                             |
| OE5HWN-6  | DL2JA-2                             |
| OE6DJG-2  | DL2JA-2                             |
| SP3GJ-9   | DL2JA-2                             |
| SP6TSE-7  | DK5EN-1, DL2JA-2                    |

## 4. Urteil: muss der Nachbar selbst meshen?

Zwei Spalten, zwei entgegengesetzte Fragen an dieselbe Kante (docs/nbr-logformat.md) -- sie duerfen nicht verwechselt werden: **Firmware-`<verdict>`** fragt, ob AUSSER MIR jemand diesen Nachbarn hoert (Sicht: Nachbar als Gehoerter); **eigenes Urteil / Firmware-`<meshneed>`** fragt, ob DIESER Nachbar Knoten hoert, die sonst niemand hoert (Sicht: Nachbar als Hoerer). Nur die zweite Spalte wird unten verglichen, `<verdict>` steht nur zur Information daneben.

| Rufzeichen | eigenes Urteil | unabgedeckte Knoten                                                                                                                                                                                                                                                                     | Firmware-`<meshneed>` | Uebereinstimmung | Firmware-`<verdict>` (nur Info) |
| ---------- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- | ---------------- | ------------------------------- |
| DB0ED-99   | exklusiv       | DB0FHR-12                                                                                                                                                                                                                                                                               | MESH                  | ja               | RED                             |
| DK5EN-1    | exklusiv       | IS0QLX-11                                                                                                                                                                                                                                                                               | RED                   | nein             | EXCL                            |
| DK5EN-92   | redundant      | -                                                                                                                                                                                                                                                                                       | RED                   | ja               | EXCL                            |
| DL2JA-1    | redundant      | -                                                                                                                                                                                                                                                                                       | RED                   | ja               | RED                             |
| DL2JA-2    | exklusiv       | DA6CH-10, DB0MMR-12, DD1GR-2, DF2AP-99, DF2KX-12, DF8RD-1, DG2NPE-10, DG3MNF-4, DG7RJ-8, DH1IS-12, DJ9DQ-1, DJ9DQ-11, DK4DO-1, DL2JA-3, DL5AZZ-99, DL5RAS-2, DL9UW-01, DM3KS-12, DM3KS-7, DM6CS-12, DO1AU-1, DO1PIT-10, DO2QG-1, DO5NE-1, DO7GH, OE3LCR-55, OE5HWN-6, OE6DJG-2, SP3GJ-9 | MESH                  | ja               | RED                             |
| DL2UD-1    | redundant      | -                                                                                                                                                                                                                                                                                       | RED                   | ja               | RED                             |

### Abweichungen eigenes Urteil <-> Firmware-`<meshneed>`

| Rufzeichen | eigenes Urteil | Firmware-`<meshneed>` |
| ---------- | -------------- | --------------------- |
| DK5EN-1    | exklusiv       | RED                   |

### Nicht vergleichbar

Keine.

## 4b. Deckungsmenge und wechselseitig redundante Gruppen

Die paarweise Rechnung aus Abschnitt 4 beantwortet nur "ist DIESER eine Nachbar verzichtbar, wenn alle anderen bleiben?". Hoeren zwei Nachbarn exakt dieselbe (sonst von niemandem gehoerte) Menge, gilt in dieser Rechnung jeder fuer sich als redundant -- schaltet man aber beide ab, fehlt die Menge. Dieser Abschnitt macht das explizit.

- Universum (von irgendeinem Nachbarn gehoert, von mir nicht direkt): 39 Knoten.
- Minimale Deckungsmenge (Greedy, **nicht beweisbar minimal** -- Set Cover ist NP-schwer, das ist eine obere Schranke): DB0ED-99, DK5EN-1, DL2JA-2 (3 Nachbar(n)).

### Wechselseitig redundante Gruppen

Keine.

## 5. Stabilitaet des Urteils ueber die Zeit

| Rufzeichen | Verlauf                                                                                                                                                                                                                                                                                                                          | Anzahl Snapshots | stabil ab Snapshot | letztes Urteil |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------- | ------------------ | -------------- |
| DB0ED-99   | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED   | 46               | 1                  | RED            |
| DK5EN-1    | EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL                                                                                                                                                                                                                                     | 12               | 1                  | EXCL           |
| DK5EN-92   | EXCL -> EXCL -> EXCL -> EXCL                                                                                                                                                                                                                                                                                                     | 4                | 1                  | EXCL           |
| DL2JA-1    | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED          | 45               | 1                  | RED            |
| DL2JA-2    | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED   | 46               | 1                  | RED            |
| DL2UD-1    | EXCL -> EXCL -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED | 46               | 3                  | RED            |

## 6. Tabellendruck

- rows max: 21, rows median: 21.0
- EVICT gesamt: 56
- **Tabellenueberlauf in 43 Snapshot(s)** -- Urteil aus Abschnitt 4 dort nicht haltbar.

| Zeitpunkt               | up  | rows | maxrows | voll |
| ----------------------- | --- | ---- | ------- | ---- |
| 2026-09-21 19:35:49.802 | 16  | 14   | 21      |      |
| 2026-09-21 19:50:49.797 | 31  | 18   | 21      |      |
| 2026-09-21 20:05:49.793 | 46  | 18   | 21      |      |
| 2026-09-21 20:20:49.890 | 61  | 21   | 21      | JA   |
| 2026-09-21 20:35:49.788 | 76  | 21   | 21      | JA   |
| 2026-09-21 20:50:49.807 | 91  | 21   | 21      | JA   |
| 2026-09-21 21:05:49.806 | 106 | 21   | 21      | JA   |
| 2026-09-21 21:20:49.810 | 121 | 21   | 21      | JA   |
| 2026-09-21 21:35:49.915 | 136 | 21   | 21      | JA   |
| 2026-09-21 21:50:49.802 | 151 | 21   | 21      | JA   |
| 2026-09-21 22:05:49.800 | 166 | 21   | 21      | JA   |
| 2026-09-21 22:20:49.800 | 181 | 21   | 21      | JA   |
| 2026-09-21 22:35:49.914 | 196 | 21   | 21      | JA   |
| 2026-09-21 22:50:49.801 | 211 | 21   | 21      | JA   |
| 2026-09-21 23:05:50.020 | 226 | 21   | 21      | JA   |
| 2026-09-21 23:20:49.921 | 241 | 21   | 21      | JA   |
| 2026-09-21 23:35:49.937 | 256 | 21   | 21      | JA   |
| 2026-09-21 23:50:49.843 | 271 | 21   | 21      | JA   |
| 2026-09-22 00:05:49.849 | 286 | 21   | 21      | JA   |
| 2026-09-22 00:20:49.948 | 301 | 21   | 21      | JA   |
| 2026-09-22 00:35:49.828 | 316 | 21   | 21      | JA   |
| 2026-09-22 00:50:49.854 | 331 | 21   | 21      | JA   |
| 2026-09-22 01:05:49.953 | 346 | 21   | 21      | JA   |
| 2026-09-22 01:20:49.972 | 361 | 21   | 21      | JA   |
| 2026-09-22 01:35:49.781 | 376 | 21   | 21      | JA   |
| 2026-09-22 01:50:49.779 | 391 | 21   | 21      | JA   |
| 2026-09-22 02:05:49.861 | 406 | 21   | 21      | JA   |
| 2026-09-22 02:20:49.775 | 421 | 21   | 21      | JA   |
| 2026-09-22 02:35:49.861 | 436 | 21   | 21      | JA   |
| 2026-09-22 02:50:49.768 | 451 | 21   | 21      | JA   |
| 2026-09-22 03:05:49.776 | 466 | 21   | 21      | JA   |
| 2026-09-22 03:20:49.881 | 481 | 21   | 21      | JA   |
| 2026-09-22 03:35:49.781 | 496 | 21   | 21      | JA   |
| 2026-09-22 03:50:49.786 | 511 | 21   | 21      | JA   |
| 2026-09-22 04:05:49.888 | 526 | 21   | 21      | JA   |
| 2026-09-22 04:20:49.879 | 541 | 21   | 21      | JA   |
| 2026-09-22 04:35:49.881 | 556 | 21   | 21      | JA   |
| 2026-09-22 04:50:49.789 | 571 | 21   | 21      | JA   |
| 2026-09-22 05:05:49.897 | 586 | 21   | 21      | JA   |
| 2026-09-22 05:20:49.900 | 601 | 21   | 21      | JA   |
| 2026-09-22 05:57:07.513 | 638 | 21   | 21      | JA   |
| 2026-09-22 06:12:07.446 | 653 | 21   | 21      | JA   |
| 2026-09-22 06:27:07.459 | 668 | 21   | 21      | JA   |
| 2026-09-22 06:42:07.529 | 683 | 21   | 21      | JA   |
| 2026-09-22 06:57:07.446 | 698 | 21   | 21      | JA   |
| 2026-09-22 07:12:07.450 | 713 | 21   | 21      | JA   |

EVICT je Stunde:

| Stunde        | Anzahl |
| ------------- | ------ |
| 2026-09-21 20 | 2      |
| 2026-09-21 21 | 4      |
| 2026-09-21 22 | 7      |
| 2026-09-21 23 | 4      |
| 2026-09-22 00 | 4      |
| 2026-09-22 01 | 9      |
| 2026-09-22 02 | 5      |
| 2026-09-22 03 | 8      |
| 2026-09-22 04 | 3      |
| 2026-09-22 05 | 5      |
| 2026-09-22 06 | 5      |

Verdraengte Rufzeichen:

| Rufzeichen | Anzahl verdraengt |
| ---------- | ----------------- |
| DL2JA-3    | 5                 |
| DG2NPE-10  | 4                 |
| DC2MAC-1   | 4                 |
| DB0MMR-12  | 3                 |
| DL5RAS-2   | 3                 |
| DF8RD-1    | 3                 |
| DO1PIT-10  | 3                 |
| SP3GJ-9    | 2                 |
| DM3KS-12   | 2                 |
| DO2QG-1    | 2                 |
| DM6CS-12   | 2                 |
| DO7GH      | 2                 |
| DF2KX-12   | 1                 |
| OE5HWN-6   | 1                 |
| DK4DO-1    | 1                 |
| DO5NE-1    | 1                 |
| DK5EN-92   | 1                 |
| DL9UW-01   | 1                 |
| IS0QLX-11  | 1                 |
| DK4YU-77   | 1                 |
| DM3KS-7    | 1                 |
| SP6TSE-7   | 1                 |
| DH1IS-12   | 1                 |
| DK5EN-1    | 1                 |
| DD1GR-2    | 1                 |
| DD7MH-55   | 1                 |
| DG7RJ-8    | 1                 |
| DL5AZZ-99  | 1                 |
| DG3MNF-4   | 1                 |
| DJ9DQ-11   | 1                 |
| DJ9DQ-1    | 1                 |
| OE3LCR-55  | 1                 |
| OE6DJG-2   | 1                 |

## 7. Wirkung des 2-Hop-Schnitts

- Frames gekuerzt: 1875
- Pfad-Token insgesamt verworfen: 2733

Verteilung nach ursprueglicher Pfadlaenge (`<ntok>`):

| ntok | Anzahl |
| ---- | ------ |
| 3    | 1250   |
| 4    | 398    |
| 5    | 221    |
| 6    | 6      |

## 8. Verworfene Frames (DROP)

_keine Daten_

Je Stunde und Grund:

_keine Daten_

## 9. Positionen

| Rufzeichen | lat      | lon      | mesh | hw  | letzte Sichtung         | Entfernung (km) |
| ---------- | -------- | -------- | ---- | --- | ----------------------- | --------------- |
| DB0ED-99   | 48.286   | 12.03367 | ja   | 43  | 2026-09-22 06:54:00.772 | 25.709          |
| DB0FHR-12  | 47.86033 | 12.13183 | ja   | 9   | 2026-09-22 06:48:03.815 | 67.531          |
| DB0HOB-12  | 47.748   | 12.25    | ja   | 12  | 2026-09-22 06:26:02.009 | 82.643          |
| DB0ISM-1   | 48.22066 | 11.6665  | ja   | 43  | 2026-09-22 06:58:49.960 | 21.474          |
| DB0MMR-12  | 48.13933 | 12.66867 | ja   | 39  | 2026-09-22 06:27:21.178 | 75.069          |
| DC2MAC-1   | 48.20716 | 12.05933 | ja   | 12  | 2026-09-22 06:54:54.549 | 32.599          |
| DD7MH-55   | 48.165   | 12.61667 | ja   | 39  | 2026-09-22 06:48:54.090 | 70.397          |
| DF2KX-12   | 48.46667 | 11.92283 | ja   | 43  | 2026-09-22 06:44:24.783 | 15.124          |
| DF8RD-1    | 48.22417 | 11.683   | ja   | 43  | 2026-09-22 06:46:45.265 | 20.823          |
| DG3MNF-4   | 48.5505  | 11.86833 | ja   | 43  | 2026-09-22 07:16:47.625 | 18.546          |
| DG7RJ-8    | 48.302   | 11.62767 | ja   | 44  | 2026-09-22 06:48:34.832 | 14.316          |
| DK4YU-77   | 48.46783 | 11.94467 | ja   | 39  | 2026-09-21 22:05:10.125 | 16.642          |
| DK5EN-1    | 48.416   | 11.75817 | ja   | 43  | 2026-09-21 22:53:14.305 | 1.744           |
| DK5EN-98   | 48.40783 | 11.738   | ja   | 43  | 2026-09-22 07:04:21.937 |                 |
| DL2JA-1    | 48.4225  | 11.7865  | ja   | 43  | 2026-09-22 06:51:02.906 | 3.934           |
| DL2JA-2    | 48.423   | 11.78667 | ja   | 43  | 2026-09-22 06:52:42.948 | 3.968           |
| DL2JA-3    | 48.535   | 11.67233 | ja   | 43  | 2026-09-22 06:49:26.142 | 14.946          |
| DL2UD-1    | 48.40883 | 11.76133 | ja   | 43  | 2026-09-22 07:03:38.221 | 1.726           |
| DL3NCU-1   | 48.3015  | 11.91433 | ja   | 3   | 2026-09-22 07:14:10.649 | 17.594          |
| DL5AZZ-99  | 48.17333 | 11.63517 | ja   | 43  | 2026-09-22 01:23:29.209 | 27.162          |
| DL5RAS-2   | 48.07133 | 11.68717 | ja   | 12  | 2026-09-22 05:29:21.295 | 37.606          |
| DM6CS-12   | 48.2455  | 11.36933 | ja   | 3   | 2026-09-22 04:18:53.763 | 32.691          |
| DO1PIT-10  | 48.14333 | 11.49    | ja   | 46  | 2026-09-22 01:23:31.256 | 34.668          |
| DO7GH      | 47.9775  | 11.96017 | ja   | 51  | 2026-09-22 05:49:52.439 | 50.605          |
