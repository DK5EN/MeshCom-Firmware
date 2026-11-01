#!/usr/bin/env python3
"""M5 verify (inference): what the OE3XIA-12 radio state machine does inside and at the edges of silences.

usage: python3 -I v5_edges.py <v1.json> <oe3xia.log> <out.json>
Counts RX_RESTART (by src) and [MC-SM] transitions inside silences vs outside (rate per hour),
and the time from the last TX_DONE to each silence end / silence start.
"""
import json
import re
import sys
from bisect import bisect_left, bisect_right
from collections import Counter
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")
SM = re.compile(r"\[MC-SM\] (\w+ -> \w+)")
RR = re.compile(r"RX_RESTART src=(\w+)")


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def main() -> None:
    v1 = json.load(open(sys.argv[1]))
    sil = [(d["a"], d["b"]) for d in v1["silences"]]
    starts = [a for a, _ in sil]
    span = [datetime.fromisoformat(x).timestamp() for x in v1["span"]]
    tin = sum(b - a for a, b in sil)
    tout = span[1] - span[0] - tin
    cin, cout = Counter(), Counter()
    txdone = []
    with open(sys.argv[2], errors="replace") as f:
        for line in f:
            tm = TS.match(line)
            if not tm:
                continue
            m = SM.search(line) or RR.search(line)
            if not m:
                continue
            t = ts(tm.group(1))
            key = m.group(1) if "MC-SM" in line else "RX_RESTART src=" + m.group(1)
            if key == "TX_DONE -> RX_LISTEN":
                txdone.append(t)
            i = bisect_right(starts, t) - 1
            (cin if i >= 0 and sil[i][0] < t < sil[i][1] else cout)[key] += 1
    rates = {k: {"in_per_h": round(cin[k] / tin * 3600, 1), "out_per_h": round(cout[k] / tout * 3600, 1)}
             for k in sorted(set(cin) | set(cout))}

    def since_tx(t: float) -> float | None:
        j = bisect_left(txdone, t) - 1
        return round(t - txdone[j], 1) if j >= 0 else None
    end_lag = [since_tx(b) for _, b in sil]
    start_lag = [since_tx(a + 0.001) for a, _ in sil]
    # baseline: fraction of all ordinary RX events within 10 s after a TX_DONE is not available here;
    # report raw lags and share <= 10 s
    out = {"hours_in": round(tin / 3600, 2), "hours_out": round(tout / 3600, 2), "rates": rates,
           "silence_end_s_after_last_txdone": end_lag,
           "share_end_within_10s_of_txdone": round(sum(1 for x in end_lag if x is not None and x <= 10) / len(sil), 3),
           "silence_start_s_after_last_txdone": start_lag,
           "share_start_within_10s_of_txdone": round(sum(1 for x in start_lag if x is not None and x <= 10) / len(sil), 3),
           "txdone_per_h_in": rates.get("TX_DONE -> RX_LISTEN", {}).get("in_per_h")}
    print(json.dumps({k: v for k, v in out.items()}, indent=1))
    json.dump(out, open(sys.argv[3], "w"), indent=1)


if __name__ == "__main__":
    main()
