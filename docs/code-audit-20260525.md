# MeshCom Firmware Code Audit

**Date:** 2026-05-25
**Branch:** v4.35p_prio (rebased onto upstream `oe1kbc_v4.35p` HEAD = b0c26176)
**Upstream HEAD tag:** v4.35p.05.23
**Local commits on top:** fddae071, 9ff1578a (sendExtern stack-overflow fix + platform-conditional allocation)
  (all other local commits are docs/tools — no `src/` changes)
**Auditor:** Claude Code (automated)
**Rules:** docs/codequality-rules.md
**Previous audit:** docs/code-audit-20260517.md (2026-05-17, 66 findings, base 2083fdbd)

**Delta vs. 2026-05-17:** 16 new upstream commits between 2083fdbd and b0c26176 (tags
v4.35p.05.20 and v4.35p.05.23). Activity confirms that `oe1kbc_v4.35p` / `dev` are
actively maintained again after the 2026-05-12 lull.

Key upstream changes in this window:
- **PR #954 (DH1FR/wolfSSH):** HMAC-Console replaces TLS-Console; 4 separate bug fixes
  (TX-IRQ watchdog, startTransmit error recovery, resetExternUDP, retransmit memcpy order,
  SendAckMessage prio=3)
- **b5dc2e5d (oe1kbc_v4.35p):** `log_functions.cpp/h` removed entirely; GPS functions
  refactored; net_console updated
- **b0c26176 (oe1kbc_v4.35p):** RAK4631 SSID query removed

Net effect: significant functional bug fixes (TX lockup, retransmit, ACK priority). The
net_console HMAC-auth contains a plaintext password bypass (new SECURITY finding). All
long-standing audit CRITICALs from 2026-05-17 remain open.

---

## Audit Summary

| Category | Rule IDs | Status | Critical | High | Medium | Low |
|----------|----------|--------|----------|------|--------|-----|
| Memory Safety | MEM-01..05 | FAIL | 0 | 0 | 3 | 2 |
| Buffer Safety | BND-01..05 | FAIL | 1 | 3 | 2 | 0 |
| Input Validation | Section 3 | FAIL | 1 | 2 | 2 | 0 |
| Thread Safety | RACE-01..08 | FAIL | 0 | 3 | 5 | 1 |
| ISR Safety | ISR-01..04 | FAIL | 1 | 2 | 2 | 0 |
| SPI Bus | SPI-01..05 | FAIL | 0 | 1 | 1 | 0 |
| Auth & Security | Section 7 | FAIL | 2 | 5 | 1 | 0 |
| Error Handling | Section 8 | FAIL | 1 | 2 | 5 | 0 |
| Watchdog | STAB-01..05 | PARTIAL | 1 | 1 | 2 | 0 |
| Compiler/Build | COMP-01..05 | FAIL | 1 | 4 | 0 | 0 |
| Type Safety | Section 11 | FAIL | 0 | 1 | 1 | 0 |
| Lifetime Safety | Section 12 | PARTIAL | 0 | 0 | 2 | 0 |
| Logging Safety | Section 13 | PARTIAL | 0 | 2 | 1 | 0 |
| Design Patterns | Section 14 | FAIL | 1 | 1 | 1 | 0 |
| Protocol Correctness | Section 15 | FAIL | 1 | 1 | 1 | 0 |
| State Machines | Section 16 | PARTIAL | 0 | 0 | 2 | 0 |
| Data Drift | Section 17 | FAIL | 1 | 0 | 0 | 0 |
| TCP/Web/SSE | Section 18 | FAIL | 2 | 1 | 1 | 0 |
| Test Readiness | Section 19 | FAIL | 0 | 0 | 1 | 0 |
| Stack Safety | STK-01..04 | FAIL | 1 | 1 | 1 | 0 |
| Dead-code regression | -- | INFO | 0 | 1 | 0 | 0 |

**Total: 14 Critical, 31 High, 35 Medium, 3 Low = 83 findings**

Delta vs. 2026-05-17:
- −1 CRITICAL: NEW-2 (node_via flash migration risk — compat struct confirmed to have the field at WisBlock-API.h:561, migration safe)
- −1 HIGH → CLOSED: startTransmit all 3 TX paths now have error recovery (was untracked, now resolved)
- −1 HIGH → CLOSED: retransmit memcpy order (was untracked, now resolved)
- −1 HIGH → CLOSED: SendAckMessage prio=3 (was untracked, now resolved)
- +1 HIGH: net_console.cpp:174 plaintext password bypass in HMAC auth
- +1 HIGH: net_console.cpp:171 password printed to Serial during auth
- STAB-01 upgraded from CRITICAL to PARTIAL (TX watchdog added; main-loop WDT still missing)
- Sections 8/13 finding counts adjusted for new error-handling paths and log_functions removal
- Note: absolute numbers appear higher because several previously-untracked HIGH findings from new code are now enumerated

---

## Status Legend

- **OPEN** — finding unchanged since 2026-05-17
- **FIXED** — finding fully resolved, removed from active findings
- **PARTIAL** — fix applied to some but not all, or mitigation added without closing
- **NEW** — finding introduced by new upstream commits (2026-05-18..23)
- **CLOSED** — finding confirmed resolved in this audit pass

---

## 1. Memory Safety (MEM-01..05)

Unchanged from 2026-05-17. No new memory-management commits in this window.

### MEM-01: malloc/new after initialization

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| LOW | nrf52/nrf52_main.cpp | 83 | `malloc()` in `nrf52_getMaxFreeBlock()` | OPEN |

### MEM-02: Arduino String in hot paths

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | loop_functions.cpp | 176, 186 | Global `String strSOFTSER_BUF`, `String strTelemetry` in main loop | OPEN |
| MEDIUM | loop_functions.cpp | multiple | Temporary `String` concatenations in per-message processing | OPEN |
| MEDIUM→PARTIAL | aprs_functions.cpp | 202..316 | Per-byte `.concat()` → `cConcatN[]` (from 01d55b71); final `String` membership of `aprsMessage` retained | PARTIAL |

### MEM-03: C++ new without delete

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| LOW | spectral_scan.cpp | 108 | `new uint16_t[]`, `delete[]` added at 172 — bounded lifetime, still heap | PARTIAL |

### MEM-04: Display buffer allocation

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| LOW | t-deck-pro/tdeck_pro.cpp | 195, 204, 213 | `ps_calloc` + NULL check + `malloc()` fallback, no propagation to caller | PARTIAL |
| LOW | t-deck/tdeck_main.cpp | 314 | Same pattern | PARTIAL |
| LOW | t5-epaper/t5epaper_main.cpp | 258-280 | Same; `decodebuffer` fallback cast `(lv_color_t *)` on `uint8_t *` — cosmetic typo | PARTIAL |

---

## 2. Buffer Overflow Prevention (BND-01..05)

### BND-01: Banned unsafe functions

**nrf52/at_cmd.h:30 — CRITICAL, sole remaining `sprintf` macro**

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | nrf52/at_cmd.h | 30 | `AT_PRINTF`: `int len = sprintf(buff, __VA_ARGS__);` — trivial fix missed by the upstream sweep that patched the ESP32 sibling | OPEN |
| MEDIUM | t-deck-pro/ui_deckpro.cpp | 1864, 2117, 2122, 2519 | 4x `sprintf(txt/cDatum/cZeit, ...)` not converted | OPEN |

**Displays/BaseDisplay/SD.cpp:368 — HIGH dead-code regression (unchanged)**

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | Displays/BaseDisplay/SD.cpp | 368 | `strcpy(&filename[8], ".bmp", sizeof(4))` — 3-arg `strcpy` does not compile; saved only by `Displays/*` being excluded from all `build_src_filter` | OPEN |

### BND-02: snprintf return value not checked

~600+ unchecked `snprintf` call sites. No change. Priority: check truncation at protocol boundaries
(`node_call`, `node_ssid`, `node_pwd`, `node_atxt`). UI labels are benign.

### BND-03: memcpy without prior length validation

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

### BND-04: Missing static_assert on protocol structs

`aprsMessage`, `s_meshcom_settings` (now with `node_via[10]`), `mheardLine` lack
`static_assert`. Unchanged.

---

## 3. Input Validation — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | phone_commands.cpp | 226, 324, 364, 385, 403, 466, 479-488, 555 | unchanged | OPEN |
| HIGH | web_functions/web_setup.cpp | 69-259 | unchanged | OPEN |
| HIGH | web_functions/web_functions.cpp | 1920 | unchanged | OPEN |
| MEDIUM | web_functions/web_functions.cpp | 353-382, 1767, 1827, 1856, 1889 | unchanged | OPEN |
| MEDIUM | gps_functions.cpp | various | unchanged | OPEN |

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

---

## 5. Interrupt Safety (ISR-01..04) — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | nrf52/nrf52_main.cpp | 317, 326, 335 | ISR handlers lack `IRAM_ATTR` (nRF52-acceptable; flagged for symmetry) | OPEN |
| MEDIUM | gps_functions.cpp | ≈206 | ISR off-by-one on `pulseTimes` | OPEN |
| CRITICAL | t5-epaper/io_extend.c | 26 | `printf()` inside ISR | OPEN |
| HIGH | lora_functions.cpp | 325, 327, 344, 346, 350, 375 | `Serial.printf` in OnRxDone | OPEN |
| HIGH | lora_functions.cpp | 332 | `startRadioReceive()` (SPI) from ISR | OPEN |

---

## 6. SPI Bus Safety (SPI-01..05) — OPEN (unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | lora_functions.cpp | 332 | SPI call from ISR | OPEN |
| MEDIUM | (system-wide) | -- | No explicit core pinning for LoRa/SPI tasks | OPEN |

---

## 7. Authentication & Security

### Previously open findings — all OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | udp_functions.cpp | 534 | `WiFi.softAP(meshcom_settings.node_call)` — no WPA2 password | OPEN |
| CRITICAL | safeboot/main.cpp | 68, 187 | `WiFi.softAP(hostname)` — no password (2 sites) | OPEN |
| HIGH | nrf52/nrf52_ble.cpp | 274 | `SECMODE_OPEN` (commented `SECMODE_ENC_WITH_MITM` at line 133) | OPEN |
| HIGH | web_functions/web_functions.cpp | 234 | empty password = open access | OPEN |
| HIGH | web_functions/web_functions.cpp | 353-368 | URL-param auth instead of HTTP Basic | OPEN |
| HIGH | esp32/esp32_main.cpp | 271 | `uint32_t PIN = 000000;` hardcoded BLE PIN | OPEN |
| MEDIUM | safeboot/ElegantOTA.cpp | 21-48 | OTA auth optional; no firmware signing | OPEN |

### NEW: net_console plaintext password bypass (HIGH)

**File:** `src/net_console.cpp:174`
**Introduced by:** `50d277f8` (HMAC-Console), modified by `85993122`

```c
// KBC check without SHA256
if(memcmp(respBuf, s_password, strlen(s_password)) != 0)
{
    // HMAC-SHA256 path — only reached if respBuf ≠ plaintext password
    ...
    authOk = ct_equal(expected, received, 32);
}
else
{
    authOk = true;  // respBuf == plaintext password → bypass HMAC
}
```

A client that sends the raw plaintext password (instead of the 64-char hex HMAC response)
is granted access immediately (`authOk = true` at line 195). The 16-byte nonce challenge is
completely bypassed. This defeats the stated purpose of the HMAC protocol: to prevent the
password from travelling in plaintext over the network. An adversary who captures one
plaintext-authenticated session now has the password directly.

The comment `// KBC check without SHA256` confirms this is intentional — intended as a
convenience fallback for `nc` sessions — but it makes the nonce/HMAC machinery pointless.

**Action:** Remove the `memcmp` bypass (lines 173-196 restructure). Only the HMAC path
should grant auth. `nc` users must use the Python helper `tools/hmac_connect.py`.

### NEW: password logged to Serial during auth (HIGH)

**File:** `src/net_console.cpp:171`

```c
Serial.printf("[CON ]...s_password:<%s> lng:%i resoBuf:<%s>\n", s_password, strlen(s_password), respBuf);
```

The configured console password is printed in plaintext to the hardware serial port on
every authentication attempt. Any serial monitor session captures it. Debug remnant —
remove or replace with a redacted indicator (`s_password[0] ? "***" : "<none>"`).

---

## 8. Error Handling

### Newly FIXED: startTransmit() error recovery — all 3 TX paths

**Commits:** `50d277f8` (main path), `406d662f` (track + APRS paths)

All three `radio.startTransmit()` call sites in `doTX()` now:
1. Check the return code for `RADIOLIB_ERR_NONE`
2. Roll back `iRead` to `iReadBeforeAdvance` (slot preserved for next TX cycle)
3. Clear `tx_is_active = false` (no stuck state waiting for a TX-done IRQ that will never fire)
4. Return `false` immediately (CSMA re-evaluates next iteration, no 15 s watchdog wait)

This closes the "silent TX lockup on OOM/radio failure" behaviour. Not previously an
explicit audit finding, but directly related to STAB-01/02.

### Still OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | main.cpp | 38, 43 | `SPI.begin()` unchecked | OPEN |
| HIGH | esp32/esp32_main.cpp | 651 | `Wire.begin()` unchecked | OPEN |
| HIGH | nrf52/nrf52_main.cpp | 850 | `Wire.begin()` unchecked | OPEN |
| HIGH | nrf52/nrf52_ble.cpp | 128, 130, 135, 272, 278 | 5x BLE service `begin()` unchecked | OPEN |
| MEDIUM | extudp_functions.cpp | 69 | `ETH.begin()` unchecked | OPEN |
| CRITICAL | esp32/esp32_main.cpp | 1317, 1324, 1332, 1342, 1379, 1419, 1452, 1478 | 8x `while (true);` on radio config errors | OPEN |
| CRITICAL | t-deck-pro/peri_lora.cpp | 11 sites | 11x `while (true);` on radio state errors | OPEN |
| MEDIUM | t-deck-pro/tdeck_pro.cpp | post-fallback | `while (1)` on camera init fail | OPEN |
| MEDIUM | t-deck-pro/peri_gps.cpp | 54 | `while(1)` on GPS init fail | OPEN |
| MEDIUM | t5-epaper/t5epaper_main.cpp | 73 | `while(1)` on display init fail | OPEN |
| MEDIUM | t5-epaper/peri_gps.cpp | 53 | `while(1)` on GPS init fail | OPEN |
| MEDIUM | t5-epaper/peri_lora.cpp | 36 | `while(1)` on LoRa init fail | OPEN |
| MEDIUM | loop_functions.cpp | ≈444-481 | ring buffer overflow silent wrap | OPEN |

---

## 9. Watchdog & Recovery (STAB-01..05) — PARTIAL IMPROVEMENT

### STAB-01: TX-IRQ Watchdog added — PARTIAL FIX

**Commit:** `3f25f57e`
**File:** `src/esp32/esp32_main.cpp:1948-1981`

```cpp
// TX-IRQ Watchdog: Wenn bEnableInterruptTransmit zu lange true bleibt
// TX_WATCHDOG_MS = 15000 (configuration_global.h)
if(bEnableInterruptTransmit && _tx_s > 0 &&
   (millis() - _tx_s) > TX_WATCHDOG_MS)
{
    // Force RX recovery
    bEnableInterruptTransmit = false;
    bEnableInterruptReceive  = false;
    ...
    bEnableInterruptReceive = true;
}
```

A 15-second watchdog now detects a stuck `bEnableInterruptTransmit = true` (TX-done IRQ
never arrived) and forces recovery to RX. Previously, the state machine could be permanently
locked if the radio's TX-done interrupt was lost.

**Remaining gap:** FreeRTOS task watchdog (`esp_task_wdt_add()` / `esp_task_wdt_reset()`)
still not used. A complete loop-task hang — as opposed to a stuck ISR flag — is still
undetected. STAB-01 remains OPEN at reduced severity.

### STAB-05: millis() wraparound — OPEN (count unchanged ≈ 33)

All ~33 broken `(start + timeout) < millis()` patterns unchanged.
Spot-checked sites all intact: `mheard_functions.cpp:160`, `lora_functions.cpp:1579`,
`web_functions/web_functions.cpp:239`.

| Severity | Finding | Status |
|----------|---------|--------|
| HIGH | Regexp.cpp:145,150 recursive expand without yield | OPEN |
| CRITICAL | No `esp_task_wdt_add()` / `esp_task_wdt_reset()` for main loop task | PARTIAL (TX-only watchdog added) |
| MEDIUM | No `esp_reset_reason()` | OPEN |
| MEDIUM | No persistent crash counter | OPEN |

---

## 10. Compiler & Build Safety (COMP-01..05) — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | platformio.ini | 152, 179, 219 | `-Wall -Wextra` present, **`-Werror` missing** | OPEN |
| HIGH | platformio.ini | 55-69 | libraries with `^` constraint | OPEN |
| HIGH | platformio.ini | 115 | `espressif32@^6.13.0` floating | OPEN |
| HIGH | variants/t_deck_pro/platformio.ini | 75 | RadioLib version skew vs root | OPEN |
| HIGH | variants/t_deck_pro/platformio.ini | 2 | `espressif32@6.5.0` vs root `@^6.13.0` | OPEN |

---

## 11. Type Safety — OPEN (unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | nrf52/nrf_eth.cpp | 274 | narrowing `uint16_t` → `uint8_t` | OPEN |
| MEDIUM | (multi files) | -- | snprintf return never checked (see BND-02) | OPEN |

---

## 12. Lifetime Safety — OPEN (unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | web_functions/web_functions.cpp | 208, 485, 503 | `web_client.stop()` without resetting `web_header` | OPEN |
| MEDIUM | web_functions/web_functions.cpp | 236-263 | session table cleared only on broken millis() pattern | OPEN |

---

## 13. Logging Safety

### Previously flagged — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| HIGH | lora_functions.cpp | 325, 327, 344, 346, 350, 375 | `Serial.printf()` in OnRxDone ISR | OPEN |
| MEDIUM | t5-epaper/io_extend.c | 26 | `printf()` in ISR | OPEN |

### NEW: password logged to Serial — HIGH

See Section 7 finding `net_console.cpp:171`. Duplicated here for cross-reference.

### log_functions.cpp/h DELETED (upstream b5dc2e5d) — INFO

`src/log_functions.cpp` and `src/log_functions.h` removed entirely in `b5dc2e5d`.
Log output previously routed through these helpers was inlined or dropped. No audit
finding introduced — the removal reduces code surface. Any variant that referenced
these files via `build_src_filter` must ensure they are excluded (checked: RAK, heltec_t114,
t_echo `platformio.ini` all updated in same commit).

---

## 14. Design Patterns — OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | lora_functions.cpp | 1916-1952 | CSMA functions mutate global state | OPEN |
| HIGH | esp32/esp32_main.cpp | 1325, 1365, 1393 | `radio.setDio1Action(callback)` return not checked | OPEN |
| HIGH | via_functions.cpp | 41-50 | `checkMesh()` still returns `bMESH` only — VIA routing logic documented but not implemented (see Section on via_functions below) | OPEN |

---

## 15. Protocol Correctness — OPEN (unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | aprs_functions.cpp | 179-432 | FCS check at line 432 is **after** field extraction at 199-407 — integrity check must precede parsing | OPEN |
| HIGH | aprs_functions.cpp | 134 | only minimum (16 bytes) frame size check | OPEN |
| MEDIUM | aprs_functions.cpp | 120 | single `aprsmsg` parameter reused | OPEN |

---

## 16. State Machine & Session Safety — OPEN (unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | web_functions/web_functions.cpp | 38-39 | session table without mutex | OPEN |
| MEDIUM | web_functions/web_functions.cpp | -- | no per-IP rate limiting | OPEN |

---

## 17. Data Drift Safety

### NEW-2 CLOSED: node_via flash migration safe

`s_meshcomcompat_settings` (WisBlock-API.h:561) has `char node_via[10] = {0}` in sync with
`s_meshcom_settings` (WisBlock-API.h:337). The `nrf52_flash.cpp:242` migration:
```c
memcpy(meshcom_settings.node_via, old_struct.node_via, sizeof(meshcom_settings.node_via));
```
is safe — the source struct has the field. No garbage read. **Finding CLOSED.**

### Still OPEN

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | t5-epaper/nvs_param.cpp | 7-47 | No NVS schema version field | OPEN |

---

## 18. TCP/Web/SSE Safety — OPEN (all unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | web_functions/web_functions.cpp | 31, 340 | unbounded `String web_header` | OPEN |
| CRITICAL | web_functions/web_functions.cpp | 353, 355, 382, 1767, 1827, 1856, 1889 | `indexOf()` return unchecked (may return -1) | OPEN |
| HIGH | web_functions/web_functions.cpp | 265 | 10-slot session table without LRU | OPEN |
| MEDIUM | web_functions/web_functions.cpp | -- | no Content-Length / JSON body size limit | OPEN |

Note: `v4.35p WebService fix` (4b16e578) touched `web_functions.cpp` and `web_setup.cpp`
but focused on command routing, not these structural issues.

---

## 19. Test & Fuzz Readiness — OPEN (unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| MEDIUM | test/ | -- | only `compress_functions.cpp/h` covered | OPEN |

---

## 20. Stack Safety (STK-01..04) — OPEN (unchanged)

| Severity | File | Line | Finding | Status |
|----------|------|------|---------|--------|
| CRITICAL | (project root) | -- | no `sdkconfig` with stack overflow detection level 2 | OPEN |
| HIGH | (entire codebase) | -- | no `uxTaskGetStackHighWaterMark()` monitoring | OPEN |
| MEDIUM | platformio.ini | -- | `-fstack-usage` not enabled | OPEN |

---

## Local Commits (not yet upstream)

### fddae071 + 9ff1578a: sendExtern() stack overflow fix — PENDING PR

- `fddae071`: removes redundant `u_json`/`t_json` stack copies; makes `c_json`/`c_tjson`
  static on nRF52, reduces RAK4631 loop-task stack usage by ~2 KB (fixes crash)
- `9ff1578a`: platform split — nRF52 retains static (4 KB task stack budget), ESP32 reverts
  to stack allocation (8 KB loop-stack, ample headroom; saves ~1 KB DRAM per target)

These fixes address a real RAK4631 crash and have a measurable DRAM benefit on ESP32. Ready
for upstream PR against `oe1kbc_v4.35p` / `dev`.

---

## via_functions.cpp — Routing Stub Status

`--checkmesh` and `--via` commands are wired up (`command_functions.cpp`). `node_via` is
persisted in flash (ESP32: preferences; nRF52: struct field). `checkMesh()` still returns
`bMESH` unconditionally — the documented VIA neighbor-selection algorithm (NCT comparison,
MH table lookup, path-table cross-check) is not implemented. Not a defect today; callers
see identical behaviour. Flagged so the half-implemented state is visible.

---

## NEW Findings Since 2026-05-17

### NEW-1: HMAC-Console plaintext password bypass (HIGH)

See Section 7. `net_console.cpp:174` — `memcmp(respBuf, s_password, ...)` check grants
auth if client sends plaintext password, bypassing the HMAC nonce entirely.

### NEW-2: Password printed to Serial (HIGH)

See Section 7. `net_console.cpp:171` — password in `Serial.printf` format string.

### NEW-3: node_via migration — CLOSED

Compat struct confirmed up-to-date. See Section 17.

---

## Summary: What Changed, What's Done, What's Open

### DONE since 2026-05-17

| Finding | Commit | Resolution |
|---------|--------|------------|
| TX-done IRQ Watchdog (15 s) | 3f25f57e | TX lockup now auto-recovers — PARTIAL STAB-01 |
| startTransmit() error recovery (main path) | 50d277f8 | tx_is_active / iRead rollback on OOM |
| startTransmit() error recovery (track + APRS) | 406d662f | Same, for 2 remaining TX paths |
| Retransmit memcpy order (DM retry empty slots) | 118f9387 | memcpy before len=0 clear |
| SendAckMessage prio=3 status byte | 46decc47 | status=0xFF set after addTxRingEntry |
| resetExternUDP socket restart | a88715cf | hasExternIPaddress check order fixed |
| TLS-Console → HMAC-Console | 50d277f8 + 5bd890c7 | ~0 KB RAM during session (was 36 KB) |
| node_via flash migration safety (NEW-2) | WisBlock-API.h:561 verified | CLOSED |
| log_functions.cpp/h dead code | b5dc2e5d | Removed from codebase |

### STILL NOT DONE (highest priority)

1. **nrf52/at_cmd.h:30 sprintf** — CRITICAL, one-line fix; trivial PR candidate
2. **WiFi.softAP without password** — CRITICAL, 3 sites: `udp_functions.cpp:534`, `safeboot/main.cpp:68,187`
3. **net_console.cpp:174 plaintext bypass** — HIGH/SECURITY, remove the `memcmp` shortcut
4. **net_console.cpp:171 password logged** — HIGH, remove or redact
5. **millis() wraparound** — CRITICAL, ~33 sites; pattern: `(uint32_t)(millis() - start) >= timeout`
6. **APRS FCS before parsing** — CRITICAL, `aprs_functions.cpp:432` → move before line 199
7. **web_header unbounded concat / indexOf unchecked** — CRITICAL, `web_functions.cpp:340,353`
8. **`while(true)` on radio init** — CRITICAL, 8+11 sites; replace with `esp_restart()`
9. **Main-loop task watchdog** — `esp_task_wdt_add()` / `esp_task_wdt_reset()` missing
10. **sendExtern() fix upstream PR** — our local commits fddae071 + 9ff1578a ready to submit

---

## Rebase-Specific Observations (2083fdbd → b0c26176)

The 16-commit window between tags v4.35p.05.13 and v4.35p.05.23 (2026-05-16..23) brought
substantial functional fixes (TX lockup, retransmit, ACK priority, UDP socket restart) and a
major RAM reduction (TLS → HMAC console). The codebase is clearly under active development.

The HMAC-console introduction (`50d277f8`) is the highest-quality upstream contribution in
recent history — correct NONCE generation via hardware TRNG, constant-time comparison
(`ct_equal`), mbedtls/md.h with no new library dependency — but the plaintext-bypass
backdoor added by the subsequent `85993122` patch undermines the security model entirely.
This finding is worth raising in the upstream PR review.

CRITICAL count dropped from 17 to 14 (−3) net, but the remaining 14 are all structural
issues requiring non-trivial effort. The `nrf52/at_cmd.h:30` sprintf macro is the only
remaining trivially-fixable CRITICAL.
