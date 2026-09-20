#!/usr/bin/env python3
"""Analyze 24h of HEY frames: origins, nct, paths, relays, loops, importance simulation."""
import json, time
from collections import defaultdict, Counter

_seen_lines = set()
heys = []
for l in open("heys.jsonl"):
    if l not in _seen_lines:
        _seen_lines.add(l)
        heys.append(json.loads(l))
del _seen_lines
fleet = {n["call"]: n for n in json.load(open("fleet.json"))}

def hist(counter_pairs, total=None, width=40):
    mx = max(c for _, c in counter_pairs) or 1
    n = total or sum(c for _, c in counter_pairs)
    out = []
    for label, c in counter_pairs:
        bar = "#" * max(0, round(width * c / mx))
        out.append(f"{str(label):>14} | {bar:<{width}} {c:>6}  ({100.0*c/n:4.1f}%)")
    return "\n".join(out)

print(f"HEY frames (24h): {len(heys)}")

# hourly coverage
byhour = Counter(h["timestamp"] // 3600 * 3600 for h in heys)
hours = sorted(byhour)
print(f"hours covered: {len(hours)}, min/h={min(byhour.values())}, max/h={max(byhour.values())}, avg/h={sum(byhour.values())//len(byhour)}")
for hh in hours:
    print(" ", time.strftime("%m-%d %H:00", time.gmtime(hh)), byhour[hh])

# origins
per_origin = defaultdict(list)
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    if not path:
        continue
    per_origin[path[0]].append(h)

print(f"distinct HEY origins (24h): {len(per_origin)}")
print(f"fleet size (nodes DB): {len(fleet)}")

# gw flag and nct per origin (latest frame wins)
nct_latest = {}
gw_flag = {}
for o, frames in per_origin.items():
    latest = max(frames, key=lambda f: f["timestamp"])
    nct_latest[o] = latest.get("nct", 0)
    gw_flag[o] = latest.get("gw", 0)

nct_vals = list(nct_latest.values())
zero = sum(1 for v in nct_vals if v == 0)
print(f"\norigins reporting nct==0: {zero} ({100*zero/len(nct_vals):.1f}%)  nct>0: {len(nct_vals)-zero}")
print(f"origins with gw=1: {sum(gw_flag.values())}")

print("\n== NCNT (nct) distribution over reporting origins (latest frame) ==")
buckets = [(0,1,"0 (unbek.)"),(1,2,"1"),(2,3,"2"),(3,4,"3"),(4,6,"4-5"),(6,9,"6-8"),(9,13,"9-12"),(13,21,"13-20"),(21,41,"21-40"),(41,80,"41-79"),(80,99,"80-98"),(99,1000,"99 (Kappe)")]
pairs = [(lbl, sum(1 for v in nct_vals if lo <= v < hi)) for lo, hi, lbl in buckets]
print(hist(pairs, total=len(nct_vals)))

nz = sorted(v for v in nct_vals if v > 0)
if nz:
    import statistics
    print(f"\nnct>0: n={len(nz)} min={nz[0]} p25={nz[len(nz)//4]} median={nz[len(nz)//2]} p75={nz[3*len(nz)//4]} p90={nz[int(len(nz)*0.9)]} max={nz[-1]}")

# path length distribution
plen = Counter()
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    plen[len(path)] += 1
print("\n== Path length distribution (1=Selbstupload des GW, 2=direkt gehoert, >=3 relayed) ==")
pairs = [(k, plen[k]) for k in sorted(plen)]
print(hist(pairs, total=len(heys)))

# relay leaderboard: intermediate positions
relay_count = Counter()
lasthop_count = Counter()
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    if len(path) >= 3:
        for r in path[1:-1]:
            relay_count[r] += 1
    if len(path) >= 2:
        lasthop_count[path[-1]] += 1

print("\n== TOP 25 RELAYS (Auftreten als Zwischenhop in HEY-Pfaden, 24h) ==")
for r, c in relay_count.most_common(25):
    fw = (fleet.get(r) or {}).get("firmware", "?")
    hw = (fleet.get(r) or {}).get("hardware", "?")
    print(f"{r:>12} relays={c:>5} nct_own={nct_latest.get(r,'-'):>4} gw={gw_flag.get(r,'-')} fw={fw} hw={hw}")

print("\n== TOP 15 GATEWAYS/last-hops (wer meldet die meisten fremden Frames) ==")
for r, c in lasthop_count.most_common(15):
    print(f"{r:>12} reports={c:>5}")

# loops: repeated callsign in path
loops = [h for h in heys if (lambda p: len(p) != len(set(p)))([x for x in h.get("path","").split(",") if x])]
print(f"\npaths with repeated callsign (LOOPS): {len(loops)} ({100*len(loops)/len(heys):.2f}%)")
for h in loops[:8]:
    print("  ", h["path"], "ts", h["timestamp"])

# zombie: same msg_id spanning long time
span = defaultdict(lambda: [None, None])
for h in heys:
    mid = h.get("msg_id")
    ts = h["timestamp"]
    s = span[mid]
    s[0] = ts if s[0] is None else min(s[0], ts)
    s[1] = ts if s[1] is None else max(s[1], ts)
spans = sorted(((b - a, m) for m, (a, b) in span.items() if b - a > 0), reverse=True)
print(f"\nmsg_ids seen more than once: {sum(1 for d,_ in spans)}, with span>60s: {sum(1 for d,_ in spans if d>60)}, span>300s: {sum(1 for d,_ in spans if d>300)}, span>900s: {sum(1 for d,_ in spans if d>900)}")
print("longest spans (s):", [d for d, _ in spans[:10]])

# copies per msg_id
copies = Counter()
for m, (a, b) in span.items():
    pass
cc = Counter()
for h in heys:
    cc[h["msg_id"]] += 1
dist = Counter(cc.values())
print("\n== Kopien pro msg_id (im Feed sichtbare Duplikate desselben Frames) ==")
pairs = [(k, dist[k]) for k in sorted(dist)][:10]
print(hist(pairs, total=sum(dist.values())))
uniq = len(cc)
print(f"unique msg_ids: {uniq}, frames: {len(heys)}, feed DUP/NEW ratio: {(len(heys)-uniq)/uniq:.2f}")

# ---------------- Importance simulation on observed topology ----------------
# heard-by graph: consecutive path pairs X->Y mean Y heard X directly
hears = defaultdict(set)   # node -> set of stations it HEARS directly
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    for a, b in zip(path, path[1:]):
        hears[b].add(a)

# nct known for neighbor X if X reported nct>0 in 24h
print(f"\nnodes with observed 'hears' neighbors: {len(hears)}")

imp = {}
known_ratio = {}
for n, nbrs in hears.items():
    s = 0.0
    known = 0
    for x in nbrs:
        v = nct_latest.get(x, 0)
        if v > 0:
            s += 1.0 / v
            known += 1
        else:
            s += 1.0
    imp[n] = s
    known_ratio[n] = known / len(nbrs) if nbrs else 0.0

kr = sorted(known_ratio.values())
print("\n== known_ratio: Anteil der Nachbarn mit bekanntem nct (Qualitaetsgate 3.1) ==")
buckets = [(0,0.001,"0%"),(0.001,0.25,"<25%"),(0.25,0.5,"25-49%"),(0.5,0.75,"50-74%"),(0.75,0.999,"75-99%"),(0.999,2,"100%")]
pairs = [(lbl, sum(1 for v in kr if lo <= v < hi)) for lo, hi, lbl in buckets]
print(hist(pairs, total=len(kr)))
ge50 = sum(1 for v in kr if v >= 0.5)
print(f"nodes passing gate (known_ratio >= 50%): {ge50}/{len(kr)} ({100*ge50/len(kr):.1f}%)")

print("\n== Importance-Verteilung (Sim auf beobachteter Topologie, konservativ unbekannt=1.0) ==")
iv = sorted(imp.values())
buckets = [(0,0.5,"<0.5"),(0.5,1,"0.5-1"),(1,2,"1-2"),(2,3,"2-3"),(3,5,"3-5"),(5,8,"5-8"),(8,12,"8-12"),(12,20,"12-20"),(20,1000,">=20")]
pairs = [(lbl, sum(1 for v in iv if lo <= v < hi)) for lo, hi, lbl in buckets]
print(hist(pairs, total=len(iv)))
print(f"median={iv[len(iv)//2]:.2f} p90={iv[int(len(iv)*0.9)]:.2f} max={iv[-1]:.2f}")
over_cap = sum(1 for v in iv if v >= 8.0)
print(f"nodes at/over IMP_CAP=8: {over_cap} ({100*over_cap/len(iv):.1f}%)")

# slot_start distribution (nur Gate-Passierer, wie ADR)
print("\n== slot_start-Verteilung (nur Nodes die das 50%-Gate passieren) ==")
slots = Counter()
for n, v in imp.items():
    if known_ratio[n] >= 0.5:
        r = min(v, 8.0) / 8.0
        slots[int((1.0 - r) * 7)] += 1
pairs = [(f"slot {k}..{k+2}", slots[k]) for k in sorted(slots)]
print(hist(pairs, total=sum(slots.values())))

# Was waere OHNE Gate (alle Nodes, Mixed-Fleet-Realitaet)
print("\n== slot_start-Verteilung OHNE Gate (alle Nodes, heutige Datenlage) ==")
slots2 = Counter()
for n, v in imp.items():
    r = min(v, 8.0) / 8.0
    slots2[int((1.0 - r) * 7)] += 1
pairs = [(f"slot {k}..{k+2}", slots2[k]) for k in sorted(slots2)]
print(hist(pairs, total=sum(slots2.values())))

# importance vs importance-with-full-knowledge (both-known subset)
# how different would importance be if unknown were skipped instead of 1.0
imp_skip = {}
for n, nbrs in hears.items():
    s = sum(1.0 / nct_latest[x] for x in nbrs if nct_latest.get(x, 0) > 0)
    imp_skip[n] = s
big_shift = sum(1 for n in imp if imp[n] >= 8.0 and imp_skip[n] < 2.0)
print(f"\nnodes at cap (>=8) under conservative rule but <2.0 if unknowns skipped: {big_shift}")

# fleet firmware distribution
fwc = Counter((n.get("firmware") or "?") for n in fleet.values())
print("\n== Fleet firmware distribution (nodes DB) ==")
for fw, c in fwc.most_common(15):
    print(f"{fw!r:>16} {c:>5}  ({100*c/len(fleet):.1f}%)")

hwc = Counter((str(n.get("hardware")) or "?") for n in fleet.values())
print("\n== Fleet hardware distribution (top 15) ==")
for hw, c in hwc.most_common(15):
    print(f"{hw!r:>24} {c:>5}  ({100*c/len(fleet):.1f}%)")
