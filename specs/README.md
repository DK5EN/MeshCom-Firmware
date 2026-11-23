# Formal checks

**Two tracks check the firmware: TLA+ models for task and interrupt interleavings, bounded model checking for
parsers. Both run only on request. A self-test without any solver runs in every host gate.**

Review and plan: [`docs/formal-review-20261123.md`](../docs/formal-review-20261123.md). Campaign history:
[`docs/campaign-formal-01-20261123.md`](../docs/campaign-formal-01-20261123.md).

> [!WARNING]
> **Nothing in this directory has been run since the rework of 2026-11-23.** The models were edited without a
> parser. The first step of the next run is `specs/tla/run_tlc.sh --sany-all`.

## Key facts

- **TLA+ track:** 5 models, 57 configurations in `tla/`. Runner `tla/run_tlc.sh`. Proves properties of a model
  written by hand from the C code.
- **Bounded track:** 8 groups, 69 jobs in `cbmc/`. Runner `cbmc/run_cbmc.sh`. Compiles the real `src/` files, so a
  pass is a statement about the code up to the stated bound.
- **Always on:** `specs/selftest.sh` in stage 2 of `tools/regression.sh`. It lints the models, checks them for
  drift against `src/` and tests the verdict logic of both runners on canned tool output.
- **On request only, and only local:** `tools/regression.sh --formal`, or the runners directly, on the operator's
  Mac. No GitHub run (operator 2026-11-23: the Mac is stronger than a hosted runner).
- **Tool versions** are pinned in `tools.lock`. A different version is a tool error.
- **Results** are written by the runners to `tla/RESULTS.md` and `cbmc/RESULTS.md` and committed. Counterexample
  traces are regenerated on every run and stay out of git.

## How to run

```
specs/selftest.sh                          # no solver: lint, drift, runner logic
specs/tla/run_tlc.sh --sany-all            # parse every model (Java, seconds)
specs/tla/run_tlc.sh --all                 # every configuration, writes tla/RESULTS.md
specs/tla/run_tlc.sh netconsole.cfg        # one configuration
specs/cbmc/run_cbmc.sh --list              # jobs with tier, memory budget, expectation
specs/cbmc/run_cbmc.sh --all               # quick tier, writes cbmc/RESULTS-quick.md
specs/cbmc/run_cbmc.sh --all --heavy       # quick and heavy tier, writes cbmc/RESULTS.md
specs/cbmc/run_cbmc.sh aprs/pass_40.check  # one job
```

- **One runner at a time.** No parallel solver runs on one machine.
- **Memory:** every bounded job has a budget (`MEM` in its `.check`, default 4096 MB quick, 12288 MB heavy). A job
  above its budget is killed and reported as `memout`, a tool error. `MEM_MB=<n>` overrides all budgets.
- **macOS has no hard memory limit per process.** The budget is enforced by the runner, which polls the footprint
  twice a second and kills the job. Unbudgeted solver runs exhausted the machine twice on 2026-11-23.

## Rules

1. **Every property that holds has a red twin.** A mutation must turn it red, and for the named reason. TLA+:
   `\* tlc: expect-fail=<Property>`. Bounded: `EXPECT=fail` plus `EXPECT_PROP`.
2. **A property must be able to fail.** A property that restates a guard of its own model proves nothing. Judge a
   mutation by its consequence: a spliced frame, a session on a dead socket. Not by a flag the guard sets.
3. **A mutation of the code beats a mutation of the harness.** `mut_code_*` jobs remove a bounds check from a copy
   of the source. A buffer that is one byte short only proves that the tool is awake.
4. **A pass job keeps unwinding assertions.** The runner refuses `--no-unwinding-assertions` in a pass job unless
   the job states an `UNWIND_WAIVER`.
5. **A real defect is recorded as an expected-red check** named `<model>_today_*.cfg`, so the gate stays green and
   the defect stays visible. The fix flips it. Configurations of a design that is gone are named `*_was_*`.
6. **A model names its source functions** in `manifest.json`. When one of them changes, `drift.py --check` fails
   the host gate. Read the change, update the model if the behaviour changed, then `specs/drift.py --accept <model>`.
7. **State the bound.** A bounded proof is complete only up to its input length and unwind bound. Both are in the
   job's `CHECKS` text.

## What is not covered

- **The models are hand translations.** A TLA+ result holds for the model. Planned link to the code: replay of
  model traces as host tests, monitors on bench captures, asserts for model assumptions (BACKLOG `FORMAL-LINK`).
- **Assumptions no tool checks:** one writer to the TX ring on ESP32, no re-entry of the radio callback on nRF52,
  every byte-ring access inside its lock, a critical section stops every other actor.
- **Timeouts are fairness.** Deadline arithmetic is outside every model.
- **Full-frame proofs** of `decodeAPRS` and `decodeAPRSPOS` sit in the heavy tier and ran out of memory at 6 GB on
  2026-11-23. The quick tier proves `decodeAPRS` up to 40 bytes.
- **Not modelled at all:** entry-point parsers that need a seam, 16 of 21 findings of the concurrency map, power
  loss during an update or a settings save, anything across more than one node.

## Files

| Path                        | Content                                                    |
| --------------------------- | ---------------------------------------------------------- |
| `selftest.sh`               | Self-test of the instruments, no solver                    |
| `tla_lint.py`               | Static lint of models and configurations                   |
| `drift.py`, `manifest.json` | Model to source mapping and drift check                    |
| `tools.lock`                | Pinned tool versions and the checksum of the TLA+ jar      |
| `tla/`                      | Active models, configurations, runner, results             |
| `tla/archive/heap/`         | Retired heap models of OTA-LOWHEAP-01, not part of `--all` |
| `cbmc/`                     | Harnesses, jobs, runner, results                           |
