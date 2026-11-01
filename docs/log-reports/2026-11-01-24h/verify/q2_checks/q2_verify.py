#!/usr/bin/env python3
"""Independent Q2 (TX slot pool) verification over the normalised logs.

Usage: python3 -I q2_verify.py <norm-dir> [out.json]
Reads <norm-dir>/<node>.log for oe3xhl oe3xia oe3xir oe3xoc oe3xwj db0ais, streams line by line.
Prints a JSON summary (and writes it to out.json if given).
"""
from __future__ import annotations

import json
import re
import statistics
import sys
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Optional

NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj", "db0ais"]
TS_RE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  ")
W_RE = re.compile(r"RING_WRITE slot=(\d+) type=([0-9A-F]+) status=([0-9A-F]+) len=(\d+) msg_id=([0-9A-F]{8}) queued=(\d+)/20 src=(\w+)")
P_RE = re.compile(r"RING_PRIO slot=(\d+) prio=(\d)")
S_RE = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=(\d) old_id=([0-9A-F]{8}) new_id=([0-9A-F]{8})")
R_RE = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) type=([0-9A-F]+) status=\w+ len=(\d+) msg_id=([0-9A-F]{8}) retry=(\d+) queued=(\d+)/20 lat=(\d+)ms")
D_RE = re.compile(r"RING_DROP_STALE slot=(\d+) prio=(\d) age_s=(\d+) msg_id=([0-9A-F]{8}) reason=(\w+)")
RXLOG_RE = re.compile(r"\[LOG\] \d{3} .*RSSI:")
TX_RE = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) ")
C_RE = re.compile(r"\[NBR\]\|CANCEL\|\d+\|([0-9A-F]{8})\|")
ST_RE = re.compile(r"RING_STATUS queued=(\d+) pending=(\d+) retrying=(\d+) done=(\d+)")


@dataclass
class Entry:
    mid: str
    typ: str
    src: str
    t: Optional[float]
    line: int
    queued: int
    prio: int = 0
    fate: str = "pending"
    t_end: Optional[float] = None
    lat_ms: Optional[int] = None
    prev: Optional["Entry"] = None   # superseded predecessor (chain)
    read_t: Optional[float] = None
    stale_age: Optional[int] = None


def ts(line: str) -> Optional[float]:
    m = TS_RE.match(line)
    if not m:
        return None
    return datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp()


def pct(v: list[float], p: float) -> Optional[float]:
    if not v:
        return None
    s = sorted(v)
    k = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return round(s[k], 2)


def analyse(path: str) -> dict[str, Any]:
    entries: list[Entry] = []
    live: dict[str, list[Entry]] = defaultdict(list)   # msg_id -> live unread entries (GW-ACK pairs share an id)

    def lpop(mid: str) -> Optional[Entry]:
        lst = live.get(mid)
        if not lst:
            return None
        e = lst.pop(0)
        return e

    def lget(mid: str) -> Optional[Entry]:
        lst = live.get(mid)
        return lst[-1] if lst else None

    by_slot: dict[int, Entry] = {}       # last written entry per slot
    last_read: Optional[Entry] = None
    counts: Counter[str] = Counter()
    rx_t: list[float] = []
    txair_t: list[float] = []
    hold_fire_t: list[float] = []
    cadbusy_t: list[float] = []
    gate_t: list[float] = []
    status_retrying_nonzero = 0
    status_max = 0
    read_max = 0
    cancel_unmatched = 0
    decode_err_lines: list[str] = []
    prelog_reads = 0
    with open(path, "rb") as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.decode("latin-1").rstrip("\r\n")
            if "RING_" not in line and "[LOG] " not in line and "OnRxDone" not in line \
                    and "CANCEL" not in line and "Destination-CallSign" not in line \
                    and "RX_TIMEOUT_FIRE" not in line and "CAD_BUSY" not in line \
                    and "TX_GATE_ENTER" not in line:
                continue
            t = ts(line)
            if line.endswith("OnRxDone") and len(line) < 40:
                counts["onrxdone_lines"] += 1
                continue
            if RXLOG_RE.search(line):
                # every decoded reception ([LOG] NNN ... RSSI:) passes an OnRxDone exit that re-arms
                # the CSMA hold (lora_functions.cpp:993/1012/1034/1307/2746); ACK frames print no
                # "OnRxDone" line (handleACK early return), so these lines are the re-arm proxy.
                if t is not None:
                    rx_t.append(t)
                counts["rx_decoded"] += 1
                continue
            if "RX_TIMEOUT_FIRE" in line:
                if t is not None:
                    hold_fire_t.append(t)
                continue
            if "TX_GATE_ENTER" in line:
                if t is not None:
                    gate_t.append(t)
                continue
            if "CAD_BUSY" in line and "CAD_BUSY_1" not in line:
                if t is not None:
                    cadbusy_t.append(t)
                counts["cad_busy_confirmed"] += 1
                continue
            if "CAD_BUSY_1" in line:
                continue
            m = W_RE.search(line)
            if m:
                e = Entry(m.group(5), m.group(2), m.group(7), t, n, int(m.group(6)))
                entries.append(e)
                live[e.mid].append(e)
                by_slot[int(m.group(1))] = e
                counts["write"] += 1
                continue
            m = P_RE.search(line)
            if m:
                e2 = by_slot.get(int(m.group(1)))
                if e2 is not None and e2.prio == 0:
                    e2.prio = int(m.group(2))
                continue
            m = S_RE.search(line)
            if m:
                old = lpop(m.group(3))
                new = lget(m.group(4))
                counts["supersede"] += 1
                if old is not None:
                    old.fate, old.t_end = "superseded", t
                    if new is not None:
                        new.prev = old
                else:
                    counts["supersede_old_unknown"] += 1
                continue
            m = R_RE.search(line)
            if m:
                mid = m.group(5)
                e3 = lpop(mid)
                if e3 is None:
                    e3 = Entry(mid, m.group(3), "prelog", None, n, -1, int(m.group(2)))
                    e3.fate = "prelog"
                    prelog_reads += 1
                e3.read_t = t
                e3.lat_ms = int(m.group(8))
                if e3.fate != "prelog":
                    e3.fate = "read_noair"
                if int(m.group(6)) != 0:
                    counts["read_retry_nonzero"] += 1
                read_max = max(read_max, int(m.group(7)))
                last_read = e3
                counts["read"] += 1
                if e3.src == "prelog":
                    entries.append(e3)
                continue
            m = TX_RE.search(line)
            if m:
                if t is not None:
                    txair_t.append(t)
                counts["log_tx"] += 1
                if last_read is not None and last_read.mid == m.group(1):
                    if last_read.fate in ("read_noair", "prelog"):
                        last_read.fate = "aired" if last_read.fate == "read_noair" else "prelog_aired"
                    last_read.t_end = t
                else:
                    counts["log_tx_unmatched"] += 1
                continue
            if "Destination-CallSign Error" in line:
                counts["dest_err"] += 1
                decode_err_lines.append(line[:23])
                if last_read is not None and last_read.fate in ("read_noair", "prelog"):
                    last_read.fate = "decode_drop" if last_read.fate == "read_noair" else "prelog_decode_drop"
                    last_read.t_end = t
                continue
            m = D_RE.search(line)
            if m:
                counts["stale_" + m.group(5)] += 1
                e4 = lpop(m.group(4))
                if e4 is not None:
                    e4.fate, e4.t_end, e4.stale_age = "stale", t, int(m.group(3))
                else:
                    counts["stale_unmatched"] += 1
                continue
            m = C_RE.search(line)
            if m:
                counts["nbr_cancel"] += 1
                e5 = lpop(m.group(1))
                if e5 is not None:
                    e5.fate, e5.t_end = "cancelled", t
                else:
                    cancel_unmatched += 1
                continue
            m = ST_RE.search(line)
            if m:
                status_max = max(status_max, int(m.group(1)))
                if int(m.group(3)) != 0:
                    status_retrying_nonzero += 1
                continue
            for k in ("RING_DROP_NEW", "RING_DROP_PRIO", "RING_REARM", "RING_SUPERSEDE_KEEP"):
                if k in line:
                    counts[k] += 1
    return {
        "entries": entries, "counts": counts, "rx_t": rx_t, "txair_t": txair_t,
        "hold_fire_t": hold_fire_t, "cadbusy_t": cadbusy_t, "gate_t": gate_t,
        "status_retrying_nonzero": status_retrying_nonzero, "status_max": status_max,
        "read_queued_max": read_max, "cancel_unmatched": cancel_unmatched,
        "dest_err_first_last": (decode_err_lines[0], decode_err_lines[-1]) if decode_err_lines else None,
        "prelog_reads": prelog_reads,
    }


def count_in(ts_list: list[float], a: float, b: float) -> int:
    return bisect_left(ts_list, b) - bisect_right(ts_list, a)


def union_len(iv: list[tuple[float, float]]) -> float:
    iv = sorted(iv)
    tot, cs, ce = 0.0, None, None
    for s, e in iv:
        if cs is None or s > ce:
            if cs is not None:
                tot += ce - cs
            cs, ce = s, e
        else:
            ce = max(ce, e)
    if cs is not None:
        tot += ce - cs
    return tot


def head_analysis(R: dict[str, Any], targets: list[Entry]) -> dict[str, Any]:
    """For each target (stale tail entry), fraction of its last-link life during which no
    higher-precedence entry was live (strict prio pick, ignores NBR case-B hold), own TX count in
    the window, and RX/hold stats in the head segments."""
    ents = [e for e in R["entries"] if e.t is not None]
    rx, tx = R["rx_t"], R["txair_t"]
    out = []
    for tg in targets:
        if tg.t is None or tg.t_end is None:
            continue
        a, b = tg.t, tg.t_end
        block = []
        for e in ents:
            if e is tg:
                continue
            end = e.t_end if e.t_end is not None else (e.read_t if e.read_t is not None else b)
            if e.read_t is not None:
                end = min(end, e.read_t)
            if e.t >= b or end <= a:
                continue
            if e.prio < tg.prio or (e.prio == tg.prio and e.t < tg.t):
                block.append((max(a, e.t), min(b, end)))
        blocked = union_len(block)
        win = b - a
        # head segments = complement of blocked within [a,b]
        segs: list[tuple[float, float]] = []
        cur = a
        for s, e in sorted(block):
            if s > cur:
                segs.append((cur, s))
            cur = max(cur, e)
        if cur < b:
            segs.append((cur, b))
        rx_in_head = sum(count_in(rx, s, e) for s, e in segs)
        max_rx_gap_head = 0.0
        for s, e in segs:
            pts = [s] + rx[bisect_right(rx, s):bisect_left(rx, e)] + [e]
            for i in range(1, len(pts)):
                max_rx_gap_head = max(max_rx_gap_head, pts[i] - pts[i - 1])
        out.append({
            "gate_in_head": sum(count_in(R["gate_t"], s, e) for s, e in segs),
            "busy_in_head": sum(count_in(R["cadbusy_t"], s, e) for s, e in segs),
            "win_s": round(win, 1), "head_s": round(win - blocked, 1),
            "own_tx_in_win": count_in(tx, a, b), "rx_in_head": rx_in_head,
            "max_rx_gap_in_head_s": round(max_rx_gap_head, 1),
        })
    if not out:
        return {"n": 0}
    hs = [o["head_s"] for o in out]
    return {
        "n": len(out),
        "head_s_p50": pct(hs, 50), "head_s_p90": pct(hs, 90), "head_s_max": max(hs),
        "head_share_mean_pct": round(100 * sum(hs) / sum(o["win_s"] for o in out), 1),
        "own_tx_in_win_p50": pct([o["own_tx_in_win"] for o in out], 50),
        "own_tx_in_win_min": min(o["own_tx_in_win"] for o in out),
        "n_with_head_ge_30s": sum(1 for o in out if o["head_s"] >= 30),
        "n_head_ge_30s_and_rxgap_lt_5_8s": sum(1 for o in out if o["head_s"] >= 30 and o["max_rx_gap_in_head_s"] < 5.85),
        "head_s_total": round(sum(hs), 1),
        "gate_entries_in_head_total": sum(o["gate_in_head"] for o in out),
        "cad_busy_in_head_total": sum(o["busy_in_head"] for o in out),
        "n_head_ge_30s_zero_gate": sum(1 for o in out if o["head_s"] >= 30 and o["gate_in_head"] == 0),
        "max_rx_gap_in_head_p50": pct([o["max_rx_gap_in_head_s"] for o in out], 50),
    }


def chain_head(e: Entry) -> tuple[Entry, int]:
    n, cur = 1, e
    while cur.prev is not None:
        cur = cur.prev
        n += 1
    return cur, n


def node_report(R: dict[str, Any]) -> dict[str, Any]:
    E: list[Entry] = R["entries"]
    written = [e for e in E if e.src != "prelog"]
    fates = Counter(e.fate for e in written)
    tot = len(written)
    res: dict[str, Any] = {"counts": dict(R["counts"]), "writes": tot,
                           "fate_pct": {k: round(100 * v / tot, 2) for k, v in fates.items()},
                           "fate_n": dict(fates), "fate_sum_pct": round(sum(100 * v / tot for v in fates.values()), 2),
                           "status_retrying_nonzero": R["status_retrying_nonzero"],
                           "queued_max": {"write": max(e.queued for e in written), "read": R["read_queued_max"],
                                          "status": R["status_max"]},
                           "cancel_unmatched": R["cancel_unmatched"], "prelog_reads": R["prelog_reads"],
                           "dest_err_first_last": R["dest_err_first_last"]}
    # hourly mean queued at write, slope
    hours: dict[int, list[int]] = defaultdict(list)
    t0 = next((e.t for e in written if e.t is not None), None)
    if t0 is not None:
        for e in written:
            if e.t is not None:
                hours[int((e.t - t0) // 3600)].append(e.queued)
        xs = sorted(hours)
        ys = [statistics.mean(hours[x]) for x in xs]
        if len(xs) > 2:
            mx, my = statistics.mean(xs), statistics.mean(ys)
            slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
            res["hourly_mean_queued_slope_per_h"] = round(slope, 4)
            res["hourly_mean_queued_first_last3"] = [round(y, 2) for y in ys[:3]] + [round(y, 2) for y in ys[-3:]]
    # own HEY chains (type 40 auto_pos)
    for label, typ, src in (("own_hey", "40", "auto_pos"), ("own_wx", "21", "user_wx")):
        tails = [e for e in written if e.typ == typ and e.src == src and e.fate != "superseded"]
        fc = Counter(e.fate for e in tails)
        spans = []
        reset_evidence = 0
        for e in tails:
            h, n = chain_head(e)
            end = e.t_end if e.t_end is not None else e.read_t
            if h.t is not None and end is not None:
                spans.append(end - h.t)
                if n > 1 and end - h.t > 180 and e.fate in ("aired", "stale"):
                    reset_evidence += 1
        stale_tails = [e for e in tails if e.fate == "stale"]
        d: dict[str, Any] = {"episodes": len(tails), "fate": dict(fc),
                             "aired_pct": round(100 * fc.get("aired", 0) / len(tails), 1) if tails else None,
                             "chain_span_max_s": round(max(spans), 1) if spans else None,
                             "chains_span_gt180_not_dropped_at_180": reset_evidence,
                             "stale_age_s_values": dict(Counter(e.stale_age for e in stale_tails)),
                             "stale_last_link_age_s_max": round(max((e.t_end - e.t for e in stale_tails if e.t and e.t_end), default=0), 1)}
        if label == "own_hey":
            aired_t = sorted(e.read_t for e in tails if e.fate == "aired" and e.read_t is not None)
            gaps = [(aired_t[i] - aired_t[i - 1]) / 60 for i in range(1, len(aired_t))]
            d["aired_gap_min_p50_max"] = (pct(gaps, 50), round(max(gaps), 1) if gaps else None)
        d["head_analysis_stale"] = head_analysis(R, stale_tails)
        res[label] = d
    # prio-5 / prio-4 all stale: head analysis
    res["stale_all_head"] = head_analysis(R, [e for e in written if e.fate == "stale"])
    # ACK waits (type 41 aired)
    acks = [e for e in written if e.typ == "41" and e.fate == "aired" and e.lat_ms is not None]
    waits = [e.lat_ms / 1000 for e in acks]
    a: dict[str, Any] = {"n": len(acks), "p50": pct(waits, 50), "p90": pct(waits, 90),
                         "max": round(max(waits), 1) if waits else None,
                         "share_ge20_pct": round(100 * sum(1 for w in waits if w >= 20) / len(waits), 1) if waits else None}
    if waits:
        hist = Counter(int(w // 2) * 2 for w in waits if 10 <= w < 30)
        a["hist_2s_10_30"] = {str(k): hist[k] for k in sorted(hist)}
        q0 = [e.lat_ms / 1000 for e in acks if e.queued == 0]
        q1 = [e.lat_ms / 1000 for e in acks if e.queued == 1]
        a["p50_queued0"], a["n_queued0"] = pct(q0, 50), len(q0)
        a["p50_queued1"], a["n_queued1"] = pct(q1, 50), len(q1)
        # own TX and RX during wait, for waits >= 20 s
        long_ = [e for e in acks if e.lat_ms >= 20000 and e.t is not None and e.read_t is not None]
        if long_:
            txc = [count_in(R["txair_t"], e.t, e.read_t) for e in long_]
            rxc = [count_in(R["rx_t"], e.t, e.read_t) for e in long_]
            fire = [count_in(R["hold_fire_t"], e.t, e.read_t) for e in long_]
            busy = [count_in(R["cadbusy_t"], e.t, e.read_t) for e in long_]
            a["ge20"] = {"n": len(long_), "own_tx_during_p50": pct(txc, 50), "own_tx_during_p90": pct(txc, 90),
                         "own_tx_during_zero": sum(1 for c in txc if c == 0),
                         "rx_during_p50": pct(rxc, 50), "hold_expiries_p50": pct(fire, 50),
                         "cad_busy_p50": pct(busy, 50)}
            # ahead-of-it frames: own TX in wait whose entry was enqueued before this ACK
            short = [e for e in acks if e.lat_ms < 20000 and e.t is not None and e.read_t is not None]
            a["lt20"] = {"n": len(short),
                         "own_tx_during_p50": pct([count_in(R["txair_t"], e.t, e.read_t) for e in short], 50),
                         "rx_during_p50": pct([count_in(R["rx_t"], e.t, e.read_t) for e in short], 50)}
    res["ack_wait"] = a
    # decode drops / phone_msg
    pm = [e for e in E if e.src == "phone_msg"]
    if pm:
        res["phone_msg"] = {"writes": len(pm), "fate": dict(Counter(e.fate for e in pm))}
    prelog = Counter(e.fate for e in E if e.src == "prelog")
    res["prelog_fate"] = dict(prelog)
    res["supersede_by_type"] = dict(Counter(f"{e.typ}/{e.src}" for e in written if e.fate == "superseded"))
    return res


def main() -> None:
    d = sys.argv[1]
    out: dict[str, Any] = {}
    for node in NODES:
        R = analyse(f"{d}/{node}.log")
        out[node] = node_report(R)
    s = json.dumps(out, indent=1, default=str)
    print(s)
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w") as fh:
            fh.write(s)


if __name__ == "__main__":
    main()
