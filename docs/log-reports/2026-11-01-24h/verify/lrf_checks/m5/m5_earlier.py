#!/usr/bin/env python3
"""M5 (e): receive-silence proxy for captures that predate the ONRXDONE_TIME / RX_ERROR / CAD markers.

The old OE3XIA capture (logs/berg-node, 2026-09-01 22:09 .. 09-02 06:17) has no ONRXDONE_TIME,
RX_ERROR, CAD_SCAN or CHANNEL_UTIL lines, only the '[LOG] <len> <type> x<id> ...' reception line
(and 'TX-LoRa' / 'MH-LoRa' mirror lines, which are NOT counted). A silence proxy is therefore a gap
of more than 300 s between two consecutive '[LOG] \\d{3} ' reception lines of the subject log, listed
with the number of such reception lines the peer logs recorded inside it. The same proxy is also run on the
new capture (oe3xia.log) to show how it compares to the ONRXDONE_TIME+RX_ERROR definition.
Usage: python3 -I m5_earlier.py <out.json> <name> <subject.log> <peer.log> ... [-- <name> <subject.log> <peer.log> ...]
"""
from __future__ import annotations
import bisect, datetime as dt, json, re, sys
from pathlib import Path

PFX = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
RXL = re.compile(r"^\d\d:\d\d:\d\d \[LOG\] \d{3} ")
THR = 300.0


def t(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=dt.timezone.utc).timestamp()


def fmt(x: float) -> str:
    return dt.datetime.fromtimestamp(x, dt.timezone.utc).strftime("%m-%d %H:%M:%S")


def rxlines(p: Path) -> tuple[list[float], float, float]:
    xs: list[float] = []; first = last = None
    with open(p, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = PFX.match(ln)
            if not m:
                continue
            ts = t(m.group(1))
            first = ts if first is None else first; last = ts
            if RXL.match(m.group(2)):
                xs.append(ts)
    xs.sort()
    return xs, first, last


def one(name: str, subj: str, peers: list[str]) -> dict:
    xs, f, l = rxlines(Path(subj))
    px = {Path(p).name: rxlines(Path(p))[0] for p in peers}
    gaps = [(a, b) for a, b in zip(xs, xs[1:]) if b - a > THR]
    allg = sorted((b - a for a, b in zip(xs, xs[1:])), reverse=True)
    lst = []
    for a, b in gaps:
        lst.append({"start": fmt(a), "end": fmt(b), "dur_s": round(b - a, 1),
                    "peer_rx_lines_inside": {k: bisect.bisect_left(v, b) - bisect.bisect_right(v, a) for k, v in px.items()}})
    return {"name": name, "subject": Path(subj).name, "span": [fmt(f), fmt(l)], "span_h": round((l - f) / 3600, 2), "rx_lines": len(xs),
            "gaps_gt_300s": len(gaps), "total_s": round(sum(b - a for a, b in gaps)), "longest_gaps_s": [round(g, 1) for g in allg[:5]],
            "gaps_gt_120s": sum(1 for g in allg if g > 120), "list": lst}


def main() -> None:
    out = Path(sys.argv[1]); args = sys.argv[2:]
    groups: list[list[str]] = [[]]
    for a in args:
        if a == "--":
            groups.append([])
        else:
            groups[-1].append(a)
    res = [one(g[0], g[1], g[2:]) for g in groups if len(g) >= 2]
    out.write_text(json.dumps(res, indent=1))
    for r in res:
        print(f"{r['name']}: {r['subject']} span {r['span'][0]} .. {r['span'][1]} ({r['span_h']} h), rx lines {r['rx_lines']}, "
              f"gaps>300s {r['gaps_gt_300s']} total {r['total_s']} s, >120s {r['gaps_gt_120s']}, longest {r['longest_gaps_s']}")
        for g in r["list"][:12]:
            print("   ", g)


if __name__ == "__main__":
    main()
