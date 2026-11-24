# PR-Text: gemeinsamer PR (Auto Update, kleine Korrekturen, Gateway, Fernverwaltung, hmac_connect.py)

Entwurf, nicht eingereicht. Ziel: `icssw-org/MeshCom-Firmware` `dev`, Zweig `pr/combined` von `upstream/dev`.
Stand 25.11.2026. Welche Dateien und Abschnitte hineingehen, steht in
[pr-combined-scope-20261125.md](pr-combined-scope-20261125.md); die Bench-Belege in
[campaign-rc-bench-20261125.md](campaign-rc-bench-20261125.md).

Offene Punkte vor dem Einreichen stehen am Ende dieses Dokuments (nicht Teil des PR-Texts).

---

## Titel

Auto Update ohne Speicherfehler, kleine Korrekturen, Gateway-Halten und Registerabfrage, Fernverwaltung pins/gpio

## Text (ab hier ist es der PR-Body)

**Dieser PR behebt, dass Auto Update auf ESP32 ohne PSRAM nach längerer Laufzeit mit "Not enough free memory"
abbricht und dass der T-Beam beim Löschen des Ablagebereichs neu startet. Dazu kommen fünf kleine Korrekturen, das
Halten von Textnachrichten bei Serverausfall, die Registerabfrage über EXTUDP, die Knotenseite der
Fernverwaltungskarten und ein Fix für `tools/hmac_connect.py`.**

Jeder Teil liegt in eigenen Commits und lässt sich einzeln zurücknehmen. Nichts davon ändert das Funkformat.

Closes #1303, Closes #1305, Closes #1306, Closes #1307, Closes #1308, Refs #1309 (SB-06 ist nicht enthalten),
Closes #<Issue Auto Update Speicher>, Closes #<Issue INA226>

### 1. Auto Update: kein Speicherfehler mehr, kein Neustart am T-Beam

**Was sich ändert**

- **Zulassung vor dem Download misst, was der Lauf braucht** (`src/esp32/fw_update_net.cpp`, `src/fw_update.h`).
  - Ursache: der Download braucht 66 bis 69 KB internen Heap, die Hälfte davon sind die zwei TLS-Puffer von je
    16.717 Byte, die gleichzeitig existieren müssen. Nach Stunden Laufzeit ist der Heap zerstückelt; die alte Prüfung
    ließ den Lauf zu, dann scheiterte TLS, oder sie lehnte ab, obwohl ein Neustart geholfen hätte.
  - Jetzt: Mindestwerte für freien Heap und größten Block je Chip, eine Probe-Allokation der beiden Puffer, und
    während eines Laufs beantwortet der Webserver nur die Update-Seite (sonst 503 mit Retry-After), kein WLAN-Scan.
- **Eigener, sparsamer TLS-Client** (`src/esp32/mc_tls/`): eine Vertrauenswurzel je Host statt des ganzen Bündels
  (`src/fw_update_roots.h`), sendet Teilschreibvorgänge vollständig.
- **Ein Puffer je Update-Lauf** (`src/fw_au_buf.h`): beim Start des Laufs, solange der große freie Block noch da
  ist, wird ein Block von 33.440 Byte belegt. Er hält während der TLS-Sitzungen beide TLS-Puffer und dient danach
  der Image-Prüfung als 32-KB-Wörterbuch. Die TLS-Puffer zerstückeln den Heap dadurch nicht mehr.
  - Dazu gehört eine Obergrenze für TLS-Allokationen während eines Laufs (`src/fw_tls_alloc_cap.h`): der Hook auf
    `mbedtls_platform_set_calloc_free` ist auf jedem ESP32 aktiv und begrenzt nur während eines Laufs.
  - Kein kopierter Fremdcode: mbedTLS bleibt das des Frameworks.
- **T-Beam startet beim Löschen nicht mehr neu** (AU-29): das Löschen in 64-KB-Schritten hielt CPU0 bis zu 900 ms je
  Block fest, der Idle-Task kam fünf Sekunden lang nicht dran und der Task-Watchdog schlug zu. Jetzt schläft der Lauf
  nach jedem Block und wartet begrenzt, bis der Idle-Task gelaufen ist. Der Fehler steckt auch in 11.21 und 11.22.
- **Installation aus der Weboberfläche am T-Beam** (AU-30): Neustart des Lauf-Tasks, wenn der Puffer verfehlt wurde;
  die Image-Prüfung läuft bei knappem Speicher in einem eigenen Start vor dem WLAN.
- **Installationsplan** (#1309, AU-22, AU-23): `--autoupdate delay|day|time` und Felder im Setup zeigen die geplante
  Minute und kündigen sie an; der Plan installiert nie selbst. Der Heap-Test nach Webnutzung am T-Beam räumt
  TIME_WAIT-Verbindungen vorher ab.

**Geprüft auf Hardware** (Fork-Images, siehe "Nicht geprüft" zum Unterschied)

- Heltec V3: Prüfung und Download mit Ablage, 1.180.497 Byte in 11,2 s, Löschen 2,7 s, alle TLS-Puffer im
  Lauf-Puffer (24.11.2026). [Nacht: noch einzutragen]
- T-Beam v1.2: 10 von 10 Web-Installationen ohne Watchdog (24.11.2026); mit dem Stand dieses PRs Download und
  Ablage, Löschen 6,0 s, längste Idle-Lücke 362 ms, niedrigster freier Heap im Lauf 11.332 Byte. [Nacht]
- T-Deck Plus (PSRAM): Download und Ablage, 1.617.579 Byte in 20,7 s (PSRAM-Weg, kein Lauf-Puffer). [Nacht]

### 2. Kleine Korrekturen

- **Parametertabelle der Setup-Seite im Flash** (#1305, RAM-WSP-01): spart 880 Byte RAM am RAK4631 und 1.008 Byte am
  Heltec V3 (gemessen am Build). Seit dem Fork-Release vom 20.11.2026 im Feld.
- **RX-Pfad** (#1306):
  - Ein Störrahmen beendet keine laufende Wiederholung mehr (QA-RX-03).
  - Die RX-Kopie trägt keine Bytes eines früheren Rahmens mehr (BL-02).
  - Die ID in der `:rej`-Antwort stimmt (Versatz korrigiert).
  - Seit 06.11.2026 im Feld.
- **INA226-Kalibrierung wird geschrieben** (INA226-CAL-01): die Bibliothek lehnt ein Paar aus Shunt und Maximalstrom
  über 81,9 mV Vollausschlag ab und schreibt dann keine Kalibrierung; die Firmware hat den Rückgabewert ignoriert
  (0,1 Ohm mit 20 A ergab 0 mA). Jetzt senkt `--shunt` den Maximalstrom passend, `--imax` lehnt ein zu großes Paar
  ab, ein gespeichertes Paar wird beim Start geklemmt. **Nicht auf Hardware geprüft** (kein INA226 am Bench).
- **Web-Etikett vBUS** ohne das verirrte `<` (WEB-INA-01).

### 3. Gateway

- **Halten bei Serverausfall** (#1307, `src/udp_hold.h`): ein Gateway hält eigene und gehörte Textnachrichten bis
  20 Minuten in einem Ring (1 KB auf klassischem ESP32, 6 KB auf S3 und RAK4631) und sendet sie nach der Rückkehr des
  Servers einmal mit derselben ID nach. Telemetrie, ACK, Ping, RM1, MCP, SET, CET und SOTA werden nicht gehalten.
  Es gibt keinen Schalter; das Halten ist für Gateways immer an.
- **Registerabfrage über EXTUDP** (#1308, `src/ext_reg.h`): `{"type":"reg","q":"SN"}` liefert ein Konfigurationsregister
  wie über BLE, `{"q":"all"}` alle. Geheimnisse werden herausgefiltert, Antworten gehen nur an den eingestellten
  Partner, höchstens eine Anfrage je Sekunde.

### 4. Fernverwaltung (#1303)

- Die Karten der Weboberfläche (seit #1281 upstream) bekamen Ablehnungen, weil die Knotenseite fehlte. Jetzt liest
  und setzt RM1 die Pins (`pins`, `gpio`; der Button-Pin nur lesend), und die Texte passen zur Wartezeit von 171 s.

### 5. `tools/hmac_connect.py`

- Unter Windows gibt es `signal.pthread_kill` nicht: schloss der Knoten die Verbindung, starb der Empfangsthread mit
  einem Traceback und die Sitzung hing bis Enter oder Strg-C. Jetzt weckt ein Konsolen-Strg-C-Ereignis den
  Hauptthread, ohne Konsole `_thread.interrupt_main()`.
- Ein Kodierfehler auf stdout (Codepage ohne die Zeichen) beendete den Empfangsthread still; jetzt werden die
  Zeichen ersetzt.
- Ein zweites SIGTERM oder SIGHUP während des Aufräumens übersprang das Schließen von Socket und Log; jetzt werden
  diese Signale beim Aufräumen ignoriert.
- Unter Windows nicht ausprobiert (kein Windows-Rechner); im Test wird das fehlende `pthread_kill` simuliert.

### Nicht geprüft

- **Der PR-Zweig selbst:** die Bench-Zahlen oben stammen von Fork-Images. Diese enthalten zusätzlich Patches an
  Bibliotheken (HTTP-Client, NimBLE, mDNS, DHCP-Server, W5100S), die dieser PR nicht mitbringt. Der PR-Zweig wurde für
  alle Board-Envs gebaut und auf Heltec V3 und T-Beam einmal mit Download und Ablage geprüft. [noch auszuführen]
- **Boards ohne Messung:** T-Beam Supreme, Wireless Tracker, E22-Varianten, Heltec V4.
- **T-Beam bleibt knapp:** der größte Block beim Start des Laufs lag zwischen 34,8 und 57,3 KB für 33,4 KB Bedarf.
- **INA226:** nur Host-Tests.
- **hmac_connect.py unter Windows:** nur simuliert.

---

## Offene Punkte vor dem Einreichen (nicht Teil des PR-Texts)

- Issue-Nummern für Auto Update (Speicher, AU-29) und INA226 nach dem Anlegen eintragen
  ([upstream-issue-drafts-20261125.md](upstream-issue-drafts-20261125.md)).
- #1306: der Commit `304414f1` enthält auch QA-RX-07 und QA-RX-02, die das Issue nicht nennt (scope E2).
- #1307: das Issue verspricht einen Schalter `--udphold`; der Fork hat ihn nur in Messbuilds (scope E3). Issue-Text
  anpassen oder einen Statusbefehl schreiben.
- #1308: ob EXT-PEER-01 (Partnerwechsel wirkt ohne Neustart) mitgeht (scope E4).
- PR-Zweig bauen (alle Envs) und auf Heltec V3 und T-Beam einmal Download und Ablage messen (scope E8).
