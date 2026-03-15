# Release Notes -- MeshCom Firmware v4.35n_prio_v20260315_fix4 (2026-03-15)

APRS-Parser Hardening: korrupte RF-Pakete koennen keinen Absturz mehr verursachen.

---

## Aenderungen seit v4.35n_prio_v20260315_fix3

### 18. APRS-Parser: Source/Destination-Path-Schleifen gegen korrupte Pakete abgesichert
- **Ursache**: Ein korruptes RF-Paket ohne `>` Trennzeichen (z.B. `OE1FUC-13,OE3MAG-12,OE3CZC-##...Muell...`) liess die Source-Path-Schleife in `decodeAPRS()` den gesamten Restbuffer durchlaufen. Fuer jedes Byte wurde `String::concat()` aufgerufen — bei ~250 Bytes Muell fuehrte das zu 488ms Verarbeitungszeit (normal: 1-9ms) und Heap-Fragmentierung auf dem ESP32.
- **Auswirkung**: OE1KBC-12 und OE3MAG-12 abgestuerzt am 15.03.2026 nach Empfang eines solchen Pakets.
- **Fix**: Beide Path-Parsing-Schleifen (Source + Destination) begrenzt auf max 120 Bytes. Non-Printable-Bytes (< 0x20 oder > 0x7E) brechen die Schleife sofort ab. Der bestehende `bSourceEndOk`/`bDestinationEndOk`-Check verwirft das Paket dann automatisch.
- **Betroffene Datei**: `src/aprs_functions.cpp`

### 19. APRS-Parser: Buffer-Overread bei FW-Sub-Version behoben
- **Ursache**: Nach dem Parsen von FW-Version und Hardware-Byte wurde `RcvBuffer[inext]` ohne Bounds-Check gelesen. Die Zeilen direkt darueber hatten den Check `if(inext < rsize)`, Zeile 403 nicht.
- **Fix**: `inext < rsize` Guard eingefuegt, analog zu den bestehenden Guards.
- **Betroffene Datei**: `src/aprs_functions.cpp`

### 20. printAsciiBuffer: Bounds-Check bei kurzen Buffern
- **Ursache**: Die Debug-Funktion griff auf `buffer[0..3]` zu ohne vorher `len >= 4` zu pruefen.
- **Fix**: `if(len < 4) return;` als erstes Statement.
- **Betroffene Datei**: `src/loop_functions.cpp`

---

# Release Notes -- MeshCom Firmware v4.35n_prio_v20260315_fix3 (2026-03-15)

Heltec V2: GPS-Pins von GPIO 3/23 auf GPIO 12/13 umgelegt.

---

## Aenderungen seit v4.35n_prio_v20260315_fix2

### 17. Heltec V2: GPS-Pins auf GPIO 12/13 umgelegt
- **Ursache**: Der bisherige Fix (bGPSON-Check) umgeht den Pin-Konflikt nur bei deaktiviertem GPS. Bei aktiviertem GPS belegt GPIO 3 (GPS_TX_PIN = UART0 RX) weiterhin den USB-Serial-Empfang. Auch GPIO 23 (GPS_RX_PIN) ist suboptimal — auf manchen Heltec-V2-Revisionen ist er anderweitig belegt.
- **Fix**: GPS-Pins auf freie GPIOs umgelegt: `GPS_RX_PIN 13` (ESP32 Input, kein Strapping Pin), `GPS_TX_PIN 12` (ESP32 Output, Strapping Pin MTDI — als Output sicher, da GPS-RX hochohmig ist und beim Boot nicht zieht). UART0 (GPIO 3) bleibt frei fuer serielle Kommandos.
- **Betroffene Datei**: `variants/heltec_wifi_lora_32_V2/configuration.h`

---

# Release Notes -- MeshCom Firmware v4.35n_prio_v20260315_fix2 (2026-03-15)

Code-Review Fixes: 3 Bugs behoben + Race Conditions in Channel-Utilization beseitigt.

---

## Aenderungen seit v4.35n_prio_v20260315_fix1

### 13. UTC-Offset-Rechenfehler in mheard Path-Anzeige (Bug)
- **Ursache**: Die Zeitberechnung fuer mheard-Path-Timestamps verwendete `(60 * 60 + 24)` = 3624 statt `3600` Sekunden pro Stunde. Zusaetzlich wurde `node_utcoff` (float, unterstuetzt Halbstunden-Zonen wie UTC+5:30) auf `(int)` gecastet, was die Halbstunden-Praezision verlor.
- **Auswirkung**: mheard-Path-Zeiten waren je nach UTC-Offset 24s bis ~5min falsch (bei UTC+12). Halbstunden-Zonen (Indien, Nepal) zeigten zusaetzlich 30min Abweichung.
- **Fix**: Formel an das korrekte Pattern `(long)(meshcom_settings.node_utcoff * 3600.0)` angeglichen, wie es bereits in `loop_functions.cpp` und `web_functions.cpp` an anderen Stellen verwendet wird.
- **Betroffene Dateien**: `src/mheard_functions.cpp` (2 Stellen), `src/web_functions/web_functions.cpp` (1 Stelle)

### 14. Off-by-One in APRS-Telemetrie-Parsing (Bug)
- **Ursache**: 15 Parsing-Schleifen in `decodeAPRSPOS()` und der Display-Funktion verwendeten `id <= PayloadBuffer.length()`, was den Zugriff auf Index == length erlaubte (Out-of-Bounds). In 14 der 15 Schleifen wurde `charAt(id)` in der Break-Condition aufgerufen, BEVOR die Bounds-Pruefung `id == PayloadBuffer.length()` griff.
- **Auswirkung**: Out-of-Bounds-Lesezugriff bei APRS-Paketen, deren Payload genau am Feldende endet (z.B. letztes Feld ohne nachfolgendes `/`). Arduino `charAt()` gibt `\0` zurueck, daher kein Crash, aber undefiniertes Parsing-Verhalten moeglich.
- **Fix**: `id <= X.length()` durch `id < X.length()` ersetzt in allen 15 Schleifen (13 in `aprs_functions.cpp`, 2 in `loop_functions.cpp`).
- **Betroffene Dateien**: `src/aprs_functions.cpp`, `src/loop_functions.cpp`

### 15. pow(2,n) durch Bit-Shift ersetzt (Bug)
- **Ursache**: ADC-Maximalwert wurde mit `pow(2, resolution) - 1` berechnet. `pow()` gibt `double` zurueck, die implizite Konversion zu `int` kann Rundungsfehler verursachen.
- **Fix**: Ersetzt durch `(1 << resolution) - 1` — schneller, deterministisch, kein Fliesskomma-Risiko.
- **Betroffene Datei**: `src/batt_functions.cpp`

### 16. Race Conditions: volatile durch std::atomic ersetzt
- **Ursache**: Die Channel-Utilization-Counter (`ch_util_rx_accum`, `ch_util_tx_accum`) und ISR-Flags (`is_receiving`, `tx_is_active`) waren als `volatile` deklariert. Auf dem nRF52 laufen die Radio-Callbacks (`OnRxDone`, `OnTxDone`, `OnHeaderDetect`) in einem separaten LoRa-Task — dort ist das `+=` Read-Modify-Write-Pattern nicht atomar. `volatile` verhindert nur Compiler-Optimierungen, bietet aber keine Atomizitaetsgarantie auf ARM Cortex.
- **Auswirkung**: Auf nRF52 konnten Channel-Utilization-Werte durch gleichzeitigen Zugriff von Radio-Task und Main-Loop verfaelscht werden (Lost Updates). Auf ESP32 bestand kein reales Problem (deferred ISR im Main Loop), aber der Code war nicht formal korrekt.
- **Fix**: `volatile bool` / `volatile unsigned long` durch `std::atomic<bool>` / `std::atomic<unsigned long>` ersetzt. Das `+=`-Pattern wird durch `fetch_add()` + `exchange()` atomar ausgefuehrt. Einfache Reads/Writes (`=`, `==`, `if(...)`) funktionieren mit `std::atomic` transparent weiter.
- **Betroffene Dateien**: `src/loop_functions_extern.h`, `src/loop_functions.cpp`, `src/lora_functions.cpp` (5 Stellen), `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

---

# Release Notes -- MeshCom Firmware v4.35n_prio_v20260315_fix1 (2026-03-15)

Bugfix fuer Heltec V2: GPS-Init blockiert serielle Kommandos.

---

## Aenderungen seit v4.35n_prio_v20260315

### 12. Heltec V2: GPS-Init blockiert serielle Kommandoeingabe
- **Ursache**: `GPS_Init()` wurde beim Start bedingungslos aufgerufen (`#if defined(ENABLE_GPS)`), ohne den Runtime-Flag `bGPSON` zu pruefen. Auf dem Heltec V2 verwendet GPS `GPIO 3` als TX-Pin — derselbe Pin wie UART0 RX (USB-Serial). Die GPS-Baudraten-Erkennung (4 Baudraten x 2s = ~8s) belegte den Pin und blockierte danach den seriellen Empfang dauerhaft.
- **Auswirkung**: Bug-Report: Heltec V2 nimmt keine seriellen Kommandos mehr an. V3 und RAK nicht betroffen (dedizierte GPS-Pins ohne UART0-Konflikt).
- **Fix**: `bGPSON`-Check vor `GPS_Init()` — wenn GPS vom User nicht aktiviert ist, wird die GPS-Initialisierung uebersprungen. Log-Ausgabe bei deaktiviertem GPS.
- **Betroffene Datei**: `src/esp32/esp32_main.cpp`

---

# Release Notes -- MeshCom Firmware v4.35n_prio_v20260315 (2026-03-15)

Nachrichtenprioritaet, Trickle-HEY und erweiterte Statistik auf Basis von
`v4.35p_fixes`. Implementierung von ADR-001 Phase 1 (kein On-Air-Change).

---

## Aenderungen seit v4.35n_20260315_fix3

### Priority-Queue: 5-stufige Nachrichtenprioritaet (ADR-001 Vorschlag A)
- **Problem**: DMs und ACKs standen hinter 5-10 Relay/HEY-Paketen in der FIFO-Queue.
  Bei 50-80% CAD-Busy-Rate fuehrte das zu 10-50s unnoetige Verzoegerung fuer menschliche Nachrichten.
- **Loesung**: 5 Prioritaetsstufen mit differenziertem CSMA-Backoff. ACK und DM werden
  jetzt bevorzugt gesendet — erwartete Latenz-Reduktion von 10-50s auf 2-3s fuer Prio-1 Pakete.
- **Prioritaetsstufen**:
  - Prio 1 (Kritisch): ACK + persoenliche DM — CSMA-Backoff 2000ms
  - Prio 2 (Hoch): Gruppen + Broadcast "*" — CSMA-Backoff 3000ms
  - Prio 3 (Normal): Mesh-Relay — CSMA-Backoff 4000ms
  - Prio 4 (Niedrig): Position — CSMA-Backoff 4500ms
  - Prio 5 (Hintergrund): HEY — CSMA-Backoff 5000ms
- **Prio-Erkennung**: `getMessagePriority()` erkennt Typ via msg_type Byte,
  Relay via `RING_STATUS_DONE`, DM vs Gruppe via `CheckGroup()`.
  Relay-Pakete werden zuverlaessig ueber den Status bei Einfuegen unterschieden.
- **Prio-Entnahme**: `getNextTxSlot()` scannt den Ring-Buffer nach hoechster Prioritaet.
  Bei gleicher Prio: FIFO-Reihenfolge (aeltester zuerst). Ersetzt die alte `iRead++` FIFO-Logik.
- **Prio-Drop**: Bei voller Queue wird der aelteste Eintrag der niedrigsten Prioritaet
  verworfen. Neues Paket wird nur eingefuegt wenn es hoehere Prio hat als das zu droppende.
  ACKs und DMs werden nie zugunsten niedrigerer Pakete verworfen.
- **RAM-Aufwand**: 30+120 Bytes (`ringPriority[MAX_RING]` + `ringEnqueueTime[MAX_RING]`)
- **Kein On-Air-Change**: Alte Firmware empfaengt alle Pakete korrekt.
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
- **Logging**: `[MC-TRICKLE] SEND/SUPPRESS/TOPO_CHANGE interval=... consistent=... neighbors=...`
- **Betroffene Dateien**: `src/configuration_global.h`, `src/loop_functions_extern.h`,
  `src/loop_functions.cpp`, `src/lora_functions.cpp`,
  `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

### Erweiterte Statistik und Logging (ADR-001 Vorschlag D)
- **[MC-STAT]** alle 5 Minuten: TX-Zaehler pro Prioritaet, Drops pro Prio, Preemption-Zaehler
- **[MC-PRIO]** alle 5 Minuten: Latenz avg/max pro Prioritaetsstufe (Queue-to-TX)
- **[MC-HWM]** alle 30 Minuten: Queue/CSMA High-Water-Marks, aktuelles Trickle-Intervall
- **serial_monitor.py**: Neue Alerts (PRIO1_STARVED >10s), Trickle-Summary in Intervall-Report
- **loganalyse.sh**: Neue Sektionen PRIORITY_DISTRIBUTION, TRICKLE_HEY, HIGH_WATER_MARKS
- **Betroffene Dateien**: `src/esp32/esp32_main.cpp`, `tools/serial_monitor.py`, `tools/loganalyse.sh`

---

# Release Notes -- MeshCom Firmware v4.35n_20260315_fix3 (2026-03-15)

Bugfixes und Verbesserungen auf Basis von `oe1kbc_v4.35p`. Diese Version
laeuft als "U-Boot" (Vorab-Test) vor der offiziellen v4.35p.

---

## Aenderungen seit v4.35n_20260315_fix2

### Code-Alignment mit upstream/oe1kbc_v4.35p
Folgende Aenderungen aus unseren frueheren Fixes wurden zurueckgenommen, um keine
unnoetige Differenz zum Upstream zu erzeugen:

- **APRS IGate-Text**: Upstream verwendet `node_atxt` direkt im komprimierten LoRa-APRS
  Paket — kein Bug, User koennen eigenen ATXT setzen. Unseren Kommentar-Block entfernt
  und an Upstream angeglichen.
- **sendPosition Magic-Values**: `unsigned long` und `0xEEEE`/`0x9999` beibehalten wie
  Upstream. Kein Bug, Stilfrage — Intervalle sind konsistent `long`.

**Betroffene Dateien**: `src/aprs_functions.cpp`, `src/loop_functions.h`,
`src/loop_functions.cpp`, `src/command_functions.cpp`

---

## Aenderungen seit v4.35n_20260315_fix1

### 11. T-Deck / T-Deck Plus: CURRENT_LIMIT 240→140 — Boot-Hang behoben
- **Ursache**: `setCurrentLimit()` akzeptiert nur Werte im Bereich 0–140 mA. Die T-Deck-Varianten hatten `CURRENT_LIMIT 240` definiert. Der `setCurrentLimit()`-Aufruf war in aelteren Versionen auskommentiert (`/* ... */`) und hatte daher nie Auswirkung. Durch das Uncomment in fix1 wurde der Code aktiv — der ueberhoethe Wert loeste `RADIOLIB_ERR_INVALID_CURRENT_LIMIT` aus, woraufhin die Firmware in eine `while(true)`-Endlosschleife ging.
- **Auswirkung**: T-Deck und T-Deck Plus booteten nicht mehr und waren nicht bedienbar (Bug-Report 2026-03-15).
- **Fix**: `CURRENT_LIMIT` in beiden Varianten von 240 auf 140 mA korrigiert (Maximum des SX1262/SX1268 OCP-Bereichs, konsistent mit allen anderen Board-Varianten).
- **Betroffene Dateien**: `variants/t_deck/configuration.h`, `variants/t_deck_plus/configuration.h`

---

## Aenderungen seit v4.35n_20260315

### GPS_REFRESH_INTERVAL auf 5s zurueckgesetzt
- Die Aenderung auf 10s war nicht upstream-konform. Zurueck auf 5s um keine unnoetige Differenz zum Upstream zu erzeugen.
- **Betroffene Datei**: `src/configuration_global.h`

### sendMheard MESH/NCNT Feature uebernommen
- Kurts Weiterentwicklung in `sendMheard()` uebernommen: Beim BLE-Reconnect sendet die Firmware jetzt auch MESH- und NCNT-Felder (getValue Felder 9+10) im JSON an die Phone-App. Zuvor wurden diese Felder bei uns als Bug geflagged — nach Analyse ist es ein sinnvolles Feature.
- **Betroffene Datei**: `src/mheard_functions.cpp`

---

## Bugfixes

### 1. MHeard memcpy Bounds-Check und strcmp→memcmp
- **Ursache**: `memcpy` in `updateMheard()` kopierte `mh_callsign.length()` Bytes ohne Bounds-Check in den 10-Byte `mheardCalls`-Slot. Bei Callsigns >= 10 Zeichen wurde ueber die Slot-Grenze geschrieben und der Nachbar-Eintrag korrumpiert. Zusaetzlich verglich `strcmp()` an 3 Stellen ohne Laengenbegrenzung — bei nicht sauber terminierten Buffer-Eintraegen konnte ueber die Slot-Grenze hinausgelesen und der falsche Eintrag gematcht werden.
- **Auswirkung**: Bug-Report Rainer (2026-03-15): Gespeicherte MHeard-Eintraege zeigten falsche HW-Typen (z.B. Heltec V3 als TLORA_V2) und gemixte Callsigns. Frische Eintraege waren korrekt.
- **Fix**: `memcpy` mit `min(length, sizeof-1)` begrenzt. `strcmp` durch `memcmp` mit expliziter max-length (`ivgll`) ersetzt — die Laengenberechnung existierte in `updateHeyPath` bereits, wurde aber nicht genutzt.
- **Betroffene Datei**: `src/mheard_functions.cpp`

### 2. MHeard ncount-Quelle, Comma-Parsing und 12h-Zeitfenster
- **Ursache 1**: `mheardNCount[ipos]` in `updateMheard()` und `updateHeyPath()` nutzte den stale Array-Wert statt des aktuellen Paket-Werts. Bei neuen Eintraegen war der Wert 0 oder stammte von einem frueheren Node.
- **Ursache 2**: Comma-Check in `updateHeyPath()` blockierte gueltige Path-Payloads die Kommas enthielten und verhinderte still den NCount-Update.
- **Ursache 3**: `getMheardCount()` zaehlte nur Nodes der letzten Stunde (60*60). Nodes die alle 30min eine Position senden wurden nach 60min nicht mehr gezaehlt.
- **Fix**: `mheardLine.mh_ncount` (Wert aus aktuellem Paket) statt `mheardNCount[ipos]`. Comma-Check-Block entfernt. Zeitfenster auf 12h erweitert (60*60*12), konsistent mit MHeard-Expiry. MESH/NCNT Felder aus `sendMheard()` JSON entfernt (unzuverlaessige Legacy-Daten).
- **Betroffene Datei**: `src/mheard_functions.cpp`

### 3. ExtUDP Null-Pointer-Schutz und Socket-Reset-Absicherung
- **Ursache**: `inputJson["dst"]` gibt `nullptr` zurueck wenn das JSON-Feld fehlt. Die direkte Zuweisung an `String` ist undefiniertes Verhalten. Nach `resetExternUDP()` in `sendExtern()` fiel die Ausfuehrung durch zu `endPacket()` auf dem geschlossenen Socket.
- **Auswirkung**: Absturz bei fehlerhaftem JSON von externen Quellen. Undefiniertes Verhalten nach Socket-Reset.
- **Fix**: Null-Checks und Laengenvalidierung fuer `dst` (max 9) und `msg` (max 150) vor Verarbeitung. `return` nach `resetExternUDP()` in beiden Write-Bloecken.
- **Betroffene Datei**: `src/extudp_functions.cpp`

### 4. POSINFO-Logs mit Zeitstempel fuer Log-Analyse
- **Ursache**: Die `[POSI]`-Log-Ausgaben in `setSMartBeaconing()` enthielten keinen Zeitstempel. Bei der Log-Analyse waren Speed-, Distance- und Rate-Entscheidungen nicht zeitlich einordbar.
- **Fix**: Tag von `[POSI]` auf `[POSINFO]` vereinheitlicht. Alle 7 `printf`-Stellen um `getTimeString()` Prefix erweitert.
- **Betroffene Datei**: `src/loop_functions.cpp`

### 5. GPS_REFRESH_INTERVAL von 5s auf 10s
- **Ursache**: GPS-Abfrage alle 5 Sekunden erzeugte unnoetige CPU-Last. Smart Beaconing arbeitet mit Distanz- und Richtungsaenderungen, nicht mit der GPS-Polling-Frequenz.
- **Fix**: `GPS_REFRESH_INTERVAL` von 5 auf 10 Sekunden.
- **Betroffene Datei**: `src/configuration_global.h`

### 6. Audio Log-Tag [audi] zu [audio]
- **Ursache**: Alle anderen Module nutzen vollstaendige Tags (`[CSMA]`, `[LOOP]`, `[POSINFO]`). Das abgekuerzte `[audi]` wurde von Log-Filtern nicht gefunden.
- **Fix**: Suchen-Ersetzen `[audi]` → `[audio]` in allen 16 Log-Ausgaben.
- **Betroffene Datei**: `src/esp32/esp32_audio.cpp`

### 7. MHeard/Path-Persistence Size-Check gegen Datenkorruption
- **Ursache**: Nach der MAX_MHEARD-Aenderung (20→120) hatten alte `mheard.dat`-Dateien auf SD eine andere Groesse als die neuen Arrays. `loadMHeardPersistence()` las ohne Groessencheck — die Byte-Grenzen stimmten nicht mehr, die gesamte MHeard-Tabelle wurde korrumpiert. Gleiches Problem bei `mhpath.dat` (MAX_MHPATH 30→150).
- **Auswirkung**: Bug-Report Rainer: Falsche HW-Typen und gemixte Callsigns bei gespeicherten Eintraegen nach Firmware-Update (T-DECK mit SD-Persistence).
- **Fix**: File-Size-Check vor dem Laden. Erwartete Groesse wird aus `sizeof()` der aktuellen Arrays berechnet. Bei Mismatch wird die alte Datei geloescht — die Tabelle wird frisch aus eingehenden Paketen aufgebaut.
- **Betroffene Datei**: `src/mheard_functions.cpp`

### 8. CONFFIN nach Config-Commands im gleichen Buffer senden
- **Ursache**: Beim BLE-Connect wurde CONFFIN (Config-Finish-Signal an die Phone-App) ueber `addBLEOutBuffer()` in den regulaeren toPhone-Buffer geschrieben, waehrend die Config-JSONs ueber `addBLEComToOutBuffer()` in den priorisierten ComToPhone-Buffer gingen. Da ComToPhone immer Vorrang hat, wurde CONFFIN erst nach allen Config-JSONs gesendet oder ging bei vollem Buffer verloren. Auf NRF52 wurde CONFFIN zusaetzlich direkt im BLE connect_callback aufgerufen (BLE-Task-Kontext statt Main-Loop).
- **Auswirkung**: Phone-App erhielt CONFFIN verzoegert oder gar nicht. Config-Sequenz wurde als unvollstaendig interpretiert.
- **Fix**: `sendConfigFinish()` nutzt jetzt `addBLEComToOutBuffer()`. CONFFIN wird nach allen Config-Commands und `sendMheard()` eingereiht (nicht mehr im connect_callback / connect_pending). Reihenfolge garantiert: Config-JSONs → MHeard → CONFFIN, alles im ComToPhone-Buffer.
- **Betroffene Dateien**: `src/nrf52/nrf52_ble.cpp`, `src/nrf52/nrf52_main.cpp`, `src/esp32/esp32_main.cpp`, `src/command_functions.cpp`
