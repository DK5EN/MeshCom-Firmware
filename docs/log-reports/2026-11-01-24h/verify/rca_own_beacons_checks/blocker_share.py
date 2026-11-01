#!/usr/bin/env python3
"""While an entry of class C is alive, which class is the head? Time shares per
class C and per final outcome (tx / ttl / superseded). Usage: python3 -I blocker_share.py <log>"""
from __future__ import annotations

import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import head_hazard as hh  # noqa: E402

SUP = __import__("re").compile(r"\] RING_SUPERSEDE slot=(\d+) prio=\d old_id=([0-9A-F]+)")


def run(path: str) -> None:
    slots: dict[int, list] = {}  # slot -> [prio, t, cls, id, shares Counter]
    pend = None
    last_t = None
    agg: dict[tuple[str, str], Counter[str]] = defaultdict(Counter)
    selfs: dict[str, list[float]] = defaultdict(list)

    def close(s: int, outcome: str) -> None:
        e = slots.pop(s)
        agg[(e[2], outcome)].update(e[4])
        if outcome == "ttl" and e[2].startswith("own_"):
            selfs[e[2]].append(e[4].get("self", 0.0))

    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace")
            if not hh.TS.match(line):
                continue
            t = hh.ts(line[:23])
            if last_t is not None and slots and t > last_t:
                p, te, c, _, _ = min(slots.values(), key=lambda v: (v[0], v[1]))
                h = c if c != "other" else f"other_p{p}"
                dt = t - last_t
                for e in slots.values():
                    if e[2] in ("own_HEY", "own_WX", "relay_HEY", "relay_POS"):
                        e[4]["self" if (e[0], e[1]) == (p, te) else h] += dt
            last_t = t
            body = line[25:]
            if "RING_" not in body:
                continue
            if (m := hh.W.search(body)):
                pend = (int(m.group(1)), hh.CLASSES.get((m.group(2), m.group(4)), "other"), m.group(3), t)
                if pend[0] in slots:
                    slots[pend[0] + 1000] = slots.pop(pend[0])  # park old until SUPERSEDE/evict line
            elif (m := hh.P.search(body)) and pend and int(m.group(1)) == pend[0]:
                slots[pend[0]] = [int(m.group(2)), pend[3], pend[1], pend[2], Counter()]
                pend = None
            elif (m := SUP.search(body)):
                if int(m.group(1)) + 1000 in slots:
                    close(int(m.group(1)) + 1000, "superseded")
            elif (m := hh.TXR.search(body)):
                s = int(m.group(1))
                if s in slots and slots[s][3] == m.group(2):
                    close(s, "tx")
            elif (m := hh.STL.search(body)):
                s = int(m.group(1))
                if s in slots and slots[s][3] == m.group(2):
                    close(s, "ttl")
            for k in [k for k in slots if k >= 1000]:
                if pend is None:
                    close(k, "superseded_or_other")
    print(f"### {path}")
    for (c, o), cnt in sorted(agg.items()):
        tot = sum(cnt.values())
        if tot < 30:
            continue
        print(f"{c:10s} {o:10s} life_s={tot:8.0f} " + " ".join(f"{k}:{v*100/tot:.0f}%" for k, v in cnt.most_common(5)))

    for c, xs in sorted(selfs.items()):
        xs.sort()
        print(f"{c} ttl-dropped entries: n={len(xs)} seconds as head (self): p50={xs[len(xs)//2]:.0f}"
              f" >=5s:{sum(x >= 5 for x in xs)} >=10s:{sum(x >= 10 for x in xs)} zero(<1s):{sum(x < 1 for x in xs)}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        run(p)
