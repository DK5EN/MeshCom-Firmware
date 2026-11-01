#!/usr/bin/env python3
"""Helper for the OE3XHL-12 report page: numbers that need the normalised raw logs.

Usage: python3 -I oe3xhl_measure.py --norm <dir with oe3x*.log> --out <data/extra/oe3xhl_measure.json>

Measures (stdlib only, streaming, one pass per file):
  * RX_ERROR share per OE3 node, denominator = all decoded receptions (ONRXDONE_TIME, which also
    fires for 12-byte ACK frames) + RX_ERROR; longest and median gap between two receptions
    (ONRXDONE_TIME or RX_ERROR, logger clock).
  * OE3XHL: CANCEL lines, distinct ids, relay decisions (RLY q=tx), CANCEL share.
  * Reception of OE3XHL-12 as last hop at the other four nodes ([LOG] lines and [NBR]|ME lines).
  * {CET} copies received by OE3XHL over RF: age = logger clock - 3600 s - content stamp (UTC);
    copies older than 150 s with their path.
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  ")
CET_RE = re.compile(
    r"\[LOG\].*? x(\w{8}) H(\d\d) .*? M01 ([^>]*)>\*:\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)"
)
LH_HL = re.compile(r"\[LOG\].* ([^ >]*)>")


def parse_ts(s: str) -> datetime:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f")


def scan(path: Path, node: str) -> dict[str, Any]:
    n_done = n_err = 0
    prev: datetime | None = None
    gaps: list[float] = []
    longest = (0.0, "")
    hl_last_hop = 0
    me_hl = 0
    cancel = 0
    cancel_ids: set[str] = set()
    rly_tx = 0
    cet_rows: list[tuple[float, str, str, str]] = []
    cet_n = 0
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TS.match(line)
            if not m:
                continue
            body = line[m.end():]
            if "ONRXDONE_TIME" in body or "RX_ERROR" in body:
                if "ONRXDONE_TIME" in body:
                    n_done += 1
                elif "[MC-DBG] RX_ERROR" in body:
                    n_err += 1
                else:
                    continue
                t = parse_ts(m.group(1))
                if prev is not None:
                    g = (t - prev).total_seconds()
                    gaps.append(g)
                    if g > longest[0]:
                        longest = (g, m.group(1))
                prev = t
                continue
            if node != "oe3xhl":
                if "[LOG]" in body and "OE3XHL-12>" in body:
                    # path element directly before '>' is the last hop
                    mm = re.search(r"([^, ]+)>", body)
                    if mm and mm.group(1) == "OE3XHL-12":
                        hl_last_hop += 1
                elif "[NBR]|ME|" in body and "OE3XHL-12" in body:
                    me_hl += 1
                continue
            if "[NBR]|CANCEL|" in body:
                cancel += 1
                parts = body.split("|")
                if len(parts) > 3:
                    cancel_ids.add(parts[3])
            elif "[LOG] RLY " in body and " q=tx " in body:
                rly_tx += 1
            elif "{CET}" in body and "[LOG]" in body:
                cm = CET_RE.search(body)
                if cm:
                    cet_n += 1
                    t = parse_ts(m.group(1)) - timedelta(hours=1)
                    c = datetime.strptime(cm.group(4), "%Y-%m-%d %H:%M:%S")
                    age = (t - c).total_seconds()
                    if age > 150:
                        cet_rows.append((age, m.group(1), cm.group(1), cm.group(3)))
    total = n_done + n_err
    out: dict[str, Any] = {
        "rx_decoded_onrxdone_time": n_done,
        "rx_error": n_err,
        "rx_error_share_pct": round(100.0 * n_err / total, 2) if total else 0.0,
        "gap_longest_s": round(longest[0], 1),
        "gap_longest_end": longest[1],
        "gap_median_s": round(statistics.median(gaps), 2) if gaps else 0.0,
        "gaps_over_300s": sum(1 for g in gaps if g > 300),
    }
    if node != "oe3xhl":
        out["hl_as_last_hop_log_lines"] = hl_last_hop
        out["hl_nbr_me_lines"] = me_hl
    else:
        out["cancel_lines"] = cancel
        out["cancel_distinct_ids"] = len(cancel_ids)
        out["rly_q_tx"] = rly_tx
        out["cancel_pct_of_rly_q_tx"] = round(100.0 * cancel / rly_tx, 2) if rly_tx else 0.0
        out["cet_rf_copies_parsed"] = cet_n
        out["cet_rf_older_150s_n"] = len(cet_rows)
        out["cet_rf_older_150s"] = [
            {"age_s": round(a, 1), "logger_time": t, "msg_id": i, "path": p}
            for a, t, i, p in sorted(cet_rows, key=lambda r: r[1])
        ]
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--norm", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    norm = Path(a.norm)
    res = {n: scan(norm / f"{n}.log", n) for n in NODES}
    doc = {"generated_by": "generators/lib/oe3xhl_measure.py", "nodes": res}
    Path(a.out).write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
