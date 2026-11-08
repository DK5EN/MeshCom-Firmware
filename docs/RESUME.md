# RESUME -- pick up here

**Stand:** 2026-11-08 evening, branch `fork-dev`. Release **`v4.40a.11.08`** published (Latest, tag on `bc05ea88`,
68 assets built by CI, checked; web flasher and Pages on `.11.08`, `.11.06` pruned). It contains everything since `v4.40a.11.06`, incl. the official `v4.40a.11.07`
merge (`da76033b`), GPIO-01..05, GPSV4-01 (hardware-untested), TD-THEME-01, TD-CHAT-01, and the BACKLOG-ROUND fixes
(CLK-01, CET-AGE-01, ping display, nRF52 ping budget, CSMA-03, web setup page). No PR of ours is open.

**Fleet on `v4.40a.11.08` CI images since 2026-11-08 20:37** (incl. DK5EN-98). **SOAK-1108 running** on DK5EN-98
until 2026-11-09 20:00 CET: screen `soak98` on rpizero, `~/meshlog/dk5en-98-soak-11.08-20261108/`; watch-list and
next step in [BACKLOG.md](BACKLOG.md) (soak table).

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

- **CLK-01:** fixed and benched 2026-11-08 (F1-F3, A3 via CET-AGE-01, A4 3.8 %); released in `v4.40a.11.08`. Next: W4
  upstream PR.
  [campaign-clk-01-20261107.md](campaign-clk-01-20261107.md).

## Bench checks owed from campaign docs (add to the bulk bench)

Not all of these have a backlog row; each campaign doc lists its own steps.

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
