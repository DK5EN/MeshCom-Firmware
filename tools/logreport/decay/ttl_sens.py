#!/usr/bin/env python3
"""TTL sensitivity for prio 4 / prio 5 (DONE or own READY), per node + pooled over the selected nodes (key kept as "pooled_ia_ir_oc_wj" for schema stability).

wait(item) = real RING_TX_READ latency if sent; if dropped: wait of the item in the no-TTL replay (cf.py, model A),
censored at end if never served. Counting 'would be dropped under TTL=X' = items with wait > X (exact for X <= current TTL,
model-based for X > current). Writes work/ttl_sens.json."""
from __future__ import annotations

import json

from .common import NODES, work
from .cf import build, simulate

GRID = {4: [30, 45, 60, 90, 120, 180, 240, 300, 600], 5: [30, 60, 90, 120, 150, 180, 240, 300, 600]}
CUR = {4: 120, 5: 180}


def run(exclude: tuple[str, ...] = ()) -> None:
    out: dict = {}
    pooled = {4: [], 5: []}
    for node in [n for n in NODES if n not in exclude]:
        d, items, svc_t, svc_e = build(node)
        if not svc_t:
            continue
        t_end = svc_t[-1]
        s_off, sup_off, _, q_off, _ = simulate(items, svc_t, False, t_end)
        res = {}
        for p in (4, 5):
            waits = []
            for e in items:
                if e["prio"] != p:
                    continue
                if e["outcome"] == "sent":
                    waits.append((e["lat_ms"] / 1000.0, e["src"] in ("auto_pos", "user_wx"), "sent"))
                elif e["outcome"] == "dropped":
                    k = id(e)
                    if k in s_off:
                        waits.append((s_off[k] - e["t"], e["src"] in ("auto_pos", "user_wx"), "dropped_replay"))
                    elif k in sup_off:
                        waits.append((e["drop_age_s"] + 0.0, e["src"] in ("auto_pos", "user_wx"), "dropped_superseded"))
                    else:
                        waits.append((t_end - e["t"], e["src"] in ("auto_pos", "user_wx"), "dropped_censored"))
            tbl = {}
            for X in GRID[p]:
                over = [w for w in waits if w[0] > X]
                tbl[str(X)] = {"n_over": len(over), "own": sum(1 for w in over if w[1]),
                               "pct_of_items": round(100 * len(over) / len(waits), 2) if waits else 0.0}
            res[f"p{p}"] = {"n_items": len(waits), "ttl_now": CUR[p], "table": tbl}
            pooled[p] += waits
        out[node] = res
    out["pooled_ia_ir_oc_wj"] = {}
    for p in (4, 5):
        waits = pooled[p]
        tbl = {}
        for X in GRID[p]:
            over = [w for w in waits if w[0] > X]
            tbl[str(X)] = {"n_over": len(over), "own": sum(1 for w in over if w[1]),
                           "pct_of_items": round(100 * len(over) / len(waits), 2) if waits else 0.0}
        out["pooled_ia_ir_oc_wj"][f"p{p}"] = {"n_items": len(waits), "ttl_now": CUR[p], "table": tbl}
    with open(work() / "ttl_sens.json", "w") as fh:
        json.dump(out, fh, indent=1)
    for k, v in out.items():
        print(k)
        for p, r in v.items():
            print("  ", p, "n", r["n_items"], {x: (y["n_over"], y["own"]) for x, y in r["table"].items()})
