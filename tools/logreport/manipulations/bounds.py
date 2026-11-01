"""Airtime upper bound: relay TX that were sent although a foreign copy was heard while queued (cancel opportunity), per node/type."""
import json, math; 
from common import CALL, NODES, OUTD, load
from collections import Counter, defaultdict
def air(plen, sf=11,bw=250e3,cr=2):
    ts=(2**sf)/bw; tpre=(8+4.25)*ts
    npay=8+max(math.ceil((8*plen-4*sf+28+16)/(4*sf))*(cr+4),0)
    return (tpre+npay*ts)*1000
res={}; tot_all=0
for n in NODES:
    ev=load(n); me=CALL[n]
    rw=defaultdict(list); rxs=defaultdict(list); acks=defaultdict(list)
    for e in ev:
        if e['k']=='ringwrite' and e['src'] in ('rx_relay','rx_ack_fwd'): rw[e['id']].append(e['t'])
        elif e['k']=='rx': rxs[e['id']].append(e)
        elif e['k']=='ack': acks[e['id']].append(e)
    a=Counter(); cnt=Counter(); allair=0
    for e in ev:
        if e['k']!='tx': continue
        al=air(e['len']); allair+=al
        if e['src']!='r': continue
        q=[t for t in rw.get(e['id'],[]) if t<=e['t']]
        if not q: continue
        tq=max(q)
        if e['typ']=='A': d=[x for x in acks.get(e['id'],[]) if tq<x['t']<e['t'] and x['dup']]
        else: d=[x for x in rxs.get(e['id'],[]) if tq<x['t']<e['t'] and x['dup'] and len(x['path'])>=2 and x['path'][-1]!=me]
        if d: a[e['typ']]+=al; cnt[e['typ']]+=1
    res[n]={'tx_air_s':round(allair/1000),'opportunity_air_s_by_type':{k:round(v/1000) for k,v in a.items()},'opportunity_tx_by_type':dict(cnt),'opportunity_air_s_total':round(sum(a.values())/1000)}
    res[n]['opportunity_share_of_node_tx_air_pct']=round(100*sum(a.values())/allair,1)
    print(n,res[n])
tot=sum(v['opportunity_air_s_total'] for v in res.values()); allt=sum(v['tx_air_s'] for v in res.values())
print('cluster',tot,allt,round(100*tot/allt,1))
res['_cluster']={'opportunity_air_s':tot,'tx_air_s':allt,'share_pct':round(100*tot/allt,1)}
json.dump(res,open(OUTD/'work'/'bounds.json','w'),indent=1)
