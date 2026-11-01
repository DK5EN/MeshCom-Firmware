#!/usr/bin/env python3
"""Merge saved mcmap node tiles into master.tsv / distances.tsv / coverage.txt.

The mcmap query itself is a manual/agent step (MCP tool `mcp__mcmap-prod__nodes_query`, read-only).
Save every tool result as a JSON file (`{"nodes": [...]}`) and pass the files here. Tile bboxes used for
the 2026-11-01 capture (bbox = "minLon,minLat,maxLon,maxLat", limit 500 each; re-use them for logs in the
same region, adapt the boxes for other regions - a tile that returns exactly 500 nodes is truncated, split it):

    9.4,46.3,13.3,49.1     AT west
    13.3,46.3,17.2,49.1    AT east
    5.5,47.0,10.5,51.3     DE south-west
    10.5,47.0,15.5,51.3    DE south-east
    5.5,51.3,10.5,55.5     DE north-west
    9.8,50.8,16.0,56.0     DE north-east / PL border
    12.0,45.5,17.5,46.3    SI / N-IT

Inputs besides the tiles: --calls DIR with one `calls_<node>.txt` per log (lines `COUNT CALL` or `CALL`;
the union of all files is the wanted set) and the logging nodes as `--logger CALL-SSID` (repeatable; the
distance column is `d_<callsign without SSID, lower case>`, or `d_<call><ssid>` when two loggers share a call). Nodes without a position (or lat/lon 0/0) get
no distance row; position-less nodes are never returned by bbox queries.

    python3 -I tools/logreport/master/merge.py --calls <dir> --out <dir> \
        --logger OE3XHL-12 --logger OE3XIA-12 tile1.json tile2.json ...

Writes master.tsv, distances.tsv, coverage.txt into --out.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

EXCLUDE_MISSING = {"OE1XAR-33", "OE1XAR-62"}  # server time-signal injectors, not nodes
HEADER = ["call", "hardware", "firmware", "lat", "lon", "altitudeM", "description", "name", "isGateway"]


def clean(v: Any) -> str:
    if v is None:
        return ""
    return str(v).replace("\t", " ").replace("\n", " ").replace("\r", " ").strip()


def load_calls(path: Path) -> list[str]:
    """Calls of one calls_<node>.txt: last whitespace-separated token of every non-empty line."""
    out: list[str] = []
    with path.open(encoding="utf-8") as fh:
        for line in fh:
            parts = line.split()
            if parts:
                out.append(parts[-1])
    return out


def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    r = 6371.0088
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = p2 - p1
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def pos_of(n: dict[str, Any] | None) -> tuple[float, float] | None:
    if not n or n.get("lat") is None or n.get("lon") is None:
        return None
    if float(n["lat"]) == 0 and float(n["lon"]) == 0:
        return None
    return float(n["lat"]), float(n["lon"])


def merge(tiles: list[Path], per_log: dict[str, list[str]], loggers: list[str], out: Path) -> dict[str, Any]:
    all_calls = sorted({c for v in per_log.values() for c in v})
    wanted = set(all_calls)
    best: dict[str, dict[str, Any]] = {}
    tile_hits = 0
    for path in tiles:
        with path.open(encoding="utf-8") as fh:
            data = json.load(fh)
        for n in data["nodes"]:
            tile_hits += 1
            call = n.get("call", "")
            if call not in wanted:
                continue
            prev = best.get(call)
            if prev is None or (n.get("lastSeenUnix") or 0) > (prev.get("lastSeenUnix") or 0):
                best[call] = n
    out.mkdir(parents=True, exist_ok=True)
    with (out / "master.tsv").open("w", encoding="utf-8") as fh:
        fh.write("\t".join(HEADER) + "\n")
        for call in sorted(best):
            n = best[call]
            row = [call, clean(n.get("hardware")), clean(n.get("firmware")), clean(n.get("lat")),
                   clean(n.get("lon")), clean(n.get("altitudeM")), clean(n.get("description")),
                   clean(n.get("name")), "true" if n.get("isGateway") else "false"]
            fh.write("\t".join(row) + "\n")
    logger_pos = {lg: pos_of(best.get(lg)) for lg in loggers}
    bases = [lg.split("-")[0].lower() for lg in loggers]
    # d_<call>; when two loggers share a base call (HB9VQQ-4, HB9VQQ-5) keep the SSID: d_hb9vqq4
    cols = ["d_" + (lg.replace("-", "").lower() if bases.count(b) > 1 else b) for lg, b in zip(loggers, bases)]
    with (out / "distances.tsv").open("w", encoding="utf-8") as fh:
        fh.write("\t".join(["call"] + cols) + "\n")
        for call in sorted(best):
            p = pos_of(best[call])
            if p is None:
                continue
            vals = []
            for lg in loggers:
                lp = logger_pos[lg]
                vals.append("" if lp is None else f"{haversine_km(p[0], p[1], lp[0], lp[1]):.2f}")
            fh.write("\t".join([call] + vals) + "\n")
    missing = [c for c in all_calls if c not in best and c not in EXCLUDE_MISSING]
    lines = ["Master-data coverage (mcmap-prod nodes_query, bbox tiles)", "",
             f"All calls: {len(all_calls)}; with master data: {len(best)}; without: {len(all_calls) - len(best)} "
             f"(of which OE1XAR-33/62 excluded from missing list: "
             f"{len([c for c in all_calls if c in EXCLUDE_MISSING and c not in best])}).",
             f"Missing (excluding OE1XAR-33/62): {len(missing)}", "", "Per log list:"]
    for name, calls in sorted(per_log.items()):
        have = sum(1 for c in calls if c in best)
        lack = sum(1 for c in calls if c not in best and c not in EXCLUDE_MISSING)
        lines.append(f"- {name}: {len(calls)} calls, {have} with data, {lack} without")
    lines += ["", "Calls without master data (excluding OE1XAR-33/62):", " ".join(missing), "",
              "Caveat: only the queried tiles are covered. Calls heard from outside the boxes cannot be matched by "
              "bbox and appear as missing even if they exist in the database. Position-less nodes are never "
              "returned by bbox queries either."]
    (out / "coverage.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    summary = {"tile_hits": tile_hits, "matched_calls": len(best), "with_position": sum(1 for c in best if pos_of(best[c])),
               "loggers_missing": [lg for lg in loggers if lg not in best], "missing_count": len(missing)}
    return summary


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--calls", required=True, type=Path, help="dir with calls_<node>.txt files")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--logger", action="append", default=[], help="logging node CALL-SSID (repeatable)")
    ap.add_argument("tiles", nargs="+", type=Path, help="saved nodes_query result JSON files")
    a = ap.parse_args(argv)
    files = sorted(a.calls.glob("calls_*.txt"))
    if not files:
        raise SystemExit(f"no calls_*.txt in {a.calls}")
    per_log = {f.name: load_calls(f) for f in files}
    print(json.dumps(merge(a.tiles, per_log, a.logger, a.out)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
