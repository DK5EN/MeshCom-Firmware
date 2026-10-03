#!/usr/bin/env python3
"""Cross-node LoRa exchange on the bench fleet (REG-05 / TM-26, runbook 2.4).

    mesh_exchange.py --port /dev/cu.usbmodem1101 --port /dev/cu.usbserial-0001 \\
                     --port /dev/cu.usbserial-573C000584 --out exchange.json
    mesh_exchange.py --dry-run --port A --port B     # print the plan, open nothing

Sequence (what the 2026-08-29 scratchpad script did by hand):

  1. open every port, wait ``--settle`` seconds (ESP32 bridges reboot on open);
  2. ``--info`` on each node -> callsign; the identity guard (identity_guard.py,
     fleet.json) must pass for every node before anything is transmitted;
  3. ``--loradebug on`` everywhere;
  4. ``--sendpos`` from each node in turn, ``--spacing`` seconds apart (a position
     beacon of the node's own callsign, nothing else is ever sent);
  5. ``--mheard`` on each node;
  6. ``--loradebug off`` everywhere -- also when anything above raised.

Pass = the beacon of every node appears in the ``--mheard`` of every other node.
Exit 0 = pass, 1 = at least one pair missing or a node refused by the guard,
2 = usage. The ``--mheard`` window is 12 h, so rows are filtered by their ``age=``
to the run's own length; an old entry from an earlier run cannot satisfy a pair.

Pure logic (parse_mheard, build_matrix, verdict, summarize) does no I/O; the
session class is rak_harness.RakSession (DTR per port family via dtr_for).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Set, Tuple

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# --mheard row (src/command_functions.cpp): "[MH] call=DK5EN-90 <ts> typ=... age=3min ..."
_MH_ROW = re.compile(r"\[MH\]\s+call=(\S+)")
_MH_AGE = re.compile(r"\bage=(\d+)min\b")
# Older / free-form replies: a callsign with SSID somewhere in a table row.
_CALL_SSID = re.compile(r"(?<![A-Za-z0-9-])([A-Z]{1,2}\d[A-Z]{1,4}-\d{1,2})(?![A-Za-z0-9-])")
# Live log lines (loradebug) are not part of the --mheard reply.
_LIVE_LOG = re.compile(r"MH-LoRa|RX-LoRa|TX-LoRa|OnTXDone|OnRxDone")


# ---------------------------------------------------------------- pure logic

def parse_mheard(text: str, max_age_min: Optional[int] = None) -> Set[str]:
    """Callsigns listed in a ``--mheard`` reply.

    Current firmware prints one ``[MH] call=X ... age=Nmin ...`` row per station
    (plus a ``[MH] window=720min rows=N`` header). When any such row exists only
    those count; ``max_age_min`` drops rows older than that. Replies without
    ``[MH] call=`` rows fall back to callsign-with-SSID tokens (the format
    rak_harness.scenario_mheard matches), skipping live LoRa log lines.
    """
    lines = text.replace("\r", "").split("\n")
    heard: Set[str] = set()
    saw_rows = False
    for line in lines:
        m = _MH_ROW.search(line)
        if not m:
            continue
        saw_rows = True
        if max_age_min is not None:
            age = _MH_AGE.search(line)
            if age and int(age.group(1)) > max_age_min:
                continue
        heard.add(m.group(1).strip(",;"))
    if saw_rows:
        return heard
    for line in lines:
        if _LIVE_LOG.search(line) or "--mheard" in line or "--mh" in line:
            continue
        for m in _CALL_SSID.finditer(line):
            heard.add(m.group(1))
    return heard


def build_matrix(calls: List[str], heard: Dict[str, Set[str]]) -> Dict[str, Dict[str, bool]]:
    """``matrix[sender][listener]`` = listener's --mheard contains sender, for A != B."""
    return {
        a: {b: a in heard.get(b, set()) for b in calls if b != a}
        for a in calls
    }


def verdict(matrix: Dict[str, Dict[str, bool]]) -> Tuple[bool, List[Tuple[str, str]]]:
    """(ok, missing) with missing = [(sender, listener)] where the listener did not hear."""
    missing = [(a, b) for a, row in matrix.items() for b, ok in row.items() if not ok]
    return (not missing, missing)


def summarize(calls: List[str], matrix: Dict[str, Dict[str, bool]]) -> str:
    """Human-readable grid (rows = sender, columns = listener) plus the verdict."""
    ok, missing = verdict(matrix)
    width = max([len(c) for c in calls] + [8])
    out = ["beacon heard by (row = sender, column = listener):"]
    out.append(" " * (width + 2) + "  ".join(c.ljust(width) for c in calls))
    for a in calls:
        cells = ["-" if a == b else ("yes" if matrix[a][b] else "NO") for b in calls]
        out.append(a.ljust(width) + "  " + "  ".join(c.ljust(width) for c in cells))
    pairs = len(calls) * (len(calls) - 1)
    if ok:
        out.append(f"PASS: all {pairs} pairs heard ({len(calls)} nodes)")
    else:
        out.append(f"FAIL: {len(missing)} of {pairs} pairs missing")
        for a, b in missing:
            out.append(f"  {a} not heard by {b}")
    return "\n".join(out)


# ------------------------------------------------------------------- driver

def auto_max_age_min(n_nodes: int, spacing: float) -> int:
    """Longest age (minutes) a beacon of this run can have at --mheard time."""
    return int(math.ceil((n_nodes * spacing + 60.0) / 60.0)) + 2


def plan_lines(ports: Sequence[str], settle: float, spacing: float, dtr: str) -> List[str]:
    n = len(ports)
    lines = [f"mesh_exchange plan: {n} nodes, dtr={dtr}, settle={settle:g}s, spacing={spacing:g}s"]
    for p in ports:
        lines.append(f"  port {p}")
    lines += [
        f"  1. open all ports, settle {settle:g}s",
        "  2. --info on each node -> callsign; identity guard (fleet.json) per node, refuse on failure",
        "  3. --loradebug on on each node",
        f"  4. --sendpos from each node in turn, {spacing:g}s apart ({n} beacons)",
        f"  5. --mheard on each node (rows older than {auto_max_age_min(n, spacing)} min ignored)",
        "  6. --loradebug off on each node (always, also after an error)",
        f"  pass = every beacon in every other node's --mheard ({n * (n - 1)} pairs); exit 1 otherwise",
        f"  estimated duration ~{int(settle + 10 * n + n * spacing + 10 * n)}s",
    ]
    return lines


def make_session(port: str, dtr: str, log_dir: Path) -> Any:
    """Open a rak_harness.RakSession (the only place a serial port is opened)."""
    import rak_harness  # lazy: pulls extudp_peer, keeps the pure parts import-light

    dtr_flag = None if dtr == "auto" else (dtr == "on")
    stamp = time.strftime("%Y%m%d-%H%M%S")
    name = re.sub(r"[^A-Za-z0-9]+", "_", Path(port).name)
    log_dir.mkdir(parents=True, exist_ok=True)
    s = rak_harness.RakSession(port, log_path=log_dir / f"mesh_exchange_{name}_{stamp}.log", dtr=dtr_flag)
    s.open()
    return s


def _pump_all(sessions: Sequence[Any], seconds: float) -> None:
    """Drain every session for ``seconds`` (round-robin, so no buffer overruns)."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        for s in sessions:
            s.pump(min(0.2, max(0.0, end - time.monotonic())))


def guard_node(info_text: str, fleet: Dict[str, Any]) -> Tuple[str, str]:
    """(fleet node name, callsign) or raise identity_guard.IdentityError."""
    from identity_guard import IdentityError, parse_info, require

    call = parse_info(info_text).call
    names = [n for n, e in fleet["nodes"].items() if e["call"] == call]
    if not call or not names:
        raise IdentityError(f"callsign {call!r} is not a registered bench node in fleet.json: refused")
    require(info_text, names[0], fleet)
    return names[0], call


def run_exchange(sessions: Sequence[Any], settle: float, spacing: float,
                 fleet: Dict[str, Any], clock_sleep: Callable[[float], None] = time.sleep) -> Dict[str, Any]:
    """Steps 1-6 on already-open sessions. Returns the result dict."""
    result: Dict[str, Any] = {"calls": [], "ports": [s.port for s in sessions]}
    loradebug_sent = False
    try:
        _pump_all(sessions, settle)

        calls: List[str] = []
        refusals: List[str] = []
        for s in sessions:
            idx = s.send("--info")
            s.wait_for(r"\.\.\.Call: <", 6.0, since=idx)
            s.pump(2.0)
            text = "\n".join(s.lines_since(idx))
            try:
                _, call = guard_node(text, fleet)
                calls.append(call)
            except Exception as exc:  # noqa: BLE001 - IdentityError and parse trouble alike
                refusals.append(f"{s.port}: {exc}")
        if refusals:
            result.update(ok=False, refused=refusals, error="identity guard refused; nothing was transmitted")
            return result
        if len(set(calls)) != len(calls):
            result.update(ok=False, error=f"duplicate callsigns across ports: {calls}; nothing was transmitted")
            return result
        result["calls"] = calls

        loradebug_sent = True
        for s in sessions:
            s.send("--loradebug on")
        _pump_all(sessions, 1.5)

        tx_evidence: Dict[str, List[str]] = {}
        for s, call in zip(sessions, calls):
            idx = s.send("--sendpos")
            _pump_all(sessions, spacing)
            tx_evidence[call] = [l for l in s.lines_since(idx) if re.search(r"OnTXDone|TX-LoRa", l)][:3]
        result["tx_evidence"] = tx_evidence

        raw: Dict[str, str] = {}
        heard: Dict[str, Set[str]] = {}
        max_age = auto_max_age_min(len(sessions), spacing)
        for s, call in zip(sessions, calls):
            idx = s.send("--mheard")
            s.wait_for(r"\[MH\]|MHeard", 6.0, since=idx)
            s.pump(2.0)
            raw[call] = "\n".join(s.lines_since(idx))
            heard[call] = parse_mheard(raw[call], max_age_min=max_age)
        matrix = build_matrix(calls, heard)
        ok, missing = verdict(matrix)
        result.update(matrix=matrix, missing=[list(p) for p in missing], ok=ok,
                      heard={c: sorted(h) for c, h in heard.items()}, mheard_raw=raw)
        return result
    finally:
        if loradebug_sent:
            for s in sessions:
                try:
                    s.send("--loradebug off")
                except Exception:  # noqa: BLE001 - keep restoring the other nodes
                    pass
            try:
                _pump_all(sessions, 1.0)
            except Exception:  # noqa: BLE001
                pass


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="cross-node LoRa exchange: every beacon in every other node's --mheard")
    ap.add_argument("--port", action="append", default=[], help="serial port of one bench node (repeat, at least two)")
    ap.add_argument("--dtr", choices=["auto", "on", "off"], default="auto",
                    help="DTR on open (auto: on for usbmodem native-USB, off for USB-UART bridges)")
    ap.add_argument("--settle", type=float, default=50.0, help="seconds to wait after opening all ports (default 50)")
    ap.add_argument("--spacing", type=float, default=25.0, help="seconds between the --sendpos of consecutive nodes (default 25)")
    ap.add_argument("--out", type=Path, default=None, help="write the result JSON here")
    ap.add_argument("--log-dir", type=Path, default=HERE / "runs", help="raw session logs (default tools/bench/runs)")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, open nothing")
    return ap


def main(argv: Optional[Sequence[str]] = None,
         session_factory: Optional[Callable[..., Any]] = None) -> int:
    ap = build_parser()
    a = ap.parse_args(argv)
    if len(a.port) < 2:
        ap.error("at least two --port are required")
    if a.settle < 0 or a.spacing <= 0:
        ap.error("--settle must be >= 0 and --spacing > 0")

    for line in plan_lines(a.port, a.settle, a.spacing, a.dtr):
        print(line)
    if a.dry_run:
        return 0

    from identity_guard import fleet_problems, load_fleet

    fleet = load_fleet()
    problems = fleet_problems(fleet)
    if problems:
        print("fleet.json inconsistent: " + "; ".join(problems), file=sys.stderr)
        return 1

    factory = session_factory or make_session
    sessions: List[Any] = []
    result: Dict[str, Any]
    try:
        for p in a.port:
            sessions.append(factory(p, a.dtr, a.log_dir))
        result = run_exchange(sessions, a.settle, a.spacing, fleet)
    finally:
        for s in sessions:
            try:
                s.close()
            except Exception:  # noqa: BLE001
                pass

    if "matrix" in result:
        print(summarize(result["calls"], result["matrix"]))
    else:
        print("REFUSED: " + result.get("error", "unknown"), file=sys.stderr)
        for r in result.get("refused", []):
            print("  " + r, file=sys.stderr)
    if a.out:
        a.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print(f"wrote {a.out}")
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
