# F3 — Buffer & Type Safety

Repo: `.`, branch `v4.35p_prio`, HEAD `1ba101f4`.
Baseline docs read: `docs/codequality-rules.md`, `fable-verdict.md` (2026-07-12), `docs/code-audit-fixes-20260627.md`,
`docs/code-audit-20260626.md`.

**Verification of prior claims:** none of the buffer-related findings in `fable-verdict.md`
(SEC-02, SEC-03, SEC-04, SEC-05, SEC-06, BUG-07, BUG-08, BUG-09, BUG-10, BUG-11, BUG-12, BUG-13)
have been fixed in the tree. All were re-checked line by line and are still live. They are listed
below only where this angle adds new impact (e.g. BUG-08 turns out to be the trigger for a
`memcpy(dst, src, SIZE_MAX)`), otherwise flagged in the _Still-open prior findings_ table.

---

## Board classes (needed to read the inventory)

`src/configuration_global.h:79-116` has a 5-branch `#if` ladder. `ENABLE_XML`, `ENABLE_SBUFFER`
and `ENABLE_TBEAM` are **defined in no `platformio.ini` anywhere** (`grep -rn … --include='*.ini'`
returns nothing), so only two classes are actually built:

| Class                                                  | Boards                                                                                                                                                   | MAX_MHEARD | MAX_MHPATH | MAX_RING | MAX_DEDUP_RING | MAX_LOG | MAX_RING_UDP |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | ---------- | -------- | -------------- | ------- | ------------ |
| **A** — `CONFIG_IDF_TARGET_ESP32S3 \|\| BOARD_RAK4630` | Heltec V3/V4, T-Beam S3/Supreme, T-Deck(+/Pro), E22-S3, T3-S3, wireless-paper, E213/E290, RAK4631                                                        | 80         | 100        | 20       | 100            | 10      | 20           |
| **B** — fallback                                       | classic ESP32 (Heltec V2, TTGO, T-Beam 1262/1268/AXP2101, wireless-stick/tracker) **and every non-RAK nRF52** (`heltec_t114`, `t_echo`, `T-Connect-Pro`) | 30         | 40         | 30       | 70             | 20      | 25           |

Note the inversion the `#if` comment claims to avoid: `heltec_t114` / `t_echo` are 256 KB nRF52840
parts that land in the "ESP32 original — reduced sizes due to RAM constraints" branch and therefore
get **larger** rings (MAX_RING 30 vs 20, MAX_RING_UDP 25 vs 20) than the RAK4631. `MAX_RING` being
board-dependent is safe today only because every consumer array uses the same macro — but there is
no `static_assert` anywhere enforcing that.

`MAX_CALL_LEN 20` and `LONGNAME_MAXLEN 20` (`configuration_global.h:54,57`) are **referenced
nowhere in `src/`** — the real callsign buffer is `char node_call[10]` (`src/esp32/esp32_flash.h:17`).
Two "max callsign length" constants exist and neither is the truth. (Rule 17)

---

## Buffer inventory

Excludes `src/Fonts/`, `src/GFX_Root/`, `*/maps/`, `Font_*`, `img_*`. "zeroed?" = at definition or
before each use. "bounds-checked on write?" = every write path provably ≤ size.

### RF / packet path (untrusted)

| Buffer                                        | file:line                                | size expr                            | resolved                            | zeroed?                                                                                   | bounds-checked on write?                                                                 |
| --------------------------------------------- | ---------------------------------------- | ------------------------------------ | ----------------------------------- | ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `RcvBuffer`                                   | `loop_functions.cpp:376`                 | `UDP_TX_BUF_SIZE*2`                  | 510                                 | `={0}`; `memset(…,0,255)` only (`lora_functions.cpp:1213,1162`) — top 255 B never cleared | yes (`size`≤255)                                                                         |
| `rxPayloadCopy[2][…]`                         | `lora_functions.cpp:309`                 | `[2][UDP_TX_BUF_SIZE]`               | 2×255                               | no                                                                                        | **yes** — `rxSize = min(size,255)` at `:313` (nRF52 only; ESP32 has no equivalent clamp) |
| `print_buff` (ACK)                            | `lora_functions.cpp:207,380`             | `[30]`                               | 30                                  | no                                                                                        | fixed 12-byte memcpy, **no `size>=12` gate** (BUG-10, open)                              |
| `lora_tx_buffer`                              | `lora_functions.cpp:97`                  | `UDP_TX_BUF_SIZE+10`                 | 265                                 | no                                                                                        | yes (`sendlng`≤255)                                                                      |
| `ringBuffer[..][..]`                          | `loop_functions.cpp:383`                 | `[MAX_RING][UDP_TX_BUF_SIZE+5]`      | 20/30 × 260                         | `={0}`                                                                                    | yes (writes at `+2`, len≤255 → 257)                                                      |
| `own_msg_id`                                  | `loop_functions.cpp:379`                 | `[MAX_RING][5]`                      | 20/30 × 5                           | `={0}`                                                                                    | yes                                                                                      |
| `retryCount`/`ringPriority`/`ringEnqueueTime` | `loop_functions.cpp:389,474,…`           | `[MAX_RING]`                         | 20/30                               | BSS                                                                                       | yes                                                                                      |
| `ringBufferLoraRX` (dedup)                    | `loop_functions.cpp:392`                 | `[MAX_DEDUP_RING][5]`                | 100/70 × 5                          | `={0}`                                                                                    | yes (`loraWrite` is `atomic<uint8_t>`, wrapped)                                          |
| `ringbufferRAWLoraRX`                         | `loop_functions.cpp:396`                 | `[MAX_LOG][UDP_TX_BUF_SIZE+5]`       | 10/20 × 260                         | `={0}`                                                                                    | yes (fixed 254-byte memcpy)                                                              |
| `ringBufferUDPout`                            | `loop_functions.cpp:404`                 | `[MAX_RING_UDP][UDP_TX_BUF_SIZE+20]` | 20/25 × 275                         | **no initializer** (BSS)                                                                  | write yes (`len+1`≤256); **read overruns row** — see F3-17                               |
| `BLEtoPhoneBuff`                              | `loop_functions.cpp:409`                 | `[MAX_RING][MAX_MSG_LEN_PHONE+5]`    | 20/30 × 305                         | `={0}`                                                                                    | write yes; **length byte truncates** (F3-12)                                             |
| `BLEComToPhoneBuff`                           | `loop_functions.cpp:414`                 | `[MAX_RING][MAX_MSG_LEN_PHONE+5]`    | 20/30 × 305                         | `={0}`                                                                                    | **no clamp in `addBLEComToOutBuffer`**, only caller clamps                               |
| `cConcat1/2/3` (decodeAPRS)                   | `aprs_functions.cpp:184,187,190`         | `[UDP_TX_BUF_SIZE]` ×3               | 3×255 stack                         | `memset` each                                                                             | **NO** for the payload loop — see F3-7                                                   |
| `temp` (decodeAPRS)                           | `aprs_functions.cpp:124`                 | `[11]`                               | 11                                  | no                                                                                        | fixed 10-byte memcpy, but `rsize` may be < 10 (stale read)                               |
| `decode_text`                                 | `aprs_functions.cpp:535`                 | `[25]`                               | 25                                  | `memset`                                                                                  | yes (`ipt<11`)                                                                           |
| `msg_start` (encode*)                         | `aprs_functions.cpp:1000,1113,1169,1268` | `[UDP_TX_BUF_SIZE]`                  | 255 stack                           | no                                                                                        | snprintf-bounded                                                                         |
| `incomingPacket`                              | `udp_functions.cpp:67`                   | `UDP_TX_BUF_SIZE`                    | 255                                 | BSS                                                                                       | **NO** — `[len]=0` with len==255 (SEC-05, open)                                          |
| `convBuffer`                                  | `udp_functions.cpp:69`                   | `UDP_TX_BUF_SIZE+50`                 | 305                                 | `memset(…,255)` only                                                                      | yes                                                                                      |
| `incomingExtPacket`                           | `extudp_functions.cpp:44`                | `UDP_TX_BUF_SIZE`                    | 255                                 | BSS                                                                                       | **NO** — `[len]=0` (SEC-06, open)                                                        |
| `externQueue[].buffer`                        | `extudp_functions.cpp:50`                | `[500]` ×2                           | 1000                                | no                                                                                        | yes (`buflen ≤ sizeof`)                                                                  |
| `c_json`/`c_tjson`                            | `extudp_functions.cpp:262-266`           | `[500]` ×2                           | 1000 (stack on ESP32, BSS on nRF52) | yes                                                                                       | snprintf-bounded                                                                         |
| `hb_buffer`/`dt_buffer`                       | `udp_functions.cpp:1041,1089`            | `UDP_TX_BUF_SIZE+50`                 | 305 stack                           | no                                                                                        | yes (36+255=291 ≤ 305; margin only 14 B)                                                 |
| `inc_udp_buffer` (nRF ETH)                    | `nrf52/nrf_eth.cpp:34`                   | `UDP_TX_BUF_SIZE+5`                  | 260                                 | `memset(…,255)`                                                                           | yes                                                                                      |
| `config_buf` (nRF ETH CONF)                   | `nrf52/nrf_eth.cpp:511`                  | `UDP_CONF_BUFF_SIZE`                 | 255 stack                           | `={0}`                                                                                    | **NO — 251-byte overflow, F3-1**                                                         |

### Phone / BLE path (untrusted)

| Buffer                      | file:line                       | size expr                     | resolved  | zeroed?           | bounds-checked on write?                              |
| --------------------------- | ------------------------------- | ----------------------------- | --------- | ----------------- | ----------------------------------------------------- |
| `conf_data` (nRF52)         | `nrf52/nrf52_ble.cpp:251`       | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | read length discarded — F3-21                         |
| `BleQueueItem.data` (ESP32) | `esp32/esp32_main.cpp:354`      | `MAX_MSG_LEN_PHONE`           | 300       | `= {}`            | yes (`length ≤ 300` at `:356`)                        |
| `textbuff_phone`            | `phone_commands.cpp:22`         | `[MAX_MSG_LEN_PHONE]`         | 300       | `={0}`            | **NO** — `txt_msg_len_phone` underflow (BUG-07, open) |
| `toPhoneBuff`               | `phone_commands.cpp:65`         | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | yes (blelen ≤ 255)                                    |
| `ComToPhoneBuff`            | `phone_commands.cpp:133`        | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | **`blelen-1` underflow — F3-11**                      |
| `ssid_arr`/`pwd_arr`        | `phone_commands.cpp:559,560`    | **VLA** `[ssid_len+1]`        | 1..256    | `={0}` (GNU ext.) | **NO** (SEC-03, open)                                 |
| `call_arr`                  | `phone_commands.cpp:399`        | **VLA** `[msg_payload_len+1]` | 1..256    | **no**            | **uninitialised at len 0 — F3-23**                    |
| `helper_string`             | extern, `phone_commands.cpp:20` | `[256]`                       | 256       | —                 | snprintf-bounded                                      |

### Console / command path

| Buffer                                | file:line                                   | size expr             | resolved    | zeroed?              | bounds-checked on write?                                     |
| ------------------------------------- | ------------------------------------------- | --------------------- | ----------- | -------------------- | ------------------------------------------------------------ |
| `msg_text` (global)                   | `loop_functions.cpp:220`                    | `MAX_MSG_LEN_PHONE*2` | 600         | `={0}`               | snprintf-bounded; one `strcat` at `:2485` (safe, ≤35)        |
| `msg_text` (local, commandAction)     | `command_functions.cpp:199`                 | `[300]`               | 300 stack   | **no**               | filled by `snprintf`; **tail uninitialised → F3-13**         |
| `_owner_c`                            | `command_functions.cpp:200`                 | `[300]`               | 300 stack   | **no**               | snprintf-bounded; `_owner_c[-1]` write → F3-14               |
| `print_buff` (cmd)                    | `command_functions.cpp:88`                  | `[350]`               | 350 BSS     | `memset` before JSON | `serializeJson` sized by `measureJson`, not `sizeof` → F3-18 |
| `msg_buffer` (cmd)                    | `command_functions.cpp:90`                  | `[MAX_MSG_LEN_PHONE]` | 300 BSS     | `memset`             | yes (`json_len ≤ 298`)                                       |
| `msg_detail`                          | `command_functions.cpp:91`                  | `[100]`               | 100 BSS     | `memset`             | yes                                                          |
| `vmsg` (commandCheck)                 | `command_functions.cpp:115`                 | `[100]`               | 100 stack   | strncpy NUL-pads     | yes (all commands ≤ 18 chars)                                |
| `strText`                             | `esp32_main.cpp:240` / `nrf52_main.cpp:284` | `[600]`               | 600 BSS     | `={0}`               | **NO NUL slot reserved → F3-19**                             |
| `msg_buffer` (checkSerialCommand)     | `esp32_main.cpp:3929`                       | `[600]`               | 600 stack   | no                   | yes (`inext > sizeof-2` break)                               |
| `msg_text_check` / `msg_text_checked` | `loop_functions.cpp:3004,3005`              | `[200]` ×2            | 400 stack   | `memset`             | **NO — SEC-04 still open, F3-5**                             |
| `message_text` (web)                  | `web_setup.cpp:20`                          | `[200]`               | 200 stack   | no                   | snprintf-bounded everywhere                                  |
| `s_password`                          | `net_console.cpp:47`                        | `[15]`                | 15          | `={0}`               | —                                                            |
| `chalBuf`/`respBuf`                   | `net_console.cpp:146,156`                   | `[48]`,`[72]`         | 48/72 stack | respBuf `={0}`       | yes (`strcpy`/`strcat` of literals; `idx < 71`)              |

### MHeard / display

| Buffer                              | file:line                       | size expr                             | resolved               | zeroed?               | bounds-checked on write?                                        |
| ----------------------------------- | ------------------------------- | ------------------------------------- | ---------------------- | --------------------- | --------------------------------------------------------------- |
| `mheardBuffer`                      | `mheard_functions.cpp:24`       | `[MAX_MHEARD][60]`                    | 80/30 × 60             | `memset` at init      | yes, but copies 60 B incl. uninitialised stack tail (F3-24)     |
| `mheardCalls`                       | `mheard_functions.cpp:25`       | `[MAX_MHEARD][10]`                    | 80/30 × 10             | `memset`              | yes (`icsize` clamped)                                          |
| `mheardPathCalls`                   | `loop_functions_extern.h:304`   | `[MAX_MHPATH][10]`                    | 100/40 × 10            | `memset` before write | **source over-read — F3-3**                                     |
| `mheardPathBuffer1`                 | `loop_functions_extern.h:306`   | `[MAX_MHPATH][50]`                    | 100/40 × 50            | `memset` before write | **source over-read — F3-3**                                     |
| `pageText`/`pageLastText`           | `loop_functions.cpp:743,750`    | `[maxdisplines][25]`, `[PAGE_MAX][…]` | 7/11 × 25              | `={0}`                | strncpy + explicit NUL — correct                                |
| `pageTextLong2`/`pageLastTextLong2` | `loop_functions.cpp:745,752`    | `[200]`, `[PAGE_MAX][200]`            | 200 / 6-10×200         | `={0}`                | strncpy + explicit NUL — correct                                |
| `loc_buf` / `nformat` (printfdeb)   | `printfdeb_functions.cpp:90,41` | `[600]`, `[300]`                      | 900 stack **per call** | nformat `memset`      | vsnprintf-bounded; **`malloc` fallback in the RX path — F3-10** |

---

## Findings

### F3-1: `nrf_eth.cpp` CONF zero-fill loop overruns `config_buf[255]` by up to 251 bytes — remote, unauthenticated UDP

`src/nrf52/nrf_eth.cpp:511-520` · **Severity: CRITICAL**

```c
uint8_t config_buf[UDP_CONF_BUFF_SIZE] = {0};              // 255 bytes, stack
if (packetSize <= UDP_CONF_BUFF_SIZE && packetSize >= UDP_MSG_INDICATOR_LEN) {
    memcpy(config_buf, inc_udp_buffer + UDP_MSG_INDICATOR_LEN, packetSize - UDP_MSG_INDICATOR_LEN);
    for (int i = 0; i < UDP_CONF_BUFF_SIZE; i++)           // <-- 255 iterations, always
        config_buf[packetSize - UDP_MSG_INDICATOR_LEN + i] = 0x00;
```

The loop counter starts at 0 but the index is biased by `packetSize-4`, so it writes
`config_buf[packetSize-4 … packetSize-4+254]`. The guard only bounds `packetSize`, not the index.

**Exploit input:** one UDP datagram to the node's port 1990, 255 bytes, first four bytes
`47 41 54 45`→ no; `43 4F 4E 46` ("CONF") + 251 arbitrary bytes. Then `packetSize=255` →
the loop writes zeros to `config_buf[251 … 505]` — **251 bytes past a 255-byte stack array**,
smashing the saved registers/return address of `NrfETH::getUDP()`. No authentication of any kind
on this path. Affects every nRF52 build with `HAS_ETHERNET` (RAK4631 + RAK13800).
The loop is also entirely redundant — `config_buf` is already `= {0}`.

**Fix:** delete the loop (the `= {0}` initialiser already zero-fills), or write
`for (int i = packetSize - UDP_MSG_INDICATOR_LEN; i < UDP_CONF_BUFF_SIZE; i++) config_buf[i] = 0;`.

---

### F3-2: `blelen - 4` underflow → `memcpy(dst, src, 0xFFFFFFFF)` — reachable from one RF frame

`src/web_functions/web_functions.cpp:1279,1295-1296` · **Severity: CRITICAL**

```c
uint8_t blelen = BLEtoPhoneBuff[iRead][0];      // 1-byte length field
...
memcpy(toPhoneBuff, BLEtoPhoneBuff[iRead] + 1, blelen - 4);       // int arithmetic → size_t
memcpy(tbuffer,     BLEtoPhoneBuff[iRead] + 1 + (blelen - 4), 4);
```

`blelen` is `uint8_t`; `blelen - 4` promotes to `int` and, for `blelen ∈ {0,1,2,3}`, is negative →
converted to `size_t` = `0xFFFFFFFC … 0xFFFFFFFF`. `toPhoneBuff` is a 300-byte **stack** array.

`blelen ∈ {0,1,2,3}` is not hypothetical — it is exactly what BUG-08 produces.
`addBLEOutBuffer` (`loop_functions.cpp:527,546`):

```c
if (len > UDP_TX_BUF_SIZE) len = UDP_TX_BUF_SIZE-4;   // only clamps len > 255
BLEtoPhoneBuff[toPhoneWrite][0] = len + 4;            // uint16 → uint8
```

`len ∈ [252,255]` → `len+4 ∈ [256,259]` → stored as `0..3`.

**Exploit input:** transmit one LoRa text frame whose `decodeAPRS` result has
`aprsmsg.msg_len == 255` (a 255-byte frame with a correct FCS, `payload_type = 0x3A`,
`msg_destination_call = "*"`). `OnRxDone` → `addBLEOutBuffer(RcvBuffer, 255)`
(`lora_functions.cpp:903`) → `BLEtoPhoneBuff[w][0] = 3`. Any subsequent load of the web UI's
messages page (`sub_content_messages`) executes `memcpy(toPhoneBuff, src, (size_t)-1)`.
Guaranteed hard fault / arbitrary stack+heap destruction. Second memcpy additionally reads from
`src - 3`.

**Fix (two layers):** (a) clamp in `addBLEOutBuffer` at `len > UDP_TX_BUF_SIZE - 4`; (b) in the
consumer, `if (blelen < 4) { skip slot; continue; }` before any arithmetic, and cast the length
through `size_t` only after the check. Same treatment needed in `sendToPhone`.

---

### F3-3: `memcpy` sized by the _destination_ from an Arduino `String` source — heap over-read broadcast over the air

`src/mheard_functions.cpp:526,532` · **Severity: HIGH**

```c
int ipc = mheardLine.mh_sourcepath.length() - ips;
if(ipc > 37) ipc = 37;                       // <-- computed, then NEVER USED

memset(mheardPathCalls[ipos], 0x00, sizeof(mheardPathCalls[ipos]));
memcpy(mheardPathCalls[ipos], mheardLine.mh_sourcecallsign.c_str(),
       sizeof(mheardPathCalls[ipos]));       // reads 10 bytes
memset(mheardPathBuffer1[ipos], 0x00, sizeof(mheardPathBuffer1[ipos]));
memcpy(mheardPathBuffer1[ipos], mheardLine.mh_sourcepath.substring(ips).c_str(),
       sizeof(mheardPathBuffer1[ipos]));     // reads 50 bytes
```

The length is `sizeof(dest)`, never the source length. `mh_sourcecallsign` and `mh_sourcepath` are
`String`s built in `decodeAPRS` from RF bytes (`aprs_functions.cpp:238-240`) and Arduino `String`
on both cores heap-allocates exactly `len+1` (rounded to the allocator granule) — there is no SSO.

**Exploit input:** a HEY frame (`payload_type = 0x40`) with a minimal path, e.g.
`40 <id×4> <flags> "OE1A,OE1B>" 40 <payload> 00 <hw> <mod> <fcs×2>`. `mh_sourcecallsign` = `"OE1A"`
→ 5-byte heap block; `memcpy` reads 10 → 5 bytes past. `mh_sourcepath.substring(5)` = `"OE1B"`
→ 5-byte temporary; `memcpy` reads 50 → **45 bytes of adjacent heap** copied into
`mheardPathBuffer1[ipos]`. That buffer is then rendered on the display, returned by `--path`,
serialised into the BLE MHeard JSON and used to build outgoing HEY path payloads → remote heap
disclosure, plus a fault if the allocation sits at the end of a heap region.
Reached from `OnRxDone` → `updateHeyPath` (`lora_functions.cpp:637`) on every HEY from any node.

**Fix:** use the already-computed bound —
`size_t n = strlen(src); if (n > sizeof(dst)-1) n = sizeof(dst)-1; memcpy(dst, src, n);`
(the `memset` above already supplies the NUL). Do the same for `mheardPathCalls`.

---

### F3-4: nRF52 ETH `CONF` record — attacker-chosen `call_len`/`short_len` drive VLAs and OOB reads

`src/nrf52/nrf_eth.cpp:528-556` · **Severity: HIGH**

```c
int call_len = config_buf[1];                       // 0..255, attacker
char call_arr[call_len + 1];                        // VLA
call_arr[call_len] = '\0';
memcpy(call_arr, config_buf + 2, call_len);         // reads config_buf[2..256]  → 2 past a 255-byte array
...
if (config_buf[2 + call_len] == 0x01) {             // index up to 257 → OOB read
    short_len = config_buf[2 + call_len + 1];       // index up to 258 → OOB read
    char short_arr[short_len + 1];
    memcpy(short_arr, config_buf + (2 + call_len + 2), short_len);   // src up to +259, len up to 255
```

Same class as SEC-03 but on the **unauthenticated UDP** path rather than the paired-BLE path.
`packetSize` is known here and is never used to validate `call_len`/`short_len`.

**Exploit input:** UDP datagram `"CONF" 00 FF <251 bytes>` → `call_len = 0xFF`; the shortname block
then reads `config_buf[258]`/`config_buf[259 … 513]`, i.e. ~259 bytes of stack past `config_buf`,
which land verbatim in `shortname` → written to flash and beaconed as the node shortname.
Also allocates up to 512 bytes of VLA on a 4 KB nRF52 task stack.

**Fix:** validate `2 + call_len + 2 + short_len <= packetSize - UDP_MSG_INDICATOR_LEN` before use;
replace both VLAs with `char call_arr[10]` / `char short_arr[6]` matching the destination fields.

---

### F3-5: SEC-04 URL-decode loop still overruns both 200-byte stack buffers — NOT FIXED

`src/loop_functions.cpp:3004-3069` · **Severity: HIGH**

Re-verified: the loop is still counted by `iu` (unused in the body) while the source cursor `ii`
advances by up to 12 and the destination cursor `in` by up to 4 per iteration. Neither `in` nor `ii`
is bounded, and the `strMsg.length() > 160` gate is at `:3090`, after the loop.

**Exploit input:** a message body of 16 consecutive `%F0%9F%98%80` sequences (192 chars) followed
by ~8 filler chars, delivered as BLE msg-type `0xA0` (`::<body>`), through the web
`manualcommand`/message form, or over the TCP console on 2323. After 16 iterations `ii = 192`,
`in = 64`; the remaining ~184 iterations each copy one byte, driving `in` to ≈248 →
**writes `msg_text_checked[200..247]`** and reads `msg_text_check[200..375]`, both 48/176 bytes past
200-byte stack arrays in `sendMessage`.

**Fix:** drive the loop by the source index (`while (ii < len_check)`), add
`if (in >= (int)sizeof(msg_text_checked)-1) break;` inside, and move the length gate before decoding.

---

### F3-6: SEC-03 BLE `0x55` Wi-Fi record still unvalidated — NOT FIXED

`src/phone_commands.cpp:554-563` · **Severity: HIGH**

`ssid_len = conf_data[2]`, `pwd_len = conf_data[ssid_len+3]` (itself an unchecked index up to 258 in
a 300-byte buffer), guarded only by `> 0`. `memcpy(pwd_arr, conf_data + (4+ssid_len), pwd_len)`
reads from offset up to 259 for up to 255 bytes → up to 214 bytes past `conf_data[300]`.
Two attacker-sized VLAs (up to 512 bytes combined) on the nRF52 BLE-callback stack.

**Exploit input:** BLE write `05 55 FF …` → `ssid_len = 0xFF`, `pwd_len = conf_data[258]`.

**Root cause is F3-21** — `readPhoneCommand` never receives the real frame length.

---

### F3-7: `decodeAPRS` payload loop is unbounded relative to `cConcat1[255]`; `MAX_APRS_FRAME_SIZE` explicitly permits 340

`src/aprs_functions.cpp:9,149-150,184,364-378` · **Severity: HIGH (latent — no caller reaches it today)**

```c
#define MAX_APRS_FRAME_SIZE 340
uint16_t decodeAPRS(uint8_t RcvBuffer[UDP_TX_BUF_SIZE], uint16_t rsize, …)   // param says 255
    if(rsize > MAX_APRS_FRAME_SIZE) return 0x00;                             // gate says 340
    char cConcat1[UDP_TX_BUF_SIZE];                                          // buffer is 255
    for(ib=inext; ib < rsize; ib++) { … cConcat1[iConcat1] = RcvBuffer[ib]; iConcat1++; }  // no cap
```

The source- and destination-path loops are capped at 120 iterations each; the **payload loop is
not capped at all**. With `rsize = 340` and a minimal header (`inext ≈ 13`) the loop writes
`cConcat1[0 … 326]` — a **72-byte stack overflow**.

Why it is not exploitable _today_: every caller happens to clamp to ≤255
(`OnRxDone` size≤255 by PHY, `udp_functions.cpp:153`, `nrf_eth.cpp:272`, `queueExtern` sources).
It is a landmine, not a bug-of-the-day: the function's own contract (parameter type 255, gate 340)
is self-contradictory, `sendExtern` declares `uint8_t buffer[500]` and passes `buflen` straight
through (`extudp_functions.cpp:235,251`), and the `externQueue` entry buffer is 500 bytes. One
future caller that forwards a >271-byte frame turns this into a remote stack smash.

**Fix:** cap the payload loop (`&& iConcat1 < (int)sizeof(cConcat1)-1`) and make the gate consistent
— either `rsize > UDP_TX_BUF_SIZE → reject`, or size `cConcat1` to `MAX_APRS_FRAME_SIZE`.
Also add `static_assert(MAX_APRS_FRAME_SIZE <= sizeof(cConcat1))`.

---

### F3-8: `encodeStartAPRS` clamp forgets the 6-byte header offset → 5-byte overflow of every 255-byte encode target

`src/aprs_functions.cpp:1027-1032` · **Severity: MEDIUM-HIGH**

```c
uint16_t ilng = src_path.length() + 1 + dst_path.length() + 1;
if(ilng >= UDP_TX_BUF_SIZE) ilng = UDP_TX_BUF_SIZE - 1;   // 254
memcpy(msg_buffer+6, msg_start, ilng);                    // writes msg_buffer[6 .. 259]
return ilng+6;                                            // 260
```

The clamp bounds `ilng` against the buffer size but the write starts at `+6`. When `ilng` reaches
254 the copy writes `msg_buffer[255..259]` — 5 bytes past the callers that pass a 255-byte array:
`lora_functions.cpp:813,895` (`uint8_t tempRcvBuffer[255]`, stack, inside `OnRxDone`),
`udp_functions.cpp:306`, `nrf_eth.cpp:418`. `encodeAPRS` then calls
`encodePayloadAPRS(msg_buffer + 260, …)` (`:1052`) — the `(inext+10) >= UDP_TX_BUF_SIZE` clamp at
`:1060` happens _after_ that call, so the payload copy also starts out of bounds.

Reaching `ilng ≥ 254` needs `src_path + dst_path ≥ 252`. Purely-relayed frames conserve
`src+dst+payload ≤ 242` so the RF-only path stays 6 bytes short; the UDP-gateway path
(`udp_functions.cpp:213-214` appends `","+node_call`, then `checkVia` at `:311` prepends
`node_via + ","`) adds up to 41 characters and does cross it: a 255-byte GATE datagram with a
120-char source path and 120-char destination path, on a node with a long `node_via`, overflows
`tempRcvBuffer[UDP_TX_BUF_SIZE]` at `udp_functions.cpp:306`.

**Fix:** `if (ilng + 6 >= UDP_TX_BUF_SIZE) ilng = UDP_TX_BUF_SIZE - 7;` and move the
`inext + 10` clamp in `encodeAPRS` _above_ the `encodePayloadAPRS` call. Better: give both
functions an explicit `size_t buf_size` parameter.

---

### F3-9: SEC-05/SEC-06 UDP terminator off-by-one — NOT FIXED (both sites)

`src/udp_functions.cpp:100,104` and `src/extudp_functions.cpp:222,228` · **Severity: MEDIUM**

`read(buf, UDP_TX_BUF_SIZE)` can return 255; `buf[255] = 0` writes one past a 255-byte BSS array.
**Exploit input:** any 255-byte UDP datagram to port 1990 (resp. 1799). Corrupts the adjacent
global on every receive. One-line fix per site (`read(buf, UDP_TX_BUF_SIZE-1)`).

---

### F3-10: `printfdeb` re-parses formatted output as a format string, and `malloc`s inside the radio callback — NOT FIXED

`src/printfdeb_functions.cpp:90,102-109,118` · **Severity: HIGH (SEC-02 confirmed still live)**

`Serial.printf(temp)` at `:118` — already-substituted RF text is re-parsed as a format string.
Adding to the prior finding, from the buffer angle:

- `loc_buf[600]` + `nformat[300]` = **900 bytes of stack per call**, and `printfdeb` is called
  ~20× inside `OnRxDone`, which on nRF52 runs in the radio callback context.
- `:103` `temp = (char*)malloc(len+1)` — a **runtime heap allocation in the packet-RX path**,
  directly violating MEM-01 ("no `malloc`/`new` after initialization … no malloc in packet
  processing paths"). An attacker who makes `vsnprintf` produce >600 bytes (long source path +
  long payload in `printBuffer_aprs`, `loop_functions.cpp:2955`) forces a heap allocation per
  received frame → fragmentation and, on allocation failure, a silently dropped log line.
- `:66` `uformat[in-1]` is read when `in == 0` — a one-byte read _before_ the format string.
  Only reachable if a literal starts with `';'` (none do today), so Low on its own.

**Fix:** `Serial.printf("%s", temp);` (one line); drop the malloc path and just truncate; gate the
whole function behind a debug flag so it is not on the RX hot path.

---

### F3-11: `sendComToPhone` text branch: `blelen - 1` with `blelen == 0` → `memcpy` of `SIZE_MAX`

`src/phone_commands.cpp:135,151` · **Severity: MEDIUM**

```c
uint8_t blelen = BLEComToPhoneBuff[ComToPhoneRead][0];
...
else { ComToPhoneBuff[0] = 0x40;
       memcpy(ComToPhoneBuff+1, BLEComToPhoneBuff[ComToPhoneRead]+1, blelen-1); }   // 0-1 → SIZE_MAX
```

`BLEComToPhoneBuff[..][0] = len` is a `uint16_t → uint8_t` truncation
(`loop_functions.cpp:585`), and callers pass `json_len + 1` with `json_len` clamped to 298
(`command_functions.cpp:4482,4490`). `json_len == 255` → `len == 256` → stored as **0**.

**Exploit input:** configure telemetry strings so the `{"TYP":"TM","PARM":…}` JSON serialises to
exactly 255 bytes (`--setparm/--setunit/--setformat/--seteqns/--setvales`, 50 bytes each), then
request `--tel` from the phone. Hard fault. The same 1-byte length field also silently truncates
every config record >255 bytes (BUG-09, still warn-only with no clamp and no `return`).

**Fix:** widen the framing length to 2 bytes, or `if (len > 254) return;` in
`addBLEComToOutBuffer` _and_ `if (blelen < 1) { advance; return; }` in the consumer.

---

### F3-12: `addBLEOutBuffer` / `addBLEComToOutBuffer` write a 16-bit length into a 1-byte field with no clamp

`src/loop_functions.cpp:527-528,546,579-586` · **Severity: MEDIUM (trigger for F3-2 and F3-11)**

`addBLEOutBuffer`: guard is `len > UDP_TX_BUF_SIZE` but the stored value is `len+4` → wraps for
`len ∈ [252,255]`. `addBLEComToOutBuffer`: the `len > 245` check only _prints a warning_ — no clamp,
no early return — and then does `memcpy(BLEComToPhoneBuff[w]+1, buffer, len)` into a
`MAX_MSG_LEN_PHONE+5` = 305-byte row with **no upper bound on `len` at all**. Today every caller
happens to clamp to ≤299; one caller passing >304 is a BSS overflow across ring slots.

**Fix:** clamp at `len > UDP_TX_BUF_SIZE - 4` and `len > MAX_MSG_LEN_PHONE + 4` respectively, and
`return` after the warning.

---

### F3-13: `commandAction` offset arms read past the NUL into uninitialised stack — 9 of 217 arms

`src/command_functions.cpp:199,240` + the sites below · **Severity: MEDIUM**

`char msg_text[300];` at `:199` has **no initialiser**; it is filled only by
`snprintf(msg_text, sizeof(msg_text), "%s", sVar.c_str())` at `:240`, which writes `strlen+1` bytes
and leaves `msg_text[strlen+1 … 299]` as stack garbage (and `commandAction` recurses from the
3-argument overload, so the garbage is the previous invocation's frame — attacker-influenced).

`commandCheck(msg_text+2, cmd)` succeeding guarantees only `strlen(msg_text) >= 2 + strlen(cmd)`.
Any read at an offset **> `2 + strlen(cmd)`** can therefore start past the NUL.
(`commandCheck` itself is safe: `strncpy` NUL-pads `vmsg[100]` and the longest command is 18 chars.)

I enumerated all 217 arms mechanically. **208 are safe**, all by the same construction: the command
literal carries its own trailing space (e.g. `"setctry "`, `"button gpio "`), so the read offset
equals `2 + strlen(cmd)` — which is at worst the NUL itself, yielding an empty string. **9 arms
violate it**, all by treating the separator space as implicit:

| check   | cmd            | safe offset ≤ | actual read                                    | over by | triggering input |
| ------- | -------------- | ------------- | ---------------------------------------------- | ------- | ---------------- |
| `:257`  | `utcoff`       | 8             | `sscanf(msg_text+9,"%f",…)` `:259`             | 1       | `--utcoff`       |
| `:311`  | `settime`      | 9             | `String strSetTime = msg_text+10;` `:314`      | 1       | `--settime`      |
| `:367`  | `maxv`         | 6             | `sscanf(msg_text+7,"%f",…)` `:369`             | 1       | `--maxv`         |
| `:2327` | `extudpip`     | 10            | `snprintf(node_extern,…,msg_text+11)` `:2332`  | 1       | `--extudpip`     |
| `:2867` | `softser send` | 14            | `snprintf(_owner_c,…,msg_text+15)` `:2869`     | 1       | `--softser send` |
| `:3744` | `setout `      | 9             | `msg_text+10` `:3750`, `msg_text+12` `:3767`   | 1, 3    | `--setout `      |
| `:3811` | `setio `       | 8             | `msg_text+9` `:3817`, `msg_text+11` `:3830`    | 1, 3    | `--setio `       |
| `:4261` | `setgrc`       | 8             | `snprintf(_owner_c,…"%s;",msg_text+9)` `:4263` | 1       | `--setgrc`       |
| `:4348` | `regex`        | 7             | `snprintf(_owner_c,…,msg_text+8)` `:4350`      | 1       | `--regex`        |

`String strSetTime = msg_text+10;` (`:314`) is the worst of these: it runs `strlen` over
uninitialised stack; if no NUL exists in `msg_text[10..299]` the scan continues past the 300-byte
array into `_owner_c` and beyond, then heap-allocates a `String` of that length.
`snprintf("%s", msg_text+N)` is bounded on the _output_ but still `strlen`s the source, so the same
over-read applies.

Reachable from: BLE msg-type `0xA0` beginning with `--` (paired phone), the web `manualcommand`
parameter (`web_setup.cpp:26-32`), the TCP console on 2323, and the serial console.

**Fix (minimal):** `char msg_text[300] = {0};` — one character, kills the uninitialised read on all
9 arms and makes every offset a bounded empty string. **Fix (structural):** see the dispatch table
in the structural proposal.

---

### F3-14: `--softser send` writes `_owner_c[-1]` — one byte _before_ a stack array

`src/command_functions.cpp:2869-2871` · **Severity: MEDIUM**

```c
snprintf(_owner_c, sizeof(_owner_c), "%s", msg_text+15);
if(_owner_c[strlen(_owner_c)-1] == 0x0a)
    _owner_c[strlen(_owner_c)-1] = 0x00;
```

No empty check. When `_owner_c` is empty, `strlen()-1` is `(size_t)-1` and `_owner_c[SIZE_MAX]`
wraps to `_owner_c - 1` — a read, and if that byte happens to be `0x0A`, a **write** one byte before
a 300-byte stack array (adjacent to `msg_text[300]` in the same frame).

**Exploit input:** `--softser send` (exactly 14 chars, no argument) from BLE/web/console. Combined
with F3-13 the source is uninitialised, so an immediate NUL there is a coin flip, not a certainty —
which is worse, because it is intermittent.

**Fix:** `size_t n = strlen(_owner_c); if (n > 0 && _owner_c[n-1] == 0x0A) _owner_c[n-1] = 0;`

---

### F3-15: `handleACK` copies 12 bytes with no minimum-length gate — NOT FIXED

`src/lora_functions.cpp:202-214` · **Severity: MEDIUM**

Only gate is `payload[0] != MSG_TYPE_ACK`. `memcpy(print_buff, payload, 12)` then runs on a frame
that may be 1 byte long. On nRF52 `payload` points into `rxPayloadCopy[2][255]` (static) so bytes
past `size` are the previous packet's; on ESP32 it is the RadioLib buffer. `msg_id` is built from
those stale bytes at `:220` and can match `checkOwnTx` → `findAndStopRingSlot` cancels an unrelated
retransmission. **Exploit input:** a 1-byte LoRa frame `41`. One-line fix: `if (size < 12) return false;`.

---

### F3-16: UDP zero-scan reads one past on odd `packetSize` — NOT FIXED, and now in two files

`src/udp_functions.cpp:121-123`, `src/nrf52/nrf_eth.cpp:233-235` · **Severity: LOW-MEDIUM**

`for (i = 0; i < packetSize; i += 2) … buf[i+1]` — with odd `packetSize` the final `i+1 ==
packetSize`. In `udp_functions.cpp` the array is exactly 255 bytes, so a 255-byte datagram is a true
OOB read. In `nrf_eth.cpp` the array is `UDP_TX_BUF_SIZE+5` so it stays in-bounds — the divergence
noted as DRY-21 means the fix has to be applied twice.

---

### F3-17: `sendMeshComUDP` reads 16 bytes past the ring row (and past the whole array on the last slot)

`src/udp_functions.cpp:423,454`, `src/nrf52/nrf52_main.cpp:2648` · **Severity: LOW-MEDIUM**

```c
uint16_t msg_len = ringBufferUDPout[udpRead][0];                       // up to 255
memcpy(convBuffer, ringBufferUDPout[udpRead] + 1 + 36, msg_len);       // 37 + 255 = 292
```

Row size is `UDP_TX_BUF_SIZE+20` = **275**. With `msg_len = 255` the copy reads 17 bytes past the
row; when `udpRead == MAX_RING_UDP-1` that is past the entire `ringBufferUDPout` array. The result
is fed straight into `decodeAPRS`. Triggered by any gateway forwarding a max-size frame.

**Fix:** `size_t n = min<size_t>(msg_len, sizeof(ringBufferUDPout[0]) - 37);` — and note the magic
`36` is the `"DATA"+GW_ID+call+ver+rssi+snr+mod` header length hard-coded in three places.

---

### F3-18: `serializeJson` sized by `measureJson(doc)` instead of `sizeof(dest)`

`src/command_functions.cpp:4479,4529,4639,4729,4967,4995,5037,…` (13 sites), `src/mheard_functions.cpp:653` · **Severity: LOW-MEDIUM**

```c
char print_buff[350];                                    // command_functions.cpp:88
serializeJson(tmdoc, print_buff, measureJson(tmdoc));    // size arg is the *required* size
```

The bound is derived from the document, not from the buffer, so it provides no protection at all —
if the document is larger than 350, `serializeJson` writes 350+ bytes into a 350-byte BSS array.
The telemetry document carries five 50-byte user strings (`node_parm/unit/format/eqns/values`);
with JSON escaping (`"` → `\"`) each can double, and the escaped worst case exceeds 350.
Passing `measureJson(doc)` also means the buffer is **not NUL-terminated** when the document
exactly fits — the subsequent `strlen(print_buff)` then depends on the preceding `memset`.
`mheard_functions.cpp:653` has the same shape against a 300-byte stack array.

**Fix:** always `serializeJson(doc, buf, sizeof(buf))` and check the returned byte count.

---

### F3-19: `strText[600]` can be filled completely, leaving `strlen` to run past the array

`src/esp32/esp32_main.cpp:3882-3885,3904-3907,3913`, `src/nrf52/nrf52_main.cpp:2525-2528,2533` · **Severity: MEDIUM**

```c
strText[iTxtPos] = rd;                     // write first
if(iTxtPos < sizeof(strText) - 1)          // bound second
    iTxtPos++;
...
iTxtLen = strlen(strText);
```

At `iTxtPos == 599` the write lands on the last slot and `iTxtPos` stops there — the array now has
600 non-NUL bytes and the `strlen` at `:3913` walks into adjacent BSS until it finds a zero.
**Exploit input:** 600+ bytes with no `\n`/`\r` on the TCP console (port 2323) or serial.
Note also `iTxtPos < sizeof(strText) - 1` compares `int` against `size_t` on ESP32 while the nRF52
copy at `:2526` has the `(int)` cast — the DRY-22 drift, and a `-Wsign-compare` violation.

**Fix:** bound before writing (`if (iTxtPos < (int)sizeof(strText)-1) strText[iTxtPos++] = rd;`).

---

### F3-20: `iVar` / `dVar` / `fVar` used uninitialised when `sscanf` finds no conversion

`src/command_functions.cpp:201-203` + 40 `sscanf` sites · **Severity: MEDIUM**

`int iVar; double dVar=0.0; float fVar=0.0;` — `iVar` is **uninitialised**. `sscanf` leaves its
output untouched on a failed match, so e.g.
`sscanf(msg_text+14, "%d", &meshcom_settings.node_button_pin)` (`:991`),
`sscanf(msg_text+14, "%d", &meshcom_settings.node_analog_pin)` (`:1022`) and
`sscanf(msg_text+8, "%d", &meshcom_settings.node_parm_time)` (`:4444`) write **directly into the
persisted settings struct** and leave the previous value / garbage there when the argument is
missing or non-numeric. Several of these become GPIO pin numbers.
Only 3 sites (`--setout`, `--setio`, `--rotate`) pre-seed a sentinel and range-check afterwards.
None of the 40 `sscanf` calls check the return value.

**Fix:** `if (sscanf(...) != 1) { reply("bad argument"); return; }` at every site, plus range checks
before assigning to settings (rule 3: "Integer parameters: check range before cast").

---

### F3-21: `readPhoneCommand` has no length parameter — every length check trusts attacker bytes

`src/phone_commands.h:4`, `src/phone_commands.cpp:208,241-243` · **Severity: MEDIUM (structural root of F3-6, BUG-07)**

```c
void readPhoneCommand(uint8_t conf_data[MAX_MSG_LEN_PHONE]);   // decays to uint8_t*
uint8_t msg_len = conf_data[0];   // attacker-supplied
```

Both callers know the real length and throw it away:

- `esp32_main.cpp:354-359` validates `item.length ≤ MAX_MSG_LEN_PHONE`, queues it — then
  `:2776 readPhoneCommand(bleItem.data)` drops `item.length`.
- `nrf52_ble.cpp:252` `g_ble_uart.read(conf_data, MAX_MSG_LEN_PHONE)` discards the return value.

Consequence: `0x55` (F3-6), `0xA0` (`txt_msg_len_phone = msg_len - 2` underflows for `msg_len ∈
{0,1}` → 254/255, BUG-07), `0x10` (`msg_len >= 35` gates a `memcpy(recv_hash, conf_data+4, 32)`),
and `0x50` all validate against a byte the attacker chose.

**Fix:** `void readPhoneCommand(const uint8_t *conf_data, size_t len)` and gate every case on
`len`, not on `conf_data[0]`.

---

### F3-22: `strncpy` without NUL termination in the VIA path

`src/via_functions.cpp:107-129` · **Severity: LOW-MEDIUM**

```c
char cMH[10];
memset(cMH, 0x00, sizeof(cMH));
strncpy(cMH, mheardCalls[iset], sizeof(cMH));     // n == sizeof(dest), no room for NUL
...
aprsmsg.msg_destination_path = cMH;               // strlen over a possibly unterminated array
```

`mheardCalls[MAX_MHEARD][10]` is filled by a bounded `memcpy` that reserves the terminator
(`mheard_functions.cpp:299-301`), so today `cMH` is terminated by luck rather than construction.
`sizeof(cMH)-1` is the correct bound. Other `strncpy` sites in `loop_functions.cpp` (`:1729,1731,
2245,2247,2301,2303,2347,2349`) all use `sizeof-1` **and** write the terminator explicitly — those
are correct and are the pattern to copy.
`src/t-deck-pro/ui_deckpro_port.cpp:223` `strncpy(list[i].name, WiFi.SSID(i).c_str(), 16)` has the
same unterminated shape.

---

### F3-23: BLE `0x50` (set callsign) reads uninitialised VLA when the payload length is 0

`src/phone_commands.cpp:399-408` · **Severity: LOW-MEDIUM**

```c
char call_arr[msg_payload_len + 1];        // VLA, NO initialiser
for (int i = 0; i < msg_payload_len; i++) { call_arr[i] = conf_data[i+3]; call_arr[i+1] = 0; }
String sVar = call_arr;
```

With `msg_payload_len == 0` the loop never runs and `call_arr[1]` is uninitialised stack;
`String sVar = call_arr` then `strlen`s it. **Exploit input:** BLE write `03 50 00` → the node's
callsign is set to whatever stack bytes follow, and `snprintf(node_call, 10, "%s", …)` persists it.

**Fix:** `char call_arr[MAX_CALL_LEN+1] = {0};` plus `if (msg_payload_len == 0 || msg_payload_len >
sizeof(call_arr)-1) break;`.

---

### F3-24: minor / dead-code buffer issues

**Severity: LOW**

- `src/loop_functions.cpp:471` `printfdeb("… RCV:%s\n", size, RcvBuffer+6)` — `%s` on a buffer that
  `memcpy(RcvBuffer, payload, size)` did **not** NUL-terminate. In-bounds today only because bytes
  255-509 of `RcvBuffer` are never written, but it leaks the tail of the _previous_ frame into the log.
- `src/mheard_functions.cpp:323-326` `char cBuffer[60]; snprintf(...); memcpy(mheardBuffer[ipos],
cBuffer, sizeof(cBuffer));` — copies the uninitialised tail after the NUL into BSS, and
  `mheard_functions.cpp:174` writes the whole array to SD → stack contents persisted to storage.
- `src/loop_functions.cpp:3112-3113` BUG-11 still open: `char cnewMsg[10]` for a `{mcp}` reformat
  whose minimum output is 13 bytes → silent truncation of the command and password.
- `src/aprs_functions.cpp:397-403` BUG-13 still open: the trailer/FCS bytes are read without
  checking `inext+3 < rsize`; `bPayloadEndOk` only proves a NUL exists, not that 4 bytes follow.
- `src/loop_functions.cpp:2049` `memcpy(cpasswd, cset+12, 5)` into `char cpasswd[6]` leaves
  `cpasswd[5]` uninitialised; only saved by the `%-5.5s` precision at `:2093`.
- `src/Displays/BaseDisplay/SD.cpp:368` `strcpy(&filename[8], ".bmp", sizeof(4));` — a three-argument
  `strcpy`, i.e. code that cannot compile. Confirmed dead: `src_filter` excludes `-<Displays/*>`
  (`platformio.ini:139`). Delete it.
- `src/net_console.cpp:147,149` `strcpy`/`strcat` on `chalBuf[48]` — safe (all literals, max 41
  bytes used) but a plain BND-01 violation in the security-sensitive path.
- `src/loop_functions.cpp:2485` `strcat(msg_text, " >>>>>>>>>>>>>>")` — safe (≤35 of 600) but BND-01.

---

## Type-safety findings

### T3-1: `printBuffer_ack(char*, uint8_t[…], int8_t size)` — 16-bit length narrowed to a signed byte

`src/loop_functions.h:46`, `src/loop_functions.cpp:2960`, call `src/lora_functions.cpp:211`
· **Severity: MEDIUM**

`handleACK` passes `uint16_t size` into an `int8_t` parameter. Any frame ≥128 bytes arrives as a
**negative** length. The body only tests `size == 7`, so every other value — including `-1` from a
255-byte frame — takes the branch that unconditionally reads `payload[0..11]`, on a frame that may
be 1 byte long (see F3-15). Rule 11: "NO implicit narrowing … requires explicit cast + range check".
**Fix:** `uint16_t size`, and gate the 12-byte branch on `size >= 12`.

### T3-2: `uint8_t(sizeof(RcvBuffer))` truncates 510 → 254

`src/nrf52/nrf52_main.cpp:469` · **Severity: LOW (wrong today, dangerous when copied)**

```c
for (int i = 0; i < uint8_t(sizeof(RcvBuffer)); i++)   // uint8_t(510) == 254
    RcvBuffer[i] = 0x00;
```

The comment says "clear the buffers"; it clears 254 of 510 bytes. Benign at boot (BSS is already
zero) but this is precisely the `size_t → uint8_t` idiom the rules forbid, sitting one line away
from the RF receive buffer.

### T3-3: 16-bit lengths stored in 1-byte framing fields, no clamp

`src/loop_functions.cpp:546` (`= len + 4`), `:585` (`= len`), `src/phone_commands.cpp:92,160`
(`blelen = blelen + 2` on a `uint8_t`), `src/nrf52/nrf_eth.cpp:632` (`ringBuffer[iWrite][0] =
rx_buf_size`) · **Severity: MEDIUM** — this is the mechanism behind F3-2, F3-11 and F3-12.
`blelen = blelen + 2` additionally wraps 254/255 → 0/1 and is then passed as the BLE write length.

### T3-4: `uint8_t` length arithmetic that can go negative

- `src/phone_commands.cpp:526` `txt_msg_len_phone = msg_len - 2` (BUG-07, open).
- `src/phone_commands.cpp:72,140,151` `blelen - 1` (F3-11).
- `src/web_functions/web_functions.cpp:1295,1296` `blelen - 4` (F3-2).
- `src/command_functions.cpp:2870` `strlen(_owner_c) - 1` on `size_t` (F3-14).
  All follow the same pattern: an unsigned byte minus a constant, promoted to `int`, then implicitly
  converted to `size_t` at the `memcpy` call — the one place where the sign is silently lost.

### T3-5: signed/unsigned comparisons on length checks

- `src/esp32/esp32_main.cpp:3905` `if(iTxtPos < sizeof(strText) - 1)` — `int` vs `size_t`
  (the nRF52 twin at `nrf52_main.cpp:2526` has `(int)`; the fix was applied to one copy only).
- `src/esp32/esp32_main.cpp:3947` `if(inext > sizeof(msg_buffer)-2)` — `int` vs `size_t`.
- `src/loop_functions.cpp:3007` `if(len_check > sizeof(msg_text_check)-1)` — `int len` vs `size_t`;
  a negative `len` converts to a huge unsigned and _accidentally_ takes the clamp branch. Correct by
  luck.
  None of the firmware environments build with `-Wsign-compare` (see R-10 below), so none of these is
  diagnosed.

### T3-6: `char` vs `uint8_t` on payload bytes

`decodeAPRS` stores RF bytes as `char` (`aprs_functions.cpp:208,225,232,306,318,375`) on a target
where `char` is signed. Bytes ≥ 0x80 become negative and flow into `String`, `is_equ`, `Regexp`.
The URL decoder (`loop_functions.cpp:3045,3050`) compares `msg_text_check[ii+is] >= 'A'` on a signed
`char`, so any byte ≥ 0x80 takes the `- '0'` branch and produces a large negative `ib` that is then
stored into `msg_text_checked[in]`. Not a memory-safety bug on its own, but it makes the decode
result unpredictable for exactly the inputs an attacker controls.

### T3-7: explicit truncating casts on lengths

`src/nrf52/nrf_eth.cpp:279` `sendExtern(true, "udp", RcvBuffer, (uint8_t)lora_tx_msg_len, 0, 0)` —
`lora_tx_msg_len` is `uint16_t` and the parameter is `uint16_t`; the cast can only lose data.
Harmless today (values ≤255) but it is a cast that hides exactly the class of bug in T3-3.

### T3-8: `int` ring indices shared across tasks

`toPhoneWrite/toPhoneRead/udpWrite/udpRead/ComToPhoneWrite` are plain `int`
(`loop_functions_extern.h:189,190,196,197,201`) while `iWrite/iRead/loraWrite` were converted to
`std::atomic<uint8_t>`. From the buffer angle the risk is that a torn/crossed index makes
`BLEtoPhoneBuff[toPhoneRead]` and `ringBufferUDPout[udpRead]` read a slot the writer is mid-memcpy
into — which is how F3-2's `blelen` can be observed as a partially-written value even after the
length-byte fix. (Overlaps CONC-15/16 in the verdict; noted here because it also gates F3-2/F3-11.)

---

## Rule violations (`docs/codequality-rules.md`)

| Rule             | Requirement                                                           | Status                            | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| ---------------- | --------------------------------------------------------------------- | --------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1** (MEM)      | No `malloc`/`new` after init; none in packet paths                    | **VIOLATED**                      | `printfdeb_functions.cpp:103` mallocs inside `OnRxDone`'s logging; `String` allocation throughout `decodeAPRS`/`aprsMessage` (7 `String`s per received frame, `aprs_structures.h:20-26`) and `mheardLine` (7 more, `:69-75`)                                                                                                                                                                                                                                                                                                                                                                       |
| **1** (MEM)      | Never Arduino `String` in hot paths                                   | **VIOLATED**                      | `struct aprsMessage` / `struct mheardLine` are constructed per received frame in `OnRxDone`; `decodeAPRS` performs ≥6 heap assignments, `checkVia` 2 more, `String searchPath = String(",") + … ` at `lora_functions.cpp:1114-1115` allocates twice per relay decision. Nothing checks for allocation failure: a failed `String` assignment yields an **empty** string, and the code then relays a frame with an empty source path / writes an empty callsign into `mheardCalls`, silently. Under heap exhaustion this is a correctness failure that presents as data corruption, not as an error. |
| **1** (MEM)      | All buffer sizes as `#define` in a single header                      | **PARTIAL**                       | `configuration_global.h` holds the ring sizes, but literals `[200]`, `[300]`, `[350]`, `[500]`, `[600]`, `[60]`, `[30]`, `[20]` are hard-coded at ~120 declaration sites; `MAX_CALL_LEN`/`LONGNAME_MAXLEN` are defined and used **nowhere** while the real field is `node_call[10]`                                                                                                                                                                                                                                                                                                                |
| **2** (BND-01)   | NEVER `sprintf`/`strcpy`/`strcat`/`gets`                              | **VIOLATED**                      | `net_console.cpp:147,149`; `loop_functions.cpp:2485,3494`; `esp32_main.cpp:816`; `nrf52_main.cpp:575`; `t-deck-pro/ui_deckpro.cpp:1864,2117,2122,2519`; dead `Displays/BaseDisplay/SD.cpp:368`                                                                                                                                                                                                                                                                                                                                                                                                     |
| **2** (BND-02)   | Always `snprintf` with the **correct** size                           | **VIOLATED**                      | 50 `snprintf` calls whose size argument is not `sizeof(dest)`; 14 `serializeJson` calls sized by `measureJson` (F3-18)                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| **2** (BND-03)   | `strncpy`/`strncat` with bounds                                       | **PARTIAL**                       | `via_functions.cpp:119`, `t-deck-pro/ui_deckpro_port.cpp:223`, `web_functions.cpp:1836` pass `sizeof(dest)` with no room for the NUL (F3-22)                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **2** (BND-04)   | All `memcpy`: validate length BEFORE copying                          | **VIOLATED**                      | 74 `memcpy` calls with a non-constant length; unvalidated: F3-1, F3-2, F3-3, F3-4, F3-6, F3-11, F3-15, F3-17, `loop_functions.cpp:586`, `nrf_eth.cpp:637`                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| **2** (BND-05)   | Bounds-check every array index                                        | **VIOLATED**                      | F3-1 (index biased by attacker length), F3-4 (`config_buf[2+call_len]`), F3-6 (`conf_data[ssid_len+3]`), F3-13 (offset past NUL)                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **2**            | `static_assert` on all protocol struct sizes                          | **VIOLATED — zero**               | `grep -rn 'static_assert' src` → 3 hits, all inside a stale audit `.md` under `src/code_review/`. No `static_assert` in any compiled file. The 2026-05-08 audit already raised this (BND-04) and it is still open.                                                                                                                                                                                                                                                                                                                                                                                 |
| **2**            | Check `snprintf` return for truncation (`>= buf_size`)                | **VIOLATED — 0 of ~400**          | Not one `snprintf` return value is captured anywhere in `src/`. Same for all 40 `sscanf` calls (F3-20).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| **3**            | Radio RX: validate packet length before buffer copy                   | **PARTIAL**                       | nRF52 clamps (`lora_functions.cpp:313`); ESP32 (`esp32_main.cpp:3778`) passes the driver length straight to `memcpy(RcvBuffer, payload, size)` (`:407`) with no clamp — safe only because `RcvBuffer` is 2× the PHY maximum                                                                                                                                                                                                                                                                                                                                                                        |
| **3**            | Network messages: validate header length fields against actual data   | **VIOLATED**                      | F3-1, F3-4 (UDP `CONF`), F3-17                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| **3**            | BLE writes: validate data length before parsing                       | **VIOLATED**                      | F3-21 — the real length never reaches the parser                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **3**            | Integer parameters: check range before cast                           | **VIOLATED**                      | F3-20                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **10** (COMP-01) | `-Wall -Wextra -Werror`                                               | **VIOLATED**                      | `-Wall -Wextra` appears only in the two `safeboot` environments (`platformio.ini:158,185,225`). No firmware target enables it; `-Werror` appears nowhere; `-Wconversion`/`-Wsign-compare`/`-Wformat=2` nowhere. 31 variant `.ini` files, 0 with warnings-as-errors.                                                                                                                                                                                                                                                                                                                                |
| **10**           | No implicit signed/unsigned conversion in size calculations           | **VIOLATED**                      | T3-5                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| **10**           | Use `size_t` for all sizes/lengths                                    | **VIOLATED**                      | T3-1, T3-3, T3-4                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **11**           | No implicit narrowing                                                 | **VIOLATED**                      | T3-1, T3-2, T3-3                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **11**           | Protocol structs: `__attribute__((packed))` or field-by-field         | **N/A/OK**                        | decode is field-by-field; but no `static_assert` on the resulting frame layout                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| **11**           | Pointer arithmetic: validate `offset < len` before `buf[offset]`      | **VIOLATED**                      | F3-4, F3-13, `aprs_functions.cpp:397-403`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| **13**           | Log macros always use literal format strings                          | **VIOLATED**                      | `printfdeb_functions.cpp:118` `Serial.printf(temp)` (SEC-02)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **15**           | Checksums verified BEFORE any field parsing                           | **VIOLATED (known, D4 deferred)** | FCS is checked at `aprs_functions.cpp:414`, after the source/destination/payload parse loops — i.e. after the loop in F3-7                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| **15**           | Minimum and maximum frame sizes validated before access               | **PARTIAL**                       | `rsize < 16` / `rsize > 340` gates exist, but `MAX_APRS_FRAME_SIZE` (340) exceeds the buffer the parser writes into (255) — F3-7                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **17**           | Compile-time constants: single source of truth, never redefined       | **VIOLATED**                      | `MAX_CALL_LEN 20` / `LONGNAME_MAXLEN 20` vs `node_call[10]`; `MAX_RING`/`MAX_MHEARD`/… re-declared in 5 `#if` branches, 3 of which (`ENABLE_XML`, `ENABLE_SBUFFER`, `ENABLE_TBEAM`) are dead                                                                                                                                                                                                                                                                                                                                                                                                       |
| **19**           | Parsers take `(const uint8_t*, size_t)`; boundary tests 0/1/max/max+1 | **VIOLATED**                      | `readPhoneCommand` takes no length (F3-21); `decodeAPRS`'s array parameter decays and lies about its size; zero tests exist (TEST-36/37 still open)                                                                                                                                                                                                                                                                                                                                                                                                                                                |

---

## Structural fix proposal

The bug list above has one shape repeated 20 times: **a length and a buffer travel separately, and
the check that ties them together is written by hand at each site.** Every finding is a site where
that hand-written check is missing, off by a constant, or applied to the wrong operand. Fixing the
20 sites leaves the 21st to be written next month. Four changes make the class structurally
impossible; the first two are small enough to be minimal-change PRs against upstream DEV.

### 1. One size header with `static_assert` on every relationship (rules 1, 2, 17)

Add `src/buffer_sizes.h`, included from `configuration_global.h`, containing every buffer size as a
named constant plus the invariants that the code currently assumes silently:

```c
static_assert(sizeof(RcvBuffer)            >= 2 * LORA_MAX_PHY_PAYLOAD, "RX copy target");
static_assert(MAX_APRS_FRAME_SIZE          <= APRS_CONCAT_BUF_SIZE,     "F3-7");
static_assert(APRS_ENCODE_HEADER_LEN + APRS_MAX_PATH_TOTAL + 1 <= UDP_TX_BUF_SIZE, "F3-8");
static_assert(sizeof(BLEtoPhoneBuff[0])    >= UDP_TX_BUF_SIZE + 5,      "BLE row");
static_assert(UDP_TX_BUF_SIZE + BLE_TIMESTAMP_LEN <= UINT8_MAX,         "F3-12: 1-byte length field");
static_assert(sizeof(ringBufferUDPout[0])  >= UDP_HDR_LEN + 1 + UDP_TX_BUF_SIZE, "F3-17");
static_assert(sizeof(meshcom_settings.node_call) - 1 >= MAX_CALL_LEN,   "MAX_CALL_LEN vs node_call[10]");
```

The last one **fails today** and is the point: it converts an invisible documentation lie into a
build error. `static_assert` on the wire-frame layout (`APRS_HDR_LEN == 6`,
`APRS_TRAILER_LEN == 6`) pins the constants that `decodeAPRS`/`encodeAPRS` currently rediscover
with magic `+6`/`+10`/`+36`.

Cost: one header, ~30 lines, no behavioural change. Catches F3-8, F3-12, F3-17 at compile time and
documents the rest.

### 2. A bounded span for the RX buffer (rules 3, 11, 19)

```c
struct ByteSpan {
    const uint8_t *p; size_t n;
    bool     has(size_t off, size_t len) const { return off <= n && len <= n - off; }  // no overflow
    uint8_t  at(size_t i, uint8_t dflt = 0) const { return i < n ? p[i] : dflt; }
    ByteSpan sub(size_t off, size_t len) const { return has(off,len) ? ByteSpan{p+off,len} : ByteSpan{p,0}; }
};
```

Change the signatures to `decodeAPRS(ByteSpan frame, aprsMessage&)`,
`readPhoneCommand(ByteSpan cmd)`, `getMeshComUDPpacket(ByteSpan pkt)`,
`handleACK(ByteSpan frame, …)`, `getUDP` → `ByteSpan`. Then:

- F3-4 and F3-6 become impossible: `conf_data[ssid_len+3]` is `cmd.at(ssid_len+3)`, which returns 0
  for an out-of-range index instead of reading the stack.
- F3-15 becomes `if (!frame.has(0,12)) return false;`.
- F3-21 disappears — the length is part of the type, so it cannot be dropped at a call site.
- Rule 19 ("all parsers accept `(const uint8_t*, size_t)`") is satisfied by construction, which is
  what makes the parsers fuzzable off-target.

`has()` is written as `len <= n - off` after the `off <= n` test specifically so that
`off + len` can never wrap — the integer-overflow half of rule 10.

Cost: a 15-line header plus signature changes at ~8 call sites. Contained enough for one PR.

### 3. A length-carrying dispatch table for `commandAction` (F3-13, F3-14, F3-20)

Replace the 217 `if (commandCheck(msg_text+2, "x") == 0) { … msg_text+N … }` arms with

```c
struct Cmd { const char *name; uint8_t argc_min; void (*fn)(ByteSpan arg, bool ble); };
```

The dispatcher matches `name`, computes `arg = full.sub(2 + strlen(name), …)` **once**, skips
separator whitespace, and rejects the command if `arg.n < argc_min`. The handler never sees an
offset, so an off-by-one offset cannot be written. This also collapses SIMP-26 (the 4860-line
function) and makes the 40 `sscanf` sites a single `parse_int(arg, &out, min, max)` helper that
returns an error — fixing F3-20 wholesale.

This is a Track-B refactor and should be proposed to upstream as a plan first (`CLAUDE.md` rule 2).
The **one-character Track-A mitigation in the meantime is `char msg_text[300] = {0};`** at
`command_functions.cpp:199`, which neutralises all 9 offset arms today.

### 4. Build flags and a native fuzz target (rules 10, 19)

For the firmware environments in `platformio.ini` and the 31 variants:

```
-Wall -Wextra -Werror
-Wconversion -Wsign-compare -Wformat=2 -Wformat-security
-Wvla                      # the four attacker-sized VLAs in F3-4/F3-6/F3-23
-fstack-protector-strong
-Wstack-usage=1536         # printfdeb's 900-byte frame in the radio callback
```

`-Wconversion` alone flags T3-1..T3-5. `-Wvla` flags F3-4/F3-6/F3-23. `-Wformat-security` flags
`Serial.printf(temp)` (F3-10). Expect a large first-pass warning count — stage it as
`-Werror=conversion` on the touched files first.

For the native test environment that TEST-37 must create anyway, add
`-fsanitize=address,undefined -fno-sanitize-recover=all` and a libFuzzer entry point:

```c
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    aprsMessage m; decodeAPRS(ByteSpan{d,n}, m); return 0;
}
```

plus the same for `readPhoneCommand` and `getMeshComUDPpacket`. F3-1, F3-4, F3-5, F3-7 and F3-15
are all first-minute findings for such a harness. `_FORTIFY_SOURCE` is not useful here (newlib-nano
on both targets does not implement the `__*_chk` builtins), so ASan on the native build is the
substitute.

---

## Still-open prior findings re-verified against HEAD

| Prior ID           | Claim                                      | Status at `1ba101f4`                                                                                                               |
| ------------------ | ------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| SEC-02             | `printfdeb` format-string injection        | **OPEN** — `printfdeb_functions.cpp:118` unchanged                                                                                 |
| SEC-03             | BLE 0x55 OOB read                          | **OPEN** — `phone_commands.cpp:554-563` unchanged (F3-6)                                                                           |
| SEC-04             | URL-decode 200-byte overflow               | **OPEN** — `loop_functions.cpp:3022-3069` unchanged (F3-5)                                                                         |
| SEC-05             | UDP `incomingPacket[255]=0`                | **OPEN** — `udp_functions.cpp:100,104` (F3-9)                                                                                      |
| SEC-06             | ext-UDP `incomingExtPacket[255]=0`         | **OPEN** — `extudp_functions.cpp:222,228` (F3-9)                                                                                   |
| BUG-07             | 0xA0 `msg_len-2` underflow                 | **OPEN** — `phone_commands.cpp:526`                                                                                                |
| BUG-08             | `len+4` in a 1-byte field                  | **OPEN** — `loop_functions.cpp:546`; now shown to be the trigger for a `SIZE_MAX` memcpy (F3-2)                                    |
| BUG-09             | `addBLEComToOutBuffer` warn-only           | **OPEN** — `loop_functions.cpp:579-586` (F3-12)                                                                                    |
| BUG-10             | `handleACK` no `size>=12`                  | **OPEN** — `lora_functions.cpp:214` (F3-15)                                                                                        |
| BUG-11             | `cnewMsg[10]` too small                    | **OPEN** — `loop_functions.cpp:3112`                                                                                               |
| BUG-12             | UDP zero-scan `i+1`                        | **OPEN** — `udp_functions.cpp:123`, and a second copy at `nrf_eth.cpp:235` (F3-16)                                                 |
| BUG-13             | APRS trailer read past payload             | **OPEN** — `aprs_functions.cpp:397-403`                                                                                            |
| D4 (2026-06-27)    | "APRS FCS check before parsing — deferred" | **STILL DEFERRED**, and F3-7 shows why it matters: the payload loop that can overflow runs before the FCS check                    |
| C1/B3 (2026-06-27) | "iWrite/iRead atomic" — claimed done       | **CORRECT** for `iWrite/iRead/loraWrite`; `toPhoneWrite/toPhoneRead/udpWrite/udpRead/ComToPhoneWrite` are still plain `int` (T3-8) |
