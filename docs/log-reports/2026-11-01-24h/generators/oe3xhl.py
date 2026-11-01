#!/usr/bin/env python3
"""Per-node report generator: OE3XHL-12 (24-h log 31.10.-01.11.2026).

Usage: python3 -I oe3xhl.py --data <RUN>/data --kit <repo>/tools/logreport --out <RUN>/oe3xhl.html
  --data  directory holding q2/ q34/ q5/ q6/ health/ master/ extra/ and CORE-FINDINGS.md
  --kit   directory holding reportkit.py
  --out   HTML file to write
Stdlib only, no raw logs at render time. Numbers that need the raw logs come from
extra/oe3xhl_measure.json (written by generators/lib/oe3xhl_measure.py --norm <dir>).
Literals are limited to judgement text and to figures taken from the findings.md files in data/;
the dev details name the source of each.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any

CET = "{CET}"
PING = "{ping}"
NODE = "oe3xhl"
CALL = "OE3XHL-12"
LABELS = {"oe3xhl": "OE3XHL-12", "oe3xia": "OE3XIA-12", "oe3xir": "OE3XIR-12",
          "oe3xoc": "OE3XOC-12", "oe3xwj": "OE3XWJ-12"}


def de(x: float, nd: int = 0) -> str:
    """German number format: thousands '.', decimal ','."""
    s = f"{x:,.{nd}f}"
    return s.replace(",", "X").replace(".", ",").replace("X", ".")


def pct(a: float, b: float, nd: int = 1) -> str:
    return de(100.0 * a / b, nd) + " %"


def load_json(p: Path) -> Any:
    return json.loads(p.read_text(encoding="utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--kit", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    data = Path(a.data)
    sys.path.insert(0, a.kit)
    from reportkit import bars, chip, esc, explain, hist, page, table  # noqa: PLC0415

    H = load_json(data / "health/results.json")
    Q2 = load_json(data / "q2/results.json")["nodes"][NODE]
    Q34 = load_json(data / "q34/results.json")["nodes"][NODE]
    Q5all = load_json(data / "q5/results.json")["nodes"]
    Q5 = Q5all[NODE]
    Q6all = load_json(data / "q6/results.json")
    Q6 = Q6all["nodes"][NODE]
    X = load_json(data / "extra/oe3xhl_measure.json")["nodes"]
    XIA = load_json(data / "extra/oe3xia_analysis.json")
    h = H["nodes"][NODE]
    hn = H["nodes"]
    noise_p5 = H["network"]["noise_carpet_p5_dbm"]

    # ---------- master data ----------
    master: dict[str, dict[str, str]] = {}
    with (data / "master/master.tsv").open(encoding="utf-8") as f:
        for r in csv.DictReader(f, delimiter="\t"):
            master[r["call"]] = r
    dist: dict[str, str] = {}
    with (data / "master/distances.tsv").open(encoding="utf-8") as f:
        for r in csv.DictReader(f, delimiter="\t"):
            dist[r["call"]] = r["d_oe3xhl"]
    log_neigh = {x["call"]: x for x in h["neighbours"]["top"]}
    me = master[CALL]
    gjc = master["OE3GJC-12"]
    gjc_km = float(dist["OE3GJC-12"])

    def inv_row(call: str, with_rf: bool = False) -> list[str]:
        m = master.get(call)
        ln = log_neigh.get(call)
        if m:
            loc = m["description"] or f"{float(m['lat']):.4f} N, {float(m['lon']):.4f} E"
            d = dist.get(call, "")
            dtxt = "k. A."
            if call == CALL:
                dtxt = "Standort selbst"
            elif d:
                dtxt = de(float(d), 2 if float(d) < 1 else 1) + " km"
            row = [call, m["hardware"], m["firmware"], loc, f"{m['altitudeM']} m", dtxt]
        elif call == "OE3XIR-12":
            row = [call, "RAK4631", "4.40a", "keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg)",
                   "k. A.", "k. A."]
        else:
            hw = ln["hw"] if ln and ln.get("hw") not in (None, "n/a") else "unbekannt"
            fw = ln["fw_log"].replace("40:a", "4.40a") + " (aus Log)" if ln else "unbekannt"
            row = [call, hw + " (aus Log)" if hw != "unbekannt" else hw, fw,
                   "keine Stammdaten", "k. A.", "k. A."]
        if with_rf:
            row.append(f"{de(ln['rssi_median'])} dBm / {de(ln['snr_median'])} dB ({de(ln['n'])})"
                       if ln else "k. A.")
        return row

    # ---------- figure counter ----------
    order = ["util", "rxerr", "precad", "e1", "wait", "ehey", "e3", "e7", "fate", "cancelh", "e2",
             "e6", "eping", "e4", "e5"]
    seen: list[str] = []

    def ref(key: str) -> str:
        return f"Abb. {order.index(key) + 1}"

    def cap(key: str, text: str) -> str:
        seen.append(key)
        assert order[len(seen) - 1] == key, (key, seen)
        return f"{ref(key)}: {text}"

    # ---------- SVG helpers (classes only, no colours; umlauts as ae/oe/ue) ----------
    def tx(x: float, y: float, s: str, cls: str = "d-txt", anchor: str = "middle") -> str:
        return f'<text class="{cls}" x="{x}" y="{y}" text-anchor="{anchor}">{esc(s)}</text>'

    def bx(x: float, y: float, w: float, hh: float, lines: list[str], cls: str = "",
           anim: str = "") -> str:
        out = [f'<rect class="d-box {cls} {anim}" x="{x}" y="{y}" width="{w}" height="{hh}" rx="8"/>']
        n = len(lines)
        for i, s in enumerate(lines):
            yy = y + hh / 2 + (i - (n - 1) / 2) * 17 + 4
            out.append(tx(x + w / 2, yy, s, "d-txt" if i == 0 else "d-txt-s"))
        return "".join(out)

    def ln(x1: float, y1: float, x2: float, y2: float, cls: str = "", anim: str = "",
           dash: bool = False) -> str:
        st = ' style="stroke-dasharray:5 5"' if dash else ""
        return (f'<line class="d-line {cls} {anim}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"{st}/>')

    def head(x: float, y: float, fcls: str, direction: str = "r") -> str:
        d = -1 if direction == "r" else 1
        pts = f"{x},{y} {x + d * 10},{y - 5} {x + d * 10},{y + 5}"
        return f'<polygon class="{fcls}" points="{pts}"/>'

    def head_up(x: float, y: float, fcls: str) -> str:
        return f'<polygon class="{fcls}" points="{x},{y} {x - 5},{y + 10} {x + 5},{y + 10}"/>'

    def dot(x: float, y: float, r: float, cls: str) -> str:
        return f'<circle class="d-node {cls}" cx="{x}" cy="{y}" r="{r}"/>'

    body: list[str] = []
    add = body.append

    # ---------- numbers ----------
    span = h["span"]
    tr = h["traffic"]
    rx_total = Q5["rawpass"]["rx_decoded_frames"]
    ack_ev = tr["ack_events"]
    msg_ev = tr["heard_events"]
    ty = tr["types"]
    own = h["own_tx"]
    n_tx = own["tx_lines"]
    fate = Q2["fate"]
    fc = fate["counts"]
    tot_w = fate["total_writes"]
    occ = Q2["occupancy"]["queued"]
    cad = h["cad"]
    rf = h["rf"]
    cen = h["firmware_census"]
    qmax = Q2["occupancy"]["minutes_ge_5"]
    relay_fate = Q6["relay_fate"]
    xh_ia = log_neigh["OE3XIA-12"]["n"]
    relay_n = sum(relay_fate.values())
    tw = Q6["tx_wait"]["wait"]
    n_cancel = Q6["totals"]["cancel"]
    n_cancel_lines = Q2["fate"]["cancel_lines"]
    n_need_a = Q6["totals"]["need_A"] - Q6["totals"]["gwi"]
    n_need_b = Q6["totals"]["need_B"]
    opp = Q6["cancel_opportunity"]
    opp_air = Q6["opportunity_airtime"]
    csma = Q34["csma"]
    head_r = Q34["headroom"]
    ack_fwd = own["src_type"]["r:A"]
    ack_dup_fwd = opp["A"]["sent_with_foreign_copy_heard_while_queued"]
    f5ce7 = h["ack_attribution"]["top_acked_prefix"][0]
    ack_pct_all = H["network"]["ack_ping_attribution"]["ack_airtime_share_pct"][NODE]
    gw_tx_after = Q5["tx_after_overheard_gateway_radiation_n"]
    cet_tx = Q5["tx_count"]

    eth = h["eth"]
    gwr = h["gateway"]

    # corrected / ACK-aware measures (extra/oe3xhl_measure.json) and cluster ping share (q6 network)
    xh = X[NODE]
    others4 = [k for k in X if k != NODE]
    err_oth = [X[k]["rx_error_share_pct"] for k in others4]
    cancel_lines = xh["cancel_lines"]
    cancel_matched = Q6["totals"]["cancel"]
    rly_q_tx = xh["rly_q_tx"]
    cancel_share = pct(cancel_lines, rly_q_tx)
    psh = Q6all["network"]["ping_share"]
    cl_tx = psh["total_tx"]
    cl_ack = psh["ping_tx"]["ack_of_ping_relay"]
    cl_ping = psh["ping_tx"]["ping_orig_or_relay"]
    cl_air = psh["total_air_s"]
    lp = Q6all["network"]["link_probability"]
    stale = xh["cet_rf_older_150s"]
    stale_gjc = [r for r in stale if r["path"].endswith("OE3GJC-12")]
    stale_other = [r for r in stale if not r["path"].endswith("OE3GJC-12")]
    ia_sil = XIA["n_gaps"]
    ia_sil_pct = 100.0 * XIA["silent_s"] / XIA["span_s"]
    ia_gap = XIA["max_gap"]["oe3xia"]
    ev_hey = Q2["hey_chains"]["own_hey"]
    ev_rhey = Q2["hey_chains"]["relay_hey"]
    lat_prio = Q2["latency"]["by_prio_s"]
    cen_ev = cen["events_pre_cad_pct"]
    hc = fate["by_src"]

    # ---------- head ----------
    add('<div class="eyebrow">MeshCom Log-Auswertung · OE3XHL-12</div>')
    add("<h1>OE3XHL-12 auf der Sinawarte: ein gesunder, ruhiger Knoten und der einzige mit "
        "sichtbarem Overhear-Cancel; im Umfeld und bei den ACK-Relays heißt es Beobachten</h1>")
    add('<ul class="meta">'
        f"<li><b>Log-Datei:</b> oe3xhl.log, {de(span['lines'])} Zeilen</li>"
        "<li><b>Zeitraum:</b> 31.10.2026 06:44:04 bis 01.11.2026 07:46:02 Logger-Uhr "
        f"({de(span['hours'], 2)} h); die Knotenuhr läuft mit dem Logger (Versatz höchstens 1 s, "
        "UTC+1), es ist keine Umrechnung nötig</li>"
        "<li><b>Hardware:</b> RAK4631 (nRF52840) mit Ethernet (Chip W5100S abgeleitet, nicht im Log)</li>"
        "<li><b>Firmware:</b> 4.40a, Release-Stempel 20260929 (FLASH_VERSION, kein Baudatum); der genaue Build ist unbekannt "
        "(kein Boot-Block im Log), der Slot-Pool (TXP) ist aus den Markern abgeleitet (Vermutung)</li>"
        f"<li><b>Standort:</b> {me['description']}, {de(float(me['lat']), 4)} N, "
        f"{de(float(me['lon']), 4)} E, {me['altitudeM']} m Höhe (mcmap)</li>"
        "<li><b>Rolle:</b> Internet-Gateway und Relay</li>"
        "<li><b>LoRa:</b> SF11, 250 kHz Bandbreite, CR 4/6 (Modus 8/8 aus den Logzeilen, Sendezeit "
        "stimmt); Frequenz und Sendeleistung stehen nicht im Log</li>"
        "<li><b>Stand:</b> 01.11.2026</li></ul>")

    add('<div class="bluf"><p class="lead">OE3XHL-12 ist gesund: kein Neustart, keine Empfangspause '
        f"über {de(xh['gap_longest_s'])} s, kein Text und kein ACK durch den Zeitverfall verloren, "
        f"nur {de(xh['rx_error_share_pct'], 1)} % Empfangsfehler (die anderen vier: "
        f"{de(min(err_oth), 1)} bis {de(max(err_oth), 1)} %). Das Gesamturteil lautet Beobachten, "
        "weil das Umfeld auffällt, nicht der Knoten. Er ist auch der einzige, an dem der "
        f"Overhear-Cancel im Log sichtbar ist: {de(cancel_lines)} CANCEL-Zeilen "
        f"({cancel_share} der {de(rly_q_tx)} Relays), davon {de(cancel_matched)} sauber einem "
        "Relay zugeordnet.</p></div>")

    add('<div class="callout watch"><b>Beobachten (Umfeld und Netz, nicht die Gesundheit dieses Knotens)</b><ul>'
        f"<li><b>Firmware ohne CAD in der Nachbarschaft:</b> {de(cen_ev, 1)} % der "
        "gehörten Ereignisse stammen von Stationen ohne Kanalabfrage vor dem Senden, mehr "
        "als an jedem anderen der fünf Knoten. Das beschreibt die Nachbarschaft; am Knoten selbst "
        "zeigt sich nichts davon. OE3HES-1 steht nur 20 m neben dem Knoten.</li>"
        f"<li><b>Veraltete Zeit von OE3GJC-12:</b> {len(stale_gjc)} Funkkopien des Zeitsignals "
        f"({CET}), die älter als 150 s waren (siehe F5).</li>"
        "<li><b>Eigene HEY warten länger als Relay-HEY:</b> Median "
        f"{de(tw['o/@/p5']['wait_med_s'], 1)} s gegen {de(tw['r/@/p5']['wait_med_s'], 1)} s "
        "(siehe F2). Hier folgenlos, an ausgelasteten Knoten nicht.</li></ul></div>")

    add('<div class="callout act"><b>Handlungsbedarf (Entscheidung beim Betreiber, nicht an '
        "diesem Knoten)</b><ul>"
        f"<li><b>ACK-Flut durch Prüf-Pings:</b> {de(ack_fwd)} von {de(n_tx)} Sendungen dieses Knotens "
        f"({pct(ack_fwd, n_tx)}) sind weitergeleitete ACKs. {de(f5ce7['share_pct'], 1)} % der "
        f"empfangenen ACKs bestätigen das {PING} von OE3XWJ-12 (eine Prüfnachricht pro Minute). "
        "Für ACK-Relays gibt es keinen Abbruchpfad. Zwei Fragen an die Betreiber: Ist der "
        "Dauer-Ping von OE3XWJ-12 gewollt, und soll der Auto-Ping auf RAK4631 eine Obergrenze "
        "haben wie auf ESP32 (Unterschied ESP32 / RAK4631, Entscheidung offen)?</li></ul></div>")

    # ---------- Antwort-Index ----------
    add("<h2>Antwort-Index</h2>")
    idx = [
        ["Q0 Gesundheit", "Knoten gesund, ruhigster Funkstandort. Beobachten wegen des Umfelds: "
         f"{de(cen_ev, 1)} % der gehörten Ereignisse von Firmware ohne CAD (Nachbarschaft, nicht der "
         "Knoten).", chip("watch")],
        ["Q2 TX-Puffer", f"Pool ohne Auffälligkeit: höchstens {int(occ['max'])} von 20 Slots belegt, "
         "0 Überläufe, Schicksal der Einträge zu 100 % erklärt.", chip("ok")],
        ["Q3 Verlust durch Zeitverfall", f"{fc['stale_ttl']} Verfalls-Drops in {de(span['hours'], 0)} h "
         "(2 eigene HEY, 1 Relay-HEY), kein Text/ACK/Zeitsignal. Eigene Wetter-Position wartete bis "
         f"{de(head_r['p4']['max'])} s bei {head_r['p4']['ttl_s']} s TTL.",
         chip("watch")],
        ["Q4 Gewinn durch Zeitverfall", f"Praktisch keiner nötig: {de(Q34['drop_toa_s'], 2)} s Sendezeit "
         f"({de(Q34['drop_toa_pct_of_tx'], 2)} %) gespart.", chip("ok")],
        ["Q5 Zeitsignal", f"{Q5['rf_copies_per_id']['n']} von {Q5['expected_in_window']} Telegrammen "
         f"angekommen, {Q5['tx_age_gt150_n']} veraltet gesendet. Beobachten: OE3GJC-12 strahlt "
         "veraltete Zeit ab; Server-Frames werden nie abgebrochen (Fall A).", chip("watch")],
        ["Q6 Manipulationen", f"Overhear-Cancel wirkt ({de(cancel_lines)} CANCEL-Zeilen, "
         f"{cancel_share} der Relays); ACK-Relays ohne Abbruchpfad, Ping-Test als Last "
         "(Entscheidung offen).", chip("act")],
    ]
    add(table(["Frage", "Kurzantwort", "Urteil"], idx, raw=True))

    add('<nav class="toc"><ol>'
        '<li><a href="#gesundheit">Gesundheit und Funkumfeld</a></li>'
        '<li><a href="#f2">F2 TX-Puffer mit Slots</a></li>'
        '<li><a href="#f3">F3 Verlust durch Zeitverfall</a></li>'
        '<li><a href="#f4">F4 Gewinn durch Zeitverfall</a></li>'
        '<li><a href="#f5">F5 Zeitsignal</a></li>'
        '<li><a href="#f6">F6 Manipulationen dieses Knotens</a></li>'
        '<li><a href="#bugs">Verdachtsfälle Firmware</a></li>'
        '<li><a href="#nachbarn">Nachbarn und Hardware</a></li>'
        '<li><a href="#grenzen">Grenzen</a></li>'
        '<li><a href="#fragen">Offene Fragen</a></li>'
        '<li><a href="#dev">Methodik und Code-Stellen</a></li></ol></nav>')

    # ================= Gesundheit =================
    add('<h2 id="gesundheit">Gesundheit und Funkumfeld</h2>')
    add("<p><b>Der Knoten läuft ohne Befund; sein Umfeld ist das ruhigste der Gruppe, aber mit dem "
        "höchsten Anteil an Stationen ohne Kanalabfrage.</b></p>")
    n_hey = ty["@"]["n"]
    n_pos = ty["!"]["n"]
    n_txt = ty[":"]["n"]
    add("<ul>"
        f"<li><b>Verkehr:</b> {de(rx_total)} empfangene Frames, davon {de(ack_ev)} ACK-Frames "
        f"({pct(ack_ev, rx_total)}). Die übrigen {de(msg_ev)} sind HEY ({de(n_hey)}, "
        f"{pct(n_hey, msg_ev)}), Positionen ({de(n_pos)}, {pct(n_pos, msg_ev)}) und Texte "
        f"({de(n_txt)}, {pct(n_txt, msg_ev)}). Das sind Empfänge, keine angenommenen Pakete.</li>"
        f"<li><b>Eigene Sendungen:</b> {de(n_tx)} ({de(own['tx_per_hour'], 1)} je Stunde), "
        f"{de(own['airtime_s'], 1)} s Sendezeit, das sind {de(Q34['tx_duty_pct'], 1)} % der Zeit. "
        f"Davon {own['src_split']['o']} eigene, {de(own['src_split']['r'])} Relays, "
        f"{own['src_split']['g']} Server-Einspeisungen.</li>"
        f"<li><b>Kanalauslastung:</b> Mittel {de(h['channel']['CHANNEL_UTIL']['mean'], 1)} %, "
        f"Median {de(h['channel']['CHANNEL_UTIL']['median'])} %, 95. Perzentil "
        f"{de(h['channel']['CHANNEL_UTIL']['p95'])} %, Spitze {de(h['channel']['CHANNEL_UTIL']['max'])} % "
        f"({h['channel']['CHANNEL_UTIL']['ge70']} Messwerte über 70 %). Nachts ist es ruhiger ("+ref("util")+"). Die bekannte ESP32-Überhöhung der "
        "Anzeige betrifft den nRF52 nicht.</li>"
        f"<li><b>Rauschen:</b> Der Rauschteppich (5. Perzentil der RSSI bei Empfangsfehlern) liegt "
        f"bei {de(rf['rx_error_rssi']['p5'])} dBm, 1 dB über dem theoretischen Wert von "
        f"{de(rf['theory_floor_bw250_dbm'])} dBm für 250 kHz. Die anderen vier OE3-Knoten liegen "
        f"zwischen {de(noise_p5['oe3xwj'])} und {de(noise_p5['oe3xoc'])} dBm. Kein Empfangsfehler mit mehr als "
        "-85 dBm ("+ref("rxerr")+").</li>"
        f"<li><b>Empfangsfehler:</b> {de(rf['rx_error_n'])} RX_ERROR (Kopf erkannt, kein gültiges "
        f"Frame), {de(rf['rx_error_per_hour_mean'], 1)} je Stunde, {de(xh['rx_error_share_pct'], 1)} % "
        f"aller Empfangsversuche ({de(xh['rx_error'])} von {de(xh['rx_error'] + xh['rx_decoded_onrxdone_time'])}; "
        "die Zählung enthält die ACK-Frames, die keine OnRxDone-Zeile erzeugen). Bei den anderen vier "
        f"Knoten sind es {de(min(err_oth), 1)} bis {de(max(err_oth), 1)} %; der Schwellwert für "
        "Beobachten (40 %) wird an keinem OE3-Knoten erreicht.</li>"
        f"<li><b>Kanalabfrage (CAD, horcht vor dem Senden, ob jemand sendet):</b> {de(cad['cad_scans'])} "
        f"Abfragen, {de(cad['initial_busy_rate_pct'], 1)} % zeigten zuerst „belegt“, "
        f"{de(cad['confirmed_busy_pct_of_initial_busy'], 1)} % davon bestätigt, "
        f"{de(cad['false_positive_pct_of_initial_busy'], 1)} % Fehlalarm. "
        "Am Sende-Tor war in 73,2 % der Fälle gleich der erste Versuch frei, in 2,4 % brauchte es 3 "
        f"oder mehr; höchstens {cad['attempt_max']} Versuche, 0 Sicherheits-Timeouts.</li>"
        f"<li><b>Neustarts und Speicher:</b> Laufzeit lückenlos von {de(h['stat']['up_first'])} s bis "
        f"{de(h['stat']['up_last'])} s, {len(h['stat']['uptime_resets'])} Neustarts, "
        f"{len(h['health']['crash_hits'])} Absturzmeldungen, freier Speicher konstant "
        f"{de(h['stat']['heap']['min'])} B (der nRF52-Wert ist grob).</li>"
        f"<li><b>Gateway:</b> {de(gwr['keepalive_ok'])} Keepalives ok, "
        f"{gwr['beat_after_keep_missing']} ({pct(gwr['beat_after_keep_missing'], gwr['keepalive_ok'])}) "
        "ohne Antwort des Servers. Um 31.10. 07:22:10 fielen die Antworten bei allen fünf "
        "OE3-Gateways gleichzeitig aus (Serverseite). Ethernet: 0 Verbindungsabbrüche, 0 Resets, "
        f"{eth['last']['renews']} DHCP-Erneuerungen (alle 15 min), {eth['last']['renew_fail']} Fehler.</li>"
        f"<li><b>Empfangspausen:</b> {xh['gaps_over_300s']} über 300 s. Längste Lücke "
        f"{de(xh['gap_longest_s'])} s (ACK-Frames mitgezählt), sie endete am 31.10. "
        f"{xh['gap_longest_end'][11:19]}; Median zwischen zwei Empfängen {de(xh['gap_median_s'], 1)} s. "
        f"Zum Vergleich: OE3XIA-12 hat {ia_sil} Empfangspausen über 5 min ({de(ia_sil_pct, 1)} % der "
        f"Zeit, längste {de(ia_gap)} s); die anderen drei OE3-Knoten höchstens "
        f"{de(max(X[k]['gap_longest_s'] for k in ('oe3xir', 'oe3xoc', 'oe3xwj')))} s.</li></ul>")

    add(hist([(k[-2:], v) for k, v in h["channel"]["util_by_hour"].items()],
             cap("util", "Kanalauslastung im Stundenmittel: 14 bis 21 %, nachts ruhiger"), unit=" %",
             ylabel="Stunde (Logger-Uhr), Beginn 31.10. 06 Uhr"))
    five = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
    add(bars([(LABELS[k], hn[k]["rf"]["rx_error_per_hour_mean"]) for k in five],
             cap("rxerr", "OE3XHL-12 hat mit 69 Empfangsfehlern je Stunde den saubersten Empfang"),
             unit=" /h"))

    add("<h3>Beobachten: Firmware ohne CAD in der Nachbarschaft</h3>")
    add(f"<p><b>{cen['nodes_pre_cad']} von {cen['nodes_total']} gehörten Absendern "
        f"({de(cen['nodes_pre_cad_pct'], 1)} %) laufen auf Firmware ohne CAD</b> (4.35 bis Buchstabe o und "
        f"älter). Sie tragen {de(cen['events_pre_cad_pct'], 1)} % der Ereignisse, "
        f"{de(cen['unique_msgs_pre_cad_pct'], 1)} % der Nachrichten und {de(cen['airtime_pre_cad_pct'], 1)} % der "
        "Sendezeit, die der Knoten hört. Das ist der höchste Wert der fünf OE3-Knoten (" + ref("precad") + "). "
        "Er beschreibt das Umfeld des Standorts, nicht den Zustand des Knotens: Beobachten, kein "
        "Handlungsbedarf für diesen Knoten. Ein Update der Nachbarn wäre die Abhilfe.</p>")
    add(bars([(LABELS[k], hn[k]["firmware_census"]["events_pre_cad_pct"]) for k in five],
             cap("precad", f"Bei OE3XHL-12 stammt mit {de(cen_ev, 1)} % der größte Ereignisanteil von Firmware ohne CAD"),
             unit=" %"))
    # explainer 1
    e1 = (
        bx(10, 20, 190, 70, ["Firmware vor CAD", "sendet sofort"], "d-act")
        + bx(225, 20, 190, 70, ["Funkkanal", "gemeinsam genutzt"], "d-neu")
        + bx(440, 20, 190, 70, ["OE3XHL-12 mit CAD", "horcht erst, dann Senden"], "d-ok")
        + ln(200, 55, 215, 55, "d-act-s", "anim-flow") + head(225, 55, "d-act-f")
        + ln(440, 55, 425, 55, "d-ok-s") + head(415, 55, "d-ok-f", "l")
        + tx(320, 125, f"{de(cen_ev, 1)} % der gehoerten Ereignisse stammen von Absendern vor CAD",
             "d-txt-s")
        + tx(320, 148, f"Wirkung auf diesen Knoten nicht belegt: nur {de(rf['rx_error_per_hour_mean'])} "
             "Empfangsfehler je Stunde", "d-txt-s")
        + tx(320, 171, "Stationen vor CAD pruefen den Kanal nicht", "d-txt-s")
        + tx(320, 190, "und koennen mitten in ein fremdes Frame senden", "d-txt-s")
    )
    add(explain(e1, cap("e1", "Stationen ohne Kanalabfrage senden sofort, OE3XHL-12 prüft den Kanal zuerst"),
                height=210))
    top_pc = cen["top_pre_cad"][:8]
    add(table(["Call", "Hardware", "Firmware", "Ereignisse", "Anteil", "Standort", "Höhe", "Distanz"],
              [[x["call"], master[x["call"]]["hardware"] if x["call"] in master else x["hw"],
                x["fw"], de(x["events"]), de(x["events_pct_of_nodes"], 1) + " %",
                (master[x["call"]]["description"] if x["call"] in master and
                 master[x["call"]]["description"] else f"{float(x['lat']):.4f} N, {float(x['lon']):.4f} E"),
                master[x["call"]]["altitudeM"] + " m" if x["call"] in master else "k. A.",
                de(x["dist_km"], 2 if x["dist_km"] < 1 else 1) + " km"] for x in top_pc],
              num_cols=(3, 4, 7),
              caption="Die acht lautesten Absender ohne CAD (Ereignisse am Knoten, Standort und Höhe aus mcmap)"))
    add(f"<p>Zusammen sind das {de(cen['events_pre_cad'])} Ereignisse. OE3HES-1 (T-Deck+, 20 m entfernt, am selben Mast "
        "wie der Knoten) wird mit -98 dBm und SNR +5 dB am stärksten empfangen und ist mit 4,6 % "
        "der zweitlauteste Sender ohne CAD.</p>")

    # ================= F2 =================
    add('<h2 id="f2">F2 TX-Puffer mit Slots</h2>')
    add("<p><b>Der Slot-Puffer (20 Plätze für wartende Sendungen) arbeitet ohne Auffälligkeit: nie "
        f"mehr als {int(occ['max'])} Plätze belegt, 0 Überläufe, jeder der {de(tot_w)} Einträge "
        "hat ein erklärtes Schicksal.</b></p>")
    rows_f = [
        ["Gesendet", de(fc["tx_air"]), pct(fc["tx_air"], tot_w, 2)],
        ["Durch Overhear-Cancel abgebrochen", de(fc["nbr_cancel"]), pct(fc["nbr_cancel"], tot_w, 2)],
        ["Durch neueren Eintrag ersetzt (Supersede)", de(fc["superseded"]), pct(fc["superseded"], tot_w, 2)],
        ["Nach TTL verworfen", de(fc["stale_ttl"]), pct(fc["stale_ttl"], tot_w, 2)],
        ["Summe", de(tot_w), "100,00 %"],
    ]
    add(table(["Schicksal eines Puffer-Eintrags", "Anzahl", "Anteil"], rows_f, num_cols=(1, 2),
              caption="0 unerklärte Einträge, 0 Einträge am Logende offen; Abbruch zählt "
                      f"{n_cancel_lines} Logzeilen, jede einem ungesendeten Eintrag zugeordnet"))
    add("<ul>"
        f"<li><b>Belegung:</b> Median {int(occ['p50'])}, 90. Perzentil {int(occ['p90'])}, "
        f"99. Perzentil {int(occ['p99'])}, Maximum {int(occ['max'])} von 20 Slots; "
        f"{de(qmax)} min insgesamt mit 5 oder mehr belegten Slots. Kein Aufbau über den Tag "
        f"(Steigung {de(Q2['occupancy']['hourly_mean_slope_per_h'], 3)} Slots je Stunde).</li>"
        f"<li><b>Hängende Slots:</b> keine. Status „retrying“ in {de(Q2['occupancy']['status_samples'])} Stichproben "
        f"{Q2['occupancy']['status_retrying_max']}-mal; keine Mehrfachsendung desselben Eintrags "
        f"({Q2['double_tx']['pairs']} Doppel-Sendungen, {Q2['double_write']['pairs']} doppelt eingereihte IDs).</li>"
        f"<li><b>Ersetzen:</b> {Q2['supersede']['events']} Supersede (alle HEY, "
        f"{Q2['supersede']['by_old_src']['rx_relay']} Relay-HEY, {Q2['supersede']['by_old_src']['auto_pos']} eigene). "
        f"Der ersetzte Eintrag war im Median {de(Q2['supersede']['old_age_s']['p50'], 1)} s alt "
        f"(Maximum {de(Q2['supersede']['old_age_s']['max'], 1)} s). "
        f"{Q2['supersede']['chain_tail']['nbr_cancel']} dieser Ketten endeten im "
        f"Overhear-Cancel, {Q2['supersede']['chain_tail']['tx_air']} gingen auf Sendung.</li>"
        f"<li><b>Wartezeit nach Priorität:</b> ACK im Median {de(lat_prio['1']['p50'], 1)} s "
        f"(90. Perzentil {de(lat_prio['1']['p90'], 1)} s), Texte und {CET} {de(lat_prio['3']['p50'], 1)} s "
        f"({de(lat_prio['3']['p90'], 1)} s), Positionen {de(lat_prio['4']['p50'], 1)} s "
        f"({de(lat_prio['4']['p90'], 1)} s), HEY {de(lat_prio['5']['p50'], 1)} s "
        f"({de(lat_prio['5']['p90'], 1)} s). Positionen und "
        "HEY-Relays haben also ein Wartezeit-Plateau bei etwa 60 s (" + ref("wait") + ").</li></ul>")
    add(bars([("ACK (Prio 1)", head_r["p1"]["p90"]), ("Text, Server (Prio 3)", head_r["p3"]["p90"]),
              ("Position (Prio 4)", head_r["p4"]["p90"]), ("HEY (Prio 5)", head_r["p5"]["p90"])],
             cap("wait", "Positionen und HEY warten im 90. Perzentil etwa 60 s, ACKs 15 s"), unit=" s"))
    add("<p><b>Ursache des 60-s-Plateaus (Vermutung):</b> Beim Overhear-Cancel (Relay wird "
        "abgebrochen, wenn ein Nachbar die Sendung übernimmt) wartet ein Positions- oder "
        "HEY-Relay im Fall B (ein anderer deckt die Hörer bereits ab) einmalig 20 s länger, "
        "höchstens 60 s, damit die Nachbarn zuerst senden. Die Zahlen passen dazu: Abbrüche "
        f"kommen im Median {de(fate['cancel_age_s']['p50'], 1)} s nach dem Einreihen "
        f"(90. Perzentil {de(fate['cancel_age_s']['p90'], 1)} s).</p>")
    wait_rows = []
    names = {"o/@/p5": "Eigenes HEY (Prio 5)", "r/A/p1": "ACK-Relay (Prio 1)",
             "r/!/p4": "Positions-Relay (Prio 4)", "r/@/p5": "HEY-Relay (Prio 5)",
             "g/:/p3": f"Server-Einspeisung inkl. {CET} (Prio 3)", "o/!/p4": "Eigene Wetter-Position (Prio 4)",
             "r/:/p3": "Text-Relay (Prio 3)"}
    for k, v in tw.items():
        wait_rows.append([names[k], de(v["n"]), de(v["wait_med_s"], 1), de(v["wait_p90_s"], 1)])
    add(table(["Klasse", "Anzahl", "Wartezeit Median (s)", "90. Perzentil (s)"], wait_rows,
              num_cols=(1, 2, 3), caption="Wartezeit je Sendeklasse zwischen Einreihen und Senden"))
    add("<h3>Beobachten: eigene HEY warten länger als Relay-HEY</h3>")
    add("<p><b>Eigene HEY warten im Median "
        f"{de(tw['o/@/p5']['wait_med_s'], 1)} s, Relay-HEY derselben Priorität {de(tw['r/@/p5']['wait_med_s'], 1)} s</b> "
        f"(90. Perzentil {de(tw['o/@/p5']['wait_p90_s'], 1)} gegen {de(tw['r/@/p5']['wait_p90_s'], 1)} s; "
        f"längste Wartezeit eines eigenen HEY {de(lat_prio['5']['max'], 1)} s bei {head_r['p5']['ttl_s']} s TTL). "
        "Die Ursache ist in der Ursachenanalyse "
        '<a href="verify/rca_own_beacons.md">verify/rca_own_beacons.md</a> beschrieben '
        "(Messung hauptsächlich an OE3XOC-12, Code Stand 651e0920):</p>"
        "<ul><li><b>Eigene Bake an der Spitze:</b> sie bekommt die Haltezeit von 5,5 s für Priorität 4 und 5, "
        "und jeder empfangene Frame startet diesen Timer neu (<code>src/lora_functions.cpp:2746-2747</code>).</li>"
        "<li><b>Relay gleicher Priorität:</b> im Fall A (NBR, nur mit <code>--nbrrelay on</code>) fällt die "
        "Haltezeit nach 8 s Wartezeit auf 150 bis 220 ms (<code>src/lora_functions.cpp:3998-4006</code>, "
        "<code>src/txring_functions.cpp:385-405</code>).</li>"
        "<li><b>Eigene Baken sind davon ausgeschlossen</b>, auch vom CSMA-Altersboden "
        "(<code>src/csma_timing.h:99-111</code>). Dazu kommt die strenge Prioritätsfolge: ACKs gehen vor.</li></ul>")
    add("<p>Am OE3XHL-12 ist die Wirkung klein: der Kanal ist ruhiger (19 statt 35 Empfänge je Minute wie an "
        "OE3XOC-12), eigene Köpfe der Warteschlange leeren sich mit 1,9 bis 2,8 je Minute, etwa so schnell wie "
        "Relay-HEY (1,88 je Minute; Ursachenanalyse, Abschnitt 3), "
        f"{hc['user_wx']['tx_air']} von {hc['user_wx']['tx_air']} Wetter-Positionen und "
        f"{de(ev_hey['aired_pct'], 1)} % der eigenen HEY gingen raus. Das Gefälle der Mediane ist hier damit "
        "nur zum Teil erklärt. <b>Vermutung:</b> ein Überlebens-Effekt, denn "
        f"{ev_rhey['fate']['nbr_cancel']} Relay-HEY endeten im Overhear-Cancel, bevor sie sendeten; gezählt "
        "werden nur die gesendeten, und das sind die mit kurzer Wartezeit. Die Ursachenanalyse schließt diesen "
        "Effekt nur für Knoten ohne CANCEL-Zeilen aus. Fix-Kandidat (nicht umgesetzt, ungetestet): den "
        "Altersboden auf Priorität 4 und 5 für Einträge ausdehnen, die keine Relays sind.</p>")
    ehey = (
        bx(10, 14, 290, 62, ["Eigenes HEY (Prio 5)", "Haltezeit 5,5 s, bei jedem Empfang neu"], "d-watch")
        + bx(340, 14, 290, 62, ["Relay-HEY, Fall A (Prio 5)", "nach 8 s nur 150 bis 220 ms"], "d-ok")
        + ln(20, 120, 300, 120, "")
        + dot(60, 120, 5, "d-neu") + dot(125, 120, 5, "d-neu") + dot(190, 120, 5, "d-neu")
        + dot(255, 120, 5, "d-neu anim-pulse")
        + tx(160, 100, "Empfang: Timer startet neu", "d-txt-s")
        + tx(160, 148, "wartet, bis 5,5 s Ruhe herrscht", "d-txt-s")
        + ln(350, 120, 630, 120, "")
        + dot(420, 120, 5, "d-neu") + dot(520, 120, 5, "d-ok anim-pulse")
        + tx(490, 100, "kurze Suche, sendet", "d-txt-s")
        + tx(490, 148, "nach 8 s fast sofort", "d-txt-s")
        + tx(320, 186, f"Wartezeit OE3XHL-12, Median: eigen {de(tw['o/@/p5']['wait_med_s'], 1)} s, "
             f"Relay {de(tw['r/@/p5']['wait_med_s'], 1)} s", "d-txt")
    )
    add(explain(ehey, cap("ehey", "Eigene Baken warten auf 5,5 s Ruhe, Relays gleicher Priorität dürfen nach 8 s fast sofort senden"),
                height=206))
    add("<p>Weitere Befunde: 0 doppelte Eigensendungen innerhalb von 30 min (an OE3XIA-12 kommen 12 vor); "
        f"die Wartezeit der ACKs ist hier mit {de(lat_prio['1']['p50'], 1)} s die kürzeste der Gruppe "
        "(Median IA 21,9 s, IR 20,9 s, OC 23,3 s; Quelle verify/q2.md, Zeile 6).</p>")

    # ================= F3 =================
    add('<h2 id="f3">F3 Was verlieren wir durch den Zeitverfall</h2>')
    add(f"<p><b>Fast nichts: in {de(span['hours'], 0)} Stunden verfielen {fc['stale_ttl']} Einträge "
        f"({pct(fc['stale_ttl'], tot_w, 2)} der Einträge), alle HEY. "
        f"Kein Text, kein ACK, kein Relay-Text und kein {CET}-Telegramm ging verloren.</b> TTL "
        "(Time to Live) ist die Höchstdauer, die ein ungesendeter Eintrag im Puffer bleibt: "
        "HEY 180 s, Position 120 s, ACK 90 s, Relay-Text 300 s.</p>")
    se = fate["stale_events"]
    drop_rows = [
        [e["t"][8:10] + "." + e["t"][5:7] + ". " + e["t"][11:], e["id"],
         "eigenes HEY" if e["src"] == "auto_pos" else "Relay-HEY", f"{e['age_s']} s",
         "nie gesendet"] for e in se
    ]
    add(table(["Zeit (Logger)", "Msg-ID", "Art", "Alter beim Verwerfen", "Gesendet?"],
              drop_rows, caption="Alle drei Verfalls-Drops; kein [LOG] TX und kein RING_TX_READ "
                                 "mit derselben ID vor dem Drop"))
    hc = fate["by_src"]
    add("<ul>"
        f"<li><b>Eigene HEY:</b> {ev_hey['chains']} Ketten, {ev_hey['fate']['tx_air']} gesendet "
        f"({de(ev_hey['aired_pct'], 1)} %), {ev_hey['fate']['stale_ttl']} verfallen. Zwischen zwei "
        f"gesendeten HEY liegen im Median {de(Q2['extras']['own_hey_gap_s']['p50'] / 60, 1)} min, "
        f"höchstens {de(Q2['extras']['own_hey_gap_s']['max'] / 60, 1)} min.</li>"
        f"<li><b>Eigene Wetter-Position:</b> {hc['user_wx']['tx_air']} von {hc['user_wx']['tx_air']} gesendet, 0 verfallen. "
        "Ein Internet-Uplink des Wetter-Frames ist im Log nicht zu sehen (0 TX-UDP-Zeilen).</li>"
        f"<li><b>Relay-Verlust:</b> 1 Relay-HEY ({[e for e in se if e['src'] == 'rx_relay'][0]['id']}, "
        f"{[e for e in se if e['src'] == 'rx_relay'][0]['t'][11:]}). Ein anderer der fünf "
        "Knoten hatte die ID schon vor unserem Einreihen gesendet (Klasse „schwach“: kein "
        "Nachweis der Weitergabe); der Absender meldete sich binnen "
        f"{de(Q34['relay_refresh_median_s'])} s mit einem neueren HEY.</li>"
        f"<li><b>Zeitsignal:</b> 0 Verwürfe wegen des 150-s-Tors, 0 Verfall; das älteste eigene "
        f"Telegramm war {de(Q5['tx_age_s_logger_based']['max'], 1)} s alt.</li></ul>")
    add("<h3>Beobachten: eigene Wetter-Position nahe an der TTL</h3>")
    add(f"<p>Die Wetter-Position (Prio 4, alle 30 min) wartete höchstens {de(head_r['p4']['max'], 1)} s bei "
        f"{head_r['p4']['ttl_s']} s TTL; "
        f"{head_r['p4']['n_ge80pct_ttl_sent']} von {head_r['p4']['n_sent']} Positionen über 80 % der "
        "TTL. Hier hat es gereicht. Bei OE3XOC-12 verfielen 34 von 49 Wetter-Positionen; bei "
        "mehr Last könnte das auch hier passieren (Vermutung).</p>")
    e3 = (
        ln(40, 110, 600, 110, "")
        + '<rect class="d-box d-watch" x="40" y="86" width="537" height="24" rx="4"/>'
        + tx(308, 103, "Wetter-Position wartet hinter ACKs und Relays", "d-txt-s")
        + ln(124, 70, 124, 110, "d-acc-s") + tx(124, 62, f"Median {de(tw['o/!/p4']['wait_med_s'], 1)} s")
        + ln(469, 70, 469, 110, "d-acc-s") + tx(469, 62, f"90. Perz. {de(tw['o/!/p4']['wait_p90_s'], 1)} s")
        + ln(577, 40, 577, 110, "d-watch-s") + tx(577, 32, f"Maximum {de(head_r['p4']['max'], 1)} s", "d-txt", "end")
        + ln(600, 70, 600, 150, "d-act-s", "anim-pulse") + tx(600, 172, "TTL 120 s: danach verworfen",
                                                                 "d-txt", "end")
        + tx(40, 140, "0 s", "d-txt-s", "start") + tx(40, 172, f"Alle {hc['user_wx']['tx_air']} Wetter-Positionen wurden noch gesendet",
                                                       "d-txt-s", "start")
    )
    add(explain(e3, cap("e3", f"Die Wetter-Position erreichte {de(head_r['p4']['max'])} s von {head_r['p4']['ttl_s']} s Lebensdauer, gesendet wurde sie noch"),
                height=190))

    add("<h3>CSMA-Altersboden</h3>")
    add("<p>Der Altersboden verkürzt die Haltezeit vor dem Senden, wenn ein ACK oder Relay-Text "
        "(Prio 1 bis 3) länger als 20 s im Puffer liegt (Stufe 1) oder 60 s (Stufe 2). "
        "CSMA ist das Prinzip „erst horchen, dann senden“.</p>")
    add("<ul>"
        f"<li><b>Menge:</b> {csma['events']} Ereignisse in {csma['episodes']} Slot-Episoden "
        f"({csma['ep_prio']['1']} ACK, {csma['ep_prio']['3']} Relay-Text). Stufe 2 trat nie auf.</li>"
        f"<li><b>Betroffen:</b> {csma['tx_prio_le3_wait_ge20s']} von {de(csma['tx_prio_le3'])} Sendungen "
        f"mit Prio 1 bis 3 ({de(csma['tx_prio_le3_wait_ge20s_pct'], 1)} %) warteten 20 s oder länger, "
        "keine 60 s. An OE3XIA-12, OE3XIR-12 und OE3XOC-12 sind es 51 bis 63 %.</li>"
        f"<li><b>Wirkung:</b> Haltezeit im Median {de(csma['hold_ms']['p50'])} ms. Nach dem ersten "
        f"Ereignis ging der Slot im Median {de(csma['replay']['real_remaining_after_first_aged_event_s']['p50'], 1)} s "
        "später auf Sendung. Modell: etwa 0,6 bis 1,6 s gespart je Episode, je nach Versuchsnummer "
        "(die Versuchsnummer steht nicht im Log).</li>"
        f"<li><b>Kollisionszeichen:</b> Fehlerempfang binnen 3 s nach eigener Sendung bei "
        f"gealterten ACKs {de(csma['probe_p1_aged']['pct_rxerr_within_3s_after_tx'], 1)} %, bei "
        f"frischen {de(csma['probe_p1_fresh']['pct_rxerr_within_3s_after_tx'], 1)} %, Zufallsrate "
        f"{de(csma['baseline_pct_rxerr_in_3s'], 1)} %. Kein Unterschied; wirkt neutral.</li></ul>")
    ht = [[k.upper().replace("P", "Prio "), f"{v['ttl_s']} s", de(v["n_sent"]), de(v["max"], 1),
           de(v["n_dropped"])] for k, v in head_r.items()]
    add(table(["Klasse", "TTL", "Gesendet", "Längste Wartezeit (s)", "Verworfen"], ht,
              num_cols=(1, 2, 3, 4), caption="Reserve bis zur TTL je Priorität"))

    # ================= F4 =================
    add('<h2 id="f4">F4 Was gewinnen wir durch den Zeitverfall</h2>')
    add("<p><b>An diesem Knoten fast nichts, weil kaum etwas verfällt. Der Gewinn wäre Frische, "
        "keine Entlastung des Kanals.</b></p>")
    add("<ul>"
        f"<li><b>Sendezeit:</b> die drei verworfenen Frames hätten {de(Q34['drop_toa_s'], 2)} s "
        f"gekostet, {de(Q34['drop_toa_pct_of_tx'], 2)} % der eigenen {de(Q34['tx_toa_s'], 1)} s "
        "Sendezeit.</li>"
        f"<li><b>Slots:</b> {Q34['cf']['stale_slot_seconds_real']} Slot-Sekunden belegt ({fc['stale_ttl']} Drops "
        f"mit je rund 181 s). Der Puffer erreichte höchstens {int(occ['max'])} von 20 Slots.</li>"
        "<li><b>Gegenrechnung ohne TTL:</b> nicht belastbar. Das Modell (Wiedergabe der Priorität "
        "ohne TTL) trifft die echte Reihenfolge nur zu "
        f"{de(Q34['cf']['baseline_pick_match_pct'], 1)} %, weil es den Overhear-Cancel nicht kennt. "
        "Ich verwende es für OE3XHL-12 nicht.</li>"
        f"<li><b>Echter Nutzen an anderer Stelle:</b> das Ersetzen (Supersede) lässt von {Q2['supersede']['events']} Ketten "
        f"{Q2['supersede']['chain_tail']['tx_air']} in aktueller Form senden, und die Abbrüche befreien "
        f"{fc['nbr_cancel']} Puffer-Einträge, ohne auf die TTL zu warten.</li></ul>")

    # ================= F5 =================
    add('<h2 id="f5">F5 Zeitsignal</h2>')
    add(f"<p><b>Das Zeitsignal ({CET}-Telegramm des Servers OE1XAR-33, alle 607 s) kam vollständig "
        "an und wurde nie veraltet weitergegeben.</b> Der Server ist kein Knoten; der Inhalt ist "
        "UTC, obwohl er „CET“ heißt (Kurzbefund der Gruppe).</p>")
    add("<ul>"
        f"<li><b>Eingang:</b> {Q5['udp_got']} von {Q5['expected_in_window']} Telegrammen über den Internetweg (UDP); die {Q5['udp_missed']} fehlenden "
        "(6AE5897E um 17:20:27 UTC, 6AE5898D um 19:52:14 UTC; Quelle q5/findings.md) kamen per Funk in 2 bis 9 s und "
        f"wurden weitergegeben. Auf Funk wurden {Q5['rf_copies_per_id']['n']} von {Q5['expected_in_window']} gehört.</li>"
        f"<li><b>Funkkopien je Telegramm:</b> Mittel {de(Q5['rf_copies_per_id_mean'], 1)}, Median {de(Q5['rf_copies_per_id']['p50'])}, "
        f"höchstens {Q5['rf_copies_per_id']['max']}.</li>"
        f"<li><b>Eigene Sendung:</b> {cet_tx} {CET}-Sendungen ({Q5['tx_src_dist']['g']} als Gateway, {Q5['tx_src_dist']['r']} als Relay). Alter "
        f"beim Senden im Median {de(Q5['tx_age_s_logger_based']['p50'], 1)} s, 90. Perzentil "
        f"{de(Q5['tx_age_s_logger_based']['p90'], 1)} s, Maximum {de(Q5['tx_age_s_logger_based']['max'], 1)} s; "
        f"{Q5['tx_age_gt120_n']} über 120 s, {Q5['tx_age_gt150_n']} über dem 150-s-Tor. Wartezeit im Puffer höchstens "
        f"{de(Q5['tx_wait_ms']['max'] / 1000, 1)} s.</li>"
        f"<li><b>Doppelte Abstrahlung:</b> {gw_tx_after} von {cet_tx} Sendungen "
        f"({de(Q5['tx_after_overheard_pct'], 1)} %) gingen raus, nachdem der Knoten dieselbe ID schon "
        f"von einem anderen Gateway gehört hatte (Vorsprung im Median {de(Q5['tx_after_overheard_lead_s']['p50'], 1)} s). "
        "Das ist der niedrigste Wert der Gruppe (andere "
        f"{de(min(v['tx_after_overheard_pct'] for k, v in Q5all.items() if k not in (NODE, 'db0ais')))} bis "
        f"{de(max(v['tx_after_overheard_pct'] for k, v in Q5all.items() if k not in (NODE, 'db0ais')))} %).</li>"
        f"<li><b>Kosten:</b> {de(Q5['cet_tx_airtime_s_per_h'], 1)} s je Stunde Sendezeit "
        f"({de(Q5['rawpass']['tx_cet_share_airtime_pct'], 1)} % der Sendezeit), {de(Q5['cet_rx_airtime_s_per_h'], 1)} s je "
        f"Stunde Empfang ({de(Q5['rawpass']['rx_cet_share_of_decoded_airtime_pct'], 1)} % der Empfangszeit).</li>"
        f"<li><b>Uhr:</b> Knotenuhr = Logger minus 1 s, NTP {Q5['clock']['ntp_ok']}-mal ok, 7 Timeouts "
        "(Quelle q5/findings.md); die Altersrechnung stimmt (Alter beim Einspeisen "
        f"{de(Q6['cet']['gw_rx_age_median'])} s, Median).</li></ul>")
    add("<h3>Beobachten: Server-Frames werden nie abgebrochen</h3>")
    add(f"<p>Bei jedem {CET} und jeder Server-Einspeisung ({Q6['totals']['gwi']}) meldet der Knoten „Fall A“: er sei "
        "einziger Versorger mindestens einer Station. Fall A wird nie abgebrochen. Es gab 0 CANCEL "
        f"für {CET}, obwohl der Knoten sonst {de(n_cancel)}-mal abbricht. Wirkung hier klein: die "
        "Sendung des Knotens wird von den anderen vier OE3-Knoten gar nicht gehört. Siehe "
        "Verdachtsfälle.</p>")
    add("<h3>Beobachten: veraltete Zeit von OE3GJC-12 als Gateway</h3>")
    ages_g = sorted(r["age_s"] for r in stale_gjc)
    add(f"<p>Von {de(xh['cet_rf_copies_parsed'])} Funkkopien waren {len(stale)} älter als 150 s "
        f"({pct(len(stale), xh['cet_rf_copies_parsed'], 2)}). <b>{len(stale_gjc)} davon kamen von OE3GJC-12</b> "
        f"als Gateway-Abstrahlung (Pfad OE1XAR-33, OE3GJC-12) mit einem Alter von {de(ages_g[0])} bis "
        f"{de(ages_g[-1])} s. OE3GJC-12 ist ein {gjc['hardware']} mit Firmware {gjc['firmware']}, "
        f"{gjc['description']}, {de(float(gjc['lat']), 4)} N, {de(float(gjc['lon']), 4)} E, {gjc['altitudeM']} m, "
        f"{de(gjc_km, 1)} km von hier (Stammdaten mcmap) und der Relayer, den dieser Knoten am häufigsten "
        "direkt hört. Eine sechste Kopie "
        f"({de(stale_other[0]['age_s'])} s, Pfad OE1XAR-33, OE3XHL-12, OE3HUS-01) trägt unseren Ruf im Pfad: "
        "Es ist unsere eigene, zeitnah gesendete Abstrahlung, die OE3HUS-01 später noch einmal weitergab "
        "(Vermutung: Relay ohne {CET}-Tor oder mit längerer Warteschlange). Unser Knoten gab nichts davon "
        "weiter (alle sechs als Duplikat erkannt, DUP:d). Bewertung: Beobachten, Entscheidung beim "
        "Betreiber von OE3GJC-12.</p>".replace("{CET}", CET))
    add("<p><b>Zwei Vermutungen zur Ursache</b> (aus dem Log nicht zu unterscheiden): <b>(1)</b> Der Knoten "
        "läuft mit einem Build ohne das 150-s-Tor, also vor Commit 05a18b36 (v4.40a.10.23, upstream über "
        "#1238). Ohne Tor strahlt ein Gateway ein verspätet eingetroffenes Telegramm unverändert ab. "
        "<b>(2)</b> Das Tor ist vorhanden, aber die vertrauenswürdige Uhr des Knotens läuft um Minuten "
        "nach; dann erscheint das Telegramm jünger, als es ist. Eine nicht vertrauenswürdige Uhr würde "
        "dagegen sperren (das Tor schlägt dann geschlossen fehl, <code>src/cet_age.h:129</code>). "
        "Prüfen lässt sich das nur am Knoten selbst: Build-Stand und Uhrzeit abfragen.</p>")
    dots = "".join(dot(40 + 560 * min(r["age_s"], 380) / 380.0, 80, 6, "d-act" + (" anim-pulse" if r is stale_gjc[0] else ""))
                   for r in stale_gjc)
    gate_x = 40 + 560 * 150 / 380.0
    e7 = (
        ln(40, 80, 600, 80, "")
        + ln(gate_x, 50, gate_x, 110, "d-act-s", "anim-pulse")
        + tx(gate_x, 40, "Tor 150 s", "d-txt")
        + dots
        + tx(40, 108, "0 s", "d-txt-s", "start") + tx(600, 108, "380 s", "d-txt-s", "end")
        + tx(320, 135, "Alter beim Senden: " + ", ".join(de(a) for a in ages_g) + " s", "d-txt-s")
        + tx(320, 165, f"{len(stale_gjc)} Abstrahlungen von OE3GJC-12 waren aelter als das Tor", "d-txt-s")
        + tx(320, 188, "Unser Knoten leitete sie nicht weiter (Duplikat)", "d-txt-s")
    )
    add(explain(e7, cap("e7", f"OE3GJC-12 strahlte {len(stale_gjc)}-mal ein Zeitsignal ab, das älter als 150 s war"),
                height=210))

    # ================= F6 =================
    add('<h2 id="f6">F6 Manipulationen dieses Knotens</h2>')
    add("<p><b>Was der Knoten am Verkehr ändert, nützt überwiegend: Dedup, Hop-Zähler, "
        "Overhear-Cancel. Schwachpunkt sind die ACK-Relays.</b> Zahlen aus der Q6-Auswertung; Anteile "
        "an den Sendungen sind je Knoten oder je Cluster ausgewiesen.</p>")
    tt = Q6["totals"]
    sw = Q6["stat_windows"]
    cev = Q6all["network"]["cancel_eval_oe3xhl"]
    pos_items = [
        f"<b>Overhear-Cancel:</b> {de(cancel_lines)} CANCEL-Zeilen, {de(cancel_matched)} davon sauber einem "
        f"Relay zugeordnet ({cancel_share} der {de(rly_q_tx)} Relays); etwa 537 s Sendezeit gespart "
        f"(HEY {cev['by_type']['H']}, Position {cev['by_type']['P']}, Text {cev['by_type']['T']} "
        "Abbrüche; 537 s aus q6/findings.md, Modellrechnung).",
        f"<b>Abbruch rechtzeitig:</b> im Median {de(fate['cancel_age_s']['p50'], 1)} s nach dem "
        "Einreihen; 20 von 20 Abbrüchen, die OE3XIA-12 nennen, passen zu einer Sendung von OE3XIA-12 "
        "innerhalb von 5 s.",
        f"<b>Fall-B-Wartesperre:</b> {de(n_need_b)} von {de(n_need_a + n_need_b)} Relay-Entscheidungen "
        f"({pct(n_need_b, n_need_a + n_need_b)}) im Fall B; {pct(n_cancel, n_need_b)} davon endeten im Abbruch.",
        f"<b>Dedup (Mehrfach-Empfang verwerfen):</b> {de(sw['dup'])} Duplikate gegen "
        f"{de(sw['newid'])} neue IDs ({de(sw['dup_per_new'], 2)} je neu, ACKs eingeschlossen). "
        f"Fenster etwa {de(Q6['loops']['dedup_window_min_est'])} min ({Q6['loops']['dedup_ring_slots']} Plätze "
        f"bei {de(Q6['loops']['dedup_adds_per_min'], 1)} Einträgen je Minute, eigene Rechnung).",
        f"<b>Hop-Grenze:</b> {tt['rly_hop0']} Frames am Ende der Flut (hop0), {tt['rly_gwcap']} durch "
        f"die Gateway-Pfadgrenze, {tt['rly_gwfilter']} Ping/Pong gefiltert.",
        f"<b>Ersetzen:</b> {tt['supersede']} alte HEY durch neuere ersetzt.",
        "<b>Schleifenschutz:</b> 0 Auslösungen nötig, keine Schleife, kein Ping-Pong.",
    ]
    neg_items = [
        f"<b>ACK-Relays ohne Abbruch:</b> {de(ack_fwd)} ACKs ({pct(ack_fwd, n_tx)} der Sendungen dieses Knotens), "
        f"{de(ack_dup_fwd)} davon ({pct(ack_dup_fwd, ack_fwd, 0)}) gesendet, nachdem ein Duplikat "
        f"gehört wurde ({de(Q6['opportunity_airtime']['opportunity_air_s_by_type']['A'])} s "
        "Sendezeit, Obergrenze).",
        f"<b>Ping-Last im Cluster:</b> ACK-Relays allein sind {pct(cl_ack, cl_tx)} aller Sendungen der "
        f"fünf OE3-Knoten ({de(cl_ack)} von {de(cl_tx)}; 20,2 % der Sendezeit, CORE-FINDINGS.md). Zusammen mit den "
        f"{de(cl_ping)} eigenen Pings von OE3XWJ-12 sind es {pct(cl_ack + cl_ping, cl_tx)} der Sendungen und "
        f"{pct(psh['ping_air_s'], cl_air)} der Sendezeit. Die Zahl dieses Knotens ({pct(ack_fwd, n_tx)}) ist eine "
        "andere Größe: sie bezieht sich auf seine eigenen Sendungen.",
        f"<b>Ping-Flut von außen:</b> {de(f5ce7['events'])} von {de(ack_ev)} empfangenen ACKs "
        f"({de(f5ce7['share_pct'], 1)} %) bestätigen das {PING} von OE3XWJ-12. ACKs belegen "
        f"{de(ack_pct_all, 1)} % der Empfangszeit. Die ACKs stammen von fremden Gateways "
        "(Gateway-ACK-Format, im Mittel 2,9 verschiedene ACK-IDs je Ping-Ereignis, Median 3, Maximum 8; Quelle data/extra/oe3xwj_ping.json; die 4,6 aus verify/q6.md zählen je Message-ID, die über 24 h doppelt vergeben wird).",
        f"<b>Angenommene Abdeckung:</b> {cev['inferred_nonzero']} von {de(cev['cancels'])} Abbrüchen "
        f"({pct(cev['inferred_nonzero'], cev['cancels'], 0)}) stützen sich auf eine "
        f"Symmetrie-Annahme; {cev['unobserved']} von {de(cev['edge_pairs'])} Relayer-Hörer-Paaren haben keine "
        f"beobachtete Kante, {cev['weak_cnt_le2']} nur 1 bis 2 Treffer (Quelle: Q6-Auswertung).",
        f"<b>Sendung trotz gehörter Kopie:</b> {opp_air['opportunity_air_s_total']} s "
        f"({de(opp_air['opportunity_share_of_node_tx_air_pct'], 1)} % der Sendezeit) gingen raus, "
        "obwohl eine fremde Kopie schon gehört wurde (Obergrenze, nicht Einsparung).",
        f"<b>Server-Einspeisungen:</b> {tt['gwi']} von {tt['gwi']} gesendet, 0 Abbruch (Fall A).",
    ]
    neu_items = [
        "<b>Pfad anhängen:</b> jedes Relay hängt den eigenen Ruf an und senkt den Hop-Zähler um 1; "
        "ein HEY wächst dabei von 31 auf etwa 72 Byte.",
        f"<b>Uploads zum Server:</b> {de(tt['gwu'])} Gateway-Uploads, ohne Funkkosten.",
        f"<b>Echo-Tabelle:</b> {tt['echo']} eigene Baken, davon 7 nie als Echo gehört (q6/findings.md M16).",
        "<b>Funkinsel:</b> die Sendungen dieses Knotens (ohne ACKs) hört im Cluster praktisch niemand: "
        f"OE3XIA-12 {lp['oe3xhl->oe3xia']['heard_nonack']} von {de(lp['oe3xhl->oe3xia']['tx_nonack'])} "
        f"({de(100 * lp['oe3xhl->oe3xia']['p_nonack'], 1)} %), OE3XIR-12, OE3XOC-12 und OE3XWJ-12 je 0. "
        f"Er selbst hört OE3XIA-12 ({de(lp['oe3xia->oe3xhl']['p_nonack'], 2)} Anteil, "
        f"{xh_ia} Empfänge mit OE3XIA-12 als letztem Funkstück), aber weder OE3XIR-12 noch OE3XOC-12 noch OE3XWJ-12 "
        "direkt. Ob abgebrochene Relays Hörer verfehlen, lässt sich deshalb nicht prüfen. Die "
        f"Empfangspausen von OE3XIA-12 ({ia_sil} über 5 min) erklären die {lp['oe3xhl->oe3xia']['heard_nonack']} "
        "von 2.235 nicht.",
        "<b>Entscheidungswechsel:</b> in 31 % der Folgen wechselt der Fall A/B zwischen zwei "
        "Relays (verschiedene Absender), kein Flattern je Frame.",
    ]

    def cards(items: list[str], cls: str, title: str) -> str:
        return (f'<div class="card {cls}"><b>{title}</b><ul>'
                + "".join(f"<li>{x}</li>" for x in items) + "</ul></div>")

    add('<div class="grid3">' + cards(pos_items, "pos", "Positiv")
        + cards(neg_items, "neg", "Negativ") + cards(neu_items, "neu", "Neutral") + "</div>")

    rows_tx = []
    for k, label in [("r:A", "ACK-Relay"), ("r:@", "HEY-Relay"), ("r:!", "Positions-Relay"),
                     ("r::", "Text-Relay"), ("g::", "Server-Einspeisung"),
                     ("o:@", "Eigenes HEY"), ("o:!", "Eigene Wetter-Position")]:
        rows_tx.append([label, de(own["src_type"][k]), pct(own["src_type"][k], n_tx, 2)])
    rows_tx.append(["Summe", de(n_tx), "100,00 %"])
    add(table(["Sendeklasse", "Anzahl", "Anteil an den Sendungen"], rows_tx, num_cols=(1, 2),
              caption="Zusammensetzung der 4.375 Sendungen"))
    add(bars([("gesendet", relay_fate["sent"]), ("abgebrochen (Cancel)", relay_fate["cancel"]),
              ("ersetzt", relay_fate["supersede"]), ("TTL", relay_fate["stale"]),
              ("ungeklärt", relay_fate["unaccounted"])],
             cap("fate", f"Von {de(relay_n)} Relay-Einträgen wurden {de(n_cancel)} abgebrochen (+ {relay_fate['unaccounted']} verschränkte CANCEL-Zeilen)"),
             unit=""))
    ph = Q6["per_hour"]
    add(hist([(k[-2:], v["cancel"]) for k, v in ph.items()],
             cap("cancelh", "Abbrüche je Stunde: Spitzen um 09 Uhr (" + str(ph["10-31 09"]["cancel"]) + ") und 23 Uhr (" + str(ph["10-31 23"]["cancel"]) + ")"), unit=" Abbrüche",
             ylabel="Stunde (Logger-Uhr), Beginn 31.10. 06 Uhr"))
    add("<p>Die Abbrüche betrafen vor allem Relays von OE3GJC-12 (292) und OE3MAG-10 (217); "
        "danach OE3XIA-12 (20), OE3RDB-2 (16), OE3HUS-01 (14), OE3HES-1 (11), OE3ZKW-12 (10) und "
        "OE1XAR-12 (7). Typen: HEY 457, Position 117, Text 13.</p>")

    e2 = (
        bx(10, 14, 170, 62, ["OE3XWJ-12 (RAK4631)", PING + " je Minute"], "d-act")
        + ln(180, 45, 225, 45, "d-act-s", "anim-flow") + head(235, 45, "d-act-f")
        + bx(240, 14, 170, 62, ["Fremde Gateways", "ca. 2,9 ACK-IDs je Ping"], "d-neu")
        + ln(410, 45, 445, 45, "d-act-s", "anim-flow") + head(455, 45, "d-act-f")
        + bx(460, 14, 170, 62, ["OE3XHL-12", "leitet ACKs weiter"], "d-watch")
        + tx(320, 108, f"{de(cl_ping)} Pings, {de(f5ce7['events'])} ACK-Empfaenge hier ({de(f5ce7['share_pct'], 1)} % aller ACKs)", "d-txt-s")
        + tx(320, 130, f"{de(ack_fwd)} ACK-Relays = {pct(ack_fwd, n_tx)} der Sendungen dieses Knotens", "d-txt-s")
        + tx(320, 152, f"Cluster: ACK-Relays allein {pct(cl_ack, cl_tx)}, mit den Pings {pct(cl_ack + cl_ping, cl_tx)}", "d-txt-s")
        + tx(320, 174, "Kein Abbruch moeglich: ACKs gelten im Puffer nicht als Relay", "d-txt-s")
    )
    add(explain(e2, cap("e2", "Ein Ping je Minute löst mehrere ACKs fremder Gateways aus; jeder neue wird weitergeleitet"),
                height=192))

    e6 = (
        bx(10, 10, 170, 44, ["OE3GJC-12", "sendet das HEY schon"], "d-neu")
        + bx(235, 10, 170, 44, ["OE3XHL-12", "Relay wartet im Puffer"], "d-watch")
        + ln(180, 32, 225, 32, "") + head(235, 32, "d-acc-f")
        + ln(270, 54, 130, 142, "d-ok-s") + ln(320, 54, 320, 142, "d-ok-s")
        + ln(370, 54, 510, 142, "d-watch-s", dash=True)
        + dot(130, 158, 15, "d-ok") + dot(320, 158, 15, "d-ok") + dot(510, 158, 15, "d-watch")
        + tx(130, 195, "Hoerer A", "d-txt-s") + tx(320, 195, "Hoerer B", "d-txt-s")
        + tx(510, 195, "Hoerer C", "d-txt-s")
        + tx(170, 108, "beobachtet", "d-txt-s", "end") + tx(490, 100, "angenommen", "d-txt-s", "start")
        + tx(560, 40, f"{cev['inferred_nonzero']} von {cev['cancels']} Abbruechen", "d-txt-s")
        + tx(560, 58, "beruhen auf Annahme", "d-txt-s")
    )
    add(explain(e6, cap("e6", f"{pct(cev['inferred_nonzero'], cev['cancels'], 0)} der Abbrüche setzen voraus, dass der Nachbar die Hörer auch erreicht"),
                height=215))

    # ================= Bugs =================
    add('<h2 id="bugs">Verdachtsfälle Firmware</h2>')
    add("<p><b>Eine Plattform-Differenz mit offener Entscheidung und zwei Beobachtungen, keine davon "
        "ein bewiesener Fehler.</b> Die beiden Beobachtungen betreffen die Auswahl, wann ein Relay "
        "abgebrochen wird.</p>")
    add("<h3>V0: Auto-Ping ohne Obergrenze auf RAK4631 (Unterschied ESP32 / RAK4631, Entscheidung offen)</h3>")
    add("<ul>"
        f"<li><b>Beobachtung:</b> OE3XWJ-12 sendet {de(cl_ping)}-mal {PING} an Gruppe 2025, im Takt von 60 s. "
        "OE3XIR-12 sendet dasselbe an das Ziel „255.“ (689-mal, jedes Mal am Decoder verworfen). Beides sind "
        "Auto-Pings, die auf RAK4631 nie enden.</li>"
        "<li><b>Beleg:</b> ESP32 sendet nur, solange der Zähler größer 0 ist, und zählt bei jedem Ping herunter; "
        "er stoppt nach <code>node_pingmax</code> Pings. Die RAK4631-Schleife hat keinen Zähler. "
        "<code>--ping stop</code> setzt den Zähler auf 0 und wirkt auf RAK4631 deshalb nicht; nur das Löschen von "
        "<code>--pingcall</code> beendet den Auto-Ping.</li>"
        "<li><b>Code-Stelle:</b> <code>src/nrf52/nrf52_main.cpp:2717-2737</code> (kein Zählerzugriff) gegen "
        "<code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code>; <code>src/command_functions.cpp:3886</code>. "
        "Gelesen am Stand 651e0920; der Upstream-Stand dev 147812e0 hat denselben Code.</li>"
        "<li><b>Status:</b> Unterschied zwischen den Plattformen, <b>Entscheidung offen</b>, welches Verhalten "
        "gewollt ist (Entscheidung des Betreibers, kein Fehler). Die Folge im Log ist die ACK-Flut dieses "
        "Berichts; die Last hat drei Hebel: den Ping-Test von OE3XWJ-12, diese Plattform-Differenz und die "
        "Firmware der fremden Gateways, die die ACKs erzeugen (Vermutung: sie laufen mit einem Stand, der "
        "<code>{ping}</code> beantwortet; am Stand 651e0920 sendet ein Gateway dafür kein ACK, "
        "<code>src/lora_functions.cpp:2169-2180</code>).</li></ul>".replace("{ping}", PING))
    eping = (
        bx(10, 14, 190, 52, ["ESP32-Schleife", "Zaehler > 0 noetig"], "d-ok")
        + ln(200, 40, 235, 40, "") + head(245, 40, "d-acc-f")
        + bx(250, 14, 170, 52, ["sendet Ping", "Zaehler minus 1"], "d-ok")
        + ln(420, 40, 445, 40, "") + head(455, 40, "d-acc-f")
        + bx(460, 14, 170, 52, ["stoppt nach", "node_pingmax Pings"], "d-ok")
        + bx(10, 94, 190, 52, ["RAK4631-Schleife", "kein Zaehler"], "d-watch")
        + ln(200, 120, 235, 120, "d-watch-s", "anim-flow") + head(245, 120, "d-watch-f")
        + bx(250, 94, 170, 52, ["sendet Ping alle", "node_pingtime s"], "d-watch")
        + ln(420, 120, 445, 120, "d-act-s", "anim-flow") + head(455, 120, "d-act-f")
        + bx(460, 94, 170, 52, ["ohne Ende", "--ping stop wirkt nicht"], "d-act")
        + tx(320, 178, "Nur das Loeschen von --pingcall beendet den Auto-Ping auf RAK4631", "d-txt-s")
    )
    add(explain(eping, cap("eping", "ESP32 zählt Pings herunter und stoppt, RAK4631 pingt ohne Zähler weiter (Entscheidung offen)"),
                height=196))
    add("<h3>V1: ACK-Relays haben keinen Abbruchpfad</h3>")
    add("<ul>"
        f"<li><b>Beobachtung:</b> {de(ack_dup_fwd)} von {de(ack_fwd)} ACK-Relays ({pct(ack_dup_fwd, ack_fwd, 0)}) "
        "wurden gesendet, nachdem ein Duplikat desselben ACK gehört worden war, während es wartete.</li>"
        f"<li><b>Beleg:</b> RING_WRITE mit Quelle rx_ack_fwd ({de(ack_fwd)}), Zuordnung über Msg-ID "
        "(Q6 Abschnitt 3).</li>"
        "<li><b>Code-Stelle:</b> <code>src/lora_functions.cpp:657-661</code> (ACK wird mit "
        "<code>addTxRingEntry(..., \"rx_ack_fwd\")</code> eingereiht), "
        "<code>:1358-1361</code> (Abbruch-Suche überspringt alles außer Ring-Art Relay), "
        "gelesen am Stand 651e0920.</li>"
        "<li><b>Status:</b> Verhalten bestätigt (Code gelesen), ob gewollt: Verdacht.</li></ul>")
    e4 = (
        bx(10, 20, 190, 62, ["Puffer-Slot", "ACK X wartet"], "d-watch")
        + bx(235, 20, 170, 62, ["Abbruch-Suche", "liest nur Art Relay"], "d-neu")
        + bx(440, 20, 190, 62, ["ACK X wird gesendet", f"{ack_dup_fwd} von {de(ack_fwd)} nach Kopie"], "d-act")
        + ln(200, 51, 225, 51, "") + head(235, 51, "d-acc-f")
        + ln(405, 51, 430, 51, "d-act-s", "anim-flow") + head(440, 51, "d-act-f")
        + bx(10, 115, 190, 50, ["Fremde Kopie von X", "wird gehoert"], "d-ok")
        + ln(105, 115, 105, 102, "d-ok-s", "anim-flow") + head_up(105, 82, "d-ok-f")
        + tx(425, 128, "ACK-Slot zaehlt als Art OTHER:", "d-txt-s")
        + tx(425, 148, "die Suche ueberspringt ihn, es gibt keinen Abbruch", "d-txt-s")
    )
    add(explain(e4, cap("e4", "Die Abbruch-Suche überspringt ACK-Einträge, deshalb wird auch nach gehörter Kopie gesendet"),
                height=185))
    add("<h3>V2: Server-Slots gelten immer als „einziger Versorger“ (Fall A)</h3>")
    add("<ul>"
        f"<li><b>Beobachtung:</b> {tt['gwi']} Server-Einspeisungen, alle Fall A, alle gesendet, 0 CANCEL; "
        f"{gw_tx_after} von {cet_tx} {CET}-Sendungen kamen nach einer gehörten Abstrahlung.</li>"
        "<li><b>Beleg:</b> [NBR]|NEED-Zeile hinter jedem RING_WRITE mit Quelle udp_rx, Fall A in "
        "100 % der Gruppe (666 von 666), Q5 Abschnitt 2.</li>"
        "<li><b>Code-Stelle:</b> <code>src/gw_udp_ring.h:49-70</code> (NEED-Zeile für Server-Slot), "
        "<code>src/nbr_matrix.h:799-809</code> (Vertrag von nbrServerNeed), "
        "<code>src/lora_functions.cpp:1376-1384</code> (REFUSE bei nicht leerem alone).</li>"
        "<li><b>Status:</b> Verdacht. Ob die Menge „alone“ für den Server-Slot richtig gefüllt wird, "
        "lässt sich aus dem Log nicht prüfen.</li></ul>")
    e5 = (
        bx(10, 20, 140, 62, ["Server", CET + " / Text"], "d-neu")
        + bx(190, 20, 170, 62, ["OE3XHL-12", "Server-Slot"], "d-watch")
        + bx(400, 20, 230, 62, ["Fall A: alone nicht leer", "wird nie abgebrochen"], "d-act")
        + ln(150, 51, 180, 51, "", "anim-flow") + head(190, 51, "d-acc-f")
        + ln(360, 51, 390, 51, "d-act-s", "anim-flow") + head(400, 51, "d-act-f")
        + tx(320, 118, f"{tt['gwi']} von {tt['gwi']} Server-Frames gesendet, 0 CANCEL", "d-txt-s")
        + tx(320, 142, f"{gw_tx_after} von {cet_tx} CET-Sendungen nach bereits gehoerter Abstrahlung ({de(Q5['tx_after_overheard_pct'], 1)} %)", "d-txt-s")
        + tx(320, 166, "Die anderen vier OE3-Knoten hoeren diese Sendungen nicht", "d-txt-s")
    )
    add(explain(e5, cap("e5", "Server-Frames laufen immer im Fall A und werden deshalb nie abgebrochen"),
                height=185))

    # ================= Nachbarn =================
    add('<h2 id="nachbarn">Nachbarn und Hardware-Inventar</h2>')
    add("<p><b>Neun direkte Nachbarn; nur OE3HES-1 (20 m entfernt) kommt deutlich über dem Rauschteppich "
        "an (-98 dBm, SNR +5 dB).</b> Alle anderen liegen bei -112 bis -113 dBm; ihr SNR "
        "(von -4 dB bei OE3GJC-12 bis -19 dB) sagt mehr. "
        "RSSI und SNR gelten nur für das letzte Funkstück (Median, Anzahl der Empfänge in Klammern).</p>")
    dn = [x["call"] for x in h["neighbours"]["top"]]
    add(table(["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz", "RSSI / SNR (n)"],
              [inv_row(c, True) for c in dn], num_cols=(5,),
              caption="Direkte Nachbarn des Knotens (letztes Pfad-Element)"))
    others = ["OE3XIR-12", "OE3XOC-12", "OE3XWJ-12", "OE3HUS-02", "OE3REI-14", "OE1NHU-22",
              "OE3RDB-1", "OE3XSI-13"]
    add("<details><summary>Weitere genannte Stationen (8) und OE3XHL-12 selbst</summary>"
        + table(["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz"],
                [inv_row("OE3XHL-12")] + [inv_row(c) for c in others], num_cols=(5,))
        + "<p class=\"small\">OE3XIR-12 hat keine Stammdaten in mcmap; Hardware und Firmware stammen "
          "aus der Auswertung der Gruppe (alle Knoten RAK4631, 4.40a). Der Zeitserver OE1XAR-33 "
          "ist kein Knoten.</p></details>")
    add("<p>Netzsicht: <a href=\"uebersicht.html\">Übersicht der Gruppe</a>.</p>")

    # ================= Grenzen =================
    add('<h2 id="grenzen">Was nicht geprüft ist / Grenzen</h2>')
    add("<p><b>Nicht aus dem Log erkennbar</b></p><ul>"
        "<li>Ob die vier anderen OE3-Knoten den Overhear-Cancel ausgeschaltet haben; dort erklärt "
        "Fall A in 100 % der Entscheidungen das Fehlen schon.</li>"
        "<li>Ob ein abgebrochenes Relay einen Hörer verfehlt hat: dessen Empfänger loggen nicht.</li>"
        "<li>Der genaue Firmware-Build; der Slot-Pool ist aus Markern abgeleitet.</li>"
        "<li>Frequenz und Sendeleistung (kein Boot-Block im Log).</li>"
        "<li>Ob der Überlebens-Effekt (Relay-HEY scheiden per Cancel aus) das Wartezeit-Gefälle eigener gegen "
        "fremde HEY an diesem Knoten ganz erklärt.</li>"
        "<li>Ob OE3GJC-12 einen Build ohne Zeit-Tor oder eine zu langsame Uhr hat.</li></ul>")
    add("<p><b>Zählgrenzen</b></p><ul>"
        f"<li>Das Log beginnt 06:44:04 (die anderen Knoten ab etwa 06:30) und enthält 2 durch "
        f"Konsolenausgabe verschränkte CANCEL-Zeilen und 2 verschränkte RING_WRITE-Zeilen; die "
        f"Mechanismus-Auswertung (Q6) zählt {cancel_matched} saubere CANCEL, die Schicksals-Rechnung (Q2) und "
        f"die Rohzählung alle {cancel_lines}.</li>"
        "<li>Funkabdeckung wurde nur mit den fünf mitloggenden Knoten geprüft.</li>"
        "<li>RSSI-Werte schwacher Links sind durch den Rauschteppich begrenzt (Median -113 dBm); "
        "der SNR ist aussagekräftiger.</li></ul>")

    # ================= Fragen =================
    add('<h2 id="fragen">Offene Fragen an den Betreiber</h2><ol>'
        "<li>Läuft der Overhear-Cancel (<code>--nbrrelay on</code>) nur auf OE3XHL-12? Er ist es, "
        "der abbricht; auf den anderen vier Knoten lässt sich das aus dem Log nicht klären "
        "(Vermutung: Konfiguration unterscheidet sich).</li>"
        f"<li>Ist das {PING} von OE3XWJ-12 (eine Nachricht je Minute an Gruppe 2025) dauerhaft "
        "gewollt? Es verursacht fast den gesamten ACK-Verkehr. Und soll der Auto-Ping auf RAK4631 wie auf "
        "ESP32 nach einer Höchstzahl enden (Unterschied ESP32 / RAK4631, Entscheidung offen)?</li>"
        "<li>Können OE3HES-1 (am selben Mast), OE3HUS-01 und OE3RDB-2 auf eine Firmware mit CAD "
        "aktualisiert werden?</li>"
        "<li>Kann der Betreiber von OE3GJC-12 prüfen, warum das Gateway Zeitsignale mit "
        f"{de(ages_g[0])} bis {de(ages_g[-1])} s Alter abstrahlt (Build vor Commit 05a18b36 oder eine um Minuten "
        "nachgehende Uhr; beides Vermutung)?</li>"
        f"<li>Entwickler: Eigene HEY warten im Median {de(tw['o/@/p5']['wait_med_s'], 1)} s, "
        f"Relay-HEY nur {de(tw['r/@/p5']['wait_med_s'], 1)} s. Die Ursache steht in "
        '<a href="verify/rca_own_beacons.md">verify/rca_own_beacons.md</a>; soll der Altersboden auf '
        "Priorität 4 und 5 für Nicht-Relay-Einträge ausgedehnt werden?</li></ol>")

    # ================= Dev =================
    add('<details class="dev" id="dev"><summary>Methodik, Marker, Code-Stellen und Skripte '
        "(technisch)</summary>")
    add("<h3>Quellen und Abgleich</h3><ul>"
        "<li><code>health/results.json</code> (Basislinie), <code>q2</code> (Slot-Pool), "
        "<code>q34</code> (TTL, CSMA), <code>q5</code> (Zeitsignal), <code>q6</code> "
        "(Manipulationen), <code>master/master.tsv</code> und <code>distances.tsv</code>, "
        "<code>extra/oe3xhl_measure.json</code> und <code>extra/oe3xia_analysis.json</code>, "
        "<code>CORE-FINDINGS.md</code> (v2) sowie die Prüftabellen <code>verify/*.md</code>.</li>"
        f"<li>CANCEL: {cancel_lines} Zeilen im Rohlog (<code>extra/oe3xhl_measure.json</code>, "
        f"{xh['cancel_distinct_ids']} verschiedene IDs), davon {cancel_matched} "
        "sauber geparst (q6, Übersicht) und 2 in <code>[MC-TEST] RX_FRAME</code>-Zeilen "
        f"eingebettet; q2 zählt {cancel_lines} und ordnet alle zu. Anteil {cancel_share} = "
        f"{cancel_lines} / {rly_q_tx} <code>[LOG] RLY … q=tx</code>-Zeilen (Prüfung verify/q6.md Zeile 6).</li>"
        "<li>RING_WRITE: 4.987 Treffer im Rohlog, 4.985 gezählt (2 verschränkt). RING_TX_READ "
        "4.372 zugeordnet gegen 4.375 TX; 3 Lesevorgänge ohne Eintrag davor (Logbeginn).</li>"
        "<li>Capture-Zeitraum: 06:44:04 bis 07:46:02 (Q-Dateien nennen für die Gruppe ungefähr "
        "06:30 bis 07:50).</li>"
        "<li>Aus Findings-Dateien übernommene Literale (nicht in results.json): 537 s gespart und "
        "20 von 20 OE3XIA-Abbrüche (q6/findings.md M7), 7 nie gehörte Echos (M16), 31 % Fall-Wechsel, "
        "zwei fehlende UDP-Telegramme mit Zeiten und 7 NTP-Timeouts (q5/findings.md), 20,2 % ACK-Sendezeit, "
        "689 Pings an „255.“, 2,9 ACK-IDs je Ping (data/extra/oe3xwj_ping.json), Wartezeit-Vergleich IA/IR/OC "
        "(verify/q2.md Zeile 6), 19 und 35 Empfänge je Minute und 1,9 bis 2,8 Kopf-Abflüsse je Minute "
        "(verify/rca_own_beacons.md).</li>"
        "<li>Korrigierte Zählung (CORE v2): RX_ERROR-Anteil und Lückenlängen enthalten die ACK-Frames "
        "(<code>ONRXDONE_TIME</code>). <code>health/results.json</code> trägt noch die ältere Zählung "
        "ohne ACKs; dieser Bericht nimmt <code>extra/oe3xhl_measure.json</code> (stimmt mit "
        "<code>extra/oe3xia_analysis.json</code> überein).</li>"
        "<li>OE3GJC-12: Stammdaten aus <code>master/master.tsv</code> (Höhe und Koordinaten wie dort), "
        "Distanz aus <code>master/distances.tsv</code>.</li>"
        f"<li>Alter der {CET}-Kopien: <code>extra/oe3xhl_measure.json</code> (Loggerzeit minus 3.600 s minus "
        f"Inhaltszeit), {xh['cet_rf_copies_parsed']} Kopien, {len(stale)} über 150 s.</li></ul>")
    add("<h3>Marker</h3><ul>"
        "<li><code>[NBR]|CANCEL|&lt;Minuten seit Start&gt;|&lt;msg_id&gt;|&lt;H/P/T&gt;|&lt;Relayer&gt;|"
        "&lt;Masken&gt;</code>: Relay abgebrochen, weil der genannte Nachbar sendete "
        "(Emit-Stelle <code>src/lora_functions.cpp:1473</code>).</li>"
        "<li><code>[NBR]|NEED|…</code> mit Fall A, B oder U; <code>[NBR]|REFUSE|…</code>: Abbruch "
        "verweigert, weil alone nicht leer (<code>:1380</code>).</li>"
        "<li><code>RING_WRITE</code>, <code>RING_TX_READ</code>, <code>RING_SUPERSEDE</code>, "
        "<code>RING_DROP_STALE slot= prio= age_s= msg_id= reason=ttl</code>; <code>CSMA_AGED</code>.</li>"
        "<li><code>[CET];tx</code>, <code>[CET];gw_rx</code>, <code>[CET];rx</code>, "
        "<code>[LOG] GWI</code>.</li></ul>")
    add("<h3>Code-Stellen (Stand 651e0920, gelesen)</h3><ul>"
        "<li>Fall-B-Sperre: <code>src/configuration_global.h:583-585</code> (20 s einmalig, 60 s "
        "Deckel), Logik <code>src/txring_functions.cpp:421-440</code>.</li>"
        "<li>TTL: <code>src/txring_ttl.h:46-64</code>, Sweep "
        "<code>src/txring_functions.cpp:567-645</code>.</li>"
        "<li>CSMA-Altersboden: <code>src/lora_functions.cpp:4030-4045</code>, "
        "<code>src/csma_timing.h:71-110</code>.</li>"
        "<li>ACK-Relay: <code>src/lora_functions.cpp:657-661</code>; Cancel-Scan "
        "<code>:1358-1361</code>.</li>"
        "<li>Server-NEED: <code>src/gw_udp_ring.h:49-70</code>, "
        "<code>src/nbr_matrix.h:799-809</code>.</li>"
        "<li>Auto-Ping: <code>src/nrf52/nrf52_main.cpp:2717-2737</code>, "
        "<code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code>, "
        "<code>src/command_functions.cpp:3886</code>.</li>"
        "<li>Haltezeit eigener Baken gegen Relay Fall A: <code>src/lora_functions.cpp:2746-2747</code>, "
        "<code>:3998-4006</code>, <code>src/txring_functions.cpp:385-405</code>, "
        "<code>src/csma_timing.h:99-111</code>.</li>"
        "<li>Zeit-Tor: <code>src/cet_age.h:129</code> (nicht vertrauenswürdige Uhr sperrt).</li></ul>")
    add("<h3>Skripte</h3><ul>"
        "<li>Dieser Generator: <code>generators/oe3xhl.py --data … --kit … --out …</code>.</li>"
        "<li>Messhelfer: <code>generators/lib/oe3xhl_measure.py --norm &lt;norm-Ordner&gt; --out "
        "data/extra/oe3xhl_measure.json</code> (einmal je Aufnahme; liest die normalisierten Logs).</li>"
        "<li>Auswertungsskripte der Q-Berichte liegen in deren <code>scripts/</code>-Ordnern.</li></ul>")
    add("</details>")

    add('<div class="foot">Quellen: oe3xhl.log (31.10. bis 01.11.2026), Auswertungen Gesundheit, Q2, '
        "Q3/Q4, Q5, Q6, mcmap-Stammdaten. Auswertung der Fork-Firmware DK5EN, nicht die offizielle "
        'Firmware. <a href="uebersicht.html">Übersicht der Gruppe</a></div>')

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page("MeshCom Log-Auswertung OE3XHL-12", "\n".join(body)), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
