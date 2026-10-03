---
description: Run the complete end-to-end regression - stage 1 host Unity envs + golden selftest, stage 2 host tool suites, stage 3 bench fleet over USB - and report one verdict with live counts
allowed-tools: [Bash, Read, Grep, Glob]
---

Run the fork's full regression and report whether the firmware tree is green.
The mechanics live in `tools/regression.sh`; this skill decides the stages,
watches the run, and writes the verdict. Inventory and rationale:
`docs/test-suite-map.md`.

## Arguments

- No args: stages 1 and 2 (no hardware; ~5 min on a warm .pio build cache, 20-40 min after a clean build).
- `all` or `--bench`: stages 1, 2 and 3 (bench fleet on USB).
- `1`, `2`, `3`, or a comma list: only those stages.
- `--flash`, `--extudp`, `--deepsleep`, `--soak-seconds N`: passed through to
  stage 3 (`tools/bench/bench_suite.py`). `--flash` builds and reflashes every
  attached node first.
- `--list`: print the plan, run nothing.

## Step 0: Preconditions

```bash
git status --short | head
pgrep -fl 'pio (test|run|project)' || echo "no pio running"
ls /dev/cu.* 2>/dev/null
```

- A dirty tree is fine, but name the uncommitted files in the report so the
  verdict is attributed to the right state.
- If another `pio` process is running, stop: one `pio` at a time (shared
  build cache; `selftest.sh` shells out to `pio project config`). Never start
  a second one and never run bare `pio test`.
- Stage 3 needs at least one `tools/bench/fleet.json` node on USB. With no
  `/dev/cu.usb*` port present, say so and drop stage 3 instead of failing
  the whole run. Bench rules still apply: no test traffic to anything but
  group `TEST`, no node without its own callsign (the identity guard
  enforces this and the driver skips a node whose guard fails).

## Step 1: Launch

```bash
tools/regression.sh --stage <stages> [-- <bench args>]
```

Run it in the background with the output captured; stage 1 alone takes 5 minutes
warm and up to 30 minutes cold. Poll the log every few minutes rather than waiting blind.
The script writes every step's log plus `summary.log` to
`tools/bench/runs/regression-<timestamp>/` (gitignored) and ends with
`RESULT: PASS` or `RESULT: FAIL (n step(s))`.

## Step 2: Read the result

- The last block of the output is the summary table (`OK`/`FAIL`/`SKIP` per
  step) followed by `Unity: <envs> envs, <cases> cases, ...`.
- For every `FAIL` line, open the named log and quote the first failing
  assertion or error, not the whole log.
- `SKIP` on `topo_shadow raw window` means rpizero was unreachable and the
  full-window case was ignored; say so, it is not a failure.
- Stage 3: `bench-summary.json` in the run directory has one entry per step
  with `rc`, seconds and log path; a node whose identity guard failed shows
  its later steps as `SKIP` with the reason.

## Step 3: Report

BLUF first line: `PASS` or `FAIL`, commit, stages, duration. Then:

- The live Unity line (envs, cases, failed, skipped) and the stage 2 counts
  as printed. Never reuse a number from an earlier run or from
  `docs/test-suite-map.md`.
- Each failure: step, one-line cause, log path.
- Anything skipped and why (unreachable rpizero, node not attached, port held
  by another process).

Do not fix failures inside this skill. A red step is a finding to hand back;
the fix, its regression test and the re-run are separate work.
