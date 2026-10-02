import re, statistics
from datetime import datetime
rx = re.compile(r'^(\S+ \S+)\s+\S+ \[LOG\] \d+ \S+ x[0-9A-F]{8} H\d\d S\d T\d M\d\d ([^>\s]+)>.* SNR:(-?\d+) DUP:(\w)')
per = {}
for line in open('fable/combined_window.log', errors='replace'):
    if '[LOG]' not in line or 'SNR:' not in line: continue
    m = rx.match(line)
    if not m: continue
    ts = datetime.strptime(m.group(1)[:19], '%Y-%m-%d %H:%M:%S')
    last = m.group(2).split(',')[-1]
    per.setdefault(last, []).append((ts, int(m.group(3)), m.group(4)))
T = -16
for call, v in sorted(per.items(), key=lambda kv: -len(kv[1])):
    snrs = [s for _, s, _ in v]
    below = sum(1 for s in snrs if s < T)
    cross = sum(1 for a, b in zip(snrs, snrs[1:]) if (a >= T) != (b >= T))
    span_h = (v[-1][0] - v[0][0]).total_seconds() / 3600 or 1
    print(f"{call:12s} n={len(v):5d} /h={len(v)/span_h:6.1f} med={statistics.median(snrs):4.0f} min={min(snrs):4d} max={max(snrs):4d} below-16={below:4d} crossings={cross}")
