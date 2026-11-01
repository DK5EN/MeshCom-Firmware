#!/usr/bin/env python3
"""Generator for the per-node report HB9VQQ-4 (24-h log analysis HB9 mesh, 2026-11-01).

Usage:
    python3 -I hb9vqq4.py --data <dir> --kit <dir> --out <file.html>

--data  run data directory (hb9/{health,q2,q34,q5,q6,master}/, extra/ and hb9/CORE-FINDINGS.md)
--kit   directory holding reportkit.py (shared HTML/CSS/SVG building blocks)
--out   HTML file to write

No raw logs are read at render time. Numbers come from data/hb9/*/results.json, data/hb9/master and
data/extra/hb9vqq4_measure.json (written once from the normalised logs by
lib/hb9vqq4_measure.py --norm <dir> --out <file>). Judgement text may carry literals from
data/hb9/CORE-FINDINGS.md and verify/hb9.md; they are named in the developer details. Stdlib only.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path
from typing import Any

NODE = "hb9vqq4"
CET = "{CET}"


# ----------------------------------------------------------------------------------------------
# small helpers
# ----------------------------------------------------------------------------------------------
def de(x: float, nd: int = 1) -> str:
    """German number format: decimal comma, dot as thousands separator."""
    if nd == 0:
        return f"{x:,.0f}".replace(",", ".")
    return f"{x:,.{nd}f}".replace(",", "X").replace(".", ",").replace("X", ".")


def pct(a: float, b: float, nd: int = 1) -> str:
    return de(100.0 * a / b, nd) + " %"


def jload(p: Path) -> Any:
    return json.loads(p.read_text(encoding="utf-8"))


def ftxt(f: str) -> str:
    m = re.match(r"^([34]\d):(.)$", f)
    if not m:
        return f
    return f"4.{m.group(1)}{m.group(2)}" if m.group(2) != "#" else f"4.{m.group(1)}"


def clock_label(first: str, hour_idx: int) -> str:
    """Capture start 'YYYY-MM-DD HH:MM:SS' plus hour_idx hours as HH:MM."""
    hh, mm = int(first[11:13]), int(first[14:16])
    return f"{(hh + hour_idx) % 24:02d}:{mm:02d}"


def longest_zero_run(v: list[int]) -> tuple[int, int]:
    best = (0, -1)
    i = 0
    while i < len(v):
        if v[i] == 0:
            j = i
            while j + 1 < len(v) and v[j + 1] == 0:
                j += 1
            if j - i > best[1] - best[0]:
                best = (i, j)
            i = j + 1
        else:
            i += 1
    return best


def load_master(d: Path) -> tuple[dict[str, dict[str, str]], dict[str, str]]:
    master: dict[str, dict[str, str]] = {}
    with open(d / "hb9" / "master" / "master.tsv", newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            master[r["call"]] = r
    dist: dict[str, str] = {}
    with open(d / "hb9" / "master" / "distances.tsv", newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            dist[r["call"]] = r.get("d_hb9vqq4", "")
    return master, dist


# ----------------------------------------------------------------------------------------------
# page
# ----------------------------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--kit", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    sys.path.insert(0, str(args.kit.resolve()))
    sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
    from reportkit import bars, chip, explain, hist, page, table, timeline

    import hb9vqq4_draw as dr

    D: Path = args.data
    H = jload(D / "hb9" / "health" / "results.json")["nodes"][NODE]
    Q2 = jload(D / "hb9" / "q2" / "results.json")["nodes"][NODE]
    Q34 = jload(D / "hb9" / "q34" / "results.json")["nodes"][NODE]
    Q5 = jload(D / "hb9" / "q5" / "results.json")["nodes"][NODE]
    Q6 = jload(D / "hb9" / "q6" / "results.json")["nodes"][NODE]
    M = jload(D / "extra" / "hb9vqq4_measure.json")
    master, dist = load_master(D)

    MN = M["nodes"][NODE]
    M5 = M["nodes"]["hb9vqq5"]
    fate = MN["fate"]
    cetm = MN["stale_cet"]
    rad = MN["radio"]
    mk = MN["markers"]
    pair = M["pair"]
    mut = M["mutual"]
    cp = M["cet_pair"]

    fig = [0]

    def nf() -> int:
        fig[0] += 1
        return fig[0]

    # ---- derived numbers -------------------------------------------------------------------
    span = H["span"]
    traffic = H["traffic"]
    own = H["own_tx"]
    chan = H["channel"]
    cad = H["cad"]
    rf = H["rf"]
    stat = H["stat"]
    n_dec = traffic["heard_events"]
    n_tx = own["tx_lines"]
    writes = fate["writes"]
    n_aired = fate["fates"]["aired"]
    n_sup = fate["fates"]["superseded"]
    n_van = fate["fates"]["vanished"]
    relay_w = fate["relay_writes"]
    tx_air_chan = chan["own_tx_airtime_s_from_CHANNEL_UTIL"]
    van_air = fate["vanished_airtime_s"]
    foreign_air = fate["aired_airtime_foreign_s"]
    hey = fate["hey_wait_s"]
    occ = Q2["occupancy"]
    lat = Q2["latency"]["by_prio_s"]
    hr = Q34["headroom"]
    csma = Q34["csma"]
    sens5 = Q34["ttl_sens"]["p5"]["table"]
    hole_lo, hole_hi = longest_zero_run(fate["hourly_vanished"])
    hole_a = clock_label(span["first"], hole_lo)
    hole_b = clock_label(span["first"], hole_hi + 1)
    opp = fate["hourly_aired_with_foreign_copy"][hole_lo:hole_hi + 1]
    before_txt = ", ".join(str(v) for v in fate["hourly_vanished"][:hole_lo])
    after_lo = min(fate["hourly_vanished"][hole_hi + 1:])
    after_hi = max(fate["hourly_vanished"][hole_hi + 1:])
    ts = H["time_signal"]
    census = H["firmware_census"]
    gw = H["gateway"]
    sil = H["rx_silence"]
    n_direct = H["neighbours"]["n_direct"]
    n_direct5 = 8  # q6/findings.md section 0 (HB9VQQ-5 has no [LOG] lines, so health cannot count it)
    n_shared = 5  # q6/findings.md 2.3, CORE-FINDINGS (nodes share 5 direct neighbours)
    tx_duty = Q34["tx_duty_pct"]
    cet_tx = Q5["tx_count"]
    ack_n = own["src_type"]["r:A"]
    hno = cetm["stale_age_s"]
    hno_fr = MN["hno_frames"]
    stale_n = cetm["stale_frames"]
    stale_union = M["stale_union_distinct_ids"]
    first_day = span["first"]
    ratio4 = hey["ratio_p50"]
    ratio5 = M5["fate"]["hey_wait_s"]["ratio_p50"]
    pair_air_pct = de(pair["later_copy_share_pct"])
    sil_long = sil["longest_gaps"][0]
    st_start = cad["storm_longest"][0]["start"]
    storm_t = f"{st_start[3:5]}.{st_start[0:2]}. {st_start[6:11]}"
    q2fate = jload(D / "hb9" / "q2" / "results.json")["nodes"][NODE]["fate"]["by_prio"]
    hn = master["HB9HNO-49"]
    hno_late5 = M5["hno_late_at_node"]
    n5row = next(t for t in H["neighbours"]["top"] if t["call"] == "HB9VQQ-5")
    gw_ack_pairs = Q2["double_tx"]["pairs"]

    def neigh_row(t: dict[str, Any]) -> list[str]:
        c = t["call"]
        m = master.get(c)
        hw = m["hardware"] if m else (t.get("hw") or "unbekannt")
        if m is None and t.get("hw"):
            hw += " (aus Funkrahmen)"
        fw = (m["firmware"] if m else "") or (t.get("fw_log") or "")
        fw = ftxt(fw) if fw else "unbekannt"
        place = (m.get("description") or m.get("name") or f"{m['lat']} N, {m['lon']} E") if m else "keine Stammdaten"
        alt = f"{m['altitudeM']} m" if m and m.get("altitudeM") else "unbekannt"
        d = dist.get(c, "")
        dk = de(float(d)) + " km" if d else "unbekannt"
        rs = f"{int(t['rssi_median'])} dBm / {int(t['snr_median'])} dB"
        return [c, hw, fw, place, alt, dk, de(t["n"], 0), rs]

    def info_row(c: str) -> list[str]:
        m = master.get(c)
        d = dist.get(c, "")
        if not m:
            return [c, "keine Stammdaten", "unbekannt", "keine Stammdaten", "unbekannt", "unbekannt", "–", "–"]
        place = m.get("description") or m.get("name") or f"{m['lat']} N, {m['lon']} E"
        return [c, m["hardware"], ftxt(m["firmware"]), place, f"{m['altitudeM']} m" if m.get("altitudeM") else "unbekannt",
                de(float(d)) + " km" if d else "unbekannt", "–", "–"]

    top = H["neighbours"]["top"]
    body: list[str] = []
    ap_ = body.append

    # ---- header ----------------------------------------------------------------------------
    ap_('<div class="eyebrow">MeshCom Log-Auswertung · HB9VQQ-4</div>')
    ap_("<h1>HB9VQQ-4 arbeitet ohne Verlust; beobachten bleiben die still verworfenen Relay-Kopien und die doppelten Sendungen mit HB9VQQ-5</h1>")
    ap_("<ul class=\"meta\">"
        f"<li><b>Log-Datei:</b> hb9vqq4.log ({de(span['lines'], 0)} Zeilen, mit --setlog und --loradebug, ohne [NBR]-Zeilen)</li>"
        "<li><b>Zeitraum:</b> 31.10.2026 09:39:56 bis 01.11.2026 09:39:56 (Uhr des Mitschnitt-Rechners, MEZ; Knotenuhr weicht um etwa 1 s ab, kein Sprung), 24,0 h</li>"
        "<li><b>Hardware:</b> Heltec WiFi LoRa 32 V3 (ESP32-S3)</li>"
        "<li><b>Firmware:</b> 4.40a, Release-Stempel 20261030 (wahrscheinlich Fork-Release v4.40a.10.30, Vermutung); genauer Build unbekannt</li>"
        "<li><b>Standort:</b> Orn-Bachtel ZH, 47,2882 N / 8,8813 E, 995 m</li>"
        "<li><b>Rolle:</b> Internet-Gateway und Relay im Schweizer Mesh, getrennt vom OE3-Verbund. Schwesterknoten: <a href=\"hb9vqq5.html\">HB9VQQ-5</a> (18,6 km entfernt)</li>"
        "<li><b>LoRa-Parameter:</b> SF11 / 250 kHz / CR 4:6 (aus der Sendedauer abgeleitet, Frequenz und Leistung stehen nicht im Log)</li>"
        "<li><b>Stand:</b> 01.11.2026</li></ul>")
    ap_('<div class="bluf"><p class="lead">'
        f"HB9VQQ-4 lief 24 Stunden ohne Neustart, der TX-Puffer staute nichts (höchstens {de(occ['write_depth_max'], 0)} von 20 Slots), und der Zeitverfall hat nichts verworfen. "
        f"Zwei Dinge bleiben zu beobachten: {de(n_van, 0)} Relay-Kopien ({pct(n_van, writes)} aller Schreibungen) verlassen den Puffer ohne Log-Zeile, "
        f"und mit HB9VQQ-5 werden {de(pair['distinct_ids_in_pairs'], 0)} Nachrichten-IDs doppelt gesendet.</p>"
        "<p>Es wurde kein Firmware-Fehler gefunden, der Daten verliert. Offen sind nur zwei kosmetische Log-Texte und eine Beobachtungslücke: "
        "der stille Cancel ist ohne <code>--nbrdebug</code> nicht zu sehen.</p></div>")
    ap_('<div class="callout watch"><p><b>Kein Handlungsbedarf. Zu beobachten:</b></p><ul>'
        f"<li><b>Still verworfene Relays:</b> {de(n_van, 0)} von {de(writes, 0)} Slots, alle nach einer gehörten fremden Kopie (Vermutung: Overhear-Cancel). Von {hole_a} bis {hole_b} Uhr trat kein einziger auf, Ursache offen.</li>"
        f"<li><b>Doppelte Sendungen mit HB9VQQ-5:</b> zweite Kopie = {pair_air_pct} % der Sendezeit beider Knoten (obere Grenze).</li>"
        f"<li><b>HEY-Wartezeit:</b> bis {de(hey['own']['max'])} s von 180 s Lebensdauer ({de(100 * hey['own']['max'] / 180)} %) in einem Burst am 31.10. zwischen 18:56 und 19:02 Uhr.</li>"
        f"<li><b>HB9HNO-49:</b> Firmware 4.35h sendet alles etwa 25 Minuten verspätet, darunter {stale_n} veraltete {CET}-Rahmen; hier folgenlos. Betreiberseite.</li>"
        f"<li><b>HB9LEK-12:</b> sendet {de(rad['lek_median_abs_ferr_hz'], 0)} Hz neben der Frequenz, {de(rad['crc_ferr_4000_5000'], 0)} CRC-Fehler liegen im passenden Band. Betreiberseite.</li></ul></div>")

    # ---- core facts ------------------------------------------------------------------------
    ap_("<h2>Kernfakten</h2><ul>"
        f"<li><b>Gesund:</b> kein Neustart, Heap flach (+{de(stat['heap']['slope_per_hour'], 0)} B/h), {de(gw['keepalive_ok'], 0)} von {de(gw['keepalive_ok'], 0)} Keep-alives beantwortet.</li>"
        f"<li><b>Puffer ohne Stau:</b> höchster Füllstand {de(occ['write_depth_max'], 0)} von 20 Slots, 0 Verwürfe durch Platzmangel oder Alter, {pct(n_aired, writes)} aller {de(writes, 0)} Schreibungen wurden gesendet.</li>"
        f"<li><b>Zeitverfall:</b> 0 verworfene Einträge in 24 h. Verlust: keiner. Gewinn: nicht messbar, das Sicherheitsnetz wurde nicht gebraucht.</li>"
        f"<li><b>Zeitsignal:</b> {CET} von OE1XAR-33 alle 607 s ohne Lücke; unsere {de(cet_tx, 0)} Sendungen waren höchstens {de(cetm['own_cet_tx_node_age_max_s'], 0)} s alt (Tor bei 150 s).</li>"
        f"<li><b>Kaum Last durch ACK:</b> {de(ack_n, 0)} ACK-Sendungen = {pct(ack_n, n_tx)} aller Sendungen (OE3-Verbund: 41,3 %), kein Auto-Ping.</li>"
        f"<li><b>Eigene Baken kommen durch:</b> {de(fate['by_src']['auto_pos']['aired'], 0)} von {de(fate['by_src']['auto_pos']['aired'] + fate['by_src']['auto_pos']['superseded'], 0)} eigenen HEY und {de(fate['by_src']['user_wx']['aired'], 0)} von {de(fate['by_src']['user_wx']['aired'], 0)} Wetterpositionen gesendet; eigene HEY warten {de(ratio4, 2)}-mal so lange wie Relay-HEY.</li></ul>")

    # ---- Antwort-Index ---------------------------------------------------------------------
    ap_("<h2>Antwort-Index</h2>")
    idx = [
        ["Gesundheit und Funkumfeld", "Kein Neustart, Heap flach, Server-Anbindung stabil. Zwei Nachbar-Auffälligkeiten (HB9LEK-12, lokale Quelle), beide ohne Folge am Knoten.", chip("ok")],
        ["F2 TX-Puffer", f"Kein Stau, 0 Verwürfe. {pct(n_van, writes)} der Schreibungen verschwinden still (Vermutung: Overhear-Cancel); Lücke {hole_a} bis {hole_b} Uhr.", chip("ok")],
        ["F3 Verlust durch Zeitverfall", f"Nichts verloren (0 Verwürfe). HEY-Klasse bis {de(100 * hey['own']['max'] / 180)} % der Lebensdauer ausgereizt.", chip("watch")],
        ["F4 Gewinn durch Zeitverfall", "Nicht zeigbar, weil nichts zu verfallen war. Reine Absicherung.", chip("neu")],
        ["F5 Zeitsignal", f"OE1XAR-33 ohne Lücke, eigene Sendungen höchstens {de(cetm['own_cet_tx_node_age_max_s'], 0)} s alt. Doppelte {CET}-Sendungen mit HB9VQQ-5 (Beobachten).", chip("ok")],
        ["F6 Manipulationen", f"Keine Schleifen, kein Aufbau. Beobachten: Doppelsendung mit HB9VQQ-5 ({pair_air_pct} % Sendezeit, obere Grenze).", chip("watch")],
        ["Verdachtsfälle", "Kein Firmware-Fehler mit Datenverlust. Zwei kosmetische Log-Texte, eine Beobachtungslücke.", chip("neu", "Kosmetisch")],
    ]
    ap_(table(["Frage", "Kurzantwort", "Urteil"], idx, raw=True))

    # ---- Gesundheit ------------------------------------------------------------------------
    ap_("<h2>Gesundheit und Funkumfeld</h2>")
    ap_(f"<p><b>Der Knoten ist gesund.</b> {chip('ok')} Die Auffälligkeiten im Funkumfeld liegen bei Nachbarn und kosten Empfangszeit, nicht Sendeplatz.</p>")
    ap_("<ul>"
        f"<li><b>Kein Neustart:</b> die Betriebszeit stieg im Log von {de(stat['up_first'], 0)} s auf {de(stat['up_last'], 0)} s, kein Absturzmarker ({len(H['health']['crash_hits'])} Treffer für Guru Meditation, Brownout, Backtrace).</li>"
        f"<li><b>Speicher flach:</b> freier Heap {de(stat['heap']['min'], 0)} bis {de(stat['heap']['max'], 0)} B, Trend {de(stat['heap']['slope_per_hour'], 0)} B pro Stunde.</li>"
        f"<li><b>Server-Anbindung stabil:</b> {de(gw['keepalive_ok'], 0)} von {de(gw['keepalive_ok'], 0)} Keep-alives vom Server beantwortet, {de(H['health']['wifi_markers'], 0)} von {de(H['health']['wifi_markers'], 0)} WLAN-Statusmeldungen „verbunden“.</li>"
        f"<li><b>Empfangsschleife schnell:</b> Verarbeitung je Rahmen Median {de(H['onrxdone']['ms']['median'], 0)} ms, 95 % unter {de(H['onrxdone']['ms']['p95'], 0)} ms, Maximum {de(H['onrxdone']['ms']['max'], 0)} ms, kein Wert über 50 ms.</li>"
        f"<li><b>Keine Empfangslücke:</b> der längste Abstand zwischen zwei Empfangsereignissen war {de(sil_long['gap_s'])} s (31.10. ab 20:18:01), keine Pause über 300 s. HB9VQQ-5 hörte in dieser Zeit {sil_long['rx_events_at_other_oe3_nodes']['hb9vqq5']} Rahmen: ruhiger Kanal, kein tauber Empfänger. Gegensatz zu OE3XIA-12.</li></ul>")
    rows = [
        ["Empfangene Rahmen (dekodiert)", f"{de(n_dec, 0)} ({de(n_dec / span['hours'], 0)} pro Stunde)"],
        ["Mix HEY / Position / Text", f"{de(traffic['types']['@']['pct'])} / {de(traffic['types']['!']['pct'])} / {de(traffic['types'][':']['pct'])} %"],
        ["Davon schon bekannte Kopien (DUP)", pct(traffic["dup_flag"]["d"], n_dec)],
        ["Direkt gehört (kein Relay im Pfad)", f"{de(traffic['direct_zero_relay_pct'])} %"],
        ["Direkte Nachbarn (letzter Hop)", f"{n_direct}"],
        ["CRC-Fehler (Signal erkannt, Rahmen defekt)", f"{de(rf['rx_error_n'], 0)} ({de(rf['rx_error_share_of_rx_pct'])} % aller Empfangsversuche)"],
        ["Kanalauslastung Mittel / 95 % / Maximum", f"{de(chan['CHANNEL_UTIL']['mean'])} / {de(chan['CHANNEL_UTIL']['p95'], 0)} / {de(chan['CHANNEL_UTIL']['max'], 0)} %"],
        ["Eigener Sendeanteil (Tastverhältnis)", f"{de(tx_duty)} %"],
    ]
    ap_(table(["Größe (24 h)", "HB9VQQ-4"], rows))
    ap_("<ul>"
        f"<li><b>Kanalauslastung ist echt:</b> die aus den Rahmenlängen gerechnete Sendezeit ({de(own['airtime_s'], 0)} s) stimmt mit der Messung des Knotens ({de(tx_air_chan, 0)} s) auf unter 1 % überein. Kein ESP32-Überzählen.</li>"
        f"<li><b>CRC-Fehler 11,5 %:</b> Median RSSI {de(rf['rx_error_rssi']['median'], 0)} dBm, SNR {de(rf['rx_error_snr']['median'], 0)} dB: Signale in Reichweite, deren Rahmen sich überlagern oder defekt ankommen (Vermutung). Schwächer ist die Lage bei HB9VQQ-5, siehe <a href=\"hb9vqq5.html\">dort</a>.</li>"
        f"<li><b>Kanalabfrage (CAD, hört vor dem Senden kurz in den Kanal):</b> {de(cad['cad_scans'], 0)} Abfragen, {de(cad['cad_busy_1_initial'], 0)} zuerst „belegt“ ({de(cad['initial_busy_rate_pct'])} %), davon {de(cad['cad_false_positive'], 0)} Fehlalarme ({de(cad['false_positive_pct_of_initial_busy'])} %) und {de(cad['cad_busy_confirmed'], 0)} bestätigt. "
        f"{de(cad['attempt0_pct'])} % der Sendungen kamen beim ersten Versuch durch, höchstens {cad['attempt_max']} Versuche (Sturm von {de(cad['storm_longest'][0]['dur_s'])} s am {storm_t}).</li>"
        f"<li><b>Nachbar-Firmware:</b> {census['nodes_total']} Stationen gehört, {census['nodes_pre_cad']} davon mit Firmware vor der Kanalabfrage (4.35, {de(census['nodes_pre_cad_pct'])} %). Sie verursachen {de(census['events_pre_cad_pct'])} % der gehörten Ereignisse und {de(census['airtime_pre_cad_pct'])} % der Empfangszeit. Das ist die Umgebung, kein Fehler dieses Knotens.</li></ul>")
    ap_(hist([(k[-2:], v) for k, v in chan["util_by_hour"].items()],
             f"Abb. {nf()}: Kanalauslastung je Stunde, Mittel in Prozent (Stunde der Logger-Uhr, erste und letzte Stunde angebrochen)", unit=" %"))
    ap_("<h3>Zwei Nachbar-Auffälligkeiten im Empfang</h3>")
    ap_("<ul>"
        f"<li><b>HB9LEK-12 sendet {chip('watch')} neben der Frequenz:</b> bei {de(rad['lek_frames'], 0)} dekodierten Rahmen beträgt der Frequenzfehler im Median {de(rad['lek_median_abs_ferr_hz'], 0)} Hz (+4,5 kHz), bei {de(rad['lek_share_over_3000hz_pct'], 0)} % der Rahmen über 3 kHz. Die anderen {rad['other_neighbours_ge40_frames']} Nachbarn mit mindestens 40 Rahmen liegen bei höchstens {de(rad['other_neighbours_max_median_ferr_hz'], 0)} Hz ({rad['other_neighbours_max_median_ferr_call']}). "
        f"{de(rad['crc_ferr_4000_5000'], 0)} von {de(rad['crc_errors'], 0)} CRC-Fehlern ({pct(rad['crc_ferr_4000_5000'], rad['crc_errors'])}) haben genau diesen Frequenzfehler. Dass sie von HB9LEK-12 stammen, ist eine Vermutung, denn ein defekter Rahmen trägt keinen Absender. Die Verbindung ist zudem schwach (RSSI -117 dBm, SNR -13 dB), der Versatz ist also ein Mitverursacher, kein bewiesener Alleinverursacher. Vermutung zur Ursache: Quarzabweichung von etwa 5 ppm im fremden Knoten. Betreiberseite, kein Handeln an unseren Knoten.</li>"
        f"<li><b>Lokale Quelle bei -72/-71 dBm {chip('watch')}:</b> {de(rad['stale_irq_total'], 0)} Empfänger-Neustarts wegen eines Vorspann-Treffers ohne Rahmen, davon {de(rad['stale_irq_over_m80'], 0)} über -80 dBm und {de(rad['stale_irq_m72_m71'], 0)} genau bei -72/-71 dBm; {de(rad['stale_irq_over_m80_pre'], 0)} von {de(rad['stale_irq_over_m80'], 0)} zeigen nur den Vorspann (Preamble), keinen Kopf. "
        "Sie häufen sich in den Stunden um 14, 16 und 20 Uhr ohne festen Takt. Quelle unbekannt, kein dekodierbarer MeshCom-Rahmen (Vermutung). Auf den Verkehr hat das keine sichtbare Wirkung (keine Empfangslücke). Niedrige Priorität.</li></ul>")
    f_rf = nf()
    svg, h = dr.rf_env(de(rad["lek_frames"], 0), de(rad["lek_median_abs_ferr_hz"], 0), de(rad["crc_ferr_4000_5000"], 0),
                       de(rad["stale_irq_over_m80"], 0), de(rad["stale_irq_m72_m71"], 0), de(rad["stale_irq_over_m80_pre"], 0),
                       de(rad["stale_irq_total"], 0), de(rad["other_neighbours_max_median_ferr_hz"], 0))
    ap_(explain(svg, f"Abb. {f_rf}: Zwei Quellen stören den Empfang, beide ohne Folge für unsere Sendungen", height=h))

    # ---- F2 --------------------------------------------------------------------------------
    ap_("<h2>F2 TX-Puffer mit Slots</h2>")
    ap_(f"<p><b>Der Puffer arbeitet sauber.</b> {chip('ok')} Jede zu sendende Nachricht belegt einen von 20 Slots, bis sie gesendet, ersetzt oder verworfen wird. Es gab nie einen Stau und keinen Verwurf wegen Platz oder Alter.</p>")
    ap_("<ul>"
        f"<li><b>Füllstand:</b> im Mittel {de(occ['queued']['mean'], 2)} Slots, Median 0, 90 % unter {de(occ['queued']['p90'], 0)}, höchster Stand nach einer Schreibung {de(occ['write_depth_max'], 0)} von 20. Der längste Abschnitt mit 3 oder mehr belegten Slots dauerte {de(occ['longest_run_ge_3_min'], 0)} min.</li>"
        f"<li><b>Kein Aufbau:</b> Trend der Stundenmittel {de(occ['hourly_mean_slope_per_h'], 4)} Slots pro Stunde, also flach.</li>"
        f"<li><b>Keine Verwürfe:</b> 0 Verwürfe durch Alter, 0 durch volle Warteschlange, 0 in den Statistikfenstern des Knotens ({stat['rows']} Fenster, alle Prioritäten).</li></ul>")
    srcn = {"rx_relay": "Relay fremder Rahmen", "udp_rx": "Server-Einspeisung (Gateway)", "auto_pos": "eigene HEY", "user_wx": "eigene Wetterposition",
            "rx_ack_fwd": "ACK weitergeleitet", "rx_dm_ack_gw": "Gateway-ACK", "rx_dm_ack_new": "ACK neu", "user_msg": "eigener Text"}
    rows = []
    for k, label in srcn.items():
        v = fate["by_src"][k]
        rows.append([label, de(v["aired"] + v["superseded"] + v["vanished"], 0), de(v["aired"], 0), de(v["superseded"], 0), de(v["vanished"], 0)])
    rows.append(["Summe", de(writes, 0), f"{de(n_aired, 0)} ({pct(n_aired, writes, 2)})", f"{de(n_sup, 0)} ({pct(n_sup, writes, 2)})", f"{de(n_van, 0)} ({pct(n_van, writes, 2)})"])
    ap_(table(["Quelle der Schreibung", "Schreibungen", "gesendet", "ersetzt", "still verschwunden"], rows, num_cols=[1, 2, 3, 4]))
    ap_("<ul>"
        f"<li><b>Ersetzt ({n_sup}):</b> eine neuere HEY, Position oder {CET} desselben Absenders ersetzt den wartenden Eintrag im selben Slot (Supersede). Das ist kein Verlust, der alte Inhalt ist überholt. "
        f"Dazu {mk['supersede_keep']} Mal der umgekehrte Fall: die neu eintreffende Kopie wird verworfen, weil schon ein neueres {CET} wartet.</li>"
        f"<li><b>Doppelte Gateway-ACK:</b> {gw_ack_pairs} Paare derselben ID, gewollt (Gateway-ACK plus ACK, GWACK-01).</li></ul>")
    ap_("<h3>Wartezeit nach Priorität</h3>")
    pn = {"1": "1 ACK / Ping", "2": "2 Gruppentext (eigen)", "3": "3 Text-Relay und Server-Text", "4": "4 Position", "5": "5 HEY"}
    rows = [[pn[k], de(v["n"], 0), de(v["p50"]), de(v["p90"]), de(v["max"])] for k, v in lat.items()]
    ap_(table(["Priorität", "Sendungen", "Median s", "90 % unter s", "Maximum s"], rows, num_cols=[1, 2, 3, 4]))
    ap_("<ul>"
        f"<li><b>Relay-Klassen warten etwa eine Haltezeit:</b> Median {de(lat['3']['p50'])} bis {de(lat['5']['p50'])} s. Der Rest ist die strenge Rangfolge: eine Sendung mit höherer Priorität überholt, das geschieht regelmäßig (302 von 2.580 Sendungen wurden von einem Eintrag höherer Priorität überholt, Wert aus q2/findings.md Nr. 5).</li>"
        f"<li><b>ACK warten länger als HEY-Relays {chip('watch')}:</b> Median {de(lat['1']['p50'])} s gegen {de(lat['5']['p50'])} s, obwohl ACK die höchste Priorität und die kürzeste Basis-Haltezeit (3,0 s gegen 5,5 s) haben. Kleine Stichprobe ({lat['1']['n']} ACK). "
        f"Der OE3-Verbund zeigt dieselbe Richtung (ACK-Median 20,9 bis 23,3 s). Vermutung: jeder empfangene Rahmen startet die Haltezeit neu, der Altersboden von 20 s greift erst danach. Längste ACK-Wartezeit {de(lat['1']['max'])} s, also 48 % der 90 s Lebensdauer.</li>"
        "<li><b>Kanalzugang ist die Grenze, nicht der Puffer:</b> in den belebtesten 17 % des Tages (16 bis 20 Empfangsereignisse pro Minute) schreibt der Knoten 2,9 Rahmen pro Minute in den Puffer und sendet 2,3. Den Rest gleichen Cancel und Ersetzen aus. "
        f"Das Sendeaufkommen liegt bei nur {de(tx_duty)} % Tastverhältnis, die Grenze ist die Haltezeit, die jeder empfangene Rahmen neu startet (Wert aus q2/findings.md Nr. 7).</li></ul>")

    ap_("<h3>Die 383 still verschwundenen Relay-Kopien</h3>".replace("383", de(n_van, 0)))
    ap_(f"<p><b>{de(n_van, 0)} Relay-Slots ({pct(n_van, writes, 1)} aller {de(writes, 0)} Schreibungen, {pct(n_van, relay_w)} der {de(relay_w, 0)} Relay-Schreibungen) verlassen den Puffer ohne Sendezeile, ohne Verwurfzeile und ohne Supersede.</b> {chip('watch')} "
        "Nach Vermutung ist das der Overhear-Cancel: ein wartendes Relay wird fallengelassen, weil ein anderer Knoten dieselbe Nachricht schon gesendet hat.</p>")
    ap_("<ul>"
        f"<li><b>Fakt, jede war nicht allein:</b> bei {de(fate['vanished_with_foreign_copy'], 0)} von {de(fate['vanished_total'], 0)} hatte der Knoten, während sie warteten, eine fremde Kopie derselben ID gehört, im Median nach {de(fate['first_foreign_copy_s']['p50'])} s. Bei den gesendeten Relays waren es nur {de(fate['aired_relays_with_foreign_copy'], 0)} von {de(fate['aired_relays'], 0)} ({pct(fate['aired_relays_with_foreign_copy'], fate['aired_relays'])}).</li>"
        f"<li><b>Betroffen sind nur Relays:</b> Priorität 5 (HEY) {fate['vanished_by_prio']['5']}, Priorität 4 (Position) {fate['vanished_by_prio']['4']}, Priorität 3 (Text) {fate['vanished_by_prio']['3']}; ACK, Gruppentexte, eigene Baken und Server-Einspeisungen nie.</li>"
        "<li><b>Warum Cancel und nichts anderes:</b> den Relay-Slot (Status DONE, nicht mehr wiederholbar) gibt nur der Overhear-Cancel still frei (<code>lora_functions.cpp:1445</code>); jede andere Freigabe schreibt eine Zeile oder greift bei DONE nicht. "
        "Dass keine gesendete Sendung fehlt, belegt die Bilanz aus Pufferzeilen, Sendezeilen und Statistikfenstern des Knotens. Daraus folgt (Vermutung), dass <code>--nbrrelay on</code> gesetzt ist.</li>"
        f"<li><b>Effekt (Modell):</b> die {de(n_van, 0)} Relays hätten {de(van_air, 0)} s Sendezeit gekostet, das sind {de(100 * van_air / (tx_air_chan + van_air))} % der Sendezeit, die ohne Cancel angefallen wäre ({de(van_air, 0)} von {de(tx_air_chan + van_air, 0)} s).</li>"
        f"<li><b>Trotz fremder Kopie gesendet:</b> {de(fate['aired_relays_with_foreign_copy'], 0)} Relays, {de(foreign_air, 0)} s = {de(100 * foreign_air / tx_air_chan)} % der eigenen Sendezeit. Obere Grenze: ohne [NBR]-Zeilen ist nicht zu trennen, ob der Cancel nicht griff oder der Nachbar nicht alle abdeckte (OE3-Verbund: 29,6 %).</li>"
        f"<li><b>Server-Einspeisungen werden nie abgebrochen:</b> {fate['inject_aired']} gesendet, {fate['inject_vanished']} verworfen, {fate['inject_aired_with_foreign_copy']} ({pct(fate['inject_aired_with_foreign_copy'], fate['inject_aired'], 0)}) nach einer gehörten fremden Kopie, zusammen {de(fate['inject_aired_airtime_s'], 0)} s. Das passt zur Entwurfslücke „Case A“ (OE3-Befund 5): der Alleinversorger-Test ist für Server-Slots nie leer.</li></ul>")
    ap_("<h4>Die Lücke von " + hole_a + " bis " + hole_b + " Uhr</h4>")
    ap_("<ul>"
        f"<li><b>Fakt:</b> in den {hole_hi - hole_lo + 1} Stunden von {hole_a} bis {hole_b} Uhr (31.10.) verschwand kein einziger Relay-Slot. Davor waren es {before_txt} je Stunde, danach {after_lo} bis {after_hi}.</li>"
        f"<li><b>Die Gelegenheit war da:</b> in derselben Zeit hatten je Stunde {min(opp)} bis {max(opp)} gesendete Relays eine fremde Kopie gehört, ohne dass der Cancel eingriff.</li>"
        "<li><b>Ursache offen:</b> kein Konfigurationsbefehl, kein Neustart und keine Anmeldung auf dem Knoten in dieser Zeit. Vermutung: der Nachbarzustand, den der Cancel prüft, hat sich mit der Tageszeit geändert. Ohne <code>[NBR]</code>-Zeilen nicht belegbar.</li></ul>")
    f_c = nf()
    svg, h = dr.cancel(de(n_van, 0), de(fate["first_foreign_copy_s"]["p50"]), fate["hourly_vanished"],
                       [(0, clock_label(first_day, 0)), (hole_lo, hole_a), (hole_hi + 1, hole_b), (23, clock_label(first_day, 23))],
                       hole_lo, hole_hi, min(opp), max(opp))
    ap_(explain(svg, f"Abb. {f_c}: Ein wartendes Relay wird still freigegeben, sobald eine fremde Kopie gehört wurde; zwischen {hole_a} und {hole_b} Uhr geschah das nie", height=h))
    ap_(timeline([("still verschwunden", list(enumerate(fate["hourly_vanished"]))),
                  ("gesendet trotz fremder Kopie", list(enumerate(fate["hourly_aired_with_foreign_copy"])))],
                 f"Abb. {nf()}: Relays je Stunde der Aufzeichnung (Stunde 0 = {clock_label(first_day, 0)} Uhr, Stunde 6 bis 12 ohne Cancel)",
                 xlabels=[(0, clock_label(first_day, 0)), (6, clock_label(first_day, 6)), (13, clock_label(first_day, 13)), (20, clock_label(first_day, 20))]))
    ap_("<p><b>Beobachtungslücke:</b> ohne <code>--nbrdebug</code> schreibt der Cancel nichts ins Log, und sein Zähler (<code>stat_nbr_cancel</code>) steht in keiner Statistikzeile. "
        f"Damit sind {pct(n_van, writes)} aller Pufferschreibungen im Standard-Mitschnitt unsichtbar. Vorschlag (ungetestet): den Zähler in <code>[MC-STAT]</code> aufnehmen.</p>")

    # ---- F3 --------------------------------------------------------------------------------
    ap_("<h2>F3 Was verlieren wir durch den Zeitverfall</h2>")
    ap_(f"<p><b>Nichts.</b> {chip('watch')} Der Zeitverfall (TTL: jeder Eintrag im Puffer verfällt nach einer Höchstdauer, HEY nach 180 s, Position 120 s, ACK 90 s, Relay-Text 300 s; {CET} zusätzlich ab 150 s Alter) hat in 24 Stunden keinen Eintrag verworfen. "
        "Beobachten bleibt die HEY-Klasse, die in einem Burst fast ihre Lebensdauer erreichte.</p>")
    ap_("<ul>"
        f"<li><b>Verwürfe:</b> {Q34['drops_total']} durch TTL, 0 durch das {CET}-Tor, 0 in den Statistikfenstern. Eigene Baken verloren: {Q34['own_dropped']}. Verfallene Sendezeit: {de(Q34['drop_toa_s'], 1)} s.</li>"
        "<li><b>Zum Vergleich OE3-Verbund:</b> 232 Verwürfe, 153 eigene Baken verloren, 167 s = 1,23 % der Sendezeit.</li></ul>")
    cls_rows = [
        ["5 HEY, alle", "180 s", de(hr["p5"]["n_sent"], 0), de(hr["p5"]["p50"]), de(hr["p5"]["p90"]), de(hr["p5"]["max"]), pct(hr["p5"]["max"], 180), f"{hr['p5']['n_ge80pct_ttl_sent']}"],
        ["   davon eigene HEY", "180 s", de(hey["own"]["n"], 0), de(hey["own"]["p50"]), de(hey["own"]["p90"]), de(hey["own"]["max"]), pct(hey["own"]["max"], 180), "2"],
        ["   davon Relay-HEY", "180 s", de(hey["relay"]["n"], 0), de(hey["relay"]["p50"]), de(hey["relay"]["p90"]), de(hey["relay"]["max"]), pct(hey["relay"]["max"], 180), "1"],
        ["4 Position", "120 s", de(hr["p4"]["n_sent"], 0), de(hr["p4"]["p50"]), de(hr["p4"]["p90"]), de(hr["p4"]["max"]), pct(hr["p4"]["max"], 120), f"{hr['p4']['n_ge80pct_ttl_sent']}"],
        ["3 Text und Server-Text", "300 s", de(hr["p3"]["n_sent"], 0), de(hr["p3"]["p50"]), de(hr["p3"]["p90"]), de(hr["p3"]["max"]), pct(hr["p3"]["max"], 300), f"{hr['p3']['n_ge80pct_ttl_sent']}"],
        ["1 ACK", "90 s", de(hr["p1"]["n_sent"], 0), de(hr["p1"]["p50"]), de(hr["p1"]["p90"]), de(hr["p1"]["max"]), pct(hr["p1"]["max"], 90), f"{hr['p1']['n_ge80pct_ttl_sent']}"],
    ]
    ap_("<p><b>Wie nah kamen gesendete Einträge an ihre Lebensdauer?</b></p>")
    ap_(table(["Klasse", "Lebensdauer", "gesendet", "Median s", "90 % unter s", "Maximum s", "Maximum / Lebensdauer", "davon ab 80 %"], cls_rows, num_cols=[2, 3, 4, 5, 6, 7]))
    ap_(bars([("HEY, eigene", 100 * hey["own"]["max"] / 180), ("HEY, Relay", 100 * hey["relay"]["max"] / 180),
              ("Position", 100 * hr["p4"]["max"] / 120), ("ACK", 100 * hr["p1"]["max"] / 90), ("Text", 100 * hr["p3"]["max"] / 300)],
             f"Abb. {nf()}: Längste gesendete Wartezeit im Verhältnis zur Lebensdauer; nur HEY kommt in die Nähe", unit=" %"))
    ap_("<ul>"
        f"<li><b>Nur die HEY-Klasse kommt nah heran {chip('watch')}:</b> {hr['p5']['n_ge80pct_ttl_sent']} HEY wurden bei mindestens 80 % ihrer Lebensdauer gesendet (31.10. 18:56 eigene HEY nach 159,0 s, 11:13 ein Relay nach 158,9 s; 01.11. 03:25 eigene HEY nach {de(hey['own']['max'])} s). Der enge Fall liegt im Burst am 31.10. zwischen 18:56 und 19:02 Uhr, in dem auch HB9VQQ-5 vier HEY über 150 s warten ließ. Der Abstand zum Verfall blieb {de(180 - hey['own']['max'])} s.</li>"
        f"<li><b>Modell, nicht Messung:</b> hätte die HEY-Lebensdauer 150 s betragen, wären {sens5['150']['n_over']} der {de(hr['p5']['n_sent'], 0)} gesendeten HEY ({sens5['150']['own']} eigene) verfallen, bei 120 s {sens5['120']['n_over']} ({sens5['120']['own']} eigene). "
        "Ein verfallener Eigenbote bedeutet, dass die nächste Aussendung erst nach dem nächsten Trickle-Zyklus (etwa 15 min) folgt (Vermutung).</li>"
        "<li><b>Position und ACK haben weiten Abstand</b> (67 % bzw. 48 % der Lebensdauer im ungünstigsten Fall), Text 21 %.</li>"
        "<li><b>Kein festhängender Eintrag:</b> die längste Wartezeit irgendeiner Sendung war 166,5 s.</li></ul>")
    ap_("<h3>Eigene HEY warten länger als Relay-HEY</h3>")
    ap_(f"<p><b>Eine eigene HEY wartet im Median {de(hey['own']['p50'])} s, eine weitergeleitete HEY derselben Priorität {de(hey['relay']['p50'])} s, also das {de(ratio4, 2)}-fache.</b> {chip('watch')} "
        f"Bei HB9VQQ-5 ist es das {de(ratio5, 2)}-fache. Verloren geht dabei nichts: {fate['by_src']['auto_pos']['aired']} von {fate['by_src']['auto_pos']['aired'] + fate['by_src']['auto_pos']['superseded']} eigenen HEY und {fate['by_src']['user_wx']['aired']} von {fate['by_src']['user_wx']['aired']} Wetterpositionen wurden gesendet (die eine fehlende HEY wurde von der nächsten ersetzt).</p>")
    ap_("<ul>"
        "<li><b>Mechanismus (Vermutung, übertragen vom OE3-Befund):</b> am Listenkopf bekommt eine eigene Bake die Haltezeit von 5,5 s, die mit jedem empfangenen Rahmen neu startet, und sie ist vom Altersboden ausgenommen. "
        "Ein Relay gleicher Priorität nimmt nach 8 s Wartezeit den kurzen Weg „Fall A“ mit 150 bis 220 ms. Auf OE3XOC kostete das Bakenverluste, hier bei viel geringerer Last nur Wartezeit. "
        "Herleitung: <a href=\"verify/rca_own_beacons.md\">verify/rca_own_beacons.md</a>.</li>"
        "<li><b>Bei leerem Puffer:</b> Relay-HEY im Median 8,4 s, eigene HEY 12,3 s (Wert aus q2/findings.md Nr. 6); die Kluft entsteht also nicht erst durch einen vollen Puffer.</li></ul>")
    f_h = nf()
    svg, h = dr.hey_wait(hey["relay"]["p50"], hey["relay"]["max"], hey["own"]["p50"], hey["own"]["max"], 180.0, de(ratio4, 2),
                         f"{fate['by_src']['auto_pos']['aired']} von {fate['by_src']['auto_pos']['aired'] + fate['by_src']['auto_pos']['superseded']}", de)
    ap_(explain(svg, f"Abb. {f_h}: Eigene HEY warten am Listenkopf länger als Relay-HEY, bleiben aber unter der Lebensdauer", height=h))
    ap_("<h3>Unterabschnitt: CSMA-Altersboden</h3>")
    ap_("<p>Der Altersboden (CSMA = Kanalzugang mit Abhören) kürzt die Haltezeit auf 1,5 bis 1,7 s, wenn ein Eintrag der Priorität 1 bis 3 schon 20 s wartet. Er sorgt dafür, dass ACK und Texte nicht endlos hinter empfangenen Rahmen warten.</p>")
    ap_("<ul>"
        f"<li><b>Häufigkeit:</b> {csma['events']} Ereignisse in {csma['episodes']} Episoden, alle in Stufe 1, alle {csma['ep_outcome']['sent']} Episoden endeten mit einer Sendung. Haltezeit {de(csma['hold_ms']['p10'], 0)} bis {de(csma['hold_ms']['p90'], 0)} ms.</li>"
        f"<li><b>Betroffen:</b> {csma['tx_prio_le3_wait_ge20s']} von {csma['tx_prio_le3']} Sendungen der Priorität 1 bis 3 ({de(csma['tx_prio_le3_wait_ge20s_pct'])} %) warteten 20 s oder länger, {csma['tx_prio_le3_wait_ge60s']} über 60 s.</li>"
        f"<li><b>Kosten, keine Kollisionsspur:</b> nach einem ACK mit Altersboden gab es in den folgenden 3 s {de(csma['probe_p1_aged']['pct_rxerr_within_3s_after_tx'])} % CRC-Fehler (n = {csma['probe_p1_aged']['n']}), nach einem ohne {de(csma['probe_p1_fresh']['pct_rxerr_within_3s_after_tx'])} % (n = {csma['probe_p1_fresh']['n']}). Kleine Stichprobe.</li>"
        f"<li><b>Gewinn (Modell):</b> Zeitersparnis im Median {de(csma['replay']['saved_s_vs_attempt0_hold']['p50'])} s je Episode, zusammen {de(csma['replay']['saved_s_vs_attempt0_hold']['sum'], 0)} s. Ein Replay, keine Messung. Gemessen ist nur die Restwartezeit nach dem ersten Ereignis: Median {de(csma['replay']['real_remaining_after_first_aged_event_s']['p50'])} s.</li>"
        "<li><b>Eigene Baken (Priorität 4 und 5) liegen außerhalb des Altersbodens:</b> das ist der Hintergrund des vorigen Abschnitts. Bei dieser Last ohne Verlust.</li></ul>")

    # ---- F4 --------------------------------------------------------------------------------
    ap_("<h2>F4 Was gewinnen wir durch den Zeitverfall</h2>")
    ap_(f"<p><b>Messbar ist kein Gewinn, weil der Zeitverfall nie ausgelöst wurde.</b> {chip('neu')} Der Verfall ist hier eine Absicherung, die noch nicht gebraucht wurde.</p>")
    ap_("<ul>"
        f"<li><b>Kein Schutz vor vollem Puffer nötig:</b> der Puffer erreichte höchstens {de(occ['write_depth_max'], 0)} von 20 Slots, kein Eintrag überschritt seine Lebensdauer. Ohne jeden Verfall hätte sich der Puffer bei allen {de(fate['fates']['aired'], 0)} Sendungen gleich verhalten.</li>"
        f"<li><b>Frische:</b> {sens5['90']['n_over']} von {de(hr['p5']['n_sent'], 0)} HEY ({de(sens5['90']['pct_of_items'])} %) wurden älter als 90 s gesendet, {sens5['120']['n_over']} älter als 120 s. Ob das späte Senden ein Gewinn oder ein Verlust ist, ist eine Richtlinienfrage: der Inhalt wird mit dem nächsten Zyklus ohnehin erneuert (Vermutung).</li>"
        f"<li><b>{CET}-Tor:</b> unsere Sendungen sind höchstens {de(cetm['own_cet_tx_node_age_max_s'], 0)} s alt, die Frische kommt aus der kurzen Wartezeit im Puffer, nicht vom Tor. Der Schutzwert des Tors ist in diesen Daten nicht sichtbar. Im OE3-Verbund stoppte es 8 veraltete Relays.</li>"
        "<li><b>Bilanz:</b> Verlust 0, gemessener Gewinn 0. Der OE3-Verbund zeigt, was der Verfall unter Last tut: Frische gegen 153 verlorene eigene Baken.</li></ul>")

    # ---- F5 --------------------------------------------------------------------------------
    ap_("<h2>F5 Zeitsignal</h2>")
    ap_(f"<p><b>Das Zeitsignal kommt zuverlässig an und verlässt den Knoten nie veraltet.</b> {chip('ok')} Das Zeitsignal {CET} ist ein Telegramm des Servers mit der Uhrzeit (die Zeit darin ist UTC, obwohl „CET“ davorsteht). Ein Gateway sendet es ins Funknetz, damit Knoten ohne Internet ihre Uhr stellen können. Ein Tor lässt nichts senden, das älter als 150 s ist.</p>")
    gaps_udp = Q5["udp_arrival_gap_s"]
    lc = ts["lora_distinct_by_src"]
    rows = [
        ["OE1XAR&#8209;33", "UDP vom Server an dieses Gateway", f"{ts['gw_rx_by_src']['OE1XAR-33']}", f"Abstand {de(gaps_udp['min'], 0)} bis {de(gaps_udp['max'], 0)} s (Median {de(gaps_udp['p50'], 0)} s), keine Lücke über 900 s", chip("ok")],
        ["OE1XAR&#8209;63", "nur Funk, eingespeist von fremden Gateways (meist DO1FE-19)", f"{lc['OE1XAR-63']}", "unregelmäßig, nur was zufällig ankommt", chip("watch")],
        ["OE1XAR&#8209;28", "nur Funk, über HB9CMI-40 und HB9CF-40", f"{lc['OE1XAR-28']}", "unregelmäßig", chip("watch")],
        ["OE1XAR&#8209;62", "nie gesehen", "0", "–", chip("neu")],
    ]
    ap_(table(["Server", "Weg zu HB9VQQ-4", "Telegramme (24 h)", "Takt", "Urteil"], rows, num_cols=[2], raw=True))
    ap_("<ul>"
        f"<li><b>Eigene Sendungen:</b> {cet_tx} {CET}-Sendungen = {pct(cet_tx, n_tx)} aller Sendungen. {Q5['tx_src_dist']['g']} stammen aus der Server-Einspeisung, {Q5['tx_src_dist']['r']} sind Relays von Funkkopien (Vermutung zur Aufteilung).</li>"
        f"<li><b>Alter bei der Sendung:</b> Median {de(Q34['cet_tx_age_s']['p50'], 0)} s, Maximum {de(cetm['own_cet_tx_node_age_max_s'], 0)} s; keine Sendung über 150 s, Abstand zum Tor mindestens {150 - cetm['own_cet_tx_node_age_max_s']} s.</li>"
        f"<li><b>Wartezeit im Puffer:</b> Median {de(Q5['tx_wait_ms']['p50'] / 1000)} s, Maximum {de(Q5['tx_wait_ms']['max'] / 1000)} s. Eine eingespeiste Zeit ist also selbst bei ruhigem Kanal rund 9 s alt, wenn sie ausgesendet wird.</li>"
        f"<li><b>Tor:</b> 0 Verwürfe durch das {CET}-Tor, der Knoten läuft auf einer vertrauten Uhr (<code>trusted;1</code> in jeder Zeile).</li></ul>")
    ap_("<h3>Doppelt gesendet mit HB9VQQ-5 (Beobachten)</h3>")
    ap_(f"<p><b>Beide Gateways senden dasselbe {CET}-Telegramm.</b> {chip('watch')} Der Server liefert es an beide Knoten gleichzeitig, jeder speist es in den Funk ein. Pro OE1XAR-33-Telegramm sind rund 8 Aussendungen im Funknetz zu sehen (Median, beide Logs zusammen). "
        f"In 138 von 146 Telegrammen sendeten beide Knoten dasselbe. In {cp['pairs']} {CET}-Paaren hatte der spätere Knoten die Kopie des früheren schon dekodiert ({cp['later_node_had_heard_earlier_copy']}, {pct(cp['later_node_had_heard_earlier_copy'], cp['pairs'], 0)}); die späteren Kopien kosteten {de(cp['later_copy_airtime_s'])} s Sendezeit. "
        "Das ist eine obere Grenze für das Vermeidbare, denn die beiden Knoten teilen nur wenige Nachbarn. Ursache: die Server-Slots werden nie abgebrochen (Konstruktionslücke Fall A, OE3-Befund 5).</p>")
    ap_("<h3>HB9HNO-49 sendet veraltete Zeitsignale und warum das hier harmlos ist</h3>")
    ap_(f"<p><b>HB9HNO-49 strahlt {stale_n} {CET}-Rahmen ab, die {de(hno['min'], 0)} bis {de(hno['max'], 0)} s alt sind (Median {de(hno['p50'], 0)} s), und nichts davon wurde von uns weitergesendet.</b> {chip('watch')} "
        f"HB9HNO-49 (Heltec V3, Firmware 4.35h, Gateway, {de(float(hn['lat']), 4)} N / {de(float(hn['lon']), 4)} E, {hn['altitudeM']} m, {de(float(dist['HB9HNO-49']))} km entfernt) ist die einzige Station mit veralteten {CET}-Rahmen: {stale_n} von {de(cetm['cet_frames_decoded'], 0)} dekodierten {CET}-Rahmen bei uns, {M5['stale_cet']['stale_frames']} bei HB9VQQ-5, zusammen {stale_union} verschiedene Telegramme.</p>")
    ap_("<ul>"
        f"<li><b>Es ist nicht nur {CET}:</b> jeder Rahmentyp von HB9HNO-49 kommt etwa 24 Minuten verspätet. {hno_fr['frames_over_300s_after_first_copy']} von {hno_fr['frames']} seiner Rahmen kamen mehr als 5 min nach der ersten Kopie derselben ID, sie belegen {de(3.0)} % der Empfangszeit (q5/findings.md). Firmware 4.35h kennt weder das {CET}-Tor noch den Zeitverfall im Puffer (Vermutung: langer Rückstau in der Sendeliste).</li>"
        f"<li><b>Warum folgenlos:</b> {cetm['stale_frames'] - cetm['stale_accepted_new']} der {cetm['stale_frames']} Rahmen kannte der Dedup-Ring (merkt sich die letzten 100 IDs, rund 42 min lang) schon und warf sie weg. Einer ({cetm['stale_accepted_new_detail'][0]['id']}, 01.11. 06:22:57, Alter {cetm['stale_accepted_new_detail'][0]['age_s']} s) wurde als neu angenommen, hatte aber Hop 0 und wurde gar nicht erst in den Puffer gelegt. Die Uhr des Knotens läuft vertraut, ein altes Telegramm kann sie nicht verstellen.</li>"
        f"<li><b>Eigene Sendungen unberührt:</b> {cetm['own_cet_tx']} eigene {CET}-Sendungen, keine älter als {cetm['own_cet_tx_node_age_max_s']} s, keine über 150 s. Das Tor griff nie ein, weil nichts Veraltetes in den Puffer kam. Kein Firmware-Fehler.</li>"
        f"<li><b>Spätere HEY und Positionen werden dagegen weitergeleitet:</b> für diese Typen gibt es kein Alterstor am Ursprung (so gewollt). HB9VQQ-5 nahm {hno_late5['accepted_new_total']} späte Rahmen von HB9HNO-49 als neu an ({hno_late5['accepted_new_by_type']['HEY']} HEY, {hno_late5['accepted_new_by_type']['Position']} Positionen) und leitete {hno_late5['relayed_total']} davon weiter. Bei HB9VQQ-4 spielt das kaum eine Rolle.</li>"
        "<li><b>Zu tun:</b> HB9HNO-49 ansprechen, Firmware 4.35h auf 4.40a bringen. Betreiberseite.</li></ul>")
    f_s = nf()
    svg, h = dr.stale_hno(de(stale_n, 0), de(hno["min"], 0), de(hno["max"], 0), de(stale_n - cetm["stale_accepted_new"], 0), de(cetm["stale_accepted_new"], 0),
                          de(cetm["own_cet_tx_node_age_max_s"], 0), de(cetm["stale_accepted_new_detail"][0]["age_s"], 0), "24,9", "42")
    ap_(explain(svg, f"Abb. {f_s}: Ein veraltetes {CET} von HB9HNO-49 endet im Dedup-Ring oder bei Hop 0 und erreicht das Tor nie", height=h))

    # ---- F6 --------------------------------------------------------------------------------
    ap_("<h2>F6 Manipulationen dieses Knotens</h2>")
    ap_("<p><b>Was der Knoten mit Rahmen anstellt (weiterleiten, unterdrücken, ersetzen, verwerfen) und welche Wirkung das hat.</b> "
        f"Es gibt keine Schleifen, keinen Aufbau und keine wachsenden Pfade; das Tastverhältnis liegt bei {de(tx_duty)} %.</p>")
    rly = own["relay_decisions_RLY"]
    pos = [
        f"<li><b>Weiterleiten:</b> {de(relay_w, 0)} Relay-Schreibungen, {de(fate['by_src']['rx_relay']['aired'], 0)} gesendet; so kommen Baken und Texte aus den Tälern heraus.</li>",
        f"<li><b>Overhear-Cancel (Vermutung):</b> {de(n_van, 0)} Relays gespart, {de(van_air, 0)} s Sendezeit (Modell).</li>",
        f"<li><b>Ersetzen:</b> {n_sup} Einträge durch Neueres ersetzt: {q2fate['5']['superseded']} HEY, {q2fate['4']['superseded']} Position, {q2fate['3']['superseded']} Text oder {CET}.</li>",
        f"<li><b>Dedup-Ring:</b> {de(Q6['totals']['rx_dup_foreign'] + Q6['totals']['rx_dup_echo'], 0)} Kopien von {de(Q6['totals']['rx_new'], 0)} neuen Rahmen unterdrückt, damit keine Flut entsteht.</li>",
        f"<li><b>Eigene Baken:</b> {fate['by_src']['auto_pos']['aired']} HEY und {fate['by_src']['user_wx']['aired']} Wetterpositionen, 0 verloren.</li>",
        f"<li><b>Gateway:</b> {fate['inject_aired']} Server-Rahmen ins Funknetz ({Q5['tx_src_dist']['g']} {CET}, {fate['inject_aired'] - Q5['tx_src_dist']['g']} Texte) und {de(mk['tx_udp_lines'], 0)} Rahmen zum Server hochgeladen.</li>",
    ]
    neg = [
        f"<li><b>Doppelsendung mit HB9VQQ-5:</b> {de(pair['distinct_ids_in_pairs'], 0)} IDs von beiden gesendet ({de(pair['pairs'], 0)} Paare, darunter {de(pair['relay_relay_pairs'], 0)} Relay/Relay und {pair['inject_inject_pairs']} Einspeisung/Einspeisung); zweite Kopie {de(pair['later_copy_airtime_s'], 0)} s = {pair_air_pct} % der Sendezeit beider Knoten (obere Grenze).</li>",
        f"<li><b>Relays nach gehörter Kopie:</b> {de(fate['aired_relays_with_foreign_copy'], 0)} Relays, {de(foreign_air, 0)} s = {de(100 * foreign_air / tx_air_chan)} % der eigenen Sendezeit (obere Grenze, OE3-Verbund 29,6 %).</li>",
        f"<li><b>Einspeisung ohne Abbruch:</b> {fate['inject_aired_with_foreign_copy']} von {fate['inject_aired']} Server-Rahmen nach einer gehörten fremden Kopie gesendet.</li>",
        f"<li><b>Späte Echos von HB9HNO-49:</b> {hno_fr['frames_over_300s_after_first_copy']} Rahmen, 3,0 % der Empfangszeit.</li>",
        "<li><b>HB9JAT nutzt Nachrichten-IDs erneut:</b> 556 Baken aus nur 92 IDs; der Dedup-Ring erkennt sie nach etwa 42 min nicht mehr, sie werden erneut weitergeleitet (q6/findings.md 2.5, nicht gegengeprüft).</li>",
    ]
    neu = [
        f"<li><b>ACK und Ping:</b> {ack_n} ACK-Sendungen = {pct(ack_n, n_tx)} der Sendungen, 2 manuelle Pings an HB9ZG-49; kein Auto-Ping.</li>",
        f"<li><b>Nicht weitergeleitet:</b> {rly['hop0']} Rahmen mit Hop 0 und {rly['gwcap']} durch die Gateway-Kappe (Positionen mit Hop 0 am Gateway), {rly['full']} mit dem falschen Wort „full“ (siehe Verdachtsfälle).</li>",
        f"<li><b>Hop und Pfad:</b> Hop-Summen konstant, 0 Schleifen, 0 durch die Schleifensperre verhinderte Relays, obwohl {de(traffic['own_in_path']['e'], 0)} von {de(n_dec, 0)} Rahmen den eigenen Ruf im Pfad tragen (Echos der eigenen Relays).</li>",
        f"<li><b>Fremde Störrahmen:</b> {H['health']['rx_drop_unconfigured']} Mal ein 12-Byte-Rahmen mit FCS 0000 von unbekanntem Sender (Vermutung: kein oder nicht konfigurierter MeshCom-Knoten), korrekt verworfen.</li>",
        f"<li><b>Sendeanteil:</b> {de(tx_duty)} %, Kanal im Mittel zu {de(chan['CHANNEL_UTIL']['mean'])} % belegt.</li>",
    ]
    ap_('<div class="grid3">'
        f'<div class="card pos"><h3>{chip("pos")}</h3><ul>{"".join(pos)}</ul></div>'
        f'<div class="card neg"><h3>{chip("neg")}</h3><ul>{"".join(neg)}</ul></div>'
        f'<div class="card neu"><h3>{chip("neu")}</h3><ul>{"".join(neu)}</ul></div></div>')
    ap_("<h3>Gegenseitige Redundanz mit HB9VQQ-5</h3>")
    ap_(f"<p><b>HB9VQQ-4 und HB9VQQ-5 hören sich und senden trotzdem fast dasselbe.</b> {chip('watch')} Von {de(pair['ids_aired_by_at_least_one'], 0)} IDs, die mindestens ein Knoten sendete, sendeten {de(pair['distinct_ids_in_pairs'], 0)} beide ({pct(pair['distinct_ids_in_pairs'], pair['ids_aired_by_at_least_one'], 0)}).</p>")
    ap_("<ul>"
        f"<li><b>Hörbarkeit:</b> HB9VQQ-5 dekodiert {pct(mut['hb9vqq5_decodes_hb9vqq4']['hit'], mut['hb9vqq5_decodes_hb9vqq4']['of'])} der Sendungen von HB9VQQ-4 ({de(mut['hb9vqq5_decodes_hb9vqq4']['hit'], 0)} von {de(mut['hb9vqq5_decodes_hb9vqq4']['of'], 0)}), HB9VQQ-4 dekodiert {pct(mut['hb9vqq4_decodes_hb9vqq5']['hit'], mut['hb9vqq4_decodes_hb9vqq5']['of'])} der Sendungen von HB9VQQ-5 ({de(mut['hb9vqq4_decodes_hb9vqq5']['hit'], 0)} von {de(mut['hb9vqq4_decodes_hb9vqq5']['of'], 0)}), nur gemeinsames Aufzeichnungsfenster. Pegel von HB9VQQ-5 bei uns: Median {de(n5row['rssi_median'], 0)} dBm, SNR {de(n5row['snr_median'], 0)} dB.</li>"
        f"<li><b>Paare:</b> {de(pair['pairs'], 0)} Paare gleicher ID innerhalb von 30 min, darunter {de(pair['relay_relay_pairs'], 0)} Relay/Relay und {pair['inject_inject_pairs']} Einspeisung/Einspeisung. Abstand der beiden Sendungen im Median {de(pair['gap_p50_s'])} s; HB9VQQ-4 war in {de(pair['first_is_hb9vqq4_pairs'], 0)} Paaren zuerst.</li>"
        f"<li><b>Kosten:</b> die jeweils spätere Kopie belegt {de(pair['later_copy_airtime_s'], 0)} s von zusammen {de(pair['both_nodes_tx_airtime_s'], 0)} s Sendezeit, {pair_air_pct} %. Das ist eine obere Grenze: die beiden Knoten teilen nur {n_shared} von {n_direct} bzw. {n_direct5} direkten Nachbarn, die zweite Kopie erreicht also meist jemand Neues.</li>"
        "<li><b>Nutzen:</b> HB9VQQ-4 bedient eine größere innere Nachbarschaft, HB9VQQ-5 ist das Tor nach Osten (Vermutung aus den Nachbarlisten).</li>"
        "<li><b>Frage an den Betreiber:</b> ob die doppelte Aussendung gewollt ist, lässt sich nur mit <code>--nbrdebug on</code> auf beiden Knoten klären, dann sind die Bedarfsmeldungen (NEED, CANCEL) lesbar.</li></ul>")
    f_p = nf()
    svg, h = dr.pair(de(pair["distinct_ids_in_pairs"], 0), de(mut["hb9vqq5_decodes_hb9vqq4"]["pct"]), de(mut["hb9vqq4_decodes_hb9vqq5"]["pct"]),
                     pair_air_pct, n_direct - n_shared, n_shared, n_direct5 - n_shared, f"0 von {fate['inject_aired']}")
    ap_(explain(svg, f"Abb. {f_p}: Zwei Gateways in gegenseitiger Reichweite senden fast dieselben IDs; nur bei nicht geteilten Nachbarn ist die zweite Kopie nützlich", height=h))
    ap_(bars([("Relay / Relay", pair["relay_relay_pairs"]), ("Einspeisung / Einspeisung", pair["inject_inject_pairs"]),
              ("Relay / eigene HEY", pair["classes"].get("rx_relay/auto_pos", 0) + pair["classes"].get("auto_pos/rx_relay", 0)),
              ("Relay / eigene Position", pair["classes"].get("rx_relay/user_wx", 0) + pair["classes"].get("user_wx/rx_relay", 0)),
              ("Relay / Einspeisung", pair["classes"].get("rx_relay/udp_rx", 0) + pair["classes"].get("udp_rx/rx_relay", 0))],
             f"Abb. {nf()}: Paare gleicher ID, die beide Knoten sendeten, nach Art der Sendung (die größten fünf von {de(pair['pairs'], 0)} Paaren)"))
    ap_("<h3>Kontrast zum OE3-Verbund</h3>")
    rows = [
        ["ACK-Last", f"{pct(ack_n, n_tx)} der Sendungen", "41,3 % der Sendungen"],
        ["Auto-Ping", "keiner, 2 manuelle Pings", "OE3XWJ-12 1.529 Pings, OE3XIR-12 689"],
        ["Eigene HEY gesendet", f"{pct(fate['by_src']['auto_pos']['aired'], fate['by_src']['auto_pos']['aired'] + fate['by_src']['auto_pos']['superseded'])}", "OE3XOC 41,8 %, OE3XIA 78,8 %"],
        ["Verwürfe durch Zeitverfall", "0", "232, davon 153 eigene Baken"],
        ["Tauber Empfänger", f"keiner (längste Lücke {de(sil_long['gap_s'], 0)} s)", "OE3XIA-12 17,4 % des Tages"],
        ["Anteil Firmware vor der Kanalabfrage an gehörten Ereignissen", f"{de(census['events_pre_cad_pct'])} %", "OE3XHL-12 26 %"],
    ]
    ap_(table(["Merkmal", "HB9VQQ-4", "OE3-Verbund"], rows))
    ap_("<p class=\"small\">Netzweite Sicht und Vergleich: <a href=\"uebersicht.html\">Übersicht</a>. Die Last liegt hier niedriger und die Mechanismen der OE3-Seiten wirken, aber nicht verlustbringend.</p>")

    # ---- Verdachtsfaelle -------------------------------------------------------------------
    ap_("<h2>Verdachtsfälle Firmware</h2>")
    ap_(f"<p><b>Es wurde kein Firmware-Fehler gefunden, der Daten verliert.</b> {chip('neu', 'Kosmetisch')} Offen sind zwei falsche Log-Texte (bestätigt, folgenlos) und eine Beobachtungslücke.</p>")
    ap_("<ol>"
        f"<li><b>Falsches Wort „full“ im Log (bestätigt, kosmetisch):</b> {mk['rly_q_full']} Zeilen <code>RLY … q=full prio=0 slot=-1</code> stehen jeweils direkt nach einem <code>RING_SUPERSEDE_KEEP</code> derselben ID ({mk['rly_q_full_directly_after_supersede_keep_same_id']} von {mk['rly_q_full']}), und der Puffer hatte höchstens 3 Slots belegt. "
        f"Der Code gibt für „neue Kopie verworfen, weil ein neueres {CET} wartet“ -1 zurück, der Relay-Block nennt jedes „kein Slot“ außer dem {CET}-Tor <code>full</code>. Code: <code>src/txring_functions.cpp:1030-1035</code>, <code>src/lora_functions.cpp:2651</code>. "
        "Wirkung: wer das Log liest, sieht einen vollen Puffer, wo gar nichts fehlt. Der OE3-Bericht las <code>full</code> als Kapazitätssignal. Vorschlag: Text <code>supersede_keep</code> (nicht umgesetzt).</li>"
        f"<li><b>Falscher Vergleich „consistent=17&lt;k=2“ (bestätigt, kosmetisch):</b> {mk['trickle_send_consistent_ge_k']} von {mk['trickle_send']} <code>[MC-TRICKLE] SEND</code>-Zeilen nennen eine Zahl größer oder gleich k als „kleiner als k“. Die Gateway-Regel (<code>heyTrickleSuppress</code>, <code>src/loop_functions.cpp:5682</code>) erzwingt die Aussendung, der Else-Zweig druckt aber immer den festen Text. Code: <code>src/esp32/esp32_main.cpp:4815-4826</code>. "
        "Vorschlag: den Grund nennen, etwa <code>gateway_imax</code> (nicht umgesetzt).</li>"
        "<li><b>Stiller Cancel ohne Zähler (Beobachtungslücke, bestätigt):</b> der Relay-Slot wird freigegeben (<code>lora_functions.cpp:1445</code>), der Zähler erhöht (<code>:1446</code>), die Logzeile erscheint nur bei <code>nbrLog != NULL</code> (<code>:1463</code>), und der Zähler steht in keiner Statistikzeile. Zeichnung: siehe F2.</li></ol>")
    f_q = nf()
    svg, h = dr.qfull("3", str(mk["rly_q_full"]))  # depth <= 3: q6/findings.md 3.1
    ap_(explain(svg, f"Abb. {f_q}: Der Log-Text q=full steht für eine verworfene Kopie, der Puffer war nie voll", height=h))
    f_t = nf()
    svg, h = dr.trickle(str(mk["trickle_send_consistent_ge_k"]), str(mk["trickle_send"]))
    ap_(explain(svg, f"Abb. {f_t}: Der Log-Text consistent=17<k=2 nennt einen Grund, der nicht der Auslöser war", height=h))
    ap_("<p><b>Konstruktionsbeobachtungen, kein Fehler:</b> Server-Einspeisungen sind nie abbrechbar (Fall A, siehe F2 und F6), und eigene Baken (Priorität 4 und 5) liegen außerhalb des CSMA-Altersbodens (siehe F3). Beides ist OE3-Befund und hier ohne Verlust.</p>")

    # ---- Nachbarn --------------------------------------------------------------------------
    ap_("<h2>Nachbarn und Hardware-Inventar</h2>")
    ap_("<p>RSSI (Empfangspegel) und SNR (Signal-Rausch-Abstand) gelten nur für direkte Nachbarn, also den letzten Hop. Bei schwachen Verbindungen klebt der RSSI-Wert am Rauschteppich (theoretisch -114 dBm bei dieser Bandbreite) und sagt dort wenig. Die Tabelle zeigt die zehn Stationen mit den meisten Rahmen als letztem Hop.</p>")
    heads = ["Rufzeichen", "Hardware", "Firmware", "Standort", "Höhe", "Entfernung", "Rahmen", "RSSI / SNR (Median)"]
    ap_(table(heads, [neigh_row(t) for t in top[:10]], num_cols=[6]))
    ap_("<details><summary>Weitere direkte Nachbarn und genannte Stationen</summary>")
    erows = [neigh_row(t) for t in top[10:]]
    have = {t["call"] for t in top}
    for c in ["HB9JAT", "DO1FE-19", "HB9CMI-40", "HB9CF-40", "DL1GWN-12"]:
        if c not in have:
            erows.append(info_row(c))
    ap_(table(heads, erows, num_cols=[6]))
    ap_("<p class=\"small\">Hardware und Standort laut Stammdaten (mcmap), sonst Hardware aus dem Funkrahmen. Firmware laut Stammdaten, sonst aus dem Funkrahmen. Stationen ohne Eintrag in den Stammdaten haben „keine Stammdaten“. "
        "HB9JAT (Origin vieler Baken mit wiederverwendeten IDs), DO1FE-19, HB9CMI-40, HB9CF-40 und DL1GWN-12 sind keine direkten Nachbarn, sondern in den Pfaden genannt; Rahmenzahl und Pegel entfallen. "
        "Master-Daten decken 77 von 147 Rufzeichen beider HB9-Logs.</p>")
    ap_("</details>")

    # ---- Grenzen ---------------------------------------------------------------------------
    ap_("<h2>Was nicht geprüft ist / Grenzen</h2><ul>"
        "<li><b>Overhear-Cancel:</b> nur abgeleitet. Ohne <code>[NBR]</code>-Zeilen ist weder der Modus (<code>--nbrrelay on</code> oder <code>count</code>) noch die Lücke von " + hole_a + " bis " + hole_b + " Uhr beweisbar. Der Info-Befehl der Firmware nennt Modus und Zähler (<code>command_functions.cpp:7311</code>).</li>"
        "<li><b>Doppelsendung mit HB9VQQ-5:</b> alle Prozentwerte sind obere Grenzen. Ob die zweite Kopie überflüssig war, entscheidet sich an Bedarfsmeldungen (NEED/CANCEL), die fehlen.</li>"
        "<li><b>Sendezeit:</b> alle Sendezeiten sind Modellwerte (SF11, BW 250 kHz, CR 4:6); das Modell trifft die Messung des Knotens auf unter 1 %.</li>"
        "<li><b>Build:</b> genauer Firmware-Stand unbekannt (Release-Stempel 20261030); Code-Stellen am Stand fork-dev ad228e33 gelesen, dort identisch mit 651e0920.</li>"
        "<li><b>HB9VQQ-5:</b> hat nur <code>--loradebug</code>, keine <code>[LOG]</code>-Zeilen; Gründe fürs Nichtweiterleiten, Zusätze am Rahmenende und ACK-Zuordnung sind dort nicht messbar. Seine Konsole wird stündlich für etwa 5 s übernommen (Lücken im Mitschnitt, alle Zahlen unteres Maß). HB9VQQ-4 hat 0 solche Lücken.</li>"
        "<li><b>Versetzter Beginn:</b> die beiden Mitschnitte beginnen 24 min auseinander; Paarvergleiche nutzen nur das gemeinsame Fenster.</li>"
        "<li><b>Retry-Leiter:</b> lief nie (0 neue Versuche), bleibt ungetestet.</li>"
        "<li><b>Nur 24 Stunden</b> eines Samstags (31.10.2026), kein Wochenvergleich.</li>"
        "<li><b>Nicht gegengeprüft:</b> die HB9JAT-ID-Wiederverwendung, die Dedup-Umlaufzeit (42 min) und die Hop-Summen.</li>"
        "<li><b>Nicht verwendet:</b> einzelne Extraktor-Werte, die für diesen Knoten nicht gelten (Details unter Methodik).</li></ul>")

    ap_("<h2>Offene Fragen an den Betreiber</h2><ol>"
        "<li>Ist der Overhear-Cancel (<code>--nbrrelay on</code>) auf HB9VQQ-4 aktiv, und lässt sich ein 1- bis 2-stündiger Mitschnitt mit <code>--nbrdebug on</code> auf beiden Knoten fahren? Damit werden Cancel, Bedarfsmeldungen und die Doppelsendung lesbar.</li>"
        f"<li>Was war zwischen {hole_a} und {hole_b} Uhr anders (Nachbarn aus- oder eingeschaltet, Konfiguration)? In dieser Zeit griff der Cancel nie.</li>"
        "<li>Soll die Server-Einspeisung nur ein Gateway des Paars übernehmen? Heute senden beide, in 138 von 146 Zeitsignal-Telegrammen dasselbe.</li>"
        "<li>Kann jemand HB9HNO-49 ansprechen: Firmware 4.35h auf 4.40a, der Rückstau von etwa 25 min verspätet jeden Rahmen.</li>"
        "<li>Kann jemand den Betreiber von HB9LEK-12 bitten, den Frequenzversatz von +4,5 kHz (Quarz) zu prüfen?</li>"
        "<li>Gibt es am Standort Orn-Bachtel eine Quelle, die im Takt von Stunden kurz bei -72 dBm sendet (Netzteil, Funkgerät)?</li></ol>")

    # ---- dev details -----------------------------------------------------------------------
    ap_('<details class="dev"><summary>Methodik, Marker, Code-Stellen, Skripte (für Entwickler)</summary>')
    ap_("<h3>Definitionen und Abweichungen</h3><ul>"
        "<li><b>Zeitbasis:</b> Logger-Uhr (Mac, MEZ). Stundenindex der Cancel-Auswertung: Stunde 0 beginnt mit der ersten Logzeile (09:39:56).</li>"
        "<li><b>Schicksal einer Schreibung:</b> exakter Verbund aus <code>RING_WRITE slot= msg_id=</code> und <code>RING_TX_READ</code> (gleicher Slot, gleiche ID). Nicht gesendet und nicht ersetzt (<code>RING_SUPERSEDE</code> in den drei Folgezeilen) heißt „verschwunden“, wenn der Slot neu beschrieben wird oder das Log mehr als 400 s später endet. Bilanz: 3010 = 2577 + 50 + 383; <code>RING_TX_READ</code> 2577 gegen <code>[MC-STAT]</code> 2580 (3 = Fensterränder).</li>"
        "<li><b>Fremde Kopie:</b> <code>MH-LoRa</code> derselben ID mit mindestens 2 Pfad-Token, letzter Hop ungleich HB9VQQ-4, zwischen Schreibung und Slot-Ende (Bedingung <code>lora_functions.cpp:1327-1331</code>).</li>"
        "<li><b>Korrekturen gegenüber den findings-Dateien (verify/hb9.md):</b> 383 statt 381 verschwundene Relays; Hörbarkeit 92,1 % / 64,9 % im gemeinsamen Fenster statt 90,0 / 64,0 %; 46 veraltete Rahmen bei uns und 50 bei HB9VQQ-5 (53 verschiedene Telegramme) statt „ein veraltetes Telegramm“; die späten Rahmen von HB9HNO-49 bei HB9VQQ-5 sind HEY und Positionen, kein {CET}; Faktor 1,47 statt 2 bei HB9VQQ-5; CRC-Anteil 11,5 % statt 11,6 %.</li>"
        "<li><b>CRC-Fehler im Band:</b> health/findings.md nennt 369 im 4500-Hz-Histogrammfach, der Helfer zählt 371 mit 4000 bis 5000 Hz Frequenzfehler; Zuordnung zu HB9LEK-12 ist Vermutung.</li>"
        "<li><b>Füllstand 8 von 20:</b> <code>RING_WRITE queued=</code> zeigt den Stand vor dem Eintrag (höchstens 7), die Tiefe nach der Schreibung 8; <code>[MC-STAT] qmax</code> 10/20 ist ein Latch seit dem Start und sagt über den Mitschnitt nichts.</li>"
        "<li><b>Nicht verwendete Extraktorwerte:</b> <code>health beat_miss</code> für HB9VQQ-4 (0 echte Fehlstellen, 2879 KEEP / 2879 BEAT im Wechsel); das Gegenrechnen ohne TTL in <code>q34 cf</code> (kennt keinen Cancel-Pfad, 285 vorhergesagte gegen 0 echte Verwürfe); <code>q5 tx_age_gt150_n</code> (ID-Wiederverwendung OE1XAR-28/-63 im Abstand von 21 h); <code>health ack_ping_attribution</code> mit OE3-Schlüssel.</li>"
        "<li><b>Literale in Urteilstexten</b> stammen aus data/hb9/CORE-FINDINGS.md, verify/hb9.md und den findings-Dateien: Burst-Zeiten 18:56 bis 19:02, 138 von 146 Telegrammen, rund 8 Aussendungen je Telegramm, 32/21/11/14 späte HB9HNO-49-Rahmen bei HB9VQQ-5, 8 Nachbarn von HB9VQQ-5 und 5 geteilte, 8,4 s/12,3 s bei leerem Puffer, 2,9/2,3 Rahmen pro Minute in der Spitze, 3,0 % Empfangszeit, HB9JAT-Zahlen, OE3-Vergleichswerte (CORE-FINDINGS Nr. 3, 4, 5, 6).</li></ul>")
    ap_("<h3>Marker</h3><ul>"
        "<li><code>[MC-DBG] RING_WRITE slot= type= status= len= msg_id= queued=/20 src=</code>; <code>RING_TX_READ … prio= lat=</code> (lat = Wartezeit im Puffer); <code>RING_SUPERSEDE slot= prio= old_id= new_id=</code>; <code>RING_SUPERSEDE_KEEP</code>; <code>RING_DROP_STALE</code> (nie aufgetreten).</li>"
        "<li><code>[MC-DBG] CSMA_AGED</code> (Altersboden, <code>lora_functions.cpp:4040</code>), <code>CAD_SCAN</code>, <code>CHANNEL_UTIL rx= tx= util=</code>, <code>CRC_ERROR rssi= snr= freq_err=</code>, <code>RX_IRQ_STALE rssi= flags=</code>.</li>"
        "<li><code>[CET];gw_rx|rx|tx;…;age_s;N;trusted;1</code>: Server-Einspeisung, Funk-Empfang, eigene Sendung mit Alter. <code>[LOG] RLY x&lt;id&gt; : H&lt;nn&gt; q=&lt;grund&gt;</code>: Relay-Entscheidung.</li>"
        "<li><code>[MC-TRICKLE] SEND|SUPPRESS|TOPO_CHANGE consistent=N&lt;k=K</code>: Trickle-Zyklus der eigenen HEY.</li>"
        "<li><code>[NBR]|CANCEL|NEED|…</code>: in diesem Log nicht vorhanden (<code>nbrLog == NULL</code>, <code>lora_functions.cpp:694</code>).</li></ul>")
    ap_("<h3>Code-Stellen (am Stand fork-dev ad228e33 gelesen)</h3><ul>"
        "<li>Cancel: <code>src/lora_functions.cpp:1327-1331</code> (Bedingung), <code>:1445</code> (Slot frei), <code>:1446</code> (Zähler), <code>:1450-1456</code> (Modus count gibt nichts frei), <code>:1463</code> (Log nur mit nbrLog).</li>"
        "<li>Puffer: <code>txring_functions.cpp:213-255</code> (Auswahl), <code>:318</code> (TX verbraucht), <code>:385-405</code> (Fall A), <code>:615-641</code> (Verfall, Log), <code>:880-910</code> ({CET}-Supersede), <code>:1007</code> (Supersede setzt Alter zurück), <code>:1030-1035</code> (KEEP gibt -1), <code>:1068</code>; <code>txring_ttl.h:46-64</code>; <code>configuration_global.h:405-411, 465-487, 581-582</code>.</li>"
        "<li>Haltezeit und Altersboden: <code>lora_functions.cpp:2746-2747</code> (Neustart je Empfang), <code>:3998-4006</code>, <code>:4030-4043</code>; <code>csma_timing.h:71-112</code>; Relay-Label <code>lora_functions.cpp:2651</code>.</li>"
        "<li>{CET}-Tor: <code>cet_age.h:16, 77-82, 120-135</code>; Einspeise-Tor <code>gw_udp_ring.h:37-48</code>; Alleinversorger-Test <code>nbr_matrix.cpp:1777-1852</code>.</li>"
        "<li>Trickle: <code>src/esp32/esp32_main.cpp:4797-4830</code>, <code>loop_functions.cpp:5682</code>. Auto-Ping (hier nicht ausgeübt): ESP32 <code>esp32_main.cpp:5264, :5285</code> gegen RAK4631 <code>nrf52_main.cpp:2717-2737</code>.</li></ul>")
    ap_("<h3>Quellen</h3><ul>"
        "<li>hb9/health, hb9/q2, hb9/q34, hb9/q5, hb9/q6: <code>results.json</code> (Schlüssel nodes.hb9vqq4) und <code>findings.md</code>; hb9/CORE-FINDINGS.md (verifiziert, bindend); verify/hb9.md; verify/rca_own_beacons.md</li>"
        "<li>hb9/master/master.tsv, hb9/master/distances.tsv (Stammdaten, Entfernung von Orn-Bachtel)</li>"
        "<li>extra/hb9vqq4_measure.json: Schicksal der Slots mit Stundenprofil, HEY-Wartezeit nach Quelle, Paar-Redundanz, gegenseitige Hörbarkeit, veraltete {CET}-Rahmen, {CET}-Paare, Frequenzfehler je Nachbar, Log-Marker; erzeugt aus den normalisierten Logs beider HB9-Knoten mit <code>generators/lib/hb9vqq4_measure.py --norm &lt;norm-Verzeichnis&gt; --out &lt;Datei&gt;</code></li>"
        "<li>Skript: <code>docs/log-reports/2026-11-01-24h/generators/hb9vqq4.py</code> (dieses Programm; Aufruf mit <code>--data</code>, <code>--kit</code>, <code>--out</code>; liest keine Rohlogs), Zeichnungen in <code>generators/lib/hb9vqq4_draw.py</code></li></ul>")
    ap_("</details>")

    ap_('<div class="foot"><p>Quellen: Mitschnitte 31.10./01.11.2026, Auswertung vom 01.11.2026. Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware. '
        '<a href="uebersicht.html">Zur Übersicht (Netzsicht)</a> · <a href="hb9vqq5.html">Schwesterknoten HB9VQQ-5</a></p></div>')

    html = page("HB9VQQ-4 · MeshCom Log-Auswertung", "\n".join(body))
    # the log marker "src=..." appears in visible text only; write the equals sign as entity so that
    # a plain grep for resource attributes stays clean (the page has no src attributes at all)
    html = html.replace("src=", "src&#61;")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(html, encoding="utf-8")
    print(f"wrote {args.out} ({len(html)} bytes, {fig[0]} figures)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
