"""Overhear-cancel at the node with CANCEL lines (OE3XHL in the 2026-11-01 capture): who, what, how strongly supported."""
import bisect
import json
import sys
from common import OUTD, cancel_node, load, median
from collections import Counter, defaultdict
n=cancel_node()  # the node with CANCEL lines
ev=load(n)
snaps={}
for e in ev:
    if e['k']=='row': snaps.setdefault(e['up'],{})[e['idx']]=e['call']
ups=sorted(snaps)
def rows_at(up):
    i=bisect.bisect_left(ups,up); c=[u for u in (ups[i] if i<len(ups) else None, ups[i-1] if i>0 else None) if u is not None]
    return snaps[min(c,key=lambda u:abs(u-up))]
# edge history: (from,to) -> list of (t,cnt,snr)
edges=defaultdict(list)
for e in ev:
    if e['k']=='edge': edges[(e['from'],e['to'])].append((e['t'],e['cnt'],e['snr']))
need_t={}  # id -> need event (time of queue)
for e in ev:
    if e['k']=='need': need_t.setdefault(e['id'],e)
rly_t={}
for e in ev:
    if e['k']=='rly' and e['q']=='tx': rly_t.setdefault(e['id'],e)
canc=[e for e in ev if e['k']=='cancel']
if not canc:
    json.dump({'cancels':0},open(OUTD/'work'/'cancel_eval.json','w'),indent=1)
    sys.exit(0)
byrel=Counter(c['relayer'] for c in canc); bytyp=Counter(c['typ'] for c in canc)
delay=[]; ninf=0; nrows=[]; cnts=[]; snrs=[]; weak=0; unobs=0; total_edges=0
inf_nonzero=0
rm_calls=Counter()
n_all_strong=0
for c in canc:
    rows=rows_at(c['up'])
    b=int(c['before'],16); inf=int(c['inf'],16) if c['inf'] else 0
    ids=[i for i in range(128) if b>>i&1]
    nrows.append(len(ids))
    if inf: inf_nonzero+=1
    q=rly_t.get(c['id'])
    if q: delay.append(c['t']-q['t'])
    strong=True
    for i in ids:
        call=rows.get(i)
        if not call: continue
        rm_calls[call]+=1
        if call==c['relayer']: continue
        total_edges+=1
        hist=[x for x in edges.get((c['relayer'],call),[]) if x[0]<=c['t']]
        if not hist:
            unobs+=1; strong=False; continue
        last=hist[-1]; cnts.append(last[1])
        if last[2] not in ('NA',): 
            try: snrs.append(int(last[2]))
            except Exception: pass
        if last[1]<=2: weak+=1; strong=False
    if strong and not inf: n_all_strong+=1
print('cancels',len(canc),'by relayer',byrel.most_common(8),'by typ',dict(bytyp))
print('queue->cancel delay median s',round(median(delay),1),'p90',sorted(delay)[int(.9*len(delay))] if delay else None)
print('cancels with SYM-inferred removed rows',inf_nonzero,'(%.1f%%)'%(100*inf_nonzero/len(canc)))
print('removed rows per cancel: median',median(nrows),'max',max(nrows))
print('(R->row) edge pairs',total_edges,'unobserved edge',unobs,'weak(cnt<=2)',weak,'median cnt',median(cnts),'median SNR',median(snrs))
print('cancels where every removed row has an observed edge with cnt>=3 and nothing inferred',n_all_strong)
print('top removed rows',rm_calls.most_common(8))
# per hour
import time
ph=Counter(time.strftime('%d %H',time.localtime(c['t'])) for c in canc)
json.dump({'cancels':len(canc),'by_relayer':dict(byrel),'by_type':dict(bytyp),'delay_median_s':median(delay),'inferred_nonzero':inf_nonzero,'removed_rows_median':median(nrows),
 'edge_pairs':total_edges,'unobserved':unobs,'weak_cnt_le2':weak,'median_edge_cnt':median(cnts),'median_snr':median(snrs),'all_strong_nothing_inferred':n_all_strong,'per_hour':dict(sorted(ph.items())),'top_removed_rows':rm_calls.most_common(10)},open(OUTD/'work'/'cancel_eval.json','w'),indent=1)

# --- confirmation: removed row C later seen in the path of a copy of the cancelled id (C demonstrably had the frame)
rxs=defaultdict(list)
for e in ev:
    if e['k']=='rx': rxs[e['id']].append(e)
conf=Counter(); per_cancel_all=0; per_cancel_some=0
for c in canc:
    rows=rows_at(c['up']); b=int(c['before'],16)
    ids=[i for i in range(128) if b>>i&1]
    calls=[rows[i] for i in ids if i in rows and rows[i]!=c['relayer']]
    if not calls: per_cancel_all+=1; continue
    toks=set()
    for r in rxs.get(c['id'],[]):
        if 0<=r['t']-c['t']<=1800:
            toks.update(r['path'])
    okc=[x for x in calls if x in toks]
    conf['rows_total']+=len(calls); conf['rows_confirmed_in_later_path']+=len(okc)
    if len(okc)==len(calls): per_cancel_all+=1
    elif okc: per_cancel_some+=1
print('removed rows (excluding relayer itself):',dict(conf))
print('cancels with all removed rows confirmed/relayer-only:',per_cancel_all,'some:',per_cancel_some,'none:',len(canc)-per_cancel_all-per_cancel_some)
