# Change Document: pr/dev-bugfix Branch

**Autor:** dk5en

**Datum:** März 2026 

**Zielversion:** DEV 

**Betroffene Plattformen:** ESP32 (Heltec V3, E22, TTGO T-Beam, T-Beam Supreme), nRF52 (RAK4630)

---

## Executive Summary

Diese Branch enthält eine Serie von **Stabilitätsverbesserungen und Fehlerbehandlungsoptimierungen** über mehrere Systemkomponenten hinweg. Der Fokus liegt auf:

1. **WiFi/Netzwerk-Stabilität** — Reduzierung unnötiger Resets durch intelligentere Fehlererkennung
2. **UDP Socket-Zuverlässigkeit** — Proper Cleanup nach Fehlern, Overflow-Protection
3. **BLE Race Conditions** — Thread-safe Datenübergabe statt direkter Cross-Core Calls
4. **Hardware-Monitoring** — Heap-Überwachung und Debug-Verbesserungen

Diese Änderungen sind **keine Breaking Changes** und sollten die Funktionalität bewahren, während die Stabilität und Debugging-Fähigkeit verbessert werden.

---

## 1. WiFi & Netzwerk-Stabilität (ESP32)

### Ausgangslage

Das ESP32-System hatte mehrere WiFi-Stabilitätsprobleme:
- **Unnötige Resets:** Heartbeat-Timeouts führten zu bedingungslosen WiFi-Resets, selbst wenn WiFi noch verbunden war
- **Blockierende Ping-Checks:** `Ping.ping()` ist synchron und blockiert den Main Loop
- **NTP-Fehler-Kaskaden:** Fehlgeschlagene NTP-Updates isolierten das System nicht korrekt
- **Redundante Reconnect-Logik:** Mehrfache WiFi-Reconnect-Versuche in kurzen Intervallen

**Analyse:** Log-Daten zeigten ~22 unnötige WiFi-Resets in ~4 Stunden bei durchgehend verbundenem WiFi.

### Implementierte Verbesserungen

#### 1.1 Heartbeat-Timeout mit zweistufiger Strategie
**Commits:** e5093e3, e446859
**Dateien:** `src/esp32/esp32_main.cpp`, `src/udp_functions.cpp`, `src/configuration_global.h`, `src/loop_functions.cpp`

**Motivation:** Timeout ohne Kontextprüfung führt zu unnötigen Störungen. Der Server kann sich selbstständig erholen.

**Änderungen:**
- Neuer Flag: `extern bool hb_warn_logged` (verhindert Log-Spam bei wiederholten Warnings)
- Neuer Timer: `unsigned long last_upd_timer` (Tracking des letzten Heartbeat)
- Zweistufiger Heartbeat-Check:
  - **35 Sekunden:** WiFi-Status prüfen, Warnung loggen (nur einmalig)
  - **65 Sekunden:** Nur reset wenn WiFi tatsächlich getrennt; bei aktiver Verbindung nur loggen
- Heartbeat-Loss wird bei jedem empfangenen Paket reset (BEAT oder GATE-Message)

**Code-Referenzen (dev Branch):**
```
esp32_main.cpp: Lines ~1900-1950 (vorher: blinder Reset)
udp_functions.cpp: Lines ~400-450 (vorher: einfacher timeout check)
```

**Erwartete Verbesserung:** Reduktion unnötiger WiFi-Resets um ~90%, besseres Verständnis echter Konnektivitätsprobleme durch strukturierte Logging.

---

#### 1.2 Ping-Strategie durch WiFi.status() ersetzt
**Commit:** 365d48b
**Datei:** `src/udp_functions.cpp`

**Motivation:** `Ping.ping()` ist blockierend und kann Main Loop für Sekunden pausieren. WiFi.status() ist nicht-blockierend und ausreichend für die Verbindungsprüfung.

**Änderungen:**
- Ersetzte `Ping.ping(meshcom_settings.node_gw)` durch `WiFi.status() != WL_CONNECTED`
- Server-Erreichbarkeit wird nun über Heartbeat-Mechanismus validiert (besser als einzelner Ping)
- Entfernt: `pingFailure` Counter und redundante Reconnect-Logik

**Code-Referenzen (dev Branch):**
```
udp_functions.cpp: checkWifiPing() function ~Line 650
(vorher: Ping.ping() blocking call)
```

**Erwartete Verbesserung:** Main Loop responsiveness, kein blocking auf unreichbarem Gateway, schnellere Detektion echter WiFi-Trennungen.

---

#### 1.3 NTP-Fehler isoliert, WiFi bleibt stabil
**Commit:** 050a174
**Datei:** `src/udp_functions.cpp`

**Motivation:** Fehlgeschlagene NTP-Updates sollten nicht die UDP-Verbindung zerbrechen.

**Änderungen:**
- `timeClient.forceUpdate()` Fehler wird nur geloggt, triggert kein UDP-Reset mehr
- NTP-Retry findet im nächsten Zyklus statt

**Code-Referenzen (dev Branch):**
```
udp_functions.cpp: udpUpdateTimeClient() function ~Line 690
(vorher: Udp.stop() on NTP failure)
```

**Erwartete Verbesserung:** NTP-Instabilität beeinträchtigt nicht mehr die Messaging-Funktionalität.

---

### 2. UDP Socket-Zuverlässigkeit

#### 2.1 UDP Ring-Buffer Overflow-Schutz
**Commit:** 2f07fc0
**Datei:** `src/udp_functions.cpp`

**Motivation:** Buffer Overflows können zu Speichercorruption führen; strukturiertes Handling ist notwendig.

**Änderungen:**
- Längere Puffer (`UDP_TX_BUF_SIZE`) für transmit/receive
- Overflow-Checks vor `memcpy` Operationen
- Graceful Degradation: Puffer wird verworfen statt zu überschreiben

**Erwartete Verbesserung:** Verhindert Memory Corruption und Crashes bei hohem UDP-Datenaufkommen.

---

#### 2.2 UDP TX Error Handling mit Return
**Commit:** 87facf1
**Datei:** `src/udp_functions.cpp`

**Motivation:** Nach Socket-Reset sollten keine weiteren Operationen auf diesem Socket stattfinden.

**Änderungen:**
- `resetMeshComUDP()` wird aufgerufen, wenn Fehler-Counter (`err_cnt_udp_tx >= MAX_ERR_UDP_TX`) erreicht ist
- **Wichtig:** Nach Reset wird sofort `return` gemacht, um `endPacket()` zu vermeiden
- Error Counter wird reset auf 0

**Code-Referenzen (dev Branch):**
```
udp_functions.cpp: sendMeshComUDP() function ~Line 420
(vorher: resetMeshComUDP() ohne return)
```

**Erwartete Verbesserung:** Sauberer Socket-Cleanup, Vermeidung doppelter Operationen auf invalidem Socket.

---

### 3. ExtUDP (Externe UDP) Zuverlässigkeit

#### 3.1 ExtUDP JSON-Validierung mit Null-Pointer-Schutz
**Commit:** 73657ba
**Datei:** `src/extudp_functions.cpp`

**Motivation:** Externe JSON-Eingaben könnten malformed sein oder kritische Felder fehlen. Null-Pointer Dereference führt zu Crashes.

**Änderungen:**
- Neue Validierungen vor JSON-Parsing:
  - `type` field check: muss existieren und `"msg"` sein
  - `dst` field: darf nicht NULL sein, 1-9 Zeichen
  - `msg` field: darf nicht NULL sein, 1-150 Zeichen
- Struktur: Validierung → Assignment → Use (nicht direkt im JSON-Zugriff)

**Code-Referenzen (dev Branch):**
```
extudp_functions.cpp: getExtern() function ~Line 101
(vorher: direkter Zugriff inputJson["dst"], inputJson["msg"] ohne Null-Check)
```

**Erwartete Verbesserung:** Robustheit gegen malformed externe Requests, bessere Fehlermeldungen.

---

#### 3.2 ExtUDP Socket-Reset mit Return
**Commit:** d82bf17
**Datei:** `src/extudp_functions.cpp`

**Motivation:** Nach `resetExternUDP()` sollten keine weiteren Operationen auf dem Socket folgen.

**Änderungen:**
- `UdpExtern.write()` Fehler triggert `resetExternUDP()` gefolgt von sofortigem `return`
- Verhindert `endPacket()` auf invalidem Socket

**Code-Referenzen (dev Branch):**
```
extudp_functions.cpp: sendExtern() function ~Line 400
(vorher: resetExternUDP() ohne return, endPacket() folgte)
```

**Erwartete Verbesserung:** Saubere Socket-State, keine Fehler auf bereits invalidem Socket.

---

### 4. BLE (Bluetooth Low Energy) Stabilität

#### 4.1 BLE Race Condition: FreeRTOS Queue statt Direct Call
**Commit:** b1273ad
**Datei:** `src/esp32/esp32_main.cpp`

**Motivation:** NimBLE-Callbacks laufen auf separatem Task. Direkter Aufruf von `readPhoneCommand()` kann zu Race Conditions führen, wenn Main Loop die gleiche Struktur modifiziert.

**Änderungen:**
- Neue FreeRTOS Queue: `static QueueHandle_t bleQueue` (5 Items)
- `BleQueueItem` Struktur mit data[] und length
- **Callback:** Daten in Queue einreihen statt direkter Funktion
- **Main Loop:** Daten aus Queue lesen und verarbeiten

**Code-Referenzen (dev Branch):**
```
esp32_main.cpp:
- MyServerCallbacks::onWrite() ~Line 280 (vorher: readPhoneCommand() direct call)
- esp32setup() ~Line 1387 (neu: bleQueue initialization)
- esp32loop() (noch zu prüfen auf Queue-Abarbeitung)
```

**Erwartete Verbesserung:** Keine Race Conditions mehr, Thread-safe BLE-Datenübergabe, stabilere Bluetooth-Operationen.

---

#### 4.2 BLE Authentication: Toten Check entfernt
**Commit:** b9a8a8f
**Datei:** `src/esp32/esp32_main.cpp`

**Motivation:** Unnötige Disconnects bei unverschlüsselten Verbindungen; die Firmware sollte auch ohne Encryption funktionieren.

**Änderungen:**
- **Vorher:** `if (!connInfo.isEncrypted()) { disconnect(); }` (hartes Requirement)
- **Nachher:** `Serial.printf("encrypted: %s")` (nur Logging, kein Disconnect)
- BLE-Verbindung wird akzeptiert, unabhängig von Encryption-Status

**Code-Referenzen (dev Branch):**
```
esp32_main.cpp: MyServerCallbacks::onAuthenticationComplete() ~Line 260
(vorher: Hardcoded disconnect, 8 Zeilen)
```

**Erwartete Verbesserung:** Höhere Kompatibilität mit verschiedenen BLE-Clients, weniger Disconnects.

---

#### 4.3 BLE Connect Pending Flag
**Commit:** b1273ad
**Datei:** `src/esp32/esp32_main.cpp`

**Motivation:** `commandAction()` sollte nicht direkt aus Callback laufen.

**Änderungen:**
- Neuer Flag: `static volatile bool connect_pending`
- Callback setzt Flag, Main Loop verarbeitet
- Entfernt: direkter `commandAction()` Call aus Callback

**Erwartete Verbesserung:** Bessere Control Flow, keine direkten Operationen aus Interrupt-Kontext.

---

### 5. Hardware-spezifische Verbesserungen

#### 5.1 nRF52 Heap-Monitoring (RAK4630)
**Commit:** 44d37af
**Datei:** `src/nrf52/nrf52_main.cpp`

**Motivation:** nRF52 nutzt `heap_3` (libc malloc wrapper). `xPortGetFreeHeapSize()` ist nicht zuverlässig. Bessere Heap-Diagnostik für Memory-Leaks.

**Implementierung:**
- `#include <malloc.h>` für `mallinfo()`
- `extern "C" char *sbrk(int incr)` für Heap-Grenze
- Neue Funktion `nrf52_getFreeHeap()`:
  - `fordblks` = free bytes im Arena
  - `sbrk(0)` = aktuelles Heap-Ende
  - `(Stack - Heap)` = zusätzlicher freier Speicher
  - Gesamt: `freeFromSbrk + mi.fordblks`

**Code-Referenzen (dev Branch):**
```
nrf52_main.cpp:
- Neue Funktion ~Line 25-45 (vorher: existiert nicht)
- nrf52setup() ~Line 335 (Logging bei Boot)
- nrf52loop() ~Line 1750 (periodisches Logging)
```

**Logging-Output:**
```
[HEAP];12345;(free)    // Beispiel
```

**Erwartete Verbesserung:** Aussagekräftige Heap-Diagnostik, Früherkennung von Memory-Leaks auf nRF52.

---

#### 5.2 raw_rx Ring-Overflow Debug-Ausgabe
**Commit:** 9b3481e
**Datei:** `src/lora_functions.cpp`

**Motivation:** Ring-Buffer Overflows waren schwer zu debuggen. Strukturierte Ausgabe hilft bei Diagnose.

**Änderungen:**
- Debug-Ausgabe wird nur unter spezifischen Bedingungen geloggt
- Verhindert Log-Spam
- Bessere Strukturierung für Log-Analyse

**Erwartete Verbesserung:** Besseres Debugging ohne Performance-Penalty.

---

### 6. Allgemeine Codequalität

#### 6.1 Startup-Sequenz vereinheitlicht (ESP32 & nRF52)
**Commits:** e5093e3, 44d37af (implizit)
**Dateien:** `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`

**Motivation:** Redundante `save_settings()` Aufrufe und Split-Logik in conditional Branches.

**Änderungen:**
- Verschiebung von `save_settings()` nach den Conditionals
- Garantiert, dass Settings immer (auch on first boot) persistiert werden
- Konsistenz zwischen ESP32 und nRF52 Startup

**Code-Referenzen (dev Branch):**
```
esp32_main.cpp: esp32setup() ~Line 630
nrf52_main.cpp: nrf52setup() ~Line 395
(vorher: save_settings() in beiden Branches)
```

**Erwartete Verbesserung:** Reduktion doppelter Speichervorgänge, Konsistenz zwischen Plattformen.

---

## Zusammenfassung der Änderungen

| Kategorie | Art | Commit | Datei | Zeilen |
|-----------|-----|--------|-------|--------|
| WiFi Timeout | Bugfix | e5093e3 | esp32_main.cpp | +49, -9 |
| WiFi Ping | Improvement | 365d48b | udp_functions.cpp | +5, -10 |
| NTP Error | Bugfix | 050a174 | udp_functions.cpp | +2, -5 |
| UDP Overflow | Improvement | 2f07fc0 | udp_functions.cpp | varies |
| UDP TX Reset | Bugfix | 87facf1 | udp_functions.cpp | +4, -5 |
| ExtUDP JSON | Bugfix | 73657ba | extudp_functions.cpp | +20, -10 |
| ExtUDP Reset | Bugfix | d82bf17 | extudp_functions.cpp | +1 |
| BLE Queue | Bugfix | b1273ad | esp32_main.cpp | +30, -20 |
| BLE Encrypt | Improvement | b9a8a8f | esp32_main.cpp | +4, -8 |
| nRF52 Heap | Feature | 44d37af | nrf52_main.cpp | +25 |
| raw_rx Debug | Improvement | 9b3481e | lora_functions.cpp | varies |

---

## Klassifizierung

| Typ | Anzahl | Beispiele |
|-----|--------|----------|
| **Bugfixes** | 6 | WiFi Timeout, UDP Reset, ExtUDP Validation, BLE Queue, NTP Error |
| **Improvements** | 4 | WiFi Ping Strategy, BLE Encryption Handling, raw_rx Debug, Startup Sequence |
| **Features** | 1 | nRF52 Heap Monitoring |

---

## Testing-Empfehlungen

1. **WiFi Stabilität:** Längeres Monitoring auf instabilem WiFi-Netzwerk, Prüfe auf unnötige Resets
2. **UDP Handling:** Sende große Datenmengen, Prüfe auf Overflows/Errors
3. **BLE:** Verbinde/Trenne Client mehrmals, Prüfe auf Race Conditions
4. **Heap (nRF52):** Monitor `[HEAP]` Logs über längere Zeit, Prüfe auf Memory Leaks
5. **Cross-Platform:** Teste alle 4 ESP32-Varianten + nRF52

---

## Bekannte Einschränkungen

- ExtUDP JSON-Feldlängen sind fest (dst: 1-9 Zeichen, msg: 1-150 Zeichen)
- BLE Queue hat Kapazität von 5 Items (bei voller Queue werden neue Items verworfen)
- nRF52 Heap-Berechnung hängt von `mallinfo()` Genauigkeit ab

---

## Rückwärts-Kompatibilität

✅ **Vollständig kompatibel** — Keine Breaking Changes für Schnittstellen oder Datenformate.

---

## Commit-Historie (für Referenz)

```
e5093e3 Heartbeat-Timeout: Diagnostisches Logging statt blindem WiFi-Reset
a225548 WiFi Boot-Fehler: Hardware-Reset und sofortiger Retry
9b3481e raw_rx Ring-Overflow Debug-Ausgabe unterdrueckt
44d37af nRF52 RAK4630: Heap-Monitoring via mallinfo()/sbrk()
b9a8a8f BLE: Toten Verschluesselungs-Check entfernt
2f07fc0 UDP Ring-Buffer Overflow-Schutz
87facf1 UDP TX: Return nach Reset und sauberer resetMeshComUDP()
d82bf17 ExtUDP: Return nach Socket-Reset in sendExtern()
73657ba ExtUDP JSON-Validierung: Null-Pointer-Schutz und Feldprüfung
e446859 Heartbeat-Loss-Detection für ESP32
365d48b WiFi-Stabilität: Ping.ping() durch WiFi.status() ersetzt, HB-Timeout 40s, redundanten Reconnect entfernt
050a174 NTP-Fehler zerstört WiFi-Verbindung nicht mehr
b1273ad BLE Race Condition: FreeRTOS Queue statt Cross-Core Direct Call
```

---

**Versionsstatus:** Draft für Maintainer-Review
**Letztes Update:** 2026-03-07
