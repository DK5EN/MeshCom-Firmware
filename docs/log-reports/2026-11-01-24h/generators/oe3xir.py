"""Generator: per-node report OE3XIR-12 (MeshCom 24-h log analysis, 31.10.2026).

Usage:
    python3 -I oe3xir.py --data <dir> --kit <dir> --out <file>

--data  the run's data directory (health/ q2/ q34/ q5/ q6/ master/ extra/, CORE-FINDINGS.md)
--kit   directory holding reportkit.py
--out   output HTML file

Every number comes from --data. Counts that only the raw log holds (dropped pings, CAD gate entries
per ring read, TX_FRAME / TX_START, ACK wait of real ACKs, RX_ERROR share incl. ACK receptions,
dedup window, {CET} wait) are measured once by generators/lib/oe3xir_ping_cost.py (needs --norm)
and read here from data/extra/oe3xir_ping_cost.json. Judgement text is written in this file.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path
from typing import Any

ACK_TOA_S = 0.313  # model value: 12-byte ACK frame at SF11/BW250/CR4:6 (health findings)


def de(x: float, nd: int = 1) -> str:
    """German number format: decimal comma, dot as thousands separator."""
    s = f"{x:,.{nd}f}"
    return s.replace(",", "_").replace(".", ",").replace("_", ".")


def pct(a: float, b: float, nd: int = 1) -> str:
    return de(100.0 * a / b, nd) + " %"


def jload(base: Path, rel: str) -> dict[str, Any]:
    with (base / rel).open(encoding="utf-8") as fh:
        return json.load(fh)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--kit", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    base = Path(a.data)
    sys.path.insert(0, a.kit)
    from reportkit import bars, chip, esc, explain, hist, page, table  # noqa: PLC0415

    H = jload(base, "health/results.json")["nodes"]["oe3xir"]
    Q2 = jload(base, "q2/results.json")["nodes"]["oe3xir"]
    Q34 = jload(base, "q34/results.json")["nodes"]["oe3xir"]
    Q5 = jload(base, "q5/results.json")["nodes"]["oe3xir"]
    Q6 = jload(base, "q6/results.json")["nodes"]["oe3xir"]

    X = jload(base, "extra/oe3xir_ping_cost.json")
    xc = X["counts"]
    ping_gate = X["ping_gate_entries_per_read"]
    tx_gate = X["tx_gate_entries_per_read"]
    ackw = X["ack_wait_s"]
    n_ping = xc["ping_errors"]

    master: dict[str, dict[str, str]] = {}
    with (base / "master/master.tsv").open(encoding="utf-8") as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            master[row["call"]] = row

    # ---------------- derived numbers ----------------
    otx = H["own_tx"]
    tx_n = otx["tx_lines"]
    airtime = otx["airtime_s"]
    types = otx["type_split"]
    fate = Q2["fate"]
    occ = Q2["occupancy"]
    strong = H["strong_signals"]
    tw = Q6["tx_wait"]["wait"]
    csma = Q34["csma"]
    cf = Q34["cf"]
    gcet = Q5
    traffic = H["traffic"]
    heard, acks = traffic["heard_events"], traffic["ack_events"]
    ping_drop = fate["counts"]["tx_noair_decodefail"]
    ping_wr_pct = pct(ping_drop, fate["total_writes"])
    gate_share = de(X["ping_share_of_gate_entries_pct"]) + " %"
    ping_lat_p50 = de(X["ping_lat_ms_p50"] / 1000)
    ping_lat_max = de(X["ping_lat_ms_max"] / 1000)
    n_rxerr = H["rf"]["rx_error_n"]
    rxerr_share = de(X["rx_error_share_incl_ack_pct"]) + " %"
    cet_age_max = Q5["tx_age_s_logger_based"]["max"]
    cet_wait_max = X["cet_tx_wait_ms_max"] / 1000
    cet_margin = 150 - cet_age_max
    err_hours = H["strong_signals"]["strong_err_per_hour"]
    full_hours = [v for k, v in sorted(err_hours.items())][1:-1]
    err_min, err_max = min(full_hours), max(full_hours)
    n_strong = H["strong_signals"]["err_strong"]
    ttot = sum(types.values())
    wx = fate["by_src"]["user_wx"]
    wx_air, wx_lost = wx["tx_air"], wx["stale_ttl"]
    wx_n = wx_air + wx_lost
    t_ack_relay = types["A"]
    opp = Q6["opportunity_airtime"]["opportunity_tx_by_type"]
    opp_ack = opp["A"]
    opp_other = sum(v for k, v in opp.items() if k != "A")
    opp_air = Q6["opportunity_airtime"]["opportunity_air_s_total"]
    opp_pct = de(Q6["opportunity_airtime"]["opportunity_share_of_node_tx_air_pct"]) + " %"
    need_a = Q6["totals"]["need_A"]
    ack_air_s = t_ack_relay * ACK_TOA_S

    def cap(text: str) -> str:
        """Caption with a placeholder; numbers are assigned in page order at the end."""
        return f"Abb. @@{text[:24]}@@: {text}"

    # ---------------- SVG helpers (classes only, umlauts as ae/oe/ue) ----------------
    def box(x: int, y: int, w: int, h: int, lines: list[str], cls: str = "d-box",
            anim: str = "") -> str:
        out = [f'<rect class="{cls} {anim}" x="{x}" y="{y}" width="{w}" height="{h}" rx="8"/>']
        n = len(lines)
        y0 = y + h / 2 - (n - 1) * 8 + 4
        for i, t in enumerate(lines):
            c = "d-txt" if i == 0 else "d-txt-s"
            out.append(f'<text class="{c}" x="{x + w / 2}" y="{y0 + i * 16:.0f}" '
                       f'text-anchor="middle">{esc(t)}</text>')
        return "".join(out)

    def arrow(x1: int, y1: int, x2: int, y2: int, cls: str = "d-line", anim: str = "") -> str:
        head = f"M{x2 - 7},{y2 - 5} L{x2},{y2} L{x2 - 7},{y2 + 5}" if x2 >= x1 else (
            f"M{x2 + 7},{y2 - 5} L{x2},{y2} L{x2 + 7},{y2 + 5}")
        return (f'<line class="{cls} {anim}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>'
                f'<path class="{cls}" d="{head}"/>')

    def label(x: int, y: int, t: str, cls: str = "d-txt-s", anchor: str = "middle") -> str:
        return f'<text class="{cls}" x="{x}" y="{y}" text-anchor="{anchor}">{esc(t)}</text>'

    # ---------------- Explainer drawings ----------------
    nb_rssi = [n["rssi_median"] for n in H["neighbours"]["top"]]
    e_ping = explain(
        label(320, 22, f"Einmal pro Minute, {de(n_ping, 0)} mal in 11,5 h", "d-txt")
        + box(10, 50, 130, 64, ["Auto-Ping", "Ziel in --pingcall", "steht auf 255."], "d-box d-act")
        + arrow(140, 82, 170, 82)
        + box(170, 50, 130, 64, ["Sendepuffer", "Prio 1, wartet", f"median {ping_lat_p50} s"],
              "d-box d-acc")
        + arrow(300, 82, 330, 82)
        + box(330, 50, 130, 64, ["Dekoder prueft", "Ziel '255.'", "ungueltig"], "d-box d-act",
              "anim-pulse")
        + arrow(460, 82, 490, 82)
        + box(490, 50, 140, 64, ["Slot wird frei", "ohne Meldung", "an den Client"], "d-box d-neu")
        + '<line class="d-line d-act-s" x1="395" y1="114" x2="395" y2="150" '
          'stroke-dasharray="4 4"/>'
        + box(330, 150, 130, 44, ["Funk: nichts", "0 Sendungen"], "d-box d-neu")
        + '<circle class="d-acc-f anim-move" cx="170" cy="82" r="6" style="--dx:160px;--dy:0px"/>'
        + label(160, 170, "vorher: Kanalabfragen (CAD)", "d-txt-s", "middle")
        + label(160, 188, "und Wartezeit verbraucht", "d-txt-s", "middle"),
        cap("Der Auto-Ping an '255.' wird eingereiht, gewinnt die Auswahl und stirbt im Dekoder"),
        height=210)

    e_pinglimit = explain(
        label(320, 20, "Auto-Ping: Unterschied ESP32 / RAK4631, Entscheidung offen", "d-txt")
        + box(10, 36, 190, 62, ["ESP32", "Zaehler node_pingcount", "zaehlt bis 0"], "d-box d-ok")
        + arrow(200, 67, 225, 67)
        + box(225, 36, 190, 62, ["Nach node_pingmax", "Pings: Stopp", "von selbst"], "d-box d-ok")
        + arrow(415, 67, 440, 67)
        + box(440, 36, 190, 62, ["--ping stop", "setzt Zaehler 0", "Ping stoppt"], "d-box d-ok")
        + box(10, 118, 190, 62, ["RAK4631 (OE3XIR-12)", "kein Zaehler", "in der Schleife"], "d-box d-act")
        + arrow(200, 149, 225, 149, "d-line d-act-s", "anim-flow")
        + box(225, 118, 190, 62, ["Ping alle 60 s,", "solange --pingcall", "gesetzt ist"], "d-box d-act",
              "anim-pulse")
        + arrow(415, 149, 440, 149)
        + box(440, 118, 190, 62, ["--ping stop: ohne", "Wirkung. Nur", "--pingcall none stoppt"], "d-box d-watch")
        + label(320, 205, "ESP32: esp32_main.cpp:5264, :5285   RAK4631: nrf52_main.cpp:2717-2737", "d-txt-s"),
        cap("Auf dem ESP32 begrenzt ein Zaehler den Auto-Ping, auf dem RAK4631 gibt es keinen"),
        height=222)

    e_intf = explain(
        box(10, 20, 170, 70, ["Starke lokale Quelle", "-50 bis -56 dBm", "Art unbekannt"],
            "d-box d-act", "anim-pulse")
        + arrow(180, 55, 440, 55, "d-line d-act-s", "anim-flow")
        + box(440, 20, 190, 70, ["OE3XIR-12 Empfaenger", "Fehlempfang (RX_ERROR)",
                                 f"ueber -60 dBm: {n_strong}"], "d-box d-watch")
        + box(10, 130, 170, 60, ["Nachbarn", f"{de(min(nb_rssi), 0)} bis {de(max(nb_rssi), 0)} dBm",
                                 "(Median je Nachbar)"], "d-box d-ok")
        + arrow(180, 160, 440, 160, "d-line d-ok-s")
        + box(440, 130, 190, 60, ["Normale Empfaenge", f"{de(heard, 0)} Frames dekodiert"], "d-box d-ok")
        + label(310, 45, f"jede volle Stunde {err_min} bis {err_max} mal", "d-txt-s")
        + label(310, 150, "schwach, aber lesbar", "d-txt-s"),
        cap("Eine starke lokale Quelle feuert in jeder Stunde in den Empfaenger, die Nachbarn sind viel leiser"),
        height=205)

    e_starve = explain(
        label(320, 20, "Eigene Wetter-Position (Prio 4) gegen Relay-Position (Prio 4) an der Spitze des Puffers",
              "d-txt-s")
        + box(10, 40, 150, 56, ["Eigenes Wetter", "an der Spitze"], "d-box d-act")
        + arrow(160, 68, 200, 68)
        + box(200, 40, 250, 56, ["Hold 5,5 s, bei jedem", "Empfang neu gestartet"], "d-box d-watch",
              "anim-pulse")
        + arrow(450, 68, 490, 68)
        + box(490, 40, 140, 56, ["TTL 120 s:", "verworfen"], "d-box d-act", "anim-drop")
        + box(10, 116, 150, 56, ["Relay-Position", "an der Spitze"], "d-box d-acc")
        + arrow(160, 144, 200, 144)
        + box(200, 116, 250, 56, ["Fall A: nach 8 s nur", "150-220 ms Hold"], "d-box d-ok")
        + arrow(450, 144, 490, 144)
        + box(490, 116, 140, 56, ["gesendet", "(Funk)"], "d-box d-ok")
        + label(320, 196, "Alters-Boden gilt nur fuer Prio 1-3 und nicht fuer eigene Baken", "d-txt-s"),
        cap("Eigene Baken haben an der Spitze keinen Ausweg, Relays gleicher Prioritaet schon"),
        height=212)

    cet_x0, cet_w = 40, 528
    cet_bar = cet_w * cet_age_max / 150
    e_cet = explain(
        label(320, 20, "Zeitsignal {CET} im Sendepuffer, Prio 3, hinter Prio-1-Eintraegen (11 ACKs, 3 Pings)",
              "d-txt-s")
        + f'<rect class="d-box d-acc" x="{cet_x0}" y="45" width="{cet_bar:.1f}" height="26" rx="4"/>'
        + f'<line class="d-line d-act-s anim-pulse" x1="{cet_x0 + cet_w + 4}" y1="35" '
          f'x2="{cet_x0 + cet_w + 4}" y2="85"/>'
        + label(cet_x0, 100, "0 s", "d-txt-s", "start")
        + label(cet_x0 + cet_w, 100, f"Alter beim Senden {de(cet_age_max)} s", "d-txt", "end")
        + label(cet_x0 + cet_w + 4, 124, "Grenze 150 s", "d-txt", "end")
        + label(320, 152, f"Reserve {de(cet_margin)} s. Wartezeit im Puffer allein: "
                          f"{de(cet_wait_max)} s", "d-txt-s")
        + label(320, 172, "Ueber 150 s wird das Zeitsignal verworfen statt gesendet", "d-txt-s"),
        cap(f"Das aelteste eigene Zeitsignal war beim Senden {de(cet_age_max)} s alt, "
            f"{de(cet_margin)} s unter der Grenze"),
        height=185)

    e_veto = explain(
        label(320, 20, "Eine fremde Kopie wurde gehoert, waehrend das eigene Relay noch wartete", "d-txt-s")
        + box(10, 40, 150, 60, ["ACK-Relays", f"{de(opp_ack, 0)}"
                                f" von {de(t_ack_relay, 0)}", "gehoert, gesendet"], "d-box d-act")
        + arrow(160, 70, 230, 70)
        + box(230, 40, 190, 60, ["kein Cancel-Pfad", "fuer ACK-Frames", "(Code gelesen)"], "d-box d-neu")
        + box(10, 120, 150, 60, ["Andere Relays", f"{de(opp_other, 0)} gehoert,", "gesendet"], "d-box d-watch")
        + arrow(160, 150, 230, 150)
        + box(230, 120, 190, 60, ["Fall A: 'ich bin", "Alleinversorger'", f"in 100 % von {de(need_a, 0)}"],
              "d-box d-watch")
        + arrow(420, 70, 470, 110)
        + arrow(420, 150, 470, 120)
        + box(470, 90, 160, 60, ["gesendet trotzdem", f"{de(opp_air, 0)} s Sendezeit",
                                 "(obere Grenze)"], "d-box d-act"),
        cap("Kein Relay dieses Knotens wird je zurueckgezogen: ACKs haben keinen Pfad, Texte sehen sich als einzigen Versorger"),
        height=200)

    e_clock = explain(
        box(10, 30, 190, 60, ["Logger-Uhr", "06:47:35", "(MEZ, UTC+1)"], "d-box d-ok")
        + box(225, 30, 190, 60, ["Knoten-Uhr", "07:47:34", "([LOG]-Zeile)"], "d-box d-act", "anim-pulse")
        + box(440, 30, 190, 60, ["Zeitsignal-Alter", "rechnet in UTC", "bleibt korrekt"], "d-box d-ok")
        + arrow(200, 60, 225, 60)
        + label(215, 120, "+3599 s", "d-txt")
        + label(320, 150, "feste Verschiebung ueber die ganze Aufnahme (UTC+2 statt UTC+1)", "d-txt-s")
        + label(320, 170, "betrifft nur Anzeige, LOG-Zeiten und mheard", "d-txt-s"),
        cap("Die Knotenuhr laeuft eine Stunde vor, das Zeitsignal-Gate ist davon nicht betroffen"),
        height=185)

    e_ackflood = explain(
        box(5, 40, 135, 64, ["OE3XWJ-12", "{ping} an 2025", "1 pro Minute"], "d-box d-acc")
        + arrow(140, 72, 170, 72, "d-line d-acc-s", "anim-flow")
        + box(170, 40, 135, 64, ["Fremde Gateways", "senden die ACKs", "(nicht unsere)"], "d-box d-neu")
        + arrow(305, 72, 335, 72, "d-line d-watch-s", "anim-flow")
        + box(335, 40, 135, 64, ["ACK-Frames", f"{de(xc['ack_frames_f5ce7'], 0)} von {de(xc['ack_frames'], 0)}",
                                 f"({pct(xc['ack_frames_f5ce7'], xc['ack_frames'])})"], "d-box d-watch")
        + arrow(470, 72, 500, 72, "d-line d-watch-s", "anim-flow")
        + box(500, 40, 135, 64, ["OE3XIR-12 relayt", f"{de(t_ack_relay, 0)} ACKs",
                                 f"{pct(t_ack_relay, ttot, 1)} der Sendungen"], "d-box d-act")
        + label(320, 140, "fast alle ACKs gehoeren zu den Pings eines einzigen Absenders", "d-txt-s")
        + label(320, 160, f"Sendezeit der ACK-Relays: rund {de(ack_air_s, 0)} s von {de(airtime, 0)} s "
                          f"({pct(ack_air_s, airtime)}, gerechnet)", "d-txt-s")
        + label(320, 180, "Vermutung: die ACK-gebenden Gateways laufen mit anderer Firmware", "d-txt-s"),
        cap("Der Minuten-Ping von OE3XWJ-12 erzeugt fast alle ACKs, die dieser Knoten weiterleitet"),
        height=200)

    e_hold = explain(
        label(320, 20, "Jeder empfangene Frame startet die Wartezeit (Hold) vor dem Senden neu", "d-txt-s")
        + '<line class="d-line" x1="20" y1="110" x2="620" y2="110"/>'
        + '<rect class="d-ok" x="40" y="70" width="3" height="40"/>'
        + '<rect class="d-ok" x="150" y="70" width="3" height="40"/>'
        + '<rect class="d-ok" x="260" y="70" width="3" height="40"/>'
        + '<rect class="d-ok" x="330" y="70" width="3" height="40"/>'
        + '<rect class="d-watch anim-pulse" x="43" y="95" width="105" height="14" rx="4"/>'
        + '<rect class="d-watch" x="153" y="95" width="105" height="14" rx="4"/>'
        + '<rect class="d-watch" x="263" y="95" width="65" height="14" rx="4"/>'
        + '<rect class="d-acc" x="333" y="95" width="135" height="14" rx="4"/>'
        + box(480, 60, 140, 50, ["Senden", "erst bei Ruhe"], "d-box d-acc")
        + label(320, 140, "Empfang (senkrecht), Hold (waagerecht, schematisch)", "d-txt-s")
        + label(320, 165, f"Median ACK-Wartezeit {de(ackw['p50'])} s; {de(ackw['share_ge_20s_pct'], 0)} % der ACKs "
                          "warten 20 s oder laenger", "d-txt-s"),
        cap("Auf diesem belebten Kanal setzt der Empfang die Sendewartezeit immer wieder zurueck"),
        height=180)

    # ---------------- charts ----------------
    hours = sorted(strong["strong_err_per_hour"])
    chart_intf = hist(
        [(h.split()[1], strong["strong_err_per_hour"][h]) for h in hours],
        cap("Starke Fehlempfaenge (ueber -60 dBm) in jeder Stunde, Mess-Stunden 06 und 18 nur teilweise"),
        unit="", ylabel="Logger-Stunde 31.10.2026, Anzahl RX_ERROR ueber -60 dBm")

    chart_types = bars(
        [("ACK-Relays", types["A"]), ("HEY-Relays", types["@"]), ("Positionen", types["!"]),
         ("Texte und Zeitsignal", types[":"])],
        cap("Mehr als die Haelfte aller 2.598 Sendungen sind weitergeleitete ACKs"))

    chart_wait = bars(
        [("ACK-Relay (Prio 1)", tw["r/A/p1"]["wait_med_s"]),
         ("HEY-Relay (Prio 5)", tw["r/@/p5"]["wait_med_s"]),
         ("Server-Text/Zeit (Prio 3)", tw["g/:/p3"]["wait_med_s"]),
         ("Relay-Text (Prio 3)", tw["r/:/p3"]["wait_med_s"]),
         ("Relay-Position (Prio 4)", tw["r/!/p4"]["wait_med_s"]),
         ("Eigene Position (Prio 4)", tw["o/!/p4"]["wait_med_s"])],
        cap("Median der Wartezeit im Sendepuffer bis zur Sendung, in Sekunden"), unit=" s")

    # ---------------- tables ----------------
    type_rows = [
        ("ACK-Weiterleitungen", de(types["A"], 0), pct(types["A"], ttot, 1)),
        ("HEY-Weiterleitungen", de(types["@"], 0), pct(types["@"], ttot, 1)),
        (f"Positionen ({Q6['totals']['tx_r_!']} fremde, {Q6['totals']['tx_o_!']} eigene)", de(types["!"], 0), pct(types["!"], ttot, 1)),
        ("Texte (146 vom Server, 69 Relays)", de(types[":"], 0), pct(types[":"], ttot, 1)),
    ]
    t_types = table(["Sendeart", "Anzahl", "Anteil"], type_rows, num_cols=(1, 2))

    wr = fate["total_writes"]
    fc_counts = fate["counts"]
    fate_rows = [
        ("Gesendet (auf Funk)", de(fc_counts["tx_air"], 0), de(fate["pct"]["tx_air"], 2) + " %"),
        ("Aus dem Puffer genommen, aber vom Dekoder verworfen (kein Funk)",
         de(fc_counts["tx_noair_decodefail"], 0), de(fate["pct"]["tx_noair_decodefail"], 2) + " %"),
        ("Durch neuere Version ersetzt (Supersede)", de(fc_counts["superseded"], 0),
         de(fate["pct"]["superseded"], 2) + " %"),
        ("Zu alt verworfen (TTL)", de(fc_counts["stale_ttl"], 0), de(fate["pct"]["stale_ttl"], 2) + " %"),
        ("Bei Logende noch im Puffer", de(fc_counts["pending_end"], 0),
         de(fate["pct"]["pending_end"], 2) + " %"),
        ("Summe", de(wr, 0), de(fate["sum_pct"], 1) + " %"),
    ]
    t_fate = table(["Schicksal der Puffer-Einträge", "Anzahl", "Anteil"], fate_rows, num_cols=(1, 2))

    cad_rows = [("0 (Kanal sofort frei)", "41,8 %"), ("1", "21,7 %"), ("2", "12,7 %"),
                ("3 oder mehr", "23,8 %"), ("Summe", "100,0 %")]
    t_cad = table(["Kanalabfragen (CAD) vor einer Sendung", "Anteil der Sendungen"], cad_rows,
                  num_cols=(1,))

    ack_aged = csma["probe_p1_aged"]
    ack_fresh = csma["probe_p1_fresh"]
    t_csma = table(
        ["ACK-Sendungen (Prio 1)", f"mit Alters-Boden (n={ack_aged['n']})",
         f"ohne (n={ack_fresh['n']})"],
        [("Kanalabfragen je Sendung (Mittel)", de(ack_aged["cad_mean"], 2), de(ack_fresh["cad_mean"], 2)),
         ("RX_ERROR in den 3 s nach der eigenen Sendung", de(ack_aged["pct_rxerr_within_3s_after_tx"]) + " %",
          de(ack_fresh["pct_rxerr_within_3s_after_tx"]) + " %"),
         ("Echo eines Nachbarn binnen 60 s", de(ack_aged["pct_echo_within_60s"]) + " %",
          de(ack_fresh["pct_echo_within_60s"]) + " %")],
        num_cols=(1, 2))

    # neighbour table (top 25 direct), joined with master data
    def nb_row(n: dict[str, Any]) -> list[str]:
        call = n["call"]
        m = master.get(call)
        hw = (m["hardware"] if m else None) or n.get("hw") or "unbekannt"
        fw = m["firmware"] if m else None
        if not fw:
            fl = n.get("fw_log")
            fw = ("4." + fl.replace(":", "") + " (Log)") if fl else "unbekannt"
        if m:
            where = m["description"] or f"{de(float(m['lat']), 4)} N, {de(float(m['lon']), 4)} O"
            alt = f"{m['altitudeM']} m"
        else:
            where, alt = "keine Stammdaten", "unbekannt"
        rs = f"{de(n['rssi_median'], 0)} ({de(n['rssi_p10'], 0)}..{de(n['rssi_p90'], 0)}) dBm / {de(n['snr_median'], 0)} dB"
        return [call, hw, fw, where, alt, "n/a", str(n["n"]), rs]

    nb_head = ["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz", "Frames", "RSSI Median (p10..p90) / SNR"]
    top = H["neighbours"]["top"]
    t_nb = table(nb_head, [nb_row(n) for n in top[:10]], num_cols=(6,))
    t_nb_more = table(nb_head, [nb_row(n) for n in top[10:]], num_cols=(6,))

    mg = master.get("OE3GJC-12")
    gjc_desc = (f"OE3GJC-12 ({mg['hardware']}, {mg['firmware']}, {mg['description']}, "
                f"{de(float(mg['lat']), 4)} N {de(float(mg['lon']), 4)} O, {mg['altitudeM']} m, mcmap)"
                if mg else "OE3GJC-12")
    other_rows = [
        ["OE3XIR-12 (dieser Knoten)", "RAK4631", "4.40a (Log)", "keine Stammdaten", "unbekannt", "-"],
        ["OE3MIF-12", "unbekannt", "unbekannt", "keine Stammdaten", "unbekannt", "n/a"],
        ["OE3NRA-1", "unbekannt", "unbekannt", "keine Stammdaten", "unbekannt", "n/a"],
        ["OE3MTC-10", "unbekannt", "unbekannt", "keine Stammdaten", "unbekannt", "n/a"],
        ["OE3GJC-12", mg["hardware"] if mg else "unbekannt", mg["firmware"] if mg else "unbekannt",
         f"{mg['description']} ({de(float(mg['lat']), 4)} N, {de(float(mg['lon']), 4)} O)" if mg else "-",
         f"{mg['altitudeM']} m" if mg else "unbekannt", "n/a"],
        ["OE3XDC-12", "TTGO LoRa32 V2.1 1.6 (Log)", "4.34v (Log)", "keine Stammdaten", "unbekannt", "n/a"],
        ["OE3RFA-7", "T-Deck (Log)", "4.35h (Log)", "keine Stammdaten", "unbekannt", "n/a"],
        ["OE1XAR-33", "Server (Zeitsignal-Absender, kein Knoten)", "-", "-", "-", "-"],
    ]
    t_other = table(["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz"], other_rows, raw=False)

    # ---------------- text blocks ----------------
    def li(lead: str, text: str) -> str:
        return f"<li><strong>{lead}</strong> {text}</li>"

    def ul(items: list[str]) -> str:
        return "<ul>" + "".join(items) + "</ul>"

    span_h = Q2["span"]["hours"]
    verdicts = [
        ("Q0 Gesundheit und Funkumfeld", chip("watch"),
         "Knoten stabil (0 Neustarts, Heap flach, Puffer höchstens 11 von 20). Beobachten: starke lokale "
         f"Quelle, Art unbekannt ({n_strong} Fehlempfänge über -60 dBm), Knotenuhr +1 h."),
        ("Q2 TX-Puffer mit Slots", chip("act", "Handlungsbedarf (Entscheidung)"),
         f"Puffer arbeitet sauber (0 Überläufe), aber {ping_wr_pct} der Einträge sind der eigene Auto-Ping "
         f"an '255.', der ohne Funk verworfen wird ({n_ping} mal). Auf dem RAK4631 hat der Auto-Ping keine "
         "Zählgrenze (Unterschied zu ESP32, Entscheidung offen)."),
        ("Q3 Verlust durch Zeitverfall", chip("watch"),
         "Nur 8 Positionen verfielen: 5 eigene Wetter-Positionen (von 22) und 3 fremde. "
         "0 ACK, 0 Text, 0 Zeitsignal."),
        ("Q4 Gewinn durch Zeitverfall", chip("ok"),
         "Gewinn ist Frische, nicht Sendezeit: 8,7 s gespart (0,56 % der Sendezeit); Puffer bliebe auch "
         "ohne TTL bei höchstens 8 Einträgen."),
        ("Q5 Zeitsignal", chip("watch"),
         "Nichts Veraltetes gesendet (68 von 68 unter 150 s), aber das älteste eigene Signal "
         f"war beim Senden {de(cet_age_max)} s alt: {de(cet_margin)} s Reserve gegen 150 s."),
        ("Q6 Manipulationen", chip("act", "Handlungsbedarf (Design, netzweit)"),
         "Kein Relay wird je zurückgezogen (100 % Fall A, ACKs ohne Cancel-Pfad); "
         f"{opp_pct} der Sendezeit folgten auf eine schon gehörte Kopie (obere Grenze)."),
    ]
    t_index = table(["Frage", "Urteil", "Kurzantwort"], [list(v) for v in verdicts], raw=True)

    meta = ul([
        "<li><strong>Log-Datei:</strong> meshcom_oe3xir_2026-10-31_064704.log</li>",
        "<li><strong>Zeitraum:</strong> 31.10.2026 06:47:05 bis 18:16:30 Logger-Uhr (MEZ, UTC+1), "
        f"{de(span_h, 2)} h, also nur knapp die Hälfte der 25 h der anderen Knoten; Vergleiche laufen "
        "deshalb pro Stunde. Die Knotenuhr zeigt +1 h (3599 s), alle Zeiten hier sind Logger-Zeit.</li>",
        "<li><strong>Hardware:</strong> RAK4631 (nRF52840) laut eigenem HW-Feld im Log, mit Ethernet (Chip W5100S abgeleitet)</li>",
        "<li><strong>Firmware:</strong> 4.40a, Release-Stempel 20260929 (FLASH_VERSION, kein Baudatum); exakter Build unbekannt "
        "(kein Boot-Block im Log), Slot-Pool (TXP) nur aus Markern abgeleitet</li>",
        "<li><strong>Standort / Höhe:</strong> keine Stammdaten; Höhe unbekannt</li>",
        "<li><strong>Rolle:</strong> Internet-Gateway und Relay (1.377 Keepalives, 1.401 Uploads, "
        "146 Server-Einspeisungen)</li>",
        "<li><strong>LoRa-Parameter:</strong> SF11, BW250, CR4:6 (abgeleitet: Sendedauer passt zur "
        "Kanalauslastung; Frequenz und Leistung stehen nicht im Log)</li>",
        "<li><strong>Stand:</strong> 01.11.2026</li>",
    ])

    bluf = (
        '<div class="bluf"><p class="lead">OE3XIR-12 läuft stabil als Relay, aber '
        f'{ping_wr_pct} seiner Sendepuffer-Einträge sind der eigene Auto-Ping an das ungültige Ziel „255.“, '
        'der jede Minute ohne Funk im Dekoder stirbt, und eine starke lokale Quelle verursacht in jeder '
        'Stunde Fehlempfang.</p>'
        '<p>Die Ursache des Pings ist eine Einstellung im Knoten selbst (Ping-Ziel), die die Firmware '
        'weder beim Setzen noch beim Einreihen prüft. Dass er nie aufhört, liegt an einem Unterschied '
        'ESP32 / RAK4631: Der ESP32 zählt Auto-Pings und stoppt, der RAK4631 hat keine Zählgrenze, '
        '<code>--ping stop</code> wirkt dort nicht. Welches Verhalten gewollt ist, ist offen. Die '
        f'Zeitsignal-Reserve von {de(cet_margin)} s ist dünn, aber es ging nichts Veraltetes auf Funk.</p></div>')

    act = (
        '<div class="callout watch"><strong>Empfehlung an den Betreiber</strong>'
        + ul([
            li("Ping-Ziel löschen:", "Der Knoten pingt jede Minute das Ziel „255.“ an. "
               "<code>--pingcall none</code> löscht es und setzt <code>node_pingtime</code> auf 0; "
               "<code>--ping stop</code> hilft auf dem RAK4631 nicht. Alternativ ein gültiges Ziel eintragen. "
               f"Das spart {ping_wr_pct} der Puffer-Einträge und {gate_share} der Kanalabfragen vor der "
               "Entnahme."),
            li("Entscheidung (Firmware):", "Soll der Auto-Ping auf dem RAK4631 wie auf dem ESP32 nach "
               "<code>node_pingmax</code> Pings enden, oder dauerhaft laufen? Der Code ist im Upstream-Stand "
               "147812e0 derselbe."),
            li("Quelle suchen:", "Am Standort liegt eine starke lokale Quelle bei -50 bis -56 dBm im Band, "
               "Art unbekannt. Kein anderer der sechs Knoten hat auch nur einen solchen Fehlempfang."),
            li("Uhr prüfen:", "Die Knotenuhr läuft auf UTC+2, im Winter wäre UTC+1 richtig. Das Zeitsignal-Gate "
               "rechnet in UTC und ist nicht betroffen."),
        ]) + "</div>")

    # ---- Section Q0 ----
    s0 = (
        "<h2 id=\"q0\">Gesundheit und Funkumfeld</h2>"
        "<p><strong>Der Knoten ist gesund; auffällig sind nur das Funkumfeld und die Konfiguration.</strong> "
        "Alle Raten gelten pro Stunde der 11,5 h.</p>"
        + ul([
            li("Verkehr:", f"{de(heard, 0)} gehörte Frames plus {de(acks, 0)} ACK-Frames "
               f"({pct(acks, heard + acks)} aller Empfänge); 169 "
               "verschiedene Absender (ohne Zeitserver), 32 direkte Nachbarn. Empfänge werden vor der Dublettenprüfung gezählt, "
               "sind also keine akzeptierten Pakete."),
            li("Eigene Sendungen:", f"{de(tx_n, 0)} ({de(otx['tx_per_hour'])} pro Stunde), {de(airtime)} s Sendezeit "
               "(3,8 % Auslastung durch eigenes Senden). Davon 2.435 Relays, 146 Server-Einspeisungen, 17 eigene "
               "Wetter-Positionen und keine eigene HEY-Bake (Grund nicht im Log)."),
            li("Kanal:", "Auslastung im Mittel 20,8 %, Spitze 71 %. Fehlempfang (RX_ERROR: Präambel gesehen, "
               "kein gültiger Frame) "
               f"{de(n_rxerr, 0)} mal = {de(H['rf']['rx_error_per_hour_mean'])} pro Stunde = {rxerr_share} aller "
               "Empfangsereignisse (Fehlempfang plus alle dekodierten Empfänge einschließlich ACKs); "
               "Median -102 dBm, SNR -4 dB, also rauschbegrenzt. Unter der Beobachten-Schwelle von 40 %."),
            li("Empfangsunterbrechung:", f"Keine. Längste Pause zwischen zwei dekodierten Empfängen "
               f"{de(X['rx_longest_silence_s'])} s, keine Lücke über 5 Minuten (anders als bei OE3XIA-12, dessen "
               "Empfänger zeitweise taub war, siehe Gesamtbild)."),
            li("Kanalabfrage (CAD = Horchen vor dem Senden):", "40,7 % der 13.261 Abfragen sehen zuerst "
               "„belegt“, 85,0 % davon bestätigen sich, 15,0 % sind Fehlalarm. 23,8 % der Sendungen brauchen "
               "3 oder mehr Abfragen."),
            li("Ältere Firmware ohne CAD:", "17 von 169 Absendern (10,1 %) laufen auf 4.35a-o oder älter, "
               "also ohne Horchen vor dem Senden; sie tragen 16,6 % der Empfangsereignisse und 17,1 % der Sendezeit."),
            li("Stabilität:", "0 Neustarts (Laufzeit 300 s bis 41.406 s monoton), 0 Abstürze, Heap konstant "
               "92.252 B (nRF52-Wert ist grob). Server-Heartbeat: 12 von 1.377 Antworten fehlten (0,87 %) in 11 Lücken; "
               "eine Lücke fällt auf das serverseitige Ereignis vom 31.10. 07:22:10, das alle fünf "
               "OE3-Gateways traf (laut Gesamtbild)."),
        ])
        + t_cad
        + "<h3>Starke lokale Quelle</h3>"
        f"<p><strong>{n_strong} Fehlempfänge liegen über -60 dBm, in jeder Stunde {err_min} bis {err_max} "
        "(volle Stunden).</strong> Kein anderer der sechs Knoten hat einen einzigen.</p>"
        + ul([
            li("Pegel:", "Die Werte häufen sich bei -50/-51 und -55/-56 dBm mit SNR -22 bis -13 dB; "
               "zusätzlich 15 dekodierte Frames mit RSSI über -60 dBm (SNR Median -6 dB)."),
            li("Deshalb sind RSSI-Maxima unten irreführend:", "Die höchsten RSSI-Werte direkter Nachbarn "
               "(-50 bis -61 dBm) stammen von diesen Stör-Ereignissen, nicht von Nachbarn; es gelten die Mediane."),
            li("Art der Quelle:", "Starke lokale Quelle, Art unbekannt. Ein Fehlempfang setzt eine erkannte "
               "Präambel voraus; die zwei festen Pegel sprechen für einen oder zwei feste Sender. Ein anderes "
               "LoRa-Netz oder ein unprotokollierter Knoten am Standort ist ebenso wahrscheinlich wie eine "
               "Nicht-LoRa-Quelle. Ein Echo der eigenen Sendung ist ausgeschlossen (kein Zeitbezug zur eigenen "
               "Sendung). Die nRF52 liefert keine Frequenzablage (ferr immer 0), das Log kann es nicht entscheiden."),
            li("Wirkung:", "Ob dadurch Nutzframes verloren gehen, ist aus dem Log nicht ableitbar."),
        ])
        + e_intf + chart_intf
        + "<h3>Knotenuhr +1 h</h3>"
        "<p><strong>Die Knotenuhr steht über die ganze Aufnahme auf UTC+2 (+3599 s gegen den Logger).</strong> "
        "Die Zeitsignal-Alter stimmen trotzdem (Gate rechnet in UTC, Median 0 s).</p>"
        + ul([
            li("Beleg:", "Logger 06:47:35, [LOG]-Zeile des Knotens 07:47:34. Die anderen vier OE3-Knoten liegen "
               "bei -1 s."),
            li("Ursache:", "Vermutung: fester UTC-Versatz 2 aus dem Sommer ohne Zeitzonenregel; "
               "Verdachtsstelle <code>src/clock.cpp:153-165</code> (tzApplyNow). Die Einstellung steht nicht im Log."),
        ])
        + e_clock
    )

    # ---- Section Q2 ----
    s2 = (
        "<h2 id=\"q2\">F2 TX-Puffer mit Slots</h2>"
        f"<p><strong>Die Puffer-Mechanik arbeitet sauber, {ping_wr_pct} der Einträge sind aber ein Fehler "
        "in der Konfiguration.</strong> Der Sendepuffer hat 20 Plätze (Slots).</p>"
        + ul([
            li("Füllstand:", f"Median {int(occ['queued']['p50'])}, p90 {int(occ['queued']['p90'])}, höchstens "
               f"{int(occ['queued']['max'])} Einträge (Statusmeldung alle 30 s); STAT-Fenster zeigen höchstens "
               "11 von 20. Mindestens 5 Einträge lagen 62,5 Minuten an, mindestens 10 eine Minute. Kein "
               "Anstieg über den Tag."),
            li("Überläufe:", "0 abgewiesene Einträge (RING_DROP_NEW / DROP_PRIO), 0 festhängende Slots, "
               "0 Wiederholungs-Rearms. Die Wiederholungsstufe (Retry) lief nie; ihr Verhalten ist mit diesen "
               "Daten ungeprüft."),
            li("Abrechnung:", "Jeder Eintrag hat ein Schicksal, 0 ungeklärt (Tabelle)."),
            li("Doppelsendungen:", "7 Id-Paare. 3 ACK-Paare sind das Gateway-Doppel by design "
               "(GWACK-01, zwei Schreibvorgänge je Id); 4 Texte wurden nach 19 bis 26 Minuten erneut "
               "gesendet (Vermutung: das Dublettengedächtnis mit 100 Einträgen hatte sie vergessen)."),
            li("Ersetzt:", "16 fremde HEY-Relays wurden durch eine neuere Version desselben Absenders ersetzt "
               "(Supersede); alle Ersatzketten gingen auf Funk."),
        ])
        + t_fate
        + "<h3>Der ungültige Auto-Ping</h3>"
        f"<p><strong>Der Knoten schreibt jede Minute einen {{ping}} an das Ziel „255.“ in den Puffer; der Dekoder "
        f"verwirft ihn, bevor er gesendet wird.</strong> {de(n_ping, 0)} mal, "
        f"von {X['ping_first'][11:19]} bis {X['ping_last'][11:19]} Logger-Zeit.</p>"
        + ul([
            li("Ursache (Code gelesen):", "Die Quelle „phone_msg“ im Log ist nur das Etikett der Ping-Funktion "
               "<code>sendPing()</code>, kein Handy. Der <em>eigene Auto-Ping des Knotens</em> (<code>node_pingcall</code>) "
               "sendet im Takt von <code>--pingtime</code> (60 s) an das in <code>--pingcall</code> gespeicherte Ziel. "
               "Dieses Ziel lautet „255.“."),
            li("Warum die Firmware es nicht merkt:", "Weder <code>--pingcall</code> "
               "(<code>src/command_functions.cpp:3806-3831</code>) noch <code>sendPing()</code> "
               "(<code>src/loop_functions.cpp:3820</code>) prüft das Ziel beim Setzen oder Einreihen; erst der Dekoder "
               "meldet „Destination-CallSign Error [255.]“ und gibt den Slot still frei. Die Meldung erscheint nur "
               "mit <code>--loradebug on</code> (hier eingeschaltet); eine Zeile „[LOG] TX“ gibt es nicht (0 von "
               f"{de(n_ping, 0)})."),
            li("Kosten:", f"Der Eintrag hat Prio 1 und gewinnt die Auswahl vor Relays. Pro Ping fallen im Mittel "
               f"{de(ping_gate['mean'], 2)} Kanalabfragen (CAD-Zyklen) an (echte Sendungen: "
               f"{de(tx_gate['mean'], 2)}), insgesamt {de(ping_gate['sum'], 0)} von "
               f"{de(X['gate_entries_total'], 0)} Kanalabfragen vor Puffer-Entnahme "
               f"({gate_share}). Jeder Ping endet mit einem CAD_FREE_NO_TX ({de(xc['cad_free_no_tx'], 0)} mal). "
               "Sendezeit kostet er nicht."),
            li("Auswertungsfalle:", f"Das Log schreibt für die verworfenen Pings trotzdem eine Zeile TX_FRAME "
               f"({de(xc['tx_frame'], 0)} TX_FRAME-Zeilen gegenüber {de(xc['tx_start'], 0)} echten Sendungen "
               "(TX_START)). Wer TX_FRAME zählt, hält sie für gesendet."),
            li("Wie das Ziel in den Knoten kam:", "Vermutung, nicht entscheidbar: Die Kommandozeile "
               "(<code>--pingcall</code>) prüft das Ziel im aktuellen Stand mit <code>checkRegexCall</code>; "
               "ein Import der Einstellungen oder ein älterer Build kann es ungeprüft gesetzt haben."),
        ])
        + e_ping
        + "<h3>Der Ping hört nicht auf: Unterschied ESP32 / RAK4631 (Entscheidung offen)</h3>"
        "<p><strong>Die Schleife des RAK4631 sendet den Auto-Ping dauerhaft, die des ESP32 zählt mit und "
        "hört auf.</strong> Das ist ein Plattformunterschied, kein Befund über einen Fehler; welches "
        "Verhalten gewollt ist, entscheidet der Betreiber der Firmware.</p>"
        + ul([
            li("ESP32:", "Sendet nur, solange <code>node_pingcount</code> größer 0 ist, und zählt pro Ping herunter "
               "(<code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code>). Nach <code>node_pingmax</code> "
               "Pings ist Schluss; <code>--ping stop</code> setzt den Zähler auf 0 "
               "(<code>src/command_functions.cpp:3886</code>)."),
            li("RAK4631 (nRF52):", "Die Schleife in <code>src/nrf52/nrf52_main.cpp:2717-2737</code> sendet alle "
               "<code>node_pingtime</code> Sekunden an <code>node_pingcall</code>, solange der Eintrag nicht leer ist, "
               "und liest den Zähler nie. <code>--ping stop</code> hat deshalb keine Wirkung; nur das Löschen von "
               "<code>--pingcall</code> (<code>none</code>) beendet den Ping. Upstream dev 147812e0 hat denselben Code."),
            li("Hier:", f"{de(n_ping, 0)} Pings in 11,5 h ohne Ende, an ein Ziel, das nie gesendet werden kann. "
               "OE3XWJ-12 (ebenfalls RAK4631) pingt aus demselben Grund dauerhaft die Gruppe 2025 "
               "(siehe F6)."),
        ])
        + e_pinglimit
        + "<h3>Wartezeiten im Puffer</h3>"
        + t_types
        + chart_types
        + chart_wait
        + f"<p><strong>ACK-Relays warten im Median {de(ackw['p50'])} s (p90 {de(ackw['p90'])} s, Maximum "
        f"{de(ackw['max'])} s bei 90 s TTL); {de(ackw['share_ge_20s_pct'], 0)} % warten 20 s oder länger "
        "(nur echte ACKs, die verworfenen Pings sind ausgeschlossen).</strong> "
        "Vermutung (Schluss aus Code und Verteilung, nicht direkt gemessen): Das liegt an der Wartezeit vor dem "
        "Senden (Hold), die jeder empfangene Frame neu startet, nicht an der Puffertiefe (Median 1). "
        "Ein DM-ACK über drei solche Hops könnte die 40-s-Wiederholungsleiter des Absenders "
        "überschreiten (Vermutung, nicht beobachtet; im Log kein DM-Retry).</p>"
        + e_hold
    )

    # ---- Section Q3 ----
    s3 = (
        "<h2 id=\"q3\">F3 Was verlieren wir durch den Zeitverfall</h2>"
        "<p><strong>8 Einträge verfielen (alle Positionen, TTL 120 s), nichts davon war ein Text, ACK oder "
        "Zeitsignal.</strong> Keiner der 8 war vorher gesendet worden.</p>"
        + ul([
            li("Eigene Wetter-Positionen:", f"{wx_n} geschrieben, {wx_air} gesendet ({pct(wx_air, wx_n, 0)}), "
               f"{wx_lost} verfielen nach 120 bis 121 s. "
               "Längste Funk-Pause der Wetterdaten: 2,0 h. Ob die Daten das Internet über UDP erreichten, "
               "lässt sich hier nicht prüfen (dieser Knoten schreibt keine TX-UDP-Zeilen)."),
            li("Mechanismus (an OE3XOC-12 untersucht):", "An der Spitze des Puffers bekommt eine eigene Bake "
               "(Prio 4/5) 5,5 s Wartezeit, die jeder empfangene Frame neu startet; der Alters-Boden gilt nur für "
               "Prio 1-3, und ein Relay gleicher Priorität nimmt im Fall A den Kurzweg (150-220 ms nach 8 s). "
               "Nachweis und Code-Stellen: <a href=\"verify/rca_own_beacons.md\">verify/rca_own_beacons.md</a>. "
               "Dass dieselbe Ursache die verfallenen Wetter-Positionen hier erklärt, ist Vermutung "
               f"(nur {wx_lost} Fälle, nicht einzeln geprüft); Designlücke, kein Programmierfehler."),
            li("Fremde Positionen (Relay):", "3 verfielen nach 120 bis 122 s: 1 war anderswo schon gesendet, "
               "1 mit niedrigerem Hop erneut gehört, bei 1 ist die anderweitige Weitergabe nicht belegt; "
               "2 von 3 wurden binnen 30 min vom Absender erneuert (Median 1.680 s)."),
            li("Nicht betroffen:", "0 Verfall bei HEY (Prio 5, 180 s), ACK (90 s), Relay-Text (300 s) und "
               "Zeitsignal ({CET}, 0 Verwürfe im Puffer)."),
            li("Reserve der Fristen:", "Gesendete Wartezeiten maximal: ACK 60,5 s von 90 s, Position 114,0 s von "
               "120 s, HEY 163,7 s von 180 s, Prio 3 (Relay-Text und Server-Text) 147,9 s von 300 s. Bei Positionen und HEY stehen "
               "5 bzw. 4 Sendungen über 80 % der Frist."),
        ])
        + e_starve
        + "<h3>Zeitsignal {CET}</h3>"
        "<p><strong>0 Zeitsignale wurden im Puffer verworfen; das Gate (150 s) musste nie eingreifen.</strong> "
        "{CET} ist das Zeitsignal des Servers OE1XAR-33; das Gate verwirft eine Weiterleitung, wenn sie älter "
        "als 150 s ist.</p>"
        "<h3>CSMA-Altersboden</h3>"
        "<p><strong>Der Altersboden greift hier dauerhaft, und es gibt keine Kollisionsspur; der Nutzen ist "
        "nur geschätzt.</strong> CSMA ist das Horchen-vor-Senden; der Boden verkürzt die Wartezeit eines "
        "Eintrags der Prio 1 bis 3, der älter als 20 s (Stufe 1) oder 60 s (Stufe 2) ist.</p>"
        + ul([
            li("Häufigkeit:", f"{de(csma['events'], 0)} Ereignisse in {de(csma['episodes'], 0)} Warte-Episoden "
               f"({csma['stage_episodes']['1']} Stufe 1, {csma['stage_episodes']['2']} Stufe 2), fast alle "
               f"ACKs ({csma['ep_prio']['1']} Prio 1, {csma['ep_prio']['3']} Prio 3). "
               f"{de(csma['tx_prio_le3_wait_ge20s_pct'])} % der Sendungen der Prio 1 bis 3 "
               f"({csma['tx_prio_le3_wait_ge20s']} von {csma['tx_prio_le3']}) warteten mindestens 20 s."),
            li("Nutzen:", "Nicht direkt messbar. Eine Wiedergabe auf den echten Empfangszeiten ergibt eine "
               "Ersparnis von median 0,6 bis 1,6 s je Episode (Spanne, weil der Versuchszähler nicht geloggt wird)."),
            li("Kollisionen:", "Kein Unterschied. Fehlempfang binnen 3 s nach der Sendung: 13,3 % (mit Boden) "
               "gegen 14,8 % (ohne), zufälliges 3-s-Fenster 31,9 %."),
            li("Doppelsendungen:", "3 ACK-Id-Paare im Abstand von 15 s; das zweite war immer gealtert. Ein "
               "ACK-Weiterleiter hat keinen Cancel-Pfad, der Boden erzeugt sie nicht."),
        ])
        + t_csma
    )

    # ---- Section Q4 ----
    s4 = (
        "<h2 id=\"q4\">F4 Was gewinnen wir durch den Zeitverfall</h2>"
        "<p><strong>Der Gewinn ist Frische, keine Sendezeit: 8,7 s Sendezeit gespart (gemessene Sendedauer der "
        "verworfenen Einträge), 0,56 % der eigenen Sendezeit.</strong> Das sind 0,8 s pro Stunde.</p>"
        + ul([
            li("Puffer (Modell):", "Ohne TTL würde der Puffer ebenfalls nicht überlaufen (Rechenmodell: höchstens "
               "8 Einträge, mittlere Tiefe 0,85 gegen 1,12). Frei gewordene Slot-Sekunden real: 964."),
            li("Alter der Sendungen ohne TTL (Modell):", "Die 8 Einträge würden mit einem Alter von median 134 s "
               "(p90 149 s, Maximum 175 s) gesendet."),
            li("Verzögerung frischer Frames ohne TTL (Modell):", f"{de(cf['fresh_delayed']['pct_delayed'])} % "
               f"({cf['fresh_delayed']['n_delayed_gt_0_5s']} von {de(cf['fresh_delayed']['n_compared'], 0)}) "
               f"frischer Frames der Prio 3 bis 5 würden später gesendet, median {de(cf['fresh_delayed']['delay_s_p50'], 0)} s, "
               f"p90 {de(cf['fresh_delayed']['delay_s_p90'], 0)} s, Maximum {de(cf['fresh_delayed']['delay_s_max'], 0)} s."),
            li("Empfindlichkeit (Modell):", "Kürzere Fristen verwürfen mehr: Prio 4 bei 90 s 13 statt 8 (bei 60 s 41), "
               "Prio 5 bei 150 s 3 und bei 120 s 11 statt 0. Ab 180 s (Prio 4) gäbe es 0 Verfälle."),
        ])
        + "<p><small>Alle Zahlen dieses Abschnitts mit „Modell“ sind obere Grenzen aus einer Wiedergabe erster Ordnung, "
        "keine Messung. Das Rechenmodell ist von erster Ordnung (keine zusätzlichen Leerlauf-Sendezeitpunkte); "
        "es reproduziert die echte Auswahl zu 100 % (1.176 Entscheidungen).</small></p>"
    )

    # ---- Section Q5 ----
    s5 = (
        "<h2 id=\"q5\">F5 Zeitsignal</h2>"
        f"<p><strong>Das eigene Zeitsignal war nie älter als 150 s, die Reserve beträgt aber nur {de(cet_margin)} s "
        f"({de(cet_age_max)} gegen 150 s).</strong> "
        "Das Signal {CET} sendet der Server OE1XAR-33 alle 607 s; es trägt UTC, obwohl es „CET“ heißt "
        "(serverseitig, Beobachten).</p>"
        + ul([
            li("Empfang über Internet (UDP):", f"{gcet['udp_got']} von {gcet['expected_in_window']} Telegrammen erreichten den "
               f"Knoten, Abstand median {de(gcet['udp_arrival_gap_s']['p50'])} s, Maximum "
               f"{de(gcet['udp_arrival_gap_s']['max'])} s. Auf Funk kamen {gcet['rf_valid_frames']} Kopien von "
               f"{gcet['rf_copies_per_id']['n']} Stempeln an."),
            li("Eigene Aussendung:", "68 Sendungen (Prio 3, vom Server eingespeist). Alter beim Senden median "
               f"22,2 s, p90 40,6 s, Maximum {de(cet_age_max)} s (Logger-Uhr); 0 über 150 s. Wartezeit im Puffer median 20,8 s, "
               f"p90 40,0 s, Maximum {de(cet_wait_max)} s (data/extra/oe3xir_ping_cost.json)."),
            li("Reserve:", f"Vermutung: Das Gate wertet beim Entnehmen aus (Alter laut Knoten {X['cet_tx_age_s_max_node']} s) "
               f"und erkennt das spätere CAD-Warten nicht mehr; am Ende lag das Signal {de(cet_margin)} s unter der "
               "Grenze. In den 148 s vor dieser Sendung (31.10. 09:34:13 bis 09:36:41) kamen 14 Einträge der Prio 1 "
               "vorher an die Reihe: 11 ACK-Relays und 3 der verworfenen Pings."),
            li("Nach fremder Aussendung:", "In 52 von 68 Fällen (76,5 %) hatte der Knoten die Funkaussendung eines anderen "
               "Gateways derselben Nachricht schon gehört (Vorlauf median 19,2 s). Ein Rückzug (CANCEL) kam nie, "
               "der Server-Slot sieht sich in 68 von 68 Fällen als Alleinversorger (GWRX-01-Umfeld, Beobachten)."),
            li("Kosten:", "3,34 % der eigenen Sendezeit und 3,28 % der dekodierten Empfangszeit."),
            li("Veraltetes von Fremden:", "36 von 300 Funkkopien (12,0 %) waren älter als 150 s, zuletzt weitergeleitet "
               "von OE3MIF-12 (9), OE3NRA-1 (8), OE3XDC-12 (6), OE3MTC-10 (4) und OE3XPA-12 (4). "
               "Dieser Knoten leitet sie nicht weiter (0 Verwürfe am Gate nötig, 2 Funkempfänge mit 1.207 s und "
               "2.338 s Alter)."),
            li("OE3GJC-12:", f"Das Gateway {gjc_desc} strahlt laut Gesamtbild (OE3XHL-Log) veraltete {{CET}} ab "
               "(5 Kopien mit 154 bis 356 s). Vermutung: ein Build ohne das Gate (vor 05a18b36 / #1238) "
               "oder eine vertraute Uhr, die Minuten nachgeht (eine nicht vertraute Uhr verwirft, "
               "<code>src/cet_age.h:129</code>). In der Liste der veralteten Relays dieses Knotens "
               "kommt OE3GJC-12 nicht vor. Betreiberseite."),
        ])
        + e_cet
        + "<p>Das Netzbild aller Knoten steht in der <a href=\"uebersicht.html\">Übersicht</a>.</p>"
    )

    # ---- Section Q6 ----
    s6 = (
        "<h2 id=\"q6\">F6 Manipulationen dieses Knotens</h2>"
        "<p><strong>Der Knoten leitet viel weiter und zieht nie ein Relay zurück; das Bremsen der Weiterleitung "
        "funktioniert hier nicht.</strong> Relays, die auf bereits gehörte Kopien folgen, kosten bis zu "
        f"{opp_pct} der eigenen Sendezeit (obere Grenze).</p>"
        + '<div class="grid3">'
        '<div class="card pos"><h3>Positiv</h3>' + ul([
            li("Dubletten:", "10.695 Kopien unterdrückt gegenüber 3.713 neuen Ids (2,9 Kopien je neue Id); "
               "Grenze: Gedächtnis hat nur 100 Plätze."),
            li("Relay-Bilanz:", "1.032 Relays eingereiht: 1.013 gesendet (98,2 %), 16 ersetzt (1,6 %), 3 verfallen (0,3 %)."),
            li("Weiterleitungsbremsen:", "107 am Hop-Ende (hop0), 177 am Gateway-Pfadlimit, 552 Ping/Pong-Filter, "
               "2 Schleifenstopps."),
            li("Zeitverfall:", "16 Ersetzungen und 8 Verfälle halten den Puffer frisch."),
            li("Gateway:", "1.401 Uploads, 146 Server-Einspeisungen und 68 Zeitsignale auf Funk."),
        ]) + "</div>"
        '<div class="card neg"><h3>Negativ</h3>' + ul([
            li("Ungültiger Ping:", f"{de(n_ping, 0)} verworfene Einträge, {ping_wr_pct} der Puffer-Schreibvorgänge."),
            li("ACK-Relays ohne Cancel:", f"{de(opp_ack, 0)} von {de(t_ack_relay, 0)} ({pct(opp_ack, t_ack_relay)}) "
               "gesendet, nachdem eine Kopie gehört wurde (obere Grenze: die Kopie erreichte nicht dieselben Hörer)."),
            li("Fall-A-Veto:", f"{opp_other} weitere Relays ({opp['@']} HEY, {opp['!']} Positionen, {opp[':']} Texte) trotz "
               f"gehörter Kopie; alle {de(need_a, 0)} NEED-Auswertungen ergaben Fall A."),
            li("Sendezeit:", f"{de(opp_air, 0)} s von {de(airtime, 0)} s ({opp_pct}) nach gehörter Kopie, obere Grenze: die Kopie erreichte "
               "nicht dieselben Hörer."),
            li("Eigene Wetterdaten:", f"{wx_lost} von {wx_n} nicht gesendet."),
        ]) + "</div>"
        '<div class="card neu"><h3>Neutral</h3>' + ul([
            li("Pfad-Anhängen:", "HEY von 31 B auf rund 72 B je Relay-Hop (461 auf 854 ms)."),
            li("GW-ACK-Paare:", "3 Paare, Absicht (GWACK-01)."),
            li("Dubletten-Fenster:", "2 Relays für Ids, die nach 1.330 und 1.426 s zurückkamen; beide Zeiten liegen "
               f"über dem Dubletten-Fenster dieses Knotens von {de(X['dedup_window_min'])} min (100 Plätze), "
               "also Ablauf des Gedächtnisses."),
            li("Nachbarberichte:", "104 HN-Berichte empfangen, kein Sendemarker (RPTTX) im Log."),
            li("Altersboden:", f"{de(csma['events'], 0)} Ereignisse, neutral bis positiv (siehe F3)."),
        ]) + "</div></div>"
        + "<h3>ACK-Flut durch einen Absender</h3>"
        f"<p><strong>{de(xc['ack_frames_f5ce7'], 0)} von {de(xc['ack_frames'], 0)} ACK-Frames "
        f"({pct(xc['ack_frames_f5ce7'], xc['ack_frames'])}), die dieser Knoten hört, quittieren die Pings von "
        "OE3XWJ-12.</strong> Sie tragen die Message-Id-Kennung F5CE7 der Pings, die OE3XWJ-12 einmal pro Minute "
        "an die Gruppe 2025 sendet; netzweit sind die ACK-Relays die größte Einzellast auf dem Kanal "
        "(41,3 % der Sendungen und 20,2 % der Sendezeit des Clusters, ohne die Pings selbst). "
        "Ob der Ping gewollt ist, ist eine Frage an den Betreiber von OE3XWJ-12.</p>"
        + ul([
            li("Wer die ACKs sendet:", "Nicht die fünf OE3-Knoten, sondern fremde Gateways (Gateway-ACK-Format, "
               "im Mittel rund 4,6 verschiedene ACK-Ids je Ping). Im Code-Stand 651e0920 sendet ein Gateway "
               "auf einen {ping} nie ein ACK (<code>src/lora_functions.cpp:2169-2173</code>, upstream gleich). "
               "Vermutung: die ACK-gebenden Gateways laufen mit anderer Firmware."),
            li("Drei Hebel:", "Der Ping-Takt von OE3XWJ-12 (RAK4631 ohne Zählgrenze, siehe F2), das Testmuster "
               "und die Firmware der fremden Gateways."),
        ])
        + e_ackflood
        + "<h3>Kein Relay wird zurückgezogen</h3>"
        "<p><strong>Dieser Knoten meldet in allen 1.178 NEED-Auswertungen „Fall A“ (Alleinversorger), "
        "deshalb gab es 0 Rückzüge (CANCEL).</strong> Ob der Rückzugsmodus aus ist, folgt nicht aus dem Log.</p>"
        + ul([
            li("Dauerhaft allein:", "In je rund 95 bis 98 % der Auswertungen gilt OE3RFA-7 (1.159), OE3MTC-10 (1.150), "
               "OE3NRA-1 (1.140) und OE3MIF-12 (1.120) als nur von diesem Knoten versorgt, obwohl OE3MTC-10, OE3NRA-1 "
               "und OE3MIF-12 auch von anderen OE3-Knoten direkt gehört werden."),
            li("Bewertung:", "Fall A liegt schon in der ersten Betriebsstunde bei 100 %; das 12-h-Fenster "
               "(<code>nbr_matrix.h:125</code>) ist nicht die Ursache, und abgeleitete symmetrische Kanten können die "
               "Rückrichtung liefern. Die richtige Frage ist, warum die Menge der Alleinversorgten nie leer ist. "
               "Ansatzpunkt: die Abhängigen-Regel und der Test auf einen alternativen Versorger "
               "(<code>src/nbr_matrix.cpp:1777</code>, <code>:1816-1852</code>). Beobachten, Design."),
        ])
        + e_veto
    )

    # ---- Bugs ----
    sbug = (
        "<h2 id=\"bugs\">Verdachtsfälle Firmware-Bugs</h2>"
        "<p>Nur echte Verdachtsfälle; „Verdacht“ heißt: Code gelesen, Verhalten im Log belegt, Ursache nicht "
        "vollständig bewiesen. Code-Stand: Repo-Kopf 651e0920, der Knoten meldet Release-Stempel 20260929 (FLASH_VERSION).</p>"
        + ul([
            li("1 Ping ohne Zielprüfung (Firmware-Befund).",
               "Beobachtung: 689 Einträge „{ping}“ an „255.“ im Takt von 60 s, vom Dekoder verworfen. "
               "Beleg: Rohlog 31.10. 06:47:33.947 ff.; Fehler- und CAD_FREE_NO_TX-Zeile je 689 mal, 0 [LOG] TX. "
               "Code: <code>src/loop_functions.cpp:3820-3895</code> (sendPing prüft das Ziel nicht), "
               "<code>src/nrf52/nrf52_main.cpp:2729</code> (nur Test auf nichtleer), "
               "<code>src/aprs_functions.cpp:369-380</code> (Ablehnung), "
               "<code>src/lora_functions.cpp:3270-3274</code> (Slot-Freigabe ohne Rückmeldung). "
               "Vorschlag: Ziel vor dem Einreihen prüfen und einmal melden."),
            li("1a Auto-Ping ohne Zählgrenze auf RAK4631 (Unterschied ESP32 / RAK4631, Entscheidung offen, "
               "kein Fehler).",
               "Beobachtung: Der Ping läuft über die ganze Aufnahme (und bei OE3XWJ-12 ebenso). "
               "Code: ESP32 prüft und zählt <code>node_pingcount</code> "
               "(<code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code>); der nRF52-Pfad "
               "(<code>src/nrf52/nrf52_main.cpp:2717-2737</code>) tut das nicht, <code>--ping stop</code> "
               "(<code>src/command_functions.cpp:3886</code>) bleibt dort wirkungslos; Upstream 147812e0 gleich. "
               "Zu entscheiden: Zähler auch auf nRF52 oder bewusst dauerhaft."),
            li("2 TX_FRAME für nie gesendete Frames (Verdacht, gering).",
               f"Beobachtung: {de(xc['tx_frame'], 0)} TX_FRAME-Zeilen bei {de(xc['tx_start'], 0)} TX_START. "
               "Beleg: Rohlog (Zählung). Code: Format in <code>src/capture_functions.cpp:186</code>, "
               "Emit-Stelle nicht gelesen. Folge: Auswertungen zählen verworfene Frames als gesendet."),
            li("3 ACK-Weiterleitung ohne Cancel (bestätigt als Verhalten, Bewertung Design).",
               "Beobachtung: 947 von 1.421 ACK-Relays folgten auf eine gehörte Kopie. "
               "Code: <code>src/lora_functions.cpp:653-662</code> (Ring-Art OTHER ohne NEED), "
               "<code>:1358-1361</code> (Cancel-Suche nur für Art RELAY)."),
            li("4 Alleinversorger-Veto (Verdacht, Design).",
               f"Beobachtung: 100 % Fall A bei {de(need_a, 0)} Auswertungen. Ansatz: Abhängigen-Regel und "
               "Test auf alternativen Versorger. Code: <code>src/nbr_matrix.cpp:1777</code>, <code>:1816-1852</code>, "
               "<code>src/nbr_matrix.h:125</code>, <code>src/lora_functions.cpp:1366-1386</code>."),
        ])
    )

    # ---- Neighbours ----
    snb = (
        "<h2 id=\"nachbarn\">Nachbarn und Hardware-Inventar</h2>"
        "<p>32 direkte Nachbarn (letzter Hop). Distanzen fehlen, weil für OE3XIR-12 keine Koordinaten vorliegen. "
        "RSSI und SNR gelten nur für den letzten Hop; die Mediane stammen aus 5.091 Empfängen, die "
        "Spitzen bis -50 dBm sind Störer-Einfluss.</p>"
        + t_nb
        + "<details><summary>Weitere 15 direkte Nachbarn (Tabelle)</summary>" + t_nb_more + "</details>"
        + "<h3>Weitere genannte Stationen</h3>" + t_other
    )

    # ---- Limits ----
    slim = (
        "<h2 id=\"grenzen\">Was nicht geprüft ist / Grenzen</h2>"
        + ul([
            li("Aufnahmedauer:", "11,5 h (31.10. 06:47 bis 18:16), keine Nacht; Tagesgang nur für diesen Ausschnitt bekannt."),
            li("Keine Stammdaten für OE3XIR-12:", "Standort, Höhe, Antenne unbekannt; "
               "Distanzen zu Nachbarn nicht berechenbar."),
            li("Build unbekannt:", "kein Boot-Block; TXP-Slot-Pool, Fix-Stand und Zielprüfung sind aus dem "
               "Verhalten abgeleitet (Vermutung)."),
            li("Funkparameter:", "SF11/BW250/CR4:6 abgeleitet, Frequenz und Sendeleistung nicht im Log."),
            li("Sicht des Auswerters:", "Nur fünf OE3-Knoten loggten; ob ein Frame sonst jemanden erreichte, "
               "bleibt offen, wenn niemand von ihnen ihn hörte."),
            li("Rückzugsmodus:", "Ob Overhear-Cancel eingeschaltet ist, lässt sich aus dem Log nicht ablesen."),
            li("Störer:", "Art und Standort der Quelle unbekannt; Folgen für Nutzframes nicht messbar."),
            li("Internet-Seite der Wetterdaten:", "Dieser Knoten schreibt keine TX-UDP-Zeilen; ob die 5 verfallenen "
               "Positionen das Internet erreichten, ist offen."),
            li("Ping-Ursache:", "Wie „255.“ in die Einstellung kam, ist nicht belegt."),
            li("Ping-Verhalten RAK4631:", "Ob der Auto-Ping ohne Zählgrenze gewollt ist, ist nicht aus Log oder Code "
               "zu entscheiden, sondern eine Entscheidung."),
            li("Eigene Baken:", "Die Ursache der verfallenen Wetter-Positionen ist an OE3XOC-12 belegt, "
               "für diesen Knoten nicht einzeln geprüft."),
        ])
    )

    sopen = (
        "<h2 id=\"fragen\">Offene Fragen an den Betreiber</h2>"
        + ul([
            "<li>Ist der Auto-Ping gewollt, und wer hat das Ziel „255.“ gesetzt (Import, App, älterer Build)? "
            "Kann <code>--pingcall none</code> gesetzt werden?</li>",
            "<li>Soll der Auto-Ping auf dem RAK4631 wie auf dem ESP32 begrenzt werden (Firmware-Entscheidung)?</li>",
            "<li>Ist am Standort eine Störquelle bekannt (Schaltnetzteil, Verstärker, Funkgerät, Sender)?</li>",
            "<li>Warum steht die Knotenuhr auf UTC+2? Soll eine Zeitzonenregel gelten?</li>",
            "<li>Ist die eigene HEY-Bake abgeschaltet (0 gesendet in 11,5 h)?</li>",
            "<li>Soll OE3XIR-12 mit Standort und Höhe in mcmap eingetragen werden?</li>",
            "<li>Warum endet das Log am 31.10. um 18:16 (Logger gestoppt)?</li>",
        ])
    )

    # ---- Dev ----
    sdev = (
        '<details class="dev"><summary>Methodik, Marker, Code-Stellen, Skripte (technisch)</summary>'
        "<h3>Methodik und Abgleich</h3>"
        + ul([
            li("Quellen:", "health/results.json, q2/, q34/, q5/, q6/ results.json und findings.md, "
               "master/master.tsv, base/analysis_oe3xir.txt, Rohlog norm/oe3xir.log (Stichproben)."),
            li("Raten:", "durch 11,49 h geteilt (Zeitraum 06:47:05 bis 18:16:30, 245.490 Zeilen)."),
            li("689 gegen 688:", f"{de(n_ping, 0)} Dekoder-Fehlerzeilen (extra/oe3xir_ping_cost.json). q2 führt {ping_drop} RING_TX_READ mit Quelle "
               "phone_msg, weil der erste Ping vor Logbeginn eingereiht wurde (kein RING_WRITE); 1 weiterer Ping "
               "war bei Logende noch im Puffer."),
            li("Wartezeit-Alter {CET}:", f"q5 (tx_age_s_logger_based) nennt {de(cet_age_max)} s (Logger-basiert, "
               f"bei Sendung), die Puffer-Wartezeit ist {de(cet_wait_max)} s (RING_TX_READ lat=, "
               f"extra/oe3xir_ping_cost.json) und die Knoten-Zeile [CET];tx;age_s;{X['cet_tx_age_s_max_node']}. "
               f"Reserve: 150 - {de(cet_age_max)} = {de(cet_margin)} s."),
            li("ACK-Wartezeit:", f"Nur echte ACKs (RING_TX_READ type=41, n={de(ackw['n'], 0)}; die Pings sind "
               f"ausgeschlossen): p50 {de(ackw['p50'])} s, p90 {de(ackw['p90'])} s, Max {de(ackw['max'])} s, "
               f"{de(ackw['share_ge_20s_pct'], 0)} % mit 20 s oder mehr (extra/oe3xir_ping_cost.json, deckt sich "
               f"mit q6 tx_wait r/A/p1 und q34 headroom p1). Die {de(csma['tx_prio_le3_wait_ge20s_pct'])} % im "
               "Abschnitt F3 betreffen alle Sendungen der Prio 1 bis 3, nicht nur ACKs."),
            li("Fehlempfang-Anteil:", f"{n_rxerr} RX_ERROR gegen {de(xc['rx_decoded_incl_ack'], 0)} dekodierte "
               f"Empfänge einschließlich ACKs (ONRXDONE_TIME) = {rxerr_share}; die Zählung nur über OnRxDone-Zeilen "
               "übersieht jede ACK-Aufnahme und überschätzt den Anteil."),
            li("Dubletten-Fenster:", f"{de(X['dedup_window_min'])} min = 100 Plätze geteilt durch die Rate der "
               "RX_DEDUP_ADD-Zeilen."),
            li("Modell oder Messung:", "Gemessen: alle Zählungen und Wartezeiten. Modell (obere Grenze, "
               "Wiedergabe erster Ordnung): alles zu „ohne TTL“ und „längere Fristen“ (F4), die Ersparnis des "
               "Alters-Bodens, die Anteile „nach gehörter Kopie“ (F6)."),
            li("Eigene Baken (Mechanismus):", "verify/rca_own_beacons.md (Knoten OE3XOC-12); für diesen Knoten nur "
               "die Zählung aus q2 (fate by_src user_wx)."),
            li("Pufferhöhe:", "10 in der 30-s-Statusmeldung, 11 im STAT-Fenster und beim Schreibvorgang."),
            li("Fehlempfang:", "4.800 (Zeilen RX_ERROR) gegen 4.804 (Summe der STAT-Fenster)."),
            li("Sendungen:", "2.598 (TX_START) gegen 2.601 (Summe STAT); 2.594 Sendungen mit RING_WRITE im Log, "
               "4 ohne (Vermutung: vor Logbeginn eingereiht)."),
            li("Zeitfenster Zeitsignal:", "q5 rechnet 11,36 h (erstes bis letztes Telegramm)."),
            li("Rechenwerte:", "ACK-Sendezeit 1.421 x 0,313 s = 445 s (gerechnet, ACK-Frame 12 B, SF11 BW250)."),
        ])
        + "<h3>Marker (Rohlog-Formate)</h3>"
        + table(["Marker", "Bedeutung"], [
            ("RING_WRITE slot=1 type=3A status=FF len=36 msg_id=987EE191 queued=6/20 s&#114;c&#61;phone_msg",
             "Eintrag im Sendepuffer; src nennt die Funktion, queued ist die Tiefe vor dem Schreiben"),
            ("RING_TX_READ slot=3 prio=1 type=3A ... lat=38122ms", "Entnahme; lat = Zeit im Puffer"),
            ("APRS decode - Destination-CallSign Error [255.]", "Dekoder lehnt das Ziel ab"),
            ("CAD_FREE_NO_TX restoring RX", "Kanal frei, aber nichts zu senden; Rückkehr in den Empfang"),
            ("[MC-TEST] TX_FRAME len=36 hex=...", "Capture-Zeile des Frames (auch bei verworfenen)"),
            ("[CET];tx;age_s;147;trusted;1;wait_ms;147865;prio;3", "Zeitsignal-Sendung mit Alter und Wartezeit"),
            ("RX_ERROR rssi=-51 snr=-20", "Präambel gesehen, kein gültiger Frame"),
            ("CSMA_AGED slot=3 age=36438 stage=1 hold=1570", "Alters-Boden greift bei Empfang"),
            ("RING_DROP_STALE ... prio=4 age_s=120", "Eintrag nach TTL verworfen"),
        ], raw=True)
        + "<h3>Code-Stellen</h3>"
        + ul([
            "<li><code>src/loop_functions.cpp:3820-3895</code> sendPing; <code>src/nrf52/nrf52_main.cpp:2717-2737</code> "
            "Ping-Takt ohne Zähler; <code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code> Ping-Takt mit "
            "Zähler (node_pingcount).</li>",
            "<li><code>src/command_functions.cpp:3804-3840</code> --pingcall mit checkRegexCall; "
            "<code>:3886</code> --ping stop (Zähler 0); "
            "<code>src/config_json.h:358</code> node_pingcall als CFG_STR ohne Bereichsprüfung.</li>",
            "<li><code>src/aprs_functions.cpp:369-380</code> Ziel-Prüfung; <code>src/lora_functions.cpp:3270-3274</code> TXRING_FIN_DROP.</li>",
            "<li>TTL: <code>src/txring_ttl.h:46-64</code>, <code>src/txring_functions.cpp:567-645</code>; "
            "Boden: <code>src/lora_functions.cpp:4030-4045</code>, <code>src/csma_timing.h:71-110</code>; "
            "CET-Gate: <code>src/cet_age.h:120-135</code>, <code>src/lora_functions.cpp:2913</code>.</li>",
            "<li>Uhr: <code>src/clock.cpp:153-165</code> (Verdacht).</li>",
        ])
        + "<h3>Skripte</h3>"
        + ul([
            "<li><code>generators/oe3xir.py</code> (dieser Bericht, liest nur <code>--data</code>); "
            "<code>generators/lib/oe3xir_ping_cost.py</code> (Hilfsprogramm, braucht <code>--norm</code>, "
            "schreibt <code>data/extra/oe3xir_ping_cost.json</code>: Kanalabfragen je Entnahme, Ping gegen echte "
            "Sendung, und die Rohlog-Zählungen dieser Seite). "
            f"Ergebnis: Ping {de(ping_gate['sum'], 0)} Gate-Einträge bei {de(ping_gate['n'], 0)} Entnahmen "
            f"(Mittel {de(ping_gate['mean'], 2)}, p90 {ping_gate['p90']}, Max {ping_gate['max']}), "
            f"echte Sendungen {de(tx_gate['sum'], 0)} bei {de(tx_gate['n'], 0)} "
            f"(Mittel {de(tx_gate['mean'], 2)}, p90 {tx_gate['p90']}, Max {tx_gate['max']}); Anteil der Pings "
            f"{gate_share}; Wartezeit der Pings im Puffer median {ping_lat_p50} s, Maximum {ping_lat_max} s.</li>",
            "<li>Vorarbeiten: Themenauswertungen health, txpool (q2), decay (q34), timesignal (q5), "
            "manipulations (q6) unter tools/logreport/.</li>",
        ])
        + "</details>"
    )

    foot = (
        '<div class="foot"><p>Quellen: Rohlog meshcom_oe3xir_2026-10-31_064704.log, health/q2/q34/q5/q6-Ergebnisse, '
        'mcmap-Stammdaten (Stand 01.11.2026). Netzweite Sicht: <a href="uebersicht.html">Übersicht</a>.</p>'
        "<p>Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware.</p></div>"
    )

    toc = (
        '<nav class="toc"><ol>'
        '<li><a href="#q0">Gesundheit und Funkumfeld</a></li>'
        '<li><a href="#q2">F2 TX-Puffer</a></li><li><a href="#q3">F3 Verlust</a></li>'
        '<li><a href="#q4">F4 Gewinn</a></li><li><a href="#q5">F5 Zeitsignal</a></li>'
        '<li><a href="#q6">F6 Manipulationen</a></li><li><a href="#bugs">Befunde</a></li>'
        '<li><a href="#nachbarn">Nachbarn</a></li><li><a href="#grenzen">Grenzen</a></li>'
        '<li><a href="#fragen">Offene Fragen</a></li></ol></nav>'
    )

    body = (
        '<div class="eyebrow">MeshCom Log-Auswertung · OE3XIR-12</div>'
        "<h1>OE3XIR-12 läuft stabil, schickt aber jede Minute einen Auto-Ping an das ungültige Ziel „255.“ "
        "und hört eine starke lokale Quelle</h1>"
        + meta + bluf + act
        + "<h2>Antwort-Index</h2>" + t_index + toc
        + s0 + s2 + s3 + s4 + s5 + s6 + sbug + snb + slim + sopen + sdev + foot
    )
    html_out = page("OE3XIR-12 Log-Auswertung 24 h", body)
    order: list[str] = []
    for m in re.finditer(r"Abb\. @@(.*?)@@:", html_out):
        if m.group(1) not in order:
            order.append(m.group(1))
    for i, key in enumerate(order, 1):
        html_out = html_out.replace(f"Abb. @@{key}@@:", f"Abb. {i}:")
    Path(a.out).write_text(html_out, encoding="utf-8")
    print(f"written {a.out} ({len(html_out)} bytes, figures {len(order)})")


if __name__ == "__main__":
    main()
