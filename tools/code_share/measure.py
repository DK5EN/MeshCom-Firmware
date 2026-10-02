#!/usr/bin/env python3
"""How much of the pre-fork firmware is still in a given ref?

    python3 tools/code_share/measure.py [--ref upstream/dev] [--base <sha>] \
        [--out build/code-share]

Runs `git blame -w` over every file of the hand-written core of --ref and counts,
per line, whether the commit that last touched it is part of the history of the
baseline (original), was authored by DK5EN (via a merged DK5EN pull request, or a
direct commit) or by someone else. A second pass with move/copy detection (-M -C)
gives the generous original share. Writes <out>/summary.json, which
tools/code_share/render_pages.py turns into the pr-history page.

Core = src/ (without vendored or generated code: Fonts, Platforms, GFX_Root,
Displays, SDWrapper, the T5 and T-Deck-Pro UIs, the generated ota.h), variants/,
config/, platformio.ini. lib/ is third-party and only counted for the note on the
page.

Baseline default: the first parent of the oldest merge whose subject says "from
DK5EN", i.e. upstream as it was right before the first PR of this fork.
Authorship is by commit e-mail (--author-mail, default air-maxx.net), so a PR that
upstream re-authored counts as "other". Stdlib only; needs git and a few minutes
for the blame passes.
"""

import argparse
import collections
import concurrent.futures as cf
import functools
import json
import re
import subprocess
import sys
from pathlib import Path

EXCL = (
    "src/t5-epaper/",
    "src/Fonts/",
    "src/Platforms/",
    "src/GFX_Root/",
    "src/Displays/",
    "src/t-deck-pro/",
    "src/SDWrapper/",
    "lib/",
)
BIG_FILES = [
    "src/command_functions.cpp",
    "src/loop_functions.cpp",
    "src/lora_functions.cpp",
    "src/esp32/esp32_main.cpp",
    "src/nrf52/nrf52_main.cpp",
    "src/web_functions/web_functions.cpp",
    "src/aprs_functions.cpp",
    "src/udp_functions.cpp",
    "src/mheard_functions.cpp",
]


def in_core(p):
    if p.startswith(EXCL) or p == "src/safeboot/ota.h":
        return False
    return p.startswith(("src/", "variants/", "config/")) or p == "platformio.ini"


def area(f):
    p = f.split("/")
    if f.startswith("src/") and len(p) == 2:
        return "src/ Kernlogik (Funk, Mesh, Kommandos, APRS)"
    return "src/" + p[1] if f.startswith("src/") else p[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", default=".")
    ap.add_argument("--ref", default="upstream/dev")
    ap.add_argument(
        "--base",
        default=None,
        help="baseline commit (default: before the first 'from DK5EN' merge)",
    )
    ap.add_argument("--author-mail", default="air-maxx.net")
    ap.add_argument("--out", default="build/code-share")
    ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()
    repo = a.repo

    def git(*args):
        r = subprocess.run(
            ["git", "-C", repo, *args], capture_output=True, text=True, errors="replace"
        )
        return r.stdout

    merges = [
        line.split("|", 2)
        for line in git(
            "log",
            a.ref,
            "--merges",
            "--format=%H|%ad|%s",
            "--date=short",
            "--grep=from DK5EN",
        ).splitlines()
    ]
    prs = []
    for h, d, s in merges:
        m = re.search(r"#(\d+) from (\S+)", s)
        if m:
            prs.append((h, d, int(m.group(1)), m.group(2)))
    if not prs:
        sys.exit("no 'from DK5EN' merge commits found on " + a.ref)
    base = (
        a.base or git("rev-parse", prs[-1][0] + "^1").strip()
    )  # git log lists newest first
    base_set = set(git("rev-list", base).split())

    def tree(ref):
        out = {}
        for line in git("ls-tree", "-r", ref).splitlines():
            meta, path = line.split("	", 1)
            out[path] = meta.split()[2]
        return out

    tb, th = tree(base), tree(a.ref)
    fb = sorted(p for p in tb if in_core(p))
    fh = sorted(p for p in th if in_core(p))

    def nlines(blob):
        b = subprocess.run(
            ["git", "-C", repo, "cat-file", "blob", blob], capture_output=True
        ).stdout
        return b.count(b"\n") + (1 if b and not b.endswith(b"\n") else 0)

    def blame(f, moves):
        out = git(
            "blame",
            "--line-porcelain",
            "-w",
            *(["-M", "-C"] if moves else []),
            a.ref,
            "--",
            f,
        )
        res, cur, au = [], None, None
        for line in out.splitlines():
            m = re.match(r"^([0-9a-f]{40}) \d+ \d+", line)
            if m:
                cur = m.group(1)
                continue
            if line.startswith("author-mail "):
                au = line[12:]
                continue
            if line.startswith("\t"):
                res.append((cur, au))
        return f, res

    def is_dk(au):
        return bool(au) and a.author_mail in au

    modes, per_file, cnt, auth, filesby = (
        {},
        {},
        collections.Counter(),
        {},
        collections.defaultdict(set),
    )
    for mode in ("strict", "moves"):
        t = [0, 0, 0, 0]
        with cf.ThreadPoolExecutor(a.jobs) as ex:
            for f, res in ex.map(functools.partial(blame, moves=(mode == "moves")), fh):
                b = sum(1 for c, au in res if c in base_set)
                d = sum(1 for c, au in res if c not in base_set and is_dk(au))
                o = len(res) - b - d
                t = [t[0] + len(res), t[1] + b, t[2] + d, t[3] + o]
                if mode == "strict":
                    per_file[f] = (len(res), b, d, o)
                    for c, au in res:
                        if c not in base_set:
                            cnt[c] += 1
                            auth[c] = au
                            filesby[c].add(f)
        modes[mode] = dict(zip(("lines", "original", "dk5en", "other"), t, strict=True))
        print(mode, modes[mode], flush=True)

    c2pr = {}
    for h, d, n, br in sorted(prs, key=lambda x: x[2], reverse=True):
        for c in git("rev-list", f"{h}^1..{h}^2").split():
            c2pr.setdefault(c, (n, d, br))
    per_pr = collections.defaultdict(lambda: {"lines": 0, "files": set()})
    direct = 0
    for c, n in cnt.items():
        if not is_dk(auth[c]):
            continue
        if c in c2pr:
            e = per_pr[c2pr[c][0]]
            e["lines"] += n
            e["files"].update(filesby[c])
        else:
            direct += n
    areas = collections.defaultdict(lambda: [0, 0])
    for f, (n, b, *_) in per_file.items():
        areas[area(f)][0] += n
        areas[area(f)][1] += b
    big = {}
    for f in BIG_FILES:
        n, b = per_file.get(f, (0, 0, 0, 0))[:2]
        big[f] = {"base": nlines(tb[f]) if f in tb else 0, "now": n, "kept": b}
    both = set(fb) & set(fh)
    summary = {
        "ref": a.ref,
        "ref_sha": git("rev-parse", a.ref).strip(),
        "base": base,
        "base_commits": len(base_set),
        "base_lines": sum(nlines(tb[f]) for f in fb),
        "now_lines": sum(v[0] for v in per_file.values()),
        "modes": modes,
        "dk5en_pr_lines": sum(v["lines"] for v in per_pr.values()),
        "dk5en_direct_lines": direct,
        "prs_with_lines": len(per_pr),
        "per_pr": {
            str(n): {"lines": v["lines"], "files": len(v["files"])}
            for n, v in per_pr.items()
        },
        "files": {
            "baseline": len(fb),
            "now": len(fh),
            "deleted": len(set(fb) - set(fh)),
            "identical": sum(1 for f in both if tb[f] == th[f]),
            "modified": sum(1 for f in both if tb[f] != th[f]),
            "new": len(set(fh) - set(fb)),
        },
        "areas": {k: {"lines": v[0], "original": v[1]} for k, v in areas.items()},
        "big_files": big,
        "lib_lines_ref": sum(nlines(th[p]) for p in th if p.startswith("lib/")),
    }
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "summary.json").write_text(json.dumps(summary, indent=1, sort_keys=True))
    print("wrote", out / "summary.json")


if __name__ == "__main__":
    main()
