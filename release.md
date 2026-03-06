# Release Notes — MeshCom Firmware v4.35n (Bugfix-CSMA)

Zusammenfassung aller Änderungen in den Branches `pr/dev-bugfix` und `pr-dev-bugfix-csma` gegenüber dem Upstream-DEV-Branch.

---

## Bugfixes

### Retransmit-Ringpuffer: Nachrichtenverlust behoben
- **updateRetransmissionStatus()** scannt wieder alle MAX_RING Slots statt nur den eingeschränkten [iRead..iWrite)-Bereich. Bereits konsumierte Slots enthalten noch gültige Retry-Daten und müssen weiter erreichbar sein.
- **Text-Nachrichten-Slots** werden nach TX nicht mehr sofort gelöscht. Nur Fire-and-Forget-Einträge (Relay/ACK/Beacon mit Status 0xFF) werden direkt freigegeben; Text-Nachrichten bleiben für Retry-Tracking erhalten.
- **ACK-Empfang gibt Slots frei**: Beim Empfang eines passenden ACK (Text oder binär 0x41) wird der zugehörige Ringpuffer-Slot korrekt zurückgesetzt. Zuvor blieben Slots belegt und blockierten den Puffer.
- **Binäres ACK (0x41)** stoppt jetzt die Retransmission im Ringpuffer — zuvor wurde nur der Phone-Notification-Status gesetzt, Broadcast/Group-Retries liefen endlos weiter.

### WiFi-Stabilität
- **NTP-Fehler**: Ein fehlgeschlagenes `timeClient.forceUpdate()` reißt nicht mehr die gesamte WiFi-Verbindung ab (kein `WiFi.disconnect()` / `Udp.stop()` mehr). UDP-Dienste bleiben stabil.
- **Ping durch Status ersetzt**: Blockierendes `Ping.ping()` (bis zu 5s Main-Loop-Blockade) durch nicht-blockierendes `WiFi.status()` ersetzt. Behebt T-Deck WiFi-Abbrüche nach ~10 Minuten.
- **Heartbeat-Timeout**: Zweistufiges System — Warnung bei 35s mit WiFi-Status-Check, tatsächlicher Reset erst bei 65s und nur bei tatsächlich getrennter Verbindung. Log-Analyse zeigte 22 unnötige WiFi-Resets in 4 Stunden bei durchgehender Konnektivität.
- **Boot-Timeout**: WiFi-Verbindungs-Timeout beim Boot von 30s auf 15s reduziert. Bei erstem Fehlschlag: Hardware-Reset (`WiFi.disconnect(true,true)`) mit sofortigem Retry statt 5-Minuten-Wartezeit.
- **Redundanter Reconnect entfernt**: 5s-Reconnect-Block konkurrierte mit dem Ping-Watchdog und verursachte permanente WiFi-Drops.

### BLE-Stabilität
- **Race Condition behoben**: NimBLE-Callbacks (Core 0) kommunizieren jetzt über FreeRTOS Queue statt direktem Cross-Core-Aufruf. Main Loop (Core 1) verarbeitet BLE-Daten via `xQueueReceive()`. Behebt Crashes bei gleichzeitigem BLE-Send/Receive.
- **Toter Verschlüsselungs-Check entfernt**: `onAuthenticationComplete()` trennte unverschlüsselte Clients, obwohl Verschlüsselung explizit deaktiviert war (`setSecurityAuth(false,false,false)`). Dead Code entfernt.

### UDP-Zuverlässigkeit
- **JSON-Validierung (ExtUDP)**: Null-Pointer-Schutz und Feldprüfung für `type`, `dst`, `msg` — verhindert Crashes bei fehlerhaftem JSON.
- **Socket-Reset-Schutz**: Nach `resetExternUDP()` und `resetMeshComUDP()` wird sofort returned — verhindert `endPacket()` auf geschlossenem Socket.
- **Ring-Buffer Overflow**: Manuelles `udpWrite++` durch `addRingPointer()` ersetzt — verhindert blinde Pointer-Fortschaltung bei vollem Puffer.
- **Sauberer Reset**: `resetMeshComUDP()` setzt jetzt alle Flags korrekt (`node_hasIPaddress=false`, `iWlanWait=0`, `web_timer=0`) und vermeidet Reconnect-Loops.

### RX-Timeout-Timing
- **iReceiveTimeOutTime** wird nach RX-Timeout-Restart korrekt re-initialisiert. Zuvor blieb der Wert bei 0, der nächste Timeout-Zyklus feuerte nie (Prüfung `> 0` immer false → 25–38s RADIO_SILENT-Lücken).
- **Timeout nur im Idle-Pfad**: Reset erfolgt nur beim Rückfall in den Idle-Zustand, nicht nach jedem erfolgreichen RX.

---

## Features

### CSMA/CA — Carrier Sense mit Hardware-CAD
Ersetzt blinde Sende-Delays durch echte Kanalüberwachung mittels Hardware Channel Activity Detection.

- **Konstanten und Zustandsvariablen**: Slot-basierter exponentieller Backoff mit konfigurierbaren Timing-Parametern (CSMA_BASE 4500/3000/2000ms, CSMA_SLOT_SIZE 35ms, max. 3 Versuche).
- **ESP32 TX-Gate** (SX127x + SX126x): Hardware-CAD via `radio.scanChannel()`, Double-Check bei erstem Busy-Ergebnis (filtert Fehlalarme), dynamisches `csma_timeout` statt fixem `RECEIVE_TIMEOUT`.
- **nRF52 TX-Gate** (RAK4630): Async CAD via `Radio.StartCad()` + `OnCadDone()`-Callback mit 100ms Safety-Timeout. Gleiche Double-Check- und Backoff-Logik wie ESP32.
- **Alte Blind-Delays entfernt**: `cmd_counter` und `tx_waiting` durch CAD-basierte Entscheidung ersetzt.

### Mesh-Relay: Loop-Erkennung und Hop-Counter-Schutz
- **Loop-Detection**: Vor dem Relay wird geprüft, ob der eigene Callsign bereits in `msg_source_path` vorkommt. Falls ja, wird das Relay übersprungen. Verhindert endlose Relay-Zyklen bei >10 aktiven Nodes.
- **Hop-Counter Safeguard**: `max_hop` wird mit `0x0F` maskiert, damit Flag-Bits (server/track/app_offline/mesh) bei Hop-Decrement nicht korrumpiert werden.

### Deduplizierungs-Puffer vergrößert
- **Eigener MAX_DEDUP_RING** (60 Slots) entkoppelt vom TX-Ringpuffer (MAX_RING). Bei 9+ gleichzeitig sendenden Nodes erzeugt jede Broadcast-Nachricht bis zu 10 IDs (1 MSG + 9 ACKs) — der alte 30-Slot-Puffer war zu schnell voll, was zu falscher Duplikat-Erkennung führte.

### Heartbeat-Loss-Detection (ESP32)
- ESP32 erkennt jetzt Server-Ausfall über Heartbeat-Timeout (wie nRF52). Bei Überschreitung wird `resetMeshComUDP()` aufgerufen statt endlos in toter UDP-Verbindung zu hängen.

### nRF52/RAK4630: Heap-Monitoring
- FreeRTOS `xPortGetFreeHeapSize()` liefert auf nRF52 (heap_3/libc-malloc) falsche Werte. Jetzt wird `mallinfo()` + `sbrk()`-Gap zum Stack-Pointer für akkurate Heap-Auswertung verwendet.

### nRF52/RAK4630: LoRa-Port
- ESP32-Verbesserungen für das Callback-basierte nRF52-Radiomodell portiert: `Radio.Rx()` zu Beginn von `OnRxDone()` (minimiert RX-Blindfenster), `is_receiving`-Guard im Timeout-Handler, RING_STATUS-Reports alle 30s, Payload-Safety-Copy.

---

## Debug & Logging

### Einheitliche State-Machine-Tags
- **[MC-SM]**-Tags für alle State-Transitions auf ESP32 und nRF52 (inkl. OnRxDone/OnTxDone-Callbacks). Ermöglicht automatisierte Log-Analyse und Timeline-Rekonstruktion.
- **[MC-DBG]**-Ausgaben mit konsistentem Format und State-Kontext auf beiden Plattformen.
- Doppelte Log-Einträge (z.B. redundantes `RX_PROCESS -> RX_LISTEN` nach `checkRX()`) entfernt.

### Channel Utilization Metriken
- Firmware trackt RX/TX-Airtime pro 10s-Fenster und gibt `[MC-DBG] CHANNEL_UTIL rx=Xms tx=Yms util=Z%` aus (ESP32 + nRF52).
- ESP32: RX-Airtime via `radio.getTimeOnAir(ibytes)` berechnet (kein OnHeaderDetect-Callback verfügbar).

### RING_OVERFLOW-Differenzierung
- `addRingPointer()` gibt jetzt den Puffernamen aus ("tx", "raw_rx", "phone"). TX-Overflow (kritisch) ist nicht mehr ununterscheidbar von raw_rx-Cycling (kosmetisch).
- raw_rx Ring-Overflow-Meldung unterdrückt (normales WebUI-Verhalten, kein Fehler).

---

## Tools

### serial_monitor.py
- Neues Echtzeit-Monitoring-Tool: parst Firmware-Output, farbcodierte Events, Inline-Activity-Indicator (`.RrCTt` für States, `x!` für Fehler).
- **CHANNEL_BUSY vs. RADIO_SILENT**: Unterscheidet aktive Kanalnutzung (legitime Kollisionen) von echtem Funkstille-Zustand. Verhindert Fehlalarme.
- Ctrl+C-Handler und `RE_RX_TIMEOUT_FIRE`-Regex korrigiert.

### loganalyse.sh
- Umfassendes Log-Analyse-Skript: Metriken, Fehlerzähler, Timing-Statistiken aus Firmware-Logs.
- Angepasst an `serial_monitor.py`-Ausgabeformat (Zeitstempel-Prefix, neue Feldnamen `rssi=/snr=/size=`).
- Single-Pass-AWK für RSSI/SNR-Korrelation, adaptive RX_TIMEOUT_FIRE-Statistiken, BUFFER_DROPS, CAD_STATS.
- BSD-awk-kompatibel (keine gawk-Abhängigkeit).

---

## Betroffene Dateien

| Datei | Bereich |
|-------|---------|
| `src/esp32/esp32_main.cpp` | WiFi, BLE, CSMA/CA, Logging, Timeout |
| `src/nrf52/nrf52_main.cpp` | CSMA/CA, Logging, Heap-Monitoring |
| `src/lora_functions.cpp` | Retransmit, ACK, CAD, nRF52-Port |
| `src/loop_functions.cpp` | Dedup-Puffer, CSMA-Konstanten |
| `src/udp_functions.cpp` | UDP-Reset, NTP, Heartbeat |
| `src/extudp_functions.cpp` | JSON-Validierung, Socket-Schutz |
| `src/phone_commands.cpp` | BLE Queue-Integration |
| `src/aprs_functions.cpp` | Loop-Detection, Hop-Counter |
| `src/configuration_global.h` | Konstanten, Version |
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
