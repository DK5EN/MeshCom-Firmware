#!/usr/bin/env python3
"""TTL decay / CSMA analysis: drop tables, loss classification, counterfactual replay, TTL sensitivity.

    python3 -I tools/logreport/decay/run.py --norm <dir with <node>.log> --out <dir> \
        [--ttl-exclude oe3xhl] [--annotations annotations.json]

Node set = every *.log in --norm that carries the logger wall-clock prefix (day 0 = date of the earliest
line). Logs without that prefix only contribute the per-prio TX latency / stale counters
(network.<node>). --ttl-exclude drops nodes from the TTL-sensitivity replay (the first report excluded
the node whose nbrrelay CANCEL path the model does not cover). --annotations merges hand-written
`cet_manual` / `examples` into network.cet / network.examples. Intermediates go to <out>/work (pickles
are large and can be deleted afterwards); the result is <out>/results.json.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from logreport.decay import pipeline  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--ttl-exclude", default="", help="comma list of nodes left out of the TTL-sensitivity replay")
    ap.add_argument("--annotations", help="JSON with cet_manual / examples")
    a = ap.parse_args(argv)
    ann: dict[str, Any] = {}
    if a.annotations:
        with open(a.annotations) as fh:
            ann = json.load(fh)
    a.out.mkdir(parents=True, exist_ok=True)
    excl = tuple(x for x in a.ttl_exclude.split(",") if x)
    res = pipeline.run(a.norm, a.out, ann, excl)
    print("written", a.out / "results.json", "nodes:", ", ".join(res["nodes"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
