# RM web GUI concept - Fable verdict

Review of the concept (not code). 6 finders (about 70 candidates), 5 Opus verifiers. Detail per
cluster: `verdict-ux.md` (contains the page wire-sketch), `verdict-security.md`,
`verdict-protocol.md`, `verdict-storage.md`, `verdict-webtests.md`.

## Verdict on the user requirement

As conceived the concept does NOT meet "no commands, button oriented": it keeps the raw command
dropdown, `txpower <n>` and `setout <pin> <on|off>` still need typing, toggles are separate
on/off entries with no state, and a human still sees "Sync counter". The tile design below meets it.

## Confirmed findings

1. **High - raw commands and free text remain** (ux-1,3,8). Fix: tiles (Info, Switches, TX power stepper, Restart), `setout` only under Advanced, one validated call field plus heard-node chips.
2. **High - toggles show no state** (ux-2). The `status` reply reports only gw, mesh, led (`led=` only on boards with an LED, so it doubles as capability flag). gps/track/display/txpower are never reported; reply limit is 63 chars, worst case today 56. Fix: one compact token, e.g. `s=GtDwMl p=17/22`; show "?" until a status arrived.
3. **High - silent rejects lock the target** (protocol-2, security-9, ux missed-2). A wrong key, stale counter, out-of-range value (txpower above the target maximum, `led` to old firmware) is rejected with no reply and counts toward 3 rejects/90 s -> 5 min lockout. Sender sees "waiting" forever. Fix: at most 2 unanswered sends per target per 90 s, timeout state, cap pickers by what the target reported (txpower 15 when unknown).
4. **High - slot-number send is an escalation** (security-1, protocol-1). Anything that can reach or trick the node's web page can send allowlisted commands to 3 remote nodes, even with RM off on the node. Web auth is IP based, open when `node_webpwd` is empty, every response has `ACAO: *`, Origin/Host/Content-Type are never checked, OPTIONS gets 200. Fix: custom header (`X-RM`) plus Host allowlist on state-changing endpoints, or keep passwords out of firmware (see decisions).
5. **High - pre-existing, found by the review: `GET /setparam/?manualcommand=<--cmd>` works cross-site** and gives full control of the node including `--passwd` (web_setup.cpp:30-39). Separate fix, independent of this concept.
6. **Medium - feedback states undefined** (ux-5, protocol-4). There is no delivery ACK. Define queued, waiting, no answer (60-90 s), unverified, done ok, done err; translate error tokens into sentences.
7. **Medium - Sync button must go** (ux-7). Sync automatically only when the sender has no trusted clock and no learnt target mark; show a 10 s cooldown after each send; resend path not worth building (reply cache is RAM, one command deep).
8. **Medium - password validator** (protocol M-1). A leading space shows "password set: yes" but the receive hook treats it as none. One validator for field, `--passwd` and sender: 1-14 bytes, printable ASCII, no leading/trailing space, not `none`; reject, never truncate.
9. **Medium - proportionate confirmation** (ux-4 downgraded). No command cuts a node off (RM is LoRa only, reboot returns after 8 s). One plain confirm on Restart, Mesh off, Gateway off, lowering TX power.
10. **Medium - RAK output limits** (webtests M1). One write is capped at 2048 B (rest dropped, call reports success); nRF52 `printf` uses a 256 B stack buffer but sends the full length (stack bytes leak). Emit tiles as short `println` chunks.
11. **Medium - password leaks that exist today** (webtests M2, M3, security missed-3): Manual Command puts `--passwd` in the GET line and serial echo; an unauthenticated `GET /rmsend` leaves the password in `web_header`, never wiped. The concept's "never sent back" claim is false while `config.json` exports `node_passwd` in clear (explicit operator decision, ADR RM-D1) with `ACAO: *`.
12. **Medium - changing `node_passwd` re-keys console 2323 and KISS** on ESP32 (`none` opens both). The password field needs a warning.
13. **Medium - page lifecycle** hard-wired to `setup` / `#rmcard` (loadPage ~1198, poll ~1311-1312); after the 4 h session expiry a 401 is swallowed silently.
14. **Medium - no tests exist for the RM card JS.** Need: scaffold script extracted into jsdom (feasible, string literals only), handler parsing extracted to a pure file for a native test, and a password-leak test WITH a positive control (canary through today's Manual Command path must be found in the log, the new endpoint must not).
15. **Medium - store layout** (storage): header-only codec `rm_nodes_store.h`, magic/ver/seq/3x{call[10],key[32],used}/CRC32, about 145 B; ESP32 one blob in namespace `RmNodes`; nRF52 `/rm_nodes.a|b` with no rename (rename defect measured on DK5EN-90), write to the invalid or lower-seq slot, read back and compare, load highest valid seq; loop task only; fail closed ("no saved nodes" is a safe state because the Adafruit core erases a whole 4 kB page per flush); wipe from `clear_flash()`/`flash_reset()` plus a "Forget all" button; release notes must say a layout bump also wipes saved nodes. New `rmSendCommandKey(dst, key32, ...)`; the password variant wraps it.
16. **Low** - hotspot `web_functions.cpp` (22 fork commits since September); a new `web_rm_page.cpp` has no precedent and needs shared rm helpers moved out and one `rmScaffoldJs()` hook.

## Refuted or downgraded (do not re-investigate)

- storage-8 (ENOSPC on nRF52): InternalFS has 224 blocks of 128 B, DK5EN-90 uses 29.
- protocol-5 (reply tag breaks when own call changes): the reply goes to the old call anyway.
- tests-altitude-4 ("derived key protects nothing"): it protects the target's console and KISS passwords (they use the raw password); it does not stop RM replay, the key is a full RM credential.
- webmech-10 (POST body echoed to serial): never echoed. tests-10 (double click burns a counter): the busy check runs first.
- Browser-side signing: not viable, `crypto.subtle` is absent on plain HTTP to a LAN IP and the counter/reply key live in the node.
- RM-capable detection of heard nodes: impossible (RM setting is never on air, MHEARD firmware field is one letter).

## Design decisions for the user

- **Where saved passwords live:** firmware slots (stage 2, store above) versus opt-in "remember on this browser" localStorage (no firmware store, no slot-send escalation, but per device and per address; the RAK is reached by DHCP IP). The verifiers recommend browser-only stage 1, firmware store later.
- **Node picking:** heard-node chips from MHEARD plus a "Manage" button on each MHEARD card, plus one validated call field for nodes behind relays.
- **/setparam CSRF hardening** as its own fix.
