"""Per ordered pair A->B: fraction of A's transmissions (non-ACK) that B logged as received with last hop A within 3 s."""
import json; 
from common import CALL, NODES, OUTD, load
from collections import defaultdict
ev={n:load(n) for n in NODES}
rx={}
for b in NODES:
    d=defaultdict(list)
    for e in ev[b]:
        if e['k']=='rx': d[(e['id'],e['path'][-1] if e['path'] else '')].append(e['t'])
    rx[b]=d
ackrx={b:defaultdict(list) for b in NODES}
for b in NODES:
    for e in ev[b]:
        if e['k']=='ack': ackrx[b][e['id']].append(e['t'])
res={}
rssi={}
for a in NODES:
    txs=[e for e in ev[a] if e['k']=='tx']
    for b in NODES:
        if a==b: continue
        n=0;h=0;na=0;ha=0
        for e in txs:
            if e['typ']=='A':
                na+=1
                if any(0<=t-e['t']<=3 for t in ackrx[b].get(e['id'],[])): ha+=1
            else:
                n+=1
                if any(0<=t-e['t']<=3 for t in rx[b].get((e['id'],CALL[a]),[])): h+=1
        res[f'{a}->{b}']={'tx_nonack':n,'heard_nonack':h,'p_nonack':round(h/n,3) if n else None,'tx_ack':na,'heard_ack':ha,'p_ack':round(ha/na,3) if na else None}
        print(a,'->',b,res[f'{a}->{b}'])
json.dump(res,open(OUTD/'work'/'linkp.json','w'),indent=1)
