#!/usr/bin/env python3
"""TX pool (TXP) analysis: RING_* state machine, fate shares, latency, supersede, cross-node relay latency.

    python3 -I tools/logreport/txpool/run.py --norm <dir with <node>.log> --out <dir> \
        [--clock-profiles clock_profiles.json] [--annotations annotations.json]

Node set = every *.log in --norm. Logs with the logger wall-clock prefix are the "clocked" set (pooled,
cross-node); logs without it (e.g. a PuTTY capture) are analysed per node from the `[LOG] HH:MM:SS` clock,
using an optional clock profile (see pool.analyse). --annotations merges hand-written judgement
(network.verdict ...) into results.json; without it the file holds measured data only.
Writes results.json plus <node>.json, <node>.reads.json, extras.json into --out.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from logreport.txpool import pipeline  # noqa: E402


def _load(p: str | None) -> dict[str, Any]:
    if not p:
        return {}
    with open(p) as fh:
        return json.load(fh)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--clock-profiles", help="JSON {node: profile} for logs without wall-clock prefix")
    ap.add_argument("--annotations", help="JSON merged into results.json (network.* keys)")
    a = ap.parse_args(argv)
    res = pipeline.run(a.norm, a.out, _load(a.clock_profiles), _load(a.annotations))
    print("written", a.out / "results.json", "nodes:", ", ".join(res["nodes"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
