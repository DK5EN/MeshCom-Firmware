# Release Notes -- MeshCom Firmware v4.35* (2026-03-16)

Nachrichtenprioritaet, Trickle-HEY, erweiterte Statistik,
APRS-Parser Hardening und diverse Bugfixes auf Basis von `oe1kbc_v4.35p`.

Kein On-Air-Change — alte Firmware empfaengt alle Pakete korrekt.

---

## Upstream-Sync 2026-03-16 (oe1kbc_v4.35p, zweite Runde)

### SDWrapper entfernt — externe SD-Bibliothek
- Gesamtes `src/SDWrapper/` Verzeichnis (122 Dateien, ~27.000 Zeilen) entfernt.
  Upstream verwendet jetzt die externe Library `sdcard_esp32` (GitHub: glucee/sdcard_esp32).
- T-Deck und T-Deck Plus: Library in `lib_deps` aufgenommen, SDWrapper aus Build-Filter ausgeschlossen.
- **Betroffene Dateien**: `src/SDWrapper/*` (geloescht), `platformio.ini`, `variants/t_deck/platformio.ini`, `variants/t_deck_plus/platformio.ini`

### BaseDisplay: virtual Destruktor
- `~BaseDisplay()` auf `virtual ~BaseDisplay()` geaendert — korrekte C++-Praxis fuer polymorphe Klassen.
- **Betroffene Datei**: `src/Displays/BaseDisplay/base.h`

### APRS: node_atxt direkt verwenden
- `encodeLoRaAPRS()` verwendete eine temporaere `String`-Variable mit Fallback `"(via MeshCom)"`. Upstream nutzt `meshcom_settings.node_atxt` direkt — spart String-Allokation.
- **Betroffene Datei**: `src/aprs_functions.cpp`

### ExtUDP: "none" Payload-Check
- Neue Validierung: JSON-Payload `"none"` wird abgefangen und mit Fehlermeldung abgebrochen.
- **Betroffene Datei**: `src/extudp_functions.cpp`

### GPS_Init() immer aufrufen
- `GPS_Init()` wird jetzt ohne `bGPSON`-Check aufgerufen (wie upstream). Unbenutzte Variable `connect_pending` entfernt.
- **Betroffene Datei**: `src/esp32/esp32_main.cpp`

### serial_monitor.py: Log-Verzeichnis
- Log-Verzeichnis von `/tmp/meshcom_monitor` auf `./meshcom_monitor` geaendert (upstream-konform).
- **Betroffene Datei**: `tools/serial_monitor.py`

---

## strcpy/strcat Buffer-Overflow Hardening (2026-03-16)

Alle unsicheren `strcpy()` und `strcat()` Aufrufe durch groessenbegrenzte Varianten ersetzt.

### SSID-Migration: Buffer-Overflow behoben (ESP32 + NRF52)
- `node_ossid[40]` wurde via `strcpy()` in `node_ssid[33]` kopiert — 7 Byte Overflow in angrenzende Struct-Felder.
- **Fix**: `strncpy()` mit `sizeof(node_ssid) - 1`.
- **Betroffene Dateien**: `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

### commandCheck(): unbegrenzter Parameter in 100-Byte Buffer
- `strcpy(vmsg[100], msg)` ohne Laengenpruefung — `msg` kann beliebig lang sein.
- **Fix**: `strncpy()` mit `sizeof(vmsg) - 1`.
- **Betroffene Datei**: `src/command_functions.cpp`

### Display-Text und Serial-Input: Bounds-Checks ergaenzt
- `pageLastTextLong1[25]` und `pageLastTextLong2[200]`: Source konnte laenger als Buffer sein (3 Plattformen: T-Deck Pro, E290, Tracker).
- `line_text[21]`: `strcat()`-Schleife ohne Bounds-Check beim ersten Concat.
- `msg_text[600]`: Serial-Input ohne Laengenpruefung (ESP32 + NRF52).
- **Fix**: `strncpy()`/`strncat()` mit `sizeof()`.
- **Betroffene Dateien**: `src/loop_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

---

## Upstream-Sync 2026-03-16 (oe1kbc_v4.35p)

### GPS: Log-Ausgaben nur bei aktiviertem GPS
- Baudrate-Erkennung und "not found"-Meldungen werden nur noch bei `bGPSON == true` ausgegeben. Reduziert Log-Spam wenn GPS deaktiviert ist.
- **Betroffene Datei**: `src/gps_functions.cpp`

### MHeard: ncount-Logik korrigiert
- Bei Nicht-HEY-Paketen (Position, Text) ist kein NCOUNT im Paket enthalten. `updateMheard()` uebernimmt jetzt den bestehenden Tabellenwert `mheardNCount[ipos]` statt den (leeren) Paket-Wert.
- `updateHeyPath()` wird nach `updateMheard()` aufgerufen, damit der ncount beim Path-Update bereits aktuell ist.
- **Betroffene Dateien**: `src/mheard_functions.cpp`, `src/lora_functions.cpp`

### NRF52: sendPosition mit vorheriger Position bei Richtungsaenderung
- Bei `posinfo_shot` (Richtungs-/Distanz-Trigger) wird die Position von `posinfo_prev_lat/lon` gesendet statt der aktuellen GPS-Position — damit wird die Kurve auf der Karte korrekt abgebildet.
- 15s Mindestabstand zwischen Positions-Sendungen (`posinfo_timer_min`) verhindert Spam.
- **Betroffene Datei**: `src/nrf52/nrf52_main.cpp`

---

## Neue Features

### Priority-Queue: 5-stufige Nachrichtenprioritaet
- **Problem**: DMs und ACKs standen hinter 5-10 Relay/HEY-Paketen in der FIFO-Queue.
  Bei 50-80% CAD-Busy-Rate fuehrte das zu 10-50s unnoetige Verzoegerung fuer menschliche Nachrichten.
- **Loesung**: 5 Prioritaetsstufen mit differenziertem CSMA-Backoff:
  - Prio 1 (Kritisch): ACK + persoenliche DM — CSMA-Backoff 2000ms
  - Prio 2 (Hoch): Gruppen + Broadcast "*" — CSMA-Backoff 3000ms
  - Prio 3 (Normal): Mesh-Relay — CSMA-Backoff 4000ms
  - Prio 4 (Niedrig): Position — CSMA-Backoff 4500ms
  - Prio 5 (Hintergrund): HEY — CSMA-Backoff 5000ms
- **Prio-Erkennung**: `getMessagePriority()` erkennt Typ via msg_type Byte,
  Relay via `RING_STATUS_DONE`, DM vs Gruppe via `CheckGroup()`.
- **Prio-Entnahme**: `getNextTxSlot()` scannt den Ring-Buffer nach hoechster Prioritaet.
  Bei gleicher Prio: FIFO-Reihenfolge (aeltester zuerst).
- **Prio-Drop**: Bei voller Queue wird der aelteste Eintrag der niedrigsten Prioritaet
  verworfen. ACKs und DMs werden nie zugunsten niedrigerer Pakete verworfen.
- **RAM-Aufwand**: 30+120 Bytes (`ringPriority[MAX_RING]` + `ringEnqueueTime[MAX_RING]`)
- **Betroffene Dateien**: `src/configuration_global.h`, `src/loop_functions_extern.h`,
  `src/loop_functions.cpp`, `src/lora_functions.h`, `src/lora_functions.cpp`,
  `src/esp32/esp32_main.cpp`

### Trickle-HEY: Adaptive HEY-Frequenz (ADR-001 Vorschlag C, RFC 6206)
- **Problem**: Bei 100 Nodes mit HEY alle 15 Min: ~7 HEY/Min = ein HEY alle 8.5s.
  Signifikanter Overhead bei 50-80% CAD-Busy.
- **Loesung**: Trickle-Algorithmus (RFC 6206 adaptiert) passt HEY-Intervall dynamisch an.
  Erwartete Einsparung: 60-70% weniger HEY-Traffic in stabilen Netzen.
- **Parameter**: Imin=30s, Imax=15min (wie bisher), k=2 (Redundanzschwelle)
- **Verhalten**:
  - Intervall verdoppelt sich bei Stabilitaet (30s -> 1m -> 2m -> 4m -> 8m -> 15m)
  - Reset auf 30s bei Topologieaenderung (neuer/verlorener Nachbar via getMheardCount())
  - Suppression: HEY wird unterdrueckt wenn >=2 konsistente Nachbar-HEYs gehoert
- **Betroffene Dateien**: `src/configuration_global.h`, `src/loop_functions_extern.h`,
  `src/loop_functions.cpp`, `src/lora_functions.cpp`,
  `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

### Erweiterte Statistik und Logging
- **[MC-STAT]** alle 5 Minuten: TX-Zaehler pro Prioritaet, Drops pro Prio, Preemption-Zaehler
- **[MC-PRIO]** alle 5 Minuten: Latenz avg/max pro Prioritaetsstufe (Queue-to-TX)
- **[MC-HWM]** alle 30 Minuten: Queue/CSMA High-Water-Marks, aktuelles Trickle-Intervall
- **serial_monitor.py**: Neue Alerts (PRIO1_STARVED >10s), Trickle-Summary in Intervall-Report
- **loganalyse.sh**: Neue Sektionen PRIORITY_DISTRIBUTION, TRICKLE_HEY, HIGH_WATER_MARKS
- **Betroffene Dateien**: `src/esp32/esp32_main.cpp`, `tools/serial_monitor.py`, `tools/loganalyse.sh`

---

## Bugfixes

### APRS-Parser Hardening

**Source/Destination-Path-Schleifen gegen korrupte Pakete abgesichert**
- Ein korruptes RF-Paket ohne `>` Trennzeichen liess die Source-Path-Schleife in `decodeAPRS()` den gesamten Restbuffer durchlaufen. Fuer jedes Byte wurde `String::concat()` aufgerufen — bei ~250 Bytes Muell fuehrte das zu 488ms Verarbeitungszeit (normal: 1-9ms) und Heap-Fragmentierung.
- Crash-Ursache fuer OE1KBC-12 und OE3MAG-12 am 15.03.2026.
- **Fix**: Beide Path-Parsing-Schleifen begrenzt auf max 120 Bytes. Non-Printable-Bytes (< 0x20 oder > 0x7E) brechen die Schleife sofort ab.

**Buffer-Overread bei FW-Sub-Version behoben**
- Nach dem Parsen von FW-Version und Hardware-Byte wurde `RcvBuffer[inext]` ohne Bounds-Check gelesen.
- **Fix**: `inext < rsize` Guard eingefuegt, analog zu den bestehenden Guards.

**Off-by-One in APRS-Telemetrie-Parsing**
- 15 Parsing-Schleifen in `decodeAPRSPOS()` und der Display-Funktion verwendeten `id <= PayloadBuffer.length()`, was Out-of-Bounds-Zugriff erlaubte.
- **Fix**: `id <= X.length()` durch `id < X.length()` ersetzt in allen 15 Schleifen.

- **Betroffene Dateien**: `src/aprs_functions.cpp`, `src/loop_functions.cpp`

### Heltec V2: GPS-Pin-Konflikt mit UART0

**GPS-Pins auf GPIO 12/13 umgelegt**
- GPIO 3 (GPS_TX_PIN = UART0 RX) belegte den USB-Serial-Empfang. Auch GPIO 23 (GPS_RX_PIN) war auf manchen Revisionen anderweitig belegt.
- **Fix**: GPS-Pins auf freie GPIOs umgelegt: `GPS_RX_PIN 13`, `GPS_TX_PIN 12`. UART0 (GPIO 3) bleibt frei fuer serielle Kommandos.
- **Betroffene Datei**: `variants/heltec_wifi_lora_32_V2/configuration.h`

**GPS-Init blockierte serielle Kommandoeingabe** (revertiert)
- Dieser Fix wurde rueckgaengig gemacht: upstream ruft `GPS_Init()` jetzt wieder bedingungslos auf. Siehe Upstream-Sync oben.

### T-Deck / T-Deck Plus: Boot-Hang durch ungueltige Current-Limit

- `CURRENT_LIMIT 240` ueberschritt den gueltigen Bereich (0-140 mA). Nach dem Uncomment des `setCurrentLimit()`-Aufrufs loeste der ueberhoethe Wert `RADIOLIB_ERR_INVALID_CURRENT_LIMIT` aus — Endlosschleife beim Boot.
- **Fix**: `CURRENT_LIMIT` von 240 auf 140 mA korrigiert (Maximum des SX1262/SX1268 OCP-Bereichs).
- **Betroffene Dateien**: `variants/t_deck/configuration.h`, `variants/t_deck_plus/configuration.h`

### MHeard-Datenkorruption

**memcpy Bounds-Check und strcmp→memcmp**
- `memcpy` in `updateMheard()` kopierte ohne Bounds-Check in den 10-Byte `mheardCalls`-Slot. Bei Callsigns >= 10 Zeichen wurde der Nachbar-Eintrag korrumpiert. `strcmp()` verglich ohne Laengenbegrenzung.
- Bug-Report Rainer: Gespeicherte MHeard-Eintraege zeigten falsche HW-Typen und gemixte Callsigns.
- **Fix**: `memcpy` mit `min(length, sizeof-1)` begrenzt. `strcmp` durch `memcmp` mit expliziter max-length ersetzt.

**ncount-Quelle, Comma-Parsing und Zeitfenster**
- `mheardNCount[ipos]` nutzte den stale Array-Wert statt des aktuellen Paket-Werts. Comma-Check blockierte gueltige Path-Payloads. `getMheardCount()` zaehlte nur Nodes der letzten Stunde statt 12h.
- **Fix**: Paket-Wert statt Array-Wert. Comma-Check entfernt. Zeitfenster auf 12h erweitert.

**Persistence Size-Check gegen Datenkorruption**
- Nach der MAX_MHEARD-Aenderung (20→120) hatten alte `mheard.dat`-Dateien eine andere Groesse. `loadMHeardPersistence()` las ohne Groessencheck — die gesamte MHeard-Tabelle wurde korrumpiert.
- **Fix**: File-Size-Check vor dem Laden. Bei Mismatch wird die alte Datei geloescht.

- **Betroffene Datei**: `src/mheard_functions.cpp`

### UTC-Offset-Rechenfehler in mheard Path-Anzeige

- Die Zeitberechnung verwendete `(60 * 60 + 24)` = 3624 statt `3600` Sekunden pro Stunde. `node_utcoff` (float) wurde auf `(int)` gecastet, was die Halbstunden-Praezision verlor.
- **Fix**: Formel an `(long)(meshcom_settings.node_utcoff * 3600.0)` angeglichen.
- **Betroffene Dateien**: `src/mheard_functions.cpp`, `src/web_functions/web_functions.cpp`

### Race Conditions: volatile durch std::atomic ersetzt

- Channel-Utilization-Counter und ISR-Flags waren als `volatile` deklariert. Auf dem nRF52 ist das `+=` Read-Modify-Write-Pattern nicht atomar — Lost Updates moeglich.
- **Fix**: `volatile` durch `std::atomic` ersetzt. `+=` durch `fetch_add()` + `exchange()` atomar ausgefuehrt.
- **Betroffene Dateien**: `src/loop_functions_extern.h`, `src/loop_functions.cpp`, `src/lora_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

### ExtUDP Null-Pointer-Schutz und Socket-Reset

- `inputJson["dst"]` gibt `nullptr` zurueck wenn das JSON-Feld fehlt — undefiniertes Verhalten bei Zuweisung an `String`. Nach `resetExternUDP()` fiel die Ausfuehrung durch zu `endPacket()` auf dem geschlossenen Socket.
- **Fix**: Null-Checks und Laengenvalidierung. `return` nach `resetExternUDP()`.
- **Betroffene Datei**: `src/extudp_functions.cpp`

### CONFFIN nach Config-Commands im gleichen Buffer senden

- CONFFIN wurde ueber den regulaeren toPhone-Buffer gesendet, waehrend Config-JSONs ueber den priorisierten ComToPhone-Buffer gingen. CONFFIN kam daher verzoegert oder gar nicht an.
- **Fix**: `sendConfigFinish()` nutzt jetzt `addBLEComToOutBuffer()`. Reihenfolge garantiert: Config-JSONs → MHeard → CONFFIN.
- **Betroffene Dateien**: `src/nrf52/nrf52_ble.cpp`, `src/nrf52/nrf52_main.cpp`, `src/esp32/esp32_main.cpp`, `src/command_functions.cpp`

### pow(2,n) durch Bit-Shift ersetzt

- ADC-Maximalwert mit `pow(2, resolution) - 1` berechnet. `pow()` gibt `double` zurueck — implizite Konversion zu `int` kann Rundungsfehler verursachen.
- **Fix**: `(1 << resolution) - 1`.
- **Betroffene Datei**: `src/batt_functions.cpp`

### printAsciiBuffer: Bounds-Check bei kurzen Buffern

- Die Debug-Funktion griff auf `buffer[0..3]` zu ohne vorher `len >= 4` zu pruefen.
- **Fix**: `if(len < 4) return;` als erstes Statement.
- **Betroffene Datei**: `src/loop_functions.cpp`

---

## Code-Alignment mit Upstream

Folgende Aenderungen wurden an `upstream/oe1kbc_v4.35p` angeglichen, um keine unnoetige Differenz zu erzeugen:

- **APRS IGate-Text**: Upstream verwendet `node_atxt` direkt im komprimierten LoRa-APRS Paket — unseren Kommentar-Block entfernt.
- **sendPosition Magic-Values**: `unsigned long` und `0xEEEE`/`0x9999` beibehalten wie Upstream.
- **GPS_REFRESH_INTERVAL**: Zurueck auf 5s (Upstream-konform).
- **sendMheard MESH/NCNT**: Kurts Weiterentwicklung uebernommen — MESH- und NCNT-Felder im BLE-JSON.

**Betroffene Dateien**: `src/aprs_functions.cpp`, `src/loop_functions.h`, `src/loop_functions.cpp`, `src/command_functions.cpp`, `src/mheard_functions.cpp`, `src/configuration_global.h`

---

## Kleinere Verbesserungen

- **POSINFO-Logs**: Tag von `[POSI]` auf `[POSINFO]` vereinheitlicht, alle 7 `printf`-Stellen um `getTimeString()` erweitert. (`src/loop_functions.cpp`)
- **Audio Log-Tag**: `[audi]` → `[audio]` in allen 16 Log-Ausgaben. (`src/esp32/esp32_audio.cpp`)

---

## Basis: MeshCom Firmware v4.35n (2026-03-11)

Diese Version baut auf v4.35n auf. Die vollstaendigen Release Notes der Basis-Version
(CSMA/CA, WiFi/BLE/UDP-Stabilitaet, Retransmit-Fixes, Refactoring, Tools)
sind unter dem Tag `v4.35n_20260311` dokumentiert.

---

## Supported Hardware

E22-DevKitC.bin (433 MHz)
E22_XML-DevKitC.bin (433 MHz)
E22_1268_S3-DevKitC.bin (433 MHz)
E22_1262-DevKitC.bin (868 MHz)
E22_1262_S3-DevKitC.bin (868 MHz)
heltec_wifi_lora_32_V2.bin
heltec_wifi_lora_32_V3.bin
heltec_wireless_stick_v3.bin
heltec_wireless_tracker.bin
ttgo-lora32-v21.bin
ttgo_tbeam.bin
ttgo_tbeam_SX1262.bin
ttgo_tbeam_SX1268.bin
ttgo_tbeam_supreme_l76k.bin
vision-master-e290.bin
wiscore_rak4631.bin
t_deck.bin
t_deck_plus.bin

Please use webflasher https://esptool.oevsv.at for upgrade from 4.30q:
t_deck.bin
t_deck_plus.bin

Newer version able to upgrade via OTA-Flasher.

[MeshCom Changelog](https://icssw.org/meshcom-versionen/)

[MeshCom@ICSSW Projektseite](https://icssw.org/meshcom/)
