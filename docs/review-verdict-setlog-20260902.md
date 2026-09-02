# SL-01..SL-07 — Fable Verdict (2026-09-02)

Review of `feat-setlog-20260902` @ `11ff99b8` (four finders incl. object-code stack
measurements on the RAK4631 build; orchestrator re-derived the load-bearing claims in
`dedup_functions.cpp`, `udp_functions.cpp`, `nrf_eth.cpp`, `lora_functions.cpp:1156`).

## Finding 1: `is_new_packet()` prints under `--loradebug`, so the SL-01 probe doubles the markers

- **File:** `src/lora_functions.cpp:636`, `:872`; `src/dedup_functions.cpp:36,49`
- **Severity:** medium (breaks the Welle-3 cross-check `newid`/`dup` vs `RX_DEDUP_*`)
- **Fix (S1):** call `is_new_packet(RcvBuffer+1)` exactly once per frame, unconditionally,
  before the RX print; reuse the result at the verdict (`setlogCountDedup(rx_is_new)`).
  No `addLoraRxBuffer()` runs between the two sites for the same frame (`:929` is inside
  the new-packet branch).

## Finding 2: Gateway-injected frames never count as `newid`

- **File:** `src/udp_functions.cpp:335,494`; `src/nrf52/nrf_eth.cpp:667`
- **Severity:** medium (the dedup-window formula `MAX_DEDUP_RING/(newid/300 s)` is wrong
  exactly on the gateways this plan targets)
- **Fix (S2):** `stat_newid.fetch_add(1)` next to each of those `addLoraRxBuffer()` calls.

## Finding 3: Hop byte in RLY/GWI/GWU carries the 0x20 app-offline flag

- **File:** `src/lora_functions.cpp:1156` (`max_hop |= 0x20`), RLY/GWU sites, `udp_functions.cpp`, `nrf_eth.cpp`
- **Severity:** medium (parser sees `H23` for hop 3)
- **Fix (S3):** print `max_hop & 0x0F` in RLY, GWI, GWU (TX already masks). RX line unchanged.

## Finding 4: `getTimeString()` mallocs a `String` per new line, 2–4 per frame in the LORA task

- **File:** every `printfdeb("%s [LOG] %s\n", getTimeString().c_str(), buf)` site (10)
- **Severity:** medium (same class as the NimBLE-starvation finding on ESP32)
- **Fix (S4):** `void setlogPrint(const char *body)` in `loop_functions.cpp` that formats
  `HH:MM:SS` into a stack `char[12]` from the same clock fields `getTimeString()` uses and
  calls `printfdeb` once; all ten sites use it.

## Finding 5: Two identical STAT fill blocks

- **File:** `src/esp32/esp32_main.cpp:2164-2205`, `src/nrf52/nrf52_main.cpp:1381-1421`
- **Severity:** low
- **Fix (S5):** `void setlogFillStat(struct setlogStatFields *f, uint32_t heap)` in
  `loop_functions.cpp`: drains the interval counters (`exchange(0)`), reads
  `stat_drop_count[1..5]` without clearing, fills mheard/trickle/versions/uptime. Clearing
  of `stat_drop_count[]` stays platform-side: ESP32 keeps its existing memset; nRF52 clears
  inside `taskENTER_CRITICAL()/taskEXIT_CRITICAL()` (increments happen under that lock).

## Finding 6: Duplicated RX/ACK format strings, prefix test cannot catch drift

- **File:** `src/loop_functions.cpp:3176-3210`, `test/test_setlog_lines/test_main.cpp:42-58`
- **Severity:** medium
- **Fix (S6):** give `printBuffer_aprs()` and `printBuffer_ack()` a trailing
  `const char *tail = ""` parameter (default in `loop_functions.h`), append `%s` before the
  `\n` in the one existing format string; delete `printBuffer_aprs_rx`/`printBuffer_ack_rx`
  and their declarations; callers format the tail with `setlogFormatRxTail` into a local
  and pass it. The old line is byte-identical by construction; replace the prefix test by
  one that checks the tail formatter only.

## Finding 7: `ringmax` misses the saturated case; ringSource copy on retransmit outside the lock

- **File:** `src/txring_functions.cpp:601`; `src/lora_functions.cpp:2133-2141`
- **Severity:** low
- **Fix (S7):** sample `txRingDepth()` for `stat_ring_max` before the `resultSlot >= 0`
  test (a full ring is the value we want to see). The retransmit copy stays (all callers
  run at the same priority without time slicing); one-line comment.

## Finding 8: Tests

- **File:** `test/test_setlog_lines/test_main.cpp`, `test/test_txring/test_txring.cpp`
- **Fix (S8):** `n = 1` and exact-fit `n` cases for every formatter; a `test_txring` case
  that `ringSource[]` is copied on priority eviction (env `native_aprs` builds
  `txring_functions.cpp`); mapping test for the `rx_` prefix kept.

## Finding 9: Comments and includes

- **Fix (S9):** `txring_functions.h` includes only what it needs (`extern uint8_t
  ringSource[MAX_RING]`); `setlog_lines.h` is included in `txring_functions.cpp`. Trim the
  multi-line German comments in `lora_functions.cpp` added by this branch to ≤ 2 lines
  each (upstream style is terse); every "1 KB timer task" remark is wrong — `OnRxDone`
  runs in the 16 KB LORA task (`RX_TIMEOUT_VALUE 0` on all variants, DIO1 → semaphore →
  LORA task), measured chain 2.1 kB; say "LORA task" or nothing.

## Refuted or declined (do not re-investigate)

- RLY reason precedence (gwfilter/gwcap/ping before self/aprs/nomesh): the printed reason
  is the first disqualifier in code execution order; the plan table's order described
  the code wrongly. Kept; the skill reference states the rule.
- Stack budget in `OnRxDone`: +196 B on a 16 KB task. Non-issue.
- Serial blocking: ESP32 UART 115200 adds 8–12 ms per received frame under `--setlog`,
  on top of the ~22 ms the existing `[LOG]` line already costs; nRF52 CDC ~2 ms. Accepted
  instrumentation cost, stated in the PR text.
- `std::atomic` on both cores is inline lock-free (ldrex/strex, s32c1i); no libatomic.
- Block-scope `extern PacketStatus_t RadioPktStatus` in `OnRxError`: same pattern as the
  existing code above it.
- `setlogPathHasCall` replacing the old String-based loop check at `:1439`: declined,
  keeps the upstream relay logic untouched.
- `setlogPrintTx` counting `stat_txn` with `--setlog` off: intended.
