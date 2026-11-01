"""{CET} time-signal radiation per node: tx age, gateway rx age, drops."""
import json; 
from common import NODES, NORM, OUTD, STANDALONE
from collections import Counter
res={}
for n in NODES+STANDALONE:
    tx=[];gw=[];rx=[];drop=[];ids=Counter()
    with open(NORM/f'{n}.log','rb') as fh:
        for raw in fh:
            i=raw.find(b'[CET];')
            if i<0: continue
            f=raw[i:].decode('latin-1').strip().split(';')
            d={f[j]:f[j+1] for j in range(2,len(f)-1,2)}
            k=f[1]
            try:
                if k=='tx': tx.append((int(d['age_s']),int(d['wait_ms']),int(d['prio'])))
                elif k=='gw_rx': gw.append((int(d['age_s']),int(d['hop'])))
                elif k=='rx': rx.append((int(d['age_s']),int(d['hop']),int(d.get('path',0))))
                elif k=='drop': drop.append((d.get('where'),int(d['age_s'])))
            except Exception: pass
    def q(v,p): 
        v=sorted(v); return v[int(p*(len(v)-1))] if v else None
    res[n]={'cet_tx':len(tx),'tx_age_median_s':q([a for a,_,_ in tx],.5),'tx_age_p90_s':q([a for a,_,_ in tx],.9),'tx_age_max_s':max([a for a,_,_ in tx],default=None),
            'tx_age_gt60':sum(1 for a,_,_ in tx if a>60),'tx_wait_median_ms':q([w for _,w,_ in tx],.5),
            'gw_rx':len(gw),'gw_rx_neg_age':sum(1 for a,_ in gw if a<0),'gw_rx_age_median':q([a for a,_ in gw],.5),
            'lora_rx_cet':len(rx),'lora_rx_age_median':q([a for a,_,_ in rx],.5),'lora_rx_age_gt150':sum(1 for a,_,_ in rx if a>150),'drops':drop}
    print(n,res[n])
json.dump(res,open(OUTD/'work'/'cet.json','w'),indent=1)
