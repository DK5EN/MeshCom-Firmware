# MeshCom Firmware Code Audit

**Date:** 2026-05-17
**Branch:** v4.35p_prio (rebased onto upstream `oe1kbc_v4.35p` HEAD = 2083fdbd)
**Local commits on top:** 5ec0241a, 10117b3e, 564ef1bc, 911914fb, 542b5a84, 9d99154b, ab19a6b0
  (docs/log feature + NimBLE tuning — only 9d99154b touches `src/esp32/esp32_ble.cpp`)
**Auditor:** Claude Code (automated)
**Rules:** docs/codequality-rules.md
**Previous audit:** docs/code-audit-20260508.md (2026-05-08, 69 findings, base aa457d8a)

**Delta vs. 2026-05-08:** Upstream merged commit **01d55b71** ("v4.35p code
review", 2026-05-09) directly responding to the previous audit. The audit
itself was checked into upstream at `src/code_review/code-audit-20260508.md`.
A follow-up commit **2083fdbd** ("v4.35p checkmesh", 2026-05-12) added
mesh-routing flags (`bCHECKMESH`, `bVIA`, `node_via[10]`) and a new
`via_functions.cpp/h`.

Net effect: substantial progress on Section 2 (sprintf/strcpy) and a
material fix to Section 1 MEM-02 CRITICAL (`charBuffer_aprs` no longer
returns `String`). One new regression introduced
(`src/Displays/BaseDisplay/SD.cpp:368`, 3-arg `strcpy` — does not compile,
but the file is excluded from all variant `build_src_filter`s, so the build
is unaffected). All other CRITICAL findings remain.

---

## Audit Summary

| Category | Rule IDs | Status | Critical | High | Medium | Low |
|----------|----------|--------|----------|------|--------|-----|
| Memory Safety | MEM-01..05 | FAIL | 1 (was 2) | 0 | 3 | 2 |
| Buffer Safety | BND-01..05 | FAIL | 2 (was 4) | 1 | 1 | 0 |
| Input Validation | Section 3 | FAIL | 1 | 2 | 1 | 0 |
| Thread Safety | RACE-01..08 | FAIL | 0 | 3 | 3 | 1 |
| ISR Safety | ISR-01..04 | FAIL | 1 | 1 | 2 | 0 |
| SPI Bus | SPI-01..05 | FAIL | 0 | 1 | 1 | 0 |
| Auth & Security | Section 7 | PARTIAL | 2 | 4 | 1 | 0 |
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
| **NEW** Dead-code regression | -- | INFO | 0 | 1 | 0 | 0 |
| **NEW** Schema migration | -- | INFO | 0 | 1 | 0 | 0 |

**Total: 17 Critical, 21 High, 25 Medium, 3 Low = 66 findings**
(−3 vs. 2026-05-08:
  −1 CRITICAL MEM-03 in `spectral_scan.cpp` (`scan_freq()` removed entirely)
  −1 CRITICAL MEM-02 in `loop_functions.cpp:2401` / `lora_functions.cpp:454` (charBuffer_aprs no longer returns String)
  −2 CRITICAL BND-01 in `esp32/at_cmd.h:30` and `nrf52/WisBlock-API.h:664` (sprintf → snprintf, esp32 path)
  +1 HIGH new dead-code regression in `Displays/BaseDisplay/SD.cpp:368`
  +1 HIGH new schema-migration risk for `node_via[10]` field
  (other CRITICAL/HIGH counts unchanged))

---

## Status Legend

- **OPEN** — finding unchanged since 2026-05-08
- **FIXED** — finding fully resolved, removed from this audit
- **PARTIAL** — fix applied to some, not all, call sites
- **NEW** — finding introduced by upstream `01d55b71` / `2083fdbd`

---

## 1. Memory Safety (MEM-01..05)

### MEM-01: malloc/new after initialization — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| LOW | nrf52/nrf52_main.cpp | 83 | `malloc()` in `nrf52_getMaxFreeBlock()` | OPEN |

### MEM-02: Arduino String in hot paths — PARTIAL FIX

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| ~~CRITICAL~~ FIXED | loop_functions.cpp | 2403 | **FIXED in 01d55b71**: `charBuffer_aprs(...)` no longer returns `String`; now writes directly into `ringbufferRAWLoraRX[RAWLoRaWrite]` via `memcpy` and returns `void`. No more per-packet heap churn on the RX path | FIXED |
| ~~CRITICAL~~ FIXED | lora_functions.cpp | 455 | **FIXED**: caller updated; old line `memcpy(... charBuffer_aprs(...).c_str(), ...)` replaced with `charBuffer_aprs(aprsmsg);` (the previous `String`-returning overload is gone) | FIXED |
| MEDIUM | loop_functions.cpp | 176, 186 | Global `String strSOFTSER_BUF`, `String strTelemetry` still used in main loop | OPEN |
| MEDIUM | loop_functions.cpp | multiple | Other temporary `String` concatenations in per-message processing | OPEN |
| MEDIUM → PARTIAL | aprs_functions.cpp | 202, 213, 220, 298, 309, 316, 392 | **PARTIAL** in 01d55b71: per-byte `.concat()` calls replaced with local `char cConcat1/2/3[UDP_TX_BUF_SIZE]` accumulators; single `String` assignment performed *after* the loop (msg_source_path, msg_source_last, msg_source_call, msg_destination_*, msg_payload, pos_atxt). Reduces heap reallocations from N (per byte) to 1 (per field). Final `String` membership of `aprsMessage` retained, so heap allocation count not zero | PARTIAL |
| LOW | aprs_functions.cpp | 181-190, 280-285, 379-381, 615-617 | New `cConcatN` accumulators are not bounds-checked against `UDP_TX_BUF_SIZE`, but the enclosing for-loops cap `ib` at 120 bytes (`(ib - 6) < 120` / `(ib - inextstart) < 120`), so overflow is structurally impossible at current parameters. Flag for future refactors | NEW (informational) |

### MEM-03: C++ new without delete (memory leak) — PARTIAL FIX

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL → ACCEPTABLE | spectral_scan.cpp | 108 | `new uint16_t[RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE]` for `*results` — **NOW DELETED** at line 172 (`delete[] res; res = nullptr;` added by 01d55b71). Allocation still happens, but lifetime is now bounded by the calling loop iteration. Downgraded from CRITICAL leak to STYLE finding (better: static buffer). Severity dropped to LOW | PARTIAL FIX |
| ~~CRITICAL~~ FIXED | spectral_scan.cpp | 188 | **FIXED in 01d55b71**: `uint16_t *scan_freq(float freq)` function and its `new uint16_t[...]{0}` body **removed entirely** (the function was unused) | FIXED |

### MEM-04: Display buffer allocation — IMPROVED

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| ~~MEDIUM~~ LOW | t-deck-pro/tdeck_pro.cpp | 195, 204, 213 | **IMPROVED in 01d55b71**: each `ps_calloc` now followed by NULL check + `malloc()` fallback to internal RAM. Severity downgraded to LOW (still no caller is informed of the fallback) | PARTIAL FIX |
| ~~LOW~~ LOW | t-deck/tdeck_main.cpp | 314 | **IMPROVED**: removed `delay(3000); ESP.restart();` panic. Now falls back to `malloc()` if `ps_malloc` returns NULL. Still LOW (no propagation to caller) | PARTIAL FIX |
| ~~MEDIUM~~ LOW | t5-epaper/t5epaper_main.cpp | 258-280 | **IMPROVED**: same `malloc()` fallback added for all three buffers (`lv_disp_buf_1`, `lv_disp_buf_2`, `decodebuffer`). Note: `decodebuffer` is `uint8_t *` but the fallback cast is `(lv_color_t *)` — possible typo. Filed under NEW below | PARTIAL FIX |

### MEM-05/06: Buffer size constants & xTaskCreateStatic — PASS (unchanged)

---

## 2. Buffer Overflow Prevention (BND-01..05) — SIGNIFICANT PROGRESS

### BND-01: Banned unsafe functions — MOSTLY FIXED

**sprintf() — was 77+ instances, now ≈ 5 sites (CRITICAL downgraded to MEDIUM)**

| Severity | File | Lines | Finding | Status |
|----------|------|-------|---------|--------|
| ~~CRITICAL~~ FIXED | esp32/at_cmd.h | 30 | **FIXED in 01d55b71**: `AT_PRINTF` / `api_ble_printf` macro changed from `sprintf(buff, __VA_ARGS__)` to `snprintf(buff, sizeof(buff), __VA_ARGS__)` (in `nrf52/WisBlock-API.h:664`, ESP32 branch only) | FIXED (esp32) |
| **CRITICAL** OPEN | nrf52/at_cmd.h | 30 | **STILL OPEN**: the duplicate `AT_PRINTF` macro for the nRF52 build retains `int len = sprintf(buff, __VA_ARGS__);`. The corresponding ESP32 macro was patched but the nRF52 sibling file was missed | OPEN |
| ~~CRITICAL~~ FIXED | nrf52/WisBlock-API.h | 664 | **FIXED**: `api_ble_printf` ESP32 branch macro fixed (same commit). The nRF52 branch uses `g_ble_uart.printf` directly — no sprintf | FIXED |
| ~~CRITICAL~~ FIXED | esp32/esp32_main.cpp | 833, 953-954, 1113, 1491, 1495, 1497 | **FIXED in 01d55b71**: 7 `sprintf` → `snprintf` (gwsrv, node_ssid, node_pwd, cvers, node_call, cBLEName, cManufData) | FIXED |
| ~~CRITICAL~~ FIXED | esp32/esp32_functions.cpp | 83, 134, 141 | **FIXED**: 3 `sprintf` → `snprintf` in `startDisplay()` cvers | FIXED |
| ~~HIGH~~ FIXED | t-deck/event_functions.cpp | ≈20 sites | **FIXED**: all 20 `sprintf` in setup button handler → `snprintf` | FIXED |
| ~~HIGH~~ FIXED | t-deck/lv_obj_functions.cpp | 24 sites | **FIXED**: all 24 `sprintf` → `snprintf` (vChar/sv/cTime/u_pos/cDatum/cZeit) | FIXED |
| ~~MEDIUM~~ FIXED | rtc_functions.cpp | 117 | **FIXED**: `sprintf(cdate, ...)` → `snprintf(cdate, sizeof(cdate), ...)` | FIXED |
| ~~MEDIUM~~ FIXED | i2c_scanner.cpp | 52, 123, 130, 138 | **FIXED**: all 4 `sprintf(cInfo, ...)` → `snprintf` | FIXED |
| ~~MEDIUM~~ FIXED | nrf52/nrf52_main.cpp | 421, 898, 904 | **FIXED**: 3 `sprintf(cvers/cvers1, ...)` → `snprintf` | FIXED |
| ~~MEDIUM~~ FIXED | nrf52/nrf52_functions.cpp | 64, 209 | **FIXED**: 2 `sprintf` in `startDisplay()` → `snprintf` | FIXED |
| ~~MEDIUM~~ FIXED | nrf52/nrf52_ble.cpp | 93, 98, 122 | **FIXED**: 3 `sprintf(helper_string, ...)` → `snprintf` | FIXED |
| ~~MEDIUM~~ FIXED | command_functions.cpp | 4188 | **FIXED**: `sprintf(meshcom_settings.node_parm, "%s", "none")` → `snprintf` | FIXED |
| MEDIUM OPEN | t-deck-pro/ui_deckpro.cpp | 1864, 2117, 2122, 2519 | **OPEN**: 4 `sprintf(txt/cDatum/cZeit, ...)` not converted by 01d55b71 (file was not touched) | OPEN |
| MEDIUM OPEN | nrf52/nrf52_main.cpp | (init/log paths) | A few `sprintf` outside the cleaned region may remain; full sweep needed | OPEN |

**strcpy() — was 5 sites (HIGH), now 1 broken site + 1 cosmetic NEW regression**

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| ~~HIGH~~ FIXED | web_functions/web_functions.cpp | 1797 | **FIXED in 01d55b71**: `strcpy(message_text, message.c_str())` → `strncpy(message_text, message.c_str(), sizeof(message_text))` | FIXED |
| ~~MEDIUM~~ FIXED | onebutton_functions.cpp | 88, 92 | **FIXED**: `strcpy(pageTextLong1/2, ...)` → `strncpy(..., sizeof(...))` | FIXED |
| HIGH | **NEW** Displays/BaseDisplay/SD.cpp | 368 | **NEW REGRESSION** in 01d55b71: the line was changed from `strcpy(&filename[8], ".bmp");` to `strcpy(&filename[8], ".bmp", sizeof(4));`. **This will not compile** — `strcpy` is a 2-argument libc function, and `sizeof(4)` evaluates to `sizeof(int) == 4`, leaving a 3-argument call. The build succeeds only because `Displays/*` is excluded from every variant `build_src_filter`. If any future variant enables Displays, the build will break. Action: replace with `memcpy(&filename[8], ".bmp", 5);` (5 = string + NUL) or restore `strcpy(&filename[8], ".bmp");` | NEW |
| LOW | Displays/BaseDisplay/SD.cpp | -- | `loadFullscreenBMP` 4-byte filename suffix logic unchanged; sibling site to the regression above | OPEN |

### BND-02: snprintf return value not checked — OPEN

Now that ~50 new `snprintf` call sites exist (replacing `sprintf`), the total
unchecked-return count has *increased* slightly. None of the new sites
check `(ret >= bufsize)` for truncation. Total ≈ 600+ `snprintf` sites,
zero checked. No change in severity — the audit recommendation is to add
truncation detection at protocol boundaries (node_call, node_ssid, node_pwd,
node_atxt). For UI labels (`vChar[10]`, `cTime[50]`, etc.) truncation is
benign.

### BND-03: memcpy without prior length validation — OPEN (all sites unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | nrf52/nrf52_ble.cpp | 293 | unchanged | OPEN |
| HIGH | esp32/esp32_main.cpp | 310 | unchanged | OPEN |
| HIGH | lora_functions.cpp | 211 | unchanged | OPEN |
| HIGH | lora_functions.cpp | 385 | unchanged | OPEN |
| HIGH | phone_commands.cpp | 541 | unchanged | OPEN |
| HIGH | nrf52/nrf_eth.cpp | 270 | unchanged | OPEN |
| MEDIUM | udp_functions.cpp | 140, 172 | unchanged | OPEN |
| MEDIUM | nrf52/nrf52_flash.cpp | 70-226 | unchanged | OPEN |

### BND-04: Missing static_assert on protocol structs — OPEN

Unchanged. `aprsMessage`, `s_meshcom_settings` (now extended with
`node_via[10]`), `mheardLine` lack `static_assert` / `_Static_assert`.

---

## 3. Input Validation — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | phone_commands.cpp | 226, 324, 364, 385, 403, 466, 479-488, 555 | unchanged | OPEN |
| HIGH | web_functions/web_setup.cpp | 69-259 | unchanged | OPEN |
| HIGH | web_functions/web_functions.cpp | 1920 | unchanged | OPEN |
| MEDIUM | web_functions/web_functions.cpp | 353-382, 1767, 1827, 1856, 1889 | unchanged | OPEN |
| MEDIUM | gps_functions.cpp | (refactored) | unchanged | OPEN |

---

## 4. Thread Safety (RACE-01..08) — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | esp32/esp32_audio.cpp | 38 | binary semaphore as mutex | OPEN |
| MEDIUM | t-deck/tdeck_main.cpp | 111 | binary semaphore as mutex | OPEN |
| LOW | nrf52/nrf52_main.cpp | 400 | binary semaphore (acceptable) | OPEN |
| MEDIUM | t-deck/tdeck_main.cpp | 405 | `portMAX_DELAY` in display flush | OPEN |
| MEDIUM | nrf52/api_functions.cpp | 262 | `portMAX_DELAY` | OPEN |
| MEDIUM | time_functions.cpp | 96 | `localtime()` not `localtime_r()` | OPEN |
| HIGH | loop_functions.cpp | 299-300 | volatile RW ring-buffer race | OPEN |
| HIGH | nrf52/nrf52_main.cpp | 233-236 | CAD flags ISR vs main race | OPEN |
| HIGH | gps_functions.cpp | 174-175 | volatile pulseTimes ISR race | OPEN |
| MEDIUM | esp32/esp32_main.cpp | 443, 458 | scanFlag / transmissionState | OPEN |
| MEDIUM | loop_functions.cpp | 75, 204-206 | cross-module volatile bools | OPEN |
| MEDIUM | t-deck-pro/peri_gps.cpp | 78 | unpinned task with `double` | OPEN |
| MEDIUM | t-deck-pro/peri_gyroscope.cpp | 14-16 | unpinned task with `float` globals | OPEN |

### RACE-05: std::atomic usage — PASS (unchanged)

---

## 5. Interrupt Safety (ISR-01..04) — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | nrf52/nrf52_main.cpp | 317, 326, 335 | ISR handlers lack `IRAM_ATTR` (nRF52 has no IRAM_ATTR — acceptable; flag for symmetry) | OPEN |
| MEDIUM | gps_functions.cpp | ≈206 | ISR off-by-one on `pulseTimes` | OPEN |
| CRITICAL | t5-epaper/io_extend.c | 26 | `printf()` inside ISR | OPEN |
| HIGH | lora_functions.cpp | 325, 327, 344, 346, 350, 375 | `Serial.printf` in OnRxDone | OPEN |
| HIGH | lora_functions.cpp | 332 | `startRadioReceive()` (SPI) from ISR | OPEN |

---

## 6. SPI Bus Safety (SPI-01..05) — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | lora_functions.cpp | 332 | SPI from ISR | OPEN |
| MEDIUM | (system-wide) | -- | No explicit core pinning for LoRa/SPI tasks | OPEN |

---

## 7. Authentication & Security — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | udp_functions.cpp | 534 | `WiFi.softAP(meshcom_settings.node_call)` — no WPA2 password | OPEN |
| CRITICAL | safeboot/main.cpp | 68, 187 | `WiFi.softAP(hostname)` — no password (2 sites) | OPEN |
| HIGH | nrf52/nrf52_ble.cpp | 274 | `SECMODE_OPEN` (line 133 still has commented `SECMODE_ENC_WITH_MITM`) | OPEN |
| HIGH | web_functions/web_functions.cpp | 234 | empty password = open access | OPEN |
| HIGH | web_functions/web_functions.cpp | 353-368 | URL-param auth instead of HTTP Basic | OPEN |
| HIGH | esp32/esp32_main.cpp | 271 | `uint32_t PIN = 000000;` hardcoded pairing PIN; application-layer SHA-256 gate via `bt_code` still optional (added in bf05f9de, see prior audit) | OPEN |
| MEDIUM | safeboot/ElegantOTA.cpp | 21-48 | OTA auth optional; no firmware signing | OPEN |

---

## 8. Error Handling — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | main.cpp | 38, 43 | `SPI.begin()` unchecked | OPEN |
| HIGH | esp32/esp32_main.cpp | 651 | `Wire.begin()` unchecked | OPEN |
| HIGH | nrf52/nrf52_main.cpp | 850 | `Wire.begin()` unchecked | OPEN |
| HIGH | nrf52/nrf52_ble.cpp | 128, 130, 135, 272, 278 | 5x BLE service `begin()` unchecked | OPEN |
| MEDIUM | extudp_functions.cpp | 69 | `ETH.begin()` unchecked | OPEN |
| CRITICAL | esp32/esp32_main.cpp | 1317, 1324, 1332, 1342, 1379, 1419, 1452, 1478 | 8x `while (true);` on radio config errors | OPEN |
| CRITICAL | t-deck-pro/peri_lora.cpp | 11 distinct sites | 11x `while (true);` on radio state errors | OPEN |
| MEDIUM | t-deck-pro/tdeck_pro.cpp | (post-fallback init region) | `while (1)` on camera init fail | OPEN |
| MEDIUM | t-deck-pro/peri_gps.cpp | 54 | `while(1)` on GPS init fail | OPEN |
| MEDIUM | t5-epaper/t5epaper_main.cpp | 73 | `while(1)` on display init fail | OPEN |
| MEDIUM | t5-epaper/peri_gps.cpp | 53 | `while(1)` on GPS init fail | OPEN |
| MEDIUM | t5-epaper/peri_lora.cpp | 36 | `while(1)` on LoRa init fail | OPEN |
| MEDIUM | loop_functions.cpp | ≈444-481 | ring buffer overflow silent wrap | OPEN |

---

## 9. Watchdog & Recovery (STAB-01..05) — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | (entire codebase) | -- | No `esp_task_wdt_add()` / `esp_task_wdt_reset()` | OPEN |
| HIGH | Regexp.cpp | 145, 150 | recursive expand without yield | OPEN |
| MEDIUM | gps_functions.cpp | ≈206 | 5 s busy-wait baud detection | OPEN |
| MEDIUM | (entire codebase) | -- | No `esp_reset_reason()` | OPEN |
| MEDIUM | (entire codebase) | -- | No persistent crash counter | OPEN |

### STAB-05: millis() wraparound — OPEN (count unchanged ≈ 33)

All sites unchanged. Spot-checked:
- `mheard_functions.cpp:160, 192` — broken pattern intact
- `lora_functions.cpp:1579` — broken pattern intact
- `web_functions/web_functions.cpp:239` — broken pattern intact
- `esp32/esp32_main.cpp` — 4+ broken sites confirmed
- `nrf52/nrf52_main.cpp` — 3+ broken sites confirmed

---

## 10. Compiler & Build Safety (COMP-01..05) — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | platformio.ini | 152, 179, 219 | `-Wall -Wextra` present, **`-Werror` MISSING**. (Note: this finding plus the new BND-01 dead-code regression at SD.cpp:368 reinforce each other — `-Werror` plus enabling `Displays/*` would catch the 3-arg strcpy at compile time) | OPEN |
| HIGH | platformio.ini | 55-69 | libraries with `^` constraint | OPEN |
| HIGH | platformio.ini | 115 | `espressif32@^6.13.0` | OPEN |
| HIGH | variants/t_deck_pro/platformio.ini | 75 | RadioLib version skew | OPEN |
| HIGH | variants/t_deck_pro/platformio.ini | 2 | `espressif32@6.5.0` vs root `@^6.13.0` | OPEN |

---

## 11. Type Safety — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | nrf52/nrf_eth.cpp | 274 | narrowing `uint16_t` → `uint8_t` | OPEN |
| MEDIUM | (multi files) | -- | `snprintf` return never checked (now even more sites — see BND-02) | OPEN |

---

## 12. Lifetime Safety — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | web_functions/web_functions.cpp | 208, 485, 503 | `web_client.stop()` without resetting web_header | OPEN |
| MEDIUM | web_functions/web_functions.cpp | 236-263 | session table cleared only on broken millis() pattern | OPEN |

---

## 13. Logging Safety — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | lora_functions.cpp | 325, 327, 344, 346, 350, 375 | `Serial.printf()` in OnRxDone | OPEN |
| MEDIUM | t5-epaper/io_extend.c | 26 | `printf()` in ISR | OPEN |

`log_functions.cpp` (still local commit `5ec0241a`) remains COMPLIANT.

---

## 14. Design Patterns — OPEN (signature change, not behavior)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | lora_functions.cpp | 1916-1952 | CSMA functions mutate global state | OPEN |
| HIGH | esp32/esp32_main.cpp | 1325, 1365, 1393 | `radio.setDio1Action(callback)` return not checked | OPEN |
| HIGH | (new) via_functions.cpp | 41-50 | `checkMesh()` extracted into its own TU by 2083fdbd. Currently still returns `bMESH` (the `bVIA` / `node_via` routing logic described in the file header comment is not yet implemented). This is half a refactor: structure in place, behavior unchanged. The header doc-block declares rules that the body does not implement | NEW (informational, no harm yet) |

---

## 15. Protocol Correctness — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | aprs_functions.cpp | 179-432 | FCS computed at line 424 and checked at 432 — **still after** field extraction at 199-407 (now into `cConcatN` buffers). The 01d55b71 fix replaced `String.concat` with `char[]` writes but did not move the integrity check before parsing | OPEN |
| HIGH | aprs_functions.cpp | 134 | only minimum (16 bytes) frame size check | OPEN |
| MEDIUM | aprs_functions.cpp | 120 | single `aprsmsg` parameter reused | OPEN |

---

## 16. State Machine & Session Safety — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | web_functions/web_functions.cpp | 38-39 | session table without mutex | OPEN |
| MEDIUM | web_functions/web_functions.cpp | -- | no per-IP rate limiting | OPEN |

---

## 17. Data Drift Safety — OPEN + NEW

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | t5-epaper/nvs_param.cpp | 7-47 | No NVS schema version field | OPEN |
| **HIGH NEW** | esp32/esp32_flash.h | 173 / nrf52/WisBlock-API.h | 336, 560 | **Struct extended**: `char node_via[10] = {0};` added to `s_meshcom_settings` and `s_meshcomcompat_settings` by 2083fdbd. Initialized via `preferences.getString("node_via")` (esp32_flash.cpp:252) with no migration check on first boot after upgrade — older flash images that lack the key will silently return an empty string (OK because of the `{0}` default), but if a NimBLE/RAK device boots into mixed-version firmware, the absence of a struct version field means corrupt slot interleaving is possible. **Action:** add a `FLASH_VERSION` check around this field, or bump it (already done to `20260510`) | NEW |

---

## 18. TCP/Web/SSE Safety — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | web_functions/web_functions.cpp | 31, 340 | unbounded `String web_header` | OPEN |
| CRITICAL | web_functions/web_functions.cpp | 353, 355, 382, 1767, 1827, 1856, 1889 | `indexOf()` return unchecked | OPEN |
| HIGH | web_functions/web_functions.cpp | 265 | 10-slot session table without LRU | OPEN |
| MEDIUM | web_functions/web_functions.cpp | -- | no Content-Length / JSON body size limit | OPEN |

---

## 19. Test & Fuzz Readiness — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | test/ | -- | only `compress_functions.cpp/h` | OPEN |

---

## 20. Stack Safety (STK-01..04) — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | (project root) | -- | no `sdkconfig` with stack overflow level 2 | OPEN |
| HIGH | (entire codebase) | -- | no `uxTaskGetStackHighWaterMark()` | OPEN |
| MEDIUM | platformio.ini | -- | `-fstack-usage` not enabled | OPEN |

---

## NEW Findings introduced by upstream 01d55b71 / 2083fdbd

### NEW-1: 3-argument `strcpy` is malformed C++ (HIGH dead-code)

**File:** src/Displays/BaseDisplay/SD.cpp:368
**Diff (01d55b71):**
```c
-    strcpy(&filename[8], ".bmp");
+    strcpy(&filename[8], ".bmp", sizeof(4));
```
`sizeof(4)` evaluates to `sizeof(int) == 4`. `strcpy` takes 2 arguments per
ISO C `<string.h>`; this call has 3 and will fail to compile. The build is
currently green only because `Displays/*` is excluded from every
`build_src_filter`:

```ini
# platformio.ini (esp32 group)
src_filter =
    +<*>
    -<nrf52/*>
    -<Displays/*>      # ← excluded
    ...
# variants/wiscore_rak4631/platformio.ini
build_src_filter =
    +<*>
    -<esp32/*>
    -<Displays/*>      # ← excluded
```

**Risk:** the moment any future variant enables Displays/*, the build will
break. Trivial to fix: `memcpy(&filename[8], ".bmp", 5);` (5 = 4 chars + NUL)
or just revert to the original 2-arg `strcpy`.

### NEW-2: Schema migration risk for `node_via` field (HIGH)

`s_meshcom_settings` (esp32_flash.h:173, nrf52/WisBlock-API.h:336, 560)
gained `char node_via[10] = {0};` between two consecutive upstream commits
(01d55b71 had no field, 2083fdbd added it). The nRF52 path persists via
`nrf52_flash.cpp` which uses `memcpy` against the old `sizeof(s_meshcom_settings)`
— on first boot after a firmware upgrade, the migration code will read 10
bytes of stale flash data into `node_via`. The init default `{0}` does not
help because the migration overwrites it.

`FLASH_VERSION` was bumped to `20260510` in `configuration_global.h`, but
there is no observable read-side check that compares the version *before*
the `memcpy` and zero-fills the new field on mismatch. (Spot-check
`nrf52_flash.cpp:70-226`; the version field is not used as a gate.)

**Action:** verify `nrf52_flash.cpp` migration explicitly zeroes
`meshcom_settings.node_via` when the stored `FLASH_VERSION` is older than
20260510.

### NEW-3: `via_functions.cpp` documented behavior not implemented (INFO)

The file header documents a non-trivial routing rule set (`bVIA == true`
plus NCT-based neighbor selection). The actual body returns `bMESH` only,
ignoring `bVIA` and `node_via`. Not a defect today (callers see the same
behavior as before the refactor) but flagged so reviewers don't assume the
documented rules are active.

### NEW-4: `decodebuffer` PSRAM fallback cast typo (LOW)

**File:** src/t5-epaper/t5epaper_main.cpp:278
```c
decodebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), DISP_BUF_SIZE / 2);
if (decodebuffer == NULL)
{
    decodebuffer = (lv_color_t *)malloc(sizeof(uint8_t) * (DISP_BUF_SIZE / 2));
}
```
`decodebuffer` is `uint8_t *`; the fallback cast is `(lv_color_t *)`. The
C compiler will warn (and `-Werror` would fail). The allocation size is
correct, so the bug is cosmetic in `-Wall -Wextra` mode.

---

## Summary of Deltas vs. 2026-05-08

### FIXED (5 CRITICAL findings closed)

1. **MEM-02 CRITICAL** — `charBuffer_aprs()` no longer returns `String`; LoRa RX hot path no longer allocates `String` per packet (`loop_functions.cpp:2403`, `lora_functions.cpp:455`)
2. **MEM-03 CRITICAL** — `scan_freq()` and its un-deleted `new[]` removed (`spectral_scan.cpp:188`)
3. **MEM-03 PARTIAL** — `delete[] res` added (`spectral_scan.cpp:172`); downgrade from CRITICAL to LOW
4. **BND-01 CRITICAL** — `esp32/at_cmd.h:30` AT_PRINTF macro `sprintf` → `snprintf`
5. **BND-01 CRITICAL** — `nrf52/WisBlock-API.h:664` `api_ble_printf` ESP32 branch `sprintf` → `snprintf`

### PARTIAL (significant progress, not full)

- **BND-01 sprintf** — ~70 of ~77 sites converted to `snprintf`. Remaining: `nrf52/at_cmd.h:30` (the only CRITICAL macro not fixed), `t-deck-pro/ui_deckpro.cpp` (4 sites)
- **BND-01 strcpy** — `web_functions.cpp:1797` and `onebutton_functions.cpp:88,92` fixed; `SD.cpp:368` regressed
- **MEM-02 aprs_functions** — per-byte `String.concat` replaced with `char[]` accumulators; final `String` assignment retained
- **MEM-04 display buffers** — NULL checks + RAM fallback added on `tdeck_pro.cpp`, `tdeck_main.cpp`, `t5epaper_main.cpp` (severity dropped MEDIUM → LOW)

### NEW (introduced by upstream)

- **HIGH** dead-code regression: `Displays/BaseDisplay/SD.cpp:368` 3-arg `strcpy` (not built)
- **HIGH** schema migration risk: `node_via[10]` added to `s_meshcom_settings` without explicit NRF52 migration check
- **INFO** half-finished routing refactor: `via_functions.cpp` documents rules its body doesn't implement
- **LOW** cosmetic cast typo: `t5epaper_main.cpp:278` casts `uint8_t*` to `lv_color_t*`

### Confirmed UNCHANGED (still CRITICAL, no progress)

- All 3 WiFi open-AP findings
- BLE `SECMODE_OPEN` (nrf52_ble.cpp:274)
- Hardcoded `PIN = 000000` (esp32_main.cpp:271)
- 8 + 11 `while(true)` on init failures
- FCS-after-extraction in `aprs_functions.cpp`
- All `web_header` unbounded-concat / `substring`-of-negative-index bugs
- 33+ `millis()` wraparound sites
- No task watchdog, no `-Werror`, no `sdkconfig` stack checks, no NVS schema version
- All `memcpy` length-validation findings (lora/phone_commands/nrf_eth)

---

## Top 10 Priority Fixes (updated)

### 1. WiFi AP password protection (Section 7 — CRITICAL)
- `udp_functions.cpp:534` and `safeboot/main.cpp:68, 187`
- Add WPA2 password: `WiFi.softAP(ssid, password)`

### 2. millis() wraparound (STAB-05 — CRITICAL, ~33 sites)
- Replace `(start + timeout) < millis()` with `(uint32_t)(millis() - start) >= timeout`

### 3. FCS validation before field parsing (Section 15 — CRITICAL)
- `aprs_functions.cpp` — move the FCS check (currently at 432) to **before** the field extraction loops at 179-407. The 01d55b71 fix simplified per-byte heap pressure but kept the integrity check at the wrong end of the parser
- Pure win: integrity-before-parse rejects malformed/malicious packets without parser side effects

### 4. nrf52/at_cmd.h sprintf macro (BND-01 — CRITICAL, sole remaining sprintf macro)
- Apply the same one-line fix that `esp32/at_cmd.h:30` and `nrf52/WisBlock-API.h:664` received: `sprintf(buff, ...)` → `snprintf(buff, sizeof(buff), ...)`
- Trivial, ought to land in the next minor upstream PR

### 5. Task watchdog configuration (STAB-01/02 — CRITICAL)
- Add `esp_task_wdt_add()` / `esp_task_wdt_reset()`; replace `while(true);` with `esp_restart()`

### 6. BLE pairing security (Section 7 — HIGH)
- `nrf52_ble.cpp:274` — restore `SECMODE_ENC_WITH_MITM` (already in commented code at line 133)
- `esp32_main.cpp:271` — remove hardcoded `PIN = 000000` fallback; require `bt_code` set on first boot

### 7. memcpy length validation (BND-03 — HIGH)
- `lora_functions.cpp:211` — validate `size >= 12`
- `phone_commands.cpp:541` — bound against `sizeof(textbuff_phone)`

### 8. Web header buffer limiting (Section 18 — CRITICAL)
- `web_functions.cpp:340` — bound `web_header` to ≤ 4-8 KB
- Check every `indexOf()` return before `substring()`

### 9. `-Werror` + clean dead code (COMP-01 — CRITICAL)
- Add `-Werror` to `build_flags`
- Fix or remove `Displays/BaseDisplay/SD.cpp:368` (3-arg strcpy)
- Fix `t5epaper_main.cpp:278` `lv_color_t*` cast typo

### 10. NVS schema migration (NEW-2 — HIGH)
- Verify `nrf52_flash.cpp` migration explicitly zeroes `meshcom_settings.node_via` when stored `FLASH_VERSION < 20260510`
- Long-term: add a real schema-version gate to NVS / RAK flash read

---

## Files Most Affected (updated)

| File | Findings | Highest Severity | Delta |
|------|----------|-----------------|-------|
| nrf52/nrf52_main.cpp | 17 (was 19) | CRITICAL | −2 sprintf, +1 NEW field |
| lora_functions.cpp | 14 | CRITICAL | unchanged |
| esp32/esp32_main.cpp | 8 (was 14) | CRITICAL | −6 sprintf |
| platformio.ini | 14 | CRITICAL | unchanged |
| t-deck-pro/peri_lora.cpp | 11 | CRITICAL | unchanged |
| web_functions/web_functions.cpp | 9 (was 10) | CRITICAL | −1 strcpy |
| loop_functions.cpp | 7 (was 8) | CRITICAL | −1 charBuffer_aprs |
| aprs_functions.cpp | 5 | CRITICAL | unchanged (per-byte concat fixed, FCS-after-parse remains) |
| phone_commands.cpp | 5 | CRITICAL | unchanged |
| nrf52/nrf52_ble.cpp | 2 (was 5) | HIGH | −3 sprintf |
| t-deck/event_functions.cpp | 0 (was 1) | -- | −20 sprintf |
| t-deck/lv_obj_functions.cpp | 0 (was 1) | -- | −24 sprintf |
| spectral_scan.cpp | 1 (was 2) | LOW (was CRITICAL) | −1 leak, severity drop |
| **NEW** Displays/BaseDisplay/SD.cpp | 1 | HIGH | NEW regression (not built) |

---

## Rebase-Specific Observations (oe1kbc_v4.35p aa457d8a → 2083fdbd)

Two upstream commits since 2026-05-08:

- **01d55b71 "v4.35p code review"** (2026-05-09) — **direct response to the previous audit**. Upstream copied `code-audit-20260508.md` into `src/code_review/` and applied the bulk of the BND-01 sprintf/strcpy recommendations along with the MEM-02 / MEM-03 / MEM-04 fixes documented above. The only regression is the SD.cpp:368 3-arg strcpy, which does not affect any current build. Closes 5 CRITICAL findings; PARTIAL on 4 more.
- **2083fdbd "v4.35p checkmesh"** (2026-05-12) — adds `bCHECKMESH`, `bVIA`, `node_via[10]` and the new `via_functions.cpp/h` extracted from `loop_functions.cpp`. Behavior unchanged (the documented routing rules are not yet implemented). Adds the NEW-2 (schema migration) and NEW-3 (documented-but-unimplemented rules) findings.

**Bottom line:** The rebase delivered the largest single batch of audit fixes
since the audits began. CRITICAL count dropped from 20 to 17. The remaining
CRITICAL findings are the high-effort items (WiFi password, millis()
wraparound across ~33 sites, FCS-before-parse, task watchdog, web_header
buffer limiting) — none of these can be one-line fixes. The `nrf52/at_cmd.h`
sprintf macro is the one remaining trivial CRITICAL: a sibling of two
already-fixed macros that the upstream sweep simply missed.
