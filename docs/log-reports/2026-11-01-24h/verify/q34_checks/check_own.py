#!/usr/bin/env python3
"""Own-beacon fate per node (auto_pos HEY, user_wx WX): written/sent/dropped/superseded, longest gap
between consecutive radiations, TX-UDP uplink of dropped WX.

Usage: python3 -I check_own.py <ev.json from parse_ring.py>
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict


def hhmm(t: float) -> str:
    d, r = divmod(t, 86400)
    return f"{'31.10' if d == 0 else '01.11'} {int(r // 3600):02d}:{int(r % 3600 // 60):02d}:{r % 60:05.2f}"


def main() -> None:
    ev = json.load(open(sys.argv[1]))
    for n, e in ev.items():
        reads = defaultdict(list)
        for r in e["read"]:
            reads[r[6]].append(r[0])
        drops = {(d[4], d[1]): d[0] for d in e["drop"]}
        sup_old = {s[3] for s in e["sup"]}
        udp = defaultdict(list)
        for t, mid in e["txudp"]:
            udp[mid].append(t)
        tstart = min(x[0] for x in e["write"])
        tend = max(x[0] for x in e["write"])
        for src in ("auto_pos", "user_wx"):
            ws = [w for w in e["write"] if w[7] == src]
            if not ws:
                continue
            sent = [w for w in ws if any(w[0] <= t <= w[0] + 14400 for t in reads.get(w[5], []))]
            dr = [w for w in ws if (w[5], w[1]) in drops and 0 <= drops[(w[5], w[1])] - w[0] < 400]
            su = [w for w in ws if w[5] in sup_old and w not in sent]
            other = len(ws) - len(sent) - len(dr) - len(su)
            txt = sorted(min(t for t in reads[w[5]] if w[0] <= t <= w[0] + 14400) for w in sent)
            gaps = [(b - a, a, b) for a, b in zip([tstart] + txt, txt + [tend])]
            g = max(gaps)
            udp_ok = sum(1 for w in dr if any(abs(t - w[0]) < 5 for t in udp.get(w[5], [])))
            print(f"{n} {src}: written {len(ws)} sent {len(sent)} ({100*len(sent)/len(ws):.0f} %) dropped {len(dr)} "
                  f"superseded {len(su)} other {other} | longest gap {g[0]/3600:.2f} h {hhmm(g[1])} -> {hhmm(g[2])} | "
                  f"dropped with TX-UDP +-5s: {udp_ok}/{len(dr)}")


if __name__ == "__main__":
    main()
