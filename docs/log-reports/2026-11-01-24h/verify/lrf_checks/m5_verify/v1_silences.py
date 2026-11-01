#!/usr/bin/env python3
"""M5 verify (1): independent recount of OE3XIA-12 RX silences.

usage: python3 -I v1_silences.py <oe3xia.log> <out.json>
Silence = gap > 300 s between consecutive RX events (ONRXDONE_TIME or RX_ERROR),
logger-local timestamps. Also reports alternative definitions and per-silence
liveness (other log lines, TX, CAD, max any-line gap) to rule out logger dropouts.
"""
import json
import re
import sys
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")
RECV = re.compile(r"\[LOG\] (\d{3}|ERR) ")


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def gaps(times: list[float], thr: float = 300.0) -> list[tuple[float, float]]:
    return [(a, b) for a, b in zip(times, times[1:]) if b - a > thr]


def summ(g: list[tuple[float, float]]) -> dict:
    d = [b - a for a, b in g]
    return {"n": len(g), "total_s": round(sum(d), 1), "longest_s": round(max(d), 1) if d else 0}


def main() -> None:
    src, out = sys.argv[1], sys.argv[2]
    all_t: list[float] = []
    rx: list[float] = []
    rx_done_only: list[float] = []
    recv_log: list[float] = []
    tx: list[tuple[float, str]] = []
    cad: list[float] = []
    with open(src, errors="replace") as f:
        for line in f:
            m = TS.match(line)
            if not m:
                continue
            t = ts(m.group(1))
            all_t.append(t)
            if "ONRXDONE_TIME" in line:
                rx.append(t)
                rx_done_only.append(t)
            elif "] RX_ERROR " in line:
                rx.append(t)
            elif "[LOG] TX x" in line:
                tx.append((t, line))
            elif "CAD_SCAN result=" in line:
                cad.append(t)
            if RECV.search(line):
                recv_log.append(t)
    sil = gaps(rx)
    det = []
    for a, b in sil:
        inside = [t for t in all_t if a < t < b]
        pts = [a] + inside + [b]
        maxgap = max(y - x for x, y in zip(pts, pts[1:]))
        det.append({
            "start": datetime.fromtimestamp(a).strftime("%m-%d %H:%M:%S"),
            "end": datetime.fromtimestamp(b).strftime("%m-%d %H:%M:%S"),
            "dur_s": round(b - a, 1),
            "lines_inside": len(inside),
            "max_anyline_gap_s": round(maxgap, 1),
            "tx_inside": sum(1 for t, _ in tx if a < t < b),
            "cad_inside": sum(1 for t in cad if a < t < b),
            "a": a, "b": b,
        })
    res = {
        "span": [datetime.fromtimestamp(all_t[0]).isoformat(), datetime.fromtimestamp(all_t[-1]).isoformat()],
        "rx_events": len(rx),
        "def_onrxdone_or_rxerror": summ(sil),
        "def_onrxdone_only": summ(gaps(rx_done_only)),
        "def_log_reception_lines": summ(gaps(recv_log)),
        "max_anyline_gap_in_any_silence_s": max(d["max_anyline_gap_s"] for d in det),
        "silences": det,
    }
    s = res["def_onrxdone_or_rxerror"]
    print(f"silences: {s['n']} total {s['total_s']} s longest {s['longest_s']} s first {det[0]['start']}")
    print("onrxdone only:", res["def_onrxdone_only"], " [LOG] recv lines:", res["def_log_reception_lines"])
    print("max any-line gap inside a silence:", res["max_anyline_gap_in_any_silence_s"], "s")
    print("silences with 0 TX:", sum(1 for d in det if d["tx_inside"] == 0),
          " with 0 CAD:", sum(1 for d in det if d["cad_inside"] == 0))
    with open(out, "w") as f:
        json.dump(res, f, indent=1)


if __name__ == "__main__":
    main()
