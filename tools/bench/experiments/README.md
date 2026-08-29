# T-Deck bench experiment scripts (session 2026-08-28/29)

Scratch scripts used to find the lost-flush defect and the map crash. Each opens one USB session
(resets the device), waits for it to settle, sends `--` commands and prints the lines that matter.
Run from `tools/bench/runs/` so the `tdeck_run_*.log` files land there. Eye tests print a marker
per step; the operator reports one yes/no per run.

| Script                      | Purpose                                                                               |
| --------------------------- | ------------------------------------------------------------------------------------- |
| `maptest.py`                | Zoom 6 in / 6 out via `--mapzoom`, prints the `[MAP]` centring line per step          |
| `crashtest.py`              | G01 reproducer: 4 foreign stations via `--injectpos`, 36 zoom steps under crash watch |
| `corr.py`                   | N muted/unmuted messages 6 s apart, full logging (operator counts bubbles)            |
| `fixsd.py`                  | SD-only or normal message path with `--flushfix on` (the mitigation test)             |
| `adbg.py`                   | `--audiodbg 1/2` split of the message path (tone only / SD only)                      |
| `mutetest.py`               | muted vs tone messages                                                                |
| `splittest.py`              | `--sdtest` / `--playtone` before a muted message                                      |
| `crctest.py`                | message flush CRC vs forced re-render CRC                                             |
| `frametest.py`              | `--framedump` ASCII frame after each message + scroll geometry                        |
| `bus.py`                    | `[BUS]` SPI2 register snapshot before flushes, muted vs SD-only path                  |
| `blinktest.py`, `blink2.py` | `--blink` transfer probe (alternating inverted frames), with/without WiFi flood       |
| `one.py`                    | one message, then optional extra commands (`--reflush`, `--invalidate`)               |
| `monitor.py`                | 15-minute crash monitor: prints Guru/backtrace/reset events                           |

`maptest.py` and `crashtest.py` are folded into the `map` scenario of `tdeck_harness.py`
(2026-08-29); the flush-loss scripts are kept as the record of the defect hunt.
