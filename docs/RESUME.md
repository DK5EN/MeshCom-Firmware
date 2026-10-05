# RESUME -- pick up here

**Stand:** 2026-10-05, branch `fork-dev`, last release `v4.40a.10.06` (dated 10-06, published 10-05).

## 2026-10-05 -- web GUI for #1187/#1189/#1190, release v4.40a.10.06

- Shipped (fork-dev `e1801d9f`, `839e952b`): Auto Update as one web selector off/prod/dev, MTU card,
  remote-management card + send panel (`POST /rmsend`, `GET /rmstatus`), header "Meshcom 4.40a";
  console `--rm` renamed to `--remotemgmt` (`node_rm` and web key `rm` unchanged).
- Release `v4.40a.10.06` from `573b6444`: 66 assets, flasher keep 1 (10.05 pruned, 30/30 verified),
  Safeboot unchanged from 10.05. Dated ahead because the AU tag parser rejects a `.2` suffix: never cut
  a same-day `.N` release while AU exists; date it the next day instead.
- AU dev channel on DK5EN-1: check found 10.06, GitHub download 1.11 MB in 10.8 s, Safeboot apply
  1.74 MB in 10.6 s, boots `tag=v4.40a.10.06`.
- Bench nodes: DK5EN-1 runs v4.40a.10.06 (updchan dev); DK5EN-90 runs the 839e952b dev build, RM off.

## 2026-10-05 -- campaign #1187-#1191 done, release v4.40a.10.05

- Shipped (fork-dev, `c16a9d5f`..`bfe5310c`): MTU default 1280 on every board and Safeboot (#1190),
  BLE reconnect hardening (#1191), S&F for PMs from the server + `--stor` (off, #1188), RM1 remote
  management with HMAC (off, #1189), Auto Update (off, #1187: `.bin.zz` staging in the end of
  `ota_0`, five GitHub roots, Safeboot ROM-tinfl apply, classic ESP32 pauses BLE, old-Safeboot
  detection). Plan and decisions: `docs/concept-open-issues-20261004.md` (status table, section 9).
- Release `v4.40a.10.05`: 66 assets (39 + 27 `.bin.zz`), web flasher (keep 1, 30 boards verified),
  new Safeboot images. Dev-channel AU test on DK5EN-1 against this release: check, real GitHub
  download (1.10 MB in 10.8 s), Safeboot apply (1.73 MB in 10.5 s), boots `v4.40a.10.05`.
- Open: STOR needs the server operator's approval; SNF M1-M4 against the real server; prod AU needs
  icssw-org to publish `.bin.zz` assets; upstream PRs for the five features not opened; BLE backlog B1
  (bleQueue carry-over); classic Safeboot headroom 1.5 KB (drop ESPmDNS next time it grows).
- AU-12 (Safeboot capability version): Safeboot carries `MCSB;ver;002`, the app scans the partition and
  locks auto update (command, boot demotion, handover, greyed web dropdown + footnote) below
  `AU_SAFEBOOT_MIN`. Contract: `docs/safeboot-ota-contract.md` "Capability version". Needs a web-flasher
  update on every node whose Safeboot predates 2026-10-05; not yet released. Bench 2026-10-05 passed:
  DK5EN-92 with the pre-AU Safeboot (git 12e35ad0, v0): `safeboot;old;sbver;0`, `--autoupdate auto` refused,
  web `aumode=dev` fails with the channel untouched, dropdown disabled + footnote, a config-import auto mode
  demoted to notify at boot; v1 (strings) and v2 (marker) read correctly on DK5EN-92/-1; DK5EN-1 full USB
  flash gives `sbver;2` and `[SAFEBOOT];ver;2`. DK5EN-92 restored to the current Safeboot (v2), AU off. The
  nocap-reflash path (F1) is host-tested only.
- Bench nodes: DK5EN-1 runs v4.40a.10.05 (new Safeboot); DK5EN-90 normal build, `--remotemgmt off`;
  DK5EN-92 runs an INSTRUMENT v4.40a.10.05 with the OLD Safeboot (flagged `safeboot;old`, a staged
  test record remains, AU off). node_passwd on DK5EN-1/-90 set to bench values (rmbench1/rmbench90).

## Branch model

- `fork-dev` is the only working branch: `upstream/dev` (icssw-org/MeshCom-Firmware) plus ONE
  port commit that carries the fork-only docs, tests, tools and GitHub Pages.
- Retired 2026-10-02: `fork-main`, `fork-neo`, `fork-neo-test`, `master` (tag
  `archive/fork-neo-test-20261002`). Nothing is developed there any more.
- Upstream sync: cherry-pick, never merge the fork history in. After icssw-org squash-merges a PR
  of ours, **merge** (not rebase) to re-sync.
- All PRs target upstream `dev`; PR text is German and lists files, functions and the why.
  Minimal, targeted changes only. DK5EN never merges upstream himself (Kurt reviews and merges).
- Upstream state: PR #1186 (4.40a: Store-and-Forward, Nachbarschaftsmatrix, serial config)
  merged 2026-10-02 as `e1e2acea`. No open upstream PR by dk5en today (checked via `gh`).
  The N-36 fix (`42dbf03a`) is not yet offered upstream.

## Last release and flasher policy

- `v4.40a.10.02`, cut from `fork-dev` `08b7dd8b`; firmware is byte-identical to the official
  `v4.40a`.
- The web flasher on gh-pages offers only the latest release (`--keep 1`).
- Both GitHub Actions workflows are disabled; releases are manual via `/release-firmware`.
- Release history: [docs/release-journal.md](release-journal.md); user-facing delta:
  [release-notes.md](../release-notes.md).

## How to verify the tree

- Host: `tools/regression.sh --stage 1,2`. With the bench fleet attached: `--stage all`. The
  `/full-regression` skill wraps it (local only, `.claude/commands/` is gitignored).
- Stage 1: every `[env:native*]` (48 envs, 1698 Unity cases today) plus `test/golden/selftest.sh`.
- Stage 2: ruff syntax gate, pytest over `tools/bench`, `tools/tests`, `tools/mock` (553 cases), the
  `test/test_nbrlog` scripts, node tests, the jsdom safeboot page test and four `--self-test`
  tools.
- Stage 3: `tools/bench/bench_suite.py` (nodes matched by USB serial, identity guard as the gate,
  `prepare <node>`, per-board harness, OTA with its own image build, web GUI badge, mesh exchange;
  `--flash`, `--extudp`, `--deepsleep` are opt-in, `--instrument` is the default). Green on RAK,
  T-Beam and T-Deck on 2026-10-04 (run 9, `docs/bench-20261003-stage3.md`). A bench run flips the
  PlatformIO checksum (`PLATFORMIO_BUILD_FLAGS`), so the next stage 1 builds cold.
- **Never run bare `pio test`**: it walks the board envs and flashes attached hardware.
- **One `pio` process at a time**: shared build cache; `selftest.sh` itself calls
  `pio project config`.
- Inventory of every suite: [docs/test-suite-map.md](test-suite-map.md). Runner details:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Not verified until lint, typecheck, format-check and the real suite have run.

## Bench fleet

State of `tools/bench/fleet.json`, 2026-10-04. RAK, T-Beam and T-Deck are on USB; the RAK runs an
instrument image (`INSTRUMENT_ENABLED=1`) of `fork-dev`, T-Beam and T-Deck run the plain WEB-04
image (17:52 build, OTA 2026-10-04 evening); the Heltec is unplugged.

| Node     | Board           | USB serial              | Port last seen                | IP            |
| -------- | --------------- | ----------------------- | ----------------------------- | ------------- |
| DK5EN-90 | RAK4631 (nRF52) | 230D6EBB3266D20E        | /dev/cu.usbmodem1101          | 192.168.68.77 |
| DK5EN-14 | T-Deck Plus     | E0:72:A1:AD:65:E0 (MAC) | /dev/cu.usbmodem101           | 192.168.68.70 |
| DK5EN-92 | T-Beam v1.2     | 573C000584              | /dev/cu.usbserial-573C0005841 | 192.168.68.75 |
| DK5EN-1  | Heltec V3       | 0001                    | /dev/cu.usbserial-0001        | 192.168.68.76 |

DK5EN-98 is the production Heltec V3 (logger on rpizero), not a bench node.

Handling:

- Ports move and DHCP moves the IPs per reboot; the driver reads the live IP from `--info` and
  writes it back to `fleet.json`. Resolve ports by USB serial (ioreg or pyserial), never by name.
- Harness `--node` takes the fleet key (`t-deck-14`), not the callsign.
- Heltec V3 on CP2102: DTR is the PRG button, a held DTR is a long press (deep sleep). The guard
  asserts DTR only on native-USB ports.
- Opening a port reboots every ESP32. The RAK needs DTR. The T-Beam flashes at 460800 (921600
  fails).
- The RAK web GUI has no mDNS (the Ethernet path ships no responder): reach it by IP from the
  boot log (`Ethernet.localIP():`).

Hard bench rules:

- Test traffic only to group `TEST` or as a direct message to an own node. Never broadcast.
- Never a foreign callsign (never OE1XAR, never XX0XXX).
- Bench TX power at most 2 dBm.
- Run `tools/bench/identity_guard.py` before any test.
- No USB port open while `serial_capture.py` holds it (check `lsof`).
- `pio run -t upload` needs an explicit `--upload-port`; autodetect can land on the T-Deck.

## Where things are

- Open work: [docs/BACKLOG.md](BACKLOG.md) is the single list. Do not copy items here.
- Tests: [docs/test-suite-map.md](test-suite-map.md); runner:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Releases: [docs/release-journal.md](release-journal.md), [release-notes.md](../release-notes.md).
- History: [docs/archive/](archive/README.md). The session journal 2026-09-03 .. 2026-10-01 that
  used to live in this file is
  [docs/archive/resume-journal-20260903-20261001.md](archive/resume-journal-20260903-20261001.md).

## Hottest first (details in BACKLOG)

- WEB-04 (web GUI delivery ticks vanish, DK1TCP-77): fixed and bench-verified on `fork-dev`
  2026-10-04, fork only. MCApp's own ack-matcher bugs went to the webapp agent
  (`~/Desktop/webapp-ack-matcher-issue.md`). Verdict and log:
  `docs/webgui-ack-ticks-verdict-20261004.md`, `docs/webgui-ack-ticks-impl-plan.md`.
- T-Deck campaign (`docs/campaign-tdeck-w02-20261004.md`): both waves done on 2026-10-04 (TD-15
  map restore + hemisphere fix, TD-09 tile cache, TD-11 ACK glyph, W0.2 warning flags on the five
  envs and on `nrf52_base`), nothing offered upstream yet: one German PR for TD-09/TD-11/TD-15
  and one for W0.2 + T5-01 + EXT-01 are the next step. All three bench nodes run instrument images of
  this tree since 2026-10-04 16:45 (RAK DFU, T-Beam and T-Deck OTA); `--stage all` green apart
  from a fixed harness false positive.
- N-36 (softAP on a fresh ESP32) is fixed on `fork-dev`; the upstream PR text is drafted
  (`docs/pr-draft-n36-20261003.md`), not filed.
- RX-01: the SX127x T-Beam missed a direct beacon during its `RX_TIMEOUT` receive restart (one
  observation, run 9); measure over a soak before touching upstream's defer logic.
- `--mesh off` still transmits on two RF paths (gateway DM-ACK, DM-store custody); operator
  decision pending.
- GPS: altitude Kalman re-seed length and baro fusion tau (both measured, neither changed).
- One unexplained TASK_WDT on DK5EN-1 (2026-09-28), web send path the only candidate.

## Last three sessions

- **2026-10-04:** bench campaign (prepare step, RAK under the driver, TD-20 = trackball clamp,
  stage 3 green on three nodes); RX-01 24 h soak on DK5EN-98 started (console on rpizero,
  passive Extern-UDP sniffer on mcapp, evaluate 2026-10-05 after 11:17); T-Deck campaign wave 1:
  TD-15 map restore after reboot, TD-09 PSRAM tile cache, MEM-04 re-measured and closed, W0.2
  warning inventory; three new harness scenarios (`map_rebuild`, `map_persist_seed/check`).
  Afternoon: DR-16 re-decided (both-valid, `test_drift_dr16_gates`, `--phase` gone from the
  selftest), TM-43 Extern-UDP soak on the RAK run for the first time, the regression open-points
  paper closed and archived (`docs/archive/regression-offene-punkte-20261003.md`).
  Evening: T-Deck campaign wave 2 (TD-11 ACK glyph in the bubble, W0.2 flags on five envs +
  nrf52_base, `msg_ack` harness scenario), then T5-01 and EXT-01 fixed blind (five of five envs
  with `-Werror`), and EXT-03: Extern-UDP refuses broadcast/multicast targets. Evening: WEB-04 web GUI ticks (verdict via fable-review, one
  orchestrated wave, advisor must-fix for late ACKs, before/after bench on DK5EN-92/-14).
- **2026-10-03:** end-to-end regression runner (`tools/regression.sh`, `/full-regression`), suite
  inventory `docs/test-suite-map.md`, stub move (`cfcfcb3c`); docs consolidated for re-entry.
- **2026-10-02:** 4.40a port (PR #1186 merged), release `v4.40a.10.02`, and the N-36 softAP fix
  (`42dbf03a`).
