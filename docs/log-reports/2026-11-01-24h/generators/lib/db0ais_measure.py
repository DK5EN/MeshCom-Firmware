#!/usr/bin/env python3
"""Raw-log measurements for the DB0AIS-12 report page (needs the normalised db0ais.log).

Usage:
    python3 -I db0ais_measure.py --norm <dir holding db0ais.log> --out <data/extra/db0ais_measure.json>

Measures what no earlier data file holds:
  clock_step      node-clock step of -1 h (capture lines, times, settings-save markers between)
  id_reuse        message ids that were relayed twice (origin, payload time, gap, dedup-ring state)
  id_overlap      time-server ids shared between OE1XAR-33 and OE1XAR-63 (received side)
  dedup_window    time the 100-slot dedup ring needs to turn over (min / median / max)
  rx_attempts     RX_ERROR, OnRxDone and ACK receptions from the raw lines
Stdlib only.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

RE_LOG_CLOCK = re.compile(r"^(\d\d):(\d\d):(\d\d) \[LOG\] ")
RE_T = re.compile(r" t=(\d+)\s*$")
RE_RX_CET = re.compile(
    r"^(\d\d:\d\d:\d\d) \[LOG\] \d+ : x(\w{8}) H\d+ .*? ([A-Z0-9-]+)[,>][^:]*:\{CET\}"
    r"(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d).*? DUP:(\w) .*? t=(\d+)"
)
RE_TX_CET = re.compile(r"^(\d\d:\d\d:\d\d) TX-LoRa\s+(\d+) : x(\w{8}) .*? ([A-Z0-9-]+)[,>][^:]*:\{CET\}(\S+ \S+)")
RE_CET_ID = re.compile(r" x(\w{8}) H\d+ .*\{CET\}")
RE_NEED = re.compile(r"^\[NBR\]\|NEED\|\d+\|(\w{8})\|(\w)\|(\w)\|")
RE_DEDUP = re.compile(r"RX_DEDUP_(NEW|ADD|DUP) msg_id=(\w{8})")
RE_TXREAD = re.compile(r"RING_TX_READ .*? msg_id=(\w{8})")


def secs(hms: str) -> int:
    h, m, s = (int(x) for x in hms.split(":"))
    return h * 3600 + m * 60 + s


def pct(a: float, b: float) -> float:
    return round(100.0 * a / b, 2) if b else 0.0


def stamp_s(stamp: str) -> int:
    """Seconds since the epoch of a 'YYYY-MM-DD HH:MM:SS' payload stamp (UTC)."""
    return int(datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp())


def measure(log: Path) -> dict[str, Any]:
    clock_step: dict[str, Any] | None = None
    prev_clock: tuple[int, int, str, int] | None = None  # (line, seconds, text, t_ms)
    setst_between: list[str] = []
    rx_err = rx_done = ack_rx = 0
    last_t = 0
    adds: list[tuple[int, str]] = []  # (t_ms, id) in order
    add_seq = 0
    add_index_of: dict[str, int] = {}  # id -> latest ADD sequence number
    rx_cet: dict[str, list[dict[str, Any]]] = {}
    txread_lines: dict[str, list[int]] = {}
    tx_cet_by_line: dict[int, dict[str, Any]] = {}
    new_adds_between: dict[str, int] = {}
    first_add_seq: dict[str, int] = {}
    cet_ids: set[str] = set()
    need_cet = {"total": 0, "case_A": 0}

    with log.open(encoding="utf-8", errors="replace") as fh:
        for no, raw in enumerate(fh, 1):
            line = raw.rstrip("\n")
            if line.startswith("[MC-DBG] RX_ERROR "):
                rx_err += 1
            elif line.startswith("OnRxDone"):
                rx_done += 1
            m = RE_LOG_CLOCK.match(line)
            if m:
                sec = secs(f"{m.group(1)}:{m.group(2)}:{m.group(3)}")
                text = line[:8]
                mt2 = RE_T.search(line)
                if prev_clock is not None and clock_step is None and prev_clock[1] - sec >= 3300:
                    mt = RE_T.search(line)
                    clock_step = {
                        "line_before": prev_clock[0],
                        "time_before": prev_clock[2],
                        "t_ms_before": prev_clock[3],
                        "line_after": no,
                        "time_after": text,
                        "step_s": sec - prev_clock[1],
                        "t_ms_after": int(mt.group(1)) if mt else None,
                        "between": list(setst_between),
                    }
                prev_clock = (no, sec, text, int(mt2.group(1)) if mt2 else 0)
                setst_between = []
                if line[8:].startswith(" [LOG] 012 A "):
                    ack_rx += 1
            elif line.startswith("[SETST]"):
                setst_between.append(line)
            mt = RE_T.search(line)
            if mt and "[LOG]" in line:
                last_t = int(mt.group(1))
            md = RE_DEDUP.search(line)
            if md:
                kind, mid = md.group(1), md.group(2)
                if kind == "ADD":
                    add_seq += 1
                    adds.append((last_t, mid))
                    if mid in add_index_of and mid not in new_adds_between:
                        # second ADD of an id: ADD events since its first ADD
                        new_adds_between[mid] = add_seq - first_add_seq[mid]
                    first_add_seq.setdefault(mid, add_seq)
                    add_index_of[mid] = add_seq
            mc = RE_CET_ID.search(line)
            if mc:
                cet_ids.add(mc.group(1))
            mn = RE_NEED.match(line)
            if mn and mn.group(1) in cet_ids:
                need_cet["total"] += 1
                if mn.group(3) == "A":
                    need_cet["case_A"] += 1
            mr = RE_RX_CET.match(line)
            if mr:
                rx_cet.setdefault(mr.group(2), []).append(
                    {
                        "origin": mr.group(3),
                        "stamp": mr.group(4),
                        "dup": mr.group(5),
                        "t_ms": int(mr.group(6)),
                    }
                )
            mx = RE_TXREAD.search(line)
            if mx:
                txread_lines.setdefault(mx.group(1), []).append(no)
            mtx = RE_TX_CET.match(line)
            if mtx:
                tx_cet_by_line[no] = {
                    "clock": mtx.group(1),
                    "len": int(mtx.group(2)),
                    "id": mtx.group(3),
                    "origin": mtx.group(4),
                    "stamp": mtx.group(5),
                }

    # ---- relayed twice
    tx_lines_sorted = sorted(tx_cet_by_line)
    pairs: list[dict[str, Any]] = []
    for mid, lines in sorted(txread_lines.items()):
        if len(lines) < 2:
            continue
        txs = []
        for ln in lines:
            nxt = [x for x in tx_lines_sorted if ln <= x <= ln + 12]
            txs.append(tx_cet_by_line[nxt[0]] if nxt else None)
        if txs[0] is None or txs[1] is None:
            continue
        gap = stamp_s(txs[1]["stamp"]) - stamp_s(txs[0]["stamp"])
        pairs.append(
            {
                "id": mid,
                "first_origin": txs[0]["origin"],
                "second_origin": txs[1]["origin"],
                "payload_gap_s": gap,
                "same_payload": txs[0]["stamp"] == txs[1]["stamp"],
                "first_stamp": txs[0]["stamp"],
                "second_stamp": txs[1]["stamp"],
                "adds_between": new_adds_between.get(mid),
            }
        )
    gaps = [p["payload_gap_s"] for p in pairs]
    adds_between = [p["adds_between"] for p in pairs if p["adds_between"] is not None]

    # ---- id overlap -33 vs -63 (received side, first sight)
    by_origin: dict[str, dict[str, dict[str, Any]]] = {}
    for mid, recs in rx_cet.items():
        for r in recs:
            by_origin.setdefault(r["origin"], {}).setdefault(mid, r)
    ids33 = by_origin.get("OE1XAR-33", {})
    ids62 = by_origin.get("OE1XAR-62", {})
    ids63 = by_origin.get("OE1XAR-63", {})
    shared_33_63 = sorted(set(ids33) & set(ids63))
    shared_62 = sorted((set(ids62) & set(ids33)) | (set(ids62) & set(ids63)))
    lag = [stamp_s(ids63[i]["stamp"]) - stamp_s(ids33[i]["stamp"]) for i in shared_33_63]
    first_sight_dup = 0
    for i in shared_33_63:
        recs = [r for r in rx_cet[i] if r["origin"] == "OE1XAR-63"]
        if recs and min(recs, key=lambda r: r["t_ms"])["dup"] == "d":
            first_sight_dup += 1

    # ---- dedup ring turnover (time for 100 ADDs)
    windows_min: list[float] = []
    for k in range(100, len(adds)):
        dt = adds[k][0] - adds[k - 100][0]
        if dt > 0:
            windows_min.append(dt / 60000.0)
    return {
        "clock_step": clock_step,
        "need_cet": need_cet,
        "rx_attempts": {"rx_error": rx_err, "onrxdone": rx_done, "ack_rx": ack_rx},
        "id_reuse": {
            "n": len(pairs),
            "pairs": pairs,
            "payload_gap_s": {
                "min": min(gaps) if gaps else None,
                "median": statistics.median(gaps) if gaps else None,
                "max": max(gaps) if gaps else None,
            },
            "first_origin_set": sorted({p["first_origin"] for p in pairs}),
            "second_origin_set": sorted({p["second_origin"] for p in pairs}),
            "same_payload_n": sum(1 for p in pairs if p["same_payload"]),
            "adds_between": {
                "min": min(adds_between) if adds_between else None,
                "median": statistics.median(adds_between) if adds_between else None,
                "max": max(adds_between) if adds_between else None,
            },
        },
        "id_overlap": {
            "ids_33": len(ids33),
            "ids_62": len(ids62),
            "ids_63": len(ids63),
            "shared_33_63": len(shared_33_63),
            "shared_33_63_pct_of_63": pct(len(shared_33_63), len(ids63)),
            "shared_62_with_33_or_63": len(shared_62),
            "lag_63_behind_33_s": {
                "min": min(lag) if lag else None,
                "median": statistics.median(lag) if lag else None,
                "max": max(lag) if lag else None,
            },
            "first_sight_of_63_flagged_dup": first_sight_dup,
        },
        "dedup_window": {
            "adds": len(adds),
            "turnover_min": {
                "min": round(min(windows_min), 1) if windows_min else None,
                "median": round(statistics.median(windows_min), 1) if windows_min else None,
                "max": round(max(windows_min), 1) if windows_min else None,
            },
        },
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--norm", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    res = measure(a.norm / "db0ais.log")
    a.out.write_text(json.dumps(res, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
