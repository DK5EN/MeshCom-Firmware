"""Frequency error of decoded frames per last hop (RXP line preceding MH-LoRa).

usage: python3 -I ferr_by_lasthop.py <norm_log> <out_json>
"""
from __future__ import annotations

import json
import re
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import pct  # noqa: E402
from common import iter_lines  # noqa: E402

RXP = re.compile(r"Received packet: RSSI:\s*(-?\d+) dBm / SNR:\s*(-?\d+) dB / Frequency error:\s*(-?[\d.]+)")
MH = re.compile(r"MH-LoRa\s+(\d{3}) (\S) x([0-9A-F]{8}) H(\d\d) S(\d) T(\d) M(\d\d) (.*?)>")


def main(path: str, out: str) -> None:
    last = None
    by = defaultdict(list)
    for n, t, b in iter_lines(path):
        m = RXP.search(b)
        if m:
            last = (t, float(m.group(1)), float(m.group(2)), float(m.group(3)))
            continue
        m = MH.search(b)
        if m and last and t - last[0] < 1.0:
            toks = m.group(8).split(",")
            by[toks[-1]].append(last[3])
            last = None
    res = {}
    for c, v in sorted(by.items(), key=lambda kv_: -len(kv_[1])):
        if len(v) >= 40:
            res[c] = {"n": len(v), "ferr_p5": pct(v, 5), "ferr_p50": pct(v, 50), "ferr_p95": pct(v, 95),
                      "share_gt3000_pct": round(100.0 * sum(1 for x in v if abs(x) > 3000) / len(v), 1)}
    json.dump(res, open(out, "w"), indent=1)
    for c, v in res.items():
        print(c, v)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
