# BLE, battery and backlog campaign 2026-09-29

Branch: `fork-neo-test`, base `1e5db6a7`. Fork only, no upstream PR this round (operator
2026-09-29). Mechanics: `/orchestrate-waves`, one commit set per wave.

## Scope (operator decisions 2026-09-29)

1. **BLE**: host-side phone harness (happy, edge, overrun, PIN empty/set), fix what it finds,
   then the same checks on hardware (RAK DK5EN-90, Heltec V3 DK5EN-1) with the bleak client
   and the UDP mock server as traffic source.
2. **Battery**: `docs/archive/concept-battery-consolidation-20260923.md`, the FULL concept
   (cadence, shared filter, all boards, unified percent curve, `--analog` onto the shared block).
   Switched-divider boards (Heltec V3/V4/Stick): sample every 30 s with one 100 ms divider
   window, so a board without a battery shows one short red flash per sample instead of the
   LED staying on for 30 s.
3. **Backlog**: INS-05, DM-17, GW-02 (`docs/BACKLOG.md` §3.8bb). DM-18 (`:rej` id offset) found
   and logged, not fixed (needs a decision on what a `:rej` does to the own DM).
4. **Soak**: afterwards OTA DK5EN-98 with everything, restore 22 dBm, `--gateway on`,
   `--mesh on`, start the soak per `docs/soak-20260928-impl-plan.md` wave 2.

## Bench (checked 2026-09-29 with `identity_guard.py`)

| Node     | Board                  | Port                     | IP                  | State before                                       |
| -------- | ---------------------- | ------------------------ | ------------------- | -------------------------------------------------- |
| DK5EN-90 | RAK4631                | `/dev/cu.usbmodem1101`   | 192.168.68.68 (ETH) | 4.35u 09-28 11:07, 2 dBm, GW off, MESH off, no PIN |
| DK5EN-1  | Heltec V3              | `/dev/cu.usbserial-0001` | 192.168.68.71       | 4.35u 09-28 18:05, 2 dBm, GW off, MESH on, no PIN  |
| DK5EN-98 | Heltec V3 (production) | OTA only                 | DK5EN-98.local      | operator set GW off, 2 dBm, MESH off for the bench |

Mac (mock server host): 192.168.68.58. Nodes reach the mock with `--srvip <mac>` (RAM only,
needs gateway on). Bench PIN tests must end with the PIN state found above (none).

BLE baseline on the unchanged image (`ble_golden.py`, two runs per node, identical): RAK 16
burst frames (I IS1 SE S1 SW S2 SN SN1 W G SA IO TM MH MH CONFFIN), Heltec 15 (same, AN instead
of MH), all valid JSON, max 229 B. The faults need load or a refused notify to show.

## Findings feeding the waves

- **BLE-N1** notify failure loses the frame: `sendToPhone()`/`sendComToPhone()` pop before
  sending (`src/phone_commands.cpp` ~77), `esp32_write_ble()` ignores `notify()`'s result
  (`src/esp32/esp32_main.cpp` ~2065).
- **BLE-N2** MTU blindness: the negotiated MTU is only logged (`esp32_main.cpp` ~400); every
  clamp assumes MTU 247.
- **WF-01** stays parked (needs the real app).
- **BAT-LED** Heltec V3 arms the divider on one `read_batt()` call and reads on the next
  (`src/batt_function_old.cpp` ~671-701); with the neo 30 s `loopInterval_battCheck()`
  (`src/loop_scheduler.h:151`) the divider stays on for 30 s. Operator to confirm the LED
  follows the divider on the bench.

## Battery migration contract (wave 3)

- `src/batt_pipeline.h` is the single source for EMA (tau 30 s, dt-based), settle rule
  (3 tau and 8 samples), BAT-01 detector, percent curve, sampling scheduler, Brown block.
- `loopAction_battCheck()` ticks every 100 ms while no TX/RX is active (the scheduler gate is
  the concept's "drop samples taken during TX").
- `read_batt()` stays the per-file entry and returns the FILTERED battery mV. Inside it,
  `battSchedTick(now)`: NONE -> cached value; ARM -> enable the divider (switched boards);
  READ -> raw read, release divider, detector fed with the RAW sample, EMA update. Profiles:
  SWITCHED for boards with a divider enable pin, FIXED (1 s) otherwise.
- "No reading" stays `0` mV (T-Deck header, `/B=` suppression via `battProbeState`). A filtered
  value below 1000 mV reports 0.
- `mv_to_percent()` keeps its name and returns `battPercent(mv, max_mv)`; 0 for 0 mV.
- Low-voltage deep sleep only through `battEmaLowVoltage()` (not before settled).
- Both battery files export `uint32_t battSampleCount(void)` (incremented per READ) so the
  caller prints its debug line once per real sample, not every 100 ms tick.

| Writer | Files                                                                                                                             |
| ------ | --------------------------------------------------------------------------------------------------------------------------------- |
| W3a    | `src/batt_functions.{h,cpp}`, `test/test_batt_detect/` (USE_NEW_BATT boards)                                                      |
| W3b    | `src/batt_function_old.cpp` (Heltec V2/V3/V4/Stick, RAK, T114, T-Echo, the rest of that path)                                     |
| W3c    | `src/loop_scheduler.{h,cpp}`, `src/esp32/loop_actions_esp32.cpp`, `src/nrf52/loop_actions_nrf52.cpp`, `src/adc_functions.{h,cpp}` |

## Later findings (2026-09-29)

- **BLE-N3** (BLE advisor) the drain read `bf_peek()` and then `bf_tail_gen()`: an eviction
  between the two (nRF52: OnRxDone in the LORA task) paired frame A with B's generation and the
  pop took B, never sent. Fixed with `bf_peek_gen()` / `bf_pop_if()` (one lock each,
  `src/byte_fifo.*`); regression in `test_byte_fifo`.
- **TIME-01** (millis teleport tests) every NBR minute stamp was `(uint16_t)(millis()/60000)`;
  2^32 ms is not a multiple of 65536 min, so at the wrap (49.7 d uptime) the counter jumped
  6046 -> 0 and the next `nbrSweep()` wiped every row and edge; gateway-flag stamps were off by
  98 min. Fixed with `uptimeMin16()` (`src/uptime_min.h`, 39 call sites). Red/green in
  `test_nbr_matrix` `test_teleport_*`.
- **TIME-02** `battEmaUpdate()` read a forward gap >= 2^31 ms as "backwards" and froze the filter
  for ~24.8 days. Small backward steps (< 1 h) are still ignored, anything else is a gap.
- **BAT-NOISE** (not fixed, documented) the switched-divider boards take one raw reading per
  30 s; with tau 30 s the EMA passes most of the noise. An 8x block average would smooth away
  the floating-divider signature the BAT-01 detector needs (> 250 mV jumps), so it stays a
  single reading until a bench run shows the detector still works with averaging.
- Advisor items applied: `--display cont` battery lines at most every 10 s; old-path
  `battHardwarePresent()` treats a reported 0 mV as absent (no `/B=000` on USB boards).
- `-Os` for `wiscore_rak4631` (operator request): `build_unflags = -Ofast`, `-Os`.

## Wave status log

| Wave | Content                                                                            | Status                                                                                 |
| ---- | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| 0    | bench identity, baseline `--info` and BLE burst of both nodes                      | done                                                                                   |
| 1    | writer B: INS-05, DM-17, GW-02; writer D: `tools/bench/ble_stress.py`              | done; advisor REWORK (deep-sleep clear placement, 0xF0 save path) applied; D committed |
| 2    | writer H: BLE harness + BLE-N1/N2; writer P: `src/batt_pipeline.h`                 | done; BLE advisor REWORK (BLE-N3) applied                                              |
| 3    | battery migration W3a/W3b/W3c                                                      | done; battery advisor APPROVED, two optional items applied, BAT-NOISE documented       |
| 3t   | writer T: millis teleport tests in five suites                                     | done; TIME-01, TIME-02 found and fixed                                                 |
| 4    | full gate (48 native envs, 32 board envs), commits, flash RAK + Heltec, bench runs | gate running                                                                           |
| 5    | OTA DK5EN-98, restore 22 dBm / GW on / MESH on, start soak                         | planned                                                                                |

Note: editing `platformio.ini` changes `project.checksum`, which wipes `.pio/build` -- the first
board build after it is a full rebuild.
