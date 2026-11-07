# Campaign: GPIO-01 hardware pins on the Pins page and over RM1

Resume point. Design and decisions: [concept-gpio-pins-20261106.md](concept-gpio-pins-20261106.md). Backlog row: `GPIO-01`.

## Operator decisions (2026-11-07)

- **No stop after W0:** the pin table is a configuration item; real-world use will show its errors.
- **No bench sitting, no jumper tests, no soak:** implementation only. OMs test it in the field after a release.
- **End of this campaign:** W5 committed, host gate green, local builds of the default envs. CI build and release only on a separate request.
- **Part B boards:** T-Beam, Heltec V3 and T-Deck Plus get free pins; every other ESP32 board offers none (default refuse); nRF52 answers "not on this board".

## Flow

W0 to W5 run one after the other on `fork-dev`, one writer at a time. Each wave: implementer, scoped checks, orchestrator gate, advisor for behaviour changes, commit, this file updated.

## Wave status

| Wave | Work                                                  | Status |
| ---- | ----------------------------------------------------- | ------ |
| W0   | Per-board pin table, setter audit, restart needs      | open   |
| W1   | `pin_policy.h`, sanitizer and setter checks           | open   |
| W2   | Part A: `/pinassign`, Pins page section               | open   |
| W3   | Part B: `--gpio`, `--gpiocontrol`, runtime            | open   |
| W4   | Part B: web table, guard, `/gpiostatus`               | open   |
| W5   | RM1 `pins` and `gpio`, RM page, sender tool, vectors  | open   |
| Gate | `tools/regression.sh --stage 1,2`, default envs built | open   |

## Wave log
