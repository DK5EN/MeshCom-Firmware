# logreport - multi-node MeshCom log analysis and HTML reports

Turns a set of parallel 24-h serial captures (`--setlog on` + `--loradebug on`, `[NBR]` lines) into measured data
and self-contained HTML reports (German, light/dark, animated explainer drawings), with every claim checked
adversarially against the raw logs. First run: `docs/log-reports/2026-11-01-24h/` (five OE3 gateways plus DB0AIS).

## Layout

| Path                           | What                                                                                    |
| ------------------------------ | --------------------------------------------------------------------------------------- |
| `run.py`                       | Runner: normalize, extract, render                                                      |
| `reportkit.py`                 | Shared HTML/CSS/SVG kit: theme tokens, chips, tables, charts, `explain()` drawings      |
| `txpool/`                      | F2 TX slot pool: occupancy, latency, fate accounting, supersede, cross-node ranking     |
| `decay/`                       | F3/F4 TTL and {CET} gate drops, loss test, counterfactual replay, CSMA age floor        |
| `timesignal/`                  | F5 {CET} time signal: cadence, radiations per telegram, age, stale relays, clocks       |
| `manipulations/`               | F6 relay decisions, overhear-cancel, redundancy, ping/ACK load, loops, gateway inject   |
| `health/`                      | Per-node baseline: traffic, noise floor, CAD, RX silences, interferers, ETH, census     |
| `master/merge.py`              | Merge saved mcmap `nodes_query` tiles into master.tsv / distances.tsv / coverage.txt    |
| `briefs/`                      | Agent briefs used by the workflow below (common, report, addendum, verify, rework)      |
| `../tests/test_logreport_*.py` | Pytest on small fixture logs (`../tests/fixtures/logreport/`), part of the stage-2 gate |

Each topic runs alone as `python3 -I tools/logreport/<topic>/run.py --norm <dir> --out <dir> [topic flags]` and
writes `results.json` plus intermediates. Stdlib only.

## Run directory (`docs/log-reports/<run-id>/`)

| Path                    | What                                                                                                                    |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `*.html`                | The reports: one per node plus `uebersicht.html` (network view). Self-contained.                                        |
| `logreport.json`        | Per-run config: node-key overrides (`names`) and per-topic extra flags (`topics`)                                       |
| `config/`               | Per-run inputs referenced by `logreport.json` (clock profiles, annotations)                                             |
| `data/<q>/`             | `results.json` per topic (q2 txpool, q34 decay, q5 timesignal, q6 manipulations, health) and the verified `findings.md` |
| `data/master/`          | Master data (mcmap)                                                                                                     |
| `data/extra/`           | Page-specific measurements produced by generator helpers                                                                |
| `data/CORE-FINDINGS.md` | The verified headline findings all pages must agree with                                                                |
| `generators/`           | One generator per page (`<page>.py` -> `<page>.html`), helpers in `generators/lib/`                                     |
| `verify/`               | Adversarial verification tables and their check scripts                                                                 |

Raw logs stay in `logs/` (gitignored); normalised copies and intermediates go to `<logs>/.logreport-work/`.

## Re-running on a new capture

1. Put the captures into `logs/<capture>/`. Create `docs/log-reports/<run-id>/logreport.json` (copy the
   2026-11-01 one; adjust `names`, drop topic flags that were specific to that capture, set
   `--standalone-start` for captures without a wall-clock prefix, e.g. PuTTY logs).
2. `python3 -I tools/logreport/run.py --logs logs/<capture> --run docs/log-reports/<run-id> --steps normalize,extract`
3. Master data: query mcmap `nodes_query` by bbox tiles (see `master/merge.py` docstring), save the JSONs, run
   `master/merge.py` into `data/master/`.
4. Judgement and pages (agent workflow, `/orchestrate-waves`):
   - Wave 1: one analysis agent per topic reads `results.json` + raw logs and writes `data/<q>/findings.md`
     (`briefs/common.md`).
   - Wave 2: one writer per page builds `generators/<page>.py` (`briefs/report.md` + `report-addendum.md`).
   - Wave 3: one Opus verifier per topic tries to refute every claim (`briefs/verify.md`) into `verify/`;
     root-cause questions that survive get their own RCA; `data/CORE-FINDINGS.md` is rewritten from the verdicts.
   - Wave 4: one rework agent per page applies the corrections (`briefs/rework.md`); visual check in Chrome,
     light and dark, desktop and phone width.
5. `python3 -I tools/logreport/run.py --run docs/log-reports/<run-id> --steps render`

## Known limits

- The topic code was distilled from the first run. `health/build_report.py` writes its narrative findings.md
  only for the 2026-11-01 node set (other sets get a stub table; `results.json` is always complete).
- Logs without a wall-clock prefix (PuTTY) need `--standalone-start` (timesignal, health) and a txpool clock
  profile; the defaults fit the 2026-10-30 DB0AIS capture only and print a warning.
- `manipulations` uses the host time zone for hour buckets; run with the same `TZ` to compare runs.
- Own callsign defaults to `<NODE>-12`; pass `--owncall NODE=CALL` otherwise.
- RX statistics must count ACK receptions: 12-byte ACK frames print no `OnRxDone` line.
- A MessageID is unique only for about 4 h; cross-node matching uses windows of at most 30 min.
