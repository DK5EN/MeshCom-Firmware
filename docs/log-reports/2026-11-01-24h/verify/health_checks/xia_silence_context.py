#!/usr/bin/env python3
"""Context of every >300 s receive silence (no ONRXDONE_TIME and no RX_ERROR) on one node.

Per silence: max logger gap between ANY two lines (serial/logger drop test), CHANNEL_UTIL samples
(should arrive every ~10 s) and their rx= sum, RX_TIMEOUT_FIRE millis ts= drift vs logger clock,
own TX (RADIO_TX) and CAD scans inside, CAD result split, what happened in the 3 s before the
first reception that ended the silence, and what preceded the silence start.
Usage: python3 -I xia_silence_context.py <norm-dir> [node=oe3xia]
"""
from __future__ import annotations
import collections, datetime as dt, re, sys
from pathlib import Path

PFX = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")


def t(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def kind(body: str) -> str:
    b = re.sub(r"\d+", "N", body)
    return b[:40]


def main() -> None:
    norm = Path(sys.argv[1]); node = sys.argv[2] if len(sys.argv) > 2 else "oe3xia"
    lines: list[tuple[float, str]] = []
    with open(norm / f"{node}.log", encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = PFX.match(ln)
            if m:
                lines.append((t(m.group(1)), m.group(2).rstrip()))
    rx = [i for i, (_, b) in enumerate(lines) if b.startswith("[MC-DBG] ONRXDONE_TIME ms=") or b.startswith("[MC-DBG] RX_ERROR rssi=")]
    gaps = [(a, b) for a, b in zip(rx, rx[1:]) if lines[b][0] - lines[a][0] > 300]
    end_ctx = collections.Counter(); start_ctx = collections.Counter()
    tot_cad = collections.Counter(); tot_tx = 0; worst_line_gap = 0.0
    print(f"{node}: {len(gaps)} silences >300 s, total {sum(lines[b][0]-lines[a][0] for a,b in gaps):.0f} s")
    print("start            dur  maxLineGap utilN utilRxSum tx cad(busy/free) drift_ms  end_trigger")
    for a, b in gaps:
        seg = lines[a + 1:b]
        lg = max((y[0] - x[0] for x, y in zip(lines[a:b], lines[a + 1:b + 1])), default=0)
        worst_line_gap = max(worst_line_gap, lg)
        util = [int(m.group(1)) for _, s in seg if (m := re.match(r"\[MC-DBG\] CHANNEL_UTIL rx=(\d+)ms", s))]
        tx = sum(1 for _, s in seg if s.startswith("[MC-DBG] RADIO_TX"))
        cad = collections.Counter("busy" if "result=-702" in s else "free" for _, s in seg if s.startswith("[MC-DBG] CAD_SCAN"))
        tot_cad.update(cad); tot_tx += tx
        rt = [(ts, int(m.group(1))) for ts, s in seg if (m := re.match(r"\[MC-DBG\] RX_TIMEOUT_FIRE ts=(\d+)", s))]
        drift = ((rt[-1][1] - rt[0][1]) - (rt[-1][0] - rt[0][0]) * 1000) if len(rt) > 1 else 0
        tb = lines[b][0]
        pre = [s for ts, s in lines[max(0, b - 40):b] if tb - ts <= 3.0]
        trig = "CAD" if any(s.startswith("[MC-DBG] CAD_SCAN") for s in pre) else ("TX" if any("OnTXDone" in s for s in pre) else ("RX_RESTART" if any("RX_RESTART" in s for s in pre) else "none"))
        end_ctx[trig] += 1
        ta = lines[a][0]
        post = [s for ts, s in lines[a + 1:a + 60] if ts - ta <= 5.0]
        st = "TX" if any("OnTXDone" in s for s in post) else ("CAD" if any("CAD_SCAN" in s for s in post) else "none")
        start_ctx[st] += 1
        print(f"{dt.datetime.fromtimestamp(ta):%m-%d %H:%M:%S} {tb-ta:5.0f} {lg:8.1f}s {len(util):5d} {sum(util):8d} {tx:3d} {cad['busy']:3d}/{cad['free']:<3d} {drift:8.0f}  {trig}")
    print(f"worst logger gap between any two lines inside silences: {worst_line_gap:.1f} s")
    print(f"own RADIO_TX inside silences: {tot_tx}; CAD scans busy/free: {tot_cad['busy']}/{tot_cad['free']}")
    print(f"what precedes the first reception (<=3 s): {dict(end_ctx)}")
    print(f"what follows the last reception (<=5 s): {dict(start_ctx)}")


if __name__ == "__main__":
    main()
