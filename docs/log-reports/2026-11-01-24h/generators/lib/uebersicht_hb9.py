"""Swiss mesh (HB9VQQ-4 / HB9VQQ-5) parts of the overview page: matrix rows, section, table rows.

Numbers come from <data>/hb9/*/results.json where those are correct and from <data>/extra/uebersicht_hb9.json
(verified numbers, each block names its source). The two are cross-checked at render time.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

import uebersicht_draw as D
from uebersicht_head import HB9_NODES, Data, chip, de, ol, tab, ul

NM = "nicht messbar"
Xfn = Callable[[str, D.Drawing, str], str]


def _pct(a: float, b: float, nd: int = 1) -> str:
    return de(100.0 * a / b, nd)


class Hb9:
    """Resolved HB9 numbers; asserts that results.json and the verified extra file agree."""

    def __init__(self, d: Data) -> None:
        x = d.hb9x
        q2, q34, q6, hl = d.hb9["q2"], d.hb9["q34"], d.hb9["q6"], d.hb9["health"]
        self.x = x
        self.m = d.hb9_master
        self.util = hl["network"]["chan_util_mean_pct"]
        self.noise = hl["network"]["noise_carpet_p5_dbm"]
        f4, f5 = q2["nodes"]["hb9vqq4"]["fate"], q2["nodes"]["hb9vqq5"]["fate"]
        self.tx4 = q6["network"]["ping_share"]["total_tx"]
        self.ping_frames = q6["network"]["ping_share"]["ping_tx"]["ping_orig_or_relay"]
        self.ping_air = q6["network"]["ping_share"]["ping_air_s"]
        self.drops = q34["network"]["drops_total"]
        self.drops5 = q34["network"]["hb9vqq5"]["ring_drop_stale"]
        ob = q34["network"]["own_beacons"]
        self.hey = ob["hb9vqq4/own_HEY"]
        self.wx = ob["hb9vqq4/own_WX_POS"]
        self.cet4 = q6["nodes"]["hb9vqq4"]["cet"]
        self.writes4, self.writes5 = f4["total_writes"], f5["total_writes"]
        self.van4 = f4["counts"]["overwritten_unexplained"]
        self.van5 = f5["counts"]["overwritten_unexplained"]
        p, c, t, f6 = x["pool"], x["decay"], x["time"], x["f6"]
        assert self.writes4 == p["writes_4"] and self.writes5 == p["writes_5"]
        assert self.van4 == p["vanished_4"] and self.van5 == p["vanished_5"]
        assert self.tx4 == f4["counts"]["tx_air"]
        assert self.hey["outcomes"]["sent"] == c["own_hey_written_4"] - 1 and self.hey["written"] == c["own_hey_written_4"]
        assert self.cet4["tx_age_max_s"] == t["own_cet_age_max_4_s"] and self.cet4["cet_tx"] == t["own_cet_tx_4"]
        assert self.drops == 0 and self.drops5 == 0 and self.ping_frames == f6["manual_ping_frames_4"]
        assert c["own_hey_sent_5"] == c["own_hey_written_5"] - 1


def _place(m: dict[str, dict[str, str]], call: str) -> str:
    r = m[call]
    return f"{r['description']} ({r['altitudeM']} m)"


def matrix_rows(d: Data) -> list[list[Any]]:
    h = Hb9(d)
    c = h.x["decay"]
    rows: list[list[Any]] = []
    for key, call in HB9_NODES:
        r = h.m[call]
        link = f'<a href="{key}.html">{call}</a>'
        if key == "hb9vqq4":
            hey = f"100 % ({h.hey['outcomes']['sent']} von {h.hey['outcomes']['sent']}; 1 ersetzt)"
            wx = f"0 von {h.wx['written']}"
            cancel = f"0 Zeilen ({h.van4} stille Freigaben)"
            drops = h.drops
        else:
            hey = f"100 % ({c['own_hey_sent_5']} von {c['own_hey_sent_5']}; 1 ersetzt)"
            wx = f"0 von {c['own_wx_written_5']}"
            cancel = NM
            drops = h.drops5
        rows.append([link, r["hardware"], _place(h.m, call), de(h.util[key]), de(h.noise[key], 0),
                     hey, wx, drops, cancel])
    return rows


def verdict_rows() -> list[list[str]]:
    v = {"hb9vqq4": ["ok", "ok", "watch", "neu", "watch", "watch"],
         "hb9vqq5": ["watch", "ok", "ok", "neu", "watch", "watch"]}
    return [[f'<a href="{k}.html">{call}</a>'] + [chip(c) for c in v[k]] for k, call in HB9_NODES]


def f6_ack_pct(d: Data) -> str:
    """ACK-relay share of HB9VQQ-4 TX count, formatted."""
    h = Hb9(d)
    return _pct(h.x["f6"]["ack_tx_4"], h.tx4)


def index_row(d: Data) -> list[Any]:
    h = Hb9(d)
    f6 = h.x["f6"]
    return ["Schweizer Netz (HB9VQQ-4/-5)",
            "Stabil, derselbe Code ohne die OE3-Lastprobleme: ACK-Last "
            f"{_pct(f6['ack_tx_4'], h.tx4)} % statt 41 %, kein Auto-Ping, 0 Verfalls-Verwerfungen. "
            "Beobachten: HB9VQQ-5 am Empfindlichkeitsrand, Zeit-Rückstau von HB9HNO-49 (Betrieb). "
            "Kein Firmware-Fehler gefunden.", chip("watch")]


def fw_rows(d: Data, R: Callable[[str], str]) -> list[list[str]]:
    h = Hb9(d)
    x = h.x
    return [
        ["12", "HB9: <code>RLY q=full</code> wird für <code>RING_SUPERSEDE_KEEP</code> gedruckt (falscher Grund im Log)",
         f"{x['cosmetic']['q_full_lines_4']} Zeilen an HB9VQQ-4, jede direkt nach dem Supersede-Keep; kein Datenverlust",
         "<code>lora_functions.cpp:2651</code>, <code>txring_functions.cpp:1030-1035</code>",
         chip("neu", "kosmetisch"), chip("ok", "Niedrig")],
        ["13", "HB9: <code>[MC-TRICKLE] SEND consistent=17&lt;k=2</code> bei Gateway-erzwungenen Sendungen "
               "(Text widerspricht dem Wert)",
         f"SEND-Zeilen mit consistent ≥ 2: {x['cosmetic']['trickle_lines_4']} (-4), {x['cosmetic']['trickle_lines_5']} (-5)",
         "<code>esp32_main.cpp:4815-4826</code>", chip("neu", "kosmetisch"), chip("ok", "Niedrig")],
        ["14", "HB9: stiller Relay-Cancel ohne Log-Zeile; <code>stat_nbr_cancel</code> in keiner STAT-Zeile",
         f"{h.van4} Slots an HB9VQQ-4 verschwinden ohne Zeile ({R('hbcancel')})",
         "<code>lora_functions.cpp:1445,1463</code> (Zeile nur mit nbrLog)",
         chip("watch", "Beobachtung"), chip("ok", "Niedrig")],
    ]


def verify_table_row(d: Data) -> list[Any]:
    v = d.hb9x["verify_counts"]
    return [v["topic"], f'<a href="{v["file"]}">{v["file"]}</a>', v["confirmed"], v["corrected"], v["refuted"]]


def verify_changes(d: Data) -> list[str]:
    x = d.hb9x
    s, f = x["stale"], x["setting"]
    return [
        f"<b>HB9 · veralteter {{CET}}-Rückstau:</b> {s['frames_4']} / {s['frames_5']} veraltete Frames lagen im Funk "
        f"({s['telegrams']} Telegramme, alle von {s['call']}); „1 von 148“ gilt nur für als neu angenommene Frames. "
        f"Die {s['lines_orchestrator']} Empfänge der ersten Zählung waren Zeilen, nicht Frames ({s['frames_4']}).",
        f"<b>HB9 · Nachzügler bei HB9VQQ-5:</b> die „43 neu angenommenen / 23 weitergeleiteten“ sind HEY- und "
        f"Positions-Frames, kein {{CET}} ({s['late_hey_pos_accepted_5']} angenommen, {s['late_hey_pos_relayed_5']} relayt).",
        f"<b>HB9 · Gegenseitiger Empfang:</b> {de(f['decode_5_hears_4_pct'])} / {de(f['decode_4_hears_5_pct'])} % im "
        "gemeinsamen Fenster (nicht 90 / 64 %); eigene HEY warten "
        f"{de(x['decay']['own_hey_wait_factor_4'], 2)}× (-4) bzw. {de(x['decay']['own_hey_wait_factor_5'], 2)}× (-5) so lange "
        "wie relayte HEY (nicht „2×“ auf beiden); Empfindlichkeitsrand von -5 am SNR festgemacht, nicht am RSSI.",
    ]


def dev_items() -> list[str]:
    return [
        "Schweizer Netz: <code>data/hb9/{q2,q34,q5,q6,health}/results.json</code>, <code>data/hb9/master/</code>, "
        "<code>data/hb9/CORE-FINDINGS.md</code>; verifizierte Zahlen in <code>data/extra/uebersicht_hb9.json</code> "
        "(Quelle je Block benannt), Prüftabelle <code>verify/hb9.md</code>, Skripte <code>verify/hb9_checks/</code>. "
        "Extraktor-Artefakte in den HB9-results.json sind nicht übernommen (health beat_miss -4, q34-Gegenprobe ohne "
        "Cancel-Pfad, q5 tx_age_gt150_n, q2-Flugquote HB9VQQ-5, ACK-Zuordnung mit OE3-Schlüssel).",
    ]


def section(d: Data, X: Xfn, R: Callable[[str], str], oe3: dict[str, Any]) -> list[str]:
    h = Hb9(d)
    x = h.x
    st, ss, hl, po, de_, ti, f6 = (x["setting"], x["stale"], x["health"], x["pool"], x["decay"],
                                   x["time"], x["f6"])
    m4, m5, hno, lek = h.m["HB9VQQ-4"], h.m["HB9VQQ-5"], h.m["HB9HNO-49"], h.m["HB9LEK-12"]
    b: list[str] = []
    b.append('<h2 id="hb9">Schweizer Netz (HB9VQQ-4 / HB9VQQ-5)</h2>')
    b.append("<p><b>Ein drittes, getrenntes Netz: zwei Heltec-V3-Gateways in der Schweiz mit derselben "
             "Firmware-Familie 4.40a. Die Zahlen stehen neben OE3 und DB0AIS, nie mit ihnen korreliert; der Vergleich "
             "unten ist ein Vergleich von Kennzahlen, kein Beweis.</b> Er stützt die Lesart, dass die OE3-Probleme "
             "durch Last und Konfiguration entstehen, zusätzlich zu den belegten Designlücken.</p>")
    b.append(ul([
        f"<b>HB9VQQ-4:</b> {m4['hardware']} (ESP32-S3), Internet-Gateway „{m4['description']}“, {m4['lat']} N "
        f"{m4['lon']} E, {m4['altitudeM']} m. Mitschnitt {st['span_4']}, <code>--setlog</code> und "
        "<code>--loradebug</code>, keine [NBR]-Zeilen (nbrdebug aus).",
        f"<b>HB9VQQ-5:</b> {m5['hardware']} (ESP32-S3), Gateway „{m5['description']}“, {m5['lat']} N {m5['lon']} E, "
        f"{m5['altitudeM']} m. Mitschnitt {st['span_5']}, nur <code>--loradebug</code>, keine [LOG]-Zeilen: "
        "Empfangs-Schwanz-Flags, Relay-Gründe, STAT, GWU/GWI, ACK-Zuordnung und NEED/CANCEL sind "
        f"{NM}; Quelle, Priorität und Wartezeit der Sendungen sind aus RING_WRITE und RING_TX_READ rekonstruiert, "
        "RSSI/SNR aus „Received packet“.",
        f"<b>Abstand und Hörbarkeit:</b> ca. {de(st['distance_km'])} km; im gemeinsamen Fenster dekodiert -5 "
        f"{de(st['decode_5_hears_4_pct'])} % der Sendungen von -4, -4 dekodiert {de(st['decode_4_hears_5_pct'])} % "
        "von -5; RSSI ca. −97 / −100 dBm.",
        f"<b>Logger:</b> <code>tools/meshlogger.py</code> über TCP 2323. Bei -5 übernimmt {st['console_takeovers_5']} Mal "
        f"ein zweiter Konsolen-Client die Sitzung ({st['takeover_hourly']} davon zur vollen Stunde, je "
        f"{de(st['takeover_s_lo'])} bis {de(st['takeover_s_hi'])} s, zusammen {de(st['takeover_sum_s'])} s; "
        "<code>net_console.cpp</code>): Logger-Lücken, alle -5-Zählungen sind Untergrenzen. Bei -4: keine.",
    ]))

    # verdicts per question
    b.append("<h3>Urteile je Frage</h3>")
    vr = [
        ["Gesundheit", chip("ok"), chip("watch"),
         "Beide ohne Neustart, Heap flach, Keep-Alives zu 100 % beantwortet. -5 am Empfindlichkeitsrand: "
         f"{de(hl['crc_pct_5'])} % der erkannten Frames mit CRC-Fehler, SNR p50 {hl['snr_p50_5']} dB (nahe der "
         f"SF11-Grenze), {de(hl['ferr_lt1k_pct_5'])} % der CRC-Fehler mit Frequenzfehler unter 1 kHz: keine Störung."],
        ["F2 TX-Puffer", chip("ok"), chip("ok"),
         f"0 RING_DROP_*, Tiefe höchstens {po['depth_max_4']} / {po['depth_max_5']} von {po['ring_slots']}; "
         f"{de(po['aired_pct_4'])} % / {de(po['aired_pct_5'])} % von {h.writes4} / {h.writes5} Einträgen gesendet. "
         f"-4: {po['vanished_4']} Slots ({de(po['vanished_4_pct_writes'])} %) still frei nach fremder Kopie "
         "(Overhear-Cancel, Schluss)."],
        ["F3 Verlust durch Zeitverfall", chip("watch"), chip("ok"),
         f"0 RING_DROP_STALE auf beiden: nichts verloren. -4: HEY-Klasse bis {de(de_['hey_peak_s'])} s von "
         f"{de_['hey_ttl_s']} s ({de(de_['hey_peak_pct'])} %) im Schub {de_['hey_burst']} (Beobachten)."],
        ["F4 Gewinn durch Zeitverfall", chip("neu"), chip("neu"),
         f"Nichts verworfen, also messbar nichts gespart. Die Gegenprobe ist für -4 ungültig "
         f"({de_['cf_predicted_drops_4']} vorhergesagt gegen 0 real: der Cancel-Pfad fehlt im Modell), für -5 "
         f"{NM}. CSMA-Altersboden: {de_['csma_events_4']} Ereignisse in {de_['csma_episodes_4']} Episoden (-4), "
         f"{de_['csma_events_5']} in {de_['csma_episodes_5']} (-5), ohne Kollisionsmerkmal."],
        ["F5 Zeitsignal", chip("watch"), chip("watch"),
         f"Eigenes {{CET}} höchstens {ti['own_cet_age_max_4_s']} s (-4, {ti['own_cet_tx_4']} Sendungen) bzw. "
         f"{ti['own_cet_age_max_5_s']} s (-5, {ti['own_cet_tx_5']}), {ti['gate_drops']} Sperren-Treffer. "
         f"Beobachten: veralteter Rückstau von {ss['call']} und die Doppelsendung desselben Telegramms (siehe unten)."],
        ["F6 Manipulationen", chip("watch"), chip("watch"),
         f"Kein Aufstau, 0 Schleifen, Sendeanteil {de(f6['tx_duty_4_pct'])} % / {de(f6['tx_duty_5_pct'])} %, "
         "ACK-/Ping-Last ca. 1 %. Beobachten: beide Knoten senden viele IDs gegenseitig doppelt (siehe unten). "
         f"Bei -5 sind ACK-Zuordnung und Relay-Gründe {NM}."],
    ]
    b.append(tab(["Frage", "HB9VQQ-4", "HB9VQQ-5", "Begründung"], vr))

    # comparison to OE3
    b.append("<h3>Vergleich mit dem OE3-Cluster</h3>")
    ack4, ack5 = f6["ack_tx_4"], f6["ack_tx_5"]
    cmp_rows = [
        ["ACK-Relays, Anteil an den Sendungen",
         f"{oe3['p_ack_tx']} % (mit Pings {oe3['p_all_tx']} %)",
         f"{_pct(ack4, h.tx4)} % ({ack4} von {h.tx4})", f"{_pct(ack5, f6['tx_aired_5'])} % ({ack5} von {f6['tx_aired_5']})"],
        ["Auto-Ping", f"OE3XWJ-12 jede Minute ({oe3['pings']} Pings)",
         f"keiner; nur manuelle Pings ({h.ping_frames} Ping-Frames, {h.ping_air} s Sendezeit)", NM],
        ["Verfalls-Verwerfungen (TTL)", str(oe3["drops"]), str(h.drops), str(h.drops5)],
        ["Eigene Beacons verloren", f"OE3XOC {oe3['lost_oc']}, OE3XIA {oe3['lost_ia']} (HEY und WX)",
         f"0 (HEY {h.hey['outcomes']['sent']} von {h.hey['written']} gesendet, 1 ersetzt; WX {h.wx['written']} von "
         f"{h.wx['written']})",
         f"0 (HEY {de_['own_hey_sent_5']} von {de_['own_hey_written_5']}, 1 ersetzt; WX {de_['own_wx_sent_5']} von "
         f"{de_['own_wx_written_5']}; aus Ring-Zeilen)"],
        ["Eigene HEY gegen relayte HEY (Wartezeit)", "eigene Beacons verhungern an OE3XOC und OE3XIA",
         f"{de(de_['own_hey_wait_factor_4'], 2)}× so lange, ohne Verlust ({de(de_['own_hey_aired_pct_4'])} % gesendet)",
         f"{de(de_['own_hey_wait_factor_5'], 2)}× so lange, ohne Verlust ({de(de_['own_hey_aired_pct_5'])} % gesendet)"],
        ["Overhear-Cancel", f"{oe3['cancel_lines']} CANCEL-Zeilen (nur OE3XHL); ACK-Relays ohne Cancel-Pfad",
         f"{h.van4} stille Freigaben ({de(po['vanished_4_pct_writes'])} % der Einträge), jede nach fremder Kopie; "
         "keine Log-Zeile",
         f"Cancel-Zeilen {NM}; {h.van5} Slots verschwinden still ({po['vanished_5_with_foreign_copy']} davon mit "
         "fremder Kopie)"],
        ["Veraltete {CET}-Quelle",
         "vier fremde Relays (OE3MIF-12, OE3NRA-1, OE3MTC-10, OE3XDC-12) und das Gateway " + oe3["gjc"],
         f"{ss['call']} ({hno['hardware']}, {hno['firmware']}): {ss['frames_4']} Frames",
         f"{ss['call']}: {ss['frames_5']} Frames"],
        ["Ausgang der veralteten {CET}",
         "eigene Knoten senden nie über 150 s; die Alterssperre stoppte 8 bei der Weiterleitung",
         f"1 als neu angenommen (Hop0-Ausstieg, kein Ring-Eintrag); eigenes {{CET}} höchstens "
         f"{ti['own_cet_age_max_4_s']} s",
         f"0 als neu angenommen; eigenes {{CET}} höchstens {ti['own_cet_age_max_5_s']} s"],
    ]
    b.append(tab(["Kennzahl", "OE3-Cluster", "HB9VQQ-4", "HB9VQQ-5"], cmp_rows))
    b.append("<p class=\"small\">Die Last ist verschieden (anderes Netz, anderer Tag, andere Dichte): der Vergleich zeigt, "
             "dass dieselbe Firmware ohne den Ping-Strom und ohne ACK-Flut keine Beacon-Verluste und keine "
             "Verfalls-Verwerfungen zeigt. Dass die eigenen HEY auch hier länger warten als relayte, ist dieselbe "
             "Asymmetrie wie bei OE3 (siehe Befund 3), nur ohne Folgen bei dieser Last.</p>")

    # stale source
    b.append("<h3>Veraltete Zeit: HB9HNO-49 sendet 25 Minuten verspätet</h3>")
    b.append(X("hbcet", D.hb9_stale_cet(de(ss["example_age_s"] / 60.0), str(ss["example_age_s"]),
                                        str(max(ti["own_cet_age_max_4_s"], ti["own_cet_age_max_5_s"]))),
               "HB9HNO-49 sendet jeden Frame-Typ ca. 25 Minuten verspätet: {CET} wird gestoppt, HEY und Position laufen weiter"))
    b.append(ul([
        f"<b>Befund:</b> {ss['telegrams']} verschiedene {{CET}}-Telegramme, {ss['age_lo_s']} bis {ss['age_hi_s']} s alt, "
        f"alle zuletzt von {ss['call']} gesendet ({ss['frames_4']} Frames bei -4, {ss['frames_5']} bei -5). "
        f"{ss['call']}: {hno['hardware']}, Firmware {hno['firmware']} (vor CAD und ohne {{CET}}-Alterssperre), Gateway, "
        f"{hno['lat']} N {hno['lon']} E, {hno['altitudeM']} m (mcmap).",
        f"<b>Ausgang:</b> nur ein veraltetes {{CET}} wurde als neu angenommen ({ss['example_id']} bei -4, "
        f"{ss['example_age_s']} s alt, Hop0-Ausstieg, kein Ring-Eintrag); keiner der beiden Knoten sendete ein {{CET}} "
        "älter als 150 s. Kein Firmware-Fehler.",
        f"<b>HEY und Position:</b> späte Frames von {ss['call']} werden weitergeleitet (bei -5 "
        f"{ss['late_hey_pos_accepted_5']} angenommen, {ss['late_hey_pos_relayed_5']} relayt; {ss['late_copies_per_day']} "
        "Kopien am Tag): gewollt, denn für diese Typen gibt es kein Alterstor am Ursprung und die Frist des Rings zählt "
        "ab dem Einreihen. Das kostet Sendezeit; die Ursache liegt beim Betreiber.",
    ]))

    # observations
    b.append("<h3>Beobachten</h3>")
    b.append(ul([
        f"<b>HB9VQQ-5 am Empfindlichkeitsrand:</b> {de(hl['crc_pct_5'])} % der erkannten Frames fallen durch die CRC "
        f"(bei -4: {de(hl['crc_pct_4'])} %), SNR p50 {hl['snr_p50_5']} dB; {de(hl['ferr_lt1k_pct_5'])} % der CRC-Fehler "
        "haben einen Frequenzfehler unter 1 kHz. Standort 1432 m; dass es an weit entfernten Sendern liegt, ist Vermutung.",
        f"<b>HB9LEK-12</b> ({lek['hardware']}, {lek['firmware']}, {lek['lat']} N {lek['lon']} E, {lek['altitudeM']} m) "
        f"sendet bei allen {hl['hb9lek_frames']} dekodierten Frames um +{de(hl['hb9lek_offset_khz'])} kHz neben der "
        f"Frequenz und verursacht bei -4 ca. 19 % der CRC-Fehler (die Zuordnung ist ein Schluss: CRC-Frames tragen "
        "keinen Absender).",
        f"<b>Lokale Quelle bei -4:</b> wiederkehrend {hl['local_source_events_4']} Präambel-Ereignisse ohne Frame bei "
        f"{hl['local_source_dbm'].replace('-', '−')} dBm.",
        f"<b>Overhear-Cancel bei -4:</b> {h.van4} Slots ({de(po['vanished_4_pct_writes'])} % der Einträge, "
        f"{de(po['vanished_4_pct_relay_writes'])} % der Relay-Einträge) werden still frei, jeder erst nach einer "
        "gehörten fremden Kopie; nur der NBR-Cancel gibt einen fertigen Relay-Slot still frei, das setzt "
        f"<code>--nbrrelay on</code> voraus (Schluss). Von {po['cancel_hole_4']} gab es keinen Cancel, obwohl die "
        "Gelegenheiten blieben; die Ursache ist ohne [NBR]-Zeilen offen.",
        f"<b>Zeitsignal-Quellen:</b> OE1XAR-33 nur per UDP (Takt {ti['udp_cadence_s']} s, keine Lücke); OE1XAR-63 "
        f"({ti['n63_telegrams']} Telegramme, über DO1FE-19) und OE1XAR-28 ({ti['n28_telegrams']}, über HB9CMI-40 / "
        "HB9CF-40) nur per Funk; OE1XAR-62 nie.",
        f"<b>Doppelsendung des {{CET}}:</b> ca. {ti['radiations_per_telegram']} Aussendungen je Telegramm; beide Knoten "
        f"senden dasselbe Telegramm in {ti['same_telegram_both']} Fällen, und der spätere hatte die frühere Kopie in "
        f"{ti['later_had_earlier_pct']} % ({ti['later_had_earlier_n']}) schon dekodiert. Das ist die Designlücke "
        "„Gateway-Inject-Slots werden nie zurückgenommen“ (Befund 5 bei OE3).",
        f"<b>Gegenseitige Doppelsendungen:</b> {f6['redundant_ids']} IDs senden beide Knoten ({f6['redundant_relay_pairs']} "
        f"Relay/Relay-Paare); die spätere Kopie macht {de(f6['later_copy_airtime_pct'])} % der Paar-Sendezeit aus "
        "(Obergrenze: nur fünf gemeinsame direkte Nachbarn). "
        f"{de(f6['relay_after_foreign_pct_4'])} % der Sendezeit von -4 sind Relays nach gehörter fremder Kopie "
        f"(Obergrenze; OE3: {de(f6['oe3_relay_after_foreign_pct'])} %).",
        f"<b>Schub bei den HEY:</b> bis {de(de_['hey_peak_s'])} s von {de_['hey_ttl_s']} s Wartezeit bei -4 am "
        f"{de_['hey_burst']}; ohne Verlust.",
    ]))
    b.append(X("hbcancel", D.hb9_silent_cancel(str(h.van4), de(po["vanished_4_pct_writes"])),
               "Der Overhear-Cancel gibt Relay-Slots still frei: im Log bleibt nur die Bilanz"))

    # operator
    b.append("<h3>Betreiber</h3>")
    b.append(ol([
        f"<b>HB9LEK-12:</b> Sendefrequenz um +{de(hl['hb9lek_offset_khz'])} kHz versetzt (alle {hl['hb9lek_frames']} "
        "Frames); Quarz oder Frequenzeinstellung prüfen (Vermutung).",
        f"<b>{ss['call']}:</b> von Firmware {hno['firmware']} auf 4.40 bringen; sonst bleiben der 25-Minuten-Rückstau, die "
        "veralteten {CET} und die späten HEY/Position-Kopien.",
        "<b>HB9VQQ-5:</b> den nächsten Mitschnitt mit <code>--setlog on</code> fahren (sonst bleiben Relay-Gründe, "
        "STAT und ACK-Zuordnung unsichtbar); bei beiden Knoten <code>--nbrdebug on</code>, um das Loch im Cancel "
        f"({po['cancel_hole_4']}) zu klären.",
        f"<b>Zweiter Konsolen-Client bei -5:</b> {st['console_takeovers_5']} Übernahmen, jede Stunde ein Client auf der "
        "Betreiberseite, der den Logger kurz aus der Sitzung wirft; mit dem Betreiber klären und abstellen.",
    ]))
    b.append("<p><b>Firmware:</b> im Schweizer Netz wurde kein Firmware-Fehler gefunden. Zwei kosmetische Log-Texte und "
             "eine Beobachtungslücke stehen als Nummer 12 bis 14 in der Befundtabelle.</p>")
    return b
