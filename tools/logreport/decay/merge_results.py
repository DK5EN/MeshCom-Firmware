#!/usr/bin/env python3
"""Merge all intermediate JSON into q34/results.json and print the tables used in findings.md."""
from __future__ import annotations

import json
import re
from collections import defaultdict
from pathlib import Path

from .common import NODES, load, pct, toa_ms, work



def unclocked_stats(path: Path) -> dict:
    lat: dict[int, list] = defaultdict(list)
    n_stale = n_csma = 0
    rx = re.compile(r"RING_TX_READ slot=\d+ prio=(\d) .* lat=(\d+)ms")
    with open(path, "rb") as fh:
        for raw in fh:
            if b"RING_TX_READ" in raw:
                m = rx.search(raw.decode("latin-1"))
                if m:
                    lat[int(m[1])].append(int(m[2]) / 1000)
            elif b"RING_DROP_STALE" in raw:
                n_stale += 1
            elif b"CSMA_AGED" in raw:
                n_csma += 1
    out = {"ring_drop_stale": n_stale, "csma_aged": n_csma, "tx_by_prio": {}}
    for p, v in sorted(lat.items()):
        out["tx_by_prio"][f"p{p}"] = {"n": len(v), "p50": round(pct(v, 50), 1), "p99": round(pct(v, 99), 1),
                                      "max": round(max(v), 1), "n_gt_20s": sum(1 for x in v if x > 20),
                                      "n_gt_60s": sum(1 for x in v if x > 60)}
    return out


def context() -> dict:
    ctx = {}
    for n in NODES:
        d = load(n)
        ntx = len(d["logtx"])
        span_h = (d["logtx"][-1][0] - d["logtx"][0][0]) / 3600
        ctx[n] = {"tx": ntx, "span_h": round(span_h, 2), "tx_per_h": round(ntx / span_h, 1),
                  "rx_logged": len(d["logrx"]), "rx_per_h": round(len(d["logrx"]) / span_h),
                  "rx_error_per_h": round(len(d["onrxerr"]) / span_h),
                  "cad_busy_1_per_tx": round(len(d["busy"]) / ntx, 2),
                  "tx_gate_enter_per_tx": round(len(d["gate"]) / ntx, 2),
                  "cancel_events": len(d["cancel"]),
                  "ring_supersede": len(d["sup"]),
                  "txudp_lines": len(d["txudp"])}
    return ctx


def run(out_dir: Path, unclocked: dict[str, Path], time_base: str) -> dict:
    a = json.load(open(work() / "a.json"))
    cf = json.load(open(work() / "cf.json"))
    ts = json.load(open(work() / "ttl_sens.json"))
    cs = json.load(open(work() / "csma.json"))
    drops = json.load(open(work() / "drops_classified.json"))
    ctx = context()
    res: dict = {"meta": {
        "toa_assumption": "SF11 BW250 CR4/6 (MOD 8 = fastslow, country 8 = EU8) preamble 8 explicit header CRC on, LDRO off; "
                          "bytes on air = ring len; validated: observed TX_START->OnTXDone is constant ~40 ms below calc over lens 12..153",
        "time_base": f"logger wall clock; day 0 = {time_base}",
        "cf_model": "see scripts/cf.py docstring; HL excluded (nbrrelay CANCEL path not modelled, baseline pick match 56 %)"},
        "nodes": {}, "network": a["network"]}
    # drop age tables per node/class
    for n in NODES:
        nd = a["nodes"][n]
        tab = defaultdict(list)
        for r in drops:
            if r["node"] == n:
                tab[f"p{r['prio']}/{r['cat']}"].append(r)
        t2 = {}
        for k, v in sorted(tab.items()):
            ages = [x["age_s"] for x in v]
            tl = [toa_ms(x["len"]) / 1000 for x in v if x.get("len")]
            t2[k] = {"n": len(v), "age_s_min": min(ages), "age_s_max": max(ages),
                     "len_bytes_med": pct([x["len"] for x in v if x.get("len")], 50) if tl else None,
                     "toa_s_total": round(sum(tl), 2)}
        nd["drop_table"] = t2
        nd["context"] = ctx[n]
        nd["cf"] = cf.get(n)
        nd["ttl_sens"] = ts.get(n)
        nd["csma"] = cs[n]
        res["nodes"][n] = nd
    res["network"]["ttl_sens_pooled_ia_ir_oc_wj"] = ts["pooled_ia_ir_oc_wj"]
    for un, up in unclocked.items():
        res["network"][un] = unclocked_stats(up)
    tot_toa = sum(res["nodes"][n]["drop_toa_s"] for n in NODES)
    tot_tx = sum(res["nodes"][n]["tx_toa_s"] for n in NODES)
    res["network"]["drop_toa_s_total"] = round(tot_toa, 1)
    res["network"]["tx_toa_s_total"] = round(tot_tx, 1)
    res["network"]["drop_toa_pct_of_tx"] = round(100 * tot_toa / tot_tx, 2)
    # relay potential-loss refresh stats
    rel = [r for r in drops if r["cat"].startswith("relay")]
    pot = [r for r in rel if r["loss_class"] in ("no_evidence", "weak_cluster_tx_before", "weak_heard_same_hop")]
    strict = [r for r in rel if r["loss_class"] == "no_evidence"]
    res["network"]["relay_potential_loss"] = {
        "relay_total": len(rel), "weak_or_none": len(pot), "none": len(strict),
        "weak_or_none_refreshed_by_originator_within_30min": sum(1 for r in pot if r["refreshed_after_s"] is not None),
        "none_refreshed": sum(1 for r in strict if r["refreshed_after_s"] is not None),
        "text_or_ack_drops": sum(1 for r in drops if r.get("type") in ("3A", "41")),
        "refresh_median_s_positive": pct([r["refreshed_after_s"] for r in rel if (r["refreshed_after_s"] or 0) > 0], 50)}
    with open(out_dir / "results.json", "w") as fh:
        json.dump(res, fh, indent=1)
    print(json.dumps({k: res["network"][k] for k in ("drop_toa_s_total", "tx_toa_s_total", "drop_toa_pct_of_tx",
                                                     "relay_potential_loss")}, indent=1))
    for n in NODES:
        print(n, json.dumps(ctx[n]))
        print("   ", json.dumps(res["nodes"][n]["drop_table"]))
    return res
