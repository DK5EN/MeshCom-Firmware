#!/usr/bin/env python3
"""Report page: DB0AIS-12, 16.7-day PuTTY capture 2026-10-30 17:01 .. 2026-11-16 09:30 (one boot).

Usage:
    python3 -I db0ais_14d.py --data <run>/data --kit <repo>/tools/logreport --out <run>/db0ais_14d.html

Reads q2/results.json (txpool), extra/db0ais_14d_measure.json (lib/db0ais_14d_measure.py over the full
capture, freeze line 4448151), master/master.tsv (mcmap tiles of 2026-11-01). Numbers taken from the
health/timesignal extractors (work dir, not copied) and from the mcmap gateway_availability query of
2026-11-21 are written in as constants below, with their source. Stdlib only.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path
from typing import Any

HOME = (49.997, 8.5922)  # DB0AIS-12, master data


def n(x: float | int, dec: int = 0) -> str:
    s = f"{x:,.{dec}f}"
    return s.replace(",", "X").replace(".", ",").replace("X", ".")


def pc(a: float, b: float, dec: int = 1) -> str:
    return n(100.0 * a / b, dec) + " %" if b else "0 %"


def dist_km(lat: float, lon: float) -> float:
    r = 6371.0
    p1, p2 = math.radians(HOME[0]), math.radians(lat)
    dp, dl = p2 - p1, math.radians(lon - HOME[1])
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def load_master(data: Path) -> dict[str, dict[str, str]]:
    with (data / "master" / "master.tsv").open(encoding="utf-8", newline="") as fh:
        return {r["call"]: r for r in csv.DictReader(fh, delimiter="\t")}


HW = {"RAK4631": "RAK4631 (WisBlock, nRF52840)", "TLORA V2.1.6": "TTGO LoRa32 V2.1",
      "TBEAM V1.2": "T-Beam V1.2", "EBYTE E22": "EBYTE E22", "HELTEC V3": "Heltec WiFi LoRa 32 V3",
      "TBEAM SUPREME": "T-Beam Supreme"}

# Direct neighbours (last hop), from health/results.json neighbours.top (healthy part).
NEIGH = [
    ("DB0MW-12", 24234, -85, 5), ("DB0HRF-12", 23349, -98, 1), ("DB0KT-15", 15070, -84, 5),
    ("DM9PT-20", 12532, -98, 0), ("DL0TRZ-20", 5495, -97, 0), ("DL0BOD-20", 5088, -101, -3),
    ("DL2IAS-10", 4883, -101, -4), ("DH2PC-20", 4646, -101, -5), ("DF6PC-20", 4396, -101, -5),
    ("DG6FBX-1", 1876, -105, -16),
]
LOG_FW = {"DB0MW-12": "4.40a", "DB0HRF-12": "4.40a", "DB0KT-15": "4.40a", "DM9PT-20": "4.40a",
          "DL0TRZ-20": "4.35p / 4.40a", "DL0BOD-20": "4.40a", "DL2IAS-10": "4.40a", "DH2PC-20": "4.40a",
          "DF6PC-20": "4.40a", "DG6FBX-1": "4.35d", "DM1MT-1": "4.35f", "DL2MK-12": "4.35k",
          "DB5COA-21": "4.35k", "DM1MT-14": "4.35o"}
PRE_CAD = [("DG6FBX-1", 2119), ("DM1MT-1", 1781), ("DL2MK-12", 1088), ("DB5COA-21", 1046), ("DM1MT-14", 979)]


def node_row(m: dict[str, dict[str, str]], call: str) -> list[str]:
    r = m.get(call)
    if not r or not r.get("lat"):
        return ["keine Stammdaten", "", "", LOG_FW.get(call, "")]
    loc = (r.get("description") or r.get("name") or "").strip() or "ohne Beschreibung"
    d = dist_km(float(r["lat"]), float(r["lon"]))
    hw = HW.get(r["hardware"], r["hardware"])
    fw = LOG_FW.get(call, r["firmware"])
    if r["firmware"] and r["firmware"] not in fw:
        fw += f" (DB: {r['firmware']})"
    return [f"{loc}, {r['altitudeM']} m", n(d, 1) + " km", hw, fw]


def hang_drawing() -> str:
    from reportkit import explain
    s = []
    s.append('<rect class="d-box d-acc" x="20" y="30" width="170" height="60" rx="8"/>')
    s.append('<text class="d-txt" x="105" y="56" text-anchor="middle">Hauptschleife</text>')
    s.append('<text class="d-txt-s" x="105" y="76" text-anchor="middle">sendet JSON an MeshDash</text>')
    s.append('<rect class="d-box d-act" x="250" y="30" width="170" height="60" rx="8"/>')
    s.append('<text class="d-txt" x="335" y="56" text-anchor="middle">W5100S nach Reset</text>')
    s.append('<text class="d-txt-s" x="335" y="76" text-anchor="middle">Socket existiert nicht</text>')
    s.append('<line class="d-line d-acc-s anim-flow" x1="190" y1="60" x2="250" y2="60"/>')
    s.append('<path class="d-line d-act-s anim-flow" d="M335,90 C335,130 105,130 105,90"/>')
    s.append('<text class="d-txt-s" x="220" y="145" text-anchor="middle">wartet auf SEND_OK oder TIMEOUT - kommt nie</text>')
    s.append('<rect class="d-box d-ok" x="460" y="30" width="160" height="60" rx="8"/>')
    s.append('<text class="d-txt" x="540" y="56" text-anchor="middle">Funk-Empfang</text>')
    s.append('<text class="d-txt-s" x="540" y="76" text-anchor="middle">laeuft weiter (eigene Task)</text>')
    s.append('<rect class="d-box d-act anim-pulse" x="460" y="110" width="160" height="60" rx="8"/>')
    s.append('<text class="d-txt" x="540" y="136" text-anchor="middle">TX-Puffer</text>')
    s.append('<text class="d-txt-s" x="540" y="156" text-anchor="middle">fuellt sich, 20/20</text>')
    s.append('<line class="d-line" x1="540" y1="90" x2="540" y2="110"/>')
    s.append('<text class="d-txt-s" x="20" y="200">Ohne Hauptschleife: kein Senden, kein Keepalive, keine Uhr, kein Watchdog auf dem nRF52.</text>')
    return explain("".join(s), "Abb. 2: Wo DB0AIS am 14.11. um 05:43 haengen blieb", width=640, height=215)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--kit", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    sys.path.insert(0, str(a.kit))
    from reportkit import chip, esc, hist, page, table  # noqa: E402

    m = json.loads((a.data / "extra" / "db0ais_14d_measure.json").read_text(encoding="utf-8"))
    q2: Any = json.loads((a.data / "q2" / "results.json").read_text(encoding="utf-8"))
    master = load_master(a.data)
    hs, hg = m["segments"]["healthy"], m["segments"]["hang"]

    fate = {"tx_air": 38072, "superseded": 183, "nbr_cancel": 218, "stale_cet": 1, "stale_ttl": 6, "pending_end": 1}
    writes = sum(fate.values())
    _ = q2  # results.json carries the same fate split (cross-checked when written)
    qd = {int(k): v for k, v in m["qdepth"].items()}
    qd_total = sum(qd.values())
    drops_hang = hg["drop_new"]
    drop_total = sum(drops_hang.values()) + sum(hg["drop_prio"].values())
    hang_h = (m["end_ms"] - m["freeze_ms"]) / 3_600_000

    b: list[str] = []
    b.append('<p class="eyebrow">MeshCom Log-Auswertung · DB0AIS-12 · 14-Tage-Mitschnitt</p>')
    b.append("<h1>DB0AIS-12: Sendepuffer 14,5 Tage verlustfrei, dann 52 Stunden Funkstille durch einen Hänger im Ethernet-Treiber</h1>")
    b.append('<ul class="meta">'
             "<li><b>Log:</b> putty.log (PuTTY-Mitschnitt, 4.701.227 Zeilen, 276 MB), Fortsetzung des 24-h-Logs vom 30.10. "
             "(die ersten 17,6 MB sind byte-gleich)</li>"
             "<li><b>Zeitraum:</b> 30.10.2026 17:01 bis 16.11.2026 09:30 (16,7 Tage), ein einziger Boot, kein Neustart. "
             "Zeiten aus den Millisekunden des Knotens plus PuTTY-Startzeit, MEZ.</li>"
             "<li><b>Knoten:</b> DB0AIS-12, RAK4631 (WisBlock, nRF52840) mit Ethernet (W5100S), Firmware 4.40a "
             "(Stand 28.10.), Mörfelden JN49HX, 148 m. Internet-Gateway über HAMNET (Server DL, 44.148.230.197).</li>"
             "<li><b>LoRa:</b> 433,175 MHz, 250 kHz, SF11, CR 4/6, 22 dBm</li>"
             "<li><b>Stand:</b> 21.11.2026</li></ul>")

    b.append('<div class="bluf"><p class="lead">Kurzfassung</p>'
             "<p>Die Frage war: Passt der TX-Puffer, und geht nichts verloren? Für die ersten 14,5 Tage lautet die Antwort ja. "
             f"Von {n(writes)} Einträgen im Sendepuffer wurden {n(fate['tx_air'])} gesendet; der Rest wurde absichtlich "
             "ersetzt oder gestrichen. Der Puffer war nie mehr als halb voll (höchstens 10 von 20 Plätzen). "
             "Keine Text- oder ACK-Nachricht ging verloren.</p>"
             "<p>Am <b>14.11. um 05:43</b> blieb die Hauptschleife der Firmware in der Ethernet-Bibliothek hängen. Vorher war der "
             "HAMNET-Server fast fünf Stunden stumm, dann folgten ein DHCP-Fehlschlag und ein Hardware-Reset des Ethernet-Chips. "
             f"Danach sendete DB0AIS <b>{n(hang_h, 1)} Stunden lang nichts mehr</b>, weder über Funk noch ans Internet, "
             "empfing aber weiter. In dieser Zeit lief der Puffer voll, und "
             f"{n(drop_total)} Weiterleitungen wurden verworfen, darunter {n(drops_hang.get('3A', 0))} Textnachrichten. "
             "Der Kartenserver sah das Gateway in genau diesem Fenster offline. Das ist ein Firmware-Fehler, kein Bedienfehler. "
             "Er ist im aktuellen Stand noch nicht behoben.</p></div>")

    idx = [
        ["Passt der TX-Puffer?", f"Ja: höchstens 10 von 20 Plätzen, {pc(qd.get(0, 0), qd_total, 0)} der Einträge fanden einen leeren Puffer vor.", chip("ok")],
        ["Geht im Normalbetrieb etwas verloren?", f"Nein: 0 Texte, 0 ACKs verworfen. {n(fate['superseded'] + fate['nbr_cancel'])} gewollte Streichungen, 7 Altersverwürfe (eigene Baken, Zeitsignal).", chip("ok")],
        ["Was geschah am 14.11.?", f"Hauptschleife hängt nach einem Ethernet-Chip-Reset, {n(hang_h, 1)} h kein Senden, {n(drop_total)} Weiterleitungen verworfen.", chip("act")],
        ["Ethernet und Server", "30 kurze Linkabrisse (1-4 s) ohne Folgen; 2.566-mal war der Server 35 s stumm; die DHCP-Erneuerung am 03.11. scheiterte (ohne Folgen).", chip("watch")],
        ["Funkkanal", "Auslastung im Mittel 10,7 %, Spitze 85 %; 7./8.11. doppelt so voll wie sonst.", chip("ok")],
        ["Firmware ohne CAD in der Umgebung", "47 von 724 Knoten (6,5 %), 10 % des Verkehrs.", chip("watch")],
    ]
    b.append("<h2>Antwort-Index</h2>")
    b.append(table(["Frage", "Kurzantwort", "Urteil"], idx, raw=True))

    b.append("<h2>Begriffe in einem Halbsatz</h2><ul>"
             "<li><b>TX-Puffer / Slot:</b> Warteschlange vor dem Sender, 20 Plätze. Jeder Platz trägt eine Priorität: "
             "1 ACK, 3 Text, 4 Position, 5 Heartbeat.</li>"
             "<li><b>CSMA/CAD:</b> Vor jedem Senden hört der Knoten, ob der Kanal frei ist (Channel Activity Detection). "
             "Ist er belegt, wartet er und versucht es erneut. Daher kommt die Wartezeit im Puffer.</li>"
             "<li><b>Supersede:</b> Ein neueres Paket ersetzt ein älteres, noch nicht gesendetes (z.&nbsp;B. ein neueres Zeitsignal).</li>"
             "<li><b>Nachbar-Abbruch:</b> Ein Relay-Eintrag wird gestrichen, weil ein Nachbar das Paket schon weitergegeben hat.</li>"
             "<li><b>Keepalive/BEAT:</b> Das Gateway meldet sich alle 30 s beim Server, der Server antwortet mit BEAT.</li>"
             "<li><b>W5100S:</b> Der Ethernet-Chip des RAK13800-Moduls. Die Firmware spricht ihn über eine Bibliothek an, "
             "die Netzwerk-Sockets im Chip verwaltet.</li></ul>")

    # ---------------- Q1 TX buffer
    b.append(f"<h2>1. Passt der TX-Puffer? {chip('ok')}</h2>")
    b.append(f"<p>Gemessen über die gesunden 14,5 Tage (30.10. 17:01 bis 14.11. 05:43): {n(writes)} Einträge, "
             f"im Mittel {n(writes / 348.7, 0)} pro Stunde. Bei jedem Eintrag steht im Log, wie viele Plätze schon belegt waren. "
             f"{pc(qd.get(0, 0), qd_total)} trafen auf einen leeren Puffer, {pc(qd.get(1, 0), qd_total)} auf einen belegten Platz, "
             f"nur {n(sum(v for k, v in qd.items() if k >= 5))} Einträge auf 5 oder mehr. Der höchste Stand war 10 von 20, am 07.11.</p>")
    bins = [(str(k), float(qd.get(k, 0))) for k in range(0, 11)]
    b.append(hist(bins, "Abb. 1: Belegte Plätze beim Einreihen eines neuen Eintrags (0 bis 10 von 20), 14,5 Tage. Fast immer war der Puffer leer.", ylabel="Einträge"))
    fate_rows = [
        ["Gesendet", n(fate["tx_air"]), pc(fate["tx_air"], writes)],
        ["Nachbar hat es schon weitergegeben (gewollt)", n(fate["nbr_cancel"]), pc(fate["nbr_cancel"], writes)],
        ["Durch neueres Paket ersetzt (gewollt)", n(fate["superseded"]), pc(fate["superseded"], writes)],
        ["Zu alt verworfen: eigene Bake (TTL 120/180 s)", n(fate["stale_ttl"]), pc(fate["stale_ttl"], writes, 2)],
        ["Zu alt verworfen: fremdes Zeitsignal", n(fate["stale_cet"]), pc(fate["stale_cet"], writes, 2)],
        ["Beim Schnitt noch im Puffer", n(fate["pending_end"]), pc(fate["pending_end"], writes, 2)],
        ["Summe", n(writes), "100,0 %"],
    ]
    b.append(table(["Schicksal eines Eintrags", "Anzahl", "Anteil"], fate_rows, num_cols=(1, 2),
                   caption="Tabelle 1: Jeder Eintrag des Sendepuffers ist genau einem Ausgang zugeordnet, nichts bleibt ungeklärt."))
    b.append("<p>Die 6 Altersverwürfe sind eigene Baken (3 Positionen, 3 Heartbeats) am 07. und 08.11., den lebhaftesten Tagen. Eine Bake, die 2 bis 3 Minuten nicht durchkam, ist veraltet; die nächste kommt ohnehin. Dazu kommen 42 Zeitsignale, "
             "die nicht eingereiht wurden, weil schon ein Zeitsignal im Puffer wartete (im Log als <code>q=full</code>). "
             "Das ist kein voller Puffer, sondern Absicht: ein zweites Zeitsignal hätte keinen Nutzen.</p>")
    w = m["wait"]
    b.append("<p><b>Wartezeit bis zum Senden</b> (Median / 95. Perzentil / Maximum): "
             f"Relay-Texte {n(w['r3']['median'] / 1000, 1)} / {n(w['r3']['p95'] / 1000, 1)} / {n(w['r3']['max'] / 1000, 0)} s, "
             f"Relay-ACKs {n(w['r1']['median'] / 1000, 1)} / {n(w['r1']['p95'] / 1000, 1)} / {n(w['r1']['max'] / 1000, 0)} s, "
             f"Server-Texte ins Mesh {n(w['g3']['median'] / 1000, 1)} / {n(w['g3']['p95'] / 1000, 1)} / {n(w['g3']['max'] / 1000, 0)} s, "
             f"Positionen {n(w['r4']['median'] / 1000, 1)} / {n(w['r4']['p95'] / 1000, 1)} / {n(w['r4']['max'] / 1000, 0)} s. "
             "Die Wartezeit entsteht fast ganz durch das Lauschen vor dem Senden (CSMA): Bei SF11 und 250 kHz belegt ein Rahmen "
             "den Kanal 0,4 bis 1 s, und 37 % der ersten Kanalabfragen fanden ihn belegt. Texte und ACKs kommen schneller dran "
             "als Positionen und Heartbeats, so wie die Prioritäten es vorsehen.</p>")

    # ---------------- Q2 losses
    b.append(f"<h2>2. Geht im Normalbetrieb etwas verloren? {chip('ok')}</h2>")
    b.append("<p>Im Normalbetrieb musste die Firmware keinen einzigen Eintrag wegen Platzmangel verwerfen "
             "(0-mal <code>RING_DROP_NEW</code>, 0-mal <code>RING_DROP_PRIO</code>), und die Datenpuffer zum Server liefen nie über. "
             f"{n(hs['rx_new'].get(':', 0))} neue Textrahmen und {n(hs['rx_new'].get('A', 0))} neue ACK-Rahmen wurden empfangen. "
             "Wo ein Text nicht weitergegeben wurde, war das eine Regel und keine Panne: Hop-Grenze am Gateway "
             "(<code>gwcap</code>, 9.792), Hop-Zähler bei 0 (3.750), Gruppenfilter (456), Schleifenschutz (10).</p>")
    days = sorted(m["per_day"].items())
    rows = []
    for d, v in days:
        if d >= "2026-11-14":
            continue
        u = m["util_day"].get(d, {})
        rows.append([d[8:10] + "." + d[5:7] + ".", n(v.get("rx_new", 0)), n(v.get("tx", 0)), str(m["qmax_day"].get(d, 0)),
                     n(u.get("mean", 0), 1) + " %", str(u.get("max", 0)) + " %"])
    b.append(table(["Tag", "Neue Pakete empfangen", "Gesendet", "Puffer max.", "Kanal Mittel", "Kanal Spitze"], rows,
                   num_cols=(1, 2, 3, 4, 5),
                   caption="Tabelle 2: Tageswerte (30.10. ab 17:01, 13.11. ganz). Der 07./08.11. war der lebhafteste Zeitraum; "
                           "auch dort blieb der Puffer bei höchstens 10 Plätzen."))

    # ---------------- Q3 hang
    b.append(f"<h2>3. Was geschah am 14.11. um 05:43? {chip('act')}</h2>")
    b.append("<h3>Ablauf</h3><ol>"
             "<li><b>01:02 bis 05:43:</b> Die letzte BEAT-Antwort des HAMNET-Servers kommt um 01:02. Danach bleibt der Server "
             "4 Stunden 41 Minuten stumm, der Knoten sendet aber weiter seine Keepalives. Ab 05:40 scheitern zehn UDP-Sendungen "
             "nach je 1,6 s, NTP läuft in Zeitüberschreitung.</li>"
             "<li><b>05:43:24:</b> Die Firmware fordert per DHCP eine neue Adresse an. Der DHCP-Server antwortet nicht "
             "(12 s blockiert), die Adresse ist 0.0.0.0. Ein zweiter Versuch scheitert ebenso.</li>"
             "<li><b>Rund 40 s später:</b> Nach zwei Fehlschlägen setzt die Firmware den Ethernet-Chip W5100S hart zurück "
             "(<code>hw_reset;reason;dhcp_fail</code>). Danach klappt DHCP in 4 s, die alte Adresse 44.149.189.136 ist wieder da.</li>"
             "<li><b>Unmittelbar danach</b> gibt die Hauptschleife eine empfangene Position von DL0TRZ-20 als JSON an MeshDash "
             "(EXTUDP, 44.149.189.135:1799) weiter. Die Zeile <code>[EXT] Out: … Len: 279</code> ist die letzte Ausgabe der Hauptschleife. "
             "Die Bestätigungszeile <code>[EXT];tx</code> folgt nie.</li>"
             f"<li><b>Bis zum Ende des Mitschnitts</b> (16.11. 09:30, {n(hang_h, 1)} h später) laufen nur noch die Empfangsroutinen. "
             "Die Uhrzeit im Log bleibt auf 05:43:42 stehen, weil sie in der Hauptschleife fortgeschrieben wird.</li></ol>")
    b.append(hang_drawing())
    b.append("<h3>Ursache</h3>"
             "<p>Die Sendefunktion der Ethernet-Bibliothek (<code>socketSendUDP</code> in RAK13800-W5100S, <code>socket.cpp:518</code>) "
             "wartet in einer Schleife, bis der Chip entweder „gesendet“ (SEND_OK) oder „Zeitüberschreitung“ (TIMEOUT) meldet. "
             "Eine Zeitgrenze im Programm selbst gibt es nicht. Der Hardware-Reset hat alle Sockets im Chip gelöscht. "
             "Die Firmware öffnet danach den Gateway-Socket neu, den EXTUDP-Socket zu MeshDash aber nicht: der wird erst in einem "
             "späteren Schleifendurchlauf neu gestartet, und diesen Durchlauf gab es nicht mehr. Ein Sendebefehl auf einen Socket, "
             "den der Chip nicht kennt, löst weder SEND_OK noch TIMEOUT aus. Die Schleife läuft also endlos.</p>"
             "<p>Die Empfangsroutinen laufen auf dem nRF52 in einer eigenen Task und gehen darum weiter. Das erklärt das Bild im Log: "
             "Pakete kommen an, werden in den Puffer geschrieben, aber nie gesendet. Einen Hardware-Watchdog, der den Knoten in "
             "so einem Fall neu startet, nutzt die nRF52-Firmware nicht.</p>"
             "<p><b>Sicherheit der Aussage:</b> Die Stelle des Hängers ist durch das Log belegt (letzte Ausgabe vor der Sendefunktion, "
             "danach keine einzige Ausgabe der Hauptschleife mehr). Dass der fehlende Socket die Schleife ohne Ausweg lässt, ist aus "
             "dem Code geschlossen, nicht auf dem Prüfstand nachgestellt. Im gesamten Mitschnitt gab es genau einen Hardware-Reset "
             "des Ethernet-Chips, und genau dieser führte zum Hänger. Die 30 kurzen Linkabrisse davor kamen ohne Reset aus und "
             "blieben folgenlos.</p>")
    b.append("<h3>Folgen in den 52 Stunden</h3>")
    hr = [
        ["Funk-Aussendungen", "0", "vorher rund 2.500 pro Tag"],
        ["Keepalives an den Server", "0", "vorher alle 30 s"],
        ["Neue Pakete empfangen", n(sum(hg["rx_new"].values())), "Empfang lief weiter"],
        ["Weiterleitung verworfen: Text", n(drops_hang.get("3A", 0)), "Puffer voll (20/20), keine niedrigere Priorität zum Verdrängen"],
        ["Weiterleitung verworfen: ACK", n(drops_hang.get("41", 0)), "dto."],
        ["Weiterleitung verworfen: Position", n(drops_hang.get("21", 0)), "dto."],
        ["Weiterleitung verworfen: Heartbeat", n(drops_hang.get("40", 0)), "dto."],
        ["Verdrängt durch höhere Priorität", n(sum(hg["drop_prio"].values())), "Prioritätsregel greift noch, nützt aber nichts"],
        ["Pakete fürs Internet-Gateway verworfen", n(hg["overflow"].get("udp", 0)), "Server-Puffer übergelaufen"],
    ]
    b.append(table(["Was", "Anzahl", "Bemerkung"], hr, num_cols=(1,),
                   caption="Tabelle 3: 14.11. 05:44 bis 16.11. 09:30. Die Verwürfe sind Weiterleitungen, keine eigenen Nachrichten."))
    b.append(f"<p><b>Für das Netz war der Schaden kleiner, als die Zahlen aussehen.</b> DB0MW-12 (gleicher Betreiber, {node_row(master, 'DB0MW-12')[1]} entfernt) "
             "lief weiter: Von den 729 verschiedenen Texten, die DB0AIS nicht weitergeben konnte, hörte DB0AIS mindestens 586 (80 %) "
             "als Weiterleitung von DB0MW-12. Verloren war vor allem der Weg über DB0AIS ins Internet. Texte aus dem hessischen Mesh "
             "kamen in diesen 52 Stunden nicht über DB0AIS zum Server, und Server-Texte kamen nicht über DB0AIS ins Mesh.</p>")
    b.append('<div class="callout ok"><p><b>Gegenprobe Kartenserver (mcmap, abgefragt 21.11.):</b> DB0AIS-12 als Gateway '
             "am 14.11. 19,8 % online (das entspricht bis ca. 05:45), am 15.11. 0,5 %, am 16.11. 54 % (das entspricht ab ca. 12:00). "
             "Das Fenster stimmt mit dem Log auf die Minute überein. Seit dem 16.11. mittags läuft das Gateway wieder ohne Lücke "
             "(17. bis 20.11. jeweils 99,95 bis 100 %).</p></div>")

    # ---------------- Q4 Ethernet
    b.append(f"<h2>4. Ethernet, Server und DHCP in den 14,5 Tagen davor {chip('watch')}</h2><ul>"
             "<li><b>Linkabrisse:</b> 30, alle 1 bis 4 s lang (Median 2 s), alle ohne Folgen. Die Erholung ohne Chip-Reset funktioniert.</li>"
             "<li><b>Server stumm:</b> 2.566-mal kam 35 s lang keine Antwort des HAMNET-Servers (8,5 % der Keepalives ohne BEAT). "
             "Am meisten 02. bis 05.11. (bis 595 am Tag) und am 14.11. vor dem Hänger; im Normalbetrieb dauerten die längsten Lücken 15 Minuten, am 14.11. waren es 4 Stunden 41 Minuten. Das liegt im HAMNET-Pfad oder am Server, nicht am Knoten: "
             "der Link blieb dabei oben.</li>"
             "<li><b>DHCP-Erneuerung:</b> Im 24-h-Bericht vom 01.11. war offen, ob sie funktioniert. Sie kam am 03.11. um 05:01 "
             "(3,5 Tage nach dem Start, halbe Lease-Zeit) und <b>scheiterte</b> (<code>dhcp;rc;1</code>, RENEW_FAIL). "
             "Der Knoten behielt die Adresse und arbeitete normal weiter. Am 14.11. antwortete der DHCP-Server erneut zweimal "
             "nicht und erst nach dem Chip-Reset. Der DHCP-Server im HAMNET-Router sollte geprüft werden; "
             "eine feste Adresse für DB0AIS würde den DHCP-Weg ganz umgehen.</li>"
             "<li><b>Zeit (NTP):</b> 1.376 erfolgreiche Abfragen, 537 Zeitüberschreitungen oder Sendefehler, gehäuft in den Server-Stummphasen.</li></ul>")

    # ---------------- Q5 RF
    b.append(f"<h2>5. Funkumfeld {chip('ok')}</h2><ul>"
             "<li><b>Kanalauslastung:</b> Mittel 10,7 %, Median 9 %, 95. Perzentil 29 %, Spitze 85 %. Am 07. und 08.11. "
             "lag das Tagesmittel bei 20 bis 21 %, sonst bei 8 bis 12 %. Auch dann blieb der Puffer klein.</li>"
             "<li><b>Rauschboden:</b> -106 dBm (unteres 5-%-Perzentil der Fehlempfänge), theoretisch -114 dBm bei 250 kHz. "
             "Rund 8 dB fehlen; für einen Standort im Rhein-Main-Gebiet ist das üblich.</li>"
             "<li><b>Fehlempfänge:</b> 144.942 Empfangsversuche ohne gültigen Rahmen (55 % aller Empfangsereignisse, Median -101 dBm). "
             "Das ist der Empfänger, der auf Rauschen oder abgeschnittene Rahmen anspringt; dabei geht kein Paket verloren.</li>"
             "<li><b>Übersteuerung:</b> kein dekodierter Rahmen stärker als -60 dBm, der stärkste Nachbar kommt mit -61 dBm. Kein Problem.</li>"
             "<li><b>Zeitsignal:</b> Die drei Kennungen OE1XAR-33, -62 und -63 sind keine Knoten. Sie sind das Zeitsignal der Server "
             "(OE- und DL-Server speisen unter eigener Kennung ein). Takt 10 Minuten, 72 bis 127 Baken je Kennung fehlen in 14,5 Tagen, "
             "gehäuft am 09.11.</li></ul>")

    nrows = []
    for call, cnt, rssi, snr in NEIGH:
        nrows.append([call, *node_row(master, call), n(cnt), f"{rssi} dBm / {snr} dB"])
    b.append(table(["Nachbar (letzter Hop)", "Standort, Höhe", "Entfernung", "Hardware", "Firmware", "Rahmen", "RSSI / SNR Median"],
                   nrows, num_cols=(2, 5),
                   caption="Tabelle 4: Die zehn häufigsten direkten Nachbarn. RSSI/SNR gelten nur für den letzten Hop. "
                           "Stammdaten aus mcmap vom 01.11.; Firmware aus dem Log, abweichende Datenbankangabe in Klammern."))

    # ---------------- Q6 pre-CAD
    b.append(f"<h2>6. Firmware ohne Kanalabfrage {chip('watch')}</h2>"
             "<p>Firmware 4.35a bis 4.35o sendet ohne vorher zu lauschen (CAD kam mit 4.35p). Im Empfang von DB0AIS: "
             "47 von 724 Absendern (6,5 %), die 10 % des Verkehrs erzeugen. Die aktivsten:</p>")
    prow = [[c, *node_row(master, c), n(ev)] for c, ev in PRE_CAD]
    b.append(table(["Rufzeichen", "Standort, Höhe", "Entfernung", "Hardware", "Firmware", "Rahmen"], prow, num_cols=(2, 5),
                   caption="Tabelle 5: Die fünf aktivsten Knoten mit Firmware vor 4.35p. Ein Update dort entlastet den Kanal am meisten."))

    # ---------------- Recommendations
    b.append("<h2>Empfehlungen</h2><ol>"
             "<li><b>Firmware (DK5EN):</b> Die Sendeschleife der W5100S-Bibliothek bekommt eine eigene Zeitgrenze, und nach jedem "
             "Hardware-Reset des Chips wird auch der EXTUDP-Socket neu geöffnet, bevor wieder gesendet wird. "
             "Als letzte Sicherung ein Watchdog auf dem nRF52, der eine hängende Hauptschleife nach einigen Minuten neu startet. "
             "Das gilt für jeden RAK4631 mit Ethernet und EXTUDP, nicht nur für DB0AIS. "
             "Bis zum Fix kann der Hänger wieder auftreten, sobald der DHCP-Server zweimal hintereinander nicht antwortet.</li>"
             "<li><b>Betreiber:</b> Den DHCP-Server im HAMNET-Router prüfen (Erneuerung am 03.11. gescheitert, am 14.11. zwei Fehlschläge). "
             "Eine feste IP-Adresse für DB0AIS vermeidet den DHCP-Weg und damit diesen Auslöser.</li>"
             "<li><b>Betreiber, solange der Fix fehlt:</b> Fällt DB0AIS-12 auf der Karte als Gateway aus, hilft ein Neustart. "
             "Die Uhrzeit im Log, die stehen bleibt, ist das sichere Zeichen für diesen Fehler.</li></ol>")

    b.append("<h2>Offene Fragen an den Betreiber</h2><ol>"
             "<li>Was ist am 16.11. gegen 12:00 passiert: Neustart von Hand oder Stromunterbrechung?</li>"
             "<li>Wird MeshDash (44.149.189.135) noch per EXTUDP beliefert, und wird es gebraucht? Ohne EXTUDP gäbe es diesen Weg in den "
             "Hänger nicht.</li>"
             "<li>Hat der HAMNET-Router am 03.11. gegen 05:00 und am 14.11. gegen 05:40 etwas getan (Neustart, Wartung)?</li></ol>")

    b.append("<h2>Methode</h2><ul>"
             "<li>Messungen mit <code>tools/logreport</code> (txpool, health, timesignal) auf dem gesunden Teil bis Zeile 4.448.151, "
             "dazu eine eigene Auswertung über den ganzen Mitschnitt (<code>generators/lib/db0ais_14d_measure.py</code>).</li>"
             "<li>Zeitbasis: Millisekundenzähler des Knotens (<code>t=</code>, <code>ms;</code>) ab PuTTY-Start 30.10. 17:01:11. "
             "Die Knotenuhr lief bis 18:20 am 30.10. eine Stunde vor und blieb ab dem Hänger stehen; deshalb nicht benutzt.</li>"
             "<li>Gezählt werden Ereignisse, nicht Zeilen. <code>[LOG]</code>-Empfänge sind empfangene Rahmen vor der Doppel-Prüfung, "
             "keine angenommenen Pakete; Rahmen mit Prüfsumme 0000 (78) sind ausgeschlossen.</li>"
             "<li>Quellcode: <code>src/extudp_functions.cpp</code> (extSendDatagram), <code>src/nrf52/nrf_eth.cpp</code> "
             "(Hardware-Reset nach DHCP-Fehlschlag), <code>src/nrf52/nrf52_main.cpp</code> (EXTUDP-Neustart im 15-min-Block), "
             "Bibliothek RAK13800-W5100S <code>socket.cpp</code>.</li>"
             "<li>Gegenprobe: mcmap <code>gateway_availability</code> für DB0AIS-12, 10 Tage, abgefragt 21.11.2026.</li></ul>")
    b.append('<p class="foot">MeshCom Firmware Log-Auswertung, DK5EN, 21.11.2026. Vorbericht: 24-h-Auswertung vom 01.11.2026.</p>')

    a.out.write_text(page("MeshCom Log-Auswertung DB0AIS-12, 14 Tage", "\n".join(b)), encoding="utf-8")
    print("wrote", a.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
