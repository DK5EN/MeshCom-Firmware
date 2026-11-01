"""TTL headroom, TTL sensitivity, ACK wait at empty queue, own beacons and CSMA age-floor episodes.

usage: python3 -I decay_checks.py <norm_log> <writes_jsonl> <out_json>
Needs only RING_* / CSMA_AGED markers (no --setlog), so it works for HB9VQQ-5 as well.
TTL rules: src/txring_ttl.h (prio 5 any status 180 s; DONE only: ACK p1 90 s, p4 120 s, p3 300 s).
"""
from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import dist, hour_key, iter_lines, kv  # noqa: E402

ACK_SRC = {"rx_ack_fwd", "rx_dm_ack_gw", "rx_dm_ack_new"}


def ttl_of(w):
    p = w["prio"]
    if p == 5:
        return 180.0
    if w["status"] != "FF":
        return None
    if p == 1:
        return 90.0 if w["src"] in ACK_SRC else None
    if p == 3:
        return 300.0
    if p == 4:
        return 120.0
    return None


def main(path: str, wl: str, out: str) -> None:
    ws = [json.loads(x) for x in open(wl)]
    aired = [w for w in ws if w["fate"] == "aired" and w.get("lat_ms") is not None]
    res = {}
    cls = defaultdict(list)
    for w in aired:
        t = ttl_of(w)
        if t is None:
            cls["none(%s/p%s)" % (w["src"], w["prio"])].append(w["lat_ms"] / 1000.0)
            continue
        own = "own" if w["src"] in ("auto_pos", "user_wx", "user_msg") else ("gw" if w["src"] == "udp_rx" else "relay")
        cls["p%s_%s(ttl%d)" % (w["prio"], own, t)].append(w["lat_ms"] / 1000.0)
    head = {}
    for k, v in sorted(cls.items()):
        d = dist(v)
        ttl = float(k.split("ttl")[1].rstrip(")")) if "ttl" in k else None
        row = {"n": d["n"], "p50": round(d["p50"], 1), "p90": round(d["p90"], 1), "p99": round(d["p99"], 1), "max": round(d["max"], 1)}
        if ttl:
            row["max_pct_of_ttl"] = round(100 * d["max"] / ttl, 1)
            row["n_ge50pct"] = sum(1 for x in v if x >= 0.5 * ttl)
            row["n_ge80pct"] = sum(1 for x in v if x >= 0.8 * ttl)
        head[k] = row
    res["headroom"] = head
    sens = {}
    for name, sel, thr in (("p5_any", lambda w: w["prio"] == 5, (30, 60, 90, 120, 150, 180)),
                           ("p4_DONE", lambda w: w["prio"] == 4 and w["status"] == "FF", (30, 45, 60, 90, 120)),
                           ("p1_ack", lambda w: w["prio"] == 1 and w["src"] in ACK_SRC, (30, 45, 60, 90))):
        v = [(w["lat_ms"] / 1000.0, w["src"] in ("auto_pos", "user_wx")) for w in aired if sel(w)]
        sens[name] = {"n": len(v), "would_drop_if_ttl": {str(t): {"all": sum(1 for x, o in v if x > t), "own": sum(1 for x, o in v if x > t and o)} for t in thr}}
    res["ttl_sensitivity_aired_entries"] = sens
    # ACK wait at empty queue
    e1 = [w["lat_ms"] / 1000.0 for w in aired if w["prio"] == 1 and w["queued"].startswith("0/")]
    a1 = [w["lat_ms"] / 1000.0 for w in aired if w["prio"] == 1 and not w["queued"].startswith("0/")]
    res["prio1_wait_s"] = {"queue_empty_at_write": dist(e1), "queue_nonempty_at_write": dist(a1)}
    rl = [w["lat_ms"] / 1000.0 for w in aired if w["src"] == "rx_relay" and w["prio"] == 5 and w["queued"].startswith("0/")]
    rl2 = [w["lat_ms"] / 1000.0 for w in aired if w["src"] == "rx_relay" and w["prio"] == 5 and not w["queued"].startswith("0/")]
    res["relay_p5_wait_s"] = {"queue_empty_at_write": dist(rl), "queue_nonempty_at_write": dist(rl2)}
    # own beacons
    ob = {}
    for src in ("auto_pos", "user_wx"):
        l = [w for w in ws if w["src"] == src]
        wt = sorted(w["t"] for w in l)
        gaps = [b - a for a, b in zip(wt, wt[1:])]
        ob[src] = {"written": len(l), "fate": dict(Counter(w["fate"] for w in l)),
                   "wait_s": dist([w["lat_ms"] / 1000.0 for w in l if w.get("lat_ms") is not None]),
                   "write_gap_s": dist(gaps), "write_gap_lt_450s": sum(1 for g in gaps if g < 450), "write_gap_gt_1200s": sum(1 for g in gaps if g > 1200),
                   "wait_gt_60s": sum(1 for w in l if (w.get("lat_ms") or 0) > 60000), "wait_gt_120s": sum(1 for w in l if (w.get("lat_ms") or 0) > 120000)}
    res["own_beacons"] = ob
    # relayed vs own wait, same prio 5
    res["p5_wait_own_vs_relay_s"] = {"own": dist([w["lat_ms"] / 1000.0 for w in aired if w["src"] == "auto_pos"]),
                                     "relay": dist([w["lat_ms"] / 1000.0 for w in aired if w["src"] == "rx_relay" and w["prio"] == 5])}
    # CSMA aged episodes joined to writes
    by_slot = defaultdict(list)
    for w in ws:
        by_slot[w["slot"]].append(w)
    ev = []
    for n, t, b in iter_lines(path):
        if b.startswith("[MC-DBG] CSMA_AGED "):
            d = kv(b)
            s = int(d["slot"])
            m = None
            for w in by_slot.get(s, []):
                if w["t"] - 0.5 <= t <= (w["end"] or t) + 0.5:
                    m = w
                    break
            ev.append((t, s, int(d["age"]), int(d["stage"]), int(d["hold"]), m))
    eps = []
    for e in ev:
        if eps and eps[-1][-1][5] is e[5] and e[5] is not None:
            eps[-1].append(e)
        elif eps and eps[-1][-1][1] == e[1] and e[0] - eps[-1][-1][0] < 60:
            eps[-1].append(e)
        else:
            eps.append([e])
    cs = {"events": len(ev), "episodes": len(eps), "unmatched_events": sum(1 for e in ev if e[5] is None),
          "episodes_by_src_prio": dict(Counter("%s/p%s" % (e[0][5]["src"], e[0][5]["prio"]) if e[0][5] else "?" for e in eps)),
          "events_by_src_prio": dict(Counter("%s/p%s" % (e[5]["src"], e[5]["prio"]) if e[5] else "?" for e in ev)),
          "age_at_first_event_s": dist([e[0][2] / 1000.0 for e in eps]),
          "all_episodes_end_aired": all((e[0][5] or {}).get("fate") == "aired" for e in eps),
          "remaining_wait_after_first_event_s": dist([((e[0][5] or {}).get("end", e[0][0]) - e[0][0]) for e in eps if e[0][5]]),
          "per_hour": dict(sorted(Counter(hour_key(e[0]) for e in ev).items()))}
    res["csma_aged"] = cs
    # TX with wait >= 20 s among prio 1-3 (floor population)
    pop = [w for w in aired if w["prio"] in (1, 2, 3)]
    res["prio1to3_aired"] = {"n": len(pop), "wait_ge20s": sum(1 for w in pop if w["lat_ms"] >= 20000), "wait_ge60s": sum(1 for w in pop if w["lat_ms"] >= 60000),
                             "wait_ge20s_by_src": dict(Counter(w["src"] for w in pop if w["lat_ms"] >= 20000))}
    json.dump(res, open(out, "w"), indent=1, default=str)
    print(json.dumps(res, indent=1, default=str)[:7000])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
