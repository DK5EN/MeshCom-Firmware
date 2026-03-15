# Diff-Report v4: v4.35p_fixes vs upstream/oe1kbc_v4.35p

Erstellt: 2026-03-15 10:28
Author: Martin (DK5EN)

Vergleichsbasis: `upstream/oe1kbc_v4.35p` (Stand 9e05b33) vs. `v4.35p_fixes` (Stand 6dee874)

Aenderungen seit v3: Kurt hat 2 weitere Commits gemacht (1862d09, 9e05b33) — Audio Tags und CONFFIN Timing uebernommen.

---

## BLUF (Bottom Line Up Front)

**Kurt hat weitere Fixes uebernommen — Audio komplett, CONFFIN Timing uebernommen:**

- **Neu erledigt (seit v3):** Audio Log-Tags (alle 15 verbleibenden Stellen), CONFFIN Timing (esp32_main, nrf52_ble, nrf52_main)
- **Uebernommen:** sendMheard MESH/NCNT Feature (Kurts Ansatz in unseren Code integriert)
- **Offen:** MHeard stale ncount, APRS IGate-Text, sendPosition Magic-Values, POSINFO printf-Bug, Persistence Size-Check, CONFFIN addBLEComToOutBuffer
- **Rueckschritt:** `mheardLine.mh_ncount = mheardNCount[ipos]` weiterhin vorhanden
- **Neues Problem:** `connect_pending` in esp32_main.cpp ist jetzt toter Code (wird gesetzt aber nie gelesen)

---

## TEIL 1: Noch offene Aenderungen — konkrete Code-Stellen

---

### 1. MHeard: Stale ncount

**Datei:** `src/mheard_functions.cpp`

#### Stale ncount Zuweisung entfernen (Zeile ~278)

```cpp
// AKTUELL bei Kurt (Zeile 278):
    mheardLine.mh_ncount = mheardNCount[ipos];

// FIX — Zeile entfernen.
// mheardLine.mh_ncount hat bereits den korrekten Wert aus dem
// empfangenen Paket. mheardNCount[ipos] ist der alte Array-Wert
// (bei neuen Eintraegen 0 oder von einem frueheren Node).
```

---

### 2. APRS IGate-Text

**Datei:** `src/aprs_functions.cpp`

**Zeilen ~1171-1176** (`encodeLoRaAPRScompressed`):
```cpp
// AKTUELL bei Kurt:
    String msgtext="(MeshCom)";
    if(meshcom_settings.node_atxt[0] != 0x00)
        msgtext = meshcom_settings.node_atxt;

    snprintf(msg_start, sizeof(msg_start), "%s>APLT00-1,WIDE1-1:=%c%c%c%c%c%c%c%c%c%c P[ %s",
        cSourceCall, meshcom_settings.node_symid, clat[0], clat[1], clat[2], clat[3],
        clon[0], clon[1], clon[2], clon[3], meshcom_settings.node_symcd, msgtext.c_str());

// FIX — IGate-Text auskommentieren, snprintf ohne %s:
    /* IGate-Text nicht im LoRa-APRS Paket — IGate identifiziert sich ueber Callsign
    String msgtext="(MeshCom)";
    if(meshcom_settings.node_atxt[0] != 0x00)
        msgtext = meshcom_settings.node_atxt;
    */

    snprintf(msg_start, sizeof(msg_start), "%s>APLT00-1,WIDE1-1:=%c%c%c%c%c%c%c%c%c%c P[",
        cSourceCall, meshcom_settings.node_symid, clat[0], clat[1], clat[2], clat[3],
        clon[0], clon[1], clon[2], clon[3], meshcom_settings.node_symcd);
```

---

### 3. sendPosition Magic-Values

**Dateien:** `src/loop_functions.h`, `src/loop_functions.cpp`, `src/command_functions.cpp`

#### Header (loop_functions.h, Zeile ~64):
```cpp
// AKTUELL:  void sendPosition(unsigned long intervall, ...);
// FIX:     void sendPosition(unsigned int intervall, ...);
```

#### Funktion (loop_functions.cpp, Zeile ~2698):
```cpp
// AKTUELL:  void sendPosition(unsigned long uintervall, ...)
// FIX:     void sendPosition(unsigned int uintervall, ...)
```

#### Lokale Variable (Zeile ~2709):
```cpp
// AKTUELL:  unsigned long intervall = uintervall;
// FIX:     unsigned int intervall = uintervall;
```

#### Magic Values (Zeilen ~2712, ~2720):
```cpp
// AKTUELL (Zeile 2712):  if(intervall == 0xEEEE)
// FIX:                   if(intervall == 1)

// AKTUELL (Zeile 2720):  ... intervall == 0x9999)
// FIX:                   ... intervall == 0)
```

#### Aufrufer (command_functions.cpp, Zeilen ~2672, ~2684):
```cpp
// AKTUELL (Zeile 2672):  sendPosition(0x9999, meshcom_settings.node_lat, ...);
// FIX:                   sendPosition(0, meshcom_settings.node_lat, ...);

// AKTUELL (Zeile 2684):  sendPosition(0xEEEE, meshcom_settings.node_lat, ...);
// FIX:                   sendPosition(1, meshcom_settings.node_lat, ...);
```

---

### 4. POSINFO printf-Bug

**Datei:** `src/loop_functions.cpp`, Zeile ~3541

```cpp
// AKTUELL bei Kurt (BUG — schliessende Klammer nach \n statt davor):
if(bGPSDEBUG) Serial.printf("%s [POSINFO]...WiFi connected & Stationary -> Suppressing drift (Rate: 1800s\n)", getTimeString().c_str());

// FIX:
if(bGPSDEBUG) Serial.printf("%s [POSINFO]...WiFi connected & Stationary -> Suppressing drift (Rate: 1800s)\n", getTimeString().c_str());
```

Ergebnis: `)` wird nach dem Zeilenumbruch gedruckt — sichtbar als einzelne `)` in der naechsten Log-Zeile.

---

### 5. MHeard/Path Persistence Size-Check

**Datei:** `src/mheard_functions.cpp`

#### loadMHeardPersistence() (Zeile ~863, nach `if(!file) return;`):
```cpp
// FIX — vor den file.read() Aufrufen einfuegen:
        size_t expected_mh = sizeof(mheardCalls) + sizeof(mheardBuffer) + sizeof(mheardLat)
                           + sizeof(mheardLon) + sizeof(mheardEpoch) + sizeof(mheardNCount);
        if(file.size() != expected_mh) {
            Serial.printf("[TDECK]...mheard.dat size mismatch (%u != %u), deleting\n", file.size(), expected_mh);
            file.close();
            SD.remove("/mheard.dat");
            return;
        }
```

#### loadPathPersistence() (Zeile ~897, nach `if(!file) return;`):
```cpp
// FIX — vor den file.read() Aufrufen einfuegen:
        size_t expected_path = sizeof(mheardPathCalls) + sizeof(mheardPathBuffer1)
                             + sizeof(mheardPathEpoch) + sizeof(mheardPathLen);
        if(file.size() != expected_path) {
            Serial.printf("[TDECK]...mhpath.dat size mismatch (%u != %u), deleting\n", file.size(), expected_path);
            file.close();
            SD.remove("/mhpath.dat");
            return;
        }
```

---

### 6. CONFFIN: addBLEOutBuffer → addBLEComToOutBuffer

**Datei:** `src/command_functions.cpp`, Zeile ~4791

Kurt hat das CONFFIN-Timing uebernommen (commandAction-Verschiebung), aber den Buffer-Typ nicht geaendert:

```cpp
// AKTUELL bei Kurt:
    addBLEOutBuffer(msg_buffer, strlen(print_buff) + 1);

// FIX:
    addBLEComToOutBuffer(msg_buffer, strlen(print_buff) + 1);
```

Grund: CONFFIN muss ueber den Command-Buffer gesendet werden, nicht den normalen Out-Buffer, damit es nach den Config-JSONs in der richtigen Reihenfolge ankommt.

---

### 7. Toter Code: connect_pending (esp32_main.cpp)

**Datei:** `src/esp32/esp32_main.cpp`

Kurt hat den `connect_pending`-Check-Block entfernt (korrekt), aber die Variable und das Setzen nicht:

```cpp
// Zeile 230 — Deklaration bleibt:
static volatile bool connect_pending = false;

// Zeile 258 — wird weiterhin gesetzt, aber nie gelesen:
        connect_pending = true;  // commandAction runs in Main Loop
```

Beide Zeilen koennen entfernt werden — rein kosmetisch, kein Bug.

---

## TEIL 2: Seit v3 neu erledigt

### Audio Log-Tags (alle 16/16) ✅
Kurt hat in den 2 neuen Commits alle 15 verbleibenden `[audi]` → `[audio]` Stellen gefixt. Alle 16 Tags sind jetzt korrekt.

### sendMheard MESH/NCNT Feature ✅
Kurts Ansatz uebernommen: In `sendMheard()` werden Felder 9 (mesh) und 10 (ncount) aus dem gespeicherten Buffer extrahiert und als "MESH"/"NCNT" im JSON an die Phone-App gesendet. Kein Bug — sinnvolles Feature fuer BLE-Reconnect. Code in unseren Branch integriert.

### CONFFIN Timing (3 Dateien) ✅
- `esp32_main.cpp`: `connect_pending`-Block entfernt, `commandAction("--conffin")` nach `sendMheard()` eingefuegt
- `nrf52_ble.cpp`: `commandAction("--conffin")` in `connect_callback` auskommentiert
- `nrf52_main.cpp`: `commandAction("--conffin")` nach `sendMheard()` eingefuegt

---

## TEIL 3: Bereits in v3 erledigt

- ExtUDP Null-Pointer-Schutz ✅
- MHeard strcmp→memcmp + memcpy Bounds-Check ✅
- getMheardCount 12h-Fenster ✅
- POSINFO Timestamps ([POSI]→[POSINFO] + getTimeString) ✅

---

## Zusammenfassung: Fortschritt ueber alle Versionen

| Fix | v1 | v2 | v3 | v4 |
|-----|----|----|----|----|
| ExtUDP Null-Pointer | offen | erledigt | ✅ | ✅ |
| strcmp→memcmp | offen | erledigt | ✅ | ✅ |
| memcpy Bounds-Check | offen | erledigt | ✅ | ✅ |
| 12h-Fenster | offen | erledigt | ✅ | ✅ |
| POSINFO Timestamps | offen | erledigt | ✅ | ✅ |
| Audio Log-Tags | offen | offen | offen (1/16) | ✅ |
| CONFFIN Timing | offen | offen | offen | ✅ |
| MHeard stale ncount | offen | offen | offen | **offen** |
| sendMheard MESH/NCNT | offen | offen | offen | ✅ (uebernommen) |
| APRS IGate-Text | offen | offen | offen | **offen** |
| sendPosition Magic-Values | offen | offen | offen | **offen** |
| POSINFO printf-Bug | — | — | offen | **offen** |
| Persistence Size-Check | offen | offen | offen | **offen** |
| CONFFIN addBLEComToOutBuffer | — | — | offen | **offen** |
| connect_pending toter Code | — | — | — | **neu** |

**Stand: 10 von 15 Punkte erledigt, 5 offen (davon 1 rein kosmetisch)**

---

## Nicht im Scope (erwartete Abweichungen)

- **`release.md`**: Unsere Release Notes vs. Kurts Hardware-Liste
- **`src/configuration_global.h`**: VERSION_SUB "n" vs "p", FLASH_VERSION Datum
- **`src/main.cpp`**: Whitespace/Einrueckung
