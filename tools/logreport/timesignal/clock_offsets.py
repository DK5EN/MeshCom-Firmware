#!/usr/bin/env python3
"""Clock offsets per node: logger wall clock (CET, UTC+1) vs node [LOG] HH:MM:SS,
and logger clock vs [NTP];ok epoch / TimeClient UTC lines.

Usage (via run.py): clock_offsets.py; writes <out>/data/clock_offsets.json
"""
import json
import re
from collections import Counter
from datetime import datetime, timezone
from statistics import median

from nodeset import CLUSTER as NODES, LOGGER_UTC_OFF_S, NORM, OUT

RE_LOG = re.compile(rb"^(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  (\d\d):(\d\d):(\d\d) (?:\[LOG\]|RX-UDP|TX-LoRa|MH-LoRa)")
RE_NTP = re.compile(rb"^(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  \[NTP\];ok;epoch;(\d+);rtt;(\d+)")
RE_TC = re.compile(rb"^(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  TimeClient now \(UTC\): (\d\d):(\d\d):(\d\d)")


def norm(d: float) -> float:
    d = (d + 43200.0) % 86400.0 - 43200.0
    return d


def pct(v, p):
    s = sorted(v)
    if not s:
        return None
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def main() -> None:
    norm_dir, out = NORM, OUT / "data" / "clock_offsets.json"
    res = {}
    for n in NODES:
        offs = []          # (logger_epoch_utc, offset_s)
        ntp = []           # (logger_utc_float, ntp_epoch - logger_utc)
        tc = []
        with open(f"{norm_dir}/{n}.log", "rb") as f:
            for line in f:
                m = RE_LOG.match(line)
                if m:
                    d, H, M, S, ms, h2, m2, s2 = m.groups()
                    lsec = int(H) * 3600 + int(M) * 60 + int(S) + int(ms) / 1000.0
                    nsec = int(h2) * 3600 + int(m2) * 60 + int(s2)
                    ep = datetime.strptime(d.decode() + f" {H.decode()}:{M.decode()}:{S.decode()}", "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp() - LOGGER_UTC_OFF_S
                    offs.append((ep + int(ms) / 1000.0, norm(lsec - nsec)))
                    continue
                m = RE_NTP.match(line)
                if m:
                    d, H, M, S, ms, ep, rtt = m.groups()
                    lep = datetime.strptime(d.decode() + f" {H.decode()}:{M.decode()}:{S.decode()}", "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp() - LOGGER_UTC_OFF_S + int(ms) / 1000.0
                    ntp.append((lep, int(ep) - lep, int(rtt)))
                    continue
                m = RE_TC.match(line)
                if m:
                    d, H, M, S, ms, h2, m2, s2 = m.groups()
                    lsec = (int(H) * 3600 + int(M) * 60 + int(S) + int(ms) / 1000.0 - LOGGER_UTC_OFF_S) % 86400
                    tsec = int(h2) * 3600 + int(m2) * 60 + int(s2)
                    tc.append(norm(lsec - tsec))
        # node clock offset vs logger, in whole hours bucket
        hb = Counter(round(o / 3600.0) for _, o in offs)
        dom = hb.most_common(1)[0][0] if hb else 0
        resid = [o - dom * 3600 for _, o in offs if round(o / 3600.0) == dom]
        # hourly median residual
        hourly = {}
        for t, o in offs:
            if round(o / 3600.0) != dom:
                continue
            k = datetime.fromtimestamp(t, timezone.utc).strftime("%m-%d %H")
            hourly.setdefault(k, []).append(o - dom * 3600)
        hmed = {k: round(median(v), 2) for k, v in hourly.items()}
        res[n] = {
            "log_lines": len(offs),
            "hour_buckets": {str(k): v for k, v in sorted(hb.items())},
            "dominant_offset_h": dom,
            "resid_s": {"min": round(min(resid), 2), "p5": round(pct(resid, 5), 2), "median": round(median(resid), 2),
                         "p95": round(pct(resid, 95), 2), "max": round(max(resid), 2)},
            "ntp_ok": len(ntp),
            "ntp_vs_logger_s": {"min": round(min(x for _, x, _ in ntp), 2), "median": round(median(x for _, x, _ in ntp), 2),
                                 "max": round(max(x for _, x, _ in ntp), 2)} if ntp else None,
            "ntp_rtt_ms": {"median": median(r for *_, r in ntp)} if ntp else None,
            "timeclient_vs_logger_s": {"min": round(min(tc), 2), "median": round(median(tc), 2), "max": round(max(tc), 2), "n": len(tc)} if tc else None,
            "hourly_median_resid_s_sample": dict(list(hmed.items())[:4]),
            "hourly_resid_median_range_s": [min(hmed.values()), max(hmed.values())] if hmed else None,
        }
        # offsets transitions
        trans = []
        prev = None
        for t, o in offs:
            b = round(o / 3600.0)
            if prev is not None and b != prev:
                trans.append((datetime.fromtimestamp(t, timezone.utc).strftime("%m-%d %H:%M:%S"), prev, b))
            prev = b
        res[n]["offset_hour_transitions"] = trans[:10]
        res[n]["offset_hour_transition_count"] = len(trans)
    json.dump(res, open(out, "w"), indent=1)
    for n, r in res.items():
        print(n, r["hour_buckets"], "dom", r["dominant_offset_h"], r["resid_s"], "ntp", r["ntp_vs_logger_s"], "tc", r["timeclient_vs_logger_s"], "trans", r["offset_hour_transition_count"])


if __name__ == "__main__":
    main()
