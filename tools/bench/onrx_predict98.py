import re
PFX=25
L=open("/Users/martinwerner/meshlog/rpizero/dk5en-98/run-20261012-2310.log",errors="replace").read().split("\n")
ev=[];cur=None
def cls(b):
    if "[LOG]" in b: return "setlog"
    if "[NBR]" in b: return "nbr"
    return "other"   # loradebug / displayinfo / retx / test
for i,ln in enumerate(L):
    b=ln[PFX:] if len(ln)>PFX else ""
    if re.search(r"RX_FLAG_PROCESS ts=",b):
        cur={"bytes":{"setlog":0,"nbr":0,"other":0},"ms":None}; continue
    if cur is None: continue
    if "RX_TIMEOUT_FIRE" in b or "CHANNEL_UTIL" in b or "[KEEP]" in b:
        cur=None; continue
    m=re.search(r"ONRXDONE_TIME ms=(\d+)",b)
    if m:
        cur["ms"]=int(m.group(1)); ev.append(cur); cur=None; continue
    cur["bytes"][cls(b)]+=len(b)+2
def pct(v,p):
    v=sorted(v); k=(len(v)-1)*p/100; f=int(k); c=min(f+1,len(v)-1); return v[f]+(v[c]-v[f])*(k-f)
def show(name,vals):
    print(f"{name:46s} n={len(vals)} mean={sum(vals)/len(vals):5.1f} p50={pct(vals,50):5.1f} p95={pct(vals,95):5.1f} p99={pct(vals,99):5.1f} max={max(vals):5.1f} >50={sum(1 for x in vals if x>50)/len(vals)*100:4.1f}%")
K=0.0868
tot={k:sum(e["bytes"][k] for e in ev) for k in("setlog","nbr","other")}
print("events",len(ev),"avg bytes/event",{k:round(v/len(ev)) for k,v in tot.items()})
base=[e["ms"] for e in ev]
show("observed (all four streams on)",base)
for name,drop in (("night1 loradebug only (drop setlog+nbr)",("setlog","nbr")),("night2 loradebug+setlog (drop nbr)",("nbr",)),("reference loradebug off (drop other)",("other",))):
    pred=[max(1.0,e["ms"]-K*sum(e["bytes"][k] for k in drop)) for e in ev]
    show("PREDICT "+name,pred)
