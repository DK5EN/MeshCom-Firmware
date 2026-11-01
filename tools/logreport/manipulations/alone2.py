"""Are the 'alone' (sole-provider) dependents provably hearing us?  per node: class of alone rows."""
import json, bisect; 
from common import CALL, NODES, OUTD, STANDALONE, load
from collections import Counter
res={}
for n in NODES+STANDALONE:
    ev=load(n)
    me=CALL[n]
    direct=Counter(); heardme=Counter()
    for e in ev:
        if e['k']=='me': direct[e['from']]+=1
        elif e['k']=='edge' and e['from']==me: heardme[e['to']]+=1
    snaps={}
    for e in ev:
        if e['k']=='row': snaps.setdefault(e['up'],{})[e['idx']]=(e['call'],int(e['age']),int(e['hearers']),e['verdict'])
    ups=sorted(snaps)
    cls=Counter(); cls_rows=Counter(); excl=Counter(); tot=0; needs=0; allasym=0; anyproven=0
    rowage=[]
    for e in ev:
        if e['k']!='need': continue
        try: a=int(e['alone'],16)
        except ValueError: continue
        i=bisect.bisect_left(ups,e['up']); c=[u for u in (ups[i] if i<len(ups) else None, ups[i-1] if i>0 else None) if u is not None]
        if not c: continue
        rows=snaps[min(c,key=lambda u:abs(u-e['up']))]
        am=[rows[i][0] for i in range(128) if a>>i&1 and i in rows]
        if not am: continue
        needs+=1
        proven=0
        for call in am:
            d=direct.get(call,0)>0; h=heardme.get(call,0)>0
            k='both' if d and h else 'direct_only' if d else 'heardme_only' if h else 'neither'
            cls[k]+=1
            if h: proven+=1
        if proven==0: allasym+=1
    res[n]={'need_with_alone':needs,'alone_row_classes':dict(cls),'needs_where_no_alone_row_provably_hears_us':allasym,
            'direct_calls':len(direct),'heardme_calls':len(heardme)}
    print(n,res[n])
json.dump(res,open(OUTD/'work'/'alone2.json','w'),indent=1)
