# MeshCom Firmware - Development Guidelines

## Project Context

Open-source MeshCom Firmware. We contribute via PRs against the **upstream DEV branch** of icssw-org/MeshCom-Firmware.

## Start Here

- `docs/RESUME.md`: current state, last release, open items. Read it first in a fresh session.
- `docs/BACKLOG.md`: open work and operator decisions. `docs/architecture/08-defect-catalogue.md`: defect IDs.
- Campaign docs (`docs/campaign-*.md`) are the resume points of multi-wave work; keep them and `RESUME.md` current per wave.
- Reviews (own PRs, wave verdicts, foreign upstream PRs): `REVIEW.md` is the standard (R1-R4: complete across platforms, every env builds, checked against every surface and sanitizer, LoRa byte budget); it stays byte-identical to `upstream/dev`. Fork-only rules live in `docs/review-fork.md`.

## Branch Model

- `fork-dev` is the only working branch: `upstream/dev` plus the fork's docs, tests, tools, GitHub Pages **and fork-only firmware** (`src/`, `platformio.ini`, `variants/`) that is not upstream yet. New firmware lands on `fork-dev` first; upstream PRs are cut from `upstream/dev` later.
- `fork-nightly` (planned, NIGHTLY-01, operator 2026-11-16): the stable release branch. The operator merges into it himself; nobody else moves it. CI will build and publish it every night as a normal `v4.40a.MM.DD` release (for now even when unchanged). Releases come only from `fork-nightly`; no hand-cut `v*` tags from `fork-dev`.
- `fork-main`, `fork-neo`, `fork-neo-test` and `master` are retired (tag `archive/fork-neo-test-20261002`). Do not build on them.
- Sync upstream by cherry-pick; never merge fork history into upstream.
- Fork debug code goes into the instrumentation files (`src/instrument.*`, `src/test_inject.*`, `src/t-deck/tdeck_debug.*`) with one-line hooks in the regular sources.
- After icssw-org squash-merges a PR of ours, **merge** `upstream/dev` (not rebase) to re-sync.
- All PRs target upstream `dev`, with the description in German. DK5EN never merges upstream himself; the maintainers review and merge.

## Releases

- `/release-firmware` is the recipe. `MeshCom CI` (`.github/workflows/meshcom-ci.yml`) builds and publishes on `v*` tags (release, Latest) and `ci-proof-*` tags (draft; delete draft and tag afterwards).
- Never a same-day `.N` release (the auto-update tag parser rejects it). Local fork builds need `export MC_AU_DEV_CHANNEL=1`.
- Every `gh` call needs `-R DK5EN/MeshCom-Firmware` (no `gh` default repo is set; with two remotes it may pick upstream).

## PR Workflow

`/submit-pr` walks it: sync upstream first, branch from `upstream/dev` (not `fork-dev`), port the minimum, no rewrites or large refactors, detailed German PR body (which code changed and why), target upstream `dev`. `/sync-upstream` brings upstream into `fork-dev` (merge or cherry-pick, never rebase).

## Testing and Gates

- Host gate: `tools/regression.sh --stage 1,2` (all `[env:native*]` Unity envs in one `pio` call, golden selftest, two ESP32 and three nRF52 board builds, pytest and tool suites; about 3 min warm, 6 min when the ESP32 boards rebuild).
- Wave commits: `tools/regression.sh --changed` runs only the steps the uncommitted changes can turn red (`tools/regression_select.py`; an unknown path means the full gate). The full host gate stays mandatory before a release, an upstream PR, after an upstream sync and at the end of a campaign.
- With bench hardware: `tools/regression.sh --stage all` (adds stage 3, `tools/bench/bench_suite.py`; `--flash`, `--extudp`, `--deepsleep` are opt-in, passed after `--`).
- `/full-regression` wraps the runner (`.claude/commands/`, local and gitignored).
- **Never run bare `pio test`**: it walks the board envs and flashes attached hardware.
- **One `pio` process at a time** (shared build cache and global package store; `test/golden/selftest.sh` shells out to `pio project config`). Parallel writers route every `pio` call through a mkdir-lock wrapper (`piolock.sh` in the scratchpad).
- Suite inventory: `docs/test-suite-map.md`; the per-suite table is generated (`docs/test-suite-inventory.md`, `python3 tools/suite_map.py --write` after adding a suite, env or case).

## Toolchain Pitfalls

- nRF52 builds fail with make "Error 126" / "Bad CPU type": the pinned gcc-arm is x86_64 and Rosetta is missing. Run `softwareupdate --install-rosetta --agree-to-license`; do not bump the toolchain.
- Harness and bench logs go to `tools/bench/runs/harness/`, never the repo root.
- Raw `*.log` there is gitignored; the summaries (`.json`, `.jsonl`, `.summary`) are not. A wave's commit includes the summaries its docs cite and deletes the rest.
- ESP32 app builds are not byte-reproducible; compare RAM/flash regions, not image hashes.

## Hardware and Flashing

Bench fleet, USB serials, DHCP addresses, the 2323 console login and per-board flashing are in the local `/bench` command (`tools/bench/fleet.json` has the fleet); `/flash-rak` flashes the RAK4631. Load `/bench` before any flash, serial session or bench test.

## Bench Rules (hard)

- DK5EN-98 is the production gateway, not a bench node: no work on it without an explicit OK.
- Test traffic only to group `9999` or as a direct message to an own node. Never broadcast. (`::{TEST}` is a personal message to a station named TEST, not a group.)
- Never use a foreign callsign (never OE1XAR, never XX0XXX). Every callsign SSID is unique.
- Bench TX power at most 2 dBm.
- Run `tools/bench/identity_guard.py` before any test.
- Run `lsof <port>` before opening a port; no port open while `serial_capture.py` holds it (also Chrome Web Serial).
- Opening a serial port reboots every ESP32. The RAK4631 needs DTR to talk.
- To take a node off the air without powering it down, use `--airgap on` (INSTRUMENT builds, `docs/bench-airgap.md`).
