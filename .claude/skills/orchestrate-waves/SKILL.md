---
name: orchestrate-waves
description: Safe multi-agent dispatch mechanics — file-ownership mapping, disjoint-file parallelism, shared-resource ownership, model/effort tiering, brief hygiene, gate-after-wave, advisor gate before commit, per-wave campaign state, shared-tree defense, resume-don't-respawn. Use before launching 2+ writer subagents, when a task decomposes into 3+ delegable subtasks, or when running a multi-wave campaign. Trigger on "parallelize this", "dispatch subagents for this", "run this as waves", "break this into parallel tasks". Not needed for a single read-only Explore agent.
---

# Orchestrate Waves

One failure mode drives this whole skill: concurrent subagents writing to overlapping files clobber
each other and emit phantom lint/typecheck errors that burn a triage cycle. Observed across 10+
repos and stacks; the discipline below is what reliably prevented it.

Ownership is not only about files. Once the file sets are disjoint, the remaining collisions happen
in the shared build cache, the shared test runner, and the one device or port — model those the
same way (§1.1) or the wave produces failures nobody's edit caused.

## 0. Plan for approval

Unless the user already said "just implement", present the plan before dispatching anything. It
always carries: the waves, the file-ownership table, model + effort per agent (§2 table), an
estimated cost and wall-clock per wave, and the open questions ("ask me anything"). The user asks
for this every time; making it standard saves a round trip.

## 1. Map file ownership, then classify

Before spawning anything, list every file each planned task will touch.

| Task                                                   | Dispatch                                                                        |
| ------------------------------------------------------ | ------------------------------------------------------------------------------- |
| Read-only (`Explore`, `scout`, audit, finder)          | Unlimited parallelism, always — never conflicts                                 |
| Writer, file set disjoint from every concurrent writer | Parallel                                                                        |
| Writer sharing a file with a concurrent writer         | Carve out the hotspot (below) — else serialize (run, finish, commit, then next) |

One shared file is enough to corrupt a wave; "mostly disjoint" is not disjoint. If unsure whether
two briefs overlap, grep the module/component name across both before deciding — don't guess.

**Dispatch a wave as one message with multiple Agent calls.** Separate messages run sequentially
and silently cost you the parallelism you just planned.

**Hotspot carve-out** turns most serialize cases into parallel ones: when the only overlap is an
integration file (registry, router, `__init__.py`, exports, config), assign that file to _no_
agent. Agents write their disjoint modules; the orchestrator applies the few-line integration
edits itself at the gate. Zero isolation cost, nothing to clobber.

**No agent-created worktrees, no `isolation: "worktree"`.** Deep overlap that a carve-out can't fix
means serialize or re-slice the tasks (an agent-made worktree off stale `main` has already caused a
real incident). If a worktree is genuinely required — a parallel PR branch — the orchestrator
creates it: pin it to the exact SHA (`git worktree add -b <x> /abs/path <HEAD-SHA>`; auto-created
worktrees have resolved to an upstream merge-base ~40 commits stale), copy in any gitignored
tooling the agent needs (`.claude/` is absent in a fresh worktree), and copy edits to those files
back by hand afterwards.

**Size briefs evenly.** The wave gate waits for the slowest agent, so wall-clock per wave equals
the longest brief — split anything whose file count or change surface is far above the others'.
Target ≤ ~25 min per implementer brief: the p90 over two weeks was 27 min, the worst 110 min and
$38 — every outlier was a brief that should have been two.

### 1.1 Shared resources are ownership too

Disjoint source files do not make two agents independent. Before dispatch, list what the wave's
verification commands touch besides source — the build tree, the test runner, any device, port,
license or rate-limited API they invoke — and give each slot to at most one agent, or keep it for
the gate:

| Shared resource                                                                   | Rule                                                                                                                                 |
| --------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| Incremental build tree (`.pio/build/<env>`, `target/`, a bundler cache)           | One build target per writer. Concurrent builds of the same target corrupt archive members and emit linker errors in untouched files. |
| Test runner sharing one env, DB, or port (`pio test -e native`, a single test DB) | Writers run only their own suite (`-f <own suite>`, `-k`, one spec file). The whole-env run belongs to the gate.                     |
| Physical device, serial port, single-client console, fixed localhost port         | Exclusive and serialized: a gate step, or a single-agent wave with no concurrent writers — never inside a multi-writer wave.         |

**When the scarce resource runs out, it caps the wave, not the rule.** More writers than build
targets or test slots means: split the wave, or give the excess writers a verification that needs
no slot (compile-only, a host-level unit test) and move their env run to the gate. Never two
writers on one slot.

Put the consequence in the brief as well: **a build or test failure in a file outside your
exclusive set is a shared-resource race until proven otherwise** — look for a concurrent build
(`pgrep -fl <builder>`, a lock file), clean that target, rebuild once, and report it; never "fix"
your own correct code to chase it.

## 2. Brief every subagent — restate this list every time

Never rely on a subagent rediscovering these from project memory or a prior session.

- **Exclusive file set**, stated explicitly — never touch anything outside it. Lint/type/CI config
  files are read-only unless the brief is specifically about them.
- **Never create, stub, or delete a file at a path another agent owns.** If a sibling's header or
  module is missing, report that and stop — verifying the missing dependency is the deliverable,
  not working around it. One agent stubbed a sibling's unwritten header, built against it, then
  `rm`'d it to clean up, destroying the real file that had landed in between: both file sets were
  correct, and the damage was at a third path neither owned.
- **No git commands** (add/commit/push/checkout/stash/restore/reset) and no self-created worktrees —
  the orchestrator owns all git. A stray `git restore` on a shared tree has wiped two agents'
  uncommitted work in one incident.
- **No whole-repo formatters** — scope formatting to touched files, or defer to a cleanup wave.
- **Name every irreversible or outward-facing action that is banned** — transmitting on a live
  network, publishing, pushing, sending mail, writing to production. Scope-of-files is not
  scope-of-effects, and a wave that drives real hardware or a live service can reach the outside
  world without editing a single file it doesn't own.
- **Out-of-scope bugs are still in scope** — fix if trivial, escalate explicitly if not; never
  silently patch, never silently ignore.
- **A bug fix ships with a regression test** that fails before the fix and passes after — name it
  in the brief, and require the before/after output in the report. The "before" is a real run
  against the unfixed code (write and run the test before the edit, or against a copy of the old
  file — no git stash), with its failing output pasted. No before-run means the item is reported as **unverified**, not fixed —
  and a test that still passes with the fix reverted proves nothing. In 24h of sessions
  (2026-10-12) three implementers reported "fixed" on tests that had no before-run or passed
  without the fix; all disclosed it, none had proven it.
- **Explicit verification commands scoped to the touched files** (targeted lint/typecheck, the
  relevant test file) to run before reporting done — the full suite runs once, at the gate, by the
  orchestrator. Plus an integrity check on anything it must _not_ have changed (for example, a
  `grep -c` on a pinned config rule, with the expected count stated).
- **Prove the change is in the artifact, not just that the build was green.** Ask at dispatch
  whether the touched code sits behind a feature flag, `#if` guard, plugin registry, or anything
  dead-code elimination can drop — if yes, exit code 0 proves nothing: a guard placed above the
  config include once compiled an entire file to nothing and passed the gate. Require a positive
  existence check — a string/symbol scan of the built artifact, a marker count asserted against an
  expected number, or a log line observed at runtime.
- **Reference paths, symbols, and line ranges — never paste file bodies.** Orchestrator output
  tokens cost ~5x its input and ~10x a Sonnet read; the agent reading the file itself is far
  cheaper. Do spend a sentence on conclusions that save the agent twenty exploratory calls ("the
  bug is in `handler.py:validate()`; the frontend sends camelCase").
- **Give exact signatures for the APIs the brief depends on, and compile before testing.** List the
  signatures the agent must call or implement, one line each, test helpers included; these are not
  file bodies. Add what the call needs to resolve: in Rust the `use` line for a trait and any Cargo
  feature; in C/C++ the header and any `#ifdef` guard around the declaration. A `scout` collects
  existing ones (§3.1), and the plan fixes a sibling's unwritten API before dispatch. Name a
  compile-only check of the agent's own crate or target to run before any test, covering test code
  when the brief writes tests. Rust: `cargo check -p <crate>`, plus `--tests`, because plain
  `cargo check` skips test code. PlatformIO: `pio test -e <native_env> --without-testing`, native
  envs only (a bare `pio test` flashes attached boards); its `0 test cases` and `[PASSED]` only mean
  it compiled. A one-target check does not replace the wave gate's build of every target. This
  targets API-guessing errors, and most compile errors were in test code (72 of 105 with a known
  target). Expected saving: under 1% of subagent turns. All compile fix
  loops together cost 2.6% of subagent turns and 1.7% of their output tokens across about 400
  subagent transcripts (measured 2026-11-07). Any saving on lookup calls was not measured.
- **Locate before reading: `grep -n`, then a line range.** No unbounded `Read` or `cat` on a file
  over ~400 lines; read `sed -n 'a,bp'` / `Read` with `offset`+`limit` around the hit. Whole-file
  views over 3k tokens were 37% of the file-view context re-sent across 24h of sessions
  (2026-10-10), and two-thirds of it sat in subagents, which re-send it on every later turn.
  Exception: a brief whose job is condensing that file — then read it in chunks.
- **No `sleep` polling.** Long commands run with `run_in_background: true` and wait for the
  completion notification; a wait-for-condition is one `Monitor` until-loop (§3).
- **Require a terse structured report, at most 15 lines**: files touched, gate result (the
  verification commands and their pass/fail line, not their full output), deviations from the
  brief, open questions. No narrative — every report stays in the orchestrator's context for the
  rest of the session and is re-read on every later turn.

Model tier and effort are part of the dispatch decision. Use the named agents in
`.claude/agents/` (prices per MTok in/out, as of 2026-10):

| Work                                                         | Dispatch as                | Model and effort                                                     |
| ------------------------------------------------------------ | -------------------------- | -------------------------------------------------------------------- |
| Read-only recon: ownership maps, callers, greps, `ls` checks | `scout`                    | Haiku 5.5 ($0.10/$0.50), low (frontmatter default)                   |
| Read-and-condense: logs, transcripts, test output, big files | `scout`                    | Haiku 5.5, medium (pass `effort`)                                    |
| Implementation, every first dispatch                         | `implementer`              | Sonnet 5.5 ($2/$10), medium (frontmatter default)                    |
| Analysis writers: RCA, triage verdicts, evidence write-ups   | `implementer`              | Sonnet 5.5, high (pass `effort`) + evidence checklist in the brief   |
| Rework after an advisor rework list (§4 step 6)              | `implementer`, fresh agent | Sonnet 5.5, high (pass `effort`) — never an Opus override            |
| Review finders (fan-out)                                     | `general-purpose` + model  | Sonnet 5.5, medium                                                   |
| Review verifiers                                             | `general-purpose` + model  | Opus 5.5 ($4/$20), medium                                            |
| Advisor gate (§4 step 6)                                     | `general-purpose` + model  | Opus 5.5, medium — fresh agent, never the orchestrator's own context |
| Planning, reading wave diffs                                 | orchestrator (yourself)    | Opus 5.5, medium (session default)                                   |
| Hard debugging, a wave's second rework round                 | orchestrator (yourself)    | Opus 5.5, high (`/effort high`), back to medium afterwards           |

The two "orchestrator (yourself)" rows assume the session runs on Opus. **On a Sonnet
orchestrator they read "dispatch a fresh Opus agent"** — see §2.1; they are not a contradiction
with the rest of the table.

- **Every Agent call states model and effort explicitly.** `general-purpose` without a `model`
  inherits the session — over two weeks that ran finders on Opus/Fable at `max`. Frontmatter
  effort beats the user's wish unless the call passes `effort`: when the user names a model or
  effort ("Sonnet 5.5 medium"), pass it on every call of the wave. One campaign ignored exactly
  that request and ran every implementer at high.
- **Medium is the implementer's only first-dispatch effort for code.** There is no "this brief is
  hard → high" tier: a brief that feels too hard for Sonnet medium gets split, or you do it
  yourself. Never override the implementer to Opus (three such runs cost $42 and still took
  38–97 min).
- **Analysis writers are the exception: high on first dispatch.** Briefs whose output is a
  judgement over evidence (root-cause analyses, triage verdicts, "is this fixed upstream") failed
  at Sonnet medium: the Opus gate sent back 17 of 24 RCAs (2026-10-11) for wrong mechanisms,
  credited fixes that had been reverted, and wrong affected platforms; one Sonnet-high round then
  cleared 11 of 12. Code briefs had no such rate. Put the evidence checklist in the brief: every
  cited commit is still in the target branch and not reverted (`git branch --contains`,
  `git log --grep=Revert`), every tag/version named exists (`git tag -l`), every platform claim
  is checked per platform, and every "already fixed" claim names the commit and branch.
- **High is earned by a rework verdict.** When the advisor returns a rework list, dispatch a
  _fresh_ `implementer` with `effort: "high"`, briefed with the same exclusive file set, the
  advisor's rework items (as you verified them) and the original acceptance criteria. A resumed
  agent (`SendMessage`) keeps the effort it was spawned with, so rework is the one case where a
  new dispatch beats resuming. If that rework round comes back with a second rework list, take
  it yourself at `/effort high` instead of a third dispatch.
- **Tiny rework stays with you.** When the verified rework list is a few lines in total (about 10
  or fewer, no new test file), apply it yourself with its fail-before/pass-after test instead of
  dispatching: a fresh agent's cold read costs more than the fix. Say so in the wave report.
  Anything larger goes to the fresh high implementer above.
- **No Fable in the dispatch path.** Advisor, verifiers and finder bumps run on Opus 5.5 at
  medium. Opus reviewing a Sonnet implementer's diff is already the independent, higher-tier
  look the advisor gate needs.
- **Haiku 5.5 is ~20x cheaper than Sonnet 5.5 — default every read-only job to it**, especially
  anything whose raw output would otherwise land in the orchestrator's context (§3.1).
- **Never give Haiku a writer brief** (not yet re-tested on 5.5): small models under "make the
  linter pass" pressure weaken the config and report success anyway. Haiku is safe only where it
  writes nothing and its output is checkable at a glance. Mechanical repo-wide fixes → script
  them inline.

### 2.1 Sonnet orchestrator

When the session itself runs on Sonnet (to halve the orchestrator's cache-read bill), the Opus
judgment moves out of the session into fresh Opus agents. Everything else in this skill applies
unchanged.

1. **Opus plans, Sonnet runs the waves.** Wave 0 is a fresh `general-purpose` agent,
   `model: "opus"`, `effort: "medium"`, that writes the campaign doc: decomposition into waves,
   the file-ownership table, acceptance criteria per wave, model and effort per agent. Sonnet
   executes from that doc and does not re-plan; a plan that no longer fits goes back to a fresh
   Opus planner with the doc and the reason.
2. **Fixed escalation triggers — not "if this feels hard".** Dispatch a fresh Opus
   `general-purpose` agent at medium, briefed with the evidence and the question, when:
   - a gate is red after one fix attempt, or
   - the advisor returns a second rework list for the same wave, or
   - the next step needs a root-cause judgment (why it fails, which commit caused it, whether it
     is already fixed upstream), or
   - a subagent's claim contradicts what the diff, the gate output or the artifact shows.

   Never escalate by switching the session's model or effort: that re-writes the whole context
   cache. A session that went a whole campaign without one escalation is a finding for the
   audit, not proof that nothing was hard.

3. **Advisor rework items go to the implementer as is.** An Opus orchestrator verifies the
   advisor's items before the rework dispatch (§4 step 6); a Sonnet orchestrator does not judge
   them. It passes the advisor's list unchanged to the fresh high implementer, and the next
   advisor pass on the rework diff checks the result. An item the implementer disputes with
   evidence goes back to the advisor, not to Sonnet's own judgment.
4. **Reading wave diffs:** Sonnet does the mechanical checks of §4 and §5 itself (`ls`, gate,
   `git diff --stat`, the spot-check of the sharpest claim). Judging whether a diff is _correct_
   is the advisor's job, which already runs on Opus for every behavioral wave.

## 3. Run the wave

- Harness-tracked agents re-invoke you when they finish. Don't busy-poll, don't read raw
  transcripts. To block until a condition holds use `Monitor`; to inspect one finished agent's
  output use `TaskOutput`; to end one use `TaskStop`.
- **No `sleep` polling — yours or an agent's.** A long command (build, gate, flash, soak) runs
  with `run_in_background: true`, and its completion notification re-invokes you; to wait on a
  condition (a marker in a log, a file appearing, a process ending) use one `Monitor` with an
  until-loop that exits on the condition. Never `sleep N; check` in a loop of turns: 1,237 sleep
  calls cost 18 h of wall-clock over four weeks (2026-10-10), and every check is one more full
  context re-read. Put the same rule in every brief that runs anything longer than a minute.
- When a wave lands, post a one-line interim status (what finished, what's outstanding) without
  being asked.
- **Fill the barrier with read-ahead.** While wave N's writers run, dispatch `scout` agents for
  wave N+1: ownership mapping, code recon, log checks. Read-only work never conflicts with
  anything, so the dead time between waves is free parallelism.
- **Close what you open.** A barrier-filling watcher built on `tail -F | grep` never exits on its
  own, and they stack up unnoticed across a campaign. End the pipeline on the run's final marker
  (`sed '/=== overall/q'`) or TaskStop it the moment its verdict arrives, and never state a
  background task's state without checking it (`pgrep`, the task list, the tail of its output) —
  an empty output file is not evidence that it died.
- **Size fan-outs to the usage limits.** Never fan out on the top model or at `max`/`xhigh`
  effort. Dispatch at most ~8 agents per message and launch the next batch when one lands: one
  campaign ran 21 Fable `max` finders in parallel; all 21 died on the Fable limit and the session
  limit, and their spend bought nothing. A limit hit should cost one batch, not the whole fan-out.

### 3.1 Keep the orchestrator's context small

The orchestrator's own context is the largest single cost: over two weeks ~78% of its spend was
cache reads, at a median of 240k and a p90 of 550k tokens re-read on every turn. Every byte you
pull in is paid again on each later turn.

- **Send lookups to a `scout`: the main thread receives answers, not raw `sed` and `grep` output.**
  "Where is X set", "who calls Y", "which files does Z touch" go out as a question and come back as
  a conclusion with `file:line` references. Keep for yourself only the lines you are about to judge
  or edit and checks whose target you already know, including the §5 spot-checks. Estimated saving:
  about 8–10% (the owner's estimate, not measured).
- Bulky reads go to a `scout` that returns a terse summary: logs, transcripts, full test output,
  large files, `--info` dumps.
- The same locate-then-range rule from §2 applies to the reads you keep: `grep -n` first, then the
  range.
- Pipe gate and build output through `tail`, `grep -c` or a summary line before it reaches you.
- **Batch independent calls into one message.** Every turn re-reads the whole context, so the
  number of turns is the cost multiplier, not the number of calls. Scout dispatches and the reads
  you keep (line ranges, known-target greps, `ls` checks) that don't depend on each other go out
  together in one message — or as one Bash command chaining the reads. Over four weeks only 5% of
  orchestrator turns carried more than one call, while 29% were pure read-only recon ($614 at
  ~200k context each, 2026-10-10). Before sending a single read, ask what else you will need next
  and send it too.
- **One script for routine checks.** The recurring gate-adjacent checks (gate run, `git status`,
  `git diff --stat`, the `ls` of expected files) go out as one Bash command or one small script
  in the scratchpad, not as five turns.
- **No status-check turns.** Agents and background shells re-invoke you on completion; a turn
  spent asking "is it done yet" is a full context re-read that buys nothing. Between dispatch and
  notification, either do read-ahead (§3) or end the turn.
- **Compact at every wave boundary, without a person.** The orchestrator cannot run `/compact`
  itself; the `wave-compact` mod does it on a safe word. Once a wave is committed and its campaign
  docs are updated (§6), end the wave report with this as its **last line, alone**:

  `<<WAVE-BOUNDARY-COMPACT resume=<path of the resume doc>>>`

  and end the turn. The mod compacts between turns (instructions: keep the resume path, standing
  user instructions, the next wave, open questions, running agents; drop tool output and
  reports), then submits a prompt telling you to re-read the resume doc and continue. So: docs
  first, marker last, nothing after it. Only the exact last line of a main-session answer fires;
  quoting the marker in prose or from a subagent does nothing. Never emit it mid-wave, while
  agents or background gates are still running, or before the commit. As a backstop for
  sessions without the mod, `autoCompactWindow` in `settings.json` (tokens, 100000–1000000) set
  to about 200000 caps the context too, though at a token count rather than a boundary. Turns
  above 400k context cost $1,053 over four weeks (2026-10-10), and one undelegated session ran
  421 turns over 13 h at up to 512k context for $53 (2026-10-12) — about 40% of that day's
  orchestrated spend. Undelegated work has the same boundaries (PR pushed, release published,
  bench run reported): update the docs and emit the marker at each one.

## 4. Gate after the wave, never during it

Run the gate only once **all** agents in the wave have returned, in this order:

1. **`ls` every file the wave was supposed to produce**, before reading any report. A file that
   vanished (§2, third-path rule) is invisible in every agent's self-report — the agent that
   deleted it verified the deletion and called that a pass.
2. **Apply the orchestrator-owned hotspot edits** (the carved-out integration files from §1).
3. **Run the full local gate** — lint, typecheck, format-check, and the real test suite, plus the
   whole-env test runs no writer was allowed to run. This is the only place the full suite runs;
   agent-side verification was scoped to touched files, so the gate is the first whole-tree check,
   not a redundant one.
4. **Rebuild clean and sequentially** when the wave shared a build target, or when any agent
   reported a size/RAM/perf figure — those figures from a shared incremental build are not
   trustworthy (concurrent builds of one target have corrupted archive members and broken the
   link in untouched files). Read nRF52 RAM from the static figure: its linker "RAM" line always
   shows about 99% because it counts the heap reservation.
   Skip it for a wave that touched no shared build target and claims no such numbers.
5. **Hardware or live-system checks**, serialized — here, unless the wave was itself the
   single-agent hardware wave of §1.1.
6. **Advisor pass before commit for any wave that changes behavior.** Dispatch one independent
   reviewer — a fresh `general-purpose` agent, `model: "opus"`, `effort: "medium"` — against the
   wave's diff and its acceptance criteria: `fable-review` in its "advisor pass on an existing
   diff/PR/wave" mode, which skips the finder fan-out entirely. It returns APPROVED or a concrete
   rework list; verify its claims yourself before acting on them (on a Sonnet orchestrator: pass
   them on unverified, §2.1 item 3), then send the verified items to
   a fresh implementer at high (§2 table), re-check, re-run the advisor on the rework diff, and
   hand off to §5. Docs-only, test-only and mechanical waves may skip this —
   say so in the wave report rather than skipping silently. It earns its cost: over two weeks 32
   of 44 decisive advisor verdicts sent the wave back for rework. To save wall-clock, start the
   advisor on an agent's file set as soon as that agent returns and its own files check out
   (step 1 plus its scoped verification), instead of waiting for the whole wave; the whole-wave
   diff still gets one final look.

An error surfacing mid-wave from a file a sibling hasn't finished touching is not a regression —
treat it as unconfirmed until every agent has returned, then re-run clean.

## 5. Verify, then commit — orchestrator only

This is the single commit point for a wave; §4 ends by handing off here. Global CLAUDE.md already
bans trusting a self-report and `git add -A`. At wave scale that means:

- Read `git diff` / `git show` on the exact files the agent touched — not its narrative.
- Re-run the verification suite yourself, and spot-check its sharpest behavioral claim directly
  (curl the endpoint, grep the cited line, run that one test).
- Re-check `git status` / `git log` immediately before **each** commit, not once at the start.
- Commit per wave, explicit paths, diff actually viewed before staging — a verification you said
  you'd do must actually happen, especially for anything headed to production.

This has caught a wire-protocol field mismatch that would have shipped and contradictory claims
between two agents' summaries, and correctly separated a real regression from an in-flight sibling's
transient fallout.

## 6. Write the campaign state down, per wave

A multi-wave campaign outlives the orchestrator's context — one recent campaign compacted four
times mid-flight. The wave map, the ownership table, what closed and what is still open survive a
compaction only if they are on disk.

- After **every** wave, before dispatching the next one: update the campaign's own status
  documents — the plan, the backlog, whichever chart the operator reads. Not at session end.
- Grep the finding or item ID across the docs before updating: a status tracked in two places (an
  inline status box _and_ a summary table) goes stale in the one you didn't think of.
- Treat those documents as the resume point: a fresh context should be able to reconstruct the
  wave plan from them without re-reading the transcript. That is what makes compacting at each wave
  boundary (§3.1) safe — if the docs can't carry the next wave, fix the docs.
- **Keep the current state short and archive finished waves.** The next wave, the open questions
  and the resume steps live in their own short file, about one screen. When a wave closes, move its
  details to an archive doc in the same update and leave one line: the outcome and a pointer. Look
  things up in the archive through a `scout` (§3.1). One campaign plan grew to 97 KB and was read
  53 times. Estimated saving: about 1–2% (the owner's estimate, not measured).

## Failure handling

- **Agent killed or self-blocked** → `git status` first to establish what it actually changed, clear
  the blocker, then resume _that_ agent via `SendMessage` with a recap of its brief and current
  state. Never spawn a replacement for resumable work — forking risks duplicate or conflicting
  output, and a respawn re-reads cold everything the original already holds warm in cache. This is
  also the recovery path when a sibling damaged an agent's work: message the victim with what
  happened and that it was not at fault. The one exception is an advisor rework list: that work
  is finished, not interrupted, and goes to a fresh implementer at high (§2).
- **HEAD moved unexpectedly** (commits appear/vanish, a rebase happened under you) → stop and ask
  the user; don't rebase, force-resolve, or guess which state is authoritative. Up to 7 concurrent
  sessions on one checkout have been observed, so when a shared tree is plausible, brief read agents
  as "AUDIT ONLY — do not edit/create/delete any file".

## Composes with

`fable-review` — a review campaign is these mechanics plus its finder fan-out and adversarial
verification; the advisor gate itself is §4 step 6 and applies to every behavioral wave, campaign
or not. The `scout` and `implementer` dispatch tiers are defined in `.claude/agents/`.
