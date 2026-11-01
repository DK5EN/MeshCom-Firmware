#!/usr/bin/env python3
"""Markdown rows from lrf_m2.json. Usage: python3 -I table.py <lrf_m2.json> <mode> [<mode> ...]
mode like cad/worst, cad/mean, ideal/worst, ideal/mean, cad/rand."""
import json
import sys

def agree(path):
    """baseline model vs measured outcome (aired vs not aired) per node, class, mode."""
    d = json.load(open(path))
    print("| node | class | mode | meas aired | pred aired | TP | FN | FP | TN | mean p (meas aired) | mean p (meas lost) | time err s p10/p50/p90 (TP-ish) |")
    print("| ---- | ----- | ---- | ---------: | ---------: | -: | -: | -: | -: | ------------------: | -----------------: | ------------------------------- |")
    for n, r in d["nodes"].items():
        for c, s in r["summary"].items():
            for mode in ("ideal/worst", "ideal/mean", "cad/worst", "cad/mean"):
                v = s["variants"][f"base/{mode}"]
                cf = v["confusion"]
                te = s.get(f"base_{mode}_time_err_s_p10_p50_p90")
                print(f"| {n[3:].upper()} | {c} | {mode} | {s['measured_end'].get('tx', 0)} | {v['pred_aired']} | {cf['meas_aired_pred_aired']} | {cf['meas_aired_pred_lost']} | "
                      f"{cf['meas_lost_pred_aired']} | {cf['meas_lost_pred_lost']} | {v['mean_p_meas_aired']} | {v['mean_p_meas_lost']} | {te} |")




def totals(path):
    """sum over both nodes and classes; 'incl relays' = extra own airtime x (1 + mean measured relay count of that node/class)."""
    d = json.load(open(path))
    for mode in ("cad/worst", "ideal/worst"):
        for var in ("A1", "A2", "A3"):
            rec = oth = tot = net = rf = ra = 0.0
            for n, r in d["nodes"].items():
                for c, s in r["summary"].items():
                    v = s["variants"][f"{var}/{mode}"]
                    rel = d["relays"]["nodes"][n]["summary"][c]["relay_count_mean"]
                    ext = (v["recovered_ttl"] + v["extra_other"]) * r["mean_air_ms_" + c] / 1000
                    rec += v["recovered_ttl"]; oth += v["extra_other"]; tot += ext; net += ext * (1 + rel)
                    rf += v["rapid_floor"]; ra += v["rapid_any"]
            print(f"{mode:11s} {var} recovered {rec:6.1f} extra_other {oth:5.1f} own airtime {tot:6.1f} s incl. relays {net:6.1f} s rapid(floor) {rf:6.1f} rapid(any) {ra:6.1f}")
    for n, r in d["nodes"].items():
        print(f"{n}: own TX airtime {r['own_tx_airtime_s']} s in {r['span_h']} h")


if len(sys.argv) > 1 and sys.argv[1] == "--totals":
    totals(sys.argv[2])
    sys.exit(0)

if len(sys.argv) > 1 and sys.argv[1] == "--agree":
    agree(sys.argv[2])
    sys.exit(0)

d = json.load(open(sys.argv[1]))
for mode in sys.argv[2:]:
    print(f"\nmode {mode}")
    print("| node | class | variant | entries | aired (meas) | TTL-lost (meas) | other (meas) | recovered gross | net of base model | extra other | extra TX airtime s | rapid (floor) | rapid (any) |")
    print("| ---- | ----- | ------- | ------: | -----------: | --------------: | -----------: | --------------: | ----------------: | ----------: | -----------------: | ------------: | ----------: |")
    for n, r in d["nodes"].items():
        for c, s in r["summary"].items():
            m = s["measured_end"]
            for var in ("base", "A1", "A2", "A3"):
                v = s["variants"][f"{var}/{mode}"]
                print(f"| {n[3:].upper()} | {c} | {var} | {s['entries']} | {m.get('tx', 0)} | {m.get('ttl', 0)} | {s['entries'] - m.get('tx', 0) - m.get('ttl', 0)} | "
                      f"{v['recovered_ttl']} | {v['recovered_ttl_net_of_base_model']} | {v['extra_other']} | {v['extra_airtime_s_all']} | {v['rapid_floor']} | {v['rapid_any']} |")

