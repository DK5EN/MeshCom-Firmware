#!/usr/bin/env python3
"""Ladder-order gate for commandAction()'s commandCheck() dispatch chain.

commandCheck() in src/command_functions.cpp does not compare for equality:

    vmsg[strlen(command)] = 0x00;      // truncates the INPUT to command's length
    if(casecmp(vmsg, command) == 0) return 0;

It truncates the incoming command to the length of the candidate name and
compares THAT. This is a PREFIX match. commandAction() chains ~324 of these
in one long if/else-if ladder, so when name A is a prefix of name B, whichever
rung is tested first wins -- if A comes before B, B's rung can never be
reached: A already matched and returned.

This is exactly what happened live: "softser app" (line ~3284, sets
bSOFTSER_APP and clears strSOFTSER_BUF, then returns) was tested before
"softser app0" (sets iNextTelemetry = 0 in addition to the same two effects).
Every "--softser app0" command silently ran as "--softser app" --
iNextTelemetry never got reset -- and nothing failed at compile or link time
to say so. Both commands are exercised in the BLE golden corpus, so the
captures recorded the collapsed behaviour as correct.

The ladder already gets this right elsewhere on purpose -- see the "heap "
vs "heap" comment at command_functions.cpp:4843 -- so the discipline is
known, just unenforced. This script enforces it: extract every
`commandCheck(<anything>, (char*)"NAME")` rung in source order, and fail
when a shorter name's rung precedes a longer name it is a prefix of.

  python3 test/golden/command_ladder_lint.py
  python3 test/golden/command_ladder_lint.py --self-test

Exit 0 when every prefix pair is ordered longer-before-shorter (or there are
no prefix pairs at all), 1 on any violation or if the extraction pattern
finds nothing.
"""
import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

import re

REPO = Path(__file__).resolve().parents[2]
SOURCE = "src/command_functions.cpp"

# `commandCheck(<anything up to the comma>, (char*)"NAME")` -- the exact shape
# every rung in command_functions.cpp uses (the function definition itself
# takes `char *msg, char *command` with no string literal, so it never
# matches; nor does a bare mention like "commandCheck() is a prefix match" in
# a comment, since it has no literal second argument either).
RUNG = re.compile(r'commandCheck\([^,]*,\s*\(char\*\)\s*"([^"]*)"\s*\)')


@dataclass
class Rung:
    name: str
    line: int


@dataclass
class Violation:
    prefix_name: str
    prefix_line: int
    blocked_name: str
    blocked_line: int

    def message(self, rel: str) -> str:
        return (
            f'{rel}:{self.prefix_line}: "{self.prefix_name}" is tested before '
            f'{rel}:{self.blocked_line}: "{self.blocked_name}", and is a '
            f'prefix of it -- commandCheck() is a prefix match, so '
            f'"{self.blocked_name}" can never be reached; it always matches '
            f'"{self.prefix_name}" first. Move the longer rung ahead of the '
            f'shorter one.')


def blank_comments(text: str) -> str:
    """Replace comment bodies with spaces, preserving every newline.

    Rungs inside commented-out blocks are NOT part of the ladder and must not
    be scanned: `compress `, `softser test`, `softser test0` and `softser xml`
    all sit inside /* ... */ today. None collides with a live name right now,
    but `softser test` is itself a prefix of `softser test0`, so scanning dead
    code would report a violation about code that cannot run -- a false
    positive in a gate, which is worse than no gate.

    Comment bodies are blanked rather than deleted so that byte offsets, and
    therefore the reported line numbers, stay exact. Same lesson as
    carve_extern_lint.py, which was hidden from its own definitions by a
    trailing comment containing a parenthesis.
    """
    def blank(m: "re.Match[str]") -> str:
        return "".join(c if c == "\n" else " " for c in m.group(0))
    # strings first would be safer in general C, but no commandCheck() literal
    # in this file contains a comment marker -- asserted by the self-test.
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    text = re.sub(r"//[^\n]*", blank, text)
    return text


def extract_rungs(text: str) -> List[Rung]:
    """Every commandCheck() rung, in the order it appears in the file.

    Commented-out rungs are skipped -- see blank_comments().
    """
    out = []
    for m in RUNG.finditer(blank_comments(text)):
        line = text.count("\n", 0, m.start()) + 1
        out.append(Rung(name=m.group(1), line=line))
    return out


def find_violations(rungs: List[Rung]) -> Tuple[List[Violation], int]:
    """Compare every pair of DISTINCT names for a prefix relationship.

    A name can legitimately appear more than once (e.g. the same literal
    reused inside a handler body to tell "on" from "off" after an `||`
    rung) -- that is not a ladder-order question, so only the first
    occurrence of each name is what "where does this rung sit in the
    ladder" means. Comparing first-occurrence lines catches the case that
    matters: whichever rung comes first in the file is the one that will
    actually run when the mesh sends a matching command.

    Returns (violations, correctly_ordered_pair_count).
    """
    first_line = {}
    order: List[str] = []
    for r in rungs:
        if r.name not in first_line:
            first_line[r.name] = r.line
            order.append(r.name)

    violations: List[Violation] = []
    correct = 0
    for i, a in enumerate(order):
        for b in order[i + 1:]:
            if a == b:
                continue
            shorter, longer = (a, b) if len(a) <= len(b) else (b, a)
            if len(shorter) == len(longer):
                continue
            if not longer.startswith(shorter):
                continue
            if first_line[shorter] < first_line[longer]:
                violations.append(Violation(
                    prefix_name=shorter, prefix_line=first_line[shorter],
                    blocked_name=longer, blocked_line=first_line[longer]))
            else:
                correct += 1
    return violations, correct


def analyze(text: str) -> Tuple[List[Rung], List[Violation], int]:
    rungs = extract_rungs(text)
    violations, correct = find_violations(rungs)
    return rungs, violations, correct


def check(path: Optional[Path] = None) -> Tuple[List[Rung], List[Violation], int, List[str]]:
    """Run the analysis against a real file. Returns (rungs, violations,
    correct_count, fatal_problems) -- fatal_problems is non-empty only when
    the extraction pattern found nothing, which must never look like a
    clean pass."""
    p = path if path is not None else (REPO / SOURCE)
    text = p.read_text()
    rungs, violations, correct = analyze(text)
    fatal = []
    if not rungs:
        fatal.append(
            f"no commandCheck(...) rungs found in {p} -- the RUNG pattern "
            f"stopped matching the source, this check is now void")
    return rungs, violations, correct, fatal


def self_test() -> int:
    ok = True

    cases = [
        ("correctly-ordered prefix pair", (
            '    if(commandCheck(msg_text+2, (char*)"heap ") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"heap") == 0) { return; }\n'
        ), "clean"),
        ("reversed prefix pair", (
            '    if(commandCheck(msg_text+2, (char*)"softser app") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"softser app0") == 0) { return; }\n'
        ), "fatal"),
        ("no prefix pairs at all", (
            '    if(commandCheck(msg_text+2, (char*)"utcoff") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"maxv") == 0) { return; }\n'
        ), "clean"),
        ("unrelated names", (
            '    if(commandCheck(msg_text+2, (char*)"volt on") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"proz off") == 0) { return; }\n'
        ), "clean"),
        # A reversed pair that is COMMENTED OUT must stay clean: dead rungs are
        # not part of the ladder, and reporting them is a false positive in a
        # gate. This shape is real -- `softser test`/`softser test0` sit inside
        # a /* */ block in command_functions.cpp today.
        ("reversed prefix pair inside a block comment", (
            '    /*\n'
            '    if(commandCheck(msg_text+2, (char*)"softser test") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"softser test0") == 0) { return; }\n'
            '    */\n'
            '    if(commandCheck(msg_text+2, (char*)"utcoff") == 0) { return; }\n'
        ), "clean"),
        ("reversed prefix pair behind a line comment", (
            '    // if(commandCheck(msg_text+2, (char*)"softser test") == 0) { return; }\n'
            '    // if(commandCheck(msg_text+2, (char*)"softser test0") == 0) { return; }\n'
            '    if(commandCheck(msg_text+2, (char*)"utcoff") == 0) { return; }\n'
        ), "clean"),
        # ...but a LIVE rung after a comment block must still be seen, so the
        # blanking cannot swallow real code.
        ("live reversed pair following a comment block", (
            '    /* an explanatory comment about softser */\n'
            '    if(commandCheck(msg_text+2, (char*)"softser app") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"softser app0") == 0) { return; }\n'
        ), "fatal"),
        ("pattern matches nothing", (
            '    // this file has no commandCheck rungs at all\n'
            '    if(x == 0) { return; }\n'
        ), "fatal"),
    ]

    for name, text, want in cases:
        rungs, violations, correct = analyze(text)
        fatal = not rungs
        if want == "fatal" and fatal:
            good = True
            detail = "no rungs found"
        elif want == "fatal" and violations:
            good = True
            detail = f"{len(violations)} violation(s): " + "; ".join(
                v.message("test.cpp") for v in violations)
        elif want == "clean":
            good = (not fatal) and (not violations)
            detail = f"{len(rungs)} rung(s), {correct} correctly-ordered pair(s)"
        else:
            good = False
            detail = "expected fatal but got neither no-rungs nor violations"
        print(f"  {'ok ' if good else 'FAIL'} {name}: {detail}")
        ok = ok and good

    # the reversed case must name BOTH line numbers, or a fix could not be
    # located from the output alone
    _, violations, _ = analyze(
        '    if(commandCheck(msg_text+2, (char*)"softser app") == 0) { return; }\n'
        '    else\n'
        '    if(commandCheck(msg_text+2, (char*)"softser app0") == 0) { return; }\n')
    named_both_lines = (
        len(violations) == 1
        and violations[0].prefix_line == 1
        and violations[0].blocked_line == 3)
    print(f"  {'ok ' if named_both_lines else 'FAIL'} "
          f"reversed pair names both rung line numbers: {violations}")
    ok = ok and named_both_lines

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    rungs, violations, correct, fatal = check()
    for f in fatal:
        print(f"FATAL: {f}")
    if fatal:
        return 1

    names = {r.name for r in rungs}
    print(f"{len(rungs)} rung(s), {len(names)} distinct name(s), "
          f"{len(violations) + correct} prefix pair(s) found "
          f"({correct} correctly ordered, {len(violations)} violation(s))")

    for v in violations:
        print(f"VIOLATION {v.message(SOURCE)}")

    if violations:
        print(f"\n{len(violations)} violation(s)")
        return 1

    print("command ladder: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
