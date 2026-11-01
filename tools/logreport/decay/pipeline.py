"""Decay report pipeline: <norm>/<node>.log -> <out>/results.json (intermediates in <out>/work)."""

from __future__ import annotations

import datetime as dt
import json
import pickle
from pathlib import Path
from typing import Any

from . import analyze_a, cf, common, csma, drops, merge_results, parse, pooled, ttl_sens


def _nan_to_none(o: Any) -> Any:
    """NaN (percentile of an empty list) is not valid JSON; an empty sample is null."""
    if isinstance(o, float) and o != o:
        return None
    if isinstance(o, dict):
        return {k: _nan_to_none(v) for k, v in o.items()}
    if isinstance(o, list):
        return [_nan_to_none(v) for v in o]
    return o


def run(norm: Path, out: Path, annotations: dict[str, Any], ttl_exclude: tuple[str, ...] = ()) -> dict[str, Any]:
    logs = sorted(norm.glob("*.log"))
    if not logs:
        raise SystemExit(f"no *.log files in {norm}")
    dates = {p.stem: parse.first_date(p) for p in logs}
    clocked = [n for n, d in dates.items() if d is not None]
    unclocked = {p.stem: p for p in logs if dates[p.stem] is None}
    if not clocked:
        raise SystemExit("no log with a logger wall-clock prefix; the decay analysis needs at least one")
    base = min(d for d in dates.values() if d is not None)
    work = out / "work"
    common.configure(work, clocked, base)
    no_log: list[str] = []
    for node in clocked:
        d = parse.parse(norm / f"{node}.log", base)
        with (work / f"{node}.pkl").open("wb") as fh:
            pickle.dump(d, fh)
        print(node, "parsed", d["_lines"], "lines", flush=True)
        if not d["logtx"] or not d["logrx"]:
            no_log.append(node)
    # nodes without [LOG] TX/RX lines (no --setlog on) cannot be replayed: only their ring counters count
    for node in no_log:
        unclocked[node] = norm / f"{node}.log"
    clocked = [n for n in clocked if n not in no_log]
    if not clocked:
        raise SystemExit("no log with [LOG] TX/RX lines (--setlog on); the decay analysis needs at least one")
    common.configure(work, clocked, base)
    drops.run()
    analyze_a.run()
    cf.run()
    ttl_sens.run(ttl_exclude)
    csma.run()
    res = merge_results.run(out, unclocked, base.isoformat() if isinstance(base, dt.date) else str(base))
    if no_log:
        res["unavailable"] = {n: "no [LOG] TX/RX lines in the capture (--setlog on missing): drop table, loss "
                                 "classification, counterfactual replay and TTL sensitivity not computable; "
                                 "network.%s carries the ring counters only" % n for n in no_log}
    pooled.append_pooled(res, annotations)
    res = _nan_to_none(res)
    with (out / "results.json").open("w") as fh:
        json.dump(res, fh, indent=1)
    return res
