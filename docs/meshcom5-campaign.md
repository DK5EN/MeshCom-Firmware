# MeshCom 5 topology campaign

Implementation of stages 1 to 3 of `docs/meshcom5-topologie/meshcom5-topologie.html`
(Fassung 2, 2026-09-25) on branch `feature-neighbour-matrix`. Stage 4 (auto-via) and mcmap
M1 to M3 are out of scope. Run with `/orchestrate-waves`; this file is the resume point.

## Operator decisions (2026-09-25)

| Nr | Question                    | Decision                                                                                                                                                                                                                                                                                                                         |
| -- | --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1  | Free heap versus capacity   | Implement the concept's sizes (64/64 classic, 128/128 S3 and nRF52). Every size is a per-family constant under `#ifndef` in `configuration_global.h` so it can be reduced later with one edit.                                                                                                                                  |
| 2  | Base                        | Merge `fork-neo-test` (5180eef4, includes upstream KISS/TCP) into this branch. The sset4 boot migration is deleted, as its `#error` demanded.                                                                                                                                                                                  |
| 3  | 24 h live shadow hold       | Dropped. Replaced by a host replay shadow over the DK5EN-98 capture 21.-24.09. at the stage 2 gate; the nRF52 consistency check moves into the stage 3 field run. The on-device `[TOPO]\|DIFF` comparator is not built.                                                                                                         |
| 4  | On-air NCNT                 | Change it to the symmetric definition (concept 4.8). All on-air emitters (`R<n>`, `/N`, HEY group, HN `R<heard>`) cap at two digits: `NBR_NCNT_AIR_MAX 99`. Web and console show the uncapped value.                                                                                                                          |
| 5  | Phone MTU                   | No MTU work. The MH frame keeps the existing 245-byte limit; FailSoft drops the new fields first when a frame is too long.                                                                                                                                                                                                      |

## Waves

| Wave | Stage     | Content                                                                                                                                                                                                                                                                  | Status  |
| ---- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------- |
| 0    | sync      | Merge `fork-neo-test`, delete sset4 migration, baseline (native suites, golden, all firmware envs, nm symbols)                                                                                                                                                           | running |
| 1    | 1 prep    | W1a replay harness + 24.09 fixture (byte-identical `[NBR]` lines from the dense build); W1b `tools/nm_symsum.py`                                                                                                                                                        | open    |
| 2    | 1 core    | Header contract by the orchestrator; W2a `nbr_matrix.*` (edge pool, NbrMask, callsign word, share rule, counter halving, clamp, minute sweep); W2b mask consumers (txring, lora cover scan, web/command neighbours, loop, both mains)                                  | open    |
| 3    | 2 views   | W3a `nbr_views.*`, direct extension, horizon, echo table, ME step off the path check; W3b horizon/echo feed in `lora_functions.cpp` + replay shadow over 21.-24.09.                                                                                                     | open    |
| 4    | 3 cutover | W4a removal + core readers; W4b web + command; W4c mains + loop + MH JSON (7 new fields, FailSoft); W4d T-Deck, T-Deck Pro, `topo.dat`                                                                                                                                  | open    |
| 5    | 3 field   | Bench on the three USB nodes (T-Deck Plus DK5EN-14 reboot keeps topology, Heltec V3 DK5EN-1, T-Beam v1.2), then 24 h field run on DK5EN-98 and DK5EN-1. The RAK4631 DK5EN-90 nRF52 consistency test runs last, after everything else is finished (operator, 2026-09-25). | open    |
| 6    | docs      | `nbr-logformat.md`, `docs/architecture/09/10/11`, changelog                                                                                                                                                                                                              | open    |

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
