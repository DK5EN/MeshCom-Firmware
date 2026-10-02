#!/usr/bin/env python3
"""Token-normalized sliding-window duplicate detector for C/C++ sources."""
from __future__ import annotations

import hashlib
import re
import sys
from collections import defaultdict
from pathlib import Path

SKIP_DIRS = {"Fonts", "GFX_Root", "maps", "t5-src", "src"}  # 'src' only as nested asset dir
WINDOW = int(sys.argv[2]) if len(sys.argv) > 2 else 12


def norm(line: str) -> str | None:
    line = re.sub(r"//.*$", "", line)
    line = line.strip()
    if not line or line.startswith("#") or line in {"{", "}", "};"}:
        return None
    if line.startswith("/*") or line.startswith("*"):
        return None
    line = re.sub(r"\s+", " ", line)
    return line


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "src")
    files = []
    for p in root.rglob("*"):
        if p.suffix not in {".cpp", ".c"}:
            continue
        parts = set(p.parts)
        if parts & {"Fonts", "GFX_Root", "maps", "t5-src"}:
            continue
        if re.search(r"(Font_|img_|firasans|bitmaps)", p.name, re.I):
            continue
        files.append(p)

    windows = defaultdict(list)
    for p in files:
        lines = []
        for i, raw in enumerate(p.read_text(errors="replace").splitlines(), 1):
            n = norm(raw)
            if n:
                lines.append((i, n))
        for i in range(len(lines) - WINDOW + 1):
            chunk = "\n".join(l for _, l in lines[i : i + WINDOW])
            if len(set(l for _, l in lines[i : i + WINDOW])) < WINDOW // 2:
                continue  # too repetitive/trivial
            h = hashlib.sha1(chunk.encode()).hexdigest()
            windows[h].append((str(p), lines[i][0], lines[i + WINDOW - 1][0]))

    clones = {h: v for h, v in windows.items() if len(v) > 1}
    # group overlapping into blocks per file-pair
    pairs = defaultdict(int)
    for v in clones.values():
        seen = set()
        for a in v:
            for b in v:
                if a[0] < b[0]:
                    pairs[(a[0], b[0])] += 1
                elif a[0] == b[0] and a[1] < b[1]:
                    pairs[(a[0], b[0] + " (self)")] += 1
        del seen

    total_dup_lines = sum(len(v) - 1 for v in clones.values())
    print(f"window={WINDOW} files={len(files)}")
    print(f"duplicated {WINDOW}-line windows: {len(clones)}")
    print()
    print("Top duplicated file pairs (by # of cloned windows):")
    for (a, b), n in sorted(pairs.items(), key=lambda x: -x[1])[:30]:
        print(f"{n:>5}  {a}  <->  {b}")
    print()
    print("Longest individual clone sites (sample):")
    shown = 0
    for h, v in sorted(clones.items(), key=lambda x: -len(x[1])):
        if shown >= 12:
            break
        locs = ", ".join(f"{a}:{b}-{c}" for a, b, c in v[:4])
        print(f"  x{len(v)}  {locs}")
        shown += 1


if __name__ == "__main__":
    main()
