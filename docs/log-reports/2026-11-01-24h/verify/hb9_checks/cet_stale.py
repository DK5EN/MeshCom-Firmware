#!/usr/bin/env python3
"""Claim 1: stale {CET} frames received / transmitted by HB9VQQ-4 and HB9VQQ-5.

Usage: python3 -I cet_stale.py <norm-dir>
Age = (logger time - 3600 s) - payload time ({CET} payload is UTC, logger is CET = UTC+1).
Counts frames (one MH-LoRa line per decoded frame; TX-LoRa line per own TX), not [LOG] duplicates.
"""
import re
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

FR = re.compile(r"^(\S+ \S+)\s+\S+ (MH-LoRa|TX-LoRa|RX-LoRa2)\s+\d+ : x([0-9A-F]{8}) (H\d\d) \S+ \S+ \S+ ([^>]+)>\*:\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)")
CETTX = re.compile(r"^(\S+ \S+)\s+\[CET\];tx;age_s;(-?\d+)")
CETRX = re.compile(r"^(\S+ \S+)\s+\[CET\];rx;src;(\S+?);age_s;(-?\d+);trusted;\d;hop;(\d+)")
RLY = re.compile(r"\[LOG\] RLY x([0-9A-F]{8}) : (H\d\d) q=(\S+)")
RW = re.compile(r"RING_WRITE .*msg_id=([0-9A-F]{8}) .*src=(\S+)")
RTR = re.compile(r"^(\S+ \S+)\s+\[MC-DBG\] RING_TX_READ .*msg_id=([0-9A-F]{8}) .*lat=(\d+)ms")


def ts(s: str) -> datetime:
    return datetime.strptime(s[:19], "%Y-%m-%d %H:%M:%S")


def age(logger: str, payload: str) -> float:
    return (ts(logger) - ts(payload)).total_seconds() - 3600.0


def run(path: Path, own: str) -> None:
    rx: list[tuple[str, str, str, float, str]] = []
    tx: list[tuple[str, str, float]] = []
    cettx_max = -999
    cettx_n = 0
    cetrx_old: list[tuple[str, str, int, int]] = []
    rly: dict[str, str] = {}
    writes: Counter[str] = Counter()
    write_src: dict[str, set[str]] = {}
    reads: dict[str, list[float]] = {}
    new_ids: set[str] = set()
    with path.open(errors="replace") as fh:
        for line in fh:
            m = FR.match(line)
            if m:
                t, kind, mid, h, path_s, pay = m.groups()
                a = age(t, pay)
                if kind == "MH-LoRa":
                    rx.append((t, mid, path_s, a, h))
                elif kind == "TX-LoRa":
                    tx.append((t, mid, a))
                else:
                    new_ids.add(mid + "@" + t[:16])
                continue
            m = CETTX.match(line)
            if m:
                cettx_n += 1
                cettx_max = max(cettx_max, int(m.group(2)))
                continue
            m = CETRX.match(line)
            if m and int(m.group(3)) > 150:
                cetrx_old.append((m.group(1), m.group(2), int(m.group(3)), int(m.group(4))))
                continue
            m = RLY.search(line)
            if m:
                rly[m.group(1)] = m.group(3)
                continue
            m = RW.search(line)
            if m:
                writes[m.group(1)] += 1
                write_src.setdefault(m.group(1), set()).add(m.group(2))
                continue
            m = RTR.match(line)
            if m:
                reads.setdefault(m.group(2), []).append(int(m.group(3)) / 1000)
    stale = [r for r in rx if r[3] > 150]
    print(f"== {path.name} ({own})")
    print(f"  {{CET}} frames decoded (MH-LoRa): {len(rx)}; older than 150 s: {len(stale)}")
    lh = Counter(r[2].split(",")[-1] for r in stale)
    print(f"  stale by last hop: {dict(lh)}")
    via_hno = [r for r in stale if "HB9HNO-49" in r[2].split(",")]
    print(f"  stale with HB9HNO-49 in path: {len(via_hno)}; distinct ids {len({r[1] for r in via_hno})}")
    if stale:
        ages = sorted(r[3] for r in stale)
        print(f"  stale age min/median/max: {ages[0]:.0f} / {ages[len(ages)//2]:.0f} / {ages[-1]:.0f} s")
    other = [r for r in stale if r[2].split(",")[-1] != "HB9HNO-49"]
    for r in other:
        print(f"  stale, last hop not HB9HNO-49: {r[0]} x{r[1]} {r[4]} path={r[2]} age={r[3]:.0f}")
    # stale frames that were accepted as NEW (RX-LoRa2 in same minute with same id)
    acc = [r for r in stale if (r[1] + "@" + r[0][:16]) in new_ids]
    print(f"  stale frames accepted as NEW (RX-LoRa2): {len(acc)}")
    for r in acc:
        print(f"    {r[0]} x{r[1]} {r[4]} path={r[2]} age={r[3]:.0f} rly={rly.get(r[1], '-')} ring_writes={writes.get(r[1], 0)} src={sorted(write_src.get(r[1], []))}")
    print(f"  [CET];rx lines with age_s > 150: {len(cetrx_old)} {cetrx_old}")
    print(f"  own {{CET}} TX (TX-LoRa): {len(tx)}; max age {max(a for _, _, a in tx):.0f} s; older than 150 s: {sum(1 for _, _, a in tx if a > 150)}")
    print(f"  [CET];tx lines: {cettx_n}; max age_s {cettx_max}")
    stale_ids = {r[1] for r in stale}
    tx_stale_ids = [x for x in tx if x[1] in stale_ids]
    print(f"  own TX of an id that also arrived stale: {len(tx_stale_ids)}")
    for t, mid, a in tx_stale_ids:
        print(f"    TX {t} x{mid} age at own TX {a:.0f} s")


def main() -> None:
    d = Path(sys.argv[1])
    run(d / "hb9vqq4.log", "HB9VQQ-4")
    run(d / "hb9vqq5.log", "HB9VQQ-5")


if __name__ == "__main__":
    main()
