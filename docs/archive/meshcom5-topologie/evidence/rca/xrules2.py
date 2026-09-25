#!/usr/bin/env python3
"""Exclusive contribution #X per direct neighbour under three coverage rules, replayed from [NBR] lines.
R12: any hit in 12 h (firmware today). R60: any hit in the last 60 min. SHARE: hits in 12 h >= 10 % of the
strongest direct hearer of that station. Text edges ('T') are dropped so old runs match 8487ea2a semantics."""
import sys, re
from datetime import datetime, timedelta
from collections import defaultdict
W12, W60 = timedelta(hours=12), timedelta(minutes=60)
files = sys.argv[1:]
hits = defaultdict(list)      # (frm,to) -> [datetime]
me = defaultdict(list)        # frm -> [datetime]  (I heard frm)
snaps = []
last_up = None
own = 'DK5EN-98'
def reset():
    hits.clear(); me.clear()
for fn in files:
    for line in open(fn, errors='replace'):
        if '[NBR]|' not in line: continue
        try: t = datetime.strptime(line[:23], '%Y-%m-%d %H:%M:%S.%f')
        except ValueError: continue
        f = line.split('[NBR]|', 1)[1].strip().split('|')
        kind = f[0]
        try: up = int(f[1])
        except (IndexError, ValueError): continue
        if last_up is not None and up + 5 < last_up:   # reboot
            reset()
        last_up = up
        if kind == 'EDGE' and len(f) >= 5 and f[4] != 'T':
            hits[(f[2], f[3])].append(t)
        elif kind == 'ME' and len(f) >= 3:
            me[f[2]].append(t)
        elif kind == 'EVICT' and len(f) >= 5:
            old = f[3]
            for k in [k for k in hits if old in k]: del hits[k]
            me.pop(old, None)
        elif kind == 'SNAP':
            snaps.append((t, up))
            # compute at this moment
            D = {x for x, ts in me.items() if ts and t - ts[-1] < W12 and x != own}
            def heard_by(N, rule):
                out = set()
                for (frm, to), ts in hits.items():
                    if to != N: continue
                    rec = [x for x in ts if t - x < W12]
                    if not rec: continue
                    if rule == 'R60' and t - rec[-1] >= W60: continue
                    out.add((frm, len(rec)))
                return out
            res = {}
            for rule in ('R12', 'R60', 'SHARE'):
                H = {N: heard_by(N, 'R60' if rule == 'R60' else 'R12') for N in D}
                if rule == 'SHARE':
                    best = defaultdict(int)
                    for N in D:
                        for frm, c in H[N]: best[frm] = max(best[frm], c)
                    Hs = {N: {frm for frm, c in H[N] if c >= max(1, 0.10 * best[frm])} for N in D}
                else:
                    Hs = {N: {frm for frm, c in H[N]} for N in D}
                for N in D:
                    others = set().union(*[Hs[M] for M in D if M != N]) if len(D) > 1 else set()
                    X = Hs[N] - D - {own} - others
                    res[(rule, N)] = (len(Hs[N] - D - {own}), len(X), sorted(X))
            snaps[-1] = (t, up, res, D)
watch = ['DB0ED-99', 'DL2JA-2', 'DL2UD-1', 'DK5EN-1']
import json
series=[]
for s in snaps:
    if len(s) < 4: continue
    t, up, res, D = s
    series.append({'t': t.strftime('%Y-%m-%d %H:%M'), 'up': up, **{f'{w}|{r}': (res[(r, w)][1] if (r, w) in res else None) for w in ['DB0ED-99','DL2JA-2'] for r in ('R12','R60','SHARE')}})
json.dump(series, open('xrules-series.json','w'), indent=0)
print(f"{'time':12s} {'up':>4s} | " + ' | '.join(f"{w:>9s} R12  R60  SH" for w in watch))
flips = defaultdict(int); prev = {}
pos = defaultdict(lambda: defaultdict(int)); cnt = 0
for s in snaps:
    if len(s) < 4: continue
    t, up, res, D = s; cnt += 1
    row = []
    for w in watch:
        cells = []
        for rule in ('R12', 'R60', 'SHARE'):
            v = res.get((rule, w))
            cells.append('-' if v is None else f"{v[0]}/{v[1]}")
            if v is not None:
                pos[w][rule] += (v[1] > 0)
                key = (w, rule); st = v[1] > 0
                if key in prev and prev[key] != st: flips[key] += 1
                prev[key] = st
        row.append(' '.join(f"{c:>5s}" for c in cells))
    if cnt % 4 == 0: print(f"{t:%m-%d %H:%M} {up:4d} | " + ' | '.join(f"{'':3s}{r}" for r in row))
print('\nshare of snapshots with #X>0 and role flips (needed<->redundant):')
for w in watch:
    print(w, {r: f"{pos[w][r]}/{cnt} flips {flips[(w,r)]}" for r in ('R12','R60','SHARE')})
last = snaps[-1]
print('\nlast snapshot', last[0], 'exclusive sets:')
for rule in ('R12','R60','SHARE'):
    for w in watch:
        v = last[2].get((rule, w))
        if v: print(' ', rule, w, v[2])
