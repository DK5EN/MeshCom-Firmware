#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""nm_symsum.py -- RAM of the neighbourhood stores, measured from the ELF symbol
table instead of the total-RAM link line.

docs/meshcom5-topologie/ (section 4.10, and build.py's FAM/MEAS_NM tables) makes a
"today" claim per board family: X bytes for MHeard, Y for the path table, Z for the
neighbourhood matrix, W for the ring-need/-alone side tables. Those numbers were
measured once, by hand, against one build per family. This tool re-measures them
straight from ``nm -S`` on the actual firmware.elf, per env, so any later wave (or a
CI gate) can prove its RAM claim instead of re-typing the old numbers.

Usage::

    uv run tools/nm_symsum.py --elf E22-DevKitC=/path/E22-DevKitC.elf
    uv run tools/nm_symsum.py /path/to/dir/with/env.elf/files --family
    uv run tools/nm_symsum.py --elf wiscore_rak4631=/path/x.elf --json

Only the stdlib is used; ``nm`` (the board's cross-toolchain nm, auto-picked per env
via ``nm_for()`` -- the same rule as ``tools/neo/gate.sh``) is the only external
program invoked, and only to list symbols -- this tool never builds anything.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --------------------------------------------------------------------------- groups
# One regex per group, tried in order, first match wins. A symbol that matches none
# of these is not part of any neighbourhood store and is left out of the table
# entirely (it still counts toward nothing -- "total" is the sum of the groups
# below, not of the whole ELF). Extend this table, don't restructure the script,
# when a future wave adds a new topology array.
#
# NOTE on "mheard": mheardPath* is its own group (mhpath) and is matched first, so
# the mheard pattern's negative lookahead only has to keep out that one prefix.
# The second mheard pattern catches the nRF52-only function-local `static struct
# mheardLine mheardLine;` copies (sendMheard(), showMHeard(), sub_page_mheard()) --
# demangled by ``nm -C`` as "<function>()::mheardLine". They are genuinely part of
# the mheard footprint (build.py's ``stat`` field, folded into the same "today"
# total), so they are grouped with "mheard" here rather than reported separately.
GROUP_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("mhpath", re.compile(r"^mheardPath")),
    ("mheard", re.compile(r"^mheard(?!Path)")),
    ("mheard", re.compile(r"::mheardLine$")),
    ("nbr", re.compile(r"^nbrMatrix\b")),
    ("nbr", re.compile(r"^(nbrCall|nbrRow|nbrHears|nbrHeardBy|nbrEdge|nbrExt|hzCall|hzEntry|hzMeta)")),
    ("rings", re.compile(r"^ring(Need|Alone)\b")),
]
GROUP_ORDER = ["mheard", "mhpath", "nbr", "rings"]

# nm -S output types that are actual storage (bss/data), not code or debug info.
# Lowercase = local (internal-linkage or, for GCC, function-local static) symbol,
# uppercase = external. Both are real RAM.
DATA_TYPES = set("bBdD")

_NM_LINE = re.compile(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([a-zA-Z])\s+(.*)$")

# --------------------------------------------------------------------------- nm_for
# Mirrors tools/neo/gate.sh's nm_for(): picks the cross-nm binary by env name.
# Same known imprecision as gate.sh -- "heltec*" also matches heltec_wifi_lora_32_V2
# and heltec_wireless_stick/_tracker, which are classic ESP32 (not S3) boards, and
# "wiscore*" is the only nRF52-prefix name that matches a real env (heltec_t114 and
# t_echo need the explicit names below, gate.sh's ENVS list never included them).
# Kept 1:1 with gate.sh on purpose so the two tools never quietly disagree; fix both
# together if this ever needs tightening.
TOOLCHAIN_ROOT = Path(os.path.expanduser("~/.platformio/packages"))
NM_S3 = TOOLCHAIN_ROOT / "toolchain-xtensa-esp32s3" / "bin" / "xtensa-esp32s3-elf-nm"
NM_ESP32 = TOOLCHAIN_ROOT / "toolchain-xtensa-esp32" / "bin" / "xtensa-esp32-elf-nm"
NM_ARM = TOOLCHAIN_ROOT / "toolchain-gccarmnoneeabi" / "bin" / "arm-none-eabi-nm"


def nm_for(env: str) -> Path:
    # nRF52 exact names first: "heltec_t114" would otherwise fall into the
    # "heltec*" S3 bucket below (it shares the heltec_* name prefix while being
    # one of the three nRF52 variant boards, see platformio.ini's nrf52_base
    # comment) -- gate.sh never hits this because its own ENVS list never
    # includes heltec_t114 or t_echo, so the ordering bug was latent there too.
    if env.startswith("wiscore") or env == "t_echo" or env == "heltec_t114":
        return NM_ARM
    if env.startswith(("heltec", "t_deck")) or env == "ttgo_tbeam_supreme":
        return NM_S3
    return NM_ESP32


# --------------------------------------------------------------------------- family
# build.py's FAM dict names one representative env per family. Real envs are
# classified here by name pattern (build.py itself has no general env->family map
# either -- it only ever measured these four). Unknown envs fall back to "classic";
# --family flags them so a wrong guess is visible, not silent.
FAMILY_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("E22_XML", re.compile(r"XML")),
    ("nRF52", re.compile(r"^(wiscore_rak4631|heltec_t114|t_echo)$")),
    ("S3", re.compile(r"(S3|_V3$|_V4$|t_deck|tbeam_supreme|T3_S3|T-ETH-ELITE)")),
]
FAMILY_DEFAULT = "klassisch"


def family_for(env: str) -> str:
    for name, pat in FAMILY_PATTERNS:
        if pat.search(env):
            return name
    return FAMILY_DEFAULT


# --------------------------------------------------------------------------- core


@dataclass
class EnvResult:
    env: str
    elf: Path
    groups: dict[str, int] = field(default_factory=dict)
    unmatched_count: int = 0

    @property
    def total(self) -> int:
        return sum(self.groups.get(g, 0) for g in GROUP_ORDER)

    @property
    def family(self) -> str:
        return family_for(self.env)


def parse_nm_output(text: str) -> list[tuple[int, str, str]]:
    """Returns (size, type, demangled_name) for every line nm printed with a size."""
    out = []
    for line in text.splitlines():
        m = _NM_LINE.match(line.strip())
        if not m:
            continue
        _addr, size_hex, typ, name = m.groups()
        out.append((int(size_hex, 16), typ, name))
    return out


def run_nm(nm_bin: Path, elf: Path) -> str:
    try:
        proc = subprocess.run(
            [str(nm_bin), "-C", "-S", "--size-sort", str(elf)],
            capture_output=True, text=True, check=False,
        )
    except FileNotFoundError as exc:
        raise RuntimeError(f"nm binary not found: {nm_bin} ({exc})") from None
    if proc.returncode != 0:
        raise RuntimeError(f"{nm_bin} {elf} failed (rc={proc.returncode}): {proc.stderr.strip()}")
    return proc.stdout


def classify(name: str) -> str | None:
    for group, pat in GROUP_PATTERNS:
        if pat.search(name):
            return group
    return None


def measure(env: str, elf: Path, nm_bin: Path) -> EnvResult:
    result = EnvResult(env=env, elf=elf)
    for size, typ, name in parse_nm_output(run_nm(nm_bin, elf)):
        if typ not in DATA_TYPES or size == 0:
            continue
        group = classify(name)
        if group is None:
            result.unmatched_count += 1
            continue
        result.groups[group] = result.groups.get(group, 0) + size
    return result


# --------------------------------------------------------------------------- CLI


def collect_targets(args: argparse.Namespace) -> dict[str, Path]:
    """env -> elf path, from --elf EN=PATH entries and directory/file positionals."""
    targets: dict[str, Path] = {}
    for spec in args.elf or []:
        if "=" not in spec:
            raise SystemExit(f"--elf expects ENV=PATH, got: {spec!r}")
        env, path = spec.split("=", 1)
        targets[env] = Path(path)
    for pos in args.targets or []:
        p = Path(pos)
        if p.is_dir():
            for elf in sorted(p.glob("*.elf")):
                targets.setdefault(elf.stem, elf)
        elif p.is_file():
            targets.setdefault(p.stem, p)
        else:
            raise SystemExit(f"not a file or directory: {p}")
    return targets


def print_table(results: list[EnvResult]) -> None:
    header = ["env"] + GROUP_ORDER + ["total"]
    widths = [max(len(header[i]), *(len(str(_row_val(r, header[i]))) for r in results)) if results else len(header[i])
              for i in range(len(header))]

    def fmt_row(cells: list[str]) -> str:
        return "  ".join(c.ljust(w) for c, w in zip(cells, widths))

    print(fmt_row(header))
    print(fmt_row(["-" * w for w in widths]))
    for r in results:
        cells = [r.env] + [str(r.groups.get(g, 0)) for g in GROUP_ORDER] + [str(r.total)]
        print(fmt_row(cells))
        if r.unmatched_count:
            print(f"  ({r.unmatched_count} other data/bss symbols not in any group, not counted)")


def _row_val(r: EnvResult, key: str) -> str:
    if key == "env":
        return r.env
    if key == "total":
        return str(r.total)
    return str(r.groups.get(key, 0))


def print_family_table(results: list[EnvResult]) -> None:
    by_family: dict[str, list[EnvResult]] = {}
    for r in results:
        by_family.setdefault(r.family, []).append(r)
    for family in sorted(by_family):
        rows = by_family[family]
        print(f"=== {family} ===")
        print_table(rows)
        totals = {r.total for r in rows}
        if len(totals) > 1:
            print(f"  WARN: {family} envs disagree on total: {sorted(totals)}")
        print()


def to_json(results: list[EnvResult]) -> dict:
    return {
        "envs": {
            r.env: {
                "elf": str(r.elf),
                "family": r.family,
                "groups": {g: r.groups.get(g, 0) for g in GROUP_ORDER},
                "total": r.total,
                "unmatched_count": r.unmatched_count,
            }
            for r in results
        }
    }


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("targets", nargs="*", help="ELF file(s) or a directory holding <env>.elf files")
    p.add_argument("--elf", action="append", metavar="ENV=PATH",
                    help="explicit env=path mapping; repeatable")
    p.add_argument("--nm", metavar="PATH", help="override the nm binary for every env (skips nm_for())")
    p.add_argument("--json", action="store_true", help="machine-readable output")
    p.add_argument("--family", action="store_true", help="group the table by board family")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    targets = collect_targets(args)
    if not targets:
        print("no ELF targets given (use --elf ENV=PATH or a directory of <env>.elf files)", file=sys.stderr)
        return 2

    results: list[EnvResult] = []
    had_error = False
    for env, elf in sorted(targets.items()):
        if not elf.exists():
            print(f"skip {env}: {elf} does not exist", file=sys.stderr)
            had_error = True
            continue
        nm_bin = Path(args.nm) if args.nm else nm_for(env)
        try:
            results.append(measure(env, elf, nm_bin))
        except RuntimeError as exc:
            print(f"skip {env}: {exc}", file=sys.stderr)
            had_error = True

    if not results:
        return 2

    if args.json:
        print(json.dumps(to_json(results), indent=2))
    elif args.family:
        print_family_table(results)
    else:
        print_table(results)

    return 1 if had_error else 0


if __name__ == "__main__":
    raise SystemExit(main())
