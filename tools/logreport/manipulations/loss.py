"""Q6.4: did cluster neighbours that A thought it needed (NEED mask) actually get the frame, by A's relay fate?"""
import json, bisect; 
from common import CALL2NODE, NODES, OUTD, load
from collections import Counter, defaultdict
ev={n:load(n) for n in NODES}
# per node: id -> times heard (rx any copy, ack excluded)
heard={n:defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e['k']=='rx': heard[n][e['id']].append(e['t'])
# link probability gate (from linkp): only pairs with p_nonack>=0.15 qualify as "A can reach N"
lp=json.load(open(OUTD/'work'/'linkp.json'))
good={tuple(k.split('->')):v['p_nonack'] for k,v in lp.items() if v['p_nonack'] and v['p_nonack']>=0.15}
rng={n:(ev[n][0]['t'],ev[n][-1]['t']) for n in NODES}
res={}
tot=Counter(); rows=[]
for a in NODES:
    e_a=ev[a]
    snaps={}
    for e in e_a:
        if e['k']=='row': snaps.setdefault(e['up'],{})[e['idx']]=e['call']
    ups=sorted(snaps)
    sent=set(); canc=set(); sup=set(); stale=set()
    tx_t=defaultdict(list)
    for e in e_a:
        if e['k']=='tx' and e['src']=='r': tx_t[e['id']].append(e['t'])
        elif e['k']=='cancel': canc.add(e['id'])
        elif e['k']=='supersede': sup.add(e['old'])
        elif e['k']=='dropstale': stale.add(e['id'])
    gwi=set(e['id'] for e in e_a if e['k']=='gwi')
    for e in e_a:
        if e['k']!='need' or e['id'] in gwi: continue
        try: nd=int(e['need'],16)
        except ValueError: continue
        i=bisect.bisect_left(ups,e['up']); c=[u for u in (ups[i] if i<len(ups) else None, ups[i-1] if i>0 else None) if u is not None]
        if not c: continue
        rowsd=snaps[min(c,key=lambda u:abs(u-e['up']))]
        calls=[rowsd[j] for j in range(128) if nd>>j&1 and j in rowsd]
        tgt=[CALL2NODE[c_] for c_ in calls if c_ in CALL2NODE and CALL2NODE[c_]!=a]
        if not tgt: continue
        t0=e['t']
        fate='sent' if any(0<=x-t0<900 for x in tx_t.get(e['id'],[])) else 'cancel' if e['id'] in canc else 'supersede' if e['id'] in sup else 'stale' if e['id'] in stale else 'other'
        for nn in tgt:
            if (a,nn) not in good: continue
            if not (rng[nn][0]+300<=t0<=rng[nn][1]-1800): continue
            got=any(-120<=t-t0<=1800 for t in heard[nn].get(e['id'],[]))
            tot[(fate,got)]+=1
            rows.append((a,nn,fate,got))
byfate=defaultdict(lambda:[0,0])
for (f,g),v in tot.items(): byfate[f][0 if g else 1]+=v
out={f:{'neighbour_got_frame':v[0],'neighbour_did_not':v[1],'got_pct':round(100*v[0]/(v[0]+v[1]),1)} for f,v in byfate.items()}
print(out)
# per pair
pp=defaultdict(Counter)
for a,nn,f,g in rows: pp[f'{a}->{nn}'][f'{f}:{"got" if g else "miss"}']+=1
for k,v in sorted(pp.items()): print(k,dict(v))
json.dump({'by_fate':out,'by_pair':{k:dict(v) for k,v in pp.items()},'good_links':{f'{a}->{b}':p for (a,b),p in good.items()}},open(OUTD/'work'/'loss.json','w'),indent=1)
