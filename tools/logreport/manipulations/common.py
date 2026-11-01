"""Shared loaders for the Q6 analysis scripts."""
from __future__ import annotations

import json

from nodeset import ALL, NOLOG, NORM, OUT, OWNCALL, STANDALONE  # noqa: F401
from nodeset import CLUSTER as NODES  # noqa: F401

WORK = OUT / "work"
OUTD = OUT
CALL = dict(OWNCALL)
CALL2NODE = {v: k for k, v in CALL.items() if k not in STANDALONE}


def load(node: str) -> list[dict]:
    out = []
    with open(WORK / f"{node}.jsonl") as fh:
        for line in fh:
            out.append(json.loads(line))
    return out


def cancel_node() -> str:
    """The cluster node with the most overhear-cancel events (first node on a tie / none)."""
    return max(NODES, key=lambda m: sum(1 for e in load(m) if e['k'] == 'cancel'))


def hour_of(t: float) -> str:
    import time
    return time.strftime("%m-%d %H", time.localtime(t))


def is_stat_time(t: float) -> bool:
    return True


def median(xs: list[float]) -> float:
    xs = sorted(xs)
    n = len(xs)
    if n == 0:
        return float("nan")
    return xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2


def pct(a: float, b: float) -> float:
    return round(100.0 * a / b, 2) if b else 0.0
