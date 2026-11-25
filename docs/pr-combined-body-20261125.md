**Dieser PR behebt, dass Auto Update auf ESP32 ohne PSRAM nach längerer Laufzeit mit "Not enough free memory"
abbricht und dass der T-Beam beim Löschen des Ablagebereichs neu startet. Dazu kommen kleine Korrekturen im Empfangspfad und am INA226, das Halten von Textnachrichten bei Serverausfall, die
Registerabfrage über EXTUDP, die Knotenseite der Fernverwaltungskarten, zwei T-Deck-Korrekturen, ein öffentlicher
Hostname für die Weboberfläche (#1323) und ein Fix für `tools/hmac_connect.py`.**

Jeder Teil liegt in eigenen Commits und lässt sich einzeln zurücknehmen. Nichts davon ändert das Funkformat.

Closes #1303, Closes #1305, Closes #1306, Closes #1307, Closes #1308, Closes #1323, Closes #1325, Closes #1326,
Refs #1309 (die strengere Safeboot-Prüfung ist nicht enthalten)

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
  Lauf-Puffer (24.11.2026). Nacht 24./25.11.: drei Läufe im Abstand von drei Stunden, 3 von 3 abgelegt in 10,9 bis
  11,4 s, kein Neustart.
- T-Beam v1.2: 10 von 10 Web-Installationen ohne Watchdog (24.11.2026); mit dem Stand dieses PRs Download und
  Ablage, Löschen 6,0 s, längste Idle-Lücke 362 ms, niedrigster freier Heap im Lauf 11.332 Byte. Nacht: 3 von 3 abgelegt in 13,6 bis 13,7 s, kein Neustart; zweimal
  fehlte nach dem Download der Speicher für die Probe-Entpackung, sie wurde wie vorgesehen auf den Prüfstart beim
  Installieren verschoben.
- T-Deck Plus (PSRAM): Download und Ablage, 1.617.579 Byte in 20,7 s (PSRAM-Weg, kein Lauf-Puffer). Nacht: 3 von 3
  abgelegt in 20,6 bis 22,0 s, kein Neustart.

### 2. Kleine Korrekturen

- **Parametertabelle der Setup-Seite im Flash** (#1305, RAM-WSP-01): spart 880 Byte RAM am RAK4631 und 1.008 Byte am
  Heltec V3 (gemessen am Build). Seit dem Fork-Release vom 20.11.2026 im Feld.
- **RX-Pfad** (#1306):
  - Ein Störrahmen beendet keine laufende Wiederholung mehr (QA-RX-03).
  - Die RX-Kopie trägt keine Bytes eines früheren Rahmens mehr (BL-02).
  - Die ID in der `:rej`-Antwort stimmt (Versatz korrigiert).
  - Ein Rahmen mit einem Sonderziel als Absender (`*`, `H`, `HG`, `TEST`, `TESTER`) wird verworfen (QA-RX-07).
  - Eine Hop-Grenze aus `{SET}` wird nicht mehr bei jedem Rahmen in den Flash geschrieben: der erste Wechsel nach dem
    Start sofort, weitere höchstens alle 10 Minuten (QA-RX-02). Ein Neustart innerhalb dieses Fensters verliert den
    noch nicht geschriebenen Wert.
  - Seit 06.11.2026 im Feld.
- **INA226-Kalibrierung wird geschrieben** (INA226-CAL-01): die Bibliothek lehnt ein Paar aus Shunt und Maximalstrom
  über 81,9 mV Vollausschlag ab und schreibt dann keine Kalibrierung; die Firmware hat den Rückgabewert ignoriert
  (0,1 Ohm mit 20 A ergab 0 mA). Jetzt senkt `--shunt` den Maximalstrom passend, `--imax` lehnt ein zu großes Paar
  ab, ein gespeichertes Paar wird beim Start geklemmt. **Nicht auf Hardware geprüft** (kein INA226 am Bench).
- **Web-Etikett vBUS** ohne das verirrte `<` (WEB-INA-01).

### 3. Gateway

- **Halten bei Serverausfall** (#1307, `src/udp_hold.h`): ein Gateway hält eigene und gehörte Textnachrichten bis
  20 Minuten in einem Ring (1 KB auf klassischem ESP32, 6 KB auf S3 und RAK4631, nichts auf T114 und T-Echo, die
  kein Netz haben) und sendet sie nach der Rückkehr des
  Servers einmal mit derselben ID nach. Telemetrie, ACK, Ping, RM1, MCP, SET, CET und SOTA werden nicht gehalten.
  Es gibt keinen Schalter; das Halten ist für Gateways immer an.
- **Registerabfrage über EXTUDP** (#1308, `src/ext_reg.h`): `{"type":"reg","q":"SN"}` liefert ein Konfigurationsregister
  wie über BLE, `{"q":"all"}` alle. Geheimnisse werden herausgefiltert, Antworten gehen nur an den eingestellten
  Partner, höchstens eine Anfrage je Sekunde.
- **Neuer EXTUDP-Partner wirkt sofort** (EXT-PEER-01): eine geänderte oder gelöschte `--extudpip` gilt ohne Neustart;
  ein leerer Partner öffnet keinen Socket, ein ungültiger hält nicht am alten fest.

### 4. Fernverwaltung (#1303)

- Die Karten der Weboberfläche (seit #1281 upstream) bekamen Ablehnungen, weil die Knotenseite fehlte. Jetzt liest
  und setzt RM1 die Pins (`pins`, `gpio`; der Button-Pin nur lesend), und die Texte passen zur Wartezeit von 171 s.

### 5. T-Deck

- **Unterhaltungs-Tabs verschwinden nicht mehr** (TD-35): mit WLAN läuft das T-Deck bei 25 bis 41 KB freiem internem
  Heap, unter der Grenze von 40 KB der Nachrichten-Kürzung. Die Kürzung löschte einen Tab, den sie leer gekürzt hatte;
  der öffentliche Tab verschwand, der letzte DM-Tab blieb. Jetzt kürzt sie nur Nachrichten.
- **Kein grauer Schatten unter Knöpfen im hellen Theme** (TD-36): LVGLs helles Standard-Theme legt einen Schatten unter
  jeden Knopf; unter der fast weißen Füllung blieb nur der Schatten sichtbar. Schattenbreite 0, dunkles Theme
  unverändert.

### 6. Öffentlicher Hostname für die Weboberfläche (#1323)

- Der Web-Guard lehnt jeden Host ab, der keine IP, kein einteiliger Name und kein LAN-Suffix ist; hinter einer
  Portweiterleitung war der Knoten über seinen DynDNS-Namen nicht erreichbar (403). `--webhost <name>|none` (ohne
  Argument zeigt es den Wert), Feld auf der Setup-Seite und `/setparam/?webhost=`: genau dieser Name wird akzeptiert,
  ohne Wildcard und ohne Suffix-Vergleich. Origin-, X-MC- und Sec-Fetch-Prüfung bleiben. Ohne Namen gilt das alte
  Verhalten. Neue Einstellung `node_webhost`.

### 7. `tools/hmac_connect.py`

- Unter Windows gibt es `signal.pthread_kill` nicht: schloss der Knoten die Verbindung, starb der Empfangsthread mit
  einem Traceback und die Sitzung hing bis Enter oder Strg-C. Jetzt weckt ein Konsolen-Strg-C-Ereignis den
  Hauptthread, ohne Konsole `_thread.interrupt_main()`.
- Ein Kodierfehler auf stdout (Codepage ohne die Zeichen) beendete den Empfangsthread still; jetzt werden die
  Zeichen ersetzt.
- Ein zweites SIGTERM oder SIGHUP während des Aufräumens übersprang das Schließen von Socket und Log; jetzt werden
  diese Signale beim Aufräumen ignoriert.
- Unter Windows nicht ausprobiert (kein Windows-Rechner); im Test wird das fehlende `pthread_kill` simuliert.

### Getestet

- **Builds (R2):** `pio run` über alle 33 Envs aus `default_envs` auf diesem Zweig: 33 von 33 gebaut (25.11.2026),
  ohne neue Warnungen aus `src/`. Ohne den Update-Kanal des Forks gebaut.
  - RAM: der Halte-Ring belegt 6.144 Byte auf ESP32-S3 und RAK4631 und 1.024 Byte auf klassischem ESP32; die
    Parametertabelle im Flash spart rund 1 KB. T-Beam nach dem Auto-Update-Teil: `dram0_0_seg` 192 Byte weniger als
    `dev`, IRAM unverändert.
  - Flash RAK4631: 83,6 % (vorher 82,8 %).
- **Bench mit genau diesem Zweig** (25.11.2026, gebaut mit älterem Versionsstempel, Kanal `prod`): Prüfung und
  Download mit Ablage des offiziellen `v4.40a.11.22` von icssw-org, nicht installiert.
  - Heltec V3 (DK5EN-1): 1.180.497 Byte in 12,4 s, Löschen 2,6 s, Probe-Entpackung im Lauf-Puffer, keine
    fehlgeschlagene Allokation.
  - T-Beam v1.2 (DK5EN-92): erst kein Block für den Lauf-Puffer (32.756 Byte frei am Stück), Neustart des Lauf-Tasks
    wie vorgesehen, dann Puffer gesetzt, 1.240.686 Byte abgelegt, Probe-Entpackung im Lauf-Puffer.
  - Danach hat Safeboot das abgelegte Image beim nächsten OTA-Aufruf installiert: der Übergabeweg lief also auch.
- **Host-Tests des Forks** (Unity und pytest) über diesen Zweig gelegt: alle Suiten zu den Teilen dieses PRs laufen.
  Rot sind nur Suiten für Teile, die bewusst fehlen (Bibliotheks-Patches, Update-Kanal des Forks, Messbefehle). Die
  Tests selbst bleiben im Fork; upstream hat dafür keine Test-Umgebung.

### Auf und ab (F-12, F-13)

- **Nach dem Update:** keine neue automatische Aussendung, keine geänderten Vorgaben. Neue Einstellungen
  `node_audelay` (3), `node_auday` (aus), `node_autime` (03:00) und `node_webhost` (leer) mit eigenen Schemazeilen;
  `FLASH_STRUCT_VERSION` unverändert, alle anderen Einstellungen bleiben.
- **Zurück auf ein älteres Release:** die neuen Einstellungen werden ignoriert. Ein abgelegtes, aber noch nicht
  installiertes Update mit Prüfmarke (T-Beam, Prüfung beim Start) würde ein älteres Release ohne diese Prüfung an
  Safeboot übergeben. Das betrifft nur einen Knoten, der mit abgelegtem Image zurückgeflasht wird.
- **Funk und Rahmen (R4, F-01):** keine Änderung an einem On-Air-Format oder einer Länge. Neu auf EXTUDP ist nur die
  Antwort auf `{"type":"reg"}`, und nur an den eingestellten Partner.

### Nicht geprüft

- **Bibliotheks-Patches des Forks fehlen hier:** die Nachtläufe oben stammen von Fork-Images mit Patches an HTTP-Client,
  NimBLE, mDNS, DHCP-Server und W5100S, die dieser PR nicht mitbringt. Der Zweig selbst lief einmal je Board (siehe
  "Getestet"), nicht über eine Nacht. Ohne diese Patches gibt es im TLS-Client keine Prüfung des Zertifikatsdatums
  und kein TLS 1.2 als Minimum; `WiFiClientSecure` von `dev` hat beides auch nicht.
- **Boards ohne Messung:** T-Beam Supreme, Wireless Tracker, E22-Varianten, Heltec V4.
- **T-Beam bleibt knapp:** der größte Block beim Start des Laufs lag zwischen 32,8 und 57,3 KB für 33,4 KB Bedarf; der
  Neustart des Lauf-Tasks fängt das ab.
- **INA226, Halten bei Serverausfall auf dem RAK4631, `--webhost` mit echter Portweiterleitung:** INA226 nur
  Host-Tests; das Halten auf ESP32 am 24.11.2026 im Messbuild geprüft; `--webhost` auf einem T-Deck Plus im LAN.
- **hmac_connect.py unter Windows:** nur simuliert.

### Nicht enthalten

- Bibliotheks-Patches (NimBLE, HTTP-Client, mDNS, DHCP-Server, W5100S, lwIP), darunter die Prüfung auf ein NUL-Byte in
  der Download-URL der Release-Liste: eigener PR, wenn sie einzeln auf Hardware geprüft sind.
- Strengere Safeboot-Prüfung vor der Übergabe (#1309 bleibt offen).
- DNS-Abfrage für den EXTUDP-Partner ohne Blockieren der Hauptschleife. Hier nur: ein nicht auflösbarer Name wird
  höchstens alle 30 s erneut versucht.
- `--postime` ohne GPS-Modul (#1327): nur gemeldet.
- Fork-eigene Messbefehle und der Update-Kanal des Forks.
