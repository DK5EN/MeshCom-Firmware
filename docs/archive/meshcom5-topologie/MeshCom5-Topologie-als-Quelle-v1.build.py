#!/usr/bin/env python3
# Generator fuer ~/Desktop/MeshCom5-Topologie-als-Quelle.html
# Alle Zahlen werden hier gerechnet und gegen die gemessenen Werte geprueft.
import json, os, re

OUT = os.path.expanduser('~/Desktop/MeshCom5-Topologie-als-Quelle.html')
CSS = open(os.path.expanduser('~/.claude/skills/konzeptpapier/assets/style.css')).read()

# ---------------------------------------------------------------- Messwerte
# nm -S auf firmware.elf, Builds 2026-09-24 auf feature-neighbour-matrix d9fc5c5a
MEAS = {
    'klassisch': dict(env='heltec_wifi_lora_32_V2', N_mh=30, N_path=40, mh=1656, path=2841, nbr=1332, stat=0, rows_old=13),
    'XML':       dict(env='E22_XML-DevKitC',        N_mh=50, N_path=50, mh=2756, path=3551, nbr=3156, stat=0, rows_old=21),
    'S3':        dict(env='heltec_wifi_lora_32_V3', N_mh=80, N_path=100, mh=4406, path=7101, nbr=3156, stat=0, rows_old=21),
    'nRF52':     dict(env='wiscore_rak4631',        N_mh=80, N_path=100, mh=4406, path=7101, nbr=3156, stat=1752, rows_old=21),
}
# Formeln gegen die Messung: MHeard 55 B je Eintrag + 6 B, Pfad 71 B je Eintrag + 1 B
for k, m in MEAS.items():
    assert m['mh'] == 55 * m['N_mh'] + 6, k
    assert m['path'] == 71 * m['N_path'] + 1, k
RINGS_OLD = 160  # ringNeed + ringAlone, 2 x 20 x 4 B, gemessen 80 + 80

# ---------------------------------------------------------------- Zielgroessen
SIZE = {
    'klassisch': dict(R=64,  W=8,  E=256, X=48, H=40),
    'XML':       dict(R=64,  W=8,  E=256, X=48, H=40),
    'S3':        dict(R=128, W=16, E=512, X=64, H=96),
    'nRF52':     dict(R=128, W=16, E=512, X=64, H=96),
}
EXT_BITS = [('Sekunde', 6), ('PLT', 2), ('MOD', 8), ('RSSI', 8), ('LAT', 21), ('LON', 22), ('ALT', 16), ('PL', 4), ('MESH', 1), ('f', 4), ('s', 4)]
assert sum(b for _, b in EXT_BITS) == 96
EXT_B = 12
CORE_B = 12
CALL_B = 8
EDGE_B = 6
HZ_META = 3

def newsize(s):
    R, W, E, X, H = s['R'], s['W'], s['E'], s['X'], s['H']
    d = dict(
        calls=CALL_B * R,
        core=CORE_B * R,
        masks=2 * W * R,
        edges=EDGE_B * E,
        ext=EXT_B * X,
        hz=(CALL_B + W + HZ_META) * H,
        hdr=2 + 2 + 4 + W + W + 4,    # boot_min, last_sweep, boot_epoch, D60-Maske, Via-Maske, Via-Zustand
        echo=4 * (4 + W + W + 2),     # letzte 4 eigene Rahmen: msg_id, Maske erste Hand, Maske zweite Hand, Minute
    )
    d['total'] = sum(d.values())
    d['rings'] = 2 * 20 * W
    return d

NEW = {k: newsize(s) for k, s in SIZE.items()}
NET = {}
for k in MEAS:
    m = MEAS[k]
    old = m['mh'] + m['path'] + m['nbr'] + m['stat']
    new = NEW[k]['total']
    NET[k] = dict(old=old, new=new, old_all=old + RINGS_OLD, new_all=new + NEW[k]['rings'],
                  net=new + NEW[k]['rings'] - old - RINGS_OLD)
assert NET['klassisch']['old'] == 5829 and NET['S3']['old'] == 14663 and NET['nRF52']['old'] == 16415
for k in NET:
    assert NET[k]['net'] < 0, k

# Kantenpool-Papier: Zeile mit call[10] und zwei uint64_t, gemessen 40 B (xtensa und arm)
AOS_ROW = 40

# ---------------------------------------------------------------- MH-JSON
base = {"TYP": "MH", "CALL": "OE7XWT-12", "DATE": "2026-09-24", "TIME": "08:27:33", "PLT": 64, "HW": 127,
        "MOD": 255, "RSSI": -160, "SNR": -20, "DIST": 1234.5, "PL": 15, "MESH": 1, "NCNT": 127}
newf = {"AGE": 720, "HM": -20, "ROLE": "S", "EX": 127, "NB": 127, "GW": 1, "VIA": 1}
posf = {"LAT": -48.4231, "LON": -123.7869, "ALT": 12345}
J = lambda d: len(json.dumps(d, separators=(',', ':')))
J_BASE = J(base) + 1
J_NEW = J(dict(base, **newf)) + 1
J_POS = J(dict(dict(base, **newf), **posf)) + 1
assert J_NEW <= 245 < J_POS

def de(n):
    s = f"{abs(n):,}".replace(',', '.')
    return ('-' if n < 0 else '') + s

def kb(n):
    return f"{n/1000:.1f}".replace('.', ',')

# ---------------------------------------------------------------- SVG 5: RAM-Balken
def svg_ram():
    scale = 0.031
    x0 = 170
    rows = []
    y = 40
    colors_old = [('MHeard', '#f6c9a0'), ('Pfadtabelle', '#e7c1e6'), ('Matrix', '#c9dcf5'), ('Ringe', '#dde3ea'), ('mheardLine-Statics', '#f3b4b4')]
    colors_new = [('Rufzeichen + Kern', '#b9d7c4'), ('Masken', '#9cc9ab'), ('Kanten', '#c9dcf5'), ('Direkt-Erweiterung', '#f6e3a0'), ('Horizont', '#e7c1e6'), ('Ringe', '#dde3ea')]
    out = []
    for k in ['klassisch', 'XML', 'S3', 'nRF52']:
        m = MEAS[k]; n = NEW[k]
        out.append(f'<text x="20" y="{y+14}" class="b">{ {"klassisch": "klassisch", "XML": "E22_XML", "S3": "S3", "nRF52": "nRF52"}[k] }</text>')
        out.append(f'<text x="20" y="{y+38}" class="t11">{m["env"]}</text>')
        # heute
        segs = [m['mh'], m['path'], m['nbr'], RINGS_OLD, m['stat']]
        x = x0
        out.append(f'<text x="{x0-8}" y="{y+14}" text-anchor="end" class="t11">heute</text>')
        for (lab, col), v in zip(colors_old, segs):
            if v <= 0:
                continue
            w = v * scale
            out.append(f'<rect x="{x:.1f}" y="{y+2}" width="{w:.1f}" height="16" style="fill:{col};stroke:#fff"/>')
            x += w
        out.append(f'<text x="{x+6:.1f}" y="{y+14}" class="mono">{de(sum(segs))} B</text>')
        # neu
        y2 = y + 24
        segs = [n['calls'] + n['core'], n['masks'], n['edges'], n['ext'], n['hz'] + n['hdr'] + n['echo'], n['rings']]
        x = x0
        out.append(f'<text x="{x0-8}" y="{y2+14}" text-anchor="end" class="t11">neu</text>')
        for (lab, col), v in zip(colors_new, segs):
            w = v * scale
            out.append(f'<rect x="{x:.1f}" y="{y2+2}" width="{w:.1f}" height="16" style="fill:{col};stroke:#fff"/>')
            x += w
        out.append(f'<text x="{x+6:.1f}" y="{y2+14}" class="mono">{de(sum(segs))} B, netto {de(NET[k]["net"])} B, {SIZE[k]["R"]} statt {m["rows_old"]} Zeilen</text>')
        y += 66
    # Legende
    ly = y + 6
    lx = 20
    out.append(f'<text x="{lx}" y="{ly+12}" class="t11 b">heute:</text>')
    lx = 70
    for lab, col in colors_old:
        out.append(f'<rect x="{lx}" y="{ly+2}" width="14" height="12" style="fill:{col}"/>')
        out.append(f'<text x="{lx+18}" y="{ly+12}" class="t11">{lab}</text>')
        lx += 18 + len(lab) * 6.2 + 18
    ly += 22
    out.append(f'<text x="20" y="{ly+12}" class="t11 b">neu:</text>')
    lx = 70
    for lab, col in colors_new:
        out.append(f'<rect x="{lx}" y="{ly+2}" width="14" height="12" style="fill:{col}"/>')
        out.append(f'<text x="{lx+18}" y="{ly+12}" class="t11">{lab}</text>')
        lx += 18 + len(lab) * 6.2 + 18
    h = ly + 30
    return f'<svg viewBox="0 0 960 {h}" role="img" aria-label="RAM je Familie">\n' + '\n'.join(out) + '\n</svg>'

# ---------------------------------------------------------------- SVG 3: Speicherlayout
def svg_layout():
    o = []
    def bar(x, y, w, h, label, cls='node', sub=None):
        o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" class="{cls}" style="rx:2"/>')
        o.append(f'<text x="{x+w/2:.1f}" y="{y+h/2+4:.1f}" text-anchor="middle" class="mono">{label}</text>')
    # A Rufzeichen
    y = 30
    o.append(f'<text x="40" y="{y}" class="b">Rufzeichen: ein 64-Bit-Wort, 6 Bit je Zeichen, Code 0 beendet</text>')
    chars = ['D', 'K', '5', 'E', 'N', '-', '9', '8', 'Ende']
    def code(c):
        if c == 'Ende': return 0
        if c.isdigit(): return 1 + int(c)
        if c == '-': return 37
        return 11 + ord(c) - ord('A')
    x = 40
    for c in chars:
        bar(x, y + 10, 60, 26, c, 'node srv')
        o.append(f'<text x="{x+30}" y="{y+50}" text-anchor="middle" class="t11">{code(c)}</text>')
        x += 60
    bar(x, y + 10, 100, 26, '10 Bit frei', 'node')
    o.append(f'<text x="{x+110}" y="{y+28}" class="t11">Vergleich zweier Rufzeichen = ein Wortvergleich</text>')
    # B Kern
    y = 110
    o.append(f'<text x="40" y="{y}" class="b">Zeilenkern: 12 Byte, fuer jede Zeile</text>')
    fields = [('lat16', 2), ('lon16', 2), ('last_min', 2), ('rpt_min', 2), ('flags', 1), ('hw', 1), ('ncnt', 1), ('ext', 1)]
    x = 40
    for lab, b in fields:
        bar(x, y + 10, b * 60, 26, lab)
        x += b * 60
    o.append(f'<text x="40" y="{y+52}" class="t11">ext: Nummer des Slots der Direkt-Erweiterung, 0xFF = keiner; ncnt: zuletzt gemeldete Nachbarzahl der Station</text>')
    # C Masken
    y = 196
    o.append(f'<text x="40" y="{y}" class="b">Masken je Zeile: eigene Felder, 64 Bit klassisch, 128 Bit S3 und nRF52</text>')
    bar(40, y + 10, 260, 26, 'hears[R]: wen X hoert', 'node srv')
    bar(300, y + 10, 260, 26, 'heardBy[R]: wer X hoert', 'node srv')
    o.append(f'<text x="570" y="{y+28}" class="t11">getrennt vom Kern, sonst 4 Byte Fuellung je Zeile</text>')
    # D Kante
    y = 266
    o.append(f'<text x="40" y="{y}" class="b">Kante: 6 Byte, "y hat x gehoert"</text>')
    x = 40
    for lab, b in [('x', 1), ('y', 1), ('cnt', 1), ('snr', 1), ('last_min', 2)]:
        bar(x, y + 10, b * 60, 26, lab)
        x += b * 60
    o.append(f'<text x="{x+10}" y="{y+28}" class="t11">256 Kanten klassisch, 512 auf S3 und nRF52</text>')
    # E Erweiterung
    y = 336
    o.append(f'<text x="40" y="{y}" class="b">Direkt-Erweiterung: 96 Bit = 12 Byte, nur fuer direkt gehoerte Zeilen</text>')
    x = 40
    for lab, b in EXT_BITS:
        w = b * 9
        o.append(f'<rect x="{x}" y="{y+22}" width="{w}" height="26" class="node prx" style="rx:1"/>')
        o.append(f'<text x="{x+w/2:.1f}" y="{y+39}" text-anchor="middle" class="mono">{b}</text>')
        lab2 = 'M' if lab == 'MESH' else ('sec' if lab == 'Sekunde' else lab)
        o.append(f'<text x="{x+w/2:.1f}" y="{y+17}" text-anchor="middle" class="t11">{lab2}</text>')
        x += w
    assert x == 904
    # F Horizont
    y = 416
    o.append(f'<text x="40" y="{y}" class="b">Horizont-Eintrag: 19 Byte klassisch, 27 Byte auf S3 und nRF52</text>')
    bar(40, y + 10, 240, 26, 'Rufzeichen 8 B', 'node hamf ham')
    bar(280, y + 10, 240, 26, 'Eintrittsmaske 8 / 16 B', 'node hamf ham')
    bar(520, y + 10, 90, 26, 'Meta 3 B', 'node hamf ham')
    o.append(f'<text x="620" y="{y+28}" class="t11">Meta: letzte Minute, kleinste Hop-Zahl, G-Bit</text>')
    return '<svg viewBox="0 0 960 466" role="img" aria-label="Speicherlayout">\n' + '\n'.join(o) + '\n</svg>'

SVG_LAYOUT = svg_layout()
SVG_RAM = svg_ram()

# ---------------------------------------------------------------- Werte fuer den Text
V = {}
for k in ['klassisch', 'XML', 'S3', 'nRF52']:
    V[k] = dict(old=de(NET[k]['old']), new=de(NET[k]['new']), net=de(NET[k]['net']),
                old_all=de(NET[k]['old_all']), new_all=de(NET[k]['new_all']))
row_c = CALL_B + CORE_B + 2 * SIZE['klassisch']['W']
row_s = CALL_B + CORE_B + 2 * SIZE['S3']['W']
hz_c = CALL_B + SIZE['klassisch']['W'] + HZ_META
hz_s = CALL_B + SIZE['S3']['W'] + HZ_META
assert (row_c, row_s, hz_c, hz_s) == (36, 52, 19, 27)
FILE_S3 = NEW['S3']['total'] + 32

TOKENS = {
    'CSS': CSS,
    'SVG_LAYOUT': SVG_LAYOUT,
    'SVG_RAM': SVG_RAM,
    'OLD_C': V['klassisch']['old'], 'OLD_X': V['XML']['old'], 'OLD_S': V['S3']['old'], 'OLD_N': V['nRF52']['old'],
    'NEW_C': V['klassisch']['new'], 'NEW_S': V['S3']['new'],
    'NET_C': V['klassisch']['net'], 'NET_X': V['XML']['net'], 'NET_S': V['S3']['net'], 'NET_N': V['nRF52']['net'],
    'NEWALL_C': V['klassisch']['new_all'], 'NEWALL_S': V['S3']['new_all'],
    'OLDALL_C': V['klassisch']['old_all'], 'OLDALL_X': V['XML']['old_all'], 'OLDALL_S': V['S3']['old_all'], 'OLDALL_N': V['nRF52']['old_all'],
    'NEWALL_X': V['XML']['new_all'], 'NEWALL_N': V['nRF52']['new_all'],
    'J_BASE': str(J_BASE), 'J_NEW': str(J_NEW), 'J_POS': str(J_POS),
    'FILE_S3': kb(FILE_S3),
    'AOS_ROW': str(AOS_ROW),
}
for k in ['klassisch', 'S3']:
    n = NEW[k]
    suf = 'C' if k == 'klassisch' else 'S'
    for f in ['calls', 'core', 'masks', 'edges', 'ext', 'hz', 'hdr', 'echo', 'rings']:
        TOKENS[f'{f.upper()}_{suf}'] = de(n[f])

BODY = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'MeshCom5-Topologie-als-Quelle.body.html')).read()
for t, v in TOKENS.items():
    BODY = BODY.replace('@@' + t + '@@', v)
left = re.findall(r'@@[A-Z_0-9]+@@', BODY)
assert not left, left
open(OUT, 'w').write(BODY)
print('written', OUT, len(BODY))
print({k: NET[k] for k in NET})
print('JSON', J_BASE, J_NEW, J_POS, 'file S3', FILE_S3)
