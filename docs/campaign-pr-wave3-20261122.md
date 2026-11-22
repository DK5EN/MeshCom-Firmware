# Campaign: upstream PR wave 3 (2026-11-22)

One upstream PR against icssw-org `dev` carrying every DK5EN issue implemented on `fork-dev`, plus two last fixes.
Base `fork-dev` `afde3da0`, `upstream/dev` `5db0a4f5` (#1313 merged, not yet synced into `fork-dev`).

## Operator decisions 2026-11-22

- D1 `Closes` for the 20 implemented issues: #1287-#1302, #1304, #1310, #1312, #1314. Not closed (not implemented):
  #1303, #1305-#1309, #1311.
- D2 POSINFO-LAG-01: fix in wave 1, file an upstream issue (English, assigned DK5EN), `Closes` it in the PR.
- D3 #1288: replace the `temp(aprswx)` workaround (`632aa9b3`) with a real fix before porting.
- D5 Neighbour-count beacon trigger (upstream code since v4.35p, never fired because of POSINFO-LAG-01): removed.
  The position beacon follows the configured interval (typically ~30 min); neighbour count and signal travel in
  the HEY frames.
- D6 Track mode / aprs.fi (operator: no spam, red warnings already seen on aprs.fi): automatic position beacons
  (periodic, track/smart beaconing, turn/distance shots) keep at least 60 s from the last position TX of any kind;
  aprs.fi warns below an average of about 30 s (developer, aprsfi group 2018). Manual `--sendpos`/button/phone keep
  the 30 s `BEACON_SHOT_MIN_MS`.
- D4 Push allowed: `fork-dev` to origin (desk link_fix), `pr/wave3` to origin, PR to icssw-org `dev`.

Not in this campaign: TBEAM-HEAP-01 (deferred, matters only for AU-25), ACK-CANCEL-01 (concept session, wave 7),
desk proposals P-0053/P-0104/P-0125 and P-0167 reopen (operator only).

## Waves

| Wave | Who                    | Exclusive files                                                                                  | State |
| ---- | ---------------------- | ------------------------------------------------------------------------------------------------ | ----- |
| 1    | POSINFO (Sonnet med)   | `src/posinfo_timer.h` (new), posinfo blocks of `esp32_main.cpp` / `nrf52_main.cpp`, its test     | done  |
| 1    | APRSWX (Sonnet med)    | `command_functions.cpp` aprswx rows + status rung, `test/golden/toggle_table_lint.py`, cmd tests | done  |
| 1    | port map (Haiku scout) | read-only                                                                                        | done  |
| 1    | orchestrator           | `platformio.ini` (`test_posinfo_timer` in `[env:native]`), BACKLOG, RESUME, this doc             | -     |
| 2    | orchestrator           | `/sync-upstream`: merge `upstream/dev` #1313 into `fork-dev`, push `fork-dev`, desk links        | open  |
| 3    | orchestrator           | `/submit-pr`: `pr/wave3` from `upstream/dev`, port, gate, German body, Opus advisor, open PR     | open  |

Gate wave 1: `tools/regression.sh --stage 1,2`, board builds of touched envs, Opus advisor, commit. Bench: `[POS]`
timestamps vs interval on DK5EN-1.

PR body notes carried over: T114 D4 (old `--checkmesh` bit `node_sset2` 0x2000), T5_EPAPER and T-Deck Pro bypass the
msgwake gate, BLE/app not exposed yet; T-Deck click with an empty ring no longer sets `pageHold`; comment-only nits
from wave 3 (`docs/campaign-wave3-fixes-20261122.md`, "Deferred to the PR cut").

## Port map (scout, wave 1, read-only merge-tree per commit against `upstream/dev` `5db0a4f5`)

- #1298 TBEAM-SLEEP-01 is `d0ecf82f` (not in the first list): add it. Src clean, `test_command_setters` conflicts.
- Clean or docs-only conflicts: `632aa9b3` (to be replaced by the wave 1 fix), `f4af55b0`, `7e553d12`, `a43dd28e`,
  `a91d0e1d`.
- Src conflicts: `e8188e3c` (extudp, needs `src/extudp_refresh.h` from `6afba162`), `b08c5ea5`/`885fa3b3`
  (`loop_functions.cpp`; `885fa3b3` needs `src/clock_instrument.h` from `244cd0c6`, INSTRUMENT), `c22cb3d2`,
  `1830d8ed`, `45732840`, `57acc7c3` (mostly `platformio.ini`, `loop_functions.cpp`, nrf52 main).
- Fork-only, stays out: `docs/`, `tools/bench/`, `tools/tests/`, `tools/w5100s_patch.py`, golden lints and fixtures.
  Native envs in `platformio.ini` hunks: decide per env at the port (upstream has `[env:native]`?).
- Size: 86 files in src/test/platformio.ini, about +5600 / -640.

## Wave 1 advisor (Opus) -- REWORK

1. `test_posinfo_timer` models the loop in its own Sim; reverting the mains passes it. Add a source-scan pin.
2. Rollover assert can never fail (one send); no `first_window` case.
3. Stale comment `src/loop_scheduler.h:18-22`; `posinfo_timer.h` "HEAD" wording.
4. Instant beacon stamps only `lastOwnPosTx` (`loop_functions.cpp:5797`), so a periodic beacon may follow 1 s later.
5. Track cadence rises to 30 s -> D6.
   Checked fine: NCNT removal, #1288 BLE tail, lint check 9. `--aprswx foo` now does nothing (was status): PR text.
   Rework -> fresh implementer, Sonnet high: source-scan pin, rollover/first-window cases, comments, 60 s gate from the
   later of `posinfo_timer_min` and `lastOwnPosTx` (`haveOwnPosTx()`/`lastOwnPosTxMs()`, `loop_functions.h`). Gate
   Unity 4160/4160, pytest 1639, 13 boards.

## Wave 1 advisor round 2 -- small rework, done by the orchestrator

EXTUDP tele (`extudp_functions.cpp` `handleExternTelemetry`) sent `sendPosition(0x9999)` per datagram (30 s floor
only): now gated by `posinfoAutoSpacingOk(... POSINFO_AUTO_MIN_MS)`, deferred via `posinfo_shot` (pin
`test_extudp_tele_beacon_gated`, red before). Stale comments in `beacon_rate.h`, `loop_functions.cpp`. PR text:
`--posshot` and the app position push now wait for the 60 s gate (delayed, not lost); `--aprswx <junk>` now does
nothing (was status); first-fix beacon at ~100 s after boot now actually goes out.
`test_getextern` (env `native_extern`) stubs the new accessors; its tele test now checks send / defer / send at 60 s
(red on the old code: `Expected 1 Was 2`).

Wave 1 done: final gate `tools/bench/runs/harness/gate-prwave3-w1d.summary` (Unity 122 envs 4161/4161, pytest 1639,
13 boards, deferred-log string in the Heltec V3 and RAK images). Advisor round 1 REWORK (fresh Sonnet high), round 2
small rework done by the orchestrator with fail-before tests; no third advisor pass (orchestrator-sized change, every
item pinned by a red-before test). Bench `[POS]` timestamps: with the PR branch on DK5EN-1.
