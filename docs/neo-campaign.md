# The neo campaign: two fork branches, one upstream branch

Written 2026-09-19. Living document -- update it after every stage, not at the
end. The wave plan survives a context loss only if it is on disk.

## 1. What neo is

`upstream/dev` plus better code. Nothing else.

The fork's DRY-unification campaign (`dry-unification`, 182 commits, W1-W7 plus
C4d, ETH-02/03, H6-01, the toggle soaks) is replayed onto `upstream/dev` as a
curated series of about 15 thematic commits, so that a maintainer can read it as
a story instead of an archaeology site.

Long-term goal: a `dev-dk5en` branch on `icssw-org/MeshCom-Firmware` that
becomes the basis for MeshCom 5. Kurt OE1KBC has to agree to that branch first.
Nothing is pushed upstream until it is proven here.

### Explicitly NOT in neo

The DM store-and-forward stages 0-4 and the EXTUDP originator keys (item 225)
live on `fork-main` and stay there for now. neo promises "behaves like 4.35t,
only more stable"; untested new protocol behaviour works against that promise.
Operator decision 2026-09-19.

### The U-boat property

The firmware must be **indistinguishable on the air** from official 4.35t and
better only under load. That is a deliberate test strategy, not an oversight:
the fleet runs neo without knowing it, and any behavioural difference that shows
up in the field is a real finding.

Consequences:

- `SOURCE_VERSION` / `SOURCE_VERSION_SUB` / `SOURCE_VERSION_WEB_SUB` do not
  change. The on-air field is a hard 4+1 characters (`node_fwversion` is
  `char[8]`, always written with `%-4.4s%-1.1s`), and the BLE struct offset 1284
  is pinned by a `static_assert`.
- The release name `4.35t_20260919_neo` exists only in the git tag, the GitHub
  release, the asset filenames and the web-flasher manifest.
- `FLASH_VERSION` (`src/configuration_global.h:88`) gets the neo date. It is a
  compile-time literal that reaches the boot log (`esp32_main.cpp:824`,
  `nrf52_main.cpp:549`) **and the `--setlog` STAT line**
  (`loop_functions.cpp:3346` into the `flash` field of `setlog_lines.h:106`) --
  so serial, net console and web, but never the air. The U-boat property holds.
  `flashLayoutCompatible()` compares `FLASH_STRUCT_VERSION` instead, which must
  NOT move, on pain of wiping the fleet's settings. The bump belongs in the
  chapter that owns `configuration_global.h` (K02), not at the tip -- with
  fold-before-stage-3 a tip-only bump would leave the two branches disagreeing
  on the value for the whole series.
- `__DATE__` / `__TIME__` already reach `--info` and the web GUI, so a bench
  operator can always tell which image is running.

## 2. The three branches

```
upstream/dev
     |
     +--> fork-neo-test    ~15 commits, WITH test harness and instrumentation
     |                     the verification bench; this is where truth is
     |                     established
     |
     +--> fork-neo         the same ~15 commits, production only, no test
                           ballast and no instrumentation -- shaped like
                           upstream/dev is today
                                |
                                +--> icssw-org/dev-dk5en  (after Kurt agrees)
```

Both fork branches start from `upstream/dev`, **not** from each other. If
`fork-neo` were branched off `fork-neo-test` and then stripped, its history
would carry strip commits -- exactly the archaeology neo exists to remove.

`fork-neo` is what upstream sees: production ready, but hard to verify, in the
same sense that today's `upstream/dev` is hard to verify. `fork-neo-test` is
where that verifiability lives.

## 3. Stages

| Stage | What                                                                | Gate                                                                                                                |
| ----- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| 0     | Merge `upstream/dev` into `dry-unification` -- **done, `c09e22b8`** | 34/34 native envs, 8/8 board envs                                                                                   |
| 1     | `fork-neo-test` from `upstream/dev` -- **done**                     | 40/40 builds over 5 commits x 8 envs, regions measured at every commit, G1 empty                                    |
| 2     | `fork-neo-test` complete -- **done**                                | 34/34 host envs, 32/32 board envs, 2/2 safeboot, one instrumented build with 13 INSTR strings in the ELF            |
| 3     | `fork-neo` from `upstream/dev`, stripped -- **done**                | 38/40 (the two reds are upstream's baseline at K01), and the symbol sets match `fork-neo-test` on all eight targets |
| 4     | `fork-neo` complete -- **done**                                     | 32/32 board envs, 2/2 safeboot, instrumented build with the same 13 INSTR strings as `fork-neo-test`                |
| 5     | Release + web flasher                                               | differential run **passed**, 12 h soak on three nodes **passed**; release and flasher still open                    |
| 6     | Negotiate `dev-dk5en` with Kurt, push                               | --                                                                                                                  |

Stage 0 was mandatory before anything else: `dry-unification` branched at
`1cb2d9e6` and did not know the last 11 upstream commits. Projecting fork
content onto an `upstream/dev` base without that merge would have reverted
Kurt's work wordlessly -- the trap that already hit PR #1135.

The changelog (`docs/CHANGELOG-neo.md`) is written **before** stage 1, because
it defines the commit cut. It is the outline, not the post-hoc documentation.
Accept that stage 1 will rewrite parts of it: the build gate decides which
chapters can stand alone, and merging two chapters merges two sections.

One consequence of the fold-before-stage-3 policy, so it is not a surprise
later: folding rewrites the intermediate commits of `fork-neo-test`. The
per-commit builds of stage 1 then attest to commits that no longer exist. The
tip is unchanged, so the stage-2 evidence survives intact -- but the branch is
force-rewritten, and model B's forward merges start from the rewritten branch.

## 4. Construction: projection, not rebase

An interactive rebase is not an option: 741 commits against `upstream/dev`, core
files touched 30-64 times each, and upstream squash-merges our PRs, so identical
content appears to git as a conflict.

```
git checkout -b <branch> upstream/dev
# per theme:  git checkout --no-overlay <content tree> -- <paths>
#             commit with the chapter's changelog text
# closing:    git diff <content tree> <branch> -- <filter set>   must be empty
```

`--no-overlay` is not optional. `git checkout <tree> -- <paths>` runs in overlay
mode by default and **never removes files absent from the source tree**
(reproduced on git 2.55.0). 77 of the 248 files in the filter set are deletions,
so without the flag chapters K01 and K10 would commit nothing at all and the
error would surface only at the closing check, after eighteen chapters.

The tree-identity check at the end makes the thematic split provably lossless on
`fork-neo-test`. On `fork-neo` it does not apply as stated -- see section 7.

Projection precondition: the content tree must already contain `upstream/dev` in
full. After `c09e22b8` it does (`git merge-base --is-ancestor upstream/dev
dry-unification` succeeds).

Scope of the filter set, measured on `c09e22b8`: **248 files** -- 48 added, 77
deleted (vendor fonts, dead platforms, 20 `lv_conf.h` copies), 122 modified, 1
renamed. `src/` 164, `variants/` 79, `lib/` 4, `config/` 1, `platformio.ini`.

The two `safeboot*.bin` are at repo root and therefore **outside** the filter
set, but every ESP32 `upload_command` flashes them (`platformio.ini:1082,1086`).
K15 changes `src/safeboot/`, so those bins must be rebuilt and committed as part
of K15 on both branches -- otherwise `fork-neo` ships new safeboot source with
upstream's stale binary, and a stale safeboot ships unnoticed.

## 5. What gets stripped for fork-neo

Two separate things, both removed -- and the inventory is larger than it looks.

**1. Host test harness.** `test/` and the 34 `[env:native*]` blocks in
`platformio.ini`. These land last on `fork-neo-test` (chapter K19) because a
native env's `build_src_filter` set is only complete at the tip; running them
after an intermediate chapter fails for reasons that have nothing to do with the
chapter. `platformio.ini` is therefore the one file split across two chapters:
K18 lands it without the native blocks, K19 appends them.

**2. The firmware instrumentation is NOT stripped.** Corrected 2026-09-19 after
checking the tree: `upstream/dev` already carries it. `src/instrument.h` is
byte-identical to ours, `src/instrument.cpp` differs by 18 lines, and seven
files include it upstream already. Our entire delta is those 18 lines plus four
additional includers (`extudp_functions.cpp`, `nrf52/nrf_eth.cpp`,
`esp32/gateway_service_esp32.cpp`, `nrf52/gateway_service_nrf52.cpp`).

Removing it from `fork-neo` would delete Kurt's own code and push the branch
further from upstream instead of closer -- the wordless removal this whole plan
exists to prevent. `INSTRUMENT_ENABLED` defaults to 0 and no env sets it, so it
costs a shipped image nothing.

**3. One unguarded bench marker.** `src/t-deck/tdeck_helpers.cpp:143` prints
`[KBL];set;%u` at preprocessor depth 0. It is the only genuinely fork-local
bench artefact in the production source, and the only hand-written line of the
strip.

Also out: `docs/` except `CHANGELOG-neo.md`, the **fork's additions to**
`tools/`, `.claude/`, `.prettierignore`, `release.md`, `release-notes.md`.

### What must NOT be stripped

- **`MC_DIAG` and `MC_INJECT_HOOKS` stay.** They are field diagnostics, not
  instrumentation (`configuration_global.h:190-215` explains the difference at
  length). K17's title naming both invites exactly the confusion that would lose
  them.
- **The field diagnostics stay**: `--udplog`, `--udpstat`, `--wifistat`,
  `--ethstat`, `--persiststat`. Verified: none of them is inside
  `#if INSTRUMENT_ENABLED` today -- they sit under plain `ESP32` / `NRF52_SERIES`
  guards or at depth 0, before the instrumentation block that runs
  `command_functions.cpp:4343-4803`. The boundary is implementable as written;
  it is the inventory that was wrong.
- **`lib/tinyxml2` stays.** It is not ballast. Upstream pulls tinyxml2 via a
  `lib_deps` URL that drags in `contrib/html5-printer.cpp` and its own `main()`;
  the linker resolves crt0's reference from it and pulls libstdc++ iostream and
  locale in with it. Vendoring it with a `srcFilter` (`95d6fbe6`, MEM-04/R3-04)
  bought `E22_XML-DevKitC` **+5768 B DRAM and +572 B IRAM**. Restoring the URL
  would put that env back at 3456 B of IRAM -- below the threshold agreed in
  section 6. It travels, and it earns a changelog chapter.

### tools/ is partly upstream's

`upstream/dev` already ships `tools/safeboot.py`, `tools/set_custom_name.py`,
`tools/ensure_safeboot.py` and others, and its own safeboot envs call two of
them. "tools/ is out" means the fork's additions only. The forced companions are
**three** scripts per safeboot env, not one (`platformio.ini:1119-1121,
1163-1165`): `ensure_tasmota_framework.py` (fork-added),
`set_custom_name.py` and `safeboot.py` (both upstream's already).

`.github/workflows/ci-build.yml` is fork-added and runs `pio test -e native`
plus `tools/resource_watch.py`. It cannot travel to `fork-neo` unchanged.

### The root-level delta

Decided 2026-09-19. `fork-neo` lives on `DK5EN/MeshCom-Firmware`, so these are
ours to set:

- **No automatic GitHub builds.** Releases are built on the operator's MacBook
  (about six minutes for the full sweep) and published to GitHub by hand -- full
  control over what ships, and the web flasher on gh-pages has its own concept.
  Actions are already disabled at repo level, which is what actually prevents a
  build; the fork-added `.github/workflows/ci-build.yml` is dropped because it
  needs `test/` and `tools/resource_watch.py`, and upstream's
  `meshcom-ci.yml` is left untouched so the branch carries no gratuitous delta
  into the handover.
- `.gitignore`, `.gitattributes`, `.github/ISSUE_TEMPLATE/*`: fork versions kept,
  they are harmless and useful.
- `README.md`, `CLAUDE.md`: decided at stage 3, when the branch gets its own
  framing text.

### The strip is a script, not handwork

Define it once as a checked-in, deterministic transformation and run it. That is
what makes section 7's G1 meaningful on `fork-neo`, and it is the difference
between a repeatable branch and one nobody can rebuild.

### How the strip is verified

The strip is now small enough to state exactly: `test/` goes, the 34
`[env:native*]` blocks in `platformio.ini` go, and one marker line goes. Nothing
else. No source rewrite, no guard sweep.

That restores the cheap gate the earlier draft had to give up. `test/` and the
native envs reach no board image -- no `#include` in `src/` points at them and
none of those envs is in `default_envs` -- so at the same chapter, `fork-neo` and
`fork-neo-test` must produce **the same eight firmwares**.

- **Symbol-set diff** (`nm --defined-only --size-sort`, `size -A`) between the
  two branches at the same chapter. A symbol in one and not the other names the
  difference directly. This is the load-bearing check.
- **Byte compare, if wanted**: `SOURCE_DATE_EPOCH` set and both branches built at
  the same absolute path (or `-ffile-prefix-map`), comparing `firmware.bin` with
  the `esp_app_desc` SHA field masked. `__DATE__`/`__TIME__` sit in the image at
  five sites, so this never works without those precautions.
- **String scan of the ELF** after the strip commit, to prove the marker is gone
  and nothing else went with it.

One thing the earlier draft got right for the wrong reason and which still
holds: add a `-DINSTRUMENT_ENABLED=1` build to the stage-2 gate. `INSTRUMENT_ENABLED`
is 0 by default (`src/instrument.h:34-35`) and no ini sets it, so without that
one build nothing ever compiles the instrumented path and it can rot unnoticed
on both branches. Note the spelling: `-DINSTRUMENT_ENABLED=1`, no space. Setting
`PLATFORMIO_BUILD_FLAGS` changes `project.checksum` and wipes `.pio/build`.

## 6. The gate set: 8 envs

Seven established targets plus `E22_XML-DevKitC`.

Measured on `c09e22b8`:

| env                    | chip     | Flash | `dram0_0_seg` free | `iram0_0_seg` free |
| ---------------------- | -------- | ----- | ------------------ | ------------------ |
| heltec_wifi_lora_32_V3 | ESP32-S3 | 42.5% | 171864 B           | 277124 B           |
| E22-DevKitC            | ESP32    | 45.8% | 21384 B            | 4972 B             |
| E22_XML-DevKitC        | ESP32    | --    | 17816 B            | **4028 B**         |
| ttgo_tbeam             | ESP32    | 46.3% | 21568 B            | 4972 B             |
| ttgo_tbeam_supreme     | ESP32-S3 | 43.5% | 168984 B           | 274580 B           |
| t_deck                 | ESP32-S3 | 17.3% | 142640 B           | 268304 B           |
| t_deck_plus            | ESP32-S3 | 17.3% | 142640 B           | 268304 B           |
| wiscore_rak4631        | nRF52840 | 84.7% | no segments        | no segments        |

Why exactly these eight:

- **Nine of the ten classic-ESP32 envs use byte-identical IRAM: 126100.** Not
  similar -- identical, because the IRAM content is framework and driver code
  placed the same way everywhere. One representative covers all nine, and
  `E22-DevKitC` already is that representative.
- `E22_XML-DevKitC` is the single outlier (tinyxml2 costs it 944 B IRAM and
  3568 B DRAM) and the only env already below the tool's 4096 B threshold. It is
  the env that will break first, not the RAK.
- The S3 boards have roughly ten times the headroom and are in the set for
  behaviour and flash coverage, not for region pressure.
- `wiscore_rak4631` is nRF52: no linker segments, its constraint is flash at
  84.7 % with 124 kB left.

### Measure regions, never the RAM line

`ttgo_tbeam` reports `RAM: 7.9%` and sits at 96.2 % IRAM at the same time. The
PlatformIO summary measures against the PSRAM-inclusive total and is blind to
the tight regions -- wrong by an order of magnitude. Every gate run calls

```
python3 tools/resource_watch.py dram --env <env> --map .pio/build/<env>/firmware.map
```

**Decided: `--min-headroom 4000` for `E22_XML-DevKitC`, 4096 for the rest.**
The env sits at 4028 B today and the campaign is not going to fix a pre-existing
constraint first. The rule the gate enforces is therefore "do not make it
worse", not "reach the default". Note that the 4028 exist only because
`lib/tinyxml2` is vendored (section 5); restoring upstream's `lib_deps` URL puts
this env at 3456 B and the threshold fails. The two decisions are coupled.

Known levers if headroom is ever needed: the T-Beam board JSON's PSRAM flags
cost 4.4 kB IRAM, the tinyxml2 example `main()` 5.8 kB DRAM.

**The nine-are-identical argument is a snapshot, not an invariant.** IRAM
content is board-independent today because `src/` has exactly two `IRAM_ATTR`
sites and no per-env flag places code in IRAM. A chapter that adds an ISR or an
`IRAM_ATTR` function under a board-specific `#ifdef`, or that changes how
`MC_DIAG` / `DISABLE_NET_CONSOLE` are handled (E22_XML only), would spread them
apart silently and the 8-env gate would not see it. Rather than gate all ten
classic envs every time, the gate carries a trigger:

> If a chapter's diff touches `IRAM_ATTR`, an ISR registration, or any
> `variants/*/platformio.ini` of a classic env, that chapter runs the full set of
> ten classic envs instead of the single representative.

**The two safeboot envs are the only entries in `default_envs`**
(`platformio.ini:16-18`) and are not in the 8-env set. K15 changes
`src/safeboot/`, so that chapter's gate must include `esp32-safeboot` and
`esp32-S3-safeboot`, and must rebuild and commit the two root `safeboot*.bin`.

## 7. Verification

| Gate | Check                                                                                                                                                                                                                                                                                                                                                                                                    |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G1   | On `fork-neo-test`: tree identity over the whole filter set, `platformio.ini` included -- it is copied verbatim there. On `fork-neo` that check cannot hold (11 stripped files, `instrument.cpp/.h` absent, the `tdeck_helpers.cpp` marker, the transformed `platformio.ini`), so the check becomes `strip(fork-neo-test tree) == fork-neo tree`, with the strip as the checked-in script from section 5 |
| G2   | 8 builds per commit, sequential. Parallel `pio run` corrupts `.pio/build` and emits phantom linker errors in untouched files                                                                                                                                                                                                                                                                             |
| G3   | Region check per build; string scan of the ELF after every strip commit                                                                                                                                                                                                                                                                                                                                  |
| G4   | RAM/flash/region deltas, same-base only -- cross-base figures are baseline drift                                                                                                                                                                                                                                                                                                                         |
| G5   | Differential run against official 4.35t: two nodes, same traffic, compare frames. Every difference is a known fix or a defect, nothing in between                                                                                                                                                                                                                                                        |
| G6   | Bench fleet (RAK-90, Heltec-93, T-Beam-92, T-Deck-14), 24 h soak, web-flasher pass per chip family including an abort test                                                                                                                                                                                                                                                                               |

On-wire differences are detectable without hardware through the golden captures
(`test/golden/`) and the corpora (`test_aprs_reencode`, `test_aprs_fuzz`,
`test/support/traces/dedup_trace.txt`). That scaffolding lives on
`fork-neo-test` only, which is why truth is established there and `fork-neo`
afterwards only has to prove it produces the same firmware.

Cost: about **310 sequential board builds** across both branches plus two full
32-env sweeps. Measured on the stage-0 gate, an incremental board build takes
**~30 s** (7 builds in 212 s), so a per-commit gate is minutes, not hours --
which is also why the trigger rule in section 6 is affordable. The exception is
any chapter that edits `platformio.ini` or a variant ini: that wipes
`.pio/build` and forces full rebuilds, so K18 and K19 are the expensive ones.
This belongs in a driver script, not in hand work.

## 8. Long-term maintenance: the part that is easy to underestimate

This is not a one-shot. Once `dev-dk5en` exists, every upstream PR has to reach
both fork branches: `fork-neo-test` so the PR can actually be verified, and
`fork-neo` so it stays the production mirror.

Three ways to carry that, in rising order of upfront cost and falling order of
ongoing cost:

**A -- replay on both branches.** Every change is made twice, by hand. Simple to
explain, and the cost never goes away.

**B -- `fork-neo` is the trunk, `fork-neo-test` is an overlay.** Upstream PRs
land on `fork-neo` and are merged forward into `fork-neo-test`. Each change is
made once. Cost: the `INSTRUMENT_ENABLED` blocks sit inside production files, so
a PR touching `platformio.ini` or the one marker line conflicts on the forward merge --
`command_functions.cpp` is both the highest-churn file and one of the seven.

**C -- make the instrumentation additive.** Move the instrumentation out of
production files behind hooks, so the overlay is purely new files plus a
separate ini include and merges cleanly forever. Highest upfront cost, lowest
ongoing cost, and it would make the strip trivial instead of mechanical.

**D -- `fork-neo-test` is the trunk, `fork-neo` is re-derived from it.** All
work lands on `fork-neo-test`, which carries the full tree. `fork-neo` is not
maintained at all: it is thrown away and re-projected from `fork-neo-test`
whenever the upstream offering needs refreshing.

**Decided 2026-09-20: D, replacing B, on the operator's correction.** B had
`fork-neo` as the trunk and required a forward merge into `fork-neo-test` for
every change, plus a one-off sealing merge before the first PR to resolve the
overlap between strip and campaign edits. Both disappear under D, because
nothing is ever merged between the two branches -- one is a function of the
other.

What makes D affordable is that the derivation is a script and its result is
checkable, not a judgement call:

- the projection is `git checkout --no-overlay <tree> --pathspec-from-file`
  per chapter (section 4), driven by the same path lists both branches use;
- the strip is `tools/neo_strip.py`, idempotent, and it exits non-zero if a
  transformation it expected to make finds nothing;
- two gates close it: `strip(fork-neo-test)` must equal `fork-neo` over
  `src lib variants config platformio.ini`, and the defined-symbol sets of the
  two branches must be identical on all eight lead targets.

Both branches were rebuilt from scratch under D on 2026-09-20 and both gates
came back empty, so the cost of a re-derivation is measured, not estimated: one
script run plus the gate.

C stays the direction of travel. Under D it buys less than it did under B --
there is no forward merge left to conflict -- but it would shrink the strip from
two transformations to zero, and that is the difference between a script that
has to be right and one that has nothing to do.

Operational consequence to write down before it bites: **never commit to
`fork-neo`.** A change made there is lost at the next re-derivation, silently,
because the derivation starts from `upstream/dev` and never reads the old
branch. The tip that was thrown away is tagged (`neo-backup-<date>-fork-neo`)
so the loss is recoverable, but nothing warns about it.

## 9. Web flasher

Design is settled in `docs/meshcom-web-flasher-plan.md` (approved, not started).
It is not optional: OTA only ever writes `ota_0`, and no code path in the tree
writes safeboot or the partition table. A five-region full flash over Web Serial
is the only way to ship a new safeboot to the field.

Web Serial is USB, so an abort is always repairable by re-flashing -- unlike an
aborted OTA. It works in Chrome and Edge only; nRF52 gets a UF2 download
fallback.

Location: `dk5en.github.io/MeshCom-Firmware`. Pages are not enabled on the
upstream repo, so the flasher stays on the fork regardless of where the code
ends up.

## 10. Decisions taken

- **Changelog language: German.** `docs/CHANGELOG-neo.md` is written in German
  because its first job is the negotiation with Kurt OE1KBC. It follows the
  structure of `docs/CHANGELOG-stability.md`, not its language.
- **Maintenance model: D** (section 8), decided 2026-09-20 on the operator's
  correction. `fork-neo-test` is the trunk and replaces `dry-unification`;
  `fork-neo` is re-derived from it by projection plus strip, never merged with
  it, never committed to. This drops model B and with it the sealing merge B
  would have needed.
- **Failure policy: append during stage 1, fold before stage 3.** When a commit
  fails its gate on `fork-neo-test`, the fix is appended as a fixup rather than
  amended in, so the stage does not restart and the earlier builds stay valid.
  Before stage 3 those fixups are folded into the chapter commits they belong to,
  so `fork-neo` carries the clean series. `fork-neo-test` is allowed to be honest
  about how it got there; `fork-neo` is the version Kurt reads.
- **`E22_XML-DevKitC` threshold: `--min-headroom 4000`**, 4096 everywhere else.
  The gate enforces "do not make it worse" on that env instead of a constraint
  the campaign did not create.
- **`lib/tinyxml2` travels to `fork-neo`.** Reverted from an earlier decision to
  restore upstream's `lib_deps` URL: that URL drags in an example `main()` worth
  5768 B of DRAM and 572 B of IRAM on the tightest env in the tree, which would
  put it below the threshold just agreed. It is a campaign result, not ballast,
  and it earns a changelog chapter.
- **Gate stays at 8 envs**, with the trigger rule in section 6 covering the case
  where the nine classic envs could diverge.
- **The instrumentation stays on `fork-neo`.** Reversing the strip decision of
  the same day: `upstream/dev` already ships it, so removing it would delete the
  maintainer's own code. This restores the operator's original answer, which was
  right; the later "no instrumentation" instruction meant the test harness.
- **`test/` and the native envs land last**, as chapter K19 on `fork-neo-test`
  only, because a native env's source filter is complete only at the tip.
- **`docs/CHANGELOG-neo.md` is written complete in one pass**, not layered per
  gate. The end state is known; commit hashes can be added as references
  afterwards, once the chapters have survived their gates.
- **No automatic GitHub builds** (section 5): build on the MacBook, publish by
  hand.
- **`fork-neo` keeps the campaign's `FLASH_VERSION` (`20260912`).** Decided
  2026-09-20. It is the stamp the campaign was built and measured under, and it
  is what the bench and soak results refer to. `upstream/dev` sits at
  `20260909`; the difference is visible in the boot log and in the `fw=` field
  of the STAT line, and it is the only version-like delta the branch carries.
  The release name `4.35t_20260919_neo` lives in the GitHub tag, not in the
  firmware.

## 11. Stage 1 result

`fork-neo-test` exists: six commits on `upstream/dev` (the changelog, then five
code commits). G1 is empty over the filter set -- the split provably lost
nothing. 40/40 builds across 8 envs.

**Seventeen chapters became five commits, and that is a property of the code.**
Five projection attempts with different orderings converged on it. Four distinct
dependency classes force it, and they were found in this order, each only
visible after the previous one was fixed:

1. **Missing new headers.** A chapter's file includes a header another chapter
   owns. Solved as a fixpoint: a header lands no later than its first consumer.
2. **Modified headers.** Upstream's version of a header lacks a declaration or,
   in the case of `configuration_global.h`, the include guard the fork added.
   Only visible once class 1 was gone.
3. **Type changes.** R2-04 (`aprsMessage` from `String` to `char[]`) touches 18
   files across 11 chapters. A type change and its call sites cannot be split.
4. **Moved definitions.** The C-series carve-outs moved functions between files.
   `checkSerialCommand()` in two commits means it is defined twice and the link
   fails. This class is invisible to both header analysis and type checking --
   and carve-outs were half the campaign.

The lesson for stage 3: enumerate all four classes up front rather than
discovering them one build at a time.

**Measured on the way, and it belongs in the pitch to Kurt:** the first commit
carries only upstream's code plus K01's deletions, so its region figures are
upstream's. `ttgo_tbeam` sits at **20 bytes** of free `iram0_0_seg` there;
`E22_XML-DevKitC` at **1160 bytes** of `dram0_0_seg`. After the core commit:
4972 and 17816. Twenty bytes means the next IRAM-placed function breaks the
link, which is what happened to upstream CI at `9d885b1a`.

**One instrument failure, recorded so it is not repeated.** The first full gate
reported 40/40 green with the region check silently doing nothing:
`tools/resource_watch.py` is not projected onto `fork-neo-test`, and the gate
tested for the string "error" rather than the exit code, so
"No such file or directory" counted as a pass. The builds were real; the region
verdict was void. The instrument now lives in the scratchpad, the gate uses the
exit code and `--strict`, and it self-tests before the run.

## 12. Stage 2 result

Everything green on the tip of `fork-neo-test`: **34/34 host envs, 32/32 board
envs, both safeboot envs**, and one `-DINSTRUMENT_ENABLED=1` build whose ELF
carries 13 `INSTR` strings -- so the measurement firmware demonstrably still
compiles and is not rotting.

Three things the stage found that no plan document had:

1. **`tools/` is needed twice, not once.** `ensure_tasmota_framework.py` for the
   two safeboot envs (known) and `force_include_settings_stub.py` for
   `env:native_nrf52_settings_paths` (not known -- that env's build died with
   "missing SConscript file" before a single test ran). All `tools/` references
   in `platformio.ini` were then enumerated at once instead of discovered one at
   a time: the remaining two, `bench/ble_golden.py` and `logharvest.py`, sit in
   comment lines and are not dependencies.
2. **Safeboot must be built last.** The safeboot envs use the Tasmota
   platform-espressif32 fork, whose framework package shares its installation
   directory with mainline espressif32. Building safeboot swaps that directory
   out from under every subsequent mainline build. `ensure_tasmota_framework.py`
   repairs the Tasmota side; the ordering avoids paying for it 32 times.
3. **The root `safeboot*.bin` really do go stale.** They sit outside the filter
   set while K15 changes `src/safeboot/`, so the branch carried new safeboot
   source with upstream's old binary until the gate rebuilt them. Same size as
   the working tree's, not byte-identical -- `__DATE__`/`__TIME__` and the ELF
   checksum are in the image.

Three fixup commits are appended, to be folded before stage 3: the two `tools/`
scripts and the rebuilt binaries.

## 13. Stage 3 result

`fork-neo` exists: six commits on `upstream/dev`, the three stage-1/2 fixups
folded into their chapters. `tools/force_include_settings_stub.py` is not needed
here at all -- it hung off a native env the strip removes, which is a small
independent sign that the cut is coherent.

**The strip gate holds, byte for byte.** `tools/neo_strip.py` applied to
`fork-neo-test`'s `platformio.ini` and `tdeck_helpers.cpp` produces exactly
`fork-neo`'s versions, and a second run reports nothing left to strip. The whole
difference between the branches over the filter set is those two files, 999
deleted lines.

**The symbol sets are identical on all eight targets**, which is the check that
actually proves nothing was lost:

| target                 | symbols |
| ---------------------- | ------- |
| heltec_wifi_lora_32_V3 | 12212   |
| E22-DevKitC            | 11266   |
| E22_XML-DevKitC        | 11474   |
| ttgo_tbeam             | 11404   |
| ttgo_tbeam_supreme     | 12463   |
| t_deck                 | 15575   |
| t_deck_plus            | 15574   |
| wiscore_rak4631        | 3193    |

Zero symbols present in one branch and absent in the other. Neither `test/` nor
the native envs reach a board image, so this is what the theory predicted -- but
a prediction is not a measurement, and the earlier instrument failure is why it
was measured.

Builds: 38/40. The two reds are `E22_XML-DevKitC` and `ttgo_tbeam` at the K01
commit, which carries upstream's code plus deletions only -- upstream's own
numbers, closed by the core commit. The tip is clean on all eight.

## 14. Stage 4 result

`fork-neo` builds everything: **32/32 board envs, both safeboot envs**, and the
`-DINSTRUMENT_ENABLED=1` build carries **13 `INSTR` strings -- the same count as
`fork-neo-test`**. The instrumentation demonstrably survives the strip
unchanged, which is the point: it is upstream's code and was never ours to
remove.

The host suite has no counterpart here by design. Its replacement is the
stage-3 symbol diff, which is done.

Everything that can be established without hardware is now established. What
remains is the part the workstation cannot answer: does this firmware behave on
the air like official 4.35t.

## 15. Stage 5 result -- both hardware checks passed

**Differential run passed** (2026-09-19): no measurable difference on the air
against `upstream/dev`, +11 296 B heap on the Heltec and +15 560 B on the
T-Beam, and 10,5 / 45,5 kB less flash.

**Twelve-hour soak passed** (19:41 to 08:00, three nodes, one of them with mesh
and gateway on under real network load). No reboot, no crash, no ring overflow,
no gateway error, no reconnect. Uptime 12.00 h, strictly monotonic over 24
snapshots. Heap flat after warm-up: 79 017 -> 78 119 B mean over nine hours.
Loop instrumentation reported five gaps in twelve hours, all in the first minute
after boot.

Measurement note worth keeping: the free heap reaches the console over two
different channels depending on `--setlog`. With `off` it is the `[HEAP]` line
from `loopAction_heapMon()`; with `on` that line is suppressed and the value
appears once per STAT window in the `heap=` field instead. Grepping for `[HEAP]`
alone therefore misses two of the three soak nodes -- all three did report a
flat heap. The asymmetry behind it (ESP32 gated only the print, nRF52 gated the
whole scheduler entry) is cleaned up in stage 6.

## 16. Stage 6 result -- the branches consolidated

Both branches were thrown away and rebuilt from scratch on 2026-09-20, so the
three fixups stopped being fixups and `fork-neo-test` took over the whole tree.
The old tips are kept as `neo-backup-20260920-fork-neo` and
`neo-backup-20260920-fork-neo-test`.

Two content changes went into `dry-unification` first, so the projection would
pick them up:

- `build(safeboot)` -- the checked-in `safeboot.bin`/`safeboot-s3.bin` still
  came from before the campaign. The board envs flash exactly those root files
  through their `upload_command`, so a stale image ships unnoticed. Rebuilt,
  -1712 B each. The safeboot build sees nothing of the campaign delta: its
  `build_src_filter` admits only `src/safeboot/*`, `esp32_flash.*` and
  `settings_schema.cpp`.
- `refactor(loop)` -- `heapMonTimer` was the only one of the seven migrated
  scheduler entries whose `enabled()` meant something different per platform.
  Both now gate on `!bDisplayLog`. Console output is indistinguishable either
  way; see the changelog entry in K07 for why.

`fork-neo-test`, ten commits on `upstream/dev`:

| Commit         | Contents                                                |
| -------------- | ------------------------------------------------------- |
| `docs(neo)`    | `docs/CHANGELOG-neo.md`                                 |
| `neo K01`      | 57 paths                                                |
| `neo` (core)   | 187 paths, incl. `tools/ensure_tasmota_framework.py`    |
| `neo K15`      | 6 paths, incl. both safeboot images                     |
| `neo K16`      | 1 path                                                  |
| `neo K17`      | 1 path                                                  |
| `neo K19`      | 440 paths, incl. `tools/force_include_settings_stub.py` |
| `neo: docs`    | 215 paths                                               |
| `neo: tools`   | 188 paths                                               |
| `neo: project` | 19 paths, including the fork's four deletions           |

The three former fixups now sit where their reason sits: the Tasmota script in
the core commit, because that commit brings the `platformio.ini` that names it
as a pre-step of the two safeboot envs; the settings stub in K19, with the
native env that needs it; the safeboot images in K15.

`fork-neo`, six commits, the same chapters minus K19 and minus the three
non-code commits, with `tools/neo_strip.py` applied inside the core commit.

Gates, all green:

| Gate                                                 | Result                                      |
| ---------------------------------------------------- | ------------------------------------------- |
| `git diff fork-neo-test dry-unification`, whole tree | empty                                       |
| `strip(fork-neo-test)` vs `fork-neo`                 | empty over the filter set                   |
| safeboot images, `fork-neo` vs `fork-neo-test`       | identical                                   |
| `fork-neo` per commit, 8 lead targets                | 38/40                                       |
| defined-symbol sets, 8 lead targets                  | 0 deviations, 3193-15575 symbols per target |
| working tree at the tip                              | 8/8 boards, 34/34 native                    |

The two reds are both on K01 and both are upstream's own baseline, not a
regression: `ttgo_tbeam` has 20 bytes of free `iram0_0_seg` there and
`E22_XML-DevKitC` 1160 B DRAM / 3456 B IRAM. K01 carries only upstream's code
plus the campaign's deletions, so it is the closest thing to a measurement of
`upstream/dev` itself. Both builds link; what fails is our own 4000-byte
headroom rule. From the core commit onward all eight targets are green -- the
campaign is what gives those two boards room again.

## 17. Still open

- Whether `.github/workflows/` additionally gets a hard `if: false` on top of the
  repo-level Actions disable, at the cost of a visible delta against upstream.
  Relevant for `fork-neo-test`, which carries the fork's own `ci-build.yml`
  triggering on branch pushes; `fork-neo` carries upstream's `.github`
  unchanged and has no such delta.
- Whether `tools/neo_strip.py` should travel to `fork-neo`. It documents
  exactly what was removed, which is a courtesy to a reviewer; it also
  describes two branches that do not exist in upstream's repository, which is
  noise there. Currently it travels.
- Pushing both branches to `DK5EN/MeshCom-Firmware`, and whether
  `origin/dry-unification` is deleted along with the local branch.
- Release `4.35t_20260919_neo` and the GH Pages web flasher
  (`tools/pages_flasher.py` does not exist yet).
- The conversation with Kurt OE1KBC about `fork-neo` as a PR.

## 18. Review history

- **2026-09-19, advisor pass (Fable) on `1eb13f5f` + `332e9542`.** Ten defects
  confirmed against the tree and fixed in this revision. The load-bearing one:
  `git checkout <tree> -- <paths>` runs in overlay mode and never deletes, so the
  projection as originally written would have committed none of the 77 deletions.
  Also confirmed: `INSTRUMENT_ENABLED` already defaults to 0, making the original
  bridge gate a no-op; the strip surface is 11 files rather than 7;
  `configuration_global.h` carries no guard at all; `tdeck_helpers.cpp:143` is an
  unguarded bench marker; `FLASH_VERSION` also reaches the setlog STAT line;
  dropping `lib/tinyxml2` breaks `E22_XML-DevKitC`; the file count was 248 with
  48 additions, not 251 with 49; the two safeboot envs are the only members of
  `default_envs` and were missing from the gate; and an incremental board build
  takes ~30 s, which removes the economy argument for a reduced env set.
