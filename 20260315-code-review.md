# MeshCom Firmware -- Code Review 2026-03-15

Branch: `v4.35p_fixes_prio`
Scope: `src/` directory (~414 files, ~70k LoC)
Focus: Datentyp-Fehler, Klassen-Hierarchie, strukturelle Probleme, Speicher-Fragmentierung

---

## 1. BUGS (verifiziert, sofort behebbar)

### 1.1 UTC-Offset-Rechenfehler (3 Stellen)

**Dateien:**
- `src/mheard_functions.cpp:658`
- `src/mheard_functions.cpp:848`
- `src/web_functions/web_functions.cpp:963`

**Code (fehlerhaft):**
```cpp
unsigned long lt = mheardPathEpoch[iset] + ((60 * 60 + 24) * (int)meshcom_settings.node_utcoff);
//                                           ^^^^^^^^^^^^
//                                           = 3624  (FALSCH)
```

**Korrekt waere:**
```cpp
unsigned long lt = mheardPathEpoch[iset] + ((long)(meshcom_settings.node_utcoff * 3600.0));
```

**Warum:** `60 * 60 + 24 = 3624` statt `3600`. Bei UTC+1 ist die angezeigte mheard-Path-Zeit 24 Sekunden zu weit, bei UTC+12 fast 5 Minuten. Zusaetzlich wird `node_utcoff` (float, unterstuetzt Halbstunden-Zonen wie UTC+5:30) auf `int` gecastet, was die Halbstunden-Praezision verliert.

**Vergleich mit korrekten Stellen:**
- `loop_functions.cpp:389`: `meshcom_settings.node_utcoff * 3600.0` (richtig)
- `web_functions.cpp:1244`: `meshcom_settings.node_utcoff * 60 * 60` (richtig)
- `nrf52_main.cpp:966`: `TimeSpan(meshcom_settings.node_utcoff * 60 * 60)` (richtig)

---

### 1.2 Off-by-One in APRS-Parsing-Schleifen (15+ Stellen)

**Datei:** `src/aprs_functions.cpp`

**Code (Zeile 547):**
```cpp
for(unsigned int id=istarttext; id<=PayloadBuffer.length(); id++)
{
    if(PayloadBuffer.charAt(id) == '/' || ... || id == PayloadBuffer.length() || ipt > 25)
    {
        break;
    }
    aprspos.pos_atxt.concat(PayloadBuffer.charAt(id));
```

**Problem:** `id <= PayloadBuffer.length()` erlaubt den Zugriff auf Index == length, was Out-of-Bounds ist. `PayloadBuffer.charAt(length)` gibt `\0` zurueck (Arduino-Verhalten), aber die Schleife laeuft einen Durchlauf zu weit. Selbes Muster bei Zeilen 569, 573, 597, 601, 625, 629 und weiteren.

**Fix:** `id < PayloadBuffer.length()` verwenden.

---

## 2. DATENTYP-PROBLEME

### 2.1 float/double-Inkonsistenz auf ESP32

Der ESP32 hat keine Hardware-FPU fuer `double`. Jede double-Operation wird in Software emuliert (~10x langsamer als float).

| Datei | Zeile | Problem |
|-------|-------|---------|
| `bme680.cpp` | 175 | `double x=(double)fpress680/(double)fBasePress; x=-7990*log(x);` |
| `bmx280.cpp` | 307 | `double`-basierte Hoehen-Berechnung mit `log()` |
| `aprs_structures.h` | - | `double lat, lon, lat_d, lon_d` in `aprsPosition` |
| `gps_functions.h` | - | `double latitude, longitude, altitude` in `GPSData` |
| `phone_commands.cpp` | 25-27 | Kommentar: "Phone currently sends float and we cast to double. We should fix that" |

**Empfehlung:** Fuer Sensor-Berechnungen `float` und `logf()` verwenden. GPS-Koordinaten benoetigen `double` fuer ausreichende Praezision (6-7 Dezimalstellen).

### 2.2 pow() mit Integer-Rueckgabe

**Datei:** `src/batt_functions.cpp:354`
```cpp
const int adcMax = pow(2, resolution) - 1;
```
`pow()` gibt `double` zurueck, implizite Konversion zu `int` kann Rundungsfehler verursachen.

**Fix:** `const int adcMax = (1 << resolution) - 1;`

### 2.3 Signed/Unsigned-Vergleiche mit strlen()

**Dateien:** `command_functions.cpp:137,3809`, `loop_functions.cpp:3278,3798`
```cpp
for(int ipos=2; ipos<(int)strlen(msg_text); ipos++)
```
Expliziter Cast noetig weil `strlen()` `size_t` (unsigned) zurueckgibt. Funktioniert, aber inkonsistent -- manchmal wird gecastet, manchmal nicht.

### 2.4 Integer-Ueberlauf in Channel-Utilization

**Dateien:** `esp32_main.cpp:1782`, `nrf52_main.cpp:1110`
```cpp
unsigned int util = (unsigned int)((rx_ms + tx_ms) * 100 / window);
```
Bei hoher Auslastung kann `(rx_ms + tx_ms) * 100` einen `unsigned long`-Ueberlauf verursachen.

---

## 3. SPEICHER-FRAGMENTIERUNG

### 3.1 Arduino String-Proliferation

**320+ String-Objekte** im Codebase, **463 `.c_str()`-Aufrufe**.

Jede String-Manipulation (`+`, `concat()`, `substring()`) erzeugt Heap-Allokationen. Auf dem ESP32 (160-320 KB DRAM) fuehrt das zu Heap-Fragmentierung ueber Laufzeit.

**Kritische Stellen:**

| Datei | Problem |
|-------|---------|
| `aprs_structures.h` | `aprsMessage` hat 8 String-Member (msg_source_path, msg_source_call, etc.) |
| `aprs_structures.h` | `mheardLine` hat 6 String-Member |
| `t-deck/lv_obj_functions.cpp:178` | `static std::vector<std::pair<String, MsgBubble>> persisted_msgs` -- Strings in Vektoren |
| `lora_functions.cpp:957-958` | Temporaere String-Objekte fuer Suche: `String(",") + call + ","` |

**Empfehlung:** Fuer haeufig durchlaufenen Code (LoRa RX, APRS-Parsing) `char[]`-Buffers statt `String` verwenden. APRS-Strukturen mit festen `char`-Arrays statt `String` definieren.

### 3.2 Repeated new/delete fuer SDWrapper

**Datei:** `src/Displays/BaseDisplay/SD.cpp`

9 Stellen mit `sd = new SDWrapper()` gefolgt von `delete sd` in derselben Funktion. Jeder SD-Zugriff alloziert und gibt Heap-Speicher frei, was Fragmentierung foerdert.

**Empfehlung:** Statische oder Singleton-Instanz verwenden.

### 3.3 Event-Data-Allokationen (T-Deck)

**Datei:** `src/t-deck/lv_obj_functions.cpp`
```cpp
HeaderEventData *hed = new HeaderEventData();  // Zeile 2881
DeleteEventData *ded = new DeleteEventData();  // Zeile 2903
```
Pro Message-Bubble werden kleine Objekte auf dem Heap alloziert. Bei MSG_TAB_MAX_MESSAGES=50 entstehen 100+ Allokationen. Zwar korrekt freigegeben (im LV_EVENT_DELETE-Handler), aber das Muster fragmentiert den Heap.

### 3.4 Grosse Stack-Allokationen

| Datei | Zeile | Groesse |
|-------|-------|---------|
| `loop_functions.cpp` | 1671 | `char words[100][21]` = **2100 Bytes** |
| `loop_functions.cpp` | 616-618 | `pageLastText` Arrays = **2400 Bytes** (global) |
| `extudp_functions.cpp` | 202-235 | 4x `char/uint8_t [500]` = **2000 Bytes** auf dem Stack |
| `command_functions.cpp` | 112 | `char print_buff[600]` |
| `esp32_main.cpp` | 3497 | `char msg_buffer[600]` |

ESP32-Standard-Stack-Groesse ist 8 KB. Verschachtelte Funktionsaufrufe mit grossen Stack-Buffers koennen Stack-Ueberlauf verursachen.

---

## 4. STRUKTURELLE PROBLEME

### 4.1 Exzessiver globaler Zustand

**790+ globale Variablen**, hauptsaechlich in:
- `loop_functions.cpp` (100+ boolsche Flags, Ring-Buffer, Counter, Timer)
- `loop_functions_extern.h` (333 Zeilen extern-Deklarationen)

Beispiel `loop_functions_extern.h:159-202`:
```cpp
extern unsigned char ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5];
extern int iWrite;
extern int iRead;
extern volatile bool is_receiving;
extern volatile bool tx_is_active;
extern volatile unsigned long ch_util_rx_accum;
```

**Auswirkung:** Implizite Kopplung zwischen allen Modulen. Jede Datei kann jede Variable lesen/schreiben. Keine Kapselung, kein definierter Zugriff.

### 4.2 Race Conditions (Thread-Safety)

FreeRTOS-Tasks und ISRs greifen auf gemeinsame Daten ohne Synchronisierung zu:

| Shared State | Writer | Reader | Schutz |
|-------------|--------|--------|--------|
| `ringBuffer[]`, `iWrite`, `iRead` | LoRa ISR, UDP Task | Main Loop | **keiner** |
| `is_receiving`, `tx_is_active` | LoRa ISR | Main Loop | `volatile` (unzureichend) |
| `ch_util_rx_accum/tx_accum` | ISR + Main | Statistik-Ausgabe | `volatile` (unzureichend) |
| `rxBufInUse[2]` | ISR | Main Loop | **keiner** |

`volatile` verhindert Compiler-Optimierungen, bietet aber **keine Atomizitaet** auf ARM Cortex. Noetig waere `std::atomic<>` oder ein FreeRTOS-Mutex/Semaphore.

### 4.3 Ring-Buffer ohne Bounds-Check

**Datei:** `src/lora_functions.cpp:998-1001`
```cpp
if(size + 2 > UDP_TX_BUF_SIZE)
    size = UDP_TX_BUF_SIZE - 2;
memset(ringBuffer[iWrite], 0x00, UDP_TX_BUF_SIZE+1);
ringBuffer[iWrite][0]=size;
memcpy(ringBuffer[iWrite]+2, RcvBuffer, size);
```

Der Size-Check (Zeile 995-996) ist korrekt, aber das Muster ist an anderen Stellen (z.B. Zeile 824-826, 858-860) nicht konsistent implementiert. Es gibt keine Ueberlaufpruefung fuer den Ring selbst (`iWrite` vs `iRead` Kollision).

### 4.4 God-Files

| Datei | Zeilen | Verantwortlichkeiten |
|-------|--------|---------------------|
| `command_functions.cpp` | 4792 | 50+ Kommandos, Sensor-Setup, Netzwerk-Config |
| `loop_functions.cpp` | 3850 | Main Loop, Display, Timer, Globals |
| `esp32_main.cpp` | 3546 | ESP32-Init, GPS, Audio, WiFi, BLE |
| `lora_functions.cpp` | 1804 | LoRa RX/TX, Priority Queue, Retransmission |

### 4.5 Massive Include-Ketten

```
command_functions.cpp:  45 #includes
esp32_main.cpp:         90 #includes
```

Jede Aenderung an einem Header kann einen Rebuild des gesamten Projekts ausloesen.

### 4.6 Fehlender virtueller Destruktor

**Datei:** `src/Displays/BaseDisplay/base.h:29-31`
```cpp
~BaseDisplay() {        // NICHT virtual!
    freePageMemory();
}
```

`BaseDisplay` erbt von `GFX` (das keinen Destruktor hat) und hat 9 abgeleitete Klassen (DEPG0150BNS810, DEPG0154BNS800, etc.). Wird ein abgeleitetes Objekt ueber einen `GFX*`- oder `BaseDisplay*`-Zeiger geloescht, wird der Destruktor der abgeleiteten Klasse nicht aufgerufen.

**Fix:** `virtual ~BaseDisplay() { freePageMemory(); }`

### 4.7 Code-Duplikation

Ring-Buffer-Schreibmuster identisch an 10+ Stellen in `lora_functions.cpp`:
```cpp
ringBuffer[iWrite][0]=12;
ringBuffer[iWrite][1]=RING_STATUS_DONE;
memcpy(ringBuffer[iWrite]+2, print_buff, 12);
addTxRingEntry("...");
```

Das wurde teilweise refactored (`addTxRingEntry`), aber der `memcpy`-Teil davor ist immer noch dupliziert.

---

## 5. UNSAFE STRING OPERATIONS

### 5.1 strcpy() ohne Bounds-Check (21 Stellen)

| Datei | Zeile | Code |
|-------|-------|------|
| `esp32_main.cpp` | 833-834 | `strcpy(meshcom_settings.node_ssid, meshcom_settings.node_ossid)` |
| `esp32_main.cpp` | 3494 | `strcpy(msg_text, strText.c_str())` |
| `nrf52_main.cpp` | 518-519 | `strcpy(meshcom_settings.node_ssid, ...)` |
| `nrf52_main.cpp` | 2116 | `strcpy(msg_text, strText.c_str())` |
| `loop_functions.cpp` | 1573-1653 | 6x `strcpy()` in `pageLastText`-Arrays |
| `web_functions.cpp` | 1775 | `strcpy(message_text, message.c_str())` |

### 5.2 strcat() in Schleifen (Buffer-Overflow-Risiko)

**Datei:** `src/loop_functions.cpp:1770-1776`
```cpp
for(itxt=1; itxt<iwords; itxt++) {
    if((strlen(line_text) + strlen(words[itxt])) > 19) {
        strcat(line_text, words[itxt]);   // Zeile 1770 -- KEIN Size-Check
    } else {
        strcat(line_text, " ");           // Zeile 1775
        strcat(line_text, words[itxt]);   // Zeile 1776
    }
}
```

**Empfehlung:** `strlcpy`/`strlcat` oder `snprintf` verwenden.

---

## 6. KLASSEN-HIERARCHIE

### 6.1 Display-Vererbungskette

```
Print (Arduino)
  +-- GFX : public Print                    (kein Destruktor)
       +-- BaseDisplay : public GFX         (nicht-virtueller Destruktor)
            +-- DEPG0150BNS810
            +-- DEPG0154BNS800
            +-- DEPG0213BNS800
            +-- DEPG0290BNS75A
            +-- DEPG0290BNS800
            +-- GDEP015OC1
            +-- GDE029A1
            +-- LCMEN2R13EFC1
            +-- QYEG0213RWS800
```

**Probleme:**
1. Kein virtueller Destruktor (siehe 4.6)
2. `GFX` hat keinen Destruktor -- wenn `BaseDisplay` Speicher alloziert (page_black, page_red via `new[]`), ist das Freigeben nur im `BaseDisplay`-Destruktor garantiert, nicht bei Loeschung ueber `GFX*`.

### 6.2 Keine Abstraktion fuer Platform-spezifischen Code

Es gibt keine gemeinsame Basisklasse oder Interface fuer platform-spezifische Implementierungen. Stattdessen werden `#ifdef`-Bloecke verwendet:
- `esp32_main.cpp` vs `nrf52_main.cpp` haben stark duplizierten Code
- Gemeinsame Logik (Ring-Buffer, APRS-Parsing, mheard) wird in beiden Dateien identisch implementiert

---

## 7. ZUSAMMENFASSUNG NACH PRIORITAET

### Sofort beheben (Bugs)
1. **UTC-Offset-Rechenfehler** -- 3 Stellen, falsche mheard-Zeitanzeige (Abschnitt 1.1)
2. **Off-by-One in APRS-Parsing** -- 15+ Stellen, Out-of-Bounds-Zugriff (Abschnitt 1.2)
3. **pow() statt Bit-Shift** -- 1 Stelle, moeglicher Rundungsfehler (Abschnitt 2.2)

### Hoch (Stabilitaet/Sicherheit)
4. **strcpy/strcat ohne Bounds** -- 21+ Stellen, Buffer-Overflow-Risiko (Abschnitt 5)
5. **Race Conditions** -- Ring-Buffer + volatile Flags ohne Synchronisierung (Abschnitt 4.2)
6. **Ring-Buffer Overflow** -- fehlende iWrite/iRead Kollisionspruefung (Abschnitt 4.3)

### Mittel (Performance/Fragmentierung)
7. **String-Proliferation** -- 320+ String-Objekte fragmentieren Heap (Abschnitt 3.1)
8. **double statt float** -- unnoetige Software-FPU-Emulation (Abschnitt 2.1)
9. **SDWrapper new/delete** -- 9 Stellen, Fragmentierung (Abschnitt 3.2)
10. **Grosse Stack-Buffers** -- 2000+ Bytes in einzelnen Funktionen (Abschnitt 3.4)

### Niedrig (Wartbarkeit)
11. **790+ Globals** -- keine Kapselung (Abschnitt 4.1)
12. **God-Files** -- 3500-4800 Zeilen pro Datei (Abschnitt 4.4)
13. **Fehlender virtueller Destruktor** -- BaseDisplay (Abschnitt 4.6)
14. **Code-Duplikation** -- Ring-Buffer-Pattern (Abschnitt 4.7)
15. **Include-Ketten** -- 45-90 Includes pro Datei (Abschnitt 4.5)

---

*Review durchgefuehrt mit Claude Opus 4.6 am 2026-03-15*
