"""TX service rate vs RX load per minute (channel-access limit), plus gap statistics.

usage: python3 -I service_rate.py <norm_log> <out_json>
RX events = OnRxDone lines + OnRxError (CRC_ERROR) lines (each re-arms the CSMA hold); TX = RING_TX_READ;
queue = last RING_STATUS queued value in the minute.
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import dist, iter_lines  # noqa: E402


def main(path: str, out: str) -> None:
    rx = defaultdict(int)
    err = defaultdict(int)
    tx = defaultdict(int)
    wr = defaultdict(int)
    q = {}
    stamps = []
    for n, t, b in iter_lines(path):
        m = int(t // 60)
        if b == "OnRxDone":
            rx[m] += 1
            stamps.append(t)
        elif b.startswith("[MC-DBG] CRC_ERROR "):
            err[m] += 1
            stamps.append(t)
        elif b.startswith("[MC-DBG] RING_TX_READ "):
            tx[m] += 1
        elif b.startswith("[MC-DBG] RING_WRITE "):
            wr[m] += 1
        elif b.startswith("[MC-DBG] RING_STATUS "):
            q[m] = int(b.split("queued=")[1].split()[0])
    mins = sorted(set(rx) | set(tx) | set(q))
    buckets = defaultdict(list)
    for m in mins:
        r = rx[m] + err[m]
        k = "0-5" if r <= 5 else "6-10" if r <= 10 else "11-15" if r <= 15 else "16-20" if r <= 20 else "21+"
        buckets[k].append(m)
    res = {"minutes": len(mins)}
    res["by_rx_per_min"] = {}
    for k in ("0-5", "6-10", "11-15", "16-20", "21+"):
        ms = buckets.get(k, [])
        if not ms:
            continue
        res["by_rx_per_min"][k] = {"minutes": len(ms), "tx_per_min_mean": round(sum(tx[m] for m in ms) / len(ms), 2),
                                   "writes_per_min_mean": round(sum(wr[m] for m in ms) / len(ms), 2),
                                   "queue_mean": round(sum(q.get(m, 0) for m in ms) / len(ms), 2), "queue_max": max(q.get(m, 0) for m in ms)}
    stamps.sort()
    gaps = [b - a for a, b in zip(stamps, stamps[1:])]
    res["rx_or_err_gap_s"] = dist(gaps)
    for thr in (3.3, 4.8, 5.85, 8.0, 15.0):
        res["share_gaps_ge_%.2fs_pct" % thr] = round(100.0 * sum(1 for g in gaps if g >= thr) / len(gaps), 1)
    res["rx_events_per_min_mean"] = round(sum(rx[m] + err[m] for m in mins) / len(mins), 2)
    json.dump(res, open(out, "w"), indent=1)
    print(json.dumps(res, indent=1))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
