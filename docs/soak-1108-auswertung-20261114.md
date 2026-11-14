# SOAK-1108: Auswertung des 24-Stunden-Laufs auf DK5EN-98

**Der Lauf ist sauber durchgelaufen: kein Neustart, kein Verlust im Sendering, kein hängender Sendevorgang, kein Verlust im Mitschnitt. Zwei Beobachtungen bleiben offen: Relay-Frames warten oft länger als 10 s, und weitergeleitete ACKs warten bis zu 25 s.**

- Stand: 2026-11-14
- Firmware: `v4.40a.11.08`, CI-Image, auf DK5EN-98 seit 2026-11-08 20:22
- Quelle: `~/meshlog/dk5en-98-soak-11.08-20261108/` auf rpizero, ausgewertet auf Kopien von `2026-11-08.log` und `2026-11-09.log`

## Handlungsbedarf

- **`--setlog on` ist auf DK5EN-98 noch gesetzt.** Der Logger hat nur `loradebug`, `txcapture` und `nbrdebug` zurückgestellt. Abschalten braucht dein ausdrückliches OK für DK5EN-98.
- **Zwei Beobachtungen brauchen eine Entscheidung,** ob sie ein Backlog-Eintrag werden (Abschnitt "Offene Beobachtungen").

## Kernfakten

- **Laufzeit:** 2026-11-08 20:38:29 bis 2026-11-09 20:00:05 (23,4 h), 268.346 Zeilen, 0 Reconnects. Der Logger endete planmäßig.
- **Sendepfad:** 1659 Kanalabfragen mit Ergebnis frei (`CAD_FREE`), 1659 Sendestarts (`TX_START`), 1659 Sendungen (`RADIO_TX`). Jede freie Abfrage führte zu genau einer Sendung.
- **Backoff:** 1606 Sendungen (96,8 %) gingen beim ersten Versuch hinaus, der höchste Versuch war 4. Es gab 61 `CAD_BUSY`, 60 `CAD_FALSE_POSITIVE`, keine `CAD_FREE_NO_TX` und 32 `CSMA_AGED`.
- **Sendering:** `RETRANSMIT_GIVEUP`, `RETRANSMIT_GIVEUP_DM`, `RING_DROP_STALE`, `RING_DROP_PRIO`, `RING_DROP_NEW`, `RING_OVERFLOW` und `RING_REJECT` kamen kein einziges Mal vor.
- **Mitschnitt:** `CAPTURE_DROPPED` 0. Es gibt einen Start-Banner, um 20:38:36 direkt nach `--setlog on` (RESET_REASON=3, SW). Danach keinen mehr.

## Ergebnis gegen die Watch-List

| Punkt                            | Ergebnis                                                                | Bewertung             |
| -------------------------------- | ----------------------------------------------------------------------- | --------------------- |
| CSMA-01/05 Zeitverlauf           | Kette `CAD_FREE`, `TX_START`, `RADIO_TX` lückenlos                      | bestanden             |
| CSMA-01/05 Wartezeit bis Sendung | 320 von 1659 Sendungen warteten über 10 s                               | offen, siehe unten    |
| CSMA-Backoff                     | Versuche bis 4, keine Schleife                                          | bestanden             |
| CSMA-03/04 nur nRF52/INSTRUMENT  | kein `CAD_STALE_RECHECK`, kein Suffix `free=`                           | bestanden             |
| QA-RX-03 Ringfreigabe            | alle sieben Zähler 0                                                    | bestanden             |
| RX-Pfad (BL-02)                  | `ERR_PAYLOAD` 0, `RX_ERROR` 0, `CHECKRX zero_length` 0, `missed_edge` 0 | bestanden             |
| RX-Pfad Fehlerrate               | `CRC_ERROR` 312 (13 pro Stunde), kein Vergleich mit dem Lauf von 11.06  | nicht bewertbar       |
| Dedup und Store-Echo             | keine DUP-Flut, aber 112 IDs mehrfach neu, Ursache geklärt              | kein Funktionsfehler  |
| CET-AGE-01                       | 435 `[CET]`-Zeilen, kein `skip`; Uhr gegen Anzeige nicht geprüft        | Wächter nie ausgelöst |
| REBOOT-98                        | kein neuer Start nach dem Lauf-Beginn                                   | bestanden             |

## Offene Beobachtungen

**Relay-Frames warten oft länger als 10 s**

- **Zahlen:** Wartezeit (`wait=` im `TX`-Log) über 10 s bei Prio 1: 9 von 53, Prio 2: 7 von 167, Prio 3: 83 von 442, Prio 4: 66 von 328, Prio 5: 155 von 669. Das sind 320 von 1659.
- **Prio 5:** Median 3,6 s, p95 37,2 s, Maximum 71,8 s.
- **Einordnung:** Die Watch-List nannte "über etwa 10 s von `RELAY_QUEUED` bis `RADIO_TX`" als schlecht. Für Prio 3 bis 5 ist das vermutlich das Relay-Fenster unter Last, also gewollt. Das Kriterium war für diese Klassen zu streng. Das ist eine Annahme, nicht geprüft.

**Weitergeleitete ACKs warten bis zu 25 s**

- **Zahlen:** `src=r` mischt 20 echte Relay-ACKs (Median 6,1 s, Maximum 24,8 s) mit 6 ACKs des Gateways selbst (3 IDs, je 2 Kopien, Maximum 21,9 s). `src=o` (n=27) sind eigene Textframes, keine ACKs.
- **Beispiel:** 2026-11-09 17:04. ACK `0465F0B8` ging nach 10,1 s und noch einmal nach 21,9 s hinaus, ACK `578B35E1` nach 24,8 s. Der Kanal war frei (`CAD_FREE attempt=0`), die Auslastung lag bei 9 bis 19 %, die Warteschlange hatte 3 bis 4 Einträge.
- **Zweimal gesendet:** ACK `0465F0B8` ist die gewollte doppelte Gateway-Kopie (GWACK-01), kein Relay-Fehler. Die zweite Kopie wartet nach der ersten ein volles Fenster.
- **Ursache:** Ein ACK der Prio 1 bekommt 3,0 bis 3,35 s Wartezeit. Jeder empfangene Frame startet sie neu. Erst die Alterung bei 20 s (`CSMA_AGE_STAGE1_MS`, `src/configuration_global.h:435`) beendet das Warten. Das ist eine Designlücke, kein Programmierfehler. Hintergrund: [rca-ack-relay-wait-20261114.md](rca-ack-relay-wait-20261114.md).

**Doppelte `RX_DEDUP_NEW` bei eigenen Beacons**

- **Zahlen:** 112 Message-IDs erscheinen mehrfach als neu, das sind 160 zusätzliche `RX_DEDUP_NEW`. 111 der IDs sind eigene Frames. 110 Paare liegen unter 60 s, das Minimum bei 2,2 s. Insgesamt 3142 `RX_DEDUP_NEW` und 3895 `RX_DEDUP_DUP`.
- **Beispiel:** Eigene Position `1AE1E0BF` kam 9 s nach der Sendung über DL3CSM-1 zurück und 12 s danach über DL2UD-1, beide als neu.
- **Ursache:** Eigene Positionen und HEY werden beim Senden nicht in den Dedup-Ring eingetragen, Texte schon. Jedes Relay-Echo ist deshalb wieder neu. Es gibt keine doppelte Speicherung, Zustellung oder Weiterleitung. Hintergrund: [rca-dedup-double-new-20261114.md](rca-dedup-double-new-20261114.md).

## Was nicht geprüft ist

- **Vergleich mit dem Lauf von 11.06:** kein Vergleichsmitschnitt zur Hand, deshalb keine Bewertung der CRC- und `RX_IRQ_STALE`-Raten (990 in 23,4 h, rund 41 pro Stunde).
- **Empfangsstille:** 28 Ereignisse über 60 s (24 mal 60 s, 3 mal 120 s, 1 mal 180 s). Ein Fehler ist daraus nicht ableitbar. In den Stunden 16 und 20 liegen 25 der 52 Logzeilen.
- **Store-Echo:** doppelte Speicherung oder Zustellung einer Nachricht nicht gesucht.
- **Uhr:** keine Prüfung der Anzeigeuhr gegen die Soll-Zeit, nur das Log.
- **Hardware:** nur DK5EN-98. nRF52-Knoten kamen im Lauf nicht vor.
