"""Q6.6 gateway effect: server->RF injection (GWI / TX src=g) across the five OE3 gateways, and GWU upload multiplicity."""
import json, math; 
from common import CALL, NODES, OUTD, load, median
from collections import Counter, defaultdict
ev={n:load(n) for n in NODES}
def air(plen, sf=11,bw=250e3,cr=2):
    ts=(2**sf)/bw; tpre=(8+4.25)*ts
    npay=8+max(math.ceil((8*plen-4*sf+28+16)/(4*sf))*(cr+4),0)
    return (tpre+npay*ts)*1000
# payload by id (from any rx copy) to classify CET vs other
payload={}
for n in NODES:
    for e in ev[n]:
        if e['k']=='rx' and e['typ']==':': payload.setdefault(e['id'],e['pl'])
gwi=defaultdict(list); txg=defaultdict(list); gwu=defaultdict(list); need_g={}
for n in NODES:
    for e in ev[n]:
        if e['k']=='gwi': gwi[e['id']].append((e['t'],n,e['hop'],e['from']))
        elif e['k']=='tx' and e['src']=='g': txg[e['id']].append((e['t'],n,e['wait'],e['len'],e['cad']))
        elif e['k']=='gwu': gwu[e['id']].append((e['t'],n))
def clusters(d):
    out=[]
    for mid,l in d.items():
        l=sorted(l); cur=[l[0]]
        for r in l[1:]:
            if r[0]-cur[-1][0]>1800: out.append((mid,cur)); cur=[r]
            else: cur.append(r)
        out.append((mid,cur))
    return out
cl=clusters(txg)
dist=Counter(); dist_cet=Counter(); dist_other=Counter(); spread=[]; air_tot=0; air_first=0; frm=Counter()
frm_hops=Counter()
for mid,c in cl:
    nodes=sorted({x[1] for x in c}); k=len(nodes)
    cet=payload.get(mid,'').startswith('{CET}')
    dist[k]+=1
    (dist_cet if cet else dist_other)[k]+=1
    ts=sorted(x[0] for x in c)
    if k>1: spread.append(ts[-1]-ts[0])
    a=sorted(air(x[3]) for x in c); air_tot+=sum(a); air_first+=a[0]
for mid,c in clusters(gwi):
    for x in c: frm[x[3]]+=1
    for x in c: frm_hops[x[2]]+=1
# GWI never radiated (cancelled/dropped) per node
gwi_n=Counter(); gwi_rad=Counter()
for mid,l in gwi.items():
    for (t,n,h,f) in l:
        gwi_n[n]+=1
        if any(0<=x[0]-t<600 and x[1]==n for x in txg.get(mid,[])): gwi_rad[n]+=1
# GWU multiplicity
ucl=clusters(gwu)
udist=Counter(len({x[1] for x in c}) for mid,c in ucl)
# who radiated vs. whether nodes hear each other
res={'gwi_total':sum(gwi_n.values()),'gwi_by_node':dict(gwi_n),'gwi_radiated_by_node':dict(gwi_rad),
 'frames_radiated_k_nodes':dict(sorted(dist.items())),'cet_radiated_k_nodes':dict(sorted(dist_cet.items())),'other_text_radiated_k_nodes':dict(sorted(dist_other.items())),
 'median_spread_s_between_first_and_last_radiator':median(spread),'p90_spread_s':sorted(spread)[int(.9*len(spread))] if spread else None,
 'airtime_s_all_gateway_tx':round(air_tot/1000,1),'airtime_s_if_one_gateway_only':round(air_first/1000,1),
 'gwi_top_origin':frm.most_common(8),'gwi_hop_hist':dict(sorted(frm_hops.items())),
 'gwu_nodes_per_id':dict(sorted(udist.items())),'gwu_total':sum(len(c) for _,c in ucl)}
print(json.dumps(res,indent=1))
json.dump(res,open(OUTD/'work'/'gateway.json','w'),indent=1)

# --- cancel opportunity for gateway-injected slots: later radiator heard an earlier radiator's copy while its own slot was queued?
rx_by={n:defaultdict(list) for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e['k']=='rx': rx_by[n][e['id']].append(e)
opp=Counter(); case_c=Counter()
need_by={n:{} for n in NODES}
for n in NODES:
    for e in ev[n]:
        if e['k']=='need': need_by[n].setdefault(e['id'],[]).append(e)
for mid,c in cl:
    firsts={}
    for x in sorted(c): firsts.setdefault(x[1],x)
    seq=sorted(firsts.values())
    for i in range(1,len(seq)):
        t,n,w,l,cad=seq[i]
        tq=t-w/1000.0           # enqueue time = tx time - wait
        earlier={s[1] for s in seq[:i]}
        heard=[r for r in rx_by[n].get(mid,[]) if tq<r['t']<t and r['path'] and r['path'][-1]!=CALL[n]]
        k='heard_earlier_gateway_copy_while_queued' if heard else 'nothing_heard'
        opp[k]+=1
        nd=need_by[n].get(mid)
        case_c[(k, nd[0]['case'] if nd else '?')]+=1
print('later radiators (n-th>=2):',dict(opp)); print(dict(case_c))
res['later_radiator_cancel_opportunity']=dict(opp)
res['later_radiator_by_need_case']={f'{k[0]}|{k[1]}':v for k,v in case_c.items()}
json.dump(res,open(OUTD/'work'/'gateway.json','w'),indent=1)
