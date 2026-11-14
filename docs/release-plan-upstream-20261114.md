# Upstream-Releaseplan: geprüfter Code zuerst, je ein Feldtest dabei

**Welle 1 (Uhr, T-Deck, drei Feldteile) ist am 14.11.2026 geschnitten und geht nach dem Bench am 17.11.2026 an upstream `dev`. Danach jede Woche ein PR; der LoRa-Kanalzugriff bleibt draußen, bis er stabil ist.**

Empfehlung: Web-Oberfläche als eigenen PR nach Absprache mit Kurt. Der Kanalzugriff läuft parallel durch eine Stabilisierung mit festen Ausstiegskriterien und geht frühestens am 29.01.2027 als Welle 7 raus.

- Stand: 14.11.2026, Fassung 3
- Basis: `fork-dev` 173b1e3c, `upstream/dev` 520394fc (PR #1275). PR #1274 und #1275 sind eingemergt.
- Quellen: [features-vs-upstream-20261114.md](features-vs-upstream-20261114.md), [BACKLOG.md](BACKLOG.md) 3.2, [soak-1108-auswertung-20261114.md](soak-1108-auswertung-20261114.md), [rca-ack-relay-wait-20261114.md](rca-ack-relay-wait-20261114.md), [rca-dedup-double-new-20261114.md](rca-dedup-double-new-20261114.md), [campaign-backlog-round-20261108.md](campaign-backlog-round-20261108.md)
- Gantt: `~/Desktop/upstream-releaseplan-20261114.html`

> [!WARNING]
> **Vier Punkte brauchen eine Entscheidung.**
>
> - **Dedup eigener Baken (DEDUP-OWN-01):** Firmware ändern oder nur die Log-Auswertung anpassen. Empfehlung: erst die Log-Regel, Firmware nur, wenn die Auswertung eigene Echos nicht filtern kann. Entscheidung bis 20.11.2026.
> - **Soak auf DK5EN-98 im Januar:** braucht dein OK. Alternative ist ein Bench-Gateway, dann ohne echten Verkehr.
> - **Auto Update (Welle 7):** nur, wenn AU-24 und AU-25 bis 08.01.2027 reproduziert sind. Sonst rutscht die Welle hinter den Kanalzugriff.
> - **Feiertagspause und `--gateway lab`:** wie in Fassung 1 angenommen, Pause vom 21.12.2026 bis 03.01.2027, Lab-Gateway bleibt im Fork.

## Fassung 3: Welle 1 am 14.11.2026 geschnitten

Die bisherigen Wellen 1 bis 3 sind zusammengelegt und ohne Ping geschnitten. Ping folgt als eigene Welle 2. Die Tabelle "Die Wellen" unten gilt in dieser Nummerierung.

- **Branch:** `pr/uhr-web-tdeck` aus `upstream/dev` 520394fc, Worktree `../MeshCom-PR-next`.
- **Drin:** Uhr jede Sekunde (CLK-01, CLK-PING-01), CET-Filter (CET-AGE-01), T-Deck Theme, Chat und Setup-Tab (TD-THEME-01, TD-CHAT-01), GPS-Himmel auf T-Deck, T-Deck Pro und T114, Build ohne Exceptions und mit Debug-Level 0, schlankeres LVGL.
- **Feldteile:** Heltec V4 GNSS (GPSV4-01), DHT22 (SENS-04, SENS-05 nur Prüfung beim Laden), T-Deck-Pro-Pins (GPIO-06, GPIO-08, GPIO-09).
- **Draußen:** Ping (`b3b2bf0b`, `5583b177`), die T-Deck-Kopfzeilen-Umschalter für BLE und WLAN (TD-30, TD-33), die GPS-Schrift (TD-29), das INSTRUMENT-Testmuster (DISP-PAT-01) und alle INSTRUMENT-Uhr-Hooks.
- **Offen, zu entscheiden:** die Web-Oberfläche. Upstream hat CSS und JS inline, der Fork lagert sie in `src/web_functions/web/` aus und erzeugt `web_assets.h` mit dem Fork-Tool `tools/gen_web_assets.py`. Empfehlung: Web als eigener PR, zuerst der Umbau nach Absprache mit Kurt.

| Welle | PR an `dev`         | Thema                    | Feldteil (C)                      |
| ----- | ------------------- | ------------------------ | --------------------------------- |
| 1     | 17.11.2026          | Uhr, T-Deck              | Heltec V4 GNSS, DHT22, T-Deck Pro |
| 2     | 20.11.2026          | Ping                     | keiner                            |
| 3     | 27.11.2026          | Pins-Seite               | freie GPIO, standardmäßig aus     |
| 4     | 04.12.2026          | Fernverwaltung und Mesh  | RAK4631 mit L76K                  |
| 5     | 11.12.2026          | Gateway                  | Vision Master E290                |
| 6     | 18.12.2026          | Auto Update und Safeboot | keiner, bedingt                   |
| 7     | frühestens 29.01.27 | Kanalzugriff und Relay   | keiner                            |

## Kernfakten

- **Prinzip fail fast:** Was der Bench belegt hat, trägt die Welle. Was nur das Feld zeigen kann, fährt in kleinen Stücken mit, damit ein Fehler früh und begrenzt auffällt.
- **Grenze des Feldtests:** Funkpfad, Auto Update und Safeboot werden nie im Feld getestet. Dort kann ein Fehler einen Knoten kosten.
- **Kanalzugriff nicht fertig:** Weitergeleitete ACKs warten bis 24,8 s, weil jeder empfangene Frame ihre Wartezeit neu startet (ACK-AGE-01, Soak vom 08.11.2026 auf DK5EN-98). Der Soak hatte keinen nRF52-Knoten.
- **Takt:** Montag und Dienstag portieren, Mittwoch und Donnerstag Gate und Bench, Freitag PR. Danach zwei Wochen Feld-Rückmeldung für den Feldteil.
- **Merge-Zeit upstream:** zuletzt unter einem Tag (PR #1245 bis #1272).
- **Bleibt im Fork:** Update-Kanal `dev` (`MC_AU_DEV_CHANNEL`), RM `upd chan`, INSTRUMENT-Diagnosen, Bench-Skripte, `--gateway lab`.

## Reifegrad

Jede Änderung hat eine von vier Stufen. Die Stufe entscheidet, wie sie in den Plan kommt.

| Stufe           | Bedeutung                                    | Umgang im Plan                                     |
| --------------- | -------------------------------------------- | -------------------------------------------------- |
| A, bench-belegt | auf Hardware gemessen oder im Soak bestanden | trägt die Welle                                    |
| B, Bench offen  | Host-Tests grün, Bench-Check steht aus       | Bench in den Gate-Tagen der Welle, sonst kein Teil |
| C, nur im Feld  | Hardware oder Lage fehlt auf dem Bench       | ein Stück je Welle, fail fast                      |
| D, nicht stabil | offene Designlücke oder Messung              | zurückgehalten, eigene Strecke                     |

**Regeln für den Feldteil (C)**

1. **Begrenzt:** auf ein Board oder standardmäßig aus.
2. **Nichts verschlechtern:** nur Funktionen, die upstream heute fehlen oder nie liefen.
3. **Offen benannt:** der PR-Text sagt "nicht auf Hardware geprüft" und was Nutzer melden sollen.
4. **Rücknahme:** der Feldteil liegt in eigenen Commits und lässt sich einzeln zurückziehen.

## Die Wellen (Fassung 2)

Gilt nur noch für die Inhalte je Thema. Nummern und Termine stehen in Fassung 3 oben.

| Welle | PR an `dev` | Thema                    | Feldteil (C)                  |
| ----- | ----------- | ------------------------ | ----------------------------- |
| 1     | 20.11.2026  | Uhr und Ping             | Heltec V4 GNSS                |
| 2     | 27.11.2026  | Web-Oberfläche           | DHT22                         |
| 3     | 04.12.2026  | T-Deck Anzeige           | T-Deck Pro                    |
| 4     | 11.12.2026  | Pins-Seite               | freie GPIO, standardmäßig aus |
| 5     | 18.12.2026  | Fernverwaltung und Mesh  | RAK4631 mit L76K              |
| 6     | 08.01.2027  | Gateway                  | Vision Master E290            |
| 7     | 15.01.2027  | Auto Update und Safeboot | keiner, bedingt               |
| 8     | 29.01.2027  | Kanalzugriff und Relay   | keiner, frühestens            |

### Welle 1: Uhr und Ping (PR 20.11.2026)

- **Frontend (A):** Die Uhr wird jede Sekunde gezeichnet. Ping- und Pong-Bilder bleiben 10 s stehen, dann kommt die Uhr zurück (CLK-01, CLK-PING-01, PING-HOLD-01, PING-HOLD-02, PING-STOP-01).
  - Bench 08.11.2026: Drift unter 0,5 s, längste Lücke der Uhr auf DK5EN-1 16 s statt 158 s.
- **Backend (A):** Filter gegen alte `{CET}`-Beacons und gemeinsamer Uhr-Code (CET-AGE-01).
  - Re-Bench 08.11.2026, Heltec und T-Beam: jede Zeitsetzung innerhalb 0,8 s, kein Rücksprung.
- **Bench in der Welle (B):** nRF52-Ping-Budget auf dem RAK4631 (PING-NRF-01).
- **Feldteil (C):** Heltec V4 GNSS mit korrigiertem RX und TX, Enable- und Reset-Leitungen, I2C auf SDA 4 und SCL 3 (GPSV4-01).
  - Warum fail fast passt: GNSS auf dem Heltec V4 lief upstream nie. Ein Fehler kann nichts Funktionierendes kosten.
- **Bekannte Lücke:** Im TRACK-Modus bleibt ein Pong die volle Ping-Zeit stehen (TRACK-PING-01). Steht im PR-Text.

### Welle 2: Web-Oberfläche (PR 27.11.2026)

- **Frontend (A):** Setup-Zeilen, eigenes Thema und Schriftgröße, Kontrast für Sonnenlicht, Menüreihenfolge (WEB-DARK-01, WEB-A11Y-01).
  - Betreiber-Review in Chrome und Safari am 08.11.2026 auf DK5EN-14.
- **Backend (A):** tabellengesteuerte Web-Einstellungen, weniger `String`-Kopien, sichere Argumentkopien im Befehlsparser. Seit `v4.40a.11.06` im Fork-Release.
- **Bench in der Welle (B):** Web-Passwort setzen, ändern und löschen (WEB-PWD-01), Laden mit kaltem Cache auf ESP32 und RAK4631 (WEB-GZ-01), doppelte ID `extudp` (WEB-DUP-01), `setlat` auf dem RAK4631 (WEB-LATLON-01).
- **Feldteil (C):** DHT22 über `--dht 22` (SENS-04, SENS-05).
  - Warum fail fast passt: DHT22 wurde bisher nie gelesen.
  - Wen es trifft: DHT11-Nutzer. Der Sensor nutzt jetzt den mit `--owgpio` gesetzten Pin statt des fest eingebauten. Gehört als Warnung in den PR-Text.

### Welle 3: T-Deck Anzeige (PR 04.12.2026)

- **Frontend (A):** Theme Auto, Dark oder Light, lesbarer Light-Modus, T-Deck-Setup, Schloss-Symbol, GPS-Himmel im Position-Reiter (TD-THEME-01, TD-CHAT-01, DISP-PAT-01, WEB-TD-01). Bench auf DK5EN-14.
- **Backend:** unbenutzte T-Deck-Widgets und -Schriften entfernt, Build ohne Exceptions und mit Debug-Level 0.
  - Der Build-Schalter wirkt auf jedes Environment. Die Welle prüft darum REVIEW.md R2 mit allen Envs.
- **Feldteil (C):** T-Deck Pro: Tastatur-Interrupt auf GPIO 15 statt 46, entfernte T-Deck-Kopien, GPS-Himmel auf Seite 3 (GPIO-06, GPIO-08, GPIO-09).
  - Kann einen funktionierenden Knoten kosten: Ist die neue Pin-Zuordnung falsch, reagiert die Tastatur nicht. Der PR bittet T-Deck-Pro-Besitzer um Rückmeldung.

### Welle 4: Pins-Seite (PR 11.12.2026)

- **Frontend (A):** Pins-Seite mit Live-Aktualisierung, Board-LED nur dort (WEB-PIN-01, WEB-LED-01). Web-GUI in Chrome auf RAK, T-Beam und T-Deck Plus am 06.11.2026.
- **Backend:** Pin-Richtlinie in den vier Settern, RAK-Quarz- und Reset-Pins abgelehnt und beim Laden repariert (GPIO-01, GPIO-02, RAK-PIN-01, GPIO-03).
- **Bench in der Welle (B):** LED-Modi über einen Neustart und per RM1 (WEB-LED-01).
- **Feldteil (C):** freie GPIO schaltbar, standardmäßig aus, nur T-Beam und Heltec V3 (GPIO-01 Teil B).
  - Betreiber-Entscheidung 07.11.2026: ohne Bench freigeben, Rückmeldungen der OMs sammeln (GPIO-07).
  - Ein Konfigurationsimport oder RM kann den Hauptschalter nicht einschalten.

### Welle 5: Fernverwaltung und Mesh (PR 18.12.2026)

- **Frontend (B):** Queues-Karte der RM-Seite liest je Zeile, Texte passen zum Timeout von 171 s (RM-Q-01, RM-NOANS-01, RM-TXT-02). RM-Q-01 hat bisher nur JS-Tests.
- **Backend (A):** Mailbox-Regeln (QA-RX-03) und RX-Pfad (BL-02). Beide im Soak vom 08.11.2026 ohne Befund: alle sieben Ring-Zähler 0, `ERR_PAYLOAD` 0, `RX_ERROR` 0.
  - Annahme: BL-02 hängt nicht am CSMA-Code. Beim Port prüfen; hängt er daran, wandert er in Welle 8.
- **Backend (B):** Geheimnisse im BLE-Pfad maskiert, tote nRF52-Characteristic `0xF0A1` entfernt (QA-NET-02, BLE-SET-01). Bench: nRF Connect auf DK5EN-90.
- **Feldteil (C):** RAK4631 mit L76K-GPS, Erkennung bei 38400 Baud mit Rückfall auf u-blox. Weiterführung von OE1KBC PR #1259.

### Pause vom 21.12.2026 bis 03.01.2027

- **Kein PR.** Zeit für einen Fork-Release und für die Feld-Rückmeldungen der Wellen 1 bis 5.

### Welle 6: Gateway (PR 08.01.2027)

- **Frontend (A):** EXTUDP-Adresse im Web, `--gateway on` auf T114 und T-Echo abgelehnt.
- **Backend (A):** EXTUDP-Peerwechsel und Register-Abfrage (EXT-PEER-01). Bench 06.11.2026 auf allen vier Knoten, auch während eines WLAN-Ausfalls. Dazu UDP-Drain und erneute Namensauflösung.
- **Backend (B):** Nachrichten werden bei Serverausfall bis 20 Minuten gehalten und mit derselben ID nachgesendet.
  - Das Halten ist auf jedem Gateway immer an; `--udphold` ist nur Status und Simulation. Darum kein Feldteil.
  - Bench: `--udphold outage on`. Ein echter Serverausfall ist nicht geprüft und steht so im PR-Text.
- **Feldteil (C):** Vision Master E290 ohne periodische Vollauffrischung (`E290_FULL_EVERY=0`). Kein Gerät auf dem Bench.

### Welle 7: Auto Update und Safeboot (PR 15.01.2027, bedingt)

- **Frontend (A):** Installationsplan auf Setup und Info, Knopf heißt Download (AU-22). Seit `v4.40a.11.06` im Fork-Release.
- **Backend:** Sockets freigeben und Neustart des Update-Jobs bei zerteiltem Heap (AU-23, `au_gate_burst.py` 20 von 20), Akku-Untergrenze, Staging-Sperre, Safeboot-Prüfung (SB-06).
- **Bedingung:** AU-24 und AU-25 sind reproduziert. Beide zeigen Download-Abbrüche nach Stunden Laufzeit, Ursache unbekannt. Ohne Erklärung ist nicht klar, ob AU-23 sie behebt.
- **Bench in der Welle (B):** falsches Chip-Image und Nicht-App-Image an Safeboot (SB-06).
- **Kein Feldteil:** Ein Fehler im Update-Pfad kann einen Knoten kosten.

### Stabilisierung des Kanalzugriffs (parallel, kein PR)

Ziel ist Welle 8. Sie beginnt erst, wenn alle Ausstiegskriterien erfüllt sind.

| Arbeit                                     | Zeitraum                | Ergebnis                                                            |
| ------------------------------------------ | ----------------------- | ------------------------------------------------------------------- |
| ACK-Wartezeit (ACK-AGE-01)                 | 16.11. bis 09.12.2026   | Alterung für Prio 1 bei 6 bis 8 s, Host-Test, Bench ESP32 und nRF52 |
| Dedup eigener Baken (DEDUP-OWN-01)         | Entscheidung 20.11.2026 | Log-Regel 6.14, Firmware nur bei Bedarf                             |
| CSMA-Messung (CSMA-04, CSMA-02)            | 07.12. bis 16.12.2026   | Messung auf drei Knoten, Slot neu gesetzt                           |
| nRF52 CAD-Takt und Strom (CSMA-03, PWR-01) | 14.12. bis 18.12.2026   | Schwelle aus neuer p99, Strom mit INA226                            |
| Soak 24 h                                  | 11.01. bis 19.01.2027   | ESP32-Gateway und nRF52-Knoten, Auswertung                          |

**Ausstiegskriterien**

1. **ACK-Wartezeit:** Relay-ACK der Prio 1 höchstens 8 s im Soak. Heute bis 24,8 s. Der Zielwert ist ein Vorschlag aus der RCA, nicht abgestimmt.
2. **Messung:** CSMA-04 auf drei Knoten gelaufen, Slot nach CSMA-02 gesetzt.
3. **nRF52:** CSMA-03-Schwelle aus der neuen p99 gesetzt. Der Strom mit dem 1-ms-Takt während einer Kanalabfrage ist gemessen und bewertet (PWR-01).
4. **Soak:** 24 h ohne Neustart, ohne Ring-Verlust, mit nRF52-Knoten im Lauf.

### Welle 8: Kanalzugriff und Relay (frühestens PR 29.01.2027)

- **Inhalt:** Vorrang höherer Priorität im CSMA-Warten, gestraffte Relay-Zeiten, CAD-Takt 1 ms auf nRF52, Alterung der ACKs, Hop-Zähler 8 bis 15 auf 7 (CSMA-01, CSMA-03, CSMA-05, ACK-AGE-01, IDEE-04).
  - CAD heißt Kanalabfrage vor dem Senden.
- **Nichts Sichtbares:** die eine Ausnahme von der Paarung. Ein Funkpfad-PR steht allein.
- **Kein Feldteil:** Der Funkpfad wird nie im Feld getestet.

## Was nicht geprüft ist

**Zuordnung**

- **Reifegrad je Änderung** stammt aus BACKLOG, Release-Journal und Kampagnen-Dokumenten, nicht aus einem neuen Bench-Lauf.
- **Abschnitte 5, 6, 9 und 10 der Feature-Liste** sind nicht Zeile für Zeile gegen `upstream/dev` geprüft. Teile davon könnten schon upstream sein.
- **Dateigrenzen der Wellen:** Ob jede Welle nur ihre Dateien berührt, zeigt erst der Port.

**Feldteile**

- **Feld-Rückmeldung ist nicht sicher.** Welche OMs ein Heltec V4, T-Deck Pro, L76K-Modul, DHT22 oder E290 haben, ist nicht bekannt.
- **Seit `v4.40a.11.08` im Fork:** Ob Fork-Nutzer zu diesen Teilen schon etwas gemeldet haben, ist nicht geprüft.

**Termine**

- **Plan, keine Zusage der Maintainer.** Die Feiertagspause ist angenommen.
- **Welle 8** hängt an den Ausstiegskriterien, nicht am Kalender.

**Gantt**

- **Sichtprüfung im Browser:** dunkel und Desktop-Breite. Hell und Handybreite nicht angesehen.

<details>
<summary>Fassung 1 vom 14.11.2026: neun Wellen nach Thema, Kanalzugriff als Welle 8</summary>

- Neun Wellen, je ein sichtbarer Teil mit passendem Backend, ohne Blick auf den Teststand.
- Welle 8 Kanalzugriff am 22.01.2027 mit Soak, Welle 9 Heltec V4, L76K und T-Deck Pro ohne Termin.
- Ersetzt, weil BACKLOG und Soak-Auswertung zeigen, dass der Kanalzugriff nicht fertig ist, und weil die Hardware-Teile als Feldteile früher rausgehen können.

</details>
