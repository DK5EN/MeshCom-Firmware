#!/usr/bin/env python3
"""Attribute Claude Code token usage and list-price cost to the rows of docs/campaign-gantt.html.

Reads the local Claude Code transcripts of this repo (~/.claude/projects/*MeshCom-Firmware*/**/*.jsonl,
subagents included), dedupes per API request, prices each request at the API list price and assigns it
to the chart row whose day window (CET, start day 00:00 to the day after the end day) contains it. With
overlapping rows the row that started last wins; rows starting on the same day share a request equally.
Release / upstream-merge milestones (st "rel") and plan rows own no window. Rows that end before the
oldest transcript on this machine get `nolog:1`. Writes `tok:` / `usd:` into each row and the totals into
the USAGE constant, in place. Re-runnable.

Usage: python3 tools/gantt_usage.py [--dry-run]
"""

from __future__ import annotations

import datetime as dt
import glob
import json
import re
import sys
from pathlib import Path

HTML = Path(__file__).resolve().parent.parent / "docs" / "campaign-gantt.html"
ROOT = Path.home() / ".claude" / "projects"
PROJECT_GLOB = "*MeshCom-Firmware*"
# API list prices in $/MTok (input, output, cache read); cache write 5 min = 1.25x input, 1 h = 2x.
PRICE: dict[str, tuple[float, float, float]] = {
    "fable-5-1": (10, 50, 0.25),
    "fable-5": (10, 50, 1.0),
    "opus-5-5": (4, 20, 0.20),
    "opus-5": (5, 25, 0.50),
    "sonnet-5-5": (2, 10, 0.20),
    "sonnet-5": (2, 10, 0.20),
    "haiku-5-5": (0.10, 0.50, 0.01),
}
CET = dt.timezone(dt.timedelta(hours=1))
ROW_RE = re.compile(r'^ \{p:"[^"]*",n:"(?P<n>[^"]*)",id:"[^"]*",st:"(?P<st>\w+)",s:"(?P<s>[\d-]+)",e:"(?P<e>[\d-]+)"')
USAGE_RE = re.compile(r"^const USAGE = \{.*\};$", re.M)


def price(model: str) -> tuple[float, float, float] | None:
    for key in sorted(PRICE, key=len, reverse=True):
        if key in model:
            return PRICE[key]
    return None


def requests() -> list[tuple[dt.datetime, int, float]]:
    seen: set[str] = set()
    out: list[tuple[dt.datetime, int, float]] = []
    for f in glob.glob(f"{ROOT}/{PROJECT_GLOB}/**/*.jsonl", recursive=True):
        with open(f, errors="replace") as fh:
            for line in fh:
                if '"usage"' not in line:
                    continue
                try:
                    d = json.loads(line)
                except json.JSONDecodeError:
                    continue
                m = d.get("message") or {}
                u, model, ts = m.get("usage"), m.get("model") or "", d.get("timestamp") or ""
                key = d.get("requestId") or m.get("id")
                if d.get("type") != "assistant" or not u or not ts or not key or key in seen:
                    continue
                seen.add(key)
                p = price(model)
                if p is None:
                    continue
                cc = u.get("cache_creation") or {}
                w1h = cc.get("ephemeral_1h_input_tokens", 0)
                w5m = cc.get("ephemeral_5m_input_tokens", 0) if cc else u.get("cache_creation_input_tokens", 0)
                inp, outp = u.get("input_tokens", 0), u.get("output_tokens", 0)
                cr = u.get("cache_read_input_tokens", 0)
                usd = (inp * p[0] + outp * p[1] + cr * p[2] + w5m * p[0] * 1.25 + w1h * p[0] * 2) / 1e6
                t = dt.datetime.fromisoformat(ts.replace("Z", "+00:00"))
                out.append((t, inp + outp + cr + w5m + w1h, usd))
    return out


def day(s: str) -> dt.datetime:
    return dt.datetime.strptime(f"2026-{s}", "%Y-%m-%d").replace(tzinfo=CET)


def main() -> int:
    html = HTML.read_text()
    lines = html.split("\n")
    rows: list[dict] = []
    for i, line in enumerate(lines):
        m = ROW_RE.match(line)
        if m:
            rows.append({"i": i, "st": m["st"], "s": day(m["s"]), "e": day(m["e"]) + dt.timedelta(days=1), "tok": 0, "usd": 0.0})
    reqs = requests()
    if not reqs:
        print("no transcripts found", file=sys.stderr)
        return 1
    cover = min(t for t, _, _ in reqs)
    owners = [r for r in rows if r["st"] in ("done", "run")]
    tot_tok, tot_usd, un_tok, un_usd = 0, 0.0, 0, 0.0
    for t, tok, usd in reqs:
        tot_tok += tok
        tot_usd += usd
        hit = [r for r in owners if r["s"] <= t < r["e"]]
        if not hit:
            un_tok += tok
            un_usd += usd
            continue
        last = max(r["s"] for r in hit)
        share = [r for r in hit if r["s"] == last]
        for r in share:
            r["tok"] += tok // len(share)
            r["usd"] += usd / len(share)
    for r in rows:
        line = re.sub(r",(tok|usd|nolog):[^,]*(?=,note:)", "", lines[r["i"]])
        extra = ""
        if r["st"] in ("done", "run"):
            if r["e"] <= cover:
                extra = ",nolog:1"
            else:
                extra = f',tok:{r["tok"]},usd:{r["usd"]:.2f}'
        lines[r["i"]] = line.replace(",note:", extra + ",note:", 1)
    now = dt.datetime.now(CET).strftime("%m-%d %H:%M")
    usage = (f'const USAGE = {{ tok: {tot_tok}, usd: {tot_usd:.2f}, unTok: {un_tok}, unUsd: {un_usd:.2f}, '
             f'since: "{cover.astimezone(CET).strftime("%Y-%m-%d")}", asOf: "{now}" }};')
    out = "\n".join(lines)
    out = USAGE_RE.sub(usage, out) if USAGE_RE.search(out) else out
    print(usage)
    print(f"{len(reqs)} requests, {sum(1 for r in rows if 'nolog' in lines[r['i']])} rows before {cover.date()}")
    if "--dry-run" not in sys.argv:
        HTML.write_text(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
