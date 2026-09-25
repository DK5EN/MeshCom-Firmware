# tools/neo

`fork-neo` is a function of `fork-neo-test`, not a branch anyone works on. These
files are that function. Background: `docs/neo-campaign.md`, section 8 (model D)
and section 4 (projection, not rebase).

| File              | What it does                                                                   |
| ----------------- | ------------------------------------------------------------------------------ |
| `paths/*.txt`     | the chapter cut: which of the 706 paths belongs to which commit                |
| `derive.sh`       | throws `fork-neo` away and rebuilds it from `upstream/dev`, then gates itself  |
| `gate.sh`         | builds every `fork-neo` commit on the eight lead targets, then the symbol diff |
| `../neo_strip.py` | the two content transformations `derive.sh` applies -- it does not travel      |

## `fork-neo` does not advertise that it is a derivative

Operator decision 2026-09-20: nothing on `fork-neo` says a strip happened. It is
`upstream/dev` plus better code, and the test scaffolding was never part of what
is offered -- the fork's tests are the fork's, not production. Consequences for
anyone touching these scripts:

- `neo_strip.py` runs from a temporary copy and is **not** projected onto the
  branch. Neither is anything else that exists only to produce it.
- `docs/CHANGELOG-neo.md` carries no section explaining what was removed, and
  should not grow one.
- Source comments on `fork-neo` still name `env:native*` envs and
  `test/golden/*.py` linters that live only on `fork-neo-test` (20 references in
  10 files, measured 2026-09-20). Accepted, not an oversight: they record how
  the change was verified here, the same way any comment may cite a bench the
  reader does not have.

## Never commit to `fork-neo`

`derive.sh` starts from `upstream/dev` and never reads the old branch, so a
change made on `fork-neo` is gone at the next run. It tags the tip it discards
(`neo-backup-<timestamp>-fork-neo`), which makes the loss recoverable but not
visible. Work on `fork-neo-test`.

## Usage

```sh
tools/neo/derive.sh                       # rebuild fork-neo, run the two cheap gates
tools/neo/gate.sh /tmp/neo-gate.log       # the expensive one, ~20 min
```

`derive.sh` closes with two checks that cost nothing: `strip(fork-neo-test)`
must equal `fork-neo` over `src lib variants config platformio.ini`, and the two
safeboot images must be identical. `gate.sh` adds the per-commit builds and the
defined-symbol comparison.

## The path lists

`K01 CORE K15 K16 K17` are the code cut, 262 paths, disjoint and complete
against `git diff --no-renames upstream/dev fork-neo-test` over
`src lib variants config platformio.ini`. `K19.txt` (444 paths, `test/`) is
listed here for the record; `derive.sh` does not use it, because the test
harness does not travel to `fork-neo`.

Two properties are worth restating because they are easy to lose:

- **`--no-renames`.** `src/t5-epaper/scr_mrg.cpp` moved to
  `src/ui_common/scr_mgr.c`. With rename detection the diff shows only the new
  path, so the old one survives the projection and the closing tree-identity
  check fails somewhere unrelated.
- **`--no-overlay`.** `git checkout <tree> -- <paths>` deletes nothing in its
  default overlay mode. 78 of the 262 paths are deletions.

If the cut ever changes, re-assert disjointness and completeness before
projecting -- a path claimed twice, or by nothing, breaks the closing check in a
way that is tedious to diagnose afterwards.
