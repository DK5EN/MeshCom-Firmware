#!/usr/bin/env python3
"""Generator fuer ~/Desktop/Nachbarschaftsmatrix-Kantenpool.html (Konzeptpapier)."""
from __future__ import annotations
import json, math, os, sys
from pathlib import Path

SCR = Path(__file__).resolve().parent
REPO = Path("/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main")
OUT = Path.home() / "Desktop" / "Nachbarschaftsmatrix-Kantenpool.html"
STYLE = Path.home() / ".claude/skills/konzeptpapier/assets/style.css"

# ---------------------------------------------------------------- Konstanten
ROW_TODAY, CELL_TODAY = 24, 6
ROW1, MASK1, EDGE1, HDR1 = 20, 8, 6, 8          # Stufe 1: 64-Bit-Masken
ROW2, MASK2 = 20, 16                             # Stufe 2: 128-Bit-Masken (S3/nRF)

def dense(n):            return n*ROW_TODAY + n*n*CELL_TODAY + 4
def pool(n, e, mask=MASK1, row=ROW1): return n*(row + 2*mask) + e*EDGE1 + HDR1
def rect(n, m):          return n*ROW_TODAY + n*m*CELL_TODAY + 4
PATH_ENTRY = 10 + 52 + 4 + 4 + 1

FAM = {
  "classic": dict(name="ESP32 klassisch", rows=13, mhpath=40, mheard=80, s1_rows=64, s1_edges=256),
  "xml":     dict(name="ESP32 klassisch, ENABLE_XML", rows=21, mhpath=50, mheard=50, s1_rows=64, s1_edges=256),
  "s3":      dict(name="ESP32-S3", rows=21, mhpath=100, mheard=80, s1_rows=64, s1_edges=384),
  "nrf":     dict(name="nRF52840", rows=21, mhpath=100, mheard=80, s1_rows=64, s1_edges=384),
}
ENV_FAM = {
  "E22-DevKitC":"classic","E22_1262-DevKitC":"classic","esp32-loraprs-e22":"classic","esp32-loraprs-ra01":"classic",
  "heltec_wifi_lora_32_V2":"classic","ttgo-lora32-v21":"classic","ttgo_tbeam":"classic","ttgo_tbeam_SX1262":"classic",
  "ttgo_tbeam_SX1268":"classic","E22_XML-DevKitC":"xml",
  "E22_1262_S3-DevKitC-1-N16R8":"s3","E22_1268_S3-DevKitC-1-N16R8":"s3","LilyGo_T-Beam-1W":"s3","LilyGo_T3_S3_V1_3":"s3",
  "LilyGo_T_Connect_Pro":"s3","T-ETH-ELITE_1262":"s3","heltec_wifi_lora_32_V3":"s3","heltec_wifi_lora_32_V4":"s3",
  "heltec_wireless_stick":"s3","heltec_wireless_tracker":"s3","t_deck":"s3","t_deck_plus":"s3","t_deck_pro":"s3",
  "ttgo_tbeam_supreme":"s3","vision-master-e213":"s3","vision-master-e290":"s3","wireless-paper":"s3",
  "heltec_t114":"nrf","t_echo":"nrf","wiscore_rak4631":"nrf",
  "esp32-safeboot":"safeboot","esp32-S3-safeboot":"safeboot",
}
ENV_ORDER = ["esp32-safeboot","esp32-S3-safeboot","E22_1262-DevKitC","E22-DevKitC","E22_XML-DevKitC",
  "E22_1268_S3-DevKitC-1-N16R8","E22_1262_S3-DevKitC-1-N16R8","heltec_wifi_lora_32_V2","heltec_wifi_lora_32_V3",
  "heltec_wifi_lora_32_V4","heltec_wireless_stick","heltec_wireless_tracker","heltec_t114","vision-master-e290",
  "vision-master-e213","wireless-paper","T-ETH-ELITE_1262","LilyGo_T-Beam-1W","LilyGo_T3_S3_V1_3","LilyGo_T_Connect_Pro",
  "ttgo-lora32-v21","ttgo_tbeam","ttgo_tbeam_SX1262","ttgo_tbeam_SX1268","ttgo_tbeam_supreme","t_deck","t_deck_plus",
  "t_deck_pro","t_echo","wiscore_rak4631","esp32-loraprs-e22","esp32-loraprs-ra01"]

def fam_sizes(f):
    d = FAM[f]
    return dict(
        dense=dense(d["rows"]), path=d["mhpath"]*PATH_ENTRY,
        s1=pool(d["s1_rows"], d["s1_edges"]),
        s2=pool(128, 512, MASK2, ROW2) if f in ("s3","nrf") else None,
    )

# ---------------------------------------------------------------- RAM-Daten
def load_ram():
    fresh = SCR / "resource-nbr-20260924.json"
    old = REPO / "tools" / "resource_baseline.json"
    data_old = json.loads(old.read_text()) if old.exists() else {}
    data_new = json.loads(fresh.read_text()) if fresh.exists() else {}
    rows = []
    for env in ENV_ORDER:
        e = data_new.get(env); src = "2026-09-24, feature-neighbour-matrix d9fc5c5a"
        if e is None:
            e = data_old.get(env); src = "Baseline 2026-09-11, fork-main"
        if e is None:
            rows.append(dict(env=env, fam=ENV_FAM[env], used=None, length=None, src="n/a")); continue
        if "dram0_0_seg" in e:
            used, length, kind = e["dram0_0_seg"]["used"], e["dram0_0_seg"]["length"], "dram0_0_seg"
        else:
            used, length, kind = e["ram_used"], e["ram_total"], "RAM-Zeile"
        rows.append(dict(env=env, fam=ENV_FAM[env], used=used, length=length, kind=kind, src=src,
                         iram=e.get("iram0_0_seg"), flash=(e.get("flash_used"), e.get("flash_total"))))
    return rows

def kb(b): return f"{b/1024:.1f}"

# ---------------------------------------------------------------- SVG-Helfer
def esc(s): return s.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")
def T(x, y, s, cls="", anchor="", extra=""):
    a = f' text-anchor="{anchor}"' if anchor else ""
    c = f' class="{cls}"' if cls else ""
    return f'<text x="{x}" y="{y}"{a}{c}{extra}>{esc(s)}</text>'
def R(x, y, w, h, cls="node", extra=""):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" class="{cls}"{extra}/>'
def L(d, cls="ln", marker=True):
    m = ' marker-end="url(#arr)"' if marker else ""
    return f'<path d="{d}" class="{cls}"{m}/>'
DEFS = ('<defs><marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
        '<path d="M0 0 L10 5 L0 10 z" fill="#1f2a37"/></marker></defs>')
def box(x, y, w, lines, cls="node", first_bold=True, lh=16, extra=""):
    h = 18 + lh*len(lines)
    out = [R(x, y, w, h, cls, extra)]
    for i, ln in enumerate(lines):
        c = "b" if (i == 0 and first_bold) else "t11"
        out.append(T(x + w/2, y + 14 + lh*i + (2 if i else 0), ln, c, "middle"))
    return "\n".join(out), h

def figure(svg_inner, w, h, caption, aria, fid=""):
    idattr = f' id="{fid}"' if fid else ""
    return (f'<figure{idattr}><div class="fig"><svg viewBox="0 0 {w} {h}" role="img" aria-label="{esc(aria)}">'
            f'{DEFS}{svg_inner}</svg></div><figcaption>{caption}</figcaption></figure>')

# ---------------------------------------------------------------- Abb. 1: Ist-Zustand (dichte Matrix, Schnappschuss)
CALLS = ["DK5EN-98","DL2JA-2","DK5EN-1","DL2UD-1","DB0ED-99","DB0HOB-12","DC2MAC-1","DB0FHR-12","DL2JA-3","DL3NCU-1",
         "DL2JA-1","DD7MH-55","OE2XZR-12","DB0MMR-12","DB0ISM-1","DF2KX-12","DG7RJ-12","DG7RJ-8","OE7XWT-12","DO7GH","DG3MNF-4"]
# Zeile X (gehoert), Spalten Y (Hoerer), 1-basiert wie auf der Web-Seite vom 2026-09-24 07:25
FILLED = {1:[2,3,4,5],2:[1,3,4,5,7,10,18],3:[1],4:[1,2],5:[1,2,7,10,15],6:[2,5,12,19,20],7:[2,5],8:[2,5,6,13,19,20],
          9:[2],10:[2,5],11:[1,2,4,5,6,10,12,15,19],12:[2,5,6,14],13:[2,14],14:[2,12],15:[2,5],16:[2],17:[2,18],
          18:[2,15],19:[2,5,6,8],20:[2,6,19],21:[2]}
DIRECT = {2,3,4,5,11}
GW = {1,2,3,8,9,13,18}
assert sum(len(v) for v in FILLED.values()) == 67

def fig_ist():
    o = []
    # linke Seite: Speicherbild
    o.append(T(40, 30, "NbrMatrix heute, 21 Zeilen: 3154 B BSS", "b"))
    b, h = box(40, 44, 330, ["rows[21] x 24 B = 504 B", "call[10], rpt_min, lat, lon (float),", "last_min, flags, hw"], "node")
    o.append(b)
    b, h2 = box(40, 44+h+14, 330, ["cells[21][21] x 6 B = 2646 B", "cnt_text, cnt_pos, cnt_hey, snr, last_min", "cell[X][Y] = 'Y hat X gehoert'"], "node prx")
    o.append(b)
    y3 = 44+h+14+h2+14
    b, h3 = box(40, y3, 330, ["Belegt im Feld: 67 von 420 Zellen (16 %)", "Zeilen: 21 von 21, 16 Verdraengungen in 11 h", "jede weitere Zeile: +282 B"], "node bad")
    o.append(b)
    o.append(T(40, y3+h3+26, "Quelle: [NBR]|SNAP 660, DK5EN-98, 2026-09-24 07:02", "t11"))
    # rechte Seite: Gitter
    gx, gy, cs = 470, 60, 11
    o.append(T(gx + 21*cs/2, 30, "cells[X][Y], Schnappschuss 07:25", "b", "middle"))
    o.append(T(gx + 21*cs/2, 46, "Spalte Y = Hoerer, Zeile X = Gehoerter", "t11", "middle"))
    for i in range(21):
        for j in range(21):
            x, y = gx + j*cs, gy + i*cs
            X, Y = i+1, j+1
            if X == Y:
                fill = "#c9d3dd"
            elif Y in FILLED.get(X, []):
                fill = "#1f6feb" if (Y == 1 or X == 1) else "#7b4fbf"
            else:
                fill = "#f3f6f9"
            o.append(f'<rect x="{x}" y="{y}" width="{cs-1}" height="{cs-1}" fill="{fill}"/>')
    # Achsenbeschriftung: nur Spalte 1 / Zeile 1 und Markierung direkter Zeilen
    o.append(T(gx-6, gy+8, "1", "t11", "end"))
    o.append(T(gx-6, gy+21*cs-2, "21", "t11", "end"))
    o.append(T(gx+3, gy+21*cs+14, "1", "t11"))
    o.append(T(gx+21*cs-12, gy+21*cs+14, "21", "t11"))
    o.append(T(gx-30, gy+21*cs/2, "X", "t11", "middle"))
    o.append(T(gx+21*cs/2, gy+21*cs+28, "Y", "t11", "middle"))
    # Legende rechts vom Gitter
    lx = gx + 21*cs + 30; ly = gy + 6
    def leg(y, color, text):
        return f'<rect x="{lx}" y="{y-9}" width="12" height="12" fill="{color}"/>' + T(lx+18, y+1, text, "t11")
    o.append(leg(ly, "#1f6feb", "Spalte 0 / Zeile 0: ich"))
    o.append(leg(ly+20, "#7b4fbf", "andere belegte Zelle"))
    o.append(leg(ly+40, "#c9d3dd", "Diagonale, nie belegt"))
    o.append(leg(ly+60, "#f3f6f9", "leer: 353 Zellen"))
    o.append(T(lx, ly+92, "Entscheidung liest nur", "t11"))
    o.append(T(lx, ly+107, "Spalte 0, Zeile 0 und die", "t11"))
    o.append(T(lx, ly+122, "Spalten direkter Nachbarn;", "t11"))
    o.append(T(lx, ly+137, "der Rest ist Anzeige.", "t11"))
    return figure("\n".join(o), 960, 320,
        "Abb. 1: Ist-Zustand. 84 % des Speichers sind Zellen, 84 % der Zellen sind leer, und die Zeilen sind trotzdem voll. "
        "Die Sendeentscheidung liest nur die blauen Kreuze und die Spalten der fuenf direkten Nachbarn.",
        "Ist-Zustand der Nachbarschaftsmatrix")

# ---------------------------------------------------------------- Abb. 2: Zielbild
def fig_ziel():
    o = []
    o.append(R(30, 20, 250, 330, "zone")); o.append(T(155, 40, "Schreiber", "b", "middle"))
    o.append(R(300, 20, 370, 330, "zone")); o.append(T(485, 40, "NbrMatrix, Arduino-frei (nbr_matrix.cpp)", "b", "middle"))
    o.append(R(690, 20, 250, 330, "zone")); o.append(T(815, 40, "Leser", "b", "middle"))
    b1, h1 = box(45, 60, 220, ["OnRxDone-Haken", "lora_functions.cpp: Pfad, Typ,", "SNR jedes dekodierten Frames"]); o.append(b1)
    b2, h2 = box(45, 150, 220, ["HN-Bericht im HEY", "nbrNoteReport(): 'S hat X gehoert'", "mit SNR, bis 8 Eintraege"]); o.append(b2)
    b3, h3 = box(45, 240, 220, ["Sweep je Minute", "Kante aelter als Fenster: frei,", "Bits in beiden Masken loeschen"]); o.append(b3)
    r1, rh1 = box(320, 60, 330, ["rows[64] x 36 B = 2304 B", "call, lat16, lon16, last_min, rpt_min, flags, hw", "hears: uint64_t     heardby: uint64_t"], "node srv"); o.append(r1)
    r2, rh2 = box(320, 170, 330, ["Kantenpool edges[256] x 6 B = 1536 B", "x, y, cnt, snr, last_min", "jede Kante setzt hears(y) Bit x, heardby(x) Bit y"], "node prx"); o.append(r2)
    o.append(T(485, 285, "Zeile 0 = ich, nie verdraengt; Direktzeilen geschuetzt", "t11", "middle"))
    o.append(T(485, 301, "Verdraengung: aelteste Hop-2-Zeile, dann aelteste Kante", "t11", "middle"))
    o.append(T(485, 317, "Fenster NBR_WINDOW_MIN 12 h, Geister-Sweep bleibt", "t11", "middle"))
    l1, lh1 = box(705, 60, 220, ["Relay-Entscheidung", "need, alone, cover:", "nur Masken, O(N) Wortoperationen"], "node srv"); o.append(l1)
    l2, lh2 = box(705, 140, 220, ["Rollen: #N, #X, Super", "Masken und popcount,", "kein Zellzugriff"], "node srv"); o.append(l2)
    l3, lh3 = box(705, 220, 220, ["Web, --neighbours, [NBR]-Log", "Pool: Zaehler T/P/H und SNR", "Masken: D/I, Hoerer"], "node"); o.append(l3)
    l4, lh4 = box(705, 300, 220, ["Symmetrie-Annahme (--nbrsym)", "SNR der Gegenkante aus dem Pool"], "node"); o.append(l4)
    # Pfeile
    o.append(L("M265 83 L320 83"))
    o.append(L("M265 173 L300 173 L300 190 L320 190"))
    o.append(L("M265 263 L300 263 L300 210 L320 210"))
    o.append(L("M485 130 L485 170"))
    o.append(L("M650 83 L705 83"))
    o.append(L("M650 100 L680 100 L680 163 L705 163"))
    o.append(L("M650 200 L680 200 L680 243 L705 243"))
    o.append(L("M650 215 L672 215 L672 323 L705 323"))
    return figure("\n".join(o), 960, 370,
        "Abb. 2: Zielbild Stufe 1. Zeilen tragen zwei Bitmasken, Kanten liegen in einem Pool fester Groesse. "
        "Die Sendeentscheidung und die Rollen rechnen nur noch auf den Masken; Zaehler und SNR liest nur die Anzeige und die Symmetrie-Annahme.",
        "Zielbild Kantenpool mit Masken")

# ---------------------------------------------------------------- Abb. 3: Kantenpool erklaert
def fig_pool():
    o = []
    gx, gy, cs = 70, 70, 26
    o.append(T(gx + 3*cs, 34, "Dichte Matrix, N = 6", "b", "middle"))
    o.append(T(gx + 3*cs, 50, "36 Zellen x 6 B = 216 B", "t11", "middle"))
    filled = {(1,0):"A",(2,0):"B",(0,2):"C",(3,2):"D"}
    for i in range(6):
        for j in range(6):
            x, y = gx + j*cs, gy + i*cs
            fill = "#c9d3dd" if i == j else ("#7b4fbf" if (i,j) in filled else "#f3f6f9")
            o.append(f'<rect x="{x}" y="{y}" width="{cs-2}" height="{cs-2}" fill="{fill}"/>')
            if (i,j) in filled:
                o.append(f'<text x="{x+cs/2-1}" y="{y+cs/2+4}" text-anchor="middle" fill="#fff" font-size="12" font-weight="600">{filled[(i,j)]}</text>')
        o.append(T(gx-8, gy + i*cs + 16, str(i), "mono", "end"))
        o.append(T(gx + i*cs + 12, gy + 6*cs + 12, str(i), "mono", "middle"))
    o.append(T(gx-24, gy + 3*cs, "X", "t11", "middle")); o.append(T(gx + 3*cs, gy + 6*cs + 28, "Y", "t11", "middle"))
    o.append(L("M240 150 L330 150"))
    o.append(T(285, 140, "nur belegte", "t11", "middle")); o.append(T(285, 166, "Zellen", "t11", "middle"))
    # Pool-Tabelle
    px, py = 340, 34
    o.append(T(px + 150, py, "Kantenpool, 8 Eintraege x 6 B = 48 B", "b", "middle"))
    o.append(T(px + 150, py+16, "Kapazitaet unabhaengig von N", "t11", "middle"))
    o.append(R(px, 60, 300, 8*20+26, "node"))
    hdr = ["#", "x", "y", "cnt", "snr", "last_min"]
    cols = [px+18, px+60, px+100, px+150, px+200, px+260]
    for c, hname in zip(cols, hdr): o.append(T(c, 78, hname, "b", "middle"))
    rows = [("A",1,0,12,-7,641),("B",2,0,3,-12,655),("C",0,2,5,-9,660),("D",3,2,1,-15,630)]
    for k in range(8):
        y = 98 + k*20
        if k < len(rows):
            r = rows[k]
            vals = [r[0], r[1], r[2], r[3], r[4], r[5]]
            for c, v in zip(cols, vals): o.append(T(c, y, str(v), "mono", "middle"))
        else:
            o.append(T(px+18, y, str(k), "mono", "middle")); o.append(T(px+150, y, "frei (x = 0xFF)", "mono", "middle"))
    # Masken rechts
    mx = 665
    o.append(T(mx, 34, "Masken je Zeile, Bit i = Zeile i", "b"))
    o.append(T(mx, 50, "jede Kante setzt zwei Bits, Bit 0 = ich", "t11"))
    lines = ["rows[0].hears   = 000110  A,B", "rows[2].hears   = 001001  C,D",
             "rows[1].heardby = 000001  A", "rows[2].heardby = 000001  B",
             "rows[0].heardby = 000100  C", "rows[3].heardby = 000100  D"]
    for k, ln in enumerate(lines): o.append(T(mx, 78 + k*18, ln, "mono"))
    o.append(T(mx, 200, "Direkt(X)   = rows[0].hears", "mono"))
    o.append(T(mx, 218, "HoertMich(X)= rows[0].heardby", "mono"))
    o.append(T(mx, 236, "Hoerer(P)   = rows[P].heardby", "mono"))
    # Notizen unten
    o.append(R(40, 262, 880, 58, "zone"))
    o.append(T(60, 282, "Neue Zeile: dichte Matrix waechst um 2N+1 Zellen (282 B bei N = 21), der Pool um 0 B; die Zeile kostet 36 B.", "t11"))
    o.append(T(60, 300, "Pool voll: aelteste Kante ohne Zeile 0 weicht. Zeile verdraengt: ihre Kanten werden frei, ihr Bit in allen Masken geloescht.", "t11"))
    return figure("\n".join(o), 960, 330,
        "Abb. 3: Was ein Kantenpool ist. Nur Zellen, die je einen Treffer hatten, werden zu Eintraegen; jeder Eintrag setzt "
        "ein Bit in der Hoerer-Maske des Gehoerten und in der Hoert-Maske des Hoerers. Die Rechnung laeuft auf den Masken, der Pool liefert Zaehler und SNR.",
        "Kantenpool erklaert")

# ---------------------------------------------------------------- Abb. 4: Wachstum
def fig_growth():
    o = []
    x0, y0, x1, y1 = 90, 30, 900, 300   # Plotbereich
    nmax, ymax = 64, 32000
    def X(n): return x0 + (x1-x0)*n/nmax
    def Y(b): return y1 - (y1-y0)*b/ymax
    for yb in range(0, ymax+1, 4000):
        o.append(f'<path d="M{x0} {Y(yb):.1f} L{x1} {Y(yb):.1f}" stroke="#e3e8ee" stroke-width="1"/>')
        o.append(T(x0-8, Y(yb)+4, f"{yb//1000} kB" if yb else "0", "t11", "end"))
    for n in (0, 16, 32, 48, 64):
        o.append(T(X(n), y1+16, str(n), "t11", "middle"))
    o.append(T(x1, y1+32, "Zeilen N, inkl. Zeile 0", "t11", "end"))
    for n in (13, 21, 41, 48):
        o.append(f'<path d="M{X(n):.1f} {y0} L{X(n):.1f} {y1}" stroke="#c9d3dd" stroke-width="1" stroke-dasharray="3 3"/>')
    for n, lab in ((41,"41: 1-Hop max 24 h"), (48,"48: 2-Hop p90 24 h")):
        o.append(f'<text x="{X(n)-5:.1f}" y="{y0+8}" class="t11" text-anchor="end" transform="rotate(90 {X(n)-5:.1f} {y0+8})">{esc(lab)}</text>')
    o.append(T(X(13), y1+46, "13: heute klassisch", "t11", "middle"))
    o.append(T(X(21), y1+60, "21: heute S3 und RAK", "t11", "middle"))
    series = [
        ("dicht heute: 24N + 6N^2", "#2a78d6", lambda n: dense(n)),
        ("rechteckig, 20 Hoerer (Super-Node): 144N", "#eb6834", lambda n: rect(n, 20)),
        ("Kantenpool Stufe 1: 36N + 4N x 6 B", "#1baf7a", lambda n: pool(n, 4*n)),
    ]
    for name, col, f in series:
        pts = " ".join(f"{X(n):.1f},{Y(min(f(n), ymax)):.1f}" for n in range(2, nmax+1))
        o.append(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="2" stroke-linejoin="round"/>')
        for n in (13, 21, 64):
            v = f(n)
            if v <= ymax:
                o.append(f'<circle cx="{X(n):.1f}" cy="{Y(v):.1f}" r="4" fill="{col}" stroke="#fff" stroke-width="2"><title>{esc(name)}: N = {n}, {v} B</title></circle>')
    # Direktbeschriftung am Ende
    o.append(T(X(64)-6, Y(min(dense(64),ymax))-8, "26,1 kB", "t11", "end"))
    o.append(T(X(64)-6, Y(rect(64,20))-8, "9,2 kB", "t11", "end"))
    o.append(T(X(64)-6, Y(pool(64,256))+16, "3,8 kB", "t11", "end"))
    # Legende
    lx, ly = x0+10, y0+8
    for k, (name, col, f) in enumerate(series):
        o.append(f'<path d="M{lx} {ly+k*18} L{lx+22} {ly+k*18}" stroke="{col}" stroke-width="2"/>')
        o.append(T(lx+30, ly+k*18+4, name, "t11"))
    return figure("\n".join(o), 960, 370,
        "Abb. 4: Speicher je Zeilenzahl. Die dichte Matrix ueberschreitet bei 41 Zeilen (1-Hop-Maximum der Flotte in 24 h) 12 kB; "
        "der Kantenpool bleibt bei 64 Zeilen unter 4 kB. Die rechteckige Variante hilft nur dort nicht, wo es zaehlt: am Super-Node mit 20 und mehr Hoerern.",
        "Wachstum der Matrix je Layout")

# ---------------------------------------------------------------- Abb. 5: Entscheidungsfluss Fall A / Fall B
def fig_flow():
    o = []
    b, h = box(40, 30, 220, ["Frame dekodiert", "OnRxDone(), vor dem Pfad-Umbau"]); o.append(b)
    b, h = box(300, 30, 260, ["Relay-Kandidat?", "Mesh an, Hop frei, kein Duplikat,", "nicht gwcap, nicht hop0"]); o.append(b)
    b, h = box(600, 30, 320, ["Wissen vorhanden?", "--nbrrelay on, Pfad gueltig,", "Abhaengige D nicht leer (known)"]); o.append(b)
    b, h = box(600, 140, 320, ["Nein: Fluten wie heute", "RING_KIND_OTHER, Prio-Basis 4500/5500 ms,", "kein Nachrang, kein Abbruch"], "node"); o.append(b)
    b, h = box(300, 140, 260, ["Ja: need und alone aus Masken", "need  = D ohne HatF", "alone = need ohne Alternative"], "node srv"); o.append(b)
    o.append(R(300, 236, 260, 34, "node")); o.append(T(430, 258, "alone != 0 ?", "b", "middle"))
    bA, hA = box(40, 310, 410, ["Fall A: Vorrang, nie Abbruch",
        "Basis 3500 ms + Slots 0..2, vor Relay 4500 und POS 5500",
        "fremde Kopie: nur REFUSE-Zeile, Slot bleibt",
        "ab 8 s Wartezeit: 150 ms Schutzabstand je Re-Arm, dann CAD"], "node srv"); o.append(bA)
    bB, hB = box(490, 310, 430, ["Fall B: Nachrang, Abbruch bei Deckung",
        "POS/HEY: Sperre 20 s ab Einreihen, Slots 7..9; Text: heutige Basis",
        "fremde Kopie von L: need &= ~Hoerer(L); need == 0: CANCEL",
        "gehaltener Slot blockiert niemanden; ab 60 s Kurzsuche wie A"], "node prx"); o.append(bB)
    o.append(L("M260 53 L300 53")); o.append(L("M560 53 L600 53"))
    o.append(L("M760 100 L760 140")); o.append(T(768, 126, "nein", "t11"))
    o.append(L("M600 80 L580 80 L580 163 L560 163")); o.append(T(585, 120, "ja", "t11"))
    o.append(L("M430 206 L430 236"))
    o.append(L("M300 253 L245 253 L245 310")); o.append(T(252, 300, "ja", "t11"))
    o.append(L("M560 253 L705 253 L705 310")); o.append(T(712, 300, "nein, auch bei need = 0", "t11"))
    o.append(T(480, 445, "Text als Fall B: heutige Basis und Slots, aber Abbruch. Alles ohne Wissen bleibt heutiges Fluten (Konzept 1: nichts unterdruecken auf Verdacht).", "t11", "middle"))
    return figure("\n".join(o), 960, 460,
        "Abb. 5: Wie ein Relay in Fall A oder Fall B kommt. Entschieden wird je Frame beim Einreihen aus den Masken; "
        "der Fall bestimmt Basis, Slots, Re-Arm-Verhalten und ob ein Abbruch zulaessig ist.",
        "Entscheidungsfluss Fall A und Fall B")

# ---------------------------------------------------------------- Abb. 6: Zeitachse animiert
def fig_timeline():
    SIM, DUR = 25.0, 12.5  # simulierte Sekunden, Animationsdauer
    x0, x1 = 250, 920
    def X(t): return x0 + (x1-x0)*t/SIM
    def kt(t): return f"{t/SIM:.4f}"
    o = []
    rowsy = {"super": 70, "redA": 130, "redB": 190, "alt": 250}
    labels = {"super": ("Super-Node, Fall A", "alone != 0: 8 Blaetter nur ueber ihn"),
              "redA": ("Redundant 1, Fall B", "alone = 0: alle Abhaengigen gedeckt"),
              "redB": ("Redundant 2, Fall B", "alone = 0"),
              "alt": ("Alt-Firmware", "ohne Matrix: Re-Arm je Empfang")}
    for k, y in rowsy.items():
        o.append(T(x0-12, y-2, labels[k][0], "b", "end")); o.append(T(x0-12, y+14, labels[k][1], "t11", "end"))
        o.append(f'<path d="M{x0} {y+20} L{x1} {y+20}" stroke="#e3e8ee"/>')
    # Zeitachse
    ya = 292
    o.append(f'<path d="M{x0} {ya} L{x1} {ya}" class="ln"/>')
    for t in (0, 5, 10, 15, 20, 25):
        o.append(f'<path d="M{X(t):.1f} {ya-4} L{X(t):.1f} {ya+4}" class="ln"/>')
        o.append(T(X(t), ya+18, f"{t} s", "mono", "middle"))
    o.append(T(X(0), 40, "t = 0: Frame von S kommt bei allen drei an", "t11"))
    def bar(k, t_a, t_b, color, label="", cancel_at=None):
        y = rowsy[k]; w = X(t_b)-X(t_a)
        r = (f'<rect x="{X(t_a):.1f}" y="{y}" width="0" height="16" fill="{color}" rx="3">'
             f'<animate attributeName="width" values="0;0;{w:.1f};{w:.1f}" keyTimes="0;{kt(t_a)};{kt(t_b)};1" dur="{DUR}s" repeatCount="indefinite"/></rect>')
        return r
    def mark(k, t, text, color, above=False, anchor_end=False):
        y = rowsy[k]
        ty = y-6 if above else y+11
        tx = X(t)-4 if anchor_end else X(t)+14
        an = ' text-anchor="end"' if anchor_end else ''
        return (f'<g opacity="0"><animate attributeName="opacity" values="0;0;1;1" keyTimes="0;{kt(t)};{kt(t)};1" dur="{DUR}s" repeatCount="indefinite"/>'
                f'<rect x="{X(t):.1f}" y="{y-2}" width="10" height="20" fill="{color}"/>'
                f'<text x="{tx:.1f}" y="{ty}" class="t11"{an}>{esc(text)}</text></g>')
    # Super: Backoff 0..3.5, TX bei 3.5
    o.append(bar("super", 0, 3.5, "#cde2fb"))
    o.append(mark("super", 3.5, "TX nach 3,5 s, Basis 3500 ms plus Slot", "#1f6feb"))
    # Redundante: Sperre geplant bis 20 s (hell), Kopie des Super bei 4,0 s gehoert -> CANCEL
    for k in ("redA", "redB"):
        o.append(bar(k, 0, 4.0, "#fde6cf"))
        o.append(mark(k, 4.0, "Kopie des Super gehoert: need &= ~Hoerer(Super) = 0, CANCEL", "#c62828", above=True))
        y = rowsy[k]
        o.append(f'<rect x="{X(4.0):.1f}" y="{y}" width="{X(20)-X(4.0):.1f}" height="16" fill="none" stroke="#d9822b" stroke-dasharray="4 3" rx="3"/>')
        o.append(T(X(20)-6, y+12, "geplante Sperre bis 20 s", "t11", "end"))
    # Alt-Firmware: Backoff 0..4.5 re-armt bei 4.0 und 9.5, TX bei 19
    o.append(bar("alt", 0, 4.0, "#c9d3dd"))
    o.append(mark("alt", 4.0, "Re-Arm", "#1f2a37", above=True))
    o.append(bar("alt", 4.0, 9.5, "#c9d3dd"))
    o.append(mark("alt", 9.5, "Re-Arm", "#1f2a37", above=True))
    o.append(bar("alt", 9.5, 19.0, "#c9d3dd"))
    o.append(mark("alt", 19.0, "TX nach 19 s, Feld-Median DL2UD-1", "#5b6b7b", above=True, anchor_end=True))
    # Cursor
    o.append(f'<line x1="{x0}" y1="30" x2="{x0}" y2="{ya}" stroke="#c62828" stroke-width="1.2">'
             f'<animate attributeName="x1" from="{x0}" to="{x1}" dur="{DUR}s" repeatCount="indefinite"/>'
             f'<animate attributeName="x2" from="{x0}" to="{x1}" dur="{DUR}s" repeatCount="indefinite"/></line>')
    return figure("\n".join(o), 960, 320,
        "Abb. 6: Zeitachse eines Frames an drei Nachbarn mit Matrix und einem ohne (Animation, 12,5 s je Durchlauf, 0,5 s je Sekunde). "
        "Der Super-Node sendet als Erster, die redundanten Knoten hoeren seine Kopie und brechen ab, bevor ihre Sperre ablaeuft; "
        "die Alt-Firmware startet ihren Backoff bei jedem Empfang neu und sendet als Letzte, im Feld nach 19 s (Median).",
        "Zeitachse Fall A und Fall B")

# ---------------------------------------------------------------- Abb. 7: Nachbarschaft DK5EN-98
def fig_graph():
    o = []
    o.append(T(30, 28, "Schnappschuss DK5EN-98, 2026-09-24 07:25, Fenster 12 h", "b"))
    me, mh = box(30, 230, 150, ["DK5EN-98", "ich, Zeile 0"], "node wwwf")
    o.append(me)
    nb = [("DL2JA-2", "Super, #X 8, #N 19", "node srv", 70),
          ("DK5EN-1", "Blatt: nur ich hoere es", "node bad", 150),
          ("DL2UD-1", "Redundant, #X 0", "node", 230),
          ("DB0ED-99", "Redundant, #X 0, #N 10", "node", 310),
          ("DL2JA-1", "Redundant, relayt nie", "node", 390)]
    for call, sub, cls, y in nb:
        b, h = box(260, y, 190, [call, sub], cls); o.append(b)
        o.append(L(f"M180 {230+mh/2:.0f} L220 {230+mh/2:.0f} L220 {y+h/2:.0f} L260 {y+h/2:.0f}"))
    # Zonen fuer Hop-2
    zA = ["DL2JA-3","OE2XZR-12","DB0MMR-12","DF2KX-12","DG7RJ-12","DG7RJ-8","DO7GH","DG3MNF-4"]
    zB = ["DB0HOB-12","DC2MAC-1","DB0FHR-12","DL3NCU-1","DD7MH-55","DB0ISM-1","OE7XWT-12"]
    o.append(R(560, 50, 180, 20+len(zA)*18+10, "zone")); o.append(T(650, 68, "8 exklusiv an DL2JA-2", "b", "middle"))
    for k, c in enumerate(zA): o.append(T(650, 88+k*18, c, "mono", "middle"))
    yB = 50+20+len(zA)*18+40
    o.append(R(560, yB, 180, 20+len(zB)*18+10, "zone")); o.append(T(650, yB+18, "7 doppelt gehoert", "b", "middle"))
    for k, c in enumerate(zB): o.append(T(650, yB+38+k*18, c, "mono", "middle"))
    # Kanten: DL2JA-2 -> beide Zonen, DB0ED-99 -> Zone B, DK5EN-1 hoert nur DL2JA-2
    o.append(L(f"M450 92 L560 92", "lnh")); o.append(T(505, 84, "hoert", "t11", "middle"))
    o.append(L(f"M450 100 L520 100 L520 {yB+40} L560 {yB+40}", "lnh"))
    o.append(L(f"M450 332 L540 332 L540 {yB+60} L560 {yB+60}", "ln"))
    o.append(T(505, 324, "hoert auch", "t11", "middle"))
    # Erklaerung rechts
    ex = 770
    o.append(T(ex, 60, "Rollen aus meiner Sicht:", "b"))
    lines = ["#X(N) = Knoten, die N hoert und", "die weder ich noch ein anderer", "direkter Nachbar hoert.",
             "", "Super: groesstes #X, mindestens 2", "und mindestens doppelt so gross", "wie das zweitgroesste.",
             "", "Needed: #X >= 1.", "Redundant: #X = 0.", "", "Blatt (rot): nur ich hoere es,", "darum 'Mesh needed' fuer mich.",
             "", "DB0ED-99 hoert 10, davon 7", "auch DL2JA-2 und 3 auch ich:", "#X 0, obwohl #N 10."]
    for k, ln in enumerate(lines):
        if ln: o.append(T(ex, 82+k*16, ln, "t11"))
    return figure("\n".join(o), 960, 470,
        "Abb. 7: Die Nachbarschaft von DK5EN-98 am 24.09. Der Rang haengt am exklusiven Beitrag, nicht an der Nachbarzahl: "
        "DL2JA-2 versorgt acht Knoten allein und ist Super-Node, DB0ED-99 hoert zehn und ist redundant.",
        "Nachbarschaftsgraph DK5EN-98")

# ---------------------------------------------------------------- Abb. 8: RAM je Plattform
def fig_ram(ramrows):
    boards = [r for r in ramrows if r["fam"] != "safeboot" and r["used"]]
    bh, gap = 14, 6
    x0, x1 = 250, 860
    top = 40
    h = top + len(boards)*(bh+gap) + 40
    o = []
    maxlen = max(r["length"] for r in boards)
    def X(b): return x0 + (x1-x0)*b/maxlen
    o.append(f'<rect x="{x0}" y="12" width="12" height="12" fill="#2a78d6"/>'); o.append(T(x0+18, 22, "statisch belegt", "b"))
    o.append(f'<rect x="{x0+150}" y="12" width="12" height="12" fill="#e3e8ee"/>'); o.append(T(x0+168, 22, "Segmentlaenge", "b"))
    o.append(T(x1, 22, "frei = Laenge minus belegt", "t11", "end"))
    famname = {"classic":"klassisch","xml":"klassisch XML","s3":"S3","nrf":"nRF52"}
    for k, r in enumerate(boards):
        y = top + k*(bh+gap)
        o.append(T(x0-8, y+11, r["env"], "mono", "end"))
        o.append(f'<rect x="{x0}" y="{y}" width="{X(r["length"])-x0:.1f}" height="{bh}" fill="#e3e8ee" rx="3"/>')
        o.append(f'<rect x="{x0}" y="{y}" width="{X(r["used"])-x0:.1f}" height="{bh}" fill="#2a78d6" rx="3">'
                 f'<title>{esc(r["env"])}: {r["used"]} von {r["length"]} B ({r["kind"]}), {esc(r["src"])}</title></rect>')
        free = r["length"] - r["used"]
        label = f'{kb(r["used"])} / {kb(r["length"])} kB, frei {kb(free)} kB, {famname[r["fam"]]}'
        tw = len(label) * 6.2
        if X(r["length"]) - X(r["used"]) >= tw + 16:
            o.append(T(X(r["length"])-6, y+11, label, "t11", "end"))
        elif X(r["length"]) + 6 + tw <= 940:
            o.append(T(X(r["length"])+6, y+11, label, "t11"))
        else:
            o.append(f'<text x="{X(r["used"])-6:.1f}" y="{y+11}" text-anchor="end" class="t11" fill="#fff" style="fill:#fff">{esc(label)}</text>')
    o.append(T(x0, h-14, "ESP32: Linker-Segment dram0_0_seg (statische Daten; auf S3 liegt dort auch der Heap). nRF52: RAM-Zeile des Builds. Quelle je Balken im Tooltip.", "t11"))
    return figure("\n".join(o), 960, h,
        "Abb. 8: Statischer RAM je Board-Umgebung. Stufe 1 kostet auf klassischem ESP32 3,8 kB und gibt mit Stufe 1b 2,8 kB zurueck; "
        "die Reserve jeder Umgebung steht rechts am Balken.",
        "RAM je Plattform")

# ---------------------------------------------------------------- Tabellen
def tbl(head, rows, num_cols=()):
    h = "".join(f'<th{" class=num" if i in num_cols else ""}>{c}</th>' for i, c in enumerate(head))
    b = ""
    for r in rows:
        b += "<tr>" + "".join(f'<td{" class=num" if i in num_cols else ""}>{c}</td>' for i, c in enumerate(r)) + "</tr>"
    return f"<table><thead><tr>{h}</tr></thead><tbody>{b}</tbody></table>"

def platform_table(ramrows):
    rows = []
    for r in ramrows:
        f = r["fam"]
        if f == "safeboot":
            rows.append((r["env"], "Safeboot", "keine Matrix", "-", "-", "-", "-", "-")); continue
        s = fam_sizes(f); d = FAM[f]
        used = f'{kb(r["used"])} / {kb(r["length"])}' if r["used"] else "n/a"
        free = kb(r["length"]-r["used"]) if r["used"] else "n/a"
        s2 = kb(s["s2"]) if s["s2"] else "-"
        rows.append((r["env"], d["name"], used, free, f'{d["rows"]} / {kb(s["dense"])}', f'{d["s1_rows"]} / {kb(s["s1"])}', s2, f'{d["mhpath"]} / {kb(s["path"])}'))
    return tbl(["Umgebung", "Familie", "RAM belegt / Segment (kB)", "frei (kB)", "Matrix heute: Zeilen / kB", "Stufe 1: Zeilen / kB", "Stufe 2 (kB)", "Pfadtabelle: Eintraege / kB"],
               rows, num_cols=(2,3,4,5,6,7))

def example_table():
    rows = [
        ("Direkt", "rows[0].hears", "DL2JA-2, DK5EN-1, DL2UD-1, DB0ED-99, DL2JA-1", "5"),
        ("HoertMich", "rows[0].heardby", "DL2JA-2, DK5EN-1, DL2UD-1, DB0ED-99", "4"),
        ("Gateways", "NBR_FLAG_GW als Maske", "DL2JA-2, DK5EN-1, DB0FHR-12, DL2JA-3, OE2XZR-12, DG7RJ-8", "6"),
        ("Abhaengige D", "(Direkt | HoertMich) &amp; ~GW &amp; ~1", "DL2UD-1, DB0ED-99, DL2JA-1", "3"),
        ("Pfad(F)", "Bits der Pfad-Token", "OE7XWT-12, DL2JA-2", "2"),
        ("Hoerer(OE7XWT-12)", "rows[OE7XWT-12].heardby", "DL2JA-2, DB0ED-99, DB0HOB-12, DB0FHR-12", "4"),
        ("Hoerer(DL2JA-2)", "rows[DL2JA-2].heardby", "ich, DK5EN-1, DL2UD-1, DB0ED-99, DC2MAC-1, DL3NCU-1, DG7RJ-8", "7"),
        ("HatF", "Pfad | Hoerer(P) fuer jedes P im Pfad", "Vereinigung der drei Zeilen darueber", "11"),
        ("<b>need</b>", "D &amp; ~HatF", "<b>DL2JA-1</b>", "1"),
        ("Versorger", "Direkt &amp; HatF", "DL2JA-2, DK5EN-1, DL2UD-1, DB0ED-99", "4"),
        ("gedeckt", "OR rows[M].heardby fuer M in Versorger", "enthaelt DL2JA-1 nicht: DL2JA-1 hat nie etwas wiederholt, seine Spalte ist leer", "-"),
        ("<b>alone ohne --nbrsym</b>", "need &amp; ~gedeckt", "<b>DL2JA-1: Fall A</b>, Vorrang, nie Abbruch", "1"),
        ("Symmetrie-Annahme", "Gegenkante aus dem Pool: DL2JA-2 hat DL2JA-1 gehoert, SNR &gt;= -16 dB", "DL2JA-1 hoert DL2JA-2 vermutlich: Alternative gefunden, Zeile [NBR]|SYM|ALT", "-"),
        ("<b>alone mit --nbrsym</b>", "need &amp; ~(gedeckt | angenommen)", "<b>leer: Fall B</b>, Sperre 20 s, Abbruch sobald eine Kopie DL2JA-1 deckt", "0"),
    ]
    return tbl(["Menge", "Rechnung auf Masken", "Inhalt im Schnappschuss", "Bits"], rows, num_cols=(3,))


# ---------------------------------------------------------------- Anhaenge (Forschungsbericht)
COUNTS = {1:{2:12,3:289,4:93,5:49},2:{1:610,3:3,4:159,5:138,7:1,10:5,18:1},3:{1:492},4:{1:443,2:15},
          5:{1:750,2:66,7:1,10:1,15:1},6:{2:53,5:358,12:17,19:4,20:1},7:{2:17,5:21},8:{2:1,5:282,6:26,13:8,19:7,20:1},
          9:{2:11},10:{2:11,5:35},11:{1:70,2:23,4:72,5:23,6:3,10:19,12:1,15:2,19:3},12:{2:76,5:98,6:16,14:1},
          13:{2:24,14:5},14:{2:58,12:18},15:{2:13,5:20},16:{2:12},17:{2:6,18:3},18:{2:42,15:5},19:{2:68,5:14,6:18,8:7},
          20:{2:22,6:4,19:2},21:{2:9}}
for x, ys in FILLED.items():
    assert sorted(ys) == sorted(COUNTS[x]), x
ROWATTR = [("DK5EN-98","-","Y","Y",5,"-","-"),("DL2JA-2","D","Y","Y",19,8,"Super"),("DK5EN-1","D","Y","Y",2,0,"Redundant"),
 ("DL2UD-1","D","N","Y",3,0,"Redundant"),("DB0ED-99","D","N","Y",10,0,"Redundant"),("DB0HOB-12","I","N","Y",5,"-","-"),
 ("DC2MAC-1","I","N","Y",2,"-","-"),("DB0FHR-12","I","Y","Y",1,"-","-"),("DL2JA-3","I","Y","Y",0,"-","-"),
 ("DL3NCU-1","I","N","Y",3,"-","-"),("DL2JA-1","D","N","Y",0,0,"Redundant"),("DD7MH-55","I","N","Y",3,"-","-"),
 ("OE2XZR-12","I","Y","Y",1,"-","-"),("DB0MMR-12","I","N","Y",2,"-","-"),("DB0ISM-1","I","N","Y",3,"-","-"),
 ("DF2KX-12","I","N","Y",0,"-","-"),("DG7RJ-12","I","N","Y",0,"-","-"),("DG7RJ-8","I","Y","Y",2,"-","-"),
 ("OE7XWT-12","I","N","Y",4,"-","-"),("DO7GH","I","N","Y",2,"-","-"),("DG3MNF-4","I","N","Y",0,"-","-")]
# Kontrolle: #N = Spaltensumme
for j, r in enumerate(ROWATTR, start=1):
    assert r[4] == sum(1 for x in COUNTS if j in COUNTS[x]), (r[0], r[4])

def appendices(ramrows):
    H = []
    H.append('<h2 id="a">Anhang: Befunde und Rohdaten</h2>'
             '<p>Die Anhaenge sammeln, was das Papier oben verwendet, in zitierfaehiger Form: Umbaustellen im Code, '
             'der Schnappschuss als Testdatensatz, die Baudaten aller Umgebungen, die Flottenmessung und ein nummeriertes Befundregister.</p>')
    # A: Umbaustellen
    H.append('<h3 id="aa">Anhang A: Umbaustellen im Code fuer Stufe 1 und 1b</h3>')
    H.append(tbl(["Datei", "Stelle", "Heute", "Umbau"], [
     ("src/nbr_matrix.h", "78 bis 170", "NbrRow 24 B, NbrCell 6 B, cells[N][N]", "NbrRow 36 B mit hears/heardby (uint64_t), lat16/lon16; NbrEdge 6 B; edges[NBR_MAX_EDGES]; static_assert NBR_MAX_ROWS &lt;= 64"),
     ("src/nbr_matrix.h", "440 bis 520", "Masken-API uint32_t: nbrDirectMask, nbrHeardMeMask, nbrHearersMask, nbrCoverMask, NbrNeed", "uint64_t; nbrMaskCount ueber __builtin_popcountll"),
     ("src/nbr_matrix.cpp", "35 Zugriffe m.cells[x][y] in 17 Funktionen", "nbrInit 151/156, nbrZeroRowAndColumn, nbrMaybeSweep 291, nbrApplyGroup 409, nbrFillUnknownSnr 452, nbrNoteFrame 565/655/679, nbrNoteReport 928/943, nbrHearers 987, nbrHeardDirectly 1002, nbrRowExclusive 1014, nbrRowMeshNeedCount 1072/1084, nbrHeardMeMask 1139, nbrHearersMask 1155, nbrHearsSym 1192/1199, nbrRelayNeed 1322, nbrReach 1454, nbrFormatRow 1488, nbrLogSnapshot 1536", "Schreiber auf nbrEdgeHit(); Leser der Entscheidung auf Masken; Leser der Anzeige auf nbrEdgeFind(); nbrBit() 64 Bit"),
     ("src/nbr_matrix.cpp", "12 nbrLog()-Schreiber", "[NBR]-Zeilen ME, EDGE, EVICT, SYM, RPT, SNAP, ROW, NEED", "Format unveraendert (docs/nbr-logformat.md); neu EVICT-E fuer verdraengte Kanten"),
     ("src/web_functions/web_functions.cpp", "1582, 1662, 1755, 1830", "nbrRowIsDirect(), #N-Zaehlung, D/I-SNR, Zellanzeige", "Masken fuer D/I und #N, nbrEdgeFind() fuer SNR und Zaehler"),
     ("src/command_functions.cpp", "4877, 4901 bis 4923", "--path, --neighbours", "--path hinter MC_PATHINFO; --neighbours unveraendert (nutzt nbrExclusive/nbrFormatRow)"),
     ("src/txring_functions.{h,cpp}", "60 bis 61, 696 bis 697, 764 bis 765", "ringNeed[], ringAlone[] uint32_t", "uint64_t, 320 B statt 160 B"),
     ("src/lora_functions.cpp", "840 bis 900, 1836 bis 1890", "Cover-Scan und Einreihen mit uint32_t-Lokalen", "uint64_t; Logik unveraendert"),
     ("src/configuration_global.h", "309, 320, 330, 352", "NBR_MAX_ROWS 21/21/11/13", "64 je Familie; neu NBR_MAX_EDGES 256 klassisch, 384 S3 und nRF52; MC_PATHINFO Default 1"),
     ("src/mheard_functions.cpp", "106 bis 116, 265, 478, 999, 1194", "Pfadpuffer, savePathPersistence, updateHeyPath, showPath, showPathTDECK", "alles unter #if MC_PATHINFO"),
     ("src/web_functions/web_functions.cpp, src/t-deck/tdeck_main.cpp", "1543, Menue; 264", "sub_page_path(), loadPathPersistence()", "unter #if MC_PATHINFO; String-Scan des Images auf 'Path Information'"),
     ("test/", "test_nbr_matrix (66 Tests), test_nbr_report (3), test_nbrlog, test_txring, test_txring_flood", "gruen am 23.09.", "bleiben gruen; neu: Pool voll, Zeile verdraengt loescht Kanten, Sweep loescht Bits, Replay-Paritaet gegen den Mitschnitt"),
     ("tools/nbrsnap.py, tools/nbrhopcheck.py", "Feldauswertung", "rekonstruieren die Matrix aus [NBR]-Zeilen", "unveraendert nutzbar, weil das Logformat bleibt"),
    ]))
    # B: Fixture
    H.append('<h3 id="ab">Anhang B: Schnappschuss DK5EN-98, 2026-09-24 07:25, als Testdatensatz</h3>'
             '<p>Zeilen mit den Spalten der Web-Seite, danach die 67 Kanten als Liste X,Y,Summe mit der Zellsemantik "Y hat X gehoert". '
             'Die Summe ist cnt_text + cnt_pos + cnt_hey. Eignet sich als Fixture fuer den Replay-Vergleich und als Rechenbeispiel fuer 4.2.</p>')
    H.append(tbl(["#", "Rufzeichen", "D/I", "G", "M", "#N", "#X", "Role"],
                 [(i+1, r[0], r[1], r[2], r[3], r[4], r[5], r[6]) for i, r in enumerate(ROWATTR)], num_cols=(0,5,6)))
    lines = ["X,Y,Summe"] + [f"{x},{y},{c}" for x in sorted(COUNTS) for y, c in sorted(COUNTS[x].items())]
    H.append("<pre>" + "\n".join(lines) + "</pre>")
    H.append('<p>Kontrollsummen: 67 Kanten, #N jeder Zeile ist die Spaltensumme dieser Liste; die Gateway-Zeilen (G = Y) sind 1, 2, 3, 8, 9, 13, 18; direkt gehoert sind 2, 3, 4, 5, 11.</p>')
    # C: Baudaten
    H.append('<h3 id="ac">Anhang C: Baudaten aller 32 Umgebungen, 2026-09-24</h3>'
             '<p>Sequenzieller Build auf feature-neighbour-matrix d9fc5c5a mit tools/resource_watch.py snapshot, alle 32 Umgebungen gruen. '
             'RAM-Zeile und Flash-Zeile aus dem PlatformIO-Bericht, dram0_0_seg und iram0_0_seg aus firmware.map (nur ESP32). '
             'Die T-Beam-Varianten melden 1.310.720 B RAM inklusive 4 MB PSRAM; massgeblich ist dort dram0_0_seg.</p>')
    rows = []
    for r in ramrows:
        env = r["env"]
        e = None
        try:
            e = json.loads((SCR / "resource-nbr-20260924.json").read_text()).get(env)
        except Exception:
            e = None
        if not e:
            rows.append((env, "n/a", "", "", "")); continue
        ram = f'{e["ram_used"]:,} / {e["ram_total"]:,}'.replace(",", ".")
        dram = f'{e["dram0_0_seg"]["used"]:,} / {e["dram0_0_seg"]["length"]:,}'.replace(",", ".") if "dram0_0_seg" in e else "-"
        iram = f'{e["iram0_0_seg"]["used"]:,} / {e["iram0_0_seg"]["length"]:,}'.replace(",", ".") if "iram0_0_seg" in e else "-"
        flash = f'{e["flash_used"]:,} / {e["flash_total"]:,}'.replace(",", ".")
        rows.append((env, ram, dram, iram, flash))
    H.append(tbl(["Umgebung", "RAM-Zeile (B)", "dram0_0_seg (B)", "iram0_0_seg (B)", "Flash (B)"], rows, num_cols=(1,2,3,4)))
    H.append('<p>Lesart: auf klassischem ESP32 liegt iram0_0_seg bei 126.100 von 131.072 B; das ist kein Anwendungscode und durch Refactoring nicht zu gewinnen. '
             'Der statische DRAM-Bedarf der klassischen Familie ist seit der Baseline vom 11.09. um rund 19 kB gesunken (Byte-Ringe, 20.09.).</p>')
    # D: Flottenmessung
    H.append('<h3 id="ad">Anhang D: Flottenmessung mcmap, 2026-09-24</h3>'
             '<p>Vom Betreiber uebergeben. Der gemeldete Nachbarzaehler stammt aus getMheardCount() (src/mheard_functions.cpp:691): '
             'direkt auf HF gehoerte Stationen der letzten Stunde, Eintraege bleiben 12 h in der Liste, gekappt durch MAX_MHEARD (30 klassisch, 80 S3 und nRF52). '
             'mcmap haelt nur den letzten Wert je Knoten, keine Historie. Der Linkgraph des Servers enthaelt nur Hops auf Pfaden, die ein Gateway erreicht haben.</p>')
    H.append(tbl(["Groesse (823 Knoten)", "Median", "p90", "p99", "Maximum"], [
        ("Gemeldeter Nachbarzaehler, 1 h", "3", "11", "30", "37 (OE3GXW-1, OE1FUC-12)"),
    ], num_cols=(1,2,3)))
    H.append(tbl(["Fenster", "1-Hop max", "2-Hop p90", "2-Hop p99", "2-Hop max"], [
        ("1 h", "11", "12", "22", "25"), ("6 h", "25", "33", "58", "70"), ("24 h", "41", "48", "84", "113"),
        ("7 d", "59", "80", "133", "326 (HB4LO-7, vermutlich Ausreisser, ungeprueft)"),
    ], num_cols=(1,2,3,4)))
    H.append('<ul><li>Der Linkgraph unterzaehlt 1-Hop-Nachbarn um den Faktor 3 bis 4 gegenueber dem gemeldeten Zaehler.</li>'
             '<li>Fuenf von 262 TLORA-V2.1.6-Knoten melden genau 30 und sind vermutlich gesaettigt.</li>'
             '<li>Die staerksten Knoten (IZ5RGO-10, IW5EIA-12, DB0HOB-12) hoeren 7 bis 12 Stationen je Stunde, aber 40 bis 55 in 7 Tagen: das Fenster zaehlt mehr als der Knoten.</li>'
             '<li>Keine der beiden Quellen liefert einen verlaesslichen 2-Hop-Zaehler: die Knoten melden nur Summen, der Linkgraph sieht nur gatewaynahe Links.</li></ul>')
    # E: Befundregister
    H.append('<h3 id="ae">Anhang E: Befundregister</h3><p>Ein Befund je Zeile, mit Quelle, damit spaetere Arbeiten ihn zitieren koennen, ohne das Papier neu zu lesen.</p>')
    F = [
     ("F1", "84 % des Matrixspeichers sind Zellen, 84 % der Zellen sind leer: 67 von 420 belegt bei 21 Zeilen.", "[NBR]|SNAP 660, DK5EN-98, 24.09. 07:02; Strukturen nbr_matrix.h"),
     ("F2", "Die Zeilen sind der Engpass: 21 von 21 belegt, 16 Verdraengungen in 11 h; 46 Rufzeichen in 12 h im Lauf 1.", "Mitschnitt 24.09.; docs/nbr-wichtigkeit-konzept.md 2.3"),
     ("F3", "Jede weitere Zeile kostet heute 24 + 6 x (2N + 1) Byte, bei N = 21 also 282 B.", "gerechnet"),
     ("F4", "Die Sendeentscheidung liest nur Spalte 0, Zeile 0, die Hoerer der Pfadteilnehmer und die Spalten der Versorger; alles direkte Zeilen.", "src/nbr_matrix.cpp 1225 bis 1300"),
     ("F5", "Spalten indirekter Zeilen werden nur durch weitergeleitete HN-Berichte gefuellt, weil Pfadpaar-Kanten immer in der Spalte des letzten Hops landen; Leser sind nur nbrRowExclusive(), das LEAF-Urteil und die Entfernung.", "src/nbr_matrix.cpp 655, 928 bis 943, 1005, 1036, 1454"),
     ("F6", "Ein rechteckiger Zuschnitt (alle Zeilen, nur direkte Spalten) spart am Super-Node nichts, weil dort 20 bis 40 Spalten direkt sind, und braucht einen zweiten Allokator.", "Abschnitt 4.1, Abb. 4"),
     ("F7", "Masken sind uint32_t; nbrBit() liefert ab Index 32 still 0. Mehr als 32 Zeilen brauchen 64-Bit-Masken.", "src/nbr_matrix.cpp 1107 bis 1110"),
     ("F8", "Flotte: Nachbarzaehler Median 3, p90 11, p99 30, max 37 (1 h); 2-Hop in 24 h p90 48, p99 84, max 113; 1-Hop max 41 in 24 h, 59 in 7 d.", "Anhang D"),
     ("F9", "Der HEY-Zaehler ist ein 1-h-Wert, gekappt bei MAX_MHEARD; 5 von 262 TLORA V2.1.6 stehen auf 30 und sind gesaettigt.", "Anhang D"),
     ("F10", "Ein 1-h-Fenster erklaert Blaetter faelschlich exklusiv; 12 bis 24 h sind noetig. Die Matrix arbeitet mit 12 h.", "Anhang D; NBR_WINDOW_MIN"),
     ("F11", "Klassischer ESP32 ist die haeufigste Familie und steht unter den staerksten Knoten; die Dimensionierung muss dort passen.", "Betreiber, 24.09.; Anhang D"),
     ("F12", "Statischer DRAM nach dem RAM-Rueckgewinn: klassisch mindestens 29,5 kB frei, XML 24,8 kB, S3 mindestens 141 kB, nRF52 mindestens 150 kB; alle 32 Umgebungen bauen gruen.", "Anhang C"),
     ("F13", "IRAM auf klassischem ESP32 ist zu 96 % belegt, aber nicht durch Anwendungscode; kein Hebel fuer dieses Vorhaben.", "Anhang C"),
     ("F14", "Die Pfadtabelle kostet 71 B je Eintrag (2.840 klassisch, 3.550 XML, 7.100 S3 und nRF52) und speist nur Web-Seite, --path, T-Deck-Seite und SD-Sicherung; keine Entscheidung, kein BLE.", "src/loop_functions_extern.h 455 bis 458; src/mheard_functions.cpp"),
     ("F15", "Die Matrix ersetzt die Pfadtabelle nicht eins zu eins: das 2-Hop-Fenster verwirft Pfade ab drei Hops mit Absicht, die Pfadtabelle zeigt sie und das G-Bit.", "src/nbr_matrix.h 235 bis 245; sub_page_path()"),
     ("F16", "Fall A heisst alone != 0 (jemand haengt allein an mir); Basis 3500 ms, Slots 0..2, Kappung des Re-Arm ab 8 s, nie Abbruch. Fall B heisst alone == 0; einmalige Sperre 20 s, Slots 7..9, Abbruch bei Deckung, Deckel 60 s; Text bleibt bei der heutigen Basis.", "src/configuration_global.h 471 bis 477; src/txring_functions.cpp 283 bis 372"),
     ("F17", "Die je Re-Arm neu addierte Sperre kostete am ersten on-Tag 149 Relays und 10 HN-Meldungen bei 137 s Median-Wartezeit; als einmalige Frist sind es 0 Verluste und p90 42 s.", "docs/nbr-stage2-campaign.md Welle 5; Auswertung 24.09."),
     ("F18", "Nach Welle 5 und 6: 48 eigene Relays je Stunde (vorher 76), 208 Abbrueche bei 661 Fall-B- und 81 Fall-A-Entscheidungen, 0 RING_DROP, kein Neustart.", "Auswertung 24.09. 07:05"),
     ("F19", "Der Super-Node DL2JA-2 wiederholt im Median 109 s nach der ersten Kopie, weil jeder Empfang seinen Backoff neu startet; jeder gehoerte Frame kostet 6 bis 9 s. Das aendert sich erst mit der Fall-A-Kappung auf seinem Geraet.", "docs/nbr-wichtigkeit-konzept.md 2.4"),
     ("F20", "Ohne --nbrsym stand DL2JA-1 (relayt nie) dauerhaft in der Allein-Maske und zwang 96 % aller Relays in Fall A.", "docs/nbr-wichtigkeit-konzept.md 4"),
     ("F21", "Rolle: #X = Knoten, die der Nachbar hoert und sonst niemand in meiner Hoerweite; Super bei #X >= 2 und doppeltem Abstand; Needed ab 1; Redundant bei 0. #N taugt nicht: DB0ED-99 hoert 10 mit #X 0.", "web_functions.cpp 1670 bis 1700; Abb. 7"),
     ("F22", "Die Kappung der Zeilen aendert die Zahl, nicht die Ordnung: 10 zu 1 statt 29 zu 1. Das #X eines Gateway-Nachbarn ist um Serverzufuhr ueberhoeht (29 gemeldet, 12 in Reichweite).", "docs/nbr-wichtigkeit-konzept.md 2.1"),
     ("F23", "Rechenbeispiel: Frame OE7XWT-12 ueber DL2JA-2 ergibt need = {DL2JA-1}; ohne Symmetrie Fall A, mit Symmetrie Fall B.", "Abschnitt 4.2, Anhang B"),
     ("F24", "Kantenpool Stufe 1: 64 Zeilen zu 36 B plus 256 Kanten zu 6 B = 3.848 B klassisch, 384 Kanten = 4.616 B auf S3 und nRF52; netto mit abgeschalteter Pfadtabelle 322 B weniger als heute auf klassischem ESP32.", "Abschnitt 4.5"),
     ("F25", "Alle Entscheidungsmengen sind 64-Bit-Woerter; eine Relay-Entscheidung braucht unter 150 Wortoperationen statt rund 600 Zellzugriffen mit Frischetest.", "Abschnitt 4.2"),
    ]
    H.append(tbl(["Nr", "Befund", "Quelle"], F))
    return "".join(H)

# ---------------------------------------------------------------- Dokument
def build():
    ramrows = load_ram()
    css = STYLE.read_text()
    css += """
  svg .wwwf { fill:#e9f1fd; }
  .bits { font-family: ui-monospace, Menlo, monospace; font-size: 12px; letter-spacing: 1px; }
  .tag.step { background:#5b6b7b; }
"""
    sz = {f: fam_sizes(f) for f in FAM}
    cl, s3 = sz["classic"], sz["s3"]
    n_fresh = sum(1 for r in ramrows if r["src"].startswith("2026-09-24"))
    if n_fresh == 32:
        ram_src_note = "alle 32 Umgebungen frisch gebaut am 2026-09-24 auf feature-neighbour-matrix d9fc5c5a, sequenziell mit tools/resource_watch.py snapshot"
    else:
        ram_src_note = (f"{n_fresh} von 32 Umgebungen frisch gebaut am 2026-09-24 auf feature-neighbour-matrix d9fc5c5a, "
                        f"die uebrigen aus tools/resource_baseline.json vom 2026-09-11 (fork-main, vor dem RAM-Rueckgewinn)")
    boards = [r for r in ramrows if r["fam"] != "safeboot" and r["used"]]
    tight = min(boards, key=lambda r: r["length"]-r["used"])
    tight_note = f'Die engste Umgebung ist {tight["env"]} mit {kb(tight["length"]-tight["used"])} kB Reserve im statischen Segment; jede klassische Umgebung hat mindestens {kb(min(r["length"]-r["used"] for r in boards if r["fam"] in ("classic","xml")))} kB, seit der RAM-Rueckgewinn vom 20.09. die drei Ausgangsringe auf Byte-Ringe umgestellt hat.'

    H = []
    H.append(f"""<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<title>Nachbarschaftsmatrix als Kantenpool</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>{css}</style></head><body><main>
<h1>Nachbarschaftsmatrix als Kantenpool: Speicher, Sendeentscheidung, Rollen</h1>
<ul class="meta">
<li>Stand: 2026-09-24</li>
<li>Autor: DK5EN, mit Claude</li>
<li>Codebasis: MeshCom-Firmware Fork, Branch <code>feature-neighbour-matrix</code> ab <code>d9fc5c5a</code></li>
<li>Quelle Matrix: <code>src/nbr_matrix.h</code> (Strukturen 78 bis 170, Masken 440 bis 520), <code>src/nbr_matrix.cpp</code> (Entscheidung 1107 bis 1400)</li>
<li>Quelle Relay-Faelle: <code>src/configuration_global.h</code> 471 bis 477, <code>src/txring_functions.cpp</code> 283 bis 372, <code>src/lora_functions.cpp</code> 840 bis 900 und 1836 bis 1890</li>
<li>Quelle Rollen: <code>src/web_functions/web_functions.cpp</code> <code>sub_page_neighbours()</code> 1596 bis 1820</li>
<li>Quelle Pfadtabelle: <code>src/mheard_functions.cpp</code> <code>updateHeyPath()</code> 478 bis 662, <code>sub_page_path()</code> 1543, <code>src/configuration_global.h</code> 301 bis 353</li>
<li>Quelle Feldzahlen: <code>docs/nbr-wichtigkeit-konzept.md</code> Abschnitt 2, <code>docs/nbr-stage2-campaign.md</code> Wellen 5 und 6, Mitschnitt <code>rpizero:~/meshlog/dk5en-98/2026-09-24.log</code></li>
<li>Quelle Flottenzahlen: mcmap-Produktionsschnappschuss vom 2026-09-24 (823 Knoten), vom Betreiber uebergeben</li>
<li>Quelle RAM: <code>tools/resource_watch.py snapshot</code>; {ram_src_note}</li>
<li>Rufzeichen: Schnappschuss von DK5EN-98; fremde Rufzeichen erscheinen nur als Beobachtung, nie als Sender</li>
</ul>

<h2 id="s1">1. Ergebnis</h2>
<div class="bluf">
<p>Die Nachbarschaftsmatrix speichert heute N mal N Zellen zu 6 Byte. Bei 21 Zeilen sind das 2,6 von 3,15 kB, jede weitere Zeile kostet 282 Byte, und der Speicher waechst quadratisch. Im Feld sind 67 von 420 Zellen belegt, waehrend 46 Rufzeichen in 12 Stunden um 21 Zeilen konkurrieren. Die Flotte zeigt, wo das zaehlt: 823 Knoten melden im Median 3 Nachbarn, die staerksten 30 bis 37 in einer Stunde und ueber 40 in 24 Stunden, ihre 2-Hop-Nachbarschaft haelt in 24 Stunden 48 (p90) bis 113 Stationen. Die Matrix laeuft also genau an den Super-Nodes voll, und dort entscheidet heute die Verdraengung ueber die Rolle. Die meisten dieser Knoten sind klassische ESP32 mit 13 Zeilen.</p>
<p>Daraus folgt:</p>
<ol>
<li><b>Stufe 1, Kantenpool und Masken:</b> Zellen werden zu Eintraegen eines Pools fester Groesse, jede Zeile traegt zwei 64-Bit-Masken, die Sendeentscheidung und die Rollen rechnen nur noch auf den Masken. 64 Zeilen und 256 Kanten kosten {cl['s1']} Byte auf klassischem ESP32, 64 Zeilen und 384 Kanten {s3['s1']} Byte auf S3 und nRF52. Funkverhalten und Logformat bleiben identisch.</li>
<li><b>Stufe 1b, Pfadtabelle hinter Compile-Flag:</b> Default an, im Fork auf klassischem ESP32 aus. Das gibt dort {cl['path']} Byte frei; Pool und Masken brauchen {cl['s1']-cl['dense']} Byte mehr als die heutige Matrix, netto bleiben {cl['dense']+cl['path']-cl['s1']} Byte uebrig.</li>
<li><b>Stufe 2, nur S3 und nRF52:</b> 128 Zeilen mit 128-Bit-Masken und 512 Kanten fuer {s3['s2']} Byte, wenn der Feldlauf zeigt, dass der Rang an Super-Nodes mit 64 Zeilen zu stark gekappt wird.</li>
</ol>
<p class="reco">Empfehlung: Stufe 1 und 1b nach der Auswertung von Feldlauf 2 (ab 2026-09-23 20:01) umsetzen, Abnahme ueber den Replay-Vergleich der [NBR]-Zeilen im Host-Test; Stufe 2 erst nach 24 Stunden Feld auf einem Super-Node.</p>
</div>

<h3>Entscheidungen zu den Anforderungen</h3>
{tbl(["Nr", "Anforderung", "Entscheidung", "Stufe", "Abschnitt"], [
 ("1", "Was ein Kantenpool ist", "Pool fester Groesse fuer belegte Zellen, zwei Masken je Zeile, Verdraengung nach Alter", "1", "4.1"),
 ("2", "Grafiken", "Abb. 1 bis 8, davon Abb. 6 animiert", "-", "2 bis 4"),
 ("3", "Was zu Fall A und Fall B fuehrt, mit Folgen", "Allein-Menge nicht leer heisst Fall A; Folgen aus zwei Feldtagen", "0", "4.3"),
 ("4", "Entscheider fuer Super-Node und Redundant", "#X je direktem Nachbarn, Super bei #X &gt;= 2 und doppeltem Abstand", "0", "4.4"),
 ("5", "Speichereffiziente Rechnung", "Alle Mengen als 64-Bit-Masken, popcount, O(N) Wortoperationen", "1", "4.2"),
 ("6", "Uebersicht 32 Plattformen", "RAM-Tabelle und Abb. 8 aus dem Build aller Umgebungen", "-", "4.5"),
 ("7", "Pfadtabelle behalten oder abschalten", "Compile-Flag, Default an, klassischer ESP32 im Fork aus", "1b", "4.6"),
])}

<h3>Ausbaustufen</h3>
{tbl(["Stufe", "Was steht", "Erfuellt", "Luecke am Super-Node", "Luecke auf klassischem ESP32", "Voraussetzung"], [
 ("0 heute", "Dichte Matrix 21/13 Zeilen, Fall A/B, Rollen", "Sendeentscheidung, Rang bis 21 Zeilen", "40 Nachbarn passen nicht in 21 Zeilen, Verdraengung entscheidet", "13 Zeilen, Rang nur bis 12 Nachbarn", "-"),
 ("1", "Kantenpool, 64-Bit-Masken, 64 Zeilen", "1-Hop-Maximum 41 und 2-Hop p90 48 der Flotte in 24 h", "2-Hop p99 84 wird gekappt, Ordnung bleibt", "keine, 64 Zeilen auch dort", "Replay-Paritaet im Host-Test"),
 ("1b", "Pfadtabelle per Flag aus (klassisch)", "64 Zeilen auf klassischem ESP32 fuer 322 B weniger als heute", "-", "kein --path, keine Pfadseite", "Upstream-Default bleibt an"),
 ("2", "128 Zeilen, 128-Bit-Masken (S3, nRF52)", "2-Hop p99 84 und Maximum 113", "-", "nicht vorgesehen", "Feldnachweis, dass 64 Zeilen den Rang verfaelschen"),
])}

<h3>Inhalt</h3>
<ol class="toc">
<li><a href="#s1">Ergebnis</a></li>
<li><a href="#s2">Ausgangslage</a></li>
<li><a href="#s3">Zielbild</a></li>
<li><a href="#s4">Bausteine</a>: 4.1 Kantenpool, 4.2 Rechnen auf Masken, 4.3 Fall A und Fall B, 4.4 Super-Node und Redundant, 4.5 RAM der 32 Plattformen, 4.6 Pfadtabelle</li>
<li><a href="#s5">Fehlerszenarien</a></li>
<li><a href="#s6">Rollout</a></li>
<li><a href="#s7">Offene Punkte</a></li>
<li><a href="#a">Anhang</a>: A Umbaustellen, B Schnappschuss als Testdatensatz, C Baudaten der 32 Umgebungen, D Flottenmessung, E Befundregister</li>
</ol>

<h2 id="s2">2. Ausgangslage</h2>
<p class="reco">Befund: Der Speicher sitzt in den Zellen, der Engpass in den Zeilen. Die Sendeentscheidung braucht von den Zellen nur Spalte 0, Zeile 0 und die Spalten der direkten Nachbarn.</p>
{tbl(["Element", "Wert", "Quelle"], [
 ("Zeile <code>NbrRow</code>", "24 B: call[10], rpt_min, lat, lon, last_min, flags, hw", "src/nbr_matrix.h:84"),
 ("Zelle <code>NbrCell</code>", "6 B: cnt_text, cnt_pos, cnt_hey, snr, last_min; cell[X][Y] = 'Y hat X gehoert'", "src/nbr_matrix.h:150"),
 ("Zeilen je Board", "21 auf S3, nRF52 und ENABLE_XML; 13 auf klassischem ESP32; 11 in der Entwicklervariante", "src/configuration_global.h:309, 320, 330, 352"),
 ("Groesse heute", f"21 Zeilen {dense(21)} B, 13 Zeilen {dense(13)} B; jede weitere Zeile bei 21: 282 B", "gerechnet aus den Strukturen"),
 ("Fenster", "12 h (NBR_WINDOW_MIN 720), Geister-Sweep alle 1024 min gegen den 16-Bit-Ueberlauf", "src/nbr_matrix.h:59 bis 70"),
 ("Masken", "uint32_t ueber Zeilenindizes, nbrBit() liefert 0 ab Index 32", "src/nbr_matrix.cpp:1107"),
 ("Entscheidung liest", "Spalte 0 (Direkt), Zeile 0 (HoertMich), Hoerer der Pfadteilnehmer, Spalten der Versorger", "src/nbr_matrix.cpp:1225 bis 1300"),
 ("Indirekte Spalten", "gefuellt nur durch weitergeleitete HN-Berichte; gelesen nur von nbrRowExclusive(), LEAF-Urteil und Entfernung", "src/nbr_matrix.cpp:1005, 1036, 1454"),
 ("Pfadpaar-Kanten", "landen immer in der Spalte des letzten Hops, der per Definition direkt ist", "src/nbr_matrix.cpp:655"),
 ("Feld DK5EN-98", "67 von 420 Zellen belegt, 21 von 21 Zeilen, 16 Verdraengungen in 11 h, 0 DROP FULL", "[NBR]|SNAP 660 im Mitschnitt vom 24.09."),
 ("Feld DK5EN-98, Lauf 1", "46 Rufzeichen in 12 h auf 21 Zeilen, 56 Verdraengungen in 11,9 h, davon 2 auf direkte Zeilen", "docs/nbr-wichtigkeit-konzept.md 2.3"),
 ("Pfadtabelle", "71 B je Eintrag; 100 Eintraege auf S3/nRF52, 40 klassisch, 50 XML", "src/loop_functions_extern.h:455 bis 458"),
])}
{fig_ist()}
<h3>Mengengeruest</h3>
<p>Die Flottenzahlen stammen aus dem mcmap-Schnappschuss vom 24.09. Der gemeldete Nachbarzaehler kommt aus <code>getMheardCount()</code> und zaehlt eine rollende Stunde, gekappt durch MAX_MHEARD (30 klassisch, 80 auf S3 und nRF52); fuenf von 262 TLORA-V2.1.6-Knoten stehen genau auf 30 und sind vermutlich gesaettigt. Der Linkgraph des Servers kennt nur Hops, die ein Gateway erreicht haben, und unterzaehlt 1-Hop-Nachbarn um den Faktor 3 bis 4. Beides sind Untergrenzen.</p>
{tbl(["Groesse", "heute", "Ziel", "Bemerkung"], [
 ("Nachbarn je Knoten, 1 h (823 Knoten)", "Median 3, p90 11, p99 30, max 37", "-", "gemeldeter Zaehler, gekappt bei 30 auf klassischem ESP32"),
 ("1-Hop-Nachbarn, Maximum", "1 h: 11, 6 h: 25, 24 h: 41, 7 d: 59", "64 Zeilen", "aus dem Linkgraph, Untergrenze"),
 ("2-Hop-Nachbarschaft, 24 h", "p90 48, p99 84, max 113", "64 Zeilen (Stufe 1), 128 (Stufe 2)", "aus dem Linkgraph, Untergrenze"),
 ("2-Hop-Nachbarschaft, 6 h", "p90 33, p99 58, max 70", "64 Zeilen", "-"),
 ("Zeilen DK5EN-98", "21 von 21 voll, 46 Rufzeichen in 12 h", "64", "kein Super-Node, hoert 5 direkt"),
 ("Kanten DK5EN-98", "67 belegt, 3,2 je Zeile", "4 je Zeile, 256 Kanten", "Annahme fuer Super-Nodes: mehr Berichte je Zeile, Stufe 1 auf S3/nRF52 deshalb 6 je Zeile"),
 ("Fenster", "12 h", "12 h, spaeter 24 h pruefen", "Flottenmessung: 1 h erklaert Blaetter faelschlich exklusiv; 12 bis 24 h noetig"),
 ("Boards im Feld", "klassischer ESP32 ist die haeufigste Familie", "Stufe 1 muss dort passen", "Betreibervorgabe 2026-09-24"),
])}

<h2 id="s3">3. Zielbild</h2>
<p class="reco">Entscheidung: Zeilen und Kanten trennen. Zeilen bleiben ein Feld fester Groesse mit zwei Bitmasken, Kanten liegen in einem zweiten Feld fester Groesse. Jede Entscheidung laeuft auf den Masken, jede Anzeige auf dem Pool.</p>
{fig_ziel()}
{tbl(["Komponente", "Ort", "Rolle"], [
 ("<code>NbrRow</code>, 36 B", "src/nbr_matrix.h", "call[10], lat16, lon16 in 0,01 Grad, last_min, rpt_min, flags, hw, hears (uint64_t), heardby (uint64_t)"),
 ("<code>NbrEdge</code>, 6 B", "src/nbr_matrix.h", "x, y, cnt, snr, last_min; frei bei x == 0xFF"),
 ("<code>nbrEdgeFind(m, x, y)</code>", "src/nbr_matrix.cpp", "linearer Scan ueber den Pool, ersetzt jeden Zugriff <code>m.cells[x][y]</code>; NULL, wenn keine Kante"),
 ("<code>nbrEdgeHit(m, x, y, typ, snr, now)</code>", "src/nbr_matrix.cpp", "Kante anlegen oder treffen, beide Masken setzen, bei vollem Pool aelteste Kante freigeben"),
 ("<code>nbrSweep(m, now)</code>", "src/nbr_matrix.cpp", "je Minute: Kanten aelter als das Fenster freigeben und Bits loeschen; ersetzt den Frischetest je Zelle"),
 ("Masken-API", "src/nbr_matrix.cpp", "nbrDirectMask, nbrHeardMeMask, nbrHearersMask werden Einzeiler auf rows[].hears / heardby"),
 ("Web, Kommando, Log", "web_functions.cpp, command_functions.cpp, nbr_matrix.cpp", "lesen Zaehler und SNR ueber nbrEdgeFind(); Zeilenformat der [NBR]-Zeilen unveraendert"),
 ("Host-Test", "env native_nbr_matrix, tools/nbrsnap.py", "Replay des Feldlogs: SNAP-, ROW- und NEED-Zeilen muessen byte-identisch bleiben"),
])}
""")

    H.append(f"""
<h2 id="s4">4. Bausteine</h2>
<h3 id="s41">4.1 Kantenpool</h3>
<p class="reco">Entscheidung: Ein Pool fester Groesse mit 6-Byte-Eintraegen ersetzt die N mal N Zellen; die Kapazitaet haengt nicht mehr an der Zeilenzahl.</p>
<p>Eine Zelle der heutigen Matrix ist eine Beobachtung "Y hat X gehoert" mit drei Zaehlern nach Frametyp, dem zuletzt gesehenen SNR und einer Minute. Von 420 moeglichen Beobachtungen hat DK5EN-98 nach elf Stunden 67. Ein Kantenpool speichert nur diese 67: jeder Eintrag traegt die beiden Zeilenindizes x und y dazu, sonst denselben Inhalt. Die Tabelle ist damit eine Liste von Kanten eines Graphen, keine Kreuztabelle mehr. Wer eine Zelle sucht, durchlaeuft die Liste; wer weiss, welche Zeilen sich hoeren, liest die Masken der Zeilen, die jede Kante beim Anlegen setzt. Die Praemisse fuer die Groesse: 67 Kanten auf 21 Zeilen sind 3,2 je Zeile, ein Super-Node bekommt ueber HN-Berichte bis zu 8 Kanten je berichtendem Nachbarn dazu, darum 4 je Zeile auf klassischem ESP32 und 6 auf S3 und nRF52.</p>
{fig_pool()}
{tbl(["Option", "Mechanik", "Bewertung"], [
 ("Dichte Matrix, wie heute", "cells[N][N] x 6 B", '<span class="tag bad">verworfen</span> quadratisch: 64 Zeilen kosten 26 kB'),
 ("Zelle auf 4 B kuerzen", "ein Zaehler statt drei, uint16 Minute", '<span class="tag bad">verworfen</span> spart ein Drittel, bleibt quadratisch'),
 ("Rechteckig: alle Zeilen, nur direkte Spalten", "cells[N][M], M Spaltenslots mit eigener Verdraengung", '<span class="tag bad">verworfen</span> am Super-Node ist M = 20 bis 40, dazu ein zweiter Allokator; Abb. 4'),
 ("Kantenpool plus zwei Masken je Zeile", "edges[E] x 6 B, hears/heardby als uint64_t", '<span class="tag ok">gewaehlt</span> linear in Zeilen und Kanten, Entscheidung ohne Poolzugriff'),
 ("Nur Masken, kein Pool", "rows mit Masken, keine Zaehler, kein SNR je Kante", '<span class="tag warn">Teilmenge</span> Entscheidung geht, aber die Symmetrie-Annahme braucht den SNR der Gegenkante; ohne ihn faellt DL2JA-1 zurueck in Fall A (96 % der Relays im Lauf 1)'),
])}
{fig_growth()}
<p>Regeln fuer den Pool:</p>
<ul>
<li>Eine Kante entsteht beim ersten Treffer und setzt zwei Bits: Bit x in <code>rows[y].hears</code>, Bit y in <code>rows[x].heardby</code>. Die Diagonale bleibt leer.</li>
<li>Der Sweep laeuft einmal je Minute ueber den Pool statt alle 1024 Minuten ueber alle Zellen; eine Kante aelter als das Fenster wird frei, ihre zwei Bits werden geloescht. Damit entfaellt der Frischetest je Zellzugriff in jeder Entscheidung.</li>
<li>Ist der Pool voll, weicht die aelteste Kante, die weder Spalte 0 noch Zeile 0 beruehrt; erst danach die aelteste ueberhaupt. Das schuetzt Direkt und HoertMich, die Grundlage jeder Entscheidung.</li>
<li>Wird eine Zeile verdraengt, werden ihre Kanten frei und ihr Bit in allen Masken geloescht, ein Durchlauf ueber Pool und Zeilen.</li>
<li>Die Suche nach einer Kante ist ein linearer Scan ueber hoechstens 384 Eintraege. Je Frame fallen hoechstens 8 Pfad-Token und 8 Berichtseintraege an, also unter 20 Suchen; bei einem Frame je 14 s ist das bedeutungslos, bei fuenf Frames je Sekunde am Super-Node unter 40.000 Vergleiche je Sekunde.</li>
</ul>

<h3 id="s42">4.2 Rechnen auf Masken</h3>
<p class="reco">Entscheidung: Jede Menge der Sendeentscheidung und jede Rolle ist ein 64-Bit-Wort; die Rechnung besteht aus OR, AND NOT und popcount ueber hoechstens N Woerter.</p>
<p>Die Praemisse: die Entscheidung fragt nie nach Zaehlern oder Minuten, nur nach "frisch und gesetzt". Genau das ist ein Bit, sobald der Sweep veraltete Kanten selbst entfernt. Mit Bit 0 fuer die eigene Zeile:</p>
<pre>Direkt        = rows[0].hears                       // ich hoere X
HoertMich     = rows[0].heardby                     // X hat mich gehoert
D             = (Direkt | HoertMich) &amp; ~GW &amp; ~1      // Abhaengige, Gateways bekommen den Frame vom Server
Pfad          = OR bit(p)            fuer p im Pfad
HatF          = Pfad | OR rows[p].heardby            // wer den Frame hat oder einen Traeger gehoert hat
need          = D &amp; ~HatF
Versorger     = Direkt &amp; HatF &amp; ~angenommen
gedeckt       = OR rows[M].heardby   fuer M in Versorger   // X hat M gehoert
alone         = need &amp; ~gedeckt                     // Fall A, wenn != 0
Deckung(L)    = rows[L].heardby                     // beim Mithoeren einer Kopie von L
hoert(N)      = rows[N].hears &amp; ~1 &amp; ~bit(N)
#X(N)         = popcount(hoert(N) &amp; ~Direkt &amp; ~OR hoert(M) fuer M in Direkt, M != N)
E_self        = Direkt &amp; ~OR hoert(M) fuer M in Direkt</pre>
<p>Kosten je Relay-Entscheidung: heute bis zu (N-1) mal (Pfadlaenge + N-1) Zellzugriffe mit Frischetest, bei N = 21 rund 600; auf Masken sind es Pfadlaenge plus zweimal N Wortoperationen, bei N = 64 unter 150, ohne einen einzigen Poolzugriff. Die Symmetrie-Annahme ist der einzige Leser des Pools in der Entscheidung: sie braucht den SNR der Gegenkante "M hat X gehoert" und nur dann, wenn die Beobachtung fehlt. Das Rechenbeispiel aus dem Schnappschuss 07:25 zeigt beide Faelle an einem Frame von OE7XWT-12 ueber DL2JA-2:</p>
{example_table()}
<p>Speicher fuer die Entscheidung: zwei Woerter je Zeile, 16 B mal 64 Zeilen gleich 1 kB, in den 36 B je Zeile enthalten. Zwei Ring-Seitenfelder <code>ringNeed[]</code> und <code>ringAlone[]</code> werden uint64_t, 2 mal 20 mal 8 B gleich 320 B statt 160 B.</p>

<h3 id="s43">4.3 Fall A und Fall B</h3>
<p class="reco">Entscheidung: unveraendert gegenueber Stufe 2 des Konzepts; der Kantenpool aendert nur die Rechnung, nicht die Regel.</p>
<p>Ein Relay-Kandidat bekommt beim Einreihen zwei Masken: <code>need</code>, die direkten Nachbarn und Rueckhoerer, die den Frame noch nicht haben koennen, und <code>alone</code>, die davon, die ihn von niemandem sonst bekommen koennen. Ist <code>alone</code> nicht leer, ist es Fall A: es haengt mindestens ein Knoten allein an mir. Ist <code>alone</code> leer, ist es Fall B, auch wenn <code>need</code> leer ist. Hat die Matrix kein Wissen, weil der Pfad ungueltig ist oder keine abhaengige Zeile existiert, gibt es keinen Fall, das Relay laeuft wie heute.</p>
{fig_flow()}
{tbl(["Fall", "Basis und Slots", "Re-Arm bei fremdem Empfang", "Abbruch", "Zweck"], [
 ("A", "3500 ms + Slots 0..2, vor Relay 4500, POS und HEY 5500, hinter ACK und DM 3000", "bis 8 s Wartezeit voll; danach nur 150 ms Schutzabstand plus Jitter, dann CAD", "nie; fremde Kopie erzeugt eine REFUSE-Zeile", "der Knoten, an dem Blaetter haengen, kommt zuerst und verhungert nicht mehr"),
 ("B, POS und HEY", "Prio-Basis + Slots 7..9; einmalige Sperre 20 s ab Einreihen, kein Aufaddieren je Re-Arm", "innerhalb der Sperre max(Rest, Basis); ab 60 s Kurzsuche wie A", "sobald gehoerte Kopien need auf 0 bringen", "die Flut der Nachbarn abwarten und dann nur senden, wenn noch jemand fehlt"),
 ("B, Text", "heutige Basis und Slots", "wie heute", "wie B", "Menschen warten auf Text"),
 ("kein Wissen", "Prio-Basis wie heute", "wie heute", "nie", "nichts unterdruecken auf Verdacht"),
])}
<p>Die Praemissen der Zahlen: 3500 ms liegt zwischen ACK und dem heutigen Relay, damit ein Fall-A-Relay jede Wiederholung der Alt-Firmware schlaegt. 20 s Sperre faengt die Haelfte der fremden Kopien am Standort DK5EN-98 (Median 19 bis 26 s), 82 % der Abbrueche fielen im Feld in die ersten 60 s, daher der 60-s-Deckel. 8 s fuer die Re-Arm-Kappung entsprechen etwa zwei vollen Backoffs.</p>
{fig_timeline()}
<p>Folgen, aus dem Feld gemessen:</p>
{tbl(["Was", "vor Stufe 2 (Lauf 1, 11,9 h)", "erster Tag --nbrrelay on (23.09., 9 h)", "nach Welle 5 und 6 (24.09., 11 h)"], [
 ("Eigene Relays je Stunde", "76", "22", "48"),
 ("Verzoegerung der Super-Node-Kopie DL2JA-2", "109 s Median, TX-verhungert durch Re-Arm", "unveraendert, Alt-Firmware", "unveraendert, Alt-Firmware"),
 ("Fall-B-Wartezeit", "-", "Median 137 s, max 16 min: Sperre wurde je Re-Arm neu addiert", "POS p90 42 s, max 65 s; HEY p90 42 s, max 72 s"),
 ("Verworfene Relays (RING_DROP)", "0", "149 Relays und 10 HN-Meldungen", "0"),
 ("Abbrueche", "0", "62 % der Fall-B-Entscheidungen", "208 von 661 Fall B (31 %), 81 Fall A, 0 REFUSE-Fehler"),
 ("Kosten je gehoertem Frame waehrend des Wartens", "6 bis 9 s zusaetzlich", "-", "Fall A: gekappt ab 8 s"),
])}
<p>Zwei Folgen verdienen den Blick des Betreibers. Erstens: Fall A haengt an der Symmetrie-Annahme. Ohne <code>--nbrsym</code> stand DL2JA-1, das nie etwas wiederholt, dauerhaft in der Allein-Maske und zwang 96 % aller Relays in Fall A; mit der Annahme wird es gedeckt, sobald ein Versorger es mit mindestens -16 dB gehoert hat. Zweitens: der Abbruch wirkt nur bei Knoten mit der neuen Firmware. Der Super-Node DL2JA-2 sendet weiter nach 109 s, weil seine Alt-Firmware bei jedem Empfang neu wartet; erst mit der Kappung aus Fall A auf seinem Geraet sendet er als Erster, wie Abb. 6 zeigt.</p>

<h3 id="s44">4.4 Super-Node, Needed, Redundant</h3>
<p class="reco">Entscheidung: Die Rolle eines Nachbarn ist sein exklusiver Beitrag #X aus meiner Sicht; Super-Node ist der direkte Nachbar mit dem groessten #X, wenn #X mindestens 2 und mindestens doppelt so gross wie das zweitgroesste ist.</p>
<p>#X(N) zaehlt die Knoten, die N hoert und die weder ich direkt noch ein anderer direkter Nachbar hoert. Das ist <code>nbrRowMeshNeedCount()</code>, auf Masken ein popcount. Needed heisst #X mindestens 1, Redundant heisst #X gleich 0; beides gibt es nur fuer direkte Zeilen, indirekte Zeilen zeigen einen Strich. Eigene Blaetter, E_self, sind direkt gehoerte Zeilen, die kein anderer direkter Nachbar hoert; sie machen mich selbst noetig und faerben die Zeile rot. Die rohe Nachbarzahl #N taugt nicht als Rang: DB0ED-99 hoert zehn, aber sieben davon hoert auch DL2JA-2 und drei hoere ich selbst.</p>
{fig_graph()}
<p>Der Rang wird fuer die Sendeentscheidung nicht gebraucht: sie haengt nur an der eigenen Bedarfsmenge, und die Ordnung entsteht daraus von selbst. Ein Knoten mit vielen exklusiven Blaettern ist bei fast jedem Frame in Fall A und sendet vorn; ein Knoten ohne exklusive Blaetter ist in Fall B, wartet und bricht ab. Zwei Fall-A-Knoten nebeneinander trennen sich ueber CAD und den Abbruch beim Verlierer. Es gibt keine Election, kein neues Frame und kein neues Feld. Der Rang bleibt Anzeige und das wertvollste Diagnoseprodukt der Matrix: eine ueber Wochen exklusive Zeile ist der dokumentierte Fall fuer einen zweiten Standort.</p>
{tbl(["Option fuer den Rang", "Mechanik", "Bewertung"], [
 ("#X aus meiner Matrix", "popcount ueber Masken", '<span class="tag ok">gewaehlt</span> lokal, ohne Funkverkehr, Ordnung stabil ueber 46 Schnappschuesse'),
 ("Nachbarzaehler NCT aus dem HEY", "R&lt;n&gt; je Nachbar", '<span class="tag bad">verworfen</span> ueberschaetzt DB0ED-99 um Faktor 10, Untergrenze durch MAX_MHEARD'),
 ("Election ueber HEY-Ziel HN", "neues Ziel, Rang auf Luft", '<span class="tag bad">verworfen</span> jede heutige Firmware verwirft das Ziel beim Dekodieren'),
 ("Entfernungsgewicht", "#X mal f(Reichweite)", '<span class="tag bad">verworfen</span> zaehlt doppelt, braucht Kalibrierdaten; nur Anzeige'),
])}
<p>Was die Kappung der Zeilen mit dem Rang macht, ist gemessen: mit 21 Zeilen sah DK5EN-98 fuer DL2JA-2 10 statt 29 exklusive Knoten, die Ordnung 10 zu 1 blieb. Mit 64 Zeilen deckt Stufe 1 die 2-Hop-Nachbarschaft von 90 % der Knoten in 24 h vollstaendig; an den staerksten Super-Nodes bleibt die Zahl gekappt, die Ordnung nicht. Die Verdraengung schuetzt direkte Zeilen, verdraengt also nur Hop-2-Zeilen und nie die Menge, auf der die Sendeentscheidung rechnet.</p>
""")

    H.append(f"""
<h3 id="s45">4.5 RAM der 32 Plattformen</h3>
<p class="reco">Entscheidung: Stufe 1 auf allen Familien mit 64 Zeilen; die Kantenzahl folgt der Familie, die Pfadtabelle finanziert den Pool auf klassischem ESP32.</p>
<p>Gemessen ist das Linker-Segment fuer statische Daten. Auf klassischem ESP32 ist <code>dram0_0_seg</code> 124.580 Byte lang, weil der Bluetooth-Stack den Rest des DRAM reserviert; der Heap entsteht aus dem Rest dieses Segments und aus zwei weiteren Regionen. Auf S3 umfasst das Segment 345.856 Byte inklusive Heap, jedes statische Byte fehlt dort dem Heap eins zu eins. nRF52 meldet die RAM-Zeile des Builds, der Heap ist der Rest der 248 kB abzueglich der Stacks. {ram_src_note}. {tight_note}</p>
{fig_ram(ramrows)}
{platform_table(ramrows)}
{tbl(["Familie", "Umgebungen", "Matrix heute", "Stufe 1", "Pfadtabelle", "Netto Stufe 1 + 1b"], [
 ("ESP32 klassisch", "9", f"{dense(13)} B, 13 Zeilen", f"{cl['s1']} B, 64 Zeilen, 256 Kanten", f"{cl['path']} B, 40 Eintraege", f"{cl['s1']-cl['dense']-cl['path']:+d} B"),
 ("ESP32 klassisch, XML", "1", f"{dense(21)} B, 21 Zeilen", f"{sz['xml']['s1']} B, 64 Zeilen, 256 Kanten", f"{sz['xml']['path']} B, 50 Eintraege", f"{sz['xml']['s1']-sz['xml']['dense']-sz['xml']['path']:+d} B"),
 ("ESP32-S3", "17", f"{dense(21)} B, 21 Zeilen", f"{s3['s1']} B, 64 Zeilen, 384 Kanten", f"{s3['path']} B, 100 Eintraege, bleibt", f"{s3['s1']-s3['dense']:+d} B"),
 ("nRF52840", "3", f"{dense(21)} B, 21 Zeilen", f"{sz['nrf']['s1']} B, 64 Zeilen, 384 Kanten", f"{sz['nrf']['path']} B, 100 Eintraege, bleibt", f"{sz['nrf']['s1']-sz['nrf']['dense']:+d} B"),
 ("Safeboot", "2", "keine Matrix", "-", "-", "0"),
])}
<p>Die Praemisse fuer 64 Zeilen ist die Flottenmessung: 41 direkte Nachbarn in 24 h als Maximum und 48 als p90 der 2-Hop-Nachbarschaft passen hinein, 84 (p99) nicht. Die Praemisse fuer 64-Bit-Masken ist dieselbe Zahl: 32 Bit reichen nicht mehr, 128 Bit kosten je Zeile 16 B mehr und sind Stufe 2. Der klassische ESP32 bekommt dieselbe Zeilenzahl wie S3, weil die Flotte ihn genau dort einsetzt, wo die Matrix voll laeuft, und die Rechnung auf Masken ihn nicht mehr kostet als den S3.</p>

<h3 id="s46">4.6 Pfadtabelle</h3>
<p class="reco">Entscheidung: Compile-Flag <code>MC_PATHINFO</code>, Default 1 fuer Upstream-Paritaet; im Fork setzen die neun klassischen ESP32-Umgebungen 0, sobald Stufe 1 gebaut ist.</p>
<p>Die Pfadtabelle haelt je Absender den zuletzt gesehenen kuerzesten Pfad als Text, die Hop-Zahl und ein G-Bit fuer "ueber ein Gateway gekommen", 12 h lang, mit 71 Byte je Eintrag. Sie speist die Web-Seite Path Information, das Kommando <code>--path</code>, die Pfadseite am T-Deck-Display und auf T-Deck die Sicherung nach SD. Kein BLE-Client und keine Entscheidung liest sie. Die Matrix ersetzt sie nicht eins zu eins: das 2-Hop-Fenster verwirft Pfade ab drei Hops mit Absicht, die Pfadtabelle zeigt sie, etwa <code>3G/DB0HOB-12,DB0ED-99</code> fuer DL1MSE-12. Fuer alles innerhalb von zwei Hops zeigt die Matrix mehr: wer wen hoert, mit Zaehler und SNR, statt eines einzelnen Pfads.</p>
{tbl(["Option", "Mechanik", "RAM klassisch", "Bewertung"], [
 ("Behalten wie heute", "MAX_MHPATH 40/100", "0 B frei", '<span class="tag warn">Stufe 0</span> upstream-identisch, zahlt den Pool nicht'),
 ("Verkleinern", "MAX_MHPATH 40 auf 20, 100 auf 50", "1420 B frei", '<span class="tag warn">Teilmenge</span> fuer S3 und nRF52 sinnvoll, dort ist die Tabelle mit 7,1 kB groesser als die Matrix'),
 ("Compile-Flag, Default an", "MC_PATHINFO schaltet Puffer, Seite, Kommando und T-Deck-Seite gemeinsam", "2840 B frei bei 0", '<span class="tag ok">gewaehlt</span> Upstream sieht keine Aenderung, der Fork entscheidet je Umgebung'),
 ("Entfernen", "Code und Puffer weg", "2840 B frei", '<span class="tag bad">verworfen</span> aendert Upstream-Verhalten und die 3-Hop-Sicht'),
])}
<p>Das Flag liegt an vier Stellen: den vier Puffern in <code>mheard_functions.cpp</code>, <code>updateHeyPath()</code>, <code>sub_page_path()</code> mit dem Menuepunkt, sowie <code>showPath()</code>, <code>showPathTDECK()</code> und der SD-Sicherung. T-Deck-Umgebungen sind S3 und behalten das Flag auf 1. Der String-Scan des Images nach dem Bau prueft, dass "Path Information" bei 0 verschwunden ist, so wie es die Instrument-Regel fuer jede Guard-Aenderung verlangt.</p>

<h2 id="s5">5. Fehlerszenarien</h2>
{tbl(["Szenario", "Erkennung", "Reaktion", "Luecke", "ab Stufe"], [
 ("Pool voll am Super-Node", "Zaehler EVICT-E im [NBR]-Log, --info", "aelteste Kante ohne Bezug zu Zeile 0 weicht; Direkt und HoertMich bleiben", "Rang gekappt, Entscheidung vollstaendig", "1"),
 ("Zeile verdraengt, Kanten bleiben liegen", "Host-Test: nach Verdraengung keine Kante mit dem alten Index", "Verdraengung loescht Kanten und Bits in einem Durchlauf", "keine", "1"),
 ("16-Bit-Minute laeuft ueber (45 Tage)", "Geist-Sweep wie heute", "Sweep je Minute entfernt Kanten mit Alter ueber dem Fenster, bevor sie 32768 erreichen", "keine", "1"),
 ("Zeilenindex ab 64", "static_assert auf NBR_MAX_ROWS &lt;= 64", "Compile-Fehler, kein stiller Bitverlust wie heute bei nbrBit() ab 32", "keine", "1"),
 ("Symmetrie ohne SNR, weil die Gegenkante verdraengt wurde", "SYM-Zeile fehlt", "keine Annahme, Fall A, sichere Richtung", "mehr Fluten", "1"),
 ("Server-Frame ueber ein Gateway erzeugt falsche Direktheit", "ME-Zeile fehlt fuer die Zeile", "Text speist nur den ME-Schritt, Spalte 0 nur per Empfang", "POS/HEY vom Server bleiben ein Restrisiko", "0"),
 ("Fenster 1 h statt 12 h", "Flottenmessung", "12 h bleibt, 24 h als Kommandowert pruefen", "Blaetter, die nur nachts exklusiv sind", "offen"),
])}

<h2 id="s6">6. Rollout</h2>
{tbl(["Schritt", "Stufe", "Inhalt", "Abnahmekriterium"], [
 ("1", "1", "Pool, Masken, Sweep in nbr_matrix.{{h,cpp}} hinter derselben API; Zugriffe m.cells[x][y] auf nbrEdgeFind() umgestellt", "Host-Tests native_nbr_matrix, native_nbr_report, test_txring gruen; Replay des Mitschnitts vom 24.09.: SNAP, ROW, NEED, CANCEL, REFUSE byte-identisch zum dichten Build"),
 ("2", "1", "ringNeed/ringAlone auf uint64_t, nbrBit() 64 Bit, NBR_MAX_ROWS 64 je Familie", "tools/resource_watch.py: kein ESP32-Segment unter 20 kB frei, nRF52 unter 60 %"),
 ("3", "1b", "MC_PATHINFO in den neun klassischen Umgebungen auf 0", "String-Scan: 'Path Information' fehlt im Image; --path meldet 'nicht gebaut'"),
 ("4", "1", "OTA auf DK5EN-98 und DK5EN-93, 24 h mit --nbrrelay on", "tools/nbrhopcheck.py gruen, Relays je Stunde und CANCEL-Quote im Korridor vom 24.09., 0 RING_DROP"),
 ("5", "1", "Bench T-Beam DK5EN-92 als klassischer ESP32", "freier Heap nach 1 h mindestens wie vor dem Flash, Toleranz 1 kB"),
 ("6", "2", "128-Bit-Masken auf S3 und nRF52, nur nach Feldnachweis", "Rang an einem Super-Node mit 64 gegen 128 Zeilen verglichen"),
])}

<h2 id="s7">7. Offene Punkte</h2>
{tbl(["Punkt", "Warum es zaehlt", "Klaerungsweg"], [
 ("Kantenzahl am Super-Node", "256 Kanten sind aus 3,2 je Zeile und einer Annahme zu HN-Berichten abgeleitet", "einen Super-Node mit der neuen Firmware laufen lassen, EVICT-E zaehlen"),
 ("Freier Heap auf klassischem ESP32 im Betrieb", "statisches Segment hat Reserve, der Laufzeit-Heap mit WiFi und BLE ist nicht gemessen", "--info auf DK5EN-92 vor und nach Stufe 1"),
 ("Fenster 24 h", "Flottenmessung empfiehlt 12 bis 24 h; 12 h ist heute", "NBR_WINDOW_MIN als Kommandowert, Feldvergleich"),
 ("Entfernungsfilter fuer #X an Gateway-Nachbarn", "DL2JA-2 zaehlte 29 exklusive, 12 davon in Reichweite", "lat16/lon16 der Zeile, Filter nur in der Anzeige"),
])}
""")
    H.append(appendices(ramrows))
    H.append("</main></body></html>")
    html = "".join(H)
    OUT.write_text(html)
    print(f"written {OUT} ({len(html)} bytes), fresh envs: {n_fresh}")

if __name__ == "__main__":
    build()
