# N-25 — GPS baud scan trips the task watchdog and boot-loops the node

**Status:** VERIFIED by code reading + two field logs (same board, our build vs. upstream build), then
re-verified by an eight-angle adversarial review — and since **2026-08-22 reproduced and fixed on the
bench**. Waves 0, 1 and 2 are landed (`dae2d863` S1, `f20b922d` S4, `7bd313bd` S5+B-15); GPS detection
on a Heltec V3 drops from 12 000 ms to 2120 ms. **Waves 3–6 remain open**, as do hardware checks §8.4
and §8.5. Bench record and open-item run book: `docs/gps-sensor-bench-20260822.md`.
**Severity:** Critical — permanent boot loop, node unusable, no recovery window, recoverable only by reflashing.
**Class:** regression introduced on this branch (`v4.35p_prio`), not an upstream defect.
**Reported:** 2026-08-21, T-Beam Supreme (`ttgo_tbeam_supreme`, ESP32-S3), field unit.
**Branch:** `v4.35p_prio` @ `df4c39c1` · **Upstream merge-base:** `8114d7ae`
**Related:** [N-17](architecture/08-defect-catalogue.md) (same root cause, WiFi scan — fixed, but incompletely: see B-13), N-09 (`while(true);` family), N-20 (nRF52 loop stalls).

> **Scope note for the implementer.** Every file:line here was read against the tree at `df4c39c1`
> and re-checked by an independent pass. After any rebase, re-verify before editing — see
> `docs/BACKLOG.md` §0.2.
>
> **Read §7 before §9.** The obvious fix (sprinkle `esp_task_wdt_reset()` at the blocking site) is
> the wrong shape, and §7 explains why. §6 is the evidence for that conclusion.

---

## 1. Symptom

Node boots, associates with WiFi, prints the GPS init banner, then aborts and reboots. Forever.

Our build (defective):

```
CLIENT STARTED
==============
[WIFI]...SSID: GN1 CHAN: 8 RSSI: -68 BSSID: C8:0E:14:70:5F:6E
[WIFI]...connecting to CHAN: 8 BSSID: C8:0E:14:70:5F:6E
[WIFI]...power: 80 RSSI:-69
[GPS ]...Init GPIO RX=9 TX=8
E (27275) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (27275) task_wdt:  - loopTask (CPU 1)
E (27275) task_wdt: Tasks currently running:
E (27275) task_wdt: CPU 0: IDLE0
E (27275) task_wdt: CPU 1: loopTask
E (27275) task_wdt: Aborting.

abort() was called at PC 0x42068ff4 on core 0

Backtrace: 0x40378516:0x3fc9b400 0x403819c9:0x3fc9b420 0x40387abd:0x3fc9b440 0x42068ff4:0x3fc9b4c0 0x4037a529:0x3fc9b4e0 0x420f5f97:0x3fcf4900 0x42068d81:0x3fcf4920 0x40382f8c:0x3fcf4940

ELF file SHA256: ada926990a1cdca6

E (13692) esp_core_dump_flash: Not enough space to save core dump!
E (13698) esp_core_dump_elf: Failed to prepare core dump storage (257)!
E (13704) esp_core_dump_common: Core dump write binary failed with error=257
Rebooting...
ESP-ROM:esp32s3-20210327
rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
```

Upstream firmware, **same board, same GPS module, same WiFi** (control case):

```
[WIFI]...power: 80 RSSI:-69
[GPS ]...Init GPIO RX=9 TX=8
[GPS ]...found with 38400 baud (116 chars)
[GPS ]...erkannte Baudrate: 38400
[GPS ]...Try to init L76K/UBLOX
[GPS ]>>> $PCAS06,0*1B
[GPS ]<<< $GPTXT,01,01,02,SW=URANUS5,V5.3.0.0*1D
[GPS ]...L76K GNSS erfolgreich getestet
[GPS ]...L76K erkannt
[WIFI]...connect OK
[WIFI]...now listening at IP 192.168.1.46, UDP port 1990
```

The control case is what makes this conclusive: the GPS hardware, the baud rate and the detection
logic are all fine. Only our build dies.

> **Do not do arithmetic with the `27275` timestamp.** The same panic block also prints
> `E (13692) esp_core_dump_flash` — the TWDT line uses `ESP_EARLY_LOG`'s cycle-count stamp, a
> different clock. Nothing in this document depends on that number; keep it that way.

---

## 2. Backtrace

The backtrace cannot be symbolised from the log alone — it needs the ELF with SHA256
`ada926990a1cdca6`, the field unit's exact build. What can be read without it:

| Frames                                                           | Stack pointer | Meaning                                                            |
| ---------------------------------------------------------------- | ------------- | ------------------------------------------------------------------ |
| `0x40378516` `0x403819c9` `0x40387abd` `0x42068ff4` `0x4037a529` | `0x3fc9b4xx`  | panic/abort path — `abort()` reported at `PC 0x42068ff4` on core 0 |
| `0x420f5f97` `0x42068d81` `0x40382f8c`                           | `0x3fcf49xx`  | second stack — the interrupted context                             |

Two distinct stack regions are consistent with the abort being raised from the watchdog handler
against a task running on the other core, matching the header (`CPU 1: loopTask`). `0x42xxxxxx` is
flash-mapped text, `0x4037/0x4038xxxx` is IRAM. That is the limit of what the raw addresses support.

**The backtrace is not load-bearing** — the print sequence pins the fault exactly (§3.4). It is
recorded so a decoded version can be attached if the unit is reflashed with a retained ELF.

> **Decoded 2026-08-22 on a second board.** The defect was reproduced on a Heltec V3 with a u-blox
> module, and that crash's backtrace was symbolised against its own ELF (SHA256 `7778525e3221dfae`).
> It confirms the reading above exactly:
>
> | Frame        | Symbol                                             |
> | ------------ | -------------------------------------------------- |
> | `0x40377d9e` | `panic_abort` — `esp_system/panic.c:408`           |
> | `0x4038111d` | `esp_system_abort` — `esp_system.c:137`            |
> | `0x403870cd` | `abort` — `newlib/abort.c:46`                      |
> | `0x4206371c` | `task_wdt_isr` — `esp_system/task_wdt.c:176`       |
> | `0x40379db1` | `_xt_lowint1` — `xtensa_vectors.S:1118`            |
> | `0x420f0783` | `cpu_ll_waiti` (inlined into `esp_pm_impl_waiti`)  |
> | `0x42063ed9` | `esp_vApplicationIdleHook` — `freertos_hooks.c:63` |
> | `0x4038259c` | `prvIdleTask` — `freertos/tasks.c:4099`            |
>
> The second stack is the interrupted `IDLE0`; the starved task is `loopTask` on CPU 1, as the panic
> header says. The addresses differ from the field capture because it is a different build — the
> structure is identical.

> **Actionable:** `variants/ttgo_tbeam_supreme/platformio.ini:4` already sets
> `monitor_filters = esp32_exception_decoder`, so any capture taken through `pio device monitor` on
> the build host decodes automatically. This log came through a plain terminal, so it arrived raw.
> **Use `pio device monitor` for every verification capture in §8.**

---

## 3. Root cause

### 3.1 What this branch changed

Commit `4c21cb49` (2026-06-27, audit finding **C3**, "Main-loop task watchdog") added to
`src/esp32/esp32_main.cpp` — and touched no other file:

| Line    | Call                        | Effect                                       |
| ------- | --------------------------- | -------------------------------------------- |
| `:30`   | `#include "esp_task_wdt.h"` | —                                            |
| `:606`  | `esp_task_wdt_add(NULL)`    | subscribes **loopTask** to the task watchdog |
| `:1815` | `esp_task_wdt_reset()`      | feeds it, first statement of `esp32loop()`   |

At the merge-base `8114d7ae`, `git grep -a esp_task_wdt 8114d7ae -- src/` returns **zero code hits
tree-wide**. Upstream never arms the watchdog anywhere.

```bash
git show 8114d7ae:src/esp32/esp32_main.cpp | grep -an esp_task_wdt   # no output
grep -an esp_task_wdt src/esp32/esp32_main.cpp                       # 30, 606, 1815
```

> The audit row `docs/code-audit-fixes-20260627.md:35` names commit **`f121f3a1`**, not `4c21cb49`.
> `f121f3a1` is a dangling pre-rebase orphan of the same logical change (same author, message and
> content). `4c21cb49` is the reachable ancestor of `HEAD`. Use `4c21cb49`.

**`:606` is the first statement of `esp32setup()`** (the function opens at `:604`). The subscription
therefore covers **the whole of setup as well as the loop**, and the next feed of any kind is
`udp_functions.cpp:611`, reached from `startNetwork()` at `esp32_main.cpp:1739`. Everything in setup
before that point runs subscribed but unfed — see B-7 and B-8 in §6.

Subscribing loopTask is correct in principle. Its side effect is that **every pre-existing long
block — in setup and in one loop iteration alike — became a panic-reboot.** N-17 was the first
casualty. N-25 is the second. §6 shows there are at least twelve more.

### 3.2 The block

`WZ_GPS_Init()` is called at `src/esp32/esp32_main.cpp:2706`, under `if(bGPSON)` (`:2700`) under
`#if defined(ENABLE_GPS)` (`:2694`). That call site is **inside `esp32loop()`**, which spans lines
1813–3829. There is no early return between `:1815` and `:2706`, so the first iteration with
`bGPSON` set reaches it unconditionally. Only two call sites exist tree-wide
(`esp32_main.cpp:2706`, `nrf52_main.cpp:1496`) and neither is in setup.

`WZ_GPS_Init()` (`src/gps_functions.cpp:708`) prints the banner at `:717` and calls
`detectBaudrate()` at `:728`. `src/gps_functions.cpp:22` defines `GPS_BAUDRATE_SOFTCHECK`
**globally, for every board**, after `#include "configuration.h"` at `:13`:

```c
#define GPS_BAUDRATE_SOFTCHECK        // GPS Baudratenermittlung wird mit Software Loop geprüft
```

so `:65-166` is the live implementation and the ISR/edge variant at `:167-242` is dead (see A-1). No
`-D`, `-U` or `#undef` anywhere in `platformio.ini` or any `variants/*/platformio.ini` touches it.
The live loop:

```c
for(int iGpsBaud=0; iGpsBaud < (int)GPS_BAUD_COUNT; iGpsBaud++)   // :75   GPS_BAUDS has 8 entries (:49)
{
    GPSSerial.begin(GPS_BAUDS[iGpsBaud], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);   // :85
    uint32_t start = millis();
    while (millis() - start < 1500)                                // :95
    {
        while (GPSSerial.available()) { /* count NMEA-ish chars */ }
    }
    GPSSerial.end();                                               // :134
}
// winner picked at :143-161
```

Three properties make this fatal:

1. **No yield of any kind.** No `delay()`, `yield()`, `vTaskDelay()` or watchdog feed in the loop
   body. Verified against the arduino-esp32 2.0.17 sources, not just this excerpt:
   `HardwareSerial::begin`'s `yield()`/`delay(100)` sits inside its `if (!baud)` auto-detect branch,
   not taken here; `flush()` reaches `uartFlushTxOnly`, itself a busy-spin with no yield;
   `available()`/`read()` are non-blocking; `end()` has no wait loop. The only yielding call in the
   body is the `Serial.printf` at `:129`, gated on `iGPSDEBUG >= 2` and outside the 1500 ms wait.
   **A `yield()` would not feed the TWDT anyway** — only `esp_task_wdt_reset()` does.
2. **No early exit.** The `for` at `:75-136` has no `break`, `continue` or `return`. Even when the
   correct baud rate is found on the first pass it still walks all eight.
3. **Therefore the duration is constant:** 8 × 1500 ms = **12 000 ms**, plus per-step
   `begin`/`flush`/`end` overhead, independent of whether the GPS answers.

### 3.3 …and it does not stop there

`WZ_GPS_Init()` continues into `GPSprobe()` (`:571`) and the L76K/UBLOX setup, which add **a further
~4–5 s**, also unfed:

- RX-clear loop `:588-603`, bounded by `WAIT_DURATION` = 2000 ms
- `WaitPause()` `:411-432`, up to ~1050 ms
- `readUBX()` `:367-386` — a **sliding** 500 ms idle timeout, reset on every byte received. Against
  a continuously streaming module this has **no upper bound at all.**

The real worst case for `WZ_GPS_Init()` is therefore **~16–17 s with an unbounded tail**, not 12 s.
This matters for the fix: it kills "just raise the watchdog timeout" as a workaround (§12 Q3).

### 3.4 Why it aborts, and why the evidence is conclusive

The task watchdog configuration comes from the framework this project actually pins.
`variants/ttgo_tbeam_supreme/platformio.ini:2` has `extends = esp32` → `platformio.ini:243`
`platform = espressif32@^6.13.0` → that platform pins `framework-arduinoespressif32 ~3.20017.0`,
resolving to the **unsuffixed** package `3.20017.241212`:

```bash
grep -an ESP_TASK_WDT ~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32s3/sdkconfig
# CONFIG_ESP_TASK_WDT=y
# CONFIG_ESP_TASK_WDT_PANIC=y
# CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 is not set
```

The same file's `CONFIG_ARDUINO_RUNNING_CORE=1` independently confirms loopTask runs on CPU 1,
matching the panic header.

> Two other `framework-arduinoespressif32*` packages are installed on this machine
> (`@3.20014.231204` for espressif32 6.5/6.6, and `@src-627abe59…` = arduino-esp32 3.3.7, used only
> by the two safeboot envs). Each has a different sdkconfig path, different line numbers and — in
> the 3.3.7 case — IDF-5 key names (`CONFIG_ESP_TASK_WDT_EN`/`_INIT`). **Cite the resolved package,
> not a glob.** The values agree in all three, so the argument is robust; an earlier citation was not.

**The timing.** The 5 s window starts at the loop-top feed (`:1815`), not at the scan. The work
between `:1815` and `:2706` — `btn.tick()` at `:1818`, the LoRa RX/TX path with its `delay(2)` calls,
`MyClock.CheckEvent()` at `:2656` — consumes part of it. The abort therefore lands roughly **two or
three baud steps into a scan that needs twelve seconds.** Do not write "5 s into the scan".

**No other feed is reachable in that window.** The four N-17 feeds (`udp_functions.cpp:611`, `:624`,
`:631`, `:701`) live inside `startNetwork()`, called from setup `:1739` and from loop `:2731`,
`:3682`, `:3704` — all _after_ `:2706`.

**The print sequence pins it.** `[GPS ]...Init GPIO RX=9 TX=8` (`gps_functions.cpp:717`) is the last
line; `GPS_RX_PIN 9`/`GPS_TX_PIN 8` match `variants/ttgo_tbeam_supreme/configuration.h:100-101`.
Between that print and the scan there is nothing but two `pinMode` calls and `GPSSerial.end()`. The
next reachable print, `[GPS ]...found with %lu baud (%i chars)` (`:154`), appears in the upstream log
and not in ours.

**Why upstream survives the identical block:** loopTask is unsubscribed there and
`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1` is unset, so nothing watches CPU 1 at all. Upstream is not
correct here — it is unmonitored.

**Our branch does touch `gps_functions.cpp`**, so state the comparison precisely rather than implying
the file is untouched: `git diff 8114d7ae..HEAD -- src/gps_functions.cpp` is 8+/9−, covering `:186`
(dead ISR guard — see A-3), the millis-rollover-safe comparisons at `:370`, `:391`, `:414-421`,
`:599`, and `posinfo_hdop`→`fposinfo_hdop` at `:849`/`:853`. **None of them touches the SOFTCHECK
branch (`:65-166`) or `WZ_GPS_Init()` (`:708-756`)** — those are byte-identical to upstream.

---

## 4. Reachability — three triggers, and no way out

**Trigger A — boot.** First `esp32loop()` iteration with `bGPSON` set. `bGPSON` is loaded from the
persisted `node_sset & 0x0040` at `esp32_main.cpp:768`, so a node that had GPS on before the update
boot-loops immediately after it, with no user action.

Factory-fresh nodes are **not** hit: the first-boot branch at `esp32_main.cpp:1008-1015` sets
`node_sset |= 0x0035`, which does not contain `0x40`. (Its comment claims it enables `bGPSON` — the
comment is wrong. See §12 Q6; "correcting" it to match the comment would newly expose fresh units.)

**Trigger B — `--gps on`.** `command_functions.cpp:1520` clears `gpsInitDone`, `:1526` sets
`node_sset |= 0x0040`, and **`:1534` calls `save_settings()` — before the scan crashes.** So this
does not merely abort once: it persists GPS-on and converts itself into Trigger A. **One command
permanently bricks the node into the boot loop.** Reachable from serial, BLE and the net console.

**Trigger C — physical button.** `onebutton_functions.cpp:225` issues
`commandAction("--gps on", false)`, with the same persistence consequence.

`--gps reset` (`command_functions.cpp:1606`) clears `gpsInitDone` but never sets `bGPSON`, so it only
re-triggers the scan on a node where GPS was already on.

**There is no recovery window, and this is provable from code order.** Every command-input path in
`esp32loop()` — the BLE queue drain at `:2859-2865`, `hasMsgFromPhone`/`commandAction` at
`:2867-2879`, `Serial.available()` at `:4018`, `netConsoleAvailable()` at `:4036` — is lexically
**after** `:2706`. During the fatal iteration the code that would read a rescue command never runs.
Recovery requires reflashing or a settings wipe.

> Correction to an earlier draft: the net console is **not** open by default. `bNETCONSOLE` is
> `false` (`loop_functions.cpp:164`), the listener exists only after `--netconsole on`
> (`command_functions.cpp:2207`), and auth is skipped only when the password is empty
> (`net_console.cpp:135-137`). BLE remains unauthenticated by default (N-07); serial needs physical
> access.

---

## 5. Affected environments

`GPS_BAUDRATE_SOFTCHECK` is global, so every board that compiles the GPS path takes it. The only
escape is `GPS_BAUDRATE_SETFIX`, whose early return is at `gps_functions.cpp:69-71`.

| Environment                                                                            | MCU   | Verdict                                                                                                                                                                                   |
| -------------------------------------------------------------------------------------- | ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ttgo_tbeam_supreme`                                                                   | ESP32 | **abort + boot loop** (the reported unit)                                                                                                                                                 |
| `ttgo_tbeam`, `ttgo_tbeam_SX1262`, `ttgo_tbeam_SX1268`, `ttgo-lora32-v21`              | ESP32 | abort + boot loop                                                                                                                                                                         |
| `LilyGo_T-Beam-1W`, `LilyGo_T3_S3_V1_3`, `LilyGo_T_Connect_Pro`                        | ESP32 | abort + boot loop                                                                                                                                                                         |
| `t_deck`, `t_deck_plus`                                                                | ESP32 | abort + boot loop                                                                                                                                                                         |
| `t_deck_pro`                                                                           | ESP32 | Trigger B/C only — `esp32_main.cpp:881` forces `bGPSON=false`; `--gps reset` compiled out (`command_functions.cpp:1599`)                                                                  |
| `esp32-external-radio`                                                                 | ESP32 | abort + boot loop — extends `t_deck_pro`, inherits its config                                                                                                                             |
| `E22-DevKitC`, `E22_XML-DevKitC`, `E22_1262-DevKitC`, `E22_1262_S3-…`, `E22_1268_S3-…` | ESP32 | abort + boot loop                                                                                                                                                                         |
| `esp32-loraprs-e22`, `esp32-loraprs-ra01`                                              | ESP32 | abort + boot loop                                                                                                                                                                         |
| `heltec_wifi_lora_32_V2/V3/V4`, `heltec_wireless_stick`                                | ESP32 | abort + boot loop                                                                                                                                                                         |
| `vision-master-e213`, `vision-master-e213-preview`, `vision-master-e290`               | ESP32 | abort + boot loop                                                                                                                                                                         |
| `T-ETH-ELITE_1262`                                                                     | ESP32 | abort + boot loop                                                                                                                                                                         |
| `heltec_wireless_tracker`                                                              | ESP32 | **not affected by N-25** — `SETFIX 115200` early-returns, _and_ `ENABLE_L76K` gives `GPSprobe()` a fast path. But it is the **worst board for the setup-path blocks** (B-7 + B-8 ≥ 10 s). |
| `heltec_t114`, `t_echo`                                                                | nRF52 | 12–17 s loop stall, **no abort** — no watchdog exists anywhere in `src/nrf52/`                                                                                                            |
| `wireless-paper`                                                                       | ESP32 | **not affected** — no `ENABLE_GPS` (`variants/wireless-paper/configuration.h:23`); GPS body, the `:2706` call site and the `--gps` handlers all compile out                               |
| `wiscore_rak4631`                                                                      | nRF52 | **not affected** — uses `ENABLE_RAK_GPS`, a different macro, with a separate throttled `getGPS()`; the `ENABLE_GPS` call site compiles out                                                |
| `t5_epaper`                                                                            | ESP32 | out of scope — no `configuration.h` in its variant dir; pre-existing non-buildable env, commented out of `default_envs`                                                                   |
| `esp32-safeboot`, `esp32-S3-safeboot`, `native*`                                       | —     | out of scope — `build_src_filter` never includes `gps_functions.cpp`                                                                                                                      |

**How to regenerate this table:**

```bash
grep -an "ENABLE_GPS\|GPS_BAUDRATE_SETFIX\|GPS_BAUDRATE_SOFTCHECK" variants/*/configuration.h
pio project config --json-output     # resolved build_flags / -I / build_src_filter per env
```

Beware commented-out `//#define` lines — they do not count, and several variants carry both forms.
And do **not** pipe a repo-wide `grep` through `head`: the macro search returns 39 hits and
`variants/` sorts before `src/`, so a `head -20` hides `src/gps_functions.cpp:22` and inverts the
entire picture. That truncation — not any file-encoding problem — is how the first draft of this
document got the live code path wrong.

---

## 6. What else the watchdog now kills

This is the inventory `4c21cb49` needed and never got. Every entry blocks ≥5 s or is unbounded, is
reachable from a single `esp32loop()` iteration or from `esp32setup()`, and has **no watchdog feed**.
Two have already reached the field as crash reports (N-17, N-25); the rest are waiting.

| ID   | Path                                                                    | file:line                                                                       | Worst case                           | Reachability                                                                   |
| ---- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------- | ------------------------------------ | ------------------------------------------------------------------------------ |
| B-1  | `doWiFiConnect()` → `startMeshComUDP()` → 2 × `WiFi.hostByName()`       | `udp_functions.cpp:940,957,987,995,1002`                                        | **62 000 ms** (31 000 each)          | boot + every WiFi reconnect on any gateway/web/extudp node                     |
| B-2  | `loopWebserver()` → `sub_page_spectrum()` → status poll                 | `web_functions.cpp:1439-1463`; `spectral_scan.cpp:114-116`                      | **unbounded** (no timeout on status) | anyone reaching the web UI; password optional                                  |
| B-3  | `--spectrum` → `sx126x_spectral_scan()`                                 | `command_functions.cpp:668-673`; `spectral_scan.cpp:138-188`                    | **4 300–6 200 ms+**                  | serial / BLE / net console                                                     |
| B-4  | `loopSOFTSER()` → `sendSOFTSER` → `getSOFTSER`                          | `softser_functions.cpp:267` (6000 ms spin), `:309`                              | **6 500 ms**                         | any node with `--softser on`, every refresh                                    |
| B-5  | `neth.initethDHCP()`                                                    | `esp32_eth.cpp:26`, `:68-76` (50 × `delay(200)`)                                | **11 000 ms**                        | T-ETH-ELITE / T-Connect-Pro, every boot without DHCP                           |
| B-6  | `flushDeferredDisplayUpdates()` → E-Ink refresh `wait()`                | `Displays/BaseDisplay/hardware.cpp:63-67`; `LCMEN2R13EFC1/hardware.cpp:102-105` | **unbounded, no deadline at all**    | **every received message** on wireless-paper / e213 / e290                     |
| B-7  | `esp32setup()` USB-CDC wait, **12 lines after the subscription**        | `esp32_main.cpp:618-621`                                                        | **5 000 ms**                         | boot on battery, all 12 `ARDUINO_USB_CDC_ON_BOOT=1` variants incl. the Supreme |
| B-8  | `esp32setup()` → `displayTFT(…,5000)`                                   | `esp32_main.cpp:1175` → `tft_display_functions.cpp:214`                         | **5 000 ms**                         | `heltec_wireless_tracker`; stacks on B-7                                       |
| B-9  | `WZ_GPS_Init()` beyond the scan — `GPSprobe` + L76K/UBLOX setup         | `gps_functions.cpp:571,588-601,505-560,450`                                     | **+4–5 s, unbounded tail**           | same triggers as N-25 (§3.3)                                                   |
| B-10 | `loopWebserver()` write to a stalled peer                               | `web_functions.cpp:334`; framework `MAX_WRITE_RETRY`                            | **10 000 ms per write**              | remote slow-loris; `setTimeout()` never called                                 |
| B-11 | `commandAction("--help")` — 26 × `delay(100)`                           | `command_functions.cpp:726-796`                                                 | 2 600 ms; ~5 200 ms on a stalled CDC | serial / BLE / net console                                                     |
| B-12 | `--showi2c` → `scanI2C()`                                               | `command_functions.cpp:4436`; `i2c_scanner.cpp:61-68`                           | **12 600 ms** on a hung bus          | user command; fault path                                                       |
| B-13 | `startNetwork()` **AP branch** — returns before reaching the N-17 feeds | `udp_functions.cpp:568-591`                                                     | 1 000–2 000 ms + B-1                 | `--wifiap on` — **the N-17 fix missed this branch**                            |
| B-14 | `startExternUDP()`                                                      | `extudp_functions.cpp:70`, `:115` (`hostByName`)                                | **31 000 ms**                        | `bEXTUDP` with a hostname in `node_extern`                                     |

Also present, lower severity: `startMCU811()` ≈ 6 s in setup; `NTPClient::forceUpdate` ≈ 1 s hourly;
`esp32_main.cpp:3699-3703` `delay(1500)`. And the **11 × `while (true);` in
`t-deck-pro/peri_lora.cpp:48-114`** (finding N-09) sit on the setup path — since setup _is_ watched,
`4c21cb49` silently converted eleven diagnosable hard hangs into an anonymous boot loop.

**Four of these are structurally unbounded**, not merely long: B-1 (DNS), B-2 (spectral status has no
timeout), B-6 (E-Ink BUSY), B-10 (stalled TCP peer). That fact drives §7.

**B-15 — the GPS detection is a bare argmax with no minimum count** (`gps_functions.cpp:143-150`,
`itxt` starts at 0), and `WZ_GPS_Init()` sets `pinMode(GPS_RX_PIN, INPUT)` with no pull-up on any
variant. On a board with `--gps on` and no GPS wired, a single noise byte landing in the matched
character set can "detect" a phantom baud rate and fall through into `GPSprobe()` against nothing.

**Ruled out after reading — do not re-investigate:** `net_console.cpp` (best-effort `MSG_DONTWAIT`
writes at `:97-106`, 30 s auth in its own task at `:122` — use it as the reference model); DS18B20
(`onewire_functions.cpp:281-286`, correctly split); u8g2 `nextPage` loops; `esp32_audio.cpp:483` and
the `while(1)` loops in `t-deck-pro`/`t5-epaper` `peri_gps`/`peri_lora`/`btn_task` (separate,
unsubscribed FreeRTOS tasks); `lv_obj_functions.cpp:3692` (bounded SD read).

**Two comments in this tree actively mislead on this subject** and should be fixed wherever the sweep
touches them: `esp32_main.cpp:620` says `delay()` prevents the watchdog firing, and
`web_functions.cpp:1460` says the same of `yield()`. **Neither feeds the task watchdog.** Only
`esp_task_wdt_reset()` does. `esp32_main.cpp:617` additionally says "maximal 3 Sekunden" over a
5000 ms wait.

---

## 7. Fix direction

**The obvious fix is the wrong shape.** Adding `esp_task_wdt_reset()` next to the GPS scan stops this
crash and should be done as an emergency patch — but it must not be mistaken for the fix:

1. **It does not converge.** Two field crashes found two sites; §6 found twelve more. Nothing
   prevents the thirteenth.
2. **At the four unbounded sites it makes things worse.** Feeding inside an unbounded loop converts a
   panic-reboot into a **permanent silent freeze** — for a mesh node strictly worse than a reboot,
   and the exact failure mode the C3 watchdog was added to catch.
3. **It spreads `#if defined(ESP32)` + `esp_task_wdt_reset()` into files shared with nRF52** — which
   is how the two `detectBaudrate()` implementations drifted apart in the first place.

Recommended order:

**S1 — move the subscription to the end of `esp32setup()`.** One line. Setup legitimately performs
seconds of one-shot init; watching it was never what C3 asked for. Removes B-7, B-8, `startMCU811`
and the entire `while(true);` init family at a stroke, and changes nothing about the loop guard.
**Highest value per unit of risk in this document.**

**S2 — bound the unbounded loops at their source.** Real defects independent of the watchdog: a
deadline in `spectral_scan.cpp:114`; a deadline plus error return in both E-Ink `wait()` implementations —
`BaseDisplay::wait()` is literally `while(digitalRead(pin_busy) == HIGH) { yield(); }` with no
exit condition other than the hardware; `web_client.setTimeout(...)` for B-10; resolve-once-and-cache (or move off the loop
path) for the three `hostByName` sites, which are pure boot-time configuration.

**S3 — set the TWDT period deliberately at runtime and document it** as the floor that covers
legitimately bounded work — explicitly _not_ as the fix, and justified against the §6 table. Note
that B-9's unbounded tail means no finite timeout is safe on its own.

**S4 — targeted feeds** only at what survives S1–S3 (the GPS baud scan, the WiFi scan), behind a
single named helper that compiles to nothing off ESP32 so the pattern stays greppable.

**S5 — fix the GPS scan itself**, watchdog aside: `break` out of the `for` loop once a baud rate is
identified. Cuts typical GPS init from 12 s to ~1.5 s and is a genuine improvement to upstream code.
**The acceptance criterion is decided here, not by the implementer:** break when the window has
yielded at least one complete `$…*HH` NMEA sentence with a valid checksum. A bare character-count
threshold is not acceptable — B-15 is why.

**S6 — `coredump` partition (A-7).** Separate change, separate PR, separate risk.

> **Upstream note.** S2 and S5 are improvements to upstream code and should be offered as their own
> PR with a German description (project rule). S1 and S4 are meaningful only together with
> `4c21cb49`, which is ours — do **not** send them upstream alone.

> **Rejected: a separate watchdog-feeder task.** It would make the watchdog structurally unable to
> detect a wedged main loop, degrading it to "is the scheduler alive" — which the interrupt watchdog
> already covers — and would hide N-17, N-25 and all of §6 rather than fix any of them.

---

## 8. Verification required

A green build is not evidence for this defect.

> **Native testing of the scan is not achievable as first demanded — do not plan around it.**
> `env:native` has `build_src_filter = -<*> +<Regexp.cpp> +<regex_functions.cpp>`;
> `gps_functions.cpp` is not compiled there. `gps_functions.h` includes `<TinyGPSPlus.h>` and
> `<HardwareSerial.h>`, and `test/support/` shims neither (it has only `Arduino.h`,
> `configuration.h`, `debugconf.h`, `printfdeb_functions.h`). `ENABLE_GPS`, `GPS_RX_PIN` and
> `GPS_TX_PIN` exist only in `variants/*/configuration.h`, so nearly the whole file preprocesses away
> natively. The scan calls `GPSSerial.begin/available/read/end` on a real `HardwareSerial` with no
> injection seam. Making it natively testable is a refactor plus new shims plus a `platformio.ini`
> env — a project in its own right, not a precondition for this fix.

Required:

1. **Regression coverage, honestly scoped.** Either (a) extract the scan's duration/selection logic
   into a pure function with an injected clock and serial source, test _that_ natively, and say so in
   the PR; or (b) declare the timing property hardware-only and verify it by capture. Do not claim
   native coverage that does not exist. If (a), the extraction is its own wave and owns
   `gps_functions.cpp`, `gps_functions.h`, `platformio.ini` and `test/support/`; the gate must name
   the suite explicitly (`pio test -e native -f <suite>`), because a bare `pio test -e native` will
   re-run only the existing suites and report a false pass.
2. **Hardware, the reported unit.** T-Beam Supreme with its L76K: boot with `bGPSON` set must reach
   `[GPS ]...L76K erkannt` and a running node, with no `task_wdt` / `abort()` / `Rebooting...`.
3. **Hardware, Trigger B/C.** On a running node send `--gps reset`, then `--gps on`, then press the
   user button; none may abort. Remember `--gps on` persists **before** crashing, so a failed attempt
   leaves the node boot-looping — have the recovery procedure ready first.
4. **Hardware, the no-GPS case.** A board with `--gps on` and no module attached must complete the
   scan without aborting **and must not report a phantom baud rate** (B-15).
5. **Hardware, B-7.** Boot on battery with no USB host attached, on a board with
   `ARDUINO_USB_CDC_ON_BOOT=1`. The USB-connected bench never exercises this case.
6. **Second MCU family.** Build `wiscore_rak4631` and confirm nRF52 behaviour is unchanged.
7. **RAM/flash delta** recorded for `ttgo_tbeam_supreme`, `heltec_wifi_lora_32_V3`, `wiscore_rak4631`.

**Capture every verification through `pio device monitor`** so a crash decodes (§2).

> **Status as of 2026-08-22** — full record in `docs/gps-sensor-bench-20260822.md`.
>
> | Item | Status                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
> | ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
> | 1    | **Not done.** No native coverage claimed. The timing property is evidenced by hardware capture only.                                                                                                                                                                                                                                                                                                                                                                                                                         |
> | 2    | **Substituted.** The reported Supreme was not on the bench. Reproduced and verified on a Heltec V3 with a u-blox module, same failure signature, decoded backtrace. Supreme builds clean; not run on hardware.                                                                                                                                                                                                                                                                                                               |
> | 3    | **Passed, complete.** `--gps reset` (2309 ms), `--gps off` + `--gps on` (2249 ms), and the button: a triple click on PRG runs `--gps on` + `--track on` (`onebutton_functions.cpp:210`). Executed on a board with no module, so the callback drove a full 12 001 ms scan — no abort, no reboot, node uptime unbroken.                                                                                                                                                                                                        |
> | 4    | **Passed, with one limit.** A second Heltec V3 with no module: scan completes all 8 rates in 12 000 ms, reports `keine gueltige NMEA-Sequenz`, then `[GPS_ERR]`. No abort, no boot loop, **no phantom baud rate**. Limit: B-15's actual trigger was not exercised — the floating RX pin read zero characters at every rate, so even the old argmax reported failure correctly (verified against upstream `v4.35p.08.20` on the same board). The fix is shown to report no phantom; it is not shown to survive a noisy pin.   |
> |      | Same board on the **pre-fix** build is worse than the with-GPS case: abort 5.09 s in, boot loop, and **no per-baud output at all** — the debug print is gated on `GPS_BAUDS_RX > 0`, so the operator sees only `Init GPIO` and then the watchdog.                                                                                                                                                                                                                                                                            |
> | 5    | **Deferred, and not testable on this bench.** `heltec_wifi_lora_32_V3` has `-DARDUINO_USB_CDC_ON_BOOT=1` commented out (`variants/heltec_wifi_lora_32_V3/platformio.ini:28`), so `Serial` is the UART bridge, `!Serial` is never true and the 5000 ms wait at `esp32_main.cpp:618` never runs. The classic T-Beam has no native USB and the RAK4631 is nRF52. Needs one of: Supreme, heltec_wifi_lora_32_V4, wireless_tracker, T3_S3_V1_3, t_deck, t_deck_plus, t_deck_pro, t5_epaper, T-ETH-ELITE, vision-master-e213/e290. |
> | 6    | **Passed.** `wiscore_rak4631` builds; flashed and run on a RAK4631. nRF52 arms no task watchdog at all, so this path is unaffected; its own GPS defect is catalogued separately as N-26.                                                                                                                                                                                                                                                                                                                                     |
> | 7    | **Recorded.** V3 +456 B flash, Supreme +456 B, RAK4631 +16 B. RAM unchanged on all three.                                                                                                                                                                                                                                                                                                                                                                                                                                    |

**Recovery procedure for a boot-looping node** — establish before any hardware wave starts, per
`CLAUDE.md`: ESP32 boards via `esptool` erase + reflash (`tools/esp32_erase.sh`); nRF52 via
double-tap reset into the UF2 bootloader. A node in this loop cannot be reached over the air, by BLE,
or over the net console (§4).

**If the hardware for a given wave is not on the bench**, land the code wave with its hardware checks
explicitly marked deferred in the commit message and the catalogue — do not silently skip them and do
not claim verification that did not happen.

---

## 9. Suggested wave breakdown for `/orchestrate-waves`

> **Waves 1, 2 and 3 all own `src/gps_functions.cpp`. They MUST run strictly serially.** Only waves 4
> and 5 may run alongside wave 3. One shared file is enough to corrupt a wave.

| Wave | Serial? | Goal                                                               | Exclusive files                                                                                                     | Gate                                                                               |
| ---- | ------- | ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| 0    | —       | **S1**: move `esp_task_wdt_add(NULL)` to the end of `esp32setup()` | `src/esp32/esp32_main.cpp`                                                                                          | both MCU families build; hardware §8.5 (battery boot, no USB) passes               |
| 1    | **A**   | **S4 (emergency)**: feed the watchdog across the GPS scan          | `src/gps_functions.cpp`                                                                                             | hardware §8.2 passes — the reported unit boots. Duration still ~16 s **by design** |
| 2    | **A**   | **S5**: early exit on a checksum-valid NMEA sentence; fix B-15     | `src/gps_functions.cpp`                                                                                             | hardware §8.2 **and** §8.4; typical GPS init < 3 s, measured and recorded          |
| 3    | **A**   | **A-1…A-9**: delete or repair the dead ISR variant and the drifts  | `src/gps_functions.cpp`                                                                                             | no behaviour change on any live path; both families build                          |
| 4    | —       | **S2**: bound B-2, B-6, B-10, B-1; close B-13                      | `src/spectral_scan.cpp`, `src/web_functions/`, `src/Displays/`, `src/udp_functions.cpp`, `src/extudp_functions.cpp` | each bound has a test or a documented hardware check                               |
| 5    | —       | **S6**: `coredump` partition                                       | `partitions-4MB-safeboot.csv`, `partitions-16MB-safeboot.csv`                                                       | **hardware flash + OTA test required** — see the warning below                     |
| 6    | —       | **S3** + fold the §6 inventory into the defect catalogue           | `docs/architecture/08-defect-catalogue.md`, `docs/BACKLOG.md`                                                       | catalogue carries B-1…B-15 with IDs; no code change                                |

Wave 0 goes first deliberately: one line, it removes the largest class of exposure, and it de-risks
every hardware step that follows.

> **Wave 5 warning the implementer needs.** An app-only OTA does **not** rewrite the on-flash
> partition table — the table lives at a fixed offset (`0x8000`) and is not part of the OTA payload.
> A field node updated over the air keeps its **old** table regardless of what the new binary's CSV
> says. Verify both: (a) an OTA'd node with the OLD table still boots and its app still fits the OLD
> app-partition size; (b) a factory-reflashed node with the NEW table boots clean. Note
> `t_deck`/`t_deck_plus` use `partitions-16MB-safeboot.csv`, which has the same 4 KB coredump, and
> `t_deck_pro`/`t5_epaper` use the platform's stock table, which is unverified here.

**Do not let a writer agent commit.** Orchestrator commits only, after reading the diff and re-running
the gate itself — a subagent's self-report is not verification.

---

## 10. Adjacent defects in the GPS file

Not the reported crash. Recorded so they are fixed or deleted deliberately rather than rediscovered.

**A-1 — the ISR/edge `detectBaudrate()` (`gps_functions.cpp:197-240`) is dead code**, the `#else` of
the global define at `:22` (`#if` `:65`, `#else` `:167`, `#endif` `:242`). Confirmed nothing outside
that block references `SAMPLE_COUNT`, `SAMPLE_DURATION`, `pulseIndex` or `handleRxInterrupt`, and
nothing else lives inside it. Delete it, or make the choice a per-variant flag — an unreachable second
implementation is how the two drift.

**A-2 — `return -1` from an `unsigned long` function** (`:210` and `:226`, inside A-1). Wraps to
`4294967295`. `detectedBaud` is a file-scope `unsigned long` (`:59`) with no narrowing, so the
caller's `if (detectedBaud > 0)` (`:730`) is **true** for the failure value: it would call
`GPSSerial.begin(4294967295, …)` and the `[GPS_ERR]` branch at `:753` is unreachable. `-Werror` is on
for `src/` but without `-Wconversion`, so it compiles silently. Latent only because A-1 is dead.

**A-3 — the ISR variant's early exit is unreachable, and this is OUR regression.**
`git diff 8114d7ae..HEAD -- src/gps_functions.cpp` changes `:186` from `pulseIndex < SAMPLE_COUNT` to
`pulseIndex + 1 < SAMPLE_COUNT`. Upstream lets `pulseIndex` reach 50 so the wait at `:204` can exit;
ours caps it at 49 against a `< 50` test, so only the `millis()` timeout can end it.
`SAMPLE_COUNT = 50` (`:170`). Inert today because A-1 is dead — but it must be labelled
branch-introduced, not inherited.

**A-4 — `GPS_BAUDRATE_SETFIX` is honoured only in the SOFTCHECK branch.** The early return (`:69-71`)
is inside `#if defined(GPS_BAUDRATE_SOFTCHECK)`; the `#else` variant has no SETFIX handling. Latent
while the define is global; a live trap the moment anyone makes it per-variant.

**A-5 — comment drift only.** `:94` says "2 Sekunden lang" over a 1500 ms wait (`:95`).
**Correction to an earlier draft: `WAIT_DURATION` is NOT dead.** It is live at `:588` and `:599`,
bounding the RX-clear timeout inside `GPSprobe()`, which `WZ_GPS_Init()` calls at `:743`. Only the
`:1010` reference sits in the commented-out `WZ_L76Kreset()` (`:996-1032`).

**A-6 — `gpsDetected` has three writers**, not two: `detectBaudrate()` (`:141`, `:156`),
`WZ_GPS_Init()` (`:741`, `:754`) and the `--gps off` handler (`command_functions.cpp:1540`). Declared
at `loop_functions.cpp:26`. Redundant but consistent today; not harmful.

**A-7 — no core dumps.** `partitions-4MB-safeboot.csv:6` allocates `coredump` **4 KB** at `4092K`;
`partitions-16MB-safeboot.csv:7` allocates the same 4 KB at `16380K`. Error `257` is `ESP_ERR_NO_MEM`,
consistent with the size check failing, and the failure itself proves coredump-to-flash is enabled.
Every env inheriting `[esp32]` (`platformio.ini:242`, table at `:284`) or overriding to the 16 MB
table hits this. `t_deck_pro` and `t5_epaper` extend neither and fall back to the platform's stock
table — **unverified**; do not assert it either way.

**A-8 — nRF52 drift in the re-entry path.** The defensive `GPSSerial.end()` before re-detection
(`:722-726`, "vorsorglich schließen") is **commented out** on the T114/T-Echo branch (`:719-721`).
`--gps on`/`--gps reset` on those boards re-enters `Serial1.begin()` without closing the previous
instance. Whether the nRF52 UARTE tolerates a double-begin is not answerable from this repo.

**A-9 — `--gps reset` returns without setting `bReturn`** (`command_functions.cpp:1608`), so the user
likely gets no acknowledgement. UX only; not traced to the consumer.

---

## 11. Refuted during review — do not re-investigate

- **"A plain `grep` skips `gps_functions.cpp` because of its encoding."** False. `file` reports plain
  UTF-8 and `grep -rn` finds `:22` from the repo root. The first draft's wrong conclusion came from
  `| head -20` truncating a 39-hit result. Recorded because the false explanation was repeated twice
  before being caught.
- **"`wireless-paper` is affected."** It has no `ENABLE_GPS` at all.
- **"The net console is unauthenticated in the default configuration."** `bNETCONSOLE` defaults false;
  the listener is opt-in.
- **"`WAIT_DURATION` is a dead constant."** Live in `GPSprobe()`.
- **"The abort lands ~5 s into the scan."** The window starts at the loop-top feed; it lands two or
  three baud steps in.
- **A separate feeder task** as the fix — rejected with reasons in §7.
- **BLE-vs-loop-task concurrency on `GPSSerial`** — already mitigated: BLE commands are queued and
  drained only in the loop task (`nrf52_ble.cpp:34-39`, mirrored at `esp32_main.cpp:2859-2865`).
- **The `detectedBaud` shadow** inside `detectBaudrate()` (`:59` vs `:73`) — a smell, not a bug; the
  return value is what propagates.
- **`msg_text` clobbering between GPS and command code** — synchronous fill-then-consume within one
  loop-task iteration; not exploitable in the observed single-threaded model.
- **`init_loop_function()` mattering to re-init** — it touches only position fields
  (`loop_functions.cpp:2599-2607`), not `gpsInitDone`/`bGPSON`/`gpsDetected`.

---

## 12. Open questions

1. Was `4c21cb49` reviewed against any inventory of existing blocking work? §6 suggests not. The
   answer decides how much of the §6 table is in scope now versus later.
2. Should the watchdog cover `esp32setup()` at all? S1 says no. Anyone who disagrees must address
   B-7, B-8 and the eleven `while(true);` in `peri_lora.cpp` concretely.
3. **Raising `CONFIG_ESP_TASK_WDT_TIMEOUT_S` is not a fix.** B-1, B-2, B-6, B-10 and B-9's `readUBX()`
   tail are unbounded, so no finite value is safe. Any raise must be justified against the §6 table
   and documented as a floor, not a remedy.
4. Do `heltec_t114` and `t_echo` interact with the nRF52 loop-freeze family (N-20) through this same
   12–17 s stall? Needs hardware.
5. How many field units are already boot-looping? Trigger A fires on the first boot after update for
   any node with GPS previously enabled, and affected users cannot report it — the node never finishes
   booting, and §4 proves there is no command window.
6. `esp32_main.cpp:1008-1015` sets `node_sset |= 0x0035` with a comment claiming it enables `bGPSON`
   (`0x40`). Which is right — the mask or the comment? "Correcting" it toward the comment would expose
   factory-fresh units to Trigger A.
