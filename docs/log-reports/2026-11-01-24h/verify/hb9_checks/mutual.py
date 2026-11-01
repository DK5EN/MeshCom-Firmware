#!/usr/bin/env python3
"""Claim 3: mutual audibility. Share of node A's TX-LoRa frames that node B decodes (MH-LoRa, same msg_id,
last hop = A) within W seconds (logger clocks, both ~1 s ahead of node NTP, agree within 0.2 s).

Usage: python3 -I mutual.py <norm-dir>
"""
import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

L = re.compile(r"^(\S+ \S+)\s+\S+ (MH-LoRa|TX-LoRa)\s+\d+ . x([0-9A-F]{8}) H\d\d \S+ \S+ \S+ ([^>]+)>")


def load(p: Path) -> tuple[list[tuple[float, str]], dict[str, list[tuple[float, str]]]]:
    tx: list[tuple[float, str]] = []
    rx: dict[str, list[tuple[float, str]]] = defaultdict(list)
    with p.open(errors="replace") as fh:
        for line in fh:
            m = L.match(line)
            if not m:
                continue
            t = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp()
            if m.group(2) == "TX-LoRa":
                tx.append((t, m.group(3)))
            else:
                rx[m.group(3)].append((t, m.group(4).split(",")[-1]))
    return tx, rx


def share(tx: list[tuple[float, str]], rx: dict[str, list[tuple[float, str]]], a_call: str, w: float, t0: float, t1: float) -> tuple[int, int]:
    n = hit = 0
    for t, mid in tx:
        if not (t0 <= t <= t1):
            continue
        n += 1
        if any(lh == a_call and -1.0 <= r - t <= w for r, lh in rx.get(mid, [])):
            hit += 1
    return hit, n


def main() -> None:
    d = Path(sys.argv[1])
    tx4, rx4 = load(d / "hb9vqq4.log")
    tx5, rx5 = load(d / "hb9vqq5.log")
    t0 = max(tx4[0][0], tx5[0][0])
    t1 = min(tx4[-1][0], tx5[-1][0])
    for w in (3.5, 6.0):
        h, n = share(tx4, rx5, "HB9VQQ-4", w, t0, t1)
        print(f"W={w}s: -5 decodes -4 TX {h}/{n} = {100*h/n:.1f} %")
        h, n = share(tx5, rx4, "HB9VQQ-5", w, t0, t1)
        print(f"W={w}s: -4 decodes -5 TX {h}/{n} = {100*h/n:.1f} %")
    print("TX-LoRa lines -4/-5:", len(tx4), len(tx5))


if __name__ == "__main__":
    main()
