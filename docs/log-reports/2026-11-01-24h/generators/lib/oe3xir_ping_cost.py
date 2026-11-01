"""Helper for the OE3XIR-12 page: cost of the node's own auto-ping to "255." and raw-log counts.

Reads the normalised log of OE3XIR-12 and writes data/extra/oe3xir_ping_cost.json:
CAD gate entries per ring read (dropped ping vs real transmission), queue latency of the pings and
the raw-log counters the page quotes (ping errors, CAD_FREE_NO_TX, TX_FRAME / TX_START, ACK frames,
longest {CET} buffer wait).

Usage (from the repo root):
    python3 -I docs/log-reports/<run>/generators/lib/oe3xir_ping_cost.py \\
        --norm logs/<capture>/.logreport-work/norm --out docs/log-reports/<run>/data/extra/oe3xir_ping_cost.json
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
from datetime import datetime
from pathlib import Path

RE_LAT = re.compile(r"lat=(\d+)ms")
RE_CET_WAIT = re.compile(r"\[CET\];tx;age_s;(\d+);.*?wait_ms;(\d+)")
RE_STRONG = re.compile(r"RX_ERROR rssi=(-?\d+)")
RE_TYPE = re.compile(r"type=([0-9A-F]{2})")
DEDUP_SLOTS = 100  # RX dedup ring size (slot=n/100 in RX_DEDUP_ADD)


def ts(line: str) -> float:
    """Logger wall time of a normalised line in seconds (naive; only differences are used)."""
    return datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f").timestamp()


def summ(x: list[int]) -> dict[str, float | int]:
    xs = sorted(x)
    return {
        "n": len(xs),
        "sum": sum(xs),
        "mean": round(statistics.mean(xs), 2),
        "p50": xs[len(xs) // 2],
        "p90": xs[int(len(xs) * 0.9)],
        "max": xs[-1],
    }


def measure(log: Path) -> dict[str, object]:
    gates_since_read = 0
    last_read_gates: int | None = None
    last_read_waiting = False
    cur_lat = -1
    ping_gates: list[int] = []
    tx_gates: list[int] = []
    ping_lat: list[int] = []
    cnt = {"ping_errors": 0, "cad_free_no_tx": 0, "tx_frame": 0, "tx_start": 0,
           "ack_frames": 0, "ack_frames_f5ce7": 0, "rx_error_strong": 0, "rx_error": 0,
           "rx_decoded_incl_ack": 0}
    ack_lat: list[int] = []
    dedup_first = dedup_last = 0.0
    dedup_adds = 0
    rx_prev = 0.0
    rx_gap_max = 0.0
    rx_gap_at = ""
    cet_wait_max_ms = 0
    cet_age_max_s = 0
    first_ping = ""
    last_ping = ""
    with log.open("rb") as fh:
        for raw in fh:
            line = raw.decode("utf-8", "replace")
            if "TX_GATE_ENTER" in line:
                gates_since_read += 1
            elif "RING_TX_READ" in line:
                if last_read_waiting and last_read_gates is not None:
                    tx_gates.append(last_read_gates)
                last_read_gates = gates_since_read
                last_read_waiting = True
                gates_since_read = 0
                m = RE_LAT.search(line)
                cur_lat = int(m.group(1)) if m else -1
                mt = RE_TYPE.search(line)
                if mt and mt.group(1) == "41" and cur_lat >= 0:
                    ack_lat.append(cur_lat)
            elif "Destination-CallSign Error [255.]" in line:
                cnt["ping_errors"] += 1
                stamp = line[:23]
                first_ping = first_ping or stamp
                last_ping = stamp
                if last_read_waiting:
                    ping_gates.append(last_read_gates or 0)
                    ping_lat.append(cur_lat)
                    last_read_waiting = False
            elif "CAD_FREE_NO_TX" in line:
                cnt["cad_free_no_tx"] += 1
            elif "[MC-TEST] TX_FRAME" in line:
                cnt["tx_frame"] += 1
            elif "[MC-DBG] TX_START" in line:
                cnt["tx_start"] += 1
            elif "[LOG] 012 A " in line:
                cnt["ack_frames"] += 1
                if " xF5CE7" in line:
                    cnt["ack_frames_f5ce7"] += 1
            elif "ONRXDONE_TIME" in line:
                cnt["rx_decoded_incl_ack"] += 1
                t = ts(line)
                if rx_prev and t - rx_prev > rx_gap_max:
                    rx_gap_max = t - rx_prev
                    rx_gap_at = line[:19]
                rx_prev = t
            elif "RX_DEDUP_ADD" in line:
                dedup_adds += 1
                dedup_last = ts(line)
                dedup_first = dedup_first or dedup_last
            elif "RX_ERROR" in line:
                cnt["rx_error"] += 1
                m = RE_STRONG.search(line)
                if m and int(m.group(1)) > -60:
                    cnt["rx_error_strong"] += 1
            elif "[CET];tx;" in line:
                m = RE_CET_WAIT.search(line)
                if m:
                    cet_age_max_s = max(cet_age_max_s, int(m.group(1)))
                    cet_wait_max_ms = max(cet_wait_max_ms, int(m.group(2)))
    if last_read_waiting and last_read_gates is not None:
        tx_gates.append(last_read_gates)
    ping = summ(ping_gates)
    real = summ(tx_gates)
    total = int(ping["sum"]) + int(real["sum"])
    ls = sorted(ping_lat)
    al = sorted(ack_lat)
    rx_total = cnt["rx_error"] + cnt["rx_decoded_incl_ack"]
    dedup_per_min = dedup_adds / ((dedup_last - dedup_first) / 60.0)
    return {
        "ping_gate_entries_per_read": ping,
        "tx_gate_entries_per_read": real,
        "gate_entries_total": total,
        "ping_share_of_gate_entries_pct": round(100.0 * int(ping["sum"]) / total, 1),
        "ping_lat_ms_p50": ls[len(ls) // 2],
        "ping_lat_ms_max": ls[-1],
        "ping_first": first_ping,
        "ping_last": last_ping,
        "counts": cnt,
        "ack_wait_s": {
            "n": len(al),
            "p50": round(al[len(al) // 2] / 1000, 1),
            "p90": round(al[int(len(al) * 0.9)] / 1000, 1),
            "max": round(al[-1] / 1000, 1),
            "share_ge_20s_pct": round(100.0 * sum(1 for v in al if v >= 20000) / len(al), 1),
        },
        "rx_error_share_incl_ack_pct": round(100.0 * cnt["rx_error"] / rx_total, 1),
        "rx_longest_silence_s": round(rx_gap_max, 1),
        "rx_longest_silence_at": rx_gap_at,
        "dedup_window_min": round(DEDUP_SLOTS / dedup_per_min, 1),
        "cet_tx_wait_ms_max": cet_wait_max_ms,
        "cet_tx_age_s_max_node": cet_age_max_s,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--norm", required=True, help="directory with the normalised logs (oe3xir.log)")
    ap.add_argument("--out", required=True, help="output JSON (data/extra/oe3xir_ping_cost.json)")
    a = ap.parse_args()
    res = measure(Path(a.norm) / "oe3xir.log")
    Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(res))


if __name__ == "__main__":
    main()
