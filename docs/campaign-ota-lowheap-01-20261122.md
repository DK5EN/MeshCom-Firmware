# Campaign: OTA-LOWHEAP-01 (2026-11-22)

ESP32-S3 without PSRAM (Heltec V3, T-Beam Supreme) refuses Auto Update with "Not enough free memory" after long
uptime. Fork issue DK5EN/MeshCom-Firmware#3, analysis [rca-ota-lowheap-01-20261122.md](rca-ota-lowheap-01-20261122.md).
Subsumes AU-24 and AU-25. Base `fork-dev` `d7503fb1`. Auto Update is fork-only firmware: no upstream PR in this
campaign (it goes with upstream wave 6).

State: **plan, waiting for the operator's OK and answers to Q1-Q4.**

## Code facts (scout, 2026-11-22, against `fork-dev`)

- Gate `heapGateEval()` `src/esp32/fw_update_net.cpp:701`; S3: `FWNET_MIN_FREE` 0 (`:128-132`), largest block 17000
  (`:121`), `twoRecBufProbe()` `:648` takes two 16717 B blocks at once (`src/fw_update.h:876`).
- BLE pause `AU_BLE_PAUSE` is 1 only for `CONFIG_IDF_TARGET_ESP32` (`src/esp32/fw_update_net.h:240-244`); pause is
  `esp32BlePause()` `src/esp32/esp32_main.cpp:670`. Only the forced pause has an S3 crash note (`:725-726`, NimBLE
  host task panics on deinit/reinit).
- Job restart `fwJobRestartDue()` `src/fw_update.h:1101` needs `bleDown`, so it never fires on S3
  (`fw_update_net.cpp:1523-1534`).
- `twPurge()` `:689` and `shedHold()` `:738-752` already run on S3.
- STAT line (`src/setlog_lines.cpp:85-99`) has `heap=` only, no largest block.
- `tools/resource_watch.py` runs only as `--self-test` (`tools/regression.sh:240`); not in CI. Baseline 2026-09-11.
  `meshcom-ci.yml` captures no `RAM:` line and uploads no `.elf`/`.map`.
- No "update pending, run after reboot" mechanism exists; NVS holds only the staged download record.
- Host tests: `native_fw_update` (`test/test_fw_update`, covers `heapGateEval`/`fwJobRestartDue` helpers),
  `native` (`test_au_ui_state`), `native_au_schedule`.

## Waves

| Wave | Who                                  | Exclusive files                                                                                                                        | Verification                                            | State |
| ---- | ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------- | ----- |
| 1a   | RAM-HUNT, implementer Sonnet high    | read-only on `src/`; writes only `docs/rca-ota-lowheap-01-20261122.md` section 3.2 addendum; worktree at the 09-11 SHA by orchestrator | `nm_symsum.py` diff 09-11 vs HEAD Heltec V3, top 20     | plan  |
| 1b   | CI-BUDGET, implementer Sonnet medium | `tools/resource_watch.py`, `tools/resource_baseline.json`, `.github/workflows/meshcom-ci.yml`, its pytest                              | pytest of `resource_watch`, `ci-proof-*` tag run        | plan  |
| 1c   | STAT-IBLK, implementer Sonnet medium | `src/setlog_lines.{h,cpp}`, STAT fill site `src/loop_functions.cpp` ~4100, setlog Unity test, log parsers that split STAT              | own native env, fail-before test                        | plan  |
| 2    | orchestrator, bench (single)         | DK5EN-1 only, INSTRUMENT build of HEAD with 1c                                                                                         | R6: 1 h web bursts + forced checks, largest-block curve | plan  |
| 3a   | S3-RESCUE, implementer Sonnet medium | `src/esp32/fw_update_net.{h,cpp}`, `src/esp32/esp32_main.cpp` BLE pause block, `test/test_fw_update`                                   | `native_fw_update`, Heltec V3 + T-Deck Plus build       | plan  |
| 3b   | FRESH-BOOT, after 3a (shares files)  | `src/fw_update.h` pending-flag logic, `fw_update_net.cpp` job entry, `esp32_main.cpp` setup hook, `test/test_fw_update`                | same                                                    | plan  |
| 3c   | RAM-RECLAIM (only if 1a finds waste) | files named by 1a                                                                                                                      | 13 boards, RAM delta per class                          | plan  |
| 4    | orchestrator                         | gate `--stage 1,2`, 13 boards, Opus advisor, bench T3 on DK5EN-1 (S3) and DK5EN-92 (classic), docs, release decision                   |                                                         | plan  |

Wave 1 has three disjoint writers; 1b and 1c hold no shared build target (1b is Python, 1c one native env); 1a builds
in its own worktree, sequentially behind the pio lock. Wave 3a and 3b share `fw_update_net.cpp` and `esp32_main.cpp`:
serialized.

## Open questions for the operator

- Q1 Measure first (wave 1a and the 1 h bench repro in wave 2) before the fixes, or ship 3a and 3b at once.
- Q2 TLS reserve block (rca section 6 step 3, about 34 KB less heap in normal operation): only if wave 2 shows the
  fixes in 3a/3b are not enough.
- Q3 CI static RAM budget: fail the build above +1 KB per env without a baseline update, or warn only.
- Q4 Overnight run R1 on DK5EN-1 with the field image `v4.40a.11.21` (bench node, standing OK) in addition to the 1 h
  recipe.

## Log

- 2026-11-22: analysis moved from the Desktop into `docs/`, reporter call signs replaced by "Field 1/2". Backlog row
  `OTA-LOWHEAP-01` (Implement), AU-24 and AU-25 point to it. Scout verified the code facts above.
