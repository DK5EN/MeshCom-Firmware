#!/usr/bin/env python3
"""Own-beacon fate and head-of-queue analysis for OE3XOC-12 from the normalised log.

Usage: python3 -I oe3xoc_ownbeacons.py --norm <norm-dir> --out <json>
  --norm  dir holding oe3xoc.log (<logs>/.logreport-work/norm/)
  --out   json to write (RUN/data/extra/oe3xoc_ownbeacons.json)

One streaming pass over RING_WRITE / RING_PRIO / RING_TX_READ / RING_DROP_STALE / RING_SUPERSEDE,
TX_GATE_ENTER, CAD_BUSY, RX_TIMEOUT_FIRE and [LOG] reception lines. Stdlib only. All times are the
logger clock written at the start of each line. Output keys are documented in the generator's dev section.

Head of queue = live entry with the lowest prio value, then the earliest enqueue time (the pick of
getNextTxSlot(), src/txring_functions.cpp:213-251; supersede resets the enqueue time). The NBR case-B
hold skip of getNextTxSlot is not modelled.
"""

from __future__ import annotations

import argparse
import json
import re
from bisect import bisect_left, bisect_right
from collections import Counter
from datetime import datetime
from pathlib import Path
from typing import Any

TS = re.compile(r"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  ")
W = re.compile(
    r"RING_WRITE slot=(\d+) type=([0-9A-F]+) status=[0-9A-F]+ len=\d+ msg_id=([0-9A-F]+) queued=\d+/\d+ src=(\w+)"
)
P = re.compile(r"RING_PRIO slot=(\d+) prio=(\d)")
SUP = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=\d old_id=([0-9A-F]+) new_id=([0-9A-F]+)")
RD = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) type=[0-9A-F]+ status=\w+ len=\d+ msg_id=([0-9A-F]+) ")
STL = re.compile(r"RING_DROP_STALE slot=(\d+) prio=(\d) age_s=(\d+) msg_id=([0-9A-F]+) reason=(\w+)")
RTF = re.compile(r"RX_TIMEOUT_FIRE ts=\d+ wait=(\d+)")
CLASSES = {
    ("40", "auto_pos"): "own_HEY",
    ("40", "rx_relay"): "relay_HEY",
    ("21", "user_wx"): "own_WX",
    ("21", "rx_relay"): "relay_POS",
}


def stamp(m: re.Match[str]) -> float:
    y, mo, d, h, mi, s, ms = (int(x) for x in m.groups())
    return datetime(y, mo, d, h, mi, s, ms * 1000).timestamp()


def iso(t: float) -> str:
    return datetime.fromtimestamp(t).strftime("%Y-%m-%dT%H:%M:%S")


def pct(v: list[float], q: float) -> float:
    s = sorted(v)
    return s[min(len(s) - 1, int(q * len(s)))] if s else 0.0


class Ent:
    __slots__ = ("mid", "slot", "typ", "src", "t", "prio", "t_end", "read_t", "fate", "prev")

    def __init__(self, mid: str, slot: int, typ: str, src: str, t: float) -> None:
        self.mid, self.slot, self.typ, self.src, self.t = mid, slot, typ, src, t
        self.prio = 9
        self.t_end: float | None = None
        self.read_t: float | None = None
        self.fate = "pending"
        self.prev: Ent | None = None


def union_len(iv: list[tuple[float, float]]) -> float:
    tot, cs, ce = 0.0, None, None
    for s, e in sorted(iv):
        if cs is None or ce is None or s > ce:
            if cs is not None and ce is not None:
                tot += ce - cs
            cs, ce = s, e
        else:
            ce = max(ce, e)
    if cs is not None and ce is not None:
        tot += ce - cs
    return tot


def head_analysis(
    targets: list[Ent], ents: list[Ent], gate_t: list[float], busy_t: list[float]
) -> dict[str, Any]:
    """Share of the last link's life with a higher-precedence live entry, head time, gate entries."""
    n = 0
    win_tot = head_tot = 0.0
    gates = busy = zero_gate_long = long_n = 0
    heads: list[float] = []
    for tg in targets:
        if tg.t_end is None:
            continue
        a, b = tg.t, tg.t_end
        block: list[tuple[float, float]] = []
        for e in ents:
            if e is tg:
                continue
            end = e.t_end if e.t_end is not None else b
            if e.read_t is not None:
                end = min(end, e.read_t)
            if e.t >= b or end <= a:
                continue
            if e.prio < tg.prio or (e.prio == tg.prio and e.t < tg.t):
                block.append((max(a, e.t), min(b, end)))
        segs: list[tuple[float, float]] = []
        cur = a
        for s, e2 in sorted(block):
            if s > cur:
                segs.append((cur, s))
            cur = max(cur, e2)
        if cur < b:
            segs.append((cur, b))
        head = sum(e2 - s for s, e2 in segs)
        g = sum(bisect_left(gate_t, e2) - bisect_right(gate_t, s) for s, e2 in segs)
        bz = sum(bisect_left(busy_t, e2) - bisect_right(busy_t, s) for s, e2 in segs)
        n += 1
        win_tot += b - a
        head_tot += head
        gates += g
        busy += bz
        heads.append(head)
        if head >= 30:
            long_n += 1
            zero_gate_long += 1 if g == 0 else 0
    return {
        "n": n,
        "window_s_total": round(win_tot, 1),
        "head_s_total": round(head_tot, 1),
        "blocked_pct": round(100 * (1 - head_tot / win_tot), 1) if win_tot else 0.0,
        "head_s_p50": round(pct(heads, 0.5), 1),
        "gate_entries_in_head": gates,
        "cad_busy_in_head": busy,
        "n_head_ge_30s": long_n,
        "n_head_ge_30s_zero_gate": zero_gate_long,
    }


def run(path: Path) -> dict[str, Any]:
    ents: list[Ent] = []
    live: dict[str, list[Ent]] = {}
    by_slot: dict[int, Ent] = {}
    reads: list[tuple[float, int, str, float]] = []  # t, prio, mid, write-time of that entry
    rx_ok_t: list[float] = []
    rx_err_t: list[float] = []
    gate_t: list[float] = []
    busy_t: list[float] = []
    # head hazard state
    slots: dict[int, tuple[int, float, str, str]] = {}
    pend: tuple[int, str, str, float] | None = None
    head_time: Counter[str] = Counter()
    head_tx: Counter[str] = Counter()
    head_gate: Counter[str] = Counter()
    head_rx: Counter[str] = Counter()
    holds: dict[str, list[int]] = {}
    last_t: float | None = None
    first_t = 0.0
    n_lines = 0

    def head() -> str:
        if not slots:
            return "empty"
        p, _te, c, _i = min(slots.values(), key=lambda v: (v[0], v[1]))
        return c if c != "other" else f"other_p{p}"

    with path.open("rb") as fh:
        for raw in fh:
            n_lines += 1
            line = raw.decode("latin-1", "replace")
            m0 = TS.match(line)
            if not m0:
                continue
            t = stamp(m0)
            if last_t is None:
                first_t = t
            h = head()
            if last_t is not None and t > last_t:
                head_time[h] += t - last_t
            last_t = t
            body = line[25:]
            if "[LOG] " in body[:20]:
                if " TX " not in body and "FCS:0000" not in body and " ERR " not in body:
                    head_rx[h] += 1
                continue
            if "TX_GATE_ENTER" in body:
                gate_t.append(t)
                head_gate[h] += 1
                continue
            if "] CAD_BUSY attempt=" in body:
                busy_t.append(t)
                continue
            if body.startswith("[MC-DBG] ONRXDONE_TIME ms="):
                rx_ok_t.append(t)
                continue
            if body.startswith("[MC-DBG] RX_ERROR rssi="):
                rx_err_t.append(t)
                continue
            m = RTF.search(body)
            if m:
                holds.setdefault(h, []).append(int(m.group(1)))
                continue
            if "RING_" not in body:
                continue
            m = W.search(body)
            if m:
                slot, typ, mid, src = int(m.group(1)), m.group(2), m.group(3), m.group(4)
                e = Ent(mid, slot, typ, src, t)
                ents.append(e)
                live.setdefault(mid, []).append(e)
                by_slot[slot] = e
                pend = (slot, CLASSES.get((typ, src), "other"), mid, t)
                continue
            m = P.search(body)
            if m:
                s, pr = int(m.group(1)), int(m.group(2))
                e2 = by_slot.get(s)
                if e2 is not None and e2.prio == 9:
                    e2.prio = pr
                if pend is not None and pend[0] == s:
                    slots[s] = (pr, pend[3], pend[1], pend[2])
                    pend = None
                continue
            m = SUP.search(body)
            if m:
                lst = live.get(m.group(2))
                old = lst.pop(0) if lst else None
                lst2 = live.get(m.group(3))
                new = lst2[-1] if lst2 else None
                if old is not None:
                    old.fate, old.t_end = "superseded", t
                    if new is not None:
                        new.prev = old
                s = int(m.group(1))
                if s in slots and slots[s][3] == m.group(2):
                    del slots[s]
                continue
            m = RD.search(body)
            if m:
                s, mid = int(m.group(1)), m.group(3)
                lst = live.get(mid)
                e3 = lst.pop(0) if lst else None
                if e3 is not None:
                    e3.fate, e3.read_t, e3.t_end = "air", t, t
                    reads.append((t, int(m.group(2)), mid, e3.t))
                else:
                    reads.append((t, int(m.group(2)), mid, -1.0))
                if s in slots and slots[s][3] == mid:
                    c = slots[s][2]
                    if h == c or (h.startswith("other") and c == "other"):
                        head_tx[h] += 1
                    del slots[s]
                continue
            m = STL.search(body)
            if m:
                lst = live.get(m.group(4))
                e4 = lst.pop(0) if lst else None
                if e4 is not None:
                    e4.fate, e4.t_end = "ttl", t
                s = int(m.group(1))
                if s in slots and slots[s][3] == m.group(4):
                    del slots[s]
                continue
    assert last_t is not None
    end_t = last_t

    own_wx = [e for e in ents if e.typ == "21" and e.src == "user_wx"]
    own_hey = [e for e in ents if e.typ == "40" and e.src == "auto_pos"]
    superseded = sum(1 for e in own_hey if e.fate == "superseded")
    hey_tails = [e for e in own_hey if e.fate != "superseded"]
    hey_air = [e for e in hey_tails if e.fate == "air"]
    hey_ttl = [e for e in hey_tails if e.fate == "ttl"]
    hey_unk = len(hey_tails) - len(hey_air) - len(hey_ttl)

    # hour buckets over the whole log, by end of fate (episode tails for HEY, every WX)
    hours: dict[str, list[int]] = {}
    t0h = datetime.fromtimestamp(first_t).replace(minute=0, second=0, microsecond=0).timestamp()
    k = t0h
    while k <= end_t:
        hours[datetime.fromtimestamp(k).strftime("%d.%m. %H")] = [0, 0]
        k += 3600
    for e in [*own_wx, *hey_tails]:
        if e.fate in ("air", "ttl") and e.t_end is not None:
            key = datetime.fromtimestamp(e.t_end).strftime("%d.%m. %H")
            if key in hours:
                hours[key][0 if e.fate == "air" else 1] += 1

    # WX gaps
    wx_air_t = sorted(e.read_t for e in own_wx if e.read_t is not None)
    gaps = [(b - a, a, b) for a, b in zip(wx_air_t, wx_air_t[1:], strict=False)]
    gmax = max(gaps) if gaps else (0.0, 0.0, 0.0)
    trailing_wx = end_t - wx_air_t[-1] if wx_air_t else 0.0
    # consecutive lost WX in the longest gap
    lost_in_gap = [e for e in own_wx if e.fate == "ttl" and gmax[1] < e.t < gmax[2]]
    # next WX aired after the gap is followed by n lost until log end
    after = [e for e in own_wx if e.t > gmax[2] and e.fate == "ttl"]
    # FIFO check: no prio-4 entry written later than the dropped WX was read inside its life
    fifo_checked = fifo_viol = 0
    for e in own_wx:
        if e.fate != "ttl" or e.t_end is None:
            continue
        fifo_checked += 1
        for rt, pr, _mid, wt in reads:
            if rt > e.t_end:
                break
            if rt >= e.t and pr == 4 and wt > e.t:
                fifo_viol += 1
                break
    # window TX by priority
    win_tx: Counter[str] = Counter()
    for rt, pr, _mid, _wt in reads:
        if gmax[1] <= rt <= gmax[2]:
            win_tx[str(pr)] += 1
    hey_air_t = sorted(e.read_t for e in hey_air if e.read_t is not None)
    hgaps = [(b - a) / 60 for a, b in zip(hey_air_t, hey_air_t[1:], strict=False)]
    # lost WX by clock hour of the write time (local logger clock)
    night_lost = sum(
        1 for e in own_wx if e.fate == "ttl" and (datetime.fromtimestamp(e.t).hour >= 21 or datetime.fromtimestamp(e.t).hour < 8)
    )
    night_all = sum(1 for e in own_wx if datetime.fromtimestamp(e.t).hour >= 21 or datetime.fromtimestamp(e.t).hour < 8)

    rx_all = sorted(rx_ok_t + rx_err_t)
    rgaps = [(b - a, a) for a, b in zip(rx_all, rx_all[1:], strict=False)]
    rg = max(rgaps) if rgaps else (0.0, 0.0)
    rx_gap_vals = [g for g, _a in rgaps]
    hazard: dict[str, Any] = {}
    for cls, tt in head_time.items():
        if tt < 60:
            continue
        hs = sorted(holds.get(cls, []))
        mins = tt / 60
        hazard[cls] = {
            "head_s": round(tt),
            "tx": head_tx[cls],
            "tx_per_min": round(head_tx[cls] / mins, 2),
            "gate_per_min": round(head_gate[cls] / mins, 2),
            "rx_per_min": round(head_rx[cls] / mins, 2),
            "hold_p50_ms": hs[len(hs) // 2] if hs else 0,
        }
    return {
        "meta": {
            "source": "oe3xoc.log (logger clock)",
            "lines": n_lines,
            "first": iso(first_t),
            "last": iso(end_t),
        },
        "wx": [
            {"t": iso(e.t), "fate": e.fate, "tx": iso(e.read_t) if e.read_t else None, "id": e.mid}
            for e in own_wx
        ],
        "wx_summary": {
            "total": len(own_wx),
            "air": sum(e.fate == "air" for e in own_wx),
            "ttl": sum(e.fate == "ttl" for e in own_wx),
            "longest_gap_s": round(gmax[0]),
            "longest_gap_start": iso(gmax[1]),
            "longest_gap_end": iso(gmax[2]),
            "lost_inside_longest_gap": len(lost_in_gap),
            "lost_after_longest_gap": len(after),
            "trailing_gap_s": round(trailing_wx),
            "last_air": iso(wx_air_t[-1]) if wx_air_t else None,
            "lost_written_21_to_08": night_lost,
            "written_21_to_08": night_all,
        },
        "hey_summary": {
            "written": len(own_hey),
            "superseded": superseded,
            "episodes": len(hey_tails),
            "air": len(hey_air),
            "ttl": len(hey_ttl),
            "unresolved": hey_unk,
            "air_pct_of_episodes": round(100 * len(hey_air) / len(hey_tails), 1) if hey_tails else 0.0,
            "air_pct_of_written": round(100 * len(hey_air) / len(own_hey), 1) if own_hey else 0.0,
            "gap_min_p50": round(pct(hgaps, 0.5), 1),
            "gap_min_p90": round(pct(hgaps, 0.9), 1),
            "gap_min_max": round(max(hgaps), 1) if hgaps else 0.0,
            "gaps_over_20min": sum(1 for g in hgaps if g > 20),
            "trailing_gap_h": round((end_t - hey_air_t[-1]) / 3600, 2) if hey_air_t else 0.0,
            "last_air": iso(hey_air_t[-1]) if hey_air_t else None,
            "longest_gap_start": iso(hey_air_t[hgaps.index(max(hgaps))]) if hgaps else None,
        },
        "rx_all": {
            "decoded": len(rx_ok_t),
            "errors": len(rx_err_t),
            "error_share_pct": round(100 * len(rx_err_t) / (len(rx_err_t) + len(rx_ok_t)), 1) if rx_all else 0.0,
            "gap_s_median": round(pct(rx_gap_vals, 0.5), 2),
            "gap_s_longest": round(rg[0], 1),
            "gap_longest_start": iso(rg[1]),
            "gaps_over_300s": sum(1 for g in rx_gap_vals if g > 300),
        },
        "own_per_hour": [{"hour": k2, "air": v[0], "ttl": v[1]} for k2, v in hours.items()],
        "window_tx_by_prio": dict(sorted(win_tx.items())),
        "window_tx_total": sum(win_tx.values()),
        "fifo": {"checked": fifo_checked, "violations": fifo_viol},
        "head_hazard": hazard,
        "head_analysis": {
            "own_HEY_ttl": head_analysis(hey_ttl, ents, gate_t, busy_t),
            "own_WX_ttl": head_analysis([e for e in own_wx if e.fate == "ttl"], ents, gate_t, busy_t),
        },
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    res = run(a.norm / "oe3xoc.log")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
