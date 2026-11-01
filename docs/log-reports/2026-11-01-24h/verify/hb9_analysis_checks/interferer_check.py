"""Look for a recurring off-frequency / strong local source in CRC_ERROR and RX_IRQ_STALE lines.

usage: python3 -I interferer_check.py <norm_log> <out_json>
"""
from __future__ import annotations

import json
import sys
from collections import Counter

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import dist, fmt, hour_key, iter_lines, kv  # noqa: E402


def main(path: str, out: str) -> None:
    crc = []
    stale = []
    for n, t, b in iter_lines(path):
        if b.startswith("[MC-DBG] CRC_ERROR "):
            d = kv(b)
            crc.append((t, float(d["rssi"]), float(d["snr"]), float(d["freq_err"]), int(d["size"])))
        elif b.startswith("[MC-DBG] RX_IRQ_STALE rssi="):
            d = kv(b)
            stale.append((t, float(d["rssi"]), d["flags"]))
    hi = [c for c in crc if abs(c[3]) > 3000]
    lo = [c for c in crc if abs(c[3]) <= 3000]
    res = {"crc_total": len(crc), "crc_ferr_gt3000": len(hi)}
    res["hi_ferr_value_hist_500Hz"] = dict(sorted(Counter(int(round(c[3] / 500.0) * 500) for c in hi).items()))
    res["hi_rssi"] = dist([c[1] for c in hi])
    res["hi_snr"] = dist([c[2] for c in hi])
    res["hi_size"] = dist([c[4] for c in hi])
    res["hi_per_hour"] = dict(sorted(Counter(hour_key(c[0]) for c in hi).items()))
    res["lo_rssi"] = dist([c[1] for c in lo])
    gaps = [b[0] - a[0] for a, b in zip(hi, hi[1:])]
    res["hi_interarrival_s"] = dist(gaps)
    # periodicity: fraction of gaps within +-3 s of a multiple of 60 s / 30 s / 15 s
    for per in (15, 30, 60, 120, 300):
        near = sum(1 for g in gaps if g >= per * 0.5 and min(g % per, per - g % per) <= 3.0)
        res["hi_gap_near_multiple_%ds" % per] = round(100.0 * near / max(1, len(gaps)), 1)
    st_hi = [s for s in stale if s[1] > -80]
    res["stale_total"] = len(stale)
    res["stale_gt_m80"] = len(st_hi)
    res["stale_gt_m80_per_hour"] = dict(sorted(Counter(hour_key(s[0]) for s in st_hi).items()))
    res["stale_gt_m80_rssi_hist"] = dict(sorted(Counter(int(s[1]) for s in st_hi).items()))
    res["stale_gt_m80_flags"] = dict(Counter(s[2] for s in st_hi))
    sg = [b[0] - a[0] for a, b in zip(st_hi, st_hi[1:])]
    res["stale_gt_m80_interarrival_s"] = dist(sg)
    for per in (15, 30, 60, 120, 300):
        near = sum(1 for g in sg if g >= per * 0.5 and min(g % per, per - g % per) <= 3.0)
        res["stale_gap_near_multiple_%ds" % per] = round(100.0 * near / max(1, len(sg)), 1)
    # coincidence of hi-ferr CRC errors with strong stale events (within 10 s)
    st_t = [s[0] for s in st_hi]
    co = 0
    for c in hi:
        if any(abs(c[0] - x) < 10 for x in st_t):
            co += 1
    res["hi_ferr_crc_with_strong_stale_within_10s"] = co
    res["hi_first_last"] = [fmt(hi[0][0]), fmt(hi[-1][0])] if hi else None
    json.dump(res, open(out, "w"), indent=1)
    print(json.dumps(res, indent=1)[:5000])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
