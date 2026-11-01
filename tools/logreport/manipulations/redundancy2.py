#!/usr/bin/env python3
"""Q6.2 v2: separate parallel (same hop generation) from chained relays; classify parallel pairs.

Output: work/redundancy2.json, work/pairs2.jsonl
"""
from __future__ import annotations

import json
import math
import sys
from collections import Counter, defaultdict
from itertools import combinations

sys.path.insert(0, str(__import__("pathlib").Path(__file__).parent))
from common import CALL, CALL2NODE, NODES, OUTD, load

ev = {n: load(n) for n in NODES}
THR = 5  # >= THR receptions in 24 h means "audible"


def airtime_ms(plen: int, sf: int = 11, bw: float = 250e3, cr: int = 2) -> float:
    ts = (2 ** sf) / bw
    tpre = (8 + 4.25) * ts
    de = 1 if ts > 0.016 else 0
    npay = 8 + max(math.ceil((8 * plen - 4 * sf + 28 + 16) / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return (tpre + npay * ts) * 1000


aud = Counter()
for b in NODES:
    for e in ev[b]:
        if e["k"] == "rx" and e["path"]:
            a = CALL2NODE.get(e["path"][-1])
            if a and a != b:
                aud[(a, b)] += 1

# RX index per node: id -> [(t, lasthop, dup)]
rxidx = {n: defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e["k"] == "rx":
            rxidx[n][e["id"]].append((e["t"], e["path"][-1] if e["path"] else "", e["dup"]))
ackrx = {n: defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e["k"] == "ack":
            ackrx[n][e["id"]].append(e["t"])
rlyq = {n: defaultdict(list) for n in NODES}
needidx = {n: defaultdict(list) for n in NODES}
refidx = {n: defaultdict(list) for n in NODES}
cancidx = {n: defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e["k"] == "rly" and e["q"] == "tx":
            rlyq[n][e["id"]].append(e["t"])
        elif e["k"] == "need":
            needidx[n][e["id"]].append(e)
        elif e["k"] == "refuse":
            refidx[n][e["id"]].append(e)
        elif e["k"] == "cancel":
            cancidx[n][e["id"]].append(e)

tx = []
for n in NODES:
    for e in ev[n]:
        if e["k"] == "tx" and e["src"] in ("r", "g"):
            tx.append((e["t"], n, e["id"], e["typ"], e["src"], e["hop"], e["len"]))
tx.sort()
by_id = defaultdict(list)
for r in tx:
    by_id[r[2]].append(r)
clusters = []
for mid, lst in by_id.items():
    cur = [lst[0]]
    for r in lst[1:]:
        if r[0] - cur[-1][0] > 1800:
            clusters.append((mid, cur))
            cur = [r]
        else:
            cur.append(r)
    clusters.append((mid, cur))

# per type, per generation parallel stats
out_types = defaultdict(lambda: {"clusters": 0, "tx": 0, "gen_groups": 0, "parallel_extra_tx": 0, "chain_tx": 0})
pair_rows = []
pair_dir = Counter()  # ordered (first, second) parallel pair counts
cls_by_dir = defaultdict(Counter)
cls_all = Counter()
cls_by_typ = defaultdict(Counter)
par_extra_air = Counter()
tx_air = Counter()
maxpar_hist = defaultdict(Counter)  # type -> size of largest same-hop group -> count of clusters
for mid, cl in clusters:
    typ = Counter(r[3] for r in cl).most_common(1)[0][0]
    # each node once (first TX)
    first = {}
    for r in cl:
        first.setdefault(r[1], r)
    recs = sorted(first.values())
    o = out_types[typ]
    o["clusters"] += 1
    o["tx"] += len(recs)
    for r in recs:
        tx_air[typ] += airtime_ms(r[6])
    groups = defaultdict(list)
    for r in recs:
        groups[r[5]].append(r)
    o["gen_groups"] += len(groups)
    maxpar_hist[typ][max(len(g) for g in groups.values())] += 1
    for h, g in groups.items():
        if len(g) >= 2:
            o["parallel_extra_tx"] += len(g) - 1
            for r in sorted(g)[1:]:
                par_extra_air[typ] += airtime_ms(r[6])
            for ra, rb in combinations(sorted(g), 2):
                a, b = ra[1], rb[1]
                ta, tb = ra[0], rb[0]
                pair_dir[(a, b)] += 1
                if typ == "A":
                    heard = [(t, "", True) for t in ackrx[b].get(mid, []) if ta <= t <= tb + 0.2]
                else:
                    heard = [x for x in rxidx[b].get(mid, []) if x[1] == CALL[a] and ta - 1 <= x[0] <= tb + 0.2]
                tq = [t for t in rlyq[b].get(mid, []) if ta - 1800 <= t <= tb]
                tq = max(tq) if tq else None
                ref = [x for x in refidx[b].get(mid, []) if x["relayer"] == CALL[a]]
                needs = needidx[b].get(mid, [])
                if heard:
                    hb = [x for x in heard if x[0] < tb]
                    if hb:
                        if ref:
                            cls = "B_heard_A_while_queued:REFUSE(sole-provider veto)"
                        elif needs and needs[0]["case"] == "A":
                            cls = "B_heard_A_while_queued:caseA_no_refuse_line"
                        elif needs and needs[0]["case"] == "B":
                            cls = "B_heard_A_while_queued:caseB_cover_insufficient"
                        else:
                            cls = "B_heard_A_while_queued:no_need_line"
                    else:
                        cls = "B_heard_A_only_after_own_tx"
                elif tb - ta < 1.5:
                    cls = "simultaneous(<1.5s)"
                elif aud[(a, b)] < THR and typ != "A":
                    cls = "hidden:A_not_audible_at_B"
                elif typ == "A" and not any(0 <= t - ta <= 3 for t in ackrx[b].get(mid, [])) and aud[(a, b)] < THR:
                    cls = "hidden:A_not_audible_at_B"
                else:
                    cls = "audible_pair_but_A_copy_not_received"
                cls_all[cls] += 1
                cls_by_typ[typ][cls] += 1
                cls_by_dir[(a, b)][cls] += 1
                pair_rows.append({"id": mid, "typ": typ, "a": a, "b": b, "dt": round(tb - ta, 2), "hop": h, "cls": cls})
    # chain tx = tx in generations other than the first
    o["chain_tx"] += len(recs) - len(groups.get(max(groups), []))

res = {
    "thr_audible": THR,
    "types": {t: dict(v) for t, v in out_types.items()},
    "largest_parallel_group_hist": {t: dict(sorted(c.items())) for t, c in maxpar_hist.items()},
    "parallel_pair_classes": dict(cls_all),
    "parallel_pair_classes_by_type": {t: dict(c) for t, c in cls_by_typ.items()},
    "parallel_pairs_by_direction": {f"{a}->{b}": {"n": v, "classes": dict(cls_by_dir[(a, b)])} for (a, b), v in pair_dir.most_common()},
    "airtime_s_all_tx_by_type": {t: round(v / 1000, 1) for t, v in tx_air.items()},
    "airtime_s_parallel_extra_by_type": {t: round(v / 1000, 1) for t, v in par_extra_air.items()},
    "audibility_A_to_B": {f"{a}->{b}": v for (a, b), v in sorted(aud.items())},
}
json.dump(res, open(OUTD / "work" / "redundancy2.json", "w"), indent=1)
with open(OUTD / "work" / "pairs2.jsonl", "w") as fh:
    for d in pair_rows:
        fh.write(json.dumps(d) + "\n")
print(json.dumps({k: res[k] for k in ("types", "largest_parallel_group_hist", "parallel_pair_classes", "parallel_pair_classes_by_type", "airtime_s_all_tx_by_type", "airtime_s_parallel_extra_by_type")}, indent=1))
for k, v in list(res["parallel_pairs_by_direction"].items())[:20]:
    print(k, v)
print(res["audibility_A_to_B"])
