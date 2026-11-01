#!/usr/bin/env python3
"""Q6.2 redundancy: per msg_id (window <= 30 min), which of the five OE3 nodes transmitted it.

Outputs work/redundancy.json and work/doubles.jsonl (one record per relay pair).
"""
from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict
from itertools import combinations

sys.path.insert(0, str(__import__("pathlib").Path(__file__).parent))
from common import CALL, CALL2NODE, NODES, OUTD, load, median

ev = {n: load(n) for n in NODES}

# audibility: count of RX frames at B whose last hop is A (path[-1] == CALL[A]) with path len >= 2
aud = Counter()
aud_rssi = defaultdict(list)
for b in NODES:
    for e in ev[b]:
        if e["k"] == "rx" and len(e["path"]) >= 1:
            a = CALL2NODE.get(e["path"][-1])
            if a and a != b:
                aud[(a, b)] += 1
                aud_rssi[(a, b)].append(e["rssi"])
# ACK frames carry no path: no information.

# transmissions
tx = []  # (t, node, id, typ, src, hop, len)
for n in NODES:
    for e in ev[n]:
        if e["k"] == "tx":
            tx.append((e["t"], n, e["id"], e["typ"], e["src"], e["hop"], e["len"], e["wait"], e["cad"]))
tx.sort()

# per-node RX index: id -> list of (t, lasthop_call, dup)
rxidx = {n: defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e["k"] == "rx":
            rxidx[n][e["id"]].append((e["t"], e["path"][-1] if e["path"] else "", e["dup"], e["path"]))
# relay queue time (RLY q=tx) and NEED/REFUSE index
rlyq = {n: defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e["k"] == "rly" and e["q"] == "tx":
            rlyq[n][e["id"]].append(e["t"])
needidx = {n: defaultdict(list) for n in NODES}
refidx = {n: defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e["k"] == "need":
            needidx[n][e["id"]].append(e)
        elif e["k"] == "refuse":
            refidx[n][e["id"]].append(e)

# group transmissions by id into clusters (gap <= 1800 s between consecutive events of the id)
by_id = defaultdict(list)
for r in tx:
    by_id[r[2]].append(r)
clusters = []
for mid, lst in by_id.items():
    lst.sort()
    cur = [lst[0]]
    for r in lst[1:]:
        if r[0] - cur[-1][0] > 1800:
            clusters.append((mid, cur))
            cur = [r]
        else:
            cur.append(r)
    clusters.append((mid, cur))

dist_all = Counter()
dist_by_typ = defaultdict(Counter)
dist_relay_only = defaultdict(Counter)  # distinct relaying nodes (src=r) per cluster, by type
pair_cnt = Counter()
pair_cnt_by_typ = defaultdict(Counter)
airtime_extra = 0.0


def airtime_ms(plen: int, sf: int = 11, bw: float = 250e3, cr: int = 2) -> float:
    import math
    ts = (2 ** sf) / bw
    tpre = (8 + 4.25) * ts
    de = 1 if ts > 0.016 else 0
    npay = 8 + max(math.ceil((8 * plen - 4 * sf + 28 + 16) / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return (tpre + npay * ts) * 1000


doubles = []
typ_air = Counter()
typ_air_redundant = Counter()
for mid, cl in clusters:
    nodes = {r[1] for r in cl}
    typ = Counter(r[3] for r in cl).most_common(1)[0][0]
    relay_nodes = sorted({r[1] for r in cl if r[4] == "r"})
    gw_nodes = sorted({r[1] for r in cl if r[4] == "g"})
    own_nodes = sorted({r[1] for r in cl if r[4] == "o"})
    dist_all[len(nodes)] += 1
    dist_by_typ[typ][len(nodes)] += 1
    dist_relay_only[typ][len(relay_nodes)] += 1
    # airtime: all transmissions of this id; "redundant" = all but the first per distinct node set minimum 1
    for r in cl:
        typ_air[typ] += airtime_ms(r[6])
    first_air = airtime_ms(cl[0][6])
    for r in cl[1:]:
        typ_air_redundant[typ] += airtime_ms(r[6])
    if len(relay_nodes) >= 2:
        for a, b in combinations(relay_nodes, 2):
            pair_cnt[(a, b)] += 1
            pair_cnt_by_typ[typ][(a, b)] += 1
        # per pair detail: first relay tx of each node
        first = {}
        for r in cl:
            if r[4] == "r" and r[1] not in first:
                first[r[1]] = r
        for a, b in combinations(sorted(first, key=lambda x: first[x][0]), 2):
            ra, rb = first[a], first[b]  # ra earlier
            ta, tb = ra[0], rb[0]
            # did b hear a's copy (last hop == a) before tb?
            heard = [x for x in rxidx[b].get(mid, []) if x[1] == CALL[a] and ta - 1 <= x[0] < tb]
            heard_any_before = [x for x in rxidx[b].get(mid, []) if x[0] < tb and x[1] != CALL[b]]
            q = [t for t in rlyq[b].get(mid, []) if ta - 1800 <= t <= tb]
            tq = max(q) if q else None
            # b heard a's TX only after b queued?  (a's copy arrives ~ ta+0.9)
            heard_while_queued = [x for x in heard if tq is not None and x[0] >= tq]
            ref = [x for x in refidx[b].get(mid, []) if x["relayer"] == CALL[a]]
            needs = needidx[b].get(mid, [])
            mutual = aud[(a, b)] >= 5
            mutual_rev = aud[(b, a)] >= 5
            if heard_while_queued:
                if ref:
                    cls = "heard_while_queued_REFUSE_alone"
                elif needs:
                    cls = "heard_while_queued_no_cancel_" + needs[0]["case"]
                else:
                    cls = "heard_while_queued_no_need_line"
            elif heard:
                cls = "heard_before_queue"
            elif tb - ta < 1.2:
                cls = "simultaneous"
            elif aud[(a, b)] < 5:
                cls = "a_not_audible_at_b"
            else:
                cls = "a_audible_but_not_heard_this_time"
            doubles.append({"id": mid, "typ": typ, "a": a, "b": b, "ta": ta, "tb": tb, "dt": round(tb - ta, 2),
                            "cls": cls, "a_hears_b": aud[(b, a)], "b_hears_a": aud[(a, b)]})

res = {
    "n_clusters": len(clusters),
    "nodes_per_id_all": dict(sorted(dist_all.items())),
    "nodes_per_id_by_type": {t: dict(sorted(c.items())) for t, c in dist_by_typ.items()},
    "relay_nodes_per_id_by_type": {t: dict(sorted(c.items())) for t, c in dist_relay_only.items()},
    "pair_counts": {f"{a}+{b}": v for (a, b), v in pair_cnt.most_common()},
    "pair_counts_by_type": {t: {f"{a}+{b}": v for (a, b), v in c.most_common()} for t, c in pair_cnt_by_typ.items()},
    "audibility_counts_A_to_B": {f"{a}->{b}": v for (a, b), v in sorted(aud.items())},
    "audibility_median_rssi": {f"{a}->{b}": median(v) for (a, b), v in sorted(aud_rssi.items())},
    "airtime_s_by_type_all_tx": {t: round(v / 1000, 1) for t, v in typ_air.items()},
    "airtime_s_by_type_non_first_tx": {t: round(v / 1000, 1) for t, v in typ_air_redundant.items()},
    "double_classes": dict(Counter(d["cls"] for d in doubles)),
    "double_classes_by_type": {t: dict(Counter(d["cls"] for d in doubles if d["typ"] == t)) for t in {d["typ"] for d in doubles}},
}
json.dump(res, open(OUTD / "work" / "redundancy.json", "w"), indent=1)
with open(OUTD / "work" / "doubles.jsonl", "w") as fh:
    for d in doubles:
        fh.write(json.dumps(d) + "\n")
print(json.dumps({k: res[k] for k in ("n_clusters", "nodes_per_id_all", "nodes_per_id_by_type", "relay_nodes_per_id_by_type", "double_classes", "double_classes_by_type", "airtime_s_by_type_all_tx", "airtime_s_by_type_non_first_tx")}, indent=1))
print("pairs", list(res["pair_counts"].items())[:12])
print("aud", res["audibility_counts_A_to_B"])
