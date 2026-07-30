# 08 — Defect Catalogue & Remediation Plan

> **What is actually broken, in what order do we fix it, and how does each fix become
> one commit and one upstream PR?**

Review date 2026-07-31, branch `v4.35p_prio` at `1ba101f4`. Method: 8 independent finder
angles → adversarial verification → orchestrator re-verification of every
decision-relevant claim against the real source, the real toolchain, and the real build.

**Verification legend**

| Mark          | Meaning                                                                                 |
| ------------- | --------------------------------------------------------------------------------------- |
| **VERIFIED**  | Orchestrator independently re-derived it (command shown or cited) — not finder-reported |
| **CONFIRMED** | Finder cited file:line and mechanism; spot-checked, not fully re-derived                |
| **REFUTED**   | Claimed by a finder or by this concept, disproved. Recorded so it is not re-found       |

---

## 0. Prior art — this is not a fresh start

`fable-verdict.md` (repo root, 2026-07-12) already holds **39 verified findings**
(`SEC-01` … `TEST-39`) against this same branch. The `docs/architecture/` concept
(01–07) was written without reading it and re-derived its structural half while missing
its security and concurrency halves entirely.

**Fix status: essentially all 39 are still open.** No commit references any ID
(`git log --all | grep -iE 'SEC-0|BUG-1|CONC-1|DRY-2|SIMP-2|ALT-3|TEST-3'` → empty). The
June 26–27 fix commits predate the verdict and address the earlier `docs/code-audit-*`
series.

**Correction to the prior verdict:** `DRY-20` is labelled `RESOLVED` but its own body says
both battery implementations are live. The label meant "finder disagreement resolved", not
"defect fixed". Re-derived today: 13 variants define `USE_NEW_BATT`, **17 do not** — the
split has grown since the verdict counted 12/15 of 27. **Status: OPEN.**

This catalogue **adopts the existing IDs**. New findings get `N-nn`.

---

## 1. Corrections to this concept (docs 01–07)

The concept contained errors that would have sent work in the wrong direction. Listed
first because several of its recommendations must be withdrawn before anyone acts on them.

### C-01 — `OnRxDone` does not run in interrupt context — **VERIFIED**

01 and 04 state the RX callback runs "off the radio callback" with everything it touches
shared with the main loop. Wrong on both platforms, differently:

- **ESP32:** the only call is `checkRX()` at `esp32_main.cpp:3778`, itself called from
  `esp32loop()` at `:2217`. It runs in `loopTask`. The only ISR is
  `setFlagReceive`/`setFlagSent` (`:487`, `:503`) — nine lines, four atomics.
- **nRF52:** it runs in the SX126x `"LORA"` FreeRTOS task at priority 2, which **preempts
  `loop()` at priority 1**.

This mislabel is load-bearing — it is the stated justification for `displayMux`.

### C-02 — the radio-interface recommendation is ~10× oversized and mis-targeted — **VERIFIED**

01 §3, 04 item 7 and 05 Phase 3 rank "extract a radio interface, collapse ~3,200 lines of
duplicated scheduler" as the single highest-leverage change. The 3,200 figure is
`1947 + 1233` — the two loop _sizes_, not a duplication measurement.

Measured overlap between `esp32loop()` and `nrf52loop()`: **~221–268 lines (≈15 %)**, in 9
blocks. **None of the shared blocks is radio code** — they are deferred display text, NTP/RTC
parsing, the position beacon and sensor polling, all already radio-independent and
extractable today with zero RF risk.

The real blocker is not a differing API but a differing **concurrency model** (C-01), plus
CAD being synchronous on one side (`radio.scanChannel()`) and asynchronous on the other
(`Radio.StartCad()` → `OnCadDone`). A 7-method interface does not hide that.

**Withdrawn.** Replaced by: extract the ~221 radio-independent shared lines into
`common_loop.cpp`. Small, safe, real.

### C-03 — the golden-vector corpus does not exist — **VERIFIED**

06 Layer 2 and 07 §1.5 call `tools/meshcom_monitor/*.log` "a golden-vector corpus you
already own" and cost the extractor at 3–5 sessions. This is the claimed foundation of the
entire before/after oracle.

Measured across all 17 logs:

| Metric                                   | Value   |
| ---------------------------------------- | ------- |
| Hex dumps (`[MC-DBG] CRC_PAYLOAD[255]:`) | 1,821   |
| `MH-LoRa:` decode lines                  | 25,645  |
| Files containing **any** hex dump        | 3 of 17 |
| **Usable (hex, decode) pairs**           | **0**   |

The reason is structural, not a parsing detail. `esp32_main.cpp:3811-3821` emits
`CRC_PAYLOAD` **only inside the `RADIOLIB_ERR_CRC_MISMATCH` branch**, which returns before
`OnRxDone()` (the sole call, in the sibling `else` at `:3778`) and therefore before
`decodeAPRS()`. **A frame can never be both dumped and decoded.** Every dump is a
CRC-failed frame, and is 255 bytes of `checkRX`'s uninitialised stack — the tails visibly
contain ASCII from earlier `printf` calls. True frame lengths are recorded nowhere, so
06's instruction that "the extractor must honour the length field" is not implementable.

06's own worked example is a mismatched pair: the hex is from `…172422.log:363` at
17:26:21.918 (CRC-failed, RSSI −119, visibly bit-damaged); the `MH-LoRa:` block beside it
is from 17:24:32.263 — **109 seconds and a different frame away**.

**And the oracle would be circular even with correct pairs.** `MH-LoRa:` has one emit site
(`lora_functions.cpp:496` → `printBuffer_aprs`), which pretty-prints the struct
`decodeAPRS()` just filled. Asserting against it proves `decode_new(x) == decode_today(x)`
— a regression fence, never a correctness oracle. The corpus is also self-selecting
(`decodeAPRS` returns `0x00` on FCS mismatch) and lossy (`lat`/`lon`/`alt` are not in the
`MH-LoRa:` format string at all).

**Withdrawn.** See §4 for what replaces it. This is the most consequential correction in
the review: the zero-tolerance before/after requirement had no foundation.

### C-04 — the Arduino 2.0.17 headline is false for four boards — **VERIFIED**

03 states every `espressif32` release pins `~3.20017.0`. Re-derived from the platform
manifests:

| Platform version | `framework-arduinoespressif32` | Arduino core |
| ---------------- | ------------------------------ | ------------ |
| 6.5.0            | `~3.20014.0`                   | **2.0.14**   |
| 6.6.0            | `~3.20014.0`                   | **2.0.14**   |
| 6.13.0           | `~3.20017.0`                   | 2.0.17       |

So `t_deck` and `t_deck_plus` (`@ 6.6.0`) and `t_deck_pro` and `t5_epaper` (`@ 6.5.0`)
already run **Arduino 2.0.14 / IDF 4.4.5** — a different core from the other 26 boards.
The doc's headline denies exactly this. "Bumping the platform buys nothing" is wrong for
those four: it is a real core update, and it belongs in the sequence.

### C-05 — 02's finding B-01 has the wrong mechanism and a board-breaking fix — **VERIFIED**

02 claims `[nrf52]`, declared in three variant files, "collides and the effective content
depends on glob order". ConfigParser merges duplicate sections option-by-option; there is
no race. `pio project config --json-output` shows `heltec_t114` and `t_echo`
**deterministically** receive from the `wiscore_rak4631` copy:

```
-D BOARD_RAK4630="RAK4630"        # used at 66 #if/#elif sites
-Isrc/nrf52
build_src_filter: … +<../variants/wiscore_rak4631/*>
```

02's proposed cleanup — move `[nrf52]` to root, push the wiscore-specific filter line into
`[env:wiscore_rak4631]` — would silently strip all three from two shipping boards.
**Withdrawn as written.** The section duplication is still worth removing, but only
together with explicit per-board restatement of these three options.

### C-06 — remaining concept corrections — **CONFIRMED**

| ID   | Doc         | Claim                                                               | Reality                                                                                                                                                                                                                                                            |
| ---- | ----------- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| C-06 | 07 §1.1     | `-D LORA_ISR_DEBUG` unlocks 5 ISR traces on any board               | All 5 sites sit inside `#if defined BOARD_RAK4630`. **No-op on the Heltec V3 bench.**                                                                                                                                                                              |
| C-07 | 04          | `mheard` ↔ `ui_deckpro` layering violation, ~200 lines              | **Inverted**: it is LVGL table code (`showMHeardTDECK`) living in `mheard_functions.cpp`, 54 lines. Remediation item 4 moved code the wrong way.                                                                                                                   |
| C-08 | 04          | "218 commands"                                                      | 217 arms / 227 distinct verbs. The `--setinfo off` before `--setinfo` example is invented — no bare `--setinfo` exists.                                                                                                                                            |
| C-09 | 04          | `commandAction` is one flat if/else chain, refactor is "mechanical" | 7 chain segments (breaks at `:1478, 2227, 2749, 2981, 3205, 3532`) feeding an 11-flag shared continuation, ~190 set-sites. `--info` matches at `:719`, executes at `:4694`. `commandCheck` is a **prefix** match, so the doc's binary-search alternative is wrong. |
| C-10 | 04          | CC counts `&&`/`                                                    |                                                                                                                                                                                                                                                                    | `   | `arch_metrics.py`'s `\b…\b` wrapper cannot match them. The stated formula is wrong; rankings stand. |
| C-11 | 03          | quotes the "tight boards" ring config                               | That block is `#if ENABLE_TBEAM` — **defined nowhere**. Dead code quoted as live.                                                                                                                                                                                  |
| C-12 | 03          | step 0b "RAM baseline across all 32 envs"                           | `tools/ram_snapshot.py` hardcodes 7 targets. Not executable as written.                                                                                                                                                                                            |
| C-13 | 01/06/07    | concept covers concurrency                                          | "dual-core", "core 0", "core 1", "pinnedToCore" appear **nowhere** in 01–07. No core model at all.                                                                                                                                                                 |
| C-14 | 06 §Layer 1 | native extraction costed at 5–8 sessions                            | Measured closure: `aprs_functions.cpp` needs 7 globals + 3 stubs; `via_functions.cpp` 9 + 5. **1–2 sessions.** Pessimism was deferring the cheapest layer.                                                                                                         |
| C-15 | 07 §5       | `pio test` on device                                                | Blocked twice: `src/main.cpp` has no `PIO_UNIT_TESTING` guard, and `upload_protocol = custom` hardcodes `${build_dir}/${env}/firmware.bin` while `pio test` emits to `<env>/<test>/firmware.bin`. Flash headroom is fine (1.49 of 3.40 MB).                        |
| C-16 | 05          | large contributions are "structurally unmergeable" upstream         | **24 PRs from this author are merged upstream, 0 open**, from `+1 −0` to `+3984 −1516`. Assumption, not measurement. Withdrawn.                                                                                                                                    |
| C-17 | 07 §1.4     | recommends `--debug csv` as the machine-readable mode               | 15,783 of 25,645 captured lines carry payload semicolons that pass through unescaped. CSV mode is not parseable on real traffic.                                                                                                                                   |

---

## 2. Live defects — new findings

Severity reflects reachability from untrusted input. **RF** = any node in radio range,
unauthenticated, unattributable.

### N-01 — `{MCP}` remote command: password check is a character-set membership test — **VERIFIED** — Critical, RF

`src/loop_functions.cpp:2057-2071`

```c
for(int ip=0;ip<5;ip++) {
    if(cpasswd[ip] != 0x00 && bpass) {
        bool bp=false;
        for(int ic=0;ic<15;ic++)
            if(meshcom_settings.node_passwd[ic] == cpasswd[ip])
                bp=true;                 // membership, not equality
        bpass = bp;
    }
}
```

Two independent bypasses:

1. **Space padding.** `command_functions.cpp:3007` stores the password as `"%-14.14s"` —
   left-justified, space-padded to 14. Any password shorter than 14 characters therefore
   contains `0x20`, and five spaces satisfy the test. Order, position and repetition are
   all irrelevant.
2. **Zero bytes.** `bpass` is initialised `true` and the inner check runs only for
   `cpasswd[ip] != 0x00`. `cset` is zeroed and the password field sits at offset 12, so a
   payload short enough leaves all five bytes zero — the loop never executes and `bpass`
   stays `true`.

The second gate compares three characters against decimal digits of
`aprsmsg.msg_id & 0x3FF` — the attacker's own frame, freely chosen. The handler then
reaches `commandAction("--setout …")` for GPIO control.

**Relation to SEC-01:** SEC-01 found only the empty-password path. Its proposed fix
(`if(!bpass || cpasswd[0]==0x00) return;`) **does not close the membership test.**

**Fix:** constant-time `memcmp` against the stored password with an explicit length, after
rejecting an unset password. Regression test: five spaces must fail against a 6-character
password.

### N-02 — `{SET}` mutates mesh routing with no authentication — **VERIFIED** — Critical, RF

`src/loop_functions.cpp:2121-2127`

```c
if(aprsmsg.msg_payload.startsWith("{SET}") > 0) {
    char cset[30];
    snprintf(cset, sizeof(cset), "%s", aprsmsg.msg_payload.c_str());
    sscanf(cset+5, "%d;%d;", &meshcom_settings.max_hop_text, &meshcom_settings.max_hop_pos);
    return;
}
```

No password gate, no range check, `%d` into signed fields, written straight into persisted
settings. One broadcast `{SET}0;0;` sets `max_hop == 0` on every node in radio range: each
keeps transmitting, nobody relays it, and every affected node is silently cut out of the
mesh. No log line, no visible symptom. **Not in any prior document.**

**Fix:** same auth gate as `{MCP}`, plus range clamp to the documented hop limits.

### N-03 — CONF zero-fill writes up to 251 bytes past a stack buffer — **VERIFIED** — Critical, UDP

`src/nrf52/nrf_eth.cpp:517`. Found independently by two finders.

```c
uint8_t config_buf[UDP_CONF_BUFF_SIZE] = {0};        // 255, already zeroed
…
for (int i = 0; i < UDP_CONF_BUFF_SIZE; i++)
    config_buf[packetSize - UDP_MSG_INDICATOR_LEN + i] = 0x00;
```

The counter starts at 0 but the index is biased by the packet length. `packetSize == 255`
writes `config_buf[251 … 505]`. Unauthenticated UDP to the CONF port on nRF52 with
Ethernet. **The loop is also redundant** — the buffer is already `= {0}`.

**Fix:** delete the loop. The ESP32 counterpart is clean — exactly the divergence `DRY-21`
predicted.

### N-04 — `memcpy` with a length near `SIZE_MAX` from one RF frame — **VERIFIED** — Critical, RF

Chain of three sites:

1. `loop_functions.cpp:527` — `if (len > UDP_TX_BUF_SIZE) len = UDP_TX_BUF_SIZE-4;` lets
   `len ∈ {252,253,254,255}` through unchanged.
2. `loop_functions.cpp:546` — `BLEtoPhoneBuff[toPhoneWrite][0] = len + 4;` into one byte →
   wraps to 0,1,2,3. (This is `BUG-08`.)
3. `web_functions.cpp:1279,1296` — `uint8_t blelen = …[0];` then
   `memcpy(toPhoneBuff, …, blelen - 4);`. Promotion to `int` gives −4…−1; conversion to
   `size_t` gives ≈`SIZE_MAX`.

**Trigger:** a 252–255 byte LoRa text frame with a valid FCS, then any load of the web
messages page. Immediate hard fault.

**Fix:** clamp in `addBLEOutBuffer` (`len > UDP_TX_BUF_SIZE-4`), and bounds-check `blelen`
before the subtraction.

### N-05 — heap over-read is rebroadcast over the air — **VERIFIED** — High, RF

`src/mheard_functions.cpp:526` and `:532`

```c
int ipc = mheardLine.mh_sourcepath.length() - ips;
if(ipc > 37) ipc = 37;                                    // computed correctly…
…
memcpy(mheardPathBuffer1[ipos],
       mheardLine.mh_sourcepath.substring(ips).c_str(),
       sizeof(mheardPathBuffer1[ipos]));                  // …and never used. 50 bytes.
```

Length is the **destination** size (50). The source is a temporary `String` from an
RF-derived path that may be a few bytes. Up to ~44 bytes of adjacent heap are copied in,
then displayed, JSON-exported to the phone, and used to build **outgoing HEY paths**.

**Fix:** use `ipc`.

### N-06 — unbounded array index from a web parameter — **VERIFIED** — Critical, LAN

`src/web_functions/web_setup.cpp:551`

```c
uint8_t t_io = (uint8_t)port.charAt(1) - 48;
if(port.charAt(0)=='B') t_io+=8;
snprintf(meshcom_settings.node_mcp17t[t_io], sizeof(…), "%s", …);
```

Only `port.length() == 2` is checked. `charAt(1) == 0xFF` gives `t_io = 215` against
`node_mcp17t[16][16]` → a 16-byte write ~3.4 kB past the array, inside `meshcom_settings`,
followed by `save_settings()`.

**Fix:** validate `t_io < 16` and reject otherwise.

### N-07 — BLE command channel is unauthenticated and ungated — **VERIFIED** — Critical, BLE range

`esp32_main.cpp:1588` `setSecurityAuth(false,false,false)`; `:1613-1626` all `WRITE_ENC` /
`READ_ENC` commented out; `:2784` the `if(hasMsgFromPhone)` dispatch calls `commandAction`
with **no `isPhoneReady` gate** — that check begins only at `:2794`. Any BLE device in
range reaches the full command surface including `--cleanflash`, `--setpwd`, `--ota-update`.

A prior audit accepted this as "only a buggy app could do that". The code does not
support that reading.

### N-08 — `millis()` rollover: the safe idiom is not universal — **VERIFIED** — Medium

The A1 "millis() wraparound" fixes in four June commits converted ~70 sites to the safe
`(uint32_t)(millis() - t) >= N` form. Still remaining:

- **33** assignments of the form `X = millis() + N`
- **11** comparisons of the form `millis() > X`

including **both** platforms' `rebootAuto` (`esp32_main.cpp:3173`, `nrf52_main.cpp:2019`)
and `DisplayOffWait` (`:3153`, `:2000`). After 49.7 days of uptime the scheduled reboot
either never fires or fires immediately. Mesh nodes run for months.

### N-09 — 11 × `while (true);` hard hang — **VERIFIED** — High

`src/t-deck-pro/peri_lora.cpp:48–114`. The 2026-06-26 audit mandated a fix and explicitly
pre-rejected the T-Deck-Pro exemption in writing ("_trotz T-Deck-Pro: geteilter
Code-Pfad_"). `code-audit-fixes-20260627.md:70` then used that exemption and marked the row
✅ done. `t_deck_pro` is in `default_envs` and ships as a CI release artifact.

### N-10 — board identity macros are arithmetic on product names — **VERIFIED** — Medium

`platformio.ini` writes `-D BOARD_E22_S3="esp32-s3-devkitc-1-n16r8"`; PlatformIO's ini
parser **consumes the quotes**, so the compiler receives an identifier sequence
(`pio run -v` confirms). At 12 sites in 5 files,
`#if defined(BOARD_TBEAM_V3) || (BOARD_E22_S3)` therefore evaluates
`0 - 0 - 0 - 1 - 0 == -1` → true. The guard works **by accident**:

```
$ xtensa-esp32s3-elf-gcc -E -DBOARD_E22_S3=esp32-s3-devkitc-1-n16r8 pp.c  → guard = 1
$ xtensa-esp32s3-elf-gcc -E -DBOARD_E22_S3=esp32-s3-devkitc-0-n16r8 pp.c  → guard = 0
```

One digit in a marketing string removes the I²C bus-reset workaround, with no diagnostic.
**28 `BOARD_*` macros** are defined this way. `-Wundef` catches it and is enabled nowhere.

Sites: `aht20.cpp:45,70`, `bmp390.cpp:54,84`, `bmx280.cpp:169,213`,
`rtc_functions.cpp:28,71,83,99`, `sht21.cpp:41,68`. The prior verdict (`DRY-25`) listed 9
sites in 4 files and **missed `rtc_functions.cpp` entirely**; two sites it did list
(`bmx280.cpp:131,143`) have since been changed to plain `#ifdef BOARD_TBEAM_V3`, so E22_S3
now silently lacks the workaround there — a regression introduced after the verdict.

### N-11 — GPL-3.0 libraries statically linked into an MIT-licensed firmware — **CONFIRMED** — legal

`lib/GxEPD2` and `lib/ESP32-audioI2S` are GPL-3.0; `lib/epdiy` and `lib/TinyGSM` are
LGPL-3.0; `lib/es7210` and `lib/Timeout` carry no licence. The root `LICENSE` is MIT.
Also: an external contributor patched `lib/TinyGSM/` in place, invisible to `lib_deps`.

Not a code defect, but it is the kind of thing that surfaces at the worst moment. Needs a
decision, not a fix.

### N-12 — `FLASH_VERSION` neither migrates nor resets — **CONFIRMED** — High

The version check runs _after_ `init_flash()` and is not followed by a re-read, so the old
RAM copy is written straight back — while the log prints `FLASH cleared new version`.
There are two incompatible `meshcom_settings` layouts (2008 B ESP32 / 1968 B nRF52); nRF52
raw-`memcpy`s its struct with two marker bytes as the only integrity check, so inserting a
field mid-struct corrupts every nRF52 node in the field. `--cleanflash` is a no-op on
nRF52.

This is the highest-risk item for any fleet-wide settings change.

### N-13 — over-synchronisation: atomics and locks where there is no concurrency — **VERIFIED**

Direct answer to the project goal _"atomics exactly where there is genuine concurrent
access, and nowhere else"_. Because of C-01, the ESP32 LoRa path is single-context, so
**14 objects can drop synchronisation on ESP32**. Two are outright dead:

- **`scanFlag`** (`esp32_main.cpp:473`) — zero readers, zero writers anywhere in the tree.
  `code-audit-fixes-20260627.md:29` records "made atomic" as a completed fix on a variable
  nobody uses.
- **`ch_util_rx_start`** — never written on ESP32; the source says so at `:3775`.

And `displayMux` is worse than redundant: `queueDisplayText` (`lora_functions.cpp:127-141`)
copies an `aprsMessage` containing **seven Arduino `String`s** — seven heap allocations —
inside `portENTER_CRITICAL(&displayMux)`, i.e. with interrupts disabled and a cross-core
spinlock held. On nRF52 the same code runs under `taskENTER_CRITICAL()`.

### N-14 — nRF52 TX ring is multi-writer with no mutual exclusion — **CONFIRMED** — High

`iWrite` is loaded three times across `ringBuffer[iWrite][0]=…; memcpy(ringBuffer[iWrite]+2,…); addTxRingEntry()`.
The `LORA` task (`lora_functions.cpp:1169`) preempts `loop_task`
(`loop_functions.cpp:3219`) mid-`memcpy`; both fill the same slot and a spliced frame goes
on the air. **The C1 "atomic iWrite/iRead" fix does not address this** — atomic indices are
not mutual exclusion.

The correct pattern already exists in this codebase and should be the template: the nRF52
CAD flag protocol at `nrf52_main.cpp:390-394` and `:1332-1338` (atomics plus symmetric
snapshot critical sections on both sides).

### N-15 — a removed guard is justified by a platform-specific assumption — **VERIFIED** — High

`src/phone_commands.cpp:529`

```c
// Spin-wait removed: readPhoneCommand now runs in Main Loop,
// no cross-core conflict with sendToPhone() possible
```

True on ESP32 (`bleQueue` defers). **False on nRF52**, where `api_functions.cpp:254` calls
`readPhoneCommand` directly in the Bluefruit callback task at priority 2. The guard was
removed globally on a platform-specific premise.

### N-16 — blocking work inside critical sections on nRF52 — **CONFIRMED** — High

`Radio.Send()` inside `taskENTER_CRITICAL()` (`lora_functions.cpp:1685`, `:1726`, `:1787`)
reaches `SX126xWaitOnBusy()` → `delay(1)` → `vTaskDelay()` **with the tick frozen**.

---

## 3. Refuted claims — do not re-investigate

| Claim                                                                                                  | Refuting evidence                                                                                                                               |
| ------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| The `(BOARD_E22_S3)` guard is a hard build error; two default envs do not compile                      | `pio run -e E22_1262_S3-DevKitC-1-N16R8` → **SUCCESS** (93 s). PlatformIO strips the quotes; the macro reaches the compiler unquoted. See N-10. |
| `snprintf(value, 100, …)` on `char value[40]` (`web_functions.cpp:1660`) is an error-severity overflow | Source is `char node_mcp17t[16][16]` — max 16 bytes. Rule violation (bound must be `sizeof(dst)`), **not** a memory-safety bug.                 |
| `snprintf(val, 160, …)` in `extudp_functions.cpp:195` overflows                                        | `char val[160+1]`, and an explicit guard above caps dst ≤ 9 and msg ≤ 150. Safe.                                                                |
| `[nrf52]` triple declaration is a glob-order race                                                      | ConfigParser merges option-by-option. `pio project config` output is deterministic. See C-05.                                                   |
| `MAX_APRS_FRAME_SIZE 340` vs 255-byte parser buffers is exploitable                                    | All six callers traced; none can supply more than 255 bytes today. **Latent**, not live.                                                        |
| The concept's "218 commands"                                                                           | 217 arms / 227 verbs. See C-08.                                                                                                                 |
| `-Wall -Wextra` produces an unmanageable warning backlog                                               | Measured: **9 warnings total**, 4 in `src/`. The June audit's `-Werror` exception rests on a backlog that does not exist.                       |

---

## 4. What replaces the golden-vector plan

C-03 removed the oracle. The zero-tolerance requirement needs a real one. Three
mechanisms, in ascending cost:

1. **Differential testing against the current build.** Compile the _pre-fix_ and _post-fix_
   decoder into one native binary and assert they agree on a large corpus of generated and
   captured frames. This is a true before/after comparison and needs no external oracle —
   it directly encodes "we did not change behaviour we did not mean to change".
2. **A real capture path.** Add a hex dump of **accepted** frames (currently only CRC-failed
   frames are dumped) behind `MC_TEST_HOOKS`, then re-capture on the bench. That produces
   the corpus 06 assumed already existed. Cheap — one `printfdeb` next to the existing one.
3. **Hand-authored specification vectors.** For the frame layouts documented in a future
   wire-format document, write vectors from the _specification_, not from the decoder's
   output. Only these can catch a pre-existing decode bug; the other two are regression
   fences.

`--inject` (07 H-01) remains the right hook, but at the wrong seam: `CRC_ERROR`,
`CRC_PAYLOAD` and `ERR_PAYLOAD` live in `checkRX` _above_ `OnRxDone`, so scenario 3 only
appears covered. Inject at the `checkRX` boundary instead.

Also corrected from 07 §7: scenario 20 is tautological (`--lora` reads back
`meshcom_settings`, never the SX1262); scenario 11 names the wrong marker
(`TX_GATE_ENTER` fires _after_ the backoff — use `RX_TIMEOUT_FIRE wait=`); scenario 12 is
unachievable (10 sites share the PRNG, so seeding fixes the sequence but not the
position). Of 25 scenarios: 12 falsifiable, 6 after correction, **7 cannot fail or cannot
be built**.

A native build additionally falls into the `#else` of `configuration_global.h:79-116` and
silently selects `MAX_MHEARD` 30 / `MAX_DEDUP_RING` 70 / `MAX_RING` 30 — against 80/100/20
on both bench boards. Every ring dimension differs. The native environment must pin an
explicit board profile.

---

## 5. Remediation order

Each row is one commit and one upstream PR. Upstream has merged 24 PRs from this author
(`+1` to `+3984` lines), so small surgical PRs are the proven path.

### Wave 0 — enablement (no behaviour change, no hardware)

| #   | Item                                                                                                                   | Evidence              |
| --- | ---------------------------------------------------------------------------------------------------------------------- | --------------------- |
| 0.1 | CI: build all 32 envs on PR and push                                                                                   | `TEST-38`             |
| 0.2 | `-Wall -Wextra` on firmware targets (currently only safeboot), fix the 4 warnings, then `-Werror` on `build_src_flags` | F6, C-17              |
| 0.3 | `-Wundef` + convert `BOARD_*` to flags with separate name macros                                                       | N-10                  |
| 0.4 | Pin `nordicnrf52`                                                                                                      | 02 B-04               |
| 0.5 | `[env:native]` + Unity + `Arduino.h` shim, explicit board profile                                                      | `TEST-37`, C-14, C-03 |

### Wave 1 — RF-reachable criticals (each a standalone PR)

| #   | Item                                   | ID              | Size        |
| --- | -------------------------------------- | --------------- | ----------- |
| 1.1 | `printfdeb` non-literal format string  | `SEC-02`        | 1 line      |
| 1.2 | CONF zero-fill overflow                | `N-03`          | delete loop |
| 1.3 | `{MCP}` password bypass                | `N-01`/`SEC-01` | small       |
| 1.4 | `{SET}` unauthenticated routing change | `N-02`          | small       |
| 1.5 | `memcpy` length underflow chain        | `N-04`/`BUG-08` | small       |
| 1.6 | mheard heap over-read                  | `N-05`          | 1 line      |
| 1.7 | web `t_io` bound check                 | `N-06`          | 2 lines     |
| 1.8 | BLE command gate                       | `N-07`          | small       |

### Wave 2 — remaining prior-verdict Track A

`SEC-03` … `SEC-06`, `BUG-07`, `BUG-10` … `BUG-13`, `CONC-14` … `CONC-19`, `N-08`, `N-09`,
`N-14`, `N-15`, `N-16`. `CONC-14` is the root fix that resolves `CONC-15`/`16`/`17`/`18`.

### Wave 3 — structural (propose upstream as a plan first)

`N-13` (remove 14 over-synchronisations, delete `scanFlag`), `DRY-20` … `DRY-25`,
`SIMP-26` … `SIMP-30`, `ALT-31` … `ALT-35`, `STATE-28`, plus the corrected C-02 extraction
of ~221 radio-independent shared loop lines.

### Deferred, with triggers

| Item                                                      | Trigger to revisit                                         |
| --------------------------------------------------------- | ---------------------------------------------------------- |
| Arduino 3.x migration                                     | after Wave 0 gives a RAM baseline and a CI gate            |
| Arduino 2.0.14 → 2.0.17 on the four lagging boards (C-04) | with Wave 0's CI matrix in place                           |
| LVGL 8 → 9                                                | never, unless the T-Deck UI is rewritten for other reasons |
| Radio interface / HAL                                     | only after C-02's cheap extraction proves the seam         |
| Licensing (N-11)                                          | maintainer decision, not an engineering task               |
| `FLASH_VERSION` migration (N-12)                          | before any change to `meshcom_settings` layout             |
