#!/usr/bin/env python3
"""Parity gate between src/command_functions.cpp's --help output and its
real command set (the commandCheck() ladder plus the COMMAND_TOGGLES[]
table).

  python3 test/golden/help_parity_lint.py
  python3 test/golden/help_parity_lint.py --self-test

Exit 0 when every check below is clean, 1 on any finding or if extraction
finds nothing (which must never look like a clean pass).

Five checks:

  A. Coverage        -- every command keyword defined outside an
                        INSTRUMENT_ENABLED region must be advertised
                        somewhere in --help (aliases count anywhere in the
                        text; see "(alias --wx)").
  B. Guard parity     -- a --help mention must not be LESS guarded than the
                        command it advertises, or it over-advertises: shown
                        on boards that do not have the command.
  C. Reverse coverage -- every "--kw" mentioned in --help (outside an
                        INSTRUMENT_ENABLED help line) must have a real
                        definition; catches a typo or a removed command
                        left behind in the help text.
  D. Help visibility  -- the mirror image of B: every non-instrument
                        definition needs at least one --help mention that
                        is not MORE guarded than the command, or it
                        under-advertises: the command silently vanishes
                        from --help on some boards where it still compiles.
  E. Output hazards   -- a printfdeb() literal in the help block must not
                        contain ';' (printfdeb turns ';' into ' ' outside
                        --debug csv), and a printlndeb()/printdeb() literal
                        must not contain '%%' (printed literally, no
                        printf pass).

B and D are two different failure directions, not one check run twice: B
misses nothing when a command has no definition at all under a guard
weaker than required (over-advertising is only ever a problem where a
mention exists), D misses nothing when a definition's mentions are ALL
guarded no more loosely than it is (never mind that B may also have
nothing to say there, e.g. two architecture-exclusive definitions of the
same name with no guard in common -- B's per-key intersection then imposes
no requirement at all, but D still checks each definition against its OWN
mentions independently). Keeping both is what closed the silent-pass holes
below.

B, D and (mid-line) C are keyed by FULL command text, not by the leading
keyword alone -- grouping "button gpio " (guarded #ifndef BOARD_T_DECK_PRO)
together with "button on"/"button off" (unguarded) under the single
keyword "button" is exactly the bug: since not every button-* definition
is guarded, the old aggregate imposed NO requirement at all, and an
unguarded "--button gpio 99" help line passed silently. Splitting
"button gpio" out as its own group is what makes it fail: mutation-proven
silent passes fixed this way (2026-09-26) --

  * --button gpio 99 (handler #ifndef BOARD_T_DECK_PRO) diluted by the
    unguarded --button on/off sibling.
  * --gps reset (same guard) diluted by the unguarded --gps on/off pair.
  * --390 on (handler #if defined(ENABLE_BMP390)) diluted by the unguarded
    --390 off toggle row; same shape for --680 on / --811 on
    (#if defined(ENABLE_BMX280)).
  * a mid-line reference such as "(see --kiss)" was invisible to guard
    parity entirely -- only a help line's OWN leading command was checked,
    so deleting the guarded --kiss lines and leaving a bare, unguarded
    "(see --kiss)" behind passed clean. Every "--kw" occurrence in a
    segment is now scanned, not just the leading one; an alias mention
    like "(alias --wx)" still passes when the alias handler is unguarded,
    since an unguarded definition carries no requirement to check against.

A segment's leading command is matched at increasing specificity: first
token, then first+second token when the second is a word (not a number or
placeholder) -- expanding an "a/b" alternative like "on/off" into both
"kw a" and "kw b" -- and only falling back to the bare keyword when no
longer candidate is a definition that actually exists. See
Mention/scan_mentions() and full_command_key().
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE = "src/command_functions.cpp"

WORD_BOUNDARY_CHARS = "A-Za-z0-9-"
INSTRUMENT_GUARD = "INSTRUMENT_ENABLED"

# Item 6: the (char*) cast is not part of what makes this a rung -- some
# call sites (and every synthetic self-test fixture) may omit it.
CMDCHECK_RE = re.compile(
    r'commandCheck\s*\(\s*msg_text\s*\+\s*2\s*,\s*(?:\(char\*\)\s*)?"([^"\\]*)"\s*\)'
)
HELP_CHECK_RE = re.compile(
    r'commandCheck\s*\(\s*msg_text\s*\+\s*2\s*,\s*(?:\(char\*\)\s*)?"help"\s*\)'
)
TOGGLES_ARRAY_RE = re.compile(r"COMMAND_TOGGLES\s*\[\s*\]\s*=\s*")
TOGGLE_ROW_RE = re.compile(r'\{\s*"(--[^"\\]*)"')
PRINT_CALL_RE = re.compile(r"\b(printfdeb|printlndeb|printdeb)\s*\(")
DIRECTIVE_RE = re.compile(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)")


# ---------------------------------------------------------------------------
# Findings
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Finding:
    line: int
    message: str


# ---------------------------------------------------------------------------
# Comment / string-literal aware scanning
# ---------------------------------------------------------------------------


def strip_comments(text: str) -> tuple[str, list[tuple[int, int, str]]]:
    """Blank out // and /* */ comments (newlines kept), leaving code and
    string/char literals verbatim. Returns (masked_text, string_spans) where
    string_spans is [(start, end_exclusive, raw_content), ...] for every
    double-quoted literal that was not inside a comment, in source order.
    """
    n = len(text)
    out = list(text)
    spans: list[tuple[int, int, str]] = []
    i = 0
    state = "code"
    str_start = 0
    while i < n:
        c = text[i]
        if state == "code":
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                out[i] = " "
                out[i + 1] = " "
                state = "line_comment"
                i += 2
                continue
            if c == "/" and i + 1 < n and text[i + 1] == "*":
                out[i] = " "
                out[i + 1] = " "
                state = "block_comment"
                i += 2
                continue
            if c == '"':
                state = "string"
                str_start = i
                i += 1
                continue
            if c == "'":
                state = "char"
                i += 1
                continue
            i += 1
            continue
        if state == "line_comment":
            if c == "\n":
                state = "code"
            else:
                out[i] = " "
            i += 1
            continue
        if state == "block_comment":
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                out[i] = " "
                out[i + 1] = " "
                state = "code"
                i += 2
                continue
            if c != "\n":
                out[i] = " "
            i += 1
            continue
        if state == "string":
            if c == "\\" and i + 1 < n:
                i += 2
                continue
            if c == '"':
                spans.append((str_start, i + 1, text[str_start + 1 : i]))
                state = "code"
            i += 1
            continue
        if state == "char":
            if c == "\\" and i + 1 < n:
                i += 2
                continue
            if c == "'":
                state = "code"
            i += 1
            continue
    return "".join(out), spans


def match_delim(masked: str, open_idx: int, open_ch: str, close_ch: str) -> int:
    """masked[open_idx] must be open_ch. Returns the index of the matching
    close_ch (string/char literal aware), or -1 if unbalanced."""
    n = len(masked)
    assert masked[open_idx] == open_ch
    depth = 1
    i = open_idx + 1
    state = "code"
    while i < n:
        c = masked[i]
        if state == "code":
            if c == '"':
                state = "string"
            elif c == "'":
                state = "char"
            elif c == open_ch:
                depth += 1
            elif c == close_ch:
                depth -= 1
                if depth == 0:
                    return i
            i += 1
            continue
        if state == "string":
            if c == "\\" and i + 1 < n:
                i += 2
                continue
            if c == '"':
                state = "code"
            i += 1
            continue
        if state == "char":
            if c == "\\" and i + 1 < n:
                i += 2
                continue
            if c == "'":
                state = "code"
            i += 1
            continue
    return -1


def decode_c_string(raw: str) -> str:
    """Interpret C backslash escapes (not printf %-directives)."""
    escapes = {
        "n": "\n",
        "t": "\t",
        "r": "\r",
        "\\": "\\",
        '"': '"',
        "'": "'",
        "0": "\0",
        "a": "\a",
        "b": "\b",
        "f": "\f",
        "v": "\v",
    }
    out: list[str] = []
    i = 0
    n = len(raw)
    while i < n:
        c = raw[i]
        if c == "\\" and i + 1 < n:
            nc = raw[i + 1]
            out.append(escapes.get(nc, nc))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_leading_string_literal(args_text: str) -> str | None:
    """Parses the first (possibly adjacent-concatenated) string literal at
    the start of a call's argument list. Returns the decoded content, or
    None if the first argument is not a string literal."""
    i = 0
    n = len(args_text)
    parts: list[str] = []
    found = False
    while True:
        while i < n and args_text[i] in " \t\r\n":
            i += 1
        if i < n and args_text[i] == '"':
            found = True
            j = i + 1
            buf: list[str] = []
            while j < n and args_text[j] != '"':
                if args_text[j] == "\\" and j + 1 < n:
                    buf.append(args_text[j])
                    buf.append(args_text[j + 1])
                    j += 2
                    continue
                buf.append(args_text[j])
                j += 1
            parts.append("".join(buf))
            i = j + 1
            continue
        break
    if not found:
        return None
    return decode_c_string("".join(parts))


# ---------------------------------------------------------------------------
# Preprocessor guard-stack tracking
# ---------------------------------------------------------------------------

_DEFINED_PAREN_RE = re.compile(r"\bdefined\s*\(\s*([A-Za-z_]\w*)\s*\)")
_DEFINED_BARE_RE = re.compile(r"\bdefined\s+([A-Za-z_]\w*)\b")


def normalize_condition(cond: str) -> str:
    cond = re.sub(r"\s+", " ", cond.strip())
    cond = _DEFINED_PAREN_RE.sub(r"defined(\1)", cond)
    cond = _DEFINED_BARE_RE.sub(r"defined(\1)", cond)
    cond = re.sub(r"\s+", " ", cond.strip())
    return cond


@dataclass
class _Frame:
    branch_conds: list[str] = field(default_factory=list)
    current: str = ""


def compute_guard_stack(masked: str) -> dict[int, frozenset[str]]:
    """Returns, for every physical (1-indexed) line of `masked`, the
    frozenset of normalized preprocessor conditions active on that line
    (i.e. the guard stack, collapsed to a set)."""
    lines = masked.split("\n")
    n = len(lines)
    guard_at_line: dict[int, frozenset[str]] = {}
    stack: list[_Frame] = []

    def current_set() -> frozenset[str]:
        return frozenset(f.current for f in stack if f.current)

    i = 0
    while i < n:
        raw_line = lines[i]
        stripped = raw_line.strip()
        if stripped.startswith("#"):
            j = i
            full = raw_line.rstrip()
            joined = [full]
            while joined[-1].endswith("\\") and j + 1 < n:
                joined[-1] = joined[-1][:-1]
                j += 1
                joined.append(lines[j].strip())
            full_directive = " ".join(part.strip() for part in joined).strip()

            snap = current_set()
            for line_no in range(i + 1, j + 2):
                guard_at_line[line_no] = snap

            m = DIRECTIVE_RE.match(full_directive)
            if m:
                kind = m.group(1)
                rest = m.group(2).strip()
                if kind == "if":
                    cond = normalize_condition(rest)
                    stack.append(_Frame([cond], cond))
                elif kind == "ifdef":
                    cond = f"defined({rest})"
                    stack.append(_Frame([cond], cond))
                elif kind == "ifndef":
                    cond = f"!defined({rest})"
                    stack.append(_Frame([cond], cond))
                elif kind == "elif":
                    if stack:
                        fr = stack[-1]
                        negs = " && ".join(f"!({c})" for c in fr.branch_conds)
                        newcond = normalize_condition(rest)
                        fr.current = f"{negs} && ({newcond})" if negs else newcond
                        fr.branch_conds.append(newcond)
                elif kind == "else":
                    if stack:
                        fr = stack[-1]
                        fr.current = " && ".join(f"!({c})" for c in fr.branch_conds)
                elif kind == "endif":
                    if stack:
                        stack.pop()
            i = j + 1
            continue
        guard_at_line[i + 1] = current_set()
        i += 1
    return guard_at_line


def line_of(offset_to_line: list[int], idx: int) -> int:
    """offset_to_line[k] = char offset where line k+1 starts (0-indexed
    list, 1-indexed lines). Returns the 1-indexed line containing idx."""
    import bisect

    pos = bisect.bisect_right(offset_to_line, idx) - 1
    return pos + 1


def build_line_offsets(text: str) -> list[int]:
    offsets = [0]
    for i, c in enumerate(text):
        if c == "\n":
            offsets.append(i + 1)
    return offsets


# ---------------------------------------------------------------------------
# Command-set extraction
# ---------------------------------------------------------------------------

_TOKEN_RE = re.compile(rf"[{WORD_BOUNDARY_CHARS}]+")
_WORD_TOKEN_RE = re.compile(r"[A-Za-z]+(?:/[A-Za-z]+)*")


def extract_keyword(raw: str) -> str | None:
    s = raw.removeprefix("--")
    s = s.lstrip()
    m = _TOKEN_RE.match(s)
    if not m:
        return None
    return m.group(0).lower()


def full_command_key(raw: str) -> str:
    """The lookup key for a definition's specific sub-command: the first
    two whitespace-separated tokens (keyword + sub-word) when the second
    is a plain word, or just the keyword otherwise (a trailing argument
    marker like the space in "button gpio " never reaches here since
    split() drops it). Mirrors how a --help mention's leading tokens are
    read -- see Mention/scan_mentions()."""
    s = raw.removeprefix("--").strip()
    parts = s.split()
    if not parts:
        return ""
    kw = extract_keyword(parts[0])
    if kw is None:
        return ""
    if len(parts) == 1 or not _WORD_TOKEN_RE.fullmatch(parts[1]):
        return kw
    return f"{kw} {parts[1].lower()}"


@dataclass
class Definition:
    keyword: str
    full_key: str
    line: int
    guard: frozenset[str]


def find_definitions(masked: str, offsets: list[int]) -> list[tuple[str, str, int]]:
    """Every real command definition (commandCheck() rung or
    COMMAND_TOGGLES[] row) as (keyword, full_key, line)."""
    out: list[tuple[str, str, int]] = []

    for m in CMDCHECK_RE.finditer(masked):
        raw = m.group(1)
        kw = extract_keyword(raw)
        if kw is None:
            continue
        out.append((kw, full_command_key(raw) or kw, line_of(offsets, m.start())))

    m_arr = TOGGLES_ARRAY_RE.search(masked)
    if m_arr:
        open_idx = masked.find("{", m_arr.end())
        if open_idx != -1:
            close_idx = match_delim(masked, open_idx, "{", "}")
            if close_idx != -1:
                for rm in TOGGLE_ROW_RE.finditer(masked, open_idx, close_idx + 1):
                    raw = rm.group(1)
                    kw = extract_keyword(raw)
                    if kw is None:
                        continue
                    out.append(
                        (kw, full_command_key(raw) or kw, line_of(offsets, rm.start()))
                    )

    return out


def build_requirements(defs: list[Definition], key_fn) -> dict[str, frozenset[str]]:
    """A guard requirement for `key_fn(d)` exists only when EVERY
    definition sharing that key is guarded; it is then the intersection of
    their guards (the guards common to all of them). One unguarded sibling
    (or two guarded siblings with nothing in common) means no requirement
    at all under this key -- exactly the dilution this lint used to suffer
    from when the key was the bare keyword; see full_command_key()."""
    groups: dict[str, list[Definition]] = {}
    for d in defs:
        if d.keyword == "help":
            continue
        groups.setdefault(key_fn(d), []).append(d)
    reqs: dict[str, frozenset[str]] = {}
    for key, group in groups.items():
        if not group or not all(g.guard for g in group):
            continue
        common = frozenset.intersection(*(g.guard for g in group))
        if common:
            reqs[key] = common
    return reqs


# ---------------------------------------------------------------------------
# Help block extraction
# ---------------------------------------------------------------------------


@dataclass
class HelpCall:
    line: int
    func: str
    text: str
    guard: frozenset[str]


def find_help_block(masked: str) -> tuple[int, int] | None:
    m = HELP_CHECK_RE.search(masked)
    if not m:
        return None
    open_idx = masked.find("{", m.end())
    if open_idx == -1:
        return None
    close_idx = match_delim(masked, open_idx, "{", "}")
    if close_idx == -1:
        return None
    return (open_idx, close_idx + 1)


def find_help_calls(
    masked: str,
    block: tuple[int, int],
    offsets: list[int],
    guard_at_line: dict[int, frozenset[str]],
) -> list[HelpCall]:
    start, end = block
    calls: list[HelpCall] = []
    for m in PRINT_CALL_RE.finditer(masked, start, end):
        func = m.group(1)
        open_idx = m.end() - 1
        close_idx = match_delim(masked, open_idx, "(", ")")
        if close_idx == -1:
            continue
        args_text = masked[open_idx + 1 : close_idx]
        text = parse_leading_string_literal(args_text)
        if text is None:
            continue
        ln = line_of(offsets, m.start())
        calls.append(HelpCall(ln, func, text, guard_at_line.get(ln, frozenset())))
    return calls


# ---------------------------------------------------------------------------
# --help mentions: every "--kw" occurrence, leading or mid-line, plus the
# specific sub-command it names when its next token is a word (item 2, 5).
# ---------------------------------------------------------------------------

_MENTION_RE = re.compile(rf"--([{WORD_BOUNDARY_CHARS}]+)", re.IGNORECASE)
_SECOND_TOKEN_RE = re.compile(r"[ \t]+([A-Za-z]+(?:/[A-Za-z]+)*)")


@dataclass
class Mention:
    line: int
    guard: frozenset[str]
    keyword: str
    candidates: list[
        str
    ]  # "kw sub" full-command candidates, longest-first alternatives expanded


def scan_mentions(help_calls: list[HelpCall]) -> list[Mention]:
    mentions: list[Mention] = []
    for call in help_calls:
        for m in _MENTION_RE.finditer(call.text):
            kw = m.group(1).lower()
            sm = _SECOND_TOKEN_RE.match(call.text, m.end())
            candidates: list[str] = []
            if sm:
                for alt in sm.group(1).split("/"):
                    if alt:
                        candidates.append(f"{kw} {alt.lower()}")
            mentions.append(Mention(call.line, call.guard, kw, candidates))
    return mentions


# ---------------------------------------------------------------------------
# The five checks
# ---------------------------------------------------------------------------


def check_coverage(
    keyword_defs: dict[str, list[Definition]],
    help_text_all: str,
) -> list[Finding]:
    """A: every keyword defined outside INSTRUMENT_ENABLED is mentioned
    somewhere in --help (anywhere in the text -- an alias mention counts)."""
    findings: list[Finding] = []
    for kw, defs in sorted(keyword_defs.items()):
        if kw == "help":
            continue
        if not any(INSTRUMENT_GUARD not in d.guard for d in defs):
            continue
        pattern = re.compile(
            rf"--{re.escape(kw)}(?![{WORD_BOUNDARY_CHARS}])", re.IGNORECASE
        )
        if not pattern.search(help_text_all):
            first_line = min(d.line for d in defs)
            findings.append(
                Finding(
                    first_line,
                    f"--{kw}: command has a handler but is not advertised anywhere in --help output",
                )
            )
    return findings


def check_guard_parity(
    mentions: list[Mention],
    full_defs: dict[str, list[Definition]],
    full_reqs: dict[str, frozenset[str]],
    kw_reqs: dict[str, frozenset[str]],
) -> list[Finding]:
    """B: a --help mention (leading or mid-line) must not be less guarded
    than the specific sub-command it names -- resolved at "kw sub"
    granularity when that is a real definition, falling back to the bare
    keyword only when no such longer match exists (item 2, 5)."""
    findings: list[Finding] = []
    for men in mentions:
        if men.keyword == "help":
            continue
        resolved = False
        for cand in men.candidates:
            if cand not in full_defs:
                continue
            resolved = True
            req = full_reqs.get(cand)
            if req and not req <= men.guard:
                missing = sorted(req - men.guard)
                findings.append(
                    Finding(
                        men.line,
                        f"--{cand}: help mention guard {sorted(men.guard) or []} does not "
                        f"cover handler guard(s) {missing}",
                    )
                )
        if resolved:
            continue
        req = kw_reqs.get(men.keyword)
        if req and not req <= men.guard:
            missing = sorted(req - men.guard)
            findings.append(
                Finding(
                    men.line,
                    f"--{men.keyword}: help mention guard {sorted(men.guard) or []} does not "
                    f"cover handler guard(s) {missing}",
                )
            )
    return findings


def check_reverse_coverage(
    mentions: list[Mention],
    keyword_defs: dict[str, list[Definition]],
) -> list[Finding]:
    """C: every "--kw" mentioned in --help (outside an INSTRUMENT_ENABLED
    help line) must have a real definition somewhere."""
    findings: list[Finding] = []
    seen: set[tuple[int, str]] = set()
    for men in mentions:
        if men.keyword == "help" or men.keyword in keyword_defs:
            continue
        if INSTRUMENT_GUARD in men.guard:
            continue
        key = (men.line, men.keyword)
        if key in seen:
            continue
        seen.add(key)
        findings.append(
            Finding(
                men.line,
                f"--{men.keyword}: mentioned in --help output but has no commandCheck/toggle "
                f"definition anywhere in this file",
            )
        )
    return findings


def _condition_implied_by(cond: str, def_guard: frozenset[str]) -> bool:
    """True when `cond` (one element of a mention's guard set) is
    guaranteed true whenever every condition in `def_guard` is true. An
    exact match always qualifies; a top-level "||" disjunction (the real
    "defined(ESP32) || defined(NRF52_SERIES)" shape guarding --udplog's
    only mention, against each architecture's OWN single-condition
    definition) also qualifies as soon as ONE of its disjuncts is itself
    implied -- if the definition already requires NRF52_SERIES, "ESP32 ||
    NRF52_SERIES" is true for free. This is a syntactic, single-level
    split (no nested parens occur in this file's guards), the same
    narrowness command_ladder_lint.py's ARCH_GUARDS carve-out accepts for
    the identical split."""
    if cond in def_guard:
        return True
    parts = [p.strip() for p in cond.split("||")]
    return len(parts) > 1 and any(p in def_guard for p in parts)


def _guard_implied_by(mention_guard: frozenset[str], def_guard: frozenset[str]) -> bool:
    return all(_condition_implied_by(c, def_guard) for c in mention_guard)


def check_help_visibility(
    all_defs: list[Definition],
    mentions: list[Mention],
) -> list[Finding]:
    """D: the mirror of B. Every non-instrument definition needs at least
    one --help mention whose guard is not MORE restrictive than the
    definition's own -- otherwise the command silently vanishes from
    --help on some board where it still compiles. Checked per definition
    (not per aggregate key), so an architecture-exclusive pair with no
    guard in common still gets each half checked against its own guard."""
    by_full: dict[str, list[Mention]] = {}
    by_kw: dict[str, list[Mention]] = {}
    for men in mentions:
        for cand in men.candidates:
            by_full.setdefault(cand, []).append(men)
        by_kw.setdefault(men.keyword, []).append(men)

    findings: list[Finding] = []
    for d in all_defs:
        if d.keyword == "help" or INSTRUMENT_GUARD in d.guard:
            continue
        pool = by_full.get(d.full_key) or by_kw.get(d.keyword) or []
        if not pool:
            continue  # never mentioned at all -- check A's job, not this one
        if not any(_guard_implied_by(m.guard, d.guard) for m in pool):
            findings.append(
                Finding(
                    d.line,
                    f"--{d.full_key}: defined under guard {sorted(d.guard) or []} but every "
                    f"--help mention of it is guarded more strictly -- it would vanish from "
                    f"--help on some board where the command still compiles",
                )
            )
    return findings


def check_hazards(help_calls: list[HelpCall]) -> list[Finding]:
    """E: printfdeb() literals must not contain ';'; printlndeb()/
    printdeb() literals must not contain '%%'."""
    findings: list[Finding] = []
    for call in help_calls:
        snippet = call.text.replace("\n", "\\n")
        if len(snippet) > 80:
            snippet = snippet[:77] + "..."
        if call.func == "printfdeb" and ";" in call.text:
            findings.append(
                Finding(
                    call.line,
                    f"printfdeb literal contains ';' (printed as a space outside "
                    f'--debug csv): "{snippet}"',
                )
            )
        elif call.func in ("printlndeb", "printdeb") and "%%" in call.text:
            findings.append(
                Finding(
                    call.line,
                    f"{call.func} literal contains '%%' (printed literally, no printf "
                    f'pass): "{snippet}"',
                )
            )
    return findings


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def analyze(text: str) -> list[Finding]:
    masked, _string_spans = strip_comments(text)
    offsets = build_line_offsets(masked)
    guard_at_line = compute_guard_stack(masked)

    raw_defs = find_definitions(masked, offsets)
    if not raw_defs:
        return [
            Finding(
                1,
                "no commandCheck(...)/COMMAND_TOGGLES definitions found -- the extraction "
                "pattern stopped matching this source, this check is now void",
            )
        ]
    all_defs = [
        Definition(kw, fk, ln, guard_at_line.get(ln, frozenset()))
        for (kw, fk, ln) in raw_defs
    ]

    keyword_defs: dict[str, list[Definition]] = {}
    full_defs: dict[str, list[Definition]] = {}
    for d in all_defs:
        keyword_defs.setdefault(d.keyword, []).append(d)
        full_defs.setdefault(d.full_key, []).append(d)

    full_reqs = build_requirements(all_defs, lambda d: d.full_key)
    kw_reqs = build_requirements(all_defs, lambda d: d.keyword)

    block = find_help_block(masked)
    if block is None:
        return [
            Finding(
                1,
                'could not locate the --help block (commandCheck(...,"help") not found)',
            )
        ]

    help_calls = find_help_calls(masked, block, offsets, guard_at_line)
    help_text_all = "\n".join(c.text for c in help_calls)
    mentions = scan_mentions(help_calls)

    findings: list[Finding] = []
    findings += check_coverage(keyword_defs, help_text_all)
    findings += check_guard_parity(mentions, full_defs, full_reqs, kw_reqs)
    findings += check_reverse_coverage(mentions, keyword_defs)
    findings += check_help_visibility(all_defs, mentions)
    findings += check_hazards(help_calls)
    findings.sort(key=lambda f: (f.line, f.message))
    return findings


# kept as a thin, testable alias -- some callers/tests read more naturally
# asking for "the findings" than "the analysis".
def lint_source(text: str) -> list[Finding]:
    return analyze(text)


def check(path: Path | None = None) -> tuple[list[Finding], Path]:
    p = path if path is not None else (REPO / SOURCE)
    text = p.read_text(encoding="utf-8", errors="replace")
    return analyze(text), p


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------


def _has(findings: list[Finding], *needles: str) -> bool:
    return any(all(n in f.message for n in needles) for f in findings)


def self_test() -> int:
    ok = True

    def case(name: str, good: bool, detail: str) -> None:
        nonlocal ok
        print(f"  {'ok ' if good else 'FAIL'} {name}: {detail}")
        ok = ok and good

    # -- A: coverage --------------------------------------------------------

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"foo") == 0) { doFoo(); }\n'
        "    else\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--bar  something\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "A: missing command is a finding",
        _has(findings, "--foo", "not advertised"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"foo") == 0) { doFoo(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--foo  does the thing\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "A: covered command is not a finding",
        not _has(findings, "--foo"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"weather") == 0 || '
        'commandCheck(msg_text+2, (char*)"wx") == 0) { doWeather(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--weather  temp/hum/press (alias --wx)\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "A: alias mentioned anywhere counts as coverage", findings == [], str(findings)
    )

    findings = analyze(
        "static const ToggleRow COMMAND_TOGGLES[] =\n"
        "{\n"
        '    { "--relay on",  &bRelay, nullptr, 0xFFFFFFFF, 0x0000, nullptr, TG_DIRTY_NONE, TG_FLAG_TRUE },\n'
        '    { "--relay off", &bRelay, nullptr, 0xFFFFFFFF, 0x0000, nullptr, TG_DIRTY_NONE, 0 },\n'
        "};\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--other  something\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "A: toggle-table row missing from --help is a finding",
        _has(findings, "--relay", "not advertised"),
        str(findings),
    )

    findings = analyze(
        "    #if INSTRUMENT_ENABLED\n"
        '    if(commandCheck(msg_text+2, (char*)"bench") == 0) { doBench(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--other  something\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "A: INSTRUMENT_ENABLED-only command is exempt",
        not _has(findings, "--bench"),
        str(findings),
    )

    findings = analyze(
        "    #if INSTRUMENT_ENABLED\n"
        '    if(commandCheck(msg_text+2, (char*)"dual") == 0) { doDualBench(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"dual") == 0) { doDualReal(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--other  something\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "A: one instrument + one real definition still needs coverage",
        _has(findings, "--dual", "not advertised"),
        str(findings),
    )

    # -- B: guard parity, at keyword granularity (unchanged shape) ----------

    findings = analyze(
        "    #ifdef BOARD_X\n"
        '    if(commandCheck(msg_text+2, (char*)"bar") == 0) { doBar(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--bar  something\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "B: guarded handler + unguarded help line is a finding",
        _has(findings, "--bar", "guard"),
        str(findings),
    )

    findings = analyze(
        "    #ifdef BOARD_X\n"
        '    if(commandCheck(msg_text+2, (char*)"bar") == 0) { doBar(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        "        #ifdef BOARD_X\n"
        '        printlndeb("--bar  something\\n");\n'
        "        #endif\n"
        "        return;\n"
        "    }\n"
    )
    case("B: matching guards pass", findings == [], str(findings))

    # -- B + item 2: sub-command granularity ---------------------------------

    # --button gpio 99 (specific, guarded) diluted by unguarded --button on/off.
    findings = analyze(
        "    #ifndef BOARD_X\n"
        '    if(commandCheck(msg_text+2, (char*)"button gpio ") == 0) { doGpio(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"button on") == 0) { doOn(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"button off") == 0) { doOff(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--button gpio 99  pin\\n--button on/off  check\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "B item2: --button gpio guard not diluted by unguarded on/off siblings",
        _has(findings, "--button gpio", "guard") and not _has(findings, "--button on"),
        str(findings),
    )

    # --gps reset (specific, guarded) diluted by unguarded --gps on/off.
    findings = analyze(
        "    #ifndef BOARD_X\n"
        '    if(commandCheck(msg_text+2, (char*)"gps reset") == 0) { doReset(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"gps on") == 0) { doOn(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"gps off") == 0) { doOff(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--gps on/off  chip\\n--gps reset  factory reset\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "B item2: --gps reset guard not diluted by unguarded on/off siblings",
        _has(findings, "--gps reset", "guard") and not _has(findings, "--gps on"),
        str(findings),
    )

    # --390 on (guarded) diluted by the unguarded --390 off toggle row; same
    # shape named for --680/--811.
    findings = analyze(
        "    #if defined(ENABLE_BMP390)\n"
        '    if(commandCheck(msg_text+2, (char*)"390 on") == 0) { doOn(); }\n'
        "    #endif\n"
        "static const ToggleRow COMMAND_TOGGLES[] =\n"
        "{\n"
        '    { "--390 off", &bBMP3ON, nullptr, 0xFFFFFFFF, 0x0000, nullptr, TG_DIRTY_NONE, 0 },\n'
        '    { "--680 off", &bBME680ON, nullptr, 0xFFFFFFFF, 0x0000, nullptr, TG_DIRTY_NONE, 0 },\n'
        '    { "--811 off", &bMCU811ON, nullptr, 0xFFFFFFFF, 0x0000, nullptr, TG_DIRTY_NONE, 0 },\n'
        "};\n"
        "    #if defined(ENABLE_BMX280)\n"
        '    if(commandCheck(msg_text+2, (char*)"680 on") == 0) { doOn(); }\n'
        "    else\n"
        '    if(commandCheck(msg_text+2, (char*)"811 on") == 0) { doOn(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--390 on/off  BMP390\\n--680 on/off  BME680\\n--811 on/off  CCS811\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "B item2: --390 on guard not diluted by unguarded --390 off toggle",
        _has(findings, "--390 on", "guard") and not _has(findings, "--390 off"),
        str(findings),
    )
    case(
        "B item2: same shape for --680 on / --811 on",
        _has(findings, "--680 on", "guard") and _has(findings, "--811 on", "guard"),
        str(findings),
    )

    # -- D + item 4: help guard stricter than handler ------------------------

    # A mention MORE guarded than the handler is not caught by B (B only
    # requires the mention be at least as guarded), but it means the
    # command vanishes from --help wherever BOARD_X holds without
    # EXTRA_FLAG. Retired/renamed from the old "wider guard covers and
    # passes" case: it still passes B, but now fails D.
    src_narrower_help = (
        "    #ifdef BOARD_X\n"
        '    if(commandCheck(msg_text+2, (char*)"bar") == 0) { doBar(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        "        #ifdef BOARD_X\n"
        "        #ifdef EXTRA_FLAG\n"
        '        printlndeb("--bar  something\\n");\n'
        "        #endif\n"
        "        #endif\n"
        "        return;\n"
        "    }\n"
    )
    findings = analyze(src_narrower_help)
    case(
        "D item4: help guard narrower (stricter) than the handler's is a finding",
        not _has(findings, "--bar", "does not cover")  # B stays clean
        and _has(findings, "--bar", "vanish"),  # D fires
        str(findings),
    )

    # The persiststat-shaped probe: an unguarded command whose only mention
    # is also unguarded is clean; moving the DEFINITION under a board guard
    # while the mention stays unguarded must give a finding somewhere (here,
    # from B: the mention is now under-guarded relative to the handler).
    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"persiststat") == 0) { doStat(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printdeb("--persiststat  NVS-only values\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "D probe baseline: unguarded def + unguarded mention is clean",
        findings == [],
        str(findings),
    )
    findings = analyze(
        "    #if defined(BOARD_T_DECK)\n"
        '    if(commandCheck(msg_text+2, (char*)"persiststat") == 0) { doStat(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printdeb("--persiststat  NVS-only values\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "D probe: --persiststat moved under BOARD_T_DECK gives a finding",
        _has(findings, "persiststat"),
        str(findings),
    )

    # D's own reason to exist: two architecture-exclusive definitions with
    # NOTHING in common (so B's per-key intersection imposes no requirement
    # at all) must still each be checked against their OWN mention. One
    # mention is wrongly guarded here -- D must catch it even though B has
    # nothing to say.
    findings = analyze(
        "    #ifdef BOARD_A\n"
        '    if(commandCheck(msg_text+2, (char*)"widget on") == 0) { doA(); }\n'
        "    #endif\n"
        "    #ifdef BOARD_B\n"
        '    if(commandCheck(msg_text+2, (char*)"widget on") == 0) { doB(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        "        #ifdef BOARD_A\n"
        '        printlndeb("--widget on/off  A form\\n");\n'
        "        #endif\n"
        "        #ifdef WRONG_FLAG\n"
        '        printlndeb("--widget on/off  B form\\n");\n'
        "        #endif\n"
        "        return;\n"
        "    }\n"
    )
    case(
        "D: catches a mis-guarded mention that B's empty intersection misses",
        not _has(findings, "--widget on", "does not cover")
        and _has(findings, "--widget on", "vanish"),
        str(findings),
    )

    findings = analyze(
        "    #ifdef BOARD_A\n"
        '    if(commandCheck(msg_text+2, (char*)"widget on") == 0) { doA(); }\n'
        "    #endif\n"
        "    #ifdef BOARD_B\n"
        '    if(commandCheck(msg_text+2, (char*)"widget on") == 0) { doB(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        "        #ifdef BOARD_A\n"
        '        printlndeb("--widget on/off  A form\\n");\n'
        "        #endif\n"
        "        #ifdef BOARD_B\n"
        '        printlndeb("--widget on/off  B form\\n");\n'
        "        #endif\n"
        "        return;\n"
        "    }\n"
    )
    case(
        "D: correctly per-architecture-guarded mentions pass",
        findings == [],
        str(findings),
    )

    # Real shape (src/command_functions.cpp:1041/4356/4380): one mention
    # guarded by a single "A || B" #if, one definition per architecture
    # each guarded by just A or just B. A || B is implied for free once
    # either disjunct already holds, so this must stay clean -- a plain
    # set-subset test without _condition_implied_by()'s "||" handling
    # flags this as a false positive (--udplog would "vanish" nowhere).
    findings = analyze(
        "    #ifdef BOARD_A\n"
        '    if(commandCheck(msg_text+2, (char*)"udplog on") == 0 || '
        'commandCheck(msg_text+2, (char*)"udplog off") == 0) { doA(); }\n'
        "    #endif\n"
        "    #ifdef BOARD_B\n"
        '    if(commandCheck(msg_text+2, (char*)"udplog on") == 0 || '
        'commandCheck(msg_text+2, (char*)"udplog off") == 0) { doB(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        "        #if defined(BOARD_A) || defined(BOARD_B)\n"
        '        printlndeb("--udplog on/off  one line per datagram\\n");\n'
        "        #endif\n"
        "        return;\n"
        "    }\n"
    )
    case(
        "D: a single '||' mention guard covers each architecture's own single condition",
        findings == [],
        str(findings),
    )

    # -- B + item 5: mid-line mentions ---------------------------------------

    findings = analyze(
        "    #ifdef BOARD_Y\n"
        '    if(commandCheck(msg_text+2, (char*)"kiss on") == 0) { doOn(); }\n'
        "    else\n"
        '    if(commandCheck(msg_text+2, (char*)"kiss off") == 0) { doOff(); }\n'
        "    #endif\n"
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--other  (see --kiss)\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "B item5: mid-line '(see --kiss)' unguarded is a finding",
        _has(findings, "--kiss", "guard"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"weather") == 0 || '
        'commandCheck(msg_text+2, (char*)"wx") == 0) { doWeather(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--weather  temp/hum/press (alias --wx)\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "B item5: mid-line alias to an unguarded handler still passes",
        findings == [],
        str(findings),
    )

    # -- C: reverse coverage --------------------------------------------------

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"foo") == 0) { doFoo(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--foo  thing\\n--bogus  not a real command\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "C: --bogus mentioned with no definition is a finding",
        _has(findings, "--bogus"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"foo") == 0) { doFoo(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--foo  thing\\n");\n'
        "        #if INSTRUMENT_ENABLED\n"
        '        printlndeb("--onlybench  not a real command outside instrument\\n");\n'
        "        #endif\n"
        "        return;\n"
        "    }\n"
    )
    case(
        "C: an INSTRUMENT_ENABLED-only help mention is exempt from reverse coverage",
        not _has(findings, "--onlybench"),
        str(findings),
    )

    # -- E: output hazards ----------------------------------------------------

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"baz") == 0) { doBaz(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printfdeb("--baz 9;..9;  groups\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "E: ';' in printfdeb is a finding",
        _has(findings, "printfdeb", "';'"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"baz") == 0) { doBaz(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--baz 9;..9;  groups\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "E: ';' in printlndeb is not flagged",
        not _has(findings, "printfdeb"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"maxv") == 0) { doMaxv(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printlndeb("--maxv 100%% battery\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "E: '%%' in printlndeb is a finding",
        _has(findings, "printlndeb", "%%"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"maxv") == 0) { doMaxv(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printdeb("--maxv 100%% battery\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "E: '%%' in printdeb is a finding",
        _has(findings, "printdeb", "%%"),
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"maxv") == 0) { doMaxv(); }\n'
        '    if(commandCheck(msg_text+2, (char*)"help") == 0)\n'
        "    {\n"
        '        printfdeb("--maxv 100%% battery\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "E: '%%' in printfdeb is fine (real printf)",
        not _has(findings, "%%"),
        str(findings),
    )

    # -- item 6: (char*) cast is optional -------------------------------------

    findings = analyze(
        '    if(commandCheck(msg_text+2, "foo") == 0) { doFoo(); }\n'
        '    if(commandCheck(msg_text+2, "help") == 0)\n'
        "    {\n"
        '        printlndeb("--foo  does the thing\\n");\n'
        "        return;\n"
        "    }\n"
    )
    case(
        "item6: commandCheck() without the (char*) cast is still recognised",
        findings == [],
        str(findings),
    )

    # -- structural fallbacks --------------------------------------------------

    findings = analyze(
        "void commandAction(char *umsg_text, bool ble) { doNothing(); }\n"
    )
    case(
        "no rungs at all is fatal",
        len(findings) == 1 and "extraction pattern" in findings[0].message,
        str(findings),
    )

    findings = analyze(
        '    if(commandCheck(msg_text+2, (char*)"foo") == 0) { doFoo(); }\n'
    )
    case(
        "no --help block found is fatal",
        len(findings) == 1 and "help" in findings[0].message and findings[0].line == 1,
        str(findings),
    )

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    findings, _path = check()
    rel = SOURCE
    for f in findings:
        print(f"{rel}:{f.line}: {f.message}")

    if findings:
        print(f"\n{len(findings)} finding(s)")
        return 1

    print("help parity: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
