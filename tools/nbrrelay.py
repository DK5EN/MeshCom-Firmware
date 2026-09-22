#!/usr/bin/env python3
"""Relay-Baseline aus den --setlog-Zeilen eines Mitschnitts (docs/nbr-wichtigkeit-konzept.md, 2.4/8).

Liest die [LOG]-RX-Zeile (mit Pfad, DUP:d/n, OWN:e/-, t=ms), die RLY-Entscheidung und die
TX-Zeile (prio, src, wait, q) und liefert:

  - Empfang und Duplikatanteil je Typ, Relay-Entscheidungen je Typ und Grund
  - eigene Relays je Typ mit Wartezeit im Ring
  - je eigenem Relay: gab es eine FREMDE Wiederholung derselben msg_id vor bzw. nach der
    eigenen TX (first-order "haette abbrechen koennen"), fremde Relayer je Frame
  - je fremdem Relayer: Verzoegerung seiner Kopie gegen die erste gehoerte Kopie
  - eigene Wartezeit (Ringtiefe 1) gegen die Zahl der waehrend des Wartens gehoerten Frames
    (CSMA-Re-Arm, lora_functions.cpp:662/1746)
  - Link-Asymmetrie aus den [NBR]-Zeilen, falls vorhanden

    python3 tools/nbrrelay.py --since 2026-09-21 --own DK5EN-98 log1 [log2 ...]

Eine fremde Wiederholung ist ein Empfang derselben msg_id, dessen Pfad >= 2 Token hat (nicht der
Absender-Retry) und dessen letzter Hop nicht das eigene Rufzeichen ist.
"""
from __future__ import annotations

import argparse
import bisect
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime

NAMES = {":": "TEXT", "!": "POS", "@": "HEY"}
RE_RX = re.compile(r"\[LOG\] (\d{3}) ([:!@]) x([0-9A-F]{8}) H(\d\d) S\d T\d M\d\d (\S+?)>(\S*?)(?:[:!@])?.*?DUP:(\w) OWN:(\S+) t=(\d+)")
RE_TX = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) ([:!@]) H(\d\d) prio=(\d) src=(\w) wait=(\d+) q=(\d+) cad=(\d+) len=(\d+) t=(\d+)")
RE_RLY = re.compile(r"\[LOG\] RLY x([0-9A-F]{8}) ([:!@]) H(\d\d) q=(\w+) prio=(-?\d+) slot=(-?\d+)")


def pct(a: int, b: int) -> str:
    return f"{100.0 * a / b:.0f}%" if b else "-"


def quant(v: list[int], q: float) -> int:
    return v[min(len(v) - 1, int(len(v) * q))]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--since", metavar="YYYY-MM-DD[THH:MM]")
    ap.add_argument("--own", help="eigenes Rufzeichen (Default: aus einer [NBR]|SNAP-Zeile)")
    args = ap.parse_args(argv)

    since = None
    if args.since:
        fmt = "%Y-%m-%dT%H:%M" if "T" in args.since else "%Y-%m-%d"
        since = datetime.strptime(args.since, fmt)

    own = args.own
    rx: dict[str, list] = defaultdict(list)
    rx_t: list[int] = []
    tx: list[tuple] = []
    rly: Counter = Counter()
    n_rx: Counter = Counter()
    n_dup: Counter = Counter()
    heard_me: set[str] = set()
    i_heard: set[str] = set()

    for fn in args.files:
        with open(fn, encoding="utf-8", errors="replace") as f:
            for line in f:
                try:
                    host = datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f")
                except ValueError:
                    continue
                if since and host < since:
                    continue
                i = line.find("[NBR]|")
                if i >= 0:
                    f_ = line[i + 6 :].rstrip("\n").split("|")
                    if f_[0] == "SNAP" and own is None:
                        own = f_[2]
                    elif f_[0] == "EDGE" and own and f_[2] == own:
                        heard_me.add(f_[3])
                    elif f_[0] == "ME":
                        i_heard.add(f_[2])
                    continue
                m = RE_RX.search(line)
                if m:
                    ln, typ, mid, hop, path, dest, dup, ownf, t = m.groups()
                    rx[mid].append((int(t), typ, path.split(",")))
                    rx_t.append(int(t))
                    n_rx[typ] += 1
                    if dup == "d":
                        n_dup[typ] += 1
                    continue
                m = RE_TX.search(line)
                if m:
                    mid, typ, hop, prio, src, wait, q, cad, ln, t = m.groups()
                    tx.append((int(t), mid, typ, int(prio), src, int(wait), int(q)))
                    continue
                m = RE_RLY.search(line)
                if m:
                    rly[(m.group(2), m.group(4))] += 1

    if own is None:
        print("eigenes Rufzeichen unbekannt: --own angeben", file=sys.stderr)
        return 2
    rx_t.sort()
    span_h = (rx_t[-1] - rx_t[0]) / 3600000 if len(rx_t) > 1 else 0
    # Praeexistente Randbedingung, beim Verifizieren der neuen Stage-2-Fixture
    # aufgefallen: ein Mitschnitt ganz ohne [LOG]-RX-Zeile (z. B. ein reiner
    # [NBR]-Auszug) liess rx_t leer und rx_t[-1] mit IndexError crashen.
    mean_gap_s = (rx_t[-1] - rx_t[0]) / max(1, len(rx_t)) / 1000 if rx_t else 0.0

    print(f"== Empfang je Typ (eigen: {own}, {len(rx_t)} Frames, {span_h:.1f} h, mittlerer Abstand {mean_gap_s:.1f} s) ==")
    for typ in ":!@":
        print(f"  {NAMES[typ]:5s} rx={n_rx[typ]:5d} dup={n_dup[typ]:5d} ({pct(n_dup[typ], n_rx[typ])})")

    reasons = sorted({r for (_, r) in rly})
    print("\n== RLY-Entscheidungen je Typ und Grund ==")
    print("  Typ   " + " ".join(f"{r:>9s}" for r in reasons))
    for typ in ":!@":
        print(f"  {NAMES[typ]:5s} " + " ".join(f"{rly[(typ, r)]:9d}" for r in reasons))

    print("\n== eigene Relays (TX src=r) je Typ, Wartezeit im Ring ==")
    for typ in ":!@":
        w = sorted(t[5] for t in tx if t[4] == "r" and t[2] == typ)
        if w:
            print(f"  {NAMES[typ]:5s} n={len(w):4d} ({len(w)/max(span_h,1e-9):.1f}/h)  wait ms: median {quant(w,0.5)}, p90 {quant(w,0.9)}, max {w[-1]}")
    print(f"  eigene Frames: { {NAMES[k]: v for k, v in Counter(t[2] for t in tx if t[4]=='o').items()} }, vom Server: {sum(1 for t in tx if t[4]=='g')}")

    print("\n== fremde Wiederholung vor/nach der eigenen Relay-TX ==")
    total: Counter = Counter(); before: Counter = Counter(); after: Counter = Counter()
    copies: dict[str, list[int]] = defaultdict(list)
    delay: dict[str, list[int]] = defaultdict(list)
    r_bef: Counter = Counter(); r_aft: Counter = Counter()
    own_delay: list[int] = []
    for t_tx, mid, typ, prio, src, wait, q in tx:
        if src != "r":
            continue
        total[typ] += 1
        evs = rx.get(mid, [])
        if not evs:
            continue
        first = min(e[0] for e in evs)
        own_delay.append(t_tx - first)
        foreign = [e for e in evs if e[0] > first and len(e[2]) >= 2 and e[2][-1] != own]
        if any(e[0] <= t_tx for e in foreign):
            before[typ] += 1
        if any(e[0] > t_tx for e in foreign):
            after[typ] += 1
        copies[typ].append(len({e[2][-1] for e in foreign}))
        for e in foreign:
            r = e[2][-1]
            delay[r].append(e[0] - first)
            if e[0] <= t_tx:
                r_bef[r] += 1
            else:
                r_aft[r] += 1
    for typ in ":!@":
        if total[typ]:
            c = sorted(copies[typ])
            print(f"  {NAMES[typ]:5s} relays={total[typ]:4d}  fremde Kopie vorher: {before[typ]:4d} ({pct(before[typ], total[typ])})  nachher: {after[typ]:4d} ({pct(after[typ], total[typ])})  fremde Relayer je Frame: Median {quant(c,0.5)} max {c[-1]}")

    print("\n== je fremdem Relayer: Verzoegerung seiner Kopie gegen die erste Kopie ==")
    if own_delay:
        own_delay.sort()
        print(f"  {own:10s} eigene TX: Median {quant(own_delay,0.5):6d} ms  p90 {quant(own_delay,0.9):6d} ms  (n={len(own_delay)})")
    for r in sorted(delay, key=lambda k: -len(delay[k])):
        d = sorted(delay[r])
        print(f"  {r:10s} Kopien={len(d):4d} vor eigener TX={r_bef[r]:4d} nach={r_aft[r]:4d}  Median {quant(d,0.5):6d} ms  p90 {quant(d,0.9):6d} ms")

    print("\n== Wartezeit eigener Relays (Ringtiefe 1) gegen waehrend des Wartens gehoerte Frames ==")
    buckets: dict[int, list[int]] = defaultdict(list)
    for t_tx, mid, typ, prio, src, wait, q in tx:
        if src != "r" or q != 1:
            continue
        n = bisect.bisect_right(rx_t, t_tx) - bisect.bisect_left(rx_t, t_tx - wait)
        buckets[min(n, 5)].append(wait)
    for k in sorted(buckets):
        w = sorted(buckets[k])
        print(f"  gehoert={k}{'+' if k == 5 else ' '}: n={len(w):4d}  Median {quant(w,0.5):6d} ms  p90 {quant(w,0.9):6d} ms")

    relayed = {t[1] for t in tx if t[4] == "r"}
    c_not: Counter = Counter()
    for mid, evs in rx.items():
        if mid in relayed:
            continue
        c_not[len({e[2][-1] for e in evs if len(e[2]) >= 2 and e[2][-1] != own})] += 1
    print(f"\nnicht selbst wiederholte Frames, fremde Relayer je Frame: {sorted(c_not.items())}")
    if heard_me or i_heard:
        print(f"\nAsymmetrie: hoerten mich {sorted(heard_me)}; direkt gehoert {sorted(i_heard)}; nur-hoeren-mich: {sorted(heard_me - i_heard)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
