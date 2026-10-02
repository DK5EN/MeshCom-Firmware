import json, statistics
d=json.load(open('echo.json'))
order=d['order']; echo=d['echo']
import os
N=set(os.environ.get('NSET','DL2JA-2,DL2UD-1').split(','))
def rate(pred, win=None):
    hit=[]
    for x in order:
        ok=any(pred(p) and (win is None or dt<=win) for dt,p,h in echo.get(x,[]))
        hit.append(ok)
    n=sum(hit); miss=[not h for h in hit]
    pairs=sum(1 for i in range(len(miss)-1) if miss[i] and miss[i+1])
    # longest run of misses
    run=best=0
    for m in miss:
        run=run+1 if m else 0; best=max(best,run)
    return n, pairs, best
preds={
 'any echo (paper base)': lambda p: True,
 'named anywhere in path': lambda p: bool(N & set(p[1:])),
 'named node is last hop (I heard a named TX)': lambda p: p[-1] in N,
 'path after me only named nodes (survives via)': lambda p: set(p[1:])<=N,
 'DL2JA-2 anywhere': lambda p: 'DL2JA-2' in p[1:],
 'DL2UD-1 anywhere': lambda p: 'DL2UD-1' in p[1:],
}
L=len(order)
print('frames',L,'pairs',L-1)
for k,f in preds.items():
    for w in (None,300):
        n,pairs,best=rate(f,w)
        p=n/L; 
        print(f'{k:48s} win={str(w):4s} hits {n:2d}/{L} = {100*p:4.1f}%  miss^2 indep {100*(1-p)**2:4.1f}%  observed miss-pairs {pairs:2d}/{L-1} = {100*pairs/(L-1):4.1f}%  longest miss run {best}')
# DL2JA-2 delays
dl=[]; lines=0
for x in order:
    for dt,p,h in echo.get(x,[]):
        if 'DL2JA-2' in p[1:]:
            lines+=1
    ds=[dt for dt,p,h in echo.get(x,[]) if 'DL2JA-2' in p[1:]]
    if ds: dl.append(min(ds))
print('DL2JA-2 lines',lines,'msgs',len(dl),'median first appearance',statistics.median(dl),'>300:',sorted(int(v) for v in dl if v>300))
own_tx=[dt for x in order for dt,p,h in echo.get(x,[]) if p[-1]=='DL2JA-2']
print('DL2JA-2 own relay lines (last hop) n',len(own_tx),'median',statistics.median(own_tx),'sorted',sorted(int(v) for v in own_tx))
fh=[dt for x in order for dt,p,h in echo.get(x,[]) if p[-1]=='DL2JA-2' and p[1]=='DL2JA-2']
print('DL2JA-2 first-hand own relay n',len(fh),'median',statistics.median(fh))
# which msgs have DL2JA-2 in path but not as last hop
for x in order:
    for dt,p,h in echo.get(x,[]):
        if 'DL2JA-2' in p[1:] and p[-1]!='DL2JA-2': print('  DL2JA-2 not last:',x,int(dt),','.join(p))
# echo lines per msg count of DL2JA-2 transmissions
from collections import Counter
c=Counter(x for x in order for dt,p,h in echo.get(x,[]) if p[-1]=='DL2JA-2')
print('msgs with >1 DL2JA-2 TX line:',{k:v for k,v in c.items() if v>1})
# time between own POS
import datetime
own=[datetime.datetime.fromisoformat(d['own'][x]) for x in order]
gaps=[(own[i+1]-own[i]).total_seconds()/60 for i in range(len(own)-1)]
print('own POS interval min: median',statistics.median(gaps),'min',min(gaps),'max',max(gaps))
