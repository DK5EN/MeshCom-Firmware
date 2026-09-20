# ESP32 C++ Code Quality Rules

Reusable secure coding rules for ESP32 (single- and dual-core) firmware projects
using Arduino framework and FreeRTOS. Derived from production audit experience.

These rules are MANDATORY. Violations are treated as bugs.

---

## 1. Memory Safety (MEM-01..05)

- **No `malloc`/`new` after initialization** -- all allocation in `setup()` only.
- Ring buffers, session pools, TX/RX queues: static arrays, fixed size at compile time.
- String handling: fixed `char[]` arrays -- NEVER Arduino `String` in hot paths.
- If buffer size varies, use worst-case static allocation.
- Never mix `malloc`/`free` with `pvPortMalloc`/`vPortFree`.
- Prefer `xTaskCreateStatic()` for long-lived tasks.
- Monitor heap continuously: `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`.
- Log heap metrics periodically: free, min-ever, largest-block.
- All buffer sizes defined as `#define` constants in a single header, used everywhere.

## 2. Buffer Overflow Prevention (BND-01..05)

- **NEVER** `sprintf()`, `strcpy()`, `strcat()`, `gets()` -- always bounded variants.
- Always `snprintf()` with correct size parameter.
- Always `strncpy()`, `strncat()` with bounds.
- All `memcpy()` calls: validate length BEFORE copying, assert `len <= buffer_size`.
- All array access: bounds-check index before use.
- Format strings: ALWAYS string literals, NEVER variables.
- Check `snprintf` return for truncation (`>= buf_size`).
- `static_assert` / `_Static_assert` on all protocol struct sizes.

## 3. Input Validation

Every external input is untrusted. Validate BEFORE processing:

- Radio RX: validate packet length before buffer copy.
- Serial/UART frames: validate frame length against maximum before processing.
- Network messages: validate header length fields match actual data, check against max.
- BLE writes: validate characteristic data length before parsing.
- REST API: validate all JSON fields (type, range, length) before applying.
- Integer parameters: check range before cast (reject out-of-range values).

## 4. Thread Safety (RACE-01..08)

- FreeRTOS Queues for passing data between tasks (message-passing preferred).
- `xSemaphoreCreateMutex()` for shared state (NOT `xSemaphoreCreateBinary()`).
  - Mutex provides priority inheritance, binary semaphore does NOT.
- NEVER access shared data from two tasks without synchronization.
- `portMUX_TYPE` spinlocks for cross-core shared data on dual-core ESP32.
- `volatile` alone is NOT thread-safe on dual-core -- needs mutex/spinlock/atomic.
- `portDISABLE_INTERRUPTS` does NOT protect against the other core.
- `vTaskSuspendAll` only suspends the calling core.
- All mutex takes with TIMEOUT (50-500ms), never `portMAX_DELAY`. Log + handle failure.
- Consistent lock ordering to prevent deadlocks (document ordering in comments).
- Error paths MUST release held locks (RAII pattern or goto cleanup).
- Never block in FreeRTOS timer callbacks.
- Float-using tasks: pin to specific core (FPU context).
- Use `std::atomic<T>` for simple counters/flags shared across cores.
  - `std::memory_order_relaxed` sufficient for monotonic counters.
- Never `strtok()` / `localtime()` / `rand()` -- use `_r` reentrant variants or `esp_random()`.

## 5. Interrupt Safety (ISR-01..04)

- ISR handlers: ONLY set flags or use `_FromISR` FreeRTOS API calls.
  - `xQueueSendFromISR()`, NOT `xQueueSend()`
  - `xSemaphoreGiveFromISR()`, NOT `xSemaphoreGive()`
- NO `Serial.print`, NO SPI/I2C, NO `delay()`, NO mutex take in ISR.
- All ISR handlers: mark with `IRAM_ATTR`.
- Functions called from ISR must also be in IRAM.
- ISR data: `DRAM_ATTR` (not flash).
- ISR execution: < 10 microseconds, defer work to task via queue/notification.
- No compiler-generated library calls in ISR (no division, no 64-bit math).

## 6. SPI Bus Safety (SPI-01..05)

- NEVER access SPI from ISR on shared bus (ISR only sets flag/semaphore).
- Single `spi_bus_mutex` for ALL SPI devices sharing a bus.
- Hold mutex for ENTIRE multi-transaction sequence (not per-transaction).
- CS pins correctly managed per device.
- Never use SPI1 (flash cache bus) for peripherals.
- Pin SPI-using tasks to same core, or use spinlock.

## 7. Authentication & Security

- Web interface: HTTP Basic Auth on ALL endpoints, no exceptions.
- BLE: PIN-based pairing required, configurable PIN (not hardcoded default).
- OTA: firmware upload requires authentication.
- OTA: validate firmware binary (magic bytes, chip ID, size) BEFORE any flash write.
- WiFi AP mode: ALWAYS password-protected (WPA2, min 8 chars). Never open.
- Credentials stored in NVS -- not in source code.
- Use `esp_random()` (hardware RNG) for randomness -- not `rand()`.

## 8. Error Handling

- ALL function return values checked (SPI, I2C, radio, network operations).
- ALL `begin()` calls: check return value, log and handle failure.
- Sensor/peripheral failure: graceful degradation (continue without that subsystem).
- Queue full: log warning, drop oldest or reject new (defined per queue).
- Network failure: retry with backoff, not infinite loop.
- Parse errors: log + discard, never crash.

## 9. Watchdog & Recovery (STAB-01..05)

- Task watchdog: every custom task registered with `esp_task_wdt_add()`.
- `esp_task_wdt_reset()` at the top of every loop iteration.
- All loops contain `vTaskDelay()` or `taskYIELD()` -- no busy-wait.
- All loops MUST yield within 1ms.
- Boot reason logging: `esp_reset_reason()` at startup.
- Persistent crash counter: if >N crashes in M minutes, enter safe mode.
- Never disable brownout detector.
- `millis()` wraparound: `(uint32_t)(millis() - start) >= interval` ONLY.
  - NEVER `(now > start + timeout)` -- wraps incorrectly at 49 days.

## 10. Compiler & Build Safety (COMP-01..05)

- Build flags: `-Wall -Wextra -Werror` -- every warning is an error.
- No implicit conversions between signed/unsigned in size calculations.
- All size arithmetic: check for overflow before use (especially `a + b > MAX`).
- Use `size_t` for all sizes and lengths, `uint16_t`/`uint32_t` for protocol fields.
- Never compile out assertions (`NDEBUG` forbidden).
- Never put side effects inside `assert()` / `configASSERT()`.
- Every `switch` on enum has `default:` case with error log.
- ALL library versions pinned exactly (no `^` or `~`).

## 11. Type Safety

- NO implicit narrowing: `uint32_t` -> `uint8_t` requires explicit cast + range check.
- `strlen()` returns `size_t` -- NEVER store in `uint8_t` (truncation at 255).
- signed/unsigned comparison: ALWAYS explicit cast.
- `snprintf` returns `int` (can be negative) -- check `< 0` before using as offset.
- Enum values for states/types, NEVER raw integers.
- Protocol structs for wire parsing: `__attribute__((packed))` or field-by-field decode.
- Pointer arithmetic: ALWAYS validate `offset < len` before `buf[offset]`.

## 12. Lifetime Safety

- Session/slot pointers: after free/release, set pointer to NULL, invalidate all references.
- TCP client slots: cleanup on disconnect MUST reset parser, flush pending, mark slot free.
- Callbacks: NEVER store address of stack variable for async use (queue, callback, notification).
- Ring buffer slot reuse: `occupied = false` ONLY after consumer is done reading.
- Timer callbacks: ALWAYS validate target still exists (in_use check under mutex).
- AsyncTCP `onData`/`onDisconnect` can race -- disconnect handler must not free while data callback may be pending.

## 13. Logging Safety

- Log macros ALWAYS use literal format strings: `LOG("MOD", "got %s", data)`, never `LOG("MOD", data)`.
- Log buffer bounded (fixed-size, never dynamic).
- Per-module log level control at runtime.
- NO logging from ISR context (use counters, post to queue for deferred logging).

## 14. Design Patterns

- SPSC queues: document producer/consumer contract in header. If second producer added, add mutex.
- Callbacks: check for NULL before invocation. Set callback BEFORE starting consumer task.
- Pure functions preferred for codecs, CRC, CSMA -- no global state, testable on native.
- Static allocation audit: no malloc/new in packet processing paths. All buffers compile-time sized.

## 15. Protocol Correctness

- Each transport gets its own parser instance (no shared parser state).
- Checksums/FCS verified BEFORE any field parsing.
- Minimum and maximum frame sizes validated before access.
- Header fully read and validated before processing payload.
- Bounded scans: all string/path scans capped to prevent unbounded iteration.

## 16. State Machine & Session Safety

- ALL state/event combinations handled (no unhandled cases in switch).
- Session pool: mutex held during search + init (atomic allocation).
- Session pointers: check `in_use` under mutex before accessing session data.
- Timer events: validate session ID range AND in_use before dispatch.
- Rate limiting: cap repeated responses to prevent amplification.
- Retransmission counter: increment ONCE per timeout expiry (not per tick).

## 17. Data Drift Safety

- Settings schema versioning: store version in NVS. On mismatch -> factory reset to safe defaults.
- Library versions: ALL pinned exactly (no `^` or `~`).
- Compile-time constants: single source of truth in one header. Never redefine elsewhere.
- `#ifdef NATIVE_BUILD` branches: BOTH branches must be tested (native + ESP32 build).

## 18. TCP/Web/SSE Safety

- TCP server: enforce max client count. Reject excess connections. Cleanup on disconnect.
- REST API: JSON body size limited. All params validated. Path traversal blocked.
- SSE: max client count enforced. Backpressure via bounded buffer, drop oldest. Keepalive heartbeat.
- OTA: validate firmware magic + chip ID BEFORE any flash write. WDT reset per chunk.
- UDP tunnels: rate limit per peer. Source filtering. Checksums validated before forwarding.
- WiFi AP: ALWAYS password-protected (WPA2, min 8 chars). Never open.

## 19. Test & Fuzz Readiness

- All parsers accept `(const uint8_t* buf, size_t len)` -- suitable for fuzzing.
- All parsers return error code for invalid input -- never crash, never hang.
- Test both valid AND malformed input in unit tests.
- Boundary tests: 0 bytes, 1 byte, max bytes, max+1 bytes.

## 20. Stack Safety (STK-01..04)

- Stack overflow checking level 2 enabled in sdkconfig.
- ESP32 stack sizes in BYTES not words.
- Log `uxTaskGetStackHighWaterMark()` for all tasks periodically.
- Consider `-fstack-usage` compiler flag.

---

## Audit Checklist

When auditing an ESP32 project against these rules, check each category and record:

| Category | Rule IDs | Status | Findings |
|----------|----------|--------|----------|
| Memory Safety | MEM-01..05 | | |
| Buffer Safety | BND-01..05 | | |
| Input Validation | Section 3 | | |
| Thread Safety | RACE-01..08 | | |
| ISR Safety | ISR-01..04 | | |
| SPI Bus | SPI-01..05 | | |
| Auth & Security | Section 7 | | |
| Error Handling | Section 8 | | |
| Watchdog | STAB-01..05 | | |
| Compiler/Build | COMP-01..05 | | |
| Type Safety | Section 11 | | |
| Lifetime Safety | Section 12 | | |
| Logging Safety | Section 13 | | |
| Protocol Correctness | Section 15 | | |
| State Machines | Section 16 | | |
| Data Drift | Section 17 | | |
| TCP/Web/SSE | Section 18 | | |
| Test Readiness | Section 19 | | |
| Stack Safety | STK-01..04 | | |
