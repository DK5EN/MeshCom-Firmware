#!/usr/bin/env python3
"""For every TTL drop: how much of its lifetime was the entry at the head of the queue (no other pending
entry with lower prio value, or same prio and older)? Slot occupancy reconstructed from RING_WRITE (start),
RING_TX_READ / RING_DROP_STALE / next RING_WRITE of the same slot (end). Also: node TX count and longest
TX-free gap inside the drop's lifetime. Tests whether drops are "starvation behind higher prio" or
"head-of-queue but no TX opportunity" (CSMA hold 5.5 s for prio 4/5, re-armed per received frame).

Usage: python3 -I check_headtime.py <ev.json from parse_ring.py>
"""
from __future__ import annotations

import bisect
import json
import sys
from statistics import median


def main() -> None:
    ev = json.load(open(sys.argv[1]))
    allfr = []
    for n in ("oe3xia", "oe3xoc", "oe3xir", "oe3xwj"):
        e = ev[n]
        ends: list[tuple[float, int]] = [(r[0], r[1]) for r in e["read"]] + [(d[0], d[1]) for d in e["drop"]]
        ends += [(w[0] - 1e-4, w[1]) for w in e["write"]]
        ends.sort()
        endt_by_slot: dict[int, list[float]] = {}
        for t, s in ends:
            endt_by_slot.setdefault(s, []).append(t)
        ents = []
        for w in e["write"]:
            lst = endt_by_slot.get(w[1], [])
            i = bisect.bisect_right(lst, w[0])
            te = lst[i] if i < len(lst) else w[0] + 1e9
            ents.append((w[0], te, w[8] if w[8] is not None else 9, w[5], w[1], w[7]))
        ents.sort()
        st = [x[0] for x in ents]
        txt = sorted(x[0] for x in e["ltx"])
        fr = []
        for d in e["drop"]:
            me = [x for x in ents if x[3] == d[4] and x[4] == d[1] and x[0] <= d[0] and abs(x[1] - d[0]) < 1]
            if not me:
                continue
            m = me[-1]
            t0, t1, p = m[0], d[0], m[2]
            j = bisect.bisect_left(st, t1)
            others = [x for x in ents[max(0, j - 400):j] if x is not m and x[1] > t0 and x[0] < t1 and (x[2] < p or (x[2] == p and x[0] < t0))]
            # head time = lifetime minus union of others' overlaps
            iv = sorted((max(t0, x[0]), min(t1, x[1])) for x in others)
            cov = 0.0
            cs = ce = None
            for a, b in iv:
                if cs is None or a > ce:
                    if cs is not None:
                        cov += ce - cs
                    cs, ce = a, b
                else:
                    ce = max(ce, b)
            if cs is not None:
                cov += ce - cs
            head = (t1 - t0) - cov
            k0, k1 = bisect.bisect_left(txt, t0), bisect.bisect_left(txt, t1)
            ins = [t0] + txt[k0:k1] + [t1]
            gap = max(b - a for a, b in zip(ins, ins[1:]))
            pend345 = [x for x in ents[max(0, j - 400):j] if x is not m and x[0] < t1 < x[1] and 3 <= x[2] <= 5]
            fr.append((head / (t1 - t0), k1 - k0, gap, m[5] in ("auto_pos", "user_wx"), len(pend345) == 0))
        allfr += fr
        if fr:
            print(f"{n}: drops {len(fr)} head-time share p50 {100*median(x[0] for x in fr):.0f} %, "
                  f"drops with >=30 % head time {sum(1 for x in fr if x[0] >= 0.3)}, "
                  f"no other prio3-5 pending at drop {sum(1 for x in fr if x[4])}, "
                  f"TX during lifetime p50 {median(x[1] for x in fr):.0f}, longest TX-free gap p50 {median(x[2] for x in fr):.0f} s")
    print(f"all: {len(allfr)} head-share p50 {100*median(x[0] for x in allfr):.0f} %, >=30 %: {sum(1 for x in allfr if x[0] >= 0.3)}, "
          f">=50 %: {sum(1 for x in allfr if x[0] >= 0.5)}; own only p50 {100*median(x[0] for x in allfr if x[3]):.0f} %")


if __name__ == "__main__":
    main()
