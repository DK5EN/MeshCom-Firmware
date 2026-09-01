# RAM-Vergleich Heltec V3 — Upstream-Sync + NimBLE-Tuning 2026-05-14

## Fragestellung

1. Haben die Upstream-Maintainer im jüngsten Sync tatsächlich RAM gespart
   (Behauptung: u.a. durch NimBLE-Änderungen)?
2. Wieviel zusätzlich kann durch gezielte NimBLE-Konfiguration gespart
   werden, wenn man nur als BLE-Server (kein Client/Scan) operiert?

## Methodik

- Board: Heltec V3 (`heltec_wifi_lora_32_V3`, ESP32-S3)
- Toolchain: PlatformIO, identische Build-Umgebung
- Drei Builds:
  - **Baseline** (`b63f6fb3`): v4.35p_prio vor Upstream-Sync 2026-05-14
  - **Rebased** (`911914fb`): selber Inhalt, rebased auf
    `upstream/oe1kbc_v4.35p` HEAD `2083fdbd` (zwei neue Upstream-Commits
    `01d55b71` code review + `2083fdbd` checkmesh)
  - **Optimized**: Rebased + NimBLE Build-Flags (siehe unten)

## Frage 1 — Upstream-Sync allein

| Region          | Region Size | Baseline    | Rebased     | Diff      |
|-----------------|-------------|-------------|-------------|-----------|
| iram0_0_seg     | 362.240 B   | 85.076 B    | 85.076 B    | 0         |
| iram0_2_seg     | 8.388.576 B | 997.992 B   | 999.092 B   | +1.100    |
| dram0_0_seg     | 345.856 B   | 201.736 B   | 201.752 B   | +16       |
| drom0_0_seg     | 33.554.400 B| 1.329.949 B | 1.330.085 B | +136      |
| **RAM**         | 327.680 B   | 133.028 B   | 133.044 B   | **+16**   |
| **Flash**       | 3.403.776 B | 1.373.385 B | 1.374.637 B | **+1.252**|

**Ergebnis:** Die Behauptung trifft nicht zu. Upstream hat NimBLE nicht
angefasst (keine `BT_NIMBLE_*` Konfigänderung, nur `sprintf`→`snprintf`
Härtung in `nrf52_ble.cpp` und `esp32_main.cpp`). RAM ist minimal
gestiegen, durch das neue `checkmesh`-Feature in `2083fdbd`.

## Frage 2 — Aktives NimBLE-Tuning

### Eingriff (platformio.ini, `[esp32]` build_flags)

Geändert:
```
-DCONFIG_BT_NIMBLE_MAX_BONDS=4    →  =1
-DCONFIG_BT_NIMBLE_MAX_CCCDS=12   →  =2
```

Neu hinzu:
```
-DCONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED
-DCONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED
-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=3072
-DCONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4
```

### Begründung pro Flag

| Flag | Code-Belegt | Warum sicher |
|---|---|---|
| `ROLE_CENTRAL_DISABLED` | Keine `NimBLEClient`/`connect()`-Calls im Source | Wir sind kein BLE-Client |
| `ROLE_OBSERVER_DISABLED` | Keine `NimBLEScan`-Calls im Source | Wir scannen nichts |
| `MAX_BONDS=1` | `setSecurityAuth(false,false,false)` – kein Pairing aktiv | Kein Bonding genutzt |
| `MAX_CCCDS=2` | 2 NOTIFY-Chars × 1 Connection = 2 CCCD-Einträge | Exakter Bedarf |
| `HOST_TASK_STACK_SIZE=3072` | Callbacks rufen nur `xQueueSend` + `Serial.printf` | Reichlich Spielraum |
| `MSYS1_BLOCK_COUNT=4` | NUS-Pakete typ. <100 B, MTU ≤255, 1 Conn | 4 × ~92 B = 368 B Pufferplatz |

`CONFIG_BT_NIMBLE_CRYPTO_STACK_MBEDTLS=1` wurde getestet, ist aber für
arduino-esp32 nicht nutzbar — der Linker meldet:

```
undefined reference to `mbedtls_cipher_cmac_starts`
```

Die vorgebaute mbedtls-Lib in arduino-esp32 ist ohne `MBEDTLS_CMAC_C`
kompiliert. Daher Flag wieder entfernt; tinycrypt bleibt aktiv. Würde
einen mbedtls-Rebuild erfordern (~8 kB potenzieller Flash-Gewinn).

### Messwerte (Rebased vs. Optimized)

| Region          | Rebased     | Optimized   | Diff      | %      |
|-----------------|-------------|-------------|-----------|--------|
| iram0_0_seg     | 85.076 B    | 85.076 B    | 0         | 0,00 % |
| iram0_2_seg     | 999.092 B   | 994.456 B   | **−4.636**| −0,46 %|
| dram0_0_seg     | 201.752 B   | 200.960 B   | **−792**  | −0,39 %|
| drom0_0_seg     | 1.330.085 B | 1.326.829 B | **−3.256**| −0,24 %|
| **RAM**         | 133.044 B   | 132.252 B   | **−792**  | −0,60 %|
| **Flash**       | 1.374.637 B | 1.366.745 B | **−7.892**| −0,57 %|

### Realer Runtime-Effekt > Linker-Diff

Der Linker zeigt nur statisch allozierten Speicher. Drei weitere
Einsparungen wirken erst zur Laufzeit (nach `NimBLEDevice::init()`):

| Quelle | Geschätzt | Wo gemessen |
|---|---|---|
| Static DRAM (s.o.) | −792 B | `dram0_0_seg` |
| NimBLE Host Task Stack 4096→3072 | −1.024 B | Heap (xTaskCreate) |
| MSYS-Pool 12→4 Blöcke | −~736 B | Heap (`os_mempool`) |
| MAX_BONDS 4→1 Persistenz | −~300 B | Heap (NVS-Cache) |
| MAX_CCCDS 12→2 | −~80 B  | Heap |
| **Erwartete Heap-Free-Differenz** | **~2,9 kB** | `esp_get_free_heap_size()` nach BLE-Init |

Pro-Tipp aus `docs/NimBLE.md` umsetzen: vor und nach
`NimBLEDevice::init()` einmal Heap loggen, um den realen Effekt zu
verifizieren.

## Vergleich Baseline → Optimized (Gesamtgewinn)

| Metrik          | Baseline    | Optimized   | Diff       |
|-----------------|-------------|-------------|------------|
| **RAM**         | 133.028 B   | 132.252 B   | **−776 B** |
| **Flash**       | 1.373.385 B | 1.366.745 B | **−6.640 B**|

## Fazit

- **Upstream-Anspruch nicht belegt** für Heltec V3 — RAM unverändert
  bzw. minimal gestiegen.
- **NimBLE-Tuning bringt messbare Einsparung**: statisch −792 B DRAM,
  runtime erwartet ~2,9 kB freier Heap, dazu −7,9 kB Flash.
- Größter Hebel war `ROLE_CENTRAL_DISABLED` + `ROLE_OBSERVER_DISABLED`
  (~33 kB Flash-Code laut Library-Doku, davon kommt der Großteil der
  −7,9 kB; der Rest sind Buffer-Caps).
- Weiteres Potenzial nur über NimBLE-Library-Fork:
  `BT_NIMBLE_SM_LEGACY/SM_SC` abschalten (~3 kB Flash),
  `ACL_BUF_COUNT 12→6` (~1 kB DRAM),
  `HCI_EVT_HI_BUF_COUNT 30→8` (~1 kB DRAM). Aktuell nicht empfohlen.

## Test-Plan vor Akzeptanz

1. App-Verbindung: NUS-Pairing ohne PIN → funktioniert
2. Nachricht senden + Empfang über NOTIFY → funktioniert
3. Reconnect nach Disconnect (`advertiseOnDisconnect(true)`) →
   funktioniert
4. Optional: `esp_get_free_heap_size()` vor/nach `NimBLEDevice::init()`
   loggen → Differenz ~2,9 kB kleiner als vor dem Tuning

## Artefakte

- Build-Logs: `/tmp/build_baseline_heltec_v3.log`,
  `/tmp/build_rebased_heltec_v3.log`,
  `/tmp/build_nimble_optimized_heltec_v3.log`
- ELF-Dateien: `/tmp/firmware_baseline_heltec_v3.elf`,
  `/tmp/firmware_rebased_heltec_v3.elf`,
  `/tmp/firmware_nimble_optimized_heltec_v3.elf`
- Test-Firmware: `~/Desktop/heltec_v3_nimble_optimized_<YYYYMMDD_HHMM>.bin`
