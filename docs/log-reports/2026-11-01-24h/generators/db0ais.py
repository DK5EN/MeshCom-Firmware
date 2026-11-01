#!/usr/bin/env python3
"""Per-node report page: DB0AIS-12 (24-h log analysis 2026-11-01), reworked after verification.

Usage:
    python3 -I db0ais.py --data <run>/data --kit <repo>/tools/logreport --out <run>/db0ais.html

--data  directory holding health/ q2/ q5/ q6/ master/ extra/ and CORE-FINDINGS.md
--kit   directory holding reportkit.py
--out   HTML file to write

Reads only --data and --kit. Raw-log numbers come from extra/db0ais_measure.json, written once by
lib/db0ais_measure.py (--norm <normalised logs>). Stdlib only.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any

CET = "{CET}"  # time-signal marker as it appears in the mesh, kept out of f-string braces


def n(x: float | int, dec: int = 0) -> str:
    """German number format: thousands dot, decimal comma."""
    s = f"{x:,.{dec}f}"
    return s.replace(",", "X").replace(".", ",").replace("X", ".")


def pc(a: float, b: float, dec: int = 1) -> str:
    """Percent a/b in German format with unit."""
    return n(100.0 * a / b, dec) + " %" if b else "0 %"


def de_dt(s: str) -> str:
    """'2026-10-30 17:01:11' -> '30.10.2026 17:01:11'."""
    d, t = s.split(" ")
    y, m, dd = d.split("-")
    return f"{dd}.{m}.{y} {t}"


def hm(sec: float) -> str:
    """Seconds -> 'H h MM min' (German)."""
    m = int(round(sec / 60.0))
    return f"{m // 60} h {m % 60:02d} min"


def load(data: Path, rel: str) -> Any:
    return json.loads((data / rel).read_text(encoding="utf-8"))


def load_master(data: Path) -> dict[str, dict[str, str]]:
    out: dict[str, dict[str, str]] = {}
    with (data / "master" / "master.tsv").open(encoding="utf-8", newline="") as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            out[row["call"]] = row
    return out


# ---------------------------------------------------------------- SVG helpers
# Colours only through kit classes (d-*), text >= 12 px, umlauts as ae/oe/ue in SVG text.


def s_box(x: int, y: int, w: int, h: int, cls: str = "", extra: str = "") -> str:
    return f'<rect class="d-box {cls} {extra}" x="{x}" y="{y}" width="{w}" height="{h}" rx="8"/>'


def s_txt(x: int, y: int, text: str, small: bool = False, anchor: str = "middle") -> str:
    c = "d-txt-s" if small else "d-txt"
    return f'<text class="{c}" x="{x}" y="{y}" text-anchor="{anchor}">{text}</text>'


def s_line(x1: int, y1: int, x2: int, y2: int, cls: str = "") -> str:
    return f'<line class="d-line {cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>'


def s_head(x: int, y: int, direction: str = "r", cls: str = "d-acc-f") -> str:
    """Small triangle arrow head with its tip at (x, y)."""
    if direction == "r":
        pts = f"{x},{y} {x - 9},{y - 5} {x - 9},{y + 5}"
    elif direction == "l":
        pts = f"{x},{y} {x + 9},{y - 5} {x + 9},{y + 5}"
    else:
        pts = f"{x},{y} {x - 5},{y - 9} {x + 5},{y - 9}"
    return f'<polygon class="{cls}" points="{pts}"/>'


def s_node(cx: int, cy: int, r: int, cls: str = "", extra: str = "") -> str:
    return f'<circle class="d-node {cls} {extra}" cx="{cx}" cy="{cy}" r="{r}"/>'


# ---------------------------------------------------------------- main build


def build(data: Path, kit: Path, out: Path) -> None:
    sys.path.insert(0, str(kit))
    from reportkit import bars, chip, explain, hist, page, table  # noqa: PLC0415

    allres = load(data, "health/results.json")["nodes"]
    res = allres["db0ais"]
    eth = load(data, "health/db0ais_eth.json")
    q2 = load(data, "q2/results.json")["nodes"]["db0ais"]
    q5 = load(data, "q5/results.json")["nodes"]["db0ais"]
    q6 = load(data, "q6/results.json")["nodes"]["db0ais"]
    ex = load(data, "extra/db0ais_measure.json")
    master = load_master(data)

    span = res["span"]
    tr = res["traffic"]
    own_tx = res["own_tx"]
    ch = res["channel"]
    chu = ch["CHANNEL_UTIL"]
    cad = res["cad"]
    rf = res["rf"]
    fc = res["firmware_census"]
    ring = res["ring"]
    gw = res["gateway"]
    ethr = res["eth"]
    fate = q2["fate"]
    lat = q2["latency"]["by_prio_s"]
    cet_tx = q5["tx_total"]
    hours = span["hours"]

    # ---- receive attempts, corrected denominator (errors + OnRxDone + ACK receptions)
    err_n = rf["rx_error_n"]
    done_n = res["markers_top"]["OnRxDone"]
    ack_n = tr["ack_events"]
    att_n = err_n + done_n + ack_n
    err_share = 100.0 * err_n / att_n
    ok_share = 100.0 - err_share

    def err_share_of(node: str) -> float:
        r = allres[node]
        e = r["rf"]["rx_error_n"]
        return 100.0 * e / (e + r["markers_top"]["OnRxDone"] + r["traffic"]["ack_events"])

    oe3_shares = [err_share_of(k) for k in allres if k != "db0ais"]

    cs = ex["clock_step"]
    elapsed_s = (cs["t_ms_after"] - cs["t_ms_before"]) / 1000.0
    nc = ex["need_cet"]
    reuse = ex["id_reuse"]
    ovl = ex["id_overlap"]
    ddw = ex["dedup_window"]["turnover_min"]
    gaps = [p["payload_gap_s"] for p in reuse["pairs"]]
    gaps_main = [g for g in gaps if g > 10000]
    ring_slots = ring["dedup"]["ring_size"]

    fig_no = [0]

    def cap(text: str) -> str:
        fig_no[0] += 1
        return f"Abb. {fig_no[0]}: {text}"

    own = master["DB0AIS-12"]
    util_h = ch["util_by_hour"]
    hourly = eth["hourly"]
    miss_total = eth["keepalives_without_beat"]
    night = sum(v["no_beat"] for k, v in hourly.items() if k.split(" ")[1] in ("22", "23", "00", "01", "02"))
    night_by_h = {k.split(" ")[1]: v["no_beat"] for k, v in hourly.items()}
    runs = eth["miss_runs_all_n_by_len"]
    lw = eth["link_windows"]
    longest = eth["miss_runs_longest"][0]
    tx_air = own_tx["airtime_s"]
    cet_air_s = q5["rawpass"]["tx_airtime_s_cet"]
    cet_air_all = q5["rawpass"]["tx_airtime_s_all"]
    alone = q6["alone_hist"]
    alone_hist = {int(k): v for k, v in alone["alone_size_hist"].items()}
    need_n = alone["need_lines"]
    acc = 0
    alone_median = 0
    for k in sorted(alone_hist):
        acc += alone_hist[k]
        if acc * 2 >= need_n:
            alone_median = k
            break
    top_alone = alone["top_alone_calls"]
    wait = q6["tx_wait"]["wait"]
    q6t = q6["totals"]
    xhl_src = q5["by_source"]
    unk_via = xhl_src["OE1XAR-63"]["first_via_top"]

    b: list[str] = []
    add = b.append

    # ------------------------------------------------------------ header
    add('<div class="eyebrow">MeshCom Log-Auswertung · DB0AIS-12</div>')
    add("<h1>DB0AIS-12: ruhiger Kanal, Sendepuffer ohne Verlust, Ethernet überlebt zwei "
        "Linkabrisse. Offen bleibt die DHCP-Erneuerung; zwei von drei Zeitserver-Kennungen "
        "teilen sich Nachrichten-IDs.</h1>")
    add("<ul class=\"meta\">"
        f"<li><b>Log-Datei:</b> db0ais.log (PuTTY-Mitschnitt, {n(span['lines'])} Zeilen, ohne "
        "Zeitstempel je Zeile)</li>"
        f"<li><b>Zeitraum:</b> {de_dt(span['first'])} bis {de_dt(span['last'])} "
        f"({n(hours, 2)} h), Uhr des Loggers (PC, MEZ). Die Zeiten sind aus den Millisekunden "
        "des Knotens plus PuTTY-Startzeit abgeleitet, Genauigkeit ±5 s. Die Knotenuhr ging bis "
        f"{cs['time_before'][:5]} Knotenzeit (Mitschnittzeile {n(cs['line_before'])}, "
        f"{int(cs['time_before'][:2]) - 1:02d}:{cs['time_before'][3:5]} Logger-Zeit) eine Stunde "
        "vor (UTC+2), danach UTC+1.</li>"
        "<li><b>Hardware:</b> RAK4631 (nRF52840) mit Ethernet; der Chip W5100S ist nur "
        "abgeleitet, er steht nicht im Log.</li>"
        f"<li><b>Firmware:</b> {res['identity']['version_keep']}. Der „Build-Stempel“ "
        f"<code>{list(res['identity']['stat_fw'])[0].split('/')[1]}</code> ist der Release-Stempel FLASH_VERSION, kein Build-Datum. Der "
        "genaue Build ist unbekannt (kein Boot-Block im Log). Der Marker <code>rng_seed</code> "
        "beim Start belegt die ETH-04-Korrektur F3; F1 und F2 sind daraus abgeleitet, weil sie "
        "im selben Commit liegen (Vermutung).</li>"
        f"<li><b>Standort/Höhe:</b> {own['description']}, {own['lat']} N {own['lon']} E, "
        f"{own['altitudeM']} m (Stammdaten)</li>"
        "<li><b>Rolle:</b> Internet-Gateway und Relay im hessischen Netz (Server-Kennung DL, "
        "Weg über Hamnet)</li>"
        "<li><b>LoRa-Parameter:</b> 433,175 MHz, 250 kHz Bandbreite, SF11, CR 4/6, Präambel 8, "
        "22 dBm</li>"
        "<li><b>Stand:</b> 01.11.2026</li></ul>")

    add('<div class="bluf"><p class="lead">DB0AIS-12 läuft unauffällig: Der Kanal ist ruhig '
        f"(Auslastung im Mittel {n(chu['mean'], 1)} %), der Sendepuffer verlor keinen Eintrag, und das "
        "Ethernet überstand Boot-DHCP und zwei Linkabrisse ohne neue DHCP-Runde.</p>"
        '<p class="lead">Offen bleiben drei Punkte (Beobachten): die DHCP-Erneuerung kam in '
        f"{n(hours, 0)} h nie vor, {n(err_share, 1)} % der Empfangsversuche enden ohne gültigen Rahmen, "
        f"und die Zeitserver-Kennungen -33 und -63 benutzen dieselben Nachrichten-IDs "
        f"({n(ovl['shared_33_63'])} gemeinsame IDs, {n(reuse['n'])} davon zweimal von uns weitergegeben).</p></div>")
    add('<div class="callout watch"><b>Nächster Schritt für den Betreiber:</b> ein Mitschnitt, '
        "der eine Lease-Erneuerung enthält (Lease 1 Woche, also länger als 3,5 Tage "
        "mitschneiden, oder die Lease am Router kurz setzen). Ohne das bleibt ETH-04 auf "
        "Beobachten. Details im Abschnitt „ETH-04“.</div>")

    # ------------------------------------------------------------ Antwort-Index
    add("<h2>Antwort-Index</h2>")
    own_beacons = own_tx["src_type"]["o:@"] + own_tx["src_type"]["o:!"]
    idx = [
        ("Q0 Gesundheit",
         f"Keine Neustarts, kein Heap-Verlust, Kanal ruhig. Auffällig: Rauschen ({n(err_share, 1)} % der "
         "Empfangsversuche ohne gültigen Rahmen), Server-Antworten nachts verloren, Uhr "
         "sprang um 1 h.", chip("watch")),
        ("ETH-04 Ethernet/DHCP",
         "Boot-DHCP und 2 Linkabrisse sauber, Lease-Erneuerung nie ausgelöst.",
         chip("watch")),
        ("Q2 TX-Puffer mit Slots",
         f"{n(fate['total_writes'])} Schreibvorgänge, {n(fate['counts']['tx_air'])} gesendet, "
         f"{n(fate['counts']['superseded'])} ersetzt, {n(fate['unexplained_n'])} ungeklärt, höchster Füllstand "
         f"{n(res['ring']['max']['queued'])} von {n(ring['MAX_RING'])}.", chip("ok")),
        ("Q3 Verlust durch Zeitverfall",
         f"Kein Eintrag lief ab ({n(len(ring['drop_stale_by_prio_reason']))} Verwürfe wegen Alter). "
         f"{n(own_tx['relay_decisions_RLY']['cet'])} veraltete fremde Zeitsignale vom Gate gestoppt.", chip("ok")),
        ("Q4 Gewinn durch Zeitverfall",
         "Bei dieser Last nicht sichtbar: Puffer nie voll, Alterungsregeln griffen nie.",
         chip("ok")),
        ("Q5 Zeitsignal",
         f"Drei Kennungen (-33/-62/-63): {n(cet_tx)} Aussendungen {CET} in {n(hours, 0)} h = "
         f"{n(q5['cet_tx_per_h'], 1)} je Stunde, {n(q5['rawpass']['tx_cet_share_airtime_pct'], 1)} % der Sendezeit. "
         f"-33 und -63 teilen {n(ovl['shared_33_63'])} IDs, die Knotenuhr sprang um 1 h zurück.",
         chip("watch")),
        ("Q6 Manipulationen",
         f"Eigene Baken {n(own_beacons)} von {n(own_beacons)} gesendet. Alle {n(q6t['need_A'])} "
         "NEED-Entscheidungen Fall A, nie Abbruch (Beobachten, Design).", chip("watch")),
    ]
    add(table(["Frage", "Kurzantwort", "Urteil"], [[a, bb, c] for a, bb, c in idx], raw=True))

    # glossary (once)
    add('<details><summary>Begriffe in einem Halbsatz</summary><ul>'
        "<li><b>CAD:</b> Kanalabfrage, bevor gesendet wird (Lauschen auf Preamble).</li>"
        f"<li><b>Slot:</b> ein Platz im Sendepuffer; der Puffer hat {n(ring['MAX_RING'])} Plätze.</li>"
        "<li><b>TTL:</b> Höchstalter eines Eintrags im Sendepuffer; danach wird er verworfen.</li>"
        f"<li><b>{CET}:</b> Zeitsignal-Telegramm des Servers (Inhalt ist UTC, das Etikett "
        "„CET“ ist irreführend).</li>"
        "<li><b>Supersede:</b> ein neuerer Eintrag desselben Absenders ersetzt einen noch nicht "
        "gesendeten alten.</li>"
        "<li><b>Overhear-Cancel:</b> ein Relay-Eintrag wird gestrichen, weil ein Nachbar das "
        "Paket schon gesendet hat.</li>"
        f"<li><b>Dedup-Ring:</b> Merkliste der zuletzt gesehenen Nachrichten-IDs ({n(ring_slots)} Plätze), "
        "damit nichts doppelt weitergegeben wird.</li>"
        "<li><b>Keepalive/BEAT:</b> Das Gateway meldet sich alle 30 s beim Server (Keepalive), "
        "der Server antwortet mit BEAT.</li>"
        "<li><b>RX_ERROR:</b> Funkchip erkannte etwas (Preamble/Header), aber kein gültiger "
        "Rahmen kam heraus.</li></ul></details>")
    add('<p class="small">Netzwerkweite Sicht: <a href="uebersicht.html">Übersicht</a>. '
        "Dieser Knoten liegt in einem anderen Netz (Hessen) und an einem anderen Tag als die "
        "österreichischen Knoten; es gibt keine Querauswertung.</p>")

    # ------------------------------------------------------------ Q0 Gesundheit
    add("<h2>Gesundheit und Funkumfeld</h2>")
    dup = tr["dup_flag"]
    ty = tr["types"]
    sh = tr["airtime_share_by_type_pct"]
    sp = own_tx["src_pct"]
    sil = res["rx_silence"]
    add("<ul>"
        f"<li><b>Verkehr:</b> {n(tr['heard_events'])} Empfangsereignisse (keine "
        f"angenommenen Pakete), dazu {n(tr['ack_events'])} Quittungsrahmen; {n(tr['fcs0000_excluded'])} Rahmen mit "
        f"Prüfsumme 0000 (Decoder-Abbruch) sind ausgeschlossen. Davon neu {n(dup['n'])}, Duplikate "
        f"{n(dup['d'])}. {n(tr['unique_originators_excl_time_server'])} verschiedene Absender, "
        f"{n(tr['unique_msgids'])} Nachrichten-IDs.</li>"
        f"<li><b>Typen:</b> Heartbeat/HEY {n(ty['@']['n'])} ({n(ty['@']['pct'], 1)} %), Text {n(ty[':']['n'])} "
        f"({n(ty[':']['pct'], 1)} %), Position {n(ty['!']['n'])} ({n(ty['!']['pct'], 1)} %).</li>"
        f"<li><b>Sendezeit der empfangenen Rahmen:</b> HEY {n(sh['@'], 1)} %, Text {n(sh[':'], 1)} %, Position "
        f"{n(sh['!'], 1)} %, Quittungen {n(sh['ACK'], 1)} %. Quittungen sind hier also kaum eine Last "
        f"(Summe {n(sum(sh.values()), 1)} %).</li>"
        f"<li><b>Eigener Sendeanteil:</b> {n(own_tx['tx_lines'])} Aussendungen ({n(own_tx['tx_per_hour'], 1)} je Stunde, "
        f"{n(tx_air)} s Sendezeit): {n(sp['r'], 1)} % Relay, {n(sp['g'], 1)} % Server an Mesh, {n(sp['o'], 1)} % "
        f"eigene Rahmen (Summe {n(sum(sp.values()), 1)} %).</li>"
        f"<li><b>Kanalauslastung:</b> Mittel {n(chu['mean'], 1)} %, Median {n(chu['median'], 1)} %, 95. Perzentil "
        f"{n(chu['p95'], 1)} %, Spitze {n(chu['max'], 1)} % ({n(chu['ge70'])} Messwert über 70 %). Kaum "
        f"Tag/Nacht-Unterschied, Stundenmittel {n(min(util_h.values()), 1)} bis {n(max(util_h.values()), 1)} %. "
        "Der ESP32-Aufblähungsfehler gilt für den nRF52 nicht; Gegenprobe negativ.</li>"
        f"<li><b>Neustarts:</b> {n(len(res['stat']['uptime_resets']))}. Betriebszeit steigt gleichmäßig von "
        f"{n(res['stat']['up_first'])} s bis {n(res['stat']['up_last'])} s; der einzige Start (Soft-Reset) liegt am "
        f"Mitschnittbeginn. Heap {n(res['stat']['heap']['first'])} auf {n(res['stat']['heap']['last'])} Byte, "
        f"Steigung {n(res['stat']['heap']['slope_per_hour'], 1)} Byte/h: kein Leck-Signal.</li>"
        f"<li><b>Empfangslücken:</b> Median {n(sil['median_gap_s'], 2)} s zwischen Empfängen (Quittungen "
        f"mitgezählt), längste Lücke {n(sil['longest_gaps'][0]['gap_s'])} s; {n(sil['gaps_gt_300s'])} über 300 s. "
        "Das Muster des taub gewordenen Empfängers von OE3XIA (lange Stillen ohne jeden Empfang, "
        "während Kanalabfrage und Senden weiterlaufen) fehlt hier.</li>"
        f"<li><b>Zustandsmaschine:</b> {n(res['state_machine']['csma_backoff_rc_minus1'])} mal „Kanal belegt, "
        "nicht gesendet“ (normal); keine Fehlerzustände.</li>"
        f"<li><b>Pings:</b> {n(res['health']['ping_send_lines'])} gesendete Pings in {n(hours, 0)} h. Der "
        "Unterschied ESP32/RAK4631 beim Auto-Ping (RAK ohne Zähler, Entscheidung offen) berührt "
        "diesen Knoten daher nicht.</li></ul>")
    add(hist([(k.split(" ")[1], v) for k, v in util_h.items()],
             cap(f"Kanalauslastung je Stunde (Mittel, Prozent): flach zwischen {n(min(util_h.values()), 1)} "
                 f"und {n(max(util_h.values()), 1)} %"),
             unit=" %", ylabel="Stunde des Tages, Logger-Uhr 30.10. 17 Uhr bis 31.10. 18 Uhr"))

    add("<h3>Rauschen und Empfangsfehler (Beobachten)</h3>")
    add("<ul>"
        f"<li><b>RX_ERROR:</b> {n(err_n)} Fehlversuche stehen {n(done_n)} empfangenen Rahmen (OnRxDone) und "
        f"{n(ack_n)} Quittungen gegenüber (Quittungen erscheinen nicht als OnRxDone-Zeile und gehören in "
        f"den Nenner); das sind {n(err_share, 1)} % aller {n(att_n)} Empfangsversuche "
        f"({n(ok_share, 1)} % Rahmen, Summe 100,0 %), {n(rf['rx_error_per_hour_mean'])} Fehler je Stunde. Der "
        f"Wert ist der höchste der sechs ausgewerteten Knoten (die fünf österreichischen: "
        f"{n(min(oe3_shares), 1)} bis {n(max(oe3_shares), 1)} %); Schwelle für Beobachten 40 %. "
        "Er bleibt auf Beobachten.</li>"
        f"<li><b>Rauschteppich:</b> RSSI der Fehlversuche 5. Perzentil {n(rf['rx_error_rssi']['p5'])} dBm, "
        f"Median {n(rf['rx_error_rssi']['median'])} dBm, SNR-Median {n(rf['rx_error_snr']['median'])} dB. Die "
        f"theoretische Schwelle bei 250 kHz liegt bei etwa {n(rf['theory_floor_bw250_dbm'])} dBm, der Teppich also "
        f"rund {n(abs(rf['theory_floor_bw250_dbm'] - rf['rx_error_rssi']['p5']))} dB darüber. "
        f"Die Schätzung über RSSI minus SNR ({n(rf['noise_rssi_minus_snr_snr_ge0']['median'])} dBm) weicht "
        "davon ab; absolute Werte sind Vermutung. Der RSSI-Wert von Fehlversuchen kann bei "
        "Header-Fehlern veraltet sein.</li>"
        f"<li><b>Starke Störer:</b> {n(rf['rx_error_gt_minus85'])} Fehlversuche über −85 dBm, "
        f"{n(rf['rx_error_gt_minus60'])} über −60 dBm; höchster gültiger Empfang {n(rf['rssi_all']['max'])} dBm, "
        "nie nahe der Übersteuerungsgrenze. Anders als bei OE3XIR gibt es keine starke Einzelquelle.</li></ul>")
    w_ok = round(620 * ok_share / 100)
    add(explain(
        s_box(10, 20, 620, 26, "d-neu")
        + f'<rect class="d-box d-ok" x="10" y="20" width="{w_ok}" height="26" rx="8"/>'
        + f'<rect class="d-box d-watch anim-pulse" x="{10 + w_ok}" y="20" width="{620 - w_ok}" height="26" rx="8"/>'
        + s_txt(10 + w_ok // 2, 38, f"Rahmen {n(ok_share, 1)} %", small=True)
        + s_txt(10 + w_ok + (620 - w_ok) // 2, 38, f"RX_ERROR {n(err_share, 1)} %", small=True)
        + s_txt(320, 76, f"Alle Empfangsversuche: {n(att_n)} = {n(done_n)} Rahmen + {n(ack_n)} Quittungen "
                         f"+ {n(err_n)} Fehler", small=True)
        + s_box(40, 100, 170, 56, "d-neu")
        + s_txt(125, 123, "Teppich p5", small=True)
        + s_txt(125, 143, f"{rf['rx_error_rssi']['p5']:.0f} dBm".replace(".", ","))
        + s_box(235, 100, 170, 56, "d-neu")
        + s_txt(320, 123, "Fehler-Median", small=True)
        + s_txt(320, 143, f"{rf['rx_error_rssi']['median']:.0f} dBm, SNR {rf['rx_error_snr']['median']:.0f} dB")
        + s_box(430, 100, 170, 56, "d-ok")
        + s_txt(515, 123, "Staerkster Empfang", small=True)
        + s_txt(515, 143, f"{rf['rssi_all']['max']:.0f} dBm")
        + s_txt(320, 190, "Rauschbegrenzt: viel Energie, wenig dekodierbare Rahmen", small=True),
        cap(f"{n(err_share, 1)} % der Empfangsversuche enden ohne gültigen Rahmen, der Teppich liegt "
            f"bei etwa {n(rf['rx_error_rssi']['p5'])} dBm"), height=210))

    add("<h3>Kanalabfrage (CAD) und ältere Firmware</h3>")
    ca = cad["cad_attempt_pct"]
    add(bars([("sofort frei (0 Versuche)", ca["0"]),
              ("1 Wiederholung", ca["1"]),
              ("2 Wiederholungen", ca["2"]),
              ("3 und mehr", cad["attempt_ge3_pct"])],
             cap("Die Hälfte der Sendungen startet ohne Wartezeit durch Kanalabfrage"),
             unit=" %"))
    sl = cad["storm_longest"][0]
    vn = fc["versions_nodes"]
    v440 = sum(v for k, v in vn.items() if k.startswith("4.40"))
    v435 = sum(v for k, v in vn.items() if k.startswith("4.35"))
    v40 = vn.get("4.0#", 0)
    vt = fc["nodes_total"]
    add("<ul>"
        f"<li><b>CAD:</b> {n(cad['cad_scans'])} Abfragen, {n(cad['initial_busy_rate_pct'], 1)} % zeigten zuerst "
        f"„belegt“; davon waren {n(cad['false_positive_pct_of_initial_busy'], 1)} % Fehlalarm "
        f"({n(cad['cad_false_positive'])}). Längste Wartefolge {n(sl['dur_s'], 1)} s "
        f"({n(sl['max_attempt'])} Versuche, {sl['start'][3:5]}.{sl['start'][0:2]}. {sl['start'][6:]}). "
        f"CAD_SAFETY_TIMEOUT {n(cad['cad_safety_timeout'])}.</li>"
        f"<li><b>Ältere Firmware ohne Kanalabfrage</b> (4.35a bis o und älter, sie sendet ohne "
        f"vorher zu lauschen): {n(fc['nodes_pre_cad'])} von {n(fc['nodes_total'])} Stationen "
        f"({n(fc['nodes_pre_cad_pct'], 1)} %), sie erzeugen {n(fc['events_pre_cad_pct'], 1)} % der "
        f"Empfangsereignisse und {n(fc['airtime_pre_cad_pct'], 1)} % der empfangenen Sendezeit. Das ist die "
        "Nachbarschaft, nicht der Zustand dieses Knotens (Beobachten, nie Handlungsbedarf für ihn). Liste im "
        "Inventar unten.</li>"
        f"<li><b>Firmware-Verteilung der gehörten Stationen:</b> 4.40x {n(v440)} ({pc(v440, vt)}), 4.35x "
        f"{n(v435)} ({pc(v435, vt)}), 4.0# {n(v40)} ({pc(v40, vt)}); Summe "
        f"{n(100 * (v440 + v435 + v40) / vt, 1)} %.</li></ul>")

    # ------------------------------------------------------------ ETH-04
    add("<h2>ETH-04: Ethernet und DHCP</h2>")
    add(f'<p><b>Urteil {chip("watch")}: Der Boot-DHCP und beide Linkabrisse liefen sauber '
        f"durch, die DHCP-Lease-Erneuerung kam in {n(hours, 0)} Stunden nicht vor. Der Test ist nicht "
        "entscheidend.</b></p>")
    add("<p>Hintergrund: ETH-04 war der Fehler, dass DB0AIS (MikroTik-Router, Lease 1 Woche, "
        "<code>always-broadcast=no</code>) seine DHCP-Lease stundenlang verlor. Jeder "
        "Wiederherstellungsweg verwarf früher die noch gültige Lease. Die Korrektur besteht aus "
        "F1 (UDP erneut öffnen und die IP behalten), F2 (Chip-Reset nach zwei DHCP-Fehlversuchen) und "
        "F3 (Zufallszahlen aus dem SoftDevice-Generator). <b>Belegt ist nur F3:</b> der Marker "
        "<code>[ETH];event;rng_seed;src;sd</code> steht beim Start im Log. F1 und F2 sind abgeleitet, "
        "weil sie im selben Commit (708b5091) wie F3 landeten (Vermutung für diesen Build).</p>")
    sts = {x[1]: x[2] for x in eth["stalls"]}
    got_ip_ms = int([x[1] for x in eth["other_eth_events"] if ";got_ip;" in x[1]][0].split(";")[-1])
    first_eth = ethr["first"]
    last_eth = ethr["last"]
    fl1, fl2 = lw[0], lw[1]
    rows = [
        ["Boot-DHCP", chip("ok"),
         f"Lease in {n(sts['dhcp_begin'] / 1000, 1)} s (Stillstand der Hauptschleife {n(sts['dhcp_begin'])} ms), "
         f"IP {first_eth['ip']}, <code>got_ip</code> bei {n(got_ip_ms / 1000, 1)} s "
         "Betriebszeit. Der Start war ein Soft-Reset. Nur ein "
         "Start; der Fehler trat früher nur gelegentlich auf."],
        ["Linkabriss 1", chip("ok"),
         f"{fl1['link_down'][3:5]}.{fl1['link_down'][0:2]}. {fl1['link_down'][6:]}, "
         f"{n(fl1['down_ms'] / 1000, 1)} s. IP behalten (<code>ip;1</code>), keine DHCP-Runde, UDP "
         "lief weiter."],
        ["Linkabriss 2", chip("ok"),
         f"{fl2['link_down'][3:5]}.{fl2['link_down'][0:2]}. {fl2['link_down'][6:]}, "
         f"{n(fl2['down_ms'] / 1000, 1)} s. IP behalten, keine DHCP-Runde."],
        ["Lease-Erneuerung", chip("watch", "nicht geprüft"),
         f"{n(last_eth['renews'])} Erneuerungen, {n(last_eth['renew_fail'])} Fehlschläge in {n(hours, 2)} h. Bei einer "
         "Woche Lease wäre die erste Erneuerung nach etwa 3,5 Tagen fällig (Annahme: halbe Lease-Zeit)."],
        ["Chip-Reset (F2)", chip("watch", "nicht ausgelöst"),
         f"{n(last_eth['resets'])} Hardware-Resets, 0 <code>reopen_udp</code>-Ereignisse: es gab keine "
         "DHCP-Fehlversuche, die F2 hätten auslösen können."],
        ["UDP-Sendefehler", chip("ok"),
         f"Zähler tx_fail bleibt {n(last_eth['tx_fail'])} in allen {n(ethr['summaries'])} Minutenzeilen; "
         f"längste Sendeblockade {n(eth['tx_max_ms_final'])} ms."],
    ]
    add(table(["Pfad", "Ergebnis", "Beleg"], rows, raw=True))
    add(explain(
        s_box(10, 12, 200, 62, "d-ok") + s_txt(110, 36, f"Boot: Lease in {n(sts['dhcp_begin'] / 1000, 1)} s")
        + s_txt(110, 58, "1 Start geprueft", small=True)
        + s_box(220, 12, 200, 62, "d-ok") + s_txt(320, 36, "Link-Flap: IP bleibt")
        + s_txt(320, 58, "2 von 2 sauber", small=True)
        + s_box(430, 12, 200, 62, "d-watch", "anim-pulse") + s_txt(530, 36, "Lease-Erneuerung")
        + s_txt(530, 58, "nie ausgeloest (T1 ca. 3,5 d)", small=True)
        + s_node(70, 150, 28, "d-ok") + s_txt(70, 155, "Knoten", small=True)
        + s_box(250, 124, 140, 52, "d-neu") + s_txt(320, 155, "Router/Switch", small=True)
        + s_box(500, 124, 130, 52, "d-neu") + s_txt(565, 155, "DHCP-Server", small=True)
        + s_line(98, 150, 250, 150, "d-ok-s anim-flow")
        + s_line(390, 150, 500, 150, "d-watch-s")
        + s_txt(440, 142, "Lease 1 Woche", small=True)
        + s_txt(320, 205, "Gruen: erlebt und bestanden. Gelb: im Log nicht vorgekommen.",
                small=True),
        cap("ETH-04: Boot und Linkabrisse bestanden, die Lease-Erneuerung wurde nie "
            "ausgelöst"), height=220))

    add("<h3>Server-Antworten gehen nachts verloren</h3>")
    gl = eth["link_windows"]
    g62x = res["time_signal"]["gw_rx_gaps"]["OE1XAR-62"]
    gaps62 = ", ".join(f"{n(x['gap_s'])} s" for x in g62x["largest_gaps"])
    add("<ul>"
        f"<li><b>Umfang:</b> {n(miss_total)} von {n(eth['keepalives'])} "
        f"Keepalives ohne BEAT-Antwort ({n(eth['pct_without_beat'], 2)} %), in {n(eth['miss_runs_n'])} "
        f"Ausfallserien ({n(runs['1'])} einzelne, {n(runs['2'])} mal zwei, je einmal drei und vier in Folge). "
        f"Die Meldung „Server not responding for 35s“ steht {n(eth['udp_not_responding_n'])} mal im Log.</li>"
        f"<li><b>Wann:</b> {n(night)} der {n(miss_total)} Ausfälle liegen zwischen 22:00 und 02:59 (Stunde 22: "
        f"{night_by_h['22']}, 23: {night_by_h['23']}, 00: {night_by_h['00']}, 01: {night_by_h['01']}, 02: "
        f"{night_by_h['02']}), {n(miss_total - night)} verstreut tagsüber.</li>"
        f"<li><b>Längste Stille:</b> {n(longest['silence_s'])} s ({longest['first_missed_keep'][3:5]}.{longest['first_missed_keep'][0:2]}. "
        f"{longest['first_missed_keep'][6:]} bis {longest['next_beat_after'][6:]}).</li>"
        f"<li><b>Linkabriss 1:</b> letzter BEAT {gl[0]['last_beat_before'][6:]}, nach dem Wiederkehren des "
        f"Links dauerte es {n(gl[0]['first_beat_after_up_s'])} s bis zum nächsten BEAT (2 Keepalives unbeantwortet).</li>"
        f"<li><b>Linkabriss 2:</b> der Server schwieg schon seit {gl[1]['last_beat_before'][6:]}, also "
        f"{n(gl[1]['age_s_at_down'], 1)} s vor dem Abriss; {n(gl[1]['first_beat_after_up_s'], 1)} s nach dem Link-Up kam der erste BEAT. "
        "Vermutung: ein Ereignis am Router oder Switch hinter dem Knoten löste beides aus, nicht der "
        "Ethernet-Stack des Knotens.</li>"
        f"<li><b>Gateway-Funktion trotz Ausfall:</b> Keepalives wurden immer gesendet (größte "
        f"Lücke {n(eth['keepalive_gap_max_s'], 1)} s), {n(eth['gwu_n'])} Uploads LoRa→Server und "
        f"{n(eth['ext_tx_n'])} EXT-UDP-Ausgänge liefen weiter. Ob der Server sie erhielt, ist vom Knoten "
        "aus nicht erkennbar (keine Quittung).</li>"
        f"<li><b>Zeitsignal-Folge:</b> Von {n(g62x['expected_in_span'])} "
        f"erwarteten {CET}-Telegrammen der Kennung -62 kamen {n(gw['cet_gw_rx'])} über das Internet an; die "
        f"größten Lücken ({gaps62}) liegen am Linkabriss 1 und im nächtlichen "
        "Ausfallfenster.</li></ul>")
    add(hist([(k.split(" ")[1], v["no_beat"]) for k, v in hourly.items()],
             cap(f"Fehlende Server-Antworten je Stunde: {n(night)} von {n(miss_total)} zwischen 22 und 03 Uhr"),
             unit=" Keepalives", ylabel="Stunde, Logger-Uhr"))
    add(explain(
        s_box(10, 70, 130, 60, "d-ok") + s_txt(75, 105, "Knoten DB0AIS")
        + s_box(250, 40, 140, 120, "d-watch", "anim-pulse")
        + s_txt(320, 90, "Weg Hamnet", small=True) + s_txt(320, 112, "Router/Switch", small=True)
        + s_box(500, 70, 130, 60, "d-neu") + s_txt(565, 105, "Server")
        + s_line(140, 88, 250, 88, "d-ok-s anim-flow") + s_head(250, 88, "r", "d-ok-f")
        + s_line(390, 88, 500, 88, "d-ok-s anim-flow") + s_head(500, 88, "r", "d-ok-f")
        + s_line(500, 118, 390, 118, "d-act-s") + s_txt(445, 138, "BEAT fehlt", small=True)
        + s_txt(320, 190, "Keepalive geht raus (tx_fail 0), BEAT kommt nachts nicht zurueck",
                small=True)
        + s_txt(320, 208, f"{miss_total} von {eth['keepalives']} verloren, {night} davon zwischen 22 und 03 Uhr",
                small=True),
        cap("Keepalive läuft hin, die Antwort bleibt nachts oft aus: Ursache hinter dem "
            "Knoten (Vermutung)"), height=220))
    add("<h3>So lässt sich die Lease-Erneuerung testen</h3>"
        "<ol>"
        "<li><b>Langer Mitschnitt:</b> mindestens 4 Tage am Stück (Lease 1 Woche, erste "
        "Erneuerung bei etwa der halben Zeit). Im Log zählt der Zähler <code>renews</code> in "
        "den Minutenzeilen <code>[ETH];link;up;…</code>, ein erfolgreicher Ablauf zeigt "
        "<code>renews</code> größer 0 und <code>renew_fail;0</code>.</li>"
        "<li><b>Lease am Router kurz setzen</b> (Vorschlag, nicht erprobt: einige Minuten). "
        "Andere Gateways im Netz mit kurzer Lease erneuern alle 5 bis 15 Minuten ohne "
        "Fehlschlag; das Netz und die Leaselänge sind dort aber andere.</li>"
        "<li><b>Mit <code>--ethdrop</code> auf einem INSTRUMENT-Build</b> den "
        "Wiederherstellungspfad auslösen (die Befehle laufen nur im Instrument-Build, "
        "docs/issue-rak-dhcp-after-reboot-db0ais.md).</li>"
        "<li><b>Serie von 5 bis 10 <code>--reboot</code>:</b> jeder Start soll mit einem "
        "DISCOVER eine Lease bekommen (Prüfplan im selben Dokument). Der Router-Stand "
        "<code>always-broadcast</code> ist aus dem Log nicht ablesbar.</li></ol>")

    # ------------------------------------------------------------ F2
    add("<h2>F2 TX-Puffer mit Slots</h2>")
    add(f'<p><b>Urteil {chip("ok")}: Der Puffer arbeitet sauber, bei dieser Last gibt es '
        "keine Warteschlange und keinen Verlust.</b></p>")
    fc_ = fate["counts"]
    fp_ = fate["pct"]
    occ = q2["occupancy"]
    ex_ = q2["extras"]
    sup = q2["supersede"]
    add(table(["Schicksal der Puffer-Einträge", "Anzahl", "Anteil"],
              [["gesendet", n(fc_["tx_air"]), n(fp_["tx_air"], 2) + " %"],
               ["durch Neueren ersetzt (Supersede)", n(fc_["superseded"]), n(fp_["superseded"], 2) + " %"],
               ["abgelaufen, abgebrochen, ungeklärt", n(fate["unexplained_n"]), "0,00 %"],
               ["Summe", n(fate["total_writes"]), n(fp_["tx_air"] + fp_["superseded"], 2) + " %"]],
              num_cols=[1, 2]))
    p1, p3, p4, p5 = lat["1"], lat["3"], lat["4"], lat["5"]
    add("<ul>"
        f"<li><b>Füllstand:</b> höchstens {n(occ['queued']['max'])} von {n(ring['MAX_RING'])} Plätzen belegt "
        f"(Statuszeilen), im Median {n(occ['queued']['p50'])}, 90. Perzentil {n(occ['queued']['p90'])}. Der "
        f"STAT-Zähler nennt {n(res['stat']['ringmax_max'])}; er zählt den Slot in Sendung mit.</li>"
        f"<li><b>Verwerfungen:</b> 0 RING_DROP_NEW/PRIO/STALE, {n(ring['ring_overflow'])} Überläufe, "
        f"{n(ring['ack_reject'])} Quittungsabweisungen; Retry-Leiter nie benutzt (retrying 0 in "
        f"{n(occ['status_samples'])} Stichproben, RETRANSMIT_GIVEUP {n(ring['retransmit_giveup'])}).</li>"
        f"<li><b>Wartezeit vor dem Senden (Median/90. Perz.):</b> Quittung {n(p1['p50'], 1)}/{n(p1['p90'], 1)} s, "
        f"Relay-Text {n(p3['p50'], 1)}/{n(p3['p90'], 1)} s, Position {n(p4['p50'], 1)}/{n(p4['p90'], 1)} s, "
        f"HEY {n(p5['p50'], 1)}/{n(p5['p90'], 1)} s; Maximum {n(p5['max'], 1)} s (eine eigene HEY).</li>"
        f"<li><b>Eigene HEY:</b> {n(ex_['own_hey_aired'])} von {n(q2['hey_chains']['own_hey']['chains'])} gesendet, "
        f"Abstand im Median {n(ex_['own_hey_gap_s']['p50'])} s, 90. Perzentil {n(ex_['own_hey_gap_s']['p90'])} s, "
        f"{n(ex_['own_hey_gaps_over_20min'])} Lücken über 20 Minuten.</li>"
        f"<li><b>Supersede:</b> {n(sup['events'])} Fälle ({n(sup['by_type']['3A'])} Texte, {n(sup['by_type']['40'])} HEY), "
        f"jeder alte Eintrag war zuvor nicht gesendet ({n(sup['old_had_reads'])} Lesevorgänge); die Ersatzfassung "
        f"ging in allen {n(sup['chain_tail']['tx_air'])} Fällen auf Sendung.</li>"
        f"<li><b>Doppelsendungen:</b> {n(q2['double_tx']['pairs'])} Paare binnen 30 Minuten.</li>"
        f"<li><b>Kontrast zu OE3XOC und OE3XIA:</b> auch hier warten eigene Baken in der Spitze länger als "
        f"weitergeleitete (eigene HEY Median/90. Perz. {n(wait['o/@/p5']['wait_med_s'], 1)}/{n(wait['o/@/p5']['wait_p90_s'], 1)} s, "
        f"weitergeleitete HEY {n(wait['r/@/p5']['wait_med_s'], 1)}/{n(wait['r/@/p5']['wait_p90_s'], 1)} s; eigene Position "
        f"{n(wait['o/!/p4']['wait_med_s'], 1)}/{n(wait['o/!/p4']['wait_p90_s'], 1)} s gegen "
        f"{n(wait['r/!/p4']['wait_med_s'], 1)}/{n(wait['r/!/p4']['wait_p90_s'], 1)} s). Die Mediane sind fast gleich, "
        "der Unterschied zeigt sich nur im Ausläufer. An den belasteten österreichischen Knoten ist er die "
        "Ursache für verlorene Baken (eigene Baken bekommen die 5,5-s-Wartezeit, Relays gleicher Klasse den "
        "kurzen Fall-A-Pfad, siehe verify/rca_own_beacons.md). Hier reicht die Last nicht, um etwas zu "
        f"verlieren: {n(own_beacons)} von {n(own_beacons)} eigenen Baken gesendet, 0 durch TTL "
        "verworfen.</li></ul>")

    # ------------------------------------------------------------ F3
    add("<h2>F3 Was verlieren wir durch den Zeitverfall</h2>")
    add(f'<p><b>Urteil {chip("ok")}: Nichts. Kein Eintrag erreichte sein Höchstalter, und die '
        f"{CET}-Sperre musste an keiner eigenen Sendung eingreifen.</b></p>")
    ttl = {"1": 90, "3": 300, "4": 120, "5": 180}

    def ttl_row(label: str, k: str) -> list[str]:
        return [label, f"{ttl[k]} s", f"{n(lat[k]['max'], 1)} s", pc(lat[k]["max"], ttl[k], 0)]

    add(table(["Klasse", "TTL", "längste Wartezeit eines gesendeten Eintrags", "Anteil der TTL"],
              [ttl_row("Quittung (Prio 1)", "1"), ttl_row("Relay-Text (Prio 3)", "3"),
               ttl_row("Position (Prio 4)", "4"), ttl_row("HEY (Prio 5)", "5")],
              num_cols=[1, 2, 3]))
    cet_drops = q5["cet_drops"]
    ages = sorted(int(x[1]) for x in cet_drops)
    ages_txt = ", ".join(str(a) for a in ages[:-1]) + " und " + str(ages[-1])
    add("<ul>"
        f"<li><b>RING_DROP_STALE:</b> {n(len(ring['drop_stale_by_prio_reason']))} (weder Grund ttl noch cet). "
        f"Alle Klassen blieben unter der Hälfte ihrer TTL, nur die HEY erreichte {pc(lat['5']['max'], 180, 0)}.</li>"
        f"<li><b>{CET}-Sperre (150 s):</b> älteste eigene Aussendung {n(q5['tx_age_s']['max'])} s, Median "
        f"{n(q5['tx_age_s']['p50'], 1)} s, 90. Perzentil {n(q5['tx_age_s']['p90'])} s. Es gab keine "
        "Verwerfung im Puffer.</li>"
        f"<li><b>Gate am Relay-Eingang:</b> {n(len(cet_drops))} fremde {CET}-Rahmen (Alter {ages_txt} s) "
        "wurden nicht weitergegeben. Jeder hätte rund 0,8 s Sendezeit gekostet, zusammen etwa "
        f"{n(len(cet_drops) * cet_air_s / cet_tx)} s (Abschätzung: mittlere Sendezeit eines Zeitsignal-Rahmens "
        f"{n(cet_air_s / cet_tx, 2)} s).</li></ul>")
    add("<h3>CSMA-Altersboden</h3>"
        "<p>Der Altersboden verkürzt die Wartezeit von Quittungen und Relay-Texten, die "
        f"länger als 20 s im Puffer liegen. <b>Bei DB0AIS-12 wurde er nie ausgelöst:</b> {n(cad['csma_aged'])} "
        f"Ereignisse <code>CSMA_AGED</code>; von {n(own_tx['prio_split']['1'] + own_tx['prio_split']['3'])} "
        "Aussendungen der Prio 1 bis 3 wartete nur "
        f"eine länger als 20 s ({n(p3['max'], 1)} s). Der Auslöser war nie erfüllt; daraus lässt sich nichts "
        "über den Build ableiten.</p>")

    # ------------------------------------------------------------ F4
    add("<h2>F4 Was gewinnen wir durch den Zeitverfall</h2>")
    add(f'<p><b>Urteil {chip("ok")}: Bei dieser Last ist kein Gewinn messbar, aber auch '
        "keiner nötig.</b></p>")
    add("<ul>"
        f"<li><b>Platzgewinn:</b> keiner, der Puffer war nie über {n(occ['queued']['max'])} von "
        f"{n(ring['MAX_RING'])} gefüllt. Da nie ein Eintrag verfiel, hätte der Ablauf ohne Zeitverfall gleich "
        "ausgesehen (Schluss aus 0 Verwürfen, nicht nachgerechnet).</li>"
        "<li><b>Gewinn an Aktualität:</b> nicht sichtbar, es wurde nichts verworfen.</li>"
        f"<li><b>Gewinn durch die {CET}-Sperre:</b> {n(len(cet_drops))} veraltete fremde Zeitsignale wurden "
        "nicht neu verbreitet (siehe F3). Das ist der einzige belegte Effekt.</li></ul>")

    # ------------------------------------------------------------ F5
    add("<h2>F5 Zeitsignal</h2>")
    add(f'<p><b>Urteil {chip("watch")}: Das Signal ist regelmäßig und korrekt, aber drei '
        "Kennungen erzeugen das Dreifache der Last wie im österreichischen Netz (dort eine Kennung). "
        "Zwei der drei Kennungen teilen sich Nachrichten-IDs. Die Knotenuhr sprang zusätzlich "
        "ohne erkennbare Ursache.</b></p>")
    add(f"<p>{CET} ist ein Zeitsignal-Telegramm, das der Server alle zehn Minuten schickt. "
        "OE1XAR-33, OE1XAR-62 und OE1XAR-63 sind Zeitserver-Kennungen (keine Stationen, "
        "keine Stammdaten). <b>OE1XAR-63 kommt nur in diesem Netz vor, seine Herkunft ist "
        "unbekannt:</b> er wird nie über das Internet eingespeist und erreicht den Knoten nur über "
        f"Funk; als erster Hop sehen wir meist {unk_via[0][0]} ({unk_via[0][1]} Telegramme), {unk_via[1][0]} "
        f"({unk_via[1][1]}) und {unk_via[2][0]} ({unk_via[2][1]}). Im österreichischen Netz kommt er nicht "
        "vor (dort nur -33).</p>")
    srcs = q5["by_source"]
    cad_ = q5["cadence"]
    t62 = srcs["OE1XAR-62"]
    own62 = t62["own_tx"]
    by_so = q5["tx_by_source_and_origin"]
    g62 = by_so["OE1XAR-62/g"]
    r62 = by_so["OE1XAR-62/r"]
    s33 = srcs["OE1XAR-33"]
    s63 = srcs["OE1XAR-63"]
    add(table(["Kennung", "Telegramme (IDs)", "Takt Median", "Weg zum Knoten",
               "vom Knoten gesendet"],
              [["OE1XAR-62", n(t62["distinct_ids"]), f"{n(cad_['OE1XAR-62']['gap_s']['p50'])} s",
                f"Internet (UDP) {n(t62['udp_injected'])}, Funk {n(t62['rf_distinct_ids'])} IDs",
                f"{n(own62)} ({n(g62)} als Gateway, {n(r62)} als Relay)"],
               ["OE1XAR-33", n(s33["distinct_ids"]), f"{n(cad_['OE1XAR-33']['gap_s']['p50'])} s",
                f"nur Funk, {n(s33['rf_distinct_ids'])} IDs",
                f"{n(s33['own_tx'])} Relays ({pc(s33['own_tx'], s33['distinct_ids'], 0)} der Telegramme)"],
               ["OE1XAR-63", n(s63["distinct_ids"]), f"{n(cad_['OE1XAR-63']['gap_s']['p50'])} s",
                f"nur Funk, {n(s63['rf_distinct_ids'])} IDs",
                f"{n(s63['own_tx'])} Relays ({pc(s63['own_tx'], s63['distinct_ids'], 0)} der Telegramme)"]],
              num_cols=[1]))
    share_rel = 100.0 * (s33["own_tx"] + s63["own_tx"]) / cet_tx
    add(bars([("OE1XAR-62 als Gateway", g62), ("OE1XAR-33 als Relay", s33["own_tx"]),
              ("OE1XAR-63 als Relay", s63["own_tx"]), ("OE1XAR-62 als Relay", r62)],
             cap(f"{n(cet_tx)} Aussendungen {CET} in {n(hours, 0)} h ({n(q5['cet_tx_per_h'], 1)} je Stunde): "
                 f"{n(share_rel, 0)} % davon sind Weitergaben der zwei Funk-Kennungen"), unit=" Stück"))
    combos = q5["slots_by_source_combo"]
    full = combos["OE1XAR-33+OE1XAR-62+OE1XAR-63"]
    m_gap = q5["source_phase"]["merged_gap_s"]
    add("<ul>"
        f"<li><b>Kosten Senden:</b> {n(q5['cet_tx_airtime_s_per_h'], 1)} s je Stunde = "
        f"{n(q5['rawpass']['tx_cet_share_airtime_pct'], 1)} % der eigenen Sendezeit "
        f"({n(cet_air_s, 1)} von {n(cet_air_all, 1)} s) und {n(q5['cet_share_of_tx_count_pct'], 1)} % der "
        "Sendeereignisse.</li>"
        f"<li><b>Kosten Empfangen:</b> {n(q5['cet_rx_airtime_s_per_h'], 1)} s je Stunde = "
        f"{n(q5['rawpass']['rx_cet_share_of_decoded_airtime_pct'], 1)} % der empfangenen Sendezeit "
        "(Vergleich österreichisches Netz: 3 bis 5 % bei einer Kennung).</li>"
        f"<li><b>Redundanz:</b> {n(full)} von {n(q5['slots_total'])} Zehn-Minuten-Fenstern enthalten alle drei "
        "Kennungen. Die Takte driften gegeneinander "
        f"({n(cad_['OE1XAR-62']['gap_s']['p50'])}/{n(cad_['OE1XAR-33']['gap_s']['p50'])}/"
        f"{n(cad_['OE1XAR-63']['gap_s']['p50'])} s); die Lücke zwischen zwei Telegrammen beträgt "
        f"zusammengelegt im Median {n(m_gap['p50'])} s, im 90. Perzentil {n(m_gap['p90'])} s.</li>"
        "<li><b>Korrekte Zeit:</b> kein falsches Telegramm gesehen; Inhalt ist UTC (das Etikett "
        f"„CET“ stimmt nicht). Alle {n(q5['trusted_flags']['1'])} Zeitsignal-Zeilen tragen "
        "<code>trusted;1</code>.</li>"
        f"<li><b>Doppelsendung:</b> {n(q5['tx_src_g_after_overheard'][0])} von "
        f"{n(q5['tx_src_g_after_overheard'][1])} eigenen Gateway-Aussendungen "
        f"({pc(q5['tx_src_g_after_overheard'][0], q5['tx_src_g_after_overheard'][1], 0)}) folgten auf "
        "ein bereits gehörtes Gateway-Signal derselben ID (siehe Verdachtsfälle).</li>"
        f"<li><b>ID-Kollision -33/-63:</b> {n(ovl['shared_33_63'])} von {n(ovl['ids_63'])} IDs der Kennung -63 "
        f"({n(ovl['shared_33_63_pct_of_63'], 0)} %) haben vorher die Kennung -33 getragen; {n(reuse['n'])} davon "
        "haben wir zweimal gesendet. Ausführlich im nächsten Abschnitt.</li></ul>")
    add(explain(
        s_box(10, 12, 130, 36, "d-neu") + s_txt(75, 35, "OE1XAR-33", small=True)
        + s_box(10, 62, 130, 36, "d-neu") + s_txt(75, 85, "OE1XAR-62", small=True)
        + s_box(10, 112, 130, 36, "d-neu") + s_txt(75, 135, "OE1XAR-63", small=True)
        + s_line(140, 30, 270, 80, "d-watch-s anim-flow")
        + s_line(140, 80, 270, 85, "d-ok-s anim-flow")
        + s_line(140, 130, 270, 92, "d-watch-s anim-flow")
        + s_txt(205, 168, "-62: Internet, -33/-63: Funk", small=True)
        + s_node(320, 85, 48, "d-acc") + s_txt(320, 82, "DB0AIS", small=True)
        + s_txt(320, 100, "sendet alle", small=True)
        + s_line(368, 85, 470, 85, "d-acc-s anim-flow") + s_head(470, 85, "r")
        + s_box(470, 50, 160, 70, "d-watch", "anim-pulse")
        + s_txt(550, 78, f"{n(q5['cet_tx_per_h'], 1)} je Stunde")
        + s_txt(550, 100, f"{n(q5['rawpass']['tx_cet_share_airtime_pct'], 1)} % Sendezeit", small=True)
        + s_txt(320, 205, "Drei Telegramme je 10 Minuten sagen dasselbe, jedes wird gesendet",
                small=True),
        cap(f"Drei Zeitserver-Kennungen, ein Gateway: {CET} belegt "
            f"{n(q5['rawpass']['tx_cet_share_airtime_pct'], 0)} % der Sendezeit"),
        height=220))

    # ---- ID collision
    ex0 = reuse["pairs"][0]
    add("<h3>Dieselbe ID zweimal weitergegeben: -33 und -63 teilen den ID-Zähler (Beobachten)</h3>")
    add("<ul>"
        f"<li><b>Befund:</b> {n(reuse['n'])} Nachrichten-IDs haben wir zweimal weitergegeben, jeweils zuerst "
        f"als OE1XAR-33 und danach als OE1XAR-63. Die beiden Telegramme sind <b>verschieden</b> "
        f"({n(reuse['same_payload_n'])} gleiche Nutzlasten): Beispiel ID {ex0['id']}, einmal mit der Zeit "
        f"{ex0['first_stamp'][11:]} UTC (-33), einmal mit {ex0['second_stamp'][11:]} UTC (-63). Es ist also "
        "<b>keine Wiedereinspeisung desselben Telegramms</b> und kein Fehler des Knotens, sondern eine ID, die "
        "zwei Zeitserver-Kennungen benutzen.</li>"
        f"<li><b>Abstand:</b> {n(len(gaps_main))} der {n(reuse['n'])} Paare liegen "
        f"{hm(min(gaps_main))} bis {hm(max(gaps_main))} auseinander (Unterschied der Zeit in der Nutzlast), eines "
        f"{hm(min(gaps))}. Die Kennung -63 wiederholt die ID-Folge von -33 also mit einem nahezu festen Rückstand "
        f"von rund 2,9 Stunden. Insgesamt tragen {n(ovl['shared_33_63'])} der {n(ovl['ids_63'])} -63-IDs "
        f"({n(ovl['shared_33_63_pct_of_63'], 0)} %) auch eine -33-Meldung (Q5 zählt ebenfalls {n(ovl['shared_33_63'])}); "
        f"mit der Kennung -62 überschneidet sich keine ID ({n(ovl['shared_62_with_33_or_63'])}). Warum -63 so "
        "arbeitet, ist unbekannt (Vermutung: eine zweite Server-Instanz mit demselben Zähler, der zeitversetzt "
        "läuft).</li>"
        "<li><b>Einordnung:</b> Eine Nachrichten-ID ist nur etwa 4 Stunden eindeutig. 2,9 Stunden liegen "
        "innerhalb dieser Frist, es ist also eine <b>echte Kollision</b> und kein Auswerteartefakt "
        "(bei mehr als 4 Stunden Abstand wäre es keine mehr).</li>"
        f"<li><b>Nicht jede gemeinsame ID wurde doppelt gesendet:</b> bei den übrigen "
        f"{n(ovl['shared_33_63'] - reuse['n'])} gemeinsamen IDs war mindestens eine der beiden Meldungen nicht "
        f"von uns gesendet (die Kennung -63 geben wir nur zu {pc(s63['own_tx'], s63['distinct_ids'], 0)} weiter, "
        f"-33 zu {pc(s33['own_tx'], s33['distinct_ids'], 0)}).</li>"
        f"<li><b>Was es kostet:</b> keine zusätzliche Sendezeit, denn es sind {n(reuse['n'])} verschiedene "
        f"Telegramme, die einzeln weiterzugeben sind (zusammen etwa {n(reuse['n'] * cet_air_s / cet_tx)} s, bereits in den "
        f"{n(q5['rawpass']['tx_cet_share_airtime_pct'], 1)} % oben enthalten). Das Risiko liegt woanders: Der "
        f"Dedup-Ring dieses Knotens ({n(ring_slots)} Plätze) vergisst eine ID nach "
        f"{n(ddw['median'])} Minuten im Median ({n(ddw['min'])} bis {n(ddw['max'])} Minuten, aus dem Slot-Zähler der "
        f"{n(ex['dedup_window']['adds'])} Einträge). Das -63-Telegramm kommt das {n(min(gaps_main) / 60 / ddw['max'], 0)}- "
        f"bis {n(max(gaps_main) / 60 / ddw['min'], 0)}-Fache dieses Gedächtnisses später und wird deshalb als neu behandelt "
        f"({n(ovl['first_sight_of_63_flagged_dup'])} Erstempfänge als Duplikat markiert). Ein Knoten, dessen "
        "Ring länger als etwa 2,4 bis 2,9 Stunden zurückreicht, würde das -63-Telegramm still als Duplikat "
        "von -33 verwerfen, ohne Spur außer einer RX_DEDUP_DUP-Zeile. Das ist eine Modellrechnung, hier nicht "
        "eingetreten.</li>"
        f"<li><b>Abweichung zu Q6:</b> Q6 schätzt das Ringgedächtnis dieses Knotens auf "
        f"{n(q6['loops']['dedup_window_min_est'], 0)} Minuten. Aus den Rohzeilen folgt "
        f"{n(ddw['median'], 0)} Minuten (die Slots laufen lückenlos 0 bis 99, {n(ex['dedup_window']['adds'])} "
        "Einträge in 25 Stunden). Diese Seite nutzt den Rohwert; Q6 ist zu prüfen.</li></ul>")
    add(explain(
        s_box(10, 15, 210, 54, "d-neu") + s_txt(115, 38, "OE1XAR-33: ID " + ex0["id"][-4:], small=True)
        + s_txt(115, 58, ex0["first_stamp"][11:16] + " UTC", small=True)
        + s_box(10, 95, 210, 54, "d-watch", "anim-pulse")
        + s_txt(115, 118, "OE1XAR-63: gleiche ID " + ex0["id"][-4:], small=True)
        + s_txt(115, 138, "rund 2,9 h spaeter, andere Zeit", small=True)
        + s_line(220, 42, 300, 72, "d-neu") + s_line(220, 122, 300, 92, "d-watch-s anim-flow")
        + s_node(340, 82, 40, "d-acc") + s_txt(340, 86, "DB0AIS", small=True)
        + s_box(420, 30, 210, 104, "d-ok")
        + s_txt(525, 56, f"Dedup-Ring {ring_slots} Plaetze", small=True)
        + s_txt(525, 80, f"Umlauf ca. {n(ddw['median'], 0)} min", small=True)
        + s_txt(525, 104, "ID laengst vergessen", small=True)
        + s_txt(525, 124, "beide werden gesendet", small=True)
        + s_txt(320, 178, "Kein Verlust hier, weil der Ring kurz ist.", small=True)
        + s_txt(320, 200, "Ein Ring von mehr als 2,9 h wuerde -63 als Duplikat verwerfen.", small=True),
        cap("Zwei Zeitserver-Kennungen teilen eine ID: nur das kurze Gedächtnis des Rings verhindert "
            "den Verlust"), height=215))

    add("<h3>Knotenuhr um eine Stunde zurückgesprungen (Beobachten)</h3>")
    clk = res["clock"]["offset_dist_s"]
    add("<ul>"
        f"<li><b>Befund:</b> Die Knotenuhr lief bis {cs['time_before'][:5]} Knotenzeit bei UTC+2 "
        f"({n(clk['3600'])} Messpunkte mit +3600 s gegen den Logger), danach bei UTC+1 ({n(clk['0'])} Messpunkte "
        f"mit 0 s). Im Mitschnitt steht in Zeile {n(cs['line_before'])} die Zeit {cs['time_before']}, in Zeile "
        f"{n(cs['line_after'])} die Zeit {cs['time_after']}; dazwischen liegen {n(elapsed_s, 1)} s Betriebszeit. "
        "Der Sprung ist also genau −1 h.</li>"
        "<li><b>Auslöser im Log:</b> zwischen den beiden Zeilen steht ein <code>[SETST];save;encoded</code> mit "
        "<code>save;ok</code> (Einstellungen wurden wirklich gespeichert, nicht „unchanged“ wie die "
        "Speichervorgänge davor). Die Ursache nennt das Log nicht. <b>Vermutung</b>: eine Änderung der "
        "Zeitzonen- oder Offset-Einstellung am Gerät; die Uhr übernimmt sie über die Minutenroutine in "
        "<code>src/clock.cpp:153-165</code> (<code>tzApplyNow()</code>).</li>"
        "<li><b>Wirkung:</b> Das Alter der Zeitsignale wird aus UTC gerechnet, die Sperre ist "
        f"also nicht betroffen (Zeitsignal von -62 hat Alter 0 s, n={n(q5['udp_age_s']['n'])}). Betroffen sind "
        "nur Anzeige- und Log-Zeit.</li>"
        "<li><b>Annahme:</b> Der Knoten trug seit dem Sommer einen festen Offset von +2 h "
        "(Vermutung, die Einstellung steht nicht im Log).</li></ul>")
    add(explain(
        s_box(20, 20, 250, 70, "d-watch") + s_txt(145, 48, "Knotenuhr vorher")
        + s_txt(145, 72, f"{cs['time_before']} (UTC+2)", small=True)
        + s_box(370, 20, 250, 70, "d-ok") + s_txt(495, 48, "Knotenuhr nachher")
        + s_txt(495, 72, f"{cs['time_after']} (UTC+1)", small=True)
        + s_line(270, 55, 366, 55, "d-acc-s anim-flow") + s_head(366, 55, "r")
        + s_txt(318, 45, "-1 h", small=True)
        + s_box(20, 115, 250, 40, "d-neu") + s_txt(145, 140, "[SETST] save ok (geaendert)", small=True)
        + s_box(370, 115, 250, 40, "d-ok") + s_txt(495, 140, "Altersrechnung bleibt UTC", small=True)
        + s_line(145, 90, 145, 115, "d-neu") + s_line(495, 90, 495, 115, "d-ok-s")
        + s_txt(320, 190, "Ursache nicht im Log (Vermutung: Zeitzonen-Einstellung)", small=True),
        cap("Die Knotenuhr springt nach einem Speichervorgang um eine Stunde zurück; die "
            "Altersprüfung rechnet in UTC"), height=210))

    # ------------------------------------------------------------ F6
    add("<h2>F6 Manipulationen dieses Knotens</h2>")
    add(f"<p>Wo greift die Firmware in Pakete ein und wie wirkt das? Zahlen aus dem {n(hours, 0)}-h-Log.</p>")
    rly = own_tx["relay_decisions_RLY"]
    echoes = q6t["echo"]
    add('<div class="grid3">'
        '<div class="card pos"><h3>Positiv</h3><ul>'
        f"<li><b>Eigene Baken vollständig:</b> {n(own_tx['src_type']['o:@'])} HEY und "
        f"{n(own_tx['src_type']['o:!'])} Wetter-/Positionsmeldungen gesendet, 0 verworfen. "
        f"Für {n(echoes)} von {n(own_tx['src_split']['o'])} eigenen Rahmen hörte der Knoten ein Echo eines "
        "Nachbarn (Q6 totals.echo, hier nicht nachgeprüft).</li>"
        f"<li><b>{CET}-Sperre:</b> {n(rly['cet'])} veraltete fremde Zeitsignale nicht weitergegeben.</li>"
        f"<li><b>Weiterleitungsregeln:</b> {n(rly['hop0'])} Rahmen ohne Resthop nicht weitergegeben (hop0), "
        f"{n(rly['gwcap'])} Texte oder Positionen wegen der Hop-Obergrenze am Gateway nicht weitergegeben "
        "(gwcap, Typ nicht aufgeschlüsselt): spart Sendezeit.</li>"
        f"<li><b>Keine Schleifen:</b> {n(ring['relay_loop_blocked'])} RELAY_LOOP_BLOCKED, kein Hop-Zähler über "
        f"{n(max(int(k[1:]) for k in tr['hop_H_field']))}.</li>"
        "</ul></div>"
        '<div class="card neg"><h3>Negativ</h3><ul>'
        f"<li><b>Fall A bei {pc(q6t['need_A'], need_n, 0)} der NEED-Entscheidungen:</b> alle "
        f"{n(q6t['need_A'])} ergaben Fall A, 0 CANCEL. {n(q5['tx_src_g_after_overheard'][0])} von "
        f"{n(q5['tx_src_g_after_overheard'][1])} eigenen Zeitsignal-Aussendungen folgten auf ein gehörtes "
        "Gateway-Signal.</li>"
        f"<li><b>{CET} dreifach:</b> {n(q5['cet_share_of_tx_count_pct'], 1)} % der Sendeereignisse (siehe F5).</li>"
        f"<li><b>ID-Kollision -33/-63:</b> {n(reuse['n'])} IDs zweimal gesendet; nur das kurze Ringgedächtnis "
        "verhindert, dass -63 als Duplikat verschwindet.</li>"
        "</ul></div>"
        '<div class="card neu"><h3>Neutral</h3><ul>'
        f"<li><b>Gateway-Upload:</b> {n(eth['gwu_n'])} Uploads "
        f"Mesh→Server, {n(eth['gwi_n'])} Server→Mesh, {n(eth['ext_tx_n'])} EXT-UDP-Ausgänge; keine Funkkosten "
        "für den Upload.</li>"
        f"<li><b>Supersede:</b> {n(sup['events'])} Ersetzungen, alle ersetzten Einträge nie gesendet.</li>"
        f"<li><b>Relay der Heartbeats:</b> {n(own_tx['src_type']['r:@'])} fremde HEY weitergegeben "
        f"({n(ty['@']['pct'], 0)} % der empfangenen Rahmen sind HEY); Nutzen in diesem Log nicht gemessen.</li>"
        f"<li><b>Quittungs-Weitergabe:</b> nur {n(own_tx['src_type']['r:A'])} (ohne Abbruchpfad); Quittungen machen "
        f"{n(sh['ACK'], 1)} % der empfangenen Sendezeit aus.</li>"
        f"<li><b>Eigene Baken warten länger als Relays:</b> Median gleich, 90. Perzentil "
        f"{n(wait['o/@/p5']['wait_p90_s'], 1)} s gegen {n(wait['r/@/p5']['wait_p90_s'], 1)} s (siehe F2); "
        "ohne Folgen bei dieser Last.</li></ul></div></div>")

    # ------------------------------------------------------------ Verdachtsfälle
    add("<h2>Verdachtsfälle Firmware-Bugs</h2>")
    add("<p>Nur Punkte, die das Log berühren. Keiner ist als Fehler bewiesen.</p>")
    add("<h3>Fall A bei jeder NEED-Entscheidung: die Menge „alone“ ist nie leer (Beobachten, Design)</h3>")
    rw = ring["ring_write_by_src_type"]
    add("<ul>"
        "<li><b>Beobachtung:</b> Jeder Relay-Eintrag und jede Einspeisung bekommt vor dem "
        f"Senden eine NEED-Zeile. Alle {n(q6t['need_A'])} sind Fall A (die Menge „alone“, also die Nachbarn, "
        f"für die der Knoten der einzige Anbieter wäre, ist nie leer): Heartbeat {n(rw['rx_relay:40'])}, Text "
        f"{n(rw['rx_relay:3A'] + rw['udp_rx:3A'])} (davon {n(rw['udp_rx:3A'])} vom Server), Position "
        f"{n(rw['rx_relay:21'])}. 0 <code>[NBR]|CANCEL</code>, {n(q6t['refuse'])} REFUSE.</li>"
        f"<li><b>Beleg:</b> {CET}: alle {n(nc['total'])} NEED-Zeilen für Zeitsignal-Rahmen sind Fall A "
        f"({n(nc['case_A'])}/{n(nc['total'])}; Q5 und extra/db0ais_measure.json), und "
        f"{n(q5['tx_src_g_after_overheard'][0])} von {n(q5['tx_src_g_after_overheard'][1])} Gateway-Aussendungen von "
        "-62 kamen nach einem gehörten Signal desselben Telegramms. Die Menge „alone“ hat im Median "
        f"{n(alone_median)} Einträge (nur in {n(alone['alone_exactly_one'])} Fällen genau einen). Am häufigsten "
        f"stehen darin {top_alone[0][0]} ({pc(top_alone[0][1], need_n, 0)} der NEEDs) und {top_alone[1][0]} "
        f"({pc(top_alone[1][1], need_n, 0)}). Ob der Knoten tatsächlich für sie der einzige Anbieter ist, "
        "oder ob die Matrix Rückwege nur aus Symmetrie schließt, zeigt das Log nicht.</li>"
        "<li><b>Code-Stelle:</b> <code>src/nbr_matrix.cpp:1777</code> und <code>:1816-1852</code> "
        "(Test auf einen alternativen Anbieter und Abhängigenregel; aus Symmetrie abgeleitete Kanten können "
        "den Rückweg liefern, <code>:1829-1848</code>), <code>src/lora_functions.cpp:1380</code> (REFUSE bei "
        "nicht leerem „alone“), <code>src/nbr_matrix.h:795-800</code>, <code>src/gw_udp_ring.h</code>.</li>"
        "<li><b>Status:</b> Beobachten (Design/Parameter, nicht als Programmfehler bewiesen). Die Frage ist "
        "nicht, ob „need“ gleich „alone“ ist (das trifft fast nie zu), sondern <b>warum die Menge „alone“ für "
        "einen Server-Slot nie leer ist</b>. Ob der Abbruchmodus an diesem Knoten eingeschaltet ist, lässt "
        "sich aus dem Log nicht beweisen. Gleiche Fehlerklasse wie GWRX-01 "
        "(<code>docs/rca-gwrx01-20261023.md</code>).</li></ul>")
    add(explain(
        s_box(10, 20, 180, 70, "d-acc") + s_txt(100, 48, "Server-Telegramm")
        + s_txt(100, 72, "im Puffer (Slot)", small=True)
        + s_box(230, 20, 180, 70, "d-watch") + s_txt(320, 48, "Fremdes Gateway")
        + s_txt(320, 72, "sendet es zuerst", small=True)
        + s_box(450, 20, 180, 70, "d-act", "anim-pulse") + s_txt(540, 48, "NEED: Fall A")
        + s_txt(540, 72, "alone nie leer = Veto", small=True)
        + s_line(190, 55, 230, 55, "d-acc-s") + s_line(410, 55, 450, 55, "d-acc-s")
        + s_line(540, 90, 540, 130, "d-act-s anim-flow") + s_head(540, 130, "d", "d-act-f")
        + s_box(430, 135, 200, 50, "d-act", "anim-drop") + s_txt(530, 165, "Wird trotzdem gesendet", small=True)
        + s_txt(210, 170, f"{q6t['need_A']} von {need_n} NEED = Fall A, 0 CANCEL", small=True)
        + s_txt(210, 195, f"{q5['tx_src_g_after_overheard'][0]} von {q5['tx_src_g_after_overheard'][1]} "
                          "Zeitsignalen doppelt", small=True),
        cap("Der Veto-Fall A schützt immer: Abbruch nach gehörter Fremdaussendung kam nie vor"),
        height=215))
    add("<h3>Nachrichten-ID von zwei Zeitservern geteilt (Beobachten, Ursache ungeklärt)</h3>")
    add("<ul>"
        f"<li><b>Beobachtung:</b> {n(ovl['shared_33_63'])} IDs kommen mit -33 und mit -63 vor, in "
        f"{n(reuse['n'])} Fällen haben wir beide gesendet (Abstand rund 2,9 h, verschiedene Nutzlast).</li>"
        f"<li><b>Beleg:</b> extra/db0ais_measure.json (aus den Rohzeilen: RING_TX_READ je ID, Nutzlast aus "
        f"TX-LoRa, Slot-Zähler aus RX_DEDUP_ADD). Q5 zählt dieselben {n(ovl['shared_33_63'])} Kollisionen.</li>"
        "<li><b>Code-Stelle:</b> der Duplikattest vergleicht nur die 4 Byte der Nachrichten-ID, nicht Absender "
        "oder Nutzlast (<code>src/dedup_functions.cpp:25-31</code>), und der Ring wird lückenlos reihum "
        "beschrieben (<code>addLoraRxBuffer()</code>, <code>:55-75</code>); die Zeitserver-Seite liegt außerhalb "
        "dieses Repos.</li>"
        "<li><b>Status:</b> Verdacht, Server-Konfiguration. Falls die Zeitserver nicht angepasst werden können, "
        f"wäre die Gegenmaßnahme eine zusätzliche Prüfung der Nutzlast beim Duplikattest für {CET}-Rahmen "
        "(ungetestet, nur Vorschlag).</li></ul>")

    # ------------------------------------------------------------ Nachbarn
    add("<h2>Nachbarn und Hardware-Inventar</h2>")
    add("<p>Direkte Nachbarn sind die letzten Stationen im Pfad. RSSI und SNR gelten nur für "
        "diesen letzten Hop. Dass der 90. Perzentilwert bei fast allen Nachbarn nahe −75 dBm "
        "liegt, ist auffällig; Vermutung: RSSI bildet die Gesamtleistung im Band ab, also "
        "sind Median und 10. Perzentil aussagekräftiger.</p>")

    def nb_row(call: str, n_hits: int | None = None, rssi: str = "", snr: str = "",
               fw_log: str = "", hw_log: str = "", dist: Any = None) -> list[str]:
        m = master.get(call)
        if m is None:
            note = "keine Stammdaten"
            if hw_log:
                note += f" (laut Log: {hw_log}, Firmware {fw_log})"
            return [call, note, "", "", "", "", rssi, snr, str(n_hits or "")]
        loc = (m["description"] or f"{m['lat']}, {m['lon']}")
        d = f"{dist:.1f}".replace(".", ",") + " km" if dist not in (None, "") else ""
        return [call, m["hardware"], m["firmware"], loc, f"{m['altitudeM']} m", d, rssi, snr,
                n(n_hits) if n_hits is not None else ""]

    nb = [x for x in res["neighbours"]["top"] if x["call"] != "DB0AIS-12"]
    nb_rows = [nb_row(x["call"], x["n"], f"{int(x['rssi_median'])} dBm",
                      f"{int(x['snr_median'])} dB", x.get("fw_log", ""), x.get("hw", ""),
                      x.get("dist_km")) for x in nb]
    head = ["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz", "RSSI Median",
            "SNR Median", "Rahmen"]
    top3 = sum(x["n"] for x in nb[:3])
    add(table(head, nb_rows[:3], num_cols=[5, 6, 7, 8],
              caption=f"Die drei häufigsten direkten Nachbarn (zusammen {n(top3)} Rahmen)."))
    add(f"<details><summary>Alle direkten Nachbarn ({n(len(nb))}, ohne Echo der eigenen Aussendung)"
        "</summary>" + table(head, nb_rows, num_cols=[5, 6, 7, 8]) + "</details>")
    pre = fc["top_pre_cad"]
    pre_rows = []
    for x in pre:
        r = nb_row(x["call"], x["events"], "", "", x["fw"], x["hw"], x["dist_km"])
        pre_rows.append([r[0], r[1], x["fw"], r[3], r[4], r[5], f"{x['events']}", f"{x['msgs']}"])
    add("<details><summary>Stationen ohne Kanalabfrage (Firmware 4.35a bis o), die meisten "
        "Ereignisse</summary>"
        + table(["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz", "Ereignisse",
                 "Nachrichten"], pre_rows, num_cols=[5, 6, 7])
        + "</details>")
    add("<p class=\"small\">Die eigene Station DB0AIS-12 hat 2 Echo-Einträge im Pfad und ist "
        "hier nicht aufgeführt. DA6WT-20 und DB4MLA-12 sind in der Karte nicht verzeichnet.</p>")

    # ------------------------------------------------------------ Grenzen
    add("<h2>Was nicht geprüft ist / Grenzen</h2>")
    add("<p>Das Log hat keinen Fehler gezeigt, der Einstellungen kosten würde; die folgenden Punkte "
        "sind Lücken der Auswertung.</p><ul>"
        "<li><b>DHCP-Erneuerung:</b> nicht ausgelöst, nicht belegt (siehe ETH-04).</li>"
        "<li><b>Router-Stand:</b> <code>always-broadcast</code> und die Lease-Länge sind aus "
        "dem Log nicht ablesbar; die Lease-Länge von einer Woche stammt aus den "
        "ETH-04-Unterlagen.</li>"
        "<li><b>Build:</b> unbekannt; die ETH-04-Korrektur F3 ist über den Marker belegt, F1/F2 und der "
        "Slot-Pool-Stand sind aus Log-Markern geschlossen (Vermutung).</li>"
        "<li><b>Server-Empfang:</b> ob der Server die "
        f"{n(eth['gwu_n'])} Uploads erhielt, ist nicht prüfbar.</li>"
        "<li><b>Zeitbasis:</b> keine Wandzeit im Mitschnitt; alle Uhrzeiten sind abgeleitet "
        "(±5 s).</li>"
        "<li><b>Querauswertung:</b> keine; anderes Netz, anderer Tag. Netzvergleich nur in "
        "der Übersicht.</li>"
        "<li><b>ID-Kollision -33/-63:</b> nicht geprüft, ob andere Knoten im hessischen Netz ein "
        "-63-Telegramm wegen der Kollision verworfen haben; dieses Log sieht nur den eigenen Ring.</li>"
        "<li><b>Gegenrechnung ohne Zeitverfall:</b> für diesen Knoten nicht gerechnet, da "
        "nichts verfiel.</li></ul>")

    # ------------------------------------------------------------ offene Fragen
    add("<h2>Offene Fragen an den Betreiber</h2><ol>"
        "<li>Steht am MikroTik inzwischen <code>always-broadcast=yes</code>, und welche "
        "Lease-Zeit gilt für DB0AIS?</li>"
        f"<li>Wurde um {cs['time_before'][:5]} Knotenzeit (Mitschnitt Zeile {n(cs['line_before'])}) am Knoten eine "
        "Zeitzone oder ein Offset geändert (Speichervorgang direkt vor dem Uhrsprung)?</li>"
        "<li>Was ist in der Nacht zwischen 22 und 03 Uhr hinter dem Knoten los (Router, "
        "Switch, Hamnet-Strecke)? Gleiche Ausfälle in anderen Logs desselben Standorts?</li>"
        "<li>Woher kommt OE1XAR-63, und brauchen DL-Gateways alle drei Kennungen? Warum benutzt -63 "
        "die ID-Folge von -33 mit einem Rückstand von rund 2,9 Stunden?</li>"
        "<li>Kann ein Mitschnitt über mehr als 4 Tage laufen (Lease-Erneuerung)?</li></ol>")

    # ------------------------------------------------------------ Entwickler
    add('<details class="dev"><summary>Methodik, Marker, Code-Stellen, Skripte (technisch)'
        "</summary>"
        "<h3>Zeitbasis</h3><ul>"
        "<li>Kein Wandstempel je Zeile. Zeit = PuTTY-Start 17:01:11 (PC-Uhr, MEZ) plus "
        "Knoten-Millisekunden (<code>t=</code>, <code>ms;</code>); die NTP-Epoche "
        f"(<code>[NTP];ok;epoch</code>) wurde gegengeprüft ({n(gw['ntp']['ok'])} Messungen).</li>"
        "<li>Knotenuhr-Stempel der <code>[LOG]</code>-Zeilen: +3600 s gegen den Logger bis "
        f"Dateizeile {n(cs['line_before'])} ({cs['time_before']} Knotenzeit), danach 0 s "
        f"(Zeile {n(cs['line_after'])}: {cs['time_after']}); extra/db0ais_measure.json, Block "
        "<code>clock_step</code>. Der Sprung wurde in den Stundenmitteln berücksichtigt.</li></ul>"
        "<h3>Zahlenabgleich</h3><ul>"
        f"<li>RX_ERROR-Anteil: {n(err_n)} / ({n(err_n)} + {n(done_n)} OnRxDone + {n(ack_n)} Quittungen) = "
        f"{n(err_share, 2)} %. Quellen: health/results.json (rf.rx_error_n, markers_top.OnRxDone, "
        "traffic.ack_events), gegengeprüft mit extra/db0ais_measure.json (rx_attempts). Quittungsrahmen "
        "(12 Byte) schreiben keine OnRxDone-Zeile (<code>src/lora_functions.cpp:1018-1034</code>); der "
        "ältere Wert 66,9 % ohne sie war falsch.</li>"
        f"<li>Kanalauslastung: {n(chu['mean'], 1)} % = Mittel aus {n(chu['n'])} <code>CHANNEL_UTIL</code>-Zeilen; "
        f"{n(ch['STAT_util_5min']['mean'], 1)} % = Mittel der {n(ch['stat_rows'])} STAT-Zeilen (5-Minuten-Fenster). "
        f"Beide Quellen genannt, Kern: {n(chu['mean'], 1)} %.</li>"
        f"<li>Füllstand: {n(occ['queued']['max'])} aus <code>RING_STATUS queued</code>, "
        f"{n(res['stat']['ringmax_max'])} aus STAT-<code>ringmax</code>; die zweite Zahl zählt den Slot in "
        "Sendung mit.</li>"
        f"<li>Zeitsignal -62 über Internet: {n(gw['cet_gw_rx'])} (health/results.json, gateway.cet_gw_rx); Q5 zählt "
        f"{n(t62['distinct_ids'])} verschiedene -62-IDs, davon {n(t62['distinct_ids'] - t62['udp_injected'])} ohne "
        "UDP-Weg.</li>"
        f"<li>Die {n(nc['total'])} NEED im Q5-Bericht zählen nur Zeitsignal-Rahmen; die {n(q6t['need_A'])} hier alle "
        "Typen.</li>"
        f"<li>Dedup-Umlauf: Zeit zwischen dem n-ten und dem (n+100)-ten <code>RX_DEDUP_ADD</code>, "
        f"{n(ex['dedup_window']['adds'])} Einträge; q6/results.json (loops.dedup_window_min_est) weicht ab "
        "(siehe Abschnitt ID-Kollision).</li></ul>"
        "<h3>Marker (Auswahl)</h3><ul>"
        "<li><code>[ETH];event;link;down|up;ip;1;ms;…</code>, "
        "<code>[ETH];event;rng_seed;src;sd</code> "
        "(<code>src/nrf52/nrf52_main.cpp:512</code>), <code>[ETH];event;got_ip</code>, "
        "<code>[ETH];stall;dhcp_begin;ms;4171</code>.</li>"
        f"<li><code>[GW];keep;tx;ok;1</code> ({n(eth['keepalives'])}), <code>[GW];rx;type;BEAT</code> "
        f"({n(eth['beats'])}), <code>[UDP] Server not responding for 35s - ETH UP</code> "
        f"({n(eth['udp_not_responding_n'])}).</li>"
        f"<li><code>[CET];tx;age_s;…;trusted;1;wait_ms;…;prio;3</code> ({n(cet_tx)}), "
        f"<code>[CET];drop;where;relay;age_s;N</code> ({n(len(cet_drops))}).</li>"
        "<li><code>[NBR]|NEED|…|T|A|…</code> (Typ H/T/P, Fall A), "
        f"<code>[NBR]|REFUSE</code> ({n(q6t['refuse'])}), <code>[NBR]|CANCEL</code> (0).</li>"
        "<li><code>[SETST];save;encoded|ok|skipped_unchanged</code> um den Uhrsprung "
        f"(Zeilen {n(cs['line_before'] + 1)}/{n(cs['line_before'] + 2)}).</li>"
        "<li><code>RX_DEDUP_NEW|ADD|DUP msg_id=…</code> mit <code>slot=N/100</code>, "
        "<code>RING_TX_READ … msg_id=…</code> und <code>TX-LoRa … &lt;Pfad&gt;&gt;*:{CET}&lt;Zeit&gt;</code> für die "
        "ID-Kollision.</li></ul>"
        "<h3>Code-Stellen</h3><ul>"
        "<li>Wiederherstellung: <code>src/nrf52/nrf_eth.cpp</code> (<code>resetDHCP()</code>, "
        "<code>eth_recovery::decide()</code>), <code>src/nrf52/udp_drain_nrf52.cpp:86</code> "
        "(<code>MAX_ERR_UDP_TX</code>); Dokument "
        "<code>docs/issue-rak-dhcp-after-reboot-db0ais.md</code> (F3-Marker :291-294, Commit 708b5091).</li>"
        "<li>Relay-Gründe: <code>src/lora_functions.cpp:2377-2396</code> "
        "(gwcap für Texte und Positionen, gwfilter), :2477 (nomesh).</li>"
        "<li>TTL: <code>src/txring_ttl.h:46-64</code>, Sweep "
        "<code>src/txring_functions.cpp:566-640</code>; Altersboden "
        "<code>src/lora_functions.cpp:4030-4045</code>.</li>"
        f"<li>{CET}-Sperre: <code>src/cet_age.h:120-135</code> (150 s).</li>"
        "<li>Uhr: <code>src/clock.cpp:153-165</code> (Vermutung für den Uhrsprung).</li>"
        "<li>Dedup: <code>src/dedup_functions.cpp:25-31</code> (Vergleich der 4 Byte Nachrichten-ID), "
        "<code>:55-75</code> (Eintrag und Slot-Zähler).</li>"
        "<li>NEED/Fall A: <code>src/nbr_matrix.cpp:1777</code>, <code>:1816-1852</code>, "
        "<code>:1829-1848</code>.</li></ul>"
        "<h3>Quellen und Skripte</h3><ul>"
        "<li>health/findings.md (DB0AIS-Block, ETH-04), q2/q5/q6 findings und results.json, "
        "master/master.tsv, verify/health.md, verify/q5.md, verify/q6.md, data/CORE-FINDINGS.md (v2).</li>"
        "<li>Rohzeilen-Messung dieser Seite: <code>generators/lib/db0ais_measure.py</code> "
        "(<code>--norm &lt;Ordner mit db0ais.log&gt; --out data/extra/db0ais_measure.json</code>): Uhrsprung, "
        "doppelt weitergegebene IDs, ID-Überschneidung -33/-63, Dedup-Umlauf, Empfangsversuche.</li>"
        "<li>Generator: <code>generators/db0ais.py</code> (<code>--data --kit --out</code>).</li>"
        "</ul></details>")

    add('<div class="foot">Quellen: Mitschnitt db0ais.log (30.10.2026 17:01 bis 31.10.2026 '
        "18:09, PuTTY), Stammdaten aus mcmap, Auswertungen health/q2/q34/q5/q6 vom "
        "01.11.2026. Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware. "
        '<a href="uebersicht.html">Zur Übersicht</a>.</div>')

    html = page("MeshCom Log-Auswertung DB0AIS-12", "\n".join(b))
    out.write_text(html, encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--kit", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    build(a.data, a.kit, a.out)


if __name__ == "__main__":
    main()
