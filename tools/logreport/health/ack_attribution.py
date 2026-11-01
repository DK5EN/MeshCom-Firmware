#!/usr/bin/env python3
"""ACK attribution: which acked msg_id prefixes (5 hex chars) dominate 012-byte ACK receptions.

Usage (via run.py): ack_attribution.py; writes <out>/ack_attribution.json
The acked msg_id is the 2nd x<id> field of `[LOG] 012 A x<ackid> H<b5> x<acked_id> ..`.
"""
from __future__ import annotations
import collections
import json
import re

RE = re.compile(r"\[LOG\] 012 A x([0-9A-F]{8}) H([0-9A-F]{2}) x([0-9A-F]{8}) (\w\w) (\w\w) RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w)")
RE_ID = re.compile(r"(?:NEW-PING|NEW-MSG|TX-LoRa)\s+\d+ : x([0-9A-F]{8}) .*? (\S+?)>(\S+?):\{ping\}")
from nodeset import ALL as NODES, NORM, OUT


def main() -> None:
    norm, out = NORM, OUT / "ack_attribution.json"
    res = {}
    for n in NODES:
        pre: collections.Counter[str] = collections.Counter()
        uniq_ack: dict[str, set[str]] = collections.defaultdict(set)
        total = 0
        pings: dict[str, str] = {}
        bytes45 = collections.Counter()
        with open(norm / f"{n}.log", "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if "[LOG] 012 A x" in line:
                    m = RE.search(line)
                    if m:
                        total += 1
                        pre[m.group(3)[:5]] += 1
                        uniq_ack[m.group(3)[:5]].add(m.group(1))
                        bytes45[(m.group(4), m.group(5))] += 1
                elif "{ping}" in line and ("NEW-PING" in line or "TX-LoRa" in line):
                    m = RE_ID.search(line)
                    if m:
                        pings[m.group(1)[:5]] = f"{m.group(2)}>{m.group(3)}"
        top = []
        for p, c in pre.most_common(8):
            top.append({"acked_prefix": p, "events": c, "share_pct": round(100.0 * c / total, 1) if total else 0,
                        "distinct_ack_ids": len(uniq_ack[p]), "ping_origin": pings.get(p)})
        res[n] = {"ack012_events": total, "top_acked_prefix": top, "ping_origins_seen_in_log": pings,
                  "b10_b11_top": {f"{a}/{b}": c for (a, b), c in bytes45.most_common(5)}}
    out.write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
