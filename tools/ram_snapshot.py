#!/usr/bin/env python3
"""
Deprecated: use tools/resource_watch.py instead (any env, baseline+delta
reporting, no hardcoded target list -- see defect C-12). This script is kept
working for backward compatibility but is no longer the preferred tool.

MeshCom RAM/Flash snapshot extractor.

Runs `pio run -e <env>` for each target and parses the memory summary lines
emitted by PlatformIO's size step. Outputs a JSON array to stdout.

Usage:
  python3 tools/ram_snapshot.py                       # all 7 targets
  python3 tools/ram_snapshot.py --only heltec,rak     # by short name or env name
  python3 tools/ram_snapshot.py --quiet               # suppress pio stderr pass-through

Exit code: 0 if all targets succeeded, 1 if any failed or parse error.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

TARGETS: list[dict] = [
    {"env": "heltec_wifi_lora_32_V3", "label": "Heltec V3",     "chip": "ESP32-S3"},
    {"env": "E22-DevKitC",            "label": "E22",            "chip": "ESP32"},
    {"env": "ttgo_tbeam",             "label": "T-Beam",         "chip": "ESP32"},
    {"env": "ttgo_tbeam_supreme",     "label": "T-Beam Supreme", "chip": "ESP32-S3"},
    {"env": "t_deck",                 "label": "T-Deck",         "chip": "ESP32-S3"},
    {"env": "t_deck_plus",            "label": "T-Deck Plus",    "chip": "ESP32-S3"},
    {"env": "wiscore_rak4631",        "label": "RAK4631",        "chip": "nRF52840"},
]

SHORT_NAMES: dict[str, str] = {
    "heltec":    "heltec_wifi_lora_32_V3",
    "e22":       "E22-DevKitC",
    "tbeam":     "ttgo_tbeam",
    "supreme":   "ttgo_tbeam_supreme",
    "tdeck":     "t_deck",
    "tdeck-plus":"t_deck_plus",
    "tdeck_plus":"t_deck_plus",
    "rak":       "wiscore_rak4631",
    "rak4631":   "wiscore_rak4631",
}

_RAM_RE   = re.compile(r"RAM:\s+\[.*?\]\s+([\d.]+)%\s+\(used (\d+) bytes from (\d+) bytes\)")
_FLASH_RE = re.compile(r"Flash:\s+\[.*?\]\s+([\d.]+)%\s+\(used (\d+) bytes from (\d+) bytes\)")
_PIO_BIN  = Path.home() / ".platformio/penv/bin/pio"


def resolve_targets(only: str | None) -> list[dict]:
    if not only:
        return TARGETS
    wanted: set[str] = set()
    for part in only.split(","):
        key = part.strip().lower()
        wanted.add(SHORT_NAMES.get(key, part.strip()))
    return [t for t in TARGETS if t["env"] in wanted]


def snapshot_env(target: dict, project_root: Path, quiet: bool) -> dict:
    env = target["env"]
    proc = subprocess.run(
        [str(_PIO_BIN), "run", "-e", env],
        capture_output=True, text=True, cwd=project_root,
    )
    combined = proc.stdout + proc.stderr

    if not quiet:
        for line in combined.splitlines():
            if any(k in line for k in ("RAM:", "Flash:", "error:", "ERROR", "[FAILED]", "SUCCESS")):
                print(f"  [{env}] {line}", file=sys.stderr, flush=True)

    ram_m   = _RAM_RE.search(combined)
    flash_m = _FLASH_RE.search(combined)

    if not ram_m or not flash_m:
        return {
            "env": env, "label": target["label"], "chip": target["chip"],
            "success": False, "error": "parse_failed",
            "raw_tail": combined[-400:].replace("\n", " "),
        }

    return {
        "env":         env,
        "label":       target["label"],
        "chip":        target["chip"],
        "success":     proc.returncode == 0,
        "ram_used":    int(ram_m.group(2)),
        "ram_total":   int(ram_m.group(3)),
        "ram_pct":     float(ram_m.group(1)),
        "flash_used":  int(flash_m.group(2)),
        "flash_total": int(flash_m.group(3)),
        "flash_pct":   float(flash_m.group(1)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only",         help="comma-separated short names or env names")
    parser.add_argument("--quiet",        action="store_true", help="suppress pio output on stderr")
    parser.add_argument("--project-root", default=os.getcwd(), help="PlatformIO project root")
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()
    targets = resolve_targets(args.only)
    if not targets:
        print(json.dumps({"error": f"no targets matched --only {args.only!r}"}), file=sys.stderr)
        return 1

    results = [snapshot_env(t, project_root, args.quiet) for t in targets]
    print(json.dumps(results, indent=2))
    return 0 if all(r.get("success") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
