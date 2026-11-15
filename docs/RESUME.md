# RESUME -- pick up here

**Resume here: [campaign-release-train-20261115.md](campaign-release-train-20261115.md).** Next: bulk build and
flash, then central bench **B-1** (list: [bench-b1-20261115.md](bench-b1-20261115.md)), fixes and release **F-1**. Deadline: upstream wave 2
(Ping plus web GUI) on **2026-11-20**, which needs PING-NRF-01 and the browser rows from B-1.

## State 2026-11-15

- **I-2 done 2026-11-15:** B-1 list with the triage of older campaign docs, wave-2 PR content list
  ([upstream-wave2-content-20261115.md](upstream-wave2-content-20261115.md), operator questions open), PWR-01 and SENS-04
  to the field.
- **Branch `fork-dev`**, wave I-1 committed as `b6d0da94` (CET-AGE-LOG-01, TRACK-PING-01, AU-26, TD-NAV-01 option 2,
  web brightness alignment, web assets README). Gate stage 1,2 PASS (3812 Unity cases), six board builds green.
- **Last fork release `v4.40a.11.08`** (Latest, tag on `bc05ea88`, 68 CI assets checked, web flasher and Pages on `.11.08`).
- **Upstream:** PR #1277 (wave 1: clock, T-Deck, V4 GNSS/DHT22/T-Deck Pro field parts) merged 2026-11-14 unchanged and
  tagged **`v4.40a.11.14`** by upstream; synced into `fork-dev` 2026-11-15 (`d4384eac`, all conflicts on the fork side).
  No PR of ours is open; no `pr/*` branch or extra worktree exists. The next fork tag must not be `v4.40a.11.14`.
- **Operator decisions 2026-11-15** (wave 0): listed in the campaign doc. In short: DEDUP-LAP-01 won't fix; ACK-AGE-01
  exit = "clearly better than SOAK-1108"; web GUI goes upstream as is with the generator and a README, together with
  Ping on 2026-11-20; TD-NAV-01 option 2.

## Fleet

- **DK5EN-98** (production gateway): fork-dev build `aa8e1b45` since 2026-11-14 21:44 (ACK-AGE-01). `--setlog on`
  stays until the ACK-AGE-01 run under load, then off (OK given 2026-11-15). Any other work on it needs an explicit OK.
  ACK-AGE-01 night run evaluated: [ackage-auswertung-20261115.md](ackage-auswertung-20261115.md) (17 relay ACKs, median
  3.3 s, max 13.6 s vs SOAK-1108 6.1 / 24.8 s; quiet night, load run still owed).
- **DK5EN-1, -14, -90, -92** still run the PR #1277 build; the bulk flash before B-1 replaces it.

## Running campaigns

- **Release train 2026-11-15:** [campaign-release-train-20261115.md](campaign-release-train-20261115.md) -- the active one.
- **Channel access stabilisation** (ACK-AGE-01, ACK-CANCEL-01, CSMA-04/02, CSMA-03, PWR-01/02, soak): dates and exit
  criteria in [release-plan-upstream-20261114.md](release-plan-upstream-20261114.md).
- **Upstream waves 2-7:** [release-plan-upstream-20261114.md](release-plan-upstream-20261114.md).
- **TD-THEME-01:** W4 bench on DK5EN-14 is part of B-1. [campaign-tdeck-theme.md](campaign-tdeck-theme.md).
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

- Open work: [BACKLOG.md](BACKLOG.md) 3.2. Closed rows since 2026-11-15:
  [archive/backlog-closed-20261115.md](archive/backlog-closed-20261115.md); full row history:
  [archive/backlog-rows-20261107.md](archive/backlog-rows-20261107.md), older
  [archive/backlog-closed-20261024.md](archive/backlog-closed-20261024.md).
- Fork vs upstream feature list: [features-vs-upstream-20261114.md](features-vs-upstream-20261114.md).
- Defect IDs: [architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md). Tests:
  [test-suite-map.md](test-suite-map.md).
- Releases: [release-journal.md](release-journal.md), [release-notes.md](../release-notes.md). Plan view:
  [campaign-gantt.html](campaign-gantt.html).
- Session journals that used to live here: [2026-11-07, incl. this page before the debloat](archive/resume-journal-20261107.md),
  [2026-10-23 .. 2026-10-24](archive/resume-journal-20261024.md),
  [2026-10-05 .. 2026-10-23](archive/resume-journal-20261005-20261023.md),
  [2026-09-03 .. 2026-10-01](archive/resume-journal-20260903-20261001.md).
