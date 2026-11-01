#!/usr/bin/env python3
"""Gaps between consecutive OnRxDone events (logger clock, 'OnRxDone' lines) and the
share of gaps long enough for a given CSMA hold. Usage: python3 -I rx_gaps.py <log> ..."""
from __future__ import annotations

import sys
from datetime import datetime


def run(path: str) -> None:
    ts: list[float] = []
    with open(path, "rb") as fh:
        for raw in fh:
            if raw[25:33] == b"OnRxDone":
                ts.append(datetime.strptime(raw[:23].decode(), "%Y-%m-%d %H:%M:%S.%f").timestamp())
    gaps = [b - a for a, b in zip(ts, ts[1:]) if b - a < 600]
    n = len(gaps)
    print(f"### {path}  rxdone={len(ts)}  gaps<600s={n}  median_gap={sorted(gaps)[n//2]:.2f}s")
    for hold in (0.25, 1.6, 3.2, 4.6, 5.6, 6.0):
        # a hold of H fits if the silent gap after RX end exceeds H (gap measured end-to-end,
        # so it includes the next frame's airtime: an upper bound on the quiet time)
        share = sum(1 for g in gaps if g > hold) / n * 100
        print(f"  gaps > {hold:4.2f}s : {share:5.1f}%")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        run(p)
