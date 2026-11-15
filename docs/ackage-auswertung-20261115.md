# Auswertung ACK-AGE-01 auf DK5EN-98 (2026-11-14/15)

**Der Fix wirkt, wo er greift: Jedes ACK, das 6 s alt wurde, ging 1,5 bis 1,7 s später hinaus. Das Maximum sinkt von 24,8 s auf 13,6 s, der Median von 6,1 s auf 3,3 s. Die Nacht war aber ruhig, deshalb ist der kleinere Median vor allem eine Folge der niedrigen Last und kein Beweis für den Fix.**

- Lauf: DK5EN-98 (Heltec WiFi LoRa 32 V3, Produktions-Gateway), fork-dev `aa8e1b45`, 2026-11-14 21:45 bis 2026-11-15 12:00 (14,2 h)
- Mitschnitt: `meshlogger.py` auf rpizero, `~/meshlog/dk5en-98-ackage-20261114/`, 85 110 Zeilen, 0 Reconnects, `--setlog on` und `--loradebug on`
- Vergleich: SOAK-1108 ([soak-1108-auswertung-20261114.md](soak-1108-auswertung-20261114.md), [rca-ack-relay-wait-20261114.md](rca-ack-relay-wait-20261114.md))
- Messgröße: `wait=` in der Zeile `[LOG] TX x<id> A ... prio=1 src=r`, also die Zeit von der Einreihung bis zur Sendung

## Ergebnis

| Kennzahl                     | SOAK-1108 (v4.40a.11.08) | ACK-AGE-01 (`aa8e1b45`) |
| ---------------------------- | ------------------------ | ----------------------- |
| Relay-ACKs (`rx_ack_fwd`)    | 20                       | 17                      |
| Gateway-ACKs (`rx_dm_ack_*`) | 6                        | 0                       |
| Median Wartezeit             | 6,06 s                   | 3,26 s                  |
| p95                          | 21,7 s                   | 11,4 s                  |
| Maximum                      | 24,8 s                   | 13,6 s                  |
| ACKs über 10 s               | 9 von 26                 | 2 von 17                |
| `CSMA_AGED ... ack=1`        | Zeile gab es noch nicht  | 8 Zeilen = 4 Ereignisse |
| Kanalauslastung Mittel       | 9 bis 19 % im Beispiel   | 1,9 % (Max 38 %)        |

- **13 von 17 ACKs** gingen nach genau einem Wartefenster hinaus (3,10 bis 3,41 s). Dabei lief keine Neustart-Kette, der Fix kam nicht zum Zug.
- **Alle 4 langen Wartezeiten** fallen in ein einziges Bündel um 22:53 (Abschnitt "Das Bündel um 22:53").
- **Keine ACK ging verloren:** 17 `RING_WRITE ... src=rx_ack_fwd`, 17 `TX`-Zeilen mit denselben IDs.

## Das Bündel um 22:53

Ein Gruppentext (ID `55A8F2F8`, Gruppe 9) erzeugte vier ACKs mit verschiedenen IDs. Sie kamen im Abstand von etwa 3,4 s an. DK5EN-98 sendet sie nacheinander, und nach jeder eigenen Sendung läuft die volle Wartezeit erneut.

| ACK        | Eingereiht | Gealtert (6 s)       | Gesendet | `wait=` |
| ---------- | ---------- | -------------------- | -------- | ------- |
| `0103F79C` | 22:53:15,8 | 22:53:22,5 (6,76 s)  | 22:53:24 | 8,4 s   |
| `2CA53289` | 22:53:19,1 | 22:53:25,9 (6,76 s)  | 22:53:27 | 8,3 s   |
| `0C6CBC28` | 22:53:22,5 | 22:53:32,2 (9,68 s)  | 22:53:34 | 11,4 s  |
| `0C2DE148` | 22:53:25,9 | 22:53:37,9 (11,96 s) | 22:53:39 | 13,6 s  |

- **Stufe 1 greift wie geplant:** Ein Empfang nach 6 s Alter setzt die Wartezeit auf 1,5 bis 1,7 s statt 3,0 bis 3,35 s. Alle vier Sendungen folgten dem 1,5 bis 1,7 s später, jeweils mit `CAD_FREE attempt=0`.
- **Warum 11,4 und 13,6 s:** Das sind Platz 3 und 4 in der Schlange. Nach einer eigenen Sendung gilt die volle Wartezeit ohne Altersabschlag (`csma_reset`, gewollt, damit Nachbarn ihr Fenster behalten). Gealtert wird erst beim nächsten Empfang.
- **Ohne das Bündel** lag das Maximum bei 3,41 s.

## Was der Lauf nicht zeigt

- **Kein kontrollierter Vergleich:** Die Nacht war ruhig (1,9 % Mittel gegen 9 bis 19 % im SOAK-1108-Beispiel). Neustart-Ketten über 6 s gab es außerhalb des Bündels nicht. Der kleinere Median kommt von der Last, nicht vom Fix.
- **Gateway-ACKs:** Keine `rx_dm_ack_gw`/`rx_dm_ack_new` im Lauf, ihre Wartezeit ist ungemessen.
- **nRF52:** Weiter offen (Bench auf einem RAK4631).
- **Wirkung beim Absender:** Nicht gemessen; ob ein Absender wegen eines späten ACK wiederholt, sieht man im Log von DK5EN-98 nicht.

## Gesundheit des Laufs

| Aspekt              | Status     | Kommentar                                                                        |
| ------------------- | ---------- | -------------------------------------------------------------------------------- |
| Neustarts / Crashes | OK         | ein Boot (Software-Reset beim Flashen 21:44), kein Crash, kein TX-Watchdog       |
| State Machine       | OK         | 11 337 Übergänge, 0 Fehler                                                       |
| CSMA / CAD          | OK         | 571 Sendeversuche, 97,7 % beim ersten CAD frei, 1 kurzer CAD-Sturm (1,6 s)       |
| Kanalauslastung     | OK         | Mittel 1,9 %, Maximum 38 %                                                       |
| OnRxDone            | OK         | Maximum 14 ms, keine Warnung                                                     |
| WLAN                | OK         | keine Unterbrechung                                                              |
| Sendering           | OK         | 1 Drop: veraltetes `{CET}`-Zeitsignal, korrekt verworfen (siehe unten)           |
| Relay-Wartezeit     | Beobachten | Prio 4: 54 von 82 über 10 s, Prio 5: 31 von 73; offene Beobachtung aus SOAK-1108 |

## Nebenbefund: falsche Altersangabe in `RING_DROP_STALE`

- **Log:** `RING_DROP_STALE slot=1 prio=3 age_s=4294448 msg_id=6AF3A414 reason=cet` (2026-11-14 21:54:12).
- **Der Drop ist richtig:** Das Zeitsignal trug `20:51:39`, geprüft wurde um 20:54:12 UTC, also 153 s alt bei einer Grenze von 150 s (`CET_MAX_AGE_S`, `src/cet_age.h:16`).
- **Die Zahl ist falsch:** `txRingDropStaleCet()` ruft `txRingSweep(0, clk, true)` mit `now_ms = 0` auf (`src/txring_functions.cpp:682`). Das Alter `0 - Einreihzeit` läuft über. Nur die Debugzeile ist betroffen, die Entscheidung nutzt im CET-Pfad kein `age_ms`.
- **Vorschlag:** `millis()` übergeben oder im CET-Pfad `age_s` weglassen. Backlog-Zeile `CET-AGE-LOG-01`.

## Nächste Schritte

- ACK-AGE-01 auf einem nRF52-Knoten benchen (offen).
- Unter Last nachmessen: ein Abendlauf mit mehr Verkehr zeigt, ob Neustart-Ketten über 6 s jetzt bei etwa 8 s enden.
- `CET-AGE-LOG-01` beheben, wenn ohnehin an `txring_functions.cpp` gearbeitet wird.
