# MeshCom Firmware — Audit Fix Implementation

**Date:** 2026-06-27
**Branch:** v4.35p_prio
**Base:** `4893b8b9` (docs: code audit 2026-06-26 — Entscheidungsprotokoll Review)
**HEAD after fixes:** `9437307a` (pushed to origin/v4.35p_prio)
**Scope:** 17 mandatory fixes from the carry-forward audit (docs/code-audit-20260525.md,
priority list re-confirmed in docs/code-audit-20260626.md)
**Implementer:** Claude Code — 4 parallel worktree subagents (Groups A–D, file-exclusive)
+ 1 sequential post-merge step for the cross-cutting atomic changes (C1/B3)

**Build verification:** clean full build, both mandated targets green
- `heltec_wifi_lora_32_V3` (ESP32-S3): RAM 34.1 %, Flash 40.4 %
- `wiscore_rak4631` (nRF52): RAM 32.9 %, Flash 69.7 %

---

## Result

**14 of 17 resolved** (13 code changes + B4 already satisfied). A3 is a structural no-op,
D4 is deferred (see Deviations).

| Fix | Description | Status | File(s) | Commit |
|-----|-------------|--------|---------|--------|
| A1 | millis() wraparound → `(uint32_t)(millis()-start) >= iv` (**70** sites) | ✅ done | esp32_main, nrf52_main, lora, loop, gps, adc, batt, web_functions:240, mheard | all 4 |
| A2 | `while(true)` after radio-init error → `esp_restart()` (8 sites) | ✅ done | esp32/esp32_main.cpp | f121f3a1 |
| A3 | setDio1Action() return value check | ⚠️ no-op | — | — |
| B1 | OnRxDone ISR logs gated `#ifdef LORA_ISR_DEBUG` | ✅ done | lora_functions.cpp | c8d5eb91 |
| B2 | scanFlag → `std::atomic<bool>` | ✅ done | esp32/esp32_main.cpp | f121f3a1 |
| B3 | nRF52 CAD flags → `std::atomic<bool>` (release/acquire) | ✅ done | nrf52_main, loop_functions_extern.h | 9437307a |
| B4 | pulseTimes ISR race | ✅ already `volatile` (no change) | gps_functions.cpp | — |
| B5 | GPS ISR off-by-one → `pulseIndex + 1 < SAMPLE_COUNT` | ✅ done | gps_functions.cpp | c8d5eb91 |
| C1 | iWrite/iRead → `std::atomic<uint8_t>` | ✅ done | loop_functions(.cpp/_extern.h), lora_functions, nrf_eth | 9437307a |
| C2 | Ring-buffer overflow log | ✅ done | loop_functions.cpp | c8d5eb91 |
| C3 | Main-loop task watchdog (`esp_task_wdt_add/reset`) | ✅ done | esp32/esp32_main.cpp | f121f3a1 |
| D1 | Remove HMAC plaintext-bypass | ✅ done | net_console.cpp | 6ba4f3c7 |
| D2 | Mask password in serial log | ✅ done | net_console.cpp | 6ba4f3c7 |
| D3 | sprintf → snprintf (AT_PRINTF) | ✅ done | nrf52/at_cmd.h | 704b8ef3 |
| D4 | APRS FCS check before parsing | ❌ deferred | aprs_functions.cpp | — |
| D5 | APRS max-frame-size check (`MAX_APRS_FRAME_SIZE 340`) | ✅ done | aprs_functions.cpp | 6ba4f3c7 |

A1 background: a `(timer + interval) < millis()` comparison froze a node after 49 days of
continuous operation — confirmed field failure. All 70 such comparisons across the
build-verifiable files were rewritten to the subtraction form.

---

## Commits (one per work-unit)

```
f121f3a1  fix(esp32_main): A1, A2, B2(scanFlag), C3
704b8ef3  fix(nrf52):      A1, D3
c8d5eb91  fix(lora/ring/gps): A1, B1, B5, C2
6ba4f3c7  fix(security/aprs): D1, D2, D5, A1
9437307a  fix(ring/cad):   C1, B3 (atomics)
```

Commit strategy: one commit per work-unit rather than per thematic group A/B/C/D — the
file-exclusive subagent split places several groups inside single files (e.g.
`esp32_main.cpp` carries A1+A2+B2+C3), so clean per-group commits would require manual
hunk-staging. The A/B/C/D rationale belongs in the (German) upstream PR description.

---

## Decisions

- **A1 scope:** only build-verifiable files (compiled by heltec V3 or rak4631). Excludes
  board files not built by either target: `t-deck/`, `t5-epaper/`, `Platforms/VisionMaster*`,
  `safeboot/`, and `t-deck-pro/peri_lora.cpp`.
- **A2:** 8 radio-init sites in `esp32_main.cpp`; `t-deck-pro/peri_lora.cpp` (11) excluded
  (not built by either mandated target → unverifiable).
- **B2:** `scanFlag` → `std::atomic<bool>` only. `transmissionState` left `volatile int`
  (already volatile; it is an `int` RadioLib error-code extern'd in `lora_functions.cpp` +
  `lora_setchip.cpp`, so converting would ripple across 3 files for marginal benefit).

---

## Deviations

- **A3 (no-op):** RadioLib's `setDio1Action(void(*)(void))` returns **`void`**
  (SX126x.h:238 / SX127x.h:723) — there is no return value to check. Not implementable as
  specified; no behavioural change.
- **B1:** the cited raw `Serial.printf` in OnRxDone had already been refactored to
  `bLORADEBUG`-gated `printfdeb` upstream; an additional compile-time `#ifdef LORA_ISR_DEBUG`
  gate was added on top (the block is `#if defined BOARD_RAK4630`).
- **B4:** `pulseTimes`/`pulseIndex` are already `volatile`; the GPS ISR is single-core
  (ISR↔task, barrier via `detachInterrupt`) → satisfied without change.
- **D4 (deferred — OPEN):** "validate FCS before parsing" is not minimally achievable. The
  FCS coverage end-offset (`inext` in `decodeAPRS`) is computed *during* the variable-length
  source/destination/payload parse loops, so it is not known at function entry. Relocating
  the check requires a separate length-determination pass — i.e. restructuring, which
  violates the minimal-changes rule. Left at its original location pending a decision.

---

## C1 / B3 — atomic ripple notes

- **C1:** `addRingPointer(volatile int&, ...)` cannot bind a `std::atomic`, so the single
  tx-ring call in `lora_functions.cpp` was inlined (the helper stays unchanged for the other
  rings). `DEBUG_MSG_VAL("RADIO", (int)iWrite, …)` got an explicit cast. All remaining
  accesses use atomic's implicit (seq_cst ≥ release/acquire) operators. Correctness depends
  on every TU seeing the atomic type — verified via a **clean full build** of both targets.
- **B3:** publish in `OnCadDone` uses `cad_done_flag.store(true, memory_order_release)`;
  reads use seq_cst (≥ acquire).

---

## Open items

1. **D4** — APRS FCS-before-parse (deferred; needs a small length-determination pass).
2. **Upstream PR** against `icssw-org` DEV with detailed German description — not yet created.

## Process note

A first parallel run was discarded: `isolation: worktree` branched 3 of 4 subagents from a
~40-commit-stale upstream merge-base instead of current HEAD. The run was redone on
worktrees manually pinned to the reviewed SHA. (Side effect of the discarded round: those
subagents installed RAK board definitions into the global `~/.platformio` — outside the repo.)
