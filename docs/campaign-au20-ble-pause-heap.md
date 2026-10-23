# Campaign: AU-20 BLE pause heap loss

Status: **closed 2026-10-23: fixed (route A), benched, gate green.** Resume point for AU-20. Section 7 is the
RCA, section 6 the progress log.

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
- W2 bench (`dea4f82a` INSTRUMENT, airgap on, logs `tools/bench/runs/harness/au20-*`):

  | Run                           | Cycles | Free heap      | Alloc blocks | Alloc bytes | Tasks   | Panics |
  | ----------------------------- | ------ | -------------- | ------------ | ----------- | ------- | ------ |
  | DK5EN-92 control (no pause)   | 100    | 0              | 0            | 0           | 13 / 13 | -      |
  | DK5EN-92 pause                | 100    | -7996 (-51/cy) | +105 (1.00)  | +6292       | 13 / 13 | 0      |
  | DK5EN-1 (S3) control          | 100    | +3196          | -6           | -3100       | 13 / 13 | -      |
  | DK5EN-1 (S3) pause            | 100    | -8012 (-52/cy) | +105 (1.00)  | +6332       | 13 / 13 | 0      |
  | DK5EN-92 pause, diag one-line | 100    | -2848 (-2/cy)  | +4 (0.00)    | +2760       | 13 / 13 | 0      |

  Slopes are least-squares per cycle; each run also has a one-off first-cycle step of about 4-5 blocks.
  Diag image: `ble_npl_callout_deinit(&ble_hs_flow_timer)` inserted before the `callout_init` in
  `ble_hs_flow_startup()`, edited in `.pio/libdeps` only, restored after the build; DK5EN-92 reflashed with
  the standard `dea4f82a` INSTRUMENT image afterwards. `tmr_armed` stays flat in every run (the orphan is
  never armed), as predicted.

- S3 side result: 100 forced pauses on DK5EN-1 without a panic, where the 64cd365d bench panicked on cycle
  2 with the AU-19 signature. The AU-19 patch (`be1589e0`) most likely cures the S3 as well. Not acted on:
  the S3 auto update still has no BLE pause (scope rule, section 3).
- Operator chose route A (2026-10-23).
- W4 (Sonnet implementer): `tools/nimble_patch.py` second hunk, marker `/* MESHCOM-PATCH AU-20 */` on the inserted
  `ble_npl_callout_deinit(&ble_hs_flow_timer)` in `ble_hs_flow_startup()`; same guards as AU-19 (idempotent, unknown
  text fails, file missing with NimBLE in lib_deps fails). 8 new pytest cases, failing on the pre-fix script (10 failed,
  2 passed, 1 skipped), 13/13 after; the real-library case checks every installed copy.
- W5: advisor (Opus) APPROVED; taken: stronger real-library test, docstring note on the host-reset path (timer may be
  armed, deinit stops it; a queued event gets its callback back in the same host task). Safety basis: host task and
  esp_timer task both on core 0, esp_timer at higher priority. Release builds of the 7 targets plus INSTRUMENT
  `ttgo_tbeam`: all ESP32 images carry both markers and exactly one `callout_deinit` call in `ble_hs_flow_startup`;
  RAK has no NimBLE (no-op). Bench DK5EN-92, production patch, 100 pause cycles: `ablk` slope 0.00 (was 1.00), free
  heap -1.5 B/cycle (was -51), 0 panics, all resumes ok (`au20-92-fix-pause.log`). Gate `tools/regression.sh --stage
1,2` PASS (2753/2753 Unity, 1220 pytest).
- Operator 2026-10-23: no upstream report (the pinned NimBLE-Arduino 2.2.3 is old, upstream has a newer version);
  no BLE pause for the S3 auto update (not needed).

## 7. RCA (W3)

**Mechanism (confirmed).** In the pinned NimBLE-Arduino 2.2.3, built without NimBLE mem pools
(`CONFIG_NIMBLE_STACK_USE_MEM_POOLS 0`, `nimconfig.h:243`) and with `CONFIG_BT_NIMBLE_USE_ESP_TIMER 1`,
`npl_freertos_callout_init()` (`npl_os_freertos.c` ~1785) calls `esp_timer_create()` on every call with no
check for an existing handle. Per BLE start the flow-control callout is initialised twice:

1. `ble_hs_init()` -> `ble_hs_flow_init()` (`ble_hs_flow.c` ~291): timer A.
2. host sync -> `ble_hs_startup_go()` -> `ble_hs_flow_startup()` (`ble_hs_flow.c` ~273): timer B into the
   same handle; A is orphaned (never armed, never deleted).
3. `ble_hs_deinit()` -> `ble_hs_flow_deinit()` deletes B only. `ble_hs_flow_stop()` has no caller.

One esp_timer struct (about 32-36 B payload, about 50 B with the heap header) is lost per
`NimBLEDevice::init()`. Independent of AU-19 and of the board: classic ESP32 and S3 lose the same one block
per cycle. Boot runs init once, so a node that never pauses loses nothing.

**Field impact.** Classic ESP32 with the auto update on: one pause per daily check, about 50 B/day, cleared
by any reboot (about 1.5 kB after 30 days of uptime). S3 nodes: none today (no BLE pause). Each leaked
block also fragments the heap a little (largest block fell 14 kB over 100 cycles on DK5EN-92), which
matters for the 76000 B TLS gate on the T-Beam (AU-17).

**Fix routes (operator decision).**

| Route | Change                                                                                                                                       | Cost / risk                                                                                                                             |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| A     | `tools/nimble_patch.py` second hunk: `ble_npl_callout_deinit(&ble_hs_flow_timer)` before the init in `ble_hs_flow_startup()` (the diag line) | Small: same mechanism as the AU-19 patch, already benched (100 cycles flat). pytest cases for the hunk. No ini change                   |
| B     | Patch `npl_freertos_callout_init()`: delete an existing handle before `esp_timer_create()`                                                   | Covers every double init, but touches every callout in the stack; the memset in deinit leaves handles NULL, so mostly safe, wider bench |
| C     | Won't fix: document 50 B per pause                                                                                                           | None; leak and fragmentation stay                                                                                                       |
| +     | Report upstream (h2zero/NimBLE-Arduino, esp-nimble): not checked whether current master still has it                                         | Independent of A-C                                                                                                                      |

Recommendation: A, plus the upstream report. If A is chosen, W4 = 1 implementer (Sonnet medium,
`tools/nimble_patch.py` + its pytest), W5 = gate, release build scan, DK5EN-92 100 cycles with the
production patch (`ablk` slope 0), advisor.
