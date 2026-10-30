# Campaign WEB-AUTH-01 -- web login: Enter key, autofocus, no plaintext password on the wire

**Status:** W0-W3 done 2026-10-30, fork-dev `9ac4f45f` (W1/W2 advisor APPROVED after rework, gate stage 1,2
PASS, bench DK5EN-1 + DK5EN-98 PASS). W4 upstream PR next. Base `18f04e26`.

## BLUF

- **Bug #1241:** `sub_page_login()` has no `<form>`, so Enter does nothing; the button tag is also unclosed.
- **Leak 1:** the login sends the password in plaintext (`GET /?nodepassword=<pwd>`, unencoded) over HTTP.
- **Leak 2:** a login authorises the client **IP** for 4 h; every device behind the same NAT/IP is logged in.
- **Fix:** form + autofocus; nonce-based HMAC-SHA256 login (the password itself never crosses the wire); a random 128-bit
  session cookie replaces the IP table. Plaintext `/?nodepassword=<pwd>` is refused.
- **Not fixed (needs TLS):** a passive sniffer can still steal a session cookie; settings changes after login travel
  in plaintext; `/rmpasswd` sends the RM secret in a POST body.
- **Residual risks (accepted):** a sniffer who captures one nonce + mac pair can run an offline dictionary attack on
  the password (HMAC-SHA256 is fast, passwords are at most 19 chars): a long random web password matters. Any LAN
  client can keep the login locked by 5 bad attempts per minute (open sessions stay valid) or flood `/?loginnonce`
  to evict a pending nonce (the user retries).

## Contract C1 (wire)

| Request                               | Auth needed | Response                                                                                                                 |
| ------------------------------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------ |
| `GET /?loginnonce`                    | no          | 200, body = 32 lowercase hex (16 random bytes). Throttled: 403 `{"ok":false,"err":"throttled"}`                          |
| `GET /?login=<nonce32>.<mac64>`       | no          | 200 `{"ok":true}` + `Set-Cookie: mcs=<32hex>; Path=/; HttpOnly; SameSite=Strict`. Else 403 `{"ok":false,"err":"<code>"}` |
| `GET /?logout`                        | no          | 200, drops the session of the request cookie, `Set-Cookie: mcs=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict`           |
| `GET /?nodepassword=` (empty, legacy) | no          | treated as `/?logout` (cached old pages)                                                                                 |
| `GET /?nodepassword=<x>` (legacy)     | no          | 403 `{"ok":false,"err":"plaintext"}`                                                                                     |
| everything else, password set         | yes         | session valid iff the `Cookie` header carries `mcs=<32hex>` of a live slot; else login page / 401 as today               |

- `mac = HMAC-SHA256(key = UTF-8 bytes of the password, msg = the 32 ASCII hex chars of the nonce)`, lowercase hex.
- Error codes: `format` (malformed arg), `nonce` (unknown, expired or used), `badpw`, `throttled`.
- Nonce: 4 slots, valid 30 s, single use (consumed by any login attempt that names it), oldest replaced when full.
- Session: 10 slots, 4 h idle timeout (refreshed by each valid request, same as the old IP table), LRU eviction.
- Throttle: 5 failed logins within a 60 s window that starts at the first failure block nonce issue and login for
  60 s (fixed window, not sliding: at most about 8 guesses per minute in the worst case).
- `--webpwd` (any change) drops every session and nonce.
- The comparison of MACs and tokens is constant-time. Timing uses `millis()`, wrap-safe.
- `/?login` and `/?logout` count as state-changing for `web_guard.h` (need `X-MC: 1` / same-origin when guarded).

## Contract C2 (C API, `src/web_functions/web_auth.h`, pure C++11, no Arduino)

```
typedef bool (*WebAuthRng)(uint8_t *out, size_t n);                                   // false = no randomness
enum WebAuthResult { WA_OK, WA_FORMAT, WA_NONCE, WA_BADPW, WA_THROTTLED, WA_RNG };
void webAuthReset();
bool webAuthIssueNonce(uint32_t now_ms, WebAuthRng rng, char out_hex[33]);            // false = throttled
WebAuthResult webAuthLogin(const char *arg, size_t len, const char *password, uint32_t now_ms,
                           WebAuthRng rng, char out_token_hex[33]);                   // arg = "<nonce>.<mac>"
bool webAuthSessionValid(const char *cookie, size_t len, uint32_t now_ms);           // Cookie header value; refreshes
void webAuthLogout(const char *cookie, size_t len);
bool webAuthCookieToken(const char *cookie, size_t len, char out_hex[33]);           // finds mcs=<32hex>
const char *webAuthErrCode(WebAuthResult r);                                          // "format", "nonce", ...
```

## Contract C3 (JS, `src/web_functions/web_login_page.cpp`)

`webLoginScaffoldJs()` prints, inside the scaffold `<script>`: `mcSha256`, `mcHmacHex(keyStr, msgStr)`,
`login(pwd)` (nonce, HMAC, `/?login=`, reload on 200, message in `#loginerr` on 403), `logout()` (`/?logout`, reload),
`mcLoginFocus()` (focus `#nodepassword` if present). `sub_page_login()` moves there: `<form onsubmit>`,
`type=submit`, `autocomplete="current-password"`, `#loginerr`. Markers `LOGIN-JS-BEGIN/END`, `LOGIN-HTML-BEGIN/END`.

## Waves and file ownership

| Wave | Agent                     | Exclusive files                                                                                                                                             | Verification                                      |
| ---- | ------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------- |
| W0   | orchestrator              | `platformio.ini` (`[env:native_web_auth]`), `web_functions.h` (decl), this doc                                                                              | done                                              |
| W1a  | implementer Sonnet medium | `src/web_functions/web_auth.h`, `web_auth.cpp`, `test/test_web_auth/`, `src/web_functions/web_guard.h`, `test/test_web_guard/test_main.cpp`                 | `pio test -e native_web_auth -e native_web_guard` |
| W1b  | implementer Sonnet medium | `src/web_functions/web_login_page.cpp` (new), `tools/webgui_login_test.js` (new)                                                                            | node only, no pio                                 |
| W1   | advisor Opus medium       | review W1 diff against C1-C3                                                                                                                                |                                                   |
| W2   | implementer Sonnet medium | `src/web_functions/web_functions.cpp`, `src/command_functions.cpp` (`--webpwd` hook only), nRF52 RNG shim location per brief                                | native suites + builds at gate                    |
| W2   | orchestrator gate         | `tools/regression.sh` (login test step), `docs/test-suite-map.md`; `regression.sh --stage 1,2`; builds Heltec V3, E22, T-Deck, RAK; RAM/flash vs `18f04e26` |                                                   |
| W2   | advisor Opus medium       | whole campaign diff                                                                                                                                         |                                                   |
| W3   | orchestrator, serialized  | OTA DK5EN-98 (operator OK 2026-10-30), set web password, Chrome matrix                                                                                      |                                                   |
| W4   | orchestrator              | commit, RESUME/BACKLOG, `/submit-pr` (firmware + tests, German body, closes #1241)                                                                          |                                                   |

## Log

- 2026-10-30 W0: root cause verified at `web_functions.cpp:1836` (no form), `:1375` (plaintext GET),
  `:413-470` (IP session). `crypto.subtle` is unavailable on `http://node.local` (not a secure context), so the JS
  carries its own SHA-256. Upstream has `web_guard.h` and `hmac_sha256.*`, not the fork test tree.
- 2026-10-30 W1: web_auth (23 then 27 native tests), web_guard tokens (54), login JS + jsdom test (27 checks, 3
  mutations fail, old markup fails 12). Advisor APPROVED; applied: last-char compare tests, residual-risk notes,
  fixed-window throttle wording, logout catch.
- 2026-10-30 W2: integration in web_functions.cpp (cookie session replaces the IP table, C1 routes, Set-Cookie,
  quiet serial echo for login arg and Cookie line, scaffold hook, `--webpwd` resets sessions). Rework by the
  orchestrator: the RNG callback reports failure (WA_RNG) instead of a `micros()` fallback on nRF52; advisor
  required the legacy `/?nodepassword=` request line to be wiped from `web_currentLine`; Cookie scan finds `mcs=`
  beyond 255 bytes; Web-API_documentation.txt documents the login for scripts.
- 2026-10-30 gate: `regression.sh --stage 1,2` PASS (84 envs, 3111/3111, `webgui_login_test.js` 27, golden
  `variant-ini-effective.json` regenerated for `native_web_auth` only). Builds Heltec V3, RAK4631, E22, T-Deck Plus,
  T-Beam OK. Cost vs `18f04e26`: E22 +456 B RAM / +5.2 kB flash, RAK +472 B RAM / +4.8 kB flash (86.8 %). Firmware
  strings: `mcHmacHex`, `loginnonce`, `SameSite=Strict` present, old plaintext login JS absent. Commit `9ac4f45f`.
- 2026-10-30 W3 bench (HTTP script `webauth_bench.py`, logs `tools/bench/runs/harness/webauth-*.log`):
  DK5EN-1 (USB flash, identity guard ok) 19/19: login page and 401 without cookie, nonce/login/cookie, replay 403,
  wrong password 403, plaintext 403, `mcs=` behind 400 B of foreign cookies, `xmcs=` and last-char-changed tokens
  refused, logout, `--webpwd` drops sessions, throttle after 5 failures and release after 60 s; the documented
  curl/openssl recipe logs in. Chrome: autofocus on load, Enter with wrong password shows "Wrong password" and
  refocuses, Enter with the right one logs in, `loadPage` works with the cookie, after logout `loadPage` shows the
  login form with focus. DK5EN-98 (OTA via `webflash.py`, 72 s; no 2323 login in `.env`, so the password was set
  via `/setparam/?manualcommand=` and the identity guard was not run; HTTP-only test, no RF traffic) 17/17 without
  the throttle case, Chrome Enter login. Both nodes: web password removed again.
