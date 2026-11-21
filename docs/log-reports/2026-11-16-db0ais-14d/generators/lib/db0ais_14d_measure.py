#!/usr/bin/env python3
"""TX-ring / loss / ETH measurements for the DB0AIS 14-day capture. Stdlib only.

Usage: python3 -I measure.py <full.log> <freeze_line> <out.json>
Time base: millis from t=/ts=/ms; fields; boot = 2026-10-30 17:01:11 CET (capture start).
"""
from __future__ import annotations

import json
import re
import statistics as st
import sys
from collections import Counter, defaultdict
from datetime import datetime, timedelta

BOOT = datetime(2026, 10, 30, 17, 1, 11)
MS = re.compile(rb"(?:\bt=|\bts=|;ms;)(\d{4,})")
RW = re.compile(rb"RING_WRITE slot=(-?\d+) type=(\w+) .*queued=(\d+)/(\d+) src=(\w+)")
DN = re.compile(rb"RING_DROP_NEW slot=-?\d+ prio=(\d) type=(\w+) msg_id=(\w+)")
DP = re.compile(rb"RING_DROP_PRIO slot=-?\d+ prio=(\d) type=(\w+) msg_id=(\w+) replaced_by_prio=(\d) src=(\w+)")
RLY = re.compile(rb"\[LOG\] RLY x(\w+) (.) H\d+ q=(\w+)")
TXL = re.compile(rb"\[LOG\] TX x(\w+) (.) H\d+ prio=(\d) src=(\w) wait=(\d+) q=(\d+) cad=(\d+) len=(\d+)")
TXR = re.compile(rb"RING_TX_READ .*lat=(\d+)ms")
UTIL = re.compile(rb"CHANNEL_UTIL rx=(\d+)ms tx=(\d+)ms util=(\d+)%")
RXN = re.compile(rb"\[LOG\] \d{3} (.) x(\w+) .*DUP:n")


def day_of(ms: int) -> str:
    return (BOOT + timedelta(milliseconds=ms)).strftime("%Y-%m-%d")


def main() -> None:
    path, freeze, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    seg = {"healthy": defaultdict(Counter), "hang": defaultdict(Counter)}
    qdepth = Counter()
    qmax_day: dict[str, int] = defaultdict(int)
    qsize = set()
    waits: dict[str, list[int]] = defaultdict(list)
    lat: list[int] = []
    per_day = defaultdict(Counter)
    util_day: dict[str, list[int]] = defaultdict(list)
    drops_text: list[dict] = []
    last_ms = 0
    first_hang_ms = None
    eth_down: list[int] = []
    ntp = Counter()
    with open(path, "rb") as fh:
        for ln, line in enumerate(fh, 1):
            m = MS.search(line)
            if m:
                v = int(m.group(1))
                if v >= last_ms:
                    last_ms = v
            part = "healthy" if ln <= freeze else "hang"
            if part == "hang" and first_hang_ms is None:
                first_hang_ms = last_ms
            c = seg[part]
            d = day_of(last_ms)
            if b"RING_WRITE" in line and (m := RW.search(line)):
                q, n = int(m.group(3)), int(m.group(4))
                qsize.add(n)
                c["ring_write"][m.group(5).decode()] += 1
                if part == "healthy":
                    qdepth[q] += 1
                    qmax_day[d] = max(qmax_day[d], q)
            elif b"RING_DROP_NEW" in line and (m := DN.search(line)):
                c["drop_new"][m.group(2).decode()] += 1
                if part == "healthy":
                    per_day[d]["drop_new_" + m.group(2).decode()] += 1
                    if m.group(2) in (b"3A", b"41"):
                        drops_text.append({"ms": last_ms, "type": m.group(2).decode(), "id": m.group(3).decode()})
            elif b"RING_DROP_PRIO" in line and (m := DP.search(line)):
                c["drop_prio"][m.group(2).decode() + ">" + m.group(4).decode()] += 1
                if part == "healthy":
                    per_day[d]["drop_prio_" + m.group(2).decode()] += 1
            elif b"[LOG] RLY" in line and (m := RLY.search(line)):
                c["rly_q"][m.group(3).decode()] += 1
                if part == "healthy":
                    per_day[d]["rly_" + m.group(3).decode()] += 1
            elif b"[LOG] TX x" in line and (m := TXL.search(line)):
                c["tx"][m.group(4).decode()] += 1
                if part == "healthy":
                    waits[m.group(4).decode() + m.group(3).decode()].append(int(m.group(5)))
                    per_day[d]["tx"] += 1
            elif b"RING_TX_READ" in line and (m := TXR.search(line)):
                lat.append(int(m.group(1)))
            elif b"CHANNEL_UTIL" in line and (m := UTIL.search(line)):
                if part == "healthy":
                    util_day[d].append(int(m.group(3)))
            elif b"RING_OVERFLOW" in line:
                c["overflow"][line.split(b"buf=")[1].split()[0].decode(errors="replace")] += 1
            elif b"[ETH];link;down" in line:
                eth_down.append(last_ms)
            elif b"[NTP];" in line:
                ntp[line.split(b";")[1].decode(errors="replace")] += 1
            elif b"[KEEP]" in line:
                c["keep"]["n"] += 1
            elif b"CSMA_AGED" in line:
                c["csma_aged"]["n"] += 1
            if b"DUP:n" in line and (m := RXN.search(line)):
                c["rx_new"][m.group(1).decode()] += 1
                if part == "healthy":
                    per_day[d]["rx_new"] += 1

    def pct(xs: list[int], p: float) -> int:
        xs = sorted(xs)
        return xs[min(len(xs) - 1, int(p * len(xs)))] if xs else 0

    res = {
        "boot": BOOT.isoformat(sep=" "),
        "freeze_line": freeze,
        "freeze_ms": first_hang_ms,
        "freeze_wall": (BOOT + timedelta(milliseconds=first_hang_ms or 0)).isoformat(sep=" ", timespec="seconds"),
        "end_ms": last_ms,
        "end_wall": (BOOT + timedelta(milliseconds=last_ms)).isoformat(sep=" ", timespec="seconds"),
        "qsize": sorted(qsize),
        "qdepth": dict(sorted(qdepth.items())),
        "qmax_day": dict(sorted(qmax_day.items())),
        "segments": {k: {kk: dict(vv) for kk, vv in v.items()} for k, v in seg.items()},
        "per_day": {k: dict(v) for k, v in sorted(per_day.items())},
        "util_day": {k: {"mean": round(st.mean(v), 1), "max": max(v), "n": len(v)} for k, v in sorted(util_day.items())},
        "wait": {k: {"n": len(v), "median": int(st.median(v)), "p95": pct(v, .95), "max": max(v)} for k, v in sorted(waits.items())},
        "lat": {"n": len(lat), "median": int(st.median(lat)) if lat else 0, "p95": pct(lat, .95), "p99": pct(lat, .99), "max": max(lat) if lat else 0},
        "drops_text": [{**x, "wall": (BOOT + timedelta(milliseconds=x["ms"])).isoformat(sep=" ", timespec="seconds")} for x in drops_text],
        "eth_down_wall": [(BOOT + timedelta(milliseconds=x)).isoformat(sep=" ", timespec="seconds") for x in eth_down],
        "ntp": dict(ntp),
    }
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(res, fh, indent=1)


if __name__ == "__main__":
    main()
