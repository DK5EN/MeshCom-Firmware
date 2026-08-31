# T-Deck bench experiment scripts (session 2026-08-28/29)

Scratch scripts used to find the lost-flush defect and the map crash. Each opens one USB session
(resets the device), waits for it to settle, sends `--` commands and prints the lines that matter.
Run from `tools/bench/runs/` so the `tdeck_run_*.log` files land there. Eye tests print a marker
per step; the operator reports one yes/no per run.

| Script                      | Purpose                                                                                                                                |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `maptest.py`                | Zoom 6 in / 6 out via `--mapzoom`, prints the `[MAP]` centring line per step                                                           |
| `crashtest.py`              | G01 reproducer: 4 foreign stations via `--injectpos`, 36 zoom steps under crash watch                                                  |
| `corr.py`                   | N muted/unmuted messages 6 s apart, full logging (operator counts bubbles)                                                             |
| `fixsd.py`                  | SD-only or normal message path with `--flushfix on` (the mitigation test)                                                              |
| `adbg.py`                   | `--audiodbg 1/2` split of the message path (tone only / SD only)                                                                       |
| `mutetest.py`               | muted vs tone messages                                                                                                                 |
| `splittest.py`              | `--sdtest` / `--playtone` before a muted message                                                                                       |
| `crctest.py`                | message flush CRC vs forced re-render CRC                                                                                              |
| `frametest.py`              | `--framedump` ASCII frame after each message + scroll geometry                                                                         |
| `bus.py`                    | `[BUS]` SPI2 register snapshot before flushes, muted vs SD-only path                                                                   |
| `blinktest.py`, `blink2.py` | `--blink` transfer probe (alternating inverted frames), with/without WiFi flood                                                        |
| `one.py`                    | one message, then optional extra commands (`--reflush`, `--invalidate`)                                                                |
| `monitor.py`                | 15-minute crash monitor: prints Guru/backtrace/reset events                                                                            |
| `bootloop.py`               | TM-34 WiFi arms: N power cycles by port-open, one CSV row of WiFi facts per boot                                                       |
| `apreboot.py`               | TM-38 AP-reboot recovery: four boards, detached, operator cycles the APs mid-run                                                       |
| `srvprobe.py`               | TM-39 country server probe: cycles `--gateway srv OE/DL/IT`, one KEEP/BEAT/UDP table per country, `--parse-only` re-reduces saved logs |

`maptest.py` and `crashtest.py` are folded into the `map` scenario of `tdeck_harness.py`
(2026-08-29); the flush-loss scripts are kept as the record of the defect hunt.

`bootloop.py` is the TM-34 runner, not a scratch script: it owns the port for
`--boots × --seconds` (24 × 75 s ≈ 30 min per arm), so check nothing else is on the node first
(`lsof /dev/cu.usbmodem1101`). `--parse-only <logs>` re-reduces existing logs without hardware.
Arms, metrics and acceptance bars: `docs/wifi-findings-20260829.md` §10.

`apreboot.py` is the TM-38 runner and the one script here that must survive the bench Mac losing
the network: the Mac is on the same APs the operator is about to power-cycle, so every interactive
session on it dies with them. `start` therefore double-forks into its own session (`setsid`, stdio
to files, `runner.pid`), owns all four USB ports for ~16 minutes, uses no network at all, and calls
the operator with a macOS notification plus a spoken line when it is time to cut the APs; `status`,
`stop` and the parse-only `report` are separate invocations that run afterwards. It reuses
`wifisoak.py`'s `[WIFI]` regexes and event fields and adds the `[UDP]`, `[NTP]` and `[ETH]` ones.
Regression tests: `python3 -m unittest tools/bench/experiments/test_apreboot.py` (synthetic
transcripts plus the phase machine end to end against a fake serial port, no hardware). Operator
runbook, pass criteria and prerequisites: `docs/bench-ap-reboot.md`.
- `ntpsync.py` — NTP-01: triggert --ntpsync in Schleife, misst ok/timeout + rtt-Verteilung (--parse-only fuer alte Logs)
