#!/usr/bin/env python3
"""Per-node HTML report for OE3XOC-12 (24-h log 2026-10-31/11-01).

Usage: python3 -I oe3xoc.py --data <dir> --kit <dir> --out <file>
  --data  run data dir (health/ q2/ q34/ q5/ q6/ master/ extra/ and CORE-FINDINGS.md)
  --kit   dir holding reportkit.py
  --out   html file to write

Stdlib only; no raw logs at render time. The own-beacon timeline, the head-of-queue analysis and the
reception-gap figures come from extra/oe3xoc_ownbeacons.json, written once from the normalised log by
lib/oe3xoc_ownbeacons.py (--norm <dir> --out <file>). Judgement text may carry literals from
CORE-FINDINGS.md and verify/*.md; they are named in the developer details.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

OE3 = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]


def tm(iso: str) -> str:
    """ISO logger time -> 'dd.mm. HH:MM' (WX list) in German style."""
    d = datetime.fromisoformat(iso)
    return d.strftime("%d.%m. %H:%M")


def tms(iso: str) -> str:
    d = datetime.fromisoformat(iso)
    return d.strftime("%d.%m. %H:%M:%S")


def hhmm(s: float) -> str:
    return f"{s / 3600:.2f}".replace(".", ",")


def fmt(v: float, digits: int = 0) -> str:
    """German number format: thousands '.', decimals ','."""
    s = f"{v:,.{digits}f}"
    return s.replace(",", "X").replace(".", ",").replace("X", ".")


def load(data: Path, rel: str) -> Any:
    return json.loads((data / rel).read_text(encoding="utf-8"))


def load_master(data: Path) -> dict[str, dict[str, str]]:
    rows = (data / "master" / "master.tsv").read_text(encoding="utf-8").splitlines()
    head = rows[0].split("\t")
    out: dict[str, dict[str, str]] = {}
    for ln in rows[1:]:
        parts = ln.split("\t")
        parts += [""] * (len(head) - len(parts))
        out[parts[0]] = dict(zip(head, parts, strict=False))
    return out


def load_dist(data: Path) -> dict[str, str]:
    rows = (data / "master" / "distances.tsv").read_text(encoding="utf-8").splitlines()
    head = rows[0].split("\t")
    col = head.index("d_oe3xoc")
    out: dict[str, str] = {}
    for ln in rows[1:]:
        parts = ln.split("\t")
        if len(parts) > col and parts[col]:
            out[parts[0]] = parts[col]
    return out


# ---------------------------------------------------------------------------
# SVG helpers (classes only, no colours)
# ---------------------------------------------------------------------------
def r(x: int, y: int, w: int, h: int, cls: str = "", extra: str = "") -> str:
    return f'<rect class="d-box {cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="6" {extra}/>'


def c(x: int, y: int, rad: int, cls: str = "", extra: str = "") -> str:
    return f'<circle class="d-node {cls}" cx="{x}" cy="{y}" r="{rad}" {extra}/>'


def t(x: int, y: int, s: str, small: bool = False, anchor: str = "middle") -> str:
    cl = "d-txt-s" if small else "d-txt"
    return f'<text class="{cl}" x="{x}" y="{y}" text-anchor="{anchor}">{s}</text>'


def ln(x1: int, y1: int, x2: int, y2: int, cls: str = "", extra: str = "") -> str:
    return f'<line class="d-line {cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" {extra}/>'


# ---------------------------------------------------------------------------
def build(data: Path, kit: Path, out: Path) -> None:
    sys.path.insert(0, str(kit))
    from reportkit import bars, chip, esc, explain, hist, page, table, timeline  # noqa: PLC0415

    q2 = load(data, "q2/results.json")["nodes"]["oe3xoc"]
    q34 = load(data, "q34/results.json")["nodes"]["oe3xoc"]
    q5 = load(data, "q5/results.json")["nodes"]["oe3xoc"]
    q6 = load(data, "q6/results.json")["nodes"]["oe3xoc"]
    hl = load(data, "health/results.json")["nodes"]["oe3xoc"]
    ob = load(data, "extra/oe3xoc_ownbeacons.json")
    q34all = load(data, "q34/results.json")["nodes"]
    master = load_master(data)
    dist = load_dist(data)

    # ---- numbers used in several places -------------------------------------------------------
    fate = q2["fate"]
    csma = q34["csma"]
    cf = q34["cf"]
    hz = ob["head_hazard"]
    ha = ob["head_analysis"]
    rxa = ob["rx_all"]
    fresh = cf["fresh_delayed"]
    stale_age = cf["stale_air_age_s_if_served"]
    figno = [0]

    def ab(text: str) -> str:
        figno[0] += 1
        return f"Abb. @@{figno[0]}@@: {text}"

    def ul(items: list[str]) -> str:
        return "<ul>" + "".join(f"<li>{i}</li>" for i in items) + "</ul>"

    def callsign_info(call: str) -> str:
        m = master.get(call)
        if call == "OE3XIR-12":
            return "keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg)"
        if not m:
            return "keine Stammdaten"
        place = m.get("description") or "ohne Ortsangabe"
        d = dist.get(call)
        ds = f"{fmt(float(d), 1)} km" if d else "Entfernung unbekannt"
        return (f"{m['hardware']}, Firmware {m['firmware']}, {place} "
                f"({m['lat']} N / {m['lon']} E), {m['altitudeM']} m, {ds}")

    # ---- Abbildungen: Diagramme ---------------------------------------------------------------
    util_h = [(k.split()[1], v) for k, v in hl["channel"]["util_by_hour"].items()]
    fig_util = hist(util_h, ab("Kanalauslastung je Stunde (Mittel, %), 31.10. 06 Uhr bis 01.11. 07 Uhr"),
                    unit=" %", ylabel="Stunde (Loggeruhr)")

    ackh = q2["extras"]["rx_ack_fwd_hist"]
    fig_ack = bars([("unter 5 s", ackh["<5"]), ("5 bis 10 s", ackh["5-10"]),
                    ("10 bis 20 s", ackh["10-20"]), ("20 bis 30 s", ackh["20-30"]),
                    ("30 bis 60 s", ackh["30-60"]), ("60 s und mehr", ackh[">=60"])],
                   ab("Wartezeit einer ACK-Weiterleitung in der Warteschlange (3.365 ACKs)"))

    oph = ob["own_per_hour"]
    xs = list(range(len(oph)))
    xl = [(float(i), h["hour"].split()[1]) for i, h in enumerate(oph) if h["hour"].split()[1] in ("06", "12", "18", "00")]
    xl.append((float(len(oph) - 1), oph[-1]["hour"].split()[1]))
    fig_beacon = timeline([("gesendet", [(float(x), float(oph[x]["air"])) for x in xs]),
                           ("verworfen (TTL)", [(float(x), float(oph[x]["ttl"])) for x in xs])],
                          ab(f"Eigene Baken (WX und HEY) je Stunde: gesendet gegen verworfen, "
                             f"Uhrzeit {oph[0]['hour'][:6]} {oph[0]['hour'][7:]} Uhr bis "
                             f"{oph[-1]['hour'][:6]} {oph[-1]['hour'][7:]} Uhr"), xlabels=xl)

    s5 = q34["ttl_sens"]["p5"]["table"]
    s4 = q34["ttl_sens"]["p4"]["table"]
    def lab(prio: str, name: str, k: str, now: str, row: dict[str, Any]) -> str:
        kind = "heute, Messung" if k == now else "Modell"
        return f"Prio {prio} ({name}), TTL {k} s ({kind}): davon eigene {row['own']}"

    sens_items = [
        (lab("5", "HEY", k, "180", s5[k]), float(s5[k]["n_over"])) for k in ("120", "150", "180", "240", "300")
    ] + [
        (lab("4", "Position", k, "120", s4[k]), float(s4[k]["n_over"])) for k in ("60", "90", "120", "180", "240")
    ]
    fig_sens = bars(sens_items, ab("Verworfene Einträge bei anderer TTL: Modell auf den Messdaten von "
                                   "OE3XOC-12 (heute Prio 5 = 180 s, Prio 4 = 120 s; nur die heutigen Werte "
                                   "sind gemessen, alle anderen sind Modell-Obergrenzen der ersten Näherung)"))

    csh = [(k.split()[1], float(v.get("csmaaged", 0))) for k, v in q6["per_hour"].items()]
    fig_csma = hist(csh, ab("CSMA_AGED-Ereignisse je Stunde (Verkürzung der Wartezeit alter Einträge)"),
                    color="var(--c4)", ylabel="Stunde (Loggeruhr)")

    # ---- Erklärzeichnungen ---------------------------------------------------------------------
    # E1 Verhungern: drei Teile
    hey_h = ha["own_HEY_ttl"]
    e1 = (
        t(320, 22, "Drei Teile: warum eine Bake am Kopf der Warteschlange nicht sendet")
        + r(10, 40, 200, 112, "d-acc") + t(110, 62, "1 Strikte Auswahl")
        + t(110, 84, "Prio 1, 3, 4, 5 der Reihe nach", True)
        + t(110, 102, f"{fmt(hey_h['blocked_pct'])} % der Zeit steht", True)
        + t(110, 120, "Höheres davor (OC, HEY)", True)
        + r(220, 40, 200, 112, "d-act anim-pulse") + t(320, 62, "2 Ruhezeit neu gestartet")
        + t(320, 84, "Prio 4/5: 5,5 s, bei jedem", True)
        + t(320, 102, "empfangenen Rahmen von vorn", True)
        + t(320, 120, f"{hey_h['gate_entries_in_head']} Kanalprüfungen in {fmt(hey_h['head_s_total'])} s", True)
        + r(430, 40, 200, 112, "d-watch") + t(530, 62, "3 Kein Altersboden")
        + t(530, 84, "Der Boden gilt für Prio 1 bis 3,", True)
        + t(530, 102, "nicht für Prio 4 und 5", True)
        + t(530, 120, "(csma_timing.h:99-111)", True)
        + ln(210, 96, 220, 96, "d-line") + ln(420, 96, 430, 96, "d-line")
        + t(320, 178, f"Sendezeit nur {fmt(q34['tx_duty_pct'], 1)} % der Zeit: Kanalzugang ist knapp, nicht die Sendezeit.", True)
        + r(10, 190, 620, 28, "d-act anim-drop")
        + t(320, 209, "Folge: Frist läuft ab (WX 120 s, HEY 180 s), der Eintrag wird verworfen.", True)
    )
    # E1b Kandidat Altersboden
    e1b = (
        t(320, 22, "Kandidat, nicht getestet: Altersboden für Prio 4/5 an Nicht-Relay-Slots")
        + r(10, 50, 62, 44, "d-neu") + t(41, 77, "Funk")
        + ln(80, 72, 88, 72, "d-line")
        + r(90, 50, 100, 44, "d-ok anim-pulse") + t(140, 77, "eigene Bake")
        + t(140, 114, "am Kopf", True)
        + r(210, 50, 200, 44, "d-neu") + t(310, 70, "bisher: 5,5 s Ruhezeit", True)
        + t(310, 86, "bei jedem Empfang neu", True)
        + r(430, 50, 200, 44, "d-ok") + t(530, 70, "Kandidat: ab 20 s Alter", True)
        + t(530, 86, "kürzere Ruhezeit (Schätzung)", True)
        + ln(190, 72, 210, 72, "d-line") + ln(410, 72, 430, 72, "d-ok-s anim-flow")
        + t(320, 150, "Wirkung nicht gemessen. Nur die TTL zu verlängern hilft nicht:", True)
        + t(320, 170, "die Bake würde länger am Kopf warten, ohne eher zu senden.", True)
    )
    # E1c eigene Bake gegen Relay: Ruhezeit am Kopf
    ticks_h = [40, 75, 100, 150, 180, 215, 260, 290, 335, 365, 410, 440, 490, 520, 570]
    own_rates = [v["tx_per_min"] for k, v in hz.items() if k in ("own_HEY", "own_WX")]
    rel_rates = [v["tx_per_min"] for k, v in hz.items() if k in ("relay_HEY", "relay_POS")]
    e1c_parts = [
        t(320, 22, "Gleiche Priorität, andere Ruhezeit am Kopf: eigene Bake gegen Relay"),
        t(20, 50, "Eigene Bake am Kopf (Prio 4/5): Ruhezeit 5,5 s, bei jedem Empfang neu", True, "start"),
    ]
    for i, x in enumerate(ticks_h):
        nxt = ticks_h[i + 1] if i + 1 < len(ticks_h) else 620
        e1c_parts.append(ln(x, 58, x, 78, "d-neu"))
        e1c_parts.append(r(x, 84, min(nxt - x, 110), 14, "d-neu"))
        e1c_parts.append(ln(x, 154, x, 174, "d-neu"))
        e1c_parts.append(r(x, 180, 6, 14, "d-ok"))
    e1c_parts += [
        t(20, 118, f"Folge: sendet nur {fmt(min(own_rates), 2)} bis {fmt(max(own_rates), 2)} mal pro Minute", True, "start"),
        t(20, 146, "Relay gleicher Prio (Fall A, nach 8 s Wartezeit): Ruhezeit 150 bis 220 ms", True, "start"),
        t(20, 216, f"Folge: sendet nach fast jedem Rahmen, {fmt(min(rel_rates), 1)} bis {fmt(max(rel_rates), 1)} mal pro Minute", True, "start"),
    ]
    e1c = "".join(e1c_parts)
    # E2 Hold wird neu gestartet
    ticks = [150, 200, 260, 310, 345, 400, 505, 540, 580]
    e2_parts = [
        t(320, 22, "Die Wartezeit startet nach jedem empfangenen Rahmen neu"),
        t(70, 62, "Empfang", True, "middle"),
        t(70, 112, "ACK 3,0 s", True, "middle"),
        t(70, 162, "Bake 5,5 s", True, "middle"),
    ]
    for i, x in enumerate(ticks):
        nxt = ticks[i + 1] if i + 1 < len(ticks) else 700
        e2_parts.append(ln(x, 46, x, 70, "d-neu"))
        gap = nxt - x
        ack_len = min(gap, 60)
        e2_parts.append(r(x, 98, ack_len, 18, "d-ok" if gap >= 60 else "d-neu"))
        bk_len = min(gap, 110, 620 - x)
        e2_parts.append(r(x, 148, bk_len, 18, "d-neu"))
    e2_parts += [
        ln(120, 70, 620, 70, "d-neu"),
        t(320, 196, f"Median-Abstand zwischen Empfaengen {fmt(rxa['gap_s_median'], 2)} s: ACKs kommen durch, Baken kaum.", True),
    ]
    e2 = "".join(e2_parts)
    # E3 Ping -> ACKs -> Relays
    e3 = (
        t(320, 22, "Ein Ping pro Minute erzeugt ACK-Rahmen, die jeder Knoten weiterleitet")
        + r(10, 80, 130, 60, "d-acc") + t(75, 106, "OE3XWJ-12") + t(75, 126, "Ping 1 pro Minute", True)
        + c(215, 70, 14, "d-neu") + c(215, 110, 14, "d-neu") + c(215, 150, 14, "d-neu")
        + ln(140, 110, 201, 70, "d-acc-s anim-flow") + ln(140, 110, 201, 110, "d-acc-s anim-flow")
        + ln(140, 110, 201, 150, "d-acc-s anim-flow")
        + ln(229, 70, 460, 100, "d-act-s anim-flow") + ln(229, 110, 460, 110, "d-act-s anim-flow")
        + ln(229, 150, 460, 120, "d-act-s anim-flow")
        + r(460, 80, 170, 60, "d-act") + t(545, 106, "OE3XOC-12") + t(545, 126, "3.378 ACK-Relays", True)
        + t(320, 184, "98,8 % der ACK-Rahmen bei OC (27.340 von 27.675) quittieren WJ-Pings.", True)
        + t(320, 204, "ACK-Relays sind 53,8 % der OC-Sendungen (3.378 von 6.276).", True)
    )
    # E4 Relay trotz gehoerter Kopie (ACK ohne Abbruch)
    e4 = (
        t(320, 22, "ACK-Weiterleitung hat keinen Abbruch, auch wenn eine Kopie schon lief")
        + r(10, 50, 140, 50, "d-neu") + t(80, 72, "Nachbar sendet") + t(80, 90, "denselben ACK", True)
        + ln(150, 75, 250, 75, "d-ok-s anim-flow")
        + r(250, 50, 140, 50, "d-ok") + t(320, 72, "OC hoert Kopie") + t(320, 90, "waehrend Wartezeit", True)
        + r(250, 130, 140, 50, "d-act anim-pulse") + t(320, 152, "OC sendet trotzdem") + t(320, 170, "ohne Abbruch-Pfad", True)
        + ln(320, 100, 320, 130, "d-act-s")
        + ln(390, 155, 480, 155, "d-act-s anim-flow") + c(520, 155, 22, "d-neu") + t(520, 160, "Hoerer", True)
        + t(540, 80, "2.494 von 3.378 ACK-Relays", True) + t(540, 98, "(73,8 %), Obergrenze", True)
    )
    # E4b Sole provider
    e4b = (
        t(320, 22, "Alleinzulieferer-Menge nie leer: OC sieht stets Fall A (kein Abbruch)")
        + r(250, 80, 140, 50, "d-act") + t(320, 102, "OE3XOC-12") + t(320, 120, "NEED: Fall A 100 %", True)
        + r(10, 40, 150, 34, "d-neu") + t(85, 62, "OE3XIA-12 hoert", True)
        + r(10, 88, 150, 34, "d-neu") + t(85, 110, "OE3XIR-12 hoert", True)
        + r(10, 136, 150, 34, "d-neu") + t(85, 158, "OE3XWJ-12 hoert", True)
        + r(470, 40, 160, 34, "d-act") + t(550, 62, "OE3MIF-12 (2863)", True)
        + r(470, 88, 160, 34, "d-act") + t(550, 110, "OE3MTC-10 (2855)", True)
        + r(470, 136, 160, 34, "d-act") + t(550, 158, "OE3NRA-1 (2840)", True)
        + ln(160, 57, 470, 57, "d-neu-s") + ln(160, 105, 470, 105, "d-neu-s") + ln(160, 153, 470, 153, "d-neu-s")
        + ln(390, 105, 470, 105, "d-act-s anim-flow")
        + t(320, 200, "Andere Knoten hoeren diese Stationen direkt: kein Abbruch moeglich.", True)
    )
    # E5 veraltete CET
    e5 = (
        t(320, 22, "Veraltete Zeitansage ({CET}): fremde Relays halten sie Minuten")
        + r(10, 70, 110, 60, "d-ok") + t(65, 96, "Server") + t(65, 114, "Alter 0 s", True)
        + ln(120, 100, 170, 100, "d-ok-s anim-flow")
        + r(170, 70, 140, 60, "d-ok") + t(240, 96, "Gateways") + t(240, 114, "Alter Sekunden", True)
        + ln(310, 100, 350, 100, "d-act-s anim-flow")
        + r(350, 70, 140, 60, "d-act anim-pulse") + t(420, 90, "Fremdes Relay", True) + t(420, 110, f"Funkkopien p90 {fmt(q5['rf_age_s']['p90'])} s", True)
        + t(420, 124, f"Maximum {fmt(q5['rf_age_s']['max'])} s", True)
        + ln(490, 100, 530, 100, "d-act-s anim-flow")
        + r(530, 70, 100, 60, "d-watch") + t(580, 94, "OC-Tor", True) + t(580, 112, "ab 150 s", True)
        + t(580, 126, "verwerfen", True)
        + t(320, 176, "OC stoppte 2 solche Relays (Alter 2011 s und 2508 s).", True)
        + t(320, 198, f"Eigene Sendungen: {q5['tx_age_gt150_n']} von {q5['tx_count']} aelter als 150 s (Max. {fmt(q5['tx_age_s_logger_based']['max'])} s).", True)
    )
    # E6 Server-Inject
    e6 = (
        t(320, 22, "Jede Server-Zeitansage senden alle fuenf Gateways der Gruppe")
        + r(250, 40, 140, 40, "d-ok") + t(320, 65, "Server")
        + c(90, 150, 26, "d-neu") + c(205, 150, 26, "d-neu") + c(320, 150, 26, "d-act anim-pulse")
        + c(435, 150, 26, "d-neu") + c(550, 150, 26, "d-neu")
        + t(90, 155, "HL", True) + t(205, 155, "IA", True) + t(320, 155, "OC", True)
        + t(435, 155, "IR", True) + t(550, 155, "WJ", True)
        + ln(300, 80, 100, 124, "d-ok-s anim-flow") + ln(310, 80, 205, 124, "d-ok-s anim-flow")
        + ln(320, 80, 320, 124, "d-ok-s anim-flow") + ln(330, 80, 435, 124, "d-ok-s anim-flow")
        + ln(340, 80, 540, 124, "d-ok-s anim-flow")
        + t(320, 200, "OC sendet in 131 von 151 Faellen (86,8 %), nachdem schon eine Kopie auf Funk war.", True)
    )
    # E7 RX-Fehler
    e7 = (
        t(320, 22, "RX-Fehler: Praeambel erkannt, aber kein gueltiger Rahmen")
        + ln(40, 150, 600, 150, "d-neu-s")
        + t(320, 172, "Rauschteppich um -112 dBm (p5 der Fehler-RSSI)", True)
        + r(80, 50, 60, 100, "d-ok") + t(110, 40, "Rahmen", True) + t(110, 100, "ok", True)
        + r(200, 110, 60, 40, "d-act anim-pulse") + t(230, 102, "Fehler", True)
        + r(320, 120, 60, 30, "d-act anim-pulse") + t(350, 112, "Fehler", True)
        + r(440, 70, 60, 80, "d-ok") + t(470, 62, "Rahmen", True) + t(470, 114, "ok", True)
        + t(320, 198, f"{fmt(rxa['errors'])} RX-Fehler = {fmt(rxa['error_share_pct'], 1)} % aller Empfangsversuche, Median-SNR {fmt(hl['rf']['rx_error_snr']['median'])} dB.", True)
    )
    # E8 Dedup-Fenster
    e8 = (
        t(320, 22, "Dedup-Ring: 100 Eintraege, bei OC reicht er nur etwa 17,7 Minuten")
        + ln(40, 110, 600, 110, "d-neu-s")
        + r(40, 90, 330, 40, "d-ok") + t(205, 115, "ID bekannt: Kopie wird verworfen")
        + r(370, 90, 230, 40, "d-act anim-pulse") + t(485, 115, "ID vergessen: Kopie ist neu")
        + t(40, 150, "Rahmen kommt", True, "start") + t(370, 150, "Ring ueberschrieben", True, "start")
        + t(320, 184, "OC nahm 38 fremde IDs ein zweites Mal an (7 nach mehr als 10 min, 1 nach ueber 30 min).", True)
    )
    # E9 Firmware ohne CAD
    e9 = (
        t(320, 22, "Aeltere Firmware ohne Kanalabfrage (CAD) sendet auch auf belegtem Kanal")
        + t(60, 62, "Mit CAD", True) + r(110, 46, 200, 28, "d-ok") + t(210, 65, "wartet, dann Sendung", True)
        + r(330, 46, 120, 28, "d-neu") + t(390, 65, "Sendung", True)
        + t(60, 122, "Ohne CAD", True) + r(110, 106, 200, 28, "d-neu") + t(210, 125, "Sendung", True)
        + r(250, 106, 160, 28, "d-act anim-pulse") + t(330, 125, "Ueberlappung", True)
        + t(320, 170, "19 von 215 Stationen (8,8 %) laufen ohne CAD, sie stellen 16,4 % der Empfaenge.", True)
        + t(320, 192, "Ob daraus RX-Fehler entstehen, zeigt das Log nicht (Vermutung).", True)
    )

    # E3b Ping-Zaehler Plattformunterschied
    e3b_parts = [
        t(320, 22, "Auto-Ping: ESP32 zählt herunter, RAK4631 hat keinen Zähler"),
        t(20, 56, "ESP32", False, "start"),
        t(20, 120, "RAK4631", False, "start"),
    ]
    for i in range(3):
        e3b_parts.append(r(110 + i * 90, 36, 76, 30, "d-ok"))
        e3b_parts.append(t(148 + i * 90, 56, f"Ping {3 - i}", True))
    e3b_parts.append(r(380, 36, 240, 30, "d-neu"))
    e3b_parts.append(t(500, 56, "Zähler 0: Ende", True))
    for i in range(5):
        e3b_parts.append(r(110 + i * 90, 100, 76, 30, "d-watch anim-pulse" if i == 4 else "d-watch"))
        e3b_parts.append(t(148 + i * 90, 120, "Ping", True))
    e3b_parts.append(ln(110, 150, 600, 150, "d-watch-s anim-flow"))
    e3b_parts.append(t(320, 172, "Kein Zähler: Ping alle node_pingtime s, solange ein Ziel gesetzt ist.", True))
    e3b = "".join(e3b_parts)

    def ex(svg: str, cap: str, h: int = 220) -> str:
        return explain(svg, ab(cap), height=h)

    # ---- Nachbartabelle ------------------------------------------------------------------------
    nb = hl["neighbours"]["top"]

    nb_rows = []
    for n in nb:
        call = n["call"]
        m = master.get(call)
        if call == "OE3XIR-12":
            ort, hoe, ds = "keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg)", "unbekannt", "unbekannt"
        elif m:
            ort = (m.get("description") or "ohne Ortsangabe") + f" ({m['lat']} N / {m['lon']} E)"
            hoe = f"{m['altitudeM']} m"
            ds = f"{fmt(float(dist[call]), 1)} km" if call in dist else "unbekannt"
        else:
            ort, hoe, ds = "keine Stammdaten", "unbekannt", "unbekannt"
        fwl = n.get("fw_log") or ""
        fw = m["firmware"] if m else (("4." + fwl.replace(":", "")) if fwl else "unbekannt")
        hw = (m["hardware"] if m else None) or n.get("hw") or "unbekannt"
        nb_rows.append([call, hw, fw, ort, hoe, ds, f"{fmt(n['rssi_median'])} dBm / {fmt(n['snr_median'])} dB",
                        fmt(n["n"])])
    nb_head = ["Call", "Hardware", "Firmware", "Standort", "Höhe", "Distanz", "RSSI / SNR (Median)",
               "Empfänge"]
    nb_main = table(nb_head, nb_rows[:10], num_cols=(7,))
    nb_all = table(nb_head, nb_rows, num_cols=(7,))

    # ---- abgeleitete Zahlen ---------------------------------------------------------------------
    tr = hl["traffic"]
    otx = hl["own_tx"]
    chu = hl["channel"]["CHANNEL_UTIL"]
    cadh = hl["cad"]
    rfh = hl["rf"]
    sth = hl["stat"]
    fch = hl["firmware_census"]
    ringh = hl["ring"]
    ws = ob["wx_summary"]
    hs = ob["hey_summary"]
    span_h = hl["span"]["hours"]
    n_tx = otx["tx_lines"]
    ackpfx = hl["ack_attribution"]["top_acked_prefix"][0]
    ack_relays = otx["prio_split"]["1"]
    ack_relay_pct = 100 * ack_relays / n_tx
    csma_cluster = sum(v["csma"]["events"] for k, v in q34all.items() if k in OE3)
    fb = q6["totals"]
    co = q6["cancel_opportunity"]
    op = q6["opportunity_airtime"]
    rel_fate = q6["relay_fate"]
    lat_s = q2["latency"]["by_prio_s"]
    cs_ep = csma["ep_prio"]
    gap_wx = ws["longest_gap_s"]
    lost_gap = [w for w in ob["wx"] if w["fate"] == "ttl" and ws["longest_gap_start"] < w["t"] < ws["longest_gap_end"]]
    lost_gap_first, lost_gap_last = tm(lost_gap[0]["t"])[-5:], tm(lost_gap[-1]["t"])[-5:]
    win_tx = ob["window_tx_by_prio"]
    alone = q6["alone_hist"]
    alone_n = alone["need_lines"]
    top_alone = {c_: n_ for c_, n_ in alone["top_alone_calls"]}
    dedup = hl["ring"]["dedup"]
    dedup_rate = dedup["add"] / (span_h * 60)
    dedup_min = dedup["ring_size"] / dedup_rate
    loops = q6["loops"]
    relay_class = q34["relay_class"]
    rc_covered = relay_class["redundant_cluster_tx"] + relay_class["redundant_heard_lower_hop"]
    rc_weak = relay_class["weak_heard_same_hop"] + relay_class["weak_cluster_tx_before"]
    rc_none = relay_class["no_evidence"]
    rc_all = rc_covered + rc_weak + rc_none
    hey_air_pct_ep = fmt(hs["air_pct_of_episodes"], 1)
    hey_air_pct_wr = fmt(hs["air_pct_of_written"], 1)
    own_lost = q34["own_dropped"]
    own_wx_pct = 100 * ws["ttl"] / ws["total"]
    night_l, night_n = ws["lost_written_21_to_08"], ws["written_21_to_08"]
    day_l, day_n = ws["ttl"] - night_l, ws["total"] - night_n
    rx_err_share = fmt(rxa["error_share_pct"], 1)
    fd = fate["counts"]
    fp = fate["pct"]

    # ---- Antwort-Index --------------------------------------------------------------------------
    idx_rows = [
        ["Q0 Gesundheit", f"Stabil (kein Neustart, kein Speicherleck, kein Überlauf). Höchste Last der Gruppe: "
         f"{fmt(chu['mean'], 1)} % mittlere Auslastung, {fmt(otx['tx_per_hour'], 1)} Sendungen pro Stunde. "
         f"RX-Fehler {rx_err_share} % (unter der Schwelle von 40 %).", chip("ok")],
        ["Q2 TX-Puffer", f"Mechanik in Ordnung: 0 Überläufe, höchstens {ringh['max']['queued']} von {ringh['MAX_RING']} "
         "Plätzen, jedes Schicksal erklärt. Die Auswahlregel davor lässt Prio-4/5-Baken verhungern.", chip("act")],
        ["Q3 Verlust durch Zeitverfall", f"{fmt(q34['drops_total'])} Einträge verworfen, {own_lost} davon eigene Baken "
         f"(WX {ws['ttl']} von {ws['total']}, HEY {hs['ttl']} von {hs['episodes']} Episoden). Texte und ACKs: 0.", chip("act")],
        ["Q4 Gewinn durch Zeitverfall", f"Frische, nicht Sendezeit: {fmt(q34['drop_toa_s'], 1)} s Sendezeit gespart "
         f"({fmt(q34['drop_toa_pct_of_tx'], 2)} % der OC-Sendezeit). Ohne TTL würden laut Modell "
         f"{fmt(fresh['pct_delayed'], 1)} % der frischen Einträge später laufen.", chip("ok")],
        ["Q5 Zeitsignal", f"{q5['udp_got']} von {q5['udp_got']} Telegrammen angekommen, {q5['tx_age_gt150_n']} veraltete selbst "
         f"gesendet. {fmt(q5['tx_after_overheard_pct'], 1)} % der OC-Sendungen folgen einer bereits gehörten Kopie.",
         chip("watch")],
        ["Q6 Manipulationen", f"Kein Abbruch für ACK-Relays, Alleinzulieferer-Fall A {fmt(alone_n)} von {fmt(alone_n)}, "
         f"{fmt(ack_relay_pct, 1)} % der Sendungen sind ACK-Relays auf Pings von OE3XWJ-12 (Cluster-Befund).", chip("act")],
    ]
    idx = table(["Frage", "Kurzantwort", "Urteil"], idx_rows, raw=True)

    # ---- Abschnitte ------------------------------------------------------------------------------
    b: list[str] = []
    b.append('<div class="eyebrow">MeshCom Log-Auswertung · OE3XOC-12</div>')
    b.append("<h1>OE3XOC-12 trägt die höchste Last der Gruppe und lässt dabei seine eigenen "
             "Wetter- und HEY-Baken verhungern</h1>")
    mo = master["OE3XOC-12"]
    b.append('<ul class="meta">'
             f"<li><b>Log-Datei:</b> oe3xoc.log ({fmt(hl['span']['lines'])} Zeilen)</li>"
             f"<li><b>Zeitraum:</b> {tms(ob['meta']['first'])[:6]}2026 {tms(ob['meta']['first'])[7:]} bis "
             f"{tms(ob['meta']['last'])[:6]}2026 {tms(ob['meta']['last'])[7:]}, {fmt(span_h, 2)} h "
             "(Uhr des Loggers; Knotenuhr -1 s dazu; "
             f"Knoten lief schon {sth['boot_before_capture_start_s']} s vor Logbeginn, kein Neustart)</li>"
             "<li><b>Hardware:</b> RAK4631 (nRF52840) mit Ethernet (Chip W5100S, Vermutung, nicht im Log)</li>"
             "<li><b>Firmware:</b> 4.40a, Release-Stempel 20260929 (FLASH_VERSION, kein Baudatum), "
             "genauer Build unbekannt</li>"
             f"<li><b>Standort/Höhe:</b> {esc(mo['description'])}, {mo['lat'].replace('.', ',')} N / {mo['lon'].replace('.', ',')} E, "
             f"{mo['altitudeM']} m (Stammdaten)</li>"
             "<li><b>Rolle:</b> Internet-Gateway und Relay</li>"
             "<li><b>LoRa:</b> SF11, BW250, CR4/6 (Code am HEAD, <code>lora_setchip.cpp:199-200</code>; für diesen "
             "Build Vermutung), Sendeleistung 22 dBm (Log), Frequenz nicht im Log</li>"
             "<li><b>Stand:</b> 01.11.2026</li></ul>")
    b.append('<div class="bluf"><p class="lead">'
             f"OE3XOC-12 ist mit {fmt(chu['mean'], 1)} % mittlerer Kanalauslastung und {fmt(otx['tx_per_hour'], 1)} "
             "Sendungen pro Stunde der am stärksten belastete der fünf Knoten und läuft dabei stabil. "
             f"Seine eigenen Baken verhungern: Wetterposition {ws['ttl']} von {ws['total']} verworfen, "
             f"HEY nur zu {hey_air_pct_ep} % gesendet (von {hs['episodes']} Episoden; "
             f"von allen {hs['written']} geschriebenen HEY {fmt(hs['air_pct_of_written'], 0)} %).</p>"
             '<p><a href="uebersicht.html">Netzübersicht</a></p></div>')
    b.append('<div class="callout act"><b>Handlungsbedarf (Entscheidung beim Betreiber):</b> Die eigene '
             f"Wetterposition war {fmt(gap_wx / 3600, 2)} Stunden nicht auf Funk "
             f"({tms(ws['longest_gap_start'])} bis {tms(ws['longest_gap_end'])}). "
             f"Über UDP ging sie jedes Mal ins Internet (34 von 34).<ul>"
             "<li><b>Ursache (Befund):</b> Am Kopf der Warteschlange bekommt eine eigene Bake die Ruhezeit von "
             "5,5 s, die jeder empfangene Rahmen neu startet. Ein Relay gleicher Priorität nimmt den Pfad "
             "„Fall A“ und darf nach 8 s Wartezeit schon nach 150 bis 220 ms senden. Eigene Baken sind von "
             "diesem Pfad und vom Altersboden ausgenommen.</li>"
             "<li><b>Kandidat A (ungetestet):</b> Altersboden auch für Prio 4 und 5 an Nicht-Relay-Slots "
             "(<code>csma_timing.h:99-111</code>, begrenzt in <code>lora_functions.cpp:4030-4043</code>). "
             "Wirkung nicht gemessen.</li>"
             "<li><b>Kandidat B (größerer Eingriff, ungetestet):</b> die Kurzsuche aus Fall A auch für eigene "
             "Prio-4/5-Einträge nach 8 s.</li>"
             "<li><b>Eine TTL je Klasse allein hilft nicht:</b> eine längere Frist lässt die Bake nur länger "
             "am Kopf warten, ohne dass sie früher sendet. Nötig wäre eine Änderung am Kanalzugang.</li>"
             "<li><b>Option C:</b> so lassen, weil das Internet die Wetterposition ohnehin bekommt.</li>"
             "</ul></div>")

    b.append("<h2>Antwort-Index</h2>")
    b.append(idx)

    # --- Gesundheit
    b.append("<h2>Gesundheit und Funkumfeld (Q0)</h2>")
    b.append("<p><b>Der Knoten ist technisch gesund, aber der Kanal um ihn ist voll.</b></p>")
    src_pct = otx["src_pct"]
    cp = cadh["cad_attempt_pct"]
    b.append(ul([
        f"<b>Verkehr:</b> {fmt(tr['heard_events'])} gehörte Rahmen (Text, Position, HEY) plus {fmt(tr['ack_events'])} "
        f"ACK-Rahmen. ACKs sind {fmt(tr['ack_share_of_all_frames_pct'], 1)} % aller Empfänge. Neu "
        f"{fmt(tr['dup_flag']['n'])}, Duplikate {fmt(tr['dup_flag']['d'])} "
        f"(ohne {tr['fcs0000_excluded']} Dekoder-Abbrüche mit FCS:0000).",
        f"<b>Eigene Sendungen:</b> {fmt(n_tx)} ({fmt(otx['tx_per_hour'], 1)} pro Stunde), {fmt(otx['airtime_s'])} s "
        f"Sendezeit ({fmt(q34['tx_duty_pct'], 1)} % Dauer). {fmt(src_pct['r'], 1)} % sind Relays, "
        f"{fmt(src_pct['g'], 1)} % Server-Einspeisung, {fmt(src_pct['o'], 1)} % eigene Baken ({otx['src_split']['o']}).",
        f"<b>Auslastung:</b> Mittel {fmt(chu['mean'], 1)} %, Median {fmt(chu['median'], 1)} %, p95 {fmt(chu['p95'])} %, "
        f"Spitze {fmt(chu['max'])} % ({chu['ge70']} Messwerte ab 70 %, {chu['ge90']} ab 90 %). Ganztags gleichmäßig.",
        f"<b>Rauschen:</b> Rauschteppich um {fmt(rfh['rx_error_rssi']['p5'])} dBm (p5 der Fehler-RSSI), Median "
        f"{fmt(rfh['rx_error_rssi']['median'])} dBm, etwa 2 dB über der Theorie für BW250 "
        f"({fmt(rfh['theory_floor_bw250_dbm'])} dBm). Stärkster Empfang {fmt(rfh['rssi_all']['max'])} dBm, "
        "nichts über -40 dBm.",
        f"<b>Kanalabfrage (CAD):</b> {fmt(cadh['cad_scans'])} Abfragen, {fmt(cadh['initial_busy_rate_pct'], 1)} % zunächst "
        f"belegt, davon {fmt(cadh['confirmed_busy_pct_of_initial_busy'], 1)} % bestätigt. Beim Sendestart: "
        f"{fmt(cp['0'], 1)} % ohne Wartezug, {fmt(cp['1'], 1)} % mit einem, {fmt(cp['2'], 1)} % mit zwei, "
        f"{fmt(cadh['attempt_ge3_pct'], 1)} % mit drei oder mehr (Höchstwert {cadh['attempt_max']}).",
        f"<b>Absturz, Speicher, Ethernet:</b> 0 Neustarts, 0 Abstürze, freier Speicher {fmt(sth['heap']['first'])} auf "
        f"{fmt(sth['heap']['last'])} Byte ({fmt(sth['heap']['slope_per_hour'], 1)} Byte pro Stunde, flach). Ethernet: "
        f"{hl['eth']['counters_final']['tx_fail']} Sendefehler, DHCP alle 5 Minuten erneuert "
        f"({hl['eth']['counters_final']['renews']} mal, {hl['eth']['counters_final']['renew_fail']} Fehler). "
        "Ein Server-Ausfall am 31.10. um 07:22 (etwa 39 s) traf alle fünf Knoten gleichzeitig: Serverseite.",
        f"<b>Empfangslücken (alle Empfänge inklusive ACK):</b> keine über 5 Minuten. Median zwischen zwei Empfängen "
        f"{fmt(rxa['gap_s_median'], 2)} s, längste Lücke {fmt(rxa['gap_s_longest'])} s. OE3XIA-12 hat dagegen "
        "35 Stillstände über 5 Minuten bei weiter laufender Kanalabfrage (Empfänger vermutlich taub, siehe "
        '<a href="oe3xia.html">Seite OE3XIA-12</a>).',
        f"<b>Ältere Firmware ohne CAD:</b> {fch['nodes_pre_cad']} von {fch['nodes_total']} gehörten Stationen "
        f"({fmt(fch['nodes_pre_cad_pct'], 1)} %), sie stellen {fmt(fch['events_pre_cad_pct'], 1)} % der Empfänge und "
        f"{fmt(fch['airtime_pre_cad_pct'], 1)} % der Sendezeit.",
    ]))
    b.append(fig_util)
    ats = tr["airtime_share_by_type_pct"]
    b.append(table(["Sendezeit der empfangenen Rahmen nach Typ", "Anteil"],
                   [["ACK-Rahmen", f"{fmt(ats['ACK'], 1)} %"], ["HEY (Heartbeat)", f"{fmt(ats['@'], 1)} %"],
                    ["Texte", f"{fmt(ats[':'], 1)} %"], ["Positionen", f"{fmt(ats['!'], 1)} %"],
                    ["Summe", f"{fmt(sum(ats.values()), 1)} %"]], num_cols=(1,)))
    ps = otx["prio_split"]
    prows = [["Prio 1 (ACK-Relays)", fmt(ps["1"])],
             [f"Prio 3 (Server-Einspeisung {otx['src_split']['g']}, Relay-Text {otx['src_type']['r::']})", fmt(ps["3"])],
             ["Prio 4 (Positionen)", fmt(ps["4"])], ["Prio 5 (HEY)", fmt(ps["5"])]]
    prows = [[a_, n_, f"{fmt(100 * int(n_.replace('.', '')) / n_tx, 1)} %"] for a_, n_ in prows]
    prows.append(["Summe", fmt(n_tx), "100,0 %"])
    b.append(table(["Eigene Sendungen nach Priorität", "Anzahl", "Anteil"], prows, num_cols=(1, 2)))
    b.append("<h3>Unauffällig: RX-Fehler</h3>")
    b.append(ul([
        f"<b>Was:</b> {fmt(rxa['errors'])} RX-Fehler ({fmt(rfh['rx_error_per_hour_mean'])} pro Stunde): Präambel oder Kopf "
        f"erkannt, aber kein gültiger Rahmen. Das sind {rx_err_share} % aller Empfangsversuche "
        f"(Fehler {fmt(rxa['errors'])} geteilt durch Fehler plus alle {fmt(rxa['decoded'])} dekodierten Rahmen, ACKs "
        "eingeschlossen).",
        "<b>Nenner:</b> ACK-Empfänge sind mitgezählt (sie drucken keine OnRxDone-Zeile, nur ONRXDONE_TIME); ohne "
        "sie wäre der Anteil zu hoch.",
        f"<b>Befund:</b> Median-Fehler-SNR {fmt(rfh['rx_error_snr']['median'])} dB, Median-RSSI "
        f"{fmt(rfh['rx_error_rssi']['median'])} dBm: rauschbegrenzt. {fmt(rfh['rx_error_gt_minus85'])} Fehler über -85 dBm, "
        f"{rfh['rx_error_gt_minus60']} über -60 dBm, keine Übersteuerung.",
        "<b>Urteil:</b> unter der Schwelle von 40 %, kein Befund am Knoten; es ist die Funklage am Buchberg.",
    ]))
    b.append(ex(e7, "Rauschbegrenzter Empfang: ein Teil der Rahmen geht unter dem Rauschteppich verloren", 215))
    b.append("<h3>Beobachten: Firmware ohne Kanalabfrage</h3>")
    pre = fch["top_pre_cad"][:3]
    b.append(ul([
        "<b>Was:</b> Stationen mit Firmware vor CAD (4.35 und älter, genauer: vor 4.35p) senden, ohne den Kanal zu "
        "prüfen. Größte Sender: " + ", ".join(f"{p_['call']} ({fmt(p_['events'])} Empfänge)" for p_ in pre) + ".",
        "<b>Urteil:</b> Beobachten. Das ist die Nachbarschaft, nicht die Gesundheit dieses Knotens. "
        "Ein Kollisionsbeleg liegt nicht vor (Vermutung).",
    ]))
    b.append(ex(e9, "Ohne Kanalabfrage überlappen Sendungen auf belegtem Kanal", 215))

    # --- F2
    b.append("<h2>F2 TX-Puffer mit Slots (Q2)</h2>")
    b.append("<p><b>Der Puffer arbeitet sauber. Das Verhungern liegt an der Auswahl- und Wartezeitregel davor, "
             "nicht am Puffer.</b></p>")
    occ = q2["occupancy"]
    b.append(ul([
        f"<b>Belegung:</b> {fmt(fate['total_writes'])} Einträge geschrieben. Tiefe Median {fmt(occ['queued']['p50'])}, "
        f"p90 {fmt(occ['queued']['p90'])}, Maximum {fmt(occ['queued']['max'])} von {ringh['MAX_RING']}; nie ab 15. "
        f"{fmt(occ['minutes_ge_5'])} Minuten mit 5 oder mehr belegten Plätzen, {fmt(occ['minutes_ge_10'], 1)} Minuten "
        "mit 10 oder mehr.",
        "<b>Kein Aufstau:</b> Stundentrend +0,014 Plätze pro Stunde (Verifikation), 0 verlorene Neueinträge "
        "(RING_DROP_NEW/PRIO), kein hängender Slot (RING_STATUS retrying immer 0), 0 Neuversuche in der Retry-Leiter "
        "(für diese Daten ungetestet).",
        "<b>Schicksal aller Einträge:</b> geschlossen auf 100 %, nichts ungeklärt.",
    ]))
    b.append(table(["Schicksal", "Einträge", "Anteil"],
                   [["gesendet", fmt(fd["tx_air"]), f"{fmt(fp['tx_air'], 2)} %"],
                    ["durch neuere Bake ersetzt", fmt(fd["superseded"]), f"{fmt(fp['superseded'], 2)} %"],
                    ["Zeitverfall (TTL)", fmt(fd["stale_ttl"]), f"{fmt(fp['stale_ttl'], 2)} %"],
                    ["am Logende noch wartend", fmt(fd["pending_end"]), f"{fmt(fp['pending_end'], 2)} %"],
                    ["Summe", fmt(fate["total_writes"]), f"{fmt(fate['sum_pct'], 2)} %"]], num_cols=(1, 2)))
    p1 = lat_s["1"]
    ge20 = fmt(100 * q2["extras"]["prio1_ge20s_share"])
    b.append("<h3>Beobachten: ACK-Wartezeit 20 bis 60 s je Hop</h3>")
    b.append(ul([
        f"<b>Messung (echte ACKs):</b> ACK-Weiterleitung wartet im Median {fmt(p1['p50'], 1)} s, p90 {fmt(p1['p90'], 1)} s, "
        f"Maximum {fmt(p1['max'], 1)} s (TTL 90 s). {ge20} % warten 20 s oder länger. Zum Vergleich OE3XHL 6,6 s.",
        "<b>Ursache (Vermutung, mit dem Histogramm verträglich):</b> nicht die Tiefe der Warteschlange, sondern die "
        "Ruhezeit nach jedem empfangenen Rahmen (Hold). Auch bei leerer Warteschlange bleiben 16 bis 18 s "
        "(IA/OC-Wert der Verifikation); die Stufe im Histogramm liegt nach dem 20-s-Altersboden.",
        "<b>Folge (Vermutung, nicht beobachtet):</b> ein ACK über mehrere solcher Hops kann länger dauern "
        "als die 40-s-Retry-Leiter des Absenders.",
        f"<b>Prio 3:</b> Median {fmt(lat_s['3']['p50'], 1)} s, p90 {fmt(lat_s['3']['p90'], 1)} s. "
        f"<b>Prio 4:</b> Median {fmt(lat_s['4']['p50'], 1)} s, p90 {fmt(lat_s['4']['p90'], 1)} s. "
        f"<b>Prio 5:</b> Median {fmt(lat_s['5']['p50'], 1)} s, p90 {fmt(lat_s['5']['p90'], 1)} s, p99 "
        f"{fmt(lat_s['5']['p99'], 1)} s, Maximum {fmt(lat_s['5']['max'], 1)} s.",
    ]))
    b.append(fig_ack)
    b.append(ex(e2, "Jeder empfangene Rahmen startet die Wartezeit neu; Baken brauchen die längste Ruhe", 215))
    sp = q2["latency"]["by_src_prio_s"]
    b.append("<h3>Eigene Baken gegen Relays derselben Priorität</h3>")
    b.append("<p><b>Eigene Baken und Relays stehen in derselben Reihe, aber am Kopf der Warteschlange kommt "
             "die eigene Bake kaum zum Senden.</b></p>")
    own_hey_wr, rel_hey_wr = hs["written"], ringh["ring_write_by_src_type"]["rx_relay:40"]
    rel_pos_wr = ringh["ring_write_by_src_type"]["rx_relay:21"]
    rel_pos_sent = fate["by_prio"]["4"]["tx_air"] - ws["air"]
    rel_pos_ttl = fate["by_prio"]["4"]["stale_ttl"] - ws["ttl"]
    rel_pos_sup = q2["supersede"]["by_type"]["21"]
    rel_hey = q2["hey_chains"]["relay_hey"]
    b.append(table(
        ["Klasse", "geschrieben", "gesendet", "TTL", "ersetzt", "Wartezeit gesendet (Median / p90)"],
        [["eigene HEY (Prio 5)", fmt(own_hey_wr), fmt(hs["air"]), fmt(hs["ttl"]), fmt(hs["superseded"]),
          f"{fmt(sp['auto_pos/p5']['p50'], 1)} s / {fmt(sp['auto_pos/p5']['p90'], 1)} s"],
         ["Relay-HEY (Prio 5)", fmt(rel_hey_wr), fmt(rel_hey["fate"]["tx_air"]), fmt(rel_hey["fate"]["stale_ttl"]),
          fmt(rel_hey_wr - rel_hey["fate"]["tx_air"] - rel_hey["fate"]["stale_ttl"]),
          f"{fmt(sp['rx_relay/p5']['p50'], 1)} s / {fmt(sp['rx_relay/p5']['p90'], 1)} s"],
         ["eigene WX (Prio 4)", fmt(ws["total"]), fmt(ws["air"]), fmt(ws["ttl"]), "0",
          f"{fmt(sp['user_wx/p4']['p50'], 1)} s / {fmt(sp['user_wx/p4']['p90'], 1)} s"],
         ["Relay-Positionen (Prio 4)", fmt(rel_pos_wr), fmt(rel_pos_sent), fmt(rel_pos_ttl), fmt(rel_pos_sup),
          f"{fmt(sp['rx_relay/p4']['p50'], 1)} s / {fmt(sp['rx_relay/p4']['p90'], 1)} s"]],
        num_cols=(1, 2, 3, 4)))
    ccls = [("own_HEY", "eigene HEY"), ("own_WX", "eigene WX"), ("relay_HEY", "Relay-HEY"), ("relay_POS", "Relay-Positionen")]
    b.append(table(
        ["Kopf der Warteschlange", "Zeit als Kopf", "Sendungen als Kopf", "Sendungen je Minute", "Ruhezeit (Median)"],
        [[lbl, f"{fmt(hz[k]['head_s'])} s", fmt(hz[k]["tx"]), fmt(hz[k]["tx_per_min"], 2),
          f"{fmt(hz[k]['hold_p50_ms'])} ms"] for k, lbl in ccls], num_cols=(1, 2, 3, 4)))
    b.append(ul([
        "<b>Befund (Messung):</b> als Kopf der Warteschlange sendet eine eigene Bake "
        f"{fmt(min(hz['own_HEY']['tx_per_min'], hz['own_WX']['tx_per_min']), 2)} bis "
        f"{fmt(max(hz['own_HEY']['tx_per_min'], hz['own_WX']['tx_per_min']), 2)} mal pro Minute, ein Relay "
        f"gleicher Priorität {fmt(hz['relay_POS']['tx_per_min'], 1)} bis {fmt(hz['relay_HEY']['tx_per_min'], 1)} mal. "
        "Die abgelaufene Ruhezeit liegt bei eigenen Baken um 5 Sekunden, bei Relays bei 185 ms.",
        "<b>Ursache (Befund aus Log und Code, <code>verify/rca_own_beacons.md</code>):</b> Jeder empfangene Rahmen "
        "startet die Ruhezeit des Kopfes neu (<code>lora_functions.cpp:2746-2747</code>). Ein Relay nimmt unter "
        "<code>--nbrrelay on</code> den Pfad „Fall A“: nach 8 s Wartezeit nur noch 150 ms plus 0 bis 2 mal 35 ms "
        "(<code>txring_functions.cpp:385-405</code>, ausgewählt in <code>lora_functions.cpp:3998</code>). Eigene "
        "Baken haben 5,5 s plus Jitter und keinen Ausweg: der Altersboden schließt Prio 4 und 5 aus "
        "(<code>csma_timing.h:101</code>).",
        "<b>Einordnung:</b> Entwurfslücke, kein Programmierfehler. Der Plan zur Fehlerbehebung "
        "(<code>docs/meshcom-lora-qrt-fix-plan-20261018.md</code>) sieht vor, dass Prio 4 und 5 nie eskalieren; "
        "für Relays gilt das durch Fall A nicht mehr.",
        "<b>Ausgeschlossen:</b> Reihenfolge innerhalb der Priorität (0 Überholungen), Status READY gegen DONE, "
        "Abbruch durch Overhear-Cancel, Sendeabstands-Sperre, Schreibzeitpunkt der Wetterposition.",
        "<b>Einschränkung:</b> Die Wartezeit der Tabelle zählt nur gesendete Einträge; die verworfenen fehlen.",
    ]))
    b.append(ex(e1c, "Gleiche Priorität, andere Ruhezeit am Kopf: eigene Bake gegen Relay", 232))

    # --- F3
    b.append("<h2>F3 Was verlieren wir durch den Zeitverfall (Q3)</h2>")
    b.append("<p><b>Verloren gehen nur Baken und Relay-Baken. Kein Text, kein ACK und keine {CET}-Meldung wurde "
             "durch den Zeitverfall verworfen.</b></p>")
    dt_ = q34["drop_table"]
    b.append(ul([
        f"<b>Umfang:</b> {fmt(q34['drops_total'])} Einträge verworfen (alle Grund TTL, kein {{CET}}-Verfall): "
        f"eigene WX {dt_['p4/own_WX_POS']['n']}, eigene HEY {dt_['p5/own_HEY']['n']}, "
        f"Relay-HEY {dt_['p5/relay_HEY']['n']}, Relay-Positionen {dt_['p4/relay_POS']['n']}.",
        f"<b>Nie gesendet:</b> keiner der {fmt(q34['drops_total'])} lief vor dem Verwerfen über Funk (0 TX_READ, 0 TX im Log).",
        "<b>Zeitverteilung:</b> über den ganzen Tag, kein einzelnes Ereignis; es tritt bei "
        f"{fmt(otx['tx_per_hour'], 1)} Sendungen pro Stunde laufend auf.",
        f"<b>Nachts schlimmer (Befund, Ursache offen):</b> von {night_n} WX, die zwischen 21 und 8 Uhr geschrieben "
        f"wurden, gingen {night_l} verloren; von {day_n} am Tag {day_l}. Die Stundenlast ist dabei flach "
        "(ACK-Weiterleitungen, Relays, Empfänge); die Ursache der Häufung ist ungeklärt.",
    ]))
    b.append("<h3>Eigene Baken: Handlungsbedarf</h3>")
    b.append(table(
        ["Bake", "geschrieben", "gesendet", "verworfen (TTL)", "ersetzt", "gesendet in %"],
        [["WX-Position (Prio 4)", fmt(ws["total"]), fmt(ws["air"]), fmt(ws["ttl"]), "0",
          f"{fmt(100 * ws['air'] / ws['total'], 1)} % von {ws['total']} geschriebenen"],
         ["HEY (Prio 5)", fmt(hs["written"]), fmt(hs["air"]), fmt(hs["ttl"]), fmt(hs["superseded"]),
          f"{hey_air_pct_ep} % von {hs['episodes']} Episoden ({hey_air_pct_wr} % von {hs['written']} geschriebenen)"]],
        num_cols=(1, 2, 3, 4)))
    b.append(ul([
        f"<b>WX:</b> {ws['ttl']} von {ws['total']} verloren ({fmt(own_wx_pct, 1)} %), {ws['air']} gesendet. Längste "
        f"Funkpause {fmt(gap_wx / 3600, 2)} h ({tms(ws['longest_gap_start'])} bis {tms(ws['longest_gap_end'])}), "
        f"{ws['lost_inside_longest_gap']} Wetterpositionen in Folge verworfen ({lost_gap_first} bis {lost_gap_last}). "
        f"Nach der Bake von {tms(ws['longest_gap_end'])[-8:]} gingen die {ws['lost_after_longest_gap']} folgenden "
        f"wieder verloren; zum Logende ({tms(ob['meta']['last'])[-8:]}) dauert die Funkpause bereits "
        f"{fmt(ws['trailing_gap_s'] / 3600, 2)} h und ist dort abgeschnitten (mindestens so lang).",
        f"<b>HEY:</b> {hs['written']} geschrieben, {hs['superseded']} durch eine neuere HEY ersetzt (Absicht). Von "
        f"{hs['episodes']} verbleibenden Episoden liefen {hs['air']} über Funk, {hs['ttl']} verfielen. Lücken zwischen "
        f"gesendeten HEY: Median {fmt(hs['gap_min_p50'], 1)} min, p90 {fmt(hs['gap_min_p90'], 1)} min, Maximum "
        f"{fmt(hs['gap_min_max'], 1)} min (= {fmt(hs['gap_min_max'] / 60, 2)} h, {tms(hs['longest_gap_start'])[-8:]} "
        f"bis {tms(hs['last_air'])[-8:]}); {hs['gaps_over_20min']} Lücken über 20 min. Diese längste Lücke liegt "
        f"am Logende: die letzte Sendung ({tms(hs['last_air'])[-8:]}) liegt nur "
        f"{fmt((datetime.fromisoformat(ob['meta']['last']) - datetime.fromisoformat(hs['last_air'])).total_seconds())} s "
        "vor dem Logende. Die Verifikation liest sie als bis zum Logende offen, also mindestens 3,0 h.",
        f"<b>Welcher Nenner?</b> {hs['air']} von {hs['episodes']} Episoden ({hey_air_pct_ep} %) = geschriebene HEY "
        f"({hs['written']}) minus ersetzte ({hs['superseded']}); {hs['air']} von {hs['written']} geschriebenen "
        f"({hey_air_pct_wr} %) zählt die Ersetzungen als Nichtsendung mit. Beide Zahlen stimmen; wer „gesendet“ "
        "sagt, nennt den Nenner dazu.",
        "<b>Internet:</b> die Wetterposition ging bei jedem der 34 Verluste per UDP ins Internet "
        "(34 von 34 mit TX-UDP-Zeile zur Schreibzeit). Verloren ist die Funkkopie für Stationen, "
        "die nur über LoRa hören.",
        f"<b>Kosten, wenn alle gesendet worden wären:</b> {own_lost} Baken = "
        f"{fmt(dt_['p4/own_WX_POS']['toa_s_total'] + dt_['p5/own_HEY']['toa_s_total'], 1)} s Sendezeit "
        f"(WX {fmt(dt_['p4/own_WX_POS']['toa_s_total'], 1)} s, HEY {fmt(dt_['p5/own_HEY']['toa_s_total'], 1)} s) = "
        f"{fmt(100 * (dt_['p4/own_WX_POS']['toa_s_total'] + dt_['p5/own_HEY']['toa_s_total']) / q34['tx_toa_s'], 1)} % "
        f"der OC-Sendezeit ({fmt(q34['tx_toa_s'])} s).",
        f"<b>Während der {fmt(gap_wx / 3600, 1)}-h-Pause</b> sendete OC {fmt(ob['window_tx_total'])} Einträge: "
        f"{fmt(win_tx['1'])} ACK-Relays, {fmt(win_tx['3'])} Texte und Einspeisungen (Prio 3), {fmt(win_tx['4'])} "
        f"Positionen, {fmt(win_tx['5'])} HEY. Kein später geschriebener Prio-4-Eintrag überholte die "
        f"verworfenen WX ({ob['fifo']['violations']} von {ob['fifo']['checked']} Fenstern).",
        "<b>Beispiel (Verifikation):</b> WX 81125FC5 stand vom 31.10. 14:39:14 bis 14:39:45 am Kopf einer fast "
        "leeren Warteschlange, ohne dass gesendet wurde; alle 1 bis 3 s kam ein Rahmen herein (darunter fünf Echos "
        "eines gerade gesendeten ACK) und startete die Ruhezeit neu.",
    ]))
    b.append(fig_beacon)
    b.append("<h3>Warum sie verhungern: drei Teile</h3>")
    b.append(ul([
        f"<b>1 Strikte Auswahl:</b> gesendet wird immer der Eintrag mit der höchsten Priorität "
        f"(<code>txring_functions.cpp:213-251</code>). Bei verworfenen Baken stand in {fmt(ha['own_HEY_ttl']['blocked_pct'])} % "
        "der Lebenszeit (eigene HEY, letzte Verknüpfung) ein höher priorisierter oder älterer Eintrag davor. "
        "(Cluster, Verifikation: 64 bis 72 %.)",
        f"<b>2 Ruhezeit pro Empfang neu:</b> in der übrigen Zeit, in der die Bake am Kopf stand "
        f"({fmt(ha['own_HEY_ttl']['head_s_total'])} s bei den {ha['own_HEY_ttl']['n']} verworfenen eigenen HEY), "
        f"gab es nur {ha['own_HEY_ttl']['gate_entries_in_head']} Kanalprüfungen, alle belegt "
        f"({fmt(ha['own_HEY_ttl']['head_s_total'] / ha['own_HEY_ttl']['gate_entries_in_head'])} s je Versuch). "
        f"{ha['own_HEY_ttl']['n_head_ge_30s_zero_gate']} von {ha['own_HEY_ttl']['n_head_ge_30s']} Baken mit "
        "mindestens 30 s am Kopf machten gar keinen Versuch.",
        "<b>3 Kein Altersboden für Prio 4/5:</b> der Boden verkürzt die Ruhezeit alter Einträge nur für Prio 1 bis 3 "
        "(<code>csma_timing.h:99-111</code>).",
        f"<b>Kein Sendezeit-Limit:</b> Dauer der Sendungen nur {fmt(q34['tx_duty_pct'], 1)} % der Zeit. Das ist ein "
        "Limit des Kanalzugangs, keines der Sendezeit (Schluss aus den Zahlen).",
        "<b>Dazu:</b> etwa 70 % der Lebenszeit einer verworfenen Bake liegt hinter ACK-Weiterleitungen der Prio 1 "
        f"({fmt(ringh['ring_write_by_src_type']['rx_ack_fwd:41'])} ACK-Einträge in {fmt(span_h, 1)} h).",
    ]))
    b.append(ex(e1, "Drei Teile: warum eine Bake am Kopf der Warteschlange nicht sendet", 232))
    b.append("<p><b>Mechanismus in einfachen Worten:</b> Vor jedem Senden wartet der Knoten nach dem letzten "
             "empfangenen Rahmen eine Ruhezeit (ACK 3,0 s, Relay 4,5 s, Positionen und HEY 5,5 s). Gewählt wird "
             f"immer der Eintrag mit der höchsten Priorität; ein Altern gibt es nicht. OC hört im Median alle "
             f"{fmt(rxa['gap_s_median'], 2)} s einen Rahmen, und die 5,5 s Ruhe sind darum selten. Relays gleicher "
             "Priorität dürfen nach 8 s mit 150 bis 220 ms Ruhe senden, eigene Baken nicht. Die Frist (WX 120 s, "
             "HEY 180 s) wirft die Bake dann weg.</p>")
    b.append(f"<h3>Relay-Verluste durch Zeitverfall ({rc_all}): meist anderweitig übertragen</h3>")
    b.append(table(["Klasse", "Beleg", "Einträge"],
                   [["anderweitig übertragen, Cluster hat gesendet",
                     "ein anderer der fünf Knoten sendete die ID nach unserem Schreiben",
                     str(relay_class["redundant_cluster_tx"])],
                    ["anderweitig übertragen, niedrigerer Hop", "wir hörten eine Kopie mit niedrigerem Hop",
                     str(relay_class["redundant_heard_lower_hop"])],
                    ["schwach", "Kopie vor unserem Schreiben gesendet oder gleicher Hop, kein Folgebeleg",
                     str(rc_weak)],
                    ["ohne jeden Beleg", "niemand trug die ID weiter", str(rc_none)],
                    ["Summe", "", str(rc_all)]], num_cols=(2,)))
    b.append(ul([
        f"<b>Anderweitig übertragen:</b> {rc_covered} von {rc_all} ({fmt(100 * rc_covered / rc_all, 0)} %). "
        "Das heißt nur: ein anderer Knoten hat die ID gesendet, nicht, dass das Versorgungsgebiet dieses Knotens sie "
        f"bekam. <b>Mögliche Verluste:</b> {rc_weak + rc_none}, davon {rc_none} ohne Beleg; alle sind "
        f"erneuerbare Typen (HEY, Position). {q34['relay_refreshed_within_30min']} von {rc_all} bekamen binnen 30 min "
        f"eine neuere Nachricht desselben Absenders (Median {fmt(q34['relay_refresh_median_s'])} s).",
        "<b>Beispiel (möglicher Verlust):</b> 31.10. 11:35:09 HEY 1AFB2190 von OE3KJO-64 (direkt gehört, RSSI "
        "-108 dBm): von 11:32:09 an wartend, in den drei Minuten sendete OC 11 Rahmen (8 ACKs, 2 Relay-Positionen, "
        "1 Text), die HEY nie; sie ging per UDP an den Server (RF-Verlust). Die nächste HEY des Absenders folgte "
        "um 11:43:46.",
        "<b>Einordnung:</b> OE3KJO-64: " + esc(callsign_info("OE3KJO-64")) + ".",
    ]))
    b.append("<h3>CSMA-Altersboden (Unterabschnitt)</h3>")
    b.append("<p><b>Der Altersboden wirkt neutral bis positiv, bevorzugt aber nur Prio 1 bis 3. Für die "
             "eigentlichen Opfer (Prio 4/5) tut er nichts.</b></p>")
    b.append(ul([
        "<b>Was:</b> Ist der erste Eintrag älter als 20 s (Stufe 1) oder 60 s (Stufe 2), verkürzt der Knoten "
        "die Ruhezeit (CSMA_AGED). Gezählt wird bei jedem Neustart der Ruhezeit nach einem Empfang.",
        f"<b>Umfang:</b> {fmt(csma['events'])} Ereignisse bei OC (Cluster aller fünf Knoten: {fmt(csma_cluster)}) in "
        f"{fmt(csma['episodes'])} Warte-Episoden; {fmt(cs_ep['1'])} sind ACKs (Prio 1), {cs_ep['3']} Prio 3, Stufe 2 "
        f"nur {csma['stage_events']['2']} mal. {fmt(csma['tx_prio_le3_wait_ge20s_pct'], 1)} % der Prio-1-bis-3-Sendungen "
        "warteten 20 s oder länger.",
        f"<b>Wirkung (Modell, nicht gemessen):</b> nach der ersten Verkürzung wird im Median "
        f"{fmt(csma['replay']['real_remaining_after_first_aged_event_s']['p50'], 1)} s später gesendet (p90 "
        f"{fmt(csma['replay']['real_remaining_after_first_aged_event_s']['p90'], 1)} s, gemessen); eingespart je "
        "Episode 0,6 bis 1,6 s (Modell, der Versuchszähler wird nicht geloggt).",
        f"<b>Kollisionen:</b> RX-Fehler binnen 3 s nach eigener Sendung {fmt(csma['probe_p1_aged']['pct_rxerr_within_3s_after_tx'], 1)} % "
        f"(verkürzt) gegen {fmt(csma['probe_p1_fresh']['pct_rxerr_within_3s_after_tx'], 1)} % (nicht verkürzt): "
        "keine Kollisionsspur im eigenen RX-Fehler (schwacher Beleg: eine Kollision an einem dritten Knoten "
        f"bliebe unsichtbar). Doppelte Sendung derselben ACK-ID binnen 120 s: "
        f"{fmt(csma['probe_p1_aged']['pct_dup_tx_within_120s'], 2)} % gegen "
        f"{fmt(csma['probe_p1_fresh']['pct_dup_tx_within_120s'], 2)} %.",
        f"<b>Zahlenabweichung:</b> {fmt(csma['events'])} Ereignisse (unabhängig nachgezählt) gegen "
        f"{fmt(fb['csmaaged'])} im Gesundheitsbericht (anderer Parser, Ursache nicht untersucht). Diese Seite nutzt "
        f"{fmt(csma['events'])}.",
    ]))
    b.append(fig_csma)

    # --- F4
    b.append("<h2>F4 Was gewinnen wir durch den Zeitverfall (Q4)</h2>")
    b.append(f"<p><b>Der Gewinn ist Frische, keine Sendezeit: nur {fmt(q34['drop_toa_pct_of_tx'], 2)} % der "
             "OC-Sendezeit würden gespart.</b></p>")
    hd = q34["headroom"]
    b.append(ul([
        f"<b>Sendezeit (Messung):</b> die {fmt(q34['drops_total'])} verworfenen Einträge hätten "
        f"{fmt(q34['drop_toa_s'], 1)} s gekostet ({fmt(q34['drop_toa_s'] / span_h, 1)} s pro Stunde) von "
        f"{fmt(q34['tx_toa_s'])} s Sendezeit = {fmt(q34['drop_toa_pct_of_tx'], 2)} %.",
        f"<b>Plätze:</b> {fmt(cf['stale_slot_seconds_real'])} Platz-Sekunden frei (im Mittel "
        f"{fmt(cf['stale_slot_seconds_real'] / (span_h * 3600), 2)} von {ringh['MAX_RING']} Plätzen).",
        f"<b>Ohne TTL (Modell, erste Näherung, Obergrenze; keine Messung):</b> {cf['stale_fate_no_ttl']['served']} der "
        f"{cf['baseline_ttl_drops']} Alten würden gesendet, mit Funkalter Median {stale_age['p50']} s, "
        f"p90 {stale_age['p90']} s, Maximum {stale_age['max']} s ({stale_age['n_gt_300']} über 300 s). "
        f"{fmt(fresh['pct_delayed'], 1)} % der frischen Prio-3-bis-5-Einträge "
        f"({fmt(fresh['n_delayed_gt_0_5s'])} von {fmt(fresh['n_compared'])}) kämen später, Median "
        f"+{fmt(fresh['delay_s_p50'])} s, p90 +{fmt(fresh['delay_s_p90'])} s, Maximum +{fmt(fresh['delay_s_max'])} s. "
        f"Das Modell stimmt in {fmt(cf['baseline_pick_match_pct'], 1)} % der Auswahlschritte mit der Messung überein; "
        "bei 219 von 224 Verwerfungen an vier Knoten (Verifikation) wartete noch ein anderer Eintrag, die "
        "„keine zusätzliche Leerlaufzeit“-Näherung ist also klein.",
        f"<b>Tiefe (Modell):</b> zeitgewichtet {fmt(cf['depth_ttl_on']['mean_at_events'], 2)} mit TTL gegen "
        f"{fmt(cf['depth_no_ttl']['mean_at_events'], 2)} ohne, Maximum {cf['depth_ttl_on']['max']} gegen "
        f"{cf['depth_no_ttl']['max']} von {ringh['MAX_RING']}: auch ohne TTL kein Überlauf.",
        f"<b>Spielraum der TTL (Messung):</b> ACK ({hd['p1']['ttl_s']} s) höchstens {fmt(hd['p1']['max'], 1)} s genutzt, "
        f"Relay ({hd['p3']['ttl_s']} s) höchstens {fmt(hd['p3']['max'], 1)} s. Prio 4: {hd['p4']['n_ge80pct_ttl_sent']} "
        f"gesendete über 80 % der TTL, Prio 5: {hd['p5']['n_ge80pct_ttl_sent']}.",
    ]))
    b.append(fig_sens)
    b.append(ul([
        "<b>Lesart (Modell):</b> Die Zahl ist die Menge verworfener Einträge, wenn die Frist anders wäre. "
        "Nur der heutige Wert ist gemessen; kürzere Fristen sind eine Obergrenze der ersten Näherung "
        "(eine kürzere Frist gibt dem nächsten Eintrag früher den Sendeplatz), längere sind reines Modell: "
        "kein gesendeter Prio-4/5-Eintrag wartete länger als seine TTL. Prio 5 bei 300 s: "
        f"{s5['300']['n_over']} statt {s5['180']['n_over']}; Prio 4 bei 240 s: {s4['240']['n_over']} statt "
        f"{s4['120']['n_over']}. Richtung sicher, Größe Modell.",
        "<b>Preis (Vermutung aus dem Modell):</b> Rahmen würden bis zu 4 bis 5 Minuten alt gesendet.",
        f"<b>Folgerung:</b> Eigene Baken machen bei OC {s5['180']['own']} von {s5['180']['n_over']} (Prio 5) und "
        f"{s4['120']['own']} von {s4['120']['n_over']} (Prio 4) der Verwerfungen aus. Eine eigene Frist je Klasse "
        "allein hilft aber nicht: die Bake wartet dann länger am Kopf, ohne früher zu senden. Es braucht eine "
        "Änderung am Kanalzugang (ungetestet).",
    ]))
    b.append(ex(e1b, "Kandidat Altersboden: ungetestet, Wirkung nicht gemessen", 190))

    # --- F5
    b.append("<h2>F5 Zeitsignal (Q5)</h2>")
    b.append("<p><b>Das Zeitsignal kommt vollständig an und OC sendet nie veraltete Zeit. Beobachten: die "
             "Mehrfachabstrahlung und veraltete Relays von außen.</b></p>")
    q5age = q5["tx_age_s_logger_based"]
    b.append(ul([
        f"<b>Ankunft:</b> {q5['udp_got']} von {q5['expected_in_window']} {{CET}}-Telegrammen von OE1XAR-33 (Abstand "
        f"Median {fmt(q5['udp_arrival_gap_s']['p50'])} s) per UDP, 0 ausgefallen. Der Inhalt ist UTC, die "
        f"Bezeichnung „CET“ falsch. Der Server ist kein Knoten. Der größte Abstand ist "
        f"{fmt(q5['udp_arrival_gap_s']['max'])} s; ein Server-Neustart als Ursache ist eine Vermutung.",
        f"<b>Funkkopien:</b> OC hört je Telegramm im Mittel {fmt(q5['rf_copies_per_id_mean'], 1)} Kopien (Median "
        f"{fmt(q5['rf_copies_per_id']['p50'])}, Maximum {fmt(q5['rf_copies_per_id']['max'])}). "
        f"{fmt(q5['rf_age_gt150_pct'], 1)} % der Funkkopien ({q5['rf_age_gt150_n']} von "
        f"{q5['rf_valid_frames']}) sind älter als 150 s.",
        f"<b>Eigene Sendungen:</b> {q5['tx_count']}, Alter beim Senden Median {fmt(q5age['p50'])} s, p90 "
        f"{fmt(q5age['p90'])} s, Maximum {fmt(q5age['max'])} s; {q5['tx_age_gt150_n']} über 150 s. "
        f"Warteschlange Median {fmt(q5['tx_wait_ms']['p50'] / 1000, 1)} s, Maximum "
        f"{fmt(q5['tx_wait_ms']['max'] / 1000, 1)} s.",
        f"<b>Doppelsendung:</b> {q5['tx_after_overheard_any_rf_copy_n']} von {q5['tx_count']} "
        f"({fmt(q5['tx_after_overheard_pct'], 1)} %) folgten einer bereits gehörten Funkkopie "
        f"({q5['tx_after_overheard_gateway_radiation_n']} davon einer Abstrahlung eines Gateways); "
        "0 Abbrüche ([NBR]|NEED immer Fall A).",
        f"<b>Kosten:</b> {{CET}} macht {fmt(q5['cet_share_of_stat_rx_pct'], 1)} % der empfangenen und "
        f"{fmt(q5['cet_share_of_stat_tx_pct'], 1)} % der gesendeten Sendezeit aus, "
        f"{fmt(q5['cet_share_of_tx_count_pct'], 1)} % der Sendeereignisse.",
        f"<b>Tor:</b> {q5['ring_drop_stale_cet']} mal {{CET}}-Verfall im Puffer, 2 mal Relay verworfen (Alter 2011 s und 2508 s).",
    ]))
    b.append("<h3>Beobachten: veraltete Zeit von fremden Relays</h3>")
    st = q5["rf_stale_lasthop_top"]
    b.append(ul([
        "<b>Befund:</b> Die Stationen, die fast nur veraltete Kopien liefern, erscheinen bei OC als letzter Hop mit "
        + ", ".join(f"{c_} {n_}" for c_, n_ in st[:4]) + " Kopien über 150 s. Über den ganzen Lauf und alle "
        "fünf Knoten (Gruppenbefund): OE3MIF-12 62 von 64, OE3NRA-1 47 von 47, OE3MTC-10 46 von 46 (Median "
        "817 s), OE3XDC-12 44 von 49 (Median 261 s) Kopien älter als 150 s; zusammen 79,6 % der veralteten "
        "Abstrahlungen.",
        "<b>Einordnung:</b> OE3NRA-1: " + esc(callsign_info("OE3NRA-1")) + ". OE3MTC-10: "
        + esc(callsign_info("OE3MTC-10")) + ". OE3XDC-12: " + esc(callsign_info("OE3XDC-12"))
        + ". OE3MIF-12: " + esc(callsign_info("OE3MIF-12")) + ".",
        "<b>Vermutung:</b> ältere Firmware ohne {CET}-Tor; Eingriff beim Betreiber der vier Stationen. Das Tor "
        "gilt seit 05a18b36 (v4.40a.10.23, upstream über #1238) auch für den Gateway-Einspeisepfad.",
        "<b>Nicht bei OC gesehen:</b> OE3GJC-12 (RAK4631, Gateway Traiskirchen) strahlt laut Cluster-Befund "
        "ebenfalls veraltete {CET}-Zeit ab (Vermutung: Build ohne Tor oder eine zu langsame vertrauenswürdige "
        "Uhr). OC selbst hat von ihr keine {CET}-Kopie im Log.",
    ]))
    b.append(ex(e5, "Fremde Relays halten die Zeitansage minutenlang, das OC-Tor stoppt sie ab 150 s", 215))
    b.append("<h3>Beobachten: Server-Einspeisung durch alle Gateways</h3>")
    b.append(ul([
        f"<b>Befund:</b> {fb['gwi']} Server-Einspeisungen bei OC, alle gesendet, keine mit Abbruch. Im Cluster: "
        "231 von 362 Rahmen strahlen 4 bis 5 der 5 Gateways ab, 735 s Sendezeit doppelt (5,4 %).",
        "<b>Urteil:</b> Beobachten. Fünf Gateways koordinieren sich nicht, weil der Abbruch in Fall A nie "
        "greift.",
    ]))
    b.append(ex(e6, "Alle Gateways strahlen dieselbe Server-Einspeisung ab", 215))

    # --- F6
    b.append("<h2>F6 Manipulationen dieses Knotens</h2>")
    ack_n = co["A"]["sent_with_foreign_copy_heard_while_queued"]
    b.append('<div class="grid3">'
             '<div class="card pos"><h3>Positiv</h3>' + ul([
                 f"<b>Dedup:</b> {fmt(fb['rx_dup_foreign'])} fremde Duplikate und {fmt(fb['rx_dup_echo'])} Echos eigener "
                 f"Sendungen unterdrückt bei {fmt(fb['rx_new'])} neuen Rahmen: weniger Last.",
                 f"<b>Relay-Flut:</b> {fmt(fb['rly_tx'])} Relays, {fmt(rel_fate['sent'])} gesendet "
                 f"({fmt(100 * rel_fate['sent'] / fb['rly_tx'], 1)} %), {rel_fate['supersede']} ersetzt, "
                 f"{rel_fate['stale']} durch TTL.",
                 f"<b>Begrenzung:</b> hop0 {fb['rly_hop0']}, gwcap {fb['rly_gwcap']} (Pfad schon so lang wie das "
                 f"Hop-Limit), gwfilter {fmt(fb['rly_gwfilter'])} ({{ping}}, {{pong}} und andere nicht weitergeleitet).",
                 f"<b>Ersetzen:</b> {fb['supersede']} neuere Baken ersetzen unversendete alte "
                 f"({q2['supersede']['by_type']['40']} HEY, {q2['supersede']['by_type']['21']} Positionen).",
                 f"<b>{{CET}}-Tor:</b> {fb['rly_cet']} veraltete Relays gestoppt.",
                 "<b>Keine Schleifen:</b> keine Hop-Zunahme, keine Ping-Pong-Sendung.",
             ]) + "</div>"
             '<div class="card neg"><h3>Negativ</h3>' + ul([
                 f"<b>Baken verhungern:</b> {own_lost} verworfen, davon {ws['ttl']} von {ws['total']} WX.",
                 f"<b>ACK-Relays ohne Abbruch:</b> {fmt(ack_n)} von {fmt(fb['tx_r_A'])} "
                 f"({fmt(100 * ack_n / fb['tx_r_A'], 1)} %) nach gehörter Kopie gesendet; Relays und ACKs zusammen "
                 f"{fmt(sum(op['opportunity_tx_by_type'].values()))} Sendungen, {fmt(op['opportunity_air_s_total'])} s = "
                 f"{fmt(op['opportunity_share_of_node_tx_air_pct'], 1)} % der OC-Sendezeit (Obergrenze).",
                 f"<b>Alleinzulieferer:</b> {fmt(alone_n)} von {fmt(alone_n)} Entscheidungen Fall A, 0 Abbrüche; "
                 f"{fmt(fb['refuse'])} REFUSE.",
                 f"<b>Dedup-Fenster kurz:</b> {loops['foreign_reaccepted']} fremde IDs doppelt als neu angenommen, "
                 f"1 Relay derselben ID nach {fmt(loops['relay_tx_twice_gap_s_min'][-1])} s.",
                 f"<b>ACK-Wartezeit:</b> Median {fmt(p1['p50'], 1)} s je Hop.",
                 f"<b>Mehrfachabstrahlung</b> der Server-Telegramme ({fmt(q5['tx_after_overheard_pct'], 1)} %).",
             ]) + "</div>"
             '<div class="card neu"><h3>Neutral</h3>' + ul([
                 f"<b>Upload zum Server:</b> {fmt(fb['gwu'])} Uploads (GWU), kostet keine Sendezeit.",
                 "<b>Doppelte ACK-Sendung derselben ID binnen 120 s:</b> 4 Paare, jeweils mit zweiter Wartezeit "
                 "ab 20 s (Verifikation).",
                 f"<b>CSMA_AGED:</b> {fmt(csma['events'])} Verkürzungen, neutral bis positiv.",
                 f"<b>Echos eigener Baken:</b> {fb['echo']} (Diagnose, kein Eingriff).",
             ]) + "</div></div>")
    b.append("<h3>Beobachten: Ping-Last von OE3XWJ-12</h3>")
    b.append(ul([
        f"<b>Befund:</b> OE3XWJ-12 sendet einmal pro Minute {{ping}} an Gruppe 2025 (1.529 mal im Cluster). "
        f"Bei OC gehören {fmt(ackpfx['events'])} von {fmt(tr['ack_events'])} ACK-Rahmen ({fmt(ackpfx['share_pct'], 1)} %) "
        f"dazu; {fmt(ack_relays)} ACK-Relays = {fmt(ack_relay_pct, 1)} % der OC-Sendungen ({fmt(ack_relays)} von {fmt(n_tx)}). "
        "Im Cluster: ACK-Relays 41,3 % der Sendungen und 20,2 % der Sendezeit; mit den 1.529 eigenen Pings "
        "48,5 % und 25,9 %.",
        "<b>Einordnung:</b> OE3XWJ-12: " + esc(callsign_info("OE3XWJ-12")) + ".",
        "<b>Woher die ACKs kommen:</b> keiner der fünf Knoten erzeugt sie (0 ACK-Sendungen mit eigener Quelle). "
        "Es sind Gateway-ACKs fremder Gateways, im Mittel etwa 4,6 verschiedene ACK-IDs je Ping. Am HEAD "
        "quittiert ein Gateway {ping} nie (<code>lora_functions.cpp:2169-2180</code>, auch upstream); die "
        "quittierenden Gateways laufen also vermutlich mit einem anderen Build (Vermutung).",
        "<b>Unterschied ESP32 / RAK4631, Entscheidung offen:</b> Die ESP32-Schleife sendet den automatischen Ping nur, "
        "solange ein Zähler größer 0 ist, und zählt ihn herunter (<code>esp32_main.cpp:5264</code>, "
        "<code>:5285</code>). Die nRF52-Schleife der RAK4631 kennt keinen Zähler und pingt alle "
        "<code>node_pingtime</code> Sekunden, solange ein Ping-Ziel gesetzt ist "
        "(<code>nrf52_main.cpp:2717-2737</code>). <code>--ping stop</code> setzt den Zähler auf 0 "
        "(<code>command_functions.cpp:3886</code>) und wirkt auf einer RAK deshalb nicht; nur das Löschen von "
        "<code>--pingcall</code> hilft. Upstream <code>dev</code> 147812e0 hat denselben Code. Welches Verhalten "
        "gewollt ist, entscheidet der Betreiber.",
        "<b>Urteil:</b> Betreiberfrage (siehe unten), Cluster-Befund.",
    ]))
    b.append(ex(e3, "Ein Ping pro Minute erzeugt den größten Teil der ACK-Last", 215))
    b.append(ex(e3b, "Ping-Zähler: ESP32 hört nach der Höchstzahl auf, RAK4631 nicht (Entscheidung offen)", 190))
    b.append("<h3>Beobachten: kurzes Dedup-Fenster</h3>")
    b.append(ul([
        f"<b>Befund:</b> der Ring merkt sich {dedup['ring_size']} IDs, bei {fmt(dedup_rate, 2)} neuen IDs pro Minute "
        f"({fmt(dedup['add'])} in {fmt(span_h, 2)} h) reicht er rund {fmt(dedup_min, 1)} Minuten. Danach wird eine "
        "Kopie wieder als neu angenommen.",
        f"<b>Folge:</b> {loops['foreign_reaccepted']} fremde IDs doppelt als neu angenommen (Median-Abstand "
        f"{fmt(loops['foreign_reaccept_gap_s_median'])} s, {loops['foreign_reaccept_gap_gt_10min']} über 10 min, "
        f"{loops['foreign_reaccept_gap_gt_30min']} über 30 min).",
    ]))
    b.append(ex(e8, "Zählt der Ring nur 100 Einträge, vergisst er alte IDs nach rund 18 Minuten", 205))

    # --- Verdachtsfälle
    b.append("<h2>Verdachtsfälle Firmware</h2>")
    b.append("<p>Nur echte Kandidaten. Keiner ist mit einer Codeänderung geprüft.</p>")
    b.append("<h3>V1: Eigene Baken verhungern (Entwurfslücke)</h3>")
    b.append(ul([
        f"<b>Beobachtung:</b> {own_lost} eigene Baken verworfen, {fmt(gap_wx / 3600, 2)} h ohne WX auf Funk.",
        f"<b>Beleg:</b> Abschnitt F3 und F2 (eigene gegen Relay), Drops {q34['drops_total']}/{q34['drops_total']} mit "
        "Grund TTL, 0 vorher gesendet; Wurzelursache in <code>verify/rca_own_beacons.md</code>.",
        "<b>Code-Stelle:</b> <code>src/lora_functions.cpp:2746-2747</code> (Ruhezeit pro Empfang neu), "
        "<code>:3998-4006</code> und <code>src/txring_functions.cpp:385-405</code> (Fall A nur für Relays), "
        "<code>src/csma_timing.h:99-111</code> (Altersboden nur Prio 1 bis 3), "
        "<code>src/txring_functions.cpp:213-251</code> (strikte Priorität), "
        "<code>:566-645</code> (TTL-Sweep), <code>src/txring_ttl.h:46-64</code>.",
        "<b>Status:</b> bestätigt als Verhalten, Einordnung Entwurfslücke (Plan nimmt an, dass Prio 4/5 nie "
        "eskalieren); Korrektur ungetestet. Kleinster Kandidat: Altersboden auch für Prio 4/5 an "
        "Nicht-Relay-Slots (<code>lora_functions.cpp:4030-4043</code>). Offen: Häufung der Verluste in der Nacht.",
    ]))
    b.append("<h3>V2: ACK-Relays ohne Overhear-Cancel</h3>")
    b.append(ul([
        f"<b>Beobachtung:</b> {fmt(ack_n)} von {fmt(fb['tx_r_A'])} ACK-Relays wurden gesendet, nachdem eine Kopie "
        "während der Wartezeit gehört wurde.",
        "<b>Beleg:</b> q6 <code>cancel_opportunity</code>, Obergrenze (die Kopie erreicht evtl. nicht dieselben "
        "Hörer).",
        "<b>Code-Stelle:</b> <code>src/lora_functions.cpp:661</code> (ACK-Weiterleitung als Art OTHER, "
        "kein NEED), <code>:1327</code>, <code>:1359</code> (Abbruchsuche nur für Relays in Relay-Slots).",
        "<b>Status:</b> bestätigt (Code und Log); ob ein Abbruch erwünscht ist, ist Designfrage.",
    ]))
    b.append(ex(e4, "ACK-Relays laufen auch nach einer gehörten Kopie weiter", 215))
    b.append("<h3>V3: Alleinzulieferer-Menge nie leer (immer Fall A)</h3>")
    b.append(ul([
        f"<b>Beobachtung:</b> {fmt(alone_n)} von {fmt(alone_n)} NEED-Zeilen sind Fall A, 0 Abbrüche. OE3MIF-12, "
        f"OE3MTC-10 und OE3NRA-1 stehen in {fmt(top_alone['OE3MIF-12'])}, {fmt(top_alone['OE3MTC-10'])} und "
        f"{fmt(top_alone['OE3NRA-1'])} davon im Alleinzulieferer-Satz "
        f"({fmt(100 * top_alone['OE3NRA-1'] / alone_n, 1)} bis {fmt(100 * top_alone['OE3MIF-12'] / alone_n, 1)} %).",
        "<b>Beleg:</b> q6 <code>alone_hist</code>, <code>persistent_alone</code>; andere OE3-Knoten hören "
        "die drei direkt (IA/IR/WJ).",
        "<b>Nicht die Erklärung:</b> das 12-h-Fenster (<code>nbr_matrix.h:125</code>). Alle fünf Knoten starteten "
        "mit dem Log, Fall A gilt schon in den ersten 60 Betriebsminuten zu 100 %, und aus Symmetrie "
        "abgeleitete Kanten können die Rückrichtung liefern (<code>nbr_matrix.cpp:1829-1848</code>). "
        "Die Frage lautet: warum ist die Alleinzulieferer-Menge nie leer.",
        "<b>Code-Stelle:</b> <code>src/nbr_matrix.cpp:1777</code> (Abhängigen-Regel) und "
        "<code>:1816-1852</code> (Test auf alternativen Zulieferer).",
        "<b>Status:</b> Verdacht, Beobachten/Design. Ob der Abbruch-Modus (nbrcancel) auf OC an oder aus ist, "
        "folgt nicht sicher aus dem Log; die Relay-Ruhezeiten von 150 bis 220 ms sprechen für „an“ (Vermutung).",
    ]))
    b.append(ex(e4b, "Wegen der Alleinzulieferer-Regel hält OC sich für alleinzuständig und bricht nie ab", 215))

    # --- Nachbarn
    b.append("<h2>Nachbarn und Hardware-Inventar</h2>")
    b.append("<p>Direkte Nachbarn (letzter Hop) mit den meisten Empfängen. RSSI und SNR gelten nur für diesen "
             "letzten Hop und sind Mediane. Stammdaten aus mcmap; Firmware laut Stammdaten, sonst aus dem Log.</p>")
    b.append(nb_main)
    b.append(f"<details><summary>Alle {len(nb_rows)} Nachbarn mit Angaben</summary>" + nb_all + "</details>")
    b.append("<p>OE3XIR-12: keine Stammdaten (laut Auswertung 02.09.2026 Hegerberg). Die drei Stationen "
             "OE3MIF-12, OE3NRA-1 und OE3MTC-10 hat OC sehr deutlich direkt gehört (-95 bis -97 dBm).</p>")

    # --- Grenzen
    b.append("<h2>Was nicht geprüft ist / Grenzen</h2>")
    b.append("<p><b>Kann das Log nicht sagen</b></p>")
    b.append(ul([
        "Der genaue Firmware-Build ist unbekannt; Code-Aussagen gelten für den Stand HEAD 651e0920.",
        "Ob die Wetterposition im Internet tatsächlich erschien, ist nicht geprüft (nur die UDP-Zeile am OC).",
        "Ob der Abbruch-Modus (nbrcancel) an ist, ist nicht beweisbar (nur aus den Relay-Ruhezeiten geschlossen).",
        "Warum die Verluste eigener WX nachts häufiger sind, obwohl die Stundenlast flach ist.",
        "Ob ein RX-Fehler (Kopf ohne gültigen Rahmen) die Ruhezeit ebenfalls neu startet; wenn ja, hungern die "
        "Baken noch stärker.",
    ]))
    b.append("<p><b>Modellgrenzen</b></p>")
    b.append(ul([
        "Die Rechnung „ohne TTL“ und alle TTL-Werte über dem heutigen sind Modell (erste Näherung, Obergrenzen); "
        "die Werte kürzerer Fristen sind nicht exakt, weil eine kürzere Frist den Sendeplatz früher freigibt.",
        "Das Verlust-Urteil sieht nur fünf Knoten und was sie hören. „Kein Beleg“ ist kein Beweis, dass "
        "niemand es trug.",
        "Die Zeitpunkte der Baken-Zeitleiste stammen aus eigener Zählung (siehe Methodik); die Kopf-Rekonstruktion "
        "ignoriert die 20-s-Haltezeit von Fall-B-Slots.",
        "CSMA_AGED und RING_DROP_STALE nennen weder Versuchszähler noch Typ; die Wirkung ist deshalb nur "
        "aus Modell und Gegenprobe ableitbar (Logging-Lücke, kein Fehler).",
    ]))
    b.append("<p><b>Ausgeschlossen</b></p>")
    b.append(ul([
        "ESP32-Auslastungsinflation (OC ist nRF52).",
        "Retry-Leiter: 0 Neuversuche, deshalb für diese Daten ungetestet.",
    ]))

    # --- Offene Fragen
    b.append("<h2>Offene Fragen an den Betreiber</h2>")
    b.append(ul([
        "<b>Baken:</b> Sind WX und HEY auf Funk wichtig genug für Kandidat A oder B, oder reicht das "
        "Internet (Option C)? Beide Kandidaten sind ungetestet; ein Test auf einem ausgelasteten Knoten wäre "
        "der nächste Schritt.",
        f"<b>Ping:</b> Ist der minütliche {{ping}} von OE3XWJ-12 an Gruppe 2025 gewollt? Er bestimmt "
        f"{fmt(ack_relay_pct, 1)} % der OC-Sendungen. Soll der RAK4631 wie der ESP32 nach einer Höchstzahl "
        "aufhören (Unterschied ESP32 / RAK4631, Entscheidung offen)?",
        "<b>Fremde Relays:</b> Welche Firmware laufen auf OE3MIF-12, OE3NRA-1, OE3MTC-10 und OE3XDC-12? Und "
        "welche Gateways quittieren die {ping}-Rahmen?",
        "<b>Abbruch-Modus:</b> Ist nbrcancel auf OC an? (Aus dem Log nicht sicher ablesbar.)",
    ]))

    # --- Dev
    b.append('<details class="dev"><summary>Methodik, Marker, Code-Stellen, Skripte</summary>')
    b.append("<h3>Quellen und Zählweisen</h3>")
    b.append(ul([
        "Zeitbasis: Loggeruhr (Wandzeit am Zeilenanfang). Knotenuhr -1 s.",
        "Empfangszeilen <code>[LOG]</code> werden vor Dedup und Eigen-TX-Filter gedruckt; Dekoderabbrüche "
        "mit <code>FCS:0000</code> sind ausgenommen. ACK-Rahmen (12 Byte) drucken keine OnRxDone-Zeile "
        "(<code>lora_functions.cpp:1018-1034</code>); jede Empfangsstatistik zählt sie über "
        "<code>ONRXDONE_TIME</code> mit.",
        "Q0: <code>health/results.json</code> (nodes.oe3xoc). Q2: <code>q2/results.json</code> "
        "(fate, latency, hey_chains, occupancy, supersede). Q3/Q4: <code>q34/results.json</code> (drop_table, "
        "relay_class, cf, ttl_sens, csma, headroom). Q5: <code>q5/results.json</code>. Q6: "
        "<code>q6/results.json</code> (totals, cancel_opportunity, opportunity_airtime, alone_hist, loops). "
        "Stammdaten: <code>master/master.tsv</code>, <code>master/distances.tsv</code>.",
        "Eigene Zählung aus dem Log: <code>extra/oe3xoc_ownbeacons.json</code>, erzeugt von "
        "<code>generators/lib/oe3xoc_ownbeacons.py --norm &lt;norm-Verzeichnis&gt; --out &lt;json&gt;</code>. "
        "Enthalten: Wetterpositionen mit Schicksal, HEY-Episoden, Stundenverlauf, Sendungen im Fenster nach Priorität, "
        "Reihenfolge-Prüfung, Kopf-Rekonstruktion (Zeit als Kopf, Sendungen je Minute, Ruhezeit), "
        "Kopf-Analyse der verworfenen Baken, Empfangslücken über alle Empfänge.",
        "Kopf der Warteschlange = lebender Eintrag mit niedrigster Priorität, dann ältester Schreibzeit "
        "(<code>getNextTxSlot</code>, Supersede setzt die Zeit zurück, <code>txring_functions.cpp:1007</code>).",
        "Marker: <code>RING_WRITE</code> (Eintrag, Feld src mit auto_pos | user_wx | rx_relay | rx_ack_fwd | udp_rx), "
        "<code>RING_PRIO</code>, <code>RING_TX_READ</code> (zum Senden gelesen), "
        "<code>RING_DROP_STALE slot= prio= age_s= msg_id= reason=ttl</code>, "
        "<code>RING_SUPERSEDE old_id= new_id=</code>, <code>RX_TIMEOUT_FIRE wait=</code> (abgelaufene Ruhezeit), "
        "<code>TX_GATE_ENTER</code> (Kanalprüfung), <code>CAD_BUSY</code>, "
        "<code>CSMA_AGED slot= age= stage= hold=</code>, <code>[NBR]|NEED</code> (A = allein zuständig, "
        "B = nicht), <code>[NBR]|REFUSE</code>, <code>[NBR]|CANCEL</code>, <code>[CET];drop;where;relay</code>.",
        "Nenner der HEY-Quote: Episoden = geschrieben minus ersetzt (<code>q2 hey_chains</code>, "
        "<code>extra</code>); „von geschriebenen“ = <code>q34</code> 41/141. Beide stehen auf der Seite.",
        "RX-Fehleranteil: Fehler / (Fehler + ONRXDONE_TIME-Zeilen) = "
        f"{fmt(rxa['errors'])} / ({fmt(rxa['errors'])} + {fmt(rxa['decoded'])}); ACK-Empfänge sind enthalten "
        "(<code>verify/health.md</code> #18).",
        "Längste Empfangslücke: größte Lücke zwischen zwei Empfängen (ONRXDONE_TIME plus RX_ERROR), "
        "ACK-Empfänge eingeschlossen (<code>verify/health.md</code> #8).",
        "HEY-Lücke: zwischen zwei gesendeten HEY; die Verifikation (<code>verify/q34.md</code> #7) liest die "
        "längste als bis zum Logende offen (mindestens 3,0 h).",
        f"CSMA_AGED: {fmt(csma['events'])} bei OC, {fmt(csma_cluster)} im Cluster (Summe von <code>q34</code>, "
        "<code>verify/q34.md</code> #19). Prio 3 nur 38 Episoden im Cluster, davon 1 bei OC.",
        "Modell: <code>q34 cf</code> und <code>ttl_sens</code> (erste Näherung, Obergrenzen, "
        "<code>verify/q34.md</code> #14 bis #16).",
        "Wartezeit-Rechnung: <code>q2 latency.by_src_prio_s</code>; Kopf-Tabelle aus "
        "<code>extra/oe3xoc_ownbeacons.json head_hazard</code> (stimmt mit <code>verify/rca_own_beacons.md</code> "
        "überein).",
        "Opportunity-Zahlen: <code>q6 cancel_opportunity</code> / <code>opportunity_airtime</code>.",
        "Dedup-Rechnung: <code>health ring.dedup.add</code> / (Dauer in Minuten); Verifikation nennt 17,6 min.",
        "Einzelbelege aus der Verifikation (Literale im Text): UDP-Zeile bei 34 von 34 verworfenen WX "
        "(<code>q34.md</code> #8), Beispiel WX 81125FC5 (#9), Beispiel HEY 1AFB2190 (#11), Cluster-Zahlen zu Ping, "
        "Server-Einspeisung und veralteten Relays (<code>CORE-FINDINGS.md</code>).",
    ]))
    b.append("<h3>Code-Stellen</h3>")
    b.append(ul([
        "<code>src/txring_functions.cpp:213-251</code> getNextTxSlot (strikte Priorität, FIFO je Priorität)",
        "<code>src/txring_functions.cpp:385-405</code> Ruhezeit „Fall A“ für Relays (150 ms nach 8 s)",
        "<code>src/txring_functions.cpp:566-645</code> txRingSweep (TTL), <code>:1007</code> Supersede setzt Zeit zurück",
        "<code>src/txring_ttl.h:46-64</code> TTL je Klasse, <code>src/configuration_global.h:465-480</code>",
        "<code>src/configuration_global.h:483-487</code> Ruhezeiten je Priorität (3000/3000/4500/5500/5500 ms)",
        "<code>src/lora_functions.cpp:2746-2747</code> Ruhezeit pro Empfang neu; <code>:3998-4006</code> Wahl des "
        "Pfads; <code>:4030-4043</code> Ruhezeit je Slot (Ansatz für Kandidat A)",
        "<code>src/csma_timing.h:99-111</code> Altersboden (nur Prio 1 bis 3)",
        "<code>src/lora_functions.cpp:661</code>, <code>:1327</code>, <code>:1359</code>, <code>:1380</code>",
        "<code>src/nbr_matrix.cpp:1777</code>, <code>:1816-1852</code>",
        "<code>src/esp32/esp32_main.cpp:5264</code>, <code>:5285</code>; <code>src/nrf52/nrf52_main.cpp:2717-2737</code>; "
        "<code>src/command_functions.cpp:3886</code> (Ping-Zähler)",
        "<code>src/cet_age.h</code> (CET_MAX_AGE_S 150)",
    ]))
    b.append("<h3>Skripte</h3>")
    b.append(ul([
        "Erzeuger dieser Seite: <code>generators/oe3xoc.py --data &lt;dir&gt; --kit &lt;dir&gt; --out &lt;file&gt;</code>.",
        "Messhelfer: <code>generators/lib/oe3xoc_ownbeacons.py</code> (liest die normalisierten Logs, schreibt "
        "<code>data/extra/oe3xoc_ownbeacons.json</code>).",
        "Vorarbeiten: Auswertungen q2 bis q6 und Gesundheitsbericht, Verifikation in <code>verify/</code>.",
    ]))
    wx_rows = [[tm(w["t"]) + ":" + w["t"][17:19], "gesendet" if w["fate"] == "air" else "verworfen (TTL)"]
               for w in ob["wx"]]
    b.append(f"<details><summary>Alle {len(wx_rows)} Wetterpositionen (Schreibzeit und Schicksal)</summary>"
             + table(["Schreibzeit (Loggeruhr)", "Schicksal"], wx_rows) + "</details>")
    b.append("</details>")

    b.append('<div class="foot">Quellen: oe3xoc.log, Auswertungen q2, q34, q5, q6, Gesundheitsbericht, '
             "Stammdaten mcmap. Kernbefunde: CORE-FINDINGS, Verifikation und Wurzelursache in verify/. Netzübersicht: "
             '<a href="uebersicht.html">uebersicht.html</a>. '
             "Auswertung der Fork-Firmware DK5EN, nicht die offizielle Firmware.</div>")

    # Abbildungsnummern in Dokumentreihenfolge vergeben (die Diagramme entstehen vor dem Einbau)
    html = "\n".join(b)
    order: dict[str, int] = {}
    for m in re.finditer(r"@@(\d+)@@", html):
        order.setdefault(m.group(1), len(order) + 1)
    html = re.sub(r"@@(\d+)@@", lambda m: str(order[m.group(1)]), html)
    # Kit zentriert die Zeichnungen per Flex; bei schmaler Anzeige wird links abgeschnitten
    fix = "<style>figure.explain{align-items:flex-start}figure.explain svg{margin-inline:auto}</style>"
    out.write_text(page("MeshCom Log-Auswertung · OE3XOC-12", fix + html), encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--kit", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    build(a.data, a.kit, a.out)
    print(f"wrote {a.out} ({a.out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
