#!/usr/bin/env python3
"""Per-node, per-hour manipulation counts + relay outcome ledger (Q6 sub-question 1).

Outputs work/ledger.json.
Relay ledger: every '[LOG] RLY ... q=tx' (type T/P/H) is followed to its fate by msg_id on the same node:
  sent (TX src=r same id within 900 s), cancel ([NBR]|CANCEL), supersede (RING_SUPERSEDE old_id),
  stale (RING_DROP_STALE id), else unaccounted.
"""
from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict

sys.path.insert(0, str(__import__("pathlib").Path(__file__).parent))
from common import NODES, OUTD, STANDALONE, load, median

HOUR = lambda t: __import__("time").strftime("%m-%d %H", __import__("time").localtime(t))

res: dict = {}
for n in NODES + STANDALONE:
    ev = load(n)
    perhour: dict[str, Counter] = defaultdict(Counter)
    tot: Counter = Counter()
    # index fates
    tx_by_id = defaultdict(list)
    cancel_by_id = defaultdict(list)
    sup_old = defaultdict(list)
    stale_by_id = defaultdict(list)
    for e in ev:
        if e["k"] == "tx":
            tx_by_id[e["id"]].append(e)
        elif e["k"] == "cancel":
            cancel_by_id[e["id"]].append(e)
        elif e["k"] == "supersede":
            sup_old[e["old"]].append(e)
        elif e["k"] == "dropstale":
            stale_by_id[e["id"]].append(e)

    def hh(e):
        return HOUR(e["t"]) if e["t"] is not None and n not in STANDALONE else "na"

    fate: Counter = Counter()
    fate_type: dict = defaultdict(Counter)
    latency_sent = []
    for e in ev:
        k = e["k"]
        h = hh(e)
        key = None
        if k == "rx":
            key = "rx_dup_echo" if (e["dup"] and e["own"]) else ("rx_dup_foreign" if e["dup"] else "rx_new")
            if not e["dup"] and e["own"]:
                key = "rx_new_ownpath"
        elif k == "ack":
            key = "ack_dup" if e["dup"] else "ack_new"
        elif k == "err":
            key = "rx_crc_err"
        elif k == "rly":
            key = "rly_" + e["q"]
        elif k == "tx":
            key = f"tx_{e['src']}_{e['typ']}"
        elif k in ("need",):
            key = f"need_{e['case']}"
        elif k in ("cancel", "refuse", "echo", "gwu", "gwi", "csmaaged", "supersede", "dropstale", "loopblk", "ackreject", "cadbusy", "cadsafety", "relayq", "nbrdrop"):
            key = k if k != "dropstale" else f"dropstale_{e['reason']}"
        elif k == "sym":
            key = "sym_" + e["role"]
        if key:
            perhour[h][key] += 1
            tot[key] += 1
        if k == "rly" and e["q"] == "tx":
            # fate
            ids = e["id"]
            t0 = e["t"]
            f = None
            tx_hit = [x for x in tx_by_id.get(ids, []) if x["src"] == "r" and 0 <= x["t"] - t0 < 900] if n not in STANDALONE else [x for x in tx_by_id.get(ids, []) if x["src"] == "r"]
            if tx_hit:
                f = "sent"
                if n not in STANDALONE:
                    latency_sent.append(min(x["t"] for x in tx_hit) - t0)
            else:
                cc = [x for x in cancel_by_id.get(ids, []) if n in STANDALONE or 0 <= x["t"] - t0 < 900]
                if cc:
                    f = "cancel"
                elif [x for x in sup_old.get(ids, []) if n in STANDALONE or 0 <= x["t"] - t0 < 900]:
                    f = "supersede"
                elif [x for x in stale_by_id.get(ids, []) if n in STANDALONE or 0 <= x["t"] - t0 < 900]:
                    f = "stale"
                else:
                    f = "unaccounted"
            fate[f] += 1
            fate_type[e["typ"]][f] += 1
    res[n] = {
        "totals": dict(tot),
        "per_hour": {h: dict(c) for h, c in sorted(perhour.items())},
        "relay_fate": dict(fate),
        "relay_fate_by_type": {t: dict(c) for t, c in fate_type.items()},
        "relay_wait_median_s": round(median(latency_sent), 2) if latency_sent else None,
    }
    print(n, dict(tot))
    print("   fate", dict(fate), {t: dict(c) for t, c in fate_type.items()})
json.dump(res, open(OUTD / "work" / "ledger.json", "w"), indent=1)
