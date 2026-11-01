"""Attribute CRC_ERROR frames with a fixed large frequency error to a transmitter via CRC_PAYLOAD.

usage: python3 -I crc_source_check.py <norm_log> <out_json> [ferr_min_abs]

The CRC_PAYLOAD dump (hex bytes) still contains the readable path in most frames: bytes 6.. up to '>'.
We list the longest readable callsign run and count it per origin token and per ferr bucket.
"""
from __future__ import annotations

import json
import re
import sys
from collections import Counter

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import iter_lines, kv  # noqa: E402

CALL = re.compile(r"[A-Z0-9]{1,2}[0-9][A-Z0-9]{1,4}(?:-\d{1,2})?")


def main(path: str, out: str, fmin: float) -> None:
    pend = None
    rows = []
    for n, t, b in iter_lines(path):
        if b.startswith("[MC-DBG] CRC_ERROR "):
            d = kv(b)
            pend = (t, float(d["rssi"]), float(d["snr"]), float(d["freq_err"]), int(d["size"]))
        elif pend is not None and b.startswith("[MC-DBG] CRC_PAYLOAD["):
            hx = b.split(":", 1)[1].split()
            try:
                raw = bytes(int(h, 16) for h in hx)
            except ValueError:
                raw = b""
            txt = raw[5:].decode("latin-1")
            gt = txt.find(">")
            head = txt[:gt] if gt > 0 else txt[:60]
            toks = [x for x in head.split(",") if CALL.fullmatch(x)]
            rows.append((pend, toks, gt > 0))
            pend = None
    hi = [r for r in rows if abs(r[0][3]) > fmin]
    lo = [r for r in rows if abs(r[0][3]) <= fmin]
    res = {"rows": len(rows), "hi": len(hi), "lo": len(lo)}
    res["hi_origin_top"] = Counter(r[1][0] for r in hi if r[1]).most_common(10)
    res["hi_no_origin_n"] = sum(1 for r in hi if not r[1])
    res["lo_origin_top"] = Counter(r[1][0] for r in lo if r[1]).most_common(10)
    res["hi_last_readable_token_top"] = Counter(r[1][-1] for r in hi if r[1]).most_common(8)
    res["hi_with_gt_n"] = sum(1 for r in hi if r[2])
    # origin share among all frames (hi) with a clean origin
    res["hi_rssi_by_origin"] = {}
    for c, k in res["hi_origin_top"][:5]:
        v = [r[0][1] for r in hi if r[1] and r[1][0] == c]
        res["hi_rssi_by_origin"][c] = {"n": len(v), "rssi_p50": sorted(v)[len(v) // 2], "ferr_p50": sorted(r[0][3] for r in hi if r[1] and r[1][0] == c)[len(v) // 2]}
    json.dump(res, open(out, "w"), indent=1)
    print(json.dumps(res, indent=1))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 3000.0)
