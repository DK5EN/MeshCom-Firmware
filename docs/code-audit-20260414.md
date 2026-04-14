# MeshCom Firmware Code Audit

**Date:** 2026-04-14
**Branch:** v4.35p_prio
**Auditor:** Claude Code (automated)
**Rules:** docs/codequality-rules.md (ESP32 C++ Code Quality Rules)

---

## Audit Summary

| Category | Rule IDs | Status | Critical | High | Medium | Low |
|----------|----------|--------|----------|------|--------|-----|
| Memory Safety | MEM-01..05 | FAIL | 2 | 0 | 3 | 2 |
| Buffer Safety | BND-01..05 | FAIL | 4 | 1 | 1 | 0 |
| Input Validation | Section 3 | FAIL | 1 | 2 | 1 | 0 |
| Thread Safety | RACE-01..08 | FAIL | 0 | 3 | 3 | 1 |
| ISR Safety | ISR-01..04 | FAIL | 1 | 1 | 1 | 0 |
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

**Total: 21 Critical, 20 High, 25 Medium, 3 Low = 69 findings**

---

## 1. Memory Safety (MEM-01..05)

### MEM-01: malloc/new after initialization

| Severity | File | Line | Finding |
|----------|------|------|---------|
| LOW | nrf52/nrf52_main.cpp | 82 | `malloc()` in `nrf52_getMaxFreeBlock()` heap probe (runtime, not setup) |

### MEM-02: Arduino String in hot paths

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | lora_functions.cpp | 454 | `charBuffer_aprs()` returns `String` inside OnRxDone ISR context -- heap allocation in ISR |
| CRITICAL | lora_functions.cpp | 1034-1035 | `String` creation for dedup check in OnRxDone path |
| MEDIUM | loop_functions.cpp | 176, 181-182, 186 | Global `String` objects (`strSOFTSER_BUF`, `strTelemetry`) used in main loop |
| MEDIUM | loop_functions.cpp | 1532, 1550, 1563, 1585 | Multiple temporary `String` concatenations in per-message processing |

### MEM-03: C++ new without delete (memory leak)

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | spectral_scan.cpp | 108 | `new uint16_t[]` -- returned pointer never freed, leak per scan |
| CRITICAL | spectral_scan.cpp | 188 | `new uint16_t[]` -- same pattern, dead code but dangerous |

### MEM-04: Display buffer allocation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | t-deck-pro/tdeck_pro.cpp | 195-198 | Triple `ps_calloc` for display buffers, no unified error recovery |
| LOW | t-deck/tdeck_main.cpp | 318 | `ps_malloc` for LVGL buffer, only assert on failure |
| LOW | t5-epaper/t5epaper_main.cpp | 258-272 | Triple `ps_malloc`/`ps_calloc` for display buffers |

### MEM-05: Buffer size constants -- PASS

All major buffer sizes centralized in `configuration_global.h` (lines 49-101). Ring buffers statically allocated:
- `ringBuffer[MAX_RING][UDP_TX_BUF_SIZE+5]` (loop_functions.cpp:266)
- `ringBufferLoraRX[MAX_DEDUP_RING][5]` (loop_functions.cpp:275)
- `ringBufferUDPout[MAX_RING_UDP][UDP_TX_BUF_SIZE+20]` (loop_functions.cpp:284)

### MEM-06: xTaskCreate vs xTaskCreateStatic

No `xTaskCreateStatic()` usage found. All task creation is dynamic but occurs during initialization only (acceptable).

---

## 2. Buffer Overflow Prevention (BND-01..05)

### BND-01: Banned unsafe functions

**sprintf() -- 60+ instances (CRITICAL)**

| Severity | File | Lines | Finding |
|----------|------|-------|---------|
| CRITICAL | esp32/at_cmd.h | 30 | `AT_PRINTF` macro uses unbounded `sprintf` into 255-byte buffer |
| CRITICAL | nrf52/at_cmd.h | 30 | Same `AT_PRINTF` macro |
| CRITICAL | esp32/esp32_main.cpp | 785, 895, 1053, 1426, 1430, 1432 | 6x `sprintf` into settings/BLE name buffers |
| CRITICAL | esp32/esp32_functions.cpp | 81, 132, 139 | `sprintf` into 20-byte `cvers` buffer |
| HIGH | t-deck/event_functions.cpp | 511-649 | 20x `sprintf` in event handler |
| HIGH | t-deck/lv_obj_functions.cpp | 697, 3542-3889 | 24x `sprintf` in UI functions |
| MEDIUM | rtc_functions.cpp | 117 | `sprintf` into 40-byte `cdate` buffer |
| MEDIUM | i2c_scanner.cpp | 52, 123, 130, 138 | 4x `sprintf` in scanner |
| MEDIUM | nrf52/nrf52_main.cpp | 417, 574, 879, 885 | 4x `sprintf` |
| MEDIUM | nrf52/nrf52_ble.cpp | 93, 98, 122 | 3x `sprintf` |
| MEDIUM | command_functions.cpp | 4034 | `sprintf` into settings buffer |

**strcpy() -- 4 instances (HIGH)**

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | web_functions/web_functions.cpp | 1797 | `strcpy(message_text, message.c_str())` -- 200-byte buffer, unchecked source length |
| MEDIUM | onebutton_functions.cpp | 77, 81 | `strcpy` for page text buffers (fixed-size arrays, but no bounds check) |
| LOW | Displays/BaseDisplay/SD.cpp | 368 | `strcpy` for filename extension |

### BND-02: snprintf return value not checked -- 30+ instances

No `snprintf` calls validate return value `>= buf_size` for truncation detection. Affected files:
- softser_functions.cpp (8 instances)
- loop_functions.cpp (9 instances)
- aprs_functions.cpp (4 instances)
- command_functions.cpp (multiple)
- phone_commands.cpp (2 instances)

### BND-03: memcpy without prior length validation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | nrf52/nrf52_ble.cpp | 293 | `memcpy(&meshcom_settings, data, sizeof(...))` -- full struct from BLE, no validation |
| HIGH | esp32/esp32_main.cpp | 310 | BLE characteristic copy -- `item.length` has no upper bound check |
| HIGH | lora_functions.cpp | 211 | `memcpy(print_buff, payload, 12)` -- no check that `size >= 12` |
| HIGH | lora_functions.cpp | 385 | `memcpy(RcvBuffer, payload, size)` -- size not validated against buffer |
| HIGH | phone_commands.cpp | 466, 487-488 | Multiple copies with dynamic lengths unchecked |
| MEDIUM | udp_functions.cpp | 140, 172 | UDP indicator/message copy without prior length validation |
| MEDIUM | nrf52/nrf52_flash.cpp | 70-226 | 47+ migration memcpy calls assuming correct field sizes |

### BND-04: Missing static_assert on protocol structs

No `static_assert` found for critical protocol structures:
- `aprsMessage` (aprs_structures.h)
- `s_meshcom_settings` (esp32_flash.h)
- `mheardLine` (mheard_functions.h)

Only one `_Static_assert` in entire codebase: `bq27220_data_memory.h:93`.

---

## 3. Input Validation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | phone_commands.cpp | 226, 324, 364, 385 | No length validation before array access on BLE command data |
| HIGH | web_functions/web_setup.cpp | 69-181 | `toInt()`/`toDouble()` with no range validation on REST parameters |
| HIGH | web_functions/web_functions.cpp | 1918 | JSON output with unescaped user content (injection risk) |
| MEDIUM | web_functions/web_functions.cpp | 355 | URL parsing assumes `HTTP/1.1` present, no fallback |

**Compliant areas:**
- LoRa RX: `lora_functions.cpp:311` -- proper bounds check before memcpy
- BLE ESP32: `esp32_main.cpp:305-311` -- validates length within `MAX_MSG_LEN_PHONE`
- BLE nRF52: `nrf52_ble.cpp:279-293` -- validates exact struct size match

---

## 4. Thread Safety (RACE-01..08)

### RACE-01: Binary semaphore misused as mutex

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | esp32/esp32_audio.cpp | 38 | `xSemaphoreCreateBinary()` for audio shared state (should be mutex) |
| MEDIUM | t-deck/tdeck_main.cpp | 111 | `xSemaphoreCreateBinary()` for TFT access (comment says "mutex") |
| LOW | nrf52/nrf52_main.cpp | 397 | `xSemaphoreCreateBinary()` for task signaling (acceptable for signaling) |

### RACE-02: portMAX_DELAY in mutex takes

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | t-deck/tdeck_main.cpp | 405 | `xSemaphoreTake(xSemaphore, portMAX_DELAY)` in display flush |
| MEDIUM | nrf52/api_functions.cpp | 204 | `xSemaphoreTake(g_task_sem, portMAX_DELAY)` in main wait loop |

### RACE-03: Thread-unsafe functions

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | time_functions.cpp | 96 | `localtime()` instead of `localtime_r()` |

**Compliant:** `clock.cpp:82,343` uses `localtime_r()`. No `strtok()` found.

### RACE-04: Volatile without synchronization (CRITICAL pattern)

35+ volatile variables accessed from ISR and main loop without mutex/atomic:

| Severity | File | Lines | Finding |
|----------|------|-------|---------|
| HIGH | loop_functions.cpp | 267-268 | `volatile int iWrite/iRead` ring buffer indices -- read-modify-write race |
| HIGH | nrf52/nrf52_main.cpp | 230-233 | CAD flags (`cad_done_flag`, etc.) -- ISR vs main task race |
| HIGH | gps_functions.cpp | 776-781 | GPS pulse timing array -- ISR vs main loop race |
| MEDIUM | esp32/esp32_main.cpp | 410, 425 | `transmissionState`, `scanFlag` -- ISR writes, main reads |
| MEDIUM | loop_functions.cpp | 75, 204-206 | Cross-module volatile bools without locks |

### RACE-05: std::atomic usage -- PASS

Properly used in:
- `loop_functions.cpp:276,301-312` -- `loraWrite`, `is_receiving`, `tx_is_active`, channel utilization
- `esp32_main.cpp:413-422` -- `receiveFlag`, `transmittedFlag`, enable flags

### RACE-06: Lock ordering documentation -- FAIL

No lock ordering documentation found anywhere in the codebase.

### RACE-07: Float tasks not pinned to core

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | t-deck-pro/peri_gps.cpp | 78 | `xTaskCreate` (not pinned) with `double` GPS coordinates |
| MEDIUM | t-deck-pro/peri_gyroscope.cpp | 14-16 | Global `float` factors accessed from unpinned task |

---

## 5. Interrupt Safety (ISR-01..04)

### ISR-01/02: ISR handlers -- MOSTLY COMPLIANT

All ESP32 ISR handlers have `ICACHE_RAM_ATTR`, GPS handler has `IRAM_ATTR`.

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | nrf52/nrf52_main.cpp | 315, 324, 333 | `interruptHandle1/2/3()` lack `IRAM_ATTR` (nRF52 may not require, but best practice) |

### ISR-03: Prohibited operations in ISR

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | t5-epaper/io_extend.c | 26 | `printf("interrupt_handler\n")` inside ISR |
| HIGH | lora_functions.cpp | 326-352 | Multiple `Serial.printf()` calls in OnRxDone callback |
| HIGH | lora_functions.cpp | 334 | `Radio.Rx()` (SPI bus access) in OnRxDone ISR |

### ISR-04: DRAM_ATTR -- PASS

ISR data uses `std::atomic<bool>` (correct for ESP32). Global volatile variables default to DRAM.

---

## 6. SPI Bus Safety (SPI-01..05)

### SPI-01: SPI bus mutex -- PARTIAL

`portMUX_TYPE displayMux` spinlock exists for display updates. `bSPI_ETH_Active` flag guards LoRa/Ethernet bus sharing.

### SPI-02: SPI access from ISR

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | lora_functions.cpp | 334 | `Radio.Rx(RX_TIMEOUT_VALUE)` called directly in OnRxDone ISR (SPI access) |

### SPI-03: CS pin management -- PASS

Proper LOW/HIGH sequencing in `Displays/BaseDisplay/hardware.cpp:28-43`.

### SPI-04: Task pinning

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | (system-wide) | -- | No explicit core pinning for LoRa/SPI tasks on dual-core ESP32 |

---

## 7. Authentication & Security

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | udp_functions.cpp | 533 | `WiFi.softAP(node_call)` -- NO WPA2 password, open AP |
| CRITICAL | safeboot/main.cpp | 68 | `WiFi.softAP(hostname)` -- NO password in safeboot mode |
| CRITICAL | nrf52/nrf52_ble.cpp | 248 | `SECMODE_OPEN` for BLE characteristics -- no encryption required |
| HIGH | web_functions/web_functions.cpp | 234 | Web password optional; empty password = open access |
| HIGH | web_functions/web_functions.cpp | 353-368 | Custom URL-param auth instead of HTTP Basic Auth (credentials in URL) |
| HIGH | esp32/esp32_main.cpp | 239 | BLE PIN hardcoded to `000000` |
| MEDIUM | safeboot/ElegantOTA.cpp | 21-48 | OTA authentication optional; no firmware signature verification |

**Note:** nrf52_ble.cpp:133 shows `SECMODE_ENC_WITH_MITM` was **commented out** (2025-04-28).

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
| CRITICAL | esp32/esp32_main.cpp | 1256-1413 | 8x `while(true)` on radio config errors (setOutputPower, setCRC, etc.) |
| CRITICAL | t-deck-pro/peri_lora.cpp | 48, 54, 60, 66 | 4x `while(true)` on radio state errors |

### Queue overflow handling

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | loop_functions.cpp | 444-481 | Ring buffer overflow silently wraps/overwrites without production warning |

**Compliant areas:**
- `bmx280.cpp:176` -- checks `begin()` return
- `ina226_functions.cpp:27` -- checks `begin()` return

---

## 9. Watchdog & Recovery (STAB-01..05)

### STAB-01/02: Task watchdog -- NOT CONFIGURED

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | (entire codebase) | -- | Zero calls to `esp_task_wdt_add()` or `esp_task_wdt_reset()` |

### STAB-03: Busy-wait loops

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | Regexp.cpp | 161 | Recursive regex matching loop without yield, can run unbounded |

### STAB-04: Reset reason logging

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | (entire codebase) | -- | No `esp_reset_reason()` call at startup |
| MEDIUM | (entire codebase) | -- | No persistent crash counter / safe mode logic |

### STAB-05: millis() wraparound bugs -- CRITICAL

Wrong pattern `(start + timeout) < millis()` found in 13+ locations (fails after 49.7 days):

| Severity | File | Line | Pattern (WRONG) |
|----------|------|------|-----------------|
| CRITICAL | mheard_functions.cpp | 160 | `lastsaveMHEARDPersistence + 30000 > millis()` |
| CRITICAL | mheard_functions.cpp | 192 | `lastsavePATHPersistence + 30000 > millis()` |
| CRITICAL | esp32/esp32_main.cpp | 2333 | `(rtc_refresh_timer + 60000) > millis()` |
| CRITICAL | esp32/esp32_main.cpp | 2400 | `(posinfo_timer + interval) < millis()` |
| CRITICAL | esp32/esp32_main.cpp | 3301 | `(web_timer + 1000) < millis()` |
| CRITICAL | nrf52/nrf52_main.cpp | 1695, 1698 | `(posinfo_timer + interval) < millis()` |
| CRITICAL | lora_functions.cpp | 1574 | `millis() > track_to_meshcom_timer + interval` |

**Correct pattern** (used in some places): `(uint32_t)(millis() - start) >= interval`
Example: `lora_functions.cpp:1135` -- correct.

---

## 10. Compiler & Build Safety (COMP-01..05)

### COMP-01: Build flags

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | platformio.ini | 130, 157, 197 | `-Wall -Wextra` present but `-Werror` MISSING -- warnings not treated as errors |

### COMP-05: Library version pinning

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | platformio.ini | 50-71 | Multiple libraries use `^` (e.g., `ArduinoJson@^7.4.3`) -- allows minor updates |
| HIGH | platformio.ini | 110 | `espressif32@^6.13.0` -- platform version not exact |
| HIGH | variants/t_deck_pro/platformio.ini | 75 | RadioLib `@7.1.2` vs main `@7.6.0` -- version mismatch |

### COMP-02..04 -- PASS

No `NDEBUG` found. No side effects in `assert()`. Switch statements have `default:` cases.

---

## 11. Type Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | nrf52/nrf_eth.cpp | 274 | `(uint8_t)lora_tx_msg_len` -- narrowing `uint16_t` to `uint8_t` without range check |
| MEDIUM | (multiple files) | -- | `snprintf` return value (`int`, can be negative) never checked before use as offset |

**No `__attribute__((packed))` found on protocol structs** -- structs use String members (packing not applicable, but wire-format parsing should use field-by-field decode).

---

## 12. Lifetime Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | web_functions/web_functions.cpp | 208, 485 | `web_client.stop()` without resetting object -- potential use-after-close |
| MEDIUM | web_functions/web_functions.cpp | 238-243 | Session table slots only cleared on 4-hour timeout, not on abrupt disconnect |

**No AsyncTCP race conditions** -- current web implementation is synchronous.

---

## 13. Logging Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | lora_functions.cpp | 326-352 | `Serial.printf()` calls in OnRxDone ISR (blocking I/O in ISR) |
| MEDIUM | t5-epaper/io_extend.c | 26 | `printf()` inside ISR handler |

**Compliant:** All log format strings appear to be string literals (no variable format strings detected).

---

## 14. Design Patterns

### CSMA not a pure function

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | lora_functions.cpp | 1914-1950 | `csma_compute_timeout()` depends on global ring buffer state; `csma_reset()` modifies global counters -- not testable in isolation |

### Callback safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | esp32/esp32_main.cpp | 1331, 1367, 1395 | `radio.setDio1Action(callback)` -- return value not checked |

### Static allocation audit -- PASS (except spectral_scan.cpp)

Ring buffers and packet processing paths use compile-time sized static arrays.

---

## 15. Protocol Correctness

### FCS checked AFTER field extraction

| Severity | File | Lines | Finding |
|----------|------|-------|---------|
| CRITICAL | aprs_functions.cpp | 179-395 | Fields extracted via String::concat (lines 192-344) BEFORE FCS validation (lines 370-379) |

### Frame size validation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| HIGH | aprs_functions.cpp | 134 | Only minimum (16 bytes) checked; no maximum frame size validation |

### Parser instance isolation

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | aprs_functions.cpp | 120 | Single `aprsmsg` struct parameter reused for LoRa and UDP -- no isolation if concurrent |

---

## 16. State Machine & Session Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | web_functions/web_functions.cpp | 38-39 | Session table (10 slots) accessed without mutex |
| MEDIUM | web_functions/web_functions.cpp | -- | No per-IP rate limiting on login attempts (brute-force possible) |

---

## 17. Data Drift Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | t5-epaper/nvs_param.cpp | 15-47 | No NVS schema version field -- firmware updates with changed struct won't detect stale data |

**Compliant:** Constants centralized in `configuration_global.h` (single source of truth).

---

## 18. TCP/Web/SSE Safety

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | web_functions/web_functions.cpp | 31, 340 | `String web_header` -- unbounded concatenation of HTTP request data (heap exhaustion via long request) |
| CRITICAL | web_functions/web_functions.cpp | 1856, 1827, 1889 | `indexOf()` return not checked before `substring()` -- -1 causes garbage extraction |
| HIGH | web_functions/web_functions.cpp | 265 | Fixed 10-slot session table with no LRU eviction -- legitimate users blocked when full |
| MEDIUM | web_functions/web_functions.cpp | -- | No Content-Length validation or JSON body size limiting on REST endpoints |

---

## 19. Test & Fuzz Readiness

| Severity | File | Line | Finding |
|----------|------|------|---------|
| MEDIUM | test/ | -- | Only `compress_functions.cpp/h` in test directory; no unit test framework, no parser tests, no fuzz targets |

**Compliant:** `decodeAPRS()` accepts `(uint8_t*, uint16_t, aprsMessage&)` -- suitable signature for fuzzing.

---

## 20. Stack Safety (STK-01..04)

| Severity | File | Line | Finding |
|----------|------|------|---------|
| CRITICAL | (project root) | -- | No `sdkconfig` with stack overflow checking level 2 |
| HIGH | (entire codebase) | -- | No `uxTaskGetStackHighWaterMark()` calls -- no runtime stack monitoring |
| MEDIUM | platformio.ini | -- | `-fstack-usage` compiler flag not enabled |

**Compliant:** All `xTaskCreate()` calls use byte sizes correctly (e.g., `1024 * 3`).

---

## Top 10 Priority Fixes

### 1. WiFi AP password protection (Section 7 -- CRITICAL)
- `udp_functions.cpp:533` and `safeboot/main.cpp:68`
- Add WPA2 password: `WiFi.softAP(ssid, password)`
- Impact: Open AP allows anyone in RF range to connect

### 2. millis() wraparound (STAB-05 -- CRITICAL, 13+ locations)
- Replace `(start + timeout) < millis()` with `(uint32_t)(millis() - start) >= timeout`
- Affects mheard, position reporting, web server, RTC refresh
- Silent failure after 49.7 days of uptime

### 3. sprintf -> snprintf (BND-01 -- CRITICAL, 60+ locations)
- Replace all `sprintf()` with `snprintf()` including correct buffer size
- Priority: `AT_PRINTF` macro (at_cmd.h), esp32_main.cpp, esp32_functions.cpp
- Risk: Stack/heap corruption on oversized input

### 4. FCS validation before field parsing (Section 15 -- CRITICAL)
- `aprs_functions.cpp:370-379` -- move FCS check before field extraction at line 179
- Currently processes untrusted data before validating integrity

### 5. Task watchdog configuration (STAB-01/02 -- CRITICAL)
- Add `esp_task_wdt_add()` and `esp_task_wdt_reset()` for all long-lived tasks
- Replace `while(true)` deadlocks in esp32_main.cpp with `esp_restart()`

### 6. String allocation in ISR (MEM-02 -- CRITICAL)
- `lora_functions.cpp:454` -- replace `charBuffer_aprs()` String return with fixed char buffer
- Remove all `String` operations from OnRxDone path

### 7. BLE security (Section 7 -- CRITICAL)
- `nrf52_ble.cpp:248` -- restore `SECMODE_ENC_WITH_MITM` (was commented out)
- `esp32_main.cpp:239` -- make BLE PIN configurable, not hardcoded `000000`

### 8. memcpy length validation (BND-03 -- CRITICAL)
- `nrf52_ble.cpp:293` -- validate BLE data size before struct copy
- `phone_commands.cpp:226+` -- validate all array indices before access

### 9. Web header buffer limiting (Section 18 -- CRITICAL)
- `web_functions.cpp:340` -- bound `web_header` String concatenation to max 4-8 KB
- Reject oversized HTTP requests to prevent heap exhaustion

### 10. Memory leak in spectral scan (MEM-03 -- CRITICAL)
- `spectral_scan.cpp:108,188` -- replace `new[]` with static buffer or ensure `delete[]`

---

## Files Most Affected

| File | Findings | Highest Severity |
|------|----------|-----------------|
| lora_functions.cpp | 14 | CRITICAL |
| esp32/esp32_main.cpp | 12 | CRITICAL |
| web_functions/web_functions.cpp | 10 | CRITICAL |
| loop_functions.cpp | 8 | CRITICAL |
| nrf52/nrf52_ble.cpp | 5 | CRITICAL |
| phone_commands.cpp | 5 | CRITICAL |
| aprs_functions.cpp | 4 | CRITICAL |
| nrf52/nrf52_main.cpp | 6 | HIGH |
| spectral_scan.cpp | 2 | CRITICAL |
| t-deck/lv_obj_functions.cpp | 1 | HIGH |
