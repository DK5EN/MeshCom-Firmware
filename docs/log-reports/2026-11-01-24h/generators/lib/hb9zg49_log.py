#!/usr/bin/env python3
"""HB9ZG-49 receive check from an HB9 [LOG] capture -> data/hb9/extra/hb9zg49_log.json.

Usage: python3 -I hb9zg49_log.py --norm <norm dir> --out <json> [--log hb9vqq4.log] [--call HB9ZG-49]

HEY frames (type '@', payload 'R1;' + one 'NCNT,RSSI,SNR;' group per relay) carry, per relay k>=1 in
the path, its reception of path element k-1 (src/aprs_functions.cpp appendHeySignalReport). That shows
what a relay hears, independent of the logging node. Unique per (msg id, relay); FCS:0000 excluded.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics as st
from collections import Counter, defaultdict
from pathlib import Path

RX = re.compile(r"^(\S+ \S+)\s+\S+ \[LOG\] \d+ (\S) x([0-9A-F]+) H\d+ S\d T\d M\d+ (\S+?)>(.*?) HW:\S+ MOD:\S+ "
                r"FCS:(\S+) FW:\S+ LH:\S+ RSSI:(-?\d+) SNR:(-?\d+) DUP:\S")
GROUP = re.compile(r"\d+,\d+,-?\d+")
GROUP2 = re.compile(r"\d+,-?\d+")


def hey_groups(payload: str) -> list[tuple[int, int, int]]:
    m = re.match(r"[^@]*@?R\d;(.*)", payload)
    if not m:
        return []
    out: list[tuple[int, int, int]] = []
    for g in m.group(1).split(";"):
        if GROUP.fullmatch(g):  # 4.40: NCNT,RSSI,SNR
            n, r, sn = (int(x) for x in g.split(","))
            out.append((n, r, sn))
        elif GROUP2.fullmatch(g):  # 4.35 relays: RSSI,SNR (keeps the position of later groups)
            r, sn = (int(x) for x in g.split(","))
            out.append((-1, r, sn))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--norm", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--log", default="hb9vqq4.log")
    ap.add_argument("--call", default="HB9ZG-49")
    a = ap.parse_args()
    zg = a.call
    first_ts = last_ts = ""
    lasthop_rssi: list[int] = []
    lasthop_kind: Counter[str] = Counter()
    seen_lasthop: set[tuple[str, int]] = set()
    seen_rep: set[tuple[str, str, str]] = set()
    reports: dict[str, list[tuple[int, int, int, str]]] = defaultdict(list)  # relay -> (ncnt, rssi, snr, prev)
    vqq2_seen_by_zg = 0
    for line in (a.norm / a.log).open(errors="replace"):
        m = RX.match(line)
        if not m or m.group(6) == "0000":
            continue
        ts, typ, mid, path, rest = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
        first_ts = first_ts or ts
        last_ts = ts
        calls = path.split(",")
        if calls[-1] == zg and (mid, len(calls)) not in seen_lasthop:
            seen_lasthop.add((mid, len(calls)))
            lasthop_rssi.append(int(m.group(7)))
            lasthop_kind["eigen" if len(calls) == 1 else "relais"] += 1
        if zg in calls[1:] and "HB9VQQ-2" in calls[: calls.index(zg)]:
            vqq2_seen_by_zg += 1
        if typ != "@":
            continue
        g = hey_groups(rest)
        for k in range(1, len(calls)):
            if k - 1 < len(g) and (mid, calls[k], calls[k - 1]) not in seen_rep:
                seen_rep.add((mid, calls[k], calls[k - 1]))
                n, r, s = g[k - 1]
                reports[calls[k]].append((n, -r, s, calls[k - 1]))

    def summary(v: list[tuple[int, int, int, str]]) -> dict:
        band = [r for _, r, s, _ in v if -17 <= s <= -12]
        return {
            "n": len(v),
            "ncnt_median": st.median([x[0] for x in v if x[0] >= 0] or [-1]),
            "rssi_median": st.median(x[1] for x in v),
            "snr_median": st.median(x[2] for x in v),
            "share_at_floor": round(sum(1 for x in v if x[1] <= -127) / len(v), 3),
            "band_snr_m17_m12": {"n": len(band), "rssi_median": st.median(band) if band else None},
            "prev_hops": Counter(x[3] for x in v).most_common(),
            "rssi_hist": sorted(Counter(x[1] for x in v).items()),
        }

    relays = {c: summary(v) for c, v in reports.items() if len(v) >= 50 or c == zg}
    out = {
        "source": a.log,
        "window": [first_ts, last_ts],
        "call": zg,
        "lasthop": {"unique_frames": len(lasthop_rssi), "kind": dict(lasthop_kind),
                    "rssi_median": st.median(lasthop_rssi) if lasthop_rssi else None,
                    "rssi_min": min(lasthop_rssi, default=None), "rssi_max": max(lasthop_rssi, default=None)},
        "frames_zg_relayed_after_vqq2": vqq2_seen_by_zg,
        "relays": relays,
    }
    a.out.write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n")
    print(f"wrote {a.out}: {len(relays)} relays, ZG reports {relays.get(zg, {}).get('n')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
