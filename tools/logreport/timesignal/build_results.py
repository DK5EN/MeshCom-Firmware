#!/usr/bin/env python3
"""Merge the partial JSON files in <out>/data into <out>/results.json.

Usage (via run.py): build_results.py
"""
import json

from nodeset import CLUSTER, NOLOG, OUT, STANDALONE

DATA = OUT / "data"
DROP_NET = ("gateway_radiation_counts_in_window", "relay_lasthop_counts_in_window", "gateways_radiating_per_telegram",
            "gateways_radiating_dist", "foreign_gateways_per_telegram", "distinct_relay_paths_per_telegram",
            "distinct_radiations_lower_bound_per_telegram", "our_gateway_tx_in_window", "our_tx_per_telegram_in_window",
            "path_len_dist_distinct_paths", "ids_in_all5_window", "window_all5_utc")


def L(name: str) -> dict:
    with open(DATA / f"{name}.json") as fh:
        return json.load(fh)


res: dict = {"nodes": {}, "network": {}}
if CLUSTER:
    a, x, co = L("oe3_analysis"), L("oe3_extra"), L("clock_offsets")
    net = {k: v for k, v in a["network"].items() if k not in DROP_NET}
    net["oe3_cluster"] = x
    res["network"] = net
    for n in CLUSTER:
        node = dict(a["nodes"][n])
        node["clock"] = co[n]
        node["rawpass"] = L(f"raw_{n}")
        node["tx_age_node_based_join"] = x["tx_age_node_based_join"][n]
        res["nodes"][n] = node
for n in STANDALONE:
    d = dict(L(f"db_analysis_{n}"))
    d["clock_node_vs_ntp"] = L(f"db_clock_{n}")
    d["source_phase"] = L(f"db_phase_{n}")
    d["rawpass"] = L(f"raw_{n}")
    res["nodes"][n] = d
    res["network"][f"{n}_is_separate_mesh"] = True
for n in NOLOG:
    # wall-clock capture without [LOG] lines: only what the raw frames and {CET} lines show
    ev = [json.loads(x) for x in open(DATA / f"events_{n}.jsonl")]
    kinds: dict[str, int] = {}
    for e in ev:
        kinds[e["k"]] = kinds.get(e["k"], 0) + 1
    res["nodes"][n] = {
        "rawpass": L(f"raw_{n}"),
        "cet_events_by_kind": kinds,
        "unavailable": {
            "rf_age_and_copies": "no [LOG] RX records (--setlog on missing): RSSI/SNR/FCS of received {CET} copies unknown",
            "tx_airtime_wait_prio": "no [LOG] TX records: queue wait, prio and src of own {CET} radiations unknown",
            "stat_airtime_share": "no [LOG] STAT windows: RX/TX airtime and ringmax/drop counters unknown",
            "cluster_correlation": "node left out of the cross-node correlation (UDP lag, hears-gateway matrix, clock offsets)",
        },
    }
    res["network"].setdefault("nodes_without_log_lines", []).append(n)
with open(OUT / "results.json", "w") as fh:
    json.dump(res, fh, indent=1, default=str)
print("ok", len(json.dumps(res)))
