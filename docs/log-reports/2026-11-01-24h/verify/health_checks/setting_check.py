#!/usr/bin/env python3
"""Setting claims: own HW id (from own-originated TX-LoRa lines), STAT fw= stamp, STAT up= monotonic
(only '[LOG] STAT' lines), W5100S/ETH summary lines present, RESETREAS lines.
Usage: python3 -I setting_check.py <norm-dir>
"""
from __future__ import annotations
import collections, re, sys
from pathlib import Path


def main() -> None:
    norm = Path(sys.argv[1])
    for n in ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj", "db0ais"]:
        call = n.upper() + "-12"
        own_tx = re.compile(r"TX-Lo[Rr]a\s+\d{3} \S x[0-9A-F]{8} H\w+ S\d T\d M\d+ " + re.escape(call) + r"[>,].* HW:(\d+)")
        hw = collections.Counter(); fw = collections.Counter(); ups: list[int] = []; eth = 0; rr = 0
        with open(norm / f"{n}.log", encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                if (m := own_tx.search(ln)):
                    hw[m.group(1)] += 1
                if "[LOG] STAT " in ln:
                    if (m := re.search(r" fw=(\S+) up=(\d+) ", ln)):
                        fw[m.group(1)] += 1; ups.append(int(m.group(2)))
                if ln.startswith("[ETH];link;up") or "  [ETH];link;up" in ln:
                    eth += 1
                if "RESETREAS" in ln:
                    rr += 1
        dec = sum(1 for a, b in zip(ups, ups[1:]) if b < a)
        print(f"{n}: own HW {dict(hw)}, fw {dict(fw)}, STAT n={len(ups)} up {ups[0]}..{ups[-1]} decreases {dec}, ETH summaries {eth}, RESETREAS lines {rr}")


if __name__ == "__main__":
    main()
