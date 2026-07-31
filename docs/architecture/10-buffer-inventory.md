# 10 — Buffer Inventory & Type Safety

> **Where does every byte of attacker-controlled data land, how large is the array it lands in,
> and what makes that relationship true — a check somebody remembered to write, or the type
> system?**

Re-verified 2026-07-31 against the working tree at `3fb2c917` (branch `v4.35p_prio`, rebased onto
`upstream/dev`, 35 upstream commits after the raw finder report was written). Source material:
`docs/review/2026-07-31/f3-buffers.md`. **Every file:line citation in that report was re-checked
against the current tree**; 61 of them had moved and are corrected here (see §7).

**Threat model.** Five input channels reach the parsers. Only one of them has any authentication:

| Channel                    | Entry point                                 | Authenticated?                                            | Attributable?                    |
| -------------------------- | ------------------------------------------- | --------------------------------------------------------- | -------------------------------- |
| **LoRa RF**                | `OnRxDone()` → `decodeAPRS()`               | **No**                                                    | **No** — callsign is self-stated |
| **UDP (gateway/CONF)**     | `getMeshComUDPpacket()`, `NrfETH::getUDP()` | **No**                                                    | Source IP only                   |
| **UDP (extern/JSON)**      | `getExtern()` port 1799                     | **No**                                                    | Source IP only                   |
| **BLE**                    | `readPhoneCommand()`                        | 0x10 PIN handshake exists but is not gated on all opcodes | MAC address                      |
| **Web / TCP console 2323** | `web_setup.cpp`, `checkSerialCommand()`     | Web: none; console: `s_password[15]`                      | LAN peer                         |

RF is the one that matters most: a 255-byte frame with a correct FCS costs one transmission from
anywhere in radio range, leaves no attributable trace, and is _relayed by the mesh_ — one frame can
reach every node in a region. Severity below is scaled by reachability from that channel first,
unauthenticated LAN/UDP second, owner-controlled configuration last.

Cross-references: [`08-defect-catalogue.md`](08-defect-catalogue.md) (IDs `N-01`…`N-16`,
refutations in §3), [`fable-verdict.md`](../../fable-verdict.md) (`SEC-02`…`BUG-13`),
[`docs/codequality-rules.md`](../codequality-rules.md) (rule numbers in §5). Where an ID appears in
more than one document, the mapping is given explicitly; discrepancies are flagged rather than
silently resolved.

---

## 1. Size constants

All ring sizes come from a 5-branch `#if` ladder in `src/configuration_global.h:81-118`. The
scalar buffer sizes are on lines 54-67 and 201 and are **not** board-dependent.

### 1.1 Board-independent constants

| Constant                | Value | Declared at                  | Used for                                                       |
| ----------------------- | ----- | ---------------------------- | -------------------------------------------------------------- |
| `UDP_TX_BUF_SIZE`       | 255   | `configuration_global.h:64`  | The universal "one LoRa frame" size. 23 buffers derive from it |
| `UDP_CONF_BUFF_SIZE`    | 255   | `configuration_global.h:65`  | `= UDP_TX_BUF_SIZE`; nRF52 ETH CONF record                     |
| `UDP_MSG_INDICATOR_LEN` | 4     | `configuration_global.h:67`  | `"GATE"` / `"CONF"` prefix length                              |
| `MAX_MSG_LEN_PHONE`     | 300   | `configuration_global.h:201` | BLE frame size; 9 buffers derive from it                       |
| `MAX_ZEROS`             | 6     | `configuration_global.h:120` | UDP zero-run rejection threshold                               |
| `MAX_CALL_LEN`          | 20    | `configuration_global.h:57`  | **Referenced nowhere in `src/`** — dead                        |
| `LONGNAME_MAXLEN`       | 20    | `configuration_global.h:54`  | **Referenced nowhere in `src/`** — dead                        |
| `MAX_APRS_FRAME_SIZE`   | 340   | `aprs_functions.cpp:9`       | Frame-size gate in `decodeAPRS` — **not** in the size header   |
| `MAX_EXTERN_QUEUE`      | 2     | `extudp_functions.cpp:48`    | External-UDP queue depth — **not** in the size header          |

Two constants claim to bound a callsign; the actual field is `char node_call[10]`
(`esp32/esp32_flash.h:17`, `nrf52/WisBlock-API.h:183` and `:421` — declared three times). Neither
`MAX_CALL_LEN` nor `LONGNAME_MAXLEN` is the truth, and neither is enforced anywhere. Rule 17
violation, and the exact case a `static_assert` would turn into a build error (§6.1).

### 1.2 Board-class ring sizes

| Constant         | `ENABLE_XML` | `ENABLE_SBUFFER` | ESP32-S3 ∥ RAK4630 | `ENABLE_TBEAM` | fallback   |
| ---------------- | ------------ | ---------------- | ------------------ | -------------- | ---------- |
| `MAX_MHEARD`     | 50           | 50               | **80**             | 10             | **30**     |
| `MAX_MHPATH`     | 50           | 50               | **100**            | 10             | **40**     |
| `MAX_RING`       | 20           | 20               | **20**             | 10             | **30**     |
| `MAX_DEDUP_RING` | 60           | 60               | **100**            | 10             | **70**     |
| `MAX_LOG`        | 20           | 20               | **10**             | 10             | **20**     |
| `MAX_RING_UDP`   | 20           | 20               | **20**             | 10             | **25**     |
| Declared at      | `:82-87`     | `:89-94`         | `:97-102`          | `:104-109`     | `:112-117` |

**Three of the five branches are live; two are dead.**

| Branch                                      | Live?   | Boards                                                                                                                                                                                                                        | Count |
| ------------------------------------------- | ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----- |
| `ENABLE_XML`                                | **yes** | `E22_XML-DevKitC` only (`variants/E22_XML-DevKitC/configuration.h:9`)                                                                                                                                                         | 1     |
| `ENABLE_SBUFFER`                            | **no**  | `#define`d in no `.ini`, no `configuration.h`, no source file. Byte-identical to the `ENABLE_XML` branch                                                                                                                      | 0     |
| `CONFIG_IDF_TARGET_ESP32S3 ∥ BOARD_RAK4630` | **yes** | every ESP32-S3 board + RAK4631: Heltec V3/V4, wireless-stick/tracker, E213/E290, wireless-paper, E22\_\*\_S3, T-ETH-ELITE, T3-S3, T-Beam-1W, T-Beam Supreme, T-Deck/Plus/Pro, T5-ePaper, **T-Connect-Pro**, `wiscore_rak4631` | 19    |
| `ENABLE_TBEAM`                              | **no**  | defined nowhere. Comment says "very smal version only for developer tests"                                                                                                                                                    | 0     |
| fallback                                    | **yes** | classic ESP32 (Heltec V2, TTGO T-Beam ×3, ttgo-lora32-v21, az-delivery DevKitC ×4) **and both non-RAK nRF52840 boards** (`heltec_t114`, `t_echo`)                                                                             | 11    |

Two corrections to the raw report here:

- It called `ENABLE_XML` dead. It is not — `E22_XML-DevKitC` defines it, and that variant is in
  `default_envs`. Consistent with `f1-factcheck.md:399` and `f8-completeness.md:443`.
- It placed `LilyGo_T_Connect_Pro` among the nRF52 boards in the fallback class. It is an
  **ESP32-S3** (`boards/esp32s3_flash_16MB.json` → `mcu: esp32s3`), so it is in the S3 class.

The inversion the ladder's own comment tries to avoid is real and unchanged: `heltec_t114` and
`t_echo` are 256 KB nRF52840 parts that land in the branch commented _"ESP32 original (~160 KB
DRAM) — reduced buffer sizes due to RAM constraints"_, and therefore get **larger** rings
(`MAX_RING` 30 vs 20, `MAX_RING_UDP` 25 vs 20, `MAX_LOG` 20 vs 10) than the RAK4631 they are
otherwise identical to. This is safe today only because every consumer array is declared with the
same macro. Nothing enforces that — there is no `static_assert` anywhere in the tree (§5).

---

## 2. Buffer inventory

Excludes `src/Fonts/`, `src/GFX_Root/`, `*/maps/`, `Font_*`, `img_*`. "Zeroed?" means at
definition or before every use. "Bounded?" means every write path is provably ≤ size. Sizes given
as `A/B` are S3+RAK / fallback. Line numbers are current-tree.

### 2.1 RF and packet path — untrusted, unauthenticated

| Buffer                           | Declared at                              | Size expression                      | Resolved                            | Zeroed?                                                              | Bounded?                                                                                    | Reached from |
| -------------------------------- | ---------------------------------------- | ------------------------------------ | ----------------------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- | ------------ |
| `RcvBuffer`                      | `loop_functions.cpp:378`                 | `UDP_TX_BUF_SIZE*2`                  | 510                                 | `={0}`; runtime `memset` only 255 B (`lora_functions.cpp:1214,1265`) | yes (`size` ≤ 255 by PHY)                                                                   | RF, UDP      |
| `rxPayloadCopy[2][…]`            | `lora_functions.cpp:309`                 | `[2][UDP_TX_BUF_SIZE]`               | 2×255                               | no                                                                   | **yes** — `rxSize = min(size,255)` at `:313`. **nRF52 only**; ESP32 has no equivalent clamp | RF (nRF52)   |
| `print_buff` (ACK)               | `lora_functions.cpp:207,380`             | `[30]`                               | 30                                  | no                                                                   | **no** — fixed 12-byte `memcpy`, no `size>=12` gate (**F3-15**)                             | RF           |
| `lora_tx_buffer`                 | `lora_functions.cpp:97`                  | `UDP_TX_BUF_SIZE+10`                 | 265                                 | no                                                                   | yes (`sendlng` ≤ 255)                                                                       | TX only      |
| `ringBuffer[..][..]`             | `loop_functions.cpp:385`                 | `[MAX_RING][UDP_TX_BUF_SIZE+5]`      | 20/30 × 260                         | `={0}`                                                               | yes (writes at `+2`, len ≤ 255 → 257)                                                       | RF, UDP      |
| `own_msg_id`                     | `loop_functions.cpp:381`                 | `[MAX_RING][5]`                      | 20/30 × 5                           | `={0}`                                                               | yes                                                                                         | internal     |
| `retryCount`                     | `loop_functions.cpp:391`                 | `[MAX_RING]`                         | 20/30                               | `={0}`                                                               | yes                                                                                         | internal     |
| `ringPriority`/`ringEnqueueTime` | `loop_functions.cpp:476,477`             | `[MAX_RING]`                         | 20/30                               | BSS                                                                  | yes                                                                                         | internal     |
| `ringBufferLoraRX` (dedup)       | `loop_functions.cpp:394`                 | `[MAX_DEDUP_RING][5]`                | 100/70 × 5                          | `={0}`                                                               | yes (`loraWrite` is `atomic<uint8_t>`, wrapped)                                             | RF           |
| `ringbufferRAWLoraRX`            | `loop_functions.cpp:398`                 | `[MAX_LOG][UDP_TX_BUF_SIZE+5]`       | 10/20 × 260                         | `={0}`                                                               | yes (fixed 254-byte `memcpy`, `:3019`)                                                      | RF           |
| `ringBufferUDPout`               | `loop_functions.cpp:406`                 | `[MAX_RING_UDP][UDP_TX_BUF_SIZE+20]` | 20/25 × 275                         | **no initializer** (BSS)                                             | write yes (`len+1` ≤ 256); **read overruns the row** (**F3-17**)                            | RF → UDP     |
| `BLEtoPhoneBuff`                 | `loop_functions.cpp:411`                 | `[MAX_RING][MAX_MSG_LEN_PHONE+5]`    | 20/30 × 305                         | `={0}`                                                               | write yes; **1-byte length field truncates** (**F3-12** → **F3-2**)                         | RF → BLE/web |
| `BLEComToPhoneBuff`              | `loop_functions.cpp:416`                 | `[MAX_RING][MAX_MSG_LEN_PHONE+5]`    | 20/30 × 305                         | `={0}`                                                               | **no clamp in `addBLEComToOutBuffer`** — only callers clamp (**F3-12**)                     | commands     |
| `cConcat1/2/3` (decodeAPRS)      | `aprs_functions.cpp:184,187,190`         | `[UDP_TX_BUF_SIZE]` ×3               | 3×255 stack                         | `memset` each                                                        | source/dest loops capped at 120; **payload loop uncapped** (**F3-7**)                       | RF, UDP      |
| `temp` (decodeAPRS)              | `aprs_functions.cpp:124`                 | `[11]`                               | 11                                  | no                                                                   | fixed 10-byte `memcpy` at `:419,484`; `rsize` may be < 10                                   | RF           |
| `decode_text`                    | `aprs_functions.cpp:535`                 | `[25]`                               | 25                                  | `memset`                                                             | yes (`ipt<11`)                                                                              | RF           |
| `msg_start` (encode\*)           | `aprs_functions.cpp:1000,1113,1169,1268` | `[UDP_TX_BUF_SIZE]`                  | 255 stack                           | no                                                                   | `snprintf`-bounded                                                                          | TX           |
| `incomingPacket`                 | `udp_functions.cpp:67`                   | `UDP_TX_BUF_SIZE`                    | 255                                 | BSS                                                                  | **no** — `[len]=0` with `len==255` (**F3-9** / SEC-05)                                      | UDP 1990     |
| `convBuffer`                     | `udp_functions.cpp:69`                   | `UDP_TX_BUF_SIZE+50`                 | 305                                 | `memset(…,255)` only                                                 | write yes; over-read source (**F3-17**)                                                     | UDP          |
| `incomingExtPacket`              | `extudp_functions.cpp:44`                | `UDP_TX_BUF_SIZE`                    | 255                                 | BSS                                                                  | **no** — `[len]=0` (**F3-9** / SEC-06)                                                      | UDP 1799     |
| `externQueue[].buffer`           | `extudp_functions.cpp:50`                | `[500]` ×2                           | 1000                                | no                                                                   | yes (`buflen ≤ sizeof`)                                                                     | UDP          |
| `c_json`/`c_tjson`               | `extudp_functions.cpp:348-352`           | `[500]` ×2                           | 1000 (stack on ESP32, BSS on nRF52) | `={0}` / `memset`                                                    | `serializeJson(…, json_len+1)` — sized by document (**F3-18** shape)                        | UDP          |
| `val` (extern JSON)              | `extudp_functions.cpp:225`               | `[160+1]`                            | 161 stack                           | `={0}`                                                               | yes — explicit guard at `:266` caps dst ≤ 9 and msg ≤ 150. **Safe**                         | UDP 1799     |
| `hb_buffer`/`dt_buffer`          | `udp_functions.cpp:1043,1091`            | `UDP_TX_BUF_SIZE+50`                 | 305 stack                           | no                                                                   | yes (36+255=291 ≤ 305; margin 14 B)                                                         | TX           |
| `inc_udp_buffer` (nRF ETH)       | `nrf52/nrf_eth.cpp:34`                   | `UDP_TX_BUF_SIZE+5`                  | 260                                 | `memset(…,255)`                                                      | yes                                                                                         | UDP (nRF52)  |
| `config_buf` (nRF ETH CONF)      | `nrf52/nrf_eth.cpp:509`                  | `UDP_CONF_BUFF_SIZE`                 | 255 stack                           | `={0}`                                                               | zero-fill **fixed** in `93bb68d0`; **field parse still unbounded (F3-4)**                   | UDP (nRF52)  |

### 2.2 Phone / BLE path — untrusted

| Buffer                      | Declared at                  | Size expression               | Resolved  | Zeroed?           | Bounded?                                                       |
| --------------------------- | ---------------------------- | ----------------------------- | --------- | ----------------- | -------------------------------------------------------------- |
| `conf_data` (nRF52)         | `nrf52/nrf52_ble.cpp:251`    | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | read length discarded at `:252` (**F3-21**)                    |
| `BleQueueItem.data` (ESP32) | `esp32/esp32_main.cpp:273`   | `MAX_MSG_LEN_PHONE`           | 300       | `= {}` at `:354`  | yes (`length ≤ 300` at `:356`) — but length dropped at `:2816` |
| `textbuff_phone`            | `phone_commands.cpp:22`      | `[MAX_MSG_LEN_PHONE]`         | 300       | `={0}`            | **no** — `txt_msg_len_phone = msg_len-2` underflow (BUG-07)    |
| `toPhoneBuff` (BLE)         | `phone_commands.cpp:65`      | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | yes (`blelen` ≤ 255); `blelen-1` at `:72` underflows at 0      |
| `toPhoneBuff` (web)         | `web_functions.cpp:1278`     | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | **no** — `blelen-4` underflow (**F3-2**)                       |
| `ComToPhoneBuff`            | `phone_commands.cpp:134`     | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | **no** — `blelen-1` underflow (**F3-11**)                      |
| `ssid_arr`/`pwd_arr`        | `phone_commands.cpp:559,560` | **VLA** `[len+1]`             | 1..256 ea | `={0}` (GNU ext.) | **no** (**F3-6** / SEC-03)                                     |
| `call_arr`                  | `phone_commands.cpp:399`     | **VLA** `[msg_payload_len+1]` | 1..256    | **no**            | **uninitialised at len 0** (**F3-23**)                         |
| `device_hash`/`recv_hash`   | `phone_commands.cpp:283,284` | `[32]` ×2                     | 64 stack  | no                | yes (`msg_len >= 35` gate at `:281`, but on an attacker byte)  |
| `helper_string`             | `nrf52/nrf52_ble.cpp:35`     | `[256]`                       | 256       | `={0}`            | `snprintf`-bounded                                             |
| `bleBuffer` (MHeard JSON)   | `mheard_functions.cpp:346`   | `[MAX_MSG_LEN_PHONE]`         | 300 stack | `={0}`            | `serializeJson(…, measureJson+1)` — sized by doc (**F3-18**)   |

### 2.3 Console / command path

| Buffer                                | Declared at                                   | Size expression       | Resolved    | Zeroed?              | Bounded?                                                           |
| ------------------------------------- | --------------------------------------------- | --------------------- | ----------- | -------------------- | ------------------------------------------------------------------ |
| `msg_text` (global)                   | `loop_functions.cpp:222`                      | `MAX_MSG_LEN_PHONE*2` | 600         | `={0}`               | `snprintf`-bounded; one `strcat` at `:2554` (safe, ≤35) — BND-01   |
| `msg_text` (local, `commandAction`)   | `command_functions.cpp:199`                   | `[300]`               | 300 stack   | **no**               | filled by `snprintf` at `:240`; **tail uninitialised** (**F3-13**) |
| `_owner_c`                            | `command_functions.cpp:200`                   | `[300]`               | 300 stack   | **no**               | `snprintf`-bounded; `_owner_c[-1]` write (**F3-14**)               |
| `print_buff` (cmd)                    | `command_functions.cpp:88`                    | `[350]`               | 350 BSS     | `memset` before JSON | `serializeJson` sized by `measureJson`, not `sizeof` (**F3-18**)   |
| `msg_buffer` (cmd)                    | `command_functions.cpp:90`                    | `[MAX_MSG_LEN_PHONE]` | 300 BSS     | `memset`             | yes (`json_len ≤ 298`, clamped at `:4588`)                         |
| `msg_detail`                          | `command_functions.cpp:91`                    | `[100]`               | 100 BSS     | `memset`             | yes                                                                |
| `vmsg` (`commandCheck`)               | `command_functions.cpp:115`                   | `[100]`               | 100 stack   | `strncpy` NUL-pads   | yes (longest command literal is 18 chars)                          |
| `strText`                             | `esp32_main.cpp:240` / `nrf52_main.cpp:287`   | `[600]`               | 600 BSS     | `={0}`               | **no NUL slot reserved** (**F3-19**) — ESP32 partly mitigated      |
| `msg_buffer` (`checkSerialCommand`)   | `esp32_main.cpp:4007` / `nrf52_main.cpp:2570` | `[600]`               | 600 stack   | no                   | yes (`inext > sizeof-2` break at `:4026`)                          |
| `msg_text_check` / `msg_text_checked` | `loop_functions.cpp:3249,3250`                | `[200]` ×2            | 400 stack   | `memset`             | **no** (**F3-5** / SEC-04)                                         |
| `strconcat` (beacon)                  | `loop_functions.cpp:3738`                     | `[100]`               | 100 stack   | `={0}`               | **no** — 20 × `strncat(dst, src, sizeof(dst)-1)` (**F3-25**, new)  |
| `message_text` (web)                  | `web_functions/web_setup.cpp:20`              | `[200]`               | 200 stack   | no                   | `snprintf`-bounded everywhere                                      |
| `s_password`                          | `net_console.cpp:47`                          | `[15]`                | 15 BSS      | `={0}`               | —                                                                  |
| `chalBuf`/`respBuf`                   | `net_console.cpp:146,156`                     | `[48]`,`[72]`         | 48/72 stack | `respBuf ={0}`       | yes (`strcpy`/`strcat` of literals, ≤41; `idx < 71`) — BND-01      |

### 2.4 MHeard / display

| Buffer                              | Declared at                     | Size expression                       | Resolved               | Zeroed?               | Bounded?                                                              |
| ----------------------------------- | ------------------------------- | ------------------------------------- | ---------------------- | --------------------- | --------------------------------------------------------------------- |
| `mheardBuffer`                      | `mheard_functions.cpp:24`       | `[MAX_MHEARD][60]`                    | 80/30 × 60             | `memset` at init      | yes, but copies 60 B including uninitialised stack (**F3-24**)        |
| `mheardCalls`                       | `mheard_functions.cpp:25`       | `[MAX_MHEARD][10]`                    | 80/30 × 10             | `memset`              | yes (`icsize` clamped at `:299`)                                      |
| `mheardPathCalls`                   | `loop_functions_extern.h:306`   | `[MAX_MHPATH][10]`                    | 100/40 × 10            | `memset` before write | **source over-read, and no NUL reserved** (**F3-3**)                  |
| `mheardPathBuffer1`                 | `loop_functions_extern.h:308`   | `[MAX_MHPATH][50]`                    | 100/40 × 50            | `memset` before write | **source over-read** (**F3-3**); `[49]=0` forced at `:533`            |
| `pageText`/`pageLastText`           | `loop_functions.cpp:745,752`    | `[maxdisplines][25]`, `[PAGE_MAX][…]` | 7 or 11 × 25           | `={0}`                | `strncpy` + explicit NUL — correct                                    |
| `pageTextLong2`/`pageLastTextLong2` | `loop_functions.cpp:747,754`    | `[200]`, `[PAGE_MAX][200]`            | 200 / 6-10×200         | `={0}`                | `strncpy` + explicit NUL — correct                                    |
| `loc_buf` / `nformat` (printfdeb)   | `printfdeb_functions.cpp:90,41` | `[600]`, `[300]`                      | 900 stack **per call** | `nformat` `memset`    | `vsnprintf`-bounded; **`malloc` fallback in the RX path** (**F3-10**) |

---

## 3. Findings — memory safety

Severity scale: **CRITICAL** = memory corruption from unauthenticated remote input with no
precondition beyond radio/network reach. **HIGH** = corruption or information disclosure from
unauthenticated remote input with a precondition, or corruption on a link-local channel.
**MEDIUM** = corruption reachable only from owner-controlled configuration or a bounded over-read
with a real consequence. **LOW** = correctness/robustness, no memory-safety consequence today.
**LATENT** = the code is wrong but no current caller reaches it.

Counts: 2 CRITICAL open, 3 HIGH open, 1 MEDIUM-HIGH open, 10 MEDIUM open, 5 LOW-MEDIUM open,
1 LATENT, 7 LOW (bundled as F3-24), plus 1 CRITICAL and 1 HIGH **fixed on this branch**.

---

### F3-1 — nRF52 ETH CONF zero-fill overran `config_buf[255]` by up to 251 bytes — **FIXED in `93bb68d0`**

`src/nrf52/nrf_eth.cpp:513-521` · was **CRITICAL**, unauthenticated UDP · = `N-03` in
[08 §2](08-defect-catalogue.md)

The loop counter started at 0 while the write index was biased by `packetSize - 4`, so a 255-byte
`"CONF"` datagram wrote zeros to `config_buf[251 … 505]` — 251 bytes past a 255-byte stack array,
over the saved registers and return address of `NrfETH::getUDP()`.

Fixed by starting the counter at the end of the payload:

```c
for (int i = packetSize - UDP_MSG_INDICATOR_LEN; i < UDP_CONF_BUFF_SIZE; i++)
    config_buf[i] = 0x00;
```

Verified in the current tree at `:520`. The commit's own index table (`packetSize` 5 → last index
255 before, 254 after) matches. Note the loop remains redundant — `config_buf` is `= {0}` — and was
kept deliberately so the intent survives if the initialiser is ever dropped.

**Not fixed by that commit: F3-4 below, in the same function, 12 lines further down.**

---

### F3-2 — `blelen - 4` underflow → `memcpy(dst, src, ≈SIZE_MAX)` from one RF frame — **CRITICAL, OPEN**

`src/web_functions/web_functions.cpp:1279,1296,1297` · = `N-04`, trigger = `BUG-08`

```c
uint8_t blelen = BLEtoPhoneBuff[iRead][0];                      // :1279
...
memcpy(toPhoneBuff, BLEtoPhoneBuff[iRead] + 1, blelen - 4);      // :1296
memcpy(tbuffer,     BLEtoPhoneBuff[iRead] + 1 + (blelen - 4), 4);// :1297
```

`blelen` is `uint8_t`; `blelen - 4` promotes to `int`, is negative for `blelen ∈ {0,1,2,3}`, and is
then converted to `size_t` at the `memcpy` call → `0xFFFFFFFC … 0xFFFFFFFF`. `toPhoneBuff` is a
300-byte **stack** array (`:1278`).

`blelen ∈ {0,1,2,3}` is produced by `addBLEOutBuffer` (F3-12):

```c
if (len > UDP_TX_BUF_SIZE)  len = UDP_TX_BUF_SIZE-4;   // :529 — only clamps len > 255
BLEtoPhoneBuff[toPhoneWrite][0] = len + 4;             // :548 — uint16 → uint8
```

`len ∈ [252,255]` passes the guard unchanged, `len+4 ∈ [256,259]`, stored as `0…3`.

**Concrete trigger.** Transmit one LoRa frame of 252–255 bytes with a correct FCS,
`payload_type = 0x3A` (`:`), `msg_destination_call = "*"`. `OnRxDone` →
`addBLEOutBuffer(RcvBuffer, size)` (`lora_functions.cpp:873`, also `:954`, `:1093`) →
`BLEtoPhoneBuff[w][0] = 0…3`. Any subsequent load of the web UI messages page
(`sub_content_messages`, `web_functions.cpp:1262`) then executes
`memcpy(toPhoneBuff, src, (size_t)-1)`. Guaranteed hard fault; the second `memcpy` additionally
reads from `src - 4 … src - 1`.

**Fix (two layers).** (a) In `addBLEOutBuffer`, clamp at `len > UDP_TX_BUF_SIZE - 4`. (b) In every
consumer, `if (blelen < 4) { advance; continue; }` before any arithmetic. Same treatment needed in
`sendToPhone` (`phone_commands.cpp:72`, `blelen - 1`).

---

### F3-4 — nRF52 ETH `CONF`: attacker-chosen `call_len`/`short_len` drive VLAs and OOB reads — **CRITICAL, OPEN**

`src/nrf52/nrf_eth.cpp:532-556` (raw report said `:528-556`) · unauthenticated UDP

```c
int  call_len = config_buf[1];                                  // :532  0..255, attacker
char call_arr[call_len + 1];                                    // :533  VLA
call_arr[call_len] = '\0';                                      // :534
memcpy(call_arr, config_buf + 2, call_len);                     // :535  reads config_buf[2..256]
...
if (config_buf[2 + call_len] == 0x01) {                         // :543  index up to 257
    short_len = config_buf[2 + call_len + 1];                   // :545  index up to 258
    char short_arr[short_len + 1];                              // :546  second VLA
    memcpy(short_arr, config_buf + (2 + call_len + 2), short_len); // :547 src up to +259
```

`packetSize` is in scope here and is never used to validate `call_len` or `short_len`. Same class as
SEC-03, but on the **unauthenticated UDP** path rather than the paired-BLE path.

**Concrete trigger.** One UDP datagram to port 1990: `43 4F 4E 46` (`"CONF"`) `00 FF` followed by
249 arbitrary bytes → `config_buf[1] = 0xFF` → `call_len = 255`; the shortname block then reads
`config_buf[257]`, `config_buf[258]`, and copies `config_buf[259 … 513]` — roughly 259 bytes of
stack past a 255-byte array — verbatim into `shortname`, which is written to flash and beaconed.
Also allocates up to 512 bytes of VLA on the nRF52 task stack.

**Fix.** Validate `2 + call_len + 2 + short_len <= packetSize - UDP_MSG_INDICATOR_LEN` before any
access; replace both VLAs with `char call_arr[10]` / `char short_arr[6]` matching the destination
fields. Structurally: §6.2.

---

### F3-3 — `memcpy` sized by the _destination_ from an Arduino `String` source — heap over-read rebroadcast over the air — **HIGH, OPEN**

`src/mheard_functions.cpp:527,532` (raw report said `:526,532`) · = `N-05`

```c
int ipc = mheardLine.mh_sourcepath.length() - ips;                        // :517
if (ipc > 37) ipc = 37;                                                   // :518-519 — computed, NEVER USED

memset(mheardPathCalls[ipos], 0x00, sizeof(mheardPathCalls[ipos]));       // :526
memcpy(mheardPathCalls[ipos], mheardLine.mh_sourcecallsign.c_str(),
       sizeof(mheardPathCalls[ipos]));                                    // :527 — reads 10
memset(mheardPathBuffer1[ipos], 0x00, sizeof(mheardPathBuffer1[ipos]));   // :531
memcpy(mheardPathBuffer1[ipos], mheardLine.mh_sourcepath.substring(ips).c_str(),
       sizeof(mheardPathBuffer1[ipos]));                                  // :532 — reads 50
mheardPathBuffer1[ipos][49] = 0x00;                                       // :533
```

The length is `sizeof(dest)`, never the source length. Both sources are `String`s built in
`decodeAPRS` from RF bytes (`aprs_functions.cpp:238-240`), and Arduino `String` heap-allocates
`len+1` on both cores — there is no small-string optimisation.

**Concrete trigger.** A HEY frame (`payload_type = 0x40`) with a minimal path:
`40 <id×4> <flags> "OE1A,OE1B>" 40 <payload> 00 <hw> <mod> <fcs×2>`. `mh_sourcecallsign = "OE1A"` →
a 5-byte heap block; the `memcpy` reads 10 → 5 bytes past. `mh_sourcepath.substring(5) = "OE1B"` →
a 5-byte temporary; the `memcpy` reads 50 → **45 bytes of adjacent heap** land in
`mheardPathBuffer1[ipos]`. That buffer is rendered on the display, returned by `--path`, serialised
into the BLE MHeard JSON, and used to build **outgoing HEY path payloads** → remote heap
disclosure, plus a fault if the allocation sits at the end of a heap region. Reached from
`OnRxDone` → `updateHeyPath` on every HEY from any node.

**Addition to the raw report:** `:533` forces a NUL only into `mheardPathBuffer1`. There is **no**
equivalent for `mheardPathCalls[ipos]` — the `memcpy` at `:527` overwrites all 10 bytes including
the terminator slot, so that row can be left unterminated whenever the source callsign is ≥ 10
characters. Consumers (`snprintf(buf, 11, "%s", mheardPathCalls[iset])` at
`mheard_functions.cpp:915`) then read past the row.

**Fix.** Use the already-computed bound:
`size_t n = strlen(src); if (n > sizeof(dst)-1) n = sizeof(dst)-1; memcpy(dst, src, n);` — the
preceding `memset` supplies the NUL. Same for `mheardPathCalls`.

---

### F3-5 — URL-decode loop still overruns both 200-byte stack buffers — **HIGH, OPEN** (SEC-04)

`src/loop_functions.cpp:3245-3312` (raw report said `:3004-3069`) · gate at `:3335`

Re-verified line by line. The loop at `:3267` is counted by `iu`, which is **not used in the body**;
the source cursor `ii` advances by up to 12 per iteration and the destination cursor `in` by up to 4. Neither `in` nor `ii` is bounded. The `strMsg.length() > 160` gate is at `:3335`, _after_ the
loop. `len_check` is clamped to 199 at `:3252-3253`, which bounds only the number of iterations
(≤ 200), not the cursors.

**Concrete trigger.** A message body of 16 consecutive `%F0%9F%98%80` sequences (192 characters)
followed by ~8 filler characters, delivered as BLE msg-type `0xA0` (`::<body>`), via the web
`manualcommand` form, or over the TCP console on 2323. After 16 iterations `ii = 192`, `in = 64`;
the remaining ~184 iterations each copy one byte, driving `in` to ≈248 → **writes
`msg_text_checked[200 … 247]`** (48 bytes past a 200-byte stack array) and reads
`msg_text_check[200 … 375]` (176 bytes past). Both live in `sendMessage`'s frame.

**Fix.** Drive the loop by the source index (`while (ii < len_check)`), add
`if (in >= (int)sizeof(msg_text_checked)-1) break;` inside, and move the length gate before the
decode.

---

### F3-6 — BLE `0x55` Wi-Fi record still unvalidated — **HIGH, OPEN** (SEC-03)

`src/phone_commands.cpp:554-563` (unchanged since the report)

`ssid_len = conf_data[2]` (`:554`), `pwd_len = conf_data[ssid_len + 3]` (`:555` — itself an
unchecked index up to 258 into a 300-byte buffer), guarded only by `> 0` at `:557`.
`memcpy(pwd_arr, conf_data + (4 + ssid_len), pwd_len)` (`:563`) reads from offset up to 259 for up
to 255 bytes → up to 214 bytes past `conf_data[300]`. Two attacker-sized VLAs (up to 512 bytes
combined) on the nRF52 BLE-callback stack.

**Concrete trigger.** BLE write `05 55 FF …` → `ssid_len = 0xFF`, `pwd_len = conf_data[258]`.

**Root cause is F3-21** — `readPhoneCommand` never receives the real frame length, so every check
here is against a byte the attacker chose.

---

### F3-10 — `printfdeb` format-string injection — **FIXED in `1cbcf8c9`**; two residual issues remain — **MEDIUM, OPEN**

`src/printfdeb_functions.cpp` · was **HIGH** (SEC-02)

`Serial.printf(temp)` re-parsed already-substituted RF text as a format string. Fixed at `:122`:

```c
Serial.printf("%s", temp);
```

Verified in the current tree; a `-Wformat=2` build of `heltec_wifi_lora_32_V3` now produces **zero**
format diagnostics in `src/` (measured, §6.5). The commit records a native before/after repro with
the RF payload `hi %s %s %s %x %x`.

**Still open in the same function, from the buffer angle:**

- `loc_buf[600]` (`:90`) + `nformat[300]` (`:41`) = **900 bytes of stack per call**, and `printfdeb`
  is called ~20× inside `OnRxDone`, which on nRF52 runs in the SX126x `"LORA"` FreeRTOS task at
  priority 2. Rule 20 (`STK`).
- `:103` `temp = (char*)malloc(len+1)` — a **runtime heap allocation in the packet-RX path**,
  violating MEM-01. An attacker who makes `vsnprintf` produce > 600 bytes (long source path plus
  long payload in `printBuffer_aprs`, `loop_functions.cpp:3022`) forces one heap allocation per
  received frame → fragmentation, and on allocation failure a silently dropped log line.
- `:66` `uformat[in-1]` is read when `in == 0` — a one-byte read _before_ the format string. Only
  reachable if a literal starts with `';'` (none do today), so Low on its own.

**Fix.** Drop the `malloc` path and truncate; gate the whole function behind a debug flag so it is
not on the RX hot path.

---

### F3-8 — `encodeStartAPRS` clamp forgets the 6-byte header offset → 5-byte overflow of every 255-byte encode target — **MEDIUM-HIGH, OPEN**

`src/aprs_functions.cpp:1027-1034` (file unchanged since the report)

```c
uint16_t ilng = aprsmsg.msg_source_path.length() + 1
              + aprsmsg.msg_destination_path.length() + 1;  // :1027
if (ilng >= UDP_TX_BUF_SIZE) ilng = UDP_TX_BUF_SIZE - 1;    // :1029-1030 → 254
memcpy(msg_buffer + 6, msg_start, ilng);                    // :1032 → writes [6 … 259]
return ilng + 6;                                            // :1034 → 260
```

The clamp bounds `ilng` against the buffer size but the write starts at `+6`. At `ilng == 254` the
copy writes `msg_buffer[255 … 259]` — 5 bytes past every caller that passes a 255-byte array:
`lora_functions.cpp:861,950` (`uint8_t tempRcvBuffer[UDP_TX_BUF_SIZE]`, stack, inside the RX path),
`udp_functions.cpp:306,313`, `nrf_eth.cpp:418`. `encodeAPRS` then calls
`encodePayloadAPRS(msg_buffer + inext, …)` at `:1051` — the `(inext + 10) >= UDP_TX_BUF_SIZE` clamp
at `:1059-1060` runs _after_ that call, so the payload copy also starts out of bounds.

Reaching `ilng ≥ 254` needs `src_path + dst_path ≥ 252`. A purely relayed RF frame conserves
`src+dst+payload ≤ 242`, so the RF-only path stays 6 bytes short. The **UDP-gateway path does cross
it**: `udp_functions.cpp:213-214` appends `"," + node_call`, then `checkVia` at `:311` prepends
`node_via + ","` — up to 41 additional characters. A 255-byte `GATE` datagram with a 120-character
source path and a 120-character destination path, on a node with a long `node_via`, overflows
`tempRcvBuffer[255]` at `udp_functions.cpp:306`.

**Fix.** `if (ilng + 6 >= UDP_TX_BUF_SIZE) ilng = UDP_TX_BUF_SIZE - 7;`, and move the `inext + 10`
clamp in `encodeAPRS` _above_ the `encodePayloadAPRS` call. Better: give both functions an explicit
`size_t buf_size` parameter.

---

### F3-25 — `strncat(dst, src, sizeof(dst)-1)` × 20 into a 100-byte stack buffer — **MEDIUM, OPEN — new in this document**

`src/loop_functions.cpp:3738-3758` · **not present in the raw report**

```c
char strconcat[100] = {0};                        // :3738
strcpy (strconcat, cbatt);                        // :3739  (safe, cbatt ≤ 14)
strncat(strconcat, calt,   sizeof(strconcat)-1);  // :3740
strncat(strconcat, cncnt,  sizeof(strconcat)-1);  // :3741
... 18 more identical calls ...                   // :3742-3758
```

`strncat`'s third argument is the maximum number of bytes to **append**, not the size of the
destination. Each call is therefore permitted to append 99 bytes on top of whatever is already
there, and always writes a terminator afterwards. The 20 appends can write far more than 100 bytes.

The author knew the budget — `:3760-3771` checks
`strlen(strconcat) + strlen(catxt) + strlen(cname) > 100` and clears fields — but that check runs
**after** the overflow.

**Concrete trigger** (owner configuration, no attacker needed). `PositionToAPRS()` on a node with a
BME280/BME680 and six group calls configured via `--setgrc`:

| Field      | Content                                   | Bytes   |
| ---------- | ----------------------------------------- | ------- |
| `cbatt`    | `/B=100`                                  | 6       |
| `calt`     | `/A=000161`                               | 9       |
| `cpress`   | `/P=1004.9`                               | 9       |
| `chum`     | `/H=100.0`                                | 8       |
| `ctemp`    | `/T=-10.5`                                | 8       |
| `cqfe`     | `/F=1004`                                 | 7       |
| `cqnh`     | `/Q=1005.4`                               | 9       |
| `cncnt`    | `/N99`                                    | 4       |
| `cversion` | `/V=3`                                    | 4       |
| `ctele`    | `/Y=1`                                    | 4       |
| `cgrc`     | `/R=99999;99999;99999;99999;99999;99999;` | 39      |
| **total**  |                                           | **107** |

107 bytes into a 100-byte stack array. Adding a CCS811 (`/C=2000`), BME680 gas
(`/G=12345.6`) and `ctemp2` (`/O=-10.5`) pushes it to ~132. `cgrc` alone can be 39 bytes and is
fully owner-controlled through `--setgrc`, which is itself reachable from BLE, web and the TCP
console.

`csfpegel`, `csfpegel2`, `csftemp`, `csfbatt` (`:3570-3573`) are declared, zero-initialised,
`strncat`ed — and **never written**. Dead fields contributing 0 bytes; delete them.

**Fix.** `strncat(dst, src, sizeof(dst) - strlen(dst) - 1)` at every site, or replace the whole
chain with one bounded `snprintf`. `-Wstringop-overflow` does not catch this (the lengths are
runtime values); ASan on a native harness would (§6.5).

---

### F3-9 — UDP terminator off-by-one, both sites — **MEDIUM, OPEN** (SEC-05, SEC-06)

`src/udp_functions.cpp:100,104` and `src/extudp_functions.cpp:308,314`
(raw report said `extudp:222,228` — file gained 86 lines)

```c
int len = Udp.read(incomingPacket, UDP_TX_BUF_SIZE);   // udp_functions.cpp:100 — can return 255
...
incomingPacket[len] = 0;                               // :104 — writes incomingPacket[255]
```

`incomingPacket` and `incomingExtPacket` are both exactly `UDP_TX_BUF_SIZE` = 255 bytes and both
live in BSS, so this corrupts the adjacent global on every 255-byte datagram.

**Concrete trigger.** Any 255-byte UDP datagram to port 1990 (resp. 1799). One-line fix per site:
`read(buf, UDP_TX_BUF_SIZE - 1)`.

---

### F3-11 — `sendComToPhone` text branch: `blelen - 1` with `blelen == 0` → `memcpy` of `SIZE_MAX` — **MEDIUM, OPEN**

`src/phone_commands.cpp:135,151` (unchanged)

```c
uint8_t blelen = BLEComToPhoneBuff[ComToPhoneRead][0];                    // :135
...
ComToPhoneBuff[0] = 0x40;                                                 // :150
memcpy(ComToPhoneBuff+1, BLEComToPhoneBuff[ComToPhoneRead]+1, blelen-1);  // :151
```

`BLEComToPhoneBuff[..][0] = len` is a `uint16_t → uint8_t` truncation
(`loop_functions.cpp:587`), and callers pass `json_len + 1` with `json_len` clamped to
`MAX_MSG_LEN_PHONE - 2` = 298 (`command_functions.cpp:4588-4589`, `:4638-4639`, 13 sites).
`json_len == 255` → `len == 256` → stored as **0** → `blelen - 1` = `(size_t)-1`.

**Concrete trigger.** Configure telemetry strings so the `{"TYP":"TM","PARM":…}` document
serialises to exactly 255 bytes (`--setparm` / `--setunit` / `--setformat` / `--seteqns` /
`--setvales`, 50 bytes each), then request `--tel` from the phone. Hard fault. The same 1-byte
field silently truncates every config record > 255 bytes (BUG-09).

**Fix.** Widen the framing length to 2 bytes, or `if (len > 254) return;` in
`addBLEComToOutBuffer` **and** `if (blelen < 1) { advance; return; }` in the consumer.

---

### F3-12 — 16-bit lengths written into 1-byte framing fields with no clamp — **MEDIUM, OPEN** (BUG-08, BUG-09)

`src/loop_functions.cpp:529-530,548` and `:581-588`

```c
void addBLEOutBuffer(uint8_t *buffer, uint16_t len)          // :527
{
    if (len > UDP_TX_BUF_SIZE) len = UDP_TX_BUF_SIZE-4;      // :529-530 — wrong threshold
    memcpy(BLEtoPhoneBuff[toPhoneWrite] + 1, buffer, len);   // :533
    ...
    BLEtoPhoneBuff[toPhoneWrite][0] = len + 4;               // :548 — wraps for len ∈ [252,255]
```

```c
void addBLEComToOutBuffer(uint8_t *buffer, uint16_t len)     // :576
{
    if (len > 245) printfdeb("[ERR]…BLE out-buffer to long…"); // :581-584 — warning only
    BLEComToPhoneBuff[ComToPhoneWrite][0] = len;             // :587 — truncates
    memcpy(BLEComToPhoneBuff[ComToPhoneWrite] + 1, buffer, len); // :588 — NO upper bound at all
```

The second function has no clamp and no early return; the `memcpy` writes into a
`MAX_MSG_LEN_PHONE+5` = 305-byte row with no bound on `len`. Today every caller happens to clamp to
≤ 299; one caller passing > 304 is a BSS overflow across ring slots.

This is the mechanism behind **F3-2** and **F3-11**.

**Fix.** Clamp at `len > UDP_TX_BUF_SIZE - 4` and `len > MAX_MSG_LEN_PHONE + 4` respectively, and
`return` after the warning.

---

### F3-13 — `commandAction` offset arms read past the NUL into uninitialised stack — 9 of 223 arms — **MEDIUM, OPEN**

`src/command_functions.cpp:199,240` plus the sites below

`char msg_text[300];` at `:199` has **no initialiser**. It is filled only by
`snprintf(msg_text, sizeof(msg_text), "%s", sVar.c_str())` at `:240`, which writes `strlen+1` bytes
and leaves `msg_text[strlen+1 … 299]` as stack garbage. `commandAction` recurses from its
3-argument overload, so the garbage is the previous invocation's frame — attacker-influenced.

`commandCheck(msg_text+2, cmd) == 0` guarantees only `strlen(msg_text) >= 2 + strlen(cmd)`. Any read
at an offset **greater than `2 + strlen(cmd)`** can therefore start past the NUL. (`commandCheck`
itself is safe: `strncpy` NUL-pads `vmsg[100]` at `:115` and the longest command literal is 18
characters.)

**Arm count corrected: 223, not 217.** The rebase added six (`pingcall `, `pingtime `,
`ping start`, `ping stop`, `pingmax max`, `pingmax `); all six carry their own trailing space or
are matched exactly, so none is a new violation. The nine violating arms are unchanged — all treat
the separator space as implicit:

| Check line | Command        | Safe offset ≤ | Actual read                                         | Over by | Trigger          |
| ---------- | -------------- | ------------- | --------------------------------------------------- | ------- | ---------------- |
| `:257`     | `utcoff`       | 8             | `sscanf(msg_text+9, "%f", …)` at `:259`             | 1       | `--utcoff`       |
| `:311`     | `settime`      | 9             | `String strSetTime = msg_text+10;` at `:314`        | 1       | `--settime`      |
| `:367`     | `maxv`         | 6             | `sscanf(msg_text+7, "%f", …)` at `:369`             | 1       | `--maxv`         |
| `:2331`    | `extudpip`     | 10            | `snprintf(node_extern, …, msg_text+11)` at `:2336`  | 1       | `--extudpip`     |
| `:2871`    | `softser send` | 14            | `snprintf(_owner_c, …, msg_text+15)` at `:2873`     | 1       | `--softser send` |
| `:3850`    | `setout `      | 9             | `msg_text+10` at `:3856`, `msg_text+12` at `:3873`  | 1, 3    | `--setout `      |
| `:3917`    | `setio `       | 8             | `msg_text+9` at `:3923`, `msg_text+11` at `:3936`   | 1, 3    | `--setio `       |
| `:4367`    | `setgrc`       | 8             | `snprintf(_owner_c, …"%s;", msg_text+9)` at `:4369` | 1       | `--setgrc`       |
| `:4454`    | `regex`        | 7             | `snprintf(_owner_c, …, msg_text+8)` at `:4456`      | 1       | `--regex`        |

`String strSetTime = msg_text+10;` (`:314`) is the worst: it runs `strlen` over uninitialised
stack; if no NUL exists in `msg_text[10 … 299]` the scan continues past the 300-byte array into
`_owner_c[300]` (the next frame slot) and beyond, then heap-allocates a `String` of that length.
`snprintf("%s", msg_text+N)` is bounded on its _output_ but still `strlen`s the source, so the same
over-read applies at the other seven sites.

Reachable from: BLE msg-type `0xA0` beginning with `--` (paired phone), the web `manualcommand`
parameter (`web_functions/web_setup.cpp:26`), the TCP console on 2323, and the serial console.

**Fix (minimal, Track A).** `char msg_text[300] = {0};` — one token, kills the uninitialised read
on all nine arms and makes every offset a bounded empty string. **Fix (structural):** §6.3.

---

### F3-14 — `--softser send` writes `_owner_c[-1]` — **MEDIUM, OPEN**

`src/command_functions.cpp:2873-2875` (raw report said `:2869-2871`)

```c
snprintf(_owner_c, sizeof(_owner_c), "%s", msg_text+15);   // :2873
if(_owner_c[strlen(_owner_c)-1] == 0x0a)                   // :2874
    _owner_c[strlen(_owner_c)-1] = 0x00;                   // :2875
```

No empty check. When `_owner_c` is empty, `strlen()-1` is `(size_t)-1`; `_owner_c[SIZE_MAX]` wraps
to `_owner_c - 1` — a read, and if that byte happens to be `0x0A`, a **write** one byte before a
300-byte stack array (adjacent to `msg_text[300]` in the same frame).

**Concrete trigger.** `--softser send` — exactly 14 characters, no argument — from BLE, web, or
console. Combined with F3-13 the source is uninitialised, so an immediate NUL is a coin flip rather
than a certainty, which makes the bug intermittent rather than absent.

**Fix.** `size_t n = strlen(_owner_c); if (n > 0 && _owner_c[n-1] == 0x0A) _owner_c[n-1] = 0;`

---

### F3-15 — `handleACK` copies 12 bytes with no minimum-length gate — **MEDIUM, OPEN** (BUG-10)

`src/lora_functions.cpp:202-214` (unchanged)

The only gate is `payload[0] != MSG_TYPE_ACK` at `:204`. `memcpy(print_buff, payload, 12)` at `:214`
then runs on a frame that may be 1 byte long. On nRF52 `payload` points into the static
`rxPayloadCopy[2][255]`, so bytes past `size` are the _previous_ packet's; on ESP32 it points into
the RadioLib buffer. `msg_id` is built from those stale bytes at `:220` and can match `checkOwnTx`
→ `findAndStopRingSlot` cancels an unrelated retransmission.

**Concrete trigger.** A 1-byte LoRa frame `41`. One-line fix: `if (size < 12) return false;`.

See also **T3-1** — the same frame is passed into an `int8_t size` parameter.

---

### F3-19 — `strText[600]` can be filled completely, leaving `strlen` to run past the array — **MEDIUM on nRF52, LOW on ESP32 — partially mitigated upstream**

`src/esp32/esp32_main.cpp:3948-3951,3971-3974,3979` (raw report said `:3882-3885,3904-3907,3913`)
`src/nrf52/nrf52_main.cpp:2546-2549,2554` (raw report said `:2525-2528,2533`)

```c
strText[iTxtPos] = rd;                     // write first
if(iTxtPos < (int)sizeof(strText) - 1)     // bound second
    iTxtPos++;
...
iTxtLen = strlen(strText);
```

At `iTxtPos == 599` the write lands on the last slot and `iTxtPos` stops there, so all 600 bytes can
be non-NUL and the `strlen` walks into adjacent BSS until it finds a zero.

**The rebase changed this, on ESP32 only.** `esp32_main.cpp:3982-3990` now adds:

```c
// Self-healing: normally every stored byte is non-NUL, so strlen == iTxtPos.
if(iTxtLen != iTxtPos) { memset(strText, 0x00, sizeof(strText)); iTxtPos = 0; return; }
```

That does **not** remove the out-of-bounds `strlen` — it still reads past the array before the
comparison — but it does catch the mismatch and discard the buffer, so the downstream
`strText[iTxtLen-1]` accesses at `:3997` are no longer reachable with an out-of-range index.
Residual: an OOB read of a few BSS bytes. **`nrf52_main.cpp` has no such guard** (`:2554` goes
straight from `strlen` to `strText[iTxtLen-1]` at `:2560`) — a fresh instance of the `DRY-22`
divergence, introduced by the rebase.

**Concrete trigger.** 600+ bytes with no `\n`/`\r`. On ESP32 via the TCP console (port 2323) or
serial; on nRF52 **serial only** — all three nRF52 variants define `-D DISABLE_NET_CONSOLE`
(`variants/{wiscore_rak4631,heltec_t114,t_echo}/platformio.ini`).

Note the write-bound at `esp32_main.cpp:3972` compares `int` against `size_t` while the sibling at
`:3949` has the `(int)` cast — a `-Wsign-compare` violation and the DRY-22 drift, see **T3-5**.

**Fix.** Bound before writing: `if (iTxtPos < (int)sizeof(strText)-1) strText[iTxtPos++] = rd;`
Apply to both platforms.

---

### F3-20 — `iVar` and 42 unchecked `sscanf` calls write straight into persisted settings — **MEDIUM, OPEN**

`src/command_functions.cpp:201-203` plus 42 `sscanf` sites (raw report said 40)

```c
double dVar=0.0;   // :201
int    iVar;       // :202  — UNINITIALISED
float  fVar=0.0;   // :203
```

`sscanf` leaves its output untouched on a failed match. Twelve sites write **directly into the
persisted settings struct**:

| Line    | Target                             | Consequence of a missing/non-numeric argument |
| ------- | ---------------------------------- | --------------------------------------------- |
| `:259`  | `meshcom_settings.node_utcoff`     | stale value silently kept                     |
| `:369`  | `meshcom_settings.node_maxv`       | stale                                         |
| `:392`  | `meshcom_settings.node_postime`    | stale                                         |
| `:995`  | `meshcom_settings.node_button_pin` | **becomes a GPIO pin number**                 |
| `:1026` | `meshcom_settings.node_analog_pin` | **becomes a GPIO pin number**                 |
| `:1975` | `meshcom_settings.node_owgpio`     | **becomes a GPIO pin number**                 |
| `:2930` | `meshcom_settings.node_ss_baud`    | stale                                         |
| `:2939` | `meshcom_settings.node_ss_rx_pin`  | **becomes a GPIO pin number**                 |
| `:2948` | `meshcom_settings.node_ss_tx_pin`  | **becomes a GPIO pin number**                 |
| `:3342` | `meshcom_settings.node_pingtime`   | stale                                         |
| `:3386` | `meshcom_settings.node_pingmax`    | stale                                         |
| `:4550` | `meshcom_settings.node_parm_time`  | stale                                         |

Only three arms (`--setout`, `--setio`, `--rotate`) pre-seed a sentinel (`iVar = 99` at `:3852`,
`:3919`) and range-check afterwards. **None of the 42 `sscanf` calls in this file checks the return
value.** Tree-wide, 3 of 81 `sscanf` calls do (`net_console.cpp:80`,
`web_functions/web_setup.cpp:579,594`) — see §5.

**Fix.** `if (sscanf(...) != 1) { reply("bad argument"); return; }` at every site, plus a range check
before assignment (rule 3: "Integer parameters: check range before cast"). Structurally: §6.3.

---

### F3-21 — `readPhoneCommand` has no length parameter; every length check trusts an attacker byte — **MEDIUM (structural root of F3-6 and BUG-07), OPEN**

`src/phone_commands.h:4`, `src/phone_commands.cpp:208,241,243`

```c
void readPhoneCommand(uint8_t conf_data[MAX_MSG_LEN_PHONE]);   // decays to uint8_t*
...
uint8_t msg_len         = conf_data[0];   // :241 — attacker-supplied
uint8_t msg_payload_len = conf_data[2];   // :243 — attacker-supplied
```

Both callers know the real length and throw it away:

- `esp32_main.cpp:355-358` reads `pCharacteristic->getLength()`, validates
  `item.length <= MAX_MSG_LEN_PHONE`, queues it — then `:2816` calls
  `readPhoneCommand(bleItem.data)`, dropping `item.length`.
- `nrf52_ble.cpp:252` calls `g_ble_uart.read(conf_data, MAX_MSG_LEN_PHONE)` and **discards the
  return value**.

Consequence: opcode `0x55` (F3-6), `0xA0` (`txt_msg_len_phone = msg_len - 2` at `:526` underflows
for `msg_len ∈ {0,1}` → 254/255, BUG-07), `0x10` (`msg_len >= 35` at `:281` gates
`memcpy(recv_hash, conf_data+4, 32)` at `:286`), and `0x50` all validate against a byte the
attacker chose.

**Fix.** `void readPhoneCommand(const uint8_t *conf_data, size_t len)` and gate every case on `len`,
not on `conf_data[0]`. Structurally: §6.2.

---

### F3-16 — UDP zero-scan reads one past on odd `packetSize` — **LOW-MEDIUM, OPEN** (BUG-12)

`src/udp_functions.cpp:121-123`, `src/nrf52/nrf_eth.cpp:233-235` (both unchanged)

```c
for (int i = 0; i < packetSize; i += 2)
    if (inc_udp_buffer[i] == 0x00 && inc_udp_buffer[i + 1] == 0x00)
```

With odd `packetSize` the final iteration reads `buf[packetSize]`. In `udp_functions.cpp` the array
is exactly 255 bytes, so a 255-byte datagram is a true OOB read. In `nrf_eth.cpp` the array is
`UDP_TX_BUF_SIZE+5` = 260, so it stays in bounds. The `DRY-21` divergence means the fix has to be
applied twice.

---

### F3-17 — `sendMeshComUDP` reads 17 bytes past the ring row — **LOW-MEDIUM, OPEN**

`src/udp_functions.cpp:423,454`, `src/nrf52/nrf52_main.cpp:2669` (raw report said `nrf52_main:2648`)

```c
uint16_t msg_len = (uint16_t)ringBufferUDPout[udpRead][0];              // :423 — up to 255
memcpy(convBuffer, ringBufferUDPout[udpRead] + 1 + 36, msg_len);        // :454 — 37 + 255 = 292
```

The row is `UDP_TX_BUF_SIZE+20` = **275** bytes. With `msg_len = 255` the copy reads 17 bytes past
the row; when `udpRead == MAX_RING_UDP-1` that is past the entire array. The result is fed straight
into `decodeAPRS` at `:461`. Triggered by any gateway forwarding a max-size frame.

**Fix.** `size_t n = min<size_t>(msg_len, sizeof(ringBufferUDPout[0]) - 37);` — and note the magic
`36` is the `"DATA"+GW_ID+call+ver+rssi+snr+mod` header length, hard-coded in three places.

---

### F3-18 — `serializeJson` sized by `measureJson(doc)` instead of `sizeof(dest)` — **LOW-MEDIUM, OPEN**

15 sites (raw report said 14): `command_functions.cpp:4585,4635,4745,4835,5078,5106,5148,5176,5253,5337,5374,5411,5438`
and `mheard_functions.cpp:348,651`. The report cited `mheard:653` and missed `mheard:348`.

```c
char print_buff[350];                                     // command_functions.cpp:88
memset(print_buff, 0, sizeof(print_buff));                // :4583
serializeJson(tmdoc, print_buff, measureJson(tmdoc));     // :4585 — bound is the *required* size
```

The bound is derived from the document, not the buffer, so it provides no protection: if the
document is larger than 350, `serializeJson` writes 350+ bytes into a 350-byte BSS array. The
telemetry document carries five 50-byte user strings (`node_parm`/`unit`/`format`/`eqns`/`values`);
with JSON escaping (`"` → `\"`) each can double, and the escaped worst case exceeds 350. Passing
`measureJson(doc)` also means the buffer is **not NUL-terminated** when the document exactly fits —
the subsequent `strlen(print_buff)` at `:4587` then depends on the preceding `memset`.
`mheard_functions.cpp:348,651` have the same shape against a 300-byte stack array.

`extudp_functions.cpp:440,464,485,525` use `json_len + 1` instead — same class, different constant.

**Fix.** Always `serializeJson(doc, buf, sizeof(buf))` and check the returned byte count.

---

### F3-22 — `strncpy` without room for the NUL in the VIA path — **LOW-MEDIUM, OPEN**

`src/via_functions.cpp:118,130,140` (raw report said `:107-129,119`)

```c
char cMH[10];                                        // :118
memset(cMH, 0x00, sizeof(cMH));                      // :129
strncpy(cMH, mheardCalls[iset], sizeof(cMH));        // :130 — n == sizeof(dest)
...
aprsmsg.msg_destination_path = cMH;                  // :140 — strlen over a possibly unterminated array
```

`mheardCalls[MAX_MHEARD][10]` is filled by a bounded `memcpy` that reserves the terminator
(`mheard_functions.cpp:299-301`), so `cMH` is terminated by luck rather than construction.
`sizeof(cMH)-1` is the correct bound. The other `strncpy` sites in `loop_functions.cpp` all use
`sizeof-1` **and** write the terminator explicitly — those are the pattern to copy.
`src/web_functions/web_functions.cpp:1836` and `src/t-deck-pro/ui_deckpro_port.cpp:223`
(`strncpy(list[i].name, WiFi.SSID(i).c_str(), 16)`) have the same unterminated shape.

---

### F3-23 — BLE `0x50` (set callsign) reads an uninitialised VLA when the payload length is 0 — **LOW-MEDIUM, OPEN**

`src/phone_commands.cpp:399-408` (unchanged)

```c
char call_arr[msg_payload_len + 1];              // :399 — VLA, NO initialiser
for (int i = 0; i < msg_payload_len; i++) {      // :401
    call_arr[i]   = conf_data[i + 3];            // :403
    call_arr[i+1] = 0x00;                        // :404
}
String sVar = call_arr;                          // :408
```

With `msg_payload_len == 0` the loop never runs and `call_arr[0]` is uninitialised stack;
`String sVar = call_arr` then `strlen`s it.

**Concrete trigger.** BLE write `03 50 00` → the node's callsign is set to whatever stack bytes
follow, and `snprintf(node_call, 10, "%s", …)` persists it to flash.

**Fix.** `char call_arr[MAX_CALL_LEN+1] = {0};` plus
`if (msg_payload_len == 0 || msg_payload_len > sizeof(call_arr)-1) break;`

---

### F3-7 — `decodeAPRS` payload loop is unbounded relative to `cConcat1[255]`; `MAX_APRS_FRAME_SIZE` permits 340 — **LATENT (confirmed by 08 §3)**

`src/aprs_functions.cpp:9,122,149,184,364-378` (file unchanged since the report)

```c
#define MAX_APRS_FRAME_SIZE 340                                          // :9
uint16_t decodeAPRS(uint8_t RcvBuffer[UDP_TX_BUF_SIZE], uint16_t rsize, …)  // :122 — param says 255
    if (rsize > MAX_APRS_FRAME_SIZE) return 0x00;                        // :149 — gate says 340
    char cConcat1[UDP_TX_BUF_SIZE];                                      // :184 — buffer is 255
    for (ib = inext; ib < rsize; ib++) { … cConcat1[iConcat1++] = …; }   // :364-378 — no cap
```

The source- and destination-path loops are capped at 120 iterations each; the **payload loop is not
capped at all**. With `rsize = 340` and a minimal header (`inext ≈ 13`) the loop would write
`cConcat1[0 … 326]` — a 72-byte stack overflow.

**08 §3 records this as REFUTED-as-exploitable / confirmed-as-latent: all six callers traced, none
can supply more than 255 bytes today.** That adjudication stands and is carried forward here
unchanged. The reasons it is still worth fixing:

- the function's own contract is self-contradictory (parameter type says 255, gate says 340);
- `sendExtern` declares `uint8_t buffer[500]` and passes `buflen` straight through
  (`extudp_functions.cpp:321,337`), and the `externQueue` entry buffer is 500 bytes;
- one future caller that forwards a > 271-byte frame turns it into a remote stack smash;
- the FCS is checked at `:414`, i.e. **after** this loop (rule 15, `D4` deferred).

**Fix.** Cap the payload loop (`&& iConcat1 < (int)sizeof(cConcat1)-1`), make the gate consistent
(`rsize > UDP_TX_BUF_SIZE → reject`), and add
`static_assert(MAX_APRS_FRAME_SIZE <= sizeof(cConcat1))` — which fails today, which is the point.

---

### F3-24 — minor and dead-code buffer issues — **LOW**

| #   | Site                                                                                                  | Issue                                                                                                                                                                                                                                                                                             |
| --- | ----------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| a   | `loop_functions.cpp:471`                                                                              | `printfdeb("… RCV:%s\n", size, RcvBuffer+6)` — `%s` on a buffer that `memcpy(RcvBuffer, payload, size)` (`lora_functions.cpp:407`) did **not** NUL-terminate. In-bounds today only because bytes 255-509 of `RcvBuffer` are never written; it leaks the tail of the _previous_ frame into the log |
| b   | `mheard_functions.cpp:323-326`, `:451-454`                                                            | `char cBuffer[60]; snprintf(...); memcpy(mheardBuffer[ipos], cBuffer, sizeof(cBuffer));` — copies the uninitialised tail after the NUL into BSS, and `:174` writes the whole array to SD → stack contents persisted to storage                                                                    |
| c   | `loop_functions.cpp:3357-3358`                                                                        | BUG-11: `char cnewMsg[10]` for a `{mcp}` reformat whose minimum output is 13 bytes → silent truncation of the command **and** the password                                                                                                                                                        |
| d   | `aprs_functions.cpp:397-403`                                                                          | BUG-13: the trailer/FCS bytes are read without checking `inext+3 < rsize`; `bPayloadEndOk` only proves a NUL exists, not that 4 bytes follow                                                                                                                                                      |
| e   | `loop_functions.cpp:2117-2118`                                                                        | `memcpy(cpasswd, cset+12, 5)` into `char cpasswd[6]` leaves `cpasswd[5]` uninitialised; only saved by the `%-5.5s` precision at `:2162`                                                                                                                                                           |
| f   | `Displays/BaseDisplay/SD.cpp:368`                                                                     | `strcpy(&filename[8], ".bmp", sizeof(4));` — a three-argument `strcpy`, i.e. code that cannot compile. Confirmed dead: `platformio.ini:171` (`[esp32] src_filter`) and the `[nrf52]` filter both exclude `-<Displays/*>`. Delete it                                                               |
| g   | `net_console.cpp:147,149`; `loop_functions.cpp:2554,3739`; `esp32_main.cpp:819`; `nrf52_main.cpp:578` | `strcpy`/`strcat` on bounded literals — safe today, plain BND-01 violations. `loop_functions.cpp:3739` is the entry point of **F3-25**                                                                                                                                                            |

---

## 4. Findings — type safety

### T3-1 — `printBuffer_ack(char*, uint8_t[…], int8_t size)`: a 16-bit length narrowed to a signed byte — **MEDIUM**

`src/loop_functions.h:46`, body at `src/loop_functions.cpp:3029` (raw report said `:2960`), call at
`src/lora_functions.cpp:211`

```c
void printBuffer_ack(char *msgSource, uint8_t payload[UDP_TX_BUF_SIZE+10], int8_t size);
```

`handleACK` passes `uint16_t size` into an `int8_t` parameter. Any frame ≥ 128 bytes arrives as a
**negative** length. The body only tests `size == 7` (`:3031`); every other value — including `-1`
from a 255-byte frame — takes the else branch that unconditionally reads `payload[0 … 11]`, on a
frame that may be 1 byte long (F3-15). Rule 11.

**Fix.** `uint16_t size`, and gate the 12-byte branch on `size >= 12`.

### T3-2 — `uint8_t(sizeof(RcvBuffer))` truncates 510 → 254 — **LOW**

`src/nrf52/nrf52_main.cpp:473` (raw report said `:469`)

```c
for (int i = 0; i < uint8_t(sizeof(RcvBuffer)); i++)   // uint8_t(510) == 254
    RcvBuffer[i] = 0x00;
```

The comment says "clear the buffers"; it clears 254 of 510 bytes. Benign at boot (BSS is already
zero), but this is exactly the `size_t → uint8_t` idiom rule 11 forbids, one line away from the RF
receive buffer.

### T3-3 — 16-bit lengths stored in 1-byte framing fields — **MEDIUM**

`loop_functions.cpp:548` (`= len + 4`), `:587` (`= len`); `phone_commands.cpp:92,160`
(`blelen = blelen + 2` on a `uint8_t`); `nrf52/nrf_eth.cpp:640` (`ringBuffer[iWrite][0] =
rx_buf_size`, raw report said `:632`). This is the mechanism behind F3-2, F3-11 and F3-12.
`blelen = blelen + 2` additionally wraps 254/255 → 0/1 and is then passed as the BLE write length
at `:93` and `:95`.

### T3-4 — `uint8_t` length arithmetic that can go negative — **MEDIUM**

| Site                                        | Expression                         | Finding |
| ------------------------------------------- | ---------------------------------- | ------- |
| `phone_commands.cpp:526`                    | `txt_msg_len_phone = msg_len - 2`  | BUG-07  |
| `phone_commands.cpp:72,140,151`             | `blelen - 1`                       | F3-11   |
| `web_functions/web_functions.cpp:1296,1297` | `blelen - 4`                       | F3-2    |
| `command_functions.cpp:2874,2875`           | `strlen(_owner_c) - 1` on `size_t` | F3-14   |

All four follow one pattern: an unsigned byte minus a constant, promoted to `int`, then implicitly
converted to `size_t` at the `memcpy` call — the one place where the sign is silently lost.

### T3-5 — signed/unsigned comparisons on length checks — **MEDIUM**

| Site                        | Expression                                 | Note                                                                                                                                                                       |
| --------------------------- | ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `esp32/esp32_main.cpp:3972` | `if(iTxtPos < sizeof(strText) - 1)`        | `int` vs `size_t`. The sibling at `:3949` **and** the nRF52 twin at `nrf52_main.cpp:2547` both have the `(int)` cast — the fix was applied to two of three copies (DRY-22) |
| `esp32/esp32_main.cpp:4026` | `if(inext > sizeof(msg_buffer)-2)`         | `int` vs `size_t`                                                                                                                                                          |
| `loop_functions.cpp:3252`   | `if(len_check > sizeof(msg_text_check)-1)` | `int len` vs `size_t`; a negative `len` converts to a huge unsigned and _accidentally_ takes the clamp branch. Correct by luck                                             |

`-Wsign-compare` is implied by `-Wall`, which **is** on for the 23 ESP32 variants (via `[esp32]`,
`platformio.ini:190`) — yet these produce no diagnostic, because `-Wsign-compare` only fires on
`<`/`>` between a signed and an unsigned _of the same rank_; here the `int` is promoted. The flag
that catches them is `-Wsign-conversion` / `-Wconversion` (§6.5). The nRF52 targets have **no**
`-Wall` at all.

### T3-6 — `char` vs `uint8_t` on payload bytes — **MEDIUM**

`decodeAPRS` stores RF bytes as `char` (`aprs_functions.cpp:208,225,232,306,318,375`) on targets
where `char` is signed. Bytes ≥ 0x80 become negative and flow into `String`, `is_equ` and `Regexp`.
The URL decoder (`loop_functions.cpp:3290,3295`) compares `msg_text_check[ii+is] >= 'A'` on a signed
`char`, so any byte ≥ 0x80 takes the `- '0'` branch and produces a large negative `ib` that is then
stored into `msg_text_checked[in]`. Not a memory-safety bug on its own, but it makes the decode
result unpredictable for exactly the inputs an attacker controls.

### T3-7 — explicit truncating casts on lengths — **LOW**

`src/nrf52/nrf_eth.cpp:279` — `sendExtern(true, (char*)"udp", RcvBuffer, (uint8_t)lora_tx_msg_len,
0, 0)`. `lora_tx_msg_len` is `uint16_t` and the parameter is `uint16_t`; the cast can only lose
data. Harmless today (values ≤ 255) but it hides exactly the class of bug in T3-3.

### T3-8 — `int` ring indices shared across tasks — **MEDIUM** (overlaps CONC-15/16)

`udpWrite`/`udpRead` (`loop_functions_extern.h:191,192`), `toPhoneWrite`/`toPhoneRead` (`:198,199`),
`ComToPhoneWrite`/`ComToPhoneRead` (`:203,204`) are plain `int`, while `iWrite`/`iRead` (`:178,179`)
and `loraWrite` (`:207`) were converted to `std::atomic<uint8_t>`. From the buffer angle the risk is
that a torn or crossed index makes `BLEtoPhoneBuff[toPhoneRead]` or `ringBufferUDPout[udpRead]` read
a slot the writer is mid-`memcpy` into — which is how F3-2's `blelen` can be observed as a
partially-written value **even after** the length-byte fix.

### T3-9 — integer overflow in size arithmetic — **LOW (no site found), OPEN as a class**

Searched for `a + b > MAX` patterns on attacker-controlled operands (rule 10). None of the size
computations in the RF/UDP/BLE paths can wrap a `size_t` today, because every operand is bounded by
a `uint8_t` or `uint16_t` before the addition. The three places where an addition is performed on an
unvalidated attacker byte — `nrf_eth.cpp:543,545,554` (`2 + call_len`, `2 + call_len + 1`,
`2 + call_len + short_len + 2`) — cannot wrap `int`, but _do_ index out of bounds (F3-4). The
mitigation in §6.2 (`has(off, len)` written as `len <= n - off` after `off <= n`) closes the class
by construction rather than by case analysis.

---

## 5. Rule violations

Mapped to `docs/codequality-rules.md`. Counts measured on the current tree, 2026-07-31.

**Measurement scope.** "core" = 120 files under `src/` excluding `Fonts/`, `GFX_Root/`,
`Platforms/`, `Displays/`, `t-deck*/`, `t5-epaper/`, `safeboot/`, `lvgl`, `assets.h`. "all" = 230
files, excluding only `Fonts/`, `GFX_Root/`, `*/maps/`, `Font_*`, `img_*` (the raw report's scope).

| Metric                                           | core   | all    | Raw report said                                              |
| ------------------------------------------------ | ------ | ------ | ------------------------------------------------------------ |
| `snprintf` calls                                 | 521    | 695    | ~400                                                         |
| … with a **numeric literal** bound, not `sizeof` | **27** | **61** | 50                                                           |
| … whose **return value is checked**              | **3**  | **3**  | **0**                                                        |
| `sscanf` calls                                   | 70     | 81     | 40 (in `commandAction` only: 42)                             |
| … whose return value is checked                  | **3**  | **3**  | 0                                                            |
| `memcpy` calls                                   | 150    | 155    | 74 with a non-constant length                                |
| `strcpy`                                         | 5      | 7      | —                                                            |
| `strcat`                                         | 3      | 3      | —                                                            |
| bare `sprintf`                                   | **0**  | 5      | listed as violated                                           |
| `strncpy`                                        | 20     | 26     | —                                                            |
| `static_assert` in **compiled** code             | **0**  | **0**  | 0 (2 hits, both in `src/code_review/code-audit-20260508.md`) |

Three corrections to the raw report's counts:

- **`snprintf` return values _are_ checked at 3 sites** — `esp32/at_cmd.h:30`, `nrf52/at_cmd.h:30`,
  `nrf52/WisBlock-API.h:692`, all inside the `AT_PRINTF` macro, all using `sizeof(buff)`. The nRF52
  one came from commit `5ec13237` ("bounded AT_PRINTF snprintf (D3)"). The claim "not one" is now
  false; the claim "essentially none" stands (3 of 695).
- **`sscanf` return values are checked at 3 sites** — `net_console.cpp:80`,
  `web_functions/web_setup.cpp:579,594`. None of them is in `commandAction`.
- **Bare `sprintf` does not exist in the core**; the 5 hits are all in `t-deck-pro/ui_deckpro.cpp`
  and the dead `Displays/BaseDisplay/SD.cpp`.

| Rule           | Requirement                                                           | Status                            | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| -------------- | --------------------------------------------------------------------- | --------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1** MEM-01   | No `malloc`/`new` after init; none in packet paths                    | **VIOLATED**                      | `printfdeb_functions.cpp:103` mallocs inside the `OnRxDone` logging path; `String` allocation throughout `decodeAPRS`/`aprsMessage` (7 `String`s per received frame, `aprs_structures.h:20-26`) and `mheardLine` (7 more)                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| **1** MEM-01   | Never Arduino `String` in hot paths                                   | **VIOLATED**                      | `struct aprsMessage` and `struct mheardLine` are constructed per received frame in `OnRxDone`; `decodeAPRS` performs ≥ 6 heap assignments, `checkVia` 2 more. Nothing checks for allocation failure — a failed `String` assignment yields an **empty** string, so under heap exhaustion the node relays a frame with an empty source path and writes an empty callsign into `mheardCalls`, silently                                                                                                                                                                                                                                                                                                 |
| **1**          | All buffer sizes as `#define` in a single header                      | **PARTIAL**                       | `configuration_global.h` holds the ring sizes; literals `[200]`, `[300]`, `[350]`, `[500]`, `[600]`, `[100]`, `[60]`, `[30]` are hard-coded at ~120 declaration sites. `MAX_APRS_FRAME_SIZE` lives in `aprs_functions.cpp:9`, `MAX_EXTERN_QUEUE` in `extudp_functions.cpp:48`. `MAX_CALL_LEN`/`LONGNAME_MAXLEN` are defined and used nowhere while the real field is `node_call[10]`                                                                                                                                                                                                                                                                                                                |
| **2** BND-01   | Never `sprintf`/`strcpy`/`strcat`/`gets`                              | **VIOLATED**                      | 8 core sites: `net_console.cpp:147,149`; `loop_functions.cpp:2554,3739`; `esp32_main.cpp:819`; `nrf52_main.cpp:578`; plus dead `Displays/BaseDisplay/SD.cpp:368` and 4 in `t-deck-pro/`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| **2** BND-02   | Always `snprintf` with the **correct** size                           | **VIOLATED**                      | 27 core sites with a literal bound (61 tree-wide); 15 `serializeJson` sized by `measureJson` (F3-18)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **2** BND-03   | `strncpy`/`strncat` with bounds                                       | **VIOLATED**                      | `via_functions.cpp:130`, `web_functions.cpp:1836`, `t-deck-pro/ui_deckpro_port.cpp:223` pass `sizeof(dest)` with no room for the NUL (F3-22); **20 × `strncat(dst, src, sizeof(dst)-1)`** at `loop_functions.cpp:3740-3758` misuse the append bound entirely (F3-25)                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **2** BND-04   | All `memcpy`: validate length BEFORE copying                          | **VIOLATED**                      | 150 core `memcpy` calls; unvalidated: F3-2, F3-3, F3-4, F3-6, F3-11, F3-15, F3-17, plus `loop_functions.cpp:588` and `nrf_eth.cpp:642`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **2** BND-05   | Bounds-check every array index                                        | **VIOLATED**                      | F3-4 (`config_buf[2+call_len]`), F3-6 (`conf_data[ssid_len+3]`), F3-13 (offset past NUL), N-06 (`node_mcp17t[t_io]` from a web parameter)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| **2**          | `static_assert` on all protocol struct sizes                          | **VIOLATED — zero**               | `grep -rn 'static_assert' src` → 2 hits, both inside `src/code_review/code-audit-20260508.md`. No `static_assert` in any compiled file. The 2026-05-08 audit already raised this and it is still open                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| **2**          | Check `snprintf` return for truncation                                | **VIOLATED — 3 of 695**           | Only the three `AT_PRINTF` macro sites                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **3**          | Radio RX: validate packet length before buffer copy                   | **PARTIAL**                       | nRF52 clamps (`lora_functions.cpp:313`); ESP32 (`esp32_main.cpp:3840`) passes the driver length straight to `memcpy(RcvBuffer, payload, size)` (`lora_functions.cpp:407`) with no clamp — safe only because `RcvBuffer` is 2× the PHY maximum                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **3**          | Network messages: validate header length fields                       | **VIOLATED**                      | F3-4 (UDP `CONF`), F3-17                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| **3**          | BLE writes: validate data length before parsing                       | **VIOLATED**                      | F3-21 — the real length never reaches the parser                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| **3**          | Integer parameters: check range before cast                           | **VIOLATED**                      | F3-20 — 12 settings fields, 5 of them GPIO pin numbers                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **10** COMP-01 | `-Wall -Wextra -Werror`                                               | **PARTIAL / VIOLATED**            | `-Wall -Wextra` **is** on for all 23 ESP32 firmware variants via `[esp32] build_flags` (`platformio.ini:190`) — the raw report mis-attributed line 158 (pre-rebase) to a safeboot env. It is **not** on for **9 of 34** environments: the 3 nRF52 variants (`[nrf52]` block has no warning flags) **and 6 ESP32 variants that override `build_flags` without inheriting `${esp32.build_flags}`** — `t5_epaper`, `t_deck_pro`, `vision-master-e213`, `vision-master-e213-preview`, `vision-master-e290`, `wireless-paper`. Re-derived with `pio project config --json-output`. `-Werror` appears nowhere; `-Wconversion`/`-Wformat=2` nowhere. Measured baseline: **8 warnings**, 3 unique in `src/` |
| **10**         | No implicit signed/unsigned conversion in size calcs                  | **VIOLATED**                      | T3-5                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| **10**         | Use `size_t` for all sizes/lengths                                    | **VIOLATED**                      | T3-1, T3-3, T3-4                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| **10**         | Size arithmetic: check overflow before use                            | **PARTIAL**                       | T3-9 — no wrapping site found, but the pattern is checked nowhere                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| **11**         | No implicit narrowing                                                 | **VIOLATED**                      | T3-1, T3-2, T3-3 — measured: **236 unique `-Wconversion` sites in `src/`**                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| **11**         | Protocol structs `packed` or field-by-field                           | **OK**                            | Decode is field-by-field; but there is no `static_assert` on the resulting frame layout                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| **11**         | Pointer arithmetic: validate `offset < len`                           | **VIOLATED**                      | F3-4, F3-13, `aprs_functions.cpp:397-403`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| **13**         | Log macros always use literal format strings                          | **FIXED**                         | `printfdeb_functions.cpp:122` — `1cbcf8c9`. `-Wformat=2` now clean in `src/`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| **15**         | Checksums verified BEFORE any field parsing                           | **VIOLATED (known, D4 deferred)** | FCS is checked at `aprs_functions.cpp:414`, after the source/destination/payload parse loops — i.e. after the loop in F3-7                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| **15**         | Min and max frame sizes validated before access                       | **PARTIAL**                       | `rsize < 16` (`:136`) and `rsize > 340` (`:149`) gates exist, but `MAX_APRS_FRAME_SIZE` exceeds the buffer the parser writes into (F3-7)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| **17**         | Compile-time constants: single source of truth                        | **VIOLATED**                      | `MAX_CALL_LEN 20`/`LONGNAME_MAXLEN 20` vs `node_call[10]` (itself declared 3×); ring sizes re-declared in 5 `#if` branches, **2** of which are dead (not 3 — `ENABLE_XML` is live)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| **19**         | Parsers take `(const uint8_t*, size_t)`; boundary tests 0/1/max/max+1 | **VIOLATED**                      | `readPhoneCommand` takes no length (F3-21); `decodeAPRS`'s array parameter decays and lies about its size. The native test env exists (`pio test -e native`, 11 tests green) but compiles only `Regexp.cpp` and `regex_functions.cpp` — **zero parser tests**                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **20** STK     | Consider `-fstack-usage`; log stack high-water marks                  | **VIOLATED**                      | `printfdeb`'s 900-byte frame in the nRF52 radio task (F3-10); four attacker-sized VLAs (F3-4 ×2, F3-6 ×2, F3-23)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |

---

## 6. Making overflow structurally impossible

The bug list above has one shape repeated twenty times: **a length and a buffer travel separately,
and the check that ties them together is written by hand at each site.** Every finding is a site
where that hand-written check is missing, off by a constant, or applied to the wrong operand. Fixing
the twenty sites leaves the twenty-first to be written next month.

Five mechanisms, in ascending cost. The first, second and fifth are small enough to be
minimal-change PRs against upstream DEV under the `CLAUDE.md` rules.

### 6.1 One size header with `static_assert` on every relationship (rules 1, 2, 17)

Add `src/buffer_sizes.h`, included from `configuration_global.h`, containing every buffer size as a
named constant plus the invariants the code currently assumes silently:

```c
static_assert(sizeof(RcvBuffer) >= 2 * LORA_MAX_PHY_PAYLOAD,               "RX copy target");
static_assert(MAX_APRS_FRAME_SIZE <= APRS_CONCAT_BUF_SIZE,                 "F3-7");
static_assert(APRS_ENCODE_HEADER_LEN + APRS_MAX_PATH_TOTAL + 1 <= UDP_TX_BUF_SIZE, "F3-8");
static_assert(sizeof(BLEtoPhoneBuff[0]) >= UDP_TX_BUF_SIZE + 5,            "BLE row");
static_assert(UDP_TX_BUF_SIZE + BLE_TIMESTAMP_LEN <= UINT8_MAX,            "F3-12: 1-byte length field");
static_assert(sizeof(ringBufferUDPout[0]) >= UDP_HDR_LEN + 1 + UDP_TX_BUF_SIZE, "F3-17");
static_assert(sizeof(meshcom_settings.node_call) - 1 >= MAX_CALL_LEN,      "MAX_CALL_LEN vs node_call[10]");
static_assert(POS_BEACON_FIELD_TOTAL < sizeof(strconcat),                  "F3-25");
```

Three of these **fail today** — `MAX_APRS_FRAME_SIZE <= 255`, `UDP_TX_BUF_SIZE + 4 <= 255`, and the
`MAX_CALL_LEN` one — and that is the point: they convert invisible documentation lies into build
errors. `static_assert` on the wire-frame layout (`APRS_HDR_LEN == 6`, `APRS_TRAILER_LEN == 6`,
`UDP_DATA_HDR_LEN == 36`) pins the constants that `decodeAPRS`/`encodeAPRS`/`sendMeshComUDP`
currently rediscover with magic `+6`/`+10`/`+36`.

The board-class ladder also needs one assertion per class, because nothing today enforces that a
consumer array and its index bound use the same macro:

```c
static_assert(sizeof(ringBuffer) / sizeof(ringBuffer[0]) == MAX_RING, "ring/index drift");
```

**Cost:** one header, ~40 lines, no behavioural change. Catches F3-8, F3-12, F3-17, F3-25 at compile
time and documents the rest. This is the single highest-leverage item in this document.

### 6.2 A bounded slice type for the RX buffer (rules 3, 11, 19)

```c
struct ByteSpan {
    const uint8_t *p;
    size_t         n;
    bool     has(size_t off, size_t len) const { return off <= n && len <= n - off; }
    uint8_t  at (size_t i, uint8_t dflt = 0) const { return i < n ? p[i] : dflt; }
    ByteSpan sub(size_t off, size_t len) const {
        return has(off, len) ? ByteSpan{p + off, len} : ByteSpan{p, 0};
    }
};
```

`has()` is written as `len <= n - off` _after_ the `off <= n` test specifically so that `off + len`
can never wrap — the integer-overflow half of rule 10 (T3-9), closed by construction.

Change the signatures to `decodeAPRS(ByteSpan frame, aprsMessage&)`,
`readPhoneCommand(ByteSpan cmd)`, `getMeshComUDPpacket(ByteSpan pkt)`,
`handleACK(ByteSpan frame, …)`, and make `NrfETH::getUDP()` return a `ByteSpan`. Then:

| Finding    | What happens                                                                                                                  |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------- |
| F3-4, F3-6 | `config_buf[2 + call_len]` becomes `cmd.at(2 + call_len)`, returning 0 for an out-of-range index instead of reading the stack |
| F3-15      | becomes `if (!frame.has(0, 12)) return false;`                                                                                |
| F3-17      | becomes `row.sub(37, msg_len)`, which yields an empty span instead of over-reading                                            |
| F3-21      | **disappears** — the length is part of the type, so it cannot be dropped at a call site                                       |
| Rule 19    | satisfied by construction, which is what makes the parsers fuzzable off-target (§6.5)                                         |

**Cost:** a 15-line header plus signature changes at ~8 call sites. Contained enough for one PR.

### 6.3 A length-carrying dispatch table for `commandAction` (F3-13, F3-14, F3-20)

Replace the 223 `if (commandCheck(msg_text+2, "x") == 0) { … msg_text+N … }` arms with

```c
struct Cmd {
    const char *name;
    uint8_t     argc_min;
    void      (*fn)(ByteSpan arg, bool from_ble);
};
```

The dispatcher matches `name`, computes `arg = full.sub(2 + strlen(name), …)` **once**, skips
separator whitespace, and rejects the command if `arg.n < argc_min`. The handler never sees an
offset, so an off-by-one offset **cannot be written**. This also collapses the 4,900-line function
(SIMP-26) and turns the 42 `sscanf` sites into one `parse_int(arg, &out, min, max)` helper that
returns an error — fixing F3-20 wholesale.

This is a Track-B refactor and must be proposed to upstream as a plan first (`CLAUDE.md` §2). The
**one-token Track-A mitigation in the meantime is `char msg_text[300] = {0};`** at
`command_functions.cpp:199`, which neutralises all nine offset arms today.

### 6.4 `static_assert` on wire structs, and one bounded-append helper

`decodeAPRS`/`encodeAPRS` parse field by field, so there is no wire struct to assert on — but the
_offsets_ are constants scattered across three files. Introduce the struct purely as a layout
witness:

```c
struct __attribute__((packed)) AprsHeader {
    uint8_t  payload_type;
    uint32_t msg_id;
    uint8_t  flags;
};
static_assert(sizeof(AprsHeader) == 6, "APRS header is 6 bytes — encodeStartAPRS writes at +6");
```

and replace the twenty `strncat` calls in F3-25 with one helper whose bound cannot be mis-supplied:

```c
template <size_t N> void append(char (&dst)[N], const char *src) {
    size_t used = strlen(dst);
    if (used + 1 < N) snprintf(dst + used, N - used, "%s", src);
}
```

The array-reference parameter means `sizeof(dst)` is derived from the type, so the F3-25 class
(passing the wrong bound) becomes unrepresentable.

### 6.5 Compiler flags and sanitizers — measured, not estimated

All numbers below were measured on this tree, 2026-07-31, target `heltec_wifi_lora_32_V3` (ESP32-S3,
48 `src/` translation units, all compiled).

**Baseline** — `pio run -e heltec_wifi_lora_32_V3` with the current flags: **SUCCESS**, 8 warnings
total, **3 unique in `src/`** (`net_console.cpp:288` `-Wmisleading-indentation`, `Regexp.cpp:297`
×2 `-Wclobbered`). The June audit's `-Werror` exception rests on a backlog that does not exist —
consistent with 08 §3. **`-Werror` can be turned on for the ESP32 targets after three one-line
fixes.**

**Extended** — `PLATFORMIO_BUILD_FLAGS="-Wconversion -Wsign-compare -Wformat=2 -Wvla"`:

| Flag                               | What it catches                                                                                    | Unique sites in `src/`                                 | Notes                                                                                                                                                                                                                                                                                                                                                       |
| ---------------------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `-Wconversion`                     | T3-1, T3-2, T3-3, T3-4, T3-5 — every implicit narrowing and sign change in a size calculation      | **236**                                                | The dominant cost. Stage as `-Werror=conversion` on touched files first                                                                                                                                                                                                                                                                                     |
| `-Wfloat-conversion`               | `double`→`float`→`int` in the sensor/beacon paths                                                  | **77**                                                 | Subset of `-Wconversion`; mostly benign, mostly in `batt_*`/`gps_*`                                                                                                                                                                                                                                                                                         |
| `-Wvla`                            | F3-4 (×2, nRF52), F3-6 (×2), F3-23 — every attacker-sized VLA                                      | **3** on this target: `phone_commands.cpp:399,559,560` | The two `nrf_eth.cpp` VLAs need an nRF52 build to show. **3 of 3 target findings hit**                                                                                                                                                                                                                                                                      |
| `-Wformat=2` / `-Wformat-security` | Non-literal format strings (SEC-02)                                                                | **0**                                                  | Clean since `1cbcf8c9`. The commit records that it produced 2 diagnostics on the unfixed code — this flag is the regression test                                                                                                                                                                                                                            |
| `-Wsign-compare`                   | T3-5                                                                                               | **0**                                                  | Already implied by `-Wall`, which is already on. Does **not** fire on `int` vs `size_t` after promotion — use `-Wsign-conversion`                                                                                                                                                                                                                           |
| `-Wstringop-overflow`              | Constant-length `memcpy`/`strcpy` past a known array                                               | 0 additional                                           | Does **not** catch F3-25 (runtime lengths) or F3-2 (`SIZE_MAX` from an underflow)                                                                                                                                                                                                                                                                           |
| `-fstack-protector-strong`         | Turns F3-1/F3-4/F3-5/F3-8-class stack smashes into a controlled abort instead of silent corruption | n/a                                                    | Runtime cost only; the right default for the RF-facing build                                                                                                                                                                                                                                                                                                |
| `-Wstack-usage=1536`               | `printfdeb`'s 900-byte frame in the nRF52 radio task (F3-10)                                       | not measured                                           | Rule 20                                                                                                                                                                                                                                                                                                                                                     |
| `-Wall -Wextra` on nRF52           | Everything the ESP32 builds already get                                                            | not measured                                           | **Add to the `[nrf52]` block AND to the 6 ESP32 variants that override `build_flags`** — 9 of 34 environments currently build with no warning flags at all (3 nRF52 + `t5_epaper`, `t_deck_pro`, `vision-master-e213`, `vision-master-e213-preview`, `vision-master-e290`, `wireless-paper`), which is exactly where F3-4, F3-19 (nRF52 half) and T3-2 live |

Library noise for reference: the same flags produce 4,288 warnings across the whole build, i.e.
~3,850 come from RadioLib, NimBLE, U8g2 and the Arduino core. Anything staged toward `-Werror` must
be scoped with `-isystem` on the library include paths or applied per-file.

**Sanitizers on the native build.** `[env:native]` exists (`platformio.ini:140-155`) and works —
`pio test -e native` runs 11 Unity tests green in 0.59 s. Host toolchain verified: Apple clang 21.0
with `-fsanitize=address,undefined -fno-sanitize-recover=all` links and traps a synthetic 1-byte
`memset` overflow. Two gaps to close, in order:

1. **`build_src_filter` currently lists only `Regexp.cpp` and `regex_functions.cpp`.** Every parser
   in this document is outside it. Add `+<aprs_functions.cpp>` first — it has **no** hardware
   dependency beyond `Arduino.h` and `meshcom_settings`, both already shimmed in `test/support/`.
   `phone_commands.cpp` and `udp_functions.cpp` need `ByteSpan` (§6.2) plus a settings stub.
2. **Add the sanitizer flags to the native env**, not to the firmware envs:

   ```ini
   [env:native]
   build_flags =
       ${env:native.build_flags}
       -fsanitize=address,undefined
       -fno-sanitize-recover=all
       -fno-omit-frame-pointer
   build_type = debug
   ```

   `test/support/configuration.h` already pins `CONFIG_IDF_TARGET_ESP32S3`, so the native build
   exercises the S3+RAK ring sizes — the class that ships on 19 of 31 variants. A second env
   pinning the fallback class would cover the other 11.

Then a libFuzzer entry point per parser:

```c
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    aprsMessage m;
    decodeAPRS(ByteSpan{d, n}, m);
    return 0;
}
```

plus the same for `readPhoneCommand` and `getMeshComUDPpacket`. **F3-4, F3-5, F3-7, F3-15 and F3-25
are all first-minute findings for such a harness**; F3-2 needs the ring-buffer producer in the loop,
which is the second harness to write. `_FORTIFY_SOURCE` is not useful on target (newlib-nano
implements none of the `__*_chk` builtins), so ASan on the native build is the substitute, not a
supplement.

### 6.6 Order of work

| Wave | Items                                                                                                                                          | Why first                                                               |
| ---- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| 0    | §6.5 baseline: `-Werror` on the 25 envs that already have `-Wall -Wextra` (3 fixes), `-Wall -Wextra` on the 9 that lack it, `-Wvla` everywhere | Zero behaviour change, mechanically prevents the VLA class from growing |
| 1    | F3-2, F3-4 (CRITICAL, both remote)                                                                                                             | One frame / one datagram to memory corruption                           |
| 2    | F3-3, F3-5, F3-6 (HIGH) + the one-token F3-13 mitigation                                                                                       | Remote disclosure and stack overflow                                    |
| 3    | §6.1 size header + `static_assert`                                                                                                             | Turns F3-8, F3-12, F3-17, F3-25 into build errors                       |
| 4    | F3-8, F3-9, F3-11, F3-12, F3-15, F3-25                                                                                                         | Track-A one-liners, each with a regression test                         |
| 5    | §6.2 `ByteSpan` + native harness extension                                                                                                     | Makes F3-4, F3-6, F3-15, F3-21 unrepresentable and the rest fuzzable    |
| 6    | §6.3 dispatch table (propose upstream as a plan first)                                                                                         | Track B; collapses F3-13, F3-14, F3-20 and SIMP-26 together             |

---

## 7. Verification status

### 7.1 What was re-verified against the current tree

Files **changed** by the rebase (line numbers had to be re-derived): `command_functions.cpp` (+117),
`configuration_global.h`, `esp32/esp32_main.cpp` (+102), `extudp_functions.cpp` (+86),
`loop_functions.cpp` (+296), `loop_functions.h`, `loop_functions_extern.h`, `lora_functions.cpp`
(+174), `nrf52/nrf52_main.cpp` (+21), `nrf52/nrf_eth.cpp` (+9), `printfdeb_functions.cpp` (+8),
`udp_functions.cpp` (+8), `via_functions.cpp` (+12), `platformio.ini`.

Files **unchanged** since the report (citations spot-checked and confirmed): `aprs_functions.cpp`,
`aprs_functions.h`, `mheard_functions.cpp`, `phone_commands.cpp`, `phone_commands.h`,
`web_functions/web_functions.cpp`, `web_functions/web_setup.cpp`, `net_console.cpp`,
`nrf52/nrf52_ble.cpp`, `Displays/BaseDisplay/SD.cpp`, `t-deck-pro/ui_deckpro_port.cpp`.

**61 citations were corrected.** The largest shifts: `loop_functions.cpp` +245 in the `sendMessage`
region (F3-5, BUG-11), `command_functions.cpp` +106 in the `setout`/`setio`/`setgrc`/`regex` arms
(F3-13) and +106/+111 for the `serializeJson` sites (F3-18), `extudp_functions.cpp` +86 (F3-9).

### 7.2 Claim-by-claim status

| Claim                                        | Status                                                            | Evidence                                                                                                                                                              |
| -------------------------------------------- | ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F3-1 CONF zero-fill overflow                 | **FIXED**                                                         | `93bb68d0`; loop at `nrf_eth.cpp:520` now starts at `packetSize-4`. Re-read                                                                                           |
| F3-10 `Serial.printf(temp)` format injection | **FIXED**                                                         | `1cbcf8c9`; `printfdeb_functions.cpp:122`. `-Wformat=2` build: 0 diagnostics in `src/`                                                                                |
| F3-2 `blelen-4` → `SIZE_MAX` memcpy          | **OPEN, re-verified**                                             | `web_functions.cpp:1279,1296,1297` + `loop_functions.cpp:529,548` read in full                                                                                        |
| F3-3 mheard heap over-read                   | **OPEN, re-verified + extended**                                  | `mheard_functions.cpp:517-533`. New: `mheardPathCalls` has no NUL slot reserved                                                                                       |
| F3-4 CONF field parse                        | **OPEN, re-verified, citation corrected**                         | `nrf_eth.cpp:532-556` (was `:528-556`)                                                                                                                                |
| F3-5 URL decode                              | **OPEN, re-verified, citation corrected**                         | `loop_functions.cpp:3245-3312` (was `:3004-3069`); loop read in full                                                                                                  |
| F3-6 BLE 0x55                                | **OPEN, re-verified**                                             | `phone_commands.cpp:554-563` unchanged                                                                                                                                |
| F3-7 `MAX_APRS_FRAME_SIZE`                   | **LATENT — adjudication carried forward**                         | 08 §3: all six callers traced, ≤ 255 today. Code re-read at `aprs_functions.cpp:149,364-378`; unchanged                                                               |
| F3-8 `encodeStartAPRS` +6                    | **OPEN, re-verified**                                             | `aprs_functions.cpp:1027-1034`; file unchanged                                                                                                                        |
| F3-9 UDP `[len]=0` ×2                        | **OPEN, one citation corrected**                                  | `udp_functions.cpp:100,104`; `extudp_functions.cpp:308,314` (was `:222,228`)                                                                                          |
| F3-11 `blelen-1`                             | **OPEN, re-verified**                                             | `phone_commands.cpp:135,151`; clamp confirmed at `command_functions.cpp:4588`                                                                                         |
| F3-12 1-byte length fields                   | **OPEN, citations corrected**                                     | `loop_functions.cpp:529,548,581,587,588`                                                                                                                              |
| F3-13 offset arms                            | **OPEN, all 9 re-derived; count corrected 217 → 223**             | 6 new `ping*` arms added by the rebase, none violating                                                                                                                |
| F3-14 `_owner_c[-1]`                         | **OPEN, citation corrected**                                      | `command_functions.cpp:2873-2875` (was `:2869-2871`)                                                                                                                  |
| F3-15 `handleACK`                            | **OPEN, re-verified**                                             | `lora_functions.cpp:202-214`; unchanged                                                                                                                               |
| F3-16 zero-scan `i+1`                        | **OPEN, re-verified**                                             | `udp_functions.cpp:121-123`, `nrf_eth.cpp:233-235`; unchanged                                                                                                         |
| F3-17 UDP row over-read                      | **OPEN, one citation corrected**                                  | `nrf52_main.cpp:2669` (was `:2648`)                                                                                                                                   |
| F3-18 `measureJson`                          | **OPEN, count corrected 14 → 15**                                 | Report missed `mheard_functions.cpp:348`                                                                                                                              |
| F3-19 `strText[600]`                         | **PARTIALLY MITIGATED on ESP32; OPEN on nRF52**                   | Self-heal at `esp32_main.cpp:3981-3989` is **new from the rebase**; `nrf52_main.cpp` has no equivalent. Verified against `git show 1ba101f4:src/esp32/esp32_main.cpp` |
| F3-20 `sscanf`                               | **OPEN, count corrected 40 → 42; 12 settings targets enumerated** | `command_functions.cpp` re-grepped                                                                                                                                    |
| F3-21 `readPhoneCommand`                     | **OPEN, one citation corrected**                                  | `esp32_main.cpp:2816` (was `:2776`)                                                                                                                                   |
| F3-22 `strncpy` VIA                          | **OPEN, citations corrected**                                     | `via_functions.cpp:118,130,140` (was `:107-129,119`)                                                                                                                  |
| F3-23 BLE 0x50 VLA                           | **OPEN, re-verified**                                             | `phone_commands.cpp:399-408`; unchanged                                                                                                                               |
| F3-24 a–g                                    | **OPEN, 5 of 7 citations corrected**                              | Bundle re-checked site by site                                                                                                                                        |
| F3-25 `strncat` chain                        | **NEW — not in the raw report**                                   | `loop_functions.cpp:3738-3758`; field widths re-derived from `:3592-3729`                                                                                             |
| T3-1 … T3-8                                  | **OPEN, 5 citations corrected**                                   | See §4                                                                                                                                                                |
| T3-9 integer overflow                        | **NEW — searched, no wrapping site found**                        | Recorded so it is not re-investigated                                                                                                                                 |

### 7.3 Stale, refuted, or contradicted

| Raw-report claim                                                                               | Verdict                                          | Correction                                                                                                                                                                                                                                                                                                |
| ---------------------------------------------------------------------------------------------- | ------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ENABLE_XML` is a dead branch                                                                  | **STALE / wrong**                                | Live: `variants/E22_XML-DevKitC/configuration.h:9`, and that env is in `default_envs`. Agrees with `f1-factcheck.md:399` and `f8-completeness.md:443`. Only `ENABLE_SBUFFER` and `ENABLE_TBEAM` are dead                                                                                                  |
| `T-Connect-Pro` is an nRF52 in the fallback class                                              | **wrong**                                        | `boards/esp32s3_flash_16MB.json` → `mcu: esp32s3`; it is in the S3 class                                                                                                                                                                                                                                  |
| "`-Wall -Wextra` appears only in the two safeboot environments (`platformio.ini:158,185,225`)" | **wrong then, wrong now**                        | Pre-rebase line 158 was inside `[esp32]` (section starts at `:132`), not a safeboot env. 25 of 34 environments have it. Current lines: `:190` (`[esp32]`), `:217`, `:257` (safeboot), `:149-150` (`[env:native]`). **9** build without it — the 3 nRF52 plus 6 ESP32 variants that override `build_flags` |
| "Not one `snprintf` return value is captured anywhere in `src/`"                               | **wrong**                                        | 3 sites (`esp32/at_cmd.h:30`, `nrf52/at_cmd.h:30`, `nrf52/WisBlock-API.h:692`), one of them added by `5ec13237`                                                                                                                                                                                           |
| "None of the 40 `sscanf` calls check the return value"                                         | **imprecise**                                    | True for the 42 in `commandAction`; tree-wide 3 of 81 do check                                                                                                                                                                                                                                            |
| "50 `snprintf` calls whose size argument is not `sizeof(dest)`"                                | **imprecise**                                    | 27 in the core, 61 tree-wide, depending on scope. Scope is now stated (§5)                                                                                                                                                                                                                                |
| "`grep -rn static_assert src` → 3 hits"                                                        | **stale**                                        | 2 hits today, both in `src/code_review/code-audit-20260508.md`. The conclusion (zero in compiled code) is unchanged                                                                                                                                                                                       |
| "14 `serializeJson` calls sized by `measureJson`"                                              | **undercount**                                   | 15; `mheard_functions.cpp:348` was missed                                                                                                                                                                                                                                                                 |
| "217 `commandAction` arms"                                                                     | **stale**                                        | 223 after the rebase; the 9 violating arms are unchanged. 08 §3 records "217 arms / 227 verbs" as of `1ba101f4` — that figure is now also stale                                                                                                                                                           |
| `snprintf(value, 100, …)` on `char value[40]` (`web_functions.cpp:1660`) is an overflow        | **REFUTED — carried forward from 08 §3**         | Source is `char node_mcp17t[16][16]`, max 16 bytes. BND-02 rule violation, **not** a memory-safety bug                                                                                                                                                                                                    |
| `snprintf(val, 160, …)` in `extudp_functions.cpp` overflows                                    | **REFUTED — carried forward from 08 §3**         | `char val[160+1]` at `:225`; explicit guard at `:266` caps `dst` ≤ 9 and `msg` ≤ 150 → worst case 162 bytes, truncated to 160 by the bound. Safe. **Note the discrepancy:** 08 §3 cites `extudp_functions.cpp:195`; the current line is `:281` (file +86 lines)                                           |
| `MAX_APRS_FRAME_SIZE 340` vs 255-byte parser buffers is exploitable                            | **REFUTED as live — carried forward from 08 §3** | All six callers traced; latent, not reachable today. Recorded here as F3-7 with LATENT severity, not dropped                                                                                                                                                                                              |

### 7.4 Carried over without independent re-derivation

| Claim                                                                           | Source             | Why not re-derived                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| ------------------------------------------------------------------------------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Arduino `String` heap-allocates `len+1` with no SSO on both cores               | raw report         | Would need a library read; the finding (F3-3) does not depend on the exact allocator granule                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| The escaped worst case of the telemetry JSON exceeds 350 bytes                  | raw report         | Needs a runtime measurement with five 50-byte user strings; the structural point (bound derived from the document, not the buffer) holds regardless                                                                                                                                                                                                                                                                                                                                                                                        |
| ~~`OnRxDone` runs in the SX126x `"LORA"` FreeRTOS task at priority 2 on nRF52~~ | 08 §1 `C-01`       | **WRONG — corrected 2026-07-31.** The `"LORA"` task runs at priority **1** (`board.cpp:44` defines `TASK_PRIO_NORMAL 1` because `#ifndef` cannot see the core's `= 2` enum) and `configUSE_TIME_SLICING` is 0. The real priority-2 path is the **FreeRTOS timer service task** (`RadioOnRxTimeoutIrq → RadioBgIrqProcess → RadioEvents->RxDone`) on a **1 KB** stack (`configTIMER_TASK_STACK_DEPTH 256` words). This makes F3-10's stack concern **worse**, not better. See [`09-concurrency-map.md`](09-concurrency-map.md) and 08 C-01. |
| `-Wall -Wextra` produces 9 warnings, 4 in `src/`                                | 08 §3              | Re-measured today: **8 total, 3 unique in `src/`**. Small drift from the rebase; conclusion unchanged                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `printfdeb` is called ~20× inside `OnRxDone`                                    | raw report         | Call count not re-derived; the ≥1-per-frame claim is enough for the MEM-01 finding                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| CONC-15/16 (non-atomic ring indices)                                            | `fable-verdict.md` | T3-8 restates the buffer-side consequence only; the concurrency analysis is `f2-concurrency.md`'s                                                                                                                                                                                                                                                                                                                                                                                                                                          |
