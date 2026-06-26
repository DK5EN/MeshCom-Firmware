# MeshCom Firmware Code Audit

**Date:** 2026-06-26
**Branch:** v4.35p_prio
**HEAD:** b67e6d32
**Upstream base:** d3af8986 (upstream/dev — after rebase 2026-06-26)
**Scope:** changed files vs upstream/dev (2 files: `src/configuration_global.h`, `src/extudp_functions.cpp`)
**Rules:** docs/codequality-rules.md
**Previous audit:** docs/code-audit-20260531.md (2026-05-31, 83 findings carried from 2026-05-25)
**Auditor:** Claude Code (automated, two-phase scan + delta)

---

## Delta vs. 2026-05-31

The upstream commits integrated in this window (871da1ad → d3af8986, 8 functional commits
via PRs #1009/#1010/#1013/#1014/#1016/#1017/#1018/#1021) affect Heltec V3 init, T-DECK
SETUP, via/routing, and a new Heltec E213 board. None of these touch our two changed files.

Our changed files carry two PR-scoped patches:
- `fix: stack overflow in sendExtern() — platform-conditional buffer allocation`
- `fix(sendExtern): platform-conditional buffer allocation` (same logical patch)
- New: `queueExtern()` + `flushExternQueue()` deferred-send ring buffer

**`src/configuration_global.h`**: only `FLASH_VERSION` updated (20260608 → 20260626). No
code quality impact.

| Status | Count | Notes |
|--------|-------|-------|
| New findings (our code) | 2 | BND-02 silent truncation; RACE-01 externQueue sync |
| Resolved | 0 | no fixes in this window |
| Existing (inherited, pre-upstream) | 5 | MEM-03 String globals in extudp_functions.cpp |
| Full-src carry-forward | 83 | unchanged from 2026-05-25 (not re-enumerated here) |

---

## Audit Summary

| Category | Rule IDs | Critical | High | Medium | Low |
|----------|----------|----------|------|--------|-----|
| Buffer Safety | BND-01..05 | 0 | 0 | 1 | 0 |
| Memory Safety | MEM-01..05 | 0 | 0 | 5 | 0 |
| Thread Safety | RACE-01..08 | 0 | 0 | 1 | 0 |

**Delta total: 7 findings in changed files (0 CRITICAL, 0 HIGH, 7 MEDIUM, 0 LOW)**
(2 newly introduced by our code, 5 inherited pre-existing from upstream)

---

## Findings

### src/extudp_functions.cpp

---

#### MEDIUM BND-02 — memcpy without reject on oversized input [NEW — our code]

**Line 509–510:**
```cpp
if(buflen > 500) buflen = 500;
memcpy(entry->buffer, buffer, buflen);
```

**Context:** `queueExtern()` — new function introduced by our sendExtern deferral patch.
Copies incoming LoRa packet into ring buffer slot before handing off to main loop.

**Violation:** BND-02: "All `memcpy()` calls: validate length BEFORE copying, assert
`len <= buffer_size`." The current code clamps silently instead of rejecting. If a caller
passes `buflen > 500`, the data is truncated without any log or error signal, and the
truncated (corrupt) packet is forwarded via UDP.

**Assessment:** Low real-world risk — the LoRa protocol caps packet length well below 500 B.
But the silent truncation pattern violates the rule and hides over-length packets. A packet
larger than 500 B would be forwarded with the tail cut off, no warning emitted.

**Fix:**
```cpp
if(buflen > sizeof(entry->buffer)) {
    Serial.printf("[EXT] queueExtern: buflen %u > %u, dropped\n", buflen, sizeof(entry->buffer));
    return;
}
memcpy(entry->buffer, buffer, buflen);
```

---

#### MEDIUM RACE-01 — externQueue used-flag set without memory barrier [NEW — our code]

**Lines 506–530:** `queueExtern()` + `flushExternQueue()`

```cpp
// producer (queueExtern — called from radio callback / OnRxDone):
memcpy(entry->buffer, buffer, buflen);
entry->buflen = buflen;
// ... more field writes ...
entry->used = true;          // ← store-store ordering not guaranteed

// consumer (flushExternQueue — called from main loop):
if(externQueue[i].used)      // ← can see used=true before buffer contents
    sendExtern(...);
entry->used = false;
```

**Context:** Classic SPSC (single-producer, single-consumer) ring buffer pattern. On nRF52
(single-core, cooperative scheduling) this is safe. On ESP32 dual-core (Xtensa LX6/LX7
with out-of-order store buffers), the CPU can make `entry->used = true` visible on the
other core before the preceding `memcpy` / field writes are visible. This allows the
consumer to read partially-written buffer contents.

**Violation:** RACE-01: "NEVER access shared data from two tasks without synchronization."
The `used` bool is the only synchronization primitive, and it is not `std::atomic<bool>`.

**Assessment:** Latent bug, low probability of manifesting on ESP32-S3 in practice due to
write-combining behavior, but non-zero on dual-core builds. Not a problem on nRF52.

**Fix — minimal:** Mark `used` as `std::atomic<bool>` and use `store(true,
memory_order_release)` / `load(memory_order_acquire)`:
```cpp
struct externQueueEntry {
    uint8_t  buffer[500];
    uint16_t buflen;
    int16_t  rssi;
    int8_t   snr;
    char     src_type[8];
    std::atomic<bool> used{false};
};
// producer:
entry->used.store(true, std::memory_order_release);
// consumer:
if(externQueue[i].used.load(std::memory_order_acquire))
```

---

#### MEDIUM MEM-03 (×5) — Arduino String globals and locals [EXISTING — pre-upstream code]

Pre-existing upstream code, not introduced by our patches. Flagged because the file
differs from upstream/dev. Documented for completeness; not a regression.

| Line | Code | Classification |
|------|------|----------------|
| 25 | `String s_extern_node_ip = ""` | global String, initialisation path only; low heap churn risk |
| 27 | `String strExtOutput` | global accumulator used in `strEsc()` — not re-entrant |
| 28 | `String str_ip` | global, used in `startExternUDP()` for DNS fallback |
| 457 | `String strKurz = c_json` | local in `sendExtern()`, debug-log path only; created, substring-sliced, then destroyed |
| 484 | `String strKurz = c_tjson` | same pattern, telemetry log path |

**Rule:** MEM-03: "String handling: fixed `char[]` arrays — NEVER Arduino `String` in hot
paths."

**Assessment:** Lines 25/27/28 are cold-path (init, error escape). Lines 457/484 are inside
`sendExtern()` which runs on each received packet — moderate hot path. All cause small heap
allocations on every call; on nRF52 with constrained heap this contributes to fragmentation.
Line 27 (`strExtOutput`) is additionally non-re-entrant: `strEsc()` writes to it as a
global accumulator, then returns it — unsafe if ever called from two contexts.

**Fix for hot-path (lines 457/484):** Replace with `strnlen` + direct pointer arithmetic,
or a fixed `char[10]` preview buffer for the log print. Lines 25/27/28: convert to
`char[]` with `snprintf` for init assignment.

---

## Full-src carry-forward

The 83 findings from docs/code-audit-20260525.md remain open and unchanged. Line numbers
were not re-verified in this pass (scope is PR delta only). No upstream commit in this
window touched any of the previously catalogued sites.

Priority ranking unchanged from 2026-05-31:
1. **nrf52/at_cmd.h:30 `sprintf`** — CRITICAL, trivial one-line fix
2. **WiFi.softAP without password** — CRITICAL, 3 sites
3. **net_console.cpp:174 plaintext password bypass** — HIGH/SECURITY
4. **millis() wraparound** — CRITICAL, ~33 sites
5. **APRS FCS before parsing** — CRITICAL
6. **web_header unbounded concat** — CRITICAL

---

## Fazit

Der aktuelle PR-Scope (2 Dateien) erzeugt **keine neuen CRITICAL- oder HIGH-Befunde**.
Die beiden neuen MEDIUM-Befunde stammen aus dem neuen `queueExtern`-Code:

- **BND-02** (stille Trunkierung): geringes Risiko, da LoRa-Pakete die 500-B-Grenze
  nicht erreichen, aber gegen Coding Rules. Einzeiler-Fix (reject statt clamp).
- **RACE-01** (fehlende Memory-Ordering-Garantie): latenter Bug auf ESP32 Dual-Core,
  harmlos auf nRF52. Fix: `std::atomic<bool>` mit acquire/release semantics.

Die 5 MEM-03-Treffer sind vererbter Upstream-Code — keine Regression durch unsere
Patches. Die globale Befundlage (83 Open-Findings aus 2026-05-25) bleibt unverändert.
