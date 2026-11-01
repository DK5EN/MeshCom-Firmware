#!/usr/bin/env python3
"""Q3/Q4(a): per-drop table + loss classification. Writes work/drops.json (list of dicts)."""
from __future__ import annotations

import json
from collections import defaultdict

from .common import NODES, hhmmss, load, work

WIN_AFTER = 1800.0   # 30 min


def run() -> None:
    D = {n: load(n) for n in NODES}
    # indices: msg_id -> sorted list of (t, node) for own TX ([LOG] TX) and for receptions
    tx_idx: dict[str, list] = defaultdict(list)
    rx_idx: dict[str, list] = defaultdict(list)
    for n, d in D.items():
        for r in d["logtx"]:
            tx_idx[r[1]].append((r[0], n, r[3], r[4]))   # t, node, hop, prio
        for r in d["logrx"]:
            rx_idx[r[2]].append((r[0], n, r[3], r[4], r[5], r[6], r[1], r[7]))
    # r fields logrx: t, ptype, mid, hop, dup, rssi, snr, orig, size
    out = []
    for n, d in D.items():
        writes = d["write"]
        # prio map: (slot, nearest t) -> prio; prio events follow write within ms
        prio_by_t = {}
        for (t, slot, p) in d["prio"]:
            prio_by_t.setdefault(slot, []).append((t, p))
        w_by_mid: dict[str, list] = defaultdict(list)
        for w in writes:
            w_by_mid[w[5]].append(w)
        txr_by_mid: dict[str, list] = defaultdict(list)
        for r in d["txread"]:
            txr_by_mid[r[6]].append(r)
        rx_here: dict[str, list] = defaultdict(list)
        for r in d["logrx"]:
            rx_here[r[2]].append(r)
        cancels = defaultdict(list)
        for c in d["cancel"]:
            cancels[c[1]].append(c)
        for (t, slot, prio, age, mid_i, reason, ln) in d["drop"]:
            mid = f"{mid_i:08X}" if isinstance(mid_i, int) else mid_i
            tw = t - age
            # matching write: same msg_id, same slot, latest <= t
            cand = [w for w in w_by_mid.get(mid, []) if w[1] == slot and w[0] <= t]
            w = cand[-1] if cand else None
            rec = {"node": n, "t": t, "ts": hhmmss(t), "slot": slot, "prio": prio, "age_s": age,
                   "mid": mid, "reason": reason, "line": ln, "write_found": w is not None}
            if w:
                rec.update(type=w[2], status=w[3], len=w[4], queued=w[6], src=w[7],
                           write_t=w[0], write_age_s=round(t - w[0], 1))
            else:
                rec.update(type=None, status=None, len=None, src=None, write_t=None)
            # origin / type char from first reception of this id on this node before drop
            rx0 = [r for r in rx_here.get(mid, []) if r[0] <= t + 1]
            if rx0:
                r0 = rx0[0]
                rec.update(orig=r0[7], ptype=r0[1], first_rx_hop=r0[3], first_rx_t=r0[0],
                           n_rx_here_before_drop=len(rx0))
            else:
                rec.update(orig=None, ptype=None)
            # (0) did THIS node transmit it between write and drop? (must be no: slot freed at TX)
            twin0 = (w[0] if w else tw) - 1
            txr = [r for r in txr_by_mid.get(mid, []) if twin0 <= r[0] <= t]
            rec["own_txread_before_drop"] = len(txr)
            # (i) same node [LOG] TX of that id in [tw-60, t+30min]
            own_tx = [x for x in tx_idx.get(mid, []) if x[1] == n and tw - 60 <= x[0] <= t + WIN_AFTER]
            rec["own_tx_window"] = [(round(x[0] - t, 1)) for x in own_tx]
            # (ii) other node TX
            oth_before = [x for x in tx_idx.get(mid, []) if x[1] != n and tw - 1800 <= x[0] < tw]
            oth_after = [x for x in tx_idx.get(mid, []) if x[1] != n and tw <= x[0] <= t + WIN_AFTER]
            rec["other_tx_before_write"] = sorted({x[1] for x in oth_before})
            rec["other_tx_after_write"] = [(x[1], round(x[0] - t, 1)) for x in oth_after]
            # (iii) this node heard it again after the write (neighbour relay) within +30 min
            heard = [r for r in rx_here.get(mid, []) if tw <= r[0] <= t + WIN_AFTER and r[0] > (rx0[0][0] if rx0 else 0)]
            rec["heard_again_here"] = [(round(r[0] - t, 1), r[3], r[5], r[6]) for r in heard]
            # any other node receiving it in window (reach proxy)
            anyrx = [x for x in rx_idx.get(mid, []) if tw - 1800 <= x[0] <= t + WIN_AFTER]
            rec["rx_nodes_any"] = sorted({x[1] for x in anyrx})
            rec["cancel_here"] = [(round(c[0] - t, 1), c[2], c[3]) for c in cancels.get(mid, [])
                                  if tw - 5 <= c[0] <= t + 5]
            # refresh test: same originator, same ptype heard on any of the five nodes after write
            refresh = None
            if rec.get("orig") and rec.get("ptype"):
                newer = []
                for n2, d2 in D.items():
                    for r in d2["logrx"]:
                        if tw <= r[0] <= t + WIN_AFTER and r[7] == rec["orig"] and r[1] == rec["ptype"] \
                                and r[2] != mid:
                            newer.append((r[0], n2))
                refresh = sorted(newer)[0][0] - t if newer else None
            rec["refreshed_after_s"] = refresh
            out.append(rec)
    out.sort(key=lambda r: (r["node"], r["t"]))
    with open(work() / "drops.json", "w") as fh:
        json.dump(out, fh)
    print(len(out), "drops;", sum(1 for r in out if r["write_found"]), "with write found")
