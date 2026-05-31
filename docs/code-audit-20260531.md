# MeshCom Firmware Code Audit

**Date:** 2026-05-31
**Branch:** v4.35p_prio (rebased onto upstream `dev` HEAD = eba328b4)
**HEAD:** 02487f18 (our release.md doc commit on top of the rebase)
**Upstream base:** eba328b4 (`dev`) — incorporates merge PRs #957–#964
**Local src commits on top:** 85712628, 2b26caae (sendExtern stack-overflow fix + platform-conditional allocation — unchanged since 2026-05-25, only re-hashed by the rebase)
  (all other local commits are docs/tools — no `src/` changes)
**Auditor:** Claude Code (automated, two-phase scan + delta)
**Rules:** docs/codequality-rules.md
**Previous audit:** docs/code-audit-20260525.md (2026-05-25, 83 findings, base b0c26176)

---

## Delta vs. 2026-05-25 — essentially nil

The window between the previous audit (base `b0c26176`, tag v4.35p.05.23) and this one
(base `eba328b4`, `dev`) contains exactly **one** substantive upstream commit:

- **`4a0fe3e2` v4.35p web_function** — swaps the two display labels "APRS Symbol" and
  "APRS Group" in the web setup page (`src/web_functions/web_functions.cpp:1147–1150`,
  `sub_page_setup()`). Pure UI text change, 2 insertions / 2 deletions at the same
  location. **No logic change, no new finding, no line-number shift** for any of the
  web_functions.cpp findings carried forward from 2026-05-25 (refs at 31, 208, 234,
  236–265, 340, 353–382, 485, 503, 1767, 1827, 1856, 1889, 1920 all remain valid).

The intervening merge commits #957–#960 (WebService fix, netconsole stop, no
log_functions, RAK SSID weg) were **already integrated** into our branch via the
2026-05-25 rebase onto v4.35p.05.23 and are reflected in the previous audit. The branch
target moved from `oe1kbc_v4.35p` to `dev`, but `dev` contains the same content.

Our two local `src/` commits (sendExtern fix) are byte-for-byte the same patches audited
on 2026-05-25; only their commit hashes changed during the rebase.

**Net effect: all 83 findings from 2026-05-25 carry forward unchanged.** No findings
resolved, no findings introduced. The full enumerated catalogue lives in
docs/code-audit-20260525.md and is not duplicated here verbatim; the summary below
reproduces the carried-forward counts.

| Status | Count | Notes |
|--------|-------|-------|
| New findings        | 0 | `web_function` is a label swap with no logic change |
| Resolved            | 0 | no fixes landed in this window |
| Existing (unchanged)| 83 | line numbers re-verified stable (see below) |

---

## Phase 1: Mechanical scan (current state)

`python3 tools/code_audit_scan.py --full` — 317 files scanned, 503 raw regex hits
(5 CRITICAL, 17 HIGH, 481 MEDIUM). The MEDIUM bulk is the well-known unchecked-`snprintf`
and `millis()`-wraparound population that the contextual audit consolidates into ~35
distinct findings. Raw counts are identical in character to 2026-05-25 — no file in the
scan set changed except web_functions.cpp (label swap).

### Mechanical CRITICAL/HIGH hits vs. contextual classification

| Mech. sev | Rule | Site | Contextual verdict (carried from 05-25) |
|-----------|------|------|------------------------------------------|
| CRITICAL | BND-01 | nrf52/at_cmd.h:30 | **CRITICAL — OPEN.** Sole remaining `sprintf` macro; one-line fix. |
| CRITICAL | BND-01 | t-deck-pro/ui_deckpro.cpp:1864,2117,2122,2519 | MEDIUM — OPEN. Fixed local buffers, bounded inputs; scanner over-rates as CRITICAL. |
| HIGH | BND-01 | Displays/BaseDisplay/SD.cpp:368 | HIGH — OPEN. 3-arg `strcpy` dead-code regression; only saved by `Displays/*` build exclusion. |
| HIGH | BND-01 | net_console.cpp:147,149 | **FALSE POSITIVE.** `strcpy("NONCE: ")` + `strcat("\r\n")` into `chalBuf[48]`; max content 7+32+2 = 41 B. Bounded, safe. Pre-existing code (not in this window). |
| HIGH | BND-01 | loop_functions.cpp:1968 | **FALSE POSITIVE.** `strcat(msg_text, " >>>>>>>>>>>>>>")`; reaching this line requires `izeile > 5`, only reachable after the loop branch at 1925/1933 already wrote `msg_text` via `snprintf` and truncated it with `msg_text[20]=0x00`. So `msg_text` ≤20 chars + 14-char append = 34 ≪ `MAX_MSG_LEN_PHONE*2`. Bounded. Pre-existing code. |
| HIGH | RACE-03 | net_console.cpp:216,347 | MEDIUM — OPEN. `portMAX_DELAY` semaphore take (carried from RACE section). |
| HIGH | RACE-03 | nrf52/api_functions.cpp:262 | MEDIUM — OPEN (was already listed 05-25). |
| HIGH | RACE-03 | t-deck/tdeck_main.cpp:401 | MEDIUM — OPEN (display flush, was listed 05-25 at :405). |
| HIGH | MEM-01 | nrf52/nrf52_main.cpp:84 | LOW — OPEN. `malloc()` in `nrf52_getMaxFreeBlock()` (diagnostic). |
| HIGH | STAB-01 | nrf52/nrf52_main.cpp:1093 | MEDIUM — OPEN. `delay(>=5000)` blocking; nRF52 has no task WDT armed here. |
| HIGH | MEM-01 | t-deck-pro/tdeck_pro.cpp:200,208,217; t-deck/tdeck_main.cpp:319; t5-epaper/t5epaper_main.cpp:263,271,279 | LOW/PARTIAL — OPEN. `ps_calloc`/`malloc` display-buffer fallbacks with NULL checks (MEM-04). |

**No new mechanical site appears that was not present on 2026-05-25.** The
net_console.cpp:147/149 and loop_functions.cpp:1968 strcpy/strcat hits are pre-existing
code that the scanner flags by pattern; contextual analysis confirms both are bounded and
safe (documented here so future delta passes do not re-classify them as regressions).

---

## Audit Summary (carried forward from 2026-05-25, unchanged)

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

**Total: 14 Critical, 31 High, 35 Medium, 3 Low = 83 findings** (unchanged from 2026-05-25)

For the full per-finding tables (file/line/status for every entry above), see
**docs/code-audit-20260525.md** — none of those entries changed in this window.

---

## Highest-Priority Open Items (unchanged ranking from 2026-05-25)

1. **nrf52/at_cmd.h:30 `sprintf`** — CRITICAL, one-line fix; the only trivially-fixable CRITICAL.
2. **WiFi.softAP without password** — CRITICAL, 3 sites: `udp_functions.cpp:534`, `safeboot/main.cpp:68,187`.
3. **net_console.cpp:174 plaintext password bypass** — HIGH/SECURITY; `memcmp` shortcut defeats the HMAC nonce.
4. **net_console.cpp:171 password logged to Serial** — HIGH; remove or redact.
5. **millis() wraparound** — CRITICAL, ~33 sites; pattern `(uint32_t)(millis() - start) >= timeout`.
6. **APRS FCS before parsing** — CRITICAL, `aprs_functions.cpp:432` must precede field extraction at 199.
7. **web_header unbounded concat / indexOf unchecked** — CRITICAL, `web_functions.cpp:340,353`.
8. **`while(true)` on radio init** — CRITICAL, 8+11 sites; replace with `esp_restart()`.
9. **Main-loop task watchdog** — `esp_task_wdt_add()` / `esp_task_wdt_reset()` still missing (TX-only WDT exists).
10. **sendExtern() fix upstream PR** — local commits 85712628 + 2b26caae ready to submit.

---

## Build verification (this rebase)

| Target | Arch | Result |
|--------|------|--------|
| heltec_wifi_lora_32_V3 | ESP32-S3 | SUCCESS (RAM 33.9 %, Flash 40.3 %) |
| wiscore_rak4631 | nRF52840 | SUCCESS (Flash 69.4 %, link-RAM 99.14 %) |

Both branches of the platform-conditional `sendExtern` buffer allocation
(`#ifdef ESP32` stack vs. nRF52 static) compile cleanly on the post-rebase tree.

---

## Fazit

Reines Sync-Audit ohne inhaltliche Veränderung der Befundlage. Der einzige neue
Upstream-Commit in diesem Fenster (`4a0fe3e2 web_function`) ist ein Label-Tausch auf der
Web-Setup-Seite ohne Logikänderung und erzeugt keinen neuen Befund. Alle 83 Befunde des
Audits vom 2026-05-25 bleiben unverändert offen; die referenzierten Zeilennummern wurden
gegen den aktuellen Stand verifiziert und sind stabil.

Der mechanische Scan meldet in `net_console.cpp:147/149` und `loop_functions.cpp:1968`
neue `strcpy`/`strcat`-Treffer — beide sind nach Kontextprüfung gebundene, sichere
Schreibvorgänge (Puffer ausreichend dimensioniert) und betreffen bereits vor diesem
Fenster vorhandenen Code. Sie werden hier dokumentiert, damit sie in künftigen
Delta-Läufen nicht fälschlich als Regression gewertet werden.

Die Trendlinie bleibt: Die 14 CRITICALs sind überwiegend strukturelle Themen
(WiFi-AP ohne Passwort, `while(true)`-Radio-Init, millis()-Wraparound, APRS-FCS-Reihenfolge,
unbeschränkter `web_header`). Einzig `nrf52/at_cmd.h:30` ist ein trivialer Einzeiler und
sollte als nächster Upstream-PR-Kandidat zusammen mit dem sendExtern-Fix eingereicht werden.
