"""Attribute cluster TX + airtime to the own {ping} stream of the node with most own pings (WJ in 2026-11-01) (pings + their ACKs + relays)."""
import json, math; 
from common import NODES, OUTD, load
from collections import Counter, defaultdict
def air(plen, sf=11,bw=250e3,cr=2):
    ts=(2**sf)/bw; tpre=(8+4.25)*ts
    npay=8+max(math.ceil((8*plen-4*sf+28+16)/(4*(sf-2)))*(cr+4),0) if ts>0.016 else 0
    de=1 if ts>0.016 else 0
    npay=8+max(math.ceil((8*plen-4*sf+28+16)/(4*(sf-2*de)))*(cr+4),0)
    return (tpre+npay*ts)*1000
ev={n:load(n) for n in NODES}
# ping tx times by id
PINGER=max(NODES,key=lambda m: sum(1 for e in ev[m] if e['k']=='tx' and e['src']=='o' and e['typ']==':'))
pings=defaultdict(list)
for e in ev[PINGER]:
    if e['k']=='tx' and e['src']=='o' and e['typ']==':': pings[e['id']].append(e['t'])
def is_ping_id(mid,t):
    return any(-5<=t-x<=1800 for x in pings.get(mid,[]))
# ack id -> acked id (+time) over all nodes
ackmap=defaultdict(list)
for n in NODES:
    for e in ev[n]:
        if e['k']=='ack':
            ackmap[e['id']].append((e['t'],e['ex'].split()[0].lstrip('x')))
def ack_is_ping(aid,t):
    for (tt,ex) in ackmap.get(aid,[]):
        if abs(tt-t)<1800 and is_ping_id(ex,tt): return True
    return False
tot=Counter(); ping_c=Counter(); tair=0.0; pair=0.0
by_node=defaultdict(lambda: Counter())
for n in NODES:
    for e in ev[n]:
        if e['k']!='tx': continue
        a=air(e['len'])
        tot['tx']+=1; tair+=a
        by_node[n]['tx']+=1; by_node[n]['air']+=a
        hit=False
        if e['typ']==':' and is_ping_id(e['id'],e['t']): hit=True; kind='ping_orig_or_relay'
        elif e['typ']=='A' and ack_is_ping(e['id'],e['t']): hit=True; kind='ack_of_ping_relay'
        if hit:
            ping_c[kind]+=1; pair+=a
            by_node[n]['ping_tx']+=1; by_node[n]['ping_air']+=a
print('total tx',tot['tx'],'air s',round(tair/1000))
print('ping-attributed',dict(ping_c),'air s',round(pair/1000),'share tx %.1f%% air %.1f%%'%(100*sum(ping_c.values())/tot['tx'],100*pair/tair))
out={'total_tx':tot['tx'],'total_air_s':round(tair/1000),'ping_tx':dict(ping_c),'ping_air_s':round(pair/1000),'by_node':{n:{'tx':v['tx'],'air_s':round(v['air']/1000),'ping_tx':v['ping_tx'],'ping_air_s':round(v['ping_air']/1000)} for n,v in by_node.items()}}
for n,v in out['by_node'].items(): print(n,v, 'ping share tx %.1f%% air %.1f%%'%(100*v['ping_tx']/v['tx'],100*v['ping_air_s']/v['air_s']))
json.dump(out,open(OUTD/'work'/'pingshare.json','w'),indent=1)
