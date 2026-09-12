#!/usr/bin/env python3
"""Extract the serial command ladder from src/command_functions.cpp.

`commandAction()` is a 5,900-line if/else ladder of `commandCheck(msg_text+2,
"name")` tests. Two things about it have to be captured before it is replaced
by a table (audit D2-06/D2-07/D2-10), and neither is visible at runtime:

1. **The ordered name list.** `commandCheck()` is a *prefix* match -- it
   truncates the input to the length of the command name before comparing
   (`command_functions.cpp:157-168`). So `--softser app` matches the input
   `--softser app0`, and whichever test comes first in the ladder wins. Order
   is therefore behaviour, and the after-table must reproduce it.

2. **The shadowed pairs.** Wherever a command name is a prefix of a later
   one, the later branch is unreachable for its own canonical spelling. Some
   of these are deliberate (a bare `--x` and `--x <arg>` handled together),
   some are defects -- the audit names `--softser app0` (defect 3) as one.
   This tool finds them all mechanically instead of by reading.

Outputs, written to stdout or a directory:

    ladder-order.txt    one `<index> <line> <name>` row per ladder test
    inner-checks.txt    the disambiguation calls inside already-matched bodies
    shadowed.txt        every (earlier, later) pair where earlier is a prefix
                        of later, with both line numbers

    python3 test/golden/extract_commands.py --list
    python3 test/golden/extract_commands.py --shadowed
    python3 test/golden/extract_commands.py --out test/golden/corpus/commands/
    python3 test/golden/extract_commands.py --self-test

Two caveats this tool does not resolve, and a caller must not read past:

- **Preprocessor guards are recorded, not evaluated.** A name that appears
  twice under *different* `#if` conditions is the platform split, not a dead
  branch, and lands in `guarded-duplicates.txt` for a human to confirm --
  "different guard text" is not a proof of mutual exclusion.
  `duplicates.txt` holds only same-guard repeats.
- **A duplicate is only dead if the first branch cannot fall through.** Most
  ladder branches return or set a flag and continue; the pattern `"passwd "`
  followed by `"passwd"` is deliberate -- the trailing space makes the two
  literals different lengths, so the bare form falls through to the second
  site by design.

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
LADDER_SRC = REPO_ROOT / "src" / "command_functions.cpp"

# commandCheck(msg_text+2, (char*)"name") -- the only call shape in the ladder;
# the other two matches of the word in the file are the definition and a
# comment, and neither has this argument form.
_CALL_RE = re.compile(
    r'commandCheck\(\s*msg_text\s*\+\s*\d+\s*,\s*\(\s*char\s*\*\s*\)\s*"((?:[^"\\]|\\.)*)"'
)


@dataclass(frozen=True)
class Command:
    index: int
    line: int
    name: str          # as written; no leading "--", msg_text+2 skipped it
    guard: str = ""    # the enclosing #if conditions, innermost last, " && "-joined

    @property
    def token(self) -> str:
        """The command word without the argument separator."""
        return self.name.rstrip()

    @property
    def takes_arg(self) -> bool:
        """A trailing space in the literal means the branch expects a value."""
        return self.name.endswith(" ")


# A ladder test is an `if(` at the ladder's own indentation -- four spaces
# inside commandAction(), optionally preceded by `else` on its own line. Every
# other commandCheck() call is an *inner* one: a disambiguation inside a branch
# that has already matched, e.g. `setname `/`operatorname ` sharing a body and
# asking again which of the two it was (:2555-2557). Those inner calls are not
# ladder entries, they cannot shadow anything, and counting them as duplicates
# turns eight deliberate constructs into fake defects -- which is what a first,
# indentation-blind version of this tool did.
_LADDER_IF_RE = re.compile(r"^    (?:else\s+)?if\(")


def _condition_spans(lines: List[str]) -> List[range]:
    """Line ranges (1-based) covered by each ladder-level `if` condition.

    The condition can wrap (`persistflash on ||` on :5167 continues on :5168),
    so the span ends where the parentheses balance, not at the newline.
    """
    spans: List[range] = []
    for i, text in enumerate(lines):
        if not _LADDER_IF_RE.match(text):
            continue
        depth = 0
        j = i
        while j < len(lines):
            depth += lines[j].count("(") - lines[j].count(")")
            if depth <= 0:
                break
            j += 1
        spans.append(range(i + 1, j + 2))
    return spans


_CPP_IF_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b\s*(.*)$")


def _guard_stack(lines: List[str]) -> List[str]:
    """Per line (1-based, index 0 unused): the enclosing #if conditions.

    Deliberately shallow -- it records the condition *text*, it does not
    evaluate it. That is enough for the one thing it is needed for: telling
    apart two sites that can both compile into the same image from two that
    sit in different arms of the same `#if`, which is why the ESP32 and nRF52
    copies of `udplog on` are not a duplicate.
    """
    out: List[str] = [""]
    stack: List[str] = []
    for text in lines:
        m = _CPP_IF_RE.match(text)
        if m:
            kind, cond = m.group(1), m.group(2).strip()
            if kind in ("if", "ifdef", "ifndef"):
                prefix = {"ifdef": "defined ", "ifndef": "!defined "}.get(kind, "")
                stack.append(prefix + cond)
            elif kind == "elif":
                if stack:
                    stack[-1] = "elif " + cond
            elif kind == "else":
                if stack:
                    stack[-1] = "!(" + stack[-1] + ")"
            elif kind == "endif":
                if stack:
                    stack.pop()
        # Each level is parenthesised before joining. Without that, a nested
        # `#if` whose own condition has a top-level `||` loses its grouping:
        # `#if INSTRUMENT_ENABLED` around
        # `#if (defined(ESP32) && !defined(X)) || defined(NRF52_SERIES)`
        # joined flat reads, under C precedence, as
        # `(INSTRUMENT_ENABLED && ESP32 && !X) || NRF52_SERIES` -- which says
        # the command is present on any nRF52 build. It is not; it needs
        # INSTRUMENT_ENABLED too. Found by command_name_scan.py bucketing
        # `srvip ` (command_functions.cpp:5268) as unexplained-absent on the
        # shipping nRF52 image, 2026-09-11.
        out.append(" && ".join(f"({c})" for c in stack))
    return out


def blank_comments(text: str) -> str:
    """Replace C comments with spaces, keeping every line and column.

    Without this the scan counts commented-out branches as live commands. The
    very first entry it used to report, `"compress "` at :288, sits inside a
    `/* TEST ... */` block: the node answers `...wrong command --compress`,
    which is how it was found -- by driving the generated script at real
    hardware, not by reading. The audit's D2-01/04 names ~55 lines of
    commented-out branches, so it was never going to be the only one.

    String literals are respected, or a command containing `//` would truncate
    the rest of the file.
    """
    out = list(text)
    i, n = 0, len(text)
    state = "code"
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == '"':
                state = "string"
            elif c == "'":
                state = "char"
            elif c == "/" and nxt == "*":
                state = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            elif c == "/" and nxt == "/":
                while i < n and text[i] != "\n":
                    out[i] = " "
                    i += 1
                continue
        elif state == "string":
            if c == "\\":
                i += 2
                continue
            if c == '"':
                state = "code"
        elif state == "char":
            if c == "\\":
                i += 2
                continue
            if c == "'":
                state = "code"
        elif state == "block":
            if c == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 2
                state = "code"
                continue
            if c != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def extract(src: Path = LADDER_SRC, *, inner: bool = False) -> List[Command]:
    """Ladder-level commandCheck sites in source order.

    With `inner=True` the disambiguation calls are returned instead.
    """
    lines = blank_comments(src.read_text(errors="replace")).splitlines()
    ladder_lines = {n for span in _condition_spans(lines) for n in span}
    guards = _guard_stack(lines)

    commands: List[Command] = []
    for lineno, text in enumerate(lines, 1):
        is_ladder = lineno in ladder_lines
        if is_ladder == inner:
            continue
        for m in _CALL_RE.finditer(text):
            commands.append(
                Command(len(commands), lineno, m.group(1), guards[lineno])
            )
    return commands


def shadowed_pairs(commands: Iterable[Command]) -> List[Tuple[Command, Command]]:
    """(earlier, later) where the earlier test swallows the later name.

    Compared on the **raw literal**, trailing space included, because that is
    what `commandCheck()` compares: it truncates the input to
    `strlen(command)` and then does a full case-insensitive compare of that
    many bytes. So `"softser app"` (11 bytes, no trailing space) does swallow
    `"softser app0"` -- the first 11 bytes are equal -- while
    `"softser fixpegel "` does **not** swallow `"softser fixpegel2 "`: byte 16
    is a space in the command and a '2' in the input, and the compare fails.

    Stripping the trailing space before comparing, which an earlier version of
    this tool did, reports that second pair as a defect. It is not one.
    """
    out: List[Tuple[Command, Command]] = []
    ordered = list(commands)
    for i, earlier in enumerate(ordered):
        for later in ordered[i + 1:]:
            if later.name != earlier.name and later.name.startswith(earlier.name):
                out.append((earlier, later))
    return out


def duplicate_names(commands: Iterable[Command]) -> List[Tuple[Command, Command]]:
    """The identical literal tested twice; the second site is unreachable
    (defect 2, `setowndns `).

    Again the raw literal, not the token: `"passwd "` and `"passwd"` are two
    different commands by design -- the bare form falls through to its own
    branch precisely because the lengths differ.
    """
    seen: dict[str, Command] = {}
    out: List[Tuple[Command, Command]] = []
    for cmd in commands:
        if cmd.name in seen:
            first = seen[cmd.name]
            # Two sites under different #if conditions are the platform split,
            # not a dead branch: the ESP32 and nRF52 copies of `udplog on`
            # never compile into the same image. Reported separately by
            # `guarded_duplicates()` rather than dropped, because "different
            # guard text" is not a proof of mutual exclusion.
            if first.guard == cmd.guard:
                out.append((first, cmd))
        else:
            seen[cmd.name] = cmd
    return out


def guarded_duplicates(commands: Iterable[Command]) -> List[Tuple[Command, Command]]:
    """Same literal, different enclosing #if: for a human to confirm."""
    seen: dict[str, Command] = {}
    out: List[Tuple[Command, Command]] = []
    for cmd in commands:
        if cmd.name in seen and seen[cmd.name].guard != cmd.guard:
            out.append((seen[cmd.name], cmd))
        elif cmd.name not in seen:
            seen[cmd.name] = cmd
    return out


def render_order(commands: List[Command]) -> str:
    return "".join(
        f"{c.index:4d} {c.line:6d} {c.name!r}\n" for c in commands
    )


def render_shadowed(pairs: List[Tuple[Command, Command]]) -> str:
    return "".join(
        f"{a.name!r} (line {a.line}) shadows {b.name!r} (line {b.line})\n"
        for a, b in pairs
    )


def _self_test() -> int:
    failures = 0
    commands = extract()

    # The commented-out `"compress "` at :288 must not be in the list.
    if any(c.name.strip() == "compress" for c in commands):
        failures += 1
        print("FAIL: a commented-out command is counted as a ladder entry")
    blanked = blank_comments('a = "/* not a comment */"; /* real */ b;')
    if '"/* not a comment */"' not in blanked or "real" in blanked:
        failures += 1
        print(f"FAIL: comment stripping: {blanked!r}")
    if len(blank_comments("x /* a\nb */ y")) != len("x /* a\nb */ y"):
        failures += 1
        print("FAIL: comment stripping changed the length")

    if len(commands) < 280:
        failures += 1
        print(f"FAIL: only {len(commands)} ladder sites found")

    # The split must be exhaustive: every call is either a ladder test or an
    # inner disambiguation, never both and never neither.
    inner = extract(inner=True)
    raw = LADDER_SRC.read_text(errors="replace")
    live = len(_CALL_RE.findall(blank_comments(raw)))
    total = len(_CALL_RE.findall(raw))
    if len(commands) + len(inner) != live:
        failures += 1
        print(f"FAIL: {len(commands)} ladder + {len(inner)} inner != {live} live calls")
    if live >= total:
        failures += 1
        print(f"FAIL: comment stripping removed nothing ({live} of {total})")

    # An inner disambiguation must not be reported as a ladder duplicate:
    # `operatorname ` (:2555 ladder, :2557 inner) is the canonical shape.
    if any(a.token == "operatorname" for a, _ in duplicate_names(commands)):
        failures += 1
        print("FAIL: inner disambiguation counted as a ladder duplicate")

    # The two documented defects must both be visible to this tool, or it is
    # not detecting what it claims to detect.
    dups = duplicate_names(commands)
    if not any(a.name == "setowndns " for a, _ in dups):
        failures += 1
        print("FAIL: --setowndns duplicate (audit defect 2) not detected")

    shadows = shadowed_pairs(commands)
    # Audit defect 3 was FIXED 2026-09-12: the `softser app0` rung now sits
    # ahead of `softser app` in the ladder, so the longer name is reached and
    # its `iNextTelemetry = 0` runs. This assertion used to require the
    # shadowing to be PRESENT -- it was pinning a known defect, which is the
    # right thing to do while the defect stands and the wrong thing after it is
    # fixed. Inverted rather than deleted, so the fix cannot silently regress.
    # The standing gate against the whole class is
    # test/golden/command_ladder_lint.py.
    if any(b.name == "softser app0" for _, b in shadows):
        failures += 1
        print("FAIL: softser app0 is shadowed again -- audit defect 3 regressed")

    # The near-miss that must NOT be reported: byte 16 differs.
    if any(b.name == "softser fixpegel2 " for _, b in shadows):
        failures += 1
        print("FAIL: softser fixpegel2 reported as shadowed; it is not")

    # Deliberate arg/bare pairs must not be reported as duplicates.
    for token in ("passwd", "maxhop", "heap"):
        if any(a.name.rstrip() == token for a, _ in dups):
            failures += 1
            print(f"FAIL: {token!r} arg/bare pair counted as a duplicate")

    # The platform split must land in the guarded bucket, not the defect one.
    if any(a.name.startswith("udplog") for a, _ in dups):
        failures += 1
        print("FAIL: the ESP32/nRF52 udplog pair counted as a dead duplicate")
    if not any(a.name == "udplog on" for a, _ in guarded_duplicates(commands)):
        failures += 1
        print("FAIL: the ESP32/nRF52 udplog pair was not reported as guarded")

    # Nested guards must keep their grouping, or a consumer that evaluates the
    # text gets the wrong answer for every command built this way.
    srvip = [c for c in commands if c.name.strip() == "srvip"]
    if not srvip:
        failures += 1
        print("FAIL: srvip not found")
    elif "(INSTRUMENT_ENABLED)" not in srvip[0].guard or not srvip[0].guard.startswith("("):
        failures += 1
        print(f"FAIL: nested guard lost its parenthesisation: {srvip[0].guard}")

    # Order must be the file order, which is what makes the prefix match
    # deterministic.
    if [c.line for c in commands] != sorted(c.line for c in commands):
        failures += 1
        print("FAIL: extracted order is not source order")

    print(f"extract_commands.py self-test: {len(commands)} sites, "
          f"{len(shadows)} shadowed pairs, {len(dups)} duplicates -- "
          + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--src", type=Path, default=LADDER_SRC)
    ap.add_argument("--list", action="store_true", help="print the ordered ladder")
    ap.add_argument("--shadowed", action="store_true", help="print shadowed pairs")
    ap.add_argument("--duplicates", action="store_true", help="print duplicate names")
    ap.add_argument("--guarded", action="store_true",
                    help="print same-name pairs that sit under different #if guards")
    ap.add_argument("--inner", action="store_true",
                    help="print the inner disambiguation calls instead of the ladder")
    ap.add_argument("--out", type=Path, default=None, help="write the fixtures here")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()

    commands = extract(args.src, inner=args.inner)
    if args.list:
        sys.stdout.write(render_order(commands))
    if args.shadowed:
        sys.stdout.write(render_shadowed(shadowed_pairs(commands)))
    if args.duplicates:
        sys.stdout.write(render_shadowed(duplicate_names(commands)))
    if args.guarded:
        sys.stdout.write(render_shadowed(guarded_duplicates(commands)))
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "ladder-order.txt").write_text(render_order(commands))
        (args.out / "shadowed.txt").write_text(render_shadowed(shadowed_pairs(commands)))
        (args.out / "duplicates.txt").write_text(render_shadowed(duplicate_names(commands)))
        (args.out / "guarded-duplicates.txt").write_text(
            render_shadowed(guarded_duplicates(commands)))
        (args.out / "inner-checks.txt").write_text(render_order(extract(args.src, inner=True)))
        print(f"{len(commands)} commands written to {args.out}", file=sys.stderr)
    if not (args.list or args.shadowed or args.duplicates or args.guarded or args.out):
        ap.error("nothing to do: pass --list, --shadowed, --duplicates, --guarded or --out")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
