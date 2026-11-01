#!/usr/bin/env python3
"""DB0AIS-12 (standalone, DL mesh) {CET} analysis. Node-clock time base (second resolution).
Usage: python3 -I analyze_db.py <q5_dir> <out_json>"""
import json
import math
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from statistics import median

from common import OWNCALL, cont_epoch, load, pct, stats
from nodeset import NORM as NORM_DIR, OUT as OUT_DIR

NODE = sys.argv[1]  # standalone node (log without logger wall clock)
D = str(OUT_DIR / "data")
ev = load(D, NODE)
OWN = OWNCALL[NODE]


def airtime_ms(L: int) -> float:
    tsym = 2 ** 11 / 250.0
    npay = 8 + max(math.ceil((8 * L - 4 * 11 + 28 + 16) / (4 * 11)), 0) * 6
    return (npay + 8.25) * tsym


res = {}
# msg_ids of -33 and -63 collide (same counter family), so every key is (source, id)
content = {}
for e in ev:
    if "cont" in e and e["cont"][:1] != "<":
        content.setdefault((e["path"][0], e["id"]), (e["path"][0], e["cont"], cont_epoch(e["cont"])))
res["sources"] = dict(Counter(v[0] for v in content.values()))
res["id_collisions_between_sources"] = len({k[1] for k in content}) - len(content)
# attach the source to txmeta lines from the TX-LoRa line that follows them
order = sorted(range(len(ev)), key=lambda k: ev[k]["ln"])
for pos, k in enumerate(order):
    e = ev[k]
    if e["k"] == "txmeta":
        for k2 in order[pos + 1:pos + 40]:
            if ev[k2]["k"] == "tx" and ev[k2]["id"] == e["id"]:
                e["srcc"] = ev[k2]["path"][0]
                e["path"] = ev[k2]["path"]
                break
        else:
            e["srcc"] = None
res["wrong_time_cet_lt"] = len([e for e in ev if "cont" in e and e["cont"][:1] == "<"])
t0 = min(e["t"] for e in ev if "cont" in e)
t1 = max(e["t"] for e in ev if "cont" in e)
res["window_utc"] = [datetime.fromtimestamp(t0, timezone.utc).isoformat(), datetime.fromtimestamp(t1, timezone.utc).isoformat()]
hours = (t1 - t0) / 3600.0
res["hours"] = round(hours, 2)

udp = {(e["path"][0], e["id"]): e["t"] for e in ev if e["k"] == "udp"}
rf = [e for e in ev if e["k"] == "rf" and e.get("fcs") != "0000" and e["cont"][:1] != "<"]
res["rf_fcs0000"] = len([e for e in ev if e["k"] == "rf" and e.get("fcs") == "0000"])
by_src = defaultdict(lambda: {"ids": set(), "udp": 0, "rf_frames": 0, "rf_ids": set(), "tx": 0})
for k, (s, c, ce) in content.items():
    by_src[s]["ids"].add(k)
for k in udp:
    if k in content:
        by_src[content[k][0]]["udp"] += 1
for e in rf:
    s = e["path"][0]
    by_src[s]["rf_frames"] += 1
    by_src[s]["rf_ids"].add((s, e["id"]))
txm = [e for e in ev if e["k"] == "txmeta"]
for e in txm:
    if e.get("srcc"):
        by_src[e["srcc"]]["tx"] += 1
def ck(e):
    return (e["path"][0], e["id"])
res["by_source"] = {s: {"distinct_ids": len(v["ids"]), "udp_injected": v["udp"], "rf_frames": v["rf_frames"],
                         "rf_distinct_ids": len(v["rf_ids"]), "own_tx": v["tx"]} for s, v in by_src.items()}

# cadence per source (content timestamps)
cad = {}
for s in by_src:
    cs = sorted(content[i][2] for i in by_src[s]["ids"])
    g = [b - a for a, b in zip(cs, cs[1:])]
    cad[s] = {"n": len(cs), "gap_s": stats(g, 1), "gaps_gt_900": [(datetime.fromtimestamp(a, timezone.utc).strftime("%m-%d %H:%M"), round(b - a)) for a, b in zip(cs, cs[1:]) if b - a > 900]}
res["cadence"] = cad
# -62 UDP reception cadence
u62 = sorted(t for k, t in udp.items() if k in content and content[k][0] == "OE1XAR-62")
res["udp62_arrival_gap_s"] = stats([b - a for a, b in zip(u62, u62[1:])], 1)
res["udp_age_s"] = stats([udp[k] - content[k][2] for k in udp if k in content], 1)

# RF copies per id per source
for s in by_src:
    cnt = Counter()
    for e in rf:
        if e["path"][0] == s:
            cnt[e["id"]] += 1
    res["by_source"][s]["rf_copies_per_id"] = stats(list(cnt.values()), 1)
    ages = [e["t"] - content[ck(e)][2] for e in rf if e["path"][0] == s]
    res["by_source"][s]["rf_age_s"] = stats(ages, 1)
    res["by_source"][s]["rf_age_gt150_pct"] = round(100.0 * len([a for a in ages if a > 150]) / max(1, len(ages)), 1)
    res["by_source"][s]["rf_age_gt150_n"] = len([a for a in ages if a > 150])
    res["by_source"][s]["rf_path_len_dist"] = dict(sorted(Counter(len(e["path"]) - 1 for e in rf if e["path"][0] == s).items()))
    res["by_source"][s]["rf_stale_lasthop_top"] = Counter(e["path"][-1] for e in rf if e["path"][0] == s and e["t"] - content[ck(e)][2] > 150).most_common(6)
    res["by_source"][s]["first_via_top"] = Counter(e["path"][1] for e in rf if e["path"][0] == s and len(e["path"]) >= 2).most_common(6)

# which telegrams are first heard by what: for -33/-63 via RF only
# ids heard on RF by id: first copy age
first_age = {}
for e in sorted(rf, key=lambda x: x["t"]):
    first_age.setdefault(ck(e), e["t"] - content[ck(e)][2])
for s in by_src:
    v = [first_age[i] for i in by_src[s]["ids"] if i in first_age]
    res["by_source"][s]["first_heard_age_s"] = stats(v, 1)
    res["by_source"][s]["first_heard_age_gt150_n"] = len([a for a in v if a > 150])

# overlap: sources by 10-min slot; are -33/-63 telegrams ever the only time signal (i.e. does -62 UDP cover)
# TX
res["tx_total"] = len(txm)
res["tx_src_dist"] = dict(Counter(e["src"] for e in txm))
res["tx_prio_dist"] = dict(Counter(e["prio"] for e in txm))
res["tx_wait_ms"] = stats([e["wait"] for e in txm], 0)
res["tx_wait_ms_src_g"] = stats([e["wait"] for e in txm if e["src"] == "g"], 0)
res["tx_wait_ms_src_r"] = stats([e["wait"] for e in txm if e["src"] == "r"], 0)
res["tx_by_source_and_origin"] = {"%s/%s" % k: v for k, v in Counter((e.get("srcc") or "?", e["src"]) for e in txm).items()}
ages_tx = [e["t"] - content[(e["srcc"], e["id"])][2] for e in txm if e.get("srcc") and (e["srcc"], e["id"]) in content]
res["tx_age_s"] = stats(ages_tx, 1)
res["tx_age_gt150_n"] = len([a for a in ages_tx if a > 150])
res["tx_age_gt120_n"] = len([a for a in ages_tx if a > 120])
cets = [e for e in ev if e["k"] == "cet"]
res["cet_lines"] = dict(Counter(e["kind"] for e in cets))
txa = [int(e["age_s"]) for e in cets if e["kind"] == "tx" and e["age_s"] != "na"]
res["cet_tx_age_node_s"] = stats(txa, 0)
res["cet_tx_age_gt150"] = len([a for a in txa if a > 150])
res["cet_tx_wait_ms"] = stats([int(e["wait_ms"]) for e in cets if e["kind"] == "tx"], 0)
rxa = [int(e["age_s"]) for e in cets if e["kind"] == "rx" and e["age_s"] != "na"]
res["cet_rx_age_node_s"] = stats(rxa, 0)
res["cet_rx_age_gt150_n"] = len([a for a in rxa if a > 150])
res["cet_rx_hop_dist"] = dict(Counter(e["hop"] for e in cets if e["kind"] == "rx"))
res["cet_rx_by_src"] = dict(Counter(e["src"] for e in cets if e["kind"] == "rx"))
res["cet_drops"] = [(e["where"], e["age_s"]) for e in cets if e["kind"] == "drop"]
res["gw_rx_age_dist"] = dict(sorted(Counter(int(e["age_s"]) for e in cets if e["kind"] == "gw_rx" and e["age_s"] != "na").items()))
res["gw_rx_src"] = dict(Counter(e["src"] for e in cets if e["kind"] == "gw_rx"))
res["ring_drop_stale_cet"] = len([e for e in ev if e["k"] == "ringany" and "reason=cet" in e["line"]])
res["trusted_flags"] = dict(Counter(e["trusted"] for e in cets if "trusted" in e))
# stale TX: list
bad = [(datetime.fromtimestamp(e["t"], timezone.utc).strftime("%m-%d %H:%M:%S"), e["srcc"], round(e["t"] - content[(e["srcc"], e["id"])][2]), e["src"], e["wait"]) for e in txm if e.get("srcc") and (e["srcc"], e["id"]) in content and e["t"] - content[(e["srcc"], e["id"])][2] > 100]
res["tx_age_gt100_list"] = bad[:20]
res["tx_age_gt100_n"] = len(bad)

# TX after overheard
rfby = defaultdict(list)
for e in rf:
    rfby[ck(e)].append(e)
after = 0
n_g = 0
for e in txm:
    if e["src"] != "g":
        continue
    n_g += 1
    if [x for x in rfby.get((e.get("srcc"), e["id"]), []) if x["t"] < e["t"]]:
        after += 1
res["tx_src_g_after_overheard"] = [after, n_g]

# airtime budget: CET rx and tx airtime per hour
rx_air = sum(airtime_ms(e["len"]) for e in rf)
tx_air = sum(airtime_ms(e["len"]) for e in txm)
res["cet_rx_airtime_s_per_h"] = round(rx_air / 1000 / hours, 1)
res["cet_tx_airtime_s_per_h"] = round(tx_air / 1000 / hours, 1)
res["cet_rf_frames_per_h"] = round(len(rf) / hours, 1)
res["cet_tx_per_h"] = round(len(txm) / hours, 1)
res["cet_tx_by_source_per_h"] = {s: round(v["tx"] / hours, 2) for s, v in by_src.items()}
# STAT
stat = []
with open(NORM_DIR / f"{NODE}.log", "rb") as f:
    pat = re.compile(rb"\[LOG\] STAT util=(\d+) rx=(\d+) tx=(\d+) .* txn=(\d+) .* ringmax=(\d+)/20 drop=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)")
    for line in f:
        if b"[LOG] STAT" in line:
            m = pat.search(line)
            if m:
                stat.append(tuple(int(x) for x in m.groups()))
res["stat_windows"] = len(stat)
res["stat_util_median"] = median(s[0] for s in stat)
res["stat_rx_s_per_h"] = round(sum(s[1] for s in stat) / 1000 / (len(stat) * 300 / 3600), 1)
res["stat_tx_s_per_h"] = round(sum(s[2] for s in stat) / 1000 / (len(stat) * 300 / 3600), 1)
res["stat_txn_total"] = sum(s[3] for s in stat)
res["cet_share_of_stat_tx_pct"] = round(100.0 * tx_air / max(1, sum(s[2] for s in stat)) * (len(stat) * 300 / 3600) / hours, 1)
res["cet_share_of_stat_rx_pct"] = round(100.0 * rx_air / max(1, sum(s[1] for s in stat)) * (len(stat) * 300 / 3600) / hours, 1)
res["cet_share_of_tx_count_pct"] = round(100.0 * len(txm) / max(1, sum(s[3] for s in stat)) * (len(stat) * 300 / 3600) / hours, 1)
res["stat_ringmax_p50_p90_max"] = [median(s[4] for s in stat), pct([s[4] for s in stat], 90), max(s[4] for s in stat)]
res["stat_drop_total_prio1to5"] = [sum(s[5 + k] for s in stat) for k in range(5)]

# redundancy of sources: 10-min slots covering ids of different source
# For every -62 UDP telegram: time of the nearest -33 and -63 telegram content
slots = defaultdict(set)
for k, (s, c, ce) in content.items():
    slots[int(ce // 600)].add(s)
res["slots_total"] = len(slots)
res["slots_by_source_combo"] = {"+".join(k): v for k, v in Counter(tuple(sorted(v)) for v in slots.values()).items()}
json.dump(res, open(f"{D}/db_analysis_{NODE}.json", "w"), indent=1, default=str)
print(json.dumps(res, indent=1, default=str)[:9000])
