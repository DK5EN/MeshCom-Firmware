#!/usr/bin/env python3
"""Geraetesicht der Nachbarschaftsmatrix je Schnappschuss (docs/nbr-wichtigkeit-konzept.md, 2.1).

tools/nbrlog.py rechnet die Union ueber den ganzen Lauf ("DL2JA-2 hoert 40"). Dieses Skript
rekonstruiert aus denselben [NBR]-Zeilen, was das GERAET zu jedem SNAP tatsaechlich in seinen
NBR_MAX_ROWS Zeilen hatte: nur Zeilen, die im Schnappschuss stehen, nur Kanten, die innerhalb
NBR_WINDOW_MIN (720 min) getroffen und seither nicht durch eine Verdraengung genullt wurden.
Daraus je direktem Nachbarn #N (gehoerte Knoten) und #X (davon von niemand anderem in meiner
Hoerweite gehoert) -- die Rechnung aus nbrRowMeshNeed(), als Zahl. Dazu der <meshneed>-Verlauf
aus den ROW-Zeilen und die Verdraengungen von Direktzeilen.

    python3 tools/nbrsnap.py --since 2026-09-21 log1 [log2 ...]

Zeilenformat: docs/nbr-logformat.md. Das eigene Rufzeichen kommt aus der SNAP-Zeile, sonst --own.
"""
from __future__ import annotations

import argparse
import sys
from collections import Counter, OrderedDict, defaultdict
from datetime import datetime

WINDOW = 720


def parse_host(line: str) -> datetime | None:
    try:
        return datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f")
    except ValueError:
        return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", help="Mitschnitt-Dateien mit [NBR]-Zeilen")
    ap.add_argument("--since", metavar="YYYY-MM-DD[THH:MM]", help="nur Zeilen ab diesem Zeitpunkt")
    ap.add_argument("--own", help="eigenes Rufzeichen, falls keine SNAP-Zeile im Log steht")
    ap.add_argument("--watch", nargs="*", default=None, help="Nachbarn fuer die Zeitreihe (Default: alle direkten)")
    args = ap.parse_args(argv)

    since = None
    if args.since:
        fmt = "%Y-%m-%dT%H:%M" if "T" in args.since else "%Y-%m-%d"
        since = datetime.strptime(args.since, fmt)

    edges: dict[tuple[str, str], int] = {}  # (frm, to) -> last_up, "to hat frm gehoert"
    me_hits: dict[str, int] = {}  # frm -> last_up, cell[frm][0], wird bei EVICT genullt
    me_calls: set[str] = set()  # alle je direkt gehoerten Rufzeichen (ME-Zeilen), nie genullt
    evicts: list[tuple[int, str, str]] = []
    snaps: list[dict] = []
    union_heard: dict[str, set[str]] = defaultdict(set)
    meshneed_seq: dict[str, list[str]] = defaultdict(list)
    own = args.own
    cur: dict | None = None
    n_lines = 0

    def zero_call(c: str) -> None:
        for k in [k for k in edges if c in k]:
            del edges[k]
        me_hits.pop(c, None)

    for fn in args.files:
        with open(fn, encoding="utf-8", errors="replace") as f:
            for line in f:
                i = line.find("[NBR]|")
                if i < 0:
                    continue
                host = parse_host(line)
                if host is None or (since and host < since):
                    continue
                n_lines += 1
                f_ = line[i + 6 :].rstrip("\n").split("|")
                kind, up = f_[0], int(f_[1])
                if kind == "EDGE":
                    frm, to = f_[2], f_[3]
                    edges[(frm, to)] = up
                    if frm != to:
                        union_heard[to].add(frm)
                elif kind == "ME":
                    me_hits[f_[2]] = up
                    me_calls.add(f_[2])
                elif kind == "EVICT":
                    evicts.append((up, f_[3], f_[4]))
                    zero_call(f_[3])
                elif kind == "SNAP":
                    own = f_[2]
                    cur = {"up": up, "host": host, "rows": OrderedDict(), "nrows": int(f_[3]), "max": int(f_[4])}
                elif kind == "ROW" and cur is not None:
                    call, meshneed = f_[3], f_[8]
                    cur["rows"][call] = meshneed
                    if meshneed != "NA":
                        meshneed_seq[call].append(meshneed)
                elif kind == "ENDSNAP" and cur is not None:
                    rows = set(cur["rows"])
                    up = cur["up"]

                    def fresh(t: int) -> bool:
                        return (up - t) % 65536 < WINDOW

                    direct = {c for c, t in me_hits.items() if c in rows and fresh(t) and c != own}
                    res = {}
                    for x in sorted(direct):
                        hx = {frm for (frm, to), t in edges.items() if to == x and frm in rows and frm not in (x, own) and fresh(t)}
                        excl = [
                            f2
                            for f2 in hx
                            if f2 not in direct
                            and not any((f2, m) in edges and fresh(edges[(f2, m)]) for m in direct if m != x)
                        ]
                        res[x] = (len(hx), len(excl))
                    cur["res"] = res
                    snaps.append(cur)
                    cur = None

    if own is None:
        print("kein eigenes Rufzeichen: keine SNAP-Zeile im Log, --own angeben", file=sys.stderr)
        return 2

    # Direkte Nachbarn ausschliesslich aus ME-Zeilen (wie tools/nbrlog.py). EDGE-Zeilen mit
    # Ziel == eigenes Rufzeichen taugen dafuer nicht: ein Gateway setzt Serverframes mit
    # "<Absender>,<ich>" auf LoRa, und das Echo dieser Frames liefert EDGE-Kanten "ich habe
    # <Absender> gehoert", obwohl nie ein Funkempfang stattfand.
    direct_all = sorted((me_calls | set(args.watch or [])) - {own})

    print(f"[NBR]-Zeilen: {n_lines}, Schnappschuesse: {len(snaps)}, Verdraengungen: {len(evicts)}, eigen: {own}")
    print("\n== Union ueber den Lauf (Sicht von tools/nbrlog.py) ==")
    for x in direct_all:
        hx = union_heard.get(x, set()) - {own}
        others: set[str] = set()
        for y in direct_all:
            if y != x:
                others |= union_heard.get(y, set())
        excl = {f for f in hx if f not in me_calls and f not in others}
        print(f"  {x:10s} hoert {len(hx):2d}  exklusiv {len(excl):2d}")

    print("\n== <meshneed> je direktem Nachbarn (Firmware, ROW-Zeilen) ==")
    for c in direct_all:
        s = meshneed_seq.get(c, [])
        print(f"  {c:10s} n={len(s):2d} {dict(Counter(s))}")

    watch = [c for c in direct_all if any(c in s["res"] for s in snaps)]
    print("\n== Geraetesicht je Schnappschuss: hoert/exklusiv <meshneed> ==")
    print("Zeit           up  Zeilen  " + "  ".join(f"{c:>14s}" for c in watch))
    for s in snaps:
        cells = []
        for c in watch:
            if c in s["res"]:
                n, e = s["res"][c]
                cells.append(f"{n:2d}/{e:2d} {s['rows'].get(c, '-'):4s}")
            else:
                cells.append("-")
        print(f"{s['host'].strftime('%m-%d %H:%M')}  {s['up']:4d}  {s['nrows']:2d}/{s['max']:2d}  " + "  ".join(f"{x:>14s}" for x in cells))

    print("\n== Zusammenfassung Geraetesicht (exklusiv: min / Median / max) ==")
    for c in watch:
        v = sorted(s["res"][c][1] for s in snaps if c in s["res"])
        if v:
            print(f"  {c:10s} {v[0]:2d} / {v[len(v)//2]:2d} / {v[-1]:2d}  ueber {len(v)} Schnappschuesse")

    de = [(up, old) for up, old, new in evicts if old in direct_all]
    print(f"\nVerdraengungen von Direktzeilen: {len(de)} von {len(evicts)}: {de}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
