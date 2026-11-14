# Fork-Features gegenüber upstream/dev: Grundlage für den nächsten PR

**Gegenüber `upstream/dev` hat die Fork-Firmware 12 Funktionsgruppen, die dort fehlen. Zwei davon, Uhr und Ping sowie Kanalzugriff, sind klein und eigenständig und eignen sich als erster PR.**

Empfehlung: Mit Uhr und Ping (CLK-01, CET-AGE-01, PING-\*) und dem Kanalzugriff (CSMA-01/03/05, IDEE-04) beginnen. Uhr und Ping sind bench-geprüft, der Kanalzugriff teils (CSMA-03 ja, CSMA-01 und CSMA-05 nur Host-Tests). Beide berühren keine Hardware-Pins.

- Stand: 14.11.2026
- Vergleichsbasis: `upstream/dev` 520394fc (PR #1275, 13.11.2026), Fork-Stand `2b8b444f`
- Letzte Synchronisation: `ac763f66` (PR #1272 und c6c9fdf0). Vor dem Schnitt erneut `upstream/dev` einmergen (PR #1274 und #1275 sind danach dazugekommen).
- Quellen: `release-notes.md` (`v4.40a.11.01`, `.11.06`, `.11.08`), `git diff upstream/dev -- src variants platformio.ini`

## Kernfakten

- **Umfang:** 234 Dateien, 20.767 Zeilen neu und 6.933 entfernt in `src`, `variants` und `platformio.ini`. Der größte Teil sind die Web-Assets, die Pin-Logik und die Safeboot-Seite.
- **Neue Dateien ohne Gegenstück upstream:** 47. Darunter `pin_policy.h`, `pin_board.cpp`, `gpio_control.cpp`, `udp_hold.h`, `ext_reg.h`, `fw_schedule.h`, `clock_tick.h`, `tdeck_theme.cpp`, `tdeck_gps_sky.cpp`, `ping_budget.h`.
- **Schon upstream, nicht Teil des PR:** Auto Update, Fernverwaltung RM1, Speichern und Weiterleiten, Zeitzonen, MTU, Sendering-Pool, Sperrliste, GPS-Positionsfilter, Landeszeichen, UV und Wind, Ladestatus, Web-Login, INA226 auf Heltec (PR #1272), Mailbox-Echo.
- **Nicht übertragbar:** der Update-Kanal `dev` (nur mit `MC_AU_DEV_CHANNEL`) und die INSTRUMENT-Diagnosen. Beides ist bewusst fork-eigen.
- **Hardware-Status:** Heltec V4 GNSS (GPSV4-01), T-Deck-Pro-Pins (GPIO-06) und RAK4631 L76K sind nicht auf Hardware geprüft.

## Die Funktionen im Einzelnen

### 1. Uhr und Ping (bench-geprüft)

- **Die Anzeigeuhr springt nicht mehr um 1 bis 2 Minuten zurück** (CET-AGE-01).
  - Ursache: ein zweiter Server-Strom liefert `{CET}`-Beacons, die 20 bis 115 s alt sind.
  - Jetzt: ein Beacon, der mehr als 5 s hinter der Uhr liegt, wird ignoriert (Log `[CET];skip;back_s`).
- **Die Uhr wird jede Sekunde gezeichnet,** auch auf der Track-Seite (CLK-01, CLK-PING-01).
  - Gemessen am 08.11.2026 auf DK5EN-1 und DK5EN-92: Drift +0,1 s in 2 h, ein OLED-Bild 37,6 ms.
- **Ping- und Pong-Frames bleiben 10 s stehen, dann kehrt die Uhr zurück** (PING-HOLD-01, PING-HOLD-02, PING-STOP-01). Längste Lücke der Uhr auf DK5EN-1: 16 s statt 158 s.
- **nRF52 pingt nur nach `--ping start`** und hält `--pingmax` ein (PING-NRF-01). Das ändert das Verhalten auf RAK4631, T114 und T-Echo.

### 2. Kanalzugriff und Relay (Host-Tests und Bench)

- **Ein Frame höherer Priorität verkürzt ein laufendes CSMA-Warten** (CSMA-01). Nur Host-Tests.
- **Leerlauf-Anker, Relay-Jitter, Backoff-Timeout und ACK-Wiederholung wurden gestrafft** (CSMA-05). Nur Host-Tests.
- **Die nRF52-Schleife fragt jede Millisekunde, solange eine Kanalabfrage (CAD) läuft** (CSMA-03).
  - Ursache: 100 ms Pause je Durchlauf, jedes freie Ergebnis war 60 bis 97 ms alt.
  - Jetzt: 1 bis 2 ms Alter auf DK5EN-90. Ein altes Ergebnis wird einmal neu geprüft, bevor gesendet wird.
- **Ein Relay setzt einen Hop-Zähler von 8 bis 15 auf 7 zurück** (IDEE-04), damit Frames alter Firmware nicht kreisen. Nur Host-Test.
- **24-Stunden-Lauf auf DK5EN-98:** Sendepfad lückenlos, Ring ohne Verluste. Offen sind lange Wartezeiten von Relay-ACKs. Hintergrund: [soak-1108-auswertung-20261114.md](soak-1108-auswertung-20261114.md).

### 3. Pins und freie GPIO (bench-geprüft, teils ohne Hardware)

- **Pin-Belegung auf der Pins-Seite** (GPIO-01): welcher Pin welche Funktion hat, und setzen, wo das Board es erlaubt.
  - Eine Pin-Richtlinie lehnt Funk-, Flash-, Display- und GPS-Pins in den vier Settern ab (1-Wire, Taster, analog, Wind), auch beim Laden.
  - Ein Taster auf GPIO 0 (Heltec V3 PRG) zeigt sich als Pin, nicht als "none" (GPIO-02).
- **Freie GPIO schaltbar, standardmäßig aus** (GPIO-01 Teil B): `--gpiocontrol on|off` und `--gpio list|<n> on|off|in|release`.
  - Nur geprüfte Boards: T-Beam (13, 25) und Heltec V3 (2, 4, 5, 6). Ein Konfigurationsimport oder RM kann den Hauptschalter nicht einschalten.
- **Fernverwaltung liest und setzt Pins** (RM1 `pins` und `gpio`). Der Taster-Pin ist per RM nur lesbar.
- **Ein Pin-Setter kann kein RAK4631 mehr lahmlegen** (RAK-PIN-01, GPIO-03): Quarz- und Reset-Pins werden abgelehnt und beim Laden repariert.
- **T-Deck-Pro-Pins korrigiert** (GPIO-04, GPIO-06, GPIO-08, GPIO-09). Nicht getestet, kein T-Deck Pro auf dem Bench.
- **Board-LED nur noch auf der Pins-Seite;** die Setup-Seite behält den Ladestatus-Schalter. Live-Pins-Seite mit 5-s-Aktualisierung (WEB-PIN-01, WEB-LED-01).

### 4. T-Deck Anzeige und Theme (bench-geprüft auf DK5EN-14)

- **Theme Auto, Dark oder Light** (TD-THEME-01), Konsole `--theme auto|dark|light`, bleibt über Neustarts.
- **Lesbarer Light-Modus** (TD-CHAT-01, DISP-PAT-01): schwarzer Text, 3 px Umrandung, getrennte Füllfarben, gesendete Blase grün. Farben aus Fotos des Panels.
- **Setup-Seite für die T-Deck-Anzeige** (WEB-TD-01): Chat-Schrift S, M, L (`--chatfont`), Anzeige Auto, Hell, Dunkel (`--bl mode auto|day|night`), Helligkeit.
- **Schloss-Symbol in der Kopfzeile,** solange die Tastatursperre aktiv ist.
- **GPS-Himmelsansicht** auf dem T-Deck (Position-Reiter mit SKY/TEXT-Umschalter) und dem T-Deck Pro (Seite 3), gemeinsamer Kern (GPS-SKY W2 bis W4). Auf dem T-Deck Pro ist die Seite nicht getestet.

### 5. Web-Oberfläche

- **Setup-Seite:** Beschriftung und Steuerelement in einer Zeile, automatisch hell und dunkel, grüne Auswahl.
- **Eigenes Thema und eigene Schriftgröße der Web-Oberfläche,** im Browser gespeichert, mit tickender Uhr.
- **Kontrast für Sonnenlicht angehoben:** Fließtext 7:1, Nebentext 5,5:1, Bedienelemente 3:1.
- **Menüreihenfolge:** Pins nach Weather, Remote nach Position. CSS und JS werden komprimiert ausgeliefert (WEB-DARK-01, WEB-A11Y-01).
- **Web-Passwort auf der Seite verwalten** (WEB-PWD-01).

### 6. Auto Update und Safeboot

- **Installationsplan** (Verzögerung, Wochentag, Uhrzeit) auf Setup und Info. Nur ein Hinweis, die Installation bleibt aus.
- **T-Beam verweigert Prüfen und Download nicht mehr nach Web-Nutzung** (AU-23): geschlossene Sockets werden freigegeben, bei zerteiltem Heap einmal neu gestartet.
- **Strengere Prüfung vor der Übergabe:** Akku-Untergrenze beim Anwenden und eine Staging-Sperre. Safeboot prüft Header, Chip-ID, Segmentzahl und App-Descriptor (SB-06).
- **Der Update-Knopf heißt Download, nicht Install** (AU-22). Prüfzeiten nutzen die Knotenuhr.
- **Fernumschaltung des Update-Kanals** (RM1 `upd chan prod|dev`) gibt es nur in Fork-Builds.

### 7. Netz und Gateway

- **Nachrichten werden bei einem Serverausfall gehalten:** bis 20 Minuten, nur Text, 1 KB auf klassischem ESP32 und 6 KB auf S3 und RAK, danach mit derselben ID nachgesendet (`--udphold`).
- **EXTUDP:** Peer-Wechsel wirkt sofort (EXT-PEER-01). Antwort auf Register-Abfragen `{"type":"reg","q":"SN"}`, nur lesend, ohne Geheimnisse, nur an den Peer, eine pro Sekunde.
- **Gateway-Stabilität:** UDP-Drain, erneute Namensauflösung des Servers, nicht blockierende EXTUDP-Namensauflösung.
- **`--gateway lab <host[:port]>`** für einen Lab-Server (Entwickler, Code LB).

### 8. Sensoren und GPS

- **DHT22 läuft** (SENS-04, SENS-05): `--dht 11|22`, bleibt über Neustarts, nutzt den mit `--owgpio` gesetzten Pin.
- **RAK4631 L76K:** Erkennung bei 38400 Baud mit Rückfall auf u-blox, `--setublox` und `--setl76k` bleiben über Neustarts (OE1KBC PR #1259, weitergeführt). Nicht getestet, kein L76K auf dem Bench.
- **Heltec V4 GNSS:** RX und TX waren vertauscht, Enable-, Wake- und Reset-Leitungen, I2C auf SDA 4 und SCL 3 (GPSV4-01). Nicht getestet, kein Heltec V4 auf dem Bench.

### 9. Fernverwaltung (RM) und Nachrichtenspeicher

- **Queues-Karte der RM-Seite liest je Zeile** und sendet den nächsten Befehl erst nach der Antwort (RM-Q-01).
- **Texte der RM-Seite passen zum Timeout:** keine Antwort nach 171 s (RM-NOANS-01, RM-TXT-02).
- **Mailbox:** Nachrichten an oder von Dienst-Rufzeichen werden nicht gespeichert, die Zustell-ID kommt in den Dedup-Ring, ein Frame mit eigenem Rufzeichen im Pfad wird nicht gespeichert (QA-RX-03).

### 10. Stabilität und Build

- **Mesh:** Echo-Scan erst nach dem Dekodieren, `{SET}`-Speichern ratenbegrenzt, Sonder-Rufzeichen als Quelle, RX-Kopierrest gelöscht (BL-02).
- **Weniger Last:** BLE-Callbacks loggen nicht mehr im Host-Task, weniger `String`-Kopien, sichere Argumentkopien im Befehlsparser.
- **Kleinere Builds:** keine Exceptions, Debug-Level 0, unbenutzte T-Deck-Widgets und -Schriften entfernt, tabellengesteuerte Web-Einstellungen, gemeinsamer Hash- und Uhr-Code.
- **Ethernet als Build-Flag je Board,** aus auf T114 und T-Echo. Dort wird `--gateway on` abgelehnt.
- **Geheimnisse werden im BLE-Einstellungspfad maskiert** (QA-NET-02). Dieser Pfad existiert über Funk nicht, die tote nRF52-Characteristic `0xF0A1` ist entfernt (BLE-SET-01).

### 11. Diagnose und Bench-Werkzeuge

- **Empfangsstille im Log** (nur mit `--loradebug`). Die Werte stehen im Release `v4.40a.11.01`.
- **INSTRUMENT-Builds protokollieren die Uhr** (`[CLOCK]`, `[DISPCLK]`, `--timesrc`, `--clockstat`, `--oledshot`, `--tdshot`). Release-Images bleiben unverändert.
- **Skripte:** `clock_drift.py`, `--offline-ok` für `identity_guard`, OLED- und T-Deck-Bildschirmfoto.

### 12. Weitere Fork-Details

- **Vision Master E290 OTP:** keine periodische Vollauffrischung (`E290_FULL_EVERY=0`).
- **BMP390-Guard, Zeitzone und Sensor-Korrekturen** (TZ-02, SENS-03, BAT-05, TXP-02).
- **Test:** `setlat` und `setlon` lesen zurück (WEB-LATLON, P-0124).

## Vorschlag zum Schneiden

| PR  | Inhalt                                      | Risiko           | Bench                 |
| --- | ------------------------------------------- | ---------------- | --------------------- |
| 1   | Uhr und Ping (Abschnitt 1)                  | niedrig          | ja                    |
| 2   | Kanalzugriff und Hop-Klemme (Abschnitt 2)   | mittel, Funkpfad | nRF52 ja, ESP32 teils |
| 3   | Pins, GPIO und RAK-Pin-Schutz (Abschnitt 3) | mittel, Hardware | ja, T-Deck Pro nein   |
| 4   | T-Deck Theme und Web (Abschnitte 4 und 5)   | niedrig          | ja                    |
| 5   | Heltec V4 GNSS und RAK L76K (Abschnitt 8)   | mittel           | nein                  |

Die übrigen Gruppen (Auto Update, Netz, Stabilität) hängen am Fork-Stand und brauchen jeweils einen eigenen Schnitt aus `upstream/dev`.

## Was nicht geprüft ist

- **Abgleich Zeile für Zeile:** Die Zuordnung "fehlt upstream" beruht auf neuen Dateien und den Release Notes. Für Abschnitte 5, 6, 9 und 10 ist nicht jede Änderung einzeln gegen `upstream/dev` geprüft. Vor jedem PR gilt `/submit-pr`.
- **Auto Update und Netz:** `updchan`, `cet_age` und `E290_FULL_EVERY` kommen auch in `upstream/dev` vor. Der Unterschied liegt in einzelnen Zeilen, nicht in ganzen Dateien.
- **Ohne Hardware:** Heltec V4, T-Deck Pro, L76K-Modul, GPS-Himmelsansicht auf dem T-Deck Pro.
- **Nicht auf dem Bench:** CSMA-01 und CSMA-05 (nur Host-Tests), IDEE-04 (nur Host-Test).
