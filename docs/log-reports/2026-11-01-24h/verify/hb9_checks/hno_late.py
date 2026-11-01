#!/usr/bin/env python3
"""Claim 1 (second half): late HB9HNO-49 copies accepted as NEW and relayed, by frame type.

Usage: python3 -I hno_late.py <norm-dir>
A frame with last hop HB9HNO-49 is 'late' when the same msg_id was first decoded at this node
300..3600 s earlier (3600 s cap excludes HB9JAT id reuse). Accepted = RX-LoRa2 line for that id within 1 s.
Relayed = own TX-LoRa line for that id after the late reception (within 400 s).
Also reports frames whose path contains HB9HNO-49 followed by HB9VQQ-5 decoded at -4 (copies -5 relayed).
"""
import re
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

FR = re.compile(r"^(\S+ \S+)\s+\S+ (MH-LoRa|TX-LoRa|RX-LoRa2)\s+\d+ (.) x([0-9A-F]{8}) H\d\d \S+ \S+ \S+ ([^>]+)>(\S*?)([:!@])")


def ts(s: str) -> float:
    return datetime.strptime(s[:23], "%Y-%m-%d %H:%M:%S.%f").timestamp()


def kind(path_s: str, sep: str, body: str) -> str:
    return {"@": "HEY", "!": "pos", ":": "text"}[sep]


def load(p: Path) -> list[tuple[float, str, str, str, str]]:
    out = []
    with p.open(errors="replace") as fh:
        for line in fh:
            m = FR.match(line)
            if m:
                t, k, _c, mid, path_s, _d, sep = m.groups()
                out.append((ts(t), k, mid, path_s, sep))
    return out


def run(p: Path) -> None:
    ev = load(p)
    first: dict[str, float] = {}
    acc_t: dict[str, list[float]] = {}
    tx_t: dict[str, list[float]] = {}
    for t, k, mid, path_s, sep in ev:
        if k == "MH-LoRa":
            first.setdefault(mid, t)
        elif k == "RX-LoRa2":
            acc_t.setdefault(mid, []).append(t)
        else:
            tx_t.setdefault(mid, []).append(t)
    late = Counter()
    acc = Counter()
    rel = Counter()
    cet_rel = 0
    for t, k, mid, path_s, sep in ev:
        if k != "MH-LoRa" or path_s.split(",")[-1] != "HB9HNO-49":
            continue
        gap = t - first[mid]
        if not (300 <= gap <= 3600):
            continue
        ty = {"@": "HEY", "!": "pos", ":": "text"}[sep]
        late[ty] += 1
        if any(abs(a - t) < 1.0 for a in acc_t.get(mid, [])):
            acc[ty] += 1
            if any(0 < x - t < 400 for x in tx_t.get(mid, [])):
                rel[ty] += 1
    print(f"== {p.name}: late HB9HNO-49 copies by type {dict(late)} total {sum(late.values())}")
    print(f"   accepted as NEW {dict(acc)} total {sum(acc.values())}; relayed by this node {dict(rel)} total {sum(rel.values())}")


def via5(p4: Path) -> None:
    ev = load(p4)
    first: dict[str, float] = {}
    n = Counter()
    for t, k, mid, path_s, sep in ev:
        if k != "MH-LoRa":
            continue
        first.setdefault(mid, t)
        toks = path_s.split(",")
        if toks[-1] == "HB9VQQ-5" and "HB9HNO-49" in toks and toks.index("HB9HNO-49") == len(toks) - 2:
            gap = t - first[mid]
            n[({"@": "HEY", "!": "pos", ":": "text"}[sep], gap > 300)] += 1
    print(f"== at -4, frames with path ...,HB9HNO-49,HB9VQQ-5 (type, >300 s after first copy): {dict(n)}")


def cross(pa: Path, pb: Path) -> None:
    """HB9HNO-49 frames accepted as NEW at node A whose id node B decoded/sent 300..3600 s earlier."""
    ea, eb = load(pa), load(pb)
    first_b: dict[str, float] = {}
    for t, k, mid, path_s, sep in eb:
        if k in ("MH-LoRa", "TX-LoRa"):
            first_b.setdefault(mid, t)
    first_a: dict[str, float] = {}
    acc_t: dict[str, list[float]] = {}
    tx_t: dict[str, list[float]] = {}
    for t, k, mid, path_s, sep in ea:
        if k == "RX-LoRa2":
            acc_t.setdefault(mid, []).append(t)
        elif k == "TX-LoRa":
            tx_t.setdefault(mid, []).append(t)
    acc = Counter()
    rel = Counter()
    for t, k, mid, path_s, sep in ea:
        if k != "MH-LoRa":
            continue
        if mid in first_a:
            continue
        first_a[mid] = t
        if path_s.split(",")[-1] != "HB9HNO-49" or mid not in first_b:
            continue
        gap = t - first_b[mid]
        if not (300 <= gap <= 3600):
            continue
        if not any(abs(a - t) < 1.0 for a in acc_t.get(mid, [])):
            continue
        ty = {"@": "HEY", "!": "pos", ":": "text"}[sep]
        acc[ty] += 1
        if any(0 < x - t < 400 for x in tx_t.get(mid, [])):
            rel[ty] += 1
    print(f"== {pa.name}: first copy from HB9HNO-49, accepted NEW, id seen at {pb.name} 300-3600 s earlier: {dict(acc)} total {sum(acc.values())}; relayed {dict(rel)} total {sum(rel.values())}")


def main() -> None:
    d = Path(sys.argv[1])
    run(d / "hb9vqq4.log")
    run(d / "hb9vqq5.log")
    via5(d / "hb9vqq4.log")
    cross(d / "hb9vqq5.log", d / "hb9vqq4.log")
    cross(d / "hb9vqq4.log", d / "hb9vqq5.log")


if __name__ == "__main__":
    main()
