"""Fate of own originated beacons (auto_pos, user_wx): written to ring -> sent / superseded / stale / other."""
import json; 
from common import NODES, OUTD, STANDALONE, load, median
from collections import Counter
res={}
for n in NODES+STANDALONE:
    ev=load(n)
    tx_ids=set(e['id'] for e in ev if e['k']=='tx' and e['src']=='o')
    sup=Counter(); stale={}; 
    for e in ev:
        if e['k']=='supersede': sup[e['old']]+=1
        if e['k']=='dropstale': stale[e['id']]=e
    c=Counter(); stale_prio=Counter(); stale_age=[]
    for e in ev:
        if e['k']=='ringwrite' and e['src'] in ('auto_pos','user_wx'):
            t=e['typ']
            kind='HEY' if t=='40' else 'POS' if t=='21' else t
            key=f"{e['src']}:{kind}"
            if e['id'] in tx_ids: c[key+':sent']+=1
            elif sup.get(e['id']): c[key+':superseded']+=1
            elif e['id'] in stale: c[key+':stale_ttl']+=1; stale_prio[stale[e['id']]['prio']]+=1; stale_age.append(stale[e['id']]['age'])
            else: c[key+':other']+=1
    res[n]=dict(c)
    print(n,dict(c),'stale age median',median(stale_age) if stale_age else None)
json.dump(res,open(OUTD/'work'/'ownfate.json','w'),indent=1)
