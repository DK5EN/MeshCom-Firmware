#!/usr/bin/env python3
"""Pre-CAD share of heard frame events per node, independent of the analysis scripts.
Event = a decoded non-ACK [LOG] reception line (has 'RSSI:'), FCS:0000 excluded. Originator = first
path call. Own call and FW major 0 (server time signal) excluded. Per originator the most frequent
FW value whose letter is not '#'. Pre-CAD = major < 35 or 4.35a..o; 4.35p+ and 4.40* are CAD-capable
(docs: 'vor 4.35p kein CAD'). 4.35# (letter unknown) reported separately.
Usage: python3 -I precad_share.py <norm-dir>
"""
from __future__ import annotations
import collections, re, sys
from pathlib import Path

LINE = re.compile(r"\[LOG\] (\d{3}) (\S) x[0-9A-F]{8} H\d\d S\d T\d M\d+ (\S+?)>.* FCS:(\w{4}) FW:(\d+):(\S) LH:\w+ RSSI:")
OWN = {"oe3xhl": "OE3XHL-12", "oe3xia": "OE3XIA-12", "oe3xir": "OE3XIR-12", "oe3xoc": "OE3XOC-12", "oe3xwj": "OE3XWJ-12", "db0ais": "DB0AIS-12"}


def cls(major: int, letter: str) -> str:
    if major < 35:
        return "pre"
    if major == 35:
        if letter == "#":
            return "unk"
        return "pre" if letter.lower() <= "o" else "post"
    return "post"


def main() -> None:
    norm = Path(sys.argv[1])
    for n, own in OWN.items():
        ev = collections.Counter(); fw: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
        with open(norm / f"{n}.log", encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                if "[LOG]" not in ln or "RSSI:" not in ln:
                    continue
                m = LINE.search(ln)
                if not m or m.group(4) == "0000":
                    continue
                o = m.group(3).split(",")[0]
                if o == own or int(m.group(5)) == 0:
                    continue
                ev[o] += 1; fw[o][(int(m.group(5)), m.group(6))] += 1
        c: dict[str, str] = {}
        for o, cnt in fw.items():
            good = [k for k, _ in cnt.most_common() if k[1] != "#"]
            k = good[0] if good else cnt.most_common(1)[0][0]
            c[o] = cls(*k)
        tot = sum(ev.values()); pre = sum(v for o, v in ev.items() if c[o] == "pre")
        npre = sum(1 for o in ev if c[o] == "pre"); unk = sum(1 for o in ev if c[o] == "unk")
        print(f"{n}: originators {len(ev)}, pre-CAD {npre} ({100*npre/len(ev):.1f}%), unknown 4.35# {unk}; events {tot}, pre-CAD {pre} = {100*pre/tot:.1f}%")


if __name__ == "__main__":
    main()
