"""Independent parser for the q6 verification (OE3 cluster logs).

Usage: python3 -I q6_parse.py <norm-dir> <out.json>
Streams each <node>.log once and writes a compact event dump (JSON) used by q6_verify.py.
"""
from __future__ import annotations

import json
import re
import sys
from datetime import datetime
from pathlib import Path

NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  ")
TX = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) (\S) H(\w\w) prio=(\d) src=(\w) wait=(\d+) q=\d+ cad=\d+ len=(\d+) t=(\d+)")
TXLORA = re.compile(r"TX-Lo[Rr]a\s+\d{3} A x([0-9A-F]{8}) H(\w\w) x([0-9A-F]{8}) (\w\w) (\w\w)")
RXACK = re.compile(r"\[LOG\] \d{3} A x([0-9A-F]{8}) H(\w\w) x([0-9A-F]{8}) (\w\w) (\w\w) RSSI:\S+ SNR:\S+ DUP:(\S) OWN:(\S) t=(\d+)")
RXTXT = re.compile(r"\[LOG\] \d{3} ([:!@]) x([0-9A-F]{8}) H(\w\w) S\d T\d M\d\d (\S+?)>(.*) FCS:([0-9A-F]{4}) .* DUP:(\S) OWN:(\S) t=(\d+)")
STAT = re.compile(r"\[LOG\] STAT util=\d+ rx=\d+ tx=(\d+) .* up=(\d+)")
NEED = re.compile(r"\[NBR\]\|NEED\|(\d+)\|([0-9A-F]{8})\|(\w)\|(\w)\|([0-9A-F]+)\|([0-9A-F]+)\|(-?\d+)\|")
CANCEL = re.compile(r"\[NBR\]\|(CANCEL\??)\|(\d+)\|([0-9A-F]{8})\|(\w)\|([^|]+)\|")
REFUSE = re.compile(r"\[NBR\]\|REFUSE\|(\d+)\|([0-9A-F]{8})\|")
ROW = re.compile(r"\[NBR\]\|ROW\|(\d+)\|(\d+)\|([^|]+)\|")
SNAP = re.compile(r"\[NBR\]\|SNAP\|(\d+)\|")
ME = re.compile(r"\[NBR\]\|ME\|\d+\|([^|]+)\|")
DEDUPADD = re.compile(r"RX_DEDUP_ADD msg_id=")
RLYQ = re.compile(r"\[LOG\] RLY x([0-9A-F]{8}) (\S) H(\w\w) q=(\w+)")


def ts_of(line: str) -> float | None:
    m = TS.match(line)
    if not m:
        return None
    return datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp()


def parse(path: Path) -> dict:
    d: dict = {"tx": [], "rx": [], "stat_tx_ms": 0, "stat_up_first": None, "need": [], "cancel": [],
               "refuse": 0, "snaps": [], "me": {}, "dedup_add": 0, "first_ts": None, "last_ts": None,
               "rly": {}, "rptx": 0}
    pending_ack_tx: dict | None = None
    cur_snap: dict | None = None
    with path.open("rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace").rstrip("\n")
            t = ts_of(line)
            if t is not None:
                if d["first_ts"] is None:
                    d["first_ts"] = t
                d["last_ts"] = t
            if "RPTTX" in line:
                d["rptx"] += 1
            m = TX.search(line)
            if m:
                e = {"ts": t, "id": m.group(1), "typ": m.group(2), "h": m.group(3), "prio": int(m.group(4)),
                     "src": m.group(5), "wait": int(m.group(6)), "len": int(m.group(7)), "t": int(m.group(8)),
                     "acked": None}
                d["tx"].append(e)
                pending_ack_tx = e if e["typ"] == "A" else None
                continue
            if pending_ack_tx is not None and "TX_FRAME len=12 hex=" in line:
                hx = line.split("hex=")[1].strip()
                if len(hx) >= 24 and hx[:2] == "41":
                    rid = "".join(reversed([hx[2 + 2 * i:4 + 2 * i] for i in range(4)]))
                    if rid == pending_ack_tx["id"]:
                        pending_ack_tx["acked"] = "".join(reversed([hx[12 + 2 * i:14 + 2 * i] for i in range(4)]))
                        pending_ack_tx["gwbyte"] = hx[20:22]
                pending_ack_tx = None
                continue
            if pending_ack_tx is not None and "TX-Lo" in line:
                m = TXLORA.search(line)
                if m and m.group(1) == pending_ack_tx["id"]:
                    pending_ack_tx["acked"] = m.group(3)
                    pending_ack_tx["gwbyte"] = m.group(4)
                pending_ack_tx = None
                continue
            if "[LOG] " in line and " RSSI:" in line:
                m = RXACK.search(line)
                if m:
                    d["rx"].append({"ts": t, "id": m.group(1), "typ": "A", "h": m.group(2), "acked": m.group(3),
                                    "gwb": m.group(4), "dup": m.group(6), "own": m.group(7), "t": int(m.group(8))})
                    continue
                m = RXTXT.search(line)
                if m and m.group(6) != "0000":
                    path_s = m.group(4)
                    toks = path_s.split(",")
                    d["rx"].append({"ts": t, "id": m.group(2), "typ": m.group(1), "h": m.group(3),
                                    "src": toks[0], "last": toks[-1], "ntok": len(toks),
                                    "ping": m.group(1) == ":" and toks[0] == "OE3XWJ-12" and m.group(5).startswith("2025:{ping}"),
                                    "dup": m.group(7), "own": m.group(8), "t": int(m.group(9))})
                continue
            m = STAT.search(line)
            if m:
                d["stat_tx_ms"] += int(m.group(1))
                if d["stat_up_first"] is None:
                    d["stat_up_first"] = [t, int(m.group(2))]
                continue
            if "[NBR]|" in line:
                m = NEED.search(line)
                if m:
                    d["need"].append({"ts": t, "min": int(m.group(1)), "id": m.group(2), "typ": m.group(3),
                                      "case": m.group(4), "need": m.group(5), "alone": m.group(6),
                                      "slot": int(m.group(7))})
                    continue
                m = CANCEL.search(line)
                if m:
                    d["cancel"].append({"kind": m.group(1), "id": m.group(3), "typ": m.group(4), "relayer": m.group(5)})
                    continue
                if REFUSE.search(line):
                    d["refuse"] += 1
                    continue
                m = SNAP.search(line)
                if m:
                    cur_snap = {"ts": t, "rows": {}}
                    d["snaps"].append(cur_snap)
                    continue
                m = ROW.search(line)
                if m and cur_snap is not None:
                    cur_snap["rows"][int(m.group(2))] = m.group(3)
                    continue
                m = ME.search(line)
                if m:
                    d["me"][m.group(1)] = d["me"].get(m.group(1), 0) + 1
                continue
            if DEDUPADD.search(line):
                d["dedup_add"] += 1
                continue
            m = RLYQ.search(line)
            if m:
                d["rly"][m.group(4)] = d["rly"].get(m.group(4), 0) + 1
    return d


def main() -> None:
    norm = Path(sys.argv[1])
    out = {n: parse(norm / f"{n}.log") for n in NODES}
    Path(sys.argv[2]).write_text(json.dumps(out))


if __name__ == "__main__":
    main()
