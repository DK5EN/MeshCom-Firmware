import re, collections, datetime
ME="DK5EN-98"
rx=re.compile(r'^(\S+ \S+)\s.*MH-LoRa \d+ (\S) x[0-9A-F]+ H\d+ S\d T\d M\d+ ([^>]+)>')
ev=[]
for line in open('combined_window.log',errors='replace'):
    m=rx.search(line)
    if not m: continue
    ts=datetime.datetime.strptime(m.group(1)[:19],'%Y-%m-%d %H:%M:%S')
    ev.append((ts,m.group(2),m.group(3).split(',')))
for start in ['2026-09-23 00:12:00','2026-09-23 08:00:00','2026-09-23 20:01:00']:
    a=datetime.datetime.strptime(start,'%Y-%m-%d %H:%M:%S'); b=a+datetime.timedelta(hours=12)
    minhop=collections.defaultdict(lambda:99); hz=set()
    for ts,t,tok in ev:
        if not(a<=ts<b): continue
        s=tok[0]
        if s==ME: continue
        minhop[s]=min(minhop[s],len(tok))
        if t in '!@' and len(tok)>=3: hz.add(s)
    near=[s for s in hz if minhop[s]<=2]
    print(start,"12h: horizon senders",len(hz),"near(min<=2)",len(near),"of which direct",sum(1 for s in near if minhop[s]==1),"far",len(hz)-len(near))
