#!/usr/bin/env python3
"""Aggregate link_load_overview data: leaderboards, degree/load distributions."""
import json, sys
from collections import defaultdict

def load(fn):
    return json.load(open(fn))

def hist(values, buckets):
    """ASCII histogram over given bucket edges [(lo,hi,label)...]"""
    counts = []
    for lo, hi, label in buckets:
        c = sum(1 for v in values if lo <= v < hi)
        counts.append((label, c))
    mx = max(c for _, c in counts) or 1
    n = len(values)
    lines = []
    for label, c in counts:
        bar = "#" * max(0, round(40 * c / mx))
        pct = 100.0 * c / n if n else 0
        lines.append(f"{label:>12} | {bar:<40} {c:>5}  ({pct:4.1f}%)")
    return "\n".join(lines)

def main(fn):
    d = load(fn)
    links = d["links"]
    print(f"window={d['window']} segments={len(links)} oldest={d['oldestBucketUnix']}")

    # per-node aggregation
    out_trav = defaultdict(int)   # node as fromCall: frames it transmitted that someone logged
    out_org = defaultdict(int)
    in_trav = defaultdict(int)
    out_deg = defaultdict(int)    # distinct outgoing segments
    in_deg = defaultdict(int)
    relayed_out = defaultdict(int)
    for l in links:
        out_trav[l["fromCall"]] += l["traversals"]
        out_org[l["fromCall"]] += l["origins"]
        in_trav[l["toCall"]] += l["traversals"]
        out_deg[l["fromCall"]] += 1
        in_deg[l["toCall"]] += 1
        relayed_out[l["fromCall"]] += l["relayed"]

    nodes = set(out_trav) | set(in_trav)
    print(f"nodes on segments: {len(nodes)}")

    print("\n== TOP 20 SEGMENTS by origins (distinct stations depending on segment) ==")
    top = sorted(links, key=lambda l: -l["origins"])[:20]
    for l in top:
        rr = l["relayed"] / l["traversals"] if l["traversals"] else 0
        print(f"{l['fromCall']:>12} -> {l['toCall']:<12} origins={l['origins']:>3} trav={l['traversals']:>5} relayed={l['relayed']:>5} ({rr:4.0%})")

    print("\n== TOP 20 NODES by outgoing traversals (TX load leaderboard) ==")
    for n, v in sorted(out_trav.items(), key=lambda x: -x[1])[:20]:
        print(f"{n:>12} out_trav={v:>6} out_deg={out_deg[n]:>3} in_trav={in_trav[n]:>6} in_deg={in_deg[n]:>3} relayed_onward={relayed_out[n]:>5}")

    print("\n== TOP 20 NODES by out-degree (how many distinct stations hear them) ==")
    for n, v in sorted(out_deg.items(), key=lambda x: -x[1])[:20]:
        print(f"{n:>12} out_deg={v:>3} in_deg={in_deg[n]:>3} out_trav={out_trav[n]:>6}")

    print("\n== Distribution: out-degree per node (proxy for 'wie viele hoeren mich') ==")
    vals = [out_deg[n] for n in nodes]
    print(hist(vals, [(0,1,"0"),(1,2,"1"),(2,3,"2"),(3,4,"3"),(4,6,"4-5"),(6,9,"6-8"),(9,13,"9-12"),(13,21,"13-20"),(21,1000,">20")]))

    print("\n== Distribution: in-degree per node (proxy for NC_self: wie viele hoere ich) ==")
    vals = [in_deg[n] for n in nodes]
    print(hist(vals, [(0,1,"0"),(1,2,"1"),(2,3,"2"),(3,4,"3"),(4,6,"4-5"),(6,9,"6-8"),(9,13,"9-12"),(13,21,"13-20"),(21,1000,">20")]))

    print("\n== Distribution: traversals per segment ==")
    vals = [l["traversals"] for l in links]
    print(hist(vals, [(1,2,"1"),(2,3,"2"),(3,5,"3-4"),(5,9,"5-8"),(9,17,"9-16"),(17,33,"17-32"),(33,65,"33-64"),(65,129,"65-128"),(129,100000,">128")]))

    # concentration: share of total traversals carried by top X% nodes
    tot = sum(out_trav.values())
    ranked = sorted(out_trav.values(), reverse=True)
    for frac in (0.01, 0.05, 0.10, 0.20):
        k = max(1, int(len(ranked) * frac))
        share = sum(ranked[:k]) / tot if tot else 0
        print(f"top {frac:4.0%} of nodes ({k:>3}) carry {share:5.1%} of all segment traversals")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "linkload24.json")
