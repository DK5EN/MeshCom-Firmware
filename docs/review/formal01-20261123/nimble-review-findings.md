# NimBLE review findings

- Date: 2026-11-23
- Branch: `review` (diff against `main`; `origin/HEAD` does not exist in this repo)
- Method: `/code-review high`, a single automated reviewer pass
- Focus: paths reachable in the peripheral-only build, and the deinit/init cycle used for
  firmware-update pauses

## Status

All findings are **unconfirmed candidates**. Nothing was run on hardware or in a test, and no finding
has been checked by hand against the code yet. No code has been changed.

No CLAUDE.md applies to this tree, so every finding below is a correctness finding.

## Summary

| #   | Severity  | Location                                                         | Finding                                                      |
| --- | --------- | ---------------------------------------------------------------- | ------------------------------------------------------------ |
| 1   | important | `code/NimBLEServer.cpp:645`                                      | Remote stack overflow from a 512-byte VLA on write           |
| 2   | important | `code/NimBLEDevice.cpp:962`                                      | Use-after-free when deinit frees objects after a failed stop |
| 3   | important | `code/nimble/porting/npl/freertos/src/nimble_port_freertos.c:73` | Old host task can delete the new host task                   |
| 4   | important | `code/NimBLEDevice.cpp:888`                                      | Failed init cannot be retried; sync wait has no timeout      |
| 5   | important | `code/NimBLEAttValue.cpp:111`                                    | Heap over-read in `setValue` when append fails               |
| 6   | important | `code/NimBLEAttValue.cpp:109`                                    | Patch NB-01 writes through a possibly null pointer           |
| 7   | minor     | `code/nimble/nimble/host/src/ble_att_svr.c:2497`                 | Prepare writes can hold the whole mbuf pool for 30 s         |
| 8   | minor     | `code/nimble/nimble/host/src/ble_hs_stop.c:263`                  | Stop connection counter is never reset                       |
| 9   | minor     | `code/NimBLEServer.cpp:519`                                      | Bond deleted before the peer proves its identity             |
| 10  | minor     | `code/NimBLEServer.cpp:309`                                      | `getPeerInfo(index)` uses the wrong index                    |

## Recommended next steps

1. Confirm findings 1-3 against the code first. A remote peer (1) or the normal update pause/resume
   cycle (2, 3) can trigger them.
2. Then 4-6, which matter mostly on the resume path with a fragmented heap.
3. Draft fixes with regression tests where the code is testable off-target.
4. Findings 9 and 10 cannot be reached in the current configuration (bonding off, one connection);
   note them and revisit if that configuration changes.

## Findings in detail

### 1. Remote stack overflow on characteristic write

- Location: `code/NimBLEServer.cpp:645`
- Severity: important

The write handler declares `uint8_t buf[maxLen]` on the stack, sized by the characteristic's maximum
length (512 by default from `createCharacteristic`), not by the bytes actually received. The build
shrinks the NimBLE host task stack to 3072 bytes, so every write spends 512 bytes before `onWrite` and
its logging even start.

Failure scenario: an unpaired attacker connects and sends a Write Request, or Prepare Write plus
Execute Write, with 512 bytes to the RX characteristic. The call chain runs from
`ble_hs_event_rx_data` through l2cap, att_svr, gatts and `handleGattEvent` (512-byte VLA) to
`writeEvent`, `setValue`/`realloc`, the app's `onWrite` and printf logging, which exceeds 3 KB. Result:
stack overflow and a watchdog or panic reset of the node.

Fix: size the buffer from `OS_MBUF_PKTLEN(ctxt->om)` after the length check (or copy straight into the
value with `os_mbuf_copydata`), or create TX/RX with an explicit small `max_len` such as 244.

### 2. Use-after-free when deinit runs after a failed stop

- Location: `code/NimBLEDevice.cpp:962`
- Severity: important

With `clearAll=true`, `NimBLEDevice::deinit` deletes the server, services and characteristics even
when `nimble_port_stop()` failed and the host is still running. The host's GATT table keeps pointers
into the freed objects (each attribute's callback argument and the service definitions), and the GAP
handler calls `NimBLEDevice::getServer()`, which now returns nullptr. The firmware relies on exactly
this case ("the resume rebuilds the objects on the running stack"), and the rebuild's
`ble_gatts_reset()` fails with `BLE_HS_EBUSY` if a link is up.

Failure scenario: pause during an update. `ble_hs_stop` returns an error (for example `EALREADY`), the
objects are deleted anyway and the host task keeps running. A peer then sends a Read/Write Request to
the old RX handle, and `handleGattEvent` dereferences the freed `NimBLELocalValueAttribute`. A new
connect hits `pServer->m_connectedPeers` with `pServer == nullptr` and crashes.

Fix: only free objects when `rc == 0`. Otherwise return false, or call `ble_gatts_reset()` first and
check its result.

### 3. Old host task can delete the new host task

- Location: `code/nimble/porting/npl/freertos/src/nimble_port_freertos.c:73`
- Severity: important

`nimble_port_stop()` returns as soon as the stop event's callback releases its semaphore, which is
before the old host task calls `esp_nimble_disable()`. That function deletes whatever the global
`host_task_h` points to. If `init()` reaches `esp_nimble_enable()` first, `host_task_h` already names
the new task, so the old task deletes it and then returns from its task function.

Failure scenario: rapid pause/resume (a failed pause followed by an immediate resume, or the
`bleguardcycle` bench). The old host task is preempted on core 0 by WiFi/TLS work while the job task
runs deinit and init. The old task deletes the new host task, then returns, which aborts with "task
should not return". If it does not abort, `init()` spins forever in `while (!m_synced)` while holding
`s_bleLife`. This may be the ESP32-S3 "forced deinit/reinit panics in the host task" noted in the
firmware.

Fix: in `host_task`, delete itself with `vTaskDelete(NULL)` and clear `host_task_h` only if it is
still the current task, or have `nimble_port_stop` wait until the host task has really exited.

### 4. Failed init cannot be retried, and init can hang

- Location: `code/NimBLEDevice.cpp:888` (sync wait at line 928)
- Severity: important

If `esp_bt_controller_enable` or `esp_nimble_hci_init` fails, `init` returns false without undoing
`esp_bt_controller_init`/enable. Every retry then fails at `esp_bt_controller_init` with
`ESP_ERR_INVALID_STATE`. Separately, the `while (!m_synced)` wait has no timeout.

Failure scenario: resume after an update with a fragmented heap. `esp_nimble_hci_init` fails, so the
"resume failed" flag is set and the loop retries. Each retry fails immediately because the controller
is already initialized, so BLE stays off until reboot. If the controller never syncs, the job task
blocks inside `init()` holding `s_bleLife` indefinitely.

Fix: on each error, unwind what has already succeeded (`esp_bt_controller_disable`/`deinit`), and
bound the sync wait, returning false when it expires.

### 5. Heap over-read in `setValue` when append fails

- Location: `code/NimBLEAttValue.cpp:111`
- Severity: important

`setValue` calls `append()`, which returns without copying if the value is too long or `realloc`
fails, then runs `memcmp(m_attr_value, value, len)`. That reads `len` bytes from a buffer whose
capacity can be much smaller (initially 20).

Failure scenario: low heap during an update. A peer writes 512 bytes to RX, `realloc(..., 513)` fails,
append logs and returns, and `memcmp` reads 512 bytes from a 21-byte block. This is a heap over-read
in the host task; it may fault or return garbage depending on the adjacent memory.

Fix: check `m_attr_len == len` first so `memcmp` only runs after a successful append, for example
`return m_attr_len == len && memcmp(...) == 0;`.

### 6. Patch NB-01 writes through a possibly null pointer

- Location: `code/NimBLEAttValue.cpp:109`
- Severity: important

Patch NB-01 writes `m_attr_value[0]` without checking for null. `m_attr_value` is null when the
constructor's `calloc` failed, which the constructor only logs. It is also null in a moved-from
object. Any later `setValue`, including a zero-length write that used to be harmless, now stores to
address 0.

Failure scenario: the service is rebuilt on resume with a fragmented heap, so `calloc(21)` for a
characteristic value fails. A peer then sends a 0-byte Write Request to RX: `writeEvent`, then
`setValue`, then a store to `NULL[0]`, giving a LoadStoreError panic.

Fix: guard with `if (m_attr_value) m_attr_value[0] = '\0';`, or reallocate when the pointer is null.

### 7. Prepare writes can hold the whole mbuf pool for 30 seconds

- Location: `code/nimble/nimble/host/src/ble_att_svr.c:2497`
- Severity: minor

Each Prepare Write takes a fresh mbuf from the shared msys pool, which has only 4 blocks in this
build (`MSYS1_BLOCK_COUNT=4`). The queue is limited only by its 64 entries and the 30 s timeout, so
four prepare writes hold the whole pool.

Failure scenario: a peer sends 4 Prepare Write Requests and never sends Execute. All msys blocks are
held, so ATT responses, SM/L2CAP signalling replies and the app's notification frames (allocated with
`ble_hs_mbuf_from_flat`) fail with ENOMEM until the 30 s timeout disconnects the link. The peer can
repeat this indefinitely.

Fix: limit prepared bytes or entries per connection to well below the msys count, or shorten
`BLE_ATT_SVR_QUEUED_WRITE_TMO`.

### 8. Stop connection counter is never reset

- Location: `code/nimble/nimble/host/src/ble_hs_stop.c:263`
- Severity: minor

`ble_hs_stop_conn_cnt` is incremented for each terminate, decremented only while the stop listener
is registered, and not cleared in `ble_hs_stop()`. After a stop that ends by timeout (link still up
after 2 s, the case of a forced pause with a phone connected), the count stays non-zero. Every later
stop then waits the full 2 s even with no connections, and the uint8 counter can also underflow.

Failure scenario: forced pause with a peer that ignores the terminate; the timeout fires with
`cnt=1`. On the next pause with no link, `cnt > 0` arms the 2 s timer instead of finishing
immediately, so every later pause/resume cycle takes about 2 s longer.

Fix: set `ble_hs_stop_conn_cnt = 0` before the terminate loop, or in `ble_hs_stop_done()`.

### 9. Bond deleted before the peer proves its identity

- Location: `code/NimBLEServer.cpp:519`
- Severity: minor (not reachable today)

On a repeat-pairing event, the server deletes the stored bond of whatever address the peer claims,
before the peer has proven anything. Anyone who spoofs a bonded device's address can wipe its bond
and re-pair as that device. Not reachable today because bonding is off
(`setSecurityAuth(false,false,false)`), so no bonds exist.

Failure scenario: if bonding is ever turned on (`MAX_BONDS=1`), an attacker spoofs the phone's
identity address and starts pairing. The bond is deleted and the attacker pairs in its place; the
real phone loses its bond.

Fix: if bonding is enabled later, return `BLE_GAP_REPEAT_PAIRING_IGNORE`, or ask the application
before deleting.

### 10. `getPeerInfo(index)` uses the wrong index

- Location: `code/NimBLEServer.cpp:309`
- Severity: minor (not reachable today)

`getPeerInfo(index)` counts only occupied slots but then indexes the full array with
`m_connectedPeers[count]` instead of using `peer`. With `MAX_CONNECTIONS=1` this never fires.

Failure scenario: with more than one connection allowed and slots `{NONE, h1, h2}`,
`getPeerInfo(0)` looks up slot 0 (NONE) instead of h1 and returns empty info.

Fix: `return getPeerInfoByHandle(peer);`.
