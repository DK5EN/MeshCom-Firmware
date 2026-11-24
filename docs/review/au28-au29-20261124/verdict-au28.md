# AU-28 shortlist (advisor / verifier) - download "conn -7"

Date 2026-11-24. Read-only review of committed history up to 6ca39d7d/a6833f44, the patched framework store and the bench
logs. Scratch evidence: `/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/b06da057-6de3-4dc3-ace0-30f9cf4b8bb4/scratchpad/verify/` (captured headers `*.hdr`, host simulation `harness.cpp`, `hhr_{stock,patched,fixed}.inc`,
stale-marker experiment `stale.py`).

## Verdict

**Root cause CONFIRMED: AE-01 (tools/arduino_esp32_patch.py, commit 9696a3ba) fails every header line of 2048 bytes or more
with HTTPC_ERROR_NO_HTTP_SERVER (-7). The github.com 302 for every release asset carries a 4204-byte
Content-Security-Policy line, so hop 1 of the install download fails after a good TLS handshake.** Nothing else in
878ebef4..HEAD is needed to explain the failure, and every other suspect is refuted for AU-28.

## Evidence (reproduced, not taken from the longlists)

1. **Return path in the real store file.** `~/.platformio/packages/framework-arduinoespressif32/libraries/HTTPClient/src/HTTPClient.cpp`
   (store re-installed 21:29:42, patched 21:29:55 on 2026-11-23; it holds the current `HTTP_PATCHED` verbatim):
   `handleHeaderResponse()` L1284-1286 `else if(headerLine.length() >= 2048) { _canReuse = false; return HTTPC_ERROR_NO_HTTP_SERVER; }`
   -> `sendRequest()` (L585-711): code -7 is not a 3xx, no redirect, `returnError(-7)` -> `http.GET()` -7 ->
   `httpGet()` (src/esp32/fw_update_net.cpp:838) -> `httpFail()` L787-810: lastError 0 -> `"conn -7"`. The only other -7 is
   L1375 (blank line while `_returnCode == 0`), which needs an unparsable status line. Stock `Stream::readStringUntil`
   (cores/esp32/Stream.cpp:327) has no cap.
2. **Headers re-measured today (curl --http1.1, UA MeshCom-AU, Connection: close).** Line length incl. CRLF:

   | Response                                    | Longest line                        | Location | Total header |
   | ------------------------------------------- | ----------------------------------- | -------- | ------------ |
   | github.com 302, icssw-org v4.40a.11.22 V3   | CSP 4205                            | 958      | 5785 B       |
   | github.com 302, DK5EN v4.40a.11.22 V3       | CSP 4205                            | 959      | 5785 B       |
   | github.com 302, DK5EN v4.40a.11.21 V3       | CSP 4205                            | 959      | 5786 B       |
   | api.github.com releases/latest (both repos) | 325 (Access-Control-Expose-Headers) | -        | 1350 B       |
   | release-assets.githubusercontent.com 206    | 77                                  | -        | 940 B        |

   Only the github.com 302 exceeds 2048. The CHECK job (api.github.com only) passes, as observed.

3. **Host simulation of the real code** (`harness.cpp`: the function text cut from the store file, compiled with a
   std::string-backed String and a fake client, fed the captured header bytes):

   | Input                                       | stock (HTTP_ORIGINAL) | patched store (AE-01 v1) | proposed AE-01 v2      |
   | ------------------------------------------- | --------------------- | ------------------------ | ---------------------- |
   | 3 x github.com 302                          | 302, Location kept    | **-7** after 3483 B      | 302, Location kept     |
   | api.github.com 200                          | 200                   | 200                      | 200                    |
   | release-assets 206                          | 206, size 100         | 206                      | 206                    |
   | Location 5000 B                             | 302                   | -7                       | fail (cut used header) |
   | 20 x 2000 B unused headers (40 KB)          | 200                   | 200 (no total bound)     | fail at 32 KB budget   |
   | Transfer-Encoding / Content-Encoding 5000 B | -9 / 200              | -7                       | fail                   |

4. **Binary check of the built images** (`xtensa-esp32-elf-objdump` of `HTTPClient::handleHeaderResponse`): every ELF
   built before 17:03 on 2026-11-23 (heapmap set 09:46-13:05, incl. `old20-v3.elf` = the last good 13:04 image) has the
   stock 864-byte function; every ELF built after (`instr-*` 18:55, `instr-plain-*` 19:17/19:48, `trace-*` 21:51/22:57,
   `.pio/build/ttgo_tbeam` 23:26) has the 1056-byte AE-01 function with `movi a8, 0x7ff` (the >= 2048 compare). The
   good/bad boundary of the bench is exactly the store patch.
5. **Failing log is consistent** (ota-lowheap-arena-20261123/chan-check/serial-DK5EN-1.log:357-375): gate ok 23:36:46.6,
   `buf32;rec;hit;2;miss;0` (= in + out record of one mbedtls_ssl_setup, fw_update_net.cpp:384/525), no `tlscap;refused`,
   `alloc;end;fails;0`, `fail;conn -7` at 48.6 = one TLS session, failure on hop 1, ~2 s.
6. **Bisect builds are AE-01 builds too.** 6bbca7a2 contains no `tools/arduino_esp32_patch.py` and no pre-script
   (`git show 6bbca7a2:tools/arduino_esp32_patch.py` fails), yet its 19:37 install fails after a good handshake
   (`tls;52` = socket fd, pre-f6c6acd7 misreport). It was built between patched ELFs (18:55 and 19:17/19:48), so the global
   store carried AE-01. The bisect therefore did not separate source commits; it separated only "store patched or not".

## Per-suspect table

Status is for AU-28 unless noted.

| Suspect (longlist id)                                                                       | Status                              | Evidence                                                                                                                                                                                                                                                                                                                                                                 |
| ------------------------------------------------------------------------------------------- | ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| AE-01 2048 B line cap (F1-S1, F5-01, F6-00)                                                 | **CONFIRMED, root cause**           | Evidence 1-6                                                                                                                                                                                                                                                                                                                                                             |
| Stale-marker trap, `_apply` returns on marker (F1-S2, F5-07)                                | **CONFIRMED** (fix risk)            | `stale.py`: script with HTTP_MAX_LINE 8192 leaves the patched store byte-identical (still 2048); a fresh store gets 8192. Local builds would keep failing after a script-only fix                                                                                                                                                                                        |
| Bisect contamination by the global store (F5-08)                                            | **CONFIRMED**                       | Evidence 6                                                                                                                                                                                                                                                                                                                                                               |
| CI ships AE-01 v1 in the next release (F5-10)                                               | **CONFIRMED** (consequence)         | meshcom-ci.yml caches only `~/.platformio/.cache` (downloads), so `packages/` is fresh and the HEAD pre-script (platformio.ini:163) patches it; `git tag --contains 9696a3ba` is empty. A release from HEAD would break AU for every user, including the AU path that would deliver the fix. CI has no stale-marker problem: it always patches from the pristine archive |
| Tests cannot see it (F5-02, F5-03, F5-04)                                                   | **CONFIRMED**                       | `test_http_behavior` greps text and pins `HTTP_MAX_LINE >= 1024`; `test_anchors_match_real_framework_once` skips on a patched store; no wire fixture anywhere                                                                                                                                                                                                            |
| Store churn by package swaps (F5-09)                                                        | UNRESOLVED, not a cause             | .piopm and all patched files dated 21:29 show a re-install; it only decides whether a local build gets the patch                                                                                                                                                                                                                                                         |
| AE-01 blank line with `_returnCode == 0` (F1-S3)                                            | REFUTED                             | Status line parses (sim: 302 on stock); cap fires first                                                                                                                                                                                                                                                                                                                  |
| Per-byte read over McTlsClient, 0xFF phantom byte, available()<0 (F1-S4, F2-S9, F2-S15)     | REFUTED                             | Sim reproduces -7 with a perfect byte stream, no TLS involved; EOF/close paths give -5                                                                                                                                                                                                                                                                                   |
| AE-01 deadline / idle / delay(1) (F1-S5, F1-S6, F5-11)                                      | REFUTED                             | Those paths return -11 / -5                                                                                                                                                                                                                                                                                                                                              |
| Redirect, reuse, Content-Length 0 (F1-S7)                                                   | REFUTED                             | Hop 2 never runs (hit;2)                                                                                                                                                                                                                                                                                                                                                 |
| AE-02 WiFiClient / WiFiUDP nothrow (F1-S8, F6-04, F6-05, F5-12)                             | REFUTED                             | McTlsClient opens its own socket; failure would be -1                                                                                                                                                                                                                                                                                                                    |
| AE-03 hostByName, mc_dns, tlsResolve (F1-S9, F1-S10, F2-S6, F6-01..03, F5-13)               | REFUTED                             | DNS failure -> lastError -1 -> `tls;-1`; handshake to github.com succeeded                                                                                                                                                                                                                                                                                               |
| Date verify callback, TLS 1.2 minimum, bundle retry (F1-S11..S13, F2-S7, F6-11)             | REFUTED                             | Would give a nonzero lastError; 6bbca7a2 lacks them and fails                                                                                                                                                                                                                                                                                                            |
| TLS alloc cap / hook on all boards (F1-S14, F1-S15, F2-S3, F2-S13, F6-10)                   | REFUTED for hop 1; hop 2 UNRESOLVED | No `tlscap;refused`, fails 0. Hop 2 has never run with the cap armed: check it in the post-fix bench run                                                                                                                                                                                                                                                                 |
| 4 KB out record, buf32, mc_ssl_send loop, vendored mbedTLS TUs (F1-S16, F2-S1, S2, S8, S12) | REFUTED                             | Sim needs none of it; 6bbca7a2 (none of it) fails; check job uses the same TUs and passes                                                                                                                                                                                                                                                                                |
| lwIP wraps, mdns/dhcps overrides, net console, UDP drain (F2-S4, S5, F6-06..09, F6-12)      | REFUTED                             | Not on the HTTP path; cannot produce a parsed -7                                                                                                                                                                                                                                                                                                                         |
| heaptrace / INSTRUMENT, f6c6acd7 (F1-S17, F2-S10, S11)                                      | REFUTED                             | Plain images fail too; f6c6acd7 only changed the log text                                                                                                                                                                                                                                                                                                                |
| NimBLE patches, BLE pause (F4 all, for AU-28)                                               | REFUTED                             | BLE is down during the job; no HTTP relation                                                                                                                                                                                                                                                                                                                             |
| String realloc of a 4 KB line (F1-S18)                                                      | UNRESOLVED, low                     | The stock 13:04 image stored the 4.2 KB CSP line without trouble; v2 below no longer stores unused long lines                                                                                                                                                                                                                                                            |
| Same cap on the check path (F1-S19)                                                         | CONFIRMED latent                    | api.github.com is at 325 B today; v2 removes the cliff                                                                                                                                                                                                                                                                                                                   |
| AU-29 items rated medium: F4 S-01 flash guard / ipc0, F4 S-11 INSTRUMENT confounder, F5-06  | UNRESOLVED (AU-29, out of scope)    | 11.21 without any AE patch resets too, so AU-29 is independent of AU-28. Needs the bench timestamps F4 lists                                                                                                                                                                                                                                                             |

## Fix specification: AE-01 v2

### Header loop (replaces the v1 block; prototype in `/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/b06da057-6de3-4dc3-ace0-30f9cf4b8bb4/scratchpad/verify/fixed_loop.txt`, validated by the simulation)

- Marker `/* MESHCOM-PATCH AE-01v2 */` (the v1 marker `/* MESHCOM-PATCH AE-01 */` is not a substring of it).
- Constants: `HTTP_MAX_LINE = 4096` stored bytes per line (Location is 959 B today; signed URLs grow, 4x margin),
  `HTTP_MAX_HEADER = 32768` bytes for the whole header block (real: 5.8 KB), `HTTP_HEADER_DEADLINE_MS = 30000` unchanged,
  idle `_tcpTimeout` unchanged.
- Per byte: count into `headerBytes`; above HTTP_MAX_HEADER fail. Store a byte only while `headerLine.length() < HTTP_MAX_LINE`,
  otherwise set `lineCut` and keep draining to `\n`. Memory stays bounded at 4 KB per line, the deadline still bounds time.
- At the end of a cut line: fail if it is the status line, has no `:` in the stored prefix, or its name is one the client
  uses: Location, Content-Length, Transfer-Encoding, Connection, or any key in `_currentHeaders` (Transfer-Encoding,
  Content-Encoding for the AU). Otherwise `continue` (the line is dropped whole, never parsed truncated). Date and
  Set-Cookie are dropped too: a dropped cookie is harmless, a truncated one is not.
- Error code for "a header we use is too long" and "header budget exceeded": prefer `HTTPC_ERROR_TOO_LESS_RAM` (-8) so the
  AU log no longer confuses it with a real non-HTTP peer (-7). The -7 of a blank line without status code stays stock.
- Docstring: replace the false premise "far below 2 KiB" with the measured numbers (CSP 4204 B, Location 959 B,
  2026-11-24) and the rule "drain unused long lines, fail only on used ones".

### Marker versioning and upgrade path

- Freeze the current block as a literal `HTTP_PATCHED_V1` (2048 / 30000 baked in, not built from the live constants),
  the way tools/nimble_patch.py keeps `*_PATCHED_V1` forms.
- `patch_http_text`: V2 marker present -> require the exact current V2 block, else RuntimeError ("stale AE-01 form,
  re-install the framework"); `HTTP_PATCHED_V1` present exactly once -> replace with V2; stock anchor present once -> apply V2;
  anything else -> RuntimeError.
- Generalise `_apply` for AE-02/AE-03: "marker present" must also mean "current patched block present", so a future
  change can never be skipped silently again.
- `markers_present` prints the versioned id (`AE-01v2@HTTPClient.cpp`), so the build log proves the version, not only the marker.
- Optional, for bisects: `--revert` restoring the `*_ORIGINAL` text, so an older commit can be built on a clean store.
- After the change: rebuild once, then check the store (`grep -c "MESHCOM-PATCH AE-01v2"` = 1, no `>= 2048`) and the ELF
  (`handleHeaderResponse` without `movi a8, 0x7ff`).

## Regression tests (red now, green after)

| Id  | Where                                                                             | What                                                                                                                                                                                                                                                                                                                                                                                                                    | Red today because                           |
| --- | --------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------- |
| T1  | tools/tests (pytest that compiles a host harness with c++), or a native Unity env | Vendored stock `handleHeaderResponse` text + `patch_http_text` -> compile against String/client stubs -> feed fixtures `tools/tests/fixtures/au_http/github_302.hdr` (copy of `/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/b06da057-6de3-4dc3-ace0-30f9cf4b8bb4/scratchpad/verify/fk302.hdr`), `api_latest.hdr`, `release_assets_206.hdr`. Expect 302 with the full Location, 200, 206 | v1 returns -7 on the 302                    |
| T2  | same harness                                                                      | Negative cases: Location > HTTP_MAX_LINE fails; long Transfer-Encoding / Content-Encoding / status line fail; 40 KB of unused headers fail at the budget; 4.2 KB unused line passes; trickle beyond 30 s -> -11                                                                                                                                                                                                         | v1 accepts 40 KB and rejects the 4.2 KB CSP |
| T3  | test_arduino_esp32_patch.py                                                       | `patch_http_text(stock with HTTP_PATCHED_V1)` equals `patch_http_text(stock)`                                                                                                                                                                                                                                                                                                                                           | v1 marker short-circuits, text unchanged    |
| T4  | same                                                                              | Marker present with a body that is neither V1 nor V2 -> RuntimeError                                                                                                                                                                                                                                                                                                                                                    | returns the text unchanged                  |
| T5  | same, real store                                                                  | Replace the "already patched" skip: if a marker is present, assert the installed file holds the current block                                                                                                                                                                                                                                                                                                           | skip hides a stale store                    |
| T6  | same                                                                              | Replace `HTTP_MAX_LINE >= 1024` with a fixture-derived rule: stored cap >= 2 x the longest used header (Location) in the fixtures, and the CSP fixture line > 2048 B passes behaviourally (T1)                                                                                                                                                                                                                          | pins the bug                                |

Bench check after the fix (not a host test): `--update install` on DK5EN-1 (V3) completes; log shows `buf32;rec;hit;4`
(two sessions) and no `tlscap;refused` on hop 2 (never exercised so far). AU-29 on the T-Beam stays a separate item.

## Notes

- No release from HEAD before AE-01 v2 lands: CI patches a fresh store and would ship v1 (Evidence: F5-10 row).
- The stock arduino-esp32 code has no cap at all; v2 keeps the DoS bound (4 KB per line, 32 KB per header block, 30 s)
  without depending on how long a third-party server makes headers it does not need.
