#!/usr/bin/env python3
"""Q3/Q4 (b): CSMA age floor (CSMA_AGED). Writes work/csma.json.

Episode = one ring slot (slot + estimated enqueue time) that produced >=1 CSMA_AGED event.
Quiet-gap replay: after each decoded RX (ONRXDONE_TIME) the hold restarts; the TX gate fires at r_i + H
when no further RX arrives within H. Aged hold = logged hold; "today" hold = prio base (+175 ms mean jitter)
for attempt 0 and base*2/3 for attempt 2 (range).
"""
from __future__ import annotations

import json
from bisect import bisect_left
from collections import Counter, defaultdict

from .common import NODES, load, pct, toa_ms, work
from .ringq import entries

BASE = {1: 3000, 2: 3000, 3: 4500}
JIT = 175


def fire_time(rx: list[float], i0: int, hold: float, t_min: float) -> float | None:
    """first fire >= t_min given RX arrivals rx[i0:], hold in s."""
    i = i0
    while i < len(rx):
        nxt = rx[i + 1] if i + 1 < len(rx) else float("inf")
        if nxt - rx[i] > hold and rx[i] + hold >= t_min:
            return rx[i] + hold
        i += 1
    return None


def run() -> None:
    out: dict = {}
    for node in NODES:
        d = load(node)
        es = entries(d)
        by_slot: dict[int, list] = defaultdict(list)
        for e in es:
            by_slot[e["slot"]].append(e)
        rx = sorted(d["onrx"])
        # episodes
        ev_by: dict[tuple, list] = {}
        for (t, slot, age, stage, hold, ln) in d["csma"]:
            enq = t - age / 1000.0
            key = None
            for k in list(ev_by.keys())[-40:]:
                if k[0] == slot and abs(k[1] - enq) < 1.5:
                    key = k
                    break
            if key is None:
                key = (slot, enq)
                ev_by[key] = []
            ev_by[key].append((t, age, stage, hold))
        eps = []
        for (slot, enq), evs in ev_by.items():
            # entry: latest write on slot with t <= enq+1.5
            cand = [e for e in by_slot.get(slot, []) if e["t"] <= enq + 1.5]
            e = cand[-1] if cand else None
            ep = {"slot": slot, "enq": enq, "n_events": len(evs), "t_a": evs[0][0], "t_l": evs[-1][0],
                  "stage_max": max(x[2] for x in evs), "age_max_s": max(x[1] for x in evs) / 1000.0,
                  "hold_med_ms": pct([x[3] for x in evs], 50)}
            if e is not None and abs(e["t"] - enq) < 3.0:
                ep.update(prio=e["prio"], type=e["type"], outcome=e["outcome"], t_x=e["t_end"],
                          lat_s=(e["lat_ms"] / 1000 if e["lat_ms"] else None), src=e["src"])
            else:
                ep.update(prio=None, outcome="nomatch", t_x=None, lat_s=None)
            eps.append(ep)
        res: dict = {"events": len(d["csma"]), "episodes": len(eps),
                     "stage_events": dict(Counter(x[3] for x in d["csma"])),
                     "stage_episodes": dict(Counter(ep["stage_max"] for ep in eps)),
                     "ep_prio": dict(Counter(ep["prio"] for ep in eps)),
                     "ep_outcome": dict(Counter(ep["outcome"] for ep in eps)),
                     "hold_ms": {"p10": pct([x[4] for x in d["csma"]], 10), "p50": pct([x[4] for x in d["csma"]], 50),
                                 "p90": pct([x[4] for x in d["csma"]], 90)} if d["csma"] else None}
        # TX population
        tx = d["logtx"]   # (t, mid, ptype, hop, prio, src, wait, q, cad, len, ts, line)
        le3 = [x for x in tx if x[4] <= 3]
        aged = [x for x in le3 if x[6] >= 20000]
        res["tx_prio_le3"] = len(le3)
        res["tx_prio_le3_wait_ge20s"] = len(aged)
        res["tx_prio_le3_wait_ge20s_pct"] = round(100 * len(aged) / max(1, len(le3)), 1)
        res["tx_prio_le3_wait_ge60s"] = sum(1 for x in le3 if x[6] >= 60000)
        # quiet-gap replay for sent episodes
        deltas_lo, deltas_hi, real_rem, fire_vs_tx = [], [], [], []
        n_eval = 0
        for ep in eps:
            if ep["outcome"] != "sent" or ep["t_x"] is None or ep["prio"] not in (1, 2, 3):
                continue
            i0 = bisect_left(rx, ep["t_a"] - 0.05)
            h_aged = (0.15 if ep["stage_max"] == 2 else ep["hold_med_ms"] / 1000.0)
            h_aged = ep["hold_med_ms"] / 1000.0
            f_a = fire_time(rx, i0, h_aged, ep["t_a"])
            base = BASE[ep["prio"]]
            f_u0 = fire_time(rx, i0, (base + JIT) / 1000.0, ep["t_a"])
            f_u2 = fire_time(rx, i0, (base * 2 / 3 + JIT) / 1000.0, ep["t_a"])
            if f_a is None or f_u0 is None or f_u2 is None:
                continue
            n_eval += 1
            deltas_hi.append(max(0.0, f_u0 - f_a))
            deltas_lo.append(max(0.0, f_u2 - f_a))
            real_rem.append(ep["t_x"] - ep["t_a"])
            fire_vs_tx.append(ep["t_x"] - f_a)
        res["replay"] = {"n_episodes_eval": n_eval}
        if n_eval:
            res["replay"].update({
                "real_remaining_after_first_aged_event_s": {"p50": round(pct(real_rem, 50), 1), "p90": round(pct(real_rem, 90), 1),
                                                             "sum": round(sum(real_rem))},
                "saved_s_vs_attempt0_hold": {"p50": round(pct(deltas_hi, 50), 2), "p90": round(pct(deltas_hi, 90), 2),
                                             "sum": round(sum(deltas_hi)), "pct_ep_gt_1s": round(100 * sum(1 for x in deltas_hi if x > 1) / n_eval, 1)},
                "saved_s_vs_attempt2_hold": {"p50": round(pct(deltas_lo, 50), 2), "p90": round(pct(deltas_lo, 90), 2),
                                             "sum": round(sum(deltas_lo)), "pct_ep_gt_1s": round(100 * sum(1 for x in deltas_lo if x > 1) / n_eval, 1)},
                "real_tx_minus_sim_fire_s": {"p10": round(pct(fire_vs_tx, 10), 2), "p50": round(pct(fire_vs_tx, 50), 2),
                                             "p90": round(pct(fire_vs_tx, 90), 2)},
                "pct_saved_of_real_remaining_attempt0": round(100 * sum(deltas_hi) / max(1.0, sum(real_rem)), 1),
            })
        # latency aged vs non-aged by prio (only informative as a population description)
        lat = {}
        for p in (1, 3):
            a = [x[6] / 1000 for x in tx if x[4] == p and x[6] >= 20000]
            n = [x[6] / 1000 for x in tx if x[4] == p and x[6] < 20000]
            lat[f"p{p}"] = {"n_ge20": len(a), "n_lt20": len(n),
                            "ge20_p50": round(pct(a, 50), 1) if a else None, "ge20_p90": round(pct(a, 90), 1) if a else None,
                            "lt20_p50": round(pct(n, 50), 1) if n else None}
        res["latency_split"] = lat
        # collision / double-TX proxies: aged (prio<=3, wait>=20s) vs non-aged (prio<=3, wait<20s)
        err_t = sorted(d["onrxerr"])
        mid_rx: dict[str, list] = defaultdict(list)
        for r in d["logrx"]:
            mid_rx[r[2]].append(r[0])
        mid_tx: dict[str, list] = defaultdict(list)
        for x in tx:
            mid_tx[x[1]].append(x[0])

        def probe(pop):
            if not pop:
                return None
            cad2 = sum(1 for x in pop if x[8] >= 2)
            cad3 = sum(1 for x in pop if x[8] >= 3)
            err3 = 0
            echo = 0
            dbl = 0
            for x in pop:
                t0 = x[0]
                toa = toa_ms(x[9]) / 1000
                j = bisect_left(err_t, t0 + toa)
                if j < len(err_t) and err_t[j] <= t0 + toa + 3.0:
                    err3 += 1
                if any(t0 + 0.5 < tt <= t0 + 60 for tt in mid_rx.get(x[1], [])):
                    echo += 1
                if sum(1 for tt in mid_tx[x[1]] if abs(tt - t0) <= 120) > 1:
                    dbl += 1
            n = len(pop)
            return {"n": n, "cad_mean": round(sum(x[8] for x in pop) / n, 2), "pct_cad_ge2": round(100 * cad2 / n, 1),
                    "pct_cad_ge3": round(100 * cad3 / n, 1), "pct_rxerr_within_3s_after_tx": round(100 * err3 / n, 1),
                    "pct_echo_within_60s": round(100 * echo / n, 1), "pct_dup_tx_within_120s": round(100 * dbl / n, 2)}

        for p in (1, 3):
            res[f"probe_p{p}_aged"] = probe([x for x in tx if x[4] == p and x[6] >= 20000])
            res[f"probe_p{p}_fresh"] = probe([x for x in tx if x[4] == p and x[6] < 20000])
        # baseline: P(>=1 RX_ERROR in a 3s window), random times across the span
        if err_t and tx:
            span0, span1 = tx[0][0], tx[-1][0]
            import random
            rnd = random.Random(1)
            hit = 0
            N = 4000
            for _ in range(N):
                t = rnd.uniform(span0, span1)
                j = bisect_left(err_t, t)
                if j < len(err_t) and err_t[j] <= t + 3.0:
                    hit += 1
            res["baseline_pct_rxerr_in_3s"] = round(100 * hit / N, 1)
        out[node] = res
        print(node, json.dumps(res))
    with open(work() / "csma.json", "w") as fh:
        json.dump(out, fh, indent=1)
