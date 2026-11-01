"""Cross-node view of the two hb9 gateways: ids relayed by both, double airing, who cancels whom.

usage: python3 -I pair_checks.py <writes4.jsonl> <writes5.jsonl> <out_json>
Matching key: msg_id within 30 min (ids are only unique ~4 h, and HB9JAT reuses them after > 1 h).
"""
from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import dist  # noqa: E402


def load(p):
    return [json.loads(x) for x in open(p)]


def main(p4: str, p5: str, out: str) -> None:
    a = load(p4)
    b = load(p5)
    ib = defaultdict(list)
    for w in b:
        ib[w["mid"]].append(w)
    pairs = []
    used = set()
    for w in a:
        best = None
        for v in ib.get(w["mid"], []):
            if id(v) in used:
                continue
            if abs(v["t"] - w["t"]) <= 1800:
                best = v
                break
        if best is not None:
            used.add(id(best))
            pairs.append((w, best))
    res = {"writes_4": len(a), "writes_5": len(b), "ids_written_by_both": len(pairs)}
    cls = Counter()
    both_air = []
    for w, v in pairs:
        key = ("4:" + w["fate"], "5:" + v["fate"])
        cls["%s|%s" % key] += 1
        if w["fate"] == "aired" and v["fate"] == "aired":
            both_air.append((w, v))
    res["fate_matrix"] = dict(cls.most_common())
    res["both_aired_n"] = len(both_air)
    res["both_aired_gap_s"] = dist([abs(w["end"] - v["end"]) for w, v in both_air])
    res["both_aired_gap_le_30s"] = sum(1 for w, v in both_air if abs(w["end"] - v["end"]) <= 30)
    res["both_aired_first_is_4"] = sum(1 for w, v in both_air if w["end"] < v["end"])
    res["both_aired_by_src_prio"] = dict(Counter("%s/p%s" % (w["src"], w["prio"]) for w, v in both_air))
    res["both_aired_by_type"] = dict(Counter(w["type"] for w, v in both_air))
    # rank at write: who writes first
    res["write_delta_5_minus_4_s"] = dist([v["t"] - w["t"] for w, v in pairs])
    # of the relay pairs only, share of ids where the second airer was in-time to hear the first (gap > 3 s)
    rel = [(w, v) for w, v in pairs if w["src"] == "rx_relay" and v["src"] == "rx_relay"]
    res["relay_pairs"] = len(rel)
    rc = Counter("4:%s|5:%s" % (w["fate"], v["fate"]) for w, v in rel)
    res["relay_pairs_fate"] = dict(rc.most_common())
    ra = [(w, v) for w, v in rel if w["fate"] == "aired" and v["fate"] == "aired"]
    res["relay_both_aired_n"] = len(ra)
    res["relay_both_aired_share_of_pairs_pct"] = round(100.0 * len(ra) / max(1, len(rel)), 1)
    res["relay_both_aired_by_prio"] = dict(Counter(w["prio"] for w, v in ra))
    json.dump(res, open(out, "w"), indent=1, default=str)
    print(json.dumps(res, indent=1, default=str))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
