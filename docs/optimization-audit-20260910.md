# Optimization audit 2026-09-10: DRY and RAM

Branch `fork-main`, HEAD `c51c5881`. Audit only: no source was changed, nothing was built for
this report. All byte figures marked "measured" come from the 32 release images built on
2026-09-10 09:00-09:09 from this HEAD (`.pio/build/*/firmware.elf`, `nm -S` / `size -A`).
Everything else is a derivation from source and is marked as such.

Companion file: `docs/optimization-audit-20260910-appendix.md` holds the sixteen underlying
scout and analyst reports verbatim (inventories, per-item design notes, rejected lists).

## 1. Result in brief

- **The two proposals from the request both hold, with one correction each.** Duplicate code is
  real and large: one 5,935-line dispatcher function, ~1,700 lines of ESP32/nRF52 twin code with
  23 named drift bugs, three parallel UI trees, and settings fields enumerated by hand in six
  places. Callsign storage is worth compacting, but the biggest "callsign" buffer contains no
  callsign at all, so the win comes from binary rows and index tables, not from pointers alone.
- **Measured low-to-medium-risk RAM bundle, wire contract untouched:** about 21.8 kB static DRAM on
  a classic ESP32 E22, 18.3 kB on Heltec V3, 16.9 kB on RAK4631, plus 48 kB on T-Deck Pro from one
  config line and a further 8.5 kB on the E22_XML board that sits 1.2 kB from its DRAM limit.
- **The binding constraint is not DRAM but IRAM on the three T-Beam classic envs:** 131,051 of
  131,072 bytes used. None of that IRAM is our code. The only lever that moves it is the PSRAM board
  flag (4,952 B), and it is blocked on one bench reading, not on code.
- **Thirteen real defects surfaced as a by-product**, eleven verified on the cited lines (section
  6). Four are one-line fixes; two change nRF52 gateway behaviour and need a soak.
- **Three claims in earlier docs are refuted by the images:** U8g2 fonts are already in flash (the
  6.6 kB "move to flash" item is void), unreferenced fonts and functions already cost zero bytes
  (`--gc-sections` is a framework default on both toolchains), and interning is not the rejected
  hashing idea.
- **Recommended shape of the one-shot PR** is in section 7: the structural refactors that only a
  large PR can carry (settings schema table, single settings struct, shared UDP frame handler,
  table-driven command dispatcher, shared UI directory), the measured RAM bundle, and the
  one-line defect fixes. High-risk items (frame pool, hop-index path rows, common gateway loop)
  stay out and are listed with their gates.

## 2. Ground truth and constraints

### 2.1 Frozen wire contracts

Nothing below may change bytes on any of these surfaces. Storage behind them may change freely.

| Surface                     | Defining code                                                                                                 | Frozen detail                                                                                                                                                                                                       |
| --------------------------- | ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| BLE to phone                | `phone_commands.cpp`, `ble_json_frame.h`, `command_functions.cpp` tail (:5497-6174)                           | frame status bytes 0x40/0x44/0x91, opcodes 0x10..0xF0, 27 JSON keys, MTU budget 244                                                                                                                                 |
| BLE settings characteristic | `nrf52_ble.cpp:296` `setFixedLen(sizeof(s_meshcom_settings)+1)`                                               | on nRF52 the struct bytes are the payload: field order and size frozen, append only                                                                                                                                 |
| Net console TCP 2323        | `net_console.cpp`                                                                                             | `NONCE: <32 hex>\r\n` challenge, banner, plaintext                                                                                                                                                                  |
| Mesh UDP 1799               | `udp_functions.cpp`, `nrf52/nrf_eth.cpp`, `conf_frame.cpp`                                                    | `GATE`/`BEAT`/`CONF` indicators, payload type at +4, CONF TLV tags 0x00-0x04                                                                                                                                        |
| EXTUDP                      | `extudp_functions.cpp`, `extern_notice_json.h`, `extern_tele_json.h`                                          | key names and order per frame type, inbound key set                                                                                                                                                                 |
| LoRa air frame              | `aprs_functions.cpp` encode/decode, `lora_functions.cpp`                                                      | payload type, msg_id, flags, path fields, FCS                                                                                                                                                                       |
| Serial and console text     | every `printfdeb` / `Serial.printf` site                                                                      | tools parse it; byte-identical output is the acceptance test for every refactor in section 5                                                                                                                        |
| Settings on flash           | `esp32_flash.cpp` (NVS, field by field), `nrf52_flash.cpp` (raw struct file), `FLASH_STRUCT_VERSION 20260724` | ESP32 may reorder internally; nRF52 layout change requires a version bump, which wipes settings unless a migration is written (`nrf52_flash.cpp:112-135` shows the mechanism, currently for one legacy layout only) |

### 2.2 Toolchain facts that void several old assumptions

- Both frameworks compile with `-Os -ffunction-sections -fdata-sections` and link with
  `-Wl,--gc-sections` by default (`platformio-build-esp32*.py`, nRF52 `platform.txt`). An
  unreferenced function or `const` table costs zero flash and zero RAM. Every "delete dead X"
  item in this report is source hygiene unless it says otherwise.
- The U8g2 fonts `u8g2_font_10x20_mf` (4,194 B) and `u8g2_font_6x10_mf` (2,393 B) sit at flash
  addresses (`0x3f41e824`, `0x3f41f886` on E22_XML) inside `.flash.rodata`. `nm` prints them as
  `D` because the section is writable-flagged. The `docs/archive/ram-opti.md` item 10 claim of 6.6 kB DRAM
  is wrong on every measured ESP32 target. Close the item.
- Root `src/*.cpp` files compile for every hardware env (`+<*>` filters). `batt_function_old.cpp`,
  `spectral_scan.cpp`, `test_inject.cpp`, `tinyxml_functions.cpp`, `Regexp.cpp` and
  `regex_functions.cpp` are all in the images; the first four are emptied by internal `#if`
  guards, the last two are live on hardware (`checkRegexCall` is on the RX path).
- The largest single static objects in the classic ESP32 images that are not ours:
  `g_cnxMgr` 3,800 B (WiFi), `vflash_mem` 2,048 B (BT controller), `packet$` 1,460 B (libmdns,
  pulled in by `MDNS.begin()`), `esp_err_msg_table` in flash. Not reachable without a custom IDF,
  which stays rejected.

### 2.3 Measured baseline (release images of this HEAD)

| env                              |              .data+.bss (B) | IRAM text (B) | flash text+rodata (B) | note                                  |
| -------------------------------- | --------------------------: | ------------: | --------------------: | ------------------------------------- |
| ttgo_tbeam / \_SX1262 / \_SX1268 | 114,264 / 114,392 / 114,384 |   **131,051** |             1,460,747 | IRAM segment 131,072: **21 B free**   |
| E22_XML-DevKitC                  |                     123,416 |       127,615 |             1,627,775 | dram0_0_seg 124,580: **1,164 B free** |
| E22-DevKitC                      |                     113,112 |       126,099 |             1,411,191 | ~11.5 kB DRAM free                    |
| ttgo-lora32-v21                  |                     112,880 |       126,099 |             1,400,143 |                                       |
| heltec_wifi_lora_32_V3           |                     116,244 |        85,114 |             1,343,727 | S3, ~210 kB free                      |
| ttgo_tbeam_supreme               |                     116,572 |        87,658 |             1,376,207 | S3                                    |
| t_deck / t_deck_plus             |                     135,156 |        93,934 |             2,062,727 | S3, LVGL heap in PSRAM                |
| t_deck_pro                       |                 **189,664** |        90,594 |             1,877,563 | S3, LVGL heap 48 kB in internal DRAM  |
| wiscore_rak4631                  |                      88,844 |             0 |               657,280 | nRF52, ample                          |
| t_echo / heltec_t114             |             92,676 / 87,236 |             0 |     613,832 / 587,848 | nRF52                                 |

All 30 hardware envs are in the appendix table. The three T-Beam classic envs and E22_XML are the
only ones at a cliff; they decide what "saves RAM" means for the fleet.

### 2.4 Where the RAM is (measured symbol sizes, bytes)

| symbol                            | E22-DevKitC | E22_XML | ttgo_tbeam | heltec V3 | t_deck | t_deck_pro | rak4631 |
| --------------------------------- | ----------: | ------: | ---------: | --------: | -----: | ---------: | ------: |
| BLEtoPhoneBuff                    |       6,100 |   6,100 |      6,100 |     6,100 |  6,100 |      6,100 |   6,100 |
| BLEComToPhoneBuff                 |       6,100 |   6,100 |      6,100 |     6,100 |  6,100 |      6,100 |   6,100 |
| ringBufferUDPout                  |       5,500 |   5,500 |      5,500 |     5,500 |  5,500 |      5,500 |   5,500 |
| ringbufferRAWLoraRX               |       5,200 |   5,200 |      5,200 |     2,600 |  2,600 |      2,600 |   2,600 |
| ringBuffer (TX)                   |       5,200 |   5,200 |      5,200 |     5,200 |  5,200 |      5,200 |   5,200 |
| mheardBuffer + mheardNCount       |       1,920 |   3,200 |      1,920 |     5,120 |  5,120 |      5,120 |   5,120 |
| mheardPathBuffer1                 |       2,080 |   2,600 |      2,080 |     5,200 |  5,200 |      5,200 |   5,200 |
| mheardCalls + mheardPathCalls     |         700 |   1,000 |        700 |     1,800 |  1,800 |      1,800 |   1,800 |
| mheardLat + mheardLon             |         480 |     800 |        480 |     1,280 |  1,280 |      1,280 |   1,280 |
| page history ring (4 arrays)      |       2,904 |   2,904 |      2,904 |     2,904 |  2,904 |      2,904 |   2,904 |
| u8g2 objects + shared buffer      |       1,400 |   1,400 |      1,400 |     1,400 |      - |          - |   1,400 |
| meshcom_settings                  |       2,032 |   2,032 |      2,032 |     2,032 |  2,088 |      2,088 |   2,000 |
| externQueue                       |       1,028 |   1,028 |      1,028 |     1,028 |  1,028 |      1,028 |   1,028 |
| adc_chars (defect, section 6)     |           - |       - |      1,296 |     1,296 |      - |      1,296 |       - |
| HardWare[] + strCountry[] Strings |         848 |     848 |        848 |       848 |    848 |        848 |     636 |
| softser String tables             |           - |   2,720 |          - |         - |  2,720 |          - |       - |
| cap_ring + capture line           |         768 |       - |        768 |       768 |    768 |        768 |     768 |
| web_header_collect                |       1,024 |   1,024 |      1,024 |     1,024 |  1,024 |      1,024 |   1,024 |
| work_mem_int (LVGL pool)          |           - |       - |          - |         - |      - | **49,152** |       - |

## 3. Callsign storage: what the pointer idea does and does not buy

The request proposed replacing every stored callsign by a pointer or index into one table. The
inventory (appendix S3, analysis R2) shows:

- `mheardBuffer[MAX_MHEARD][60]`, the largest table in this area, holds **no callsign**. Its only
  writer is the `snprintf` at `mheard_functions.cpp:395-397`: date, time, payload type, hw, mod,
  rssi, snr, distance, path length, mesh flag, ncount. All scalars stored as text.
- Callsigns live in `mheardCalls[][10]`, `mheardPathCalls[][10]` (parallel arrays), the settings
  struct (70 B, frozen), `aprsMessage`/`mheardLine` `String` fields (transient), and the text hop
  list `mheardPathBuffer1[MAX_MHPATH][52]`.
- An intern table alone (8 B entry, 96/192 slots, refcount and LRU bytes) costs **more** than the
  two `[10]` arrays it replaces: net −426 B classic, −492 B on S3/nRF52. It pays only as the
  enabler for the hop list, where 52 B of text become 8 B of indices.
- Interning stores the callsign verbatim exactly once and never emits the index; it is not the
  hashing idea rejected in `docs/archive/ram-opti.md` item 12, which derived a lossy tag from the callsign.

So the callsign work splits into R2-01 (binary mheard row, independent of any callsign change,
low risk, the largest gain on the cliff boards) and the R2-02/R2-03 bundle (hop indices plus intern
table, high risk, S3/nRF52 gain). Section 4 ranks them accordingly.

## 4. RAM proposals

Columns: static bytes freed on classic ESP32 / ESP32-S3 / nRF52 (measured symbol sizes unless
marked est.), risk L/M/H, effort S/M/L, and whether it belongs in the one-shot PR. Wire contract is
"none" for every row; rows that change console text say so.

| ID          | Change                                                                                                        | Where                                                                         | classic / S3 / nRF52 (B)                                     | Risk | Effort | One-shot             | Verification                                                                                             |
| ----------- | ------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------------ | ---- | ------ | -------------------- | -------------------------------------------------------------------------------------------------------- |
| R4-01       | T-Deck Pro LVGL pool to PSRAM (`LV_MEM_CUSTOM 1` + `ps_malloc`, as t_deck already does)                       | `variants/t_deck_pro/platformio.ini:33` includes `config/lv_conf.h:49-52`     | 0 / **49,152** (t_deck_pro) / 0                              | M    | S      | yes                  | rebuild, `nm` shows `work_mem_int` gone; no bench unit, functional check deferred                        |
| R1-04       | `ringbufferRAWLoraRX` allocated on first web rxlog request, writer no-ops on NULL                             | `loop_functions.cpp:428-430,3216-3236`, `lora_functions.cpp:697`              | **5,200** / 2,600 / 2,600                                    | M    | M      | yes                  | rxlog page unchanged; 12 h nRF52 soak with webserver off (writer runs on timer task, C10)                |
| R1-02       | Merge `BLEComToPhoneBuff` into `BLEtoPhoneBuff` (byte 1 already tags the type), one lock, one overflow policy | `loop_functions.cpp:441-450,580-676`, `phone_commands.cpp:60-210`             | 4,980 / 4,980 / 4,980                                        | M    | M      | yes                  | interleaved config-reply and text burst drain in FIFO order; web history shows text only                 |
| R4-02/03    | Skip U8g2 objects, shared 1 kB frame buffer and page-history ring on boards without an OLED (E22 family)      | `loop_functions.cpp:361-403,775-779`                                          | 4,304 (no-OLED boards) / - / -                               | M    | S      | yes, with board list | per-board hardware confirmation of "no OLED"; E22 already runs `bDisplayOff`                             |
| R2-01       | `mheardBuffer` text rows to a 16 B binary struct, text rendered on output with the same formats               | `mheard_functions.cpp:35,56,138-203,395-398,524-527,690-803`                  | 1,440 / 3,840 / 3,840                                        | M    | M      | yes                  | `--mheard` dump and BLE 0x44 MH JSON byte-diff; `test_decodemheard`; persistence file needs a format tag |
| R1-01       | BLE slots sized to the producers' clamps (260 and 246 instead of 305)                                         | `loop_functions_extern.h:267,272`, `loop_functions.cpp:586-589,651-656`       | 2,080 / 2,080 / 2,080                                        | L    | S      | yes                  | `--bledebug` frame dump byte-identical for max-length text, JSON, mheard                                 |
| R3-11       | `MC_CAPTURE` default off on classic ESP32 (or follow `INSTRUMENT_ENABLED`)                                    | `capture_functions.h:49-50`                                                   | 1,388 / 0 / 0                                                | M    | S      | decision             | `--capture` must still answer "compiled out"; string-scan the image (INS-01 lesson)                      |
| R3-01       | `adc_chars` declared as scalar, not `[sizeof(struct)]` (defect)                                               | `batt_function_old.cpp:198`                                                   | 1,260 on `batt_function_old` boards (T-Beam, V3, t_deck_pro) | L    | S      | yes                  | `nm` 0x510 to 0x24                                                                                       |
| R3-13       | `mheardPathBuffer1` row 52 to 40 B (the surviving half of ram-opti item 12)                                   | `mheard_functions.cpp:66,593-594,613`                                         | 480 / 1,200 / 1,200                                          | M    | S      | yes                  | inject a 7-hop path frame, `--path` output unchanged; corpus check for paths over 39 chars               |
| R2-07       | `HardWare[36]` `String` table to `const char* const[]`                                                        | `mheard_functions.cpp:84-86,850`                                              | 576 / 576 / 432 (+~900 B boot heap)                          | L    | S      | yes                  | `--mheard` hardware column across all 36 ids                                                             |
| R3-12       | `mheardLat/Lon` `double` to `float`                                                                           | `mheard_functions.cpp:37-38`                                                  | 240 / 640 / 640                                              | M    | S      | yes                  | printed lat/lon equal to 5 decimals on a bench frame                                                     |
| R1-06       | `externQueue` entry buffer 500 to 264 B                                                                       | `extudp_functions.cpp:65-73`                                                  | 472 / 472 / 472                                              | L    | S      | yes                  | EXTUDP JSON on 1799 byte-identical for a max-length frame                                                |
| R1-03       | `ringBufferUDPout` `+20` padding to `+1` (the over-read it guarded was fixed by WF-01)                        | `loop_functions_extern.h:260`, `udp_functions.cpp:673`, `nrf52_main.cpp:3029` | 380 / 380 / 380                                              | L    | S      | yes                  | UDP hex on the server for a 255 B frame                                                                  |
| R3-06       | `strCountry[17]` `String` table to `const char* const[]`                                                      | `lora_setchip.cpp:62-78`, `settings_sanitize.cpp:99`                          | 272 / 272 / 204                                              | L    | S      | yes                  | native `test_settings_sanitize`                                                                          |
| R3-04       | Vendor tinyxml2 without `contrib/` (its `main()` drags in iostream and locale)                                | `variants/E22_XML-DevKitC/platformio.ini`, new `lib/tinyxml2/`                | 5,768 (E22_XML only) + 572 IRAM, ~40 kB flash                | L    | M      | yes                  | zero `_ZSt4cout` / `locale_init.o` in the map                                                            |
| R3-10       | Softserial `String` tables (`strPARM/strPARM_ID/strUNIT[10][5]`, `strSID/strSNAME`) to fixed `char` arrays    | `softser_functions.cpp:334-339`                                               | 2,720 (E22_XML, t_deck) / - / -                              | M    | M      | yes                  | softserial XML bench frame                                                                               |
| R3-07       | `decodeAPRS` scratch buffers: two of three hold a callsign, size them 24 B                                    | `aprs_functions.cpp:184-192`                                                  | stack −462 B per RX frame, all platforms                     | L    | S      | yes                  | `test_aprs_corpus` byte-identical; nRF52 LORA task high-water mark                                       |
| R2-04       | `aprsMessage`/`mheardLine` callsign and path `String` fields to fixed `char[]`                                | `aprs_structures.h:20-26,70-76`                                               | heap churn −450..550 B and ~14 malloc/free per RX (est.)     | M    | M      | yes                  | aprs native tests green; `--heap` watermark over 1 h; BLE connect soak (NimBLE precedent)                |
| R3-05/08/09 | `const String&` parameters on the RX path, `getTimeStr(char*,n)` for hot log lines                            | `aprs_functions.cpp:28,53,556`, `loop_functions.cpp:3143`                     | heap churn only (est.)                                       | L    | S/M    | yes                  | native tests; heap trace over a 100-frame inject                                                         |
| R3-15       | `JsonDocument` per EXTUDP datagram: reserve once                                                              | `extern_tele_json.h:41`, `extudp_functions.cpp:474-478,810-812`               | transient heap, unquantified                                 | M    | M      | later                | native JSON tests byte-identical; heap trace                                                             |
| D5-01       | `ota_html[]` in the safeboot image lacks `const`                                                              | `safeboot/ota.h:1`                                                            | ~24,318 (safeboot image only)                                | L    | S      | yes                  | safeboot map section for `ota_html`                                                                      |
| R3-02       | T-Beam PSRAM unflag (`-DBOARD_HAS_PSRAM`, `-mfix-esp32-psram-cache-issue`)                                    | `variants/ttgo_tbeam*/platformio.ini`                                         | **IRAM +4,952** on the three T-Beam envs                     | M    | S      | blocked              | read `[PSRM]` on T-Beam-92 after `[HEAP] (init)`; only if PSRAM init already reports 0                   |
| R3-03       | Drop OneWire on the T-Beam family                                                                             | `variants/ttgo_tbeam*/configuration.h` (`OneWire_GPIO 4`)                     | IRAM +1,121, DRAM +103                                       | M    | S      | decision             | feature removal for a board family; map attribution                                                      |
| R1-05       | Refcounted frame pool shared by TX ring and UDP-out ring (the only two rings holding identical bytes)         | `txring_functions.cpp:442-520`, `udp_functions.cpp:1727-1760`                 | 4,128 / 4,128 / 4,128 (est.)                                 | H    | L      | no                   | pool invariants in `test_txring`; 24 h dual-role gateway soak                                            |
| R2-02/03    | Hop list as 8 B index rows plus callsign intern table (with 256 B hash heads)                                 | `mheard_functions.cpp:66,590-613`, new `callsign_table.h`                     | 1,334 / 3,908 / 3,908 (bundle, est.)                         | H    | L      | no                   | 12 h bench diff of `--path` and web mheard; dangling-index assert build                                  |
| R1-07       | Byte-pool slab for the BLE ring                                                                               | `loop_functions.cpp:580-676`                                                  | superseded by R1-02                                          | H    | L      | no                   |                                                                                                          |

### 4.1 Bundle arithmetic (rows marked "yes", measured sizes)

| Platform                    | L-risk rows                       | M-risk rows                                                 | Total             | Context                                            |
| --------------------------- | --------------------------------- | ----------------------------------------------------------- | ----------------- | -------------------------------------------------- |
| E22-DevKitC (classic)       | 3,780 (R1-01/03/06, R2-07, R3-06) | 18,032 (R1-04, R1-02, R2-01, R4-02/03, R3-11, R3-13, R3-12) | **~21.8 kB**      | of 113.1 kB static; headroom today ~11.5 kB        |
| E22_XML-DevKitC (classic)   | 3,780                             | 18,032 + R3-04 5,768 + R3-10 2,720                          | **~30.3 kB**      | headroom today 1,164 B; R3-04 alone ends the cliff |
| ttgo_tbeam family (classic) | 3,780 + R3-01 1,296               | 18,032                                                      | **~23.1 kB DRAM** | DRAM is not the problem there; IRAM needs R3-02    |
| heltec_wifi_lora_32_V3 (S3) | 3,780 + R3-01 1,296               | 2,600 + 4,980 + 3,840 + 1,200 + 640                         | **~18.3 kB**      | no cliff; value is heap headroom and BLE stability |
| wiscore_rak4631 (nRF52)     | 3,568                             | 13,260                                                      | **~16.9 kB**      | no cliff; also −462 B RX stack (R3-07)             |
| t_deck_pro (S3)             | R4-01 alone                       |                                                             | **~49 kB**        | drops static RAM from 190 kB to ~141 kB            |

## 5. DRY proposals

Ranked by what the one-shot window uniquely enables. LOC figures come from `diff -w -B` of the
pair bodies or from the branch inventory; flash figures are estimates (8-12 B per removed line of
Xtensa `-Os` code) unless stated.

### 5.1 Structural (only feasible in a large PR)

| ID          | Change                                                                                                                                                                                                                                                                                                                                    | Where                                                                                                                                                                        |                     LOC removed | Flash (est.)                     | Risk | Effort | Gate                                                                                                                                                        |
| ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------: | -------------------------------- | ---- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1-05       | One settings schema table. `config_json.cpp:88-230` already has a 109-entry `X(name,type,member,min,max,esc)` table, file-local. Promote it to `src/settings_schema.h`, add `persist`, `cmd`, `ble_register`, `echo` columns; drive ESP32 NVS load/save, nRF52 migration copy, web setup, sanitize ranges and the command setters from it | `esp32_flash.cpp` (112 fields, 322 refs), `nrf52_flash.cpp` (106, 146), `web_setup.cpp` (109 branches), `settings_sanitize.cpp:50-102`, `command_functions.cpp` (119 fields) | ~450 + ~350 web + ~200 sanitize | −4..8 kB ESP32, +2.6 kB rodata   | M    | L      | full round trip: save, reboot, dump `--info` and config JSON, byte-compare; BLE settings characteristic byte-compare on RAK4631                             |
| D1-04       | One `s_meshcom_settings` definition in `src/settings_struct.h`. nRF52 copy lacks `node_ntp`, `node_immediate_save`, `node_modus`, `node_mute`, `node_persist_to_flash`, `node_disp_rot` (verified), which is why shared code cannot touch them                                                                                            | `esp32/esp32_flash.h:8-242` vs `nrf52/WisBlock-API.h:178-392` (182 identical lines)                                                                                          |                            ~215 | 0                                | M    | M      | append only; nRF52 needs a written migration from layout 20260724 (extend the compat mechanism at `nrf52_flash.cpp:112`) or the bump wipes every nRF52 node |
| D1-06       | Replace the frozen `s_meshcomcompat_settings` snapshot plus its hand-written copy with a version-tagged migration driven by D1-05                                                                                                                                                                                                         | `nrf52/WisBlock-API.h:413-609`, `nrf52_flash.cpp:119-121`                                                                                                                    |                            ~300 | −700 B nRF52 stack               | M    | M      | flash an old image, upgrade, byte-compare `--info` and the BLE characteristic                                                                               |
| D2-06       | Table-driven boolean toggles: 120 `on/off` branches (58 matched pairs) become `const CmdEntry` rows with style and payload columns; six echo styles exist, so the row carries which one                                                                                                                                                   | `command_functions.cpp:457-5195`, new `src/command_table.cpp`                                                                                                                |                          ~1,500 | **−6.0 kB** ESP32, −3.5 kB nRF52 | M    | L      | golden capture D2-V (section 7.3); native `test_command_table` for shadowing and offsets                                                                    |
| D2-07       | Table-driven setters: 71 `--set<X> <value>` branches, one parse/range/store helper                                                                                                                                                                                                                                                        | `command_functions.cpp:321-5606`                                                                                                                                             |                          ~1,100 | −2.6 kB                          | M/H  | L      | D2-V; each branch's `sscanf` offset and clamp transcribed and tested                                                                                        |
| D2-10       | Exact-match `commandCheck` variant; only the 13 order-dependent prefix pairs keep `CMD_PREFIX`                                                                                                                                                                                                                                            | `command_functions.cpp:157-168` and the 13 pairs listed in appendix D2 §1                                                                                                    |                               0 | +0.1 kB                          | M    | M      | adversarial input set (`--info`/`--infoX`, `--lora`/`--loradebug on`, `--heap`/`--heap tag`)                                                                |
| D1-01       | Shared GATE/CONF/BEAT frame handler `handleMeshComUdpFrame()`; platforms keep socket read and link reset behind a plain `McNetOps` function-pointer struct (no vtable)                                                                                                                                                                    | `udp_functions.cpp:205-638` vs `nrf52/nrf_eth.cpp:352-824` (189 identical of 435/473)                                                                                        |                            ~350 | −0.3 kB, −260 B nRF52 stack      | M    | L      | byte-compare UDP 1799 TX and BLE frames on RAK4631 and Heltec V3 via the injectraw EXTUDP recipe                                                            |
| D1-03       | One `checkSerialCommand()`; net-console reader inside behind `DISABLE_NET_CONSOLE`                                                                                                                                                                                                                                                        | `esp32/esp32_main.cpp:4307-4434` vs `nrf52/nrf52_main.cpp:2895-3007` (102 identical)                                                                                         |                            ~102 | −600 B ESP32 stack peak          | L    | S      | `::9999 x` and `--pos` over USB and 2323 on both platforms, frame bytes equal                                                                               |
| D3-05       | `lora_setcountry()` 14-case switch to a `const CountryRfProfile[]` table with nRF52 and ESP32 columns; MAN (case 7) stays code                                                                                                                                                                                                            | `lora_setchip.cpp:193-503`                                                                                                                                                   |                            ~230 | −1.8..2.8 kB                     | M    | M      | dump `node_freq/bw/cr/sf/track/preamble` for country 1-15 on both platforms                                                                                 |
| D3-01       | One `aprsExtractTag()` for the 11 `/X=value/` extractor blocks in `decodeAPRSPOS()`                                                                                                                                                                                                                                                       | `aprs_functions.cpp:667-1041`                                                                                                                                                |                            ~140 | −1.1..1.7 kB                     | L    | S      | `aprsPosition` bit-compare over the field corpus                                                                                                            |
| D4-01/02    | `src/ui_common/` for the two `peri_gps.cpp` (0.87 similar, 111-line identical block) and the `scr_mrg` screen-manager pair; excluded by default, re-included for the three LVGL envs                                                                                                                                                      | `t-deck-pro/peri_gps.cpp:267-416` vs `t5-epaper/peri_gps.cpp:230-378`; `ui_scr_mrg.c/.h` vs `scr_mrg.cpp/.h`                                                                 |                            ~700 | 0 (envs are mutually exclusive)  | L    | M      | clean rebuild of `t_deck_pro` and `t5_epaper`; no bench unit for either, state it in the PR                                                                 |
| D6-01/02/03 | `variants/`: common `configuration_default.h` plus per-board deltas (E22 cluster 0.92-0.98, T-Beam 0.92, t_deck pair 0.98), `extends=` bases in `platformio.ini`, delete 21 vestigial `lv_conf.h` copies (15,010 lines, never read: LVGL is `lib_ignore`d there)                                                                          | `variants/*/configuration.h` (30 files, 3,397 lines), `variants/*/platformio.ini`, `variants/*/lv_conf.h`                                                                    |                 ~1,300 + 15,010 | 0                                | M    | L      | `-E` macro-set diff per env; `pio project config` diff. Highest upstream-conflict surface: sync immediately before submit                                   |

### 5.2 Helpers and hygiene (small, also fine as later minimal PRs)

| ID                      | Change                                                                                                                                                                                                                                                                                                                                                              | Where                                                                                                                        |     LOC | Risk | Note                                                                                                     |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- | ------: | ---- | -------------------------------------------------------------------------------------------------------- |
| D1-08 / D5-02           | Shared `batt_detect.h` for the 48 identical lines; both battery implementations stay (12 vs 15 boards)                                                                                                                                                                                                                                                              | `batt_function_old.cpp:70-120` vs `batt_functions.cpp:71-120`                                                                |      50 | L    | C06 names this pair as the one where a fix landed in the wrong TU                                        |
| D1-07                   | One `at_cmd.h` (51 of 52 lines identical)                                                                                                                                                                                                                                                                                                                           | `esp32/at_cmd.h`, `nrf52/at_cmd.h`                                                                                           |      52 | L    |                                                                                                          |
| D1-02                   | nRF52 hand-built ACK phone frame via the shared `buildAckPhoneFrame()`                                                                                                                                                                                                                                                                                              | `nrf52/nrf_eth.cpp:563-581` vs `ack_attribution.h:122-137`                                                                   |      14 | L    | closes a drift bug (section 6)                                                                           |
| D3-02                   | `finalizeAndSendAPRS()` for the 7 identical msgid/save/checkVia/encode epilogues                                                                                                                                                                                                                                                                                    | `loop_functions.cpp:3326,4740,4817,4969,5307` and 2 more                                                                     |      55 | L    | `msg_buffer` byte-diff at all 7 sites                                                                    |
| D3-03                   | `addBLEComToOutBuffer` uses `addRingPointer()` like its twin (its raw `Write++` wrap silently overwrites unread slots)                                                                                                                                                                                                                                              | `loop_functions.cpp:663-680`                                                                                                 |      10 | L    | subsumed by R1-02 if that ships                                                                          |
| D3-04                   | `gpsSendCmd()` wrapping the repeated `Serial1`/`GPSSerial` port select                                                                                                                                                                                                                                                                                              | `gps_functions.cpp:684-716`                                                                                                  |      24 | L    |                                                                                                          |
| D3-06                   | `radioSendNow()` for the three copies of the TX critical section                                                                                                                                                                                                                                                                                                    | `lora_functions.cpp:1854-1873,1902-1927,1967-1990`                                                                           |      55 | M    | bench-verify TX on both platforms; N-16 history                                                          |
| D3-07/08                | Table-drive the I2C address names and the `checkRegexCall` allowlist                                                                                                                                                                                                                                                                                                | `i2c_scanner.cpp:110-122`, `regex_functions.cpp:24-49`                                                                       |      30 | L    | D5 rates the I2C one as not worth it; either way small                                                   |
| D4-03/04                | Shared mheard-row fill helper and TRACK no-fix formatter for T-Deck and T-Deck Pro                                                                                                                                                                                                                                                                                  | `mheard_functions.cpp:922-971` vs `ui_deckpro.cpp:1300-1352`; `ui_deckpro.cpp:2182-2218` vs `lv_obj_functions.cpp:4340-4376` |      75 | M    | T-Deck Plus bench for the shared pieces                                                                  |
| D4-07/08                | Data-drive the keyboard remap ladder and the APRS symbol dropdown map                                                                                                                                                                                                                                                                                               | `t-deck/tdeck_main.cpp:967-1021`, `t-deck/event_functions.cpp:67-132`                                                        |      70 | M    | keyboard remap is user text entry: mandatory bench pass on DK5EN-14                                      |
| D5-04/06                | `i2cBusResetIfNeeded()` for 10 call sites; `sensorInitLog()` for the four identical init lines                                                                                                                                                                                                                                                                      | `bmx280/bmp390/sht21/aht20.cpp`                                                                                              |      45 | L    | console text byte-identical                                                                              |
| D5-08                   | Template for the 8 numeric `printdeb` overloads                                                                                                                                                                                                                                                                                                                     | `printfdeb_functions.cpp:160-233`                                                                                            |      20 | L    |                                                                                                          |
| D5-11                   | `fmtHms()` for the two identical `%02i:%02i:%02i` sites                                                                                                                                                                                                                                                                                                             | `loop_functions.cpp:1676,1678`                                                                                               |       4 | L    | the "many time formatters" premise did not hold; `getTimeString()` already covers 16 sites               |
| D2-01/04                | Delete the unreachable second `--setowndns` block and ~55 lines of commented-out branches                                                                                                                                                                                                                                                                           | `command_functions.cpp:4027-4051, 288-298, 3267-3288, 3351-3358, 710-728`                                                    |      80 | L    |                                                                                                          |
| D2-05                   | One spelling for bit clears (`&= ~MASK`); the `= x & 0x7Fxx` form also clears bit 15                                                                                                                                                                                                                                                                                | ~40 sites in `command_functions.cpp`                                                                                         |       0 | M    | audit each mask; native test over `node_sset*` transitions                                               |
| D2-09                   | Move `--specstart/end/step/samples` behind `INSTRUMENT_ENABLED`                                                                                                                                                                                                                                                                                                     | `command_functions.cpp:4654-4741`                                                                                            |       0 | M    | operator decision; string-scan the image afterwards (INS-01)                                             |
| D4-05, R4-04, D6-04..12 | Source-only deletions: `Font_Mono_Bold_20.c` (t-deck-pro), `Font_Geist_Light_20.c`, `firasans_12.h` (110 kB, never included), `nrf52/glcdfont.c`, five dead functions (`init_scan`, `direction_parse`, `LilyGo_logo`, `Veille_logo`, `ble_log_settings`), `src/Platforms/{M328P,M1280,M2560,SAMD21G18A,ESP8266}`, 47 of 48 `src/Fonts/*.h`, unused `<SD.h>` include | see appendix D6                                                                                                              | ~22,500 | L    | **0 bytes in any image** (verified: `Font_Mono_Bold_20` is absent from the t_deck_pro ELF). Hygiene only |

### 5.3 Investigated and dropped

- `startNetwork()` vs `startETH()`: no shared body (event-driven WiFi state machine vs blocking
  W5100S); only a 15-line IP-apply tail is extractable.
- Wholesale `setup()` or BLE-callback merges: already converged on `BleQueueItem`; the rest is an
  `#if` thicket, which is the C06 mechanism, not the cure.
- A uniform sensor descriptor table: seven sensor files share a shape, not a signature.
- Unifying the JSON writers: the three ArduinoJson wrappers already share one pattern;
  `config_json.cpp` is hand-rolled on purpose for CRC canonicalisation.
- Delegating the LVGL boards' GPS to `gps_functions.cpp`: a different library (TinyGPS++ vs the
  custom NMEA/UBX driver); a rewrite, not a merge.
- `phone_commands.cpp` opcodes into the text dispatcher: the BLE frame format is frozen.
- `std::map` or hash dispatch for commands: puts the key table in RAM on the DRAM-cliff boards.
- The `WisBlock-API.h` internal 78-line duplicate: it is the current and the compat flash layout,
  intentionally two structs (superseded by D1-06, not deleted).

## 6. Defects found on the way

Verified means the cited lines were read in this audit. All are independent of the refactors and
can ship as one-line or small fixes; four change stored values or nRF52 gateway behaviour and are
marked as decisions.

| #   | Defect                                                                                                                                                                               | Where                                                                          | Status                                               | Effect                                                                       |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------ | ---------------------------------------------------- | ---------------------------------------------------------------------------- |
| 1   | `esp_adc_cal_characteristics_t adc_chars[sizeof(esp_adc_cal_characteristics_t)]`: 36 copies of a 36 B struct                                                                         | `batt_function_old.cpp:198`                                                    | verified, 1,296 B in the T-Beam/V3/t_deck_pro images | 1,260 B DRAM per affected board                                              |
| 2   | `--setowndns ` handled twice; the second block is unreachable                                                                                                                        | `command_functions.cpp:3959` and `:4027`                                       | verified                                             | dead code                                                                    |
| 3   | `--softser app0` shadowed by the prefix match on `--softser app`                                                                                                                     | `command_functions.cpp:3245`, `:3254`                                          | verified                                             | `app0` silently runs `app`; fix changes behaviour, decision                  |
| 4   | `--specstep` writes `node_specsamples`, `--specsamples` writes `node_specstep`                                                                                                       | `command_functions.cpp:4696-4729`                                              | verified                                             | stored values swapped; fix changes stored value, decision                    |
| 5   | nRF52 gateway calls `sendUDP()` only in the pass where `getUDP()` returned "no packet"                                                                                               | `nrf52/nrf52_main.cpp:2069-2073` vs `esp32/esp32_main.cpp:3849`                | verified                                             | busy nRF52 gateway starves its TX ring; decision, needs soak                 |
| 6   | nRF52 heartbeat recovery nested under `if(!neth.hasIPaddress)`: a live link with a silent server never recovers                                                                      | `nrf52/nrf52_main.cpp:2104` vs ESP32 stage-2 `resetMeshComUDP()`               | verified                                             | gateway recovery; decision, needs soak                                       |
| 7   | nRF52 UDP zero-scan reads `buf[i+1]` past the datagram on odd sizes; ESP32 has `i + 1 < packetSize`                                                                                  | `nrf52/nrf_eth.cpp:400` vs `udp_functions.cpp:214`                             | verified                                             | one-byte over-read                                                           |
| 8   | nRF52 settings struct lacks six fields the ESP32 copy has                                                                                                                            | `nrf52/WisBlock-API.h` vs `esp32/esp32_flash.h`                                | verified by grep                                     | shared code cannot compile on nRF52; root cause of "fix landed on one side"  |
| 9   | Telemetry gate uses `bHeyFirst` on nRF52 where ESP32 uses `bTeleFirst && bAllStarted`; nRF52 also omits `extra_hey_time` spreading                                                   | `nrf52/nrf52_main.cpp:2018` vs `esp32/esp32_main.cpp:3462`; `:1974` vs `:3409` | verified                                             | first telemetry burst fires on the HEY flag                                  |
| 10  | `checkSerialCommand` keeps `char msg_buffer[600]` on the ESP32 loop stack; the N-22 fix made it `static` on nRF52 only                                                               | `esp32/esp32_main.cpp:4380` vs `nrf52/nrf52_main.cpp:2951`                     | verified                                             | 600 B stack peak on ESP32; net-console input dead on nRF52 (per D1)          |
| 11  | `--pingcall` bounds the write into `node_pingcall` with `sizeof(node_call)`                                                                                                          | `command_functions.cpp:3690`                                                   | verified                                             | harmless today (both 10 B), wrong expression                                 |
| 12  | nRF52 hand-built ACK phone frame sends `out[6]=0`, never the attribution suffix ESP32 sends                                                                                          | `nrf52/nrf_eth.cpp:563-581` vs `ack_attribution.h:122-137`                     | analyst report                                       | phone app gets no attribution callsign from nRF52 gateways                   |
| 13  | RX-01 unconfigured-source guard, `bGATEWAY_NOPOS`, `sendDisplayPosition()` for server frames, `hb_warn_logged` reset, `decodeAPRS()` return check: each present on one platform only | `udp_functions.cpp:287,339` vs `nrf52/nrf_eth.cpp:468` etc. (appendix D1-01)   | analyst report                                       | an nRF52 gateway can radiate XX0XXX GATE frames; see D1-01 for the full list |

## 7. The one-shot PR

The window allows one large upstream PR; everything after it returns to minimal changes. The
recommendation is therefore: spend the window on the changes that cannot be split into small
follow-ups, bank the measured RAM bundle, and ship the small helpers wherever they fit. Keep the
high-risk items out; each has a defined gate for a later, separate PR.

### 7.1 Composition

| Wave | Content                                                                                                             | Gate before the next wave                                                                                                  |
| ---- | ------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| 0    | Golden captures (7.3) on RAK4631, Heltec V3, T-Beam-92, T-Deck-14 at the base commit                                | captures archived; `resource_watch.py regions` baseline for all 32 envs from a same-base build                             |
| 1    | Defects 1, 2, 7, 10, 11 (no behaviour change) and hygiene deletions (D2-04, D4-05, R4-04, D6-04..12)                | 32-env build, `.bin` byte-compare where the change is source-only, `nm` for `adc_chars`                                    |
| 2    | RAM bundle L rows (R1-01, R1-03, R1-06, R2-07, R3-06, R3-07, D5-01) and R3-04, R3-10                                | frame dumps byte-identical per row; regions diff shows the predicted deltas                                                |
| 3    | Settings: D1-05 schema table, D1-04 single struct, D1-06 migration, D2-08 sanitize fold; D1-07, D1-08, D1-02, D1-03 | settings round trip on both platforms; BLE characteristic byte-compare; nRF52 upgrade from a 20260724 image keeps settings |
| 4    | Command table: D2-10 then D2-06, D2-07; D2-01                                                                       | golden capture diff empty outside deliberately changed lines; `test_command_table`                                         |
| 5    | RAM bundle M rows (R1-04, R1-02, R2-01, R2-04, R4-02/03 with the board list, R3-13, R3-12, R4-01)                   | per-row verification from section 4; 12 h nRF52 soak for R1-04; T-Deck Pro build-only, stated in the PR                    |
| 6    | D1-01 shared UDP frame handler (carries defects 12, 13); D3-01, D3-02, D3-05; D4-01/02 `ui_common`                  | injectraw EXTUDP byte-compare on both platforms; country dump; clean rebuild of the two LVGL boards without bench          |
| 7    | D6-01/02/03 variants restructure, synced against upstream immediately before submission                             | `-E` macro-set diff per env; ideally its own PR because it conflicts with any concurrent variant change upstream           |

Decisions the operator owns before wave 5 and 6: defects 3, 4 (behaviour and stored-value
changes), R3-11 and D2-09 (diagnostics compiled out on some boards), R3-03 (OneWire off on
T-Beam), the OLED-less board list for R4-02/03, and the nRF52 migration versus wipe for D1-04.

### 7.2 Left out, with the gate that would admit it

| Item     | Why out                                                                                                             | Admission gate                                                                                  |
| -------- | ------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| D1-09    | Common gateway service block: fixes defects 5 and 6 but rewrites the live recovery path on both platforms           | 24 h soak on RAK-90 and Heltec-93 with `meshlogger.py`, `[GW];rx;` and UDP payloads equal       |
| D1-10    | Common loop scheduler (18 shared timer predicates)                                                                  | 6 h emission-timestamp comparison; fix defect 9 standalone first                                |
| R1-05    | Refcounted frame pool: two-writer refcount inside the eviction critical section (C10)                               | pool invariants in `test_txring`, 24 h dual-role soak                                           |
| R2-02/03 | Hop-index rows plus intern table: O(192) strcmp per RX unless hash heads are budgeted; dangling-index failure modes | 12 h bench diff, assert-on-dangling build; only after R2-01 has shipped                         |
| R3-02    | T-Beam PSRAM unflag, the only IRAM lever                                                                            | one `[PSRM]` reading on T-Beam-92; if PSRAM already fails to init, the unflag removes dead code |
| R3-15    | EXTUDP `JsonDocument` reserve                                                                                       | heap trace on the EXTUDP bench recipe                                                           |

### 7.3 Verification that the whole PR rests on

- **Golden command capture (D2-V):** every command name driven over USB and over TCP 2323 (different
  code paths), canonical, out-of-range, non-numeric and the adversarial prefix set; the 13 BLE JSON
  registers captured the same run; repeated at the post-change commit on the same hardware and
  diffed. Destructive commands in a separate manual list. No such capture exists today; it is the
  prerequisite for wave 4 and the safety net for everything else.
- **Byte-compare on every wire surface** per changed row: BLE frame dumps, UDP 1799 hex on the
  server, EXTUDP JSON on the listener, `[GW];rx;` and `[UDP];tx;` log lines, the nRF52 BLE settings
  characteristic.
- **Region gate:** `resource_watch.py regions` for all 32 envs against a same-base build after each
  wave; the T-Beam IRAM number must not grow by a single byte.
- **Native tests** that exist and must stay green unchanged: `test_aprs_decode`,
  `test_aprs_reencode`, `test_aprs_corpus`, `test_aprs_fuzz`, `test_decodemheard`,
  `test_mheard_aging`, `test_txring`, `test_config_json`, `test_settings_sanitize`,
  `test_extern_tele_json`, `test_extern_notice_json`, `test_batt_detect`. New: `test_command_table`.
- **Bench coverage is honest about its gaps:** T-Deck Pro and T5 e-paper have no bench unit;
  changes there are build-only and the PR description must say so.

## 8. Closed and rejected

- `docs/archive/ram-opti.md` item 10 (U8g2 fonts into flash): void, fonts are already in flash on every
  measured target. Item 11 (custom IDF): stays rejected. Item 12 hashing: stays rejected; the
  52-to-40 trim survives as R3-13, interning as R2-03 with the distinction stated in section 3.
- Plain `MAX_*` count cuts: done in MEM-01; `MAX_DEDUP_RING` is settled by field measurement and is
  5 B per slot anyway.
- Deleting either battery implementation: both are live across disjoint board sets.
- Compiler flags `-fno-rtti -fno-exceptions -fmerge-all-constants`, extra `RADIOLIB_EXCLUDE_*`,
  NimBLE tightening, `DISABLE_TLS_CONSOLE`: already set, already default, or zero yield (exception
  metadata is in `.flash.rodata_noload`).
- `web_header_collect[1024]` static: deliberate BSS-over-heap, load-bearing for CS-03.
- Task stack shrinks: no high-water mark exists for any of our tasks; the only instrument covers the
  nRF52 EXTUDP chain. Unquantified, therefore no proposal; R3-07 reduces demand instead.
- `HEAP_TEST`, `LORA_ISR_DEBUG`, `MC_TEST_HOOKS` and eleven other macros are tested but never
  defined (appendix D6). They are diagnostics, not dead code; INS-01 forbids sweeping them.

## 9. Method

Two waves of read-only agents on the tree at `c51c5881`: six inventory scouts (build matrix, global
storage, callsign storage, prior-work digest, wire-contract boundary, mechanical clone scan with a
rolling-hash detector over 227 files) and ten analysts (rings and queues, callsigns, heap and IRAM,
display RAM, platform pairs, command dispatcher, core clones, UI clones, peripherals and web,
dead code and variants). Every headline number and every defect in section 6 marked "verified" was
re-checked by the orchestrator against the source lines or the release ELFs; three analyst claims
were corrected on the way (the "not compiled" file list, the `adc_chars` board list, the flash
saving of an already-garbage-collected font). Analyst reports are in the appendix verbatim,
including each one's rejected list.
