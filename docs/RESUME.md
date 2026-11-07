# RESUME -- pick up here

**Stand:** 2026-11-07, branch `fork-dev`, last release **`v4.40a.11.06`** (Latest, tag on `3ecf429e`, 68 assets, built by CI).
Base is upstream `dev` 649b5b17 (merged into fork-dev 2026-11-07 as `71c324e7`, tree unchanged): our
[#1243](https://github.com/icssw-org/MeshCom-Firmware/pull/1243), [#1244](https://github.com/icssw-org/MeshCom-Firmware/pull/1244),
[#1245](https://github.com/icssw-org/MeshCom-Firmware/pull/1245), [#1247](https://github.com/icssw-org/MeshCom-Firmware/pull/1247)
(LRF ping/beacon/RX-silence), [#1248](https://github.com/icssw-org/MeshCom-Firmware/pull/1248) (REVIEW.md) and
[#1249](https://github.com/icssw-org/MeshCom-Firmware/pull/1249) (CHARS-01 national characters, TD-25/TD-24, closes #1221),
[#1250](https://github.com/icssw-org/MeshCom-Firmware/pull/1250) (BAT-04 charge state and BLE `IS2`, closes #1073) and
[#1251](https://github.com/icssw-org/MeshCom-Firmware/pull/1251) (WX-01 UV/wind/gust plus the TLM-03 T# fix, closes #999)
plus sp2ong's #1246 (E290 OTP refresh default) are upstream; their branches and worktrees are removed. [#1257](https://github.com/icssw-org/MeshCom-Firmware/pull/1257) (`SNF-ECHO-01`) merged 2026-11-06 as `649b5b17`, synced into `fork-dev` 2026-11-07 (tree unchanged). Fork-only firmware left: the `dev` AU
channel, RM1 `upd chan`, `--gateway lab` (2fe7ebd4).
Upstream `dev` last synced 2026-11-07. `fork-dev` has no firmware change since the tag, so nothing is unreleased.
Next step: operator bench checks (campaign-open-items), TD-22 soak, TD-23 UAT.

This page is the current state only. Dated session entries live in the archive journals (see "Where things
are"); open work lives in [docs/BACKLOG.md](BACKLOG.md).

## Open now

- **`v4.40a.11.06` shipped 2026-11-07, fleet updated:** DK5EN-1, -14, -92 by Auto Update in Chrome, DK5EN-90 by USB DFU with the CI zip (Chrome's serial chooser could not be driven), DK5EN-98 last through its Safeboot page in Chrome, still on channel `prod`. Web flasher verified (31 boards, 0 mismatches). T-Beam idle heap on the release image passes (+136 B against 64016 B).

- **T-Beam Auto Update download, open (`AU-24`):** see `docs/BACKLOG.md`. Read this before the next release.

- **Open checks carried over from finished work.** Taken from the entries archived 2026-11-07 ([journal](archive/resume-journal-20261107.md)) and not re-tested since. Mostly checks still owed on shipped code; the QA line also holds unfixed findings:
  - `SNF-ECHO-01` ([#1257](https://github.com/icssw-org/MeshCom-Firmware/pull/1257)): field check of `refr=` on the affected store node.
  - Open-items campaign: operator bench checks in [campaign-open-items-20261102.md](campaign-open-items-20261102.md).
  - Bench fixes 2026-11-06: the checks that needed CI images (soak, Safeboot `/ota` page, manipulated `.autoupdate`, BLE, RAK secrets with a real phone) in [campaign-bench-fixes-20261106.md](campaign-bench-fixes-20261106.md).
  - LRF: own beacons at prio 2 and the boot-time ping-target clean-up are host-tested only.
  - CHARS-01 and TD-25/TD-24: no OLED, T-Deck Pro or T5 bench in the fork; E290 tested by sp2ong only ([campaign-chars01-e2e-20261102.md](campaign-chars01-e2e-20261102.md)).
  - QA review: bench list in section 8.6 (DK5EN-92/-90), app tolerance of `bt_code` -1 (WP4), QA-SIZE-06/09/12, QA-RX-04/05, QA-AU-06, GET `tempoffsetoutdoor` UNKNOWN bug (needs OK) ([report](review/qa-review-fork-dev-20261025.md)).
  - T-Deck heap/WiFi: new soak, GUI visual check on the device ([campaign-tdeck-heap-wifi-20261025.md](campaign-tdeck-heap-wifi-20261025.md)).
  - RM1 `upd chan`: McApp work order MCProxy B14 (`doc/2026-10-31_1000-rm1-updchan-requirements.md`, commit `786099a`).

- **Dev update channel is a build flag since `6c84a46c`** (after the release): `MC_AU_DEV_CHANNEL=1` in fork CI and every
  local fork build; upstream builds know prod only.
- **Campaign resume points with open steps:**
  - BAT-04 (upstream #1073) and WX-01 + TLM-03 (upstream #999): implemented and benched 2026-11-02, merged upstream
    as #1250 and #1251, released. Open: WX-01
    field test with a real LTR390 and anemometer; AXP192 current path host-tested only. Resume point:
    [campaign-bat04-wx01-cloud-20261102.md](campaign-bat04-wx01-cloud-20261102.md).
  - Log-report fixes (LRF): W0-W2 done 2026-11-01 (ping target locked, own beacons at prio 2, RX-silence logging on
    nRF52 and ESP32 with end-reason ages; benched on DK5EN-90 (RAK) and DK5EN-1 (Heltec V3, W3), released in `v4.40a.11.01`).
    Next: field capture
    ([campaign-logreport-fixes-20261101.md](campaign-logreport-fixes-20261101.md)).
  - TX pool (TXP): released, 93-h DK5EN-98 soak passed (0 drops, 0 TTL evictions, peak 5/20). Open: operator decision on the TTL values ([campaign-tx-pool.md](campaign-tx-pool.md)).
  - LoRa QRT: DK5EN-98 night capture, release decision ([campaign-lora-qrt-20261018.md](campaign-lora-qrt-20261018.md), Status).
  - Web flasher WF-05: CI marker check, Windows run ([campaign-webflasher-autodetect-20261019.md](campaign-webflasher-autodetect-20261019.md)).
  - Fork hardening: visual LED confirmation, phone-app SN check, deep-sleep LED test, LED pins on T-Echo, T114,
    T-ETH-Elite, DevKitC in the field ([campaign-fork-hardening-20261018.md](campaign-fork-hardening-20261018.md)).
  - DUP/OnRxDone RCA: DK5EN-98 byte-model nights; GWRX-01 count-mode night on DK5EN-98 (operator OK) ([rca-brief-dup-onrxdone-20261013.md](rca-brief-dup-onrxdone-20261013.md)).
  - RM extended commands: RM-DUP on hardware, gwbench driver gaps, page check
    ([rm-gui/extended-commands-impl-plan.md](rm-gui/extended-commands-impl-plan.md) sections 6, 9, 10).

## Hottest first (details in BACKLOG)

- AU-14: single TASK_WDT during an AU download, cause unknown.
- TRK-02/03: "TRACK on after web flash" (Heltec V3) not reproduced, the flasher cannot click GPIO0 while the
  app runs; TRK-03 found on the way: T-Beam first boot set TRACK instead of GPS since V4.21, fixed
  (`TBEAM_FIRST_BOOT_SSET`), released in v4.40a.10.24, upstream PR candidate
  ([campaign-trk02-webflash-track-20261024.md](campaign-trk02-webflash-track-20261024.md)).
- AU-21: Wireless Tracker AU check failed (`tls -9984`, heap at 0): TFT sprite 25.6 KB fixed on fork-dev (`e6e74bc6`,
  `c100451a`), released in v4.40a.10.24; open: S3 TLS heap floor and BLE pause after a DK5EN-1 measurement
  ([campaign-au21-tft-heap-20261024.md](campaign-au21-tft-heap-20261024.md)).
- RM-NVS-01: E22 field report, NVS-full hypothesis; waiting for the reporter's `--info` and log.
- GWRX-01 count-mode night on DK5EN-98 (operator OK) before relying on `--nbrrelay on` in the field.
- GWACK-01: fixed on fork-dev 2026-10-24 (`c462d018`, option C: both ACK copies with one id), benched 28/28; released in v4.40a.10.25, upstream with #1238 (`campaign-gwack01-20261024.md`).
- BLK-01 call blocklist `--blockcall`: on fork-dev 2026-10-24 (`ecf00df0`, `1635ba56`, `986aa573`), benched
  DK5EN-1/-90/-92, DK5EN-98 runs the build (list empty). Released, so the third-party
  DM3KS build (sku2atsc/MeshCom-Firmware, DB0SD area) can retire. DK5EN-14 flashed 10-24 16:09. Open: fork-key
  migration not benched ([campaign-blockcall.md](campaign-blockcall.md)).
- Backlog sweep 2026-11-02 ([campaign-backlog-sweep-20261102.md](campaign-backlog-sweep-20261102.md), released): GPS-05b closed
  (tau stays 30 min), BL-05 accepted risk; fixed host-only TD-31, BL-02, PF-T1, MCC-T1, WEB-DARK-02, TZ-02, SENS-03, TD-32;
  CSMA-04 instrumentation built (INSTRUMENT only). Bench checks listed in the campaign doc. New: SENS-04.
- Stability: REBOOT-98 (single event, watch); BL-04 closed with WEB-WDT-01.
- Gateway server table: FR/US rows when operators name servers; stale `test/golden/corpus/commands`.

## Branch model

- `fork-dev` is the only working branch: `upstream/dev` (icssw-org/MeshCom-Firmware) plus the fork's docs,
  tests, tools, GitHub Pages **and fork-only firmware** (`src/`, `platformio.ini`, `variants/`) that is not
  upstream yet. New firmware lands on `fork-dev` first; upstream PRs are cut from `upstream/dev` later.
- Retired 2026-10-02: `fork-main`, `fork-neo`, `fork-neo-test`, `master` (tag
  `archive/fork-neo-test-20261002`). Nothing is developed there any more.
- Upstream sync: cherry-pick, never merge the fork history in. After icssw-org merges a PR of ours,
  **merge** `upstream/dev` (not rebase) to re-sync.
- All PRs target upstream `dev`; PR text is German and lists files, functions and the why.
  Minimal, targeted changes only. DK5EN never merges upstream himself (the maintainers review and merge).
- Merged upstream PRs of ours since 4.40a: #1200, #1201, #1207, #1208, #1213, #1214, #1224, #1233, #1235, #1238, #1243, #1244, #1245, #1247, #1248, #1249, #1250, #1251, #1257.
- Nothing temporary remains in `src/` (`389ace0d` reverted 2026-10-15).

## Last release and flasher policy

- `v4.40a.11.06` (tag on `3ecf429e`, CI-built, run 42306803386), 68 assets, Latest.
- Upstream (icssw-org), checked 2026-11-07: Latest is `v4.40a.10.28` (published 2026-10-28, tag on `10203967`, the #1240
  merge), 68 assets including 28 `.autoupdate` (zlib, `78 da`, sampled on two), so upstream CI builds them, first in this
  release. `upstream/dev` is at `649b5b17`, 35 commits after that tag (#1243 to #1257), none released upstream yet.
  `fork-dev` is 0 commits behind `upstream/dev`.
- CI builds and publishes since `v4.40a.10.11`: `MeshCom CI` (`.github/workflows/meshcom-ci.yml`) fires on
  `v*` tags (release, Latest) and `ci-proof-*` tags (draft; delete draft and tag afterwards). Recipe:
  `/release-firmware` (includes the T-Beam idle-heap step against `tools/heap_baseline.json`).
- The web flasher on gh-pages offers only the latest release (`--keep 1`); root `safeboot*.bin` are the
  release assets. Releases up to 10.20 can no longer be re-staged by `pages_flasher.py` (WF-05).
- Never cut a same-day `.N` release (the AU tag parser rejects it): date it the next day.
- Release history: [docs/release-journal.md](release-journal.md); user-facing delta:
  [release-notes.md](../release-notes.md).

## How to verify the tree

- Host: `tools/regression.sh --stage 1,2`. With the bench fleet attached: `--stage all`. The
  `/full-regression` skill wraps it.
- Stage 1: every `[env:native*]` (88 envs, 3324 Unity cases on 2026-11-02) plus `test/golden/selftest.sh`
  and the nRF52 board link checks.
- Stage 2: ruff lint gate, pytest over `tools/bench`, `tools/tests`, `tools/mock` (1473 cases on 2026-11-01),
  the `test/test_nbrlog` scripts, node tests, the jsdom page tests and the `--self-test` tools.
- Stage 3: `tools/bench/bench_suite.py` (nodes matched by USB serial, identity guard as the gate,
  `prepare <node>`, per-board harness, OTA with its own image build, web GUI badge, mesh exchange;
  `--flash`, `--extudp`, `--deepsleep` are opt-in, `--instrument` is the default). A bench run flips the
  PlatformIO checksum (`PLATFORMIO_BUILD_FLAGS`), so the next stage 1 builds cold.
- **Never run bare `pio test`**: it walks the board envs and flashes attached hardware.
- **One `pio` process at a time**: shared build cache; `selftest.sh` itself calls `pio project config`.
- Inventory of every suite: [docs/test-suite-map.md](test-suite-map.md). Runner details:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Not verified until lint, typecheck, format-check and the real suite have run.

## Bench fleet

Registry: `tools/bench/fleet.json`. Addresses come from DHCP and move between sessions: resolve the port by
USB serial and confirm the callsign before any flash or test. Last known firmware per node:

| Node     | Board           | USB serial              | Last known firmware (date)                                      |
| -------- | --------------- | ----------------------- | --------------------------------------------------------------- |
| DK5EN-90 | RAK4631 (nRF52) | 230D6EBB3266D20E        | normal `fork-dev` `a7b21de5` (10-24); ETH .75, no 2323, no mDNS |
| DK5EN-14 | T-Deck Plus     | E0:72:A1:AD:65:E0 (MAC) | fork-dev build + Safeboot v3 (10-19); .66 (10-24)               |
| DK5EN-92 | T-Beam v1.2     | 573C000584              | normal `fork-dev` `a7b21de5` (10-24); .71                       |
| DK5EN-1  | Heltec V3       | 0001                    | normal `fork-dev` `a7b21de5` (10-24); .73, net console on       |

DK5EN-1, -90 and -92 run gateway on (real server), `--mesh off`, `--nbrrelay on`, 2 dBm.

DK5EN-98 is the production Heltec V3 (gateway, logger on rpizero), not a bench node; last known on its
production build of `1a6a3704` (QRT waves). Any work on it needs an explicit OK; it is flashed over the air
with `tools/webflash.py` (allow more than its 120 s wait for the node to return).

Handling:

- Confirm the callsign on the start page or via `identity_guard.py --port` before any flash or test.
  Resolve ports by USB serial (ioreg or pyserial), never by name. Console logins go through
  `tools/mc_console.py` (password `MC_PW_<CALL>` from the gitignored `.env`).
- Harness `--node` takes the fleet key (`t-deck-14`), not the callsign.
- Heltec V3 on CP2102: DTR is the PRG button, a held DTR is a long press (deep sleep). The guard
  asserts DTR only on native-USB ports.
- Opening a port reboots every ESP32. The RAK needs DTR. The T-Beam flashes at 460800 (921600 fails).
- The RAK web GUI has no mDNS (the Ethernet path ships no responder): reach it by IP from the
  boot log (`Ethernet.localIP():`).
- rpizero NATs only Hamnet: Internet cases on a gateway behind it fail by design. Never capture on it.

Hard bench rules:

- Test traffic only to group `TEST` or as a direct message to an own node. Never broadcast.
- Never a foreign callsign (never OE1XAR, never XX0XXX).
- Bench TX power at most 2 dBm.
- Run `tools/bench/identity_guard.py` before any test.
- No USB port open while `serial_capture.py` holds it (check `lsof`).
- `pio run -t upload` needs an explicit `--upload-port`; autodetect can land on the T-Deck.

## Where things are

- Open work: [docs/BACKLOG.md](BACKLOG.md) is the single list. Do not copy items here. Closed rows:
  [archive/backlog-closed-20261024.md](archive/backlog-closed-20261024.md).
- Defect IDs: [docs/architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md).
- Tests: [docs/test-suite-map.md](test-suite-map.md); runner:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Releases: [docs/release-journal.md](release-journal.md), [release-notes.md](../release-notes.md).
- History: [docs/archive/](archive/README.md). Session journals that used to live in this file:
  [finished entries moved out 2026-11-07](archive/resume-journal-20261107.md),
  [2026-10-23 .. 2026-10-24](archive/resume-journal-20261024.md),
  [2026-10-05 .. 2026-10-23](archive/resume-journal-20261005-20261023.md),
  [2026-09-03 .. 2026-10-01](archive/resume-journal-20260903-20261001.md).

## Last three sessions

- **2026-11-04:** Ideas until end of 2027 written up for discussion with OE1KBC and OE1KFR, not decided and not sequenced: [ROADMAP.md](ROADMAP.md),
  [hardware principles](hardware-guiding-principles.md), [market analysis](market-analysis-meshtastic-meshcore-20261104.md). Backlog rows IDEE-01..20; DEF-GWLINK closed.
- **2026-11-03:** Upstream check: `upstream/dev` (`4f60051c`) fully merged, no PR of ours open. Docs cleanup
  (obsolete `389ace0d` note dropped). Backlog sweep W0/W1 (`7e99ad40`, `2c1108e2`) is on `fork-dev`, not released.
  Branch clean-up (`origin/backup/fork-dev-code-review-*`, `origin/claude/fork-dev-code-review-*`) pending.
- **2026-11-02:** CHARS-01 waves A and B (national characters on E290 and T-Deck), see Open now. AU web GUI
  button reads "Download" (`c4689942`).
