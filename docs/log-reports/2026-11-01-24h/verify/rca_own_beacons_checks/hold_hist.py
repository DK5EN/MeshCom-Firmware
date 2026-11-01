#!/usr/bin/env python3
"""Histogram of armed CSMA holds (RX_TIMEOUT_FIRE wait=) per reconstructed head class.
Reuses head_hazard's head reconstruction. Usage: python3 -I hold_hist.py <log>"""
from __future__ import annotations

import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import head_hazard as hh  # noqa: E402


def bucket(w: int) -> str:
    if w <= 100: return "<=100 rapid"
    if 150 <= w <= 220: return "150-220 caseA-short"
    if w <= 1500: return "221-1500 aged/other"
    if w <= 3000: return "1501-3000"
    if w <= 4000: return "3001-4000 p1/p2 base,caseA"
    if w <= 5000: return "4001-5000 p3 base/p4-5 x5/6"
    if w <= 6000: return "5001-6000 p4/p5 base"
    return ">6000"


def run(path: str) -> None:
    slots: dict[int, tuple] = {}
    pend = None
    hist: dict[str, Counter[str]] = defaultdict(Counter)
    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace")
            if not hh.TS.match(line):
                continue
            body = line[25:]
            if (m := hh.RTF.search(body)):
                if slots:
                    p, te, c, _ = min(slots.values(), key=lambda v: (v[0], v[1]))
                    h = c if c != "other" else f"other_p{p}"
                else:
                    h = "empty"
                hist[h][bucket(int(m.group(1)))] += 1
                continue
            if "RING_" not in body:
                continue
            if (m := hh.W.search(body)):
                pend = (int(m.group(1)), hh.CLASSES.get((m.group(2), m.group(4)), "other"), m.group(3), hh.ts(line[:23]))
            elif (m := hh.P.search(body)) and pend and int(m.group(1)) == pend[0]:
                slots[pend[0]] = (int(m.group(2)), pend[3], pend[1], pend[2]); pend = None
            elif (m := hh.TXR.search(body)) or (m := hh.STL.search(body)):
                s = int(m.group(1))
                if s in slots and slots[s][3] == m.group(2):
                    del slots[s]
    print(f"### {path}")
    for h, c in sorted(hist.items()):
        tot = sum(c.values())
        print(f"{h:12s} n={tot:6d} " + "  ".join(f"{k}:{v*100/tot:.0f}%" for k, v in sorted(c.items())))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        run(p)
