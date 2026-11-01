#!/usr/bin/env python3
"""M5 verify (3): share of OE3XIA-12 TX heard by neighbour loggers, inside vs outside silences.

usage: python3 -I v3_heard.py <v1.json> <oe3xia.log> <out.json> <win_s> <name=logfile> [...]
Heard (non-ACK) = same msg_id at a neighbour with last hop OE3XIA-12, rx time in [tx-5, tx+win].
ACK sensitivity: an ACK TX counts as 'heard' if a neighbour logs an ACK line with the same id
within +-3 s of the TX (ambiguous: other relays send the same ACK id; reported separately).
"""
import json
import math
import re
import sys
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")
TX = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) (\S) H\w\w prio=\d+ src=(\w+) wait=(\d+)")
FR = re.compile(r"\[LOG\] \d{3} (\S) x([0-9A-F]{8}) H\w\w S\d T\d M\w\w (\S+?)>")
ACK = re.compile(r"\[LOG\] 0(?:07|12) A x([0-9A-F]{8}) ")
ME = "OE3XIA-12"


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def wilson(k: int, n: int) -> tuple[float, float]:
    if n == 0:
        return (float("nan"), float("nan"))
    z = 1.959964
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (round(100 * (c - h), 1), round(100 * (c + h), 1))


def fisher(a: int, b: int, c: int, d: int) -> float:
    """two-sided Fisher exact for [[a,b],[c,d]] via log-hypergeometric."""
    r1, r2, c1 = a + b, c + d, a + c
    n = r1 + r2
    lf = [0.0] * (n + 1)
    for i in range(1, n + 1):
        lf[i] = lf[i - 1] + math.log(i)

    def lp(x: int) -> float:
        return lf[r1] - lf[x] - lf[r1 - x] + lf[r2] - lf[c1 - x] - lf[r2 - c1 + x] - (lf[n] - lf[c1] - lf[n - c1])
    p0 = lp(a)
    tot = 0.0
    for x in range(max(0, c1 - r2), min(r1, c1) + 1):
        v = lp(x)
        if v <= p0 + 1e-9:
            tot += math.exp(v)
    return min(1.0, tot)


def main() -> None:
    v1 = json.load(open(sys.argv[1]))
    sil = [(d["a"], d["b"]) for d in v1["silences"]]
    starts = [a for a, _ in sil]
    win = float(sys.argv[4])

    def inside(t: float) -> int:
        i = bisect_right(starts, t) - 1
        return i if i >= 0 and sil[i][0] < t < sil[i][1] else -1

    txs = []
    with open(sys.argv[2], errors="replace") as f:
        for line in f:
            if "[LOG] TX x" not in line:
                continue
            m, tm = TX.search(line), TS.match(line)
            if m and tm:
                txs.append((ts(tm.group(1)), m.group(1), m.group(2), m.group(3), int(m.group(4))))
    rx_last = defaultdict(list)   # id -> [(t, name)] last hop XIA
    rx_ack = defaultdict(list)
    spans = {}
    for arg in sys.argv[5:]:
        name, path = arg.split("=", 1)
        first = last = None
        with open(path, errors="replace") as f:
            for line in f:
                tm = TS.match(line)
                if not tm:
                    continue
                last = ts(tm.group(1))
                if first is None:
                    first = last
                if "[LOG]" not in line:
                    continue
                m = FR.search(line)
                if m and m.group(3).split(",")[-1] == ME:
                    rx_last[m.group(2)].append((ts(tm.group(1)), name))
                    continue
                m = ACK.search(line)
                if m:
                    rx_ack[m.group(1)].append((ts(tm.group(1)), name))
        spans[name] = (first, last)
    # clock skew: rx - tx for unique-id non-ACK matches
    skew = []
    res = {"in": Counter(), "out": Counter(), "in4": Counter(), "out4": Counter()}
    all4 = (max(s[0] for s in spans.values()), min(s[1] for s in spans.values()))
    in_src = Counter()
    in_relay_queued_before = 0
    in_relay = 0
    for t, mid, typ, src, wait in txs:
        k = "in" if inside(t) >= 0 else "out"
        if k == "in":
            in_src[(typ, src)] += 1
            if src == "r":
                in_relay += 1
                if t - wait / 1000 < sil[inside(t)][0]:
                    in_relay_queued_before += 1
        if typ == "A":
            hit = any(abs(rt - t) <= 3 for rt, _ in rx_ack.get(mid, []))
            res[k]["ack"] += 1
            res[k]["ack_heard"] += hit
            if all4[0] <= t <= all4[1]:
                res[k + "4"]["ack"] += 1
                res[k + "4"]["ack_heard"] += hit
            continue
        hits = [rt - t for rt, _ in rx_last.get(mid, []) if -5 <= rt - t <= win]
        skew += hits[:1]
        res[k]["nonack"] += 1
        res[k]["heard"] += bool(hits)
        res[k]["type_" + typ] += 1
        res[k]["heard_" + typ] += bool(hits)
        if all4[0] <= t <= all4[1]:
            res[k + "4"]["nonack"] += 1
            res[k + "4"]["heard"] += bool(hits)
    skew.sort()
    out = {"win_s": win, "n_tx": len(txs), "skew_median_s": skew[len(skew) // 2] if skew else None,
           "skew_p95_s": skew[int(.95 * len(skew))] if skew else None,
           "all4_window": [datetime.fromtimestamp(x).isoformat() for x in all4],
           "inside_tx_by_type_src": {f"{a}/{b}": c for (a, b), c in in_src.items()},
           "inside_relay_tx": in_relay, "inside_relay_queued_before_silence": in_relay_queued_before}
    for k in ("in", "out", "in4", "out4"):
        c = res[k]
        out[k] = dict(c)
        out[k]["share_nonack"] = round(100 * c["heard"] / c["nonack"], 1) if c["nonack"] else None
        out[k]["wilson"] = wilson(c["heard"], c["nonack"])
    out["fisher_nonack"] = fisher(res["in"]["heard"], res["in"]["nonack"] - res["in"]["heard"],
                                  res["out"]["heard"], res["out"]["nonack"] - res["out"]["heard"])
    out["fisher_nonack_all4"] = fisher(res["in4"]["heard"], res["in4"]["nonack"] - res["in4"]["heard"],
                                       res["out4"]["heard"], res["out4"]["nonack"] - res["out4"]["heard"])
    out["fisher_ack_sens"] = fisher(res["in"]["ack_heard"], res["in"]["ack"] - res["in"]["ack_heard"],
                                    res["out"]["ack_heard"], res["out"]["ack"] - res["out"]["ack_heard"])
    print(json.dumps(out, indent=1))
    json.dump(out, open(sys.argv[3], "w"), indent=1)


if __name__ == "__main__":
    main()
