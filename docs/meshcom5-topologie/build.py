#!/usr/bin/env python3
"""Generator fuer docs/meshcom5-topologie/meshcom5-topologie.html.

Setzt die Textteile aus body/*.html (in Namensreihenfolge) zusammen, bettet style.css,
die Abbildungen aus figures/ und die gerechneten Tabellen ein und prueft jede Zahl gegen
die gemessenen Werte. Aufruf: python3 docs/meshcom5-topologie/build.py

Platzhalter im Text: @@NAME@@ fuer Zahlen, @@FIG:name@@ fuer eine Abbildungsdatei aus
figures/, @@GEN:name@@ fuer eine hier erzeugte Abbildung oder Tabelle, @@FILE:pfad@@ fuer
einen HTML-Block aus data/.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'meshcom5-topologie.html')


def rd(*p):
    return open(os.path.join(HERE, *p), encoding='utf-8').read()


def de(n):
    s = f"{abs(n):,}".replace(',', '.')
    return ('-' if n < 0 else '') + s


def kb(n):
    return f"{n / 1000:.1f}".replace('.', ',')


# ------------------------------------------------------------------ Messwerte heute
# nm -S auf firmware.elf, Builds 2026-09-24 auf feature-neighbour-matrix d9fc5c5a.
# MHeard: 55 B je Eintrag (8 Felder = 54 B plus mheard_send_idx 1 B) + 6 B Verwaltung.
# Pfadtabelle: 71 B je Eintrag + 1 B. nRF52: drei statische struct mheardLine zu 584 B.
FAM = {
    'klassisch': dict(env='heltec_wifi_lora_32_V2', N_mh=30, N_path=40, nbr=1332, stat=0, rows_old=13),
    'E22_XML':   dict(env='E22_XML-DevKitC',        N_mh=50, N_path=50, nbr=3156, stat=0, rows_old=21),
    'S3':        dict(env='heltec_wifi_lora_32_V3', N_mh=80, N_path=100, nbr=3156, stat=0, rows_old=21),
    'nRF52':     dict(env='wiscore_rak4631',        N_mh=80, N_path=100, nbr=3156, stat=1752, rows_old=21),
}
MEAS_NM = {'klassisch': (1656, 2841), 'E22_XML': (2756, 3551), 'S3': (4406, 7101), 'nRF52': (4406, 7101)}
for k, f in FAM.items():
    f['mh'] = 55 * f['N_mh'] + 6
    f['path'] = 71 * f['N_path'] + 1
    assert (f['mh'], f['path']) == MEAS_NM[k], k
RINGS_OLD = 160

# ------------------------------------------------------------------ Zielgroessen
SIZE = {
    'klassisch': dict(R=64,  W=8,  E=256, X=48, H=48),
    'E22_XML':   dict(R=64,  W=8,  E=256, X=48, H=48),
    'S3':        dict(R=128, W=16, E=512, X=64, H=112),
    'nRF52':     dict(R=128, W=16, E=512, X=64, H=112),
}
EXT_BITS = [('Sekunde', 6), ('PLT', 2), ('MOD', 8), ('RSSI', 8), ('LAT', 21), ('LON', 22), ('ALT', 16),
            ('PL', 4), ('MESH', 1), ('f', 4), ('s', 4), ('FW', 5), ('W0', 1), ('SYM', 1)]
EXT_BITCOUNT = sum(b for _, b in EXT_BITS)
assert EXT_BITCOUNT == 103
EXT_B = 13
CALL_B, CORE_B, EDGE_B, HZ_META = 8, 12, 6, 4


def newsize(s):
    R, W, E, X, H = s['R'], s['W'], s['E'], s['X'], s['H']
    d = dict(calls=CALL_B * R, core=CORE_B * R, masks=2 * W * R, edges=EDGE_B * E, ext=EXT_B * X,
             hz=(CALL_B + W + HZ_META) * H,
             hdr=2 + 2 + 4 + W + W + 4,          # boot_min, last_sweep, Bootepoche, D60, Via-Maske, Via-Zustand
             echo=4 * (4 + W + W + 2))           # 4 eigene Rahmen: msg_id, Maske erste Hand, zweite Hand, Minute
    d['total'] = sum(d.values())
    d['rings'] = 2 * 20 * W
    return d


NEW = {k: newsize(s) for k, s in SIZE.items()}
NET = {}
for k, f in FAM.items():
    old = f['mh'] + f['path'] + f['nbr'] + f['stat']
    NET[k] = dict(old=old, new=NEW[k]['total'], old_all=old + RINGS_OLD, new_all=NEW[k]['total'] + NEW[k]['rings'])
    NET[k]['net'] = NET[k]['new_all'] - NET[k]['old_all']
    assert NET[k]['net'] < 0, (k, NET[k])
assert NET['klassisch']['old'] == 5829 and NET['S3']['old'] == 14663 and NET['nRF52']['old'] == 16415
AOS_ROW = 40   # struct mit call[10] + 6 kleinen Feldern + 2 x uint64_t, gemessen xtensa und arm
# 128 Zeilen auch klassisch, mit den S3-Groessen (Befund 16 der Pruefung: 7,9 kB, nicht 6,2 kB)
ALL128 = newsize(dict(R=128, W=16, E=512, X=64, H=112))
ALL128_DELTA = ALL128['total'] + ALL128['rings'] - NET['klassisch']['old_all']

# ------------------------------------------------------------------ Abbildungen, generiert


def svg_layout():
    o = []

    def bar(x, y, w, h, label, cls='node'):
        o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" class="{cls}" style="rx:2"/>')
        o.append(f'<text x="{x + w / 2:.1f}" y="{y + h / 2 + 4:.1f}" text-anchor="middle" class="mono">{label}</text>')
    y = 30
    o.append(f'<text x="40" y="{y}" class="b">Rufzeichen: ein 64-Bit-Wort, 6 Bit je Zeichen, Code 0 beendet</text>')
    chars = ['D', 'K', '5', 'E', 'N', '-', '9', '8', 'Ende']

    def code(c):
        if c == 'Ende':
            return 0
        if c.isdigit():
            return 1 + int(c)
        if c == '-':
            return 37
        return 11 + ord(c) - ord('A')
    x = 40
    for c in chars:
        bar(x, y + 10, 60, 26, c, 'node srv')
        o.append(f'<text x="{x + 30}" y="{y + 50}" text-anchor="middle" class="t11">{code(c)}</text>')
        x += 60
    bar(x, y + 10, 100, 26, '10 Bit frei', 'node')
    o.append(f'<text x="{x + 110}" y="{y + 28}" class="t11">Vergleich zweier Rufzeichen = ein Wortvergleich</text>')
    y = 110
    o.append(f'<text x="40" y="{y}" class="b">Zeilenkern: 12 Byte, fuer jede Zeile</text>')
    x = 40
    for lab, b in [('lat16', 2), ('lon16', 2), ('last_min', 2), ('rpt_min', 2), ('flags', 1), ('hw', 1), ('ncnt', 1), ('ext', 1)]:
        bar(x, y + 10, b * 60, 26, lab)
        x += b * 60
    o.append(f'<text x="40" y="{y + 52}" class="t11">ext: Nummer des Slots der Direkt-Erweiterung, 0xFF = keiner; ncnt: zuletzt gemeldete Nachbarzahl der Station</text>')
    y = 196
    o.append(f'<text x="40" y="{y}" class="b">Masken je Zeile: eigene Felder, 64 Bit klassisch, 128 Bit S3 und nRF52</text>')
    bar(40, y + 10, 260, 26, 'hears[R]: wen X hoert', 'node srv')
    bar(300, y + 10, 260, 26, 'heardBy[R]: wer X hoert', 'node srv')
    o.append(f'<text x="570" y="{y + 28}" class="t11">getrennt vom Kern, sonst 4 Byte Fuellung je Zeile</text>')
    y = 266
    o.append(f'<text x="40" y="{y}" class="b">Kante: 6 Byte, "y hat x gehoert"</text>')
    x = 40
    for lab, b in [('x', 1), ('y', 1), ('cnt', 1), ('snr', 1), ('last_min', 2)]:
        bar(x, y + 10, b * 60, 26, lab)
        x += b * 60
    o.append(f'<text x="{x + 10}" y="{y + 28}" class="t11">cnt halbiert alle 90 min; snr der Kante (x, ich) gleitend ueber 8 Rahmen</text>')
    y = 336
    o.append(f'<text x="40" y="{y}" class="b">Direkt-Erweiterung: {EXT_BITCOUNT} Bit in {EXT_B} Byte, nur fuer direkt gehoerte Zeilen</text>')
    x = 40
    scale = 8
    for lab, b in EXT_BITS:
        w = b * scale
        o.append(f'<rect x="{x}" y="{y + 22}" width="{w}" height="26" class="node prx" style="rx:1"/>')
        o.append(f'<text x="{x + w / 2:.1f}" y="{y + 39}" text-anchor="middle" class="mono">{b}</text>')
        short = {'MESH': 'M', 'Sekunde': 'sec', 'W0': 'W', 'SYM': 'Y'}.get(lab, lab)
        ly = y + 62 if lab == 'SYM' else y + 17   # zwei 1-Bit-Felder nebeneinander: eines unter den Balken
        o.append(f'<text x="{x + w / 2:.1f}" y="{ly}" text-anchor="middle" class="t11">{short}</text>')
        x += w
    assert x == 40 + EXT_BITCOUNT * scale
    y = 426
    o.append(f'<text x="40" y="{y}" class="b">Horizont-Eintrag: 20 Byte klassisch, 28 Byte auf S3 und nRF52</text>')
    bar(40, y + 10, 240, 26, 'Rufzeichen 8 B', 'node hamf ham')
    bar(280, y + 10, 240, 26, 'Eintrittsmaske 8 / 16 B', 'node hamf ham')
    bar(520, y + 10, 120, 26, 'Meta 4 B', 'node hamf ham')
    o.append(f'<text x="650" y="{y + 28}" class="t11">Meta: letzte Minute, Hop-Minimum je 6-h-Epoche x 2, G-Bit</text>')
    return '<svg viewBox="0 0 960 476" role="img" aria-label="Speicherlayout">\n' + '\n'.join(o) + '\n</svg>'


def svg_ram():
    scale = 0.031
    x0 = 170
    out = []
    y = 40
    c_old = [('MHeard', '#f6c9a0'), ('Pfadtabelle', '#e7c1e6'), ('Matrix', '#c9dcf5'), ('Ringe', '#dde3ea'), ('mheardLine-Statics', '#f3b4b4')]
    c_new = [('Rufzeichen + Kern', '#b9d7c4'), ('Masken', '#9cc9ab'), ('Kanten', '#c9dcf5'), ('Direkt-Erweiterung', '#f6e3a0'), ('Horizont + Kopf + Echo', '#e7c1e6'), ('Ringe', '#dde3ea')]
    for k in ['klassisch', 'E22_XML', 'S3', 'nRF52']:
        f = FAM[k]
        n = NEW[k]
        out.append(f'<text x="20" y="{y + 14}" class="b">{k}</text>')
        out.append(f'<text x="20" y="{y + 38}" class="t11">{f["env"]}</text>')
        segs = [f['mh'], f['path'], f['nbr'], RINGS_OLD, f['stat']]
        x = x0
        out.append(f'<text x="{x0 - 8}" y="{y + 14}" text-anchor="end" class="t11">heute</text>')
        for (lab, col), v in zip(c_old, segs):
            if v <= 0:
                continue
            w = v * scale
            out.append(f'<rect x="{x:.1f}" y="{y + 2}" width="{w:.1f}" height="16" style="fill:{col};stroke:#fff"/>')
            x += w
        out.append(f'<text x="{x + 6:.1f}" y="{y + 14}" class="mono">{de(sum(segs))} B</text>')
        y2 = y + 24
        segs = [n['calls'] + n['core'], n['masks'], n['edges'], n['ext'], n['hz'] + n['hdr'] + n['echo'], n['rings']]
        x = x0
        out.append(f'<text x="{x0 - 8}" y="{y2 + 14}" text-anchor="end" class="t11">neu</text>')
        for (lab, col), v in zip(c_new, segs):
            w = v * scale
            out.append(f'<rect x="{x:.1f}" y="{y2 + 2}" width="{w:.1f}" height="16" style="fill:{col};stroke:#fff"/>')
            x += w
        out.append(f'<text x="{x + 6:.1f}" y="{y2 + 14}" class="mono">{de(sum(segs))} B, netto {de(NET[k]["net"])} B, {SIZE[k]["R"]} statt {f["rows_old"]} Zeilen</text>')
        y += 66
    ly = y + 6
    out.append(f'<text x="20" y="{ly + 12}" class="t11 b">heute:</text>')
    lx = 70
    for lab, col in c_old:
        out.append(f'<rect x="{lx}" y="{ly + 2}" width="14" height="12" style="fill:{col}"/>')
        out.append(f'<text x="{lx + 18}" y="{ly + 12}" class="t11">{lab}</text>')
        lx += 18 + len(lab) * 6.2 + 18
    ly += 22
    out.append(f'<text x="20" y="{ly + 12}" class="t11 b">neu:</text>')
    lx = 70
    for lab, col in c_new:
        out.append(f'<rect x="{lx}" y="{ly + 2}" width="14" height="12" style="fill:{col}"/>')
        out.append(f'<text x="{lx + 18}" y="{ly + 12}" class="t11">{lab}</text>')
        lx += 18 + len(lab) * 6.2 + 18
    return f'<svg viewBox="0 0 960 {ly + 30}" role="img" aria-label="RAM je Familie">\n' + '\n'.join(out) + '\n</svg>'


SERIES = json.load(open(os.path.join(HERE, 'data', 'coverage-rules-series.json')))


def svg_coverage():
    # Stufenlinie #X von DB0ED-99 ueber 243 Schnappschuesse, heutige Regel gegen Anteilsregel.
    n = len(SERIES)
    x0, x1, y_top = 90, 930, 40
    h = 70
    out = []
    lanes = [('DB0ED-99|R12', 'heute: ein Treffer in 12 h deckt', '#c62828'),
             ('DB0ED-99|SHARE', 'Anteilsregel: mindestens 10 % der Treffer', '#2a9d5c')]
    maxv = 5
    for li, (key, label, col) in enumerate(lanes):
        base = y_top + li * (h + 50) + h
        zeros = sum(1 for s in SERIES if (s.get(key) or 0) == 0)
        out.append(f'<text x="{x0}" y="{base - h - 8}" class="b">{label}</text>')
        out.append(f'<text x="{x0 + 330}" y="{base - h - 8}" class="t11">#X = 0 in {zeros} von {n} Schnappschuessen</text>')
        out.append(f'<path d="M{x0} {base} L{x1} {base}" class="ln"/>')
        for v in range(0, maxv + 1):
            yy = base - v * h / maxv
            if v in (0, maxv):
                out.append(f'<text x="{x0 - 8}" y="{yy + 4:.1f}" text-anchor="end" class="t11">{v}</text>')
        pts = []
        for i, s in enumerate(SERIES):
            v = s.get(key)
            v = 0 if v is None else min(v, maxv)
            x = x0 + (x1 - x0) * i / (n - 1)
            yy = base - v * h / maxv
            if pts:
                pts.append(f'L{x:.1f} {pts_last_y:.1f}')
            pts.append(f'L{x:.1f} {yy:.1f}' if pts else f'M{x:.1f} {yy:.1f}')
            pts_last_y = yy
        out.append(f'<path d="{" ".join(pts)}" style="fill:none;stroke:{col};stroke-width:1.6"/>')
    # Neustarts markieren (up springt zurueck)
    ybot = y_top + 2 * (h + 50)
    prev = None
    for i, s in enumerate(SERIES):
        if prev is not None and s['up'] + 5 < prev:
            x = x0 + (x1 - x0) * i / (n - 1)
            for li in range(len(lanes)):
                base = y_top + li * (h + 50) + h
                out.append(f'<path d="M{x:.1f} {base - h} L{x:.1f} {base}" class="lnd"/>')
        prev = s['up']
    out.append(f'<text x="{x0}" y="{ybot - 8}" class="t11">{SERIES[0]["t"]}</text>')
    out.append(f'<text x="{x1}" y="{ybot - 8}" text-anchor="end" class="t11">{SERIES[-1]["t"]}</text>')
    out.append(f'<text x="{(x0 + x1) / 2:.0f}" y="{ybot - 8}" text-anchor="middle" class="t11">gestrichelt: Neustart von DK5EN-98 (Flash)</text>')
    return f'<svg viewBox="0 0 960 {ybot + 4}" role="img" aria-label="Exklusiver Beitrag DB0ED-99">\n' + '\n'.join(out) + '\n</svg>'


# ------------------------------------------------------------------ RAM je Umgebung
ENV_FAM = {
    'E22_1262-DevKitC': 'klassisch', 'E22-DevKitC': 'klassisch', 'E22_XML-DevKitC': 'E22_XML',
    'E22_1268_S3-DevKitC-1-N16R8': 'S3', 'E22_1262_S3-DevKitC-1-N16R8': 'S3', 'heltec_wifi_lora_32_V2': 'klassisch',
    'heltec_wifi_lora_32_V3': 'S3', 'heltec_wifi_lora_32_V4': 'S3', 'heltec_wireless_stick': 'S3',
    'heltec_wireless_tracker': 'S3', 'heltec_t114': 'nRF52', 'vision-master-e290': 'S3', 'vision-master-e213': 'S3',
    'wireless-paper': 'S3', 'T-ETH-ELITE_1262': 'S3', 'LilyGo_T-Beam-1W': 'S3', 'LilyGo_T3_S3_V1_3': 'S3',
    'LilyGo_T_Connect_Pro': 'S3', 'ttgo-lora32-v21': 'klassisch', 'ttgo_tbeam': 'klassisch',
    'ttgo_tbeam_SX1262': 'klassisch', 'ttgo_tbeam_SX1268': 'klassisch', 'ttgo_tbeam_supreme': 'S3', 't_deck': 'S3',
    't_deck_plus': 'S3', 't_deck_pro': 'S3', 't_echo': 'nRF52', 'wiscore_rak4631': 'nRF52',
    'esp32-loraprs-e22': 'klassisch', 'esp32-loraprs-ra01': 'klassisch',
}
RAM = json.load(open(os.path.join(HERE, 'data', 'ram-20260924.json')))


def env_table():
    rows = ['<table class="small"><tr><th>Umgebung</th><th>Familie</th><th class="num">belegt / Segment (B)</th>'
            '<th class="num">frei heute</th><th class="num">netto</th><th class="num">frei danach</th><th>Zeilen heute / neu</th></tr>']
    for env in sorted(ENV_FAM, key=lambda e: (list(FAM).index(ENV_FAM[e]), e)):
        fam = ENV_FAM[env]
        r = RAM.get(env)
        if not r:
            continue
        if 'dram0_0_seg' in r:
            used, seg = r['dram0_0_seg']['used'], r['dram0_0_seg']['length']
        else:
            used, seg = r['ram_used'], r['ram_total']
        free = seg - used
        net = NET[fam]['net']
        rows.append(f'<tr><td>{env}</td><td>{fam}</td><td class="num">{de(used)} / {de(seg)}</td><td class="num">{de(free)}</td>'
                    f'<td class="num">{de(net)}</td><td class="num">{de(free - net)}</td><td>{FAM[fam]["rows_old"]} / {SIZE[fam]["R"]}</td></tr>')
    rows.append('</table>')
    return '\n'.join(rows)


# ------------------------------------------------------------------ Platzhalter
T = {
    'AOS_ROW': str(AOS_ROW), 'EXT_B': str(EXT_B), 'EXT_BITS': str(EXT_BITCOUNT),
    'ALL128': kb(ALL128_DELTA),
}
for k in FAM:
    suf = {'klassisch': 'C', 'E22_XML': 'X', 'S3': 'S', 'nRF52': 'N'}[k]
    T[f'OLD_{suf}'] = de(NET[k]['old'])
    T[f'NEW_{suf}'] = de(NET[k]['new'])
    T[f'OLDALL_{suf}'] = de(NET[k]['old_all'])
    T[f'NEWALL_{suf}'] = de(NET[k]['new_all'])
    T[f'NET_{suf}'] = de(NET[k]['net'])
for k, suf in [('klassisch', 'C'), ('S3', 'S')]:
    for f in ['calls', 'core', 'masks', 'edges', 'ext', 'hz', 'hdr', 'echo', 'rings']:
        T[f'{f.upper()}_{suf}'] = de(NEW[k][f])
T['FILE_S3'] = kb(NEW['S3']['total'] + 32)
T['SHARE_DB0ED_ZERO'] = str(sum(1 for s in SERIES if (s.get('DB0ED-99|SHARE') or 0) == 0))
T['R12_DB0ED_ZERO'] = str(sum(1 for s in SERIES if (s.get('DB0ED-99|R12') or 0) == 0))
T['N_SNAPS'] = str(len(SERIES))

GEN = {'layout': svg_layout(), 'ram': svg_ram(), 'coverage': svg_coverage(), 'envtable': env_table()}

body = ''.join(rd('body', f) for f in sorted(os.listdir(os.path.join(HERE, 'body'))) if f.endswith('.html'))
body = body.replace('@@CSS@@', rd('style.css'))
body = re.sub(r'@@FIG:([a-z0-9-]+)@@', lambda m: rd('figures', m.group(1) + '.svg'), body)
body = re.sub(r'@@GEN:([a-z0-9]+)@@', lambda m: GEN[m.group(1)], body)
body = re.sub(r'@@FILE:([a-z0-9./-]+)@@', lambda m: rd('data', m.group(1)), body)
for t, v in T.items():
    body = body.replace('@@' + t + '@@', v)
left = re.findall(r'@@[A-Za-z_:0-9./-]+@@', body)
assert not left, left
# Abbildungsnummern fortlaufend pruefen
nums = [int(n) for n in re.findall(r'<figcaption>Abb\. (\d+):', body)]
assert nums == list(range(1, len(nums) + 1)), nums
open(OUT, 'w', encoding='utf-8').write(body)
print('written', OUT, len(body), 'figures', len(nums))
print({k: NET[k] for k in NET})
print('all128 delta', ALL128_DELTA)
