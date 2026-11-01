#!/usr/bin/env python3
"""Stream each wall-clock log once and extract the ring / TX / RX events needed for the decay analysis.

Output: <work>/<node>.pkl  (dict of lists). Time base: logger wall clock, seconds since
00:00:00 of day 0 = the date of the earliest logger timestamp (float). Node [LOG] clocks are NOT used
for ordering.
"""
from __future__ import annotations

import datetime as dt
import re
from pathlib import Path

TS_RE = re.compile(rb"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  ")

RE_WRITE = re.compile(r"RING_WRITE slot=(\d+) type=([0-9A-F]{2}) status=([0-9A-F]{2}) len=(\d+) "
                      r"msg_id=([0-9A-F]{8}) queued=(\d+)/(\d+) src=(\S+)")
RE_PRIO = re.compile(r"RING_PRIO slot=(\d+) prio=(\d+)")
RE_TXREAD = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d+) type=([0-9A-F]{2}) status=([0-9A-F]{2}) "
                       r"len=(\d+) msg_id=([0-9A-F]{8}) retry=(\d+) queued=(\d+)/\d+ lat=(\d+)ms")
RE_DROP = re.compile(r"RING_DROP_STALE slot=(\d+) prio=(\d+) age_s=(\d+) msg_id=([0-9A-F]{8}) reason=(\w+)")
RE_SUP = re.compile(r"RING_SUPERSEDE(_KEEP)? slot=(\d+) prio=(\d+) old_id=([0-9A-F]{8}) new_id=([0-9A-F]{8})")
RE_CSMA = re.compile(r"CSMA_AGED slot=(\d+) age=(\d+) stage=(\d) hold=(\d+)")
RE_LOGTX = re.compile(r"(\d\d:\d\d:\d\d) \[LOG\] TX x([0-9A-F]{8}) (\S) H([0-9A-F]{2}) prio=(\d) src=(\S) "
                      r"wait=(\d+) q=(\d+) cad=(\d+) len=(\d+) t=(\d+)")
RE_LOGRX = re.compile(r"(\d\d:\d\d:\d\d) \[LOG\] (\d{3}) (\S) x([0-9A-F]{8}) H([0-9A-F]{2}) (.*?)"
                      r"RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w) OWN:(\S) t=(\d+)")
RE_RLY = re.compile(r"(\d\d:\d\d:\d\d) \[LOG\] RLY x([0-9A-F]{8}) (\S) H([0-9A-F]{2}) q=(\S+) prio=(\d) slot=(-?\d+)")
RE_CANCEL = re.compile(r"\[NBR\]\|CANCEL\|(\d+)\|([0-9A-F]{8})\|(\w)\|([^|]*)\|")
RE_BUSY = re.compile(r"CAD_BUSY_1 attempt=(\d+)")
RE_TXGATE = re.compile(r"TX_GATE_ENTER qlen=(\d+) cad_attempt=(\d+)")
RE_TXUDP = re.compile(r"TX-UDP\s+(\d+) (\S) x([0-9A-F]{8})")


def first_date(path: Path) -> dt.date | None:
    """Date of the first timestamped line, or None when the log has no logger wall-clock prefix."""
    with path.open("rb") as fh:
        for i, raw in enumerate(fh):
            m = TS_RE.match(raw)
            if m:
                return dt.date(int(m.group(1)), int(m.group(2)), int(m.group(3)))
            if i >= 200:
                return None
    return None


def tsec(m: re.Match, base: dt.date) -> float:
    day = (dt.date(int(m.group(1)), int(m.group(2)), int(m.group(3))) - base).days
    return (day * 86400 + int(m.group(4)) * 3600 + int(m.group(5)) * 60
            + int(m.group(6)) + int(m.group(7)) / 1000.0)


def parse(path: Path, base: dt.date) -> dict:
    d: dict[str, list] = {k: [] for k in
                          ["write", "prio", "txread", "drop", "sup", "csma", "logtx", "logrx",
                           "rly", "cancel", "busy", "gate", "onrxerr", "ontxdone", "txstart", "txudp", "cet", "onrx"]}
    n = 0
    with open(path, "rb") as fh:
        for raw in fh:
            n += 1
            m = TS_RE.match(raw)
            if not m:
                continue
            t = tsec(m, base)
            line = raw[m.end():].decode("latin-1")
            if "[MC-DBG]" in line:
                if "RING_WRITE" in line:
                    x = RE_WRITE.search(line)
                    if x:
                        d["write"].append((t, int(x[1]), x[2], x[3], int(x[4]), x[5], int(x[6]), x[8], n))
                elif "RING_PRIO" in line:
                    x = RE_PRIO.search(line)
                    if x:
                        d["prio"].append((t, int(x[1]), int(x[2])))
                elif "RING_TX_READ" in line:
                    x = RE_TXREAD.search(line)
                    if x:
                        d["txread"].append((t, int(x[1]), int(x[2]), x[3], x[4], int(x[5]), x[6],
                                            int(x[7]), int(x[8]), int(x[9]), n))
                elif "RING_DROP_STALE" in line:
                    x = RE_DROP.search(line)
                    if x:
                        d["drop"].append((t, int(x[1]), int(x[2]), int(x[3]), x[4], x[5], n))
                elif "RING_SUPERSEDE" in line:
                    x = RE_SUP.search(line)
                    if x:
                        d["sup"].append((t, bool(x[1]), int(x[2]), int(x[3]), x[4], x[5]))
                elif "CSMA_AGED" in line:
                    x = RE_CSMA.search(line)
                    if x:
                        d["csma"].append((t, int(x[1]), int(x[2]), int(x[3]), int(x[4]), n))
                elif "CAD_BUSY_1" in line:
                    x = RE_BUSY.search(line)
                    if x:
                        d["busy"].append((t, int(x[1])))
                elif "TX_GATE_ENTER" in line:
                    x = RE_TXGATE.search(line)
                    if x:
                        d["gate"].append((t, int(x[1]), int(x[2])))
                elif "ONRXDONE_TIME" in line:
                    d["onrx"].append(t)
                elif "RX_ERROR" in line:
                    d["onrxerr"].append(t)
                elif "TX_START" in line:
                    d["txstart"].append(t)
            elif "[LOG]" in line:
                if "[LOG] TX x" in line:
                    x = RE_LOGTX.search(line)
                    if x:
                        d["logtx"].append((t, x[2], x[3], int(x[4], 16), int(x[5]), x[6], int(x[7]),
                                           int(x[8]), int(x[9]), int(x[10]), n))
                elif "RSSI:" in line and "FCS:0000" not in line:
                    x = RE_LOGRX.search(line)
                    if x:
                        # first path token for payload-bearing lines: after "H.. S. T. M.. "
                        body = x[6]
                        pm = re.search(r"M[0-9A-F]{2} ([^,>\s]+)", body)
                        orig = pm[1] if pm else ""
                        d["logrx"].append((t, x[3], x[4], int(x[5], 16), x[9], int(x[7]), int(x[8]),
                                           orig, int(x[2])))
                elif "[LOG] RLY" in line:
                    x = RE_RLY.search(line)
                    if x:
                        d["rly"].append((t, x[2], int(x[4], 16), x[5], int(x[6]), int(x[7])))
            elif "TX-UDP" in line:
                x = RE_TXUDP.search(line)
                if x:
                    d["txudp"].append((t, x[2], int(x[1])))
            elif line.startswith("[CET];tx;"):
                d["cet"].append((t, line.strip()))
            elif "|CANCEL|" in line:
                x = RE_CANCEL.search(line)
                if x:
                    d["cancel"].append((t, x[2], x[3], x[4]))
            elif "OnTXDone" in line:
                d["ontxdone"].append(t)
    d["_lines"] = n
    return d
