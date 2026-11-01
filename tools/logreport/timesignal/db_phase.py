"""Phase of the three server SSIDs against each other (DB0AIS capture, content timestamps).
Usage (via run.py): db_phase.py <node>; writes <out>/data/db_phase_<node>.json"""
import json
import sys
from collections import defaultdict

from common import cont_epoch, load, stats
from nodeset import OUT as OUT_DIR

NODE = sys.argv[1]
ev = load(str(OUT_DIR / "data"), NODE)
c = defaultdict(dict)
for e in ev:
    if "cont" in e and e["cont"][:1] != "<":
        c[e["path"][0]][e["id"]] = cont_epoch(e["cont"])
ts = {s: sorted(v.values()) for s, v in c.items()}
def nearest(t, arr):
    return min(arr, key=lambda x: abs(x - t)) - t


def offsets(src, ref):
    """Distance of each telegram of source `src` to the nearest telegram of `ref` (empty if either is absent)."""
    if not ts.get(ref):
        return []
    return [nearest(t, ts[ref]) for t in ts.get(src, [])]


d33 = offsets("OE1XAR-33", "OE1XAR-62")
d63 = offsets("OE1XAR-63", "OE1XAR-62")
d3363 = offsets("OE1XAR-63", "OE1XAR-33")
r = {"minus62_to_nearest_33_s": stats(d33, 0), "minus62_to_nearest_63_s": stats(d63, 0), "33_to_nearest_63_s": stats(d3363, 0)}
# combined sequence gaps (all three sources merged, sorted)
allt = sorted(t for v in ts.values() for t in v)
g = [b - a for a, b in zip(allt, allt[1:])]
r["merged_gap_s"] = stats(g, 0)
r["merged_gaps_gt_300"] = len([x for x in g if x > 300])
r["merged_n"] = len(allt)
# 62 phase drift: content time mod 600 per telegram
json.dump(r, open(OUT_DIR / "data" / f"db_phase_{NODE}.json", "w"), indent=1)
print(json.dumps(r, indent=1))
