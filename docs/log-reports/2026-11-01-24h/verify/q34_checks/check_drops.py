#!/usr/bin/env python3
"""Verify q34 TTL-drop claims: per-node classification, sent-before-drop, own-beacon delivery,
relay-drop redundancy, RX of own dropped ids elsewhere.

Usage: python3 -I check_drops.py <ev.json from parse_ring.py>
"""
from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict

W = 1800.0


def hhmm(t: float) -> str:
    d, r = divmod(t, 86400)
    return f"{'31.10' if d == 0 else '01.11'} {int(r // 3600):02d}:{int(r % 3600 // 60):02d}:{r % 60:05.2f}"


def main() -> None:
    ev = json.load(open(sys.argv[1]))
    tx_by_id: dict[str, list] = defaultdict(list)   # id -> (t, node)
    rx_by_id: dict[str, list] = defaultdict(list)   # id -> (t, node, hop)
    for n, e in ev.items():
        for x in e["ltx"]:
            tx_by_id[x[1]].append((x[0], n))
        for x in e["read"]:
            tx_by_id[x[6]].append((x[0], n + ":read"))
        for x in e["lrx"]:
            if x[6] != "0000":
                rx_by_id[x[1]].append((x[0], n, x[3]))
    tot = Counter()
    sent_before = 0
    own_rx_elsewhere = 0
    relay_cls = Counter()
    for n, e in ev.items():
        writes = defaultdict(list)
        for w in e["write"]:
            writes[w[5]].append(w)
        cls = Counter()
        for d in e["drop"]:
            t, slot, prio, age, mid, reason = d
            cand = [w for w in writes.get(mid, []) if w[0] <= t and w[1] == slot]
            if not cand:
                cls[(prio, "prelog", reason)] += 1
                continue
            w = cand[-1]
            src = w[7]
            own = src in ("auto_pos", "user_wx")
            kind = ("own_" if own else "relay_") + ("HEY" if w[2] == "40" else "POS" if w[2] == "21" else w[2])
            cls[(prio, kind, reason)] += 1
            # any TX of this id at this node between write and drop
            if any(w[0] <= tt <= t and nn.startswith(n) for tt, nn in tx_by_id.get(mid, [])):
                sent_before += 1
            if own:
                if any(nn != n and w[0] - W <= tt <= t + W for tt, nn, _ in rx_by_id.get(mid, [])):
                    own_rx_elsewhere += 1
            else:
                other_tx_after = [tt for tt, nn in tx_by_id.get(mid, []) if not nn.startswith(n) and ":read" not in nn and w[0] <= tt <= t + W]
                other_tx_any = [tt for tt, nn in tx_by_id.get(mid, []) if not nn.startswith(n) and ":read" not in nn and w[0] - W <= tt <= t + W]
                own_rx = [(tt, h) for tt, nn, h in rx_by_id.get(mid, []) if nn == n and w[0] - W <= tt <= t + W]
                first_hop = [h for tt, h in own_rx if tt <= w[0] + 0.5]
                h0 = max(first_hop) if first_hop else None
                lower = h0 is not None and any(h < h0 for tt, h in own_rx if tt > w[0] + 0.5)
                any_rx_other = any(nn != n and w[0] - W <= tt <= t + W for tt, nn, _ in rx_by_id.get(mid, []))
                if other_tx_after:
                    c = "cluster_tx_after_write"
                elif lower:
                    c = "reheard_lower_hop"
                elif other_tx_any:
                    c = "cluster_tx_before_write"
                else:
                    c = "none"
                relay_cls[(n, kind, c)] += 1
        print(n, len(e["drop"]), dict(sorted(cls.items())))
        for k, v in cls.items():
            tot[k[1]] += v
        ages = Counter((d[2], d[3]) for d in e["drop"])
        print("   ages", dict(sorted(ages.items())), "reasons", Counter(d[5] for d in e["drop"]))
    print("TOTAL", dict(tot), "sum", sum(tot.values()))
    print("drops with own TX of same id between write and drop:", sent_before)
    print("own dropped ids received by another node (+-30min):", own_rx_elsewhere)
    rc = Counter()
    for (n, k, c), v in relay_cls.items():
        rc[c] += v
    print("relay classes", dict(rc), "sum", sum(rc.values()))
    print("relay classes by node", dict(sorted(relay_cls.items())))


if __name__ == "__main__":
    main()
