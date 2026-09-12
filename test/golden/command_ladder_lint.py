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

A SECOND, distinct way the same ladder shadows itself: two rungs with the
EXACTLY SAME name. commandCheck() returns on the first match, so whichever
rung is tested first wins and the second, identically-named rung can never
be reached -- dead code, same root cause as the prefix case, different
shape. This is not hypothetical: `commandCheck(msg_text+2,
(char*)"setowndns ")` appears twice, at command_functions.cpp:4001 and
:4069, and the block at :4069 is unreachable. This script also catches
that -- see find_duplicates() and is_rung_head() below for how it tells a
real duplicate from the legitimate repeats that already exist in this same
file (a literal re-tested inside its own rung's handler body, and a rung
name reused once per mutually exclusive build architecture).

  python3 test/golden/command_ladder_lint.py
  python3 test/golden/command_ladder_lint.py --self-test

Exit 0 when every prefix pair is ordered longer-before-shorter (or there are
no prefix pairs at all) AND no rung name is duplicated live, 1 on any
violation or if the extraction pattern finds nothing.
"""
import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import re

REPO = Path(__file__).resolve().parents[2]
SOURCE = "src/command_functions.cpp"

# `commandCheck(<anything up to the comma>, (char*)"NAME")` -- the exact shape
# every rung in command_functions.cpp uses (the function definition itself
# takes `char *msg, char *command` with no string literal, so it never
# matches; nor does a bare mention like "commandCheck() is a prefix match" in
# a comment, since it has no literal second argument either).
RUNG = re.compile(r'commandCheck\([^,]*,\s*\(char\*\)\s*"([^"]*)"\s*\)')

# This firmware builds for exactly two mutually exclusive architecture
# families -- nRF52 (RAK4631, guarded by NRF52_SERIES) and ESP32 (Heltec,
# T-Beam, T-Deck, ...), see this repo's CLAUDE.md Hardware & Flashing
# section -- and never both in the same binary. A rung name guarded by one
# of these and repeated, verbatim, guarded by the other cannot collide at
# runtime: at most one copy is ever compiled in. That is the ONLY
# preprocessor split this script trusts as proof of exclusivity (see
# find_duplicates()) -- it is not a general #if/#else solver, just a
# narrow, evidenced carve-out for a pattern that is actually in the file
# today ("udplog on"/"udplog off" at command_functions.cpp:4807/:4831).
ARCH_GUARDS = frozenset({"defined(NRF52_SERIES)", "defined(ESP32)"})


@dataclass
class Rung:
    name: str
    line: int
    is_head: bool


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


@dataclass
class Duplicate:
    name: str
    winning_line: int
    dead_line: int

    def message(self, rel: str) -> str:
        return (
            f'{rel}:{self.dead_line}: "{self.name}" is the exact same rung '
            f'name already tested at {rel}:{self.winning_line} -- '
            f'commandCheck() returns on the first match, so the block at '
            f'{rel}:{self.dead_line} can never run; it is dead code. '
            f'Remove it, or rename one of the two rungs.')


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


def is_rung_head(blanked: str, start: int) -> bool:
    """Is the commandCheck() call at `start` (an offset into `blanked`,
    comment bodies already blanked -- see blank_comments()) itself a test
    in the ladder's own if/else-if chain, as opposed to a re-test of an
    already-matched name nested inside a rung's own handler body?

    Only the ladder-chain position is a live "rung" for duplicate-name
    purposes. Walking backward from the call, skipping whitespace (which
    is also what a blanked-out comment looks like):

      * immediately preceded by "||"      -> another OR'd name in the SAME
        condition as a rung already in the chain (e.g. "bmx off" ==0 ||
        "bme off" ==0) -- still chain-level, so also a head.
      * immediately preceded by "if("     -> the first name in this
        condition; keep looking behind the "if" itself:
          - preceded by "else"            -> an else-if link in the chain
            -> head.
          - preceded by nothing at all    -> the very first rung in the
            file -> head.
          - preceded by anything else (a "{" that just opened a handler
            body, a statement's trailing ";", an assignment's "(", ...)
            -> this "if" is INSIDE a rung's body, not a sibling of it ->
            not a head. This is the "operatorname " shape at
            command_functions.cpp:2587/:2589: line 2587 is the chain rung
            (`"setname " || "operatorname "`); line 2589 re-tests
            "operatorname " one level deeper, already inside that rung's
            `{`, purely to tell which of the two OR'd names matched. Same
            shape for "bmx off", "aprscomment ", "wifi on",
            "persistflash on", "persistsd on" and "immediatesave on".
      * preceded by neither "||" nor "if(" (e.g. `bool on =
        (commandCheck(...) == 0);`) -> a plain expression, not a
        condition of its own -> not a head.

    A rung that is never a head anywhere is never a chain entry (it is
    ALWAYS someone else's inner re-test), so it can never shadow anything
    and is excluded from duplicate detection entirely -- that is the
    legitimate-repeat carve-out the module docstring and find_violations()
    describe, made concrete.
    """
    p1 = start
    while p1 > 0 and blanked[p1 - 1].isspace():
        p1 -= 1
    prefix = blanked[:p1]
    if prefix.endswith("||"):
        return True
    if not prefix.endswith("if("):
        return False
    before_if = prefix[:prefix.rfind("if")]
    k = len(before_if)
    while k > 0 and before_if[k - 1].isspace():
        k -= 1
    seg = before_if[:k]
    if seg == "":
        return True
    if seg.endswith("else"):
        before_else = seg[:-4]
        if not before_else or not (before_else[-1].isalnum() or before_else[-1] == "_"):
            return True
    return False


def guard_stack_at_line(text: str) -> Dict[int, Tuple[str, ...]]:
    """For every line, the tuple of currently-open #if/#ifdef/#ifndef
    guard conditions (as raw, stripped text -- e.g. "defined(ESP32)"),
    outermost first. #elif/#else do not change the stack (a later branch
    of the same #if is still that same #if for our purposes -- we only
    ever check membership in ARCH_GUARDS, never which branch), #endif pops
    the innermost still-open guard. Used only to recognise the
    NRF52_SERIES/ESP32 split in find_duplicates(); not a real preprocessor
    (unbalanced #if/#endif in the source -- there is none -- would just
    under- or over-pop silently).
    """
    stack: List[str] = []
    result: Dict[int, Tuple[str, ...]] = {}
    for i, line in enumerate(text.split("\n"), start=1):
        s = line.strip()
        if re.match(r"^#\s*(if|ifdef|ifndef)\b", s):
            stack.append(re.sub(r"^#\s*(if|ifdef|ifndef)\s*", "", s).strip())
        elif re.match(r"^#\s*endif\b", s):
            if stack:
                stack.pop()
        result[i] = tuple(stack)
    return result


def extract_rungs(text: str) -> List[Rung]:
    """Every commandCheck() rung, in the order it appears in the file.

    Commented-out rungs are skipped -- see blank_comments().
    """
    out = []
    blanked = blank_comments(text)
    for m in RUNG.finditer(blanked):
        line = text.count("\n", 0, m.start()) + 1
        out.append(Rung(name=m.group(1), line=line,
                         is_head=is_rung_head(blanked, m.start())))
    return out


def find_violations(rungs: List[Rung]) -> Tuple[List[Violation], int]:
    """Compare every pair of DISTINCT names for a prefix relationship.

    A name can legitimately appear more than once (e.g. the same literal
    reused inside a handler body to tell "on" from "off" after an `||`
    rung, or once per mutually exclusive build architecture -- see
    is_rung_head() and find_duplicates() for exactly which repeats are
    legitimate and why) -- that is not a ladder-order question, so only
    the first occurrence of each name is what "where does this rung sit in
    the ladder" means. Comparing first-occurrence lines catches the case
    that matters: whichever rung comes first in the file is the one that
    will actually run when the mesh sends a matching command.

    An exactly duplicated name is a DIFFERENT failure mode from a prefix
    collision (same name shadowing itself outright, not a shorter name
    swallowing a longer one) and is reported separately by
    find_duplicates(); this function's `a == b: continue` below is exactly
    what keeps the two counts from overlapping.

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


def _arch_exempt(g1: Tuple[str, ...], g2: Tuple[str, ...]) -> bool:
    """True when g1 and g2 (each a rung's currently-open #if guard stack,
    from guard_stack_at_line()) prove the two rungs can never both be
    compiled into the same build -- see ARCH_GUARDS."""
    found1 = set(g1) & ARCH_GUARDS
    found2 = set(g2) & ARCH_GUARDS
    return len(found1) == 1 and len(found2) == 1 and found1 != found2


def find_duplicates(rungs: List[Rung],
                     guard_by_line: Dict[int, Tuple[str, ...]]) -> List[Duplicate]:
    """Exactly-duplicated rung NAMES -- not prefix collisions, the same
    name tested twice.

    Only rungs that are themselves a link in the if/else-if chain count
    (Rung.is_head, see is_rung_head()); a name re-tested inside a rung's
    own handler body to disambiguate an OR'd condition is not a second
    chain entry and is never compared here at all -- it cannot shadow
    anything, since it only runs after its own rung already matched.

    Among same-name chain entries, a pair is exempt (not reported) only
    when ARCH_GUARDS proves they compile into mutually exclusive builds
    (see _arch_exempt()) -- the "udplog on"/"udplog off" shape at
    command_functions.cpp:4807 (inside `#if defined(NRF52_SERIES)`) and
    :4831 (inside `#if defined(ESP32)`). Every other same-name pair of
    chain entries -- in particular two with no enclosing #if at all, e.g.
    "setowndns " at command_functions.cpp:4001 and :4069 -- is reported:
    nothing here proves they cannot both compile into the same binary, so
    whichever is tested first wins and the second is dead code.

    Returns one Duplicate per non-exempt pair of same-name chain entries.
    """
    head_lines: Dict[str, List[int]] = {}
    for r in rungs:
        if r.is_head:
            head_lines.setdefault(r.name, []).append(r.line)

    duplicates: List[Duplicate] = []
    for name, lines in head_lines.items():
        lines = sorted(lines)
        for i, a in enumerate(lines):
            for b in lines[i + 1:]:
                if _arch_exempt(guard_by_line.get(a, ()), guard_by_line.get(b, ())):
                    continue
                duplicates.append(Duplicate(name=name, winning_line=a, dead_line=b))
    return duplicates


def analyze(text: str) -> Tuple[List[Rung], List[Violation], int, List[Duplicate]]:
    rungs = extract_rungs(text)
    violations, correct = find_violations(rungs)
    duplicates = find_duplicates(rungs, guard_stack_at_line(text))
    return rungs, violations, correct, duplicates


def check(path: Optional[Path] = None
          ) -> Tuple[List[Rung], List[Violation], int, List[Duplicate], List[str]]:
    """Run the analysis against a real file. Returns (rungs, violations,
    correct_count, duplicates, fatal_problems) -- fatal_problems is
    non-empty only when the extraction pattern found nothing, which must
    never look like a clean pass."""
    p = path if path is not None else (REPO / SOURCE)
    text = p.read_text()
    rungs, violations, correct, duplicates = analyze(text)
    fatal = []
    if not rungs:
        fatal.append(
            f"no commandCheck(...) rungs found in {p} -- the RUNG pattern "
            f"stopped matching the source, this check is now void")
    return rungs, violations, correct, duplicates, fatal


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
        rungs, violations, correct, duplicates = analyze(text)
        fatal = not rungs
        if want == "fatal" and fatal:
            good = True
            detail = "no rungs found"
        elif want == "fatal" and violations:
            good = True
            detail = f"{len(violations)} violation(s): " + "; ".join(
                v.message("test.cpp") for v in violations)
        elif want == "clean":
            good = (not fatal) and (not violations) and (not duplicates)
            detail = (f"{len(rungs)} rung(s), {correct} correctly-ordered "
                      f"pair(s), {len(duplicates)} duplicate(s)")
        else:
            good = False
            detail = "expected fatal but got neither no-rungs nor violations"
        print(f"  {'ok ' if good else 'FAIL'} {name}: {detail}")
        ok = ok and good

    # the reversed case must name BOTH line numbers, or a fix could not be
    # located from the output alone
    _, violations, _, _ = analyze(
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

    # -- exact-duplicate rung name cases --------------------------------
    dup_cases = [
        ("exact duplicate rung name", (
            '    if(commandCheck(msg_text+2, (char*)"setowndns ") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"setowndns ") == 0) { return; }\n'
        ), "duplicate"),
        # A duplicate inside a block comment must stay clean -- same rule
        # blank_comments() already applies to the prefix-pair check: dead
        # rungs are not part of the ladder.
        ("exact duplicate where the second copy is commented out", (
            '    if(commandCheck(msg_text+2, (char*)"setowndns ") == 0) { return; }\n'
            '    /*\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"setowndns ") == 0) { return; }\n'
            '    */\n'
        ), "clean"),
        # Legitimate repeat #1: the literal re-tested inside its own OR'd
        # rung's handler body, to tell which of the OR'd names matched --
        # the "operatorname " / "bmx off" shape.
        ("legitimate repeat: literal re-tested inside its own handler body", (
            '    if(commandCheck(msg_text+2, (char*)"bmx off") == 0 || '
            'commandCheck(msg_text+2, (char*)"bme off") == 0)\n'
            '    {\n'
            '        if(commandCheck(msg_text+2, (char*)"bmx off") == 0)\n'
            '        {\n'
            '            return;\n'
            '        }\n'
            '    }\n'
        ), "clean"),
        # Legitimate repeat #2: same rung name, once per mutually exclusive
        # build architecture -- the "udplog on"/"udplog off" shape. Each
        # copy is itself a chain link (reached via "else", like ethstat/
        # udpstat before it in the real file), so this exercises
        # _arch_exempt() rather than the is_head nesting check above.
        ("legitimate repeat: same name in two mutually exclusive #if architectures", (
            '    #if defined(NRF52_SERIES)\n'
            '    if(commandCheck(msg_text+2, (char*)"ethstat") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"udplog on") == 0) { return; }\n'
            '    #endif\n'
            '    #if defined(ESP32)\n'
            '    if(commandCheck(msg_text+2, (char*)"udpstat") == 0) { return; }\n'
            '    else\n'
            '    if(commandCheck(msg_text+2, (char*)"udplog on") == 0) { return; }\n'
            '    #endif\n'
        ), "clean"),
    ]

    for name, text, want in dup_cases:
        rungs, violations, correct, duplicates = analyze(text)
        fatal = not rungs
        if want == "duplicate":
            good = (not fatal) and (not violations) and len(duplicates) == 1
            detail = f"{len(duplicates)} duplicate(s): " + "; ".join(
                d.message("test.cpp") for d in duplicates)
        elif want == "clean":
            good = (not fatal) and (not violations) and (not duplicates)
            detail = f"{len(rungs)} rung(s), {len(duplicates)} duplicate(s)"
        else:
            good = False
            detail = "bad case"
        print(f"  {'ok ' if good else 'FAIL'} {name}: {detail}")
        ok = ok and good

    # the duplicate finding must name BOTH line numbers
    _, _, _, duplicates2 = analyze(
        '    if(commandCheck(msg_text+2, (char*)"setowndns ") == 0) { return; }\n'
        '    else\n'
        '    if(commandCheck(msg_text+2, (char*)"setowndns ") == 0) { return; }\n')
    dup_named_both_lines = (
        len(duplicates2) == 1
        and duplicates2[0].winning_line == 1
        and duplicates2[0].dead_line == 3)
    print(f"  {'ok ' if dup_named_both_lines else 'FAIL'} "
          f"duplicate names both rung line numbers: {duplicates2}")
    ok = ok and dup_named_both_lines

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    rungs, violations, correct, duplicates, fatal = check()
    for f in fatal:
        print(f"FATAL: {f}")
    if fatal:
        return 1

    names = {r.name for r in rungs}
    print(f"{len(rungs)} rung(s), {len(names)} distinct name(s), "
          f"{len(violations) + correct} prefix pair(s) found "
          f"({correct} correctly ordered, {len(violations)} violation(s)), "
          f"{len(duplicates)} exact-duplicate rung(s) found")

    for v in violations:
        print(f"VIOLATION {v.message(SOURCE)}")
    for d in duplicates:
        print(f"VIOLATION {d.message(SOURCE)}")

    if violations or duplicates:
        print(f"\n{len(violations)} prefix violation(s), "
              f"{len(duplicates)} duplicate-rung violation(s)")
        return 1

    print("command ladder: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
