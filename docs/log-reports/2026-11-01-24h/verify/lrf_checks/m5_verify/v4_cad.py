#!/usr/bin/env python3
"""M5 verify (4): CAD busy share (CAD_SCAN result=-702 = RADIOLIB_LORA_DETECTED) inside vs outside
OE3XIA-12 silence windows, at OE3XIA and (same wall-clock windows) at the other loggers.

usage: python3 -I v4_cad.py <v1.json> <out.json> <name=logfile> [...]
Outside = within the OE3XIA silence-definition span, not inside a silence. Also 'near' =
outside but within 30 min of a silence edge (time-of-day matched control).
"""
import json
import re
import sys
from bisect import bisect_right
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")
CAD = re.compile(r"CAD_SCAN result=(-?\d+)")


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def main() -> None:
    v1 = json.load(open(sys.argv[1]))
    sil = [(d["a"], d["b"]) for d in v1["silences"]]
    starts = [a for a, _ in sil]
    span = [datetime.fromisoformat(x).timestamp() for x in v1["span"]]

    def cls(t: float) -> str:
        i = bisect_right(starts, t) - 1
        if i >= 0 and sil[i][0] < t < sil[i][1]:
            return "in"
        prev_end = sil[i][1] if i >= 0 else -1e18
        next_start = sil[i + 1][0] if i + 1 < len(sil) else 1e18
        return "near" if min(t - prev_end, next_start - t) <= 1800 else "far"

    out = {}
    for arg in sys.argv[3:]:
        name, path = arg.split("=", 1)
        c = {k: [0, 0] for k in ("in", "near", "far")}
        codes = {}
        with open(path, errors="replace") as f:
            for line in f:
                if "CAD_SCAN" not in line:
                    continue
                m, tm = CAD.search(line), TS.match(line)
                if not m or not tm:
                    continue
                t = ts(tm.group(1))
                if not (span[0] <= t <= span[1]):
                    continue
                codes[m.group(1)] = codes.get(m.group(1), 0) + 1
                k = cls(t)
                c[k][1] += 1
                c[k][0] += m.group(1) == "-702"
        o = {"codes": codes}
        for k, (b, n) in c.items():
            o[k] = [b, n, round(100 * b / n, 1) if n else None]
        bo, no = c["near"][0] + c["far"][0], c["near"][1] + c["far"][1]
        o["outside"] = [bo, no, round(100 * bo / no, 1) if no else None]
        o["ratio_in_over_out"] = round(o["in"][2] / o["outside"][2], 3) if c["in"][1] and no else None
        out[name] = o
        print(name, "in", o["in"], "outside", o["outside"], "near", o["near"], "ratio", o["ratio_in_over_out"], codes)
    json.dump(out, open(sys.argv[2], "w"), indent=1)


if __name__ == "__main__":
    main()
