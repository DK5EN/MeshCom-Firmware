"""Helper for the overview page: receive silences per OE3 node, counting every reception.

A reception is an ONRXDONE_TIME line (every decoded frame, ACKs included) or an RX_ERROR line. The
"OnRxDone" print is skipped on the 12-byte ACK path (src/lora_functions.cpp:1018-1034), so counting
that line alone would miss ACK receptions and shift silence starts.

Usage: python3 -I uebersicht_rx_silence.py --norm <dir with oe3xhl.log ...> --out <json>
Writes data/extra/uebersicht_rx_silence.json. Stdlib only.
"""

from __future__ import annotations

import argparse
import bisect
import datetime as dt
import json
import re
from pathlib import Path
from typing import Any

PFX = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
UTIL = re.compile(r"\[MC-DBG\] CHANNEL_UTIL rx=(\d+)ms")
NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
THR_S = 300.0


def ts(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def fmt(x: float) -> str:
    return dt.datetime.fromtimestamp(x).strftime("%Y-%m-%d %H:%M:%S")


def analyse(path: Path) -> dict[str, Any]:
    rx: list[float] = []
    cad: list[float] = []
    util: list[tuple[float, int]] = []
    first = last = 0.0
    with path.open(encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = PFX.match(ln)
            if not m:
                continue
            t = ts(m.group(1))
            if not first:
                first = t
            last = t
            b = m.group(2)
            if b.startswith("[MC-DBG] ONRXDONE_TIME ms=") or b.startswith("[MC-DBG] RX_ERROR rssi="):
                rx.append(t)
            elif b.startswith("[MC-DBG] CAD_SCAN"):
                cad.append(t)
            else:
                u = UTIL.match(b)
                if u:
                    util.append((t, int(u.group(1))))
    rx.sort()
    gaps = [(a, b) for a, b in zip(rx, rx[1:]) if b - a > THR_S]
    long_all = max(((b - a, a) for a, b in zip(rx, rx[1:])), default=(0.0, 0.0))
    out: dict[str, Any] = {
        "span_s": round(last - first, 1),
        "receptions": len(rx),
        "silences_gt_300s": len(gaps),
        "silent_s": round(sum(b - a for a, b in gaps), 1),
        "silent_pct_of_span": round(100 * sum(b - a for a, b in gaps) / (last - first), 1),
        "longest_gap_s": round(long_all[0], 1),
        "longest_gap_start": fmt(long_all[1]) if long_all[1] else "",
    }
    if gaps:
        out["first_silence_start"] = fmt(gaps[0][0])
        inside_util = [v for t, v in util if any(a + 11 <= t <= b for a, b in gaps)]
        out["util_samples_inside"] = len(inside_util)
        out["util_samples_inside_rx0"] = sum(1 for v in inside_util if v == 0)
        in_cad = [t for t in cad if any(a < t < b for a, b in gaps)]
        out["cad_scans_inside"] = len(in_cad)

        def cad_before(x: float) -> bool:
            i = bisect.bisect_left(cad, x)
            return i > 0 and x - cad[i - 1] <= 3.0

        ends = [b for _, b in gaps]
        endset = set(ends)
        other = [x for x in rx if x not in endset]
        out["ends_with_cad_before"] = sum(1 for e in ends if cad_before(e))
        out["other_rx_with_cad_before_pct"] = round(100 * sum(1 for x in other if cad_before(x)) / len(other), 1)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    res = {n: analyse(a.norm / f"{n}.log") for n in NODES}
    res["_meta"] = {
        "threshold_s": THR_S,
        "reception": "ONRXDONE_TIME or RX_ERROR line (ACK receptions included)",
        "source": "norm logs, generators/lib/uebersicht_rx_silence.py",
    }
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(res, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
