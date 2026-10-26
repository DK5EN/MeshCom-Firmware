#!/usr/bin/env python3
"""Auto-update stage headroom check (QA-SIZE-AU01 / QA-AU-12).

For every <env>.bin + <env>.autoupdate pair, apply fwStageLayout() (src/fw_update.h) with
running == new image == the .bin size and report how much room is left in ota_0. Exit 1 if
any env would be refused (the node could no longer stage the update), warn when the possible
growth is below --warn-kb.

    tools/au_stage_check.py .pio/build/*/            # dirs of <env>.bin / <env>.autoupdate
    tools/au_stage_check.py a.bin a.autoupdate       # explicit files

ota_0 size per env: <dir>/partitions.bin if present (the real table PlatformIO built), else
--partitions (CSV, default partitions-4MB-safeboot.csv), --slot-kb overrides both.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

ALIGN = 0x10000  # FW_STAGE_ALIGN
ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CSV = ROOT / "partitions-4MB-safeboot.csv"

OK, REF_SIZE, REF_OVERLAP, REF_SMALL, REF_INFLATE = range(5)
NAMES = {OK: "ok", REF_SIZE: "size", REF_OVERLAP: "overlap", REF_SMALL: "small",
         REF_INFLATE: "inflate"}


def stage_layout(slot: int, running: int, z_len: int, i_len: int) -> tuple[int, int]:
    """Exact port of fwStageLayout(): (result, stageOff)."""
    if z_len == 0 or z_len > slot:
        return REF_SIZE, 0
    off = (slot - z_len) & ~(ALIGN - 1)
    if off < ALIGN:
        return REF_SMALL, 0
    if running > off:
        return REF_OVERLAP, off
    if i_len == 0 or i_len > off:
        return REF_INFLATE, off
    return OK, off


def max_growth(slot: int, size: int, z_len: int) -> int:
    """Largest image growth (bytes) that still stages. zLen is scaled with the image size,
    which is an approximation: real compression depends on the content."""
    def ok(g: int) -> bool:
        z = max(1, round(z_len * (size + g) / size))
        return stage_layout(slot, size + g, z, size + g)[0] == OK

    if size <= 0 or not ok(0):
        return -1
    lo, hi = 0, slot
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if ok(mid):
            lo = mid
        else:
            hi = mid - 1
    return lo


def _kb(n: int) -> str:
    return "-" if n < 0 else f"{n // 1024}KB"


def _size(text: str) -> int:
    t = text.strip().upper()
    if t.startswith("0X"):
        return int(t, 16)
    if t.endswith("K"):
        return int(t[:-1]) * 1024
    if t.endswith("M"):
        return int(t[:-1]) * 1024 * 1024
    return int(t)


def slot_from_csv(path: Path) -> int:
    for line in path.read_text().splitlines():
        cols = [c.strip() for c in line.split(",")]
        if len(cols) >= 5 and not line.lstrip().startswith("#") and cols[2] == "ota_0":
            return _size(cols[4])
    raise ValueError(f"{path}: no ota_0 row")


def slot_from_bin(path: Path) -> int:
    """ESP-IDF partition table: 32-byte entries, magic bytes AA 50 (0x50AA as LE u16), type app (0), subtype 0x10."""
    data = path.read_bytes()
    for i in range(0, len(data) - 31, 32):
        magic, ptype, sub, _off, size = struct.unpack_from("<HBBII", data, i)
        if magic != 0x50AA:
            break
        if ptype == 0 and sub == 0x10:
            return size
    raise ValueError(f"{path}: no ota_0 entry")


def collect(args: list[str]) -> list[tuple[Path, Path]]:
    pairs: list[tuple[Path, Path]] = []
    seen: set[Path] = set()
    files: list[Path] = []
    for a in args:
        p = Path(a)
        files += sorted(p.glob("*.autoupdate")) if p.is_dir() else [p]
    for f in files:
        stem = f.with_suffix("")
        if f.suffix == ".bin":
            z, b = stem.with_suffix(".autoupdate"), f
        elif f.suffix == ".autoupdate":
            z, b = f, stem.with_suffix(".bin")
        else:
            raise ValueError(f"{f}: neither .bin nor .autoupdate")
        if z in seen:
            continue
        seen.add(z)
        if not z.is_file() or not b.is_file():
            raise ValueError(f"{stem}: needs both .bin and .autoupdate")
        pairs.append((b, z))
    return pairs


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="+", help="directories or <env>.bin / <env>.autoupdate files")
    ap.add_argument("--partitions", type=Path, default=DEFAULT_CSV,
                    help="CSV used when a dir has no partitions.bin")
    ap.add_argument("--slot-kb", type=int, help="ota_0 size override in KB")
    ap.add_argument("--warn-kb", type=int, default=64, help="warn below this max growth (KB)")
    ns = ap.parse_args(argv)

    try:
        pairs = collect(ns.paths)
        if not pairs:
            raise ValueError("no .autoupdate files found")
        csv_slot = slot_from_csv(ns.partitions) if ns.slot_kb is None else ns.slot_kb * 1024
    except (OSError, ValueError) as e:
        print(f"au_stage_check: {e}", file=sys.stderr)
        return 2

    bad = warn = 0
    print(f"{'env':<34}{'bin':>9}{'zlen':>9}{'ota_0':>9}{'reserve':>10}{'max growth':>12}")
    for b, z in pairs:
        slot = csv_slot
        pbin = b.parent / "partitions.bin"
        if ns.slot_kb is None and pbin.is_file():
            try:
                slot = slot_from_bin(pbin)
            except (OSError, ValueError) as e:
                print(f"au_stage_check: {e}", file=sys.stderr)
                return 2
        size, z_len = b.stat().st_size, z.stat().st_size
        res, off = stage_layout(slot, size, z_len, size)
        grow = max_growth(slot, size, z_len)
        note = ""
        if res != OK:
            bad += 1
            note = f"  REFUSED ({NAMES[res]})"
        elif grow < ns.warn_kb * 1024:
            warn += 1
            note = f"  WARN (< {ns.warn_kb} KB)"
        print(f"{b.stem:<34}{size // 1024:>7}KB{z_len // 1024:>7}KB{slot // 1024:>7}KB"
              f"{(slot - z_len - size) // 1024:>8}KB{_kb(grow):>12}{note}")
    print("reserve = ota_0 - zlen - bin; max growth scales zlen with the image (approximation)")
    print(f"{len(pairs)} env(s), {bad} refused, {warn} below the warn threshold")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
