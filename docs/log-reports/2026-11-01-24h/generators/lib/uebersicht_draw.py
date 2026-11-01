"""SVG primitives and explainer drawings of the overview page (ASCII text only, viewBox 640 wide)."""

from __future__ import annotations

import math


W = 640


def _esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _w(s: str, cls: str) -> float:
    return len(s) * (7.4 if cls == "d-txt" else 6.6)


def T(x: float, y: float, s: str, cls: str = "d-txt-s", anchor: str = "middle") -> str:
    assert s.isascii(), s
    w = _w(s, cls)
    lo = x - w / 2 if anchor == "middle" else (x if anchor == "start" else x - w)
    assert lo >= 6 and lo + w <= W - 6, (s, lo, lo + w)
    return f'<text class="{cls}" x="{x:g}" y="{y:g}" text-anchor="{anchor}">{_esc(s)}</text>'


def B(x: float, y: float, w: float, h: float, cls: str, label: str = "", sub: str = "",
      anim: str = "") -> str:
    out = [f'<rect class="d-box {cls}" x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="8"/>']
    cx = x + w / 2
    if label and sub:
        assert _w(label, "d-txt") <= w - 8 and _w(sub, "d-txt-s") <= w - 8, (label, sub)
        out.append(T(cx, y + h / 2 - 2, label, "d-txt"))
        out.append(T(cx, y + h / 2 + 14, sub, "d-txt-s"))
    elif label:
        assert _w(label, "d-txt") <= w - 8, label
        out.append(T(cx, y + h / 2 + 5, label, "d-txt"))
    inner = "".join(out)
    return f'<g class="{anim}">{inner}</g>' if anim else inner


def C(x: float, y: float, r: float, cls: str, label: str = "") -> str:
    out = [f'<circle class="d-node {cls}" cx="{x:g}" cy="{y:g}" r="{r:g}"/>']
    if label:
        out.append(T(x, y + 5, label, "d-txt"))
    return "".join(out)


def A(x1: float, y1: float, x2: float, y2: float, cls: str = "d-acc-s", flow: bool = False) -> str:
    ang = math.atan2(y2 - y1, x2 - x1)
    hx, hy = x2 - 9 * math.cos(ang), y2 - 9 * math.sin(ang)
    px, py = -math.sin(ang) * 4.5, math.cos(ang) * 4.5
    base = cls[:-2] if cls.endswith("-s") else cls
    fill = base + "-f" if base in ("d-ok", "d-act", "d-watch", "d-acc") else "d-txt"
    line_cls = "d-line " + cls + (" anim-flow" if flow else "")
    return (f'<line class="{line_cls}" x1="{x1:g}" y1="{y1:g}" x2="{hx:.1f}" y2="{hy:.1f}"/>'
            f'<polygon class="{fill}" points="{x2:g},{y2:g} {hx + px:.1f},{hy + py:.1f} '
            f'{hx - px:.1f},{hy - py:.1f}"/>')


def L(x1: float, y1: float, x2: float, y2: float, cls: str = "") -> str:
    return f'<line class="d-line {cls}" x1="{x1:g}" y1="{y1:g}" x2="{x2:g}" y2="{y2:g}"/>'


def dot(x: float, y: float, r: float, cls: str, anim: str = "") -> str:
    a = f" {anim}" if anim else ""
    return f'<circle class="{cls}{a}" cx="{x:g}" cy="{y:g}" r="{r:g}"/>'


# Each drawing returns (svg inner markup, height). Numbers are passed in by the caller (from data/).

Drawing = tuple[str, int]


def ping_platform() -> Drawing:
    """Item 1: ESP32 counts the auto-ping down, RAK4631 has no counter."""
    s = [T(320, 16, "Auto-Ping: gleiche Einstellung, zwei Plattformen", "d-txt")]
    s.append(B(16, 34, 150, 52, "d-ok", "ESP32-Schleife", "esp32_main.cpp"))
    s.append(A(168, 60, 208, 60, "d-ok-s"))
    s.append(B(210, 34, 210, 52, "d-ok", "node_pingcount > 0 ?", "zaehlt je Ping herunter"))
    s.append(A(422, 60, 462, 60, "d-ok-s"))
    s.append(B(464, 34, 160, 52, "d-ok", "Stopp", "nach node_pingmax"))
    s.append(B(16, 110, 150, 52, "d-act", "RAK4631-Schleife", "nrf52_main.cpp"))
    s.append(A(168, 136, 208, 136, "d-act-s"))
    s.append(B(210, 110, 210, 52, "d-act", "kein Zaehler", "nur node_pingcall gesetzt ?"))
    s.append(A(422, 136, 462, 136, "d-act-s", True))
    s.append(B(464, 110, 160, 52, "d-act", "Ping alle 60 s", "ohne Ende", anim="anim-pulse"))
    s.append(T(320, 196, "RAK4631: --ping stop setzt nur node_pingcount = 0 und bleibt wirkungslos"))
    s.append(T(320, 218, "es hilft nur, --pingcall zu loeschen; upstream dev hat denselben Code"))
    return "".join(s), 232


def ping_255(n: str, hours: str) -> Drawing:
    """Item 2: no validation of the ping target."""
    s = [B(16, 40, 100, 48, "d-neu", "Einstellung", "pingcall 255.")]
    s.append(A(118, 64, 158, 64, "d-neu-s"))
    s.append(B(160, 40, 150, 48, "d-acc", "Ring Prio 1", "255.:{ping}"))
    s.append(A(312, 64, 352, 64, "d-acc-s", True))
    s.append(B(354, 40, 140, 48, "d-act", "Decode", "Ziel ungueltig"))
    s.append(A(496, 64, 536, 64, "d-act-s"))
    s.append(B(538, 40, 86, 48, "d-neu", "0 Funk", "verworfen"))
    s.append(T(320, 120, f"{n} Mal in {hours} h (1 pro Minute), keine Sendung, keine Rueckmeldung"))
    s.append(T(320, 142, "jeder Eintrag kostet einen CAD-Zyklus; weder --pingcall noch sendPing prueft das Ziel"))
    return "".join(s), 156


def own_vs_relay(relay_rate: str, own_rate: str, rx_per_min: str) -> Drawing:
    """Item 3: root cause - same priority, different hold at the head of the queue."""
    s = [T(320, 16, "Am Kopf der Warteschlange, gleiche Prioritaet 4/5", "d-txt")]
    s.append(B(16, 30, 170, 52, "d-ok", "Relay am Kopf", "nach 8 s Wartezeit"))
    s.append(A(188, 56, 218, 56, "d-ok-s"))
    s.append(B(220, 30, 232, 52, "d-ok", "Haltezeit 150-220 ms", "NBR Fall A: kurze Suche"))
    s.append(A(454, 56, 484, 56, "d-ok-s", True))
    s.append(B(486, 30, 138, 52, "d-ok", "Funk", f"{relay_rate} je Minute"))
    s.append(B(16, 100, 170, 52, "d-act", "Eigener Beacon", "am Kopf, kein Ausweg"))
    s.append(A(188, 126, 218, 126, "d-act-s"))
    s.append(B(220, 100, 232, 52, "d-act", "Haltezeit 5,5 s", "bei jedem Empfang neu", anim="anim-pulse"))
    s.append(A(454, 126, 484, 126, "d-act-s"))
    s.append(B(486, 100, 138, 52, "d-neu", "Funk", f"{own_rate} je Minute"))
    s.append(L(466, 112, 478, 140, "d-act-s"))
    s.append(L(478, 112, 466, 140, "d-act-s"))
    s.append(T(320, 180, f"OE3XOC hoert ca. {rx_per_min} Frames pro Minute: jedes setzt die 5,5 s zurueck"))
    s.append(T(320, 202, "Altersboden und Fall-A-Suche gelten nur fuer Relays"))
    s.append(T(320, 224, "nach 120 s (WX) bzw. 180 s (HEY) verfaellt der Beacon"))
    return "".join(s), 238


def ack_no_cancel(a: str, b: str, c: str, d: str) -> Drawing:
    """Item 4: ACK relays have no overhear-cancel path."""
    s = [C(50, 95, 26, "", "A"), A(78, 95, 118, 95, "d-acc-s", True)]
    s.append(B(120, 40, 300, 110, "d-box"))
    s.append(T(270, 58, "Warteschlange von B"))
    s.append(B(136, 70, 132, 48, "d-neu", "Cancel-Scan", "nur Relays"))
    s.append(B(320, 70, 90, 48, "d-act", "ACK", "Prio 1"))
    s.append(A(270, 94, 316, 94, "d-neu-s"))
    s.append(L(284, 82, 304, 106, "d-act-s"))
    s.append(L(304, 82, 284, 106, "d-act-s"))
    s.append(A(422, 94, 500, 94, "d-act-s", True))
    s.append(B(504, 70, 120, 48, "d-act", "Funk", "doppelt"))
    s.append(T(320, 176, f"{a} / {b} / {c} / {d} ACK-Relays (HL / IA / IR / OC): nach gehoerter Kopie gesendet"))
    s.append(T(320, 198, "Obergrenze: die gehoerte Kopie muss nicht dieselben Hoerer abdecken"))
    return "".join(s), 212


def server_slot(n: str, m: str, share: str) -> Drawing:
    """Item 5: server slots are always case A."""
    s = [B(16, 40, 150, 52, "d-acc", "{CET}-Slot", "vom Server")]
    s.append(A(168, 66, 208, 66, "d-acc-s"))
    s.append(B(210, 40, 214, 52, "d-watch", "Menge 'nur ich'", "nie leer (MIF, NRA)"))
    s.append(A(426, 66, 466, 66, "d-act-s", True))
    s.append(B(468, 40, 156, 52, "d-act", "Fall A", f"{n} von {m}, 0 Cancel"))
    s.append(T(320, 126, f"{share} % unserer Gateway-Sendungen folgen einer gehoerten Aussendung"))
    s.append(T(320, 148, "Frage an die NBR-Verantwortlichen: warum ist die Menge fuer Server-Slots nie leer?"))
    s.append(T(320, 170, "(Abhaengigen-Regel und Rueckkanten-Test, nicht das 12-h-Fenster)"))
    return "".join(s), 184


def deaf_rx(n: str, pct: str, longest: str) -> Drawing:
    """Item 6: OE3XIA deaf receiver."""
    s = [B(16, 30, 170, 56, "d-ok", "Andere Knoten", "empfangen weiter")]
    s.append(A(188, 58, 258, 58, "d-ok-s", True))
    s.append(B(260, 30, 150, 56, "d-act", "OE3XIA-12", "kein RX-Interrupt", anim="anim-pulse"))
    s.append(B(450, 18, 174, 36, "d-ok", "CAD sieht Kanal belegt"))
    s.append(B(450, 62, 174, 36, "d-ok", "Senden laeuft"))
    s.append(B(450, 106, 174, 36, "d-watch", "Rx-Neustart alle 4,8 s"))
    for y in (36, 80, 124):
        s.append(L(410, 58, 448, y))
    s.append(T(320, 172, f"{n} Stillen laenger als 5 Minuten = {pct} % der Aufzeichnung, laengste {longest} s", "d-txt"))
    s.append(T(320, 196, "Ende meist nach Standby + CAD; der Neustart ohne Standby loest es nicht (Verdacht)"))
    return "".join(s), 210


def ping_stream(acks: str, per_ping: str) -> Drawing:
    s = [B(16, 66, 130, 56, "d-acc", "OE3XWJ-12", "Ping je Minute")]
    s.append(A(148, 94, 228, 94, "d-acc-s", True))
    s.append(T(188, 84, "{ping}"))
    s.append(B(230, 50, 190, 88, "d-neu", "Fremde Gateways", f"ACK-IDs je Ping: {per_ping}"))
    s.append(A(422, 94, 502, 94, "d-act-s", True))
    s.append(T(462, 84, "ACKs"))
    s.append(B(504, 66, 120, 56, "d-act", "ACK-Relays", f"{acks} Sendungen"))
    s.append(T(320, 168, "Der Ping loest bei fremden Gateways ACKs aus, unsere Knoten relaisen sie"))
    s.append(T(320, 190, "das Cluster sendet diese ACKs weiter, obwohl sie keinen Cancel-Pfad haben"))
    return "".join(s), 204


def veto() -> Drawing:
    s = [C(70, 150, 26, "", "A"), C(320, 60, 26, "d-watch", "B"), C(570, 150, 26, "", "C")]
    s.append(A(98, 150, 542, 150, "d-acc-s", True))
    s.append(T(320, 140, "Kopie von A"))
    s.append(A(88, 130, 298, 74, "d-acc-s"))
    s.append(T(190, 86, "B hoert A"))
    s.append(A(342, 74, 552, 130, "d-act-s", True))
    s.append(T(450, 86, "B sendet doppelt"))
    s.append(T(320, 196, "B hoert die Kopie von A, waehrend sein Relay wartet (REFUSE)"))
    s.append(T(320, 218, "und sendet trotzdem: Fall A = 'ich bin allein zustaendig'"))
    return "".join(s), 230


def stale_cet(rows: list[tuple[str, float, str]]) -> Drawing:
    """Bars: (label, age seconds, class); 150 s line is drawn."""
    k = 0.4
    s = []
    for i, (lab, v, cls) in enumerate(rows):
        y = 40 + i * 34
        wbar = v * k
        s.append(T(214, y + 17, lab, anchor="end"))
        s.append(f'<rect class="d-box {cls}" x="222" y="{y}" width="{wbar:g}" height="24" rx="3"/>')
        s.append(T(222 + wbar + 6, y + 17, f"{v:g} s".replace(".", ","), "d-txt-s", "start"))
    xl = 222 + 150 * k
    ybot = 40 + len(rows) * 34
    s.append(f'<line class="d-line d-watch-s" x1="{xl:g}" y1="28" x2="{xl:g}" y2="{ybot}"/>')
    s.append(T(xl, 18, "Alterssperre 150 s"))
    s.append(T(320, ybot + 24, "Alter der Zeit beim Senden: unsere Gateways Maximum, fremde Relays Median"))
    return "".join(s), ybot + 36


def fanout(own: str, foreign: str, relays: str, total: str, air: str) -> Drawing:
    s = [B(240, 12, 160, 40, "d-acc", "OE1XAR-33")]
    cols = [(16, "d-acc", "5 eigene Gateways", f"{own} je Telegramm"),
            (232, "d-watch", "fremde Gateways", f"{foreign} gehoert je Tel."),
            (448, "d-neu", "Relays", f"{relays} je Telegramm")]
    for x, cls, lab, sub in cols:
        s.append(B(x, 100, 176, 56, cls, lab, sub))
        s.append(A(320, 52, x + 88, 100, "d-acc-s", True))
    s.append(T(320, 186, f"zusammen im Mittel {total} Aussendungen je {{CET}}-Telegramm", "d-txt"))
    s.append(T(320, 208, f"ca. {air} s Sendezeit je Telegramm (Untergrenze), alle 607 s"))
    s.append(T(320, 230, "dazu ca. 2 fremde Gateways je Telegramm, die nur aus Relay-Pfaden erschlossen sind"))
    return "".join(s), 244


def dedup_window(n: str) -> Drawing:
    k = 17.3
    x0 = 60
    s = [f'<rect class="d-box d-ok" x="{x0}" y="50" width="{17.7 * k:.1f}" height="28" rx="4"/>',
         f'<rect class="d-box d-neu" x="{x0 + 17.7 * k:.1f}" y="50" width="{(26.4 - 17.7) * k:.1f}" '
         'height="28" rx="4"/>',
         T(x0 + 17.7 * k / 2, 69, "ID im Dedup-Ring"),
         T(x0 + (17.7 + 26.4) / 2 * k, 69, "Fenster endet (Last)"),
         L(60, 110, 580, 110)]
    for m in (0, 10, 20, 30):
        s.append(T(x0 + m * k, 134, "0" if m == 0 else f"{m} min"))
    s.append(dot(x0 + 23.5 * k, 110, 8, "d-act-f", "anim-pulse"))
    s.append(T(x0 + 23.5 * k, 98, "Kopie kehrt zurueck (22-25 min)"))
    s.append(T(320, 168, "Fenster = 100 Eintraege bei 3,8 bis 5,7 IDs pro Minute = 17,7 bis 26,4 min"))
    s.append(T(320, 190, f"ein fremder Knoten sendet sie erneut, der Ring laesst sie durch ({n} Faelle)"))
    return "".join(s), 204


def interferer() -> Drawing:
    s = ['<rect class="d-box d-neu" x="130" y="170" width="110" height="20" rx="3"/>',
         '<rect class="d-box d-act anim-pulse" x="330" y="50" width="110" height="140" rx="3"/>',
         L(100, 190, 480, 190),
         T(185, 160, "-110 dBm"), T(385, 40, "-50 bis -56 dBm"),
         T(185, 210, "Rauschteppich (p5)"), T(385, 210, "lokale Quelle OE3XIR"),
         T(480, 90, "323 RX-Fehler", anchor="start"),
         T(480, 108, "ueber -60 dBm,", anchor="start"),
         T(480, 126, "jede Stunde", anchor="start")]
    return "".join(s), 222


def csma_chain() -> Drawing:
    s = []
    for i, lab in enumerate("ABCD"):
        s.append(C(60 + i * 170, 80, 24, "d-watch" if i else "", lab))
    for i in range(3):
        s.append(A(86 + i * 170, 80, 204 + i * 170, 80, "d-watch-s", True))
        s.append(T(145 + i * 170, 62, "21-23 s"))
    s.append(T(320, 134, "3 Hops mal 21-23 s Median-Wartezeit = 63 bis 70 s", "d-txt"))
    s.append(T(320, 158, "Vermutung: ueberschreitet die 40-s-Wiederholung des Absenders (nicht beobachtet)"))
    return "".join(s), 172


def ssids() -> Drawing:
    s = [B(16, 20, 160, 44, "d-acc", "OE1XAR-62", "per UDP, 145 mal"),
         B(16, 76, 160, 44, "d-neu", "OE1XAR-33", "nur per Funk"),
         B(16, 132, 160, 44, "d-neu", "OE1XAR-63", "nur per Funk"),
         C(500, 90, 44, "d-watch", "DB0AIS")]
    for y in (42, 98, 154):
        s.append(A(178, y, 456, 90, "d-acc-s", True))
    s.append(T(500, 156, "11 bis 12 % der Sendezeit"))
    s.append(T(500, 176, "fuer {CET} (OE3: 3 bis 5 %)"))
    return "".join(s), 192


def clock_offset() -> Drawing:
    s = [B(16, 30, 170, 56, "d-neu", "Logger", "10:00"),
         B(16, 104, 170, 56, "d-watch", "OE3XIR-12", "zeigt 11:00 (+1 h)"),
         A(188, 58, 298, 58, "d-ok-s"), A(188, 132, 298, 132, "d-watch-s"),
         B(300, 30, 324, 56, "d-ok", "Altersrechnung in UTC", "bleibt richtig"),
         B(300, 104, 324, 56, "d-watch", "Anzeige und Log", "1 h daneben (UTC+2 fest)"),
         T(320, 188, "Gleiches galt fuer DB0AIS bis 18:20 (Knotenuhr UTC+2, dann Sprung um -1 h)")]
    return "".join(s), 200


def eth04() -> Drawing:
    k = 135
    s = [L(60, 100, 600, 100),
         f'<rect class="d-box d-ok" x="60" y="82" width="{1.05 * k:.1f}" height="18" rx="3"/>',
         T(131, 72, "Mitschnitt 25 h"),
         L(532, 70, 532, 100, "d-watch-s"), dot(532, 100, 7, "d-watch-f", "anim-pulse"),
         T(532, 60, "Lease-Erneuerung ca. 3,5 d")]
    for d in range(5):
        s.append(T(60 + d * k, 128, "0" if d == 0 else f"{d} d"))
    s.append(T(320, 164, "Start und 2 Link-Flaps sauber; die Erneuerung (Ausloeser von ETH-04) blieb ungetestet"))
    return "".join(s), 180


def pre_cad() -> Drawing:
    s = [B(16, 24, 190, 48, "d-watch", "4.35-Station", "sendet ohne Kanalabfrage"),
         B(16, 120, 190, 48, "d-ok", "4.40a-Station", "hoert erst (CAD)"),
         B(300, 70, 120, 60, "d-neu", "Kanal", "gemeinsam"),
         A(208, 48, 298, 90, "d-watch-s", True), A(208, 144, 298, 112, "d-ok-s"),
         T(440, 84, "16 bis 26 %", "d-txt", "start"),
         T(440, 104, "der Empfangsereignisse", anchor="start"),
         T(440, 122, "je Knoten stammen von", anchor="start"),
         T(440, 140, "Pre-CAD-Firmware", anchor="start")]
    return "".join(s), 186


def supersede_chain() -> Drawing:
    s = []
    for i in range(4):
        x = 24 + i * 160
        s.append(B(x, 40, 112, 44, "d-ok" if i == 3 else "d-neu", f"HEY {i + 1}"))
        if i < 3:
            s.append(A(x + 114, 62, x + 158, 62, "d-acc-s", True))
    s.append(T(320, 108, "jede neue HEY ersetzt die wartende und setzt die Eintragszeit zurueck"))
    s.append(T(320, 142, "Kette lebt 537 bis 902 s; die TTL von 180 s zaehlt ab dem letzten Ersetzen", "d-txt"))
    s.append(T(320, 166, "TTL begrenzt das Alter des Inhalts, nicht die Wartezeit des Beacons"))
    return "".join(s), 180


def stale_log() -> Drawing:
    s = [B(16, 30, 300, 48, "d-box", "RING_DROP_STALE", "slot, prio, age, id, reason"),
         A(318, 54, 358, 54, "d-neu-s"),
         B(360, 30, 120, 48, "d-act", "Typ ?", "Quelle ?"),
         A(420, 80, 420, 108, "d-neu-s"),
         B(360, 110, 264, 44, "d-neu", "RING_WRITE-Zeile noetig", "fehlt vor Log-Start"),
         T(320, 188, "5 Verwerfungen bei OE3XIA (06:41:14-24) vor Logbeginn: nicht klassifizierbar")]
    return "".join(s), 200


def hb9_stale_cet(age_min: str, age_s: str, gate_max: str) -> Drawing:
    """Swiss mesh: HB9HNO-49 (4.35h) re-radiates frames about 25 min late; {CET} stops, HEY/position pass."""
    s = [T(320, 16, "HB9HNO-49 sendet Frames mit ca. 25 Minuten Verspaetung", "d-txt")]
    s.append(B(16, 34, 180, 60, "d-watch", "HB9HNO-49", "FW 4.35h, ohne Sperre", anim="anim-pulse"))
    s.append(A(198, 64, 238, 64, "d-watch-s", True))
    s.append(B(240, 34, 160, 60, "d-box", "HB9VQQ-4 / -5", "Frame ca. 25 min alt"))
    s.append(A(402, 52, 440, 41, "d-ok-s"))
    s.append(B(442, 18, 182, 46, "d-ok", "{CET}: gestoppt", "Dedup / Hop0, kein Relay"))
    s.append(A(402, 76, 440, 99, "d-watch-s", True))
    s.append(B(442, 76, 182, 46, "d-watch", "HEY/Position: relayt", "by design, kein Alterstor"))
    s.append(L(70, 168, 570, 168, "d-acc-s"))
    s.append(dot(90, 168, 7, "d-ok-f"))
    s.append(T(90, 150, "frisch (5 s)"))
    s.append(dot(550, 168, 8, "d-watch-f", "anim-pulse"))
    s.append(T(550, 150, f"Alter {age_s} s", "d-txt-s", "middle"))
    s.append(A(104, 168, 536, 168, "d-watch-s", True))
    s.append(T(320, 190, f"{age_min} Minuten Rueckstau bis zur erneuten Aussendung", "d-txt"))
    s.append(T(320, 218, "Einzige alte {CET}, die als neu angenommen wurde: Hop0-Ausstieg, kein Ring-Eintrag"))
    s.append(T(320, 240, f"Eigene {{CET}}-Sendungen beider Knoten: Alter hoechstens {gate_max} s, 0 Sperren-Treffer"))
    return "".join(s), 254


def hb9_silent_cancel(n: str, pct: str) -> Drawing:
    """Swiss mesh: the overhear-cancel frees relay slots without any log line (observability gap)."""
    s = [B(16, 30, 170, 56, "d-neu", "Relay-Slot", "wartet in der Queue")]
    s.append(A(188, 58, 218, 58, "d-neu-s"))
    s.append(B(220, 30, 200, 56, "d-ok", "NBR-Cancel", "gibt den Slot still frei"))
    s.append(A(422, 58, 462, 58, "d-watch-s"))
    s.append(B(464, 30, 160, 56, "d-watch", "Log-Zeile", "nur mit nbrLog", anim="anim-pulse"))
    s.append(T(320, 120, f"HB9VQQ-4: {n} Slots ({pct} % der Eintraege) nach fremder Kopie still frei", "d-txt"))
    s.append(T(320, 146, "Beleg nur ueber die Bilanz: geschrieben = gesendet + ersetzt + verschwunden"))
    s.append(T(320, 168, "Der Zaehler stat_nbr_cancel steht in keiner STAT-Zeile"))
    s.append(T(320, 190, "Von 15:39 bis 22:39 kein Cancel: Ursache offen (keine [NBR]-Zeilen im Log)"))
    return "".join(s), 204
