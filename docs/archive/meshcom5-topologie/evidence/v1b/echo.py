import re, sys, statistics
from datetime import datetime
from collections import defaultdict, Counter
ME='DK5EN-98'
T0=datetime(2026,9,23,0,12); T1=datetime(2026,9,24,8,46,59)
pos_re=re.compile(r'^(\S+ \S+)\s+\S+ NEW-POS \d+ ! x([0-9A-F]{8}) .*? '+ME+'>')
log_re=re.compile(r'^(\S+ \S+)\s+\S+ \[LOG\] \d+ (\S) x([0-9A-F]{8}) H(\d+) S\d T\d M\d+ (\S+?)>')
def ts(s): return datetime.strptime(s[:19],'%Y-%m-%d %H:%M:%S')
own={}  # msgid -> tx time
order=[]
lines=open(sys.argv[1],encoding='utf-8',errors='replace').read().splitlines()
for l in lines:
    m=pos_re.match(l)
    if m:
        t=ts(m.group(1))
        if T0<=t<=T1:
            own[m.group(2)]=t; order.append(m.group(2))
echo=defaultdict(list) # msgid -> list of (dt, path tokens, hop)
for l in lines:
    m=log_re.match(l)
    if not m: continue
    mid=m.group(3)
    if mid not in own: continue
    path=m.group(5).split(',')
    if path[0]!=ME: continue
    dt=(ts(m.group(1))-own[mid]).total_seconds()
    echo[mid].append((dt,path,int(m.group(4))))
print('own POS in window',len(order),'first',own[order[0]],'last',own[order[-1]])
print('with echo',sum(1 for x in order if echo[x]),'none',sum(1 for x in order if not echo[x]))
nbrs=['DK5EN-1','DB0ED-99','DL2UD-1','DL2JA-2','DL2JA-1']
for n in nbrs:
    f=s=both=0
    for x in order:
        fh=any(len(p)>1 and p[1]==n for _,p,_ in echo[x])
        sh=any(n in p[2:] for _,p,_ in echo[x])
        if fh: f+=1
        if sh and not fh: s+=1
        if fh and sh: both+=1
    print(f'{n:9s} first {f:3d} second-only {s:3d} both {both}')
# DL2JA-2 second-hand: who fed it
feed=Counter(); 
for x in order:
    for dt,p,h in echo[x]:
        if 'DL2JA-2' in p[2:]:
            i=p.index('DL2JA-2'); feed[p[i-1]]+=1
print('DL2JA-2 second-hand feeders (echo lines):',dict(feed))
feedmsg=Counter()
for x in order:
    fh=any(len(p)>1 and p[1]=='DL2JA-2' for _,p,_ in echo[x])
    if fh: continue
    fs=set(p[p.index('DL2JA-2')-1] for _,p,_ in echo[x] if 'DL2JA-2' in p[2:])
    for f in fs: feedmsg[f]+=1
print('DL2JA-2 second-only msgs by feeder:',dict(feedmsg))
# delays
for n in ['DL2JA-2','DL2UD-1','DB0ED-99','DK5EN-1']:
    d1=[dt for x in order for dt,p,h in echo[x] if p[1:2]==[n]]
    d2=[dt for x in order for dt,p,h in echo[x] if n in p[2:] and p[-1]==n]
    d1.sort(); d2.sort()
    def q(v,pc): return v[min(len(v)-1,int(pc*len(v)))] if v else None
    print(n,'first-hand relay heard as last hop? n',len(d1),'median',statistics.median(d1) if d1 else None,'p90',q(d1,.9),'| second-hand own relay n',len(d2),'median',statistics.median(d2) if d2 else None, 'max', max(d2) if d2 else None)
# DL2JA-2 relay delays: any echo line where DL2JA-2 is the last token (DL2JA-2 transmitted it) 
dj=[ (x,dt,p) for x in order for dt,p,h in echo[x] if p[-1]=='DL2JA-2']
print('lines where DL2JA-2 is last hop:',len(dj))
# DL2JA-2 appearance, earliest per msg
ea=[]
for x in order:
    ds=[dt for dt,p,h in echo[x] if 'DL2JA-2' in p[1:]]
    if ds: ea.append(min(ds))
ea.sort()
print('msgs with DL2JA-2 anywhere',len(ea),'delays>300',[int(d) for d in ea if d>300])
import json
json.dump({'order':order,'own':{k:str(v) for k,v in own.items()},'echo':{k:[(dt,p,h) for dt,p,h in v] for k,v in echo.items()}},open(sys.argv[2],'w'))
