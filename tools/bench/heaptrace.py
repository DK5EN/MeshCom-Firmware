#!/usr/bin/env python3
"""Decode the OTA-LOWHEAP-01 heap allocation trace into a live heap per caller.

Input: serial capture log; lines "[HT];a|r|f|m;..." (optional timestamp prefix).
Usage: heaptrace.py <log> --elf fw.elf [--toolchain-prefix xtensa-esp32s3-elf-]
       [--at NAME ...] [--top 40] [--skip-fn REGEX] [--json out.json]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Optional

DEFAULT_SKIP_FN = (
    r"malloc|calloc|realloc|heap_caps|\bnew\b|operator new|strdup|String::|std::"
    r"|mem_malloc|pbuf_alloc"
)
# Boot markers: "rst:0x" anywhere (ROM reset line, "... rst:0x1 (POWERON)"), "ESP-ROM:" only as the
# first token after the capture timestamp (a log line merely containing "first:" or the like is no boot).
RST_MARKER = re.compile(r"(?<![A-Za-z0-9_])rst:0x")  # not the tail of "first:0x"
ROM_MARKER = "ESP-ROM:"
_TS_PREFIX = re.compile(
    r"^\s*(?:\[?(?:\d{4}-\d{2}-\d{2}[ T])?\d{2}:\d{2}:\d{2}(?:[.,]\d+)?\]?)?[\s>]*")
# addr2line is queried at pc - RET_ADJ: a saved PC is a return address, it points after the call
RET_ADJ = 3
UNKNOWN = ("??", "??:0")

PcInfo = tuple[str, str]  # (function, file:line)


@dataclass
class Block:
    size: int
    task: str
    pcs: tuple[int, ...]
    t: int


@dataclass
class Marker:
    name: str
    t: int
    ifree: int
    iblk: int
    drops: int
    live: dict[int, Block]  # snapshot at marker time


@dataclass
class Segment:
    index: int
    live: dict[int, Block] = field(default_factory=dict)
    markers: list[Marker] = field(default_factory=list)
    unmatched_free: int = 0
    double_alloc: int = 0
    failed_realloc: int = 0
    unmatched_realloc: int = 0
    shadowed: int = 0  # allocs that hit a live address (displaced block kept in `shadow`)
    untraced_bytes: int = 0  # sum of "[HT];i;<t>;drain;stack;<bytes>" lines
    has_ht: bool = False  # any [HT] line seen in this segment
    # displaced blocks per address, oldest first; the next free/realloc-old releases these first
    shadow: dict[int, list[Block]] = field(default_factory=dict)


def _hex(s: str) -> int:
    try:
        return int(s, 16)
    except ValueError:
        return 0


def _int(s: str) -> int:
    try:
        return int(s)
    except ValueError:
        return 0


def _is_boot_marker(raw: str) -> bool:
    if RST_MARKER.search(raw):
        return True
    return raw[_TS_PREFIX.match(raw).end():].startswith(ROM_MARKER)  # type: ignore[union-attr]


def _put(seg: Segment, ptr: int, blk: Block) -> None:
    """Make blk the owner of ptr; a displaced live block is kept as shadow (address-reuse race)."""
    old = seg.live.get(ptr)
    if old is not None:
        seg.double_alloc += 1
        seg.shadowed += 1
        seg.shadow.setdefault(ptr, []).append(old)
    seg.live[ptr] = blk


def _release(seg: Segment, ptr: int) -> bool:
    """Free ptr: the oldest shadowed block goes first (its free/realloc-old is the late one), else the owner."""
    sh = seg.shadow.get(ptr)
    if sh:
        sh.pop(0)
        if not sh:
            del seg.shadow[ptr]
        return True
    return seg.live.pop(ptr, None) is not None


def _release_owner(seg: Segment, ptr: int) -> bool:
    """Plain free: written before the real free, so never late; it releases the live owner, never a shadow."""
    return seg.live.pop(ptr, None) is not None


def parse_log(lines: list[str]) -> list[Segment]:
    segs = [Segment(0)]
    for raw in lines:
        i = raw.find("[HT];")
        if i < 0:
            # consecutive boot markers without [HT] lines in between make no empty segment
            if _is_boot_marker(raw) and segs[-1].has_ht:
                segs.append(Segment(len(segs)))
            continue
        f = raw[i:].strip().split(";")
        if len(f) < 4:
            continue
        seg = segs[-1]
        seg.has_ht = True
        kind = f[1]
        t = _int(f[2])
        if kind == "i":  # info line; only "drain;stack;<bytes>" is known, others are ignored
            if len(f) >= 6 and f[3] == "drain" and f[4] == "stack":
                seg.untraced_bytes += _int(f[5])
            continue
        if kind == "m":
            kv = dict(zip(f[4::2], f[5::2]))
            seg.markers.append(
                Marker(f[3], t, _int(kv.get("ifree", "0")), _int(kv.get("iblk", "0")),
                       _int(kv.get("drops", "0")), dict(seg.live))
            )
            continue
        if kind not in ("a", "r", "f", "o") or len(f) < 6:
            continue
        ptr = _hex(f[3])
        size = _int(f[4])
        task = f[5]
        pcs = tuple(p for p in (_hex(x) for x in f[6:9]) if p)
        if kind == "a":
            if ptr == 0:
                continue
            _put(seg, ptr, Block(size, task, pcs, t))
        elif kind == "f":
            if ptr == 0:
                continue
            if not _release_owner(seg, ptr):
                seg.unmatched_free += 1
        elif kind == "o":  # realloc moved the block to PSRAM: written after the realloc, may be late
            if ptr == 0:
                continue
            if not _release(seg, ptr):
                seg.unmatched_realloc += 1
        else:  # realloc
            old = _hex(f[9]) if len(f) > 9 else 0
            if ptr == 0:
                seg.failed_realloc += 1  # old block stays
                continue
            if old:
                if not _release(seg, old):
                    seg.unmatched_realloc += 1
            _put(seg, ptr, Block(size, task, pcs, t))
    return segs


def find_addr2line(prefix: Optional[str]) -> Optional[str]:
    dirs = [os.path.expanduser(f"~/.platformio/packages/{p}/bin")
            for p in ("toolchain-xtensa-esp32s3", "toolchain-xtensa-esp32",
                      "toolchain-xtensa-esp-elf")]
    if prefix:
        for d in dirs + [""]:
            cand = os.path.join(d, prefix + "addr2line") if d else prefix + "addr2line"
            path = shutil.which(cand) if not d else (cand if os.path.exists(cand) else None)
            if path:
                return path
        return None
    for d in dirs:
        hits = sorted(glob.glob(os.path.join(d, "*-addr2line")))
        if hits:
            return hits[0]
    return None


def resolve_pcs(pcs: list[int], elf: Optional[str], prefix: Optional[str] = None) -> dict[int, PcInfo]:
    """One addr2line call for all unique PCs; "??" fallback, never raises."""
    uniq = sorted(set(pcs))
    out: dict[int, PcInfo] = {p: UNKNOWN for p in uniq}
    tool = find_addr2line(prefix)
    if not uniq or not elf or not tool:
        return out
    try:
        res = subprocess.run([tool, "-f", "-C", "-e", elf]
                             + [f"0x{max(p - RET_ADJ, 0):08x}" for p in uniq],
                             capture_output=True, text=True, timeout=300)
    except (OSError, subprocess.SubprocessError):
        return out
    rows = res.stdout.splitlines()
    if len(rows) < 2 * len(uniq):
        return out
    for n, p in enumerate(uniq):
        out[p] = (rows[2 * n].strip() or "??", rows[2 * n + 1].strip() or "??:0")
    return out


def _caller_pc(blk: Block, skip: re.Pattern[str], info: dict[int, PcInfo]) -> Optional[int]:
    if not blk.pcs:
        return None
    for p in blk.pcs:
        if not skip.search(info.get(p, UNKNOWN)[0]):
            return p
    return blk.pcs[-1]


def group(live: dict[int, Block], info: dict[int, PcInfo], skip: Optional[re.Pattern[str]]) -> list[dict]:
    groups: dict[int, dict] = {}
    for blk in live.values():
        if skip is None:
            pc: Optional[int] = blk.pcs[0] if blk.pcs else None
        else:
            pc = _caller_pc(blk, skip, info)
        key = pc if pc is not None else -1
        g = groups.setdefault(key, {"bytes": 0, "blocks": 0, "tasks": set(), "pc": pc})
        g["bytes"] += blk.size
        g["blocks"] += 1
        g["tasks"].add(blk.task)
    rows = []
    for g in groups.values():
        fn, loc = info.get(g["pc"], UNKNOWN) if g["pc"] is not None else UNKNOWN
        rows.append({"bytes": g["bytes"], "blocks": g["blocks"], "function": fn, "location": loc,
                     "pc": f"0x{g['pc']:08x}" if g["pc"] is not None else None,
                     "tasks": sorted(g["tasks"])})
    rows.sort(key=lambda r: (-r["bytes"], r["function"]))
    return rows


def analyse(seg: Segment, at: Optional[list[str]], skip_fn: str, elf: Optional[str],
            prefix: Optional[str] = None) -> list[dict]:
    """Per-marker report dicts for one segment."""
    markers = seg.markers
    sel = [m for m in markers if (not at or m.name in at)]
    allpcs = [p for m in sel for b in m.live.values() for p in b.pcs]
    info = resolve_pcs(allpcs, elf, prefix)
    skip = re.compile(skip_fn)
    reports = []
    for m in sel:
        k = markers.index(m)
        live_total = sum(b.size for b in m.live.values())
        rep: dict = {"marker": m.name, "t": m.t, "ifree": m.ifree, "iblk": m.iblk,
                     "drops": m.drops, "live_total": live_total, "live_blocks": len(m.live),
                     "by_caller": group(m.live, info, None),
                     "by_first_outside_allocator": group(m.live, info, skip),
                     "delta": None}
        if k > 0:
            prev = markers[k - 1]
            d_live = live_total - sum(b.size for b in prev.live.values())
            d_blocks = len(m.live) - len(prev.live)
            drop = prev.ifree - m.ifree
            rep["delta"] = {"from": prev.name, "live": d_live, "ifree_drop": drop,
                            "blocks": d_blocks,
                            "overhead_per_block": ((drop - d_live) / d_blocks) if d_blocks else None}
        reports.append(rep)
    return reports


def _table(rows: list[dict], top: int) -> list[str]:
    out = [f"  {'bytes':>9} {'blocks':>6}  {'function':<40} {'file:line':<36} tasks"]
    for r in rows[:top]:
        out.append(f"  {r['bytes']:>9} {r['blocks']:>6}  {r['function'][:40]:<40} "
                   f"{r['location'][-36:]:<36} {','.join(r['tasks'])}")
    if len(rows) > top:
        out.append(f"  ... {len(rows) - top} more groups")
    out.append(f"  {sum(r['bytes'] for r in rows):>9} {sum(r['blocks'] for r in rows):>6}  SUM")
    return out


def render(seg: Segment, reports: list[dict], top: int) -> str:
    out = [f"== boot segment {seg.index}: {len(seg.markers)} markers, {len(seg.live)} live blocks at end, "
           f"unmatched free {seg.unmatched_free}, double alloc {seg.double_alloc}, "
           f"failed realloc {seg.failed_realloc}, unmatched realloc {seg.unmatched_realloc}, "
           f"shadowed {seg.shadowed}, untraced bytes (drain stack) {seg.untraced_bytes}"]
    for r in reports:
        out.append(f"-- marker {r['marker']} t={r['t']}ms  live_total={r['live_total']} "
                   f"({r['live_blocks']} blocks)  ifree={r['ifree']} iblk={r['iblk']} drops={r['drops']}")
        d = r["delta"]
        if d:
            ov = "n/a" if d["overhead_per_block"] is None else f"{d['overhead_per_block']:.1f}"
            out.append(f"   since {d['from']}: delta_live={d['live']:+d} delta_ifree={-d['ifree_drop']:+d} "
                       f"delta_blocks={d['blocks']:+d} overhead/block~{ov}")
        if r["drops"] > 0:
            out.append(f"   WARNING: {r['drops']} dropped events, the map is incomplete")
        out.append(" by caller (pc1):")
        out += _table(r["by_caller"], top)
        out.append(" by first caller outside allocator/glue:")
        out += _table(r["by_first_outside_allocator"], top)
    return "\n".join(out)


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--elf")
    ap.add_argument("--toolchain-prefix")
    ap.add_argument("--at", action="append", help="marker name (repeatable); default all of last boot")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--skip-fn", default=DEFAULT_SKIP_FN)
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    with open(a.log, errors="replace") as fh:
        segs = parse_log(fh.read().splitlines())
    seg = segs[-1]
    reports = analyse(seg, a.at, a.skip_fn, a.elf, a.toolchain_prefix)
    print(render(seg, reports, a.top))
    if any(r["drops"] > 0 for r in reports):
        print("WARNING: drops > 0 in at least one marker, the allocation map is incomplete")
    if a.json:
        with open(a.json, "w") as fh:
            json.dump({"segment": seg.index, "shadowed": seg.shadowed,
                       "untraced_bytes": seg.untraced_bytes, "markers": reports}, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
