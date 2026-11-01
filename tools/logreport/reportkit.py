"""reportkit -- shared building blocks for the MeshCom 24-h log reports (2026-11-01).

Owned by the orchestrator. Report writers import it read-only:

    import sys; sys.path.insert(0, "<scratchpad>/kit")
    from reportkit import page, chip, table, bars, hist, timeline, esc

Everything renders to self-contained HTML (no external fonts, scripts or images), works in light
and dark theme (prefers-color-scheme and data-theme), BITV-minded contrast. Colour key:
ok = green, watch (Beobachten) = cyan, act (Handlungsbedarf) = rose/red, neutral = grey,
accent = indigo. No orange/brown/amber anywhere (operator rule).
Four font sizes only: --fs-h1, --fs-h2, --fs-body, --fs-small.
"""

from __future__ import annotations

import html
from typing import Iterable, Sequence

CSS = r"""
:root{
  /* Layout: single reading column 960px, tables full column width, charts as inline SVG. */
  color-scheme: light;
  --bg:#f7f9fc; --bg2:#ffffff; --bg3:#eef2f8; --line:#d6deea;
  --fg:#0f1a2b; --fg2:#3f4f68; --fg3:#5d6b82;
  --acc:#4338ca; --acc-bg:rgba(67,56,202,.08);
  --ok:#047857; --ok-bg:rgba(4,120,87,.09);
  --watch:#0e7490; --watch-bg:rgba(14,116,144,.10);
  --act:#be123c; --act-bg:rgba(190,18,60,.08);
  --neu:#475569; --neu-bg:rgba(71,85,105,.09);
  --c1:#4338ca; --c2:#0e7490; --c3:#be185d; --c4:#047857; --c5:#7c3aed; --c6:#475569;
  --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  --mono: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  --fs-h1: 1.75rem; --fs-h2: 1.25rem; --fs-body: 1rem; --fs-small: .875rem;
  --radius: 10px;
}
@media (prefers-color-scheme: dark){ :root:not([data-theme="light"]){
  color-scheme: dark;
  --bg:#0f172a; --bg2:#141e33; --bg3:#1b2742; --line:#2b3857;
  --fg:#e7edfb; --fg2:#a9b8d2; --fg3:#8a9ab6;
  --acc:#a5b4fc; --acc-bg:rgba(165,180,252,.12);
  --ok:#34d399; --ok-bg:rgba(52,211,153,.12);
  --watch:#22d3ee; --watch-bg:rgba(34,211,238,.12);
  --act:#fb7185; --act-bg:rgba(251,113,133,.12);
  --neu:#a3b1c6; --neu-bg:rgba(163,177,198,.12);
  --c1:#a5b4fc; --c2:#22d3ee; --c3:#f472b6; --c4:#34d399; --c5:#c4b5fd; --c6:#94a3b8;
}}
:root[data-theme="dark"]{
  color-scheme: dark;
  --bg:#0f172a; --bg2:#141e33; --bg3:#1b2742; --line:#2b3857;
  --fg:#e7edfb; --fg2:#a9b8d2; --fg3:#8a9ab6;
  --acc:#a5b4fc; --acc-bg:rgba(165,180,252,.12);
  --ok:#34d399; --ok-bg:rgba(52,211,153,.12);
  --watch:#22d3ee; --watch-bg:rgba(34,211,238,.12);
  --act:#fb7185; --act-bg:rgba(251,113,133,.12);
  --neu:#a3b1c6; --neu-bg:rgba(163,177,198,.12);
  --c1:#a5b4fc; --c2:#22d3ee; --c3:#f472b6; --c4:#34d399; --c5:#c4b5fd; --c6:#94a3b8;
}
*,*::before,*::after{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--fg);font-family:var(--sans);font-size:var(--fs-body);
  line-height:1.6;padding:clamp(1.25rem,4vw,3rem) 16px;overflow-wrap:break-word}
main{max-width:960px;margin:0 auto;display:flex;flex-direction:column;gap:1.25rem}
h1{font-size:var(--fs-h1);line-height:1.25;margin:0;text-wrap:balance}
h2{font-size:var(--fs-h2);line-height:1.3;margin:1.25rem 0 0;padding-top:1rem;border-top:1px solid var(--line);text-wrap:balance}
h3{font-size:var(--fs-body);margin:.5rem 0 0}
p,ul,ol{margin:0;max-width:75ch}
ul,ol{padding-left:1.25rem;display:flex;flex-direction:column;gap:.3rem}
small,.small,figcaption,.meta,td.n small{font-size:var(--fs-small);color:var(--fg2)}
a{color:var(--acc)}
a:focus-visible,summary:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
code,.mono{font-family:var(--mono);font-size:var(--fs-small)}
.eyebrow{font-size:var(--fs-small);color:var(--fg3);letter-spacing:.06em;text-transform:uppercase;font-weight:600}
ul.meta{list-style:none;padding:0;gap:.1rem}
.bluf{background:var(--acc-bg);border-left:4px solid var(--acc);border-radius:var(--radius);padding:1rem 1.25rem;display:flex;flex-direction:column;gap:.5rem}
.bluf .lead{font-weight:700}
.callout{border-radius:var(--radius);padding:.85rem 1.1rem;border-left:4px solid var(--neu);background:var(--neu-bg);display:flex;flex-direction:column;gap:.4rem}
.callout.act{border-color:var(--act);background:var(--act-bg)}
.callout.watch{border-color:var(--watch);background:var(--watch-bg)}
.callout.ok{border-color:var(--ok);background:var(--ok-bg)}
.chip{display:inline-block;font-size:var(--fs-small);font-weight:700;line-height:1.3;padding:.1rem .55rem;border-radius:999px;border:1px solid currentColor;white-space:nowrap}
.chip.ok{color:var(--ok);background:var(--ok-bg)}
.chip.watch{color:var(--watch);background:var(--watch-bg)}
.chip.act{color:var(--act);background:var(--act-bg)}
.chip.neu{color:var(--neu);background:var(--neu-bg)}
.chip.pos{color:var(--ok);background:var(--ok-bg)}
.chip.neg{color:var(--act);background:var(--act-bg)}
.table-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:var(--radius);background:var(--bg2)}
table{border-collapse:collapse;width:100%;font-size:var(--fs-small)}
th,td{padding:.4rem .65rem;text-align:left;vertical-align:top;border-bottom:1px solid var(--line)}
th{background:var(--bg3);font-weight:700;color:var(--fg)}
tr:last-child td{border-bottom:0}
td.n,th.n{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
figure{margin:0;background:var(--bg2);border:1px solid var(--line);border-radius:var(--radius);padding:.75rem 1rem;display:flex;flex-direction:column;gap:.4rem;overflow-x:auto}
/* Charts keep a minimum drawing width so SVG text never shrinks below ~10 px on a phone; the figure
   scrolls sideways instead (the page body never does). */
figure svg{width:100%;min-width:720px;height:auto;display:block}
svg text{fill:var(--fg2);font-family:var(--sans);font-size:13px}
svg .axis{stroke:var(--line);stroke-width:1}
svg .grid{stroke:var(--line);stroke-width:1;stroke-dasharray:2 4;opacity:.7}
svg .lbl{fill:var(--fg);font-weight:600}
nav.toc ol{columns:2;column-gap:2rem;display:block}
nav.toc li{margin-bottom:.2rem}
details{border:1px solid var(--line);border-radius:var(--radius);background:var(--bg2);padding:.6rem 1rem}
details>summary{cursor:pointer;font-weight:600}
details[open]>summary{margin-bottom:.6rem}
.grid3{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:.75rem}
.card{border:1px solid var(--line);border-radius:var(--radius);background:var(--bg2);padding:.75rem 1rem;display:flex;flex-direction:column;gap:.35rem}
.card.pos{border-top:4px solid var(--ok)} .card.neg{border-top:4px solid var(--act)} .card.neu{border-top:4px solid var(--neu)}
.foot{font-size:var(--fs-small);color:var(--fg3);border-top:1px solid var(--line);padding-top:1rem}
@media (max-width:520px){nav.toc ol{columns:1}}
/* Explainer drawings (one small animated SVG per identified issue).
   Shapes: .d-box (node/box), .d-node (circle), .d-line (static link), .d-txt / .d-txt-s (labels).
   State colours: .d-ok .d-act .d-watch .d-acc .d-neu (stroke+fill tint). Animations loop from a
   meaningful resting frame; with prefers-reduced-motion they stop on that frame. */
/* No align-items:center here: a centred flex child wider than its container is cut off on the left and
   cannot be scrolled; margin:auto centres it only while it fits. */
figure.explain svg{max-width:640px;min-width:520px;margin:0 auto}
svg .d-box{fill:var(--bg3);stroke:var(--line);stroke-width:1.5}
svg .d-node{fill:var(--bg3);stroke:var(--fg3);stroke-width:1.5}
svg .d-line{stroke:var(--fg3);stroke-width:1.5;fill:none}
svg .d-txt{fill:var(--fg);font-size:13px;font-weight:600}
svg .d-txt-s{fill:var(--fg2);font-size:13px}
svg .d-ok{stroke:var(--ok);fill:var(--ok-bg)} svg .d-act{stroke:var(--act);fill:var(--act-bg)}
svg .d-watch{stroke:var(--watch);fill:var(--watch-bg)} svg .d-acc{stroke:var(--acc);fill:var(--acc-bg)}
svg .d-neu{stroke:var(--neu);fill:var(--neu-bg)}
svg .d-ok-s{stroke:var(--ok)} svg .d-act-s{stroke:var(--act)} svg .d-watch-s{stroke:var(--watch)} svg .d-acc-s{stroke:var(--acc)}
svg .d-ok-f{fill:var(--ok)} svg .d-act-f{fill:var(--act)} svg .d-watch-f{fill:var(--watch)} svg .d-acc-f{fill:var(--acc)}
/* .anim-flow: dashes run along a path/line (packet in flight). */
svg .anim-flow{stroke-dasharray:6 6;animation:mc-flow 1.2s linear infinite}
@keyframes mc-flow{to{stroke-dashoffset:-24}}
/* .anim-pulse: element breathes (attention, active state). */
svg .anim-pulse{animation:mc-pulse 2s ease-in-out infinite;transform-box:fill-box;transform-origin:center}
@keyframes mc-pulse{0%,100%{opacity:1}50%{opacity:.35}}
/* .anim-move: element travels by --dx/--dy (set inline: style="--dx:200px"), then restarts. */
svg .anim-move{animation:mc-move var(--dur,3s) ease-in-out infinite}
@keyframes mc-move{0%,10%{transform:translate(0,0);opacity:1}80%{transform:translate(var(--dx,0),var(--dy,0));opacity:1}100%{transform:translate(var(--dx,0),var(--dy,0));opacity:0}}
/* .anim-drop: element fades out late in the cycle (dropped / expired / lost). */
svg .anim-drop{animation:mc-drop var(--dur,3s) ease-in infinite}
@keyframes mc-drop{0%,60%{opacity:1}85%,100%{opacity:.08}}
/* .anim-delay-1..3: stagger. */
svg .anim-delay-1{animation-delay:.5s} svg .anim-delay-2{animation-delay:1s} svg .anim-delay-3{animation-delay:1.5s}
@media (prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
"""

VERDICT = {
    "ok": ("ok", "OK"),
    "watch": ("watch", "Beobachten"),
    "act": ("act", "Handlungsbedarf"),
    "neu": ("neu", "Neutral"),
    "pos": ("pos", "Positiv"),
    "neg": ("neg", "Negativ"),
}

SERIES = ["var(--c1)", "var(--c2)", "var(--c3)", "var(--c4)", "var(--c5)", "var(--c6)"]


def esc(s: object) -> str:
    return html.escape(str(s), quote=True)


def chip(kind: str, text: str | None = None) -> str:
    """kind: ok | watch | act | neu | pos | neg. Text defaults to the German label."""
    cls, label = VERDICT[kind]
    return f'<span class="chip {cls}">{esc(text or label)}</span>'


def table(head: Sequence[str], rows: Iterable[Sequence[object]], num_cols: Sequence[int] = (),
          raw: bool = False, caption: str | None = None) -> str:
    """HTML table in a scroll wrapper. num_cols = indices right-aligned (tabular nums).
    raw=True: cells are inserted unescaped (use when cells contain chip() etc.)."""
    nc = set(num_cols)
    th = "".join(f'<th class="n">{esc(h)}</th>' if i in nc else f"<th>{esc(h)}</th>" for i, h in enumerate(head))
    body = []
    for r in rows:
        tds = []
        for i, c in enumerate(r):
            v = str(c) if raw else esc(c)
            tds.append(f'<td class="n">{v}</td>' if i in nc else f"<td>{v}</td>")
        body.append("<tr>" + "".join(tds) + "</tr>")
    cap = f"<figcaption>{esc(caption)}</figcaption>" if caption else ""
    return f'<div class="table-wrap"><table><thead><tr>{th}</tr></thead><tbody>{"".join(body)}</tbody></table></div>{cap}'


def _fmt(v: float) -> str:
    if abs(v) >= 100 or float(v).is_integer():
        return f"{v:,.0f}".replace(",", ".")
    return f"{v:.1f}".replace(".", ",")


def bars(items: Sequence[tuple[str, float]], caption: str, unit: str = "", width: int = 900,
         color: str = "var(--c1)") -> str:
    """Horizontal bar chart. items = [(label, value)]. caption = 'Abb. N: <Aussage>'."""
    if not items:
        return ""
    vmax = max(v for _, v in items) or 1
    # label column sized to the longest label (~7.4 px per char at 13 px), so no label runs out on the left
    lw = int(min(max(120, 7.4 * max(len(str(lab)) for lab, _ in items) + 20), width * 0.6))
    row, pad = 26, 8
    h = len(items) * row + pad * 2
    bw = width - lw - 90
    out = [f'<svg viewBox="0 0 {width} {h}" role="img" aria-label="{esc(caption)}">']
    for i, (lab, v) in enumerate(items):
        y = pad + i * row
        w = max(1.0, bw * v / vmax)
        out.append(f'<text x="{lw - 8}" y="{y + 17}" text-anchor="end">{esc(lab)}</text>')
        out.append(f'<rect x="{lw}" y="{y + 4}" width="{w:.1f}" height="{row - 9}" rx="3" fill="{color}"/>')
        out.append(f'<text class="lbl" x="{lw + w + 6:.1f}" y="{y + 17}">{esc(_fmt(v))}{esc(unit)}</text>')
    out.append("</svg>")
    return f'<figure>{"".join(out)}<figcaption>{esc(caption)}</figcaption></figure>'


def hist(bins: Sequence[tuple[str, float]], caption: str, unit: str = "", width: int = 900,
         height: int = 240, color: str = "var(--c2)", ylabel: str = "") -> str:
    """Vertical histogram / column chart. bins = [(label, value)]."""
    if not bins:
        return ""
    vmax = max(v for _, v in bins) or 1
    l, r, t, b = 56, 12, 16, 44
    pw, ph = width - l - r, height - t - b
    n = len(bins)
    cw = pw / n
    out = [f'<svg viewBox="0 0 {width} {height}" role="img" aria-label="{esc(caption)}">']
    for k in range(5):
        gy = t + ph - ph * k / 4
        out.append(f'<line class="grid" x1="{l}" x2="{width - r}" y1="{gy:.1f}" y2="{gy:.1f}"/>')
        out.append(f'<text x="{l - 6}" y="{gy + 4:.1f}" text-anchor="end">{esc(_fmt(vmax * k / 4))}</text>')
    out.append(f'<line class="axis" x1="{l}" x2="{width - r}" y1="{t + ph}" y2="{t + ph}"/>')
    step = max(1, n // 12)
    for i, (lab, v) in enumerate(bins):
        bh = ph * v / vmax
        x = l + i * cw
        out.append(f'<rect x="{x + cw * 0.12:.1f}" y="{t + ph - bh:.1f}" width="{cw * 0.76:.1f}" height="{bh:.1f}" rx="2" fill="{color}"><title>{esc(lab)}: {esc(_fmt(v))}{esc(unit)}</title></rect>')
        if i % step == 0:
            out.append(f'<text x="{x + cw / 2:.1f}" y="{t + ph + 19}" text-anchor="middle">{esc(lab)}</text>')
    if ylabel:
        out.append(f'<text x="{l}" y="{height - 6}">{esc(ylabel)}</text>')
    out.append("</svg>")
    return f'<figure>{"".join(out)}<figcaption>{esc(caption)}</figcaption></figure>'


def timeline(series: Sequence[tuple[str, Sequence[tuple[float, float]]]], caption: str,
             xlabels: Sequence[tuple[float, str]] = (), unit: str = "", width: int = 900,
             height: int = 260, ymax: float | None = None) -> str:
    """Line chart. series = [(name, [(x, y), ...])], x e.g. hours since start.
    xlabels = [(x, 'HH:MM')]. Legend is drawn from series names."""
    pts = [p for _, s in series for p in s]
    if not pts:
        return ""
    xmin, xmax = min(p[0] for p in pts), max(p[0] for p in pts)
    ym = ymax if ymax is not None else (max(p[1] for p in pts) or 1)
    l, r, t, b = 56, 12, 28, 40
    pw, ph = width - l - r, height - t - b
    sx = lambda x: l + (x - xmin) / ((xmax - xmin) or 1) * pw  # noqa: E731
    sy = lambda y: t + ph - min(y, ym) / ym * ph  # noqa: E731
    out = [f'<svg viewBox="0 0 {width} {height}" role="img" aria-label="{esc(caption)}">']
    for k in range(5):
        gy = t + ph - ph * k / 4
        out.append(f'<line class="grid" x1="{l}" x2="{width - r}" y1="{gy:.1f}" y2="{gy:.1f}"/>')
        out.append(f'<text x="{l - 6}" y="{gy + 4:.1f}" text-anchor="end">{esc(_fmt(ym * k / 4))}{esc(unit)}</text>')
    out.append(f'<line class="axis" x1="{l}" x2="{width - r}" y1="{t + ph}" y2="{t + ph}"/>')
    for x, lab in xlabels:
        out.append(f'<text x="{sx(x):.1f}" y="{t + ph + 19}" text-anchor="middle">{esc(lab)}</text>')
    lx = l
    for i, (name, s) in enumerate(series):
        col = SERIES[i % len(SERIES)]
        d = " ".join(f"{'M' if j == 0 else 'L'}{sx(x):.1f},{sy(y):.1f}" for j, (x, y) in enumerate(s))
        out.append(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="1.8"/>')
        if s:
            ex, ey = s[-1]
            out.append(f'<circle cx="{sx(ex):.1f}" cy="{sy(ey):.1f}" r="3" fill="{col}"/>')
        out.append(f'<rect x="{lx}" y="6" width="12" height="12" rx="2" fill="{col}"/>')
        out.append(f'<text x="{lx + 16}" y="16">{esc(name)}</text>')
        lx += 24 + 7 * len(name)
    out.append("</svg>")
    return f'<figure>{"".join(out)}<figcaption>{esc(caption)}</figcaption></figure>'


def explain(svg_inner: str, caption: str, width: int = 640, height: int = 220) -> str:
    """Small animated explainer drawing for one identified issue.

    svg_inner uses the .d-* shape/state classes and .anim-* animation classes from CSS.
    Rules: the resting frame (no animation) must already tell the story; text inside the SVG
    stays >= 13 px and inside the viewBox; colours only via classes (theme-aware).
    caption = 'Abb. N: <Aussage>'."""
    return (f'<figure class="explain"><svg viewBox="0 0 {width} {height}" role="img" '
            f'aria-label="{esc(caption)}">{svg_inner}</svg><figcaption>{esc(caption)}</figcaption></figure>')


def page(title: str, body_html: str, lang: str = "de") -> str:
    """Complete self-contained HTML document."""
    return (f'<!DOCTYPE html>\n<html lang="{lang}">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>{esc(title)}</title>\n<style>{CSS}</style>\n</head>\n<body>\n<main>\n'
            f'{body_html}\n</main>\n</body>\n</html>\n')
