# RESUME -- pick up here

**Stand:** 2026-10-03, branch `fork-dev`, HEAD `cfcfcb3c`, last release `v4.40a.10.02`.

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
- Stage 1: every `[env:native*]` (48 envs, 1627 Unity cases today) plus `test/golden/selftest.sh`.
- Stage 2: pytest over `tools/bench`, `tools/tests`, `tools/mock` (467 cases), the
  `test/test_nbrlog` scripts, node tests, the jsdom safeboot page test and four `--self-test`
  tools.
- Stage 3: `tools/bench/bench_suite.py` (nodes matched by USB serial, identity guard, per-board
  harness, OTA; `--flash`, `--extudp`, `--deepsleep` are opt-in). Never run against hardware yet.
- **Never run bare `pio test`**: it walks the board envs and flashes attached hardware.
- **One `pio` process at a time**: shared build cache; `selftest.sh` itself calls
  `pio project config`.
- Inventory of every suite: [docs/test-suite-map.md](test-suite-map.md). Runner details:
  [docs/automation-runner-runbook.md](automation-runner-runbook.md).
- Not verified until lint, typecheck, format-check and the real suite have run.

## Bench fleet

State of `tools/bench/fleet.json`, 2026-10-01. Nothing is attached today.

| Node     | Board           | USB serial       | Port last seen                | IP            |
| -------- | --------------- | ---------------- | ----------------------------- | ------------- |
| DK5EN-90 | RAK4631 (nRF52) | 230D6EBB3266D20E | /dev/cu.usbmodem1101          | ETH, DHCP     |
| DK5EN-14 | T-Deck Plus     | (native USB)     | /dev/cu.usbmodem2101          | WiFi          |
| DK5EN-92 | T-Beam v1.2     | 573C000584       | /dev/cu.usbserial-573C0005841 | 192.168.68.73 |
| DK5EN-1  | Heltec V3       | 0001             | /dev/cu.usbserial-0001        | 192.168.68.62 |

DK5EN-98 is the production Heltec V3 (logger on rpizero), not a bench node.

Handling:

- Ports move. Resolve by USB serial (ioreg or pyserial), never by name.
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

- N-36 (softAP on a fresh ESP32) is fixed on `fork-dev`; the upstream PR is not filed.
- The 4.40a soak on DK5EN-98 (OTA 2026-10-01 23:27) is not evaluated.
- First stage-3 run of the regression runner on real hardware.
- DR-16: drift-matrix verdict vs the code comment near `bTeleFirst` in `src/nrf52/nrf52_main.cpp`;
  the one row that keeps `--phase implementation` in `selftest.sh`.
- `--mesh off` still transmits on two RF paths (gateway DM-ACK, DM-store custody); operator
  decision pending.
- GPS: altitude Kalman re-seed length and baro fusion tau (both measured, neither changed).
- One unexplained TASK_WDT on DK5EN-1 (2026-09-28), web send path the only candidate.

## Last three sessions

- **2026-10-03:** end-to-end regression runner (`tools/regression.sh`, `/full-regression`), suite
  inventory `docs/test-suite-map.md`, stub move (`cfcfcb3c`); docs consolidated for re-entry.
- **2026-10-02:** 4.40a port (PR #1186 merged), release `v4.40a.10.02`, and the N-36 softAP fix
  (`42dbf03a`).
- **2026-10-01:** upstream sync check against `v4.35v.09.30`: three cherry-picks (settings saved
  where they change, T-Deck setup-page map, RAK HAMNET NTP) plus the `save_position()` guard;
  native gate green, advisor approved.
