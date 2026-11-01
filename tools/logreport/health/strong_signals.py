#!/usr/bin/env python3
"""Strong-signal / overload / interferer scan.

For every node: receptions (decoded [LOG] RX/ACK + RX_ERROR) with RSSI above a threshold,
bursty? (per hour histogram, clusters), and which last hops produce them.
Usage (via run.py): strong_signals.py [thr_dbm=-70]; writes <out>/strong_signals.json
"""
from __future__ import annotations
import collections
import json
import re
import sys

from nodeset import ALL as NODES, NORM, OUT, STANDALONE
RE_PREF = re.compile(r"^(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d)\.\d{3}  ")
RE_RX = re.compile(r"\[LOG\] (\d{3}) (\S) x\w{8} H\w\w S\d T\d M\d+ (\S+?)>.* FCS:(\w{4}) .*RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w)")
RE_ERR = re.compile(r"RX_ERROR rssi=(-?\d+) snr=(-?\d+) ts=(\d+)")


def main() -> None:
    norm, out = NORM, OUT / "strong_signals.json"
    thr = int(sys.argv[1]) if len(sys.argv) > 1 else -70
    res = {}
    for n in NODES:
        hour_dec: collections.Counter[str] = collections.Counter()
        hour_err: collections.Counter[str] = collections.Counter()
        last_hops: collections.Counter[str] = collections.Counter()
        snr_dec: list[int] = []
        snr_err: list[int] = []
        err_total = dec_total = 0
        strong_err = strong_dec = 0
        cur_ms = 0
        cur_hour = "?"
        with open(norm / f"{n}.log", "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = RE_PREF.match(line)
                if m:
                    cur_hour = f"{m.group(1)[5:]} {m.group(2)}"
                    body = line[m.end():]
                else:
                    body = line
                    if n in STANDALONE:
                        mm = re.search(r"\bt=(\d+)\b|\bts=(\d+)\b", body)
                        if mm:
                            ms = int(mm.group(1) or mm.group(2))
                            if ms >= cur_ms:
                                cur_ms = ms
                        h = cur_ms // 3600000
                        cur_hour = f"+{h:02d}h"
                if "RX_ERROR" in body:
                    mm = RE_ERR.search(body)
                    if mm:
                        err_total += 1
                        r = int(mm.group(1))
                        if r > thr:
                            strong_err += 1
                            hour_err[cur_hour] += 1
                            snr_err.append(int(mm.group(2)))
                elif "[LOG]" in body and "RSSI:" in body:
                    mm = RE_RX.search(body)
                    if mm and mm.group(4) != "0000":
                        dec_total += 1
                        r = int(mm.group(5))
                        if r > thr:
                            strong_dec += 1
                            hour_dec[cur_hour] += 1
                            snr_dec.append(int(mm.group(6)))
                            path = mm.group(3).split(",")
                            last_hops[path[-1]] += 1
        res[n] = {"thr_dbm": thr, "decoded_total": dec_total, "decoded_strong": strong_dec, "err_total": err_total, "err_strong": strong_err,
                  "strong_decoded_per_hour": dict(sorted(hour_dec.items())), "strong_err_per_hour": dict(sorted(hour_err.items())),
                  "last_hops_strong": dict(last_hops.most_common(10)),
                  "snr_decoded_strong_median": sorted(snr_dec)[len(snr_dec) // 2] if snr_dec else None,
                  "snr_err_strong_median": sorted(snr_err)[len(snr_err) // 2] if snr_err else None}
    out.write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
