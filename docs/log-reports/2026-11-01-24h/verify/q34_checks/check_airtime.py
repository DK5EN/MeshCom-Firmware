#!/usr/bin/env python3
"""Dropped vs radiated airtime per node (SF11 BW250 CR4/6, preamble 8, explicit header, CRC, LDRO off).
Radiated = [LOG] TX lines (frames really put on air). Dropped = len of the RING_WRITE behind each drop.

Usage: python3 -I check_airtime.py <ev.json from parse_ring.py>
"""
from __future__ import annotations

import json
import math
import sys


def toa_ms(pl: int, sf: int = 11, bw: float = 250e3, cr: int = 2, pre: int = 8) -> float:
    ts = (2 ** sf) / bw * 1000
    n = 8 + max(math.ceil((8 * pl - 4 * sf + 28 + 16) / (4 * sf)) * (cr + 4), 0)
    return (pre + 4.25) * ts + n * ts


def main() -> None:
    ev = json.load(open(sys.argv[1]))
    D = T = 0.0
    for n, e in ev.items():
        wl = {}
        for w in e["write"]:
            wl.setdefault((w[5], w[1]), []).append((w[0], w[4]))
        dsum = 0.0
        for d in e["drop"]:
            c = [l for t, l in wl.get((d[4], d[1]), []) if t <= d[0]]
            if c:
                dsum += toa_ms(c[-1])
        tsum = sum(toa_ms(x[7]) for x in e["ltx"])
        span = (max(x[0] for x in e["write"]) - min(x[0] for x in e["write"])) / 3600
        D += dsum
        T += tsum
        print(f"{n}: dropped {dsum/1000:.1f} s, radiated {tsum/1000:.0f} s ({len(e['ltx'])} TX), share {100*dsum/tsum:.2f} %, "
              f"dropped/h {dsum/1000/span:.1f} s, TX duty {100*tsum/1000/(span*3600):.2f} %")
    print(f"all: dropped {D/1000:.0f} s radiated {T/1000:.0f} s share {100*D/T:.2f} %")
    print("toa(12)", round(toa_ms(12)), "toa(32)", round(toa_ms(32)), "toa(107)", round(toa_ms(107)))


if __name__ == "__main__":
    main()
