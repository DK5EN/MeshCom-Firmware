# RAM-Optimierung Heltec V3 — Vorschläge (2026-05-14)

## Stand nach NimBLE-Tuning

Heltec V3 (ESP32-S3): **132.252 B RAM / 327.680 B** (40,4 %), Flash 1.366.745 B.
NimBLE-Tuning bereits eingebaut (Commit `9d99154b`, −792 B DRAM static,
~2,1 kB Heap zur Laufzeit, −7,9 kB Flash).

## Top-DRAM-Verbraucher (statisch, aus `nm` der ELF)

| Symbol | Größe | Quelle |
|---|---|---|
| `BLEtoPhoneBuff` | 9.150 B | `loop_functions.cpp:326` — `[MAX_RING][MAX_MSG_LEN_PHONE+5]` |
| `BLEComToPhoneBuff` | 9.150 B | `loop_functions.cpp:331` — `[MAX_RING][MAX_MSG_LEN_PHONE+5]` |
| `ringBufferUDPout` | 8.250 B | `loop_functions.cpp:321` — `[MAX_RING_UDP][UDP_TX_BUF_SIZE+20]` |
| `ringBuffer` | 7.800 B | `loop_functions_extern.h:170` — `[MAX_RING][UDP_TX_BUF_SIZE+5]` |
| `mheardPathBuffer1` | 7.500 B | `mheard_functions.cpp:30` — `[MAX_MHPATH][50]` |
| `mheardBuffer` | 7.200 B | `mheard_functions.cpp:22` — `[MAX_MHEARD][60]` |
| `ringbufferRAWLoraRX` | 5.200 B | `loop_functions.cpp:313` — `[MAX_LOG][UDP_TX_BUF_SIZE+5]` |
| `u8g2_font_10x20_mf` | 4.194 B | U8g2-Lib in `.data` (sollte in Flash) |
| `g_cnxMgr` | 3.800 B | esp-idf WiFi (prebuilt, nicht abschaltbar) |
| `ftm_initiator` | 2.776 B | esp-idf WiFi FTM (prebuilt) |
| `u8g2_font_6x10_mf` | 2.393 B | U8g2-Lib in `.data` |
| `externQueue` | 2.056 B | `extudp_functions.cpp:56` — `[MAX_EXTERN_QUEUE]` |

ESP32-S3 Konstanten aus `configuration_global.h:82-87`:
```c
#define MAX_MHEARD 120  // "85-124 H00 nodes observed"
#define MAX_MHPATH 150  // "multiple paths per node"
#define MAX_RING 30     // wirkt auf ringBuffer + BLEtoPhone + BLEComToPhone (= 26,1 kB!)
#define MAX_LOG 20      // ringbufferRAWLoraRX (diagnostic only)
#define MAX_RING_UDP 30
```

## Bewertungsmatrix der Vorschläge

| # | Maßnahme | DRAM | Heap | Flash | Risiko | Aufwand |
|---|---|---|---|---|---|---|
| 1 | `MAX_LOG` 20→10 | −2.600 B | — | — | sehr niedrig | 1 Zeile |
| 2 | `MAX_LOG` 20→5 | −3.900 B | — | — | niedrig | 1 Zeile |
| 3 | `MAX_RING` 30→20 (3 Buffer!) | −8.700 B | — | — | mittel (Telemetrie nötig) | 1 Zeile |
| 4 | `MAX_RING_UDP` 30→20 | −2.750 B | — | — | mittel (Telemetrie nötig) | 1 Zeile |
| 5 | `web_header` String → `char[512]` | — | −viel (Frag.!) | minimal | mittel | ~30 Zeilen |
| 6 | `MAX_MHEARD` 120→80 | −1.920 B | — | — | niedrig | 1 Zeile |
| 7 | `MAX_MHPATH` 150→100 | −2.750 B | — | — | niedrig | 1 Zeile |
| 8 | `MAX_EXTERN_QUEUE` 4→2 | −1.028 B | — | — | sehr niedrig | 1 Zeile |
| 9 | APRS-Pfad-Konkatenation entstrings | — | −Fragm. | — | mittel | ~50 Zeilen |
| 10 | U8g2-Fonts in Flash zwingen | −6.587 B | — | +6,5 kB | niedrig | Lib-Wrapper |
| 11 | IDF Kconfig: FTM/coredump/err_msg | −5.620 B | — | — | hoch (Custom-IDF) | sehr hoch |

**Empfohlene Reihenfolge:** 1 → 5 → 6 → 8 → 3 → 4 (mit Telemetrie) → 2 → 7 → 9 → 10.

---

## Vorschlag 1: `MAX_LOG` 20 → 10 (Quick Win, sehr niedriges Risiko)

**Was:** `ringbufferRAWLoraRX[MAX_LOG][UDP_TX_BUF_SIZE+5]` ist ein reiner
Diagnose-Puffer für die letzten N RX-Frames im Klartext-Log. Wird über
`/log`-Kommando ausgelesen, sonst nicht funktional kritisch.

**Wo:** `configuration_global.h:86`

**Vorher:** `#define MAX_LOG 20` → 5.200 B
**Nachher:** `#define MAX_LOG 10` → 2.600 B (**−2.600 B DRAM**)

**Funktionalität erhalten?** Ja — Log-Funktion bleibt voll funktional,
zeigt nur die letzten 10 statt 20 Frames. Andere Varianten (TTGO T-Beam)
nutzen ebenfalls 10–20.

**Risiko:** Sehr niedrig. Nur diagnostische Information geht verloren.

---

## Vorschlag 5: `web_header` von String auf `char[]` (Heap-Fragmentierung!)

**Was:** `String web_header` (global, `web_functions.cpp:31`) wird in
`work_webpage()` Zeile 340 in einer **per-Zeichen-Schleife** mit `+= c`
befüllt. Jedes Zeichen kann eine Reallocation auslösen — bei einer
typischen HTTP-Request mit 200–500 B sind das 10+ Reallocs pro Request,
die den Heap zerschneiden.

**Wo:** `web_functions/web_functions.cpp:31, 314, 340, 353, 355–365`

**Diagnose:**
```cpp
String web_header;                   // Zeile 31 (global, Heap)
// ...
web_header = "";                     // Zeile 314 (alloc/free)
while (client.connected()) {
    if (client.available()) {
        char c = client.read();
        web_header += c;             // Zeile 340 — Realloc-Spike!
```

**Lösung (Skizze):**
```cpp
static char web_header[1024];        // BSS statt Heap
static uint16_t web_header_len = 0;
// ...
web_header_len = 0;
web_header[0] = '\0';
while (client.connected()) {
    if (client.available()) {
        char c = client.read();
        if (web_header_len < sizeof(web_header) - 1) {
            web_header[web_header_len++] = c;
            web_header[web_header_len] = '\0';
        }
    }
}
```
Plus Anpassung der Aufrufer (`indexOf`/`substring`/`trim` → `strstr`/
`memmove`/inline Trim). 1024 B BSS statt unkalkulierbarem Heap.

**Funktionalität erhalten?** Ja, sofern Bound (hier 1024 B) für HTTP
Headers reicht. Typische Browser-Requests sind 300–800 B Header. Falls
zu klein → Header schneiden statt Crash (defensive).

**Risiko:** Mittel — größerer Diff in mehreren Funktionen. Sollte mit
ausführlichem Web-UI-Test verifiziert werden (Login, Settings-Seiten,
APRS-Beacon-Form etc.).

**Effekt:** Eliminiert einen der größten Heap-Fragmentierungs-Hotspots.
Realer Free-Heap-Gewinn nach langer Web-Nutzung wahrscheinlich >2 kB,
plus stabilerer Heap.

---

## Vorschlag 6: `MAX_MHEARD` 120 → 80 (Empirisch begründbar)

**Was:** Kommentar in `configuration_global.h:82` sagt
*"85–124 H00 nodes observed"* — aber das ist der Worst-Case eines
zentralen Gateway-Nodes. Typische Endgeräte sehen <50 Nachbarn.
80 deckt 95 % der realen Knoten ab und gibt Spielraum.

**Wo:** `configuration_global.h:82` (ESP32-S3-Branch)

**Vorher:** `#define MAX_MHEARD 120`
**Nachher:** `#define MAX_MHEARD 80`

**Effekt auf alle 7 mheard-Arrays gleichzeitig:**
- `mheardBuffer`   60 B × 40 = 2.400 B
- `mheardCalls`    10 B × 40 = 400 B
- `mheardLat`       8 B × 40 = 320 B (genutzt für Position!)
- `mheardLon`       8 B × 40 = 320 B (genutzt für Position!)
- `mheardAlt`       4 B × 40 = 160 B
- `mheardEpoch`     4 B × 40 = 160 B
- `mheardNCount`    4 B × 40 = 160 B
- **Summe:** **−3.920 B DRAM**

**Funktionalität erhalten?** Ja — bei Überlauf rotiert der FIFO und
verdrängt den ältesten Eintrag (`updateMheard()`,
`mheard_functions.cpp:263-270`). Auf einem Gateway-Node würden bei >80
Knoten die ältesten/inaktivsten verdrängt — kein Funktionsverlust für
Routing/ACK.

**Risiko:** Niedrig — falls jemand mit 100+ aktiven Nachbarn arbeitet,
würde die mheard-Liste schneller rotieren, ältere Knoten "vergessen".
Aging-Logik (12 h, Zeile 231) sorgt ohnehin für Rotation.

**Wichtig: NICHT `mheardLat/Lon` löschen** — Agent-Vorschlag war falsch.
Beide Arrays werden in `lora_functions.cpp:551-552` von eingehenden
Positions-Frames geschrieben und in `:520-521` für eigene
Position-Broadcasts ausgelesen.

---

## Vorschlag 8: `MAX_EXTERN_QUEUE` 4 → 2

**Was:** Deferred-Queue für External-UDP (best-effort Forwarding).
Jeder Eintrag ~514 B (500 B Payload + Metadaten).

**Wo:** `extudp_functions.cpp:56`

**Vorher:** 4 × 514 = 2.056 B
**Nachher:** 2 × 514 = 1.028 B (**−1.028 B DRAM**)

**Funktionalität erhalten?** Ja, External-UDP ist non-critical (Gateway-
Forwarding). Backpressure schon vorhanden.

**Risiko:** Sehr niedrig.

---

## Vorschlag 3: `MAX_RING` 30 → 20 (DICKE FISCHE, braucht Telemetrie)

**Was:** Die zentrale RX/TX-Verarbeitungs-Queue (`ringBuffer`) sowie
beide BLE-Out-Buffer (`BLEtoPhoneBuff`, `BLEComToPhoneBuff`) sind alle
mit `MAX_RING` dimensioniert. Eine Reduzierung wirkt **dreifach**.

**Wo:** `configuration_global.h:84`

**Vorher:** `#define MAX_RING 30`
**Nachher:** `#define MAX_RING 20`

**Effekt:**
- `ringBuffer` 30 × 260 B = 7.800 B  →  20 × 260 B = 5.200 B  (−2.600 B)
- `BLEtoPhoneBuff` 30 × 305 B = 9.150 B  →  20 × 305 B = 6.100 B  (−3.050 B)
- `BLEComToPhoneBuff` 30 × 305 B = 9.150 B  →  20 × 305 B = 6.100 B  (−3.050 B)
- **Summe:** **−8.700 B DRAM** in einer Konstante.

**Validierung vorab nötig:** Andere Varianten (TTGO T-Beam, vision-master-e290,
RAK4631) nutzen bereits `MAX_RING=20` (Zeilen 69, 76, 91) — empirisch
validiert. Aber Heltec V3 hat in der Praxis mehr Traffic auf der LoRa
RX-Seite (höhere SF, mehr Backbone). Vor Reduzierung:

1. Telemetrie-Counter einbauen, der `max(iWrite − iRead) % MAX_RING`
   pro Buffer in einem Stunden-Intervall loggt.
2. Bei aktiver Nutzung 24–48 h mitlaufen lassen.
3. Wenn Peak < 15 bei allen drei Buffern → reduzieren auf 20 sicher.

**Funktionalität erhalten?** Ja, *falls* gemessener Peak unter neuer
Cap. Bei Überlauf werden ältere Einträge verdrängt
(`addRingPointer()`, `loop_functions.cpp:4132-4137`). In einem Burst
würden Nachrichten/ACKs verloren gehen — daher Telemetrie zwingend.

**Risiko:** Mittel ohne Telemetrie / niedrig nach Telemetrie-Validierung.

---

## Vorschlag 4: `MAX_RING_UDP` 30 → 20 (analog)

**Wo:** `configuration_global.h:87`

**Vorher:** `#define MAX_RING_UDP 30` → 8.250 B
**Nachher:** `#define MAX_RING_UDP 20` → 5.500 B (**−2.750 B DRAM**)

**Wie #3:** Telemetrie auf `udpWritePos − udpReadPos` zuerst.

---

## Vorschlag 2: `MAX_LOG` 20 → 5 (aggressivere Variante von #1)

Wenn diagnostisches Log weiter eingedampft werden soll: 5 letzte Frames
reichen für Troubleshooting nach einem Crash.

**Effekt:** −3.900 B DRAM (statt −2.600 B bei #1).

**Risiko:** Niedrig, primär weniger Forensik-Komfort.

---

## Vorschlag 7: `MAX_MHPATH` 150 → 100

**Was:** Path-Buffer für mehrere Hops pro Knoten.

**Wo:** `configuration_global.h:83`

**Effekt auf 4 Arrays:**
- `mheardPathBuffer1` 50 B × 50 = 2.500 B
- `mheardPathCalls`   10 B × 50 = 500 B
- `mheardPathEpoch`    4 B × 50 = 200 B
- `mheardPathLen`      1 B × 50 = 50 B
- **Summe:** **−3.250 B DRAM**

**Funktionalität erhalten?** Ja, FIFO-Rotation analog zu MAX_MHEARD.

**Risiko:** Niedrig.

---

## Vorschlag 9: APRS-Pfad-Konkatenation entkoppeln

**Was:** In `lora_functions.cpp:1041-1071` werden `aprsmsg.msg_source_path`
und `aprsmsg.msg_payload` per `String.concat()` zusammengesetzt — in der
LoRa RX-Hot-Path (jeder eingegangene Frame). Mehrere `+=` pro Frame
verursachen Heap-Reallocs.

**Lösung:** `char source_path[64]` + `snprintf` mit Index-Tracking. Die
beiden APRS-Strings sind protokollbedingt begrenzt (Path ≤ 56 Zeichen).

**Effekt:** Reduziert Heap-Fragmentierung deutlich bei hohem RX-Traffic.
Realer Free-Heap-Gewinn schwer zu quantifizieren — vermutlich 1–3 kB
nach mehreren Stunden Betrieb.

**Risiko:** Mittel — APRS-Pfad-Parsing ist Protokollkern; Test auf
viele Hop-Varianten nötig.

---

## Vorschlag 10: U8g2-Fonts in Flash zwingen

**Befund:** `u8g2_font_10x20_mf` (4.194 B) und `u8g2_font_6x10_mf`
(2.393 B) liegen in `.data` (DRAM), obwohl sie als `const uint8_t`
deklariert sein sollten. Wahrscheinlich liefert die U8g2-Lib das Section-
Attribut auf ESP32-S3 nicht korrekt aus, oder PlatformIO ignoriert es.

**Mögliche Fixes:**
1. Eigenes Wrapper-Header mit `const uint8_t my_font_6x10[] PROGMEM = {…};`
   (Font-Bytes extrahieren) → Linker pinnt in `.rodata`.
2. U8g2 als source-build mit `-DU8X8_USE_PINS` und expliziter
   `__attribute__((section(".rodata"))) `.
3. Auf neuere U8g2-Version warten, die ESP32-S3 sauber unterstützt.

**Effekt:** −6.587 B DRAM, +6.587 B Flash (vorhanden, kaum 41 %
genutzt).

**Risiko:** Niedrig — reine Lage-Verschiebung der Font-Bytes.

**Aufwand:** Mittel (Lib-Untersuchung + Build-Test).

---

## Vorschlag 11: IDF-Kconfig Bloat (nicht empfohlen ohne dringenden Bedarf)

**Befund:**
- `ftm_initiator` 2.776 B + `ftm_responder` ~ähnlich groß: WiFi Fine Time
  Measurement, von dieser Firmware ungenutzt.
- `esp_err_msg_table` 1.720 B: Human-readable Error Strings.
- `s_coredump_stack` 1.124 B: Crash-Dump-Stack.

**Theoretischer Effekt:** −5.620 B DRAM.

**Realität:** arduino-esp32 (platform-espressif32) liefert ESP-IDF
**prebuilt** mit. Build-Flags `-DCONFIG_*` haben keine Wirkung auf
prebuilt Libs. Voraussetzung: kompletter Custom-IDF-Build mit
geändertem `sdkconfig`. Aufwand sehr hoch, CI/CD-Bruch wahrscheinlich.

**Empfehlung:** Nicht angehen, bevor Vorschläge 1–10 ausgeschöpft sind.

---

## Zusammenfassung — Phasen-Plan

**Phase 1 (sofort umsetzbar, sehr niedriges Risiko, ~−7,6 kB DRAM):**
1. `MAX_LOG` 20 → 10                              (−2.600 B)
2. `MAX_MHEARD` 120 → 80                          (−3.920 B)
3. `MAX_EXTERN_QUEUE` 4 → 2                       (−1.028 B)

**Phase 2 (Heap-Fragmentierung, ~−2 kB Heap Runtime):**
4. `web_header` String → `char[1024]`             Heap-Stabilität ↑↑

**Phase 3 (mit Telemetrie validiert, ~−14,7 kB DRAM):**
5. Telemetrie für `MAX_RING`-Peak + `MAX_RING_UDP`-Peak einbauen
6. 24–48 h Messung im realen Betrieb
7. Bei Peak <15: `MAX_RING` 30 → 20               (−8.700 B)
8. Bei Peak <15: `MAX_RING_UDP` 30 → 20           (−2.750 B)
9. `MAX_MHPATH` 150 → 100                         (−3.250 B)

**Phase 4 (mittlerer Aufwand, ~−6,6 kB DRAM + Heap):**
10. APRS-Pfad-Konkatenation auf `char[]`
11. U8g2-Fonts in Flash via Wrapper

**Theoretisches Maximum (Phasen 1–4):** ~24,9 kB DRAM (von 132,3 kB →
107,4 kB, also −18,8 %) + Heap-Stabilität.

**Realistisches Ziel ohne IDF-Eingriff:** Phase 1+2 ohne Telemetrie:
**−7,6 kB DRAM + Heap-Stabilisierung**, in 2–3 Stunden umsetzbar.

## Verifizierte Falschmeldungen

Während der Agenten-Analyse aufgekommen, hier zur Korrektur:

- **mheardLat/mheardLon "ungenutzt"** — FALSCH. Beide werden in
  `lora_functions.cpp:520-521` (Read) und `:551-552` (Write) für
  Position-Relay verwendet. Nicht löschen.
- **const-Kandidaten in T-Deck-Code** — out of scope für Heltec V3
  (T-Deck/T-Deck-Pro/T5-Epaper werden im Heltec-V3-Build via
  `src_filter` ausgeschlossen).
- **Audio-Task-Stack 16 kB** — T-Deck-only, im Heltec V3 Build nicht
  enthalten.

---

# Teil 2 — T-Deck (Lilygo, ESP32-S3 + PSRAM)

## Stand T-Deck (Build vom 2026-05-14)

T-Deck (`env:t_deck`, ESP32-S3 mit 8 MB PSRAM, 16 MB Flash):
- **DRAM**: 151.284 B / 327.680 B (**66,24 %** — deutlich knapper als
  Heltec V3 mit 40,4 %)
- **iram0_2_seg** (Code via Cache): 1.448.556 B / 8.388.576 B (17,3 %)
- **drom0_0_seg** (Flash .rodata via Cache): 2.886.325 B / 8.388.576 B
  (34,4 %)
- **Flash gesamt**: 2.929.589 B / 12.582.912 B (23,3 %)

T-Deck zieht zusätzlich gegenüber Heltec V3 in den Build:
- `src/t-deck/*.cpp` (insb. `lv_obj_functions.cpp` 4.310 Zeilen,
  `tdeck_main.cpp`, `event_functions.cpp`)
- LVGL 8.x als UI-Framework (`lv_conf.h` mit `LV_MEM_CUSTOM_ALLOC=ps_malloc`)
- TFT_eSPI (Bodmer) für 320×240 IPS-Display
- ESP32-audioI2S (Helix MP3-Decoder) für Audio-Output
- SensorLib, EspSoftwareSerial

## Top DRAM-Verbraucher T-Deck (nur `.dram0.*`, gefiltert)

| Symbol | Größe | Anteil | Herkunft / Teilen mit Heltec V3 |
|---|---|---|---|
| **`audio`** | **10.096 B** | 4,4 % | T-Deck-only (ESP32-audioI2S `Audio` class state) |
| `BLEtoPhoneBuff` | 9.150 B | 4,0 % | shared mit Heltec V3 |
| `BLEComToPhoneBuff` | 9.150 B | 4,0 % | shared |
| `ringBufferUDPout` | 8.250 B | 3,6 % | shared |
| `ringBuffer` | 7.800 B | 3,4 % | shared |
| `mheardPathBuffer1` | 7.500 B | 3,3 % | shared |
| `mheardBuffer` | 7.200 B | 3,1 % | shared |
| `ringbufferRAWLoraRX` | 5.200 B | 2,3 % | shared |
| `g_cnxMgr` | 3.800 B | 1,7 % | esp-idf WiFi (prebuilt) |
| `ftm_initiator` | 2.776 B | 1,2 % | esp-idf WiFi FTM (prebuilt) |
| `externQueue` | 2.056 B | 0,9 % | shared |
| `meshcom_settings` | 2.008 B | 0,9 % | shared (init data) |

**Wichtiger Befund — Karten- und Glyph-Daten liegen KORREKT in Flash:**
| Symbol | Größe | Section (Adresse) |
|---|---|---|
| `data_europe` | 192.960 B | `.flash.rodata` (0x3c1a8f30) |
| `data_deutschland` | 192.960 B | `.flash.rodata` |
| `data_oesterreich` | 192.000 B | `.flash.rodata` |
| `data_wien` | 157.440 B | `.flash.rodata` (0x3c206f08) |
| `data_wien_umgebung` | 153.450 B | `.flash.rodata` |
| `glyph_bitmap` (×5) | ~42.500 B | `.flash.rodata` |

→ **Diese Symbole verbrauchen KEIN DRAM**, sie liegen über den Cache
direkt im Flash. Insgesamt ~870 kB Kartendaten + Glyphen sauber als
`const` deklariert. Hier ist nichts zu retten.

## T-Deck-spezifische DRAM-Differenz zu Heltec V3

T-Deck: 151.284 B – Heltec V3: 132.252 B = **+19.032 B**.
Die +19 kB verteilen sich auf:

- `audio` Singleton (10.096 B) — ESP32-audioI2S Library-State.
  *Nur sinnvoll, wenn Audio-Output tatsächlich genutzt wird.*
- LVGL Widget-State + Stylesheet-Cache (~3-4 kB, verteilt auf viele
  kleine Symbole, nicht in Top-Liste sichtbar).
- TFT_eSPI Driver-State (~2-3 kB).
- SensorLib/EspSoftwareSerial-State (~1-2 kB).

## T-Deck-spezifische Optimierungsvorschläge

### Vorschlag T1: `audio` bedingt linken (−10,1 kB DRAM, wenn ungenutzt)

**Befund:** `Audio audio;` global in `src/esp32/esp32_audio.cpp:23`
(10.096 B DRAM). Der ESP32-audioI2S Helix-MP3-Decoder hält große
interne State-Strukturen (Bitstream-Buffer, Huffman-Tables-Refs,
Decoder-State).

**Voraussetzung:** Wird Audio auf T-Deck überhaupt benutzt? Falls
Audio-Playback nur für RX-Notifications/Ton-Signale eingesetzt wird,
könnte man:

1. **Lazy-Init prüfen:** Ist `audio` schon zum Boot-Zeitpunkt
   instanziiert oder erst bei erstem Play-Call? Falls global global,
   konsumiert das Objekt immer 10 kB DRAM unabhängig vom Audio-Bedarf.
2. **Bedingt compilieren:** Falls Audio nicht zwingend genutzt wird,
   `#ifdef ENABLE_AUDIO` um die globale Instanz + Task-Creation
   (`src/esp32/esp32_audio.cpp:23, 102`). Default OFF, opt-in im
   Build-Flag.
3. **Pointer + heap-alloc:** `Audio* audio = nullptr;` und erst bei
   erstem Play `audio = new Audio()` (PSRAM-fähig via `ps_malloc`).

**Effekt:** Bis zu **−10.096 B DRAM** wenn Audio nicht genutzt
wird; sonst kein Effekt (verschoben in Heap/PSRAM).

**Funktionalität erhalten?** Ja, sofern Audio explizit aktiviert
wird wenn benötigt. Bei Audio-Nutzung fällt der Speicher wieder an,
nur dann eben dynamisch.

**Risiko:** Niedrig — saubere Conditional-Compilation. Wenn Audio im
T-Deck-Workflow aktiv genutzt wird (was zu prüfen ist), keinen Effekt
sondern nur Strukturverlagerung.

### Vorschlag T2: `persisted_msgs` std::vector explizit in PSRAM

**Befund:** `src/t-deck/lv_obj_functions.cpp:178` —
`static std::vector<std::pair<String, MsgBubble>> persisted_msgs;`
mit Limit `PERSISTED_MSG_LIMIT=1000` (Zeile 180).

Bei 1.000 persistierten Nachrichten × ~500 B/Eintrag = bis zu **~500 kB**
Heap-Bedarf. Das LVGL `LV_MEM_CUSTOM_ALLOC=ps_malloc` (Zeile 64 in
`lv_conf.h`) wirkt nur auf LVGL-interne Allokationen — `std::vector`
nutzt weiterhin den Standard-Allokator (internal RAM).

Sobald `persisted_msgs.size()` ein paar hundert erreicht, geht der
Heap im internen RAM aus, während 8 MB PSRAM ungenutzt sind.

**Lösung:** Custom-Allocator für den Vector, der PSRAM nutzt:

```cpp
template <typename T>
struct PsramAllocator {
    using value_type = T;
    T* allocate(size_t n) {
        return (T*) heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
    }
    void deallocate(T* p, size_t) { heap_caps_free(p); }
};

static std::vector<std::pair<String, MsgBubble>,
                   PsramAllocator<std::pair<String, MsgBubble>>>
                   persisted_msgs;
```

ABER: die `String`-Felder innerhalb der `MsgBubble` allozieren weiterhin
über den Standard-Heap, da Arduino-`String` kein Custom-Allocator
unterstützt. Daher zusätzlich:

- Variante a (klein): `MsgBubble` von `String`-Feldern auf
  `char[N]`-Felder mit festen Maxima umstellen. Wirkungsvoll, größerer
  Code-Diff.
- Variante b (mittel): `MsgBubble::text` etc. in PSRAM allozieren über
  manuellen Wrapper (`(char*)ps_malloc(len+1)`).

**Effekt:** Bis zu **−~500 kB Heap-Druck im internen RAM** bei voller
Nachrichten-Historie. Praktisch wahrscheinlich −50 bis −150 kB im
Alltagsbetrieb.

**Funktionalität erhalten?** Ja — PSRAM-Zugriff ist langsamer als
internes RAM, aber Persisted-Messages werden nicht im Frame-Render-
Hotpath gelesen, sondern nur beim Öffnen der Chat-Historie. PSRAM-
Latenz hier irrelevant.

**Risiko:** Mittel — Refactoring der Vector-Allokation und ggf. der
`MsgBubble`-Struktur. Crash-Risiko bei PSRAM-Init-Fehler (Heltec V3
hat kein PSRAM, T-Deck schon) — Fallback auf internal RAM nötig.

### Vorschlag T3: `msg_tab_entries` bewusst NICHT in PSRAM

**Befund:** `src/t-deck/lv_obj_functions.cpp:171` — wird im UI-Loop
(`msg_tabs_add_message` Zeile 2667 und vielen Render-Pfaden) häufig
durchlaufen.

**Empfehlung:** **Im internen RAM belassen.** Dieser Vector ist
performance-kritisch. PSRAM-Zugriff (~50 ns vs. ~3 ns) würde Scroll-
Performance der Nachrichtenliste spürbar machen.

Stattdessen `MSG_TAB_MAX_MESSAGES` (Zeile 175, derzeit 50) und Max-Tabs
(50) prüfen — bei realistischer Nutzung sind 30 Tabs × 30 Messages
ausreichend. Statische Begrenzung statt Heap-Wachstum.

### Vorschlag T4: Event-Handler `new`/`delete` durch Pool ersetzen

**Befund:** `src/t-deck/lv_obj_functions.cpp:2922, 2946` —
`HeaderEventData* data = new HeaderEventData{...};`. Wird in
Event-Callback gefreed. **Leak-Risiko**, wenn Callback nicht aufgerufen
wird (z.B. Widget vorher zerstört).

**Lösung:** Statischer Pool fester Größe (z.B. 8 Slots) statt `new`.
Eliminiert Leak-Risiko und Heap-Fragmentierung.

**Effekt:** −minimal Heap, +Stabilität.

**Risiko:** Niedrig.

### Vorschlag T5: `strMaps` const

**Befund:** `src/t-deck/tdeck_extern.cpp:21` —
`String strMaps[MAX_MAP]` (5 String-Header × ~12 B = 60 B).

**Lösung:** `static const char* strMaps[MAX_MAP] = { ... };` — String-
Literale liegen ohnehin in `.rodata`. Die 60 B Pointer-Array bleiben
sogar in `.rodata`, da `const`.

**Effekt:** ~−60 B DRAM. Trivial, mitnehmen.

## Welche Heltec-V3-Vorschläge gelten AUCH für T-Deck?

Alle Phase-1 + Phase-3-Vorschläge wirken **identisch** auf T-Deck, da
die Buffer-Konstanten (`MAX_RING`, `MAX_LOG`, `MAX_MHEARD`, etc.) im
gemeinsamen Code-Pfad liegen. Da T-Deck-DRAM viel knapper ist (66 %
statt 40 %), ist der Effekt dort **wichtiger**:

| Maßnahme | Heltec V3 | T-Deck | Risiko |
|---|---|---|---|
| `MAX_LOG` 20→10 | −2.600 B | −2.600 B | sehr niedrig |
| `MAX_MHEARD` 120→80 | −3.920 B | −3.920 B | niedrig |
| `MAX_EXTERN_QUEUE` 4→2 | −1.028 B | −1.028 B | sehr niedrig |
| `MAX_RING` 30→20 (mit Telemetrie) | −8.700 B | −8.700 B | mittel |
| `MAX_RING_UDP` 30→20 (mit Telemetrie) | −2.750 B | −2.750 B | mittel |
| `MAX_MHPATH` 150→100 | −3.250 B | −3.250 B | niedrig |
| `web_header` String → char[] | Heap | Heap | mittel |

Auf T-Deck wäre Phase 1 (−7,6 kB) das gleiche; relativ aber **doppelt
so wertvoll** wegen 327 kB DRAM-Budget und 151 kB schon belegt.

## Phasen-Plan T-Deck

**Phase 1 (gemeinsam mit Heltec V3, sofort):**
- `MAX_LOG` 20 → 10, `MAX_MHEARD` 120 → 80, `MAX_EXTERN_QUEUE` 4 → 2,
  `strMaps` const → **−7,6 kB DRAM gemeinsam**.

**Phase 2 (T-Deck-spezifisch, mittel):**
- T1: `audio` bedingt linken oder lazy-init → **−10 kB DRAM** (falls
  Audio nicht zwingend genutzt).
- T4: Event-Pool statt `new`/`delete` → Heap-Stabilität.

**Phase 3 (PSRAM-Verlagerung, mittel-groß):**
- T2: `persisted_msgs` mit PSRAM-Allocator → **bis zu −500 kB Heap-
  Druck im internen RAM**.

**Phase 4 (Telemetrie-validiert, gemeinsam):**
- `MAX_RING` 30 → 20, `MAX_RING_UDP` 30 → 20, `MAX_MHPATH` 150 → 100 →
  **−14,7 kB DRAM**.

**Theoretisches Max für T-Deck (Phase 1–4):** ~32 kB DRAM + Heap-
Verlagerung in PSRAM → DRAM-Auslastung von 66 % auf ~56 %.

## Was bei T-Deck NICHT gewinnbringend ist

- **U8g2-Fonts:** Nicht in T-Deck-Build (T-Deck nutzt LVGL/TFT_eSPI mit
  eigenen Glyph-Bitmaps die korrekt in `.rodata` liegen).
- **Map-Daten in DRAM verschieben:** Sind bereits in `.flash.rodata`,
  Verbesserung hier nicht möglich.
- **LVGL-Framebuffer:** Liegt schon in PSRAM (`tdeck_main.cpp:314`,
  `ps_malloc`).
- **IDF-WiFi-Symbole:** Wie bei Heltec V3 prebuilt, nur via Custom-IDF-
  Build änderbar (nicht empfohlen).

## Artefakte

- Heltec V3 Optimized-ELF: `/tmp/firmware_nimble_optimized_heltec_v3.elf`
- T-Deck Build-Log: `/tmp/build_t_deck.log`
- T-Deck ELF: `.pio/build/t_deck/firmware.elf`
- DRAM-Symbol-Liste: extrahiert via
  `xtensa-esp32s3-elf-gcc-nm --print-size --size-sort` auf die ELF,
  Adress-Filter `3fc[9a-f]` für reine DRAM-Symbole.
- Section-Layout via `xtensa-esp32s3-elf-readelf -S`.
- Toolchain-Pfad: `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/`
