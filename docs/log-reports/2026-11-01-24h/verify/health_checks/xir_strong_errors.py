#!/usr/bin/env python3
"""OE3XIR strong RX_ERROR (> -60 dBm) check.
- counts strong RX_ERROR per node (all six logs; DB0AIS has no wall clock, counts only),
- per logger hour for OE3XIR,
- time since the last own OnTXDone (own-TX echo test) for strong vs weak errors,
- time to the nearest TX start (RADIO_TX line) of each other OE3 logger (nearby-node test).
Usage: python3 -I xir_strong_errors.py <norm-dir>
"""
from __future__ import annotations
import bisect, collections, datetime as dt, re, sys
from pathlib import Path

PFX = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
ERR = re.compile(r"\[MC-DBG\] RX_ERROR rssi=(-?\d+) snr=(-?\d+)")


def t(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def scan(p: Path) -> tuple[list[tuple[float, int, int]], list[float], list[float]]:
    errs: list[tuple[float, int, int]] = []; txd: list[float] = []; txs: list[float] = []
    with open(p, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = PFX.match(ln)
            body = m.group(2) if m else ln.strip()
            ts = t(m.group(1)) if m else 0.0
            if (e := ERR.match(body)):
                errs.append((ts, int(e.group(1)), int(e.group(2))))
            elif body.startswith("OnTXDone"):
                txd.append(ts)
            elif body.startswith("[MC-DBG] RADIO_TX"):
                txs.append(ts)
    return errs, txd, txs


def q(xs: list[float], p: float) -> float:
    xs = sorted(xs); return xs[min(len(xs) - 1, int(p * len(xs)))] if xs else float("nan")


def main() -> None:
    norm = Path(sys.argv[1])
    data = {n: scan(norm / f"{n}.log") for n in ["oe3xir", "oe3xhl", "oe3xia", "oe3xoc", "oe3xwj", "db0ais"]}
    for n, (e, _, _) in data.items():
        print(f"{n}: RX_ERROR {len(e)}, > -60 dBm {sum(1 for x in e if x[1] > -60)}, > -85 dBm {sum(1 for x in e if x[1] > -85)}")
    e, txd, _ = data["oe3xir"]
    strong = [x for x in e if x[1] > -60]
    hours = collections.Counter(dt.datetime.fromtimestamp(x[0]).strftime("%H") for x in strong)
    print("OE3XIR strong per logger hour:", dict(sorted(hours.items())))
    print("OE3XIR strong SNR range:", min(x[2] for x in strong), "..", max(x[2] for x in strong),
          "RSSI values:", collections.Counter(x[1] for x in strong).most_common(6))
    def since_tx(ts: float) -> float:
        i = bisect.bisect_left(txd, ts); return ts - txd[i - 1] if i else 1e9
    ds = [since_tx(x[0]) for x in strong]; dw = [since_tx(x[0]) for x in e if x[1] <= -60]
    print(f"time since own OnTXDone: strong median {q(ds,.5):.1f}s, share <2 s {100*sum(d<2 for d in ds)/len(ds):.1f}%; "
          f"weak median {q(dw,.5):.1f}s, share <2 s {100*sum(d<2 for d in dw)/len(dw):.1f}%")
    for o in ["oe3xhl", "oe3xia", "oe3xoc", "oe3xwj"]:
        txs = data[o][2]
        def near(ts: float) -> float:
            i = bisect.bisect_left(txs, ts)
            c = [abs(ts - txs[j]) for j in (i - 1, i) if 0 <= j < len(txs)]
            return min(c) if c else 1e9
        dn = [near(x[0]) for x in strong]; dwn = [near(x[0]) for x in e if x[1] <= -60 and x[0] < strong[-1][0] + 60]
        print(f"  nearest {o} TX start: strong share within 3 s {100*sum(d<3 for d in dn)/len(dn):.1f}% vs weak {100*sum(d<3 for d in dwn)/len(dwn):.1f}%")


if __name__ == "__main__":
    main()
