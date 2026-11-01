"""Body of the network overview page (uebersicht.html): build() returns the HTML body and figure numbers."""

from __future__ import annotations

import uebersicht_draw as D
import uebersicht_hb9 as HB
import uebersicht_head as H
from uebersicht_head import NODES, Data, chip, de, ol, tab, ul


def dte(s: str) -> str:
    """'2026-10-31 11:49:01' -> '31.10.2026 11:49:01'."""
    day, _, rest = s.partition(" ")
    y, m, dd = day.split("-")
    return f"{dd}.{m}.{y} {rest}".strip()


def st(*chips: str) -> str:
    return '<p class="status">' + " ".join(chips) + "</p>"


def pct(a: float, b: float, nd: int = 1) -> str:
    return de(100.0 * a / b, nd)


def build(d: Data, ref: dict[str, int]) -> tuple[str, dict[str, int]]:
    K = H.K
    figs: dict[str, int] = {}
    counter = [0]

    def nxt(key: str) -> int:
        counter[0] += 1
        figs[key] = counter[0]
        return counter[0]

    def R(key: str) -> str:
        return f"Abb. {ref.get(key, '?')}"

    def X(key: str, drawing: D.Drawing, text: str) -> str:
        n = nxt(key)
        return K.explain(drawing[0], f"Abb. {n}: {text}", 640, drawing[1])

    q2n, q34n, q5o, q6n, hn = (d.q2["network"], d.q34["network"], d.q5["network"]["oe3_cluster"],
                               d.q6["network"], d.health["network"])
    V = d.ver
    m = d.master

    # ------------------------------------------------------------------ numbers derived from data/
    sil = d.sil["oe3xia"]
    n_sil = sil["silences_gt_300s"]
    sil_s = f"{round(sil['silent_s']):d}"
    sil_pct = de(sil["silent_pct_of_span"])
    sil_long = f"{round(sil['longest_gap_s']):d}"
    sil_long_start = dte(sil["longest_gap_start"])
    sil_first = dte(sil["first_silence_start"])
    others_longest = {k: d.sil[k]["longest_gap_s"] for k in ("oe3xhl", "oe3xir", "oe3xoc", "oe3xwj")}
    ps = q6n["ping_share"]
    tot_tx, tot_air = ps["total_tx"], ps["total_air_s"]
    ack_tx = ps["ping_tx"]["ack_of_ping_relay"]
    pings = ps["ping_tx"]["ping_orig_or_relay"]
    ack_air = V["ping"]["ack_relay_air_s"]
    ping_air = ps["ping_air_s"]
    p_ack_tx, p_ack_air = pct(ack_tx, tot_tx), pct(ack_air, tot_air)
    p_all_tx, p_all_air = pct(ack_tx + pings, tot_tx), pct(ping_air, tot_air)
    csma_nodes = {k: d.q34["nodes"][k]["csma"] for k in ("oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj")}
    csma_ev = sum(v["events"] for v in csma_nodes.values())
    csma_ep = sum(v["episodes"] for v in csma_nodes.values())
    csma_p3 = sum(v["ep_prio"].get("3", 0) for v in csma_nodes.values())
    csma_p2 = sum(v["ep_prio"].get("2", 0) for v in csma_nodes.values())
    csma_p1 = sum(v["ep_prio"].get("1", 0) for v in csma_nodes.values())
    gfn = {k: d.q5["nodes"][k] for k in ("oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj")}
    gf_n = sum(v["tx_after_overheard_gateway_radiation_n"] for v in gfn.values())
    gf_tot = sum(v["tx_count"] for v in gfn.values())
    gf_pct = pct(gf_n, gf_tot)
    gf_node = {k: pct(v["tx_after_overheard_gateway_radiation_n"], v["tx_count"]) for k, v in gfn.items()}
    rcm = q5o["radiation_class_per_telegram_mean"]
    own_gw, for_gw, relays_pt = de(rcm["our_gateway"]), de(rcm["foreign_gateway"]), de(rcm["relay"])
    rad_tot_f = rcm["our_gateway"] + rcm["foreign_gateway"] + rcm["relay"]
    rad_tot = de(rad_tot_f)
    rad_air = de(q5o["radiation_airtime_per_telegram_s"])
    n_tel = q5o["all5_window_ids"]
    fgw_med = int(q5o["foreign_gateways_per_telegram"]["p50"])
    stale_rows = V["stale_relays_full_run"]["rows"]
    air_s = q6n["redundancy"]["airtime_s_all_tx_by_type"]
    air_all = 13564.0
    ob = d.q34["network"]["own_beacons"]
    cancel_lines = V["cancel"]["lines"]
    rxe = V["rx_error_share_pct"]
    q34n_drops = q34n["drops_total"]
    ttl_node = {k: d.q34["nodes"][k]["drops_total"] for k in d.q34["nodes"]}
    ttl_node["db0ais"] = q34n["db0ais"]["ring_drop_stale"]

    def own_hey(key: str) -> tuple[int, int, int]:
        hc = d.q2.get("nodes", {}).get(key, {}).get("hey_chains", {}).get("own_hey")
        if not hc or "fate" not in hc:  # {"n": 0}: node sends no own HEY
            return 0, 0, 0
        return hc["fate"].get("tx_air", 0), hc["fate"].get("stale_ttl", 0), hc["chains"]

    def own_wx(key: str) -> tuple[int, int]:
        w = d.q2["nodes"][key]["fate"]["by_src"].get("user_wx", {})
        return w.get("stale_ttl", 0), sum(w.values())

    def mrow(call: str) -> dict[str, str] | None:
        return m.get(call)

    def place(call: str) -> str:
        r = mrow(call)
        if not r:
            return "keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg)" if call == "OE3XIR-12" \
                else "keine Stammdaten"
        return f"{r['description'].split(' JN')[0] if r['description'] else r['lat'] + ', ' + r['lon']} " \
               f"({r['altitudeM']} m)"

    gjc = mrow("OE3GJC-12")
    gjc_txt = (f"OE3GJC-12 ({gjc['hardware']}, {gjc['firmware']}, {gjc['description']}, {gjc['lat']} N "
               f"{gjc['lon']} E, {gjc['altitudeM']} m, mcmap)" if gjc else "OE3GJC-12 (keine Stammdaten)")
    rca = V["rca"]
    pe = d.wjping["cluster_by_ping_event"]
    pe_mean, pe_med, pe_max = de(pe["ack_ids_per_ping_mean"]), pe["median"], pe["max"]
    ir_dec = d.q2["nodes"]["oe3xir"]["fate"]["counts"]["tx_noair_decodefail"]
    ir_tot = d.q2["nodes"]["oe3xir"]["fate"]["total_writes"]
    ir_err = hn["oe3xir_invalid_ping"]["aprs_decode_errors_dest_255"]["APRS_decode_err:APRS decode - Destination-CallSign Error [N.]"]
    ir_nocad = d.health["nodes"]["oe3xir"]["cad"]["cad_free_no_tx"]
    ir_hours = de(d.health["nodes"]["oe3xir"]["span"]["hours"])
    b: list[str] = []

    # ------------------------------------------------------------------ header
    b.append('<div class="eyebrow">MeshCom Log-Auswertung · Netzwerk-Übersicht</div>')
    b.append("<h1>Das OE3-Cluster ist stabil und verliert keine Texte, sendet aber zu viel doppelt "
             "und lässt die eigenen Beacons der vollen Knoten verhungern</h1>")
    b.append("<ul class=\"meta\">"
             "<li><b>Zeitraum OE3:</b> 31.10.2026 ca. 06:30 bis 01.11.2026 ca. 07:50 (Logger-Uhr; "
             "OE3XIR endet am 31.10.2026 um 18:16, also nach 11,5 h)</li>"
             "<li><b>Zeitraum DB0AIS:</b> 30.10.2026 17:01 bis 31.10.2026 18:09 (25,2 h, Knotenuhr; "
             "anderes Netz, anderer Tag, nie mit OE3 korreliert)</li>"
             f"<li><b>Zeitraum HB9:</b> {d.hb9x['setting']['span_4']} (HB9VQQ-4) und {d.hb9x['setting']['span_5']} "
             "(HB9VQQ-5), Logger-Uhr; drittes Netz, nie mit OE3 oder DB0AIS korreliert</li>"
             "<li><b>Knoten, drei Netze:</b> (1) OE3-Cluster: OE3XHL-12, OE3XIA-12, OE3XIR-12, OE3XOC-12, OE3XWJ-12 "
             "(fünf Relays mit Internet-Gateway, parallel mitgeschnitten); (2) DB0AIS-12 (Hessen); "
             "(3) Schweiz: HB9VQQ-4 und HB9VQQ-5 (zwei Internet-Gateways)</li>"
             "<li><b>Hardware:</b> OE3 und DB0AIS: alle sechs RAK4631 (nRF52840), mit Ethernet (W5100S ist abgeleitet, "
             "steht nicht im Log); HB9: zwei Heltec WiFi LoRa 32 V3 (ESP32-S3)</li>"
             "<li><b>Firmware:</b> 4.40a, kein Neustart im Mitschnitt. Der Stempel 20260929 ist die "
             "Release-Kennung <code>FLASH_VERSION</code> (<code>src/loop_functions.cpp:3673</code>), kein "
             "Build-Datum; der genaue Build ist unbekannt. Code-Stellen sind am Stand 651e0920 gelesen. "
             "Vermutung aus dem Log-Bild: TX-Slot-Pool mit Supersede und die NBR-NEED-Zeile sind enthalten</li>"
             "<li><b>LoRa:</b> 433,175 MHz, BW 250 kHz, SF11, CR 4/6 (nur DB0AIS im Boot-Block; bei OE3 "
             "aus der Sendezeit abgeleitet)</li>"
             "<li><b>Uhren:</b> OE3XIR-Knotenuhr +3599 s gegen den Logger, die anderen vier −1 s; "
             "DB0AIS +1 h bis ca. 18:20 Knotenzeit, dann Rücksprung (Ursache Vermutung, "
             "<code>src/clock.cpp:153-165</code>)</li>"
             "<li><b>Zählfalle:</b> 12-Byte-ACK-Frames drucken keine „OnRxDone“-Zeile "
             "(<code>src/lora_functions.cpp:1018-1034</code>); jede Empfangszahl aus diesen Zeilen "
             "verfehlt die ACKs und ist hier korrigiert</li>"
             "<li><b>Stand:</b> 01.11.2026, nach adversarieller Prüfung (siehe Abschnitt „Prüfung“)</li></ul>")
    b.append('<div class="bluf"><p class="lead">Die fünf OE3-Gateways laufen stabil: kein Neustart, kein '
             'Ring-Überlauf, kein Text und keine Quittung durch Zeitverfall verworfen, keine Schleifen. '
             f'Verbesserungsbedarf: bis zu {de(V["ack_after_heard"]["air_pct"])} % der Sendezeit (Obergrenze) '
             'gehen auf Relay-Sendungen nach einer schon gehörten Kopie, und an OE3XOC und OE3XIA verfallen '
             'eigene Beacons unversendet.</p>'
             '<p class="reco">Empfehlung: den Altersboden auf eigene Beacons (Prio 4/5) ausdehnen und '
             'testen, die Entscheidung zum Auto-Ping der RAK4631 treffen, die Zieladresse prüfen und die '
             'Betreiber der Ping-, {CET}- und Empfangsprobleme ansprechen.</p>'
             '<p>Das Schweizer Netz (zwei Heltec-V3-Gateways HB9VQQ-4/-5) zeigt dieselbe Firmware ohne die '
             f'OE3-Lastprobleme: ACK-Last {HB.f6_ack_pct(d)} % statt {de(V["ping"]["ack_relay_tx_pct"], 0)} %, kein '
             'Auto-Ping, 0 Verfalls-Verwerfungen. Das stützt die Lesart, die OE3-Probleme als Folge von Last und '
             'Konfiguration plus den belegten Designlücken zu lesen (Vergleich, kein Beweis: anderes Netz).</p></div>')

    # ------------------------------------------------------------------ Handlungsbedarf
    b.append('<div class="callout act"><h3>Handlungsbedarf, nach Priorität</h3>'
             '<p><b>Firmware (sechs Befunde, Reihenfolge = Priorität; Details im Abschnitt „Sechs Befunde“)</b></p>'
             + ol([
                 "<b>Auto-Ping: Unterschied ESP32 / RAK4631, Entscheidung offen</b> "
                 + chip("watch", "Unterschied, Entscheidung offen") +
                 f" Die ESP32-Schleife zählt den Ping herunter und stoppt; die RAK4631-Schleife pingt ohne "
                 f"Zähler, solange <code>--pingcall</code> gesetzt ist, und <code>--ping stop</code> wirkt "
                 f"dort nicht (gleicher Code in upstream dev). Betreiberentscheidung, was gewollt ist "
                 f"({R('pingplat')}).",
                 "<b>Zieladresse des Pings wird nicht geprüft</b> " + chip("act", "bestätigt im Code") +
                 f" OE3XIR reiht jede Minute einen Ping an „255.“ ein, der erst beim Dekodieren stirbt "
                 f"({H.de(ir_err, 0)} Mal; "
                 f"{R('p255')}).",
                 "<b>Eigene Beacons verhungern an vollen Knoten</b> " + chip("act", "bestätigt im Code (Designlücke)") +
                 f" Am Kopf der Warteschlange bekommt ein eigener Beacon die 5,5-s-Haltezeit, ein Relay "
                 f"gleicher Priorität nach 8 s die 150-220-ms-Suche ({R('ownrelay')}).",
                 "<b>ACK-Weiterleitungen haben keinen Overhear-Cancel</b> " + chip("act", "bestätigt im Code") +
                 f" ({R('ack')}).",
                 "<b>{CET}-Server-Slots sind immer „Alleinversorger“</b> " + chip("watch", "Verdacht (Design)") +
                 f" 666 von 666 Entscheidungen Fall A, nie ein Cancel ({R('srv')}).",
                 "<b>OE3XIA ist zeitweise taub</b> " + chip("watch", "Verdacht") +
                 f" {n_sil} Empfangsstillen über 5 Minuten, {sil_pct} % der Zeit; Bench-Hypothese: "
                 f"Standby vor dem Empfangs-Neustart ({R('deaf')}).",
             ]) + '<p><b>Betrieb</b></p>' + ol([
                 "<b>OE3XWJ-12 und OE3XIR-12: Ping-Einstellungen.</b> OE3XWJ-12 pingt jede Minute die "
                 f"Gruppe 2025 ({pings}×); mit den ausgelösten ACK-Relays sind das {p_all_tx} % der Sendungen und "
                 f"{p_all_air} % der Sendezeit des Clusters ({R('ping')}). OE3XIR-12 hat „255.“ als Ping-Ziel "
                 "eingetragen; Eintrag korrigieren oder <code>--pingcall</code> löschen.",
                 "<b>Fremde Gateways quittieren die Pings.</b> Die ACKs kommen von Gateways außerhalb der "
                 "fünf; ihre Betreiber sind unbekannt. Vermutung: Builds ohne die {ping}-Ausnahme, die der "
                 "aktuelle Code hat.",
                 "<b>Veraltete {CET}-Zeit aus fremden Relays:</b> OE3MIF-12, OE3NRA-1, OE3MTC-10, OE3XDC-12 "
                 f"(alle ohne Stammdaten; zusammen {de(V['stale_relays_full_run']['four_share_of_stale_pct'])} % "
                 f"der veralteten Aussendungen) und das Gateway {gjc_txt} ({R('stale')}). Vermutung: Build ohne "
                 "Alterssperre (vor 05a18b36 / #1238) oder eine vorgehende Uhr.",
                 "<b>OE3XIR-12:</b> starke lokale Quelle am Standort (323 Fehlempfänge über −60 dBm, Typ unbekannt) "
                 "suchen; Zeitzone von fest +2 auf Regel umstellen.",
                 "<b>OE3XIA-12:</b> Empfänger vor Ort prüfen (Antenne, Versorgung, Funkmodul); Standby-Test auf der Bench.",
                 "<b>Stationen mit 4.35 oder älter</b> (Pre-CAD, senden ohne Kanalabfrage) tragen "
                 f"{de(min(hn['pre_cad_events_pct'][k] for k in ('oe3xhl','oe3xia','oe3xir','oe3xoc','oe3xwj')), 0)} bis "
                 f"{de(max(hn['pre_cad_events_pct'][k] for k in ('oe3xhl','oe3xia','oe3xir','oe3xoc','oe3xwj')), 0)} % "
                 f"der Empfangsereignisse je Knoten (Beobachten; {R('precad')}).",
                 "<b>Schweizer Netz (Betreiber, Details im Abschnitt „Schweizer Netz“):</b> HB9LEK-12 sendet "
                 "+4,5 kHz neben der Frequenz; HB9HNO-49 läuft auf 4.35h und sendet alles ca. 25 min verspätet; "
                 "der nächste HB9VQQ-5-Mitschnitt braucht <code>--setlog on</code>.",
             ]) + '</div>')

    # ------------------------------------------------------------------ Antwort-Index
    b.append("<h2>Antwort-Index</h2>")
    idx = [
        ["Gesundheit", f"Stabil, kein Neustart, nichts baut sich auf. OE3XIA hört {sil_pct} % der Zeit nichts "
         "(Verdacht auf hängenden Empfangspfad); OE3XIR hat eine starke lokale Quelle.", chip("act")],
        ["F2 TX-Puffer mit Slots", "Mechanik in Ordnung: 0 Überläufe, höchstens 16 von 20 Plätzen, "
         f"{de(q2n['oe3_fate_pct']['tx_air'])} % der Einträge gesendet. Die Priorisierung davor lässt eigene "
         "Beacons verhungern.", chip("watch")],
        ["F3 Verlust durch Zeitverfall", f"Nur eigene Beacons gehen wirklich verloren (153 von {q34n_drops} "
         "Verwerfungen). Kein Text, keine Quittung, kein {CET} betroffen.", chip("act")],
        ["F4 Gewinn durch Zeitverfall", f"Frische statt Sendezeit: {de(q34n['drop_toa_pct_of_tx'], 2)} % der Sendezeit "
         "gespart. Ohne Verfall würden nach dem Modell 38 bis 45 % der frischen Beacons an IA/OC später gesendet.",
         chip("ok")],
        ["F5 Zeitsignal", f"Eigene Knoten senden nie veraltete Zeit. Pro Telegramm ca. {de(rad_tot_f, 0)} "
         "Aussendungen; veraltete Zeit kommt aus vier fremden Relays und einem fremden Gateway.",
         chip("act", "Handlungsbedarf (Betrieb)")],
        ["F6 Manipulationen", "Flut mit Bremsen, aber die Bremsen wirken nur an OE3XHL; Ping-Strom und "
         "Doppelsendungen sind der größte Hebel.", chip("act")],
    ]
    idx.append(HB.index_row(d))
    b.append(tab(["Frage", "Kurzantwort", "Urteil"], idx))

    # ------------------------------------------------------------------ node matrix
    b.append("<h2>Die acht Knoten im Überblick</h2>")
    links = {k: f"{k}.html" for k, _ in NODES}
    util = hn["chan_util_mean_pct"]
    noise = hn["noise_carpet_p5_dbm"]
    rows = []
    for key, call in NODES:
        r = mrow(call)
        hw = (r["hardware"] if r else "RAK4631 (laut Log, keine Stammdaten)")
        sent, lost, chains = own_hey(key)
        hey_txt = f"{pct(sent, sent + lost, 0)} % ({sent} von {sent + lost})" if sent + lost else "sendet keine eigenen HEY"
        wl, wt = own_wx(key)
        cn = cancel_lines if key == "oe3xhl" else 0
        rows.append([f'<a href="{links[key]}">{call}</a>', hw, place(call),
                     de(util[key]), f"{de(noise[key], 0)}", hey_txt, f"{wl} von {wt}", ttl_node[key], cn])
    b.append("<p><b>Kennzahlen.</b> Rolle aller acht: Relay mit Internet-Gateway. Auslastung ist der Mittelwert der "
             "Kanalauslastung (nur dekodierte Frames plus eigenes Senden). Rauschteppich ist das 5. Perzentil der "
             "Fehlempfangs-RSSI in dBm. „Eigene HEY gesendet“ zählt Episoden (Kette aus Schreiben und Ersetzen) "
             "= gesendet geteilt durch (gesendet + durch Verfall verworfen); bezogen auf alle geschriebenen HEY "
             f"sind es an OE3XOC {de(ob['oe3xoc/own_HEY']['effective_sent_pct'], 0)} % und an OE3XIA "
             f"{de(ob['oe3xia/own_HEY']['effective_sent_pct'], 0)} %. WX = eigene Wetter-Position, durch Verfall "
             "verloren. CANCEL = Zeilen „[NBR]|CANCEL“. HB9VQQ-5 wurde ohne [LOG]-Zeilen mitgeschnitten: HEY, WX und Verfalls-Drops "
             "sind aus den Ring-Zeilen rekonstruiert, CANCEL ist nicht messbar; bei HB9VQQ-4 gibt es keine "
             "[NBR]-Zeilen, die stillen Freigaben stehen in Klammern.</p>")
    b.append(tab(["Knoten", "Hardware", "Standort, Höhe", "Auslastung %", "Rauschteppich dBm",
                  "Eigene HEY gesendet", "Eigene WX verloren", "Verfalls-Drops", "CANCEL"],
                 rows + HB.matrix_rows(d), num=(3, 4, 7, 8)))
    verd = {
        "oe3xhl": ["ok", "ok", "ok", "ok", "watch", "ok"],
        "oe3xia": ["act", "watch", "act", "ok", "watch", "act"],
        "oe3xir": ["watch", "watch", "watch", "ok", "watch", "act"],
        "oe3xoc": ["watch", "act", "act", "ok", "watch", "act"],
        "oe3xwj": ["watch", "ok", "ok", "ok", "watch", "act"],
        "db0ais": ["watch", "ok", "ok", "ok", "watch", "watch"],
    }
    vrows = [[f'<a href="{links[key]}">{call}</a>'] + [chip(v) for v in verd[key]] for key, call in NODES]
    vrows += HB.verdict_rows()
    b.append("<p><b>Urteile je Frage.</b> Die Begründung steht auf der jeweiligen Knotenseite.</p>")
    b.append(tab(["Knoten", "Gesundheit", "F2", "F3", "F4", "F5", "F6"], vrows))
    b.append("<p class=\"small\">Abstände der Knoten untereinander (Luftlinie, aus den Stammdaten): "
             "OE3XHL−OE3XIA 28,2 km, OE3XHL−OE3XOC 27,0 km, OE3XIA−OE3XOC 22,6 km, "
             "OE3XOC−OE3XWJ 46,6 km, OE3XIA−OE3XWJ 67,4 km, OE3XHL−OE3XWJ 69,3 km. "
             "OE3XIR hat keine Koordinaten. DB0AIS-12 liegt ca. 524 bis 593 km von den OE3-Knoten. "
             f"HB9VQQ-4 und HB9VQQ-5 liegen ca. {de(d.hb9x['setting']['distance_km'])} km auseinander; die Schweizer "
             "Knoten sind ein getrenntes Netz.</p>")

    # ------------------------------------------------------------------ Sechs Befunde
    xoc, xia = ob["oe3xoc/own_HEY"], ob["oe3xia/own_HEY"]
    sx, lx, _ = own_hey("oe3xoc")
    sa, la, _ = own_hey("oe3xia")
    wl_oc, wt_oc = own_wx("oe3xoc")
    wl_ia, wt_ia = own_wx("oe3xia")
    b.append("<h2>Sechs Firmware-Befunde in Reihenfolge der Priorität</h2>")
    b.append("<p>Jeder Befund trägt seinen Status: <b>bestätigt im Code</b> (Verhalten im Log und Code-Stelle "
             "gelesen), <b>Verdacht</b> (Verhalten belegt, Ursache Vermutung) oder <b>Design</b> (Absicht oder "
             "Parametrisierung, keine Codefehler im engeren Sinn). Das Feldbuild ist unbekannt; die Code-Stellen "
             "gelten am Stand 651e0920.</p>")

    b.append('<h3 id="fw1">1 · Auto-Ping: Unterschied ESP32 / RAK4631, Entscheidung offen</h3>')
    b.append(st(chip("watch", "Unterschied, Entscheidung offen"), chip("neu", "kein Bug, keine PR-Vorlage bis zur Entscheidung")))
    b.append(ul([
        "<b>ESP32:</b> die Schleife sendet nur, solange <code>node_pingcount</code> größer 0 ist, und zählt es "
        "je Ping herunter; nach <code>node_pingmax</code> Pings ist Schluss "
        "(<code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code>).",
        "<b>RAK4631 (nRF52):</b> die Schleife hat keinen Zähler. Sie sendet alle <code>node_pingtime</code> "
        "Sekunden, solange <code>node_pingcall</code> gesetzt ist "
        "(<code>src/nrf52/nrf52_main.cpp:2717-2737</code>).",
        "<b>Folge:</b> <code>--ping stop</code> setzt nur <code>node_pingcount</code> auf 0 "
        "(<code>src/command_functions.cpp:3886</code>) und bleibt auf der RAK4631 wirkungslos; nur das Löschen "
        "von <code>--pingcall</code> hilft. Upstream dev 147812e0 hat denselben Code.",
        "<b>Entscheidung offen:</b> ob die RAK4631 wie der ESP32 zählen soll oder der Dauerping gewollt ist, "
        "entscheidet der Betreiber. Bis dahin ist das ein Plattformunterschied, kein Fehler.",
        f"<b>Wirkung in diesem Mitschnitt:</b> OE3XWJ-12 pingt die Gruppe 2025 jede Minute ({pings}×), "
        f"OE3XIR-12 pingt „255.“ jede Minute ({ir_err}×).",
    ]))
    b.append(X("pingplat", D.ping_platform(), "Gleiche Einstellung, zwei Plattformen: der ESP32 zählt den Ping herunter, die RAK4631 pingt ohne Ende"))

    b.append('<h3 id="fw2">2 · Das Ping-Ziel wird nicht geprüft</h3>')
    b.append(st(chip("act", "bestätigt im Code")))
    b.append(ul([
        "<b>Beobachtung:</b> der eigene Auto-Ping von OE3XIR-12 (<code>node_pingcall</code> = „255.“) wird jede "
        "Minute in den Ring geschrieben (die Quelle „phone_msg“ ist nur das Ring-Etikett von "
        "<code>sendPing</code>, kein App-Client), gewinnt als Prio 1 die Auswahl und stirbt beim Dekodieren mit "
        "„Destination-CallSign Error [255.]“ (<code>src/aprs_functions.cpp:369-373</code>, Slot frei "
        "<code>src/lora_functions.cpp:3270-3274</code>).",
        f"<b>Umfang:</b> {ir_dec} von {ir_tot} Ring-Schreibvorgängen ({pct(ir_dec, ir_tot)} %), 0 gesendet, "
        f"{ir_err} Fehlerzeilen; "
        f"jeder Eintrag kostet einen CAD-Zyklus ({ir_nocad} CAD_FREE_NO_TX). Die Zeile TX_FRAME wird trotzdem gedruckt.",
        "<b>Lücke:</b> weder <code>--pingcall</code> (<code>src/command_functions.cpp:3806-3831</code>) noch "
        "<code>sendPing</code> (<code>src/loop_functions.cpp:3820</code>) prüft das Ziel.",
        "<b>Wie „255.“ in die Einstellung kam:</b> Vermutung (Konfigurationsimport oder älterer Build).",
    ]))
    b.append(X("p255", D.ping_255(str(ir_err), ir_hours), "Der Ping an „255.“ stirbt erst beim Dekodieren, nachdem er Auswahl und Kanalprüfung belegt hat"))

    b.append('<h3 id="fw3">3 · Eigene Beacons verhungern an vollen Knoten</h3>')
    b.append(st(chip("act", "bestätigt im Code"), chip("neu", "Design (Lücke im QRT-Plan)")))
    b.append(ul([
        f"<b>Beobachtung:</b> eigene HEY gehen an OE3XOC in {pct(sx, sx + lx)} % der Episoden auf Funk "
        f"({sx} von {sx + lx}), an OE3XIA in {pct(sa, sa + la)} % ({sa} von {sa + la}). Bezogen auf alle "
        f"geschriebenen HEY sind es {de(xoc['effective_sent_pct'], 0)} % bzw. {de(xia['effective_sent_pct'], 0)} % "
        "(Nenner immer nennen: Episoden = geschrieben minus ersetzt). Eigene WX verloren: "
        f"OE3XOC {wl_oc} von {wt_oc}, OE3XIA {wl_ia} von {wt_ia}, OE3XIR 5 von 22, OE3XWJ 4 von 50, OE3XHL 0 von 49, "
        "DB0AIS 0 von 50. Die WX-Position von OE3XOC war 7,64 h nicht auf Funk "
        "(31.10.2026 21:14 bis 01.11.2026 04:52).",
        f"<b>Kein Kapazitätsproblem:</b> die Sendeauslastung an OE3XOC liegt nur bei "
        f"{de(d.q34['nodes']['oe3xoc']['tx_duty_pct'])} %; es ist ein Zugriffsproblem auf den Kanal, keine Sendezeitgrenze.",
        f"<b>Mechanik, drei Teile (geprüft):</b> (a) die strikte Prioritätsauswahl blockiert {rca['pick_blocked_pct_lo']} "
        f"bis {rca['pick_blocked_pct_hi']} % der Kopfzeit eines verlorenen Beacons "
        "(<code>src/txring_functions.cpp:213-251</code>); (b) die 5,5-s-Haltezeit für Prio 4/5 startet bei jedem "
        "empfangenen Frame neu, und der Altersboden überspringt Prio 4/5 (<code>src/csma_timing.h:99-111</code>): "
        f"OE3XOC machte nur {rca['oc_cad_attempts_at_head']} CAD-Versuche in {rca['oc_head_time_s']} s am Kopf; "
        f"(c) an OE3XIA scheiterte CAD_BUSY {rca['ia_cad_busy_failed']}.",
        f"<b>Eigen gegen Relay, gleiche Priorität:</b> eigene WX gehen zu {rca['oc_own_wx_lost_pct']} % verloren, "
        f"relayte Positionen zu {de(rca['oc_relay_pos_lost_pct'])} %; die Wartezeit der eigenen HEY ist im "
        f"Median {de(rca['oc_wait_p50_s']['own_hey'])} s gegen {de(rca['oc_wait_p50_s']['relay_hey'])} s beim Relay.",
        "<b>Ursache (<a href=\"verify/rca_own_beacons.md\">verify/rca_own_beacons.md</a>):</b> am Kopf der Warteschlange "
        "bekommt ein eigener Beacon die 5,5-s-Haltezeit, die jeder empfangene Frame neu startet "
        "(<code>src/lora_functions.cpp:2746-2747</code>). Ein Relay gleicher Priorität läuft über den NBR-Pfad "
        "(<code>src/lora_functions.cpp:3998</code>, nur mit bNBRCANCEL) und bekommt im Fall A nach 8 s Wartezeit "
        "eine Haltezeit von 150 bis 220 ms (<code>src/txring_functions.cpp:385-405</code>). Eigene Beacons sind "
        "von diesem Pfad und vom Altersboden ausgeschlossen (<code>src/csma_timing.h:101</code>).",
        f"<b>Messung am Kopf (OE3XOC):</b> ein eigener Beacon am Kopf sendet {de(rca['oc_head_tx_per_min']['own_hey'], 2)} "
        f"bis {de(rca['oc_head_tx_per_min']['own_wx'], 2)} Mal pro Minute, ein Relay am Kopf "
        f"{de(rca['oc_head_tx_per_min']['relay_pos'], 0)} bis {de(rca['oc_head_tx_per_min']['relay_hey'], 0)} Mal. "
        "Ausgeschlossen sind Auswahlreihenfolge, READY/DONE, Cancel, Sende-Abstandssperre und die WX-Phase. "
        "Da Relays den Fall-A-Pfad nehmen, läuft bNBRCANCEL an OE3XOC (abgeleitet); die fehlenden CANCEL-Zeilen "
        "folgen aus Befund 5.",
        f"<b>Zweiter Beitrag:</b> ca. {rca['behind_ack_pct']} % der Lebenszeit eines verworfenen Beacons verbringt "
        f"er hinter Prio-1-ACK-Weiterleitungen ({rca['ack_fwd_n']} in 25 h an OE3XOC).",
        "<b>Einordnung:</b> Designlücke, kein Codefehler. Der QRT-Plan sagt „Prio 4-5 steigen nie auf“ "
        "(<code>docs/meshcom-lora-qrt-fix-plan-20261018.md:332</code>), aber der Fall-A-Pfad lässt Prio-4/5-Relays "
        "bereits aufsteigen; eigene Beacons sind die einzige Klasse ohne Ausweg.",
        "<b>Kleinster Fix-Kandidat (nicht umgesetzt, ungetestet):</b> den Altersboden auf Prio 4/5 für Nicht-Relay-Slots "
        "ausdehnen (<code>csmaApplyAgeFloor</code> oder begrenzt in <code>csma_compute_timeout_slot</code>, "
        "<code>src/lora_functions.cpp:4030-4043</code>). Eine TTL pro Klasse allein ließe eigene Beacons nur länger "
        "warten, ohne sie auf Funk zu bringen.",
        f"<b>Offen:</b> die Verluste an OE3XOC häufen sich nachts ({rca['night_losses']}), obwohl die Stundenlast flach ist.",
    ]))
    b.append(X("ownrelay", D.own_vs_relay(f"{de(rca['oc_head_tx_per_min']['relay_pos'], 0)}-{de(rca['oc_head_tx_per_min']['relay_hey'], 0)}",
                                          f"{de(rca['oc_head_tx_per_min']['own_hey'], 1)}-{de(rca['oc_head_tx_per_min']['own_wx'], 1)}",
                                          de(rca['oc_rx_per_min_own_head'], 0)),
               "Gleiche Priorität am Kopf der Warteschlange: das Relay hat einen Ausweg, der eigene Beacon wartet auf eine Funkpause"))

    b.append('<h3 id="fw4">4 · ACK-Weiterleitungen haben keinen Overhear-Cancel</h3>')
    b.append(st(chip("act", "bestätigt im Code")))
    ah = V["ack_after_heard"]
    b.append(ul([
        "<b>Code:</b> ACK-Weiterleitungen werden als Art OTHER eingereiht (<code>src/lora_functions.cpp:661</code>); "
        "der Cancel-Scan läuft nur für Frames mit „:“, „!“ oder „@“ in RELAY-Slots "
        "(<code>:1327</code>, <code>:1359</code>).",
        f"<b>Wirkung (Obergrenze):</b> {de(ah['air_pct'])} % der Cluster-Sendezeit ({ah['air_s_total']} s von "
        f"{de(air_all, 0)} s) sind Relay-Sendungen, die nach einer gehörten Fremdkopie erfolgten (ACK {ah['ack_air_s']} s, "
        f"übrige {ah['other_air_s']} s). ACK-Relays nach gehörter Kopie: {ah['hl']} (HL), {ah['ia']} (IA), "
        f"{ah['ir']} (IR), {ah['oc']} (OC) von {ah['ack_relay_tx_total']} ACK-Sendungen. Das ist eine Obergrenze: "
        "die gehörte Kopie muss nicht dieselben Hörer abdecken.",
    ]))
    b.append(X("ack", D.ack_no_cancel(str(ah['hl']), str(ah['ia']), str(ah['ir']), str(ah['oc'])),
               "ACK-Weiterleitungen werden vom Cancel-Scan nie gesehen"))

    b.append('<h3 id="fw5">5 · {CET}-Server-Slots sind immer „Alleinversorger“</h3>')
    b.append(st(chip("watch", "Verdacht (Design / Parameter)")))
    b.append(ul([
        "<b>Beobachtung:</b> jede [NBR]|NEED-Zeile für eine {CET}-ID ist Fall A (666 von 666 an den fünf OE3-Knoten, "
        "349 von 349 bei DB0AIS), 0 CANCEL. OE3MIF-12 und OE3NRA-1 stehen in 95,6 bis 100 % der NEEDs in der "
        "Alleinversorger-Menge, obwohl jeder Knoten sie direkt hört.",
        "<b>Korrektur der Frage:</b> die erste Lesart „need = alone“ ist widerlegt (gilt in 3 von 666). Richtig "
        "gefragt: warum ist die Alleinversorger-Menge für einen Server-Slot nie leer? Die Erklärung „12-h-Fenster“ "
        "ist falsch: Fall A steht schon in den ersten 60 Betriebsminuten bei 100 %, und gespiegelte Kanten können "
        "die Rückverbindung liefern (<code>src/nbr_matrix.cpp:1829-1848</code>).",
        "<b>Angriffspunkt:</b> Abhängigen-Regel und Alternativversorger-Test "
        "(<code>src/nbr_matrix.cpp:1777</code>, <code>:1816-1852</code>). Beobachten und Design.",
        f"<b>Folge:</b> {gf_pct} % ({gf_n} von {gf_tot}) unserer Gateway-Sendungen folgten einer bereits gehörten "
        "Aussendung derselben ID und wurden nicht zurückgenommen.",
    ]))
    b.append(X("srv", D.server_slot("666", "666", gf_pct), "Für Server-Slots ist die Alleinversorger-Menge nie leer, also immer Fall A und nie Cancel"))

    b.append('<h3 id="fw6">6 · OE3XIA ist zeitweise taub</h3>')
    b.append(st(chip("watch", "Verdacht (Bench-Hypothese)")))
    vx = V["xia_deaf"]
    ol_max = max(others_longest.values())
    b.append(ul([
        f"<b>Beobachtung (alle Empfänge gezählt, ACKs eingeschlossen):</b> {n_sil} Stillen von mehr als 5 Minuten, "
        f"zusammen {sil_s} s ({sil_pct} % der Aufzeichnung); die längste dauert {sil_long} s (ab {sil_long_start}). "
        f"Alle begannen ab {sil_first}. Kein anderer Knoten hat eine Stille über 5 Minuten; "
        f"deren längste Lücke ist {de(ol_max, 0)} s (OE3XWJ-Stillstand), sonst unter 90 s.",
        f"<b>Kein Logger-Verlust:</b> die größte Lücke zwischen zwei Logzeilen in den Stillen ist {de(vx['max_log_line_gap_s'])} s, "
        f"und die Firmware meldet selbst in allen {sil['util_samples_inside']} CHANNEL_UTIL-Proben in den Stillen rx=0.",
        f"<b>Keine Übersteuerung:</b> die Kanalprüfung sieht den Kanal in {vx['cad_busy_inside']} von {sil['cad_scans_inside']} "
        f"Scans belegt, und der eigene Sender wurde von den anderen {vx['tx_heard_by_others']} Mal gehört. Senden "
        "läuft mit etwa einem Zwölftel der normalen Rate (ohne Empfang gibt es nichts zu relaisen).",
        f"<b>Erholung:</b> {sil['ends_with_cad_before']} von {n_sil} Stillen enden innerhalb von 3 s nach einem "
        f"Standby+CAD-Zyklus (Grundrate {de(sil['other_rx_with_cad_before_pct'])} %). Das alle 4,8 s wiederholte "
        "Radio.Rx() ohne Standby (<code>src/nrf52/nrf52_main.cpp:1631</code>) holt den Empfänger nicht heraus.",
        "<b>Deutung (Verdacht):</b> der SX1262 bleibt im Empfangszustand hängen (keine RX-Interrupts). Funkstandort-Ursachen "
        "sind damit nicht ausgeschlossen, aber weniger wahrscheinlich.",
        "<b>Bench-Test:</b> <code>Radio.Standby()</code> vor dem Neustart im RX-Watchdog, dazu eine Sperre „N Minuten ohne RX "
        "→ Standby + Rx“. Ungeprüft, abhängig vom Build.",
    ]))
    b.append(X("deaf", D.deaf_rx(str(n_sil), sil_pct, sil_long),
               "OE3XIA hört nichts, obwohl Kanalprüfung, Senden und Empfangs-Neustarts laufen"))

    # ------------------------------------------------------------------ Befundtabelle
    b.append("<h2>Befundtabelle Firmware</h2>")
    b.append("<p>Nur Beobachtungen mit Code-Stelle, die gelesen wurde. „Verdacht“ heißt: Verhalten belegt, Ursache oder "
             "Absicht nicht entschieden. Nummer 1 bis 6 sind die oben ausgeführten Befunde in Prioritätsfolge. "
             "Nummer 12 bis 14 stammen aus dem Schweizer Netz; dort wurde kein Firmware-Fehler gefunden.</p>")
    sus = [
        ["1", "Auto-Ping ohne Zähler auf der RAK4631, <code>--ping stop</code> wirkungslos",
         f"{pings} Pings OE3XWJ jede Minute; ESP32 stoppt nach node_pingmax ({R('pingplat')})",
         "<code>nrf52_main.cpp:2717-2737</code> gegen <code>esp32_main.cpp:5264,5285</code>, "
         "<code>command_functions.cpp:3886</code>", chip("watch", "Unterschied, Entscheidung offen"), chip("watch", "Entscheidung")],
        ["2", "Ping-Ziel „255.“ wird eingereiht, erst beim Dekodieren verworfen",
         f"{ir_err} Fehlerzeilen, 0 gesendet ({R('p255')})",
         "<code>aprs_functions.cpp:369-373</code>, <code>lora_functions.cpp:3270-3274</code>, "
         "<code>command_functions.cpp:3806-3831</code>, <code>loop_functions.cpp:3820</code>",
         chip("act", "bestätigt im Code"), chip("act", "Hoch")],
        ["3", "Eigene Beacons (Prio 4/5) verhungern; Relays gleicher Prio haben Ausweg",
         f"OE3XOC {lx + wl_oc} und OE3XIA {la + wl_ia} eigene HEY/WX verloren ({R('ownrelay')})",
         "<code>lora_functions.cpp:2746-2747,3998</code>, <code>txring_functions.cpp:213-251,385-405</code>, "
         "<code>csma_timing.h:99-111</code>", chip("act", "bestätigt im Code, Design"), chip("act", "Hoch")],
        ["4", "ACK-Weiterleitung ohne Overhear-Cancel",
         f"{ah['ack_relay_tx_total']} ACK-TX; {ah['hl']}/{ah['ia']}/{ah['ir']}/{ah['oc']} nach Kopie ({R('ack')})",
         "<code>lora_functions.cpp:661,1327,1359</code>", chip("act", "bestätigt im Code"), chip("act", "Hoch")],
        ["5", "{CET}-Server-Slot: Alleinversorger-Menge nie leer, Fall A 100 %",
         f"666 von 666 NEED Fall A; {gf_pct} % Doppelsendung ({R('srv')})",
         "<code>nbr_matrix.cpp:1777,1816-1852</code>, <code>nbr_matrix.h:125</code>, "
         "<code>gw_udp_ring.h:38-70</code>", chip("watch", "Verdacht (Design)"), chip("watch", "Mittel")],
        ["6", "OE3XIA: Empfangsstillen bei laufendem Senden und Kanalprüfung",
         f"{n_sil} Stillen, {sil_pct} % der Zeit, rx=0 ({R('deaf')})",
         "<code>nrf52/nrf52_main.cpp:1631</code> (RX-Watchdog)", chip("watch", "Verdacht (Bench-Hypothese)"), chip("act", "Hoch")],
        ["7", "Dedup-Ring zählt Einträge statt Zeit",
         f"Fenster 17,7 bis 26,4 min; {V['dedup_rerelay']['n']} Re-Relays nach {de(V['dedup_rerelay']['gap_min_lo'])}-"
         f"{de(V['dedup_rerelay']['gap_min_hi'])} min ({R('dedup')})",
         "<code>dedup_functions.cpp:18-66</code>", chip("neu", "bestätigt (Verhalten)"), chip("watch", "Mittel")],
        ["8", "Supersede setzt die Frist zurück",
         f"HEY-Ketten 537 bis 902 s; Verfall 180 s nach dem letzten Ersetzen ({R('chain')})",
         "<code>txring_functions.cpp:1007</code>", chip("neu", "bestätigt, harmlos"), chip("ok", "Niedrig")],
        ["9", "RING_DROP_STALE ohne Typ und Quelle",
         f"5 Verwerfungen vor Logbeginn nicht klassifizierbar ({R('stalelog')})",
         "<code>txring_functions.cpp:639</code>", chip("neu", "bestätigt (Diagnose)"), chip("ok", "Niedrig")],
        ["10", "CSMA_AGED ohne Versuchszähler und „heute“-Wert", "Wirkung nicht aus dem Log messbar",
         "<code>lora_functions.cpp:4040</code>", chip("neu", "bestätigt (Diagnose)"), chip("ok", "Niedrig")],
        ["11", "DB0AIS: Uhr springt um −1 h ohne Logzeile", f"18:20:3x Knotenzeit ({R('clock')})",
         "<code>clock.cpp:153-165</code>", chip("watch", "Verdacht (Vermutung)"), chip("ok", "Niedrig")],
    ]
    sus += HB.fw_rows(d, R)
    b.append(tab(["Nr", "Beobachtung", "Beleg", "Code-Stelle", "Status", "Priorität"], sus))

    # ------------------------------------------------------------------ Gesundheit
    b.append("<h2>Gesundheit und Funkumfeld</h2>")
    b.append("<p><b>Das Cluster ist stabil; zwei Knoten brauchen Aufmerksamkeit: OE3XIA (zeitweise taub, "
             "Befund 6) und OE3XIR (starke lokale Quelle).</b></p>")
    ut = hn["chan_util_mean_pct"]
    nz = hn["noise_carpet_p5_dbm"]
    oe3 = ("oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj")
    ackshare = hn["ack_ping_attribution"]["ack_airtime_share_pct"]
    b.append(ul([
        "<b>Kein Neustart, kein Absturz:</b> Betriebszeit läuft bei allen sechs Knoten monoton, 0 Crash- oder "
        "Watchdog-Treffer, Heap flach (ca. 92 KB, Steigung 0 B/h).",
        "<b>Nichts baut sich auf:</b> Sende-Ring höchstens 16 von 20 Plätzen (OE3XIA, nach dem Schreiben gezählt; "
        "Rückstau zu Logbeginn, danach abgebaut), 0 verdrängte Einträge (RING_DROP_NEW/PRIO), Füllstand ohne Trend "
        f"({de(V['ring']['slope_lo'], 3)} bis +{de(V['ring']['slope_hi'], 3)} Plätze pro Stunde).",
        f"<b>Kanalauslastung:</b> Mittel {de(min(ut[k] for k in oe3))} % bis {de(max(ut[k] for k in oe3))} % "
        f"(OE3), DB0AIS {de(ut['db0ais'])} %. Gezählt werden nur dekodierte Frames und eigenes Senden; Energie "
        "aus Fehlempfängen fehlt, die echte Belegung ist höher.",
        f"<b>Rauschteppich:</b> {de(nz['oe3xhl'], 0)} dBm (OE3XHL) bis {de(nz['oe3xwj'], 0)} dBm (OE3XWJ, 8 dB über "
        "dem theoretischen −114 dBm). Der Wert ist das 5. Perzentil der Fehlempfangs-RSSI; ein zweiter Schätzer "
        "liegt innerhalb 1 dB. Einschränkung: bei Header-Fehlern kann die RSSI veraltet sein.",
        "<b>Fehlempfangs-Anteil</b> (an allen Empfangsereignissen einschließlich ACK-Empfängen, Schwelle 40 %): "
        f"OE3XHL {de(rxe['oe3xhl'])} %, OE3XIA {de(rxe['oe3xia'])} %, OE3XIR {de(rxe['oe3xir'])} %, "
        f"OE3XOC {de(rxe['oe3xoc'])} %, OE3XWJ {de(rxe['oe3xwj'])} %, DB0AIS {de(rxe['db0ais'])} %. Nur "
        "DB0AIS liegt über der Schwelle (Beobachten); die OE3-Knoten sind in Ordnung.",
        f"<b>Quittungen dominieren die Luft:</b> ACK-Frames sind {de(min(ackshare[k] for k in oe3), 0)} bis "
        f"{de(max(ackshare[k] for k in oe3), 0)} % der dekodierten Sendezeit der OE3-Knoten, bei DB0AIS "
        f"{de(ackshare['db0ais'])} %. Ursache siehe F6 (Ping-Strom).",
        "<b>Gemeinsames Serverereignis:</b> alle fünf OE3-Gateways verloren am 31.10.2026 um 07:22 gleichzeitig "
        "für ca. 39 s die Antwort auf den Server-Herzschlag. Das liegt am Server oder Uplink, nicht an den Knoten.",
        "<b>OE3XWJ-12:</b> ein Funk-Aussetzer von 153 s am 31.10.2026 um 17:03:38 (22 CAD_SAFETY_TIMEOUT, "
        "kein Senden, letzter Empfang 17:03:38, nächster 17:06:11), selbst erholt; 9 Ethernet-Link-Flaps von 1 bis 4 s.",
    ]))
    air_items = [("Hey-Relays", air_s["@"]), ("Positions-Relays", air_s["!"]), ("ACK-Weiterleitungen", air_s["A"]),
                 ("Texte und Server-Inject", air_s[":"])]
    rest = round(air_all - sum(v for _, v in air_items), 1)
    b.append(K.bars(air_items + [("Eigene Sendungen und Pings (Rest)", rest)],
                    f"Abb. {nxt('c_air')}: Sendezeit des Clusters in 25 h nach Art (Summe {de(air_all, 0)} s); "
                    "HEY-Relays sind der größte Block", unit=" s"))
    b.append(f"<p class=\"small\">Rest = Differenz zur Summe der fünf Knoten ({de(air_all, 0)} s), nicht einzeln "
             f"gemessen; er enthält die {pings} eigenen Pings von OE3XWJ. Der Ping-Strom (Pings plus ACK-Relays) umfasst "
             f"{ping_air} s und liegt quer zu diesen Arten.</p>")
    b.append("<h3>OE3XIA-12: Empfänger zeitweise taub</h3>")
    b.append(ul([
        f"<b>Beobachtung:</b> {n_sil} Stillen von mehr als 5 Minuten ohne ein einziges empfangenes Frame (ACK "
        f"eingeschlossen) oder einen Empfangsfehler, zusammen {sil_s} s ({sil_pct} % der Aufzeichnung); die "
        f"längste dauert {sil_long} s (ab {sil_long_start}). Alle begannen ab {sil_first}.",
        f"<b>Gegenprobe:</b> in jeder Stille empfingen die anderen Knoten weiter ({V['xia_deaf']['rx_events_others_lo']} "
        f"bis {V['xia_deaf']['rx_events_others_hi']} Empfänge je Fenster, alle Empfänge gezählt).",
        "<b>Urteil:</b> siehe Befund 6 (Verdacht: Empfangszustand hängt, Standby+CAD löst ihn). Firmware oder "
        "Hardware ist aus dem Log nicht entscheidbar.",
    ]))
    b.append("<h3>OE3XIR-12: starke lokale Quelle</h3>")
    b.append(ul([
        "<b>Beobachtung:</b> 323 Fehlempfänge über −60 dBm (bis −50 dBm) und 15 dekodierte Frames in dieser "
        "Größe, über jede Stunde verteilt, SNR im Median −20 dB. Kein anderer Knoten sieht das; es ist "
        "kein Echo des eigenen Sendens.",
        "<b>Urteil:</b> starke lokale Quelle am Standort OE3XIR, Typ unbekannt (Vermutung). Ein Fehlempfang setzt eine "
        "erkannte Präambel voraus, deshalb ist „kein LoRa“ nicht belegt; ein LoRa-Sender eines anderen Netzes oder "
        f"eines nicht mitgeschnittenen Geräts am Standort ist mindestens so wahrscheinlich. OE3XIR hat "
        f"{de(rxe['oe3xir'])} % Fehlempfangs-Anteil bei {ir_hours} h Aufzeichnung.",
    ]))
    b.append(X("interf", D.interferer(), "Die lokale Quelle am OE3XIR liegt 50 bis 60 dB über dem Rauschteppich"))
    b.append("<h3>Alte Firmware im Funkgebiet</h3>")
    pc_ = hn["pre_cad_events_pct"]
    b.append(ul([
        f"<b>Pre-CAD:</b> {de(min(pc_[k] for k in oe3), 0)} bis {de(max(pc_[k] for k in oe3), 0)} % der "
        "Empfangsereignisse je Knoten stammen von Stationen mit 4.35 oder älter, die ohne Kanalabfrage senden "
        f"(OE3XHL {de(pc_['oe3xhl'])} %, OE3XWJ {de(pc_['oe3xwj'])} %, OE3XIA {de(pc_['oe3xia'])} %, "
        f"OE3XIR {de(pc_['oe3xir'])} %, OE3XOC {de(pc_['oe3xoc'])} %).",
        "<b>Einordnung:</b> das beschreibt die Nachbarschaft, nicht die Gesundheit des mitschneidenden Knotens "
        "(Beobachten, nie Handlungsbedarf für ihn). Kollisionen lassen sich nicht nachweisen (kein "
        "Kollisions-Log). Betreiberaufgabe: Update ansprechen.",
    ]))
    b.append(X("precad", D.pre_cad(), "Pre-CAD-Stationen senden sofort, 4.40a hört erst auf den Kanal"))
    b.append("<h3>Uhren</h3>")
    b.append(ul([
        "<b>OE3XIR:</b> Knotenuhr dauerhaft +3599 s (Zeitzone fest +2), die anderen vier −1 s. Die "
        "Altersrechnung für {CET} läuft in UTC und bleibt richtig; falsch sind nur Anzeige und Log-Zeit.",
        "<b>DB0AIS:</b> Knotenuhr +1 h bis ca. 18:20 Knotenzeit, dann Sprung um −1 h, kurz nach einem echten "
        "Speichern der Einstellungen, ohne Grund im Log. Verdachtsstelle: <code>src/clock.cpp:153-165</code> (Vermutung).",
    ]))
    b.append(X("clock", D.clock_offset(), "Die Knotenuhr von OE3XIR geht 1 h vor, die Altersrechnung bleibt richtig"))

    # ------------------------------------------------------------------ F2
    b.append("<h2>F2 · TX-Puffer mit Slots</h2>")
    b.append("<p><b>Der Slot-Pool arbeitet sauber; das Problem liegt vor ihm: die Rangfolge lässt eigene "
             "Beacons an vollen Knoten warten, bis sie verfallen (Befund 3).</b></p>")
    fate = q2n["oe3_fate_counts"]
    tot = q2n["oe3_total_writes"]
    pc = q2n["oe3_fate_pct"]
    assert sum(fate.values()) == tot
    aw = V["ack_wait"]
    ia_l = d.q2["nodes"]["oe3xia"]["latency"]["by_prio_s"]["1"]
    oc_l = d.q2["nodes"]["oe3xoc"]["latency"]["by_prio_s"]["1"]
    b.append(ul([
        "<b>Mechanik:</b> 0 RING_DROP_NEW, 0 RING_DROP_PRIO, 0 RING_REARM; kein hängender Slot "
        "(RING_STATUS retrying = 0 in jeder Stichprobe); das niedrigste freie Slot wird immer zuerst genommen.",
        f"<b>Bilanz:</b> von {tot} Einträgen gingen {de(pc['tx_air'], 2)} % auf Funk; die Bilanz schließt "
        "mit 0 ungeklärten Einträgen (jeder Eintrag endet mit genau einer Abschlussmarke; das zeigt nicht, dass "
        "jedes gesendete Frame empfangen wurde).",
        "<b>Rangfolge:</b> strikte Priorität (ACK 1, Relay 3, Position 4, HEY 5), innerhalb einer Priorität "
        "der Älteste zuerst, ohne Schutz gegen Aushungern. Der CSMA-Altersboden deckt nur Prio 1 bis 3 "
        "(<code>src/csma_timing.h:99-111</code>).",
        f"<b>Wartezeit echter ACKs (Pings ausgenommen):</b> Median {de(ia_l['p50'])} s (OE3XIA), {de(aw['ir_p50'])} s "
        f"(OE3XIR; Mittel aus Prio 1 ohne die „255.“-Pings), {de(oc_l['p50'])} s (OE3XOC), Maximum {de(aw['max_s'])} s "
        f"gegen 90 s Frist; {aw['ir_ge20_pct']} % der ACKs an OE3XIR warten mindestens 20 s. Bei leerer Warteschlange "
        f"sind es immer noch {de(aw['empty_queue_ia'], 0)} bis {de(aw['empty_queue_oc'], 0)} s (OE3XIA/OE3XOC). "
        "Ursache laut Verifikation (Schluss aus dem Code und einer Stufe im Histogramm nach dem 20-s-Altersboden): die "
        "Haltezeit startet bei jedem empfangenen Frame neu; dazu kommt belegte Kanalprüfung an OE3XIA und OE3XIR.",
        "<b>Geschwindigkeit im Cluster:</b> OE3XWJ sendet gleiche Frames im Mittel am frühesten (mittlerer "
        "Rang 0,24), OE3XIA am spätesten (0,66). Das folgt der örtlichen Kanalbelegung (Vermutung), "
        "nicht einer Firmwaredifferenz.",
        "<b>Nicht getestet:</b> die Wiederholungsleiter lief nie (0 RING_REARM, kein Retry über 0).",
    ]))
    b.append(tab(["Schicksal der Ring-Einträge (OE3, 5 Knoten)", "Einträge", "Anteil %"], [
        ["gesendet", fate["tx_air"], de(pc["tx_air"], 2)],
        ["gelesen, beim Dekodieren verworfen (nur OE3XIR, Ping an „255.“)", fate["tx_noair_decodefail"],
         de(pc["tx_noair_decodefail"], 2)],
        ["durch Overhear-Cancel zurückgenommen (nur OE3XHL)", fate["nbr_cancel"], de(pc["nbr_cancel"], 2)],
        ["durch neueren Eintrag ersetzt (Supersede)", fate["superseded"], de(pc["superseded"], 2)],
        ["durch Zeitverfall verworfen (TTL)", fate["stale_ttl"], de(pc["stale_ttl"], 2)],
        ["am Logende noch wartend", fate["pending_end"], de(pc["pending_end"], 2)],
    ], num=(1, 2)))
    other = [("Dekodier-Fehler (OE3XIR)", fate["tx_noair_decodefail"]), ("Cancel (OE3XHL)", fate["nbr_cancel"]),
             ("Zeitverfall", fate["stale_ttl"]), ("Ersetzt", fate["superseded"]), ("Noch wartend", fate["pending_end"])]
    b.append(K.bars(other, f"Abb. {nxt('c_fate')}: Die {tot - fate['tx_air']} Einträge, die nicht gesendet "
                    f"wurden (Gesamt {tot}, davon {de(pc['tx_air'], 1)} % gesendet)", unit=""))
    b.append(f"<p class=\"small\">Zeitverfall zählt hier {fate['stale_ttl']} (ohne 5 Verwerfungen von OE3XIA vor "
             f"Logbeginn, die sich nicht zuordnen lassen); die Gesamtzahl der Verwerfungen ist {q34n_drops}.</p>")
    b.append("<h3>Beacons verhungern</h3>")
    b.append(ul([
        f"<b>OE3XOC:</b> nur {pct(sx, sx + lx, 0)} % der eigenen HEY-Episoden gehen auf Funk ({lx} von {sx + lx} "
        f"verloren; Abstand zwischen gesendeten p50 {de(xoc['sent_gap_s']['p50'] / 60, 0)} min). Eigene WX: {wl_oc} von {wt_oc} verloren.",
        f"<b>OE3XIA:</b> {pct(sa, sa + la, 0)} % der HEY-Episoden ({la} verloren), {wl_ia} von {wt_ia} WX verloren.",
        "<b>Ursache:</b> siehe Befund 3 (Ausweg nur für Relays) und Abb. " + str(ref.get("ownrelay", "?")) + ".",
    ]))
    b.append("<h3>Wartezeit pro Hop</h3>")
    b.append(ul([
        f"<b>Beobachtung:</b> ACK-Wartezeit im Median {de(ia_l['p50'])} s (OE3XIA), {de(aw['ir_p50'])} s (OE3XIR), "
        f"{de(oc_l['p50'])} s (OE3XOC).",
        f"<b>Selbst bei leerer Warteschlange</b> beträgt der Median {de(aw['empty_queue_ia'])} s (OE3XIA), "
        f"{de(aw['empty_queue_oc'])} s (OE3XOC), {de(aw['empty_queue_ir'])} s (OE3XIR): die Haltezeit wird bei jedem "
        "empfangenen Frame neu gestartet.",
        "<b>Risiko (Vermutung, nicht beobachtet):</b> eine DM-Quittung über drei solche Hops kann die "
        "40-s-Wiederholleiter des Senders überschreiten. Im Log gab es keine DM-Wiederholungen.",
    ]))
    b.append(X("csma", D.csma_chain(), "Drei Hops mit je ca. 21 bis 23 s Wartezeit summieren sich über die Wiederholfrist"))
    b.append("<h3>Ersetzen setzt die Frist zurück</h3>")
    b.append(ul([
        "<b>Beobachtung:</b> Supersede (HEY/Position, gleicher Absender) setzt die Eintragszeit zurück "
        "(<code>src/txring_functions.cpp:1007</code>). Eigene HEY-Ketten lebten 537 (OE3XIA) bis 902 s (OE3XOC). Sie "
        "verfallen trotzdem, aber erst 180 s nach dem letzten Ersetzen: die Frist begrenzt das Alter des Inhalts, "
        "nicht die Wartezeit des Beacons.",
        f"<b>Umfang:</b> Supersede 21 (OE3XHL), 94 (OE3XIA), 16 (OE3XIR), 93 (OE3XOC), 1 (OE3XWJ), 5 (DB0AIS); "
        f"fast nur HEY. {V['ring']['supersede_entries_in_chains']} ersetzte Einträge in {V['ring']['chains']} Ketten "
        f"(OE3XIA {V['ring']['chains_ia']}, OE3XOC {V['ring']['chains_oc']}) endeten ohne Funk, alle durch Verfall.",
    ]))
    b.append(X("chain", D.supersede_chain(), "Eine HEY-Kette lebt weit länger als die 180-s-Frist, weil jedes Ersetzen die Uhr neu startet"))

    # ------------------------------------------------------------------ F3
    b.append("<h2>F3 · Was verlieren wir durch den Zeitverfall?</h2>")
    b.append("<p><b>Nur eigene Beacons gehen wirklich verloren. Kein Text, keine Quittung, kein {CET} wurde "
             "vom Verfall getroffen.</b></p>")
    pcat = q34n["by_prio_cat"]
    own_lost = pcat["p5/own_HEY"] + pcat["p4/own_WX_POS"]
    b.append(K.bars([("Eigene HEY (Prio 5)", pcat["p5/own_HEY"]), ("Fremde HEY (Prio 5)", pcat["p5/relay_HEY"]),
                     ("Eigene WX-Position (Prio 4)", pcat["p4/own_WX_POS"]),
                     ("Fremde Position (Prio 4)", pcat["p4/relay_POS"]),
                     ("Vor Logbeginn (nicht klassifizierbar)", pcat["p4/unknown_prelog"] + pcat["p5/unknown_prelog"])],
                    f"Abb. {nxt('c_drop')}: Die {q34n_drops} Verwerfungen nach Art; zwei Drittel sind eigene Beacons "
                    f"({own_lost}), alle durch Verfall der Frist", unit=""))
    drows = []
    for key, call in NODES[:5]:
        pcn = d.q34["nodes"][key]["by_prio_cat"]
        drows.append([call, d.q34["nodes"][key]["drops_total"], pcn.get("p4/own_WX_POS", 0), pcn.get("p4/relay_POS", 0),
                      pcn.get("p5/own_HEY", 0), pcn.get("p5/relay_HEY", 0)])
    drows.append(["Summe", q34n_drops, pcat["p4/own_WX_POS"], pcat["p4/relay_POS"], pcat["p5/own_HEY"],
                  pcat["p5/relay_HEY"]])
    b.append(tab(["Knoten", "Verwerfungen", "eigene WX", "fremde Position", "eigene HEY", "fremde HEY"],
                 drows, num=(1, 2, 3, 4, 5), raw=False))
    b.append("<p class=\"small\">OE3XIA hat zusätzlich 5 Verwerfungen vor Logbeginn (06:41:14 bis :24), die Summenzeile "
             "ergibt deshalb nur mit diesen 5 die Gesamtzahl. OE3XIR: 11,5 h Aufzeichnung.</p>")
    rl = q34n["relay_class"]
    rpl = q34n["relay_potential_loss"]
    b.append(ul([
        f"<b>Nie gesendet:</b> für alle {q34n_drops} gibt es weder RING_TX_READ noch einen Sende-Log desselben "
        "Eintrags; es gibt keinen Fall „gesendet, dann verfallen“.",
        f"<b>Eigene Beacons:</b> {own_lost} Verluste ({pcat['p5/own_HEY']} HEY, {pcat['p4/own_WX_POS']} WX); kein "
        "anderer Knoten hörte diese IDs. Die WX-Position von OE3XOC war 7,64 h nicht auf Funk (31.10.2026 21:14:16 "
        "bis 01.11.2026 04:52:36). Im Internet blieb sie sichtbar: bei OE3XIA, OE3XOC und OE3XWJ gingen die "
        "WX-Positionen zusätzlich per UDP an den Server (OE3XHL und OE3XIR zeigen keine UDP-Sendezeilen).",
        "<b>Dauer ohne HEY/WX auf Funk (längste Stille):</b> OE3XOC WX 7,64 h, HEY 3,0 h (am Logende abgeschnitten); "
        "OE3XIA WX 2,06 h, HEY 1,0 h; OE3XIR WX 2,0 h; OE3XWJ WX 1,03 h; OE3XHL 0,53 h.",
        f"<b>Fremde Relays:</b> {rpl['relay_total']} Verwerfungen; {rl['redundant_cluster_tx'] + rl['redundant_heard_lower_hop']} "
        f"({pct(rl['redundant_cluster_tx'] + rl['redundant_heard_lower_hop'], rpl['relay_total'], 0)} %) wurden "
        f"anderweitig übertragen (nicht „überflüssig“: ein anderer Knoten sendete die ID, das heißt nicht, dass das "
        f"Gebiet des verwerfenden Knotens sie bekam), {rpl['weak_or_none']} ({pct(rpl['weak_or_none'], rpl['relay_total'], 0)} %) "
        f"sind mögliche Verluste, alle erneuerbare Typen (HEY/Position). Mittlere Erneuerung durch den Absender nach "
        f"{de(rpl['refresh_median_s_positive'], 0)} s.",
        f"<b>{{CET}}:</b> {d.q34['network']['cet']['ring_drop_stale_reason_cet']} Ring-Verwerfungen mit Grund cet. "
        f"{d.q34['network']['cet']['cet_tx_n']} {{CET}}-Frames gesendet, keines älter als 150 s "
        f"(Maximum {d.q34['network']['cet']['max_payload_age_at_tx_s']} s im Knoten-Zeitstempel; an OE3XIR "
        "148,8 s im Logger-Zeitstempel, 1,2 s vor der Sperre; nur "
        f"{d.q34['network']['cet']['n_tx_age_gt_120s']} Frames über 120 s).",
    ]))
    b.append(tab(["Klasse der 74 verworfenen Relays", "Beleg", "Anzahl"], [
        ["anderweitig übertragen (Cluster)", "ein anderer der fünf Knoten sendete die ID nach unserem Schreiben",
         rl["redundant_cluster_tx"]],
        ["anderweitig übertragen (weniger Hops)", "wir hörten eine Kopie, die über ein anderes Relay lief",
         rl["redundant_heard_lower_hop"]],
        ["schwacher Beleg", "Cluster-Sendung vor unserem Schreiben, kein Beweis für Weiterleitung",
         rl["weak_cluster_tx_before"] + rl["weak_heard_same_hop"]],
        ["kein Beleg", "niemand der fünf sendete oder hörte sie wieder", rl["no_evidence"]],
    ], num=(2,), raw=False))
    b.append("<h3>Wie viel Reserve haben die Fristen?</h3>")
    hr = q34n["headroom_pooled"]
    b.append(tab(["Priorität, Frist", "gesendet", "max. Wartezeit gesendet", "≥ 80 % der Frist", "Frist erreicht (verworfen)"], [
        [f"{p} {nm}, {hr[f'p{p}']['ttl_s']} s", hr[f"p{p}"]["n_sent"], f"{de(hr[f'p{p}']['max_sent_lat_s'])} s",
         hr[f"p{p}"]["n_sent_ge80pct_ttl"], hr[f"p{p}"]["n_dropped"]]
        for p, nm in ((1, "ACK"), (3, "Relay"), (4, "Position"), (5, "HEY"))
    ], num=(1, 3, 4), raw=False))
    b.append(f"<p class=\"small\">Die ACK-Frist hat nur Faktor {de(90 / aw['max_s'])} Reserve (max. {de(aw['max_s'])} s von "
             "90 s): wächst die ACK-Last um ca. 40 %, wäre sie die nächste Frist, die zuschlägt. Wartezeiten von "
             "181,4 s und 120,2 s über der Frist sind die 2-s-Granularität der Verfallsprüfung.</p>")
    b.append("<h3>CSMA-Altersboden</h3>")
    ev_txt = ", ".join(f"{c} {csma_nodes[k]['events']}/{csma_nodes[k]['episodes']}"
                       for k, c in (("oe3xoc", "OE3XOC"), ("oe3xia", "OE3XIA"), ("oe3xir", "OE3XIR"),
                                    ("oe3xwj", "OE3XWJ"), ("oe3xhl", "OE3XHL")))
    b.append(ul([
        "<b>Was er tut:</b> wartet ein Prio-1-bis-3-Eintrag länger als 20 s (Stufe 1) oder 60 s (Stufe 2), "
        "wird die Haltezeit nach einem empfangenen Frame verkürzt "
        "(<code>src/lora_functions.cpp:4030-4045</code>, <code>src/csma_timing.h:71-110</code>).",
        f"<b>Umfang:</b> {csma_ev} Ereignisse in ca. {de(round(csma_ep, -1), 0)} Slot-Episoden ({ev_txt}). Nach "
        f"Priorität: Prio 1 {csma_p1} Episoden, Prio 3 nur {csma_p3}, Prio 2 {csma_p2} (alle OE3XWJ). Stufe 2 praktisch nie.",
        "<b>Wirkung (Modell, nicht gemessen):</b> es gibt keinen Lauf ohne Boden. Die Nachrechnung auf echten "
        "Empfangszeitpunkten ergibt 0,6 bis 1,6 s gespart je Episode. Keine Kollisionsspur (Fehlempfänge in den 3 s "
        "nach eigenem Senden: 17,0 vs. 16,3 % OE3XOC, 21,4 vs. 25,3 % OE3XIA, 13,3 vs. 14,8 % OE3XIR, aged vs. nicht aged).",
        f"<b>Doppelsendungen:</b> dieselbe ACK-ID zweimal binnen 120 s: OE3XIA 12, OE3XOC 4, OE3XIR 3, OE3XWJ "
        f"{V['csma_aged']['double_ack_wj']} Paare; das zweite ist an IA/OC/IR immer gealtert. Der Boden erzeugt sie "
        "nicht, verhindert sie aber auch nicht.",
        "<b>Urteil:</b> neutral bis positiv. Er wirkt dort, wo er gebraucht wird (an drei von fünf Knoten ist "
        "Stufe 1 praktisch Dauerzustand), aber nicht auf Prio 4/5, die eigentlichen Opfer des Verfalls (Befund 3).",
        "<b>DB0AIS:</b> 0 CSMA_AGED, 0 Verwerfungen: der Auslöser wurde nie erreicht (längste Wartezeit bis Prio 3 "
        "20,2 s), das ist kein Build-Unterschied.",
    ]))
    b.append(X("stalelog", D.stale_log(), "Die Verwerfungs-Logzeile nennt weder Typ noch Quelle"))

    # ------------------------------------------------------------------ F4
    b.append("<h2>F4 · Was gewinnen wir durch den Zeitverfall?</h2>")
    b.append(f"<p><b>Frische, nicht Sendezeit: der Verfall spart nur {de(q34n['drop_toa_pct_of_tx'], 1)} % der Sendezeit, "
             "hält aber veraltete Beacons und die Warteschlange kurz. Die Gegenrechnungen unten sind ein Modell "
             "(erste Näherung), keine Messung.</b></p>")
    nd = {k: d.q34["nodes"][k]["drop_toa_pct_of_tx"] for k in oe3}
    b.append(ul([
        f"<b>Sendezeit:</b> {de(q34n['drop_toa_s_total'], 0)} s verworfene Sendezeit von {de(q34n['tx_toa_s_total'], 0)} s "
        f"({de(q34n['drop_toa_pct_of_tx'], 2)} %); je Knoten OE3XOC {de(nd['oe3xoc'], 2)} %, OE3XIA {de(nd['oe3xia'], 2)} %, "
        f"OE3XIR {de(nd['oe3xir'], 2)} %, OE3XWJ {de(nd['oe3xwj'], 2)} %, OE3XHL {de(nd['oe3xhl'], 2)} %.",
        "<b>Ring-Schutz:</b> der Ring war real nie über 16 von 20 belegt; selbst ohne Verfall zeigt das Modell "
        "höchstens 17. Der Gewinn ist also kein Überlaufschutz bei dieser Last.",
        "<b>Frische (Modell, Obergrenze):</b> ohne Verfall würden an OE3XIA 45 % und an OE3XOC 38 % der frischen "
        "Prio-3-bis-5-Frames später gesendet (Median +94 s bzw. +66 s); veraltete würden im Median ca. 200 s alt "
        "gesendet (Maximum 644 s). Das Modell ist erster Ordnung (219 von 224 Verwerfungen hatten ein weiteres "
        "Prio-3-bis-5-Frame wartend) und überschätzt beide Werte.",
        "<b>Freigegebene Plätze:</b> Summe der Alter der Verwerfungen = 19801 Slot-Sekunden (OE3XOC; 0,22 von 20 Plätzen "
        "im Mittel), 15110 (OE3XIA), 964 (OE3XIR), 542 (OE3XHL).",
    ]))
    ps_ = q34n["ttl_sens_pooled_ia_ir_oc_wj"]["p5"]["table"]
    ks = ["120", "150", "180", "240", "300"]
    b.append(K.hist([(f"{k} s" + (" (heute)" if k == "180" else ""), ps_[k]["n_over"]) for k in ks],
                    f"Abb. {nxt('c_ttl')}: Wie viele HEY-Frames würden bei anderer Frist verfallen (Prio 5, "
                    "4 Knoten, Modell, erste Näherung); 300 s ließen nur 33 von 156 übrig", unit=" Drops"))
    p4 = q34n["ttl_sens_pooled_ia_ir_oc_wj"]["p4"]["table"]
    b.append(tab(["Frist-Option", "Drops im Modell", "davon eigene", "Preis"], [
        ["Prio 5: 120 s", ps_["120"]["n_over"], ps_["120"]["own"], "7,7 % der HEY verfallen"],
        ["Prio 5: 150 s", ps_["150"]["n_over"], ps_["150"]["own"], "5,0 %"],
        ["Prio 5: 180 s (heute)", ps_["180"]["n_over"], ps_["180"]["own"], "3,0 %"],
        ["Prio 5: 240 s", ps_["240"]["n_over"], ps_["240"]["own"], "bis 4 min alte HEY auf Funk"],
        ["Prio 5: 300 s", ps_["300"]["n_over"], ps_["300"]["own"], "bis 5 min alte HEY auf Funk (−79 % Drops)"],
        ["Prio 4: 60 s", p4["60"]["n_over"], p4["60"]["own"], "10,3 % der Positionen verfallen"],
        ["Prio 4: 120 s (heute)", p4["120"]["n_over"], p4["120"]["own"], "3,2 %"],
        ["Prio 4: 180 s", p4["180"]["n_over"], p4["180"]["own"], "bis 3 min alte Position"],
        ["Prio 4: 240 s", p4["240"]["n_over"], p4["240"]["own"], "bis 4 min alt (−97 % Drops)"],
    ], num=(1, 2), raw=False))
    b.append("<p class=\"small\">Gepoolt über OE3XIA, OE3XIR, OE3XOC, OE3XWJ (OE3XHL nicht modelliert wegen "
             "Cancel-Pfad). Alle Werte sind erste Näherungen und Obergrenzen, auch die für kürzere Fristen: eine "
             "kürzere Frist gibt den Sendeplatz frei, den ein spätes Frame belegte, sodass andere weniger warten. "
             "Eigene Beacons sind 60 % der Prio-5- und 80 % der Prio-4-Verwerfungen. Eine Frist pro Klasse allein "
             "bringt eigene Beacons nicht auf Funk (Befund 3).</p>")

    # ------------------------------------------------------------------ F5
    b.append("<h2>F5 · Zeitsignal {CET}</h2>")
    b.append(f"<p><b>Der Server liefert pünktlich, unsere Knoten senden nie veraltete Zeit. Kosten: ca. {de(rad_tot_f, 0)} "
             "Aussendungen je Telegramm; Risiko: vier fremde Relays und ein fremdes Gateway senden Zeit, die "
             "Minuten alt ist.</b></p>")
    cg = d.q5["network"]["content_gap_s"]
    b.append(ul([
        f"<b>Takt:</b> OE1XAR-33 (Server, keine Funkstation) sendet alle {de(cg['p50'], 0)} s (min {cg['min']}, p90 "
        f"{de(cg['p90'], 0)}); {d.q5['network']['udp_ids_with_any_gateway']} von {d.q5['network']['distinct_ids']} "
        "Telegrammen erreichten das Cluster. Der Inhalt ist UTC, obwohl {CET} dransteht (Logger minus 3600 s "
        "passt auf 0,6 s im Median). Kein falsches {CET}, kein Versatz nach der Zeitumstellung am 25.10.2026.",
        "<b>Alle fünf OE3-Knoten speisen es als Gateway ein</b> (GWI-Zeile mit from=OE1XAR-33). OE3XHL und "
        "OE3XIR drucken nur keine RX-UDP-Zeile.",
        "<b>Lücken:</b> OE3XHL und OE3XWJ verpassten je 2 Telegramme per UDP (1,4 %), alle vier kamen frisch "
        "per Funk (Alter 2 bis 9 s) und wurden weitergeleitet. Eine Lücke von 674 s am 31.10.2026 ist größer als "
        "der Takt; „Server-Neustart“ ist eine Vermutung (der Zähler springt).",
        "<b>Eigene Knoten:</b> 0 von 666 gesendeten {CET}-Frames älter als 150 s (Maximum 148,8 s an OE3XIR, "
        "1,2 s Rand). Die Sperre griff 8 Mal bei der Weiterleitung (Alter 302 bis 2601 s) und 4 Mal bei DB0AIS. Sie "
        "deckt auch den Gateway-Inject-Pfad ab (<code>src/gw_udp_ring.h:39</code>, seit 05a18b36 / v4.40a.10.23, "
        "upstream über #1238).",
    ]))
    b.append(K.bars([("eigene Gateways", rcm["our_gateway"]), ("fremde Gateways (gehört)", rcm["foreign_gateway"]),
                     ("Relays", rcm["relay"])],
                    f"Abb. {nxt('c_cet')}: Aussendungen je {{CET}}-Telegramm im Mittel (zusammen {rad_tot}; "
                    f"{n_tel} Telegramme, Untergrenze)", unit=""))
    b.append(ul([
        f"<b>Trichter:</b> 5 eigene Gateways (immer alle fünf), im Mittel {for_gw} direkt gehörte fremde "
        f"Gateway-Aussendungen (Median {fgw_med} fremde Gateways je Telegramm, davon ca. 2, die nur aus Relay-Pfaden "
        f"erschlossen sind, nicht gehört), im Mittel {relays_pt} Relay-Aussendungen; zusammen 15 bis 28 Aussendungen, "
        f"im Median 20, ca. {rad_air} s Sendezeit je Telegramm, {de(q5o['radiation_airtime_s_per_h'])} s pro Stunde "
        f"({de(q5o['radiation_airtime_pct_of_hour'])} % einer Stunde, Untergrenze).",
        f"<b>Doppelt gesendet:</b> {gf_pct} % unserer Gateway-Sendungen ({gf_n} von {gf_tot}) folgten einer bereits "
        f"gehörten Aussendung derselben ID und wurden nicht zurückgenommen. OE3XOC {gf_node['oe3xoc']} %, "
        f"OE3XIR {gf_node['oe3xir']} %, OE3XIA {gf_node['oe3xia']} %, OE3XWJ {gf_node['oe3xwj']} %, "
        f"OE3XHL {gf_node['oe3xhl']} %.",
        "<b>Ursache:</b> [NBR]|NEED ist für jeden Server-Slot Fall A (666 von 666), nie ein CANCEL (Befund 5). Das ist "
        "das bekannte GWRX-01-Verhalten (<code>docs/rca-gwrx01-20261023.md</code>).",
        "<b>Nutzen unserer Aussendungen (Vermutung):</b> "
        f"{de(q5o['gateway_radiation_followed_by_relay']['our_gateway'][2], 0)} % wurden von einem Relay genau dieses "
        "Pfads gefolgt (OE3XHL 88 %, OE3XWJ 78 %, OE3XIA 61 %, OE3XIR 52 %, OE3XOC 36 %). Das Veto ist also für "
        "OE3XHL und OE3XWJ nicht grundlos, für OE3XOC erzeugen über 60 % der Sendungen keinen sichtbaren Folger.",
        "<b>Anteil an der Sendezeit:</b> 3,0 bis 4,5 % der eigenen Sendezeit je OE3-Knoten (TX 4,3 s/h), "
        "3,1 bis 5,0 % der dekodierten Empfangszeit; bei DB0AIS 11,0 % bzw. 12,0 %.",
    ]))
    ed = [("unsere Gateways (Maximum)", max(v["tx_age_s_logger_based"]["max"] for v in gfn.values()), "d-ok")]
    for row in sorted(stale_rows, key=lambda r: r["p50_s"]):
        ed.append((f"{row['call']} (Median)", float(row["p50_s"]), "d-act"))
    b.append(X("stale", D.stale_cet(ed), "Fremde Relays senden Zeit, die 4 bis 14 Minuten alt ist; unsere Gateways nie über 150 s"))
    b.append("<h3>Veraltete Zeit im Funk: vier fremde Relays und ein fremdes Gateway</h3>")
    b.append(tab(["Relay", "Hardware, Firmware, Standort", "älter als 150 s (ganzer Lauf)", "Alter p50"],
                 [[r["call"], "keine Stammdaten", f"{r['stale']} von {r['total']}", f"{r['p50_s']} s"] for r in stale_rows]
                 + [[gjc["call"] if gjc else "OE3GJC-12",
                     f"{gjc['hardware']}, {gjc['firmware']}, {gjc['description']} ({gjc['altitudeM']} m)" if gjc else "keine Stammdaten",
                     f"{V['oe3gjc']['copies']} Kopien", f"{V['oe3gjc']['age_lo_s']} bis {V['oe3gjc']['age_hi_s']} s (bei OE3XHL)"]],
                 raw=False))
    sv = V["stale_relays_full_run"]
    b.append(ul([
        f"<b>Befund (ganzer Lauf):</b> {sv['four_stale']} von {sv['all_stale']} veralteten Aussendungen "
        f"({de(sv['four_share_of_stale_pct'])} %) kommen von diesen vier Relays ({sv['relay_part']} Relay- plus "
        f"{sv['gateway_part']} Gateway-Aussendungen über 150 s). Im 67-Telegramm-Fenster waren es "
        f"{de(q5o['stale_radiations_share_from_offenders_pct'])} % von {q5o['radiation_age_gt150']['all'][0]} "
        f"Aussendungen ({de(q5o['radiation_age_gt150']['all'][2])} %). Die Alter streuen bei OE3MIF-12 und OE3NRA-1 "
        "eng (Median 327 und 328 s): vermutlich eine feste Haltezeit von 5 bis 6 min; bei OE3MTC-10 und OE3XDC-12 nicht.",
        f"<b>OE3GJC-12:</b> sendet als Gateway selbst veraltete {{CET}}-Zeit ({V['oe3gjc']['copies']} Kopien, "
        f"{V['oe3gjc']['age_lo_s']} bis {V['oe3gjc']['age_hi_s']} s alt bei OE3XHL). Vermutung: ein Build ohne die "
        "Alterssperre (vor 05a18b36 / #1238) oder eine Uhr, die Minuten nachgeht (eine nicht vertrauenswürdige Uhr "
        "schlägt geschlossen fehl, <code>src/cet_age.h:129</code>). Betreiberaufgabe, nicht unsere Firmware.",
        "<b>Schutz bei uns:</b> die Alterssperre stoppte bei der Weiterleitung 8 veraltete {CET} in OE3 "
        "(4 bei DB0AIS). Ohne sie wären diese Kopien neu geflutet worden.",
        "<b>Lange Nachzügler</b> (20 bis 48 min alt) kommen als DUP:n an, weil der Dedup-Ring die ID "
        "vergessen hat. Gestoppt wurden sie durch die Alterssperre (8), die Schleifenprüfung (4) und die "
        "Hop0-Prüfung (7); keiner wurde weitergeleitet.",
    ]))
    b.append(X("fan", D.fanout(own_gw, for_gw, relays_pt, rad_tot, rad_air),
               "Ein Server-Telegramm wird von fünf eigenen und im Mittel 6,5 gehörten fremden Gateways plus ca. 9 Relays gesendet"))
    b.append("<h3>Server-Telegramme: weitere Beobachtungen</h3>")
    b.append(ul([
        "<b>Dedup-Ring vergisst nach ca. 20 bis 48 min</b> (DUP:n bei 1207 bis 2601 s alten Kopien). Für {CET} "
        "begrenzen Sperre, Schleifen- und Hop0-Prüfung den Schaden.",
        "<b>Gate am Dequeue:</b> das Alter wird beim Auslesen geprüft; dazu kommen CAD-Zeit (bis 20 Versuche). "
        "Ein 147,9-s-Eintrag (OE3XIR) geht mit ca. 149 s auf Funk. Grenzfall, in Ordnung.",
        "<b>Dritte Server-SSID OE1XAR-63</b> taucht nur im DB0AIS-Netz auf, Herkunft unbekannt (offene Frage).",
    ]))

    # ------------------------------------------------------------------ DB0AIS
    b.append("<h2>DB0AIS-12 als eigenes Netz daneben</h2>")
    b.append("<p><b>DB0AIS ist ein anderes Netz an einem anderen Tag; die Zahlen stehen neben, nicht im "
             "Vergleich zum OE3-Cluster.</b></p>")
    dbs = d.q34["network"]["db0ais"]
    b.append(tab(["Kennzahl", "OE3-Cluster", "DB0AIS-12"], [
        ["Standort, Höhe", "Sinawarte 832 m, Exelberg 613 m, Buchberg 469 m, Jauerling 910 m (OE3XIR: Hegerberg, keine Stammdaten)", "Moerfelden (JN49HX), 148 m"],
        ["Kanalauslastung Mittel", f"{de(min(ut[k] for k in oe3))} bis {de(max(ut[k] for k in oe3))} %", f"{de(ut['db0ais'])} %"],
        ["ACK-Anteil dekodierte Sendezeit", f"{de(min(ackshare[k] for k in oe3), 0)} bis {de(max(ackshare[k] for k in oe3), 0)} %", f"{de(ackshare['db0ais'])} %"],
        ["{CET}-Anteil eigene Sendezeit", "3,0 bis 4,5 %", "11,0 % (drei SSIDs -33/-62/-63)"],
        ["Verfalls-Verwerfungen / CSMA_AGED", f"{q34n_drops} / {csma_ev}", f"{dbs['ring_drop_stale']} / {dbs['csma_aged']}"],
        ["Eigene Beacons gesendet", f"OE3XOC {pct(sx, sx + lx, 0)} % HEY-Episoden", "168 von 168 HEY, 50 von 50 WX"],
        ["Server-Takt {CET}", "OE1XAR-33 alle 607 s", "OE1XAR-62 alle 602 s, -33 alle 607 s, -63 alle 606 s"],
        ["Fehlempfangs-Anteil", f"{de(rxe['oe3xhl'])} bis {de(rxe['oe3xia'])} %", f"{de(rxe['db0ais'])} % (Beobachten)"],
    ], raw=False))
    b.append(ul([
        "<b>Drei SSIDs verdreifachen die Last:</b> DB0AIS sendet 346 {CET}-Frames in 25 h (13,8 je Stunde, "
        "10,7 s/h); 131 von 151 Zehn-Minuten-Slots tragen alle drei SSIDs. Die drei Takte driften "
        "gegeneinander (602, 607, 606 s).",
        "<b>-33 und -63 teilen eine Zähler-Familie:</b> dieselbe msg_id wird von OE1XAR-33 und ca. 2,9 h später von "
        "OE1XAR-63 benutzt (verschiedene Nutzzeiten, 81 von 142 -63-IDs kollidieren mit einer -33-ID). Das kostet keine "
        "Sendezeit; Risiko (Modell): ein Knoten mit Dedup-Gedächtnis länger als ca. 2,4 bis 2,9 h würde das -63-Telegramm "
        "verwerfen, weil der Vergleich nur die 4 Byte msg_id nutzt (<code>src/dedup_functions.cpp:25-31</code>).",
        "<b>Dedup-Fenster DB0AIS:</b> Median 42 min (31 bis 54 min) aus dem Rohzähler (RX_DEDUP_ADD slot=N/100); der Wert "
        "111,9 min in den Zwischenergebnissen ist falsch. Die OE3-Fenster (17,6 bis 26,4 min) sind unabhängig bestätigt.",
        "<b>ETH-04 (DHCP-Verlust): Beobachten.</b> Der rng_seed-Marker zeigt, dass der F3-Fix im Build ist; F1/F2 sind "
        "abgeleitet. Start-DHCP und zwei Link-Flaps (1 s und 2 s) liefen sauber, die IP blieb erhalten; die "
        "Lease-Erneuerung (Auslöser von ETH-04) kam in 25 h nie vor, der ursprüngliche Auslöser ist also ungetestet.",
        "<b>Herzschlag-Verluste:</b> 77 von 3013 ohne Antwort, 71 davon zwischen 22:00 und 02:59; "
        "die Erholung fiel mit dem Link-Flap zusammen (Pfad-Problem vor dem Knoten, Vermutung).",
    ]))
    b.append(X("ssid", D.ssids(), "Bei DB0AIS kommt jedes Server-Telegramm dreimal, mit drei Absenderkennungen"))
    b.append(X("eth", D.eth04(), "Die Lease-Erneuerung, der Auslöser von ETH-04, lag außerhalb des Mitschnitts"))

    # ------------------------------------------------------------------ Schweiz
    b += HB.section(d, X, R, {
        "p_ack_tx": p_ack_tx, "p_all_tx": p_all_tx, "pings": pings, "drops": q34n_drops,
        "lost_oc": lx + wl_oc, "lost_ia": la + wl_ia, "cancel_lines": cancel_lines, "gjc": gjc_txt,
    })

    # ------------------------------------------------------------------ F6
    b.append("<h2>F6 · Manipulationen: was die Knoten tun und was es dem Netz bringt</h2>")
    b.append("<p><b>Das Netz ist eine Flut mit Bremsen, aber die Bremsen wirken nur an OE3XHL; der Ping-Strom von "
             "OE3XWJ und das Veto machen den größten Teil der vermeidbaren Doppelsendungen aus.</b></p>")
    pos = [
        "<b>Dedup-Ring:</b> 2,3 bis 3,8 Duplikate je neuem Frame gestoppt (jedes Frame wird 3,3 bis 4,8 Mal "
        "gehört).",
        "<b>Flut mit Grenzen:</b> 9748 Relays eingereiht, 8965 gesendet (92,0 %), 587 zurückgenommen (6,0 %), "
        "196 verworfen (2,0 %); hop0 7 %, gwcap 12 bis 18 %, ping-Filter 602/552/1427 Pings nicht weitergeleitet "
        "(IA/IR/OC).",
        f"<b>Overhear-Cancel (OE3XHL):</b> {cancel_lines} CANCEL-Zeilen, davon {V['cancel']['matched']} auf ein "
        f"Relay zuordenbar ({V['cancel']['pct']} % der {V['cancel']['relays']} Relays); ca. 537 s Sendezeit gespart "
        "(HEY 457, Position 117, Text 13); Median 12,0 s nach Einreihen; 51 % der Fall-B-Slots enden im Cancel.",
        "<b>Supersede:</b> 120 ersetzte HEY/Positionen, die neuere geht raus.",
        "<b>Schleifenschutz:</b> 7 Treffer (OE3XIR 2, OE3XWJ 5), die {CET}-Reste mit 1649 bis 1727 s Alter "
        "waren.",
        "<b>{CET}-Sperre:</b> 12 Stopps, 0 veraltete Eigensendungen.",
        "<b>Keine Schleifen, kein Ping-Pong, kein Hop-Wachstum</b> unserer Knoten; Pfade höchstens 8 Einträge.",
    ]
    stale_rx = {k: gfn[k]["rf_age_gt150_n"] for k in gfn if "rf_age_gt150_n" in gfn[k]}
    neg = [
        f"<b>ACK-Weiterleitung ohne Cancel (Befund 4):</b> {ah['ack_relay_tx_total']} Sendungen, davon "
        f"{ah['hl']} (OE3XHL), {ah['ia']} (OE3XIA), {ah['ir']} (OE3XIR), {ah['oc']} (OE3XOC) nach einer gehörten Kopie "
        "im Wartezustand (Obergrenze).",
        f"<b>Ping-Strom von OE3XWJ:</b> {pings} Pings, {ack_tx} ACK-Relays; die ACK-Relays allein sind {p_ack_tx} % "
        f"({ack_tx} von {tot_tx}) der Sendungen und {p_ack_air} % ({ack_air} von {de(tot_air, 0)} s) der Sendezeit des "
        f"Clusters, mit den {pings} Pings {p_all_tx} % und {p_all_air} %. {V['ping']['ack_relay_share_of_all_ack_relays_pct']} % "
        "aller ACK-Relays gehören zu OE3XWJ-Pings.",
        f"<b>Alleinversorger-Veto (Fall A):</b> 100 % von {V['need_case_a']['lines']} NEED-Zeilen an vier Knoten, "
        "0 Cancel; 884 parallele Text-/Positions-/HEY-Paare wurden trotz gehörter Kopie gesendet.",
        "<b>Server-Inject mehrfach:</b> 231 von 362 Frames werden von 4 bis 5 Gateways gesendet; 735 s "
        "Sendezeit redundant (5,4 %).",
        f"<b>Eigene Beacons verhungern (Befund 3):</b> OE3XOC {lx + wl_oc} von 190, OE3XIA {la + wl_ia} Einträgen "
        "verworfen (Ring-Einträge, nicht Episoden).",
        f"<b>Dedup-Fenster kurz:</b> 17,7 bis 26,4 min; {V['dedup_rerelay']['n']} Re-Relays von IDs, die "
        f"{de(V['dedup_rerelay']['gap_min_lo'])} bis {de(V['dedup_rerelay']['gap_min_hi'])} min vorher schon weitergeleitet wurden.",
        "<b>Veraltete {CET} von außen:</b> empfangene {CET}-Kopien älter als 150 s: "
        + ", ".join(f"{c} {stale_rx[k]}" for k, c in (("oe3xhl", "OE3XHL"), ("oe3xia", "OE3XIA"), ("oe3xir", "OE3XIR"),
                                                   ("oe3xoc", "OE3XOC"), ("oe3xwj", "OE3XWJ"))) + ".",
    ]
    neu = [
        "<b>GW-ACK-Paare:</b> zwei Kopien einer ACK-ID gewollt (GWACK-01), 23 Paare.",
        "<b>Pfad anhängen:</b> HEY wächst von 31 auf ca. 72 B (461 auf 854 ms) je Relay-Kette; ermöglicht "
        f"Schleifenschutz und Nachbarmatrix; HEY-Relays sind {pct(air_s['@'], air_all, 0)} % der Cluster-Sendezeit "
        f"({de(air_s['@'], 0)} s).",
        "<b>Fall-B-Wartezeit</b> (zusätzlich 20 s, bis 60 s) nur für Position/HEY an OE3XHL.",
        "<b>Gateway-Upload:</b> 12390 Uploads für 5515 IDs (55 % serverseitig doppelt), keine Funkkosten.",
        "<b>Echo-Tabelle:</b> nur Diagnose; OE3XOC: 135 von 190 eigenen Beacons nie gehört (71 %).",
        "<b>Hop-Zähler H≥13:</b> 24 Empfänge von fremden Builds (OE3XHL 17), unsere Knoten leiten nicht weiter.",
        "<b>CSMA-Altersboden</b> (siehe F3): neutral bis positiv.",
    ]
    b.append('<div class="grid3">'
             '<div class="card pos"><h3>' + chip("pos") + "</h3>" + ul(pos) + "</div>"
             '<div class="card neg"><h3>' + chip("neg") + "</h3>" + ul(neg) + "</div>"
             '<div class="card neu"><h3>' + chip("neu") + "</h3>" + ul(neu) + "</div></div>")
    red = q6n["redundancy_v1_nodes_per_id"]
    ncl = sum(red.values())
    b.append("<h3>Wie viele Knoten senden dieselbe ID?</h3>")
    b.append(K.hist([(f"{k} Knoten", red[k]) for k in ["1", "2", "3", "4", "5"]],
                    f"Abb. {nxt('c_red')}: Von {ncl} IDs senden {pct(red['3'] + red['4'] + red['5'], ncl, 0)} % drei oder mehr "
                    f"Knoten, {pct(red['1'], ncl, 0)} % nur einer", unit=" IDs"))
    b.append("<p class=\"small\">Anteile: " + ", ".join(f"{k} Knoten{'' if k == '1' else ''} {pct(red[k], ncl)} %" for k in ["1", "2", "3", "4", "5"]) +
             ". Die 1529 Einzelsender-IDs bei Texten sind die Pings von OE3XWJ; die 164 "
             "Text-IDs auf allen fünf sind Server-Inject.</p>")
    b.append(ul([
        "<b>Parallel gesendet:</b> 4938 von 19133 Relay-/Inject-Sendungen (25,8 %, 3345 s von 12466 s) sind "
        "die zweite oder spätere Sendung derselben ID auf gleicher Hop-Stufe.",
        f"<b>Obergrenze:</b> {de(ah['air_pct'])} % der Cluster-Sendezeit ({ah['air_s_total']} s) sind Relay-Sendungen nach "
        f"einer gehörten Fremdkopie (ACK {ah['ack_air_s']} s, HEY/Position/Text {ah['other_air_s']} s). Das ist eine "
        "Obergrenze, keine Ersparnis: die gehörte Kopie muss nicht dieselben Hörer abdecken.",
    ]))
    pcl = q6n["redundancy"]["parallel_pair_classes"]
    npair = sum(pcl.values())
    cls_rows = [
        ["A nicht hörbar bei B (fast immer OE3XHL beteiligt)", pcl["hidden:A_not_audible_at_B"], "nein, erklärt"],
        ["gleichzeitig (unter 1,5 s)", pcl["simultaneous(<1.5s)"], "nein"],
        ["hörbar, aber B hat A nicht empfangen", pcl["audible_pair_but_A_copy_not_received"], "meist nein (Verbindung 0,16 bis 0,5)"],
        ["B hörte eine Kopie der ACK-ID im Wartezustand (nicht unbedingt die von A; keine Cancel-Logik)",
         pcl["B_heard_A_while_queued:no_need_line"], "im Prinzip ja (Obergrenze)"],
        ["B hörte A, Fall A mit REFUSE (Veto)", pcl["B_heard_A_while_queued:REFUSE(sole-provider veto)"], "gewollt (Veto)"],
        ["B hörte A, Fall A ohne REFUSE-Zeile", pcl["B_heard_A_while_queued:caseA_no_refuse_line"], "gewollt (Veto)"],
        ["B hörte A, Fall B, Abdeckung unzureichend", pcl["B_heard_A_while_queued:caseB_cover_insufficient"], "gewollt"],
    ]
    b.append(tab([f"Grund der Doppelsendung ({npair} Paare)", "Paare", "Anteil %", "vermeidbar?"],
                 [[a_, n_, pct(n_, npair), c_] for a_, n_, c_ in cls_rows], num=(1, 2), raw=False))
    pp = V["parallel_pairs"]
    b.append(f"<p class=\"small\">Die „vermeidbar“-Spalte ist eine Obergrenze: mindestens {pp['ack_class_min_not_a']} der "
             f"{pp['ack_class_n']} ACK-Paare können nicht A's Kopie sein. Verdeckte Paare: {de(pp['hidden_with_hl_pct'])} % "
             f"betreffen OE3XHL, {de(pp['hidden_wj_ia_pct'])} % sind OE3XWJ↔OE3XIA.</p>")
    lp = q6n["link_probability"]
    nk = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
    lrows = []
    for a_ in nk:
        row = [a_.upper() + "-12"]
        for bb in nk:
            row.append("-" if a_ == bb else de(lp[f"{a_}->{bb}"]["p_nonack"], 2))
        lrows.append(row)
    b.append("<p><b>Hörbarkeit zwischen den Knoten</b> (Anteil der Sendungen des Zeilenknotens, den der "
             "Spaltenknoten mit dieser Station als letztem Hop innerhalb 3 s protokolliert hat, ohne ACKs):</p>")
    b.append(tab(["sendet \\ hört", "OE3XHL", "OE3XIA", "OE3XIR", "OE3XOC", "OE3XWJ"], lrows,
                 num=(1, 2, 3, 4, 5), raw=False))
    b.append("<p class=\"small\">OE3XHL wird praktisch von keinem der anderen vier gehört (OE3XIA: 4 Empfänge, die "
             "anderen 0); OE3XHL hört nur OE3XIA. Die Verbindungen sind einseitig und grenzwertig. Untergrenzen: "
             "ein Knoten kann gerade senden oder kanalprüfen.</p>")
    b.append("<h3>Der Ping-Strom</h3>")
    b.append(ul([
        f"<b>Beobachtung:</b> OE3XWJ-12 sendet {{ping}} an Gruppe 2025 einmal pro Minute ({pings} Mal, hop 4, Prio 2). "
        "97,4 bis 98,9 % aller 12-Byte-ACK-Empfänge der OE3-Knoten quittieren genau diese IDs "
        "(OE3XHL 10001 von 10179, OE3XIA 14101 von 14482, OE3XOC 27330 von 27675).",
        f"<b>Wirkung, mit Nenner:</b> die {ack_tx} ACK-Relays allein sind {p_ack_tx} % der {tot_tx} Sendungen und "
        f"{p_ack_air} % der Sendezeit ({ack_air} von {de(tot_air, 0)} s). Zusammen mit den {pings} Pings sind es "
        f"{ack_tx + pings} Sendungen ({p_all_tx} %) und {ping_air} s ({p_all_air} %).",
        f"<b>Wer quittiert:</b> die ACKs sind Gateway-ACKs fremder Gateways außerhalb der fünf (Gateway-Format bei "
        f"{V['ping']['ack_gateway_format_share']} ACK-Empfängen; keiner der fünf sendet einen ACK selbst), im Mittel "
        f"{pe_mean} verschiedene ACK-IDs je Ping-Ereignis (Median {pe_med}, Maximum {pe_max}; Zählung je Ping, "
        f"nicht je msg_id: die Ping-IDs laufen über 24 h um, {d.wjping['distinct_ping_ids']} verschiedene IDs für {pings} Pings). Im aktuellen Code quittiert "
        "ein Gateway {ping} nie (<code>src/lora_functions.cpp:2169-2173</code>, auch upstream); die ACKenden "
        "Gateways laufen also mit anderen Builds (Vermutung).",
        "<b>Abhilfe hat drei Seiten:</b> die RAK-Ping-Entscheidung (Befund 1), das Testmuster von OE3XWJ-12 (es löst "
        "nur aus) und die Firmware der fremden Gateways.",
    ]))
    b.append(X("ping", D.ping_stream(str(ack_tx), pe_mean),
               f"Ein Ping von OE3XWJ löst bei fremden Gateways ACKs aus; unsere Knoten relaisen im Mittel {de(ack_tx / pings)} ACK-Sendungen je Ping"))
    b.append("<h3>Veto und ACK-Weiterleitung</h3>")
    b.append(ul([
        "<b>Veto (Fall A):</b> Bei OE3XIA, OE3XIR, OE3XOC, OE3XWJ ist die Alleinversorger-Menge groß (Median "
        "OE3XIA 22 Stationen, OE3XOC 7, OE3XWJ 9, OE3XIR 8; OE3XHL 1). OE3MIF-12 und OE3NRA-1 stehen bei allen vier "
        "in 95 bis 100 % der NEEDs als „nur ich versorge sie“, obwohl jeder sie direkt hört (≥ 3 [NBR]|ME-Zeilen).",
        "<b>Erklärung (Schluss, korrigiert):</b> das Veto kommt aus der Abhängigen-Regel (jede direkt gehörte Zeile ist "
        "abhängig) plus dem Test auf eine beobachtete oder gespiegelte Rückkante; es besteht schon in den ersten "
        "Betriebsminuten, das 12-h-Fenster ist also nicht die Ursache. Design/Parameter, kein Codefehler; ob Cancel "
        "an den vier Knoten eingeschaltet ist, ist aus dem Log nicht ableitbar.",
        "<b>ACK:</b> als Art OTHER eingereiht, ohne NEED (<code>src/lora_functions.cpp:656-662</code>); der "
        "Cancel-Scan verlangt Art RELAY (<code>:1359</code>).",
    ]))
    b.append(X("veto", D.veto(), "B hört A, sein Relay wartet, und es sendet trotzdem, weil es sich für allein zuständig hält"))
    b.append("<h3>Dedup-Fenster</h3>")
    b.append(ul([
        "<b>Beobachtung:</b> der Dedup-Ring zählt 100 Einträge, nicht Zeit; bei 3,8 bis 5,7 IDs/min deckt er nur "
        f"17,7 bis 26,4 min ab (<code>src/dedup_functions.cpp:18-66</code>). {V['dedup_rerelay']['n']} Re-Relays von IDs, "
        f"die {de(V['dedup_rerelay']['gap_min_lo'])} bis {de(V['dedup_rerelay']['gap_min_hi'])} min zuvor schon "
        f"weitergeleitet wurden (OE3XWJ {V['dedup_rerelay']['wj']}); OE3XOC nahm 38 fremde IDs erneut an.",
        "<b>Auslöser:</b> ein fremder Knoten strahlt das 22 bis 25 min alte Frame erneut ab (Schluss: Warteschlange "
        "ohne Frist); der Ring hat die ID vergessen und lässt es durch.",
        "<b>Eigene Frames:</b> stehen nicht im Ring, ihre Echos kommen als neu (OE3XWJ: 3668, von den eigenen Pings).",
    ]))
    b.append(X("dedup", D.dedup_window(str(V['dedup_rerelay']['n'])), "Der Dedup-Ring vergisst nach ca. 18 bis 26 Minuten; spätere Kopien gelten als neu"))

    # ------------------------------------------------------------------ Empfehlungen
    b.append("<h2>Empfehlungen</h2>")
    b.append("<h3>Firmware</h3>")
    b.append(tab(["Option", "gemessene oder modellierte Wirkung", "Preis, Risiko"], [
        ["Altersboden auf Prio 4/5 für Nicht-Relay-Slots ausdehnen (eigene Beacons; kleinster Fix-Kandidat)",
         "ungetestet; Erwartung (Schluss): der Kopf-Durchsatz eigener Beacons steigt von 0,4 bis 1,8 je Minute Richtung "
         "Relay-Werte 10 bis 17 je Minute; 32 von 34 verworfenen eigenen WX und 54 von 57 HEY waren mindestens 5 bis 10 s Kopf",
         "ca. 7,6 eigene Beacons/h Zusatzlast an OE3XOC; Prioritätsumkehr zwischen Knoten wird auf Prio 4/5 ausgedehnt; "
         "an OE3XIA bleibt CAD_BUSY"],
        ["Eigene Beacons: TTL nach Herkunft (länger oder ausgenommen), Relays unverändert",
         "Modell, Obergrenze: Prio 5 mit 300 s: 156 → 33 Drops (−79 %); Prio 4 mit 240 s: 69 → 2 (−97 %). Eigene Beacons "
         "sind 60 % bzw. 80 % dieser Drops",
         "Frames bis 4 bis 5 min alt auf Funk; bringt sie allein nicht auf Funk, sie warten nur länger"],
        ["Globale TTL kürzen", "Modell: Prio 5 mit 120 s: 404 Drops (7,7 %); Prio 4 mit 60 s: 221 (10,3 %)",
         "mehr Verluste, vor allem eigene Beacons: nicht empfohlen"],
        ["Auto-Ping der RAK4631: Zähler wie auf dem ESP32 oder Dauerping bewusst belassen",
         f"{pings} Pings OE3XWJ und {ir_err} Läufe OE3XIR in einem Tag", "Betreiberentscheidung; erst danach eine PR-Vorlage"],
        ["Zieladresse beim Einreihen oder in <code>--pingcall</code> prüfen", f"{ir_err} Fehlläufe (OE3XIR) werden vermieden", "gering"],
        ["Overhear-Cancel für ACK-Weiterleitung",
         f"{ah['hl']}/{ah['ia']}/{ah['ir']}/{ah['oc']} ACK-Relays an HL/IA/IR/OC folgen einer gehörten Kopie; Obergrenze "
         f"{ah['ack_air_s']} s ACK-Sendezeit", "Obergrenze, keine Ersparnis; Abdeckung muss geprüft werden"],
        ["Alleinversorger-Test für Server-Slots prüfen (Abhängigen-Regel, Alternativversorger)",
         f"Obergrenze der betroffenen Sendezeit {de(ah['air_pct'])} % ({ah['air_s_total']} s); Server-Inject 735 s (5,4 %) redundant",
         "Gefahr, Hörer ohne Versorger zu lassen (OE3XHL hört niemand); Wirkung aus den Logs nicht messbar"],
        ["Dedup-Ring zeitbasiert statt nach Zahl", f"beseitigt die {V['dedup_rerelay']['n']} Re-Relays nach 22 bis 25 min", "Speicher/Eintrag"],
        ["OE3XIA: Standby vor dem Empfangs-Neustart testen (Bench)", f"{n_sil} Fälle, {sil_pct} % der Zeit", "Hypothese, build-abhängig"],
    ], raw=False))
    b.append("<h3>Betrieb</h3>")
    b.append(ul([
        f"<b>OE3XWJ-12:</b> Ping-Intervall von 1 min auf deutlich länger oder abschalten (auf der RAK4631 durch Löschen von "
        f"<code>--pingcall</code>, nicht mit <code>--ping stop</code>). Erwartung: bis zu {p_all_air} % der Sendezeit "
        f"und {p_all_tx} % der Sendungen im Cluster (Obergrenze, nicht gemessen).",
        "<b>OE3XIR-12:</b> Ping-Ziel „255.“ aus der Einstellung entfernen; Zeitzone von fest +2 auf Regel oder +1 "
        "stellen; die lokale Quelle am Standort suchen.",
        "<b>Fremde Gateways, die Pings quittieren:</b> Betreiber ermitteln und auf aktuelle Firmware bringen (Vermutung: "
        "älterer Build ohne {ping}-Ausnahme).",
        "<b>Fremde Relays OE3MIF-12, OE3NRA-1, OE3MTC-10, OE3XDC-12 und das Gateway OE3GJC-12:</b> Betreiber ansprechen; "
        "Firmware mit {CET}-Alterssperre aufspielen. Stammdaten fehlen für die vier Relays.",
        "<b>OE3XIA-12 Empfänger:</b> Antenne, Stromversorgung und Funkmodul prüfen; Stillen mit einem zweiten "
        "Empfänger gegenprüfen.",
        "<b>OE3XOC-12:</b> Entscheidung, ob die WX-Position auf Funk wichtig ist (7,64 h am Stück nicht gesendet; im "
        "Internet sichtbar).",
        "<b>OE3XWJ-12 Ethernet:</b> 9 Link-Flaps prüfen (Kabel, Switch).",
        "<b>Server:</b> eine einzige Server-SSID je Region würde bei DB0AIS ca. zwei Drittel der {CET}-Last sparen; "
        "{CET} trägt UTC, die Bezeichnung irritiert (Server-Betreiber).",
        "<b>Stationen mit 4.35 und älter</b> auf 4.40 bringen.",
    ]))

    # ------------------------------------------------------------------ Nicht geprüft
    b.append("<h2>Was nicht geprüft ist</h2>")
    b.append("<p><b>Kann Einstellungen kosten</b></p>")
    b.append(ul([
        "Ob der Overhear-Cancel an OE3XIA, OE3XIR, OE3XOC, OE3XWJ aus ist: das Log trennt „aus“ nicht von "
        "„Fall A überall“. (Für OE3XOC folgt aus den Haltezeiten, dass die NBR-Relay-Suche läuft; siehe Befund 3.)",
        "Ob ein zurückgenommener Relay ein Gebiet nicht mehr erreicht: die Hörer von OE3XHL loggen nicht. "
        "Nur 206 von 1027 entfernten Hörern tauchen später im Pfad desselben Frames auf (Abwesenheit eines "
        "Beweises, kein Verlust).",
    ]))
    b.append("<p><b>Nicht auf der Bench</b></p>")
    b.append(ul([
        "Der Fix-Kandidat Altersboden für Prio 4/5 und der Standby-Test für OE3XIA: beide ungetestet.",
        "Die Wiederholungsleiter (0 Mal gelaufen), ob „Retry setzt geparkten Slot neu“ funktioniert.",
        "DM-Quittungen über mehrere langsame Hops gegen die 40-s-Leiter (Vermutung, nicht beobachtet).",
        "Lease-Erneuerung bei DB0AIS (ETH-04): kam nicht vor.",
        "Kollisionen: es gibt kein Kollisions-Log; Hinweise nur indirekt.",
    ]))
    b.append("<p><b>Datenlage</b></p>")
    b.append(ul([
        "Der genaue Build ist unbekannt (nur die Release-Kennung 20260929); Code-Aussagen gelten am Stand 651e0920, "
        "TXP und NBR-Stufe 2 sind aus dem Log-Bild geschlossen.",
        "Es sind nur fünf Beobachtungspunkte: „kein Beleg“ heißt nicht, dass niemand sonst eine ID trug. "
        "Aussendungen unterliegen der Hörbarkeit (Untergrenzen).",
        "OE3XIR endet nach 11,5 h; alle seine Anteile gelten für diese Zeit.",
        "Fristen über oder unter den heutigen sind Modell (erste Näherung, Obergrenzen). Das Modell fügt keine zusätzlichen "
        "Leerlauf-Sendezeitpunkte hinzu.",
        "DB0AIS wurde nie mit OE3 korreliert (anderes Netz, anderer Tag, nur Knotenuhr).",
        "Offen: die nächtliche Häufung der Beacon-Verluste an OE3XOC ist unerklärt.",
        "Schweizer Netz: HB9VQQ-5 ohne [LOG]-Zeilen (Relay-Gründe, STAT, ACK-Zuordnung, NEED/CANCEL nicht messbar); "
        "die Ursache des Cancel-Lochs bei HB9VQQ-4 (15:39 bis 22:39) ist ohne [NBR]-Zeilen offen; die Prüfung hat "
        "Zeitsignal-Takt, Hop-Invariante, Dedup-Umlauf, CSMA-Episodentabelle und Firmware-Zählung nicht geprüft.",
    ]))

    # ------------------------------------------------------------------ Offene Fragen
    b.append("<h2>Offene Fragen an den Betreiber</h2>")
    b.append(ol([
        "Soll der Auto-Ping der RAK4631 wie auf dem ESP32 zählen und stoppen, oder ist der Dauerping gewollt?",
        "Ist der Minuten-Ping von OE3XWJ-12 an Gruppe 2025 gewollt? Wenn ja: reicht ein längeres Intervall?",
        "Wie kam „255.“ als Ping-Ziel in die Einstellung von OE3XIR-12 (Import, älterer Build)?",
        "Läuft der Overhear-Cancel an OE3XIA, OE3XIR, OE3XOC, OE3XWJ?",
        "Ist der Verlust eigener HEY/WX an OE3XOC und OE3XIA akzeptabel, oder soll der Altersboden auf eigene Beacons "
        "ausgedehnt werden? (Entscheidung offen in docs/campaign-tx-pool.md.)",
        "Was ist am Empfänger von OE3XIA-12 (Antenne, Versorgung, Modul) vor Ort anders als an den anderen?",
        "Wer betreibt OE3MIF-12, OE3NRA-1, OE3MTC-10, OE3XDC-12 und OE3GJC-12, und welche Firmware laufen sie?",
        "Wer betreibt die Gateways, die die Pings von OE3XWJ-12 quittieren, und welche Firmware laufen sie?",
        "Was ist OE1XAR-63 (dritte Server-SSID im DB0AIS-Netz)?",
        "Was war serverseitig am 31.10.2026 um 07:22 los (alle fünf Gateways ohne Herzschlag-Antwort, ca. 39 s)?",
        "Warum endet der OE3XIR-Mitschnitt am 31.10.2026 um 18:16?",
        "Warum wurde die DB0AIS-Uhr um −1 h zurückgesetzt?",
        "Läuft bei HB9VQQ-4 <code>--nbrrelay on</code>, und warum gab es von 15:39 bis 22:39 keinen Cancel?",
        "Wird HB9HNO-49 auf 4.40 gebracht, und was staut dort 25 Minuten lang?",
        "Welcher Client übernimmt jede volle Stunde die Konsole von HB9VQQ-5?",
    ]))

    # ------------------------------------------------------------------ Prüfung
    vc = V["verify_counts"]
    tc = sum(v["confirmed"] for v in vc.values())
    tk = sum(v["corrected"] for v in vc.values())
    tr = sum(v["refuted"] for v in vc.values())
    hv = d.hb9x["verify_counts"]
    b.append("<h2>Prüfung</h2>")
    b.append("<p><b>Jede Aussage dieser Seite wurde nach der Auswertung von einem unabhängigen Prüfer neu aus den Roh-Logs "
             f"gemessen und gegen den Code gelesen: {tc} bestätigt, {tk} korrigiert, {tr} widerlegt.</b> Die Prüfer "
             "benutzten eigene Skripte (nicht die der Auswertung) und trugen je Aussage Urteil, eigene Messung und "
             "Korrektur in eine Tabelle ein. Jede Korrektur und Widerlegung ist in diese Seite eingearbeitet; Schlüsse "
             "bleiben als „Vermutung“ gekennzeichnet. Das Schweizer Netz wurde getrennt geprüft: "
             f"{hv['confirmed']} bestätigt, {hv['corrected']} korrigiert, {hv['refuted']} widerlegt "
             f'(<a href="{hv["file"]}">{hv["file"]}</a>); die Summe unten betrifft OE3 und DB0AIS, die Zeile '
             "„Summe mit HB9“ addiert beide.</p>")
    b.append(tab(["Thema", "Datei", "bestätigt", "korrigiert", "widerlegt"],
                 [[v["topic"], f'<a href="{v["file"]}">{v["file"]}</a>', v["confirmed"], v["corrected"], v["refuted"]]
                  for v in vc.values()]
                 + [["Ursache der Beacon-Verluste (RCA)", '<a href="verify/rca_own_beacons.md">verify/rca_own_beacons.md</a>',
                     "–", "–", "–"]]
                 + [["Summe OE3 und DB0AIS", "", tc, tk, tr]]
                 + [HB.verify_table_row(d)]
                 + [["Summe mit HB9", "", tc + hv["confirmed"], tk + hv["corrected"], tr + hv["refuted"]]],
                 num=(2, 3, 4)))
    b.append("<p class=\"small\">Die RCA ist eine Ursachenanalyse (Kopf-Durchsatz eigener Beacons gegen Relays, mit "
             "Ausschlusstabelle), keine Aussagenprüfung; sie enthält deshalb keine Zählung. Skripte der Prüfer liegen "
             "in <code>verify/*_checks/</code>.</p>")
    b.append("<h3>Was die Prüfung an dieser Seite geändert hat</h3>")
    b.append(ul([
        f"<b>CSMA_AGED:</b> {csma_ev} Ereignisse in ca. {de(round(csma_ep, -1), 0)} Episoden (die erste Zusammenfassung "
        "hatte die Summe der Knoten falsch addiert); Prio 3 nur "
        f"{csma_p3} Episoden, die {csma_p2} von OE3XWJ sind Prio 2.",
        f"<b>OE3XIA-Stillen:</b> {n_sil} Stillen, {sil_s} s, längste {sil_long} s ab {sil_long_start}, wenn ACK-Empfänge "
        "mitgezählt werden (die Zählung nur nach OnRxDone-Zeilen verfehlt sie); Ursache neu gefasst: Empfangszustand hängt, keine Übersteuerung.",
        f"<b>Fehlempfangs-Anteile:</b> Nenner jetzt mit ACK-Empfängen; OE3XIA {de(rxe['oe3xia'])} %, OE3XIR "
        f"{de(rxe['oe3xir'])} %; nur DB0AIS bleibt über der 40-%-Schwelle.",
        f"<b>Ping-Anteile mit Nenner:</b> ACK-Relays allein {p_ack_tx} % der Sendungen und {p_ack_air} % der Sendezeit; "
        f"mit den Pings {p_all_tx} % und {p_all_air} %.",
        f"<b>ACK-Herkunft:</b> die Pings werden von fremden Gateways quittiert (Gateway-Format), im Mittel {pe_mean} ACK-IDs je Ping-Ereignis (die Zählung je msg_id mischt zwei Pings je umgelaufener ID).",
        "<b>Fremde Gateways je Telegramm:</b> ca. 6 direkt gehört plus ca. 2 nur aus Relay-Pfaden erschlossen (die Zählung „8 fremde“ mischte beides).",
        "<b>Veralteter {CET}-Anteil:</b> Ganzer Lauf 79,6 % (199 von 250), der Fensterwert gilt nur für 67 Telegramme; Median 817 s für "
        "OE3MTC-10 und 261 s für OE3XDC-12; dazu OE3GJC-12.",
        "<b>Alleinversorger-Frage neu gestellt:</b> „need = alone“ ist widerlegt (3 von 666); die 12-h-Fenster-Erklärung "
        "ist falsch.",
        f"<b>ACK-Wartezeit OE3XIR:</b> {de(aw['ir_p50'])} / {de(aw['ir_p90'])} / {de(aw['ir_max'])} s und {aw['ir_ge20_pct']} % "
        "(der Wert der ersten Zusammenfassung mischte die Pings an „255.“ ein).",
        "<b>Beacon-Verluste:</b> Mechanismus in drei Teilen statt zwei; Ursache laut RCA: 5,5-s-Haltezeit eigener Beacons "
        "gegen 150-220 ms der Relays im Fall A.",
        "<b>Modell-Etiketten:</b> alle Rechnungen zu Fristen und Verfall ohne Frist sind als Modell und Obergrenze gekennzeichnet.",
        f"<b>Weitere:</b> ACK-Relays nach gehörter Kopie {ah['ia']} (IA); Re-Relays {V['dedup_rerelay']['n']}; "
        f"{V['ring']['supersede_entries_in_chains']} Einträge in {V['ring']['chains']} Ketten; "
        "Ringfüllstand-Steigung −0,038 bis +0,014; Cancel 589 Zeilen; Fehlempfänge an OE3XIR nicht „kein LoRa“; "
        "Stempel 20260929 ist die Release-Kennung, kein Build-Datum; „anderweitig übertragen“ statt „redundant“.",
    ] + HB.verify_changes(d)))

    # ------------------------------------------------------------------ Dev
    b.append('<details class="dev"><summary>Methodik, Marker, Code-Stellen, Skripte (Entwickler)</summary>'
             "<h3>Quellen und Zeitbasen</h3>" + ul([
                 "Ergebnisse je Thema: <code>data/q2/</code> (TX-Pool), <code>data/q34/</code> (TTL/{CET}/CSMA-Boden), "
                 "<code>data/q5/</code> (Zeitsignal), <code>data/q6/</code> (Manipulationen), <code>data/health/</code> "
                 "(Grundlinie, DB0AIS ETH-04), <code>data/master/</code> (Stammdaten aus mcmap), "
                 "<code>data/CORE-FINDINGS.md</code> (Kernbefunde v2).",
                 "<code>data/extra/uebersicht_rx_silence.json</code>: Stillen je OE3-Knoten, alle Empfänge gezählt "
                 "(ONRXDONE_TIME oder RX_ERROR); erzeugt von <code>generators/lib/uebersicht_rx_silence.py --norm &lt;norm&gt; --out</code>.",
                 "<code>data/extra/uebersicht_verified.json</code>: Zahlen aus den Prüftabellen <code>verify/*.md</code>, die die "
                 "results.json der ersten Welle nicht oder in alter Form tragen (Fehlempfangs-Anteile, volle Läufe der "
                 "veralteten Relays, ACK-Relay-Sendezeit, RCA-Kopfwerte, Prüfzählung).",
                 "Logger-Uhr (Mac, UTC+1) für OE3; DB0AIS nur Knotenzeit mit PuTTY-Start 17:01:11; "
                 "Frame-Zählung ohne FCS:0000-Zeilen; [LOG] druckt vor Dedup, zählt also Empfänge.",
                 *HB.dev_items(),
                 "Sendezeit: SF11, BW250, CR4/6, Präambel 8, explizit, CRC; konstant ca. 40 ms unter der Formel "
                 "über 12 bis 153 B (Gegenprobe).",
             ]) + "<h3>Marker (je Quelle an der Emit-Stelle gelesen)</h3>" + ul([
                 "RING_WRITE/TX_READ/DROP_STALE/SUPERSEDE: <code>src/txring_functions.cpp</code> (:639, :1068, :1033, :1072, :1076).",
                 "[NBR]|NEED/REFUSE/CANCEL: <code>src/nbr_matrix.cpp</code> :282, :294, :305, :261, :252, :366; "
                 "<code>src/lora_functions.cpp</code> :1380, :1473, :2641.",
                 "TTL-Tabelle <code>src/configuration_global.h:465-480</code>; {CET}-Sperre <code>src/cet_age.h</code> (150 s).",
             ]) + "<h3>Abweichungen der Quellen und Festlegungen</h3>" + ul([
                 f"<b>Längste Stille OE3XIA:</b> mit allen Empfängen {sil_long} s ab {sil_long_start}; zählt man nur "
                 "OnRxDone plus Fehlempfänge, sind es 1030 s ab 11:48:56 (health/results.json, die ACK-Empfänge fehlen).",
                 f"<b>Fremde Gateways je Telegramm:</b> Median {fgw_med} verschiedene fremde Gateways (q5, mit ca. 2 nur aus Relay-"
                 f"Pfaden erschlossenen) gegen Mittel {for_gw} direkt gehörte fremde Aussendungen je Telegramm.",
                 "<b>CANCEL:</b> 589 CANCEL-Zeilen (verify/q6 #6); 587 davon einem Relay-Slot zuordenbar (q6 results).",
                 f"<b>Sendezeit des Veto-Anteils:</b> q6/results.json nennt 4006 s (29,5 %), die Prüfung misst {ah['air_s_total']} s "
                 f"({de(ah['air_pct'])} %); die Seite zeigt den Prüfwert.",
                 "<b>Fehlempfangs-Anteil:</b> aus health/results.json (rx_error_n / (rx_error_n + heard_events + ack_events)) "
                 "ergeben sich Werte, die höchstens 0,1 Punkte von den Prüfwerten abweichen; die Seite zeigt die Prüfwerte.",
                 "<b>Eigene HEY gesendet</b> = gesendet/(gesendet+TTL); ersetzte (Supersede) werden nicht als "
                 "Verlust gezählt, der Nenner „alle geschriebenen“ steht daneben.",
                 f"<b>Verfalls-Drops:</b> {q34n_drops} insgesamt (q34) gegen {fate['stale_ttl']} im Ring-Schicksal (q2): 5 vor Logbeginn.",
                 f"<b>OE3GJC-12:</b> Stammdaten aus <code>data/master/master.tsv</code> ({gjc['lat'] if gjc else '-'} N, "
                 f"{gjc['altitudeM'] if gjc else '-'} m); abweichende Angaben im Auftrag (213 m) gehören zu OE3GJC-31.",
             ]) + "<h3>Nebenbefund Werkzeug: berglog.py</h3>" + ul([
                 "Die Verifikationsprüfungen in <code>tools/berglog.py</code> (<code>verify()</code>) "
                 "zählen mit Shell-Pipelines alle [LOG]-Zeilen inklusive ACK-Frames und alle {CET}-Zeilen, "
                 "auch solche außerhalb von [LOG]. Das ergibt falsche MISMATCH-Warnungen (z. B. OE3XHL Shell 21838 "
                 "gegen 11659 geparste Frames; 11659 + 10179 ACK-Frames = 21838). Der Parser selbst ist korrekt.",
             ]) + "<h3>Skripte</h3>" + ul([
                 "Diese Seite wird von <code>generators/uebersicht.py</code> erzeugt (<code>--data</code>, <code>--kit</code>, "
                 "<code>--out</code>); Hilfen in <code>generators/lib/uebersicht_*.py</code>.",
                 "Prüfer-Skripte: <code>verify/q2_checks</code>, <code>q34_checks</code>, <code>q5_checks</code>, "
                 "<code>q6_checks</code>, <code>health_checks</code>, <code>rca_own_beacons_checks</code>.",
             ]) + "</details>")

    b.append('<div class="foot">Quellen: die acht Logs, die Ergebnisse der Auswertung (data/), die Prüftabellen (verify/) und '
             "die Stammdaten (mcmap) wie oben. Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware. "
             'Knotenseiten: <a href="oe3xhl.html">OE3XHL</a>, <a href="oe3xia.html">OE3XIA</a>, '
             '<a href="oe3xir.html">OE3XIR</a>, <a href="oe3xoc.html">OE3XOC</a>, <a href="oe3xwj.html">OE3XWJ</a>, '
             '<a href="db0ais.html">DB0AIS</a>, <a href="hb9vqq4.html">HB9VQQ-4</a>, '
             '<a href="hb9vqq5.html">HB9VQQ-5</a>.</div>')
    return "\n".join(b), figs
