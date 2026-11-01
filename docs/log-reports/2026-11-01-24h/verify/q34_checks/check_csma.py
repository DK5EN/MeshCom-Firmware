#!/usr/bin/env python3
"""CSMA_AGED verification: events, slot episodes, stage-2 events, prio of the slot (via the next
RING_TX_READ of that slot), share of prio<=3 TX waiting >= 20 s, ACK wait p50, RX_ERROR within 3 s
after own TX for aged vs non-aged prio-1 TX.

Usage: python3 -I check_csma.py <ev.json from parse_ring.py>
"""
from __future__ import annotations

import bisect
import json
import sys
from collections import Counter
from statistics import median


def main() -> None:
    ev = json.load(open(sys.argv[1]))
    tot_ev = tot_ep = 0
    for n, e in ev.items():
        aged = e["aged"]
        eps = []
        for a in aged:
            if eps and eps[-1]["slot"] == a[1] and a[2] >= eps[-1]["last_age"] and a[0] - eps[-1]["t_last"] < 120:
                eps[-1]["last_age"] = a[2]
                eps[-1]["t_last"] = a[0]
                eps[-1]["n"] += 1
            else:
                eps.append({"slot": a[1], "t0": a[0], "t_last": a[0], "last_age": a[2], "n": 1})
        st2 = sum(1 for a in aged if a[3] == 2)
        # prio of each episode: first TX_READ of that slot after the episode end
        reads = e["read"]
        rt = [r[0] for r in reads]
        pc = Counter()
        aged_tx = set()
        for ep in eps:
            i = bisect.bisect_left(rt, ep["t_last"])
            while i < len(reads) and reads[i][1] != ep["slot"]:
                i += 1
            if i < len(reads) and reads[i][0] - ep["t_last"] < 120:
                pc[reads[i][2]] += 1
                aged_tx.add(i)
            else:
                pc["?"] += 1
        p13 = [r for r in reads if r[2] <= 3]
        share20 = sum(1 for r in p13 if r[8] >= 20000) / max(1, len(p13))
        acks = [r[8] / 1000 for r in reads if r[2] == 1]
        # RX_ERROR within 3 s after own TX start: aged vs not aged prio 1 TX
        rxe = sorted(e["rxerr"])

        def err_after(t: float) -> bool:
            j = bisect.bisect_left(rxe, t)
            return j < len(rxe) and rxe[j] - t <= 3.0

        a_hit = [err_after(reads[i][0]) for i in aged_tx if reads[i][2] == 1]
        n_hit = [err_after(r[0]) for k, r in enumerate(reads) if r[2] == 1 and k not in aged_tx and r[8] < 20000]
        tot_ev += len(aged)
        tot_ep += len(eps)
        print(f"{n}: events {len(aged)} episodes {len(eps)} (ev/ep {len(aged)/max(1,len(eps)):.1f}) stage2 {st2} "
              f"prio {dict(pc)} | prio<=3 TX wait>=20s {100*share20:.1f} % | ACK wait p50 {median(acks) if acks else 0:.1f}s "
              f"max {max(acks) if acks else 0:.1f}s | RXERR<=3s after TX aged {100*sum(a_hit)/max(1,len(a_hit)):.1f} % (n={len(a_hit)}) "
              f"vs fresh {100*sum(n_hit)/max(1,len(n_hit)):.1f} % (n={len(n_hit)})")
    print("TOTAL events", tot_ev, "episodes", tot_ep)


if __name__ == "__main__":
    main()
