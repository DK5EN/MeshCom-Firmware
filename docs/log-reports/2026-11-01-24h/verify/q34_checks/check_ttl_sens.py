#!/usr/bin/env python3
"""TTL sensitivity (first order) and headroom: per prio, entries whose observed wait (TX_READ lat for sent,
age for dropped) exceeds a candidate TTL. Pooled over oe3xia, oe3xir, oe3xoc, oe3xwj (hl has cancel path).
Superseded entries are excluded. For TTL < current the count is a first-order upper bound (an earlier drop
frees the TX instant the frame used, so others would wait less); for TTL > current the dropped entries'
fate is unknown (counted as over if age > T only if they were dropped below T - i.e. not countable).

Usage: python3 -I check_ttl_sens.py <ev.json from parse_ring.py>
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict

POOL = ["oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]


def main() -> None:
    ev = json.load(open(sys.argv[1]))
    items = defaultdict(list)  # prio -> (wait_s, dropped, own)
    hd = defaultdict(list)
    for n in ev:
        e = ev[n]
        for r in e["read"]:
            hd[r[2]].append((r[8] / 1000, n))
    for n in POOL:
        e = ev[n]
        wsrc = {}
        for w in e["write"]:
            wsrc[(w[5], w[1])] = w[7]
        for r in e["read"]:
            if r[2] in (4, 5):
                items[r[2]].append((r[8] / 1000, False, wsrc.get((r[6], r[1]), "") in ("auto_pos", "user_wx")))
        for d in e["drop"]:
            if d[2] in (4, 5):
                items[d[2]].append((float(d[3]), True, wsrc.get((d[4], d[1]), "") in ("auto_pos", "user_wx")))
    for p, cur in ((5, 180), (4, 120)):
        L = items[p]
        nd = sum(1 for x in L if x[1])
        print(f"prio {p}: n={len(L)} (dropped {nd}, own dropped {sum(1 for x in L if x[1] and x[2])})")
        for T in (60, 90, 120, 150, 180, 240, 300):
            over = sum(1 for x in L if x[0] > T)
            own = sum(1 for x in L if x[0] > T and x[2])
            sent_over = sum(1 for x in L if x[0] > T and not x[1])
            note = "" if T <= cur else " (sent only; dropped entries' fate unknown -> model needed)"
            if T <= cur:
                print(f"  TTL {T}: {over} ({100*over/len(L):.1f} %), own {own}")
            else:
                print(f"  TTL {T}: sent entries with wait > T: {sent_over}{note}")
    for p in (1, 2, 3, 4, 5):
        L = hd[p]
        if L:
            m = max(L)
            print(f"headroom prio {p}: sent {len(L)} max wait {m[0]:.1f} s ({m[1]})")


if __name__ == "__main__":
    main()
