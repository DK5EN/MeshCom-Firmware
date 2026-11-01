#!/usr/bin/env python3
"""Own-HEY (src=auto_pos) and relay-HEY supersede chains: time from first write to air / loss.

A chain = entries linked by RING_SUPERSEDE (old -> new in place). TTL age restarts at each supersede
(addTxRingEntryCore sets ringEnqueueTime[w]=millis()), so chain age can exceed the 180 s HEY TTL.
"""
from __future__ import annotations
from collections import Counter
from typing import Any

from . import pool as txpool


def pct(v: list[float], p: float) -> float | None:
    return txpool.pct(v, p)


def chains_for(entries: list[Any]) -> dict[str, Any]:
    """Supersede-chain statistics from the entry list returned by pool.analyse()."""
    prev: dict[int, Any] = {}
    for e in entries:
        if e.sup_by is not None:
            prev[id(e.sup_by)] = e
    res = {}
    for label, srcs in (("own_hey", ("auto_pos",)), ("relay_hey", ("rx_relay",))):
        chains: list[dict[str, Any]] = []
        for e in entries:
            if e.src not in srcs or e.type != "40":
                continue
            if e.fate == "superseded":
                continue            # not a chain tail
            n = 1
            first = e
            cur = e
            while id(cur) in prev:
                cur = prev[id(cur)]
                n += 1
                first = cur
            if n == 0:
                continue
            end_t = (e.read_ts if e.fate == "tx_air" else e.fate_ts) or e.ts
            chains.append({"len": n, "fate": e.fate, "span_s": end_t - first.ts, "start": first.ts})
        if not chains:
            res[label] = {"n": 0}
            continue
        fc = Counter(c["fate"] for c in chains)
        aired = [c for c in chains if c["fate"] == "tx_air"]
        lost = [c for c in chains if c["fate"] not in ("tx_air",)]
        res[label] = {
            "chains": len(chains), "fate": dict(fc),
            "aired_pct": round(100.0 * len(aired) / len(chains), 1),
            "chain_len_ge2": sum(1 for c in chains if c["len"] >= 2),
            "chain_len_max": max(c["len"] for c in chains),
            "span_s_aired": {"p50": pct([c["span_s"] for c in aired], 50), "p90": pct([c["span_s"] for c in aired], 90),
                              "max": max((c["span_s"] for c in aired), default=None)},
            "span_s_lost": {"p50": pct([c["span_s"] for c in lost], 50), "max": max((c["span_s"] for c in lost), default=None)},
            "span_over_180s": sum(1 for c in chains if c["span_s"] > 190),
            "longest": sorted([(round(c["span_s"]), c["len"], c["fate"], txpool.fmt_t(c["start"])) for c in chains], reverse=True)[:5],
        }
    return res
