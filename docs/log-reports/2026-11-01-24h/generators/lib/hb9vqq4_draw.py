"""SVG primitives and explainer drawings of the HB9VQQ-4 page (ASCII text only, viewBox 640 wide).

Every drawing returns (svg inner markup, height). Numbers are passed in by the caller (from data/).
Colours only through the kit classes (.d-*), text >= 13 px (kit CSS), every label is width-checked.
"""

from __future__ import annotations

import math
from collections.abc import Callable

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


def B(x: float, y: float, w: float, h: float, cls: str, label: str = "", sub: str = "", anim: str = "") -> str:
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


def A(x1: float, y1: float, x2: float, y2: float, cls: str = "d-acc-s", flow: bool = False) -> str:
    ang = math.atan2(y2 - y1, x2 - x1)
    hx, hy = x2 - 9 * math.cos(ang), y2 - 9 * math.sin(ang)
    px, py = -math.sin(ang) * 4.5, math.cos(ang) * 4.5
    base = cls[:-2] if cls.endswith("-s") else cls
    fill = base + "-f" if base in ("d-ok", "d-act", "d-watch", "d-acc") else "d-txt"
    line_cls = "d-line " + cls + (" anim-flow" if flow else "")
    return (
        f'<line class="{line_cls}" x1="{x1:g}" y1="{y1:g}" x2="{hx:.1f}" y2="{hy:.1f}"/>'
        f'<polygon class="{fill}" points="{x2:g},{y2:g} {hx + px:.1f},{hy + py:.1f} '
        f'{hx - px:.1f},{hy - py:.1f}"/>'
    )


def L(x1: float, y1: float, x2: float, y2: float, cls: str = "") -> str:
    return f'<line class="d-line {cls}" x1="{x1:g}" y1="{y1:g}" x2="{x2:g}" y2="{y2:g}"/>'


def dot(x: float, y: float, r: float, cls: str, anim: str = "") -> str:
    a = f" {anim}" if anim else ""
    return f'<circle class="d-node {cls}{a}" cx="{x:g}" cy="{y:g}" r="{r:g}"/>'


def cross(x: float, y: float, cls: str = "d-act-s") -> str:
    return L(x - 7, y - 9, x + 7, y + 9, cls) + L(x + 7, y - 9, x - 7, y + 9, cls)


Drawing = tuple[str, int]


def rf_env(lek_n: str, lek_med: str, crc_bin: str, stale_hi: str, stale_72: str, stale_pre: str,
           stale_tot: str, next_hz: str) -> Drawing:
    """Two radio-environment items: off-frequency neighbour and a recurring local source."""
    s = [T(163, 18, "HB9LEK-12 sendet neben der Frequenz", "d-txt")]
    s.append(T(477, 18, "Lokale Quelle bei -72 dBm", "d-txt"))
    # left panel: frequency axis
    s.append(L(24, 96, 304, 96, ""))
    s.append('<rect class="d-box d-ok" x="52" y="48" width="76" height="40" rx="6"/>')
    s.append(T(90, 72, "Fenster", "d-txt-s"))
    s.append('<rect class="d-box d-act anim-pulse" x="214" y="48" width="76" height="40" rx="6"/>')
    s.append(T(252, 72, "Signal", "d-txt-s"))
    s.append(A(130, 68, 210, 68, "d-act-s"))
    s.append(T(90, 118, "Sollfrequenz"))
    s.append(T(252, 118, "+4,5 kHz"))
    s.append(T(163, 148, f"{lek_n} Rahmen, Median {lek_med} Hz daneben"))
    s.append(T(163, 168, f"naechster Nachbar nur {next_hz} Hz"))
    s.append(T(163, 188, f"{crc_bin} CRC-Fehler im Band 4 bis 5 kHz"))
    # right panel: source
    for r, k in ((12, ""), (22, "anim-delay-1"), (32, "anim-delay-2")):
        s.append(dot(380, 68, r, "d-watch", "anim-pulse " + k))
    s.append(dot(380, 68, 5, "d-watch"))
    s.append(A(416, 68, 518, 68, "d-watch-s", True))
    s.append(B(520, 44, 104, 48, "d-ok", "HB9VQQ-4", "hoert zu"))
    s.append(T(477, 118, f"{stale_72} von {stale_tot} Treffern bei -72/-71 dBm"))
    s.append(T(477, 148, f"{stale_pre} von {stale_hi} oberhalb -80 dBm: nur Preamble"))
    s.append(T(477, 168, "kein Header, kein MeshCom-Rahmen"))
    s.append(T(477, 188, "Vermutung, Quelle unbekannt"))
    s.append(T(320, 224, "Beide kosten Empfangszeit, keinen Sendeplatz und keinen Verlust am Knoten"))
    return "".join(s), 238


def cancel(n_van: str, p50: str, hourly: list[int], labels: list[tuple[int, str]], hole_lo: int, hole_hi: int,
           opp_lo: int, opp_hi: int) -> Drawing:
    """Silent overhear-cancel and the hole between 15:39 and 22:39."""
    s = [T(320, 16, "Relay-Kopie im Puffer: gesendet oder still verworfen", "d-txt")]
    s.append(B(16, 30, 178, 56, "d-acc", "Fremde Kopie", f"Median nach {p50} s"))
    s.append(A(196, 58, 226, 58, "d-acc-s"))
    s.append(B(228, 30, 190, 56, "d-watch", "Relay wartet", "Slot im TX-Puffer"))
    s.append(A(420, 58, 450, 58, "d-act-s", True))
    s.append(B(452, 30, 172, 56, "d-act", "Slot frei", "ohne Logzeile", anim="anim-pulse"))
    s.append(T(320, 112, f"{n_van} Relays so verschwunden, jede hatte vorher eine fremde Kopie gehoert"))
    s.append(T(16, 140, "Verworfene Relays je Stunde der Aufzeichnung", "d-txt", "start"))
    x0, cw = 27.0, 24.4
    for i, n in enumerate(hourly):
        cls = "d-act" if n == 0 else "d-ok"
        x = x0 + i * cw
        s.append(f'<rect class="d-box {cls}" x="{x:.1f}" y="152" width="22" height="30" rx="3"/>')
        s.append(T(x + 11, 172, str(n)))
    for i, lab in labels:
        s.append(T(x0 + i * cw + 11, 204, lab))
    a, b = x0 + hole_lo * cw, x0 + (hole_hi + 1) * cw - 2.4
    s.append(L(a, 214, b, 214, "d-act-s"))
    s.append(T(320, 236, f"Stunden {hole_lo} bis {hole_hi}: kein einziger Cancel, obwohl je Stunde"))
    s.append(T(320, 256, f"{opp_lo} bis {opp_hi} gesendete Relays eine fremde Kopie gehoert hatten"))
    s.append(T(320, 280, "Ursache offen: ohne [NBR]-Zeilen nicht lesbar, Zaehler steht in keiner Statistik"))
    return "".join(s), 294


def pair(n_ids: str, a_hears_b: str, b_hears_a: str, share: str, only4: int, both: int, only5: int,
         inj: str) -> Drawing:
    """Two gateways in mutual range air almost the same ids."""
    s = [B(240, 12, 160, 44, "d-neu", "Server", "Zeitsignal, Texte")]
    s.append(A(270, 58, 150, 106, "d-acc-s", True))
    s.append(A(370, 58, 490, 106, "d-acc-s", True))
    s.append(B(30, 108, 200, 54, "d-ok", "HB9VQQ-4", "Orn-Bachtel, 995 m"))
    s.append(B(410, 108, 200, 54, "d-ok", "HB9VQQ-5", "Tanzboden, 1432 m"))
    s.append(T(320, 106, f"-5 hoert -4: {a_hears_b} %"))
    s.append(A(232, 116, 408, 116, "d-watch-s"))
    s.append(A(408, 142, 232, 142, "d-watch-s"))
    s.append(T(320, 168, f"-4 hoert -5: {b_hears_a} %"))
    s.append(A(130, 164, 115, 196, "d-acc-s"))
    s.append(A(510, 164, 525, 196, "d-acc-s"))
    s.append(A(190, 164, 290, 196, "d-watch-s"))
    s.append(A(450, 164, 350, 196, "d-watch-s"))
    s.append(B(20, 198, 190, 50, "d-acc", "nur -4", f"{only4} Nachbarn"))
    s.append(B(225, 198, 190, 50, "d-watch", "beide hoeren", f"{both} Nachbarn"))
    s.append(B(430, 198, 190, 50, "d-acc", "nur -5", f"{only5} Nachbarn"))
    s.append(T(320, 276, f"{n_ids} IDs von beiden gesendet: zweite Kopie = {share} % der Sendezeit (obere Grenze)"))
    s.append(T(320, 298, "Nur beim Nachbarn, der den Erstsender nicht hoert, ist die zweite Kopie nuetzlich"))
    s.append(T(320, 320, f"Gateway-Einspeisung wird nie abgebrochen ({inj} abgebrochen)"))
    return "".join(s), 334


def hey_wait(rel_p50: float, rel_max: float, own_p50: float, own_max: float, ttl: float, ratio: str,
             own_aired: str, de: Callable[[float], str]) -> Drawing:
    """Own HEY waits longer than a relayed HEY of the same priority."""
    x0, x1 = 40.0, 600.0
    k = (x1 - x0) / ttl

    def px(v: float) -> float:
        return x0 + v * k

    s = [T(320, 16, "Wartezeit einer HEY im TX-Puffer, Prioritaet 5, Verfall nach 180 s", "d-txt")]
    s.append(T(x0, 44, f"Relay-HEY: Median {de(rel_p50)} s, laengste {de(rel_max)} s", "d-txt-s", "start"))
    s.append(f'<rect class="d-box d-ok" x="{x0}" y="50" width="{rel_p50 * k:.1f}" height="18" rx="3"/>')
    s.append(L(x0, 59, px(rel_max), 59, "d-ok-s"))
    s.append(dot(px(rel_max), 59, 5, "d-ok"))
    s.append(T(x0, 94, f"Eigene HEY: Median {de(own_p50)} s, laengste {de(own_max)} s", "d-txt-s", "start"))
    s.append(f'<rect class="d-box d-act" x="{x0}" y="100" width="{own_p50 * k:.1f}" height="18" rx="3"/>')
    s.append(L(x0, 109, px(own_max), 109, "d-act-s"))
    s.append(dot(px(own_max), 109, 5, "d-act", "anim-pulse"))
    s.append(L(px(ttl), 38, px(ttl), 132, "d-act-s"))
    s.append(T(px(ttl) - 6, 152, "Verfall", "d-txt-s", "end"))
    s.append(L(x0, 132, x1, 132, ""))
    for v in (0, 60, 120):
        s.append(T(px(v) + (10 if v == 0 else 0), 152, f"{v} s"))
    s.append(T(320, 182, "Eigene Bake am Listenkopf: Haltezeit 5,5 s, mit jedem empfangenen Rahmen neu", "d-txt-s"))
    s.append(T(320, 202, "Relay gleicher Prioritaet: nach 8 s nur noch 150 bis 220 ms (NBR Fall A)", "d-txt-s"))
    s.append(T(320, 226, f"Faktor {ratio} beim Median, aber kein Verlust: {own_aired} eigene HEY gesendet", "d-txt"))
    return "".join(s), 242


def stale_hno(n_fr: str, a_min: str, a_max: str, dup: str, new: str, own_max: str, age_ex: str, wait_min: str,
              ring_min: str) -> Drawing:
    """Why the late {CET} frames of HB9HNO-49 stay harmless here."""
    s = [B(14, 30, 130, 56, "d-acc", "Server", "{CET} Zeitsignal")]
    s.append(A(146, 58, 176, 58, "d-acc-s"))
    s.append(B(178, 30, 150, 56, "d-ok", "HB9VQQ-5", "frisch, 5 s alt"))
    s.append(A(330, 58, 360, 58, "d-act-s"))
    s.append(B(362, 30, 264, 56, "d-act", "HB9HNO-49 (FW 4.35h)", f"sendet {wait_min} min spaeter, {age_ex} s alt", anim="anim-pulse"))
    s.append(T(320, 16, "Beispiel: Telegramm 6ADB5BE4, 01.11.2026 06:22", "d-txt"))
    s.append(A(494, 88, 494, 120, "d-act-s", True))
    s.append(B(362, 122, 264, 52, "d-watch", "HB9VQQ-4 empfaengt", f"{n_fr} Rahmen, {a_min} bis {a_max} s alt"))
    s.append(L(494, 176, 494, 190, "d-neu-s"))
    s.append(L(116, 190, 524, 190, "d-neu-s"))
    for xx in (116, 320, 524):
        s.append(A(xx, 190, xx, 204, "d-ok-s"))
    s.append(B(16, 206, 200, 56, "d-ok", f"{dup} von {n_fr}", "Dedup-Ring: DUP"))
    s.append(B(220, 206, 200, 56, "d-ok", f"{new} von {n_fr}", "neu, Hop 0: Ende"))
    s.append(B(424, 206, 200, 56, "d-ok", "Eigene {CET}", f"nie aelter als {own_max} s"))
    s.append(T(24, 150, "Ring vergisst IDs", "d-txt-s", "start"))
    s.append(T(24, 170, f"erst nach {ring_min} min", "d-txt-s", "start"))
    s.append(T(320, 290, "Das Tor bei 150 s wird nie erreicht: es gab nichts Veraltetes zu stoppen", "d-txt"))
    return "".join(s), 304


def qfull(depth_max: str, n: str) -> Drawing:
    """Log text 'q=full' for RING_SUPERSEDE_KEEP."""
    s = [B(16, 30, 170, 56, "d-acc", "Neues {CET}", "kommt an")]
    s.append(A(188, 58, 218, 58, "d-acc-s"))
    s.append(B(220, 30, 212, 56, "d-watch", "Puffer", "neueres {CET} wartet schon"))
    s.append(A(434, 58, 464, 58, "d-neu-s"))
    s.append(B(466, 30, 158, 56, "d-neu", "Kopie verworfen", "kein Slot (-1)"))
    s.append(A(545, 88, 545, 118, "d-act-s", True))
    s.append(B(446, 120, 178, 56, "d-act", "Log: q=full", "Wort stimmt nicht", anim="anim-pulse"))
    s.append(T(16, 128, f"Puffer war nie voll: hoechstens {depth_max} von 20", "d-txt-s", "start"))
    s.append(T(16, 150, f"{n} von {n} Zeilen stehen direkt nach", "d-txt-s", "start"))
    s.append(T(16, 172, "RING_SUPERSEDE_KEEP derselben ID", "d-txt-s", "start"))
    s.append(T(320, 206, "Nur der Text irrt, es geht nichts verloren", "d-txt"))
    return "".join(s), 220


def trickle(n_wrong: str, n_all: str) -> Drawing:
    """Log text 'consistent=17<k=2' printed for gateway-forced sends."""
    s = [B(16, 30, 196, 56, "d-acc", "Gateway-Regel", "letzte HEY >= Imax alt")]
    s.append(A(214, 58, 244, 58, "d-acc-s"))
    s.append(B(246, 30, 170, 56, "d-ok", "HEY geht raus", "gewollt"))
    s.append(A(418, 58, 448, 58, "d-act-s"))
    s.append(B(450, 30, 174, 56, "d-act", "Log: 17<k=2", "17 ist nicht kleiner 2", anim="anim-pulse"))
    s.append(T(320, 120, f"{n_wrong} von {n_all} SEND-Zeilen tragen diesen falschen Vergleich"))
    s.append(T(320, 142, "Der Grund der Sendung wird nicht genannt, sinnvoll waere gateway_imax"))
    s.append(T(320, 172, "Nur der Text irrt, die Sendung selbst ist gewollt", "d-txt"))
    return "".join(s), 188
