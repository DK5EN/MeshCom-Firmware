#!/usr/bin/env python3
"""Claim 7: prio-5 wait (RING_TX_READ lat=) own HEY (src auto_pos) vs relayed HEY (rx_relay); max vs 180 s TTL.

Usage: python3 -I hey_wait.py <norm-dir>
"""
import re
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

W = re.compile(r"RING_WRITE slot=(\d+) type=(\w+) .*msg_id=([0-9A-F]{8}) .*src=(\S+)")
R = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) .*msg_id=([0-9A-F]{8}) .*lat=(\d+)ms")


def q(v: list[float], p: float) -> float:
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))]


def run(p: Path) -> None:
    occ: dict[str, tuple[str, str]] = {}
    lat: dict[str, list[float]] = defaultdict(list)
    with p.open(errors="replace") as fh:
        for line in fh:
            if "RING_" not in line:
                continue
            m = W.search(line)
            if m:
                occ[m.group(1)] = (m.group(3), m.group(4))
                continue
            m = R.search(line)
            if m and m.group(2) == "5":
                o = occ.get(m.group(1))
                src = o[1] if o and o[0] == m.group(3) else "unjoined"
                lat[src].append(int(m.group(4)) / 1000)
    print(f"== {p.name}")
    for k, v in sorted(lat.items()):
        print(f"   prio5 {k}: n={len(v)} p50 {st.median(v):.1f} p90 {q(v,0.9):.1f} max {max(v):.1f} s; >=144 s (80 % TTL) {sum(x>=144 for x in v)}")


def main() -> None:
    d = Path(sys.argv[1])
    run(d / "hb9vqq4.log")
    run(d / "hb9vqq5.log")


if __name__ == "__main__":
    main()
