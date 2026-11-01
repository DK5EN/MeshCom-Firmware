#!/usr/bin/env python3
"""Receive-silence check per OE3 node with three reception markers.

A = 'OnRxDone' line (printed only on the non-ACK path, src/lora_functions.cpp:2744; the ACK path
    returns early at :1018 without it), B = 'ONRXDONE_TIME ms=' (every decoded frame incl. ACK),
C = 'RX_ERROR rssi=' (every OnRxError). Gaps > 300 s are listed for A+C (the analysed metric) and
B+C (all receptions). Also reports, per A+C gap, how many ACK receptions fell inside.
Usage: python3 -I rx_silence.py <norm-dir> [node ...]
"""
from __future__ import annotations
import bisect, datetime as dt, re, sys
from pathlib import Path

RE = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")


def t(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def gaps(xs: list[float], thr: float) -> list[tuple[float, float]]:
    return [(a, b) for a, b in zip(xs, xs[1:]) if b - a > thr]


def main() -> None:
    norm = Path(sys.argv[1])
    nodes = sys.argv[2:] or ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
    for n in nodes:
        A: list[float] = []; B: list[float] = []; C: list[float] = []; ack: list[float] = []
        with open(norm / f"{n}.log", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = RE.match(line)
                if not m:
                    continue
                body = m.group(2)
                if body.startswith("OnRxDone"):
                    A.append(t(m.group(1)))
                elif body.startswith("[MC-DBG] ONRXDONE_TIME ms="):
                    B.append(t(m.group(1)))
                elif body.startswith("[MC-DBG] RX_ERROR rssi="):
                    C.append(t(m.group(1)))
                elif "[LOG] 012 A x" in body:
                    ack.append(t(m.group(1)))
        ac = sorted(A + C); bc = sorted(B + C)
        g1 = gaps(ac, 300); g2 = gaps(bc, 300)
        print(f"{n}: OnRxDone={len(A)} ONRXDONE_TIME={len(B)} RX_ERROR={len(C)} ACK[LOG]={len(ack)}")
        print(f"  gaps>300s  OnRxDone+RX_ERROR: {len(g1)} total {sum(b-a for a,b in g1):.0f}s"
              f" max {max([b-a for a,b in g1], default=0):.0f}s")
        print(f"  gaps>300s  ONRXDONE_TIME+RX_ERROR (all receptions): {len(g2)} total {sum(b-a for a,b in g2):.0f}s"
              f" max {max([b-a for a,b in g2], default=0):.0f}s; longest any gap {max(b-a for a,b in zip(bc,bc[1:])):.0f}s")
        if g1:
            inside = [bisect.bisect_left(ack, b) - bisect.bisect_right(ack, a) for a, b in g1]
            print(f"  ACK receptions inside the A+C gaps: min {min(inside)} median {sorted(inside)[len(inside)//2]} max {max(inside)}")
            for (a, b), k in list(zip(g1, inside))[:5]:
                print(f"    {dt.datetime.fromtimestamp(a):%m-%d %H:%M:%S} {b-a:.0f}s ack_rx_inside={k}")


if __name__ == "__main__":
    main()
