#!/usr/bin/env python3
"""M5 verify (inference): does OE3XIA-12 hear the relay echo of its own non-ACK TX?

usage: python3 -I v6_echo.py <v1.json> <oe3xia.log> <out.json>
Echo = OE3XIA [LOG] reception line with the same msg_id 0.2..20 s after the TX line.
Split by TX inside/outside silence; silence-ending TX are those whose echo ends a silence.
"""
import json
import re
import sys
from bisect import bisect_right
from collections import defaultdict
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")
TX = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) (\S) ")
RX = re.compile(r"\[LOG\] \d{3} \S x([0-9A-F]{8}) ")


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def main() -> None:
    v1 = json.load(open(sys.argv[1]))
    sil = [(d["a"], d["b"]) for d in v1["silences"]]
    starts = [a for a, _ in sil]
    txs, rx = [], defaultdict(list)
    with open(sys.argv[2], errors="replace") as f:
        for line in f:
            if "[LOG]" not in line:
                continue
            tm = TS.match(line)
            if not tm:
                continue
            m = TX.search(line)
            if m:
                if m.group(2) != "A":
                    txs.append((ts(tm.group(1)), m.group(1)))
                continue
            m = RX.search(line)
            if m:
                rx[m.group(1)].append(ts(tm.group(1)))
    c = {"in": [0, 0], "out": [0, 0]}
    ends_by_echo = 0
    for t, mid in txs:
        i = bisect_right(starts, t) - 1
        k = "in" if i >= 0 and sil[i][0] < t < sil[i][1] else "out"
        hits = [r for r in rx.get(mid, []) if 0.2 <= r - t <= 20]
        c[k][1] += 1
        c[k][0] += bool(hits)
        if k == "in" and hits and min(hits) >= sil[i][1] - 0.01:
            ends_by_echo += 1
    out = {k: [a, n, round(100 * a / n, 1) if n else None] for k, (a, n) in c.items()}
    out["inside_tx_whose_echo_is_the_silence_end"] = ends_by_echo
    print(json.dumps(out))
    json.dump(out, open(sys.argv[3], "w"), indent=1)


if __name__ == "__main__":
    main()
