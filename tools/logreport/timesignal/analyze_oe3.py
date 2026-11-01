#!/usr/bin/env python3
"""OE3 cluster analysis of the {CET} time signal (OE1XAR-33 only source seen there).

Run via run.py. Reads <out>/data/events_<node>.jsonl (extract_cet.py) and STAT lines from <norm>/<node>.log;
writes <out>/data/oe3_analysis.json.
"""
import json
import math
import re
from collections import Counter, defaultdict
from statistics import median, mean

from common import LOGGER_UTC_OFF_S, CLUSTER, OWNCALL, cont_epoch, load, pct, stats, udp_arrivals
from nodeset import NORM as NORM_DIR, OUT as OUT_DIR

NORM = str(NORM_DIR)
D = str(OUT_DIR / "data")
OUT = D + "/oe3_analysis.json"
OURS = {OWNCALL[n] for n in CLUSTER}


def airtime_ms(L: int) -> float:
    """SF11 BW250 CR4/6, explicit header, CRC, 4 preamble symbols (fitted: 61 B -> 724 ms measured)."""
    tsym = 2 ** 11 / 250.0
    npay = 8 + max(math.ceil((8 * L - 4 * 11 + 28 + 16) / (4 * 11)), 0) * 6
    return (npay + 8.25) * tsym


def load_stat(node: str):
    """[(logger_utc_epoch, rx_ms, tx_ms, util, txn)] from [LOG] STAT lines (5-min windows)."""
    from datetime import datetime, timezone
    rows = []
    rx = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\.\d{3}  \d\d:\d\d:\d\d \[LOG\] STAT util=(\d+) rx=(\d+) tx=(\d+) .* txn=(\d+)")
    with open(f"{NORM}/{node}.log", "rb") as f:
        for line in f:
            if b"[LOG] STAT" not in line:
                continue
            m = rx.match(line.decode("latin1"))
            if m:
                t = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp() - LOGGER_UTC_OFF_S
                rows.append((t, int(m.group(3)), int(m.group(4)), int(m.group(2)), int(m.group(5))))
    return rows


EV = {n: load(D, n) for n in CLUSTER}

# ---------------- global content map (CLUSTER)
content = {}
for n in CLUSTER:
    for e in EV[n]:
        if "cont" in e and e["cont"][:1] != "<":
            content.setdefault(e["id"], (e["path"][0], e["cont"], cont_epoch(e["cont"])))
wrong = sum(1 for n in CLUSTER for e in EV[n] if "cont" in e and e["cont"][:1] == "<")
ids = sorted(content, key=lambda i: content[i][2])
srcs = Counter(v[0] for v in content.values())

res = {"network": {}, "nodes": {}}
net = res["network"]
net["sources"] = dict(srcs)
net["distinct_ids"] = len(ids)
net["wrong_time_cet_lt"] = wrong

# ---------------- cadence from content timestamps
ce = [content[i][2] for i in ids]
gaps = [b - a for a, b in zip(ce, ce[1:])]
net["content_gap_s"] = stats(gaps, 1)
idn = [int(i, 16) for i in ids]
idgaps = [b - a for a, b in zip(idn, idn[1:])]
net["id_step_counter"] = dict(Counter(idgaps).most_common(6))
net["content_first"] = content[ids[0]][1]
net["content_last"] = content[ids[-1]][1]
span = ce[-1] - ce[0]
net["expected_telegrams_at_median_gap"] = round(span / median(gaps)) + 1
big = [(content[a][1], round(g)) for a, b, g in zip(ids, ids[1:], gaps) if g > 1.5 * median(gaps)]
net["content_gaps_gt_1p5_median"] = big
# gap drift: content_gap vs 600
net["content_gap_minus_600_s"] = stats([g - 600 for g in gaps if g < 900], 1)

# ---------------- UDP injection per node
udp = {n: udp_arrivals(EV[n]) for n in CLUSTER}
first_udp = {}
for i in ids:
    ts = [udp[n][i] for n in CLUSTER if i in udp[n]]
    if ts:
        first_udp[i] = min(ts)
spread = []
for i in ids:
    ts = [udp[n][i] for n in CLUSTER if i in udp[n]]
    if len(ts) > 1:
        spread.append(max(ts) - min(ts))
net["udp_push_spread_across_gateways_s"] = stats(spread, 3)
net["udp_ids_with_any_gateway"] = len(first_udp)
net["udp_age_s_logger_based"] = stats([udp[n][i] - content[i][2] for n in CLUSTER for i in udp[n] if i in content], 2)
ugaps = [b - a for a, b in zip(*[sorted(first_udp[i] for i in first_udp)] * 2)] if False else None
fu = sorted(first_udp.values())
net["udp_first_arrival_gap_s"] = stats([b - a for a, b in zip(fu, fu[1:])], 1)
net["udp_first_arrival_gaps_gt_900s"] = [(round(b - a)) for a, b in zip(fu, fu[1:]) if b - a > 900]

# ---------------- per node: heard / missed
for n in CLUSTER:
    ev = EV[n]
    r = res["nodes"].setdefault(n, {})
    rf = [e for e in ev if e["k"] == "rf" and e.get("fcs") != "0000" and e["cont"][:1] != "<"]
    rfids = {e["id"] for e in rf}
    # window where the node was logging
    t0 = min(e["t"] for e in ev)
    t1 = max(e["t"] for e in ev)
    expected = [i for i in ids if t0 - 5 <= first_udp.get(i, content[i][2] + 3600) and first_udp.get(i, content[i][2] + 3600) <= t1 + 5]
    r["window_utc"] = [t0, t1]
    r["expected_in_window"] = len(expected)
    r["udp_got"] = len([i for i in expected if i in udp[n]])
    r["udp_missed"] = len([i for i in expected if i not in udp[n]])
    r["udp_missed_ids_heard_on_rf"] = len([i for i in expected if i not in udp[n] and i in rfids])
    r["not_heard_at_all"] = len([i for i in expected if i not in udp[n] and i not in rfids])
    r["rf_valid_frames"] = len(rf)
    r["rf_fcs0000_frames"] = len([e for e in ev if e["k"] == "rf" and e.get("fcs") == "0000"])
    # reception cadence of own UDP arrivals
    ua = sorted(udp[n].values())
    r["udp_arrival_gap_s"] = stats([b - a for a, b in zip(ua, ua[1:])], 1)
    r["udp_arrival_gaps_gt_900s"] = [round(b - a) for a, b in zip(ua, ua[1:]) if b - a > 900]
    # per node UDP minus first_udp: who is early/late
    lag = [udp[n][i] - first_udp[i] for i in udp[n] if i in first_udp]
    r["udp_lag_behind_first_gateway_s"] = stats(lag, 3)

# ---------------- RF copies per (node, id)
for n in CLUSTER:
    ev = EV[n]
    rf = [e for e in ev if e["k"] == "rf" and e.get("fcs") != "0000" and e["cont"][:1] != "<"]
    byid = defaultdict(list)
    for e in rf:
        byid[e["id"]].append(e)
    r = res["nodes"][n]
    cnt = [len(v) for v in byid.values()]
    r["rf_copies_per_id"] = stats(cnt, 1)
    r["rf_copies_per_id_mean"] = round(mean(cnt), 2) if cnt else None
    r["rf_distinct_paths_per_id"] = stats([len({tuple(e["path"]) for e in v}) for v in byid.values()], 1)
    r["rf_distinct_lasthop_per_id"] = stats([len({e["path"][-1] for e in v}) for v in byid.values()], 1)
    r["rf_path_len_dist"] = dict(sorted(Counter(len(e["path"]) - 1 for e in rf).items()))
    r["rf_H_dist"] = dict(sorted(Counter(e["h"] for e in rf).items()))
    r["rf_dup_flag"] = dict(Counter(e["dup"] for e in rf))
    r["rf_own_in_path"] = dict(Counter(e["own"] for e in rf))
    r["rf_rssi_snr_pathlen1"] = {"rssi_p50": median(e["rssi"] for e in rf if len(e["path"]) == 2) if any(len(e["path"]) == 2 for e in rf) else None}
    # age of every received copy (logger clock vs content, UTC)
    ages = [e["t"] - content[e["id"]][2] for e in rf]
    r["rf_age_s"] = stats(ages, 1)
    r["rf_age_gt150_n"] = len([a for a in ages if a > 150])
    r["rf_age_gt150_pct"] = round(100.0 * len([a for a in ages if a > 150]) / len(ages), 2) if ages else None
    r["rf_age_gt600_n"] = len([a for a in ages if a > 600])
    stale = Counter(e["path"][-1] for e in rf if e["t"] - content[e["id"]][2] > 150)
    r["rf_stale_lasthop_top"] = stale.most_common(8)
    r["rf_age_neg_n"] = len([a for a in ages if a < -2])

# ---------------- TX of {CET} per node
for n in CLUSTER:
    ev = EV[n]
    txm = [e for e in ev if e["k"] == "txmeta"]
    txp = {e["id"]: e for e in ev if e["k"] == "tx"}
    cetx = {int(e["ms"]): e for e in ev if e["k"] == "cet" and e["kind"] == "tx"}
    dur = {e["id"]: e["dur_ms"] for e in ev if e["k"] == "txdur"}
    r = res["nodes"][n]
    r["tx_count"] = len(txm)
    r["tx_src_dist"] = dict(Counter(e["src"] for e in txm))
    r["tx_prio_dist"] = dict(Counter(e["prio"] for e in txm))
    r["tx_wait_ms"] = stats([e["wait"] for e in txm], 0)
    r["tx_wait_ms_gateway_src"] = stats([e["wait"] for e in txm if e["src"] == "g"], 0)
    r["tx_q_after_pick"] = stats([e["q"] for e in txm], 0)
    r["tx_cad_attempts"] = dict(Counter(e["cad"] for e in txm))
    r["tx_airtime_ms_measured"] = stats(list(dur.values()), 0)
    ages_log = []
    ages_node = []
    for e in txm:
        if e["id"] in content:
            ages_log.append(e["t"] - content[e["id"]][2])
        c = cetx.get(e["ms"])
        if c and c["age_s"] != "na":
            ages_node.append(int(c["age_s"]))
    r["tx_age_s_logger_based"] = stats(ages_log, 1)
    r["tx_age_s_node_based"] = stats(ages_node, 0)
    r["tx_age_gt150_n"] = len([a for a in ages_log if a > 150])
    r["tx_age_gt120_n"] = len([a for a in ages_log if a > 120])
    r["tx_age_gt100_n"] = len([a for a in ages_log if a > 100])
    r["tx_trusted_flag"] = dict(Counter(cetx[k]["trusted"] for k in cetx))
    # TX after having already overheard an RF copy of the same id (redundant radiation)
    rfby = defaultdict(list)
    for e in ev:
        if e["k"] == "rf" and e.get("fcs") != "0000" and e["cont"][:1] != "<":
            rfby[e["id"]].append(e)
    after = 0
    after_gw = 0
    gap_after = []
    for e in txm:
        pri = [x for x in rfby.get(e["id"], []) if x["t"] < e["t"] and "OWN" and x["path"][-1] != OWNCALL[n]]
        if pri:
            after += 1
            gap_after.append(e["t"] - min(x["t"] for x in pri))
        if [x for x in pri if len(x["path"]) == 2]:
            after_gw += 1
    r["tx_after_overheard_any_rf_copy_n"] = after
    r["tx_after_overheard_gateway_radiation_n"] = after_gw
    r["tx_after_overheard_pct"] = round(100.0 * after / max(1, len(txm)), 1)
    r["tx_after_overheard_lead_s"] = stats(gap_after, 1)
    # tx wait vs overhear: did the overheard copy arrive within the queue wait
    # [CET] gate lines
    cets = [e for e in ev if e["k"] == "cet"]
    r["cet_lines"] = dict(Counter(e["kind"] for e in cets))
    gw = [int(e["age_s"]) for e in cets if e["kind"] == "gw_rx" and e["age_s"] != "na"]
    r["gw_rx_age_s_node_based"] = stats(gw, 0)
    r["gw_rx_age_dist"] = dict(sorted(Counter(gw).items()))
    r["cet_drop_lines"] = [(round(e["t"]), e["where"], e["age_s"]) for e in cets if e["kind"] == "drop"]
    r["cet_rx_lines"] = [(e["age_s"], e["hop"], e["path"]) for e in cets if e["kind"] == "rx"]
    r["ring_drop_stale_cet"] = len([e for e in ev if e["k"] in ("ringany",) and "reason=cet" in e["line"]])
    # supersede / ring events involving CET ids
    mk = Counter(x["mk"] for x in ev if x["k"] == "ring")
    r["ring_marker_counts"] = dict(mk.most_common(14))
    # RING_WRITE queue depth at CET enqueue
    qd = []
    for x in ev:
        if x["k"] == "ring" and "RING_WRITE" in x["line"]:
            m = re.search(r"queued=(\d+)/20", x["line"])
            if m:
                qd.append(int(m.group(1)))
    r["queue_depth_at_cet_enqueue"] = stats(qd, 0)
    r["cet_slot_seconds_per_hour"] = round(sum(e["wait"] for e in txm) / 1000.0 / ((r["window_utc"][1] - r["window_utc"][0]) / 3600.0), 1)
    # supersede of CET entries (older CET replaced by newer)
    r["ring_supersede_cet"] = len([x for x in ev if x["k"] == "ring" and "RING_SUPERSEDE" in x["line"]])

# ---------------- per-id radiation table (cluster)
rad = {}
for i in ids:
    paths = set()
    tx_by = {}
    first_rf = {}
    for n in CLUSTER:
        for e in EV[n]:
            if e["k"] == "rf" and e["id"] == i and e.get("fcs") != "0000":
                paths.add(tuple(e["path"]))
        for e in EV[n]:
            if e["k"] == "txmeta" and e["id"] == i:
                tx_by[n] = e
                paths.add((content[i][0], OWNCALL[n]) if e["src"] == "g" else None)
    paths.discard(None)
    rad[i] = paths
nroad = []
gw_per_id = []
foreign_gw_per_id = []
relay_per_id = []
for i in ids:
    P = rad[i]
    gws = {p[1] for p in P if len(p) >= 2}
    nroad.append(len(P))
    gw_per_id.append(len(gws))
    foreign_gw_per_id.append(len(gws - OURS))
    relay_per_id.append(len([p for p in P if len(p) >= 3]))
# restrict to ids fully inside every node's window (all 5 up)
tmin = max(res["nodes"][n]["window_utc"][0] for n in CLUSTER)
tmax = min(res["nodes"][n]["window_utc"][1] for n in CLUSTER)
inwin = [k for k, i in enumerate(ids) if tmin <= first_udp.get(i, 0) <= tmax]
net["window_all5_utc"] = [tmin, tmax]
net["ids_in_all5_window"] = len(inwin)
sel = lambda arr: [arr[k] for k in inwin]
net["distinct_radiations_lower_bound_per_telegram"] = stats(sel(nroad), 1)
net["gateways_radiating_per_telegram"] = stats(sel(gw_per_id), 1)
net["foreign_gateways_per_telegram"] = stats(sel(foreign_gw_per_id), 1)
net["distinct_relay_paths_per_telegram"] = stats(sel(relay_per_id), 1)
net["gateways_radiating_dist"] = dict(sorted(Counter(sel(gw_per_id)).items()))
allgw = Counter()
for k in inwin:
    for p in rad[ids[k]]:
        allgw[p[1]] += 1
net["gateway_radiation_counts_in_window"] = allgw.most_common(20)
# relayers
rel = Counter()
for k in inwin:
    for p in rad[ids[k]]:
        if len(p) >= 3:
            rel[p[-1]] += 1
net["relay_lasthop_counts_in_window"] = rel.most_common(25)
net["path_len_dist_distinct_paths"] = dict(sorted(Counter(len(p) - 1 for k in inwin for p in rad[ids[k]]).items()))
# copies of the same telegram heard per node, summary table
# Total transmissions of our 5 gateways
net["our_gateway_tx_in_window"] = sum(1 for n in CLUSTER for e in EV[n] if e["k"] == "txmeta" and tmin <= e["t"] <= tmax)
net["our_tx_per_telegram_in_window"] = round(net["our_gateway_tx_in_window"] / max(1, len(inwin)), 2)

# ---------------- airtime budget
for n in CLUSTER:
    ev = EV[n]
    r = res["nodes"][n]
    rf = [e for e in ev if e["k"] == "rf" and e.get("fcs") != "0000" and e["cont"][:1] != "<"]
    rx_air = sum(airtime_ms(e["len"]) for e in rf)
    txm = [e for e in ev if e["k"] == "txmeta"]
    tx_air = sum(airtime_ms(e["len"]) for e in txm)
    hours = (r["window_utc"][1] - r["window_utc"][0]) / 3600.0
    st = [s for s in load_stat(n) if r["window_utc"][0] <= s[0] <= r["window_utc"][1]]
    st_rx = sum(s[1] for s in st)
    st_tx = sum(s[2] for s in st)
    st_h = len(st) * 300 / 3600.0
    r["hours"] = round(hours, 2)
    r["cet_rx_airtime_s_per_h"] = round(rx_air / 1000.0 / hours, 1)
    r["cet_tx_airtime_s_per_h"] = round(tx_air / 1000.0 / hours, 1)
    r["stat_rx_s_per_h"] = round(st_rx / 1000.0 / st_h, 1)
    r["stat_tx_s_per_h"] = round(st_tx / 1000.0 / st_h, 1)
    r["stat_util_median_pct"] = median(s[3] for s in st)
    r["cet_share_of_stat_rx_pct"] = round(100.0 * rx_air / max(1, st_rx) * (st_h / hours), 2)
    r["cet_share_of_stat_tx_pct"] = round(100.0 * tx_air / max(1, st_tx) * (st_h / hours), 2)
    r["cet_share_of_stat_rxtx_pct"] = round(100.0 * (rx_air + tx_air) / max(1, st_rx + st_tx) * (st_h / hours), 2)
    r["stat_windows"] = len(st)
    r["stat_txn_total"] = sum(s[4] for s in st)
    r["cet_share_of_tx_count_pct"] = round(100.0 * len(txm) / max(1, sum(s[4] for s in st)) * (st_h / hours), 1)
    # RF copies per hour
    r["cet_rf_frames_per_h"] = round(len(rf) / hours, 1)
    # drops and ringmax from STAT
drops = {}
for n in CLUSTER:
    last = None
    pat = re.compile(rb"\[LOG\] STAT .* ringmax=(\d+)/20 drop=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)")
    tot = [0] * 5
    rmax = []
    with open(f"{NORM}/{n}.log", "rb") as f:
        for line in f:
            if b"[LOG] STAT" in line:
                m = pat.search(line)
                if m:
                    rmax.append(int(m.group(1)))
                    for k in range(5):
                        tot[k] += int(m.group(2 + k))
    res["nodes"][n]["stat_drop_total_prio1to5"] = tot
    res["nodes"][n]["stat_ringmax_p50_p90_max"] = [median(rmax), pct(rmax, 90), max(rmax)]

# ---------------- hourly series (airtime, number of radiations) for the whole cluster
hour = defaultdict(lambda: {"tx": 0, "rf": 0, "ids": set()})
for n in CLUSTER:
    for e in EV[n]:
        h = int(e["t"] // 3600)
        if e["k"] == "txmeta":
            hour[h]["tx"] += 1
        if e["k"] == "rf" and e.get("fcs") != "0000":
            hour[h]["rf"] += 1
net["hourly_cet_tx_total_all5_sample"] = [(h * 3600, v["tx"], v["rf"]) for h, v in sorted(hour.items())][:4]

json.dump(res, open(OUT, "w"), indent=1, default=str)
print(json.dumps(net, indent=1, default=str)[:6000])
