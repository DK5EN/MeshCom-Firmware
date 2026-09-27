#!/usr/bin/env python3
"""Hop-Pruefung der Nachbarschaftsmatrix: nur Hop 1 und Hop 2, keine Server-Rufzeichen.

Seit 8487ea2a liefert Text (':') nur noch den ME-Schritt; Zeilen entstehen damit nur noch aus
dem letzten Hop (Hop 1) und dem vorletzten Pfad-Token eines POS/HEY-Frames (Hop 2). HEY-Gruppen
und HN-Berichte legen nie eine Zeile an. Dieses Skript prueft das je Schnappschuss (SNAP/ROW):

  - Hop 1: fuer die Zeile gibt es eine ME-Zeile innerhalb von 12 h vor dem Schnappschuss.
  - Hop 2: es gibt eine EDGE-Zeile "<to> hat <Zeile> gehoert" innerhalb von 12 h, und <to> ist
    Hop 1.
  - Server-Rufzeichen: Absender, die im selben Mitschnitt per RX-UDP (Server-Kopie) kamen und
    in keinem per Funk gehoerten POS/HEY-Frame stehen, plus die Liste aus der Auswertung
    2026-09-23 (Welle 6, docs/nbr-stage2-campaign.md). Keines davon darf eine Zeile haben.

Dazu Summen ueber den Zeitraum: EDGE-Zeilen mit Typ T (muessen 0 sein) und die Typen der
Server-Frames (nur ':' erwartet -- '!' oder '@' hiesse, der Server schickt jetzt auch POS/HEY
herunter und die Text-Regel reicht nicht mehr).

    python3 tools/nbrhopcheck.py --since 2026-09-23T20:01 2026-09-23.log 2026-09-24.log

Exit 0 = alle Schnappschuesse sauber, 1 = Befund, 2 = kein Schnappschuss im Zeitraum.
Zeilenformat: docs/nbr-logformat.md.
"""

from __future__ import annotations

import argparse
import re
import sys
from datetime import datetime, timedelta

WINDOW = timedelta(minutes=720)

# Absender, die in 34 h (22.09. 09:28 bis 23.09. 19:11) nur als Text und nie in einem POS/HEY
# per Funk auftauchten, dazu HB9JAY-6 (23.09. 20:03, von DK5EN-98 eingespeist) und HB9HDI-5
# (Server-Kopie am 23.09. abends, nie in einem Funk-POS/HEY).
KNOWN_SERVER = set(
    """
OE1XAR-33 DO2QG-1 DO5NE-1 DM3KS-12 OE1XAR-62 DC4FRT-3 DL2XL-5 HB9VQQ-1 DL1UDO-12 DF2AP-99
DO2QG-99 DK6IX-12 DK3ACH-12 DG3RAP-12 DO5DMF-8 DK4DO-1 DH1FR-1 DJ9DQ-1 DO7FJK-99 DL1MX-12
DL1HRU-7 DL9CL-9 DO5DMF-99 DJ2AX-99 DK6OK-12 IS0QLX-11 DO1TFS-99 OE1KFR-1 DO7FJK-12 TG5ALY-8
DL9UW-01 IW2NKC-10 IU5SNJ-12 OE6BYD-12 DF4ND-99 DF1BO-12 DC8CE-12 DL6WAB-1 OE5ANI-13 DK1JZ-12
OE6DJG-2 F6JON-13 DB2GS-1 IS0HXK-8 DD3AT-99 OE4AZU-10 DC2JR-2 DH1FR-2 DL4AWI-7 DL2XL-15
IU5SNJ-21 DK8JP-12 IW1QQG-64 DL8NDG-7 DL1GFM-7 OE5BKO-9 DM2FK-86 DL1EEN-12 IQ8KL-15 DL1RI-15
DL2YED-99 DL4MGC-12 DD9FH-1 DB1CDD-1 DK9ZZ-12 DL1ABR-12 DM5SR-13 HB9JAY-6 HB9HDI-5
""".split()  # noqa: SIM905 -- Blockliste zum Kopieren aus Auswertungen
)

FRAME = r"(\S) x[0-9A-F]{8} H\d\d S\d T\d M\d\d ([^> ]+)>"
RE_UDP = re.compile(r"RX-UDP +\d+ " + FRAME)
RE_RF = re.compile(r"\[LOG\] +\d+ " + FRAME)
RE_NBR = re.compile(r"\[NBR\]\|(ME|EDGE|SNAP|ROW)\|(.*)$")


def parse_host(line: str) -> datetime | None:
    try:
        return datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S.%f")  # noqa: DTZ007 -- Mitschnitt ist Ortszeit ohne Zone
    except ValueError:
        return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", help="Mitschnitt-Dateien (meshlogger), chronologisch")
    ap.add_argument("--since", metavar="YYYY-MM-DD[THH:MM]", help="nur Zeilen ab diesem Zeitpunkt")
    args = ap.parse_args(argv)

    since = None
    if args.since:
        fmt = "%Y-%m-%dT%H:%M" if "T" in args.since else "%Y-%m-%d"
        since = datetime.strptime(args.since, fmt)  # noqa: DTZ007 -- gleiche Ortszeit wie der Mitschnitt

    me_last: dict[str, datetime] = {}
    edge_last: dict[tuple[str, str], datetime] = {}  # (frm, to): "to hat frm gehoert"
    udp_src: set[str] = set()
    udp_types: dict[str, int] = {}
    rf_posthey: set[str] = set()
    text_edges = 0
    snaps: list[tuple[datetime, str, list[tuple[str, str]]]] = []  # (Zeit, eigenes Call, [(Zeile, Hop)])

    # Ausgewertet wird jede ROW-Zeile in dem Moment, in dem sie gelesen wird: dann enthalten
    # me_last/edge_last genau den Stand bis zu diesem Schnappschuss, und "innerhalb 12 h" ist
    # dieselbe Frist wie NBR_WINDOW_MIN in der Firmware.
    def hop(call: str, at: datetime) -> str:
        if call in me_last and at - me_last[call] <= WINDOW:
            return "hop1"
        via = sorted(
            to
            for (frm, to), t in edge_last.items()
            if frm == call and at - t <= WINDOW and to in me_last and at - me_last[to] <= WINDOW
        )
        return "hop2 via " + ",".join(via) if via else "FAIL: kein direkter Hoerer"

    for fn in args.files:
        with open(fn, errors="replace") as fh:
            for line in fh:
                ts = parse_host(line)
                if ts is None or (since and ts < since):
                    continue
                m = RE_UDP.search(line)
                if m:
                    udp_types[m.group(1)] = udp_types.get(m.group(1), 0) + 1
                    udp_src.add(m.group(2).split(",")[0])
                    continue
                m = RE_RF.search(line)
                if m:
                    if m.group(1) in "!@":
                        rf_posthey.update(m.group(2).split(","))
                    continue
                m = RE_NBR.search(line)
                if not m:
                    continue
                f = m.group(2).rstrip().split("|")
                kind = m.group(1)
                if kind == "ME":
                    me_last[f[1]] = ts
                elif kind == "EDGE":
                    edge_last[(f[1], f[2])] = ts
                    if f[3] == "T":
                        text_edges += 1
                elif kind == "SNAP":
                    snaps.append((ts, f[1], []))
                elif snaps:  # ROW
                    snaps[-1][2].append((f[2], hop(f[2], ts)))

    server = KNOWN_SERVER | (udp_src - rf_posthey)

    if not snaps:
        print("kein Schnappschuss im Zeitraum (SNAP alle 15 min ab Boot)")
        return 2

    bad_snaps = 0
    last_detail: list[str] = []
    for at, own, calls in snaps:
        detail, fails, n1, n2 = [], 0, 0, 0
        for call, h in calls:
            if call == own:
                continue
            n1 += h == "hop1"
            n2 += h.startswith("hop2")
            flag = "  <-- SERVER" if call in server else ""
            fails += h.startswith("FAIL") or bool(flag)
            detail.append(f"  {call:11} {h}{flag}")
        bad_snaps += fails > 0
        print(
            f"{at:%Y-%m-%d %H:%M}  Zeilen {sum(c != own for c, _ in calls):2}  Hop1 {n1:2}  Hop2 {n2:2}  Befunde {fails}"
        )
        if fails:
            print("\n".join(d for d in detail if "FAIL" in d or "SERVER" in d))
        last_detail = detail

    print("\nletzter Schnappschuss:")
    print("\n".join(last_detail))
    print(f"\nText-EDGE-Zeilen: {text_edges}   Server-Frames nach Typ: {udp_types}")
    ok = bad_snaps == 0 and text_edges == 0 and set(udp_types) <= {":"}
    print("PASS" if ok else "BEFUND")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
