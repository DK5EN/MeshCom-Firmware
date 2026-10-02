#!/usr/bin/env python3
"""Put the numbers of tools/code_share/measure.py on the pr-history page.

    python3 tools/code_share/measure.py --out build/code-share
    python3 tools/code_share/render_pages.py --summary build/code-share/summary.json \
        [--dir docs/presentation]

Adds the section "Wie viel vom Original steckt noch in ..." (id="anteil") in front of
the first round, the column "In <version>" to every PR row (docs/presentation/), and
the sentence on the index card. Idempotent: an existing section and column are
replaced, not duplicated. Publish afterwards with tools/pages-sync.sh. Stdlib only.
"""
# ruff: noqa: E501  (the German page text and CSS are long single-line templates)

import argparse
import html
import json
import re
from pathlib import Path


def de(n):
    return f"{n:,}".replace(",", ".")


def pc(x):
    return f"{x:.1f}".replace(".", ",")


ORDER = [
    "src/ Kernlogik (Funk, Mesh, Kommandos, APRS)",
    "src/nrf52",
    "src/esp32",
    "src/safeboot",
    "src/ui_common",
    "src/web_functions",
    "src/t-deck",
    "variants",
    "config",
]
CSS = (
    ".share{display:flex;height:2.1rem;border-radius:8px;overflow:hidden;border:1px solid var(--line);"
    "margin:.6rem 0 .4rem;font-size:.74rem;font-family:var(--mono)}\n"
    ".share .seg{display:flex;align-items:center;justify-content:center;white-space:nowrap;overflow:hidden;color:#fff}\n"
    ".s-orig{background:#64748b}.s-pr{background:var(--acc)}.s-x{background:var(--acc2)}.s-oth{background:#0ea5e9}\n"
    "td .note{color:var(--fg3)}\ntable.sh th{width:auto}\n"
)
W5 = (
    "th:nth-child(1){width:9%}th:nth-child(2){width:13%}th:nth-child(3){width:50%}"
    "th:nth-child(4){width:18%}th:nth-child(5){width:10%}"
)
W6 = (
    "th:nth-child(1){width:8%}th:nth-child(2){width:11%}th:nth-child(3){width:44%}"
    "th:nth-child(4){width:17%}th:nth-child(5){width:9%}th:nth-child(6){width:11%}"
)
ROW = re.compile(
    r'(<tr[^>]*><td><a href="[^"]*/pull/(\d+)">#\d+</a></td><td>[\d-]+</td><td>.*?</td>'
    r'<td class="num">(.*?)</td><td class="num">[^<]*</td>)(<td class="num">[^<]*</td>)?</tr>'
)


def col(widths):
    cols = "".join('<col style="width:%d%%">' % w for w in widths)
    return "<colgroup>" + cols + "</colgroup>"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--summary", required=True)
    ap.add_argument("--dir", default="docs/presentation")
    ap.add_argument("--version", default="4.40a")
    a = ap.parse_args()
    S = json.loads(Path(a.summary).read_text())
    d = Path(a.dir)
    page = (d / "pr-history.html").read_text(encoding="utf-8")
    BASE, NOW = S["base_lines"], S["now_lines"]
    M = S["modes"]["strict"]
    ORIG, OTH = M["original"], M["other"]
    DKPR, DKX = S["dk5en_pr_lines"], S["dk5en_direct_lines"]
    assert ORIG + DKPR + DKX + OTH == NOW, "summary does not add up"
    surv = {int(k): v for k, v in S["per_pr"].items()}
    ver = a.version

    # 1. start from a page without the section, with a 5-column table
    page = re.sub(r'<section id="anteil">.*?</section>\n', "", page, flags=re.S)
    page = page.replace(f"<th>Dateien</th><th>In {ver}</th>", "<th>Dateien</th>")

    def addcol(m):
        pr, merged = int(m.group(2)), m.group(3) != "nicht gemerged"
        v = de(surv[pr]["lines"]) if merged and pr in surv else ("0" if merged else "–")
        return m.group(1) + f'<td class="num">{v}</td></tr>'

    page, n_rows = ROW.subn(addcol, page)
    page = page.replace(
        "<th>Zeilen</th><th>Dateien</th></tr></thead>",
        f"<th>Zeilen</th><th>Dateien</th><th>In {ver}</th></tr></thead>",
    )
    page = page.replace(W5, W6)
    if ".share{" not in page:
        page = page.replace("</style>", CSS + "</style>", 1)

    # 2. data for the section
    rounds, titles, merged_total = [], {}, 0
    for s in re.split(r"(?=<section>)", page)[1:]:
        h2 = re.search(r"<h2>([^<]*)", s)
        eb = re.search(r'class="eyebrow">([^<]*)', s)
        rows = re.findall(
            r'<tr[^>]*><td><a href="[^"]*/pull/(\d+)">#\d+</a></td><td>([\d-]+)</td><td>(.*?)'
            r'(?:<div class="note">.*?</div>)?</td><td class="num">(.*?)</td>',
            s,
        )
        mr = [int(r[0]) for r in rows if r[3] != "nicht gemerged"]
        for r in rows:
            titles[int(r[0])] = (r[1], re.sub("<[^>]+>", "", r[2]))
        if mr:
            rounds.append(
                (
                    eb.group(1),
                    h2.group(1),
                    len(mr),
                    sum(surv.get(p, {"lines": 0})["lines"] for p in mr),
                )
            )
            merged_total += len(mr)
    assert sum(r[3] for r in rounds) == DKPR, "round sums differ from the PR sum"
    top = sorted(surv.items(), key=lambda kv: -kv[1]["lines"])[:10]
    big, fl, ar = S["big_files"], S["files"], S["areas"]

    def kept(f):
        return round(100 * big[f]["kept"] / big[f]["base"])

    def bw(x):
        return f"{x:.2f}"

    first = S["base"][:8]
    sec = f"""<section id="anteil">
<div class="eyebrow">Zeilenbilanz · git blame auf {html.escape(S["ref"])} {S["ref_sha"][:8]} (v{ver})</div>
<h2>Wie viel vom Original steckt noch in {ver}?</h2>
<p class="lead">Die Kernfirmware ist von {de(BASE)} Zeilen (Stand vor dem ersten PR, 22.02.2026) auf {de(NOW)} Zeilen gewachsen. <b>{pc(100 * ORIG / NOW)} % der heutigen Zeilen sind noch unverändert die von Februar</b>, {pc(100 * ORIG / BASE)} % der damaligen Zeilen haben überlebt, rund {pc(100 - 100 * ORIG / BASE)} % wurden entfernt oder neu geschrieben. {pc(100 * (DKPR + DKX) / NOW)} % der heutigen Zeilen stammen aus Commits dieses Forks, {pc(100 * OTH / NOW)} % von anderen Autoren in upstream.</p>
<div class="stats">
<div class="stat"><div class="k">Ausgangsbasis 22.02.</div><div class="v">{de(BASE)}</div></div>
<div class="stat"><div class="k">Heute ({ver})</div><div class="v">{de(NOW)}</div></div>
<div class="stat"><div class="k">Original unverändert</div><div class="v">{de(ORIG)} · {pc(100 * ORIG / NOW)} %</div></div>
<div class="stat"><div class="k">Original überlebt</div><div class="v">{pc(100 * ORIG / BASE)} %</div></div>
</div>
<div class="share" role="img" aria-label="Zusammensetzung der Kernfirmware {ver}: {pc(100 * ORIG / NOW)} % Original, {pc(100 * DKPR / NOW)} % PRs dieses Forks, {pc(100 * DKX / NOW)} % Einzelcommits dieses Forks, {pc(100 * OTH / NOW)} % andere Autoren">
<span class="seg s-orig" style="width:{bw(100 * ORIG / NOW)}%">Original {pc(100 * ORIG / NOW)} %</span><span class="seg s-pr" style="width:{bw(100 * DKPR / NOW)}%">PRs DK5EN {pc(100 * DKPR / NOW)} %</span><span class="seg s-x" style="width:{bw(100 * DKX / NOW)}%"></span><span class="seg s-oth" style="width:{bw(100 * OTH / NOW)}%">andere {pc(100 * OTH / NOW)} %</span>
</div>
<p class="small">Original = Zeilen, die schon vor dem ersten PR in upstream standen. PRs DK5EN = Zeilen aus Commits von {S["prs_with_lines"]} der {merged_total} gemergten PRs dieses Forks, die heute noch stehen ({de(DKPR)}). Einzelcommits ohne PR: {de(DKX)} Zeilen. Andere = Commits anderer Autoren seit Februar, im Wesentlichen das ICSSW-Team.</p>
<h3>Je Runde: wie viel der Runde steht heute noch</h3>
<div class="tocwrap"><table class="sh">{col([52, 10, 20, 18])}<thead><tr><th>Runde</th><th>PRs</th><th>Zeilen heute</th><th>Anteil</th></tr></thead><tbody>
"""
    for eb, h2, np_, sv in rounds:
        sec += f'<tr><td>{html.escape(eb)}<div class="note small">{html.escape(h2)}</div></td><td class="num">{np_}</td><td class="num">{de(sv)}</td><td class="num">{pc(100 * sv / NOW)} %</td></tr>\n'
    sec += f"""</tbody></table></div>
<p class="small">Gezählt werden nur Zeilen, die in den PRs der Runde entstanden und im aktuellen Stand noch stehen; spätere Umbauten nehmen sie der Runde wieder weg. Die Spalte „Zeilen“ in den Tabellen unten zeigt dagegen, was GitHub zum Zeitpunkt des Merge gezählt hat.</p>
<h3>Die zehn PRs mit dem größten Anteil in {ver}</h3>
<div class="tocwrap"><table class="sh">{col([14, 52, 18, 16])}<thead><tr><th>PR</th><th>Titel</th><th>Zeilen heute</th><th>Dateien</th></tr></thead><tbody>
"""
    for pr, v in top:
        dt, t = titles.get(pr, ("", f"PR #{pr}"))
        sec += f'<tr><td><a href="https://github.com/icssw-org/MeshCom-Firmware/pull/{pr}">#{pr}</a><div class="note small">{dt}</div></td><td>{html.escape(t)}</td><td class="num">{de(v["lines"])}</td><td class="num">{v["files"]}</td></tr>\n'
    rest = DKPR - sum(v["lines"] for _, v in top)
    top_pr = top[0]
    sec += f"""</tbody></table></div>
<p class="small">Die übrigen {S["prs_with_lines"] - 10} PRs tragen zusammen {de(rest)} Zeilen bei, dazu kommen {de(DKX)} Zeilen aus Einzelcommits ohne PR. PR #{top_pr[0]} allein stellt {de(top_pr[1]["lines"])} Zeilen, das sind {pc(100 * top_pr[1]["lines"] / NOW)} % der Kernfirmware.</p>
<h3>Wo am meisten neu ist</h3>
<div class="tocwrap"><table class="sh">{col([56, 22, 22])}<thead><tr><th>Bereich</th><th>Zeilen heute</th><th>davon Original</th></tr></thead><tbody>
"""
    for k in ORDER:
        sec += f'<tr><td>{html.escape(k)}</td><td class="num">{de(ar[k]["lines"])}</td><td class="num">{pc(100 * ar[k]["original"] / ar[k]["lines"])} %</td></tr>\n'
    sec += f"""</tbody></table></div>
<p class="small">Die großen Dateien sind gewachsen, aber ihre alten Zeilen blieben meist stehen: <code>loop_functions.cpp</code> {kept("src/loop_functions.cpp")} %, <code>web_functions.cpp</code> {kept("src/web_functions/web_functions.cpp")} %, <code>command_functions.cpp</code> {kept("src/command_functions.cpp")} %, <code>lora_functions.cpp</code> {kept("src/lora_functions.cpp")} % und <code>udp_functions.cpp</code> {kept("src/udp_functions.cpp")} % der Zeilen von Februar sind noch da. Das Neue steht überwiegend in neuen Dateien ({fl["new"]} neue gegen {fl["baseline"]} Ausgangsdateien); vollständig ersetzt wurde nur die MHeard-Liste (<code>mheard_functions.cpp</code>). Von {fl["baseline"]} Dateien aus Februar sind {fl["identical"]} byte-identisch, {fl["modified"]} verändert und {fl["deleted"]} entfallen.</p>
<div class="box">Methode: <code>git blame -w</code> auf jeder Datei der handgeschriebenen Kernfirmware von {html.escape(S["ref"])} {S["ref_sha"][:8]} (<code>src/</code> ohne Fonts, Plattform-Stubs, Display- und Treiberbibliotheken, T5- und T-Deck-Pro-Oberfläche und den erzeugten <code>ota.h</code>, dazu <code>variants/</code>, <code>config/</code>, <code>platformio.ini</code>; <code>lib/</code> mit rund {pc(S["lib_lines_ref"] / 1e6)} Millionen Zeilen Fremdcode bleibt außen vor). Ausgangspunkt ist der letzte upstream-Stand vor dem Merge des ersten PR ({first}). Eine Zeile zählt als Original, wenn ihr letzter Autor-Commit in der Historie dieses Stands liegt. Mit Erkennung verschobener und kopierter Zeilen (<code>-M -C</code>) steigt der Originalanteil auf {pc(100 * S["modes"]["moves"]["original"] / NOW)} %. Zuordnung zu PRs über die Merge-Commits „from DK5EN“; Zeilenzahlen sind rohe Zeilen einschließlich Kommentaren und Leerzeilen und sagen nichts über Verhalten. Autorenzuordnung nach Commit-Adresse: PRs, die das ICSSW-Team unter eigenem Namen übernommen hat, erscheinen unter „andere“. Skripte: <code>tools/code_share/</code>.</div>
</section>
"""
    i = page.index("<section>")
    page = page[:i] + sec + page[i:]
    (d / "pr-history.html").write_text(page, encoding="utf-8")

    idx = (d / "index.html").read_text(encoding="utf-8")
    new = f"Mit Zeilenbilanz je PR und je Runde und der Frage, wie viel vom Original von Februar in {ver} noch steht: {round(100 * ORIG / NOW)} % der Kernfirmware, {round(100 * ORIG / BASE)} % der damaligen Zeilen.</p>"
    idx, n_idx = re.subn(
        r"Mit Zeilenbilanz je PR und je Runde[^<]*</p>", lambda m: new, idx, count=1
    )
    assert n_idx == 1, "index card sentence not found"
    (d / "index.html").write_text(idx, encoding="utf-8")
    print(f"rows {n_rows}, rounds {len(rounds)}, PRs on page {merged_total}")


if __name__ == "__main__":
    main()
