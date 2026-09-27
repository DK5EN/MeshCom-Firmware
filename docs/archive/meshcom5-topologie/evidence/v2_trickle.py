import re, bisect
from datetime import datetime
rx = re.compile(r'^(\S+ \S+)\s+\S+ \[LOG\] \d+ \S+ x[0-9A-F]{8} H\d\d S\d T\d M\d\d ([^>\s]+)>.* SNR:(-?\d+) DUP:(\w)')
per = {}
t0=None
for line in open('fable/combined_window.log', errors='replace'):
    if '[LOG]' not in line or 'SNR:' not in line: continue
    m = rx.match(line)
    if not m: continue
    ts = datetime.strptime(m.group(1)[:19], '%Y-%m-%d %H:%M:%S').timestamp()
    if t0 is None: t0=ts
    last = m.group(2).split(',')[-1]
    if last == 'DK5EN-98': continue
    per.setdefault(last, []).append((ts, int(m.group(3))))
tend = max(v[-1][0] for v in per.values())
T=-16
def ncnt(t, hm):
    n=0
    for c,v in per.items():
        i = bisect.bisect_right([a for a,_ in v], t)-1
        if i<0: continue
        ts,s = v[i]
        if t-ts >= 3600: continue           # D60
        if c in hm or s >= T: n+=1
    return n
def run(hm, label):
    # start after first hour so D60 is populated
    t = t0+3600; iv=900; last=None; resets=0; ticks=0
    while t < tend:
        cur = ncnt(t, hm); ticks+=1
        if last is not None and cur != last:
            resets+=1; iv=30
        last=cur
        iv=min(iv*2,900); t+=iv
    hrs=(tend-t0-3600)/3600
    print(f"{label:45s} ticks={ticks} resets={resets} over {hrs:.1f} h -> {resets/hrs:.2f}/h")
calls=set(per)
run(calls-{'DL2JA-1'}, "actual HM (only DL2JA-1 SYM-dependent)")
run(set(), "worst case: no HM at all (Stufe 4 extreme)")
run(calls, "one-sided D60 only (today-like)")
for T2 in (-8,-10,-12,-14):
    T=T2; run(set(), f"no HM, threshold {T2} dB (sensitivity)")
import statistics
print("--- one straddling neighbour at a time, threshold = its median SNR ---")
for c,v in per.items():
    med = statistics.median(s for _,s in v)
    T = med
    rate = len(v)/((v[-1][0]-v[0][0])/3600)
    run(calls-{c}, f"{c} alone SYM-dep, T=median {med:.0f} ({rate:.0f} fr/h)")
