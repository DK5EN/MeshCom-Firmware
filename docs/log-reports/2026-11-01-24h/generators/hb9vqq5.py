#!/usr/bin/env python3
"""Generator for the per-node report HB9VQQ-5 (24-h log analysis, 2026-11-01).

Usage:
    python3 -I hb9vqq5.py --data <dir> --kit <dir> --out <file.html>

--data  run data directory (hb9/{health,q2,q34,q5,q6,master}/, hb9/CORE-FINDINGS.md, extra/)
--kit   directory holding reportkit.py (shared HTML/CSS/SVG building blocks)
--out   HTML file to write

No raw logs are read at render time. HB9VQQ-5 was captured with --loradebug only (no [LOG] lines), so
the numbers that the standard extractor cannot read come from extra/hb9vqq5_measure.json, written once from
the normalised logs by lib/hb9vqq5_measure.py (--norm <dir> --out <file>). Judgement text may carry literals
from hb9/CORE-FINDINGS.md and verify/hb9.md; the developer details name them. Stdlib only.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any

NODE = "hb9vqq5"
CALL = "HB9VQQ-5"


def de(x: float, nd: int = 1) -> str:
    """German number format: decimal comma, dot as thousands separator."""
    if nd == 0:
        return f"{x:,.0f}".replace(",", ".")
    return f"{x:,.{nd}f}".replace(",", "X").replace(".", ",").replace("X", ".")


def pct(a: float, b: float, nd: int = 1) -> str:
    return de(100.0 * a / b, nd) + " %"


def jload(p: Path) -> Any:
    return json.loads(p.read_text(encoding="utf-8"))


def load_master(d: Path) -> tuple[dict[str, dict[str, str]], dict[str, str]]:
    master: dict[str, dict[str, str]] = {}
    with open(d / "hb9" / "master" / "master.tsv", newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            master[r["call"]] = r
    dist: dict[str, str] = {}
    with open(d / "hb9" / "master" / "distances.tsv", newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            dist[r["call"]] = r.get("d_hb9vqq5", "")
    return master, dist


def fw_text(fw: str) -> str:
    return fw if fw else "unbekannt"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--kit", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
    sys.path.insert(0, str(args.kit.resolve()))
    import hb9vqq5_draw as dr
    from reportkit import bars, chip, explain, hist, page, table

    D: Path = args.data
    HB = D / "hb9"
    H5 = jload(HB / "health" / "results.json")["nodes"][NODE]
    H4 = jload(HB / "health" / "results.json")["nodes"]["hb9vqq4"]
    Q2 = jload(HB / "q2" / "results.json")["nodes"][NODE]
    M = jload(D / "extra" / "hb9vqq5_measure.json")
    master, dist = load_master(D)

    rx = M["rx"]
    ring = M["ring"]
    fate = ring["fate"]
    lat = ring["latency"]
    cls = lat["classes"]
    csma = ring["csma"]
    cet = M["cet"]
    stale = cet["stale"]
    hno = M["hno"]
    pair = M["pair"]
    cp = M["cet_pair"]
    con = M["console"]
    hl = M["health"]
    cad = H5["cad"]
    chan = H5["channel"]["CHANNEL_UTIL"]
    mutual = pair["mutual_common_window"]

    # ---- derived numbers ---------------------------------------------------------------------
    writes = fate["writes"]
    n_air = fate["fates"]["aired"]
    n_sup = fate["fates"]["superseded"]
    n_van = fate["fates"]["vanished"]
    n_unseen = ring["reads"] - n_air
    ev_total = rx["crc_n"] + rx["good_n"]
    hours = M["meta"]["span_h"]
    tx_total = ring["reads"]
    air_tot = fate["aired_total_air_s"]
    dec_share = 100.0 * rx["decoded_air_s_model"] / (rx["rx_ms_sum"] / 1000.0)
    ratio_hey = round(cls["hey_own"]["p50"] / cls["hey_relay"]["p50"], 2)
    ratio_txt = de(ratio_hey, 2)
    dedup_min = 100.0 / (H5["ring"]["dedup"]["add"] / hours) * 60.0
    per_hour = [(k, v) for k, v in H5["cad"]["per_hour"].items()]
    crc_h = [v["rx_err"] for _k, v in per_hour[:-1]]
    own_hey_written = fate["by_src"]["auto_pos"]["aired"] + fate["by_src"]["auto_pos"].get("superseded", 0)
    ack_tx = sum(fate["by_src"].get(k, {}).get("aired", 0) for k in ("rx_ack_fwd", "rx_dm_ack_gw", "rx_dm_ack_new"))
    udp_w = fate["by_src"]["udp_rx"]
    rel_w = fate["by_src"]["rx_relay"]
    dbl = Q2["double_write"]["pairs"]
    sens = lat["hey_ttl_sensitivity_aired_older_than_s"]
    hno_m = master["HB9HNO-49"]
    nb_c = {t["call"]: t for t in rx["neighbours"]}

    # ---- figures (numbers in order of appearance) --------------------------------------------
    F = {"crc": 1, "crc_h": 2, "console": 3, "hey": 4, "silent": 5, "head": 6, "hno": 7, "red": 8, "trickle": 9}

    def fig(key: str, svg: tuple[str, int], caption: str) -> str:
        return explain(svg[0], f"Abb. {F[key]}: {caption}", 640, svg[1])

    body: list[str] = []
    ap_ = body.append

    # ---- header -----------------------------------------------------------------------------
    ap_(f'<div class="eyebrow">MeshCom Log-Auswertung · {CALL}</div>')
    ap_(f"<h1>{CALL} läuft stabil und verliert nichts; jedes zweite erkannte Funksignal ({de(rx['crc_share_pct'])} %) "
        "bleibt am Empfindlichkeitsrand unlesbar</h1>")
    ap_('<ul class="meta">'
        f"<li><b>Log-Datei:</b> HB9VQQ-5_loradebug_20261031-1003.log ({de(M['meta']['lines'], 0)} Zeilen)</li>"
        f"<li><b>Zeitraum:</b> 31.10.2026 10:03 bis 01.11.2026 10:03 (Uhr des Mitschnitt-Rechners, MEZ; die Knotenuhr geht etwa 1 s nach), {de(hours)} h</li>"
        "<li><b>Mitschnitt-Art:</b> nur <code>--loradebug</code>, <b>ohne</b> <code>--setlog</code> (keine [LOG]-Zeilen), ohne <code>--nbrdebug</code> (keine [NBR]-Zeilen)</li>"
        "<li><b>Hardware:</b> Heltec WiFi LoRa 32 V3 (ESP32-S3), laut Stammdaten (mcmap)</li>"
        "<li><b>Firmware:</b> 4.40a (aus dem Keep-alive), genauer Build unbekannt (keine STAT-Zeile); die TX-Pool-Marker (Ersetzen, Altersboden, {CET}-Tor) sind vorhanden</li>"
        "<li><b>Standort:</b> Tanzboden SG, 47,2215 N / 9,1072 E, 1432 m</li>"
        "<li><b>Rolle:</b> Internet-Gateway und Relay im Schweizer Mesh (getrennt vom OE3-Netz)</li>"
        "<li><b>LoRa-Parameter:</b> SF11 / 250 kHz / CR 4:6 (Modell reproduziert die Sendesummen des Knotens auf 0,6 %; Frequenz und Leistung stehen nicht im Log)</li>"
        '<li><b>Partnerknoten:</b> <a href="hb9vqq4.html">HB9VQQ-4</a> (Orn-Bachtel ZH, 995 m, 18,6 km entfernt), gleiche Betreiberseite</li>'
        "<li><b>Stand:</b> 01.11.2026</li></ul>")
    ap_('<div class="bluf"><p class="lead">'
        f"{CALL} lief 24 Stunden ohne Neustart, ohne Speicherverlust und ohne einen einzigen verworfenen Eintrag im Sendepuffer. "
        f"Die einzige Auffälligkeit ist die Lage: {de(rx['crc_n'], 0)} von {de(ev_total, 0)} Empfangsereignissen ({de(rx['crc_share_pct'])} %) "
        f"fallen durch die CRC-Prüfung, der Signal-Rausch-Abstand liegt im Median bei {de(rx['crc_snr_p50'], 0)} dB, also an der Empfindlichkeitsgrenze, nicht an einem Störer.</p>"
        "<p>Es wurde kein Firmware-Fehler gefunden. Dieser Mitschnitt hat keine [LOG]-Zeilen; was deshalb nicht messbar ist, steht im Kasten unten und ist in jedem Abschnitt als „nicht messbar“ markiert, nie als Null.</p></div>")

    # ---- Messbarkeit (prominent) -----------------------------------------------------------
    ap_('<div class="callout watch"><p><b>Was dieser Mitschnitt nicht zeigen kann</b></p>'
        "<p><b>Ursache:</b> <code>--setlog</code> war aus. Es fehlen alle [LOG]-Zeilen (Empfang, Weiterleitungsentscheidung, Sendung, STAT-Zeile) "
        "und, wie bei HB9VQQ-4, alle [NBR]-Zeilen (<code>--nbrdebug</code> aus). Was sich aus den übrigen Markern (RING_*, [CET], CHANNEL_UTIL) "
        "und den Empfangszeilen zurückrechnen lässt, ist unten gekennzeichnet; alles andere bleibt leer.</p>")
    ap_(table(["Größe", "Status", "Woher / Ersatz"], [
        ["Grund, warum ein Rahmen nicht weitergeleitet wurde (hop0, gwcap, full ...)", "nicht messbar",
         f"nur RELAY_QUEUED ist da (1.800); bei {de(rx['accepted_new'] - 1800, 0)} von {de(rx['accepted_new'], 0)} neu angenommenen Rahmen bleibt der Grund offen"],
        ["Empfangs-Flags (Duplikat, eigener Rahmen, Echo)", "nicht messbar", "Ersatz: RX_DEDUP_NEW / RX_DEDUP_DUP"],
        ["STAT-Zeile (Heap, Zähler, Firmware-Stempel)", "nicht messbar", "Ersatz: [HEAP], [MC-STAT], [MC-HWM], CHANNEL_UTIL"],
        ["Sendequelle, Priorität und Wartezeit je Sendung", "rekonstruiert", "RING_WRITE + RING_TX_READ, exakter Abgleich über Platz und Kennung"],
        ["Empfangspegel (RSSI, SNR) je Rahmen", "rekonstruiert", "Zeile „Received packet“ direkt vor jeder dekodierten Rahmenzeile"],
        ["ACK-Empfang", "abgeleitet", f"ONRXDONE_TIME minus OnRxDone = {rx['ack_receptions']} (ACK-Rahmen drucken keine OnRxDone-Zeile)"],
        ["ACK-Zuordnung (wer wurde quittiert), Gateway-Meldungen GWU/GWI, DM, MBOX", "nicht messbar", "Ersatz: RX-UDP / TX-UDP / RING_WRITE mit Quelle udp_rx"],
        ["Firmware der Nachbarn (Zählung)", "nicht ausgewertet", "nur Stammdaten und das Firmware-Feld einzelner Rahmen"],
        ["Overhear-Cancel, NEED, Case A (beide Knoten)", "nicht messbar", f"Rückschluss aus verschwundenen Plätzen ({fate['vanished_with_foreign_copy']} von {n_van} mit vorher gehörter fremder Kopie)"],
        ["Zähler im Knoten (Info-Befehl, „cancelled N“)", "nicht im Log", "wird nicht in den Mitschnitt gedruckt"],
        ["Konsolenlücken (27 mal etwa 5 s)", "Untergrenzen", f"alle Zählungen dieser Seite sind Untergrenzen ({de(con['reconnect_sum_s'])} s = {de(con['reconnect_share_pct'], 2)} % des Tages)"],
    ], raw=False))
    ap_("<p><b>Empfehlung:</b> beim nächsten Mitschnitt <code>--setlog on</code> einschalten. Für die Frage, ob der Overhear-Cancel arbeitet und warum zwei Gateways doppelt senden, "
        "reichen auf beiden Knoten ein bis zwei Stunden mit <code>--nbrdebug on</code>.</p></div>")

    ap_('<div class="callout watch"><p><b>Empfehlungen (kein Handlungsbedarf an der Firmware)</b></p><ul>'
        "<li><b>Mitschnitt-Einstellung (Betreiber):</b> <code>--setlog on</code> beim nächsten 24-h-Lauf, siehe Kasten oben.</li>"
        f"<li><b>Konsole zur vollen Stunde (Betreiber):</b> ein zweiter Client übernimmt jede Stunde die einzige Konsolen-Sitzung ({con['lost']} Unterbrechungen); wer das ist, ist offen. Der Mitschnitt verliert dabei je etwa 5 s.</li>"
        f"<li><b>HB9HNO-49 (Betreiber dort ansprechen):</b> Firmware 4.35h, sendet jeden Rahmen rund 25 Minuten zu spät (Abschnitt F5 und F6). Die beiden Knoten schaden dadurch nicht, aber {hno['late_relayed_total']} Spätkopien wurden weitergegeben.</li>"
        f"<li><b>Zwei Gateways, doppelte Sendung (Konzeptfrage):</b> {de(pair['distinct_ids_in_pairs'], 0)} Kennungen sendeten beide Knoten; ob das gewollt ist, lässt sich ohne [NBR]-Zeilen nicht entscheiden.</li>"
        "</ul></div>")

    # ---- core facts ------------------------------------------------------------------------
    ap_("<h2>Kernfakten</h2><ul>"
        f"<li><b>Stabil:</b> Laufzeit {de(hl['uptime_first_s'], 0)} s bis {de(hl['uptime_last_s'], 0)} s in {hl['uptime_samples']} Stichproben ohne Rücksprung, keine Absturzmeldung, Heap {de(hl['heap_min'], 0)} bis {de(hl['heap_max'], 0)} Byte ohne Trend, "
        f"{de(hl['keep_n'], 0)} Keep-alives und {de(hl['beat_n'], 0)} Antworten des Servers.</li>"
        f"<li><b>Empfindlichkeitsrand:</b> {de(rx['crc_n'], 0)} CRC-Fehler gegen {de(rx['good_n'], 0)} gute Empfangsereignisse ({de(rx['crc_share_pct'])} %), SNR-Median {de(rx['crc_snr_p50'], 0)} dB; "
        f"{de(rx['crc_ferr_lt1000_pct'])} % davon mit Frequenzfehler unter 1 kHz. Bei HB9VQQ-4 sind es {de(H4['rf']['rx_error_share_of_rx_pct'])} %.</li>"
        f"<li><b>Sendepuffer:</b> {de(writes, 0)} Schreibungen, {pct(n_air, writes)} gesendet, {pct(n_sup, writes)} ersetzt, {pct(n_van, writes)} still verschwunden, 0 verworfen; höchste Füllung {hl['status_queued']['max']:.0f} von 20 (RING_STATUS), {H5['ring']['ring_write_queued_max']} nach einer Schreibung.</li>"
        f"<li><b>Eigene Baken:</b> HEY {fate['by_src']['auto_pos']['aired']} von {own_hey_written} gesendet (1 durch eine neuere ersetzt), Wetterposition {fate['by_src']['user_wx']['aired']} von {fate['by_src']['user_wx']['aired']}; "
        f"die eigene HEY wartet im Median {de(cls['hey_own']['p50'])} s gegen {de(cls['hey_relay']['p50'])} s für ein Relay-HEY ({ratio_txt}-fach).</li>"
        f"<li><b>Zeitsignal:</b> {cet['gw_rx_by_src']['OE1XAR-33']} Telegramme von OE1XAR-33 per UDP ohne Lücke; {cet['tx_n']} eigene {{CET}}-Abstrahlungen, die älteste {de(cet['tx_age_s']['max'], 0)} s alt (Grenze 150 s).</li>"
        f"<li><b>Veraltete Kopien:</b> HB9HNO-49 strahlt {stale['stale']} {{CET}}-Telegramme mit {de(stale['stale_age_min'], 0)} bis {de(stale['stale_age_max'], 0)} s Alter ab; HB9VQQ-5 nahm davon {stale['stale_accepted_new']} an. "
        f"Späte HEY- und Positionskopien nahm der Knoten {hno['late_accepted_new_total']} Mal an und gab {hno['late_relayed_total']} weiter (gewollt, kein Alterstor für diese Typen).</li>"
        f"<li><b>Doppelte Sendungen mit HB9VQQ-4:</b> {de(pair['distinct_ids_in_pairs'], 0)} Kennungen von beiden gesendet; die spätere Kopie macht {de(pair['later_copy_air_pct'])} % der Paar-Sendezeit aus (Obergrenze).</li>"
        f"<li><b>Konsole:</b> {con['lost']} Übernahmen durch einen zweiten Client ({con['at_full_hour']} zur vollen Stunde), zusammen {de(con['reconnect_sum_s'])} s Lücke im Mitschnitt.</li>"
        "</ul>")

    # ---- Antwort-Index ---------------------------------------------------------------------
    idx = [
        ["Q0 Gesundheit und Funkumfeld", f"Keine Neustarts, Speicher flach, Server stabil. {de(rx['crc_share_pct'])} % CRC-Fehler bei SNR-Median {de(rx['crc_snr_p50'], 0)} dB: Empfindlichkeitsrand auf 1432 m.", chip("watch")],
        ["Q2 TX-Puffer mit Slots", f"Nichts baut sich auf (höchstens {hl['status_queued']['max']:.0f} von 20 belegt), {pct(n_air, writes)} gesendet, 0 verworfen. Eigene HEY wartet {ratio_txt}-fach länger als ein Relay-HEY, ohne Verlust.", chip("ok")],
        ["Q3 Verlust durch Zeitverfall", f"0 Verfälle durch Lebensdauer oder {{CET}}-Tor. Die HEY-Klasse erreicht bis zu {de(cls['hey_own']['max_over_ttl_pct'])} % ihrer 180 s.", chip("watch")],
        ["Q4 Gewinn durch Zeitverfall", "Nicht nachweisbar: es gab nichts zu verwerfen. Das Netz wurde nicht gebraucht; ein Ohne-TTL-Vergleich ist für diesen Knoten nicht möglich.", chip("neu")],
        ["Q5 Zeitsignal", f"UDP-Telegramme lückenlos ({de(cet['gw_rx_gap_s']['min'], 0)} bis {de(cet['gw_rx_gap_s']['max'], 0)} s Abstand), eigene Abstrahlung höchstens {de(cet['tx_age_s']['max'], 0)} s alt. Beobachten: Doppelabstrahlung mit HB9VQQ-4, veraltete Kopien von HB9HNO-49.", chip("watch")],
        ["Q6 Manipulationen", f"Kein Aufbau, keine Schleifen, Sendeanteil {de(rx['tx_duty_pct'], 1)} %. Beobachten: Doppelsendungen mit HB9VQQ-4, Spätkopien von HB9HNO-49, stündliche Konsolenübernahme.", chip("watch")],
        ["Verdachtsfälle Firmware", "Kein Fehler gefunden. Zwei kosmetische Logtexte und eine Beobachtungslücke (stiller Abbruch ohne Zähler).", chip("ok", "kein Fehler")],
    ]
    ap_("<h2>Antwort-Index</h2>")
    ap_(table(["Frage", "Kurzantwort", "Urteil"], idx, raw=True))
    ap_("<details><summary>Begriffe in einem Satz</summary><ul>"
        "<li><b>CRC-Fehler:</b> Signal erkannt, aber die Prüfsumme des Rahmens stimmt nicht; der Rahmen ist unlesbar.</li>"
        "<li><b>SNR:</b> Signal-Rausch-Abstand in dB; unter etwa -17,5 dB kann LoRa mit SF11 nichts mehr dekodieren.</li>"
        "<li><b>HEY:</b> Lebenszeichen-Bake eines Knotens, Priorität 5.</li>"
        "<li><b>Relay:</b> Knoten, der fremde Rahmen weitersendet.</li>"
        "<li><b>TTL:</b> Lebensdauer eines Eintrags im Sendepuffer; danach wird er verworfen, auch ungesendet.</li>"
        "<li><b>CAD:</b> Kanalabfrage, ob jemand sendet, bevor man selbst sendet.</li>"
        "<li><b>CSMA-Haltezeit:</b> Wartezeit vor dem Senden; jeder gehörte Rahmen setzt sie zurück.</li>"
        "<li><b>Supersede (ersetzen):</b> Eine neuere Bake desselben Absenders ersetzt die noch ungesendete ältere im selben Platz.</li>"
        "<li><b>Overhear-Cancel:</b> Eine ungesendete Relay-Kopie wird verworfen, wenn ein Nachbar sie schon gesendet hat.</li>"
        "<li><b>Case A:</b> Der Knoten hält sich für den einzigen, der bestimmte Nachbarn erreicht; dann bricht er nie ab.</li>"
        "<li><b>Dedup-Ring:</b> Speicher der zuletzt gesehenen Nachrichten-Kennungen (100 Plätze), damit Kopien nicht ein zweites Mal verarbeitet werden.</li>"
        "<li><b>{CET}:</b> Zeittelegramm des Servers OE1XAR-33 alle gut 10 Minuten; der Inhalt ist UTC.</li>"
        "<li><b>Gateway:</b> Knoten mit Internetanbindung, der Rahmen zwischen Funk und Server vermittelt.</li></ul></details>")

    # ---- Gesundheit ------------------------------------------------------------------------
    ap_("<h2>Gesundheit und Funkumfeld</h2>")
    ap_("<p><b>Der Knoten ist gesund; die Funkumgebung ist es nicht im üblichen Sinn.</b> Kein Neustart, kein Speicherverlust, die Server-Anbindung steht. "
        "Dafür hört der Knoten auf 1432 m weit mehr schwache Signale, als er lesen kann.</p>")
    ap_("<ul>"
        f"<li><b>Kein Neustart:</b> Laufzeit {de(hl['uptime_first_s'], 0)} s bis {de(hl['uptime_last_s'], 0)} s ({hl['uptime_samples']} Stichproben, streng steigend); {hl['crash_hits']} Absturzmarker.</li>"
        f"<li><b>Speicher flach:</b> {hl['heap_samples']} Stichproben, erste {de(hl['heap_first'], 0)}, letzte {de(hl['heap_last'], 0)} Byte; kleinster freier Wert und größter Block bleiben in allen Stichproben gleich.</li>"
        f"<li><b>Server-Anbindung:</b> {H5['health']['wifi_markers']} von {H5['health']['wifi_markers']} WLAN-Meldungen „up“; {de(hl['keep_n'], 0)} Keep-alives, {de(hl['beat_n'], 0)} Antworten.</li>"
        f"<li><b>Empfangsschleife:</b> ONRXDONE_TIME Median {H5['onrxdone']['ms']['median']:.0f} ms, höchstens {H5['onrxdone']['ms']['max']:.0f} ms, {H5['onrxdone']['gt50ms']} Werte über 50 ms; Kanalabfrage-Zeitüberschreitung {cad['cad_safety_timeout']}.</li>"
        f"<li><b>Keine Empfangsstille:</b> längste Pause zwischen zwei Empfangsereignissen {de(H5['rx_silence']['longest_gaps'][0]['gap_s'])} s, {H5['rx_silence']['gaps_gt_300s']} Pausen über 300 s (das Muster der tauben Empfänger bei OE3XIA fehlt).</li>"
        f"<li><b>Decodiert:</b> {de(rx['decoded'], 0)} Rahmen ({de(rx['decoded'] / hours, 0)} pro Stunde), {de(rx['distinct_ids'], 0)} verschiedene Kennungen; "
        f"{pct(rx['types']['hey'], rx['decoded'])} HEY, {pct(rx['types']['pos'], rx['decoded'])} Position, {pct(rx['types']['text'], rx['decoded'])} Text; {pct(rx['direct_zero_relay'], rx['decoded'])} ohne Relay-Hop (direkt).</li>"
        f"<li><b>ACK-Empfänge:</b> {rx['ack_receptions']} (abgeleitet aus ONRXDONE_TIME minus OnRxDone; ACK-Rahmen drucken keine OnRxDone-Zeile).</li>"
        "</ul>")

    ap_("<h3>Jedes zweite erkannte Signal ist nicht lesbar (Beobachten)</h3>")
    ap_("<ul>"
        f"<li><b>Zahlen:</b> {de(rx['crc_n'], 0)} CRC-Fehler ({de(rx['crc_n'] / hours, 0)} pro Stunde) gegen {de(rx['good_n'], 0)} gute Empfangsereignisse; Fehleranteil {de(rx['crc_share_pct'])} %. HB9VQQ-4: {de(H4['rf']['rx_error_share_of_rx_pct'])} %.</li>"
        f"<li><b>Pegel:</b> die fehlerhaften Signale liegen bei RSSI-Median {de(rx['crc_rssi_p50'], 0)} dBm (stärkstes {de(rx['crc_rssi_max'], 0)} dBm) und SNR-Median {de(rx['crc_snr_p50'], 0)} dB. "
        f"Dekodierte Rahmen: RSSI-Median {de(rx['decoded_rssi_p50'], 0)} dBm (stärkster {de(rx['decoded_rssi_max'], 0)} dBm), SNR-Median {de(rx['decoded_snr_p50'], 0)} dB.</li>"
        f"<li><b>Das Maß ist der SNR, nicht der RSSI:</b> ein Pegel von -118 dBm bei negativem SNR ist der Rauschteppich. Der SNR-Median von {de(rx['crc_snr_p50'], 0)} dB liegt an der Demodulationsgrenze von SF11 (etwa -17,5 dB).</li>"
        f"<li><b>Kein Störer:</b> {de(rx['crc_ferr_lt1000_pct'])} % der Fehlersignale haben einen Frequenzfehler unter 1 kHz, nur {de(rx['crc_ferr_gt3000_pct'])} % über 3 kHz. Ein Störer neben der Frequenz sähe anders aus.</li>"
        f"<li><b>Verlauf:</b> {min(crc_h)} bis {max(crc_h)} Fehler pro Stunde, kein Tagesgang (Abb. {F['crc_h']}).</li>"
        f"<li><b>Kosten:</b> von {de(rx['rx_ms_sum'] / 1000, 0)} s Empfangszeit (CHANNEL_UTIL) entfallen nach Modell nur {de(rx['decoded_air_s_model'], 0)} s ({de(dec_share)} %) auf dekodierte Rahmen, der Rest sind nicht lesbare Signale. "
        f"Die Kanalabfrage meldet in {pct(cad['cad_false_positive'], cad['cad_busy_1_initial'])} der ersten Treffer einen Fehlalarm ({de(cad['cad_false_positive'], 0)} von {de(cad['cad_busy_1_initial'], 0)}); das kostet Wartezeit, nicht Sendezeit.</li>"
        "<li><b>Ursache (Vermutung):</b> ferne Stationen am Rand der Reichweite; der Standort auf 1432 m hört weit. Ein Beleg für die Entfernung der Sender gibt der Mitschnitt nicht (die CRC-Rahmen haben keinen Absender).</li>"
        "</ul>")
    ap_(fig("crc", dr.crc_edge(de(rx["crc_n"], 0), de(rx["good_n"], 0), de(rx["crc_share_pct"]) + " %", int(rx["crc_snr_p50"]), int(rx["decoded_snr_p50"]), de(rx["crc_ferr_lt1000_pct"]) + " %"),
           "Die meisten Fehlersignale liegen am Rand dessen, was SF11 überhaupt lesen kann"))
    ap_(hist([(k[-2:], float(v["rx_err"])) for k, v in per_hour[:-1]],
             f"Abb. {F['crc_h']}: CRC-Fehler je Stunde, flach über den ganzen Tag (ab 10 Uhr am 31.10., letzte angebrochene Stunde weggelassen)", ylabel="Fehler pro Stunde (Stunde der Tageszeit)"))

    ap_("<h3>Kanal, Kanalabfrage und Sendeanteil</h3><ul>"
        f"<li><b>Kanalauslastung:</b> Mittel {de(chan['mean'])} %, 95. Perzentil {de(chan['p95'], 0)} %, Maximum {de(chan['max'], 0)} % (Hälfte der Zeit bei {de(chan['median'], 0)} % oder weniger). HB9VQQ-4: {de(H4['channel']['CHANNEL_UTIL']['mean'])} %.</li>"
        f"<li><b>Sendeanteil:</b> {de(rx['tx_duty_pct'], 2)} % der Zeit (CHANNEL_UTIL: {de(rx['tx_ms_sum'] / 1000, 0)} s Sendezeit).</li>"
        f"<li><b>Kanalabfrage (CAD):</b> {de(cad['cad_scans'], 0)} Abfragen, {de(cad['cad_busy_1_initial'], 0)} zuerst „belegt“ ({de(cad['initial_busy_rate_pct'])} %), davon {de(cad['cad_busy_confirmed'], 0)} bestätigt und {de(cad['cad_false_positive'], 0)} Fehlalarm. "
        f"Beim ersten Versuch frei: {de(cad['attempt0_pct'])} %; drei oder mehr Versuche: {de(cad['attempt_ge3_pct'])} %; höchstens {cad['attempt_max']} Versuche.</li>"
        f"<li><b>Veralteter Empfangs-Interrupt (RX_IRQ_STALE):</b> {de(rx['irq_stale'], 0)} Mal ({de(rx['irq_stale'] / hours, 1)} pro Stunde); die Firmware startet den Empfang jeweils neu. Kein Paketverlust (Marker meldet Präambeltreffer nahe dem Rauschen, Vermutung nach health/findings.md).</li>"
        "</ul>")

    ap_("<h3>Stündliche Konsolenübernahme (Beobachten, Betreiberseite)</h3><ul>"
        f"<li><b>Beobachtung:</b> der Logger verliert die Verbindung {con['lost']} Mal, {con['at_full_hour']} Mal um hh:00:01 bis :02 (von 11:00 bis 10:00) und {len(con['other'])} Mal um {', '.join(con['other'])}. Jedes Mal etwa {de(con['reconnect_s']['min'], 1)} bis {de(con['reconnect_s']['max'], 1)} s bis zur Wiederverbindung.</li>"
        f"<li><b>Beleg:</b> vor allen {con['preceded_by_login']} von {con['lost']} Unterbrechungen steht eine Zeile <code>[CON ]...s_password</code>; ein zweiter Client meldet sich an der einzigen Konsolen-Sitzung an und übernimmt sie, der Knoten schließt die alte (net_console.cpp, Übernahme-Pfad). Die Firmware arbeitet wie entworfen.</li>"
        f"<li><b>Folge:</b> {de(con['reconnect_sum_s'])} s ({de(con['reconnect_share_pct'], 2)} %) des Tages fehlen im Mitschnitt. Alle Zählungen auf dieser Seite sind deshalb Untergrenzen; "
        "acht RING_TX_READ-Zeilen haben keine Schreibzeile, weil diese in einer Lücke liegt.</li>"
        "<li><b>Warum auffällig:</b> HB9VQQ-4 hat 0 Unterbrechungen, weil es über einen anderen Weg erreicht wird.</li></ul>")
    ap_(fig("console", dr.console_takeover(con["lost"], con["at_full_hour"], len(con["other"]), de(con["reconnect_s"]["min"], 1), de(con["reconnect_s"]["max"], 1),
                                           de(con["reconnect_sum_s"]), de(con["reconnect_share_pct"], 2)),
            "Ein zweiter Client übernimmt jede volle Stunde die Konsole, der Logger blendet etwa 5 s aus"))

    # ---- F2 --------------------------------------------------------------------------------
    ap_("<h2>F2 TX-Puffer mit Slots</h2>")
    ap_(f"<p><b>Der Puffer arbeitet sauber: nichts baut sich auf, nichts wird verworfen, {pct(n_air, writes)} aller Schreibungen werden gesendet.</b> "
        "Die Zahlen gelten für HB9VQQ-5, rekonstruiert aus RING_WRITE und RING_TX_READ (Abgleich über Platz und Kennung, exakt).</p>")
    ap_("<ul>"
        f"<li><b>Füllstand:</b> alle 30 s gemessen ({de(hl['status_queued']['n'], 0)} Stichproben): Mittel {de(hl['status_mean'], 2)}, Median {hl['status_queued']['p50']:.0f}, 90. Perzentil {hl['status_queued']['p90']:.0f}, 99. Perzentil {hl['status_queued']['p99']:.0f}, Maximum {hl['status_queued']['max']:.0f} von 20; "
        f"{de(hl['status_ge3_pct'], 2)} % der Zeit drei oder mehr Plätze belegt, {de(hl['status_ge5_pct'], 2)} % fünf oder mehr. Höchststand {ring['hwm_last'][1]} von 20 (MC-HWM).</li>"
        f"<li><b>Kein Aufbau:</b> 0 RING_DROP-Zeilen, 0 Überlauf; der Füllstand fällt immer auf 0 bis 1 zurück.</li>"
        f"<li><b>Zählung:</b> {de(writes, 0)} Schreibungen gesehen plus {n_unseen} Sendungen, deren Schreibzeile in einer Konsolenlücke liegt; gesamt {de(tx_total, 0)} Sendungen (RING_TX_READ).</li></ul>")
    srcrows = [
        ["Relay (fremde Rahmen weitergeleitet)", "rx_relay"], ["Server-Einspeisung (Internet zu Funk)", "udp_rx"], ["eigene HEY (automatisch)", "auto_pos"],
        ["eigene Wetterposition", "user_wx"], ["ACK weitergeleitet", "rx_ack_fwd"], ["Gateway-ACK (Paar)", "rx_dm_ack_gw"], ["ACK neu (Paar)", "rx_dm_ack_new"], ["eigener Text", "user_msg"]]
    rows = []
    for lab, key in srcrows:
        f = fate["by_src"][key]
        tot = sum(f.values())
        rows.append([lab, de(tot, 0), de(f.get("aired", 0), 0), de(f.get("superseded", 0), 0), de(f.get("vanished", 0), 0)])
    rows.append(["Summe", de(writes, 0), f"{de(n_air, 0)} ({pct(n_air, writes, 2)})", f"{de(n_sup, 0)} ({pct(n_sup, writes, 2)})", f"{de(n_van, 0)} ({pct(n_van, writes, 2)})"])
    ap_(table(["Quelle der Schreibung", "Schreibungen", "gesendet", "ersetzt", "still verschwunden"], rows, num_cols=[1, 2, 3, 4]))
    ap_("<ul>"
        f"<li><b>Ersetzen (Supersede):</b> {n_sup} Mal ersetzt eine neuere HEY oder ein neueres {{CET}} den ungesendeten Eintrag im selben Platz, dazu {ring['supersede_keep']} Mal das Gegenteil (RING_SUPERSEDE_KEEP: ein neueres {{CET}} steht schon im Puffer, die ankommende Kopie entfällt). Kein Verlust, der ersetzte Rahmen ist überholt.</li>"
        f"<li><b>Still verschwunden ({n_van}):</b> {fate['vanished_by_src'].get('rx_relay', 0)} Relay-Einträge und {fate['vanished_by_src'].get('udp_rx', 0)} Server-Text verlassen den Puffer ohne Sendezeile, ohne Verfall, ohne Ersetzen. "
        f"Bei {fate['vanished_with_foreign_copy']} von {n_van} war vorher eine fremde Kopie gehört worden (erste nach Median {de(fate['first_foreign_after_write_s']['p50'])} s). "
        f"<b>Vermutung:</b> Overhear-Cancel der NBR-Relay-Betriebsart; der Modus steht nicht im Log. Drei der {n_van} liegen neben einer Konsolenlücke und können unbeobachtet gesendet worden sein, einer davon (Server-Text) war nachweisbar gesendet.</li>"
        f"<li><b>Ersparte Sendezeit:</b> höchstens {de(fate['vanished_air_s'])} s ({pct(fate['vanished_air_s'], air_tot)} der {de(air_tot, 0)} s Sendezeit, Modell).</li>"
        f"<li><b>Trotz gehörter Kopie gesendet:</b> {fate['aired_relays_with_foreign_copy']} von {de(fate['aired_relays'], 0)} Relays ({pct(fate['aired_relays_with_foreign_copy'], fate['aired_relays'])}), {de(fate['aired_relays_with_foreign_copy_air_s'])} s = {pct(fate['aired_relays_with_foreign_copy_air_s'], air_tot)} der Sendezeit (Obergrenze der vermeidbaren Zeit, Modell).</li>"
        f"<li><b>Server-Einspeisungen:</b> {fate['udp_aired']} gesendet ({de(fate['udp_aired_air_s'], 0)} s), {fate['udp_aired_after_foreign']} ({pct(fate['udp_aired_after_foreign'], fate['udp_aired'], 0)}) nach einer gehörten fremden Kopie; sie werden nicht abgebrochen. Das passt zu „Alleinversorger“ (Case A), ist hier aber nicht beweisbar (keine [NBR]-Zeilen).</li>"
        f"<li><b>Doppelte Schreibung:</b> {dbl} Gateway-ACK-Paare (zwei Schreibungen im Abstand von Millisekunden); das ist gewollt (GWACK-01) und macht {pct(dbl, tx_total, 2)} der Sendungen aus.</li></ul>")
    ap_(fig("silent", dr.silent_cancel(n_van, fate["vanished_with_foreign_copy"], de(fate["vanished_air_s"])),
            "Relay-Einträge verlassen den Puffer ohne Logzeile, nachdem ein Nachbar dieselbe Kopie gesendet hat"))
    ap_("<h3>Wartezeit nach Klasse</h3>")
    order = [("hey_own", "eigene HEY (Prio 5)"), ("hey_relay", "Relay-HEY (Prio 5)"), ("pos_own", "eigene Wetterposition (Prio 4)"), ("pos_relay", "Relay-Position (Prio 4)"),
             ("text_relay", "Relay-Text (Prio 3)"), ("text_server", "Server-Text inkl. {CET} (Prio 3)"), ("ack", "ACK (Prio 1)")]
    ap_(table(["Klasse", "n gesendet", "Median s", "90 % unter s", "Maximum s", "Lebensdauer s"],
              [[lab, de(cls[k]["n"], 0), de(cls[k]["p50"]), de(cls[k]["p90"]), de(cls[k]["max"]), de(cls[k]["ttl_s"], 0)] for k, lab in order], num_cols=[1, 2, 3, 4, 5]))
    ap_("<ul>"
        f"<li><b>Eine Haltezeit lang:</b> Relay-Text, -Position und -HEY warten im Median {de(min(cls[k]['p50'] for k in ('text_relay', 'pos_relay', 'hey_relay')))} bis {de(max(cls[k]['p50'] for k in ('text_relay', 'pos_relay', 'hey_relay')))} s; der lange Schwanz kommt aus der strengen Rangfolge.</li>"
        f"<li><b>ACK wartet länger als HEY-Relays:</b> Median {de(cls['ack']['p50'])} s gegen {de(cls['hey_relay']['p50'])} s, obwohl ACK Priorität 1 hat (n = {cls['ack']['n']}, kleine Stichprobe). Gleiche Richtung wie bei OE3; der Mechanismus ist hier nicht neu geprüft (Vermutung).</li></ul>")

    ap_("<h3>Eigene HEY wartet länger als ein Relay-HEY (Beobachten)</h3><ul>"
        f"<li><b>Beobachtung:</b> eigene HEY Median {de(cls['hey_own']['p50'])} s (n = {cls['hey_own']['n']}, längste {de(cls['hey_own']['max'])} s), Relay-HEY {de(cls['hey_relay']['p50'])} s (n = {de(cls['hey_relay']['n'], 0)}): {ratio_txt}-fach (aus den gerundeten Medianen). Bei HB9VQQ-4 ist es mit 2,15-fach deutlich mehr.</li>"
        f"<li><b>Kein Verlust:</b> {fate['by_src']['auto_pos']['aired']} von {own_hey_written} eigenen HEY gesendet (1 durch die nächste ersetzt), alle {fate['by_src']['user_wx']['aired']} Wetterpositionen gesendet. Zum Vergleich: bei OE3XOC wurden nur 41,8 %, bei OE3XIA 78,8 % der eigenen HEY gesendet (CORE-FINDINGS OE3).</li>"
        f"<li><b>Vermutung zur Ursache:</b> dieselbe Entwurfslücke wie bei OE3 (eigene Bake am Listenkopf bekommt die 5,5-s-Haltezeit, die jeder gehörte Rahmen neu startet, ein Relay derselben Priorität eskaliert nach etwa 8 s; "
        "<code>txring_functions.cpp:385-405</code>, <code>csma_timing.h:99-111</code>, <code>lora_functions.cpp:2746-2747</code>). Für diesen Knoten nicht eigens geprüft. "
        'Herleitung bei OE3: <a href="verify/rca_own_beacons.md">verify/rca_own_beacons.md</a>.</li></ul>')
    ap_(fig("hey", dr.hey_wait(de(cls["hey_own"]["p50"]), de(cls["hey_relay"]["p50"]), ratio_txt, de(cls["hey_own"]["max"]), de(cls["hey_own"]["max_over_ttl_pct"]), fate["by_src"]["auto_pos"]["aired"], own_hey_written),
            "Die eigene HEY wartet länger als ein Relay-HEY, geht aber am Ende immer raus"))

    # ---- F3 --------------------------------------------------------------------------------
    ap_("<h2>F3 Was verlieren wir durch den Zeitverfall</h2>")
    ap_(f"<p><b>Nichts. Es gab 0 Verfälle, weder durch die Lebensdauer im Puffer (TTL) noch durch das {{CET}}-Tor.</b> "
        f"Die HEY-Klasse kam bis auf {de(cls['hey_own']['max_over_ttl_pct'])} % ihrer 180 s, sie ist die Stelle, die bei höherer Last zuerst Baken verlieren würde.</p>")
    ap_("<ul>"
        f"<li><b>Verfälle:</b> 0 Zeilen <code>RING_DROP_STALE</code> (Grund ttl oder cet), {hl['drops_ring']} Zeilen RING_DROP und RING_OVERFLOW zusammen. Das Fehlen ist belegt (der Marker wird auch ohne <code>--setlog</code> gedruckt), außer in den Konsolenlücken (zusammen {de(con['reconnect_sum_s'], 0)} s).</li>"
        f"<li><b>Zum Vergleich OE3:</b> 232 verworfene Einträge, davon 153 eigene Baken (CORE-FINDINGS OE3).</li></ul>")
    hr = [[lab, de(cls[k]["ttl_s"], 0), de(cls[k]["n"], 0), de(cls[k]["p50"]), de(cls[k]["max"]), de(cls[k]["max_over_ttl_pct"]) + " %", f"{cls[k]['ge50']} / {cls[k]['ge80']}"] for k, lab in order]
    ap_(table(["Klasse", "Lebensdauer s", "n", "Median s", "Maximum s", "Maximum / Lebensdauer", "ab 50 % / ab 80 %"], hr, num_cols=[1, 2, 3, 4, 5, 6]))
    ap_(bars([(lab.split(" (")[0], cls[k]["max_over_ttl_pct"]) for k, lab in order],
             f"Abb. {F['head']}: Längste Wartezeit in Prozent der Lebensdauer; nur die HEY-Klassen kommen in die Nähe der Grenze", unit=" %"))
    ap_("<ul>"
        f"<li><b>Reserve:</b> im ungünstigsten Fall {de(180 - cls['hey_own']['max'])} s bis zum Verfall der eigenen HEY ({de(180 - cls['hey_relay']['max'])} s bei Relay-HEY). Das war in der Lastspitze am 31.10. zwischen 18:56 und 19:02 (CORE-FINDINGS).</li>"
        f"<li><b>Was ein kürzeres TTL kosten würde (Modellrechnung auf gesendeten Einträgen):</b> bei 150 s wären {sens['150']['own']} eigene und {sens['150']['all']} HEY insgesamt verfallen, bei 120 s {sens['120']['own']} und {sens['120']['all']}, bei 90 s {sens['90']['own']} und {sens['90']['all']} (von {de(cls['hey_own']['n'] + cls['hey_relay']['n'], 0)}).</li>"
        f"<li><b>Kein festhängender Eintrag:</b> die längste Wartezeit überhaupt war {de(lat['longest_wait_s'])} s (MC-HWM wait_hwm: {de(ring['hwm_last'][4] / 1000)} s).</li></ul>")
    ap_("<h3>CSMA-Altersboden (Unterabschnitt)</h3><ul>"
        f"<li><b>Ereignisse:</b> {csma['events']} CSMA_AGED-Zeilen in {csma['episodes']} Episoden, alle Stufe 1 ({csma['stage2']} Stufe 2), Haltezeit {de(csma['hold_ms_min'], 0)} bis {de(csma['hold_ms_max'], 0)} ms. Der Altersboden verkürzt die Wartezeit von ACK, Text und Gateway-Einträgen, die schon 20 s warten.</li>"
        "<li><b>Kollisionen:</b> bei HB9VQQ-4 gab es nach einer verkürzten Wartezeit keine Häufung von CRC-Fehlern; für diesen Knoten ist die Stichprobe (4 Episoden) zu klein, der Test wurde nicht gerechnet (nicht messbar).</li>"
        "<li><b>Eigene Baken sind vom Boden ausgenommen</b> (Priorität 4 und 5, <code>csma_timing.h:101</code>); das ist der Hintergrund der längeren Wartezeit oben.</li></ul>")

    # ---- F4 --------------------------------------------------------------------------------
    ap_("<h2>F4 Was gewinnen wir durch den Zeitverfall</h2>")
    ap_("<p><b>Das lässt sich an diesem Mitschnitt nicht zeigen: es gab nichts zu verwerfen.</b> Das Verfallsnetz ist eine Absicherung, die bei dieser Last nicht gebraucht wurde.</p><ul>"
        f"<li><b>Kein Ringüberlauf-Schutz nötig:</b> höchste Füllung {hl['status_queued']['max']:.0f} (RING_STATUS) bis {H5['ring']['ring_write_queued_max']} (nach einer Schreibung) von 20; kein Eintrag überschritt seine Lebensdauer.</li>"
        f"<li><b>Frische:</b> {sens['90']['all']} von {de(cls['hey_own']['n'] + cls['hey_relay']['n'], 0)} HEY ({pct(sens['90']['all'], cls['hey_own']['n'] + cls['hey_relay']['n'])}) gingen erst nach mehr als 90 s raus, {sens['120']['all']} nach mehr als 120 s. Ob ein spätes Senden Gewinn oder Verlust ist, ist eine Grundsatzfrage; die nächste HEY-Runde erneuert den Inhalt (Vermutung).</li>"
        f"<li><b>{{CET}}:</b> die eigenen Abstrahlungen sind höchstens {de(cet['tx_age_s']['max'], 0)} s alt; das hält hier die kurze Wartezeit im Puffer ein, nicht das Tor.</li>"
        "<li><b>Ohne-TTL-Rechnung:</b> nicht möglich. Sie braucht [LOG]-Zeilen; bei HB9VQQ-4 ist sie ungültig (kein Abbruchweg im Rechenmodell); nicht messbar.</li></ul>")

    # ---- F5 --------------------------------------------------------------------------------
    ap_("<h2>F5 Zeitsignal</h2>")
    ap_(f"<p><b>Das Zeitsignal ist in Ordnung: lückenlos von OE1XAR-33, nie älter als {de(cet['tx_age_s']['max'], 0)} s auf der Luft, kein veraltetes {{CET}} gesendet.</b> "
        "Auffällig sind zwei Dinge außerhalb des Knotens: HB9HNO-49 strahlt alles verspätet ab, und HB9VQQ-4 sendet dieselben Telegramme noch einmal.</p><ul>"
        f"<li><b>Quelle:</b> {cet['gw_rx_by_src']['OE1XAR-33']} Telegramme von OE1XAR-33 per UDP (Gateway-Meldung), Abstand {de(cet['gw_rx_gap_s']['min'], 1)} bis {de(cet['gw_rx_gap_s']['max'], 1)} s, Median {de(cet['gw_rx_gap_s']['p50'], 1)} s; keine Lücke über 611 s.</li>"
        f"<li><b>Nur über Funk:</b> OE1XAR-63 ({cet['rf_rx_by_src']['OE1XAR-63']} neu angenommene Meldungen) und OE1XAR-28 ({cet['rf_rx_by_src']['OE1XAR-28']}), eingespeist von fremden Gateways. OE1XAR-62: {hl['oe1xar62_lines']} Zeilen im Mitschnitt, nie gesehen.</li>"
        f"<li><b>Eigene Abstrahlung:</b> {cet['tx_n']} Mal; Alter auf der Luft Median {de(cet['tx_age_s']['p50'], 0)} s, 90 % unter {de(cet['tx_age_s']['p90'], 0)} s, 99 % unter {de(cet['tx_age_s']['p99'], 0)} s, Maximum {de(cet['tx_age_s']['max'], 0)} s. "
        f"{cet['tx_gt150']} über 150 s, {cet['drop_lines']} Zeilen <code>[CET];drop</code>. Die Wartezeit im Puffer (Median {de(cet['tx_wait_s']['p50'])} s, Maximum {de(cet['tx_wait_s']['max'])} s) macht fast das ganze Alter aus.</li>"
        "<li><b>Zeitquelle des Tors:</b> alle Zeilen tragen <code>trusted;1</code> (eigene Uhrquelle); das Telegramm kann die Uhr nicht verstellen.</li>"
        f"<li><b>Alter der über Funk angenommenen Telegramme:</b> höchstens {cet['rf_rx_age_max']} s, {cet['rf_rx_gt150']} über 150 s.</li></ul>")
    ap_("<h3>Veraltete Kopien von HB9HNO-49 (Beobachten, Betreiberseite)</h3><ul>"
        f"<li><b>Beobachtung:</b> HB9HNO-49 ({hno_m['hardware'].title()}, Firmware {hno_m['firmware']}, Gateway, {hno_m['altitudeM']} m, {de(float(dist['HB9HNO-49']))} km entfernt) strahlt {stale['stale']} verschiedene {{CET}}-Telegramme mit {de(stale['stale_age_min'], 0)} bis {de(stale['stale_age_max'], 0)} s Alter ab (Median {de(stale['stale_age_p50'], 0)} s, rund 25 min); "
        f"es ist der letzte Hop bei {stale['stale_last_hops'].get('HB9HNO-49', 0)} von {stale['stale']}. Kein anderer Sender strahlt Telegramme über 150 s Alter ab.</li>"
        f"<li><b>Nicht nur {{CET}}:</b> alle {hno['dup_copies']} Kopien bereits bekannter Rahmen von HB9HNO-49 trafen mehr als 300 s nach der ersten Kopie ein ({hno['frames_last_hop']} Rahmen mit letztem Hop HB9HNO-49). Die Firmware 4.35h kennt weder die TX-Pool-Alterung noch das {{CET}}-Tor (Vermutung nach Firmware-Stand).</li>"
        f"<li><b>Wirkung auf HB9VQQ-5:</b> alle {stale['stale']} veralteten {{CET}} endeten als Duplikat ({stale['stale_accepted_new']} angenommen), das Tor musste nicht eingreifen. "
        f"Späte HEY- und Positionskopien nahm der Knoten {hno['late_accepted_new_total']} Mal als neu an ({hno['late_accepted_new'].get('hey', 0)} HEY, {hno['late_accepted_new'].get('pos', 0)} Position, 0 Text) und gab {hno['late_relayed_total']} davon weiter. "
        "Das ist Entwurf: für HEY und Position gibt es kein Alterstor am Ursprung.</li>"
        "<li><b>Kein Firmware-Fehler:</b> eine Spätkopie wird nur angenommen, wenn der Knoten den Rahmen vorher nicht gehört hatte oder sein Dedup-Ring ihn vergessen hat; der Ring dreht sich bei HB9VQQ-5 in etwa "
        f"{de(dedup_min, 0)} min (100 Plätze bei {de(H5['ring']['dedup']['add'] / hours, 0)} Einträgen pro Stunde).</li></ul>")
    ap_(fig("hno", dr.hno_late(hno["late_accepted_new_total"], hno["late_accepted_new"].get("hey", 0), hno["late_accepted_new"].get("pos", 0), hno["late_relayed_total"], stale["stale"], 25),
            "HB9HNO-49 gibt jeden Rahmen etwa 25 Minuten zu spät ab; das {CET}-Tor musste nicht eingreifen, späte HEY und Positionen wurden teils weitergegeben"))
    ap_("<h3>Doppelabstrahlung mit HB9VQQ-4 (Beobachten)</h3><ul>"
        f"<li><b>Beobachtung:</b> in {cp['pairs']} Fällen sendeten beide Knoten dasselbe {{CET}}-Telegramm; in {cp['later_had_decoded_earlier']} ({de(cp['later_had_decoded_earlier_pct'], 0)} %) hatte der spätere Knoten die Kopie des früheren schon dekodiert.</li>"
        "<li><b>Einordnung:</b> Server-Einspeisungen werden nie abgebrochen (Alleinversorger, Case A, Entwurfslücke wie bei OE3, <code>src/gw_udp_ring.h:44-48</code>); nicht beweisbar ohne [NBR]-Zeilen. Obergrenze, weil die Knoten nur 5 gemeinsame direkte Nachbarn haben.</li></ul>")

    # ---- F6 --------------------------------------------------------------------------------
    ap_("<h2>F6 Manipulationen dieses Knotens</h2>")
    ap_("<p><b>Was der Knoten mit Rahmen anstellt (weiterleiten, unterdrücken, ersetzen, verwerfen) und welche Wirkung das hat:</b></p>")
    pos_l = [
        f"<li><b>Weiterleiten:</b> {de(rel_w.get('aired', 0) + rel_w.get('superseded', 0) + rel_w.get('vanished', 0), 0)} Relays eingereiht, {de(rel_w.get('aired', 0), 0)} gesendet; HB9VQQ-5 hört mit DO1FE-19 und HB9CMI-40 zwei Nachbarn, die HB9VQQ-4 nicht hört.</li>",
        f"<li><b>Dedup-Ring:</b> {de(rx['dup'], 0)} Duplikate verworfen, {de(rx['accepted_new'], 0)} Rahmen neu angenommen.</li>",
        f"<li><b>Ersetzen:</b> {n_sup} überholte Einträge durch neuere ersetzt, {ring['supersede_keep']} ankommende {{CET}}-Kopien entfallen, weil ein neueres schon wartet.</li>",
        f"<li><b>Overhear-Cancel (Vermutung):</b> {n_van} Einträge verschwinden still, {fate['vanished_with_foreign_copy']} mit vorher gehörter Kopie; höchstens {de(fate['vanished_air_s'])} s Sendezeit gespart.</li>",
        f"<li><b>Keine Schleifen:</b> {H5['ring']['relay_loop_blocked']} Schleifensperren nötig, Hop-Summen konstant (CORE-FINDINGS).</li>",
        f"<li><b>Upload ans Internet:</b> {de(hl['tx_udp'], 0)} Rahmen (TX-UDP), keine Funkkosten.</li>",
    ]
    neg_l = [
        f"<li><b>Doppelsendungen mit HB9VQQ-4:</b> {de(pair['distinct_ids_in_pairs'], 0)} Kennungen von beiden gesendet ({de(pair['relay_relay_pairs'], 0)} Relay-Paare, {pair['udp_udp_pairs']} Server-Paare); die spätere Kopie macht {de(pair['later_copy_air_pct'])} % der Paar-Sendezeit aus (Obergrenze).</li>",
        f"<li><b>Relay nach gehörter Kopie:</b> {fate['aired_relays_with_foreign_copy']} von {de(fate['aired_relays'], 0)} ({pct(fate['aired_relays_with_foreign_copy'], fate['aired_relays'])}), {de(fate['aired_relays_with_foreign_copy_air_s'])} s.</li>",
        f"<li><b>Server-Einspeisung nach gehörter Kopie:</b> {fate['udp_aired_after_foreign']} von {fate['udp_aired']} ({pct(fate['udp_aired_after_foreign'], fate['udp_aired'], 0)}), nie abgebrochen.</li>",
        f"<li><b>Spätkopien von HB9HNO-49:</b> {hno['late_relayed_total']} weitergegeben (Mindestwert; Ursache beim Betreiber von HB9HNO-49).</li>",
        f"<li><b>Eigene HEY wartet {ratio_txt}-fach länger</b> als ein Relay-HEY, ohne Verlust.</li>",
    ]
    neu_l = [
        f"<li><b>Gateway-ACK-Paare:</b> {dbl} gewollte Doppelschreibungen (GWACK-01), {pct(dbl, tx_total, 2)} der Sendungen.</li>",
        f"<li><b>ACK und Ping:</b> {ack_tx} ACK-Sendungen = {pct(ack_tx, tx_total)} der Sendungen; {H5['health']['ping_send_lines']} eigene Pings (bei OE3 waren es 41 % der Sendungen).</li>",
        f"<li><b>Server-Einspeisung:</b> {sum(udp_w.values())} Einträge ins Funknetz; {cet['gw_rx_by_src']['OE1XAR-33']} {{CET}}-Telegramme kamen per UDP an.</li>",
        f"<li><b>Trickle der eigenen HEY:</b> SENDEN {hl['trickle']['SEND']}, UNTERDRÜCKT {hl['trickle']['SUPPRESS']}, Nachbarwechsel {hl['trickle']['TOPO_CHANGE']}. Die Gateway-Regel erzwingt das Senden, wenn die letzte HEY älter als das Höchstintervall ist.</li>",
        f"<li><b>Fremde Kurzrahmen:</b> {H5['ring']['ack_reject']} Mal ein 12-Byte-Rahmen ohne Prüfsumme (<code>ACK_REJECT</code>), auch bei HB9VQQ-4 zur selben Zeit; korrekt verworfen.</li>",
    ]
    ap_('<div class="grid3">'
        f'<div class="card pos"><h3>{chip("pos")}</h3><ul>{"".join(pos_l)}</ul></div>'
        f'<div class="card neg"><h3>{chip("neg")}</h3><ul>{"".join(neg_l)}</ul></div>'
        f'<div class="card neu"><h3>{chip("neu")}</h3><ul>{"".join(neu_l)}</ul></div></div>')
    ap_("<h3>Zwei Gateways in Hörweite (Beobachten)</h3><ul>"
        f"<li><b>Hörbarkeit:</b> im gemeinsamen Zeitfenster dekodiert HB9VQQ-5 {pct(mutual['hb9vqq5_hears_4']['hit'], mutual['hb9vqq5_hears_4']['n'])} der Sendungen von HB9VQQ-4 ({de(mutual['hb9vqq5_hears_4']['hit'], 0)} von {de(mutual['hb9vqq5_hears_4']['n'], 0)}), "
        f"HB9VQQ-4 nur {pct(mutual['hb9vqq4_hears_5']['hit'], mutual['hb9vqq4_hears_5']['n'])} der Sendungen von HB9VQQ-5 ({de(mutual['hb9vqq4_hears_5']['hit'], 0)} von {de(mutual['hb9vqq4_hears_5']['n'], 0)}). Die Strecke ist {dist['HB9VQQ-4'].replace('.', ',')} km lang; RSSI-Median bei HB9VQQ-5 {nb_c['HB9VQQ-4']['rssi_p50']} dBm.</li>"
        f"<li><b>Doppelte Sendungen:</b> {de(pair['ids_aired_by_any'], 0)} Kennungen wurden von mindestens einem Knoten gesendet, {de(pair['distinct_ids_in_pairs'], 0)} von beiden ({de(pair['pairs'], 0)} Paare innerhalb von 30 min, Abstand Median {de(pair['gap_s']['p50'])} s). "
        f"Die späteren Kopien benötigen {de(pair['later_copy_air_s'], 0)} s von {de(pair['pair_air_s'], 0)} s Paar-Sendezeit = {de(pair['later_copy_air_pct'])} %.</li>"
        "<li><b>Obergrenze, nicht Verlust:</b> beide Knoten erreichen unterschiedliche Gebiete (nur 5 gemeinsame direkte Nachbarn); die zweite Kopie hat deshalb oft Hörer, die die erste nicht erreicht. "
        "Ob sie überflüssig war, entscheiden erst die [NBR]-Zeilen (Empfehlung oben).</li></ul>")
    ap_(fig("red", dr.redundancy(de(pair["distinct_ids_in_pairs"], 0), de(pair["later_copy_air_pct"]), de(mutual["hb9vqq5_hears_4"]["pct"]), de(mutual["hb9vqq4_hears_5"]["pct"]), de(pair["relay_relay_pairs"], 0)),
            "Beide Gateways geben dieselben Rahmen weiter; die Nachbarn hören sie doppelt"))

    # ---- Verdachtsfaelle -------------------------------------------------------------------
    ap_("<h2>Verdachtsfälle Firmware</h2>")
    ap_('<div class="callout ok"><p><b>Es wurde kein Firmware-Fehler gefunden.</b> ' + chip("ok", "kein Fehler")
        + "</p><p>Gesucht wurde in Puffer, Zeitverfall, {CET}-Tor, Weiterleitung, Dedup und Konsole. Was bleibt, sind zwei kosmetische Logtexte und eine Beobachtungslücke.</p></div>")
    ap_('<div class="callout watch"><p><b>V1 Beobachtungslücke: stiller Abbruch ohne Zähler</b> ' + chip("watch", "Beobachten") + "</p><ul>"
        f"<li><b>Beobachtung:</b> {n_van} Relay-Einträge ({pct(n_van, writes)} der Schreibungen) verlassen den Puffer ohne Logzeile (bei HB9VQQ-4 sind es 12,7 %).</li>"
        "<li><b>Beleg:</b> der Abbruch gibt den Platz zuerst frei und druckt nur, wenn die NBR-Protokollierung läuft; ein Zähler (<code>stat_nbr_cancel</code>) steht in keiner Statistikzeile.</li>"
        "<li><b>Code-Stellen:</b> <code>src/lora_functions.cpp:1445</code> (Freigabe), <code>:1446</code> (Zähler), <code>:1463</code> (Zeile nur mit <code>nbrLog</code>).</li>"
        "<li><b>Vorschlag (ungetestet):</b> den Zähler in die Zeile [MC-STAT] aufnehmen.</li>"
        f"<li><b>Zeichnung:</b> Abb. {F['silent']}.</li><li><b>Status:</b> Verdacht (Beobachtbarkeit), kein Datenverlust.</li></ul></div>")
    ap_('<div class="callout watch"><p><b>V2 Logtext „consistent=17&lt;k=2“ bei erzwungenem Senden</b> ' + chip("ok", "bestätigt (Code gelesen, kosmetisch)") + "</p><ul>"
        f"<li><b>Beobachtung:</b> {hl['trickle_send_forced']} von {hl['trickle']['SEND']} SEND-Zeilen des Trickle zeigen einen Wert, der nicht kleiner als k ist.</li>"
        "<li><b>Beleg:</b> der Zweig, der nach der Gateway-Regel sendet, druckt immer den Text „&lt;k“, unabhängig vom Grund.</li>"
        "<li><b>Code-Stelle:</b> <code>src/esp32/esp32_main.cpp:4815-4826</code> (Ausgabe in Zeile 4825).</li>"
        "<li><b>Wirkung:</b> nur der Text, das Verhalten stimmt. Vorschlag: den Grund drucken (<code>gateway_imax</code>).</li>"
        f"<li><b>Zeichnung:</b> Abb. {F['trickle']}.</li></ul></div>")
    ap_(fig("trickle", dr.trickle_text(hl["trickle_send_forced"], hl["trickle"]["SEND"]), "Bei erzwungenem Senden druckt die Firmware einen Grund, der nicht zutrifft"))
    ap_('<div class="callout"><p><b>V3 Logtext „RLY q=full“ bei RING_SUPERSEDE_KEEP</b> ' + chip("neu", "bei diesem Knoten nicht sichtbar") + "</p><ul>"
        "<li><b>Einordnung:</b> der Text erscheint nur in den [LOG]-Weiterleitungszeilen. Bei HB9VQQ-4 wurde er dreimal beobachtet (<code>src/lora_functions.cpp:2651</code>, <code>src/txring_functions.cpp:1035</code>); "
        f"bei diesem Knoten gibt es sie nicht, obwohl {ring['supersede_keep']} Mal RING_SUPERSEDE_KEEP vorkam. nicht messbar.</li></ul></div>")

    # ---- Nachbarn --------------------------------------------------------------------------
    ap_("<h2>Nachbarn und Hardware-Inventar</h2>")
    ap_("<p>RSSI (Empfangspegel) und SNR (Signal-Rausch-Abstand) gelten nur für direkte Nachbarn, also den letzten Hop. Die Stammdaten stammen aus mcmap; was dort fehlt, steht als „keine Stammdaten“.</p>")

    def place(m: dict[str, str] | None) -> str:
        if not m:
            return "keine Stammdaten"
        txt_ = [t for t in (m.get("name", ""), m.get("description", "")) if t and t != m["call"]]
        uniq: list[str] = []
        for t in txt_:
            if t not in uniq:
                uniq.append(t)
        coords = f"{float(m['lat']):.4f} N / {float(m['lon']):.4f} E".replace(".", ",")
        return (", ".join(uniq) + f" ({coords})") if uniq else coords

    def hw_row(call: str, n: str = "nicht direkt gehört", rs: str = "nicht direkt gehört") -> list[str]:
        m = master.get(call)
        d = dist.get(call, "")
        return [call, m["hardware"] if m else "keine Stammdaten", fw_text(m["firmware"]) if m else "unbekannt", place(m),
                (m["altitudeM"] + " m") if m and m.get("altitudeM") else "unbekannt", (de(float(d)) + " km") if d else "unbekannt", n, rs]

    heads = ["Rufzeichen", "Hardware", "Firmware", "Standort", "Höhe", "Entfernung", "Rahmen (letzter Hop)", "RSSI / SNR (Median)"]
    nrows = []
    for t in rx["neighbours"]:
        rs = f"{t['rssi_p50']} dBm / {t['snr_p50']} dB" if t["rssi_p50"] is not None else "–"
        nrows.append(hw_row(t["call"], de(t["n"], 0), rs))
    ap_(table(heads, nrows, num_cols=[6]))
    ap_(f"<p class=\"small\">{len(rx['neighbours'])} direkte Nachbarn (letzter Hop) in 24 Stunden. Bei den schwachen Strecken (SNR unter -14 dB) liegt der RSSI am Rauschteppich und sagt wenig; die Rahmenzahl ist hier die Aussage.</p>")
    ap_("<details><summary>Weitere genannte Stationen (Zeitserver, Einspeiser, Relais)</summary>")
    others = ["HB9CF-40", "HB9JAT", "DL1GWN-12", "HB9LEK-12"]
    erows = [hw_row(c) for c in others]
    for c in ("OE1XAR-33", "OE1XAR-63", "OE1XAR-28"):
        erows.append([c, "Zeitserver (keine Stammdaten)", "unbekannt", "keine Stammdaten", "unbekannt", "unbekannt", "nicht direkt gehört", "nicht direkt gehört"])
    ap_(table(heads, erows, num_cols=[6]))
    ap_("<p class=\"small\">HB9CF-40 und DL1GWN-12 speisen OE1XAR-63 und OE1XAR-28 ein, DO1FE-19 ist oben der Haupt-Einspeiser. HB9LEK-12 sendet auf einer um 4,5 kHz verschobenen Frequenz und wird nur von HB9VQQ-4 gehört (siehe die Seite von HB9VQQ-4). "
        f"HB9JAT erzeugt wiederkehrende Nachrichten-Kennungen. HB9VQQ-5 hat {len(rx['neighbours'])} direkte Nachbarn; mit HB9VQQ-4 gemeinsam sind nur 5.</p>")
    ap_("</details>")

    # ---- Grenzen ---------------------------------------------------------------------------
    ap_("<h2>Was nicht geprüft ist / Grenzen</h2><ul>"
        "<li><b>Kein <code>--setlog</code>:</b> alle Größen aus dem Kasten oben als „nicht messbar“ markiert; Weiterleitungsgründe, Empfangs-Flags und die Firmware der Nachbarn fehlen.</li>"
        "<li><b>Kein <code>--nbrdebug</code>:</b> ob Overhear-Cancel und <code>--nbrrelay on</code> aktiv sind und ob die Server-Einspeisung Case A hat, ist ein Rückschluss, kein Beleg.</li>"
        f"<li><b>Konsolenlücken:</b> {con['lost']} mal etwa 5 s; alle Zählungen sind Untergrenzen, drei der {n_van} verschwundenen Einträge sind deshalb mehrdeutig.</li>"
        "<li><b>Build:</b> genauer Firmware-Stand unbekannt (keine STAT-Zeile); Code-Stellen am Stand fork-dev 651e0920 gelesen (<code>git diff 651e0920 HEAD -- src</code> ist leer).</li>"
        "<li><b>Sendezeit:</b> aus Rahmenlängen und SF11/BW250/CR4:6 gerechnet (Modell); passt auf 0,6 % zu den Summen des Knotens.</li>"
        "<li><b>Ursache der CRC-Fehler:</b> Vermutung (ferne Stationen). CRC-Rahmen tragen keinen Absender.</li>"
        "<li><b>Ohne-TTL-Rechnung und Kollisionstest des Altersbodens:</b> für diesen Knoten nicht gerechnet.</li>"
        "<li><b>Retry-Leiter</b> lief nie; bleibt ungetestet.</li>"
        "<li><b>Nur 24 Stunden</b> eines Samstags (31.10.2026), kein Wochenvergleich. Getrennt vom OE3-Netz betrachtet; es gibt keine Verknüpfung mit OE3-Knoten.</li></ul>")

    ap_("<h2>Offene Fragen an den Betreiber</h2><ol>"
        "<li>Können Sie den nächsten 24-h-Mitschnitt mit <code>--setlog on</code> fahren, und auf beiden Knoten einmal ein bis zwei Stunden mit <code>--nbrdebug on</code>?</li>"
        "<li>Welcher Client meldet sich jede volle Stunde an der Konsole von HB9VQQ-5 an? Kann er einen anderen Weg nehmen, wie HB9VQQ-4 ihn hat?</li>"
        "<li>Ist <code>--nbrrelay</code> auf beiden Knoten eingeschaltet? Der Info-Befehl zeigt die Betriebsart und den Zähler „cancelled N“.</li>"
        "<li>Ist es gewollt, dass beide Gateways dieselben Server-Texte und {CET}-Telegramme einspeisen, oder soll eines davon die Einspeisung für das gemeinsame Gebiet übernehmen?</li>"
        "<li>Kennen Sie den Betreiber von HB9HNO-49 (Firmware 4.35h, sendet alles rund 25 Minuten zu spät)?</li>"
        "<li>Welche Antenne und Ausrichtung ist auf dem Tanzboden montiert? Das hilft, die Vermutung „ferne Stationen“ hinter den CRC-Fehlern zu prüfen.</li></ol>")

    # ---- dev details -----------------------------------------------------------------------
    ap_('<details class="dev"><summary>Methodik, Marker, Code-Stellen, Skripte (für Entwickler)</summary>')
    ap_("<h3>Definitionen</h3><ul>"
        "<li><b>Empfangsereignis:</b> <code>ONRXDONE_TIME</code> (dekodierter Rahmen, auch die 12-Byte-ACK) oder <code>CRC_ERROR</code>; Fehleranteil = CRC_ERROR / (CRC_ERROR + ONRXDONE_TIME). Der Anteil 52,2 % weicht von früheren Zwischenwerten (52,7 %) ab, die einen anderen Nenner verwendeten (verify/hb9.md Nr. 4a).</li>"
        "<li><b>Empfangspegel je Rahmen:</b> die Zeile <code>[LoRa]...Received packet: RSSI / SNR / Frequency error</code> unmittelbar vor der Zeile <code>MH-LoRa</code>; der Frequenzfehler gehört dem letzten Hop.</li>"
        "<li><b>Schreibung und Verbleib:</b> <code>RING_WRITE slot= msg_id= src=</code> gegen <code>RING_TX_READ slot= msg_id= lat=</code> (gesendet) und <code>RING_SUPERSEDE</code> in den drei Folgezeilen (ersetzt); alles andere nach neuem Eintrag im Platz oder 400 s vor Ende „still verschwunden“.</li>"
        "<li><b>Fremde Kopie:</b> <code>MH-LoRa</code> derselben Kennung mit mindestens zwei Pfad-Elementen und letztem Hop ungleich dem eigenen Rufzeichen, zwischen Schreibung und Ende des Platzes.</li>"
        "<li><b>Paarbildung:</b> RING_TX_READ beider Knoten, gleiche Kennung, Abstand höchstens 30 min, eins zu eins; Sendezeit der späteren Kopie gegen die Summe aller Sendungen der Paare. Hörbarkeit: letzter Hop gleich Absender, -1 bis +3,5 s um die Sendung, nur im gemeinsamen Zeitfenster.</li>"
        "<li><b>HB9HNO-49, Spätkopie:</b> erste Kopie bei HB9VQQ-5 mit letztem Hop HB9HNO-49, dieselbe Kennung 300 bis 3.600 s vorher bei HB9VQQ-4 (Funk oder Sendung) gesehen, als neu angenommen (<code>RX-LoRa2</code> innerhalb 1 s), weitergegeben = eigene <code>TX-LoRa</code> binnen 400 s. Die Obergrenze 3.600 s schließt die Kennungs-Wiederverwendung von HB9JAT aus.</li>"
        "<li><b>Alter einer {CET}-Kopie:</b> Zeit des Mitschnitt-Rechners minus 3.600 s minus Inhalt (UTC). Für eigene Sendungen: <code>[CET];tx age_s</code> des Knotens.</li>"
        "<li><b>Lebensdauer-Klassen:</b> <code>src/txring_ttl.h</code>: Prio 5 HEY 180 s, Prio 4 Position 120 s, Prio 3 Text 300 s, Prio 1 ACK 90 s; das {CET}-Tor 150 s (<code>src/cet_age.h</code>).</li>"
        "<li><b>Vermutungen:</b> Overhear-Cancel als Ursache der still verschwundenen Einträge; Entfernung der Sender hinter den CRC-Fehlern; Firmware 4.35h ohne Alterstor bei HB9HNO-49; gleicher Mechanismus der Bakenwartezeit wie bei OE3.</li></ul>")
    ap_("<h3>Marker</h3><ul>"
        "<li><code>[MC-DBG] CRC_ERROR rssi= snr= freq_err= size=</code>, <code>ONRXDONE_TIME ms=</code>, <code>OnRxDone</code>, <code>RX_IRQ_STALE rssi= flags= cnt=</code>, <code>CHANNEL_UTIL rx= tx= util=</code>.</li>"
        "<li><code>RING_WRITE slot= type= status= len= msg_id= queued=/20 src=</code>, <code>RING_TX_READ slot= prio= ... lat=</code>, <code>RING_STATUS queued=</code>, <code>RING_SUPERSEDE(_KEEP)</code>, <code>RING_DROP_STALE</code>, <code>CSMA_AGED slot= age= stage= hold=</code>, <code>[MC-HWM] uptime= queue_hwm= aged1= wait_hwm=</code>.</li>"
        "<li><code>[CET];gw_rx;src;..;age_s</code>, <code>[CET];rx</code>, <code>[CET];tx;age_s;..;trusted;..;wait_ms;..;prio</code>.</li>"
        "<li><code>[LOGGER] connection lost</code> und <code>[CON ]...s_password</code> (Konsolenübernahme), <code>[MC-TRICKLE] SEND/SUPPRESS/TOPO_CHANGE</code>.</li></ul>")
    ap_("<h3>Code-Stellen (am Stand fork-dev 651e0920 gelesen)</h3><ul>"
        "<li>Overhear-Cancel: <code>src/lora_functions.cpp:1319-1470</code> (Freigabe :1445, Zähler :1446, Zeile :1463 nur mit nbrLog); nbrLog nur mit <code>--nbrdebug</code>: <code>:688-694</code>.</li>"
        "<li>Konsolen-Übernahme: <code>src/net_console.cpp:183-232</code>.</li>"
        "<li>TX-Auswahl und Verfall: <code>src/txring_functions.cpp:213-255, 385-405, 566-645, 1007</code>, <code>src/txring_ttl.h:46-64</code>, <code>src/csma_timing.h:71-112</code>; Neustart der Haltezeit <code>src/lora_functions.cpp:2746-2747</code>.</li>"
        "<li>{CET}-Tor: <code>src/cet_age.h:120-135</code>; Server-Einspeisung: <code>src/gw_udp_ring.h:37-48</code>.</li>"
        "<li>Trickle-Text: <code>src/esp32/esp32_main.cpp:4815-4826</code>.</li></ul>")
    ap_("<h3>Quellen</h3><ul>"
        "<li>hb9/health/results.json, findings.md (Knoten hb9vqq5: rf, channel, cad, ring, gateway, onrxdone, rx_silence); Ausfälle der Standard-Extraktion für diesen Mitschnitt (zum Beispiel own_hey.aired_pct 0,0, tx_noair_other, leere Verkehrs-, Nachbar- und Firmware-Blöcke) werden nicht verwendet.</li>"
        "<li>hb9/q2/results.json (Doppelschreibungen), hb9/q2, q34, q5, q6 findings.md (Vergleichswerte, Urteilstexte), hb9/CORE-FINDINGS.md, verify/hb9.md (verifizierte Zahlen: 92,1 / 64,9 %, 42,1 %, 32 / 14 Spätkopien, 1,47-fach), verify/rca_own_beacons.md (OE3).</li>"
        "<li>hb9/master/master.tsv, distances.tsv (Spalte d_hb9vqq5), coverage.txt.</li>"
        "<li>extra/hb9vqq5_measure.json: Rohlog-Messungen, erzeugt aus den normalisierten Logs hb9vqq5.log und hb9vqq4.log mit <code>generators/lib/hb9vqq5_measure.py --norm &lt;norm-Verzeichnis&gt; --out &lt;Datei&gt;</code>. "
        "Enthalten: Empfang und Nachbarn, CRC-Statistik, Schreibung und Verbleib, Wartezeit je Klasse, CSMA_AGED, {CET}-Alter, veraltete Kopien, HB9HNO-49, Paarbildung mit HB9VQQ-4, Konsolenübernahme, Laufzeit und Heap.</li>"
        "<li>Skripte: <code>docs/log-reports/2026-11-01-24h/generators/hb9vqq5.py</code> (dieses Programm; liest keine Rohlogs), <code>generators/lib/hb9vqq5_draw.py</code> (Zeichnungen).</li></ul>")
    ap_("</details>")

    ap_('<div class="foot"><p>Quellen: Mitschnitte 31.10./01.11.2026, Auswertung vom 01.11.2026. Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware. '
        '<a href="uebersicht.html">Zur Übersicht (Netzsicht)</a> · <a href="hb9vqq4.html">Schwesterknoten HB9VQQ-4</a></p></div>')

    html = page("HB9VQQ-5 · MeshCom Log-Auswertung", "\n".join(body))
    html = html.replace("src=", "src&#61;")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(html, encoding="utf-8")
    print(f"wrote {args.out} ({len(html)} bytes, {len(F)} figures)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
