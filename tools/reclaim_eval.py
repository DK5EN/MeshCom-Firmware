#!/usr/bin/env python3
"""Auswertung eines Konsolen-Mitschnitts (meshlogger.py) fuer den RAM-Rueckgewinn.

Liest eine oder mehrere Logdateien und beantwortet die Fragen, die nach dem
Umbau der drei Ausgangsringe auf Byte-Ringe offen sind:

  * Heap: erster/letzter/kleinster Wert aus den STAT-Zeilen (`heap=`) und
    aus `[HEAP]`-Zeilen, Trend ueber die Laufzeit.
  * Neustarts: jede `[INIT]`-Zeile und jeder Sprung von `up=` nach unten.
  * Ring-Verdraengung: `RING_OVERFLOW` mit `lost=`.
  * Telefon-Ring: `BLEtoPhone RingBuff added ... lost=N` mit N > 0.
  * STAT-Kennzahlen: ringmax und drop je Fenster (TX-Ring, unveraendert).

Aufruf:
    uv run tools/reclaim_eval.py ~/Downloads/dk5en-98/2026-09-20.log [weitere...]
    python3 tools/reclaim_eval.py --since "2026-09-20 12:00" datei.log
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path

STAT_RE = re.compile(
    r"^(?P<ts>\S+ \S+)\s+\S+ \[LOG\] STAT .*?ringmax=(?P<ringmax>\d+)/(?P<ringcap>\d+) "
    r"drop=(?P<drop>[\d/]+) .*?heap=(?P<heap>\d+) .*?up=(?P<up>\d+)"
)
HEAP_RE = re.compile(r"^(?P<ts>\S+ \S+).*\[HEAP\]\S*;(?P<heap>\d+);")
INIT_RE = re.compile(r"^(?P<ts>\S+ \S+).*\[INIT\]\.\.\.build (?P<build>[^\n]+)")
OVERFLOW_RE = re.compile(r"^(?P<ts>\S+ \S+).*RING_OVERFLOW buf=(?P<buf>\w+)(?: lost=(?P<lost>\d+))?")
PHONE_LOST_RE = re.compile(r"^(?P<ts>\S+ \S+).*BLEtoPhone RingBuff added .* lost=(?P<lost>-?\d+)")
LOGGER_RE = re.compile(r"\[LOGGER\] (?P<what>\w+)")


@dataclass
class Result:
    stat_heap: list[tuple[str, int]] = field(default_factory=list)
    heap_lines: list[tuple[str, int]] = field(default_factory=list)
    inits: list[tuple[str, str]] = field(default_factory=list)
    up_drops: list[tuple[str, int, int]] = field(default_factory=list)
    overflows: list[tuple[str, str, int]] = field(default_factory=list)
    phone_lost: list[tuple[str, int]] = field(default_factory=list)
    ringmax: list[int] = field(default_factory=list)
    drops: list[str] = field(default_factory=list)
    reconnects: int = 0
    lines: int = 0
    first_ts: str = ""
    last_ts: str = ""


def scan(paths: list[Path], since: str | None) -> Result:
    r = Result()
    last_up: int | None = None
    for p in paths:
        with p.open(errors="replace") as fh:
            for line in fh:
                r.lines += 1
                ts = line[:19]
                if since and ts < since:
                    continue
                if not r.first_ts and ts[:4].isdigit():
                    r.first_ts = ts
                if ts[:4].isdigit():
                    r.last_ts = ts
                m = STAT_RE.match(line)
                if m:
                    heap = int(m.group("heap"))
                    up = int(m.group("up"))
                    r.stat_heap.append((ts, heap))
                    r.ringmax.append(int(m.group("ringmax")))
                    r.drops.append(m.group("drop"))
                    if last_up is not None and up < last_up:
                        r.up_drops.append((ts, last_up, up))
                    last_up = up
                    continue
                m = HEAP_RE.match(line)
                if m:
                    r.heap_lines.append((ts, int(m.group("heap"))))
                    continue
                m = INIT_RE.match(line)
                if m:
                    r.inits.append((ts, m.group("build").strip()))
                    continue
                m = OVERFLOW_RE.match(line)
                if m:
                    r.overflows.append((ts, m.group("buf"), int(m.group("lost") or 1)))
                    continue
                m = PHONE_LOST_RE.match(line)
                if m and int(m.group("lost")) > 0:
                    r.phone_lost.append((ts, int(m.group("lost"))))
                    continue
                m = LOGGER_RE.search(line)
                if m and m.group("what") == "reconnect":
                    r.reconnects += 1
    return r


def trend(series: list[tuple[str, int]]) -> str:
    if len(series) < 2:
        return "zu wenige Punkte"
    vals = [v for _, v in series]
    first, last = vals[0], vals[-1]
    lo, hi = min(vals), max(vals)
    n = len(vals)
    # Steigung ueber Index (Fenster sind aequidistant, 5 min je STAT)
    xs = list(range(n))
    mx, my = statistics.mean(xs), statistics.mean(vals)
    num = sum((x - mx) * (y - my) for x, y in zip(xs, vals))
    den = sum((x - mx) ** 2 for x in xs) or 1
    slope = num / den
    return (
        f"n={n} erster={first} letzter={last} min={lo} max={hi} "
        f"median={int(statistics.median(vals))} steigung={slope:+.1f} B/Fenster"
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+", type=Path)
    ap.add_argument("--since", help="nur Zeilen ab diesem Zeitstempel (YYYY-MM-DD HH:MM)")
    args = ap.parse_args(argv)

    missing = [p for p in args.logs if not p.exists()]
    if missing:
        print("fehlt:", ", ".join(str(p) for p in missing), file=sys.stderr)
        return 2

    r = scan(args.logs, args.since)
    print(f"Zeilen: {r.lines}  Zeitraum: {r.first_ts} .. {r.last_ts}  Logger-Reconnects: {r.reconnects}")
    print()
    print("Heap (STAT heap=):", trend(r.stat_heap))
    if r.heap_lines:
        print("Heap ([HEAP]):    ", trend(r.heap_lines))
    print()
    print(f"Neustarts ([INIT]): {len(r.inits)}")
    for ts, b in r.inits:
        print(f"   {ts}  {b}")
    if r.up_drops:
        print(f"Uptime-Spruenge nach unten: {len(r.up_drops)}")
        for ts, a, b in r.up_drops:
            print(f"   {ts}  up {a} -> {b}")
    print()
    print(f"RING_OVERFLOW: {len(r.overflows)}  (verlorene ungelesene Frames: {sum(l for _, _, l in r.overflows)})")
    by_buf: dict[str, int] = {}
    for _, buf, lost in r.overflows:
        by_buf[buf] = by_buf.get(buf, 0) + lost
    for buf, lost in sorted(by_buf.items()):
        print(f"   {buf}: {lost}")
    print(f"Telefon-Ring lost>0: {len(r.phone_lost)}  (Summe {sum(l for _, l in r.phone_lost)})")
    print()
    if r.ringmax:
        print(f"TX-Ring: ringmax Spitze {max(r.ringmax)}  Fenster {len(r.ringmax)}  "
              f"drop-Muster: {sorted(set(r.drops))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
