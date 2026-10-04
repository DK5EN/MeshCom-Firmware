# T-Deck legacy ids + W0.2 build flags -- campaign state (2026-10-04)

Goal: close the hardware-bound BL-06 ids (TD-09, TD-11, TD-15, MEM-04) on the bench T-Deck and
harden the five ESP32 envs without `extends = esp32` (W0.2). Orchestrated as waves
(`/orchestrate-waves`), writers on Sonnet. Base: `fork-dev` `f0086536`. E22-01 stays out: no E22
in the bench fleet, it remains a concept for the operator.

## Findings at kick-off

- W0.2: `t_deck_pro`, `t5_epaper`, `vision-master-e213` (`e290` extends it), `wireless-paper`
  carry no `build_src_flags`; `nrf52_base` has `-Wformat=2 -Werror` but no `-Wall -Wextra`.
- MEM-04: `ttgo_tbeam` `iram0_0_seg` headroom 4972 B on the current build (PSRAM unflag in
  place). `lib/tinyxml2` is vendored; `E22_XML-DevKitC` never measured on 4.40a.
- TD-15 confirmed open: `loadPosPersistence()` fills the POS table only, `map_pos_*` stays empty,
  HEY frames never reach the map.

## Night job (orchestrator, pio slot)

`scratchpad/w02/nightjob.sh`: `ttgo_tbeam` + `E22_XML-DevKitC` as shipped with
`resource_watch.py regions` (MEM-04 verdict), then the five envs with
`PLATFORMIO_BUILD_SRC_FLAGS="-Wall -Wextra -Wformat=2"` (no `-Werror`), warnings per env and file.
Wipes `.pio/build` (build-flag checksum).

## Waves

| Wave | Agent | Item  | Exclusive files                                                     | Verification                      |
| ---- | ----- | ----- | ------------------------------------------------------------------- | --------------------------------- |
| 0    | S1-S4 | recon | read-only                                                           | --                                |
| 1    | W-A   | TD-15 | src/t-deck/lv_obj_functions.cpp, src/lora_functions.cpp             | no pio; gate builds t_deck_plus   |
| 1    | W-B   | TD-09 | src/t-deck/tdeck_sdmap.cpp, new cache header + test/test_tile_cache | native test only                  |
| 2    | W-C   | TD-11 | src/t-deck/lv_obj_functions.cpp (+ ACK hook per S3)                 | no pio; gate builds t_deck_plus   |
| 2    | W-D   | W0.2  | the five variant inis + env-exclusive sources from the inventory    | no pio; gate builds the five envs |

Hotspots kept by the orchestrator: `tools/bench/tdeck_harness.py` (scenarios `map_persist`,
`map_rebuild`, `msg_ack`), `docs/BACKLOG.md`, this file, the bench doc. Gate per wave:
`tools/regression.sh --stage 1,2`, `t_deck_plus` instrument build, OTA to DK5EN-14, the new
harness scenarios with beacons from RAK-90/T-Beam-92 (group TEST, 2 dBm), fable-review on the
diff, commit. Bench beacons of this session land as direct neighbours in the RX-01 soak of
DK5EN-98 (running until 2026-10-05 11:17); mark them in that evaluation.

## Status

- 2026-10-04 ~11:40: night job started (pid in `scratchpad/w02/nightjob.out`), wave 0 scouts
  dispatched.
- 2026-10-04 11:36: night job done in 6 min (warm toolchain, ~1 min per env). MEM-04 on 4.40a:
  `ttgo_tbeam` dram 26 864 B / iram 4 972 B headroom, `E22_XML-DevKitC` dram 24 936 B /
  iram 4 028 B (below the 4 096 B watch line, not a link failure). W0.2 inventory with
  `-Wall -Wextra -Wformat=2` (no `-Werror`), `-Wmissing-field-initializers` excluded because
  `[esp32]` disables it anyway: `t_deck_pro` 68 (bq27220.cpp/.h 23 incl. 16 `-Wreorder` and
  7 `-Wformat=`, ui_deckpro.cpp 28, peri_lora/tdeck_pro/adc 9), `t5_epaper` 56 (ui.cpp 25,
  bq27220 23, t5epaper_main/peri_lora/adc 7, ui_common/scr_mgr.c 1), `vision-master-e213`,
  `-e290`, `wireless-paper` 2 each (`src/Displays/BaseDisplay/layout.cpp` `-Wtype-limits`).
  Logs: `scratchpad/w02/warn-<env>.byfile`.
- Wave 0 scouts landed 11:40. Corrections to the plan: HEY frames carry no position (payload
  `R<n>;`), so TD-15 (b) "HEY feeds the map" is void; `[INSTR-GUI] map_points` is the ring
  index of drawn markers (wraps at 30), not a count; `savePosPersistence()` writes at most
  every 30 s.
- Wave 1 landed 12:00: W-A TD-15 (restore into `map_pos_*` at boot, no LVGL call, drawn by
  `refresh_map()` on the MAP tab; hemisphere letters in `tdeck_add_pos_point()` were swapped
  and are fixed; parser in `src/t-deck/pos_persist.h`, 9 native cases), W-B TD-09 (12-slot
  PSRAM tile cache, 3 MB, `src/t-deck/tile_cache.h`, 10 native cases, `[SDMAP]` line gains
  `cache h/m`). Orchestrator: `--injectpos` also feeds the POS table, harness scenarios
  `map_rebuild`, `map_persist_seed`/`map_persist_check`, golden baseline regenerated. Advisor
  (Fable) REWORK: 8 items, all applied (save-window trigger station, hit-delta instead of a
  time comparison, 8 -> 12 slots, invalidate on rescan, cache-off detection, leading-minus
  test); item 5 noted: the POS table stores 9 callsign characters, a 10+ character callsign
  restores truncated and gets a second marker on its next live beacon (pre-existing).
- Wave 1 gate 12:15: `tools/regression.sh --stage 1,2` PASS (48 envs, 1670 cases, 553 pytest,
  golden baseline regenerated for the two new `test_filter` entries); `t_deck_plus` instrument
  image OTA'd to DK5EN-14 (build 12:07:59); harness `map_persist_seed` + `map_persist_check`
  (all six seeded callsigns drawn after the reboot, 30 markers restored from the field
  `/pos.dat`), `map_rebuild` (cycle 2: decode 0 ms, hits +6 = tiles drawn, 32 ms per rebuild),
  `map` 27/27 steps. Lesson: never start a stage-1 run while an OTA upload reads
  `.pio/build/<env>/firmware.bin` (the flag checksum wipes the directory; the node fell back
  into its app unharmed, the guard then failed once). Committed as wave 1.
