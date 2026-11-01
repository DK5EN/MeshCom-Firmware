"""Explain the slowest aired ring entries: what happened between RING_WRITE and RING_TX_READ.

usage: python3 -I slow_waits.py <norm_log> <writes_jsonl> <out_json> [min_lat_s] [max_n]
writes_jsonl comes from ring_fate.py (<out>_writes.jsonl).
"""
from __future__ import annotations

import json
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import fmt, iter_lines, kv  # noqa: E402


def main(path: str, wl: str, out: str, min_lat: float, max_n: int) -> None:
    ws = [json.loads(x) for x in open(wl)]
    sel = sorted([w for w in ws if w["fate"] == "aired" and (w.get("lat_ms") or 0) / 1000 >= min_lat],
                 key=lambda w: -w["lat_ms"])[:max_n]
    if not sel:
        print("none")
        return
    tmin = min(w["t"] for w in sel) - 1
    tmax = max(w["end"] for w in sel) + 1
    ev = []
    for n, t, b in iter_lines(path):
        if t < tmin:
            continue
        if t > tmax:
            break
        if b.startswith("[MC-DBG] "):
            c = b[9:]
            if c.startswith(("TX_GATE_ENTER", "CAD_BUSY_1", "CAD_BUSY", "CAD_FALSE", "CAD_FREE", "RING_TX_READ", "ONRXDONE_TIME", "CRC_ERROR", "CSMA_AGED", "RING_STATUS")):
                ev.append((t, c.split()[0], c))
    rows = []
    for w in sel:
        inw = [e for e in ev if w["t"] <= e[0] <= w["end"]]
        cnt = {}
        for e in inw:
            cnt[e[1]] = cnt.get(e[1], 0) + 1
        others = [kv(e[2]) for e in inw if e[1] == "RING_TX_READ"]
        prios = {}
        for o in others:
            prios[o["prio"]] = prios.get(o["prio"], 0) + 1
        qs = [int(kv(e[2])["queued"]) for e in inw if e[1] == "RING_STATUS"]
        rows.append({"id": w["mid"], "src": w["src"], "prio": w["prio"], "type": w["type"], "written": fmt(w["t"]),
                     "lat_s": round(w["lat_ms"] / 1000, 1), "events": cnt, "other_tx_by_prio_during_wait": prios,
                     "queued_p50_max_during": [sorted(qs)[len(qs) // 2] if qs else None, max(qs) if qs else None],
                     "rx_ok_during": cnt.get("ONRXDONE_TIME", 0), "gate_attempts": cnt.get("TX_GATE_ENTER", 0),
                     "cad_busy_confirmed": cnt.get("CAD_BUSY", 0), "csma_aged": cnt.get("CSMA_AGED", 0)})
    json.dump(rows, open(out, "w"), indent=1)
    for r in rows:
        print(r)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 100.0, int(sys.argv[5]) if len(sys.argv) > 5 else 12)
