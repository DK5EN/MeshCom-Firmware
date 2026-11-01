#!/usr/bin/env python3
"""Manipulation ledger analysis (relay fate, cancels, loops, redundancy, link probability) of a 24-h capture set.

Usage: python3 -I tools/logreport/manipulations/run.py --norm <dir with <node>.log> --out <dir> [--master <dir>]
                 [--owncall node=CALL ...]

Node set = the *.log files in --norm. Logs with the logger wall-clock prefix form the cross-node cluster;
logs without it (PuTTY style) are analysed standalone and never correlated. Writes <out>/results.json and
the intermediates in <out>/work/. --master is accepted for interface symmetry and unused.
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
# per-node steps (cluster and standalone nodes); order matters where a step reads another's work/*.json
STEPS = ["ledger.py", "statagg.py", "opportunity.py", "alone.py", "alone2.py", "loops.py", "ownfate.py", "txwait.py",
         "cet.py", "ackfwd.py"]
# cross-node steps: only meaningful for logs with the logger wall clock
CLUSTER_STEPS = ["alone3.py", "bounds.py", "redundancy.py", "redundancy2.py", "linkp.py", "cancel_eval.py", "loss.py",
                 "gateway.py", "pingshare.py", "clockcheck.py"]


def run_step(script: str, *args: str, verbose: bool) -> None:
    sys.argv = [script, *args]
    sink = io.StringIO()
    with contextlib.redirect_stdout(sys.stdout if verbose else sink):
        try:
            runpy.run_path(str(HERE / script), run_name="__main__")
        except SystemExit as exc:  # a step may end early (nothing to analyse)
            if exc.code not in (None, 0):
                raise


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--norm", required=True, type=Path, help="directory with <node>.log files")
    ap.add_argument("--out", required=True, type=Path, help="output directory (results.json + work/)")
    ap.add_argument("--master", type=Path, help="unused (interface symmetry)")
    ap.add_argument("--owncall", action="append", default=[], metavar="NODE=CALL", help="own callsign (default NODE-12)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args(argv)
    if not a.norm.is_dir():
        ap.error(f"--norm {a.norm} is not a directory")
    (a.out / "work").mkdir(parents=True, exist_ok=True)
    os.environ["LOGREPORT_NORM"] = str(a.norm.resolve())
    os.environ["LOGREPORT_OUT"] = str(a.out.resolve())
    os.environ["LOGREPORT_OWNCALLS"] = ",".join(a.owncall)
    sys.path.insert(0, str(HERE))
    for m in ("nodeset", "common"):
        sys.modules.pop(m, None)
    import nodeset

    if not nodeset.ALL:
        ap.error(f"no *.log files in {a.norm}")
    v = a.verbose
    run_step("parse.py", verbose=v)
    for step in STEPS:
        run_step(step, verbose=v)
    if nodeset.CLUSTER:
        for step in CLUSTER_STEPS:
            run_step(step, verbose=v)
    run_step("aggregate.py", verbose=v)
    print(f"manipulations: wrote {a.out / 'results.json'}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
