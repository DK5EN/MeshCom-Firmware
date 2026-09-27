# NBR-Dauertest -- Auswertung

> **ARCHIVED 2026-09-23.** Endbericht Feldlauf 1 (Firmware vor Stufe 2). Achtung: enthaelt noch die Text-Kanten, die seit `8487ea2a` nicht mehr entstehen (Server-Einspeisung durch Gateways). Stand heute: `../nbr-stage2-campaign.md`.

## BLUF

- Zeitraum 2026-09-21 00:00:00.073 bis 2026-09-22 09:22:24.595, 6 direkte Nachbarn: 3 eigenes Urteil **exklusiv** (muessen selbst meshen, == Firmware-`MESH`), 3 **redundant** (== `RED`).
- **1 Abweichung(en)** zwischen eigenem Urteil und Firmware-`<meshneed>` -- Tabelle in Abschnitt 4, das ist das interessanteste Ergebnis des Tests.
- **ACHTUNG: Tabellenueberlauf erkannt** (51 Snapshot(s) mit rows == maxrows) -- das Urteil aus Abschnitt 4 ist fuer diese Zeitpunkte NICHT haltbar, weil die Matrix nicht mehr alle 2-Hop-Nachbarn hielt.
- 1 Mitschnitt-Luecke(n) > 2 min.
- 72 EVICT-Ereignisse insgesamt -- Tabellendruck ist real.

## 1. Rahmen

- Zeitraum: 2026-09-21 00:00:00.073 bis 2026-09-22 09:22:24.595
- Ausgewertete Dateien: /private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/232134e1-d55b-4ed7-ab3b-e14c35f025a7/scratchpad/run1/cut/2026-09-21.log, /private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/232134e1-d55b-4ed7-ab3b-e14c35f025a7/scratchpad/run1/cut/2026-09-22.log
- Zeilen gesamt: 111415
- [NBR]-Zeilen: 13179
- Verworfene Zeilen: 98236
- Reboots: 0
- Mitschnitt-Luecken (> 2 min): 1
- Eigenes Rufzeichen (aus SNAP): DK5EN-98

Verworfen nach Grund:

| Grund        | Anzahl |
| ------------ | ------ |
| foreign_line | 98236  |

Luecken:

| von                     | bis                     | Dauer (s) |
| ----------------------- | ----------------------- | --------- |
| 2026-09-21 07:56:23.116 | 2026-09-21 19:20:44.217 | 41061.1   |

## 2. Nachbarschaft

6 direkte Nachbarn (Quelle: ME-Zeilen).

| Rufzeichen | Anzahl | Typen                          | RSSI median | RSSI min | RSSI max | ohne Bericht | erste Sichtung          | letzte Sichtung         |
| ---------- | ------ | ------------------------------ | ----------- | -------- | -------- | ------------ | ----------------------- | ----------------------- |
| DB0ED-99   | 1443   | {'H': 770, 'T': 248, 'P': 425} | -115        | -123     | -111     | 0            | 2026-09-21 19:20:59.111 | 2026-09-22 09:22:04.982 |
| DL2UD-1    | 1058   | {'P': 196, 'H': 605, 'T': 257} | -112.0      | -127     | -108     | 0            | 2026-09-21 19:21:25.231 | 2026-09-22 09:21:46.647 |
| DL2JA-2    | 1022   | {'H': 352, 'T': 246, 'P': 424} | -118.0      | -127     | -110     | 0            | 2026-09-21 19:21:37.203 | 2026-09-22 09:21:40.013 |
| DL2JA-1    | 85     | {'P': 27, 'H': 58}             | -121        | -124     | -117     | 0            | 2026-09-21 19:39:47.177 | 2026-09-22 09:22:10.915 |
| DK5EN-1    | 34     | {'H': 21, 'P': 5, 'T': 8}      | -54.0       | -77      | -48      | 0            | 2026-09-21 21:10:44.319 | 2026-09-22 08:57:55.582 |
| DK5EN-92   | 1      | {'H': 1}                       | -63         | -63      | -63      | 0            | 2026-09-21 21:22:42.964 | 2026-09-21 21:22:42.964 |

## 3. Kreuzmatrix

Je direktem Nachbar, was er gehoert hat:

| Nachbar  | Anzahl gehoert | gehoert                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| -------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DB0ED-99 | 10             | DB0FHR-12, DB0HOB-12, DB0ISM-1, DC2MAC-1, DD7MH-55, DK4YU-77, DK5EN-98, DL2JA-1, DL2JA-2, DL3NCU-1                                                                                                                                                                                                                                                                                                                                                                                                                  |
| DK5EN-1  | 5              | DK5EN-98, DL2JA-2, IS0QLX-11, OE1XAR-33, SP6TSE-7                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| DK5EN-92 | 0              | (nichts)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| DL2JA-1  | 0              | (nichts)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| DL2JA-2  | 51             | DA6CH-10, DB0ED-99, DB0HOB-12, DB0ISM-1, DB0MMR-12, DB1PB-5, DC2JR-2, DC2MAC-1, DD1GR-2, DD7MH-55, DF2AP-99, DF2KX-12, DF4ND-99, DF8RD-1, DG2NPE-10, DG3MNF-4, DG7RJ-8, DH1IS-12, DH6MAV-12, DJ9DQ-1, DJ9DQ-11, DK4DO-1, DK4YU-77, DK5EN-98, DL2JA-1, DL2JA-3, DL2UD-1, DL3NCU-1, DL5AZZ-99, DL5RAS-2, DL9CL-9, DL9UW-01, DM3KS-12, DM3KS-7, DM5SR-13, DM6CS-12, DO1AU-1, DO1PIT-10, DO2QG-1, DO5DMF-99, DO5NE-1, DO7GH, HB9VQQ-1, IS0HXK-7, OE1XAR-33, OE3LCR-55, OE5HWN-6, OE6DJG-2, OE7XWT-12, SP3GJ-9, SP6TSE-7 |
| DL2UD-1  | 3              | DK5EN-98, DL2JA-1, DL2JA-2                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |

Umgekehrt, wer diesen Knoten gehoert hat:

| Knoten    | gehoert von                         |
| --------- | ----------------------------------- |
| DA6CH-10  | DL2JA-2                             |
| DB0ED-99  | DL2JA-2                             |
| DB0FHR-12 | DB0ED-99                            |
| DB0HOB-12 | DB0ED-99, DL2JA-2                   |
| DB0ISM-1  | DB0ED-99, DL2JA-2                   |
| DB0MMR-12 | DL2JA-2                             |
| DB1PB-5   | DL2JA-2                             |
| DC2JR-2   | DL2JA-2                             |
| DC2MAC-1  | DB0ED-99, DL2JA-2                   |
| DD1GR-2   | DL2JA-2                             |
| DD7MH-55  | DB0ED-99, DL2JA-2                   |
| DF2AP-99  | DL2JA-2                             |
| DF2KX-12  | DL2JA-2                             |
| DF4ND-99  | DL2JA-2                             |
| DF8RD-1   | DL2JA-2                             |
| DG2NPE-10 | DL2JA-2                             |
| DG3MNF-4  | DL2JA-2                             |
| DG7RJ-8   | DL2JA-2                             |
| DH1IS-12  | DL2JA-2                             |
| DH6MAV-12 | DL2JA-2                             |
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
| DL9CL-9   | DL2JA-2                             |
| DL9UW-01  | DL2JA-2                             |
| DM3KS-12  | DL2JA-2                             |
| DM3KS-7   | DL2JA-2                             |
| DM5SR-13  | DL2JA-2                             |
| DM6CS-12  | DL2JA-2                             |
| DO1AU-1   | DL2JA-2                             |
| DO1PIT-10 | DL2JA-2                             |
| DO2QG-1   | DL2JA-2                             |
| DO5DMF-99 | DL2JA-2                             |
| DO5NE-1   | DL2JA-2                             |
| DO7GH     | DL2JA-2                             |
| HB9VQQ-1  | DL2JA-2                             |
| IS0HXK-7  | DL2JA-2                             |
| IS0QLX-11 | DK5EN-1                             |
| OE1XAR-33 | DK5EN-1, DL2JA-2                    |
| OE3LCR-55 | DL2JA-2                             |
| OE5HWN-6  | DL2JA-2                             |
| OE6DJG-2  | DL2JA-2                             |
| OE7XWT-12 | DL2JA-2                             |
| SP3GJ-9   | DL2JA-2                             |
| SP6TSE-7  | DK5EN-1, DL2JA-2                    |

## 4. Urteil: muss der Nachbar selbst meshen?

Zwei Spalten, zwei entgegengesetzte Fragen an dieselbe Kante (docs/nbr-logformat.md) -- sie duerfen nicht verwechselt werden: **Firmware-`<verdict>`** fragt, ob AUSSER MIR jemand diesen Nachbarn hoert (Sicht: Nachbar als Gehoerter); **eigenes Urteil / Firmware-`<meshneed>`** fragt, ob DIESER Nachbar Knoten hoert, die sonst niemand hoert (Sicht: Nachbar als Hoerer). Nur die zweite Spalte wird unten verglichen, `<verdict>` steht nur zur Information daneben.

| Rufzeichen | eigenes Urteil | unabgedeckte Knoten                                                                                                                                                                                                                                                                                                                                                                         | Firmware-`<meshneed>` | Uebereinstimmung | Firmware-`<verdict>` (nur Info) |
| ---------- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- | ---------------- | ------------------------------- |
| DB0ED-99   | exklusiv       | DB0FHR-12                                                                                                                                                                                                                                                                                                                                                                                   | MESH                  | ja               | RED                             |
| DK5EN-1    | exklusiv       | IS0QLX-11                                                                                                                                                                                                                                                                                                                                                                                   | RED                   | nein             | EXCL                            |
| DK5EN-92   | redundant      | -                                                                                                                                                                                                                                                                                                                                                                                           | RED                   | ja               | EXCL                            |
| DL2JA-1    | redundant      | -                                                                                                                                                                                                                                                                                                                                                                                           | RED                   | ja               | RED                             |
| DL2JA-2    | exklusiv       | DA6CH-10, DB0MMR-12, DB1PB-5, DC2JR-2, DD1GR-2, DF2AP-99, DF2KX-12, DF4ND-99, DF8RD-1, DG2NPE-10, DG3MNF-4, DG7RJ-8, DH1IS-12, DH6MAV-12, DJ9DQ-1, DJ9DQ-11, DK4DO-1, DL2JA-3, DL5AZZ-99, DL5RAS-2, DL9CL-9, DL9UW-01, DM3KS-12, DM3KS-7, DM5SR-13, DM6CS-12, DO1AU-1, DO1PIT-10, DO2QG-1, DO5DMF-99, DO5NE-1, DO7GH, HB9VQQ-1, IS0HXK-7, OE3LCR-55, OE5HWN-6, OE6DJG-2, OE7XWT-12, SP3GJ-9 | MESH                  | ja               | RED                             |
| DL2UD-1    | redundant      | -                                                                                                                                                                                                                                                                                                                                                                                           | RED                   | ja               | RED                             |

### Abweichungen eigenes Urteil <-> Firmware-`<meshneed>`

| Rufzeichen | eigenes Urteil | Firmware-`<meshneed>` |
| ---------- | -------------- | --------------------- |
| DK5EN-1    | exklusiv       | RED                   |

### Nicht vergleichbar

Keine.

## 4b. Deckungsmenge und wechselseitig redundante Gruppen

Die paarweise Rechnung aus Abschnitt 4 beantwortet nur "ist DIESER eine Nachbar verzichtbar, wenn alle anderen bleiben?". Hoeren zwei Nachbarn exakt dieselbe (sonst von niemandem gehoerte) Menge, gilt in dieser Rechnung jeder fuer sich als redundant -- schaltet man aber beide ab, fehlt die Menge. Dieser Abschnitt macht das explizit.

- Universum (von irgendeinem Nachbarn gehoert, von mir nicht direkt): 49 Knoten.
- Minimale Deckungsmenge (Greedy, **nicht beweisbar minimal** -- Set Cover ist NP-schwer, das ist eine obere Schranke): DB0ED-99, DK5EN-1, DL2JA-2 (3 Nachbar(n)).

### Wechselseitig redundante Gruppen

Keine.

## 5. Stabilitaet des Urteils ueber die Zeit

| Rufzeichen | Verlauf                                                                                                                                                                                                                                                                                                                                                                                  | Anzahl Snapshots | stabil ab Snapshot | letztes Urteil |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------- | ------------------ | -------------- |
| DB0ED-99   | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED   | 54               | 1                  | RED            |
| DK5EN-1    | EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL -> EXCL                                                                                                                                                                                                                                                     | 17               | 1                  | EXCL           |
| DK5EN-92   | EXCL -> EXCL -> EXCL -> EXCL                                                                                                                                                                                                                                                                                                                                                             | 4                | 1                  | EXCL           |
| DL2JA-1    | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED          | 53               | 1                  | RED            |
| DL2JA-2    | RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED   | 54               | 1                  | RED            |
| DL2UD-1    | EXCL -> EXCL -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED -> RED | 54               | 3                  | RED            |

## 6. Tabellendruck

- rows max: 21, rows median: 21.0
- EVICT gesamt: 72
- **Tabellenueberlauf in 51 Snapshot(s)** -- Urteil aus Abschnitt 4 dort nicht haltbar.

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
| 2026-09-22 07:27:07.455 | 728 | 21   | 21      | JA   |
| 2026-09-22 07:42:07.462 | 743 | 21   | 21      | JA   |
| 2026-09-22 07:57:07.566 | 758 | 21   | 21      | JA   |
| 2026-09-22 08:12:07.462 | 773 | 21   | 21      | JA   |
| 2026-09-22 08:27:07.459 | 788 | 21   | 21      | JA   |
| 2026-09-22 08:42:07.462 | 803 | 21   | 21      | JA   |
| 2026-09-22 08:57:07.436 | 818 | 21   | 21      | JA   |
| 2026-09-22 09:12:07.440 | 833 | 21   | 21      | JA   |

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
| 2026-09-22 07 | 4      |
| 2026-09-22 08 | 7      |
| 2026-09-22 09 | 5      |

Verdraengte Rufzeichen:

| Rufzeichen | Anzahl verdraengt |
| ---------- | ----------------- |
| DL2JA-3    | 6                 |
| DC2MAC-1   | 5                 |
| DG2NPE-10  | 4                 |
| DL5RAS-2   | 4                 |
| DF8RD-1    | 4                 |
| DO1PIT-10  | 4                 |
| DB0MMR-12  | 3                 |
| SP3GJ-9    | 2                 |
| DM3KS-12   | 2                 |
| DL9UW-01   | 2                 |
| DO2QG-1    | 2                 |
| DG3MNF-4   | 2                 |
| DM6CS-12   | 2                 |
| DO7GH      | 2                 |
| DF2KX-12   | 1                 |
| OE5HWN-6   | 1                 |
| DK4DO-1    | 1                 |
| DO5NE-1    | 1                 |
| DK5EN-92   | 1                 |
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
| DJ9DQ-11   | 1                 |
| DJ9DQ-1    | 1                 |
| OE3LCR-55  | 1                 |
| OE6DJG-2   | 1                 |
| DF2AP-99   | 1                 |
| DO1AU-1    | 1                 |
| DA6CH-10   | 1                 |
| DB1PB-5    | 1                 |
| IS0HXK-7   | 1                 |
| DO5DMF-99  | 1                 |
| HB9VQQ-1   | 1                 |
| DF4ND-99   | 1                 |
| DH6MAV-12  | 1                 |

## 7. Wirkung des 2-Hop-Schnitts

- Frames gekuerzt: 2213
- Pfad-Token insgesamt verworfen: 3276

Verteilung nach ursprueglicher Pfadlaenge (`<ntok>`):

| ntok | Anzahl |
| ---- | ------ |
| 3    | 1440   |
| 4    | 493    |
| 5    | 270    |
| 6    | 10     |

## 8. Verworfene Frames (DROP)

_keine Daten_

Je Stunde und Grund:

_keine Daten_

## 9. Positionen

| Rufzeichen | lat      | lon      | mesh | hw  | letzte Sichtung         | Entfernung (km) |
| ---------- | -------- | -------- | ---- | --- | ----------------------- | --------------- |
| DB0ED-99   | 48.286   | 12.03367 | ja   | 43  | 2026-09-22 08:57:21.709 | 25.709          |
| DB0FHR-12  | 47.86033 | 12.13183 | ja   | 9   | 2026-09-22 08:50:24.197 | 67.531          |
| DB0HOB-12  | 47.748   | 12.25    | ja   | 12  | 2026-09-22 08:29:51.417 | 82.643          |
| DB0ISM-1   | 48.22066 | 11.6665  | ja   | 43  | 2026-09-22 09:01:40.351 | 21.474          |
| DB0MMR-12  | 48.13933 | 12.66867 | ja   | 39  | 2026-09-22 08:34:35.647 | 75.069          |
| DC2MAC-1   | 48.20716 | 12.05933 | ja   | 12  | 2026-09-22 09:00:01.865 | 32.599          |
| DD7MH-55   | 48.165   | 12.61667 | ja   | 39  | 2026-09-22 09:20:22.457 | 70.397          |
| DF2KX-12   | 48.46667 | 11.92283 | ja   | 43  | 2026-09-22 08:57:54.524 | 15.124          |
| DF8RD-1    | 48.22417 | 11.683   | ja   | 43  | 2026-09-22 06:46:45.265 | 20.823          |
| DG3MNF-4   | 48.5505  | 11.86833 | ja   | 43  | 2026-09-22 07:47:24.806 | 18.546          |
| DG7RJ-8    | 48.302   | 11.62767 | ja   | 44  | 2026-09-22 07:50:02.600 | 14.316          |
| DK4YU-77   | 48.46783 | 11.94467 | ja   | 39  | 2026-09-21 22:05:10.125 | 16.642          |
| DK5EN-1    | 48.41633 | 11.75533 | ja   | 43  | 2026-09-22 08:58:32.439 | 1.59            |
| DK5EN-98   | 48.40783 | 11.738   | ja   | 43  | 2026-09-22 09:04:54.709 |                 |
| DL2JA-1    | 48.4225  | 11.7865  | nein | 43  | 2026-09-22 09:22:10.915 | 3.934           |
| DL2JA-2    | 48.423   | 11.78667 | ja   | 43  | 2026-09-22 08:56:31.532 | 3.968           |
| DL2JA-3    | 48.535   | 11.67233 | ja   | 43  | 2026-09-22 06:49:26.142 | 14.946          |
| DL2UD-1    | 48.40883 | 11.76133 | ja   | 43  | 2026-09-22 09:03:32.067 | 1.726           |
| DL3NCU-1   | 48.3015  | 11.91433 | ja   | 3   | 2026-09-22 09:19:55.645 | 17.594          |
| DL5AZZ-99  | 48.17333 | 11.63517 | ja   | 43  | 2026-09-22 01:23:29.209 | 27.162          |
| DL5RAS-2   | 48.0715  | 11.68767 | ja   | 12  | 2026-09-22 07:39:32.524 | 37.584          |
| DM6CS-12   | 48.2455  | 11.36933 | ja   | 3   | 2026-09-22 04:18:53.763 | 32.691          |
| DO1PIT-10  | 48.14333 | 11.49    | ja   | 46  | 2026-09-22 01:23:31.256 | 34.668          |
| DO7GH      | 47.9775  | 11.96017 | ja   | 51  | 2026-09-22 05:49:52.439 | 50.605          |
| OE7XWT-12  | 47.57767 | 12.2095  | ja   | 42  | 2026-09-22 09:20:15.532 | 98.752          |
