"""Explainer drawings for the HB9VQQ-5 report (classes only, no colours, umlauts as ae/oe/ue).

Every function returns the inner SVG of a 640-px-wide drawing for reportkit.explain(). Text is checked
against the viewBox (about 7.2 px per character at 13 px, bold 7.8 px) so nothing runs out of the drawing.
"""
from __future__ import annotations

W = 640
CH_S = 7.2
CH_B = 7.8


def _chk(x: float, text: str, anchor: str, bold: bool) -> None:
    n = len(text.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&")) * (CH_B if bold else CH_S)
    if anchor == "middle":
        lo, hi = x - n / 2, x + n / 2
    elif anchor == "end":
        lo, hi = x - n, x
    else:
        lo, hi = x, x + n
    if lo < 6 or hi > W - 6:
        raise AssertionError(f"svg text leaves the drawing ({lo:.0f}..{hi:.0f}): {text}")


def txt(x: float, y: float, t: str, small: bool = True, anchor: str = "middle") -> str:
    _chk(x, t, anchor, not small)
    return f'<text class="{"d-txt-s" if small else "d-txt"}" x="{x}" y="{y}" text-anchor="{anchor}">{t}</text>'


def box(x: float, y: float, w: float, h: float, cls: str, lines: list[str], extra: str = "") -> str:
    out = [f'<rect class="d-box {cls} {extra}" x="{x}" y="{y}" width="{w}" height="{h}" rx="6"/>']
    n = len(lines)
    for i, t in enumerate(lines):
        ln = len(t) * (CH_B if i == 0 else CH_S)
        if ln > w - 8:
            raise AssertionError(f"box text too wide for {w}: {t}")
        ty = y + h / 2 + 4.5 + (i - (n - 1) / 2) * 15
        out.append(f'<text class="d-txt{"" if i == 0 else "-s"}" x="{x + w / 2}" y="{ty:.1f}" text-anchor="middle">{t}</text>')
    return "".join(out)


def line(x1: float, y1: float, x2: float, y2: float, cls: str = "", arrow: bool = True) -> str:
    out = [f'<line class="d-line {cls}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>']
    if arrow:
        dx, dy = x2 - x1, y2 - y1
        ln = (dx * dx + dy * dy) ** 0.5 or 1.0
        ux, uy = dx / ln, dy / ln
        px, py = -uy, ux
        a = (x2 - ux * 9 + px * 5, y2 - uy * 9 + py * 5)
        b = (x2 - ux * 9 - px * 5, y2 - uy * 9 - py * 5)
        out.append(f'<polygon class="d-neu" points="{x2},{y2} {a[0]:.1f},{a[1]:.1f} {b[0]:.1f},{b[1]:.1f}"/>')
    return "".join(out)


def dot(x: float, y: float, cls: str, r: int = 9, anim: str = "") -> str:
    return f'<circle class="d-node {cls} {anim}" cx="{x}" cy="{y}" r="{r}"/>'


def crc_edge(crc_n: str, good_n: str, share: str, snr_crc: int, snr_dec: int, ferr_pct: str) -> tuple[str, int]:
    """SNR scale: CRC errors sit at the demodulation floor, decoded frames well above it."""
    def x(snr: float) -> float:
        return 20 + (snr + 27) * 600 / 32

    o = [txt(20, 22, "Signal-Rausch-Abstand (SNR) der Empfangsereignisse von HB9VQQ-5", True, "start")]
    o.append(f'<rect class="d-box d-act" x="20" y="36" width="{x(-17.5) - 20:.1f}" height="44" rx="6"/>')
    o.append(f'<rect class="d-box d-ok" x="{x(-17.5):.1f}" y="36" width="{620 - x(-17.5):.1f}" height="44" rx="6"/>')
    o.append(txt(20 + (x(-17.5) - 20) / 2, 63, "erkannt, nicht lesbar", True))
    o.append(txt(x(-17.5) + (620 - x(-17.5)) / 2, 63, "lesbar (CRC stimmt)", True))
    o.append(line(20, 122, 620, 122, "", True))
    for v in (-25, -17.5, -10, 0):
        o.append(f'<line class="d-line" x1="{x(v):.1f}" y1="116" x2="{x(v):.1f}" y2="128"/>')
    o.append(txt(x(-25), 146, "-25 dB", True))
    o.append(txt(x(-17.5), 146, "-17,5 dB", True))
    o.append(txt(x(-10), 146, "-10 dB", True))
    o.append(txt(x(0), 146, "0 dB", True))
    o.append(dot(x(snr_crc), 103, "d-act", 9, "anim-pulse"))
    o.append(dot(x(snr_dec), 103, "d-ok", 9))
    o.append(txt(20, 172, f"{crc_n} Fehler: Median {snr_crc} dB", True, "start"))
    o.append(txt(620, 172, f"dekodiert: Median {snr_dec} dB", True, "end"))
    o.append(txt(20, 198, "Grenze der Demodulation bei SF11: etwa -17,5 dB", True, "start"))
    o.append(txt(20, 218, f"Fehleranteil {share}: {crc_n} Fehler gegen {good_n} gute Ereignisse", True, "start"))
    o.append(txt(20, 238, f"{ferr_pct} der Fehler ohne Frequenzfehler: kein Stoerer neben der Frequenz", True, "start"))
    o.append(txt(20, 258, "Vermutung: ferne Stationen am Rand der Reichweite, der Standort hoert sehr weit", True, "start"))
    return "".join(o), 274


def console_takeover(lost: int, hour: int, other: int, lo: str, hi: str, total: str, share: str) -> tuple[str, int]:
    o = [box(20, 40, 150, 48, "d-neu", ["Logger", "Mitschnitt-Rechner"])]
    o.append(box(235, 40, 170, 48, "d-acc", ["Konsole des Knotens", "nur eine Sitzung"]))
    o.append(box(470, 40, 150, 48, "d-act", ["zweiter Client", "Betreiberseite"], "anim-pulse"))
    o.append(line(170, 64, 235, 64, "d-line anim-flow", True))
    o.append(line(470, 64, 405, 64, "d-act-s anim-flow", True))
    o.append(txt(437, 30, "meldet sich an", True))
    o.append(txt(202, 30, "liest mit", True))
    o.append(box(20, 112, 600, 34, "d-act", [f"Logger-Sitzung geschlossen, Wiederverbindung nach {lo} bis {hi} s"]))
    o.append(txt(20, 176, f"{lost} Unterbrechungen: {hour} zur vollen Stunde, {other} um 21:37 und 21:38", True, "start"))
    o.append(txt(20, 198, f"Zusammen {total} s = {share} % des Tages; die Zaehler sind Untergrenzen", True, "start"))
    o.append(txt(20, 220, "Davor steht jedes Mal eine Anmeldung an der Konsole (Uebernahme)", True, "start"))
    o.append(txt(20, 242, "Die Firmware arbeitet wie entworfen, der Client liegt beim Betreiber", True, "start"))
    return "".join(o), 258


def hey_wait(own_p50: str, rel_p50: str, ratio: str, mx: str, mx_pct: str, aired: int, written: int) -> tuple[str, int]:
    unit = 22.0
    own = float(own_p50.replace(",", "."))
    rel = float(rel_p50.replace(",", "."))
    o = [txt(20, 22, "Wartezeit im Sendepuffer, Median, gleiche Prioritaet 5 (HEY)", True, "start")]
    o.append(txt(20, 54, "Relay-HEY", True, "start"))
    o.append(f'<rect class="d-box d-neu" x="130" y="40" width="{rel * unit:.1f}" height="22" rx="4"/>')
    o.append(txt(130 + rel * unit + 8, 56, f"{rel_p50} s", True, "start"))
    o.append(txt(20, 90, "eigene HEY", True, "start"))
    o.append(f'<rect class="d-box d-watch" x="130" y="76" width="{own * unit:.1f}" height="22" rx="4"/>')
    o.append(txt(130 + own * unit + 8, 92, f"{own_p50} s ({ratio}-fach)", True, "start"))
    for i, cx in enumerate((150, 210, 270, 330, 390)):
        o.append(dot(cx, 134, "d-watch", 7, f"anim-pulse anim-delay-{i % 4}"))
    o.append(txt(420, 138, "gehoerte Rahmen", True, "start"))
    o.append(txt(20, 172, "Eigene HEY: Haltezeit 5,5 s, jeder gehoerte Rahmen setzt sie zurueck.", True, "start"))
    o.append(txt(20, 192, "Ein Relay derselben Prioritaet eskaliert nach etwa 8 s (NBR Case A).", True, "start"))
    o.append(txt(20, 212, "Vermutung: derselbe Mechanismus wie bei OE3.", True, "start"))
    o.append(txt(20, 238, f"Laengste Wartezeit {mx} s = {mx_pct} % der 180 s Lebensdauer", True, "start"))
    o.append(txt(20, 258, f"{aired} von {written} eigenen HEY gesendet", True, "start"))
    return "".join(o), 274


def silent_cancel(n: int, with_copy: int, air_s: str) -> tuple[str, int]:
    o = [txt(20, 24, "Sendepuffer (20 Plaetze), Relay-Eintraege warten auf den Sendeplatz", True, "start")]
    for i, xx in enumerate((20, 100, 180, 260)):
        extra = "anim-drop" if i == 2 else ""
        o.append(box(xx, 40, 70, 36, "d-neu", ["Relay"], extra))
    o.append(box(450, 36, 170, 44, "d-acc", ["Nachbar sendet", "dieselbe Kopie"]))
    o.append(line(450, 58, 340, 58, "d-line anim-flow", True))
    o.append(txt(395, 52, "wird gehoert", True))
    o.append(box(150, 108, 330, 36, "d-act", ["Eintrag verschwindet ohne Logzeile"]))
    o.append(txt(20, 176, f"{n} Eintraege verschwinden so; bei {with_copy} war zuvor eine fremde Kopie zu hoeren", True, "start"))
    o.append(txt(20, 198, f"Ersparte Sendezeit hoechstens {air_s} s (Modell). Der Schluss ist eine Vermutung.", True, "start"))
    o.append(txt(20, 220, "Geloggt wird nur mit --nbrdebug on (lora_functions.cpp:1445, :1463), kein Zaehler", True, "start"))
    return "".join(o), 236


def hno_late(acc: int, hey: int, pos: int, rel: int, stale: int, age_min: int) -> tuple[str, int]:
    o = [txt(20, 22, "Zeit", True, "start"), line(60, 18, 620, 18, "", True)]
    o.append(box(20, 44, 130, 44, "d-ok", ["frische Kopie", "bei HB9VQQ-5"]))
    o.append(box(190, 44, 250, 44, "d-watch", ["Warteschlange HB9HNO-49", "Firmware 4.35h"], "anim-pulse"))
    o.append(box(480, 44, 140, 44, "d-act", ["Spaetkopie", f"nach ~{age_min} min"]))
    o.append(line(150, 66, 190, 66, "d-line anim-flow", True))
    o.append(line(440, 66, 480, 66, "d-line anim-flow", True))
    o.append(txt(20, 124, f"HB9VQQ-5 nahm {acc} Spaetkopien als neu an ({hey} HEY, {pos} Position)", True, "start"))
    o.append(txt(20, 146, f"und gab {rel} davon weiter. Entwurf: kein Alterstor fuer HEY und Position.", True, "start"))
    o.append(txt(20, 176, f"{{CET}}: {stale} veraltete Telegramme gehoert, 0 angenommen, 0 gesendet", True, "start"))
    o.append(txt(20, 198, "Das {CET}-Tor (150 s) musste nicht eingreifen; die Ursache liegt bei HB9HNO-49", True, "start"))
    return "".join(o), 214


def redundancy(ids: str, share: str, h5: str, h4: str, rr: str) -> tuple[str, int]:
    o = [box(20, 84, 140, 48, "d-acc", ["Rahmen im Mesh", "oder vom Server"])]
    o.append(box(260, 30, 150, 44, "d-watch", ["HB9VQQ-4", "sendet"], "anim-pulse"))
    o.append(box(260, 140, 150, 44, "d-watch", ["HB9VQQ-5", "sendet"], "anim-pulse"))
    o.append(box(490, 84, 130, 48, "d-act", ["Nachbarn", "hoeren es 2 x"]))
    o.append(line(160, 100, 260, 56, "d-line anim-flow", True))
    o.append(line(160, 116, 260, 160, "d-line anim-flow", True))
    o.append(line(410, 56, 490, 100, "d-line anim-flow", True))
    o.append(line(410, 160, 490, 116, "d-line anim-flow", True))
    o.append(txt(20, 218, f"{ids} Kennungen sendeten beide Gateways, davon {rr} Relay-Paare.", True, "start"))
    o.append(txt(20, 240, f"Die spaetere Kopie macht {share} % der Paar-Sendezeit aus (Obergrenze).", True, "start"))
    o.append(txt(20, 262, f"-5 hoert -4 zu {h5} %, -4 hoert -5 zu {h4} % (gemeinsames Fenster)", True, "start"))
    o.append(txt(20, 284, "Nur 5 gemeinsame Nachbarn: die zweite Kopie erreicht oft Neue", True, "start"))
    return "".join(o), 300


def trickle_text(n_forced: int, n_send: int) -> tuple[str, int]:
    o = [box(20, 40, 210, 48, "d-acc", ["Gateway-Regel", "letzte HEY aelter als Imax"])]
    o.append(box(270, 40, 110, 48, "d-ok", ["HEY wird", "gesendet"]))
    o.append(box(420, 40, 200, 48, "d-act", ["Logzeile behauptet:", "consistent=17&lt;k=2"], "anim-pulse"))
    o.append(line(230, 64, 270, 64, "d-line anim-flow", True))
    o.append(line(380, 64, 420, 64, "d-line anim-flow", True))
    o.append(txt(20, 126, f"{n_forced} von {n_send} SEND-Zeilen zeigen eine Zahl, die nicht kleiner als k ist.", True, "start"))
    o.append(txt(20, 148, "Der Text passt dann nicht zur Ursache: gesendet wird wegen der Gateway-Regel.", True, "start"))
    o.append(txt(20, 176, "Kosmetisch: nur der Logtext ist falsch (esp32_main.cpp:4825).", True, "start"))
    return "".join(o), 192
