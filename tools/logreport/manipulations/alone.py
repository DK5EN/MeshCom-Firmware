"""Decode NEED alone/need masks to callsigns via the nearest [NBR]|ROW snapshot (same node)."""
import json; 
from common import NODES, OUTD, STANDALONE, load
from collections import Counter
import bisect
res={}
for n in NODES+STANDALONE:
    ev=load(n)
    snaps={}   # up -> {idx: (call, age, hearers, verdict, meshneed)}
    for e in ev:
        if e['k']=='row':
            snaps.setdefault(e['up'],{})[e['idx']]=(e['call'],int(e['age']),int(e['hearers']),e['verdict'],e['meshneed'])
    ups=sorted(snaps)
    def snap_for(up):
        i=bisect.bisect_left(ups,up)
        c=[]
        if i<len(ups): c.append(ups[i])
        if i>0: c.append(ups[i-1])
        if not c: return None
        b=min(c,key=lambda u:abs(u-up))
        return b, snaps[b]
    alone_c=Counter(); need_c=Counter(); sizes=Counter(); nsolo=Counter()
    onlyone=0; tot=0; nn=0
    ages=[]
    for e in ev:
        if e['k']!='need': continue
        tot+=1
        sp=snap_for(e['up'])
        if not sp: continue
        _,rows=sp
        try:
            a=int(e['alone'],16); nd=int(e['need'],16)
        except ValueError:
            continue
        am=[i for i in range(128) if a>>i&1]
        nm=[i for i in range(128) if nd>>i&1]
        sizes[len(am)]+=1
        for i in am:
            if i in rows: alone_c[rows[i][0]]+=1
        for i in nm:
            if i in rows: need_c[rows[i][0]]+=1
        if len(am)==1: onlyone+=1
        if am:
            nn+=1
    res[n]={'need_lines':tot,'alone_size_hist':dict(sorted(sizes.items())),'top_alone_calls':alone_c.most_common(12),'top_need_calls':need_c.most_common(8),'alone_exactly_one':onlyone}
    print('==',n,tot); print(' alone-size hist',dict(sorted(sizes.items()))); print(' top alone',alone_c.most_common(12)); print(' top need',need_c.most_common(8))
json.dump(res,open(OUTD/'work'/'alone.json','w'),indent=1)
