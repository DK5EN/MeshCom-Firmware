#!/usr/bin/env python3
"""M2: measured relay count per aired own beacon (own WX / own HEY) at OE3XOC and OE3XIA.

Usage: python3 -I relays.py <norm_dir> <lrf_m2.json> [--nodes oe3xoc,oe3xia]   (merges key "relays" into the json written by replay.py)

Aired own beacon = RING_TX_READ of an entry written with src user_wx (type 21, WX) or
auto_pos (type 40, HEY) in the origin log. Relay by logger L = L's own log has a
RING_TX_READ with the same msg_id (relays keep the msg_id; ids are per-originator counters)
at t in [t_tx - 2 s, t_tx + 300 s]. Loggers without ring lines (db0ais) cannot be matched.
Direct hearing = a [LOG] RX line with that msg_id whose path is exactly the origin call.
Only loggers whose log covers [t_tx - 2 s, t_tx + 300 s] count for that beacon.
Logger clocks are the PC clocks of each logger (offsets of a few seconds are inside the window).
"""
from __future__ import annotations

import json
import re
import sys
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  ")
W = re.compile(r"RING_WRITE slot=(\d+) type=([0-9A-F]+) status=[0-9A-F]+ len=\d+ msg_id=([0-9A-F]{8}) queued=\d+/\d+ src=(\w+)")
TXR = re.compile(r"RING_TX_READ slot=\d+ prio=\d type=[0-9A-F]+ status=[0-9A-F]+ len=\d+ msg_id=([0-9A-F]{8}) ")
RXL = re.compile(r"\[LOG\] \d+ \S+ x([0-9A-F]{8}) H\d+ S\d T\d M\d+ ([^>\s]+)>")
LOGGERS = ["oe3xoc", "oe3xia", "oe3xhl", "oe3xir", "oe3xwj", "hb9vqq4", "hb9vqq5", "db0ais"]
CALL = {"oe3xoc": "OE3XOC-12", "oe3xia": "OE3XIA-12"}


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def origin_beacons(path):
    src = {}
    out = []
    t0 = t1 = None
    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace")
            m0 = TS.match(line)
            if not m0:
                continue
            t = ts(m0.group(1))
            t0 = t0 or t
            t1 = t
            if "RING_WRITE" in line and (m := W.search(line)):
                if m.group(4) in ("user_wx", "auto_pos"):
                    src[m.group(3)] = "WX" if m.group(4) == "user_wx" else "HEY"
            elif "RING_TX_READ" in line and (m := TXR.search(line)):
                if m.group(1) in src:
                    out.append((m.group(1), src[m.group(1)], t))
    return out, t0, t1


def scan(path, ids, origin_call):
    tx, heard = {}, {}
    t0 = t1 = None
    n_ring = 0
    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace")
            m0 = TS.match(line)
            if not m0:
                continue
            t = ts(m0.group(1))
            t0 = t0 or t
            t1 = t
            if "RING_TX_READ" in line:
                n_ring += 1
                m = TXR.search(line)
                if m and m.group(1) in ids:
                    tx.setdefault(m.group(1), []).append(t)
            elif "[LOG]" in line and "RSSI:" in line:
                m = RXL.search(line)
                if m and m.group(1) in ids and m.group(2) == origin_call:
                    heard.setdefault(m.group(1), []).append(t)
    return tx, heard, t0, t1, n_ring


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    nodes = "oe3xoc,oe3xia"
    if "--nodes" in argv:
        nodes = argv[argv.index("--nodes") + 1]
        args = [a for a in args if a != nodes]
    norm, outp = args[0], args[1]
    out = {"matching": "relay = neighbour RING_TX_READ with same msg_id in [t_tx-2, t_tx+300] s; heard = RX [LOG] line path == origin call",
           "nodes": {}}
    for node in nodes.split(","):
        beacons, o0, o1 = origin_beacons(f"{norm}/{node}.log")
        ids = {b[0] for b in beacons}
        others = [l for l in LOGGERS if l != node]
        scans = {}
        for l in others:
            scans[l] = scan(f"{norm}/{l}.log", ids, CALL[node].upper())
        res = {"origin_call": CALL[node], "loggers": {l: {"covers_from": datetime.fromtimestamp(scans[l][2]).strftime("%m-%d %H:%M") if scans[l][2] else "no timestamps",
                                                          "covers_to": datetime.fromtimestamp(scans[l][3]).strftime("%m-%d %H:%M") if scans[l][3] else "-",
                                                          "ring_tx_read_lines": scans[l][4]} for l in others}}
        usable = [l for l in others if scans[l][4] > 0]
        res["usable_loggers"] = usable
        res["unusable_loggers"] = [l for l in others if l not in usable]
        recs = []
        for bid, cls, t in beacons:
            cov = [l for l in usable if scans[l][2] <= t - 2 and scans[l][3] >= t + 300]
            rel = [l for l in cov if any(t - 2 <= x <= t + 300 for x in scans[l][0].get(bid, []))]
            hrd = [l for l in cov if any(t - 2 <= x <= t + 300 for x in scans[l][1].get(bid, []))]
            recs.append({"id": bid, "cls": cls, "t": datetime.fromtimestamp(t).strftime("%m-%d %H:%M:%S"),
                         "covered": len(cov), "covered_by": cov, "relayed_by": rel, "heard_by": hrd})
        res["beacons"] = recs
        summ = {}
        for cls in ("WX", "HEY"):
            R = [r for r in recs if r["cls"] == cls and r["covered"] > 0]
            if not R:
                continue
            cnt = [len(r["relayed_by"]) for r in R]
            hc = [len(r["heard_by"]) for r in R]
            hist = {}
            for c in cnt:
                hist[c] = hist.get(c, 0) + 1
            cs = sorted(cnt)
            full = [r for r in R if r["covered"] == len(usable)]
            per_l = {}
            for l in usable:
                cov_l = [r for r in R if any(r2 is r for r2 in R) and l in r["covered_by"]]
                if cov_l:
                    per_l[l] = f"{sum(1 for r in cov_l if l in r['relayed_by'])}/{len(cov_l)}"
            summ[cls] = {"aired_beacons": sum(1 for r in recs if r["cls"] == cls), "with_logger_coverage": len(R),
                         "relay_count_mean": round(sum(cnt) / len(cnt), 2), "relay_count_median": cs[len(cs) // 2],
                         "relay_count_hist": dict(sorted(hist.items())), "relayed_by_logger": per_l, "zero_relays": hist.get(0, 0),
                         "heard_directly_mean": round(sum(hc) / len(hc), 2),
                         "beacons_with_all_usable_loggers_covered": len(full)}
        res["summary"] = summ
        out["nodes"][node] = res
        print(f"== {node} origin {CALL[node]}: usable loggers {usable}, unusable {res['unusable_loggers']}")
        for l in others:
            print(f"   {l:8s} covers {res['loggers'][l]['covers_from']} .. {res['loggers'][l]['covers_to']} ring_tx_read_lines={res['loggers'][l]['ring_tx_read_lines']}")
        for cls, s in summ.items():
            print(f"-- {node} {cls}: {s}")
    # merge into the replay.py output (run replay.py first): key "relays"
    try:
        with open(outp) as fh:
            full = json.load(fh)
    except (OSError, ValueError):
        full = {}
    full["relays"] = out
    with open(outp, "w") as fh:
        json.dump(full, fh, indent=1)


if __name__ == "__main__":
    main(sys.argv)
