#!/usr/bin/env python3
"""Verify the uploaded release assets before the release is published (QA-AU-05).

    tools/verify_release_assets.py <asset-dir> [--tag v4.40a.10.25] [--pairs 27] [--total 66]

For every <name>.autoupdate in the directory: the matching <name>.bin exists, inflates to the
same bytes, and carries a MCBOARD1 marker (src/mc_board_id.h) whose env is the build env of
<name> (CI renames three assets, see pages_flasher.RELEASE_ASSET_NAME) and, with --tag, whose
build tag is that tag. Exit 1 on any problem.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import make_autoupdate as mau  # noqa: E402
import pages_flasher as pf  # noqa: E402

ENV_OF_ASSET = {v: k for k, v in pf.RELEASE_ASSET_NAME.items()}


def check_pair(asset_dir: Path, name: str, tag: str | None) -> list[str]:
    """Problems of one <name>.bin / <name>.autoupdate pair (empty list = ok)."""
    bin_p, z_p = asset_dir / f"{name}.bin", asset_dir / f"{name}.autoupdate"
    if not bin_p.is_file():
        return [f"{name}: {bin_p.name} missing"]
    data = bin_p.read_bytes()
    try:
        mau.verify(z_p.read_bytes(), data)
    except mau.MakeAutoupdateError as exc:
        return [f"{name}: {z_p.name}: {exc}"]
    try:
        m = pf.read_board_marker(bin_p)
    except SystemExit as exc:
        return [f"{name}: {exc}"]
    env = ENV_OF_ASSET.get(name, name)
    if m is None:
        return [f"{name}: no MCBOARD1 marker"]
    out = []
    if m["format"] != pf.MARKER_FORMAT:
        out.append(f"{name}: marker format {m['format']}, want {pf.MARKER_FORMAT}")
    if m["env"] != env:
        out.append(f"{name}: marker env '{m['env']}', want '{env}'")
    if tag is not None and m["tag"] != tag:
        out.append(f"{name}: marker tag '{m['tag']}', want '{tag}'")
    return out


def verify_dir(asset_dir: Path, tag: str | None, pairs: int | None, total: int | None) -> list[str]:
    names = sorted(p.name[: -len(".autoupdate")] for p in asset_dir.glob("*.autoupdate"))
    problems = []
    if pairs is not None and len(names) != pairs:
        problems.append(f"{len(names)} .autoupdate assets, expected {pairs}")
    n_all = sum(1 for p in asset_dir.iterdir() if p.is_file())
    if total is not None and n_all != total:
        problems.append(f"{n_all} assets, expected {total}")
    for name in names:
        problems += check_pair(asset_dir, name, tag)
    return problems


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("asset_dir", type=Path)
    ap.add_argument("--tag", help="build tag every marker must carry")
    ap.add_argument("--pairs", type=int, help="expected number of .autoupdate/.bin pairs")
    ap.add_argument("--total", type=int, help="expected number of files in the directory")
    args = ap.parse_args(argv)
    if not args.asset_dir.is_dir():
        print(f"{args.asset_dir}: not a directory", file=sys.stderr)
        return 2
    problems = verify_dir(args.asset_dir, args.tag, args.pairs, args.total)
    for p in problems:
        print(f"FAIL {p}", file=sys.stderr)
    if not problems:
        print("release assets ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
