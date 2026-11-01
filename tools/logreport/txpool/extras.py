#!/usr/bin/env python3
"""Q2 extras: own-HEY air gaps, ACK age-floor share, cross-node relay latency (OE3 only).

Reads <outdir>/<node>.reads.json written by the pipeline ([id, read_ts, lat_ms, prio, src, type, fate]).
Cross-node statistics use the wall-clock nodes only (nodes without a logger clock are per-node only).
"""
from __future__ import annotations

import json
from collections import defaultdict
from statistics import median
from typing import Any



def pct(vals: list[float], p: float) -> float | None:
    if not vals:
        return None
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    f = int(k)
    c = min(f + 1, len(s) - 1)
    return s[f] + (s[c] - s[f]) * (k - f)


def compute(out: str, NODES: list[str], ALL: list[str]) -> dict[str, Any]:
    res: dict[str, Any] = {"nodes": {}, "cross": {}}
    reads: dict[str, list[list[Any]]] = {}
    for n in ALL:
        with open(f"{out}/{n}.reads.json") as fh:
            reads[n] = json.load(fh)
    for n in ALL:
        r = reads[n]
        d: dict[str, Any] = {}
        # own HEY gaps (auto_pos, type 40)
        ts = sorted(x[1] for x in r if x[4] == "auto_pos")
        gaps = [b - a for a, b in zip(ts, ts[1:])]
        d["own_hey_aired"] = len(ts)
        d["own_hey_gap_s"] = {"p50": pct(gaps, 50), "p90": pct(gaps, 90), "max": max(gaps) if gaps else None}
        d["own_hey_gaps_over_20min"] = sum(1 for g in gaps if g > 1200)
        # age floor share for ACK/relay prio 1..3
        for p in (1, 3):
            lat = [x[2] / 1000.0 for x in r if x[3] == p]
            d[f"prio{p}_ge20s_share"] = (sum(1 for v in lat if v >= 20) / len(lat)) if lat else None
            d[f"prio{p}_ge60s_share"] = (sum(1 for v in lat if v >= 60) / len(lat)) if lat else None
        # ACK (rx_ack_fwd) latency again, plus histogram buckets
        lat = [x[2] / 1000.0 for x in r if x[4] == "rx_ack_fwd"]
        bk = {"<5": 0, "5-10": 0, "10-20": 0, "20-30": 0, "30-60": 0, ">=60": 0}
        for v in lat:
            k = "<5" if v < 5 else "5-10" if v < 10 else "10-20" if v < 20 else "20-30" if v < 30 else "30-60" if v < 60 else ">=60"
            bk[k] += 1
        d["rx_ack_fwd_hist"] = bk
        d["rx_ack_fwd_n"] = len(lat)
        res["nodes"][n] = d

    # cross node: rx_relay entries by id, within 30 min of the first, OE3 only
    by_id: dict[str, list[tuple[str, float, float, int]]] = defaultdict(list)
    for n in NODES:
        for (mid, t, lat, prio, src, typ, fate) in reads[n]:
            if src in ("rx_relay",):
                by_id[mid].append((n, t, lat / 1000.0, prio))
    groups = []
    for mid, lst in by_id.items():
        lst.sort(key=lambda x: x[1])
        # split into clusters of events within 30 min of the first member
        cur: list[tuple[str, float, float, int]] = []
        for it in lst:
            if cur and it[1] - cur[0][1] > 1800:
                groups.append(cur)
                cur = []
            cur.append(it)
        if cur:
            groups.append(cur)
    # keep one entry per node per group (first)
    multi = []
    for g in groups:
        seen: dict[str, tuple[str, float, float, int]] = {}
        for it in g:
            if it[0] not in seen:
                seen[it[0]] = it
        if len(seen) >= 2:
            multi.append(list(seen.values()))
    res["cross"]["groups_ge2"] = len(multi)
    res["cross"]["groups_ge3"] = sum(1 for g in multi if len(g) >= 3)
    res["cross"]["groups_ge4"] = sum(1 for g in multi if len(g) >= 4)
    ranks: dict[str, list[float]] = defaultdict(list)
    rel: dict[str, list[float]] = defaultdict(list)
    slowest: dict[str, int] = defaultdict(int)
    fastest: dict[str, int] = defaultdict(int)
    pernode: dict[str, list[float]] = defaultdict(list)
    for g in multi:
        if len(g) < 3:
            continue
        lats = sorted(g, key=lambda x: x[2])
        m = median([x[2] for x in g])
        for i, it in enumerate(lats):
            ranks[it[0]].append(i / (len(g) - 1))
            rel[it[0]].append(it[2] - m)
        slowest[lats[-1][0]] += 1
        fastest[lats[0][0]] += 1
    for n in NODES:
        pernode[n] = [x[2] for g in multi for x in g if x[0] == n]
    res["cross"]["per_node"] = {
        n: {"n_in_groups_ge3": len(ranks[n]), "mean_rank_0fast_1slow": (sum(ranks[n]) / len(ranks[n])) if ranks[n] else None,
            "median_delta_to_group_median_s": median(rel[n]) if rel[n] else None,
            "slowest_count": slowest[n], "fastest_count": fastest[n],
            "lat_p50_in_groups_s": pct(pernode[n], 50), "lat_p90_in_groups_s": pct(pernode[n], 90)}
        for n in NODES}
    # by prio class: groups of prio 5 vs 3-4
    for label, pset in (("prio5", (5,)), ("prio3_4", (3, 4))):
        rk: dict[str, list[float]] = defaultdict(list)
        for g in multi:
            gg = [x for x in g if x[3] in pset]
            if len(gg) < 3:
                continue
            ls = sorted(gg, key=lambda x: x[2])
            for i, it in enumerate(ls):
                rk[it[0]].append(i / (len(gg) - 1))
        res["cross"][f"mean_rank_{label}"] = {n: (round(sum(v) / len(v), 3), len(v)) for n, v in rk.items()}
    # pairwise: median delta lat(A)-lat(B) for shared ids
    pair: dict[str, list[float]] = defaultdict(list)
    for g in multi:
        d = {x[0]: x[2] for x in g}
        for a in NODES:
            for b in NODES:
                if a < b and a in d and b in d:
                    pair[f"{a}-{b}"].append(d[a] - d[b])
    res["cross"]["pairwise_median_delta_s"] = {k: (round(median(v), 2), len(v)) for k, v in pair.items() if len(v) >= 20}
    return res
