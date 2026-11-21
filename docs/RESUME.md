# RESUME -- pick up here

**Resume here: section "State 2026-11-21" below (fork release `v4.40a.11.21` out, Latest; upstream PR #1285 open:
REVIEW.md November additions); campaign [campaign-release-train-20261115.md](campaign-release-train-20261115.md).**

## State 2026-11-21

- **Release `v4.40a.11.21`** (Latest since 2026-11-21 11:47 UTC), tag on `96aaa67c`, cut from `fork-dev` once more by
  operator decision (NIGHTLY-01 still planned). Content: AU-27, W/S hemisphere (#1284), APRS-IS weather (#1269) with
  the fork fix `temp(aprswx)`. CI run `44895542343` green; 69 assets, names as 11.20, 28 `.autoupdate` with `78 da`,
  28 of 28 re-inflate byte-exact, tag string in the Heltec V3 image, T-Deck field commands `mute=2 stat=1 udplog=1
injectraw=0`. Gate PASS 3900 Unity cases in 111 envs. Idle heap on DK5EN-92 waived by the operator.
- **Web flasher** offers only `v4.40a.11.21` (31 boards, `gh-pages` `94449670`, 11.20 pruned); root `safeboot*.bin` =
  release assets. Pages: index release card 11.21, PR history round 11 (#1284, 77 PRs), fork release table row 11.21.
- **Upstream PR [icssw-org #1285](https://github.com/icssw-org/MeshCom-Firmware/pull/1285)** open since 2026-11-21:
  `REVIEW.md` November additions (F-13 downgrade, H-17 command prefix, R1 two representations, R2 release files, R3
  web across browsers and on the RAK, F-06 to F-09, T114), branch `pr/review-november`, worktree `../MeshCom-PR-next`.
  After the merge: `/sync-upstream`, then remove worktree and branch.
- **Reply to DK1TCP** drafted (Desktop `antwort-pit-20261121.html`); open: his answer A (keep 60 px nav buttons) or B
  (shrink).
- **AU-27 fixed on fork-dev (field report DK1TCP, Heltec V3):** after an AU install of `v4.40a.11.20` and an OTA-flasher
  downgrade to `v4.40a.11.08`, Auto Update said "current" and never offered 11.20 again. Cause: the reinstall guard
  (NVS `fwstage/last`) blocked the tag even on a dated running image. Now `fwShouldInstall()` applies it only while the
  running image is undated (`src/fw_update.h`). Pin: `test_should_install_after_downgrade_dated_running` (fails on the
  old code). Gate PASS 3900 Unity cases; Opus advisor approved (no unattended reflash path: `AU_AUTO_INSTALL` is fixed
  0). Not benched: no downgrade repro on a node. Not in any release yet.
- **Same report, WEB-NAV-01 side effect:** on Linux with classic scrollbars (Vivaldi/Ubuntu) the nav buttons grew from
  about 45 to 60 px with 11.20, because the hidden scrollbar no longer eats 15 px. 60 px is the design size. Open:
  keep, or shrink the buttons; DK1TCP asked (A keep / B shrink).
- **Fleet:** DK5EN-98 runs `v4.40a.11.20` since 2026-11-21 10:50 (Auto Update from the web UI, operator OK), gateway
  connected after the reboot.

- **Upstream PR [icssw-org #1284](https://github.com/icssw-org/MeshCom-Firmware/pull/1284)** open since 2026-11-21,
  branch `pr/track-hemisphere` (worktree `../MeshCom-PR-next`, base `upstream/dev` `015beb78`), 4 commits, 11 files
  +49/-27, `Closes #1279`, `Closes #1278`. Fork side: `99186bd2`.
  - **#1279 (W9GIL):** TRACK turn beacon sent E for W and N for S. `WZ_GPS_Loop` and T-Deck Pro `tdeck_get_gps` fed
    magnitudes to `setSMartBeaconing()`; now signed. TRACK OLED line prints `fabs()`.
  - **Found on the way (operator OK, option A twice):** nRF52 RAK GPS stored signed values into `node_lat/lon` (now
    `cround4abs`); 8 own-position readers (display course/distance, `--mheard`, web, app DIST, neighbour row 0) and 5
    T-Deck map fallbacks used magnitudes against signed coordinates (now `nodeSignedLat/Lon`).
  - **#1278:** `compile_commands.json` in upstream `.gitignore` (fork already had it).
  - Gate: fork-dev 3898 Unity, 1605 pytest, 9 board envs; PR tree 9 envs + upstream native 291/291. Pins:
    `tools/tests/test_smartbeacon_signed_sites.py` (8, all fail on the old src). Opus advisor approved after two rework
    rounds. Not benched: no node west/south, no on-air TRACK check.
  - **Left out, follow-ups:** negative `node_lat` writers (T-Deck position entry `event_functions.cpp`, `config_json.h`
    import); lowercase hemisphere from T-Deck/T-Deck Pro setup; dead map-centre fallback `esp32_main.cpp` ~4966.
- **Merged upstream 2026-11-21 09:31 UTC** (`cd89e6e0`), synced into fork-dev as `0110fd3c`: no-op merge, the merged
  tree equals the pre-merge tree (fork-dev carried the whole PR in `99186bd2` and `b5ac3476`).
- **Open:** remove worktree `../MeshCom-PR-next` and branch `pr/track-hemisphere` (local and origin).

## State 2026-11-20

- **Release `v4.40a.11.20`** (Latest since 2026-11-20 21:36 UTC), tag on `1156953a`, the last release cut straight from
  `fork-dev` (operator 2026-11-20); later releases come from `fork-nightly` (NIGHTLY-01). 69 assets: upstream added the
  4 MB T3-S3 bootloader `bootloader-s3-4.bin` on 2026-11-08, CI made a bogus `.autoupdate` twin of it and the verify
  step left the release a draft; twin deleted by hand, `verify_release_assets.py --pairs 28 --total 69` ok, published
  with the same PATCH as the CI publish step. CI fixed for the next release in `1db054d9`. Gate PASS 3871 Unity cases.
  Idle-heap check on DK5EN-92 waived by the operator (T-Beam unreachable), stated in the notes.
- **Web flasher** offers only `v4.40a.11.20` (31 boards, `gh-pages` `8917823c`, 11.08 pruned). Root `safeboot*.bin`
  = published CI build of the tag (Safeboot changed with SB-06-DESC, `afaa2f40`). The first flasher push `e1e6d5f7`
  carried the 11.08 Safeboot because `publish` takes it from the repo root; `check` caught it (28 mismatches),
  re-published with the CI Safeboot. Order for next time: root safeboot = CI asset first, then `publish`.
- **Pages:** PR history at 76 merged PRs in ten rounds (round 10: #1272, #1277, #1280, #1281), index release card
  11.20.
- **RAM-WSP-01** (`938f0031`): `wsParams` in flash, RAK -880 B, Heltec V3 -1,008 B; goes upstream with wave 4. Not yet
  run on a node.
- **Upstream sync 2026-11-21:** #1269 (APRS-IS weather over Wi-Fi, Mateusz-Krajewski, merged by Kurt 2026-11-20)
  merged into fork-dev as `6731af6c`; both conflicts kept both sides, merge diff = upstream PR diff. Fork test pins
  and the variant-ini golden followed. Known upstream issue: the `--aprswx on/off` rows fall through into the status
  rung (BLE gets no node settings); fork fix `632aa9b3` `temp(aprswx)`, revert when upstream fixes it. Reported on #1269
  (comment 2026-11-20, issuecomment-7053480377). Gate: 3898 Unity
  cases, selftest green, Heltec V3, T-Beam, T-Deck, RAK build. Not in `v4.40a.11.20`.
- **Fleet:** unchanged on `029eb2f5` (DK5EN-1, -90, -98); nobody runs the release image yet.
- **Open:** flash the bench fleet with the release (DK5EN-98 only with an explicit OK); remove worktrees and branches
  `../MeshCom-PR-web` / `pr/web-gui` and `../mc-web-trial` / `scratch/web-trial-20261116`; CET-RESTAMP-01 answers;
  TD-34 icon repaint; BACKLOG rows that lag the bench docs (SB-06, CSMA-01, TBEAM-SLEEP-01, TD-NAV-01 still say "not
  benched" or "bench pending"); wave 4 trial branch from 2026-11-30; NIGHTLY-01 parked.

## State 2026-11-19

- **Upstream wave 2 out (resume here):**
  - Ping: [icssw-org #1280](https://github.com/icssw-org/MeshCom-Firmware/pull/1280) **merged** 2026-11-19 14:12 UTC
    by oe1kbc as merge commit `4cfa2ac2` (two parents, our five commits unchanged). Synced into fork-dev 2026-11-19 by
    merge, all seven PR paths on the fork side, tree unchanged. Worktree `../MeshCom-PR-next` and branch `pr/ping-hold-budget` removed (local and origin).
  - Web: [icssw-org #1281](https://github.com/icssw-org/MeshCom-Firmware/pull/1281) **merged** 2026-11-19 23:03 UTC
    by oe1kbc as merge commit `45f66dc7` (our three commits unchanged: board LED steady, pin policy + GPIO control, web
    GUI). Synced into fork-dev 2026-11-20 by merge `ffb080a5`, all PR paths on the fork side, tree unchanged (the gate
    of `029eb2f5` covers it). Worktree `../MeshCom-PR-web` and branch `pr/web-gui` (local and origin) still exist.
    RAK RAM +3.6 KB of the PR explained 2026-11-20: two nRF52 static JSON buffers (2,712 B, deliberate) and the
    `wsParams` table in RAM instead of flash (880 B RAK, 1,000 B ESP32); section "RAM delta of #1281" in
    [upstream-wave2-content-20261115.md](upstream-wave2-content-20261115.md). `wsParams` moved to flash on fork-dev
    2026-11-20 (RAM-WSP-01, RAK -880 B, Heltec V3 -1,008 B, gate PASS 3871); goes upstream with wave 4.
  - Out of the web PR: RM1 commands (wave 4), AU install schedule (wave 6), bench screenshot endpoints,
    `NRF52_NO_ETH`. The PR's `mcrm.js` already has the wave-4 cards; upstream nodes refuse those commands.
- **Found in the web PR bench, fixed on fork-dev `029eb2f5`:** WEB-LOAD-01 (the RAK refuses parallel asset requests,
  page without mc.js; retry loader `mcRe`) and WEB-NAV-01 (nav bar scrollbar squeezes the icons). Gate PASS 3871
  cases. Rows in [archive/backlog-closed-20261119.md](archive/backlog-closed-20261119.md). Both fixes are in #1281.
- **Fleet now:** DK5EN-1 and DK5EN-90 run fork-dev `029eb2f5` (normal build, USB), DK5EN-98 runs `029eb2f5` since
  2026-11-19 15:35 CET (OTA, operator OK; was `aa8e1b45`), gateway connected after the flash. DK5EN-14 and -92
  unchanged.
- **Open:** remove worktree and branch of #1281; TD-34 icon repaint; CET-RESTAMP-01
  answers; `/release-firmware` (read AU-24 first); RAM-WSP-01 into the wave 4 PR.

## State 2026-11-16 late

- **T-Deck bench 21:47-21:53:** TD-34 `--disptest square 8` PASS on `72b2f9c6` (no reset, console kept); TD-LOCK-01
  PASS on DK5EN-14 (lock glyph in the keyboard button, light and dark). DK5EN-14 now runs the TD-LOCK-01 INSTRUMENT
  image (working tree on `d76ba2b0`), theme light, key lock off. TD-LOCK-01 row archived; TD-34 keeps only the icon
  repaint. Detail: section "T-Deck bench 2026-11-16 late" in [bench-b1-20261115.md](bench-b1-20261115.md).
- **Wave 2 trial build done (resume here for `/submit-pr` web):** scratch branch `scratch/web-trial-20261116`
  (worktree `../mc-web-trial`, local only) from `upstream/dev` with the fork's whole `src/web_functions/`. RAK4631,
  Heltec V3, T-Deck and T-Beam build and link. Flash is 29-35 KB smaller than `upstream/dev` on every env (gzip
  assets), and RAK RAM is +3.6 KB. Real dependency list, runtime-only gaps (`--board led steady`, `--gpio`,
  `--gpiocontrol`, pin load repair) and fenced items (AU schedule, `HAS_NRF_ETH` guard, OLED shot): section "Trial
  build 2026-11-16" in [upstream-wave2-content-20261115.md](upstream-wave2-content-20261115.md).

## State 2026-11-16 evening

- **F-2 bench 19:20-20:36 (resume here):** DK5EN-1, -90, -92 flashed over USB with the INSTRUMENT image of `dc08974f`
  (firmware `a91d0e1d`), identity guard ok, 2 dBm. All rechecks PASS, detail in
  [bench-b1-20261115.md](bench-b1-20261115.md) section "F-2 bench" and `tools/bench/runs/harness/f2-*-20261116.summary`:
  - RM-LOSTREPLY-01 on the local mock: the reply evicts the oldest queued ACK, sender `ok` after 15 s (was `noanswer`).
  - BLE scan DK5EN-90: F0A0 only in the advertising, no F0A0 GATT service.
  - WEB-GZ-01: Firefox cold cache on the RAK; ESP32 part confirmed by the operator.
  - CLK-01 S4 (DK5EN-1, CET only): one initial set, then 4 beacons held, clock within 1.71 s for 50 min. NTP hold
    check (DK5EN-92): 6 of 6 beacons `[CET];hold;why;rank;by;NTP`.
  - Five rows closed: [archive/backlog-closed-20261116.md](archive/backlog-closed-20261116.md).
- **Fleet now:** DK5EN-1, -90, -92 on `dc08974f` INSTRUMENT, settings as before the bench (DK5EN-1 and -92 gateway off,
  DK5EN-90 gateway on the real server, mesh off, GPS on, WLAN on). DK5EN-14 unchanged (`72b2f9c6` INSTRUMENT).
- **Operator decision 2026-11-16 21:15 (wave 2, option A):** the web PR goes out as the fork's web files plus the Pins
  page of former wave 3 (pins, GPIO control, board LED, `web_txbuf`), no hand trim. Parts without an upstream backend
  (remote management, auto update) stay out and follow with waves 4 and 6. Wave 3 (27.11.) is a buffer. This replaces
  decision 1 ("ship all JS"). First step: a one-hour trial build on a scratch branch from `upstream/dev` to get the real
  dependency list. Plan: operator's Desktop HTML, Fassung 6.
- **Done after the bench, not committed yet:** TD-LOCK-01 fix (lock glyph replaces the keyboard symbol inside the
  keyboard button; test `test_lock01_glyph_inside_keyboard_button`, gate PASS 3871 cases), concept
  [concept-cet-restamp-01-20261116.md](concept-cet-restamp-01-20261116.md) (three operator questions).
- **Open:** TD-34 icon repaint (cosmetic); operator answers CET-RESTAMP-01; `/release-firmware`; `/submit-pr` wave 2 by
  Fri 2026-11-20; NIGHTLY-01 parked. The operator's Desktop checklist is kept current with every result.

## State 2026-11-16

- **2026-11-16 end of session (resume here):** `fork-dev` pushed, tree clean. Commits today:
  - `c34a05ac` BLE F0A0 GATT service removed (UUID kept in advertising), concepts RM-LOSTREPLY-01 and CET-SCATTER-01,
    `clock_drift.py` records `[CET]`.
  - `b08c5ea5` CET-SCATTER-01 Part A (forward-only `{CET}` filter); `885fa3b3` Part B (rank NTP > GPS > phone > RTC >
    CET; NTP/GPS block CET until reboot; GPS time without fix blocks too).
  - `72b2f9c6` T-Deck SKY/TEXT side tabs (TD-SKYTAB-01); `a91d0e1d` RM-LOSTREPLY-01 (RM reply may evict the oldest
    queued ACK, never for KISS clients).
  - Docs: repo release plan removed (operator's Desktop HTML is the only plan), TD-THEME-01 W4 done, TD-LOCK-01 found,
    NIGHTLY-01 decided and conceived ([concept-nightly-01-20261116.md](concept-nightly-01-20261116.md)), **parked** by
    the operator.
  - Every firmware wave: gate stage 1,2 PASS (last 3870 Unity cases), board builds, Opus advisor (CET Part B and
    RM-LOSTREPLY-01 each needed one rework).
- **Operator decisions 2026-11-16:** wave 2 ships the full web GUI incl. wave-3/4 blocks (fail fast), keeps the CSMA-03
  loop poll, takes the `web_setup.cpp` rewrite, WEB-DUP-01 in the web PR, plan kept (Ping and Web PRs on 2026-11-20).
  CET rank and CET-RESTAMP-01 wanted. RM-LOSTREPLY-01 option D2. NIGHTLY-01: 7 + 8 answers in the concept; Safeboot v4
  is built but not enforced.
- **Fleet:** DK5EN-14 runs `72b2f9c6` INSTRUMENT (T-Deck screenshots over `/screen.rgb565`, console `--tab`,
  `--touch tap`); others unchanged.
- **Open (next session):**
  - Operator: TD-34.
  - Bench: RM-LOSTREPLY-01 recheck (mock setup), CLK-01 S4 with CET Part A+B plus a hold check on an NTP node, BLE scan
    on the RAK (no F0A0 service), WEB-GZ-01 second browser.
  - Fix: TD-LOCK-01. Concept: CET-RESTAMP-01.
  - `/release-firmware` (note: Safeboot only updates with a full install), `/submit-pr` wave 2 by Fri 2026-11-20.
  - NIGHTLY-01 waves W1-W8 when the operator un-parks it.

## State 2026-11-15

- **2026-11-15 late:** B-1 done except TD-34 (`--disptest square 8`, needs the T-Deck on USB) and
  BOOT-01 on the DK5EN-1 OLED (reboot after the night run). Results: [bench-b1-20261115.md](bench-b1-20261115.md).
  Fixes on fork-dev: F-1a `afaa2f40` (LAST-PONG-01, battery `--info` line, SB-06-DESC Safeboot), F-1b `b4ae98e4`
  (PING-FAIL-REPEAT-01). Mock-server rows done (RM-DUP-01 PASS, CSMA-01 run C 14/14), BLE-SET-01 and NRF-BLEDFU PASS,
  TBEAM-HEAP-01 no regression since 11.08. RM-LOSTREPLY-01 reproduced, needs a short concept before a fix.
- **Night clock run evaluated 2026-11-16:** node clock fix holds (`lost_ms` <= 9 ms, no growth); CET-AGE-01 PASS; CLK-01
  S3 PASS on DK5EN-1 with WLAN, but S3 on DK5EN-92 and S4 on DK5EN-1 without WLAN miss `|delta| <= 2 s`: LoRa `{CET}`
  beacons step the clock 2-6 s both ways. New BACKLOG row CET-SCATTER-01 (tool gap: `clock_drift.py` records no `[CET]`
  lines). Both nodes restored, identity guard ok. Detail: "Night clock run" in the bench doc.
- **Fleet now:** DK5EN-1 and DK5EN-92 `afaa2f40` INSTRUMENT (night run), DK5EN-90 `b4ae98e4` normal (via BLE DFU),
  DK5EN-14 `f64ea131` bulk image; all 2 dBm; DK5EN-92 gateway off (operator), DK5EN-1/-90 gateways on the real server.
- **Next steps:** TD-34 and BOOT-01 OLED; RM-LOSTREPLY-01 concept; empty F0A0 service cleanup (BLE-SET-01);
  operator answers to the five wave-2 questions (checklist "Welle 2"); `/release-firmware` (release note: Safeboot
  only updates with a full install); `/submit-pr` wave 2 (Ping and Web as two PRs) by Fri 2026-11-20.
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
  criteria in the campaign doc, section "Parallel tracks".
- **Upstream waves 2-7:** the release plan is the operator's Desktop HTML (Fassung 5, 2026-11-16), not a repo file; the
  repo copy was removed 2026-11-16 (operator: double work, always outdated). Wave list also in the campaign doc.
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
- Session journals that used to live here: [2026-11-07, incl. this page before the debloat](archive/resume-journal-20261107.md),
  [2026-10-23 .. 2026-10-24](archive/resume-journal-20261024.md),
  [2026-10-05 .. 2026-10-23](archive/resume-journal-20261005-20261023.md),
  [2026-09-03 .. 2026-10-01](archive/resume-journal-20260903-20261001.md).
