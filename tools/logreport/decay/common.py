"""Shared helpers for the Q3/Q4 analysis (stdlib only)."""
from __future__ import annotations

import datetime as dt
import math
import pickle
from pathlib import Path

#: wall-clock node names of the current run (mutated in place by configure())
NODES: list[str] = []
_CTX: dict = {"work": Path("."), "base": dt.date(2000, 1, 1)}
TYPE_NAME = {"40": "HEY", "21": "POS", "3A": "TEXT", "41": "ACK", "40x": "HEY"}
TTL_S = {1: 90, 3: 300, 4: 120, 5: 180}

MAX_RING = 20


def configure(work_dir: Path, nodes: list[str], base: dt.date) -> None:
    """Set the intermediate dir, the wall-clock node set and day 0 (date of the earliest log line)."""
    work_dir.mkdir(parents=True, exist_ok=True)
    _CTX["work"] = work_dir
    _CTX["base"] = base
    NODES[:] = nodes


def work() -> Path:
    return _CTX["work"]


def load(node: str) -> dict:
    with open(work() / f"{node}.pkl", "rb") as fh:
        return pickle.load(fh)


def hhmmss(t: float) -> str:
    """Seconds since day 0 00:00 -> 'DD.MM HH:MM:SS' (logger clock)."""
    day = int(t // 86400)
    r = t - day * 86400
    d = _CTX["base"] + dt.timedelta(days=day)
    return f"{d:%d.%m} {int(r // 3600):02d}:{int(r % 3600 // 60):02d}:{r % 60:04.1f}"


def toa_ms(nbytes: int, sf: int = 11, bw_hz: float = 250000.0, cr: int = 2, preamble: int = 8,
           explicit: bool = True, crc: bool = True) -> float:
    """Semtech SX126x/SX127x LoRa time on air (ms). cr=1..4 means 4/5..4/8; EU8 uses 4/6 (cr=2).
    LDRO auto: on when Tsym > 16 ms (SF11/BW250: Tsym 8.19 ms -> off)."""
    tsym = (2 ** sf) / bw_hz * 1000.0
    de = 1 if tsym > 16.0 else 0
    ih = 0 if explicit else 1
    num = 8 * nbytes - 4 * sf + 28 + (16 if crc else 0) - 20 * ih
    npay = 8 + max(math.ceil(num / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return (preamble + 4.25) * tsym + npay * tsym


def pct(xs: list[float], p: float) -> float:
    if not xs:
        return float("nan")
    ys = sorted(xs)
    k = (len(ys) - 1) * p / 100.0
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return ys[lo] + (ys[hi] - ys[lo]) * (k - lo)
