#!/usr/bin/env python3
"""M5 verify (2): SNR/RSSI at neighbour loggers for frames whose last hop is OE3XIA-12.

usage: python3 -I v2_snr.py <v1.json> <oe3xia.log> <out.json> <name=logfile> [name=logfile ...]
Last hop = last callsign of the source path (relays append themselves); cross-checked
against the LH byte (msg_last_hw = 0x80 | BOARD_HARDWARE of the last sender).
Inside = receiver timestamp inside a silence; outside = within both log spans, not inside.
"""
import json
import math
import re
import sys
from bisect import bisect_right
from collections import Counter
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")
FR = re.compile(r"\[LOG\] \d{3} (\S) x([0-9A-F]{8}) H\w\w S\d T\d M\w\w (\S+?)>.* LH:([0-9A-F]{2}) RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w)")
ME = "OE3XIA-12"


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def span(path: str) -> tuple[float, float]:
    first = last = None
    with open(path, errors="replace") as f:
        for line in f:
            m = TS.match(line)
            if m:
                t = ts(m.group(1))
                first = t if first is None else first
                last = t
    return first, last


def q(v: list[float], p: float) -> float:
    s = sorted(v)
    k = (len(s) - 1) * p
    lo = math.floor(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def mwu(x: list[float], y: list[float]) -> float:
    n1, n2 = len(x), len(y)
    if not n1 or not n2:
        return float("nan")
    allv = sorted([(v, 0) for v in x] + [(v, 1) for v in y])
    ranks = [0.0] * len(allv)
    i = 0
    tie = 0.0
    while i < len(allv):
        j = i
        while j + 1 < len(allv) and allv[j + 1][0] == allv[i][0]:
            j += 1
        r = (i + j) / 2 + 1
        for k in range(i, j + 1):
            ranks[k] = r
        t = j - i + 1
        tie += t ** 3 - t
        i = j + 1
    r1 = sum(r for r, (v, g) in zip(ranks, allv) if g == 0)
    u = r1 - n1 * (n1 + 1) / 2
    n = n1 + n2
    sd = math.sqrt(n1 * n2 / 12 * ((n + 1) - tie / (n * (n - 1))))
    if sd == 0:
        return 1.0
    z = (u - n1 * n2 / 2) / sd
    return math.erfc(abs(z) / math.sqrt(2))


def main() -> None:
    v1 = json.load(open(sys.argv[1]))
    sil = [(d["a"], d["b"]) for d in v1["silences"]]
    starts = [a for a, _ in sil]
    xa, xb = span(sys.argv[2])
    out = {}
    for arg in sys.argv[4:]:
        name, path = arg.split("=", 1)
        ra, rb = span(path)
        lo, hi = max(xa, ra), min(xb, rb)
        res = {"in": {"snr": [], "rssi": []}, "out": {"snr": [], "rssi": []}}
        lh_last = Counter()
        lh_orig_only = Counter()
        n_contains_not_last = 0
        with open(path, errors="replace") as f:
            for line in f:
                if "[LOG]" not in line or "RSSI:" not in line:
                    continue
                m = FR.search(line)
                tm = TS.match(line)
                if not m or not tm:
                    continue
                calls = m.group(3).split(",")
                if calls[-1] != ME:
                    if ME in calls:
                        n_contains_not_last += 1
                    continue
                lh_last[m.group(4)] += 1
                if len(calls) == 1:
                    lh_orig_only[m.group(4)] += 1
                t = ts(tm.group(1))
                if not (lo <= t <= hi):
                    continue
                i = bisect_right(starts, t) - 1
                key = "in" if i >= 0 and sil[i][0] < t < sil[i][1] else "out"
                res[key]["snr"].append(int(m.group(6)))
                res[key]["rssi"].append(int(m.group(5)))
        r = {"overlap": [datetime.fromtimestamp(lo).isoformat(), datetime.fromtimestamp(hi).isoformat()],
             "lh_when_last_is_oe3xia": dict(lh_last), "lh_when_oe3xia_is_originator": dict(lh_orig_only),
             "frames_with_oe3xia_not_last": n_contains_not_last}
        for k in ("in", "out"):
            for met in ("snr", "rssi"):
                v = res[k][met]
                r[f"{k}_{met}"] = {"n": len(v)} if not v else {
                    "n": len(v), "median": q(v, .5), "q1": q(v, .25), "q3": q(v, .75),
                    "mean": round(sum(v) / len(v), 2)}
        r["p_snr"] = mwu(res["in"]["snr"], res["out"]["snr"])
        r["p_rssi"] = mwu(res["in"]["rssi"], res["out"]["rssi"])
        out[name] = r
        print(name, "in", r["in_snr"], "out", r["out_snr"], "p_snr=%.3f" % r["p_snr"])
        print("   rssi in", r["in_rssi"], "out", r["out_rssi"], "p_rssi=%.3f" % r["p_rssi"])
        print("   LH(last=XIA)", dict(lh_last.most_common(4)), "XIA in path not last:", n_contains_not_last)
    json.dump(out, open(sys.argv[3], "w"), indent=1)


if __name__ == "__main__":
    main()
