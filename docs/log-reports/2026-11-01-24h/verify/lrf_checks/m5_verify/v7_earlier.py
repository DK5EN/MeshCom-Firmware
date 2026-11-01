#!/usr/bin/env python3
"""M5 verify (5): RX-silence proxy on an OE3XIA-12 capture: gaps > 300 s between [LOG] ddd lines.

usage: python3 -I v7_earlier.py <oe3xia log> [<oe3xia log> ...]
Variants: all [LOG] ddd lines; without lines whose path ends in OE3XIA-12 (own TX/originated
frames printed as [LOG] by the old build, which has no RSSI field to tell RX from TX);
additionally without ACK lines (own ACK TX is indistinguishable there).
"""
import re
import sys
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")
L = re.compile(r"\[LOG\] (\d{3}) (\S) x[0-9A-F]{8} (.*)")
PATH = re.compile(r"M\w\w (\S+?)>")


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def gaps(t: list[float]) -> str:
    g = sorted((b - a, a) for a, b in zip(t, t[1:]) if b - a > 300)
    top = sorted((b - a for a, b in zip(t, t[1:])), reverse=True)[:3]
    first = datetime.fromtimestamp(g[-1][1]).strftime("%m-%d %H:%M:%S") if g else "-"
    return (f"n={len(g)} total={sum(x for x, _ in g):.1f}s longest={g[-1][0] if g else 0:.1f}s "
            f"(at {first}) top3={[round(x, 1) for x in top]}")


def main() -> None:
    for path in sys.argv[1:]:
        a, b, c = [], [], []
        span = []
        with open(path, errors="replace") as f:
            for line in f:
                tm = TS.match(line)
                if not tm:
                    continue
                span.append(tm.group(1))
                m = L.search(line)
                if not m:
                    continue
                t = ts(tm.group(1))
                a.append(t)
                pm = PATH.search(m.group(3))
                own = bool(pm) and pm.group(1).split(",")[-1] == "OE3XIA-12"
                if not own:
                    b.append(t)
                    if m.group(2) != "A":
                        c.append(t)
        h = (ts(span[-1]) - ts(span[0])) / 3600
        print(f"== {path}  span {span[0][:19]} .. {span[-1][:19]} ({h:.1f} h)")
        print("  all [LOG] ddd     :", len(a), gaps(a))
        print("  minus own-path    :", len(b), gaps(b))
        print("  minus own + ACK   :", len(c), gaps(c))


if __name__ == "__main__":
    main()
