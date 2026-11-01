"""Run configuration shared by the step scripts of this topic (read from the environment).

run.py exports LOGREPORT_NORM / LOGREPORT_OUT (+ optional LOGREPORT_MASTER, LOGREPORT_STANDALONE_START,
LOGREPORT_OWNCALLS, LOGREPORT_LOGGER_UTC_OFF_S,
LOGREPORT_STANDALONE_UTC_OFF_H) before it executes the step scripts; the step scripts then
`from nodeset import ...`. The node set is whatever *.log files the norm directory holds:

  CLUSTER     logs whose lines carry the logger wall-clock prefix "YYYY-MM-DD HH:MM:SS.mmm  "
              (cross-node correlation applies to these only)
  STANDALONE  logs without that prefix (PuTTY style, node clock only); analysed alone, never correlated
"""
from __future__ import annotations

import os
import re
from pathlib import Path

WALL_PREFIX = re.compile(rb"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}  ")
DEFAULT_STANDALONE_START = "2026-10-30 17:01:11"  # logger/PC clock of the standalone capture start


def has_wall_prefix(path: Path, probe: int = 200) -> bool:
    """True if most of the first `probe` non-empty lines start with the logger wall clock."""
    hit = tot = 0
    with open(path, "rb") as fh:
        for raw in fh:
            if not raw.strip():
                continue
            tot += 1
            if WALL_PREFIX.match(raw):
                hit += 1
            if tot >= probe:
                break
    return tot > 0 and hit * 2 > tot


def discover(norm: Path) -> tuple[list[str], list[str]]:
    cluster: list[str] = []
    standalone: list[str] = []
    for p in sorted(norm.glob("*.log")):
        (cluster if has_wall_prefix(p) else standalone).append(p.stem)
    return cluster, standalone


NORM = Path(os.environ.get("LOGREPORT_NORM", "."))
OUT = Path(os.environ.get("LOGREPORT_OUT", "."))
MASTER = Path(os.environ["LOGREPORT_MASTER"]) if os.environ.get("LOGREPORT_MASTER") else None
STANDALONE_START = os.environ.get("LOGREPORT_STANDALONE_START", DEFAULT_STANDALONE_START)
STANDALONE_UTC_OFF_H = int(os.environ.get("LOGREPORT_STANDALONE_UTC_OFF_H", "2"))  # node clock minus UTC at capture start
CLUSTER, STANDALONE = discover(NORM) if os.environ.get("LOGREPORT_NORM") else ([], [])
LOGGER_UTC_OFF_S = int(os.environ.get("LOGREPORT_LOGGER_UTC_OFF_S", "3600"))  # logger PC clock minus UTC (CET)
ALL = CLUSTER + STANDALONE
OWNCALL = {n: n.upper() + "-12" for n in ALL}
for _kv in filter(None, os.environ.get("LOGREPORT_OWNCALLS", "").split(",")):
    _k, _, _v = _kv.partition("=")
    OWNCALL[_k] = _v
