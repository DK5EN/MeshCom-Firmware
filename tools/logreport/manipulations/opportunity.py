"""Per relay TX: was a foreign copy of the same frame heard while the relay sat in the queue?  (cancel opportunity)"""
import json; 
from common import CALL, NODES, OUTD, STANDALONE, load, median
from collections import Counter, defaultdict
res={}
for n in NODES+STANDALONE:
    ev=load(n); me=CALL[n]
    rw=defaultdict(list); rxs=defaultdict(list); acks=defaultdict(list); needs={}; refs=defaultdict(list); canc=set()
    for e in ev:
        k=e['k']
        if k=='ringwrite' and e['src'] in ('rx_relay','rx_ack_fwd'): rw[e['id']].append(e)
        elif k=='rx': rxs[e['id']].append(e)
        elif k=='ack': acks[e['id']].append(e)
        elif k=='need': needs.setdefault(e['id'],[]).append(e)
        elif k=='refuse': refs[e['id']].append(e)
        elif k=='cancel': canc.add(e['id'])
    c=Counter(); ct=defaultdict(Counter)
    waits=defaultdict(list)
    for e in ev:
        if e['k']!='tx' or e['src']!='r': continue
        mid=e['id']; typ=e['typ']
        if n in STANDALONE: 
            tq=None
        q=[w for w in rw.get(mid,[]) if w['t'] is not None and w['t']<=e['t']]
        if not q: ct[typ]['no_queue_record']+=1; continue
        tq=max(w['t'] for w in q)
        if typ=='A':
            dups=[a for a in acks.get(mid,[]) if tq<a['t']<e['t'] and a['dup']]
        else:
            dups=[a for a in rxs.get(mid,[]) if tq<a['t']<e['t'] and a['dup'] and a['path'] and a['path'][-1]!=me and len(a['path'])>=2]
        waits[typ].append(e['t']-tq)
        key='sent_with_foreign_copy_heard_while_queued' if dups else 'sent_no_copy_heard'
        ct[typ][key]+=1
        if dups and typ!='A':
            nd=needs.get(mid,[None])[0]
            ct[typ]['  of which case '+(nd['case'] if nd else '?')]+=1
    res[n]={t:dict(v) for t,v in ct.items()}
    res[n]['median_queue_wait_s']={t:round(median(v),1) for t,v in waits.items()}
    print(n,res[n])
json.dump(res,open(OUTD/'work'/'opportunity.json','w'),indent=1)
