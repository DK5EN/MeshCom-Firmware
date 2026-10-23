# Campaign: AU-20 BLE pause heap loss

Status: **running**, approved 2026-10-23 (crash risk on DK5EN-1 accepted). Resume point for AU-20. Section 6 is the progress log.

## 1. Goal

Find where the roughly 50 B of free heap per BLE pause/resume goes. No fix route is decided: the
campaign ends with a located cause (or a proof that the pause is not the cause), then the operator
decides.

Finding (bench 2026-10-23, [campaign-bench-20261023.md](campaign-bench-20261023.md)): DK5EN-92,
`--update bleguardcycle`, 412 cycles, 60948 -> 40968 B. The unpatched 10.21 build shows the same
slope, so it predates AU-19 (`be1589e0`). Field: one pause per daily check, about 50 B/day, cleared by
any reboot. Not a 10.23 blocker.

## 2. Static findings before W0

- **BACKLOG lead holds statically (corrected by W0-B).** The pre-W0 read that the lead was wrong used
  the mem-pool branch of `npl_os_freertos.c`, which is not compiled: it sits under
  `#if CONFIG_NIMBLE_STACK_USE_MEM_POOLS` (:22), and `nimconfig.h:243` defaults that to 0. The
  compiled `npl_freertos_callout_init()` (:1785, `CONFIG_BT_NIMBLE_USE_ESP_TIMER 1`) calls
  `esp_timer_create()` unconditionally. `ble_hs_flow_init()` (`ble_hs_flow.c` ~291, from `ble_hs_init`)
  creates timer A, `ble_hs_flow_startup()` (~273, every sync) creates timer B into the same handle, A
  is orphaned. `ble_hs_flow_stop()` has no caller; `ble_hs_flow_deinit()` deletes B only. Net one
  esp_timer struct (+ heap header) per init, about 50 B. The orphan is never armed, so `tmr_armed`
  does not see it; `ablk` (+1 per cycle) and `abytes` do.
- **No control run exists.** The slope is not yet tied to the pause: the same 400 s without any pause
  may lose heap too.
- **Cycle driver output:** `auInstrBleGuardCycle()` / `auInstrCycleTick()` (`src/esp32/esp32_main.cpp`
  ~2603) print free heap and largest block only.
- **S3 has no BLE pause.** `AU_BLE_PAUSE` (`src/esp32/fw_update_net.h:231`) is 1 on
  `CONFIG_IDF_TARGET_ESP32` only. On the S3 the AU job never calls `esp32BlePause()` or
  `esp32BleForcePause()` (`fw_update_net.cpp` ~1266 is inside `#if AU_BLE_PAUSE`), and
  `bleguardcycle` answers `classic ESP32 only`. The S3 forced deinit panicked in the NimBLE host task
  on the 2nd cycle (64cd365d bench, `PC 0` in `nimble_port_run`), the same signature as AU-19; whether
  the AU-19 patch also cures the S3 is open and is a side result of W2.

## 3. Scope rules

- **No change to the auto-update behaviour on any board.** The S3 pause is a test-only path: compiled
  under `INSTRUMENT_ENABLED` only, reachable only from `--update bleguardcycle`. `fw_update_net.cpp`
  and the `AU_BLE_PAUSE` definition stay unchanged. A release (non-INSTRUMENT) S3 image must not
  contain `esp32BleForcePause`.
- Bench rules: `identity_guard.py` first, `--airgap on`, at most 2 dBm, no TX. DK5EN-98 not involved.
- S3 deinit runs only with a USB-serial capture (crash dump), DK5EN-1 resolved by USB serial `0001`.

## 4. Waves

| Wave | Content                                                                                                                                                                                                                                                                                                                                                                         | Who                | Model / effort            | Est. cost / time   |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------ | ------------------------- | ------------------ |
| W0   | **A:** condense `au19-before/after-cycles.log` into a per-cycle series (free, largest block, linear or stepped, seconds per cycle). **B:** static audit of the deinit/init path for allocations not returned: NimBLE host (mempools, msys, GAP/GATT, host task), C++ side (`NimBLEDevice`, server, services, characteristics, advertising), our `bleStackStart()` / pause code. | 2 scouts, parallel | Haiku medium; Sonnet high | ~$0.3 + $2, 15 min |
| W1   | **Instrument + S3 test path:** per-cycle `[AU];bleguard;cycle` line gains internal and 8BIT free, minimum free, task count (`uxTaskGetNumberOfTasks()`), esp_timer count; control mode `bleguardcycle0 <n>` (same tick, no pause); S3 test-only force pause (see section 3).                                                                                                    | 1 implementer      | Sonnet medium             | ~$2, 25 min        |
| W2   | **Bench**, serialized: DK5EN-92 (`ttgo_tbeam` INSTRUMENT) and DK5EN-1 (`heltec_wifi_lora_32_V3` INSTRUMENT). Per node 100 control cycles, then 100 pause cycles (S3: first 20, stop on panic). If the loss is real, layer split guided by the counters and W0-B.                                                                                                                | orchestrator       | Opus medium               | ~$3, 60-90 min     |
| W3   | **RCA:** confirm or reject the mechanism with file:line, per platform; list the fix routes with cost. Operator decides.                                                                                                                                                                                                                                                         | orchestrator       | Opus high                 | ~$2, 20 min        |
| W4+  | Fix, gate, bench before/after, advisor: planned after the W3 decision.                                                                                                                                                                                                                                                                                                          | -                  | -                         | -                  |

Each wave is committed on `fork-dev` before the next starts.

## 5. File ownership

| Wave | Exclusive files                                                                                                                                         | Shared resource                                                            |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| W0   | none (read-only)                                                                                                                                        | none                                                                       |
| W1   | `src/esp32/esp32_main.cpp` (force pause guard, `auInstrBleGuardCycle` / `auInstrCycleTick`), `src/command_functions.cpp` (`bleguardcycle` branch ~5191) | `.pio/build/ttgo_tbeam`, `.pio/build/heltec_wifi_lora_32_V3` (builds only) |
| W2   | logs `tools/bench/runs/harness/au20-*`                                                                                                                  | DK5EN-92, DK5EN-1, their USB ports and 2323 consoles                       |
| W3   | this doc, `docs/BACKLOG.md`, `docs/RESUME.md`, `docs/architecture/08-defect-catalogue.md`                                                               | none                                                                       |

W1 integrity checks: `git diff --stat src/esp32/fw_update_net.cpp src/esp32/fw_update_net.h` empty; symbol
scan: `esp32BleForcePause` present in the INSTRUMENT V3 ELF, absent from the release V3 ELF; release
`ttgo_tbeam` unchanged in behaviour (only INSTRUMENT code touched).

## 6. Progress log

- 2026-10-23: plan written; operator answers: control mode yes, no fix route decided, new doc, nodes
  DK5EN-92 and DK5EN-1, S3 pause test-only.
- W0-A (AU-19 logs): loss linear, LSQ -49.0 B/cycle (patched, 421 cycles) and -49.5 (unpatched, 35);
  not periodic; pause-time heap (stack down) falls at the same slope, so the loss persists outside the
  stack. Between 100-cycle batches the heap rose about +200 B (one unexplained -5.4 kB step). Largest
  block fragments (55284 -> 34804). 1.03 s per cycle.
- W0-B (static audit): top suspect the double-created `ble_hs_flow_timer` (section 2), high
  confidence; everything else on the init/deinit path balanced (mutexes, queues, mempools, msys, GATT
  tables, host task, C++ server objects, phone rings). `bleSessionResetAll()` not audited.
- W1: `[AU];bleguard;cycle;<i>;heap;..;blk;..;resume;<0|1|2>;int;..;min;..;ablk;..;abytes;..;tasks;..;tmr_armed;..`,
  baseline line `cycle;0`, control run `--update bleguardcycle0 <n>` (resume 2), force pause compiled
  on the S3 in INSTRUMENT builds only. Release V3 image: no `esp32BleForcePause`, no `ble;pause;force`;
  `fw_update_net.cpp/.h`, `fw_update.h` unchanged. Advisor skipped: INSTRUMENT-only bench code, no
  release behaviour change.
