"""DB0AIS node-clock offset vs NTP epoch (no logger wall clock in this capture).
Uses the closest timestamped line BEFORE each [NTP];ok line (<= 20 lines back).
Usage (via run.py): db_clock.py <node>; writes <out>/data/db_clock_<node>.json"""
import json
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from statistics import median  # noqa: F401

from nodeset import NORM as NORM_DIR, OUT as OUT_DIR
RE_T = re.compile(rb"^(\d\d):(\d\d):(\d\d) \[?[A-Za-z]")
RE_N = re.compile(rb"^\[NTP\];ok;epoch;(\d+);rtt;(\d+)")
last = None; lastn = 0; n = 0; rows = []
NODE = sys.argv[1]
with open(NORM_DIR / f"{NODE}.log", "rb") as f:
    for line in f:
        n += 1
        line = line.rstrip(b"\r\n")
        m = RE_T.match(line)
        if m and not line.startswith(b"00:00:00"):
            last = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)); lastn = n
            continue
        m = RE_N.match(line)
        if m and last is not None and n - lastn < 40:
            ep = int(m.group(1))
            o = ((last - ep % 86400 + 43200) % 86400) - 43200
            rows.append((ep, o))
# transitions
trans = []
prev = None
for ep, o in rows:
    b = round(o / 3600)
    if prev is not None and b != prev:
        trans.append((datetime.fromtimestamp(ep, timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC"), prev, b))
    prev = b
c = Counter(round(o / 3600) for _, o in rows)
res = {"n": len(rows), "hour_buckets": dict(c), "transitions": trans,
       "first_epoch_utc": datetime.fromtimestamp(rows[0][0], timezone.utc).isoformat(), "last_epoch_utc": datetime.fromtimestamp(rows[-1][0], timezone.utc).isoformat()}
json.dump(res, open(OUT_DIR / "data" / f"db_clock_{NODE}.json", "w"), indent=1)
