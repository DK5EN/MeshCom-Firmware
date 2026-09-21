#!/usr/bin/env python3
"""Worst-case call-chain stack depth for an Xtensa ELF, as a CI gate.

For each `--root` symbol, walks the direct-call graph (call0/call4/call8/
call12) and reports the deepest chain by summing each function's frame size,
taken from the windowed-ABI `entry a1, N` prologue that xtensa-gcc emits for
every non-leaf function. This covers library code too, not just files that
happen to have a `.su` (gcc stack-usage) sidecar.

Two things this canNOT see, by construction:

  * Indirect calls (callx0/callx4/callx8/callx12) -- the target is only known
    at runtime. Any indirect call on the reported chain is printed as a
    warning; the reported total is then a LOWER BOUND, not an exact figure.
  * Interrupts. ESP-IDF dispatches to a per-CPU ISR stack
    (CONFIG_FREERTOS_ISR_STACKSIZE), so only the exception frame and the
    register-window spill land on the interrupted task's stack -- a few
    hundred bytes per nesting level, bounded but not zero. --margin exists
    to leave headroom for this and for the two approximations below, not to
    make it go away -- see docs/stack-budget.md.

Two smaller approximations in the walk itself:

  * A `call*` that targets `<function+0xNN>` (a jump into the middle of a
    function -- register-window spill/fill trampolines and some tail calls
    do this) is attributed to that function's own frame size, the same as a
    call to its start. This is what the compiler actually generated; it is
    not a parsing shortcut.
  * A cycle in the call graph (recursion, direct or mutual) stops the walk
    at the point it re-enters a function already on the current path, the
    same way the prototype did. That function's own frame is still counted,
    but nothing below the cycle is. This is reported as a loud WARNING, not
    folded silently into the total -- a cycle means the real worst case is
    unbounded, not equal to the number printed.

Root symbols may be given mangled or demangled (a bare name like
"getExtern" resolves to "_Z9getExternPhi" by matching the demangled form).
An unresolvable or ambiguous root is a hard error -- this script never
silently reports 0 bytes for a symbol it could not find.
"""
from __future__ import annotations

import argparse
import collections
import difflib
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

DEFAULT_OBJDUMP_CANDIDATES = [
    "~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump",
]

# Default safety margin subtracted from the budget before the pass/fail
# check. See docs/stack-budget.md for the justification; in short it covers
# rounding in frame sizes and the window-spill trampoline overhead described
# above, NOT interrupt nesting, which is unbounded and must stay a separate,
# human judgement call.
DEFAULT_MARGIN = 512

FUNC_RE = re.compile(r"^[0-9a-f]+ <(.+)>:$")
ENTRY_RE = re.compile(r"\bentry\s+a1,\s*(0x[0-9a-f]+|[0-9]+)")
CALL_RE = re.compile(r"\bcall(?:0|4|8|12)\s+[0-9a-f]+\s+<([^>]+)>")
CALLX_RE = re.compile(r"\bcallx(?:0|4|8|12)\b")


def strip_offset(name: str) -> str:
    """'foo+0x8' -> 'foo'; a call into the middle of a function is
    attributed to that function's own frame."""
    return name.split("+", 1)[0]


@dataclass
class Disasm:
    frame: Dict[str, int] = field(default_factory=dict)
    calls: Dict[str, Set[str]] = field(default_factory=dict)
    indirect: Set[str] = field(default_factory=set)


def find_objdump(explicit: Optional[str]) -> str:
    if explicit:
        if not os.path.isfile(explicit):
            sys.exit(f"error: --objdump path does not exist: {explicit}")
        return explicit
    for candidate in DEFAULT_OBJDUMP_CANDIDATES:
        expanded = os.path.expanduser(candidate)
        if os.path.isfile(expanded):
            return expanded
    found = shutil.which("xtensa-esp32-elf-objdump")
    if found:
        return found
    sys.exit(
        "error: could not auto-detect xtensa-esp32-elf-objdump; pass --objdump "
        "explicitly (checked "
        + ", ".join(os.path.expanduser(c) for c in DEFAULT_OBJDUMP_CANDIDATES)
        + " and $PATH)"
    )


def find_cppfilt(objdump_path: str) -> Optional[str]:
    """Derive the matching c++filt from the objdump path (same toolchain
    bin/ directory), falling back to whatever is on PATH."""
    directory = os.path.dirname(objdump_path)
    basename = os.path.basename(objdump_path)
    if "objdump" in basename:
        candidate = os.path.join(directory, basename.replace("objdump", "c++filt"))
        if os.path.isfile(candidate):
            return candidate
    return shutil.which("c++filt")


def run_objdump_disassembly(objdump: str, elf: str) -> str:
    if not os.path.isfile(elf):
        sys.exit(f"error: ELF not found: {elf}")
    proc = subprocess.run(
        [objdump, "-d", elf], capture_output=True, text=True
    )
    if proc.returncode != 0:
        sys.exit(
            f"error: {objdump} -d {elf} failed (exit {proc.returncode}):\n"
            f"{proc.stderr.strip()}"
        )
    return proc.stdout


def parse_disassembly(text: str) -> Disasm:
    frame: Dict[str, int] = {}
    calls: Dict[str, Set[str]] = collections.defaultdict(set)
    indirect: Set[str] = set()
    cur: Optional[str] = None
    for line in text.splitlines():
        m = FUNC_RE.match(line)
        if m:
            cur = m.group(1)
            frame.setdefault(cur, 0)
            continue
        if cur is None:
            continue
        m = ENTRY_RE.search(line)
        if m:
            frame[cur] = max(frame[cur], int(m.group(1), 0))
            continue
        m = CALL_RE.search(line)
        if m:
            calls[cur].add(strip_offset(m.group(1)))
            continue
        if CALLX_RE.search(line):
            indirect.add(cur)
    return Disasm(frame=frame, calls=dict(calls), indirect=indirect)


def demangle_all(names: List[str], cppfilt: Optional[str]) -> Dict[str, str]:
    """Map raw symbol -> demangled form. Without a usable c++filt, every
    symbol demangles to itself (mangled C++ roots then simply won't resolve
    by their plain name, which is a clear error, not a silent wrong answer)."""
    if not names:
        return {}
    if cppfilt is None:
        return {n: n for n in names}
    proc = subprocess.run(
        [cppfilt], input="\n".join(names), capture_output=True, text=True
    )
    lines = proc.stdout.splitlines()
    if proc.returncode != 0 or len(lines) != len(names):
        return {n: n for n in names}
    return dict(zip(names, lines))


def demangled_base_name(demangled: str) -> str:
    """'getExtern(unsigned char*, int)' -> 'getExtern'; a name with no
    parameter list (plain C symbols) is returned unchanged."""
    paren = demangled.find("(")
    return demangled[:paren] if paren != -1 else demangled


def resolve_root(root: str, frame: Dict[str, str], demangled: Dict[str, str]) -> str:
    """Resolve a user-given root name (mangled or demangled) to the exact
    mangled/raw symbol key used in `frame`/`calls`. Raises SystemExit with a
    clear message (including near-matches) rather than ever returning an
    unresolved name -- an unknown root must be a hard error, never a silent
    0-byte pass."""
    if root in frame:
        return root

    base_names: Dict[str, List[str]] = collections.defaultdict(list)
    for raw, dem in demangled.items():
        base_names[demangled_base_name(dem)].append(raw)
        base_names[dem].append(raw)

    matches = sorted(set(base_names.get(root, [])))
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        listing = "\n".join(f"    {m}  ({demangled.get(m, m)})" for m in matches)
        sys.exit(
            f"error: root '{root}' is ambiguous, matches {len(matches)} symbols:\n"
            f"{listing}\n"
            "Pass the exact mangled symbol name to disambiguate."
        )

    all_base_names = sorted(set(base_names.keys()))
    close = difflib.get_close_matches(root, all_base_names, n=5, cutoff=0.5)
    hint = (
        "near-matches: " + ", ".join(close)
        if close
        else "no near-matches found in this ELF"
    )
    sys.exit(
        f"error: root symbol '{root}' not found in ELF disassembly ({hint}). "
        "An unresolvable root is treated as an error, not a pass."
    )


@dataclass
class ChainResult:
    total: int
    path: List[Tuple[str, int]]  # (function, own frame size)
    recursion_hit: Optional[str]  # function name where a cycle closed, if any
    indirect_on_path: List[str]


def deepest_chain(root: str, disasm: Disasm) -> ChainResult:
    memo: Dict[str, ChainResult] = {}
    onstack: Set[str] = set()

    def walk(fn: str) -> ChainResult:
        if fn in memo:
            return memo[fn]
        own = disasm.frame.get(fn, 0)
        if fn in onstack:
            # Cycle: stop here, but say so loudly in the result rather than
            # quietly returning a truncated-but-unflagged number.
            return ChainResult(own, [(fn, own)], fn, [])
        onstack.add(fn)
        best = ChainResult(0, [], None, [])
        for callee in sorted(disasm.calls.get(fn, ())):  # sorted: stable, deterministic output
            sub = walk(callee)
            if sub.total > best.total:
                best = sub
        onstack.discard(fn)
        result = ChainResult(
            total=own + best.total,
            path=[(fn, own)] + best.path,
            recursion_hit=best.recursion_hit,
            indirect_on_path=(
                [fn] + best.indirect_on_path
                if fn in disasm.indirect
                else best.indirect_on_path
            ),
        )
        memo[fn] = result
        return result

    return walk(root)


def format_report(
    root_display: str,
    resolved: str,
    demangled: Dict[str, str],
    result: ChainResult,
    budget: int,
    margin: int,
) -> Tuple[str, bool]:
    lines = []
    threshold = budget - margin
    # Ein Zyklus im Aufrufgraphen hat keine statische Schranke: der Lauf bricht
    # am Wiedereintritt ab, die Summe unten ist dann keine Obergrenze, sondern
    # ein willkuerlicher Zwischenstand. Ein solcher Lauf darf nie bestehen --
    # sonst waere genau das das stille Bestehen, gegen das dieses Gate da ist.
    passed = result.total <= threshold and result.recursion_hit is None
    verdict = "PASS" if passed else "FAIL"

    lines.append(f"ROOT {root_display}" + (f" ({resolved})" if resolved != root_display else ""))
    lines.append(f"  deepest chain: {result.total} bytes")
    for fn, own in result.path:
        dem = demangled.get(fn)
        label = f"{fn} ({dem})" if dem and dem != fn else fn
        marker = ""
        if fn == result.recursion_hit:
            marker = "  <-- RECURSION: re-enters a function already on this path"
        lines.append(f"    {label}: {own} bytes{marker}")
    lines.append(f"  total: {result.total} bytes")
    lines.append(f"  budget: {budget} bytes, margin: {margin} bytes, threshold: {threshold} bytes")
    lines.append(f"  verdict: {verdict}")

    if result.recursion_hit:
        lines.append(
            "  WARNING: this chain contains a call-graph cycle (recursion, direct or "
            "mutual). The walk stopped at the re-entry point, so the total above is "
            "NOT a true worst case -- a recursive path has no static bound. "
            "This alone makes the verdict FAIL, whatever the total says."
        )
    if result.indirect_on_path:
        lines.append(
            "  WARNING: indirect call(s) on this chain, target unresolvable at build "
            "time: " + ", ".join(result.indirect_on_path) + ". "
            "The total above is a LOWER BOUND, not the true worst case."
        )

    return "\n".join(lines), passed


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Worst-case direct-call stack depth for an Xtensa ELF, checked "
            "against a budget. Exits non-zero if any --root exceeds "
            "budget - margin. See docs/stack-budget.md."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("elf", help="path to the ELF to analyse")
    parser.add_argument(
        "--objdump",
        default=None,
        help=(
            "path to xtensa-esp32-elf-objdump (default: auto-detect under "
            "~/.platformio/packages/toolchain-xtensa-esp32/bin/, then $PATH)"
        ),
    )
    parser.add_argument(
        "--root",
        action="append",
        required=True,
        dest="roots",
        metavar="SYMBOL",
        help=(
            "root symbol to walk from; repeatable. Mangled or demangled "
            "(e.g. getExternUDP or _Z12getExternUDPv)."
        ),
    )
    parser.add_argument(
        "--budget",
        type=int,
        required=True,
        metavar="BYTES",
        help="stack budget in bytes (e.g. the task's configured stack size)",
    )
    parser.add_argument(
        "--margin",
        type=int,
        default=DEFAULT_MARGIN,
        metavar="BYTES",
        help=(
            f"safety margin subtracted from --budget before the pass/fail "
            f"check (default: {DEFAULT_MARGIN}; see docs/stack-budget.md)"
        ),
    )
    args = parser.parse_args()

    objdump = find_objdump(args.objdump)
    cppfilt = find_cppfilt(objdump)

    disasm_text = run_objdump_disassembly(objdump, args.elf)
    disasm = parse_disassembly(disasm_text)
    if not disasm.frame:
        sys.exit(
            f"error: no disassembled functions found in {args.elf} via {objdump} "
            "-d -- is this the right ELF/toolchain pair?"
        )

    demangled = demangle_all(list(disasm.frame.keys()), cppfilt)
    if cppfilt is None:
        print(
            "warning: no c++filt found next to objdump or on PATH; root names "
            "must be given in mangled form",
            file=sys.stderr,
        )

    all_passed = True
    reports = []
    for root_display in args.roots:
        resolved = resolve_root(root_display, disasm.frame, demangled)
        result = deepest_chain(resolved, disasm)
        text, passed = format_report(
            root_display, resolved, demangled, result, args.budget, args.margin
        )
        reports.append(text)
        all_passed = all_passed and passed

    print("\n\n".join(reports))
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
