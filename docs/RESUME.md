# RESUME -- pick up here

**Resume here: section "Now" below. Next date: upstream wave 3, two to four fix PRs by Thu 2026-11-26.** Active
campaigns: [campaign-ticket-triage-20261121.md](campaign-ticket-triage-20261121.md) (the fixes),
[campaign-release-train-20261115.md](campaign-release-train-20261115.md) (waves). Release plan: operator's Desktop HTML,
Fassung 9; working checklist: operator's Desktop `MeshCom-Checkliste-20261115.md`.

## Now (2026-11-21 late)

- **Wave 3 fix campaign running:** [campaign-wave3-fixes-20261122.md](campaign-wave3-fixes-20261122.md) (resume
  point: section "Next"). Wave 1 `c22cb3d2` (#1291, #1289, #1290, #1296, #1301) and wave 2 `1830d8ed` (#1295, #1300, #1302) fixed
  and gated, bench pending. Waves 3-4 (APRS/TDECK, display paging), then the bench. Desk links wait for a push.
- **Wave 3 by 2026-11-26 (operator 2026-11-21):** 2-4 topical PRs from `upstream/dev` (network/gateway,
  commands/APRS, display/position, update/sensors). Content: fork fixes already done (ETH-06, `temp(aprswx)` as a
  real fix, CMD-DASH-01, AU-27, TD-LOCK-01, TBEAM-SLEEP-01) plus the ticket fixes that pass gate and bench by
  2026-11-24; the rest goes with wave 4 or a second fix PR. Order: SEC-DUMP-01 first, then plan by plan.
- **Upstream issues** (icssw-org, English, assigned to DK5EN, created 2026-11-21; PR bodies use `Closes #N`):

  | Issue | Topic                                  | Issue | Topic                                      |
  | ----- | -------------------------------------- | ----- | ------------------------------------------ |
  | #1287 | ETH-06 RAK hang after W5100S reset     | #1300 | OTA-SAFEBOOT-01                            |
  | #1288 | `--aprswx on/off` fall-through (#1269) | #1301 | TBEAM1W-PWR-01 (SX1262/LLCC68 PA)          |
  | #1289 | RAK-ETH-DNS-01, RAK-NETMODE-01         | #1302 | DHT-PROBE-01                               |
  | #1290 | GW-GROUP-01, GW-DM-01, GW-CALL-01      | #1312 | TDECK-VOLT-01                              |
  | #1291 | SEC-DUMP-01                            | #1303 | wave 4: RM cards, RM1 `pins`/`gpio`        |
  | #1292 | CMD-DASH-01                            | #1304 | wave 4: RM-LOSTREPLY-01                    |
  | #1293 | APRS-TXT-01..03                        | #1305 | wave 4: RAM-WSP-01                         |
  | #1294 | DISP-PAGE-01..03, BTN-OFF-01           | #1306 | wave 4: QA-RX-03, BL-02 (RX path)          |
  | #1295 | GPS-AUTOSYM-01..03, POSINFO-NEXT-01    | #1307 | wave 5: `--udphold`                        |
  | #1296 | WEB-TEMPOFF-01                         | #1308 | wave 5: EXTUDP register query              |
  | #1297 | TD-LOCK-01                             | #1309 | wave 6: AU schedule, AU-23, SB-06          |
  | #1298 | TBEAM-SLEEP-01                         | #1310 | clock: CET-SCATTER-01                      |
  | #1299 | AU-27                                  | #1311 | wave 7: ACK-AGE-01, CSMA-01/03/05, IDEE-04 |

  Not filed: AppLink (upstream #1276 exists; never post there), POSINFO-LAG-01 (no plan yet).

- **Upstream state:** no PR of ours open; `upstream/dev` `4164f180` (#1286) synced as `66aeb04d`. Merged on
  2026-11-21: #1284 (TRACK W/S), #1285 (REVIEW.md), #1286 (nav buttons, pong id). No `pr/*` branch or extra worktree
  exists.
- **Operator open:** accept the desk close proposals P-0053, P-0104, P-0125; reopen P-0167 at the next problem-file
  sync; CET-RESTAMP-01 answers; NIGHTLY-01 parked; remote branch `origin/backup/fork-dev-code-review-xdxqzv-fbb5e966`
  (19 commits not in fork-dev, 2026-10-26) kept until the operator decides.
- **Field open:** ETH-06 confirmation on DB0AIS-12; AppLink app side with the app maintainer; app side of unsigned
  pong ids.

## State 2026-11-21

- **Releases** `v4.40a.11.21` (AU-27, #1284, #1269 with `temp(aprswx)`) and `v4.40a.11.22` (Latest; ETH-06, AppLink
  APPLINK-01 off by default, CMD-DASH-01, WEB-NAV-02, PONG-SIGN-01), both cut from `fork-dev` by operator decision.
  11.22: CI run `44939256914`, gate 4012 Unity in 116 envs and 1632 pytest, 69 assets verified, web flasher
  `--keep 1`, Pages synced.
- **Bench:** ETH-06 verified on hardware with the new hook `--ethdrop hw`; AppLink verified on Heltec V3 and RAK incl. a
  chip reset with a session up (`tools/bench/runs/harness/bench-20261121-eth06-applink.summary`). New Heltec V3 is
  DK5EN-1 (chip erased first).
- **Ticket triage** ([campaign-ticket-triage-20261121.md](campaign-ticket-triage-20261121.md)): 59 open desk tickets
  checked, 33 closed as already implemented, 11 plans (`docs/plan-*-20261121.md`, incl. TDECK-VOLT-01 added later),
  backlog rows in 3.2, operator decisions recorded in each plan. Nothing implemented.
- **Upstream syncs:** #1285 as `29d9d65c`, #1284 as `0110fd3c`, #1286 as `66aeb04d`; each merged tree equal to the
  pre-merge tree.
- Full detail of the day and of 2026-11-15 .. 2026-11-20:
  [archive/resume-journal-20261115-20261121.md](archive/resume-journal-20261115-20261121.md).

## Fleet

- **DK5EN-98** (production gateway) and **DK5EN-14** (T-Deck Plus, 192.168.68.77): CI image `v4.40a.11.22` via OTA.
  DK5EN-98: `--setlog on` until the ACK-AGE-01 run under load, then off (OK given 2026-11-15); any other work needs
  an explicit OK.
- **DK5EN-1** (new Heltec V3, .80), **DK5EN-90** (RAK, INSTRUMENT, .78), **DK5EN-92** (T-Beam): local images of the
  11.22 commit; AppLink off, ExtUDP off on -1 and -90.

## Running campaigns

- **Ticket triage 2026-11-21:** [campaign-ticket-triage-20261121.md](campaign-ticket-triage-20261121.md) -- the fixes for wave 3.
- **Release train 2026-11-15:** [campaign-release-train-20261115.md](campaign-release-train-20261115.md) -- the active one.
- **Channel access stabilisation** (ACK-AGE-01, ACK-CANCEL-01, CSMA-04/02, CSMA-03, PWR-01/02, soak): dates and exit
  criteria in the campaign doc, section "Parallel tracks".
- **Upstream waves 2-7:** the release plan is the operator's Desktop HTML (Fassung 9, 2026-11-21), not a repo file; the
  repo copy was removed 2026-11-16 (operator: double work, always outdated). Wave list also in the campaign doc.
- **GPSV4-01, GPIO-01, INA226-HELTEC-01:** field feedback only (BACKLOG "Field").
- Closed, history only: IMPL-TRAIN ([campaign-impl-train-20261107.md](campaign-impl-train-20261107.md), CLK-01 S2-S4
  moved to B-1), BACKLOG-ROUND ([campaign-backlog-round-20261108.md](campaign-backlog-round-20261108.md)), CLK-01
  ([campaign-clk-01-20261107.md](campaign-clk-01-20261107.md), upstream in #1277).

## Rules and recipes

- Branch model, gates, toolchain traps, bench rules: [CLAUDE.md](../CLAUDE.md). Bench fleet, ports, flashing: the
  local `/bench` skill and `tools/bench/fleet.json`.
- Every local fork build: `export MC_AU_DEV_CHANNEL=1` (dev update channel is a build flag since `6c84a46c`).
- Releases: `/release-firmware`; never a same-day `.N` release (the AU tag parser rejects it). Read `AU-24` (T-Beam
  Auto Update download) in the backlog before the next release.

## Where things are

- Open work: [BACKLOG.md](BACKLOG.md) 3.2. Closed rows since 2026-11-16:
  [archive/backlog-closed-20261116.md](archive/backlog-closed-20261116.md), since 2026-11-15
  [archive/backlog-closed-20261115.md](archive/backlog-closed-20261115.md); full row history:
  [archive/backlog-rows-20261107.md](archive/backlog-rows-20261107.md), older
  [archive/backlog-closed-20261024.md](archive/backlog-closed-20261024.md).
- Fork vs upstream feature list: [features-vs-upstream-20261114.md](features-vs-upstream-20261114.md).
- Defect IDs: [architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md). Tests:
  [test-suite-map.md](test-suite-map.md).
- Releases: [release-journal.md](release-journal.md), [release-notes.md](../release-notes.md). Plan view:
  [campaign-gantt.html](campaign-gantt.html).
- Session journals that used to live here: [2026-11-15 .. 2026-11-21](archive/resume-journal-20261115-20261121.md),
  [2026-11-07, incl. this page before the debloat](archive/resume-journal-20261107.md),
  [2026-10-23 .. 2026-10-24](archive/resume-journal-20261024.md),
  [2026-10-05 .. 2026-10-23](archive/resume-journal-20261005-20261023.md),
  [2026-09-03 .. 2026-10-01](archive/resume-journal-20260903-20261001.md).
