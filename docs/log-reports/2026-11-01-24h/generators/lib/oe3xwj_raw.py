"""Raw-log pass for the OE3XWJ-12 page (nothing of this runs at render time).

Reads the normalised logs of the OE3 cluster and writes five files into --out (data/extra):

    oe3xwj_ping.json       own {ping} to 2025: ACK frames per ping (at WJ and across the five logs),
                           first-ACK latency, ACK format (gateway byte, server bit)
    oe3xwj_stall.json      the longest receive gap of OE3XWJ-12 and what happened inside it
    oe3xwj_cet_stale.json  {CET} copies older than 1000 s received by WJ and what the gate did
    oe3xwj_rerelay.json    own relay transmissions ([LOG] TX src=r) of one id with a long gap (dedup expiry) and the dedup window
    oe3xwj_rxerror.json    RX_ERROR share with the corrected denominator (ACK receptions included)

Usage (from the repo root):
    python3 -I docs/log-reports/<run>/generators/lib/oe3xwj_raw.py \\
        --norm logs/<capture>/.logreport-work/norm --out docs/log-reports/<run>/data/extra

Stdlib only. Logger clock = the line prefix `YYYY-MM-DD HH:MM:SS.mmm`.
"""

from __future__ import annotations

import argparse
import bisect
import json
import re
import statistics
from collections import defaultdict
from datetime import datetime
from pathlib import Path

NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
WJ = "oe3xwj"
PING_RE = re.compile(r"TX-LoRa .*? x([0-9A-F]{8}) .*OE3XWJ-12>2025:\{ping\}")
ACK_RE = re.compile(r"\[LOG\] 012 A x([0-9A-F]{8}) H([0-9A-F]{2}) x([0-9A-F]{8}) ([0-9A-F]{2}) ")
CET_LOG_RE = re.compile(
    r"\[LOG\] \d{3} : x([0-9A-F]{8}) H(\w\w) .*? (\S+)>\*:\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)"
)
CET_RX_RE = re.compile(r"\[CET\];rx;src;(\S+?);age_s;(\d+);")
RLY_RE = re.compile(r"\[LOG\] RLY x([0-9A-F]{8}) (\S) H(\w\w) q=(\w+)")
TX_RE = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) (\S) H(\w\w) prio=(\d) src=r ")
QUEUED_RE = re.compile(r"RING_STATUS queued=(\d+)")
STALE_S = 1000


def ts(line: str) -> float:
    return datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f").timestamp()


def clock(t: float) -> str:
    return datetime.fromtimestamp(t).strftime("%Y-%m-%d %H:%M:%S")


def lines(path: Path):
    with path.open("rb") as fh:
        for n, raw in enumerate(fh, 1):
            yield n, raw.decode("latin-1", "replace")


def dist(xs: list[float], nd: int = 1) -> dict[str, float | int]:
    s = sorted(xs)
    if not s:
        return {"n": 0}
    return {
        "n": len(s),
        "mean": round(statistics.mean(s), nd),
        "p50": round(statistics.median(s), nd),
        "p90": round(s[min(len(s) - 1, int(0.9 * len(s)))], nd),
        "max": round(s[-1], nd),
    }


# ----------------------------------------------------------------------------------------------
def ping_pass(norm: Path) -> dict:
    pings: list[tuple[float, str]] = []
    for _, s in lines(norm / f"{WJ}.log"):
        if "{ping}" in s and "TX-LoRa" in s:
            m = PING_RE.search(s)
            if m:
                pings.append((ts(s), m.group(1)))
    ids = {pid for _, pid in pings}
    by_id_t: dict[str, list[float]] = defaultdict(list)
    for t, pid in pings:
        by_id_t[pid].append(t)

    def is_ping_ack(acked: str, t: float) -> bool:
        return any(-5 <= t - x <= 1800 for x in by_id_t.get(acked, []))

    acks: dict[str, list[tuple[float, str, str, str]]] = defaultdict(list)  # acked -> (t, ack, hop, gwbyte)
    wj_acks: dict[str, list[tuple[float, str, str]]] = defaultdict(list)
    per_node_ack_rx: dict[str, int] = {}
    for node in NODES:
        n_ack = 0
        for _, s in lines(norm / f"{node}.log"):
            if " 012 A x" not in s or "FCS:0000" in s:
                continue
            m = ACK_RE.search(s)
            if not m or m.group(3) not in ids:
                continue
            t = ts(s)
            if not is_ping_ack(m.group(3), t):
                continue
            n_ack += 1
            acks[m.group(3)].append((t, m.group(1), m.group(2), m.group(4)))
            if node == WJ and m.group(3).startswith("F5CE7"):
                wj_acks[m.group(3)].append((t, m.group(1), m.group(2)))
        per_node_ack_rx[node] = n_ack

    # cluster, per acked id (as q6 verification): distinct ACK ids per acked ping id
    per_id = [len({a[1] for a in v}) for v in acks.values()]
    # cluster, per ping event: distinct ACK ids in [t-5, t+1800]
    per_event = []
    for t, pid in pings:
        per_event.append(len({a[1] for a in acks.get(pid, []) if -5 <= a[0] - t <= 1800}))
    gw = srv = tot = 0
    for v in acks.values():
        for _, _, hop, gwb in v:
            tot += 1
            gw += 1 if gwb == "01" else 0
            srv += 1 if int(hop, 16) & 0x80 else 0

    # at WJ only, 600 s window
    frames: list[int] = []
    recv: list[int] = []
    first: list[float] = []
    h84 = 0
    for t, pid in pings:
        a = [x for x in wj_acks.get(pid, []) if 0 <= x[0] - t <= 600]
        frames.append(len({x[1] for x in a}))
        recv.append(len(a))
        if a:
            f0 = min(a)
            first.append(f0[0] - t)
            h84 += 1 if f0[2] == "84" else 0
    return {
        "pings": len(pings),
        "distinct_ping_ids": len(ids),
        "first_ping": clock(pings[0][0]),
        "last_ping": clock(pings[-1][0]),
        "cluster_by_acked_id": {
            "ids_with_ack": len(per_id),
            "ack_ids_per_ping_mean": round(statistics.mean(per_id), 2),
            "median": statistics.median(per_id),
            "max": max(per_id),
        },
        "cluster_by_ping_event": {
            "pings_with_ack": sum(1 for x in per_event if x),
            "ack_ids_per_ping_mean": round(statistics.mean(per_event), 2),
            "median": statistics.median(per_event),
            "max": max(per_event),
        },
        "ack_format": {
            "receptions": tot,
            "gateway_byte_01": gw,
            "server_bit_set": srv,
        },
        "ack_receptions_per_node": per_node_ack_rx,
        "at_wj": {
            "with_ack": sum(1 for r in recv if r),
            "frames_mean": round(statistics.mean(frames), 2),
            "frames_median": statistics.median(frames),
            "frames_max": max(frames),
            "recv_mean": round(statistics.mean(recv), 1),
            "recv_median": statistics.median(recv),
            "first_ack_s": dist(first),
            "first_ack_hop4": h84,
        },
    }


# ----------------------------------------------------------------------------------------------
def rx_events(path: Path) -> list[float]:
    out: list[float] = []
    for _, s in lines(path):
        if "ONRXDONE_TIME" in s or "RX_ERROR" in s:
            out.append(ts(s))
    return out


def stall_pass(norm: Path) -> dict:
    ev = {n: rx_events(norm / f"{n}.log") for n in NODES}
    wj = ev[WJ]
    gaps = sorted(((b - a, a, b) for a, b in zip(wj, wj[1:])), reverse=True)
    g, a, b = gaps[0]
    second = gaps[1][0]
    inside = {"cad_safety_timeout": 0, "radio_tx": 0, "eth_stall": 0, "rx_restart_timeout": 0}
    queued: list[int] = []
    total_safety = 0
    last_status = None
    for _, s in lines(norm / f"{WJ}.log"):
        if "CAD_SAFETY_TIMEOUT" in s:
            total_safety += 1
        if not ("MC-DBG" in s or "[ETH]" in s):
            continue
        t = ts(s)
        if t < a - 60 or t > b + 5:
            continue
        in_gap = a < t < b
        if in_gap and "CAD_SAFETY_TIMEOUT" in s:
            inside["cad_safety_timeout"] += 1
        elif in_gap and "RADIO_TX" in s:
            inside["radio_tx"] += 1
        elif in_gap and "[ETH];stall;" in s:
            inside["eth_stall"] += 1
        elif in_gap and "RX_RESTART src=timeout" in s:
            inside["rx_restart_timeout"] += 1
        m = QUEUED_RE.search(s)
        if m:
            if t <= a:
                last_status = int(m.group(1))
            elif in_gap:
                queued.append(int(m.group(1)))
    others = {}
    for n in NODES:
        if n == WJ:
            continue
        e = ev[n]
        others[n] = bisect.bisect_right(e, b) - bisect.bisect_right(e, a)
    return {
        "last_rx": clock(a) + f".{int((a % 1) * 1000):03d}",
        "next_rx": clock(b) + f".{int((b % 1) * 1000):03d}",
        "gap_s": round(g, 1),
        "second_longest_gap_s": round(second, 1),
        "inside_wj": inside,
        "cad_safety_timeout_total": total_safety,
        "queue_before_gap": last_status,
        "queue_in_gap_max": max(queued) if queued else None,
        "queue_in_gap_last": queued[-1] if queued else None,
        "receptions_at_others_in_gap": others,
        "rx_event_definition": "ONRXDONE_TIME or RX_ERROR line (ACK receptions included)",
    }


# ----------------------------------------------------------------------------------------------
def cet_pass(norm: Path) -> dict:
    rows: list[dict] = []
    last_log: dict | None = None
    pending: dict | None = None
    total_rx = 0
    for n, s in lines(norm / f"{WJ}.log"):
        if "{CET}" in s and "[LOG] " in s and " : x" in s:
            m = CET_LOG_RE.search(s)
            if m:
                last_log = {"id": m.group(1), "hop": int(m.group(2), 16) & 0x0F, "path": m.group(3).split(","),
                            "stamp_utc": m.group(4)}
            continue
        if "[CET];rx;" in s:
            total_rx += 1
            m = CET_RX_RE.search(s)
            if m and last_log is not None:
                pending = {"line": n, "logger": s[:23], "age_s": int(m.group(2)), "outcome": None,
                           "drop_line": None, **last_log}
                rows.append(pending)
            continue
        if pending is not None and "[CET];drop;where;relay" in s:
            pending["drop_line"] = n
            continue
        if pending is not None and "[LOG] RLY x" in s:
            m = RLY_RE.search(s)
            if m and m.group(1) == pending["id"]:
                pending["outcome"] = m.group(4)
                pending = None
    stale = [r for r in rows if r["age_s"] > STALE_S]
    out: dict[str, int] = defaultdict(int)
    for r in stale:
        out[r["outcome"] or "unknown"] += 1
    ages = [r["age_s"] for r in stale]
    return {
        "cet_rx_lines": total_rx,
        "stale_threshold_s": STALE_S,
        "stale_n": len(stale),
        "age_min_s": min(ages),
        "age_max_s": max(ages),
        "outcome": dict(out),
        "via_oe3mtc10_in_path": sum(1 for r in stale if "OE3MTC-10" in r["path"]),
        "oe3mtc10_last_station": sum(1 for r in stale if r["path"][-1] == "OE3MTC-10"),
        "rows": stale,
    }


# ----------------------------------------------------------------------------------------------
def rerelay_pass(norm: Path) -> dict:
    times: dict[str, list[tuple[float, str, str]]] = defaultdict(list)
    adds = 0
    first = last = None
    for _, s in lines(norm / f"{WJ}.log"):
        if first is None and s[:2] == "20":
            first = ts(s)
        if "RX_DEDUP_ADD" in s:
            adds += 1
        elif "[LOG] TX x" in s and " src=r " in s:
            m = TX_RE.search(s)
            if m:
                times[m.group(1)].append((ts(s), m.group(2), s[:23]))
    last = ts(s)
    span_min = (last - first) / 60
    window_min = 100 / (adds / span_min)
    longs = []
    for pid, v in times.items():
        for (t0, typ, l0), (t1, _, l1) in zip(v, v[1:]):
            if t1 - t0 >= 600:
                longs.append({"id": pid, "type": typ, "first": l0, "again": l1, "gap_s": round(t1 - t0)})
    longs.sort(key=lambda r: r["first"])
    return {
        "rx_dedup_add": adds,
        "span_min": round(span_min, 1),
        "dedup_slots": 100,
        "dedup_window_min": round(window_min, 1),
        "long_gap_relays": longs,
        "all_exceed_window": all(r["gap_s"] / 60 > window_min for r in longs),
    }


def rxerror_pass(norm: Path) -> dict:
    err = ok = 0
    for _, s in lines(norm / f"{WJ}.log"):
        if "RX_ERROR" in s:
            err += 1
        elif "ONRXDONE_TIME" in s:
            ok += 1
    return {
        "rx_error": err,
        "rx_done_incl_ack": ok,
        "share_pct": round(100 * err / (err + ok), 1),
        "definition": "RX_ERROR / (RX_ERROR + ONRXDONE_TIME); ONRXDONE_TIME is printed for every decoded "
        "reception including 12-byte ACKs",
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    jobs = {
        "oe3xwj_ping.json": ping_pass,
        "oe3xwj_stall.json": stall_pass,
        "oe3xwj_cet_stale.json": cet_pass,
        "oe3xwj_rerelay.json": rerelay_pass,
        "oe3xwj_rxerror.json": rxerror_pass,
    }
    for name, fn in jobs.items():
        (a.out / name).write_text(json.dumps(fn(a.norm), indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        print("wrote", a.out / name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
