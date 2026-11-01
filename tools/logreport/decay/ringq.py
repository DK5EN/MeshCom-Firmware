"""Ring reconstruction from RING_WRITE / RING_TX_READ / RING_DROP_STALE (per node).

Entry = one RING_WRITE. Outcome by matching (slot, msg_id) forward in time:
  sent       RING_TX_READ with same slot+mid after the write and before the slot's next write
  dropped    RING_DROP_STALE same slot+mid before the slot's next write
  replaced   the slot was written again with neither (supersede / evict / SENT-ladder end / ACK clear)
"""
from __future__ import annotations

from bisect import bisect_left
from collections import defaultdict
from typing import Any



def entries(d: dict) -> list[dict[str, Any]]:
    prio_by_slot: dict[int, list] = defaultdict(list)
    for (t, slot, p) in d["prio"]:
        prio_by_slot[slot].append((t, p))
    ev = []   # (t, kind, payload)
    for w in d["write"]:
        ev.append((w[0], 0, "w", w))
    for r in d["txread"]:
        ev.append((r[0], 1, "t", r))
    for r in d["drop"]:
        ev.append((r[0], 1, "d", r))
    ev.sort(key=lambda e: (e[0], e[1]))
    cur: dict[int, dict] = {}
    out: list[dict] = []
    # prio events follow their write by < 50 ms: index by (slot) sorted
    pi = {s: [x[0] for x in v] for s, v in prio_by_slot.items()}
    for (t, _, k, p) in ev:
        if k == "w":
            slot, mid = p[1], p[5]
            pr = None
            lst = prio_by_slot.get(slot)
            if lst:
                j = bisect_left(pi[slot], t - 0.001)
                if j < len(lst) and lst[j][0] - t < 0.5:
                    pr = lst[j][1]
            e = {"t": t, "slot": slot, "type": p[2], "status": p[3], "len": p[4], "mid": mid,
                 "queued": p[6], "src": p[7], "prio": pr, "outcome": "replaced", "t_end": None,
                 "lat_ms": None, "line": p[8]}
            old = cur.get(slot)
            if old is not None and old["t_end"] is None:
                old["t_end"] = t   # replaced
            cur[slot] = e
            out.append(e)
        elif k == "t":
            e = cur.get(p[1])
            if e is not None and e["mid"] == p[6] and e["outcome"] == "replaced" and e["t_end"] is None:
                e["outcome"] = "sent"
                e["t_end"] = t
                e["lat_ms"] = p[9]
                e["retry"] = p[7]
        else:
            e = cur.get(p[1])
            if e is not None and e["mid"] == p[4] and e["t_end"] is None:
                e["outcome"] = "dropped"
                e["t_end"] = t
                e["drop_reason"] = p[5]
                e["drop_age_s"] = p[3]
    return out
