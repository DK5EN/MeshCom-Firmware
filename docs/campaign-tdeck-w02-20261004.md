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
- Wave 1 committed `4c905ea0`, pushed with `94ca1a50` (DR-16 re-decided, TM-43 soak, open-points
  paper archived). Wave 2 dispatched 2026-10-04 ~15:15: W-C TD-11 (no pio), W-D W0.2 (owns the
  pio slot, builds the five envs itself). Orchestrator hotspot: `msg_ack` harness scenario.
- Wave 2 landed 2026-10-04 ~15:40. W-C TD-11: `MsgBubble` msg_id/status, glyph on the footer
  label, `tdeck_set_msg_status()` hooked at the four transitions (orchestrator folded the id
  reconstruction into `own_msg_id_u32()`), `[MSGSTAT]` console lines; harness `msg_ack` PASS on
  DK5EN-14 (ACK after 11 s, bubble found), `map_rebuild` and `map` unchanged green. W-D W0.2:
  flags on the five envs, 3 of 5 clean with `-Werror`; orchestrator fixed the three shared-file
  blockers of `t_deck_pro` (`gps_protocol.cpp` unused `ubxFrame`, `web_functions.cpp`
  `current_fStep` declaration under the same guard as its use, `loop_functions.cpp` bounded
  memcpy copies) so it is 4 of 5; `t5_epaper` stays without `-Werror` (T5-01, four real defects
  in `ui.cpp`). `nrf52_base` gained `-Wall -Wextra -Wno-missing-field-initializers`, RAK/T114/
  T-Echo clean. Inventory undercount: the night job counted src/ only per env log; the t5/t_deck_pro
  builds also warn in shared files under their guards (listed in T5-01).
- Wave 2 advisor (Fable) APPROVED, five low/medium notes, three applied: `esp32-external-radio`
  (extends `t_deck_pro`, red before W0.2) gets the flag set without `-Werror`; the msg_id capture
  ignores the receive path (`!bWithAudio`), so a late relay of our own frame after a reboot
  takes no id; `msg_ack` docstring says what `found` means. Accepted as is: msg_id low 10 bits
  repeat after ~1000 own messages per boot (same ambiguity as own_msg_id); the RAK instrument
  image is built once in the gate because stage 3 builds the RAK with `INSTRUMENT_ENABLED=1`
  and the new `-Wall -Wextra` on `nrf52_base` must hold there too.
- Follow-up 2026-10-04 ~16:10 (operator: "fix the T5 findings blind, and EXT-01"): the four
  `ui.cpp` defects fixed after the t-deck-pro twin, `BOARD_LORA_IRQ` mapped to `LORA_DIO1` (the
  DIO1 safety net is now active on the T5), `flushDeferredDisplayUpdates()` and
  `iReadBeforeAdvance` guarded like their uses, `t5_epaper` builds with `-Werror`: five of five.
  EXT-01: argument-less `--extudpip` clears the field; checked over the T-Deck net console.
  Controls heltec V3, RAK, T-Beam, T-Deck Plus (instrument) SUCCESS. The bench USB hub dropped
  off the Mac during this step (no USB device enumerated, RAK unreachable); the T-Deck was
  reached over WiFi instead.
- Hub replugged 16:14, full `tools/regression.sh --stage all -- --extudp --soak-seconds 120` on
  the tree: stage 1/2 green (1677 cases, 555 pytest), stage 3 19/20 (RAK harness, extudp soak
  with the fixed restore, T-Beam 8 steps, T-Deck harness 22 scenarios incl. `map_rebuild`, OTA
  and badge on both, mesh exchange 6/6). The one red step was a harness false positive: the
  RAK `boot` scenario's crash regex matched `assert` inside "Wasserturm" in a received beacon;
  word-bounded with a test. RAK then DFU-flashed with the tree (instrument image, build
  16:44:49): EXT-01 confirmed on nRF52 too, RAK harness 5/5. Committed `9d82ab5d`.
- EXT-03 (operator: a field node had 255.255.255.255 as Extern-UDP target) 2026-10-04 17:00:
  `src/extudp_target.h` + `test_extudp_target` (7 cases), hooked into `--extudpip` and
  `startExternUDP()`. Live on DK5EN-14 (192.168.68.70/22, build 16:56): 255.255.255.255,
  192.168.71.255 (own broadcast), 239.1.1.1 and the own IP refused with a reason, 192.168.68.255
  (a host in /22) and 192.168.68.74 accepted. Stage 1,2 PASS (1684 cases), Heltec and RAK
  control builds clean. Not committed yet.
