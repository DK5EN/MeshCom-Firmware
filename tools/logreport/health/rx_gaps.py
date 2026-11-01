#!/usr/bin/env python3
"""Longest receive silences per node (gaps between consecutive OnRxDone/OnRxError lines) and what the other nodes saw meanwhile.

A received frame is one ONRXDONE_TIME marker (12-byte ACK frames print no OnRxDone line, so counting OnRxDone
alone reports ACK-filled intervals as silences); logs without that marker fall back to OnRxDone. OnRxError always counts.

Usage (via run.py): rx_gaps.py; writes <out>/rx_gaps.json
Cluster logs (wall prefix) first, standalone logs from node millis.  A silence with CAD_SAFETY_TIMEOUT inside = radio did not return CadDone (src/nrf52/nrf52_main.cpp:1790).
"""
from __future__ import annotations
import bisect
import datetime as dt
import json
import re

from nodeset import CLUSTER as OE3, NORM, OUT, STANDALONE, STANDALONE_START
RE = re.compile(r"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  (OnRxDone|OnRxError|\[MC-DBG\] ONRXDONE_TIME|\[MC-DBG\] CAD_SAFETY_TIMEOUT|\[MC-DBG\] RADIO_TX)")


def ts(m: re.Match) -> float:
    return dt.datetime(*(int(x) for x in m.groups()[:6])).replace(tzinfo=dt.timezone.utc).timestamp() + int(m.group(7)) / 1000.0


def main() -> None:
    norm, out = NORM, OUT / "rx_gaps.json"
    rx: dict[str, list[float]] = {}
    tx: dict[str, list[float]] = {}
    sf: dict[str, list[float]] = {}
    for n in OE3:
        r: list[float] = []
        r_done: list[float] = []
        r_time: list[float] = []
        t: list[float] = []
        s: list[float] = []
        with open(norm / f"{n}.log", "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if line[:4].isdigit() and line[4:5] == "-" and ("OnRx" in line or "ONRXDONE_TIME" in line or "CAD_SAFETY" in line or "RADIO_TX" in line):
                    m = RE.match(line)
                    if not m:
                        continue
                    k = m.group(8)
                    if k == "OnRxDone":
                        r_done.append(ts(m))
                    elif k.endswith("ONRXDONE_TIME"):
                        r_time.append(ts(m))
                    else:
                        (r if k == "OnRxError" else (s if "SAFETY" in k else t)).append(ts(m))
        rx[n], tx[n], sf[n] = sorted(r + (r_time or r_done)), t, s
    res: dict = {}
    for n in OE3:
        g = sorted(((b - a, a, b) for a, b in zip(rx[n], rx[n][1:])), reverse=True)[:6]
        rows = []
        for d, a, b in g:
            others = {}
            for o in OE3:
                if o == n:
                    continue
                i, j = bisect.bisect_left(rx[o], a), bisect.bisect_right(rx[o], b)
                others[o] = j - i
            rows.append({"start": dt.datetime.fromtimestamp(a, dt.timezone.utc).strftime("%m-%d %H:%M:%S"), "gap_s": round(d, 1),
                         "own_tx_in_gap": bisect.bisect_right(tx[n], b) - bisect.bisect_left(tx[n], a),
                         "cad_safety_timeouts_in_gap": bisect.bisect_right(sf[n], b) - bisect.bisect_left(sf[n], a),
                         "rx_events_at_other_oe3_nodes": others})
        allg = [(b - a, a, b) for a, b in zip(rx[n], rx[n][1:])]
        big = [(d, a, b) for d, a, b in allg if d > 300]
        span = rx[n][-1] - rx[n][0]
        res[n] = {"rx_events": len(rx[n]), "median_gap_s": round(sorted(d for d, _, _ in allg)[len(allg) // 2], 2),
                  "gaps_gt_300s": len(big), "silent_seconds_in_gaps_gt_300s": round(sum(d for d, _, _ in big)), "silent_pct_of_span": round(100.0 * sum(d for d, _, _ in big) / span, 1),
                  "gaps_gt_300s_list": [{"start": dt.datetime.fromtimestamp(a, dt.timezone.utc).strftime("%m-%d %H:%M:%S"), "gap_s": round(d),
                                         "own_tx": bisect.bisect_right(tx[n], b) - bisect.bisect_left(tx[n], a),
                                         "others_rx": {o: bisect.bisect_right(rx[o], b) - bisect.bisect_left(rx[o], a) for o in OE3 if o != n}} for d, a, b in sorted(big, key=lambda x: x[1])],
                  "longest_gaps": rows}
    # standalone logs: no wall prefix; use the node millis `t=` of decoded/ERR [LOG] lines (STAT/TX/RLY/GW*/DM excluded)
    boot0 = dt.datetime.fromisoformat(STANDALONE_START)
    for sn in STANDALONE:
        t_ms: list[int] = []
        skip = (" STAT ", " TX x", " GWI", " GWU", " DM ", " RLY")
        with open(norm / f"{sn}.log", "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                if "[LOG]" in line and " t=" in line and not any(k in line for k in skip):
                    m = re.search(r" t=(\d+)\s*$", line)
                    if m:
                        t_ms.append(int(m.group(1)))
        t_ms.sort()
        gg = [((b - a) / 1000.0, a) for a, b in zip(t_ms, t_ms[1:])]
        res[sn] = {"rx_events": len(t_ms), "median_gap_s": round(sorted(d for d, _ in gg)[len(gg) // 2], 2),
                         "gaps_gt_300s": sum(1 for d, _ in gg if d > 300), "silent_seconds_in_gaps_gt_300s": round(sum(d for d, _ in gg if d > 300)),
                         "silent_pct_of_span": round(100.0 * sum(d for d, _ in gg if d > 300) / ((t_ms[-1] - t_ms[0]) / 1000.0), 1),
                         "longest_gaps": [{"start": (boot0 + dt.timedelta(milliseconds=a)).strftime("%m-%d %H:%M:%S"), "gap_s": round(d, 1)} for d, a in sorted(gg, reverse=True)[:4]]}
    out.write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
