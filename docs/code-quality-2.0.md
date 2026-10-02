# Code Quality 2.0 -- Error Patterns Learned in the Field

**Scope:** every coding session on this repository from 2026-08-27 to 2026-09-01 (49 transcripts),
the fix wave in git from 2026-08-18 to 2026-09-01 (about 85 fix/revert/test commits plus the
BP/TM/UP wave commits), and the bug, review and verdict documents under `docs/`.
**Companion:** `docs/codequality-rules.md` (called "1.0" below) is the generic ESP32/FreeRTOS rule
set. This file is the project-specific layer on top of it: the classes of mistake that actually
shipped, why each one got past review, and a check that finds the class in under a minute.
**Tree at time of writing:** `v4.35p_prio` @ `9c49845c` (2026-09-01).

How to read an entry:

- **Mechanism** -- what the bug does, in two or three sentences.
- **Why it slipped** -- the review failure, not the bug mechanism. This is the part to learn from.
- **Seen in** -- defect IDs from `docs/BACKLOG.md`, `docs/architecture/08-defect-catalogue.md` and
  `docs/CHANGELOG-stability.md`. Follow the ID for file and line; line numbers are not repeated here
  because they drift within hours in this repository (see P06).
- **Detector** -- a command or check that finds the class. Run from the repository root. Greps use
  `-a` where a source file is not UTF-8 (`src/aht20.cpp` is ISO-8859 and BSD grep skips it as
  binary otherwise).
- **Rule 1.0** -- the rule in `codequality-rules.md` that covers it, or `GAP`.

Frequency marks: `[xN]` = distinct occurrences counted across the sources. The review checklist in
Part D is ordered by frequency times damage.

---

## Part A -- Code-defect patterns

### C01 -- Length taken from the wrong quantity `[x9]`

**Mechanism.** A copy or serialisation bound comes from the destination's `sizeof`, from the
frame's total length, from a measured JSON length, or from a byte count where the API wants pixels.
The copy over-reads the source (heap or stack bytes go on the air or into JSON) or overruns the
destination by exactly the difference.

**Why it slipped.** The bound references a real constant (`sizeof(dst)`, `UDP_TX_BUF_SIZE`,
`measureJson()`), so a reviewer ticks "bounded". In N-05 the correctly clamped variable existed one
line above and was unused. RadioLib's `readData(buf, len)` takes `len` by value; the header comment
that says so was never read, and every ESP32 receive was booked as 255 bytes for months (N-29).

**Seen in.** N-05, N-29, UP-01, JSN-01, WF-01, G07, TXRING-CLEARSLOT-GAP, CONC-16 side finding
(`convBuffer`), PT-01 finding 5.

**Detector.**

```sh
grep -rnaE 'memcpy\([^,]+,[^,]+,\s*sizeof\(' src            # each hit: source must be a fixed array of that size
grep -rnaE 'serializeJson\([^,]+,[^,]+,\s*measureJson' src   # must be 0 (comments only today)
grep -rna 'readData(' src                                    # getPacketLength() must precede it
grep -rnaE 'memset\([^,]+,\s*0x?0*,\s*[A-Z_]+\s*\+\s*[0-9]+\)' src   # compare with the declared array size
grep -rn 'lv_disp_draw_buf_init' src/t-deck                  # size argument must not contain sizeof(lv_color_t)
```

**Rule 1.0.** BND-03/04 cover the write side. GAP: "the length derives from the producer; APIs that
take a length by value do not return one".

### C02 -- Narrow or platform-dependent length arithmetic `[x8]`

**Mechanism.** A length lives in `uint8_t` and `+2`/`+4`/`-1`/`-2` wraps at 255 or 0; the result
feeds `memcpy`, a BLE write of one byte, or an oversized message. `strncat(dst, src, sizeof(dst)-1)`
bounds the source, not the remaining room. `unsigned long` is 4 bytes on the target and 8 on the
native test host, so a rollover test can pass on the host and fail on the node.

**Why it slipped.** The wrap happens two functions away from the clamp, often in another file. The
ESP32 and nRF52 `write()` overloads promote `blelen+2` differently, so the bench board hid it. The
`strncat` idiom looks bounded and nobody summed the worst case against the destination (about 111
bytes into `char[100]`, latent only because a later budget check runs afterwards; bounded by the
remaining room since 2026-09-02).

**Seen in.** N-04, BUG-07, BUG-08, `blelen+2` (issue-ble-i-register-mtu), `cfwdate[20]`, issue
#1120 analysis (`PositionToAPRS()` strncat), NC-01 design note (`uint32_t` vs `unsigned long`).

**Detector.**

```sh
grep -rnaE '\buint8_t\s+\w*len\w*\b' src                     # then every +/- on that variable
grep -rnaE 'strncat\([^,]+,[^,]+,\s*sizeof\([^)]+\)\s*-\s*1\)' src   # must be sizeof(dst)-strlen(dst)-1 or snprintf
grep -rnaE 'unsigned long\s+\w+(\[|\s*=\s*millis)' src       # in any TU that is in a native build_src_filter: use uint32_t
```

Add `-Wconversion` and `-Wformat-truncation=2` to `build_src_flags` (the latter found `cfwdate`).

**Rule 1.0.** Section 11 and COMP-02/03 say it; nothing enforces it in the build.

### C03 -- The guard that exists is not the one that binds `[x8]`

**Mechanism.** A check is present but tests a different limit than the consumer applies, runs after
the dangerous loop, or its result is discarded. The BLE register builders checked 298 while the
stage below clamped at 245 and a byte wrap sat at 253; the I register went 247 characters long and
apps dropped node identity for a day.

**Why it slipped.** A comparison satisfies a checklist. Nobody traced the value into its consumer and
asked which of the limits is smaller. The commit message that introduced the field said "es bleibt
also Reserve" against the wrong constant, and the reviewer accepted the commit message as the size
proof.

**Seen in.** issue-ble-i-register-mtu, UP-05, N-04, SEC-04 (check after the loop), N-05 (unused
clamp), BUG-13 (`bPayloadEndOk` proves a NUL, not a fit), BP-10 H2 (three meanings of `-1`).

**Detector.**

```sh
grep -rna 'just for safety\|for safety' src                  # the phrase marks a guess; trace the variable to its consumer
grep -rnaE 'MAX_MSG_LEN_PHONE\s*-\s*2' src                   # must be comments only
grep -rna -A2 'printfdeb("\[ERR\]' src | grep -v 'len =\|return\|break'   # log-without-action
grep -rn 'return -1' src/txring_functions.cpp                # more than one cause: every caller doing if(w<0) needs a cause check
```

**Rule 1.0.** BND-03 and Section 3. GAP: "a bound is verified against the consuming limit; an unused
check result is a defect".

### C04 -- Outbound frame or JSON budget not derived from the binding limit `[x6]`

**Mechanism.** BLE frames are bounded at 244 usable JSON characters and EXTUDP datagrams at 255;
fields that grow with hop count, six group-call slots, or JSON escaping that doubles a quoted text
push the document over the limit. A byte clamp then cuts mid-value and the app loses every field, not
the last one. ArduinoJson 7.4 `serializeJson()` returns the buffer size on truncation and leaves the
buffer unterminated, so an `if (len == 0)` guard never fires.

**Why it slipped.** Three limits in three files with no shared name; the failure is a dropped frame
with no log line; the worst case (PL=7 HEY chain, 119-character callsign) never occurs on a bench
node. The `serializeJson` return semantics were assumed, not read.

**Seen in.** issue-mh-json-size-budget, UP-01, UP-05, JSN-01, BP-07 W6, BUG-11, PT-01 finding 5.

**Detector.**

```sh
grep -rna 'serializeJson(' src | grep -v 'bleJsonFrame\|sizeof\|//'      # every remaining call needs a sizeof bound and a >= size check
grep -rnaE 'snprintf\([a-z_]+,\s*[0-9]+' src                  # hard numeric size next to %s of a bounded field
```

Every builder gets a compiled worst-case probe: fill every field to its maximum and assert
`measureJson(doc) <= BLE_JSON_PAYLOAD_MAX` (`test_ble_json_frame` has the canary variant). Every
clamp on structured output fails soft (drop optional fields, re-measure) or drops the frame; the
clamped output is parsed by a real parser in a test.

**Rule 1.0.** MEM-04 (worst-case static allocation), Section 18 (inbound body size). GAP for
outbound budgets and for "truncation must preserve syntax".

### C05 -- Off-by-one at the buffer end, and the partial-read wedge `[x6]`

**Mechanism.** `read(buf, N)` into `buf[N]` followed by `buf[len] = 0`; zero-scan loops reading
`buf[i+1]` at `i == N-1`; `uformat[in-1]` at `in == 0`; `len + 1` copies. Fixing the read to `N-1`
then created UDP-02: arduino-esp32 `WiFiUdp` never frees a partially consumed datagram, so every later
datagram was ignored until reboot. `EthernetUDP` discards the remainder itself, so the RAK never
showed it.

**Why it slipped.** Two library semantics pulling in opposite directions; no 255-byte datagram on
the bench until the TM-43 control run on the Heltec.

**Seen in.** SEC-05, SEC-06, BUG-12, N-30 side finding, SEC-04, WF-01, UDP-02.

**Detector.**

```sh
grep -rnaE '\.read\(\s*\w+,\s*(UDP_TX_BUF_SIZE|sizeof\([^)]+\))\s*\)' src   # followed by [len]=0 is the overflow
grep -rnaE '\.read\(.*UDP_TX_BUF_SIZE' src                    # every partial read needs flush() or a buffer >= max datagram
```

Boundary suite 0 / 1 / max / max+1 per parser; `pio test -e native_aprs_fuzz`.

**Rule 1.0.** BND-04, Section 11, Section 19 "max+1 bytes". Covered; the code did not follow it.

### C06 -- Platform copy drift between ESP32 and nRF52 `[x19]`

**Mechanism.** The firmware carries two hand-copied implementations of the UDP, serial, settings,
battery, GPS-helper and main-loop paths. A fix lands in one copy; the other keeps the bug. A guard
list omits a sibling board. A bench hook or instrument compiles on one platform only. A fix is made
in the file the reporting board does not even compile (`batt_functions.cpp` vs
`batt_function_old.cpp` on Heltec V3). New shared code uses an API that exists on one Arduino core
only (`IPAddress::toString()`).

**Why it slipped.** Audits grep one file. "Not linked on nRF52" was asserted from misread
`--gc-sections` output. A fix author fixes where the symptom was seen. The catalogue knew the
duplication (DRY-20..22) and every wave still found a new instance.

**Seen in.** DRY-21, DRY-22, DRY-25, CONC-16 rest, N-03, CONF-01, ETH-01, CTY-01, TM-45, BAT-01
mirror, `#ifndef BOARD_RAK4630` as "is ESP32", `oledStat()` guard, SIMP-30, TM-31 dedup order
(correct on nRF52, wrong on ESP32), NTP-01 gate asymmetry, TM-12 instrument ESP32-only, TM-32
`node_ntp` missing on nRF52, `save_settings()` `void` vs `bool`.

**Detector.**

```sh
# Pair diff per function; maintain this list and run it on every fix touching one side:
#   getMeshComUDPpacket (udp_functions.cpp)   <-> NrfETH::getUDP (nrf52/nrf_eth.cpp)
#   sendMeshComUDP                             <-> sendUDP (nrf52_main.cpp)
#   checkSerialCommand (esp32_main.cpp)        <-> checkSerialCommand (nrf52_main.cpp)
#   startNetwork                               <-> startETH
#   read_batt / battDetectUpdate (batt_functions.cpp) <-> batt_function_old.cpp
#   gateway block of esp32loop                 <-> gateway block of nrf52loop
git show --stat HEAD -- src/udp_functions.cpp src/nrf52/nrf_eth.cpp src/esp32/esp32_main.cpp src/nrf52/nrf52_main.cpp
# a hit in only one file of a pair is a review question
for f in src/udp_functions.cpp src/nrf52/nrf_eth.cpp; do echo $f; grep -n '"GATE"\|"BEAT"\|"CONF"\|is_new_packet\|addLoraRxBuffer' $f; done
grep -rnaE '#if(n| !)def(ined)?\s*\(?BOARD_RAK4630' src       # platform tested by board name
grep -n "USE_NEW_BATT" variants/<board>/configuration.h platformio.ini   # which TU does the reporting board link?
nm .pio/build/<env>/src/<file>.o | grep <symbol>              # the only proof that a fix is in the reporting board's binary
diff <(grep -o 'node_[a-z_0-9]*' src/esp32/esp32_flash.h | sort -u) <(grep -o 'node_[a-z_0-9]*' src/nrf52/WisBlock-API.h | sort -u)
```

Build `wiscore_rak4631` and one ESP32 env after every edit to a shared file.

**Rule 1.0.** Section 17 covers constants only. GAP: paired-file drift.

### C07 -- Preprocessor and build-config hygiene `[x14]`

**Mechanism.** A macro tested by value evaluates a product name arithmetically (`(BOARD_E22_S3)`
without `defined()`). A macro defined nowhere selects a dead branch (`BOARD_HELTEC_V31`, `HEAP_TEST`,
`T_DECK_SPIFFS`; the "SD persistence" the GUI offers is compiled out). A `.cpp` defines a per-variant
option unconditionally and overrides 15 `configuration.h` files. A duplicate config file next to the
sources is the one that is not on the include path (`src/t-deck/lv_conf.h` is dead;
`variants/t_deck_plus/lv_conf.h` compiles). `build_src_filter` lists are duplicated per env, so a
new `.cpp` dependency links in three envs and fails in five. A symbol arrives through a transitive
include that a variant's `-D` flag removes. Bench instrumentation defaults on in every board build
and blows the DRAM segment of the smallest variant.

**Why it slipped.** `-Wundef` is unusable on ESP-IDF headers. A guard that is accidentally true has
no symptom. A file that sits next to the sources it configures looks canonical; the orchestrator
wrote the dead file's values into a brief and all eight research agents inherited them. The wave
gate built 5 to 8 envs; the release needs 32.

**Seen in.** N-10, CFG-01, DRY-25, `BOARD_HELTEC_V31`, `HEAP_TEST`, `T_DECK_SPIFFS`, N-25 A-1..A-4
(`GPS_BAUDRATE_SOFTCHECK`), two `lv_conf.h`, `oledStat()` T114/T-Echo, `--srvip` hook on E22_XML,
CHR-01 link failures in three native envs, `MC_SAFEBOOT`, `MC_CAPTURE` DRAM overflow, TM-12.

**Detector.**

```sh
# macros tested but never defined
comm -23 <(grep -rhoaE 'defined\([A-Z_0-9]+\)|#ifn?def [A-Z_0-9]+' src | grep -oE '[A-Z_0-9]{4,}' | sort -u) \
         <(grep -rhoE '[A-Z_0-9]{4,}' platformio.ini variants boards src/*.h | sort -u)
# variant options redefined inside a TU
for m in $(grep -rhoE '^#define [A-Z_]+' variants/*/configuration.h | awk '{print $2}' | sort -u); do grep -rlan "^#define $m\b" src/*.cpp; done
find . -name lv_conf.h -not -path './.pio/*' -not -path './lib/*'   # more than one per target: check pio project metadata
grep -n '^\[' variants/*/platformio.ini | awk -F: '{print $3}' | sort | uniq -d   # duplicate section names
grep -n "+<aprs_functions.cpp>" platformio.ini                # after adding a dependency: every env listing the TU needs the new one too
pio project metadata --json-output-path m.json -e <env>       # before and after any platformio.ini edit; diff must be intended
python3 tools/resource_watch.py dram --min-headroom 4096 .pio/build/<env>/firmware.map
```

Build one env per distinct `build_src_filter` / `-D` set before any commit touching `src/`: both
safeboot envs, `E22_XML-DevKitC`, `heltec_t114`, `t_echo`, `wiscore_rak4631`, `t_deck`. Note that
`t_deck_pro` excludes `src/t-deck/*`; a green `t_deck_pro` proves nothing for T-Deck code.

**Rule 1.0.** COMP-01 catches none of these. GAP: `defined()`-only tests, no option redefinition in
TUs, section-name uniqueness, DRAM headroom gate, one config file per target.

### C08 -- Blocking work on the loop task or in the RX callback `[x22]`

**Mechanism.** Synchronous `delay()`, a full WiFi channel scan, `hostByName()`, the stock
`NTPClient`, `Ethernet.begin(mac, 10000)`, software-I2C page pushes at 579 ms per frame, a 2-second
boot animation per message, a 1.1-second CW tone, `SD.exists()` per incoming message, an SD open plus
PNG decode inside an LVGL `read_cb`, or a driver getter that waits for another task
(`WiFi.getMode()` blocks 2.9 s while the driver scans) runs on the task that services LoRa RX, serial
and the display. The node is deaf for seconds; on ESP32 the task watchdog may fire. A long sequence
inside one `loop()` iteration trips the watchdog even with `delay()` calls, because `delay()` does
not feed it.

**Why it slipped.** There was no loop-period instrument until TM-13 (`[INSTR-LOOP];gap`). "Cheap
status read" was assumed for `WiFi.getMode()`. The first explanation of a map slowdown was an
input-acceleration argument; only a timestamp-gap scan of the raw log exposed the 7-second hole.
Audits searched for `delay()` and busy loops, not for library calls.

**Seen in.** WDT-01, BATT-01, N-25 B-1..B-15, TM-20, TM-01..04, N-20, N-23, TM-35, TM-34 F6,
`WiFi.getMode()` heartbeat, TM-09, TM-15, UP-02, TM-08, TM-41, TM-44, G05, G06.

**Detector.**

```sh
grep -rnaE '\bdelay\(\s*[0-9]{2,}\s*\)' src --include='*.cpp' | grep -vi 'setup'
grep -rnaE 'scanNetworks\(\)|hostByName|Ethernet\.begin|forceUpdate\(|WiFi\.getMode|esp_wifi_get_|SD\.exists|SD\.open|i2s_write' src
grep -n "for(.*delay(" src/t-deck/*.cpp                       # multi-second loop inside loop() context without esp_task_wdt_reset()
grep -rna 'delay(' src/t-deck/lv_obj_functions.cpp src/t-deck/event_functions.cpp src/lora_functions.cpp   # any hit reachable from lv_task_handler or OnRxDone
```

Runtime gate: `--instr` after 60 s idle, `INSTR-LOOP max_us` under 50 ms; a 24-boot arm with zero
`[INSTR-LOOP];gap` lines; `[ETH];stall` and `[WIFI];stall` scoped timers.

**Rule 1.0.** STAB-03/04 state the goal. GAP: an allow-list of blocking APIs per task, and "RX
callback code is allocation- and delay-free".

### C09 -- Watchdog scope and the persisted-setting trap `[x5]`

**Mechanism.** Arming the task watchdog as the first statement of `esp32setup()` put 5 to 16 s of
legitimate init under a 5-second watchdog (boot loop, release withdrawn). A persisted toggle
(`--gps on`, `--extudp on`) is written to flash before the code path it enables blocks or crashes;
every subsequent boot repeats the failure and the node cannot be reached to undo it.

**Why it slipped.** The June fix was tested on a board without GPS or WiFi. No release pass boots
with each persisted flag on and the corresponding hardware absent. A boot loop looks like a hardware
fault. Feeding the watchdog inside an unbounded loop converts a panic into a silent freeze.

**Seen in.** N-17, N-25 S1/S4, N-23, WDT-01.

**Detector.**

```sh
grep -n 'esp_task_wdt_add' src/esp32/esp32_main.cpp           # must be the last statement of esp32setup()
grep -rna 'meshcom_wdt_feed' src                              # each site inside a loop with a proven upper bound
grep -rna 'esp_reset_reason\|RESETREAS' src                   # boot banner must log the reset reason (TM-51: ESP32 [BOOT] RESET_REASON since 2026-09-02)
```

Bench: boot with every `--x on` persisted flag, one at a time, on a board without that hardware.

**Rule 1.0.** STAB-01/02 satisfied and still wrong. STAB-05 (crash counter, safe mode) is not
implemented and would have broken the loop. GAP: "a persisted setting must not make the next boot
unrecoverable".

### C10 -- Multi-writer state on nRF52 and the task-context map `[x9]`

**Mechanism.** On nRF52 `OnRxDone` runs on the FreeRTOS timer-service task (priority 2, 1 KB
stack), BLE callbacks on the Bluefruit task, ElegantOTA on `async_tcp`. Rings, the settings struct
and OTA timers were written from two of these contexts with no lock; atomic indices were mistaken for
mutual exclusion of the multi-step slot fill. A received DM triggers `SendAckMessage()` and thereby
`save_settings()`, a LittleFS write from the timer task (boot-loop class). `sendHey()` still writes
flash per send. The overflow-eviction path relocates the `iRead` entry with `memcpy`, so a "lock-free
sweep that only writes len=0" would silently delete a relocated CRITICAL DM.

**Why it slipped.** The concept document mis-modelled the `OnRxDone` context twice (ISR, then LORA
task at priority 2) before the timer task was derived on 2026-07-31. The June "atomic iWrite/iRead"
fix was recorded as closing the multi-writer ring. A plan inferred call context from `doTX()` only
and missed the CSMA helper chain into `getNextTxSlot()`.

**Seen in.** CONC-14..18, N-14, N-22, TM-46 race, BP-03 plan review, advisor-dm M5/M6, N-15.

**Detector.**

```sh
# before adding ring writes or prints to any txring_functions.cpp function:
grep -n '<fn>(' src/lora_functions.cpp src/esp32/*.cpp src/nrf52/*.cpp    # walk every caller up to loop() or OnRxDone
grep -rna 'addTxRingEntry(\|addLoraRxBuffer(' src | grep -v loop_functions.cpp   # list each caller's task per platform
grep -n 'save_settings()' src/loop_functions.cpp              # each enclosing function: reachable from OnRxDone?
awk '/^void sendHey/,/^}/' src/loop_functions.cpp | grep -c save_settings   # per-send flash writes (still 1)
grep -n 'memcpy(ringBuffer\|ringPriority\[.*\] = ringPriority' src/txring_functions.cpp   # relocation: every other mutator needs check+act under the same lock
```

Stronger: `configASSERT(xTaskGetCurrentTaskHandle() == loopTaskHandle)` at the top of
`save_settings()` on `NRF52_SERIES`. Maintain `docs/architecture/09-concurrency-map.md`.

**Rule 1.0.** RACE-03, ISR-01/02, "never block in timer callbacks" cover it once the author knows
which task runs the callback. GAP: the task/context map as a maintained artefact; NVS write budget.

### C11 -- Over-synchronisation, work inside critical sections, wrong primitive `[x9]`

**Mechanism.** Atomics and spinlocks where only one context exists (ESP32 `OnRxDone` runs on the
loop task); a spinlock around seven `String` heap copies with interrupts off; `Radio.Send()` (which
calls `delay(1)`) inside `taskENTER_CRITICAL()`; a mutex re-created while another task held the old
handle; a binary semaphore used as the SPI bus mutex with exactly one taker while SD reads never take
it (the first TFT flush after an SD access is lost); an audio task created at priority 50 when
`configMAX_PRIORITIES` is 25 (silently clamped to 24, above WiFi and BLE); a semaphore given right
after task creation, before the resource it guards is finished with.

**Why it slipped.** The June audit added atomics on the principle "shared means atomic" without a
context map. `taskENTER_CRITICAL` "protects", and nobody looked at what `Radio.Send()` does inside.
A semaphore with one taker looks like arbitration; nobody grepped for the second party. The priority
number was chosen without reading the config, and the finder had to disassemble `libfreertos.a` to
prove the clamp.

**Seen in.** N-13, N-16, CONC-19, TM-01..04 (audio), C1..C4 (tdeck-gui-verdict), G15, TM-05/TM-07.

**Detector.**

```sh
grep -rna -A10 'taskENTER_CRITICAL\|portENTER_CRITICAL\|vTaskSuspendAll' src | grep -E 'delay\(|printf|String|malloc|Radio\.|Serial\.'   # must be empty
grep -rna 'xSemaphoreCreateMutex\|xSemaphoreCreateBinary' src   # one creation per handle, never in a stop/teardown path; a resource guard is a mutex
grep -rna 'xSemaphoreTake(xSemaphore' src                     # all hits in one subsystem means the bus is not arbitrated
grep -n 'SD\.\(open\|exists\|begin\)' src/t-deck/*.cpp        # vs xSemaphoreTake in the same files
grep -rnaE 'xTaskCreate[A-Za-z]*\(' src | grep -oE ',\s*[0-9]+\s*,\s*&' # compare with configMAX_PRIORITIES of the framework
grep -rna 'std::atomic' src                                   # each must name its second context in a comment
```

**Rule 1.0.** RACE-01/02 (mutex, not binary), SPI-01..05 (violated), ISR-02 says "no delay, no
logging" for ISRs only. GAP: the same for critical sections; task priority range check.

### C12 -- Time arithmetic: rollover, wall-clock ageing, ids and versions from time `[x12]`

**Mechanism.** `timer + interval < millis()` latches true after 49.7 days (about 40 instances remain
upstream; the last live instance in this tree, the T-Deck map boundary timer, was fixed on
2026-09-02). An unsigned delta
of two stamps written by different tasks wraps to about 2^32 and aborts a healthy OTA upload. Ageing
entries against `getUnixClock()` on a node without NTP or GPS compares against garbage: every mheard
entry looked expired, `getMheardCount()` was 0, and peers showed NCNT 0. The comparison was
copy-pasted at nine sites and needed three rounds to find them all. `msg_id = millis()` collides for
two frames in one millisecond and hands out 0 at rollover. A release date was used as the settings
layout version and every release wiped every node's configuration. `min(x*2, MAX)` has no lower
clamp and latches at 0. `continue` without an index advance loops forever.

**Why it slipped.** The safe idiom was applied to about 70 sites in June and declared done; the
reversed spelling escaped the grep. Wall-clock ageing worked on every bench node because they all
had NTP. The convention `msg_id = millis()` was copied without asking about uniqueness.

**Seen in.** N-08, TM-46, NC-01, NC-02, BP-07 E5, BP-10 M8, FLASH_VERSION wipe, hey-storm trickle
floor, BP-03 plan review.

**Detector.**

```sh
grep -rnaE '[A-Za-z_\)]\s*[<>]=?\s*millis\(\)|millis\(\)\s*[<>]=?\s*[A-Za-z_]' src --include='*.cpp' | grep -v '(uint32_t)\|(int32_t)'   # 0 hits since 2026-09-02
grep -rnaE '(mheard|Path)Epoch\[[^]]*\]\s*\+\s*[0-9* ]+\s*>\s*getUnixClock\(\)' src   # must be 0
grep -rna 'getUnixClock()' src | grep -E '[<>]|-\s*[A-Za-z]'   # epoch in arithmetic is wall-clock ageing
grep -rnaE 'msg_id\s*=\s*(\(unsigned int\))?millis\(\)' src    # every hit goes through a monotonic allocator
grep -rna 'FLASH_VERSION' src | grep -v 'FLASH_STRUCT_VERSION\|printf\|idoc\|FWDATE\|//'   # the build id must not appear in a reset condition
grep -rnaE 'millis\(\) - ' src/safeboot | grep -v '(long)\|(int32_t)\|(int)'   # cross-task stamps: volatile + signed delta
grep -n 'min(trickle_interval_ms \* 2' src/esp32/esp32_main.cpp src/nrf52/nrf52_main.cpp   # needs max(..., MIN)
```

Every state-machine test gets a `uint32` wrap case (enqueue at `UINT32_MAX-100`, as in BP-03/BP-04).

**Rule 1.0.** STAB-05 covers the `millis()` idiom. GAP: monotonic clock for ageing, ids not from
time, version is a layout generation.

### C13 -- Stack budget on the nRF52 loop task and timer task `[x6]`

**Mechanism.** The Adafruit core hard-codes a 4 KB loop-task stack and a 1 KB timer-task stack.
`sendMessage()` (200+200+300 B) into `sendExtern()` (two 500 B buffers) under `checkSerialCommand()`
(600 B) reached watermark 0 and corrupted neighbouring RAM, with the reboot 2 to 4 s later. VLAs
sized from BLE bytes; a 300-byte stack buffer entered per refused message where before it was never
entered; a rule applied to one buffer in a commit and not to the new 140-byte local in the same
commit; `printfdeb` (about 900 B) from the radio callback.

**Why it slipped.** Only reachable with EXTUDP on real Ethernet; the crash is delayed and the reset
reason looked like a SoftDevice fault; nobody logged the high-water mark until N-22. The rule lived
in the plan for one function and the implementer applied it locally.

**Seen in.** N-22, N-03, SEC-03, BP-10 M1, capture hook (item 93).

**Detector.**

```sh
grep -rnaE '^\s+(char|uint8_t|unsigned char)\s+\w+\s*\[\s*([0-9]{3,}|MAX_MSG_LEN_PHONE|UDP_TX_BUF_SIZE)' src/loop_functions.cpp src/extudp_functions.cpp src/nrf52/*.cpp src/lora_functions.cpp src/bp_notice_frame.h
# every hit on a loop-task or callback path: static under NRF52_SERIES, or a proof
grep -rnaE 'char [a-z_]+ ?\[[a-z_]+ ?\+ ?1\]' src                # VLAs
grep -rna 'printfdeb' src/lora_functions.cpp                 # inside OnRxDone on the nRF52 path
```

Add `-Wvla -Werror` to `build_src_flags`; `-fstack-usage` per env and sum along the
`checkSerialCommand -> sendMessage -> sendExtern` chain; keep the `[EXT];rx/tx;stack_hwm` instrument.

**Rule 1.0.** STK-01..04, MEM-01 in intent. GAP: `-Wvla`, the stated 4 KB / 1 KB budgets, "large
locals on loop-task paths are static on nRF52".

### C14 -- Logging path side effects `[x10]`

**Mechanism.** `printfdeb()` is on every hot path and reaches serial and the open TCP 2323 console.
A correct security fix (`Serial.printf("%s", temp)`) introduced a `malloc` per line that starved
NimBLE mbufs and no BLE connection completed (found only by hardware bisect). On nRF52 `write()`
spins forever on a full CDC FIFO. The `%%` rewrite doubled percent signs. `--info` printed the WLAN
PSK to the unauthenticated console. A per-loop line ran at 125 lines per second once WiFi gave up.
A raw message text with LF in a `[BP];nack` marker can forge a `[MC-DBG] RING_STATUS` line that the
log tools parse. `DEBUG_MSG` compiles away with `DO_DEBUG 0`, and `printfdeb` strips `;` outside
`--debug csv`, so "nothing arrived" and "nothing is logged" were indistinguishable for a day (TM-31).

**Why it slipped.** The logger is treated as free; its heap, blocking and stack cost is invisible in
review. SEC-02 was verified for the security property only. The console's "no auth without
password" was documented and nobody connected it to `--info`.

**Seen in.** N-18, printfdeb CDC block, N-30, N-31, TM-21, BP-10 H3, TM-31, FL-01 suppression
marker.

**Detector.**

```sh
grep -rna 'printf("%s"' src                                  # hot paths: write(buf, len) instead
grep -rnaE 'printf.*(node_pwd|node_passwd|node_webpwd|bt_code)' src | grep -v maskSecret
grep -rnaE 'printf.*\[BP\];|printf.*\[MC-DBG\]' src            # every %s argument must be a sanitised buffer, never msg_text
grep -rnaE 'printfdeb\("\[[A-Z]*\];' src                      # machine-parsed markers must be Serial.printf
grep -n 'define DO_DEBUG' src/debugconf.h                    # 0: every DEBUG_MSG is a no-op
```

Rate check on any 100 s capture: `sort | uniq -c | sort -rn | head` on the message prefix; any line
above 10 per second is a finding. Any marker change: grep `tools/ test/ docs/ .claude/` for the
marker name first and append new fields at the end of the line.

**Rule 1.0.** Section 13 (literal formats, bounded buffer). GAP: allocation-free, non-blocking,
rate-bounded, secrets masked, RF-derived text sanitised before a parser-read marker.

### C15 -- Ring-buffer bookkeeping `[x8]`

**Mechanism.** Two overflow paths did not agree: priority eviction frees any slot, then the
unconditional "advance iRead" skipped an occupied one (N-24). Depth computed as index distance
counted freed holes (`queued=19` at 3 to 4 occupied; an 8-minute phantom refusal). A priority-5
relay at the read pointer sat for 10 minutes under strict-priority selection. The relay status byte
`0xFF` silently classifies all UDP traffic as `MSG_PRIO_NORMAL`. The in-band sentinel `"none"` for
"no payload" dropped a user message whose text was `none`.

**Why it slipped.** No native test over the ring until 2026-08-21, which found N-24 the same day.
Depth was only ever read from a debug line. Starvation only shows under sustained relay load.

**Seen in.** N-24, TXRING-CLEARSLOT-GAP, BP-02, BP-03, TM-31 status byte (pinned), PT-01 finding 4.

**Detector.**

```sh
grep -rnaE '\(w\s*>=\s*r\)|iWrite\s*-\s*iRead' src           # index-distance arithmetic must be gone
grep -rna 'queued=%d' src | grep -v txRingDepth              # every queued= printf derives from txRingDepth()
grep -n 'MSG_PRIO_BACKGROUND\|ringPriority\[' src/txring_functions.cpp   # every priority-ordered selector has an age bound
grep -rna '"none"' src | grep -v printf                      # data compared against a literal word
```

`test/test_txring` invariants: no occupied slot outside `[iRead, iWrite)`, depth equals the count of
`len > 0`, torn-write stress. `RING_ZOMBIE` in `tools/serial_monitor.py` (`dist == 0` with
`queued > 0`).

**Rule 1.0.** Section 12 and 14 partially. GAP: ring invariants unit-tested natively; depth means
occupied slots; no in-band sentinels.

### C16 -- Copy-paste literal, wrong handler, cloned constant `[x14]`

**Mechanism.** `getparam()` searched the request for `"/setparam/?"` and the whole read half of the
web API was dead. The `sendpos` web handler is a verbatim copy of `nomsgall` and toggles the wrong
flag. The zoom-out handler called `sdmap_zoom_in()`. `printlndeb(IPAddress)` resolved to the `int`
overload. A variant `configuration.h` created as a copy of the Heltec V3 file kept the old
`ADC_MULTIPLIER` when the V3 was corrected, and the node reported `BATT 0.00 V`. Two `--setowndns`
handlers, the dead one with the wrong offset. A GUI button saved settings without writing the field.

**Why it slipped.** No consumer (the GUI never calls `/getparam/`), no per-command test. Constants
in `variants/*/configuration.h` are never cross-checked against hardware. `commandCheck()` is a
prefix compare, so the second handler is unreachable and compiles.

**Seen in.** CS-04, WEB-04, upstream `ea1a430e`, IP log overload, PR #1119 (Wireless Stick V3),
NET-04, HL-03, N-28, upstream #690, TD-08.

**Detector.**

```sh
awk '/^void getparam/,/^}/' src/web_functions/web_functions.cpp | grep -v '//' | grep -n setparam   # must be empty
grep -n -B2 -A6 '"sendpos"' src/web_functions/web_setup.cpp   # the body must mention its own parameter
grep -oE 'commandCheck\(msg_text\+2, \(char\*\)"[^"]+"' src/command_functions.cpp | sort | uniq -d   # duplicate handlers
grep -n 'ADC_MULTIPLIER' variants/*/configuration.h | sort -t: -k3   # identical values across different boards are a question
grep -l 'definitions for HELTEC_V3' variants/*/configuration.h | grep -v heltec_wifi_lora_32_V3   # stale clones
grep -n 'save_settings()' src/t-deck/event_functions.cpp     # each preceded by a meshcom_settings. assignment or commandAction(
```

Bench: `curl http://<node>/getparam/?<name>` for every parameter; every `--x on/off` followed by a
read-back.

**Rule 1.0.** GAP.

### C17 -- Hardware detection on insufficient evidence `[x9]`

**Mechanism.** An I2C ACK treated as chip identity (three chips share 0x76/0x77); a baud-rate
argmax with no minimum, so one noise byte "detects" a rate; a compile-time board flag for divider
polarity; a single `begin()` with no retry; a floating ADC divider converted into a jumping battery
percentage; `/B=000` conflated with "no battery"; a bare `sat/hdop` gate that a corrupt RMC sentence
leaves intact while `gpsData.valid` is written and never read.

**Why it slipped.** Bench boards had the hardware the code assumed. The negative case (module
absent, wrong chip, floating divider) was never on the bench until the 2026-08-22 sensor bench.

**Seen in.** N-27, N-25 B-15/S5, ADC_CTRL probe, BAT-01, BAT-02, GPS-02, upstream #64, #227,
#875.

**Detector.**

```sh
grep -rnaE '^\s*\w+\.begin\([^)]*\);\s*$' src/*.cpp           # unchecked begin() returns
grep -rna -A3 'endTransmission() == 0' src | grep 'found = true'   # ACK treated as identity
grep -c 'gpsData.valid' src/gps_functions.cpp                # writers vs readers
grep -n 'node_alt = (int)gpsData.altitude\|fBaseAltidude == 0' src/*.cpp   # raw sample overwrites setting; QNH latched on first fix
```

Bench matrix: every sensor, GPS and battery path once with the hardware absent and once with the
wrong chip.

**Rule 1.0.** Section 8 "check begin()" covered and violated. GAP: detection requires positive
evidence (chip id, checksum-valid frame, measured signature).

### C18 -- Input accepted without semantic validation, and unrated shot paths `[x16]`

**Mechanism.** `handleACK()` accepted any payload whose first byte was 0x41, which is ASCII 'A':
506 of 8741 field frames were text fragments relayed as ACKs. `{SET}` wrote a hop count of 44. The
default callsign `XX0XXX` relayed over four hops. A HEY chain was unbounded on input. Any byte was
accepted as a hemisphere; an 11-digit latitude; a NUL inside a JSON `msg` silently truncated the
C-string pipeline. A LoRa text landed in `innerHTML` (stored XSS with access to `/config.json`).
`--sendpos`, the button and an unauthenticated EXTUDP `tele` datagram had no rate floor: 25,146
beacons in 21 minutes. The callsign regex allows `[0-9]+`.

**Why it slipped.** The decoder validates FCS and regex and stops; semantic rules (bit 7 of byte 5
is always set on a radio ACK; hemisphere is N or S) were only implicit in the encoder. A log
analysis matched a "1000-entry replay buffer" signature without checking that the "copies" carried
different battery values.

**Seen in.** BUG-10, `{SET}` max_hop, RX-01/TX-01, HEY_PATH_PAYLOAD_MAX, PT-01 findings 1/6, CHR-01/02,
WEB-03, TM-39 source-IP guard, FL-01, FL-02, N-06, SEC-03, BUG-07, UP-06.

**Detector.**

```sh
grep -rna 'sscanf(' src | grep -v 'if *('                    # every parsed field needs a range clamp
grep -rnaE '\|\| *[a-z]* *> *[0-9]' src/aprs_functions.cpp src/mheard_functions.cpp   # a length guard OR'ed into a match condition
grep -na 'inputJson\["[a-z]*"\]' src/*.cpp                    # every string from ArduinoJson compared against .size() before use
grep -n 'innerHTML' src/web_functions/*.cpp                  # trace each source to a printf of a decoded APRS field
grep -n 'encodeURI(' src/web_functions/*.cpp                 # must be encodeURIComponent
grep -rna 'sendPosition(0x9999\|sendHey()' src | grep -v loop_functions.cpp   # every shot path has beaconShotAllowed()
grep -n '\[0-9\]+' src/regex_functions.cpp src/aprs_functions.cpp
```

Spec vectors (`test/test_aprs_spec`) and corpus replays (`test_ack_replay`, `test_dedup_replay`),
extended for every new rule. Parser tests cover: longer than the field, truncated, control
characters, embedded NUL, the string delimiter.

**Rule 1.0.** Sections 3, 15, 16 as rules; the code did not follow them. GAP: rate floors on
originated beacons; HTML output escaping.

### C19 -- Settings persistence, two sources of truth, GUI-only state `[x14]`

**Mechanism.** The reset condition compared the build date. `flash_reset()` left `init_flash_done`
set and wrote the old RAM copy back while logging success. Out-of-range values from a corrupt struct
went straight into `radio.setOutputPower()` (settings sanitiser added in TM-32). The struct
initialiser said `node_wifion = true`, the NVS load default said `false`: every virgin T-Deck booted
with WiFi off and no serial command could turn it on. Fields exist in the struct but persist nowhere
and are overwritten with compile defaults after `init_flash()` on both platforms (`max_hop`). The web
setup page renders live values into the boxes that write stored values, so "same values in both
modes" is not evidence. `hasIPaddress` is derived from a settings string, not from the interface. A
GPS sample overwrites `--setalt` on every fix. Config export included live counters and sensor
readings. A `FLASH_STRUCT_VERSION` bump is by design a fleet wipe, and a design draft prescribed one.

**Why it slipped.** Two defaults in two files. The rule "on version mismatch factory reset" was
followed literally with the wrong notion of version. Handlers write `meshcom_settings.*` directly
instead of routing through `commandAction()`, so GUI and serial drift.

**Seen in.** FLASH_VERSION wipe, N-12, TM-32, HL-01/HL-02, CS-01/CS-02, NET-01, NET-03, GPS-03,
GPS-04, CS-03, HL-03, HL-04, advisor-dm B1.

**Detector.**

```sh
# struct default vs NVS default
grep -n 'getBool("node_wifion"\|node_wifion = ' src/esp32/esp32_flash.*
# struct members above the "nicht im Flash" boundary without an NVS key
sed -n '/struct s_meshcom_settings/,/nicht im Flash/p' src/esp32/esp32_flash.h | grep -o '[a-z_0-9]* *=' | sed 's/ *=//' | while read f; do grep -q "$f" src/esp32/esp32_flash.cpp || echo "no NVS key: $f"; done
grep -rnaE 'meshcom_settings\.[a-z_]+ = [A-Z_]+_DEFAULT' src/esp32/esp32_main.cpp src/nrf52/nrf52_main.cpp   # defaults applied after init_flash()
grep -n '_create_setup_textinput_element("own' src/web_functions/web_functions.cpp   # third argument must be the node_own* field the button writes
grep -naE 'meshcom_settings\.[a-z_]+ *= ' src/t-deck/event_functions.cpp   # direct assignment in a button handler without commandAction(
grep -rna 'FLASH_STRUCT_VERSION' src | head              # any bump: review against clear_flash / flash_reset / flashLayoutCompatible
```

`static_assert(sizeof(s_meshcom_settings) == <N>)` per platform next to the struct;
`test_settings_sanitize`; two consecutive `GET /config.json` must be byte-identical with GPS off.

**Rule 1.0.** Section 17 prescribes the wipe. GAP: version is a layout generation; runtime state
mirrors; migration instead of wipe; GUI and serial share one command path.

### C20 -- Dead, unreachable and write-only code `[x13]`

**Mechanism.** `imin` initialised to -1 and never assigned, so "evict the oldest" was dead and the
table fell through to the sequential ring (MH-02). `gpsData.valid` written, never read. Timestamp
writer emits 14 characters, parser requires 19, so clock recovery from message history never ran
(G14). `sendTelemetry()`'s `else` branch is unreachable because every caller passes a constant.
`lat_c == 'W'` when the producer only emits N/S. Eleven `while(true);` inside a block comment counted
as live by an audit. A commented-out timezone conversion replaced by an unconditional `= 0.0`. The
1-hour NTP refresh constant is dead because the caller forces `requestNow()` every 15 minutes.
`persisted_msgs` is a 1000-entry write-only `String` vector on the internal heap. A fork fail-soft
kept for a field upstream no longer emits.

**Why it slipped.** No compiler warning for a variable that is read but never written in the loop.
`-Wunused-function` does not see `#if`-excluded code. Nobody tests the full-table case. A helper
that is never called looks like a feature.

**Seen in.** MH-02, GPS-02, G14, TLM-03, G03, N-09, PT-01 finding 8, NTP-01, `T_DECK_SPIFFS`, PP/DIST
removal (item 165), HL-03, N-25 A-1..A-4, `scanFlag`.

**Detector.**

```sh
grep -rnaE 'int imin\s*=\s*-1' src                            # then check the loop assigns it
grep -rna 'sendTelemetry(' src | grep -v 'void sendTelemetry' # every call passes the same constant?
grep -n 'length() != 19\|%02i.%02i.%02i' src/t-deck/lv_obj_functions.cpp   # writer/reader round trip
grep -n "== 'W'\|== 'S'" src/t-deck/*.cpp                    # compare against the producer's alphabet
grep -n 'remove("' src/*.cpp                                 # removals of keys that are never set
for f in decodeAPRSPOS decodeMHeard getExtern commandCheck decodeTinyXML checkVia; do printf "%-16s " $f; grep -rl "$f" test/ | wc -l; done
```

Flash-size byte-identity before and after a dead-code removal, recorded in the commit.
`-Wunreachable-code`, `-Wunused-but-set-variable` on `src/`; clang-tidy `deadcode.DeadStores`.

**Rule 1.0.** GAP; Section 19 would catch G14 with a round-trip test.

### C21 -- Lifetime: delete without NULL, dangling capture, view/model desync `[x6]`

**Mechanism.** `add_map_point()` deleted a station's dot and returned early when the station
projected off-screen, leaving a dangling pointer; the next zoom deleted it again (`LoadProhibited`,
reproducible at zoom step 7). An `onDisconnect` lambda captured `[&]` of a stack frame that was gone
when the late disconnect fired. The LVGL message list appended bubbles without deleting old ones
while the model was capped at 50; about 2.7 KB PSRAM per message, `abort()` near 390. The leak heals
on any tab switch, so inspecting it destroys the evidence. `i2s_driver_uninstall()` from a button
handler while the player sits in `i2s_write(portMAX_DELAY)`.

**Why it slipped.** The double delete needs more than 30 stations and a second zoom. Two limits for
one concept with no assertion tying them. The previous handover flagged G01 but nobody had a
reproducer.

**Seen in.** G01, TM-46, TD-03/H1, C3 (audio), UP-04.

**Detector.**

```sh
grep -n -A3 'lv_obj_del(' src/t-deck/*.cpp | grep -vE 'NULL|nullptr|lv_obj_clean'   # every delete NULLs the slot in the next statement
grep -rnaE 'onDisconnect\(\[&\]|\.on[A-Z][a-zA-Z]*\(\[&\]' src   # must be empty
grep -n 'lv_obj_clean\|lv_obj_del\|_append_bubble\|MSG_TAB_MAX_MESSAGES' src/t-deck/lv_obj_functions.cpp   # every append site has a trim on the same path
grep -rna 'i2s_driver_uninstall\|vTaskSuspend\|vTaskResume' src
```

Harness: `map --map-stations 40`, `trim --trim-count 60`, `tools/bench/experiments/crashtest.py`,
`ota_regression.py` abort-retry.

**Rule 1.0.** Section 12 covered. GAP: model trim needs a view counterpart.

### C22 -- Order of operations and state-machine reachability `[x15]`

**Mechanism.** The dedup gate was evaluated after its own `addLoraRxBuffer()`, so an ESP32 gateway
relayed 0 of 30 UDP position frames. The app echo and gateway uplink fired before the TX ring was
asked. `addTxRingEntry()` returns -1 for three reasons and the caller read every -1 as
back-pressure, forcing QRT on an empty ring for factory-fresh nodes. A latched once-per-episode
enum also modelled a per-message receipt, so `onRefuse()` was provably dead for two days while an
18-case native suite stayed green (the one notice it counted came from `onSend()`). A gate that
serves two goals in two waves cancelled one of them (BP-09 hid BP-07's receipt: worse than before).
A threshold on ring depth counted relay, ACK and beacon traffic the sender did not create and fired
on the first own message; a "hysteresis" of 1 flapped QRS/QRV within 100 ms on a real radio; an
episode closing only at depth 0 never closed on a busy gateway. A silent close left a latched
routing origin stale. A re-entrant UI sink (`lv_task_handler()` inside `addMessage()`) let a nested
`sendMessage()` clobber a global written after the call.

**Why it slipped.** Each statement is correct in isolation; only a replay of the real sequence
exposes the order. Tests covered transitions, not reachability. The threshold was tuned on an empty
bench ring; nobody consulted a field `RING_STATUS` distribution first. Wave gates were "build plus
tests", not the previous wave's user-visible acceptance criterion.

**Seen in.** TM-31, BP-08, BP-07 L1, BP-10 H1/H2/M4/M5/M7, BP-01 QUIET_DEPTH, BP-04, BP-05,
QRS_MIN_USER_MSGS, BP-06, BP-09, NTP-01 follow-up, ALT-35, N-12(a), GW-01, TM-37, TM-49.

**Detector.**

```sh
for f in src/udp_functions.cpp src/nrf52/nrf_eth.cpp; do echo $f; grep -n 'is_new_packet\|addLoraRxBuffer' $f; done   # the first insert must follow the gate
grep -n -B15 'addNodeData\|addBLEOutBuffer' src/loop_functions.cpp   # the ring result w is known at that point?
grep -rna 'addTxRingEntry(' src | grep -v 'txring_functions\|= addTxRingEntry\|if *(addTxRingEntry\|return addTxRingEntry'   # discarded result
grep -n 'latchIfHigher' src/backpressure.h                    # latched emitters must not carry per-message payload
grep -n 'bp_episode_origin\|bp_episode_dst' src/loop_functions.cpp   # every close path, including silent ones, resets both
grep -n -A6 'bp_rc == BP_SEND_OK' src/t-deck/event_functions.cpp src/t-deck-pro/ui_deckpro.cpp   # only the field clear may sit inside the gate
grep -n 'TEST_ASSERT_EQUAL_INT(1,' test/test_backpressure/*.cpp   # can more than one producer yield that 1?
```

Before merging any threshold on ring depth:
`grep -h "RING_STATUS queued=" <gateway log> | sort | uniq -c`. State machines take injected time
and depth (`now_ms` parameter, no `millis()` inside). Functions that can refuse return a result
(`[[nodiscard]]`). Echo after the write.

**Rule 1.0.** Section 16 partially. GAP: check-before-commit, echo-after-commit, reachability tests,
overloaded return codes.

### C23 -- Library contract misread `[x14]`

**Mechanism.** `readData(buf, len)` takes `len` by value. `WiFiUdp::parsePacket()` returns 0 while
an unread `rx_buffer` exists. `NTPClient::forceUpdate()` flushes every queued datagram off the
shared socket and blocks 1 s. `esp_wifi_get_mode()` waits for the WiFi task. U8g2's second hardware
I2C is a silent no-op without `U8X8_HAVE_2ND_HW_I2C`. `lv_disp_draw_buf_init()` wants pixels.
`Adafruit_USBD_CDC::write()` spins forever while DTR is set. `sd_softdevice_disable()` from the loop
task hangs. tinyxml2 `QueryFloatText()` does not write on failure (stack garbage relayed as
telemetry). ArduinoJson parses `"0.002000"` to `0.0019999998`. A quoted `#include
"printfdeb_functions.h"` bypasses the native test stub regardless of `-I` order. The SX126x
library's `TASK_PRIO_NORMAL` macro shadows the core's enum. GPIO level polling at 10 ms drops 75 % of
trackball edges. Own AP selection with a pinned BSSID disabled the driver's roaming and retry
features, and `setMinSecurity(WPA2)` is a minimum, so SAE was attempted against a mesh that steers.

**Why it slipped.** The call compiles and returns a plausible value (255 bytes, `LE Connection
Complete`). The precondition lives in a header comment or platform source that nobody reads until a
bisect forces it. A driver feature was re-implemented instead of switched on; every WiFi value was an
arduino-esp32 default that arrived by accident.

**Seen in.** N-29, UDP-02, TM-35, `WiFi.getMode()` hook, TM-09, G07, CDC block, N-19, PT-01
finding 7, CS-03 float, BAT-01 include, C-01, TM-18, TM-24, TM-34 Wave W.

**Detector.**

```sh
grep -rna 'readData(\|parsePacket()\|forceUpdate(\|availableForWrite' src   # each site paired with its precondition call
grep -rna '#include <NTPClient.h>\|NTPClient timeClient' src  # must be empty
grep -n "U8X8_HAVE_2ND_HW_I2C" variants/*/platformio.ini      # every env using _2ND_HW_I2C
grep -na 'Query[A-Za-z]*Text(&' src/*.cpp                     # every call assigns its XMLError
grep -rna '#include "printfdeb_functions.h"' src              # TUs in a native build_src_filter link the real printfdeb
grep -n 'WiFi.begin(' src/udp_functions.cpp                   # a 5-argument form with a BSSID is the pin
grep -rna 'setScanMethod\|setAutoReconnect\|persistent\|setSleep' src   # every driver knob: decided, or documented as default
```

For every third-party call on a hot path, cite the library line that defines its blocking or
ownership semantics in a comment (the fixes do this), and run it under `INSTR_SECTION`.

**Rule 1.0.** GAP.

### C24 -- Polled drain slower than the producer `[x2]`

**Mechanism.** `WZ_GPS_Loop()` is the only consumer of the GPS UART and runs on a 3-second display
timer. The L76K emits about 140 B/s; the 256-byte Arduino ring loses about 165 bytes per cycle;
spliced sentences pass the 8-bit XOR checksum one time in 256, and a corrupt fix with
`lon 0.000000`, `Date 2015.14.00` was committed. The nRF52 copy polls at 1 s and does not overflow.
TRACK mode sets 1 s, so only stationary ESP32 nodes are affected.

**Why it slipped.** The interval was tuned 10 to 5 to 3 s as a display constant while it also
governs UART draining. The first analysis blamed loop stalls using the line rate instead of the data
rate and retracted.

**Seen in.** GPS-01, GPS-02, TM-52.

**Detector.**

```sh
grep -rna 'GPS_REFRESH_INTERVAL\|setRxBufferSize' src
grep -n 'gps_refresh_timer' src/esp32/esp32_main.cpp src/nrf52/nrf52_main.cpp   # compare the two cadences
```

For every `while (X.available())`: find the call cadence and assert
`rate_Bps * period_s <= rx_ring_bytes`. Falsify with TRACK mode. Native: NMEA stream with a 165-byte
gap every 420 bytes, assert a `lon == 0.0` commit is rejected.

**Rule 1.0.** GAP. Nearest: Section 8 "queue full".

### C25 -- Feature gated on the wrong flag, asymmetric gates, blind windows `[x9]`

**Mechanism.** The NTP request is sent un-gated but the reply is harvested only inside
`if (bGATEWAY)`: a gateway-off node got 545 timeouts and zero replies in 9.1 h. DHCP renewal and the
Ethernet link poll sat in the same block. `--ntpsync` was a silent no-op on a GPS node because the
ESP32 block runs only under `!posinfo_fix` while nRF52 pumps ungated. After ten polls
`doWiFiConnect()` gave up; the driver associated at 55 s and nobody harvested the event. The static-IP
path used a literal `8.8.8.8` DNS (unreachable on HAMNET) without a log line while the DHCP branch
used the router's DNS. `kissStop()` sits inside the enable guard, so `--kiss off` on a KISS-only
node leaves the port open until reboot.

**Why it slipped.** The gateway block grew by accretion: every network-adjacent call was dropped
inside it "because that is where the socket is". Every NTP claim was proven with `--gateway on`. A
"give up after N seconds" creates a blind window in which success is never noticed.

**Seen in.** TM-45, ETH-01, NTP-01 follow-up, TM-24, NET-02, NET-06, PR #1114 F3, CTY-01, N-23.

**Detector.**

```sh
awk '/if\(bGATEWAY/,/^    }/' src/esp32/esp32_main.cpp src/nrf52/nrf52_main.cpp   # list every callee; anything a node with an IP but no gateway needs is misgated
grep -n 'tryConsume\|getMeshComUDP()\|neth.getUDP()' src/esp32/esp32_main.cpp src/nrf52/nrf52_main.cpp
grep -n 'posinfo_fix' src/esp32/esp32_main.cpp src/nrf52/nrf52_main.cpp   # compare the two gates around every timeClient call
grep -rnaE '"[0-9]{1,3}(\.[0-9]{1,3}){3}"' src               # every IP literal needs a log at the substitution site
grep -n 'kissStop()' src/esp32/esp32_main.cpp                # check the enclosing if
```

Soak marker: `[NTP];ok` count above 0 on a `--gateway off --gps off` node. For every driver event
name the consumer and its gate.

**Rule 1.0.** GAP.

### C26 -- Static DRAM cliff and build plumbing `[x8]`

**Mechanism.** Classic ESP32 had 528 bytes of `dram0_0_seg` headroom while PlatformIO reported
"RAM 23.1 %" (the denominator includes IRAM and RTC). A 6 KB static buffer overflowed the segment;
the root cause was `MAX_RING 30` in the classic-ESP32 branch against 20 elsewhere. Bench
instrumentation added 2.8 KB and broke `E22_XML-DevKitC` at release time. `-Wall -Wextra` in
`build_flags` compiled the SoftDevice headers with 23,532 warnings on the maintainer's machine.
`upload_protocol = custom` never runs port autodetect, so `--port "$UPLOAD_PORT"` handed esptool an
empty string on a host without `upload_port`. The Tasmota safeboot platform and mainline share one
framework directory.

**Why it slipped.** Nothing measured `_bss_end` against the segment origin. The fork always flashed
with `--upload-port`. Build behaviour depends on host state.

**Seen in.** MEM-01, E22_XML overflow, CFG-01, PR #1115/#1118, UP-07/UP-08, framework ping-pong,
CI cache key.

**Detector.**

```sh
python3 tools/resource_watch.py dram --min-headroom 4096 .pio/build/<env>/firmware.map
pio run -e E22_XML-DevKitC 2>&1 | grep -a "overflowed\|RAM:"
grep -n 'Wextra' platformio.ini variants/*/platformio.ini    # must sit under build_src_flags
grep -l 'upload_protocol = custom' platformio.ini variants/*/platformio.ini | xargs grep -l 'UPLOAD_PORT'   # 4 variants still pair them
grep -n 'platform = ' platformio.ini variants/*/platformio.ini | grep -v '@'   # unpinned
```

**Rule 1.0.** COMP-01, COMP-05. GAP: DRAM headroom gate, safeboot isolation, warning scope.

### C27 -- Hand-built structured output without escaping `[x5]`

**Mechanism.** Three web endpoints built `{"%s":"%s"}` with printf from query strings; EXTUDP
escaped `"` and `\` by hand before ArduinoJson escaped them again; thirteen BLE builders clamped raw
bytes mid-string; a LoRa text reached `innerHTML` unescaped; `encodeURI` where `encodeURIComponent`
was needed; the JS inside a C++ string literal was invisible to a "C++ call sites" scout.

**Why it slipped.** The reference fix (`ble_json_frame.h`) existed and had been applied to one path;
the pattern was copy-pasted 17 times. The web UI predates any threat model for mesh input.

**Seen in.** JSN-01, WEB-03, BP-10 H3.

**Detector.**

```sh
grep -rna 'printf(.*"{\\"' src/web_functions/                # must be empty
grep -rna 'strEsc\|replace("\\\\"' src                       # hand escaping before ArduinoJson
grep -n 'xhttp.send()' src/web_functions/web_functions.cpp   # a readyState handler on the same line?
```

One shared escaper; `serializeJson(doc, client)` for web output.

**Rule 1.0.** GAP: HTML output escaping; one escaper.

### C28 -- Tests that prove nothing `[x16]`

**Mechanism.** A previously green test pinned the buggy value `mh_date.length() == 55` as expected.
`EXPECTED_LEN_MISMATCH == 1` freezes a count, not the vector. A CONF test compared `x == x`. The
golden fence could be neutralised by an environment variable. A "fails-before" mutation was reverted
with `git checkout`, which also discarded the uncommitted fix. New "stays/never" property tests pass
under a no-op sweep (2 of 5 in one wave). `test_flood_13_into_10_yields_five_nacks` was called an
integration test although `loop_functions.cpp` is in no native `build_src_filter`, so `sendMessage()`
wiring is exercised by zero of 528 green cases. Fixtures used the default callsign that a new guard
rejects. A harness scenario checked `is not False` on a helper that returns the actual state. A
canary was smaller than the worst-case overrun. The documented one-command runner ERRORs suites in
envs that do not contain them.

**Why it slipped.** A green run is trusted; instruments are not reviewed like firmware; corpora were
harvested from nodes carrying N-29 (every record 255 bytes); the harness ran on the developer's own
nodes.

**Seen in.** PT-01 findings 2/3, `test_aprs_reencode`, testsuite verdict 2026-08-22, TM-50
mutation, BP-03/BP-04 property tests, BP-07 integration claim, TX-01 fixtures, `gps_experiment`,
`test_ble_json_frame` canary, `test_bp_regression` header, TOOL-01..05.

**Detector.**

```sh
awk '/^\[env:native/{e=$0} /loop_functions\.cpp/{print e}' platformio.ini   # empty: no native test can exercise sendMessage()
grep -rnaE 'TEST_ASSERT_EQUAL\((\w+),\s*\1\)' test           # tautologies
grep -rna 'TEST_IGNORE' test | wc -l                         # must trend to 0
grep -n 'EXPECTED_LEN_MISMATCH\|EXPECTED_FCS_MISMATCH' test/test_aprs_reencode/*.cpp   # replace the count with the vector name
grep -n "is not False\|is not True" tools/bench/tdeck_harness.py
grep -rna 'XX0XXX\|resetRing\|s_meshcom_settings meshcom_settings' test/   # before landing a guard keyed on a default
pio test -e <env> 2>&1 | grep -E 'IGNORED|skipped|ERRORED'   # the gate loop must print the count
```

Every new test is run once against the mutated or old code and the red set recorded in the test
comment; a test green under both is documented as an invariant, not as regression proof. Before
`git checkout -- <file>`: `git diff --stat <file>` must be empty or backed up.

**Rule 1.0.** Section 19 partially. GAP: fails-before per test; instruments carry their own suite.

---

## Part B -- Process and review patterns

### P01 -- Confident claim first, discriminating experiment later `[x30+]`

**What happened.** One evening produced six root-cause verdicts for one defect ("That settles it"
twice). The BLE/WiFi coexistence hypothesis for TD-01 was carried in the backlog for days and refuted
by a 12-boot A/B. "DNS is the suspect" was written into a verdict and withdrawn ten minutes later
after reading the wrapped call. The GPS overflow was blamed on LoRa stalls using line rate instead of
data rate. "Kein Reboot beim Open" was stated at night, written into memory, and refuted in the
morning because the reset banner runs before the reader attaches. "That's expected" explained away a
tool that was sorting run directories by name. The 9999 spam class was attributed to a client
fallback from firmware code alone; the user knew it was his spam class. A three-point RCA of an OTA
failure was delivered with no safeboot serial output in existence.

**Why.** Each conclusion was stated before the cheap falsifier ran: a second board, one grep for the
`#if`, one `ls` on the SD card, one A/B flash, one look at the uptime counter. The user's own
question ("does the firmware support roaming?", "what gateway?", "I see a scrollbar on the right")
was the discriminating observation more than once.

**Guard.**

- No "root cause" in a summary or doc row without a table of runs with n and outcome, and at least
  one n >= 10 reproducer with 0/10 vs 10/10.
- Any sentence explaining a component whose code was not opened in the session is marked as an
  assumption.
- Cheapest falsification first: second board of another class, TRACK mode, WPA2-only SSID, `--gps
off` boot.
- Never update a memory note on the strength of a single negative grep.
- `grep -n "settles it\|root cause\|pins it" docs/*.md` and check each cites a run count.

### P02 -- "Verified" that was not `[x20+]`

**What happened.** "CI gates the native suites" (both fork workflows `disabled_manually`; `gh`
resolved to upstream). A build succeeded although the projection anchor had zero hits and the flashed
binary did not contain the change. The named verification env excluded the touched files
(`t_deck_pro` drops `src/t-deck/*`). The pre-commit gate ran one native env; the 12-env release gate
found the broken test in another. Items were committed as done with no hardware exercise of the
changed path (TD-07/08, PM-01, CHR-01/02, `ntpsync.py`). The BAT-02 heuristic shipped on a
self-report that said "derived from static code reading only", and the bench showed a stable 4.22 V
on the reporting board. Tree-hash identity was used instead of a build; a commit-stat reading was
used instead of an md5 comparison.

**Why.** Build green was taken as "change applied" and "change correct". The gate definition was
"native suite plus builds"; bench proofs were done only for items with an obvious serial marker.

**Guard.**

- After any scripted edit: `git diff --stat` lists the intended file with a non-zero delta before
  `pio run`; `strings firmware.bin | grep -c <marker>` before flashing an experiment arm.
- Before naming a verification env: `grep -n build_src_filter -A3 variants/<env>/platformio.ini`.
- Commit gate is the full native env list plus one env per distinct build filter (C07), not
  `pio test -e native` alone.
- Per-item acceptance row states a bench marker or says "unverified on hardware";
  `grep -n "unverified\|not run against" docs/BACKLOG.md` at release time.
- `md5 -q` for staleness, never commit-stat reading.
- "Believed fixed" is the status until a fails-before test or a bench line exists.

### P03 -- Void or blind instrument `[x15]`

**What happened.** `--screencrc` returned the same CRC for every screen (MISO not driven); verdicts
built on it were void. "No datagram reaches the Mac" rested on `DEBUG_MSG` lines that compile away;
a day was lost before a raw `Serial.printf` instrument showed both directions worked. `bootloop.py`
produced 100-byte "boots" for 52 minutes because DTR/RTS asserted does not reset the T-Deck. A
13-character message body never allocates (Arduino `String` SSO), so a heap test proved nothing. The
netconsole was enabled between two heap samples and its 8192-byte allocation looked like a leak. A
section instrument wrapped three calls and filed a High item against the wrong one. The crash regex
matched "W**assert**urm" in a beacon. mcmap absence cannot distinguish "filtered" from "never sent".
`[BOOT];ready` was printed through `printfdeb`, which strips `;` outside `--debug csv`.

**Why.** The instrument was validated only with `--parse-only` on an old log, or not at all; progress
was polled by counting files, not checking content; a negative result was read as data.

**Guard.**

- Positive control before any negative verdict: change the screen and assert the CRC differs;
  send a known datagram and see it; run the reset regex on the first 2 KB of the first boot log.
- Runner aborts if the first log is under 2 KB or lacks `rst:0x|ESP-ROM:|CLIENT SETUP`.
- Every `INSTR_SECTION` wraps exactly one callee; `section_ms` far below the reported gap flags
  unattributed time.
- Freeze `--info` (netconsole, debug flags) before the pre sample; assert message bodies exceed 13
  characters in heap tests.
- Any bench marker is a raw `Serial.printf` with `[TAG];k;v` fields.
- Zero counts in both arms of an experiment mean instrument failure, not data.

### P04 -- Subagent or scout report accepted without a spot check `[x25+]`

**What happened.** A test-audit finder claimed CI gating; a bounds finder assigned a `ps_realloc`
buffer to the internal heap; a timing finder called the `OnRxDone` chain an ISR; a scout wrote
"`mh_date` is fixed-width 8" (it is 10); a scout said "nRF52 `bUDPLOG` defaults true"; a research
track analysed a font that is not built because the orchestrator's own context file quoted the dead
`lv_conf.h`; the GW-01 scout wrote "CONFIRMED" for a server dedup that the server does not do; a
docs implementer reported changelog entries 163-166 that were 166-169 with a gap; a writer declared
an artefact missing that the orchestrator had listed minutes earlier; `SendMessage` to a finished
agent "queued" and never landed. About one wrong claim per report.

**Why.** Agents inherit the brief's facts as ground truth, cannot touch hardware, and extrapolate;
severity inflates under "find bugs" pressure; the brief itself was written from a dead config file
or from memory of a test setup.

**Guard.**

- Every numeric width, return code, byte order or "only called from" in a brief carries a
  `grep -n` citation produced in the session; the implementer quotes the citation it verified.
- Mark `[V]` only for claims the orchestrator re-derived; a report with zero contradictions and
  zero unverified items has not been adversarial.
- Spot-check the sharpest behavioral claim of every report before acting; for external systems,
  grep that system's log (`mcp__mcmap-prod__logs_grep` on the msg_id).
- `gh repo view --json nameWithOwner` before any `gh` call; always pass `-R DK5EN/MeshCom-Firmware`.
- After `SendMessage`: check task status; if completed, apply the change yourself.
- Run any subagent harness or parser once against a real device log before committing it.

### P05 -- Fix causes regression; wave cancels wave `[x16]`

**What happened.** SEC-02 to N-18 (heap churn kills BLE). C3 to N-25 (watchdog at setup start,
release withdrawn). FLASH_VERSION bump to fleet wipe. CONC-16 half fix. TM-20 delay removal to
weakest-AP selection. The `[WIFI];link` heartbeat to a 2.9 s loop gap. The TM-46 stall watchdog to
every upload aborted. BP-09 gating hid BP-07's receipt. BP-05 threshold to first-message QRS. NTP-01
gate to silent no-op on GPS nodes. The BP-10 advisor round over the whole diff found three
regressions and five mediums that the per-wave gates had passed.

**Why.** One bench board per fix; the bisect started from the session-start commit rather than the
upstream merge-base; each wave gate checked build plus tests, not the previous wave's user-visible
acceptance criterion; advisor review came after the campaign instead of per wave.

**Guard.**

- Bisect against `git merge-base v4.35p_prio upstream/dev`, never against session start.
- Per-wave bench gate on all four bench boards before the next wave.
- Each wave gate re-runs every previous wave's acceptance criterion, not only its own.
- `/fable-review` advisor over the whole multi-wave diff before the release, and per wave when
  waves touch the same state machine.
- Every "removed delay/wait" commit states what the wait was protecting.

### P06 -- Stale baselines: line numbers, counts, retracted phrases, triplicated numbers `[x25+]`

**What happened.** 5 of 18 cited line numbers were off after one session; a `:1005-1024` range
survived two passes; the orchestrator's own gate edit re-grepped `:1222` to `:1223` because a sibling
shifted the file. Release docs said 477 cases when the gate printed 480; a parallel session wrote
"530 across 11 envs" for a 485/12 gate; the count was pasted from the red run. The GPS-03 backlog row
still carried the median-filter recommendation the long doc had retracted. The same corpus numbers
live in three docs and were patched by a script with hard-coded old strings. The `FLASH_VERSION`
comment explained the previous date. A stale `FWDATE` comment survived the fix. Docs said "geplant"
after the code shipped, and the app-facing chapter named the superseded wire prefix in the one place
app developers would implement. Architecture docs still say "zero automated tests".

**Why.** Line numbers were copied from `sed -n` output taken before intermediate edits; numbers were
typed from memory of an earlier session; the retraction was applied to the long doc first and the
summary table second; three documents carry the same state.

**Guard.**

- Cite `file:function()` or a grep anchor instead of line numbers in briefs and docs.
- Before commit: `grep -ohE '\x60[a-z0-9_/.-]+\.(cpp|h):[0-9]+' docs/<report>.md | sort -u` and verify
  each with `sed -n`.
- `git diff | grep -n "<retracted phrase>"` for every retraction made in the session.
- Numbers describing a corpus or gate get their generating one-liner in the commit message; paste
  the count from the green run only (`grep -n "test cases" gate.log | tail -1`).
- After any wire-string decision: `grep -rn "<old spelling>" docs/*.md src/*.h`; after an
  implementation commit: `grep -n "geplant\|noch nicht implementiert" docs/<topic>.md`.
- `grep -n "20260724\|<old date>" src/configuration_global.h` on every version bump.

### P07 -- Shared working tree, parallel sessions, wrong branch `[x15]`

**What happened.** Three sessions built and edited in one checkout at the same minute; one ran
`pio -t clean` and wiped the other's `.pio/build` mid-build. Two sessions filed the same backlog
section twice; IDs TM-25..29 were taken between proposal and write. A commit absorbed another
session's uncommitted hunks because interactive staging is off. A 21-item campaign and four release
tags landed on `tdeck-partial-refresh-trace`, whose defining change is an `// EXPERIMENT` flag;
`v4.35p_prio` fell 154 commits behind and the release skill expected it. Bench runs opened ports
another session's overnight soak was using and killed 13 hours of it. A parallel session's boot
loops changed a neighbour board's OLED page and `[BOOT];ready` went from 8.5 to 21 s.

**Why.** Nothing signals that another session is active; `git status` is checked late; the branch
that was checked out was accepted as the working branch; the RF and WLAN bench is shared state.

**Guard.**

- Session start: `git branch --show-current` vs CLAUDE.md;
  `git rev-list --left-right --count v4.35p_prio...$(git branch --show-current)`.
- Before any build or gate: `pgrep -fl "pio run|pio test"`; `git status --short` (unexplained `M`
  entries mean stop); `stat -f '%Sm %N' $(git diff --name-only)` vs session start.
- Before any port open: `lsof /dev/cu.usb*`;
  `pgrep -fl "wifisoak|meshlogger|bootloop|gwflood|rak_harness|tdeck_harness"`.
- One `git worktree` per session; allocate ID ranges per session; `grep -c "^| <ID> "
docs/BACKLOG.md` must be 1 before filing.
- One-line experiments live behind a `-D` flag, not on a branch.

### P08 -- Upstream sync collisions and revert cycles `[x12]`

**What happened.** Upstream reverted three fork-touched PRs and restored `serializeJson(...,
measureJson()+1)`; the fork re-fixed it, then removed its now dead PP/DIST fail-soft. Upstream deleted
`FWDATE` and silently killed PR #1103. Two map hunks merged without conflict markers and had to be
reverted by hand. A rebase after an upstream squash-merge left 64 fix commits holding only
deletions (tree bit-identical, history destroyed). The first PR plan was written against a stale
fetch and predicted the wrong conflicts. PR #1118 was opened on a base the maintainer changed the same
minute. The maintainer's conflict resolution left the fork's rationale comment without the flag
behind it.

**Why.** Upstream squash-merges, reverts whole PRs and has no CI or tests. A bit-identical tree is
the usual success criterion and history damage is invisible to `git diff`. Memory rules were
generalised from one event.

**Guard.**

- `git fetch --all --prune` then `git merge-tree --write-tree HEAD upstream/dev` before any merge
  plan; never quote a resolution strategy without the conflict list.
- `git log --oneline --grep='^Revert' <old>..upstream/dev`; for each reverted symbol
  `grep -rn "<symbol>" src/ test/ release-notes.md docs/CHANGELOG-stability.md`.
- After a rebase over a squash: every commit in `upstream/dev..HEAD -- src` with zero added lines
  is `EMPTY-ADD`; merge instead of rebase.
- Keep the verdict's "silent hunks" list as a post-merge checklist and grep the rejected tokens.
- `gh pr list --repo icssw-org/MeshCom-Firmware --state open` and the maintainer's active branch
  right before `gh pr create`.
- No fork repairs for features upstream removed; release docs report final state.

### P09 -- Gate too narrow for what ships `[x10]`

**What happened.** Wave gates built 5 to 8 envs; the release build failed 5 of 32 (two safeboot,
T114, T-Echo, E22_XML). "381 cases green" hid three `TEST_IGNORE` pins. One native env at commit;
the twelve-env gate found the regression. `env:native` collected upstream suites. Python tools had no
lint gate at all (`ruff` absent, 101 pre-existing findings, about 1000 new lines checked by
`py_compile` only). A gate ran while wave writers were still active and took a green against a
sibling's in-flight edit.

**Why.** The gate target list was chosen by "what we flash", not "what the release ships"; skips
are invisible in a total; the rule "lint, typecheck, format-check" has no Python instrument here.

**Guard.**

- Gate = full native env list + one env per distinct build filter (C07) + `-Werror` on the seven
  flash targets + both safeboot envs, run after `TaskList` shows zero writers.
- Gate loop prints `grep -cE "IGNORED|skipped"` per env and fails on growth.
- `uvx ruff check --select E,F,B tools/ && uvx ruff format --check tools/` with a baseline that only
  goes down.
- `python3 tools/webflash.py --self-test`, `tools/bench/test_tdeck_parse.py`,
  `tools/bench/test_meshlogger.py` in the gate.

### P10 -- Brief, plan or intake premise wrong `[x20+]`

**What happened.** The plan placed a return type in wave 1 and the function that returns it in wave 3. The call-site list had 8 entries; the ninth was a test stub that would have broken the native
link. The brief said CONF coordinates are big-endian; the code is LSB-first. The intake said "kein
htonl im ganzen Baum" (two `htons`). The brief said "QRS fires at depth 6" (5). The operator's
"TM-39 on nRF is broken" was a DONE test item; "E22 has an under-voltage TX lock" does not exist;
"something overwrites the retry counter" has no such path; "probably already fixed" for four
upstream issues, none were. The HL-03 row described the gap backwards. The mcmap "replay buffer"
finding had two wrong premises.

**Why.** Intake lists came from a parallel session or from memory and were pasted as fact; briefs
restated them without a grep; a spec was derived from notes, not from a capture or the code.

**Guard.**

- Every brief claim "X is wrong in file F" is preceded by `grep -n "X" F` in the brief itself.
- Every intake premise gets one grep per claim in the transcript before the row is written; file
  the refutation in the row.
- For threshold changes: `grep -rln "QRS\|BP_NOTICE_QRS" test/` across every env directory and
  list every hit in the brief.
- Grep the plan for identifiers defined in a later wave; `grep -rn "<fn>(" src/ test/` including
  stubs.
- Any byte order in a brief cites `grep -n "payload\[pos" <file>` or a real capture.

### P11 -- Scripted edits without a hit-count assertion; prettier reflow; restore that wipes `[x20+]`

**What happened.** `str.replace()` missed its anchor at least seven times in one batch; twice
firmware was flashed and a run executed without the change. A `re.sub` inserted literal `\"` under
`-Werror`. A 320-line splice left a nested `#if`. A restore `sed` matched one of two lines and left
a hook commented out. A doc-edit script raised `AssertionError` after prettier re-padded the table;
one earlier edit left a dangling half-sentence in a backlog row. A renumbering script ate a paragraph
break. A fails-before mutation was reverted with `git checkout`, discarding six uncommitted fix
edits. Prettier turned a wrapped `+` into a list bullet.

**Why.** `str.replace()` is silent on zero matches; flash and run were chained in the same shell
line; prettier runs after every edit so the remembered text is never the text on disk.

**Guard.**

- Every anchored replace asserts `s.count(old) == 1` and prints it; the shell line is
  `python3 ... && pio run ...`.
- Prefer the Edit tool or line-addressed edits (`grep -n "^| CS-04 |"`) over prose anchors; run
  prettier before computing a doc anchor; re-grep the row after formatting.
- `grep -c "^#if" vs "^#endif"` on any file touched by a script; four-env build after every
  scripted source edit.
- Before `git checkout -- <file>` or `git restore`: `git diff --stat <file>` empty or backed up;
  prefer string-replace mutation plus inverse replace.
- `git diff -U0 docs/*.md | grep -n '^+[-+] '` after prettier.

### P12 -- Bench protocol failures `[x15]`

**What happened.** Two roll tests that required the operator to roll the trackball ran in the
background with nobody at the device and counted zero. Eye-test scripts asked ambiguous questions,
swallowed output, wrote logs to the repo root. Bench runs opened ports a soak was using. Orphaned
`until grep` waiters with relative paths accumulated until the user asked twice "do we need the 3
shells?". Upload dry-runs were launched with no node attached and the user interrupted three times.
"Gateway off everywhere" commands were no-ops because two nodes were already off from a test the
backlog said "must be restored". The gwflood experiment was rerun five times before the host UDP
path was proven once. A harness scenario persisted `GPS off` on two nodes via a triple click.

**Why.** Scripts written for the assistant's parsing, not for a human watching a screen; each
rerun tested firmware, LAN and mock at once; pre-flash `--info` was grepped for callsign and version
only.

**Guard.**

- Operator-action experiments print `READY -- <do X now>` and run only after an explicit go.
- Before a bench run: dump `--info` for every node and diff against the fleet table in
  `docs/BACKLOG.md`; list each acceptance criterion and the node state it requires.
- Prove host UDP ingress once with `nc -u` before any node-to-host experiment.
- Any `-t upload` or flash in a session whose request did not mention flashing: ask first.
- Waits above 8 minutes go to `run_in_background`; bounded loops (`for i in $(seq 1 40)`), absolute
  paths, `ps aux | grep -E "[u]ntil grep"` before starting a new waiter.
- Harness scenarios that reach `save_settings()` snapshot and restore the setting.

### P13 -- Numbers stated before they were computed; data honesty `[x15]`

**What happened.** "Eleven hardware ids" (nineteen); "four field nodes" (three); "2422 vectors"
(2650); "72 % CRC collisions" (1.6 to 2.8 %; the classifier counted every relay-path callsign as a
sender); "40 % channel utilisation" (about 14 %; every ESP32 RX billed as 255 bytes); a 247-byte
frame "validates the model exactly" (it proves only "> 244"); `hey/h` tiles count reports, not
transmissions; a 508-node population is receiver-biased and must not be stated network-wide; a
partial bucket cited as the minimum by the very review that flagged it; an RMS against its own median
measures scatter reduction, not accuracy.

**Why.** Numbers typed into prose from memory; a script section consumed without validating the
classifier against a known single-path frame; percentages summed to 100 so nothing looked off.

**Guard.**

- Every headline number gets its derivation inline or its generating one-liner in the commit.
- Recompute headline numbers independently by a second method before they enter a report.
- Label feed-visible vs measured; drop partial buckets; the caveat travels with every number.
- Verify at least one payload field differs between suspected "copies" before concluding replay.
- Fixture per counting rule in `tools/mock/test_loganalyse.py`.

### P14 -- Release hygiene `[x10]`

**What happened.** Three releases in 24 hours, each verified on per-change bench runs, not on a
final-tree run. A release shipped the pre-fix safeboot and the fix landed afterwards; four fleet
nodes still run the old safeboot. "Reuse the gate build" failed its freshness check: 7 of 27 images
had vanished from `.pio/build` within an hour. Both fork workflows are `disabled_manually`, so a tag
push publishes nothing and a CI edit had no effect. `gh release list` returned empty the morning after
two releases and the anomaly was dropped. The PR body cited "485 native test cases / 12 host envs"
while the PR's `platformio.ini` no longer contained those envs.

**Why.** Release cadence outran bench capacity; an exit-0 full build was assumed to leave stable
artefacts; workflow files exist and look live.

**Guard.**

- Tagged commit flashed to at least one ESP32 and one nRF52 node; `--info` build string matches.
- `ls .pio/build/*/firmware.bin | wc -l` equals 27, plus 3 nRF52 `.hex`, plus both safeboot outputs;
  anything less means rebuild.
- `git log --oneline <tag>..HEAD -- src/safeboot safeboot*.bin` non-empty means the published
  release ships a different safeboot than HEAD.
- `gh api repos/DK5EN/MeshCom-Firmware/actions/workflows --jq '.workflows[]|"\(.name)\t\(.state)"'`
  at the start of any release task.
- `grep -c "^\[env:native" platformio.ini` must match the env count quoted in the PR text.

### P15 -- Review scope too narrow for the defect class `[x6]`

**What happened.** Two review stages (36 findings, zero blockers) found none of the three problems
that blocked the T-Deck; the redraw-contract question ("for every screen element, what invalidates
it, and who guarantees that?") appeared in neither. Finders were right on the line and wrong on the
mechanism (setup vs loop caller, PSRAM vs internal heap, 27 MHz vs 40 MHz). The June watchdog fix
was applied without an inventory of existing blocking work. The audio task at priority 50 sits in
`src/esp32/`, outside the stage-1 file scope, yet is spawned by every incoming message.

**Why.** Finders reason from the code excerpt; allocator policy, call-graph context and library
semantics are not visible in it. Checklists find checklist items.

**Guard.**

- Instrument first, then ask the architecture question (redraw contract, blocking-work inventory,
  task and core map) before the checklist.
- For any heap claim: check `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` first. For any "blocks the loop"
  claim: confirm one caller is reachable from `esp32loop()`. For any "reboots" claim: capture at
  least 4 minutes with the port held open and count `rst:` lines.
- Review scope follows the call graph of the event, not the directory.
- Keep the "Refuted claims -- do not re-investigate" table in every review document.

### P16 -- Field evidence and deployed tools outside the repository `[x5]`

**What happened.** GPS field logs disappeared from `~/Downloads` mid-analysis and three scripts
failed; the statistics in the report are not reconstructible from the excerpts. Test corpora are
generated from downloads that are not in the repo. The `meshlogger.py` on the Pi had drifted from the
repo copy. Raw `.log` captures carry the WLAN PSK from `--info` output.

**Guard.**

- First step of any log analysis: copy into `test/corpora/` or the scratchpad and record `md5`;
  the doc cites the md5.
- `ssh rpizero.local md5sum ~/meshlog/meshlogger.py; md5 -q tools/meshlogger.py` before every
  capture run.
- Scan any new `.json` or `summary.*` for `SSID <|PASSWORD` before staging.

---

## Part C -- Bench and tooling pitfalls that cost the most time

| Pitfall                                                                                              | Cost seen                                                       | Rule                                                                                                          |
| ---------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| T-Deck Plus (S3 USB-JTAG) resets on every port open regardless of DTR/RTS; CP2102 boards reset too   | "constant reboots", zeroed counters, 5-min WiFi outage per open | Use the 2323 net console for stateful measurement; check `rst:0x15` in the first 2 s                          |
| RAK4631 does not reset on open, is silent unless `dtr=True`, re-enumerates on `--reboot` (`Errno 6`) | Three dead captures                                             | Reopen loop on `OSError(6)`, 2 s settle                                                                       |
| `printfdeb` strips `;` outside `--debug csv`; `DEBUG_MSG` is a no-op with `DO_DEBUG 0`               | One lost day (TM-31), three flash cycles                        | Bench markers are raw `Serial.printf`                                                                         |
| `--injectmsg` bypasses `OnRxDone`; receivers with `--loradebug off` log nothing                      | False "not received"                                            | Serial messages need the `::` prefix; turn `--loradebug on` on receivers                                      |
| `--info` does not show GPS state; `--pos` does                                                       | Wrong assertion                                                 | Find the owning `commandCheck` branch of a print                                                              |
| T-Deck serial RX line limit about 256 bytes; one char per `loop()`                                   | Silently truncated inject                                       | Pace long lines in 32-byte chunks                                                                             |
| Serial captures contain NUL bytes                                                                    | BSD grep returns nothing                                        | `grep -a`; `LC_ALL=C`                                                                                         |
| Parallel `pio run` (wave agents or parallel sessions) wipes `.pio/build`                             | Builds killed mid-run, images vanished                          | Sequential only; `pgrep -fl pio` first; `.claude/commands/build-firmware.md` said "parallel" until 2026-09-02 |
| `t_deck_pro` env excludes `src/t-deck/*`                                                             | Void verification                                               | Name the env after reading its `build_src_filter`                                                             |
| 921600 baud upload fails on CP2102/CH9102                                                            | Repeated flash failures                                         | 460800; copy the exact `upload_command` and change only `-b`                                                  |
| `gh` resolves to upstream in this checkout; `isLatest` is not a field                                | "CI runs tests", "release not found"                            | `-R DK5EN/MeshCom-Firmware`; `gh api .../releases`                                                            |
| Port 2323 is single-client; a router reboot leaves a zombie socket                                   | 2.4 h of lost log (TM-50)                                       | `PAUSE` file; `--stall-timeout`                                                                               |
| `serial_session.py` buffers until `--listen` ends; `--help` is treated as a port name                | 0-byte logs mid-run                                             | Read the docstring; poll the file only after the window                                                       |
| Chained `sleep N; ...` is blocked; foreground waits hit the 600 s timeout                            | Round trips, orphan shells                                      | `run_in_background`, bounded loops, absolute paths                                                            |
| zsh unquoted `--include=*.cpp`; `cat -A`, `ls --time-style`, `sed N,+Np`, `setsid` are GNU-only      | About 20 wasted calls                                           | Quote globs; `od -c`; `stat -f`; `nohup ... & disown`                                                         |
| Bash cwd resets between calls                                                                        | Edits and staging in the wrong directory                        | Absolute paths or `git -C`                                                                                    |
| Prettier reflow breaks exact-string anchors; wrapped `+` becomes a bullet                            | Silent no-op edits, corrupted paragraph                         | Anchor on the ID cell; run prettier before matching                                                           |
| mcmap absence is not proof (TEST group filtered; 3-min silence threshold)                            | False bench proofs                                              | Pair every "absent" claim with the node's `[UDP];tx` line                                                     |
| Injected station calls equal to the bench node's own call                                            | 728 ms fake stall                                               | Harness asserts no injected call equals `--info` call                                                         |
| Group 9999 is worldwide broadcast                                                                    | Spam on the network                                             | `TEST` group or DM between bench nodes; 20 s LoRa spacing                                                     |
| PlatformIO "RAM %" is not the static-DRAM figure                                                     | 528 B headroom looked like 23 %                                 | `resource_watch.py dram`                                                                                      |
| `tools/code_audit_scan.py` scans whole files                                                         | Pre-existing hits on a PR                                       | Filter findings to PR-added lines first                                                                       |

---

## Part D -- Review checklist

Ordered by frequency times damage. Each line is one check; run the command, read the hits.

**Tier 1 -- every diff (about five minutes)**

1. Paired-file drift (C06): does the diff touch one side of a known ESP32/nRF52 pair? If yes, diff
   the twin function and build `wiscore_rak4631` plus one ESP32 env.
2. Length source (C01): every new `memcpy`, `serializeJson`, `snprintf` bound comes from the
   producer, not from `sizeof(dst)` or `measureJson`.
3. Binding limit (C03/C04): for every new field in a BLE, UDP or JSON frame, the worst-case byte
   count against the consuming clamp is in the commit message.
4. Narrow arithmetic (C02): any `uint8_t` length with `+`/`-`; any `strncat(..., sizeof(dst)-1)`;
   any `unsigned long` compared against `millis()`.
5. Blocking calls (C08): `grep` the diff for `delay(`, `scanNetworks`, `hostByName`, `SD.`,
   `WiFi.getMode`, `esp_wifi_get_`, `i2s_write`; each hit names its task and its bound.
6. Time (C12): every `millis()` comparison is the `(uint32_t)(now - t) >= X` form; no ageing on
   `getUnixClock()`; no id from `millis()`.
7. Order (C22): side effects (echo, uplink, ring insert, dedup insert) come after the gating
   decision; a function that can refuse returns a result and the caller reads it.
8. Guards (C07): every new `#if` tests `defined()`; the macro is defined by some env
   (`pio project metadata`); no new option `#define` in a `.cpp`.
9. Logging (C14): no `printf("%s", ...)` on a hot path; no raw RF text in a `[TAG];` marker; markers
   meant for parsers are `Serial.printf`.
10. Tests (C28): the new test was run red against the old code and the red set is in the test
    comment; the env's `build_src_filter` includes the unit under test.

**Tier 2 -- when the diff touches settings, rings, state machines, nRF52 paths**

11. Settings (C19): struct default equals NVS default; every struct member has a key or is above the
    boundary on purpose; GUI handler routes through `commandAction()`; no `FLASH_STRUCT_VERSION`
    bump without a migration note.
12. Ring (C15): invariants in `test/test_txring` still hold; no index-distance depth; no in-band
    sentinel.
13. nRF52 context (C10/C13): every function reachable from `OnRxDone` is lock-guarded, print-free
    and flash-write-free; large locals on the loop-task path are static.
14. Critical sections (C11): nothing that delays, allocates, logs or talks to the radio inside
    `taskENTER_CRITICAL`; resource guards are mutexes; task priorities below
    `configMAX_PRIORITIES`.
15. State machine (C22): a latched emitter carries no per-message payload; every close path resets
    routing state; thresholds on ring depth were checked against a field `RING_STATUS` distribution.
16. Gates (C25): anything a node with an IP but no gateway needs (NTP harvest, DHCP renew, link
    poll) is outside `if (bGATEWAY)`; the ESP32 and nRF52 gates around the same feature match.

**Tier 3 -- before merge, release or PR**

17. Gate breadth (P09): full native env list, one env per distinct build filter, both safeboot envs,
    `-Werror` on the seven flash targets, skip count printed, zero writers active.
18. Upstream (P08): `merge-tree` first; `git log --grep=Revert`; grep the verdict's silent hunks;
    fetch right before `gh pr create`.
19. Numbers (P06/P13): every count in release docs comes from the green gate that immediately
    precedes the tag; corpus numbers cite their one-liner; no retracted phrase survives in a summary
    table.
20. Artefacts (P14): image count check; tagged commit flashed on one ESP32 and one nRF52; safeboot
    diff against HEAD empty.
21. Dead code (C20): a removal is proven by flash-size byte-identity; a new helper was grepped for an
    existing one first (`grep -rn "utf8\|UTF-8" src/*.h`).

**Tier 4 -- before believing a bench or field result**

22. Positive control (P03): the instrument was shown to change when the thing it measures changes.
23. Reset check: after any serial open, the node's own uptime (`ms=` in the first marker) says
    whether it rebooted; "no rst banner" proves nothing.
24. Fleet state (P12): `--info` of every node diffed against the fleet table before the run.
25. Sample size (P01): a causal claim cites n and outcome per arm; n >= 10 per hypothesis; a second
    board of another class.
26. Provenance (P16): the raw log is in the corpus with an md5 before the first number is written.

---

## Part E -- Gaps in codequality-rules.md 1.0

| 1.0 section           | Covered classes here                     | Missing (add in 1.1)                                                                                                                                                                                                                                                                                                                         |
| --------------------- | ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MEM (1)               | C14 heap churn, C26 largest block        | NVS write budget per event (C10); DRAM headroom gate (C26)                                                                                                                                                                                                                                                                                   |
| BND (2)               | C01, C02, C05 write side                 | length from the producer; APIs that take length by value (C01); truncation must preserve syntax (C04); `-Wconversion`, `-Wvla` (C02/C13)                                                                                                                                                                                                     |
| Input validation (3)  | C05, C18, C19 sanitiser                  | gate must read the field it protects (C17); NUL inside JSON strings (C18)                                                                                                                                                                                                                                                                    |
| RACE (4) / ISR (5)    | C10, C11                                 | task/context ownership map as an artefact (C10); no allocation, delay or logging inside critical sections (C11); priority range (C11)                                                                                                                                                                                                        |
| SPI (6)               | C11 (violated)                           | per-bus user list with task per user                                                                                                                                                                                                                                                                                                         |
| Error handling (8)    | C17 `begin()`, C22 return values         | echo and upload after the operation (C22); a logged-and-ignored failure is a defect (C19 `WiFi.config`)                                                                                                                                                                                                                                      |
| STAB (9)              | C09 (mis-applied), C12                   | blocking-work inventory before arming a watchdog (C09); persisted-setting trap (C09); allow-list of blocking APIs (C08); interval floor (C12)                                                                                                                                                                                                |
| COMP (10) / Type (11) | C02, C07                                 | `defined()`-only guards, no option redefinition in TUs, section uniqueness (C07); unit at call sites (C01 G07)                                                                                                                                                                                                                               |
| Lifetime (12)         | C21                                      | model trim needs a view counterpart (C21)                                                                                                                                                                                                                                                                                                    |
| Logging (13)          | C14 format strings                       | allocation-free, non-blocking, rate-bounded, secrets masked, RF text sanitised (C14); instrument must be able to show the failure (P03)                                                                                                                                                                                                      |
| Design patterns (14)  | C15 SPSC contract                        | ring invariants unit-tested natively; depth means occupied slots (C15)                                                                                                                                                                                                                                                                       |
| Protocol (15)         | C18                                      | one schema, one producer per wire type; MTU from negotiation (C04)                                                                                                                                                                                                                                                                           |
| State machines (16)   | C22 partial                              | check-before-commit; reachability tests; no overloaded return codes; thresholds validated on field load (C22)                                                                                                                                                                                                                                |
| Data drift (17)       | C07 constants, C19 (prescribes the wipe) | version is a layout generation with `static_assert(sizeof)`; migration instead of wipe; paired-file drift; runtime state mirrors (C06/C19)                                                                                                                                                                                                   |
| TCP/Web (18)          | C27 partial                              | HTML output escaping; disable path outside the enable guard (C25)                                                                                                                                                                                                                                                                            |
| Test readiness (19)   | C28 partial                              | fails-before per test; instruments carry their own suite; corpus provenance (C28)                                                                                                                                                                                                                                                            |
| none                  |                                          | drain cadence vs ring (C24); sensor vs setting precedence (C19); hard-coded fallback per config branch (C25); dead duplicate handler chain (C16); positive-evidence detection (C17); driver feature re-implemented (C23); event-vs-poll blind window (C25); dead-code discipline (C20); library-contract citation (C23); the whole of Part B |

---

## Appendix -- Candidate regexes for `tools/code_audit_scan.py`

The mechanical scanner currently implements eleven rules. These twelve are grep-stable, have a
low false-positive rate on this tree, and each maps to a pattern above.

| Rule ID  | Severity | Pattern                                                                                   | Regex                                                                           |
| -------- | -------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| CQ2-C01a | HIGH     | `memcpy` bounded by `sizeof(` of the destination                                          | `memcpy\([^,]+,[^,]+,\s*sizeof\(`                                               |
| CQ2-C01b | CRITICAL | `serializeJson` bounded by `measureJson`                                                  | `serializeJson\([^,]+,[^,]+,\s*measureJson`                                     |
| CQ2-C02a | HIGH     | `strncat` with `sizeof(dst)-1`                                                            | `strncat\([^,]+,[^,]+,\s*sizeof\([^)]+\)\s*-\s*1\)`                             |
| CQ2-C02b | LOW      | `uint8_t` length variable                                                                 | `\buint8_t\s+\w*len\w*\b`                                                       |
| CQ2-C03  | MEDIUM   | register builder checks the wrong ceiling                                                 | `MAX_MSG_LEN_PHONE\s*-\s*2`                                                     |
| CQ2-C08a | HIGH     | blocking library call outside setup                                                       | `scanNetworks\(\)\|hostByName\(\|forceUpdate\(\|WiFi\.getMode\(\|esp_wifi_get_` |
| CQ2-C08b | LOW      | `delay()` of 10 ms or more                                                                | `\bdelay\(\s*[0-9]{2,}\s*\)`                                                    |
| CQ2-C12a | HIGH     | unsafe `millis()` comparison (skips lines with an explicit `(uint32_t)`/`(int32_t)` cast) | `[A-Za-z_\)]\s*[<>]=?\s*millis\(\)\|millis\(\)\s*[<>]=?\s*[A-Za-z_]`            |
| CQ2-C12b | HIGH     | ageing against the wall clock                                                             | `Epoch\[[^]]*\]\s*\+\s*[0-9* ]+\s*>\s*getUnixClock\(\)`                         |
| CQ2-C12c | MEDIUM   | id minted from `millis()`                                                                 | `msg_id\s*=\s*(\(unsigned int\))?millis\(\)`                                    |
| CQ2-C14  | HIGH     | secret in a printf without `maskSecret` (skips `snprintf` writes into the field)          | `(^\|[^n])printf\w*\(.*(node_pwd\|node_passwd\|node_webpwd\|bt_code)`           |
| CQ2-C21  | HIGH     | `[&]` capture registered on an async object                                               | `\.on[A-Z][a-zA-Z]*\(\[&\]`                                                     |

Two checks are not regexes and belong in the scanner as small functions: "macro tested but defined
by no env" (C07, the `comm -23` pipeline) and "struct default vs NVS default" (C19, pair
`preferences.get*("key", DEFAULT)` with the initialiser of the same field).
