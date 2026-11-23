#!/usr/bin/env python3
"""OTA-LOWHEAP-01: turn MeshCom INSTRUMENT console logs into a heap timeline report.

Reads meshlogger day logs and trigger-*.log files (one node per run; every line
is prefixed by "YYYY-MM-DD HH:MM:SS[.mmm] ") and writes one self-contained HTML
file (inline SVG, no external assets) with

  1. a timeline chart (ifree, iblk, imin over wall time, steps, AU events, probes)
  2. a steps table (every step down/up with code location and nearest event)
  3. a sites table (who allocates and never frees)
  4. a heap map (last [INSTR-MAP] dump)
  5. a probe table

Recognised lines (fields ';' separated after the tag, or blank separated):
  [INSTR-HEAP] up_s N ifree B imin B iblk B ...
  [AU];...                      (check/install/dl/stage/heap;start|peak|gate|end/refuse)
  [INSTR-STEP];t_ms;..;dir;down|up;ifree;..;iblk;..;task;..;site;..;size;..;op;alloc|free|blk
  [INSTR-SITE];id;..;pcs;0x..,0x..;task;..;live;..;bytes;..;allocs;..;frees;..
  [INSTR-MAP];region;i;start;0x..;end;0x..;size;B
  [INSTR-MAP];blk;addr;0x..;size;B;kind;used|free|gap;site;id|-
  [INSTR-MAP];sum;used;B;untracked;B;free;B;holes;n;largest;B;drops;n
  [INSTR-PROBE];name;id;phase;start|end;ifree;..;iblk;..[;low;..;peak_used;..;delta;..;dt_ms;..;rc;..]
  [INSTR-HW];live;ptr;..;size;..;task;..;pc;..

Usage:
  heaptimeline.py LOG [LOG ...] --out report.html [--csv out.csv] [--step 5120]
                  [--elf firmware.elf] [--addr2line PATH]

Python 3 stdlib only.  PCs are resolved with one batched addr2line -pfiaC call.
"""
from __future__ import annotations

import argparse
import csv
import glob
import html
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path

TS_RE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)(?:\.(\d+))?\s+(.*)$")
TAG_RE = re.compile(r"\[(INSTR-[A-Z]+|AU)\](.*)$")
AU_KINDS = {"check", "install", "dl", "stage", "refuse"}
AU_HEAP_SUBS = {"start", "peak", "gate", "end"}
# frames that only describe the allocator, not the caller
WRAPPER_RE = re.compile(
    r"heap_caps|multi_heap|malloc|calloc|realloc|operator new|instrument|heapwatch|_heap|tlsf", re.I)
PALETTE = ["#2a6fdb", "#1b9e77", "#7b3fc4", "#d6336c", "#3a9d23",
           "#0aa2c0", "#4c4ccf", "#c0392b", "#8e44ad", "#16a085"]



GAP_S = 600.0  # seconds without a heap sample that break the timeline line

def num(s: str | None) -> int | None:
    """Parse decimal or 0x.. integer; None when not a number."""
    if s is None:
        return None
    s = s.strip()
    try:
        return int(s, 0)
    except ValueError:
        try:
            return int(s, 10)
        except ValueError:
            return None


def kv(tokens: list[str]) -> dict[str, str]:
    """Alternating key/value tokens to a dict (a trailing lone key is dropped)."""
    return {tokens[i]: tokens[i + 1] for i in range(0, len(tokens) - 1, 2)}


def split_fields(rest: str) -> list[str]:
    rest = rest.strip()
    toks = rest.split(";") if ";" in rest else rest.split()
    toks = [t.strip() for t in toks]
    while toks and toks[0] == "":
        toks.pop(0)
    return toks


@dataclass
class Sample:
    t: datetime
    up_s: int
    ifree: int
    imin: int
    iblk: int
    boot: int = 0


@dataclass
class Step:
    t: datetime
    kind: str          # ifree | imin | iblk | step
    direction: str     # down | up
    delta: int
    before: int | None
    after: int | None
    task: str = "-"
    site: str = "-"
    op: str = "-"
    ifree: int | None = None
    iblk: int | None = None
    src: str = "heap"  # heap | instr


@dataclass
class Event:
    t: datetime
    label: str
    kind: str          # au | probe | boot
    t_end: datetime | None = None


@dataclass
class Site:
    id: str
    pcs: list[int] = field(default_factory=list)
    task: str = "-"
    live: int = 0
    bytes: int = 0
    allocs: int = 0
    frees: int = 0

    @property
    def never_freed(self) -> bool:
        return self.allocs > 0 and self.frees == 0 and self.live > 0


@dataclass
class Probe:
    name: str
    t_start: datetime | None = None
    t_end: datetime | None = None
    start_ifree: int | None = None
    start_iblk: int | None = None
    end_ifree: int | None = None
    end_iblk: int | None = None
    low: int | None = None
    peak_used: int | None = None
    delta: int | None = None
    dt_ms: int | None = None
    rc: str = "-"


@dataclass
class MapDump:
    regions: list[dict] = field(default_factory=list)
    blocks: list[dict] = field(default_factory=list)
    summary: dict[str, int] = field(default_factory=dict)
    t: datetime | None = None


@dataclass
class Data:
    samples: list[Sample] = field(default_factory=list)
    reboots: list[datetime] = field(default_factory=list)
    steps: list[Step] = field(default_factory=list)
    events: list[Event] = field(default_factory=list)
    sites: dict[str, Site] = field(default_factory=dict)
    probes: list[Probe] = field(default_factory=list)
    map: MapDump | None = None
    hw_pcs: list[int] = field(default_factory=list)
    step_records: list[Step] = field(default_factory=list)


def parse_ts(date_s: str, frac: str | None) -> datetime:
    t = datetime.strptime(date_s, "%Y-%m-%d %H:%M:%S")
    if frac:
        t += timedelta(microseconds=int((frac + "000000")[:6]))
    return t


def iter_lines(paths: list[Path]):
    """Yield (time, tag, tokens) for every recognised line of all files, time sorted."""
    out = []
    n = 0
    for p in paths:
        with open(p, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = TS_RE.match(line.rstrip("\r\n"))
                if not m:
                    continue
                tm = TAG_RE.search(m.group(3))
                if not tm:
                    continue
                n += 1
                out.append((parse_ts(m.group(1), m.group(2)), n, tm.group(1), split_fields(tm.group(2))))
    out.sort(key=lambda r: (r[0], r[1]))
    return [(t, tag, toks) for t, _, tag, toks in out]


def parse_logs(paths: list[Path], step_bytes: int = 5120) -> Data:
    d = Data()
    seen: set[tuple] = set()
    probes: dict[str, Probe] = {}
    cur_map: MapDump | None = None
    map_closed = False
    for t, tag, toks in iter_lines(paths):
        if tag == "INSTR-HEAP":
            f = kv(toks)
            up, ifr, imn, blk = num(f.get("up_s")), num(f.get("ifree")), num(f.get("imin")), num(f.get("iblk"))
            if None in (up, ifr, imn, blk):
                continue
            key = (up, ifr, imn, blk)
            if key in seen:
                continue
            seen.add(key)
            d.samples.append(Sample(t, up, ifr, imn, blk))  # type: ignore[arg-type]
        elif tag == "AU":
            ev = au_event(t, toks)
            if ev:
                d.events.append(ev)
        elif tag == "INSTR-STEP":
            f = kv(toks)
            direction = f.get("dir", "down")
            size = num(f.get("size")) or 0
            op = f.get("op", "-")
            delta = -size if op == "alloc" else size if op == "free" else 0
            if op not in ("alloc", "free"):
                delta = -size if direction == "down" else size
            d.step_records.append(Step(t, "step", direction, delta, None, num(f.get("ifree")),
                                       f.get("task", "-"), f.get("site", "-"), op,
                                       num(f.get("ifree")), num(f.get("iblk")), "instr"))
        elif tag == "INSTR-SITE":
            f = kv(toks)
            sid = f.get("id")
            if sid is None:
                continue
            pcs = [x for x in (num(p) for p in f.get("pcs", "").split(",")) if x is not None]
            d.sites[sid] = Site(sid, pcs, f.get("task", "-"), num(f.get("live")) or 0,
                                num(f.get("bytes")) or 0, num(f.get("allocs")) or 0, num(f.get("frees")) or 0)
        elif tag == "INSTR-MAP" and toks:
            kind, f = toks[0], kv(toks[1:])
            if kind in ("region", "blk") and (cur_map is None or map_closed):
                cur_map, map_closed = MapDump(t=t), False
                d.map = cur_map
            if cur_map is None:
                continue
            if kind == "region":
                f = kv(toks)
                cur_map.regions.append({"i": f.get("region"), "start": num(f.get("start")),
                                        "end": num(f.get("end")), "size": num(f.get("size"))})
            elif kind == "blk":
                cur_map.blocks.append({"addr": num(f.get("addr")), "size": num(f.get("size")) or 0,
                                       "kind": f.get("kind", "used"), "site": f.get("site", "-")})
            elif kind == "sum":
                cur_map.summary = {k: v for k, v in ((k, num(x)) for k, x in f.items()) if v is not None}
                map_closed = True
        elif tag == "INSTR-PROBE":
            f = kv(toks)
            name = f.get("name", "?")
            if f.get("phase") == "start":
                pr = Probe(name, t_start=t, start_ifree=num(f.get("ifree")), start_iblk=num(f.get("iblk")))
                probes[name] = pr
                d.probes.append(pr)
            elif f.get("phase") == "end":
                pr = probes.get(name)
                if pr is None or pr.t_end is not None:
                    pr = Probe(name)
                    d.probes.append(pr)
                pr.t_end = t
                pr.end_ifree, pr.end_iblk = num(f.get("ifree")), num(f.get("iblk"))
                pr.low, pr.peak_used = num(f.get("low")), num(f.get("peak_used"))
                pr.delta, pr.dt_ms, pr.rc = num(f.get("delta")), num(f.get("dt_ms")), f.get("rc", "-")
        elif tag == "INSTR-HW":
            f = kv(toks[1:] if toks and toks[0] == "live" else toks)
            pc = num(f.get("pc"))
            if pc is not None:
                d.hw_pcs.append(pc)
    d.samples.sort(key=lambda s: s.t)
    mark_boots(d)
    for pr in d.probes:
        if pr.t_start:
            d.events.append(Event(pr.t_start, f"probe {pr.name}", "probe", pr.t_end))
    for b in d.reboots:
        d.events.append(Event(b, "reboot", "boot"))
    d.events.sort(key=lambda e: e.t)
    d.steps = d.step_records if d.step_records else derive_steps(d.samples, step_bytes)
    return d


def au_event(t: datetime, toks: list[str]) -> Event | None:
    if not toks:
        return None
    k = toks[0]
    if k in AU_KINDS or (k == "heap" and len(toks) > 1 and toks[1] in AU_HEAP_SUBS):
        label = "AU " + " ".join(toks[:4] if k != "check" else toks[:3])
        return Event(t, label, "au")
    return None


def mark_boots(d: Data) -> None:
    boot = 0
    prev: Sample | None = None
    for s in d.samples:
        if prev is not None and s.up_s < prev.up_s:
            boot += 1
            d.reboots.append(s.t)
        s.boot = boot
        prev = s


def derive_steps(samples: list[Sample], step: int) -> list[Step]:
    out: list[Step] = []
    for a, b in zip(samples, samples[1:]):
        if a.boot != b.boot:
            continue
        for kind in ("ifree", "imin", "iblk"):
            va, vb = getattr(a, kind), getattr(b, kind)
            if abs(vb - va) >= step:
                out.append(Step(b.t, kind, "down" if vb < va else "up", vb - va, va, vb,
                                ifree=b.ifree, iblk=b.iblk))
    return out


# ---------------------------------------------------------------- addr2line

def find_addr2line() -> str | None:
    c = sorted(glob.glob(str(Path.home() / ".platformio/packages/*/bin/xtensa-esp32s3-elf-addr2line")))
    return c[0] if c else None


def resolve_pcs(pcs: set[int], elf: str, tool: str | None) -> dict[int, str]:
    """One batched addr2line -pfiaC call; returns pc -> 'func file:line' (inlines joined by ' < ')."""
    tool = tool or find_addr2line()
    if not tool or not pcs:
        return {}
    order = sorted(pcs)
    try:
        r = subprocess.run([tool, "-pfiaC", "-e", elf] + [hex(p) for p in order],
                           capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.SubprocessError) as e:
        print(f"heaptimeline: addr2line failed: {e}", file=sys.stderr)
        return {}
    return parse_addr2line(r.stdout)


def short_path(s: str) -> str:
    return re.sub(r"\S*?/((?:[^/\s]+/)?[^/\s]+:\d+)", r"\1", s)


def parse_addr2line(text: str) -> dict[int, str]:
    res: dict[int, list[str]] = {}
    cur: int | None = None
    for ln in text.splitlines():
        m = re.match(r"^(0x[0-9a-fA-F]+):\s*(.*)$", ln)
        if m:
            cur = int(m.group(1), 16)
            res[cur] = [m.group(2)]
        elif cur is not None and ln.strip():
            res[cur].append(ln.strip())
    out = {}
    for pc, parts in res.items():
        parts = [short_path(re.sub(r"^\(inlined by\)\s*", "", p).replace(" at ", " ")) for p in parts]
        out[pc] = " < ".join(parts)
    return out


def site_location(site: Site, loc: dict[int, str]) -> str:
    """First resolved frame that is not allocator plumbing."""
    cands = [loc[p] for p in site.pcs if p in loc]
    for c in cands:
        if not WRAPPER_RE.search(c.split(" ")[0]):
            return c
    return cands[0] if cands else ""


# ---------------------------------------------------------------- rendering

def kb(v: int | None) -> str:
    return "-" if v is None else f"{v / 1024:.1f} KB"


def sgn(v: int) -> str:
    return f"{'+' if v > 0 else ''}{v}"


def tfmt(t: datetime) -> str:
    return t.strftime("%Y-%m-%d %H:%M:%S")


def nearest_event(t: datetime, events: list[Event], window: float = 120.0) -> str:
    best, bd = None, None
    for e in events:
        if e.kind == "boot":
            continue
        dt = (t - e.t).total_seconds()
        if bd is None or abs(dt) < abs(bd):
            best, bd = e, dt
    if best is None or bd is None or abs(bd) > window:
        return "-"
    return f"{best.label} ({bd:+.0f}s)"


CSS = """
:root{--bg:#fff;--fg:#1c1c1e;--mut:#666;--line:#c8ccd0;--c-free:#2a6fdb;--c-blk:#1b9e77;--c-min:#c0392b;
--ev:#7b3fc4;--step:#d6336c;--card:#f5f6f8}
@media (prefers-color-scheme:dark){:root{--bg:#16181c;--fg:#e6e8eb;--mut:#9aa0a6;--line:#3a3f45;
--c-free:#6ea8ff;--c-blk:#4fd1a5;--c-min:#ff7b72;--ev:#c4a1ff;--step:#ff8fb8;--card:#20242a}}
body{background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,"Segoe UI",Helvetica,Arial,sans-serif;
margin:1.5em;font-size:14px}
h1{font-size:20px}h2{font-size:16px;margin-top:2em}
table{border-collapse:collapse;font-size:13px}th,td{border-bottom:1px solid var(--line);padding:3px 10px;text-align:left}
td.n,th.n{text-align:right;font-variant-numeric:tabular-nums}
.flag{color:var(--c-min);font-weight:600}.mut{color:var(--mut)}
svg text{fill:var(--fg);font-size:11px;font-family:inherit}svg .mut{fill:var(--mut)}
svg .ax{stroke:var(--line);stroke-width:1}
"""


def chart_svg(d: Data, loc: dict[int, str]) -> str:
    W, H, ml, mr, mt, mb = 1100, 400, 60, 20, 30, 40
    pw, ph = W - ml - mr, H - mt - mb
    if not d.samples:
        return "<p class='mut'>no [INSTR-HEAP] samples</p>"
    times = [s.t for s in d.samples] + [e.t for e in d.events] + [s.t for s in d.steps]
    t0, t1 = min(times), max(times)
    span = max((t1 - t0).total_seconds(), 1.0)
    ymax = max(max(s.ifree, s.iblk, s.imin) for s in d.samples) * 1.08

    def X(t: datetime) -> float:
        return ml + (t - t0).total_seconds() / span * pw

    def Y(v: float) -> float:
        return mt + ph - v / ymax * ph

    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="100%" role="img" '
         f'aria-label="Heap timeline">']
    for i in range(6):
        v = ymax * i / 5
        o.append(f'<line class="ax" x1="{ml}" x2="{W - mr}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke-dasharray="2 4"/>')
        o.append(f'<text class="mut" x="{ml - 6}" y="{Y(v) + 4:.1f}" text-anchor="end">{v / 1024:.0f} KB</text>')
    for i in range(7):
        t = t0 + timedelta(seconds=span * i / 6)
        fmt = "%H:%M" if span < 86400 else "%m-%d %H:%M"
        o.append(f'<text class="mut" x="{X(t):.1f}" y="{H - mb + 16}" text-anchor="middle">{t.strftime(fmt)}</text>')
    o.append(f'<line class="ax" x1="{ml}" x2="{ml}" y1="{mt}" y2="{mt + ph}"/>'
             f'<line class="ax" x1="{ml}" x2="{W - mr}" y1="{mt + ph}" y2="{mt + ph}"/>')
    # probe runs
    for e in d.events:
        if e.kind == "probe":
            x0, x1 = X(e.t), X(e.t_end or e.t)
            o.append(f'<rect x="{x0:.1f}" y="{mt}" width="{max(x1 - x0, 2):.1f}" height="{ph}" fill="var(--ev)" '
                     f'opacity="0.15"><title>{html.escape(e.label)} {tfmt(e.t)}</title></rect>')
    # series per boot segment
    for kind, col in (("iblk", "var(--c-blk)"), ("imin", "var(--c-min)"), ("ifree", "var(--c-free)")):
        for boot in sorted({s.boot for s in d.samples}):
            seg = [s for s in d.samples if s.boot == boot]
            # A logging gap (logger down, PAUSE) is drawn as a gap, not as a straight line.
            path = "".join(("M" if i == 0 or (seg[i].t - seg[i - 1].t).total_seconds() > GAP_S else " L")
                           + f"{X(s.t):.1f},{Y(getattr(s, kind)):.1f}" for i, s in enumerate(seg))
            o.append(f'<path d="{path}" fill="none" stroke="{col}" stroke-width="1.6"/>')
    for s in d.samples:
        o.append(f'<circle cx="{X(s.t):.1f}" cy="{Y(s.ifree):.1f}" r="2" fill="var(--c-free)">'
                 f'<title>{tfmt(s.t)} up_s {s.up_s}: ifree {s.ifree} imin {s.imin} iblk {s.iblk}</title></circle>')
    # reboots and AU events
    row = 0
    for e in d.events:
        if e.kind == "probe":
            continue
        x = X(e.t)
        if e.kind == "boot":
            o.append(f'<line x1="{x:.1f}" x2="{x:.1f}" y1="{mt}" y2="{mt + ph}" stroke="var(--fg)" '
                     f'stroke-dasharray="5 3"><title>reboot {tfmt(e.t)}</title></line>'
                     f'<text x="{x + 3:.1f}" y="{mt + ph - 4}">reboot</text>')
        else:
            y = mt + 10 + (row % 4) * 11
            row += 1
            o.append(f'<line x1="{x:.1f}" x2="{x:.1f}" y1="{mt}" y2="{mt + ph}" stroke="var(--ev)" opacity="0.5">'
                     f'<title>{tfmt(e.t)} {html.escape(e.label)}</title></line>'
                     f'<text x="{x + 3:.1f}" y="{y}" fill="var(--ev)" style="fill:var(--ev)">{html.escape(e.label)}</text>')
    # steps
    for s in d.steps:
        yv = {"ifree": s.after, "imin": s.after, "iblk": s.after}.get(s.kind, s.ifree)
        if yv is None:
            yv = s.ifree or 0
        tip = (f"{tfmt(s.t)} {s.kind} {s.direction} {sgn(s.delta)} B -> {yv} | site {s.site} task {s.task} "
               f"{html.escape(step_loc(s, d, loc))}")
        o.append(f'<circle cx="{X(s.t):.1f}" cy="{Y(yv):.1f}" r="5" fill="none" stroke="var(--step)" stroke-width="2">'
                 f'<title>{tip}</title></circle>')
    lx = ml + 10
    for i, (lab, col) in enumerate((("ifree", "var(--c-free)"), ("iblk", "var(--c-blk)"), ("imin", "var(--c-min)"),
                                    ("step", "var(--step)"))):
        o.append(f'<rect x="{lx + i * 70}" y="6" width="10" height="10" fill="{col}"/>'
                 f'<text x="{lx + i * 70 + 14}" y="15">{lab}</text>')
    o.append("</svg>")
    return "".join(o)


def step_loc(s: Step, d: Data, loc: dict[int, str]) -> str:
    site = d.sites.get(s.site)
    return site_location(site, loc) if site else ""


def steps_table(d: Data, loc: dict[int, str]) -> str:
    if not d.steps:
        return "<p class='mut'>no steps</p>"
    r = ["<table><tr><th>time</th><th>kind</th><th>dir</th><th class='n'>delta</th><th class='n'>from</th>"
         "<th class='n'>to</th><th>task</th><th>site</th><th>location</th><th>nearest event</th></tr>"]
    for s in d.steps:
        r.append(f"<tr><td>{tfmt(s.t)}</td><td>{s.kind}</td><td>{s.direction}</td><td class='n'>{sgn(s.delta)}</td>"
                 f"<td class='n'>{'-' if s.before is None else s.before}</td>"
                 f"<td class='n'>{'-' if s.after is None else s.after}</td>"
                 f"<td>{html.escape(s.task)}</td><td>{html.escape(s.site)}</td>"
                 f"<td>{html.escape(step_loc(s, d, loc))}</td><td>{html.escape(nearest_event(s.t, d.events))}</td></tr>")
    r.append("</table>")
    return "".join(r)


def sites_table(d: Data, loc: dict[int, str]) -> str:
    if not d.sites:
        return "<p class='mut'>no [INSTR-SITE] records</p>"
    r = ["<table><tr><th>site</th><th>task</th><th class='n'>live bytes</th><th class='n'>live</th>"
         "<th class='n'>allocs</th><th class='n'>frees</th><th>never freed</th><th>location</th></tr>"]
    for s in sorted(d.sites.values(), key=lambda s: -s.bytes):
        pcs = ", ".join(hex(p) for p in s.pcs)
        where = site_location(s, loc) or pcs
        r.append(f"<tr><td>{html.escape(s.id)}</td><td>{html.escape(s.task)}</td><td class='n'>{s.bytes}</td>"
                 f"<td class='n'>{s.live}</td><td class='n'>{s.allocs}</td><td class='n'>{s.frees}</td>"
                 f"<td>{'<span class=flag>yes</span>' if s.never_freed else ''}</td>"
                 f"<td title='{html.escape(pcs)}'>{html.escape(where)}</td></tr>")
    r.append("</table>")
    return "".join(r)


def map_svg(d: Data, loc: dict[int, str]) -> str:
    m = d.map
    if m is None:
        return "<p class='mut'>no [INSTR-MAP] dump</p>"
    regions = list(m.regions)
    if not regions and m.blocks:
        a = [b["addr"] for b in m.blocks if b["addr"] is not None]
        e = [b["addr"] + b["size"] for b in m.blocks if b["addr"] is not None]
        regions = [{"i": "0", "start": min(a), "end": max(e), "size": max(e) - min(a)}]
    used: dict[str, int] = {}
    for b in m.blocks:
        if b["kind"] == "used" and b["site"] != "-":
            used[b["site"]] = used.get(b["site"], 0) + b["size"]
    top = [s for s, _ in sorted(used.items(), key=lambda kv_: -kv_[1])[:10]]
    colour = {s: PALETTE[i] for i, s in enumerate(top)}
    W, ml, bh = 1100, 20, 34
    H = 30 + len(regions) * (bh + 30) + 40
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="100%" role="img" aria-label="Heap map">']
    pw = W - 2 * ml
    for ri, rg in enumerate(regions):
        y = 30 + ri * (bh + 30)
        start, end = rg["start"], rg["end"]
        if start is None or end is None or end <= start:
            continue
        o.append(f'<text x="{ml}" y="{y - 6}">region {html.escape(str(rg["i"]))}: {start:#x}-{end:#x} '
                 f'({end - start} B)</text>')
        o.append(f'<rect x="{ml}" y="{y}" width="{pw}" height="{bh}" fill="var(--card)" stroke="var(--line)"/>')
        for b in m.blocks:
            a = b["addr"]
            if a is None or not (start <= a < end):
                continue
            x = ml + (a - start) / (end - start) * pw
            w = max(b["size"] / (end - start) * pw, 1.0)
            site = b["site"]
            site_o = d.sites.get(site)
            where = site_location(site_o, loc) if site_o else ""
            tip = f"{a:#x} size {b['size']} {b['kind']} site {site} {where}"
            if b["kind"] == "free":
                fill, stroke = "var(--bg)", "var(--fg)"
            elif b["kind"] == "gap":   # between tracked blocks: free or untracked used memory
                fill, stroke = "var(--bg)", "#8b9097"
            else:
                fill, stroke = colour.get(site, "#8b9097"), "none"
            o.append(f'<rect x="{x:.2f}" y="{y}" width="{w:.2f}" height="{bh}" fill="{fill}" stroke="{stroke}" '
                     f'stroke-width="0.6"><title>{html.escape(tip)}</title></rect>')
    ly = H - 24
    for i, s in enumerate(top):
        o.append(f'<rect x="{ml + i * 100}" y="{ly}" width="10" height="10" fill="{colour[s]}"/>'
                 f'<text x="{ml + i * 100 + 14}" y="{ly + 9}">site {html.escape(s)}</text>')
    o.append(f'<rect x="{ml + len(top) * 100}" y="{ly}" width="10" height="10" fill="#8b9097"/>'
             f'<text x="{ml + len(top) * 100 + 14}" y="{ly + 9}">other / untracked</text>')
    o.append(f'<rect x="{ml + len(top) * 100 + 130}" y="{ly}" width="10" height="10" fill="var(--bg)" stroke="var(--fg)"/>'
             f'<text x="{ml + len(top) * 100 + 144}" y="{ly + 9}">free hole</text>')
    o.append("</svg>")
    sm = m.summary
    line = ("<p>" + ", ".join(f"{k} {v}" for k, v in sm.items()) + "</p>") if sm else "<p class='mut'>no sum line</p>"
    return line + "".join(o)


def probe_table(d: Data) -> str:
    if not d.probes:
        return "<p class='mut'>no [INSTR-PROBE] records</p>"
    r = ["<table><tr><th>probe</th><th>start</th><th class='n'>start ifree</th><th class='n'>end ifree</th>"
         "<th class='n'>low</th><th class='n'>peak used</th><th class='n'>delta</th><th class='n'>dt ms</th>"
         "<th>rc</th></tr>"]

    def c(v: int | None) -> str:
        return f"<td class='n'>{'-' if v is None else v}</td>"
    for p in d.probes:
        r.append(f"<tr><td>{html.escape(p.name)}</td><td>{tfmt(p.t_start) if p.t_start else '-'}</td>"
                 f"{c(p.start_ifree)}{c(p.end_ifree)}{c(p.low)}{c(p.peak_used)}{c(p.delta)}{c(p.dt_ms)}"
                 f"<td>{html.escape(p.rc)}</td></tr>")
    r.append("</table>")
    return "".join(r)


def render_html(d: Data, loc: dict[int, str], title: str = "Heap timeline") -> str:
    return ("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            f"<title>{html.escape(title)}</title><style>{CSS}</style></head><body>"
            f"<h1>{html.escape(title)}</h1><p class='mut'>{len(d.samples)} heap samples, {len(d.reboots)} reboot(s), "
            f"{len(d.steps)} step(s), {len(d.sites)} site(s), {len(d.probes)} probe(s)</p>"
            f"<h2>Timeline</h2>{chart_svg(d, loc)}"
            f"<h2>Steps</h2>{steps_table(d, loc)}"
            f"<h2>Allocation sites (never-freed flagged)</h2>{sites_table(d, loc)}"
            f"<h2>Heap map</h2>{map_svg(d, loc)}"
            f"<h2>Probes</h2>{probe_table(d)}"
            "</body></html>")


def write_csv(d: Data, path: Path) -> None:
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["time", "up_s", "ifree", "imin", "iblk"])
        for s in d.samples:
            w.writerow([tfmt(s.t), s.up_s, s.ifree, s.imin, s.iblk])


def summary_text(d: Data) -> str:
    L = [f"samples {len(d.samples)}, reboots {len(d.reboots)}, steps {len(d.steps)}, sites {len(d.sites)}, "
         f"probes {len(d.probes)}"]
    for s in d.steps:
        L.append(f"  {tfmt(s.t)} {s.kind:5} {s.direction:4} {sgn(s.delta):>8} B "
                 f"{'' if s.before is None else s.before}->{'' if s.after is None else s.after}"
                 f" task {s.task} site {s.site} near {nearest_event(s.t, d.events)}")
    nf = [s.id for s in d.sites.values() if s.never_freed]
    if nf:
        L.append("never freed sites: " + ", ".join(nf))
    if d.map and d.map.summary:
        L.append("map: " + ", ".join(f"{k} {v}" for k, v in d.map.summary.items()))
    return "\n".join(L)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("logs", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, help="HTML report")
    ap.add_argument("--csv", type=Path, help="CSV time,up_s,ifree,imin,iblk")
    ap.add_argument("--step", type=int, default=5120, help="derived step threshold in bytes (default 5120)")
    ap.add_argument("--elf", help="firmware ELF for PC resolution")
    ap.add_argument("--addr2line", help="addr2line binary (default: xtensa-esp32s3-elf-addr2line from PlatformIO)")
    a = ap.parse_args(argv)
    for p in a.logs:
        if not p.is_file():
            print(f"heaptimeline: no such file: {p}", file=sys.stderr)
            return 2
    d = parse_logs(a.logs, a.step)
    loc: dict[int, str] = {}
    if a.elf:
        pcs = {p for s in d.sites.values() for p in s.pcs} | set(d.hw_pcs)
        loc = resolve_pcs(pcs, a.elf, a.addr2line)
        if pcs and not loc:
            print("heaptimeline: no PCs resolved (addr2line missing or failed)", file=sys.stderr)
    if a.out:
        a.out.write_text(render_html(d, loc, "Heap timeline: " + ", ".join(p.name for p in a.logs)),
                         encoding="utf-8")
    if a.csv:
        write_csv(d, a.csv)
    print(summary_text(d))
    return 0


if __name__ == "__main__":
    sys.exit(main())
