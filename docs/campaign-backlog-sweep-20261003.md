# Backlog sweep 2026-10-03 -- campaign state

Goal: close every row of `docs/BACKLOG.md` 3.2 that can be closed today without bench hardware
and without an operator decision; prepare the rest so the decision or the bench run is the only
thing left. Orchestrated as waves (`/orchestrate-waves`), writers on Sonnet. Base: `fork-dev`
`b5645b23`, not pushed.

## Triage of the 34 open rows

| Row                                                  | This campaign                                       | Wave       |
| ---------------------------------------------------- | --------------------------------------------------- | ---------- |
| REG-02, REG-08                                       | runner: raw-log lookup order, real counts           | 1 (W1)     |
| REG-04                                               | webgui badge test as stage-3 step                   | 1 (W2)     |
| REG-05 / TM-26                                       | `tools/bench/mesh_exchange.py` + plan step          | 1 (W3, W2) |
| W0.6                                                 | native tests for CSMA timing + compress             | 1 (W4)     |
| SOAK-440A                                            | evaluate the DK5EN-98 capture from rpizero          | 1 (W5)     |
| REG-07                                               | CI job onto `tools/regression.sh --stage 1,2`       | 1 (W6)     |
| N-36                                                 | German PR text drafted (submission = operator)      | 1 (W7)     |
| REG-06                                               | track the skill file (orchestrator)                 | gate 1     |
| BL-06                                                | triage the ~30 legacy ids against 4.40a             | 0 (scout)  |
| OPT-W4..W7, OPT-D14, N-12, BL-02                     | re-verify against 4.40a                             | 0 (scout)  |
| BL-03, GPS-05b                                       | recon now; tuning change + replay test in wave 2    | 0 -> 2     |
| W0.2                                                 | build flags on five envs, needs the pio slot        | 2          |
| DR-16, BL-01, BL-05, SB-05, HASH-TAG, ADR-*          | operator decision; questions listed at the end      | --         |
| BL-04, SB-06, SB-07, SNF-GW, MC5-TOPO, STRUCT, DEF-* | needs hardware, upstream review or a design session | --         |

## Ownership, wave 1

| Writer | Exclusive files                                                      | Verification (scoped)                      |
| ------ | -------------------------------------------------------------------- | ------------------------------------------ |
| W1     | tools/regression.sh, test/golden/selftest.sh, test/test_nbrlog/\*.py | bash -n, `--list`, nbrlog scripts          |
| W2     | tools/bench/bench_suite.py, tools/bench/test_bench_suite.py          | pytest on that file                        |
| W3     | tools/bench/mesh_exchange.py, tools/bench/test_mesh_exchange.py      | pytest on that file                        |
| W4     | test/test_csma_timing/\*, test/test_compress/test_compress.cpp       | `pio test -e native -f <suite>` (pio slot) |
| W5     | docs/soak-20261001-440a-verdict.md                                   | tools/soakstatus.py, logauswertung         |
| W6     | .github/workflows/ci-build.yml                                       | yaml parse                                 |
| W7     | docs/pr-draft-n36-20261003.md                                        | prettier                                   |

Hotspots kept by the orchestrator: platformio.ini (test_filter for W4's suites),
test/golden/native/variant-ini-effective.json (regenerate), .gitignore (REG-06), docs/BACKLOG.md
(row statuses after the gate). The pio slot in wave 1 belongs to W4 alone; the whole-tree gate
(`tools/regression.sh --stage 1,2`) runs once after the wave.

## Status

- Wave 0 (scouts) and wave 1 dispatched 2026-10-03 evening.
- Wave 1 landed 2026-10-03 ~21:45: W1-W7 all returned; gate `tools/regression.sh --stage 1,2`
  green (48 envs, 1639 cases, 497 pytest, selftest 48 commands). Scouts: BL-03 and N-12 closed,
  DM-01..06 obsolete, RF-09 confirmed open, BL-02 bounded, GPS-05b is a decision. New finding
  BL-07 (heartbeat watchdog never arms without a first BEAT). CSMA test needs a carve-out.
- Wave 2 (next): W8 RF-09 fix + twin test (pio slot); W9 ruff syntax gate (ruff.toml,
  stage 2). Wave 3: W10 CSMA carve-out + test (pio slot); W11 CQ-04 dead lv_conf.h (needs
  t_deck/t_deck_plus builds). Not this campaign: W0.2 (five cold board builds), BL-07 fix
  (behaviour change in the gateway service, wants its own review).
