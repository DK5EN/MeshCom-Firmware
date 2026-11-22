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
| 2    | orchestrator           | `/sync-upstream`: merge `upstream/dev` #1313 into `fork-dev`, push `fork-dev`, desk links        | done  |
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

## Wave 2 -- done

`upstream/dev` `5db0a4f5` (#1313, our `tools/hmac_connect.ps1`, = `ee7a8e56`) merged as `4f555c2e`; merged tree equal
to the pre-merge tree, no gate needed. `fork-dev` pushed to origin. Upstream carries its own tag `v4.40a.11.22` on
`5db0a4f5`, clashing with the fork release tag of that name (left as is; fetch warns). Desk links: P-0380, P-0154,
P-0163, P-0381, P-0382, P-0210, P-0385, P-0030 -> `c22cb3d2` done; `1830d8ed` (P-0383, P-0166, P-0167, P-0081,
P-0209, P-0351) and `45732840` (P-0044, P-0386, P-0179, P-0021, P-0384, P-0059, P-0345, TDECK-VOLT-01 ticket)
refused "no commit in firmware" (desk has not fetched the push yet): retry after wave 3.

## Wave 3 -- log

- Issue #1315 filed (POSINFO-LAG-01, English, assigned DK5EN, label bug).
- Worktree `../MeshCom-PR-next`, branch `pr/wave3` from `5db0a4f5` (pinned SHA). 17 commits, 62 files, +1991 / -481.
  Port notes: ETH-06 without the W5100S library patch script (`extra_scripts`, stays in the fork) and without
  EXT-PEER-01/extReg; `extudp_refresh.h` carries only the two ETH-06 helpers. CET Part B: `CLK_SRC()` defined in
  `clock.h` (note only, no INSTRUMENT), 11 set sites as on the fork. DHT-PROBE-01 without SENS-03 (`read_ok` local,
  `dht_ok` kept for `--info`). QA-RX-07 `isSpecialTargetCall` not ported (#1306). Comments cite no fork test/doc/desk
  paths (separate chore commit). `wpLiveNewest` extern moved to `loop_functions.h` (deferred nit).
- PR body draft: `../pr-wave3-body.md` (outside the repo), German, 21 `Closes`.
- Gate: all 36 board envs + upstream native envs in the worktree, fork host suites on an overlay copy (scratchpad);
  Opus advisor on the PR diff. Bench: POSINFO `[POS]` timestamps on DK5EN-1 with the PR build.
- Advisor (Opus) on the PR diff: one port defect fixed (`--info` NETWORK label used fork-only `HAS_NRF_ETH`; now
  `BOARD_RAK4630 && !BOARD_HELTEC_T114 && !BOARD_T_ECHO`, folded into the W1 commit), stale `charge_led_mode.h`
  comment; body items added (Track frame +9 B, PA at runtime, routing no LoRa change, beacon airtime, sset bits).
- Gate: 35/36 board envs (`esp32-external-radio` = upstream `#error`, baseline), upstream native 6/6 (291 cases);
  fork suites on the overlay: 3119/3182, every failure triaged to a fork-only feature (no port defect).
- Branch: 16 commits, 63 files, +1994 / -483. Waiting for the operator's `yes` (push `pr/wave3`, `gh pr create`).
- Operator 2026-11-22: W5100S library patch script goes into the PR after all (upstream already carries
  `pre:tools/nimble_patch.py`, same pattern; S-03 did not apply). Commit `74ed647d`; RAK build shows
  `w5100s_patch: ETH-06 applied`. Branch now 17 commits.
- Bench POSINFO-LAG-01 on DK5EN-1 (PR image, INSTRUMENT, `--airgap on` confirmed before any beacon, placeholder
  position 48.0 N / 11.0 E, reset afterwards). Run 1: first-fix beacon at 100 s after boot (never fired before the
  fix). No periodic beacon in 23 min: GPS on without a module lets the main loop reset `posinfo_interval` to 1800 s
  after 10 GPS polls (`esp32_main.cpp:4677-4684`, upstream behaviour, not the fix). Run 2 with `--gps off`: PASS (first-fix 100.0 s, periodic +300.2 s, after `--sendpos` +59.9 s),
  `tools/bench/runs/harness/bench-posinfo-lag-20261122.summary`. DK5EN-1 back on the fork-dev image, identity
  guard ok.

## Wave 3 -- done

PR [#1316](https://github.com/icssw-org/MeshCom-Firmware/pull/1316) opened 2026-11-22 against `dev`: 17 commits, 65
files, +2093 / -483, 21 closing references (GraphQL `closingIssuesReferences` 21). Body: `../pr-wave3-body.md`.
Desk links still open (desk index at fork-dev `ee7a8e56`, 2026-11-21 22:17): `1830d8ed` -> P-0383, P-0166, P-0167,
P-0081, P-0209, P-0351; `45732840` -> P-0044, P-0386, P-0179, P-0021, P-0384, P-0059, P-0345, TDECK-VOLT-01 ticket.
After the merge: `/sync-upstream` (merge path), `git worktree remove ../MeshCom-PR-next`, delete `pr/wave3`.
