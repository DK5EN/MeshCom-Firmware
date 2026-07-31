# 09 — Concurrency Map

> **Which state is touched by which core, task and ISR — so that atomics sit exactly where there is
> genuine concurrent access, and nowhere else?**

This document answers that question in both directions. It maps every execution context on both MCU
families, then every piece of shared state onto the contexts that write and read it, and gives a
verdict per object: **RACE** (genuinely concurrent, unprotected), **OK** (concurrent, correctly
protected), **OVER-SYNC** (single-context but carrying an atomic, a `volatile` or a lock), **DEAD**
(no readers and/or no writers at all), or **single** (single-context and unmarked — correct as is).
It is derived from the raw finder report `docs/review/2026-07-31/f2-concurrency.md`, but **every
file:line citation and every structural claim in it was re-verified against the current working tree
after the rebase onto `upstream/dev`**; §8 records what changed, what was refuted, and what is
carried over unverified.

Baseline: branch `v4.35p_prio` @ `3fb2c917`. Reference targets `heltec_wifi_lora_32_V3` (ESP32-S3,
2 cores) and `wiscore_rak4631` (nRF52840, 1 core, SoftDevice S140). Cross-references:
[08 §1 C-01](08-defect-catalogue.md), [08 §2 N-13 … N-16](08-defect-catalogue.md),
`fable-verdict.md` CONC-14 … CONC-19.

---

## 1. Execution contexts

### 1.1 ESP32 — `heltec_wifi_lora_32_V3` (ESP32-S3, dual core)

Core assignment comes from the board JSON, not from `platformio.ini`:
`-DARDUINO_RUNNING_CORE=1`, `-DARDUINO_EVENT_RUNNING_CORE=1`
(`~/.platformio/platforms/espressif32@6.13.0/boards/heltec_wifi_lora_32_V3.json:11-12`).

| Context                                                                    | Core                                                                              | Prio                          | Stack                              | Entry point                                                                                | What wakes it            |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------------------- | ----------------------------- | ---------------------------------- | ------------------------------------------------------------------------------------------ | ------------------------ |
| `loopTask` → `loop()` → `esp32loop()`                                      | **1**                                                                             | 1                             | 8192 B (`ARDUINO_LOOP_STACK_SIZE`) | framework `cores/esp32/main.cpp:40,71`; `src/main.cpp:52`; `src/esp32/esp32_main.cpp:1751` | free-running             |
| DIO1 GPIO ISR — `setFlagReceive` / `setFlagSent`                           | **1** (registered from `esp32setup()` on `loopTask`)                              | GPIO ISR (IDF lvl 1)          | shared GPIO ISR stack              | `src/esp32/esp32_main.cpp:490`, `:506`; registered `:1428`, `:1467-1470`, `:2066`, `:3799` | SX1262 DIO1 edge         |
| NimBLE host task — `MyServerCallbacks`, `CharacteristicCallbacks::onWrite` | **0** (`CONFIG_BT_NIMBLE_PINNED_TO_CORE 0`, `NimBLE-Arduino/src/nimconfig.h:196`) | NimBLE default                | 3072 B (`platformio.ini:188`)      | `src/esp32/esp32_main.cpp:301-360`                                                         | BLE link / GATT write    |
| BT controller tasks                                                        | 0                                                                                 | very high                     | lib                                | IDF                                                                                        | radio                    |
| `con_auth` (net-console HMAC handshake)                                    | **1** (explicit)                                                                  | 1                             | 3072 B                             | `src/net_console.cpp:378`                                                                  | TCP accept               |
| WiFi / lwIP `tcpip_task`, `wifi`                                           | 0 (IDF default)                                                                   | 18–23                         | IDF                                | IDF                                                                                        | network                  |
| Arduino event task (`WiFi.onEvent`)                                        | **1** (`ARDUINO_EVENT_RUNNING_CORE=1`)                                            | —                             | —                                  | framework                                                                                  | WiFi events              |
| GPS RX-edge ISR `handleRxInterrupt`                                        | 1                                                                                 | GPIO ISR                      | —                                  | `src/gps_functions.cpp:182`; attached `:202`, detached `:205`                              | GPS UART edge, boot only |
| Web server                                                                 | **no task** — synchronous `WiFiServer` polled from `loopTask`                     | —                             | —                                  | `src/web_functions/web_functions.cpp`                                                      | polled                   |
| `audio play task` — **T-Deck / T-Deck-Pro only**                           | **1**                                                                             | **50**                        | 16 KB                              | `src/esp32/esp32_audio.cpp:104`                                                            | play request             |
| `lora_task` — **T5-ePaper only**                                           | **unpinned**                                                                      | 23 (`configMAX_PRIORITIES-2`) | 3 KB                               | `src/t5-epaper/peri_lora.cpp:171`, `:176`; calls `checkRX(true)` at `:220`                 | `receivedFlag` poll      |
| `gps_task` — **T5-ePaper / T-Deck-Pro only**                               | **unpinned**                                                                      | 24 (`configMAX_PRIORITIES-1`) | 3 KB                               | `src/t5-epaper/peri_gps.cpp:77`, `src/t-deck-pro/peri_gps.cpp:78`                          | GPS UART                 |
| `a7682_handle` — **T-Deck-Pro only**                                       | unpinned                                                                          | 20                            | 3 KB                               | `src/t-deck-pro/tdeck_pro.cpp:381`                                                         | modem                    |
| `btn_task` (created under the name `"lora_task"`) — **T5-ePaper only**     | unpinned                                                                          | 20                            | 3 KB                               | `src/t5-epaper/t5epaper_main.cpp:671`                                                      | button                   |

Notes:

- **There is exactly one `xTaskCreate*` in ESP32 common code**: `con_auth`
  (`net_console.cpp:378`). Everything else in the table is board-specific.
- No `AsyncWebServer`/AsyncTCP in the main firmware. `CONFIG_ASYNC_TCP_RUNNING_CORE=1` appears only
  in the two `*-safeboot` envs (`platformio.ini:213`, `:253`), which build a **separate image** with
  no shared state.
- No `xTimerCreate` / `esp_timer_create` in the ESP32 main firmware. `lib/Timeout/Timeout.h` is a
  `millis()` polling helper.

### 1.2 nRF52 — `wiscore_rak4631` (also `t_echo`, `heltec_t114`)

FreeRTOS config: `configUSE_PREEMPTION=1`, **`configUSE_TIME_SLICING=0`**, `configMAX_PRIORITIES=5`,
`configTICK_RATE_HZ=1024`
(`framework-arduinoadafruitnrf52/cores/nRF5/freertos/config/FreeRTOSConfig.h:50,68,56,55`).
With time-slicing off, **equal-priority tasks never round-robin on the tick** — a switch between
them happens only on an explicit yield or a block.

| Context                                                                                                                    | Core   | Prio                                | Stack                          | Entry point                                                                                                                                      | What wakes it                     |
| -------------------------------------------------------------------------------------------------------------------------- | ------ | ----------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------- |
| `loop` task → `loop()` → `nrf52loop()`                                                                                     | single | **1** (`TASK_PRIO_LOW`)             | 1024 words = **4 KB**          | `cores/nRF5/main.cpp:42,88`; `src/nrf52/nrf52_main.cpp:1105`                                                                                     | free-running                      |
| **`LORA` task** → `Radio.BgIrqProcess()`                                                                                   | single | **1** — see the correction below    | 4096 words = **16 KB**         | `SX126x-Arduino/src/boards/mcu/board.cpp:474`, `:498`                                                                                            | `_lora_sem` given by the DIO1 ISR |
| DIO1 GPIO ISR `RadioOnDioIrq`                                                                                              | single | ISR                                 | —                              | `boards/sx126x/sx126x-board.cpp:115`; body `radio/sx126x/radio.cpp:1338-1356`                                                                    | DIO1 rising edge                  |
| **FreeRTOS timer service task**                                                                                            | single | **2** (`configTIMER_TASK_PRIORITY`) | 256 words = **1 KB**           | `FreeRTOSConfig.h:92`, `:94`                                                                                                                     | timer expiry                      |
| ↳ `RadioOnRxTimeoutIrq` / `RadioOnTxTimeoutIrq` → `RadioBgIrqProcess()` → **the whole `RadioEvents` set incl. `OnRxDone`** | single | **2**                               | **1 KB**                       | `radio.cpp:1283`, `:1294`, `:1299`, `:1310`; registered `:598-599`; `SoftwareTimer` = `xTimerCreate` (`cores/nRF5/utility/SoftwareTimer.cpp:39`) | RX/TX timeout                     |
| ↳ `periodic_wakeup` → `api_wake_loop`                                                                                      | single | 2                                   | 1 KB                           | timer created `src/nrf52/api_functions.cpp:341`; callback `src/nrf52/WisBlock-API.cpp:34`                                                        | `send_repeat_time`                |
| Bluefruit `Callback` task (`ada_callback`)                                                                                 | single | **2** (`TASK_PRIO_NORMAL`)          | 768 words = **3 KB**, queue 64 | `cores/nRF5/utility/AdaCallback.c:147`                                                                                                           | `ada_callback` queue              |
| ↳ `connect_callback`, `disconnect_callback`, `bleuart_rx_callback`, `settings_rx_callback`                                 | single | 2                                   | **3 KB**                       | `src/nrf52/nrf52_ble.cpp:205`, `:226`, `:243`, `:296`; dispatched via `ada_callback` (`bluefruit.cpp:829,849`, `BLECharacteristic.cpp:539`)      | BLE events / GATT writes          |
| Bluefruit `BLE` / `SOC` SoftDevice tasks                                                                                   | single | 3 (`TASK_PRIO_HIGH`)                | 5×256 words                    | `Bluefruit52Lib/src/bluefruit.cpp:473`, `:480`                                                                                                   | SoftDevice events                 |
| Button GPIO ISR `interruptHandle2`                                                                                         | single | ISR                                 | —                              | `src/nrf52/nrf52_main.cpp:336`; attached `:1036`                                                                                                 | `MIDDLE_BUTTON` falling edge      |

> #### Correction: the `LORA` task runs at priority **1**, not 2
>
> The raw report, [08 C-01](08-defect-catalogue.md) and [01 §"global variable bus"](01-system-overview.md)
> all state that the SX126x `"LORA"` task runs at priority 2 and therefore preempts `loop()` at
> priority 1. **It does not.** `boards/mcu/board.cpp:44-45` contains
>
> ```c
> #ifndef TASK_PRIO_NORMAL
> #define TASK_PRIO_NORMAL 1
> #endif
> ```
>
> `TASK_PRIO_NORMAL` in the Adafruit core is an **enum constant**, not a macro
> (`cores/nRF5/rtos.h:56-62`), so `#ifndef` is true and the macro wins for the rest of the
> translation unit. `xTaskCreate(_lora_task, "LORA", 4096, NULL, TASK_PRIO_NORMAL, …)` at `:498`
> therefore creates the task at priority **1**.
>
> Verified from the object code (`.pio/build/wiscore_rak4631/libfe3/SX126x-Arduino/boards/mcu/board.cpp.o`,
> `_Z15start_lora_taskv`): the 5th `xTaskCreate` argument is passed on the stack as
> `movs r3,#1 ; str r3,[sp,#0]`, with `r2 = 4096`. The core's own `loop` task
> (`libFrameworkArduino.a(main.cpp.o)`) is created with the same `[sp,#0] = 1` and `r2 = 1024`. The
> Bluefruit `Callback` task (`AdaCallback.c.o`) is created with `[sp,#0] = 2`.
>
> **Consequence:** the `LORA` task cannot preempt `loop()` at an arbitrary instruction. It runs only
> when the loop blocks or yields. The genuinely preempting radio context is the **FreeRTOS timer
> service task at priority 2**, which runs the SX126x timeout callbacks and, through
> `RadioBgIrqProcess()`, the complete `RadioEvents` dispatch — including `OnRxDone` — on a **1 KB
> stack**. See §2 and F2-21.

---

## 2. The asymmetry

**This is the single most important structural fact in this document, and no other doc states it
correctly.**

**On ESP32 the LoRa path is effectively single-context.** The radio callbacks
`OnRxDone`/`OnTxDone`/`OnRxError`/`OnRxTimeout` do **not** run in interrupt or task context — they
run in `loopTask`:

- `OnRxDone` has exactly one call site, `esp32_main.cpp:3840`, inside `checkRX()`
  (defined `:3754`), which is called from `esp32loop()` at `:2225`.
- `OnTxDone()` is called from `esp32loop()` at `:2283`.
- The only LoRa ISR is `setFlagReceive`/`setFlagSent` (`:490`, `:506`) — two identical nine-line
  bodies that touch four `std::atomic<bool>` and nothing else.
- The NimBLE host task on core 0 reaches shared state through exactly one call:
  `xQueueSend(bleQueue, &item, 0)` at `:359`.

Therefore anything not named `receiveFlag`/`transmittedFlag`/`bEnableInterrupt*`, not written by
NimBLE, and not on a T5-ePaper/T-Deck-Pro board is **`loopTask`-only by construction**. The genuine
ESP32 exposure is four NimBLE connection flags plus the ISR flag pair. Everything else in the LoRa
path that carries an atomic, a `volatile` or a spinlock is paying for nothing.

**On nRF52 the same code is genuinely concurrent, but through a different door than assumed.** The
`LORA` task shares priority 1 with `loop()` and time-slicing is off, so it interleaves with the loop
only at yield points. Those exist — `yield()` after every `loop()` iteration
(`cores/nRF5/main.cpp:68-69`), `delay()` → `vTaskDelay()` (`cores/nRF5/delay.c:33-48`), blocking
semaphore takes, and — the one that matters inside the ring enqueue sequences — **`Serial.printf`,
which calls `yield()` whenever the TinyUSB CDC write FIFO is full**
(`Adafruit_TinyUSB_Arduino/src/arduino/Adafruit_USBD_CDC.cpp:253`). The **unconditional** preemption
comes from priority 2: the Bluefruit `Callback` task (which runs `readPhoneCommand` and the settings
`memcpy` inline) and the FreeRTOS timer service task (which runs the full radio callback set via
`RadioOnRxTimeoutIrq` → `RadioBgIrqProcess`).

What this means for where synchronisation belongs:

1. **ESP32 LoRa path: remove.** 14 objects (§5) are single-context; two of them are dead. The one
   spinlock in the codebase (`displayMux`) protects a producer and a consumer that are the same
   task, and does so around seven heap allocations with interrupts disabled.
2. **nRF52 LoRa path: atomics are necessary and not sufficient.** `iWrite`/`iRead` being
   `std::atomic<uint8_t>` does not make `ringBuffer[iWrite][0]=…; memcpy(ringBuffer[iWrite]+2,…);
addTxRingEntry()` an atomic enqueue. The fix is one writer or one lock, not more atomics.
3. **nRF52 BLE path: move the work.** `readPhoneCommand` and the settings apply run at priority 2
   and preempt every reader. The ESP32 already has the right shape (`bleQueue`); porting it
   collapses four findings at once.
4. **Any "make it atomic" change must be evaluated per MCU family.** The two families have opposite
   problems in the same source lines.

---

## 3. Shared-state ownership map

Verdict legend: **RACE** genuinely concurrent and unprotected · **OK** concurrent and correctly
protected · **OVER-SYNC** single-context but synchronised · **DEAD** no readers and/or no writers ·
**single** single-context and unmarked (correct).

"radio ctx" on nRF52 means _either_ the `LORA` task (prio 1) _or_ the FreeRTOS timer service task
(prio 2) — see F2-21.

### 3.1 LoRa radio, TX ring, CSMA

| Object                                                                                                                     | Declaration                                     | Writers                                                                                                                                                                                                                                                                                             | Readers                                                                                                       | Protection                                       | ESP32                                 | nRF52                     |
| -------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ | ------------------------------------- | ------------------------- |
| `receiveFlag`                                                                                                              | `esp32/esp32_main.cpp:461`                      | DIO1 ISR (`:494`, `:510`), `loopTask` (`:2219`, `:2176`, `:2304`)                                                                                                                                                                                                                                   | `loopTask` (`:2205`, `:2085`)                                                                                 | `std::atomic<bool>` seq_cst                      | **OK**                                | n/a                       |
| `transmittedFlag`                                                                                                          | `:469`                                          | DIO1 ISR (`:499`, `:515`), `loopTask` (`:2241`, `:2047`)                                                                                                                                                                                                                                            | `loopTask` (`:2205`, `:2235`)                                                                                 | atomic                                           | **OK**                                | n/a                       |
| `bEnableInterruptReceive` / `bEnableInterruptTransmit`                                                                     | `:462`, `:470`                                  | `loopTask` (`:2156-2157`, `:2218`, `:2238-2239`, `:2286-2287`, `:2296`)                                                                                                                                                                                                                             | DIO1 ISR (`:492`, `:497`, `:508`, `:513`)                                                                     | atomic per field                                 | **RACE** F2-7                         | n/a                       |
| `scanFlag`                                                                                                                 | `:473`                                          | **none**                                                                                                                                                                                                                                                                                            | **none**                                                                                                      | atomic                                           | **DEAD** F2-6                         | n/a                       |
| `transmissionState`                                                                                                        | `:458`                                          | `loopTask` (`doTX`)                                                                                                                                                                                                                                                                                 | `loopTask` (`:2246`, `:2250`)                                                                                 | `volatile int`                                   | OVER-SYNC                             | n/a                       |
| `iReceiveTimeOutTime`                                                                                                      | `:465`                                          | ESP32 `loopTask`; nRF52 radio ctx (`lora_functions.cpp:1293`) + loop                                                                                                                                                                                                                                | both                                                                                                          | none                                             | single                                | RACE (benign)             |
| `ringBuffer[MAX_RING][…]`                                                                                                  | `loop_functions.cpp:385`                        | ESP32 `loopTask` only (33 sites in `loop_functions.cpp`, 19 in `lora_functions.cpp`, 3 in `udp_functions.cpp`); nRF52 **radio ctx** (`lora_functions.cpp:262,1031,1065,1221,1979`) **+ loop** (`loop_functions.cpp:3129,3210,3463,3874,4012,4093,4171`, `udp_functions.cpp:350`, `nrf_eth.cpp:463`) | same                                                                                                          | **none**                                         | single                                | **RACE** F2-4             |
| `iWrite` / `iRead`                                                                                                         | `:386-387`                                      | as `ringBuffer` (`lora_functions.cpp:1637`, `:1643`, `:1539`, `:1756`, `:1797`, `:1865`)                                                                                                                                                                                                            | same                                                                                                          | `std::atomic<uint8_t>`                           | OVER-SYNC                             | **RACE** F2-4             |
| `ringPriority[]` / `ringEnqueueTime[]`                                                                                     | `:476-477`                                      | `addTxRingEntry` (`lora_functions.cpp:1547`)                                                                                                                                                                                                                                                        | `getNextTxSlot` (`:1493`), `doTX` (`:1651`)                                                                   | none                                             | single                                | **RACE** F2-4             |
| `retryCount[]`                                                                                                             | `:391`                                          | as `ringBuffer`                                                                                                                                                                                                                                                                                     | `updateRetransmissionStatus` (`:1913`)                                                                        | none                                             | single                                | **RACE** F2-4             |
| `ringBufferLoraRX[]` + `loraWrite`                                                                                         | `:394-395`                                      | `addLoraRxBuffer` (`:640-656`) — nRF52 radio ctx + loop                                                                                                                                                                                                                                             | `is_new_packet` (`lora_functions.cpp:1382`)                                                                   | `loraWrite` atomic; slot bytes unprotected       | OVER-SYNC                             | **RACE** (small)          |
| `is_receiving`                                                                                                             | `:423`                                          | ESP32 `loopTask` (`esp32_main.cpp:2363`, `:3771`, `:3928`); nRF52 radio ctx (`lora_functions.cpp:395,1306,2090`)                                                                                                                                                                                    | ESP32 loop (`:3230`, `:3352`, `:3768`); nRF52 loop (`nrf52_main.cpp:1308`, `:1333`)                           | atomic                                           | **OVER-SYNC** (+ TOCTOU shape, F2-17) | OK-ish (TOCTOU)           |
| `tx_is_active`                                                                                                             | `:424`                                          | `doTX` (`lora_functions.cpp:1729,1834`), `OnTxDone` (`:2045`), `OnTxTimeout` (`:2074`)                                                                                                                                                                                                              | loop                                                                                                          | atomic                                           | OVER-SYNC                             | OK                        |
| `cad_in_progress`, `cad_done_flag`, `cad_double_check`, `cad_channel_busy`                                                 | `nrf52/nrf52_main.cpp:238-241`                  | `OnCadDone` (`:394-397`), loop (`:1359-1375`, `:1412-1417`, `:1448-1451`)                                                                                                                                                                                                                           | loop snapshot (`:1341-1346`)                                                                                  | atomics **+** symmetric `taskENTER_CRITICAL`     | not compiled                          | **OK** — the template     |
| `cad_attempt`, `csma_timeout`, `rx_irq_defer_count`                                                                        | `:426-428`                                      | nRF52 radio ctx (`lora_functions.cpp:402,1294`), `csma_reset` (`:2136-2137`), loop (`nrf52_main.cpp:1310,1319,1426-1427`)                                                                                                                                                                           | both                                                                                                          | none                                             | single                                | RACE (benign) F2-15       |
| `ch_util_rx_start`                                                                                                         | `:431`                                          | `OnHeaderDetect` (`lora_functions.cpp:2091`) — nRF52 only; **never on ESP32**, the source says so at `esp32_main.cpp:3837`                                                                                                                                                                          | `OnRxDone` (`:294`), `OnRxTimeout` (`:1329`), `OnRxError` (`:1372`)                                           | atomic                                           | **DEAD**                              | OK                        |
| `ch_util_tx_start`                                                                                                         | `:432`                                          | ESP32 loop (`esp32_main.cpp:2046`, `:2435`); nRF52 loop                                                                                                                                                                                                                                             | loop, `OnTxDone` (`:2008`), `OnTxTimeout` (`:2053`)                                                           | atomic                                           | OVER-SYNC                             | OK                        |
| `ch_util_rx_accum` / `ch_util_tx_accum`                                                                                    | `:433-434`                                      | `fetch_add` from `checkRX` (`esp32_main.cpp:3838,3870`) / radio ctx (`lora_functions.cpp:296,301,1331,1374,2010,2055`)                                                                                                                                                                              | `.exchange(0)` (`esp32_main.cpp:1977-1978`, `nrf52_main.cpp:1284-1285`)                                       | atomic RMW                                       | OVER-SYNC                             | **OK** (correct RMW)      |
| `pendingDisplayMsg` (7× `String`), `pendingDisplayRssi/Snr`                                                                | `lora_functions.cpp:116-118`                    | `queueDisplayText` (`:126`), `queueDisplayPosition` (`:145`) — ESP32 loop, nRF52 radio ctx                                                                                                                                                                                                          | `esp32_main.cpp:1949-1965`; `nrf52_main.cpp:1258-1274`                                                        | `portMUX_TYPE displayMux` / `taskENTER_CRITICAL` | **OVER-SYNC + harmful** F2-2          | **OK but harmful** F2-1   |
| `bPendingDisplayText` / `bPendingDisplayPos`                                                                               | `lora_functions.cpp:109-110`                    | as above                                                                                                                                                                                                                                                                                            | as above                                                                                                      | `volatile` + the lock above                      | OVER-SYNC                             | OK                        |
| `bSetLoRaAPRS`                                                                                                             | `loop_functions.cpp:88`                         | ESP32 loop; nRF52 `doTX` (loop) + `OnTxDone`/`OnTxTimeout` (radio ctx)                                                                                                                                                                                                                              | both                                                                                                          | `volatile`                                       | OVER-SYNC                             | RACE (benign, no tearing) |
| `bSPI_ETH_Active`, `bPendingRadioRx`                                                                                       | `lora_functions.cpp:114-115`                    | loop (`nrf52_main.cpp:1882,1894,2279,2282,2304-2324`)                                                                                                                                                                                                                                               | radio ctx (`lora_functions.cpp:341-344,2022,2029-2032`)                                                       | `volatile` only                                  | never set                             | **RACE** (advisory) F2-19 |
| `RcvBuffer[UDP_TX_BUF_SIZE*2]`                                                                                             | `loop_functions_extern.h:149`                   | `OnRxDone`: `memcpy(RcvBuffer, payload, size)` (`lora_functions.cpp:407`), `encodeAPRS(RcvBuffer, …)` (`:1216`)                                                                                                                                                                                     | `OnRxDone` (`:455`, `:673`, `:687`, `:873`, `:954`, `:1093`), `addNodeData` (`:1144`), `queueExtern` (`:701`) | none                                             | single                                | **RACE** F2-21 (new)      |
| `rxBufInUse[2]`, `rxBufIndex`, `rxPayloadCopy[2][]`                                                                        | `lora_functions.cpp:310-311` (function statics) | `OnRxDone` (`:318-323`, `:392`, `:1298`)                                                                                                                                                                                                                                                            | `OnRxDone`                                                                                                    | `taskENTER_CRITICAL` on release only             | not compiled                          | **RACE** F2-21 (new)      |
| `lora_tx_buffer`                                                                                                           | `lora_functions.cpp:97`                         | `doTX` (loop)                                                                                                                                                                                                                                                                                       | `doTX`, `Radio.Send`                                                                                          | none                                             | single                                | single                    |
| `onrxdone_max_ms`, `onrxdone_warn_count`                                                                                   | `lora_functions.cpp:105-106`                    | radio ctx (`:1281`, `:1284`)                                                                                                                                                                                                                                                                        | loop reset (`nrf52_main.cpp:1292-1294`, `esp32_main.cpp:1985-1987`)                                           | none                                             | single                                | RACE (stats) F2-15        |
| `stat_tx_count[]`, `stat_drop_count[]`, `stat_latency_*`, `stat_queue_hwm`, `stat_preempt_count`, `stat_csma_hwm_attempts` | `loop_functions_extern.h:238-244`               | `addTxRingEntry`/`doTX`/`csma_reset` (`lora_functions.cpp:1572-1573,1611,1626,1664-1672,2133-2134`)                                                                                                                                                                                                 | loop print block                                                                                              | none                                             | single                                | RACE (stats) F2-15        |
| `bLED_RED/BLUE/GREEN/ORANGE`                                                                                               | `loop_functions.cpp:71-74`                      | `OnRxDone` (`lora_functions.cpp:386`), `doTX` (`:1760,1801,1869`), `sendToPhone` (`phone_commands.cpp:89,157`)                                                                                                                                                                                      | loop LED block (`esp32_main.cpp:1780-1821`)                                                                   | none                                             | single                                | RACE (cosmetic)           |

### 3.2 BLE / phone

| Object                                                     | Declaration                                                 | Writers                                                                                                                                                                                                          | Readers                                                                                         | Protection                              | ESP32                                         | nRF52                                        |
| ---------------------------------------------------------- | ----------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | --------------------------------------- | --------------------------------------------- | -------------------------------------------- |
| `bleQueue` (5 × `BleQueueItem`)                            | `esp32/esp32_main.cpp:272,277`; created `:1586`             | NimBLE task core 0 (`:359`)                                                                                                                                                                                      | `loopTask` core 1 (`:2815`)                                                                     | FreeRTOS queue, non-blocking both sides | **OK** — the one correct cross-core primitive | n/a                                          |
| `deviceConnected`                                          | `:282`                                                      | NimBLE core 0 (`:305`, `:316`)                                                                                                                                                                                   | `loopTask` core 1 (`:2768`, `:2781`, `:2802`)                                                   | none, plain `bool`                      | **RACE (cross-core)** F2-8                    | n/a                                          |
| `g_ble_conn_handle`                                        | `:284`                                                      | NimBLE core 0 (`:308`)                                                                                                                                                                                           | `loopTask` core 1 (`:2776` → `pServer->disconnect()`)                                           | none, plain `uint16_t`                  | **RACE (cross-core)** F2-8                    | n/a                                          |
| `config_to_phone_prepare`, `conffin_sent`                  | `:289-290`                                                  | ESP32 NimBLE core 0 (`:306-307`), `loopTask` (`:2792-2793`, `:2849`, `:2886`); nRF52 Callback task (`nrf52_ble.cpp:215-216`, `:232-233`)                                                                         | `loopTask` (`:2836`, `:2882`)                                                                   | none                                    | **RACE (cross-core)** F2-8                    | **RACE**                                     |
| `oldDeviceConnected`                                       | `:283`                                                      | `loopTask` only (`:2788`, `:2804`)                                                                                                                                                                               | `loopTask`                                                                                      | none                                    | single                                        | n/a                                          |
| `g_ble_uart_is_connected`                                  | `:530`                                                      | ESP32 loop (`:2770`, `:2790`); nRF52 Callback task (`nrf52_ble.cpp:217`, `:230`)                                                                                                                                 | `sendToPhone`, `addBLEComToOutBuffer`                                                           | none                                    | single                                        | RACE                                         |
| `BLEtoPhoneBuff[]`, `toPhoneWrite`, `toPhoneRead`          | `loop_functions.cpp:411-413`                                | `addBLEOutBuffer` (`:527-569`) — ESP32 loop; **nRF52 radio ctx** (`lora_functions.cpp:238,659,831,861,873,950,954,999,1093`) **+ loop** (`udp_functions.cpp:286,315,363`)                                        | `sendToPhone` (`phone_commands.cpp:50`) — loop                                                  | none, plain `int`                       | single                                        | **RACE** F2-5 (= CONC-15, **NOT FIXED**)     |
| `ringBufferUDPout[]`, `udpWrite`, `udpRead`                | `loop_functions.cpp:406-408`                                | `addNodeData` (`udp_functions.cpp:1081`) from `OnRxDone` (`lora_functions.cpp:1144`) — nRF52 radio ctx                                                                                                           | `sendUDP` (`nrf52_main.cpp:2625`) — loop                                                        | none, plain `int`                       | single                                        | **RACE** F2-5 (= CONC-16, **NOT FIXED**)     |
| `BLEComToPhoneBuff[]`, `ComToPhoneWrite`, `ComToPhoneRead` | `loop_functions.cpp:416-418`                                | `addBLEComToOutBuffer` (`:576-604`), reached only from `commandAction` (`command_functions.cpp:4596`…`:5449`)                                                                                                    | `sendComToPhone` — loop                                                                         | none                                    | single                                        | **single** — see §8, refutes the raw report  |
| `textbuff_phone`, `txt_msg_len_phone`                      | `phone_commands.cpp:22-23`                                  | `readPhoneCommand` (`:526`, `:541-542`) — ESP32 via `bleQueue` on `loopTask`; **nRF52 Callback task** (`nrf52_ble.cpp:254`)                                                                                      | `loopTask` (`nrf52_main.cpp:1522`)                                                              | none                                    | single                                        | **RACE** F2-11 (= CONC-14)                   |
| `hasMsgFromPhone`                                          | `loop_functions.cpp:420`                                    | `readPhoneCommand` (`:545`), loop clear (`nrf52_main.cpp:1524`)                                                                                                                                                  | `loopTask` (`nrf52_main.cpp:1516`)                                                              | none                                    | single                                        | **RACE** F2-11                               |
| `meshcom_settings` (~400 B, `char[]`, `double`, `float`)   | `nrf52/WisBlock-API.h:174,389`; `esp32/esp32_flash.h:8,243` | ESP32 loop only; **nRF52 Callback task**: `settings_rx_callback` `memcpy` (`nrf52_ble.cpp:319`) + `save_settings()` (`:322`), and `readPhoneCommand` field writes (`phone_commands.cpp:412,414,444,449,498,587`) | **everything**, incl. radio ctx (`encodeAPRS`, `checkMesh`, `lora_functions.cpp:1098`) and loop | none                                    | single                                        | **RACE (torn multi-word)** F2-12 (= CONC-17) |
| `isPhoneReady`                                             | `loop_functions.cpp:436`                                    | nRF52 Callback task (`nrf52_ble.cpp:214`, `:231`), loop                                                                                                                                                          | `OnRxDone`, loop (`nrf52_main.cpp:1669`…)                                                       | none                                    | single                                        | RACE (benign `int`)                          |
| `ble_busy_flag`                                            | `phone_commands.cpp:34`                                     | `sendToPhone` (`:60`, `:111`), `sendComToPhone` (`:128`, `:179`) — **both loop-only on both MCUs**                                                                                                               | same                                                                                            | none — used as a lock                   | **OVER-SYNC**                                 | **OVER-SYNC** — see §8                       |
| `g_task_event_type`                                        | `nrf52/WisBlock-API.cpp:24` (`volatile uint16_t`)           | `\|=` from Callback task (`nrf52_ble.cpp:247`, `:342`) and timer task (`api_functions.cpp:285`)                                                                                                                  | **none anywhere in `src/`**                                                                     | `volatile` only                         | n/a                                           | **DEAD** — see §8, F2-9                      |
| `g_task_sem`                                               | `WisBlock-API.cpp:18`                                       | give from Callback task (`nrf52_ble.cpp:248`, `:344`), timer task (`api_functions.cpp:292`)                                                                                                                      | `api_wait_wake` (`:262`), `xSemaphoreTake(g_task_sem,10)` (`nrf52_main.cpp:857`)                | binary semaphore                        | n/a                                           | **OK** (but see F2-13)                       |

### 3.3 Net console / external UDP (ESP32-only features)

| Object                                           | Declaration               | Writers                                                    | Readers                                                                                                                       | Protection                                                                                      | Verdict                                                       |
| ------------------------------------------------ | ------------------------- | ---------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| `s_fd`                                           | `net_console.cpp:53`      | `authTask` core 1 (`:210-211`), `teardownClient` (`:113`)  | `MeshSerialClass::write` (`:102`, loop), `netConsoleRead` (`:402`), `netConsoleAvailable` (`:421`), `loopNetConsole` (`:345`) | mutex on writes; **`recv()` reads unguarded**                                                   | **RACE** F2-10                                                |
| `s_mutex`                                        | `:54`                     | `startNetConsole` (`:275`), **`stopNetConsole` (`:284`)**  | everything                                                                                                                    | —                                                                                               | **RACE** F2-10 (= CONC-19, **NOT FIXED**)                     |
| `s_authenticated`, `s_peek_valid`, `s_peek_byte` | `:57`, `:61`, `:62`       | `authTask` core 1, loop                                    | loop, `authTask`; `s_authenticated` read outside the lock at `:235`, `:249`, `:342`, `:388`                                   | partial mutex                                                                                   | RACE (mild)                                                   |
| `s_hs_running`, `s_server_pending`               | `:58`, `:56`              | loop + `authTask` (`:128`, `:197`, `:218`, `:376`, `:382`) | loop (`:308`, `:369`)                                                                                                         | `volatile`                                                                                      | RACE (mild); `s_hs_running` never cleared by `stopNetConsole` |
| `externQueue[2]`                                 | `extudp_functions.cpp:57` | `queueExtern` (`:593-607`) — nRF52 radio ctx, ESP32 loop   | `flushExternQueue` (`:610-618`) — loop                                                                                        | `used` is `std::atomic<bool>` with correct release/acquire, **but the producer never reads it** | **RACE** F2-14                                                |
| `externQueueWrite`                               | `:58`                     | producer only                                              | producer only                                                                                                                 | none                                                                                            | single                                                        |

### 3.4 Misc

| Object                                     | Declaration                 | Writers                                                                         | Readers                               | Protection                     | Verdict                                         |
| ------------------------------------------ | --------------------------- | ------------------------------------------------------------------------------- | ------------------------------------- | ------------------------------ | ----------------------------------------------- |
| `gKeyNum`                                  | `nrf52/nrf52_main.cpp:323`  | GPIO ISR `interruptHandle2` (`:340`), loop (`:1541`, `:1553`, `:1658`, `:1665`) | loop (`:1545`, `:1556`, `:1661`)      | **none — not even `volatile`** | **RACE** F2-16                                  |
| `pulseTimes[]`, `pulseIndex`, `lastMicros` | `gps_functions.cpp:172-174` | `handleRxInterrupt` ISR (`:182-190`)                                            | `detectBaudrate` (`:204`, `:210-214`) | `volatile`                     | **OK** (single core, boot only)                 |
| `displayMux`                               | `lora_functions.cpp:123`    | —                                                                               | —                                     | `portMUX_TYPE`                 | **OVER-SYNC** (ESP32)                           |
| `audioSemaphore`                           | `esp32/esp32_audio.cpp:26`  | binary semaphore used as mutex                                                  | audio task prio 50 core 1             | binary semaphore               | pre-existing exception (audit 20260626 item 24) |
| `xSemaphore` (T-Deck TFT)                  | `t-deck/tdeck_main.cpp:47`  | `portMAX_DELAY` take at `:401`                                                  | —                                     | binary semaphore as mutex      | pre-existing exception                          |

### 3.5 Counts

| Verdict                                         | Count | Notes                                                                                                                                                                                                                                                                                                                                                                                |
| ----------------------------------------------- | ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **RACE** — correctness-affecting                | 13    | see §4                                                                                                                                                                                                                                                                                                                                                                               |
| **RACE** — benign / observability only          | 9     | all nRF52-only: `iReceiveTimeOutTime`; `cad_attempt`/`csma_timeout`/`rx_irq_defer_count`; `bSetLoRaAPRS`; `onrxdone_*`; `stat_*`; `bLED_*`; `isPhoneReady`; `g_ble_uart_is_connected`; `ringBufferLoraRX`/`loraWrite` — **the last is "benign" only in the sense that a corrupted dedup entry causes a duplicate relay, not a malformed frame; it is the one I would promote first** |
| **OK** — concurrent and correctly protected     | 5     | `bleQueue`; nRF52 CAD flag set; `ch_util_*_accum` RMW on nRF52; `pulseTimes`/`pulseIndex`; `g_task_sem`                                                                                                                                                                                                                                                                              |
| **OVER-SYNC** — single-context but synchronised | 16    | the 14 LoRa-path objects of §5 (of which `scanFlag` and `ch_util_rx_start` are also DEAD, so they are counted in both rows), plus `ble_busy_flag` and the doubly-protected CAD store                                                                                                                                                                                                 |
| **DEAD** — no readers and/or no writers         | 3     | `scanFlag`, `ch_util_rx_start` (ESP32) — both also in the OVER-SYNC row — and `g_task_event_type` (nRF52), which is not synchronised at all                                                                                                                                                                                                                                          |
| **single** — correct as is                      | rest  | ~380 of the 423 externs on ESP32; far fewer on nRF52                                                                                                                                                                                                                                                                                                                                 |

---

## 4. Races

One block per genuine race, with the interleaving that produces a wrong result. F2-n IDs are kept so
the raw report stays traceable.

### F2-4 — nRF52 TX ring: multi-writer, no mutual exclusion — **High**

`src/loop_functions.cpp:385-387`, `:476-477`; enqueue sites listed in §3.1;
`src/lora_functions.cpp:1547` (`addTxRingEntry`), `:1529` (`advanceIReadPastEmpty`), `:1651` (`doTX`).
Cross-ref [08 N-14](08-defect-catalogue.md), audit item C1.

The enqueue is four separate loads of `iWrite`:

```c
ringBuffer[iWrite][0] = size;           // atomic load #1
ringBuffer[iWrite][1] = status;         // atomic load #2
memcpy(ringBuffer[iWrite]+2, …, size);  // atomic load #3
addTxRingEntry(...);                    // ringPriority[w], ringEnqueueTime[w], then iWrite = w+1
```

Making `iWrite`/`iRead` atomic (audit C1, `code-audit-fixes-20260627.md:33`) makes each load
tear-free. It does not stop two contexts from loading the **same** value.

**Interleaving (unconditional path — timer service task, priority 2).**

1. `loop` task (prio 1) is in `sendMessage()` (`loop_functions.cpp:3463`): it has executed
   `ringBuffer[iWrite][0]=aprsmsg.msg_len;` with `iWrite == 5` and is inside the `memcpy` at `:3464`.
2. The SX126x `RxTimeoutTimer` expires. The **FreeRTOS timer service task at priority 2** preempts
   the loop mid-`memcpy` — no yield needed, this is priority preemption.
3. `RadioOnRxTimeoutIrq` (`radio.cpp:1299`) calls `RadioBgIrqProcess()` (`:1310`). `IrqFired` is
   `true` — a DIO1 edge arrived, but the `LORA` task is priority 1 and never got the CPU because it
   does not preempt the equal-priority loop. `IRQ_RX_DONE` is set, so `RadioEvents->RxDone()` →
   `OnRxDone` runs **inside the timer task**.
4. `OnRxDone` decides to relay: `lora_functions.cpp:1221` `memset(ringBuffer[iWrite],0,…)` — also
   slot 5 — writes the relay frame, then `addTxRingEntry("rx_relay")` (`:1238`) sets
   `ringPriority[5]`, `ringEnqueueTime[5]` and advances `iWrite` to 6.
5. The timer task returns. `loop` resumes and finishes its `memcpy` **into slot 5**, splicing the
   tail of the user message over the relay frame, then `addTxRingEntry("user_msg")` (`:3486`)
   recomputes `ringPriority[6]` from `ringBuffer[6]` — an unrelated, stale slot — and advances
   `iWrite` to 7.

Result: slot 5 holds a relay header spliced with a user payload tail and a valid-looking length; it
goes on the air as a malformed APRS frame with a corrupted source path, which neighbours will relay
before it fails to decode. Slot 6 is enqueued with a priority derived from stale bytes.
`stat_queue_hwm` and the `queued` computation are both wrong.

**Second path (`LORA` task, priority 1).** Reachable only through a yield inside the sequence. Both
enqueue sequences contain one: `loop_functions.cpp:3482` (`printfdeb` under `bDisplayRetx`, between
the slot writes and `addTxRingEntry`) and `lora_functions.cpp:1232-1237` (`printfdeb` under
`bLORADEBUG`, same position). `Serial.printf` calls `yield()` when the TinyUSB CDC write FIFO is
full (`Adafruit_USBD_CDC.cpp:253`), and `yield()` is `taskYIELD()` (`rtos.cpp:75-82`), which
switches to an equal-priority ready task. So enabling LoRa debug output on a node whose USB host is
not draining makes this second path live.

`advanceIReadPastEmpty()` (`:1529-1541`) and the `iRead = iReadBeforeAdvance` rollbacks in `doTX`
(`:1756`, `:1797`, `:1865`) are plain load-modify-store on an atomic and can clobber a concurrent
advance.

**Fix.** One writer, or one lock — a single `enqueueTx(const uint8_t*, size_t, uint8_t)` helper
holding a FreeRTOS mutex (or `taskENTER_CRITICAL`; the body is pure `memcpy`, no allocation, no
radio API) across slot selection, fill and index advance. All call sites go through it. Do **not**
try to fix this with more atomics.

### F2-21 — nRF52: `OnRxDone` has two execution contexts, and they share the RX double buffer — **High**, new

`radio.cpp:1283-1312` (`RadioOnTxTimeoutIrq`, `RadioOnRxTimeoutIrq` → `RadioBgIrqProcess()`),
registered as `SoftwareTimer` callbacks at `:598-599`; `SoftwareTimer::begin` → `xTimerCreate`
(`cores/nRF5/utility/SoftwareTimer.cpp:39`); `lora_functions.cpp:310-323`, `:392`, `:1298`.

`RadioBgIrqProcess()` dispatches the entire `RadioEvents` set whenever `IrqFired` is set. It is
called from two places: the `LORA` task at priority 1 (`board.cpp:480-484`) and the SX126x timeout
callbacks at priority 2 (timer service task). The two are not mutually excluded, and priority 2
preempts priority 1.

**Consequences.**

1. `RcvBuffer`, `rxPayloadCopy[2]`, `rxBufIndex` and `rxBufInUse[2]` are not single-context. The
   double-buffer bookkeeping at `lora_functions.cpp:318-323` (`nextBuf`, `_overwrite`, index
   advance, `memcpy`) runs **outside** any critical section; only the _release_ at `:392` and
   `:1298` is guarded. The prio-2 timer task can preempt the prio-1 `LORA` task between `:319` and
   `:322` and take the same buffer.
2. `OnRxDone` — full APRS decode with Arduino `String`s, ring `memcpy`, display struct copy — can
   execute on the **timer service task's 1 KB stack** (`configTIMER_TASK_STACK_DEPTH 256`), versus
   the 16 KB the `LORA` task provides. This is a plausible stack-overflow path and it is not
   documented anywhere.
3. Every "the radio callback runs in the `LORA` task" statement elsewhere in the codebase and docs
   is incomplete.

The `_overwrite` flag computed at `:319` is the one signal that would detect (1) at runtime, and it
is discarded: its only consumer is behind `#ifdef LORA_ISR_DEBUG` (`:330`), a macro **defined
nowhere in the repo** — same as
[07 V-03](07-verification-infrastructure.md).

**Fix.** Either serialise `RadioBgIrqProcess()` with a mutex, or raise the `LORA` task to priority 3
so it strictly dominates the timer task and the SoftDevice callback task — but note that this makes
the F2-4 preemption unconditional rather than removing it, so it must be done **together with** the
enqueue lock. Independently: pull the double-buffer overwrite counter out from behind
`LORA_ISR_DEBUG` into an always-on `stat_` counter.

### F2-11 / F2-12 — nRF52: phone commands and settings applied in the Bluefruit callback task — **High**

`src/nrf52/nrf52_ble.cpp:243-254` (`bleuart_rx_callback` → `readPhoneCommand`), `:296-322`
(`settings_rx_callback`: `delay(1000)` at `:300`, `memcpy` at `:319`, `save_settings()` at `:322`);
the contradicted comment is `src/phone_commands.cpp:528-529`. Cross-ref
[08 N-15](08-defect-catalogue.md), `fable-verdict.md` CONC-14 / CONC-17.

Both handlers are dispatched through `ada_callback` (`BLECharacteristic.cpp:539`,
`BLEUart.cpp:104,118`) and therefore run in the Bluefruit `Callback` task at **priority 2**, which
preempts both the loop and the `LORA` task at arbitrary instructions.

The in-source comment that justified removing a spin-wait guard reads:

```c
// Spin-wait removed: readPhoneCommand now runs in Main Loop,
// no cross-core conflict with sendToPhone() possible
```

True on ESP32 (`bleQueue`, `esp32_main.cpp:359` → `:2815`). **False on nRF52.** A guard was removed
globally on a platform-specific premise.

**Interleaving A — text command (F2-11).** The phone sends a `0xA0` text command. The Callback task
enters `readPhoneCommand` case `0xA0` (`phone_commands.cpp:526-545`): it sets
`txt_msg_len_phone = msg_len - 2` at `:526`, then `memcpy(textbuff_phone+iposn, …)` at `:541`, then
`hasMsgFromPhone = true` at `:545`. If the loop task was already past `if(hasMsgFromPhone)`
(`nrf52_main.cpp:1516`) from a _previous_ message and re-enters between `:526` and `:541`, it runs
`commandAction(textbuff_phone, …)` (`:1522`) with a **new length against an old buffer**,
transmitting whatever bytes follow the shorter previous message.

**Interleaving B — settings (F2-12).** The phone pushes a settings blob changing the callsign from
`OE1ABC-12` to `OE1XYZ-7`. `settings_rx_callback` (prio 2) is inside
`memcpy((void*)&meshcom_settings, data, sizeof(s_meshcom_settings))` (`nrf52_ble.cpp:319`) — a plain
~400-byte byte copy over a live struct. It is preempted by nothing, but it _preempts_ the `LORA`
task, which is mid-relay and reads
`strcmp(destination_call, meshcom_settings.node_call)` (`lora_functions.cpp:1098`); on resume the
callsign reads `OE1XYZ-12`, which belongs to nobody. That frame is transmitted, relayed, and shows
up in every MHeard table on the mesh. The 8-byte `double node_lat` can be half-updated the same way,
producing a position on the wrong continent. `save_settings()` at `:322` then flashes whatever
partial state exists.

Additionally `delay(1000)` at `nrf52_ble.cpp:300` is `vTaskDelay(1000 ms)` inside a BLE write
callback: it blocks the shared Bluefruit `Callback` task for a full second, delaying every other
Bluefruit callback including `disconnect_callback`.

**Fix.** Port the ESP32 pattern: a small `xQueueSend` from `bleuart_rx_callback` and
`settings_rx_callback`, drained in `nrf52loop()`. This is the CONC-14 root fix; it collapses F2-5,
F2-11 and F2-12. Remove the `delay(1000)`.

### F2-5 — nRF52: `toPhoneWrite/Read` and `udpWrite/Read` are plain `int` across contexts — **Med-High**

`src/loop_functions.cpp:407-408` (`udpWrite`, `udpRead`), `:412-413` (`toPhoneWrite`, `toPhoneRead`);
declared `src/loop_functions_extern.h:191`, `:198`. The ring-pointer helper
`src/loop_functions.cpp:4972` still takes `volatile int&`. Cross-ref `fable-verdict.md` CONC-15 /
CONC-16 — **confirmed still open**.

Producers run in the radio context (`addBLEOutBuffer` from `OnRxDone`, nine sites in
`lora_functions.cpp`; `addNodeData` from `OnRxDone` at `lora_functions.cpp:1144`); consumers
(`sendToPhone`, `sendUDP`) run in the loop. Same shape as F2-4 — the index advance
(`loop_functions.cpp:566-569`) is a non-atomic RMW and the slot fill is unguarded — but with a
smaller blast radius, since a corrupted slot goes to the phone or the server rather than on the air.

Note the asymmetry the verdict does not state: **on ESP32 all of these are single-context**, because
the NimBLE task's only reach into shared state is `bleQueue`. This whole finding class is
nRF52-only, which confirms the verdict's sprint judgement that fixing CONC-14 resolves 15/16/17/18.

### F2-8 — ESP32: NimBLE connection state crosses core 0 → core 1 unsynchronised — **Med-High**

`src/esp32/esp32_main.cpp:282-290` (definitions), `:305-316` (writers, NimBLE host task, **core 0**),
`:2768-2793`, `:2802-2886` (readers, `loopTask`, **core 1**).

`CONFIG_BT_NIMBLE_PINNED_TO_CORE` is 0 (`NimBLE-Arduino/src/nimconfig.h:196`) and
`ARDUINO_RUNNING_CORE` is 1, so `onConnect`/`onDisconnect` genuinely execute on the other core. The
four affected objects are plain, non-`volatile`, non-atomic. The project got the _data_ path right
(`bleQueue`) and left the _control_ path unsynchronised.

**Interleaving A — stale handle, wrong link disconnected.** Phone A disconnects and phone B connects
in quick succession. Core 0 runs `onDisconnect` (`deviceConnected=false`, `:316`) then `onConnect`
(`deviceConnected=true`, `g_ble_conn_handle=H_B`, `:305-308`). Core 1's `loopTask` is inside the
`if (deviceConnected)` block at `:2768` with a register-cached `g_ble_conn_handle == H_A` — nothing
forces a reload; the variable is neither `volatile` nor atomic and `pServer->disconnect()` cannot
alias it as far as the optimiser can prove under `-Oz`. `ble_disconnect_requested` then causes
`pServer->disconnect(H_A)` at `:2776` — a stale handle. Either a no-op (an auth-failed peer stays
connected: a security regression) or, on handle reuse, a disconnect of the newly authenticated peer.

**Interleaving B — config storm sent twice or never.** `onConnect` sets
`config_to_phone_prepare = false` on core 0 (`:306`) at the same moment `loopTask` executes
`config_to_phone_prepare = false` at `:2849` after having read `true` at `:2836`. Combined with
`conffin_sent` (`:2882-2886`), the phone can receive `--conffin` before the ten `config_cmds` are
queued, or never receive it.

**Fix.** `std::atomic<bool>` / `std::atomic<uint16_t>` for the four fields is sufficient and cheap —
they are single-word. Better: extend `bleQueue` to carry connect/disconnect events so the loop owns
all BLE state transitions, which makes the ESP32 side fully single-context.

### F2-7 — ESP32: `(bEnableInterruptReceive, receiveFlag)` is atomic per field, not as a pair — **Med-High**

`src/esp32/esp32_main.cpp:490-518` (both ISRs), `:2156-2176`, `:2218-2219`, `:2238-2241`,
`:2286-2304`.

ISR: `if(bEnableInterruptReceive) receiveFlag = true;`
Loop: `bEnableInterruptReceive = false; receiveFlag = false; … reconfigure radio … bEnableInterruptReceive = true;`

**Interleaving A — spurious flag.** The ISR evaluates `bEnableInterruptReceive` → true. The loop then
executes both stores (`:2218-2219`). The ISR resumes and stores `receiveFlag = true`. The loop
enters `checkRX()` on the next iteration with no packet in the FIFO; `radio.readData()` returns an
error and the code takes the `RX_OTHER_ERROR` path, restarting RX unnecessarily. Observable as
`[MC-DBG] RX_OTHER_ERROR code=…` bursts.

**Interleaving B — lost edge.** The loop clears the gate at `:2156` before `radio.startReceive()`;
DIO1 rises during reconfiguration; the ISR sees the gate closed and drops the edge. Partially
mitigated by the deliberate `digitalRead(LORA_DIO1) == HIGH` recovery at `:2170-2176` and
`:2296-2304` — good defensive engineering, worth keeping — but the recovery only covers
level-still-high, not a pulse that has already fallen.

Because the ISR and the loop are both on core 1, this is a preemption race, not a memory-ordering
race: `std::atomic` seq_cst is more than is needed and does not help.

**Fix.** One `std::atomic<uint8_t> rx_state` with `compare_exchange_strong` for the gate
transitions; or drop the gate and let the loop's state machine decide whether a set `receiveFlag` is
meaningful — the DIO1 level check already gives the ground truth.

### F2-14 — `queueExtern` overwrites a slot whose `used` flag is still set — **Med**

`src/extudp_functions.cpp:593-607` (producer), `:610-618` (consumer), `:48-58` (struct,
`MAX_EXTERN_QUEUE == 2`). Cross-ref `code-audit-20260626.md` RACE-01.

That audit hardened `used` to `std::atomic<bool>` with release/acquire, which is correct as far as it
goes — but **the producer never inspects it**:

```c
struct externQueueEntry *entry = &externQueue[externQueueWrite];
… memcpy(entry->buffer, buffer, buflen); …
entry->used.store(true, std::memory_order_release);
externQueueWrite = (externQueueWrite + 1) % MAX_EXTERN_QUEUE;   // 2 slots
```

**Interleaving.** `flushExternQueue` is at `:616` inside
`sendExtern(…, externQueue[0].buffer, externQueue[0].buflen, …)`, blocked in `UdpExtern.write()`.
Two packets arrive; the producer runs twice (radio ctx, `lora_functions.cpp:701`), filling slot 1
then slot 0. The consumer's in-flight `sendExtern` reads `externQueue[0].buffer` mid-`memcpy` → the
JSON posted to the external server is the head of packet A spliced with the tail of packet C, with
`buflen` from A. Then `used.store(false)` at `:618` clears the flag the producer just set → packet C
is silently dropped.

**Fix.** Producer checks `if(entry->used.load(acquire)) { drop; return; }` before touching the slot,
and the consumer copies the entry out (or clears `used` only for the index it snapshotted). Better:
`xQueueSend`/`xQueueReceive`.

### F2-10 — `net_console.cpp`: mutex recreated without ownership, `recv()` outside it — **Med**

`src/net_console.cpp:284` (`s_mutex = xSemaphoreCreateMutex();` inside `stopNetConsole`), `:291`
(`teardownClient()` called without holding the mutex), `:109-118` (asymmetric protocol — the
function _gives_ a mutex its caller took), `:288-289` (missing braces:
`if(s_listen_fd >= 0) ::close(s_listen_fd); s_listen_fd = -1;`), `:345`, `:402`, `:421`
(`::recv(s_fd, …)` with no lock), `:378` (`authTask` pinned to core 1, prio 1).
Cross-ref `fable-verdict.md` CONC-19 — **confirmed unchanged**.

Beyond the verdict's description:

- **Unlocked `recv` on a mutable fd.** `netConsoleRead()` (`:402`) and `netConsoleAvailable()`
  (`:421`) call `::recv(s_fd, …)` with no mutex, while `authTask` — same core, same priority — can
  execute `if (s_fd >= 0) ::close(s_fd); s_fd = fd;` (`:210-211`). lwIP reuses small fd numbers
  aggressively; a console read can land on a freshly-opened UDP mesh socket.
- **`s_hs_running` is never cleared by `stopNetConsole`** (`:280-294`), so after
  `--extser off` / `--extser on` during a handshake, every subsequent connection is rejected at
  `:369` with "auth already in progress" until reboot.

**Fix (minimal).** In `stopNetConsole`: take `s_mutex`, tear down, give it, and **do not** recreate
it. Make `teardownClient` symmetric (caller takes, caller gives). Guard the `recv` calls with the
same mutex (`xSemaphoreTake(…, 0)` is fine — a missed poll is harmless). Add `s_hs_running = false`.

### F2-16 — `gKeyNum` written by a GPIO ISR, read and cleared by the loop, no `volatile` — **Low-Med**

`src/nrf52/nrf52_main.cpp:323` (`uint8_t gKeyNum = 0;`), `:336-343` (`interruptHandle2`), `:1036`
(`attachInterrupt(MIDDLE_BUTTON, interruptHandle2, FALLING)`), `:1541`, `:1545`, `:1553`, `:1556`,
`:1658`, `:1661`, `:1665` (loop reads and writes).

No `volatile`, no atomic. The loop does `if(gKeyNum == 2) { … gKeyNum = 0; }`. Under `-Oz` a
compiler may keep `gKeyNum` in a register across the surrounding straight-line code and never
observe the ISR's store — a button press that is silently ignored. The ISR body is itself a RMW
(`if(gKeyNum == 0) gKeyNum = 1;`), but only one of the three handlers is attached (`:1032` and
`:1040` are commented out), so there is no ISR-vs-ISR race.

`code-audit-fixes-20260627.md:31` records B4 as "`pulseTimes` ISR race — already `volatile`, no
change". Correct for `gps_functions.cpp`; `gKeyNum` is the same pattern and was never examined.

**Fix.** `volatile uint8_t gKeyNum` (single core: sufficient), or an atomic exchange.

### F2-19 — nRF52: the `bSPI_ETH_Active` SPI-bus guard is advisory — **Low-Med**

`src/lora_functions.cpp:114-115`, `:341-345`, `:2022`, `:2029-2032`;
`src/nrf52/nrf52_main.cpp:1882`, `:1894-1895`, `:2279-2283`, `:2304-2324`.

A `volatile bool` set by the loop around W5100S transactions and tested by the radio context before
`startRadioReceive()`. Between the test at `lora_functions.cpp:341` and the SPI transaction inside
`startRadioReceive()` there is no exclusion — a TOCTOU on a shared SPI bus (both chips are on the
single RAK4631 SPI bus, pins 3/29/30).

The raw report argued this "happens to work because the `LORA` task has higher priority than the
loop". **That argument is void** — the `LORA` task is priority 1, the same as the loop (§1.2), and
the _timer_ task at priority 2 can preempt the loop into the middle of an Ethernet transaction
while running `startRadioReceive()` from `OnRxTimeout`. The guard is therefore weaker than the
report assumed, not stronger.

Also `if(bPendingRadioRx) { bPendingRadioRx = false; startRadioReceive(); }`
(`nrf52_main.cpp:1895` and three siblings) is a test-and-clear RMW, and that `startRadioReceive()`
is one of the very few **not** wrapped in `taskENTER_CRITICAL` (contrast `:1321`, `:1436`, `:1452`).

**Fix.** A single `SemaphoreHandle_t spiBusMutex` taken by both the Ethernet block and every
radio-touching helper, replacing both `bSPI_ETH_Active` and the `taskENTER_CRITICAL` wrappers of
F2-3. Document the actual task-priority relationships in `src/nrf52/nrf52_radio.h` either way.

### F2-20 — T5-ePaper / T-Deck-Pro: unpinned tasks at priority 20–24 touch shared LoRa state — **Med for those boards**

`src/t5-epaper/peri_lora.cpp:171`, `:176` (`lora_task`, `LORA_PRIORITY = configMAX_PRIORITIES-2` =
23; `src/t5-epaper/peripheral.h:7`), `:220` (`checkRX(true)`);
`src/t5-epaper/peri_gps.cpp:77` / `src/t-deck-pro/peri_gps.cpp:78` (`gps_task`, prio 24);
`src/t-deck-pro/tdeck_pro.cpp:381`; `src/t5-epaper/t5epaper_main.cpp:671`.

These use `xTaskCreate`, not `…PinnedToCore`, so IDF schedules them on **either** core. `lora_task`
calls `checkRX(true)` → `OnRxDone` → the entire shared-state pipeline (`ringBuffer`,
`BLEtoPhoneBuff`, `RcvBuffer`, `pendingDisplayMsg`) from an unpinned task at priority 23,
concurrently with `loopTask` on core 1 at priority 1.

**For those two variants every "ESP32 = single-context" conclusion in this document is void**, and
the full nRF52 finding set (F2-4, F2-5, F2-14, F2-15) applies with the additional hazard of true
cross-core parallelism — `taskENTER_CRITICAL()` would not help even if it were used, and the
`double`s in `gpsData` / `posinfo_lat` / `posinfo_lon` can genuinely tear.
`src/code_review/code-audit-20260508.md:220` flags the `gps_task`/`double` case; the `lora_task`
case is recorded nowhere.

**Fix.** At minimum pin these to core 1 (`xTaskCreatePinnedToCore(…, 1)`) so they share a core with
`loopTask` and the ESP32 analysis holds. Properly: route them through queues like `bleQueue`.

### F2-1 / F2-3 — not races, but concurrency defects worth keeping in the same list

- **F2-1 / F2-2 — seven heap-allocating `String` copies inside a critical section.**
  `struct aprsMessage` (`src/aprs_structures.h:9-34`) has seven Arduino `String` members. The
  assignments `pendingDisplayMsg = aprsmsg;` (`lora_functions.cpp:133`, `:152`) and
  `_msg = pendingDisplayMsg;` (`nrf52_main.cpp:1266`) each expand to seven `String::operator=`,
  every one of which can `realloc`. On nRF52 this runs under `taskENTER_CRITICAL()` (BASEPRI raised
  to `configMAX_SYSCALL_INTERRUPT_PRIORITY`), blocking SysTick and SoftDevice application-priority
  interrupts for the whole allocation. On ESP32 it runs under `portENTER_CRITICAL(&displayMux)`,
  which disables interrupts on core 1 and then acquires the ESP-IDF heap spinlock that **core 0**
  may hold — the canonical `INT_WDT` trigger. See §5: on ESP32 the lock protects nothing at all.
- **F2-3 — `Radio.Send()` / `Radio.StartCad()` / `startRadioReceive()` inside
  `taskENTER_CRITICAL()`** (`lora_functions.cpp:1737-1739`, `:1778-1780`, `:1839-1841`;
  `nrf52_main.cpp:1321-1323`, `:1365-1368`, `:1400-1402`, `:1418-1421`, `:1436-1438`, `:1448-1454`).
  Every SX126x command goes through `SX126xCheckDeviceReady()` → `SX126xWaitOnBusy()`, whose loop
  body is `delay(1)` = `vTaskDelay()` (`cores/nRF5/delay.c:33-48`). Calling `vTaskDelay` with BASEPRI
  masked removes the running task from the ready list and requests a PendSV that cannot fire, while
  SysTick is masked so the tick never advances. `TimerStart` → `xTimerStart` also posts to the timer
  command queue from inside the masked window. A 200-byte SPI payload write at 2 MHz is ≈1 ms of
  interrupts fully disabled. Cross-ref [08 N-16](08-defect-catalogue.md).
  **Under no circumstance call a radio driver API inside `taskENTER_CRITICAL()`.**

---

## 5. Over-synchronisation — what can be removed

Everything below is **single-context on ESP32** and pays for synchronisation it does not need. The
argument is uniform and mechanical, and rests on §2: the only ESP32 ISR is
`setFlagReceive`/`setFlagSent` (`esp32_main.cpp:490`, `:506`), whose entire body touches four
atomics; `OnRxDone` is called only from `checkRX()` (`:3840`) and `OnTxDone` only from `esp32loop()`
(`:2283`), both `loopTask`; the NimBLE task's only reach into shared state is
`xQueueSend(bleQueue, …)` (`:359`). Therefore anything not named
`receiveFlag`/`transmittedFlag`/`bEnableInterrupt*` and not written by NimBLE is `loopTask`-only.

Removals are `#if !defined(BOARD_RAK4630)`-shaped, or can be handled with a type alias
(`mc_atomic<T>` = `std::atomic<T>` on nRF52, plain `T` on ESP32) so the nRF52 build is unaffected.
**Note the T5-ePaper / T-Deck-Pro exception (F2-20): those boards are ESP32 but not single-context.
Any alias must key off the board, not off `ESP32`.**

| Object                                                                             | Declaration                                                                          | Why it is single-context on ESP32                                                                                                                                                                                                          | Action                                                                                                     |
| ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------- |
| `std::atomic<bool> scanFlag`                                                       | `esp32_main.cpp:473`                                                                 | **Zero readers, zero writers** anywhere in the tree — the only other occurrence is a 2026-05-08 audit note. ESP32 CAD uses the blocking `radio.scanChannel()`.                                                                             | **Delete.** Correct the audit trail (`code-audit-fixes-20260627.md:29`).                                   |
| `std::atomic<unsigned long> ch_util_rx_start`                                      | `loop_functions.cpp:431`                                                             | **Never written on ESP32** — only writer is `OnHeaderDetect` (`lora_functions.cpp:2091`), which is wired only on nRF52 (`nrf52_main.cpp:949`); the source says so at `esp32_main.cpp:3837`. The three `.exchange(0)` reads always yield 0. | **Remove from the ESP32 build.**                                                                           |
| `volatile uint16_t g_task_event_type`                                              | `nrf52/WisBlock-API.cpp:24`                                                          | nRF52 only, and **write-only**: three `\|=` writers, no reader anywhere in `src/`.                                                                                                                                                         | **Delete**, or give it the consumer it was designed to have.                                               |
| `std::atomic<bool> is_receiving`                                                   | `loop_functions.cpp:423`                                                             | writers `esp32_main.cpp:2363`, `:3771`, `:3928`; readers `:3230`, `:3352`, `:3768` — all `loopTask`                                                                                                                                        | plain `bool` on ESP32                                                                                      |
| `std::atomic<bool> tx_is_active`                                                   | `:424`                                                                               | written in `doTX` and `OnTxDone`, both `loopTask`                                                                                                                                                                                          | plain `bool` on ESP32                                                                                      |
| `std::atomic<unsigned long> ch_util_tx_start`                                      | `:432`                                                                               | written `esp32_main.cpp:2046`, `:2435`; read `:2039` — all `loopTask`                                                                                                                                                                      | plain                                                                                                      |
| `std::atomic<unsigned long> ch_util_rx_accum` / `ch_util_tx_accum`                 | `:433-434`                                                                           | `fetch_add` from `checkRX`/`OnRxDone` (loop), `exchange` from `esp32loop` `:1977-1978` (loop)                                                                                                                                              | plain                                                                                                      |
| `std::atomic<uint8_t> iWrite` / `iRead`                                            | `:386-387`                                                                           | all enqueue sites and `doTX` are `loopTask` on ESP32                                                                                                                                                                                       | plain on ESP32; **keep on nRF52 and add a real lock there** (F2-4)                                         |
| `std::atomic<uint8_t> loraWrite`                                                   | `:395`                                                                               | `addLoraRxBuffer` reached only from `OnRxDone` / the GW-ACK path = `loopTask`                                                                                                                                                              | plain on ESP32                                                                                             |
| `portMUX_TYPE displayMux` + both critical sections                                 | `lora_functions.cpp:123`, `:129`, `:138`, `:147`, `:156`; `esp32_main.cpp:1949-1965` | producer (`queueDisplayText` ← `OnRxDone` ← `checkRX` ← `esp32loop`) and consumer are both `loopTask`                                                                                                                                      | **Delete on ESP32** — and it is actively harmful (F2-2)                                                    |
| `volatile bool bPendingDisplayText` / `bPendingDisplayPos`                         | `lora_functions.cpp:109-110`                                                         | set in `queueDisplay*` (loop), cleared in `esp32loop` (loop)                                                                                                                                                                               | plain `bool` on ESP32                                                                                      |
| `volatile int transmissionState`                                                   | `esp32_main.cpp:458`                                                                 | written by `doTX` (loop), read at `:2246`, `:2250` (loop). The ISR never touches it.                                                                                                                                                       | plain `int` on ESP32                                                                                       |
| `volatile bool bSetLoRaAPRS`                                                       | `loop_functions.cpp:88`                                                              | all ESP32 accesses are `loopTask`                                                                                                                                                                                                          | plain on ESP32; keep `volatile` on nRF52                                                                   |
| `volatile bool bSPI_ETH_Active` / `bPendingRadioRx`                                | `lora_functions.cpp:114-115`                                                         | nRF52-only feature (W5100S); ESP32 never sets them                                                                                                                                                                                         | `#if defined(BOARD_RAK4630)`                                                                               |
| `bool ble_busy_flag` used as a lock                                                | `phone_commands.cpp:34`                                                              | `sendToPhone` and `sendComToPhone` are called only from the loop on **both** MCUs (`esp32_main.cpp:2867`, `:2877`; `nrf52_main.cpp:1696`, `:1700`) — the "lock" can never be contended                                                     | **Delete on both MCUs** (it is a no-op guard, not a race — see §8)                                         |
| `taskENTER_CRITICAL()` around `cad_channel_busy = …; cad_done_flag.store(release)` | `nrf52/nrf52_main.cpp:394-397`                                                       | both are already `std::atomic`; the pair is only ever read as a snapshot under the reader's own critical section at `:1341-1346`                                                                                                           | the critical section is redundant with the atomics **or** vice versa — pick one; currently paying for both |

**Net: 14 objects in the LoRa path can drop their synchronisation on ESP32**, including the one
spinlock in the codebase; plus `ble_busy_flag` on both MCUs; three objects are outright dead
(`scanFlag`, `ch_util_rx_start` on ESP32, `g_task_event_type` on nRF52).

**Cost.** Quantified where measurable, flagged where not:

- `displayMux` — 7 × `String::operator=` inside `portENTER_CRITICAL`. Each assignment is a
  capacity compare plus, when the source is longer, `realloc` → `heap_caps_malloc` →
  `MULTI_HEAP_LOCK` → a nested `portENTER_CRITICAL` on the heap spinlock that the **other core** may
  hold. Worst case is bounded only by how long core 0 holds the heap lock; the failure mode is an
  `Interrupt wdt timeout on CPU1` panic (`INT_WDT`, 300 ms default). Not measured on hardware — the
  mechanism is certain, the frequency is not.
- The atomics themselves — **measured from this build** (`pio run -e heltec_wifi_lora_32_V3`,
  objdump of `src/*.o`). They are cheaper than one might assume, and the honest conclusion is that
  throughput is _not_ the reason to remove them. Every RMW is generated **inline**; there is no
  libcall and no interrupt masking (`nm -u` finds no `__atomic_*` undefined symbol in any of the
  three LoRa-path objects). `ch_util_rx_accum.fetch_add` compiles to an eight-instruction
  `S32C1I` compare-and-swap retry loop:

  ```asm
  memw
  l32i     a8, a9, 0
  add.n    a11, a8, a10
  wsr.scompare1 a8
  s32c1i   a11, a9, 0
  bne      a11, a2, <retry>
  ```

  | Object               | `s32c1i` (CAS loops) | `memw` (full barriers) |
  | -------------------- | -------------------- | ---------------------- |
  | `lora_functions.o`   | 10                   | 65                     |
  | `loop_functions.o`   | 0                    | 127                    |
  | `esp32/esp32_main.o` | 4                    | 75                     |

  So the direct cost is 14 CAS loops and 267 `memw` full memory barriers on the hot LoRa path — real
  but small. **The reason to remove them is false confidence, not cycles:** five prior audit items
  were closed by hardening variables that were never shared, one of them (`scanFlag`) not used at
  all, and the `displayMux` spinlock (whose cost _is_ unbounded) exists because the ISR premise was
  wrong.

- `taskENTER_CRITICAL()` on nRF52 raises BASEPRI to `configMAX_SYSCALL_INTERRUPT_PRIORITY`. Any
  allocation, `vTaskDelay` or SPI transaction inside it is a SoftDevice-timing hazard (F2-1, F2-3).

Counter-note, so this is not read as "remove all the atomics": `receiveFlag`, `transmittedFlag`,
`bEnableInterruptReceive`, `bEnableInterruptTransmit` **must stay atomic** (genuine ISR↔task), and
`iWrite`/`iRead`/`loraWrite`/`cad_*` **must stay atomic on nRF52** — but atomicity there is
necessary and **not sufficient** (F2-4).

---

## 6. Patterns

### 6.1 The one correct pattern — copy it

**The nRF52 CAD flag protocol** (`src/nrf52/nrf52_main.cpp:394-397` writer, `:1341-1346` reader) is
the only place in this codebase where a shared-state protocol is fully correct, and it is the
template for the F2-4 fix:

```c
// writer — OnCadDone, radio context
taskENTER_CRITICAL();
cad_channel_busy = channelActivityDetected;
cad_done_flag.store(true, std::memory_order_release);
taskEXIT_CRITICAL();

// reader — nrf52loop()
taskENTER_CRITICAL();
bool _cad_ip = cad_in_progress;
bool _cad_df = cad_done_flag;
bool _cad_cb = cad_channel_busy;
bool _cad_dc = cad_double_check;
taskEXIT_CRITICAL();
// … all decisions made on the snapshot, outside the lock
```

Three properties make it right, and all three are required:

1. **Symmetric.** Both sides take the lock. A guard only one side honours is not a guard.
2. **Multi-field state is snapshotted as a group**, so the reader can never observe a half-updated
   set. Per-field atomicity would not give this.
3. **The lock holds nothing but stores and loads.** No allocation, no driver call, no logging, no
   blocking. All work happens on the local snapshot after `taskEXIT_CRITICAL()`.

The display drain at `nrf52_main.cpp:1258-1274` has properties 1 and 2 and violates 3 (seven `String`
copies inside) — it is the same idea done at the wrong granularity. Fix it by snapshotting a POD, not
by removing the lock.

The second correct pattern is `bleQueue` (`esp32_main.cpp:277`, `:359`, `:1586`, `:2815`): a
5-slot FreeRTOS queue of a POD struct, non-blocking on both ends, that turns a cross-core producer
into a single-context consumer. It is the reason the entire ESP32 BLE data path is race-free. Port
it to nRF52 (F2-11) rather than inventing anything.

### 6.2 Anti-patterns to stop repeating

| Anti-pattern                                                   | Where                                                                                                                                  | Why it is wrong                                                                                                             |
| -------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| **"Make the index atomic" as a fix for a multi-writer buffer** | `iWrite`/`iRead` (C1), `loraWrite`                                                                                                     | Atomic indices make each load tear-free; they do not make fill+advance one operation. F2-4.                                 |
| **Load-modify-store on an atomic**                             | `advanceIReadPastEmpty` (`lora_functions.cpp:1539`), `iRead = iReadBeforeAdvance` (`:1756`, `:1797`, `:1865`), `g_task_event_type \|=` | `x = f(x)` on an atomic is two operations. Use `fetch_or` / `compare_exchange`.                                             |
| **Test-then-set instead of compare-exchange**                  | `checkRX` (`esp32_main.cpp:3768-3771`), `bPendingRadioRx` (`nrf52_main.cpp:1895`), `ble_busy_flag`                                     | The shape is a TOCTOU trap even where it cannot misfire today. F2-17.                                                       |
| **Blocking or allocating work inside a critical section**      | `Radio.Send` (F2-3), 7× `String` copy (F2-1/F2-2), `printfdeb` in the radio path (F2-18)                                               | `taskENTER_CRITICAL` masks SysTick and the SoftDevice; `portENTER_CRITICAL` on ESP32 nests onto a cross-core heap spinlock. |
| **A guard only one side honours**                              | `bSPI_ETH_Active` (F2-19), `externQueue::used` (F2-14)                                                                                 | Advisory flags are TOCTOU by construction.                                                                                  |
| **`volatile` used as a synchronisation primitive**             | `g_task_event_type`, `bSPI_ETH_Active`, `bSetLoRaAPRS`, `s_hs_running`                                                                 | `volatile` forces the load and store to be emitted. It gives no atomicity and no ordering.                                  |
| **A platform-specific safety argument stated as universal**    | `phone_commands.cpp:528-529` (F2-11), the "higher priority" argument for `bSPI_ETH_Active` (F2-19)                                     | Both were used to justify removing or omitting a guard. Both are false on one of the two MCU families.                      |
| **Closing an audit item by hardening a variable nobody uses**  | `scanFlag` (F2-6), `g_task_event_type`                                                                                                 | Produces a clean audit trail and no behaviour change, and the next reviewer reads the trail as evidence of safety.          |
| **Creating a task with `xTaskCreate` on a dual-core MCU**      | F2-20, five sites                                                                                                                      | Unpinned means either core, which invalidates every `taskENTER_CRITICAL`-based argument.                                    |
| **Naming a task after what it is not**                         | `t5epaper_main.cpp:671` creates `btn_task` under the name `"lora_task"`                                                                | Task names are the only runtime handle on context; a wrong one makes a trace unreadable.                                    |

---

## 7. Rules this implies

Proposed additions to `docs/codequality-rules.md`. "Mechanical" means it can be enforced by a
grep-level check in CI today; "review" means it needs a human or a real static analyser.

| #    | Rule                                                                                                                                                                                            | Enforcement                                                                                                                                                                                                               |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C-1  | **Every task-creating call on a dual-core MCU must be `xTaskCreatePinnedToCore`.** `xTaskCreate` is banned in `src/` for ESP32 targets.                                                         | **Mechanical** — `grep -rn 'xTaskCreate(' src/` must match nothing outside `#if !defined(ESP32)` blocks. Five current violations (F2-20).                                                                                 |
| C-2  | **Every shared global carries an ownership annotation** at its definition: `// ctx: <writers> -> <readers>`, using the context names from §1. New globals without one are rejected.             | **Mechanical** (presence) — a script can require the comment on any `extern` in `loop_functions_extern.h` and any non-`static` definition. **Review** (accuracy).                                                         |
| C-3  | **`volatile` is only for memory-mapped I/O.** It is never a synchronisation primitive. Use `std::atomic` for ISR↔task scalars and a lock or queue for anything wider than a word.               | **Mechanical** — flag every `volatile` declaration in `src/` that is not a register access; require a one-line justification comment. ~8 current uses (§3).                                                               |
| C-4  | **No allocation, no driver call, no logging, no blocking call inside `taskENTER_CRITICAL` / `portENTER_CRITICAL`.** Snapshot a POD, exit, then work.                                            | **Mechanical (approximate)** — flag any `taskENTER_CRITICAL`/`portENTER_CRITICAL` block containing `String`, `malloc`, `new`, `Serial`, `printfdeb`, `Radio.`, `delay`, `vTaskDelay`, `xTimer`. Catches F2-1, F2-2, F2-3. |
| C-5  | **`std::atomic` may not be used to protect a multi-step operation.** If a sequence loads an atomic more than once and the value must not change in between, it needs a lock.                    | **Review.** A partial mechanical proxy: flag `atomic_var = <expr containing atomic_var>` and `atomic_var++` (catches the RMWs in §6.2).                                                                                   |
| C-6  | **Multi-field shared state is read and written as a snapshot**, under one lock, on both sides. Per-field atomicity is not a substitute.                                                         | **Review**, with the CAD protocol (§6.1) as the reference implementation to cite in review.                                                                                                                               |
| C-7  | **A safety argument that depends on the MCU family must be stated with the family named and must hold for all shipped families**, or the code must be `#if`-guarded to the family it holds for. | **Review.** Applies to comments as much as to code — F2-11 and F2-19 are both comment-induced.                                                                                                                            |
| C-8  | **An audit item is not closed until a reader and a writer are named.** "Made X atomic" without both is not a fix.                                                                               | **Mechanical (process)** — the audit template gets mandatory "writer context" / "reader context" fields. Would have caught `scanFlag` and `g_task_event_type`.                                                            |
| C-9  | **A global with no readers, or no writers, is deleted.**                                                                                                                                        | **Mechanical** — a script that counts occurrences per identifier in `src/` and flags any `extern`/definition with exactly one occurrence, or with only writes. Three current hits.                                        |
| C-10 | **Cross-context data transfer uses a FreeRTOS queue of a POD**, not a shared buffer plus indices. New producer/consumer pairs may not introduce a new `xxxWrite`/`xxxRead` int pair.            | **Mechanical** — grep for new `int .*Write` / `int .*Read` pairs in `loop_functions.cpp`. **Review** for the design.                                                                                                      |
| C-11 | **Task priority relationships that a guard depends on must be asserted at boot**, e.g. `configASSERT(uxTaskPriorityGet(loraHandle) > uxTaskPriorityGet(NULL))`.                                 | **Mechanical (presence)** — would have caught the priority-1 `LORA` task at the first boot rather than in this review.                                                                                                    |

---

## 8. Verification status

Everything below was checked against the working tree at `3fb2c917`. The raw report was written
against `1ba101f4`, before the rebase onto `upstream/dev`; `git diff --stat 1ba101f4 HEAD` shows
`esp32_main.cpp +102`, `loop_functions.cpp +296`, `lora_functions.cpp +174`, `extudp_functions.cpp
+86`, `nrf52_main.cpp +21`, so most citations moved.

### 8.1 Structural claims

| Claim (raw report)                                                                                     | Status                  | Evidence at `3fb2c917`                                                                                                                                                                                                                                                                                        |
| ------------------------------------------------------------------------------------------------------ | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32: `OnRxDone` is called only from `checkRX()`, which runs in `loopTask`, not an ISR                | **Re-verified**         | `checkRX` defined `esp32_main.cpp:3754`, sole `OnRxDone` call at `:3840`, sole `checkRX` call from `esp32loop()` at `:2225`; `OnTxDone()` at `:2283`. (Report said `:3692`/`:3778`/`:2205`/`:2275`.)                                                                                                          |
| ESP32: the only ISR is `setFlagReceive`/`setFlagSent`, touching only atomics                           | **Re-verified**         | `:490`, `:506`. Both bodies are identical and set `receiveFlag` and `transmittedFlag`. (Report said `:487`/`:503` and implied the two differ.)                                                                                                                                                                |
| nRF52: the callbacks run in the SX126x `"LORA"` FreeRTOS task                                          | **Re-verified**         | `RadioEvents` wired `nrf52_main.cpp:942-949` (report: `:939-946`); task created `board.cpp:498`; DIO1 ISR body `radio.cpp:1338-1356` is `IrqFired = true; xSemaphoreGiveFromISR(_lora_sem, …)`.                                                                                                               |
| nRF52: that task runs **at priority 2 and preempts `loop()` at priority 1**                            | **REFUTED**             | `board.cpp:44-45` defines `TASK_PRIO_NORMAL` as a macro `1`, shadowing the core's enum (`rtos.h:59`) because `#ifndef` sees no macro. Object code confirms: `start_lora_task` passes `[sp,#0] = 1`. Loop task is also 1. `configUSE_TIME_SLICING = 0`. **Both docs 01 and 08 (C-01) state this incorrectly.** |
| nRF52: the radio callbacks nevertheless preempt the loop                                               | **True, new mechanism** | Via the FreeRTOS **timer service task at priority 2**: `RadioOnRxTimeoutIrq`/`RadioOnTxTimeoutIrq` (`radio.cpp:1283`, `:1299`, registered `:598-599`) call `RadioBgIrqProcess()`, which dispatches the full `RadioEvents` set. New finding F2-21.                                                             |
| nRF52: `LORA` and Bluefruit callback tasks "round-robin between themselves (`configUSE_TIME_SLICING`)" | **REFUTED**             | `FreeRTOSConfig.h:68` sets `configUSE_TIME_SLICING 0`. Equal-priority tasks do **not** round-robin; they switch only on an explicit yield or block.                                                                                                                                                           |
| Core pinning: `-DARDUINO_RUNNING_CORE=1`, `-DARDUINO_EVENT_RUNNING_CORE=1`                             | **Re-verified**         | `espressif32@6.13.0/boards/heltec_wifi_lora_32_V3.json:11-12`; `platformio.ini:165` pins the platform to `espressif32@^6.13.0`.                                                                                                                                                                               |
| `CONFIG_BT_NIMBLE_PINNED_TO_CORE` is 0                                                                 | **Re-verified**         | `NimBLE-Arduino/src/nimconfig.h:195-196`, not overridden in `platformio.ini` (which sets only `MAX_CONNECTIONS`, `MAX_BONDS`, `MAX_CCCDS`, role disables, `HOST_TASK_STACK_SIZE=3072`, `MSYS1_BLOCK_COUNT`, `:183-189`).                                                                                      |
| `scanFlag` has zero readers and zero writers                                                           | **Re-verified**         | `grep -rn scanFlag src/` returns its definition (`esp32_main.cpp:473`) and one 2026-05-08 audit note. Nothing else.                                                                                                                                                                                           |
| `displayMux` wraps a 7-`String` struct copy inside `portENTER_CRITICAL`                                | **Re-verified**         | `lora_functions.cpp:123` (mux), `:128-141` and `:147-160` (both critical sections), `aprs_structures.h:18-24` (the seven `String`s), drain `esp32_main.cpp:1949-1965` / `nrf52_main.cpp:1258-1274`.                                                                                                           |
| nRF52 loop task stack "256×4 words = 4 KB", callback task "256×3 = 3 KB"                               | **Re-verified**         | `main.cpp:42` `LOOP_STACK_SZ (256*4)` = 1024 words = 4 KB; `ada_callback_init(768)` from object code, 768 words = 3 KB. Timer task: `configTIMER_TASK_STACK_DEPTH 256` = 1 KB (report omitted this).                                                                                                          |

### 8.2 Findings re-verified, with corrected citations

| Finding | Status                             | Citation correction                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| ------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| F2-1    | Confirmed                          | `lora_functions.cpp:126-161` → `:126-163`; drain `nrf52_main.cpp:1253-1270` → `:1258-1274`; struct `aprs_structures.h:8-34` → `:9-34`.                                                                                                                                                                                                                                                                                                                                                                 |
| F2-2    | Confirmed                          | `displayMux` `:122` → `:123`; drain `esp32_main.cpp:1941-1959` → `:1949-1965`.                                                                                                                                                                                                                                                                                                                                                                                                                         |
| F2-3    | Confirmed                          | `Radio.Send` `:1685`/`:1726`/`:1787` → **`:1738`/`:1779`/`:1840`**; `nrf52_main.cpp:1318-1320` → `:1321-1323`, `:1356-1370` → `:1359-1375`, `:1397-1416` → `:1400-1421`, `:1433-1450` → `:1436-1454`.                                                                                                                                                                                                                                                                                                  |
| F2-4    | Confirmed; **mechanism corrected** | Ring decls `loop_functions.cpp:383-385` → **`:385-387`**; `ringPriority`/`ringEnqueueTime` `:388` → **`:476-477`**; radio-ctx writers `:262,980,1014,1169,1927` → **`:262,1031,1065,1221,1979`**; loop writers `:3218,3629,3767,3848,3926,4005,4275,4292` → **`:3129,3210,3463,3874,4012,4093,4171`**; `addTxRingEntry` `:1495` → `:1547`; `advanceIReadPastEmpty` `:1477` → `:1529`; `doTX` `:1599` → `:1651`; rollbacks `:1704,1745,1813` → `:1756,1797,1865`. Interleaving rewritten — see F2-4 §4. |
| F2-5    | Confirmed (partly refuted)         | `udpWrite/Read` `:405-406` → `:407-408`; `toPhoneWrite/Read` `:410-411` → `:412-413`; helper `:4710` → `:4972`. **The `ComToPhoneWrite` half is refuted** — see 8.4.                                                                                                                                                                                                                                                                                                                                   |
| F2-6    | Confirmed                          | `esp32_main.cpp:473` unchanged.                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| F2-7    | Confirmed                          | ISR `:487-500` → `:490-518`; loop sites `:2212-2215,2145-2147,2231-2233,2276-2281,3736-3745` → `:2218-2219,2156-2176,2238-2241,2286-2304`.                                                                                                                                                                                                                                                                                                                                                             |
| F2-8    | Confirmed                          | Definitions `:282-290` unchanged; writers `:303-320` → `:305-316`; readers `:2728-2765,2796-2846` → **`:2768-2793`, `:2802-2886`**.                                                                                                                                                                                                                                                                                                                                                                    |
| F2-9    | **Refuted as described**           | Writers confirmed (`nrf52_ble.cpp:247`, `:342`, `api_functions.cpp:285`), but **`g_task_event_type` has zero readers in `src/`** — see 8.4.                                                                                                                                                                                                                                                                                                                                                            |
| F2-10   | Confirmed, unchanged               | `net_console.cpp` was not touched by the rebase; every line number in the raw report still resolves (`:53,54,56-58,61-62,109-118,208-214,275,284-285,291,345,378,388,402,421`).                                                                                                                                                                                                                                                                                                                        |
| F2-11   | Confirmed                          | **Wrong file in the raw report**: `api_functions.cpp:243-263`/`:254` → **`nrf52_ble.cpp:243-254`**. Line numbers were right, file was not. Comment `phone_commands.cpp:528-529` unchanged. Command body `:527-546` → `:526-545`.                                                                                                                                                                                                                                                                       |
| F2-12   | Confirmed                          | **Wrong file**: `api_functions.cpp:296-347`/`:300`/`:319`/`:322` → **`nrf52_ble.cpp:296-322`**, `delay(1000)` `:300`, `memcpy` `:319`, `save_settings()` `:322`. Struct `esp32_flash.h:8`; nRF52 definition `WisBlock-API.h:174,389`.                                                                                                                                                                                                                                                                  |
| F2-13   | Confirmed                          | `nrf52_ble.cpp:248` `xSemaphoreGiveFromISR(g_task_sem, pdFALSE)` from task context; `:344` correctly uses `xSemaphoreGive`.                                                                                                                                                                                                                                                                                                                                                                            |
| F2-14   | Confirmed                          | Producer `:507-522` → **`:593-607`**; consumer `:524-535` → **`:610-618`**; struct `:47-58` → `:48-58`. `queueExtern` caller `lora_functions.cpp:701` unchanged.                                                                                                                                                                                                                                                                                                                                       |
| F2-15   | Confirmed                          | `onrxdone_*` writes `:1228-1235` → `:1280-1284`; reset `nrf52_main.cpp:1289-1291` → `:1292-1294`; `csma_reset` `:2084-2085` → `:2136-2137`; stats `:1559,1612-1620,1520-1521,2081-2082` → `:1572-1573,1611,1626,1664-1672,2133-2134`.                                                                                                                                                                                                                                                                  |
| F2-16   | Confirmed                          | `gKeyNum` `:320` → **`:323`**; ISRs `:324-348` → `:327-352`; `attachInterrupt` `:1033` → `:1036`; loop sites `:1538,1542,1550,1553,1655` → `:1541,1545,1553,1556,1658,1661,1665`.                                                                                                                                                                                                                                                                                                                      |
| F2-17   | Confirmed                          | `esp32_main.cpp:3706-3709` → **`:3768-3771`**.                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| F2-18   | Confirmed                          | `OnHeaderDetect` `:2035-2047` → `:2087-2099`; `LORA_ISR_DEBUG` sites `:331,337,358,364,372` → `:330,336,357,363,371`; the macro is still defined nowhere; `_overwrite` `:328` → `:319`, consumed only at `:328-333`.                                                                                                                                                                                                                                                                                   |
| F2-19   | Confirmed; **argument refuted**    | Sites `:341-345`, `:1969-1981` → `:341-345`, `:2022`, `:2029-2032`; `nrf52_main.cpp:1879,1891,2276-2321` → `:1882,1894-1895,2279-2324`. The "higher priority" justification is void — see 8.1.                                                                                                                                                                                                                                                                                                         |
| F2-20   | Confirmed                          | All five `xTaskCreate` sites unchanged; priorities confirmed from `t5-epaper/peripheral.h:6-10` and `t-deck-pro/peripheral.h:6-10`; `checkRX(true)` `:208` → **`:220`**.                                                                                                                                                                                                                                                                                                                               |
| F2-21   | **New**                            | Not in the raw report. See §4.                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |

### 8.3 Claims carried over from the raw report **without** independent re-verification

- The `esp32_audio` and T-Deck TFT semaphore entries are recorded as "pre-existing exceptions" per
  `docs/code-audit-20260626.md` item 24. I confirmed the declarations
  (`esp32_audio.cpp:26`, `t-deck/tdeck_main.cpp:47`, `:401`) but did not analyse those paths.
- ~~The claim that ESP32 `std::atomic` RMWs are meaningfully expensive is not measured.~~
  **Now measured** — see §5. The RMWs are inline `S32C1I` CAS loops, no libcall, no interrupt
  masking; 14 CAS loops and 267 `memw` barriers across the three LoRa-path objects. This
  **partially refutes** the raw report's framing of over-synchronisation as a throughput problem.
  The false-confidence argument stands unchanged.
- IDF-internal task priorities (WiFi/lwIP 18–23, BT controller "very high") are taken from the raw
  report and general IDF knowledge, not verified against this build's sdkconfig.
- The `-Oz` register-caching argument in F2-8 and F2-16 is a correct statement about what the
  standard permits. I did not disassemble the ESP32 or nRF52 build to show that the compiler
  actually does it here.
- F2-15's assertion that a torn read of `csma_timeout` is impossible (32-bit aligned, single core) is
  carried over as stated.

### 8.4 Refuted or stale — do not re-derive

| Raw-report claim                                                                                                         | Finding                                                                                                                                                                                                                                                                                                                                |
| ------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| The `LORA` task runs at priority 2 and preempts `loop()`                                                                 | **Refuted.** Priority 1, equal to the loop; `configUSE_TIME_SLICING = 0`. Object-code evidence in §1.2. The _conclusion_ (radio callbacks preempt the loop) survives via the timer service task at priority 2 — a different context with a **1 KB** stack. F2-21.                                                                      |
| "Bluefruit(2) == LORA(2) > loop(1)"; the two "round-robin between themselves"                                            | **Refuted.** Bluefruit callback task 2, timer task 2, `LORA` 1, loop 1, no time slicing.                                                                                                                                                                                                                                               |
| `ComToPhoneWrite`/`BLEComToPhoneBuff` is written from the Bluefruit callback task via `readPhoneCommand → commandAction` | **Refuted.** `readPhoneCommand` (`phone_commands.cpp:208-620`) never calls `commandAction`. `addBLEComToOutBuffer` is reached only from `commandAction` (`command_functions.cpp`, 13 sites), and `commandAction` is only ever called from the loop. **Single-context on both MCUs.**                                                   |
| `g_task_event_type` lost-update causes "the periodic beacon for that interval never fires"                               | **Refuted.** The variable has **zero readers anywhere in `src/`** — three `\|=` writers and nothing else. The loop wakes on `g_task_sem` alone (`nrf52_main.cpp:857`) and never inspects the event mask. The data race is real; the consequence is not. Reclassify from RACE to **DEAD** — a third instance of the `scanFlag` pattern. |
| `ble_busy_flag` is a "RACE (TOCTOU lock)"                                                                                | **Refuted.** `sendToPhone` and `sendComToPhone` are called only from the loop on both MCUs (`esp32_main.cpp:2867`, `:2877`; `nrf52_main.cpp:1696`, `:1700`). The flag can never be contended — **OVER-SYNC**, not a race. It is still worth deleting.                                                                                  |
| The BLE handlers live in `src/nrf52/api_functions.cpp`                                                                   | **Stale/wrong file.** They are in `src/nrf52/nrf52_ble.cpp`, at the same line numbers the report gave (205, 226, 243, 254, 296, 319, 322, 342, 344). Only `api_wake_loop` (`:283-292`) and the timer creation (`:341`) are actually in `api_functions.cpp`.                                                                            |
| `bLED_*` are declared in `esp32_main.cpp:161-169`                                                                        | **Wrong file.** `loop_functions.cpp:71-74`; externs at `loop_functions_extern.h:26-29`.                                                                                                                                                                                                                                                |
| `RcvBuffer` is "single (LORA task only) — fine" on nRF52                                                                 | **Refuted by F2-21.** Two radio contexts, one preempting the other. Same for `rxBufInUse[]`/`rxBufIndex`.                                                                                                                                                                                                                              |
| `bSPI_ETH_Active` "happens to work because the `LORA` task has higher priority"                                          | **Refuted.** Equal priority. The guard is weaker than the report assumed. F2-19.                                                                                                                                                                                                                                                       |
| `nrf_eth.cpp:641-643` `iWrite++` is dead code                                                                            | **Unverified — flagged.** The site still exists (`nrf_eth.cpp:640-642`) and is still a non-atomic RMW on an atomic. I did not confirm whether it is reachable.                                                                                                                                                                         |

### 8.5 Discrepancies with existing docs — flagged, not silently corrected

1. **[08 §1 C-01](08-defect-catalogue.md)** — "**nRF52:** it runs in the SX126x `"LORA"` FreeRTOS
   task at priority 2, which **preempts `loop()` at priority 1**." The priority is wrong (it is 1)
   and so is the preemption mechanism. C-01's _headline_ — `OnRxDone` does not run in interrupt
   context — is correct and unaffected; its second bullet needs replacing with §1.2 plus F2-21.
   C-01's ESP32 citations `esp32_main.cpp:3778` / `:2217` / `:487` / `:503` are pre-rebase and should
   become `:3840` / `:2225` / `:490` / `:506`.
2. **[01 §"The global variable bus"](01-system-overview.md)** — the 2026-07-31 correction box says
   "the genuine races are concentrated on nRF52, where the radio task preempts `loop()`". The
   conclusion is right; "the radio task preempts `loop()`" is not — it is the timer service task
   that preempts, running the radio callbacks. Suggested wording: _"…where the Bluefruit callback
   task and the FreeRTOS timer service task run at priority 2 and preempt `loop()`, and the timer
   task runs the radio callbacks through `RadioBgIrqProcess()`."_
3. **[08 §2 N-14](08-defect-catalogue.md)** — cites `lora_functions.cpp:1169` and
   `loop_functions.cpp:3219`; both are pre-rebase (`:1221` and `:3464`). The description "the `LORA`
   task preempts `loop_task` mid-`memcpy`" needs the F2-4 §4 correction: at equal priority the
   `LORA` task cannot preempt; the timer task can, and the `LORA` task needs a yield point.
4. **[08 §2 N-15](08-defect-catalogue.md)** — cites `api_functions.cpp:254`; the correct file is
   `nrf52_ble.cpp:254`.
5. **[08 §2 N-16](08-defect-catalogue.md)** — cites `lora_functions.cpp:1685`, `:1726`, `:1787`;
   now `:1738`, `:1779`, `:1840`.
6. **[08 §2 N-13](08-defect-catalogue.md)** — "14 objects can drop synchronisation on ESP32" is
   confirmed. It should additionally note (a) `g_task_event_type` as a third dead variable, (b)
   `ble_busy_flag`, and (c) that the removal must be gated on the board, not on `ESP32`, because of
   the T5-ePaper / T-Deck-Pro unpinned tasks (F2-20).
7. **`fable-verdict.md` CONC-14 … CONC-19** — all six re-verified as still open at `3fb2c917`. No
   contradiction. CONC-18 (`sendToPhone` TOCTOU) is listed there as "CONFIRMED (finder)"; this
   review finds the _writer_ side is what races (`addBLEOutBuffer` from the radio context), while
   `sendToPhone` itself is loop-only — so CONC-18 is a symptom of CONC-15 rather than an independent
   defect. The verdict's judgement that CONC-14 is the root fix for 15/16/17/18 is confirmed.
8. **[07 §V-03](07-verification-infrastructure.md)** — confirmed: `LORA_ISR_DEBUG` guards five
   traces (`lora_functions.cpp:330,336,357,363,371`) and is defined nowhere. Worth upgrading: one of
   the five is the only consumer of `_overwrite` (`:319`, `:328-333`), the nRF52 RX double-buffer
   collision detector — the single runtime signal for F2-21, computed and thrown away.
9. **[07 §"Coverage gaps"](07-verification-infrastructure.md)** — the hardware bench is specified as
   "ESP32-S3 + SX1262 ×2". Essentially every race in this document is nRF52-only. **The bench as
   configured cannot observe this class of defect at all**; it validates the MCU family with the
   least concurrency exposure. This should be stated explicitly there.
