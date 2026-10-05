# Verdict: web mechanics, tests, altitude (verifier, fork-dev 02314ce2)

Paths: WF = src/web_functions/web_functions.cpp, RT = src/rm_runtime.cpp, RC = src/remote_cmd.cpp,
CF = src/command_functions.cpp, WS = src/web_functions/web_setup.cpp.

## Key questions, answered

- **Page size / heap / chunking.** No total page-size limit: pages are streamed, never buffered, `Connection: close`.
  The limits are per write call:
  - W5100S `socketSend` caps one write at SSIZE = 2048 B and `EthernetClient::write` still returns `size`. Bytes past 2048 are lost silently (.pio/libdeps/wiscore_rak4631/RAK13800-W5100S/src/socket.cpp `socketSend`, EthernetClient.cpp:83-89). The largest existing single print is already 1847 B (WF:1576, nav icons).
  - nRF52 `Print::printf` uses `char buf[256]` and then `write(buf, len)` with the UNTRUNCATED vsnprintf length (framework-arduinoadafruitnrf52/cores/nRF5/Print.cpp:190-201). Any printf output over 255 B sends stack bytes beyond the buffer, which is an over-read.
  - ESP32 `Print::printf` mallocs when the output exceeds 64 B (framework-arduinoespressif32/cores/esp32/Print.cpp:47-63).
  - W5100S waits for SEND_OK on every write, so many small writes slow the RAK.

  Rule for the new page: print const literals under 2 kB, keep each printf under 64 B, and emit a static container plus scaffold JS.

- **Flash.** Not a concern. RAK .text is 592 620 B of 815 104 B (73 %, .pio/build/wiscore_rak4631, `size`), so about 219 kB headroom. The Heltec V3 firmware.bin is 1.75 MB of 3.4 MB. String literals go to flash rodata, not DRAM, so the tight E22_XML DRAM (123 420 / 124 580, tools/resource_baseline.json) is unaffected. With no-cache (WF:4224), +3-4 kB of scaffold JS only adds per-load transfer.
- **Lifecycle.** Three places hard-code the current layout:
  - `loadPage` calls `rmCardInit();rmPoll()` only when `page=='setup'` (WF:1198).
  - `rmPoll` returns early when `#rmcard` is missing (WF:1311).
  - The interval is gated on `cpage=='setup'&&#rmcard.cardopen` (WF:1312).

  All three must change together, or the new page is silently dead.

- **jsdom.** Two styles exist today. Live-node harnesses (tools/webgui_badge_test.js, webgui_tick_test.js) load the real scaffold from a node URL and need `--insecure-http-parser`; they are not in the gate. The offline harness tools/safeboot_page_test.js runs in stage 2 (tools/regression.sh:190-191).
  An offline scaffold harness is feasible. The scaffold `<script>` block (WF:1187-1401) consists only of `web_client.print/println` string literals, with one `#if ENABLE_MSGSTORE` and no printf. An extractor can decode the C literals and load them into jsdom with a stubbed fetch.
- **Host-side C++.** WF handlers write to the global `web_client`, and test/support has no Print/Client mock (Arduino.h provides String only). The pattern that works is extraction into a pure translation unit, as src/url_decode.h with test/test_url_decode already does.
- **New file wiring.** `web_client` is a non-static global with no `extern` declaration in any header (WF:61), and `rm_wipe`, `rm_json_str` and `rm_form_decode` are `static` (WF:552-612). web_rm_page.cpp would need:
  - `extern CommonWebClient web_client;` plus web_commonServer.h;
  - the rm helpers moved into a shared header/.cpp;
  - one dispatcher line and one `rmScaffoldJs()` call inside deliver_scaffold.

  The board `build_src_filter = +<*>` (platformio.ini:108) picks the new file up automatically. Native envs list their TUs explicitly, so they are unaffected.

- **Hotspot.** WF has 4535 lines, 22 fork commits since 2026-09-01, and already differs from upstream by +534/-39 (`git diff --stat upstream/dev HEAD`).

---

### webmech-1 CSRF on new POST endpoints -> CONFIRMED (MEDIUM; overlaps auth cluster)

- The CORS headers (`Access-Control-Allow-Origin: *`, fixed Allow-Headers, WF:4220-4222) only govern reading the response. The server never checks Content-Type (WF:1127-1133), so a no-cors form POST to a new endpoint executes.
- With node_webpwd empty, every client is authorised (WF:433-434). Otherwise authorisation is by IP table (WF:390-393).
- The whole GUI is already CSRF-able: GET `/setparam/?manualcommand=--passwd x` (WS:30-36) and `/callfunction/` reboot. What is new is the slot-number send, which lets a CSRF fire SIGNED commands at OTHER nodes over the air. Today `/rmsend` needs the target password.
- Fix (sound, cheap): require a custom header `X-MC-RM: 1` on the new mutating endpoints. It is not in Allow-Headers, so a cross-origin preflight fails. Parse it in the existing header loop next to content-length (WF:1127-1133) and answer 403 without it.

### webmech-2 Set-password = unauthenticated takeover -> DOWNGRADED (LOW)

- The premise "before, this needed console/BLE" is refuted. The Manual Command box stays on Setup and already runs `--passwd <x>` via GET /setparam with no auth when node_webpwd is empty (WS:30-36, WF:3098-3099). The new endpoint adds no reach.
- These points are valid and are UI/contract issues:
  - the literal `none` clears the password (CF:3143-3152);
  - the same field re-keys the net console and KISS (CF:3145-3163);
  - a value is stored space-padded `%-14.14s` (CF:3158).
- Fix: extract the `--passwd` body into one function called by both paths. Reject `none`, empty, and leading/trailing spaces. Label the field "also net console + KISS password".

### webmech-3 Password length/encoding mismatch -> DOWNGRADED (LOW)

- The trailing-space claim is refuted: `rmDeriveKey` strips trailing spaces (RC:317-325), so "abc " and "abc" give the same key on both sides.
- Non-ASCII passwords over 14 bytes are rejected VISIBLY by the sender with `passwd` (RT:603-611). There is no silent verify/lockout loop.
- Remaining issue: the set path truncates at 14 BYTES, possibly mid-UTF-8 (CF:3158), so a GUI-set long non-ASCII password can never be matched.
- Fix: one validator "printable ASCII 0x21-0x7E, 1..14" on set and on slot save, mirrored with an input `pattern`.

### webmech-4 Password managers ignore autocomplete=off -> DESIGN-DECISION (LOW)

- The fields use `autocomplete="off"` (WF:3300-3314). Browser behaviour cannot be verified from the repo.
- `new-password` stops autofill but makes Chrome offer password generation.
- Fix: `autocomplete="new-password"` on password inputs, a non-`username` name on the call field, and a manual check in Chrome and Safari.

### webmech-5 rmKeep holds the plaintext password -> CONFIRMED (MEDIUM)

- `rmKeep={...pw:''}` (WF:1302) is filled on every keystroke (WF:3302) and restored on reload (WF:1306). It is never cleared.
- On a page that promises write-only secrets, this contradicts the promise.
- Fix (cheapest): never mirror password fields into rmKeep, and set `input.value=''` in the fetch `finally`.

### webmech-6 No tile/modal component; confirm() only -> DESIGN-DECISION (LOW)

- There is no tile CSS (WF:1407-1545). `confirm(` occurs 10 times, including the nav reboot (WF:1594).
- The "blocks polling" harm is cosmetic: polling is only delayed. The webview-suppression claim is unverified.
- An armed two-tap tile is a UX choice. If chosen, keep it data-driven in the scaffold JS.

### webmech-7 HTML emission cost -> CONFIRMED with correction (MEDIUM, as a constraint)

- The real limits are per write (see Key questions): W5100S drops bytes past 2048 silently, nRF52 printf over-reads above 255 B, and ESP32 printf mallocs above 64 B. Total page size is not limited.
- The existing card's option printf lines stay under 64 B and are fine (WF:3308).
- The fix is sound: a static container plus a scaffold JS array plus one const SVG sprite. Budget: no literal at or above 2 kB, and no printf output above about 200 B on any path that also builds for the RAK.

### webmech-8 Sidebar overflow -> CONFIRMED (LOW)

- There are 11 base buttons plus 3 optional (mailbox, mcp23017, logout) and the logo (WF:1564-1600). The pitch is 60+20 px times widthfactor, with `.extra_space` 40 px (WF:1422-1426), so the bar is about 970-1250 px tall.
- Reboot and Logout already scroll on a 900 px laptop. Remote adds 80 px (56 px at widthfactor 0.7).
- Fix as proposed: place Remote after Setup and add `@media (max-height:900px)` margins.

### webmech-9 Lifecycle hard-wired to setup; 401 invisible -> CONFIRMED (MEDIUM)

- Evidence for the hard-wiring: WF:1198, 1311, 1312 (see Key questions).
- Session expiry: an unauthenticated non-page request gets a 401 with an empty text body (WF:984-987). `r.json()` throws and is swallowed (WF:1311), or shows "request failed" (WF:1315). The session expires after 4 h idle (WF:378).
- The loadPage XHR callback has no `cpage==page` guard (WF:1198), so a slow response renders under the wrong nav button. This is cosmetic.
- Fix (sound): page hook `remote`; a single timer cleared in loadPage; an `rmFetch` wrapper that maps 401 to `loadPage(cpage)`; delete the setup hooks and ids in the same change.

### webmech-10 POST handler constraints -> DOWNGRADED (LOW)

- BODY_MAX=200 (WF:618) fits every concept body.
- (b) is refuted for POST: work_webpage stops reading at the blank line and only the handler reads the body (WF:945-1123), so a body is never echoed. The quiet rule (WF:938-940) only matters for GET variants.
- (c) Referer hijack is not realistic: the scaffold URL is `/`.
- Valid DRY point: the body loop is duplicated (WF:496-534 vs 630-649).
- Fix: one `read_body(buf,max,len)` helper; route new `/rm*` paths with startsWith before the indexOf chain, as /rmsend does (WF:996-1008).

### webmech-11 No remote state source -> CONFIRMED (LOW)

- /rmstatus returns the own node's state plus the last 5 sent commands (kSentN=5, RT:37; WF:712-763). There is no remote on/off state.
- Fix: tiles show delivery state only (sent, verified, unverified, waiting), keyed by ctr.

### webmech-12 Command list duplicated -> CONFIRMED (LOW)

- The list exists three times: `rmCmds[]` (WF:3305-3307), `allowed()` (RC:130-158), and ALLOWLIST (tools/remote_cmd.py:55).
- Fix (cheapest): a Unity case that runs every web/tile command id through `rmCommandAllowed` (RC:481), plus the tile list in tools/tests/remote_cmd_vectors.json so that test_remote_cmd.py compares it with ALLOWLIST.

### tests-1 RM JS untested, web harnesses not in gate -> CONFIRMED (HIGH)

- grep for rmsend/rmstatus/rmPoll/rmCard under tools/ and test/ returns nothing. Stage 2 runs only tools/tests/*.mjs and safeboot_page_test.js (tools/regression.sh:184-192).
- Fix (sound, feasible): an offline jsdom harness that extracts WF:1187-1401 literals plus a fixture fragment, added to stage 2 next to the safeboot step.

### tests-2 Mock-testing risk; need differential vectors -> CONFIRMED (MEDIUM)

- `rm_form_decode` is `static` in WF (WF:568-586) and cannot be host-tested.
- It is also a second percent-decoder next to url_percent_decode (src/url_decode.h, already native-tested), with different rules: it rejects control bytes, while url_percent_decode drops %00.
- Fix: move the parse and decode into a pure TU with a native suite. A shared vector file with nasty passwords is used by both the JS harness and Unity. Include negative mutants: `pw` in the URL, missing slot, and confirm skipped.

### tests-3 "Password never appears" needs native + live proof -> CONFIRMED (HIGH), with one addition

- No Print mock exists (test/support/Arduino.h has String only), so the native proof requires an extracted buffer-in/buffer-out function.
- Addition: the live canary test must carry a POSITIVE CONTROL, or it cannot fail meaningfully. First send the canary through the existing leaking path, GET `/setparam/?manualcommand=--passwd CANARY` with bDEBUG on. That request line is echoed char by char (WF:942-943) and printed again by `Processing Param ... value` (WS:26-27). Assert that the grep FINDS it there, then assert that it is absent for the new endpoint.
- Scan the serial log, the 2323 console, config export (test_config_json exists), and --info.

### tests-5 localStorage / browser-side alternative -> DESIGN-DECISION

- **Browser-side SIGNING is not viable cheaply.** crypto.subtle is undefined on `http://<LAN-IP>` (an insecure context), so the scaffold would have to ship a JS SHA-256/HMAC.
- The sender counter is node state: `s_lastSent` is persisted via `rmSndSave` BEFORE the send, max'ed with clock and peer hwm (RT:629-677). The reply is verified with the key the node keeps for 10 min (RT:707-708). A browser signer therefore needs a counter endpoint plus a moved verify path.
- **The localStorage-PASSWORD variant is viable** with zero firmware store. The browser keeps the password and sends it via the existing POST /rmsend body (WF:614-710); the node still derives the key and signs. Wire exposure is unchanged compared with today.
- Costs: per browser and per origin (= node IP, lost on a DHCP change; a later device on a reused IP shares the origin and can read it); it persists in plain text in the profile; it exposes the target's net-console/KISS password, not only K.
- Storing K instead of the password needs JS SHA-256 (no subtle) plus a key-taking /rmsend.
- This is the user's call: firmware slots (shared across browsers, CSRF-reachable) vs browser store (per device, zero flash store).

### tests-6 innerHTML blind spots -> CONFIRMED (MEDIUM)

- (a) and (b) as webmech-9.
- (c): the loadPage callback writes innerHTML without checking that the page still matches (WF:1198). The effect is cosmetic.
- (d) is a real drift risk: element ids live in C strings in two places.
- (e) is the cheapest high-value check: run `new Function(src)` on the extracted scaffold. One stray quote kills every page.
- Fix: all of these in the tests-1 harness.

### tests-9 DRY / new file -> CONFIRMED with correction (MEDIUM)

- Correction: `sub_page_neighbours()` is NOT in its own file. It is defined in WF:2377 and only forward-declared at WF:70. There is no precedent for a separate page TU.
- `rmSendCommand` already builds with a derived key (RT:655-660 `rmBuildCommand(...key...)`; RC `tagOfKey`), so a key entry point is a split, not a fork.
- Fix: `rmSendCommandKey(dst,key,...)` with the password wrapper deriving the key and delegating; the new TU wired as in Key questions.

### tests-10 Destructive-button tests; double send burns counters -> DOWNGRADED (LOW)

- The double-send counter burn is refuted: the `busy` check (same dst within RM_RATE_MS 10 s, RT:620-627; remote_cmd.h:40) runs before the counter is bumped. A second click returns `busy` and uses no counter.
- Still valid: stub `confirm` false then true and assert fetch count; give each error code (dst, passwd, cmd, busy, ctr, store, send) its own message.

### tests-11 Live checks manual, HTTP quirk -> CONFIRMED (LOW)

- `--insecure-http-parser` is required (header of tools/webgui_badge_test.js). RAK-90 is reachable by IP only.
- Fix: an opt-in stage-3 step, own node only, with an oversize and a truncated body on the RAK expecting a clean 4xx.

---

## Missed by all finders (verified)

- **M1 (MEDIUM) RAK write limits: nRF52 printf stack over-read and silent W5100S truncation.**
  - Print.cpp:190-201 (adafruit nRF52) writes `len` bytes from a 256 B stack buffer when the output is longer. That sends stack contents, which can include key or password residue.
  - EthernetClient::write reports success for writes over 2048 B while socketSend sent only 2048.
  - The webmech-7 proposal of "printf of 150-400 B per tile" would trigger the over-read.
  - Fix: a hard rule in the brief, plus a host lint that flags `web_client.printf` format strings over 200 chars and print literals over 1900 B.
- **M2 (MEDIUM) The concept's "never in logs/URLs" goal is already broken by the kept Manual Command box.**
  - `--passwd x` travels in a GET URL (WS:30-36). It is echoed under bDEBUG (WF:942-943) and printed at WS:26-27.
  - Either state that limitation, or have the new page be the documented way to set the password and stop echoing `manualcommand` values that start with `--passwd`.
- **M3 (LOW) Unauthenticated GET /rmsend leaves the URL in `web_header`.**
  - The wipe only runs in the authenticated branch (WF:1000-1002 vs 975-987). The global, reserved String is reset with `= ""`, which leaves the bytes in the buffer.
  - Fix: wipe `web_header` for any `GET /rm` before the auth branch.

## Top 5 confirmed for the user

1. tests-1 / tests-6: there are no RM GUI tests in the gate. An offline jsdom harness over the extracted scaffold literals (WF:1187-1401) is feasible and should include the `new Function` syntax check.
2. tests-3 + M2: the "password never leaks" proof needs a positive control. Today the Manual Command path DOES leak `--passwd` to serial, so the grep test can fail.
3. M1 / webmech-7: on the RAK, per-write limits apply (printf at most 255 B or the stack is over-read; literals under 2048 B). Build the page as a static container plus scaffold JS.
4. webmech-9: the lifecycle is hard-wired to `setup`/`#rmcard` (WF:1198/1311/1312), and a 401 after 4 h is swallowed. Replace the hooks in one change and map 401 to the login page.
5. webmech-1 / tests-5: slot-number sending makes CSRF reach other nodes. Either add the custom-header gate or choose the browser-stored-password variant. Browser-side signing is not viable: there is no crypto.subtle on HTTP, and the counter and verify key live in the node.
