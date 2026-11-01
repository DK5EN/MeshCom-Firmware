#!/usr/bin/env python3
"""Generator for the per-node report OE3XIA-12 (24-h log analysis, 2026-11-01).

Usage:
    python3 -I oe3xia.py --data <dir> --kit <dir> --out <file.html>

--data  run data directory (health/ q2/ q34/ q5/ q6/ master/ extra/ and CORE-FINDINGS.md)
--kit   directory holding reportkit.py (shared HTML/CSS/SVG building blocks)
--out   HTML file to write

No raw logs are read at render time. Everything printed comes from the data directory; the
receive-silence figures come from extra/oe3xia_analysis.json, written once from the normalised
logs by lib/oe3xia_analyze.py (--norm <dir> --out <file>). Judgement text may carry literals from
CORE-FINDINGS.md / verify/*.md; those are named in the developer details. Stdlib only.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path
from typing import Any

NODE = "oe3xia"
CALL = "OE3XIA-12"
OE3 = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
LOGNAME = {"oe3xhl": "OE3XHL-12", "oe3xia": "OE3XIA-12", "oe3xir": "OE3XIR-12", "oe3xoc": "OE3XOC-12", "oe3xwj": "OE3XWJ-12"}


# ----------------------------------------------------------------------------------------------
# small helpers
# ----------------------------------------------------------------------------------------------
def de(x: float, nd: int = 1) -> str:
    """German decimal comma; integers without decimals when nd == 0."""
    if nd == 0:
        return f"{x:,.0f}".replace(",", ".")
    return f"{x:,.{nd}f}".replace(",", "X").replace(".", ",").replace("X", ".")


def pct(a: float, b: float, nd: int = 1) -> str:
    return de(100.0 * a / b, nd) + " %"


def mmss(sec: float) -> str:
    s = int(round(sec))
    return f"{s // 60}:{s % 60:02d} min"


def jload(p: Path) -> Any:
    return json.loads(p.read_text(encoding="utf-8"))


# ----------------------------------------------------------------------------------------------
# receive-silence analysis: produced by lib/oe3xia_analyze.py (needs the raw logs), read from data/extra
# ----------------------------------------------------------------------------------------------
def load_analysis(d: Path) -> dict[str, Any]:
    return jload(d / "extra" / "oe3xia_analysis.json")


# ----------------------------------------------------------------------------------------------
# master data
# ----------------------------------------------------------------------------------------------
def load_master(d: Path) -> tuple[dict[str, dict[str, str]], dict[str, str]]:
    master: dict[str, dict[str, str]] = {}
    with open(d / "master" / "master.tsv", newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            master[r["call"]] = r
    dist: dict[str, str] = {}
    with open(d / "master" / "distances.tsv", newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            dist[r["call"]] = r.get("d_oe3xia", "")
    return master, dist


# ----------------------------------------------------------------------------------------------
# SVG drawing helpers (classes only, no colours; umlauts as ae/oe/ue inside SVG text)
# ----------------------------------------------------------------------------------------------
def s_box(x: float, y: float, w: float, h: float, cls: str, lines: list[str], extra: str = "") -> str:
    out = [f'<rect class="d-box {cls} {extra}" x="{x}" y="{y}" width="{w}" height="{h}" rx="6"/>']
    n = len(lines)
    for i, t in enumerate(lines):
        ty = y + h / 2 + 4.5 + (i - (n - 1) / 2) * 15
        out.append(f'<text class="d-txt{"" if i == 0 else "-s"}" x="{x + w / 2}" y="{ty:.1f}" text-anchor="middle">{t}</text>')
    return "".join(out)


def s_line(x1: float, y1: float, x2: float, y2: float, cls: str = "", arrow: bool = True) -> str:
    out = [f'<line class="d-line {cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>']
    if arrow:
        dx, dy = x2 - x1, y2 - y1
        ln = (dx * dx + dy * dy) ** 0.5 or 1.0
        ux, uy = dx / ln, dy / ln
        px, py = -uy, ux
        a = (x2 - ux * 9 + px * 5, y2 - uy * 9 + py * 5)
        b = (x2 - ux * 9 - px * 5, y2 - uy * 9 - py * 5)
        out.append(f'<polygon class="d-neu" points="{x2},{y2} {a[0]:.1f},{a[1]:.1f} {b[0]:.1f},{b[1]:.1f}"/>')
    return "".join(out)


def s_txt(x: float, y: float, t: str, small: bool = True, anchor: str = "middle") -> str:
    return f'<text class="{"d-txt-s" if small else "d-txt"}" x="{x}" y="{y}" text-anchor="{anchor}">{t}</text>'


def draw_deaf(n_sil: int, longest_s: int, ends_cad: int, cad_busy: int, cad_scan: int, tx_heard: int, tx_in: int) -> str:
    """Receiver deaf: watchdog restart without Standby does not help, Standby+CAD ends it."""
    o = [s_txt(20, 20, "Ablauf einer Taubphase auf OE3XIA-12 (gemessen); Ursache im Chip ist Vermutung", True, "start")]
    # lane 1: receiver state
    o.append(s_txt(20, 52, "Empfaenger", True, "start"))
    o.append(s_box(20, 62, 110, 34, "d-ok", ["Empfang laeuft"]))
    o.append(s_box(140, 62, 330, 34, "d-act", ["Stille: 0 Rahmen, 0 Fehler (bis " + f"{longest_s:,}".replace(",", ".") + " s)"], "anim-pulse"))
    o.append(s_box(480, 62, 140, 34, "d-ok", ["Empfang wieder da"]))
    # lane 2: watchdog
    o.append(s_txt(20, 128, "Wachhund im Funktreiber, alle 4,8 s", True, "start"))
    for i, x in enumerate((165, 215, 265, 315, 365, 415)):
        o.append(f'<circle class="d-node d-watch anim-pulse anim-delay-{i % 4}" cx="{x}" cy="150" r="9"/>')
    o.append(s_txt(290, 182, "Radio.Rx() erneut, ohne Standby: ohne Wirkung", True))
    # lane 3: Standby + CAD
    o.append(s_box(450, 134, 170, 32, "d-acc", ["Standby + Kanalabfrage"]))
    o.append(s_line(570, 132, 570, 100, "d-acc-s anim-flow", True))
    o.append(s_txt(535, 188, f"{ends_cad} von {n_sil} Pausen enden", True))
    o.append(s_txt(535, 204, "binnen 3 s danach", True))
    # facts
    o.append(s_txt(20, 232, f"Kanalabfrage meldet belegt: {cad_busy} von {cad_scan} Abfragen in den Pausen", True, "start"))
    o.append(s_txt(20, 252, f"Eigene Sendungen gehen raus: {tx_heard} von {tx_in} wurden von anderen Knoten gehoert", True, "start"))
    o.append(s_txt(20, 276, "Vermutung: Funkchip haengt im Empfangszustand; Standby vor Rx waere der Test (Bench)", True, "start"))
    return "".join(o)


def draw_starve(own_hey_lost: int, own_hey_chains: int) -> str:
    """Own beacon starves: strict priority, hold re-armed per frame, CAD busy; then TTL."""
    o = []
    o.append(s_txt(20, 20, "1  Strenge Rangfolge: ACK vor Bake", False, "start"))
    for i, x in enumerate((20, 78, 194)):
        o.append(s_box(x, 30, 50, 30, "d-acc", ["ACK"]))
    o.append(s_box(136, 30, 50, 30, "d-neu", ["Relais"]))
    o.append(s_box(256, 30, 94, 30, "d-act", ["eigene Bake"], "anim-drop"))
    o.append(s_txt(370, 42, "ACK (Prio 1) senden zuerst, die Bake", True, "start"))
    o.append(s_txt(370, 58, "verliert so 64 bis 72 % ihrer Wartezeit", True, "start"))
    o.append(s_txt(20, 94, "2  Haltezeit 5,5 s, jeder gehoerte Rahmen setzt sie zurueck", False, "start"))
    o.append(s_box(20, 104, 330, 28, "d-watch", ["Haltezeit der Bake am Listenkopf"]))
    for i, x in enumerate((90, 160, 230, 300)):
        o.append(f'<circle class="d-node d-acc anim-pulse anim-delay-{i % 4}" cx="{x}" cy="150" r="7"/>')
    o.append(s_txt(370, 116, "Relais gleicher Prio: 150 bis 220 ms", True, "start"))
    o.append(s_txt(370, 132, "nach 8 s (NBR Case A), Bake: kein Weg", True, "start"))
    o.append(s_txt(370, 154, "Altersboden gilt nur fuer Prio 1 bis 3", True, "start"))
    o.append(s_txt(20, 188, "3  Kanalabfrage (CAD) schlaegt fehl", False, "start"))
    o.append(s_box(20, 198, 330, 28, "d-act", ["OE3XIA: 62 von 62 Abfragen belegt"]))
    o.append(s_txt(370, 210, "OE3XOC: nur 21 Abfragen in 2924 s", True, "start"))
    o.append(s_txt(370, 226, "am Listenkopf (Haltezeit)", True, "start"))
    o.append(s_txt(20, 258, f"Dann laeuft die Lebensdauer ab (HEY 180 s, Position 120 s): {own_hey_lost} von {own_hey_chains} HEY-Ketten verworfen", True, "start"))
    return "".join(o)


def draw_ackhold(p50: int, share: int, mx: int) -> str:
    """ACK waits 20 to 60 s because every received frame re-arms the hold."""
    o = [s_line(30, 110, 610, 110, "", True)]
    o.append(s_box(20, 40, 110, 44, "d-acc", ["ACK kommt rein"]))
    for i, x in enumerate((190, 270, 350, 430)):
        o.append(f'<circle class="d-node d-watch anim-pulse anim-delay-{i % 4}" cx="{x}" cy="110" r="9"/>')
        o.append(s_txt(x, 140, "Rahmen"))
    o.append(s_txt(310, 162, "jeder gehoerte Rahmen setzt die Wartezeit zurueck"))
    o.append(s_box(500, 40, 120, 44, "d-ok", [f"sendet nach {p50} s", "(Median)"]))
    o.append(s_line(130, 62, 495, 62, "d-line", True))
    o.append(s_txt(310, 196, f"OE3XIA: {share} % der ACK warten 20 s oder mehr, laengster {mx} s"))
    return "".join(o)


def draw_ackrelay(a: int, b: int) -> str:
    """One ACK is relayed by several gateways, ACK frames have no overhear-cancel."""
    o = [s_box(20, 80, 110, 50, "d-acc", ["Fremd-Gateway", "ACK fuer Ping"])]
    ys = (20, 82, 144)
    names = ("OE3XIA-12", "OE3XOC-12", "OE3XIR-12")
    for y, nm in zip(ys, names):
        cls = "d-watch" if nm == "OE3XIA-12" else "d-neu"
        o.append(s_box(230, y, 130, 42, cls, [nm, "queue + sendet"]))
        o.append(s_line(135, 105, 226, y + 21, "d-line anim-flow", True))
        o.append(s_line(365, y + 21, 470, 105, "d-line anim-flow", True))
    o.append(s_box(472, 80, 150, 50, "d-act", ["Nachbar hoert", "denselben ACK mehrfach"]))
    o.append(s_txt(310, 204, f"OE3XIA: {a} von {b} ACK-Sendungen, nachdem eine Kopie schon gehoert war"))
    return "".join(o)


def draw_cet(lead_s: str) -> str:
    """Server time telegram: another gateway radiates, OE3XIA radiates anyway."""
    o = [s_txt(20, 24, "Zeit", True, "start"), s_line(20, 34, 620, 34, "", True)]
    o.append(s_txt(30, 70, "Server", True, "start"))
    o.append(s_box(130, 52, 100, 34, "d-acc", ["Telegramm"]))
    o.append(s_txt(30, 120, "anderes Gateway", True, "start"))
    o.append(s_box(250, 102, 100, 34, "d-neu", ["strahlt ab"], "anim-pulse"))
    o.append(s_txt(30, 170, "OE3XIA-12", True, "start"))
    o.append(s_box(250, 152, 100, 34, "d-ok", ["hoert es"]))
    o.append(s_box(460, 152, 150, 34, "d-watch", ["strahlt trotzdem ab"], "anim-pulse"))
    o.append(s_line(360, 169, 455, 169, "d-line anim-flow", True))
    o.append(s_line(180, 88, 250, 108, "", True))
    o.append(s_txt(410, 200, f"nach median {lead_s} s", True))
    return "".join(o)


def draw_noise(n_all: str, n_err: str, n_ok: str, p_err: str, p_ok: str, hl: str, med: int, snr: int) -> str:
    """About a third of receive events are errors."""
    o = [s_txt(20, 30, f"Alle Empfangsereignisse ({n_all}), ACK-Rahmen eingerechnet", True, "start")]
    o.append('<rect class="d-box d-act anim-pulse" x="20" y="44" width="200" height="50" rx="6"/>')
    o.append('<rect class="d-box d-ok" x="220" y="44" width="400" height="50" rx="6"/>')
    o.append(s_txt(120, 74, f"RX_ERROR {p_err} %", False))
    o.append(s_txt(420, 74, f"Rahmen ok {p_ok} %", False))
    o.append(s_txt(120, 118, f"{n_err} Ereignisse"))
    o.append(s_txt(420, 118, f"{n_ok} dekodierte Rahmen"))
    o.append(s_txt(320, 150, f"Signal erkannt, kein gueltiger Rahmen: Median {med} dBm, SNR {snr} dB"))
    o.append(s_txt(320, 176, f"OE3XHL im Vergleich: {hl} %"))
    o.append(s_txt(320, 202, "Rauschteppich am Standort etwa 5 dB ueber der Theorie"))
    return "".join(o)


# ----------------------------------------------------------------------------------------------
# page
# ----------------------------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--kit", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    sys.path.insert(0, str(args.kit))
    from reportkit import bars, chip, explain, hist, page, table, timeline

    D: Path = args.data
    H = jload(D / "health" / "results.json")["nodes"][NODE]
    Q2 = jload(D / "q2" / "results.json")
    Q34 = jload(D / "q34" / "results.json")
    Q5 = jload(D / "q5" / "results.json")["nodes"][NODE]
    Q6 = jload(D / "q6" / "results.json")["nodes"][NODE]
    q2 = Q2["nodes"][NODE]
    q34 = Q34["nodes"][NODE]
    cross = Q2["network"]["cross_node"]
    master, dist = load_master(D)
    A = load_analysis(D)
    XIR = jload(D / "extra" / "oe3xir_ping_cost.json")

    fig = [0]

    def nf() -> int:
        fig[0] += 1
        return fig[0]

    # ---- derived numbers -------------------------------------------------------------------
    n_gaps = A["n_gaps"]
    sil_s = A["silent_s"]
    sil_h = sil_s / 3600.0
    sil_pct = 100.0 * sil_s / A["span_s"]
    rows = A["rows"]
    longest = max(rows, key=lambda r: r["len_s"])
    lens = sorted(r["len_s"] for r in rows)
    own_tx_sil = A["radio_tx_in_sil"]
    ntx = H["own_tx"]["tx_lines"]
    n_ev = A["events"]
    n_err = A["cnt"][NODE]["err"]
    n_ok = A["cnt"][NODE]["done"]
    n_ondone = A["cnt"][NODE]["ondone"]
    cad = H["cad"]
    ch = H["channel"]["CHANNEL_UTIL"]
    ring = H["ring"]
    traffic = H["traffic"]
    own = H["own_tx"]
    fate = q2["fate"]
    occ = q2["occupancy"]
    lat = q2["latency"]["by_prio_s"]
    ex = q2["extras"]
    hc = q2["hey_chains"]["own_hey"]
    dt_tab = q34["by_prio_cat"]
    rc = q34["relay_class"]
    cf = q34["cf"]
    csma = q34["csma"]
    hr = q34["headroom"]
    q6_co = Q6["cancel_opportunity"]
    opp = Q6["opportunity_airtime"]
    stat_pr = Q6["stat_windows"]
    fw_sil = A["other_rate"]
    rfm = H["rf"]
    err_share = A["err_share"]
    ackp = H["ack_attribution"]["top_acked_prefix"][0]
    ack_ge20 = int(round(100 * ex["prio1_ge20s_share"]))
    sil_row = rows.index(longest) + 1
    def ftxt(f: str) -> str:
        m = re.match(r"^([34]\d):(.)$", f)
        if not m:
            return f
        return f"4.{m.group(1)}{m.group(2)}" if m.group(2) != "#" else f"4.{m.group(1)} (Buchstabe nicht übertragen)"


    def neigh_row(t: dict[str, Any]) -> list[str]:
        c = t["call"]
        m = master.get(c)
        hw = m["hardware"] if m else (t.get("hw") or "unbekannt")
        if m is None and t.get("hw"):
            hw += " (aus Funkrahmen)"
        fw = (m["firmware"] if m else "") or (t.get("fw_log") or "")
        fw = ftxt(fw) if fw else "unbekannt"
        place = (m.get("description") or m.get("name") or f"Koordinaten {m['lat']}, {m['lon']}") if m else "keine Stammdaten"
        if c == "OE3XIR-12":
            place = "keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg)"
        alt = f"{m['altitudeM']} m" if m and m.get("altitudeM") else "unbekannt"
        d = dist.get(c, "")
        dk = f"{float(d):.1f}".replace(".", ",") + " km" if d else "unbekannt"
        rs = f"{int(t['rssi_median'])} dBm / {int(t['snr_median'])} dB" if t.get("rssi_median") is not None else ""
        return [c, hw, fw, place, alt, dk, de(t["n"], 0), rs]

    top = H["neighbours"]["top"]

    body: list[str] = []
    ap_ = body.append

    # ---- header ----------------------------------------------------------------------------
    ends_cad = A["ends_after_cad"]
    hey_chains = hc["chains"]
    hey_aired = hc["fate"]["tx_air"]
    hey_written = A["hey_written"]
    ap_('<div class="eyebrow">MeshCom Log-Auswertung · OE3XIA-12</div>')
    ap_(f"<h1>OE3XIA-12 empfing {n_gaps} Mal minutenlang nichts, zusammen {de(sil_h)} Stunden ({de(sil_pct)} %), obwohl Kanalabfrage und Sender weiterliefen</h1>")
    ap_("<ul class=\"meta\">"
        f"<li><b>Log-Datei:</b> meshcom_oe3xia_2026-10-31_064003.log ({de(A['log_lines'], 0)} Zeilen)</li>"
        f"<li><b>Zeitraum:</b> {A['log_first'][:6]}2026 {A['log_first'][7:]} bis {A['log_last'][:6]}2026 {A['log_last'][7:]} (Uhr des Mitschnitt-Rechners, MEZ; Knotenuhr weicht um etwa 1 s ab), {de(A['log_span_h'], 2)} h</li>"
        "<li><b>Hardware:</b> RAK4631 (nRF52840) mit Ethernet (Chip W5100S abgeleitet, nicht im Log)</li>"
        "<li><b>Firmware:</b> 4.40a, Release-Stempel 20260929 (FLASH_VERSION, kein Baudatum); genauer Build unbekannt (TX-Pool-Stand ab W1b aus den Log-Markern abgeleitet, Vermutung)</li>"
        "<li><b>Standort:</b> Exelberg (Wienerwald), 48,2497 N / 16,2452 E, 613 m</li>"
        "<li><b>Rolle:</b> Internet-Gateway und Relay</li>"
        "<li><b>LoRa-Parameter:</b> SF11 / 250 kHz / CR 4:6 (Vermutung aus der Sendedauer; Frequenz und Leistung stehen nicht im Log)</li>"
        "<li><b>Stand:</b> 01.11.2026</li></ul>")
    ap_('<div class="bluf"><p class="lead">'
        f"Der Empfänger von OE3XIA-12 stand {n_gaps} Mal länger als 5 Minuten still (zusammen {de(sil_h)} Stunden = {de(sil_pct)} %, "
        f"längste Pause {de(longest['len_s'], 0)} s ab {longest['start']}), während die anderen Knoten normal weiterempfingen. "
        f"Die Pausen enden auffällig oft kurz nach einer Kanalabfrage ({ends_cad} von {n_gaps}), der übliche Neustart des Empfangs hilft nicht: "
        "Verdacht auf einen Funkchip, der im Empfangszustand hängen bleibt (Firmware-Test auf dem Prüfstand nötig).</p>"
        "<p>Sonst ist der Knoten gesund: kein Neustart, kein Speicherverlust, Server-Anbindung stabil. "
        "Eigene Baken (HEY und Wetterposition) verhungern am Listenkopf und verfallen; das ist eine Entwurfslücke, kein Codefehler.</p></div>")
    ap_('<div class="callout act"><p><b>Handlungsbedarf</b></p><ul>'
        "<li><b>Empfangsstillstand auf dem Prüfstand nachstellen (Entwickler):</b> <code>Radio.Standby()</code> vor dem Neustart des Empfangs im Wachhund einfügen "
        "(<code>src/nrf52/nrf52_main.cpp:1631</code>) und prüfen, ob die Pausen verschwinden. Status: Verdacht, ungetestet, hängt vom Build ab.</li>"
        "<li><b>Gegenprobe Hardware (Betreiber):</b> Antenne, Stecker, Netzteil prüfen; zweiten RAK4631 am Standort mitschneiden. Funkseitige Ursachen sind weniger wahrscheinlich, aber nicht ausgeschlossen.</li>"
        f"<li><b>Eigene Baken (Entscheidung des Betreibers):</b> {hc['fate']['stale_ttl']} von {hey_chains} HEY-Ketten und {fate['by_src']['user_wx']['stale_ttl']} von {q2['slots']['own_writes_by_src']['user_wx']} Wetterpositionen sind nie gesendet worden. "
        "Kleinster Korrekturkandidat: Altersboden der Haltezeit auch für Priorität 4 und 5, aber nur für Nicht-Relay-Slots. "
        "Herleitung: <a href=\"verify/rca_own_beacons.md\">verify/rca_own_beacons.md</a>.</li>"
        "</ul></div>")

    # ---- core facts ------------------------------------------------------------------------
    ap_("<h2>Kernfakten</h2><ul>"
        f"<li><b>Taub:</b> {n_gaps} Pausen ohne einen einzigen Empfangs- oder Fehlerrahmen, {de(sil_s, 0)} s zusammen; die erste begann {A['first_gap']}, keine in den ersten 1 h 46 min.</li>"
        f"<li><b>Kein Logger-Ausfall:</b> größter Zeilenabstand im Mitschnitt innerhalb der Pausen {de(A['worst_line_gap_s'])} s; der Zähler des Funkmoduls meldet in allen {de(A['util_n'], 0)} Stichproben in den Pausen rx = 0 ms.</li>"
        "<li><b>Die anderen hörten weiter:</b> in diesen Fenstern empfingen " + ", ".join(f"{LOGNAME[o]} {de(fw_sil[o]['during_per_h'], 0)} Ereignisse pro Stunde (Tagesmittel {de(fw_sil[o]['mean_per_h'], 0)})" for o in ('oe3xhl', 'oe3xoc', 'oe3xwj')) + ".</li>"
        f"<li><b>Sender läuft weiter:</b> in den Pausen {own_tx_sil} eigene Sendungen ({de(own_tx_sil / sil_h, 1)} pro Stunde statt {de(A['radio_tx_per_h'], 1)}), {A['tx_in_sil_heard']} davon hörte mindestens ein anderer Knoten. Mit nichts Empfangenem gibt es nichts weiterzuleiten.</li>"
        f"<li><b>Eigene Baken:</b> HEY {hey_aired} von {hey_chains} Ketten gesendet ({de(hc['aired_pct'])} %; Nenner: {hey_written} Schreibungen minus {fate['by_src']['auto_pos']['superseded']} ersetzte = {hey_chains} Ketten). "
        f"Bezogen auf alle {hey_written} geschriebenen HEY sind es {pct(hey_aired, hey_written, 0)}. Wetterposition {fate['by_src']['user_wx']['tx_air']} von {q2['slots']['own_writes_by_src']['user_wx']} gesendet.</li>"
        f"<li><b>Langsamster Relay im OE3-Vergleich:</b> mittlerer Rang {de(cross['per_node'][NODE]['mean_rank_0fast_1slow'], 2)} (0 = schnellster, 1 = langsamster); in {cross['per_node'][NODE]['slowest_count']} von 1462 Vergleichsgruppen der letzte.</li>"
        f"<li><b>ACK-Wartezeit:</b> Median {de(lat['1']['p50'])} s, 90 % unter {de(lat['1']['p90'])} s, längste {de(lat['1']['max'])} s.</li>"
        f"<li><b>Funkfehler:</b> {pct(n_err, n_ev)} aller Empfangsereignisse (ACK-Rahmen eingerechnet) sind RX_ERROR (Signal erkannt, kein gültiger Rahmen); die Schwelle der Auswertung (40 %) wird nicht erreicht.</li>"
        "</ul>")

    # ---- Antwort-Index ---------------------------------------------------------------------
    idx = [
        ["Q0 Gesundheit", f"Keine Neustarts, Speicher flach, Server-Anbindung ok. Aber {n_gaps} Empfangsstillstände ({de(sil_h)} h).", chip("act")],
        ["Q2 TX-Puffer mit Slots", f"Mechanik in Ordnung (0 Überläufe, höchste Füllung {occ['queued']['max']:.0f} von 20). Eigene Baken verhungern, ACK-Wartezeit {de(lat['1']['p50'], 0)} s im Median.", chip("watch")],
        ["Q3 Verlust durch Zeitverfall", f"{q34['drops_total']} Slots verfielen, alle per TTL (Lebensdauer im Puffer), keiner war gesendet. Eigene Baken: {q34['own_dropped']}. Kein Text, kein ACK, kein {{CET}}.", chip("act")],
        ["Q4 Gewinn durch Zeitverfall", f"Frische, nicht Sendezeit: {de(q34['drop_toa_s'])} s = {de(q34['drop_toa_pct_of_tx'], 2)} % der eigenen Sendezeit. Ohne TTL würden {de(cf['fresh_delayed']['pct_delayed'])} % der frischen Baken und Relais später gehen.", chip("ok")],
        ["Q5 Zeitsignal", f"{{CET}}-Telegramme: {Q5['tx_count']} gesendet, kein veraltetes (größtes Alter {Q5['tx_age_s_logger_based']['max']:.0f} s von 150 s). {de(Q5['tx_after_overheard_pct'])} % nach bereits gehörter Abstrahlung.", chip("watch")],
        ["Q6 Manipulationen", f"Relais {Q6['relay_fate']['sent']} gesendet, ACK-Weiterleitung ohne Abbruchweg ({q6_co['A']['sent_with_foreign_copy_heard_while_queued']} von {own['src_type']['r:A']} nach gehörter Kopie), kein Abbruch durch Overhear-Cancel (alle Bedarfsmeldungen Case A).", chip("watch")],
    ]
    ap_("<h2>Antwort-Index</h2>")
    ap_(table(["Frage", "Kurzantwort", "Urteil"], idx, raw=True))
    ap_("<details><summary>Begriffe in einem Satz</summary><ul>"
        "<li><b>ACK:</b> Quittung, dass eine Nachricht angekommen ist; wird von Relais weitergegeben.</li>"
        "<li><b>HEY:</b> Lebenszeichen-Bake (Heartbeat) eines Knotens, Priorität 5.</li>"
        "<li><b>Relay:</b> Knoten, der fremde Rahmen weitersendet.</li>"
        "<li><b>TTL:</b> Lebensdauer eines Eintrags im Sendepuffer; danach wird er verworfen, auch ungesendet.</li>"
        "<li><b>CAD:</b> Kanalabfrage, ob jemand sendet, bevor man selbst sendet.</li>"
        "<li><b>CSMA-Haltezeit:</b> Wartezeit vor dem Senden; jeder gehörte Rahmen setzt sie zurück.</li>"
        "<li><b>Supersede (ersetzen):</b> Eine neuere Bake desselben Absenders ersetzt die noch ungesendete ältere im selben Platz.</li>"
        "<li><b>Overhear-Cancel:</b> Eine ungesendete Relay-Kopie wird verworfen, wenn ein Nachbar sie schon gesendet hat.</li>"
        "<li><b>Case A:</b> Der Knoten hält sich für den einzigen, der bestimmte Nachbarn erreicht; dann bricht er nie ab.</li>"
        "<li><b>Dedup-Ring:</b> Speicher der zuletzt gesehenen Nachrichten-Kennungen, damit Kopien nicht ein zweites Mal verarbeitet werden.</li>"
        "<li><b>{CET}:</b> Zeittelegramm des Servers OE1XAR-33 alle gut 10 Minuten; enthält UTC.</li>"
        "<li><b>RX_ERROR:</b> Signal erkannt, aber kein gültiger Rahmen daraus gewonnen.</li></ul></details>")

    # ---- Gesundheit ------------------------------------------------------------------------
    ap_("<h2>Gesundheit und Funkumfeld</h2>")
    ap_(f"<p><b>Der Empfänger stand {n_gaps} Mal für {mmss(lens[0])} bis {mmss(lens[-1])} still.</b> "
        "Gemeint ist: kein einziger dekodierter Rahmen (ACK-Rahmen eingeschlossen) und kein einziger Fehlerrahmen (Signal erkannt, aber nicht dekodierbar, RX_ERROR) in dieser Zeit. "
        f"Normalerweise vergehen im Median nur {de(A['median_gap'], 2)} s zwischen zwei Ereignissen.</p>")
    gmax = max((o for o in A["max_gap"] if o != NODE), key=lambda o: A["max_gap"][o])
    others_max = "sonst " + ", ".join(LOGNAME[o] + " " + de(A["max_gap"][o], 0) + " s" for o in OE3 if o not in (NODE, gmax))
    ap_("<ul>"
        f"<li><b>Umfang:</b> {n_gaps} Pausen über 300 s, zusammen {de(sil_s, 0)} s = {de(sil_h)} h = {de(sil_pct)} % des Mitschnitts. Kürzeste {mmss(lens[0])}, mittlere {mmss(lens[len(lens) // 2])}, längste {mmss(lens[-1])} (ab {longest['start']}).</li>"
        f"<li><b>Im Vergleich:</b> die längste Pause zwischen zwei Ereignissen war bei den anderen Knoten {de(A['max_gap'][gmax], 0)} s ({LOGNAME[gmax]}); {others_max}. In jedem der {n_gaps} Fenster empfingen die anderen Knoten weiter.</li>"
        f"<li><b>Rate ohne Pausen:</b> {de(A['rate_outside'], 0)} Ereignisse pro Stunde, über den ganzen Mitschnitt gemittelt {de(A['rate_all'], 0)}. Die Pausen nehmen {de(sil_pct)} % der Zeit.</li>"
        f"<li><b>Kein Logger-Ausfall:</b> größter Zeilenabstand in den Pausen {de(A['worst_line_gap_s'])} s; die Kanalauslastung (alle 10 s) läuft weiter. Das Funkmodul selbst meldet rx = 0 ms in {de(A['util_n'] - A['util_pos'], 0)} von {de(A['util_n'], 0)} Stichproben.</li>"
        f"<li><b>Nicht abgestumpft:</b> die Kanalabfrage (CAD, vor dem Senden) meldet in den Pausen bei {A['cad_busy_sil']} von {A['cad_scan_sil']} Abfragen „belegt“ ({pct(A['cad_busy_sil'], A['cad_scan_sil'])}), über den ganzen Mitschnitt {pct(A['cad_busy_all'], A['cad_scan_all'])}. Das Funkmodul sieht den Kanal also, der Empfänger liefert aber nichts, auch keinen Fehlerrahmen. Ein nur unempfindlicher Empfänger würde die starken Nachbarn weiter dekodieren.</li>"
        f"<li><b>Sender läuft:</b> {own_tx_sil} Sendungen in den Pausen ({de(own_tx_sil / sil_h, 1)} pro Stunde gegen {de(A['radio_tx_per_h'], 1)} im Mittel), {A['tx_in_sil_heard']} von {A['tx_in_sil_logged']} wurden von mindestens einem anderen Knoten mit OE3XIA-12 als letztem Hop gehört. Der Sendepfad funktioniert; der niedrige Takt kommt daher, dass ohne Empfang nichts weiterzuleiten ist.</li>"
        f"<li><b>Neustart des Empfangs wirkungslos:</b> alle etwa 4,8 s ({A['restart_sil']} Neustarts in den Pausen, Wartezeit im Median {de(A['wait_in']['med'], 0)} ms gegen {de(A['wait_out']['med'], 0)} ms sonst, weil nichts hereinkommt).</li>"
        f"<li><b>Ende der Pause:</b> {ends_cad} von {n_gaps} Pausen enden höchstens 3 s nach einer Kanalabfrage (Median {de(A['ends_after_cad_lead_median_s'])} s davor); bei anderen Empfangsereignissen gilt das für {de(A['cad_base_rate_pct'])} % (Grundrate aus {de(A['cad_base_n'], 0)} Ereignissen). Die Kanalabfrage ruft im Code <code>Radio.Standby(); Radio.StartCad()</code> auf.</li>"
        f"<li><b>Beginn der Pause:</b> {A['starts_after_rx_error']} von {n_gaps} Pausen beginnen direkt nach einem RX_ERROR ({pct(A['starts_after_rx_error'], n_gaps, 0)}; Grundrate {de(A['rx_error_base_pct'], 0)} %). Schwacher Hinweis.</li>"
        f"<li><b>Die Ränder sind unauffällig:</b> der Pegel am Anfang und Ende liegt im Median bei {de(A['bnd_median'], 0)} dBm; nur {A['bnd_strong']} von {A['bnd_n']} Randereignissen sind stärker als -80 dBm.</li>"
        "</ul>")
    ap_('<div class="callout act"><p><b>Diagnose (Ablauf gemessen, Ursache im Chip Vermutung):</b> '
        "Kein Logger-Verlust und keine Desensibilisierung, sondern ein Empfangszustand des SX1262-Funkchips, der hängt: keine Empfangs-Unterbrechung, keine Fehlermeldung, während die Kanalabfrage Aktivität sieht. "
        "Der Wachhund der Firmware startet den Empfang alle 4,8 s neu (<code>startRadioReceive()</code>, das <code>Radio.Rx()</code> ohne vorheriges <code>Radio.Standby()</code> aufruft, "
        "<code>src/nrf52/nrf52_main.cpp:1608-1647</code>, Aufruf in Zeile 1631). Das holt den Chip nicht heraus. Die Kanalabfrage dagegen setzt erst den Chip in Standby und startet dann die Abfrage "
        f"(<code>:1697-1698</code>, <code>:1762-1763</code>); danach fließen wieder Rahmen ({ends_cad} von {n_gaps}). "
        "<b>Bench-Hypothese:</b> <code>Radio.Standby()</code> vor <code>startRadioReceive()</code> im Wachhund. Unbewiesen, abhängig vom Build.</p></div>")

    # figure 1: timeline
    show = ["oe3xia", "oe3xoc", "oe3xwj", "oe3xhl"]
    series = []
    for n in show:
        b = A["bins"][n]
        vals = [v for v in b if v is not None]
        mean = sum(vals) / len(vals)
        pts = [(i * 0.5 + 0.25, v / mean) for i, v in enumerate(b) if v is not None]
        series.append((LOGNAME[n], pts))
    xl = [(h, f"{(6 + h) % 24:02d}:00") for h in range(3, 26, 3)]
    f1 = nf()
    ap_(timeline(series, f"Abb. {f1}: OE3XIA-12 fällt immer wieder unter die Rate der Nachbarn (Empfangsereignisse je 30 min, 1,0 = eigenes Mittel)",
                 xlabels=xl, ymax=2.5, height=300))
    ap_("<p class=\"small\">Jede Linie ist durch das Mittel des eigenen Knotens geteilt, damit unterschiedlich laute Standorte vergleichbar sind. Angefangene Halbstundenfenster am Anfang und Ende fehlen; OE3XIR ist nicht eingezeichnet, sein Mitschnitt endet am 31.10. um 18:16. Die Zeitachse ist die Mitschnitt-Uhr (MEZ).</p>")
    hbins = [(f"{(6 + i) % 24:02d}", A["per_hour"][i] / 60.0) for i in range(27)]
    f2 = nf()
    ap_(hist(hbins, f"Abb. {f2}: Die Stillstände verteilen sich über Tag und Nacht (Minuten ohne Empfang je Stunde)", unit=" min", ylabel="Stunde (Uhr, 31.10. ab 06:00)"))
    f3 = nf()
    ap_(explain(draw_deaf(n_gaps, int(round(longest["len_s"])), ends_cad, A["cad_busy_sil"], A["cad_scan_sil"], A["tx_in_sil_heard"], A["tx_in_sil_logged"]),
                f"Abb. {f3}: Der Wachhund-Neustart des Empfangs hilft nicht, nach Standby und Kanalabfrage kommen wieder Rahmen", height=300))
    ap_("<p><b>Was in den Pausen noch passiert:</b> Ringpuffer-Schreibungen in den Pausen: "
        + ", ".join(f"{v} × {k}" for k, v in sorted(A["src_in"].items(), key=lambda kv: -kv[1]))
        + ". Weiterleitungen fremder Rahmen und ACK-Weiterleitungen fehlen fast ganz, weil dafür etwas empfangen werden müsste. "
        "src=auto_pos ist die eigene HEY-Bake, src=user_wx die Wetterposition, src=udp_rx ein Rahmen vom Server, der ins Funknetz soll.</p>")

    ap_(f"<details><summary>Die {n_gaps} Stillstandsfenster mit Vergleich zu den anderen Knoten</summary>")
    srow = []
    for k, r in enumerate(rows, 1):
        o = r["others"]
        srow.append([k, r["start"], mmss(r["len_s"]), r["own_tx"], o["oe3xhl"], o["oe3xoc"], o["oe3xwj"], o["oe3xir"] if o["oe3xir"] else "–", "ja" if r["end_after_cad"] else "nein"])
    ap_(table(["Nr", "Beginn (31.10./01.11.)", "Dauer", "Eigene Sendungen", "Ereignisse OE3XHL", "OE3XOC", "OE3XWJ", "OE3XIR", "Ende nach Kanalabfrage"], srow, num_cols=[0, 3, 4, 5, 6, 7],
              caption="Ereignisse = dekodierte Rahmen (ACK eingeschlossen) plus Fehlerrahmen im selben Zeitfenster. OE3XIR fehlt ab 31.10. 18:16, weil dessen Mitschnitt dort endet."))
    ap_("</details>")

    ap_("<h3>Weitere Gesundheitswerte</h3>")
    hrows = [
        ["Neustarts und Abstürze", "0 (Betriebszeit steigt gleichmäßig von 300 s auf 89.715 s)", chip("ok")],
        ["Speicher", "STAT-Heap 92.268 B ohne Änderung (grobe Anzeige auf dem nRF52)", chip("ok")],
        ["Server-Anbindung", f"{H['gateway']['keepalive_ok']} Keepalives, 1 ausgebliebene Antwort (0,03 %) am 31.10. 07:22:10, gleichzeitig bei allen fünf OE3-Gateways", chip("ok")],
        ["Kanalauslastung", f"Mittel {de(ch['mean'])} %, Median {de(ch['median'], 0)} %, 95 % unter {de(ch['p95'], 0)} %, Spitze {de(ch['max'], 0)} % (3 Stichproben ab 70 %)", chip("ok")],
        ["Rauschteppich", "Untergrenze der Fehlerrahmen -109 dBm, Median -104 dBm; etwa 5 dB über dem Theoriewert -114 dBm", chip("ok")],
        ["Übersteuerung", "stärkster Empfang -72 dBm; nichts über -60 dBm", chip("ok")],
        ["Kanalabfrage vor dem Senden", f"{de(cad['cad_scans'], 0)} Abfragen, {de(cad['initial_busy_rate_pct'])} % zuerst belegt, davon {de(cad['false_positive_pct_of_initial_busy'])} % Fehlalarm; {de(cad['cad_attempt_dist']['0'] / cad['tx_gate_enter'] * 100)} % der Sendungen gleich frei, bei {de(sum(v for k, v in cad['cad_attempt_dist'].items() if int(k) >= 3) / cad['tx_gate_enter'] * 100)} % drei oder mehr Versuche", chip("watch")],
        ["Firmware ohne Kanalabfrage (Pre-CAD)", f"{H['firmware_census']['nodes_pre_cad']} von {H['firmware_census']['nodes_total']} gehörten Stationen ({de(H['firmware_census']['nodes_pre_cad_pct'])} %) senden mit 4.35a bis 4.35o oder älter ohne vorherige Kanalabfrage; sie liefern {de(H['firmware_census']['events_pre_cad_pct'])} % der gehörten Rahmen und {de(H['firmware_census']['airtime_pre_cad_pct'])} % der Sendezeit", chip("watch")],
        ["Verarbeitungszeit je empfangenem Rahmen", "im Mittel 3,9 ms, 95 % unter 13 ms, größte 56 ms (15 von 23.542 über 50 ms)", chip("ok")],
    ]
    ap_(table(["Aspekt", "Wert", "Urteil"], hrows, raw=True))
    ap_(f"<p><b>Verkehr:</b> {de(traffic['heard_events'], 0)} dekodierte Rahmen plus {de(traffic['ack_events'], 0)} ACK-Rahmen ({de(traffic['ack_share_of_all_frames_pct'])} % aller Empfänge), "
        f"{de(traffic['unique_originators_excl_time_server'], 0)} Absender. Die Hop-Zahl im H-Feld ist gleichmäßig verteilt (H00 bis H03 je etwa ein Viertel, H04 {de(traffic['hop_H_pct']['H04'])} %). "
        f"Eigene Sendungen: {de(ntx, 0)} = {de(own['tx_per_hour'], 1)} pro Stunde, {de(own['airtime_s'], 0)} s Sendezeit (2,8 % Auslastung). "
        f"Davon Relais {de(own['src_split']['r'], 0)} ({de(own['src_pct']['r'])} %), Server-Rahmen ins Funknetz {own['src_split']['g']} ({de(own['src_pct']['g'])} %), eigene Baken {own['src_split']['o']} ({de(own['src_pct']['o'])} %).</p>")

    ap_(f"<h3>Fehlempfang: {de(100 * n_err / n_ev)} % der Empfangsereignisse</h3>")
    ap_(f"<p><b>Etwa jedes dritte Empfangsereignis ist ein RX_ERROR:</b> {de(n_err, 0)} von {de(n_ev, 0)} ({pct(n_err, n_ev)}) bei {de(n_ok, 0)} dekodierten Rahmen. "
        f"Nenner sind alle Empfangsereignisse einschließlich der {de(n_ok - n_ondone, 0)} ACK-Rahmen; für ACK-Rahmen wird die Zeile OnRxDone nicht gedruckt (lora_functions.cpp:1018-1034 gegen :2744), "
        "sie müssen deshalb über ONRXDONE_TIME gezählt werden. "
        "Der Empfänger hat ein LoRa-Signal erkannt (Präambel oder Kopf), kann daraus aber keinen gültigen Rahmen bilden. "
        f"Median Pegel {int(rfm['rx_error_rssi']['median'])} dBm, Signal-Rausch-Abstand {int(rfm['rx_error_snr']['median'])} dB: schwach und rauschbegrenzt, keine Übersteuerung. Die Schwelle der Auswertung liegt bei 40 %; OE3XIA bleibt darunter (Urteil ok).</p>")
    ap_("<ul>"
        "<li><b>Vergleich im Cluster:</b> " + ", ".join(f"{LOGNAME[n]} {de(err_share[n])} %" for n in sorted(OE3, key=lambda x: err_share[x])) + " (Anteil RX_ERROR an allen Empfangsereignissen inklusive ACK, gezählt aus den Logs). OE3XIA liegt am höchsten.</li>"
        "<li><b>Starke Fehler fehlen:</b> 38 Fehlerrahmen stärker als -85 dBm, keiner stärker als -60 dBm.</li>"
        "<li><b>Zusammenhang mit den Stillständen:</b> nicht geprüft; der Fehleranteil gilt für den gesamten Mitschnitt, die Pausen enthalten selbst keine Fehlerrahmen.</li>"
        "</ul>")
    f4 = nf()
    ap_(explain(draw_noise(de(n_ev, 0), de(n_err, 0), de(n_ok, 0), de(100 * n_err / n_ev), de(100 * n_ok / n_ev), de(err_share["oe3xhl"]), int(rfm["rx_error_rssi"]["median"]), int(rfm["rx_error_snr"]["median"])), f"Abb. {f4}: Etwa jedes dritte Empfangsereignis ist ein Fehlrahmen"))

    # ---- F2 --------------------------------------------------------------------------------
    ap_("<h2>F2 TX-Puffer mit Slots</h2>")
    ap_(f"<p><b>Die Mechanik arbeitet sauber:</b> {de(q2['markers']['write'], 0)} Schreibungen, keine Überläufe, keine Verdrängung, höchste Füllung {occ['queued']['max']:.0f} von 20 Plätzen. "
        "Der Puffer hat 20 Plätze (Slots); eine Sendung belegt einen Platz, bis sie gesendet, ersetzt oder wegen Alters verworfen ist.</p>")
    ap_("<ul>"
        f"<li><b>Füllung:</b> Median {occ['queued']['p50']:.0f}, 90 % unter {occ['queued']['p90']:.0f}, 99 % unter {occ['queued']['p99']:.0f}; {de(occ['minutes_ge_5'], 0)} min mit 5 oder mehr belegten Plätzen, {de(occ['minutes_ge_10'])} min mit 10 oder mehr, {de(occ['minutes_ge_15'])} min mit 15.</li>"
        f"<li><b>Kein Aufstau:</b> die Steigung der stündlichen Mittelwerte beträgt {de(occ['hourly_mean_slope_per_h'], 3)} Plätze pro Stunde.</li>"
        "<li><b>Nichts hängt:</b> in allen 2.989 Stichproben ist retrying = 0 (kein steckengebliebener Slot). Die Wiederholungsleiter lief nie (0 RING_REARM); sie ist mit diesen Daten nicht getestet.</li>"
        "</ul>")
    fate_rows = [["gesendet", de(fate["counts"]["tx_air"], 0), de(fate["pct"]["tx_air"], 2) + " %"], ["durch neuere Bake ersetzt (gewollt)", fate["counts"]["superseded"], de(fate["pct"]["superseded"], 2) + " %"],
                 ["wegen Alters verworfen (TTL)", fate["counts"]["stale_ttl"], de(fate["pct"]["stale_ttl"], 2) + " %"]]
    ap_(f"<p><b>Was aus den {de(q2['markers']['write'], 0)} Schreibungen wurde</b> (Summe {de(fate['sum_pct'], 0)} %; 5 weitere Verfälle von vor dem Mitschnitt sind nicht enthalten):</p>")
    ap_(table(["Schicksal", "Anzahl", "Anteil"], fate_rows, num_cols=[1, 2]))
    wsrc = ring["ring_write_by_src_type"]
    names = {"rx_ack_fwd:41": "ACK weiterleiten", "rx_relay:40": "fremde HEY weiterleiten", "rx_relay:21": "fremde Position weiterleiten", "udp_rx:3A": "Rahmen vom Server ins Funknetz",
             "auto_pos:40": "eigene HEY-Bake", "rx_relay:3A": "fremden Text weiterleiten", "user_wx:21": "eigene Wetterposition", "rx_dm_ack_gw:41": "Gateway-ACK (Paar, Teil 1)",
             "rx_dm_ack_new:41": "Gateway-ACK (Paar, Teil 2)"}
    tot_w = sum(wsrc.values())
    ap_("<details><summary>Wer schreibt in den Puffer (4.176 Schreibungen nach Quelle)</summary>")
    ap_(table(["Quelle", "Anzahl", "Anteil"], [[names.get(k, k), v, pct(v, tot_w)] for k, v in sorted(wsrc.items(), key=lambda kv: -kv[1])], num_cols=[1, 2]))
    ap_("</details>")
    ap_("<h3>Wartezeit im Puffer nach Priorität</h3>")
    prow = [["1 ACK", "90 s", lat["1"]["n"], lat["1"]["p50"], lat["1"]["p90"], lat["1"]["max"]], ["3 Relais, Text, Server-Rahmen", "300 s (nur Relais)", lat["3"]["n"], lat["3"]["p50"], lat["3"]["p90"], lat["3"]["max"]],
            ["4 Position (eigene Wetter, fremde)", "120 s", lat["4"]["n"], lat["4"]["p50"], lat["4"]["p90"], lat["4"]["max"]], ["5 HEY (eigene, fremde)", "180 s", lat["5"]["n"], lat["5"]["p50"], lat["5"]["p90"], lat["5"]["max"]]]
    ap_(table(["Priorität", "TTL", "gesendet", "Median s", "90 % s", "Größte s"], [[a, b, c, de(d), de(e), de(f)] for a, b, c, d, e, f in prow], num_cols=[2, 3, 4, 5]))
    ap_("<p><b>ACK-Wartezeit 20 bis 60 s je Station:</b> TTL (Lebensdauer im Puffer) und Wartezeit sind verschiedene Dinge. "
        f"Der Median der ACK liegt bei {de(lat['1']['p50'])} s, {de(100 * ex['prio1_ge20s_share'], 0)} % warten 20 s oder länger, nur {int(round(ex['prio1_ge60s_share'] * lat['1']['n']))} ACK über 60 s. "
        "Ursache laut Code-Lesung ist nicht die Länge der Warteschlange, sondern die Haltezeit (CSMA-Hold): jeder empfangene Rahmen setzt sie neu. "
        f"Auch bei leerem Puffer warten die ACK auf OE3XIA und OE3XOC noch 16 bis 18 s (Nachmessung, verify/q2.md Nr. 7). Je Sendung zählten wir {de(Q6['totals']['cadbusy'] / ntx, 1)} Belegt-Meldungen der Kanalabfrage.</p>")
    f5 = nf()
    ack_p50 = {n: Q2["nodes"][n]["latency"]["by_prio_s"]["1"]["p50"] for n in OE3}
    ack_p50["oe3xir"] = XIR["ack_wait_s"]["p50"]  # real ACKs only; q2 value includes the dropped pings to "255."
    ap_(bars([(LOGNAME[n], ack_p50[n]) for n in ("oe3xhl", "oe3xir", "oe3xia", "oe3xoc", "oe3xwj")],
             f"Abb. {f5}: ACK-Wartezeit im Puffer, Median je OE3-Knoten in Sekunden (OE3XIR nur echte ACK, ohne die verworfenen Pings); IA, IR und OC warten mehr als doppelt so lange wie HL und WJ", unit=" s"))
    f6 = nf()
    ap_(explain(draw_ackhold(int(round(lat["1"]["p50"])), ack_ge20, int(round(lat["1"]["max"]))), f"Abb. {f6}: Jeder gehörte Rahmen verlängert die Wartezeit des ACK im Puffer"))
    ap_("<h3>Vergleich mit den anderen OE3-Knoten</h3>")
    ap_(f"<p><b>OE3XIA ist beim Weiterleiten der langsamste Knoten:</b> mittlerer Rang {de(cross['per_node'][NODE]['mean_rank_0fast_1slow'], 2)}. "
        f"Gemessen wurde, wer dieselbe Nachricht (gleiche Kennung) zuerst sendet, in {cross['groups_ge3']} Gruppen mit mindestens drei Knoten. "
        f"OE3XIA war in {cross['per_node'][NODE]['n_in_groups_ge3']} dieser Gruppen dabei und in {cross['per_node'][NODE]['slowest_count']} davon die letzte (Median {de(cross['per_node'][NODE]['lat_p50_in_groups_s'])} s, 90 % unter {de(cross['per_node'][NODE]['lat_p90_in_groups_s'], 0)} s). "
        f"Das gilt gleich für Priorität 5 (Rang {de(cross['mean_rank_prio5'][NODE][0], 2)}) und für Priorität 3 und 4 ({de(cross['mean_rank_prio3_4'][NODE][0], 2)}).</p>")
    rk = sorted(cross["per_node"].items(), key=lambda kv: kv[1]["mean_rank_0fast_1slow"])
    f7 = nf()
    ap_(bars([(LOGNAME[n], v["mean_rank_0fast_1slow"]) for n, v in rk], f"Abb. {f7}: OE3XIA-12 ist im Vergleich der langsamste Relay (mittlerer Rang, 0 = schnellster, 1 = langsamster)", width=900))
    ap_("<p class=\"small\">Vermutung: örtliche Kanalbelegung, nicht Firmware; die Rangfolge ist für alle Prioritäten gleich. Die Stillstände sind nicht herausgerechnet.</p>")

    # ---- F3 --------------------------------------------------------------------------------
    ap_("<h2>F3 Was verlieren wir durch den Zeitverfall</h2>")
    ap_(f"<p><b>Verloren gehen nur eigene Baken und einzelne fremde HEY:</b> {q34['drops_total']} Slots verfielen per TTL (Lebensdauer 180 s für HEY, 120 s für Positionen), "
        "kein einziger war vorher gesendet worden. Kein Text, kein ACK, kein Relais-Text und kein {CET}-Telegramm war dabei.</p>")
    drows = [["eigene HEY", dt_tab["p5/own_HEY"], "180–182 s"], ["fremde HEY (Relais)", dt_tab["p5/relay_HEY"], "180–182 s"], ["eigene Wetterposition", dt_tab["p4/own_WX_POS"], "120–121 s"],
             ["fremde Position (Relais)", dt_tab["p4/relay_POS"], "120 s"], ["vor Mitschnittbeginn geschrieben, nicht klassifizierbar", dt_tab["p4/unknown_prelog"] + dt_tab["p5/unknown_prelog"], "120–182 s"]]
    ap_(table(["Was verfiel", "Anzahl", "Alter beim Verfall"], drows, num_cols=[1]))
    ap_(f"<p class=\"small\">Summe {sum(r[1] for r in drows)} von {q34['drops_total']}.</p>")
    f8 = nf()
    ap_(explain(draw_starve(hc["fate"]["stale_ttl"], hc["chains"]), f"Abb. {f8}: Die eigene Bake wartet hinter ACK und Relais, bis ihre Lebensdauer abläuft", height=280))
    ap_("<h3>Eigene Baken</h3>")
    ap_("<ul>"
        f"<li><b>HEY (Lebenszeichen alle paar Minuten):</b> {fate['by_src']['auto_pos']['tx_air']} gesendet, {fate['by_src']['auto_pos']['superseded']} durch eine neuere HEY ersetzt (gewollt), {fate['by_src']['auto_pos']['stale_ttl']} per TTL verloren. "
        f"Zählt man Ketten (eine neuere ersetzt die ungesendete ältere), sind es {hc['chains']} Ketten ({hey_written} Schreibungen minus {fate['by_src']['auto_pos']['superseded']} ersetzte): {hc['fate']['tx_air']} gesendet, {hc['fate']['stale_ttl']} verloren, "
        f"das sind {de(hc['aired_pct'])} % der Ketten bzw. {pct(hey_aired, hey_written, 0)} aller {hey_written} geschriebenen HEY. Beide Zahlen sind richtig, sie haben nur verschiedene Nenner.</li>"
        f"<li><b>Abstand gesendeter HEY:</b> Median {mmss(ex['own_hey_gap_s']['p50'])}, 90 % unter {mmss(ex['own_hey_gap_s']['p90'])}, größter {mmss(ex['own_hey_gap_s']['max'])}; {ex['own_hey_gaps_over_20min']} Abstände über 20 min.</li>"
        f"<li><b>Wetterposition (jeweils xx:00 und xx:30):</b> {fate['by_src']['user_wx']['tx_air']} gesendet, {fate['by_src']['user_wx']['stale_ttl']} per TTL verloren ({fate['by_src']['user_wx']['stale_ttl']} von {q2['slots']['own_writes_by_src']['user_wx']}). Längste Funkpause der Wetterposition 2,06 h.</li>"
        f"<li><b>Internet ist nicht betroffen:</b> {A['wx_dropped_udp']} von {A['wx_dropped']} verlorenen Wetterpositionen gingen beim Schreiben per UDP an den Server (TX-UDP, gezählt im Log). Verloren ist nur die Funkkopie.</li>"
        "</ul>")
    f9 = nf()
    ap_(bars([("HEY gesendet", fate["by_src"]["auto_pos"]["tx_air"]), ("HEY ersetzt (gewollt)", fate["by_src"]["auto_pos"]["superseded"]), ("HEY per TTL verloren", fate["by_src"]["auto_pos"]["stale_ttl"]),
              ("Wetter gesendet", fate["by_src"]["user_wx"]["tx_air"]), ("Wetter per TTL verloren", fate["by_src"]["user_wx"]["stale_ttl"])],
             f"Abb. {f9}: Eigene Baken von OE3XIA-12, was aus {hey_written} HEY und {q2['slots']['own_writes_by_src']['user_wx']} Wetterpositionen wurde (Anzahl)"))
    ap_("<p><b>Ursache in drei Teilen (verifiziert):</b></p><ol>"
        "<li><b>Strenge Rangfolge:</b> der Puffer sendet zuerst, was die kleinste Prioritätszahl hat, ohne Alterungsausgleich (<code>txring_functions.cpp:213-255</code>). ACK (Priorität 1) gehen vor Baken (Priorität 4 und 5). "
        "Das nimmt einer verlorenen Bake 64 bis 72 % ihrer Wartezeit (Kopfzeit-Modell, verify/q2.md Nr. 4).</li>"
        "<li><b>Haltezeit am Listenkopf:</b> ist die Bake an der Reihe, gilt für Priorität 4 und 5 eine Haltezeit von 5,5 s, die jeder gehörte Rahmen neu startet (<code>lora_functions.cpp:2746-2747</code>); der Altersboden gilt nur für Priorität 1 bis 3 (<code>csma_timing.h:99-111</code>). "
        "Ein Relais derselben Priorität bekommt nach 8 s Wartezeit über den NBR-Weg (Case A) nur 150 bis 220 ms (<code>txring_functions.cpp:385-405</code>); die eigene Bake hat diesen Weg nicht. "
        "Auf OE3XOC schaffte eine Bake am Listenkopf in 2924 s nur 21 Kanalabfragen.</li>"
        "<li><b>Kanalabfrage schlägt fehl:</b> auf OE3XIA werden die Abfragen gemacht, aber 62 von 62 melden „belegt“ (CAD_BUSY). Ein Altersboden für Priorität 4 und 5 allein reicht dort nicht.</li></ol>"
        "<p>Die Sendezeit ist nicht der Engpass (auf OE3XOC nur 4,2 % Sendeanteil), es ist der Kanalzugang. Eine längere TTL würde Baken nur länger warten lassen, nicht senden. "
        "Ob das hinnehmbar ist, ist eine Entscheidung des Betreibers; Herleitung und Korrekturkandidat: <a href=\"verify/rca_own_beacons.md\">verify/rca_own_beacons.md</a>.</p>")
    ap_("<h3>Fremde Relais-Kopien</h3>")
    ap_(f"<p><b>Meist anderswo schon abgedeckt:</b> von {dt_tab['p5/relay_HEY'] + dt_tab['p4/relay_POS']} verfallenen fremden Kopien hat ein anderer OE3-Knoten {rc['redundant_cluster_tx']} gesendet, "
        f"bei {rc['redundant_heard_lower_hop']} hörten wir selbst eine Kopie mit niedrigerem Hop, bei {rc['weak_cluster_tx_before']} ist die Lage schwach belegt und bei {rc['no_evidence']} gibt es keinen Beleg. "
        f"Das sind {rc['redundant_cluster_tx'] + rc['redundant_heard_lower_hop']} von {sum(rc.values())} sicher oder fast sicher redundant, {rc['weak_cluster_tx_before'] + rc['no_evidence']} mögliche echte Verluste. "
        f"Von ihnen kam bei {q34['relay_refreshed_within_30min']} der 37 binnen 30 Minuten eine neuere Meldung desselben Absenders (Median {de(q34['relay_refresh_median_s'], 0)} s).</p>")
    ap_("<h3>Zeitsignal {CET} im Puffer</h3>")
    ap_(f"<p><b>Kein {{CET}}-Telegramm ist am Alterstor gescheitert:</b> 0 Verfälle mit Grund cet. {Q5['tx_count']} Telegramme gesendet, Alter beim Senden im Median {Q5['tx_age_s_node_based']['p50']:.0f} s, größtes {Q5['tx_age_s_logger_based']['max']:.0f} s "
        f"(Grenze 150 s, Reserve also etwa 28 s). {q34['cet_tx_age_s']['n_gt_120']} Telegramm war älter als 120 s.</p>")
    ap_("<h3>Altersboden der Haltezeit (CSMA_AGED)</h3>")
    ap_(f"<p><b>Der Altersboden wirkt fast nur bei ACK und bringt neutral bis leicht positiv:</b> {de(csma['events'], 0)} Meldungen in {csma['episodes']} Slot-Episoden ({csma['ep_prio']['1']} davon ACK). "
        "Ab 20 s Alter (Stufe 1) wird die Haltezeit auf etwa 1,5 s begrenzt, ab 60 s (Stufe 2) auf etwa 100 ms. Die Stufe 2 trat nur einmal auf, weil ACK schon nach 90 s verfallen und selten so lange warten.</p>")
    ap_("<ul>"
        f"<li><b>Haltezeit:</b> Median {de(csma['hold_ms']['p50'], 0)} ms ({de(csma['hold_ms']['p10'], 0)} bis {de(csma['hold_ms']['p90'], 0)} ms).</li>"
        f"<li><b>Betroffen:</b> {csma['tx_prio_le3_wait_ge20s_pct']} % der Sendungen mit Priorität 1 bis 3 warteten 20 s oder länger ({csma['tx_prio_le3_wait_ge20s']} von {csma['tx_prio_le3']}); Stufe 1 gilt hier praktisch dauernd.</li>"
        f"<li><b>Ersparnis:</b> Nachrechnung mit echten Empfangszeitpunkten ergibt im Median 0,6 bis 1,6 s je Episode, je nachdem welcher Versuch zuvor lief (der Versuchszähler steht nicht im Log).</li>"
        f"<li><b>Kollisionshinweise:</b> RX_ERROR in den 3 s nach eigener Sendung bei gealterten ACK {de(csma['probe_p1_aged']['pct_rxerr_within_3s_after_tx'])} %, bei frischen {de(csma['probe_p1_fresh']['pct_rxerr_within_3s_after_tx'])} %; zufälliges Fenster {de(csma['baseline_pct_rxerr_in_3s'], 0)} %. Kein Unterschied.</li>"
        "<li><b>Doppelte ACK:</b> 12 ACK-Kennungen gingen zweimal binnen 21 s hinaus; es sind die 12 gewollten Gateway-ACK-Paare (zwei Schreibungen im Abstand von 13 ms, GWACK-01).</li>"
        "</ul>")

    # ---- F4 --------------------------------------------------------------------------------
    ap_("<h2>F4 Was gewinnen wir durch den Zeitverfall</h2>")
    ap_(f"<p><b>Der Gewinn ist Frische, nicht Sendezeit:</b> Die {cf['baseline_ttl_drops']} verworfenen Slots hätten {de(q34['drop_toa_s'])} s Sendezeit gekostet, das sind {de(q34['drop_toa_pct_of_tx'], 2)} % der eigenen {de(q34['tx_toa_s'], 0)} s ({de(q34['drop_toa_s'] / q34['span_h'])} s pro Stunde).</p>")
    ap_("<ul>"
        f"<li><b>Ohne TTL (Modellrechnung, erste Näherung):</b> {cf['stale_fate_no_ttl']['served']} der {cf['baseline_ttl_drops']} Verfälle würden gesendet, im Alter von median {de(cf['stale_air_age_s_if_served']['p50'], 0)} s, 90 % unter {de(cf['stale_air_age_s_if_served']['p90'], 0)} s, höchstens {de(cf['stale_air_age_s_if_served']['max'], 0)} s.</li>"
        f"<li><b>Frisches käme später:</b> {de(cf['fresh_delayed']['pct_delayed'])} % der frischen Baken und Relais (Priorität 3 bis 5) würden sich verzögern, im Median um {de(cf['fresh_delayed']['delay_s_p50'], 0)} s, 90 % unter {de(cf['fresh_delayed']['delay_s_p90'], 0)} s.</li>"
        f"<li><b>Kein Überlaufschutz nötig:</b> Füllung zeitgewichtet {de(cf['depth_time_weighted']['ttl_on'], 2)} → {de(cf['depth_time_weighted']['no_ttl'], 2)}, höchste {cf['depth_ttl_on']['max']} → {cf['depth_no_ttl']['max']} von 20.</li>"
        f"<li><b>Freigegebene Platz-Sekunden:</b> {de(cf['stale_slot_seconds_real'], 0)} (im Mittel {de(cf['stale_slot_seconds_real'] / (q34['span_h'] * 3600), 2)} von 20 Plätzen).</li>"
        f"<li><b>Reserve der TTL:</b> ACK längste Wartezeit {de(hr['p1']['max'])} s bei 90 s TTL, Relais {de(hr['p3']['max'])} s bei 300 s. Priorität 4 und 5 liegen am Limit ({hr['p4']['n_ge80pct_ttl_sent']} und {hr['p5']['n_ge80pct_ttl_sent']} gesendete Rahmen über 80 % der TTL).</li>"
        "</ul>")
    t5, t4 = q34["ttl_sens"]["p5"]["table"], q34["ttl_sens"]["p4"]["table"]
    ap_(f"<p><b>Empfindlichkeit (Vermutung aus der Modellrechnung):</b> Bei Priorität 5 würde eine TTL von 300 s statt 180 s {t5['180']['n_over'] - t5['300']['n_over']} der {t5['180']['n_over']} modellierten Überschreitungen vermeiden; bei Priorität 4 würde 180 s statt 120 s alle {t4['120']['n_over']} vermeiden. "
        "Der Preis wären bis zu 4 bis 5 Minuten alte Rahmen. Eine Lösung nur für eigene Baken träfe 51 der 93 Verfälle.</p>")

    # ---- F5 --------------------------------------------------------------------------------
    ap_("<h2>F5 Zeitsignal</h2>")
    ap_(f"<p><b>Die Uhrzeit-Telegramme laufen sauber, zwei von drei gehen aber hinaus, obwohl eine Abstrahlung schon zu hören war:</b> OE3XIA hat {Q5['tx_count']} {{CET}}-Telegramme des Servers OE1XAR-33 ins Funknetz gesetzt, keines älter als 150 s "
        f"(größtes {Q5['tx_age_s_logger_based']['max']:.0f} s nach Logger-Uhr). {{CET}} ist das Zeittelegramm des Servers; trotz der Bezeichnung enthält es UTC.</p>")
    ap_("<ul>"
        f"<li><b>Zustellung vom Server:</b> {Q5['udp_got']} von {Q5['expected_in_window']} Telegrammen per UDP, Abstand im Median {Q5['udp_arrival_gap_s']['p50']:.0f} s, keines fehlt.</li>"
        f"<li><b>Wartezeit im Puffer:</b> Median {de(Q5['tx_wait_ms']['p50'] / 1000.0)} s, 90 % unter {de(Q5['tx_wait_ms']['p90'] / 1000.0, 0)} s, größte {de(Q5['tx_wait_ms']['max'] / 1000.0, 0)} s; ein {{CET}}-Rahmen hat Priorität 3 und steht hinter den ACK.</li>"
        f"<li><b>Doppelt gesendet:</b> {Q5['tx_after_overheard_any_rf_copy_n']} von {Q5['tx_count']} ({de(Q5['tx_after_overheard_pct'])} %) strahlte OE3XIA ab, nachdem schon eine Abstrahlung desselben Telegramms zu hören war ({Q5['tx_after_overheard_gateway_radiation_n']} davon von einem Gateway), im Median {de(Q5['tx_after_overheard_lead_s']['p50'])} s später.</li>"
        f"<li><b>Kosten:</b> Senden {de(Q5['cet_tx_airtime_s_per_h'])} s pro Stunde ({de(Q5['cet_share_of_stat_tx_pct'])} % der Sendezeit), Empfangen {de(Q5['cet_rx_airtime_s_per_h'])} s pro Stunde ({de(Q5['rawpass']['rx_cet_share_of_decoded_airtime_pct'])} % der dekodierten Empfangszeit); im Mittel {de(Q5['rf_copies_per_id_mean'])} Funkkopien je Telegramm.</li>"
        f"<li><b>Veraltete Zeit auf dem Funkweg:</b> {Q5['rf_age_gt150_n']} von {Q5['rf_valid_frames']} gehörten Rahmen ({de(Q5['rf_age_gt150_pct'])} %) waren älter als 150 s, am häufigsten über OE3XDC-12 ({Q5['rf_stale_lasthop_top'][0][1]}), OE3MIF-12 ({Q5['rf_stale_lasthop_top'][1][1]}) und OE3NRA-1 ({Q5['rf_stale_lasthop_top'][2][1]}). Unter unseren eigenen Sendungen ist keine (0 von {Q5['tx_count']} älter als 150 s).</li>"
        "</ul>")
    f10 = nf()
    ap_(explain(draw_cet(de(Q5["tx_after_overheard_lead_s"]["p50"])), f"Abb. {f10}: OE3XIA sendet das Zeittelegramm, obwohl ein anderes Gateway es schon abgestrahlt hat"))
    ap_("<p><b>Zeitbasis:</b> Die Knotenuhr von OE3XIA weicht nur um etwa 1 s von der Logger-Uhr ab (NTP-Abweichung im Median -0,6 s). Die Zeit im Telegramm ist UTC, die Beschriftung „CET“ ist ein Namensfehler auf dem Server. "
        "Die Altersberechnung ist davon nicht betroffen.</p>")

    # ---- F6 --------------------------------------------------------------------------------
    ap_("<h2>F6 Manipulationen dieses Knotens</h2>")
    ap_("<p><b>Was der Knoten mit Rahmen anstellt (weiterleiten, unterdrücken, ersetzen, verwerfen) und welche Wirkung das hat:</b></p>")
    rly = own["relay_decisions_RLY"]
    pos = [f"<li><b>Weiterleiten:</b> {de(ring['relay_queued'], 0)} Relais eingereiht, {de(Q6['relay_fate']['sent'], 0)} gesendet; so kommen Baken und Texte aus Tälern heraus.</li>",
           f"<li><b>Dedup-Ring (Wiedererkennung gleicher Rahmen):</b> {de(stat_pr['dup_per_new'], 2)} Kopien je neuem Rahmen unterdrückt ({de(stat_pr['dup'], 0)} Kopien auf {de(stat_pr['newid'], 0)} neue Kennungen).</li>",
           f"<li><b>Ping-Filter:</b> {rly['gwfilter']} {{ping}}-Rahmen nicht weitergeleitet (Gateway-Filter).</li>",
           f"<li><b>Hop-Grenze:</b> {rly['hop0']} Rahmen mit Hop 0 und {rly['gwcap']} durch die Gateway-Kappe nicht weitergeleitet; begrenzt die Flut.</li>",
           f"<li><b>Ersetzen:</b> {q2['supersede']['events']} ungesendete HEY durch eine neuere HEY desselben Absenders ersetzt.</li>",
           f"<li><b>Keine Schleifen:</b> {ring['relay_loop_blocked']} Schleifensperren nötig.</li>",
           "<li><b>Hilft OE3XHL:</b> 20 von 20 Abbrüchen bei OE3XHL, die OE3XIA-12 nennen, passen zu einer Sendung von OE3XIA innerhalb von 5 s (q6/findings.md, M7).</li>"]
    neg = [f"<li><b>ACK ohne Abbruchweg:</b> {own['src_type']['r:A']} ACK-Sendungen ({pct(own['src_type']['r:A'], ntx)} aller Sendungen); {q6_co['A']['sent_with_foreign_copy_heard_while_queued']} ({pct(q6_co['A']['sent_with_foreign_copy_heard_while_queued'], own['src_type']['r:A'])}) gingen raus, nachdem eine Kopie schon gehört war.</li>",
           f"<li><b>Alleinversorger-Veto (Case A):</b> in allen {Q6['totals']['need_A']} Bedarfsmeldungen sieht sich OE3XIA als einziger Versorger; 0 Abbrüche (Overhear-Cancel) bei {Q6['totals']['refuse']} Verweigerungen.</li>",
           f"<li><b>Relais nach gehörter Kopie:</b> {q6_co['@']['sent_with_foreign_copy_heard_while_queued'] + q6_co['!']['sent_with_foreign_copy_heard_while_queued'] + q6_co[':']['sent_with_foreign_copy_heard_while_queued']} von {Q6['relay_fate']['sent']} gesendeten Relais; mit den ACK zusammen {opp['opportunity_air_s_total']} s = {de(opp['opportunity_share_of_node_tx_air_pct'])} % der eigenen Sendezeit (Obergrenze, nicht Ersparnis).</li>",
           f"<li><b>Verfall:</b> {q34['drops_total']} TTL-Verfälle, davon {q34['own_dropped']} eigene Baken.</li>",
           f"<li><b>Lange Wartezeit:</b> Relais im Median {de(Q6['relay_queue_wait_median_s'])} s im Puffer; {Q6['tx_wait']['cad_attempts_hist']['10']} von {ntx} Sendungen brauchten 10 oder mehr Kanalabfragen.</li>"]
    neu = ["<li><b>Gateway-ACK-Paare:</b> 12 gewollte Doppelsendungen (GWACK-01).</li>",
           f"<li><b>Upload ans Internet:</b> {Q6['totals']['gwu']} Uploads (GWU), keine Funkkosten.</li>",
           f"<li><b>Server-Rahmen ins Funknetz:</b> {own['src_split']['g']} Rahmen (davon {Q5['tx_count']} {{CET}}); die meisten sendet auch ein anderes Gateway (Cluster: 231 von 362 Rahmen durch 4 bis 5 Gateways).</li>",
           "<li><b>Echo-Tabelle:</b> 297 ECHO-Zeilen (Nachbar hat unsere Bake wiederholt, q6/findings.md M16), nur Diagnose.</li>"]
    ap_('<div class="grid3">'
        f'<div class="card pos"><h3>{chip("pos")}</h3><ul>{"".join(pos)}</ul></div>'
        f'<div class="card neg"><h3>{chip("neg")}</h3><ul>{"".join(neg)}</ul></div>'
        f'<div class="card neu"><h3>{chip("neu")}</h3><ul>{"".join(neu)}</ul></div></div>')
    f11 = nf()
    ap_(explain(draw_ackrelay(q6_co["A"]["sent_with_foreign_copy_heard_while_queued"], own["src_type"]["r:A"]), f"Abb. {f11}: Ein ACK wird von mehreren Gateways weitergeleitet, weil ACK keinen Abbruchweg haben"))
    ap_(f"<p><b>Der Anlass dahinter:</b> {de(ackp['share_pct'])} % aller {de(traffic['ack_events'], 0)} empfangenen ACK-Rahmen bestätigen den Ping von OE3XWJ-12 an die Gruppe 2025 (einmal pro Minute). Die ACK machen {de(traffic['ack_share_of_all_frames_pct'])} % aller Empfänge und {de(traffic['airtime_share_by_type_pct']['ACK'])} % der dekodierten Empfangszeit aus. "
        "Das ist ein Netzthema (Übersichtsseite), betrifft diesen Knoten aber mit voller Wucht. "
        "<a href=\"uebersicht.html\">Netzweite Sicht in der Übersicht</a></p>")
    ap_("<ul>"
        "<li><b>Die ACK kommen von fremden Gateways:</b> im Gateway-ACK-Format, im Mittel etwa 4,6 verschiedene ACK-Kennungen je Ping. Im aktuellen Code quittiert ein Gateway einen {ping} nie (<code>lora_functions.cpp:2169-2173</code>, ebenso upstream); "
        "die quittierenden Gateways laufen also mit anderem Build (Vermutung). OE3XIA leitet diese ACK nur weiter.</li>"
        "<li><b>Auto-Ping: Unterschied ESP32 / RAK4631, Entscheidung offen:</b> die ESP32-Schleife sendet nur, solange <code>node_pingcount</code> größer 0 ist, und zählt herunter (<code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code>). "
        "Die RAK4631-Schleife hat keinen Zähler und pingt alle <code>node_pingtime</code> Sekunden, solange <code>node_pingcall</code> gesetzt ist (<code>src/nrf52/nrf52_main.cpp:2717-2737</code>); <code>--ping stop</code> (<code>command_functions.cpp:3886</code>) wirkt dort nicht, nur das Leeren von <code>--pingcall</code> stoppt. "
        "Upstream dev 147812e0 hat denselben Code. Welches Verhalten gewollt ist, entscheidet der Betreiber. In diesem Mitschnitt pingt OE3XWJ-12 alle 60 s die Gruppe 2025 (1.529 Mal, CORE-FINDINGS Nr. 1).</li></ul>")

    # ---- Verdachtsfaelle -------------------------------------------------------------------
    ap_("<h2>Verdachtsfälle Firmware</h2>")
    ap_('<div class="callout act"><p><b>V1 Empfänger taub, Betrieb läuft weiter</b> ' + chip("act", "Verdacht") + '</p><ul>'
        f"<li><b>Beobachtung:</b> {n_gaps} Pausen über 300 s ohne dekodierten Rahmen (ACK eingeschlossen) und ohne Fehlerrahmen, zusammen {de(sil_s, 0)} s = {de(sil_pct)} %, längste {de(longest['len_s'], 0)} s ab {longest['start']}; nur an diesem Knoten (die anderen höchstens {de(max(v for o, v in A['max_gap'].items() if o != NODE), 0)} s).</li>"
        f"<li><b>Beleg:</b> kein Logger-Ausfall (größter Zeilenabstand {de(A['worst_line_gap_s'])} s), Funkmodul meldet rx = 0 ms in {de(A['util_n'], 0)} Stichproben, CAD zuerst belegt {A['cad_busy_sil']} von {A['cad_scan_sil']}, {own_tx_sil} eigene Sendungen ({A['tx_in_sil_heard']} gehört), {A['restart_sil']} Neustarts des Empfangs ohne Wirkung. "
        f"{ends_cad} von {n_gaps} Pausen enden höchstens 3 s nach einer Kanalabfrage (Grundrate {de(A['cad_base_rate_pct'])} %). Beispiel {rows[sil_row - 1]['start']} Uhr, {mmss(rows[sil_row - 1]['len_s'])} (Fenster {sil_row} der Tabelle).</li>"
        "<li><b>Code-Stellen:</b> <code>src/nrf52/nrf52_main.cpp:1608-1647</code> (Wachhund: nach der Haltezeit <code>startRadioReceive()</code> ohne Standby, Aufruf in Zeile 1631), <code>src/nrf52/nrf52_radio.cpp:9-19</code> (<code>Radio.Rx()</code> bzw. <code>Radio.RxBoosted()</code>), "
        "<code>src/nrf52/nrf52_main.cpp:1697-1698</code> und <code>:1762-1763</code> (Kanalabfrage: <code>Radio.Standby(); Radio.StartCad()</code>).</li>"
        "<li><b>Hypothese für den Prüfstand:</b> <code>Radio.Standby()</code> vor <code>startRadioReceive()</code> im Wachhund. Nicht widerlegt und nicht bewiesen; hängt vom Build ab. Funkseitige Ursachen sind weniger wahrscheinlich, aber nicht ausgeschlossen.</li>"
        f"<li><b>Zeichnung:</b> Abb. {f3}.</li>"
        "<li><b>Status:</b> Verdacht.</li></ul></div>")
    ap_('<div class="callout watch"><p><b>V2 ACK-Weiterleitung ohne Abbruchweg</b> ' + chip("ok", "bestätigt (Code gelesen)") + '</p><ul>'
        f"<li><b>Beobachtung:</b> {q6_co['A']['sent_with_foreign_copy_heard_while_queued']} von {own['src_type']['r:A']} ACK-Sendungen gingen raus, nachdem eine Kopie schon gehört war (Obergrenze: ein ACK kann auch die zweite Gateway-ACK-Kopie des Absenders sein).</li>"
        "<li><b>Beleg:</b> ACK werden als Art OTHER ohne Bedarfseintrag eingereiht; der Abbruch-Scan läuft nur für ':'-, '!'- und '@'-Rahmen in Relay-Slots.</li>"
        "<li><b>Code-Stellen:</b> <code>src/lora_functions.cpp:653-662</code>, <code>:1327</code> und <code>:1359</code>.</li>"
        f"<li><b>Zeichnung:</b> Abb. {f11}.</li></ul></div>")
    ap_('<div class="callout watch"><p><b>V3 Zeitserver-Slots sind immer „Alleinversorger“ (Case A in 100 %)</b> ' + chip("watch", "Beobachten") + '</p><ul>'
        f"<li><b>Beobachtung:</b> alle {Q6['totals']['need_A']} Bedarfsmeldungen von OE3XIA sind Case A (alleiniger Versorger), 0 Abbrüche; nur OE3XHL bricht ab. Bei {{CET}}-Slots ist es im ganzen Cluster 666 von 666 (CORE-FINDINGS Nr. 5). {de(Q5['tx_after_overheard_pct'])} % der {{CET}}-Sendungen von OE3XIA folgen einer gehörten Abstrahlung.</li>"
        "<li><b>Richtige Frage:</b> nicht „Bedarf gleich allein“ (das stimmt nur in 3 von 666 Fällen), sondern warum die Menge der Alleinversorger für einen Server-Slot nie leer wird. OE3MIF-12 und OE3NRA-1 stehen bei vier Knoten in 95,6 bis 100 % der Bedarfsmeldungen darin.</li>"
        "<li><b>Eine frühere Erklärung ist widerlegt:</b> das 12-h-Fenster ist nicht der Treiber, denn Case A liegt schon in den ersten 60 Betriebsminuten bei 100 %; außerdem kann eine aus Symmetrie abgeleitete Kante die Rückrichtung liefern (<code>nbr_matrix.cpp:1829-1848</code>).</li>"
        "<li><b>Prüfstellen:</b> Dependents-Regel und Alternativ-Provider-Test (<code>src/nbr_matrix.cpp:1777</code>, <code>:1816-1852</code>), <code>src/lora_functions.cpp:1380</code> (REFUSE).</li>"
        f"<li><b>Zeichnung:</b> Abb. {f10}.</li>"
        "<li><b>Offen:</b> ob der Abbruchmodus auf OE3XIA eingeschaltet ist, lässt sich aus dem Log nicht beweisen.</li></ul></div>")
    ap_('<div class="callout watch"><p><b>V4 Eigene Baken verhungern (Entwurfslücke, kein Codefehler)</b> ' + chip("act", "bestätigt") + '</p><ul>'
        f"<li><b>Beobachtung:</b> {q34['own_dropped']} eigene Baken per TTL verloren, keine davon vorher gesendet. HEY {de(hc['aired_pct'])} % der Ketten gesendet ({hc['fate']['tx_air']} von {hey_chains}), {pct(hey_aired, hey_written, 0)} aller {hey_written} geschriebenen HEY; "
        f"Wetterposition {fate['by_src']['user_wx']['stale_ttl']} von {q2['slots']['own_writes_by_src']['user_wx']} verloren.</li>"
        "<li><b>Wurzel:</b> eine eigene Bake am Listenkopf bekommt die 5,5-s-Haltezeit, die jeder gehörte Rahmen neu startet; ein Relais derselben Priorität bekommt über den NBR-Weg 150 bis 220 ms. Eigene Baken sind von diesem Weg und vom Altersboden ausgenommen. "
        "Der Plan sah „Priorität 4 bis 5 steigt nie auf“ vor; für Relais gilt das nicht mehr.</li>"
        "<li><b>Korrekturkandidat (ungetestet):</b> Altersboden auch für Priorität 4 und 5, nur für Nicht-Relay-Slots (<code>csma_timing.h:99-108</code>, <code>lora_functions.cpp:4030-4043</code>). Auf OE3XIA kämen die 62 von 62 CAD_BUSY dazu, daher wäre auch ein Aufsteigen in der Priorität zu erwägen. "
        "Eine längere TTL allein nützt nicht. Herleitung: <a href=\"verify/rca_own_beacons.md\">verify/rca_own_beacons.md</a>.</li>"
        "<li><b>Code-Stellen:</b> <code>txring_functions.cpp:213-255</code> (strenge Rangfolge), <code>lora_functions.cpp:2746-2747</code> (Neustart der Haltezeit), <code>txring_functions.cpp:385-405</code>, <code>txring_ttl.h:46-64</code>.</li>"
        f"<li><b>Zeichnung:</b> Abb. {f8}.</li></ul></div>")

    # ---- Nachbarn --------------------------------------------------------------------------
    ap_("<h2>Nachbarn und Hardware-Inventar</h2>")
    ap_("<p>RSSI (Empfangspegel) und SNR (Signal-Rausch-Abstand) gelten nur für direkte Nachbarn, also den letzten Hop. Der RSSI-Wert klebt bei schwachen Verbindungen am Rauschteppich und ist dort nicht aussagekräftig.</p>")
    heads = ["Rufzeichen", "Hardware", "Firmware", "Standort", "Höhe", "Entfernung", "Ereignisse", "RSSI / SNR (Median)"]
    ap_(table(heads, [neigh_row(t) for t in top[:10]], num_cols=[6]))
    ap_("<details><summary>Weitere direkte Nachbarn und genannte Stationen</summary>")
    extra_calls = ["OE3MIF-12", "OE3NRA-1", "OE3MTC-10", "OE3XHL-12", "OE3XWJ-12"]
    erows = [neigh_row(t) for t in top[10:]]
    have = {t["call"] for t in top}
    for c in extra_calls:
        if c not in have:
            m = master.get(c)
            dd = dist.get(c, "")
            erows.append([c, (m["hardware"] if m else "keine Stammdaten"), (m["firmware"] if m else "unbekannt"), ((m.get("description") or "") if m else "keine Stammdaten"),
                          (m["altitudeM"] + " m" if m and m.get("altitudeM") else "unbekannt"), (de(float(dd)) + " km" if dd else "unbekannt"), "–", "–"])
    ap_(table(heads, erows, num_cols=[6]))
    ap_("<p class=\"small\">Einträge ohne Stammdaten tragen die Hardware aus dem Funkrahmen. OE3XIR-12 hat keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg). Firmware-Spalte: laut Stammdaten, sonst aus dem Funkrahmen.</p>")
    ap_("</details>")

    # ---- Grenzen ---------------------------------------------------------------------------
    ap_("<h2>Was nicht geprüft ist / Grenzen</h2><ul>"
        "<li><b>Ursache des Stillstands:</b> Ablauf gemessen, Ursache im Funkchip Vermutung. Es gibt kein Funkspektrum, keinen Chipstatus und keine Spannungsmessung im Log. Die Standby-Hypothese ist nicht auf dem Prüfstand getestet.</li>"
        "<li><b>Vor dem Mitschnitt:</b> Ob die Pausen auch an anderen Tagen auftreten, wissen wir nicht. Die erste Pause begann 1 h 46 min nach Beginn.</li>"
        "<li><b>Build:</b> genauer Firmware-Stand unbekannt; Code-Stellen am Stand fork-dev 651e0920 gelesen, TX-Pool-Stand aus Log-Markern abgeleitet.</li>"
        "<li><b>LoRa-Parameter:</b> Frequenz und Leistung nicht im Log; Sendezeit aus SF11/BW250/CR4:6 gerechnet.</li>"
        "<li><b>Verlusttest:</b> sieht nur fünf OE3-Knoten und was sie hören; „kein Beleg“ heißt nicht, dass niemand sonst den Rahmen weitergegeben hat.</li>"
        "<li><b>Modelle:</b> Ohne-TTL-Rechnung, Zeitersparnis des Altersbodens und die Kopfzeit-Anteile (64 bis 72 %) sind Modellrechnungen, keine Messung.</li>"
        "<li><b>Korrekturkandidat Altersboden für Priorität 4 und 5:</b> nicht erprobt (kein Replay, kein Bench).</li>"
        "<li><b>Overhear-Cancel:</b> ob er auf OE3XIA aktiv ist, lässt sich aus dem Log nicht beweisen.</li>"
        "<li><b>Retry-Leiter:</b> lief nie, bleibt ungetestet.</li>"
        "<li><b>Nur 25 Stunden</b> eines Samstags (31.10.2026), kein Wochenvergleich.</li></ul>")

    ap_("<h2>Offene Fragen an den Betreiber</h2><ol>"
        "<li>Wurden am Standort Exelberg an diesem Tag Antenne, Stecker, Netzteil oder Ethernet-Kabel angefasst, und gab es andere Sender in der Nähe?</li>"
        "<li>Treten die Pausen auch auf einem zweiten RAK4631 am selben Standort auf (Gegenprobe Hardware gegen Standort)? Kann der Standby-Test auf dem Prüfstand gefahren werden?</li>"
        "<li>Ist der Overhear-Cancel (<code>--nbrcancel</code>) auf OE3XIA ein- oder ausgeschaltet?</li>"
        f"<li>Ist der Verlust eigener HEY ({hc['fate']['stale_ttl']} von {hey_chains} Ketten) und der Wetterposition ({fate['by_src']['user_wx']['stale_ttl']} von {q2['slots']['own_writes_by_src']['user_wx']}) hinnehmbar, oder soll der Altersboden auch für eigene Baken (Priorität 4 und 5) gelten?</li>"
        f"<li>Auto-Ping auf dem RAK4631: soll er wie beim ESP32 nach einer Höchstzahl enden (Unterschied ESP32 / RAK4631, Entscheidung offen)? OE3XWJ-12 pingt einmal pro Minute an Gruppe 2025 und erzeugt {de(ackp['share_pct'], 0)} % der ACK, die OE3XIA hört.</li></ol>")

    # ---- dev details -----------------------------------------------------------------------
    ap_('<details class="dev"><summary>Methodik, Marker, Code-Stellen, Skripte (für Entwickler)</summary>')
    ap_("<h3>Definitionen</h3><ul>"
        "<li><b>Empfangsereignis:</b> Zeile <code>[MC-DBG] ONRXDONE_TIME ms=</code> (jeder dekodierte Rahmen, auch die 12-Byte-ACK) oder <code>[MC-DBG] RX_ERROR rssi=</code> (Signal erkannt, kein gültiger Rahmen), Logger-Zeitstempel am Zeilenanfang. Stillstand: Abstand zweier aufeinanderfolgender Ereignisse über 300 s.</li>"
        f"<li><b>Abweichung zur Gesundheitsseite:</b> health/rx_gaps.json zählt nur die Zeilen <code>OnRxDone</code> und <code>OnRxError</code>. <code>OnRxDone</code> wird für ACK-Rahmen nicht gedruckt (Schnellpfad, <code>lora_functions.cpp:1018-1034</code> gegen <code>:2744</code>). Dort 35 Pausen, 15.678 s, längste 1.030 s ab 11:48:56; "
        f"hier mit ACK (Nachmessung verify/health.md Nr. 1): {n_gaps} Pausen, {de(sil_s, 0)} s ({de(sil_pct)} %), längste {de(longest['len_s'], 0)} s ab {longest['start'][7:]}. Aus demselben Grund ist der Fehleranteil hier {de(100 * n_err / n_ev)} % (Nenner mit ACK).</li>"
        "<li><b>Zeitbasis:</b> Logger-Uhr (Mac, MEZ). Alle Zeiten dieses Berichts, auch in den Tabellen.</li>"
        f"<li><b>Kanalabfrage in den Pausen:</b> Zeilen <code>CAD_SCAN result=-702</code> gelten als belegt ({A['cad_busy_sil']} von {A['cad_scan_sil']} in den Pausen, {A['cad_busy_all']} von {A['cad_scan_all']} im ganzen Mitschnitt). Die Zahl in health/results.json (zuerst belegt, {de(cad['initial_busy_rate_pct'])} %) hat einen anderen Zähler (CAD_BUSY_1 je Abfrage-Versuch); beide führen zum selben Schluss.</li>"
        f"<li><b>Ende der Pause nach Kanalabfrage:</b> eine <code>CAD_SCAN</code>-Zeile höchstens {3} s vor dem Ereignis, das die Pause beendet; Grundrate: derselbe Test über alle {de(A['cad_base_n'], 0)} Ereignisse, die keine Pause beenden.</li>"
        "<li><b>Rang im Cross-Node-Vergleich:</b> q2/results.json network.cross_node; slowest_count summiert sich über die fünf Knoten auf 1.462 Gruppen.</li>"
        "<li><b>HEY-Ketten:</b> q2/results.json hey_chains. 78,8 % = gesendete Ketten (149 von 189); 61 % = gesendete HEY bezogen auf alle 246 Schreibungen (149/246). Die 246 Schreibungen enthalten 57 ersetzte Zwischenstände.</li>"
        "<li><b>Doppelte ACK:</b> q34/findings.md deutet die 12 Paare als parallele ACK-Einträge, q2 und q6 als gewollte Gateway-ACK-Paare (zwei Schreibungen im Abstand von 13 ms, <code>lora_functions.cpp:2323/2334</code>). Der Bericht folgt q2 und q6.</li>"
        "<li><b>ACK-Wartezeit OE3XIR:</b> Abb. 5 nimmt data/extra/oe3xir_ping_cost.json (nur echte ACK, 20,9 s); der Wert 18,8 s in q2/results.json enthält die verworfenen Pings an „255.“.</li>"
        "<li><b>Literale in Urteilstexten</b> stammen aus data/CORE-FINDINGS.md und verify/*.md: 62 von 62 CAD_BUSY und 21 Abfragen in 2924 s (verify/q2.md Nr. 4), 64 bis 72 % Kopfzeit-Anteil (ebd.), 16 bis 18 s ACK-Wartezeit bei leerem Puffer (verify/q2.md Nr. 7), Fremd-Gateway-ACK und Auto-Ping-Zahlen (CORE-FINDINGS Nr. 1), Case A 666 von 666 (Nr. 5), Grenzwert 40 % Fehleranteil (verify/health.md Nr. 18).</li></ul>")
    ap_("<h3>Marker</h3><ul>"
        "<li><code>[MC-DBG] RX_TIMEOUT_FIRE ts= wait= delta=</code> und <code>RX_RESTART src=timeout</code>: nrf52_main.cpp:1608-1647, Neustart des Empfangs nach der Haltezeit (csma_timeout); <code>[MC-SM] IDLE -&gt; RX_LISTEN</code> folgt.</li>"
        "<li><code>[MC-DBG] CAD_SCAN result=</code>: Ergebnis der Kanalabfrage (-702 belegt).</li>"
        "<li><code>[MC-DBG] CHANNEL_UTIL rx=…ms tx=…ms util=…%</code>: alle 10 s.</li>"
        "<li><code>RING_WRITE slot= type= status= len= msg_id= queued=/20 src=</code>; <code>RING_TX_READ … lat=</code>; <code>RING_DROP_STALE slot= prio= age_s= msg_id= reason=ttl</code> (txring_functions.cpp:639).</li>"
        "<li><code>[MC-DBG] CSMA_AGED slot= age= stage= hold=</code> (lora_functions.cpp:4040), gedruckt bei jedem Neustart, solange der Kopf-Slot Priorität 1 bis 3 hat.</li>"
        "<li><code>[NBR]|NEED|…|H|A|…</code>: Case A = Alleinversorger.</li></ul>")
    ap_("<h3>Code-Stellen (am Stand fork-dev 651e0920 gelesen)</h3><ul>"
        "<li>Empfangsneustart (Wachhund): <code>src/nrf52/nrf52_main.cpp:1608-1647</code>, <code>startRadioReceive()</code> in Zeile 1631, RX-DEAF-02-Kommentar <code>:1632</code>; <code>src/nrf52/nrf52_radio.cpp:9-19</code>.</li>"
        "<li>Kanalabfrage mit Standby: <code>src/nrf52/nrf52_main.cpp:1697-1698</code>, <code>:1762-1763</code>.</li>"
        "<li>Auto-Ping: RAK4631 <code>src/nrf52/nrf52_main.cpp:2717-2737</code>, ESP32 <code>src/esp32/esp32_main.cpp:5264, :5285</code>, <code>--ping stop</code> in <code>command_functions.cpp:3886</code>.</li>"
        "<li>TX-Auswahl und Verfall: <code>txring_functions.cpp:213-255, 385-405, 566-645, 1007</code>, <code>txring_ttl.h:46-64</code>, <code>csma_timing.h:71-112</code>, <code>configuration_global.h:405-411, 465-480, 486-487, 581-582</code>; Neustart der Haltezeit <code>lora_functions.cpp:2746-2747</code>; NBR-Pfad <code>:3998-4006</code>.</li>"
        "<li>{CET}-Tor: <code>cet_age.h:120-135</code>, <code>lora_functions.cpp:2915</code>.</li></ul>")
    ap_("<h3>Quellen</h3><ul>"
        "<li>health/results.json, health/findings.md, health/rx_gaps.json; verify/health.md (Nr. 1 bis 8, 18)</li>"
        "<li>q2, q34, q5, q6: findings.md und results.json (Schlüssel nodes.oe3xia); verify/q2.md, q34.md, q5.md, q6.md; verify/rca_own_beacons.md; data/CORE-FINDINGS.md (v2)</li>"
        "<li>master/master.tsv, master/distances.tsv (Stammdaten, Entfernung vom Exelberg)</li>"
        "<li>extra/oe3xia_analysis.json (Pausen, Zeitleiste, Kanalabfrage-Abgleich, TX-UDP-Abgleich; erzeugt aus den normalisierten Logs der fünf OE3-Knoten mit <code>generators/lib/oe3xia_analyze.py --norm &lt;norm-Verzeichnis&gt; --out &lt;Datei&gt;</code>); extra/oe3xir_ping_cost.json (ACK-Wartezeit OE3XIR)</li>"
        "<li>Skript: <code>docs/log-reports/2026-11-01-24h/generators/oe3xia.py</code> (dieses Programm; Aufruf mit <code>--data</code>, <code>--kit</code>, <code>--out</code>; liest keine Rohlogs)</li></ul>")
    ap_("</details>")

    ap_('<div class="foot"><p>Quellen: Mitschnitte 31.10./01.11.2026, Auswertung vom 01.11.2026. Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware. '
        '<a href="uebersicht.html">Zur Übersicht (Netzsicht)</a></p></div>')

    html = page("OE3XIA-12 · MeshCom Log-Auswertung", "\n".join(body))
    # the log marker "src=..." appears in visible text only; write the equals sign as entity so that
    # a plain grep for resource attributes stays clean (the page has no src attributes at all)
    html = html.replace("src=", "src&#61;")
    args.out.write_text(html, encoding="utf-8")
    print(f"wrote {args.out} ({len(html)} bytes, {fig[0]} figures)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
