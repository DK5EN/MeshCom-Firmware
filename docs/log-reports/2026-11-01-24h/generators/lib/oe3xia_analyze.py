#!/usr/bin/env python3
"""OE3XIA-12 receive-silence analysis (helper of generators/oe3xia.py).

Usage (from the repo root):
    python3 -I docs/log-reports/2026-11-01-24h/generators/lib/oe3xia_analyze.py \
        --norm logs/20261101-24h-log/.logreport-work/norm \
        --out docs/log-reports/2026-11-01-24h/data/extra/oe3xia_analysis.json

Streams the five normalised OE3 logs (logger wall-clock prefix) once each. A reception event is one
"[MC-DBG] ONRXDONE_TIME ms=" line (every decoded frame, 12-byte ACK frames included; the plain
"OnRxDone" marker is not printed for ACK frames, lora_functions.cpp:1018-1034 vs :2744) or one
"[MC-DBG] RX_ERROR rssi=" line (signal detected, no valid frame). A silence is a gap of more than
300 s between two consecutive reception events of OE3XIA-12.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import datetime as dt
import json
import re
import statistics
from pathlib import Path
from typing import Any

NODE = "oe3xia"
OE3 = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
OTHERS = [n for n in OE3 if n != NODE]
TS = re.compile(r"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  (.*)$")
T0 = dt.datetime(2026, 10, 31, 6, 0, 0)
RSSI_RE = re.compile(r"(?:rssi=|RSSI:)(-?\d+)")
RW_RE = re.compile(r"RING_WRITE slot=\d+ type=(\w+) status=\w+ len=\d+ msg_id=(\w+) queued=\d+/\d+ src=(\w+)")
DROP_RE = re.compile(r"RING_DROP_STALE slot=\d+ prio=(\d) age_s=\d+ msg_id=(\w+)")
TXUDP_RE = re.compile(r"TX-UDP\s+\d+ \S+ x(\w+)")
WAIT_RE = re.compile(r"RX_TIMEOUT_FIRE ts=\d+ wait=(\d+)")
FRAME_RE = re.compile(r"\[LOG\] \d{3} . x([0-9A-F]{8}) H\d\d .*? M\d\d (\S+?)>")
TXLOG_RE = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) ")
NEIGH = {"OE1BSE-1", "OE3XPA-12", "OE1RIG-4", "OE3ALB-2", "OE3GHB-1", "OE3XHQ-12"}
SILENCE_S = 300.0
CAD_LEAD_S = 3.0


def is_rx(body: str) -> bool:
    return body.startswith("[MC-DBG] ONRXDONE_TIME ms=") or body.startswith("[MC-DBG] RX_ERROR rssi=")


def parse_ts(m: re.Match[str]) -> float:
    d = dt.datetime(*(int(x) for x in m.groups()[:6]))
    return (d - T0).total_seconds() + int(m.group(7)) / 1000.0


def hms(sec: float) -> str:
    return (T0 + dt.timedelta(seconds=sec)).strftime("%d.%m. %H:%M:%S")


def load_lines(path: Path) -> list[tuple[float, str]]:
    out: list[tuple[float, str]] = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = TS.match(line.rstrip("\n"))
            if m:
                out.append((parse_ts(m), m.group(8)))
    return out


def med(xs: list[float]) -> float | None:
    return statistics.median(xs) if xs else None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    norm: Path = args.norm

    all_lines = {n: load_lines(norm / f"{n}.log") for n in OE3}
    rx: dict[str, list[float]] = {}
    cnt: dict[str, dict[str, int]] = {}
    frames: dict[str, list[tuple[float, str, str]]] = {}
    for n in OE3:
        r: list[float] = []
        c = {"done": 0, "err": 0, "ondone": 0}
        fr: list[tuple[float, str, str]] = []
        for t, s in all_lines[n]:
            if s.startswith("[MC-DBG] ONRXDONE_TIME ms="):
                c["done"] += 1
                r.append(t)
            elif s.startswith("[MC-DBG] RX_ERROR rssi="):
                c["err"] += 1
                r.append(t)
            elif s == "OnRxDone":
                c["ondone"] += 1
            elif "RSSI:" in s:
                f = FRAME_RE.search(s)
                if f:
                    fr.append((t, f.group(1), f.group(2).split(",")[-1]))
        rx[n], cnt[n], frames[n] = r, c, fr

    # 30-min bins, only bins fully inside the capture of this node
    nb = int(27 * 3600 / 1800)
    bins: dict[str, list[int | None]] = {}
    for n in OE3:
        b: list[int | None] = [0] * nb
        first, last = rx[n][0], rx[n][-1]
        for t in rx[n]:
            i = int(t // 1800)
            if 0 <= i < nb:
                b[i] = (b[i] or 0) + 1
        for i in range(nb):
            if i * 1800 < first or (i + 1) * 1800 > last:
                b[i] = None
        bins[n] = b

    lines = all_lines[NODE]
    times = [t for t, _ in lines]
    ia = rx[NODE]
    gaps = [(a, b) for a, b in zip(ia, ia[1:]) if b - a > SILENCE_S]
    radio_tx = [t for t, s in lines if s.startswith("[MC-DBG] RADIO_TX")]
    cad_t = [t for t, s in lines if s.startswith("[MC-DBG] CAD_SCAN")]
    err_t = {t for t, s in lines if s.startswith("[MC-DBG] RX_ERROR rssi=")}
    rx_set_ends = {b for _, b in gaps}

    def cad_before(x: float) -> bool:
        i = bisect.bisect_left(cad_t, x)
        return i > 0 and x - cad_t[i - 1] <= CAD_LEAD_S

    def rssi_near(t: float) -> int | None:
        """RSSI of the reception event at time t (RX_ERROR carries it itself, a decoded frame
        has its [LOG] line shortly before ONRXDONE_TIME)."""
        i = bisect.bisect_left(times, t - 0.0005)
        if i >= len(lines):
            return None
        if lines[i][1].startswith("[MC-DBG] RX_ERROR rssi="):
            m = RSSI_RE.search(lines[i][1])
            return int(m.group(1)) if m else None
        for _, s in reversed(lines[max(0, i - 14):i]):
            if "[LOG]" in s:
                m = RSSI_RE.search(s)
                if m:
                    return int(m.group(1))
        return None

    rows: list[dict[str, Any]] = []
    tot = {"scan": 0, "busy": 0, "restart": 0, "tx": 0, "util_n": 0, "util_pos": 0}
    worst_line_gap = 0.0
    ends_cad = 0
    ends_cad_lead: list[float] = []
    starts_after_err = 0
    heard_idx: dict[str, dict[str, list[tuple[float, str]]]] = {}
    for n in OTHERS:
        d: dict[str, list[tuple[float, str]]] = collections.defaultdict(list)
        for t, mid, last in frames[n]:
            d[mid].append((t, last))
        heard_idx[n] = d
    txlog = [(t, m.group(1)) for t, s in lines if (m := TXLOG_RE.search(s))]
    for a, b in gaps:
        i, j = bisect.bisect_right(times, a), bisect.bisect_left(times, b)
        seg = lines[i:j]
        scan = sum(1 for _, s in seg if "CAD_SCAN result=" in s)
        busy = sum(1 for _, s in seg if s.startswith("[MC-DBG] CAD_SCAN") and "result=-702" in s)
        rst = sum(1 for _, s in seg if "RX_RESTART" in s)
        un = [s for t, s in seg if "CHANNEL_UTIL rx=" in s and t >= a + 11]
        up = sum(1 for s in un if not re.search(r"CHANNEL_UTIL rx=0ms", s))
        ntx = bisect.bisect_left(radio_tx, b) - bisect.bisect_right(radio_tx, a)
        for x, y in zip(lines[max(0, i - 1):j + 1], lines[max(0, i - 1) + 1:j + 1]):
            worst_line_gap = max(worst_line_gap, y[0] - x[0])
        k = bisect.bisect_left(cad_t, b)
        lead = b - cad_t[k - 1] if k > 0 and b - cad_t[k - 1] <= CAD_LEAD_S else None
        if lead is not None:
            ends_cad += 1
            ends_cad_lead.append(lead)
        if a in err_t:
            starts_after_err += 1
        others = {o: bisect.bisect_right(rx[o], b) - bisect.bisect_left(rx[o], a) for o in OTHERS}
        rows.append({"start": hms(a), "len_s": b - a, "own_tx": ntx, "others": others,
                     "rssi_start": rssi_near(a), "rssi_end": rssi_near(b), "end_after_cad": lead is not None})
        tot["scan"] += scan
        tot["busy"] += busy
        tot["restart"] += rst
        tot["tx"] += ntx
        tot["util_n"] += len(un)
        tot["util_pos"] += up

    # own transmissions inside silences heard by another OE3 logger (same msg id, last hop OE3XIA-12, within 30 s)
    tx_in = [(t, mid) for t, mid in txlog if any(a < t < b for a, b in gaps)]
    heard = sum(1 for t, mid in tx_in
                if any(abs(t2 - t) <= 30 and last == "OE3XIA-12" for n in OTHERS for t2, last in heard_idx[n].get(mid, [])))
    neigh_per_silence = [sum(1 for n in OTHERS for t, _, last in frames[n] if a < t < b and last in NEIGH) for a, b in gaps]

    sil = sum(b - a for a, b in gaps)
    span = ia[-1] - ia[0]
    other_rate: dict[str, dict[str, float]] = {}
    for o in OTHERS:
        secs = 0.0
        ev = 0
        for a, b in gaps:
            lo, hi = max(a, rx[o][0]), min(b, rx[o][-1])
            if hi > lo:
                secs += hi - lo
                ev += bisect.bisect_right(rx[o], hi) - bisect.bisect_left(rx[o], lo)
        other_rate[o] = {"during_per_h": ev / (secs / 3600.0) if secs else 0.0,
                         "mean_per_h": len(rx[o]) / ((rx[o][-1] - rx[o][0]) / 3600.0), "secs": secs}
    per_hour = [0.0] * 27
    for a, b in gaps:
        t = a
        while t < b:
            h = int(t // 3600)
            e = min(b, (h + 1) * 3600)
            per_hour[h] += e - t
            t = e
    inside: list[int] = []
    outside: list[int] = []
    gi = 0
    for t, s in lines:
        if "RX_TIMEOUT_FIRE" in s:
            m = WAIT_RE.search(s)
            if not m:
                continue
            while gi < len(gaps) and gaps[gi][1] < t:
                gi += 1
            ins = gi < len(gaps) and gaps[gi][0] < t < gaps[gi][1]
            (inside if ins else outside).append(int(m.group(1)))
    src_in: dict[str, int] = {}
    for a, b in gaps:
        i, j = bisect.bisect_right(times, a), bisect.bisect_left(times, b)
        for tt, s in lines[i:j]:
            m = RW_RE.search(s)
            if m and tt < b - 1.0:
                src_in[m.group(3)] = src_in.get(m.group(3), 0) + 1
    wx_ids: set[str] = set()
    hey_ids: set[str] = set()
    dropped: dict[str, int] = {}
    udp_ids: set[str] = set()
    for _, s in lines:
        m = RW_RE.search(s)
        if m:
            if m.group(3) == "user_wx":
                wx_ids.add(m.group(2))
            elif m.group(3) == "auto_pos":
                hey_ids.add(m.group(2))
            continue
        m = DROP_RE.search(s)
        if m:
            dropped[m.group(2)] = int(m.group(1))
            continue
        m = TXUDP_RE.search(s)
        if m:
            udp_ids.add(m.group(1))
    wx_dropped = [i for i in dropped if i in wx_ids]
    hey_dropped = [i for i in dropped if i in hey_ids]
    bnd = [r["rssi_start"] for r in rows] + [r["rssi_end"] for r in rows]
    bnd_known = [x for x in bnd if x is not None]
    gaps_all = {o: [b - a for a, b in zip(rx[o], rx[o][1:])] for o in OE3}
    outside_rx = [x for x in ia if x not in rx_set_ends]
    base_cad = sum(1 for x in outside_rx if cad_before(x))
    n_err_total = cnt[NODE]["err"]
    res: dict[str, Any] = {
        "median_gap": med(gaps_all[NODE]), "max_gap": {o: max(g) for o, g in gaps_all.items()},
        "cnt": cnt,
        "err_share": {o: 100.0 * cnt[o]["err"] / (cnt[o]["err"] + cnt[o]["done"]) for o in OE3},
        "err_share_nonack": 100.0 * cnt[NODE]["err"] / (cnt[NODE]["err"] + cnt[NODE]["ondone"]),
        "bins": bins, "rows": rows, "n_gaps": len(gaps), "silent_s": sil, "span_s": span, "events": len(ia),
        "rate_all": len(ia) / (span / 3600.0), "rate_outside": len(ia) / ((span - sil) / 3600.0),
        "rate_other": {o: len(rx[o]) / ((rx[o][-1] - rx[o][0]) / 3600.0) for o in OE3},
        "events_other": {o: len(rx[o]) for o in OE3},
        "cad_scan_all": len(cad_t),
        "cad_busy_all": sum(1 for _, s in lines if s.startswith("[MC-DBG] CAD_SCAN") and "result=-702" in s),
        "cad_scan_sil": tot["scan"], "cad_busy_sil": tot["busy"], "restart_sil": tot["restart"],
        "util_n": tot["util_n"], "util_pos": tot["util_pos"], "radio_tx_in_sil": tot["tx"],
        "radio_tx_total": len(radio_tx), "radio_tx_per_h": len(radio_tx) / (span / 3600.0),
        "tx_in_sil_heard": heard, "tx_in_sil_logged": len(tx_in),
        "neigh_frames_per_silence": {"min": min(neigh_per_silence), "median": med(neigh_per_silence),
                                     "max": max(neigh_per_silence)},
        "worst_line_gap_s": worst_line_gap,
        "ends_after_cad": ends_cad, "ends_after_cad_lead_median_s": med(ends_cad_lead),
        "cad_base_rate_pct": 100.0 * base_cad / len(outside_rx), "cad_base_n": len(outside_rx),
        "starts_after_rx_error": starts_after_err,
        "rx_error_base_pct": 100.0 * n_err_total / len(ia),
        "other_rate": other_rate, "per_hour": per_hour,
        "wait_in": {"n": len(inside), "med": med([float(x) for x in inside])},
        "wait_out": {"n": len(outside), "med": med([float(x) for x in outside])},
        "src_in": src_in, "wx_written": len(wx_ids), "wx_dropped": len(wx_dropped),
        "wx_dropped_udp": sum(1 for i in wx_dropped if i in udp_ids),
        "hey_written": len(hey_ids), "hey_dropped": len(hey_dropped),
        "bnd_n": len(bnd_known), "bnd_strong": sum(1 for x in bnd_known if x >= -80),
        "bnd_median": med([float(x) for x in bnd_known]),
        "first_gap": hms(gaps[0][0]), "last_gap_end": hms(gaps[-1][1]),
        "first_rx": hms(ia[0]), "last_rx": hms(ia[-1]),
        "log_lines": len(lines), "log_first": hms(lines[0][0]), "log_last": hms(lines[-1][0]),
        "log_span_h": (lines[-1][0] - lines[0][0]) / 3600.0,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(res, indent=1, default=str), encoding="utf-8")
    print(f"wrote {args.out}: {len(gaps)} silences, {sil:.0f} s, longest {max(b - a for a, b in gaps):.0f} s, "
          f"ends after CAD {ends_cad}/{len(gaps)}, base {res['cad_base_rate_pct']:.1f} %")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
