#!/usr/bin/env python3
"""Two checks for the OE3XIA silences.
1) CHANNEL_UTIL rx= samples lying fully inside a silence (sample time >= start+11 s and <= end):
   rx>0 there means ch_util_rx_start was set (header seen) with no OnRxDone/OnRxError.
2) Base rate: share of receptions OUTSIDE silences that have a CAD_SCAN in the preceding 3 s,
   vs the share of silence ends that do.
Usage: python3 -I xia_util_and_cad_baserate.py <norm-dir> [node=oe3xia]
"""
from __future__ import annotations
import bisect, datetime as dt, re, sys
from pathlib import Path

PFX = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")


def t(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def main() -> None:
    norm = Path(sys.argv[1]); node = sys.argv[2] if len(sys.argv) > 2 else "oe3xia"
    rx: list[float] = []; cad: list[float] = []; util: list[tuple[float, int]] = []
    with open(norm / f"{node}.log", encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = PFX.match(ln)
            if not m:
                continue
            b = m.group(2)
            if b.startswith("[MC-DBG] ONRXDONE_TIME ms=") or b.startswith("[MC-DBG] RX_ERROR rssi="):
                rx.append(t(m.group(1)))
            elif b.startswith("[MC-DBG] CAD_SCAN"):
                cad.append(t(m.group(1)))
            elif (u := re.match(r"\[MC-DBG\] CHANNEL_UTIL rx=(\d+)ms", b)):
                util.append((t(m.group(1)), int(u.group(1))))
    gaps = [(a, b) for a, b in zip(rx, rx[1:]) if b - a > 300]
    inside = [(ts, v) for ts, v in util if any(a + 11 <= ts <= b for a, b in gaps)]
    nz = [(ts, v) for ts, v in inside if v > 0]
    print(f"{node}: CHANNEL_UTIL samples fully inside silences: {len(inside)}, with rx>0: {len(nz)}, rx sum {sum(v for _, v in nz)} ms")
    for ts, v in nz[:12]:
        print(f"   {dt.datetime.fromtimestamp(ts):%m-%d %H:%M:%S} rx={v}")
    def cad_before(x: float) -> bool:
        i = bisect.bisect_left(cad, x)
        return i > 0 and x - cad[i - 1] <= 3.0
    ends = [b for _, b in gaps]
    gapset = set(ends)
    outside = [x for x in rx if x not in gapset]
    print(f"silence ends with CAD_SCAN <=3 s before: {sum(map(cad_before, ends))}/{len(ends)}")
    print(f"other receptions with CAD_SCAN <=3 s before: {sum(map(cad_before, outside))}/{len(outside)} "
          f"= {100*sum(map(cad_before, outside))/len(outside):.1f}%")


if __name__ == "__main__":
    main()
