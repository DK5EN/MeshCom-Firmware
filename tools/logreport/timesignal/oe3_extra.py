#!/usr/bin/env python3
"""OE3 extras: union of distinct radiations (id,path), their age/class, per-id gateway counts,
who-hears-whom among our five gateways, TX collisions, node-based [CET];tx age join.

Run via run.py; writes <out>/data/oe3_extra.json."""
import json
import math
from collections import Counter, defaultdict
from datetime import datetime, timezone
from statistics import median

from common import CLUSTER, OWNCALL, cont_epoch, load, pct, stats, udp_arrivals
from nodeset import OUT as OUT_DIR

D = str(OUT_DIR / "data")
EV = {n: load(D, n) for n in CLUSTER}
OURS = {OWNCALL[n]: n for n in CLUSTER}


def airtime_ms(L: int) -> float:
    tsym = 2 ** 11 / 250.0
    npay = 8 + max(math.ceil((8 * L - 4 * 11 + 28 + 16) / (4 * 11)), 0) * 6
    return (npay + 8.25) * tsym


content = {}
for n in CLUSTER:
    for e in EV[n]:
        if "cont" in e:
            content.setdefault(e["id"], cont_epoch(e["cont"]))
ids = sorted(content, key=lambda i: content[i])
res = {}

# --- window where all five nodes logged
t_lo = max(min(e["t"] for e in EV[n]) for n in CLUSTER)
t_hi = min(max(e["t"] for e in EV[n]) for n in CLUSTER)
first_udp = {}
for n in CLUSTER:
    for i, t in udp_arrivals(EV[n]).items():
        first_udp[i] = min(first_udp.get(i, t), t)
W = [i for i in ids if t_lo <= first_udp.get(i, 0) <= t_hi]
res["all5_window_ids"] = len(W)
res["all5_window_utc"] = [datetime.fromtimestamp(t_lo, timezone.utc).isoformat(), datetime.fromtimestamp(t_hi, timezone.utc).isoformat()]
Wset = set(W)

# --- distinct radiations: key (id, path); time = earliest time any node logged it (reception end ~ tx end)
rad = {}
for n in CLUSTER:
    for e in EV[n]:
        if e["k"] == "rf" and e.get("fcs") != "0000" and e["id"] in Wset:
            key = (e["id"], tuple(e["path"]))
            r = rad.setdefault(key, {"t": e["t"], "len": e["len"], "heard_by": set(), "h": e["h"]})
            r["t"] = min(r["t"], e["t"])
            r["heard_by"].add(n)
# add our own TX (path = SRC, own) that nobody of ours heard
for n in CLUSTER:
    for e in EV[n]:
        if e["k"] == "txmeta" and e["id"] in Wset and e["src"] == "g":
            key = (e["id"], ("OE1XAR-33", OWNCALL[n]))
            if key not in rad:
                rad[key] = {"t": e["t"] + 0.7, "len": e["len"], "heard_by": set(), "h": e["h"], "own_only": True}
            else:
                rad[key]["own_tx"] = True
res["distinct_radiations_total"] = len(rad)
by_len = Counter(len(k[1]) - 1 for k in rad)
res["radiations_by_vias"] = dict(sorted(by_len.items()))
res["radiations_per_telegram"] = stats([sum(1 for k in rad if k[0] == i) for i in W], 1)
ours_n = lambda k: k[1][1] in OURS and len(k[1]) == 2
foreign_gw = lambda k: len(k[1]) == 2 and k[1][1] not in OURS
relay = lambda k: len(k[1]) >= 3
cls = {"our_gateway": [k for k in rad if ours_n(k)], "foreign_gateway": [k for k in rad if foreign_gw(k)], "relay": [k for k in rad if relay(k)]}
res["radiation_class_counts"] = {c: len(v) for c, v in cls.items()}
res["radiation_class_per_telegram_mean"] = {c: round(len(v) / len(W), 2) for c, v in cls.items()}
# airtime of all distinct radiations
tot = sum(airtime_ms(r["len"]) for r in rad.values())
res["radiation_airtime_s_total"] = round(tot / 1000, 1)
hours = (t_hi - t_lo) / 3600.0
res["window_hours"] = round(hours, 2)
res["radiation_airtime_s_per_h"] = round(tot / 1000 / hours, 1)
res["radiation_airtime_pct_of_hour"] = round(100.0 * tot / 1000 / hours / 3600, 2)
res["radiation_airtime_by_class_s_per_h"] = {c: round(sum(airtime_ms(rad[k]["len"]) for k in v) / 1000 / hours, 1) for c, v in cls.items()}
res["radiation_airtime_per_telegram_s"] = round(tot / 1000 / len(W), 2)

# --- age of radiations (reception-end time minus ~0.75 s airtime ~ tx start, plus content second truncation)
def age_of(k):
    return rad[k]["t"] - content[k[0]]
ages = {c: [age_of(k) for k in v] for c, v in cls.items()}
res["radiation_age_s"] = {c: stats(a, 1) for c, a in ages.items()}
allages = [a for v in ages.values() for a in v]
res["radiation_age_s"]["all"] = stats(allages, 1)
res["radiation_age_gt150"] = {c: [len([a for a in v if a > 150]), len(v), round(100.0 * len([a for a in v if a > 150]) / max(1, len(v)), 1)] for c, v in ages.items()}
res["radiation_age_gt150"]["all"] = [len([a for a in allages if a > 150]), len(allages), round(100.0 * len([a for a in allages if a > 150]) / len(allages), 1)]
res["radiation_age_gt300_pct"] = round(100.0 * len([a for a in allages if a > 300]) / len(allages), 1)
res["radiation_age_gt600_pct"] = round(100.0 * len([a for a in allages if a > 600]) / len(allages), 1)
res["radiation_age_gt1200_n"] = len([a for a in allages if a > 1200])
# telegrams for which at least one stale radiation exists
st_ids = {k[0] for k in rad if age_of(k) > 150}
res["telegrams_with_stale_radiation"] = [len(st_ids), len(W)]
# offenders: relays with last-hop and stale fraction
lh = defaultdict(lambda: [0, 0, []])
for k in cls["relay"] + cls["foreign_gateway"]:
    x = lh[k[1][-1]]
    x[0] += 1
    a = age_of(k)
    if a > 150:
        x[1] += 1
        x[2].append(a)
off = []
for c, (n_all, n_st, a) in lh.items():
    if n_st >= 5:
        off.append({"call": c, "radiations": n_all, "stale": n_st, "stale_pct": round(100.0 * n_st / n_all), "stale_age_p50": round(median(a)), "stale_age_p90": round(pct(a, 90)), "stale_age_max": round(max(a))})
res["stale_relay_offenders"] = sorted(off, key=lambda x: -x["stale"])
res["stale_radiations_share_from_offenders_pct"] = round(100.0 * sum(o["stale"] for o in off if o["stale_pct"] >= 80) / max(1, sum(1 for a in allages if a > 150)), 1)
# age of relay radiations by hop path len
res["relay_age_by_vias_p50_p90"] = {}
for v in (2, 3, 4):
    ages_v = [age_of(k) for k in cls["relay"] if len(k[1]) - 1 == v]
    if ages_v:  # a hop count without any relay copy is left out
        res["relay_age_by_vias_p50_p90"][str(v)] = [round(median(ages_v), 1), round(pct(ages_v, 90), 1), len(ages_v)]
# long-tail >1200: who
res["very_old_gt600_lasthops"] = Counter(k[1][-1] for k in rad if age_of(k) > 600).most_common(10)
res["very_old_gt600_n"] = len([1 for k in rad if age_of(k) > 600])

# --- gateways radiating per telegram (distinct first via) and radiators
gwp = defaultdict(set)
for k in rad:
    gwp[k[0]].add(k[1][1])
gcount = [len(gwp[i]) for i in W]
fcount = [len([g for g in gwp[i] if g not in OURS]) for i in W]
res["gateways_per_telegram"] = stats(gcount, 1)
res["gateways_per_telegram_dist"] = dict(sorted(Counter(gcount).items()))
res["foreign_gateways_per_telegram"] = stats(fcount, 1)
gwtot = Counter()
for i in W:
    for g in gwp[i]:
        gwtot[g] += 1
res["gateway_presence_per_telegram_pct"] = [(g, round(100.0 * c / len(W))) for g, c in sorted(gwtot.items(), key=lambda kv: (-kv[1], kv[0]))[:25]]  # ties alphabetical (deterministic)
res["distinct_gateways_seen"] = len(gwtot)
# relays per telegram (distinct last hops among len>=3)
rl = defaultdict(set)
for k in rad:
    if len(k[1]) >= 3:
        rl[k[0]].add(k[1][-1])
res["distinct_relay_nodes_per_telegram"] = stats([len(rl[i]) for i in W], 1)
res["relay_paths_per_telegram"] = stats([len([k for k in rad if k[0] == i and len(k[1]) >= 3]) for i in W], 1)

# --- who hears whom among the five gateways (gateway radiation, path len 2)
hm = {a: {b: [0, 0] for b in CLUSTER} for a in CLUSTER}  # a radiated, b heard: [heard, tx]
txs = {n: {e["id"]: e for e in EV[n] if e["k"] == "txmeta" and e["id"] in Wset and e["src"] == "g"} for n in CLUSTER}
for a in CLUSTER:
    for i in txs[a]:
        for b in CLUSTER:
            if b == a:
                continue
            hm[a][b][1] += 1
            if (i, ("OE1XAR-33", OWNCALL[a])) in rad and b in rad[(i, ("OE1XAR-33", OWNCALL[a]))]["heard_by"]:
                hm[a][b][0] += 1
res["hears_gateway_radiation_pct"] = {a: {b: round(100.0 * h / max(1, t)) for b, (h, t) in hm[a].items() if b != a} for a in CLUSTER}

# --- TX overlap among our five (airtime ~ 0.73 s): pairs within <0.75 s
coll = 0
close = 0
pairs = Counter()
for i in W:
    ts = sorted((txs[n][i]["t"], n) for n in CLUSTER if i in txs[n])
    for (t1, n1), (t2, n2) in zip(ts, ts[1:]):
        if t2 - t1 < 0.75:
            coll += 1
            pairs[(n1, n2)] += 1
        if t2 - t1 < 3.0:
            close += 1
res["our_tx_pairs_overlapping_airtime_lt0.75s"] = coll
res["our_tx_pairs_within_3s"] = close
res["our_tx_overlap_pairs"] = {f"{a}-{b}": c for (a, b), c in pairs.items()}
# TX start spread per telegram among our gateways
spread = []
ntx = []
for i in W:
    ts = [txs[n][i]["t"] for n in CLUSTER if i in txs[n]]
    ntx.append(len(ts))
    if len(ts) > 1:
        spread.append(max(ts) - min(ts))
res["our_tx_per_telegram"] = dict(sorted(Counter(ntx).items()))
res["our_tx_spread_first_to_last_s"] = stats(spread, 1)
# delay from first UDP push to first radiation by any of ours, and to first heard radiation on air
fr = []
for i in W:
    ts = [txs[n][i]["t"] for n in CLUSTER if i in txs[n]]
    if ts:
        fr.append(min(ts) - first_udp[i])
res["first_own_radiation_after_push_s"] = stats(fr, 1)

# --- node-based [CET];tx age join (nearest preceding cet tx line within 0.25 s)
nb = {}
for n in CLUSTER:
    cets = [e for e in EV[n] if e["k"] == "cet" and e["kind"] == "tx"]
    txm = [e for e in EV[n] if e["k"] == "txmeta"]
    cets.sort(key=lambda e: e["t"])
    ages_n = []
    ages_l = []
    j = 0
    for e in sorted(txm, key=lambda x: x["t"]):
        while j + 1 < len(cets) and cets[j + 1]["t"] <= e["t"]:
            j += 1
        c = cets[j] if cets else None
        if c and 0 <= e["t"] - c["t"] < 0.25 and c["age_s"] != "na":
            ages_n.append(int(c["age_s"]))
            ages_l.append(e["t"] - content[e["id"]])
    diff = [l - a for l, a in zip(ages_l, ages_n)]
    nb[n] = {"n_joined": len(ages_n), "node_age": stats(ages_n, 0), "logger_minus_node_age_s": stats(diff, 2), "node_age_gt150": len([a for a in ages_n if a > 150]), "node_age_max": max(ages_n) if ages_n else None}
res["tx_age_node_based_join"] = nb


# --- usefulness of a gateway radiation: followed by a relay of that very path (someone re-radiated it)
relayed_after = defaultdict(int)
for k in rad:
    if len(k[1]) >= 3:
        relayed_after[(k[0], k[1][1])] += 1
use = {"our_gateway": [0, 0], "foreign_gateway": [0, 0]}
for c in ("our_gateway", "foreign_gateway"):
    for k in cls[c]:
        use[c][1] += 1
        if relayed_after.get((k[0], k[1][1]), 0) > 0:
            use[c][0] += 1
res["gateway_radiation_followed_by_relay"] = {c: [a, b, round(100.0 * a / max(1, b), 1)] for c, (a, b) in use.items()}
# ours: TX after overheard AND followed by relay
by_node = {}
for n in CLUSTER:
    tot = fol = 0
    for i in W:
        if i in txs[n]:
            tot += 1
            if relayed_after.get((i, OWNCALL[n]), 0) > 0:
                fol += 1
    by_node[n] = [fol, tot, round(100.0 * fol / max(1, tot), 1)]
res["our_tx_followed_by_relay_per_node"] = by_node

json.dump(res, open(D + "/oe3_extra.json", "w"), indent=1, default=str)
for k, v in res.items():
    print(k, ":", json.dumps(v, default=str)[:900])
