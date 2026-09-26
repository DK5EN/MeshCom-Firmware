# MeshCom 5 topology campaign

Implementation of stages 1 to 3 of `docs/meshcom5-topologie/meshcom5-topologie.html`
(Fassung 2, 2026-09-25) on branch `feature-neighbour-matrix`. Stage 4 (auto-via) and mcmap
M1 to M3 are out of scope. Run with `/orchestrate-waves`; this file is the resume point.

## Operator decisions (2026-09-25)

| Nr  | Question                  | Decision                                                                                                                                                                                                                |
| --- | ------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Free heap versus capacity | Implement the concept's sizes (64/64 classic, 128/128 S3 and nRF52). Every size is a per-family constant under `#ifndef` in `configuration_global.h` so it can be reduced later with one edit.                          |
| 2   | Base                      | Merge `fork-neo-test` (5180eef4, includes upstream KISS/TCP) into this branch. The sset4 boot migration is deleted, as its `#error` demanded.                                                                           |
| 3   | 24 h live shadow hold     | Dropped. Replaced by a host replay shadow over the DK5EN-98 capture 21.-24.09. at the stage 2 gate; the nRF52 consistency check moves into the stage 3 field run. The on-device `[TOPO]\|DIFF` comparator is not built. |
| 4   | On-air NCNT               | Change it to the symmetric definition (concept 4.8). All on-air emitters (`R<n>`, `/N`, HEY group, HN `R<heard>`) cap at two digits: `NBR_NCNT_AIR_MAX 99`. Web and console show the uncapped value.                    |
| 5   | Phone MTU                 | No MTU work. The MH frame keeps the existing 245-byte limit; FailSoft drops the new fields first when a frame is too long.                                                                                              |

## Waves

| Wave | Stage     | Content                                                                                                                                                                                                                                                                  | Status |
| ---- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------ |
| 0    | sync      | Merge `fork-neo-test`, delete sset4 migration, baseline (native suites, golden, all firmware envs, nm symbols) -- done, 0f7749c2                                                                                                                                         | done   |
| 1    | 1 prep    | W1a replay harness + 24.09 fixture (byte-identical `[NBR]` lines from the dense build); W1b `tools/nm_symsum.py`                                                                                                                                                         | done   |
| 2    | 1 core    | Header contract by the orchestrator; W2a `nbr_matrix.*` (edge pool, NbrMask, callsign word, share rule, counter halving, clamp, minute sweep); W2b mask consumers (txring, lora cover scan, web/command neighbours, loop, both mains)                                    | done   |
| 3    | 2 views   | W3a `nbr_views.*`, direct extension, horizon, echo table, ME step off the path check; W3b horizon/echo feed in `lora_functions.cpp` + replay shadow over 21.-24.09.                                                                                                      | done   |
| 4    | 3 cutover | W4a removal + core readers; W4b web + command; W4c mains + loop + MH JSON (7 new fields, FailSoft); W4d T-Deck, T-Deck Pro, `topo.dat`                                                                                                                                   | done   |
| 5    | 3 field   | Bench on the three USB nodes (T-Deck Plus DK5EN-14 reboot keeps topology, Heltec V3 DK5EN-1, T-Beam v1.2), then 24 h field run on DK5EN-98 and DK5EN-1. The RAK4631 DK5EN-90 nRF52 consistency test runs last, after everything else is finished (operator, 2026-09-25). | open   |
| 6    | docs      | `nbr-logformat.md`, `docs/architecture/09/10/11`, changelog                                                                                                                                                                                                              | open   |

## Baseline and fixtures

- Field capture DK5EN-98 21.-24.09.: copied from `rpizero.local:~/meshlog/dk5en-98/` to
  `~/Downloads/dk5en-98-nbr/` (not in the repo). The 24.09. file (00:00 to 09:28) was produced
  by 8487ea2a, which is the current `nbr_matrix` code: it is the byte-identical replay target.
  The earlier days ran older builds and serve only the shadow comparison.
- Every received frame appears as a `[LOG]` line (path, type, payload, HW, MOD, FW, RSSI, SNR,
  DUP flag, `t=` millis), so the replay can drive both `nbrNoteFrame()` and the old MHeard.

## Wave log

### Wave 0

- Merge conflicts resolved in `esp32_flash.cpp` (neo side), `esp32_main.cpp` (NBR bits and
  KISS bits both kept), `txring_functions.cpp` and `test_txring.cpp` (NBR signatures kept),
  `toggle_table_lint.py` and `variant-ini-effective.json` (both sides). The auto-merge left
  duplicate 5-argument `addTxRingEntryOnce` declarations in `loop_functions.h` and
  `loop_functions_extern.h` (P15 landed on both branches); removed.
- `nbr_sset4_migrate_legacy_bits()` removed with its call on nRF52, its header block and its
  six tests. Nodes that still carry the pre-ebcf7f0b bits (DK5EN-1, DK5EN-98) set their NBR
  switches once by hand after flashing.
- `test_command_toggles`: the negative pin now names the NBR assignments, because `bKISS` and
  `bKISSMETA` legitimately read 0x0010/0x0040 after the merge.

### Wave 1

- `tools/nm_symsum.py` measures the neighbourhood stores from the ELF symbol table. On the
  wave 0 build it reproduces the concept's "today" figures to the byte: 5,989 B classic
  (5,829 + 160 rings), 14,823 B S3, 16,575 B nRF52 (incl. 1,752 B static `mheardLine` copies).
- `test/test_nbr_replay` (env `native_nbr_replay`) replays the DK5EN-98 capture from the cold
  boot of 8487ea2a (23.09. 20:02:19) to 24.09. 09:28 through the dense matrix. Fixture
  `fixtures/dk5en98-20260923-boot.txt` (1.5 MB), regenerated with
  `tools/nbr_replay_extract.py --input 2026-09-23.log --from-last-reset --input 2026-09-24.log`.
- Comparison is per trigger group (the frame, snapshot or HN report that caused the lines),
  in three modes: EXACT, INDEX-FREE (row indices and mask bits as sorted callsigns) and
  DECISION (INDEX-FREE without EDGE/ME `<cnt>` and without CANCEL/CANCEL?/REFUSE).
- The capture tool lost ~40 s around the boot (logger reconnect), so the first hours differ.
  Two things never age out and are excluded in DECISION mode: `<cnt>` of cells that stay
  fresh through the whole capture, and CANCEL/REFUSE, which depend on the device's TX
  timing. Frames with `FCS:0000` failed `decodeAPRS()` and are not replayed.
- Result: DECISION mode has 0 mismatches in 2,697 groups from up 180 on (asserted); hours 0-2
  show 41 boot-gap groups (reported only).
- Seam for wave 2: `-D NBR_MATRIX_SRC='"..."'` selects which implementation the harness
  includes, so the same file runs against a frozen copy of the dense code and the edge pool.

### Wave 2

- Contract in `src/`: per-family constants in `configuration_global.h` (`NBR_FAMILY_*` markers,
  `NBR_MAX_ROWS/EDGES/EXT_SLOTS/HZ_ENTRIES`, `NBR_SHARE_PCT`, `NBR_CNT_HALVE_MIN`,
  `NBR_SNR_AVG_N`, `NBR_NCNT_AIR_MAX`), new `src/nbr_mask.h`, public API frozen in
  `src/nbr_matrix.h` ("CONTRACT (Welle 2)" comments).
- Envs: `native_nbr_replay` is now the compat build (21 rows, share 0, no halving, SNR last
  value, 441 edges); `native_nbr_replay64` and `native_nbr_replay128` run production rules.
- Writers: W2a `nbr_matrix.*` + nbr tests (Opus); W2b txring, `loop_functions.h`/`_extern.h`,
  `lora_functions.cpp`, `loop_functions.cpp`, both mains, txring tests; W2c `web_functions.cpp`,
  `command_functions.cpp`, `tools/nbr*.py`. Orchestrator-owned: `platformio.ini`,
  `configuration_global.h`, `nbr_mask.h`, `docs/nbr-logformat.md`.
- Result: edge pool in `src/nbr_matrix.cpp`; the compat build (21 rows, old rules) matches the
  frozen dense code in 0 of 12,468 lines differing (EDGE/ME `<cnt>` blanked, masks on the low
  32 bits), also with the nRF52 deferred-log path and with or without the minute sweep. The
  production rules at 64 and 128 rows change no decision except through capacity (177 ROW
  keys, rows the dense table had evicted). DB0ED-99 keeps DB0FHR-12 as exclusive in all 53
  snapshots (#X 0 -> 2 in the final one).
- Decision recorded (W2a): the share rule for the alone mask and the cancel cover uses edge
  (M, X) "X heard M's copy", the direction of the dense code; the concept's formula (x, M)
  applies to #X only. The concept text in 4.2/4.3 is ambiguous here; the code is the reference.
- RAM after wave 2 (nm, transitional, old MHeard and path table still present):

  | Family                | Topology (was matrix) | Rings     | Total stores    | Linker RAM before -> after |
  | --------------------- | --------------------- | --------- | --------------- | -------------------------- |
  | classic (E22, T-Beam) | 3,848 (1,332)         | 320 (160) | 8,665 (5,989)   | 95,584 -> 98,264           |
  | E22_XML               | 3,848 (3,156)         | 320       | 10,475          |                            |
  | S3 (Heltec V3)        | 9,736 (3,156)         | 640       | 21,883 (14,823) | 101,548 -> 108,628         |
  | nRF52 (RAK4631)       | 9,736 (3,156)         | 640       | 23,635 (16,575) | 81,408 -> 88,872           |

  The topology matches the concept (3,840 / 9,728 B plus header). Static extras: nRF52-only
  deferred SYM buffer 390 B; web and console take their row scratch from the heap per render.

- Advisor (Fable): APPROVED. Two low findings fixed at the gate: an echoed HEY group no longer
  overwrites the SNR mean of edge (x, 0) when `NBR_SNR_AVG_N > 1` (regression test fails
  before, passes after); the web page counts #X = -1 as 0 instead of 255 (nRF52 race).
  Advisor note for wave 5: the mask/edge consistency check exists only in host tests; the RAK
  test needs a firmware-side check.
- Gate: all native suites and golden green; 34 of 35 firmware envs build with >= 4 kB DRAM
  headroom (esp32-external-radio as before).

### Wave 3

- Contract: new `src/nbr_views.h` (MH view, `nbrMhRows/Get/Count`, `nbrNcnt/Air`, routes and
  horizon, name helpers, save/load with epoch rebasing); new feed API in `src/nbr_matrix.h`
  (`NbrDirectInfo` + `nbrNoteDirect`, `nbrNoteNcnt`, `nbrNoteOwnTx`, `nbrSetClock`). Stage-4 via
  fields are not stored yet.
- Envs: `native_nbr_views` (128 rows) and `native_topo_shadow` (old MHeard next to the
  topology, replay shadow over the DK5EN-98 capture 21.-24.09.).
- Writers: W3a (Opus) `nbr_matrix.*`, new `nbr_views.cpp`, nbr tests; W3b `lora_functions.cpp`,
  `loop_functions.cpp`, `time_functions.cpp`, `test/test_topo_shadow`.
- Bench smoke after wave 2 (T-Beam v1.2 classic 64 rows, T-Deck Plus S3 128 rows, both flashed
  with d8adf932 + the two advisor fixes): clean boot, free heap at the monitor point 148 kB /
  149 kB, the T-Beam filled 3 rows within its first minute.
- Result: direct slots (103 bit in 13 B), horizon, echo table, ME step off the path check,
  `nbr_views` and the RX feed. Byte sizes match build.py except the stage-4 via fields, which
  are not stored yet (13,864 B at 128 rows, 5,528 B at 64 rows).
- Replay shadow (`native_topo_shadow`) over DK5EN-98 21.-24.09. (4,122 minutes, both sides fed
  from the same `[LOG]` lines): MHeard set identical in 99.85 % of minutes (6 minutes window
  edge), count identical, DIST identical to 0.1 km in all 19,435 comparisons; every field
  difference is finding B2 (the old code lets a relayed HEY overwrite plt/mesh/ncnt of the
  direct entry); every old path-table sender is a row, a horizon entry or excluded by rule
  (DIRECT 16,840, own call as sender 3,514, own call elsewhere 7); never both row and horizon.
  The concept's 24 h live shadow is replaced by this (operator decision 3).
- Advisor (Fable): rework, all fixed: R1 a sender could appear as 2-hop row and horizon entry
  (HN report and pre-window edges refreshed the edge but not the row minute; horizon rule now
  equals "shown as a row", regression test fails before/passes after); R2 the matrix clock is set
  in one place, `Clock::SetClock()`, which every clock source runs through; R3-R6 small. The NTP
  hook in `ntp_async.cpp` was dropped (broke env `native`; the funnel covers NTP).
- Open for wave 4 (advisor R7): `topo.dat` should store raw UTC epochs, so a `--utcoff` change
  between save and load does not shift ages.
- RAM (nm): classic 10,345 B of stores (topology 5,528), S3 26,011 (13,864), nRF52 27,763
  (13,864) -- still transitional; MHeard and the path table go in wave 4.

### Wave 4

- Contracts: `src/mh_phone.h` (MH frame builder with 13 old + 7 new keys, live frame at most once
  per neighbour and minute, connect-time list) and `src/topo_ui.h` (one board hook for display
  refresh and `/topo.dat` every 10 min, one boot hook). Env `native_mh_phone`; the MHeard envs and
  entries are gone from the ini; `native_topo_shadow` keeps running against a frozen copy of the
  old MHeard under `test/test_topo_shadow/reference/`.
- Writers: W4a (Opus) removal + lora/via/time/aprs + tests; W4b web + console; W4c `mh_phone.cpp`,
  loop, both mains; W4d T-Deck, T-Deck Pro, `topo_ui.cpp` (raw UTC in `topo.dat`, advisor R7).
- RAM after the cutover (nm stores incl. rings; before = wave 0 baseline):

  | Family                | Before | After  | Net    | Concept 4.10 |
  | --------------------- | ------ | ------ | ------ | ------------ |
  | classic (E22, T-Beam) | 5,989  | 5,848  | -141   | -129         |
  | E22_XML               | 9,599  | 5,848  | -3,751 | -3,763       |
  | S3 (Heltec V3)        | 14,823 | 14,504 | -319   | -291         |
  | nRF52 (RAK4631)       | 16,575 | 14,504 | -2,071 | -2,043       |

  Rows 13/21 -> 64/128. The small extra saving is the stage-4 via fields, not stored yet.

- Deliberate changes: `--mheard`/`--path` print one `key=value` line per row instead of the old
  box table; the web path page is a real table; the app gets DATE/TIME in local time as before
  (the builder adds `node_utcoff` back, `getUnixClock()` is raw UTC); `topo.dat` holds raw UTC.
- The destination path is reset to the destination before `checkVia()` in both UDP handlers
  (Anhang E, stage 3). Deferred: the same reset before the server upload (Anhang E, loop and
  mains; `lora_functions.cpp` `addNodeData(RcvBuffer)` uploads the received via). It needs a
  copy of the frame because the relay reuses it; decide with stage 4.
- `docs/testplan/drift-matrix.csv` DR-28/DR-29 now point at `test_nbr_views` / `test_mh_phone`.
- For fork-neo: `tools/neo/paths/*.txt` still list `src/mheard_functions.*` and do not list
  `src/nbr_mask.h`, `src/nbr_views.*`, `src/mh_phone.*`, `src/topo_ui.*`; `derive.sh` would drop
  them. Update the path lists before this branch is projected onto fork-neo.
- Advisor (Fable): rework, all fixed: R1 no MH frame without a clock (the builder now checks the
  current time itself before back-dating; an age > 0 could otherwise wrap to a plausible date);
  R2 no `topo.dat` save without a clock, implausible saved epochs rejected, save interval stamped
  before the SD work (no retry storm without a card); R3 `nbrInit()` before `nbrLoad()`, a foreign
  or rejected image is deleted; R4 T-Deck tables refresh on tab switch; low items (stale LVGL rows,
  signed utcoff casts, `via_buf` static, nothrow allocation, dead defines). Regression tests:
  `test_mh_phone::test_no_frame_when_epoch_zero`, `test_udp_frame_twin` destination-path reset
  (fails without the reset on both platforms).
- Gate: all native suites and golden green, 34 of 35 firmware envs (esp32-external-radio as
  before); string scan on t_deck/t_deck_plus: `mheard.dat`/`mhpath.dat` only in the one-time
  delete. Linker RAM against the wave 0 baseline: E22 95,584 -> 95,456, Heltec V3 101,548 ->
  101,260, RAK4631 81,408 -> 79,748.
- Pre-existing, not changed: on ESP32 the display copy of a server position is taken before
  `checkVia()`, on nRF52 after it (only visible with a node via set).
