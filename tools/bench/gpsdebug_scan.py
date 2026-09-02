#!/usr/bin/env python3
"""gpsdebug_scan.py -- bench scan tool for MeshCom `--gpsdebug on` serial captures.

Usage
-----
    python3 tools/bench/gpsdebug_scan.py <log> [--json] [--month YYYY-MM]

Stdlib only, no third-party dependencies. Companion to `tools/berglog.py`
(same conventions: dataclasses for parsed state, a `dict[str, Any]` result
tree, plain-text or `--json` output).

Log line anatomy
-----------------
`WZ_GPS_Loop()` under `iGPSDEBUG > 0` (`src/gps_functions.cpp`) prints, once
per evaluation cycle (every `gps_refresh_intervall`, 3 s, or 1 s in TRACK)::

    [GPS ]...fix:yes sat:6 hdop:2.8
    [GPS ]...Time <UTC>: 18:28:03 / Date: 2026.09.01
    [GPS ]...position  : lat:48.247924 lon:14.257376 alt:274.9

`fix:no` cycles omit the `position` line. A future firmware (doc GPS-02, this
plan's `feat-gps-nmea-20260902`) additionally prints, on a sample the new
plausibility gate throws out::

    [GPS ]...reject: lat:0.000000 lon:0.000000 date:2015.14.00 n:1

and once, on the altitude Kalman filter's first convergence::

    [GPS ]...alt converged: 275 m (P=2.3)

A `fix:` line starts a new "evaluation cycle"; the `Time <UTC>:.../Date:` and
`position` lines that follow (until the next `fix:` line) belong to it. Other
`[GPS ]` lines (e.g. the `iGPSDEBUG > 2` NMEA echo) and non-GPS lines are
ignored without failing.

A line may carry a host-side capture prefix written by
`tools/serial_monitor.py` (``YYYY-MM-DD HH:MM:SS.mmm  <payload>``) or a bare
``HH:MM:SS <payload>`` prefix; a raw `tools/bench/serial_session.py` dump
carries neither. Detected once from the first non-blank line of the file and
applied uniformly: with a prefix, 30-minute altitude buckets are keyed by
that wall-clock host timestamp; without one, by sample index at the nominal
3 s/evaluation cadence (stated in the output either way).

"Corrupt sample" (the doc's GPS-01/02 defect) is one evaluation cycle whose
`position` line has `lat == 0.0 or lon == 0.0`, or whose `Date:` line is
calendar-impossible (month outside 1..12, day outside 1..31, year < 2024) or
names a different month than the capture (`--month YYYY-MM`, default: the
most common `year.month` among the file's `Date:` lines). One corrupted
evaluation typically trips both symptoms at once (a spliced NMEA sentence
corrupts the whole fix), so cycles are deduplicated: `corrupt_samples` is a
per-cycle count, `corrupt_position` / `corrupt_date` are the raw per-line
sub-counts for diagnosis.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
from collections import Counter
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

GPS_CADENCE_S = 3.0
BUCKET_MINUTES = 30

RE_HOST_TS = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})  (.*)$")
RE_BARE_TS = re.compile(r"^(\d{2}:\d{2}:\d{2})\s+(.*)$")

RE_FIX = re.compile(r"\[GPS \]\.\.\.fix:(?P<fix>yes|no)\s+sat:(?P<sat>\d+)\s+hdop:(?P<hdop>[\d.]+)")
RE_DATE = re.compile(
    r"\[GPS \]\.\.\.Time <UTC>:\s*(?P<time>\d{2}:\d{2}:\d{2})\s*/\s*"
    r"Date:\s*(?P<year>\d{4})\.(?P<month>\d{2})\.(?P<day>\d{2})"
)
RE_POSITION = re.compile(
    r"\[GPS \]\.\.\.position\s*:\s*lat:(?P<lat>-?[\d.]+)\s+lon:(?P<lon>-?[\d.]+)\s+alt:(?P<alt>-?[\d.]+)"
)
RE_REJECT = re.compile(
    r"\[GPS \]\.\.\.reject:\s*lat:(?P<lat>-?[\d.]+)\s+lon:(?P<lon>-?[\d.]+)\s+"
    r"date:(?P<year>\d{4})\.(?P<month>\d{2})\.(?P<day>\d{2})\s+n:(?P<n>\d+)"
)
RE_CONVERGED = re.compile(r"\[GPS \]\.\.\.alt converged:\s*(?P<alt>-?\d+)\s*m")


def strip_host_ts(line: str) -> tuple[datetime | None, str]:
    """Strip a leading host-capture timestamp, if present. Returns (ts, rest)."""
    m = RE_HOST_TS.match(line)
    if m:
        return datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f"), m.group(2)
    m = RE_BARE_TS.match(line)
    if m:
        h, mi, s = (int(x) for x in m.group(1).split(":"))
        return datetime(1970, 1, 1, h, mi, s), m.group(2)
    return None, line


def detect_timestamp_mode(lines: list[str]) -> bool:
    """True when the capture carries a per-line host timestamp (checked on the
    first non-blank line: a whole file is captured consistently one way or
    the other -- tools/serial_monitor.py prefixes every line, a raw
    tools/bench/serial_session.py dump prefixes none)."""
    for raw in lines:
        if raw.strip():
            ts, _ = strip_host_ts(raw.rstrip("\n"))
            return ts is not None
    return False


@dataclass
class Cycle:
    """One GPS evaluation cycle: a `fix:` line and the `Date:`/`position:` lines
    that follow it, up to the next `fix:` line."""

    eval_index: int
    line_no: int
    ts: datetime | None
    fix: bool
    sat: int
    hdop: float
    date: tuple[int, int, int] | None = None
    position: tuple[float, float, float] | None = None
    bad_date: bool = False
    bad_position: bool = False

    @property
    def corrupt(self) -> bool:
        return self.bad_date or self.bad_position

    def virtual_seconds(self) -> float:
        """Elapsed time since capture start: real if timestamped, else eval
        index at the nominal 3 s/evaluation cadence."""
        return self.eval_index * GPS_CADENCE_S


@dataclass
class ScanResult:
    total_lines: int = 0
    timestamp_mode: bool = False
    capture_month: tuple[int, int] = (0, 0)
    evaluations: int = 0
    fixes: int = 0
    position_samples: int = 0
    date_lines: int = 0
    corrupt_position: int = 0
    corrupt_date: int = 0
    corrupt_samples: int = 0
    reject_lines: int = 0
    first_fix_eval_index: int | None = None
    first_fix_elapsed_s: float | None = None
    first_fix_ts: datetime | None = None
    alt_converged_first: dict[str, Any] | None = None
    altitude_overall: dict[str, Any] = field(default_factory=dict)
    altitude_buckets: list[dict[str, Any]] = field(default_factory=list)
    corrupt_examples: list[dict[str, Any]] = field(default_factory=list)


def _bad_date(year: int, month: int, day: int, capture_month: tuple[int, int]) -> bool:
    if not (1 <= month <= 12):
        return True
    if not (1 <= day <= 31):
        return True
    if year < 2024:
        return True
    return (year, month) != capture_month


def _resolve_capture_month(lines: list[str], explicit: tuple[int, int] | None) -> tuple[int, int]:
    if explicit is not None:
        return explicit
    votes: Counter[tuple[int, int]] = Counter()
    for raw in lines:
        _, rest = strip_host_ts(raw.rstrip("\n"))
        m = RE_DATE.search(rest)
        if m:
            votes[(int(m.group("year")), int(m.group("month")))] += 1
    if not votes:
        return (0, 0)
    return votes.most_common(1)[0][0]


def parse_capture(lines: list[str], month: tuple[int, int] | None = None) -> ScanResult:
    res = ScanResult()
    res.total_lines = len(lines)
    res.timestamp_mode = detect_timestamp_mode(lines)
    res.capture_month = _resolve_capture_month(lines, month)

    cycles: list[Cycle] = []
    cur: Cycle | None = None
    eval_index = 0

    for line_no, raw in enumerate(lines, start=1):
        ts, rest = strip_host_ts(raw.rstrip("\n"))
        if not rest.strip():
            continue

        m = RE_FIX.search(rest)
        if m:
            cur = Cycle(
                eval_index=eval_index,
                line_no=line_no,
                ts=ts,
                fix=(m.group("fix") == "yes"),
                sat=int(m.group("sat")),
                hdop=float(m.group("hdop")),
            )
            cycles.append(cur)
            eval_index += 1
            res.evaluations += 1
            if cur.fix:
                res.fixes += 1
                if res.first_fix_eval_index is None:
                    res.first_fix_eval_index = cur.eval_index
                    res.first_fix_ts = cur.ts
            continue

        m = RE_DATE.search(rest)
        if m:
            res.date_lines += 1
            year, mon, day = int(m.group("year")), int(m.group("month")), int(m.group("day"))
            if cur is not None:
                cur.date = (year, mon, day)
                if cur.ts is None and ts is not None:
                    cur.ts = ts
            if _bad_date(year, mon, day, res.capture_month):
                res.corrupt_date += 1
                if cur is not None:
                    cur.bad_date = True
                    res.corrupt_examples.append(
                        {
                            "kind": "date",
                            "line_no": line_no,
                            "date": f"{year:04d}.{mon:02d}.{day:02d}",
                        }
                    )
            continue

        m = RE_POSITION.search(rest)
        if m:
            res.position_samples += 1
            lat, lon, alt = float(m.group("lat")), float(m.group("lon")), float(m.group("alt"))
            if cur is not None:
                cur.position = (lat, lon, alt)
                if cur.ts is None and ts is not None:
                    cur.ts = ts
            if lat == 0.0 or lon == 0.0:
                res.corrupt_position += 1
                if cur is not None:
                    cur.bad_position = True
                res.corrupt_examples.append(
                    {"kind": "position", "line_no": line_no, "lat": lat, "lon": lon, "alt": alt}
                )
            continue

        m = RE_REJECT.search(rest)
        if m:
            res.reject_lines += 1
            continue

        m = RE_CONVERGED.search(rest)
        if m and res.alt_converged_first is None:
            res.alt_converged_first = {
                "line_no": line_no,
                "alt_m": int(m.group("alt")),
                "ts": ts.isoformat() if ts else None,
                "eval_index": eval_index,
            }
            continue
        # Unknown [GPS ] line, or an unrelated line -- ignore.

    res.corrupt_samples = sum(1 for c in cycles if c.corrupt)

    if res.first_fix_eval_index is not None:
        res.first_fix_elapsed_s = res.first_fix_eval_index * GPS_CADENCE_S
        first_ts = cycles[0].ts if cycles else None
        if res.timestamp_mode and first_ts is not None and res.first_fix_ts is not None:
            res.first_fix_elapsed_s = (res.first_fix_ts - first_ts).total_seconds()

    _fill_altitude_stats(res, cycles)
    return res


def _bucket_label(cycle: Cycle, timestamp_mode: bool) -> str:
    if timestamp_mode and cycle.ts is not None:
        floor = cycle.ts.replace(second=0, microsecond=0)
        floor = floor.replace(minute=(floor.minute // BUCKET_MINUTES) * BUCKET_MINUTES)
        return floor.strftime("%H:%M")
    seconds = cycle.virtual_seconds()
    bucket_start_min = int(seconds // (BUCKET_MINUTES * 60)) * BUCKET_MINUTES
    return f"+{bucket_start_min:03d}m"


def _altitude_stats(values: list[float]) -> dict[str, Any]:
    if not values:
        return {"n": 0}
    med = statistics.median(values)
    rms = math.sqrt(sum((v - med) ** 2 for v in values) / len(values))
    return {
        "n": len(values),
        "min": round(min(values), 2),
        "median": round(med, 2),
        "max": round(max(values), 2),
        "rms_around_median": round(rms, 3),
    }


def _fill_altitude_stats(res: ScanResult, cycles: list[Cycle]) -> None:
    if res.first_fix_eval_index is None:
        return
    post_fix = [c for c in cycles if c.eval_index >= res.first_fix_eval_index]
    clean = [c for c in post_fix if c.position is not None and not c.corrupt]

    res.altitude_overall = _altitude_stats([c.position[2] for c in clean if c.position])

    by_bucket: dict[str, list[float]] = {}
    order: list[str] = []
    for c in clean:
        assert c.position is not None
        label = _bucket_label(c, res.timestamp_mode)
        if label not in by_bucket:
            by_bucket[label] = []
            order.append(label)
        by_bucket[label].append(c.position[2])
    res.altitude_buckets = [{"bucket": label, **_altitude_stats(by_bucket[label])} for label in order]


def result_to_dict(res: ScanResult) -> dict[str, Any]:
    return {
        "total_lines": res.total_lines,
        "bucketing": "host-timestamp" if res.timestamp_mode else "sample-index (3s/evaluation)",
        "capture_month": f"{res.capture_month[0]:04d}-{res.capture_month[1]:02d}"
        if res.capture_month != (0, 0)
        else None,
        "evaluations": res.evaluations,
        "fixes": res.fixes,
        "position_samples": res.position_samples,
        "date_lines": res.date_lines,
        "reject_lines": res.reject_lines,
        "corrupt": {
            "samples": res.corrupt_samples,
            "position_lon_or_lat_zero": res.corrupt_position,
            "date_impossible_or_wrong_month": res.corrupt_date,
            "examples": res.corrupt_examples,
        },
        "first_fix": {
            "eval_index": res.first_fix_eval_index,
            "elapsed_s": res.first_fix_elapsed_s,
            "ts": res.first_fix_ts.isoformat() if res.first_fix_ts else None,
        },
        "alt_converged_first": res.alt_converged_first,
        "altitude_after_first_fix": {
            "overall": res.altitude_overall,
            "buckets": res.altitude_buckets,
        },
    }


def render_text(res: ScanResult, source: str) -> str:
    d = result_to_dict(res)
    lines: list[str] = []
    lines.append(f"gpsdebug_scan: {source}")
    lines.append(f"  lines: {d['total_lines']}   bucketing: {d['bucketing']}")
    lines.append(f"  capture month: {d['capture_month']}")
    lines.append(f"  evaluations: {d['evaluations']}   fixes: {d['fixes']} (fix:yes)")
    lines.append(f"  position samples: {d['position_samples']}   date lines: {d['date_lines']}")
    lines.append(f"  reject: lines: {d['reject_lines']}")
    c = d["corrupt"]
    lines.append(
        f"  corrupt samples: {c['samples']}"
        f"  (position lon/lat==0: {c['position_lon_or_lat_zero']},"
        f" bad date: {c['date_impossible_or_wrong_month']})"
    )
    for ex in c["examples"]:
        if ex["kind"] == "date":
            lines.append(f"    line {ex['line_no']}: bad date {ex['date']}")
        else:
            lines.append(f"    line {ex['line_no']}: lat:{ex['lat']} lon:{ex['lon']} alt:{ex['alt']}")
    ff = d["first_fix"]
    if ff["eval_index"] is None:
        lines.append("  first fix: none")
    else:
        lines.append(f"  first fix: eval #{ff['eval_index']}, {ff['elapsed_s']:.1f} s after capture start")
    if d["alt_converged_first"]:
        ac = d["alt_converged_first"]
        lines.append(f"  alt converged (first): {ac['alt_m']} m at line {ac['line_no']} (eval #{ac['eval_index']})")
    else:
        lines.append("  alt converged (first): not seen")
    ov = d["altitude_after_first_fix"]["overall"]
    if ov.get("n"):
        lines.append(
            "  altitude after first fix (clean samples): "
            f"n={ov['n']} min={ov['min']} median={ov['median']} max={ov['max']} "
            f"rms_around_median={ov['rms_around_median']}"
        )
        lines.append("  altitude per 30-min bucket:")
        for b in d["altitude_after_first_fix"]["buckets"]:
            lines.append(
                f"    {b['bucket']:>8s}  n={b['n']:<4d} min={b['min']:<8.2f} "
                f"median={b['median']:<8.2f} max={b['max']:<8.2f} rms={b['rms_around_median']}"
            )
    else:
        lines.append("  altitude after first fix: no clean samples")
    return "\n".join(lines)


def parse_month_arg(text: str) -> tuple[int, int]:
    try:
        year_s, month_s = text.split("-", 1)
        year, month = int(year_s), int(month_s)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"--month expects YYYY-MM, got {text!r}") from exc
    if not (1 <= month <= 12):
        raise argparse.ArgumentTypeError(f"--month month must be 1..12, got {text!r}")
    return (year, month)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Scan a MeshCom `--gpsdebug on` serial capture.")
    ap.add_argument("log", type=Path, help="capture file to analyse")
    ap.add_argument("--json", action="store_true", help="machine-readable JSON output")
    ap.add_argument(
        "--month",
        type=parse_month_arg,
        default=None,
        help="capture month as YYYY-MM (default: the most common month in the file)",
    )
    args = ap.parse_args(argv)

    with args.log.open("r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()

    res = parse_capture(lines, month=args.month)

    if args.json:
        print(json.dumps(result_to_dict(res), indent=2))
    else:
        print(render_text(res, str(args.log)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
