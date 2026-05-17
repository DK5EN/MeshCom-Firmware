# RAM-/Flash-Stand nach Upstream-Dev-Rebase 2026-05-17

## Fragestellung

Nach dem Wechsel des Upstream-Targets von `oe1kbc_v4.35p` auf `upstream/dev`
(HEAD 9c9e1908) wurden 11 neue Upstream-Commits integriert (TLS-Console,
"iram reduction", lazy ssl_context allocation, DISABLE_TLS_CONSOLE-Guard).

1. Wie wirken die neuen Upstream-Aenderungen auf RAM/Flash, insb. auf
   Heltec V3 (Referenzbasis aus dem 2026-05-14-Report)?
2. Wo stehen die anderen 6 Standardtargets aktuell, und wo sind die
   kritischen Engstellen?
3. Hat Upstream unser NimBLE-Tuning aus 32b84891 uebernommen?

## Methodik

- Branch: `v4.35p_prio` rebased auf `upstream/dev` HEAD `9c9e1908`
- Tag der Messung: `e9edf0df` (`docs: upstream sync 2026-05-17 + switch to dev branch`)
- Build: PlatformIO, identische Toolchain wie 2026-05-14-Report
- Region-Werte aus der Linker-Memory-Region-Tabelle (post-link Output)
- Alle 7 Standardtargets gebaut, alle SUCCESS

## Frage 3 vorab — Upstream hat unser NimBLE-Tuning uebernommen

Upstream-Commit `67311a4c` ("v4.35p tlsconsole + iram reduction") enthaelt
**dieselben NimBLE-Flags** wie unser eigener Commit `32b84891`:

```
-DCONFIG_BT_NIMBLE_MAX_BONDS=1            (war 4)
-DCONFIG_BT_NIMBLE_MAX_CCCDS=2            (war 12)
-DCONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED  (neu)
-DCONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED (neu)
-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=3072
-DCONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4
```

Konsequenz: Unser `32b84891` ist nach dem Rebase faktisch eine No-Op
(blieb nur wegen Whitespace-Unterschieden formal als Commit erhalten).
Die statische DRAM-Einsparung von -792 B aus unserem PR ist jetzt
Upstream-Baseline auf allen ESP32-Targets.

## Frage 1 — Heltec V3 Delta gegen 2026-05-14 Optimized

Referenzpunkt: `docs/ram-comparison-20260514.md`, Spalte "Optimized" (das war
unser 32b84891 vor dem Upstream-Dev-Rebase).

| Region          | Optimized 05-14 | Aktuell 05-17 | Diff       | Anmerkung |
|-----------------|-----------------|---------------|------------|-----------|
| iram0_0_seg     | 85.076 B        | 85.076 B      | 0          | unveraendert |
| iram0_2_seg     | 994.456 B       | 1.086.360 B   | **+91.904**| TLS-Code (XIP) |
| dram0_0_seg     | 200.960 B       | 182.112 B     | **-18.848**| Ringbuffer-Verkleinerung |
| drom0_0_seg     | 1.326.829 B     | 1.426.245 B   | **+99.416**| TLS-Rodata |
| **RAM**         | 132.252 B       | 113.408 B     | **-18.844**| **-14,2 %** |
| **Flash**       | 1.366.745 B     | 1.492.589 B   | **+125.844**| **+9,2 %** |

### Was tatsaechlich die DRAM-Einsparung bringt

Der Commit-Titel `iram reduction` ist irrefuehrend — gespart wird **DRAM**,
nicht IRAM. Die wirksame Aenderung steht in `src/configuration_global.h`
fuer ESP32-S3 und nRF52840:

```diff
- #define MAX_MHEARD 120   (Mheard-Ringbuffer)
+ #define MAX_MHEARD 80
- #define MAX_MHPATH 150   (Mheard-Pfade)
+ #define MAX_MHPATH 100
- #define MAX_RING 30      (Message-Ringbuffer)
+ #define MAX_RING 20
- #define MAX_LOG 20       (LOG-Ringbuffer)
+ #define MAX_LOG 10
- #define MAX_RING_UDP 30  (UDP-TX-Ringbuffer)
+ #define MAX_RING_UDP 20
```

Diese Ringbuffer-Verkleinerungen wirken auf alle ESP32-S3-Targets
(Heltec V3, T-Beam Supreme, T-Deck, T-Deck Plus) und RAK4631.
ESP32-Classic (E22, T-Beam) sind nicht betroffen — die haben bereits einen
eigenen, kleineren Konfig-Branch.

### Lazy ssl_context allocation (e6ce9f20)

Der Commit von Ralf Altenbrand (DH1FR) verschiebt ~36 KB mbedtls-I/O-Puffer
vom Startup-Init in die erste TLS-Verbindung. Effekt ist **ausschliesslich
Runtime-Heap**, nicht in Linker-Regions sichtbar. Erwartete Heap-Differenz
nach Boot auf Heltec V3: ~36 KB mehr frei, solange kein TLS-Client
verbunden ist.

## Frage 2 — Gesamtstand alle 7 Targets

### ESP32 Classic (E22-DevKitC, ttgo_tbeam)

| Region       | E22-DevKitC | %      | ttgo_tbeam  | %      | Limit       |
|--------------|-------------|--------|-------------|--------|-------------|
| iram0_0_seg  | 126.092 B   | 96,20% | 131.044 B   | **99,98%** | 131.072 B |
| iram0_2_seg  | 1.168.659 B | 34,97% | 1.202.187 B | 35,97% | 3.342.304 B |
| dram0_0_seg  | 123.452 B   | **99,09%** | 123.276 B | **98,95%** | 124.580 B |
| drom0_0_seg  | 311.395 B   | 7,42%  | 321.215 B   | 7,66%  | 4.194.272 B |
| **RAM (BSS+Data)** | 123.448 B | 23,2% | 123.272 B  | 9,4%   | 532k / 1.31M |
| **Flash**    | 1.614.061 B | 47,4%  | 1.662.273 B | 48,8%  | 3.403.776 B |

**Kritisch:**
- `ttgo_tbeam` `iram0_0_seg` bei **99,98 %** — nur **28 Byte frei**.
  Jede zusaetzliche `IRAM_ATTR`-Funktion oder ISR riskiert Link-Fehler.
- `E22-DevKitC` `dram0_0_seg` bei **99,09 %** — nur **1.128 Byte frei**.
  Der April-Sync-Defekt (40 B Overflow auf v4.35p) ist durch
  `DISABLE_TLS_CONSOLE` (Commit `0972ec73`) gerade so behoben — der Puffer
  ist minimal.

### ESP32-S3 mit 4 MB Flash (Heltec V3, ttgo_tbeam_supreme)

| Region       | Heltec V3   | %      | T-Beam Supreme | %      | Limit       |
|--------------|-------------|--------|----------------|--------|-------------|
| iram0_0_seg  | 85.076 B    | 23,49% | 87.620 B       | 24,19% | 362.240 B   |
| iram0_2_seg  | 1.086.360 B | 12,95% | 1.109.400 B    | 13,23% | 8.388.576 B |
| dram0_0_seg  | 182.112 B   | 52,66% | 185.024 B      | 53,50% | 345.856 B   |
| drom0_0_seg  | 1.426.245 B | 4,25%  | 1.437.177 B    | 4,28%  | 33.554.400 B|
| **RAM**      | 113.408 B   | 34,6%  | 113.776 B      | 34,7%  | 327.680 B   |
| **Flash**    | 1.492.589 B | 43,9%  | 1.529.473 B    | 44,9%  | 3.403.776 B |

Komfortabler Headroom auf beiden Targets. ~213 kB DRAM frei,
~1,9 MB Flash frei.

### ESP32-S3 mit 16 MB Flash (T-Deck, T-Deck Plus)

| Region       | t_deck      | %      | t_deck_plus | %      | Limit       |
|--------------|-------------|--------|-------------|--------|-------------|
| iram0_0_seg  | 94.172 B    | 26,00% | 94.172 B    | 26,00% | 362.240 B   |
| iram0_2_seg  | 1.459.484 B | 17,40% | 1.459.484 B | 17,40% | 8.388.576 B |
| dram0_0_seg  | 209.648 B   | 60,62% | 209.648 B   | 60,62% | 345.856 B   |
| drom0_0_seg  | 2.889.545 B | 34,45% | 2.889.609 B | 34,45% | 8.388.576 B |
| **RAM**      | 131.852 B   | 40,2%  | 131.852 B   | 40,2%  | 327.680 B   |
| **Flash**    | 2.943.737 B | 23,4%  | 2.943.801 B | 23,4%  | 12.582.912 B|

T-Deck und T-Deck Plus sind binaer praktisch identisch (Diff drom0_0_seg
64 B = lvgl/display variant). Hoechste DRAM-Auslastung aller Targets
(60,6 %) — durch zusaetzliche Display- und LVGL-Buffer.

### nRF52840 (wiscore_rak4631)

| Region | Used      | Limit     | %      |
|--------|-----------|-----------|--------|
| Code (.text) | 566.832 B | 815.104 B | 69,5% |
| RAM (.bss+.data) | 80.048 B | 248.832 B | 32,2% |
| Region "RAM" (Linker) | 235.520 B (230 KB) | 237.568 B (232 KB) | **99,14%** |

Der nRF52840-Linker meldet die RAM-Region bei **99,14 %** — das schliesst
aber den fuer Heap reservierten Bereich mit ein, der Run-Time vergeben
wird. Statisch belegt sind nur 80 kB (BSS+Data), Heap-Headroom ist
reichlich vorhanden. Flash-Headroom: 248 KB frei.

## Gesamtuebersicht (Snapshot 2026-05-17, HEAD e9edf0df)

| Target              | RAM stat.     | Flash         | Kritisch?              |
|---------------------|---------------|---------------|------------------------|
| E22-DevKitC         | 123.448 B     | 1.614.061 B   | **DRAM 99 %**          |
| heltec_wifi_lora_32_V3 | 113.408 B  | 1.492.589 B   | OK                     |
| ttgo_tbeam          | 123.272 B     | 1.662.273 B   | **IRAM 99.98 %**       |
| ttgo_tbeam_supreme  | 113.776 B     | 1.529.473 B   | OK                     |
| t_deck              | 131.852 B     | 2.943.737 B   | OK                     |
| t_deck_plus         | 131.852 B     | 2.943.801 B   | OK                     |
| wiscore_rak4631     | 80.048 B      | 566.832 B     | OK (Heap-RAM oben)     |

## Fazit

1. **Heltec V3 hat -18,8 kB DRAM gewonnen** (132,2 -> 113,4 kB) durch
   die Ringbuffer-Verkleinerungen aus `67311a4c`. Das ist die mit
   Abstand groesste RAM-Einsparung seit Wochen.
2. **Flash wuchs um +125,8 kB** auf Heltec V3 — bezahlt vom TLS-Console
   Feature (Code in iram0_2_seg + Rodata in drom0_0_seg). Headroom bleibt
   komfortabel (1,9 MB frei auf 4 MB Flash-Targets).
3. **Unser NimBLE-Tuning wurde upstream gemerged** — gut so. Unser
   Commit `32b84891` ist nun No-Op und kann beim naechsten geeigneten
   Anlass aus dem PR-Branch entfernt werden (`git rebase -i upstream/dev`,
   diesen Commit "drop"-en).
4. **ttgo_tbeam ist kritisch knapp am IRAM-Limit** (99,98 %, 28 Byte).
   Jede neue `IRAM_ATTR`-Funktion bricht den Build.
5. **E22-DevKitC ist kritisch knapp am DRAM-Limit** (99,09 %, 1.128 Byte).
   Der April-Overflow ist nur durch `DISABLE_TLS_CONSOLE` behoben.
6. **Lazy ssl_context allocation** ist eine Runtime-Optimierung,
   wirkt nicht im Linker-Output, aber gibt ~36 kB Heap zurueck bis zur
   ersten TLS-Verbindung — relevant fuer Low-RAM ESP32-Classic Nodes.

## Empfehlungen

- **Kein neuer IRAM_ATTR-Code** auf ttgo_tbeam ohne vorherige Pruefung.
- **DRAM-Beobachtung E22**: jede zusaetzliche statische Allokation
  (Buffer, struct in BSS) gefaehrdet den Build. Bei naechstem grossem
  Refactor evtl. weitere Ringbuffer-Reduktion fuer ESP32-Classic
  durchziehen, analog 67311a4c fuer ESP32-S3.
- **Heap-Verifikation lazy_ssl** zur Laufzeit: `esp_get_free_heap_size()`
  vor/nach erstem TLS-Connect loggen, um den behaupteten ~36 kB Gewinn
  zu bestaetigen.

## Artefakte

- Build-Logs (relink-only):
  `/tmp/sz_e22.txt`, `/tmp/sz_tbeam.txt`, `/tmp/sz_supreme.txt`,
  `/tmp/sz_tdeck.txt`, `/tmp/sz_tdeckp.txt`, `/tmp/sz_rak.txt`
- Heltec V3 Linker-Output direkt im Conversation-Output
- Firmware-Binaries: `~/Desktop/MeshCom-Firmware/firmware_*.bin/.hex`
- Vergleichsbasis: `docs/ram-comparison-20260514.md`
