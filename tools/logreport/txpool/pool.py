#!/usr/bin/env python3
"""TX pool (TXP) analysis of one node log.

Streams the log, builds a per-slot state machine from RING_* markers and
returns occupancy, latency, fate accounting, dup-TX, supersede (see summarise()).
analyse() takes `clock=None` for logs with a logger wall-clock prefix
(`YYYY-MM-DD HH:MM:SS.mmm  `), or a clock profile dict for captures without
one (time then comes from the `[LOG] HH:MM:SS` node clock):
  base_date        "YYYY-MM-DD" of the capture start (default 2000-01-01)
  start_clock      "HH:MM:SS" true time at capture start (default 00:00:00)
  first_valid_line first line number whose node clock is already set (default 1)
  step_back_line   line number where a node-clock step happened (default 0 = none)
  step_back_s      seconds the node clock ran ahead before that line (default 0)
"""
from __future__ import annotations

import json
import re
from collections import Counter, defaultdict
from datetime import datetime, timezone
from typing import Any

RE_WRITE = re.compile(r"RING_WRITE slot=(-?\d+) type=([0-9A-F]{2}) status=([0-9A-F]{2}) len=(\d+) msg_id=([0-9A-F]{8}) queued=(\d+)/20 src=(\S+)")
RE_PRIO = re.compile(r"RING_PRIO slot=(-?\d+) prio=(\d)")
RE_READ = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) type=([0-9A-F]{2}) status=([0-9A-F]{2}) len=(\d+) msg_id=([0-9A-F]{8}) retry=(\d+) queued=(\d+)/20 lat=(\d+)ms")
RE_STATUS = re.compile(r"RING_STATUS queued=(\d+) pending=(\d+) retrying=(\d+) done=(\d+)")
RE_SUP = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=(\d) old_id=([0-9A-F]{8}) new_id=([0-9A-F]{8})")
RE_STALE = re.compile(r"RING_DROP_STALE slot=(\d+) prio=(\d) age_s=(\d+) msg_id=([0-9A-F]{8}) reason=(\w+)")
RE_DPRIO = re.compile(r"RING_DROP_PRIO slot=(-?\d+) prio=(\d) type=([0-9A-F]{2}) msg_id=([0-9A-F]{8}) replaced_by_prio=(\d) src=(\S+)")
RE_DNEW = re.compile(r"RING_DROP_NEW slot=(-?\d+) prio=(\d) type=([0-9A-F]{2}) msg_id=([0-9A-F]{8})")
RE_GATE = re.compile(r"TX_GATE_ENTER qlen=(\d+) cad_attempt=(\d+)")
RE_LOGTX = re.compile(r"(\d\d):(\d\d):(\d\d) \[LOG\] TX x([0-9A-F]{8}) (\S) H([0-9A-F]{2}) prio=(\d) src=(\w) wait=(\d+) q=(\d+) cad=(\d+) len=(\d+)")
RE_RLY = re.compile(r"\[LOG\] RLY x([0-9A-F]{8}) (\S) H([0-9A-F]{2}) q=(\w+) prio=(\d) slot=(-?\d+)")
RE_CANCEL = re.compile(r"\[NBR\]\|CANCEL\|(\d+)\|([0-9A-F]{8})\|(\S)\|")
RE_UTIL = re.compile(r"CHANNEL_UTIL rx=(\d+)ms tx=(\d+)ms util=(\d+)%")
RE_STAT = re.compile(r"\[LOG\] STAT util=(\d+) .*?txn=(\d+) txfail=(\d+) ringmax=(\d+)/20 drop=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)")
RE_CLOCK = re.compile(r"^(\d\d):(\d\d):(\d\d) \[LOG\]")
RE_RETX = re.compile(r"RING_REARM|ACK_SKIP_READY|RING_REJECT")

_TS_CACHE: dict[str, float] = {}


def ts_prefix(line: str) -> float | None:
    k = line[:19]
    v = _TS_CACHE.get(k)
    if v is None:
        try:
            v = datetime.strptime(k, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp()
        except ValueError:
            return None
        _TS_CACHE[k] = v
    try:
        return v + int(line[20:23]) / 1000.0
    except ValueError:
        return v


def pct(vals: list[float], p: float) -> float | None:
    if not vals:
        return None
    s = sorted(vals)
    if len(s) == 1:
        return float(s[0])
    k = (len(s) - 1) * p / 100.0
    f = int(k)
    c = min(f + 1, len(s) - 1)
    return float(s[f] + (s[c] - s[f]) * (k - f))


def dist(vals: list[float]) -> dict[str, Any]:
    if not vals:
        return {"n": 0}
    return {"n": len(vals), "p50": pct(vals, 50), "p90": pct(vals, 90), "p99": pct(vals, 99),
            "max": float(max(vals)), "mean": sum(vals) / len(vals)}


def spearman(x: list[float], y: list[float]) -> float | None:
    n = len(x)
    if n < 10:
        return None

    def ranks(a: list[float]) -> list[float]:
        idx = sorted(range(len(a)), key=lambda i: a[i])
        r = [0.0] * len(a)
        i = 0
        while i < len(a):
            j = i
            while j + 1 < len(a) and a[idx[j + 1]] == a[idx[i]]:
                j += 1
            avg = (i + j) / 2.0 + 1
            for k in range(i, j + 1):
                r[idx[k]] = avg
            i = j + 1
        return r
    rx, ry = ranks(x), ranks(y)
    mx, my = sum(rx) / n, sum(ry) / n
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    dx = sum((a - mx) ** 2 for a in rx) ** 0.5
    dy = sum((b - my) ** 2 for b in ry) ** 0.5
    return num / (dx * dy) if dx and dy else None


class Entry:
    __slots__ = ("id", "slot", "ts", "line", "type", "status", "src", "prio", "wq", "fate", "read_ts",
                 "lat", "air", "decodefail", "reads", "retry", "sup_by", "closed_ts", "log_cad", "util",
                 "txq", "sup_old", "fate_ts")

    def __init__(self, mid: str, slot: int, ts: float, line: int, typ: str, status: str, src: str, wq: int):
        self.id, self.slot, self.ts, self.line, self.type, self.status, self.src, self.wq = mid, slot, ts, line, typ, status, src, wq
        self.prio = 0
        self.fate: str | None = None
        self.read_ts: float | None = None
        self.lat: int | None = None
        self.air = False
        self.decodefail = False
        self.reads = 0
        self.retry = 0
        self.sup_by: Entry | None = None
        self.closed_ts: float | None = None
        self.log_cad: int | None = None
        self.util: int | None = None
        self.txq: int | None = None
        self.sup_old = None
        self.fate_ts: float | None = None


def analyse(node: str, path: str, clock: dict[str, Any] | None = None) -> dict[str, Any]:
    is_db = clock is not None
    cfg = clock or {}
    first_valid = int(cfg.get("first_valid_line", 1))
    step_line = int(cfg.get("step_back_line", 0))
    step_s = int(cfg.get("step_back_s", 0))
    sc = [int(x) for x in str(cfg.get("start_clock", "00:00:00")).split(":")]
    slots: list[Entry | None] = [None] * 20
    entries: list[Entry] = []
    limbo: Entry | None = None
    last_write: Entry | None = None
    day = 0
    prev_clock_s = sc[0] * 3600 + sc[1] * 60 + sc[2] if is_db else 0
    cur_t = 0.0
    base_db = datetime.strptime(str(cfg.get("base_date", "2000-01-01")), "%Y-%m-%d").replace(tzinfo=timezone.utc).timestamp()
    last_util = None
    util_samples: list[tuple[float, int]] = []
    status_samples: list[tuple[float, int, int, int, int]] = []
    gate_qlen: list[int] = []
    gate_cad: list[int] = []
    stat_rows: list[dict[str, Any]] = []
    txlines: list[tuple[float, str, str, str, int, int, int, int]] = []  # t,id,type,src,prio,wait,cad,q
    clock_off: list[float] = []
    rly = Counter()
    rly_by_prio = Counter()
    unmatched_reads = 0
    open_by_id: dict[str, Entry] = {}
    pend_read: Entry | None = None
    sup_events: list[dict[str, Any]] = []
    stale_events: list[dict[str, Any]] = []
    other_drop: list[dict[str, Any]] = []
    marker_counts = Counter()
    cancel_events: list[dict[str, Any]] = []
    unexplained: list[dict[str, Any]] = []
    cad_busy_win = 0
    first_t = None
    last_t = 0.0
    write_neg = 0

    def settle_pend() -> None:
        nonlocal pend_read
        if pend_read is not None:
            e = pend_read
            if not e.air:
                e.fate = "tx_noair_decodefail" if e.decodefail else "tx_noair_other"
            else:
                e.fate = "tx_air"
            pend_read = None

    def flush_limbo(t: float, line_no: int) -> None:
        nonlocal limbo
        if limbo is not None:
            e = limbo
            limbo = None
            if e.fate is None:
                # slot reused with no marker: parked (SENT) own text released by echo/ACK, or unexplained
                if e.reads > 0:
                    e.fate = "tx_air_parked_released" if e.air else "tx_noair_other"
                else:
                    e.fate = "overwritten_unexplained"
                    unexplained.append({"line": e.line, "t": e.ts, "id": e.id, "slot": e.slot, "type": e.type,
                                        "status": e.status, "src": e.src, "prio": e.prio,
                                        "overwritten_line": line_no, "overwritten_t": t})
                e.fate_ts = t

    with open(path, "r", encoding="latin-1", errors="replace") as fh:
        for ln, line in enumerate(fh, 1):
            # ---- time
            if is_db:
                m = RE_CLOCK.match(line)
                if m and ln >= first_valid:   # earlier lines: clock not yet set (00:00:00)
                    cs = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
                    # optional profile: node clock ran step_back_s ahead until a step back at step_back_line
                    if ln < step_line:
                        cs -= step_s
                    if cs < prev_clock_s - 12 * 3600:
                        day += 1
                    prev_clock_s = cs
                cur_t = base_db + day * 86400 + prev_clock_s
                body = line
            else:
                tt = ts_prefix(line)
                if tt is not None:
                    cur_t = tt
                body = line[25:] if len(line) > 25 else line
            if first_t is None:
                first_t = cur_t
            last_t = cur_t

            if "RING_" not in body and "[LOG]" not in body and "TX_GATE" not in body and "CHANNEL_UTIL" not in body \
                    and "CAD_BUSY" not in body and "[NBR]|CANCEL" not in body and "Destination-CallSign" not in body \
                    and "CAD_FREE_NO_TX" not in body:
                continue

            # ---- order matters: limbo flush happens on any line that is not a post-write marker
            if "RING_PRIO" not in body and "RING_SUPERSEDE" not in body and "RING_DROP_PRIO" not in body \
                    and "RING_DROP_NEW" not in body and limbo is not None:
                flush_limbo(cur_t, ln)

            if "RING_WRITE" in body:
                m = RE_WRITE.search(body)
                if not m:
                    continue
                slot = int(m.group(1))
                if slot < 0:
                    write_neg += 1
                    continue
                settle_pend()
                prev = slots[slot]
                if prev is not None and prev.fate is None:
                    limbo = prev
                e = Entry(m.group(5), slot, cur_t, ln, m.group(2), m.group(3), m.group(7), int(m.group(6)))
                slots[slot] = e
                entries.append(e)
                open_by_id[e.id] = e
                last_write = e
                marker_counts["write"] += 1
            elif "RING_PRIO" in body:
                m = RE_PRIO.search(body)
                if m and last_write is not None and last_write.slot == int(m.group(1)):
                    last_write.prio = int(m.group(2))
            elif "RING_SUPERSEDE_KEEP" in body:
                marker_counts["supersede_keep"] += 1
            elif "RING_SUPERSEDE" in body:
                m = RE_SUP.search(body)
                if m:
                    marker_counts["supersede"] += 1
                    old = m.group(3)
                    ev = {"t": cur_t, "line": ln, "slot": int(m.group(1)), "prio": int(m.group(2)), "old": old,
                          "new": m.group(4)}
                    if limbo is not None and limbo.id == old:
                        limbo.fate = "superseded"
                        limbo.sup_by = last_write
                        limbo.fate_ts = cur_t
                        ev["old_age_s"] = cur_t - limbo.ts
                        ev["old_src"] = limbo.src
                        ev["old_type"] = limbo.type
                        ev["old_status"] = limbo.status
                        ev["old_reads"] = limbo.reads
                        limbo = None
                    else:
                        ev["unmatched"] = True
                    if last_write is not None:
                        ev["new_src"] = last_write.src
                        ev["new_type"] = last_write.type
                    sup_events.append(ev)
            elif "RING_DROP_PRIO" in body:
                m = RE_DPRIO.search(body)
                if m:
                    marker_counts["drop_prio"] += 1
                    if limbo is not None and limbo.id == m.group(4):
                        limbo.fate = "drop_prio"
                        limbo.fate_ts = cur_t
                        limbo = None
                    other_drop.append({"kind": "prio", "t": cur_t, "line": ln, "id": m.group(4), "prio": int(m.group(2))})
            elif "RING_DROP_NEW" in body:
                m = RE_DNEW.search(body)
                if m:
                    marker_counts["drop_new"] += 1
                    other_drop.append({"kind": "new", "t": cur_t, "line": ln, "id": m.group(4), "prio": int(m.group(2)),
                                       "slot": int(m.group(1))})
            elif "RING_DROP_STALE" in body:
                m = RE_STALE.search(body)
                if m:
                    slot = int(m.group(1))
                    e = slots[slot]
                    if e is not None and e.id == m.group(4) and e.fate is None:
                        e.fate = "stale_" + m.group(5)
                        e.fate_ts = cur_t
                    marker_counts["stale_" + m.group(5)] += 1
                    stale_events.append({"t": cur_t, "line": ln, "slot": slot, "prio": int(m.group(2)),
                                         "age_s": int(m.group(3)), "id": m.group(4), "reason": m.group(5),
                                         "matched": bool(e is not None and e.id == m.group(4)),
                                         "src": e.src if e else None, "type": e.type if e else None,
                                         "status": e.status if e else None})
            elif "RING_TX_READ" in body:
                m = RE_READ.search(body)
                if not m:
                    continue
                settle_pend()
                slot = int(m.group(1))
                mid = m.group(6)
                e = slots[slot]
                if e is not None and e.id == mid:
                    e.reads += 1
                    e.read_ts = cur_t
                    e.lat = int(m.group(9))
                    e.retry = int(m.group(7))
                    e.txq = int(m.group(8))
                    e.util = last_util
                    e.air = False
                    e.decodefail = False
                    if e.fate in (None,):
                        pass
                    pend_read = e
                    e.fate = None if e.fate is None else e.fate
                    marker_counts["read"] += 1
                else:
                    unmatched_reads += 1
            elif "RING_STATUS" in body:
                m = RE_STATUS.search(body)
                if m:
                    status_samples.append((cur_t, int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))))
            elif "TX_GATE_ENTER" in body:
                m = RE_GATE.search(body)
                if m:
                    gate_qlen.append(int(m.group(1)))
                    gate_cad.append(int(m.group(2)))
            elif "CHANNEL_UTIL" in body:
                m = RE_UTIL.search(body)
                if m:
                    last_util = int(m.group(3))
                    util_samples.append((cur_t, last_util))
            elif "CAD_BUSY" in body:
                cad_busy_win += 1
            elif "CAD_FREE_NO_TX" in body or "Destination-CallSign" in body:
                if pend_read is not None and not pend_read.air:
                    pend_read.decodefail = True
            elif "[LOG] TX " in body:
                m = RE_LOGTX.search(body)
                if m:
                    mid = m.group(4)
                    cs = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
                    if not is_db:
                        d = cs - (int(line[11:13]) * 3600 + int(line[14:16]) * 60 + int(line[17:19]))
                        d = (d + 43200) % 86400 - 43200
                        clock_off.append(d)
                    txlines.append((cur_t, mid, m.group(5), m.group(8), int(m.group(7)), int(m.group(9)),
                                    int(m.group(11)), int(m.group(10))))
                    if pend_read is not None and pend_read.id == mid:
                        pend_read.air = True
                        pend_read.log_cad = int(m.group(11))
            elif "[LOG] RLY " in body:
                m = RE_RLY.search(body)
                if m:
                    rly[m.group(4)] += 1
                    if m.group(4) == "tx":
                        rly_by_prio[int(m.group(5))] += 1
            elif "[LOG] STAT" in body:
                m = RE_STAT.search(body)
                if m:
                    stat_rows.append({"t": cur_t, "util": int(m.group(1)), "txn": int(m.group(2)),
                                      "txfail": int(m.group(3)), "ringmax": int(m.group(4)),
                                      "drop": [int(m.group(i)) for i in range(5, 10)]})
            elif "[NBR]|CANCEL" in body:
                m = RE_CANCEL.search(body)
                if m:
                    mid = m.group(2)
                    e = open_by_id.get(mid)
                    marker_counts["cancel"] += 1
                    ok = False
                    if e is not None and e.fate is None and e.reads == 0 and slots[e.slot] is e:
                        e.fate = "nbr_cancel"
                        e.fate_ts = cur_t
                        ok = True
                    cancel_events.append({"t": cur_t, "line": ln, "id": mid, "matched": ok,
                                          "age_s": (cur_t - e.ts) if e else None,
                                          "src": e.src if e else None, "prio": e.prio if e else None})
        flush_limbo(cur_t, ln)
    settle_pend()
    # remaining open entries
    for e in entries:
        if e.fate is None:
            if e.reads > 0:
                e.fate = "tx_air" if e.air else "tx_noair_other"
            else:
                e.fate = "pending_end"
    return {
        "entries": entries, "status_samples": status_samples, "gate_qlen": gate_qlen, "gate_cad": gate_cad,
        "util_samples": util_samples, "stat_rows": stat_rows, "txlines": txlines, "clock_off": clock_off,
        "rly": rly, "rly_by_prio": rly_by_prio, "unmatched_reads": unmatched_reads, "sup_events": sup_events,
        "stale_events": stale_events, "other_drop": other_drop, "marker_counts": marker_counts,
        "cancel_events": cancel_events, "unexplained": unexplained, "first_t": first_t, "last_t": last_t,
        "cad_busy": cad_busy_win, "write_neg": write_neg,
    }


def hour_key(t: float) -> str:
    return datetime.fromtimestamp(t, timezone.utc).strftime("%m-%d %H")


def fmt_t(t: float) -> str:
    return datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def summarise(node: str, R: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    entries: list[Entry] = R["entries"]
    out["span"] = {"first": fmt_t(R["first_t"]), "last": fmt_t(R["last_t"]),
                   "hours": (R["last_t"] - R["first_t"]) / 3600.0}
    out["markers"] = dict(R["marker_counts"])
    out["write_slot_neg"] = R["write_neg"]
    out["clock_offset_s"] = (pct(R["clock_off"], 50) if R["clock_off"] else None)

    # ---- 1. occupancy
    ss = R["status_samples"]
    q = [s[1] for s in ss]
    occ: dict[str, Any] = {"status_samples": len(ss), "queued": dist([float(x) for x in q])}
    n = len(q)
    for th in (5, 10, 15, 20):
        c = sum(1 for x in q if x >= th)
        occ[f"share_ge_{th}"] = c / n if n else None
        occ[f"minutes_ge_{th}"] = c * 0.5
    occ["status_pending_p90"] = pct([float(s[2]) for s in ss], 90)
    occ["status_retrying_nonzero_share"] = (sum(1 for s in ss if s[3] > 0) / n) if n else None
    occ["status_retrying_max"] = max((s[3] for s in ss), default=0)
    occ["status_done_max"] = max((s[4] for s in ss), default=0)
    occ["status_pending_max"] = max((s[2] for s in ss), default=0)
    # longest run of consecutive samples with queued >= k
    for k in (3, 5, 10):
        best = cur = 0
        best_t = None
        start = None
        for (t, qq, *_r) in ss:
            if qq >= k:
                if cur == 0:
                    start = t
                cur += 1
                if cur > best:
                    best = cur
                    best_t = start
            else:
                cur = 0
        occ[f"longest_run_ge_{k}_min"] = best * 0.5
        occ[f"longest_run_ge_{k}_start"] = fmt_t(best_t) if best_t else None
    # floor: windows of 10 min with min queued >= 1 (standing occupancy)
    wins: dict[int, list[int]] = defaultdict(list)
    for (t, qq, *_r) in ss:
        wins[int(t // 600)].append(qq)
    occ["win10_total"] = len(wins)
    occ["win10_floor_ge1"] = sum(1 for v in wins.values() if min(v) >= 1)
    occ["win10_floor_ge3"] = sum(1 for v in wins.values() if min(v) >= 3)
    # hourly profile
    hp: dict[str, list[int]] = defaultdict(list)
    for (t, qq, *_r) in ss:
        hp[hour_key(t)].append(qq)
    occ["hourly"] = {h: {"n": len(v), "mean": round(sum(v) / len(v), 2), "p90": pct([float(x) for x in v], 90),
                         "max": max(v)} for h, v in sorted(hp.items())}
    # growth: linear slope of hourly mean (entries/hour) and first vs last third
    hm = [sum(v) / len(v) for h, v in sorted(hp.items()) if len(v) >= 60]
    if len(hm) >= 4:
        xs = list(range(len(hm)))
        mx, my = sum(xs) / len(xs), sum(hm) / len(hm)
        sl = sum((x - mx) * (y - my) for x, y in zip(xs, hm)) / sum((x - mx) ** 2 for x in xs)
        occ["hourly_mean_slope_per_h"] = sl
    # event based
    occ["gate_qlen"] = dist([float(x) for x in R["gate_qlen"]])
    occ["gate_qlen_share_ge15"] = (sum(1 for x in R["gate_qlen"] if x >= 15) / len(R["gate_qlen"])) if R["gate_qlen"] else None
    wq1 = [float(e.wq + 1) for e in entries]
    occ["write_depth_after"] = dist(wq1)
    occ["write_depth_max"] = max(wq1) if wq1 else None
    occ["write_depth_ge15"] = sum(1 for x in wq1 if x >= 15)
    occ["write_depth_ge20"] = sum(1 for x in wq1 if x >= 20)
    # STAT ringmax
    rm = [r["ringmax"] for r in R["stat_rows"]]
    occ["stat_ringmax"] = dist([float(x) for x in rm])
    occ["stat_ringmax_windows_ge15"] = sum(1 for x in rm if x >= 15)
    occ["stat_windows"] = len(rm)
    drops = [0] * 5
    for r in R["stat_rows"]:
        for i in range(5):
            drops[i] += r["drop"][i]
    occ["stat_drop_sum_prio1_5"] = drops
    occ["stat_txfail_sum"] = sum(r["txfail"] for r in R["stat_rows"])
    occ["stat_txn_sum"] = sum(r["txn"] for r in R["stat_rows"])
    out["occupancy"] = occ

    # ---- 2. latency
    reads = [e for e in entries if e.reads > 0 and e.lat is not None]
    lat: dict[str, Any] = {}
    byprio: dict[int, list[float]] = defaultdict(list)
    bysrc: dict[str, list[float]] = defaultdict(list)
    bysrc_prio: dict[str, list[float]] = defaultdict(list)
    for e in reads:
        if e.fate not in ("tx_air", "tx_air_parked_released"):
            pass
        byprio[e.prio].append(e.lat / 1000.0)
        bysrc[e.src].append(e.lat / 1000.0)
        bysrc_prio[f"{e.src}/p{e.prio}"].append(e.lat / 1000.0)
    lat["by_prio_s"] = {str(p): dist(v) for p, v in sorted(byprio.items())}
    lat["by_src_s"] = {s: dist(v) for s, v in sorted(bysrc.items())}
    lat["by_src_prio_s"] = {s: dist(v) for s, v in sorted(bysrc_prio.items()) if len(v) >= 5}
    # beyond TTL (reads with lat above TTL)
    ttl = {5: 180, 4: 120, 3: 300, 1: 90}
    lat["over_ttl_reads"] = {str(p): sum(1 for e in reads if e.prio == p and e.lat / 1000.0 > t)
                             for p, t in ttl.items()}
    # latency vs util (relay prios 3-5 and acks p1 separately)
    ub: dict[str, list[float]] = defaultdict(list)
    xs: list[float] = []
    ys: list[float] = []
    for e in reads:
        if e.util is None or e.prio not in (3, 4, 5):
            continue
        b = "00-09" if e.util < 10 else "10-19" if e.util < 20 else "20-29" if e.util < 30 else "30-39" if e.util < 40 else "40+"
        ub[b].append(e.lat / 1000.0)
        xs.append(e.util)
        ys.append(e.lat)
    lat["relay_p3to5_vs_util"] = {b: dist(v) for b, v in sorted(ub.items())}
    lat["relay_p3to5_util_spearman"] = spearman(xs, ys)
    # vs cad attempts (TX line cad=)
    cb: dict[str, list[float]] = defaultdict(list)
    xs2: list[float] = []
    ys2: list[float] = []
    for e in reads:
        if e.log_cad is None:
            continue
        b = str(e.log_cad) if e.log_cad < 4 else "4+"
        cb[b].append(e.lat / 1000.0)
        xs2.append(e.log_cad)
        ys2.append(e.lat)
    lat["vs_cad_attempt"] = {b: dist(v) for b, v in sorted(cb.items())}
    lat["cad_attempt_spearman"] = spearman(xs2, ys2)
    # vs queue depth at read
    qb: dict[str, list[float]] = defaultdict(list)
    for e in reads:
        if e.txq is None:
            continue
        b = "1" if e.txq <= 1 else "2-3" if e.txq <= 3 else "4-7" if e.txq <= 7 else "8+"
        qb[b].append(e.lat / 1000.0)
    lat["vs_queue_depth"] = {b: dist(v) for b, v in sorted(qb.items())}
    # CAD busy per TX_READ
    lat["cad_busy_lines"] = R["cad_busy"]
    lat["gate_cad_attempt"] = dict(Counter(R["gate_cad"]).most_common(8))
    # hourly relay latency p50
    hl: dict[str, list[float]] = defaultdict(list)
    for e in reads:
        if e.prio in (3, 4, 5) and e.read_ts:
            hl[hour_key(e.read_ts)].append(e.lat / 1000.0)
    lat["hourly_relay_p50_s"] = {h: round(pct(v, 50), 1) for h, v in sorted(hl.items()) if len(v) >= 5}
    lat["hourly_relay_p90_s"] = {h: round(pct(v, 90), 1) for h, v in sorted(hl.items()) if len(v) >= 5}
    # hourly channel util mean
    hu: dict[str, list[int]] = defaultdict(list)
    for (t, u) in R["util_samples"]:
        hu[hour_key(t)].append(u)
    lat["hourly_util_mean"] = {h: round(sum(v) / len(v), 1) for h, v in sorted(hu.items())}
    out["latency"] = lat

    # ---- 3. fate
    fc = Counter(e.fate for e in entries)
    total = len(entries)
    fate: dict[str, Any] = {"total_writes": total, "counts": dict(fc),
                            "pct": {k: round(100.0 * v / total, 2) for k, v in fc.items()}}
    # group bucket
    fate["sum_pct"] = round(sum(100.0 * v / total for v in fc.values()), 2) if total else None
    by_src_fate: dict[str, Counter] = defaultdict(Counter)
    by_prio_fate: dict[int, Counter] = defaultdict(Counter)
    for e in entries:
        by_src_fate[e.src][e.fate or "?"] += 1
        by_prio_fate[e.prio][e.fate or "?"] += 1
    fate["by_src"] = {s: dict(c) for s, c in sorted(by_src_fate.items())}
    fate["by_prio"] = {str(p): dict(c) for p, c in sorted(by_prio_fate.items())}
    fate["unexplained_n"] = len(R["unexplained"])
    fate["unexplained_examples"] = [{**u, "t": fmt_t(u["t"]), "overwritten_t": fmt_t(u["overwritten_t"])}
                                    for u in R["unexplained"][:15]]
    pend = [e for e in entries if e.fate == "pending_end"]
    fate["pending_end_examples"] = [{"id": e.id, "t": fmt_t(e.ts), "age_s": R["last_t"] - e.ts, "slot": e.slot,
                                     "src": e.src, "prio": e.prio, "status": e.status, "type": e.type} for e in pend[:15]]
    # decode-fail drops
    dfs = [e for e in entries if e.fate == "tx_noair_decodefail"]
    fate["noair_decodefail"] = {"n": len(dfs), "by_src": dict(Counter(e.src for e in dfs)),
                                "by_prio": dict(Counter(e.prio for e in dfs)),
                                "by_type": dict(Counter(e.type for e in dfs)),
                                "first": fmt_t(dfs[0].ts) if dfs else None,
                                "last": fmt_t(dfs[-1].ts) if dfs else None,
                                "lat_s": dist([e.lat / 1000.0 for e in dfs if e.lat is not None])}
    nao = [e for e in entries if e.fate == "tx_noair_other"]
    fate["noair_other"] = {"n": len(nao), "examples": [{"id": e.id, "t": fmt_t(e.ts), "src": e.src, "prio": e.prio,
                                                         "type": e.type, "reads": e.reads} for e in nao[:10]]}
    multi = [e for e in entries if e.reads > 1]
    fate["entries_with_multiple_reads"] = len(multi)
    fate["unmatched_reads"] = R["unmatched_reads"]
    # cancel events
    ce = R["cancel_events"]
    fate["cancel_lines"] = len(ce)
    fate["cancel_matched"] = sum(1 for c in ce if c["matched"])
    fate["cancel_unmatched_examples"] = [{**c, "t": fmt_t(c["t"])} for c in ce if not c["matched"]][:8]
    fate["cancel_age_s"] = dist([c["age_s"] for c in ce if c["matched"] and c["age_s"] is not None])
    # stale events
    se = R["stale_events"]
    fate["stale_events"] = [{**s, "t": fmt_t(s["t"])} for s in se[:20]]
    fate["stale_by_prio_reason"] = dict(Counter(f"p{s['prio']}/{s['reason']}" for s in se))
    fate["stale_by_hour"] = dict(Counter(hour_key(s["t"]) for s in se))
    fate["stale_age_s"] = dist([float(s["age_s"]) for s in se])
    fate["stale_src"] = dict(Counter(f"{s['src']}/{s['type']}/{s['status']}" for s in se))
    out["fate"] = fate

    # relay RLY accounting
    out["rly"] = {"reasons": dict(R["rly"]), "tx_by_prio": {str(k): v for k, v in sorted(R["rly_by_prio"].items())}}

    # ---- 4. slot reuse / dup TX
    tx = R["txlines"]
    out["tx_lines"] = {"n": len(tx), "by_src": dict(Counter(t[3] for t in tx)),
                       "by_prio": dict(Counter(str(t[4]) for t in tx)),
                       "per_hour": dict(Counter(hour_key(t[0]) for t in tx))}
    # double TX within 30 min
    byid: dict[str, list[tuple[float, str, str, int, int, int]]] = defaultdict(list)
    for (t, mid, typ, src, prio, wait, cad, q) in tx:
        byid[mid].append((t, typ, src, prio, wait, cad))
    dup_pairs = []
    for mid, lst in byid.items():
        lst.sort()
        for i in range(1, len(lst)):
            if lst[i][0] - lst[i - 1][0] <= 1800:
                dup_pairs.append({"id": mid, "t1": lst[i - 1][0], "t2": lst[i][0], "gap_s": lst[i][0] - lst[i - 1][0],
                                  "type": lst[i][1], "src1": lst[i - 1][2], "src2": lst[i][2], "prio": lst[i][3],
                                  "wait1": lst[i - 1][4], "wait2": lst[i][4]})
    dd: dict[str, Any] = {"pairs": len(dup_pairs), "ids": len({d['id'] for d in dup_pairs}),
                          "by_type": dict(Counter(d["type"] for d in dup_pairs)),
                          "by_src": dict(Counter(f"{d['src1']}->{d['src2']}" for d in dup_pairs)),
                          "by_prio": dict(Counter(d["prio"] for d in dup_pairs)),
                          "gap_s": dist([d["gap_s"] for d in dup_pairs]),
                          "share_of_tx_pct": round(100.0 * len(dup_pairs) / max(1, len(tx)), 3),
                          "examples": [{**d, "t1": fmt_t(d["t1"]), "t2": fmt_t(d["t2"])} for d in dup_pairs[:12]]}
    out["double_tx"] = dd
    # same msg_id written twice (RING_WRITE) within 30 min
    wid: dict[str, list[Entry]] = defaultdict(list)
    for e in entries:
        wid[e.id].append(e)
    wd = []
    for mid, lst in wid.items():
        for i in range(1, len(lst)):
            if lst[i].ts - lst[i - 1].ts <= 1800:
                wd.append((mid, lst[i - 1], lst[i]))
    out["double_write"] = {"pairs": len(wd),
                           "by_src": dict(Counter(f"{a.src}->{b.src}" for _m, a, b in wd)),
                           "by_fate_first": dict(Counter(a.fate for _m, a, b in wd)),
                           "gap_s": dist([b.ts - a.ts for _m, a, b in wd]),
                           "examples": [{"id": m, "t1": fmt_t(a.ts), "t2": fmt_t(b.ts), "src1": a.src, "src2": b.src,
                                         "fate1": a.fate, "fate2": b.fate, "type": a.type} for m, a, b in wd[:10]]}
    # SENT / parked status stuck (RING_STATUS retrying>0 runs)
    run = best = 0
    best_t = None
    st_t = None
    for (t, qq, pe, re_, dn) in ss:
        if re_ > 0:
            if run == 0:
                st_t = t
            run += 1
            if run > best:
                best, best_t = run, st_t
        else:
            run = 0
    out["slots"] = {"retrying_longest_run_min": best * 0.5, "retrying_longest_start": fmt_t(best_t) if best_t else None,
                    "own_status_not_FF_writes": sum(1 for e in entries if e.status != "FF"),
                    "own_writes_by_src": dict(Counter(e.src for e in entries if e.src not in ("rx_relay", "auto_pos") )),
                    "read_retry_gt0": sum(1 for e in reads if e.retry > 0)}
    # ---- 4b. model consistency: logged queued= (depth before write) vs entry-lifetime model,
    #          and lowest-free-slot check (pool must use the first free slot)
    def close_t(e: Entry) -> float:
        if e.fate in ("tx_air", "tx_noair_decodefail", "tx_noair_other", "tx_air_parked_released"):
            return (e.read_ts or e.ts) + 1.0     # slot stays INFLIGHT until the send ends
        return e.fate_ts if e.fate_ts is not None else float("inf")
    ev_sorted = sorted(entries, key=lambda e: e.ts)
    open_list: list[Entry] = []
    diffs: list[int] = []
    holes = 0
    sub = 0
    nchk = 0
    for e in ev_sorted:
        open_list = [o for o in open_list if close_t(o) > e.ts]
        occ_slots = {o.slot for o in open_list}
        is_sup = any(o.slot == e.slot and o.fate == "superseded" and o.sup_by is e for o in open_list)
        diffs.append(e.wq - len(open_list))
        if not is_sup:
            free = [i for i in range(20) if i not in occ_slots]
            if free:
                nchk += 1
                if e.slot > free[0]:
                    holes += 1
                elif e.slot < free[0]:
                    sub += 1
        open_list.append(e)
    dc = Counter(diffs)
    out["consistency"] = {"n": len(diffs), "queued_minus_model": {str(k): v for k, v in sorted(dc.items())},
                          "exact_share": dc.get(0, 0) / max(1, len(diffs)),
                          "within1_share": (dc.get(0, 0) + dc.get(1, 0) + dc.get(-1, 0)) / max(1, len(diffs)),
                          "slot_checked": nchk, "slot_above_lowest_free": holes, "slot_below_lowest_free": sub}
    # ---- 5. supersede
    sv = R["sup_events"]
    # chain tail resolution
    def tail(e: Entry | None, depth: int = 0) -> str:
        if e is None:
            return "none"
        if e.fate == "superseded" and e.sup_by is not None and depth < 50:
            return tail(e.sup_by, depth + 1)
        return e.fate or "?"
    chain_tails = Counter()
    chain_tails_src: Counter = Counter()
    lost_chain = []
    for e in entries:
        if e.fate == "superseded":
            tl = tail(e)
            chain_tails[tl] += 1
            chain_tails_src[f"{e.src}->{tl}"] += 1
            if tl not in ("tx_air", "tx_air_parked_released"):
                lost_chain.append({"id": e.id, "t": fmt_t(e.ts), "tail": tl, "src": e.src, "type": e.type})
    out["supersede"] = {"events": len(sv), "unmatched": sum(1 for s in sv if s.get("unmatched")),
                        "by_type": dict(Counter(s.get("new_type", "?") for s in sv)),
                        "by_prio": dict(Counter(s["prio"] for s in sv)),
                        "by_new_src": dict(Counter(s.get("new_src", "?") for s in sv)),
                        "by_old_src": dict(Counter(s.get("old_src", "?") for s in sv)),
                        "old_age_s": dist([s["old_age_s"] for s in sv if "old_age_s" in s]),
                        "old_had_reads": sum(1 for s in sv if s.get("old_reads", 0) > 0),
                        "chain_tail": dict(chain_tails), "chain_tail_by_src": dict(chain_tails_src), "chain_lost_examples": lost_chain[:10],
                        "per_hour": dict(Counter(hour_key(s["t"]) for s in sv)),
                        "examples": [{**s, "t": fmt_t(s["t"])} for s in sv[:8]]}
    out["other_drops"] = {"drop_prio_new": dict(Counter(d["kind"] for d in R["other_drop"])),
                          "examples": [{**d, "t": fmt_t(d["t"])} for d in R["other_drop"][:10]]}
    return out


def dump_events(node: str, R: dict[str, Any], path: str) -> None:
    ev = []
    for e in R["entries"]:
        if e.reads > 0 and e.lat is not None:
            ev.append([e.id, round(e.read_ts or 0, 3), e.lat, e.prio, e.src, e.type, e.fate])
    with open(path, "w") as fh:
        json.dump(ev, fh)
