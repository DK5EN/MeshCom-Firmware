"""Shared parsing helpers for the hb9 analysis checks (stdlib only, run with python3 -I)."""
from __future__ import annotations

import datetime as dt
import re
from typing import Iterator, Optional, Tuple

TS_RE = re.compile(r"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  (.*)$")


def parse_ts(line: str) -> Optional[Tuple[float, str]]:
    """Return (epoch seconds of the logger wall clock treated as naive UTC, body) or None."""
    m = TS_RE.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms, body = m.groups()
    t = dt.datetime(int(y), int(mo), int(d), int(h), int(mi), int(s), tzinfo=dt.timezone.utc)
    return t.timestamp() + int(ms) / 1000.0, body


def iter_lines(path: str) -> Iterator[Tuple[int, float, str]]:
    """Stream (lineno, t, body) for every timestamped line."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for n, line in enumerate(fh, 1):
            r = parse_ts(line.rstrip("\n"))
            if r is not None:
                yield n, r[0], r[1]


def hour_key(t: float) -> str:
    return dt.datetime.fromtimestamp(t, tz=dt.timezone.utc).strftime("%m-%d %H")


def fmt(t: float) -> str:
    return dt.datetime.fromtimestamp(t, tz=dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def pct(vals, p: float) -> Optional[float]:
    if not vals:
        return None
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def dist(vals) -> dict:
    if not vals:
        return {"n": 0}
    return {"n": len(vals), "min": min(vals), "p5": pct(vals, 5), "p50": pct(vals, 50),
            "p90": pct(vals, 90), "p95": pct(vals, 95), "p99": pct(vals, 99), "max": max(vals),
            "mean": sum(vals) / len(vals)}


KV_RE = re.compile(r"(\w+)=([^\s]+)")


def kv(body: str) -> dict:
    return {k: v for k, v in KV_RE.findall(body)}


MSG_RE = re.compile(r"\b(?:MH-LoRa|TX-LoRa|RX-LoRa\d?|TX-UDP|RX-UDP|NEW-TXT)\s+(\d{3}) (.) ?: ?x([0-9A-F]{8}) H(\d\d) S(\d) T(\d) M(\d\d) (.*?) HW:")


def air_s(nbytes: int, sf: int = 11, bw: float = 250000.0, cr: int = 2, pre: int = 8) -> float:
    """LoRa time on air, explicit header, CRC on, LDRO off (SF11 BW250 CR4/6 default)."""
    import math
    tsym = (2 ** sf) / bw
    npay = 8 + max(math.ceil((8 * nbytes - 4 * sf + 28 + 16) / (4.0 * sf)) * (cr + 4), 0)
    return (pre + 4.25) * tsym + npay * tsym
