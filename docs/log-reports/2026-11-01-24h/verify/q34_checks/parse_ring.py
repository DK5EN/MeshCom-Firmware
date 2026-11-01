#!/usr/bin/env python3
"""Independent parser for the q34 verification: ring/TX/RX/CSMA events per OE3 node.

Usage: python3 -I parse_ring.py <norm-dir> <out.json>
"""
from __future__ import annotations

import json
import re
import sys
from datetime import datetime
from pathlib import Path

NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
T0 = datetime(2026, 10, 31).timestamp()

R_WRITE = re.compile(r"RING_WRITE slot=(\d+) type=(\w+) status=(\w+) len=(\d+) msg_id=(\w+) queued=(\d+)/\d+ src=(\w+)")
R_PRIO = re.compile(r"RING_PRIO slot=(\d+) prio=(\d+)")
R_READ = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d+) type=(\w+) status=(\w+) len=(\d+) msg_id=(\w+) retry=(\d+) queued=(\d+)/\d+ lat=(\d+)ms")
R_DROP = re.compile(r"RING_DROP_STALE slot=(\d+) prio=(\d+) age_s=(\d+) msg_id=(\w+) reason=(\w+)")
R_SUP = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=(\d+) old_id=(\w+) new_id=(\w+)")
R_LTX = re.compile(r"\[LOG\] TX x(\w+) (\S) H(\d+) prio=(\d+) src=(\w+) wait=(\d+) q=(\d+) cad=(\d+) len=(\d+)")
R_LRX = re.compile(r"\[LOG\] (\d{3}) (\S) x(\w+) H(\d+) .*?(\S+)>.*FCS:(\w{4})")
R_AGED = re.compile(r"CSMA_AGED slot=(\d+) age=(\d+) stage=(\d+) hold=(\d+)")


def ts(line: str) -> float | None:
    if len(line) < 23 or line[4] != "-":
        return None
    d = int(line[8:10]) - 31  # day offset: 31.10 -> 0, 01.11 -> 1
    if line[5:7] == "11":
        d = int(line[8:10])
    return d * 86400 + int(line[11:13]) * 3600 + int(line[14:16]) * 60 + float(line[17:23])


def parse(path: Path) -> dict:
    ev: dict[str, list] = {k: [] for k in ("write", "read", "drop", "sup", "ltx", "lrx", "aged", "rxerr", "txstart", "txudp")}
    last_write_by_slot: dict[int, list] = {}
    with path.open("rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1").rstrip("\n")
            t = ts(line)
            if t is None:
                continue
            if "RING_" in line:
                if (m := R_WRITE.search(line)):
                    w = [t, int(m[1]), m[2], m[3], int(m[4]), m[5], int(m[6]), m[7], None]
                    ev["write"].append(w)
                    last_write_by_slot[int(m[1])] = w
                elif (m := R_PRIO.search(line)):
                    w = last_write_by_slot.get(int(m[1]))
                    if w is not None and w[8] is None:
                        w[8] = int(m[2])
                elif (m := R_READ.search(line)):
                    ev["read"].append([t, int(m[1]), int(m[2]), m[3], m[4], int(m[5]), m[6], int(m[7]), int(m[9])])
                elif (m := R_DROP.search(line)):
                    ev["drop"].append([t, int(m[1]), int(m[2]), int(m[3]), m[4], m[5]])
                elif (m := R_SUP.search(line)):
                    ev["sup"].append([t, int(m[1]), int(m[2]), m[3], m[4]])
            elif "[LOG] TX x" in line:
                if (m := R_LTX.search(line)):
                    ev["ltx"].append([t, m[1], m[2], int(m[3]), int(m[4]), m[5], int(m[6]), int(m[9])])
            elif "[LOG] " in line and " x" in line:
                if (m := R_LRX.search(line)):
                    ev["lrx"].append([t, m[3], m[2], int(m[4]), int(m[1]), m[5], m[6]])
            elif "CSMA_AGED" in line:
                if (m := R_AGED.search(line)):
                    ev["aged"].append([t, int(m[1]), int(m[2]), int(m[3]), int(m[4])])
            elif "RX_ERROR" in line:
                ev["rxerr"].append(t)
            elif "TX_START" in line:
                ev["txstart"].append(t)
            elif " TX-UDP " in line:
                m = re.search(r" x(\w{8}) ", line)
                if m:
                    ev["txudp"].append([t, m[1]])
    return ev


def main() -> None:
    nd = Path(sys.argv[1])
    out = {n: parse(nd / f"{n}.log") for n in NODES}
    Path(sys.argv[2]).write_text(json.dumps(out))
    for n, e in out.items():
        print(n, {k: len(v) for k, v in e.items()})


if __name__ == "__main__":
    main()
