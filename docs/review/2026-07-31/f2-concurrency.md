# F2 — Concurrency / CPU-core ownership review

Repo: `.`, branch `v4.35p_prio` @ `1ba101f4`.
Reference targets: `heltec_wifi_lora_32_V3` (ESP32-S3, dual core) and `wiscore_rak4631` (nRF52840, single core).

## Headline

The single most important structural fact this review establishes — and which no existing doc
states — is:

> **On ESP32 the radio callbacks (`OnRxDone`, `OnTxDone`, `OnRxError`, `OnRxTimeout`) do NOT run
> in interrupt or task context. They run in `loopTask`.** The only ESP32 ISR is
> `setFlagReceive`/`setFlagSent`, which touches five `std::atomic<bool>` and nothing else.
> `checkRX()` (`src/esp32/esp32_main.cpp:3692`) is the sole caller of `OnRxDone`
> (`:3778`), and it is called from `esp32loop()` (`:2205`). `OnTxDone()` is called from
> `esp32loop()` (`:2275`).
>
> **On nRF52 the same callbacks run in a dedicated FreeRTOS task `"LORA"` at priority 2, which
> preempts the Arduino `loop_task` at priority 1.** (`RadioEvents` wired at
> `src/nrf52/nrf52_main.cpp:939-946`; task created in
> `.pio/libdeps/wiscore_rak4631/SX126x-Arduino/src/boards/mcu/board.cpp:498`,
> `xTaskCreate(_lora_task, "LORA", 4096, NULL, TASK_PRIO_NORMAL, …)`; the DIO1 ISR
> `RadioOnDioIrq` only does `IrqFired = true; xSemaphoreGiveFromISR(_lora_sem, …)`.)

Consequences, both directions:

- Almost every `std::atomic` and `volatile` in the LoRa path is **dead weight on ESP32** and
  **necessary but insufficient on nRF52**.
- `portMUX_TYPE displayMux` (`src/lora_functions.cpp:122`) protects nothing on ESP32 — both sides
  are `loopTask` — while costing a heap-allocating 7×`String` struct copy with interrupts disabled.
- The heavy, unprotected, genuinely-concurrent state is the **TX ring `ringBuffer[iWrite]` on
  nRF52**, and the **NimBLE connection flags across cores 0/1 on ESP32**.

Counts (see tables): **9 RACE**, **4 OK (correctly protected)**, **14 OVER-SYNCHRONISED**,
**rest single-context**. 20 findings, of which 5 are "prior art claims a fix that is not in the
source" or "prior art's fix does not close the hole".

---

## Execution context inventory

### ESP32 — `heltec_wifi_lora_32_V3` (ESP32-S3, 2 cores)

Board JSON sets `-DARDUINO_RUNNING_CORE=1` and `-DARDUINO_EVENT_RUNNING_CORE=1`
(`~/.platformio/platforms/espressif32@6.13.0/boards/heltec_wifi_lora_32_V3.json:11-12`).

| Context                                                                                                                                      | Core                                                                              | Prio                                   | Stack                              | Entry point                                                                                                  |
| -------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- | -------------------------------------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `loopTask` → `loop()` → `esp32loop()`                                                                                                        | **1**                                                                             | 1                                      | 8192 B (`ARDUINO_LOOP_STACK_SIZE`) | `framework-arduinoespressif32/cores/esp32/main.cpp:71`; `src/main.cpp:52`; `src/esp32/esp32_main.cpp:1743`   |
| LoRa DIO1 GPIO ISR — `setFlagReceive` / `setFlagSent`                                                                                        | **1** (registered from `esp32setup()` on `loopTask`)                              | GPIO ISR (IDF level 1, shared handler) | shared GPIO ISR stack              | `src/esp32/esp32_main.cpp:487`, `:503`; registered `:1420`, `:1459-1462`, `:1498`, `:2058`, `:2287`, `:3737` |
| NimBLE host task — `MyServerCallbacks::onConnect/onDisconnect/onPassKeyDisplay/onAuthenticationComplete`, `CharacteristicCallbacks::onWrite` | **0** (`CONFIG_BT_NIMBLE_PINNED_TO_CORE 0`, `NimBLE-Arduino/src/nimconfig.h:196`) | NimBLE host default                    | **3072 B** (`platformio.ini:156`)  | `src/esp32/esp32_main.cpp:303-355`                                                                           |
| BT controller tasks                                                                                                                          | 0                                                                                 | very high                              | lib                                | IDF                                                                                                          |
| `con_auth` (net-console HMAC handshake)                                                                                                      | **1** (explicit)                                                                  | 1                                      | 3072 B                             | `src/net_console.cpp:378`                                                                                    |
| WiFi / lwIP `tcpip_task` / `wifi`                                                                                                            | 0 (IDF default)                                                                   | 18–23                                  | IDF                                | IDF                                                                                                          |
| Arduino event task (`WiFi.onEvent` consumers)                                                                                                | **1** (`ARDUINO_EVENT_RUNNING_CORE=1`)                                            | —                                      | —                                  | framework                                                                                                    |
| `audio play task` (T-Deck / T-Deck-Pro only)                                                                                                 | **1**                                                                             | **50**                                 | 16 KB                              | `src/esp32/esp32_audio.cpp:104`                                                                              |
| `lora_task` (T5-ePaper only)                                                                                                                 | **unpinned**                                                                      | `configMAX_PRIORITIES-2` = 23          | 3 KB                               | `src/t5-epaper/peri_lora.cpp:171`                                                                            |
| `gps_task` (T5-ePaper, T-Deck-Pro)                                                                                                           | **unpinned**                                                                      | `configMAX_PRIORITIES-1` = 24          | 3 KB                               | `src/t5-epaper/peri_gps.cpp:77`, `src/t-deck-pro/peri_gps.cpp:78`                                            |
| `a7682_handle` (T-Deck-Pro modem)                                                                                                            | unpinned                                                                          | 20                                     | 3 KB                               | `src/t-deck-pro/tdeck_pro.cpp:381`                                                                           |
| `btn_task` (T5-ePaper)                                                                                                                       | unpinned                                                                          | 20                                     | 3 KB                               | `src/t5-epaper/t5epaper_main.cpp:671`                                                                        |
| GPS RX-edge ISR `handleRxInterrupt`                                                                                                          | 1                                                                                 | GPIO ISR                               | —                                  | `src/gps_functions.cpp:182`; attached/detached only inside `detectBaudrate()` (`:202`, `:204`)               |
| Web server                                                                                                                                   | **no task** — synchronous `WiFiServer`, polled from `loopTask`                    | —                                      | —                                  | `src/web_functions/web_commonServer.h:18`, `src/web_functions/web_functions.cpp:25`                          |

Notes:

- **No `AsyncWebServer`/AsyncTCP in the main firmware.** `ESPAsyncWebServer` + `CONFIG_ASYNC_TCP_RUNNING_CORE=1`
  appear only in the two `*-safeboot` envs (`platformio.ini:174-182`, `:212-222`), which build a
  **separate image** (`build_src_filter = +<safeboot/*>`). No shared state with the main firmware.
- No `xTimerCreate` / `esp_timer_create` in the ESP32 main firmware. `lib/Timeout/Timeout.h` is a
  `millis()` polling helper, not a timer.
- Only ONE `xTaskCreate*` in ESP32 common code: `con_auth`. Everything else is board-specific.

### nRF52 — `wiscore_rak4631` (nRF52840, 1 core, SoftDevice S140)

| Context                                                                                                                             | Core   | Prio                                           | Stack                          | Entry point                                                                                           |
| ----------------------------------------------------------------------------------------------------------------------------------- | ------ | ---------------------------------------------- | ------------------------------ | ----------------------------------------------------------------------------------------------------- |
| `loop_task` → `loop()` → `nrf52loop()`                                                                                              | single | **1** (`TASK_PRIO_LOW`)                        | 256×4 words = 4 KB             | `framework-arduinoadafruitnrf52/cores/nRF5/main.cpp:83`; `src/nrf52/nrf52_main.cpp`                   |
| **`LORA` task** — runs `OnRxDone`, `OnTxDone`, `OnTxTimeout`, `OnRxTimeout`, `OnRxError`, `OnCadDone`, `OnHeaderDetect`             | single | **2** (`TASK_PRIO_NORMAL`) → **preempts loop** | 4096 words = 16 KB             | `SX126x-Arduino/src/boards/mcu/board.cpp:474,498`; callbacks wired `src/nrf52/nrf52_main.cpp:939-946` |
| DIO1 GPIO ISR `RadioOnDioIrq`                                                                                                       | single | ISR                                            | —                              | `SX126x-Arduino/src/boards/sx126x/sx126x-board.cpp:115`; body `radio.cpp:1340`                        |
| Bluefruit callback task (`ada_callback`) — `bleuart_rx_callback`, `settings_rx_callback`, `connect_callback`, `disconnect_callback` | single | **2** (`TASK_PRIO_NORMAL`) → **preempts loop** | 256×3 words = 3 KB             | `cores/nRF5/main.cpp:86`; handlers `src/nrf52/api_functions.cpp:205,226,243,296`                      |
| Bluefruit SoftDevice event task                                                                                                     | single | 3 (`TASK_PRIO_HIGH`)                           | lib                            | Bluefruit                                                                                             |
| FreeRTOS timer-service task — `periodic_wakeup(TimerHandle_t)`                                                                      | single | `configTIMER_TASK_PRIORITY` (2)                | `configTIMER_TASK_STACK_DEPTH` | timer created `src/nrf52/api_functions.cpp:341`; callback `src/nrf52/WisBlock-API.cpp:34`             |
| SX126x `TxTimeoutTimer` / `RxTimeoutTimer` → `RadioOnTxTimeoutIrq` etc.                                                             | single | timer task (2)                                 | as above                       | `SX126x-Arduino/src/boards/mcu/nrf52832/timer.cpp:41` (10 `SoftwareTimer` slots)                      |
| Button GPIO ISR `interruptHandle2`                                                                                                  | single | ISR                                            | —                              | `src/nrf52/nrf52_main.cpp:333`; attached `:1033`                                                      |

Priority ordering that matters: **Bluefruit(2) == LORA(2) > loop(1)**. Both callback contexts
preempt the loop; between themselves they round-robin (`configUSE_TIME_SLICING`).

---

## Shared state ownership map

Legend for "verdict": **RACE** = genuinely concurrent, unprotected. **OK** = concurrent, correctly
protected. **OVER-SYNC** = single-context but marked `volatile`/atomic/locked. **single** = fine.

Where ESP32 and nRF52 differ, both are given.

### LoRa radio / TX ring

| Object                                                                                                                     | file:line                                      | Writers                                                                                                                                                                                                                              | Readers                                                            | Protection                                                              | Verdict                                                                           |
| -------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------ | ----------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| `receiveFlag`                                                                                                              | `esp32/esp32_main.cpp:461`                     | DIO1 ISR (core 1)                                                                                                                                                                                                                    | `loopTask`, also written by loop `:2214` and `:2077,:3742`         | `std::atomic<bool>` seq_cst                                             | **OK** (per-variable)                                                             |
| `transmittedFlag`                                                                                                          | `:469`                                         | DIO1 ISR                                                                                                                                                                                                                             | `loopTask` (+ written `:2231`)                                     | atomic                                                                  | OK                                                                                |
| `bEnableInterruptReceive` / `bEnableInterruptTransmit`                                                                     | `:462`, `:470`                                 | `loopTask`                                                                                                                                                                                                                           | DIO1 ISR                                                           | atomic                                                                  | **RACE as a pair** — see F2-7                                                     |
| `scanFlag`                                                                                                                 | `:473`                                         | **nobody**                                                                                                                                                                                                                           | **nobody**                                                         | atomic                                                                  | **OVER-SYNC / dead** — F2-6                                                       |
| `transmissionState`                                                                                                        | `:459`                                         | `loopTask` (`doTX`)                                                                                                                                                                                                                  | `loopTask`                                                         | `volatile int`                                                          | OVER-SYNC (ESP32)                                                                 |
| `ringBuffer[MAX_RING][…]`                                                                                                  | `loop_functions.cpp:383`                       | ESP32: `loopTask` only (16 sites). nRF52: **`LORA` task** (`lora_functions.cpp:262,980,1014,1169,1927`) **+ `loop_task`** (`loop_functions.cpp:3218,3629,3767,3848,3926,4005,4275,4292`; `udp_functions.cpp:350`; `nrf_eth.cpp:463`) | same                                                               | none                                                                    | ESP32 **single**; nRF52 **RACE** — F2-4                                           |
| `iWrite` / `iRead`                                                                                                         | `loop_functions.cpp:384-385`                   | same as above                                                                                                                                                                                                                        | same                                                               | `std::atomic<uint8_t>`                                                  | ESP32 **OVER-SYNC**; nRF52 **RACE** (atomicity ≠ mutual exclusion) — F2-4         |
| `ringPriority[]`, `ringEnqueueTime[]`, `retryCount[]`                                                                      | `loop_functions.cpp:388`, `lora_functions.cpp` | as `ringBuffer`                                                                                                                                                                                                                      | as `ringBuffer`                                                    | none                                                                    | ESP32 single; nRF52 **RACE**                                                      |
| `ringBufferLoraRX[]` + `loraWrite`                                                                                         | `loop_functions.cpp:392-393`                   | ESP32 loop; nRF52 `LORA` task **+ loop** (`addLoraRxBuffer` from `OnRxDone` and from GW ACK)                                                                                                                                         | `is_new_packet`, `checkOwnRx`, `checkServerRx`                     | `loraWrite` atomic; slot bytes unprotected                              | ESP32 OVER-SYNC; nRF52 **RACE** (small)                                           |
| `is_receiving`                                                                                                             | `loop_functions.cpp:421`                       | ESP32: `loopTask` (`:2355`, `:3709`, `:3866`, `OnRxDone`); nRF52: `LORA` task                                                                                                                                                        | ESP32 loop; nRF52 loop `:1329`                                     | atomic                                                                  | ESP32 **OVER-SYNC**; nRF52 OK-ish (load/store gate is TOCTOU, F2-17)              |
| `tx_is_active`                                                                                                             | `:422`                                         | ESP32 loop; nRF52 `doTX`(loop) + `OnTxDone`(LORA)                                                                                                                                                                                    | loop                                                               | atomic                                                                  | ESP32 **OVER-SYNC**; nRF52 OK                                                     |
| `cad_in_progress`, `cad_done_flag`, `cad_double_check`, `cad_channel_busy`                                                 | `nrf52/nrf52_main.cpp:235-238`                 | nRF52: `OnCadDone`(LORA), `OnRxDone/OnRxError/OnRxTimeout`(LORA), loop                                                                                                                                                               | loop                                                               | atomic **+** `taskENTER_CRITICAL` snapshot (`nrf52_main.cpp:1332-1338`) | **OK** (nRF52); **not compiled on ESP32**                                         |
| `cad_attempt`, `csma_timeout`, `rx_irq_defer_count`                                                                        | `loop_functions.cpp:425-427`                   | nRF52: `OnRxDone` (`lora_functions.cpp:402,1242`), `csma_reset()`(both ctx), loop                                                                                                                                                    | both                                                               | none                                                                    | ESP32 single; nRF52 **RACE (benign-ish)** — F2-15                                 |
| `ch_util_rx_start`                                                                                                         | `loop_functions.cpp:429`                       | nRF52 `OnHeaderDetect` only                                                                                                                                                                                                          | `OnRxDone`, `OnRxError`, `OnRxTimeout`                             | atomic                                                                  | ESP32 **OVER-SYNC + dead** (`esp32_main.cpp:3775` says it is never set); nRF52 OK |
| `ch_util_tx_start`                                                                                                         | `:430`                                         | ESP32 loop only; nRF52 loop only                                                                                                                                                                                                     | loop                                                               | atomic                                                                  | **OVER-SYNC** both                                                                |
| `ch_util_rx_accum` / `ch_util_tx_accum`                                                                                    | `:431-432`                                     | ESP32 loop (`checkRX`, `OnRxDone`); nRF52 LORA task                                                                                                                                                                                  | loop (`.exchange(0)`)                                              | atomic RMW (`fetch_add`, `exchange`)                                    | ESP32 **OVER-SYNC**; nRF52 **OK** (correct RMW usage)                             |
| `pendingDisplayMsg` (7 × `String`), `pendingDisplayRssi/Snr`, `bPendingDisplayText/Pos`                                    | `lora_functions.cpp:109-118`                   | `queueDisplayText/Position` `:126,:145` — ESP32 loop, nRF52 LORA task                                                                                                                                                                | ESP32 `esp32_main.cpp:1944-1957`; nRF52 `nrf52_main.cpp:1254-1270` | `portMUX_TYPE displayMux` / `taskENTER_CRITICAL`                        | ESP32 **OVER-SYNC + dangerous** — F2-2; nRF52 **OK but dangerous** — F2-1         |
| `bSetLoRaAPRS`                                                                                                             | `loop_functions.cpp:86` (`volatile bool`)      | ESP32 loop; nRF52 `doTX`(loop) + `OnTxDone/OnTxTimeout`(LORA)                                                                                                                                                                        | both                                                               | `volatile`                                                              | ESP32 **OVER-SYNC**; nRF52 RACE (benign, single-core, no tearing)                 |
| `bSPI_ETH_Active`, `bPendingRadioRx`                                                                                       | `lora_functions.cpp:114-115`                   | loop (`nrf52_main.cpp:1879,1891,2276,2301,2309,2318`)                                                                                                                                                                                | `LORA` task (`lora_functions.cpp:341,1970,1977`)                   | `volatile` only                                                         | nRF52 **RACE (advisory guard)** — F2-19                                           |
| `onrxdone_max_ms`, `onrxdone_warn_count`                                                                                   | `lora_functions.cpp:105-106`                   | nRF52 `LORA` task (`:1229,:1232`)                                                                                                                                                                                                    | loop (`nrf52_main.cpp:1289-1291`, reset to 0)                      | none                                                                    | nRF52 **RACE (stats corruption)**                                                 |
| `iReceiveTimeOutTime`                                                                                                      | `esp32_main.cpp:465` / extern                  | nRF52 `LORA` task (`lora_functions.cpp:401,1241,1982`) + loop                                                                                                                                                                        | both, as a timer base                                              | none                                                                    | nRF52 **RACE (benign)**                                                           |
| `bLED_GREEN/RED/BLUE/ORANGE`                                                                                               | `esp32_main.cpp:161-169`, extern               | nRF52 `LORA` task (`lora_functions.cpp:386`), `phone_commands.cpp:88`                                                                                                                                                                | loop LED block                                                     | none                                                                    | nRF52 RACE (cosmetic)                                                             |
| `RcvBuffer[UDP_TX_BUF_SIZE*2]`                                                                                             | `loop_functions_extern.h:147`                  | `OnRxDone` (`lora_functions.cpp:407,1162,1213`)                                                                                                                                                                                      | `OnRxDone`, `addNodeData`, `queueExtern`                           | none                                                                    | ESP32 single; nRF52 **single (LORA task only)** — fine                            |
| `lora_tx_buffer`                                                                                                           | `lora_functions.cpp:97`                        | `doTX` (loop)                                                                                                                                                                                                                        | `doTX`, radio                                                      | none                                                                    | single (loop)                                                                     |
| `stat_tx_count[]`, `stat_drop_count[]`, `stat_latency_*`, `stat_queue_hwm`, `stat_preempt_count`, `stat_csma_hwm_attempts` | `loop_functions_extern.h:236-244`              | `addTxRingEntry`/`doTX`/`csma_reset` — nRF52 both contexts                                                                                                                                                                           | loop print block                                                   | none                                                                    | nRF52 **RACE (stats only)**                                                       |

### BLE / phone

| Object                                                                | file:line                                             | Writers                                                                                                                                                                               | Readers                                                                             | Protection                              | Verdict                                                                           |
| --------------------------------------------------------------------- | ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- | --------------------------------------- | --------------------------------------------------------------------------------- |
| `bleQueue` (5 × `BleQueueItem`)                                       | `esp32/esp32_main.cpp:277,1578`                       | NimBLE task core 0 (`:359`)                                                                                                                                                           | `loopTask` core 1 (`:2775`)                                                         | FreeRTOS queue, non-blocking both sides | **OK** — the one genuinely correct cross-core primitive                           |
| `deviceConnected`                                                     | `esp32_main.cpp:282`                                  | NimBLE task **core 0** (`:305,:316`)                                                                                                                                                  | `loopTask` **core 1** (`:2728,:2741,:2762`)                                         | none, plain `bool`                      | **RACE (cross-core)** — F2-8                                                      |
| `g_ble_conn_handle`                                                   | `:284`                                                | NimBLE core 0 (`:308`)                                                                                                                                                                | `loopTask` core 1 (`:2736` → `pServer->disconnect()`)                               | none, plain `uint16_t`                  | **RACE (cross-core)** — F2-8                                                      |
| `config_to_phone_prepare`, `conffin_sent`                             | `:289-290`                                            | NimBLE core 0 (`:306-307`), `loopTask` (`:2752,:2809,:2846`), `readPhoneCommand`                                                                                                      | `loopTask`                                                                          | none                                    | **RACE (cross-core)** — F2-8                                                      |
| `oldDeviceConnected`                                                  | `:283`                                                | `loopTask` only                                                                                                                                                                       | `loopTask`                                                                          | none                                    | single                                                                            |
| `BLEtoPhoneBuff[]`, `toPhoneWrite`, `toPhoneRead`                     | `loop_functions.cpp:409-411`                          | `addBLEOutBuffer` (`:525`) — ESP32 loop; **nRF52 `LORA` task + Bluefruit callback task + loop**                                                                                       | `sendToPhone` (`phone_commands.cpp:50`) — loop                                      | none, plain `int`                       | ESP32 single; **nRF52 RACE** — F2-5 (= verdict CONC-15, **NOT FIXED**)            |
| `BLEComToPhoneBuff[]`, `ComToPhoneWrite/Read`                         | `loop_functions.cpp:414-416`                          | `addBLEComToOutBuffer` — nRF52 Bluefruit callback task via `readPhoneCommand`/`commandAction`                                                                                         | loop                                                                                | none                                    | **nRF52 RACE**                                                                    |
| `ringBufferUDPout[]`, `udpWrite`, `udpRead`                           | `loop_functions.cpp:404-406`                          | `addNodeData` from `OnRxDone` — nRF52 `LORA` task                                                                                                                                     | `sendUDP`/loop                                                                      | none, plain `int`                       | ESP32 single; **nRF52 RACE** — F2-5 (= CONC-16, **NOT FIXED**)                    |
| `textbuff_phone`, `txt_msg_len_phone`, `hasMsgFromPhone`              | `phone_commands.cpp:22-23`, `loop_functions.cpp:417`  | ESP32 `loopTask` (via `bleQueue`); **nRF52 Bluefruit callback task** (`api_functions.cpp:254`)                                                                                        | `loopTask` (`esp32_main.cpp:2781`, `nrf52_main.cpp`)                                | none                                    | ESP32 single; **nRF52 RACE** — F2-11                                              |
| `meshcom_settings` (≈400 B struct with `char[40]`, `double`, `float`) | `esp32/esp32_flash.h:236`, `nrf52/WisBlock-API.h:382` | ESP32: loop only. **nRF52: Bluefruit callback task (`api_functions.cpp:319` `memcpy` + `save_settings()`), plus `readPhoneCommand` writes (`phone_commands.cpp:517-519`)**, plus loop | **everything**, incl. `LORA` task (`encodeAPRS`, `checkMesh`, `node_call` compares) | none                                    | ESP32 single; **nRF52 RACE (torn multi-word)** — F2-12 (= CONC-17, **NOT FIXED**) |
| `isPhoneReady`                                                        | `loop_functions.cpp:434`                              | nRF52 callbacks (`api_functions.cpp:214,231`), loop                                                                                                                                   | `OnRxDone` (`lora_functions.cpp:891,1041`), loop                                    | none                                    | nRF52 RACE (benign `int`)                                                         |
| `g_ble_uart_is_connected`                                             | `esp32_main.cpp:527`                                  | loop; nRF52 callbacks                                                                                                                                                                 | `sendToPhone`, `addBLEComToOutBuffer`                                               | none                                    | nRF52 RACE                                                                        |
| `ble_busy_flag`                                                       | `phone_commands.cpp:33`                               | `sendToPhone`/`sendComToPhone`                                                                                                                                                        | same                                                                                | none — used as a lock, plain `bool`     | RACE (TOCTOU lock)                                                                |
| `g_task_event_type`                                                   | `nrf52/WisBlock-API.cpp:24` (`volatile uint16_t`)     | `\|=` from Bluefruit callback task (`nrf52_ble.cpp:247,342`) and `api_wake_loop` (`api_functions.cpp:285`)                                                                            | loop                                                                                | `volatile` only                         | **RACE (lost RMW)** — F2-9                                                        |
| `g_task_sem`                                                          | `WisBlock-API.cpp:18`                                 | give from callback task; take with `portMAX_DELAY` in `api_wait_wake` (`api_functions.cpp:262`) and `xSemaphoreTake(g_task_sem,10)` (`nrf52_main.cpp:854`)                            | —                                                                                   | binary semaphore                        | OK (but see F2-13)                                                                |

### Net console / external UDP

| Object                                           | file:line                 | Writers                                                   | Readers                                                                                                               | Protection                                                                                   | Verdict                                     |
| ------------------------------------------------ | ------------------------- | --------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- | ------------------------------------------- |
| `s_fd`                                           | `net_console.cpp:53`      | `authTask` core 1 (`:211`), `teardownClient` (`:113`)     | `MeshSerialClass::write` (loop), `netConsoleRead` (`:402`), `netConsoleAvailable` (`:421`), `loopNetConsole` (`:345`) | mutex on writes; **`recv()` reads are unguarded**                                            | **RACE** — F2-10                            |
| `s_mutex`                                        | `:54`                     | `startNetConsole` (`:275`), **`stopNetConsole` (`:284`)** | everything                                                                                                            | —                                                                                            | **RACE** — F2-10 (= CONC-19, **NOT FIXED**) |
| `s_authenticated`, `s_peek_valid`, `s_peek_byte` | `:57,61,62`               | `authTask` core 1, loop                                   | loop, `authTask`                                                                                                      | partial mutex; `s_authenticated` read outside lock (`:235,:249,:388`)                        | RACE (mild)                                 |
| `s_hs_running`, `s_server_pending`               | `:56,58`                  | loop + `authTask`                                         | loop                                                                                                                  | `volatile`                                                                                   | RACE (mild)                                 |
| `externQueue[2]`                                 | `extudp_functions.cpp:57` | `queueExtern` (`:507`) — nRF52 `LORA` task                | `flushExternQueue` (`:524`) — loop                                                                                    | `used` is `std::atomic<bool>` with correct release/acquire, **but producer never checks it** | **RACE** — F2-14                            |
| `externQueueWrite`                               | `:58`                     | producer only                                             | producer only                                                                                                         | none                                                                                         | single                                      |

### Misc

| Object                                     | file:line                   | Writers                              | Readers                                                            | Protection                     | Verdict                                         |
| ------------------------------------------ | --------------------------- | ------------------------------------ | ------------------------------------------------------------------ | ------------------------------ | ----------------------------------------------- |
| `gKeyNum`                                  | `nrf52/nrf52_main.cpp:320`  | GPIO ISR `interruptHandle2` (`:333`) | loop (`:1542,:1553,:1655`), loop also writes (`:1538,:1550,:1655`) | **none — not even `volatile`** | **RACE** — F2-16                                |
| `pulseTimes[]`, `pulseIndex`, `lastMicros` | `gps_functions.cpp:172-175` | `handleRxInterrupt` ISR              | `detectBaudrate`                                                   | `volatile`                     | **OK** (single core, init-time only)            |
| `displayMux`                               | `lora_functions.cpp:122`    | —                                    | —                                                                  | spinlock                       | ESP32 **OVER-SYNC**                             |
| `audioSemaphore`                           | `esp32/esp32_audio.cpp:26`  | binary sem used as mutex             | audio task prio 50 core 1                                          | binary semaphore               | pre-existing exception (audit 20260626 item 24) |
| `xSemaphore` (T-Deck TFT)                  | `t-deck/tdeck_main.cpp:47`  | `portMAX_DELAY` take at `:401`       | —                                                                  | binary semaphore as mutex      | pre-existing exception                          |

---

## Findings

### F2-1: nRF52 — 7 heap-allocating `String` copies inside `taskENTER_CRITICAL()`

**File:** `src/lora_functions.cpp:126-161` (`queueDisplayText`, `queueDisplayPosition`),
`src/nrf52/nrf52_main.cpp:1253-1270` (drain).
**Severity: HIGH** (real-time / SoftDevice hazard).

`struct aprsMessage` (`src/aprs_structures.h:8-34`) contains **seven Arduino `String` members**
(`msg_source_path`, `msg_source_call`, `msg_source_last`, `msg_destination_path`,
`msg_destination_call`, `msg_payload`, `msg_gateway_call`). The line
`pendingDisplayMsg = aprsmsg;` at `lora_functions.cpp:133` and `_msg = pendingDisplayMsg;`
at `nrf52_main.cpp:1262` each expand to seven `String::operator=`, every one of which can call
`realloc`/`malloc`/`free`.

`taskENTER_CRITICAL()` on this port is `portENTER_CRITICAL()` →
`portSET_INTERRUPT_MASK()` (BASEPRI raised to `configMAX_SYSCALL_INTERRUPT_PRIORITY`)
(`framework-arduinoadafruitnrf52/cores/nRF5/freertos/Source/include/task.h:178`).

**Failure scenario.** A 200-byte text packet arrives with a 60-char source path. `OnRxDone` (LORA
task, prio 2) reaches `queueDisplayText`. Inside the critical section, `msg_payload = …` finds the
current buffer too small, calls `realloc`. newlib's `__malloc_lock` on this port is `vTaskSuspendAll`;
the allocator then walks the free list. With BASEPRI masked, the SysTick and all SoftDevice
application-priority interrupts are blocked for the whole allocation. On a fragmented heap this is
tens to hundreds of microseconds ×7. During that window the SoftDevice cannot service its radio
timeslot bookkeeping → BLE connection supervision timeouts, and the DIO1 edge for the _next_ LoRa
packet is delayed, which is exactly the blind-window the surrounding code was written to avoid.

**Fix.** Do not copy `String`s under a lock. Either (a) keep a preallocated
`char pendingDisplay[…]` flat buffer and `memcpy` the already-encoded frame (`RcvBuffer`), decoding
in the loop; or (b) reserve the `String`s once at boot (`reserve(N)`) _outside_ the lock and copy
only into the reserved capacity; or (c) replace the lock with a single-producer/single-consumer
`xQueueSend`/`xQueueReceive` of a POD struct, exactly like `bleQueue`.

---

### F2-2: ESP32 — the same `displayMux` critical section protects nothing, and disables interrupts around `malloc`

**File:** `src/lora_functions.cpp:122,128-141,147-160`; `src/esp32/esp32_main.cpp:1941-1959`.
**Severity: HIGH** (over-synchronisation with real cost + false confidence).

The comment says "RACE-01 fix: spinlock protects `pendingDisplayMsg` struct copy between **ISR** and
main loop". On ESP32 there is no ISR on either side:

- Producer: `queueDisplayText` ← `OnRxDone` ← `checkRX` (`esp32_main.cpp:3778`) ← `esp32loop`
  (`:2205`). `loopTask`, core 1.
- Consumer: `esp32loop` `:1944-1957`. `loopTask`, core 1.

Same task. The lock can never be contended, and the flags can never be observed mid-update.
Meanwhile `portENTER_CRITICAL(&displayMux)` disables interrupts on core 1 and the enclosed
`pendingDisplayMsg = aprsmsg` / `_msg = pendingDisplayMsg` performs seven `String` copies →
`heap_caps_malloc` → `MULTI_HEAP_LOCK` → nested `portENTER_CRITICAL` on the heap spinlock, which the
**other core** may be holding. Acquiring a cross-core spinlock while interrupts are disabled is the
canonical ESP-IDF interrupt-watchdog trigger.

**Failure scenario.** Core 0 (WiFi/lwIP or NimBLE host) is inside `heap_caps_malloc` holding the
heap spinlock and gets descheduled/interrupted at an unlucky moment; core 1 enters
`portENTER_CRITICAL(&displayMux)`, then spins on the heap spinlock with interrupts off. Core 1's
interrupt watchdog (`INT_WDT`, 300 ms default) fires → `Interrupt wdt timeout on CPU1` panic.
Rare, but the mechanism is real and the lock buys nothing.

**Fix.** On ESP32, delete `displayMux` and the two critical sections entirely (keep them under
`#if defined(BOARD_RAK4630)`), or apply the F2-1 fix uniformly for both MCUs.

---

### F2-3: nRF52 — `Radio.Send()` / `Radio.StartCad()` / `startRadioReceive()` called inside `taskENTER_CRITICAL()` — blocking FreeRTOS calls with interrupts masked

**File:** `src/lora_functions.cpp:1685-1687`, `:1726-1728`, `:1787-1789`;
`src/nrf52/nrf52_main.cpp:1318-1320`, `:1356-1360`, `:1362-1370`, `:1397-1400`, `:1409-1416`,
`:1433-1436`, `:1445-1450`.
**Severity: HIGH.**

```
taskENTER_CRITICAL();
Radio.Send(lora_tx_buffer, sendlng);
taskEXIT_CRITICAL();
```

`RadioSend` (`SX126x-Arduino/src/radio/sx126x/radio.cpp`) does
`SX126xTXena(); SX126xSetDioIrqParams(); SX126xSetPacketParams(); SX126xSendPayload(); TimerSetValue(&TxTimeoutTimer,…); TimerStart(&TxTimeoutTimer);`

Two independent violations:

1. **Blocking delay.** Every SX126x command goes through `SX126xCheckDeviceReady()` →
   `SX126xWaitOnBusy()` (`boards/sx126x/sx126x-board.cpp:138-152`), whose loop body is
   `delay(1)`. On this core `delay()` is **`vTaskDelay()`**
   (`framework-arduinoadafruitnrf52/cores/nRF5/delay.c:33-48`). Calling `vTaskDelay` with BASEPRI
   masked removes the running task from the ready list, adds it to the delayed list, and requests a
   PendSV that cannot fire — so execution continues in a task the scheduler believes is blocked, and
   because SysTick is masked the tick never advances, so the "1 ms" wait is 0 ms and the loop
   spins its full 1000 iterations before logging a timeout. Reached whenever the chip is not
   immediately ready — e.g. after `RadioSleep()` in `RadioOnTxTimeoutIrq`/`RadioOnRxTimeoutIrq`
   (`radio.cpp:1294,1310`), where `SX126xWakeup()` also calls `SX126xWaitOnBusy()`.
2. **Queue post from a critical section.** `TimerStart` → `SoftwareTimer::start()` →
   `xTimerStart(_handle, 0)` (`cores/nRF5/utility/SoftwareTimer.cpp:62`) posts to the timer
   command queue and calls `queueYIELD_IF_USING_PREEMPTION()`. Non-blocking, so it survives, but
   the yield is deferred and `SoftwareTimer::setPeriod` → `xTimerChangePeriod` is executed in the
   same masked window.

Also: a full 200-byte SPI payload write at 2 MHz is ≈1 ms of **interrupts fully disabled**, which
alone is enough to make the SoftDevice unhappy and to lose LoRa DIO1 edges.

**Failure scenario.** Node is a gateway under load. TX is issued right after an RX timeout put the
chip to sleep. `taskENTER_CRITICAL(); Radio.Send(...)` → `SX126xWakeup` → `SX126xWaitOnBusy` →
1000 × `vTaskDelay(1)` with the tick frozen. The task manipulates its own state-list entry 1000
times with the scheduler's invariants violated. Best case: the SPI command sequence is issued
while BUSY is still high and the SX1262 silently drops it → the packet is never transmitted and
`TxTimeoutTimer` fires
later. Worst case: FreeRTOS list corruption on the delayed list.

**Fix.** The critical section is there to keep the `LORA` task out. Use the same tool the CAD path
already uses on the state flags — snapshot/flag — or take a real mutex, or (cleanest) move `doTX()`
into the `LORA` task so no cross-task exclusion is needed. Under no circumstance call a radio driver
API inside `taskENTER_CRITICAL()`.

---

### F2-4: nRF52 — TX ring is a multi-writer structure with no mutual exclusion; the "C1 atomic index" fix does not close it

**File:** `src/loop_functions.cpp:383-385`; writers listed in the ownership map;
`src/lora_functions.cpp:1495` (`addTxRingEntry`), `:1477` (`advanceIReadPastEmpty`), `:1599` (`doTX`).
**Severity: HIGH.**

`docs/code-audit-fixes-20260627.md:33` records C1 as "`iWrite/iRead` → `std::atomic<uint8_t>` ✅ done"
and `docs/code-audit-20260712.md:171` frames the remaining work as "indices/paths that fix did not cover".
Both understate the problem: **making the indices atomic does not make the enqueue atomic.** The
enqueue is:

```
ringBuffer[iWrite][0] = size;          // read iWrite  (atomic load)
ringBuffer[iWrite][1] = status;        // read iWrite  again
memcpy(ringBuffer[iWrite]+2, …, size); // read iWrite  again
addTxRingEntry(...);                   // ringPriority[w], ringEnqueueTime[w], then iWrite = w+1
```

Nothing prevents two contexts from loading the _same_ `iWrite`.

**Concrete interleaving (nRF52).**

1. `loop_task` (prio 1) is in `sendMessage()` (`loop_functions.cpp:3218`): it has executed
   `ringBuffer[iWrite][0]=aprsmsg.msg_len;` with `iWrite == 5` and is inside the `memcpy` at `:3219`.
2. A LoRa packet arrives. `LORA` task (prio 2) preempts mid-`memcpy`.
3. `OnRxDone` decides to relay: `lora_functions.cpp:1169` `memset(ringBuffer[iWrite],0,…)` — also
   slot 5 — then writes the relay frame and calls `addTxRingEntry("rx_relay")`, which sets
   `ringPriority[5]`, `ringEnqueueTime[5]` and advances `iWrite` to 6.
4. `loop_task` resumes and finishes its `memcpy` **into slot 5**, splicing the tail of the user
   message over the relay frame, then calls `addTxRingEntry("user_msg")`, which recomputes
   `ringPriority[6]` from `ringBuffer[6]` (an unrelated/stale slot) and advances `iWrite` to 7.

Result: slot 5 holds a Frankenstein frame (relay header + user payload tail) that will be
transmitted with a valid-looking length; slot 6 is enqueued with garbage length/priority derived
from stale bytes; `stat_queue_hwm` and the `queued` computation are both wrong. On the air this is
a malformed APRS frame with a corrupted source path — the kind of packet other nodes will relay
before it fails to decode.

`advanceIReadPastEmpty()` (`:1477-1488`) and the `iRead = iReadBeforeAdvance` rollbacks in `doTX`
(`:1704,:1745,:1813`) are also plain load-modify-store on an atomic — `iRead = localRead` can clobber
a concurrent advance.

**Fix.** One writer, or one lock. Preferred, minimal and consistent with the ESP32 side: a
single `enqueueTx(const uint8_t* frame, size_t len, uint8_t status)` helper that takes a FreeRTOS
mutex (or `taskENTER_CRITICAL` — the body is pure `memcpy`, no allocation, no radio API) around
slot selection + fill + index advance. All 16 call sites go through it. Do **not** try to fix this
with more atomics.

---

### F2-5: `toPhoneWrite/toPhoneRead` and `udpWrite/udpRead` are still plain `int` — CONC-15/CONC-16 are NOT fixed

**File:** `src/loop_functions.cpp:405-406` (`udpWrite`,`udpRead`), `:410-411`
(`toPhoneWrite`,`toPhoneRead`), `:415-416` (`ComToPhoneWrite/Read`), `:397-398`
(`RAWLoRaWrite/Read`); declared `src/loop_functions_extern.h:189-190,196-197,201-202,184-185`.
Ring pointer helper `src/loop_functions.cpp:4710` still takes `volatile int&`.
**Severity: MED-HIGH.** **Status: prior art claims these are open; confirmed still open.**

`docs/code-audit-20260712.md:43-44` (CONC-15, CONC-16) describes exactly this. Verified at HEAD `1ba101f4`:
unchanged. Additionally `ComToPhoneWrite` (`addBLEComToOutBuffer`, `loop_functions.cpp:593-602`)
has the same shape and is **not** listed in the verdict — on nRF52 it is written from the Bluefruit
callback task (`readPhoneCommand` → `commandAction` → `addBLECommandBack`) while the loop task
drains it at `esp32_main.cpp:2827`/`nrf52_main.cpp`.

Note the asymmetry the verdict does not state: **on ESP32 all of these are single-context** because
`readPhoneCommand` is dispatched through `bleQueue` into `loopTask`. The whole class of findings is
nRF52-only. Fixing CONC-14 (defer to loop on nRF52) therefore does resolve 15/16/17/18, as the
verdict's sprint plan says — that judgement is correct.

---

### F2-6: `scanFlag` is `std::atomic<bool>` with zero readers and zero writers

**File:** `src/esp32/esp32_main.cpp:473`.
**Severity: LOW (correctness), MED (false confidence).**

`docs/code-audit-fixes-20260627.md:29` records "B2 `scanFlag` → `std::atomic<bool>` ✅ done
f121f3a1". Grep of the whole tree: `scanFlag` appears **only** at its own definition. The ESP32 CAD
path uses the blocking `radio.scanChannel()` (`esp32_main.cpp:2380,:2399`), not an async CAD
callback. So an audit item was closed by hardening a variable that does not participate in any
data flow. It should be deleted, and the audit trail corrected — otherwise the next reviewer will
read "CAD flags are atomic on ESP32" and believe the CAD path is synchronised.

---

### F2-7: ESP32 — `(bEnableInterruptReceive, receiveFlag)` is atomic per-field but not as a pair → lost or spurious RX edge

**File:** `src/esp32/esp32_main.cpp:487-500` (ISR), `:2212-2215`, `:2145-2147`, `:2231-2233`,
`:2276-2281`, `:3736-3745` (loop).
**Severity: MED-HIGH.**

ISR:

```
if(bEnableInterruptReceive) receiveFlag = true;
```

Loop:

```
bEnableInterruptReceive = false;
receiveFlag = false;
… reconfigure radio …
bEnableInterruptReceive = true;
```

**Interleaving A (spurious flag).** ISR evaluates `bEnableInterruptReceive` → true. Loop then
executes both stores (`gate=false`, `flag=false`). ISR resumes and stores `receiveFlag = true`.
The loop now enters `checkRX()` on the _next_ iteration with no packet in the FIFO;
`radio.readData()` returns an error and the code takes the `RX_OTHER_ERROR` path, restarting RX
unnecessarily. Observable as `[MC-DBG] RX_OTHER_ERROR code=…` bursts.

**Interleaving B (lost edge).** Loop sets `bEnableInterruptReceive = false` at `:2144` before
`radio.startReceive()`; DIO1 rises during the reconfiguration; ISR sees the gate closed and drops
the edge. This one is _partially_ mitigated by the deliberate `digitalRead(LORA_DIO1) == HIGH`
recovery at `:2167-2173`, `:2293-2300`, `:3741-3746` — good defensive engineering, worth keeping —
but the recovery only covers level-still-high, not a pulse that has already fallen.

Because the ISR and the loop are **both on core 1**, this is a preemption race, not a memory-ordering
race; `std::atomic` seq_cst is more than is needed, and does not help.

**Fix.** Replace the two flags with one `std::atomic<uint8_t> rx_state` and use
`compare_exchange_strong` for the gate transitions, or (simpler and idiomatic for RadioLib) drop
the gate entirely and let the loop's own state machine decide whether a set `receiveFlag` is
meaningful — the DIO1 level check already gives you the ground truth.

---

### F2-8: ESP32 — NimBLE connection state crosses core 0 → core 1 with no synchronisation at all

**File:** `src/esp32/esp32_main.cpp:282-290` (definitions), `:303-320` (writers, NimBLE host task,
**core 0**), `:2728-2765`, `:2796-2846` (readers, `loopTask`, **core 1**).
**Severity: MED-HIGH.**

`CONFIG_BT_NIMBLE_PINNED_TO_CORE` is 0 (`NimBLE-Arduino/src/nimconfig.h:196`) and
`ARDUINO_RUNNING_CORE` is 1 — so `onConnect`/`onDisconnect` genuinely execute on the other core.
The affected objects are plain, non-`volatile`, non-atomic:
`bool deviceConnected`, `uint16_t g_ble_conn_handle`, `bool config_to_phone_prepare`,
`bool conffin_sent`.

The project got the _data_ path right (`bleQueue`, `:359`/`:2775`) and then left the _control_ path
unsynchronised.

**Failure scenario 1 (stale handle → disconnect of the wrong link).** Phone A disconnects and
phone B connects in quick succession. Core 0 runs `onDisconnect` (`deviceConnected=false`) then
`onConnect` (`deviceConnected=true`, `g_ble_conn_handle=H_B`). Core 1's `loopTask` is inside the
`if (deviceConnected)` block at `:2727` with a register-cached `g_ble_conn_handle == H_A` (nothing
forces a reload — the variable is not `volatile` and `pServer->disconnect()` cannot alias it as far
as the optimiser is concerned across `-Oz`). `ble_disconnect_requested` (set by the auth path) then
causes `pServer->disconnect(H_A)` — a stale handle. Either a no-op (auth-failed peer stays
connected, a security regression) or, on handle reuse, a disconnect of the newly authenticated
peer.

**Failure scenario 2 (config storm never sent / sent twice).** `onConnect` sets
`config_to_phone_prepare = false` on core 0 at the same time `loopTask` is executing
`config_to_phone_prepare = false` at `:2809` after having read `true`. Combined with
`conffin_sent`, the phone can receive `--conffin` before the ten `config_cmds` have been queued, or
never receive it.

**Fix.** `std::atomic<bool>` / `std::atomic<uint16_t>` for the four fields is sufficient and cheap
(they are single-word). Better: extend `bleQueue` to carry connect/disconnect events so the loop
owns all BLE state transitions — this makes the ESP32 side fully single-context and matches the
comment already in `phone_commands.cpp:529`.

---

### F2-9: nRF52 — `g_task_event_type |= X` is a lost-update RMW across three contexts

**File:** `src/nrf52/WisBlock-API.cpp:24` (`volatile uint16_t g_task_event_type`);
writers `src/nrf52/nrf52_ble.cpp:247` (`|= BLE_DATA`, Bluefruit callback task), `:342`
(`|= BLE_CONFIG`, Bluefruit callback task), `src/nrf52/api_functions.cpp:285` (`|= reason`, called
from the FreeRTOS **timer task** via `periodic_wakeup` → `api_wake_loop(STATUS)`,
`WisBlock-API.cpp:34-38`).
**Severity: MED.**

`volatile` guarantees the load and the store are emitted; it does **not** make
`load; or; store` atomic. Three contexts at two different priorities (2, 2, 2 — timer task and
callback task time-slice) perform it.

**Failure scenario.** Bluefruit callback task loads `g_task_event_type == 0`, is preempted by the
timer task which does `|= STATUS` (stores 0x0001) and gives `g_task_sem`; the callback task resumes
and stores `0 | BLE_DATA` = 0x0002 — **`STATUS` is lost**. The loop wakes once (the semaphore
counted), handles `BLE_DATA`, and the periodic position/status beacon for that interval never
fires. Silent, intermittent, exactly the class of bug that gets blamed on the radio.

**Fix.** `std::atomic<uint16_t>` with `fetch_or(reason, std::memory_order_release)` on the writers
and `exchange(0, std::memory_order_acquire)` on the loop's consume. One-line change, no lock.

---

### F2-10: `net_console.cpp` — mutex is recreated without ownership (CONC-19 NOT fixed), and `recv()` runs outside it

**File:** `src/net_console.cpp:284` (`s_mutex = xSemaphoreCreateMutex();` inside `stopNetConsole`),
`:291` (`teardownClient()` called without holding the mutex), `:109-118` (asymmetric
lock protocol — the function _gives_ a mutex its caller took), `:288-289` (missing braces),
`:402`, `:421`, `:345` (`::recv(s_fd, …)` with no lock), `:208`, `:348` (`portMAX_DELAY`), `:378`
(`authTask` pinned to core 1 prio 1).
**Severity: MED.** **Status: `docs/code-audit-20260712.md:47,209-214` describes this precisely; confirmed
unchanged at HEAD.**

Beyond the verdict's description, two additions:

- **Unlocked `recv` on a mutable fd.** `netConsoleRead()` (`:402`) and `netConsoleAvailable()`
  (`:421`) call `::recv(s_fd, …)` with no mutex, while `authTask` on the _same core, same priority_
  can round-robin in and execute `if (s_fd >= 0) ::close(s_fd); s_fd = fd;` (`:210-211`). lwIP
  reuses small fd numbers aggressively; a console read can land on a freshly-opened UDP mesh socket.
- **`s_hs_running` is never cleared by `stopNetConsole`**, so after `--extser off` / `--extser on`
  during a handshake, every subsequent connection is rejected with "auth already in progress" until
  reboot.

**Fix (minimal):** in `stopNetConsole`, take `s_mutex`, do the teardown, give it, and **do not**
recreate it; make `teardownClient` symmetric (caller takes, caller gives); guard the `recv` calls
with the same mutex (`xSemaphoreTake(..., 0)` is fine — a missed poll is harmless); add
`s_hs_running = false`.

---

### F2-11: nRF52 — `readPhoneCommand` runs in the Bluefruit callback task, and a source comment asserts the opposite

**File:** `src/nrf52/api_functions.cpp:243-263` (`bleuart_rx_callback` → `readPhoneCommand(conf_data)`
at `:254`); the contradicted comment is `src/phone_commands.cpp:528-529`.
**Severity: MED-HIGH.** **Status: CONC-14 in `docs/code-audit-20260712.md:176`; confirmed NOT fixed.**

The comment reads:

```
// Spin-wait removed: readPhoneCommand now runs in Main Loop,
// no cross-core conflict with sendToPhone() possible
```

That is true for ESP32 (`bleQueue`, `esp32_main.cpp:359`/`:2775`) and **false for nRF52**, where the
handler is invoked directly in the callback. A previously-existing spin-wait guard was removed on
the strength of a statement that only holds on one of the two MCU families. This is a
documentation-induced regression and should be called out as such in the fix.

**Concrete interleaving.** The phone sends a `0xA0` text command. Bluefruit callback task (prio 2)
enters `readPhoneCommand` case `0xA0` (`phone_commands.cpp:527-546`): it sets
`txt_msg_len_phone = msg_len-2`, writes `textbuff_phone`, then sets `hasMsgFromPhone = true`. If it
is preempted between the `txt_msg_len_phone` store and the `memcpy` — or if the loop task was
already past `if(hasMsgFromPhone)` from a _previous_ message — the loop executes
`sendMessage(textbuff_phone, txt_msg_len_phone)` (`esp32_main.cpp:2784` equivalent in
`nrf52_main.cpp`) with a **new length against an old buffer**, transmitting whatever bytes follow
the shorter previous message, including heap contents past the terminator.

**Fix.** Port the ESP32 pattern: a 5-slot `xQueueSend` from `bleuart_rx_callback`, drained in
`nrf52loop()`. This is the CONC-14 root fix and it collapses F2-5, F2-11, F2-12 and most of the
nRF52 BLE entries in the ownership map.

---

### F2-12: nRF52 — `meshcom_settings` written (and flashed) from the Bluefruit callback task, with a 1-second `vTaskDelay` in the callback

**File:** `src/nrf52/api_functions.cpp:296-347`; `delay(1000)` at `:300`;
`memcpy((void*)&meshcom_settings, data, sizeof(s_meshcom_settings))` at `:319`; `save_settings()`
at `:322`.
**Severity: MED-HIGH.** **Status: CONC-17 in `docs/code-audit-20260712.md`; confirmed NOT fixed.**

`s_meshcom_settings` (`src/esp32/esp32_flash.h:8+`) is a ~400-byte struct with `char node_call[10]`,
`double node_lat/node_lon`, `float`s and `int`s. The `memcpy` is a plain byte copy from a prio-2
task. Readers include the `LORA` task (`encodeAPRS`, `checkMesh`, and the direct
`strcmp(destination_call, meshcom_settings.node_call)` at `lora_functions.cpp:1098`) and the loop
task (beacon build, display, web).

**Failure scenario.** The phone pushes a settings blob changing the callsign from `OE1ABC-12` to
`OE1XYZ-7`. The `LORA` task preempts mid-`memcpy` at byte 14 and immediately builds a relay frame:
`meshcom_settings.node_call` reads `OE1XYZ-12` — a callsign that belongs to nobody. That frame is
transmitted, relayed by neighbours, and shows up in every MHeard table on the mesh. The 8-byte
`double node_lat` can likewise be half-updated, producing a position on the wrong continent.
`save_settings()` running from the callback also flashes whatever partial state exists at that
moment.

Additionally `delay(1000)` (= `vTaskDelay(1000 ms)`) inside a BLE write callback blocks the
Bluefruit callback task for a full second, delaying every other Bluefruit callback (including
`disconnect_callback`).

**Fix.** Same as F2-11: enqueue the raw blob, apply it in `nrf52loop()`. Remove the `delay(1000)`.

---

### F2-13: `xSemaphoreGiveFromISR(g_task_sem, pdFALSE)` called from task context, with a scalar where a pointer is required

**File:** `src/nrf52/nrf52_ble.cpp:248`.
**Severity: LOW-MED (latent).**

`bleuart_rx_callback` runs in the Bluefruit **callback task**, not an ISR (contrast `:344` in the
same file, which correctly uses `xSemaphoreGive`). Two problems: the `FromISR` variant is being used
from a task, and `pdFALSE` is passed where `BaseType_t *pxHigherPriorityTaskWoken` is expected —
it compiles because `pdFALSE` is `0` which converts to a null pointer constant. FreeRTOS tolerates
`NULL` there, and `portASSERT_IF_INTERRUPT_PRIORITY_INVALID()` is skipped because `IPSR == 0`, so it
happens to work today. It will break on any port or FreeRTOS version that hardens either check, and
it loses the yield hint.

**Fix.** `xSemaphoreGive(g_task_sem);` — matching line 344.

---

### F2-14: `queueExtern` overwrites a slot whose `used` flag is still set

**File:** `src/extudp_functions.cpp:507-522` (producer), `:524-535` (consumer), `:47-58` (struct).
**Severity: MED.**

`docs/code-audit-20260626.md:87-125` (RACE-01) hardened `used` to
`std::atomic<bool>` with `release`/`acquire`, which is correct **as far as it goes** — but the
producer never inspects it:

```
struct externQueueEntry *entry = &externQueue[externQueueWrite];
… memcpy(entry->buffer, buffer, buflen); …
entry->used.store(true, std::memory_order_release);
externQueueWrite = (externQueueWrite + 1) % MAX_EXTERN_QUEUE;   // MAX_EXTERN_QUEUE == 2
```

With only two slots, and the producer being the nRF52 `LORA` task (prio 2, called from `OnRxDone`
at `lora_functions.cpp:701`) while the consumer is the loop task (prio 1) inside a blocking UDP
`sendExtern`, two packets arriving during one `flushExternQueue()` wrap the write index onto the
slot the consumer is currently transmitting from.

**Failure scenario.** `flushExternQueue` is at `:530` inside `sendExtern(…, externQueue[0].buffer, externQueue[0].buflen, …)`,
blocked in `UdpExtern.write()`. Two packets arrive; `OnRxDone` preempts twice, filling slot 1 then
slot 0. The consumer's in-flight `sendExtern` reads `externQueue[0].buffer` mid-`memcpy` → the JSON
posted to the external server is the head of packet A spliced with the tail of packet C, with
`buflen` from A. Then `used.store(false)` at `:532` clears the flag the producer just set → packet C
is silently dropped.

**Fix.** Have the producer check `if(entry->used.load(std::memory_order_acquire)) { drop; return; }`
before touching the slot, and have the consumer copy the entry out (or `used.store(false)` only for
the exact index it snapshotted). Better: replace with `xQueueSend`/`xQueueReceive`.

---

### F2-15: nRF52 — LoRa timing/stat state is written from the `LORA` task and reset from the loop with no synchronisation

**File:** `src/lora_functions.cpp:1228-1235` (`onrxdone_max_ms`, `onrxdone_warn_count++`),
reset at `src/nrf52/nrf52_main.cpp:1289-1291`; `iReceiveTimeOutTime` (`lora_functions.cpp:401,1241,1982`
vs `nrf52_main.cpp:1295,1319,…`); `csma_timeout` / `cad_attempt`
(`lora_functions.cpp:402,1242,2084-2085` vs `nrf52_main.cpp:1464`);
`stat_*` arrays (`lora_functions.cpp:1559,1612-1615,1620,1520-1521,2081-2082`).
**Severity: LOW-MED (observability, not packet correctness).**

`onrxdone_warn_count++` and `stat_tx_count[prio]++` are non-atomic RMWs across two preempting
contexts; `onrxdone_max_ms = 0` from the loop can erase a max that the `LORA` task is about to
overwrite. Result: the ONRXDONE/CHANNEL_UTIL/MC-STAT telemetry that this firmware relies on for
field diagnosis is itself unreliable on nRF52 — which matters because it is the evidence base for
the other findings.

More consequentially, `csma_timeout` is a plain `unsigned long` written by `OnRxDone` and read by
the loop's `if((millis() - iReceiveTimeOutTime) >= csma_timeout)` at `nrf52_main.cpp:1295`. A torn
read is impossible (32-bit aligned, single core) but a _stale_ read is not — the loop can use the
previous backoff for one iteration. Benign, but it should be documented rather than accidental.

**Fix.** For the stats, accept the imprecision but say so in a comment. For `csma_timeout` /
`iReceiveTimeOutTime`, mark them `volatile` at minimum (they are the only genuinely cross-context
scalars in the CSMA loop) — or move the whole CSMA decision into the `LORA` task.

---

### F2-16: `gKeyNum` is written by a GPIO ISR and read/cleared by the loop with no `volatile`

**File:** `src/nrf52/nrf52_main.cpp:320` (definition), `:324-348` (ISRs), `:1033`
(`attachInterrupt(MIDDLE_BUTTON, interruptHandle2, FALLING)`), `:1538,:1542,:1550,:1553,:1655`
(loop reads and writes).
**Severity: LOW-MED.**

`uint8_t gKeyNum = 0;` — no `volatile`, no atomic. The loop does
`if(gKeyNum == 2) { … gKeyNum = 0; }`. With `-Oz`, a compiler is entitled to keep `gKeyNum` in a
register across the surrounding straight-line code and never observe the ISR's store.

The ISR body itself (`if(gKeyNum == 0) gKeyNum = 1;`) is a read-modify-write, but since only one of
the three handlers is actually attached (`:1029` and `:1037` are commented out) there is no
ISR-vs-ISR race — only ISR-vs-task.

Note `docs/code-audit-fixes-20260627.md:31` records B4 as "pulseTimes ISR race — already `volatile`,
no change". That is correct for `gps_functions.cpp`, but `gKeyNum` is the same pattern and was never
looked at.

**Fix.** `volatile uint8_t gKeyNum` (single core: sufficient). Or move to an atomic exchange.

---

### F2-17: `is_receiving` gate in `checkRX()` is a TOCTOU on an atomic

**File:** `src/esp32/esp32_main.cpp:3706-3709`.
**Severity: LOW (currently unreachable).**

```
if(is_receiving) return -1;
is_receiving = true;
```

Load then store, not `compare_exchange`. On ESP32 both accesses are in `loopTask`, so it cannot
misfire today — but the shape is a trap for anyone who later moves `checkRX` into a task (which the
T5-ePaper port has already done, `src/t5-epaper/peri_lora.cpp:208`). Use
`bool expected=false; if(!is_receiving.compare_exchange_strong(expected,true)) return -1;` or drop
the atomic (see over-synchronisation section).

---

### F2-18: nRF52 — blocking console I/O from the `LORA` task, including from code commented as ISR

**File:** `src/lora_functions.cpp:2035-2047` (`OnHeaderDetect`), `:328-374`, `:398-399`,
`:1226-1253` and ~40 other `printfdeb` sites inside `OnRxDone`.
**Severity: LOW-MED.**

The comment at `:367-368` says "Log RX_LISTEN -> RX_PROCESS here (not in `OnHeaderDetect` ISR where
`Serial.printf` is unreliable on nRF52)". `OnHeaderDetect` is **not** an ISR — it is
`RadioEvents.PreAmpDetect` (`nrf52_main.cpp:946`), invoked from `RadioBgIrqProcess` in the `LORA`
task. The stated reason for the workaround is wrong, though the workaround itself is harmless.

The real issue: with `bLORADEBUG` on, `OnRxDone` performs dozens of `printfdeb` calls from a
priority-2 task. On nRF52 that is TinyUSB CDC, which can block when the host is not draining;
on ESP32 the same function routes through `MeshSerialClass::write` → `xSemaphoreTake(s_mutex, 0)` →
`::send()` (`net_console.cpp:246-258`). The `ONRXDONE_TIME` / `ONRXDONE_SLOW` instrumentation added
to _measure_ callback duration is itself the dominant contributor to that duration.

Also note `LORA_ISR_DEBUG` (guarding `:331,:337,:358,:364,:372`) is **defined nowhere in the repo**
(`docs/architecture/07-verification-infrastructure.md:527` V-03 says the same). Consequence for this
angle: the RX double-buffer overwrite detector at `:328` computes `_overwrite` and then discards it
— the one signal that would tell you the nRF52 double-buffer is being reused while in flight is
compiled out.

---

### F2-19: nRF52 — the `bSPI_ETH_Active` SPI-bus guard is advisory and depends on an undocumented priority invariant

**File:** `src/lora_functions.cpp:114-115,341-345,1969-1981`;
`src/nrf52/nrf52_main.cpp:1879,1891-1892,2276-2280,2301-2321`.
**Severity: LOW-MED.**

`bSPI_ETH_Active` is a `volatile bool` set by the loop task around W5100S transactions and tested by
the `LORA` task before `startRadioReceive()`. Between the test at `lora_functions.cpp:341` and the
SPI transaction inside `startRadioReceive()` there is no exclusion — the guard is a TOCTOU.

It happens to work only because the `LORA` task has _higher_ priority than the loop task, so the
loop can never preempt the `LORA` task into the middle of a radio transaction. That invariant is
established by a third-party library (`board.cpp:498`, `TASK_PRIO_NORMAL`) and is stated nowhere in
this repo. If the RAK13800 driver ever yields inside a transaction while `bSPI_ETH_Active` is still
false, or if anyone bumps the loop task priority, both chips drive MISO simultaneously.

Additionally, `if(bPendingRadioRx) { bPendingRadioRx = false; startRadioReceive(); }`
(`nrf52_main.cpp:1892` and three siblings) is a test-and-clear RMW; and that `startRadioReceive()`
is one of the very few **not** wrapped in `taskENTER_CRITICAL` (contrast `:1319`, `:1435`, `:1449`),
so it is the one call the F2-3 critical sections do not cover.

**Fix.** A single `SemaphoreHandle_t spiBusMutex` taken by both the Ethernet block and every
radio-touching helper, replacing both `bSPI_ETH_Active` and the `taskENTER_CRITICAL` wrappers of
F2-3. Document the LORA-task-priority invariant in `src/nrf52/nrf52_radio.h` either way.

---

### F2-20: T5-ePaper / T-Deck-Pro — unpinned tasks at priority 23–24 touch the shared LoRa state

**File:** `src/t5-epaper/peri_lora.cpp:171,176` (`lora_task`, `LORA_PRIORITY = configMAX_PRIORITIES-2` = 23),
`src/t5-epaper/peri_gps.cpp:77` / `src/t-deck-pro/peri_gps.cpp:78` (`gps_task`, prio 24),
`src/t-deck-pro/tdeck_pro.cpp:381`, `src/t5-epaper/t5epaper_main.cpp:671`;
`src/t5-epaper/peri_lora.cpp:208` calls `checkRX(true)`.
**Severity: MED for those boards (out of the main test matrix, but they compile and ship).**

These are created with `xTaskCreate` (not `…PinnedToCore`), so IDF schedules them on **either**
core. `lora_task` calls `checkRX(true)` → `OnRxDone` → the entire shared-state pipeline
(`ringBuffer`, `BLEtoPhoneBuff`, `RcvBuffer`, `pendingDisplayMsg`) from an **unpinned** task at
priority 23, concurrently with `loopTask` on core 1 at priority 1.

For those two variants every "ESP32 = single-context" conclusion in this report is void, and the
full nRF52 finding set (F2-4, F2-5, F2-14, F2-15) applies with the additional hazard of **true
cross-core parallelism** — `taskENTER_CRITICAL()` would not help even if it were used, and the
`double`s in `gpsData` / `posinfo_lat` / `posinfo_lon` can genuinely tear.

`src/code_review/code-audit-20260508.md:220` flags the `gps_task`/`double` case; the `lora_task`
case is not recorded anywhere.

**Fix.** At minimum pin these to core 1 (`xTaskCreatePinnedToCore(…, 1)`) so they share a core with
`loopTask` and the ESP32 analysis holds. Properly: route them through queues like `bleQueue`.

---

## Over-synchronisation

Everything below is **single-context on ESP32** and is currently paying for synchronisation it does
not need. All of these are `#if !defined(BOARD_RAK4630)`-shaped removals, or can be handled with a
type alias (`mc_atomic<T>` = `std::atomic<T>` on nRF52, plain `T` on ESP32) so the nRF52 build is
unaffected.

The argument for single-context is uniform and mechanical: on ESP32 the only ISR is
`setFlagReceive`/`setFlagSent` (`esp32_main.cpp:487,503`), whose entire body touches four atomics
and nothing else. `OnRxDone`/`OnTxDone`/`OnRxError`/`OnRxTimeout` are called only from
`checkRX()` (`:3778`) and `esp32loop()` (`:2275`), both `loopTask`. The NimBLE task's only reach
into shared state is `xQueueSend(bleQueue, …)` (`:359`). Therefore anything not named
`receiveFlag`/`transmittedFlag`/`bEnableInterrupt*` and not written by NimBLE is `loopTask`-only.

| Object                                                                             | file:line                                                            | Why it is single-context on ESP32                                                                                                    | Action                                                                                                                                                   |
| ---------------------------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `std::atomic<bool> scanFlag`                                                       | `esp32_main.cpp:473`                                                 | **Zero readers, zero writers** anywhere in the tree. ESP32 CAD uses blocking `radio.scanChannel()` (`:2380`).                        | **Delete.** Correct the audit trail (`code-audit-fixes-20260627.md:29`).                                                                                 |
| `std::atomic<bool> is_receiving`                                                   | `loop_functions.cpp:421`                                             | writers `:2355`, `:3709`, `:3866`, `OnRxDone`; readers `:3192`, `:3314`, `:3706`, `:2299` — all `loopTask`                           | plain `bool` on ESP32                                                                                                                                    |
| `std::atomic<bool> tx_is_active`                                                   | `:422`                                                               | written in `doTX` and `OnTxDone`, both `loopTask`                                                                                    | plain `bool` on ESP32                                                                                                                                    |
| `std::atomic<unsigned long> ch_util_rx_start`                                      | `:429`                                                               | **never written on ESP32** — the code says so itself at `esp32_main.cpp:3775`                                                        | remove from ESP32 build                                                                                                                                  |
| `std::atomic<unsigned long> ch_util_tx_start`                                      | `:430`                                                               | written `:2038`, `:2427`; read `:2031` — all `loopTask`                                                                              | plain                                                                                                                                                    |
| `std::atomic<unsigned long> ch_util_rx_accum` / `ch_util_tx_accum`                 | `:431-432`                                                           | `fetch_add` from `checkRX`/`OnRxDone` (loop), `exchange` from `esp32loop:1969` (loop)                                                | plain                                                                                                                                                    |
| `std::atomic<uint8_t> iWrite` / `iRead`                                            | `:384-385`                                                           | all 16 enqueue sites and `doTX` are `loopTask` on ESP32                                                                              | plain on ESP32; **keep on nRF52 and add a real lock there** (F2-4)                                                                                       |
| `std::atomic<uint8_t> loraWrite`                                                   | `:393`                                                               | `addLoraRxBuffer` called only from `OnRxDone` / GW-ACK path = `loopTask`                                                             | plain on ESP32                                                                                                                                           |
| `portMUX_TYPE displayMux` + both critical sections                                 | `lora_functions.cpp:122,129,138,148,157`; `esp32_main.cpp:1944,1957` | producer and consumer are both `loopTask`                                                                                            | **Delete on ESP32** — and it is actively harmful (F2-2)                                                                                                  |
| `volatile bool bPendingDisplayText` / `bPendingDisplayPos`                         | `lora_functions.cpp:109-110`                                         | set in `queueDisplay*` (loop), cleared in `esp32loop` (loop)                                                                         | plain `bool` on ESP32                                                                                                                                    |
| `volatile int transmissionState`                                                   | `esp32_main.cpp:459`                                                 | written by `doTX` (loop), read at `:2251` (loop). The ISR never touches it.                                                          | plain `int` on ESP32                                                                                                                                     |
| `volatile bool bSetLoRaAPRS`                                                       | `loop_functions.cpp:86`                                              | all five ESP32 accesses (`:1098,:1758,:2268,:2271`) are `loopTask`                                                                   | plain on ESP32; keep `volatile` on nRF52                                                                                                                 |
| `volatile bool bSPI_ETH_Active` / `bPendingRadioRx`                                | `lora_functions.cpp:114-115`                                         | nRF52-only feature (W5100S); ESP32 never sets them                                                                                   | `#if defined(BOARD_RAK4630)`                                                                                                                             |
| `taskENTER_CRITICAL()` around `cad_channel_busy = …; cad_done_flag.store(release)` | `nrf52/nrf52_main.cpp:390-394`                                       | both are already `std::atomic`; the store pair is only ever read as a snapshot under the _reader's_ critical section at `:1332-1338` | the critical section here is redundant with the atomics **or** the atomics are redundant with the critical section — pick one; currently paying for both |

Net: **14 objects** can drop their synchronisation on ESP32, including the one spinlock in the
codebase. Two of them (`scanFlag`, `ch_util_rx_start`) are dead on ESP32 entirely.

Counter-note, so this is not read as "remove all the atomics": `receiveFlag`, `transmittedFlag`,
`bEnableInterruptReceive`, `bEnableInterruptTransmit` **must stay atomic** (genuine ISR↔task), and
`iWrite`/`iRead`/`loraWrite`/`cad_*` **must stay atomic on nRF52** — but atomicity there is
necessary and _not sufficient_ (F2-4).

---

## Claims about docs/architecture

### `docs/architecture/01-system-overview.md` §"The global variable bus"

The section (lines 92-121) is **directionally right and mechanically wrong in one important place**.

**Accurate:**

- "423 externs, 260 in one header" — matches (`src/loop_functions_extern.h` is 377 lines with ~260
  `extern` declarations).
- "Only 12 of 423 globals are `std::atomic`" — matches the count in `loop_functions_extern.h`
  (`iWrite`, `iRead`, `loraWrite`, `is_receiving`, `tx_is_active`, `cad_in_progress`,
  `cad_done_flag`, `cad_double_check`, `ch_util_rx_start/tx_start/rx_accum/tx_accum` = 12), plus
  five more that are file-local (`esp32_main.cpp:461-473`) and `cad_channel_busy`
  (`nrf52_main.cpp:236`) and `externQueueEntry::used`.
- "one `portMUX_TYPE` (`displayMux`), one mutex (`net_console.cpp`), one queue (`bleQueue`, 5 slots)"
  — exactly right.

**Overstated — the sentence that most needs correcting:**

> "Everything else shared between the NimBLE task, the radio ISR callback and the main loop is
> unsynchronised by construction."

Two errors in one sentence.

1. **"the radio ISR callback"** — there is no such thing on ESP32. The ESP32 ISR is nine lines
   long and touches only atomics; every function named `On*Done` runs in `loopTask`. The doc's own
   pipeline diagram (line 32) labels `OnRxDone()` as reached "|IRQ|" from the radio, which is true
   for nRF52 and false for ESP32. This mislabelling is _load-bearing_: it is the reason
   `displayMux` exists on ESP32 at all (F2-2), and it is why `queueDisplayText` was designed as a
   deferred-work queue on a platform where the work was never in an ISR to begin with.
2. **"Everything else … is unsynchronised"** — true as stated, but it implies the exposure is
   uniform across MCUs. It is not. On ESP32 the vast majority of the 423 globals are
   **single-context by construction**, because `bleQueue` funnels the only other task's writes into
   `loopTask` and the web server is synchronous. The genuine ESP32 exposure is four NimBLE
   connection flags (F2-8) plus the ISR flag pair (F2-7). On nRF52 the exposure is enormous,
   because `OnRxDone` and `readPhoneCommand` both run in preempting tasks.

**Recommended correction.** Replace the sentence with something like:

> Concurrency correctness is manual, and the exposure is asymmetric between the two MCU families.
> On ESP32 the radio callbacks run in `loopTask` (`checkRX()` → `OnRxDone`), the only ISR sets four
> atomic flags, and `bleQueue` funnels the NimBLE task's writes into the loop — so most globals are
> single-context and the atomics on them are redundant. On nRF52 `OnRxDone` runs in the SX126x
> `LORA` task at priority 2 and `readPhoneCommand` runs in the Bluefruit callback task at priority
> 2, both preempting `loop()` at priority 1 — so the same globals are genuinely shared and mostly
> unprotected. Any "make it atomic" fix must be evaluated per MCU family.

The diagram at line 32 should be annotated `IRQ (nRF52) / polled flag (ESP32)`.

Also: line 89 claims 12 `std::atomic<*>` in the type table. That is the count in
`loop_functions_extern.h`, not "across all of `src/`" as the table header implies (the real total is
19). Minor.

### `docs/architecture/07-verification-infrastructure.md`

The concurrency-adjacent claims are **accurate and, if anything, understated**:

- **V-03** (line 527): "`LORA_ISR_DEBUG` guards 5 ISR-path traces in `lora_functions.cpp` but is
  defined nowhere." Confirmed. The doc rates the cost as "document + test env". It is worse than
  that: one of the five guarded blocks is the _only_ consumer of `_overwrite` at
  `lora_functions.cpp:328-333`, the nRF52 RX double-buffer collision detector. With
  `LORA_ISR_DEBUG` undefined, the firmware computes whether it just clobbered an in-flight RX
  buffer and then throws the answer away. Recommend upgrading V-03 and pulling that specific
  counter out from behind the macro into an always-on `stat_` counter.
- Line 61-62 calls these "the earliest ISR-path transitions" — same mislabel as 01: on ESP32 they
  are not on an ISR path at all, and on nRF52 they are in the `LORA` task, not the ISR. Cosmetic,
  but it propagates the same wrong mental model.
- The doc has **no section on concurrency verification**, which is the real gap. There is no
  stress test that exercises simultaneous LoRa RX + BLE write + UDP on nRF52, which is the exact
  configuration in which F2-4, F2-11 and F2-12 manifest. Given that the bench (line 495) is
  "ESP32-S3 + SX1262 ×2", and given that essentially every finding in this report is nRF52-specific,
  the bench as configured **cannot observe this class of bug at all**. That is worth stating
  explicitly in §"Coverage gaps": _the hardware bench validates the MCU family with the least
  concurrency exposure._

### Prior art cross-check (`docs/code-audit-*.md`, `docs/code-audit-20260712.md`)

Issues a doc claims are fixed, that are **not** fixed or **not** actually addressed in current source:

| Claim                                                                                                                                               | Source                                          | Reality at `1ba101f4`                                                                                                                                                                                                                                                                                                                                          |
| --------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| "B2 `scanFlag` → `std::atomic<bool>` ✅ done"                                                                                                       | `code-audit-fixes-20260627.md:29`               | Applied to a variable with **no readers and no writers** (`esp32_main.cpp:473`). The ESP32 CAD path is `radio.scanChannel()`. Item is vacuous. — F2-6                                                                                                                                                                                                          |
| "C1 `iWrite/iRead` → `std::atomic<uint8_t>` ✅ done" and "Correctness depends on every TU seeing the atomic type — verified via a clean full build" | `code-audit-fixes-20260627.md:33,101-102`       | The atomics are in place and the build check is sound, but the **enqueue sequence remains a multi-writer non-atomic RMW on nRF52** (`ringBuffer[iWrite][…]` × 3 + `addTxRingEntry`). The audit closed "RACE-04: volatile ring indices" while the actual defect — no mutual exclusion around slot fill — is untouched. — F2-4                                   |
| "RACE-05: `std::atomic` usage — PASS"                                                                                                               | `src/code_review/code-audit-20260508.md:205`    | The usage that exists is well-formed, but the audit did not check whether load/modify/store sequences _around_ the atomics are atomic. `advanceIReadPastEmpty` (`lora_functions.cpp:1479-1487`), `iRead = iReadBeforeAdvance` (`:1704,1745,1813`), `iWrite++; if(iWrite>=MAX) iWrite=0;` (`nrf_eth.cpp:641-643`, dead code) are all non-atomic RMW on atomics. |
| "RACE-01 externQueue `used` flag → atomic ✅"                                                                                                       | `code-audit-20260626.md:87-125`                 | `used` is now correctly `std::atomic<bool>` with release/acquire, **but the producer never reads it**, so the slot can be overwritten while in flight. — F2-14                                                                                                                                                                                                 |
| "RACE-01 fix: spinlock protects `pendingDisplayMsg` struct copy between ISR and main loop"                                                          | in-source comment, `lora_functions.cpp:120`     | The premise is false on ESP32 (no ISR on either side) and the implementation is harmful (heap allocation with interrupts disabled) on both. — F2-1, F2-2                                                                                                                                                                                                       |
| "Spin-wait removed: `readPhoneCommand` now runs in Main Loop, no cross-core conflict with `sendToPhone()` possible"                                 | in-source comment, `phone_commands.cpp:528-529` | True on ESP32, **false on nRF52** (`api_functions.cpp:254`). A guard was removed on the strength of a platform-specific claim stated as universal. — F2-11                                                                                                                                                                                                     |
| "B4 `pulseTimes` ISR race — already `volatile`, no change"                                                                                          | `code-audit-fixes-20260627.md:31`               | Correct for `gps_functions.cpp`. But the same pattern in `nrf52_main.cpp:320` (`gKeyNum`, ISR-written, no `volatile`) was never examined. — F2-16                                                                                                                                                                                                              |
| CONC-14 / 15 / 16 / 17 / 19 "CONFIRMED"                                                                                                             | `docs/code-audit-20260712.md:43-47,176-214`     | All five confirmed still open, verbatim, at HEAD. No regression; recording so the next pass does not re-derive them.                                                                                                                                                                                                                                           |

Genuinely fixed and correct (do not re-open): **B3** — the nRF52 CAD flags are now `std::atomic`
_and_ snapshotted under `taskENTER_CRITICAL` on both sides (`nrf52_main.cpp:390-394`, `:1332-1338`).
That is the one place in this codebase where a shared-state protocol is fully correct, and it is a
good template for the F2-4 fix.

---

## Summary counts

| Verdict                      | Count                                                            | Objects                                                                                                                                                                                                                                                                                                                                                                                                          |
| ---------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **RACE**                     | 9 classes                                                        | TX ring on nRF52 (`ringBuffer`+`iWrite`/`iRead`/`ringPriority`/`ringEnqueueTime`/`retryCount`); phone/UDP ring indices (`toPhoneWrite/Read`, `udpWrite/Read`, `ComToPhoneWrite/Read`); NimBLE connection flags across cores 0/1; `(bEnableInterruptReceive, receiveFlag)` pair; `g_task_event_type`; `meshcom_settings` + `textbuff_phone` on nRF52; `net_console` fd/mutex; `externQueue` slot reuse; `gKeyNum` |
| **OK** (correctly protected) | 4                                                                | `bleQueue`; nRF52 CAD flag set (atomics + symmetric critical sections); `ch_util_*_accum` RMW on nRF52; `pulseTimes`/`pulseIndex`                                                                                                                                                                                                                                                                                |
| **OVER-SYNCHRONISED**        | 14                                                               | `scanFlag` (dead), `is_receiving`, `tx_is_active`, `ch_util_rx_start` (dead on ESP32), `ch_util_tx_start`, `ch_util_rx_accum`, `ch_util_tx_accum`, `iWrite`, `iRead`, `loraWrite`, `displayMux` + both critical sections, `bPendingDisplayText/Pos`, `transmissionState`, `bSetLoRaAPRS` — all ESP32                                                                                                             |
| **single-context, unmarked** | remainder (~380 of the 423 externs on ESP32; far fewer on nRF52) | fine as-is                                                                                                                                                                                                                                                                                                                                                                                                       |
