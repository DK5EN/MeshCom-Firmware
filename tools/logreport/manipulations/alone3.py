"""Persistent 'alone' calls per node vs. direct hearing at the other OE3 nodes (observation-bias test)."""
import json, bisect; 
from common import NODES, OUTD, load
from collections import Counter
direct={}; ev={}
for n in NODES:
    ev[n]=load(n)
    c=Counter()
    for e in ev[n]:
        if e['k']=='me': c[e['from']]+=1
    direct[n]=c
res={}
for n in NODES:
    snaps={}
    for e in ev[n]:
        if e['k']=='row': snaps.setdefault(e['up'],{})[e['idx']]=e['call']
    ups=sorted(snaps)
    cnt=Counter(); N=0
    for e in ev[n]:
        if e['k']!='need': continue
        try: a=int(e['alone'],16)
        except ValueError: continue
        i=bisect.bisect_left(ups,e['up']); c=[u for u in (ups[i] if i<len(ups) else None, ups[i-1] if i>0 else None) if u is not None]
        if not c: continue
        rows=snaps[min(c,key=lambda u:abs(u-e['up']))]
        N+=1
        for i in range(128):
            if a>>i&1 and i in rows: cnt[rows[i]]+=1
    pers=[(c,k) for c,k in cnt.items() if k>=0.5*N]
    out=[]
    n_alt=0
    for c,k in sorted(pers,key=lambda x:-x[1]):
        others=[m for m in NODES if m!=n and direct[m].get(c,0)>=3]
        out.append((c,k,others))
        if others: n_alt+=1
    res[n]={'need_lines':N,'persistent_alone_calls':len(pers),'persistent_alone_with_other_oe3_direct_hearer':n_alt,
            'examples':[(c,k,o) for c,k,o in out[:10]]}
    print(n,N,len(pers),n_alt)
    for c,k,o in out[:8]: print('   ',c,k,o)
json.dump(res,open(OUTD/'work'/'alone3.json','w'),indent=1)
