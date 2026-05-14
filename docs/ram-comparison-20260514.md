# RAM-Vergleich Heltec V3 — vor/nach Upstream-Sync 2026-05-14

## Fragestellung

Die Firmware-Maintainer behaupten, in den jüngsten Upstream-Commits den
RAM-Verbrauch reduziert zu haben — unter anderem durch Änderungen an NimBLE.
Diese Messung prüft den Anspruch durch direkten Build-Vergleich.

## Methodik

- Board: Heltec V3 (`heltec_wifi_lora_32_V3`, ESP32-S3)
- Toolchain: PlatformIO, identische Build-Umgebung, identische Optionen
- Baseline-Commit: `b63f6fb3` (docs: upstream sync 2026-04-19) — lokaler
  v4.35p_prio vor Rebase
- Rebased-Commit: `911914fb` (selber Inhalt, rebased auf
  `upstream/oe1kbc_v4.35p` HEAD `2083fdbd`)
- Lokale Änderungen während Vergleichsbuild gestasht, sodass nur der
  Upstream-Diff in die Messung eingeht.

Zwei neue Upstream-Commits seit Baseline:
- `01d55b71` v4.35p code review (2026-05-09)
- `2083fdbd` v4.35p checkmesh (2026-05-12)

## Messwerte (PlatformIO Memory Regions)

| Region          | Region Size | Baseline (b63f6fb3) | Rebased (911914fb) | Diff      |
|-----------------|-------------|---------------------|--------------------|-----------|
| iram0_0_seg     | 362.240 B   | 85.076 B            | 85.076 B           | 0         |
| iram0_2_seg     | 8.388.576 B | 997.992 B           | 999.092 B          | +1.100    |
| **dram0_0_seg** | 345.856 B   | **201.736 B**       | **201.752 B**      | **+16**   |
| drom0_0_seg     | 33.554.400 B| 1.329.949 B         | 1.330.085 B        | +136      |
| rtc_iram_seg    | 8.176 B     | 33 B                | 33 B               | 0         |
| rtc_data_seg    | 8.176 B     | 44 B                | 44 B               | 0         |
| rtc_slow_seg    | 8 KB        | 16 B                | 16 B               | 0         |

| Aggregate | Baseline      | Rebased       | Diff   |
|-----------|---------------|---------------|--------|
| **RAM**   | 133.028 B (40,6 %) | 133.044 B (40,6 %) | **+16 B**   |
| **Flash** | 1.373.385 B (40,3 %) | 1.374.637 B (40,4 %) | **+1.252 B** |

## Ergebnis

**Die Behauptung trifft nicht zu.** Für Heltec V3 ist der RAM-Verbrauch
durch die letzten zwei Upstream-Commits *nicht* gesunken, sondern minimal
*gestiegen* (+16 Byte DRAM).

## Analyse — welche Dateien wurden geändert?

Geänderte Dateien in den zwei Upstream-Commits, die NimBLE/BLE betreffen
könnten:

- `src/nrf52/nrf52_ble.cpp` — nRF52 BLE, **nicht** NimBLE auf ESP32.
  Änderung: `sprintf` → `snprintf` (Buffer-Overflow-Härtung), keine
  Speicheroptimierung.
- `src/esp32/esp32_main.cpp` — drei Stellen wo `sprintf` → `snprintf`
  konvertiert wurde (cBLEName, cManufData). Reine Härtung, kein
  Speichereffekt.

**Keine NimBLE-Konfiguration angefasst:**
- Keine Änderungen an `sdkconfig` / `platformio.ini` BLE-Einstellungen
- `CONFIG_BT_NIMBLE_MAX_CONN`, `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT`,
  `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` etc. unverändert
- Keine `nimble_port_stop()` / `nimble_port_deinit()` Calls hinzugefügt

Tatsächlicher Inhalt der zwei Upstream-Commits:
- `01d55b71` (v4.35p code review): Code-Härtung über viele Module
  (`sprintf` → `snprintf`, kleinere Refactorings in aprs_functions,
  display, t-deck, esp32_main, nrf52_*)
- `2083fdbd` (v4.35p checkmesh): Neues Feature *checkmesh* —
  command_functions, via_functions, esp32_flash neue Funktionalität

Die +16 Byte DRAM und +1.252 Byte Flash sind durch das neue
checkmesh-Feature erklärbar (zusätzliche Funktionen, kein
Speicher-Tradeoff).

## Fazit

Die jüngsten Upstream-Aktivitäten waren Code-Härtung und Feature-Arbeit,
keine RAM-Optimierung. Wenn NimBLE-Speicher gespart werden soll, müssten
die Stellschrauben aus `NimBLE.md` (sdkconfig / menuconfig) selbst
gesetzt werden — Upstream tut dies bislang nicht.

## Artefakte

- Build-Logs: `/tmp/build_baseline_heltec_v3.log`,
  `/tmp/build_rebased_heltec_v3.log`
- ELF-Dateien: `/tmp/firmware_baseline_heltec_v3.elf`,
  `/tmp/firmware_rebased_heltec_v3.elf`
