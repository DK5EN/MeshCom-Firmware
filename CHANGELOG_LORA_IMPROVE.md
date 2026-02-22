# MeshCom Firmware 4.35k -- Patchdokumentation Branch `lora-improve`

**Firmware-Basis:** MeshCom 4.35k.02.19 (main branch)
**Branch:** `lora-improve`
**Datum:** 2026-02-22
**Autor:** DK5EN / Martin S. Werner
**Plattform:** ESP32 (Heltec WiFi LoRa 32 V3, SX1262 via RadioLib)
**Zweck:** Dieses Dokument dient sowohl als Managementbericht als auch als maschinenlesbare Referenz fuer einen AI Coding Agent, um bei kuenftigen Firmware-Releases automatisiert pruefen und patchen zu koennen.

---

## MANAGEMENT REPORT

### BLUF (Bottom Line Up Front)

Die Original-Firmware MeshCom 4.35k verliert unter optimalen Funkbedingungen (RSSI -33 dBm, SNR +6 dB, 3 Meter Abstand) **32% aller Nachrichten**. Die Ursache ist nicht das Funkmedium, sondern **8 Firmware-Bugs** in der LoRa-Zustandsmaschine, dem Ringbuffer-Management und dem ACK-System.

Der Branch `lora-improve` behebt alle 8 Bugs durch **minimale, zielgerichtete Patches** in 5 Quelldateien. Das Ergebnis:

| Metrik | Vorher (main) | Nachher (lora-improve) |
|--------|---------------|------------------------|
| Nachrichtenverlust (unidirektional) | 32% | **0%** |
| Ringbuffer-Ueberlauf-Ereignisse | 52-89 pro Testlauf | **0** |
| Retransmit-Verhalten | endlos, unkontrolliert | **max. 5 Versuche, 30s Intervall** |
| ACK-Erkennung (Non-Gateway) | defekt (nie erkannt) | **funktional** |
| DM-Retransmit trotz ACK | ja (endlos) | **gestoppt nach ACK** |
| RX-Blindzeit nach Paketempfang | bis 4.5s | **< 1ms** |
| Sende-Verzoegerung (CAD-Wait) | 7 Loop-Zyklen blind | **3 Loop-Zyklen** |

**Kernaussage:** Saemtlicher Nachrichtenverlust war firmware-bedingt. Kein einziges Paket ging durch Funkprobleme verloren.

### DRY (Don't Repeat Yourself) -- Aenderungsuebersicht

| # | Bug-ID | Kurzbeschreibung | Dateien | Zeilen |
|---|--------|------------------|---------|--------|
| 1 | BUG #1 | RX-Timeout-Race: Empfangene Pakete werden verworfen | esp32_main.cpp | ~1570-1608 |
| 2 | BUG #2 | Kein `startReceive()` nach Paketverarbeitung: 4.5s RX-Blindzeit | esp32_main.cpp, lora_functions.cpp | ~1622-1636, ~2751-2850 |
| 3 | BUG #3 | `OnHeaderDetect` setzt CAD-Zaehler zurueck: TX wird endlos verzoegert | lora_functions.cpp | ~1262-1272 |
| 4 | BUG #4 | CAD-Counter=7 statt echtem Channel Sensing: unnoetige Sendeverzoegerung | lora_functions.cpp | ~1076 |
| 5 | BUG #5 | Kein Retransmit-Limit und zu langer Timer (62s statt 30s) | lora_functions.cpp | ~1149-1221 |
| 6 | BUG #6 | Non-Gateway-ACK ohne Original-Message-ID: ACK wird nie erkannt | lora_functions.cpp | ~648-656 |
| 7 | BUG #7* | Relay-Nachrichten mit Retransmit-Flag: Ringbuffer-Ueberflutung | lora_functions.cpp | ~820-840 |
| 8 | BUG #8 | DM-Retransmit trotz ACK: Ringbuffer wird nicht bereinigt | lora_functions.cpp | ~473-501 |

*BUG #7 wird im RING_BUFFER_FIX_GUIDE als "FIX #1 (Relay fire-and-forget)" gefuehrt und entspricht BUG #3 im BUG_REPORT_RINGBUFFER_ANALYSE.

Zusaetzlich: 19 neue `[MC-DBG]` Debug-Meldungen fuer vollstaendige Transparenz der Zustandsmaschine, plus `startsWith`-Edge-Case-Fix fuer Nachrichten die mit `{` beginnen.

**Gesamte RAM-Kosten:** 30 Bytes (`retryCount[MAX_RING]`).
**Neue Dateien:** Keine.
**Neue Abhaengigkeiten:** Keine.
**Architektur-Aenderungen:** Keine.

---

## TEIL 1: FUNKTIONSWEISE DER GEPATCHTEN FIRMWARE

Dieser Abschnitt beschreibt, wie die Firmware **nach** Anwendung aller Patches funktioniert -- aus Sicht des Senders und des Empfaengers.

### 1.1 Empfangsweg (RX) -- Was sich fuer den Empfaenger aendert

#### 1.1.1 Keine RX-Blindzeiten mehr

**Vorher:** Nach jedem empfangenen Paket war der SX1262 im Standby-Modus und empfing nichts mehr, bis der naechste `RECEIVE_TIMEOUT` (4.5s) `startReceive()` aufrief. Zusaetzlich konnte der Timeout-Handler ein gerade empfangenes Paket ueberschreiben (Race Condition).

**Nachher (BUG #1 + BUG #2 gefixt):**

1. **Sofortige RX-Wiederaufnahme:** Unmittelbar nach `radio.readData()` in `checkRX()` wird `radio.startReceive()` aufgerufen -- **bevor** `OnRxDone()` die 780 Zeilen Paketverarbeitung durchlaeuft. Die Paketdaten liegen bereits im lokalen `payload`-Buffer; der SX1262 kann sofort das naechste Paket empfangen. --> Siehe [BUG #2](#bug-2----kein-startreceive-nach-paketverarbeitung)

2. **Timeout schützt pending Pakete:** Wenn der 4.5s-Timeout ablaeuft und `receiveFlag` bereits gesetzt ist (ein Paket wartet auf Verarbeitung), wird `startReceive()` **nicht** aufgerufen. Stattdessen wird nur der Timer zurueckgesetzt, und das Paket wird im gleichen Loop-Durchlauf verarbeitet. --> Siehe [BUG #1](#bug-1----rx-timeout-race-condition)

3. **Redundante Interrupt-Neukonfiguration entfernt:** Nach `checkRX()` wird die Interrupt-Verkettung nicht mehr doppelt durchgefuehrt. `checkRX()` konfiguriert den RX-Pfad intern vollstaendig. Die Main Loop setzt nur noch den Timeout-Timer. --> Siehe [BUG #2](#bug-2----kein-startreceive-nach-paketverarbeitung)

**Ergebnis fuer den Empfaenger:**
- Kein Paketverlust durch Firmware-bedingte Blindzeiten
- RSSI/SNR-Werte werden vor `startReceive()` gesichert und bleiben korrekt
- CRC-Fehler fuehren ebenfalls zum sofortigen RX-Neustart
- Jedes CRC-Error-Ereignis wird mit RSSI/SNR/Groesse protokolliert (Kollisions-Diagnose)

#### 1.1.2 Verbesserte Debug-Transparenz im RX-Pfad

| Debug-Meldung | Trigger | Nutzen |
|---------------|---------|--------|
| `RX_TIMEOUT_FIRE` | 4.5s-Timeout abgelaufen | Zeigt Timeout-Frequenz und Idle-Perioden |
| `RX_TIMEOUT skipped` | Timeout uebersprungen (Paket pending) | Beweist, dass BUG #1 Fix aktiv ist |
| `RX_FLAG_PROCESS` | `receiveFlag` wird verarbeitet | Exakter Zeitstempel der Paketverarbeitung |
| `RX_RESTARTED src=after_readData` | RX nach Datenlesung neu gestartet | Beweist, dass BUG #2 Fix aktiv ist |
| `ONRXDONE_TIME ms=N` | `OnRxDone()` fertig | Misst Verarbeitungszeit (vorher unsichtbar) |
| `CRC_ERROR rssi=... snr=...` | CRC-Fehler mit Kontext | Unterscheidet Kollision von Rauschen |
| `HDR_DETECT tx_wait=... cmd_ctr=...` | Praeambel erkannt | Zeigt, ob CAD-Zaehler korrekt weiterlaeuft |

### 1.2 Sendeweg (TX) -- Was sich fuer den Sender aendert

#### 1.2.1 Zuverlaessiges Retransmit-System

**Vorher:** Der Retransmit-Timer lag bei 62s (Kommentar im Code sagte faelschlich 320s). Es gab kein Retry-Limit -- Nachrichten konnten endlos wiederholt werden. Relay-Nachrichten wurden mit `status=0x00` markiert und loesten ebenfalls Retransmits aus, obwohl der Relay-Knoten nie ein passendes ACK erhalten konnte.

**Nachher (BUG #5 + BUG #7 + Retry-System):**

1. **30-Sekunden Retransmit-Intervall:** Der Schwellwert wurde von `0x20` (62s) auf `0x10` (30s) geaendert. 30s liegt ueber der P90-Latenz (22.5s), sodass keine vorzeitigen Duplikate entstehen. --> Siehe [BUG #5](#bug-5----retransmit-timer-zu-lang-kein-limit)

2. **Maximal 5 Retransmit-Versuche:** Ein neues `retryCount[MAX_RING]`-Array (30 Bytes) zaehlt die Wiederholungen pro Nachricht. Nach 5 Versuchen (= 150s / 2.5 Minuten) wird aufgegeben (`RETRANSMIT_GIVEUP`). --> Siehe [BUG #5](#bug-5----retransmit-timer-zu-lang-kein-limit)

3. **Festes 30s-Intervall (kein exponentieller Backoff):** Jeder Retry wartet gleich lang (30s). Die Entscheidung fuer festes Intervall statt exponentiellem Backoff basiert auf der Beobachtung, dass bei konstanten Funkbedingungen ein kuerzeres Intervall die Zuverlaessigkeit erhoet.

4. **Relay ist Fire-and-Forget:** Alle Relay-Nachrichten (empfangen von anderen Knoten zur Weiterleitung) erhalten `status=0xFF`. Der Relay-Knoten sendet einmal und vergisst. Nur der **urspruengliche Absender** retransmittiert. --> Siehe [BUG #7](#bug-7----relay-nachrichten-mit-retransmit-flag)

5. **`retryCount` wird korrekt verwaltet:**
   - Zurueckgesetzt auf 0 bei jeder neuen Nachricht (4 Einfuege-Stellen)
   - Inkrementiert bei jeder Retransmit-Kopie
   - Zurueckgesetzt bei erfolgreicher ACK-Erkennung (cancel-on-RX)

#### 1.2.2 Funktionierendes ACK-System

**Vorher:** Non-Gateway-Knoten sendeten ACK-Pakete ohne die Original-Message-ID in Bytes 6-9. Der Sender konnte das ACK daher nie der Original-Nachricht zuordnen. Bei DM-Nachrichten wurde das ACK zwar ueber `checkOwnTx()` erkannt und dem Phone gemeldet, aber der Ringbuffer-Eintrag wurde nicht bereinigt -- die Nachricht wurde trotz ACK weiter retransmittiert.

**Nachher (BUG #6 + BUG #8 gefixt):**

1. **ACK enthaelt Original-Message-ID:** Im Nicht-Gateway-ACK-Pfad werden `print_buff[6..9]` mit `aprsmsg.msg_id` gefuellt -- identisch zum Gateway-Pfad. --> Siehe [BUG #6](#bug-6----ack-ohne-original-message-id)

2. **DM-ACK bereinigt den Ringbuffer:** Nach erfolgreichem `checkOwnTx()` wird der Ringbuffer durchsucht. Jeder Eintrag mit passender `msg_id` wird auf `status=0xFF` gesetzt und `retryCount` zurueckgesetzt. --> Siehe [BUG #8](#bug-8----dm-retransmit-trotz-ack)

3. **Generische cancel-on-RX funktioniert weiterhin:** Wenn ein Nicht-ACK-Paket mit gleicher `msg_id` empfangen wird (Echo der eigenen Nachricht durch Relay), wird der Ringbuffer-Eintrag ebenfalls auf `0xFF` gesetzt.

#### 1.2.3 Schnellere TX-Initiation

**Vorher:** Vor jedem Sendevorgang wurde `cmd_counter=7` gesetzt, was 7 Main-Loop-Zyklen blinden Wartens bedeutete. `OnHeaderDetect()` setzte zusaetzlich `cmd_counter=0` und `tx_waiting=false` bei jeder erkannten Praeambel -- auch von fremden Stationen. Auf einem belebten Kanal konnte dies den Sendevorgang endlos verzoegern.

**Nachher (BUG #3 + BUG #4 gefixt):**

1. **CAD-Counter auf 3 reduziert:** Der blinde Backoff-Delay wurde von 7 auf 3 Iterationen halbiert. Ein echtes Hardware-CAD (`radio.startChannelScan()`) ist fuer eine umfassende Neuimplementierung vorgesehen. --> Siehe [BUG #4](#bug-4----cad-counter7-blinder-delay)

2. **`OnHeaderDetect` setzt CAD-Zaehler nicht mehr zurueck:** `cmd_counter` und `tx_waiting` bleiben erhalten. Nur `is_receiving=true` wird gesetzt, was TX waehrend des aktiven Empfangs blockiert. Nach Empfangsende wird der CAD-Countdown normal fortgesetzt. --> Siehe [BUG #3](#bug-3----onheaderdetect-setzt-cad-zaehler-zurueck)

#### 1.2.4 Robustes `startsWith`-Verhalten

**Vorher:** Die Pruefung `aprsmsg.msg_payload.startsWith("{")` markierte jede Nachricht, die mit `{` beginnt, als Systemnachricht (`status=0xFF`, kein Retransmit). Benutzernachrichten wie `"{test} hallo"` wurden faelschlich nicht retransmittiert.

**Nachher:** Die Pruefung wurde praezisiert auf `startsWith("{CET}")`, `startsWith("{MCP}")` und `startsWith("{SET}")`. Nur echte Systemnachrichten werden erkannt. --> Siehe [startsWith Edge Case](#startswith-edge-case)

### 1.3 Ringbuffer -- Was sich strukturell aendert

#### 1.3.1 Ringbuffer-Architektur (unveraendert)

```
ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5]   (MAX_RING=30, 260 Bytes pro Eintrag)
Eintrag-Layout:
  [0]     = Nachrichtenlaenge
  [1]     = Status-Byte (0x00=senden, 0x01=gesendet, 0x02-0x0F=Timer, 0x10=Retransmit, 0xFF=erledigt)
  [2]     = Nachrichtentyp (0x3A=Text, 0x41=ACK, 0x21=Position)
  [3..6]  = 4-Byte Message-ID (Little-Endian)
  [7+]    = Nutzdaten (APRS-kodiert)
```

#### 1.3.2 Was sich geaendert hat

| Aspekt | Vorher (main) | Nachher (lora-improve) |
|--------|---------------|------------------------|
| Status `0x7F` (Retransmit-Kopie) | Spezial-Status, wird von `doTX()` sofort auf `0xFF` gesetzt | **Entfernt.** Retransmit-Kopien starten mit `0x01` und durchlaufen den normalen Timer-Zyklus |
| Retry-Tracking | Keines | `retryCount[MAX_RING]` (30 Bytes), max. 5 Versuche |
| Relay-Eintraege | `status=0x00` (Retransmit aktiv) | `status=0xFF` (Fire-and-Forget) |
| Ueberlauf-Erkennung | Stumm (iRead wird vorgerueckt) | `[MC-DBG] RING_OVERFLOW` wird **immer** geloggt |
| Zustandsuebersicht | Keine | `[MC-DBG] RING_STATUS` alle 30s (queued/pending/retrying/done) |

#### 1.3.3 Neuer Retransmit-Lebenszyklus einer eigenen Nachricht

```
1. Nachricht wird erstellt:
   ringBuffer[slot][1] = 0x00, retryCount[slot] = 0

2. doTX() sendet die Nachricht:
   ringBuffer[slot][1] = 0x01 (Timer startet)

3. Alle 2 Sekunden: updateRetransmissionStatus()
   ringBuffer[slot][1]++ (0x01 -> 0x02 -> ... -> 0x10)

4a. ACK empfangen (vor Schwellwert):
    ringBuffer[slot][1] = 0xFF, retryCount[slot] = 0
    --> Retransmit gestoppt

4b. Schwellwert 0x10 erreicht (30s ohne ACK):
    - Pruefung: retryCount[slot] >= MAX_RETRANSMIT (5)?
      Ja  --> ringBuffer[slot][1] = 0xFF, RETRANSMIT_GIVEUP
      Nein --> Original-Slot auf 0xFF
              Kopie nach ringBuffer[iWrite] mit status=0x01
              retryCount[iWrite] = retryCount[slot] + 1
              --> Naechster Retry-Zyklus

5. Nach 5 Versuchen (150s / 2.5 Minuten):
   RETRANSMIT_GIVEUP --> Nachricht wird aufgegeben
```

---

## TEIL 2: ALLE AENDERUNGEN IM DETAIL

Dieser Abschnitt beschreibt jede einzelne Aenderung mit Bezug zum jeweiligen Bug. Die Reihenfolge entspricht der Commit-Historie.

### Commit-Historie (main -> lora-improve)

```
921e29d  Add firmware fix guide
b5e1113  Fix LoRa RX blind windows causing ~32% message loss
a328d4f  Add BUG #4/#5 fixes, CRC diagnostics, update fix guide
539e9ea  Add ring buffer overflow fix guide for remaining message loss
60fdf8c  Fix ring buffer overflow: relay fire-and-forget + retransmit cap
432fc17  Add detailed bug report analyzing 6 root causes (German)
ccfc411  Bug Report submitted
8348dcc  Fix non-gateway ACK missing original msg_id (BUG #6)
5db1be6  Change retransmit to fixed 30s interval, increase to 5 retries
1aeb113  Fix DM retransmit despite ACK (BUG #8) + startsWith edge case
1f5dc1a  Add firmware binary and OTA screenshot
1dda2bb  Simplify README
1f64c62  Replace firmware binary with standardized naming
```

### Aenderung 1: RX-Timeout-Schutz (BUG #1)

**Datei:** `src/esp32/esp32_main.cpp`, Zeilen ~1570-1608
**Commit:** `b5e1113`
**Referenz:** [BUG #1](#bug-1----rx-timeout-race-condition)

**Was wurde geaendert:**
- Vor dem Timeout-Reset wird `receiveFlag` geprueft
- Falls gesetzt: Timer wird nur zurueckgesetzt, Paket bleibt erhalten
- Falls nicht gesetzt: `startReceive()` wird **vor** der Interrupt-Neukonfiguration aufgerufen
- Interrupt-Gating (`bEnableInterruptReceive`, `bEnableInterruptTransmit`) wird sauber deaktiviert/reaktiviert

**Wirkung:** Eliminiert das Zeitfenster, in dem DIO1-Interrupts ohne Handler feuern. Verhindert das Verwerfen pending Pakete durch den Timeout-Handler.

### Aenderung 2: Sofortiger RX-Neustart nach Datenlesung (BUG #2)

**Datei:** `src/esp32/esp32_main.cpp`, Zeilen ~2751-2850 und ~1622-1636
**Commit:** `b5e1113`
**Referenz:** [BUG #2](#bug-2----kein-startreceive-nach-paketverarbeitung)

**Was wurde geaendert:**
- In `checkRX()`: Nach `radio.readData()` wird sofort `radio.startReceive()` aufgerufen
- RSSI/SNR/FreqError werden **vor** dem RX-Neustart aus den Hardware-Registern gelesen und in lokalen Variablen gesichert
- `OnRxDone()` erhaelt die gesicherten RSSI/SNR-Werte statt direkter Hardware-Zugriffe
- Bei CRC-Fehler: Ebenfalls sofortiger RX-Neustart
- In der Main Loop nach `checkRX()`: Redundante Interrupt-Neukonfiguration (13 Zeilen) entfernt, nur noch Timeout-Timer-Reset
- CRC-Error-Logging erweitert um RSSI, SNR, Frequenzfehler, Paketgroesse und Zeitstempel

**Wirkung:** Der SX1262 ist innerhalb von Mikrosekunden nach dem FIFO-Lesen wieder empfangsbereit. Die Verarbeitungszeit von `OnRxDone()` (780 Zeilen Code, typisch 10-50ms) ist kein Blindfenster mehr.

### Aenderung 3: OnHeaderDetect-Fix (BUG #3)

**Datei:** `src/lora_functions.cpp`, Zeilen ~1262-1272
**Commit:** `b5e1113`
**Referenz:** [BUG #3](#bug-3----onheaderdetect-setzt-cad-zaehler-zurueck)

**Was wurde geaendert:**
- `tx_waiting=false` entfernt
- `cmd_counter=0` entfernt
- Nur noch `is_receiving=true` wird gesetzt
- Debug-Meldung erweitert: `[MC-DBG] HDR_DETECT tx_wait=%d cmd_ctr=%d`

**Wirkung:** Praeambel-Erkennungen von anderen Stationen unterbrechen den eigenen Sendevorgang nicht mehr. Der CAD-Countdown laeuft nach dem Empfang normal weiter.

### Aenderung 4: CAD-Counter reduziert (BUG #4)

**Datei:** `src/lora_functions.cpp`, Zeile ~1076
**Commit:** `a328d4f`
**Referenz:** [BUG #4](#bug-4----cad-counter7-blinder-delay)

**Was wurde geaendert:**
- `cmd_counter=7` geaendert zu `cmd_counter=3`
- Auskommentierte Debug-Zeilen entfernt
- Neue Debug-Meldung: `[MC-DBG] CAD_WAIT remaining=%d`

**Wirkung:** Sendeverzoegerung halbiert. Zusammen mit BUG #3-Fix wird TX nicht mehr endlos durch fremde Praeambeln blockiert.

### Aenderung 5: Retransmit-Timer und Retry-Limit (BUG #5)

**Datei:** `src/lora_functions.cpp`, Zeilen ~1149-1221
**Datei:** `src/loop_functions.cpp`, Zeile 252 (neues Array)
**Datei:** `src/loop_functions_extern.h` (extern-Deklaration)
**Commits:** `a328d4f`, `60fdf8c`, `5db1be6`
**Referenz:** [BUG #5](#bug-5----retransmit-timer-zu-lang-kein-limit)

**Was wurde geaendert:**
- Neues Array `uint8_t retryCount[MAX_RING] = {0}` (30 Bytes RAM)
- `extern`-Deklaration in `loop_functions_extern.h`
- Schwellwert von `0x20` (62s) auf `0x10` (30s) geaendert
- `MAX_RETRANSMIT = 5` definiert (festes Intervall, 5 Versuche = max. 150s)
- Retransmit-Kopien starten mit `status=0x01` statt `0x7F`
- `retryCount[iWrite] = retryCount[ircheck] + 1` bei Kopie
- Giveup-Logik: `retryCount >= MAX_RETRANSMIT` --> `status=0xFF`
- `0x7F`-Spezialbehandlung in `doTX()` entfernt (8 Zeilen)
- `retryCount[iWrite] = 0` an 4 Einfuege-Stellen:
  1. `loop_functions.cpp` ~2365 (eigene Nachrichten)
  2. `lora_functions.cpp` ~170 (ACK-Relay)
  3. `lora_functions.cpp` ~855 (Relay-Nachrichten)
  4. `udp_functions.cpp` ~332 (UDP/Server-Nachrichten)
- `retryCount[ircheck] = 0` bei cancel-on-RX (Zeile ~205)
- Debug-Meldungen: `RETRANSMIT retry=N after_sec=S msg_id=ID`, `RETRANSMIT_GIVEUP retries=N msg_id=ID`

**Wirkung:** Nachrichten werden zuverlaessig innerhalb von 30s retransmittiert, maximal 5-mal. Kein endloses Retransmit mehr. Ringbuffer-Druck durch Retransmit-Kopien ist begrenzt.

### Aenderung 6: Non-Gateway-ACK mit Original-Message-ID (BUG #6)

**Datei:** `src/lora_functions.cpp`, Zeilen ~648-656
**Commit:** `8348dcc`
**Referenz:** [BUG #6](#bug-6----ack-ohne-original-message-id)

**Was wurde geaendert:**
- Im Nicht-Gateway-ACK-Pfad: `print_buff[6..9]` wird mit `aprsmsg.msg_id` gefuellt
- Identisch zum bereits korrekten Gateway-Pfad

**Wirkung:** Der Sender kann Non-Gateway-ACKs der Original-Nachricht zuordnen. `checkOwnTx()` findet den Match, und der Retransmit wird gestoppt. Betrifft Gruppennachrichten, Broadcasts, WLNK-1 und APRS2SOTA.

### Aenderung 7: Relay Fire-and-Forget (BUG #7 / RING_BUFFER_FIX_GUIDE FIX #1)

**Datei:** `src/lora_functions.cpp`, Zeilen ~820-840
**Commit:** `60fdf8c`
**Referenz:** [BUG #7](#bug-7----relay-nachrichten-mit-retransmit-flag)

**Was wurde geaendert:**
- Gesamter `if/else`-Block fuer Relay-Nachrichtentypen entfernt
- Ersetzt durch: `ringBuffer[iWrite][1] = 0xFF` fuer **alle** Relay-Nachrichtentypen
- Alte `[RETX]` Debug-Meldung ersetzt durch: `[MC-DBG] RELAY_QUEUED msg_id=ID type=TT len=N`

**Wirkung:** Relay-Nachrichten belegen nur noch einen Ringbuffer-Slot (statt potentiell unbegrenzter Retransmit-Kopien). Der Ringbuffer-Ueberlauf durch Relay-Traffic ist eliminiert.

### Aenderung 8: DM-ACK bereinigt Ringbuffer (BUG #8)

**Datei:** `src/lora_functions.cpp`, Zeilen ~497-501
**Commit:** `1aeb113`
**Referenz:** [BUG #8](#bug-8----dm-retransmit-trotz-ack)

**Was wurde geaendert:**
- Nach `checkOwnTx()` im DM-ACK-Pfad: Schleife ueber `ringBuffer[0..MAX_RING]`
- Vergleich der `msg_id` (Bytes 3-6) mit der rekonstruierten `msg_counter`
- Bei Match: `ringBuffer[ircheck][1] = 0xFF`, `retryCount[ircheck] = 0`
- Debug-Meldung: `[RETX] DM-ACK for retid:N stop retransmit msg-id:ID`

**Wirkung:** DM-Nachrichten werden nach ACK-Empfang nicht mehr weiter retransmittiert. Der Benutzer sah zwar schon vorher das ACK-Haekchen auf dem Phone, aber die Firmware sendete trotzdem bis zu 5 weitere Kopien -- verschwendete Sendezeit und Ringbuffer-Slots.

### Aenderung 9: startsWith-Edge-Case-Fix

**Datei:** `src/loop_functions.cpp`, Zeile ~2352
**Commit:** `1aeb113`
**Referenz:** [startsWith Edge Case](#startswith-edge-case)

**Was wurde geaendert:**
- `aprsmsg.msg_payload.startsWith("{")` ersetzt durch:
  `startsWith("{CET}") || startsWith("{MCP}") || startsWith("{SET}")`

**Wirkung:** Benutzernachrichten, die zufaellig mit `{` beginnen, werden nicht mehr als Systemnachrichten fehlklassifiziert und erhalten korrekterweise Retransmit-Faehigkeit.

### Aenderung 10: Ringbuffer-Ueberlauf-Logging

**Datei:** `src/loop_functions.cpp`, Zeile ~3757
**Commit:** `60fdf8c`

**Was wurde geaendert:**
- `Serial.println(F("[MC-DBG] RING_OVERFLOW"))` in `addRingPointer()` eingefuegt
- **Nicht** durch `bLORADEBUG` gegated -- wird immer geloggt

**Wirkung:** Ringbuffer-Ueberlaeufe sind jetzt immer sichtbar, unabhaengig von der Debug-Einstellung.

### Aenderung 11: Periodische Ringbuffer-Statusmeldung

**Datei:** `src/esp32/esp32_main.cpp`, Main Loop
**Commit:** `60fdf8c`

**Was wurde geaendert:**
- Alle 30 Sekunden wird `[MC-DBG] RING_STATUS queued=Q pending=P retrying=R done=D iW=W iR=R` geloggt
- Zaehlt: pending (0x00), retrying (0x01-0xFE), done (0xFF), queued (iWrite-iRead)

**Wirkung:** Ermoeglicht die Ueberwachung der Ringbuffer-Auslastung in Echtzeit.

### Aenderung 12: Vollstaendige Debug-Instrumentierung

**Dateien:** `src/esp32/esp32_main.cpp`, `src/lora_functions.cpp`
**Commits:** `b5e1113`, `a328d4f`, `60fdf8c`

19 neue `[MC-DBG]`-Meldungen, alle durch `bLORADEBUG` gegated (ausser RING_OVERFLOW):

| Tag | Datei | Trigger |
|-----|-------|---------|
| `RX_TIMEOUT_FIRE` | esp32_main.cpp | 4.5s-Timeout abgelaufen |
| `RX_TIMEOUT skipped` | esp32_main.cpp | Timeout uebersprungen (Paket pending) |
| `RX_RESTART src=timeout` | esp32_main.cpp | RX nach Timeout neu gestartet |
| `RX_FLAG_PROCESS` | esp32_main.cpp | receiveFlag verarbeitet |
| `RX_RESTARTED src=after_readData` | esp32_main.cpp | RX nach FIFO-Lesung neu gestartet |
| `RX_RESTARTED src=after_tx` | esp32_main.cpp | RX nach TX neu gestartet |
| `TX_GATE_ENTER` | esp32_main.cpp | TX-Queue wird geprueft |
| `TX_START` | esp32_main.cpp | doTX erfolgreich |
| `TX_DONE` | esp32_main.cpp | TX abgeschlossen |
| `CRC_ERROR` | esp32_main.cpp | CRC-Fehler mit vollem Kontext |
| `RING_STATUS` | esp32_main.cpp | Periodischer Ringbuffer-Status |
| `RING_OVERFLOW` | loop_functions.cpp | Ringbuffer voll (immer aktiv) |
| `ONRXDONE_TIME` | lora_functions.cpp | Verarbeitungszeit OnRxDone |
| `CAD_WAIT` | lora_functions.cpp | CAD-Countdown-Status |
| `RADIO_TX` | lora_functions.cpp | Tatsaechliche LoRa-Uebertragung |
| `HDR_DETECT` | lora_functions.cpp | Praeambel erkannt mit Zustand |
| `RELAY_QUEUED` | lora_functions.cpp | Relay-Nachricht eingereiht |
| `RETRANSMIT` | lora_functions.cpp | Retransmit mit Versuchsnummer |
| `RETRANSMIT_GIVEUP` | lora_functions.cpp | Retransmit aufgegeben (max. erreicht) |

---

## TEIL 3: BUG-REFERENZ

Dieser Abschnitt listet alle Bugs mit eindeutigen IDs fuer Querverweise. Jeder Bug-Eintrag enthaelt die Informationen, die ein AI Coding Agent benoetigt, um in einer neuen Firmware-Version zu pruefen, ob der Bug behoben wurde.

### BUG #1 -- RX-Timeout-Race-Condition

**Schweregrad:** Hoch
**Auswirkung:** ~50% der Paketverluste (ca. 25 von 50 verlorenen Paketen)
**Datei:** `src/esp32/esp32_main.cpp`
**Betroffene Zeilen (main):** 1570-1608
**Aenderungen:** [Aenderung 1](#aenderung-1-rx-timeout-schutz-bug-1)
**Quellen:** FIRMWARE_FIX_GUIDE.md Abschnitt 5 "BUG #1", BUG_REPORT_RINGBUFFER_ANALYSE.md Abschnitt 2

**Symptom:** Pakete gehen verloren, obwohl sie physisch empfangen wurden. Tritt alle ~4.5 Sekunden auf.

**Ursache:** Der `RECEIVE_TIMEOUT`-Handler (4.5s Watchdog) ruft `startReceive()` auf, ohne zu pruefen, ob `receiveFlag` bereits gesetzt ist. Wenn ein DIO1-Interrupt zwischen zwei Loop-Iterationen `receiveFlag=true` setzt und dann der Timeout-Handler auf der naechsten Iteration laeuft:
1. `clearPacketReceivedAction()` trennt den Interrupt-Handler
2. `startReceive()` setzt den Radio-Zustand zurueck und ueberschreibt den FIFO
3. `receiveFlag` ist noch `true`, aber die Daten sind bereits weg

**Pruef-Signatur fuer AI Agent:** Suche in `esp32_main.cpp` nach dem `RECEIVE_TIMEOUT`-Handler. Pruefe:
- Wird `receiveFlag` **vor** `startReceive()` geprueft?
- Wird bei gesetztem `receiveFlag` der Timeout uebersprungen?
- Wird `startReceive()` **vor** der Interrupt-Neukonfiguration aufgerufen?

**Fix-Muster:**
```cpp
if((iReceiveTimeOutTime + RECEIVE_TIMEOUT) < millis())
{
    if(receiveFlag)  // <-- MUSS vorhanden sein
    {
        iReceiveTimeOutTime = millis();  // nur Timer zuruecksetzen
    }
    else
    {
        // startReceive() VOR clearPacketReceivedAction()
    }
}
```

---

### BUG #2 -- Kein startReceive nach Paketverarbeitung

**Schweregrad:** Hoch
**Auswirkung:** ~28% der Paketverluste ("unerklaearte stille Drops")
**Dateien:** `src/esp32/esp32_main.cpp`, `src/lora_functions.cpp`
**Betroffene Zeilen (main):** esp32_main.cpp 1622-1636, 2751-2794
**Aenderungen:** [Aenderung 2](#aenderung-2-sofortiger-rx-neustart-nach-datenlesung-bug-2)
**Quellen:** FIRMWARE_FIX_GUIDE.md Abschnitt 5 "BUG #2"

**Symptom:** Nach jedem empfangenen Paket ist der Empfaenger fuer bis zu 4.5 Sekunden blind. Pakete die waehrend `OnRxDone()` eintreffen, gehen verloren.

**Ursache:** Nach `radio.readData()` wird `OnRxDone()` aufgerufen (780 Zeilen Verarbeitung), waehrend der SX1262 im Standby-Modus steht. Erst der naechste `RECEIVE_TIMEOUT` ruft `startReceive()` auf. Zusaetzlich wird nach `checkRX()` die Interrupt-Verkettung doppelt konfiguriert, was ein weiteres Blindfenster erzeugt.

**Pruef-Signatur fuer AI Agent:** Suche in `checkRX()` (esp32_main.cpp). Pruefe:
- Wird `radio.startReceive()` **direkt nach** `radio.readData()` aufgerufen?
- Wird `startReceive()` **vor** `OnRxDone()` aufgerufen?
- Werden RSSI/SNR-Werte vor `startReceive()` in lokale Variablen gesichert?
- Wird die Interrupt-Verkettung in `checkRX()` intern gehandhabt?
- Ist die redundante Interrupt-Neukonfiguration nach `checkRX()` in der Main Loop entfernt?

**Fix-Muster:**
```cpp
state = radio.readData(payload, ibytes);
if(state == RADIOLIB_ERR_NONE) {
    saved_rssi = radio.getRSSI();    // VOR startReceive
    saved_snr  = radio.getSNR();     // VOR startReceive
    radio.startReceive();            // SOFORT nach readData
    OnRxDone(payload, ibytes, saved_rssi, saved_snr);  // DANACH verarbeiten
}
```

---

### BUG #3 -- OnHeaderDetect setzt CAD-Zaehler zurueck

**Schweregrad:** Mittel
**Auswirkung:** Endlose TX-Verzoegerung auf belebten Kanaelen
**Datei:** `src/lora_functions.cpp`
**Betroffene Zeilen (main):** 1262-1272
**Aenderungen:** [Aenderung 3](#aenderung-3-onheaderdetect-fix-bug-3)
**Quellen:** FIRMWARE_FIX_GUIDE.md Abschnitt 5 "BUG #3", BUG_REPORT_RINGBUFFER_ANALYSE.md Abschnitt 5

**Symptom:** Nachrichten in der TX-Queue werden nicht gesendet, obwohl der Kanal frei ist. Jede fremde Praeambel startet den Sende-Countdown neu.

**Ursache:** `OnHeaderDetect()` setzt `cmd_counter=0` und `tx_waiting=false` bei jeder erkannten Praeambel -- auch von anderen Stationen. Dadurch wird der CAD-Countdown nach jedem fremden Paket komplett zurueckgesetzt.

**Pruef-Signatur fuer AI Agent:** Suche `OnHeaderDetect()` in `lora_functions.cpp`. Pruefe:
- Wird `cmd_counter` zurueckgesetzt? (Sollte NICHT)
- Wird `tx_waiting` zurueckgesetzt? (Sollte NICHT)
- Wird nur `is_receiving = true` gesetzt? (Sollte JA)

**Fix-Muster:**
```cpp
void OnHeaderDetect(void) {
    is_receiving = true;  // NUR dies
    // KEIN cmd_counter=0
    // KEIN tx_waiting=false
}
```

---

### BUG #4 -- CAD-Counter=7 (blinder Delay)

**Schweregrad:** Mittel
**Auswirkung:** Unnoetige Sendeverzoegerung von ~5s pro Paket
**Datei:** `src/lora_functions.cpp`
**Betroffene Zeilen (main):** 1076
**Aenderungen:** [Aenderung 4](#aenderung-4-cad-counter-reduziert-bug-4)
**Quellen:** FIRMWARE_FIX_GUIDE.md Abschnitt 5 "BUG #4", BUG_REPORT_RINGBUFFER_ANALYSE.md Abschnitt 5

**Symptom:** Jedes Paket braucht mindestens ~5s zum Senden. Bei vollem Buffer dauert es >100s, alle Pakete zu senden.

**Ursache:** `cmd_counter=7` setzt einen blinden Delay von 7 Loop-Iterationen. Es findet kein echtes Channel Activity Detection statt -- es ist nur ein Timer. `MAX_CAD_WAIT=10` ist definiert aber nie verwendet. `radio.startChannelScan()` wird nie aufgerufen.

**Pruef-Signatur fuer AI Agent:** Suche in `doTX()` nach `cmd_counter=`. Pruefe:
- Ist der Wert <= 3?
- Wird echtes CAD (`radio.startChannelScan()`) verwendet? (Waere ideal, aber nicht in diesem Patch-Scope)
- Ist `MAX_CAD_WAIT` tatsaechlich in Verwendung?

---

### BUG #5 -- Retransmit-Timer zu lang, kein Limit

**Schweregrad:** Kritisch
**Auswirkung:** Endlose Retransmit-Schleifen, Ringbuffer-Ueberflutung
**Dateien:** `src/lora_functions.cpp`, `src/loop_functions.cpp`, `src/loop_functions_extern.h`
**Betroffene Zeilen (main):** lora_functions.cpp 1149-1221
**Aenderungen:** [Aenderung 5](#aenderung-5-retransmit-timer-und-retry-limit-bug-5)
**Quellen:** FIRMWARE_FIX_GUIDE.md Abschnitt 5 "BUG #5", RING_BUFFER_FIX_GUIDE.md Abschnitt 4 "FIX #2", BUG_REPORT_RINGBUFFER_ANALYSE.md Abschnitt 6

**Symptom:** Nachrichten werden endlos retransmittiert. Ringbuffer laeuft ueber. LoRa-Kanal wird mit Duplikaten belastet.

**Ursache:** Der Retransmit-Schwellwert `0x20` (= 62s bei 2s-Intervall) ist zu hoch. Es gibt keinen Retry-Zaehler -- nach 62s wird eine Kopie erstellt, das Original auf `0xFF` gesetzt, aber die Kopie startet den Zyklus neu. In Kombination mit BUG #7 (Relay-Retransmit) erzeugt dies eine Kaskade.

**Pruef-Signatur fuer AI Agent:** Suche `updateRetransmissionStatus()` in `lora_functions.cpp`. Pruefe:
- Gibt es ein `retryCount`-Array oder aequivalenten Retry-Zaehler?
- Gibt es eine `MAX_RETRANSMIT`-Konstante (empfohlen: 3-5)?
- Ist der Schwellwert fuer den ersten Retransmit <= 0x10 (30s)?
- Werden Retransmit-Kopien mit `status=0x01` statt `0x7F` erstellt?
- Wird bei Ueberschreitung des Limits `status=0xFF` gesetzt?
- Wird `retryCount` bei ACK-Empfang zurueckgesetzt?
- Wird `retryCount` bei neuer Nachrichteneinfuegung zurueckgesetzt?

---

### BUG #6 -- ACK ohne Original-Message-ID

**Schweregrad:** Kritisch
**Auswirkung:** Kein einziges ACK von Non-Gateway-Knoten wird erkannt
**Datei:** `src/lora_functions.cpp`
**Betroffene Zeilen (main):** 648-656
**Aenderungen:** [Aenderung 6](#aenderung-6-non-gateway-ack-mit-original-message-id-bug-6)
**Quellen:** BUG_REPORT_RINGBUFFER_ANALYSE.md Abschnitt 7

**Symptom:** Der Sender retransmittiert endlos, obwohl der Empfaenger ACKs sendet. Betrifft alle Non-Gateway-Empfaenger.

**Ursache:** Im Nicht-Gateway-ACK-Pfad werden `print_buff[6..9]` (Original-Message-ID) nicht gesetzt. Der Gateway-Pfad setzt sie korrekt. Der Sender prueft via `checkOwnTx()` die Message-ID in Bytes 6-9 des ACK-Pakets -- findet aber nur undefinierte Werte.

**ACK-Paket-Struktur:**
```
Byte [0]    = 0x41 (ACK-Typ)
Byte [1-4]  = Neue ACK-Message-ID (millis())
Byte [5]    = Flags + max_hop
Byte [6-9]  = Original-Message-ID  <-- MUSS gesetzt sein
Byte [10]   = Node/GW-Flag
Byte [11]   = 0x00 (Ende)
```

**Pruef-Signatur fuer AI Agent:** Suche in `lora_functions.cpp` nach dem ACK-Aufbau (Typ `0x41`). Pruefe:
- Werden `print_buff[6..9]` im **Nicht-Gateway**-Pfad mit `aprsmsg.msg_id` gefuellt?
- Ist der Code identisch zum Gateway-Pfad?
- Suche nach: `print_buff[6]=aprsmsg.msg_id & 0xFF`

**Fix-Muster:**
```cpp
// Im Nicht-Gateway-Pfad, VOR print_buff[10]:
print_buff[6] = aprsmsg.msg_id & 0xFF;
print_buff[7] = (aprsmsg.msg_id >> 8) & 0xFF;
print_buff[8] = (aprsmsg.msg_id >> 16) & 0xFF;
print_buff[9] = (aprsmsg.msg_id >> 24) & 0xFF;
```

---

### BUG #7 -- Relay-Nachrichten mit Retransmit-Flag

**Schweregrad:** Kritisch
**Auswirkung:** Ringbuffer-Ueberflutung durch unkontrollierte Relay-Retransmits
**Datei:** `src/lora_functions.cpp`
**Betroffene Zeilen (main):** 820-840
**Aenderungen:** [Aenderung 7](#aenderung-7-relay-fire-and-forget-bug-7--ring_buffer_fix_guide-fix-1)
**Quellen:** RING_BUFFER_FIX_GUIDE.md Abschnitt 4 "FIX #1", BUG_REPORT_RINGBUFFER_ANALYSE.md Abschnitt 4

**Symptom:** Mit Mesh aktiv: 52-89 RING_OVERFLOW-Ereignisse pro Testlauf, 40-55% Nachrichtenverlust. Der Ringbuffer ist permanent voll.

**Ursache:** Relay-Text-Nachrichten (empfangen von anderen Knoten zur Weiterleitung) erhalten `status=0x00` (Retransmit aktiviert). Der Relay-Knoten ist nicht der Absender und erhaelt nie ein passendes ACK. Jede Relay-Nachricht belegt 64s lang einen Slot und erzeugt dann eine Retransmit-Kopie.

**Pruef-Signatur fuer AI Agent:** Suche in `OnRxDone()` den Relay-Pfad (wo `ringBuffer[iWrite]` mit empfangenen Daten gefuellt wird und `addRingPointer()` aufgerufen wird). Pruefe:
- Werden Relay-Text-Nachrichten mit `status=0xFF` markiert? (Sollte JA)
- Gibt es eine Unterscheidung zwischen eigenen und Relay-Nachrichten beim Status-Byte?
- Wird die `startsWith("{")`-Pruefung fuer Relay-Nachrichten noch verwendet? (Sollte NICHT -- alle Relay = 0xFF)

**Fix-Muster:**
```cpp
// Relay-Pfad: immer Fire-and-Forget
ringBuffer[iWrite][1] = 0xFF;  // kein Retransmit fuer Relay
```

**Hinweis:** Diese Entscheidung hat einen Trade-off: Mesh-Zuverlaessigkeit sinkt, da Relay-Nachrichten nur einmal gesendet werden. Fuer zukuenftige Versionen wird ein leichtgewichtiges End-to-End-ACK-System empfohlen.

---

### BUG #8 -- DM-Retransmit trotz ACK

**Schweregrad:** Kritisch
**Auswirkung:** Pro DM-Nachricht: 5 unnoetige Retransmits + 5 unnoetige ACKs = 10 zusaetzliche LoRa-Pakete
**Datei:** `src/lora_functions.cpp`
**Betroffene Zeilen (main):** 473-501
**Aenderungen:** [Aenderung 8](#aenderung-8-dm-ack-bereinigt-ringbuffer-bug-8)
**Quellen:** BUG_REPORT_DM_RETRANSMIT.md

**Symptom:** DM-Nachrichten werden trotz ACK-Empfang weiter retransmittiert. Der Benutzer sieht das ACK-Haekchen, aber die Firmware sendet weiter.

**Ursache:** Das DM-ACK-System verwendet textbasierte `:ackNNN`-Nachrichten statt 0x41-ACK-Pakete. Nach `checkOwnTx()` wird `own_msg_id[x][4]=0x02` gesetzt (Phone erhaelt ACK-Meldung), aber der **Ringbuffer-Eintrag** wird nicht auf `0xFF` gesetzt. Die generische cancel-on-RX-Schleife (Zeile 197-215) vergleicht `RcvBuffer+1` (msg_id des ACK-Pakets) mit dem Ringbuffer -- aber die ACK-Nachricht hat eine eigene, neue msg_id, die nicht mit der Original-msg_id uebereinstimmt.

**Pruef-Signatur fuer AI Agent:** Suche in `OnRxDone()` nach der Verarbeitung von `:ack` (DM-ACK-Empfang). Pruefe:
- Wird nach `checkOwnTx()` der **Ringbuffer** nach passenden Eintraegen durchsucht?
- Wird `ringBuffer[ircheck][1] = 0xFF` gesetzt fuer den Match?
- Wird `retryCount[ircheck]` zurueckgesetzt?

**Fix-Muster:**
```cpp
int iackcheck = checkOwnTx(msg_counter);
if(iackcheck >= 0) {
    own_msg_id[iackcheck][4] = 0x02;
    // NEU: Ringbuffer bereinigen
    for(int ircheck=0; ircheck<MAX_RING; ircheck++) {
        if(ringBuffer[ircheck][0] > 0 && ringBuffer[ircheck][1] != 0xFF) {
            unsigned int ring_msg_id = /* Bytes 3-6 extrahieren */;
            if(ring_msg_id == msg_counter) {
                ringBuffer[ircheck][1] = 0xFF;
            }
        }
    }
}
```

---

### startsWith Edge Case

**Schweregrad:** Niedrig
**Auswirkung:** Benutzernachrichten die mit `{` beginnen werden nicht retransmittiert
**Datei:** `src/loop_functions.cpp`
**Betroffene Zeilen (main):** 2352
**Aenderungen:** [Aenderung 9](#aenderung-9-startswith-edge-case-fix)
**Quellen:** BUG_REPORT_DM_RETRANSMIT.md Abschnitt 5

**Pruef-Signatur fuer AI Agent:** Suche in `sendMessage()` nach `startsWith("{")`. Pruefe:
- Wird nur nach spezifischen Praefixen (`{CET}`, `{MCP}`, `{SET}`) gesucht?
- Oder wird allgemein `startsWith("{")` verwendet? (Letzteres ist fehlerhaft)

---

## TEIL 4: GEAENDERTE DATEIEN -- ZUSAMMENFASSUNG

| Datei | Aenderungen | Bugs |
|-------|-------------|------|
| `src/esp32/esp32_main.cpp` | BUG #1 Fix (Timeout-Schutz), BUG #2 Fix (RX-Neustart in checkRX + Main Loop Cleanup), Debug A-H, CRC-Diagnose erweitert, RING_STATUS periodisch | #1, #2 |
| `src/lora_functions.cpp` | BUG #3 Fix (OnHeaderDetect), BUG #4 Fix (cmd_counter 7->3), BUG #5 Fix (Retransmit-System komplett), BUG #6 Fix (ACK msg_id), BUG #7 Fix (Relay fire-and-forget), BUG #8 Fix (DM-ACK Ringbuffer-Bereinigung), Debug I-L, N-P, 0x7F-Entfernung | #3, #4, #5, #6, #7, #8 |
| `src/loop_functions.cpp` | `retryCount[MAX_RING]` Array, startsWith-Fix, RING_OVERFLOW-Logging | #5, startsWith |
| `src/loop_functions_extern.h` | `extern uint8_t retryCount[MAX_RING]` | #5 |
| `src/udp_functions.cpp` | `retryCount[iWrite] = 0` bei UDP-Nachrichteneinfuegung | #5 |

---

## TEIL 5: ANLEITUNG FUER AI CODING AGENT

### 5.1 Zweck

Dieses Dokument dient als Eingabe fuer einen AI Coding Agent bei neuen Firmware-Releases. Der Agent soll:

1. **Pruefen:** Fuer jeden Bug (BUG #1 bis #8 + startsWith) anhand der "Pruef-Signatur" feststellen, ob der Bug in der neuen Version behoben wurde.
2. **Patchen:** Falls nicht behoben, die Patches aus dem `lora-improve` Branch auf die neue Version anwenden.
3. **Verifizieren:** Die Debug-Meldungen (`[MC-DBG]`) nutzen, um die Wirksamkeit der Patches zu bestaetigen.

### 5.2 Pruef-Workflow

```
Fuer jeden Bug in [BUG #1, #2, #3, #4, #5, #6, #7, #8, startsWith]:
  1. Lies die "Pruef-Signatur" des Bugs
  2. Suche die beschriebenen Code-Muster in der neuen Firmware
  3. Ergebnis:
     a) Bug ist behoben  --> Markiere als GESCHLOSSEN
     b) Bug existiert noch --> Wende den Patch an
     c) Code hat sich geaendert --> Adaptiere den Patch an die neue Struktur
```

### 5.3 Patch-Reihenfolge

Bei Anwendung auf eine neue Firmware-Version folgende Reihenfolge einhalten:

1. `retryCount[MAX_RING]` Array hinzufuegen + extern-Deklaration (Voraussetzung fuer alle Retransmit-Aenderungen)
2. BUG #1 (RX-Timeout-Schutz) -- isoliert testbar
3. BUG #2 (RX-Neustart nach readData) -- isoliert testbar
4. BUG #3 + #4 (OnHeaderDetect + CAD-Counter) -- zusammen testen
5. BUG #6 (ACK msg_id) -- isoliert testbar
6. BUG #7 (Relay fire-and-forget) -- isoliert testbar
7. BUG #5 (Retransmit-System) -- benoetigt retryCount
8. BUG #8 (DM-ACK Ringbuffer) -- benoetigt retryCount
9. startsWith-Fix -- trivial
10. Debug-Meldungen -- unabhaengig von Fixes

### 5.4 Erfolgskriterien

| Metrik | Zielwert |
|--------|----------|
| Nachrichtenverlust (unidirektional, 3m) | < 5% |
| RING_OVERFLOW-Ereignisse | 0 |
| RETRANSMIT_GIVEUP sichtbar | Ja (beweist Retry-Cap funktioniert) |
| ONRXDONE_TIME | Messbar, typisch 10-50ms |
| RX_TIMEOUT skipped | > 0 (beweist BUG #1 Fix aktiv) |
| RX_RESTARTED after_readData | 1 pro empfangenem Paket |
| RING_STATUS queued | Bleibt unter 20 |

### 5.5 Versprechen

Wir werden die Bug-Suche und das Patchen bei jeder neuen Firmware-Version durchfuehren, bis der Firmware-Maintainer alle hier dokumentierten Probleme in der offiziellen Version behoben hat. Dieses Dokument wird mit jeder neuen Version aktualisiert:

- Behobene Bugs werden als GESCHLOSSEN markiert
- Neue Bugs werden hinzugefuegt
- Patches werden an geaenderten Code angepasst
- Die Testergebnisse werden dokumentiert

**Ziel:** Eine stabile, nachweisbar zuverlaessige MeshCom-Firmware, die unter normalen Funkbedingungen null Prozent firmware-bedingten Nachrichtenverlust aufweist.

---

## ANHANG A: Referenzdokumente

| Dokument | Inhalt |
|----------|--------|
| `FIRMWARE_FIX_GUIDE.md` | Detaillierte Analyse BUG #1-#5, Patches, Debug-Meldungen, Verifizierungsplan |
| `RING_BUFFER_FIX_GUIDE.md` | Ringbuffer-Architektur, FIX #1 (Relay), FIX #2 (Retransmit-Cap), ADR-001 (Mesh-ACK) |
| `BUG_REPORT_RINGBUFFER_ANALYSE.md` | Deutschsprachiger Bug-Report mit Mermaid-Diagrammen, BUG #1-#6, eingereicht als GitHub Issue #708 |
| `BUG_REPORT_DM_RETRANSMIT.md` | BUG #7 und #8, DM-spezifische ACK-Analyse |

## ANHANG B: Bug-Querverweis-Matrix

| Bug-ID | FIRMWARE_FIX_GUIDE | RING_BUFFER_FIX_GUIDE | BUG_REPORT_RINGBUFFER | BUG_REPORT_DM_RETRANSMIT |
|--------|--------------------|-----------------------|-----------------------|--------------------------|
| BUG #1 | Abschnitt 5 "BUG #1" | -- | Abschnitt 2 | -- |
| BUG #2 | Abschnitt 5 "BUG #2" | -- | Abschnitt 3 | -- |
| BUG #3 | Abschnitt 5 "BUG #3" | -- | -- | -- |
| BUG #4 | Abschnitt 5 "BUG #4" | -- | Abschnitt 5 | -- |
| BUG #5 | Abschnitt 5 "BUG #5" | Abschnitt 4 "FIX #2" | Abschnitt 6 | -- |
| BUG #6 | -- | -- | Abschnitt 7 | -- |
| BUG #7 | -- | Abschnitt 4 "FIX #1" | Abschnitt 4 (als "BUG #3") | -- |
| BUG #8 | -- | -- | -- | Abschnitt 3 |
| startsWith | -- | -- | -- | Abschnitt 5 |

## ANHANG C: Nummerierungs-Hinweis

Die Bug-Nummern sind ueber die verschiedenen Dokumente nicht immer konsistent:

- `BUG_REPORT_RINGBUFFER_ANALYSE.md` verwendet BUG #1-#6 mit eigener Zaehlung
  - Dessen BUG #3 (Relay-Retransmit) = hier BUG #7
  - Dessen BUG #5 (kein Retransmit-Limit) = hier BUG #5
  - Dessen BUG #6 (ACK ohne msg_id) = hier BUG #6
- `RING_BUFFER_FIX_GUIDE.md` verwendet FIX #1 und FIX #2
  - FIX #1 (Relay fire-and-forget) = hier BUG #7
  - FIX #2 (Retransmit-Cap) = hier BUG #5
- Dieses Dokument verwendet die kanonische Nummerierung BUG #1-#8

Die Bug-IDs in diesem Dokument sind die **autoritativen** Referenzen fuer den AI Coding Agent.
