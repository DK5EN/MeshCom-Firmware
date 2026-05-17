#!/usr/bin/env python3
"""
MeshCom mechanical code audit scanner.

Runs grep-based checks against the rule categories in docs/codequality-rules.md.
Outputs a JSON object with findings list and summary counts.

Usage:
  python3 tools/code_audit_scan.py                        # changed files vs upstream/dev
  python3 tools/code_audit_scan.py --full                 # all of src/
  python3 tools/code_audit_scan.py --only src/esp32/esp32_ble.cpp[,...]

Exit code: 0 always (findings are not errors; callers check the JSON).
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# Each entry: (rule_id, severity, category, short_description, regex_pattern)
# Patterns match against individual source lines. No multiline.
CHECKS: list[tuple[str, str, str, str, str]] = [
    # Buffer Safety
    ("BND-01", "CRITICAL", "Buffer Safety",  "sprintf() — use snprintf",       r"\bsprintf\s*\("),
    ("BND-01", "HIGH",     "Buffer Safety",  "strcpy() — use strncpy",         r"\bstrcpy\s*\("),
    ("BND-01", "HIGH",     "Buffer Safety",  "strcat() — use strncat",         r"\bstrcat\s*\("),
    ("BND-01", "HIGH",     "Buffer Safety",  "gets() — forbidden",             r"\bgets\s*\("),
    # Memory Safety
    ("MEM-01", "HIGH",     "Memory Safety",  "malloc() in non-setup context",  r"\bmalloc\s*\("),
    ("MEM-03", "MEDIUM",   "Memory Safety",  "Arduino String type declaration",r"\bString\s+\w+\s*[=;(,]"),
    # Thread Safety
    ("RACE-03","HIGH",     "Thread Safety",  "portMAX_DELAY in semaphore take",r"\bportMAX_DELAY\b"),
    # Stack Safety
    ("STK-02", "HIGH",     "Stack Safety",   "alloca() — stack allocation",    r"\balloca\s*\("),
    # Security
    ("SEC-01", "CRITICAL", "Security",       "hardcoded credential literal",
     r'(?i)\b(password|passwd|psk|secret|apikey)\s*=\s*"[^"]{4,}"'),
    # Watchdog / Stability
    ("STAB-01","HIGH",     "Watchdog",       "delay(>= 5000 ms) blocks WDT",
     r"\bdelay\s*\(\s*(?:[5-9]\d{3}|[1-9]\d{4,})\s*\)"),
    # Protocol / bounds
    ("BND-02", "MEDIUM",   "Buffer Safety",  "memcpy — verify length before call",
     r"\bmemcpy\s*\("),
]

_SRC_SUFFIXES = {".cpp", ".c", ".h"}


def get_changed_files(project_root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "diff", "upstream/dev", "--name-only", "--diff-filter=AM"],
        capture_output=True, text=True, cwd=project_root,
    )
    files: list[Path] = []
    for rel in result.stdout.splitlines():
        rel = rel.strip()
        if not rel:
            continue
        p = project_root / rel
        if p.exists() and p.suffix in _SRC_SUFFIXES:
            files.append(p)
    return files


def get_full_src_files(project_root: Path) -> list[Path]:
    result = subprocess.run(
        ["find", "src", "-type", "f"],
        capture_output=True, text=True, cwd=project_root,
    )
    files: list[Path] = []
    for rel in result.stdout.splitlines():
        rel = rel.strip()
        if not rel:
            continue
        p = project_root / rel
        if p.exists() and p.suffix in _SRC_SUFFIXES:
            files.append(p)
    return files


def get_specific_files(only: str, project_root: Path) -> list[Path]:
    files: list[Path] = []
    for part in only.split(","):
        p = project_root / part.strip()
        if p.exists() and p.suffix in _SRC_SUFFIXES:
            files.append(p)
        else:
            print(f"warning: {part.strip()} not found or not a source file", file=sys.stderr)
    return files


def scan_file(fpath: Path, project_root: Path) -> list[dict]:
    try:
        lines = fpath.read_text(errors="replace").splitlines()
    except OSError as exc:
        return [{"error": str(exc), "file": str(fpath.relative_to(project_root))}]

    findings: list[dict] = []
    for rule_id, severity, category, desc, pattern in CHECKS:
        rx = re.compile(pattern)
        for lineno, line in enumerate(lines, 1):
            stripped = line.strip()
            # skip pure comment lines (// or /* style)
            if stripped.startswith("//") or stripped.startswith("*") or stripped.startswith("/*"):
                continue
            if rx.search(line):
                findings.append({
                    "rule":        rule_id,
                    "severity":    severity,
                    "category":    category,
                    "description": desc,
                    "file":        str(fpath.relative_to(project_root)),
                    "line":        lineno,
                    "text":        stripped[:120],
                })
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument("--full", action="store_true", help="scan all of src/")
    mode_group.add_argument("--only", help="comma-separated file paths relative to project root")
    parser.add_argument("--project-root", default=os.getcwd(), help="project root")
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()

    if args.only:
        mode  = "specific"
        files = get_specific_files(args.only, project_root)
    elif args.full:
        mode  = "full"
        files = get_full_src_files(project_root)
    else:
        mode  = "changed"
        files = get_changed_files(project_root)

    all_findings: list[dict] = []
    for fpath in files:
        all_findings.extend(scan_file(fpath, project_root))

    severity_order = {"CRITICAL": 0, "HIGH": 1, "MEDIUM": 2, "LOW": 3}
    all_findings.sort(key=lambda f: (severity_order.get(f.get("severity", "LOW"), 9),
                                     f.get("file", ""), f.get("line", 0)))

    by_severity: dict[str, int] = {}
    for f in all_findings:
        s = f.get("severity", "UNKNOWN")
        by_severity[s] = by_severity.get(s, 0) + 1

    output = {
        "mode":          mode,
        "files_scanned": len(files),
        "scanned_files": [str(f.relative_to(project_root)) for f in files],
        "total":         len(all_findings),
        "summary":       by_severity,
        "findings":      all_findings,
    }
    print(json.dumps(output, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
