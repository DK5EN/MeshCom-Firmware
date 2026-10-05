# Verifier verdict: SECURITY/AUTH cluster (fork-dev 02314ce2)

W = src/web_functions/web_functions.cpp, WS = src/web_functions/web_setup.cpp, C = src/command_functions.cpp, R = src/rm_runtime.cpp. All lines read in this pass.

## Settled facts (code evidence)

- **Auth model.** W:373-434: if `node_webpwd` is empty, then `bPasswordOk = true` for every client (W:433-434). If it is set, auth is by source IP: a 10-entry `web_ip[]` table with a 4 h idle expiry (W:375-402). There is no cookie and no token. Login is `GET /?nodepassword=<pw>` (W:1196, W:955-970), compared with `is_equ`, with no throttle (W:415). When the user is not authenticated, every non-page path returns 401 (W:984-987), so `/rmsend` is protected exactly as well as `node_webpwd` is.
- **CORS.** Every `send_http_header` sends `ACAO: *`, `Allow-Methods: GET, POST, OPTIONS` and `Allow-Headers: ... Origin, Content-Type, Accept` (W:4220-4222); `/config.json` sends the same (W:809).
  - The node never reads `Origin` or `Host`. The header loop parses only `content-length` (W:1127-1133).
  - The node never reads `Content-Type`.
  - For an authenticated client, `OPTIONS /rmsend` falls through to `deliver_scaffold()`, which answers 200 with those CORS headers (W:1117-1119, W:1178). A preflight that asks for `Content-Type: application/json` therefore SUCCEEDS. A custom header such as `X-RM` is not in Allow-Headers, so a preflight that asks for it fails.
- **State changes over GET already exist, and they are total.**
  - `GET /setparam/?manualcommand=<anything>` runs `commandAction()` on any `--` command, with no filter (WS:30-39).
  - Routing is `indexOf` over the whole header, with any method (W:1025-1029).
  - Consequence: a LAN host (with no webpwd) or a CSRF `<img src>` (from an IP that holds a session) can already run `--passwd X`, `--passwd none`, `--remotemgmt on`, `--reboot`, `--setssid`, and so on, on THIS node today.
- **Is a slot send an escalation?** Yes, but it is lateral, not local.
  - No existing path lets node A's web or LAN attacker command nodes B, C or D. `rmSendCommand` needs the clear password in the body (W:671-695, R:603-611).
  - The only key copies in RAM are the `s_sent[]` keys: never exported, wiped after 10 min (R:692-709, RM_CACHE_MS).
  - Slots convert "reach A's web page (or CSRF it)" into "allowlisted commands on 3 remote nodes" (reboot, gateway/mesh off, txpower, ...).
  - `rmSendCommand` has no `node_rm` gate (R:570-717), so this works even with RM off on A.
- **Config export.**
  - `node_passwd` is exported in clear (src/config_json.h:305), as are `node_webpwd` (:313) and the WLAN PSK (:315).
  - This is an explicit operator decision (config_json.h:40-44; docs/adr-remote-hmac.md RM-D1 "exported in clear ... accepted").
  - Combined with `ACAO: *` (W:809), the file is readable cross-origin by any page whose request the browser lets through.
- **Shared password.**
  - `--passwd` writes `node_passwd` `%-14.14s` and calls `netConsoleSetPassword` and `kissSetPassword` (C:3157-3164). `--passwd none` clears all three (C:3144-3153).
  - Both setters are `#if defined(ESP32)`. On the RAK4631 there is no console or KISS lockout.
  - Net console HMACs the RAW trimmed password (net_console.cpp:10, 304-311), and so does KISS (kiss_functions.cpp:576-582). RM uses SHA-256(trimmed) (remote_cmd.cpp:317-326).
- **All Serial output is mirrored to the 2323 console on ESP32** (net_console.cpp header: `#define Serial MSerial`). This applies to the bDEBUG echo at W:942 and to `Processing Param` at WS:27.

---

### security-1 Slot-number send turns web reachability into control of 3 remote nodes -> CONFIRMED (HIGH)

- **Code:** W:433-434 (no webpwd = open); W:616-695; R:603-611 (password needed today).
- **Gap:** no other path to B/C/D keys (see facts above). No `node_rm` gate on the sender side (R:570-717).
- **Fix (sound, cheapest):** refuse slot send, slot save/delete with 403 `webpwd` while `node_webpwd` is empty, plus the X-header and Host guard below.
- **Nonce:** sound but optional. An inline two-step confirm in the GUI is enough for UX; the header guard already stops blind forgery.

### security-2 No CSRF/Origin check, IP auth, CORS * -> CONFIRMED (HIGH), with one factual correction

- **Code:** W:375-402 (IP session); W:4220-4222 and W:809 (ACAO *); W:1127-1133 (no Origin, Host or Content-Type parse).
- **Correction:** "the node does not even parse OPTIONS" is wrong.
  - OPTIONS gets 200 plus the permissive CORS headers from the scaffold fall-through (W:1117-1119).
  - So `Content-Type: application/json` ALONE is NOT a guard.
- **Fix:** sound only with BOTH of these on the new `/rm*` POSTs:
  - Require a custom header (`X-RM: 1`). It is not in Allow-Headers, so the preflight fails.
  - Check that Host is an IPv4 literal, the node's hostname, or `.local`. This covers DNS rebinding, where the attacker page is same-origin and so CAN send the header.
- **Also:** drop ACAO * from `/rmstatus`, `/rmsend` and `/config.json`.
- **Limit:** none of this protects own-node settings while GET `/setparam/?manualcommand=` stays open (missed-1).

### security-3 config.json exports node_passwd in clear -> DESIGN-DECISION (MEDIUM)

- **Code:** src/config_json.h:305, :40-44; ADR RM-D1 (the operator decision dated 2026-08-30 already accepts "whoever reads a config export can send RM1").
- **Error in the concept:** the claim "password write-only, never sent back" is false for THIS node's password; only the new slot keys can be kept out.
- **Real defect:** `ACAO: *` on the download (W:809).
- **Fix:**
  - Concept text: "this node's password is part of the config backup".
  - Remove ACAO * at W:809.
  - Keep slots out of the export, and say so on the download page.

### security-4 What the derived-key store buys -> DESIGN-DECISION (MEDIUM)

- **Buys:** a dumped slot key is not a credential for the target's 2323 console or KISS, which take the raw password (net_console.cpp:304-311; remote_cmd.cpp:317-326).
- **Does not buy:**
  - The key is a full, replayable RM credential (pass-the-hash): unsalted and unstretched (ADR "no key stretching").
  - The on-air RM1 frame already gives the same offline oracle for weak passwords (ADR Consequences).
- **Fix:** UI/ADR text "saved key = full RM access to that node", plus a minimum-length hint.
- **Encryption at rest:** do not claim it.

### security-5 Password-set endpoint: request echo and heap residue -> DOWNGRADED (LOW)

- **Echo:** `work_webpage` echoes only the bytes it reads, which stop at the blank line (W:924-945). A POST body is read later by the handler, so a body-only POST endpoint leaks nothing through the echo, even without the quiet flag (W:938-940).
- **Real risk:** a GET variant, or a password in the URL. The existing Manual Command `--passwd` does exactly that (missed-3).
- **Fix:**
  - New endpoint POST-only, with 405 + `rm_wipe_string` for GET.
  - Generalise the quiet prefix to `/rm`.
  - Route by `startsWith` before the `indexOf` chain at W:1010.

### security-6 Plain HTTP, GET login, no throttle -> DESIGN-DECISION (LOW, pre-existing)

- **Code:** W:1196 (login over GET), W:415 (no throttle), W:375 (IP-keyed session).
- **Scope:** all true, all independent of this concept. HTTPS is not feasible on nRF52.
- **Fix:** label "crosses the LAN in clear" at the Set button. A login throttle is a separate backlog item.

### security-7 Changing node_passwd also re-keys or opens console and KISS -> CONFIRMED (MEDIUM, ESP32 only)

- **Code:** C:3144-3164; the setters are `#if defined(ESP32)`, so the RAK has no lockout.
- **Not new as a capability:** Manual Command `--passwd none` does the same today (WS:30-39).
- **New risk:** a button labelled as an RM control hides the side effect.
- **Fix (cheapest sound):**
  - Extract one `setNodePasswd(const char*)` / `clearNodePasswd()` used by both C:3138 and the endpoint. This also avoids the literal password "none" clearing it.
  - Ask for the new password twice.
  - Clear needs a confirm that says "also opens console 2323 and KISS".
  - Splitting off an RM key (`node_rmkey`) is a separate ADR change, not needed here.

### security-8 Slots vs factory reset, format, config import -> CONFIRMED (MEDIUM)

- **Code:** esp32_flash.cpp:500-515 clears only the `Credentials` NVS namespace; nrf52_flash.cpp:659-720 removes targeted files, with `format()` only as a fallback.
- **Gap:** a new namespace or file survives `--cleanflash`, so a node that is handed over keeps 3 third-party RM keys.
- **Fix:** one line per platform (clear the slot namespace / remove the slot files in the reset path), plus a "forget all saved nodes" button. Import/export exclusion is by design; document it.

### security-9 Retries with a stale key lock the target for 5 min -> CONFIRMED (MEDIUM)

- **Code:** remote_cmd.h:40-44 (3 rejects in 90 s give a 5 min lock; tag and replay rejects are silent).
- **Sender gate:** only a 10 s `busy` per dst (R:620-627). Three clicks in 30 s are enough.
- **Clock:** only a lower bound (R:313-321); there is no upper bound.
- **Fix:** per-slot GUI back-off after 2 unverified sends, no auto-retry, and a "Test key" via `sync`. The upper clock bound belongs to the protocol cluster.

### security-10 Slot index without identity -> CONFIRMED (LOW)

- **Problem:** a stale page or a second tab can target the wrong node with a valid key.
- **Fix (cheapest):** send `slot=N&dst=<call>`, and the node rejects on mismatch. A generation counter is not needed.

### security-11 Validation and wipe on the slot write path -> DOWNGRADED (LOW)

- **Existing pieces to reuse:** `rm_wipe` (W:552), `rm_json_str` (W:589), and the dst/password checks in `rmSendCommand` (R:578-611).
- **Status:** hygiene advice, not a defect.
- **Fix:** one shared parse/validate/derive/wipe helper, and a key-taking `rmSendCommandKey()` that the password path calls after deriving.

### security-12 Slot keys via debug paths -> DOWNGRADED (LOW)

- **Status:** speculative, since there is no code yet.
- **Precedent:** mask_secret.h:1-16 shows the incident class is real.
- **Fix:** `/rmstatus` returns `{slot, call, set}` only, plus a canary-grep test (see tests-altitude-3).

### protocol-1 Slot-number send = CSRF/LAN control -> CONFIRMED (HIGH)

- Duplicate of security-1 and webmech-1. Same evidence and fix.

### tests-altitude-4 Derived-key store "buys no secrecy" -> DOWNGRADED (MEDIUM)

- **Wrong:** "node_passwd is already stored plaintext, so storing K protects nothing". The slot keys are OTHER nodes' secrets, held nowhere else on this node.
- **Right:** K protects their console/KISS raw password (security-4); plaintext slots would be strictly worse.
- **Correct part:** pass-the-hash, and the gate on webpwd.

### tests-altitude-8 "This node" password field also changes console and KISS -> CONFIRMED (MEDIUM)

- Duplicate of security-7. Evidence C:3157-3164. ESP32 only.

### webmech-1 CSRF-able POST endpoints (CORS *, simple request, IP session) -> CONFIRMED (HIGH)

- **Accuracy:** the most accurate of the three, including that OPTIONS reaches the scaffold and that Allow-Headers contains Content-Type (W:1117-1119, 4222).
- **Fix:** the X-header part is sound. Add the Host check (missed-2), because without it rebinding defeats the header.

### webmech-2 Set-password over an open GUI = new RM takeover -> DOWNGRADED (LOW)

- **Refuted:** "before, this needed console/BLE". `GET /setparam/?manualcommand=--passwd%20X` followed by `manualcommand=--remotemgmt%20on` does it today, with no auth when webpwd is empty, and is even GET-CSRFable (WS:30-39, W:1025).
- **Valid sub-points:**
  - Use an explicit set/clear action, not the text path (so a password "none" cannot clear it).
  - Reject a leading space: `node_passwd[0]==' '` reads as "not set" (mask_secret.h:30, W:3277).
  - Use the same setters as the console path.
- **Not worth it:** requiring the current password, while manualcommand is open.

### webmech-4 Password managers capture the fields -> CONFIRMED (LOW)

- **Code:** W:3300-3302 use `autocomplete="off"`; the login form at W:1643 has none.
- **Fix:** `autocomplete="new-password"` on every write-only password input, plus `data-1p-ignore` / `data-lpignore` on the call field.

### webmech-5 rmKeep holds the plaintext password for the tab lifetime -> CONFIRMED (LOW)

- **Code:** W:1302, W:1306, W:3302 (`oninput` mirrors into `rmKeep.pw`).
- **Fix:** on the new page, never mirror password inputs into `rmKeep`, and clear the input in the fetch `finally`.

### webmech-10 (echo/route part) quiet rule is /rmsend-specific; indexOf routing -> DOWNGRADED (LOW)

- **Code:** W:938-940; W:1010-1044 (`indexOf` over the whole header).
- **Scope:** echo exposure exists only for GET or URL-borne secrets (see security-5).
- **Fix:** a `startsWith("POST /rm") || startsWith("GET /rm")` quiet rule, and `/rm*` routes placed before W:1010.

---

## Conflicts between finders

- **security-2 vs webmech-1 on OPTIONS:** webmech-1 is right (W:1117-1119). A JSON Content-Type gives no protection; only a non-allowlisted custom header does, plus a Host check.
- **tests-altitude-4 vs security-4 on what K buys:** security-4 is right (see above).
- **tests-altitude-5 vs the security-1 fix:**
  - A browser-side (localStorage) or McApp password store removes the HIGH lateral-escalation root cause entirely: the node holds no third-party keys, and every send still needs the secret in the body.
  - The firmware-slot design needs the webpwd gate, the X-header and the Host check to come close.
  - This is a design choice for the user.

## Missed by all finders (verified)

1. **GET /setparam/?manualcommand= runs any console command (WS:30-39; routing W:1025, any method).**
   - This is a pre-existing GET-CSRF, `<img src>` able: full own-node takeover (`--passwd`, `--remotemgmt on`, `--setssid`, `--reboot`).
   - Unauthenticated on the LAN when webpwd is empty.
   - So the concept's CSRF guards on the password-set and RM-toggle endpoints are cosmetic. Only the slot-send guard changes the threat picture.
   - Decide explicitly: either guard `/setparam` (POST + X-header, which also touches the existing `setvalue()` JS), or accept this and document it.
2. **DNS rebinding:** `Host` is never parsed (W:1127-1133).
   - A rebound attacker page is same-origin, so it can send any custom header.
   - The TCP comes from the victim's IP, so the IP session passes (W:390).
   - The X-header guard alone does not protect slot sends; add a Host allowlist.
3. **The Manual Command `--passwd X` puts the password in a GET request line.**
   - It is echoed under bDEBUG (W:942) and by `Processing Param` (WS:27). On ESP32, Serial is mirrored to the 2323 console (net_console.cpp `#define Serial MSerial`), and 2323 is unauthenticated while `node_passwd` was empty.
   - It also stays in the unwiped `web_header` String.
   - The new POST password field fixes this only if the GUI steers users to it. Consider masking `manualcommand` values that start with `--passwd` / `--webpwd` in WS:27 and the echo.

## Top 5 confirmed for the user

1. **Slot-number sends are a real lateral escalation.** From "reach or CSRF node A's web page" to "reboot, gateway off or txpower on 3 remote nodes" (security-1 / protocol-1 / webmech-1). Gate on `node_webpwd` set, add an `X-RM` header, and add a Host allowlist. Alternatively keep the secrets in the browser or McApp (tests-altitude-5) and the problem goes away.
2. **CSRF guards must be a custom header plus a Host check.** A JSON Content-Type alone is not enough, because OPTIONS returns 200 with Content-Type allowed (W:1117-1119, 4222), and rebinding defeats a header-only guard (missed-2).
3. **"Password never sent back" is false for this node.** config.json exports `node_passwd` by operator decision, and `ACAO: *` (W:809) makes it cross-origin readable. Fix the concept text and drop ACAO * on `/config.json`, `/rmstatus` and `/rmsend`.
4. **Set and Clear also re-key or open the 2323 console and KISS on ESP32** (C:3144-3164). Use one shared setter (no "none" text path), enter the new password twice, and require a confirm on Clear.
5. **The slot store survives `--cleanflash`** (esp32_flash.cpp:500-515), and stale-key retries lock the target for 5 min (remote_cmd.h:40-44). Wipe the slots on reset, and add GUI back-off after 2 unverified sends.
6. **Context, not concept:** GET `/setparam/?manualcommand=` already gives full own-node control via GET-CSRF (missed-1). Own-node guards in the new page are cosmetic unless that is addressed.
