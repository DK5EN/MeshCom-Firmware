#!/usr/bin/env python3
"""Q3/Q4 (a): distributions, loss classification, own-beacon gaps, CET, ToA, TTL headroom.
Reads work/*.pkl + work/drops.json; writes work/a.json."""
from __future__ import annotations

import json
from bisect import bisect_left
from collections import Counter

from .common import NODES, load, pct, toa_ms, work
from .ringq import entries

TTLS = {1: 90, 3: 300, 4: 120, 5: 180}


def cat_of(r: dict) -> str:
    src = r.get("src")
    if src is None:
        return "unknown_prelog"
    if src == "auto_pos":
        return "own_HEY"
    if src == "user_wx":
        return "own_WX_POS"
    if src == "rx_relay":
        return "relay_HEY" if r["type"] == "40" else ("relay_POS" if r["type"] == "21" else "relay_other")
    return f"other_{src}"


def classify_relay(r: dict) -> str:
    """Hierarchy of evidence that the frame reached the network anyway (30 min window)."""
    hop0 = r.get("first_rx_hop")
    if r["other_tx_after_write"]:
        return "redundant_cluster_tx"             # another of the five nodes transmitted it after our write
    lower = [h for (_, h, _, _) in r["heard_again_here"] if hop0 is not None and h < hop0]
    if lower:
        return "redundant_heard_lower_hop"        # we heard a copy that went through a further relay
    if r["heard_again_here"]:
        return "weak_heard_same_hop"              # another path delivered it to us, no proof of relay
    if r["other_tx_before_write"]:
        return "weak_cluster_tx_before"           # a cluster node had sent it earlier (that is where we got it?)
    return "no_evidence"


def run() -> None:
    drops = json.load(open(work() / "drops.json"))
    for r in drops:
        r["cat"] = cat_of(r)
    # udp uplink check
    D = {n: load(n) for n in NODES}
    res: dict = {"nodes": {}, "network": {}}
    allcat: Counter = Counter()
    for n in NODES:
        dn = [r for r in drops if r["node"] == n]
        nres: dict = {"drops_total": len(dn)}
        nres["by_prio_cat"] = {f"p{p}/{c}": k for (p, c), k in
                               sorted(Counter((r["prio"], r["cat"]) for r in dn).items())}
        nres["reason"] = dict(Counter(r["reason"] for r in dn))
        for p in (4, 5):
            ages = [r["age_s"] for r in dn if r["prio"] == p]
            if ages:
                nres[f"age_p{p}"] = {"min": min(ages), "p50": pct(ages, 50), "max": max(ages)}
        # write -> drop (measured) check
        nres["write_age_range"] = [min((r["write_age_s"] for r in dn if r.get("write_age_s")), default=None),
                                   max((r["write_age_s"] for r in dn if r.get("write_age_s")), default=None)]
        # relay classes
        rel = [r for r in dn if r["cat"].startswith("relay")]
        nres["relay_class"] = dict(Counter(classify_relay(r) for r in rel))
        for r in rel:
            r["loss_class"] = classify_relay(r)
        # refreshed
        nres["relay_refreshed_within_30min"] = sum(1 for r in rel if r["refreshed_after_s"] is not None)
        nres["relay_refresh_median_s"] = pct([r["refreshed_after_s"] for r in rel
                                              if r["refreshed_after_s"] is not None and r["refreshed_after_s"] > 0], 50)
        # own beacons
        own = [r for r in dn if r["cat"] in ("own_HEY", "own_WX_POS")]
        nres["own_dropped"] = len(own)
        # ToA of dropped frames (bytes on air = ring len)
        toa = [toa_ms(r["len"]) for r in dn if r.get("len")]
        nres["drop_toa_s"] = round(sum(toa) / 1000, 2)
        nres["drop_toa_n"] = len(toa)
        nres["drop_bytes"] = sum(r["len"] for r in dn if r.get("len"))
        # node TX airtime (sent frames)
        es = entries(D[n])
        # actual air frames: [LOG] TX lines (len), exclude none
        tx_toa = sum(toa_ms(x[9]) for x in D[n]["logtx"]) / 1000
        nres["tx_frames"] = len(D[n]["logtx"])
        nres["tx_toa_s"] = round(tx_toa, 1)
        span = (D[n]["logtx"][-1][0] - D[n]["logtx"][0][0]) if D[n]["logtx"] else 0
        nres["span_h"] = round(span / 3600, 2)
        nres["tx_duty_pct"] = round(100 * tx_toa / span, 3) if span else None
        nres["drop_toa_pct_of_tx"] = round(100 * sum(toa) / 1000 / tx_toa, 2) if tx_toa else None
        # headroom: sent entries by prio
        hr = {}
        for p in (1, 3, 4, 5):
            sent = [e for e in es if e["prio"] == p and e["outcome"] == "sent"]
            if p == 1:   # TTL only applies to ACK frames
                sent = [e for e in sent if e["type"] == "41"]
            L = [e["lat_ms"] / 1000 for e in sent]
            ttl = TTLS[p]
            nd = sum(1 for r in dn if r["prio"] == p)
            hr[f"p{p}"] = {"ttl_s": ttl, "n_sent": len(L), "n_dropped": nd,
                           "p50": round(pct(L, 50), 1) if L else None,
                           "p90": round(pct(L, 90), 1) if L else None,
                           "p99": round(pct(L, 99), 1) if L else None,
                           "max": round(max(L), 1) if L else None,
                           "n_ge80pct_ttl_sent": sum(1 for x in L if x >= 0.8 * ttl),
                           "n_ge50pct_ttl_sent": sum(1 for x in L if x >= 0.5 * ttl)}
        nres["headroom"] = hr
        # CET
        cet = []
        for (t, s) in D[n]["cet"]:
            parts = s.split(";")
            try:
                cet.append((int(parts[3]), int(parts[5]), int(parts[7]), int(parts[9])))   # age_s, trusted, wait_ms, prio
            except Exception:
                pass
        nres["cet_tx_n"] = len(cet)
        if cet:
            nres["cet_tx_age_s"] = {"min": min(c[0] for c in cet), "p50": pct([c[0] for c in cet], 50),
                                    "max": max(c[0] for c in cet),
                                    "n_gt_120": sum(1 for c in cet if c[0] > 120),
                                    "n_untrusted": sum(1 for c in cet if c[1] != 1)}
            nres["cet_tx_wait_s_max"] = max(c[2] for c in cet) / 1000
        res["nodes"][n] = nres
        for r in dn:
            allcat[(r["prio"], r["cat"])] += 1
    res["network"]["drops_total"] = len(drops)
    res["network"]["by_prio_cat"] = {f"p{p}/{c}": k for (p, c), k in sorted(allcat.items())}
    res["network"]["reason"] = dict(Counter(r["reason"] for r in drops))
    rel_all = [r for r in drops if r["cat"].startswith("relay")]
    res["network"]["relay_class"] = dict(Counter(classify_relay(r) for r in rel_all))
    res["network"]["relay_refreshed"] = sum(1 for r in rel_all if r["refreshed_after_s"] is not None)

    # ---- own beacons: effective delivery and gap -------------------------------------------
    own_res: dict = {}
    for n in NODES:
        es = entries(D[n])
        for kind, src, ty in (("own_HEY", "auto_pos", "40"), ("own_WX_POS", "user_wx", "21")):
            ser = [e for e in es if e["src"] == src and e["type"] == ty]
            if not ser:
                continue
            # a "replaced" outcome whose id appears in RING_SUPERSEDE old_id = superseded
            sup_old = {s[4] for s in D[n]["sup"]}
            for e in ser:
                if e["outcome"] == "replaced":
                    e["outcome"] = "superseded" if e["mid"] in sup_old else "replaced_other"
            c = Counter(e["outcome"] for e in ser)
            sent_t = [e["t_end"] for e in ser if e["outcome"] == "sent"]
            gaps = [sent_t[i + 1] - sent_t[i] for i in range(len(sent_t) - 1)]
            # gaps containing at least one dropped beacon
            dr_t = [e["t"] for e in ser if e["outcome"] == "dropped"]
            gd = []
            for t in dr_t:
                k = bisect_left(sent_t, t)
                if 0 < k < len(sent_t):
                    gd.append(sent_t[k] - sent_t[k - 1])
            # UDP uplink at write time?
            udp_t = sorted(x[0] for x in D[n]["txudp"])
            udp_ok = 0
            for e in ser:
                if e["outcome"] == "dropped":
                    j = bisect_left(udp_t, e["t"] - 0.2)
                    if j < len(udp_t) and udp_t[j] - e["t"] < 3.0:
                        udp_ok += 1
            own_res[f"{n}/{kind}"] = {
                "written": len(ser), "outcomes": dict(c),
                "effective_sent_pct": round(100 * c.get("sent", 0) / len(ser), 1),
                "sent_gap_s": {"p50": round(pct(gaps, 50)), "p90": round(pct(gaps, 90)),
                               "max": round(max(gaps))} if gaps else None,
                "gap_around_dropped_s": {"n": len(gd), "p50": round(pct(gd, 50)) if gd else None,
                                         "max": round(max(gd)) if gd else None},
                "dropped_with_udp_uplink": udp_ok,
                "sent_lat_s": {"p50": round(pct([e["lat_ms"] / 1000 for e in ser if e["outcome"] == "sent"], 50), 1),
                               "p90": round(pct([e["lat_ms"] / 1000 for e in ser if e["outcome"] == "sent"], 90), 1)}
                if sent_t else None,
            }
    res["network"]["own_beacons"] = own_res

    with open(work() / "a.json", "w") as fh:
        json.dump(res, fh, indent=1)
    with open(work() / "drops_classified.json", "w") as fh:
        json.dump(drops, fh)
    print(json.dumps(res["network"], indent=1))
    for n in NODES:
        print(n, json.dumps(res["nodes"][n]))
