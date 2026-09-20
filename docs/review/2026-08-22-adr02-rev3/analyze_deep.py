#!/usr/bin/env python3
"""Deep dives: relay mheard segments, cap alternatives, hub slot check, zombies, spike."""
import json, time
from collections import defaultdict, Counter

_seen = set()
heys = []
for l in open("heys.jsonl"):
    if l not in _seen:
        _seen.add(l)
        heys.append(json.loads(l))

def hist(pairs, total=None, width=40):
    mx = max(c for _, c in pairs) or 1
    n = total or sum(c for _, c in pairs) or 1
    return "\n".join(f"{str(l):>14} | {'#'*max(0,round(width*c/mx)):<{width}} {c:>6}  ({100.0*c/n:4.1f}%)"
                     for l, c in pairs)

# ---- 1. relay mheard from 3-value rssi segments ----
relay_mh = []          # (relay_call, mheard) from mid-path 3-value segments
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    segs = [s for s in h.get("rssi", "").split(";") if s.strip()]
    # segments map to hops: path[i]->path[i+1] for i in 0..len(path)-2
    # 3-value segment: receiver's mheard,rssi,snr ; receiver = path[i+1]
    for i, s in enumerate(segs):
        vals = s.split(",")
        if len(vals) >= 3 and i + 1 < len(path):
            try:
                mh = int(vals[0])
                relay_mh.append((path[i + 1], mh))
            except ValueError:
                pass

per_relay_mh = defaultdict(list)
for c, m in relay_mh:
    per_relay_mh[c].append(m)
latest_mh = {c: v[-1] for c, v in per_relay_mh.items()}
print(f"3-value relay segments: {len(relay_mh)} from {len(per_relay_mh)} distinct relays")
vals = sorted(latest_mh.values())
if vals:
    print(f"relay mheard: min={vals[0]} median={vals[len(vals)//2]} p90={vals[int(len(vals)*0.9)]} max={vals[-1]}")
sat = Counter()
for c, v in latest_mh.items():
    for cap in (10, 30, 50, 80):
        if v >= cap - 1:
            sat[cap] += 1
print("relays reporting mheard >= cap-1 (saturation candidates):", dict(sat))

# ---- 2. importance sim + alternative caps ----
nct_latest = {}
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    if path:
        nct_latest.setdefault(path[0], 0)
per_origin_latest = {}
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    if path:
        o = path[0]
        if o not in per_origin_latest or h["timestamp"] > per_origin_latest[o]["timestamp"]:
            per_origin_latest[o] = h
for o, h in per_origin_latest.items():
    nct_latest[o] = h.get("nct", 0)

hears = defaultdict(set)
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    for a, b in zip(path, path[1:]):
        hears[b].add(a)

imp = {}
for n, nbrs in hears.items():
    s = 0.0
    for x in nbrs:
        v = nct_latest.get(x, 0)
        s += (1.0 / v) if v > 0 else 1.0
    imp[n] = s

iv = sorted(imp.values())
print("\nimportance percentiles:", {p: round(iv[int(len(iv)*p/100)], 2) for p in (10,25,50,75,90,95,99)})

for CAP in (8.0, 4.0, 3.0, 2.0):
    slots = Counter()
    for n, v in imp.items():
        r = min(v, CAP) / CAP
        slots[int((1.0 - r) * 7)] += 1
    pairs = [(f"slot {k}..{k+2}", slots[k]) for k in sorted(slots)]
    print(f"\n== slot_start distribution with IMP_CAP={CAP} ==")
    print(hist(pairs, total=sum(slots.values())))

# ---- 3. do the real workhorse relays get front slots? ----
relay_count = Counter()
for h in heys:
    path = [p for p in h.get("path", "").split(",") if p]
    if len(path) >= 3:
        for r in path[1:-1]:
            relay_count[r] += 1

print("\n== TOP 20 relays: real load vs simulated importance/slot (CAP=8 vs CAP=3) ==")
ranked_imp = sorted(imp.values(), reverse=True)
def imp_rank(v):
    return sum(1 for x in ranked_imp if x > v) + 1
for r, c in relay_count.most_common(20):
    v = imp.get(r)
    if v is None:
        print(f"{r:>12} load={c:>5} imp=NONE (nie als Empfaenger beobachtet)")
        continue
    s8 = int((1.0 - min(v, 8) / 8) * 7)
    s3 = int((1.0 - min(v, 3) / 3) * 7)
    print(f"{r:>12} load={c:>5} imp={v:5.2f} rank={imp_rank(v):>3}/{len(iv)} slots(cap8)={s8}..{s8+2} slots(cap3)={s3}..{s3+2}")

# spearman-ish: share of top-20-load relays that land in front slots
front8 = sum(1 for r, _ in relay_count.most_common(20) if r in imp and int((1.0-min(imp[r],8)/8)*7) <= 2)
front3 = sum(1 for r, _ in relay_count.most_common(20) if r in imp and int((1.0-min(imp[r],3)/3)*7) <= 2)
print(f"top-20 load relays in slots<=2: cap8={front8}/20, cap3={front3}/20")

# ---- 4. zombie vs wrap ----
first_last = {}
cnt = Counter()
for h in heys:
    mid = h["msg_id"]
    ts = h["timestamp"]
    cnt[mid] += 1
    if mid in first_last:
        a, b = first_last[mid]
        first_last[mid] = (min(a, ts), max(b, ts))
    else:
        first_last[mid] = (ts, ts)
spans = [b - a for a, b in first_last.values() if b > a]
buckets = [(1,60,"1-59s"),(60,300,"60-299s"),(300,600,"5-10min"),(600,900,"10-15min"),(900,1800,"15-30min"),(1800,3600,"30-60min"),(3600,86400,">1h (Wrap?)")]
pairs = [(lbl, sum(1 for s in spans if lo <= s < hi)) for lo, hi, lbl in buckets]
print("\n== msg_id Zeitspannen (Erst- bis Letztauftreten) ==")
print(hist(pairs, total=len(spans)))
z = sorted((s for s in spans if 600 <= s < 3600))
print(f"echte Zombie-Kandidaten (10-60min): {len(z)}")

# ---- 5. spike hour 04:00 UTC ----
SPIKE = 1787371200
sp = [h for h in heys if SPIKE <= h["timestamp"] < SPIKE + 3600]
prev = [h for h in heys if SPIKE - 3600 <= h["timestamp"] < SPIKE]
print(f"\nspike hour frames: {len(sp)} (prev hour: {len(prev)})")
oc = Counter([p for h in sp for p in [h['path'].split(',')[0]]])
oc_prev = Counter([p for h in prev for p in [h['path'].split(',')[0]]])
print("top origins in spike hour (count, prev-hour count):")
for o, c in oc.most_common(12):
    print(f"  {o:>12} {c:>5} (prev {oc_prev.get(o,0)})")
