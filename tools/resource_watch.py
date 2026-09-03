#!/usr/bin/env python3
"""
MeshCom per-env resource watcher (RAM/Flash baseline + delta reporting).

Parses PlatformIO's post-build memory summary (the two lines below, emitted
for every ESP32 and nRF52 toolchain build) from a log file or stdin, then
compares the result against a per-env baseline stored in a JSON file --
flagging growth or shrinking headroom as GitHub-Actions annotations.

  RAM:   [==        ]  23.4% (used 76032 bytes from 327680 bytes)
  Flash: [==========]  43.9% (used 1492589 bytes from 3403776 bytes)

Supersedes tools/ram_snapshot.py's hardcoded 7-target list (defect C-12):
this tool works for any env in platformio.ini's default_envs, one at a time,
driven by whatever log CI already captured for that env.

Usage:
  # Parse a build log and compare against the baseline (report-only, exit 0).
  python3 tools/resource_watch.py check --env wiscore_rak4631 --log build.log

  # Refresh the baseline entry for one env after a deliberate size change.
  python3 tools/resource_watch.py update --env wiscore_rak4631 --log build.log

  # Build + baseline several envs sequentially (does not parallelize --
  # PlatformIO's .pio/build cache corrupts under concurrent builds of the
  # same env).
  python3 tools/resource_watch.py snapshot --envs wiscore_rak4631,t_echo

  # Check every ESP32 linker region (dram0_0_seg AND iram0_0_seg) in a map.
  # The RAM:/Flash: summary lines cannot see these: on a PSRAM board they
  # measure against the PSRAM-inclusive total, so a board sitting at 99.98 %
  # of iram0_0_seg reports "RAM 8.7 %". Four envs are within 4 kB of a link
  # failure on a clean tree -- ttgo_tbeam and its two siblings (20 B of IRAM)
  # and E22_XML-DevKitC (920 B of DRAM).
  python3 tools/resource_watch.py regions --env ttgo_tbeam \
      --map .pio/build/ttgo_tbeam/firmware.map

  # Run the built-in parser self-tests (no pio invocation, no files touched).
  python3 tools/resource_watch.py --self-test

Exit codes:
  check:    always 0, unless --strict is given and a warning fired (then 1).
  update:   0 on success, 1 on parse failure.
  snapshot: 0 if all envs succeeded, 1 if any build or parse failed.
  --self-test: 0 if all assertions pass, 1 otherwise.
"""
from __future__ import annotations

import argparse
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path

_RAM_RE = re.compile(
    r"RAM:\s+\[.*?\]\s+([\d.]+)%\s+\(used (\d+) bytes from (\d+) bytes\)"
)
_FLASH_RE = re.compile(
    r"Flash:\s+\[.*?\]\s+([\d.]+)%\s+\(used (\d+) bytes from (\d+) bytes\)"
)

_PIO_BIN = Path.home() / ".platformio/penv/bin/pio"

# MEM-01: static-DRAM headroom against the linker limit. On the classic-ESP32
# boards (E22-DevKitC, ttgo_tbeam) dram0_0_seg (.data + .bss) was measured at
# 1.7 kB / 0.5 kB headroom on 2026-08-30 -- the next static buffer anyone adds
# fails the link with "DRAM segment data does not fit". These two lines exist
# in every ESP32 map file; nRF52 builds emit no .map at all.
_DRAM_SEG_RE = re.compile(r"dram0_0_seg\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")
_BSS_END_RE = re.compile(r"\n\s*0x([0-9a-fA-F]+)\s+_bss_end\b")

# The IRAM counterpart. `iram0_0_seg` holds the code the linker is told to
# place in internal instruction RAM; `_iram_end` is the linker's own end
# marker for it, exactly as `_bss_end` is for dram0_0_seg. This region is
# what actually overflows first on the ttgo_tbeam family -- see REGIONS.
_IRAM_SEG_RE = re.compile(r"iram0_0_seg\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")
_IRAM_END_RE = re.compile(r"\n\s*0x([0-9a-fA-F]+)\s+_iram_end\b")

# Both ESP32 regions the linker can overflow, in report order. Each entry is
# (region name, segment regex, end-symbol regex, end-symbol name).
REGIONS = (
    ("dram0_0_seg", _DRAM_SEG_RE, _BSS_END_RE, "_bss_end"),
    ("iram0_0_seg", _IRAM_SEG_RE, _IRAM_END_RE, "_iram_end"),
)

DEFAULT_BASELINE = Path(__file__).resolve().parent / "resource_baseline.json"


class ParseError(ValueError):
    """Raised when a log does not contain both a RAM and a Flash summary line."""


def parse_usage(text: str) -> dict[str, int]:
    """Extract {ram_used, ram_total, flash_used, flash_total} from build log text.

    Tolerates multiple RAM/Flash lines in one log (e.g. nRF52 safeboot
    sub-builds print their own summary first) by taking the LAST match of
    each, which corresponds to the final/main firmware image.
    """
    ram_matches = list(_RAM_RE.finditer(text))
    flash_matches = list(_FLASH_RE.finditer(text))
    if not ram_matches or not flash_matches:
        missing = []
        if not ram_matches:
            missing.append("RAM")
        if not flash_matches:
            missing.append("Flash")
        raise ParseError(
            f"could not find {' and '.join(missing)} summary line(s) in input"
        )
    ram_m = ram_matches[-1]
    flash_m = flash_matches[-1]
    return {
        "ram_used": int(ram_m.group(2)),
        "ram_total": int(ram_m.group(3)),
        "flash_used": int(flash_m.group(2)),
        "flash_total": int(flash_m.group(3)),
    }


def parse_source(log_path: str | None) -> dict[str, int]:
    """Parse usage from a log file path, or stdin when log_path is None/'-'."""
    if log_path is None or log_path == "-":
        text = sys.stdin.read()
    else:
        text = Path(log_path).read_text(errors="replace")
    return parse_usage(text)


def load_baseline(baseline_path: Path) -> dict[str, dict]:
    if not baseline_path.exists():
        return {}
    with baseline_path.open() as f:
        return json.load(f)


def save_baseline(baseline_path: Path, data: dict[str, dict]) -> None:
    with baseline_path.open("w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def _pct_of_total(delta: int, total: int) -> float:
    return (delta / total * 100.0) if total else 0.0


def _headroom_pct(used: int, total: int) -> float:
    return (100.0 - (used / total * 100.0)) if total else 100.0


def format_notice(
    env: str,
    current: dict[str, int],
    base: dict[str, int] | None,
    warn_pct: float,
    min_headroom_pct: float,
) -> tuple[str, bool]:
    """Build the GitHub-Actions annotation line for one env.

    Returns (line, is_warning).
    """
    if base is None:
        ram_h = _headroom_pct(current["ram_used"], current["ram_total"])
        flash_h = _headroom_pct(current["flash_used"], current["flash_total"])
        line = (
            f"::notice ::{env} no baseline yet -- "
            f"RAM {current['ram_used']}/{current['ram_total']} "
            f"({ram_h:.1f}% headroom) Flash "
            f"{current['flash_used']}/{current['flash_total']} "
            f"({flash_h:.1f}% headroom)"
        )
        return line, False

    ram_d = current["ram_used"] - base["ram_used"]
    flash_d = current["flash_used"] - base["flash_used"]
    ram_dp = _pct_of_total(ram_d, current["ram_total"])
    flash_dp = _pct_of_total(flash_d, current["flash_total"])
    ram_headroom = _headroom_pct(current["ram_used"], current["ram_total"])
    flash_headroom = _headroom_pct(current["flash_used"], current["flash_total"])

    is_warning = (
        ram_dp > warn_pct
        or flash_dp > warn_pct
        or ram_headroom < min_headroom_pct
        or flash_headroom < min_headroom_pct
    )

    body = (
        f"{env} "
        f"RAM {current['ram_used']}/{current['ram_total']} "
        f"({ram_d:+d} bytes, {ram_dp:+.2f}%) "
        f"Flash {current['flash_used']}/{current['flash_total']} "
        f"({flash_d:+d} bytes, {flash_dp:+.2f}%)"
    )
    level = "warning" if is_warning else "notice"
    return f"::{level} ::{body}", is_warning


def cmd_check(args: argparse.Namespace) -> int:
    baseline_path = Path(args.baseline)
    try:
        current = parse_source(args.log)
    except ParseError as e:
        print(f"::error ::{args.env} {e}", file=sys.stderr)
        return 0  # report-only by design; parse failure is still reported, not fatal
    baseline = load_baseline(baseline_path)
    base = baseline.get(args.env)
    line, is_warning = format_notice(
        args.env, current, base, args.warn_pct, args.min_headroom_pct
    )
    print(line)
    if args.strict and is_warning:
        return 1
    return 0


def cmd_update(args: argparse.Namespace) -> int:
    baseline_path = Path(args.baseline)
    try:
        current = parse_source(args.log)
    except ParseError as e:
        print(f"::error ::{args.env} {e}", file=sys.stderr)
        return 1
    baseline = load_baseline(baseline_path)
    baseline[args.env] = {
        **current,
        "date": datetime.date.today().isoformat(),
    }
    save_baseline(baseline_path, baseline)
    print(f"updated baseline for {args.env} in {baseline_path}")
    return 0


def parse_region(
    map_text: str, seg_name: str, seg_re: re.Pattern, end_re: re.Pattern, end_name: str
) -> dict[str, int] | None:
    """Extract one ESP32 linker-map region's usage.

    Returns {origin, length, end, used, headroom} or None when the map
    carries no such segment (not an ESP32 map). Raises ParseError when the
    segment exists but its end symbol does not -- that is a truncated map,
    not a different platform.
    """
    seg = seg_re.search(map_text)
    if seg is None:
        return None
    end = end_re.search(map_text)
    if end is None:
        raise ParseError(f"map has {seg_name} but no {end_name} symbol")
    origin = int(seg.group(1), 16)
    length = int(seg.group(2), 16)
    end_addr = int(end.group(1), 16)
    used = end_addr - origin
    return {
        "origin": origin,
        "length": length,
        "end": end_addr,
        "used": used,
        "headroom": length - used,
    }


def parse_regions(map_text: str) -> dict[str, dict[str, int]]:
    """Every ESP32 region in REGIONS that this map carries, by region name.

    Empty for a non-ESP32 map (nRF52 envs emit no dram0_0_seg).
    """
    out: dict[str, dict[str, int]] = {}
    for name, seg_re, end_re, end_name in REGIONS:
        got = parse_region(map_text, name, seg_re, end_re, end_name)
        if got is not None:
            out[name] = got
    return out


def parse_dram(map_text: str) -> dict[str, int] | None:
    """Static-DRAM usage. Kept as the named entry point for dram0_0_seg.

    `bss_end` is retained as an alias of the generic `end` key so callers
    written against the pre-IRAM version keep working.
    """
    got = parse_region(map_text, "dram0_0_seg", _DRAM_SEG_RE, _BSS_END_RE, "_bss_end")
    if got is None:
        return None
    return {**got, "bss_end": got["end"]}


# What runs out first differs per region, so the failure sentence does too.
_REGION_CONSEQUENCE = {
    "dram0_0_seg": "the next static buffer fails the link",
    "iram0_0_seg": "the next IRAM-placed function fails the link",
}


def cmd_dram(args: argparse.Namespace) -> int:
    """Check every ESP32 region in REGIONS, not just dram0_0_seg.

    Named `dram` for CLI compatibility with the MEM-01 wiring; `regions` is
    the alias to prefer in new call sites.
    """
    map_path = Path(args.map)
    if not map_path.is_file():
        # nRF52 envs emit no .map -- absence is not a finding.
        print(f"::notice ::{args.env} no map file at {map_path}, region check skipped")
        return 0
    try:
        found = parse_regions(map_path.read_text(errors="replace"))
    except ParseError as e:
        print(f"::error ::{args.env} {e}", file=sys.stderr)
        return 1 if args.strict else 0
    if not found:
        print(f"::notice ::{args.env} map has no ESP32 segments, region check skipped")
        return 0
    failed = False
    for name, _seg_re, _end_re, _end_name in REGIONS:
        region = found.get(name)
        if region is None:
            continue
        body = (
            f"{args.env} {name} {region['used']}/{region['length']} bytes, "
            f"headroom {region['headroom']} bytes (min {args.min_headroom})"
        )
        if region["headroom"] < args.min_headroom:
            print(f"::error ::{body} -- {_REGION_CONSEQUENCE.get(name, 'the next link fails')}")
            failed = True
        else:
            print(f"::notice ::{body}")
    if failed:
        return 1 if args.strict else 0
    return 0


def cmd_snapshot(args: argparse.Namespace) -> int:
    envs = [e.strip() for e in args.envs.split(",") if e.strip()]
    baseline_path = Path(args.baseline)
    baseline = load_baseline(baseline_path)
    ok = True
    for env in envs:
        print(f"[{env}] building...", file=sys.stderr)
        proc = subprocess.run(
            [str(_PIO_BIN), "run", "-e", env],
            capture_output=True,
            text=True,
            cwd=args.project_root,
        )
        combined = proc.stdout + proc.stderr
        try:
            current = parse_usage(combined)
        except ParseError as e:
            print(f"::error ::{env} {e}", file=sys.stderr)
            ok = False
            continue
        if proc.returncode != 0:
            print(f"::error ::{env} pio run exited {proc.returncode}", file=sys.stderr)
            ok = False
            continue
        entry = {**current, "date": datetime.date.today().isoformat()}
        # The RAM:/Flash: summary lines are an aggregate: on a PSRAM board
        # they measure against the PSRAM-inclusive total, so dram0_0_seg at
        # 99 % and iram0_0_seg at 99.98 % are both invisible in them. Record
        # the two linker regions separately, from the map, so the baseline
        # carries the quantity that actually overflows.
        map_path = Path(args.project_root) / ".pio" / "build" / env / "firmware.map"
        if map_path.is_file():
            try:
                for name, region in parse_regions(map_path.read_text(errors="replace")).items():
                    entry[name] = {"used": region["used"], "length": region["length"]}
            except ParseError as e:
                print(f"::warning ::{env} region parse skipped: {e}", file=sys.stderr)
        baseline[env] = entry
        regions_note = " ".join(
            f"{n.split('0_0')[0]} {entry[n]['used']}/{entry[n]['length']}"
            for n, *_ in REGIONS if n in entry
        )
        print(f"[{env}] RAM {current['ram_used']}/{current['ram_total']} "
              f"Flash {current['flash_used']}/{current['flash_total']}"
              + (f" | {regions_note}" if regions_note else ""), file=sys.stderr)
    save_baseline(baseline_path, baseline)
    return 0 if ok else 1


# --- self-test -------------------------------------------------------------

_SAMPLE_ESP32 = (
    "Some other output line\n"
    "RAM:   [==        ]  23.4% (used 76032 bytes from 327680 bytes)\n"
    "Flash: [==========]  43.9% (used 1492589 bytes from 3403776 bytes)\n"
)

_SAMPLE_NRF52 = (
    "RAM:   [=====     ]  47.1% (used 123456 bytes from 262144 bytes)\n"
    "Flash: [===       ]  33.3% (used 262144 bytes from 786432 bytes)\n"
)

_SAMPLE_MULTI = (
    # safeboot sub-build prints its own summary first
    "RAM:   [=         ]   9.0% (used 1000 bytes from 11000 bytes)\n"
    "Flash: [=         ]   5.0% (used 2000 bytes from 40000 bytes)\n"
    "-- linking main image --\n"
    "RAM:   [==        ]  23.4% (used 76032 bytes from 327680 bytes)\n"
    "Flash: [==========]  43.9% (used 1492589 bytes from 3403776 bytes)\n"
)

_SAMPLE_MALFORMED = "no RAM or Flash lines here at all\n"


def run_self_test() -> int:
    failures: list[str] = []

    def check(name: str, cond: bool) -> None:
        if not cond:
            failures.append(name)

    esp32 = parse_usage(_SAMPLE_ESP32)
    check("esp32 ram_used", esp32["ram_used"] == 76032)
    check("esp32 ram_total", esp32["ram_total"] == 327680)
    check("esp32 flash_used", esp32["flash_used"] == 1492589)
    check("esp32 flash_total", esp32["flash_total"] == 3403776)

    nrf52 = parse_usage(_SAMPLE_NRF52)
    check("nrf52 ram_used", nrf52["ram_used"] == 123456)
    check("nrf52 ram_total", nrf52["ram_total"] == 262144)
    check("nrf52 flash_used", nrf52["flash_used"] == 262144)
    check("nrf52 flash_total", nrf52["flash_total"] == 786432)

    multi = parse_usage(_SAMPLE_MULTI)
    check("multi takes last ram_used", multi["ram_used"] == 76032)
    check("multi takes last flash_used", multi["flash_used"] == 1492589)

    try:
        parse_usage(_SAMPLE_MALFORMED)
        failures.append("malformed input should raise ParseError")
    except ParseError as e:
        check("malformed error mentions RAM", "RAM" in str(e))
        check("malformed error mentions Flash", "Flash" in str(e))

    # format_notice: no baseline
    line, warn = format_notice("envX", esp32, None, 2.0, 10.0)
    check("no-baseline notice level", line.startswith("::notice ::"))
    check("no-baseline not a warning", warn is False)

    # format_notice: within thresholds -> notice
    base_close = {**esp32}
    line, warn = format_notice("envX", esp32, base_close, 2.0, 10.0)
    check("identical usage -> notice", line.startswith("::notice ::"))
    check("identical usage -> not warning", warn is False)

    # format_notice: growth beyond warn_pct -> warning
    base_far = {
        "ram_used": esp32["ram_used"] - 20000,
        "ram_total": esp32["ram_total"],
        "flash_used": esp32["flash_used"],
        "flash_total": esp32["flash_total"],
    }
    line, warn = format_notice("envX", esp32, base_far, 2.0, 10.0)
    check("large ram growth -> warning", warn is True)
    check("large ram growth -> ::warning line", line.startswith("::warning ::"))

    # format_notice: low headroom -> warning even with no growth
    near_full = {"ram_used": 95, "ram_total": 100, "flash_used": 50, "flash_total": 100}
    line, warn = format_notice("envX", near_full, near_full, 2.0, 10.0)
    check("low headroom -> warning", warn is True)

    # parse_dram: real-world shape (values from the E22-DevKitC map, 2026-08-30)
    sample_map = (
        "dram0_0_seg      0x000000003ffbdb5c 0x000000000001e6a4 rw\n"
        "                0x000000003ffdba58                _bss_end = ABSOLUTE (.)\n"
    )
    dram = parse_dram(sample_map)
    check("dram parsed", dram is not None)
    if dram is not None:
        check("dram length", dram["length"] == 0x1E6A4)
        check("dram used", dram["used"] == 0x3FFDBA58 - 0x3FFBDB5C)
        check("dram headroom", dram["headroom"] == dram["length"] - dram["used"])

    check("dram none on foreign map", parse_dram("no such segment here\n") is None)

    try:
        parse_dram("dram0_0_seg      0x3ffbdb5c 0x1e6a4 rw\n")
        failures.append("dram map without _bss_end should raise ParseError")
    except ParseError as e:
        check("dram error names _bss_end", "_bss_end" in str(e))

    # parse_regions: the ttgo_tbeam map, 2026-09-03 (fork-main, no KISS).
    # This is the shape that broke upstream CI on 9d885b1a: DRAM has ~10 kB
    # to spare while IRAM has 20 bytes, so a DRAM-only guard reports healthy
    # on the very build that is about to fail the link.
    tbeam_map = (
        "iram0_0_seg      0x0000000040080000 0x0000000000020000 xr\n"
        "dram0_0_seg      0x000000003ffbdb5c 0x000000000001e6a4 rw\n"
        "                0x000000003ffd9ac0                _bss_end = ABSOLUTE (.)\n"
        "                0x000000004009ffec                _iram_end = ABSOLUTE (.)\n"
    )
    tb = parse_regions(tbeam_map)
    check("tbeam both regions found", set(tb) == {"dram0_0_seg", "iram0_0_seg"})
    check("tbeam iram headroom is 20 B", tb["iram0_0_seg"]["headroom"] == 20)
    check("tbeam dram headroom is 10 048 B", tb["dram0_0_seg"]["headroom"] == 10048)
    check(
        "tbeam passes a DRAM-only 4 kB gate",
        tb["dram0_0_seg"]["headroom"] >= 4096,
    )
    check(
        "tbeam fails the same gate on IRAM -- the regression this covers",
        tb["iram0_0_seg"]["headroom"] < 4096,
    )

    # The mirror case: E22_XML-DevKitC, 2026-09-03. Here DRAM is the binding
    # region (920 B) and IRAM the looser one (3456 B) -- the opposite order
    # from the tbeam above. Which region fails first is a property of the
    # board, so a guard that watches only one of them is wrong on some board
    # whichever one it picks.
    xml_map = (
        "iram0_0_seg      0x0000000040080000 0x0000000000020000 xr\n"
        "dram0_0_seg      0x000000003ffbdb5c 0x000000000001e6a4 rw\n"
        "                0x000000003ffdbe68                _bss_end = ABSOLUTE (.)\n"
        "                0x000000004009f280                _iram_end = ABSOLUTE (.)\n"
    )
    xml = parse_regions(xml_map)
    check("xml dram headroom is 920 B", xml["dram0_0_seg"]["headroom"] == 920)
    check("xml iram headroom is 3456 B", xml["iram0_0_seg"]["headroom"] == 3456)
    check(
        "xml binding region is dram, tbeam's was iram",
        xml["dram0_0_seg"]["headroom"] < xml["iram0_0_seg"]["headroom"]
        and tb["iram0_0_seg"]["headroom"] < tb["dram0_0_seg"]["headroom"],
    )

    check("regions empty on nRF52 map", parse_regions("no segments here\n") == {})

    try:
        parse_regions(
            "iram0_0_seg      0x40080000 0x20000 xr\n"
            "dram0_0_seg      0x3ffbdb5c 0x1e6a4 rw\n"
            "                0x3ffd9ac0                _bss_end = ABSOLUTE (.)\n"
        )
        failures.append("iram segment without _iram_end should raise ParseError")
    except ParseError as e:
        check("region error names _iram_end", "_iram_end" in str(e))
        check("region error names iram0_0_seg", "iram0_0_seg" in str(e))

    if failures:
        print(f"SELF-TEST FAILED ({len(failures)}): {', '.join(failures)}", file=sys.stderr)
        return 1
    print("SELF-TEST OK (all assertions passed)")
    return 0


# --- CLI ---------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--self-test", action="store_true", help="run built-in parser self-tests and exit"
    )
    sub = parser.add_subparsers(dest="command")

    p_check = sub.add_parser("check", help="compare a build log against the baseline")
    p_check.add_argument("--env", required=True, help="PlatformIO env name")
    p_check.add_argument("--log", default="-", help="build log path, or '-' for stdin (default)")
    p_check.add_argument("--baseline", default=str(DEFAULT_BASELINE), help="baseline JSON path")
    p_check.add_argument(
        "--warn-pct", type=float, default=2.0,
        help="warn when RAM or Flash grows by more than this many percentage-points "
             "of total vs. baseline (default 2.0)",
    )
    p_check.add_argument(
        "--min-headroom-pct", type=float, default=10.0,
        help="warn when remaining RAM or Flash headroom falls below this percentage "
             "(default 10.0)",
    )
    p_check.add_argument(
        "--strict", action="store_true",
        help="exit 1 when a warning fires (default: always exit 0, report-only)",
    )
    p_check.set_defaults(func=cmd_check)

    p_update = sub.add_parser("update", help="write/refresh the baseline entry for one env")
    p_update.add_argument("--env", required=True, help="PlatformIO env name")
    p_update.add_argument("--log", default="-", help="build log path, or '-' for stdin (default)")
    p_update.add_argument("--baseline", default=str(DEFAULT_BASELINE), help="baseline JSON path")
    p_update.set_defaults(func=cmd_update)

    p_snap = sub.add_parser(
        "snapshot", help="build one or more envs sequentially and update their baselines"
    )
    p_snap.add_argument("--envs", required=True, help="comma-separated PlatformIO env names")
    p_snap.add_argument("--baseline", default=str(DEFAULT_BASELINE), help="baseline JSON path")
    p_snap.add_argument("--project-root", default=".", help="PlatformIO project root")
    p_snap.set_defaults(func=cmd_snapshot)

    p_dram = sub.add_parser(
        "dram", help="check static-DRAM (dram0_0_seg) headroom in an ESP32 linker map"
    )
    p_dram.add_argument("--env", required=True, help="PlatformIO env name (labeling only)")
    p_dram.add_argument("--map", required=True, help=".pio/build/<env>/firmware.map")
    p_dram.add_argument(
        "--min-headroom", type=int, default=4096,
        help="fail below this many bytes of dram0_0_seg headroom (default 4096)",
    )
    p_dram.add_argument(
        "--strict", action="store_true",
        help="exit 1 on failure (default: report-only)",
    )
    p_dram.set_defaults(func=cmd_dram)

    # Preferred name for new call sites: the command checks every region in
    # REGIONS, not only DRAM. `dram` stays as-is so the MEM-01 wiring and
    # anything already scripted against it keep working.
    p_regions = sub.add_parser(
        "regions",
        help="check every ESP32 linker region (dram0_0_seg, iram0_0_seg) in a map",
    )
    p_regions.add_argument("--env", required=True, help="PlatformIO env name (labeling only)")
    p_regions.add_argument("--map", required=True, help=".pio/build/<env>/firmware.map")
    p_regions.add_argument(
        "--min-headroom", type=int, default=4096,
        help="fail below this many bytes of headroom in any region (default 4096)",
    )
    p_regions.add_argument(
        "--strict", action="store_true",
        help="exit 1 on failure (default: report-only)",
    )
    p_regions.set_defaults(func=cmd_dram)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if not args.command:
        parser.print_help()
        return 1

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
