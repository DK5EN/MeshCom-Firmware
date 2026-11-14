# RESUME -- pick up here

**Stand:** 2026-11-08 evening, branch `fork-dev`. Release **`v4.40a.11.08`** published (Latest, tag on `bc05ea88`,
68 assets built by CI, checked; web flasher and Pages on `.11.08`, `.11.06` pruned). It contains everything since `v4.40a.11.06`, incl. the official `v4.40a.11.07`
merge (`da76033b`), GPIO-01..05, GPSV4-01 (hardware-untested), TD-THEME-01, TD-CHAT-01, and the BACKLOG-ROUND fixes
(CLK-01, CET-AGE-01, ping display, nRF52 ping budget, CSMA-03, web setup page). No PR of ours is open.

**Fleet on `v4.40a.11.08` CI images since 2026-11-08 20:37** (incl. DK5EN-98). **SOAK-1108 done** (2026-11-09 20:00, evaluated 2026-11-14): clean run, two findings in the BACKLOG as high priority, ACK-AGE-01 and DEDUP-OWN-01 ([soak-1108-auswertung-20261114.md](soak-1108-auswertung-20261114.md)). `--setlog on` is still set on DK5EN-98 (needs OK to switch off).

**DK5EN-98 runs a fork-dev build since 2026-11-14 21:44** (`aa8e1b45`, not a CI image): ACK frames reach the CSMA age stage 1 after 6 s (ACK-AGE-01). Log run on rpizero until 2026-11-15 12:00, screen `ackage`, `~/meshlog/dk5en-98-ackage-20261114/`; next (2026-11-15 after 12:00): evaluate the run, compare ACK waits (`CSMA_AGED ... ack=1`, `TX` wait) with SOAK-1108 (median 6.1 s, max 24.8 s). The follow-up commits `d0ecf82f`/`5f9486a7` are not on DK5EN-98 (stats counter and T-Beam only). Also on fork-dev, not benched: T-Beam 3 s press = deep sleep (TBEAM-SLEEP-01).

**Upstream sync 2026-11-14:** merged `upstream/dev` 520394fc (PRs #1274, #1275: BOARD_COUNTRY only as default, spectral scan 500 MHz split, Info page label `Gateway:`); gate stage 1,2 green, builds `wiscore_rak4631` and `heltec_wifi_lora_32_V3` green. PR feature list: [features-vs-upstream-20261114.md](features-vs-upstream-20261114.md).

**Upstream PR #1277 open (2026-11-14):** wave 1 of [release-plan-upstream-20261114.md](release-plan-upstream-20261114.md), branch `pr/uhr-tdeck` (worktree `../MeshCom-PR-next`, head `5616c8a1`, 20 commits): clock every second (CLK-01, CLK-PING-01), CET-AGE-01, new: GPS-without-fix no longer overrides NTP and the shown time never runs back up to 10 s, T-Deck theme/chat/setup tab/GPS page (TD-29) and WLAN/BLE icon states, GPS sky plot, -fno-exceptions, LVGL trim; field parts Heltec V4 GNSS, DHT22, T-Deck Pro pins. Gate: 35/36 board envs (external-radio needs its overlay, as on dev), PR-scope host suites green. Bench 2026-11-14 on DK5EN-1/-14/-90/-92; they run the PR build overnight, then back to `v4.40a.11.08`. The bench fixes of the PR are back on `fork-dev` (`f515aaa1`: GPS/NTP order, display hold, WLAN/BLE icon rule with the TD-30 strike-through; the fork icon variant is not benched yet). After the merge: `/sync-upstream`, the PR paths then match fork-dev except the fork-only TD-30 icon switch. Next week: Safeboot wave (BOOT-01 "MeshCom 4.4" on the display) and Ping.

This page is the current state only. Open work lives in [BACKLOG.md](BACKLOG.md) section 3.2; history in the
archive journals (see "Where things are").

## Next: the release train

Defined in [BACKLOG.md](BACKLOG.md) 3.2 "Release train": implement the `impl` rows (running, see IMPL-TRAIN below), one bulk build, bulk flash
of DK5EN-1, -14, -90, -92, one bulk bench session (28 `bench` rows plus the checks below), docs, then commit, push
and `/release-firmware`. Soak on DK5EN-98, the GPSV4-01 upstream PR and field checks follow after the release.

## Running campaigns

- **IMPL-TRAIN:** release-train implementation, pure coding, done 2026-11-08: W1 `d1ec388e`, W2 `2ade9625`, W3
  `244cd0c6`, W4 `84eed6d5`. Next: INSTRUMENT flash of DK5EN-1/-14/-92 for the CLK-01 before-measurement and the
  T-Deck theme bench. Bench 2026-11-08: INSTRUMENT flash done; CLK-01 S1 and S5 measured (campaign-clk-01), S2-S4 open.
  [campaign-impl-train-20261107.md](campaign-impl-train-20261107.md).

- **BACKLOG-ROUND:** waves 1-4 and the bulk bench done 2026-11-08; reduced-scope release `v4.40a.11.08` (operator).
  Next: release verification and web flasher, then the moved-out backlog rows.
  [campaign-backlog-round-20261108.md](campaign-backlog-round-20261108.md) -- resume here.

- **TD-THEME-01:** W1-W3 done (last `f4c944e1`); W4 is the bench pass on DK5EN-14 (screenshots of both themes,
  lock glyph against the bt/wifi icons, boot logo tab). [campaign-tdeck-theme.md](campaign-tdeck-theme.md).
- **GPSV4-01:** W1 on `fork-dev`; next the upstream PR, marked hardware-untested.
  [campaign-gpsv4-01-20261107.md](campaign-gpsv4-01-20261107.md).
- **GPIO-01:** done, no bench test by decision; GPIO-06 (T-Deck Pro pin) is an `impl` row, GPIO-07 a field test.
  [campaign-gpio-01-20261107.md](campaign-gpio-01-20261107.md).
- **INA226-HELTEC-01:** INA226 enabled on Heltec V3, Wireless Stick and Wireless Tracker (`ca8da6f3`); upstream PR
  [#1272](https://github.com/icssw-org/MeshCom-Firmware/pull/1272), build only, field test by users.
  Merged upstream and synced 2026-11-13 (merge of `upstream/dev`, plus an upstream whitespace-only reindent in
  `sendMessage`); `variant-macros.txt` golden regenerated for the three removed `ENABLE_INA226_DISABLED` macros.

- **CLK-01:** fixed and benched 2026-11-08 (F1-F3, A3 via CET-AGE-01, A4 3.8 %); released in `v4.40a.11.08`. Next: W4
  upstream PR.
  [campaign-clk-01-20261107.md](campaign-clk-01-20261107.md).

## Bench checks owed from campaign docs (add to the bulk bench)

Not all of these have a backlog row; each campaign doc lists its own steps.

- 2026-11-14: T-Beam 3 s press = deep sleep and wake by button on DK5EN-92 (TBEAM-SLEEP-01, BACKLOG); ACK-AGE-01 on a nRF52 node after the DK5EN-98 log evaluation.
- Open items: operator bench checks, [campaign-open-items-20261102.md](campaign-open-items-20261102.md).
- Bench fixes 2026-11-06: Safeboot `/ota` page, manipulated `.autoupdate`, BLE, RAK secrets with a real phone,
  [campaign-bench-fixes-20261106.md](campaign-bench-fixes-20261106.md).
- QA review: bench list in section 8.6 (DK5EN-92/-90), [qa-review-fork-dev-20261025.md](review/qa-review-fork-dev-20261025.md).
- T-Deck heap/WiFi: new soak and GUI check, [campaign-tdeck-heap-wifi-20261025.md](campaign-tdeck-heap-wifi-20261025.md).
- Fork hardening: LED and deep-sleep checks, [campaign-fork-hardening-20261018.md](campaign-fork-hardening-20261018.md).
- RM extended commands: RM-DUP on hardware, [extended-commands-impl-plan.md](rm-gui/extended-commands-impl-plan.md) sections 6, 9, 10.
- LRF: own beacons at prio 2 and the ping-target clean-up are host-tested only,
  [campaign-logreport-fixes-20261101.md](campaign-logreport-fixes-20261101.md).

Overnight or production (DK5EN-98, explicit OK): TX pool TTL decision ([campaign-tx-pool.md](campaign-tx-pool.md)),
LoRa QRT night capture ([campaign-lora-qrt-20261018.md](campaign-lora-qrt-20261018.md)), DUP/OnRxDone and GWRX-01
nights ([rca-brief-dup-onrxdone-20261013.md](rca-brief-dup-onrxdone-20261013.md)).

## Rules and recipes

- Branch model, gates, toolchain traps, bench rules: [CLAUDE.md](../CLAUDE.md). Bench fleet, ports, flashing: the
  local `/bench` skill and `tools/bench/fleet.json`.
- Every local fork build: `export MC_AU_DEV_CHANNEL=1` (dev update channel is a build flag since `6c84a46c`).
- Releases: `/release-firmware`; never a same-day `.N` release (the AU tag parser rejects it). Read `AU-24` (T-Beam
  Auto Update download) in the backlog before the next release.

## Where things are

- Open work: [BACKLOG.md](BACKLOG.md) 3.2. Full row history and closed rows:
  [archive/backlog-rows-20261107.md](archive/backlog-rows-20261107.md), older
  [archive/backlog-closed-20261024.md](archive/backlog-closed-20261024.md).
- Defect IDs: [architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md). Tests:
  [test-suite-map.md](test-suite-map.md).
- Releases: [release-journal.md](release-journal.md), [release-notes.md](../release-notes.md). Plan view:
  [campaign-gantt.html](campaign-gantt.html).
- Session journals that used to live here: [2026-11-07, incl. this page before the debloat](archive/resume-journal-20261107.md),
  [2026-10-23 .. 2026-10-24](archive/resume-journal-20261024.md),
  [2026-10-05 .. 2026-10-23](archive/resume-journal-20261005-20261023.md),
  [2026-09-03 .. 2026-10-01](archive/resume-journal-20260903-20261001.md).
