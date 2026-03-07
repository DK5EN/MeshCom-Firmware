# Release Notes -- MeshCom Firmware v4.35n (2026-03-07)

Zusammenfassung aller Aenderungen gegenueber dem Upstream-DEV-Branch (Branches `pr/dev-bugfix`, `pr-dev-bugfix-csma`).

---

## Dependency Updates

### RadioLib 7.1.2 -> 7.6.0
- **CAD-Optimierung** fuer bessere Erkennungsrate (v7.2.0)
- **Fix finishReceive** cleared IRQ-Flags zu frueh auf SX126x (v7.3.0)
- **Optimierte PA-Konfigurationstabelle** fuer SX1262/SX1268 (v7.5.0)
- **Fix LoRa Header IRQ check logic** (v7.6.0)
- Breaking-Change-Analyse: Keine Auswirkungen auf MeshCom (alle Breaking Changes betreffen LoRaWAN oder nicht genutzte APIs)

---

## Bugfixes

### Retransmit-Ringpuffer: Nachrichtenverlust behoben
- **updateRetransmissionStatus()** scannt wieder alle MAX_RING Slots statt nur den eingeschraenkten [iRead..iWrite)-Bereich. Bereits konsumierte Slots enthalten noch gueltige Retry-Daten und muessen weiter erreichbar sein.
- **Text-Nachrichten-Slots** werden nach TX nicht mehr sofort geloescht. Nur Fire-and-Forget-Eintraege (Relay/ACK/Beacon mit Status 0xFF) werden direkt freigegeben; Text-Nachrichten bleiben fuer Retry-Tracking erhalten.
- **ACK-Empfang gibt Slots frei**: Beim Empfang eines passenden ACK (Text oder binaer 0x41) wird der zugehoerige Ringpuffer-Slot korrekt zurueckgesetzt. Zuvor blieben Slots belegt und blockierten den Puffer.
- **Binaeres ACK (0x41)** stoppt jetzt die Retransmission im Ringpuffer -- zuvor wurde nur der Phone-Notification-Status gesetzt, Broadcast/Group-Retries liefen endlos weiter.

### WiFi-Stabilitaet
- **NTP-Fehler**: Ein fehlgeschlagenes `timeClient.forceUpdate()` reisst nicht mehr die gesamte WiFi-Verbindung ab (kein `WiFi.disconnect()` / `Udp.stop()` mehr). UDP-Dienste bleiben stabil.
- **Ping durch Status ersetzt**: Blockierendes `Ping.ping()` (bis zu 5s Main-Loop-Blockade) durch nicht-blockierendes `WiFi.status()` ersetzt. Behebt T-Deck WiFi-Abbrueche nach ~10 Minuten.
- **Heartbeat-Timeout**: Zweistufiges System -- Warnung bei 35s mit WiFi-Status-Check, tatsaechlicher Reset erst bei 65s und nur bei tatsaechlich getrennter Verbindung. Log-Analyse zeigte 22 unnoetige WiFi-Resets in 4 Stunden bei durchgehender Konnektivitaet.
- **Boot-Timeout**: WiFi-Verbindungs-Timeout beim Boot von 30s auf 15s reduziert. Bei erstem Fehlschlag: Hardware-Reset (`WiFi.disconnect(true,true)`) mit sofortigem Retry statt 5-Minuten-Wartezeit.
- **Redundanter Reconnect entfernt**: 5s-Reconnect-Block konkurrierte mit dem Ping-Watchdog und verursachte permanente WiFi-Drops.

### BLE-Stabilitaet
- **Race Condition behoben**: NimBLE-Callbacks (Core 0) kommunizieren jetzt ueber FreeRTOS Queue statt direktem Cross-Core-Aufruf. Main Loop (Core 1) verarbeitet BLE-Daten via `xQueueReceive()`. Behebt Crashes bei gleichzeitigem BLE-Send/Receive.
- **Toter Verschluesselungs-Check entfernt**: `onAuthenticationComplete()` trennte unverschluesselte Clients, obwohl Verschluesselung explizit deaktiviert war (`setSecurityAuth(false,false,false)`). Dead Code entfernt.

### UDP-Zuverlaessigkeit
- **JSON-Validierung (ExtUDP)**: Null-Pointer-Schutz und Feldpruefung fuer `type`, `dst`, `msg` -- verhindert Crashes bei fehlerhaftem JSON.
- **Socket-Reset-Schutz**: Nach `resetExternUDP()` und `resetMeshComUDP()` wird sofort returned -- verhindert `endPacket()` auf geschlossenem Socket.
- **Ring-Buffer Overflow**: Manuelles `udpWrite++` durch `addRingPointer()` ersetzt -- verhindert blinde Pointer-Fortschaltung bei vollem Puffer.
- **Sauberer Reset**: `resetMeshComUDP()` setzt jetzt alle Flags korrekt (`node_hasIPaddress=false`, `iWlanWait=0`, `web_timer=0`) und vermeidet Reconnect-Loops.

### RX-Timeout-Timing
- **iReceiveTimeOutTime** wird nach RX-Timeout-Restart korrekt re-initialisiert. Zuvor blieb der Wert bei 0, der naechste Timeout-Zyklus feuerte nie (Pruefung `> 0` immer false -> 25-38s RADIO_SILENT-Luecken).
- **Timeout nur im Idle-Pfad**: Reset erfolgt nur beim Rueckfall in den Idle-Zustand, nicht nach jedem erfolgreichen RX.

### RX-Blindspot bei CRC- und unbekannten Fehlern (NEU)
- **CRC-Error-Pfad**: RSSI/SNR/FreqError werden jetzt VOR `startReceive()` gesichert (Register sind nach RX-Restart ungueltig). Hex-Dump des beschaedigten Payloads fuer Diagnose.
- **"Other error" Blindspot behoben**: Im "other error"-Pfad von `checkRX()` fehlte der RX-Restart komplett -- das Radio blieb nach unbekannten Fehlern im Standby und empfing nichts mehr bis zum naechsten TX-Zyklus. RX-Restart + Diagnose-Logging hinzugefuegt.

### Atomarer RX-Restart nach TX (NEU)
- **Reihenfolge korrigiert**: `radio.startReceive()` wird jetzt VOR dem Reaktivieren des ISR-Callbacks aufgerufen. Zuvor konnte ein ISR zwischen `setPacketReceivedAction()` und `startReceive()` feuern, obwohl das Radio noch nicht im RX-Modus war.
- **DIO1-Flanken-Recovery**: Nach jedem RX-Restart wird der DIO1-Level geprueft. Falls HIGH aber `receiveFlag` nicht gesetzt, wurde eine steigende Flanke verpasst -- manuelles Recovery.

### Doppelter APRS2SOTA-Check entfernt (NEU)
- Exakt duplizierte `strcmp(destination_call, "APRS2SOTA")` Pruefung entfernt (toter else-Zweig, nie erreichbar).

---

## Features

### CSMA/CA -- Carrier Sense mit Hardware-CAD
Ersetzt blinde Sende-Delays durch echte Kanalueberwachung mittels Hardware Channel Activity Detection.

- **Konstanten und Zustandsvariablen**: Slot-basierter exponentieller Backoff mit konfigurierbaren Timing-Parametern (CSMA_BASE 4500/3000/2000ms, CSMA_SLOT_SIZE 35ms, max. 3 Versuche).
- **ESP32 TX-Gate** (SX127x + SX126x): Hardware-CAD via `radio.scanChannel()`, Double-Check bei erstem Busy-Ergebnis (filtert Fehlalarme), dynamisches `csma_timeout` statt fixem `RECEIVE_TIMEOUT`.
- **nRF52 TX-Gate** (RAK4630): Async CAD via `Radio.StartCad()` + `OnCadDone()`-Callback mit 100ms Safety-Timeout. Gleiche Double-Check- und Backoff-Logik wie ESP32.
- **Alte Blind-Delays entfernt**: `cmd_counter` und `tx_waiting` durch CAD-basierte Entscheidung ersetzt.

### ESP32 Header Detection via IRQ-Register-Polling (NEU)
Bringt ESP32 auf Paritaet mit dem NRF52-`OnHeaderDetect`-Callback.

- **IRQ-Polling vor CAD-Scan**: `radio.getIrqFlags()` prueft SX1262 IRQ-Status-Register per SPI (ohne Flags zu clearen). Bei gesetztem HEADER_VALID oder PREAMBLE_DETECTED wird `is_receiving = true` gesetzt und der TX-Versuch abgebrochen.
- **PREAMBLE_DETECTED als IRQ-Flag aktiviert**: `startReceive()` an allen 10 Stellen mit erweiterter IRQ-Maske aufgerufen. DIO1-Maske bleibt unveraendert (nur RX_DONE auf DIO1).
- **Safety Net**: Nach jedem RX-Restart wird DIO1-Level geprueft -- falls HIGH und `receiveFlag` nicht gesetzt, wurde eine RX_DONE-Flanke verpasst (Recovery).
- **Board-Kompatibilitaet**: Kompatibilitaets-Macro fuer LORA_DIO1 (E22_DIO1, RADIO_DIO1_PIN, PIN_LORA_DIO_1).

### Mesh-Relay: Loop-Erkennung und Hop-Counter-Schutz
- **Loop-Detection**: Vor dem Relay wird geprueft, ob der eigene Callsign bereits in `msg_source_path` vorkommt. Falls ja, wird das Relay uebersprungen. Verhindert endlose Relay-Zyklen bei >10 aktiven Nodes.
- **Hop-Counter Safeguard**: `max_hop` wird mit `0x0F` maskiert, damit Flag-Bits (server/track/app_offline/mesh) bei Hop-Decrement nicht korrumpiert werden.

### Deduplizierungs-Puffer vergroessert
- **Eigener MAX_DEDUP_RING** (60 Slots) entkoppelt vom TX-Ringpuffer (MAX_RING). Bei 9+ gleichzeitig sendenden Nodes erzeugt jede Broadcast-Nachricht bis zu 10 IDs (1 MSG + 9 ACKs) -- der alte 30-Slot-Puffer war zu schnell voll, was zu falscher Duplikat-Erkennung fuehrte.

### Heartbeat-Loss-Detection (ESP32)
- ESP32 erkennt jetzt Server-Ausfall ueber Heartbeat-Timeout (wie nRF52). Bei Ueberschreitung wird `resetMeshComUDP()` aufgerufen statt endlos in toter UDP-Verbindung zu haengen.

### nRF52/RAK4630: Heap-Monitoring
- FreeRTOS `xPortGetFreeHeapSize()` liefert auf nRF52 (heap_3/libc-malloc) falsche Werte. Jetzt wird `mallinfo()` + `sbrk()`-Gap zum Stack-Pointer fuer akkurate Heap-Auswertung verwendet.

### nRF52/RAK4630: LoRa-Port
- ESP32-Verbesserungen fuer das Callback-basierte nRF52-Radiomodell portiert: `Radio.Rx()` zu Beginn von `OnRxDone()` (minimiert RX-Blindfenster), `is_receiving`-Guard im Timeout-Handler, RING_STATUS-Reports alle 30s, Payload-Safety-Copy.

### RX-Payload Double-Buffer (RAK4630) (NEU)
- Einfacher statischer `rxPayloadCopy`-Buffer durch Double-Buffer ersetzt. Verhindert Ueberschreiben von noch nicht verarbeiteten Paketen bei schnell aufeinanderfolgenden Empfaengen. Debug-Logging fuer Buffer-Wechsel und Overwrite-Erkennung.

---

## Refactoring

### Magic Numbers durch benannte Konstanten ersetzt (NEU)
- `MSG_TYPE_ACK` (0x41), `MSG_TYPE_TEXT` (0x3A), `MSG_TYPE_POSITION` (0x21), `MSG_TYPE_HEY` (0x40) in `configuration_global.h` definiert.
- `RING_STATUS_READY` (0x00), `RING_STATUS_SENT` (0x01), `RING_STATUS_DONE` (0xFF) fuer Ringpuffer-Slot-Status.
- Alle 26+ Message-Type- und 15+ Ringpuffer-Status-Vorkommen in `lora_functions.cpp` und `esp32_main.cpp` ersetzt.

### Ringpuffer-Helper-Funktionen extrahiert (NEU)
- `extractRingMsgId(slot)`: 4-Byte Message-ID aus Ringpuffer-Slot extrahieren (ersetzt 3x duplizierten Inline-Code).
- `findAndStopRingSlot(msgId)`: Sucht Slot per msg_id, setzt Status auf RING_STATUS_DONE, cleared retryCount. Ersetzt Loop 1 (binaeres ACK) und Loop 3 (DM-ACK).

### OnRxDone() aufgeteilt -- Phase 1 (NEU)
- `handleACK()` als eigene statische Funktion extrahiert (~90 Zeilen). OnRxDone() dispatcht ueber `handleACK()` mit Early-Return bei ACK-Paketen.
- Regulaerer Nachrichtenpfad (~700 Zeilen) bleibt inline (Phase 2 in separatem Commit geplant).

### Volatile-Deklaration fuer ISR-geteilte Flags (NEU)
- `is_receiving`, `tx_is_active`, `bSetLoRaAPRS`, `ch_util_rx_start`, `ch_util_tx_start`, `ch_util_rx_accum`, `ch_util_tx_accum` als `volatile` deklariert. Ohne `volatile` kann der Compiler Reads in Tight-Loops wegoptimieren.

---

## Debug & Logging

### Einheitliche State-Machine-Tags
- **[MC-SM]**-Tags fuer alle State-Transitions auf ESP32 und nRF52 (inkl. OnRxDone/OnTxDone-Callbacks). Ermoeglicht automatisierte Log-Analyse und Timeline-Rekonstruktion.
- **[MC-DBG]**-Ausgaben mit konsistentem Format und State-Kontext auf beiden Plattformen.
- Doppelte Log-Eintraege (z.B. redundantes `RX_PROCESS -> RX_LISTEN` nach `checkRX()`) entfernt.

### Channel Utilization Metriken
- Firmware trackt RX/TX-Airtime pro 10s-Fenster und gibt `[MC-DBG] CHANNEL_UTIL rx=Xms tx=Yms util=Z%` aus (ESP32 + nRF52).
- ESP32: RX-Airtime via `radio.getTimeOnAir(ibytes)` berechnet (kein OnHeaderDetect-Callback verfuegbar).

### RING_OVERFLOW-Differenzierung
- `addRingPointer()` gibt jetzt den Puffernamen aus ("tx", "raw_rx", "phone"). TX-Overflow (kritisch) ist nicht mehr ununterscheidbar von raw_rx-Cycling (kosmetisch).
- raw_rx Ring-Overflow-Meldung unterdrueckt (normales WebUI-Verhalten, kein Fehler).

### Ring Buffer Diagnostic Logging (NEU)
- **addTxRingEntry()**: Zentraler TX-Ring-Write-Helper mit msg_id, Slot-Nr, Typ, Status und Fuellstand-Logging fuer alle 14 Schreibzugriffe (Labels: rx_ack_fwd, rx_relay, udp_rx, user_msg, beacon, etc.).
- **RING_DROP-Warnung**: Wird immer geloggt wenn ein aktiver Slot ueberschrieben wird (inkl. msg_id, Typ, retryCount).
- **RING_TX_READ** in doTX(): msg_id beim Senden geloggt.
- **RX_DEDUP_ADD/DUP/NEW**: Deduplizierungs-Entscheidungen nachvollziehbar.
- **RING_STATUS** um Dedup-Fuellstand erweitert.
- Alles hinter `bLORADEBUG` geschuetzt (ausser RING_DROP und RX_OTHER_ERROR -- immer aktiv, da kritisch).

---

## Tools

### serial_monitor.py
- Neues Echtzeit-Monitoring-Tool: parst Firmware-Output, farbcodierte Events, Inline-Activity-Indicator (`.RrCTt` fuer States, `x!` fuer Fehler).
- **CHANNEL_BUSY vs. RADIO_SILENT**: Unterscheidet aktive Kanalnutzung (legitime Kollisionen) von echtem Funkstille-Zustand. Verhindert Fehlalarme.
- Ctrl+C-Handler und `RE_RX_TIMEOUT_FIRE`-Regex korrigiert.

### loganalyse.sh
- Umfassendes Log-Analyse-Skript: Metriken, Fehlerzaehler, Timing-Statistiken aus Firmware-Logs.
- Angepasst an `serial_monitor.py`-Ausgabeformat (Zeitstempel-Prefix, neue Feldnamen `rssi=/snr=/size=`).
- Single-Pass-AWK fuer RSSI/SNR-Korrelation, adaptive RX_TIMEOUT_FIRE-Statistiken, BUFFER_DROPS, CAD_STATS.
- BSD-awk-kompatibel (keine gawk-Abhaengigkeit).

---

## Betroffene Dateien

| Datei | Bereich |
|-------|---------|
| `platformio.ini` | RadioLib 7.6.0 |
| `variants/t_deck_pro/platformio.ini` | RadioLib 7.6.0 |
| `variants/t5_epaper/platformio.ini` | RadioLib 7.6.0 |
| `src/configuration_global.h` | Konstanten, Version, Message-Types, Ring-Status |
| `src/esp32/esp32_main.cpp` | WiFi, BLE, CSMA/CA, IRQ-Polling, Atomarer RX-Restart, Logging, Timeout |
| `src/nrf52/nrf52_main.cpp` | CSMA/CA, Logging, Heap-Monitoring |
| `src/lora_functions.cpp` | Retransmit, ACK, CAD, handleACK(), Helper-Funktionen, Double-Buffer, nRF52-Port |
| `src/loop_functions.cpp` | Dedup-Puffer, CSMA-Konstanten, volatile Flags, addTxRingEntry() |
| `src/loop_functions.h` | addTxRingEntry() Deklaration |
| `src/loop_functions_extern.h` | volatile extern-Deklarationen |
| `src/batt_functions.cpp` | volatile extern Fix |
| `src/udp_functions.cpp` | UDP-Reset, NTP, Heartbeat, addTxRingEntry() |
| `src/extudp_functions.cpp` | JSON-Validierung, Socket-Schutz |
| `src/phone_commands.cpp` | BLE Queue-Integration |
| `src/aprs_functions.cpp` | Loop-Detection, Hop-Counter |
| `tools/serial_monitor.py` | Echtzeit-Monitor |
| `tools/loganalyse.sh` | Log-Analyse |

## Stable Release of MeshCom Firmware


### Supported Hardware:

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

please use webflasher https://esptool.oevsv.at for upgrade from 4.30q:
t_deck.bin
t_deck_plus.bin

newer version able to upgrade via OTA-Flasher


[MeshCom Changelog](https://icssw.org/meshcom-versionen/)

[MeshCom@ICSSW Projektseite](https://icssw.org/meshcom/)
