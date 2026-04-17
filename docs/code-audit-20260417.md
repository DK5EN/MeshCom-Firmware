# MeshCom Firmware Code Audit

**Date:** 2026-04-17
**Branch:** v4.35p_prio (rebased onto upstream `oe1kbc_v4.35p` HEAD = 95bd4c4)
**Auditor:** Claude Code (automated)
**Rules:** docs/codequality-rules.md (ESP32 C++ Code Quality Rules)
**Previous audit:** docs/code-audit-20260414.md (2026-04-14, 69 findings)

**Delta vs. 2026-04-14:** Re-validated every finding against the current tree after
the upstream rebase (17 new commits: gps_functions refactor/SoftSerial support,
nRF52 RX-Boost, T-Echo SPI → SPIM3, T-Beam 1W RX switch fix, E290 GPS pins,
download_meshcom.py). Line numbers were updated where they shifted; unchanged
findings are still present; new findings introduced by upstream are marked **NEW**.

---

## Audit Summary

| Category | Rule IDs | Status | Critical | High | Medium | Low |
|----------|----------|--------|----------|------|--------|-----|
| Memory Safety | MEM-01..05 | FAIL | 2 | 0 | 3 | 2 |
| Buffer Safety | BND-01..05 | FAIL | 4 | 1 | 1 | 0 |
| Input Validation | Section 3 | FAIL | 1 | 2 | 1 | 0 |
| Thread Safety | RACE-01..08 | FAIL | 0 | 3 | 3 | 1 |
| ISR Safety | ISR-01..04 | FAIL | 1 | 1 | 2 | 0 |
| SPI Bus | SPI-01..05 | FAIL | 0 | 1 | 1 | 0 |
| Auth & Security | Section 7 | FAIL | 3 | 3 | 1 | 0 |
| Error Handling | Section 8 | FAIL | 1 | 2 | 1 | 0 |
| Watchdog | STAB-01..05 | FAIL | 2 | 1 | 2 | 0 |
| Compiler/Build | COMP-01..05 | FAIL | 1 | 0 | 0 | 0 |
| Type Safety | Section 11 | FAIL | 0 | 1 | 1 | 0 |
| Lifetime Safety | Section 12 | PARTIAL | 0 | 0 | 2 | 0 |
| Logging Safety | Section 13 | PARTIAL | 0 | 1 | 1 | 0 |
| Design Patterns | Section 14 | FAIL | 1 | 1 | 1 | 0 |
| Protocol Correctness | Section 15 | FAIL | 1 | 1 | 1 | 0 |
| State Machines | Section 16 | PARTIAL | 0 | 0 | 2 | 0 |
| Data Drift | Section 17 | FAIL | 1 | 0 | 0 | 0 |
| TCP/Web/SSE | Section 18 | FAIL | 2 | 1 | 1 | 0 |
| Test Readiness | Section 19 | FAIL | 0 | 0 | 1 | 0 |
| Stack Safety | STK-01..04 | FAIL | 1 | 1 | 1 | 0 |

**Total: 21 Critical, 20 High, 26 Medium, 3 Low = 70 findings**
(+1 vs. 2026-04-14 — NEW medium finding in `gps_functions.cpp` ISR under
`#else` branch, see ISR-02 below.)

---

## 1. Memory Safety (MEM-01..05)

### MEM-01: malloc/new after initialization

| Severity | File | Line | Finding |
|----------|------|------|---------|
| LOW | nrf52/nrf52_main.cpp | 83 | `malloc()` in `nrf52_getMaxFreeBlock()` heap probe (runtime, not setup) — line shifted from 82 |

### MEM-02: Arduino String in hot paths

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | loop_functions.cpp | 2205 | `String charBuffer_aprs(...)` returns `String` by value; caller in `lora_functions.cpp:452` invokes it in OnRxDone path — heap churn on every RX packet |
| CRITICAL | lora_functions.cpp | 452 | `memcpy(ringbufferRAWLoraRX[...], charBuffer_aprs(...).c_str(), UDP_TX_BUF_SIZE-1)` — String temp allocated inside OnRxDone execution path |
| MEDIUM | loop_functions.cpp | 176, 186 | Global `String strSOFTSER_BUF`, `String strTelemetry` used in main loop |
| MEDIUM | loop_functions.cpp | 1532, 1550, 1563, 1585 | Multiple temporary `String` concatenations in per-message processing |
| MEDIUM | aprs_functions.cpp | 192, 204, 209, 270, 280, 285, 344 | `aprsmsg.msg_source_path.concat(...)` (and friends) — per-byte String growth during packet decode |

### MEM-03: C++ new without delete (memory leak)

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | spectral_scan.cpp | 108 | `new uint16_t[RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE]` — returned pointer never freed, leak per scan |
| CRITICAL | spectral_scan.cpp | 188 | `new uint16_t[...]{0}` — same pattern, dead code path but dangerous |

### MEM-04: Display buffer allocation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | t-deck-pro/tdeck_pro.cpp | 195-198 | Triple `ps_calloc` for display buffers, no unified error recovery |
| LOW | t-deck/tdeck_main.cpp | 318 | `ps_malloc` for LVGL buffer, no fallback on failure |
| LOW | t5-epaper/t5epaper_main.cpp | 258-279 | Triple `ps_malloc`/`ps_calloc` for display buffers |

### MEM-05: Buffer size constants — PASS

All major buffer sizes centralized in `configuration_global.h`. Ring buffers statically allocated:
- `ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5]` (loop_functions.cpp:293)
- `ringBufferLoraRX[MAX_DEDUP_RING][5]` (loop_functions.cpp:302)
- `ringBufferUDPout[MAX_RING_UDP][UDP_TX_BUF_SIZE+20]` (loop_functions.cpp:311)

### MEM-06: xTaskCreate vs xTaskCreateStatic

No `xTaskCreateStatic()` usage found (Grep: 0 hits). All task creation is dynamic but
occurs during initialization only (acceptable).

---

## 2. Buffer Overflow Prevention (BND-01..05)

### BND-01: Banned unsafe functions

**sprintf() — 77 instances across 15 files (CRITICAL, was 60+)**

Increase vs. 2026-04-14 stems from esp32/esp32_main.cpp refactor and new
helper code in gps_functions.cpp (verified by `grep -c "sprintf("`).

| Severity | File | Lines | Finding |
|----------|------|-------|---------|
| CRITICAL | esp32/at_cmd.h | 30 | `AT_PRINTF` macro uses unbounded `sprintf` into 255-byte buffer |
| CRITICAL | nrf52/at_cmd.h | 30 | Same `AT_PRINTF` macro |
| CRITICAL | nrf52/WisBlock-API.h | 658 | `API_LOG` macro: `int len = sprintf(buff, __VA_ARGS__);` — unbounded |
| CRITICAL | esp32/esp32_main.cpp | 785, 895, 896, 1053, 1430, 1434, 1436 | 7x `sprintf` into settings/BLE name buffers |
| CRITICAL | esp32/esp32_functions.cpp | 82, 133, 140 | `sprintf` into 20-byte `cvers` buffer |
| HIGH | t-deck/event_functions.cpp | ≈20 sites | 20 `sprintf` in event handler |
| HIGH | t-deck/lv_obj_functions.cpp | 24 sites | 24 `sprintf` in UI functions |
| MEDIUM | rtc_functions.cpp | 1 site | `sprintf` into `cdate` buffer |
| MEDIUM | i2c_scanner.cpp | 4 sites | `sprintf` in scanner |
| MEDIUM | nrf52/nrf52_main.cpp | 420, 583, 895, 901 | 4x `sprintf` (lines shifted from 417/574/879/885) |
| MEDIUM | nrf52/nrf52_functions.cpp | 64, 196 | 2x `sprintf` into `cvers` |
| MEDIUM | nrf52/nrf52_ble.cpp | 93, 98, 122 | 3x `sprintf` |
| MEDIUM | command_functions.cpp | 1 site | `sprintf` into settings buffer |

**strcpy() — 5 sites (HIGH)**

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | web_functions/web_functions.cpp | 1797 | `strcpy(message_text, message.c_str())` into 200-byte buffer, unchecked source length |
| MEDIUM | onebutton_functions.cpp | 77, 81 | `strcpy` for page text buffers |
| LOW | Displays/BaseDisplay/SD.cpp | 368 | `strcpy` for filename extension (hard-coded 4-byte suffix, safe in practice) |

### BND-02: snprintf return value not checked — 559 snprintf sites, zero checked

No `snprintf` call in the codebase validates return value `>= buf_size` for
truncation detection. Affected files (top 10 by count):
- loop_functions.cpp (116 instances)
- command_functions.cpp (86)
- t-deck-pro/ui_deckpro.cpp (60)
- esp32/esp32_flash.cpp (40)
- t-deck/event_functions.cpp (3) / lv_obj_functions.cpp (21)
- web_functions/web_setup.cpp (56)
- esp32/esp32_flash.cpp (40)
- esp32/esp32_eth.cpp (8)
- nrf_eth.cpp (15)

### BND-03: memcpy without prior length validation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | nrf52/nrf52_ble.cpp | 293 | `memcpy(&meshcom_settings, data, sizeof(s_meshcom_settings))` — after only the exact-size check at 279 AND marker check at 286 (COMPLIANT in v4.35p). Retained as MEDIUM: marker is not a cryptographic validator |
| HIGH | esp32/esp32_main.cpp | 310 | BLE characteristic copy — `item.length` has no upper bound check |
| HIGH | lora_functions.cpp | 209 | `memcpy(print_buff, payload, 12)` — no check that `size >= 12`; new `handleACK()` guards only `payload[0] != MSG_TYPE_ACK` |
| HIGH | lora_functions.cpp | 383 | `memcpy(RcvBuffer, payload, size)` — size bounded only by earlier `rxSize = (size <= UDP_TX_BUF_SIZE) ? size : UDP_TX_BUF_SIZE` at line 309 (RAK only); ESP32 path lacks that check here |
| HIGH | phone_commands.cpp | 466, 487-488 | `memcpy(textbuff_phone+iposn, conf_data+2, txt_msg_len_phone)` — len from BLE, no cap vs. `sizeof(textbuff_phone)` |
| HIGH | nrf52/nrf_eth.cpp | 270 | `memcpy(RcvBuffer, inc_udp_buffer+UDP_MSG_INDICATOR_LEN, lora_tx_msg_len)` — bounded by `if (lora_tx_msg_len > UDP_TX_BUF_SIZE) lora_tx_msg_len = UDP_TX_BUF_SIZE;` at 268 (PASS); retained for documentation only |
| MEDIUM | udp_functions.cpp | 140, 172 | UDP indicator/message copy without prior length validation |
| MEDIUM | nrf52/nrf52_flash.cpp | 70-226 | 47+ migration memcpy calls assuming correct field sizes |

### BND-04: Missing static_assert on protocol structs

No `static_assert` / `_Static_assert` found on the following structures (unchanged since 2026-04-14):
- `aprsMessage` (aprs_structures.h)
- `s_meshcom_settings` (esp32_flash.h, nrf52_flash.h)
- `mheardLine` (mheard_functions.h)

Only one `_Static_assert` in the entire codebase: `bq27220_data_memory.h:93`.

---

## 3. Input Validation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | phone_commands.cpp | 226, 324, 364, 385, 403, 466, 479-488 | No length validation before array access on BLE command data (e.g. `conf_data[ssid_len+3]` where `ssid_len` comes from the message itself without prior bounds check) |
| HIGH | web_functions/web_setup.cpp | 69-259 | `toInt()`/`toDouble()` with no range validation on REST parameters |
| HIGH | web_functions/web_functions.cpp | 1918 | JSON output with unescaped user content (injection risk in generated HTML) |
| MEDIUM | web_functions/web_functions.cpp | 355, 1767, 1827, 1856, 1889 | URL parsing assumes `HTTP/1.1` is present; `indexOf()` return `-1` propagated to `substring()` → garbage extraction |
| MEDIUM | gps_functions.cpp | 29 | `SoftwareSerial GPSSerial(GPS_RX_PIN, GPS_TX_PIN)` — pins come from variant macros, compile-time; no runtime validation (acceptable, but note for new variants) |

**Compliant areas:**
- LoRa RX on RAK: `lora_functions.cpp:309` — proper bounds check `rxSize = (size <= UDP_TX_BUF_SIZE) ? size : UDP_TX_BUF_SIZE`
- BLE ESP32: `esp32_main.cpp:305-311` — validates length within `MAX_MSG_LEN_PHONE`
- BLE nRF52: `nrf52_ble.cpp:279-286` — validates exact struct size AND markers

---

## 4. Thread Safety (RACE-01..08)

### RACE-01: Binary semaphore misused as mutex

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | esp32/esp32_audio.cpp | 38 | `xSemaphoreCreateBinary()` for audio shared state (should be mutex) |
| MEDIUM | t-deck/tdeck_main.cpp | 111 | `xSemaphoreCreateBinary()` for TFT access (comment says "mutex") |
| LOW | nrf52/nrf52_main.cpp | 400 | `xSemaphoreCreateBinary()` for task signaling (line shifted from 397; acceptable for signaling) |

### RACE-02: portMAX_DELAY in mutex takes

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | t-deck/tdeck_main.cpp | 405 | `xSemaphoreTake(xSemaphore, portMAX_DELAY)` in display flush |
| MEDIUM | nrf52/api_functions.cpp | 262 | `xSemaphoreTake(g_task_sem, portMAX_DELAY)` in main wait loop (line shifted from 204) |

### RACE-03: Thread-unsafe functions

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | time_functions.cpp | 96 | `localtime()` instead of `localtime_r()` |

**Compliant:** `clock.cpp:82,343` uses `localtime_r()`. No `strtok()` or `rand()` found in src (only in commented-out lvgl code).

### RACE-04: Volatile without synchronization

| Severity | File | Lines | Finding |
|----------|------|-------|---------|
| HIGH | loop_functions.cpp | 294-295 | `volatile int iWrite/iRead` ring buffer indices — read-modify-write race (line shifted from 267-268). `addRingPointer()` at `loop_functions.cpp:3933` takes them by reference but no mutex |
| HIGH | nrf52/nrf52_main.cpp | 230-233 | CAD flags (`cad_done_flag`, etc.) — ISR vs main task race |
| HIGH | gps_functions.cpp | 818-823 | GPS pulse timing array (`pulseTimes`, `pulseIndex`, `lastMicros`, `currentMicros`, `duration`, `startWait`) — ISR vs main loop race (lines shifted from 776-781; now under `#else` branch) |
| MEDIUM | esp32/esp32_main.cpp | 410, 425 | `transmissionState`, `scanFlag` — ISR writes, main reads |
| MEDIUM | loop_functions.cpp | 75, 204-206 | Cross-module volatile bools without locks (`bSetLoRaAPRS`, etc.) |

### RACE-05: std::atomic usage — PASS

Properly used in:
- `loop_functions.cpp:303, 328-329` — `loraWrite`, `is_receiving`, `tx_is_active`
- `lora_functions.cpp:290-292` — `ch_util_rx_start/accum` (new in oe1kbc_v4.35p)
- `esp32_main.cpp:413-422` — `receiveFlag`, `transmittedFlag`, enable flags

### RACE-06: Lock ordering documentation — FAIL

No lock ordering documentation found anywhere. (Positive change: new `taskENTER_CRITICAL()/taskEXIT_CRITICAL()` pairs in `lora_functions.cpp:313-321, 335-342, 367-369` at least bracket their critical sections symmetrically.)

### RACE-07: Float tasks not pinned to core

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | t-deck-pro/peri_gps.cpp | 78 | `xTaskCreate` (not pinned) with `double` GPS coordinates |
| MEDIUM | t-deck-pro/peri_gyroscope.cpp | 14-16 | Global `float` factors accessed from unpinned task |

---

## 5. Interrupt Safety (ISR-01..04)

### ISR-01/02: ISR handlers — MOSTLY COMPLIANT

Most ESP32 ISR handlers have `ICACHE_RAM_ATTR`; GPS edge-timing handler has `IRAM_ATTR`.

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | nrf52/nrf52_main.cpp | 317, 326, 335 | `interruptHandle1/2/3()` lack `IRAM_ATTR` (best-practice on nRF52 when attached via `attachInterrupt`) |
| MEDIUM | gps_functions.cpp | 833-836 | **NEW** `handleRxInterrupt()` ISR writes into `pulseTimes[pulseIndex]` *after* `pulseIndex = pulseIndex+1;` — if `pulseIndex == SAMPLE_COUNT-1` at entry, the write after increment hits `pulseTimes[SAMPLE_COUNT]` (one past the end). Bounds check at 833 is `<` SAMPLE_COUNT *before* increment, so the final stored index equals SAMPLE_COUNT → buffer overrun by 1 |

### ISR-03: Prohibited operations in ISR

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | t5-epaper/io_extend.c | 26 | `printf("interrupt_handler\n")` inside ISR |
| HIGH | lora_functions.cpp | 325, 327, 344, 346, 350, 375 | Multiple `Serial.printf()` calls in OnRxDone callback (guarded by `if(bLORADEBUG)` or `if(_overwrite)`, but still executed in ISR context when enabled) |
| HIGH | lora_functions.cpp | 332 | `startRadioReceive()` (SPI bus access via `Radio.Rx()` / `Radio.RxBoosted()`) invoked from OnRxDone ISR on nRF52 — kept across rebase |

### ISR-04: DRAM_ATTR — PASS

ISR data uses `std::atomic<bool>` (correct for ESP32). Global volatile variables default to DRAM.

---

## 6. SPI Bus Safety (SPI-01..05)

### SPI-01: SPI bus mutex — PARTIAL

`portMUX_TYPE displayMux` spinlock exists for display updates. `bSPI_ETH_Active` flag guards LoRa/Ethernet bus sharing (see `lora_functions.cpp:329-333`).

### SPI-02: SPI access from ISR

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | lora_functions.cpp | 332 | `startRadioReceive()` (→ `Radio.Rx()` / `Radio.RxBoosted()`) called directly in OnRxDone ISR path (SPI access). Introduced/aggravated by nRF52 RX-Boost commit 2adf4f2 which added an extra SPI-dependent code path |

### SPI-03: CS pin management — PASS

Proper LOW/HIGH sequencing in `Displays/BaseDisplay/hardware.cpp:28-43`.
T-Echo SPI switched to SPIM3 (`loop_functions.cpp:231`) and T-Echo TFT on SPIM1 (`nrf52/nrf52_main.cpp:49`) — separate buses, CS management unchanged.

### SPI-04: Task pinning

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | (system-wide) | -- | No explicit core pinning for LoRa/SPI tasks on dual-core ESP32 |

---

## 7. Authentication & Security

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | udp_functions.cpp | 533 | `WiFi.softAP(meshcom_settings.node_call)` — NO WPA2 password, open AP |
| CRITICAL | safeboot/main.cpp | 68, 187 | `WiFi.softAP(hostname)` — NO password in safeboot mode (2 call sites) |
| CRITICAL | nrf52/nrf52_ble.cpp | 248 | `g_lora_data.setPermission(SECMODE_OPEN, SECMODE_OPEN)` — no encryption required |
| HIGH | web_functions/web_functions.cpp | 234 | Web password optional; empty password = open access |
| HIGH | web_functions/web_functions.cpp | 353-368 | Custom URL-param auth instead of HTTP Basic Auth (password posted in URL query string) |
| HIGH | esp32/esp32_main.cpp | 239 | BLE PIN hardcoded to `000000` (`uint32_t PIN = 000000;`) |
| MEDIUM | safeboot/ElegantOTA.cpp | 21-48 | OTA authentication optional; no firmware signature verification |

**Note:** `nrf52_ble.cpp:133` still shows `SECMODE_ENC_WITH_MITM` commented out (dated comment "KBC 28.04.2025").

---

## 8. Error Handling

### Unchecked begin() calls

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | main.cpp | 37, 42 | `SPI.begin()` unchecked |
| HIGH | esp32/esp32_main.cpp | 596, 600 | `Wire.begin()`, `Wire1.begin()` unchecked |
| HIGH | nrf52/nrf52_main.cpp | 837 | `Wire.begin()` unchecked |
| HIGH | nrf52/nrf52_ble.cpp | 128-252 | 5x BLE service `begin()` unchecked |
| MEDIUM | extudp_functions.cpp | 67 | `ETH.begin()` unchecked |

### Blocking while(true) on init errors

| Severity | File | Lines | Finding |
|----------|------|-------|---------|
| CRITICAL | esp32/esp32_main.cpp | 1256, 1263, 1271, 1281, 1318, 1358, 1391, 1417 | 8x `while (true);` on radio config errors (setOutputPower, setCurrentLimit, setPreambleLength, etc.) — unchanged |
| CRITICAL | t-deck-pro/peri_lora.cpp | 48, 54, 60, 66, 72, 78, 85, 91, 97, 105, 114 | 11x `while (true);` on radio state errors |
| MEDIUM | t-deck-pro/tdeck_pro.cpp | 309 | `while (1)` on camera init fail |
| MEDIUM | t-deck-pro/peri_gps.cpp | 54 | `while(1)` on GPS init fail |
| MEDIUM | t5-epaper/t5epaper_main.cpp | 73 | `while(1)` on display init fail |
| MEDIUM | t5-epaper/peri_gps.cpp | 53 | `while(1)` on GPS init fail |
| MEDIUM | t5-epaper/peri_lora.cpp | 36 | `while(1)` on LoRa init fail |

### Queue overflow handling

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | loop_functions.cpp | 444-481 | Ring buffer overflow silently wraps/overwrites without production warning (new priority-aware drop logic at 1467-1485 logs under `bLORADEBUG` only) |

**Compliant areas:**
- `bmx280.cpp:176` — checks `begin()` return
- `ina226_functions.cpp:27` — checks `begin()` return

---

## 9. Watchdog & Recovery (STAB-01..05)

### STAB-01/02: Task watchdog — NOT CONFIGURED

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | (entire codebase) | -- | Zero calls to `esp_task_wdt_add()` or `esp_task_wdt_reset()` (grep: 0 hits) |

### STAB-03: Busy-wait loops

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | Regexp.cpp | 145, 150 | Recursive regex matching loop without yield; `singlematch` / `match` can run unbounded on pathological patterns |
| MEDIUM | gps_functions.cpp | 860 | **NEW** `while (pulseIndex < SAMPLE_COUNT && (millis() - startWait < SAMPLE_DURATION)) { delay(10); }` — busy wait up to 5 s with 10 ms yield; acceptable since used only during boot-time baud detection, but blocks loop |

### STAB-04: Reset reason logging

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | (entire codebase) | -- | No `esp_reset_reason()` call at startup (grep: 0 hits) |
| MEDIUM | (entire codebase) | -- | No persistent crash counter / safe mode logic |

### STAB-05: millis() wraparound bugs — CRITICAL

Wrong pattern `(start + timeout) < millis()` or `start + timeout > millis()` found in
**33+ locations** across `esp32_main.cpp` (20), `nrf52_main.cpp` (9),
`mheard_functions.cpp` (2), `lora_functions.cpp` (1), `web_functions.cpp` (1).
Selected samples validated:

| Severity | File | Line | Pattern (WRONG) |
|----------|------|------|-----------------|
| CRITICAL | mheard_functions.cpp | 160 | `lastsaveMHEARDPersistence + 30000 > millis()` |
| CRITICAL | mheard_functions.cpp | 192 | `lastsavePATHPersistence + 30000 > millis()` |
| CRITICAL | esp32/esp32_main.cpp | 2353, 2420 | `(rtc_refresh_timer + 60000) > millis()` |
| CRITICAL | esp32/esp32_main.cpp | 2443 | `(wifi_active_timer + 30000) < millis()` |
| CRITICAL | esp32/esp32_main.cpp | 2477 | `(softser_refresh_timer + 5000) < millis()` |
| CRITICAL | esp32/esp32_main.cpp | 2601 | `millis() < config_to_phone_prepare_timer + 3000` |
| CRITICAL | esp32/esp32_main.cpp | 2612, 2622 | `(ble_wait + 300)/+400 < millis()` |
| CRITICAL | esp32/esp32_main.cpp | 3030 | `(heapMonTimer + 60000) < millis()` |
| CRITICAL | esp32/esp32_main.cpp | 3321 | `(web_timer + 1000) < millis()` |
| CRITICAL | nrf52/nrf52_main.cpp | 1463, 1513, 1579, 1668, 1715, 2005, 2022, 2050, 2078, 2133, 2157, 2182, 2202 | Same pattern (lines shifted after upstream rebase) |
| CRITICAL | lora_functions.cpp | 1576 | `millis() > track_to_meshcom_timer + 1000 * 60 * 5` |
| CRITICAL | web_functions/web_functions.cpp | 239 | `(ulong)(web_ip_passwd_time[iwid] + (1000*60*60*4)) < millis()` |

**Correct pattern** (used in some places): `(uint32_t)(millis() - start) >= interval`
Example: `lora_functions.cpp:1135` uses the correct form.

---

## 10. Compiler & Build Safety (COMP-01..05)

### COMP-01: Build flags

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | platformio.ini | 135, 162, 202 | `-Wall -Wextra` present but `-Werror` MISSING — warnings not treated as errors |

### COMP-05: Library version pinning

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | platformio.ini | 55-69 | Multiple libraries use `^`: `ArduinoJson@^7.4.3`, `TinyGPSPlus@^1.1.0`, `OneWire@^2.3.8`, `DHT@^1.4.6`, `arduino-sht@^1.2.6`, `MCP23017@^2.3.2`, `RTClib@^2.1.4`, `AHTX0@^2.0.5`, `OneButton@^2.6.1`, `BMx280MI@^1.2.3`, `BME680@^2.0.5`, `CCS811@^1.1.3` |
| HIGH | platformio.ini | 115 | `espressif32@^6.13.0` — platform version not exact |
| HIGH | variants/t_deck_pro/platformio.ini | 75 | `RadioLib@7.1.2` vs main `RadioLib@7.6.0` (platformio.ini:109) — still unsynced (same as 2026-04-14) |
| HIGH | variants/t_deck_pro/platformio.ini | 2 | `platform = espressif32@6.5.0` — pinned but inconsistent with root `@^6.13.0` |

### COMP-02..04 — PASS

No `NDEBUG` found. No side effects in `assert()`. Switch statements in main hot paths have `default:` cases (e.g. `lora_functions.cpp:1936`).

---

## 11. Type Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | nrf52/nrf_eth.cpp | 274 | `(uint8_t)lora_tx_msg_len` — narrowing `uint16_t` to `uint8_t` without range check (bound to UDP_TX_BUF_SIZE at 268, but UDP_TX_BUF_SIZE > 255) |
| MEDIUM | (multi files) | -- | `snprintf` return value (`int`, can be negative) never checked before use as offset (0 of 559 calls check) |

**No `__attribute__((packed))` found on protocol structs** — `aprsMessage` uses String members (packing not applicable), but wire-format parsing in `decodeAPRS()` is field-by-field (acceptable).

---

## 12. Lifetime Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | web_functions/web_functions.cpp | 208, 485, 503 | `web_client.stop()` without resetting web_header — potential use-after-close if handler re-enters |
| MEDIUM | web_functions/web_functions.cpp | 236-263 | Session table slots cleared on 4-hour timeout only (via the broken millis() pattern at 239), not on abrupt disconnect |

**No AsyncTCP race conditions** — current web implementation is synchronous.

---

## 13. Logging Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | lora_functions.cpp | 325, 327, 344, 346, 350, 375 | `Serial.printf()` calls in OnRxDone path (blocking I/O in ISR; guarded by debug flags but still evaluated at ISR priority) |
| MEDIUM | t5-epaper/io_extend.c | 26 | `printf()` inside ISR handler |
| MEDIUM | gps_functions.cpp | 873 | `Serial.printf("[GPS ]...gemessene Flanken %u\n", pulseIndex);` — called from main ctx after ISR detach (line 861), so OK; retained only for documentation |

**Compliant:** All log format strings appear to be string literals (no variable format strings detected in src/).

---

## 14. Design Patterns

### CSMA not a pure function

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | lora_functions.cpp | 1916-1952 | `csma_compute_timeout()` / `csma_compute_timeout_prio()` / `csma_reset()` depend on / mutate global state (`cad_attempt`, `stat_csma_hwm_attempts`, `csma_timeout`) — not testable in isolation |

### Callback safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | esp32/esp32_main.cpp | 1331, 1371, 1399 | `radio.setDio1Action(callback)` — return value not checked |

### Static allocation audit — PASS (except spectral_scan.cpp)

Ring buffers and packet processing paths use compile-time sized static arrays.

---

## 15. Protocol Correctness

### FCS checked AFTER field extraction

| Severity | File | Lines | Finding |
|----------|------|-------|---------|
| CRITICAL | aprs_functions.cpp | 179-379 | Fields extracted via `String::concat` (lines 192-344) BEFORE FCS validation (lines 371-379) — `FCS_SUMME` computed at 371-375 and compared at 379. All String growth, regex checks and payload extraction happen on untrusted bytes before integrity check. Unchanged since 2026-04-14 |

### Frame size validation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | aprs_functions.cpp | 134 | Only minimum (16 bytes) checked; no maximum frame size validation against `UDP_TX_BUF_SIZE` |

### Parser instance isolation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | aprs_functions.cpp | 120 | Single `aprsmsg` struct parameter reused for LoRa and UDP decode paths — would race if concurrent. Currently no concurrent calls, but contract undocumented |

---

## 16. State Machine & Session Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | web_functions/web_functions.cpp | 38-39 | Session table (`web_ip[10][20]`, `web_ip_passwd_time[10]`) accessed without mutex |
| MEDIUM | web_functions/web_functions.cpp | -- | No per-IP rate limiting on login attempts (brute-force possible) |

---

## 17. Data Drift Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | t5-epaper/nvs_param.cpp | 7-47 | No NVS schema version field — `tpInit = prefs.isKey("nvsInit")` at line 19 uses a boolean flag, not a version number. Firmware updates with changed struct won't detect stale data |

**Compliant:** Compile-time constants centralized in `configuration_global.h` (single source of truth).

---

## 18. TCP/Web/SSE Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | web_functions/web_functions.cpp | 31, 340 | `String web_header` — unbounded concatenation of HTTP request data (`web_header += c` per byte) — heap exhaustion via long request |
| CRITICAL | web_functions/web_functions.cpp | 1856, 1827, 1889, 1767, 1770 | `indexOf()` return not checked before `substring()` — `-1` causes garbage extraction or String::substring abort |
| HIGH | web_functions/web_functions.cpp | 265 | Fixed 10-slot session table with no LRU eviction — legitimate users blocked when full (prints "not free IP/Password table" and drops request) |
| MEDIUM | web_functions/web_functions.cpp | -- | No Content-Length validation or JSON body size limiting on REST endpoints |

---

## 19. Test & Fuzz Readiness

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | test/ | -- | Only `compress_functions.cpp/h` in test directory; no unit test framework, no parser tests, no fuzz targets |

**Compliant:** `decodeAPRS()` accepts `(uint8_t*, uint16_t, aprsMessage&)` — suitable signature for fuzzing.

---

## 20. Stack Safety (STK-01..04)

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | (project root) | -- | No `sdkconfig` with stack overflow checking level 2 |
| HIGH | (entire codebase) | -- | No `uxTaskGetStackHighWaterMark()` calls (grep: 0 hits in src/) — no runtime stack monitoring |
| MEDIUM | platformio.ini | -- | `-fstack-usage` compiler flag not enabled |

**Compliant:** All `xTaskCreate()` calls use byte sizes correctly (e.g., `1024 * 3`).

---

## Summary of Deltas vs. 2026-04-14

### Newly introduced findings

- **ISR-02 (MEDIUM)**: `gps_functions.cpp:833-836` — `pulseIndex` pre-increment
  causes 1-slot overrun in `pulseTimes` at the upper bound. Introduced when the
  upstream baud-autodetect logic moved into the `#else` branch (commit 8e57d9f).
- **STAB-03 (MEDIUM)**: `gps_functions.cpp:860` — 5-second blocking loop with 10 ms
  `delay()` during boot-time baud detection. Acceptable on boot, noted for
  completeness.
- **SPI-02 (HIGH) aggravated**: `lora_functions.cpp:332` `startRadioReceive()`
  now branches to `Radio.RxBoosted()` / `Radio.Rx()` — the bBOOSTEDGAIN check
  added by commit 2adf4f2 keeps the SPI access in OnRxDone ISR.

### Shifted line numbers (no semantic change)

- `nrf52/nrf52_main.cpp:82` → `83` (MEM-01)
- `nrf52/nrf52_main.cpp:397` → `400` (RACE-01)
- `nrf52/api_functions.cpp:204` → `262` (RACE-02)
- `nrf52/nrf52_main.cpp:417/574/879/885` → `420/583/895/901` (BND-01)
- `loop_functions.cpp:267-268` → `294-295` (RACE-04, volatile ring indices)
- `esp32/esp32_main.cpp:2333/2400/3301` → `2353/2420/3321` (STAB-05)
- `nrf52/nrf52_main.cpp:1695/1698` → `1715` plus 12 additional sites (STAB-05 wider than previously documented)
- `charBuffer_aprs` line reference: was `lora_functions.cpp:454`, now `loop_functions.cpp:2205` (definition) + `lora_functions.cpp:452` (call site)

### Confirmed unchanged (still present, same semantics)

- All CRITICAL WiFi open-AP findings (udp_functions.cpp:533, safeboot/main.cpp:68/187)
- All 60+ `sprintf` call sites (total now 77 across 15 files)
- BLE `SECMODE_OPEN` at nrf52_ble.cpp:248 and commented-out `SECMODE_ENC_WITH_MITM` at 133
- BLE PIN `000000` at esp32/esp32_main.cpp:239
- 8x `while(true)` after radio config errors in esp32/esp32_main.cpp (1256..1417)
- `new uint16_t[]` leaks in spectral_scan.cpp:108, 188
- FCS-after-extraction bug in aprs_functions.cpp:179-379
- All web_header unbounded-concat / substring-of-negative-index bugs

### Fixed since 2026-04-14

None detected. No finding from the 2026-04-14 audit has been closed by an
upstream commit in the 17-commit window (rebase brought only feature work).

---

## Top 10 Priority Fixes

### 1. WiFi AP password protection (Section 7 — CRITICAL)
- `udp_functions.cpp:533` and `safeboot/main.cpp:68, 187`
- Add WPA2 password: `WiFi.softAP(ssid, password)`
- Impact: Open AP allows anyone in RF range to connect

### 2. millis() wraparound (STAB-05 — CRITICAL, 33+ locations)
- Replace `(start + timeout) < millis()` with `(uint32_t)(millis() - start) >= timeout`
- Affects heap monitor, BME680/BMP3/MCU811/INA226 timers, RTC refresh, web session,
  mheard, position reporting on BOTH ESP32 and nRF52
- Silent failure after 49.7 days of uptime

### 3. sprintf -> snprintf (BND-01 — CRITICAL, 77 locations)
- Replace all `sprintf()` with `snprintf()` including correct buffer size
- Priority: `AT_PRINTF` / `API_LOG` macros (at_cmd.h, WisBlock-API.h),
  esp32_main.cpp, esp32_functions.cpp
- Risk: Stack/heap corruption on oversized input

### 4. FCS validation before field parsing (Section 15 — CRITICAL)
- `aprs_functions.cpp:371-379` — move FCS check before field extraction at line 179
- Currently processes untrusted data (String concat per byte) before validating integrity

### 5. Task watchdog configuration (STAB-01/02 — CRITICAL)
- Add `esp_task_wdt_add()` and `esp_task_wdt_reset()` for all long-lived tasks
- Replace `while(true);` deadlocks in `esp32_main.cpp` (8x) and `t-deck-pro/peri_lora.cpp` (11x) with `esp_restart()`

### 6. String allocation in RX hot path (MEM-02 — CRITICAL)
- `loop_functions.cpp:2205` / `lora_functions.cpp:452` — replace `charBuffer_aprs()`
  String return with fixed char buffer passed in by caller
- Remove all `String` operations from OnRxDone path and APRS decoder

### 7. BLE security (Section 7 — CRITICAL)
- `nrf52_ble.cpp:248` — restore `SECMODE_ENC_WITH_MITM` (was commented out 2025-04-28)
- `esp32_main.cpp:239` — make BLE PIN configurable, not hardcoded `000000`

### 8. memcpy length validation (BND-03 — CRITICAL)
- `lora_functions.cpp:209` — validate `size >= 12` before `memcpy(print_buff, payload, 12)`
- `phone_commands.cpp:466, 487-488` — validate all array indices and copy lengths before access

### 9. Web header buffer limiting (Section 18 — CRITICAL)
- `web_functions.cpp:340` — bound `web_header` String concatenation to max 4-8 KB
- Reject oversized HTTP requests to prevent heap exhaustion
- Check every `indexOf()` return before passing to `substring()`

### 10. Memory leak in spectral scan (MEM-03 — CRITICAL)
- `spectral_scan.cpp:108, 188` — replace `new[]` with static buffer or ensure `delete[]`

---

## Files Most Affected

| File | Findings | Highest Severity |
|------|----------|-----------------|
| lora_functions.cpp | 14 | CRITICAL |
| esp32/esp32_main.cpp | 14 | CRITICAL |
| web_functions/web_functions.cpp | 10 | CRITICAL |
| loop_functions.cpp | 8 | CRITICAL |
| nrf52/nrf52_main.cpp | 19 | CRITICAL |
| nrf52/nrf52_ble.cpp | 5 | CRITICAL |
| phone_commands.cpp | 5 | CRITICAL |
| aprs_functions.cpp | 5 | CRITICAL |
| gps_functions.cpp | 3 | MEDIUM (NEW) |
| spectral_scan.cpp | 2 | CRITICAL |
| t-deck-pro/peri_lora.cpp | 11 | CRITICAL |
| platformio.ini | 14 | CRITICAL |

---

## Rebase-Specific Observations (oe1kbc_v4.35p 95bd4c4 window)

- **commit 8e57d9f "gps softserial"** — added `GPS_SOFTWARE_SERIAL` path and
  retained the baud-autodetect ISR. Introduced the NEW `pulseIndex` off-by-one
  (ISR-02) and a new boot-time 5 s busy-wait (STAB-03 medium).
- **commit 2adf4f2 "nrf_52 rxboost"** — added `startRadioReceive()` wrapper
  (`src/nrf52/nrf52_radio.cpp`) that branches on `bBOOSTEDGAIN`. The wrapper is
  called from OnRxDone; both branches perform SPI transactions in ISR context,
  preserving the SPI-02 HIGH finding.
- **commit da785e9 "t_echo spi changed to spim3"** — display bus moved to
  NRF_SPIM3 (`loop_functions.cpp:231`), TFT bus stays on NRF_SPIM1
  (`nrf52/nrf52_main.cpp:49`). Split across two hardware SPI peripherals is
  a net positive for bus contention.
- **commit 94cb406 "tbeam 1W RX switch fixed"** / **1ee5337 "max_txpower tbeam
  fix to 17dBm"** — variant-level changes only; no src/ findings affected.
- **commit 31481e2 "E290 gps pins changed"** — `variants/vision-master-e290/configuration.h:115-116`
  GPS_RX/TX pins, no audit impact.
- **commit 95bd4c4 "download_meshcom.py"** — tooling only.

None of these commits touched any of the locations documented in the
2026-04-14 audit in a way that closes a finding.
