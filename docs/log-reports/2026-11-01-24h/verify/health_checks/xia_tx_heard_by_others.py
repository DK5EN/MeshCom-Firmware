#!/usr/bin/env python3
"""During OE3XIA receive silences: (a) were OE3XIA's own transmissions heard by the other OE3 loggers
(a [LOG] reception line with the same msg_id whose path's last call is OE3XIA-12, within 30 s), and
(b) how many receptions (ONRXDONE_TIME + RX_ERROR, ACKs included) did each other node log in the window;
also how many frames whose last hop is a usual OE3XIA neighbour were logged by others.
Usage: python3 -I xia_tx_heard_by_others.py <norm-dir>
"""
from __future__ import annotations
import bisect, collections, datetime as dt, re, sys
from pathlib import Path

PFX = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
OTH = ["oe3xhl", "oe3xir", "oe3xoc", "oe3xwj"]
NEIGH = {"OE1BSE-1", "OE3XPA-12", "OE1RIG-4", "OE3ALB-2", "OE3GHB-1", "OE3XHQ-12"}


def t(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def load(p: Path) -> tuple[list[float], list[tuple[float, str, str]], list[tuple[float, str]]]:
    rx: list[float] = []; frames: list[tuple[float, str, str]] = []; tx: list[tuple[float, str]] = []
    with open(p, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = PFX.match(ln)
            if not m:
                continue
            b = m.group(2)
            if b.startswith("[MC-DBG] ONRXDONE_TIME ms=") or b.startswith("[MC-DBG] RX_ERROR rssi="):
                rx.append(t(m.group(1)))
            elif (f := re.search(r"\[LOG\] \d{3} . x([0-9A-F]{8}) H\d\d .*? M\d\d (\S+?)>", b)) and "RSSI:" in b:
                frames.append((t(m.group(1)), f.group(1), f.group(2).split(",")[-1]))
            elif (x := re.search(r"\[LOG\] TX x([0-9A-F]{8}) ", b)):
                tx.append((t(m.group(1)), x.group(1)))
    return rx, frames, tx


def main() -> None:
    norm = Path(sys.argv[1])
    xrx, _, xtx = load(norm / "oe3xia.log")
    gaps = [(a, b) for a, b in zip(xrx, xrx[1:]) if b - a > 300]
    oth = {n: load(norm / f"{n}.log") for n in OTH}
    idx = {n: collections.defaultdict(list) for n in OTH}
    for n in OTH:
        for ts, mid, last in oth[n][1]:
            idx[n][mid].append((ts, last))
    tx_in = [(ts, mid) for ts, mid in xtx if any(a < ts < b for a, b in gaps)]
    heard = 0
    for ts, mid in tx_in:
        if any(abs(ts2 - ts) <= 30 and last == "OE3XIA-12" for n in OTH for ts2, last in idx[n][mid]):
            heard += 1
    print(f"OE3XIA own TX inside silences: {len(tx_in)}; heard by >=1 other logger with last hop OE3XIA-12: {heard}")
    lo = collections.Counter(); hi = collections.Counter(); ngh = []
    for a, b in gaps:
        for n in OTH:
            r = oth[n][0]
            k = bisect.bisect_left(r, b) - bisect.bisect_right(r, a)
            lo[n] = min(lo.get(n, 10**9), k); hi[n] = max(hi[n], k)
        ngh.append(sum(1 for n in OTH for ts, _, last in oth[n][1] if a < ts < b and last in NEIGH))
    print("receptions at other nodes per silence (all markers, incl. ACK) min..max:", {n: f"{lo[n]}..{hi[n]}" for n in OTH})
    print(f"frames from usual OE3XIA neighbours {sorted(NEIGH)} logged by others per silence: min {min(ngh)} median {sorted(ngh)[len(ngh)//2]} max {max(ngh)}")


if __name__ == "__main__":
    main()
