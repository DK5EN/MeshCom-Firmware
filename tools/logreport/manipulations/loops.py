"""Q6.5 feedback / build-up checks."""
import json; 
from common import CALL, NODES, OUTD, STANDALONE, load, median
from collections import Counter, defaultdict
res={}
for n in NODES+STANDALONE:
    ev=load(n); me=CALL[n]
    # time axis for db0ais: node clock seconds (t may be None)
    last=None
    for e in ev:
        if e['t'] is None: e['t']=last
        else: last=e['t']
    new=defaultdict(list)
    for e in ev:
        if e['k']=='rx' and not e['dup'] and e['t'] is not None: new[e['id']].append(e)
    reacc=[]; 
    for mid,l in new.items():
        l.sort(key=lambda x:x['t'])
        for a,b in zip(l,l[1:]):
            gap=b['t']-a['t']
            if gap<4*3600: reacc.append((mid,gap,a['own'],b['own'],a['typ']))
    # exclude own-echo first accept (own originated frames are not in the ring)
    own_reacc=[r for r in reacc if r[2] or r[3]]
    foreign=[r for r in reacc if not (r[2] or r[3])]
    gaps=sorted(r[1] for r in foreign)
    # hop / path
    hops=Counter(e['hop'] for e in ev if e['k']=='rx')
    plen=Counter(len(e['path']) for e in ev if e['k']=='rx')
    # TX twice relay same id
    txr=defaultdict(list)
    for e in ev:
        if e['k']=='tx' and e['src']=='r' and e['t'] is not None: txr[e['id']].append(e['t'])
    twice=[(mid,sorted(l)) for mid,l in txr.items() if len(l)>=2]
    twice_close=[(m,l) for m,l in twice if any(b-a<4*3600 for a,b in zip(l,l[1:]))]
    gaps_tx=sorted(b-a for m,l in twice for a,b in zip(l,l[1:]) if b-a<4*3600)
    # dedup ring turnover
    adds=[e for e in ev if e['k']=='dedupadd']
    ring=adds[0]['size'] if adds else None
    span=(ev[-1]['t']-ev[0]['t']) if ev[0]['t'] is not None else None
    rate=len(adds)/(span/60) if span else None
    res[n]={'accepted_again_<4h_total':len(reacc),'own_origin_echo_first_accept':len(own_reacc),'foreign_reaccepted':len(foreign),
            'foreign_reaccept_gap_s_median':median(gaps) if gaps else None,'foreign_reaccept_gap_s_p90':gaps[int(.9*len(gaps))] if gaps else None,
            'foreign_reaccept_gap_gt_10min':sum(1 for g in gaps if g>600),'foreign_reaccept_gap_gt_30min':sum(1 for g in gaps if g>1800),
            'hop_ge13':sum(v for k,v in hops.items() if k>=13),'hop_hist':dict(sorted(hops.items())),'path_len_hist':dict(sorted(plen.items())),
            'relay_tx_same_id_twice_total':len(twice),'relay_tx_twice_gap_s_min':gaps_tx[:5],'relay_tx_twice_gap_s_median':median(gaps_tx) if gaps_tx else None,
            'dedup_ring_slots':ring,'dedup_adds_per_min':round(rate,2) if rate else None,'dedup_window_min_est':round(ring/rate,1) if rate and ring else None}
    print(n,json.dumps(res[n]))
json.dump(res,open(OUTD/'work'/'loops.json','w'),indent=1)
