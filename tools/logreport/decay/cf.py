#!/usr/bin/env python3
"""Counterfactual replay "what if no TTL" (Model A, first order).

Model: per node, the strict-priority pick of getNextTxSlot() (lowest prio value, then oldest enqueue)
is replayed on the observed arrivals (RING_WRITE of prio 3/4/5) with the observed service instants
(= RING_TX_READ events whose picked entry was prio 3/4/5). Supersede (same type + same originator,
HEY/POS) is applied on arrival. TTL on (baseline, validates the model against the log) vs TTL off.
Prio 1/2 traffic and own-text retries are not modelled: they consume their own service instants.
Limit: extra idle TX instants (stale entries going out when the node would otherwise be silent) are NOT added,
so stale frames' sim delay is an UPPER bound of their air-age; the delay inflicted on fresh frames is first-order.
"""
from __future__ import annotations

import json
from collections import defaultdict

from .common import NODES, load, pct, work
from .ringq import entries

TTLS = {4: 120.0, 5: 180.0}


def build(node: str):
    d = load(node)
    es = entries(d)
    rx_by_mid: dict[str, list] = defaultdict(list)
    for r in d["logrx"]:
        rx_by_mid[r[2]].append((r[0], r[7]))
    items = []
    for e in es:
        if e["prio"] not in (3, 4, 5):
            continue
        if e["src"] in ("auto_pos", "user_wx"):
            orig = "OWN"
        else:
            lst = [x for x in rx_by_mid.get(e["mid"], []) if x[0] <= e["t"] + 0.5]
            orig = lst[-1][1] if lst else "?" + e["mid"]
        e["key"] = (e["type"], orig) if e["prio"] in (4, 5) else None
        items.append(e)
    services = sorted((e["t_end"], e) for e in es if e["prio"] in (3, 4, 5) and e["outcome"] == "sent")
    return d, items, [t for t, _ in services], [e for _, e in services]


def simulate(items, svc_t, ttl_on: bool, t_end: float):
    arr = sorted(items, key=lambda e: e["t"])
    ai = 0
    queue: list[dict] = []
    served: dict[int, float] = {}
    superseded: set[int] = set()
    ttl_dropped: set[int] = set()
    depth_log: list[tuple[float, int]] = []

    def admit(upto: float):
        nonlocal ai
        while ai < len(arr) and arr[ai]["t"] <= upto:
            e = arr[ai]
            ai += 1
            if e["key"] is not None:
                for q in list(queue):
                    if q["key"] == e["key"] and q["prio"] == e["prio"]:
                        queue.remove(q)
                        superseded.add(id(q))
            queue.append(e)
            depth_log.append((e["t"], len(queue)))

    for tau in svc_t:
        admit(tau)
        if ttl_on:
            for q in list(queue):
                ttl = TTLS.get(q["prio"])
                if ttl and tau - q["t"] > ttl + 1.0:
                    queue.remove(q)
                    ttl_dropped.add(id(q))
        if not queue:
            continue
        best = min(queue, key=lambda q: (q["prio"], q["t"]))
        queue.remove(best)
        served[id(best)] = tau
        depth_log.append((tau, len(queue)))
    admit(t_end + 1)
    return served, superseded, ttl_dropped, queue, depth_log


def run() -> None:
    out: dict = {}
    for node in NODES:
        d, items, svc_t, svc_e = build(node)
        if not svc_t:
            continue
        t_end = svc_t[-1]
        s_on, sup_on, td_on, q_on, dl_on = simulate(items, svc_t, True, t_end)
        s_off, sup_off, td_off, q_off, dl_off = simulate(items, svc_t, False, t_end)
        # validation: baseline pick == real pick
        real_serv = {id(e): e["t_end"] for e in svc_e}
        same = sum(1 for k, t in real_serv.items() if s_on.get(k) == t)
        # real dropped set
        real_drop = {id(e) for e in items if e["outcome"] == "dropped"}
        res = {"n_items": len(items), "n_services_p345": len(svc_t),
               "baseline_pick_match_pct": round(100 * same / len(svc_t), 1),
               "baseline_ttl_drops": len(td_on), "real_ttl_drops": len(real_drop),
               "baseline_drops_in_real_drops_pct": round(100 * len(td_on & real_drop) / max(1, len(real_drop)), 1)}
        # no-TTL: fate of the items that the real run dropped
        stale = [e for e in items if e["outcome"] == "dropped"]
        fate = {"served": 0, "superseded": 0, "never_served_by_end": 0}
        air_age = []
        for e in stale:
            k = id(e)
            if k in s_off:
                fate["served"] += 1
                air_age.append(s_off[k] - e["t"])
            elif k in sup_off:
                fate["superseded"] += 1
            else:
                fate["never_served_by_end"] += 1
        res["stale_fate_no_ttl"] = fate
        if air_age:
            res["stale_air_age_s_if_served"] = {"p50": round(pct(air_age, 50)), "p90": round(pct(air_age, 90)),
                                                  "max": round(max(air_age)),
                                                  "n_gt_300": sum(1 for a in air_age if a > 300),
                                                  "n_gt_600": sum(1 for a in air_age if a > 600),
                                                  "n_gt_1800": sum(1 for a in air_age if a > 1800)}
        # fresh frames delayed: items served in both baseline and no-TTL
        delta = []
        lost_fresh = 0
        for e in items:
            k = id(e)
            if e["outcome"] == "dropped":
                continue
            if k in s_on and k in s_off:
                delta.append(s_off[k] - s_on[k])
            elif k in s_on and k not in s_off:
                lost_fresh += 1
        dpos = [x for x in delta if x > 0.5]
        res["fresh_delayed"] = {"n_compared": len(delta), "n_delayed_gt_0_5s": len(dpos),
                                "pct_delayed": round(100 * len(dpos) / max(1, len(delta)), 1),
                                "delay_s_p50": round(pct(dpos, 50), 1) if dpos else 0,
                                "delay_s_p90": round(pct(dpos, 90), 1) if dpos else 0,
                                "delay_s_max": round(max(dpos), 1) if dpos else 0,
                                "fresh_served_in_baseline_but_not_in_noTTL": lost_fresh}
        # queue depth (items of prio 3-5 only, sim) with vs without TTL
        def stats(dl):
            ds = [x[1] for x in dl]
            return {"max": max(ds), "mean_at_events": round(sum(ds) / len(ds), 2),
                    "pct_events_ge10": round(100 * sum(1 for x in ds if x >= 10) / len(ds), 1)}
        res["depth_ttl_on"] = stats(dl_on)
        res["depth_no_ttl"] = stats(dl_off)
        res["end_backlog_ttl_on"] = len(q_on)
        res["end_backlog_no_ttl"] = len(q_off)
        # time-weighted depth: integrate depth_log
        def tw(dl):
            dl = sorted(dl)
            tot = 0.0
            for (t0, d0), (t1, _) in zip(dl, dl[1:]):
                tot += d0 * (t1 - t0)
            return tot / (dl[-1][0] - dl[0][0])
        res["depth_time_weighted"] = {"ttl_on": round(tw(dl_on), 2), "no_ttl": round(tw(dl_off), 2)}
        # slot-seconds used by dropped entries until drop (real) and until sim service
        res["stale_slot_seconds_real"] = round(sum(e["drop_age_s"] for e in stale), 0)
        res["stale_slot_seconds_no_ttl_served"] = round(sum(air_age), 0)
        out[node] = res
        print(node, json.dumps(res, indent=1))
    with open(work() / "cf.json", "w") as fh:
        json.dump(out, fh, indent=1)
