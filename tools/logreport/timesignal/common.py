"""Shared loaders for the time-signal analysis (events_*.jsonl written by extract_cet.py)."""
from __future__ import annotations

import json
import re
from calendar import timegm

from nodeset import ALL, CLUSTER, LOGGER_UTC_OFF_S, OWNCALL, STANDALONE  # noqa: F401


def load(data_dir: str, node: str) -> list[dict]:
    ev = []
    with open(f"{data_dir}/events_{node}.jsonl") as f:
        for line in f:
            ev.append(json.loads(line))
    return ev


def cont_epoch(c: str):
    """'2026-10-31 05:51:20' -> UTC epoch (content is UTC despite the {CET} name)."""
    m = re.match(r"(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)$", c)
    if not m:
        return None
    y, mo, d, h, mi, s = map(int, m.groups())
    return timegm((y, mo, d, h, mi, s))


def pct(v, p):
    s = sorted(v)
    if not s:
        return None
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def stats(v, nd=1):
    v = list(v)
    if not v:
        return None
    return {"n": len(v), "min": round(min(v), nd), "p50": round(pct(v, 50), nd), "p90": round(pct(v, 90), nd),
            "p99": round(pct(v, 99), nd), "max": round(max(v), nd)}


def udp_arrivals(ev):
    """{id: t} of gateway UDP injection (RING_WRITE src=udp_rx, falls back to RX-UDP line)."""
    r = {}
    for e in ev:
        if e["k"] == "ring" and "RING_WRITE" in e["line"] and "src=udp_rx" in e["line"]:
            r.setdefault(e["ids"][0], e["t"])
        elif e["k"] == "udp":
            r.setdefault(e["id"], e["t"])
    return r
