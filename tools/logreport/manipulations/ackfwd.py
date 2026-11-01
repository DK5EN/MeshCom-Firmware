"""ACK forwarding decision reconstruction: new ACK with hop>0 -> forwarded (ringwrite rx_ack_fwd same id) or not; why not?"""
import json; 
from common import NODES, OUTD, STANDALONE, load
from collections import Counter
res={}
for n in NODES+STANDALONE:
    ev=load(n)
    seen={}      # acked msg id -> srv flag (latest add)
    fwd=set(e['id'] for e in ev if e['k']=='ringwrite' and e['src']=='rx_ack_fwd')
    cls=Counter()
    own=set(e['id'] for e in ev if e['k']=='tx' and e['src']=='o')
    for e in ev:
        if e['k']=='dedupadd':
            seen[e['id']]=e['srv']
        elif e['k']=='ack' and not e['dup']:
            hop=e['hop']&0x7f
            ex=e['ex']
            # ex is acked msg id in 'xXXXXXXXX' text form
            ex=ex.split()[0].lstrip('x') if ex else ex
            if hop==0: cls['hop0']+=1; continue
            fw=e['id'] in fwd
            s=seen.get(ex)
            key=('fwd' if fw else 'nofwd', 'acked_id_unknown' if s is None else f'acked_srv{s}')
            cls['|'.join(key)]+=1
    res[n]=dict(cls)
    print(n,dict(cls))
json.dump(res,open(OUTD/'work'/'ackfwd.json','w'),indent=1)
