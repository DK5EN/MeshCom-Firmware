# Fable Verdict — MeshCom Firmware Code Review

**Date:** 2026-07-12
**Branch reviewed:** `v4.35p_prio`
**Scope:** Whole codebase (not just the working diff), per request — shortcomings, bugs, "super classes"/god-files, DRY violations, and the test suite.
**Method:** 8 independent finder angles (3 correctness, DRY, simplification, efficiency, altitude, test-suite) → dedup → 1-vote adversarial verification. Each verifier read the actual code and returned CONFIRMED / PLAUSIBLE / REFUTED with quoted lines.

## How to use this document (for the Opus Orchestrator)

Every finding below has a stable **ID**, a **severity**, a **verification verdict**, the **evidence** (file:line + mechanism), a **failure scenario**, and a **proposed action** already scoped toward an implementation work-item. IDs are stable — cite them in generated tasks. Findings are grouped by category and ranked by severity within each group.

**Verification legend**
- **CONFIRMED** — verifier named the inputs/state and the wrong output/crash, and quoted the line.
- **PLAUSIBLE** — mechanism is real; trigger or exploitability is bounded/uncertain (stated per-finding).
- Structural findings (DRY / god-files / test-suite) are **evidence-backed observations**, not runtime bugs; they were confirmed by reading both sites / counting build targets.

**Important framing for task breakdown**
- This is an **upstream open-source contribution** (icssw-org MeshCom-Firmware, DEV branch). Project policy in `CLAUDE.md` mandates **minimal, targeted changes** and **no large refactors**. Therefore the orchestrator should split work into two tracks:
  - **Track A — Bug/security fixes** (small, surgical, PR-ready against upstream DEV). SEC-*, BUG-*, CONC-* belong here.
  - **Track B — Structural/DRY/test debt** (larger; propose to upstream as a plan first, or keep as local-quality epics). DRY-*, SIMP-*, ALT-*, TEST-* belong here.
- Each PR must carry a **detailed German description** (project rule). Note that in the task templates.

---

## Priority backlog (most severe first)

| ID | Severity | Verdict | One-line |
|----|----------|---------|----------|
| SEC-01 | **Critical** | CONFIRMED | `{MCP}` remote-command password check bypassable with an empty password + attacker-chosen msg_id → remote GPIO control |
| SEC-02 | High | CONFIRMED | `printfdeb()` re-parses received message text as a printf format string (`%n`/`%s` over the air) |
| SEC-03 | High | CONFIRMED | BLE 0x55 Wi-Fi config: attacker `ssid_len`/`pwd_len` drive `memcpy` up to ~214 bytes past the 300-byte buffer |
| SEC-04 | High | CONFIRMED | URL-decode loop overruns `msg_text_checked[200]` (writes to ~247); length guard runs *after* the loop |
| SEC-05 | Med-High | CONFIRMED | UDP RX off-by-one: 255-byte datagram → `incomingPacket[255]=0` writes one past the buffer |
| SEC-06 | Med-High | CONFIRMED | Same off-by-one on the external-UDP buffer (`incomingExtPacket[255]=0`) |
| BUG-07 | Medium | CONFIRMED | BLE 0xA0 length underflow (`conf_data[0]-2`) → bogus oversized LoRa TX from a 1-byte input |
| BUG-08 | Medium | CONFIRMED | `addBLEOutBuffer`: `len+4` stored in a 1-byte length field wraps for len 252–255 → phone misframes |
| BUG-09 | Medium | CONFIRMED | `addBLEComToOutBuffer`: JSON config >255 B truncates the 1-byte length field (warn-only, no clamp) |
| BUG-10 | Medium | CONFIRMED | `handleACK` copies 12 bytes with no `size>=12` gate → truncated ACK can cancel an unrelated retransmit |
| BUG-11 | Medium | CONFIRMED | `cnewMsg[10]` too small for `{mcp}` reformat → multi-byte MCP commands silently truncated |
| BUG-12 | Low-Med | CONFIRMED | UDP zero-scan reads `buf[i+1]` one past on odd `packetSize` (true OOB at 255) |
| BUG-13 | Low | PLAUSIBLE | APRS trailer/FCS read past payload with no bound check (stale in-allocation read, capped by MAX_APRS_FRAME_SIZE) |
| CONC-14 | High | CONFIRMED | nRF52 runs `readPhoneCommand()` inline in the BLE callback; ESP32 defers via `bleQueue` → unsynchronized settings/ring mutation |
| CONC-15 | Med-High | CONFIRMED | `toPhoneWrite/toPhoneRead` are plain ints written from the OnRxDone radio callback, drained non-atomically from the loop task |
| CONC-16 | Med-High | CONFIRMED | `udpWrite/udpRead` same non-atomic cross-task pattern (missed by the recent atomic-index fix) |
| CONC-17 | Medium | CONFIRMED (finder) | nRF52 `settings_rx_callback` memcpy's the whole settings struct while radio/loop read it → torn read (wrong TX callsign/freq) |
| CONC-18 | Medium | CONFIRMED (finder) | `sendToPhone` TOCTOU: writer can wrap and overwrite the slot between the length read and the payload memcpy |
| CONC-19 | Medium | CONFIRMED | `net_console.cpp stopNetConsole()` reassigns `s_mutex` without holding it (also leaks it) → close/send race + fd misroute |
| DRY-20 | High (maint.) | RESOLVED | Two **live** battery implementations: 15 targets compile `batt_function_old.cpp`, 12 compile `batt_functions.cpp` — fixes must be mirrored |
| DRY-21 | High (maint.) | CONFIRMED | `nrf_eth.cpp` UDP-RX handler is a near-copy of `udp_functions.cpp` and **already diverged** (ACK code 0x01 vs 0x02) |
| DRY-22 | Medium | CONFIRMED | `checkSerialCommand()` duplicated ESP32/nRF52 and already drifted (signedness fix + IAC handling on one side only) |
| DRY-23 | Medium | CONFIRMED | msg_id composition (9+ sites) and ring-enqueue triple (16 sites) copy-pasted across 4 files |
| DRY-24 | Medium | CONFIRMED | 59 on/off command toggles each duplicate the same ~17-line bool+bitmask+save block |
| DRY-25 | Medium | CONFIRMED | I2C sensor bus-reset workaround pasted in ~9 driver sites, already inconsistent (`(BOARD_E22_S3)` missing `defined()`) |
| SIMP-26 | Medium | CONFIRMED | `commandAction()` is one ~4860-line function (215 sequential prefix checks) — table-driven dispatch |
| SIMP-27 | Medium | CONFIRMED | `loop_functions.cpp` (4758 lines) mixes display, ring buffers, APRS beaconing, telemetry, utils in one TU |
| STATE-28 | Medium | CONFIRMED | ~30–60 boolean globals mirror `node_sset*` bitmasks (two sources of truth); already diverges (`bGATEWAY` forced false without clearing the persisted bit) |
| SIMP-29 | Low | CONFIRMED | Dead files in `src/`: `idf_component.yml.orig`, `src/code_review/` audit report |
| SIMP-30 | Low | CONFIRMED | `strSOFTSERAPP_ID` extern declared twice; int/float HDOP twins can disagree |
| ALT-31 | Medium | CONFIRMED | Retransmit "state machine" packed into one overloaded byte (status enum + tick counter + timer) + parallel `retryCount[]` |
| ALT-32 | Medium | CONFIRMED | Display capability expressed as an 11-term negative `#if` board chain, re-typed 6× with divergent board lists |
| ALT-33 | Low | CONFIRMED | Ring-buffer size constants duplicated across a 5-branch `#if` ladder (drift risk per memory class) |
| ALT-34 | Medium | CONFIRMED | "Node not configured" sentinel is a magic-string compare duplicated across 5 inconsistent sites |
| ALT-35 | Low | CONFIRMED | `bOneButton` flag hijacked as a display-invalidation signal → phantom button presses |
| TEST-36 | High | CONFIRMED | `test/` contains **zero runnable tests** — only a copied source file, no Unity harness anywhere |
| TEST-37 | High | CONFIRMED | No native/off-target test environment exists in `platformio.ini` — off-target unit testing is structurally impossible today |
| TEST-38 | High | CONFIRMED | CI (`.github/workflows`) only builds on pushed tags — no `pio test`, no PR/branch build |
| TEST-39 | Medium | CONFIRMED | Highest-value untested logic: APRS encode/decode, `commandAction` parsing, Regexp, coordinate conversion — all zero coverage |

---

## Track A — Bugs & security (surgical, PR-ready)

### SEC-01 — `{MCP}` remote-command password bypass  **[Critical]**
- **File:** `src/loop_functions.cpp:2056–2101` (check at `:2059`)
- **Verdict:** CONFIRMED
- **Mechanism:** `bool bpass = true;` (2056). The validation loop `if(cpasswd[ip] != 0x00 && bpass)` (2059) only runs its body — the only place `bpass` is cleared — for **non-zero** password bytes. `cpasswd` is filled from a zero-initialized `cset` (`memset(cset,0,…)` at 2045, populated only up to payload length), so a short `{MCP}` payload that carries no password leaves `cpasswd[0..4]` all `0x00` → the loop body never executes → `bpass` stays `true`. The only other gate is the "lfd" digit check (2074), but `clfd` is derived from the **attacker-controlled** `aprsmsg.msg_id` (`snprintf(clfd,…,"%03i",(int)(aprsmsg.msg_id & 0x3FF))`, 2053), so the attacker picks `msg_id` to satisfy it. Execution reaches `commandAction(cBefehl,false)` (2101) which runs `--setout Ax on/off`.
- **Failure scenario:** A crafted LoRa frame (`{MCP}` + switch/lfd chars, empty password field, `msg_id` chosen so the interspersed digits match) toggles the node's MCP23017 GPIO outputs remotely with **no valid password**, even on nodes that configured one.
- **Action:** Reject empty/all-zero received password explicitly (`if(!bpass || cpasswd[0]==0x00) return;`), and require the received password to equal the configured `node_passwd` (which must itself be non-empty for MCP control to be enabled). Add a unit test with (a) empty password, (b) wrong password, (c) correct password.
- **Track:** A. Security-sensitive — coordinate disclosure with upstream maintainers.

### SEC-02 — `printfdeb()` format-string injection from received traffic  **[High]**
- **File:** `src/printfdeb_functions.cpp:118` (emit); call sites `src/lora_functions.cpp:820,833,849,859,873,888,922`, `src/via_functions.cpp:52`, `src/lora_functions.cpp:471`
- **Verdict:** CONFIRMED
- **Mechanism:** `printfdeb()` formats varargs into `temp` via `vsnprintf` (96), then emits with `Serial.printf(temp)` (118) — passing already-substituted text as a **new** format string with no varargs. The `%%`-escaping loop only neutralizes `%` in the developer's literal format, not `%` that arrives inside a `%s`-substituted payload. `aprsmsg.msg_payload` is the decoded received message text (`aprs_functions.cpp:380`) and is logged verbatim through `%s`.
- **Failure scenario:** A remote peer sends a message whose text contains `%s%s%n`; it is placed into `temp` by the `%s`, then re-parsed by `Serial.printf(temp)` → reads/writes garbage varargs off the stack (info leak, crash, potential `%n` write) over the air.
- **Action:** Change the emit to `Serial.printf("%s", temp);` (or `Serial.print(temp)`). One-line fix. Add a guard/regression note. Also worth: gate the whole function behind a debug-enable flag (see EFF note) and shrink the 900 B stack frame.
- **Track:** A. One-line, high-value.

### SEC-03 — BLE 0x55 Wi-Fi config out-of-bounds read  **[High]**
- **File:** `src/phone_commands.cpp:554–563`
- **Verdict:** CONFIRMED
- **Mechanism:** `conf_data` is 300 bytes. `ssid_len = conf_data[2]` and `pwd_len = conf_data[ssid_len+3]` are attacker-controlled (0–255); the only guard is `if(ssid_len>0 && pwd_len>0)` — no upper bound. `memcpy(pwd_arr, conf_data+(4+ssid_len), pwd_len)` (563) reads from offset up to 259 for up to 255 bytes → up to ~214 bytes past the 300-byte buffer. VLAs `ssid_arr[ssid_len+1]`/`pwd_arr[pwd_len+1]` are also sized by untrusted input.
- **Failure scenario:** A paired phone (or a malicious app on it) sends msg_type 0x55 with `ssid_len=0xFF` → OOB stack over-read; leaked adjacent memory ends up in the stored SSID/password.
- **Action:** Validate `ssid_len` and `pwd_len` against the actual received frame length before use; reject if `4+ssid_len+1+pwd_len > received_len`. Replace VLAs with fixed max-size buffers. Add tests for oversized/short frames.
- **Track:** A.

### SEC-04 — URL-decode loop overruns `msg_text_checked[200]`  **[High]**
- **File:** `src/loop_functions.cpp:3022` (loop), `:3065` (write), guard at `:3090`
- **Verdict:** CONFIRMED (overflow real; reporter's "in advances faster" phrasing is imprecise — the true cause is the loop being counted by the unused `iu` while `ii` jumps ahead on multibyte `%`-escapes, producing phantom iterations)
- **Mechanism:** `char msg_text_check[200]; char msg_text_checked[200];` The loop `for(int iu=ispos; iu<=len_check; iu++)` runs a fixed count governed by `iu` (never used in the body). Each multibyte escape advances source `ii` by up to 12 but the loop keeps iterating; the extra iterations keep executing `msg_text_checked[in]=…; in++;` with no bound on `in`/`ii`. The `strMsg.length()>160` guard is at 3090, **after** the loop.
- **Failure scenario:** A message whose front is packed with `%F0%9F%98%80` sequences drives `in` to ~248, writing `msg_text_checked[200..247]` out of bounds (stack corruption) and reading `msg_text_check` past index 199.
- **Action:** Bound the destination writes (`if(in >= sizeof(msg_text_checked)-1) break;`) and drive the loop by the source index `ii < len_check` rather than the decoupled `iu`; move the length check before decoding. Add a fuzz test of escape-heavy inputs.
- **Track:** A.

### SEC-05 / SEC-06 — UDP / external-UDP receive off-by-one OOB write  **[Med-High]**
- **Files:** `src/udp_functions.cpp:67,100,104` and `src/extudp_functions.cpp:44,222,228`
- **Verdict:** CONFIRMED (both)
- **Mechanism:** Buffers are `unsigned char incomingPacket[UDP_TX_BUF_SIZE]` / `incomingExtPacket[UDP_TX_BUF_SIZE]` with `UDP_TX_BUF_SIZE=255` (valid 0..254). `read(buf, UDP_TX_BUF_SIZE)` can return 255; then `incomingPacket[len]=0` writes index 255 — one byte past the buffer.
- **Failure scenario:** A 255-byte UDP datagram to the node's port corrupts the adjacent global/BSS byte on every receive.
- **Action:** Pass `UDP_TX_BUF_SIZE-1` to `read()` (reserve the terminator slot), or size the buffer to `UDP_TX_BUF_SIZE+1`. Two-line fix per site. Same fix pattern; can be one PR.
- **Track:** A.

### BUG-07 — BLE 0xA0 text-command length underflow  **[Medium]**
- **File:** `src/phone_commands.cpp:526` (also `:23`, `:241`, `:541`)
- **Verdict:** CONFIRMED
- **Mechanism:** `txt_msg_len_phone = msg_len - 2;` where `msg_len = conf_data[0]` and `txt_msg_len_phone` is `uint8_t`. No check that `conf_data[0] >= 2`; values 0/1 wrap to 254/255, then `memcpy(textbuff_phone+iposn, conf_data+2, txt_msg_len_phone)` copies 254–255 bytes and flags `hasMsgFromPhone=true`.
- **Failure scenario:** A 1-byte BLE text frame produces a bogus oversized message transmitted onto LoRa. (Dest/src buffers both 300 B, so it's a logic bug, not a memory overflow.)
- **Action:** Validate `conf_data[0] >= 2` (and against the received length) before subtracting. Add a test for `conf_data[0]` in {0,1}.
- **Track:** A.

### BUG-08 — `addBLEOutBuffer` length-byte wrap  **[Medium]**
- **File:** `src/loop_functions.cpp:546`; length field `BLEtoPhoneBuff[..][0]` (1 byte, `loop_functions_extern.h:195`)
- **Verdict:** CONFIRMED
- **Mechanism:** Guard clamps only when `len > UDP_TX_BUF_SIZE (255)`. The non-'D' path stores `len+4` in a 1-byte field; for len 252–255, `len+4` = 256–259 wraps to 0–3 (254+4→2).
- **Failure scenario:** A long received text message (type ':' → gets +4 timestamp) with len 252–255 → phone reads a 0–3 byte length while the real ~258-byte payload is misframed/lost.
- **Action:** Clamp at `len > UDP_TX_BUF_SIZE-4`. One-line.
- **Track:** A.

### BUG-09 — `addBLEComToOutBuffer` length truncation (warn-only)  **[Medium]**
- **File:** `src/loop_functions.cpp:579–586`; callers e.g. `command_functions.cpp:4425–4433`
- **Verdict:** CONFIRMED
- **Mechanism:** `if(len>245){ printfdeb("…too long…") }` is a warn with **no clamp and no return**; then `BLEComToPhoneBuff[..][0]=len` (1 byte) truncates while `memcpy(…,len)` copies all bytes. Telemetry/weather config JSON (PARM/UNIT/FORMAT/EQNS/VALES, 50 B each) reaches len≈299, stored as `299&0xFF=43`.
- **Failure scenario:** Phone parses a 43-byte config record; the rest is misinterpreted → corrupted telemetry configuration in the app.
- **Action:** Reject or split payloads > 255 B (return early after the warn), or widen the framing to a 2-byte length. Add a test with a max-size config JSON.
- **Track:** A.

### BUG-10 — `handleACK` missing minimum-length gate  **[Medium]**
- **File:** `src/lora_functions.cpp:202–214,220,250`
- **Verdict:** CONFIRMED (impact conditional on `checkOwnTx` match)
- **Mechanism:** The only gate is `if(payload[0]!=MSG_TYPE_ACK) return false;`; then `memcpy(print_buff, payload, 12)` with no `size>=12` check. For a truncated ACK-type frame, bytes past `size` are stale (from the reused RX buffer). `msg_id` built from those stale bytes (220) can pass `checkOwnTx` and reach `findAndStopRingSlot(msg_id)` (250), which sets `RING_STATUS_DONE`.
- **Failure scenario:** A truncated frame with `payload[0]==0x41` cancels retransmission of an unrelated in-flight message when the stale `msg_id` coincidentally matches one of our own.
- **Action:** Add `if(size < 12) return false;` before the memcpy. One-line.
- **Track:** A.

### BUG-11 — `cnewMsg[10]` too small for `{mcp}` reformat  **[Medium]**
- **File:** `src/loop_functions.cpp:3112–3113`
- **Verdict:** CONFIRMED (truncation, not overflow — `snprintf` is bounded)
- **Mechanism:** `char cnewMsg[10];` then `snprintf(cnewMsg, 10, "{mcp}%c%s%c%s%c%s", …)`. Minimum output is 12 chars (13 bytes) even with empty remainder; realistic MCP commands are ~17–18 chars. Output is truncated to 9 chars — the command (ON/OF) and password are dropped.
- **Failure scenario:** Locally originated multi-byte `{mcp}` commands are silently corrupted; remote switching via this path never works.
- **Action:** Size `cnewMsg` to ≥18 bytes (or compute from the source length). One-line.
- **Track:** A.

### BUG-12 — UDP zero-scan reads one past on odd length  **[Low-Med]**
- **File:** `src/udp_functions.cpp:121–123`
- **Verdict:** CONFIRMED (true OOB only when `packetSize==255`; otherwise reads the just-written NUL/stale byte within the allocation)
- **Mechanism:** `for(i=0;i<packetSize;i+=2){ … inc_udp_buffer[i+1] … }`; odd `packetSize` makes the final `i+1 == packetSize`.
- **Action:** Bound with `i+1 < packetSize`. One-line. Low priority but trivial; fold into the SEC-05/06 UDP PR.
- **Track:** A.

### BUG-13 — APRS trailer/FCS read past payload  **[Low]**
- **File:** `src/aprs_functions.cpp:364–403` (reads at 397/400/403); `rsize` capped at `MAX_APRS_FRAME_SIZE=340` (`:149`); backing buffer `RcvBuffer[510]`
- **Verdict:** PLAUSIBLE — the missing bound check is real (`bPayloadEndOk` only proves a NUL exists in range, not that 4 trailing bytes follow), but for the primary caller it is a **stale in-allocation read** (index ≤343 < 510), not a hard OOB; the garbage FCS normally fails the checksum and the frame is dropped.
- **Failure scenario:** A frame whose payload NUL sits at the last byte causes hw/mod/FCS to be read from stale memory; benign in practice but relies on the FCS check as the only backstop.
- **Action:** After the payload loop, verify `inext+2 <= rsize` before reading the trailer; treat missing trailer as a malformed frame. Add to the APRS decode test suite (TEST-39).
- **Track:** A (low priority) — pairs naturally with TEST-39.

---

## Track A — Concurrency (nRF52 / FreeRTOS)

> Context: recent commits made the LoRa RX ring `iWrite/iRead` and the CAD flags atomic. The findings below are indices/paths that fix **did not** cover.

### CONC-14 — nRF52 runs the phone-command handler inline in the BLE callback  **[High]**
- **File:** `src/nrf52/nrf52_ble.cpp:254` vs `src/esp32/esp32_main.cpp:353–359,2775`
- **Verdict:** CONFIRMED
- **Mechanism:** nRF52 `bleuart_rx_callback` calls `readPhoneCommand(conf_data)` directly in the Bluefruit callback context. ESP32 instead enqueues (`xQueueSend(bleQueue,…)`) and runs the handler in the loop task. So on nRF52 `readPhoneCommand` → `commandAction`/`save_settings`/ring writes run concurrently with the loop and radio tasks with no lock.
- **Failure scenario:** A phone config command arriving while the loop task runs a periodic `commandAction('--wx')` mutates `meshcom_settings` and shared scratch buffers from two contexts; a non-atomic `ComToPhoneWrite++` loses an increment (dropped/overwritten reply) or a preemption mid-`save_settings` persists a half-updated config.
- **Action:** Mirror the ESP32 design on nRF52 — enqueue the payload in the callback and drain it in the loop. This also fixes CONC-17/18 at the root. Scope: introduce a small queue + drain point in `nrf52_main.cpp` loop.
- **Track:** A (architectural but contained). Root fix for the BLE-side races.

### CONC-15 — `toPhoneWrite/toPhoneRead` non-atomic across tasks  **[Med-High]**
- **File:** decls `src/loop_functions.cpp:410–411` (plain `int`); writer `addBLEOutBuffer` (`:525,:559`) called from OnRxDone (`lora_functions.cpp:835`); reader drain in loop task
- **Verdict:** CONFIRMED
- **Failure scenario:** A LoRa text message arrives (radio task runs `addBLEOutBuffer`→`addRingPointer`, which on ring-full reassigns `pRead`) exactly while the loop task is between `toPhoneRead++` and its wrap check → indices cross, the drain walks stale slots and replays old/blank messages to the phone, or two writers memcpy into the same slot → garbage BLE frame.
- **Action:** Make the phone-ring indices atomic (same treatment as the recently-fixed LoRa ring), or serialize writers via CONC-14's queue. Prefer the CONC-14 root fix; atomics are the localized alternative.
- **Track:** A.

### CONC-16 — `udpWrite/udpRead` non-atomic across tasks  **[Med-High]**
- **File:** decls `src/loop_functions.cpp:405–406`; writer `addUdpOutBuffer` via OnRxDone→`addNodeData` (`udp_functions.cpp:1102,1035`); reader `sendMeshComUDP` (`udp_functions.cpp:473–475`)
- **Verdict:** CONFIRMED
- **Failure scenario:** With ETH/UDP gateway active, a received packet (radio task enqueues) races the loop-task drain between `memset(ringBufferUDPout[udpRead],…)` and `udpRead++` → an unsent entry is skipped (mesh frame never reaches the server) or a just-zeroed slot is re-read → length-0/garbage UDP datagram.
- **Action:** Same as CONC-15 — atomic indices or the queue root fix.
- **Track:** A.

### CONC-17 — nRF52 torn settings-struct copy  **[Medium]**
- **File:** `src/nrf52/nrf52_ble.cpp:319` (`settings_rx_callback` memcpy of whole `meshcom_settings` then `save_settings()`)
- **Verdict:** CONFIRMED (finder analysis; not independently re-verified — treat as CONFIRMED-pending-spot-check)
- **Failure scenario:** A LoRa packet arrives while the BLE task is mid-copy; the higher-priority radio task builds a beacon from a torn struct (new callsign spliced with old suffix, or new frequency with old country) → transmits a frame with a wrong source callsign; `save_settings()` can flash a half-copied struct.
- **Action:** Subsumed by CONC-14 (defer settings apply to the loop task, apply under a single critical section). If keeping inline, guard the struct with a mutex shared with the radio/loop readers.
- **Track:** A.

### CONC-18 — `sendToPhone` TOCTOU on the ring slot  **[Medium]**
- **File:** `src/phone_commands.cpp:67` (reads `blelen` then several memcpy's); writer wraps ring at `loop_functions.cpp:531–549`
- **Verdict:** CONFIRMED (finder analysis)
- **Failure scenario:** During a burst of >MAX_RING incoming messages with a slow-to-ACK phone, the writer wraps and overwrites the slot between the length read and the payload memcpy → the phone gets a frame whose length prefix is from the old message and body from the new one (garbage timestamp/truncated text, or BLE parse failure/disconnect).
- **Action:** Snapshot the whole slot under a lock (or copy length+payload atomically), or gate writers while draining. Fixed structurally by CONC-14 + atomic indices.
- **Track:** A.

### CONC-19 — `net_console.cpp` mutex reassigned without ownership (+ leak)  **[Medium]**
- **File:** `src/net_console.cpp:280–294` (`stopNetConsole`), `:108–117` (`teardownClient`), `:208–215,378` (`authTask` on core 1)
- **Verdict:** CONFIRMED
- **Mechanism:** `stopNetConsole()` does `s_mutex = xSemaphoreCreateMutex();` (284) — overwrites the live mutex pointer (old one leaked, never `vSemaphoreDelete`d) without holding it — then calls `teardownClient()`, which is documented "must be called under mutex" and does `xSemaphoreGive(s_mutex)` (a give-without-take on the fresh mutex) plus `::close(s_fd)`. `authTask` on core 1 may still hold/block on the **old** mutex, so it is no longer mutually excluded from `write()`/`teardownClient` (now using the **new** mutex).
- **Failure scenario:** `--extser off` during a client handshake → `::close(s_fd)` races `::send(s_fd)`; reused lwIP fd numbers can misroute console output into another socket (e.g. the UDP mesh socket).
- **Action:** Take `s_mutex` in `stopNetConsole` before teardown; do **not** recreate the mutex (reuse it), or if recreation is intended, stop `authTask` and delete the old mutex under lock first.
- **Track:** A.

---

## Track B — DRY violations

### DRY-20 — Two **live** battery implementations must be kept in sync  **[High maintenance]**
- **Files:** `src/batt_function_old.cpp` (`#ifndef USE_NEW_BATT`) and `src/batt_functions.cpp` (`#if defined(USE_NEW_BATT)`)
- **Verdict:** RESOLVED (two finders disagreed; verifier counted build targets)
- **Finding:** `USE_NEW_BATT` is defined in **12** variants (E22 family, ttgo-lora32-v21, T-Beam-1W, T3-S3, T-Deck, T-Deck-Plus, wireless-paper, vision-master-e213). It is **not** defined in **15** variants — **wiscore_rak4631, Heltec WiFi LoRa 32 V2/V3/V4, heltec_t114, wireless_stick, wireless_tracker, all classic/SX1262/SX1268/Supreme T-Beams, t_deck_pro, t_echo, T-ETH-ELITE, T-Connect-Pro**. So **both files are live**; neither is dead code. (`vision-master-e290` has it commented out.)
- **Cost:** Any battery/Vext/calibration fix applied to only one file silently misses ~half the fleet, including flagship RAK4631 and Heltec V3.
- **Action:** Unify into one implementation behind a thin per-board calibration table; delete the loser. If upstream resists a refactor, at minimum add a header comment cross-referencing both files and a checklist rule "fix both." Track B epic.

### DRY-21 — `nrf_eth.cpp` UDP-RX handler duplicates `udp_functions.cpp` (already diverged)  **[High maintenance]**
- **Files:** `src/nrf52/nrf_eth.cpp:~340–470` vs `src/udp_functions.cpp:~236–470`
- **Verdict:** CONFIRMED
- **Finding:** Near-verbatim copy of the {SET}/{CET} + `:ack`/`:rej` parsing, `msg_counter` composition, 7-byte ACK build, `checkOwnTx`/`insertOwnTx`, dedup + enqueue. **Already diverged:** `udp_functions.cpp:281–285` upgrades the ACK type to `0x02` in `print_buff[5]`; the nRF52 Ethernet copy leaves it at `0x01` → phone apps on nRF52 ETH gateways get a different ACK code than on WiFi/UDP.
- **Action:** Extract a shared `handleIncomingMeshComUDP(buf,len,source)` used by both paths. Track B; also fixes the live ACK-code inconsistency (that specific fix could be Track A).

### DRY-22 — `checkSerialCommand()` duplicated ESP32/nRF52 (already drifted)  **[Medium]**
- **Files:** `src/esp32/esp32_main.cpp:3862` vs `src/nrf52/nrf52_main.cpp:2513`
- **Verdict:** CONFIRMED
- **Finding:** ~110-line bodies identical except an ESP32-only net-console block and a `(int)sizeof` signedness cast. The nRF52 copy got the signedness fix and the ESP32 didn't; only ESP32 got Telnet-IAC handling → parsing-bounds bugs get fixed on one board only.
- **Action:** Move the platform-neutral parser to `command_functions.cpp`, abstract only the input source. Track B.

### DRY-23 — msg_id composition + ring-enqueue triple copy-pasted  **[Medium]**
- **Files:** `src/loop_functions.cpp` (senders at ~3138/3713/3797/3861/3939/4033), `lora_functions.cpp:763`, `udp_functions.cpp:264`, `nrf52/nrf_eth.cpp:374`; enqueue triple at 16 sites in 4 files
- **Verdict:** CONFIRMED
- **Finding:** `((_GW_ID & 0x3FFFFF)<<10)|(node_msgid & 0x3FF)` appears 9+ times; the `ringBuffer[iWrite][0]=len; [1]=status; memcpy(+2,…); addTxRingEntry(…)` block 16 times. The fragile ordering hack at `loop_functions.cpp:3212–3221` (write 0x00, enqueue, then patch to 0xFF) shows the copy-paste is already error-prone.
- **Action:** One `enqueueTx(buf,len,status,reason)` helper + one `nextMsgId()`. Directly de-risks future ring/priority changes. Track B (high leverage).

### DRY-24 — 59 on/off command toggles duplicate the same block  **[Medium]**
- **File:** `src/command_functions.cpp` (e.g. `:905` button, `:1343` track, `:1382` gps)
- **Verdict:** CONFIRMED
- **Finding:** Each toggle repeats ~17 lines: set a bool, set/clear a `node_sset*` bit, optional `if(ble) bNodeSetting=true;`, `bReturn=true;`, `save_settings()`. Adding a setting costs ~40 pasted lines; several branches already differ only by whether they call `init_onebutton()`.
- **Action:** `handleToggle(name, &flag, ssetMask, postAction)` table. Collapses hundreds of lines and makes on/off branches consistent by construction. Track B; couples with STATE-28.

### DRY-25 — I2C sensor bus-reset workaround pasted ~9× (already inconsistent)  **[Medium]**
- **Files:** `aht20.cpp:45`, `bmp390.cpp:54,84`, `bmx280.cpp:131,143,169,213`, `sht21.cpp:41,68`
- **Verdict:** CONFIRMED
- **Finding:** `#if defined(BOARD_TBEAM_V3) || (BOARD_E22_S3) Wire.end(); Wire.begin(I2C_SDA,I2C_SCL); #endif` repeated with variation — `bmx280.cpp:131` uses only `#ifdef BOARD_TBEAM_V3` (E22_S3 missed), and every copy writes `(BOARD_E22_S3)` **without** `defined()`, which evaluates the macro's value (or 0) rather than its definedness.
- **Action:** One `i2cSensorBusReset()` helper (or a single correct guard macro `#if defined(BOARD_TBEAM_V3) || defined(BOARD_E22_S3)`) in a shared header. Fixes the latent preprocessor bug too. Track B (the `defined()` fix alone could be Track A).

---

## Track B — Simplification / god-files ("super classes")

### SIMP-26 — `commandAction()` is a ~4860-line function  **[Medium]**
- **File:** `src/command_functions.cpp:194–5052`
- **Finding:** ~215 sequential `commandCheck(...)==0` prefix blocks, no early-exit chaining, per-block save/reply boilerplate. Prefix-ordering bugs are easy (e.g. `setinfo on` vs `setinfo off` shadowing), and this one function is the constant upstream merge-conflict hotspot.
- **Action:** Table-driven dispatch `{const char* cmd, handler, flags}` scanned in a loop, handlers as small functions. Big win for merge-friendliness and flash. Track B epic; propose to upstream first.

### SIMP-27 — `loop_functions.cpp` mixes ~5 subsystems in one TU  **[Medium]**
- **File:** `src/loop_functions.cpp` (4758 lines)
- **Finding:** OLED/e-paper rendering (`sendDisplay*`), ring-buffer management (`addBLEOutBuffer`…`insertOwnTx`, `addRingPointer`), APRS position/beaconing (`PositionToAPRS`, `sendPosition`, `setSMartBeaconing`), telemetry (`sendTelemetry`, `sendHey`), and generic utils (`utf8ascii`, `cround4`, `count_char`) — plus ~200 file-scope globals.
- **Action:** Extract along the named seams into `display_*.cpp`, `txring.cpp`, `beacon.cpp`, `strutils.cpp`. Track B epic. Enables unit-testing the pure utils (feeds TEST-39).

### STATE-28 — Boolean globals mirror `node_sset*` bitmasks (two sources of truth)  **[Medium]**
- **Files:** `src/loop_functions_extern.h` (~30–60 bools); decode duplicated at `esp32_main.cpp:768ff`, `nrf52_main.cpp:530ff`, and per-command in `command_functions.cpp`
- **Verdict:** CONFIRMED, with a live divergence example: `esp32_main.cpp:879–888` forces `bGATEWAY=false` at boot without clearing the persisted `0x1000` bit, so `--info`/JSON export (reads the bool) disagrees with flash and the gateway silently re-arms next boot once creds exist.
- **Finding:** Every setting is stored twice (bool + bitmask), decoded by hand-written hex masks in ≥3 places, and updated at ~94 sites; ESP32 and nRF52 decoders must be kept bit-for-bit in sync. Any path that flips one without the other diverges from flash.
- **Action:** One settings module with typed accessors over the bitfield (setter updates bit **and** persists); delete the boolean mirrors. Track B epic; couples with DRY-24. This is both a simplification and an altitude fix.

### SIMP-29 — Dead files inside `src/`  **[Low]**
- **Files:** `src/idf_component.yml.orig` (byte-identical to `idf_component.yml`, referenced nowhere), `src/code_review/` (stale audit report in the compiled tree)
- **Action:** Delete. Trivial; reduces upstream PR surface. Track B (quick win).

### SIMP-30 — Redundant/derivable declarations  **[Low]**
- **File:** `src/loop_functions_extern.h:319,322` (`strSOFTSERAPP_ID` declared twice); `:274–275` (`posinfo_hdop` int vs `fposinfo_hdop` float twins)
- **Finding:** The int/float HDOP twins can disagree by code path (nRF52 updates the float; other paths the int) → status output vs display show inconsistent HDOP.
- **Action:** Remove the duplicate extern; keep only `fposinfo_hdop` and derive the int where needed. Track B (quick win).

---

## Track B — Altitude (fragile bandaids vs proper depth)

### ALT-31 — Retransmit "state machine" packed into one overloaded byte  **[Medium]**
- **Files:** `src/lora_functions.cpp:1876`; parallel `retryCount[]` at `loop_functions.cpp:389`
- **Finding:** `ringBuffer[i][1]` triples as status enum (`RING_STATUS_READY/DONE`), a 2 s tick counter, and a timer compared to magic `0x15`; `retryCount[]` is a bolted-on parallel array that must be manually zeroed at 5 enqueue sites; retransmit copies the payload into a fresh slot (burns ring capacity).
- **Failure scenario:** Changing the retry interval means picking a hex threshold that must not collide with the status enum sharing the byte; loop-timing changes silently rescale retry timing; a missed `retryCount` zeroing gives a slot a stale count → gives up early.
- **Action:** Per-slot struct `{state, retries, next_retry_at_millis}` with explicit transitions. Track B; note this intersects the recent ring-index work.

### ALT-32 — Display capability as negative board `#if` chains  **[Medium]**
- **File:** `src/loop_functions.cpp:343` and re-typed at `873,903,1019,1266,1469`
- **Finding:** An 11-term `!defined(BOARD_…) && …` chain, repeated 6× with **divergent** board lists. Missing a board in one chain compiles cleanly but routes it to the wrong display path (e.g. U8g2 code on an e-paper board).
- **Action:** Positive capability macros (`HAS_U8G2_OLED`, `HAS_EPD_WP`, `HAS_TFT`) defined once per variant header; display code tests capabilities. Track B.

### ALT-33 — Ring-size constants duplicated across a 5-branch `#if` ladder  **[Low]**
- **File:** `src/configuration_global.h:82`
- **Finding:** `MAX_MHEARD/MHPATH/RING/DEDUP_RING/LOG/RING_UDP` copied wholesale across ENABLE_XML / ENABLE_SBUFFER / ESP32-S3+RAK / TBEAM / fallback (first two branches already byte-identical). A missed branch silently inherits an ESP32-tuned size on a 256 KB nRF52.
- **Action:** Define baselines once, `#ifdef`-override only the constants that differ per memory class. Track B (quick win).

### ALT-34 — "Node not configured" magic-string sentinel duplicated  **[Medium]**
- **Files:** `esp32_main.cpp:856,992,1564`, `nrf52_main.cpp:590`, `safeboot/main.cpp:58`
- **Finding:** Compares against `"XX0XXX"`, `"XX0XXX-00"`, `"none"`, `0x00` — inconsistently (safeboot tests two forms; `esp32_main.cpp:992` tests only the 6-char prefix; the default is re-materialized as a literal at 1564). If the factory default changes, some paths treat the node as configured while safeboot wipes it / WifiAP force-enables, differing per image.
- **Action:** One `isNodeConfigured()` + `DEFAULT_CALL` constant in shared config used by all three images. Track B.

### ALT-35 — `bOneButton` hijacked for display invalidation  **[Low]**
- **File:** `src/loop_functions.cpp:1939` (WP track page sets `bOneButton=true` to bypass the 15 s render throttle)
- **Finding:** `bOneButton` now means both "user pressed the button" and "display cache invalid"; any future consumer of the flag (menu nav, long-press) gets phantom presses whenever the position changes.
- **Action:** Explicit display-dirty/invalidate flag consumed by the render scheduler, decoupled from input. Track B (small).

---

## Track B — Test suite (the "spectacle" — confirmed)

**Bottom line:** the suspicion is correct. There is **no runnable test suite**, no native test environment, and CI runs **no tests**. The `test/` directory is a decoy.

### TEST-36 — `test/` has zero runnable tests  **[High]**
- **Evidence:** `test/` contains only the stock PlatformIO `README` and a **copied source file** (`compress_functions.cpp/.h`). No Unity harness, no `test_main`, no `TEST_ASSERT`/`RUN_TEST` anywhere in the repo. The copied feature isn't even used in `src` (only a commented `//TEST #include` at `command_functions.cpp:43`). `pio test` finds no cases for any environment.
- **Action:** Remove the decoy or replace with real tests. See TEST-37/39 for the real work.

### TEST-37 — No native/off-target test environment  **[High]**
- **Evidence:** `platformio.ini` and all `variants/*/platformio.ini` define only hardware envs (esp32/nrf52). No `test_framework`, no `[env:native]`, no `platform=native`. Off-target unit testing is structurally impossible today.
- **Action:** Add a `[env:native]` with Unity, compiling the pure-logic modules with hardware deps stubbed. This is the enabling task for all coverage work.

### TEST-38 — CI runs no tests and doesn't build PRs  **[High]**
- **Evidence:** The only workflow triggers on pushed tags and runs `pio run` + release packaging — no `pio test`, no PR/branch build. A PR that breaks compilation for one of ~25 targets merges green; behavioral regressions ship as release firmware.
- **Action:** Add a CI job that builds a representative target matrix on PRs and runs `pio test -e native`. Gate merges on it.

### TEST-39 — Highest-value untested logic (zero coverage)  **[Medium]**
- **Evidence (all grep-confirmed zero coverage):**
  - `src/aprs_functions.cpp` — `decodeAPRS`/`encodeAPRS`/FCS (`:122,:403,:1048–1085`): pure wire-format, ideal for encode→decode round-trip + truncated/fuzz cases. Directly covers BUG-13 and would have caught SEC-01-adjacent framing.
  - `src/command_functions.cpp` — `commandAction` (`:194`): 5280-line parser for all `--` commands (frequency, callsign, gateway creds); no test exercises a single command string.
  - `src/Regexp.cpp` — the **one genuinely testable seam today** (only `setjmp`/`ctype`/`string`, no `Arduino.h`, no globals); validates callsigns/positions. Runs under native Unity unmodified.
  - `decodeAPRSPOS`/`conv_coord_to_dec` (`aprs_functions.cpp:531,1301`) — near-pure coordinate conversion.
- **Feasibility caveat:** `src/loop_functions_extern.h` declares **261 extern globals** and most modules pull in Arduino `String`/`Serial` (and `mheard_functions.cpp` pulls SD/SPI/LVGL), so the harder modules stay untestable until seams are introduced (SIMP-27 extraction helps). Start with `Regexp.cpp`, APRS codec, and coordinate conversion — coverable with zero refactor once TEST-37 exists.
- **Action:** Under `[env:native]`, add: APRS encode/decode round-trip + malformed-frame tests; Regexp pattern tests (callsign edge cases); coordinate-conversion tests. Then expand as SIMP-27 extraction creates seams.

---

## Suggested orchestration plan

1. **Sprint 0 — enablement:** TEST-37 (native env) + TEST-38 (CI) so every subsequent fix is regression-guarded. SIMP-29/30 quick deletions.
2. **Sprint 1 — security/correctness PRs (Track A):** SEC-01 (critical, coordinate with upstream) → SEC-02 → SEC-05/06/BUG-12 (one UDP PR) → SEC-03/BUG-07 (BLE parsing PR) → SEC-04 → BUG-08/09/10/11. Each with a regression test and a German PR description.
3. **Sprint 2 — concurrency (Track A):** CONC-14 as the root fix (nRF52 defer-to-loop), which resolves CONC-15/16/17/18; then CONC-19 (net_console) separately.
4. **Sprint 3+ — structural debt (Track B):** DRY-20/21/22/23/24/25, then SIMP-26/27/STATE-28 and the ALT-* items, each proposed to upstream as a plan before large refactors (per `CLAUDE.md`).
5. **Continuous:** grow TEST-39 coverage alongside each fix.

**Verification provenance:** SEC-01–06, BUG-07–13, CONC-14/15/16/19, DRY-20, TEST-36–39 were independently verified against the source (CONFIRMED except BUG-13 PLAUSIBLE). CONC-17/18 rest on the concurrency finder's read (high confidence, spot-check before the CONC-14 PR). DRY/SIMP/ALT items are evidence-backed structural observations with quoted sites.
