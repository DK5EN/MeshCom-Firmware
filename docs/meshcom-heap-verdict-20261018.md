# Heap verdict: T-Beam and the classic-ESP32 fleet (AU-17)

Date: 2026-10-18. Tree: `fork-dev` at `5a5fc7f7`, read-only analysis, no build or flash. Sources:
`tools/bench/runs/harness/au-w3-tbeam-matrix.txt` (10-18), `au1017_20261017-093623/dk5en92*.log`
(10-17), `src/esp32/fw_update_net.cpp`, `src/configuration_global.h`, the prebuilt arduino-esp32
2.0.17 `sdkconfig`, `tools/resource_baseline.json` (static map 2026-09-11), the Heltec V3
INSTRUMENT ELF of 2026-09-11 (symbol structure only).

## 1. Verdict (BLUF)

- **The T-Beam can pass the gate without a PSRAM board and without a web-flasher detour.** The gap
  is 5-10 KB. Three measured levers give 17-22 KB: the regression since 10-17 (8-13 KB), the AU task
  stack (4 KB) and the phone rings folded into the BLE pause (5 KB).
- **The 76000 B gate is correct.** One TLS session on this SDK costs about 60 KB (two fixed 16 KB
  record buffers plus the handshake). The dip to 9-10 KB at the forced gate proves it. Do not lower
  the gate; raise the free heap instead.
- **The drop since 10-17 is real and dynamic.** Same node, same net-console channel, same
  INSTRUMENT logging: BLE-paused free was 74.6-85.4 KB on 10-17 (c9e64344) and is 66-72 KB on
  10-18 (85b0c457 and later). The diff c9e64344..85b0c457 adds no large static array, so a service
  now holds 8-13 KB at run time. This is the first item to fix, and it also explains why the static
  resource baseline saw nothing.
- **The BLE pause frees about 25 KB, not 7-12 KB.** Job start 57.0 KB to paused 85 KB on 10-17,
  42-45 KB to 67-72 KB on 10-18. The campaign text compared a post-resume reading with the paused
  reading. The remaining BLE cost is the controller's static region, which no pause returns.
- **Structural fix for every small-RAM board:** move the download out of the application into
  Safeboot, where neither BLE, LoRa nor the rings exist and more than 200 KB are free. The app keeps
  only the check. Section 6.
- **Shedding services (W4) cannot close the gap on its own** (1-2 KB per service, measured), but it
  stays useful for boards that miss the gate by a few KB.

## 2. Heap map, T-Beam v1.2 (classic ESP32, no PSRAM in the build)

All numbers are internal 8-bit heap. "Measured" cites a log line; "estimate" is not measured on this
tree and needs the boot marks of section 7.

| Stage                                                           | Free heap                | Delta        | Source                                                        |
| --------------------------------------------------------------- | ------------------------ | ------------ | ------------------------------------------------------------- |
| Chip DRAM usable by software                                    | about 320 KB             |              | ESP32 datasheet (SRAM1 128 KB + SRAM2 200 KB, ROM reserve)    |
| Static `.data` + `.bss` + COMMON (`dram0_0_seg`)                | 114.3 KB used            | -114 KB      | `resource_baseline.json` 2026-09-11; src/ is about 42 %       |
| BLE controller region kept because `btInUse()` is true          | not in heap              | est. -25 KB  | Arduino core releases only BR/EDR at NimBLE init              |
| IDF/ROM reserved, task stacks of the core                       | not in heap              | est. -30 KB  | remainder                                                     |
| **Heap after Arduino init, before services** (`[HEAP] (init)`)  | **149.6 KB**             |              | measured 10-17, largest block 110.6 KB                        |
| WiFi STA driver, lwIP, netif, DHCP, DNS                         |                          | est. -40 KB  | Arduino profile: static RX 4, dynamic RX/TX 32, cache TX 4    |
| NimBLE host + controller dynamic part                           |                          | -25 KB       | measured by the pause (57.0 to 85.2 KB on 10-17)              |
| Web server listener, mDNS, NTP, UDP server link, ext UDP, KISS  |                          | est. -15 KB  | listeners are cheap; a connected TCP client costs 6-8 KB      |
| Net console listener + one session                              |                          | -1 to -8 KB  | M4 matrix: a second session costs up to 8 KB at the gate      |
| Console UART TX ring                                            |                          | -2.4 KB      | `CONSOLE_UART_TXBUF 2048`, measured 10-13                     |
| **Unexplained since 10-17**                                     |                          | **-8-13 KB** | 10-17 vs 10-18 logs, same channel, same build type            |
| **Idle, BLE advertising, no phone** (`[AU];heap;start`)         | **57.0 KB / 42-45 KB**   |              | 10-17 / 10-18                                                 |
| AU job task stack (allocated at job start)                      |                          | -12 KB       | `FWNET_TASK_STACK 12288`; high-water mark says < 3 KB used    |
| **BLE paused by the job** (`[AU];ble;pause;heap`)               | **74.6-85.4 / 66-72 KB** |              | gate wants 76000 B and a 17000 B block                        |
| TLS check session (api.github.com, 5-root PEM, streamed JSON)   |                          | -58-60 KB    | 69.2 KB measured peak in the gate comment; dip to 9.4-10.1 KB |
| Download: second TLS session + 4 KB staging block + flash write |                          | -62-64 KB    | reboot on the T-Beam when forced below the gate (AU-14 image) |
| After `[AU];ble;resume`                                         | 38-42 KB                 |              | 3-7 KB below job start right after the resume, min 33-39 KB   |

Two readings of the map matter for the decision:

1. The static side is already lean. Our own big arrays on the classic class are small today:
   `ringBuffer` 5,200 B, `phoneComStore` 3,072 B, `phoneStore` 2,048 B, `udpOutStore` 2,048 B,
   `meshcom_settings` 2,032 B, mheard tables about 5 KB, NBR matrix about 4 KB, dedup ring 350 B.
   The 2026-09-20 reclaim already turned the three output rings into byte rings (-8.5 KB).
2. The dynamic side is where the T-Beam lost ground: WiFi, BLE and the TLS session are 125 KB of
   the 150 KB that exist after init. Only one of the three (BLE) can be paused today.

## 3. Variables that drive heap allocation

### 3.1 Build-time, our tree

| Variable                                  | Value (classic)      | Where                                             | Effect on heap                                               |
| ----------------------------------------- | -------------------- | ------------------------------------------------- | ------------------------------------------------------------ |
| `FWNET_MIN_FREE` / `FWNET_MIN_BLOCK`      | 76000 / 17000        | `fw_update_net.cpp:84-93`                         | the gate; derived from the 69.2 KB measured TLS peak         |
| `FWNET_MIN_IFREE_PSRAM`                   | 24000                | `fw_update_net.cpp:102`                           | internal floor when mbedTLS runs on PSRAM (not on T-Beam)    |
| `FWNET_TASK_STACK`                        | 12288                | `fw_update_net.cpp:110`                           | allocated per job; high-water mark 9.5-11.8 KB unused        |
| `FWNET_BLK`                               | 4096                 | `fw_update_net.cpp:104`                           | staging block per job, must stay internal                    |
| `FW_UPDATE_ROOTS_PEM`                     | 5 root certificates  | `src/fw_update_roots.h`                           | `setCACert()` parses all five on every connect (est. 5-7 KB) |
| `MAX_RING`                                | 20                   | `configuration_global.h:344`                      | TX ring 20 x 260 B static                                    |
| `MAX_DEDUP_RING` / `MAX_LOG`              | 70 / 20              | `configuration_global.h:345-346`                  | dedup 350 B; `MAX_LOG` sizes the lazy raw-log ring           |
| `RING_BYTES_PHONE` / `_PHONECOM` / `_UDP` | 2048 / 3072 / 2048   | `configuration_global.h:349-351`                  | static byte rings in `loop_functions.cpp:569-575`            |
| `NBR_MAX_ROWS` / `EDGES` / `EXT` / `HZ`   | 64 / 256 / 48 / 48   | `configuration_global.h:498-510`                  | NBR matrix, classic family                                   |
| `MAX_EXTERN_QUEUE`, `EXTERN_MSG_JSON_BUF` | 2, 700               | `extudp_functions.cpp:72`, `extern_msg_json.h:23` | ext UDP queue 1 KB static, 700 B stack                       |
| `CONFIG_JSON_MAX`                         | 6144                 | `config_json.h:150`                               | `web_cfg_buf` malloc per config export/import (transient)    |
| `CONSOLE_UART_TXBUF`                      | 2048                 | `configuration_global.h:602`                      | heap-allocated UART TX ring                                  |
| `MSGSTORE_SLOTS_MAX`                      | 50 (not on T-Beam)   | `msgstore_api.h:19`                               | about 190 B per slot on boards with MSGSTORE                 |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` etc.   | 1 / 1 / 2 / 4 blocks | `platformio.ini:1487-1493`                        | NimBLE already at the floor (msys 4, host stack 3072)        |
| `BOARD_HAS_PSRAM`                         | unflagged            | `variants/ttgo_tbeam/platformio.ini:46-48`        | 4 MB PSRAM on the bench v1.2 unused; trade for 4.4 KB IRAM   |
| `INSTRUMENT_ENABLED`                      | bench builds         | build flag                                        | capture ring 768 B, sampler, heap lines                      |

### 3.2 Prebuilt SDK (arduino-esp32 2.0.17, not changeable without a custom framework build)

| Variable                                  | Value                                   | Effect                                                                    |
| ----------------------------------------- | --------------------------------------- | ------------------------------------------------------------------------- |
| `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN`      | 16384                                   | 16 KB in + 16 KB out per TLS session, allocated at handshake              |
| `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN`   | not set                                 | out buffer could be 4 KB (-12 KB) with a rebuilt SDK                      |
| `CONFIG_MBEDTLS_DYNAMIC_BUFFER`           | not set                                 | buffers could shrink after the handshake (-30 KB) with a rebuild          |
| `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC`       | y                                       | mbedTLS internal-only unless `mbedtls_platform_set_calloc_free()`         |
| `CONFIG_LWIP_TCP_WND_DEFAULT` / `SND_BUF` | 5760 / 5760                             | per TCP connection while data flows                                       |
| `CONFIG_LWIP_MAX_SOCKETS`                 | 16                                      | socket table                                                              |
| WiFi init profile (Arduino, non-static)   | RX 4 static, 32 dyn; TX 32 dyn, 4 cache | already the low-memory profile; `WiFi.useStaticBuffers()` would cost more |
| `CONFIG_ARDUINO_LOOP_STACK_SIZE`          | 8192                                    | loop task stack                                                           |
| `CONFIG_BTDM_CTRL_MODE_BTDM`              | y                                       | controller region sized for dual mode; BR/EDR share released by NimBLE    |

### 3.3 Runtime settings that add permanent heap

`--gateway on` (server link UDP socket, KEEP/BEAT), `--extudp on`, `--kiss tcp` (listener plus
6-8 KB per client), `--netconsole on` (listener plus 1-8 KB per session), the web server (one
client at a time, page `String` building), a connected phone (NimBLE connection, MTU buffers),
opening the web rxlog page once (`rawLogEnsure()` calloc, never freed), display log on.

## 4. Functions that make heavy use of the heap

| Function                                                                 | File                                              | Heap behaviour                                                                                                      |
| ------------------------------------------------------------------------ | ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `fwNetTask()` check job (`WiFiClientSecure` + `HTTPClient`, `setCACert`) | `fw_update_net.cpp:621-669`                       | one TLS session, about 60 KB peak; JSON is streamed through `FwJsonScan`, so the 65 KB release list is not buffered |
| download job (`stagePrepare`/`stageFinish`, second `WiFiClientSecure`)   | `fw_update_net.cpp:886-1130`                      | second TLS session plus `FWNET_BLK`; reboots the T-Beam when started below the gate (AU-14)                         |
| `heapGateOk()` / `tlsAllocInstall()`                                     | `fw_update_net.cpp:257-270, 583-610`              | the gate; PSRAM-first calloc only when `psramFound()`                                                               |
| `esp32BleStart()` / `NimBLEDevice::init()`                               | `esp32_main.cpp:526-600`                          | 25 KB host + controller dynamic; `esp32BlePause()` returns it via `deinit(true)`                                    |
| `esp32BleResume()`                                                       | `esp32_main.cpp:690-745`                          | re-init; heap right after the resume is 3-7 KB below the job start (check whether it recovers)                      |
| `work_webpage()` and page handlers (100 `String` uses)                   | `web_functions/web_functions.cpp`                 | page text built in `String`s, `web_header.reserve()`, fragmentation source (largest block 20-39 KB on a 67 KB heap) |
| config export/import (`web_cfg_buf = malloc(CONFIG_JSON_MAX+1)`)         | `web_functions.cpp:491`                           | 6,145 B transient                                                                                                   |
| `rawLogEnsure()`                                                         | `loop_functions.cpp:540`                          | `calloc(MAX_LOG, sizeof(rawLogLine_t))`, about 5 KB, allocated on first rxlog page view, never freed                |
| `setparam()` / `getparam()` / `call_function()` (`JsonDocument`)         | `web_functions.cpp:4733-4850`                     | ArduinoJson documents, transient                                                                                    |
| `MDNS.begin()`                                                           | `web_functions.cpp:272`                           | mDNS responder task and records (est. 2-4 KB)                                                                       |
| net console accept path (`new AuthArgs`, `con_auth` task 3072 B)         | `net_console.cpp:475-520`                         | per connection attempt; session `WiFiClient` with lwIP windows                                                      |
| `console_txbuf_apply()`                                                  | `esp32_main.cpp`                                  | 2 KB UART TX ring                                                                                                   |
| `Udp`, `UdpExtern` (`WiFiUDP`)                                           | `udp_functions.cpp:88`, `extudp_functions.cpp:61` | lwIP pbuf per datagram held until `parsePacket()`/`flush()`                                                         |
| `printfdeb()` paths through `Print::printf`                              | `printfdeb_functions.cpp`                         | malloc above 64 B per line (fixed with `write()` on the hot path, 2026-08-18)                                       |
| KISS TCP server                                                          | `kiss_functions.cpp`                              | `WiFiServer` plus one client, 1-2 KB measured at the gate (M3)                                                      |

## 5. What to tune now (current firmware)

Ordered by gain per risk. Gains are at the gate (BLE paused). Target: 76000 B with margin from
66-72 KB today.

| #   | Lever                                                                                                       | Gain at the gate           | Risk / cost                                                                                                                                                                                                                                                                                                                                                                                                     |
| --- | ----------------------------------------------------------------------------------------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | **Find and revert the run-time growth c9e64344 -> 85b0c457**                                                | 8-13 KB                    | Four builds to bisect: b06a6907 (net console AP/accept), 4d0d1756 (RM1 net/upd), e87e8a5e (upstream #1213/#1214), 85b0c457. Compare `[HEAP] (init)` and `[AU];heap;start` over the same channel. Suspects: the net console accept path (P-0168 takes over the old session, a backlog client may stay open), udp/gw_servers changes. No big static arrays in the diff, so look for sockets, tasks and `String`s. |
| 2   | `FWNET_TASK_STACK` 12288 -> 8192                                                                            | 4 KB                       | High-water mark 9.5-11.8 KB unused through check and download on both days; 8 KB keeps a 5 KB margin. Verify `stk` stays above 4 KB on the bench.                                                                                                                                                                                                                                                               |
| 3   | Fold `phoneStore` and `phoneComStore` into the BLE pause (allocate at BLE start, free in `esp32BlePause()`) | 5 KB                       | The gate always runs with BLE paused. Needs an allocation-failure path at resume (fall back to "BLE off, retry at next boot").                                                                                                                                                                                                                                                                                  |
| 4   | Trim `FW_UPDATE_ROOTS_PEM` to the roots GitHub actually chains to, or switch to a small cert bundle         | est. 3-5 KB lower TLS peak | Lowers the peak instead of raising free heap; the gate constant would follow the re-measured peak. Verify the chains of `api.github.com` and `objects.githubusercontent.com` first.                                                                                                                                                                                                                             |
| 5   | `udpOutStore` lazy (only with `--gateway on` or `--extudp on`)                                              | 2 KB non-gateway           | No gain on gateway nodes like DK5EN-92.                                                                                                                                                                                                                                                                                                                                                                         |
| 6   | Close the bench net-console session before the job, or run the job over USB on the bench                    | 1-8 KB                     | Measurement hygiene only; field nodes have no session.                                                                                                                                                                                                                                                                                                                                                          |
| 7   | Pause/resume hygiene: confirm the 3-7 KB missing right after `esp32BleResume()` returns                     | 0-5 KB per cycle           | If it does not recover, every automatic check leaks a little (the 0.1-0.3 KB per check drift).                                                                                                                                                                                                                                                                                                                  |

Levers 1-3 together: 17-22 KB. That puts the T-Beam at 83-94 KB paused, above 76000 B with margin,
and the download (which needs a few KB more than the check) stops rebooting. Levers 4-7 are
secondary.

Not worth doing: lowering the gate (the 9-10 KB dip proves the peak), raising
`MAX_DEDUP_RING` or shrinking it (350 B), fonts and `cfg_fields` (already in flash), NimBLE flags
(already at the floor), WiFi buffer counts (Arduino already applies the low-memory profile and
`esp_wifi_init` is not reachable from the sketch).

## 6. What to do for the future (T-Beam and every small-RAM node)

### 6.1 Recommended: download in Safeboot, check in the app

- The application keeps only the check (one TLS session, about 60 KB peak), writes the chosen
  asset URL, size and SHA-256 into the staged record and reboots into Safeboot.
- Safeboot (factory slot, 704 KB) downloads, verifies and installs with no BLE, no LoRa, no rings
  and no web GUI: well over 200 KB free. The AU-14 reboot-in-download class disappears with it.
- Cost: TLS in Safeboot. The Safeboot env is built with `HTTPCLIENT_NOSECURE`; `WiFiClientSecure`
  plus mbedTLS adds roughly 150 KB of flash. Check the current Safeboot image size against the
  704 KB slot before committing to this. The Safeboot runs on the Tasmota platform (IDF 5 core),
  whose mbedTLS defaults differ from the 2.0.17 app core; measure, do not assume.
- A node that fails the check gate still needs the lever list of section 5; the check alone passes
  on the T-Beam today when BLE is paused and the 10-17 heap is restored.

### 6.2 Variant without any TLS on the node

- Version discovery through a signed manifest (ed25519, a few KB of code, under 2 KB of RAM) served
  over plain HTTP from infrastructure we control, with the asset SHA-256 inside the manifest; the
  asset itself mirrored on plain HTTP. GitHub serves releases and Pages over HTTPS only, so this
  needs a mirror and a signing key in the firmware. Integrity is then equal to the TLS path;
  confidentiality is not needed for public firmware.
- Biggest RAM win (no TLS at all), highest operational cost (mirror, key management). Keep as the
  fallback if 6.1 does not fit the Safeboot slot.

### 6.3 PSRAM T-Beam variant (only if 6.1 is not done)

- The bench v1.2 has 4 MB PSRAM (4,191,863 B free at init, measured 2026-09-05). With
  `BOARD_HAS_PSRAM` the existing PSRAM-first allocator (c9e64344) moves the whole TLS session out
  of internal RAM and the gate drops to 24000 B internal.
- Cost: 4.4 KB of IRAM. The T-Beam has about 4.8 KB free after the 2026-09 unflag (and KISS took
  124 B), so this leaves a few hundred bytes and any new libc member in IRAM breaks the link. The
  fleet is mixed (a v1.2 field node reports `[PSRM] 0`), so the build must stay correct with
  `psramFound() == false`. Fragile; not the first choice.

### 6.4 SDK migration (long term, upstream decision)

- Arduino core 3.x / IDF 5 (pioarduino or Tasmota platform, already used for the Safeboot envs)
  allows a rebuilt mbedTLS with asymmetric or dynamic buffers (-12 KB or -30 KB per TLS session).
  Whether the prebuilt Tasmota libraries already set these must be verified in their sdkconfig.
  This is a whole-firmware migration (NVS behaviour, driver APIs, PSRAM flags) and belongs
  upstream, not in the AU-17 fix.

### 6.5 Heap governance (so this does not repeat)

- Add a dynamic figure to the release gate: idle free internal heap with BLE up on the classic
  bench node, recorded next to `resource_baseline.json`. The static baseline could not see the
  8-13 KB regression because it is dynamic.
- Print `[HEAP]` marks after each service start at boot (WiFi up, web server, mDNS, UDP, KISS,
  net console, BLE) so a drop is attributable from one boot log without bisecting. Today there is
  one `(init)` mark and a periodic `(mon)` line.
- Rule for new code on the classic class: no new permanent allocation or static array without a
  T-Beam figure in the commit message; prefer lazy allocation tied to the feature switch (the
  `rawLogEnsure()` pattern, plus a matching free when the switch goes off).
- Keep the INSTRUMENT heap sampler and the matrix script; rerun the W3 matrix after every lever.

## 7. Open points to measure before the decision is final

1. Bisect lever 1 with four builds; the `(init)` line tells static from dynamic in one boot.
2. Confirm `uxTaskGetStackHighWaterMark()` units (ESP-IDF returns bytes) by reading `stk` after a
   deliberate deep call; then set `FWNET_TASK_STACK`.
3. Read the heap right after `esp32BleResume()` and again 60 s later (lever 7).
4. Current Safeboot image size versus the 704 KB slot, and the Tasmota core's mbedTLS sdkconfig
   (sections 6.1, 6.4).
5. Chains of the two GitHub hosts for lever 4.

## 8. Decision for AU-17

Recommended: keep the gate, fix the regression (lever 1), apply levers 2 and 3 in the same wave,
re-run the W3 matrix, and schedule 6.1 (download in Safeboot) as the structural follow-up for the
whole classic fleet. Documenting "AU needs a PSRAM board or the web flasher" is not necessary; the
T-Beam was 8-13 KB above today's level two days ago with the same code path.
