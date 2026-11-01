#!/usr/bin/env python3
"""Per-entry TX-ring timelines from a MeshCom --loradebug log.

Usage: python3 -I timeline.py <log> [<log> ...]

For every RING_WRITE the entry is followed until it leaves the ring:
tx (RING_TX_READ, same slot+msg_id), ttl/cet (RING_DROP_STALE), superseded
(RING_SUPERSEDE old_id), evicted (RING_DROP_PRIO) or overwritten without a
marker. Supersede chains form one "episode". Classes: own HEY (40/auto_pos),
relay HEY (40/rx_relay), own WX (21/user_wx), relay pos (21/rx_relay).
"""
from __future__ import annotations

import re
import statistics
import sys
from collections import Counter
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  ")
W = re.compile(r"\] RING_WRITE slot=(\d+) type=([0-9A-F]+) status=([0-9A-F]+) len=\d+ msg_id=([0-9A-F]+) queued=(\d+)/\d+ src=(\w+)")
P = re.compile(r"\] RING_PRIO slot=(\d+) prio=(\d)")
SUP = re.compile(r"\] RING_SUPERSEDE slot=(\d+) prio=\d old_id=([0-9A-F]+) new_id=([0-9A-F]+)")
DPR = re.compile(r"\] RING_DROP_PRIO slot=(\d+) prio=\d type=[0-9A-F]+ msg_id=([0-9A-F]+)")
TXR = re.compile(r"\] RING_TX_READ slot=(\d+) prio=(\d) type=([0-9A-F]+) status=[0-9A-F]+ len=\d+ msg_id=([0-9A-F]+) retry=\d+ queued=(\d+)/\d+ lat=(\d+)ms")
STL = re.compile(r"\] RING_DROP_STALE slot=(\d+) prio=(\d) age_s=(\d+) msg_id=([0-9A-F]+) reason=(\w+)")
CADB = re.compile(r"\] CAD_BUSY attempt=")
RXLOG = re.compile(r"^\S+ \S+  \[LOG\]")

CLASSES = {("40", "auto_pos"): "own_HEY", ("40", "rx_relay"): "relay_HEY",
           ("21", "user_wx"): "own_WX", ("21", "rx_relay"): "relay_POS",
           ("21", "auto_pos"): "own_POS"}


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def pct(xs: list[float], q: float) -> float:
    if not xs:
        return float("nan")
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))]


def run(path: str) -> None:
    entries: list[dict] = []
    occ: dict[int, dict] = {}
    txs: list[tuple[float, int, str, float, int]] = []  # t, prio, cls, lat_s, idx
    last_write: dict | None = None
    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace")
            m0 = TS.match(line)
            if not m0 or "RING_" not in line:
                continue
            t = ts(m0.group(1))
            if (m := W.search(line)):
                slot = int(m.group(1))
                old = occ.get(slot)
                e = {"t": t, "slot": slot, "type": m.group(2), "id": m.group(4),
                     "q": int(m.group(5)), "src": m.group(6), "prio": None,
                     "end": None, "tend": None, "prev": None, "lat": None,
                     "cls": CLASSES.get((m.group(2), m.group(6)), "other")}
                if old is not None and old["end"] is None:
                    old["end"], old["tend"] = "overwritten", t
                    e["_old"] = old
                occ[slot] = e
                entries.append(e)
                last_write = e
                continue
            if (m := P.search(line)) and last_write and int(m.group(1)) == last_write["slot"]:
                last_write["prio"] = int(m.group(2))
                continue
            if (m := SUP.search(line)):
                cur = occ.get(int(m.group(1)))
                if cur and cur.get("_old") and cur["_old"]["id"] == m.group(2):
                    cur["_old"]["end"] = "superseded"
                    cur["prev"] = cur["_old"]
                continue
            if (m := DPR.search(line)):
                cur = occ.get(int(m.group(1)))
                if cur and cur.get("_old") and cur["_old"]["id"] == m.group(2):
                    cur["_old"]["end"] = "evicted"
                continue
            if (m := TXR.search(line)):
                slot = int(m.group(1))
                cur = occ.get(slot)
                if cur and cur["id"] == m.group(4) and cur["end"] in (None, "tx"):
                    if cur["end"] is None:
                        cur["end"], cur["tend"] = "tx", t
                        cur["lat"] = int(m.group(6)) / 1000
                        cur["txq"] = int(m.group(5))
                        txs.append((t, int(m.group(2)), cur["cls"], cur["lat"], entries.index(cur) if False else -1))
                        cur["_txidx"] = len(txs) - 1
                    else:
                        cur["retx_read"] = cur.get("retx_read", 0) + 1
                continue
            if (m := STL.search(line)):
                cur = occ.get(int(m.group(1)))
                if cur and cur["id"] == m.group(4) and cur["end"] is None:
                    cur["end"], cur["tend"] = m.group(5), t
                    cur["age_s"] = int(m.group(3))
                continue

    print(f"### {path}")
    tx_list = [e for e in entries if e["end"] == "tx"]
    for cls in ("own_HEY", "relay_HEY", "own_WX", "relay_POS", "own_POS"):
        es = [e for e in entries if e["cls"] == cls]
        if not es:
            continue
        oc = Counter(e["end"] or "open" for e in es)
        lats = [e["lat"] for e in es if e["lat"] is not None]
        qs = [e["q"] for e in es]
        prios = Counter(e["prio"] for e in es)
        print(f"{cls:10s} n={len(es):5d} prio={dict(prios)} outcomes={dict(oc)}")
        print(f"           lat_tx p50={pct(lats,.5):.1f}s p90={pct(lats,.9):.1f}s  queued_at_write p50={pct(qs,.5)} mean={statistics.mean(qs):.1f}")
        # episodes: heads = entries not superseding anything are starts; walk forward
        nxt = {id(e["prev"]): e for e in es if e["prev"] is not None}
        starts = [e for e in es if e["prev"] is None]
        ep_out: Counter[str] = Counter()
        ep_wait_tx: list[float] = []
        for s in starts:
            cur = s
            while id(cur) in nxt:
                cur = nxt[id(cur)]
            ep_out[cur["end"] or "open"] += 1
            if cur["end"] == "tx":
                ep_wait_tx.append(cur["tend"] - s["t"])
        aired = ep_out.get("tx", 0)
        print(f"           episodes={len(starts)} final={dict(ep_out)} aired={aired/len(starts)*100:.1f}%  episode_wait_tx p50={pct(ep_wait_tx,.5):.1f}s")
        # load at write: number of TX (any prio) by prio during the lifetime
        life_tx: Counter[int] = Counter()
        ahead_same = 0
        overt = 0
        n_life = 0
        for e in es:
            if e["tend"] is None:
                continue
            n_life += 1
            for f in tx_list:
                if f is e:
                    continue
                if e["t"] < f["tend"] < e["tend"]:
                    life_tx[f["prio"]] += 1
                    if f["prio"] == e["prio"]:
                        if f["t"] < e["t"]:
                            ahead_same += 1
                        else:
                            overt += 1
        if n_life:
            print("           per-entry mean TX of others during life by prio: "
                  + " ".join(f"p{p}={life_tx[p]/n_life:.1f}" for p in sorted(life_tx))
                  + f"  same-prio-older={ahead_same/n_life:.2f} same-prio-younger(overtake)={overt/n_life:.2f}")
    # time-of-minute/second phase for own WX writes
    wx = [e for e in entries if e["cls"] == "own_WX"]
    if wx:
        print("own_WX writes (HH:MM:SS end):", " ".join(
            datetime.fromtimestamp(e["t"]).strftime("%H:%M:%S") + ":" + (e["end"] or "open")[:3] for e in wx[:60]))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        run(p)
