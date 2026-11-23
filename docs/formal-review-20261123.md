# Formal checks: sound method, weak link to the code

Review of the formal verification setup (FORMAL-01, OTA-LOWHEAP-01 models). Reviewed 2026-11-23 against `fork-dev`
at `799aa238`. Read-only: no model and no solver was run for this review (operator rule: formal runs only on explicit
request). Sources: `specs/tla/`, `specs/cbmc/`, `tools/regression.sh`, `docs/campaign-formal-01-20261123.md`, run logs
under `tools/bench/runs/regression-20261123-*`.

**The setup finds real defects and its discipline is good, but today it proves properties of hand-written models and
small harnesses, not of the firmware. Three models no longer match the code, the two main memory-safety proofs never
finished, and about a third of the checked properties hold by construction.**

Recommendation: repair the instruments first (one wave, no solver), then tie models to code, then extend. Do not add
models before that.

> [!WARNING]
> **Decisions needed from the operator** (details in section 6)
>
> - **Where the solvers run.** Decided 2026-11-23: local on the Mac only, no GitHub run.
> - **What happens to `heap.tla`.** Recommendation: retire it as a model, keep its eight rules as a checklist.
> - **Whether counterexample traces stay in git.** Recommendation: no, keep a results summary instead.
> - **54 trace files are modified and uncommitted** in `specs/tla/traces/`. They come from a re-run. Revert or commit.

> [!IMPORTANT]
> **Status 2026-11-23, after the review: waves 0 to 2 are implemented and unrun.** This document describes the setup
> as it was found. The current state, the rules and the commands are in [`specs/README.md`](../specs/README.md).
> Section 7 lists what was done and what is still open.

## Key facts

- **Inventory:** 7 TLA+ models with 117 configurations, 8 bounded-model-checking groups with 59 jobs plus 1 parked
  heavy job. Runners: `specs/tla/run_tlc.sh`, `specs/cbmc/run_cbmc.sh`. Gate hook: `tools/regression.sh --formal`,
  off by default.
- **Last TLA+ run:** 113 of 113 configurations as expected on 2026-11-23 15:56
  (`regression-20261123-155623/stage2-tla.log`). 4 configurations added or renamed since then are unrun.
- **Last bounded run:** aborted after 22 jobs on 2026-11-23 14:55. 17 as expected, 5 out of memory. The groups
  `fw_assets`, `remote_cmd`, `settings_decode` and `url_decode` have no recorded result in any log I found.
- **Real findings so far:** 7, listed in section 2. All were found by the method, none by luck.
- **Coverage of the concurrency map:** 5 of 21 findings (F2-4, F2-5, F2-7, F2-10, F2-14) have a model.
- **Stale against the code:** `netconsole.tla`, `heap_frag.tla` and every `*_today_*` configuration of `extqueue`.
- **Record of results:** only the heap models have a results table. The campaign document's results section is
  empty. All run logs are gitignored.

## 1. What exists

Two independent tracks with different strength.

| Track                    | What is checked                           | Input to the tool                   | Strength                                  |
| ------------------------ | ----------------------------------------- | ----------------------------------- | ----------------------------------------- |
| TLA+ with TLC            | Interleavings of tasks, ISRs, environment | A model written by hand from the C  | Finds design races. Says nothing on code. |
| ESBMC (CBMC as fallback) | Memory safety and contracts of parsers    | The real `src/*.cpp` plus a harness | Proves the code, up to a stated bound.    |

| Model            | Lines | Configs | Subject                                                                     |
| ---------------- | ----- | ------- | --------------------------------------------------------------------------- |
| `heap.tla`       | 211   | 15      | Ownership of heap blocks around one Auto Update job (OTA-LOWHEAP-01)        |
| `heap_frag.tla`  | 343   | 47      | Heap fragmentation, 24 cells of 4 KB, today's gate and a static reservation |
| `txring.tla`     | 306   | 14      | TX slot pool: enqueue, claim for radio, finish, sweep, ACK scan (F2-4)      |
| `extqueue.tla`   | 140   | 11      | Two-slot ring between the radio callback and the UDP flush (F2-14)          |
| `rxflags.tla`    | 168   | 8       | ESP32 radio interrupt flags against the loop state machine (F2-7)           |
| `bytefifo.tla`   | 252   | 11      | Byte ring with generation counters, drain reader, history iterator (F2-5)   |
| `netconsole.tla` | 215   | 11      | Network console: handshake slot, session takeover, teardown (F2-10)         |

| Bounded group     | Jobs | Target in `src/`                                 | Bound                              |
| ----------------- | ---- | ------------------------------------------------ | ---------------------------------- |
| `conf_frame`      | 3    | `parseConfFrame`                                 | full frame, 255 B                  |
| `aprs`            | 9    | `decodeAPRS`, `encodeStartAPRS`, `decodeAPRSPOS` | 255 B intended, see section 3      |
| `fw_assets`       | 10   | `FwJsonScan` release JSON scanner                | unbounded through an inductive job |
| `applink_ws`      | 6    | `alWsFeed` websocket frame decoder               | 10 B stream, 300 B jobs fail       |
| `remote_cmd`      | 10   | `rmParse`, `rmCheck` remote management           | 32 B free text, 90 B shaped text   |
| `settings_decode` | 12   | `decode_*`, `unescape_string`                    | values up to 8 B                   |
| `url_decode`      | 4    | `url_percent_decode`                             | 24 B                               |
| `byte_fifo`       | 5    | `bf_push2`, `bf_peek_gen`, `bf_pop_if`, iterator | capacity 8, 4 operations           |

## 2. What is good

Keep these. They are the reason the campaign is worth continuing.

- **Every green check has a red twin.** A property that holds needs a mutation that breaks it. The runners treat a
  mutation that stays green as a failure.
- **Reachability witnesses.** `*_reach_*` configurations and `witness_*` jobs show that the interesting path is taken
  at all. This guards against proofs over an empty state space.
- **The bounded track compiles the real source.** No copy, no model. A harness without a body for a callee is an
  error, the word size is forced to 32 bit, every mutation must fail for a named reason (`EXPECT_PROP`).
- **`fw_assets/step.check` is the best job in the tree.** It proves one input byte for any state that satisfies an
  invariant, so the result holds for input of any length.
- **Sensitivity configurations document which defence carries the load.** `txring_sweepinflight_dead` shows the
  sweep's in-flight skip is dead code today. `txring_lenfirst_cs` shows the write order is redundant under the lock.
  `netconsole_mut_aba` shows what breaks if accept ever moves to another task.
- **Real findings:**
  - Extern queue on nRF52: torn, duplicated and silently dropped frames (F2-14). Fixed in `src/ext_queue_core.h`.
  - UDP drain: duplicate and lost frame through two lock sections (F2-5). Fixed.
  - Network console: one silent peer locks out honest clients for 10 s (F2-10). Fixed.
  - `encodeStartAPRS`: latent write past the 255 B target if path fields grow (F3-8). Fixed.
  - Asset scanner: NUL byte in a download URL yields an accepted asset with an empty URL. Fixed.
  - Heap: the Auto Update gate can pass and the job still fails, and a tab plus a phone block updates forever.
    Matches the field reports. Background: `specs/tla/README.md`.
  - Radio flags: a stalled loop can read the chip while it transmits (`rxflags_today_stall_norxintx`).

## 3. What is wrong or weak

### 3.1 Three models describe code that no longer exists

A model is only evidence while it matches the source. Nothing detects drift today.

| Model                         | Model says                                      | Code says                                                                                  |
| ----------------------------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------ |
| `netconsole.cfg`              | "the real code is this cfg", `Takeover = FALSE` | Takeover is live with generation counters and `shutdown()` (`src/net_console.cpp:526-565`) |
| `netconsole.tla` takeover     | Preempted task ends in one atomic close         | The old task keeps running until its `recv` wakes. Its later writes are the actual risk.   |
| `heap_frag.tla`               | Two 16.7 KB TLS record buffers from the heap    | Records come from the reserved 32 KB buffer (`src/esp32/fw_update_net.cpp:323-384`)        |
| `extqueue_today_*.cfg`        | "today" writer never reads `used`               | Writer checks first since the fix. The files describe the past under a present-tense name. |
| `specs/tla/README.md` anchors | Gate at `:701-722`, probe at `:652-654`         | Gate at `:931`, probe at `:877`                                                            |

- **Consequence:** the network console fix is the newest security-relevant change and its model skips the one hazard
  the fix introduces. BACKLOG `FORMAL01-RUN` already names this.
- **Claim ahead of evidence:** `src/ext_queue_core.h` says "Proven by `extqueue_fix_*.cfg`". The run exists, but the
  proof is of the model, and section 3.4 lists what that model assumes.

### 3.2 The two main memory-safety proofs never finished

- **`aprs/pass` ran out of memory after 114 s** at the 6 GB cap. This is the proof for `decodeAPRS` over a full
  frame, the largest untrusted input surface of the firmware.
- **`aprs/pos_pass` ran out of memory after 76 s.**
- **All 300-byte AppLink jobs ran out of memory.** What passed is a 10-byte stream.
- **`aprs/pass` and `aprs/enc_pass_widepath` run with `--no-unwinding-assertions`.** A loop deeper than the bound is
  then cut silently. This breaks the campaign's own rule and would make a pass unsound even if it finished.
- **Replay protection of remote management is not checked.** The harness needs `ROUNDS=2` for the replay and cache
  paths. No job sets it.

### 3.3 Properties that hold by construction

These properties restate a guard of the model. The mutation flips the same guard. They cannot fail for any reason
other than the switch, so they add no information about the design.

| Model        | Property                            | Why it cannot fail                                                                            |
| ------------ | ----------------------------------- | --------------------------------------------------------------------------------------------- |
| `txring`     | `NoEvictInflight`                   | Candidates exclude in-flight slots, the flag is set only for an in-flight victim.             |
| `txring`     | `NoExpireInflight`                  | Same shape for the sweep and the ACK scan.                                                    |
| `rxflags`    | `NoSilentLoss`                      | The park service only starts when the ring has room, and nothing fills it between.            |
| `netconsole` | `NoKickActive`                      | Preemption is allowed only while the peer has not sent. The flag records exactly that.        |
| `netconsole` | `NoRejectGood` in `fix_slot`        | Holds only in the scripted scenario of one silent and one honest peer.                        |
| `netconsole` | `SlotFree`                          | The deadline is modelled as an always-enabled action under fairness.                          |
| `extqueue`   | `NoSilentDrop` against `CountDrops` | Tests a bookkeeping variable of the model, not the design.                                    |
| `heap`       | H1, H2, H4 bound, H7, H8            | The allocate, free, unwind and reboot actions establish them directly.                        |
| `heap_frag`  | F1, `PoolOne`, `NoOverlap`          | F1 records the gate condition and compares it with itself. The others restate lock and order. |
| `heap_frag`  | F4                                  | Plain arithmetic, 22 + 9 > 24. Needs no model checker.                                        |

- **`heap.tla` as a whole** is a statement of intent. It asserts that the unwind frees everything. It does not check
  that the C code does. Its value is the list of eight rules.
- **Most bounded "mutations" mutate the harness, not the code.** A buffer one byte short proves the tool sees an
  out-of-bounds read. It does not prove the property would catch a removed bounds check. Only `fw_assets/mut_nulurl`
  and `aprs/enc_mut_widepath` break the real code.

### 3.4 Assumptions nobody checks

Each model result depends on these. None is enforced by a test, an assert or a tool.

- **ESP32 has a single writer to the TX ring** (`src/txring_functions.cpp:793-800`, C-01). With one process the
  ESP32 configuration is a sequential program and its race properties are empty.
- **The nRF52 radio callback is never entered twice at once.** The `extqueue` header names two contexts for it, the
  LORA task and the timer service task. The model has one writer.
- **Every access to a byte ring sits inside its lock,** and each lock section is one atomic step.
- **A critical section on nRF52 stops every other actor,** including interrupts that touch the same data.
- **Timeouts are fairness.** Where the code has a deadline the model has an action that may always fire. The deadline
  arithmetic, where such bugs live, is outside every model.

### 3.5 Gaps against the stated goal

The campaign goal is long-term stability and non-exploitability.

- **No liveness outside `txring`, `heap` and one console property.** "No stuck state" is unchecked for the radio
  flags, the extern queue and the byte ring. `rxflags` has no invariant that the node listens again after every path.
- **Entry-point parsers are not covered.** `handleACK`, `readPhoneCommand`, UDP receive and the LoRa receive path
  need seams (BACKLOG `FORMAL01-SEAMS`). Covered today are the functions that were already easy to isolate.
- **16 of 21 concurrency findings have no model,** among them the BLE host task against the loop (F2-8) and the
  shared SPI bus (F2-19).
- **Bounds are small.** `rxflags` explores 373 states, `extqueue` 400 to 850. Larger constants cost seconds.
- **Nothing on persistence.** Settings migration and power loss during a save or an update are the paths that can
  cost a working node. No model, no proof.

### 3.6 Runner defects

- **`run_tlc.sh` accepts any violation as the expected one.** A mutation that dies on `TypeOK` or on a deadlock
  counts as success. The bounded runner already requires a named property. The TLA+ runner needs the same.
- **A job description containing `|` corrupts the results table.** `applink_ws/pass_big_single` ran out of memory
  and its table row reads "pass". The exit code was still correct.
- **Committed traces change on every run.** TLC with several workers returns a different counterexample each time.
  That is the source of the 54 modified files.
- **No pinned tool versions.** The TLA+ jar has no checksum. Homebrew moves ESBMC. A Java 27 bug is already worked
  around by hand.
- **A missing Java is reported as a failed model** (exit 127, first run of 2026-11-23), not as a tool error.
- **No documentation for `specs/cbmc/`,** and `specs/tla/README.md` covers only the two heap models. The campaign
  document refers to `../software-audit.md`, which does not exist.

## 4. Verdict per model

| Item                                    | Verdict               | Reason and next step                                                                                               |
| --------------------------------------- | --------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `bytefifo.tla`                          | Keep                  | Best TLA+ model. Real counters, real findings. Add the reset and detach paths.                                     |
| `extqueue.tla`                          | Keep, rename          | Small and sharp. Rename `today` to `was`. Settle the two-context question.                                         |
| `txring.tla`                            | Keep, repair          | Replace the two tautologies by a consequence: the sent frame is the claimed frame. Add the retry of parked frames. |
| `rxflags.tla`                           | Keep, extend          | Add "idle and in receive implies listening". Raise bounds. Drop `NoSilentLoss`.                                    |
| `netconsole.tla`                        | Rework                | Model the generation counter and the preempted task's late writes. Until then no evidence for the fix.             |
| `heap_frag.tla`                         | Rework or freeze      | Valuable design study of the old gate. Re-model the 32 KB buffer or mark it historical.                            |
| `heap.tla`                              | Retire                | Convert H1 to H8 into checks on real allocation traces (section 5).                                                |
| `fw_assets`                             | Keep, use as template | The inductive step pattern solves the memory problem of the other parsers.                                         |
| `conf_frame`, `url_decode`, `byte_fifo` | Keep                  | Complete and cheap.                                                                                                |
| `aprs`                                  | Rework                | Split `decodeAPRS` until the full-frame proof finishes. Unwinding assertions on.                                   |
| `applink_ws`                            | Rework                | Inductive step over the decoder state instead of unrolling 300 bytes.                                              |
| `remote_cmd`                            | Keep, extend          | Add the two-round replay job. Record a result.                                                                     |
| `settings_decode`                       | Keep, record          | No recorded result. Bound of 8 B is thin for strings.                                                              |

## 5. Beyond the current concept

Ordered by value for effort.

1. **Replay model traces against the real code.** Every counterexample and a sample of random model walks become
   host tests in the native Unity environments. The lock hook of the byte ring already allows forced interleavings.
   This closes the gap between model and code and turns each finding into a regression test without a solver.
2. **Check real runs against the model rules.** The serial allocation trace (`src/instrument_heaptrace.cpp`) and the
   INSTRUMENT logs already emit the events. A small monitor script checks the heap rules H1 to H8 and the ring
   invariants on bench captures. This replaces `heap.tla` with evidence from hardware.
3. **Turn assumptions into asserts.** INSTRUMENT builds assert the task identity at each "single writer" site and
   count violations. The assumptions of section 3.4 then fail on the bench instead of silently voiding a proof.
4. **Drift check without a solver.** A manifest maps each model and job to its source functions with a content
   hash. Stage 2 of the host gate fails when a covered function changed and the model was not re-reviewed. Cheap,
   runs always, and makes the opt-in solver run honest.
5. **Concurrency checks on the C itself.** ESBMC can explore thread interleavings with a context bound. The small
   lock-free cores (`ext_queue_core.h`, the byte ring, an extracted flag sequence) can be checked as code, with no
   hand translation. Treat as an experiment: scaling is unknown.
6. **One harness, two engines.** The same entry function drives the solver at a small bound and a fuzzer at full
   length. Where the solver runs out of memory the fuzzer still covers 255 bytes. `native_aprs_fuzz` is the start.
7. **Power loss and update model.** One TLA+ model of the update sequence with a reset at every step: staged image,
   attempt counter, safeboot, settings save. Property: the node always boots a valid image and keeps its settings.
   This addresses the worst failure class, which nothing covers today.
8. **Settings round trip.** A bounded proof that decode after encode returns the same settings for every field
   type, and that an older structure version migrates without reading outside the stored image.
9. **Mesh level.** A multi-node model of relay, dedup and ACK: no frame circulates forever, a retry ends. All
   current models stop at the edge of one node.
10. **Stack bounds.** Static stack usage per task from the compiler output, checked against the task stack sizes.
    The NimBLE review found a 512 B buffer on a 3072 B stack by reading. This can be mechanical.

## 6. How to run the campaign

Six waves. Solver runs happen twice, each on an explicit operator request. Everything else is writing and host tests.
Each wave ends with the host gate `tools/regression.sh --stage 1,2` and a commit on `fork-dev`.

| Wave | Content                                                                                                                                                                                     | Solver | Done when                                                                        |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | -------------------------------------------------------------------------------- |
| 0    | Repair the instruments: runner defects of 3.6, pinned versions, README for both directories, a committed results summary, rename stale configurations, drift check (idea 4)                 | no     | Runner self-tests pass on canned tool output. Drift check is in stage 2.         |
| 1    | Repair the evidence: remove or replace the tautologies of 3.3, rework `netconsole.tla`, decide and apply the fate of both heap models, real code mutations for the bounded jobs             | no     | Every remaining property has a written reason why it can fail.                   |
| 2    | Make the bounded proofs finish: inductive step for `alWsFeed`, split `decodeAPRS`, unwinding assertions on every pass job, replay job, memory budget per job, a quick tier and a heavy tier | no     | Each job has a stated budget below 2 GB or sits in the heavy tier.               |
| R1   | First full run of both tracks                                                                                                                                                               | yes    | Results summary committed. Each red result is a finding or a fixed job.          |
| 3    | Tie models to code: trace replay tests (idea 1), bench monitors (idea 2), assumption asserts (idea 3)                                                                                       | no     | Each kept model has at least one host test generated from it.                    |
| 4    | Extend coverage in value order: parser seams, power loss and update model, settings round trip, ranked concurrency findings                                                                 | no     | Operator picks the list after R1.                                                |
| R2   | Final run, bench session for the asserts and monitors                                                                                                                                       | yes    | One document states per release what is proven, to which bound, and what is not. |

- **Parallel work:** waves 0 to 2 split cleanly by directory. One writer per model or job group, the orchestrator
  owns runners, READMEs and `tools/regression.sh`. Writers edit specs and jobs and do not execute them.
- **Order matters:** wave 0 before any run, because a broken runner voids its own verdict. Wave 1 before wave 4,
  because new models copy the patterns of the old ones.
- **Fixes found on the way** follow the existing rule: recorded as an expected-red job first, fixed in a later wave
  with a regression test.

Decisions, each with the consequence and a recommendation:

1. **Where the solvers run.**
   - On the Mac, on request: no setup, but ESBMC exhausted the 24 GB machine twice and macOS has no hard memory
     limit for a process.
   - In a Linux CI job: needs a workflow and a container with pinned tools. A hard memory limit works there, the Mac
     stays free, and a nightly or manual trigger costs no attention.
   - Recommendation was CI with a manual trigger. Operator decision 2026-11-23: local only. The per-job memory
     budget of the runner is the guard.
2. **`heap.tla`.** Retire it, or keep it as documentation of intent. Recommendation: retire, move the eight rules
   into the bench monitor of wave 3.
3. **`heap_frag.tla`.** Re-model the reserved buffer, or freeze it as the record of why the old gate failed.
   Recommendation: freeze now, re-model only if OTA-LOWHEAP-01 needs another design decision.
4. **Traces in git.** Keep 60 files that change on every run, or keep one results summary and regenerate traces on
   demand. Recommendation: summary only.
5. **Scope of wave 4.** Recommendation for the first three items: power loss and update model, the `decodeAPRS`
   call chain from LoRa receive, the BLE host task against the loop (F2-8).

## 7. Implemented after the review (2026-11-23)

Nothing below was run with a solver or with Java. The TLA+ models were edited without a parser.

- **Wave 0, instruments:**
  - `run_tlc.sh` requires the name of the expected violated property. Any other violation is unexpected.
  - `run_cbmc.sh`: table separator fixed, tiers, a memory budget per job, Linux memory reading, results files.
  - Tool versions pinned in `specs/tools.lock`. A missing Java is a tool error.
  - `specs/selftest.sh` in stage 2 of the host gate: model lint, drift check, 29 verdict checks on canned output.
  - `specs/drift.py` with `specs/manifest.json`: 5 active models, 30 source functions and files hashed.
  - Trace files removed from git and ignored. READMEs for `specs/`, `specs/tla/`, `specs/cbmc/`.
  - No CI job. Operator 2026-11-23: formal runs stay local on the Mac, which is stronger than a hosted runner.
- **Wave 1, evidence:**
  - Tautologies removed or replaced, see `specs/tla/README.md`.
  - `netconsole.tla` rewritten with one process per handshake task. It predicts three defects in today's slot
    takeover (BACKLOG `FORMAL-NETCON-MARK`).
  - `rxflags.tla`: new invariants `Listening` and `TxArmed`, timeout restart corrected.
  - `heap.tla` retired, `heap_frag.tla` frozen, both in `specs/tla/archive/heap/`.
  - Three real code mutations for the bounded track.
- **Wave 2, bounded proofs:**
  - Unwinding assertions back on in every pass job. The runner refuses a pass job without them.
  - Out-of-memory jobs moved to the heavy tier. `aprs/pass_40` proves `decodeAPRS` up to 40 bytes in the quick tier.
  - AppLink cap edge at small scope with the cap reduced to 6. Replay job for remote management.
- **Deviations from the plan:**
  - `decodeAPRS` is not split. That needs a change in `src/` and solver feedback. The full-frame job stays heavy.
  - `alWsFeed` got small-scope jobs, not an inductive step proof. An invariant written without a solver would
    most likely not be inductive at the first run.
  - `heap_frag.tla` tautologies were not repaired, the model is frozen.
  - The retry path of the TX ring and the reset paths of the byte ring are still not modelled.
- **Open:** first run (BACKLOG `FORMAL01-RUN`), link to the code (`FORMAL-LINK`), new coverage (`FORMAL-EXT`).

## What this review did not check

- **No model or job was executed.** All statements on results come from the two run logs of 2026-11-23. Statements
  on tautologies come from reading the specs.
- **The assumptions of section 3.4 are not refuted.** They are unverified. I did not search every caller of the TX
  ring or every access to a byte ring.
- **Harness stubs were not audited.** The Arduino `String` stub and the `strtod` and HMAC models can hide or invent
  behaviour. I read the HMAC model only.
- **Line anchors were spot-checked,** not swept. `txring` anchors match. Heap anchors do not.
- **The library reviews** of NimBLE, lwIP and mbedTLS in the campaign document are review work, not formal checks.
  They are outside this review.
- **Idea 5 is untested.** Whether ESBMC scales to the concurrent cores on this code is an assumption.

<details>
<summary>Run record used for this review</summary>

- `tools/bench/runs/regression-20261123-155623/stage2-tla.log`: 113 results, all as expected. Largest state space
  `txring_nrf52_big` with 510585 states in 75 s. Smallest `netconsole_today_slotlockout` with 15 states.
- `tools/bench/runs/regression-20261123-145548/stage2-tla.log`: every configuration failed with exit 127, Java not
  found.
- `tools/bench/runs/regression-20261123-145548/stage2-cbmc.log`: 22 results. As expected: 5 pass, 12 expected
  failures. Out of memory: `applink_ws/pass_big_frag`, `applink_ws/pass_big_single`,
  `applink_ws/witness_big_single`, `aprs/pass`, `aprs/pos_pass`.
- Configurations unrun since the last TLA+ run: `bytefifo_fix_udp_dup`, `bytefifo_fix_udp_loss`,
  `netconsole_fix_slot`, `netconsole_mut_preemptactive`, and the renamed `*_was_*` files.
- Jobs written and unrun: `aprs/enc_pass_widepath`, `aprs/enc_mut_widepath`, `fw_assets/pass_nulurl`,
  `fw_assets/mut_nulurl`.

</details>
