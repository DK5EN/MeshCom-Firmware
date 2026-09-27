import re, collections, datetime
ME="DK5EN-98"
rx=re.compile(r'^(\S+ \S+)\s.*MH-LoRa \d+ (\S) x[0-9A-F]+ H\d+ S\d T\d M\d+ ([^>]+)>')
minall=collections.defaultdict(lambda:99); hzhops=collections.defaultdict(list)
for line in open('combined_window.log',errors='replace'):
    m=rx.search(line)
    if not m: continue
    ts=datetime.datetime.strptime(m.group(1)[:19],'%Y-%m-%d %H:%M:%S')
    t,tok=m.group(2),m.group(3).split(',')
    s=tok[0]
    if s==ME: continue
    minall[s]=min(minall[s],len(tok))
    if t in '!@' and len(tok)>=3 and tok[-2]!=ME: hzhops[s].append((ts,len(tok),tok[-2]))
far=[s for s in hzhops if minall[s]>=3]
multi=[s for s in far if len(set(h for _,h,_ in hzhops[s]))>1]
multient=[s for s in far if len(set(e for _,_,e in hzhops[s]))>1]
print("far senders (POS/HEY, entry!=me):",len(far),"seen at >1 hop count:",len(multi),"with >1 entry token:",len(multient))
# stale-min: min hop last seen more than 12h before the entry's last sighting
stale=0; ex=[]
for s in far:
    ev=hzhops[s]; mn=min(h for _,h,_ in ev)
    last_min_seen=max(ts for ts,h,_ in ev if h==mn); last=max(ts for ts,_,_ in ev)
    if (last-last_min_seen).total_seconds()>12*3600: stale+=1; ex.append((s,mn,str(last_min_seen)[5:16],str(last)[5:16]))
print("far senders whose min-hop sighting is >12h older than their last sighting:",stale); print(ex[:8])
diff=[]
for s in far:
    per=collections.defaultdict(lambda:99)
    for _,h,e in hzhops[s]: per[e]=min(per[e],h)
    if len(set(per.values()))>1: diff.append((s,dict(per)))
print("far senders whose entry tokens have different min hop counts:",len(diff)); print(diff[:5])
