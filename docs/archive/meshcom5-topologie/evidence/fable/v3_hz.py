import re, collections
ME="DK5EN-98"
rx=re.compile(r'MH-LoRa \d+ (\S) x[0-9A-F]+ H\d+ S\d T\d M\d+ ([^>]+)>')
minhop=collections.defaultdict(lambda:99)
hz_senders=set(); hz_frames=0; entry_me=0; hz_by=collections.Counter()
seen_hops=collections.defaultdict(set)
for line in open('combined_window.log',errors='replace'):
    m=rx.search(line)
    if not m: continue
    t,path=m.group(1),m.group(2)
    tok=path.split(',')
    s=tok[0]
    if s==ME: continue
    n=len(tok)
    minhop[s]=min(minhop[s],n); seen_hops[s].add(n)
    if t in '!@' and n>=3:
        hz_frames+=1; hz_senders.add(s)
        if tok[-2]==ME: entry_me+=1
print("horizon-creating frames (POS/HEY, >=3 tok):",hz_frames)
print("frames whose entry token (2nd-to-last) is own call:",entry_me)
print("distinct senders that would get a horizon entry:",len(hz_senders))
near=[s for s in hz_senders if minhop[s]<=2]
print("  of these with min hop <=2 in same window (coexist with row / direct):",len(near))
print("   min hop 1:",sorted(s for s in near if minhop[s]==1))
print("   min hop 2:",sorted(s for s in near if minhop[s]==2))
far=[s for s in hz_senders if minhop[s]>=3]
print("  truly far (min hop>=3):",len(far))
# senders whose seen hop counts vary (for G3)
var=[(s,sorted(seen_hops[s])) for s in far if len(seen_hops[s])>1]
print("far senders seen at >1 hop count:",len(var)); print(var[:15])
