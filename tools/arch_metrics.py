#!/usr/bin/env python3
"""Crude C/C++ function extent + complexity metrics (no libclang needed).

Heuristic: a function starts at a line matching a signature-ish pattern whose
brace depth is 0, and ends when brace depth returns to 0.
Reports: LOC, cyclomatic-ish decision count, max nesting depth, #preproc branches.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

SKIP_DIRS = {"Fonts", "GFX_Root", "maps"}
SIG = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_:<>,\*&\s\[\]]*\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*$"
)
DECISION = re.compile(r"\b(if|for|while|case|catch|&&|\|\||\?)\b|\?")
KEYWORDS = {"if", "for", "while", "switch", "return", "else", "do", "sizeof"}


def strip_noise(line: str) -> str:
    line = re.sub(r"//.*$", "", line)
    line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(\\.|[^'\\])*'", "''", line)
    return line


def analyze(path: Path):
    try:
        raw = path.read_text(errors="replace").splitlines()
    except Exception:
        return []
    depth = 0
    in_block_comment = False
    cur = None
    out = []
    for i, orig in enumerate(raw, 1):
        line = orig
        if in_block_comment:
            if "*/" in line:
                line = line.split("*/", 1)[1]
                in_block_comment = False
            else:
                continue
        while "/*" in line:
            before, rest = line.split("/*", 1)
            if "*/" in rest:
                line = before + rest.split("*/", 1)[1]
            else:
                line = before
                in_block_comment = True
                break
        line = strip_noise(line)
        stripped = line.strip()

        if stripped.startswith("#"):
            if cur is not None and re.match(r"#\s*(if|ifdef|ifndef|elif|else)", stripped):
                cur["pp"] += 1
            continue

        if depth == 0 and cur is None:
            m = SIG.match(stripped)
            if m and m.group(1) not in KEYWORDS and not stripped.startswith("return"):
                cur = {
                    "name": m.group(1),
                    "start": i,
                    "dec": 0,
                    "pp": 0,
                    "maxdepth": 0,
                    "open": False,
                }

        if cur is not None:
            cur["dec"] += len(DECISION.findall(stripped))

        opens = line.count("{")
        closes = line.count("}")
        if cur is not None and opens:
            cur["open"] = True
        depth += opens
        if cur is not None:
            cur["maxdepth"] = max(cur["maxdepth"], depth)
        depth -= closes
        if depth < 0:
            depth = 0

        if cur is not None and cur["open"] and depth == 0:
            out.append(
                {
                    "file": str(path),
                    "name": cur["name"],
                    "line": cur["start"],
                    "loc": i - cur["start"] + 1,
                    "cc": cur["dec"] + 1,
                    "pp": cur["pp"],
                    "nest": cur["maxdepth"],
                }
            )
            cur = None
        elif cur is not None and not cur["open"] and stripped.endswith(";"):
            cur = None
    return out


def main():
    roots = [Path(a) for a in sys.argv[1:]] or [Path("src")]
    funcs = []
    for root in roots:
        for p in root.rglob("*"):
            if p.suffix not in {".cpp", ".c", ".h", ".hpp"}:
                continue
            if any(part in SKIP_DIRS for part in p.parts):
                continue
            funcs.extend(analyze(p))
    funcs.sort(key=lambda f: (-f["loc"],))
    print(f"{'LOC':>5} {'CC':>4} {'NEST':>4} {'PP':>4}  FUNCTION @ FILE:LINE")
    for f in funcs[:60]:
        print(
            f"{f['loc']:>5} {f['cc']:>4} {f['nest']:>4} {f['pp']:>4}  "
            f"{f['name']}() @ {f['file']}:{f['line']}"
        )
    print()
    total = len(funcs)
    print(f"total functions parsed: {total}")
    for thr in (100, 200, 400, 800):
        n = sum(1 for f in funcs if f["loc"] > thr)
        print(f"  > {thr:>4} LOC: {n:>4} ({100*n/max(total,1):.1f}%)")
    print()
    print("Top by cyclomatic-ish complexity:")
    for f in sorted(funcs, key=lambda x: -x["cc"])[:25]:
        print(
            f"{f['loc']:>5} {f['cc']:>4} {f['nest']:>4} {f['pp']:>4}  "
            f"{f['name']}() @ {f['file']}:{f['line']}"
        )


if __name__ == "__main__":
    main()
