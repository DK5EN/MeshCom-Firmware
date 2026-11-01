#!/usr/bin/env python3
"""Time-signal ({CET}) analysis of a 24-h capture set.

Usage: python3 -I tools/logreport/timesignal/run.py --norm <dir with <node>.log> --out <dir> [--master <dir>]
                 [--standalone-start "YYYY-MM-DD HH:MM:SS"] [--standalone-utc-off-h 2] [--logger-utc-off-s 3600] [--owncall node=CALL ...]

Node set = the *.log files in --norm. Logs with the logger wall-clock prefix form the cross-node cluster;
logs without it (PuTTY style) are analysed standalone and never correlated. Writes <out>/results.json and
the intermediates in <out>/data/. --master is accepted for interface symmetry and unused here.
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
    ap.add_argument("--out", required=True, type=Path, help="output directory (results.json + data/)")
    ap.add_argument("--master", type=Path, help="unused (interface symmetry)")
    ap.add_argument("--standalone-start", help="capture start of logs without wall clock, 'YYYY-MM-DD HH:MM:SS'")
    ap.add_argument("--standalone-utc-off-h", type=int, default=2, help="node clock minus UTC (h) at capture start")
    ap.add_argument("--logger-utc-off-s", type=int, default=3600, help="logger PC clock minus UTC in s (CET: 3600)")
    ap.add_argument("--owncall", action="append", default=[], metavar="NODE=CALL", help="own callsign (default NODE-12)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args(argv)
    if not a.norm.is_dir():
        ap.error(f"--norm {a.norm} is not a directory")
    (a.out / "data").mkdir(parents=True, exist_ok=True)
    os.environ["LOGREPORT_NORM"] = str(a.norm.resolve())
    os.environ["LOGREPORT_OUT"] = str(a.out.resolve())
    os.environ["LOGREPORT_LOGGER_UTC_OFF_S"] = str(a.logger_utc_off_s)
    os.environ["LOGREPORT_OWNCALLS"] = ",".join(a.owncall)
    if a.standalone_start:
        os.environ["LOGREPORT_STANDALONE_START"] = a.standalone_start
    os.environ["LOGREPORT_STANDALONE_UTC_OFF_H"] = str(a.standalone_utc_off_h)
    sys.path.insert(0, str(HERE))
    for m in ("nodeset", "common"):
        sys.modules.pop(m, None)
    import nodeset

    if not nodeset.ALL:
        ap.error(f"no *.log files in {a.norm}")
    v = a.verbose
    if nodeset.STANDALONE and not a.standalone_start:
        print(f"warning: standalone logs {nodeset.STANDALONE} without --standalone-start: using the default "
              f"{nodeset.STANDALONE_START} (date of the 2026-10-30 capture); pass the real capture start", file=sys.stderr)
    for n in nodeset.ALL:
        run_step("extract_cet.py", n, verbose=v)
        run_step("rawpass.py", n, verbose=v)
    if nodeset.CLUSTER:
        run_step("analyze_oe3.py", verbose=v)
        run_step("oe3_extra.py", verbose=v)
        run_step("clock_offsets.py", verbose=v)
    for n in nodeset.STANDALONE:
        run_step("analyze_db.py", n, verbose=v)
        run_step("db_clock.py", n, verbose=v)
        run_step("db_phase.py", n, verbose=v)
    run_step("build_results.py", verbose=v)
    print(f"timesignal: wrote {a.out / 'results.json'}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
