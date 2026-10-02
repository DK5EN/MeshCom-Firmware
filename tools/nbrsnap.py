#!/usr/bin/env python3
"""Geraetesicht der Nachbarschaftsmatrix je Schnappschuss (docs/nbr-wichtigkeit-konzept.md, 2.1).

tools/nbrlog.py rechnet die Union ueber den ganzen Lauf ("DL2JA-2 hoert 40"). Dieses Skript
rekonstruiert aus denselben [NBR]-Zeilen, was das GERAET zu jedem SNAP tatsaechlich in seinen
NBR_MAX_ROWS Zeilen hatte: nur Zeilen, die im Schnappschuss stehen, nur Kanten, die innerhalb
NBR_WINDOW_MIN (720 min) getroffen und seither nicht durch eine Verdraengung genullt wurden.
Daraus je direktem Nachbarn #N (gehoerte Knoten) und #X (davon von niemand anderem in meiner
Hoerweite gehoert) -- die Rechnung aus nbrRowMeshNeed(), als Zahl. Dazu der <meshneed>-Verlauf
aus den ROW-Zeilen und die Verdraengungen von Direktzeilen.

Die Deckung wird wie in der Firmware gerechnet (nbrICtx/nbrICovers in src/nbr_matrix.cpp): eine
Kante (x, y) deckt nur, wenn sie frisch ist UND ihr Zaehler mindestens NBR_SHARE_PCT (10) Prozent
des staerksten Zaehlers von x zu einem direkten Nachbarn erreicht; die Zaehler werden alle
NBR_CNT_HALVE_MIN (90) Minuten seit dem Boot zu (cnt + 1) >> 1 halbiert, das Frischefenster ist
NBR_WINDOW_MIN (720). Alle drei per Flag ueberschreibbar (--share-pct, --halve-min, --window-min).

    python3 tools/nbrsnap.py --since 2026-09-21 log1 [log2 ...]

Zeilenformat: docs/nbr-logformat.md. Das eigene Rufzeichen kommt aus der SNAP-Zeile, sonst --own.
"""
from __future__ import annotations

import argparse
import sys
from collections import Counter, OrderedDict, defaultdict
from datetime import datetime

#: Firmware-Vorgaben (src/nbr_matrix.h), per CLI ueberschreibbar.
WINDOW = 720
SHARE_PCT = 10
HALVE_MIN = 90


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
    ap.add_argument(
        "--share-pct", type=int, default=SHARE_PCT, metavar="N",
        help=f"Firmware NBR_SHARE_PCT: Kante deckt ab N %% des staerksten direkten Hoerers (Standard {SHARE_PCT}; 0 = jede frische Kante)",
    )
    ap.add_argument(
        "--halve-min", type=int, default=HALVE_MIN, metavar="MIN",
        help=f"Firmware NBR_CNT_HALVE_MIN: Zaehler alle MIN Minuten seit Boot halbieren (Standard {HALVE_MIN}; 0 = nie)",
    )
    ap.add_argument(
        "--window-min", type=int, default=WINDOW, metavar="MIN",
        help=f"Firmware NBR_WINDOW_MIN: Frischefenster der Kanten (Standard {WINDOW})",
    )
    args = ap.parse_args(argv)
    window, share_pct, halve_min = args.window_min, args.share_pct, args.halve_min

    since = None
    if args.since:
        fmt = "%Y-%m-%dT%H:%M" if "T" in args.since else "%Y-%m-%d"
        since = datetime.strptime(args.since, fmt)

    edges: dict[tuple[str, str], int] = {}  # (frm, to) -> last_up, "to hat frm gehoert"
    me_hits: dict[str, int] = {}  # frm -> last_up, cell[frm][0], wird bei EVICT genullt
    me_calls: set[str] = set()  # alle je direkt gehoerten Rufzeichen (ME-Zeilen), nie genullt
    cnt: dict[tuple[str, str], int] = {}  # (frm, to) -> Zaehler der Kante (Firmware, nach Halbierung)
    halve = {"last": 0}  # Zeitpunkt der letzten Halbierung dieser Sitzung (up)
    last_up: int | None = None
    evicts: list[tuple[int, str, str]] = []
    snaps: list[dict] = []
    union_heard: dict[str, set[str]] = defaultdict(set)
    meshneed_seq: dict[str, list[str]] = defaultdict(list)
    own = args.own
    cur: dict | None = None
    n_lines = 0

    def fresh_at(up: int, t: int) -> bool:
        return (up - t) % 65536 < window

    def zero_call(c: str) -> None:
        for k in [k for k in edges if c in k]:
            del edges[k]
            cnt.pop(k, None)
        me_hits.pop(c, None)

    def do_halve(up: int) -> None:
        # nbrSweep(): alle halve_min Minuten seit dem Boot wird jeder Zaehler zu (cnt + 1) >> 1.
        while halve_min > 0 and up - halve["last"] >= halve_min:
            halve["last"] += halve_min
            for k, c in cnt.items():
                cnt[k] = (c + 1) >> 1

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
                if line.count("[NBR]|") != 1:
                    continue  # zwei Zeilen ohne Newline verschweisst: nicht raten
                f_ = line[i + 6 :].rstrip("\n").split("|")
                try:
                    kind, up = f_[0], int(f_[1])
                except (IndexError, ValueError):
                    continue  # unlesbare Zeile (auch ein GW ohne Minute): nie ein Fehler
                if last_up is not None and up < last_up:
                    # Neustart: die Matrix ist leer, die Halbierung beginnt von vorn.
                    edges.clear()
                    cnt.clear()
                    me_hits.clear()
                    halve["last"] = 0
                last_up = up
                do_halve(up)
                if kind == "EDGE":
                    frm, to = f_[2], f_[3]
                    edges[(frm, to)] = up
                    cnt[(frm, to)] = int(f_[6]) if len(f_) > 6 and f_[6].isdigit() else 1
                    if frm != to:
                        union_heard[to].add(frm)
                elif kind == "RPT" and len(f_) > 5 and f_[5] == "ok":
                    # angewendeter HN-Bericht-Eintrag: ein Treffer auf (<m>, <x>)
                    k = (f_[3], f_[2])
                    if k not in edges or not fresh_at(up, edges[k]):
                        cnt[k] = 0
                    cnt[k] = min(255, cnt.get(k, 0) + 1)
                    edges[k] = up
                elif kind == "ME":
                    me_hits[f_[2]] = up
                    me_calls.add(f_[2])
                elif kind == "EVICT":
                    evicts.append((up, f_[3], f_[4]))
                    zero_call(f_[3])
                elif kind == "EVICT-E":
                    # W2c (Kantenpool): eine einzelne Kante wurde verdraengt, keine
                    # ganze Zeile -- beide Rufzeichen bleiben bestehen, nur die
                    # eine Beobachtung "<to> hat <from> gehoert" faellt weg. Wie
                    # zero_call() betrifft das nicht union_heard (das ist die
                    # Union ueber den GANZEN Lauf, nie rueckwirkend genullt).
                    edges.pop((f_[2], f_[3]), None)
                    cnt.pop((f_[2], f_[3]), None)
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

                    def fresh(t: int, up: int = up) -> bool:
                        return fresh_at(up, t)

                    direct = {c for c, t in me_hits.items() if c in rows and fresh(t) and c != own}
                    # maxd[x]: groesster Zaehler ueber frische Kanten von x zu direkten Nachbarn
                    maxd: dict[str, int] = {}
                    for (a, b), t in edges.items():
                        if b in direct and fresh(t):
                            maxd[a] = max(maxd.get(a, 0), cnt.get((a, b), 1))

                    def cov(a: str, b: str, maxd: dict[str, int] = maxd) -> bool:
                        t = edges.get((a, b))
                        return t is not None and fresh(t) and cnt.get((a, b), 1) * 100 >= share_pct * maxd.get(a, 0)

                    res = {}
                    for x in sorted(direct):
                        hx = {
                            frm
                            for (frm, to), t in edges.items()
                            if to == x and frm in rows and frm not in (x, own) and cov(frm, to)
                        }
                        excl = [
                            f2
                            for f2 in hx
                            if f2 not in direct and not any(cov(f2, m) for m in direct if m != x)
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
