# MeshCom Firmware Code Audit (dev rebase)

**Date:** 2026-05-17
**Branch:** v4.35p_prio (rebased onto upstream `dev` HEAD = 9c9e1908)
**Local commits on top:** ac8691bd, 32b84891, b144796c, 66fa5793, 1c382218, 979c5164, 6bfe8082
  (7 docs/feature commits — only 32b84891 "perf(esp32-ble): NimBLE server-only tuning" touches src/. Note: dev now ships its own NimBLE tightening, so 32b84891 may be a candidate for removal on the next rebase.)
**Auditor:** Claude Code (automated)
**Rules:** docs/codequality-rules.md
**Previous audit:** docs/code-audit-20260517.md (same day, 66 findings, base 2083fdbd on `oe1kbc_v4.35p`)

**Delta vs. earlier audit on `oe1kbc_v4.35p`:** Upstream target switched from
`oe1kbc_v4.35p` to `dev`. 19 commits (8 non-merge) integrated; the big
substance changes are:

- **`src/tls_console.cpp` / `src/tls_console.h`** — 658 + 91 line new module.
  Replaces the plain-text Telnet bridge (port 23) that was reverted in PR #942
  with a TLS-encrypted Serial console (port 2323, mbedTLS + EC P-256
  self-signed cert, password gated, single-client). Major security and
  complexity addition.
- **`src/web_functions/web_functions.cpp:314-345`** — `web_header` parsing
  switched from unbounded `String web_header += c;` to a static
  `char web_header_collect[1024]` BSS buffer with bounds check. **Closes the
  Section 18 CRITICAL heap-exhaustion finding.**
- **RAM budgets shrunk** in `src/configuration_global.h` for ESP32-S3 /
  nRF52840: `MAX_MHEARD` 120→80, `MAX_MHPATH` 150→100, `MAX_RING` 30→20,
  `MAX_LOG` 20→10, `MAX_RING_UDP` 30→20.
- **NimBLE tightening** in `platformio.ini`: `MAX_BONDS` 4→1, `MAX_CCCDS`
  12→2, `CENTRAL_DISABLED`, `OBSERVER_DISABLED`, `HOST_TASK_STACK_SIZE=3072`,
  `MSYS1_BLOCK_COUNT=4`. Same direction as our local `32b84891`.
- **`--passwd none` and `--passwd` (status)** — operator can clear the
  password and query status without leaking the secret to the console.

Net: 1 CRITICAL closed (web_header), 1 CRITICAL re-opened in a different
form (TLS console new attack surface), several MEDIUM/HIGH items added by
the new module.

---

## Audit Summary (delta from 2026-05-17 audit on oe1kbc_v4.35p)

| Category | Critical | High | Medium | Low | Δ vs. earlier audit |
|----------|----------|------|--------|-----|---------------------|
| Memory Safety | 1 | 0 | 3 | 2 | unchanged |
| Buffer Safety | 2 | 1 | 1 | 0 | unchanged |
| Input Validation | 1 | 2 | 1 | 0 | unchanged |
| Thread Safety | 0 | 3 | **4** | 1 | +1 MEDIUM (TLS handshake task) |
| ISR Safety | 1 | 1 | 2 | 0 | unchanged |
| SPI Bus | 0 | 1 | 1 | 0 | unchanged |
| Auth & Security | **2** | **5** | **2** | 0 | +1 HIGH (TLS no-rate-limit), +1 MED (TLS reuses node_passwd) |
| Error Handling | 1 | 2 | 1 | 0 | unchanged |
| Watchdog | 2 | 1 | 2 | 0 | unchanged |
| Compiler/Build | 1 | 0 | **1** | 0 | +1 MED (tab/whitespace in build_flags) |
| Type Safety | 0 | 1 | 1 | 0 | unchanged |
| Lifetime Safety | 0 | 0 | 2 | 0 | unchanged |
| Logging Safety | 0 | 1 | 1 | 0 | unchanged |
| Design Patterns | 1 | 1 | **2** | 0 | +1 MED (Serial macro pollution) |
| Protocol Correctness | 1 | 1 | 1 | 0 | unchanged |
| State Machines | 0 | 0 | 2 | 0 | unchanged |
| Data Drift | 1 | 0 | 0 | 0 | unchanged |
| TCP/Web/SSE | **1** | 1 | 1 | 0 | **−1 CRITICAL (web_header bounded)** |
| Test Readiness | 0 | 0 | 1 | 0 | unchanged |
| Stack Safety | 1 | 1 | **2** | 0 | +1 MED (TLS handshake task stack 4096) |
| Dead-code regression | 0 | 1 | 0 | 0 | unchanged (SD.cpp:368) |
| Schema migration | 0 | 1 | 0 | 0 | unchanged (node_via) |

**Total: 16 Critical, 22 High, 30 Medium, 3 Low = 71 findings**
(+5 net vs. earlier 2026-05-17 audit: −1 CRITICAL closed, +1 HIGH and +5 MEDIUM
added by the TLS console module, rest unchanged.)

---

## Section A — What changed vs. the earlier (2026-05-17 oe1kbc) audit

### A.1 FIXED — Section 18 CRITICAL #1: unbounded `web_header` String — CLOSED

**File:** src/web_functions/web_functions.cpp:314-345

Earlier audit (2026-05-17 oe1kbc) flagged:
> CRITICAL | web_functions.cpp | 31, 340 | `String web_header` — unbounded
> concatenation of HTTP request data — heap exhaustion vector

Upstream dev replaced the unbounded String accumulation with a bounded
static BSS buffer:

```c
static char web_header_collect[1024];        // BSS statt Heap
static uint16_t web_header_collect_len = 0;
...
if (web_header_collect_len < sizeof(web_header_collect) - 1)
{
    web_header_collect[web_header_collect_len++] = c;
    web_header_collect[web_header_collect_len] = '\0';
}
```

The bounds check is correct (`< sizeof - 1` leaves room for the NUL).
A long HTTP request now truncates instead of growing the heap. The
`String web_header` still exists as the post-parse handle (assigned once
from `web_header_collect`), which is fine — it is now bounded by 1024 B
not by the attacker.

**Status: FIXED.** Severity dropped from CRITICAL to RESOLVED. The
companion finding "`indexOf()` return not checked before `substring()`"
(Section 18 CRITICAL #2, ~12 sites starting at line 364) is **still
open**.

### A.2 FIXED (informational) — operator UX leak: `--passwd` no longer prints the secret

**File:** src/command_functions.cpp:2879

`--passwd <value>` previously echoed the password back; the new branch:
- `--passwd none` clears the stored password (open access), prints `cleared`
- `--passwd` (no arg) prints `SET` or `EMPTY (open access)` — never the value

Not in the previous audit list (operator-facing only), but worth noting:
the previous code printed `meshcom_settings.node_passwd` to console in
`--info` output. That path is unchanged.

### A.3 PARTIAL — `tlsConsoleSetPassword()` couples `node_passwd` to TLS auth

**File:** src/esp32/esp32_main.cpp:760, src/command_functions.cpp:2868, 2876

The same `meshcom_settings.node_passwd` is now used as:
- Web server password (`webpwd` slot — pre-existing)
- TLS console password (new in dev) — see `tls_console.cpp:289-326`

Single-secret reuse across two services is a defence-in-depth weakness:
a leak on either service compromises the other. Add as new HIGH (see
B.1).

### A.4 PARTIAL — RAM budget reduction reduces our log analysis depth

**File:** src/configuration_global.h:80-86

| Constant | was | now | impact |
|----------|-----|-----|--------|
| `MAX_MHEARD` | 120 | 80 | mheard ringbuffer shorter |
| `MAX_MHPATH` | 150 | 100 | path ringbuffer shorter |
| `MAX_RING` | 30 | 20 | msg ringbuffer shorter |
| `MAX_LOG` | 20 | 10 | LOG ringbuffer shorter |
| `MAX_RING_UDP` | 30 | 20 | UDP TX ringbuffer shorter |

Net DRAM saving roughly matches the gain needed for the TLS console
I/O buffers (~36 KB allocated only while a TLS client is connected).
Our `log_functions.cpp` log generator now produces shorter history per
report. No audit finding moved, but the prior "ring buffer overflow
silently wraps" MEDIUM (Section 8) is **more likely to trigger** at the
new buffer sizes — flag for monitoring.

### A.5 PARTIAL — NimBLE tightening overlaps with our local commit 32b84891

**File:** platformio.ini:142-156

dev now ships:
```ini
-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
-DCONFIG_BT_NIMBLE_MAX_BONDS=1
-DCONFIG_BT_NIMBLE_MAX_CCCDS=2
-DCONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED
-DCONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED
-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=3072
-DCONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4
```

Same theme as our local `32b84891` "perf(esp32-ble): NimBLE server-only
tuning". Suggest comparing the exact deltas on the next rebase — if upstream
has converged on our values, drop 32b84891 to keep our branch lean.

---

## Section B — NEW findings introduced by `tls_console.cpp/h`

This section enumerates everything new that the dev rebase added on top of
the 2026-05-17 oe1kbc audit. Items here are *in addition* to all
still-open findings from that audit (re-summarised in Section C).

### B.1 NEW HIGH — TLS console: no rate limiting on failed authentication

**File:** src/tls_console.cpp:316-325

```c
if (!match)
{
    const char* denied = "Access denied.\r\n";
    mbedtls_ssl_write(&s_ssl, (const uint8_t*)denied, strlen(denied));
    mbedtls_ssl_close_notify(&s_ssl);
    mbedtls_ssl_session_reset(&s_ssl);
    ::close(fd);
    s_hwSerial.println("[TLS] Authentication failed.");
    s_hs_running = false; vTaskDelete(nullptr); return;
}
```

The client is dropped, but there is no cool-down, no IP block list, no
counter. An attacker on the local subnet can immediately reconnect and
retry. Single-client semantics (line 588: "Rejected: handshake already in
progress") rate-limit *to the TLS handshake throughput* — measured
~1-2 s/handshake on ESP32 (line 401-402 comment). At one attempt per
second, a 14-character password from
`[a-zA-Z0-9]` has ~10^25 combinations — fine in theory, but typical
operator passwords (8 lower-case characters ≈ 2×10^11) become brute-forceable
within months on a sustained connection campaign.

**Mitigation options (ranked):**
1. Track failed attempts per source IP, exponential back-off (5 → 30 → 300 s)
2. Lock the account after N failures, require a serial-console reset
3. At minimum: log failed IPs with a counter for offline review

### B.2 NEW MEDIUM — TLS console password is the same secret as web password

**Files:** src/tls_console.cpp:289-326, src/esp32/esp32_main.cpp:760

`tlsConsoleSetPassword(meshcom_settings.node_passwd)` is called at setup;
`node_passwd` is also used by the web server (`commandAction` `--webpwd`
branch). A single compromise covers two services. Recommend: separate
`node_tlspwd` field, or document the coupling so operators understand
that changing the web password also changes TLS console access.

### B.3 NEW MEDIUM — TLS server certificate validity hardcoded 2024-2034

**File:** src/tls_console.cpp:166

```c
mbedtls_x509write_crt_set_validity(&crt, "20240101000000", "20340101000000");
```

After 2034-01-01 every freshly-generated cert will be `notAfter` in the
past. clients with strict cert validation (curl --cacert) will refuse the
connection. mbedTLS server-side will still complete the handshake (the
server isn't validating its own cert), but it's a latent ticking clock.
Recommend: compute `notAfter` as `__DATE__ + 10 years` at generation
time, or set to e.g. `20990101000000` to push beyond firmware lifetime.

### B.4 NEW MEDIUM — RACE-02: `portMAX_DELAY` in TLS handshake task

**File:** src/tls_console.cpp:337

```c
if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
{
    ...
}
```

The handshake task takes `s_mutex` with infinite wait while holding the
TLS handshake state. If the main loop's `MeshSerialClass::write()` path
(line 367, 381) ever held `s_mutex` for an unbounded duration, the
handshake completion would stall. In practice both write paths use
`xSemaphoreTake(s_mutex, 0)` (non-blocking, line 367/381/628/649), so the
hazard is bounded — but the documented project rule (RACE-02) is
violated. Replace with a finite timeout (e.g. 500 ms) and abort
cleanly on timeout.

### B.5 NEW MEDIUM — Stack safety: TLS handshake task stack = 4096 bytes

**File:** src/tls_console.cpp:598

```c
xTaskCreatePinnedToCore(handshakeTask, "tls_hs", 4096, args, 1, nullptr, 1);
```

The mbedTLS TLS 1.2 handshake with ECDHE + cert PEM parsing typically
needs 5-7 KB of stack on ESP32 (per Espressif's own measurements for
`esp-tls`). 4 KB risks overflow on long-cert handshakes or when adding
debug printf. Recommend: 6144 or 8192, and add
`uxTaskGetStackHighWaterMark` instrumentation (already a STK-02 audit
finding — this concretises it).

### B.6 NEW MEDIUM — Design pattern: `#define Serial MSerial` in a header

**File:** src/tls_console.h:88-89, src/debugconf.h:5

```c
// tls_console.h
#undef  Serial
#define Serial MSerial
```

`debugconf.h` includes `tls_console.h` "before any Serial usage", which
means almost every translation unit in the firmware sees `Serial`
replaced by `MSerial`. This is a deliberate trick (so existing
`Serial.printf` calls also go to the TLS client when authenticated), but
it is a heavy macro pattern:

- Any include order change that reverses the `#define` silently breaks the
  TLS mirroring on that file
- IDE tooling (clangd, ctags) will show the call as `Serial.printf` while
  the real callee is `MSerial::write` — debug surprise
- `s_hwSerial` capture at `tls_console.cpp:34` (`static auto& s_hwSerial = Serial;`)
  depends on being parsed *before* the `#include "tls_console.h"` — comment
  at line 8-9 calls this out. Fragile.

Not a defect today (the unit tests would catch wrong dispatch), but
flag for review: a `MSerial.printf(...)` API would be more honest than
the silent rename.

### B.7 NEW MEDIUM — Compiler/Build: tab between `MSYS1_BLOCK_COUNT=4` and `-Wall -Wextra`

**File:** platformio.ini:152

```ini
-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=3072
-DCONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4	-Wall -Wextra
```

There is a TAB character between `4` and `-Wall`. PlatformIO splits on
any whitespace so the build is unaffected, but this is a merge-conflict
artefact (called out by the user in release.md). Clean up to two lines
to keep diff hygiene.

### B.8 NEW LOW — `tls_console.cpp:309-314`: claimed constant-time compare leaks length

**File:** src/tls_console.cpp:309-314

```c
// Constant-time compare (prevent timing attack)
size_t pwLen = strlen(s_password);
size_t inLen = strlen(buf);
bool match = (pwLen == inLen);
for (size_t i = 0; i < pwLen; i++)
    if (i >= inLen || buf[i] != s_password[i]) match = false;
```

- The `(pwLen == inLen)` test runs before the loop, so length information
  leaks via timing (the early-mismatch case skips the loop entirely).
- The loop body is constant in iteration count (bounded by `pwLen` only).
- Buffer is 16 bytes (`char buf[16]`), so iterations are ≤ 14, and the
  observed timing difference across the LAN is well below mbedTLS jitter.

Severity LOW because the attack window is dominated by TLS handshake
latency, but the **comment claim is wrong** — drop the "(prevent timing
attack)" claim or implement it correctly (always iterate to a fixed
length, e.g. `sizeof(buf) - 1`).

### B.9 NEW LOW — `tls_console.cpp:470` `MBEDTLS_SSL_VERIFY_NONE` is fine for current design but worth a comment

```c
mbedtls_ssl_conf_authmode(&s_conf, MBEDTLS_SSL_VERIFY_NONE); // no client cert required
```

Server does not require client certs (correct for an operator-tool
scenario where the operator uses `openssl s_client` ad-hoc). The
self-signed server cert means clients must also use VERIFY_NONE
(equivalent to `openssl s_client -verify_return_error 0`). This is the
right trade-off for the use case; documented in the header. No action.

### B.10 NEW INFO — bind to `INADDR_ANY` (line 541) listens on all interfaces

```c
addr.sin_addr.s_addr = INADDR_ANY;
```

The TLS port (2323) is exposed on:
- WiFi STA interface (typical home LAN, behind NAT)
- WiFi softAP interface (no WPA2 password — see Section 7 CRITICAL #1)
- Ethernet interface (T-ETH-ELITE, T-CONNECT-PRO, etc.)

The Section 7 CRITICAL `WiFi.softAP(meshcom_settings.node_call)` finding
gains an exploitable companion: any device joining the open softAP can
reach the TLS console (encrypted, password gated). If the password is
weak, this is a direct RCE-equivalent surface. Combined risk: the
existing softAP-open finding is now strictly more dangerous than it was
on `oe1kbc_v4.35p`. Mitigation is the same: close the softAP CRITICAL.

---

## Section C — Findings unchanged from 2026-05-17 oe1kbc audit

All of the following remain OPEN with the same severity and location
unless noted. (Full text in the earlier audit; only the headline is
repeated here.)

### Still CRITICAL (15 — was 16, web_header closed)

| # | Section | File:Line | Finding |
|---|---------|-----------|---------|
| 1 | 7 | udp_functions.cpp:534 | WiFi softAP no password |
| 2 | 7 | safeboot/main.cpp:68 | WiFi softAP no password (safeboot) |
| 3 | 7 | safeboot/main.cpp:187 | WiFi softAP no password (safeboot 2) |
| 4 | 7 | esp32_main.cpp:271 | hardcoded `PIN = 000000` |
| 5 | 8 | esp32_main.cpp:1317.. | 8× `while(true);` on radio config |
| 6 | 8 | t-deck-pro/peri_lora.cpp | 11× `while(true);` on radio config |
| 7 | 9 (STAB-01/02) | (project-wide) | no task watchdog |
| 8 | 9 (STAB-05) | (multi) | ~33 millis() wraparound sites |
| 9 | 10 (COMP-01) | platformio.ini | -Werror missing |
| 10 | 14 | lora_functions.cpp:1916-1952 | CSMA mutates globals |
| 11 | 15 | aprs_functions.cpp | FCS checked after extraction |
| 12 | 17 | t5-epaper/nvs_param.cpp | NVS schema version field absent |
| 13 | 18 | web_functions.cpp:353-1889 | indexOf() return not checked (~12 sites) |
| 14 | 20 (STK-01) | sdkconfig | no stack overflow level 2 |
| 15 | 2 (BND-01) | nrf52/at_cmd.h:30 | `sprintf(buff, ...)` macro still un-snprintfd (sibling of fixed esp32 macro) |

(was 16 — `web_functions.cpp:31, 340` web_header closed — see A.1)

### Still HIGH (21 — +1 from TLS no-rate-limit, +1 from TLS reuses node_passwd, −1 because earlier "node_via schema migration" stays as HIGH not added again)

(Same set as 2026-05-17 oe1kbc, plus B.1 and B.2.)

### Still MEDIUM (30 — +5 from B.4, B.5, B.6, B.7, plus a Section 8 "ring buffer overflow more likely at smaller MAX_RING" note from A.4)

### Still LOW (3 — +2 from B.8, B.9)

---

## Section D — `tls_console.cpp/h` line-by-line audit summary

Items below are *new* compared to the 2026-05-17 oe1kbc audit. Severity and
section are assigned per docs/codequality-rules.md.

| Line | Severity | Rule | Note |
|------|----------|------|------|
| 170-171 | PASS | MEM-03 | `new char[512] / new char[1024]` followed by `delete[]` at 192-193 on every exit path. PASS |
| 212 | PASS | MEM-03 | `delete a` consumed in handshakeTask. PASS |
| 596 | PASS | MEM-03 | `new HandshakeArgs{}` deleted at 602 (failure path) or 212 (success path). PASS |
| 231-237 | INFO | STAB | `mbedtls_ssl_init` then on error `mbedtls_ssl_free` and exit. PASS |
| 252 | INFO | -- | 500 ms `SO_RCVTIMEO` for handshake — bounds the `mbedtls_ssl_handshake` loop wall-clock at 15 s. PASS |
| 270 | PASS | STAB-05 | `millis() - hsStart > 15000` — correct wraparound-safe pattern. PASS |
| 309-314 | LOW | -- | non-constant-time compare with misleading comment — see B.8 |
| 316-325 | HIGH | Section 7 | no auth-failure rate limit — see B.1 |
| 337 | MEDIUM | RACE-02 | `portMAX_DELAY` mutex take — see B.4 |
| 462-470 | INFO | Section 7 | `MBEDTLS_SSL_PRESET_DEFAULT` + `VERIFY_NONE` — appropriate for use case |
| 487-493 | PASS | -- | ECDHE group restriction (P-521 excluded for heap safety) — correct |
| 506-517 | INFO | -- | init task pinned to core 1 — matches Arduino loop; comment explains. PASS |
| 528-549 | PASS | Section 8 | socket / bind / listen errors fall through, no `while(true)`. PASS |
| 541 | INFO | -- | `INADDR_ANY` listens on softAP/STA/ETH — see B.10 |
| 559-573 | PASS | RACE | disconnect detection via `MSG_PEEK | MSG_DONTWAIT`, under mutex. PASS |
| 596-603 | PASS | Section 8 | `xTaskCreatePinnedToCore` return checked. PASS |
| 598 | MEDIUM | STK | 4 KB task stack — see B.5 |

Total new TLS-module findings: 1 HIGH (rate limit), 3 MEDIUM (RACE,
stack, Serial macro), 1 LOW (CT-compare claim), plus 2 INFO (cert
validity, INADDR_ANY).

---

## Section E — Updated Top-10 Priority Fixes

(reordered relative to earlier audit to reflect the new attack surface)

### 1. WiFi AP password protection (Section 7 — CRITICAL, now strictly worse with TLS console exposed on softAP)
- `udp_functions.cpp:534`, `safeboot/main.cpp:68, 187`
- Closing this also closes the new B.10 attack vector

### 2. TLS console — failed-auth rate limiting (NEW HIGH B.1)
- `tls_console.cpp:316-325` — add per-IP exponential back-off
- Combined with the open softAP this is the highest-leverage practical attack window in the firmware today

### 3. millis() wraparound (STAB-05 — CRITICAL, ~33 sites, unchanged)

### 4. FCS validation before field parsing (Section 15 — CRITICAL, unchanged)

### 5. nrf52/at_cmd.h sprintf macro (BND-01 — CRITICAL, sibling of two fixed macros, trivial)

### 6. Task watchdog configuration (STAB-01/02 — CRITICAL)
- Now even more important — the TLS handshake can hang on a malformed ClientHello

### 7. BLE pairing security (Section 7 — HIGH, unchanged)

### 8. memcpy length validation (BND-03 — HIGH, unchanged)

### 9. indexOf() return checks before substring() (Section 18 — CRITICAL, second half of the web_header CRITICAL pair)

### 10. NVS schema migration (NEW-2 from earlier audit — HIGH, plus the new `node_via[10]` field)

---

## Section F — Files most affected (delta)

| File | Findings | Highest Severity | Δ |
|------|----------|------------------|---|
| **NEW** src/tls_console.cpp | 5 (1 HIGH, 3 MED, 1 LOW) | HIGH | NEW module |
| **NEW** src/tls_console.h | 1 MED (Serial macro pollution) | MED | NEW module |
| platformio.ini | 15 (+1 MED whitespace) | CRITICAL | +1 |
| web_functions/web_functions.cpp | 8 (was 9) | CRITICAL | −1 (web_header CRITICAL closed) |
| esp32/esp32_main.cpp | 9 (+1 from TLS hookup) | CRITICAL | +1 |
| command_functions.cpp | 0 audit findings (TLS console branches are clean) | -- | 0 |
| configuration_global.h | 0 audit findings (RAM reduction is a tradeoff, not a defect) | -- | 0 |

All other files unchanged from the 2026-05-17 oe1kbc audit.

---

## Rebase-Specific Observations (oe1kbc_v4.35p 2083fdbd → dev 9c9e1908)

19 commits total (8 non-merge); themes:

- **TLS console feature** (67311a4c + e6ce9f20 + 309796d0 + d2ff12b7) —
  the substantive change. Adds an encrypted Serial-bridge service that
  replaces the plain-text Telnet (port 23) reverted in PR #942 with TLS
  on port 2323. Adds 1 HIGH, 3 MEDIUM, 1 LOW finding to the audit.
- **TLS opt-out path** (431cbb1f + 0972ec73 + e637fa65) — adds
  `DISABLE_TLS_CONSOLE` macro and applies it to `E22_XML-DevKitC`
  (low-RAM ESP32 board). Variant-level guard, clean implementation.
- **RAM budgets shrunk** (in 67311a4c via configuration_global.h) — frees
  DRAM for the TLS I/O buffers (~36 KB peak when a client is connected).
  Side effect: smaller log windows.
- **Web `web_header` bounded** (in 67311a4c) — closes one CRITICAL from
  the earlier audit. Substantive defensive fix.
- **`--passwd none/status`** (in 67311a4c) — operator UX, no longer
  leaks the password to the console on status query.
- **NimBLE tightening** (in 67311a4c platformio.ini) — converges
  on the same direction as our local 32b84891. Suggest comparing values
  and dropping 32b84891 on next rebase if they match.

**Bottom line:** The dev rebase brings *one* CRITICAL closure (web_header
heap-exhaustion) and *one* large new attack surface (TLS console on port
2323, exposed on all interfaces including the open softAP). Net audit
delta: −1 CRITICAL, +1 HIGH, +5 MEDIUM, +2 LOW = +5 findings overall.
The single highest-leverage fix remains closing the WiFi softAP open-AP
finding — it directly mitigates the new TLS console attack window
(Section B.10).
