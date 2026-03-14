# Release Notes -- MeshCom Firmware v4.35n_20260314_fix2 (2026-03-14)

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

### Gateway RING_ZOMBIE: Retransmit-Timer auf Gateways nie getaktet (NEU - v4.35n_20260308_fix1)
- **Ursache**: `updateRetransmissionStatus()` war durch `if(!bGATEWAY)` geschuetzt -- auf Gateway-Nodes wurde der Retransmit-Timer nie getaktet. Ein gesendeter Text-Nachrichten-Slot blieb nach TX bei `status=0x01` (RING_STATUS_SENT) stecken, weil der Timer den Status nie von 0x01 bis zum Threshold 0x15 hochzaehlen konnte.
- **Auswirkung**: Wenn kein Echo der eigenen Nachricht via LoRa zurueckkam (schwaches Signal, Netzwerk-Partition), blieb der Slot als "Zombie" im Ring Buffer. RING_STATUS zeigte dauerhaft `retrying=1, queued=0`. Der Slot wurde erst beim naechsten Ringpuffer-Umlauf ueberschrieben -- bis dahin war ein Slot permanent blockiert.
- **Fix**: `if(!bGATEWAY)`-Guard entfernt. Gateways durchlaufen jetzt denselben Retransmit-Zyklus (40s pro Retry, max 3 Retries = 120s) wie alle anderen Nodes.
- **Betroffene Dateien**: `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

### UDP-to-LoRa Relay: Pakete gehen verloren wenn anderer Gateway schneller sendet (NEU - v4.35n_20260307_fix4)
- **Ursache**: UDP-empfangene Text-Nachrichten wurden mit `status=0x00` (RING_STATUS_READY) in den TX-Ringpuffer geschrieben. Wenn ein anderer Gateway (z.B. DL7OSX-1) dieselbe msg_id vor dem lokalen Node via LoRa aussendet, erkennt der LoRa-RX-Handler den Ringpuffer-Eintrag als "bereits gehoert" und loescht den Slot (`len=0, status=0xFF`). Beim naechsten `doTX()` ist der Slot leer -- APRS-Decode schlaegt fehl mit `size <0> to short`.
- **Auswirkung**: Log-Analyse von DK5EN-99 zeigte 24 von 64 UDP-empfangenen Paketen (37.5%) wurden still verworfen. DK5EN-12 (nur ueber DK5EN-99 erreichbar) hat diese Nachrichten nie erhalten.
- **Fix**: UDP-Relay-Nachrichten verwenden jetzt immer `status=0xFF` (RING_STATUS_DONE / fire-and-forget). Retransmission ist Aufgabe des sendenden Nodes, nicht des Gateways. Durch `status=0xFF` ueberspringt der LoRa-RX-Handler den Slot (Bedingung `status != RING_STATUS_DONE`), und `doTX()` sendet die Nachricht zuverlaessig aus.
- **Betroffene Datei**: `src/udp_functions.cpp`

### RX_IRQ_STALE RSSI-Validierung und Header/Preamble-Differenzierung (NEU - v4.35n_20260308_fix2)
- **Ursache**: Die in fix3 eingefuehrte IRQ-Deferral-Logik behandelte `PREAMBLE_DETECTED` und `HEADER_VALID` identisch (jeweils bis zu 3 Deferrals a 4,5s = max 13,5s Blockade). Log-Analyse zeigte: 95% der RX_IRQ_STALE Events traten bei leerem Kanal auf (CHANNEL_UTIL=0%) — die SX1262 IRQ-Flags blieben nach abgebrochenen Transmissions oder Rauschimpulsen latched.
- **Auswirkung**: DK5EN-99: 230 Events = 53 Min verlorene Empfangszeit. DK5EN-12: 112 Events = 26 Min.
- **Fix**: Dreistufige Verbesserung: (1) RSSI-Validierung via `radio.getRSSI(false)` — bei RSSI < -126 dBm (unter Noise Floor) werden IRQ-Flags als stale erkannt und sofort zum Radio-Restart durchgefallen. (2) HEADER_VALID: bis zu 3 Deferrals (unveraendert, starker Indikator). (3) PREAMBLE_DETECTED ohne Header: max 1 Deferral (schwacher Indikator). Neue Debug-Logs: `RX_IRQ_STALE_EARLY` (RSSI-basiert), `RX_TIMEOUT_DEFERRED src=header_valid/preamble_only`.
- **Betroffene Datei**: `src/esp32/esp32_main.cpp`

### DM-ACK Relay: Ring-Buffer-Eintrag ohne Laenge/Status (NEU - v4.35n_20260313)
- **Ursache**: Im `rx_dm_ack_new`-Pfad (DM-ACK mit neuer msg_id weiterleiten) fehlten die Zuweisungen `ringBuffer[iWrite][0]=12` (Laenge) und `ringBuffer[iWrite][1]=RING_STATUS_DONE` (Status). Der direkt darueberstellende `rx_dm_ack_gw`-Pfad setzt beide korrekt. Beim zweiten ACK wurden nur die Daten per `memcpy` geschrieben, aber Laenge und Status blieben auf den Werten des vorherigen Slot-Zustands (typisch: len=0, status=0xFF).
- **Auswirkung**: `doTX()` liest den Slot mit len=0, ruft `decodeAPRS()` mit Laenge 0 auf, was die Fehlermeldungen `APRS decode - Packet discarded, wrong APRS-protocol - size <0> to short!` und `LoRa starting with 0x00 and 000000 ... no decode` erzeugt. Das ACK wird nicht gesendet — der empfangende Node erhaelt keine Bestaetigung ueber den zweiten Hop.
- **Fix**: `ringBuffer[iWrite][0]=12` und `ringBuffer[iWrite][1]=RING_STATUS_DONE` vor dem `memcpy` eingefuegt, identisch zum `rx_dm_ack_gw`-Pfad.
- **Betroffene Datei**: `src/lora_functions.cpp`

### OnRxDone Display/WiFi-Blockade: I2C und UDP aus Radio-Callback entfernt (NEU - v4.35n_20260314_fix1)
- **Ursache**: `sendDisplayText()` und `sendDisplayPosition()` fuehrten I2C-Transfers (SSD1306 OLED via `u8g2->firstPage()/nextPage()`) direkt im `OnRxDone()`-Callback aus. Auf dem ESP32 konkurriert der WiFi-Stack (Core 0) mit I2C-Zugriffen und blockiert die Transfers sporadisch fuer bis zu 600ms. Zusaetzlich rief `sendExtern()` synchron `UdpExtern.beginPacket()`/`.endPacket()` auf — bei aktivem ExtUDP potenziell 500ms+ durch DNS oder Socket-Wartezeiten.
- **Auswirkung**: Log-Analyse (OE1KFR-7, Heltec V3) zeigte bimodale ONRXDONE_TIME-Verteilung: 495x <=50ms, aber 38x >600ms (MAX=622ms). Alle 600ms-Events traten bei Text-Nachrichten an eigene Gruppe auf (Display-Update + Gateway-ACK-Erzeugung). Waehrend der 600ms-Blockade ist das Radio taub — keine Pakete koennen empfangen werden.
- **Fix**: Dreifacher Ansatz:
  1. **Display-Update deferred**: `sendDisplayText()`/`sendDisplayPosition()` werden nicht mehr direkt aufgerufen. Stattdessen werden die Daten in eine Pending-Struct (`pendingDisplayMsg`) kopiert. Der Main-Loop prueft `bPendingDisplayText`/`bPendingDisplayPos` und fuehrt den I2C-Transfer aus.
  2. **sendExtern async**: `sendExtern()` im OnRxDone-Pfad durch `queueExtern()` ersetzt. Ein kleiner Ringpuffer (4 Slots) nimmt die Rohdaten auf. `flushExternQueue()` im Main-Loop fuehrt JSON-Aufbau + UDP-Send aus.
  3. **ONRXDONE_TIME-Alarm**: Warnung `[MC-WARN] ONRXDONE_SLOW` wird immer geloggt wenn die Verarbeitungszeit >50ms uebersteigt (nicht hinter `bLORADEBUG`). Statistik (`ONRXDONE_STATS max=Xms warn=Y`) alle 10s im CHANNEL_UTIL-Report.
- **Betroffene Dateien**: `src/lora_functions.cpp`, `src/loop_functions_extern.h`, `src/extudp_functions.cpp`, `src/extudp_functions.h`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/configuration_global.h`

### Buffer-Unterdimensionierung: MHEARD/MHPATH/DEDUP zu klein fuer Bergstandorte (NEU - v4.35n_20260314_fix2)
- **Ursache**: Log-Analyse von 5 Berg-Gateways (OE1KBC, OE3XIR, OE3MAG, OE3XOC, OE3XWJ) ueber 60+ Stunden zeigte, dass **85-124 Nodes direkt via HF (H00)** gehoert werden. MAX_MHEARD war auf 20 eingestellt — der Puffer wurde staendig ueberschrieben, Routing-Informationen gingen verloren.
- **Auswirkung**: MHeard-Tabelle rotierte staendig (Faktor 4.25-6.2x unterdimensioniert). Unvollstaendige Netzwerksicht auf dem Server. MAX_DEDUP_RING (60) zeigte Wraparounds bei OE1KBC.
- **Fix**: Plattform-differenzierte Buffer-Groessen in `configuration_global.h`:
  - **ESP32-S3 / nRF52840** (voller RAM): MAX_MHEARD=120, MAX_MHPATH=150, MAX_DEDUP_RING=100, MAX_RING_UDP=30
  - **ESP32 original** (DRAM-limitiert): MAX_MHEARD=30, MAX_MHPATH=40, MAX_DEDUP_RING=70, MAX_RING_UDP=25
  - **ENABLE_XML**: MAX_MHEARD=50, MAX_MHPATH=50, MAX_DEDUP_RING=60
- **RAM-Impact**: +16 KB auf ESP32-S3/nRF52 (6.5% von 243 KB), +3 KB auf ESP32 original. Alle Targets bauen sauber.
- **Betroffene Datei**: `src/configuration_global.h`

### BLE-Buffer-Overflow ohne BLE-Client: Sinnloses Buffering verhindert (NEU - v4.35n_20260314_fix2)
- **Ursache**: `addBLEOutBuffer()` und `addBLEComToOutBuffer()` schrieben Daten in den Phone-Ringpuffer auch wenn kein BLE-Client verbunden war. Der Puffer lief ueber und erzeugte `RING_OVERFLOW buf=phone` Meldungen (z.B. 285x auf OE3MAG in 2h42m).
- **Auswirkung**: Sinnloser Speicherverbrauch, irrelevante Overflow-Meldungen im Log, sporadische 572-782ms Blockierungen im RX-Pfad durch BLE-Write-Versuche.
- **Fix**: Early-Return `if (!g_ble_uart_is_connected) return;` in beiden Funktionen. Daten werden nur noch gepuffert wenn ein BLE-Client tatsaechlich verbunden ist.
- **Betroffene Datei**: `src/loop_functions.cpp`

### RAK/NRF52 Serial-Hang: CDC-ACM blockiert _lora_task (NEU - v4.35n_20260314)
- **Ursache**: Auf dem nRF52840 (RAK4630) laeuft die USB-Serial-Ausgabe ueber Adafruit_USBD_CDC. Dessen `write()` blockiert in einer `while(remain && tud_cdc_n_connected)` Schleife, wenn der CDC-ACM-Puffer voll ist. Die SX126x-Arduino-Library dispatcht Radio-Callbacks (OnTxDone, OnRxDone, OnRxTimeout, ...) aus einem dedizierten FreeRTOS-Task (`_lora_task`). Wenn `Serial.printf()` in diesem Kontext blockiert, kann `_lora_task` keine Radio-IRQs mehr verarbeiten — die gesamte Firmware haengt.
- **Auswirkung**: Nutzer berichten, dass sich die serielle Verbindung auf RAK-Nodes "weghaengt", meist nach einem TX. Der Node reagiert weder auf serielle Befehle noch auf LoRa-Pakete. Erst ein Hardware-Reset behebt das Problem. Ein externes Serial-Monitor-Script (pyserial) verstaerkt das Problem, da es den CDC-ACM-Puffer schneller fuellt.
- **Fix**: Deferred-Serial-Output-System: Radio-Callbacks schreiben nicht mehr direkt auf Serial, sondern in einen kleinen Ringpuffer (24 Slots x 200 Bytes). Der Main-Loop (`nrf52loop()`) flusht den Puffer zu Serial, wo Blocking akzeptabel ist. Kontextabhaengiges Macro `CB_PRINTF()` waehlt automatisch: in `_lora_task` → deferred (non-blocking), im Main-Loop → direkt Serial (blocking OK). Auf ESP32 ist `CB_PRINTF` ein direkter Fallthrough zu `Serial.printf()` (kein Verhaltenswechsel).
- **Betroffene Dateien**: `src/deferred_serial.h` (neu), `src/nrf52/nrf52_main.cpp`, `src/lora_functions.cpp`, `src/loop_functions.cpp`

### RAK/NRF52 CAD startet nicht im RX-Continuous-Modus (NEU - v4.35n_20260311)
- **Ursache**: `Radio.StartCad()` der SX126x-Arduino Library ruft `SX126xSetCad()` auf, ohne vorher in STDBY_RC zu wechseln. Wenn das Radio in RX-Continuous-Mode ist (von `Radio.Rx(0)`), ignoriert der SX126x-Chip den SetCad-Befehl stillschweigend. CAD startet nie, DIO1 feuert nie, `OnCadDone` wird nie aufgerufen — jeder CAD-Versuch endet im 100ms Safety-Timeout.
- **Auswirkung**: Auf RAK4631/NRF52 lief jeder CAD-Scan in den Safety-Timeout. CSMA/CA konnte den Kanal nie pruefen und sendete blind nach Timeout-Ablauf.
- **Fix**: `Radio.Standby()` vor jedem `Radio.StartCad()` Aufruf. Bringt das Radio in STDBY_RC, sodass der SetCad-Befehl korrekt ausgefuehrt wird. Gleiches Muster wie RadioLib auf ESP32 (`standby() -> clearIrqStatus() -> setCad()`).
- **Betroffene Datei**: `src/nrf52/nrf52_main.cpp`

### CAD-Storm Rapid-Fire: 100ms Preamble-Check statt 0ms (NEU - v4.35n_20260308_fix1)
- **Ursache**: Bei `cad_attempt >= 3` (CSMA_MAX_ATTEMPTS erreicht) gab `csma_compute_timeout()` 0ms zurueck — das Radio wurde ohne jede Pause sofort restarted und der naechste CAD-Scan ausgefuehrt. Bei 0ms Wartezeit hat das Radio keine Chance, eingehende Preambles von schwachen Nodes zu erkennen.
- **Auswirkung**: Schwache Nodes (entfernte Stationen) werden im Rapid-Fire-Modus systematisch ueberfahren, weil deren Preamble nie detektiert wird.
- **Fix**: Neue Konstante `CSMA_RAPID_RX_MS` (100ms) als minimales RX-Fenster im Rapid-Fire-Modus. Statt sofortigem CAD-Retry hoert das Radio 100ms zu. Die bestehende IRQ-Polling-Logik (RSSI-Validierung + Header/Preamble-Differenzierung) entscheidet dann automatisch: Preamble erkannt = Deferral, kein Signal = sofort weiter mit CAD. Kosten: ~100ms pro Versuch (vernachlaessigbar vs. 2000-4500ms Backoff davor).
- **Betroffene Dateien**: `src/configuration_global.h`, `src/lora_functions.cpp`

### Stale IRQ-Flags blockieren Radio dauerhaft (NEU - v4.35n_20260307_fix3)
- **Ursache**: Der in fix2 eingefuehrte IRQ-Schutz (`PREAMBLE_DETECTED`/`HEADER_VALID`-Pruefung) hatte kein Limit fuer aufeinanderfolgende Deferrals. Bei Stoersignalen oder fehlgeschlagenen Empfaengen bleiben die SX1262-IRQ-Flags latched -- jeder Timeout-Zyklus (~4.5s) wurde erneut aufgeschoben, ohne dass je ein Paket empfangen wurde.
- **Auswirkung**: Radio-Stillstand von ueber 100 Sekunden beobachtet (Log: 22+ aufeinanderfolgende `RX_TIMEOUT_DEFERRED src=irq_rx_active`). Waehrend dieser Zeit werden keine Pakete empfangen oder gesendet -- der Node ist effektiv taub.
- **Fix**: Maximale Anzahl aufeinanderfolgender IRQ-Deferrals auf 3 begrenzt (≈13.5s). Bei Ueberschreitung wird `startReceive()` erzwungen, unabhaengig vom IRQ-Status. Zaehler wird bei erfolgreichem Empfang oder Radio-Restart zurueckgesetzt. Neuer Debug-Log `RX_IRQ_STALE` bei erzwungenem Restart.
- **Betroffene Dateien**: `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/loop_functions.cpp`, `src/loop_functions_extern.h`

### RX-Timeout bricht laufenden Paketempfang ab (NEU - v4.35n_20260307_fix2)
- **Ursache**: Der CSMA-Timeout-Handler rief `startReceive()` auf um das Radio periodisch neu zu starten, pruefte aber nur `receiveFlag` (fertig empfangene Pakete). Pakete die noch demoduliert wurden (Preamble erkannt, Daten noch im Anflug) wurden durch den Radio-Reset stillschweigend abgebrochen.
- **Auswirkung**: Log-Analyse zweier Nodes (DK5EN-99 als Repeater, DK5EN-12 als Empfaenger) zeigte 20 von 22 verlorenen Paketen (9.2% Verlustrate) durch exakt dieses Timing: `RX_TIMEOUT_FIRE` waehrend aktiver Paketdemodulation.
- **Fix**: IRQ-Register-Polling (`radio.getIrqFlags()`) auf `PREAMBLE_DETECTED`/`HEADER_VALID` vor dem `startReceive()`-Aufruf im Timeout-Handler. Bei laufendem Empfang wird der Timeout aufgeschoben statt das Radio neu zu starten. Gleiches Muster wie bereits im TX-Gate implementiert.
- **Betroffene Datei**: `src/esp32/esp32_main.cpp`

### checkOwnTx() -1 Return-Wert korrekt behandelt (NEU - v4.35n_20260307_fix1)
- **Boolean-Kontext-Bug behoben**: `if(!checkOwnTx(...))` interpretierte -1 (Fehler) als false, nicht als error-Zustand.
- **Korrekte Behandlung**: `int icheck = checkOwnTx(...); if(icheck < 0)` erlaubt proper Fehlerbehandlung. Betroffene Datei: `src/udp_functions.cpp` Zeile 315.

### Retransmit-Ringpuffer: Nachrichtenverlust behoben
- **updateRetransmissionStatus()** scannt wieder alle MAX_RING Slots statt nur den eingeschraenkten [iRead..iWrite)-Bereich. Bereits konsumierte Slots enthalten noch gueltige Retry-Daten und muessen weiter erreichbar sein.
- **Text-Nachrichten-Slots** werden nach TX nicht mehr sofort geloescht. Nur Fire-and-Forget-Eintraege (Relay/ACK/Beacon mit Status 0xFF) werden direkt freigegeben; Text-Nachrichten bleiben fuer Retry-Tracking erhalten.
- **ACK-Empfang gibt Slots frei**: Beim Empfang eines passenden ACK (Text oder binaer 0x41) wird der zugehoerige Ringpuffer-Slot korrekt zurueckgesetzt. Zuvor blieben Slots belegt und blockierten den Puffer.
- **Binaeres ACK (0x41)** stoppt jetzt die Retransmission im Ringpuffer -- zuvor wurde nur der Phone-Notification-Status gesetzt, Broadcast/Group-Retries liefen endlos weiter.

### WiFi-Stabilitaet
- **Full Radio Power-Cycle bei Init** (NEU - v4.35n_20260308_fix3): `WiFi.disconnect(true,true)` allein reicht nach einem Glitch-Reboot nicht aus -- der ESP-IDF WiFi-Stack bleibt mit `E wifi:timeout when WiFi un-init, type=4` haengen. Fix: Vor jeder WiFi-Initialisierung in `startWIFI()` wird der Radio-Chip komplett ausgeschaltet (`WiFi.mode(WIFI_OFF)` + 1000ms Delay), dann sauber neu gestartet (`WiFi.mode(WIFI_STA)` + 200ms). Beim Boot-Retry in `esp32_main.cpp` zusaetzlich 1500ms Pause fuer stabilen Hardware-Reset. Behebt zuverlaessig das Problem, dass beide Heltec V3 nach Signal-Glitch-Reboots keine IP-Adresse vom DHCP erhalten.
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
- **CAD-Alert umbenannt** (NEU - v4.35n_20260308_fix1): `CAD FALSE POSITIVE streak` zu `CAD BUSY streak` korrigiert. Der Alert zaehlt aufeinanderfolgende `LORA_DETECTED` (-702) Ergebnisse -- das sind echte Kanal-Busy-Erkennungen, keine False Positives. Counter `cad_false_pos` zu `cad_busy` umbenannt. Der separate Counter `cad_false_pos_filtered` (Double-Check filtert echte False Positives) bleibt unveraendert.
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
| `src/configuration_global.h` | Konstanten, Version, Message-Types, Ring-Status, ONRXDONE_WARN_MS, Buffer-Groessen (MHEARD/MHPATH/DEDUP/UDP) |
| `src/esp32/esp32_main.cpp` | WiFi, BLE, CSMA/CA, IRQ-Polling, Atomarer RX-Restart, Logging, Timeout, Deferred-Display, ONRXDONE_STATS |
| `src/deferred_serial.h` | Deferred-Serial-Ringpuffer fuer RAK/NRF52 (neu) |
| `src/nrf52/nrf52_main.cpp` | CSMA/CA, Logging, Heap-Monitoring, Deferred-Serial-Flush, Deferred-Display, ONRXDONE_STATS |
| `src/lora_functions.cpp` | Retransmit, ACK, CAD, handleACK(), Helper-Funktionen, Double-Buffer, nRF52-Port, CB_PRINTF, Deferred-Display, queueExtern |
| `src/loop_functions.cpp` | Dedup-Puffer, CSMA-Konstanten, volatile Flags, addTxRingEntry(), CB_PRINTF, BLE-Guard fuer Phone-Buffer |
| `src/loop_functions.h` | addTxRingEntry() Deklaration |
| `src/loop_functions_extern.h` | volatile extern-Deklarationen, ONRXDONE/Display-Deferred externs |
| `src/batt_functions.cpp` | volatile extern Fix |
| `src/udp_functions.cpp` | UDP-Reset, NTP, Heartbeat, addTxRingEntry() |
| `src/extudp_functions.cpp` | JSON-Validierung, Socket-Schutz, queueExtern/flushExternQueue |
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
