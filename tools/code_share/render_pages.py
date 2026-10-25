#!/usr/bin/env python3
"""Put the numbers of tools/code_share/measure.py on the pr-history page.

    python3 tools/code_share/measure.py --out build/code-share
    python3 tools/code_share/render_pages.py --summary build/code-share/summary.json \
        [--dir docs/presentation] [--version 4.40a] [--stand DD.MM.YYYY]

Writes, in docs/presentation/pr-history.html:
  - the block "Wie viel vom Original steckt noch in <version>?" from <h2 id="anteil">
    up to the first round heading (<h2 id="rN">), with the round, top-10 and area
    tables in <details>;
  - the column "In <version>" of every PR row in every round (r7, r6 as <h2>,
    r1-r5 as <details>): surviving lines for a measured PR, "0" for a measured PR
    with nothing left, "–" for a PR that is not merged or was merged after --stand;
  - the note "Die Spalte „In <version>“ ist für die PRs ab #N leer ..." under the
    table of the first PR merged after --stand (removed when there is none);
  - the TOC entry and the details block "Methode der Zeilenbilanz";
and the sentence "Mit Zeilenbilanz je PR und je Runde: ..." on the index card.

--stand is the date of the measured state; summary.json carries none, so it defaults
to the commit date of the summary's ref_sha (git log in the repo of this script).
Lead sentence, key facts, meta and "Was nicht geprüft ist" lines elsewhere on the
page are hand-written and not touched.
Idempotent: a second run with the same summary changes nothing. Publish afterwards
with tools/pages-sync.sh. Stdlib only.
"""
# ruff: noqa: E501  (the German page text is long single-line templates)

import argparse
import datetime as dt
import json
import re
import subprocess
import sys
from pathlib import Path


def de(n):
    return f"{n:,}".replace(",", ".")


def pc(x):
    return f"{x:.1f}".replace(".", ",")


def bw(x):
    return f"{x:.2f}"


def und(items):
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " und " + items[-1]


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
BIG = [
    ("loop_functions.cpp", "src/loop_functions.cpp"),
    ("web_functions.cpp", "src/web_functions/web_functions.cpp"),
    ("command_functions.cpp", "src/command_functions.cpp"),
    ("lora_functions.cpp", "src/lora_functions.cpp"),
    ("udp_functions.cpp", "src/udp_functions.cpp"),
]
BASE_DATE = "22.02.2026"  # last upstream state before the first PR (measure.py --base)
PR_URL = "https://github.com/icssw-org/MeshCom-Firmware/pull/"
CSS = (
    "table.sh{min-width:520px}\n"
    "td .note{color:var(--fg3);font-size:.78rem;margin-top:.15rem}\n"
    "table.sh td.num,table.sh th.num{font-family:var(--mono);font-size:.8rem;white-space:nowrap}\n"
    ".share{display:flex;height:2.1rem;border-radius:8px;overflow:hidden;border:1px solid var(--line);margin:.6rem 0 .4rem;font-size:.74rem;font-family:var(--mono)}\n"
    ".share .seg{display:flex;align-items:center;justify-content:center;white-space:nowrap;overflow:hidden;color:#fff}\n"
    ".s-orig{background:#64748b}.s-pr{background:#4f46e5}.s-x{background:#c026d3}.s-oth{background:#0369a1}\n"
)
# "Zeilen" cell of a PR without merge: closed ones say "nicht gemerged", open ones "offen"
NOT_MERGED = ("nicht gemerged", "offen")
# one PR row of a round table; the last cell ("In <version>") is optional on input
ROW = re.compile(
    r'(?P<head><tr(?P<cls>[^>]*)><td><a href="[^"]*/pull/(?P<pr>\d+)">#\d+</a></td>'
    r"<td>(?P<date>\d\d\.\d\d\.\d{4})</td><td>(?P<title>.*?)</td>"
    r'<td class="num">(?P<lines>.*?)</td><td class="num">[^<]*</td>)'
    r'(?:<td class="num">[^<]*</td>)?</tr>'
)
NOTE_DIV = re.compile(r'<div class="note">.*?</div>')
HEAD = re.compile(r"<th>Zeilen</th><th>Dateien</th>(?:<th>In [^<]*</th>)?</tr></thead>")
EMPTY_NOTE = re.compile(
    r"<p>Die Spalte „In [^“]*“ ist für die PRs ab #\d+ leer, weil die Messung vom [\d.]+ stammt\.</p>\n"
)
ANTEIL = re.compile(r'<h2 id="anteil">.*?(?=<h2 id="r\d+">)', re.S)
METHOD = re.compile(
    r'<details class="dev">\n<summary>Methode der Zeilenbilanz</summary>\n.*?</details>\n', re.S
)
ROUND_ANCHOR = re.compile(r'<(?:h2|details) id="r(\d+)">')
BOUNDARY = re.compile(r'<h2 |<details id="r\d+">')


def day(s):
    d, m, y = (int(x) for x in s.split("."))
    return dt.date(y, m, d)


def period(dates):
    a, b = min(dates), max(dates)
    if a == b:
        return f"{a:%d.%m.}"
    if a.year != b.year:
        return f"{a:%d.%m.%Y}–{b:%d.%m.%Y}"
    if a.month == b.month:
        return f"{a:%d}.–{b:%d.%m.}"
    return f"{a:%d.%m.}–{b:%d.%m.}"


def ref_html(ref, sha):
    """'upstream/dev' -> 'upstream <code>dev</code> 9669ec99'; a bare sha -> '<code>9669ec99</code>'."""
    if re.fullmatch(r"[0-9a-f]{7,40}", ref):
        return f"<code>{sha[:8]}</code>"
    remote, _, branch = ref.rpartition("/")
    return f"{remote + ' ' if remote else ''}<code>{branch}</code> {sha[:8]}"


def commit_date(where, sha):
    r = subprocess.run(
        ["git", "-C", str(where), "log", "-1", "--format=%cd", "--date=format:%d.%m.%Y", sha],
        capture_output=True,
        text=True,
    )
    if r.returncode or not re.fullmatch(r"\d\d\.\d\d\.\d{4}", r.stdout.strip()):
        sys.exit(f"cannot read the date of {sha[:8]} from git; pass --stand DD.MM.YYYY")
    return r.stdout.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--summary", required=True)
    ap.add_argument("--dir", default="docs/presentation")
    ap.add_argument("--version", default="4.40a")
    ap.add_argument(
        "--stand",
        help="date of the measured state, DD.MM.YYYY (default: commit date of ref_sha)",
    )
    a = ap.parse_args()
    S = json.loads(Path(a.summary).read_text())
    d = Path(a.dir)
    stand = a.stand or commit_date(Path(__file__).resolve().parent, S["ref_sha"])
    if not re.fullmatch(r"\d\d\.\d\d\.\d{4}", stand):
        sys.exit("--stand must be DD.MM.YYYY")
    stand_day = day(stand)
    page = (d / "pr-history.html").read_text(encoding="utf-8")
    BASE, NOW = S["base_lines"], S["now_lines"]
    M = S["modes"]["strict"]
    ORIG, OTH = M["original"], M["other"]
    DKPR, DKX = S["dk5en_pr_lines"], S["dk5en_direct_lines"]
    assert ORIG + DKPR + DKX + OTH == NOW, "summary does not add up"
    surv = {int(k): v for k, v in S["per_pr"].items()}
    ver = a.version
    refh = ref_html(S["ref"], S["ref_sha"])

    # 1. PR rows: classify every row, then fill the "In <version>" column
    info = {}  # pr -> (date, title html, merged, measured)

    def classify(m):
        pr, date = int(m["pr"]), m["date"]
        lines = re.sub("<[^>]+>", "", m["lines"]).strip()
        merged = not re.search(r"\b(closed|open)\b", m["cls"]) and lines not in NOT_MERGED
        measured = merged and (pr in surv or day(date) <= stand_day)
        info[pr] = (date, NOTE_DIV.sub("", m["title"]), merged, measured)
        return pr, measured

    def fill(m):
        pr, measured = classify(m)
        v = de(surv[pr]["lines"]) if pr in surv else ("0" if measured else "–")
        return m["head"] + f'<td class="num">{v}</td></tr>'

    page, n_rows = ROW.subn(fill, page)
    page = HEAD.sub(f"<th>Zeilen</th><th>Dateien</th><th>In {ver}</th></tr></thead>", page)
    missing = sorted(set(surv) - set(info))
    assert not missing, f"PRs of the summary without a row on the page: {missing}"
    late = sorted(p for p, (_, _, mg, ms) in info.items() if mg and not ms)
    page = EMPTY_NOTE.sub("", page)
    if late:
        i = page.index(f'/pull/{late[0]}">#{late[0]}</a></td><td>')
        j = page.index("</tbody></table></div>\n", i) + len("</tbody></table></div>\n")
        note = f"<p>Die Spalte „In {ver}“ ist für die PRs ab #{late[0]} leer, weil die Messung vom {stand} stammt.</p>\n"
        page = page[:j] + note + page[j:]
    if ".share{" not in page:
        page = page.replace("</style>", CSS + "</style>", 1)
    page = re.sub(
        r'(<li><a href="#anteil">)Wie viel vom Original steckt noch in [^<]*\?(</a></li>)',
        lambda m: f"{m[1]}Wie viel vom Original steckt noch in {ver}?{m[2]}",
        page,
    )

    # 2. per round: measured merged PRs and their surviving lines
    anchors = list(ROUND_ANCHOR.finditer(page))
    rounds = []
    for m in anchors:
        nxt = BOUNDARY.search(page, m.end())
        block = page[m.start() : nxt.start() if nxt else len(page)]
        prs = [int(r["pr"]) for r in ROW.finditer(block) if info[int(r["pr"])][3]]
        if not prs:
            continue
        n = int(m[1])
        th = re.search(
            rf'<a href="#r{n}">Runde {n}</a></td><td>[^<]*</td><td>[^<]*</td><td>([^<]*)</td>', page
        )
        if th:
            thema = th[1]
        else:
            h = re.search(r">Runde \d+: ([^<(]*)", page[m.start() :])
            thema = h[1].strip(" ,") if h else ""
        dates = [day(info[p][0]) for p in prs]
        rounds.append((n, period(dates), thema, len(prs), sum(surv.get(p, {"lines": 0})["lines"] for p in prs)))
    rounds.sort()
    merged_measured = sum(r[3] for r in rounds)
    assert sum(r[4] for r in rounds) == DKPR, "round sums differ from the PR sum"
    top = sorted(surv.items(), key=lambda kv: -kv[1]["lines"])[:10]
    big, fl, ar = S["big_files"], S["files"], S["areas"]

    def kept(f):
        return round(100 * big[f]["kept"] / big[f]["base"])

    # 3. the block from <h2 id="anteil"> to the first round heading
    late_li = ""
    if late:
        verb = "Sie wurden" if len(late) > 1 else "Er wurde"
        late_li = f"<li><strong>Nicht enthalten:</strong> {und([f'#{p}' for p in late])}. {verb} nach der Messung gemergt.</li>\n"
    sec = f"""<h2 id="anteil">Wie viel vom Original steckt noch in {ver}?</h2>
<p><strong>{pc(100 * ORIG / NOW)} % der heutigen Kernfirmware sind noch unverändert die von Februar, {pc(100 * (DKPR + DKX) / NOW)} % stammen aus Commits dieses Forks.</strong> Die Kernfirmware ist von {de(BASE)} Zeilen (vor dem ersten PR, {BASE_DATE}) auf {de(NOW)} Zeilen gewachsen. Stand der Messung: <code>git blame</code> auf {refh}, {stand}.</p>
<div class="stats">
<div class="stat"><div class="k">Ausgangsbasis {BASE_DATE[:6]}</div><div class="v">{de(BASE)}</div></div>
<div class="stat"><div class="k">Heute ({ver})</div><div class="v">{de(NOW)}</div></div>
<div class="stat"><div class="k">Original unverändert</div><div class="v">{de(ORIG)} · {pc(100 * ORIG / NOW)} %</div></div>
<div class="stat"><div class="k">Original überlebt</div><div class="v">{pc(100 * ORIG / BASE)} %</div></div>
</div>
<div class="share" role="img" aria-label="Zusammensetzung der Kernfirmware {ver}: {pc(100 * ORIG / NOW)} % Original, {pc(100 * DKPR / NOW)} % PRs dieses Forks, {pc(100 * DKX / NOW)} % Einzelcommits dieses Forks, {pc(100 * OTH / NOW)} % andere Autoren">
<span class="seg s-orig" style="width:{bw(100 * ORIG / NOW)}%">Original {pc(100 * ORIG / NOW)} %</span><span class="seg s-pr" style="width:{bw(100 * DKPR / NOW)}%">PRs DK5EN {pc(100 * DKPR / NOW)} %</span><span class="seg s-x" style="width:{bw(100 * DKX / NOW)}%"></span><span class="seg s-oth" style="width:{bw(100 * OTH / NOW)}%">andere {pc(100 * OTH / NOW)} %</span>
</div>
<ul>
<li><strong>Original:</strong> Zeilen, die schon vor dem ersten PR in upstream standen.</li>
<li><strong>PRs DK5EN:</strong> Zeilen aus Commits von {S["prs_with_lines"]} der {merged_measured} bis dahin gemergten PRs, die heute noch stehen ({de(DKPR)}).</li>
<li><strong>Einzelcommits ohne PR:</strong> {de(DKX)} Zeilen.</li>
<li><strong>Andere:</strong> Commits anderer Autoren seit Februar, im Wesentlichen das ICSSW-Team.</li>
{late_li}</ul>

<details>
<summary>Je Runde: wie viel der Runde heute noch steht</summary>
<div class="table-wrap"><table class="sh"><thead><tr><th>Runde</th><th class="num">PRs</th><th class="num">Zeilen heute</th><th class="num">Anteil</th></tr></thead><tbody>
"""
    for n, per, thema, np_, sv in rounds:
        sec += f'<tr><td><a href="#r{n}">Runde {n}</a> · {per}<div class="note small">{thema}</div></td><td class="num">{np_}</td><td class="num">{de(sv)}</td><td class="num">{pc(100 * sv / NOW)} %</td></tr>\n'
    sec += f"""</tbody></table></div>
<p>Gezählt werden nur Zeilen, die in den PRs der Runde entstanden und im Stand {stand} noch stehen. Spätere Umbauten nehmen sie der Runde wieder weg. Die Spalte „Zeilen“ in den Rundentabellen zeigt dagegen, was GitHub beim Merge gezählt hat.</p>
</details>

<details>
<summary>Die zehn PRs mit dem größten Anteil in {ver}</summary>
<div class="table-wrap"><table class="sh"><thead><tr><th>PR</th><th>Titel</th><th class="num">Zeilen heute</th><th class="num">Dateien</th></tr></thead><tbody>
"""
    for pr, v in top:
        date, title = info[pr][:2]
        sec += f'<tr><td><a href="{PR_URL}{pr}">#{pr}</a><div class="note small">{date}</div></td><td>{title}</td><td class="num">{de(v["lines"])}</td><td class="num">{v["files"]}</td></tr>\n'
    rest = DKPR - sum(v["lines"] for _, v in top)
    top_pr, top_v = top[0]
    big_txt = ", ".join(f"<code>{name}</code> {kept(f)} %" for name, f in BIG[:-1])
    big_txt += f" und <code>{BIG[-1][0]}</code> {kept(BIG[-1][1])} %"
    sec += f"""</tbody></table></div>
<p>Die übrigen {S["prs_with_lines"] - len(top)} PRs tragen zusammen {de(rest)} Zeilen bei, dazu kommen {de(DKX)} Zeilen aus Einzelcommits ohne PR. PR #{top_pr} allein stellt {de(top_v["lines"])} Zeilen, das sind {pc(100 * top_v["lines"] / NOW)} % der Kernfirmware.</p>
</details>

<details>
<summary>Wo am meisten neu ist: Zeilen heute nach Bereich</summary>
<div class="table-wrap"><table class="sh"><thead><tr><th>Bereich</th><th class="num">Zeilen heute</th><th class="num">davon Original</th></tr></thead><tbody>
"""
    for k in ORDER:
        sec += f'<tr><td>{k}</td><td class="num">{de(ar[k]["lines"])}</td><td class="num">{pc(100 * ar[k]["original"] / ar[k]["lines"])} %</td></tr>\n'
    sec += f"""</tbody></table></div>
<p>Die großen Dateien sind gewachsen, aber ihre alten Zeilen blieben meist stehen: {big_txt} der Zeilen von Februar sind noch da. Das Neue steht überwiegend in neuen Dateien, {fl["new"]} neue gegen {fl["baseline"]} Ausgangsdateien. Vollständig ersetzt wurde nur die MHeard-Liste (<code>mheard_functions.cpp</code>). Von {fl["baseline"]} Dateien aus Februar sind {fl["identical"]} byte-identisch, {fl["modified"]} verändert und {fl["deleted"]} entfallen.</p>
</details>

"""
    if ANTEIL.search(page):
        page = ANTEIL.sub(lambda m: sec, page, count=1)
    else:
        i = anchors[0].start()
        page = page[:i] + sec + page[i:]

    # 4. method details
    method = f"""<details class="dev">
<summary>Methode der Zeilenbilanz</summary>
<p><code>git blame -w</code> auf jeder Datei der handgeschriebenen Kernfirmware von {refh}. Einbezogen sind <code>src/</code> ohne Fonts, Plattform-Stubs, Display- und Treiberbibliotheken, T5- und T-Deck-Pro-Oberfläche und den erzeugten <code>ota.h</code>, dazu <code>variants/</code>, <code>config/</code> und <code>platformio.ini</code>. <code>lib/</code> mit rund {pc(S["lib_lines_ref"] / 1e6)} Millionen Zeilen Fremdcode bleibt außen vor.</p>
<ul>
<li><strong>Ausgangspunkt:</strong> der letzte upstream-Stand vor dem Merge des ersten PR ({S["base"][:8]}).</li>
<li><strong>Original:</strong> eine Zeile, deren letzter Autor-Commit in der Historie dieses Stands liegt. Mit Erkennung verschobener und kopierter Zeilen (<code>-M -C</code>) steigt der Originalanteil auf {pc(100 * S["modes"]["moves"]["original"] / NOW)} %.</li>
<li><strong>Zuordnung zu PRs:</strong> über die Merge-Commits „from DK5EN“. PRs, die das ICSSW-Team unter eigenem Namen übernommen hat, erscheinen unter „andere“ (Autorenzuordnung nach Commit-Adresse).</li>
<li><strong>Zeilen:</strong> rohe Zeilen einschließlich Kommentaren und Leerzeilen. Sie sagen nichts über Verhalten.</li>
<li><strong>Skripte:</strong> <code>tools/code_share/</code>.</li>
</ul>
</details>
"""
    page, n_method = METHOD.subn(lambda m: method, page, count=1)
    if not n_method:
        print("note: no 'Methode der Zeilenbilanz' details block found, left out", file=sys.stderr)
    (d / "pr-history.html").write_text(page, encoding="utf-8")

    # 5. index card
    idx = (d / "index.html").read_text(encoding="utf-8")
    new = f"Mit Zeilenbilanz je PR und je Runde: {pc(100 * ORIG / NOW)} % der heutigen Kernfirmware sind unverändert das Original von Februar (Stand {stand}).</p>"
    idx, n_idx = re.subn(r"Mit Zeilenbilanz je PR und je Runde[^<]*</p>", lambda m: new, idx, count=1)
    assert n_idx == 1, "index card sentence not found"
    (d / "index.html").write_text(idx, encoding="utf-8")
    print(
        f"rows {n_rows}, rounds {len(rounds)}, measured merged PRs {merged_measured}, "
        f"after the measurement {len(late)}, stand {stand}"
    )


if __name__ == "__main__":
    main()
