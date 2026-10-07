# RM web GUI - implementation plan and campaign state

Source: fable-review of the concept, 2026-10-05. Verdicts in this directory (`../archive/rm-gui-verdict.md`
first, then `verdict-*.md`; the page wire-sketch is in `verdict-ux.md`). Decisions by the user
(2026-10-05): saved passwords live in **firmware slots**; the **/setparam CSRF fix** is in scope;
bench nodes DK5EN-1 (Heltec V3, 192.168.68.71) and DK5EN-92 (T-Beam, 192.168.68.75) may be flashed.

## Wave status log

| Wave | Content                                                               | Status                              |
| ---- | --------------------------------------------------------------------- | ----------------------------------- |
| W0   | native envs in platformio.ini, pio lock helper, this plan             | done                                |
| W1a  | key store codec + ESP32/nRF52 persistence + native test               | done                                |
| W1b  | sender policy, validators, status token, `rmSendCommandKey`           | done                                |
| W1c  | web request guard (X-MC / Origin proof, Host allowlist) + native test | done                                |
| G1   | wiring (guard, CORS, /rmsend viaSync, /rmstatus, --passwd), gate      | done (advisor 2 passes: APPROVED)   |
| W2a  | handlers (/rmpasswd, /rmnodes, /rmheard, send by slot), parse header  | done                                |
| W2b  | Remote page HTML, scaffold JS, jsdom test                             | done                                |
| G2   | wiring (route, nav, loadPage, old card removed), gate, advisor        | done                                |
| W3   | bench: flash DK5EN-1 and DK5EN-92, browser click-through, docs        | done (2026-10-06, see Bench result) |

Gate = `tools/regression.sh --stage 1,2`, clean sequential builds (heltec_wifi_lora_32_V3,
ttgo_tbeam, wiscore_rak4631, plus one S3 board), string scan of the images, then an advisor pass
(`/fable-review` advisor mode) before each commit. Commits are per wave, explicit paths, fork-dev only.

## Design (decided)

- New sidebar page `Remote` (`/?page=remote`), card removed from Setup. Tiles, not commands: Info
  (status, send position, send track), Switches (GPS, Track, Display, Light, Mesh, Gateway with state
  or "?"), TX power stepper, Restart. `setout`, re-sync and the raw table sit in a collapsed Advanced.
- This node: RM switch, write-only password field (Set / Clear, POST body only), warning that the
  password also keys net console 2323 and KISS.
- Known nodes: 3 firmware slots (call + SHA-256 key only, never the password), write-only, Forget all.
  Send-by-slot only; the browser never sees a key.
- Heard-node chips from MHEARD, "Manage" button on MHEARD cards, one validated call field.
- Feedback states: queued, waiting, no answer (60-90 s), unverified, done ok, done err, in plain
  sentences. 10 s cooldown after each send. No Sync button (automatic, only when needed).
- Confirm (one plain dialog-free confirm tile state) on Restart, Mesh off, Gateway off, lower TX power.
- Per-target limit: at most 2 unanswered sends per 90 s (the target locks after 3 rejects).
- Pickers capped by what the target reported; txpower 15 when unknown.

## Contracts

### C1 key store (W1a owns `src/rm_nodes_store.h` and the persistence)

```cpp
struct RmNodeSlot { char call[10]; uint8_t key[32]; bool used; };   // call: up to 9 chars + NUL
struct RmNodes    { RmNodeSlot slot[3]; };
// pure codec (header-only, native tested): magic, ver, nslots, u32 seq, 3 x {call[10], key[32], used}, CRC32 (crc32_buf)
size_t rmNodesEncode(const RmNodes &n, uint32_t seq, uint8_t *out, size_t cap);   // 0 on error
bool   rmNodesDecode(const uint8_t *in, size_t len, RmNodes &n, uint32_t &seq);   // false on bad magic/ver/CRC/len
// persistence, loop task only, no static cache
bool rmNodesLoad(RmNodes &n);        // false = nothing valid, n zeroed (fail closed)
bool rmNodesSave(const RmNodes &n);  // read-back compare; false on any error
void rmNodesWipe();                  // called from clear_flash() / flash_reset()
```

ESP32: Preferences namespace `RmNodes`, key `nodes`, one blob. nRF52: files `/rm_nodes.a` and
`/rm_nodes.b`, no rename; save to the invalid or lower-seq slot, read back and compare; load takes the
highest valid seq. A "no saved nodes" state must be safe and visible.

### C2 sender (W1b owns `src/rm_sender_policy.h`, `src/rm_validate.h`, `src/rm_runtime.cpp`)

- `rmValidatePassword(const char*)`: 1-14 bytes, printable ASCII, no leading or trailing space, not
  `none`; reject, never truncate. `rmValidateCall(const char*)`: call with SSID, up to 9 chars,
  upper-case normalised by the caller.
- `rmSendCommandKey(dst, key32, cmd, args, ...)`: password variant becomes a wrapper (derive, call).
  Keeps today's per-send key copy.
- Sender policy (pure): `rmPolicyMaySend(target, nowMs)` false while 2 sends to that target are
  unanswered inside 90 s, or inside the 10 s cooldown; `rmPolicyState(entry, nowMs)` returns one of
  queued, waiting, noanswer, ok, err, unverified; automatic sync only when the sender has no trusted
  clock and no learnt target mark.
- `status` reply gains one compact token (63 char total reply limit, today's worst case 56):
  `s=<letters>` carrying gps, track, display, mesh, gateway, led as upper-case = on, lower-case = off,
  letters in a fixed order documented in `docs/architecture/11-wire-format.md` section 1.9.3, plus
  `p=<cur>/<max>` for txpower. Boards without an LED omit the led letter (`led=` stays the capability
  flag). Worst-case length is asserted in the native test.
- `/rmstatus` JSON (read by W2) gains per sent entry `st` (state above) and `msg` (plain sentence),
  and per target `locked` / `retry_s`. The JSON writer stays in `web_functions.cpp`; W1b exposes plain
  getters in `rm_runtime.h`.

### C3 web guard (W1c owns `src/web_functions/web_guard.h`)

Pure function over the raw request header text, native tested (std::string, no Arduino):

```cpp
enum WebGuardVerdict { WG_OK, WG_BAD_HOST, WG_CROSS_SITE };
WebGuardVerdict webGuardCheck(const char *header, size_t len, const char *path /* e.g. "/setparam/" */);
```

- Host must be an IPv4/IPv6 literal, `localhost`, or end in `.local`; anything else is WG_BAD_HOST
  (DNS rebinding).
- If `Sec-Fetch-Site` is present and is `cross-site` or `same-site`, the request is rejected
  (WG_CROSS_SITE) for every path except the plain page load `GET /`. Absent header (curl, python
  tools, old browsers) is accepted, so `tools/webflash.py` and the bench scripts keep working.
- OPTIONS answers must stop advertising permissive `Access-Control-Allow-Headers` for state-changing
  paths and `Access-Control-Allow-Origin: *` must go from `/config` and `/rmstatus` (wiring by the
  orchestrator in G1).

### C4 web page (W2a handlers, W2b page and JS)

Same-origin only; every state-changing request is a POST body (never a URL), never echoed, buffers
wiped with `rm_wipe`. Responses are JSON written with `rm_json_str`, each `print` at most 512 bytes
(RAK: one write is capped at 2048 B, nRF52 `printf` leaks the stack above 256 B).

W2a owns `src/web_functions/web_rm_handlers.{h,cpp}` and the pure `src/web_functions/web_rm_parse.h`
(form parsing and validation, native tested in env `native_rm_web_parse`). Handlers, called by the
orchestrator from the route chain in `web_functions.cpp`:

| Request                               | Body / answer                                                                                                                                                                                                                              |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `POST /rmpasswd`                      | `act=set&pw=<pw>` or `act=clear`; validator `rmValidatePasswordN`; calls `nodePasswdApply()` (G2). `{"ok":true}` / `{"ok":false,"err":"<token>"}`                                                                                          |
| `GET /rmnodes`                        | `{"nodes":[{"slot":0,"used":1,"call":"DK5EN-1"},...3 entries]}`, never a key                                                                                                                                                               |
| `POST /rmnodes`                       | `act=save&slot=0..2&call=<call>&pw=<pw>` (call upper-cased, `rmValidateCall`; key derived with the shared derive, password wiped), `act=del&slot=n`, `act=forget`. `{"ok":true}` or err token `call`, `pw`, `slot`, `dup`, `store`, `form` |
| `POST /rmsend` (extended, via helper) | `slot=<0..2>&cmd=<cmd>&args=<args>` instead of dst/pw: `rmSendBySlot(...)` in the handlers file loads the store, sends with `rmSendCommandKey`, scrubs the key; same JSON as today (`ctr`, `viaSync`, err)                                 |
| `GET /rmheard`                        | `{"heard":[{"call":"","hw":"HELTEC_V3","age_s":120,"rssi":-95},...]}`, at most 12, from `nbrMhRows()`/`nbrMhGet()`, `nbrHardwareName()`                                                                                                    |
| `GET /rmstatus` (existing, extended)  | adds per sent entry `st`, `msg` and `targets:[{dst,locked,retry,pending,chainErr,chainMsg}]` (done in G1)                                                                                                                                  |

W2b owns `src/web_functions/web_rm_page.{h,cpp}` (`sub_page_remote()` HTML skeleton, `rmScaffoldJs()`
that prints the scaffold JS in chunks, ids and class names of the tiles) and `tools/webgui_rm_test.js`
(jsdom, scaffold JS extracted from the C++ string literals). Design as in `verdict-ux.md` wire-sketch:
This node (RM switch, password set/clear with the console/KISS warning), Choose a node (heard chips
from `/rmheard`, three saved-node chips from `/rmnodes`, one validated call field, "save this node" with
password), Test connection, Info tiles, Switches with state or `?` from the verified `s=` token (old
`gw=`/`mesh=`/`led=` form still parsed), Light only when the node reported `led`, TX power stepper
capped by `p=cur/max` (15 when unknown), Restart; inline one-step confirm on Restart, Mesh off,
Gateway off, lowering TX power (no `confirm()`); tiles locked 10 s after a send with a visible
countdown; Activity list in plain sentences from `st`/`msg`; collapsed Advanced (outputs, re-sync,
raw table). Polling and timers only while the Remote page is shown; a 401 shows "log in again";
`autocomplete="new-password"` on every password input; `rmKeep` never stores a password.
The old Setup-page card, its scaffold JS and CSS are removed by the orchestrator in G2, together with
the route, the nav button and the `loadPage()` hook; W2b must not edit `web_functions.cpp`.

## File ownership

| Wave | Exclusive files                                                                                                               |
| ---- | ----------------------------------------------------------------------------------------------------------------------------- |
| W1a  | `src/rm_nodes_store.h`, `src/esp32/esp32_flash.cpp`, `src/nrf52/nrf52_flash.cpp`, `test/test_rm_nodes_store/*`                |
| W1b  | `src/rm_sender_policy.h`, `src/rm_validate.h`, `src/rm_runtime.cpp`, `src/rm_runtime.h`, `test/test_rm_sender_policy/*`       |
| W1c  | `src/web_functions/web_guard.h`, `test/test_web_guard/*`                                                                      |
| G1   | orchestrator: `src/web_functions/web_functions.cpp`, `src/web_functions/web_setup.cpp`, `src/command_functions.cpp`, `docs/*` |
| W2a  | `src/web_functions/web_rm_handlers.{h,cpp}`, `src/web_functions/web_rm_parse.h`, `test/test_rm_web_parse/*`                   |
| W2b  | `src/web_functions/web_rm_page.{h,cpp}`, `tools/webgui_rm_test.js`                                                            |
| G2   | orchestrator: `src/web_functions/web_functions.cpp`                                                                           |

`platformio.ini` (envs `native_rm_nodes_store`, `native_rm_sender_policy`, `native_web_guard` added in
W0) is orchestrator-owned. Every `pio` call goes through
`/private/tmp/claude-501/-Users-martinwerner-WebDev-MeshCom-Firmware-DEV-Main/d8e813ea-a8d3-4475-bd29-d3d9f628187b/scratchpad/pio_locked.sh`
(one pio process at a time).

## Bench result (2026-10-06, DK5EN-1 Heltec V3 `192.168.68.71`, DK5EN-92 T-Beam `192.168.68.75`, 2 dBm, DM to own node only)

- Both nodes OTA-flashed with e6f6c35b (identity guard first).
- Guard on hardware: img-style GET `/setparam` without `X-MC` and an attacker Referer 403, with `X-MC` 200,
  rebinding Host 403, cross-origin POST 403, OPTIONS preflight 403, no CORS header on `/rmstatus`.
- `/rmpasswd`: leading space, 15+ characters and `none` refused (`pw`), a valid password accepted.
- `/rmnodes`: save, duplicate (`dup`), bad slot (`slot`); the list never contains a key; the saved node
  survived a reboot of DK5EN-92.
- Send by slot DK5EN-92 -> DK5EN-1: `status` (`s=gtdMwl p=2/22 led=0`), `led on`, `led off` confirmed; a lost
  command ended as "no answer after 75 s"; the sender policy locked the target after two unanswered sends.
  Replies can take longer than 14 s over LoRa.
- Chrome (Remote page on DK5EN-92 via `dk5en-92.local`): sidebar intact (Setup icon + Remote mast), heard chips,
  saved-node chip, Test connection, tiles with state from the last answer, countdown after each send,
  Light on / off confirmed by the node (white LED lit on DK5EN-1), activity list in plain sentences.
- Not done: Restart / Mesh off / TX power tiles and the page's password field were not clicked (the API
  behind them was), RAK4631 hardware (compile only, flash 77.3 %), T-Beam `Light` hidden path, `http://<IP>/`
  was refused by the Chrome tool itself, the `.local` name worked.
- Observation: DK5EN-1 once reported `battery 0 %` in a status 4 min after boot (earlier `bat=100`).

## Deferred (known, not done)

- "Manage" button on each MHEARD card (the Remote page already shows heard-node chips).
- A save into a slot that holds a different call overwrites it silently when the page's slot list is
  stale (second browser tab); the handler could answer `used`.
- "Forget all" always answers ok (`rmNodesWipe()` returns void).
- `/?page=remote` is routed by `indexOf` over the whole header (like every other page); matching the
  request line would be cleaner.
- Measure the RAK4631 loop-task stack high-water mark during the RAK bench (slot send and save paths).
- `docs/architecture/11-wire-format.html` still shows the old status form (no generator found).
- `tools/webgui_rm_test.js` needs `jsdom@24` (`NODE_PATH`), it is not in `tools/regression.sh`.

## Open at the end

Upstream PR (none planned), RAK4631 hardware bench of the web page (DK5EN-90, DHCP IP), `/setparam`
rejection of non-browser clients is intentionally not added.
