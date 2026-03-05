# Release Notes v4.35n_20260305

## LoRa Radio Kernverbesserungen

### State Machine & CSMA/CA
- **LoRa State Machine**: `transitionTo()` ersetzt duplizierte Interrupt-Umschalt-Blöcke, Blind Window von ~10ms auf ~100µs reduziert
- **CSMA/CA**: Slot-basierter CAD-Backoff mit Double-Scan gegen False Positives, adaptive Contention Window (2^3 bis 2^8 Slots), 30s Watchdog gegen Starvation
- **DIO1 Race Condition Fix**: Pin-Polling nach `startReceive()` fängt verlorene Edge-Triggered Interrupts ab
- **Software-Timeout Init**: `iReceiveTimeOutTime` wird beim Boot initialisiert, Re-Arm nach TX-Gate verhindert permanente Deaktivierung
- **Airtime-Tracker**: RX/TX-Airtime-Messung mit exponentiellem Decay, `channel_util_percent` steuert Backoff und adaptiven TX-Wait
- **Adaptiver TX-Wait**: Ersetzt fixen 4.5s RECEIVE_TIMEOUT durch Channel-Utilization-gesteuerte Wartezeit (2.0s–14.0s)

### ACK & Retransmit
- **ACK Fast-Path**: Dedizierter `ackBuffer[8]` mit Priority TX — ACKs werden sofort nach RX gesendet statt hinter Relay-Queue zu warten
- **ACK Dedup**: Mehrstufige Deduplizierung unterdrückt redundante ACK-Storms in dichten Mesh-Netzwerken (Level 1: Queue Guard, Level 2: RX Cancel)
- **Retransmit-Optimierung**: Randomisierte Intervalle (msg_hash Jitter), HEARD-before-TX Cancel, Ring Buffer Overflow-Schutz
- **UDP→LoRa Retry**: DM-Nachrichten erhalten 2 Retries, Broadcast/Group 1 Retry, CET/SET bleiben Fire-and-Forget
- **UDP-to-LoRa Relay**: Fire-and-Forget — Server-bestätigte Nachrichten werden nicht mehr auf LoRa retransmitted

### Bugfix
- **UDP→LoRa Gateway defekt**: `checkOwnTx()` Rückgabewert (`int`, -1 = nicht gefunden) wurde als Boolean behandelt — `!(-1)` ist false, dadurch wurden **alle** Server-Nachrichten blockiert und nie per LoRa ausgesendet. Betraf alle Gateways seit Upstream-Commit 0953496.

### Mesh-Stabilität
- **Dedup Buffer**: Von `MAX_RING` entkoppelt und auf 60 Slots erhöht (vorher 30), `MAX_ACK_RING` von 8 auf 16
- **Loop Detection**: Eigenes Callsign im Source-Path wird vor Relay erkannt, Hop-Counter auf 4 Bit maskiert
- **SX127X CAD Support**: Chip-agnostisches `CAD_ACTIVITY_DETECTED` Makro für SX127x/SX126x
- **Gruppe 9 (HF)**: Nachrichten werden immer an BLE weitergeleitet, unabhängig von Node-Group-Config
- **HEY Zombie Fix**: `insertOwnTx()` für Broadcast-Discovery entfernt

### nRF52 RAK4630
- **CAD/CSMA-CA**: Channel Activity Detection über synchronen Wrapper (`nrf52_cad_scan`) aktiviert
- **Heap Monitoring**: `mallinfo()`/`sbrk()` für Heap-Überwachung (FreeRTOS heap_3 unterstützt `xPortGetFreeHeapSize()` nicht)

## WiFi & Netzwerk

- **WiFi Boot Failure**: Hardware-Reset und sofortiger Retry bei fehlgeschlagener WiFi-Verbindung beim Start
- **WiFi Stability (T-Deck)**: Redundanter Reconnect-Block entfernt, der nach ~10 Min WiFi-Abbrüche auf T-Deck verursachte
- **Heartbeat-Timeout**: Zweistufig — Warnung bei 35s mit WiFi-Statuscheck, Timeout bei 65s resettet nur bei tatsächlich getrenntem WiFi
- **NTP Fix**: Fehlgeschlagenes NTP-Update zerstört nicht mehr die WiFi-Verbindung
- **WiFi Ping**: Blockierendes `Ping.ping()` durch `WiFi.status()` ersetzt
- **UDP TX**: Error Counter mit automatischem Reset bei `MAX_ERR_UDP_TX`
- **ExtUDP**: JSON-Validierung für type/dst/msg Felder, Null-Pointer-Schutz, Feldlängen-Prüfung

## BLE

- **FreeRTOS Queue**: NimBLE-Callbacks (Core 0) und Main Loop (Core 1) Race Condition behoben — Spin-Wait durch FreeRTOS Queue ersetzt
- **Encryption Check**: Toter Verschlüsselungs-Check entfernt, der der Security-Config widersprach
- **{CET} Zeitsignal**: Wird jetzt via `addBLEOutBuffer` an BLE weitergeleitet (LoRa- und UDP-Empfang)

## Libraries

- **RadioLib**: Update 7.1.2 → 7.6.0 (Timing-Overflow-Fixes, `calculateTimeOnAir()`, HAL-Verbesserungen)
- **NimBLE-Arduino**: Revert auf 2.2.3 (Kompatibilitätsproblem mit aktuellem ESP32 Core, Update auf 2.3.8 wartet auf Core 3.x)

## T-Deck

- **Farbschema**: Neues Color-Scheme
- **Display-Kontrast**: `setContrast(255)` hardcoded statt aus potenziell korruptem Config-Wert
- **Position → SD**: Positionsdaten auf SD-Karte
- **SPIFFS**: SPIFFS-Unterstützung
- **GPS**: Init on Demand
- **HEY mit MH-Count**: MHeard-Zähler in HEY-Anzeige
- **HEAP**: Heap-Monitoring im Display

## Upstream (icssw-org)

- **nRF sendextern**: Externe Sendefunktion für nRF52
- **ExtUDP**: RSSI/SNR-Werte in ExtUDP
- **OTA Framework**: platform.ini OTA Framework Update
- **Safeboot**: Neu kompilierte Safeboot-Binaries

## Serial Monitor (tools/serial_monitor.py)

- **CSMA-Tracking**: Alle State-Machine-Transitionen mit Zeitstempeln und Verweildauer-Histogramm
- **Radio-Silent-Erkennung**: Alarm bei >20s ohne Radio-Aktivität
- **Ring-Zombie-Diagnose**: Zombie-Tracking, Overflow-Alerts, Queue-Depth (Threshold auf 150s angehoben)
- **ACK Dedup Statistiken**: Saved-vs-Transmitted Ratio in periodischen Summaries
- **NTP/Server-Überwachung**: Consecutive Heartbeat-Timeout-Erkennung, SERVER UNREACHABLE nach 3×
- **RX RESTART Flood**: Zählt nur noch `RX_TIMEOUT_FIRE` Events (keine False Alerts bei normalem Traffic)
- **Binärfilter**: Spezialzeichen aus serieller Ausgabe gefiltert
- **Loop Detection**: `RELAY_LOOP_BLOCKED` Counter

## Sonstiges

- **Debug Output**: Noisy `raw_rx` Ring Overflow Ausgabe unterdrückt
- **FLASH_VERSION**: Aktualisiert auf 20260228
- **.gitignore**: Einträge für Build-Artefakte und Tools

---

## Supported Hardware

E22-DevKitC.bin (433 MHz) | E22_XML-DevKitC.bin (433 MHz) | E22_1268_S3-DevKitC.bin (433 MHz) | E22_1262-DevKitC.bin (868 MHz) | E22_1262_S3-DevKitC.bin (868 MHz) | heltec_wifi_lora_32_V2.bin | heltec_wifi_lora_32_V3.bin | heltec_wireless_stick_v3.bin | heltec_wireless_tracker.bin | ttgo-lora32-v21.bin | ttgo_tbeam.bin | ttgo_tbeam_SX1262.bin | ttgo_tbeam_SX1268.bin | ttgo_tbeam_supreme_l76k.bin | vision-master-e290.bin | wiscore_rak4631.bin | t_deck.bin | t_deck_plus.bin

Webflasher für Upgrade ab 4.30q: https://esptool.oevsv.at

[MeshCom Changelog](https://icssw.org/meshcom-versionen/) | [MeshCom@ICSSW Projektseite](https://icssw.org/meshcom/)
