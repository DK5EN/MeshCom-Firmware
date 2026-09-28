# RAK4631: six settings copies in RAM -> three

Campaign doc and resume point. Branch `fork-neo-test`, base `af33af8b`
(v4.35u.09.28-neo). Source figures: `docs/meshcom-speicherkarte.html` (RAM),
`docs/meshcom-flashkarte.html` (flash).

## Status

| Wave | Content                                                       | State                |
| ---- | ------------------------------------------------------------- | -------------------- |
| S    | Scouting: lifetimes, task contexts, Bluefruit copy semantics  | done                 |
| 1    | Staging rewrite + shared scratch + regression test            | done                 |
| G    | Gate: native tests, RAK + T-Echo build, symbol check, advisor | done                 |
| H    | Bench: flash RAK-90, BLE settings read/write-back, msgid kept | open: RAK not on USB |

## Measured starting point (wiscore_rak4631, `nm -S`)

| Object                   | File            | Type                 | Bytes | Section |
| ------------------------ | --------------- | -------------------- | ----: | ------- |
| `meshcom_settings`       | shared          | `s_meshcom_settings` |  1984 | .data   |
| `s_pendingBleSettingsV1` | nrf52_ble.cpp   | `s_ble_settings_v1`  |  2000 | .data   |
| `s_bleSettingsSnapshot`  | nrf52_ble.cpp   | `s_ble_settings_v1`  |  2000 | .data   |
| `s_convertedBleSettings` | nrf52_ble.cpp   | `s_meshcom_settings` |  1984 | .data   |
| `s_bleSettingsOutBuf`    | nrf52_ble.cpp   | `s_ble_settings_v1`  |  2000 | .data   |
| `s_legacyV1Image`        | nrf52_flash.cpp | `s_ble_settings_v1`  |  2000 | .data   |

All six sit in `.data`, because both structs carry non-zero member initialisers
(callsign `XX0XXX-00`, symbol, power -20, ...). Each copy therefore costs its
size twice: once in RAM, once as the initialiser image in flash.

## Findings that shape the design

1. **Bug (fixed by this campaign): a BLE settings write resets the message
   counter.** `applyPendingBleSettings()` converted into
   `s_convertedBleSettings`, then `memcpy`d the whole struct over
   `meshcom_settings`. `bleSettingsFromV1()` deliberately skips `node_msgid`,
   so the live counter was replaced by the scratch's value: 0 on the first
   write, the stale value from the previous write after that. A counter reset
   replays message ids into the neighbours' dedup rings, so the node's own
   next messages can be dropped as duplicates. On nRF52 `node_msgid` is the
   only member affected; the other members missing from `s_ble_settings_v1`
   (`node_mute`, `node_disp_rot`, ...) exist only in ESP32 builds, which do
   not use this path.
2. `BLECharacteristic::write()` copies synchronously (`sd_ble_gatts_value_set`,
   BLECharacteristic.cpp:588). `notify()` reads the caller's buffer chunk by
   chunk, but each `sd_ble_gatts_hvx` copies before the loop moves on
   (:728-750). So an out buffer is free once the call returns.
3. The write callback runs in the Ada callback task; `data` points at
   Bluefruit's heap-allocated long-write buffer, which is only valid during the
   callback. The staging copy (`s_pendingBleSettingsV1`) is therefore
   required and cannot be shared: it is written by another task at any time.
4. `applyPendingBleSettings()` (nrf52_main.cpp:1726), `init_flash()` and
   `flash_reset()` all run in the Arduino loop task and never nest, so one
   v1-sized scratch can serve the read direction and the legacy blob.

## Design

Three copies remain: the live `meshcom_settings`, the staging image the BLE
task writes, and one v1 scratch owned by the loop task.

- **Staging with a sequence counter instead of a snapshot.** The callback
  increments a counter to an odd value, `memcpy`s into the staging image and
  increments it back to even. The loop task enters a critical section and
  checks the counter. If it is odd, a copy is in progress: it leaves the
  section, yields for one tick and retries. If it equals the last applied
  value, there is nothing to do. Otherwise it runs `bleSettingsFromV1(staging,
meshcom_settings)` directly on the live struct, still inside the critical
  section, and records the counter. Inside the section the callback task cannot
  run, so the read is consistent without `s_bleSettingsSnapshot`, and without
  `s_convertedBleSettings`.
- **Converting in place fixes finding 1.** Only the v1 fields are written;
  `node_msgid` stays untouched. The critical section
  grows from one 2 kB `memcpy` to a member-wise copy of the same bytes (roughly
  2-3x as long, tens of microseconds at 64 MHz); the SoftDevice interrupts stay
  unmasked by the FreeRTOS port.
- **One v1 scratch for the loop task.** `bleSettingsV1Scratch()` in
  `ble_settings_v1.cpp` returns the buffer. `init_settings_characteristic()`,
  the write-back/notify in the apply path, `init_flash()` and `flash_reset()`
  use it in place of `s_bleSettingsOutBuf` and `s_legacyV1Image`.
- **Zero-initialised storage.** The staging image and the scratch are wrapped in
  a union with a `constexpr` constructor that zeroes a byte array, so they land
  in `.bss`. Every user fully writes the buffer before reading it
  (`bleSettingsToV1()` sets every v1 member including the v1-only ones; the
  staging image is only read after a complete copy), so the initialisers are
  never observed.
- The staging logic lives in a small header, `src/nrf52/ble_settings_stage.h`,
  free of FreeRTOS includes so the native suite can drive it; the critical
  section and the yield stay in the caller.

## Expected effect

Measured after wave 1 (clean builds, PlatformIO summary):

| Build           | RAM before | RAM after |  Delta | Flash before | Flash after |  Delta |
| --------------- | ---------: | --------: | -----: | -----------: | ----------: | -----: |
| wiscore_rak4631 |     92 780 |    86 804 | -5 976 |      783 516 |     773 580 | -9 936 |

`nm`: `s_bleSettingsStage` (2 008 B) and `bleSettingsV1Scratch()::storage`
(2 000 B) are `.bss`; `meshcom_settings` is the only `.data` copy left.

The RAM card's "about 8 kB" assumed four copies could go; the staging image has
to stay (finding 3), so it is three copies, about 6 kB RAM. The flash side is
new: about 10 kB, since only `meshcom_settings` keeps an initialiser image.

## Wave 1 brief (one implementer)

One writer, because every change needs the one `wiscore_rak4631` build slot and
the global PlatformIO package store (one pio process at a time).

Exclusive files: `src/nrf52/nrf52_ble.cpp`, `src/nrf52/nrf52_flash.cpp`,
`src/nrf52/ble_settings_v1.h`, `src/nrf52/ble_settings_v1.cpp`,
`src/nrf52/ble_settings_stage.h` (new),
`test/test_ble_settings_v1/test_ble_settings_v1.cpp`.
Orchestrator-owned: `tools/neo/paths/CORE.txt` (new header), this doc.

Regression test (native, `pio test -e native_ble_settings_v1`): a staged image
applied to a live struct keeps `node_msgid`; must fail against the old snapshot-plus-full-memcpy logic and pass after. Also
covered: counter odd -> busy, no change -> no-op, two stages -> last wins.

## Gate

1. `pio test -e native_ble_settings_v1`, then all `native*` envs (never bare
   `pio test`, it uploads to boards).
2. Clean sequential builds of `wiscore_rak4631` and `t_echo`.
3. `nm -S` on the RAK ELF: `s_bleSettingsSnapshot`, `s_convertedBleSettings`,
   `s_bleSettingsOutBuf`, `s_legacyV1Image` gone; staging and scratch in `.bss`;
   RAM and flash deltas from the PlatformIO summary.
4. Advisor pass (fable-review) on the diff: behaviour change in the apply path.

## Bench (wave H)

RAK-90 (`/dev/cu.usbmodem2101`), identity guard first. Flash the gate image,
read the settings characteristic, write the same image back, then check over
serial that `--msgid` did not change and the settings survived a reboot.
