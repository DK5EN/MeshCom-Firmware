import json; 
from common import NODES, OUTD, STANDALONE, load
res={}
for n in NODES+STANDALONE:
    ev=load(n)
    st=[e for e in ev if e['k']=='stat']
    drop=[0]*5; rx=tx=0; util=[]; newid=dup=err=txn=0; ringmax=[]
    for e in st:
        try:
            d=[int(x) for x in e['drop'].split('/')]
            for i in range(5): drop[i]+=d[i]
            rx+=int(e['rx']); tx+=int(e['tx']); util.append(int(e['util'])); newid+=int(e['newid']); dup+=int(e['dup']); err+=int(e['err']); txn+=int(e['txn'])
            ringmax.append(int(e['ringmax'].split('/')[0]))
        except Exception: pass
    res[n]={'stat_windows':len(st),'drop_prio1..5':drop,'rx_air_s':round(rx/1000),'tx_air_s':round(tx/1000),'window_s':len(st)*300,'util_mean':round(sum(util)/len(util),1) if util else None,'util_max':max(util) if util else None,'newid':newid,'dup':dup,'dup_per_new':round(dup/newid,2) if newid else None,'rx_err':err,'txn':txn,'ringmax_max':max(ringmax) if ringmax else None,'ringmax_ge15_windows':sum(1 for r in ringmax if r>=15)}
    res[n]['tx_duty_pct']=round(100*tx/1000/(len(st)*300),2) if st else None
    res[n]['rx_duty_pct']=round(100*rx/1000/(len(st)*300),2) if st else None
    print(n,res[n])
json.dump(res,open(OUTD/'work'/'statagg.json','w'),indent=1)
