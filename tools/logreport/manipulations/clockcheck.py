#!/usr/bin/env python3
"""Cross-node logger clock offsets: TX at A vs first RX at B whose last hop is A."""
from __future__ import annotations

import json
import sys
from collections import defaultdict

sys.path.insert(0, str(__import__("pathlib").Path(__file__).parent))
from common import CALL2NODE, NODES, OUTD, load, median

ev = {n: load(n) for n in NODES}
tx = defaultdict(lambda: defaultdict(list))  # id -> node -> [t]
for n in NODES:
    for e in ev[n]:
        if e["k"] == "tx":
            tx[e["id"]][n].append(e["t"])
res = {}
for b in NODES:
    for e in ev[b]:
        if e["k"] == "rx" and len(e["path"]) >= 2:
            a = CALL2NODE.get(e["path"][-1])
            if a and a != b and e["id"] in tx and a in tx[e["id"]]:
                d = [e["t"] - t for t in tx[e["id"]][a] if 0 <= e["t"] - t < 20 or -20 < e["t"] - t < 0]
                if d:
                    k = f"{a}->{b}"
                    res.setdefault(k, []).append(min(d, key=abs))
out = {}
for k, v in sorted(res.items()):
    out[k] = {"n": len(v), "median_s": round(median(v), 2)}
    print(k, out[k])
json.dump(out, open(OUTD / "work" / "clockcheck.json", "w"), indent=1)
