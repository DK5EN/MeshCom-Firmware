#!/usr/bin/env python3
"""One pass over a raw node log: total decoded RX airtime vs {CET} RX airtime, all own TX by prio/src,
TX queue wait per class (CET vs others), plus NTP ok/timeout counts.

Usage (via run.py): rawpass.py <node>; reads <norm>/<node>.log, writes <out>/data/raw_<node>.json
"""
import json
import math
import re
import sys
from collections import Counter, defaultdict


def airtime_ms(L: int) -> float:
    tsym = 2 ** 11 / 250.0
    npay = 8 + max(math.ceil((8 * L - 4 * 11 + 28 + 16) / (4 * 11)), 0) * 6
    return (npay + 8.25) * tsym


def pct(v, p):
    s = sorted(v)
    if not s:
        return None
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def st(v):
    return {"n": len(v), "p50": round(pct(v, 50), 0), "p90": round(pct(v, 90), 0), "max": max(v)} if v else None


from nodeset import NORM, OUT

node = sys.argv[1]
log = str(NORM / f"{node}.log")
out = str(OUT / "data" / f"raw_{node}.json")
RX = re.compile(rb"\[LOG\] (\d{3}) (\S) x([0-9A-F]{8}) H[0-9A-F]{2}.*?(?: FCS:([0-9A-F]{4}))?.* RSSI:-?\d+ SNR:-?\d+ DUP:(\w) OWN:")
TXR = re.compile(rb"\[LOG\] TX x([0-9A-F]{8}) (\S) H[0-9A-F]{2} prio=(\d) src=(\w) wait=(\d+) q=(\d+) cad=(\d+) len=(\d+)")
rx_total = 0.0
rx_cet = 0.0
n_rx = Counter()
air_by_type = defaultdict(float)
fcs0 = 0
tx = []  # (cls, prio, src, wait, len, cad)
ntp_ok = ntp_to = 0
rtts = []
cet_ids = set()
with open(log, "rb") as f:
    for line in f:
        if b"{CET}" in line and b" : x" in line:
            m = re.search(rb" : x([0-9A-F]{8}) H", line)
            if m:
                cet_ids.add(m.group(1))
with open(log, "rb") as f:
    for line in f:
        if b"[LOG] " in line:
            if b" RSSI:" in line:
                m = RX.search(line)
                if m:
                    L = int(m.group(1))
                    fcs = m.group(4)
                    if fcs == b"0000":
                        fcs0 += 1
                        continue
                    a = airtime_ms(L)
                    rx_total += a
                    t = m.group(2).decode()
                    air_by_type[t] += a
                    n_rx[t] += 1
                    if b"{CET}" in line:
                        rx_cet += a
                continue
            m = TXR.search(line)
            if m:
                i = m.group(1)
                cls = "cet" if i in cet_ids else "other"
                tx.append((cls, int(m.group(3)), m.group(4).decode(), int(m.group(5)), int(m.group(8)), int(m.group(7))))
                continue
        elif line.find(b"[NTP];ok") >= 0:
            ntp_ok += 1
            mm = re.search(rb"rtt;(\d+)", line)
            if mm:
                rtts.append(int(mm.group(1)))
        elif line.find(b"[NTP];timeout") >= 0:
            ntp_to += 1
res = {"node": node,
       "rx_decoded_frames": sum(n_rx.values()), "rx_fcs0000": fcs0,
       "rx_decoded_airtime_s": round(rx_total / 1000, 1), "rx_cet_airtime_s": round(rx_cet / 1000, 1),
       "rx_cet_share_of_decoded_airtime_pct": round(100.0 * rx_cet / max(1, rx_total), 2),
       "rx_airtime_by_type_s": {k: round(v / 1000, 1) for k, v in sorted(air_by_type.items(), key=lambda x: -x[1])},
       "rx_frames_by_type": dict(n_rx.most_common()),
       "ntp_ok": ntp_ok, "ntp_timeout": ntp_to, "ntp_rtt_ms": st(rtts)}
by = defaultdict(list)
for cls, prio, src, wait, L, cad in tx:
    by[(cls, prio, src)].append(wait)
res["tx_total"] = len(tx)
res["tx_airtime_s_all"] = round(sum(airtime_ms(t[4]) for t in tx) / 1000, 1)
res["tx_airtime_s_cet"] = round(sum(airtime_ms(t[4]) for t in tx if t[0] == "cet") / 1000, 1)
res["tx_cet_share_airtime_pct"] = round(100.0 * res["tx_airtime_s_cet"] / max(0.001, res["tx_airtime_s_all"]), 2)
res["tx_by_class_prio_src"] = {f"{k[0]}/p{k[1]}/{k[2]}": {"n": len(v), **(st(v) or {})} for k, v in sorted(by.items())}
# wait of non-CET traffic by prio
res["tx_wait_other_by_prio_ms"] = {f"p{p}": st([t[3] for t in tx if t[0] == "other" and t[1] == p]) for p in range(1, 6)}
# prio-3 (relay class) wait: CET vs other relays
res["tx_wait_prio3_cet_ms"] = st([t[3] for t in tx if t[0] == "cet"])
res["tx_wait_prio3_other_ms"] = st([t[3] for t in tx if t[0] == "other" and t[1] == 3])
json.dump(res, open(out, "w"), indent=1)
print(node, res["rx_cet_share_of_decoded_airtime_pct"], res["tx_cet_share_airtime_pct"], res["tx_total"])
