#!/usr/bin/env python3
"""Offline evaluation of position Kalman filter bench captures (GPS-11).

Reads meshlogger captures (``YYYY-MM-DD HH:MM:SS.mmm  <console text>``) that
contain, at ``--gpsdebug 2``, one line per GPS evaluation::

    [GPS ]...pos raw:%.6lf,%.6lf kf:%.6lf,%.6lf hdop:%.1f sig:%.1fm rej:%u

and reports

* G1: APRS DDMM.MM cell stability of the raw and kf series,
* G3: (``--track``) along-track lag of kf versus raw at steady speed,
* G4: distance-triggered smart-beacon shots (``[POSINFO]...one-shot set -
  distance > ...``) in stationary periods.

Run: ``python3 tools/gps/pos_kf_eval.py [--track] [--json] capture.log ...``
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
from collections import Counter
from dataclasses import dataclass
from datetime import datetime

TS_RE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(?:\.\d+)?)\s+(.*)$")
POS_RE = re.compile(
    r"\[GPS \](?:\.\.\.)?pos raw:(-?[\d.]+),(-?[\d.]+) kf:(-?[\d.]+),(-?[\d.]+)"
    r"(?: held:(-?[\d.]+),(-?[\d.]+))?"
    r" hdop:(-?[\d.]+) sig:(-?[\d.]+)m? rej:(\d+)"
    r"(?: spd:(-?[\d.]+) crs:(-?[\d.]+) st:(still|moving|off) nm:(\d+))?"
)
SHOT_RE = re.compile(r"\[POSINFO\]\.\.\.one-shot set - distance > ")

EARTH_M_PER_DEG = 111_194.93  # mean radius 6371 km


@dataclass
class Sample:
    t: float
    raw: tuple[float, float]
    kf: tuple[float, float]
    hdop: float
    sig: float
    rej: int
    # W2 debug line extras; None when the capture predates them
    held: tuple[float, float] | None = None
    spd: float | None = None
    crs: float | None = None
    st: str | None = None
    nm: int | None = None


def parse_ts(text: str) -> float:
    fmt = "%Y-%m-%d %H:%M:%S.%f" if "." in text else "%Y-%m-%d %H:%M:%S"
    return datetime.strptime(text, fmt).timestamp()


def parse_lines(lines: list[str]) -> tuple[list[Sample], list[float]]:
    """Return (pos samples, timestamps of distance one-shot lines)."""
    samples: list[Sample] = []
    shots: list[float] = []
    for line in lines:
        m = TS_RE.match(line.rstrip("\r\n"))
        if not m:
            continue
        t = parse_ts(m.group(1))
        body = m.group(2)
        p = POS_RE.search(body)
        if p:
            v = p.groups()
            samples.append(
                Sample(
                    t,
                    (float(v[0]), float(v[1])),
                    (float(v[2]), float(v[3])),
                    float(v[6]),
                    float(v[7]),
                    int(v[8]),
                    (float(v[4]), float(v[5])) if v[4] is not None else None,
                    float(v[9]) if v[9] is not None else None,
                    float(v[10]) if v[10] is not None else None,
                    v[11],
                    int(v[12]) if v[12] is not None else None,
                )
            )
        elif SHOT_RE.search(body):
            shots.append(t)
    return samples, shots


def cround4abs(x: float) -> float:
    """Firmware cround4abs(): snprintf %.4lf, sscanf, abs."""
    return abs(float("%.4f" % x))


def _ddmm(value_abs: float) -> float:
    """PositionToAPRS bConvPos branch, then snprintf format "%07.2lf"/"%08.2lf"."""
    deg = int(value_abs)
    return deg * 100.0 + (value_abs - deg) * 60.0


def aprs_cell(lat: float, lon: float) -> tuple[str, str]:
    """On-air DDMM.MM strings with hemisphere, as PositionToAPRS() builds them."""
    la = _ddmm(cround4abs(lat))
    lo = _ddmm(cround4abs(lon))
    lat_c = "S" if lat < 0 else "N"
    lon_c = "W" if lon < 0 else "E"
    return "%07.2f%s" % (la, lat_c), "%08.2f%s" % (lo, lon_c)


def to_local_m(
    pts: list[tuple[float, float]], ref: tuple[float, float]
) -> list[tuple[float, float]]:
    c = math.cos(math.radians(ref[0]))
    return [
        ((lon - ref[1]) * EARTH_M_PER_DEG * c, (lat - ref[0]) * EARTH_M_PER_DEG)
        for lat, lon in pts
    ]


def percentile(vals: list[float], q: float) -> float:
    if not vals:
        return float("nan")
    s = sorted(vals)
    k = (len(s) - 1) * q
    lo = math.floor(k)
    hi = math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def series_stats(pts: list[tuple[float, float]]) -> dict[str, object]:
    """G1 statistics for one series of (lat, lon) degrees."""
    n = len(pts)
    if n == 0:
        return {"n": 0}
    cells = [aprs_cell(la, lo) for la, lo in pts]
    cnt = Counter(cells)
    modal, modal_n = cnt.most_common(1)[0]
    changes = sum(1 for a, b in zip(cells, cells[1:]) if a != b)
    med = (
        statistics.median(p[0] for p in pts),
        statistics.median(p[1] for p in pts),
    )
    xy = to_local_m(pts, med)
    dist = [math.hypot(x, y) for x, y in xy]
    sx = statistics.pstdev(x for x, _ in xy)
    sy = statistics.pstdev(y for _, y in xy)
    return {
        "n": n,
        "distinct_cells": len(cnt),
        "modal_cell": {"lat": modal[0], "lon": modal[1]},
        "modal_share": modal_n / n,
        "cell_changes": changes,
        "std_m": math.hypot(sx, sy),
        "std_east_m": sx,
        "std_north_m": sy,
        "p95_dist_to_median_m": percentile(dist, 0.95),
    }


def steady_segments(
    samples: list[Sample], win_s: float, min_speed: float, max_speed_cv: float, max_turn_deg: float
) -> list[tuple[int, int]]:
    """Index ranges [i, j] of raw track with steady speed and course over >= win_s."""
    n = len(samples)
    if n < 3:
        return []
    ref = samples[0].raw
    xy = to_local_m([s.raw for s in samples], ref)
    spd: list[float] = [0.0]
    crs: list[float] = [0.0]
    for i in range(1, n):
        dt = samples[i].t - samples[i - 1].t
        dx = xy[i][0] - xy[i - 1][0]
        dy = xy[i][1] - xy[i - 1][1]
        spd.append(math.hypot(dx, dy) / dt if dt > 0 else 0.0)
        crs.append(math.degrees(math.atan2(dx, dy)) % 360.0)
    segs: list[tuple[int, int]] = []
    i = 1
    while i < n:
        j = i
        while j + 1 < n and _steady(spd, crs, i, j + 1, min_speed, max_speed_cv, max_turn_deg):
            j += 1
        if j > i and samples[j].t - samples[i - 1].t >= win_s:
            segs.append((i - 1, j))
            i = j + 1
        else:
            i += 1
    return segs


def _steady(
    spd: list[float],
    crs: list[float],
    i: int,
    j: int,
    min_speed: float,
    max_cv: float,
    max_turn: float,
) -> bool:
    sp = spd[i : j + 1]
    m = statistics.fmean(sp)
    if m < min_speed or min(sp) < min_speed * 0.5:
        return False
    if statistics.pstdev(sp) / m > max_cv:
        return False
    c0 = crs[i]
    return all(abs((c - c0 + 180.0) % 360.0 - 180.0) <= max_turn for c in crs[i : j + 1])


def track_lag(
    samples: list[Sample],
    win_s: float = 30.0,
    min_speed: float = 3.0,
    max_speed_cv: float = 0.15,
    max_turn_deg: float = 10.0,
) -> dict[str, object]:
    """G3: along-track offset (kf - raw) projected on the raw course; negative = kf lags."""
    segs = steady_segments(samples, win_s, min_speed, max_speed_cv, max_turn_deg)
    lags: list[float] = []
    for a, b in segs:
        seg = samples[a : b + 1]
        ref = seg[0].raw
        raw = to_local_m([s.raw for s in seg], ref)
        kf = to_local_m([s.kf for s in seg], ref)
        dx = raw[-1][0] - raw[0][0]
        dy = raw[-1][1] - raw[0][1]
        norm = math.hypot(dx, dy)
        if norm == 0:
            continue
        ux, uy = dx / norm, dy / norm
        t0 = seg[0].t
        for s, r, k in zip(seg, raw, kf):
            if s.t - t0 < win_s:  # G3 is measured after the settling time
                continue
            lags.append((k[0] - r[0]) * ux + (k[1] - r[1]) * uy)
    absl = [abs(v) for v in lags]
    return {
        "segments": len(segs),
        "n": len(lags),
        "median_signed_m": statistics.median(lags) if lags else None,
        "median_abs_m": statistics.median(absl) if absl else None,
        "p95_abs_m": percentile(absl, 0.95) if absl else None,
    }


def stationary_shots(
    samples: list[Sample], shots: list[float], speed_max: float = 1.0, span_s: float = 30.0
) -> dict[str, object]:
    """G4: distance one-shots whose nearest raw-speed estimate is below speed_max."""
    if len(samples) < 2:
        return {"shots_total": len(shots), "shots_stationary": None}
    xy = to_local_m([s.raw for s in samples], samples[0].raw)
    stat = []
    for i, s in enumerate(samples):
        j = i
        while j > 0 and s.t - samples[j].t < span_s:
            j -= 1
        dt = s.t - samples[j].t
        v = math.hypot(xy[i][0] - xy[j][0], xy[i][1] - xy[j][1]) / dt if dt > 0 else 0.0
        stat.append(v < speed_max)
    times = [s.t for s in samples]
    n_stat = 0
    for t in shots:
        k = min(range(len(times)), key=lambda q: abs(times[q] - t))
        if abs(times[k] - t) <= 10.0 and stat[k]:
            n_stat += 1
    return {"shots_total": len(shots), "shots_stationary": n_stat}


def evaluate(
    samples: list[Sample], shots: list[float], track: bool, **track_kw: float
) -> dict[str, object]:
    res: dict[str, object] = {
        "samples": len(samples),
        "duration_s": (samples[-1].t - samples[0].t) if samples else 0.0,
        "rejected": sum(1 for s in samples if s.rej),
        "g1_raw": series_stats([s.raw for s in samples]),
        "g1_kf": series_stats([s.kf for s in samples]),
        "g4": stationary_shots(samples, shots),
    }
    held = [s.held for s in samples if s.held is not None]
    if held and len(held) == len(samples):
        res["g1_held"] = series_stats(held)
    if track:
        res["g3"] = track_lag(samples, **track_kw)
    return res


def _fmt(res: dict[str, object]) -> str:
    out = [
        "samples %d, duration %.0f s, rejected %d"
        % (res["samples"], res["duration_s"], res["rejected"])  # type: ignore[str-format]
    ]
    for key in ("g1_raw", "g1_kf"):
        s = res[key]
        assert isinstance(s, dict)
        if not s.get("n"):
            out.append("G1 %s: no samples" % key[3:])
            continue
        out.append(
            "G1 %-3s: modal cell %.1f %%, %d cells, %d changes, std %.1f m, p95 %.1f m"
            % (
                key[3:],
                100 * s["modal_share"],
                s["distinct_cells"],
                s["cell_changes"],
                s["std_m"],
                s["p95_dist_to_median_m"],
            )
        )
    g3 = res.get("g3")
    if isinstance(g3, dict):
        out.append(
            "G3: %d segments, %d samples, lag median %s m, |lag| median %s m, p95 %s m"
            % (
                g3["segments"],
                g3["n"],
                _f(g3["median_signed_m"]),
                _f(g3["median_abs_m"]),
                _f(g3["p95_abs_m"]),
            )
        )
    g4 = res["g4"]
    assert isinstance(g4, dict)
    out.append(
        "G4: distance shots total %s, in stationary periods %s"
        % (g4["shots_total"], g4["shots_stationary"])
    )
    return "\n".join(out)


def _f(v: object) -> str:
    return "n/a" if v is None else "%.1f" % v  # type: ignore[str-format]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+", help="capture files (meshlogger format)")
    ap.add_argument("--track", action="store_true", help="G3 along-track lag evaluation")
    ap.add_argument("--json", action="store_true", help="JSON output")
    ap.add_argument("--min-hdop", type=float, default=None, help="drop samples with hdop above")
    args = ap.parse_args(argv)

    samples: list[Sample] = []
    shots: list[float] = []
    for path in args.files:
        with open(path, errors="replace") as fh:
            s, sh = parse_lines(fh.readlines())
        samples += s
        shots += sh
    samples.sort(key=lambda x: x.t)
    shots.sort()
    if args.min_hdop is not None:
        samples = [s for s in samples if s.hdop <= args.min_hdop]
    if not samples:
        print("no 'pos raw:' lines found (need --gpsdebug 2 capture)", file=sys.stderr)
        return 2
    res = evaluate(samples, shots, args.track)
    print(json.dumps(res, indent=2) if args.json else _fmt(res))
    return 0


if __name__ == "__main__":
    sys.exit(main())
