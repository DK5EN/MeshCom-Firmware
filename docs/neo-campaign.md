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

| Stage | What                                                                  | Gate                                                                                                                |
| ----- | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| 0     | Merge `upstream/dev` into `dry-unification` -- **done, `c09e22b8`**   | 34/34 native envs, 8/8 board envs                                                                                   |
| 1     | `fork-neo-test` from `upstream/dev`, the chapter commits with harness | per commit: 8 builds + region check. The host suite only runs from K19 on, when `test/` and the native envs land    |
| 2     | `fork-neo-test` complete                                              | all 32 board envs, both safeboot envs, full host suite, one `-DINSTRUMENT_ENABLED=1` build, bench fleet             |
| 3     | `fork-neo` from `upstream/dev`, the same chapters minus K19, stripped | per commit: 8 builds + region check + ELF string scan + symbol-set diff against the same chapter on `fork-neo-test` |
| 4     | `fork-neo` complete                                                   | all 32 board envs, tests against every firmware                                                                     |
| 5     | Release `4.35t_20260919_neo`, web flasher on gh-pages                 | differential run vs official 4.35t, 24 h soak                                                                       |
| 6     | Negotiate `dev-dk5en` with Kurt, push                                 | --                                                                                                                  |

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

**Decided 2026-09-19: B.** `fork-neo` is the trunk, upstream PRs land there
and are merged forward into `fork-neo-test`. C stays the direction of travel
once neo has shipped once -- the conflict surface B accepts is exactly the
surface C would remove, so the two are the same decision seen twice.

Operational consequence to write down before it bites: the forward merge is
not optional and not occasional. A PR that reaches `fork-neo` and not
`fork-neo-test` is a PR nobody can verify afterwards, which defeats the reason
the split exists.

**Model B needs a sealing step before the first upstream PR.** Both branches
share only `upstream/dev` as a merge base, so the first forward merge is a full
three-way merge of overlapping content: everywhere the strip changed a line the
campaign also changed differently from base -- the `#if`/`#else` help text
around `command_functions.cpp:1040` is one -- it conflicts. Resolve that once,
deliberately, and record the sync point. Later PR merges then start from a base
that has already moved past it.

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
- **Maintenance model: B** (section 8), with the sealing merge recorded before
  the first upstream PR.
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

## 11. Still open

- The commit cut itself. A first proposal with all 248 files assigned to 18
  chapters is in `docs/neo-commit-cut.md`; that document also shows why 18 is an
  upper bound and not the answer. K19 (`test/` plus the native envs) makes 19 on
  `fork-neo-test`.
- Whether `.github/workflows/` additionally gets a hard `if: false` on top of the
  repo-level Actions disable, at the cost of a visible delta against upstream.

## 12. Review history

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
