#!/usr/bin/env python3
"""Per-node HTML report generator: OE3XWJ-12 (log key oe3xwj), 24-h log 2026-10-31/11-01.

Usage:
    python3 -I oe3xwj.py --data <dir> --kit <dir> --out <file>

--data  run data directory with q2/ q34/ q5/ q6/ health/ master/ extra/. The raw-log numbers come from
        extra/oe3xwj_*.json, written once by lib/oe3xwj_raw.py (this generator reads no raw log).
--kit   directory holding reportkit.py
--out   output HTML file

Stdlib only. All colours come from the kit classes (no hex values here).
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

NODE = "oe3xwj"
CALL = "OE3XWJ-12"
CET = "{CET}"
PING = "{ping}"

WARN: list[str] = []
FIG = {"n": 0}


# ----------------------------------------------------------------------------------------------
# number formatting
# ----------------------------------------------------------------------------------------------
def de(v: float, d: int = 0) -> str:
    """German number format: 1.234,5"""
    s = f"{v:,.{d}f}"
    return s.replace(",", "_").replace(".", ",").replace("_", ".")


def pct(v: float, d: int = 1) -> str:
    return f"{de(v, d)} %"


def abb(text: str) -> str:
    FIG["n"] += 1
    return f"Abb. {FIG['n']}: {text}"


# ----------------------------------------------------------------------------------------------
# SVG helpers for the explainer drawings (classes only, no colours)
# ----------------------------------------------------------------------------------------------
def _width(s: str, cls: str) -> float:
    return len(s) * (7.2 if cls == "d-txt" else 6.4)


def T(x: float, y: float, s: str, cls: str = "d-txt-s", anchor: str = "start",
      maxw: float | None = None) -> str:
    w = _width(s, cls)
    if anchor == "start":
        lo, hi = x, x + w
    elif anchor == "end":
        lo, hi = x - w, x
    else:
        lo, hi = x - w / 2, x + w / 2
    if lo < 2 or hi > 638:
        WARN.append(f"text outside viewBox: {s!r} ({lo:.0f}..{hi:.0f})")
    if maxw is not None and w > maxw:
        WARN.append(f"text wider than its box: {s!r} ({w:.0f} > {maxw:.0f})")
    return f'<text class="{cls}" x="{x}" y="{y}" text-anchor="{anchor}">{s}</text>'


def R(x: float, y: float, w: float, h: float, cls: str = "d-box", extra: str = "") -> str:
    return f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="6"{extra}/>'


def C(cx: float, cy: float, r: float, cls: str = "d-node", extra: str = "") -> str:
    return f'<circle class="{cls}" cx="{cx}" cy="{cy}" r="{r}"{extra}/>'


def L(x1: float, y1: float, x2: float, y2: float, cls: str = "d-line") -> str:
    return f'<line class="{cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>'


def ARR(x1: float, y1: float, x2: float, y2: float, state: str = "acc", flow: bool = True,
        delay: str = "") -> str:
    """Line with an arrow head; state in ok|act|watch|acc."""
    cls = f"d-line d-{state}-s" + (" anim-flow" if flow else "") + (f" {delay}" if delay else "")
    dx, dy = x2 - x1, y2 - y1
    n = math.hypot(dx, dy) or 1.0
    ux, uy = dx / n, dy / n
    bx, by = x2 - ux * 9, y2 - uy * 9
    px, py = -uy * 4.5, ux * 4.5
    head = (f'<polygon class="d-{state}-f" points="{x2:.1f},{y2:.1f} {bx + px:.1f},{by + py:.1f} '
            f'{bx - px:.1f},{by - py:.1f}"/>')
    return f'<line class="{cls}" x1="{x1}" y1="{y1}" x2="{bx:.1f}" y2="{by:.1f}"/>{head}'


# ----------------------------------------------------------------------------------------------
# data access
# ----------------------------------------------------------------------------------------------
def jload(p: Path) -> dict:
    return json.loads(p.read_text(encoding="utf-8"))


def load_master(data: Path) -> tuple[dict[str, dict[str, str]], dict[str, str]]:
    master: dict[str, dict[str, str]] = {}
    lines = (data / "master" / "master.tsv").read_text(encoding="utf-8").splitlines()
    head = lines[0].split("\t")
    for ln in lines[1:]:
        parts = ln.split("\t")
        parts += [""] * (len(head) - len(parts))
        row = dict(zip(head, parts))
        master[row["call"]] = row
    dist: dict[str, str] = {}
    dl = (data / "master" / "distances.tsv").read_text(encoding="utf-8").splitlines()
    dh = dl[0].split("\t")
    col = dh.index(f"d_{NODE}")
    for ln in dl[1:]:
        parts = ln.split("\t")
        if len(parts) > col:
            dist[parts[0]] = parts[col]
    return master, dist


def toa_s(payload: int, sf: int = 11, bw: float = 250e3, cr: int = 2, pre: int = 8) -> float:
    """LoRa time on air (explicit header, CRC on, LDRO off), seconds."""
    tsym = (2 ** sf) / bw
    num = 8 * payload - 4 * sf + 28 + 16
    nsym = 8 + max(math.ceil(num / (4 * sf)) * (cr + 4), 0)
    return (pre + 4.25 + nsym) * tsym


# ----------------------------------------------------------------------------------------------
# explainer drawings
# ----------------------------------------------------------------------------------------------
def draw_ping(per_ping_tx: float, per_ping_air: float, ack_ids: float) -> str:
    s = []
    s.append(R(10, 70, 130, 70, "d-box d-acc"))
    s.append(T(75, 98, "OE3XWJ-12", "d-txt", "middle", 130))
    s.append(T(75, 118, "sendet Ping", "d-txt-s", "middle", 130))
    s.append(ARR(140, 105, 250, 105, "acc"))
    s.append(T(195, 90, "1 pro Minute", "d-txt-s", "middle"))
    s.append(R(250, 50, 140, 110, "d-box d-neu"))
    s.append(T(320, 85, "Fremde Gateways", "d-txt", "middle", 140))
    s.append(T(320, 108, f"{de(ack_ids, 1)} ACK-Ids je Ping", "d-txt-s", "middle", 140))
    s.append(T(320, 130, "nicht unsere fuenf", "d-txt-s", "middle", 140))
    for i, name in enumerate(["HL", "IA", "IR", "OC"]):
        y = 28 + i * 40
        s.append(R(480, y, 70, 30, "d-box d-act"))
        s.append(T(515, y + 20, name, "d-txt", "middle", 70))
        s.append(ARR(390, 105, 480, y + 15, "act", True, "anim-delay-1"))
    s.append(T(560, 100, "leiten", "d-txt-s"))
    s.append(T(560, 118, "alle ACKs", "d-txt-s"))
    s.append(T(560, 136, "weiter", "d-txt-s"))
    s.append(R(10, 186, 620, 36, "d-box d-act"))
    s.append(T(320, 209, f"je Ping im Cluster: {de(per_ping_tx, 1)} Sendungen, "
               f"{de(per_ping_air, 1)} s Sendezeit", "d-txt", "middle", 610))
    return "".join(s)


def draw_stall(gap_s: float, n_safety: int, q_max: int, oth_lo: int, oth_hi: int, last_rx: str,
               next_rx: str) -> str:
    s = []
    lanes = [("Funk-Empfang", 38), ("CAD-Abbruch", 82), ("Eigene Sendungen", 126),
             ("Sende-Warteschlange", 170), ("Andere Knoten", 214)]
    for name, y in lanes:
        s.append(T(10, y + 5, name, "d-txt", "start", 170))
    for x in range(190, 251, 10):
        s.append(L(x, 28, x, 48, "d-line d-ok-s"))
    for x in range(490, 621, 10):
        s.append(L(x, 28, x, 48, "d-line d-ok-s"))
    s.append(R(262, 26, 216, 24, "d-box d-act anim-pulse"))
    s.append(T(370, 43, f"{de(gap_s, 0)} s ohne Empfang", "d-txt", "middle", 210))
    for i in range(n_safety):
        x = 266 + i * 9.5
        s.append(L(round(x, 1), 72, round(x, 1), 92, "d-line d-watch-s"))
    s.append(T(262, 131, f"0 Sendungen in {de(gap_s, 0)} s", "d-txt-s"))
    for i in range(q_max):
        cls = "d-box d-neu" if i < 1 else "d-box d-act"
        s.append(R(262 + i * 22, 162, 16, 16, cls))
    s.append(T(270 + q_max * 22, 175, f"0 bis {q_max} Eintraege", "d-txt-s"))
    s.append(T(262, 219, f"Die anderen vier empfangen weiter ({oth_lo} bis {oth_hi} Empfaenge)", "d-txt-s"))
    s.append(T(262, 246, last_rx, "d-txt-s"))
    s.append(T(478, 246, next_rx, "d-txt-s", "end"))
    return "".join(s)


def draw_eth(flaps: list[float], n_down: int, ka_ok: int, ka_miss: int, miss_pct: float) -> str:
    s = []
    s.append(R(10, 20, 130, 50, "d-box d-acc"))
    s.append(T(75, 42, "OE3XWJ-12", "d-txt", "middle", 130))
    s.append(T(75, 60, "RAK4631 + Ethernet", "d-txt-s", "middle", 130))
    s.append(L(140, 45, 190, 45))
    s.append(L(220, 45, 270, 45))
    s.append(L(195, 33, 215, 57, "d-line d-act-s anim-pulse"))
    s.append(L(215, 33, 195, 57, "d-line d-act-s anim-pulse"))
    s.append(R(270, 20, 100, 50, "d-box d-neu"))
    s.append(T(320, 49, "Switch/Router", "d-txt-s", "middle", 100))
    s.append(L(370, 45, 470, 45))
    s.append(R(470, 20, 160, 50, "d-box d-neu"))
    s.append(T(550, 42, "OE-Server", "d-txt", "middle", 160))
    s.append(T(550, 60, "UDP, Keepalive 30 s", "d-txt-s", "middle", 160))
    s.append(T(10, 100, f"{n_down} mal Link down, IP unveraendert (Vermutung: feste Adresse)", "d-txt-s"))
    s.append(L(10, 130, 630, 130, "d-line"))
    for h in flaps:
        x = 10 + (h - 18.0) / 12.0 * 620
        s.append(L(round(x, 1), 118, round(x, 1), 142, "d-line d-act-s"))
    s.append(T(10, 162, "18 Uhr", "d-txt-s"))
    s.append(T(320, 162, "00 Uhr", "d-txt-s", "middle"))
    s.append(T(630, 162, "06 Uhr", "d-txt-s", "end"))
    s.append(T(10, 192, f"Keepalive {de(ka_ok)} ok, {ka_miss} ohne Antwort ({de(miss_pct, 2)} %), "
               "nicht den Flaps zuzuordnen", "d-txt-s"))
    return "".join(s)


def draw_precad(n_pre: int, n_all: int, ev_pct: float, air_pct: float, rx_err_pct: float,
                top: list[tuple[str, int]]) -> str:
    s = []
    s.append(R(10, 20, 150, 56, "d-box d-act"))
    s.append(T(85, 43, "Knoten ohne CAD", "d-txt", "middle", 150))
    s.append(T(85, 63, f"{n_pre} von {n_all} Knoten", "d-txt-s", "middle", 150))
    s.append(R(480, 20, 150, 56, "d-box d-ok"))
    s.append(T(555, 43, "Knoten mit CAD", "d-txt", "middle", 150))
    s.append(T(555, 63, f"{n_all - n_pre} von {n_all} Knoten", "d-txt-s", "middle", 150))
    s.append(R(210, 30, 220, 36, "d-box d-neu"))
    s.append(T(320, 53, "Funkkanal", "d-txt", "middle", 220))
    s.append(ARR(160, 48, 208, 48, "act"))
    s.append(ARR(480, 48, 432, 48, "ok", False))
    s.append(T(185, 96, "sendet sofort", "d-txt-s", "middle"))
    s.append(T(455, 96, "hoert vorher", "d-txt-s", "middle"))
    s.append(T(10, 130, f"{de(ev_pct, 1)} % der gehoerten Frames und {de(air_pct, 1)} % der Airtime kommen "
               "von Knoten ohne CAD", "d-txt-s"))
    s.append(T(10, 154, "Spitzenreiter: " + ", ".join(f"{c} ({de(n)} Frames)" for c, n in top),
               "d-txt-s"))
    s.append(T(10, 178, f"RX-Fehler am Knoten: {de(rx_err_pct, 1)} % aller Empfangsversuche, "
               "rauschbegrenzt", "d-txt-s"))
    return "".join(s)


def draw_gwrad(own_gw: float, foreign: float, p50: float, air_s: float, wj_tx: int, wj_after: int,
               wj_pct: float) -> str:
    s = []
    s.append(R(10, 80, 120, 56, "d-box d-neu"))
    s.append(T(70, 104, "OE1XAR-33", "d-txt", "middle", 120))
    s.append(T(70, 124, "alle 607 s", "d-txt-s", "middle", 120))
    s.append(R(190, 30, 150, 52, "d-box d-acc"))
    s.append(T(265, 52, f"{de(own_gw, 0)} eigene Gateways", "d-txt", "middle", 150))
    s.append(T(265, 70, "alle strahlen ab", "d-txt-s", "middle", 150))
    s.append(R(190, 134, 150, 52, "d-box d-neu"))
    s.append(T(265, 156, f"{de(foreign, 1)} fremde", "d-txt", "middle", 150))
    s.append(T(265, 174, "Gateways direkt gehoert", "d-txt-s", "middle", 150))
    s.append(ARR(130, 100, 190, 60, "acc"))
    s.append(ARR(130, 116, 190, 158, "acc", True, "anim-delay-1"))
    s.append(R(430, 70, 200, 76, "d-box d-act"))
    s.append(T(530, 96, "Funkkanal", "d-txt", "middle", 200))
    s.append(T(530, 116, f"ca. {de(p50, 0)} Abstrahlungen", "d-txt-s", "middle", 200))
    s.append(T(530, 134, f"{de(air_s, 0)} s Sendezeit je Telegramm", "d-txt-s", "middle", 200))
    s.append(ARR(340, 56, 430, 96, "act"))
    s.append(ARR(340, 160, 430, 120, "act", True, "anim-delay-1"))
    s.append(T(10, 214, f"OE3XWJ-12: {wj_tx} Abstrahlungen, {wj_after} davon ({de(wj_pct, 1)} %) "
               "nach einer schon gehoerten", "d-txt-s"))
    return "".join(s)


def draw_stale(n: int, span_h: float, a_min: float, b_min: float, n_cet: int, n_loop: int,
               n_hop0: int) -> str:
    s = []
    s.append(R(10, 50, 120, 56, "d-box d-neu"))
    s.append(T(70, 74, "OE1XAR-33", "d-txt", "middle", 120))
    s.append(T(70, 94, "Zeit-Telegramm", "d-txt-s", "middle", 120))
    s.append(ARR(130, 78, 240, 78, "acc"))
    s.append(R(240, 50, 150, 56, "d-box d-watch"))
    s.append(T(315, 74, "OE3MTC-10", "d-txt", "middle", 150))
    s.append(T(315, 94, f"sendet {de(a_min, 0)} bis {de(b_min, 0)} min", "d-txt-s", "middle", 150))
    s.append(ARR(390, 78, 490, 78, "act"))
    s.append(C(440, 78, 7, "d-node d-act anim-move", ' style="--dx:40px;--dur:3s"'))
    s.append(R(490, 50, 140, 56, "d-box d-ok"))
    s.append(T(560, 74, "OE3XWJ-12", "d-txt", "middle", 140))
    s.append(T(560, 94, "Gate: mehr als 150 s", "d-txt-s", "middle", 140))
    s.append(T(10, 150, f"{n} Kopien in {de(span_h, 1)} h, {de(a_min, 1)} bis {de(b_min, 1)} min alt, "
               "alle mit OE3MTC-10 im Pfad", "d-txt-s"))
    s.append(T(10, 172, f"{n_cet} vom Alters-Gate verworfen, {n_loop} von der Schleifensperre "
               "(eigener Ruf im Pfad)", "d-txt-s"))
    s.append(T(10, 194, f"{n_hop0} mit Hop 0 (ohnehin nicht weiterzuleiten): nichts davon ging auf Sendung",
               "d-txt-s"))
    return "".join(s)


def draw_dedup(share_ack: float, win_now: float, win_wo: float, a_min: float, b_min: float) -> str:
    s = []
    s.append(T(10, 22, "Dedup-Ring: 100 Slots, ein Slot je gehoerte Id", "d-txt", "start"))
    w_ack = round(620 * share_ack / 100, 1)
    s.append(R(10, 36, w_ack, 36, "d-box d-act"))
    s.append(R(10 + w_ack, 36, round(620 - w_ack, 1), 36, "d-box d-neu"))
    s.append(T(16, 59, f"ACK-Ids der Pings {de(share_ack, 0)} %", "d-txt-s", "start", w_ack - 8))
    s.append(T(10 + w_ack + 8, 59, "uebrige Ids", "d-txt-s", "start"))
    s.append(L(10, 112, 630, 112))
    for m in (0, 10, 20, 30, 40, 50, 60):
        x = 10 + m * 10
        s.append(L(x, 108, x, 116))
        s.append(T(x, 130, f"{m}", "d-txt-s", "middle"))
    s.append(T(630, 148, "Minuten", "d-txt-s", "end"))
    xa = 10 + win_now * 10
    xb = 10 + win_wo * 10
    s.append(L(round(xa, 1), 88, round(xa, 1), 112, "d-line d-act-s"))
    s.append(L(round(xb, 1), 88, round(xb, 1), 112, "d-line d-ok-s"))
    s.append(T(round(xa, 1) - 6, 100, f"Fenster heute: {de(win_now, 0)} min", "d-txt-s", "end"))
    s.append(T(round(xb, 1) - 6, 100, f"ohne Ping-ACKs: {de(win_wo, 0)} min (Rechnung)", "d-txt-s",
               "end"))
    s.append(R(10 + a_min * 10, 160, round((b_min - a_min) * 10, 1), 22, "d-box d-watch"))
    s.append(T(10 + (a_min + b_min) / 2 * 10, 175,
               f"alte Zeit-Kopien: {de(a_min, 0)} bis {de(b_min, 0)} min", "d-txt-s", "middle"))
    return "".join(s)


def draw_veto(n_relay: int, n_sent: int, n_all: int, air: float, share: float, air_share: float) -> str:
    s = []
    s.append(R(10, 30, 130, 56, "d-box d-neu"))
    s.append(T(75, 54, "Nachbar", "d-txt", "middle", 130))
    s.append(T(75, 74, "sendet Frame X", "d-txt-s", "middle", 130))
    s.append(ARR(140, 58, 220, 58, "acc"))
    s.append(R(220, 30, 200, 110, "d-box d-acc"))
    s.append(T(320, 50, "OE3XWJ-12", "d-txt", "middle", 200))
    s.append(R(236, 66, 168, 36, "d-box d-watch"))
    s.append(T(320, 89, "Relay X wartet", "d-txt-s", "middle", 168))
    s.append(T(320, 124, "hoert die Kopie mit", "d-txt-s", "middle", 200))
    s.append(R(480, 30, 150, 110, "d-box d-watch"))
    s.append(T(555, 52, "Regel", "d-txt", "middle", 150))
    s.append(T(555, 74, "einziger Anbieter", "d-txt-s", "middle", 150))
    s.append(T(555, 92, "fuer OE3MIF-12,", "d-txt-s", "middle", 150))
    s.append(T(555, 110, "OE3NRA-1 ...", "d-txt-s", "middle", 150))
    s.append(T(555, 128, "Relay bleibt", "d-txt-s", "middle", 150))
    s.append(ARR(480, 85, 422, 85, "watch", False))
    s.append(R(10, 168, 620, 52, "d-box d-act"))
    s.append(T(320, 190, f"{n_relay} von {de(n_sent)} Relays ({de(share, 1)} %) trotzdem gesendet",
               "d-txt", "middle", 610))
    s.append(T(320, 210, f"mit den ACK-Weiterleitungen {n_all} Sendungen = {de(air, 0)} s "
               f"({de(air_share, 1)} % der eigenen Sendezeit, Obergrenze)", "d-txt-s", "middle", 610))
    return "".join(s)


def draw_pingcount(pings: int, span_h: float) -> str:
    s = []
    s.append(T(10, 24, "ESP32 (esp32_main.cpp:5264, :5285): Zaehler, nach --pingmax Pings Stopp",
               "d-txt-s"))
    for i in range(5):
        s.append(C(40 + i * 40, 58, 14, "d-node d-ok"))
        s.append(T(40 + i * 40, 63, str(i + 1), "d-txt-s", "middle"))
    s.append(R(250, 42, 110, 32, "d-box d-ok"))
    s.append(T(305, 63, "Stopp", "d-txt", "middle", 110))
    s.append(T(10, 112, "RAK4631 (nrf52_main.cpp:2717-2737): kein Zaehler, Stopp nur ueber --pingcall",
               "d-txt-s"))
    for i in range(9):
        cls = "d-node d-act anim-pulse" if i >= 6 else "d-node d-act"
        s.append(C(40 + i * 40, 148, 14, cls))
        s.append(T(40 + i * 40, 153, str(i + 1), "d-txt-s", "middle"))
    s.append(ARR(380, 148, 620, 148, "act"))
    s.append(T(10, 196, f"weiter alle 60 s: {de(pings)} Pings in {de(span_h, 1)} h, "
               "Ende nur mit --pingcall NONE", "d-txt-s"))
    return "".join(s)


# ----------------------------------------------------------------------------------------------
# page
# ----------------------------------------------------------------------------------------------
def li(lead: str, text: str) -> str:
    return f"<li><strong>{lead}</strong> {text}</li>"


def ul(items: list[str]) -> str:
    return "<ul>" + "".join(items) + "</ul>"


def build(data: Path, kit: Path, out: Path) -> None:
    sys.path.insert(0, str(kit))
    from reportkit import bars, chip, explain, hist, page, table  # noqa: PLC0415

    h = jload(data / "health" / "results.json")
    hn = h["nodes"][NODE]
    hnet = h["network"]
    q2 = jload(data / "q2" / "results.json")
    q2n = q2["nodes"][NODE]
    q34 = jload(data / "q34" / "results.json")["nodes"][NODE]
    q5 = jload(data / "q5" / "results.json")["nodes"][NODE]
    q6j = jload(data / "q6" / "results.json")
    q6 = q6j["nodes"][NODE]
    q6net = q6j["network"]
    master, dist = load_master(data)

    def explain_h(height: int, svg: str, caption: str) -> str:
        return explain(svg, caption, height=height)

    # ---- derived numbers -------------------------------------------------------------------
    span_h = hn["span"]["hours"]
    ps = q6net["ping_share"]
    pings = ps["ping_tx"]["ping_orig_or_relay"]
    ack_relay = ps["ping_tx"]["ack_of_ping_relay"]
    total_tx = ps["total_tx"]
    total_air = ps["total_air_s"]
    ping_air = ps["ping_air_s"]
    ping_tx_all = ack_relay + pings
    tx_share = 100 * ping_tx_all / total_tx
    air_share = 100 * ping_air / total_air
    per_tx = ping_tx_all / pings
    per_air = ping_air / pings
    per_ack_relay = ack_relay / pings
    toa_ping = toa_s(36)
    toa_ack = toa_s(12)
    model_air = pings * toa_ping + ack_relay * toa_ack
    xj = {k: jload(data / "extra" / f"oe3xwj_{k}.json")
          for k in ("ping", "stall", "cet_stale", "rerelay", "rxerror")}
    pg = xj["ping"]
    wjp = pg["at_wj"]
    ev_ack = pg["cluster_by_ping_event"]
    st = xj["stall"]
    cs = xj["cet_stale"]
    rr = xj["rerelay"]
    rxe = xj["rxerror"]
    ack_air = ack_relay * toa_ack
    ack_tx_share = 100 * ack_relay / total_tx
    ack_air_share = 100 * ack_air / total_air
    ack_share_pct = hnet["ack_ping_attribution"]["acked_prefix_F5CE7_share_pct_of_012_acks"]
    ack_air_pct = hnet["ack_ping_attribution"]["ack_airtime_share_pct"]

    adds = rr["rx_dedup_add"]
    ack_new = q6["totals"]["ack_new"]
    dd_share = 100 * ack_new / adds
    rate = adds / rr["span_min"]
    win_now = rr["dedup_window_min"]
    win_wo = 100 / ((adds - ack_new) / rr["span_min"])
    stale = cs["rows"]
    stale_a, stale_b = cs["age_min_s"] / 60, cs["age_max_s"] / 60
    q5_top = q5["rf_stale_lasthop_top"]
    oth = list(st["receptions_at_others_in_gap"].values())
    fwc = hn["firmware_census"]
    cl_rad = jload(data / "q5" / "results.json")["network"]["oe3_cluster"]
    wj_ping_air = pings * toa_ping
    ph_vals = list(q2n["tx_lines"]["per_hour"].values())
    ack_lo = min(ack_air_pct[k] for k in ("oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"))
    ack_hi = max(ack_air_pct[k] for k in ("oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"))
    q_ring_max = q2n["occupancy"]["queued"]["max"]

    fate = q2n["fate"]
    tr = hn["traffic"]
    ch = hn["channel"]["CHANNEL_UTIL"]
    cad = hn["cad"]
    eth = hn["eth"]
    own = hn["own_tx"]
    cross = q2["network"]["cross_node"]
    opp = q6["opportunity_airtime"]
    co = q6["cancel_opportunity"]
    n_veto = co["!"]["sent_with_foreign_copy_heard_while_queued"] + \
        co["@"]["sent_with_foreign_copy_heard_while_queued"] + \
        co[":"]["sent_with_foreign_copy_heard_while_queued"]
    n_veto_ack = co["A"]["sent_with_foreign_copy_heard_while_queued"]
    relays_sent = q6["relay_fate"]["sent"]

    flaps = []
    for ev in eth["events_first"]:
        if ";link;down" in ev["line"]:
            hh, mm, sec = (int(x) for x in ev["wall"][-8:].split(":"))
            val = hh + mm / 60 + sec / 3600
            flaps.append(val + 24 if val < 12 else val)

    parts: list[str] = []
    A = parts.append

    # ---- header ------------------------------------------------------------------------------
    A('<div class="eyebrow">MeshCom Log-Auswertung · OE3XWJ-12</div>')
    A("<h1>OE3XWJ-12 läuft gesund, aber sein eigener Ping an 2025 jede Minute "
      "belastet den ganzen Cluster</h1>")
    A('<ul class="meta">'
      f"<li><strong>Log-Datei:</strong> oe3xwj.log ({de(hn['span']['lines'])} Zeilen)</li>"
      "<li><strong>Zeitraum:</strong> 31.10.2026 06:27:07 bis 01.11.2026 07:57:37 Logger-Uhr "
      f"({de(span_h, 2)} h). Die Knotenuhr geht 1 s nach (UTC+1 wie der Logger), kein Versatz.</li>"
      "<li><strong>Hardware:</strong> RAK4631 (nRF52840) mit Ethernet (Chip W5100S abgeleitet, nicht im Log)</li>"
      "<li><strong>Firmware:</strong> 4.40a, Release-Stempel 20260929 (FLASH_VERSION, kein Baudatum), genauer Build unbekannt "
      "(kein Boot-Block im Log; der TX-Slot-Pool ist aus den Markern abgeleitet, also &bdquo;inferred&ldquo;)</li>"
      "<li><strong>Standort/Höhe:</strong> Jauerling, 48,3392 N / 15,3437 E, 910 m (Stammdaten)</li>"
      "<li><strong>Rolle:</strong> Internet-Gateway und Relay (GW-ID 64FD739C)</li>"
      "<li><strong>LoRa:</strong> SF11 / BW250 / CR4:6, aus der Sendezeit abgeleitet; Frequenz und "
      "Leistung stehen nicht im Log</li>"
      "<li><strong>Stand:</strong> 01.11.2026 · Netzsicht im "
      '<a href="uebersicht.html">Cluster-Überblick</a></li></ul>')

    A('<div class="bluf"><p class="lead">OE3XWJ-12 ist technisch gesund: kein Neustart, die '
      f"Sende-Warteschlange war höchstens {q2n['occupancy']['queued']['max']:.0f} von 20 Slots "
      f"belegt und {pct(fate['pct']['tx_air'], 2)} der Einträge gingen auf Sendung. "
      f"Auffällig ist der eigene Ping an 2025, {de(pings)} Stück in {de(span_h, 1)} h "
      f"(einer pro Minute). Der Ping-Test, also diese Pings plus die {de(ack_relay)} ACK-Weiterleitungen, "
      f"ist {pct(tx_share)} aller Sendungen und {pct(air_share)} der Sendezeit des Fünf-Knoten-Clusters. "
      f"Die ACK-Weiterleitungen allein sind {pct(ack_tx_share)} der Sendungen und {pct(ack_air_share)} "
      "der Sendezeit.</p>"
      "<p class=\"reco\">Empfehlung: klären, ob der Ping ein gewollter Test ist. Wenn nicht, "
      "auf dem Knoten <code>--pingcall NONE</code> setzen; wenn ja, das Intervall deutlich "
      "verlängern (zum Beispiel 10 Minuten, Modellrechnung in F6). Auf dem RAK4631 beendet "
      "<code>--ping stop</code> den Ping nicht.</p></div>")

    A('<div class="callout act"><strong>Handlungsbedarf</strong>'
      + ul([
          li("Ping prüfen:", f"Der Knoten sendet seit Logbeginn jede Minute {PING} an 2025. Fremde "
             f"Gateways quittieren jeden Ping mit im Mittel {de(ev_ack['ack_ids_per_ping_mean'], 1)} "
             "verschiedenen ACK-Ids; die vier anderen Cluster-Knoten leiten diese ACKs weiter. Wer hat "
             "<code>--pingcall 2025</code> gesetzt und warum?"),
          li("Entscheidung offen, Unterschied ESP32 / RAK4631:", "Ein ESP32 zählt die Pings herunter und "
             "hört nach <code>--pingmax</code> Stück auf. Der RAK4631 hat keinen Zähler und pingt, solange "
             "ein <code>--pingcall</code> gesetzt ist; <code>--ping stop</code> bleibt dort ohne Wirkung, nur "
             "<code>--pingcall NONE</code> beendet die Pings (Abschnitt F6). Welches Verhalten gewollt ist, "
             "entscheidet der Betreiber."),
      ]) + "</div>")

    # ---- Antwort-Index -----------------------------------------------------------------------
    rows = [
        ["Q0 Gesundheit", f"Läuft stabil: kein Neustart, Heap flach, Warteschlange höchstens "
         f"{q_ring_max:.0f} von 20. Zu beobachten: ein Funk-Stillstand von {de(st['gap_s'], 0)} s, "
         f"{eth['last']['downs']} Ethernet-Link-Flaps, {hn['gateway']['beat_after_keep_missing']} "
         "Keepalives ohne Server-Antwort.", chip("watch")],
        ["Q2 TX-Puffer mit Slots", f"Sauber: {de(fate['total_writes'])} Einträge, "
         f"{pct(fate['pct']['tx_air'], 2)} gesendet, 0 ungeklärt. Im Cluster der schnellste Relay.",
         chip("ok")],
        ["Q3 Verlust durch Zeitverfall", "Nur 4 eigene WX-Positionen (von 50) nach 120 s verworfen. "
         "Kein ACK, Text oder Relay verloren.", chip("ok")],
        ["Q4 Gewinn durch Zeitverfall", "Klein: 4,6 s Sendezeit (0,15 %). Der eigentliche Gewinn ist "
         f"das {CET}-Gate: 6 alte Zeit-Kopien gestoppt.", chip("ok")],
        ["Q5 Zeitsignal", f"Korrekt, nichts Altes gesendet (höchstens {de(q5['tx_age_s_logger_based']['max'], 1)} s). "
         f"Alte Kopien ({de(stale_a, 1)} bis {de(stale_b, 1)} min) kommen alle über OE3MTC-10; "
         f"{pct(q5['tx_after_overheard_pct'])} der eigenen Server-Abstrahlungen folgen einer schon "
         "gehörten.", chip("watch")],
        ["Q6 Manipulationen", f"Ping-Test (Pings plus ACK-Weiterleitungen): {pct(tx_share)} der "
         f"Cluster-Sendungen, {pct(air_share)} der Sendezeit; die ACK-Weiterleitungen allein "
         f"{pct(ack_tx_share)} / {pct(ack_air_share)}. Betreiber-Entscheidung nötig.", chip("act")],
    ]
    A("<h2>Antwort-Index</h2>")
    A(table(["Frage", "Kurzantwort", "Urteil"], rows, raw=True))
    A('<nav class="toc"><ol>'
      '<li><a href="#gesundheit">Gesundheit und Funkumfeld</a></li>'
      '<li><a href="#f2">F2 TX-Puffer mit Slots</a></li>'
      '<li><a href="#f3">F3 Verlust durch Zeitverfall</a></li>'
      '<li><a href="#f4">F4 Gewinn durch Zeitverfall</a></li>'
      '<li><a href="#f5">F5 Zeitsignal</a></li>'
      '<li><a href="#f6">F6 Manipulationen dieses Knotens</a></li>'
      '<li><a href="#bugs">Verdachtsfälle Firmware</a></li>'
      '<li><a href="#nachbarn">Nachbarn und Hardware</a></li>'
      '<li><a href="#grenzen">Was nicht geprüft ist</a></li>'
      '<li><a href="#fragen">Offene Fragen an den Betreiber</a></li>'
      '<li><a href="#dev">Methodik und Code-Stellen</a></li></ol></nav>')

    # ---- Gesundheit --------------------------------------------------------------------------
    A('<h2 id="gesundheit">Gesundheit und Funkumfeld</h2>')
    A('<p><strong>Der Knoten ist gesund.</strong> Kein Neustart, kein Absturz, kein Speicherverlust. '
      "Drei Dinge verdienen einen Blick: ein Funk-Stillstand, Ethernet-Aussetzer und die ACK-Last.</p>")
    cadp = hn["cad"]["cad_attempt_pct"]
    A(ul([
        li("Betrieb:", "Uptime 91.815 s beim letzten STAT, 0 Neustarts, 0 Crash-Marker, Heap "
           "konstant 92.264 B (Steigung 0,0 B/h)."),
        li("Verkehr:", f"{de(tr['heard_events'])} gehörte Frames plus {de(tr['ack_events'])} ACK-Frames "
           f"({pct(tr['ack_share_of_all_frames_pct'])} aller Empfänge); {tr['fcs0000_excluded']} "
           "Decoder-Abbrüche (FCS:0000) nicht mitgezählt. Texte "
           f"{pct(tr['types'][':']['pct'])}, HEY-Baken {pct(tr['types']['@']['pct'])}, Positionen "
           f"{pct(tr['types']['!']['pct'])}."),
        li("Kanalauslastung:", f"Mittel {pct(ch['mean'])}, Median {pct(ch['median'])}, 95. Perzentil "
           f"{pct(ch['p95'], 0)}, Spitze {pct(ch['max'], 0)}; nie über 70 %. Eigene Sendezeit "
           f"{de(own['airtime_s'], 1)} s ({pct(q34['tx_duty_pct'], 2)} Duty)."),
        li("Nachbarn:", f"{hn['neighbours']['n_direct']} direkte Nachbarn. Der stärkste ist OE3REI-14 "
           "(Median -89 dBm, SNR 5 dB, 26,8 km); viele weitere liegen bei -103 bis -105 dBm. "
           "Siehe Tabelle weiter unten."),
        li("Rauschen:", f"{de(rxe['rx_error'])} RX-Fehler "
           f"(Kopf gehört, kein gültiges Frame) = {pct(rxe['share_pct'])} aller Empfangsversuche "
           f"(Nenner: RX-Fehler plus {de(rxe['rx_done_incl_ack'])} dekodierte Empfänge einschließlich ACKs); "
           "unter der Beobachten-Schwelle von 40 %. Median -102 dBm, SNR -13 dB. Kein Empfang über "
           "-60 dBm, also kein starker Störer in der Nähe."),
        li("CAD:", f"CAD ist die Kanalabfrage vor dem Senden. {de(cad['cad_scans'])} Abfragen, "
           f"{pct(cad['initial_busy_rate_pct'])} zunächst belegt, davon {pct(cad['confirmed_busy_pct_of_initial_busy'])} "
           f"bestätigt und {pct(cad['false_positive_pct_of_initial_busy'])} Fehlalarm. "
           f"{pct(cadp['0'])} der Sendungen gingen beim ersten Versuch hinaus."),
        li("Server-Verbindung:", f"Keepalive alle 30 s: {de(hn['gateway']['keepalive_ok'])} ok von rund "
           f"3.061; {hn['gateway']['beat_after_keep_missing']} ohne Server-Antwort "
           f"({pct(hn['gateway']['beat_miss_pct_of_keepalives'], 2)}). Alle fünf OE3-Gateways "
           "verloren gemeinsam einen Moment um 07:22 (Server-Seite). NTP ok 102, Timeouts 4."),
    ]))

    A(bars([("OE3XHL-12", ack_air_pct["oe3xhl"]), ("OE3XIA-12", ack_air_pct["oe3xia"]),
            ("OE3XIR-12", ack_air_pct["oe3xir"]), ("OE3XOC-12", ack_air_pct["oe3xoc"]),
            ("OE3XWJ-12", ack_air_pct["oe3xwj"]), ("DB0AIS-12 (anderes Netz)", ack_air_pct["db0ais"])],
           abb(f"ACK-Frames belegen {de(ack_lo, 0)} bis {de(ack_hi, 0)} % der gehörten Sendezeit im "
               f"OE3-Cluster, im anderen Netz nur {de(ack_air_pct['db0ais'], 1)} %"), unit=" %"))
    A(f"<p>Am OE3XWJ-12 selbst sind es {pct(ack_air_pct['oe3xwj'])} der gehörten Sendezeit. "
      f"{pct(ack_share_pct['oe3xwj'])} der ACK-Empfänge bestätigen die eigenen Pings "
      "(Quelle und Kosten: Abschnitt F6).</p>")
    ph = q2n["tx_lines"]["per_hour"]
    A(hist([(k[-2:], v) for k, v in ph.items()],
           abb(f"Konstant rund {de(sum(ph_vals) / len(ph_vals), 0)} Sendungen pro Stunde, davon "
               f"rund {de(pings / span_h, 0)} Pings "
               "(Stundenwerte 31.10. 06 Uhr bis 01.11. 07 Uhr)"), unit=" TX"))

    A(f"<h3>Funk-Stillstand am 31.10. um {st['last_rx'][11:19]}</h3>")
    A(ul([
        li("Beobachtung:", f"{de(st['gap_s'], 0)} s lang kam kein einziges Frame an (letzter Empfang "
           f"{st['last_rx'][11:19]}, nächster {st['next_rx'][11:21]}), während die vier anderen "
           f"OE3-Knoten {min(oth)} bis {max(oth)} Empfänge hörten (ACKs eingerechnet). In der Lücke "
           f"stehen {st['inside_wj']['cad_safety_timeout']} CAD_SAFETY_TIMEOUT (die Kanalabfrage "
           "meldete sich nicht innerhalb von 100 ms, der Empfänger wurde neu gestartet) und "
           f"{st['inside_wj']['radio_tx']} eigene Sendungen."),
        li("Folge:", f"Die Warteschlange wuchs von {st['queue_before_gap']} auf {st['queue_in_gap_max']} "
           "Einträge und leerte sich danach wieder. Es gab keinen Verlust (0 Drops außer den vier "
           "WX-TTL-Fällen, die zu anderen Zeiten liegen). Der Knoten erholte sich ohne Eingriff; die "
           "letzte Zeitüberschreitung steht um 17:06:06, der erste Empfang danach um "
           f"{st['next_rx'][11:21]} (ein beschädigtes Frame, FCS:0000, und zwei RX-Fehler)."),
        li("Außerdem:", f"{st['inside_wj']['eth_stall']} ETH-Stalls (498 ms, 17:04:54 und 17:05:56) im "
           "selben Fenster. Ursache unbekannt, Vermutung: gemeinsamer SPI-/Interrupt-Engpass zwischen "
           "Funkchip und Ethernet."),
        li("Vergleich (Vermutung):", "OE3XIA-12 zeigt ein ähnliches Muster, Empfang tot bei laufendem CAD, "
           "dort 35 Mal und mit Erholung nach einem Standby-plus-CAD-Zyklus. Ob beides dieselbe Ursache "
           "hat, belegt das Log nicht; Details im Bericht zu OE3XIA-12."),
        li("Urteil:", chip("watch") + f" Einmalig in {de(span_h, 1)} h und ohne Schaden, aber ungeklärt; "
           f"{st['cad_safety_timeout_total']} CAD_SAFETY_TIMEOUT insgesamt, "
           f"{st['inside_wj']['cad_safety_timeout']} davon in diesem Fenster."),
    ]))
    A(explain_h(255, draw_stall(st["gap_s"], st["inside_wj"]["cad_safety_timeout"], st["queue_in_gap_max"],
                                min(oth), max(oth), st["last_rx"][11:19], st["next_rx"][11:19]),
                abb("Während des Stillstands läuft der Zustandsautomat weiter, aber es "
                    "kommt nichts an und es geht nichts raus")))

    A(f"<h3>Ethernet: {len(flaps)} Link-Flaps</h3>")
    A(ul([
        li("Beobachtung:", f"{len(flaps)} mal ging der Ethernet-Link für 1 bis 4 s herunter und wieder hoch, alle "
           "zwischen 31.10. 18:18 und 01.11. 05:33, keiner in den ersten zwölf Stunden. Die IP-Adresse "
           "blieb dieselbe (0 DHCP-Ereignisse, Vermutung: feste Adresse)."),
        li("Wirkung:", "Keine erkennbare: nur 2 der 9 Flaps liegen zeitlich bei einem Keepalive ohne "
           "Antwort, beide davor. ETH-Sendefehler: 1 (beim Start, 1,6 s)."),
        li("Urteil:", chip("watch") + " Kabel, Switch oder Spannungsversorgung am Standort prüfen."),
    ]))
    A(explain_h(215, draw_eth(flaps, len(flaps), hn["gateway"]["keepalive_ok"],
                              hn["gateway"]["beat_after_keep_missing"],
                              hn["gateway"]["beat_miss_pct_of_keepalives"]), abb("Neun kurze Link-Aussetzer in der zweiten Hälfte der Aufnahme, "
                                   "der Server-Kontakt blieb im Wesentlichen erhalten")))

    A("<h3>Firmware ohne CAD im Umfeld</h3>")
    A(ul([
        li("Beobachtung:", f"{fwc['nodes_pre_cad']} von {fwc['nodes_total']} gehörten Knoten "
           f"({pct(fwc['nodes_pre_cad_pct'])}) laufen mit einer Firmware vor 4.35p, "
           f"also ohne CAD. Sie sprechen ohne Kanalabfrage und verursachen {pct(fwc['events_pre_cad_pct'])} "
           f"der gehörten Frames und {pct(fwc['airtime_pre_cad_pct'])} der Airtime."),
        li("Wirkung:", "Kollisionen mit deren Sendungen sind möglich; ein Teil der "
           "RX-Fehler stammt vermutlich daher (Vermutung, nicht trennbar)."),
        li("Urteil:", chip("watch") + " Kein Handlungsbedarf an diesem Knoten, aber ein Grund für die "
           "hohe Zahl an Fehl-Empfängen."),
    ]))
    A(explain_h(200, draw_precad(fwc["nodes_pre_cad"], fwc["nodes_total"], fwc["events_pre_cad_pct"],
                                 fwc["airtime_pre_cad_pct"], rxe["share_pct"],
                                 [(x["call"], x["events"]) for x in fwc["top_pre_cad"][:3]]), abb("Knoten ohne CAD senden sofort, alle anderen hören vorher in den "
                                 "Kanal")))

    # ---- F2 ----------------------------------------------------------------------------------
    A('<h2 id="f2">F2 TX-Puffer mit Slots</h2>')
    A("<p><strong>Der Slot-Pool arbeitet einwandfrei.</strong> Der Pool hat 20 Slots; jede zu sendende "
      "Nachricht belegt einen, bis sie gesendet, ersetzt (Supersede: eine neuere Bake desselben "
      "Absenders überschreibt die ältere) oder wegen Alter verworfen (TTL: Verfallszeit je Priorität) "
      "wird.</p>")
    A(table(["Verbleib der Einträge", "Anzahl", "Anteil"],
            [["gesendet", de(fate["counts"]["tx_air"]), pct(fate["pct"]["tx_air"], 2)],
             ["ersetzt (Supersede)", de(fate["counts"]["superseded"]), pct(fate["pct"]["superseded"], 2)],
             ["nach TTL verworfen", de(fate["counts"]["stale_ttl"]), pct(fate["pct"]["stale_ttl"], 2)],
             ["Summe", de(fate["total_writes"]), pct(fate["sum_pct"], 2)]],
            num_cols=(1, 2)))
    bs = fate["by_src"]
    A(table(["Quelle des Eintrags", "Anzahl", "Priorität"],
            [["eigener Ping", de(bs["phone_msg"]["tx_air"]), "2"],
             ["Relay fremder Frames",
              de(bs["rx_relay"]["tx_air"] + bs["rx_relay"].get("superseded", 0)), "3 bis 5"],
             ["Server-Einspeisung (Gateway)", de(bs["udp_rx"]["tx_air"]), "3"],
             ["eigene WX-Position", de(bs["user_wx"]["tx_air"] + bs["user_wx"]["stale_ttl"]), "4"],
             ["ACK-Weiterleitung und Gateway-ACK", de(bs["rx_ack_fwd"]["tx_air"]
                                                      + bs["rx_dm_ack_gw"]["tx_air"]
                                                      + bs["rx_dm_ack_new"]["tx_air"]), "1"]],
            num_cols=(1,), caption=f"Herkunft der {de(fate['total_writes'])} Slot-Einträge (Summe stimmt)"))
    occ = q2n["occupancy"]
    A(ul([
        li("Füllstand:", f"Median 0, 90. Perzentil {occ['queued']['p90']:.0f}, Maximum "
           f"{occ['queued']['max']:.0f} von 20; mindestens 5 belegt nur {de(occ['minutes_ge_5'], 1)} min "
           "(im Funk-Stillstand um 17:04). Kein Aufbau über den Tag."),
        li("Pool-Fehler:", "0 RING_DROP_NEW, 0 RING_DROP_PRIO, 0 Überläufe, 0 hängende Slots "
           "(RING_STATUS retrying immer 0), 0 ungeklärte Einträge."),
        li("Wartezeit:", f"Eigener Ping Median {de(q2n['latency']['by_prio_s']['2']['p50'], 1)} s, "
           f"Relay {de(q2n['latency']['by_prio_s']['3']['p50'], 1)} s, HEY "
           f"{de(q2n['latency']['by_prio_s']['5']['p50'], 1)} s, ACK "
           f"{de(q2n['latency']['by_prio_s']['1']['p50'], 1)} s. Größtes Maximum 180,6 s bei einer HEY."),
        li("Doppelsendungen:", "7 Paare innerhalb von 30 min: 4 davon sind die vorgesehenen "
           "Gateway-ACK-Paare (GWACK-01), 3 sind ein Text, der nach 21,7 bis 23,4 min erneut kam. Bei den "
           f"zwei Relay-Paaren ({', '.join(de(r['gap_s']) + ' s' for r in rr['long_gap_relays'])}) "
           f"ist der Abstand länger als das Dedup-Fenster von {de(win_now, 1)} min (der Dedup-Ring ist die "
           "Liste der zuletzt gehörten Ids): Der Knoten hatte die Id vergessen. Ausgelöst hat das ein "
           "fremder Knoten, der den alten Text erneut abstrahlte (Vermutung: Warteschlange ohne TTL)."),
    ]))
    n_in = cross["per_node"]
    groups3 = cross["groups_ge3"]
    A(bars([(f"{k.upper()}-12", 100 * n_in[k]["fastest_count"] / groups3)
            for k in ("oe3xwj", "oe3xhl", "oe3xoc", "oe3xia", "oe3xir")],
           abb(f"OE3XWJ-12 sendet in {n_in['oe3xwj']['fastest_count']} von {groups3} gemeinsam "
               "gehörten Relay-Gruppen als Erster (46 %)"), unit=" %"))
    A(ul([
        li("Rang im Cluster:", f"Mittlerer Rang {de(n_in['oe3xwj']['mean_rank_0fast_1slow'], 2)} "
           "(0 = schnellster, 1 = langsamster), der beste Wert; zum Vergleich OE3XHL "
           f"{de(n_in['oe3xhl']['mean_rank_0fast_1slow'], 2)}, OE3XIR "
           f"{de(n_in['oe3xir']['mean_rank_0fast_1slow'], 2)}, OE3XOC "
           f"{de(n_in['oe3xoc']['mean_rank_0fast_1slow'], 2)}, OE3XIA "
           f"{de(n_in['oe3xia']['mean_rank_0fast_1slow'], 2)}. Letzter nur in {n_in['oe3xwj']['slowest_count']} Gruppen."),
        li("Latenz:", f"Median {de(n_in['oe3xwj']['lat_p50_in_groups_s'], 1)} s, 90. Perzentil "
           f"{de(n_in['oe3xwj']['lat_p90_in_groups_s'], 1)} s; bei den anderen Knoten liegt der Median "
           "bei 10,6 bis 23,4 s, das 90. Perzentil bei 61 bis 103 s."),
        li("Warum so schnell (Vermutung):", "Die Wartezeit hängt am Kanal vor Ort, nicht an der "
           "Firmware: wenige Kanal-Belegt-Meldungen pro Sendung (0,68). Außerdem leitet OE3XWJ-12 fast "
           "keine ACKs weiter (nur 49 ACK-Einträge gegenüber 1.421 bis 3.378 bei IA, IR, OC), weil "
           "es Bestätigungen eigener Nachrichten nicht weiterleitet (nach Code, lora_functions.cpp:653-662). "
           "Die Last dieses Pings tragen die anderen."),
        li("Nicht geprüft:", "Die Wiederholungsleiter (Retry) lief nie (0 RING_REARM); ihr Verhalten "
           "zeigt dieses Log nicht."),
        li("Urteil:", chip("ok")),
    ]))

    # ---- F3 ----------------------------------------------------------------------------------
    A('<h2 id="f3">F3 Was verlieren wir durch den Zeitverfall</h2>')
    A("<p><strong>Fast nichts.</strong> TTL ist die Verfallszeit eines Eintrags in der Warteschlange: "
      "HEY 180 s, Position 120 s, ACK 90 s, Relay 300 s. Am OE3XWJ-12 verfielen nur vier Einträge.</p>")
    A(ul([
        li("Betroffen:", "4 eigene WX-Positionen (Priorität 4), jeweils nach 120 bis 121 s: "
           "31.10. 11:31:04, 31.10. 18:07:35, 01.11. 07:20:35 und 01.11. 07:51:06. Von 50 geschriebenen "
           "gingen 46 auf Sendung, also gingen 8 % der eigenen WX-Baken nie über Funk."),
        li("Gesendet vor dem Verfall:", "In keinem der vier Fälle (0 RING_TX_READ). Es gibt also keinen "
           "Fall von &bdquo;gesendet und trotzdem zu alt&ldquo;."),
        li("Nicht betroffen:", "0 ACKs, 0 Texte, 0 Relays, 0 HEY, 0 {CET}-Drops (RING_DROP_STALE "
           "reason=cet). Der Hauptverkehr verfällt nicht."),
        li("Abmilderung:", "Alle vier gingen zum Schreibzeitpunkt per UDP zum Server (TX-UDP-Zeile "
           "vorhanden); die Internet-Sicht war nicht betroffen, nur der Funk-Weg."),
        li("Kein Engpass dahinter:", "Die Wartezeit der Priorität 4 ist im Median 9,6 s, "
           "99. Perzentil 94,9 s, Maximum 117,0 s; 5 Positionen kamen auf 80 % der TTL. Warum diese vier "
           "warten mussten, erklärt die Ursachenanalyse eigener Baken (verify/rca_own_beacons.md): Eigene "
           "Baken der Priorität 4 und 5 bekommen eine feste Sendepause von 5,5 s, die jeder empfangene "
           "Frame neu startet, und der CSMA-Altersboden gilt für sie nicht; ein Relay derselben Priorität "
           "darf nach 8 s die kurze Suche (150 ms) nutzen. Am OE3XWJ-12 ist der Unterschied sichtbar "
           "(eigene WX-Wartezeit im Median 34,3 s, relayte Positionen 9,4 s), führt aber nur selten zum "
           "Verlust (4 von 50), weil kein ACK-Rückstau besteht."),
        li("CSMA-Altersboden:", "Ab 20 s Wartezeit wird die Warte-Verzögerung für Priorität 1 bis 3 "
           "verkürzt. Am OE3XWJ-12 trat das nur 122 mal auf (64 Episoden), weil die Wartezeiten "
           "kurz sind (6,2 % der Sendungen über 20 s; IA, IR, OC: 51 bis 63 %). Keine Kollisionsspur, "
           "neutral."),
        li("Urteil:", chip("ok")),
    ]))

    # ---- F4 ----------------------------------------------------------------------------------
    A('<h2 id="f4">F4 Was gewinnen wir durch den Zeitverfall</h2>')
    A("<p><strong>Am OE3XWJ-12 ist der Gewinn klein, weil nichts staut.</strong> Der Verfall spart "
      "4,6 s Sendezeit (0,15 % der eigenen Sendezeit).</p>")
    A(ul([
        li("Modell ohne TTL:", "Die 4 Positionen wären mit 123 bis 132 s Alter gesendet worden "
           "(Maximum 132 s). 33 von 2.317 frischen Frames (1,4 %) wären im Median 5,3 s später "
           "gekommen. Das Modell gibt die echte Auswahl zu 100 % wieder."),
        li("Größter Gewinn:", f"Das {CET}-Gate (150 s) hat 6 mal eine alte Zeit-Kopie am Weiterleiten "
           "gehindert (Abschnitt F5). Ohne Gate hätte der Knoten sie in den Cluster zurückgeflutet."),
        li("Platz im Pool:", "Mit 8 von 20 Slots höchstens war nie Überlauf-Gefahr; der Gewinn ist "
           "Frische, kein Überlaufschutz."),
        li("Spielraum:", "ACK-TTL 90 s, höchste ACK-Wartezeit 28,2 s; Relay-TTL 300 s, höchste 163,9 s; "
           "genug Reserve."),
        li("Urteil:", chip("ok")),
    ]))

    # ---- F5 ----------------------------------------------------------------------------------
    A('<h2 id="f5">F5 Zeitsignal</h2>')
    A("<p><strong>Das Zeitsignal kommt korrekt an und OE3XWJ-12 sendet nichts Altes.</strong> "
      f"{CET} ist das Zeit-Telegramm des Servers OE1XAR-33 (alle 607 s; der Inhalt ist UTC, "
      "obwohl &bdquo;CET&ldquo; davorsteht). Alte Kopien kommen aber von außen herein.</p>")
    A(ul([
        li("Empfang:", f"{q5['udp_got']} von {q5['expected_in_window']} Telegrammen kamen per Server-Weg "
           f"(UDP), {q5['udp_missed']} fehlten (22:34:10 und 05:18:25 UTC); beide kamen frisch per Funk "
           f"und wurden weitergeleitet. Die Funkkopien: im Mittel {de(q5['rf_copies_per_id_mean'], 2)} "
           "je Telegramm."),
        li("Eigene Sendungen:", f"{q5['tx_count']} Zeit-Telegramme gesendet ({pct(q5['cet_share_of_tx_count_pct'])} "
           f"der Sendungen, {pct(q5['cet_share_of_stat_tx_pct'], 2)} der Sendezeit), davon {q5['tx_src_dist']['g']} "
           f"als Gateway und {q5['tx_src_dist']['r']} als Relay. Alter beim Senden: Median "
           f"{de(q5['tx_age_s_logger_based']['p50'], 1)} s, 90. Perzentil "
           f"{de(q5['tx_age_s_logger_based']['p90'], 1)} s, Maximum "
           f"{de(q5['tx_age_s_logger_based']['max'], 1)} s; {q5['tx_age_gt150_n']} über 150 s."),
        li("Redundanz:", f"{q5['tx_after_overheard_gateway_radiation_n']} von {q5['tx_count']} Abstrahlungen "
           f"({pct(q5['tx_after_overheard_pct'])}) folgten einer bereits gehörten "
           "Abstrahlung desselben Telegramms. Die Regel &bdquo;ich bin einziger Anbieter&ldquo; "
           "(Fall A) greift bei allen Server-Slots, das Telegramm geht trotzdem raus. Das ist das "
           "bekannte Thema GWRX-01."),
    ]))
    A(explain_h(230, draw_gwrad(cl_rad["radiation_class_per_telegram_mean"]["our_gateway"],
                                cl_rad["radiation_class_per_telegram_mean"]["foreign_gateway"],
                                cl_rad["radiations_per_telegram"]["p50"],
                                cl_rad["radiation_airtime_per_telegram_s"], q5["tx_count"],
                                q5["tx_after_overheard_gateway_radiation_n"],
                                q5["tx_after_overheard_pct"]),
                abb("Ein Telegramm wird von allen fünf eigenen und im Mittel "
                    f"{de(cl_rad['radiation_class_per_telegram_mean']['foreign_gateway'], 1)} direkt "
                    "gehörten fremden Gateways abgestrahlt (der Median 8 aus dem Cluster-Bericht enthält "
                    "zusätzlich aus Relay-Pfaden erschlossene)")))
    oc = cs["outcome"]
    A("<h3>Alte Zeit-Kopien von außen</h3>")
    A(ul([
        li("Beobachtung:", f"{cs['stale_n']} per Funk empfangene Kopien waren {de(stale_a, 1)} bis "
           f"{de(stale_b, 1)} min alt, also lange über der 150-s-Grenze. Alle {cs['via_oe3mtc10_in_path']} haben "
           f"OE3MTC-10 im Pfad (bei {cs['oe3mtc10_last_station']} als letzte Station)."),
        li("Warum sie überhaupt ankommen (Vermutung):", f"Der Dedup-Ring des OE3XWJ-12 fasst 100 Ids und "
           f"reicht nur {de(win_now, 1)} min zurück. Die Kopien sind älter; der Knoten hat die Id offenbar "
           "vergessen und behandelt sie als neu (DUP:n im Log ist die Beobachtung, das Vergessen die Deutung)."),
        li("Was das Gate macht:", f"{oc.get('cet', 0)} Kopien verworfen (Alters-Gate, RLY q=cet), "
           f"{oc.get('loop', 0)} gestoppt, weil der eigene Ruf schon im Pfad stand (Schleifensperre, q=loop), "
           f"{oc.get('hop0', 0)} hatten Hop 0 und waren ohnehin nicht weiterzuleiten. Nichts davon ging auf "
           "Sendung."),
        li("Vermutung zur Quelle:", "OE3MTC-10 hält die Kopie in der Warteschlange oder sendet nach dem "
           "Ablauf seines eigenen Dedup-Rings erneut. Ein Alters-Gate fehlt dort offenbar (ältere Firmware, "
           "Betreiber-Aktion)."),
        li("Insgesamt:", f"{q5['rf_age_gt150_n']} von {q5['rf_valid_frames']} Funk-Telegrammen "
           f"({pct(q5['rf_age_gt150_pct'])}) waren älter als 150 s. Letzte Stationen: "
           + ", ".join(f"{c} {n}" for c, n in q5_top) + "."),
    ]))
    A(explain_h(210, draw_stale(cs["stale_n"], span_h, stale_a, stale_b, oc.get("cet", 0),
                                oc.get("loop", 0), oc.get("hop0", 0)),
                abb(f"Eine {de(stale_a, 0)} bis {de(stale_b, 0)} min alte Zeit-Kopie läuft über OE3MTC-10 herein "
                    "und wird am Gate des OE3XWJ-12 gestoppt")))
    why = {"cet": "Alters-Gate", "loop": "Schleifensperre", "hop0": "Hop 0"}
    gate_rows = []
    for r in stale:
        gate_rows.append([r["logger"][8:10] + "." + r["logger"][5:7] + ". " + r["logger"][11:19],
                          f"{de(r['age_s'])} s ({de(r['age_s'] / 60, 1)} min)", r["stamp_utc"][11:],
                          ", ".join(r["path"][1:]), why.get(r["outcome"], "unbekannt"), str(r["line"])])
    A("<details><summary>Alle " + str(cs["stale_n"]) + " alten Zeit-Kopien (Tabelle)</summary>"
      + table(["Empfang (Logger)", "Alter", "Stempel im Telegramm (UTC)", "Pfad nach OE1XAR-33",
               "Ergebnis", "Log-Zeile"], gate_rows, num_cols=(5,),
              caption="Zeit-Kopien über 1000 s Alter am OE3XWJ-12 und was die Firmware damit tat")
      + "</details>")
    A("<h3>Warum der Dedup-Ring so kurz ist</h3>")
    A(ul([
        li("Rechnung:", f"Der Ring hat 100 Slots. {de(adds)} Ids wurden in {de(span_h, 1)} h aufgenommen "
           f"({de(rate, 2)} pro Minute), davon {de(ack_new)} ACK-Ids ({pct(dd_share, 0)}), fast alle "
           f"zu den eigenen Pings. Das ergibt {de(win_now, 1)} min Fenster."),
        li("Ohne die Ping-ACKs:", f"Rund {de(win_wo, 0)} min (Rechnung, setzt voraus, dass sich "
           f"sonst nichts ändert). Die alten Kopien (ab {de(stale_a, 1)} min) würden dann als Duplikat "
           "erkannt."),
        li("Urteil:", chip("watch") + " Keine Schäden, aber der Ping verkürzt das Gedächtnis "
           "des Knotens spürbar."),
    ]))
    A(explain_h(200, draw_dedup(dd_share, win_now, win_wo, stale_a, stale_b),
                abb(f"Die ACK-Ids der Pings füllen {de(dd_share, 0)} % des Dedup-Rings und verkürzen sein "
                    "Gedächtnis")))
    A("<p><strong>Urteil:</strong> " + chip("watch") + " Das Zeitsignal selbst ist in Ordnung; "
      "Folgen der alten Kopien von außen und die Mehrfach-Abstrahlung bleiben zu beobachten.</p>")

    # ---- F6 ----------------------------------------------------------------------------------
    A('<h2 id="f6">F6 Manipulationen dieses Knotens</h2>')
    A('<h3>Der Ping an 2025 (Kern dieses Berichts)</h3>')
    A("<p><strong>OE3XWJ-12 sendet jede Minute einen Ping, und die Antworten belasten den ganzen "
      "Cluster.</strong> Im Log steht <code>[PING]...send Ping to 2025</code>, danach "
      f"<code>OE3XWJ-12&gt;2025:{PING}</code> (36 Byte, Hop 4, Priorität 2).</p>")
    A(ul([
        li("Menge:", f"{de(pings)} Pings in {de(span_h, 1)} h, jede Minute (z. B. 06:27:25, "
           "06:28:34; der Sendezeitpunkt schwankt mit der Wartezeit in der Schlange), alle gesendet. Nur "
           f"{de(pg['distinct_ping_ids'])} verschiedene Message-Ids, weil der Zähler im Lauf einmal "
           "überläuft."),
        li("Antworten:", f"Am OE3XWJ-12 selbst erhielten {de(wjp['with_ack'])} der {de(pings)} Pings "
           f"({pct(100 * wjp['with_ack'] / pings)}) innerhalb von 10 min mindestens ein ACK; das erste "
           f"kommt im Median nach {de(wjp['first_ack_s']['p50'], 1)} s (90. Perzentil "
           f"{de(wjp['first_ack_s']['p90'], 1)} s). Über alle fünf Logs zusammen sind es im Mittel "
           f"{de(ev_ack['ack_ids_per_ping_mean'], 1)} verschiedene ACK-Ids je Ping (Median "
           f"{ev_ack['median']:.0f}, höchstens {ev_ack['max']}), am OE3XWJ-12 allein "
           f"{de(wjp['frames_mean'], 1)}."),
        li("Wer antwortet:", f"Fremde Gateways, nicht unsere fünf: von {de(pg['ack_format']['receptions'])} "
           "ACK-Empfängen auf Pings tragen "
           f"{de(pg['ack_format']['gateway_byte_01'])} das Gateway-Kennzeichen (Byte 10 = 0x01) und alle das "
           "Server-Bit; keiner der fünf Knoten sendet selbst ein ACK (verify/q6.md). Ein Gateway der "
           "aktuellen Firmware quittiert <code>{ping}</code> gar nicht "
           "(<code>src/lora_functions.cpp:2169-2173</code>, auch im Upstream). Vermutung: die "
           "quittierenden Gateways laufen mit anderen Builds."),
        li("Weiterleitung:", f"{de(ack_relay)} ACK-Weiterleitungen der Cluster-Knoten gehören zu diesen "
           f"Pings, also {de(per_ack_relay, 1)} je Ping. ACK-Weiterleitungen haben keinen "
           "Overhear-Cancel (das Zurückziehen eines wartenden Relays, wenn ein Nachbar es schon gesendet "
           "hat); sie werden alle gesendet."),
        li("Kosten je Ping im Cluster:", f"{de(per_tx, 1)} Sendungen und {de(per_air, 2)} s Sendezeit "
           f"({de(toa_ping, 2)} s für den Ping selbst, {de(toa_ack, 3)} s je ACK, aus SF11/BW250/CR4:6 "
           "berechnet; dasselbe Modell trifft die STAT-Summe des Clusters auf 0,1 %: 13.564 s gegen 13.546 s)."),
        li("Anteil, ACK-Weiterleitungen allein:", f"{de(ack_relay)} von {de(total_tx)} Cluster-Sendungen "
           f"({pct(ack_tx_share)}) und {de(ack_air, 0)} von {de(total_air, 0)} s Sendezeit "
           f"({pct(ack_air_share)})."),
        li("Anteil, mit den eigenen Pings:", f"{de(ping_tx_all)} Sendungen ({pct(tx_share)}) und "
           f"{de(ping_air, 0)} s ({pct(air_share)}). Die Zahl &bdquo;48,5 %&ldquo; im Überblick meint genau "
           "diese Summe aus Pings und ACK-Weiterleitungen."),
        li("Eigene Last:", f"Am OE3XWJ-12 selbst sind es {de(pings)} von {de(own['tx_lines'])} Sendungen "
           f"({pct(100 * pings / own['tx_lines'])}) und {de(wj_ping_air, 0)} s von {de(own['airtime_s'], 0)} s "
           f"Sendezeit ({pct(100 * wj_ping_air / own['airtime_s'])})."),
    ]))
    A("<p><strong>Urteil:</strong> " + chip("act") + " Die Entscheidung liegt beim Betreiber; sie hat "
      "drei Seiten: das Testmuster am OE3XWJ-12, der Ping-Zähler der RAK4631-Firmware (nächster "
      "Abschnitt) und die Firmware der fremden Gateways.</p>")
    A(explain_h(230, draw_ping(per_tx, per_air, ev_ack["ack_ids_per_ping_mean"]),
                abb("Ein Ping löst im Cluster rund sieben Sendungen und gut zwei Sekunden Sendezeit aus")))
    pt = ps["by_node"]
    node_names = [("oe3xhl", "OE3XHL-12"), ("oe3xia", "OE3XIA-12"), ("oe3xir", "OE3XIR-12"),
                  ("oe3xoc", "OE3XOC-12"), ("oe3xwj", "OE3XWJ-12")]
    A(bars([(nm, 100 * pt[k]["ping_tx"] / pt[k]["tx"]) for k, nm in node_names],
           abb("Anteil der Ping-bezogenen Sendungen an allen Sendungen des Knotens (ACK-Weiterleitungen; "
               "beim OE3XWJ-12 zusätzlich die eigenen Pings)"),
           unit=" %"))
    A("<h3>Unterschied ESP32 / RAK4631: Ping-Zähler (Entscheidung offen)</h3>")
    A("<p><strong>Der Ping hört auf dem RAK4631 nie von selbst auf.</strong> Das ist ein Unterschied "
      "der Plattformen, kein bestätigter Fehler; welches Verhalten gewollt ist, muss der Betreiber "
      "entscheiden.</p>")
    A(ul([
        li("ESP32:", "Die Schleife sendet nur, solange <code>node_pingcount &gt; 0</code> gilt, und zählt "
           "nach jedem Ping herunter (<code>src/esp32/esp32_main.cpp:5264,5285</code>). Der Zähler "
           "startet mit <code>--ping start</code> bei höchstens <code>--pingmax</code> (Obergrenze 5, "
           "<code>PING_MAX</code>, <code>src/configuration_global.h:290</code>); danach ist Schluss."),
        li("RAK4631:", "Die nRF52-Schleife prüft nur <code>node_pingtime &gt; 29</code> und ein gesetztes "
           "<code>node_pingcall</code> (<code>src/nrf52/nrf52_main.cpp:2717-2737</code>); einen Zähler "
           "gibt es dort nicht. Sie pingt im eingestellten Abstand (hier 60 s), solange ein Ruf gesetzt ist."),
        li("Folge:", "<code>--ping stop</code> setzt nur den Zähler auf 0 "
           "(<code>src/command_functions.cpp:3886</code>) und bleibt auf dem RAK4631 ohne Wirkung. Beendet "
           "wird der Ping nur mit <code>--pingcall NONE</code> (löscht den Ruf, "
           "<code>src/command_functions.cpp:3806-3831</code>). Der Upstream-Stand 147812e0 hat denselben Code."),
        li("Entscheidung (offen):", "Soll der RAK4631 wie der ESP32 nach einer festen Zahl Pings aufhören, "
           "oder ist der Dauerping auf diesen Knoten so gewollt? Bis dahin gilt: ein Test mit "
           "<code>--pingcall</code> auf einem RAK4631 braucht ein ausdrückliches Ende."),
        li("Status:", chip("watch", "Entscheidung offen") + " Quelltext gelesen und zum Log passend; "
           "nicht auf dem Prüfstand ausprobiert."),
    ]))
    A(explain_h(215, draw_pingcount(pings, span_h),
                abb("Auf dem ESP32 endet der Ping nach höchstens 5 Stück, auf dem RAK4631 läuft er weiter, "
                    "bis der Ruf gelöscht wird")))
    A("<h4>Was ein langsameres Intervall brächte (Modell, keine Messung)</h4>")
    iv = [(1, "1 min (heute)"), (5, "5 min"), (10, "10 min"), (30, "30 min")]
    iv_rows = []
    iv_items = []
    save10 = 0.0
    for m, lab in iv:
        tx_p = ping_tx_all / m
        air_p = ping_air / m
        new_tx = total_tx - ping_tx_all + tx_p
        new_air = total_air - ping_air + air_p
        iv_rows.append([lab, de(tx_p), pct(100 * (total_tx - new_tx) / total_tx),
                        de(air_p, 0), pct(100 * (total_air - new_air) / total_air)])
        iv_items.append((lab, tx_p))
        if m == 10:
            save10 = 100 * (total_tx - new_tx) / total_tx
    A(f"<p>Modell: Würde der Ping nur alle 10 min gesendet, fielen {pct(save10)} der Cluster-Sendungen "
      "weg. Das ist eine lineare Hochrechnung der gemessenen Ping-Last (Pings plus ACK-Weiterleitungen), "
      "keine Messung; sie nimmt gleiche ACK-Zahlen je Ping und gleiche Wartezeiten an.</p>")
    A(table(["Intervall", "Ping-Sendungen", "Weniger Sendungen", "Ping-Sendezeit (s)", "Weniger Sendezeit"],
            iv_rows, num_cols=(1, 2, 3, 4),
            caption=f"Modell: lineare Hochrechnung über {de(span_h, 1)} h, Pings plus ACK-Weiterleitungen, Anteil am Cluster; Annahme: ACKs je Ping und Wartezeiten bleiben gleich"))
    A(bars(iv_items, abb("Modell: Ein Ping alle 10 min würde die Ping-bezogenen Sendungen im Cluster "
                         "um 90 % senken"), unit=" TX"))
    A("<h4>Positiv, negativ, neutral</h4>")
    A('<div class="grid3">')
    A('<div class="card pos"><h3>Positiv</h3>' + ul([
        li("Relay-Fluss:", f"{de(q6['totals']['rly_tx'])} Relays eingereiht, {de(relays_sent)} gesendet, "
           "1 ersetzt, 0 verloren."),
        li("Dedup:", f"Jeder neue Frame kommt im Mittel {de((q6['totals']['rx_dup_foreign'] + q6['totals']['rx_dup_echo']) / q6['totals']['rx_new'], 1)} "
           "mal als Duplikat wieder (inklusive eigener Echos); der Knoten verwirft sie ohne Verarbeitung."),
        li("Flut-Begrenzung:", "83 Frames vom Gateway-Filter (Ping/Pong und weitere) nicht weitergeleitet, 178 ohne "
           "Restweite (Hop 0) und 290 mit zu langem Gateway-Pfad (gwcap)."),
        li("Schleifensperre:", f"{q6['totals']['rly_loop']} Treffer, davon {cs['outcome'].get('loop', 0)} bei "
           "alten Zeit-Kopien mit eigenem Ruf im Pfad."),
        li("Alters-Gate:", f"{cs['outcome'].get('cet', 0)} Stopps (Abschnitt F5)."),
        li("Tempo:", "Relay-Wartezeit im Median 9,6 s gegenüber 10,3 bis 22,5 s auf den anderen Knoten."),
    ]) + "</div>")
    A('<div class="card neg"><h3>Negativ</h3>' + ul([
        li("Ping-Last:", f"Pings plus ACK-Weiterleitungen {pct(tx_share)} der Cluster-Sendungen und "
           f"{pct(air_share)} der Sendezeit (ACK-Weiterleitungen allein {pct(ack_tx_share)} / "
           f"{pct(ack_air_share)})."),
        li("Gedächtnis:", f"{pct(dd_share, 0)} der Dedup-Ids sind Ping-ACKs; Fenster {de(win_now, 0)} statt "
           f"rund {de(win_wo, 0)} min."),
        li("Einziger-Anbieter-Regel:", f"{n_veto} Relays (von {de(relays_sent)}, {pct(100 * n_veto / relays_sent)}) "
           f"liefen trotz gehörter Fremdkopie, dazu {n_veto_ack} ACKs: zusammen {opp['opportunity_air_s_total']} s = "
           f"{pct(opp['opportunity_share_of_node_tx_air_pct'])} der eigenen Sendezeit (Obergrenze)."),
        li("Server-Einspeisung:", "290 von 290 Einspeisungen gesendet, 56 % der Zeit-Telegramme nach "
           "gehörter Abstrahlung."),
        li("Eigene WX:", f"{q6['totals']['dropstale_ttl']} von 50 nie gesendet (8 %)."),
    ]) + "</div>")
    A('<div class="card neu"><h3>Neutral</h3>' + ul([
        li("Gateway-ACK-Paare:", "4 Paare, absichtlich doppelt (GWACK-01)."),
        li("Supersede:", "1 HEY ersetzt (nach 6 s)."),
        li("Kein eigener HEY:", "0 automatische Positions-Baken in 25,5 h; nur die WX-Position alle "
           "30 min (46 von 50 gesendet). Gewollt?"),
        li("Overhear-Cancel:", "0 CANCEL, 423 REFUSE-Zeilen. Der Grund ist Fall A bei allen 2.276 "
           "NEED-Zeilen (1.986 Relays plus 290 Einspeisungen), nicht ein abgeschalteter "
           "Schalter; das lässt sich aus dem Log nicht trennen."),
    ]) + "</div></div>")
    A("<h4>Einziger-Anbieter-Regel (Fall A)</h4>")
    A(ul([
        li("Mechanismus:", "Vor dem Weiterleiten fragt der Knoten seine Nachbar-Matrix, welche "
           "Stationen den Frame noch brauchen und für welche er der einzige bekannte Anbieter ist. "
           "Ist das mindestens eine, bleibt sein Relay bestehen, auch wenn ein Nachbar den Frame schon "
           "gesendet hat."),
        li("Hier:", "Im Median stehen 9 Stationen in dieser &bdquo;allein&ldquo;-Menge. "
           "OE3MIF-12 und OE3NRA-1 stehen darin bei 99,8 % beziehungsweise 100,0 % der "
           f"{de(q6['persistent_alone']['need_lines'])} Fälle (verify/q6.md, Nr. 8), obwohl IA, IR und OC "
           "sie ebenfalls direkt hören."),
        li("Warum (Vermutung):", "Die Erklärung &bdquo;12-h-Fenster der Nachbar-Matrix&ldquo; ist widerlegt: "
           "Fall A gilt schon in der ersten Betriebsstunde bei 100 % der Fälle, und aus der Gegenrichtung "
           "erschlossene Kanten können die Rückverbindung liefern. Wahrscheinlicher liegt es an der "
           "Regel für die Abhängigen und am Test auf alternative Anbieter "
           "(<code>src/nbr_matrix.cpp:1777</code>, <code>:1816-1852</code>)."),
        li("Urteil:", chip("watch") + " Design-Frage, kein Fehler; netzweit im Überblick."),
    ]))
    A(explain_h(232, draw_veto(n_veto, relays_sent, n_veto + n_veto_ack, opp["opportunity_air_s_total"],
                        100 * n_veto / relays_sent, opp["opportunity_share_of_node_tx_air_pct"]),
              abb("Der Relay bleibt, auch wenn ein Nachbar denselben Frame schon gesendet hat")))

    # ---- Verdacht ----------------------------------------------------------------------------
    A('<h2 id="bugs">Verdachtsfälle Firmware</h2>')
    A("<p>Nur Fälle mit Beleg im Log und gelesener Code-Stelle. Der Code wurde am Repo-Stand "
      "651e0920 gelesen; der Build auf dem Knoten (Stempel 20260929) kann abweichen. Der "
      "Ping-Zähler-Unterschied zwischen ESP32 und RAK4631 steht in F6 als offene Entscheidung, nicht "
      "als Fehler.</p>")
    A("<h3>V1 Funk-Stillstand mit CAD_SAFETY_TIMEOUT</h3>")
    A(ul([
        li("Beobachtung:", f"Siehe Abbildung im Abschnitt Gesundheit: {de(st['gap_s'], 0)} s ohne Empfang, "
           f"{st['inside_wj']['cad_safety_timeout']} Zeitüberschreitungen, {st['inside_wj']['radio_tx']} "
           "eigene Sendungen, danach Erholung ohne Eingriff."),
        li("Code-Stelle:", "Ausgabe in <code>src/nrf52/nrf52_main.cpp:1794</code> (kein CadDone innerhalb von "
           "100 ms, Flags zurücksetzen, Empfang neu starten)."),
        li("Offen:", "Warum der Chip in dieser Phase weder CAD fertigstellt noch empfängt, ist aus dem Log "
           "nicht erkennbar (Funkchip, Firmware, Spannung)."),
        li("Status:", chip("watch", "Verdacht") + f" einmalig in {de(span_h, 1)} h."),
    ]))
    A("<h3>V2 ACK-Weiterleitungen ohne Overhear-Cancel</h3>")
    A(ul([
        li("Beobachtung:", f"{de(ack_relay)} ACK-Weiterleitungen für die Pings, alle gesendet; "
           "ACKs werden ohne Nachbarbewertung eingereiht."),
        li("Code-Stelle:", "<code>src/lora_functions.cpp:656-662</code> (ACK als Kind &bdquo;OTHER&ldquo;, "
           ":661), Cancel-Suche nur für <code>:</code>/<code>!</code>/<code>@</code>-Frames in Relay-Slots "
           "(<code>:1327-1329</code>, <code>:1359</code>)."),
        li("Status:", chip("watch", "Verdacht") + " Netzthema, im Überblick; siehe Abbildung beim Ping."),
    ]))
    A("<h3>V3 Einziger-Anbieter-Regel und kurzer Dedup-Ring</h3>")
    A(ul([
        li("Beobachtung:", f"Fall A bei allen {de(q6['persistent_alone']['need_lines'])} NEED-Zeilen; "
           f"Dedup-Fenster {de(win_now, 1)} min."),
        li("Code-Stellen:", "<code>src/nbr_matrix.cpp:1777-1781,1816-1852</code>; Dedup <code>src/dedup_functions.cpp:27-66</code> "
           "(100 Slots, zählend)."),
        li("Status:", chip("watch", "Verdacht") + " Design und Parameter, keine Fehlfunktion; "
           "Abbildungen in F5 und F6."),
    ]))

    # ---- Nachbarn ----------------------------------------------------------------------------
    A('<h2 id="nachbarn">Nachbarn und Hardware-Inventar</h2>')
    A("<p>Direkter Empfang ist, was der Knoten als letzten Hop gehört hat. RSSI und SNR gelten nur "
      "dafür; Median über alle Frames dieser Station.</p>")

    def row_for(call: str, n: int | None, rssi: float | None, snr: float | None,
                hw_log: str | None, fw_log: str | None, is_neighbour: bool = True) -> list[str]:
        m = master.get(call)
        if call == "OE3XIR-12":
            place = "keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg)"
            alt = "keine Stammdaten"
            d_km = "keine Stammdaten"
        elif m:
            place = m["description"] or f"{de(float(m['lat']), 4)} N / {de(float(m['lon']), 4)} E"
            alt = f"{m['altitudeM']} m"
            d_km = f"{de(float(dist[call]), 1)} km" if dist.get(call) else "unbekannt"
        else:
            place = "keine Stammdaten"
            alt = "keine Stammdaten"
            d_km = "keine Stammdaten"
        hw = (m["hardware"] if m else (f"{hw_log} (Log)" if hw_log else "unbekannt"))
        if m and m["firmware"]:
            fw = m["firmware"]
        elif fw_log:
            v = fw_log.split(":")
            fw = f"4.{v[0][-2:]}{v[1]} (Log)" if len(v) == 2 else fw_log
        else:
            fw = "unbekannt"
        rs = (f"{rssi:.0f} dBm / {snr:.0f} dB ({de(n)} Frames)" if rssi is not None and n else "nicht direkt gehört")
        return [call, hw, fw, place, alt, d_km, rs]

    nb_rows = []
    for x in hn["neighbours"]["top"]:
        nb_rows.append(row_for(x["call"], x["n"], x["rssi_median"], x["snr_median"], x.get("hw"),
                               x.get("fw_log")))
    A("<h3>Cluster-Knoten (Verweise in diesem Bericht)</h3>")
    cl = []
    nbd = {x["call"]: x for x in hn["neighbours"]["top"]}
    for c in ["OE3XHL-12", "OE3XIA-12", "OE3XIR-12", "OE3XOC-12"]:
        x = nbd.get(c)
        cl.append(row_for(c, x["n"] if x else None, x["rssi_median"] if x else None,
                          x["snr_median"] if x else None, x.get("hw") if x else "RAK4631",
                          x.get("fw_log") if x else "40:a"))
    m0 = master[CALL]
    cl.insert(0, [CALL + " (dieser Knoten)", m0["hardware"], m0["firmware"], m0["description"],
                  f"{m0['altitudeM']} m", "0 km", "-"])
    A(table(["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz", "RSSI/SNR (direkt)"], cl))
    A("<details><summary>Alle 20 direkten Nachbarn (Tabelle)</summary>"
      + table(["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz", "RSSI/SNR (direkt)"], nb_rows)
      + "<p class=\"small\">Quelle: health/results.json (Nachbarn aus dem Pfad, Hardware und Firmware aus dem "
        "Log), Stammdaten aus master.tsv (mcmap). &bdquo;Log&ldquo; = aus dem HW- und FW-Feld der Frames. "
        "OE3MTC-10, OE3MIF-12, OE3NRA-1, OE3XDC-12 und OE1XAR-12 stehen nicht in den Stammdaten."
        "</p></details>")
    A("<p>Weitere genannte Stationen: OE1XAR-33 ist der Server (Zeitsignal), kein Knoten. "
      "OE3XWJ-94 (EBYTE E22, Firmware 4.35d, 241 m, 48,469 N / 15,6813 E, 28,8 km) ist ein "
      "benachbarter Knoten ohne CAD; er ist sechsmal die letzte Station von Zeit-Kopien, die älter als 150 s sind.</p>")

    # ---- Grenzen -----------------------------------------------------------------------------
    A('<h2 id="grenzen">Was nicht geprüft ist / Grenzen</h2>')
    A("<p><strong>Gesehen ist nur die Sicht dieses Knotens und der vier Schwester-Logs.</strong></p>")
    A(ul([
        li("Build unbekannt:", "Kein Boot-Block im Log. Slot-Pool-Marker und das Fehlen der Retry-Leiter "
           "sind aus dem Verhalten geschlossen (&bdquo;inferred&ldquo;)."),
        li("Absender der ACKs:", "ACK-Frames tragen keinen Rufzeichen-Absender. Erkennbar ist nur das "
           "Format (Gateway-ACK, Server-Bit); welche fremden Gateways es sind und welche Firmware sie "
           "haben, zeigt das Log nicht. Ob 2025 eine Gruppe ist, ebenfalls nicht."),
        li("Hochrechnung:", "Das Intervall-Szenario ist ein Modell: linear, mit gleichen Wartezeiten und "
           "gleichen ACK-Zahlen je Ping; es ist keine Messung."),
        li("Funk-Stillstand:", "Ursache nicht ermittelbar; die SPI-Hypothese ist unbelegt."),
        li("Abstrahlungen:", "Alle Zählungen über andere Gateways sind Untergrenzen: nur was die fünf "
           "Logs hörten."),
        li("Overhear-Cancel:", "Ob er auf diesem Knoten an oder aus ist, kann das Log nicht zeigen; "
           "Fall A erklärt das Schweigen in beiden Fällen."),
        li("Code-Stand:", "Quelltext vom Repo-Stand 651e0920, Knoten-Build 20260929."),
        li("Nicht prüfbar:", "Bench-Test der Ping-Zähler-Lücke; keine Übertragungen, keine "
           "Änderungen am Knoten."),
    ]))

    # ---- Fragen ------------------------------------------------------------------------------
    A('<h2 id="fragen">Offene Fragen an den Betreiber</h2>')
    A('<ol>'
      "<li><strong>Ist der Ping an 2025 ein gewollter Test?</strong> Wer hat <code>--pingcall 2025</code> "
      "gesetzt und was soll er zeigen?</li>"
      "<li><strong>Wenn ja: reicht ein langsameres Intervall?</strong> Zum Beispiel alle 10 min "
      "(Modell: minus 90 % der Ping-Sendungen). Wenn nein: <code>--pingcall NONE</code>; "
      "<code>--ping stop</code> wirkt auf dem RAK4631 nicht.</li>"
      "<li><strong>Soll der RAK4631 wie der ESP32 nach einer festen Zahl Pings aufhören?</strong> "
      "Das ist der offene Unterschied der Plattformen (F6); er braucht eine Entscheidung.</li>"
      "<li><strong>Wer ist 2025?</strong> Eine Gruppe, ein Gerät, ein Skript? Welche fremden Gateways "
      "bestätigen die Pings, und welche Firmware laufen sie (Gateways der aktuellen Firmware quittieren "
      "<code>{ping}</code> nicht)?</li>"
      "<li><strong>Sendet der Knoten absichtlich keine eigene HEY-Bake?</strong> In 25,5 h gab es "
      "nur die WX-Position alle 30 min.</li>"
      "<li><strong>Ethernet:</strong> Gibt es am Standort Kabel-, Switch- oder PoE-Probleme (9 Flaps "
      "zwischen 18:18 und 05:33)?</li>"
      "<li><strong>17:03 am 31.10.:</strong> Gab es um diese Zeit ein Ereignis am Standort "
      "(Strom, Funkstörung)?</li>"
      "<li><strong>OE3MTC-10 und OE3XWJ-94:</strong> Lassen sich beide auf eine Firmware mit "
      "Alters-Gate und CAD heben?</li></ol>")

    # ---- Dev ---------------------------------------------------------------------------------
    A('<details class="dev" id="dev"><summary>Methodik, Marker, Code-Stellen, Skripte (technisch)</summary>')
    A("<h3>Quellen und Rechenweg</h3>")
    A(ul([
        li("Ping-Anteil:", f"data/q6/results.json network.ping_share (ack_of_ping_relay {de(ack_relay)}, "
           f"ping_orig_or_relay {de(pings)}, total_tx {de(total_tx)}, ping_air_s {de(ping_air, 0)} von "
           f"{de(total_air, 0)} s, das sind Pings plus ACK-Weiterleitungen). Pro Ping "
           f"{de(ack_relay)}/{de(pings)} = {de(per_ack_relay, 2)} ACK-Weiterleitungen. Nur ACK-Weiterleitungen: "
           f"{de(ack_relay)}/{de(total_tx)} = {pct(ack_tx_share)}; Sendezeit {de(ack_relay)} x ToA(12 B) = "
           f"{de(ack_air, 0)} s = {pct(ack_air_share)} (verify/q6.md Nr. 4: 2.735 s). Mit Pings "
           f"{pct(tx_share)} / {pct(air_share)}."),
        li("Sendezeit-Modell:", f"ToA(36 B) = {de(toa_ping, 3)} s, ToA(12 B) = {de(toa_ack, 3)} s "
           f"(SF11, BW250, CR4/6, 8 Präambel-Symbole, expliziter Header, CRC); {de(pings)} x + {de(ack_relay)} x "
           f"= {de(model_air, 0)} s (q6 rechnet {de(ping_air, 0)} s mit demselben Modell; Gegenprobe Cluster: "
           "13.563 s gegen 13.546 s aus den STAT-Zeilen, 0,1 % Abweichung)."),
        li("ACK-Ids je Ping:", "data/extra/oe3xwj_ping.json (lib/oe3xwj_raw.py, Roh-Log-Durchlauf über die "
           "fünf Logs): je Ping die TX-LoRa-Zeile mit Id, dazu alle ACK-Zeilen "
           "(<code>[LOG] 012 A x&lt;ackid&gt; Hnn x&lt;acked-id&gt; gg</code>) mit dieser acked-Id von 5 s vor "
           f"bis 30 min nach dem Ping, verschiedene ACK-Ids je Ping-Ereignis: Mittel "
           f"{de(ev_ack['ack_ids_per_ping_mean'], 2)}, Median {ev_ack['median']:.0f}, Maximum {ev_ack['max']}. "
           f"Am OE3XWJ-12 allein (600 s): {de(wjp['frames_mean'], 2)}. Die Zahl 4,64 der Verifikation "
           f"(verify/q6.md Nr. 5) gruppiert nach Message-Id; bei {de(pings)} Pings gibt es nur "
           f"{de(pg['distinct_ping_ids'])} verschiedene Ids (der Zähler läuft über), eine Id fasst also "
           f"zwei Pings zusammen. Je Id: {de(pg['cluster_by_acked_id']['ack_ids_per_ping_mean'], 2)}; "
           "die Seite nennt den Wert je Ping."),
        li("ACK-Anteil:", "data/health/ack_attribution.json: Präfix F5CE7 = GW-ID-Teil der Message-Ids dieses "
           "Knotens; 19.946 von 20.166 ACK-Empfängen (98,9 %). Das Präfix kennzeichnet alle eigenen Ids, "
           "die Zuordnung zu Pings ergibt sich aus der Id-Übereinstimmung mit den Ping-Sendungen "
           "(Ids wiederholen sich nach etwa 16 h). ACK-Format: data/extra/oe3xwj_ping.json ack_format."),
        li("Dedup-Fenster:", f"data/extra/oe3xwj_rerelay.json: RX_DEDUP_ADD {de(adds)} in "
           f"{de(rr['span_min'], 1)} min; ACK-Ids neu (DUP:n, data/q6) {de(ack_new)}; Fenster = 100 / "
           f"(Adds pro Minute) = {de(win_now, 1)} min; ohne ACK-Ids {de(win_wo, 1)} min (Rechnung). "
           "Re-Relays mit langem Abstand: <code>[LOG] TX x&lt;id&gt; ... src&#61;r</code>, gleiche Id, Abstand "
           "ab 600 s."),
        li("Stale {CET}:", "data/extra/oe3xwj_cet_stale.json: alle <code>[CET];rx</code> mit age_s größer 1000, "
           "samt vorausgehender [LOG]-Zeile (Pfad, Stempel) und der folgenden <code>RLY ... q=</code>-Zeile "
           "(cet = Alters-Gate, loop = Schleifensperre, hop0). Die Spanne dieses Knotens "
           f"({de(stale_a, 1)} bis {de(stale_b, 1)} min) weicht von den Netz-Angaben anderer Berichte ab; "
           "für diesen Bericht zählen die Rohzeilen. Gesamtstatistik: data/q5/results.json."),
        li("Rang im Cluster:", "q2/results.json network.cross_node; 672 / 1.462 = 46,0 % (Gruppen mit "
           "mindestens 3 Knoten). Der Anteil an den 1.232 Gruppen, in denen WJ vorkommt, wäre 54,5 %."),
        li("Funk-Stillstand:", "data/extra/oe3xwj_stall.json: größte Lücke zwischen zwei Empfangsereignissen "
           f"(ONRXDONE_TIME oder RX_ERROR, ACKs eingerechnet): {st['last_rx']} bis {st['next_rx']} = "
           f"{de(st['gap_s'], 1)} s (zweitgrößte Lücke {de(st['second_longest_gap_s'], 1)} s). Die letzte "
           "Zeitüberschreitung steht um 17:06:06, der Empfang setzt erst danach ein."),
        li("RX-Fehler-Anteil:", "data/extra/oe3xwj_rxerror.json: " + rxe["definition"] + "."),
        li("Ping-Code:", "<code>src/nrf52/nrf52_main.cpp:2717-2737</code>, "
           "<code>src/esp32/esp32_main.cpp:5264-5285</code>, <code>src/command_functions.cpp:3806-3910</code> "
           "(pingcall, pingtime, ping start/stop, pingmax), <code>src/prod_lock.h:28,34,75-78,186</code> "
           "(60 s, erlaubt 30 bis 300 s), <code>src/loop_functions.cpp:3820</code> (sendPing)."),
    ]))
    A("<h3>Marker</h3>")
    A(ul([
        li("RING_WRITE / RING_TX_READ / RING_DROP_STALE / RING_SUPERSEDE:", "Slot-Pool, "
           "<code>src/txring_functions.cpp</code> (:639 STALE, :1068 SUPERSEDE)."),
        li("CAD_SAFETY_TIMEOUT:", "<code>src/nrf52/nrf52_main.cpp:1794</code>."),
        li("[CET];rx / [CET];drop;where;relay:", "Gate <code>src/cet_age.h</code> (150 s), <code>:2597</code>."),
        li("[NBR]|NEED / REFUSE / CANCEL:", "<code>src/nbr_matrix.cpp</code>, <code>src/lora_functions.cpp:1380</code>."),
        li("[LOG] 012 A:", "ACK-Frame, 12 Byte; Hnn = Hop-Byte (0x80 = Server-Flag); danach die bestätigte Id."),
    ]))
    A("<h3>Skripte</h3>")
    A("<p>Dieser Bericht: <code>generators/oe3xwj.py</code> "
      "(<code>--data</code> Datenverzeichnis, <code>--kit</code> Kit-Verzeichnis, <code>--out</code> "
      "Ausgabedatei; liest nur Daten, keine Roh-Logs). Roh-Log-Durchlauf: "
      "<code>generators/lib/oe3xwj_raw.py --norm &lt;norm-Verzeichnis&gt; --out data/extra</code> "
      "schreibt <code>data/extra/oe3xwj_*.json</code>. Zugrunde liegende Analysen: q2, q34, q5, q6, "
      "health, master. Zahlen ohne eigene Quellenangabe im Text stammen aus den Befundtabellen dort "
      "und aus <code>data/CORE-FINDINGS.md</code> (v2) sowie <code>verify/*.md</code>.</p></details>")

    A('<div class="foot">Quellen: oe3xwj.log (Roh-Log), health, q2, q34, q5, q6, master (mcmap), '
      "extra/oe3xwj_*.json, CORE-FINDINGS.md (v2), verify/*.md. Netzsicht: "
      '<a href="uebersicht.html">uebersicht.html</a>. Auswertung der Fork-Firmware DK5EN, nicht die '
      "offizielle Firmware.</div>")

    html_out = page("MeshCom Log-Auswertung OE3XWJ-12", "\n".join(parts))
    out.write_text(html_out, encoding="utf-8")
    for w in WARN:
        print("WARN", w, file=sys.stderr)
    print(f"wrote {out} ({len(html_out)} bytes, {FIG['n']} figures)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--kit", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    build(args.data, args.kit, args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
