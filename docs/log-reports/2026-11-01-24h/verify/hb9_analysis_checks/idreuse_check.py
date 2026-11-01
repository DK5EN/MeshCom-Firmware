"""For msg_ids aired twice (> 5 min apart), compare the payloads of the two receptions: id reuse or re-relay?

usage: python3 -I idreuse_check.py <norm_log> <writes_jsonl> <out_json>
"""
from __future__ import annotations

import json
import re
import sys
from collections import defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import iter_lines  # noqa: E402

MH = re.compile(r"MH-LoRa\s+\d{3} \S x([0-9A-F]{8}) H\d\d S\d T\d M\d\d (.*?)>(.*?) HW:")


def main(path: str, wl: str, out: str) -> None:
    ws = [json.loads(x) for x in open(wl)]
    by = defaultdict(list)
    for w in ws:
        if w["fate"] == "aired":
            by[w["mid"]].append(w)
    cand = {m: l for m, l in by.items() if len(l) > 1 and l[1]["t"] - l[0]["t"] > 300}
    # collect payloads per id with times (first MH-LoRa after each write is not guaranteed; use nearest before write)
    pl = defaultdict(list)
    for n, t, b in iter_lines(path):
        m = MH.search(b)
        if m and m.group(1) in cand:
            pl[m.group(1)].append((t, m.group(2).split(",")[0], m.group(3)))
    same = diff = unk = 0
    rows = []
    for mid, l in cand.items():
        a = [p for p in pl[mid] if p[0] <= l[0]["t"] + 1]
        b2 = [p for p in pl[mid] if p[0] <= l[1]["t"] + 1 and p[0] > l[0]["end"]]
        if not a or not b2:
            unk += 1
            continue
        pa, pb = a[-1], b2[-1]
        # strip time-dependent fields is not possible generically: compare raw payload
        if pa[2] == pb[2]:
            same += 1
            verdict = "same_payload"
        else:
            diff += 1
            verdict = "different_payload"
        rows.append({"id": mid, "gap_min": round((l[1]["t"] - l[0]["t"]) / 60, 1), "origin": pa[1], "verdict": verdict,
                     "p1": pa[2][:40], "p2": pb[2][:40]})
    from collections import Counter
    res = {"origin_counts": dict(Counter(r["origin"] for r in rows)), "candidates": len(cand), "same_payload": same, "different_payload": diff, "unknown": unk, "rows": sorted(rows, key=lambda r: r["gap_min"])[:60]}
    json.dump(res, open(out, "w"), indent=1)
    print({k: v for k, v in res.items() if k != "rows"})
    for r in res["rows"][:8]:
        print(r)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
