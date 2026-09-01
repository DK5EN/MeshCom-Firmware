# MeshCom Firmware Code Audit

**Date:** 2026-06-26
**Branch:** v4.35p_prio
**HEAD:** b67e6d32
**Upstream base:** d3af8986 (upstream/dev — after rebase 2026-06-26)
**Scope:** changed files vs upstream/dev (2 files: `src/configuration_global.h`, `src/extudp_functions.cpp`)
**Rules:** docs/codequality-rules.md
**Previous audit:** docs/code-audit-20260531.md (2026-05-31, 83 findings carried from 2026-05-25)
**Auditor:** Claude Code (automated, two-phase scan + delta)

---

## Delta vs. 2026-05-31

The upstream commits integrated in this window (871da1ad → d3af8986, 8 functional commits
via PRs #1009/#1010/#1013/#1014/#1016/#1017/#1018/#1021) affect Heltec V3 init, T-DECK
SETUP, via/routing, and a new Heltec E213 board. None of these touch our two changed files.

Our changed files carry two PR-scoped patches:
- `fix: stack overflow in sendExtern() — platform-conditional buffer allocation`
- `fix(sendExtern): platform-conditional buffer allocation` (same logical patch)
- New: `queueExtern()` + `flushExternQueue()` deferred-send ring buffer

**`src/configuration_global.h`**: only `FLASH_VERSION` updated (20260608 → 20260626). No
code quality impact.

| Status | Count | Notes |
|--------|-------|-------|
| New findings (our code) | 2 | BND-02 silent truncation; RACE-01 externQueue sync |
| Resolved | 0 | no fixes in this window |
| Existing (inherited, pre-upstream) | 5 | MEM-03 String globals in extudp_functions.cpp |
| Full-src carry-forward | 83 | unchanged from 2026-05-25 (not re-enumerated here) |

---

## Audit Summary

| Category | Rule IDs | Critical | High | Medium | Low |
|----------|----------|----------|------|--------|-----|
| Buffer Safety | BND-01..05 | 0 | 0 | 1 | 0 |
| Memory Safety | MEM-01..05 | 0 | 0 | 5 | 0 |
| Thread Safety | RACE-01..08 | 0 | 0 | 1 | 0 |

**Delta total: 7 findings in changed files (0 CRITICAL, 0 HIGH, 7 MEDIUM, 0 LOW)**
(2 newly introduced by our code, 5 inherited pre-existing from upstream)

---

## Findings

### src/extudp_functions.cpp

---

#### MEDIUM BND-02 — memcpy without reject on oversized input [NEW — our code]

**Line 509–510:**
```cpp
if(buflen > 500) buflen = 500;
memcpy(entry->buffer, buffer, buflen);
```

**Context:** `queueExtern()` — new function introduced by our sendExtern deferral patch.
Copies incoming LoRa packet into ring buffer slot before handing off to main loop.

**Violation:** BND-02: "All `memcpy()` calls: validate length BEFORE copying, assert
`len <= buffer_size`." The current code clamps silently instead of rejecting. If a caller
passes `buflen > 500`, the data is truncated without any log or error signal, and the
truncated (corrupt) packet is forwarded via UDP.

**Assessment:** Low real-world risk — the LoRa protocol caps packet length well below 500 B.
But the silent truncation pattern violates the rule and hides over-length packets. A packet
larger than 500 B would be forwarded with the tail cut off, no warning emitted.

**Fix:**
```cpp
if(buflen > sizeof(entry->buffer)) {
    Serial.printf("[EXT] queueExtern: buflen %u > %u, dropped\n", buflen, sizeof(entry->buffer));
    return;
}
memcpy(entry->buffer, buffer, buflen);
```

---

#### MEDIUM RACE-01 — externQueue used-flag set without memory barrier [NEW — our code]

**Lines 506–530:** `queueExtern()` + `flushExternQueue()`

```cpp
// producer (queueExtern — called from radio callback / OnRxDone):
memcpy(entry->buffer, buffer, buflen);
entry->buflen = buflen;
// ... more field writes ...
entry->used = true;          // ← store-store ordering not guaranteed

// consumer (flushExternQueue — called from main loop):
if(externQueue[i].used)      // ← can see used=true before buffer contents
    sendExtern(...);
entry->used = false;
```

**Context:** Classic SPSC (single-producer, single-consumer) ring buffer pattern. On nRF52
(single-core, cooperative scheduling) this is safe. On ESP32 dual-core (Xtensa LX6/LX7
with out-of-order store buffers), the CPU can make `entry->used = true` visible on the
other core before the preceding `memcpy` / field writes are visible. This allows the
consumer to read partially-written buffer contents.

**Violation:** RACE-01: "NEVER access shared data from two tasks without synchronization."
The `used` bool is the only synchronization primitive, and it is not `std::atomic<bool>`.

**Assessment:** Latent bug, low probability of manifesting on ESP32-S3 in practice due to
write-combining behavior, but non-zero on dual-core builds. Not a problem on nRF52.

**Fix — minimal:** Mark `used` as `std::atomic<bool>` and use `store(true,
memory_order_release)` / `load(memory_order_acquire)`:
```cpp
struct externQueueEntry {
    uint8_t  buffer[500];
    uint16_t buflen;
    int16_t  rssi;
    int8_t   snr;
    char     src_type[8];
    std::atomic<bool> used{false};
};
// producer:
entry->used.store(true, std::memory_order_release);
// consumer:
if(externQueue[i].used.load(std::memory_order_acquire))
```

---

#### MEDIUM MEM-03 (×5) — Arduino String globals and locals [EXISTING — pre-upstream code]

Pre-existing upstream code, not introduced by our patches. Flagged because the file
differs from upstream/dev. Documented for completeness; not a regression.

| Line | Code | Classification |
|------|------|----------------|
| 25 | `String s_extern_node_ip = ""` | global String, initialisation path only; low heap churn risk |
| 27 | `String strExtOutput` | global accumulator used in `strEsc()` — not re-entrant |
| 28 | `String str_ip` | global, used in `startExternUDP()` for DNS fallback |
| 457 | `String strKurz = c_json` | local in `sendExtern()`, debug-log path only; created, substring-sliced, then destroyed |
| 484 | `String strKurz = c_tjson` | same pattern, telemetry log path |

**Rule:** MEM-03: "String handling: fixed `char[]` arrays — NEVER Arduino `String` in hot
paths."

**Assessment:** Lines 25/27/28 are cold-path (init, error escape). Lines 457/484 are inside
`sendExtern()` which runs on each received packet — moderate hot path. All cause small heap
allocations on every call; on nRF52 with constrained heap this contributes to fragmentation.
Line 27 (`strExtOutput`) is additionally non-re-entrant: `strEsc()` writes to it as a
global accumulator, then returns it — unsafe if ever called from two contexts.

**Fix for hot-path (lines 457/484):** Replace with `strnlen` + direct pointer arithmetic,
or a fixed `char[10]` preview buffer for the log print. Lines 25/27/28: convert to
`char[]` with `snprintf` for init assignment.

---

## Full-src carry-forward

The 83 findings from docs/code-audit-20260525.md remain open and unchanged. Line numbers
were not re-verified in this pass (scope is PR delta only). No upstream commit in this
window touched any of the previously catalogued sites.

Priority ranking unchanged from 2026-05-31:
1. **nrf52/at_cmd.h:30 `sprintf`** — CRITICAL, trivial one-line fix
2. **WiFi.softAP without password** — CRITICAL, 3 sites
3. **net_console.cpp:174 plaintext password bypass** — HIGH/SECURITY
4. **millis() wraparound** — CRITICAL, ~33 sites
5. **APRS FCS before parsing** — CRITICAL
6. **web_header unbounded concat** — CRITICAL

---

## Entscheidungsprotokoll — Review 2026-06-26

Vollständige Entscheidungen zu allen 83 Findings aus docs/code-audit-20260525.md.
Review durchgeführt im Interview-Format. Legende:

- **FIX** — Unbedingter Fix, wird implementiert
- **EXCEPTION** — Bewusste Ausnahme mit dokumentiertem Grund, kein Fix erforderlich
- **DE-PRIO** — De-priorisiert, kein akuter Handlungsbedarf
- **DUPLIKAT** — Bereits unter anderem Finding erfasst, Verweis auf primäres Finding

Fixes mit ★ haben erhöhte Priorität (vom Reviewer explizit hervorgehoben).

### 1. Memory Safety (MEM-01..05)

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 1 | LOW | nrf52/nrf52_main.cpp:83 | `malloc()` in Diagnosefunktion | EXCEPTION | Nur für Heap-Messung, kein Produktionspfad |
| 2 | MEDIUM | loop_functions.cpp:176,186 | `String strSOFTSER_BUF`, `String strTelemetry` global | DE-PRIO | Schleichende Degradation, kein sofortiger Absturz |
| 3 | MEDIUM | loop_functions.cpp | Temporäre String-Concatenation pro Paket | DE-PRIO | Upstream-Code, Restrisiko gering |
| 4 | MEDIUM | aprs_functions.cpp:202..316 | Per-Paket `.concat()` auf String-Objekten | DE-PRIO | Teilweise bereits upstream verbessert |
| 5 | LOW | spectral_scan.cpp:108 | `new uint16_t[]` mit korrektem `delete[]` | EXCEPTION | Korrekt gepaart, kein Leak |
| 6 | LOW | t-deck-pro/tdeck_pro.cpp:195,204,213 | `ps_calloc`/`malloc` Display-Buffer ohne Caller-Propagation | EXCEPTION | Hardware nicht im Einsatz (T-Deck-Pro) |
| 7 | LOW | t-deck/tdeck_main.cpp:314 | Gleiches Muster wie Nr. 6 | EXCEPTION | Hardware nicht im Einsatz |
| 8 | LOW | t5-epaper/t5epaper_main.cpp:258-280 | Gleiches Muster + `(lv_color_t *)` Cast | EXCEPTION | Hardware nicht im Einsatz |

### 2. Buffer Overflow Prevention (BND-01..05)

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 9 | CRITICAL | nrf52/at_cmd.h:30 | `AT_PRINTF`: `sprintf` statt `snprintf` | **FIX** | Trivialster Fix im Katalog, eine Zeile |
| 10 | MEDIUM | t-deck-pro/ui_deckpro.cpp:1864,2117,2122,2519 | 4x `sprintf()` | DE-PRIO | Hardware nicht im Einsatz (T-Deck-Pro) |
| 11 | HIGH | Displays/BaseDisplay/SD.cpp:368 | `strcpy` 3-Arg, kompiliert nicht | EXCEPTION | Toter Code, nie in `build_src_filter` |
| 12 | MEDIUM | nrf52/nrf52_ble.cpp:293 | `memcpy` ohne Längenvalidierung | EXCEPTION | BLE offen per Amateurfunk-Entscheidung |
| 13 | HIGH | esp32/esp32_main.cpp:310 | `memcpy` ohne Längenvalidierung | DE-PRIO | Kein realer Overflow-Pfad im Normalbetrieb |
| 14 | HIGH | lora_functions.cpp:211,385 | `memcpy` LoRa-Empfangspfad | EXCEPTION | LoRa protokollseitig auf 256 B begrenzt |
| 15 | HIGH | phone_commands.cpp:541 | `memcpy` App-Kommando-Pfad | DE-PRIO | Konsistent mit De-Prio Eingabevalidierung |
| 16 | HIGH | nrf52/nrf_eth.cpp:270 | `memcpy` Ethernet-Pfad nRF52 | DE-PRIO | Seltene Konfiguration |
| 17 | MEDIUM | udp_functions.cpp:140,172 | `memcpy` UDP-Empfangspfad | DE-PRIO | Vertrauenswürdiges Netzwerkumfeld |
| 18 | MEDIUM | nrf52/nrf52_flash.cpp:70-226 | `memcpy` Settings-Read/Write | DE-PRIO | Nur bei korruptem Flash relevant |

### 3. Input Validation

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 19 | CRITICAL | phone_commands.cpp:226,324,364,385,403,466,479-488,555 | BLE-Kommandos ohne Validierung | DE-PRIO | BLE offen per Entscheidung; Risiko: nur fehlerhafte App |
| 20 | HIGH | web_functions/web_setup.cpp:69-259 | Web-Setup ohne Eingabevalidierung | DE-PRIO | Web offen per Amateurfunk-Exception; Operator-Selbstverschulden |
| 21 | HIGH | web_functions/web_functions.cpp:1920 | Gleiche Kategorie wie Nr. 20 | DE-PRIO | Konsistent mit Nr. 20 |
| 22 | MEDIUM | web_functions/web_functions.cpp:353-382,1767,1827,1856,1889 | Gleiches Muster | DE-PRIO | Konsistent mit Nr. 20 |
| 23 | MEDIUM | gps_functions.cpp | GPS-Parsing ohne Wertebereichsvalidierung | DE-PRIO | Konsistent mit Entscheidung ISR-Race-Fix |

### 4. Thread Safety (RACE-01..08)

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 24 | MEDIUM | esp32/esp32_audio.cpp:38 | Binärer Semaphor als Mutex | EXCEPTION | T-Deck-Pro Audio, Hardware nicht im Einsatz |
| 25 | MEDIUM | t-deck/tdeck_main.cpp:111 | Binärer Semaphor als Mutex | EXCEPTION | Hardware nicht primäres Target |
| 26 | LOW | nrf52/nrf52_main.cpp:400 | Binärer Semaphor (nRF52) | EXCEPTION | Auf nRF52 acceptable, single-core |
| 27 | MEDIUM | t-deck/tdeck_main.cpp:405 | `portMAX_DELAY` Display-Flush | DE-PRIO | T-Deck nicht primäres Target |
| 28 | MEDIUM | nrf52/api_functions.cpp:262 | `portMAX_DELAY` | DE-PRIO | Niedriges Risiko im Normalbetrieb |
| 29 | MEDIUM | time_functions.cpp:96 | `localtime()` nicht reentrant | DE-PRIO | Sporadisch, schwer reproduzierbar |
| 30 | HIGH ★ | loop_functions.cpp:299-300 | Volatile Ring-Buffer Race iWrite/iRead | **FIX (HOHE PRIO)** | Dual-Core ESP32 Memory-Ordering ohne Garantie |
| 31 | HIGH | nrf52/nrf52_main.cpp:233-236 | CAD-Flags ISR vs. Main-Loop Race | **FIX** | ISR-zu-Main-Loop-Sync fehlt auf nRF52 |
| 32 | HIGH | gps_functions.cpp:174-175 | `pulseTimes` ISR Race | **FIX** | Torn Read bei GPS-Timing möglich |
| 33 | MEDIUM | esp32/esp32_main.cpp:443,458 | `scanFlag`/`transmissionState` ohne volatile/atomic | **FIX** | Compiler-Optimierung kann Wert in Register einfrieren |
| 34 | MEDIUM | loop_functions.cpp:75,204-206 | Cross-module volatile bools | DE-PRIO | Breite Streuung, sporadisch, niedrige Severity |
| 35 | MEDIUM | t-deck-pro/peri_gps.cpp:78 | Ungepinnter Task mit `double` | EXCEPTION | Hardware nicht im Einsatz |
| 36 | MEDIUM | t-deck-pro/peri_gyroscope.cpp:14-16 | Ungepinnter Task mit `float` globals | EXCEPTION | Hardware nicht im Einsatz |

### 5. Interrupt Safety (ISR-01..04)

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 37 | MEDIUM | nrf52/nrf52_main.cpp:317,326,335 | `IRAM_ATTR` fehlt (nRF52) | EXCEPTION | Technisch nicht erforderlich auf nRF52 |
| 38 | MEDIUM | gps_functions.cpp:≈206 | ISR Off-by-One auf `pulseTimes[]` | **FIX** | Schreibt past-end, Speicherkorruption möglich |
| 39 | CRITICAL | t5-epaper/io_extend.c:26 | `printf()` in ISR | EXCEPTION | Hardware nicht im Einsatz (T5-ePaper) |
| 40 | HIGH ★ | lora_functions.cpp:325,327,344,346,350,375 | `Serial.printf()` in OnRxDone ISR | **FIX (HOHE PRIO)** | Blockiert ISR, kann WDT-Reset auslösen |
| 41 | HIGH | lora_functions.cpp:332 | `startRadioReceive()` (SPI) aus ISR | EXCEPTION | Bewusstes Design; bPendingRadioRx-Pattern nur nRF52 |

### 6. SPI Bus Safety

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 42 | HIGH | lora_functions.cpp:332 | SPI-Call aus ISR | DUPLIKAT → Nr. 41 | Identische Fundstelle, gleiche Entscheidung |
| 43 | MEDIUM | (systemweit) | Kein explizites Core-Pinning für LoRa/SPI | DE-PRIO | Kein bestätigter Fehler |

### 7. Authentication & Security

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 44 | CRITICAL | udp_functions.cpp:534 | `WiFi.softAP()` ohne Passwort | EXCEPTION | Amateurfunk-Regulierung verbietet Verschlüsselung |
| 45 | CRITICAL | safeboot/main.cpp:68,187 | `WiFi.softAP()` ohne Passwort (2 Stellen) | EXCEPTION | Identische Begründung wie Nr. 44 |
| 46 | HIGH | nrf52/nrf52_ble.cpp:274 | `SECMODE_OPEN` — BLE ohne Auth | EXCEPTION | Amateurfunk-Regulierung + Samsung BLE-Kompatibilität; bewusstes Design |
| 47 | HIGH | web_functions/web_functions.cpp:234 | Leeres Passwort = offener Zugang | EXCEPTION | Amateurfunk-Regulierung; Operator-Verantwortung |
| 48 | HIGH | web_functions/web_functions.cpp:353-368 | URL-Parameter-Auth statt HTTP Basic | EXCEPTION | Amateurfunk-Regulierung; Operator-Verantwortung |
| 49 | HIGH | esp32/esp32_main.cpp:271 | BLE PIN `000000` hardcoded | EXCEPTION | Samsung BLE-Kompatibilität; bewusstes Design |
| 50 | MEDIUM | safeboot/ElegantOTA.cpp:21-48 | OTA ohne Auth, keine Firmware-Validierung | EXCEPTION | Amateurfunk-Regulierung |
| 51 | HIGH | net_console.cpp:174 | HMAC Plaintext-Bypass (`memcmp`-Shortcut) | **FIX** | Macht gesamte HMAC-Implementierung wirkungslos |
| 52 | HIGH | net_console.cpp:171 | Passwort im Klartext im Serial-Log | **FIX** | Mit `***` maskieren; Debug-Überbleibsel |

### 8. Error Handling

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 53 | HIGH | main.cpp:38,43 | `SPI.begin()` unchecked | EXCEPTION | Kein Rückgabewert definiert in Arduino-API |
| 54 | HIGH | esp32/esp32_main.cpp:651 | `Wire.begin()` unchecked | EXCEPTION | Konsistent mit Nr. 53 |
| 55 | HIGH | nrf52/nrf52_main.cpp:850 | `Wire.begin()` unchecked (nRF52) | EXCEPTION | Konsistent mit Nr. 53 |
| 56 | HIGH | nrf52/nrf52_ble.cpp:128,130,135,272,278 | 5x BLE `begin()` unchecked | EXCEPTION | Konsistent mit Nr. 53 |
| 57 | MEDIUM | extudp_functions.cpp:69 | `ETH.begin()` unchecked | EXCEPTION | Konsistent mit Nr. 53 |
| 58 | CRITICAL | esp32/esp32_main.cpp:1317-1478 | 8x `while(true);` bei Radio-Fehler | **FIX** | Node nach Init-Fehler dauerhaft tot; → `esp_restart()` |
| 59 | CRITICAL | t-deck-pro/peri_lora.cpp | 11x `while(true);` | **FIX** | Identisches Problem; trotz T-Deck-Pro: geteilter Code-Pfad |
| 60 | MEDIUM | t-deck-pro/tdeck_pro.cpp | `while(1)` Camera-Init | DE-PRIO | Hardware nicht im Einsatz |
| 61 | MEDIUM | t-deck-pro/peri_gps.cpp:54 | `while(1)` GPS-Init | DE-PRIO | Hardware nicht im Einsatz |
| 62 | MEDIUM | t5-epaper/t5epaper_main.cpp:73 | `while(1)` Display-Init | DE-PRIO | Hardware nicht im Einsatz |
| 63 | MEDIUM | t5-epaper/peri_gps.cpp:53 | `while(1)` GPS-Init | DE-PRIO | Hardware nicht im Einsatz |
| 64 | MEDIUM | t5-epaper/peri_lora.cpp:36 | `while(1)` LoRa-Init | DE-PRIO | Hardware nicht im Einsatz |
| 65 | MEDIUM | loop_functions.cpp:≈444-481 | Ringbuffer Overflow ohne Log | **FIX** | Stiller Datenverlust; Logging hilft Diagnose |

### 9. Watchdog & Recovery (STAB-01..05)

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 66 | CRITICAL | esp32/esp32_main.cpp | Kein `esp_task_wdt_add/reset()` für Main-Loop | **FIX** | Kompletter Loop-Hänger undetektiert |
| 67 | HIGH | Regexp.cpp:145,150 | Rekursive Expand ohne Yield | DE-PRIO | Nur bei pathologischen Inputs; kein Normalbetrieb |
| 68 | CRITICAL ★★ | (projektübergreifend, ~33 Stellen) | `millis()` Wraparound-Pattern | **FIX (HÖCHSTE PRIO)** | **Bestätigter Feldfehler** — Node nach 49 Tagen eingefroren |
| 69 | MEDIUM | (allgemein) | Kein `esp_reset_reason()` | DE-PRIO | Reine Debug-Infrastruktur |
| 70 | MEDIUM | (allgemein) | Kein persistenter Absturz-Zähler | DE-PRIO | Wünschenswert, kein akutes Problem |

### 10. Compiler & Build Safety (COMP-01..05)

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 71 | CRITICAL | platformio.ini:152,179,219 | `-Werror` fehlt | EXCEPTION | Würde bei aktuell ignoriertem Warning-Bestand Build brechen |
| 72 | HIGH | platformio.ini:55-69 | Libraries mit `^`-Constraint | EXCEPTION | Kontrollierbares Risiko; `pio update` wird bewusst gesteuert |
| 73 | HIGH | platformio.ini:115 | `espressif32@^6.13.0` floating | EXCEPTION | Konsistent mit Nr. 72 |
| 74 | HIGH | variants/t_deck_pro/platformio.ini:75 | RadioLib Version-Skew | EXCEPTION | T-Deck-Pro, nicht unser Target |
| 75 | HIGH | variants/t_deck_pro/platformio.ini:2 | espressif32-Versionskonflikt | EXCEPTION | T-Deck-Pro, nicht unser Target |

### 11. Type Safety

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 76 | HIGH | nrf52/nrf_eth.cpp:274 | `uint16_t`→`uint8_t` Narrowing | EXCEPTION | Ethernet-Modul seltene Konfiguration |
| 77 | MEDIUM | (multi files) | `snprintf` Return nie geprüft | DE-PRIO | ~600 Stellen; nur Protokollgrenzen relevant |

### 12. Lifetime Safety

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 78 | MEDIUM | web_functions/web_functions.cpp:208,485,503 | `web_header` nach `stop()` nicht resettet | EXCEPTION | Web offen per Amateurfunk-Exception |
| 79 | MEDIUM | web_functions/web_functions.cpp:236-263 | Session-Cleanup auf kaputtem `millis()`-Pattern | **FIX** | Wird durch millis()-Gesamtfix (Nr. 68) automatisch behoben |

### 13. Logging Safety

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 80 | HIGH | lora_functions.cpp:325,327,344,346,350,375 | `Serial.printf()` in ISR | DUPLIKAT → Nr. 40 | Identische Fundstelle |
| 81 | MEDIUM | t5-epaper/io_extend.c:26 | `printf()` in ISR | DUPLIKAT → Nr. 39 | Identische Fundstelle |
| 82 | HIGH | net_console.cpp:171 | Passwort im Serial-Log | DUPLIKAT → Nr. 52 | Identische Fundstelle |

### 14. Design Patterns

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 83 | CRITICAL | lora_functions.cpp:1916-1952 | CSMA mutiert globalen State | DE-PRIO | Größerer Refactoring-Aufwand; kein bestätigter Fehler |
| 84 | HIGH ★ | esp32/esp32_main.cpp:1325,1365,1393 | `setDio1Action()` Return unchecked | **FIX (HOHE PRIO)** | Stiller Empfangsausfall; schwer zu diagnostizieren |
| 85 | HIGH | via_functions.cpp:41-50 | `checkMesh()` — VIA-Routing nur Stub | EXCEPTION | Upstream-Feature in Entwicklung; bekannter Zustand |

### 15. Protocol Correctness

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 86 | CRITICAL | aprs_functions.cpp:179-432 | FCS-Check nach Parsing (muss davor) | **FIX** | Korrupte Pakete werden vollständig verarbeitet |
| 87 | HIGH | aprs_functions.cpp:134 | Nur Min-Frame-Size-Check, kein Max | **FIX** | Überlange Frames nicht abgewiesen |
| 88 | MEDIUM | aprs_functions.cpp:120 | `aprsmsg`-Objekt wiederverwendet | EXCEPTION | Kein paralleles Parsing; Single-Threaded-Pfad |

### 16. State Machine & Session Safety

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 89 | MEDIUM | web_functions/web_functions.cpp:38-39 | Session-Tabelle ohne Mutex | EXCEPTION | Web offen; gleichzeitige Zugriffe unwahrscheinlich |
| 90 | MEDIUM | web_functions/web_functions.cpp | Kein Rate-Limiting pro IP | EXCEPTION | Amateurfunk-Kontext; bekannte Operators |

### 17. Data Drift Safety

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 91 | CRITICAL | t5-epaper/nvs_param.cpp:7-47 | NVS ohne Schema-Versionsfeld | DE-PRIO | Nur T5-ePaper; Hardware nicht im Einsatz |

### 18. TCP/Web/SSE Safety

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 92 | CRITICAL | web_functions/web_functions.cpp:31,340 | `web_header` unbegrenzt + `indexOf()` unchecked | EXCEPTION | Amateurfunk-Kontext; kein externer Angreifer |
| 93 | CRITICAL | web_functions/web_functions.cpp:353,355,382,1767,1827,1856,1889 | `indexOf()` Return unchecked | DUPLIKAT → Nr. 92 | Gleiche Entscheidung |
| 94 | HIGH | web_functions/web_functions.cpp:265 | 10-Slot Session-Tabelle ohne LRU | DE-PRIO | Sessions laufen per Timeout ab; wenige Operators |
| 95 | MEDIUM | web_functions/web_functions.cpp | Kein Content-Length-Limit | DE-PRIO | Amateurfunk-Kontext; kein Angriffsszenario |

### 19. Test & Fuzz Readiness

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 96 | MEDIUM | test/ | Nur compress_functions getestet | EXCEPTION | Vollständige Test-Coverage außerhalb PR-Scope |

### 20. Stack Safety (STK-01..04)

| Nr | Severity | Datei / Stelle | Finding | Entscheidung | Begründung |
|----|----------|---------------|---------|--------------|------------|
| 97 | CRITICAL | (Projekt) | Kein `sdkconfig` mit Stack-Overflow-Detection Lvl 2 | DE-PRIO | Reine Debug-Infrastruktur; kein Laufzeitrisiko |
| 98 | HIGH | (Codebase) | Kein `uxTaskGetStackHighWaterMark()` | DE-PRIO | Wünschenswert; kein akutes Problem |
| 99 | MEDIUM | platformio.ini | `-fstack-usage` fehlt | DE-PRIO | Build-Infrastruktur; konsistent mit Nr. 97 |

---

## Zusammenfassung Entscheidungsprotokoll

| Entscheidung | Anzahl | Anteil |
|---|---|---|
| **FIX** (unbedingter Fix) | 17 | 20 % |
| **EXCEPTION** (begründete Ausnahme) | 34 | 40 % |
| **DE-PRIO** (de-priorisiert) | 21 | 25 % |
| **DUPLIKAT** (Verweis auf primäres Finding) | 10 | 12 % |
| **Gesamt** | **82** | |

### Fix-Backlog nach Priorität

| Priorität | Nr | Finding | Dateien |
|-----------|-----|---------|---------|
| ★★ HÖCHSTE | 68 | millis() Wraparound ~33 Stellen | mheard_functions.cpp, lora_functions.cpp, loop_functions.cpp, web_functions.cpp u.a. |
| ★ HOCH | 30 | LoRa Ringbuffer volatile Race | loop_functions.cpp |
| ★ HOCH | 40 | Serial.printf() in OnRxDone ISR | lora_functions.cpp |
| ★ HOCH | 84 | setDio1Action() Return unchecked | esp32/esp32_main.cpp |
| — | 9 | AT_PRINTF sprintf→snprintf | nrf52/at_cmd.h |
| — | 51 | net_console HMAC Plaintext-Bypass | net_console.cpp |
| — | 52 | Passwort im Serial-Log maskieren | net_console.cpp |
| — | 58 | while(true) Radio-Init ESP32 | esp32/esp32_main.cpp |
| — | 59 | while(true) Radio-Init T-Deck-Pro | t-deck-pro/peri_lora.cpp |
| — | 65 | Ringbuffer Overflow ohne Log | loop_functions.cpp |
| — | 66 | Main-Loop Task-Watchdog | esp32/esp32_main.cpp |
| — | 31 | CAD-Flags ISR-Race nRF52 | nrf52/nrf52_main.cpp |
| — | 32 | pulseTimes ISR-Race | gps_functions.cpp |
| — | 33 | scanFlag/transmissionState | esp32/esp32_main.cpp |
| — | 38 | GPS ISR Off-by-One | gps_functions.cpp |
| — | 79 | Session-Cleanup millis() | web_functions.cpp (→ via Fix Nr. 68) |
| — | 86 | APRS FCS vor Parsing | aprs_functions.cpp |
| — | 87 | APRS Max-Frame-Size-Check | aprs_functions.cpp |

Exceptions mit wiederkehrendem Grund:
- **Amateurfunk-Regulierung** (kein Verschlüsselungsgebot): Nr. 44, 45, 47, 48, 50, 92, 93, 95
- **Hardware nicht im Einsatz** (T-Deck-Pro, T5-ePaper, Ethernet): Nr. 6, 7, 8, 10, 24, 35, 36, 39, 60-64, 74, 75, 91, 97
- **Samsung BLE-Kompatibilität** (bewusstes Design): Nr. 46, 49
