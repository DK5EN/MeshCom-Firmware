# NimBLE boundary analysis (FORMAL-02 wave 4)

Analysis only. No build, no solver, no flashing was run. Everything below is read from the sources; "not verified" marks
what could not be checked by reading.

- Library: NimBLE-Arduino 2.2.3 (`NimBLE-Arduino/library.properties:2`), patched copy in `.pio/libdeps/<env>/NimBLE-Arduino`
  (checked in `E22-DevKitC`; markers AU-19, AU-20, NB-01..NB-07 present). Pinned in `platformio.ini:1923`.
- Path shorthand (all relative to the libdeps env dir):
  `CPP/` = `NimBLE-Arduino/src/`, `HS/` = `NimBLE-Arduino/src/nimble/nimble/host/src/`,
  `PORT/` = `NimBLE-Arduino/src/nimble/porting/nimble/src/`, `ESP/` = `NimBLE-Arduino/src/nimble/esp_port/`,
  `TR/` = `NimBLE-Arduino/src/nimble/nimble/transport/src/`.
- Effective configuration (ours): `platformio.ini:1970-1980`: 1 connection, `MSYS1_BLOCK_COUNT=4`,
  `MAX_PREP_ENTRIES=2`, `QUEUED_WRITE_TMO=5000`, host task stack 3072. Defaults that apply: preferred ATT MTU 255
  (`CPP/nimconfig.h:224`, mapped at `ESP/port/include/esp_nimble_cfg.h:284`), msys block size 292
  (`ESP/port/include/esp_nimble_cfg.h:26`), attribute value init length 20 (`CPP/NimBLEAttValue.h:42`),
  `BLE_GATT_CACHING` undefined (off), signed write on (`HS/ble_att.c:76`).
- Our side: `src/esp32/esp32_main.cpp` (onWrite `:491-504`, characteristic creation `:574-645`, queue `:2569`,
  loop drain `:4859-4864`), `src/ble_session.h:114-118` (`BleQueueItem`, 300 B + length + gen).

## 1. Data path of a phone write (ESP32)

All of it runs in the single NimBLE host task (`nimble_host`, name used at `esp32_main.cpp:4850`), from the HCI
callback queue to our `onWrite`.

| #   | Step                                  | Library function (file:line)                                                                                                       | What it does / limits                                                                                                                                                                                 |
| --- | ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Controller callback (controller task) | `ESP/esp-hci/src/esp_nimble_hci.c:139` `ble_hci_rx_acl`; `:148-156` retry loop; `host_rcv_pkt` `:182`                              | copies the ACL fragment into an mbuf from the transport ACL pool (`TR/transport.c:146-158`, `ACL_FROM_LL_COUNT=10`, `esp_nimble_cfg.h:1694`), retries until one is free (NB-03 changes the wait only) |
| 2   | Queue to host                         | `HS/ble_hs.c:701` `ble_hs_rx_data` -> `ble_mqueue_put`; `:244` `ble_hs_process_rx_data_queue`                                      | host task dequeues                                                                                                                                                                                    |
| 3   | HCI header, connection lookup         | `HS/ble_hs_hci_evt.c:1112` `ble_hs_hci_evt_acl_process`                                                                            | `hdh_len` must equal mbuf length (`:1139`); no connection: packet dropped (`:1150`); `rx_cb` runs outside the host lock (`:1165`)                                                                     |
| 4   | L2CAP reassembly                      | `HS/ble_l2cap.c:350` `ble_l2cap_rx`, `:262` `ble_l2cap_rx_payload`                                                                 | SDU length > current ATT MTU: peer disconnected (`:406-412`); more data than announced: dropped (`:280`); 30 s fragment timeout (`ble_hs_conn.c:528-535`) terminates the link                         |
| 5   | ATT dispatch                          | `HS/ble_att.c:487` `ble_att_rx`                                                                                                    | strips the opcode (`:517`), dispatches by opcode table (`ble_att.c:47-80`)                                                                                                                            |
| 6a  | Write request (0x12)                  | `HS/ble_att_svr.c:2243` `ble_att_svr_rx_write`                                                                                     | pullup of the 2 B handle, allocates the response first (`:2287`), strips the base (`:2293`), writes at offset 0 (`:2295`), sends a response or an error                                               |
| 6b  | Write command (0x52)                  | `HS/ble_att_svr.c:2309` `ble_att_svr_rx_write_no_rsp`                                                                              | same without response; every error is dropped silently                                                                                                                                                |
| 6c  | Prepare write (0x16)                  | `HS/ble_att_svr.c:2723` `ble_att_svr_rx_prep_write` -> `:2666` `ble_att_svr_insert_prep_entry` -> `:2486` `ble_att_svr_prep_alloc` | one entry from a pool of `MAX_PREP_ENTRIES` (2) plus one msys mbuf per entry; value copied with `os_mbuf_appendfrom` (`:2686`); entries sorted by handle and offset (`:2508`)                         |
| 6d  | Execute write (0x18)                  | `HS/ble_att_svr.c:2819` `ble_att_svr_rx_exec_write` -> `:2626` `ble_att_svr_prep_write` -> `:2545` validate, `:2587` extract       | list taken off the connection under the lock (`:2886`); flags != 0 executes, flags == 0 cancels (`:2890`); entries of one handle are concatenated into one mbuf chain (`:2610`)                       |
| 6e  | Signed write (0xD2)                   | `HS/ble_att_svr.c:2375-2403`                                                                                                       | needs a stored peer CSRK (`:2375-2381`), see F8                                                                                                                                                       |
| 7   | Permission check, handle lookup       | `HS/ble_att_svr.c:564` `ble_att_svr_write`, `:285` `ble_att_svr_check_perms`, `:596` `ble_att_svr_write_handle`                    | only `BLE_ATT_F_WRITE` matters: write request and write command are both accepted on a `WRITE`-only characteristic (`HS/ble_gatts.c:263-264`); our characteristics set no ENC/AUTHEN flag             |
| 8   | GATT access                           | `HS/ble_gatts.c:437` `ble_gatts_val_access`, `:503` `ble_gatts_chr_val_access`                                                     | hands the mbuf chain (`ctxt->om`) to the access callback; no offset is passed on a write                                                                                                              |
| 9   | NimBLE-Arduino access callback        | `CPP/NimBLEServer.cpp:610` `NimBLEServer::handleGattEvent`, write case `:639-662`                                                  | see below                                                                                                                                                                                             |
| 10  | Value store                           | `CPP/NimBLECharacteristic.cpp:313-316` `writeEvent` -> `CPP/NimBLEAttValue.cpp:107` `setValue` -> `:115` `append`                  | `setValue` resets the length, `append` grows the heap buffer (`realloc` to `new_len+1`, `:129`), copies, NUL-terminates (`:146-152`)                                                                  |
| 11  | Our callback                          | `CPP/NimBLECharacteristic.cpp:315` `m_pCallbacks->onWrite` -> `esp32_main.cpp:491`                                                 | `getLength()` (`CPP/NimBLELocalValueAttribute.h:66`) and `getValue()` (`:73`, returns a copy)                                                                                                         |

`handleGattEvent` write case (`CPP/NimBLEServer.cpp:639-662`):

- `maxLen = val.max_size()` (`:640`): the value's maximum, set by `createCharacteristic(..., max_len)`
  (`CPP/NimBLEAttValue.cpp:35` clamps to 512). Ours: `BLE_CHR_MAX_LEN = MAX_MSG_LEN_PHONE = 300`
  (`esp32_main.cpp:575`, `:629`, `:643`).
- First mbuf `om_len > maxLen` -> `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN` (`:641-643`). A stack buffer `buf[maxLen]`
  (variable length array, 300 B on the 3072 B host stack, `:645`), the chain is flattened into it, each next mbuf checked
  against `maxLen` (`:650-655`). Then `writeEvent(buf, len)` (`:660`). `buf` is gone after the return; the stored copy in
  `m_value` is what `onWrite` reads.
- A zero-length write passes (`len == 0`); `setValue(.., 0)` empties the value (NB-01 clears the first byte).

MTU and sizes:

- MTU exchange: `HS/ble_att_svr.c:771` `ble_att_svr_rx_mtu` answers with `chan->my_mtu` (255), then
  `ble_att_set_peer_mtu` (`HS/ble_att.c:440`, floor 23) and `TXED_MTU` flag (`ble_att_svr.c:809`). The effective
  MTU is `min(my, peer)` after an exchange, 23 before (`HS/ble_att.c:450-467`). It is also the upper bound for an incoming
  SDU (`HS/ble_l2cap.c:303-314`, `:406`). So at most 255.
- Largest single write value: MTU-3 = 252. Largest prepare value: MTU-5 = 250 (prepare header 4 B + opcode).
- A 300 B frame therefore needs the long write (prepare x2 + execute) and `2 * (MTU-5) >= 300`, i.e. MTU >= 155
  (`platformio.ini:1977-1978`; with MTU 23 a 300 B frame needs 17 prepares: the third gets `PREPARE_QUEUE_FULL`).
- Prepare entry size: msys block 292, `omp_databuf_len = 292 - sizeof(os_mbuf)` (`PORT/os_mbuf.c:244`), minus pkthdr 8 B and
  leading space 9 B (`HS/ble_hs_mbuf.c:91-96`, `LEGACY_VHCI=1` at `CPP/nimconfig.h:329-330`) leaves about 259 B for the
  first mbuf: one block per 250 B entry. Arithmetic from struct sizes, not measured on target (not verified).
- Value buffer: `NimBLEAttValue` heap buffer, initial 20 B (`CPP/NimBLEAttValue.cpp:34`), grows by `realloc` to the
  largest value written so far (capacity only grows, `:136-138`), never above `m_attr_max_len` (300).
- Prepare validation checks the concatenated length against 512 (`HS/ble_att_svr.c:2575`), not 300. The 300 limit is
  enforced afterwards by `handleGattEvent` (error at execute time).

## 2. What the patches change and what stays open

Patches that touch the write path (marker lines in the patched copy):

| Patch | Where                                                        | Effect on the write path                                                                                                                                                                                      |
| ----- | ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| NB-01 | `CPP/NimBLEAttValue.cpp:109`                                 | `setValue` clears the first byte with the length: an empty write no longer leaves the old frame readable. Null-guarded (the store was null after a failed first calloc). `onWrite` returns on length 0 anyway |
| NB-02 | `CPP/NimBLEAttValue.cpp:127-138`                             | capacity raised only after a successful `realloc`: no write past the block after a failed grow                                                                                                                |
| NB-03 | `ESP/esp-hci/src/esp_nimble_hci.c:154`                       | ACL-full retry waits 10 ms instead of one tick. The loop itself is still unbounded (F5)                                                                                                                       |
| AU-19 | `HS/ble_hs.c:458`                                            | no `callout_deinit` in `ble_hs_timer_reset`: the host timer (it carries the prepare-write and fragment timeouts via `ble_hs_conn_timer`) can no longer be called through a zeroed callback during a stop      |
| AU-20 | `HS/ble_hs_flow.c:273`                                       | flow-control timer init/deinit pairing (leak of an esp_timer per pause/resume). Not on the data path                                                                                                          |
| NB-04 | `CPP/NimBLEDevice.cpp:971-990`                               | `deinit(true)` frees server and characteristics only after a successful host stop: the GATT table (`arg` of `handleGattEvent` is the characteristic pointer, `CPP/NimBLEService.cpp:151,160`) cannot dangle   |
| NB-05 | `CPP/NimBLEDevice.cpp:844-933`                               | init failure unwinds and the sync wait is bounded: no half-started stack that still accepts writes without a server                                                                                           |
| NB-06 | `CPP/NimBLEDevice.cpp:836`                                   | old host task no longer deletes the new one: host task identity stable, so "single host task" holds for our static `item`                                                                                     |
| NB-07 | `HS/ble_hs_stop.c:143`                                       | stop counter reset: pause time only. Not on the data path                                                                                                                                                     |
| (cfg) | `platformio.ini:1970-1980`, `esp32_main.cpp:574-645,491-504` | NimBLE #1 (explicit 300 B attribute length, static `item`), #7 (2 prepare entries, 5 s). Not patches, configuration and src                                                                                   |

Remaining risks (unpatched), each with evidence:

| Area                                 | State                                                                                                                                                                                                                                                                                                                                       | Verdict                               |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| Length checks                        | request, command, execute: `handleGattEvent` rejects `> 300` (first mbuf and chain, `NimBLEServer.cpp:641,652`); `buf` on the stack is exactly `maxLen`. `prep_validate` allows up to 512 (`ble_att_svr.c:2575`) but the total per write is bounded by 2 entries x 250 B = 500 B and rejected at execute. Library default 512 B VLA is gone | bounded; host stack margin unmeasured |
| Write > 300 B                        | write request: error 0x0D to the phone. Write command: dropped, no counter, no feedback. Long write: execute returns the error. `onWrite`'s own `> MAX_MSG_LEN_PHONE` guard (`esp32_main.cpp:498`) is unreachable                                                                                                                           | silent for commands (F2)              |
| Prepare queue limits                 | pool of 2 entries, global, one connection (`ble_att_svr.c:3195-3205`). Third prepare: `PREPARE_QUEUE_FULL` (`:2492`). The two entries of an abandoned long write stay until execute/cancel, the 5 s timeout or the disconnect (`ble_hs_conn.c:247`). A new long write for the same handle after a queue-full error joins them               | spec-conforming client duty           |
| Prepare timeout                      | 5 s after the last prepare the **connection is terminated**, the queue is not just cleared (`ble_hs_conn.c:545-555`). Unit: `ble_npl_time_get() + 5000` (`ble_att_svr.c:2713`), i.e. 1 tick = 1 ms assumed (not verified per board)                                                                                                         | F4                                    |
| mbuf handling                        | msys pool 4 blocks: 2 prepare entries + response/notify mbufs; failure returns `INSUFFICIENT_RES` / `PREPARE_QUEUE_FULL` (`ble_att_svr.c:2500,2699`), no hang. Receive mbufs come from the separate ACL pool (10)                                                                                                                           | bounded, error to phone               |
| `os_mbuf_prepend` result             | prepare response ignores the return value (`ble_att_svr.c:2807-2808`); safe only while the head mbuf has leading space (the stripped HCI/L2CAP/ATT headers: 9 B)                                                                                                                                                                            | latent, F7                            |
| Allocation failure on a write        | `setValue` failure leaves length 0 (NB-01/02); the `getValue()` copy failure leaves a null pointer with the live length (F1)                                                                                                                                                                                                                | **F1**                                |
| Disconnect during a write            | everything runs in the host task: a write completes (and is stamped with the old generation) before the disconnect event; prepare entries are freed on disconnect (`ble_hs_conn.c:247`); our `bleQueueNewSession` makes in-flight items stale (`ble_session.h:135-143`, `esp32_main.cpp:363-371`)                                           | closed                                |
| Disconnect/pause with the stack down | NB-04 plus `bleLifeLock` for loop-side calls; `onWrite` takes no lock (`esp32_main.cpp:520-523`)                                                                                                                                                                                                                                            | closed by NB-04                       |
| Security                             | no ENC/AUTHEN flags, bonding off (`esp32_main.cpp:594`): any central can write. Known and decided upstream-side (`docs/architecture/08-defect-catalogue.md:382`)                                                                                                                                                                            | out of scope                          |

## 3. Boundary contract

### ESP32 `onWrite` (`esp32_main.cpp:491`)

| Assumption in onWrite                                 | Library guarantee                                                                                                                                                                                                                                                       | Holds?                                                                             |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| length delivered `<= 300`                             | `handleGattEvent` rejects more (`NimBLEServer.cpp:641,652`) and `NimBLEAttValue::append` refuses `len > max` (`NimBLEAttValue.cpp:120`)                                                                                                                                 | yes (two checks)                                                                   |
| length `0` possible and harmless                      | `setValue(.., 0)` empties the value (`:107-112`); `onWrite` returns                                                                                                                                                                                                     | yes                                                                                |
| one write = one callback                              | write request/command: yes. Long write: one callback per handle after execute (`ble_att_svr.c:2587-2620`, one `ble_att_svr_write` per handle). Two handles in one queue: two callbacks                                                                                  | yes, if the app writes one frame per ATT write (app protocol, not verifiable here) |
| `getLength()` and `getValue()` describe the same data | both read `m_value` in the same task right after `setValue`; no other task writes the RX characteristic value (`pRxCharacteristic` is local to `bleStackStart`, no `setValue` on it)                                                                                    | yes, but see next row                                                              |
| `getValue()` data pointer valid for `length` bytes    | `getValue()` returns a **copy** (`NimBLELocalValueAttribute.h:73`, copy constructor `NimBLEAttValue.h:140` -> `deepCopy` `NimBLEAttValue.cpp:88-104`). On a failed `realloc` the copy has a null pointer and length 0 while `getLength()` still reports the live length | **no (F1)**                                                                        |
| temporary lives long enough for `memcpy`              | the temporary lives to the end of the full expression (`memcpy(item.data, getValue(), n)`)                                                                                                                                                                              | yes                                                                                |
| data pointer lifetime across the callback             | only the copy; the ATT mbuf and `buf` are gone after `handleGattEvent`                                                                                                                                                                                                  | yes (we copy)                                                                      |
| task context: single host task, never re-entered      | `ble_att_svr_write_local` (the only other path to the access callback) is not called by NimBLE-Arduino or by us (`grep`); all rx runs in `ble_hs_process_rx_data_queue` in the host task                                                                                | yes                                                                                |
| `static BleQueueItem item` race free                  | follows from the row above; `xQueueSend` copies (`esp32_main.cpp:503`)                                                                                                                                                                                                  | yes                                                                                |
| `bleQueue` valid                                      | created before `bleStackStart` (`esp32_main.cpp:2569` < `:2584`), never deleted; the result of `xQueueCreate` is not checked                                                                                                                                            | yes unless the create fails (F2)                                                   |
| stack use                                             | 300 B VLA + `NimBLEConnInfo` + call chain on a 3072 B stack; `[BLE ];hoststack;free` exists (`esp32_main.cpp:4852`, only with `--bledebug`), not measured after 300 B long writes                                                                                       | not verified                                                                       |

### nRF52 Bluefruit (`bleuart_rx_callback`, `src/nrf52/nrf52_ble.cpp:397-423`)

Framework paths: `~/.platformio/packages/framework-arduinoadafruitnrf52/libraries/Bluefruit52Lib/src/services/BLEUart.cpp`
(`BLEUart.cpp`), `.../BLECharacteristic.cpp`, `.../cores/nRF5/utility/AdaCallback.c`, `adafruit_fifo.cpp`.

| Assumption in the callback               | Library behaviour                                                                                                                                                                                                                                                                                                                                                                | Holds?                               |
| ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| one write = one item                     | **No.** `bleuart_rxd_cb` appends every write to a byte FIFO (`BLEUart.cpp:88`), then calls our callback (`:104`). Our callback drains up to 300 B (`nrf52_ble.cpp:408`). Two writes before the callback task runs: first callback reads both (merge), second reads 0 (`:409` comment)                                                                                            | no (stream, modelled in wave 3)      |
| nothing is lost                          | FIFO depth 256 (`BLEUart.h:45`, `BLEUart g_ble_uart;` `nrf52_ble.cpp:63`). A write that does not fit is truncated (`wrcount < len`, `BLEUart.cpp:90-96`); the overflow callback is not set (`grep setRxOverflowCallback src`: none); the log line is compiled out without `CFG_DEBUG` -> truncated frame, no counter (F10)                                                       | no                                   |
| length `<= 300`                          | FIFO is 256 deep, `read(.., 300)` returns at most 256; RXD max length is the configured max MTU, 250 (`nrf52_ble.cpp:102` -> `BLEUart.cpp:179`): longest frame the phone can send is 250 B (ESP32: 300)                                                                                                                                                                          | yes; platform asymmetry 250 vs 300   |
| callback context: deferred callback task | `_rxd.setWriteCallback(.., true)` (`BLEUart.cpp:175`, `setRxCallback` default `deferred = true`, `BLEUart.h:66`): `ada_callback` copies the data and runs the callback in the "Callback" task (`AdaCallback.c:102-130`). If `rtos_malloc` fails it returns false and `BLECharacteristic.cpp:538-541` calls it **inline** in the SoftDevice event context                         | mostly (OOM falls back, F11)         |
| data pointer valid                       | `data` points to the malloc copy, freed after the callback (`AdaCallback.c:69-71`); `bleuart_rxd_cb` copies into the FIFO immediately                                                                                                                                                                                                                                            | yes                                  |
| FIFO access race free                    | `Adafruit_FIFO` locks a mutex per call (`adafruit_fifo.cpp:96,132,157,199`); writer (callback task) and reader (our callback, same task) are the same task                                                                                                                                                                                                                       | yes                                  |
| long writes                              | the Bluefruit long-write buffer (`BLECharacteristic.cpp:452-500`, 1024 B, unchecked `rtos_malloc`, no free on cancel) is only used for characteristics with a write-authorize callback; RXD has none (`wr_auth` only set with a callback, `BLECharacteristic.cpp:205`), so the SoftDevice handles long writes itself (closed source, not verified how it delivers the fragments) | dead for us; SoftDevice not verified |
| lost event                               | `ada_callback_queue`: when the queue is full and the resize fails the item is dropped with a `LOG_LV1` line only (`AdaCallback.c:83-97`)                                                                                                                                                                                                                                         | silent (F11)                         |

## 4. Proof plan (ESBMC 8.7, `specs/cbmc/`, runner `run_cbmc.sh`)

Conventions are those of the existing groups (`specs/cbmc/README.md`): real sources compiled, `--32`, pass jobs with
unwinding assertions, every pass has a red twin (witness or code mutation by `sed` on a copy in `gen/`), a real defect is an
expected-red `*_today_*` job until fixed.

### What can be checked as is

- **C code of the host (`ble_att_svr.c`, `ble_l2cap.c`, `os_mbuf.c`, `os_mempool.c`)**: plain C, no RTOS calls in the
  chosen functions; feasible. The compile environment is the cost: the headers pull `nimble_npl.h` -> FreeRTOS NPL
  (`porting/npl/freertos`) and `syscfg.h`. Plan: a harness include dir ahead of the library with a stub `nimble_npl_os.h`,
  `sdkconfig.h`, and the production values as `-D` (`-DMYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES=2
-DMYNEWT_VAL_BLE_ATT_SVR_QUEUED_WRITE_TMO=5000 -DMYNEWT_VAL_MSYS_1_BLOCK_COUNT=4`). Whether `ble_att_svr.c` compiles
  unmodified under ESBMC with such stubs is not verified (it is 3370 lines with many externs; ESBMC treats unresolved
  externs as nondeterministic).
- **Real `os_mbuf.c` and `os_mempool.c`** instead of an mbuf stub: the library is the adversary, so chain splitting, `adj`,
  `append`, `concat`, `pullup`, `prepend` must be the real code. Pools are static arrays (4 msys blocks of 292 B, 2 prepare
  entries, a few ACL blocks) built by the harness with the real `os_mempool_init` and `os_mbuf_pool_init`.
- **Stubs needed**: `ble_hs_lock/unlock/locked_by_cur_task` (no-ops, optional nesting counter), `ble_hs_conn_find` and
  `ble_hs_conn_find_assert` (one static `ble_hs_conn` with one `ble_l2cap_chan`), `ble_att_conn_chan_find`, `ble_att_chan_mtu`
  or the real one with `my_mtu = 255` and nondeterministic `peer_mtu` in 23..527, `ble_att_tx`/`ble_l2cap_tx` (record
  and free the mbuf), `ble_hs_timer_resched`, `ble_npl_time_get` (nondeterministic, monotonic), `ble_gap_terminate_with_conn`
  (sets a flag), `ble_gap_mtu_event`, `ble_hs_mbuf_att_pkt/l2cap_pkt` real (they need the msys pool registered).
- **C++ `NimBLEAttValue.cpp`** (166 lines): needs only `nimconfig.h` (-> `sdkconfig.h`, `ext_nimble_config.h`), `nimble_npl.h`
  (for `ble_npl_hw_enter_critical`, stub to nothing) and `NimBLELog.h` (`-DCONFIG_NIMBLE_CPP_LOG_LEVEL=0` makes the macros
  `(void)tag`). Feasible. ESBMC is expected to model `malloc/realloc/calloc` failure by default (the existing README says
  nothing; check that `--force-malloc-success` is not set; not verified).
- **`NimBLEServer::handleGattEvent` write case**: the file (1028 lines) cannot be compiled whole (needs `NimBLEDevice.h` and the
  ESP headers). Feasible by extraction: `sed -n` between two anchors (`case BLE_GATT_ACCESS_OP_WRITE_DSC:` ... `default:`,
  `NimBLEServer.cpp:638-665`) into `gen/` like the `mut_code_*` jobs, wrapped in a harness function with a stub
  `pAtt->writeEvent` that forwards to a real `NimBLEAttValue`. The extraction is a text copy at build time, not a hand copy;
  `specs/drift.py` should list the line anchors.
- **Our `onWrite`**: not compilable (inside the 4900-line `esp32_main.cpp`). The 8-line body has to move into a seam in
  `src/ble_session.h` first (like the other FORMAL-02 refactors), for example
  `bool bleItemFromWrite(BleQueueItem &it, const uint8_t *p, size_t n)` (false for null, 0, `> MAX_MSG_LEN_PHONE`), called
  from `onWrite` with the copy of F1's fix. The proof then runs the seam with the real `NimBLEAttValue` copy semantics.
- **Adafruit FIFO** (`adafruit_fifo.cpp`, plain C++, mutex calls stubbed) and `BLEUart::bleuart_rxd_cb` (extracted like the
  write case, 12 lines): feasible.

### Both directions

1. Library as adversary for our code: the seam is called with `(p, n)` where `p` and `n` come from the real
   `NimBLEAttValue` copy after a nondeterministic `setValue(data, len)` (len 0..300, then 0..512 to exercise the max
   check) under nondeterministic allocation failure. Property: no read of a null or short buffer, `item.length` in
   0..300, nothing queued on failure.
2. Our code as harness for the library: the access callback of the ATT attribute registered with the real
   `ble_att_svr_register` is the extracted `handleGattEvent` + real `NimBLEAttValue` + the seam. Nondeterministic input
   is a sequence of at most 3 ATT PDUs (write request, command, prepare, execute, cancel) with nondeterministic handle
   (2 attributes), offset, value length (0..250), MTU (23..255 effective, peer 23..527), and nondeterministic mbuf
   fragmentation of each PDU (1..3 mbufs). Properties: every callback has `len <= 300`; the delivered bytes equal the
   written bytes in order; after execute/cancel/disconnect the prepare pool and the msys pool are back to full (no leak);
   third prepare returns `PREPARE_QUEUE_FULL` and leaves the list unchanged; no callback after a cancel.

### Job list (proposal)

Costs are guesses, calibrated on the existing runs (`specs/cbmc/RESULTS.md`: simple jobs 10-15 s and about 1 GB, the largest
job 1452 s and 6.7 GB). Nothing below was run.

| Group (dir)                  | Job                       | Kind         | Content                                                                                                                                 | Bound                                               | Cost (guess)   | Feasible                        |
| ---------------------------- | ------------------------- | ------------ | --------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------- | -------------- | ------------------------------- |
| `nimble_attvalue`            | `pass_setvalue`           | pass         | `setValue`/`append` invariants: `size <= capacity <= max`, `data[size] == 0`, no write past capacity                                    | len 0..300, 3 operations, malloc may fail           | quick, < 2 GB  | yes                             |
|                              | `pass_seam`               | pass         | seam over the real copy: no null/short read, `length <= 300`                                                                            | as above, after the F1 fix                          | quick          | yes (needs the seam)            |
|                              | `today_onwrite_null`      | expected red | today's `onWrite` expression: null source of `memcpy` when the copy allocation fails (F1)                                               | as above                                            | quick          | yes (flips with the fix)        |
|                              | `witness_oom_len0`        | witness      | failed grow gives length 0 (the silent drop path)                                                                                       | as above                                            | quick          | yes                             |
|                              | `mut_code_nb02`           | mutation     | NB-02 reverted (`m_capacity` raised before `realloc`) -> write past the block                                                           | 2 writes                                            | quick          | yes                             |
|                              | `mut_code_nb01`           | mutation     | NB-01 reverted -> `data[0]` stale after an empty write                                                                                  | 2 writes                                            | quick          | yes                             |
| `nimble_gatts_write`         | `pass_chain`              | pass         | extracted write case: for any chain of 1..3 mbufs with any lengths, either an error or `len <= maxLen` and `buf` untouched beyond `len` | maxLen 300 (quick: 24), chain total 0..600          | quick          | yes (extraction)                |
|                              | `witness_reject`          | witness      | `INVALID_ATTR_VALUE_LEN` reachable on both checks                                                                                       | same                                                | quick          | yes                             |
|                              | `mut_code_nochain`        | mutation     | chain check (`:652`) removed -> `buf` overflow                                                                                          | same                                                | quick          | yes                             |
|                              | `mut_code_nofirst`        | mutation     | first-mbuf check (`:641`) removed -> `buf` overflow                                                                                     | same                                                | quick          | yes                             |
| `nimble_att_write`           | `pass_write_req`          | pass         | real `ble_att_svr_rx_write` -> end to end callback; response or error, mbufs freed                                                      | PDU 0..252 B, 1..3 mbufs, MTU 23..255               | heavy, 4-8 GB  | likely (compile env unverified) |
|                              | `pass_write_cmd`          | pass         | real `ble_att_svr_rx_write_no_rsp`: error is dropped, no leak, no callback with `> 300`                                                 | same                                                | heavy          | likely                          |
|                              | `pass_prep_exec`          | pass         | prepare x2 + execute, one handle: one callback, length = sum, order by offset, `PREPARE_QUEUE_FULL` on the third, pools restored        | offsets nondeterministic, value 0..250, MTU 23..255 | heavy, 6-12 GB | likely, the largest job         |
|                              | `pass_prep_cancel`        | pass         | prepare + cancel (flags 0), disconnect-style `ble_att_svr_prep_clear`: pools restored                                                   | same                                                | heavy          | likely                          |
|                              | `pass_two_handles`        | pass         | prepares for two handles interleaved: one callback per handle, each contiguous                                                          | 2 handles, 3 PDUs                                   | heavy          | likely                          |
|                              | `pass_prepend_leading`    | pass         | `ble_att_svr.c:2807` is safe when the head mbuf has >= 9 B leading space (assumption stated)                                            | any prepare                                         | quick          | yes                             |
|                              | `witness_long_write`      | witness      | a 300 B frame is delivered at MTU 155 (and not at MTU 154)                                                                              | MTU 23..255                                         | heavy          | likely                          |
|                              | `witness_queue_full`      | witness      | `PREPARE_QUEUE_FULL` reachable                                                                                                          | 3 prepares                                          | quick          | yes                             |
|                              | `mut_code_novalidate`     | mutation     | offset-continuity check in `prep_validate` removed -> gap/overlap data delivered                                                        | 2 prepares                                          | heavy          | likely                          |
|                              | `mut_code_noprepfree`     | mutation     | `ble_att_svr_prep_free` leaves the mbuf -> pool conservation fails                                                                      | 2 prepares                                          | quick          | yes                             |
|                              | `today_prepend_noleading` | expected red | no leading space -> `os_mbuf_prepend` returns a new head, the ignored result leaves `txom` stale (F7)                                   | 1 prepare                                           | quick          | yes (library unmodified)        |
| `nimble_l2cap_rx` (optional) | `pass_reassembly`         | pass         | `ble_l2cap_rx`: SDU > MTU never delivered (disconnect flag), completed SDU length = header length, no mbuf leak                         | 3 fragments, MTU 23..255                            | heavy          | likely                          |
|                              | `witness_oversize`        | witness      | oversize path reachable                                                                                                                 | same                                                | quick          | yes                             |
|                              | `mut_code_nolenchk`       | mutation     | `:406` check removed                                                                                                                    | same                                                | quick          | yes                             |
| `bluefruit_fifo` (optional)  | `pass_fifo`               | pass         | `Adafruit_FIFO` write/read depth 256: counts consistent, `read(300)` <= 256                                                             | 3 operations, writes 0..250                         | quick          | yes                             |
|                              | `witness_truncate`        | witness      | `wrcount < len` reachable with two writes <= 250 B (F10)                                                                                | same                                                | quick          | yes                             |
|                              | `witness_merge`           | witness      | two writes, one read returns both (stream semantics)                                                                                    | same                                                | quick          | yes                             |
|                              | `mut_fifo_cap`            | mutation     | `count = min(count, remaining())` removed                                                                                               | same                                                | quick          | yes                             |

Totals: 22 core jobs (`nimble_attvalue` 6, `nimble_gatts_write` 4, `nimble_att_write` 12), 7 optional (`nimble_l2cap_rx` 3,
`bluefruit_fifo` 4). Quick tier: 15 core jobs, heavy tier (memory budget 12288 MB): 7 core (`pass_write_req`,
`pass_write_cmd`, `pass_prep_exec`, `pass_prep_cancel`, `pass_two_handles`, `witness_long_write`, `mut_code_novalidate`).
Order of work: seam and F1 first (`nimble_attvalue`, quick, no ATT stubs), then `nimble_gatts_write`, then `nimble_att_write`
(the compile environment is the one real unknown).

### Not feasible with ESBMC (or not worth it)

- Task interleavings (host task against loop task, controller task against host task): ESBMC has no usable model of
  FreeRTOS tasks; the queue and the generation stamp stay with the TLA+ model `specs/tla/bletask.tla` (wave 3).
- Heap fragmentation and real `malloc` failure patterns: only "may fail at each call" is modelled; whether a 301 B `realloc`
  fails on the target needs the heap measurement on the bench.
- Timing: the prepare timeout (`ble_hs_conn_timer`) and the 30 s fragment timeout can be checked as pure functions of
  nondeterministic tick values, but the ticks-per-ms assumption and the supervision timeout interplay are not provable here.
- Controller/VHCI behaviour, ACL pool exhaustion against controller timing (F5), host stack depth (3072 B): measurement,
  not proof.
- SoftDevice (nRF52) is a binary: the delivery of long-write fragments to `BLE_GATTS_EVT_WRITE` cannot be checked. Only the
  Bluefruit/Adafruit C++ above it can.
- The whole of `ble_gatts.c` (3265 lines) and the security manager: out of scope; only `ble_gatts_val_access` (25 lines) is
  on the write path and is covered through the end-to-end callback.

## 5. Findings

Severity: medium = can reset or corrupt a running node; low = loss or hardening; info = documented, no action needed now.

| ID  | Sev    | Finding                                                                                                                                                                                                                                                                                                                                                                                                               |
| --- | ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F1  | medium | `onWrite` reads the length from the live value and the data from a heap **copy**; if the copy's `malloc` fails the copy has a null pointer and length 0 while `getLength()` is still `n`, so `memcpy(item.data, NULL, n)` faults in the host task (reset). A second path: `deepCopy` of a source whose first `calloc` failed copies from a null pointer (`NimBLEAttValue.cpp:102`).                                   |
| F2  | low    | Writes can be lost without any count: library `INVALID_ATTR_VALUE_LEN` on a write command, `setValue` failure (length 0, `onWrite` returns), `bleQueue == NULL` after a failed `xQueueCreate` (not checked, `esp32_main.cpp:2569`, crash in `onWrite` and in the loop drain). Only a full `bleQueue` is counted (`ble_session.h:146-149`).                                                                            |
| F3  | low    | Per-write heap traffic in the host task: `realloc` growth up to 300 B on the first long frames, plus a `malloc`+`free` of up to 301 B for every `getValue()` copy. `-DCONFIG_NIMBLE_CPP_ATT_VALUE_INIT_LENGTH=300` (`CPP/NimBLEAttValue.h:41-42`) allocates once at creation and removes the growth; the copy stays.                                                                                                  |
| F4  | low    | The 5 s `QUEUED_WRITE_TMO` terminates the connection (`ble_hs_conn.c:545-555`); the comments at `platformio.ini:1976-1977` and the campaign notes say "held at most 5 s", which is true, but the effect is a drop of the link. The link supervision timeout is 4 s (`esp32_main.cpp:417`), so a dead link goes first. Document, no patch.                                                                             |
| F5  | low    | `ble_hci_rx_acl` retries without bound (`esp_nimble_hci.c:148-156`) in the controller callback. NB-03's rationale cites the 4 block msys pool, but the receive mbuf comes from the transport ACL pool of 10 (`TR/transport.c:146-158`), so the starvation described there is not the msys pool. Correct the NB-03 text in `tools/nimble_patch.py:49-54`; a bound would need a drop-and-count decision for the packet. |
| F6  | info   | The TX characteristic (notify only, `esp32_main.cpp:617-629`) is created with `WRITE`: a phone can write it, the value lands in its `NimBLEAttValue` with the default callback, nothing reads it (TX goes through `ble_gatts_notify_custom`, `esp32_main.cpp:2705`). Dropping `WRITE` is a GATT table change; check the apps first.                                                                                   |
| F7  | info   | `ble_att_svr.c:2807-2808` ignores the result of `os_mbuf_prepend`; on allocation or leading-space shortage `txom` is freed or no longer the head. Today the head mbuf always has the 9 stripped header bytes, so it is latent. Covered by `pass_prepend_leading` (leading space 1 B: safe) and `today_prepend_noleading` (leading space 0: stale head sent, new head leaked; expected red).                           |
| F8  | info   | Signed write (`ble_att_svr.c:2375-2403`): `malloc` unchecked (`:2403`), `PKTLEN - 12` underflows on a short PDU (`:2394`). Reachable only with a stored peer CSRK; bonding is off (`setSecurityAuth(false,false,false)`), so `ble_store_read_peer_sec` fails first (not verified that no CSRK can be stored).                                                                                                         |
| F9  | info   | A second MTU request mid-session is accepted and lowers `peer_mtu` (`ble_att_svr.c:771-812`, no `TXED_MTU` check); the L2CAP check (`ble_l2cap.c:406`) then disconnects the peer for frames that were legal before. Spec-violating client only.                                                                                                                                                                       |
| F10 | medium | nRF52: the FIFO (256 B) truncates a write that does not fit and nothing counts it (`BLEUart.cpp:88-96`, no `setRxOverflowCallback`); two quick writes of about 200 B can deliver a cut frame to `readPhoneCommand`. The merge of two writes into one item (stream, no framing) is by design of the FIFO and is the wave 3 model.                                                                                      |
| F11 | low    | nRF52: `ada_callback_invoke` can fail (falls back to an inline call from the SoftDevice context, `BLECharacteristic.cpp:538-541`) or drop the event with a debug log only (`AdaCallback.c:83-97`); max frame is 250 B on nRF52 against 300 B on ESP32.                                                                                                                                                                |

### Proposed changes (text only, nothing applied)

**F1 and F2, `src/esp32/esp32_main.cpp` and `src/ble_session.h`.** Take length and pointer from one copy and drop the write
if the copy failed; count it. Seam in `ble_session.h` (so `pass_seam` can check it):

```cpp
// ble_session.h
// One ATT write -> queue item. False = nothing to queue (empty, too long, or no data: allocation failure in the
// library's value copy). The caller counts a false with n > 0 expected.
inline bool bleItemFromWrite(BleQueueItem &it, const uint8_t *p, size_t n)
{
    it = {};
    if(p == nullptr || n == 0 || n > MAX_MSG_LEN_PHONE) return false;
    memcpy(it.data, p, n);
    it.length = n;
    return true;
}

// esp32_main.cpp, CharacteristicCallbacks::onWrite
static BleQueueItem item;
const NimBLEAttValue v = pCharacteristic->getValue();      // one copy, one length
if(!bleItemFromWrite(item, v.data(), v.size())) {
    if(pCharacteristic->getLength() > 0) bleQueueOnSend(g_bleQ, false);   // library copy failed: count it
    return;
}
bleItemStamp(item, g_bleQ);
bleQueueOnSend(g_bleQ, bleQueue != NULL && xQueueSend(bleQueue, &item, 0) == pdTRUE);
```

The counter name and the loop message ("queue full") should then say "dropped" without the cause, or get a second counter
(operator choice). The `getLength()` call after a failed copy is the only extra read.

**F3, `platformio.ini` (esp32 `build_flags`).** `-DCONFIG_NIMBLE_CPP_ATT_VALUE_INIT_LENGTH=300`: about 2 x 301 B heap at
boot for the two characteristics (also any other `NimBLELocalValueAttribute`, which are none in our server).

**F5, `tools/nimble_patch.py` (docstring only).** NB-03: replace "drains the 4-block msys pool" by "waits for a free block of the
10-block transport ACL pool (`TR/transport.c:146`)". No code change.

**F10, `src/nrf52/nrf52_ble.cpp`.** Register an overflow callback that counts into `g_bleQ.dropped` (no Serial in the
callback), and size the FIFO for two maximum frames: `BLEUart g_ble_uart(512);` (+256 B RAM on nRF52; the RAK4631 headroom is
not checked here).

**F6 (optional).** `NIMBLE_PROPERTY::NOTIFY | READ` for the TX characteristic, after checking that no app writes it.

No library patch is proposed: none of F1..F11 needs a change inside NimBLE-Arduino or Bluefruit. A counter for library-side
rejections on write commands (F2) would need a patch of `handleGattEvent` (an exported counter incremented before the two
`return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN`, `NimBLEServer.cpp:642,653`); it is only worth it if the bench shows such
rejections, since `createCharacteristic(.., 300)` already bounds the frames and the phone gets the error on a write request.

### Not examined

`ble_gatts.c` outside `ble_gatts_val_access`, the security manager, advertising, the NimBLE-Arduino 2.3.0 core diff (open
item of wave 1b), the notify path (`esp32_write_ble`, `ble_gatts_notify_custom`), `xSemaphoreGiveFromISR` called from the
callback task and the non-atomic `g_task_event_type |= BLE_DATA` on nRF52 (`nrf52_ble.cpp:401-402`).
