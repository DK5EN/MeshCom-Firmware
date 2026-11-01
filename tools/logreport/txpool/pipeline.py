"""TX pool report pipeline: <norm>/<node>.log -> <out>/results.json (+ per-node intermediates)."""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from . import extras, heychain, pool

_WALL = re.compile(rb"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}  ")


def has_wall_clock(path: Path) -> bool:
    """True when the log lines carry the logger wall-clock prefix (checked on the first 200 lines)."""
    with path.open("rb") as fh:
        for i, raw in enumerate(fh):
            if _WALL.match(raw):
                return True
            if i >= 200:
                break
    return False


def run(norm: Path, out: Path, clock_profiles: dict[str, Any], annotations: dict[str, Any]) -> dict[str, Any]:
    out.mkdir(parents=True, exist_ok=True)
    logs = sorted(norm.glob("*.log"))
    if not logs:
        raise SystemExit(f"no *.log files in {norm}")
    clocked = [p.stem for p in logs if has_wall_clock(p)]
    unclocked = [p.stem for p in logs if p.stem not in clocked]
    res: dict[str, Any] = {"nodes": {}, "network": {}}
    for p in logs:
        node = p.stem
        clock = None if node in clocked else dict(clock_profiles.get(node, {}))
        R = pool.analyse(node, str(p), clock)
        summ = pool.summarise(node, R)
        with (out / f"{node}.json").open("w") as fh:
            json.dump(summ, fh, indent=1, default=str)
        pool.dump_events(node, R, str(out / f"{node}.reads.json"))
        summ["hey_chains"] = heychain.chains_for(R["entries"])
        res["nodes"][node] = summ
        print(node, "ok", summ["fate"]["total_writes"], "writes", summ["fate"]["counts"], flush=True)
        del R
    ex = extras.compute(str(out), clocked, clocked + unclocked)
    with (out / "extras.json").open("w") as fh:
        json.dump(ex, fh, indent=1)
    for n in res["nodes"]:
        res["nodes"][n]["extras"] = ex["nodes"][n]
    fate: Counter[str] = Counter()
    tot = 0
    for n in clocked:
        for k, v in res["nodes"][n]["fate"]["counts"].items():
            fate[k] += v
        tot += res["nodes"][n]["fate"]["total_writes"]
    # key names keep the "oe3_" prefix of the first report (schema stays stable): pooled over wall-clock nodes
    res["network"]["oe3_fate_counts"] = dict(fate)
    res["network"]["oe3_fate_pct"] = {k: round(100.0 * v / tot, 2) for k, v in fate.items()} if tot else {}
    res["network"]["oe3_total_writes"] = tot
    res["network"]["cross_node"] = ex["cross"]
    res["network"].update(annotations.get("network", {}))
    with (out / "results.json").open("w") as fh:
        json.dump(res, fh, indent=1, default=str)
    return res
