# RESUME -- pick up here

**Stand:** 2026-10-23, branch `fork-dev` (in sync with `upstream/dev` 112e4e5b), last release `v4.40a.10.22`
(CI, Latest, published 2026-10-23 with the 10-22 date). No upstream PR of ours is open (#1235 merged 2026-10-23). Bench session
running: [campaign-bench-20261023.md](campaign-bench-20261023.md).

This page is the current state only. Dated session entries live in the archive journals (see "Where things
are"); open work lives in [docs/BACKLOG.md](BACKLOG.md).

## Open now

- **AU-19 fix (candidate `v4.40a.10.23`):** NimBLE host-task panic in the BLE pause of `--update check` on
  the classic ESP32; plan and waves in [campaign-bench-20261023.md](campaign-bench-20261023.md), section 6. The
  remaining #1235 hardware checks (Heltec V3, T-Beam) run against `fork-dev`; findings are fixed on `fork-dev`, no PR.
- **AU-20 (BLE pause heap leak):** root cause confirmed on DK5EN-92 and DK5EN-1 (orphaned NimBLE flow-control
  esp_timer per init, about 50 B per pause); fix route waits for the operator, see
  [campaign-au20-ble-pause-heap.md](campaign-au20-ble-pause-heap.md) section 7. Side result: the AU-19 patch
  survives 100 forced pauses on the S3.
- **Fork-only firmware still waiting for upstream:** Auto Update with Safeboot v3 and the board identity marker
  (one large PR, AU-13 included), RM1 remote management (#1189), SNF-GW/STOR (#1188, STOR needs the server
  operator's OK), LoRa QRT waves 1-2, production setting lock, WEB-STOP-01, the AU-tied heap levers, RM LED and
  `chgled` override. Matrix: [fork-dev-vs-upstream-20261018.md](fork-dev-vs-upstream-20261018.md).
- **Release `v4.40a.10.22`** (tag on `8a329702`, CI run 39954657798): UDP-03, upstream #1215/#1225/#1234. 66
  assets, 27 `.autoupdate` re-inflate ok, tag string and T-Deck field-command scan ok; idle heap DK5EN-92 69068 B
  (baseline 69332 B, pass); web flasher v4.40a.10.22 (30 boards, 0 mismatches), 10.21 folder pruned; presentation
  synced. Tagged with the 10-22 date so a same-day fix can be `v4.40a.10.23`. DK5EN-92 runs a local release build
  of the tag commit; the other nodes not yet flashed.
- **Bench session 2026-10-23** ([campaign-bench-20261023.md](campaign-bench-20261023.md)): AU-17/AU-14, #1235 on
  hardware, RM-NVS-01, BL-01, RX stall #1229 = WEB-RX-01 ([campaign-rx-stall-1229.md](campaign-rx-stall-1229.md),
  W0 + W1a done, W1b next).
- **Campaign resume points with open steps:**
  - LoRa QRT: DK5EN-98 night capture, release decision ([campaign-lora-qrt-20261018.md](campaign-lora-qrt-20261018.md), Status).
  - Web flasher WF-05: CI marker check, Windows run ([campaign-webflasher-autodetect-20261019.md](campaign-webflasher-autodetect-20261019.md)).
  - Fork hardening: visual LED confirmation, phone-app SN check, deep-sleep LED test, LED pins on T-Echo, T114,
    T-ETH-Elite, DevKitC in the field ([campaign-fork-hardening-20261018.md](campaign-fork-hardening-20261018.md)).
  - DUP/OnRxDone RCA: GWRX-01 decision, DK5EN-98 byte-model nights ([rca-brief-dup-onrxdone-20261013.md](rca-brief-dup-onrxdone-20261013.md)).
  - RM extended commands: RM-DUP on hardware, gwbench driver gaps, page check
    ([rm-gui/extended-commands-impl-plan.md](rm-gui/extended-commands-impl-plan.md) sections 6, 9, 10).

## Hottest first (details in BACKLOG)

- AU-19: NimBLE panic in the BLE pause of the update check (2 in ~35), every classic ESP32 checking daily.
- AU-17: T-Beam v1.2 misses the TLS heap gate on every check. AU-14: single TASK_WDT during an AU download.
- RM-NVS-01: E22 field report, NVS-full hypothesis; waiting for the reporter's `--info` and log.
- GWRX-01: gateway re-transmits server-injected frames after hearing a relay (operator decision open).
- `--mesh off` still transmits on two RF paths (BL-01): operator decision.
- GPS: altitude Kalman re-seed length (BL-03) and baro fusion tau (GPS-05b), measured, unchanged.
- Stability, single events: TASK_WDT on DK5EN-1 (BL-04), REBOOT-98.
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
- Merged upstream PRs of ours since 4.40a: #1200, #1201, #1207, #1208, #1213, #1214, #1224, #1233.
- Nothing temporary remains in `src/` (`389ace0d` reverted 2026-10-15).

## Last release and flasher policy

- `v4.40a.10.22` (tag on `8a329702`, CI-built), 66 assets, Latest. Base official `v4.40a.hbserver`. The previous
  `v4.40a.10.21` was built locally (CI package install failed).
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
- Stage 1: every `[env:native*]` (77 envs, 2741 Unity cases on 2026-10-23) plus `test/golden/selftest.sh`
  and the nRF52 board link checks.
- Stage 2: ruff lint gate, pytest over `tools/bench`, `tools/tests`, `tools/mock` (1190 cases on 2026-10-23),
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

| Node     | Board           | USB serial              | Last known firmware (date)                                |
| -------- | --------------- | ----------------------- | --------------------------------------------------------- |
| DK5EN-90 | RAK4631 (nRF52) | 230D6EBB3266D20E        | plain build of `34974374` (10-19); no 2323, no mDNS       |
| DK5EN-14 | T-Deck Plus     | E0:72:A1:AD:65:E0 (MAC) | fork-dev build + Safeboot v3 (10-19, WF-05 bench)         |
| DK5EN-92 | T-Beam v1.2     | 573C000584              | fork-dev build + Safeboot v3 (10-19, WF-05 bench)         |
| DK5EN-1  | Heltec V3       | 0001                    | fixed UDP-03 build (10-22 21:36); net console off default |

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

- Open work: [docs/BACKLOG.md](BACKLOG.md) is the single list. Do not copy items here.
- Defect IDs: [docs/architecture/08-defect-catalogue.md](architecture/08-defect-catalogue.md).
- Tests: [docs/test-suite-map.md](test-suite-map.md); runner:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Releases: [docs/release-journal.md](release-journal.md), [release-notes.md](../release-notes.md).
- History: [docs/archive/](archive/README.md). Session journals that used to live in this file:
  [2026-10-05 .. 2026-10-23](archive/resume-journal-20261005-20261023.md),
  [2026-09-03 .. 2026-10-01](archive/resume-journal-20260903-20261001.md).

## Last three sessions

- **2026-10-23:** upstream PR #1235 cut, opened and merged (synced 4898cada, worktree and branch removed); upstream sync (#1233 merged, #1234 SP server taken); net
  console clients consolidated on `mc_console.py` with `.env` passwords; mock keep-reply timer race fixed.
- **2026-10-22:** UDP-03 fixed and benched (upstream issue #1231), PR #1233 opened and merged; upstream sync
  (#1224 merged, #1225 taken).
- **2026-10-19/20:** releases v4.40a.10.20 and v4.40a.10.21; AU-16 root cause and fix (field-verified);
  WEB-WDT-01, WEB-STOP-01, RM-NVS-01 diagnostics; web flasher WF-01..WF-05; upstream PR #1224.
