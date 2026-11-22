# Campaign: OTA-LOWHEAP-01 (2026-11-22)

ESP32-S3 without PSRAM (Heltec V3, T-Beam Supreme) refuses Auto Update with "Not enough free memory" after long
uptime. Fork issue DK5EN/MeshCom-Firmware#3, analysis [rca-ota-lowheap-01-20261122.md](rca-ota-lowheap-01-20261122.md).
Subsumes AU-24 and AU-25. Base `fork-dev` `d7503fb1`. Auto Update is fork-only firmware: no upstream PR in this
campaign (it goes with upstream wave 6).

State: **wave 1 running (2026-11-22 evening): overnight bench run (red first), CI RAM budget.**

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

## Operator decisions 2026-11-22

- D1 Measure and reproduce first (red), fix after: no fix wave before the overnight run has shown the refusal.
- D2 No reserved TLS buffers (about 34 KB less heap all the time; other allocations would fail first, with reboots
  under load as the likely result).
- D3 CI static RAM budget: fail above +1 KB per env over the committed baseline; an env without a baseline fails.
- D4 Overnight run on all bench nodes with the current INSTRUMENT build of `fork-dev` (`--heaplog 60`): DK5EN-1
  (Heltec V3, S3 without PSRAM), DK5EN-14 (T-Deck Plus, S3 with PSRAM), DK5EN-92 (T-Beam, classic), DK5EN-90 (RAK,
  heap curve only). DK5EN-98 as read-only reference (`meshlogger --no-debug-flags`, BLE connected to McApp; no
  command, no flash).
- D5 DK5EN-1 runs as gateway and transmits normally at 2 dBm for this night (exception to the no-broadcast rule,
  operator 2026-11-22).
- D6 Trigger 2026-11-23 06:00 from the Mac over the 2323 console: per ESP32 node `--update check`, `--update install`
  (stages only, never `apply`), `--update forget`, three attempts 2 min apart. Red = `lowheap` refusal.
- Operator hypothesis "heard messages queue up in RAM without a BLE client": three scouts found no unbounded growth.
  Phone ring (`loop_functions.cpp:834`) is a fixed 2-3 KB byte ring that evicts the oldest frame; NimBLE buffers
  nothing without a client (`esp32_main.cpp:2635`); all tables are fixed arrays, all malloc sites paired. Remaining
  suspects are fragmentation sources: per-page 1.4 KB `web_txbuf` block (`web_txbuf.cpp:113`), lwIP TIME_WAIT pcbs
  (purged only when the gate refuses, `fw_update_net.cpp:689`), the nightly TLS checks (about 56 KB in many pieces),
  plus the 21 KB static growth. The overnight curves decide.

## Operator decisions 2026-11-22, option A (static tables on ESP32-S3 without PSRAM)

Basis: [ota-lowheap-ram-hunt-20261122.md](ota-lowheap-ram-hunt-20261122.md) (09-11 reproduced to the byte at
`291f375a`, +21,224 B fully attributed; 71 % upstream, 6,080 B fork-only; whole classic tier -29,736 B measured).
Scope: S3 boards without PSRAM only; PSRAM boards and the RAK4631 keep the large tier.

- D7 (1C) Msgstore mailbox (13,547 B with the STOR announce tables): allocate only when the store mode is on, taking
  effect after a reboot (allocation on a fresh heap); with the mode off (factory default, `msgstore.cpp:33`) nothing
  is reserved. Refusal message if the allocation fails.
- D8 (2B) UDP outage hold `UDP_HOLD_BYTES` 6,144 -> 1,024 B (about 10 instead of 60 held texts on a gateway),
  saving 5,120 B.
- D9 (3A) Neighbour matrix stays at 128 rows (14,248 B).
- D10 (4B) `RING_BYTES_PHONE` and `RING_BYTES_UDP` 3,072 -> 2,048 B each, saving 2,048 B.
- Expected total on a V3 with the mailbox off: about 20.7 KB (13,547 + 5,120 + 2,048), close to the 09-11 level.
- Order (D1): implemented only after the overnight run has shown the refusal (red); then fix B (update after a fresh
  boot) and these cuts in one fix wave, each with a test that is red before.

## Wave 1 dispatch (2026-11-22)

- 1-bench (Sonnet medium): INSTRUMENT builds, flash -1/-14/-92/-90, gateway on -1, loggers to
  `tools/bench/runs/harness/ota-lowheap-night-20261122/`, trigger `tools/bench/ota_lowheap_trigger.py` at 06:00.
- 1b (Sonnet medium): CI RAM budget in `meshcom-ci.yml` + `resource_watch.py`; baseline refresh by the orchestrator
  from a `ci-proof-*` run at the gate.
- 1a (RAM hunt, symbol diff 09-11 vs HEAD) and 1c (largest block in the STAT line) start after the bench builds
  are done (one pio at a time; 1c edits `src/`, which must not land in tonight's image).

## Log

- 2026-11-22: analysis moved from the Desktop into `docs/`, reporter call signs replaced by "Field 1/2". Backlog row
  `OTA-LOWHEAP-01` (Implement), AU-24 and AU-25 point to it. Scout verified the code facts above.

## Wave 1b result (2026-11-22, not committed yet, waits for the stage 1,2 gate)

- `resource_watch.py gate` + CI step after `pio run` (`meshcom-ci.yml`, build log via `tee`); fails above baseline +
  1024 B, on a missing baseline entry, and on an env without RAM/Flash line. `update --log` refreshes from CI logs.
- Orchestrator fix: the header regex only knew `(platform:`; real CI logs print `(board: ...; platform: ...)`, so the
  first version saw 5 of 33 envs. Test `test_gate_reads_real_ci_header_format` red before, green after.
- Baseline refreshed from CI run `44939256914` (`v4.40a.11.22`, 33 envs, gate green on that log). The refresh drops
  the 2026-09-11 linker-region entries (`dram0_0_seg`, `iram0_0_seg`); they were informational (`dram` reads the map),
  and `snap` re-adds them from local builds.
- Evidence from the real log against the 09-11 baseline: every S3 env +20 to +25 KB, classic ESP32 -5 to -7 KB,
  nRF52 +7.5 to +15 KB. Next release tag: red if `fork-dev` grew more than 1 KB per env since 11.22.

## Wave 1 bench setup (2026-11-22 14:30, done)

- INSTRUMENT images of `1184177d` (`MC_AU_DEV_CHANNEL=1`, ELF markers `INSTR-HEAP` 5/4, `heaplog` 10/5): DK5EN-1 .78
  (USB, gateway on, connected to the server, 2 dBm), DK5EN-92 .61 (USB), DK5EN-14 .77 (WiFi OTA), DK5EN-90 .80 (DFU;
  nRF52 prints `not_available_on_this_board`, no heap telemetry). AU mode notify everywhere, TX 2 dBm, identity
  guard ok. DK5EN-98 .65 untouched, logger `--no-debug-flags`.
- Loggers until 2026-11-23 07:30 in `tools/bench/runs/harness/ota-lowheap-night-20261122/<call>/`;
  `reboot_watch.py` re-sends `--heaplog 60` after a reboot and logs `reboot-events.log`; `caffeinate` keeps the Mac
  awake. Trigger `tools/bench/ota_lowheap_trigger.py` armed for 2026-11-23 06:00 (DK5EN-1, -92, -14; refuses -98
  and -90; no `apply`). Dry run on DK5EN-92 passed (login, PAUSE hand-over, parsing).
- First readings (internal heap, bytes):

  | Node                   | Image            | Uptime | Free    | Min ever | Largest block |
  | ---------------------- | ---------------- | ------ | ------- | -------- | ------------- |
  | DK5EN-1 Heltec V3 (S3) | INSTRUMENT HEAD  | 586 s  | 98,412  | 77,572   | 86,004        |
  | DK5EN-98 Heltec V3     | CI 11.22, BLE up | 17 h   | 103,020 | -        | - (STAT only) |
  | DK5EN-14 T-Deck Plus   | INSTRUMENT HEAD  | 507 s  | 60,176  | 45,436   | 51,188        |
  | DK5EN-92 T-Beam        | INSTRUMENT HEAD  | 497 s  | 56,876  | 36,444   | 55,284        |

  Reading: the V3 idles near 98 KB free, about 21 KB below the old 119 KB comment (matches the static growth). The
  largest block at boot (86 KB) is far above the gate's 2 x 16.7 KB, so a fresh V3 passes; the refusal needs the
  heap to break up over uptime. The night decides.

## Evening check and web load (2026-11-22 19:50)

- After 4.7-5.5 h, no reboot on any node. Total free heap is flat on every node: no leak.
- The largest block drops exactly once, at the first automatic AU check 10-40 min after boot (TLS session), and is
  flat afterwards:

  | Node                 | Free (range)   | Largest block boot -> now | Min ever (during the AU check) |
  | -------------------- | -------------- | ------------------------- | ------------------------------ |
  | DK5EN-1 Heltec V3    | 96,572-100,132 | 86,004 -> 61,428          | 14,404                         |
  | DK5EN-98 Heltec V3   | 92,080-97,988  | 86,004 -> 59,380          | 11,052                         |
  | DK5EN-92 T-Beam      | 52,224-57,032  | 55,284 -> 36,852          | 9,840                          |
  | DK5EN-14 T-Deck Plus | 41,396-63,664  | 55,284 -> 21,492..38,900  | 30,320 (TLS buffers in PSRAM)  |

- Prognosis without web load: the V3 keeps about 61 KB in one block, far above the gate's 2 x 16.7 KB, so the 06:00
  trigger would likely stage cleanly (green). The field failures came through the web GUI (RCA H1/H2), which the
  console-only trigger lacks.
- Operator decision 19:50: open the web GUI of all 5 nodes (DK5EN-1, -92, -14, -98, -90) in Chrome and cycle the
  pages overnight. Each tab runs an in-page cycler (`loadPage()` every 60 s through info, messages, mailbox, wx, pins,
  position, mheard, path, neighbours, rxlog, setup; spectrum and remote left out; GET only, nothing sent on air).
  First effect: free heap -2 to -3 KB on the V3s, -14 KB on the T-Deck. The cycler dies if a tab reloads or Chrome
  discards it; check the tab counters (`window.__mcCycN`) in the morning.

## Ratchet test: repeated AU checks (2026-11-22 21:20-21:36, DK5EN-1, under web load)

- Question (operator): does each AU check leave something behind, so the largest block shrinks check by check?
- Method: 3 x `--update check` over the net console, 5 min apart, no install
  (`tools/bench/runs/harness/ota-lowheap-night-20261122/ratchet.summary`).

  | Round | Free before -> after | Largest block before -> after | Lowest free during TLS | Min ever |
  | ----- | -------------------- | ----------------------------- | ---------------------- | -------- |
  | 1     | 96,280 -> 96,492     | 61,428 -> 61,428              | 22,912                 | 12,556   |
  | 2     | 96,036 -> 96,036     | 61,428 -> 61,428              | 22,500                 | 10,904   |
  | 3     | 96,492 -> 96,040     | 61,428 -> 61,428              | 22,876                 | 10,904   |

- Result: no ratchet. The split at the first check after boot (86,004 -> 61,428) happens once; later checks reuse the
  same holes and return all memory. The TLS session leaves about 22.5 KB free at its peak every time. The lower
  min-ever (14.4 -> 10.9 KB) is the web load (about 3 KB) overlapping the TLS peak, not a leak.
- Consequence: repeated nightly checks alone do not explain the field refusal on the bench V3. Remaining candidates:
  what the download (install) itself allocates on top of the check, a phone on BLE (H2), or heap use the bench
  does not have (more neighbours, mailbox in use). The 06:00 run tests the download under web load.
