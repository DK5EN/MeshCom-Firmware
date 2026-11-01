#!/usr/bin/env python3
"""{CET} time-signal delivery per node.

(a) Internet side: `[CET];gw_rx;src;<server>;...` lines = beacons the node's gateway received from the server.
(b) LoRa side: distinct `{CET}YYYY-MM-DD HH:MM:SS` stamps heard in [LOG] RX lines (FCS != 0000), any path.
Interval = median gap of distinct stamps (derived from data, not assumed).  Missing beacons = gap/median - 1.
The stamp carries its own send time (the text is UTC; see findings), so the schedule is reconstructed from the stamps.
Usage (via run.py): cet_beacons.py; writes <out>/cet_beacons.json
"""
from __future__ import annotations
import collections
import datetime as dt
import json
import re
import statistics

from nodeset import ALL as NODES, CLUSTER, NORM, OUT, STANDALONE, STANDALONE_START
RE_PREF = re.compile(r"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  ")
RE_GW = re.compile(r"\[CET\];gw_rx;src;([\w-]+);age_s;(-?\d+);trusted;(\d);hop;(\d+);ms;(\d+)")
RE_LORA = re.compile(r"\[LOG\] \d{3} : x\w{8} H\w\w S\d T\d M\d+ (\S+?)>.*?\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d).* FCS:(\w{4}) ")


def gaps_report(stamps: list[dt.datetime]) -> dict:
    s = sorted(set(stamps))
    if len(s) < 3:
        return {"n": len(s)}
    g = [(b - a).total_seconds() for a, b in zip(s, s[1:])]
    med = statistics.median(g)
    missing = sum(max(0, round(x / med) - 1) for x in g if x > 1.5 * med)
    big = sorted(((x, a.strftime("%m-%d %H:%M:%S")) for x, a in zip(g, s) if x > 1.5 * med), reverse=True)[:5]
    return {"n_distinct": len(s), "median_gap_s": med, "expected_in_span": round((s[-1] - s[0]).total_seconds() / med) + 1,
            "missing_beacons": missing, "largest_gaps": [{"gap_s": x, "after_stamp": t} for x, t in big]}


def main() -> None:
    norm, out = NORM, OUT / "cet_beacons.json"
    boot0 = dt.datetime.fromisoformat(STANDALONE_START)
    res: dict = {}
    union: dict[str, set[dt.datetime]] = collections.defaultdict(set)
    per_node_lora: dict[str, dict[str, set[dt.datetime]]] = {}
    for n in NODES:
        gw: list[tuple[dt.datetime, str, int]] = []
        lora: dict[str, set[dt.datetime]] = collections.defaultdict(set)
        lora_ev = 0
        with open(norm / f"{n}.log", "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if "[CET];gw_rx" in line:
                    p = RE_PREF.match(line)
                    m = RE_GW.search(line)
                    if m and p:
                        w = dt.datetime(*(int(x) for x in p.groups()[:6]))
                        gw.append((w, m.group(1), int(m.group(2))))
                    elif m:
                        gw.append((boot0 + dt.timedelta(milliseconds=int(m.group(5))), m.group(1), int(m.group(2))))
                elif "{CET}" in line and "[LOG]" in line:
                    m = RE_LORA.search(line)
                    if m and m.group(3) != "0000":
                        lora_ev += 1
                        st = dt.datetime.strptime(m.group(2), "%Y-%m-%d %H:%M:%S")
                        src = m.group(1).split(",")[0]
                        lora[src].add(st)
                        if n not in STANDALONE:
                            union[src].add(st)
        per_node_lora[n] = lora
        by_src = collections.defaultdict(list)
        for w, s, a in gw:
            by_src[s].append(w)
        res[n] = {"gw_rx_by_src": {s: len(v) for s, v in by_src.items()},
                  "gw_rx_gaps": {s: ({**gaps_report(v)} if True else {}) for s, v in by_src.items()},
                  "lora_cet_events": lora_ev,
                  "lora_distinct_by_src": {s: len(v) for s, v in lora.items()},
                  "lora_gaps_by_src": {s: gaps_report(list(v)) for s, v in lora.items()}}
    # coverage vs union over the OE3 nodes (OE1XAR-33 only)
    for n in CLUSTER:
        mine = per_node_lora[n].get("OE1XAR-33", set())
        if not mine:
            continue
        lo, hi = min(mine), max(mine)  # restrict the union to the node's own span (OE3XIR stops after 11.5 h)
        un = {t for t in union.get("OE1XAR-33", set()) if lo <= t <= hi}
        res[n]["lora_oe1xar33_heard_of_union"] = {"union_distinct_in_span": len(un), "heard": len(mine & un),
                                                  "pct": round(100.0 * len(mine & un) / len(un), 1) if un else None}
    out.write_text(json.dumps(res, indent=1, default=str))


if __name__ == "__main__":
    main()
