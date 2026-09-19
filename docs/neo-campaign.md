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
  compile-time literal printed in the boot log only, never transmitted, and
  `flashLayoutCompatible()` compares `FLASH_STRUCT_VERSION` instead -- which
  must NOT move, on pain of wiping the fleet's settings.
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

| Stage | What                                                                | Gate                                             |
| ----- | ------------------------------------------------------------------- | ------------------------------------------------ |
| 0     | Merge `upstream/dev` into `dry-unification` -- **done, `c09e22b8`** | 34/34 native envs, 8/8 board envs                |
| 1     | `fork-neo-test` from `upstream/dev`, the ~15 commits with harness   | per commit: host suite + 8 builds + region check |
| 2     | `fork-neo-test` complete                                            | all 32 board envs, full suite, bench fleet       |
| 3     | `fork-neo` from `upstream/dev`, same commits, stripped              | per commit: 8 builds + region check + strip scan |
| 4     | `fork-neo` complete                                                 | all 32 board envs, tests against every firmware  |
| 5     | Release `4.35t_20260919_neo`, web flasher on gh-pages               | differential run vs official 4.35t, 24 h soak    |
| 6     | Negotiate `dev-dk5en` with Kurt, push                               | --                                               |

Stage 0 was mandatory before anything else: `dry-unification` branched at
`1cb2d9e6` and did not know the last 11 upstream commits. Projecting fork
content onto an `upstream/dev` base without that merge would have reverted
Kurt's work wordlessly -- the trap that already hit PR #1135.

The changelog (`docs/CHANGELOG-neo.md`) is written **before** stage 1, because
it defines the commit cut. It is the outline, not the post-hoc documentation.

## 4. Construction: projection, not rebase

An interactive rebase is not an option: 741 commits against `upstream/dev`, core
files touched 30-64 times each, and upstream squash-merges our PRs, so identical
content appears to git as a conflict.

```
git checkout -b <branch> upstream/dev
# per theme:  git checkout <content tree> -- <paths>  ->  commit with changelog text
# closing:    git diff <content tree> <branch> -- <filter set>   must be empty
```

The tree-identity check at the end makes the thematic split provably lossless.
The split is a storytelling decision, not a correctness one.

Projection precondition: the content tree must already contain `upstream/dev` in
full. After `c09e22b8` it does (`git merge-base --is-ancestor upstream/dev
dry-unification` succeeds).

Scope of the filter set, measured on the merged tree: **251 files** -- 49 added,
77 deleted (vendor fonts, dead platforms, 20 `lv_conf.h` copies), 122 modified,
1 renamed. `src/` 164, `variants/` 79, `lib/` 4, `config/` 1, `platformio.ini`,
two `safeboot*.bin`.

## 5. What gets stripped for fork-neo

Two separate things, both removed:

1. **Host test harness** -- `test/` (439 files) and the 34 `[env:native*]`
   blocks in `platformio.ini`.
2. **Firmware instrumentation** -- the `INSTRUMENT_ENABLED` blocks (7 files:
   `instrument.cpp/.h`, `command_functions.cpp`, `configuration_global.h`,
   `t-deck/lv_obj_functions.cpp`, `esp32/esp32_main.cpp`, `nrf52/nrf_eth.cpp`).

Also out: `docs/` except `CHANGELOG-neo.md`, `tools/`, `.claude/`,
`.prettierignore`, `release.md`, `release-notes.md`.

Forced companion: `platformio.ini:1119,1163` calls
`tools/ensure_tasmota_framework.py` in both safeboot envs. Either the script
travels or those two lines go, otherwise the safeboot build breaks.

### The strip boundary that must not be crossed

**Field diagnostics are not test instrumentation.** A guard change once swept
`--udplog`, `--udpstat`, `--wifistat` and `--ethstat` out of shipped images
without turning a single build red. `--persiststat` carries an explicit comment
saying it is a field diagnostic and deliberately outside the
`INSTRUMENT_ENABLED` surface, because the migration it checks happens on shipped
images.

Every strip commit therefore ends with a **string scan of the built ELF**, not
with a green build. Exit code 0 proves nothing about guarded code.

### What the strip costs us

`fork-neo` images are no longer comparable byte-for-byte with `fork-neo-test`.
The replacement gate: build `fork-neo-test` with `INSTRUMENT_ENABLED=0` and
compare that image against `fork-neo` at the same commit. If the strip was
mechanical and complete, those two must agree. A difference means the strip
removed something the guard did not.

Note the flag spelling: `-DINSTRUMENT_ENABLED=1`, no space. The spaced form is
dropped silently.

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

Open operator decision: `E22_XML-DevKitC` is below the 4096 B threshold today.
Proposed handling -- a per-env threshold taken from the current state, so a
commit may not make it worse, instead of fixing a pre-existing constraint first.
Known levers if headroom is needed: the T-Beam board JSON's PSRAM flags cost
4.4 kB IRAM, the tinyxml2 example `main()` costs 5.8 kB DRAM.

## 7. Verification

| Gate | Check                                                                                                                                             |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| G1   | Tree identity over the filter set. `platformio.ini` is exempt (transformed, not copied) and gets a narrower env-by-env comparison                 |
| G2   | 8 builds per commit, sequential. Parallel `pio run` corrupts `.pio/build` and emits phantom linker errors in untouched files                      |
| G3   | Region check per build; string scan of the ELF after every strip commit                                                                           |
| G4   | RAM/flash/region deltas, same-base only -- cross-base figures are baseline drift                                                                  |
| G5   | Differential run against official 4.35t: two nodes, same traffic, compare frames. Every difference is a known fix or a defect, nothing in between |
| G6   | Bench fleet (RAK-90, Heltec-93, T-Beam-92, T-Deck-14), 24 h soak, web-flasher pass per chip family including an abort test                        |

On-wire differences are detectable without hardware through the golden captures
(`test/golden/`) and the corpora (`test_aprs_reencode`, `test_aprs_fuzz`,
`test/support/traces/dedup_trace.txt`). That scaffolding lives on
`fork-neo-test` only, which is why truth is established there and `fork-neo`
afterwards only has to prove it produces the same firmware.

Cost: 15 commits x 8 envs, twice, plus two full 32-env sweeps -- about
**310 sequential board builds**. This belongs in a driver script, not in hand
work.

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
a PR touching one of those 7 files conflicts on the forward merge --
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
- **Maintenance model: B** (section 8).
- **Failure policy: append during stage 1, fold before stage 3.** When a commit
  fails its gate on `fork-neo-test`, the fix is appended as a fixup rather than
  amended in, so the stage does not restart and the earlier builds stay valid.
  Before stage 3 those fixups are folded into the chapter commits they belong
  to, so `fork-neo` carries the clean series. `fork-neo-test` is allowed to be
  honest about how it got there; `fork-neo` is the version Kurt reads.

## 11. Still open

- The `E22_XML-DevKitC` threshold decision in section 6.
- The commit cut itself. A first proposal with all 248 files assigned to 18
  chapters is in `docs/neo-commit-cut.md`; that document also shows why 18 is
  an upper bound and not the answer.
