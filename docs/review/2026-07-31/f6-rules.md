# F6 — Conformance to the project's own code-quality rules

**Repo:** .
**Branch:** v4.35p_prio · **HEAD:** 1ba101f4 · **Date:** 2026-07-30
**Rules under test:** `docs/codequality-rules.md` (232 lines, 20 numbered sections, declared MANDATORY — "Violations are treated as bugs")
**Method:** ran the project's own `/code-audit` methodology (`.claude/commands/code-audit.md` + `tools/code_audit_scan.py --full`), plus two measured builds and a cppcheck run.

---

## Headline numbers (all measured, not estimated)

| Measurement | Command | Result |
|---|---|---|
| Mechanical scan, full `src/` | `python3 tools/code_audit_scan.py --full` | 332 files, **507 findings** (4 CRITICAL, 21 HIGH, 482 MEDIUM) |
| Warning volume, current flags | `pio run -e heltec_wifi_lora_32_V3` (after `-t clean`) | **9 warnings total — 4 in `src/`, 5 in third-party libdeps** |
| Warning volume, strict flags | same + `-Wconversion -Wsign-compare -Wshadow -Wformat=2 -Wcast-align -Wold-style-cast -Wdouble-promotion -Wuseless-cast` | **29,968 total — 1,250 in `src/`, 28,718 in libdeps/framework** |
| cppcheck 2.11 (ships with the platform) | `cppcheck --enable=warning,style,performance,portability` on `src/*.cpp src/esp32/*.cpp src/web_functions/*.cpp` | **459 findings** (4 error, 28 warning, 426 style, 1 performance) |
| `static_assert` in whole tree | grep | **1** |
| `__attribute__((packed))` protocol structs | grep | **0** |
| CI quality gate | `.github/workflows/meshcom-ci.yml` | **none** — workflow triggers on `push: tags` only, runs `pio run`, no checks, no tests, no PR trigger |

---

## Rule conformance matrix

Counts are over `src/` (332 source files). "Evidence" is one representative site, not the full list.

| Rule ID / section | Rule text (short) | Conformance | Evidence file:line | Violations |
|---|---|---|---|---|
| MEM-01 | No `malloc`/`new` after init; all allocation in `setup()` | **fail** | `src/printfdeb_functions.cpp:103` (`malloc` in the logging hot path) | 9 `malloc` + 112 `new` |
| MEM-02 | Ring buffers / pools: static, compile-time fixed | pass | `src/loop_functions.cpp:378–413` (7 static ring buffers) | 0 |
| MEM-03 | Fixed `char[]`, NEVER Arduino `String` in hot paths | **fail** | `src/web_functions/web_functions.cpp:31` (`String web_header` — fed from network) | 333 `String` declarations |
| MEM-04 | Worst-case static allocation if size varies | partial | display buffers `malloc`'d w/ NULL check (`src/t-deck-pro/tdeck_pro.cpp:200`) | 7 |
| MEM-05 | Prefer `xTaskCreateStatic()`; monitor heap | **fail** | 8 `xTaskCreate*` sites, e.g. `src/t-deck-pro/peri_gps.cpp:78` | `xTaskCreateStatic`: **0**; `getMinFreeHeap`: 2 |
| BND-01 | NEVER `sprintf`/`strcpy`/`strcat`/`gets`; format strings ALWAYS literals | **fail** | `src/printfdeb_functions.cpp:118` `Serial.printf(temp)` — **non-literal format string fed with over-the-air data** | 4 `sprintf`, 5 `strcpy`, 2 `strcat`, 1 non-literal fmt |
| BND-02 | All `memcpy`: validate length BEFORE copy, assert `len <= buffer_size` | **fail** | `src/esp32/esp32_main.cpp:310`; `src/lora_functions.cpp:211,385` | 149 `memcpy` sites, only a handful length-guarded |
| BND-03 | `snprintf` with the *correct* size parameter | **fail** | `src/web_functions/web_functions.cpp:1660` `snprintf(value, 100, …)` on `char value[40]` (decl :1601) | 43 calls pass a numeric literal instead of `sizeof(dst)` |
| BND-04 | Check `snprintf` return for truncation | **fail** | project-wide; acknowledged in `docs/code-audit-20260626.md` Nr 77 as "~600 sites, DE-PRIO" | ~600 |
| BND-05 | `static_assert` on all protocol struct sizes | **fail** | no wire struct carries one | **1** `static_assert` in the entire tree; **0** packed structs |
| §3 | Input validation on every external input | **fail** | `src/phone_commands.cpp:226,324,364,385,403,466,479–488,555` (BLE cmds unvalidated) | 5 finding groups, all DE-PRIO'd |
| RACE-01 | Never touch shared data from two tasks unsynchronised | partial | fixed for externQueue (`src/extudp_functions.cpp:55,520,528`); still open for `transmissionState` (`src/lora_functions.cpp:9`) | — |
| RACE-02 | `xSemaphoreCreateMutex()`, not `Binary` (priority inheritance) | **fail** | `src/t-deck/tdeck_main.cpp:111`, `src/esp32/esp32_audio.cpp:40`, `src/nrf52/nrf52_main.cpp:407` | 3 binary vs 2 mutex |
| RACE-03 | All mutex takes with TIMEOUT, never `portMAX_DELAY` | **fail** | `src/net_console.cpp:208,348`; `src/nrf52/api_functions.cpp:262`; `src/t-deck/tdeck_main.cpp:401` | 4 |
| RACE-04 | `portMUX_TYPE` spinlocks for cross-core shared data | partial | only 5 `portMUX`/`portENTER_CRITICAL` uses tree-wide | 5 uses vs ~40 shared globals |
| RACE-05 | `volatile` alone is NOT thread-safe — needs mutex/spinlock/atomic | **fail** | `src/gps_functions.cpp:172–177` (6 `volatile` ISR-shared vars); `src/lora_functions.cpp:9` | see F6-2 |
| RACE-06 | Consistent, documented lock ordering | n/a-ish | only one mutex in play (`s_mutex`) — no ordering to violate yet | 0 |
| RACE-07 | `std::atomic<T>` for counters/flags shared across cores | **pass (improved)** | `src/loop_functions_extern.h:176–227` (13 atomics) | 0 |
| RACE-08 | Never `strtok`/`localtime`/`rand` — use `_r` variants | **fail** | `src/time_functions.cpp:96` `localtime(&unix)` | 1 |
| ISR-01..02 | ISR only flags / `_FromISR`; no Serial, SPI, delay in ISR | partial | logs now gated `#ifdef LORA_ISR_DEBUG` (`src/lora_functions.cpp:31–74`); SPI-from-ISR kept as a documented EXCEPTION (`lora_functions.cpp:332`) | 1 accepted |
| ISR-03 | All ISR handlers `IRAM_ATTR`; ISR data `DRAM_ATTR` | **fail** | `src/nrf52/nrf52_main.cpp:317,326,335` | `IRAM_ATTR`: **1** occurrence; `DRAM_ATTR`: **0** |
| ISR-04 | ISR < 10 µs, defer to task | partial | `OnRxDone` does buffer copy + state machine inline | — |
| SPI-01..05 | One `spi_bus_mutex` for all devices; pin SPI tasks to one core | **fail** | no SPI mutex exists | `spi_bus_mutex`: **0**; core pinning DE-PRIO'd (Nr 43) |
| §7 | Auth on web/BLE/OTA; AP always WPA2 | **fail (by declared exception)** | `src/udp_functions.cpp:540`, `src/safeboot/main.cpp:68,187` open AP | 3 open-AP sites + 5 auth exceptions |
| §8 | All return values checked; no infinite loop on failure | partial | `esp32_main.cpp` fixed; **`src/t-deck-pro/peri_lora.cpp:48,54,60,66,72,78,85,91,97,105,114` still `while (true);`** | 11 (see F6-1) |
| STAB-01 | Every task registered with `esp_task_wdt_add()` + reset per loop | partial | only main loop: `src/esp32/esp32_main.cpp:603`, `:1745` | 1 of 9 tasks |
| STAB-02 | All loops yield within 1 ms | partial | `vTaskDelay`/`taskYIELD` appears **4** times in all of `src/` | — |
| STAB-03 | `esp_reset_reason()` at startup | **fail** | not present | **0** |
| STAB-04 | Persistent crash counter → safe mode | **fail** | not present | 0 |
| STAB-05 | `millis()` wraparound: subtraction form only | **near-pass** | 1 residual: `src/t-deck/lv_obj_functions.cpp:3639` `if(lastsavePOSPersistence + 30000 > millis())` | 1 (was ~70) |
| COMP-01 | `-Wall -Wextra -Werror` — every warning is an error | **fail** | `platformio.ini:158,185,225` — `-Wall -Wextra`, **no `-Werror` anywhere** | 0 occurrences of `Werror` |
| COMP-02 | No implicit signed/unsigned conversion in size math | **fail** | measured only when `-Wconversion` is added | 241 `-Wconversion` + 184 `-Wfloat-conversion` in `src/` |
| COMP-03 | Never compile out assertions; no side effects in `assert` | vacuous | `assert(`: **1** occurrence, `configASSERT`: **0** | nothing to compile out |
| COMP-04 | Every `switch` on enum has `default:` | partial | 52 `switch` vs 40 `default:` | ≥12 |
| COMP-05 | ALL library versions pinned exactly (no `^`/`~`) | **fail** | `platformio.ini:66–133` + 12 variant files | **87** floating constraints |
| §11 | Type safety: no implicit narrowing, explicit casts, enum states | **fail** | `src/nrf52/nrf_eth.cpp:274` `uint16_t`→`uint8_t` | 425 conversion-class warnings in `src/` |
| §12 | Lifetime: reset parser/session on disconnect, no dangling refs | partial | session cleanup millis fixed (`web_functions.cpp:240`); `web_header` not reset after `stop()` (EXCEPTION) | 2 |
| §13 | Log macros ALWAYS literal format strings; no logging from ISR | **fail** | `src/printfdeb_functions.cpp:118` | 1 CRITICAL (F6-3) |
| §14 | Pure functions for codecs/CRC/CSMA, testable on native | **fail** | `src/lora_functions.cpp:1916–1952` CSMA mutates globals (DE-PRIO) | no native build exists |
| §15 | Checksums verified BEFORE field parsing; min+max frame size | partial | max-size added (`src/aprs_functions.cpp:9,149`); **FCS still checked after parsing** | 1 (F6-4) |
| §16 | All state/event combos handled; session pool under mutex | partial | session table has no mutex (`web_functions.cpp:38–39`, EXCEPTION) | 2 |
| §17 | Settings schema version in NVS → factory reset on mismatch | **pass** | `src/configuration_global.h:5` + `src/esp32/esp32_main.cpp:725`, `src/nrf52/nrf52_main.cpp:499` | 0 |
| §17 | `#ifdef NATIVE_BUILD` both branches tested | **fail** | `NATIVE_BUILD`: **0** occurrences | n/a |
| §18 | TCP/Web/SSE: max clients, body limits, path traversal blocked | **fail (by declared exception)** | `web_functions.cpp:31,340,353` | 4 |
| §19 | Parsers take `(const uint8_t*, size_t)`, return error codes, fuzz-ready; boundary tests | **fail** | `test/` holds `compress_functions.cpp` + a stray TinyGSM header — **no test suite, no native env, no unit tests** | total |
| STK-01..04 | Stack checking lvl 2; log `uxTaskGetStackHighWaterMark` | **fail** | not instrumented | `uxTaskGetStackHighWaterMark`: **0** |

**Summary: 3 sections pass (MEM-02, RACE-07, §17 schema versioning), 9 partial, 20 fail.**

RACE-07 is the one section that materially improved since 2026-06-27 and it holds up under inspection.

---

## Part B: previously-claimed fixes that did not hold

Sampled from `docs/code-audit-20260626.md` (the "Entscheidungsprotokoll", 95 numbered decisions),
`docs/code-audit-20260531.md` (priority ranking) and `docs/code-audit-fixes-20260627.md` (the fix claim).

### B-1 — Nr 59: 11× `while(true);` on radio-init failure — **MANDATED FIX, SILENTLY DROPPED**

**The claim.** `docs/code-audit-20260626.md:288`:

> | 59 | CRITICAL | t-deck-pro/peri_lora.cpp | 11x `while(true);` | **FIX** | Identisches Problem; **trotz T-Deck-Pro: geteilter Code-Pfad** |

The reviewer explicitly pre-rejected the "it's only T-Deck-Pro" excuse, in writing, in the decision column.

**What was delivered.** `docs/code-audit-fixes-20260627.md:70`:

> **A2:** 8 radio-init sites in `esp32_main.cpp`; `t-deck-pro/peri_lora.cpp` (11) excluded (not built by either mandated target → unverifiable).

The A2 row in the results table is marked **✅ done**, and the document's headline says "14 of 17 resolved". Nr 59 is not listed among the Deviations.

**Current source.** Still present, unchanged:

```
src/t-deck-pro/peri_lora.cpp:48,54,60,66,72,78,85,91,97,105,114 →  while (true);
```

**The excuse is factually wrong.** `t_deck_pro` is in `platformio.ini` `default_envs` (line ~46), so plain `pio run` builds it; and `.github/workflows/meshcom-ci.yml:72,108` builds it *and publishes `t_deck_pro.bin` as a GitHub release artifact*. It is shipped firmware. "Not built by either mandated target" describes the two *verification* targets the fix round chose for itself, not the product.

**Verdict: DROPPED.** A CRITICAL marked FIX, whose stated rationale explicitly anticipated and rejected the excuse that was then used to skip it, reported inside a ✅ row. Users of a T-Deck-Pro get a permanently dead node on any radio-init hiccup.

---

### B-2 — Nr 32: `pulseTimes` ISR race — **MANDATED FIX, RECLASSIFIED TO NO-OP BY THE IMPLEMENTER**

**The claim.** `docs/code-audit-20260626.md:241`:

> | 32 | HIGH | gps_functions.cpp:174-175 | `pulseTimes` ISR Race | **FIX** | Torn Read bei GPS-Timing möglich |

**What was delivered.** `docs/code-audit-fixes-20260627.md:31` marks B4 "✅ already `volatile` (no change)", and `:86`:

> **B4:** `pulseTimes`/`pulseIndex` are already `volatile`; the GPS ISR is single-core (ISR↔task, barrier via `detachInterrupt`) → satisfied without change.

**Current source.** `src/gps_functions.cpp:172–177`:

```cpp
volatile unsigned long pulseTimes[SAMPLE_COUNT];
volatile int pulseIndex = 0;
volatile unsigned long lastMicros = 0;
```

No atomic, no critical section. Unchanged.

**Why this doesn't hold.** `docs/codequality-rules.md:51` — the rule this finding was raised under — says verbatim:

> `volatile` alone is NOT thread-safe on dual-core -- needs mutex/spinlock/atomic.

The fix report's justification is precisely the thing the rule forbids, and `gps_functions.cpp` is compiled into the ESP32 (dual-core) targets, not only nRF52. The technical argument (ISR↔task on one core, `detachInterrupt` as a barrier) is *defensible for the nRF52 path* — but it is a re-adjudication of a HIGH finding performed by the implementer after the review, with no reviewer sign-off, recorded as a ✅.

**Verdict: NOT FIXED, recorded as fixed.** Severity of the underlying race is arguably low; the process failure is the finding.

---

### B-3 — Nr 33: `scanFlag` **and** `transmissionState` — **HALF DONE**

**The claim.** `docs/code-audit-20260626.md:242`: "esp32/esp32_main.cpp:443,458 | `scanFlag`/`transmissionState` ohne volatile/atomic | **FIX**". Both names, one decision.

**Current source.**
- `src/esp32/esp32_main.cpp:473` — `std::atomic<bool> scanFlag{false};` ✅
- `src/lora_functions.cpp:9,16,22,28,34,40,46` — `extern volatile int transmissionState;` (7 duplicate externs) ❌

**Verdict: PARTIAL, and honestly declared.** `docs/code-audit-fixes-20260627.md:73–75` documents the deviation ("would ripple across 3 files for marginal benefit"). Fair disclosure — but the fix table still shows B2 as ✅ done with no asterisk, and the 7 duplicated `extern volatile int transmissionState;` declarations are themselves a §17 "single source of truth" violation nobody has flagged.

---

### B-4 — Nr 86: APRS FCS checked after parsing — **CRITICAL, STILL OPEN, OPENLY DEFERRED**

`docs/code-audit-20260626.md:350` mandates **FIX** ("Korrupte Pakete werden vollständig verarbeitet"). `docs/code-audit-fixes-20260627.md:88–92` defers it with a genuine technical reason (the FCS coverage end-offset is computed *during* the variable-length parse, so it isn't known at entry) and lists it under Open items.

Current source confirms: `src/aprs_functions.cpp` still parses fields before the FCS check; only the max-frame-size guard (Nr 87) landed at `:9,149`.

**Verdict: HONEST DEFERRAL, but a CRITICAL that has now survived four consecutive audits (2026-05-08, 05-25, 05-31, 06-26) untouched.** The deferral reason ("would violate the minimal-changes rule") means the rule set and the contribution policy are in direct conflict — see F6-7.

---

### B-5 — Priority list vs. decision table inside the *same* document

`docs/code-audit-20260626.md:169–176` prints the carried-forward priority ranking:

> 2. **WiFi.softAP without password** — CRITICAL, 3 sites
> 6. **web_header unbounded concat** — CRITICAL

…while lines 268–269 and 371 of the *same file* record those exact findings as:

> | 44 | CRITICAL | udp_functions.cpp:534 | `WiFi.softAP()` ohne Passwort | **EXCEPTION** | Amateurfunk-Regulierung verbietet Verschlüsselung |
> | 92 | CRITICAL | web_functions.cpp:31,340 | `web_header` unbegrenzt + `indexOf()` unchecked | **EXCEPTION** |

**Verdict: INTERNAL CONTRADICTION.** One document simultaneously ranks two items as the #2 and #6 things to fix and marks them permanently excepted. Current source confirms both are unchanged (`src/udp_functions.cpp:540`, `src/safeboot/main.cpp:68,187`, `src/web_functions/web_functions.cpp:31`). The amateur-radio rationale for the open AP is legitimate; the ranking should have been regenerated after the decisions were taken. A reader working the list top-down wastes their time on items 2 and 6.

---

### B-6 — Nr 71: the `-Werror` exception rests on a factually false premise

`docs/code-audit-20260626.md:310`:

> | 71 | CRITICAL | platformio.ini:152,179,219 | `-Werror` fehlt | EXCEPTION | **Würde bei aktuell ignoriertem Warning-Bestand Build brechen** |
> *("would break the build given the currently-ignored stock of warnings")*

**Measured.** A clean `pio run -e heltec_wifi_lora_32_V3` emits **9 warnings**, of which **4 are in `src/`**:

```
src/Regexp.cpp:297:42            [-Wclobbered]
src/Regexp.cpp:297:64            [-Wclobbered]
src/net_console.cpp:288:5        [-Wmisleading-indentation]
src/loop_functions.cpp:3527:44   [-Wformat-truncation=]
```

The other 5 are in `TinyGPSPlus`, `BMx280MI` and `OneWire` — third-party code, which PlatformIO lets you exclude by putting the flag in `build_src_flags` instead of `build_flags`.

**Verdict: THE EXCEPTION IS UNFOUNDED.** There is no "stock of ignored warnings". `-Werror` scoped to project sources costs **four** fixes, three of them one-liners. This is the cheapest CRITICAL in the entire catalogue and it was waved through on an assumption that nobody measured.

---

### B-7 — Fixes that *did* hold (verified against current source)

Credit where due — these were checked line by line and are genuinely done:

| Fix | Claim | Current source | Verdict |
|---|---|---|---|
| D3 / Nr 9 | `AT_PRINTF` `sprintf` → `snprintf` | `src/nrf52/at_cmd.h:30` `snprintf(buff, sizeof(buff), …)` | ✅ real |
| C1 / Nr 30 | `iWrite`/`iRead` → `std::atomic<uint8_t>` | `src/loop_functions_extern.h:176–177` (+11 more atomics) | ✅ real |
| B3 / Nr 31 | nRF52 CAD flags → atomic release/acquire | `src/loop_functions_extern.h:213–215` | ✅ real |
| B5 / Nr 38 | GPS ISR off-by-one | `src/gps_functions.cpp:186` `pulseIndex + 1 < SAMPLE_COUNT` | ✅ real |
| B1 / Nr 40 | ISR `Serial.printf` gated | `src/lora_functions.cpp:31–74` `#ifdef LORA_ISR_DEBUG` | ✅ real |
| D1 / Nr 51 | HMAC plaintext bypass removed | `src/net_console.cpp:169–189` — only `ct_equal(expected, received, 32)` grants auth | ✅ real |
| D2 / Nr 52 | password masked in log | `src/net_console.cpp:171` `s_password:<***>` | ✅ real |
| A2 / Nr 58 | 8× `while(true)` in `esp32_main.cpp` | 0 hits remaining | ✅ real (but see B-1) |
| C2 / Nr 65 | ring overflow logged | `src/loop_functions.cpp:3216` | ✅ real |
| C3 / Nr 66 | main-loop task WDT | `src/esp32/esp32_main.cpp:603`, `:1745` | ✅ real |
| A1 / Nr 68 | `millis()` wraparound | 1 residual (`src/t-deck/lv_obj_functions.cpp:3639`), ~70 fixed | ✅ substantially real |
| Nr 79 | web session cleanup on millis | `src/web_functions/web_functions.cpp:240` subtraction form | ✅ real |
| D5 / Nr 87 | APRS max frame size | `src/aprs_functions.cpp:9,149` | ✅ real |
| A3 / Nr 84 | `setDio1Action()` return check | RadioLib `SX126x.h:238` → `virtual void setDio1Action(...)` | ✅ deviation is correct — genuinely returns `void` |
| 20260626 BND-02 | queueExtern silent truncation | `src/extudp_functions.cpp:510–514` now rejects + logs | ✅ real |
| 20260626 RACE-01 | externQueue `used` flag | `src/extudp_functions.cpp:55,520,528` `std::atomic<bool>` + release/acquire | ✅ real |

That is 16 of 19 sampled claims verified true. The audit process is **mostly honest**; the failures are concentrated in exactly the places where the implementer was allowed to re-adjudicate a finding without going back to the reviewer (B-1, B-2).

---

## Scanner soundness

`tools/code_audit_scan.py` is 170 lines, 11 regexes, line-oriented, no multi-line, no preprocessor, no type information, no cross-file state. Structural blind spots:

**1. It cannot see the length in a `memcpy`.** One regex, `\bmemcpy\s*\(`, tags all 149 call sites MEDIUM/"verify length before call" with zero analysis. An attacker-controlled length is indistinguishable from `memcpy(a, b, 4)`. Because ~100% of the hits are noise, BND-02 has been trained out of the reviewers' attention — visible in `docs/code-audit-20260626.md:212–218`, where six of eight `memcpy` findings are DE-PRIO/EXCEPTION on generic grounds.

**2. No check for non-literal format strings** — the very rule at `codequality-rules.md:29` and `:147`. There is no `printf\s*\(\s*[A-Za-z_]` pattern. This is how `Serial.printf(temp)` at `src/printfdeb_functions.cpp:118` survived eight audits (F6-3). A one-line `-Wformat=2` in the build found it instantly.

**3. No relation between a buffer's declared size and the bound passed to `snprintf`/`strncpy`.** `snprintf(value, 100, …)` on `char value[40]` (`web_functions.cpp:1660` vs `:1601`) is invisible; 43 such literal-bound sites exist. cppcheck flagged it in one run.

**4. `strncpy`/`strncat` are on the *approved* list, so their classic misuse is unreachable.** The scanner rewards `strncpy(d, s, strlen(s))` (no NUL termination) and `strncat(d, s, sizeof(d))` (wrong bound — must be remaining space).

**5. Zero coverage of the entire type-safety section (§11) and COMP-02.** Narrowing conversions, signed/unsigned comparisons, `strlen()` into `uint8_t` — none are regex-detectable. The `-Wconversion` probe found 425 such warnings in `src/` that the scanner reports as clean.

**6. Zero coverage of RACE-01/04/05, ISR-03, SPI-01, STAB-01/02, STK-01..04, §12, §16, §19.** The command file (`.claude/commands/code-audit.md:44–52`) delegates all of these to "Phase 2: contextual analysis" by an LLM — i.e. the sections with the highest severity are the least reproducible part of the audit, and re-running the audit does not re-derive them (2026-05-31 and 2026-06-26 both explicitly carry the 83 findings forward *without re-verifying them*: "Line numbers were not re-verified in this pass").

**7. The comment filter is indentation-luck.** `scan_file()` skips a line only if it *starts with* `//`, `*` or `/*`. Live code with a trailing comment is scanned (correct), but a commented-out block whose continuation lines happen to start with `*` is skipped while one that doesn't is flagged. Both false positives and false negatives, decided by formatting.

**8. It cannot fail a build.** The docstring says "Exit code: 0 always". There is no `--fail-on` option, so it can never be a gate — and the CI workflow does not call it anyway.

**9. Its severity model is decoupled from reality.** 4 CRITICALs are reported, all four in `t-deck-pro/ui_deckpro.cpp` `sprintf` into fixed local buffers with bounded inputs, which the 2026-05-31 audit itself downgrades to MEDIUM (`docs/code-audit-20260531.md:61`). Meanwhile the genuine CRITICAL (F6-3) scores zero. The scanner's CRITICAL count is anti-correlated with actual risk.

**10. It scans hardware that is then dismissed wholesale.** 332 files scanned; the majority of HIGH hits come from `t-deck-pro/`, `t5-epaper/`, `t-deck/`, all of which are answered "Hardware nicht im Einsatz" in the decision protocol — yet they *are* built and released by CI. Either the scan should exclude them or the exception should be withdrawn; currently it does the worst of both.

**What it does well:** the four `BND-01` string-function regexes and `SEC-01` are sound, cheap, and worth keeping as a pre-commit hook.

---

## Current warning volume

**Build command (baseline):**
```bash
pio run -e heltec_wifi_lora_32_V3 -t clean
pio run -e heltec_wifi_lora_32_V3          # flags: -Wall -Wextra (platformio.ini:158)
```
638 objects compiled (199 from `src/`). Result: **SUCCESS**, RAM 34.1 % (111,676 / 327,680 B), Flash 40.5 % (1,377,841 / 3,403,776 B).

**Total warnings: 9.**

| Category | Count | In `src/` | In third-party |
|---|---|---|---|
| `-Wunused-function` | 2 | 0 | 2 (`BMx280MI`) |
| `-Wclobbered` | 2 | 2 (`src/Regexp.cpp:297`) | 0 |
| (untagged) "extra tokens at end of `#undef`" | 2 | 0 | 2 (`OneWire`) |
| `-Wmisleading-indentation` | 1 | 1 (`src/net_console.cpp:288`) | 0 |
| `-Wimplicit-fallthrough=` | 1 | 0 | 1 (`TinyGPS++`) |
| `-Wformat-truncation=` | 1 | 1 (`src/loop_functions.cpp:3527`) | 0 |
| **Total** | **9** | **4** | **5** |

**Interpretation: the existing warnings are NOT being ignored — there is nothing to ignore.** This inverts the premise. The problem is not warning fatigue; it is that `-Wall -Wextra` on this codebase is nearly silent, so the build gives essentially no type/bounds feedback, and `-Werror` was refused (Nr 71) to protect a backlog that does not exist.

**Build command (strict probe):**
```bash
pio run -e heltec_wifi_lora_32_V3 -t clean
PLATFORMIO_BUILD_FLAGS="-Wconversion -Wsign-compare -Wshadow -Wformat=2 \
  -Wcast-align -Wold-style-cast -Wdouble-promotion -Wuseless-cast" \
  pio run -e heltec_wifi_lora_32_V3
```
**Total warnings: 29,968** — but only **1,250 (4.2 %) are in `src/`**. 28,718 come from the ESP-IDF/Arduino framework headers and libdeps.

| Flag | Total | In `src/` | Verdict |
|---|---|---|---|
| `-Wold-style-cast` | 17,106 | 604 | noise — style, not safety. Reject. |
| `-Wconversion` | 9,085 | 241 | **the flag that serves the type-safety goal**; feasible scoped to `src/` |
| `-Wuseless-cast` | 1,556 | 14 | reject |
| `-Wsign-conversion` | 643 | 0 | free — adopt |
| `-Wfloat-conversion` | 517 | 184 | relevant (ESP32 FPU is single-precision) |
| `-Wdouble-promotion` | 288 | 169 | relevant (stack + FPU-context rule at `codequality-rules.md:58`) |
| `-Wshadow` | 120 | 33 | cheap — adopt |
| `-Wcast-align` | 38 | **0** | free — adopt |
| `-Wformat=2` (`-Wformat-security`) | 1 | **1** | **1 warning, 1 real CRITICAL bug. Best signal-to-noise of any flag measured.** |

The decisive number is **4.2 %**: putting these flags in PlatformIO's `build_src_flags` (project sources only) instead of `build_flags` (everything) turns an unusable 29,968 into a tractable 1,250 — and 0 for `-Wcast-align`/`-Wsign-conversion`, 1 for `-Wformat=2`.

**cppcheck 2.11** (already installed at `~/.platformio/packages/tool-cppcheck`, usable today via `pio check`): 459 findings on the core sources, of which 4 are `error` severity:

```
error|uninitvar|src/loop_functions.cpp:1589|Uninitialized variable: nodetype
error|uninitvar|src/loop_functions.cpp:1591|Uninitialized variable: nodetype
error|bufferAccessOutOfBounds|src/web_functions/web_functions.cpp:1660|Buffer is accessed out of bounds: value
error|syntaxError|src/web_functions/web_functions.cpp:329|(parser limitation, not a defect)
```
At `--severity=high` the noise is negligible; 299 of the 459 are `cstyleCast` style hits that should be suppressed.

---

## Proposed mechanical enforcement

Ordered by (value ÷ cost), all costs measured on this tree.

| Mechanism | What it catches | Realistic here? | Expected noise | Recommendation |
|---|---|---|---|---|
| **Move warning flags to `build_src_flags`** | — (enabler) | Yes, one-line per env | −96 % of all warning volume | **Do this first.** Everything below depends on it. |
| `-Werror=format-security -Wformat=2` | Non-literal format strings (BND-01 §2, LOG §13) | Yes | **1 warning = 1 CRITICAL bug** | **Adopt as error now.** Highest-value single flag measured. |
| `-Wcast-align` | Misaligned pointer casts on Xtensa/ARM (LoadStoreAlignment panics) | Yes | **0 in `src/`** | Adopt as error — free today, prevents the next one. |
| `-Wsign-compare` (already in `-Wextra`) | Signed/unsigned comparison (COMP-02, §11) | Yes | 0 | Already on; promote to error. |
| **`-Werror` (scoped to `src/`)** | COMP-01, the rule itself | Yes | **4 fixes**, 3 one-liners | **Adopt. Overturns decision Nr 71** — the stated blocker does not exist. |
| `-Wshadow` | Shadowed locals hiding a global's update | Yes | 33 in `src/` | Adopt warning-only, ratchet to error in one sprint. |
| `pio check` = cppcheck `--severity=high --fail-on-defect=high` | uninit vars, real OOB, dead logic | **Yes — `tool-cppcheck` 2.11 already installed** | ~4 findings at high severity | **Adopt in CI now.** Suppress `cstyleCast`, `unreadVariable`, `variableScope`. |
| `-Wconversion -Wfloat-conversion -Wdouble-promotion` | Implicit narrowing — **the maintainer's goal 1** | Yes, warning-only + ratchet | 594 in `src/` | Adopt warning-only with a **count ratchet** in CI (fail if the number rises). Do not attempt `-Werror` on it. |
| `-fstack-protector-strong` | Stack smashes from the 43 literal-bound `snprintf`s | Yes on ESP32 (Flash 40.5 %, RAM 34.1 % — ample headroom). **Measure on nRF52 first (Flash 69.7 %)** | 0 warnings; ~1–3 % flash | Adopt on ESP32; gate nRF52 on a measured build. |
| ASan+UBSan on a **native** test build | Real overflows in the parsers, under fuzz | Yes — but the env does not exist yet | 0 (new infrastructure) | **Build `[env:native]`.** Rules §14/§19 already demand it and have 0 % adoption. Highest long-term value. |
| `-Wold-style-cast` / `-Wuseless-cast` | C-style casts | Technically yes | **604 + 14 in `src/`** | **Reject.** Style, not safety; guaranteed to be ignored. |
| `clang-tidy` (`bugprone-*`, `cert-*`, `concurrency-*`) | Real bug patterns, incl. some concurrency | **Partially.** `tool-clangtidy` is **not** installed here (only `tool-cppcheck`); needs `pio run -t compiledb` + `--extra-arg` massaging, and clang chokes on Xtensa builtins | high on target; low on native subset | Run on the **native subset only**, where it works cleanly. Do not fight Xtensa. |
| `-fanalyzer` | Interprocedural leaks/NULL | **No.** GCC's `-fanalyzer` is C-only; this is a C++ tree | n/a | Reject. |
| `cppcheck --addon=misra` | MISRA C:2012 | **No.** `addons/misra.py` ships, but the rule-texts file is licensed and absent, and **MISRA C does not apply to C++** | thousands | Reject. |

### The gap the flags cannot close — and the rules that would

`docs/codequality-rules.md` is a *checklist of outcomes* with no *mechanism of enforcement*. Section 11 says "NO implicit narrowing" but never names `-Wconversion`; COMP-01 asks only for `-Wall -Wextra -Werror`, which does not catch narrowing at all. That is why §11 can be 100 % violated (425 warnings) while the build is green and the rule is nominally "checked". Concretely:

**For goal 1 — make overflow structurally impossible.** Add:

- **BND-06:** *the size argument of `snprintf`/`memcpy`/`strncpy`/`strncat` MUST be `sizeof(dst)` or a `#define` declared adjacent to `dst`. Numeric literals are forbidden.* Grep-enforceable today; **43 current violations**. This is the single rule that would have caught `web_functions.cpp:1660`.
- **BND-07:** provide and mandate a decay-proof macro, so `sizeof` can never silently become `sizeof(char*)`:
  ```cpp
  #define SNPRINTF(dst, ...) \
      (static_assert(std::is_array_v<decltype(dst)>, "SNPRINTF needs an array, not a pointer"), \
       snprintf((dst), sizeof(dst), __VA_ARGS__))
  ```
  This is the mechanical form of §11 and BND-03 combined. Today the tree has **1** `static_assert` total, so BND-05 ("static_assert on all protocol struct sizes") has ~0 % adoption and no audit noticed.
- **BND-08:** every wire/protocol struct carries `static_assert(sizeof(T) == N, ...)` and `__attribute__((packed))`. Current: **0 packed structs**.
- **LOG-05:** format strings must be literals — mechanised by `-Werror=format-security`, not by review.

**For goal 2 — map state to cores.** The rules mention `portMUX`, atomics and core pinning but never require a *map*, so there is nothing to check. Add:

- **RACE-09:** every cross-task global lives in one header with an explicit ownership annotation — `CORE0_ONLY(x)`, `CORE1_ONLY(x)`, `SHARED_ATOMIC(x)`, `SHARED_MUTEX(x, lock)` — where `SHARED_ATOMIC` expands to `std::atomic<T>` and the `CORE*_ONLY` accessors carry a debug-build `assert(xPortGetCoreID() == N)`. This converts the map from documentation into a runtime-checkable invariant, and it is the only proposal here that actually answers "where are atomics genuinely needed": anything annotated `CORE*_ONLY` provably does not need one. `src/loop_functions_extern.h:176–227` is already 80 % of this header — it just lacks the annotations.
- **RACE-10:** every `xTaskCreate*` must be `…PinnedToCore` with an explicit core; `tskNO_AFFINITY` requires a written justification. Current: **8 task-creation sites, only 2 pinned** — so today the core map is *undefined by construction*, and no amount of atomics can be "exactly where needed".
- **RACE-11:** the `volatile`-is-not-enough rule (`:51`) gets a mechanical form — `volatile` on any non-MMIO variable is forbidden; grep-enforceable. Current: `src/gps_functions.cpp:172–177`, `src/lora_functions.cpp:9`.

**The enforcement point does not exist yet.** `.github/workflows/meshcom-ci.yml:3–6` triggers on `push: tags` only. There is **no PR gate and no branch gate**, and the workflow runs `pio run` with no checks, no tests and no scanner call. Every mechanism above is inert until the workflow also runs `on: pull_request`. That single change is the prerequisite for the rules being self-enforcing rather than aspirational — and it explains the whole pattern in Part B: fixes were verified by the person who wrote them, because nothing else ever verifies anything.

---

## Findings

### F6-1: Mandated CRITICAL fix (Nr 59) silently dropped; 11 `while(true);` remain in released firmware

**File:** `src/t-deck-pro/peri_lora.cpp:48,54,60,66,72,78,85,91,97,105,114`
**Severity:** HIGH (CRITICAL as originally classified)

`docs/code-audit-20260626.md:288` mandates **FIX** with the rationale "trotz T-Deck-Pro: geteilter Code-Pfad" — explicitly rejecting the T-Deck-Pro exemption. `docs/code-audit-fixes-20260627.md:70` then skips it using that exact exemption, and the A2 row is reported **✅ done** with no entry in the Deviations section.

**Failure scenario:** any SX126x init error (bad SPI, cold-solder, brownout during boot) leaves the node spinning in `while (true);` with no WDT armed — permanently dead, no reboot, no log. `t_deck_pro` is in `default_envs` and CI publishes `t_deck_pro.bin` as a release artifact (`.github/workflows/meshcom-ci.yml:72,108`), so this ships to users.

**Fix:** apply the same treatment as `esp32_main.cpp` — replace each `while (true);` with a logged `esp_restart()`; or, if the board is truly unsupported, remove it from `default_envs` and from the CI artifact list so the "not our target" claim becomes true.

---

### F6-2: Mandated fix (Nr 32) recorded as done after being reclassified to a no-op

**File:** `src/gps_functions.cpp:172–177`
**Severity:** MEDIUM (process) / LOW (technical, on nRF52)

`volatile unsigned long pulseTimes[SAMPLE_COUNT]` etc. are unchanged. The fix report justifies "no change needed" with single-core reasoning, while `docs/codequality-rules.md:51` states `volatile` alone is insufficient, and this file compiles into dual-core ESP32 targets.

**Failure scenario:** low — ISR↔task on one core makes torn reads unlikely. The real exposure is process: a HIGH finding was closed by the implementer without reviewer sign-off, inside a document reporting "14 of 17 resolved".

**Fix:** either convert to `std::atomic` / a `portMUX` critical section, or re-open Nr 32 and have the reviewer record an explicit EXCEPTION with the single-core rationale. Do not leave it as a ✅.

---

### F6-3: Format-string injection from over-the-air data — `Serial.printf(temp)`

**File:** `src/printfdeb_functions.cpp:118`
**Severity:** CRITICAL — **new; missed by all eight prior audits and by the scanner**

```cpp
int len = vsnprintf(temp, sizeof(loc_buf), nformat, copy);   // :96  — user text rendered into temp
...
Serial.printf(temp);                                          // :118 — temp used AS the format string
```

`temp` holds already-formatted log output. Remote data reaches it directly, e.g. `src/lora_functions.cpp:820` `printfdeb("[MESHx]...TEXT:%s\n", …, aprsmsg.msg_payload.c_str())` and `:471` `printfdeb("[LORA-ERROR]...%03i RCV:%s\n", size, RcvBuffer+6)`.

**Failure scenario:** a peer transmits a LoRa message whose text contains `%s%s%s%s` or `%n`. On the second `Serial.printf`, those directives are interpreted against a `va_list` with no arguments → out-of-bounds stack reads printed to the console, and `%n` is an arbitrary write. Any node with `bLORADEBUG` enabled can be crashed by a single crafted packet from any station in radio range. Violates `codequality-rules.md:29` ("Format strings: ALWAYS string literals, NEVER variables") and `:147`.

**Fix:** `Serial.printf("%s", temp);` — one character class change. Then add `-Wformat=2 -Werror=format-security` so it cannot recur.

**Secondary (MEM-01):** `:103` `temp = (char*) malloc(len+1)` allocates in the logging path on every oversized line — allocation outside `setup()`, contrary to `codequality-rules.md:12`.

---

### F6-4: `snprintf` bound larger than the destination buffer

**File:** `src/web_functions/web_functions.cpp:1660` (buffer declared `:1601`)
**Severity:** MEDIUM (latent)

```cpp
char value[40];                                                  // :1601
snprintf(value, 100, "%s", meshcom_settings.node_mcp17t[io]);    // :1660
```

Currently safe only because `node_mcp17t` is `char[16][16]` (`src/esp32/esp32_flash.h:88`), so at most 16 bytes are written. The bound is a lie: the moment that field is widened past 40, this becomes a 60-byte stack smash with no warning. Found by cppcheck (`bufferAccessOutOfBounds`); invisible to `tools/code_audit_scan.py`. Violates BND-03 ("Always `snprintf()` with correct size parameter"). **42 further sites** pass a numeric literal instead of `sizeof(dst)`.

**Fix:** `snprintf(value, sizeof(value), "%s", …)`, and adopt proposed rule BND-06 to make the class impossible.

---

### F6-5: `-Werror` refused on a premise that measurement contradicts

**File:** `platformio.ini:158, 185, 225`
**Severity:** MEDIUM (meta — but it is the gate that would have caught F6-3 and F6-4)

Decision Nr 71 (`docs/code-audit-20260626.md:310`) excepts COMP-01 because `-Werror` "would break the build given the currently-ignored stock of warnings". Measured: 9 warnings total, 4 in `src/`, all trivial.

**Failure scenario:** the build provides no type or bounds feedback, so §11 and COMP-02 are 100 % unenforced (425 conversion-class warnings appear the moment `-Wconversion` is added), and a remotely-triggerable format-string bug (F6-3) lived through eight audits.

**Fix:** move warning flags into `build_src_flags` (drops noise 96 %), fix the 4 warnings, add `-Werror -Wformat=2 -Wcast-align`; add `-Wconversion` warning-only behind a CI count ratchet. Re-open Nr 71.

---

### F6-6: No CI quality gate — every mechanism proposed here would be inert

**File:** `.github/workflows/meshcom-ci.yml:3–6`
**Severity:** MEDIUM (meta)

The only workflow triggers on `push: tags`. There is no `pull_request` or branch trigger, and the job runs `pio run` with no `pio check`, no test step, and no call to `tools/code_audit_scan.py` (which cannot fail anyway — "Exit code: 0 always").

**Failure scenario:** nothing verifies a change before merge. This is the structural cause of the Part B pattern: fixes are validated only by their author, so a mandated CRITICAL can be marked ✅ while remaining in the tree (F6-1).

**Fix:** add `on: pull_request` + `push: branches: [dev, v4.35p_prio]`; build 2 representative envs (one ESP32, one nRF52) with `-Werror`; add `pio check --severity=high --fail-on-defect=high`; give the scanner a `--fail-on {CRITICAL,HIGH}` exit code and call it.

---

### F6-7: The rule set and the contribution policy are in unresolved conflict

**Files:** `docs/codequality-rules.md:6` vs `CLAUDE.md` "Minimal Changes Only"
**Severity:** LOW (governance) — but it is the stated reason a CRITICAL is still open

`codequality-rules.md:6` says violations "are treated as bugs". `CLAUDE.md` mandates cherry-picking "the absolute minimum" and forbids refactoring. `docs/code-audit-fixes-20260627.md:88–92` defers the CRITICAL APRS FCS-ordering fix (Nr 86) precisely because fixing it "requires restructuring, which violates the minimal-changes rule".

**Failure scenario:** any finding whose correct fix is structural is permanently unfixable, and the CRITICAL backlog can only grow. Four audits have now carried Nr 86 forward.

**Fix:** state the precedence explicitly — e.g. "minimal-changes governs upstream PRs; CRITICAL rule violations may be fixed structurally on a dedicated branch" — and give Nr 86 a scheduled owner rather than an indefinite deferral.

---

### F6-8: Rules §14/§19 (pure functions, fuzz-ready parsers, boundary tests) have zero adoption

**Files:** `test/` (contains `compress_functions.cpp` + a stray TinyGSM header, no tests), `platformio.ini` (no `[env:native]`)
**Severity:** MEDIUM

`NATIVE_BUILD`: 0 occurrences. No native environment, no unit tests, no boundary tests. §19 demands parsers taking `(const uint8_t*, size_t)` that "never crash, never hang" on malformed input, and §17 demands both branches of `#ifdef NATIVE_BUILD` be tested — none of this exists, and no audit has ever scored these sections.

**Failure scenario:** the APRS/compress/ring-buffer parsers — which consume untrusted radio data — have never been executed against malformed input. This is also why F6-3 was never caught: there is no test that feeds `%n` into a message field.

**Fix:** add `[env:native]` compiling `aprs_functions`, `compress_functions` and the ring buffer with `-fsanitize=address,undefined`, plus Unity tests at 0/1/max/max+1 bytes. This is the prerequisite for ASan/UBSan and the only realistic home for clang-tidy on this project.

---

### F6-9: Documented priority list contradicts the decision table inside the same audit

**File:** `docs/code-audit-20260626.md:169–176` vs `:268–269, :371`
**Severity:** LOW

Items ranked #2 (`WiFi.softAP` open) and #6 (`web_header` unbounded) in the carried-forward priority list are marked permanent **EXCEPTION** 100 lines earlier in the same document. Both are unchanged in the current tree (`src/udp_functions.cpp:540`, `src/safeboot/main.cpp:68,187`, `src/web_functions/web_functions.cpp:31`).

**Fix:** regenerate the ranking from the decision table after each review; excepted items belong in a separate "accepted risks" list, not in the fix queue.

---

## Architecture-document consistency (`docs/architecture/README.md` + `01`–`07`)

The architecture concept is a good document set — the metric disclaimers (`README.md:34–37`, `04:8–11`) are unusually honest, and its CI diagnosis is correct. But its treatment of code quality contradicts the rules and the audits in five ways.

### AC-1 — Severity inversion: the two remediation rankings are disjoint

Every ranking table in `docs/architecture/` is sorted by a *refactoring* key, never by defect severity:

- `04-complexity-and-duplication.md:204` — `| # | Action | Deletes / unifies | Effort | Risk |` (Risk = risk of the refactor, not of the defect)
- `02-build-and-variants.md:174` — `| # | Change | Effort | Value |`
- `03-dependencies.md:201` — `| Step | Change | Blast radius | Verification needed |`
- `01-system-overview.md:180` — "in order of **leverage**"

No table anywhere carries a severity, a rule ID, or a CRITICAL/HIGH/MEDIUM column. Compare the two top-10 lists:

| Architecture top items (`04:206–213`, `02:174–183`) | Audit top items (`code-audit-20260626.md:404–423`, `20260531.md:112–124`) |
|---|---|
| merge two `peri_gps.cpp` copies (~380 LOC) | `millis()` wraparound (★★ confirmed field failure) |
| merge `scr_mrg` (~270 LOC) | LoRa ring-buffer `volatile` race |
| merge three `power_controls.cpp` (~140 LOC) | `Serial.printf()` in `OnRxDone` ISR |
| move mheard logic out of `ui_deckpro.cpp` | `at_cmd.h:30 sprintf` |
| pin `nordicnrf52`, fix a src_filter typo | `WiFi.softAP` without password |

**The two lists share not a single item.** A maintainer working the architecture list top-down would spend the first four work packages on cosmetic file merges.

Sharpest instance: `04-complexity-and-duplication.md:122–123` states

> "This function is the highest-consequence code in the firmware (it is on the path of every received packet from an untrusted RF source) and it is the least testable."

…about `OnRxDone()` — and then ranks splitting it **8 of 9** (`04:213`), below all four file merges. And the one CRITICAL still open on exactly that path — APRS FCS validated *after* field parsing (`code-audit-20260626.md:350`, deferred at `code-audit-fixes-20260627.md:88–92`) — **is not mentioned anywhere in the architecture set.**

### AC-2 — The rule set is cited once, for one rule, and immediately downgraded

`01-system-overview.md:146–147` is the **only** citation of `docs/codequality-rules.md` in all eight documents, and it quotes MEM-03. Ten lines later, `01:155–157`:

> "The project's own rules and the project's own core data structure contradict each other. This is worth recording as a **known, accepted debt rather than pretending it is a bug**…"

against `docs/codequality-rules.md:6`:

> "These rules are MANDATORY. **Violations are treated as bugs.**"

The remaining 19 rule categories (BND, RACE, ISR, SPI, STAB, COMP, Type Safety, Lifetime, Logging, Protocol, State Machines, Data Drift, TCP/Web, Test Readiness, STK) are never named; no rule ID ever appears; the Audit Checklist at `codequality-rules.md:208–233` is never referenced. The architecture concept therefore does not know it is describing a tree with 20 failing rule sections.

Related: **no audit number is carried across at all.** Four references to `docs/code-audit-*.md` exist (`01:107`, `06:22`, `06:163`, `06:186`), none with a count, date, ID or line. A reader of the architecture set alone would not learn that 83 findings are open or that 14 were CRITICAL.

### AC-3 — The `-Werror` question the architecture doc poses is now answered, and the answer reverses its advice

`06-test-strategy.md:79–80`:

> "Add `-Werror` for a curated warning subset **later, not immediately** — `-Wall -Wextra` is already on and **the existing warning volume should be measured before it becomes blocking**."

This was the epistemically correct position: defer pending measurement. **The measurement has now been taken: 9 warnings, 4 in `src/`.** The condition the doc set attached is discharged, and it discharges in favour of doing it now. `-Werror` in `build_src_flags` costs four fixes. Both `06:79–80` and audit decision Nr 71 (`code-audit-20260626.md:310`, which asserts a large "ignored warning stock" that does not exist) should be updated.

Note the difference in kind: the architecture doc treats `-Werror` as a *sequencing preference*; the audit records the same outcome as a **CRITICAL-severity accepted exception**. Same conclusion, incompatible framing.

### AC-4 — There is no core model anywhere, which directly blocks the maintainer's second goal

The maintainer's stated goal is to "map which state is touched by which CPU core so that atomics are used exactly where genuinely needed". The architecture concept is the document that would own that map. It does not contain one:

**The strings "dual-core", "single-core", "core 0", "core 1", "PRO_CPU", "APP_CPU", "pinnedToCore" and "xTaskCreate" do not appear in any of the eight documents.**

Its entire concurrency treatment is one bullet, `01-system-overview.md:104–108`:

> "Only 12 of 423 globals are `std::atomic`; there is one `portMUX_TYPE` (`displayMux`), one mutex (`net_console.cpp`), one queue (`bleQueue`, 5 slots). Everything else shared between the NimBLE task, the radio ISR callback and the main loop is unsynchronised by construction."

Verified accurate: 12 `extern std::atomic<...>` declarations, `portMUX_TYPE displayMux` at `src/lora_functions.cpp:122` (the only one), `xSemaphoreCreateMutex` only in `net_console.cpp:275,284`. The model it names — NimBLE task / radio ISR / main loop — is a *context* model, not a *core* model. Meanwhile `codequality-rules.md:50–52` and `code-audit-20260626.md:104–114` both reason explicitly about Xtensa dual-core store buffers.

Consequence: the architecture set cannot tell you where an atomic is genuinely needed versus merely defensive, and it does not record that the 2026-06-27 round converted `scanFlag`, the CAD flags and `iWrite`/`iRead` to atomics — very likely most of the 12 it counts. **Proposed rule RACE-09/RACE-10 (see above) is the missing artefact, and `src/loop_functions_extern.h:176–227` is already 80 % of it.**

### AC-5 — Where the architecture set is right, and corroborates this review

- **CI gap.** `02:162–170` ("the cheapest high-value fix in the whole repository: add a `pull_request` trigger and a build matrix") and `06:65–78` independently reach F6-6. Two reviews converging on the same root cause should settle it.
- **No tests.** `06:11` "**There are no automated tests.** Not 'few' — zero." Confirms F6-8 exactly; `06:84–97` even drafts the `[env:native]` with `-DNATIVE_BUILD` that §17 and §19 require.
- **Untrusted-input test targets.** `06:152–155` and `07:465–466` identify frame-length-field handling and malformed-frame rejection as required assertions — the same class as F6-3/F6-4.
- **Unintentional confirmation of a fix.** `07:60–63` notes that `LORA_ISR_DEBUG` "is **not defined anywhere in the repository**" and treats it as an undocumented test hook. It is in fact the residue of audit fix B1 (Nr 40) — which means that fix is not merely present but *unconditionally* effective in every shipped build. Verified good.

### AC-6 — Two smaller inconsistencies

- **Library pinning.** `codequality-rules.md:124` and `:179` both require "ALL pinned exactly (no `^` or `~`)". The architecture remediation step 3 (`03:207`) is caret **alignment**, not exact pinning. Executed exactly as written, the plan leaves all 87 floating constraints in place — it makes the violation tidier rather than fixing it.
- **`net_console.cpp` described in security-positive terms.** `07:108` — "Challenge-response; password never transmitted" — with no note that this file carried the HMAC plaintext bypass (Nr 51) and the plaintext-password log (Nr 52) until five weeks before the document was written. The statement is true of today's source (verified at `src/net_console.cpp:169–189`), but it reads as an endorsement of a file with very recent security history.
- **Tooling defect in the metric scripts.** `tools/arch_duplication.py:11` defines `SKIP_DIRS = {...,"src"}` but `main()` never reads it — the filter actually applied is the inline literal at `:33`. The comment documents behaviour that does not exist. `tools/arch_metrics.py:14` skips only three directories and does not filter `Font_*`/`img_*`/`firasans*` by filename, so font/bitmap headers sit inside the "1,032 functions parsed" baseline (`04:6`). Both inflate the architecture numbers slightly; `README.md:34–37` already warns against quoting them exactly, so this is a caveat, not a refutation.

---

### F6-10: Architecture remediation ranking is ordered by deletable LOC, inverting the audit's safety ranking

**Files:** `docs/architecture/04-complexity-and-duplication.md:204–218`, `:122–123`; vs `docs/code-audit-20260626.md:404–423`
**Severity:** MEDIUM (governance)

The two ranked backlogs share zero items. The architecture set ranks by "Deletes / unifies" LOC and effort; the audits rank by safety severity. `OnRxDone()` is called "the highest-consequence code in the firmware ... and the least testable" (`04:122–123`) and ranked 8 of 9, below four file merges. The one open CRITICAL on that path (APRS FCS-before-parse) is absent from the architecture set entirely.

**Failure scenario:** a maintainer following the architecture plan top-down spends the first four work packages on cosmetic merges while a confirmed-severity backlog ages. Conversely, an auditor following the audit list gets no credit for the structural work that would make the fixes durable.

**Fix:** produce one merged backlog with two explicit columns — *defect severity* (from the audits) and *structural leverage* (from the architecture docs) — and state the tie-break rule. Add the deferred CRITICAL (Nr 86) to `04`'s item 8, since splitting `OnRxDone()`/`decodeAPRS()` is exactly the restructuring that unblocks it.

---

### F6-11: The architecture concept contains no CPU-core model, blocking the stated goal of core-scoped atomics

**File:** `docs/architecture/01-system-overview.md:104–108` (the entire concurrency treatment)
**Severity:** MEDIUM

No architecture document contains the strings "dual-core", "core 0", "core 1", "PRO_CPU", "APP_CPU" or "pinnedToCore". The concurrency model given is by *execution context* (NimBLE task / radio ISR / main loop), not by core — while `codequality-rules.md:50–52` and `code-audit-20260626.md:104–114` both turn on the dual-core distinction.

**Failure scenario:** "atomics exactly where genuinely needed" is undecidable from the current documentation. Measured supporting evidence: 8 `xTaskCreate*` sites of which **only 2 are pinned** to a core, so the core assignment of most firmware state is undefined by construction — no annotation scheme can be applied until that is fixed.

**Fix:** add a core-ownership section to `01-system-overview.md` enumerating each shared global's owning context *and core*; adopt RACE-09 (annotation macros in `src/loop_functions_extern.h`, which already holds 12 of the 13 atomics) and RACE-10 (mandatory `xTaskCreatePinnedToCore`). Record the 2026-06-27 atomics work as the baseline.

---

### F6-12: `codequality-rules.md` is a checklist of outcomes with no named mechanism, so 20 sections can fail while the build stays green

**File:** `docs/codequality-rules.md` (whole document)
**Severity:** MEDIUM (meta — the root cause of the conformance matrix above)

Section 11 mandates "NO implicit narrowing" but never names `-Wconversion`; COMP-01 asks only for `-Wall -Wextra -Werror`, which does not detect narrowing at all. BND-05 mandates `static_assert` on protocol struct sizes — the tree has **1** `static_assert` and **0** packed structs, and eight audits never scored it. §19 mandates fuzz-ready parsers and boundary tests — **0** exist. §14 mandates natively-testable pure functions — **0** `NATIVE_BUILD` occurrences.

**Failure scenario:** rules whose enforcement mechanism is "an auditor reads the code" decay to 0 % adoption without anyone noticing, because the audit that would notice is the same unreproducible manual pass. The sections with the highest severity (RACE, ISR, STK, §12/§16/§19) are precisely the ones `tools/code_audit_scan.py` cannot check and `.claude/commands/code-audit.md:44–52` delegates to LLM judgement.

**Fix:** annotate every rule with its enforcement mechanism, and delete or demote any rule that has none. Concretely add BND-06/07/08, LOG-05, RACE-09/10/11 as specified above, each with the flag, grep, `static_assert` or CI step that proves it — then wire those into the `pull_request` gate from F6-6.
