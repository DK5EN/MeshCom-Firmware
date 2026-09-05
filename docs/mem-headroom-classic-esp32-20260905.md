# Classic ESP32: IRAM/DRAM headroom analysis (MEM-04 follow-up)

Date: 2026-09-05. Branch `fork-main`, measured in a clean worktree. Nothing committed.
Tool: `python3 tools/resource_watch.py regions --env <env> --map .pio/build/<env>/firmware.map`.

## 1. Outcome

Two build-configuration changes recover ~10.5 kB on the two tightest envs without touching
firmware code. Both are measured, not estimated.

| Env                   | Region |  Before |    After | Change                             |
| --------------------- | ------ | ------: | -------: | ---------------------------------- |
| `ttgo_tbeam` (3 envs) | IRAM   |    20 B |  4 972 B | unflag `BOARD_HAS_PSRAM` + `-mfix` |
| `ttgo_tbeam` (3 envs) | DRAM   | 9 976 B | 10 048 B | same                               |
| `E22_XML-DevKitC`     | DRAM   |   848 B |  6 616 B | vendor tinyxml2 without `contrib/` |
| `E22_XML-DevKitC`     | IRAM   | 3 456 B |  4 028 B | same                               |

Reference values on the same tree: `E22-DevKitC` DRAM 11 128 B / IRAM 4 972 B.

## 2. Lever 1: T-Beam PSRAM flags (IRAM +4 952 B)

**Cause.** The PlatformIO board definition `ttgo-t-beam.json` carries
`extra_flags = -DARDUINO_T_Beam -DBOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue`. Only the three
`ttgo_tbeam*` envs inherit these flags; all other classic-ESP32 envs (E22 family, Heltec V2,
ttgo-lora32-v21, loraprs) do not.

**Mechanism.** `cores/esp32/esp32-hal-psram.h` `#undef`s `CONFIG_SPIRAM_SUPPORT`/`CONFIG_SPIRAM`
unless `BOARD_HAS_PSRAM` is defined. With the flag, `initArduino()` calls `psramInit()`, which
links `esp_spiram_init` and `spiram_psram.c` (4 427 B, IRAM because it runs with cache
disabled). Without it, `psramInit()` is never compiled; the T-Beam IRAM figure then equals the
E22 figure exactly.

**Measured fix** (in each of `variants/ttgo_tbeam/platformio.ini`, `ttgo_tbeam_SX1262`,
`ttgo_tbeam_SX1268`):

```ini
build_unflags =
	-DBOARD_HAS_PSRAM
	-mfix-esp32-psram-cache-issue
```

**Open decision: does the hardware use PSRAM under MeshCom?**

- Hardware evidence says T-Beam V1.1/V1.2 carry PSRAM (LilyGo: 8 MB, of which the classic ESP32
  maps 4 MB). Meshtastic on a V1.2 logs `Total PSRAM: 4192107`; LilyGo issue logs on an
  ESP32-D0WDQ6-V3 print `PSRAM is enable! PSRAM: 4.00MB`. Meshtastic's `variants/esp32/tbeam`
  sets no PSRAM flag of its own; it relies on the same `board = ttgo-tbeam` definition we use.
- The only MeshCom field log of a V1.2 (`dj8meh-41`, TBEAM_AXP2101, FLASH 20260821) prints
  `[PSRM] 00:00:00 0` with `[HEAP] ... 249432` internal free heap. So under our build
  `psramInit()` apparently fails silently (its `log_w` sits under the core debug level).
  Candidates: 40 MHz `f_flash` in the board JSON vs. `CONFIG_SPIRAM_SPEED_80M` in the prebuilt
  SDK, or the custom bootloader written by our `upload_command`. Unverified.
- Fleet share (mcmap, 2026-09-05, 1 476 nodes): TBEAM V1.2 234, V1.1 129, V1.1-1268 8,
  V1.2-1262 3 (all on the affected envs), Supreme 18 (S3, unaffected). About a quarter of the
  fleet.
- Decide with the bench T-Beam-92 `[PSRM]` line right after `[HEAP] ... (init)` (board was not
  on USB on 2026-09-05; only the RAK was). If it reads 0, the unflag removes code that never
  succeeds and is free. If it reads ~4 MB, the unflag trades 4 MB PSRAM heap for 4.9 kB IRAM;
  with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` every allocation >= 4 kB would move back to
  internal heap. That is an operator decision.

## 3. Lever 2: tinyxml2 example program linked into E22_XML (DRAM +5 768 B)

**Cause.** `variants/E22_XML-DevKitC/platformio.ini` has
`lib_deps = https://github.com/leethomason/tinyxml2`. The repo ships no `library.json`
srcFilter, so PlatformIO compiles everything, including `contrib/html5-printer.cpp` and
`xmltest.cpp`. `html5-printer.cpp` defines `main()`. The Arduino core does not define `main`,
so `crt0.o` resolves it from the tinyxml2 archive, and with it `std::cout`, iostream and
`std::locale`. The map shows the chain verbatim:

```
libtinyxml2.a(html5-printer.cpp.o)   <- crt0.o (main)
libstdc++.a(globals_io.o)            <- html5-printer.cpp.o (_ZSt4cout)
libstdc++.a(locale_init.o)           <- ios.o (_ZNSt6localeC1Ev)
```

DRAM cost: `locale_init.o` 3 512 B, `globals_io.o` 1 368 B, `ctype_wE` 1 248 B, `ctype_cE` 544 B,
plus facet/punct caches and eight stream objects. The `native_xml` env uses the same URL.

**Measured fix.** `lib/tinyxml2/` with `tinyxml2.cpp`, `tinyxml2.h`, `LICENSE.txt` (zlib) and

```json
{
  "name": "tinyxml2",
  "version": "11.0.0",
  "build": { "srcFilter": ["+<tinyxml2.cpp>"] }
}
```

and the URL line removed from the variant. Result: DRAM headroom 848 -> 6 616 B, IRAM
3 456 -> 4 028 B, zero `cout`/`locale_init` symbols left.

## 4. Further DRAM items, by yield

| Item                                                                                   | Bytes               | Env         | Effort                                       |
| -------------------------------------------------------------------------------------- | ------------------- | ----------- | -------------------------------------------- |
| Five static rings (MEM-02, parked with risk assessment)                                | ~28 100             | all classic | large, parked                                |
| `ENABLE_XML` tier gets MAX_MHEARD/MAX_MHPATH 50/50 vs 30/40 elsewhere                  | ~3 100              | E22_XML     | one line in `configuration_global.h`         |
| Display buffers `pageLastTextLong2`/`pageLastText`/`pageLastLine`                      | 1 200 + 1 050 + 504 | all classic | check whether E22 drives an OLED             |
| Softserial `String` tables `strPARM`/`strPARM_ID`/`strUNIT` (+ `strSNAME`/`strSID`)    | 2 400 + 320         | E22_XML     | code change (char arrays or on-demand alloc) |
| `web_header_collect` static 1 024 (fork: "BSS statt Heap")                             | 1 024               | all         | design choice, revisit                       |
| `externQueue` (2 x 514)                                                                | 1 028               | all         | -                                            |
| `HardWare[36]` and `strCountry[17]` as `String` objects                                | 576 + 272           | all         | `const char*` tables move to flash           |
| `mheardLat`/`mheardLon` as `double`                                                    | 400 (E22_XML) / 240 | all         | `float` halves it                            |
| `strText` 600, `msg_text` 600, `RcvBuffer` 510, `convBuffer` 305, `textbuff_phone` 300 | ~2 300              | all         | individual review                            |
| `libespcoredump` (`s_coredump_stack` 1 124 etc.)                                       | 4 943               | all         | SDK, not changeable                          |
| `g_cnxMgr` (WiFi) 3 800, lwip `dns_table` 1 184, `packet$` 1 460                       | ~6 400              | all         | SDK, not changeable                          |

Largest DRAM object files (E22_XML): `loop_functions.cpp.o` 34 071 B (rings + display buffers),
`mheard_functions.cpp.o` 9 128 B, `softser_functions.cpp.o` 3 004 B, `esp32_flash.cpp.o` 2 040 B.

## 5. IRAM composition (T-Beam, 131 072 B total, 129 423 B attributed)

| Archive                 |  Bytes | Fork-controllable?                                                |
| ----------------------- | -----: | ----------------------------------------------------------------- |
| libbtdm_app.a (BT ctrl) | 30 874 | no (prebuilt, BTDM dual-mode)                                     |
| libfreertos.a           | 15 521 | no (`FREERTOS_PLACE_FUNCTIONS_INTO_FLASH` off in SDK)             |
| libc.a                  | 15 039 | no (SDK linker script places strftime/mktime/tzset/stdio in IRAM) |
| libesp_hw_support.a     | 13 016 | 4 427 of it is `spiram_psram.c` (lever 1)                         |
| libspi_flash.a          |  9 191 | no                                                                |
| libphy.a                |  8 862 | no                                                                |
| libhal.a                |  4 866 | no                                                                |
| libheap.a               |  4 475 | no                                                                |
| libesp_system.a         |  4 074 | no                                                                |
| libesp_ringbuf.a        |  3 826 | no (UART driver)                                                  |
| libcoexist.a            |  2 977 | no                                                                |
| _fill_ (alignment)      |  1 316 | no                                                                |
| libOneWire.a            |  1 121 | yes: `OneWire_GPIO` set on T-Beam (GPIO 4) and E22_XML (GPIO 25)  |
| libEspSoftwareSerial.a  |    936 | E22_XML only, needed for softserial                               |
| libNimBLE-Arduino.a     |    104 | negligible                                                        |

About 96 % of IRAM is prebuilt arduino-esp32 SDK. Shrinking it (FreeRTOS into flash, BLE-only
controller, no coredump) requires a custom framework build, which is out of scope for an
upstream PR.

## 6. Method notes

- Attribution scripts: parse `firmware.map` input sections for `.iram0.text` and
  `.dram0.data/.bss`, group by archive/object; cross-check with
  `xtensa-esp32-elf-nm -S --size-sort` filtered to `0x3ffb..0x3ffe` and type `b/B/d/D`.
- The map's "Archive member included to satisfy reference" section answers "who pulls X in"
  (used for tinyxml2 -> crt0 `main`).
- **Pitfall:** editing any `platformio.ini` changes `project.checksum` and PlatformIO deletes
  every `.pio/build/<env>` directory. Capture maps before touching ini files.
- `resource_watch.py regions` is the guard MEM-03 added; it now reports both regions.

## 7. Side answer: tzset and GPS position

`tzset()` reads the `TZ` environment variable and derives offset and DST rules from it. It
cannot derive a zone from lat/lon; that needs a polygon database of political zone borders,
far too large for a classic ESP32. Longitude / 15 gives solar time, not CET/CEST. For a
Central-European fleet a fixed `TZ` string with EU DST rules (`CET-1CEST,M3.5.0,M10.5.0/3`)
is the pragmatic option; libc then handles the switch. Note that the libc time functions
(strftime, mktime, tzset) occupy 5.6 kB of IRAM because the SDK linker script places them
there; not changeable from the fork.

## 8. Sources

- LilyGo T-Beam product page: https://lilygo.cc/products/t-beam
- Meshtastic tbeam variant: https://raw.githubusercontent.com/meshtastic/firmware/master/variants/esp32/tbeam/platformio.ini
- Meshtastic issue #3066 (V1.2 boot log, PSRAM 4 MB): https://github.com/meshtastic/firmware/issues/3066
- LilyGo-LoRa-Series issues #258, #156 (ESP32-D0WDQ6-V3, "PSRAM: 4.00MB"): https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/issues/258 , https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/issues/156
- RIOT TTGO T-Beam board doc: https://api.riot-os.org/group__boards__esp32__ttgo-t-beam.html
- Field log: `~/Downloads/dj8meh/2026-08-23-dj8meh-41-putty-1.log` (`[PSRM] 0`)
- Fleet counts: mcmap `nodes_query hardware=TBEAM`, `fleet_firmware`, 2026-09-05
