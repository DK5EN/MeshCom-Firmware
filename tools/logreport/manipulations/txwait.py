import json; 
from common import NODES, OUTD, STANDALONE, load, median
from collections import Counter, defaultdict
res={}
for n in NODES+STANDALONE:
    ev=load(n)
    tx=[e for e in ev if e['k']=='tx']
    cad=Counter(min(e['cad'],10) for e in tx)
    w=defaultdict(list)
    for e in tx: w[(e['src'],e['typ'],e['prio'])].append(e['wait']/1000)
    tab={f'{k[0]}/{k[1]}/p{k[2]}':{'n':len(v),'wait_med_s':round(median(v),1),'wait_p90_s':round(sorted(v)[int(.9*len(v))],1)} for k,v in w.items() if len(v)>=10}
    qs=[e['q'] for e in tx]
    res[n]={'cad_attempts_hist':dict(sorted(cad.items())),'wait':tab,'q_after_take_mean':round(sum(qs)/len(qs),2),'q_max':max(qs)}
    print(n,'cad hist',dict(sorted(cad.items())))
    for k,v in sorted(tab.items()): print('   ',k,v)
json.dump(res,open(OUTD/'work'/'txwait.json','w'),indent=1)
