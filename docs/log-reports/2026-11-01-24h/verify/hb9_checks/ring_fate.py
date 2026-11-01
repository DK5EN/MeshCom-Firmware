#!/usr/bin/env python3
"""Claims 2/3: slot fate per ring write (aired / superseded / vanished), foreign-copy test, hourly cancel
profile on HB9VQQ-4, and the pair redundancy (ids aired by both nodes).

Usage: python3 -I ring_fate.py <norm-dir>
Join is exact on slot + msg_id. A write is 'vanished' when its slot is written again (or the capture ends
> 400 s later) without a RING_TX_READ / RING_SUPERSEDE for it.
"""
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

TS = re.compile(r"^(\S+ \S+)")
W = re.compile(r"RING_WRITE slot=(\d+) type=(\w+) status=(\w+) len=(\d+) msg_id=([0-9A-F]{8}) .*src=(\S+)")
R = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) type=(\w+) status=\w+ len=(\d+) msg_id=([0-9A-F]{8}) .*lat=(\d+)ms")
S = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=\d old_id=([0-9A-F]{8})")
MH = re.compile(r"MH-LoRa\s+\d+ . x([0-9A-F]{8}) H\d\d \S+ \S+ \S+ ([^>]+)>")
OWN = {"hb9vqq4.log": "HB9VQQ-4", "hb9vqq5.log": "HB9VQQ-5"}


def t_of(line: str) -> float:
    return datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f").timestamp()


def toa(n: int) -> float:
    # SF11 BW250 CR4/6, preamble 8, explicit header, CRC, LDRO off
    import math
    sf, bw, cr = 11, 250000.0, 2
    ts = (2 ** sf) / bw
    pl = 8 + max(math.ceil((8 * n - 4 * sf + 28 + 16) / (4 * sf)) * (cr + 4), 0)
    return (8 + 4.25) * ts + pl * ts


def fate(p: Path) -> tuple[dict[tuple[str, float], str], dict[str, list[float]], dict[str, int]]:
    own = OWN[p.name]
    occ: dict[str, dict] = {}
    done: list[dict] = []
    rx: dict[str, list[tuple[float, str]]] = defaultdict(list)
    txt: dict[str, list[float]] = defaultdict(list)
    txlen: dict[str, int] = {}
    pend_sup: set[tuple[str, str]] = set()
    lines = p.open(errors="replace").read().splitlines()
    end_t = t_of(lines[-1])
    for i, line in enumerate(lines):
        if "MH-LoRa" in line:
            m = MH.search(line)
            if m:
                rx[m.group(1)].append((t_of(line), m.group(2)))
            continue
        if "RING_" not in line:
            continue
        m = W.search(line)
        if m:
            slot, typ, st, ln, mid, src = m.groups()
            t = t_of(line)
            old = occ.get(slot)
            if old is not None:
                sup = any(S.search(x) and S.search(x).groups() == (slot, old["id"]) for x in lines[i + 1:i + 4])
                old["fate"] = "superseded" if sup else "vanished"
                old["end"] = t
                done.append(old)
            occ[slot] = {"id": mid, "t": t, "src": src, "type": typ, "len": int(ln), "slot": slot}
            continue
        m = R.search(line)
        if m:
            slot, prio, typ, ln, mid, lat = m.groups()
            t = t_of(line)
            txt[mid].append(t)
            txlen[mid] = int(ln)
            o = occ.get(slot)
            if o is not None and o["id"] == mid:
                o["fate"] = "aired"
                o["end"] = t
                o["prio"] = int(prio)
                done.append(o)
                del occ[slot]
            continue
    for o in occ.values():
        o["fate"] = "vanished" if end_t - o["t"] > 400 else "pending"
        o["end"] = end_t
        done.append(o)
    c = Counter(o["fate"] for o in done)
    relay = [o for o in done if o["src"] == "rx_relay"]
    van = [o for o in done if o["fate"] == "vanished"]
    def foreign(o: dict) -> float | None:
        for t, path_s in rx.get(o["id"], []):
            toks = path_s.split(",")
            if o["t"] < t <= o["end"] and len(toks) >= 2 and toks[-1] != own:
                return t - o["t"]
        return None
    vf = [foreign(o) for o in van]
    aired_relay = [o for o in relay if o["fate"] == "aired"]
    af = [foreign(o) for o in aired_relay]
    print(f"== {p.name}: writes {len(done)} fates {dict(c)}")
    print(f"   relay writes {len(relay)}, relay fates {dict(Counter(o['fate'] for o in relay))}")
    print(f"   vanished by src {dict(Counter(o['src'] for o in van))} by type {dict(Counter(o['type'] for o in van))}")
    print(f"   vanished with foreign copy while queued: {sum(1 for x in vf if x is not None)} of {len(van)}")
    print(f"   aired relays with foreign copy while queued: {sum(1 for x in af if x is not None)} of {len(aired_relay)}")
    print(f"   vanished airtime (model) {sum(toa(o['len']) for o in van):.0f} s; aired airtime {sum(toa(n) for n in txlen.values()):.0f} s (distinct ids)")
    if p.name == "hb9vqq4.log":
        t0 = t_of(lines[0])
        h = Counter(int((o["t"] - t0) // 3600) for o in van)
        print("   vanished per capture hour n=0..23:", [h.get(k, 0) for k in range(24)])
        hf = Counter(int((o["t"] - t0) // 3600) for o, x in zip(aired_relay, af) if x is not None)
        print("   aired-with-foreign-copy per hour:", [hf.get(k, 0) for k in range(24)])
    srcmap = {(o["id"], round(o["end"], 3)): o["src"] for o in done if o["fate"] == "aired"}
    return srcmap, txt, txlen


def main() -> None:
    d = Path(sys.argv[1])
    f4, t4, l4 = fate(d / "hb9vqq4.log")
    f5, t5, l5 = fate(d / "hb9vqq5.log")
    # one-to-one pairs: each TX of -4 with the nearest unused TX of -5 of the same id within 30 min
    # (ids reused by their origin hours later give separate pairs)
    both = []
    for mid, ts4 in t4.items():
        free = list(t5.get(mid, []))
        for a in ts4:
            near = [b for b in free if abs(b - a) <= 1800]
            if near:
                b = min(near, key=lambda x: abs(x - a))
                free.remove(b)
                both.append((mid, a, b))
    print(f"   pairs {len(both)}, distinct ids in pairs {len({m for m, _, _ in both})}")
    ids_any = set(t4) | set(t5)
    later_air = sum(toa(l4[m] if a > b else l5[m]) for m, a, b in both)
    pair_air = sum(toa(n) * len(t4[m]) for m, n in l4.items()) + sum(toa(n) * len(t5[m]) for m, n in l5.items())
    print(f"== pair: distinct ids aired by >=1 node {len(ids_any)}; paired TX (see above)")
    cls = Counter((f4.get((m, round(a, 3)), "?"), f5.get((m, round(b, 3)), "?")) for m, a, b in both)
    print(f"   src classes (-4 src, -5 src): {dict(cls.most_common(8))}")
    print(f"   later-copy airtime {later_air:.0f} s of pair TX airtime {pair_air:.0f} s = {100*later_air/pair_air:.1f} %")


if __name__ == "__main__":
    main()
