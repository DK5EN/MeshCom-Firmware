#!/usr/bin/env python3
"""Noise-floor estimators per node.
E1 = p5 / median of RX_ERROR rssi (OnRxError, RadioPktStatus - the source comment at
     src/lora_functions.cpp OnRxError says the values may be stale for header errors).
E2 = median of (RSSI - SNR) over decoded [LOG] receptions with SNR >= 0 (FCS:0000 excluded).
E3 = median RSSI of decoded receptions with SNR <= -10 (signal below noise -> RSSI ~ noise power).
Also: share of RX_ERROR whose rssi/snr pair equals the previous decoded frame's pair (staleness test).
Usage: python3 -I noise_floor.py <norm-dir>
"""
from __future__ import annotations
import re, sys
from pathlib import Path

ERR = re.compile(r"\[MC-DBG\] RX_ERROR rssi=(-?\d+) snr=(-?\d+)")
DEC = re.compile(r"\[MC-TEST\] RX_FRAME len=\d+ rssi=(-?\d+) snr=(-?\d+)")
LOGR = re.compile(r"\[LOG\] .*RSSI:(-?\d+) SNR:(-?\d+) ")


def q(xs: list[int], p: float) -> int:
    xs = sorted(xs); return xs[min(len(xs) - 1, int(p * len(xs)))]


def main() -> None:
    norm = Path(sys.argv[1])
    for n in ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj", "db0ais"]:
        er: list[int] = []; e2: list[int] = []; e3: list[int] = []; stale = 0; last: tuple[int, int] | None = None
        with open(norm / f"{n}.log", encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                if (m := ERR.search(ln)):
                    pr = (int(m.group(1)), int(m.group(2))); er.append(pr[0])
                    if pr == last:
                        stale += 1
                elif (m := DEC.search(ln)):
                    last = (int(m.group(1)), int(m.group(2)))
                elif "[LOG]" in ln and "FCS:0000" not in ln and (m := LOGR.search(ln)):
                    r, s = int(m.group(1)), int(m.group(2))
                    if s >= 0:
                        e2.append(r - s)
                    if s <= -10:
                        e3.append(r)
        print(f"{n}: E1 RX_ERROR p5 {q(er,.05)} median {q(er,.5)} (n={len(er)}); E2 RSSI-SNR(SNR>=0) median {q(e2,.5)} (n={len(e2)}); "
              f"E3 RSSI(SNR<=-10) median {q(e3,.5)} p5 {q(e3,.05)} (n={len(e3)}); RX_ERROR equal to previous decoded rssi/snr: {stale} ({100*stale/len(er):.1f}%)")


if __name__ == "__main__":
    main()
