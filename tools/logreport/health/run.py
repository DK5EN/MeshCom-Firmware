#!/usr/bin/env python3
"""Node health baseline of a 24-h capture set: per-node baseline, beacons, strong signals, ACK attribution, ETH-04, RX gaps.

Usage: python3 -I tools/logreport/health/run.py --norm <dir with <node>.log> --out <dir> [--master <dir>]
                 [--standalone-start "YYYY-MM-DD HH:MM:SS"] [--strong-dbm -60] [--owncall node=CALL ...]

Node set = the *.log files in --norm. Logs with the logger wall-clock prefix form the cross-node cluster;
logs without it (PuTTY style) are analysed standalone and never correlated. Writes <out>/results.json and
the helper JSONs and findings.md next to it. --master supplies hardware/place/distance enrichment.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import os
import runpy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run_step(script: str, *args: str, verbose: bool) -> None:
    sys.argv = [script, *args]
    sink = io.StringIO()
    with contextlib.redirect_stdout(sys.stdout if verbose else sink):
        runpy.run_path(str(HERE / script), run_name="__main__")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--norm", required=True, type=Path, help="directory with <node>.log files")
    ap.add_argument("--out", required=True, type=Path, help="output directory (results.json, findings.md, helper JSONs)")
    ap.add_argument("--master", type=Path, help="directory with master.tsv / distances.tsv (optional)")
    ap.add_argument("--standalone-start", help="capture start of logs without wall clock, 'YYYY-MM-DD HH:MM:SS' (PC clock)")
    ap.add_argument("--strong-dbm", type=int, default=-60, help="strong-signal threshold in dBm (default -60)")
    ap.add_argument("--owncall", action="append", default=[], metavar="NODE=CALL", help="own callsign (default NODE-12)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args(argv)
    if not a.norm.is_dir():
        ap.error(f"--norm {a.norm} is not a directory")
    a.out.mkdir(parents=True, exist_ok=True)
    os.environ["LOGREPORT_NORM"] = str(a.norm.resolve())
    os.environ["LOGREPORT_OUT"] = str(a.out.resolve())
    os.environ["LOGREPORT_OWNCALLS"] = ",".join(a.owncall)
    if a.master:
        os.environ["LOGREPORT_MASTER"] = str(a.master.resolve())
    if a.standalone_start:
        os.environ["LOGREPORT_STANDALONE_START"] = a.standalone_start
    sys.path.insert(0, str(HERE))
    for m in ("nodeset", "common"):
        sys.modules.pop(m, None)
    import nodeset

    if not nodeset.ALL:
        ap.error(f"no *.log files in {a.norm}")
    v = a.verbose
    if nodeset.STANDALONE and not a.standalone_start:
        print(f"warning: standalone logs {nodeset.STANDALONE} without --standalone-start: using the default "
              f"{nodeset.STANDALONE_START} (the 2026-10-30 capture); pass the real PC-clock capture start", file=sys.stderr)
    (a.out / "results.json").unlink(missing_ok=True)  # node_baseline.py resumes from an existing results.json
    run_step("node_baseline.py", verbose=v)
    run_step("cet_beacons.py", verbose=v)
    run_step("strong_signals.py", str(a.strong_dbm), verbose=v)
    run_step("ack_attribution.py", verbose=v)
    for n in nodeset.STANDALONE:
        run_step("db0ais_eth.py", n, verbose=v)
    run_step("rx_gaps.py", verbose=v)
    run_step("build_report.py", verbose=v)
    print(f"health: wrote {a.out / 'results.json'}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
