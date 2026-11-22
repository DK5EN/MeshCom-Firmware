# OTA-LOWHEAP-01 wave 1a: where the Heltec V3 lost 21 KB of internal RAM

Status: analysis, 2026-11-22. Measured on freshly built ELFs and link maps; nothing in this document is taken from
CI logs or from memory. Every number names its build. Companion of `docs/rca-ota-lowheap-01-20261122.md` (section 3.2
and 6, step 2 "Find the 21 KB") and `docs/campaign-ota-lowheap-01-20261122.md`.

## 1. Result

- The 09-11 figure reproduces exactly: tag `dry-base-20260911` = commit `291f375a` builds `heltec_wifi_lora_32_V3` to
  `RAM: used 116244` (baseline JSON: 116,244), dram0 184,984, iram0 85,116. No gap.
- Today (`fork-dev`, src identical to `06b3a6db`): `RAM: used 137100`, dram0 206,208, iram0 85,488. Delta
  **+20,856 B** on the `RAM:` line, **+21,224 B** of heap lost (dram0 delta, 372 B of it IRAM, section 6).
- The 20,856 B are fully attributed to link-map input sections (section 3, 0 B unexplained; 179 B of it are alignment
  padding). Growth is not one thing: about 58.9 KB were added and 38.0 KB removed.
- **71 percent of the growth is upstream, not fork.** `upstream/dev` (`5db0a4f5`) builds to `RAM: used 131020`
  (+14,776 vs 09-11). The fork-only part is **+6,080 B**, and 6,152 B of that is one table, the UDP outage hold
  `s_udpHoldStore` (6,144 B, `UDP_HOLD_BYTES`).
- The S3 size tier (`CONFIG_IDF_TARGET_ESP32S3 || BOARD_RAK4630`, `configuration_global.h:346`) costs **29,736 B** against
  the classic-ESP32 values. Measured, not computed: an experiment build with the V3 forced into the classic tier gives
  `RAM: used 107364` (section 7.1). The biggest single items are `nbrMatrix` 14,248 B (-8,528 at 64 rows), the msgstore
  mailbox 13,547 B (-13,547 without `ENABLE_MSGSTORE`), the outage hold 6,144 B (-5,120 at 1,024 B).
- IRAM growth also eats heap on the S3, one byte for one byte (section 6). It is small here (+372 B), but the
  69,104 B of IRAM that shadow DRAM are a fixed price on every S3 build.

## 2. Builds

All builds: `pio run -e heltec_wifi_lora_32_V3` (release, no INSTRUMENT flag), one `pio` process at a time, never
`pio test`, nothing uploaded. Old commits were built in detached worktrees created for this analysis and removed
afterwards. ELF and map are kept outside the repo (scratchpad of the session).

| Label               | Commit (all verified with `git cat-file -t`: commit)                                                           | Tree and env                                                   | `RAM:` used (of 327,680) | Flash used | dram0_0_seg used | iram0_0_seg used |
| ------------------- | -------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------- | ------------------------ | ---------- | ---------------- | ---------------- |
| **09-11**           | `291f375a` (tag `dry-base-20260911`, 2026-09-11)                                                               | worktree; old fork line, tree does not know the AU dev channel | 116,244                  | 1,455,697  | 184,984          | 85,116           |
| **upstream/dev**    | `5db0a4f5` (`upstream/dev`, 2026-11-22)                                                                        | worktree; built without `MC_AU_DEV_CHANNEL`                    | 131,020                  | 1,752,233  | 199,888          | 85,248           |
| **HEAD**            | `ee3338d9` main tree (src, `platformio.ini`, `variants/`, `tools/mc_build_defines.py` identical to `06b3a6db`) | main tree, `export MC_AU_DEV_CHANNEL=1`                        | 137,100                  | 1,790,709  | 206,208          | 85,488           |
| **E1** (experiment) | `06b3a6db` + one edited line in `src/configuration_global.h` (S3 falls into the classic tier, see 7.1)         | worktree, `MC_AU_DEV_CHANNEL=1`                                | 107,364                  | 1,767,045  | 176,472          | 85,488           |

- Region figures: `python3 tools/resource_watch.py regions --env heltec_wifi_lora_32_V3 --map <firmware.map>` on each
  map. `RAM:` and `Flash:` lines are PlatformIO's own build output.
- 09-11 reproduces `tools/resource_baseline.json` (`git show 1184177d:tools/resource_baseline.json`) to the byte: RAM
  116,244, dram0 184,984, iram0 85,116, flash 1,455,697. `291f375a` is the commit that wrote that baseline entry
  (`git log --all --until='2026-09-12 12:00' --since=2026-09-10 -- tools/resource_baseline.json` lists it; parent `32837022` on the old `fork-main`). `6edc7499` (the
  upstream state of 09-10) was not built; it is not needed.
- HEAD vs the CI figure 137,052 (v4.40a.11.22): +48 B locally. Most likely the dev channel strings and build metadata
  of a local build; not chased.
- Upstream was built without the AU dev channel flag because the upstream tree has no hook for the environment
  variable (the hook, `tools/mc_build_defines.py`, is fork-only). That flag adds code, not static data, in the fork
  tree; the HEAD minus upstream delta shows no symbol for it.
- `git status --short safeboot.bin safeboot-s3.bin` after the HEAD build: empty (clean).

## 3. Where the 20,856 B went (09-11 to HEAD)

Method: per input section (`.bss.*`, `.data.*`, sizes and object file) from `firmware.map`, restricted to the output
sections `.dram0.data` and `.dram0.bss`, summed per group. The `RAM:` figure of PlatformIO is exactly
`.dram0.data` + `.dram0.bss` (HEAD: 25,908 + 111,192 = 137,100; 09-11: 25,348 + 90,896 = 116,244). Parser:
`memmap.py` and `diffmap.py` (scratchpad, plain Python over the map). Symbol sizes were cross-checked with
`xtensa-esp32s3-elf-nm -S` for the candidate tables (section 7); they agree to the byte.

| Group                                              | 09-11 B | HEAD B | Delta B     |
| -------------------------------------------------- | ------- | ------ | ----------- |
| NBR topology matrix (nbr_matrix.cpp, `nbrMatrix`)  | 0       | 14,252 | +14,252     |
| old mheard tables (mheard_functions.cpp, removed)  | 15,838  | 0      | -15,838     |
| msgstore mailbox (msgstore.cpp)                    | 0       | 11,591 | +11,591     |
| UDP outage hold (`s_udpHoldStore`, mux)            | 0       | 6,152  | +6,152      |
| udp_functions.cpp rest (storTick tables, snapshot) | 1,423   | 3,120  | +1,697      |
| new byte rings (phone, phonecom, udp)              | 0       | 9,216  | +9,216      |
| old slot rings (removed)                           | 17,700  | 0      | -17,700     |
| LOG ring + page text buffers (loop_functions.cpp)  | 6,020   | 1,555  | -4,465      |
| loop_functions.cpp rest                            | 7,743   | 8,113  | +370        |
| RM runtime (rm_runtime.cpp)                        | 0       | 5,950  | +5,950      |
| extudp_functions.cpp                               | 1,392   | 2,569  | +1,177      |
| other fork/upstream src objects                    | 11,786  | 19,208 | +7,422      |
| framework + PIO libraries (archives)               | 53,924  | 54,777 | +853        |
| alignment padding (`*fill*`, hdr minus symbol sum) | 418     | 597    | +179        |
| **Sum of groups**                                  |         |        | **+20,856** |

Region delta (`RAM:` line): +20,856. Sum of groups: +20,856. **Unexplained: 0 B.** The only "non-feature" line is
alignment padding (`*fill*` entries, hdr minus symbol sum).

Reading:

- The September byte-ring reclaim did not stay a saving. Slot rings and LOG ring gave back 22.2 KB (-17,700 and -4,465);
  the old mheard tables another 15.8 KB. Together 38.0 KB, which were spent on: NBR matrix 14.3 KB, msgstore 11.6 KB,
  the new byte rings 9.2 KB (3 x 3,072), the outage hold 6.2 KB, the RM runtime 6.0 KB, plus about 11.7 KB of many small
  objects.
- The NBR matrix and the old mheard tables belong together: `nbrMatrix` 14,252 against `mheard_*` 15,838 is a net
  **-1,586**. The "topology" feature was not a RAM cost on its own. Net growth is rings + msgstore + hold + RM runtime.
- `src/mheard_functions.cpp.o` is gone from the S3 image: `mheardBuffer` 4,800, `mheardPathBuffer1` 5,200,
  `mheardPathCalls` 1,000, `mheardCalls` 800, `mheardLat`/`mheardLon` 640 each, `HardWare` 576 and others.

### 3.1 Group "other src objects" (+7,422 B), per object file

| Object                                          | 09-11 B | HEAD B | Delta B    |
| ----------------------------------------------- | ------- | ------ | ---------- |
| `src/kiss_functions.cpp.o`                      | 0       | 1,227  | +1,227     |
| `src/lora_rx_park.cpp.o`                        | 0       | 1,092  | +1,092     |
| `src/esp32/udp_frame_esp32.cpp.o`               | 0       | 989    | +989       |
| `src/web_functions/web_auth.cpp.o`              | 0       | 689    | +689       |
| `src/txring_functions.cpp.o`                    | 33      | 697    | +664       |
| `src/esp32/fw_update_net.cpp.o`                 | 0       | 635    | +635       |
| `src/esp32/serial_command_esp32.cpp.o`          | 0       | 624    | +624       |
| `src/lora_functions.cpp.o`                      | 440     | 1,023  | +583       |
| `src/aprs_is_client.cpp.o`                      | 0       | 500    | +500       |
| `src/dm_dedup.cpp.o`                            | 0       | 384    | +384       |
| `src/sto_notice.cpp.o`                          | 0       | 384    | +384       |
| `src/esp32/udp_drain_esp32.cpp.o`               | 0       | 319    | +319       |
| `src/command_functions.cpp.o`                   | 759     | 1,067  | +308       |
| `src/esp32/esp32_flash.cpp.o`                   | 2,040   | 2,289  | +249       |
| `src/gps_functions.cpp.o`                       | 485     | 681    | +196       |
| `src/blocked_calls.cpp.o`                       | 0       | 173    | +173       |
| `src/dm_stats.cpp.o`                            | 0       | 165    | +165       |
| `src/reack_limiter.cpp.o`                       | 0       | 161    | +161       |
| `src/esp32/applink_esp32.cpp.o`                 | 0       | 112    | +112       |
| `src/loop_breadcrumb.cpp.o`                     | 0       | 112    | +112       |
| `src/own_msg_status.cpp.o`                      | 0       | 101    | +101       |
| `src/esp32/gateway_service_esp32.cpp.o`         | 0       | 96     | +96        |
| `src/gw_link_status.cpp.o`                      | 0       | 85     | +85        |
| `src/clock.cpp.o`                               | 88      | 173    | +85        |
| `src/web_functions/web_txbuf.cpp.o`             | 0       | 46     | +46        |
| `src/esp32/esp32_main.cpp.o`                    | 1,154   | 1,192  | +38        |
| `src/gpio_control.cpp.o`                        | 0       | 32     | +32        |
| `src/esp32/rm_au_esp32.cpp.o`                   | 0       | 29     | +29        |
| `src/wind_counter.cpp.o`                        | 0       | 21     | +21        |
| `src/ina226_functions.cpp.o`                    | 28      | 48     | +20        |
| `src/gw_reg_runtime.cpp.o`                      | 0       | 13     | +13        |
| `src/mh_phone.cpp.o`                            | 0       | 12     | +12        |
| `src/web_functions/web_nodefunctioncalls.cpp.o` | 0       | 10     | +10        |
| `src/esp32/loop_actions_esp32.cpp.o`            | 0       | 9      | +9         |
| `src/msgstore_settings.cpp.o`                   | 0       | 8      | +8         |
| `src/byte_fifo.cpp.o`                           | 0       | 8      | +8         |
| `src/phone_commands.cpp.o`                      | 318     | 326    | +8         |
| `src/ltr390.cpp.o`                              | 0       | 6      | +6         |
| `src/loop_scheduler.cpp.o`                      | 0       | 4      | +4         |
| `src/net_console.cpp.o`                         | 49      | 53     | +4         |
| `src/onewire_functions.cpp.o`                   | 103     | 62     | -41        |
| `src/lora_setchip.cpp.o`                        | 272     | 0      | -272       |
| `src/batt_function_old.cpp.o`                   | 1,367   | 167    | -1,200     |
| `src/web_functions/web_functions.cpp.o`         | 1,391   | 125    | -1,266     |
| **Sum**                                         |         |        | **+7,422** |

### 3.2 Framework and libraries (+853 B)

Largest movers (object-level, from the same maps): `libmbedtls_2.a` +548 (`supported_ciphersuites` 484),
`libWiFiClientSecure.a` +360 (`s_dummy_crt` 344), `libmbedcrypto.a` +56, `libPreferences.a` -52,
`esp32-hal-uart.c.o` -64. These are TLS-client statics; the rest nets to zero.

## 4. HEAD minus upstream/dev: the fork-only part (+6,080 B)

| Group                                              | upstream B | HEAD B | Delta B    |
| -------------------------------------------------- | ---------- | ------ | ---------- |
| NBR topology matrix (nbr_matrix.cpp, `nbrMatrix`)  | 14,252     | 14,252 | +0         |
| msgstore mailbox (msgstore.cpp)                    | 11,591     | 11,591 | +0         |
| UDP outage hold (`s_udpHoldStore`, mux)            | 0          | 6,152  | +6,152     |
| udp_functions.cpp rest (storTick tables, snapshot) | 3,107      | 3,120  | +13        |
| new byte rings (phone, phonecom, udp)              | 9,216      | 9,216  | +0         |
| LOG ring + page text buffers (loop_functions.cpp)  | 1,555      | 1,555  | +0         |
| loop_functions.cpp rest                            | 8,011      | 8,113  | +102       |
| RM runtime (rm_runtime.cpp)                        | 5,950      | 5,950  | +0         |
| extudp_functions.cpp                               | 2,149      | 2,569  | +420       |
| other fork/upstream src objects                    | 19,847     | 19,208 | -639       |
| framework + PIO libraries (archives)               | 54,777     | 54,777 | +0         |
| alignment padding (`*fill*`, hdr minus symbol sum) | 565        | 597    | +32        |
| **Sum of groups**                                  |            |        | **+6,080** |

Region delta (`RAM:` line): +6,080. Sum of groups: +6,080. **Unexplained: 0 B.**

- The fork-only cost is the UDP outage hold (6,144 B store + 8 B mux). `src/udp_hold.h` is fork-only code; the hold is
  compiled in when `UDP_HOLD_BYTES > 0`, and `udp_functions.cpp:1610` shows it only does something when `bGATEWAY`.
  On every non-gateway V3 the 6,144 B are dead weight.
- Fork-only small items: `regReply` 264 and `s_extDns` 76 in `extudp_functions.cpp`, `udpHoldRt()::r` 96 in
  `gateway_service_esp32.cpp`, `meshcom_settings` +64, AppLink statics 112 (`applink_esp32.cpp`).
- One fork-only saving: `wsParams` (1,000 B of `.data` in `web_setup.cpp` upstream) is `const` data in flash in the
  fork (-1,000 B).
- Everything else of the 14,776 B between 09-11 and upstream/dev is upstream's: NBR matrix 14,252, msgstore 11,591,
  new byte rings 9,216, RM runtime 5,950 and so on, identical in both builds (delta 0 in the table above). Upstream's
  tier definitions are the same as the fork's, so this is not a fork configuration problem; it is the S3 tier as
  merged.

### 4.1 Other object files, HEAD minus upstream

| Object                                  | upstream B | HEAD B | Delta B  |
| --------------------------------------- | ---------- | ------ | -------- |
| `src/esp32/applink_esp32.cpp.o`         | 0          | 112    | +112     |
| `src/esp32/gateway_service_esp32.cpp.o` | 0          | 96     | +96      |
| `src/esp32/esp32_flash.cpp.o`           | 2,225      | 2,289  | +64      |
| `src/esp32/esp32_main.cpp.o`            | 1,152      | 1,192  | +40      |
| `src/esp32/fw_update_net.cpp.o`         | 606        | 635    | +29      |
| `src/command_functions.cpp.o`           | 1,059      | 1,067  | +8       |
| `src/lora_functions.cpp.o`              | 1,018      | 1,023  | +5       |
| `src/net_console.cpp.o`                 | 49         | 53     | +4       |
| `src/onewire_functions.cpp.o`           | 59         | 62     | +3       |
| `src/web_functions/web_setup.cpp.o`     | 1,000      | 0      | -1,000   |
| **Sum**                                 |            |        | **-639** |

## 5. Top 30 symbols by growth

Symbol = linker input section (one `-fdata-sections` object per variable). Bytes from the link map; the larger ones
checked against `nm -S` (section 7).

### 5.1 09-11 (`291f375a`) to HEAD

| #   | Symbol                                                                                 | Object                                 | 09-11 B | HEAD B | Delta B |
| --- | -------------------------------------------------------------------------------------- | -------------------------------------- | ------- | ------ | ------- |
| 1   | `nbrMatrix`                                                                            | `src/nbr_matrix.cpp.o`                 | 0       | 14,248 | +14,248 |
| 2   | `s_entries`                                                                            | `src/msgstore.cpp.o`                   | 0       | 10,200 | +10,200 |
| 3   | `s_udpHoldStore`                                                                       | `src/udp_functions.cpp.o`              | 0       | 6,144  | +6,144  |
| 4   | `phoneStore`                                                                           | `src/loop_functions.cpp.o`             | 0       | 3,072  | +3,072  |
| 5   | `phoneComStore`                                                                        | `src/loop_functions.cpp.o`             | 0       | 3,072  | +3,072  |
| 6   | `udpOutStore`                                                                          | `src/loop_functions.cpp.o`             | 0       | 3,072  | +3,072  |
| 7   | `s_sent`                                                                               | `src/rm_runtime.cpp.o`                 | 0       | 2,976  | +2,976  |
| 8   | `g_slot`                                                                               | `src/lora_rx_park.cpp.o`               | 0       | 1,072  | +1,072  |
| 9   | `s_tomb`                                                                               | `src/msgstore.cpp.o`                   | 0       | 1,024  | +1,024  |
| 10  | `s_log`                                                                                | `src/rm_runtime.cpp.o`                 | 0       | 940    | +940    |
| 11  | `sendExtern(bool, char*, unsigned char*, unsigned short, short, signed char)::c_json`  | `src/extudp_functions.cpp.o`           | 0       | 700    | +700    |
| 12  | `storTick()::s_storTimer`                                                              | `src/udp_functions.cpp.o`              | 0       | 664    | +664    |
| 13  | `storTick()::s_cur`                                                                    | `src/udp_functions.cpp.o`              | 0       | 644    | +644    |
| 14  | `s_state`                                                                              | `src/rm_runtime.cpp.o`                 | 0       | 640    | +640    |
| 15  | `storTick()::s_heard`                                                                  | `src/udp_functions.cpp.o`              | 0       | 640    | +640    |
| 16  | `strText`                                                                              | `src/esp32/serial_command_esp32.cpp.o` | 0       | 600    | +600    |
| 17  | `s_queue`                                                                              | `src/kiss_functions.cpp.o`             | 0       | 524    | +524    |
| 18  | `sendExtern(bool, char*, unsigned char*, unsigned short, short, signed char)::c_tjson` | `src/extudp_functions.cpp.o`           | 0       | 500    | +500    |
| 19  | `supported_ciphersuites`                                                               | `lib:libmbedtls_2.a`                   | 0       | 484    | +484    |
| 20  | `g_sess`                                                                               | `src/web_functions/web_auth.cpp.o`     | 0       | 480    | +480    |
| 21  | `pendingDisplayMsg`                                                                    | `src/lora_functions.cpp.o`             | 140     | 608    | +468    |
| 22  | `s_holders`                                                                            | `src/sto_notice.cpp.o`                 | 0       | 384    | +384    |
| 23  | `dm_dedup_table`                                                                       | `src/dm_dedup.cpp.o`                   | 0       | 384    | +384    |
| 24  | `s_deframe`                                                                            | `src/kiss_functions.cpp.o`             | 0       | 360    | +360    |
| 25  | `s_dummy_crt`                                                                          | `lib:libWiFiClientSecure.a`            | 0       | 344    | +344    |
| 26  | `rmqState()::st`                                                                       | `src/esp32/udp_frame_esp32.cpp.o`      | 0       | 344    | +344    |
| 27  | `rmqReplyState()::st`                                                                  | `src/esp32/udp_frame_esp32.cpp.o`      | 0       | 344    | +344    |
| 28  | `ringNeed`                                                                             | `src/txring_functions.cpp.o`           | 0       | 320    | +320    |
| 29  | `ringAlone`                                                                            | `src/txring_functions.cpp.o`           | 0       | 320    | +320    |
| 30  | `sendMeshComUDP()::udpSnapshot`                                                        | `src/esp32/udp_drain_esp32.cpp.o`      | 0       | 319    | +319    |

Biggest shrinks, same pair, for balance:

| #   | Symbol                                        | Object                                  | 09-11 B | HEAD B | Delta B |
| --- | --------------------------------------------- | --------------------------------------- | ------- | ------ | ------- |
| 1   | `BLEtoPhoneBuff`                              | `src/loop_functions.cpp.o`              | 6,100   | 0      | -6,100  |
| 2   | `BLEComToPhoneBuff`                           | `src/loop_functions.cpp.o`              | 6,100   | 0      | -6,100  |
| 3   | `ringBufferUDPout`                            | `src/loop_functions.cpp.o`              | 5,500   | 0      | -5,500  |
| 4   | `mheardPathBuffer1`                           | `src/mheard_functions.cpp.o`            | 5,200   | 0      | -5,200  |
| 5   | `mheardBuffer`                                | `src/mheard_functions.cpp.o`            | 4,800   | 0      | -4,800  |
| 6   | `ringbufferRAWLoraRX`                         | `src/loop_functions.cpp.o`              | 2,600   | 4      | -2,596  |
| 7   | `adc_chars`                                   | `src/batt_function_old.cpp.o`           | 1,296   | 36     | -1,260  |
| 8   | `pageLastTextLong2`                           | `src/loop_functions.cpp.o`              | 1,200   | 0      | -1,200  |
| 9   | `work_webpage(bool, int)::web_header_collect` | `src/web_functions/web_functions.cpp.o` | 1,024   | 0      | -1,024  |
| 10  | `mheardPathCalls`                             | `src/mheard_functions.cpp.o`            | 1,000   | 0      | -1,000  |
| 11  | `mheardCalls`                                 | `src/mheard_functions.cpp.o`            | 800     | 0      | -800    |
| 12  | `mheardLon`                                   | `src/mheard_functions.cpp.o`            | 640     | 0      | -640    |

### 5.2 upstream/dev (`5db0a4f5`) to HEAD

| #   | Symbol                                            | Object                                  | upstream B | HEAD B | Delta B |
| --- | ------------------------------------------------- | --------------------------------------- | ---------- | ------ | ------- |
| 1   | `s_udpHoldStore`                                  | `src/udp_functions.cpp.o`               | 0          | 6,144  | +6,144  |
| 2   | `regReply`                                        | `src/extudp_functions.cpp.o`            | 0          | 264    | +264    |
| 3   | `udpHoldRt()::r`                                  | `src/esp32/gateway_service_esp32.cpp.o` | 0          | 96     | +96     |
| 4   | `s_extDns`                                        | `src/extudp_functions.cpp.o`            | 0          | 76     | +76     |
| 5   | `meshcom_settings`                                | `src/esp32/esp32_flash.cpp.o`           | 2,208      | 2,272  | +64     |
| 6   | `setSMartBeaconing(double, double)::s_autoSpeed`  | `src/loop_functions.cpp.o`              | 0          | 40     | +40     |
| 7   | `s_ip`                                            | `src/esp32/applink_esp32.cpp.o`         | 0          | 32     | +32     |
| 8   | `s_timeRank`                                      | `src/loop_functions.cpp.o`              | 0          | 28     | +28     |
| 9   | `s_lock`                                          | `src/esp32/applink_esp32.cpp.o`         | 0          | 28     | +28     |
| 10  | `s_dueLogged`                                     | `src/esp32/fw_update_net.cpp.o`         | 0          | 24     | +24     |
| 11  | `s_bleAuthAddr`                                   | `src/esp32/esp32_main.cpp.o`            | 0          | 18     | +18     |
| 12  | `s_bleConnAddr`                                   | `src/esp32/esp32_main.cpp.o`            | 0          | 18     | +18     |
| 13  | `s_cetFilter`                                     | `src/loop_functions.cpp.o`              | 0          | 16     | +16     |
| 14  | `startExternUDP()::s_unparsed`                    | `src/extudp_functions.cpp.o`            | 0          | 16     | +16     |
| 15  | `s_pw`                                            | `src/esp32/applink_esp32.cpp.o`         | 0          | 15     | +15     |
| 16  | `regRun_st`                                       | `src/extudp_functions.cpp.o`            | 0          | 12     | +12     |
| 17  | `setSMartBeaconing(double, double)::s_autoSym`    | `src/loop_functions.cpp.o`              | 0          | 12     | +12     |
| 18  | `regPendingQ`                                     | `src/extudp_functions.cpp.o`            | 0          | 10     | +10     |
| 19  | `regWant`                                         | `src/extudp_functions.cpp.o`            | 0          | 9      | +9      |
| 20  | `s_udpHoldMux`                                    | `src/udp_functions.cpp.o`               | 0          | 8      | +8      |
| 21  | `regLimiter`                                      | `src/extudp_functions.cpp.o`            | 0          | 8      | +8      |
| 22  | `s_fd`                                            | `src/esp32/applink_esp32.cpp.o`         | 0          | 8      | +8      |
| 23  | `posTextDropState()::st`                          | `src/command_functions.cpp.o`           | 0          | 8      | +8      |
| 24  | `g_phoneLink`                                     | `src/loop_functions.cpp.o`              | 0          | 8      | +8      |
| 25  | `s_authedAt`                                      | `src/esp32/applink_esp32.cpp.o`         | 0          | 8      | +8      |
| 26  | `guard variable for startExternUDP()::s_unparsed` | `src/extudp_functions.cpp.o`            | 0          | 8      | +8      |
| 27  | `s_retryAt`                                       | `src/esp32/applink_esp32.cpp.o`         | 0          | 4      | +4      |
| 28  | `s_logAt`                                         | `src/esp32/applink_esp32.cpp.o`         | 0          | 4      | +4      |
| 29  | `hopSaveLast`                                     | `src/loop_functions.cpp.o`              | 0          | 4      | +4      |
| 30  | `wifiDnsRefreshServer()::last`                    | `src/udp_functions.cpp.o`               | 0          | 4      | +4      |

## 6. Does iram0_0_seg growth eat heap on the S3?

**Yes, byte for byte, beyond the first 16 KB of IRAM.** On the ESP32-S3 the IRAM at `0x40378000` and up is the same
SRAM as the DRAM at `0x3FC88000` and up. The linker reserves the DRAM twin of every IRAM byte with a dummy section, and
`dram0_0_seg`'s "used" figure includes it. Evidence, all from the HEAD map (`firmware.map`):

- Linker script line (map line 59785): `. = (ORIGIN (dram0_0_seg) + MAX ((_iram_end - _diram_i_start), 0x0))`, placed
  right after `.dram0.dummy    0x000000003fc88000    0x10df0` (line 59784). `_diram_i_start = 0x40378000`
  (line 54615), `_iram_end = 0x40388df0` (line 127919). `0x40388df0 - 0x40378000 = 0x10df0 = 69,104`. The first
  `0x4000` (16,384 B) of IRAM, from `iram0_0_seg` origin `0x40374000` to `0x40378000`, have no DRAM twin.
- `.dram0.data` starts at `0x3fc98df0`, directly behind the dummy; `.dram0.bss` follows; `_heap_start = 0x3fcba580` (line 127924) is where the heap begins. The heap therefore starts `dummy + data + bss` bytes above `0x3FC88000`.
- Check across three builds: dummy = iram0 used - 16,384 exactly. 09-11: 85,116 - 16,384 = 68,732 (`.dram0.dummy` size
  0x10c7c). upstream: 85,248 - 16,384 = 68,864 (0x10d00). HEAD: 85,488 - 16,384 = 69,104 (0x10df0).
- `dram0_0_seg` used = dummy + `.data` + `.bss`: HEAD 69,104 + 25,908 + 111,192 = 206,204 (206,208 with the 4 B
  start alignment). That is why dram0 (206,208) is 69,108 above the `RAM:` line (137,100). The `RAM:` line hides the
  IRAM twin; the heap sees it.
- `_heap_start` moved from `0x3fcb5298` (09-11, map line 115551) to `0x3fcba580` (HEAD): 0x52E8 = **21,224 B** = RAM
  delta 20,856 + IRAM delta 372. HEAD vs upstream: `0x3fcb8cd0` to `0x3fcba580` = 0x18B0 = 6,320 B = 6,080 + 240.
- The 372 B of IRAM growth since 09-11: `libfreertos.a` +241 (`xTaskGetHandle` 161 B and
  `prvSearchForNameWithinSingleList` 80 B from `tasks.c`, pulled in by `xTaskGetHandle("nimble_host")` at `esp32_main.cpp:4733`), `src/wind_counter.cpp.o` +137
  (`windIsr`, `IRAM_ATTR`, `wind_counter.cpp:31`), rest rounding. Upstream to HEAD +240 (the same `libfreertos` 241). Small, but real heap.
- Where the 85,486 B of IRAM sit (HEAD, input sections of `.iram0.text` and `.iram0.vectors`): `libfreertos.a` 16,593,
  `libbtdm_app.a` 12,034 (Bluetooth controller), `libspi_flash.a` 10,898, `libesp_hw_support.a` 9,275, `libhal.a`
  7,750, `libphy.a` 5,033, `libheap.a` 4,751, `libesp_ringbuf.a` 3,713. Application code in IRAM is 137 B in total
  (`wind_counter`); the fork does not control the other 85 KB.

## 7. Candidate tables on the V3 today

Sizes: `xtensa-esp32s3-elf-nm -S --size-sort` on the HEAD ELF (and on the E1 ELF for the classic value), identical to
the map. "Classic" = value in the `#else` tier of `configuration_global.h:369-389`. Saving in heap = saving in static
RAM, one byte for one (the heap is what is left after `.bss`).

### 7.1 Experiment E1: the whole tier at once (measured)

Worktree at `06b3a6db`, one line changed in `src/configuration_global.h`: line 346
`#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(BOARD_RAK4630)` to `#elif defined(BOARD_RAK4630)`, so the V3 takes
the `#else` (classic) tier: `RAM: used 107,364` (-29,736 vs HEAD), dram0 176,472 (-29,736), iram0 85,488 (unchanged),
flash 1,767,045 (-23,664). The map diff (HEAD to E1) sums to the same: msgstore.cpp -11,591, nbr_matrix.cpp -8,528,
udp_functions.cpp -7,069, loop_functions.cpp -2,048, txring_functions.cpp -320, dedup_functions.cpp -150,
msgstore_settings.cpp -8, padding -22 = -29,736. The V3 would land at the level of the classic T-Beam (107,080 on CI
11.22). Idle heap estimate (computed, not measured): the bench V3 reads 98,412 B free at 586 s (campaign, wave 1
bench setup); plus 29,736 B is about 128 KB.

### 7.2 Size-tier items

| Item (symbol)                                                                                                                                                                       | Define (file:line, S3 tier / classic tier)                                                                                                                                                                                                                                      | Bytes now (nm)                                  | Bytes at classic value                                | Saving B                              | What the node loses (read from code)                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | ----------------------------------------------------- | ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Neighbour matrix `nbrMatrix` (`nbr_matrix.cpp:3252`)                                                                                                                                | `NBR_MAX_ROWS` 128 (`configuration_global.h:551`) / 64 (`:577`); `NBR_MAX_EDGES` 512 (`:554`) / 256 (`:580`); `NBR_EXT_SLOTS` 64 (`:557`) / 48 (`:583`); `NBR_HZ_ENTRIES` 112 (`:560`) / 48 (`:586`); family marker `NBR_FAMILY_LARGE` (`:359`) / `NBR_FAMILY_CLASSIC` (`:389`) | 14,248                                          | 5,720 (E1 nm)                                         | 8,528                                 | Table keeps the 63 freshest nodes of the 12 h window instead of 127 (own row 0 excluded); when full, the row with the oldest `last_min` is evicted (`nbr_matrix.h:470-476`, log `EVICT`), edge pool 256 instead of 512 (oldest edge evicted, `EVICT-E`), 48 instead of 64 direct-hearing slots (`EVICT-X`), horizon of 48 instead of 112 senders at 3 or more hops (`nbr_matrix.h:399-405`). Busy meshes see a shorter memory of far nodes; masks shrink from 128 to 64 bit. |
| `ringNeed` + `ringAlone` (`txring_functions.cpp:76-77`)                                                                                                                             | follows `NBR_MASK_WORDS = (NBR_MAX_ROWS+63)/64` (`nbr_mask.h:22`), so 8 B instead of 16 B per mask                                                                                                                                                                              | 640 (2 x 320)                                   | 320 (E1 nm)                                           | 320                                   | Only the mask width, goes with the NBR rows (64 or less).                                                                                                                                                                                                                                                                                                                                                                                                                    |
| Msgstore mailbox: `s_entries` 10,200, `s_tomb` 1,024, `s_list` 160, `s_action_ring` 80, `s_capblocked` 50, `s_cnt` 48, rest 29 (`msgstore.cpp:24,31,52`), `msgstore_settings.cpp` 8 | `ENABLE_MSGSTORE` defined only in the S3/RAK tier (`configuration_global.h:351`); slots `MSGSTORE_SLOTS_MAX` 50 (`msgstore_api.h:19`), `MSGSTORE_TOMB_SLOTS` 32 (`:38`)                                                                                                         | 11,599                                          | 0 (E1 nm)                                             | 11,599                                | The node can no longer act as a store node (last-hop mailbox): holding DMs for absent recipients and re-delivering them (`msgstore_api.h:1-14`), `--store own/list/heard`, `/?page=mailbox`. Default mode is OFF (`msgstore.cpp:33`, NVS default `msgstore_settings.cpp:48`), so a node that never switched it on loses nothing it uses.                                                                                                                                     |
| Store announce tables in `storTick()` (`udp_functions.cpp:1497-1503`): `s_storTimer` 664, `s_cur` 644, `s_heard` 640                                                                | inside `#if defined(ENABLE_MSGSTORE)` (`udp_functions.cpp:1499`); size from `STOR_MAX_CALLS` 64 (`stor_announce.h:41`)                                                                                                                                                          | 1,948                                           | 0 (E1 nm)                                             | 1,948                                 | The STOR announcement of a store node. Goes with the msgstore (same `#if`).                                                                                                                                                                                                                                                                                                                                                                                                  |
| **Msgstore feature in total**                                                                                                                                                       |                                                                                                                                                                                                                                                                                 | **13,547**                                      | 0                                                     | **13,547**                            | See the two rows above (11,599 + 1,948).                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| Msgstore knob only: fewer slots                                                                                                                                                     | `MSGSTORE_SLOTS_MAX` 50 to 25 (`msgstore_api.h:19`; `MSGSTORE_SLOTS_DEFAULT` `:20` follows)                                                                                                                                                                                     | 10,250 (`s_entries` 10,200 + `s_capblocked` 50) | 5,125 (computed: 205 B per slot = nm 10,200/50 + 1 B) | 5,125                                 | Mailbox holds 25 instead of 50 pending DMs; a full table refuses new messages ("never evict a held message for a new one", `msgstore.cpp:488`). `--storeslots` stays 1..25.                                                                                                                                                                                                                                                                                                  |
| UDP outage hold `s_udpHoldStore` (`udp_functions.cpp:1442`)                                                                                                                         | `UDP_HOLD_BYTES` 6144 (`configuration_global.h:358`) / 1024 (`:388`); 0 compiles it out (fallback `:393`)                                                                                                                                                                       | 6,144 (+ 8 B mux, `.data`)                      | 1,024 (E1 nm)                                         | 5,120 (6,152 at 0)                    | Gateway only (`udp_functions.cpp:1610`): text frames held while the server is silent for replay. About 10 frames per 1,024 B (config comment `:388`), so about 60 now (computed), 10 at 1,024, none at 0. Longer server outages then lose the oldest texts first. Non-gateway nodes lose nothing.                                                                                                                                                                            |
| Phone data ring `phoneStore` (`loop_functions.cpp:833`)                                                                                                                             | `RING_BYTES_PHONE` 3072 (`configuration_global.h:355`) / 2048 (`:385`)                                                                                                                                                                                                          | 3,072                                           | 2,048 (E1 nm)                                         | 1,024                                 | Holds about 25 instead of about 39 frames at the measured mean of 77 B (comment `:381-384`) for a BLE phone; a full ring drops the oldest frames (`byte_fifo`, RCA 3.1). Static on the V3, heap-while-BLE-is-up on classic (`loop_functions_extern.h:378-385`).                                                                                                                                                                                                              |
| UDP out ring `udpOutStore` (`loop_functions.cpp:823`)                                                                                                                               | `RING_BYTES_UDP` 3072 (`:357`) / 2048 (`:387`)                                                                                                                                                                                                                                  | 3,072                                           | 2,048 (E1 nm)                                         | 1,024                                 | Same, towards the server (gateway path); full ring drops oldest.                                                                                                                                                                                                                                                                                                                                                                                                             |
| Phone command ring `phoneComStore` (`loop_functions.cpp:836`)                                                                                                                       | `RING_BYTES_PHONECOM` 3072 in every tier (`:356`, `:386`)                                                                                                                                                                                                                       | 3,072                                           | 3,072                                                 | 0 (floor 120)                         | `static_assert(RING_BYTES_PHONECOM >= 12*(245+1))` at `loop_functions.cpp:844` forbids going below 2,952 B (the whole BLE config burst). Not a tier item.                                                                                                                                                                                                                                                                                                                    |
| Dedup ring `ringBufferLoraRX` (`dedup_functions.cpp:18`)                                                                                                                            | `MAX_DEDUP_RING` 100 (`configuration_global.h:353`) / 70 (`:379`), 5 B per entry                                                                                                                                                                                                | 500                                             | 350 (E1 nm)                                           | 150                                   | Memory window of already-seen msg_ids shrinks proportionally; in the 2026-08-25 field measurement 100 entries covered about 38 min (`dedup_functions.h:18-26`), so about 27 min at 70 (computed). Repeats after that window are flooded again.                                                                                                                                                                                                                               |
| LOG ring `ringbufferRAWLoraRX` (`loop_functions.cpp:799`)                                                                                                                           | `MAX_LOG` 10 (`:354`) / 20 (`:380`)                                                                                                                                                                                                                                             | 4 (a pointer)                                   | 4                                                     | 0                                     | Not static any more: `calloc(MAX_LOG, sizeof(rawLogLine_t))` on first use (`loop_functions.cpp:799`), 10 x 260 = 2,600 B (comment `loop_functions.cpp:768-769`) while a web page needs it. The classic tier is bigger here (20). It matters for fragmentation, not for `.bss`.                                                                                                                                                                                               |
| TX ring `ringBuffer` (`loop_functions.cpp:753`)                                                                                                                                     | `MAX_RING` 20 in both tiers (`:352`, `:378`)                                                                                                                                                                                                                                    | 5,200                                           | 5,200                                                 | 0 (260 B per slot, computed 5,200/20) | Same on both tiers, not tier-driven. Per slot 260 B plus `ringNeed`/`ringAlone`/`own_msg_id` etc.; shrinking `MAX_RING` shortens the TX queue (BP-01 backpressure threshold follows, comment `:371-377`). Listed only for completeness.                                                                                                                                                                                                                                      |

Sum of the E1-measured rows: 8,528 + 320 + 13,547 + 5,120 + 1,024 + 1,024 + 150 = 29,713; E1 itself: 29,736; the 23 B
difference is alignment padding (-22) and 1 B of rounding. Nothing else changes between the tiers.

### 7.3 NBR knobs one at a time (computed with the real header)

`sizeof(NbrMatrix)` compiled on the host from `src/nbr_matrix.h` with `-DNATIVE_BUILD` and the four `-D` values
(`nbrsize.cpp`, g++ -std=c++17). The 128/512/64/112 case gives 14,248, equal to the `nbrMatrix` nm size, and
64/256/48/48 gives 5,720, equal to the E1 nm size, so the method is validated at both ends.

| `NBR_MAX_ROWS` / `EDGES` / `EXT_SLOTS` / `HZ_ENTRIES` | `sizeof(NbrMatrix)` B | Saving vs 14,248 | Plus `ringNeed`/`ringAlone` |
| ----------------------------------------------------- | --------------------- | ---------------- | --------------------------- |
| 128 / 512 / 64 / 112 (HEAD)                           | 14,248                | 0                | 0                           |
| 128 / 512 / 64 / 48 (horizon only)                    | 12,456                | 1,792            | 0                           |
| 128 / 512 / 48 / 112 (direct slots only)              | 14,040                | 208              | 0                           |
| 128 / 256 / 64 / 112 (edge pool only)                 | 12,712                | 1,536            | 0                           |
| 96 / 384 / 64 / 112                                   | 11,720                | 2,528            | 0 (2 mask words)            |
| 64 / 512 / 64 / 112 (rows only)                       | 8,744                 | 5,504            | 320 (1 mask word)           |
| 64 / 256 / 64 / 112 (rows + edges)                    | 7,208                 | 7,040            | 320                         |
| 64 / 256 / 48 / 48 (classic)                          | 5,720                 | 8,528            | 320                         |

Every size is overridable per `-D` (`configuration_global.h:537-541`: "Wer im Feld RAM zurueckholen muss, dreht hier").
`nbr_matrix.h` guards the dependencies with `static_assert` (mask width at least rows, row index in a byte).

## 8. Other static buffers on the V3 (not tier-driven)

Sizes from `nm -S` (HEAD). A cut means a code or define change, not a tier switch; the function column is the file's
role by name only, not analysed.

| Symbol (file:line)                                                | Define                                                                      | Bytes           | Note                                                                                    |
| ----------------------------------------------------------------- | --------------------------------------------------------------------------- | --------------- | --------------------------------------------------------------------------------------- |
| `s_sent` (`rm_runtime.cpp:64`)                                    | `kSentN = RM_POLICY_MAX_ENTRIES` 12 (`rm_sender_policy.h:53`)               | 2,976           | RM runtime sent book, 248 B per slot (computed 2,976/12)                                |
| `s_log` (`rm_runtime.cpp:51`)                                     | `kLogN` 5 (`rm_runtime.cpp:50`)                                             | 940             | 188 B per entry (computed)                                                              |
| `s_state` (`rm_runtime.cpp:46`)                                   | -                                                                           | 640             | RM runtime total in `rm_runtime.cpp.o`: 5,950                                           |
| `g_slot` (`lora_rx_park.cpp:17`)                                  | `RX_PARK_SLOTS` 4 (`lora_rx_park.h:20`)                                     | 1,072           | 268 B per slot (computed)                                                               |
| `pageLastText` (`loop_functions.cpp:1195`)                        | `PAGE_MAX` 6 or 10, `maxdisplines` 7 or 11 (`loop_functions.cpp:1177-1184`) | 1,050           | Display page history                                                                    |
| `cap_ring` (`capture_functions.cpp:34`) + `captureDrain()::line`  | `CAPTURE_RING_BYTES` 768 (`capture_functions.cpp:29`)                       | 768 + 606       | Capture ring and line buffer, `capture_functions.cpp.o` 1,388 total                     |
| `c_json` / `c_tjson` (`extudp_functions.cpp:851-852`)             | `EXTERN_MSG_JSON_BUF` 700 (`extern_msg_json.h:23`), 500                     | 700 + 500       | Function statics of `sendExtern()`; `externQueue` (`extudp_functions.cpp:114`) 556 more |
| `s_queue` / `s_deframe` / `s_ackmap` (`kiss_functions.cpp:82-86`) | `KISS_QUEUE_SLOTS`                                                          | 524 / 360 / 192 | `kiss_functions.cpp.o` 1,227 total                                                      |
| `strText` (`serial_command_esp32.cpp:15`)                         | literal 600                                                                 | 600             | Serial command buffer                                                                   |
| `msg_text` (`loop_functions.cpp:560`) / `RcvBuffer` (`:746`)      | `MAX_MSG_LEN_PHONE*2`, `UDP_TX_BUF_SIZE*2`                                  | 600 / 510       | Phone text and LoRa RX buffers                                                          |
| `g_sess` / `g_nonce` (`web_functions/web_auth.cpp:38,37`)         | `SESSION_SLOTS`, `NONCE_SLOTS`                                              | 480 / 192       | Web auth sessions                                                                       |
| AppLink statics (`esp32/applink_esp32.cpp`)                       | -                                                                           | 112 total       | Small; no AppLink or net-console table above 128 B                                      |
| `net_console.cpp.o`, `web_txbuf.cpp.o`                            | -                                                                           | 53, 46          | The web TX buffer is heap per page (1.4 KB, campaign doc), not static                   |

No web, AppLink or net-console static buffer above 700 B exists on the V3 build. `phoneStore` and `phoneComStore`
(6,144 B together) are static here; on the classic ESP32 they are heap while BLE is up and released by the BLE pause
(`loop_functions_extern.h:378-385`, `loop_functions.cpp:827-829`). Making them dynamic on the S3 would not lower the idle
footprint but would give the TLS job 6,144 B back at the update (computed from the two nm sizes).

## 9. BLE, lwIP and WiFi static pools for the V3

- NimBLE: flags in `platformio.ini:1796-1802` (section `[esp32]`, inherited by `[esp32_s3]` and the V3 env):
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`, `MAX_BONDS=1`, `MAX_CCCDS=2`, central and observer roles disabled,
  `HOST_TASK_STACK_SIZE=3072`, `MSYS1_BLOCK_COUNT=4`. They take effect (the NimBLE-Arduino `nimconfig.h` guards its
  defaults with `#ifndef`, e.g. `MSYS1_BLOCK_COUNT` default 12 at `nimconfig.h:231-232`). Static NimBLE data in the
  HEAD image is 3,716 B in total (`libNimBLE-Arduino.a`, link map): `ble_att_svr_prep_entry_mem` 768,
  `ble_sm_proc_mem` 400, `ble_hs_hci_frag_data` 300, `ble_gattc_proc_mem` 224 and small pools. Already trimmed;
  nothing left to cut. The MSYS mbuf pool is allocated from the heap at start, not static.
- lwIP and WiFi: **no lwIP or WiFi buffer count is set in `platformio.ini` for the V3** (`grep -i 'lwip\|WIFI_\|CONFIG_ESP'`
  finds only the AsyncTCP flags of the Safeboot envs). The libraries are precompiled in the Arduino core
  (`framework-arduinoespressif32/tools/sdk/esp32s3/qio_qspi`); `sdkconfig` there has `CONFIG_LWIP_MAX_SOCKETS=16`
  (`:1455`), `CONFIG_LWIP_MAX_ACTIVE_TCP=16` (`:1504`), `CONFIG_LWIP_MAX_UDP_PCBS=16` (`:1531`),
  `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5760` and `..._WND_DEFAULT=5760` (`:1513-1514`),
  `CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=8` (`:1225`), `..._DYNAMIC_RX_BUFFER_NUM=32` (`:1226`),
  `..._STATIC_TX_BUFFER_NUM=8` (`:1229`), `..._CACHE_TX_BUFFER_NUM=16` (`:1230`). They cannot be changed from
  `platformio.ini`; that would need a rebuilt framework. The WiFi buffers are allocated from the heap at WiFi start,
  not part of `.bss` (not measured here, values read from the file).
- Static data in precompiled libraries (map, HEAD): `libnet80211.a` 10,653 B (`g_cnxMgr` 3,800, `ftm_initiator` 2,776,
  `s_wifi_nvs` 1,180), `libespcoredump.a` 5,735 B (`s_coredump_stack` 1,124, core dump to flash is on:
  `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, sdkconfig `:1264`), `libfreertos.a` 4,997, `liblwip.a` 4,667
  (`dns_table` 1,184), `libpp.a` 3,530, `libphy.a` 3,196, `libmdns.a` 1,898. All unchanged between 09-11, upstream
  and HEAD (the `framework + libraries` group moved only +853 B, section 3.2). None is a fork lever.

## 10. How to reproduce

```
export MC_AU_DEV_CHANNEL=1                      # fork tree only
git worktree add --detach <dir> 291f375a        # or 5db0a4f5, 06b3a6db
( cd <dir> && pio run -e heltec_wifi_lora_32_V3 )
python3 tools/resource_watch.py regions --env heltec_wifi_lora_32_V3 --map <dir>/.pio/build/heltec_wifi_lora_32_V3/firmware.map
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm -S --size-sort -C <dir>/.pio/build/heltec_wifi_lora_32_V3/firmware.elf
git worktree remove <dir>
```

Experiment E1: in `src/configuration_global.h` line 346 change
`#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(BOARD_RAK4630)` to `#elif defined(BOARD_RAK4630)` (worktree only).
The per-object diff scripts (`memmap.py`, `diffmap.py`, `gen.py`) and `nbrsize.cpp` were kept in the session scratchpad;
the logic is: parse `.dram0.data`/`.dram0.bss` input sections of the map, sum per object and per input section,
subtract, group by file.

## 11. Evidence checklist

- Commits cited exist (`git cat-file -t`): `291f375a`, `5db0a4f5`, `06b3a6db`, `ee3338d9`, `1184177d`, `6edc7499`,
  `32837022` all `commit`; tag `dry-base-20260911` is a `tag` object pointing to `291f375a`.
  `git diff --stat 06b3a6db ee3338d9 -- src platformio.ini variants tools/mc_build_defines.py` is empty;
  `git diff --stat 1184177d 06b3a6db -- src platformio.ini variants` is empty.
- Defines quoted at their lines in HEAD (`configuration_global.h`: 346, 351-358, 359, 369, 378-389, 393, 537-541,
  549-560, 575-586; `msgstore_api.h`: 19, 20, 38; and the source lines in section 7 and 8) were read in the HEAD tree.
- Every saving in 7.2 is computed from nm or map sizes of the HEAD and E1 ELFs. Computed (no symbol available) and
  labelled as such: msgstore at 25 slots (per-slot cost from nm), dedup window at 70, frames per ring or hold, idle heap
  after E1, the NBR knob table (host `sizeof`, validated at both ends).
- nm vs map: the sum of all RAM symbols from `nm -S` is lower than the map totals (HEAD 124,615 B nm against 137,100 B
  map; 09-11 105,331 against 116,244) because objects without sized symbols (libraries, padding) only exist in the
  map. The map is the one that reproduces the `RAM:` line to the byte, so the attribution uses the map; the candidate
  symbols (all sized) agree with nm to the byte.
- Worktrees `mc-ramhunt-291f375a`, `mc-ramhunt-5db0a4f5`, `mc-ramhunt-06b3a6db` removed after the analysis.
