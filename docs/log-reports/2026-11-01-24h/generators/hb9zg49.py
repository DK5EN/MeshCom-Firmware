#!/usr/bin/env python3
"""HB9ZG-49 receive check (deaf, not mute) -> hb9zg49.html.

Reads only --data (data/hb9/extra/hb9zg49_log.json from lib/hb9zg49_log.py, data/hb9/extra/hb9zg49_mcmap.json
saved from the mcmap MCP) and --kit (tools/logreport/reportkit.py).
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path


def load_kit(kit: Path):
    spec = importlib.util.spec_from_file_location("reportkit", kit / "reportkit.py")
    mod = importlib.util.module_from_spec(spec)  # type: ignore[arg-type]
    spec.loader.exec_module(mod)  # type: ignore[union-attr]
    return mod


def km(a: dict, b: dict) -> float:
    dlat = (b["lat"] - a["lat"]) * 111.2
    dlon = (b["lon"] - a["lon"]) * 111.32 * math.cos(math.radians((a["lat"] + b["lat"]) / 2))
    return math.hypot(dlat, dlon)


def de(v: float, nd: int = 1) -> str:
    s = f"{v:.{nd}f}" if nd else f"{v:.0f}"
    return s.replace("-", "−").replace(".", ",")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--kit", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    k = load_kit(a.kit)
    esc, chip = k.esc, k.chip
    lg = json.loads((a.data / "hb9" / "extra" / "hb9zg49_log.json").read_text())
    mc = json.loads((a.data / "hb9" / "extra" / "hb9zg49_mcmap.json").read_text())
    ZG = lg["call"]
    nodes = dict(mc["nodes"])
    master = a.data / "hb9" / "master" / "master.tsv"  # mcmap master data of the HB9 run, position fallback
    if master.exists():
        for row in master.read_text().splitlines()[1:]:
            f = row.split("\t")
            if len(f) >= 6 and f[0] not in nodes and f[3] and f[4]:
                nodes[f[0]] = {"hw": f[1], "fw": f[2], "lat": float(f[3]), "lon": float(f[4]), "alt": f[5]}
    zgn = nodes[ZG]
    links = mc["zg_links"]
    tx = [x for x in links if x["role"] == "tx"]
    rx = [x for x in links if x["role"] == "rx"]
    tx_act = [x for x in tx if x["active"]]
    rx_act = [x for x in rx if x["active"]]
    by = {(x["fromCall"], x["toCall"]): x for x in links + mc["control_links"]}
    rx_floor = [x for x in rx if x["rssiAvg"] <= -126.5]
    zrep = lg["relays"][ZG]

    # pair table: partners with both directions
    pairs = []
    for p in sorted({x["fromCall"] for x in rx} & {x["toCall"] for x in tx}):
        hears_zg, zg_hears = by[(ZG, p)], by[(p, ZG)]
        pairs.append((p, hears_zg, zg_hears, hears_zg["snrAvg"] - zg_hears["snrAvg"]))
    pairs.sort(key=lambda t: -t[3])
    ctrl = []
    for p, q in (("HB9CF-40", "HB9CF-44"), ("HB9CF-40", "HB9CF-46"), ("HB9CF-40", "HB9JAO-9")):
        f, r = by[(p, q)], by[(q, p)]
        ctrl.append((f"{p} ↔ {q}", f, r, abs(f["snrAvg"] - r["snrAvg"])))
    big = [t for t in pairs if t[3] >= 10 and min(t[1]["samples"], t[2]["samples"]) >= 5]
    gmin, gmax = min(t[3] for t in big), max(t[3] for t in big)

    # wider control set: weak-SNR pairs without ZG (mcmap 7d), and stations that hear ZG but ZG never hears
    wctl = mc["control_pairs_weak"]["pairs"]
    wgaps = sorted(g for _, _, g in wctl)
    wmed = (wgaps[len(wgaps) // 2 - 1] + wgaps[len(wgaps) // 2]) / 2 if len(wgaps) % 2 == 0 else wgaps[len(wgaps) // 2]
    heard_by = {x["fromCall"] for x in rx}
    oneway = sorted((x for x in tx if x["toCall"] not in heard_by and x["samples"] >= 20), key=lambda x: -x["snrAvg"])
    txp = mc["tx_power"]

    cf = by[(ZG, "HB9CF-40")], by[("HB9CF-40", ZG)]
    prev = zrep["prev_hops"]
    lh = lg["lasthop"]

    # ---- explainer drawing ----
    def box(x: float, y: float, w: float, h: float, cls: str, lines: list[str], extra: str = "") -> str:
        o = [f'<rect class="d-box {cls} {extra}" x="{x}" y="{y}" width="{w}" height="{h}" rx="6"/>']
        y0 = y + h / 2 - (len(lines) - 1) * 8 + 5
        for i, t in enumerate(lines):
            o.append(f'<text class="d-txt{"" if i == 0 else "-s"}" x="{x + w / 2}" y="{y0 + i * 16:.1f}" '
                     f'text-anchor="middle">{esc(t)}</text>')
        return "".join(o)

    svg = []
    svg.append('<polygon class="d-neu" points="250,150 320,58 390,150"/>')
    svg.append(box(258, 18, 124, 40, "d-watch", [ZG, f"{zgn['alt']} m"]))
    svg.append(box(14, 150, 150, 46, "d-ok", ["HB9CF-40", f"{de(km(zgn, nodes['HB9CF-40']), 0)} km, Tal"]))
    svg.append(box(476, 150, 150, 46, "d-ok", ["HB9FRR-12", f"{de(km(zgn, nodes['HB9FRR-12']), 0)} km, Tal"]))
    # out: strong (solid, flowing)
    svg.append('<path class="d-line d-ok-s anim-flow" d="M258,46 C180,60 120,100 100,148" style="stroke-width:3"/>')
    svg.append('<path class="d-line d-ok-s anim-flow" d="M382,46 C460,60 520,100 540,148" style="stroke-width:3"/>')
    svg.append(f'<text class="d-txt-s" x="20" y="40">sendet: {de(cf[0]["snrAvg"])} dB</text>')
    svg.append(f'<text class="d-txt-s" x="620" y="40" text-anchor="end">sendet: {de(by[(ZG, "HB9FRR-12")]["snrAvg"])} dB</text>')
    # in: weak (thin, fading)
    svg.append('<path class="d-line d-act-s anim-flow anim-drop" d="M150,150 C190,120 240,92 262,62" style="stroke-width:1.5"/>')
    svg.append('<path class="d-line d-act-s anim-flow anim-drop" d="M490,150 C450,120 400,92 378,62" style="stroke-width:1.5"/>')
    svg.append(f'<text class="d-txt-s" x="172" y="186">hört: {de(cf[1]["snrAvg"])} dB</text>')
    svg.append(f'<text class="d-txt-s" x="468" y="186" text-anchor="end">hört: {de(by[("HB9FRR-12", ZG)]["snrAvg"])} dB</text>')
    svg.append(f'<text class="d-txt" x="320" y="214" text-anchor="middle">gleiche Strecke, '
               f'{de(gmin, 0)} bis {de(gmax, 0)} dB Unterschied je Richtung</text>')
    fig1 = k.explain("".join(svg), "Abb. 1: Raus kommt HB9ZG-49 gut, rein kommt fast nichts an. Grün: was die "
                     "Talstationen von ihm hören (SNR). Rot: was er von ihnen hört.", height=230)

    # ---- body ----
    B: list[str] = []
    B.append('<p class="eyebrow">MeshCom Log-Auswertung · HB9-Netz · Einzelfall</p>')
    B.append(f"<h1>{esc(ZG)}: wird gut gehört, hört selbst schlecht</h1>")
    B.append(f'<p class="meta">Quellen: mcmap (Funkstrecken der letzten 7 Tage, Abfrage 01.11.2026) '
             f'und der 24-h-Mitschnitt von HB9VQQ-4 ({esc(lg["window"][0][:16])} bis {esc(lg["window"][1][:16])}).</p>')
    B.append(
        '<div class="callout act"><p><strong>Ja: HB9ZG-49 wird viel besser gehört, als er selbst hört.</strong> '
        f"{len(tx)} Stationen hören ihn, er hört nur {len(rx)}. Auf denselben Strecken kommt er beim Partner "
        f"{de(gmin, 0)} bis {de(gmax, 0)} dB besser an als umgekehrt. Vergleichbare Strecken ohne ihn liegen bei "
        f"0 bis {de(max(wgaps), 0)} dB.</p>"
        "<p><strong>Ob er schlecht hört oder nur sehr laut sendet, zeigen die Daten nicht.</strong> Beides sieht "
        "von außen gleich aus. Eine Endstufe (PA) mit 1 bis 2 W würde den Unterschied fast vollständig erklären. "
        "Ein Verlust im Empfangsweg (Kabel, Stecker, Filter, Board) ebenso. Klarheit bringt eine Frage an den Sysop "
        "und seine <code>--info</code>-Ausgabe.</p></div>")
    B.append(table_answers(k, ZG, tx, rx, gmin, gmax, max(wgaps)))
    B.append(fig1)

    B.append("<h2>Steckbrief</h2>")
    B.append(k.table(["Feld", "Wert"], [
        ["Rufzeichen", ZG],
        ["Hardware", f"{zgn['hw']} (SX1262)"],
        ["Firmware", f"{zgn['fw']} (laut mcmap)"],
        ["Standort", f"{de(zgn['lat'], 4)} N, {de(zgn['lon'], 4)} E, {zgn['alt']} m"],
        ["Beschreibung", zgn["desc"]],
        ["Internet-Gateway", "nein (reiner Digipeater)"],
        ["Nachbarzahl, die er selbst meldet", str(zgn["nbcnt"])],
    ]))

    B.append(f"<h2>1. Nicht stumm: {len(tx)} Stationen hören ihn</h2>")
    B.append("<ul>"
             f"<li><strong>{len(tx)} Empfänger in 7 Tagen</strong>, davon {len(tx_act)} aktuell aktiv.</li>"
             f"<li><strong>Bis {de(km(zgn, nodes['HB9VQQ-2']), 0)} km weit:</strong> HB9VQQ-2 hört ihn {by[(ZG, 'HB9VQQ-2')]['samples']}-mal "
             f"mit {de(by[(ZG, 'HB9VQQ-2')]['rssiAvg'], 0)} dBm.</li>"
             f"<li><strong>Talstationen hören ihn kräftig:</strong> HB9CF-40 mit {de(cf[0]['rssiAvg'], 0)} dBm und "
             f"{de(cf[0]['snrAvg'])} dB Rauschabstand, HB9FRR-12 mit {de(by[(ZG, 'HB9FRR-12')]['rssiAvg'], 0)} dBm und "
             f"{de(by[(ZG, 'HB9FRR-12')]['snrAvg'])} dB.</li></ul>")
    B.append(k.bars([("hören HB9ZG-49 (7 Tage)", len(tx)), ("davon aktiv", len(tx_act)),
                     ("von HB9ZG-49 gehört (7 Tage)", len(rx)), ("davon aktiv", len(rx_act))],
                    f"Abb. 2: {len(tx)} Stationen hören HB9ZG-49, er selbst hört {len(rx)} (Funkstrecken laut mcmap, 7 Tage).",
                    unit=" Stationen"))

    B.append("<h2>2. Was er hört, kommt am Limit an</h2>")
    B.append("<ul>"
             f"<li><strong>{len(rx_floor)} von {len(rx)} Empfangsstrecken</strong> zeigen im Mittel −127 dBm, "
             "den tiefsten Wert, den der Empfängerchip ausgeben kann. Das allein beweist nichts: auch gesunde "
             "Heltec V3 zeigen bei so schwachen Signalen \u2212125 bis \u2212127 dBm (HB9CF-44 und HB9CF-46 hören "
             "HB9ZG-49 selbst mit \u2212125 dBm).</li>"
             f"<li><strong>Rauschabstand am Limit:</strong> im Mittel {de(cf[1]['snrAvg'])} dB von HB9CF-40 und "
             f"{de(by[('HB9FRR-12', ZG)]['snrAvg'])} dB von HB9FRR-12. Schon wenige dB weniger und der Rahmen ist verloren.</li>"
             f"<li><strong>Einzige kräftige Strecke:</strong> HB9WAD-51 (Knonau, {de(km(zgn, nodes['HB9WAD-51']), 0)} km) mit "
             f"{de(by[('HB9WAD-51', ZG)]['rssiAvg'], 0)} dBm, aber nur {by[('HB9WAD-51', ZG)]['samples']} Meldungen in 7 Tagen. "
             "Der Empfänger kann also höhere Werte anzeigen; es kommt nur fast nie so viel an.</li></ul>")
    rows = []
    for p, h, z, gap in pairs:
        d = f"{de(km(zgn, nodes[p]), 0)} km" if p in nodes else "–"
        thin = " (wenig Daten)" if min(h["samples"], z["samples"]) < 20 else ""
        rows.append([p, d, f"{de(h['rssiAvg'], 0)} dBm / {de(h['snrAvg'])} dB ({h['samples']})",
                     f"{de(z['rssiAvg'], 0)} dBm / {de(z['snrAvg'])} dB ({z['samples']})", f"{de(gap, 0)} dB{thin}"])
    for name, f, r, gap in ctrl:
        rows.append([f"Kontrolle: {name}", "–", f"{de(f['rssiAvg'], 0)} dBm / {de(f['snrAvg'])} dB ({f['samples']})",
                     f"{de(r['rssiAvg'], 0)} dBm / {de(r['snrAvg'])} dB ({r['samples']})", f"{de(gap, 0)} dB"])
    B.append(k.table(["Partner", "Entfernung", "Richtung 1: Partner hört ZG (Pegel / SNR, Meldungen)",
                      "Richtung 2: ZG hört Partner", "Unterschied SNR"], rows, num_cols=(4,),
                     caption="Tabelle 1: Dieselbe Strecke in beiden Richtungen (mcmap, 7 Tage; Kontrollpaare 24 h). "
                             "Ein Messwert gehört immer dem Empfänger. Kontrollpaare: Richtung 1 = rechte Station hört "
                             "linke. Pegel verschiedener Chips sind nicht direkt vergleichbar, der Rauschabstand schon."))
    B.append("<p><strong>Einschränkung:</strong> Die Sendeleistung der Partner ist nicht bekannt. Ein Unterschied in "
             "der Tabelle kann auch heißen, dass HB9ZG-49 lauter sendet als der Partner. Abschnitt 3 vergleicht deshalb "
             "mit Strecken ohne ihn und fragt: taub oder laut?</p>")

    B.append("<h2>3. Gegenprobe: Ist das normal?</h2>")
    B.append("<ul>"
             f"<li><strong>{len(wctl)} schwache Vergleichsstrecken ohne HB9ZG-49</strong> sind {de(min(wgaps))} bis "
             f"{de(max(wgaps))} dB unsymmetrisch, im Median {de(wmed)} dB. Keine erreicht 10 dB.</li>"
             f"<li><strong>{len(oneway)} weitere Stationen hören ihn, er hört sie nie</strong> (Tabelle 2). Sie "
             f"hören ihn mit {de(max(x['snrAvg'] for x in oneway), 0)} bis {de(min(x['snrAvg'] for x in oneway), 0)} dB. "
             "Wäre die Strecke symmetrisch, käme ihr Signal bei ihm ähnlich an und wäre noch dekodierbar. "
             f"Mit {de(gmin, 0)} bis {de(gmax, 0)} dB weniger liegt es unter der Grenze.</li>"
             f"<li><strong>Der Empfänger ist nicht tot:</strong> HB9WAD-51 hört er mit {de(by[('HB9WAD-51', ZG)]['rssiAvg'], 0)} dBm "
             f"und {de(by[('HB9WAD-51', ZG)]['snrAvg'])} dB.</li></ul>")
    B.append(k.bars([(f"{x} \u2194 {y}", g) for x, y, g in sorted(wctl, key=lambda t: -t[2])]
                    + [(f"{ZG} \u2194 {p}", round(g, 1)) for p, _, _, g in big],
                    f"Abb. 3: Unsymmetrie im Rauschabstand. Oben {len(wctl)} Strecken ohne HB9ZG-49 (höchstens "
                    f"{de(max(wgaps), 0)} dB), unten die Strecken mit ihm ({de(gmin, 0)} bis {de(gmax, 0)} dB).",
                    unit=" dB", color="var(--c3)"))
    B.append(k.table(["Station", "Entfernung", "hört HB9ZG-49 (Pegel / SNR)", "Meldungen", "HB9ZG-49 hört sie"],
                     [[x["toCall"], f"{de(km(zgn, nodes[x['toCall']]), 0)} km" if x["toCall"] in nodes else "\u2013",
                       f"{de(x['rssiAvg'], 0)} dBm / {de(x['snrAvg'])} dB", x["samples"], "nie"] for x in oneway],
                     num_cols=(3,), caption="Tabelle 2: Einseitige Strecken (mcmap, 7 Tage, mindestens 20 Meldungen)."))
    B.append("<h3>Taub oder laut?</h3>")
    B.append("<p>Die Unsymmetrie misst <em>Lautstärke minus Gehör</em>. Woher sie kommt, lässt sich von außen nicht trennen:</p>")
    B.append(k.table(["Erklärung", "Passt zu den Zahlen?", "Woran man es erkennt"], [
        ["<strong>Endstufe (PA)</strong> hinter dem Heltec", "Ja. 1 W sind 8 dB, 2 W 11 dB mehr als die 22 dBm des "
         "Heltec. Dazu 1 bis 4 dB normale Streuung, das ergibt 12 bis 15 dB.", "Sysop fragen. Dann hört er normal, "
         "sendet aber viel weiter, als er hören kann."],
        ["<strong>Verlust im Empfangsweg</strong> (Kabel, Stecker, Filter, Board)", "Ja, wenn Senden davon nicht "
         "betroffen ist, z. B. eine PA mit verlustreichem Empfangszweig oder ein beschädigter Eingang.",
         "Anderes Heltec V3 an dieselbe Antenne; Empfangszweig der PA prüfen."],
        ["<strong>Alle Partner senden leiser</strong>", f"Unwahrscheinlich. Heltec V3 sendet ab Werk mit "
         f"{txp['heltec_v3_default_dbm']} dBm (Höchstwert {txp['heltec_v3_max_dbm']}); die Lücke zeigt sich bei rund 13 "
         "Partnern verschiedener Betreiber gleich.", "\u2013"],
        ["<strong>Firmware-Einstellung</strong>", "Nur teilweise. <code>--setboostedgain</code> ändert die "
         "Empfängerverstärkung je Knoten, laut Datenblatt aber nur um wenige dB.", "<code>--info</code>: RXBOOST"],
    ], raw=True))
    B.append("<h2>4. Was der Mitschnitt von HB9VQQ-4 zeigt</h2>")
    B.append("<ul>"
             f"<li><strong>{lh['unique_frames']} Rahmen</strong> von {esc(ZG)} hat HB9VQQ-4 direkt gehört, mit "
             f"{de(lh['rssi_median'], 0)} dBm im Median ({lh['kind'].get('relais', 0)} weitergeleitete, "
             f"{lh['kind'].get('eigen', 0)} eigene).</li>"
             f"<li><strong>Nur wenige Quellen:</strong> in {zrep['n']} HEY-Baken hat er "
             + ", ".join(f"{esc(c)} ({n})" for c, n in prev) + " gehört.</li>"
             f"<li><strong>Kein einziger Rahmen von HB9VQQ-2</strong> ({lg['frames_zg_relayed_after_vqq2']}), obwohl "
             f"HB9VQQ-2 ihn {by[(ZG, 'HB9VQQ-2')]['samples']}-mal gehört hat. Bei {de(km(zgn, nodes['HB9VQQ-2']), 0)} km "
             "und dem schwachen Empfang ist das zu erwarten.</li>"
             f"<li><strong>Nachbarzahl {zrep['ncnt_median']:.0f}</strong> im Median, die HB9ZG-49 selbst meldet; "
             f"HB9VQQ-2 meldet {lg['relays']['HB9VQQ-2']['ncnt_median']:.0f}.</li></ul>")

    B.append("<h2>5. Bewertung</h2>")
    B.append(k.table(["Frage", "Antwort", "Status"], [
        ["Sendet HB9ZG-49 normal?", f"Ja, eher kräftig: {len(tx)} Empfänger, bis {de(km(zgn, nodes['HB9VQQ-2']), 0)} km.", chip("ok")],
        ["Hört er im Verhältnis dazu?", f"Nein: {de(gmin, 0)} bis {de(gmax, 0)} dB Lücke, normal sind 0 bis "
                                         f"{de(max(wgaps), 0)} dB.", chip("act")],
        ["Taub oder laut?", "Offen. PA oder Verlust im Empfangsweg, die Daten trennen es nicht.", chip("watch")],
        ["Seit wann?", "Mindestens seit 25.10.2026 (Beginn der 7-Tage-Historie), über die Woche flach.", chip("watch")],
        ["Firmware-Fehler?", "Kein Hinweis. HB9CF-40 hat dieselbe Hardware und Firmware und keine solche Lücke.", chip("ok")],
    ], raw=True))
    B.append("<p><strong>Für das Netz ist es gleich, welche Erklärung stimmt:</strong> Er ist auf vielen Strecken "
             "zu hören, kann dort aber nichts aufnehmen und weiterleiten.</p>")

    B.append("<h2>6. Auswirkung auf das Netz</h2>")
    B.append("<ul>"
             "<li><strong>Bester Standort, wenig Relaisleistung:</strong> auf 1703 m könnte er weit verbinden, leitet aber "
             "nur weiter, was er von wenigen Stationen gerade noch hört.</li>"
             f"<li><strong>Einseitige Strecken:</strong> {len(tx_act)} Stationen hören ihn und führen ihn als Nachbarn. "
             "Das Netz sieht dadurch besser abgedeckt aus, als es ist.</li></ul>")

    B.append("<h2>7. Fragen an den Sysop (HB9JAY)</h2><ol>"
             "<li><strong>Ist eine Endstufe (PA) eingebaut?</strong> Wenn ja: welche Leistung, und hat sie einen "
             "eigenen Empfangsverstärker oder nur einen Durchgang?</li>"
             "<li><strong><code>--info</code>-Ausgabe:</strong> Sendeleistung (TXPWR) und RXBOOST.</li>"
             "<li>Filter oder Duplexer in der Leitung? Antenne, Kabel und Stecker, wann zuletzt geprüft?</li>"
             "<li>Wenn eine PA drin ist: bei geringerer Leistung passt die Reichweite zum Gehör, und das Netz "
             "plant nicht mit Strecken, die nur in eine Richtung gehen.</li></ol>")

    B.append("<details><summary>Methode und Datenquellen</summary><ul>"
             f"<li>mcmap: {esc(mc['source'])}. Historie ab {esc(mc['history_from'])}. Werte sind HEY-Meldungen, "
             "die über Gateways beim Server ankommen, nicht jeder Rahmen.</li>"
             "<li>Mitschnitt: HB9VQQ-4, 24 h, nur [LOG]-Zeilen mit gültiger Prüfsumme; je Nachrichten-ID und Station "
             "einmal gezählt. HB9VQQ-5 hat keine [LOG]-Zeilen.</li>"
             "<li>Geprüft und verworfen: der Absolutpegel als Beweis. Auch gesunde Heltec V3 zeigen bei Rauschabständen "
             "um \u221215 dB \u2212125 bis \u2212127 dBm; \u2212127 ist der Boden der Skala (RadioLib: Registerwert/2, "
             "abgeschnitten).</li>"
             f"<li>Vergleichsstrecken: {esc(mc['control_pairs_weak']['source'])}.</li>"
             f"<li>Sendeleistung Heltec V3: {esc(txp['source'])}.</li>"
             "<li>HEY-Signalbericht: je Relais die Gruppe Nachbarzahl, Pegel, Rauschabstand "
             "(src/aprs_functions.cpp, appendHeySignalReport). Die Firmware rundet oder begrenzt den Pegel nicht.</li>"
             "<li>Daten: data/hb9/extra/hb9zg49_log.json (generators/lib/hb9zg49_log.py), "
             "data/hb9/extra/hb9zg49_mcmap.json.</li></ul></details>")
    B.append('<p class="foot">Erstellt 01.11.2026 aus Messdaten; Aussagen unabhängig gegengeprüft; eine (Absolutpegel) wurde dabei verworfen. '
             "Vermutungen sind als solche markiert.</p>")
    a.out.write_text(k.page(f"{ZG}: wird gut gehört, hört selbst schlecht", "\n".join(B)))
    print(f"wrote {a.out}")
    return 0


def table_answers(k, zg: str, tx: list, rx: list, gmin: float, gmax: float, norm_max: float) -> str:
    return k.table(["Frage", "Kurzantwort", "Status"], [
        [f"Ist {zg} stumm?", f"Nein. {len(tx)} Stationen hören ihn.", k.chip("ok")],
        ["Hört er schlecht?", f"Im Verhältnis zu seiner Lautstärke ja: er hört {len(rx)} Stationen, "
                               f"{de(gmin, 0)} bis {de(gmax, 0)} dB schlechter als umgekehrt (normal: bis {de(norm_max, 0)} dB).",
         k.chip("act")],
        ["Kann es eine PA sein?", "Ja, das passt zu den Zahlen. Ebenso ein Verlust im Empfangsweg. Sysop fragen.",
         k.chip("watch")],
    ], raw=True)


if __name__ == "__main__":
    raise SystemExit(main())
