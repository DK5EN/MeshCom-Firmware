#!/usr/bin/env python3
"""q5 extra checks: funnel with gateways inferred from relay paths, NEED alone/need popcount,
DB0AIS NEED lines for {CET} ids.

Usage: python3 -I v_cet_extra.py <norm-dir>
"""
from __future__ import annotations

import json
import re
import statistics
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
OWN = {"OE3XHL-12", "OE3XIA-12", "OE3XIR-12", "OE3XOC-12", "OE3XWJ-12"}
PFX = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
FR = re.compile(r"\[LOG\]\s+\d+ : x([0-9A-F]{8}) H\d\d .*? ([A-Z0-9-]+(?:,[A-Z0-9-]+)*)>\*:\{CET\}(\S+ \S+)")
NEED = re.compile(r"\[NBR\]\|NEED\|\d+\|([0-9A-F]{8})\|T\|(\w)\|([0-9A-Fa-f]+)\|([0-9A-Fa-f]+)\|")


def utc(s: str, fmt: str) -> float:
    return datetime.strptime(s, fmt).replace(tzinfo=timezone.utc).timestamp()


def main() -> None:
    d = Path(sys.argv[1])
    paths: dict[str, set[str]] = defaultdict(set)
    content: dict[str, float] = {}
    win: list[tuple[float, float]] = []
    ratio: list[float] = []
    for n in NODES:
        first = last = 0.0
        with open(d / f"{n}.log", "rb") as fh:
            for raw in fh:
                line = raw.decode("latin-1")
                m = PFX.match(line)
                if not m:
                    continue
                t = utc(m.group(1), "%Y-%m-%d %H:%M:%S.%f") - 3600
                first = first or t
                last = t
                b = m.group(2)
                if "{CET}" in b and "FCS:0000" not in b and (f := FR.search(b)):
                    paths[f.group(1)].add(f.group(2))
                    content[f.group(1)] = utc(f.group(3), "%Y-%m-%d %H:%M:%S")
                elif (q := NEED.search(b)):
                    need = bin(int(q.group(3), 16)).count("1")
                    alone = bin(int(q.group(4), 16)).count("1")
                    if need:
                        ratio.append(alone / need)
        win.append((first, last))
    w0, w1 = max(w[0] for w in win), min(w[1] for w in win)
    direct, inferred, relays = [], [], []
    for mid, ps in paths.items():
        if not (w0 <= content[mid] <= w1 - 600):
            continue
        gw_direct = {p.split(",")[1] for p in ps if len(p.split(",")) == 2} | OWN
        gw_any = {p.split(",")[1] for p in ps if len(p.split(",")) >= 2} | OWN
        direct.append(len(gw_direct - OWN))
        inferred.append(len(gw_any - OWN))
        relays.append(sum(1 for p in ps if len(p.split(",")) > 2))
    out = {
        "window_telegrams": len(direct),
        "foreign_gw_heard_direct": {"min": min(direct), "p50": statistics.median(direct), "max": max(direct)},
        "foreign_gw_incl_inferred_from_relay_paths": {"min": min(inferred), "p50": statistics.median(inferred),
                                                     "max": max(inferred)},
        "relay_paths": {"min": min(relays), "p50": statistics.median(relays), "max": max(relays)},
        "need_lines_T_alone_over_need_popcount": {"n": len(ratio), "p50": round(statistics.median(ratio), 2),
                                                 "eq1": sum(1 for r in ratio if r == 1.0)},
    }
    # DB0AIS: NEED T lines for CET msg ids
    ids: set[str] = set()
    needs: list[tuple[str, str]] = []
    with open(d / "db0ais.log", "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1")
            if "{CET}" in line and (f := re.search(r"x([0-9A-F]{8}) H\d\d ", line)):
                ids.add(f.group(1))
            elif (q := NEED.search(line)):
                needs.append((q.group(1), q.group(2)))
    c = Counter(case for mid, case in needs if mid in ids)
    out["db0ais_need_T_for_cet_ids"] = dict(c)
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
