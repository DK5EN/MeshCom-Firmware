# tools/neo

`fork-neo` is a function of `fork-neo-test`, not a branch anyone works on. These
files are that function. Background: `docs/neo-campaign.md`, section 8 (model D)
and section 4 (projection, not rebase).

| File              | What it does                                                                   |
| ----------------- | ------------------------------------------------------------------------------ |
| `paths/*.txt`     | the chapter cut: which of the 688 paths belongs to which commit                |
| `derive.sh`       | throws `fork-neo` away and rebuilds it from `upstream/dev`, then gates itself  |
| `gate.sh`         | builds every `fork-neo` commit on the eight lead targets, then the symbol diff |
| `../neo_strip.py` | the two content transformations `derive.sh` applies inside the core commit     |

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

`K01 CORE K15 K16 K17` are the code cut, 249 paths, disjoint and complete
against `git diff --no-renames upstream/dev fork-neo-test` over
`src lib variants config platformio.ini`. `K19.txt` (439 paths, `test/`) is
listed here for the record; `derive.sh` does not use it, because the test
harness does not travel to `fork-neo`.

Two properties are worth restating because they are easy to lose:

- **`--no-renames`.** `src/t5-epaper/scr_mrg.cpp` moved to
  `src/ui_common/scr_mgr.c`. With rename detection the diff shows only the new
  path, so the old one survives the projection and the closing tree-identity
  check fails somewhere unrelated.
- **`--no-overlay`.** `git checkout <tree> -- <paths>` deletes nothing in its
  default overlay mode. 77 of the 249 paths are deletions.

If the cut ever changes, re-assert disjointness and completeness before
projecting -- a path claimed twice, or by nothing, breaks the closing check in a
way that is tedious to diagnose afterwards.
