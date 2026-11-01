#!/usr/bin/env python3
"""Reconstruct the getNextTxSlot() head over time and measure, per head class,
the time spent as head, how often the head got transmitted (hazard), the CSMA
hold values armed (RX_TIMEOUT_FIRE wait=), TX_GATE_ENTER attempts and foreign
receptions ([LOG] RX lines, FCS:0000 excluded) while that class was head.

Usage: python3 -I head_hazard.py <log> [<log> ...]
Head = alive entry with lowest prio, then earliest enqueue (supersede resets
enqueue time, as ringEnqueueTime = millis() in addTxRingEntryCore()).
"""
from __future__ import annotations

import re
import sys
from collections import Counter, defaultdict
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  ")
W = re.compile(r"\] RING_WRITE slot=(\d+) type=([0-9A-F]+) status=[0-9A-F]+ len=\d+ msg_id=([0-9A-F]+) queued=\d+/\d+ src=(\w+)")
P = re.compile(r"\] RING_PRIO slot=(\d+) prio=(\d)")
TXR = re.compile(r"\] RING_TX_READ slot=(\d+) .*msg_id=([0-9A-F]+) ")
STL = re.compile(r"\] RING_DROP_STALE slot=(\d+) .*msg_id=([0-9A-F]+) ")
RTF = re.compile(r"\] RX_TIMEOUT_FIRE ts=\d+ wait=(\d+)")
CLASSES = {("40", "auto_pos"): "own_HEY", ("40", "rx_relay"): "relay_HEY",
           ("21", "user_wx"): "own_WX", ("21", "rx_relay"): "relay_POS",
           ("21", "auto_pos"): "own_POS"}


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def run(path: str) -> None:
    slots: dict[int, tuple] = {}  # slot -> (prio, t_enq, cls, id)
    pending_write: tuple | None = None
    head_time: Counter[str] = Counter()
    head_tx: Counter[str] = Counter()
    gate: Counter[str] = Counter()
    rx: Counter[str] = Counter()
    holds: dict[str, list[int]] = defaultdict(list)
    cadbusy: Counter[str] = Counter()
    last_t: float | None = None

    def head() -> str:
        if not slots:
            return "empty"
        p, te, c, _ = min(slots.values(), key=lambda v: (v[0], v[1]))
        return c if c != "other" else f"other_p{p}"

    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace")
            m0 = TS.match(line)
            if not m0:
                continue
            t = ts(m0.group(1))
            h = head()
            if last_t is not None and t > last_t:
                head_time[h] += t - last_t
            last_t = t
            body = line[25:]
            if "[LOG] " in body and " TX " not in body and "FCS:0000" not in body and " ERR " not in body:
                rx[h] += 1
                continue
            if "TX_GATE_ENTER" in body:
                gate[h] += 1
                continue
            if "] CAD_BUSY attempt" in body:
                cadbusy[h] += 1
                continue
            if (m := RTF.search(body)):
                holds[h].append(int(m.group(1)))
                continue
            if "RING_" not in body:
                continue
            if (m := W.search(body)):
                pending_write = (int(m.group(1)), CLASSES.get((m.group(2), m.group(4)), "other"), m.group(3), t)
                continue
            if (m := P.search(body)) and pending_write and int(m.group(1)) == pending_write[0]:
                s, c, i, tw = pending_write
                slots[s] = (int(m.group(2)), tw, c, i)
                pending_write = None
                continue
            if (m := TXR.search(body)):
                s = int(m.group(1))
                if s in slots and slots[s][3] == m.group(2):
                    c = slots[s][2]
                    if h == c or (h.startswith("other") and c == "other"):
                        head_tx[h] += 1
                    del slots[s]
                continue
            if (m := STL.search(body)):
                s = int(m.group(1))
                if s in slots and slots[s][3] == m.group(2):
                    del slots[s]
                continue

    print(f"### {path}")
    print(f"{'head':12s} {'time_s':>8s} {'tx':>5s} {'tx/min':>7s} {'gate/min':>8s} {'cadbusy/min':>11s} {'rx/min':>7s} {'hold_p50':>8s} {'hold_mean':>9s}")
    for h, tt in sorted(head_time.items(), key=lambda kv: -kv[1]):
        if tt < 60:
            continue
        hs = sorted(holds[h])
        p50 = hs[len(hs) // 2] if hs else 0
        mean = sum(hs) / len(hs) if hs else 0
        mins = tt / 60
        print(f"{h:12s} {tt:8.0f} {head_tx[h]:5d} {head_tx[h]/mins:7.2f} {gate[h]/mins:8.2f} {cadbusy[h]/mins:11.2f} {rx[h]/mins:7.2f} {p50:8d} {mean:9.0f}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        run(p)
