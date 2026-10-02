#!/usr/bin/env python3
"""Producers vs parser rungs, per board env -- and GUI element vs handler.

`docs/bugreport-web-volt-toggle-20260923.md` ("Root cause", "Same category")
found the `--volt` web-switch bug by hand: `web_setup.cpp` builds the command
string `--volt` and hands it to `commandAction()`, but the parser rung has
required `volt on|off` since `93d9fee6` (2026-07-03). A bare `--volt` can
never match a longer rung (`commandMatches()`'s own length check), so the
switch has been dead in both directions since that commit, on every board,
and nothing noticed. The same report also found a second, quieter class: a
GUI switch/rung compiled in on some boards and compiled OUT on others behind
a feature guard (`ENABLE_INA226`, `ENABLE_GPS`, `OneWire_GPIO`, ...) while the
web control and its `web_setup.cpp` handler carry no matching guard, so the
control is dead there too -- same symptom, different cause.

This is the prevention lint the report's "Fix campaign" wave 1 asks for. It
does two independent checks:

CHECK 1 -- PRODUCER VS RUNG, PER ENV
-------------------------------------
1. Find every internal *producer*: a place in `src/**/*.cpp` or
   `src/**/*.h` (excluding `src/command_functions.cpp` itself -- that file
   IS the parser, not a caller of it) that hands `commandAction()` a command
   string, either
     (a) a string literal directly (`commandAction((char*)"--gps on", ...)`),
         or
     (b) a `snprintf(buf, ..., "--fmt", args...)` into a buffer that the SAME
         function then passes to `commandAction()` a few statements later.
   Anything else (a `String::c_str()`, string concatenation, a bare
   pass-through of caller-supplied text) cannot be resolved statically and is
   counted as "unchecked" -- reported only with `--verbose`, never a failure.
   This mirrors the report's own method: "Pass-through producers (serial
   input, BLE text, multi-command splitting) carry user input and are out of
   scope."

2. Find every *rung* the parser can match a producer's string against: the
   hand-written ladder (`extract_commands.extract()`, imported) and the
   D2-06 toggle table (`toggle_table_lint.parse_rows()`, imported), each with
   its enclosing `#if` guard text.

3. Instantiate every producer's format string into one or two concrete
   probe strings (`%s` -> "on" AND "off", two probes; `%d/%i/%u/%ld` -> "1";
   `%f`/`%.Nf` -> "1.0"; `%c` -> "A", not in the brief but needed for the one
   `%c%i` producer in `loop_functions.cpp`) and match each probe against
   every rung with `commandMatches()` (`toggle_table_lint.commands_matches`,
   verified against `src/command_match.h` and
   `test/test_command_match/test_command_match.cpp` -- faithful, see the
   module docstring there).

4. For every one of the 32 board envs listed in
   `test/golden/native/variant-macros-effective.txt`, evaluate the producer's
   guard and every matching rung's guard with a three-valued evaluator
   (adapted from `command_name_scan.eval_guard`/`_tokenize`, see below) fed
   facts from that baseline file. A VIOLATION is: the producer is COMPILED IN
   for env E (guard true) and every rung that could match the probe is
   compiled OUT (guard false) or there is no matching rung at all. If the
   only matching rungs are guard-UNKNOWN (a macro the baseline never
   records), that is a WARNING, never a failure -- this tool has no way to
   tell.

   Per-env directory exclusion (`src/t-deck/`, `src/t-deck-pro/`,
   `src/t5-epaper/`, `src/esp32/`, `src/nrf52/`) is modelled from
   `platformio.ini`'s `[esp32]`/`[nrf52_base]` base `src_filter`/
   `build_src_filter` and the handful of `variants/*/platformio.ini` files
   that add one of the three UI dirs back (`t_deck`, `t_deck_plus`,
   `t_deck_pro`, `t5_epaper`) -- read as text, never through `pio`. See
   `_DIR_ALLOW` below; it is a closed, hand-verified table, not a general
   `build_src_filter` parser.

THE BASELINE FILE AS A THREE-VALUED FACT SOURCE
------------------------------------------------
`variant-macros-effective.txt` (`variant_macros_effective.py`) records, per
env, every macro that is EITHER set via a `-D` flag OR appears anywhere in
`variants/*/configuration.h` / `configuration_global.h` /
`configuration_default.h` and actually got `#define`d for that env's build.
That makes an entry's ABSENCE for one env meaningful in two different ways,
and this tool has to tell them apart:

  * the macro's name is present in the baseline for SOME OTHER env -> it is
    a tracked, board-selectable macro, and its absence here means definitely
    UNDEFINED (False), not unknown -- e.g. `ENABLE_INA226` is missing for
    `wireless-paper` but present for `t_deck`, so `wireless-paper` definitely
    does not have it;
  * the macro's name never appears in the baseline for ANY env -> it was
    never a variant/global/default macro or a `-D` flag at all (e.g. it is
    something the SDK or a library header defines), and this tool genuinely
    does not know -- UNKNOWN (None).

That is exactly the same "as designed / guard unknown / UNEXPLAINED"
three-valued shape `command_name_scan.py` already uses for its own guard
evaluator (`eval_guard`/`_tokenize`/the Kleene helpers) -- reused here by
import, extended with one thing that evaluator does not need: a value per
macro, so `defined(X)` and a bare `X` stay boolean-only (as designed) while
`X == N` can also be resolved when N is comparable to the recorded value.

CHECK 2 -- GUI ELEMENT VS webSetup_setParam() HANDLER
-------------------------------------------------------
Independent second check, same script per the brief: every
`_create_setup_switch_element("<param>", ...)` and
`_create_setup_textinput_element(id, label, value, placeholder, "<param>", ...)`
in `src/web_functions/web_functions.cpp` (the 5th argument is the
`/setparam/` name -- confirmed against the handler dispatch in
`web_setup.cpp`, which reads `setupData->paramName`) must, in every env
where the element itself compiles in, have a compiled-in
`if(setupData->paramName.equals("<param>"))` block in
`webSetup_setParam()` (`src/web_functions/web_setup.cpp`). A handler that
exists with no matching element is fine (the report's own footnote: "no
finding" for that direction).

    python3 test/golden/producer_match_lint.py
    python3 test/golden/producer_match_lint.py --verbose
    python3 test/golden/producer_match_lint.py --self-test

Exit 0 clean, 1 on any violation. No third-party dependencies beyond the
sibling `test/golden` modules it imports; stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
SRC = REPO_ROOT / "src"
COMMANDS_SRC = SRC / "command_functions.cpp"
WEB_FUNCTIONS_SRC = SRC / "web_functions" / "web_functions.cpp"
WEB_SETUP_SRC = SRC / "web_functions" / "web_setup.cpp"
BASELINE = REPO_ROOT / "test" / "golden" / "native" / "variant-macros-effective.txt"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_commands import blank_comments, extract, _guard_stack
from toggle_table_lint import commands_matches, extract_table_text, parse_rows
from command_name_scan import _kleene_and, _kleene_or, _kleene_not


# ---------------------------------------------------------------------------
# Directory-based per-env compilation model
# ---------------------------------------------------------------------------
#
# `platformio.ini`'s `[esp32]` src_filter and `[nrf52_base]` build_src_filter
# both exclude `t-deck/*`, `t-deck-pro/*`, `t5-epaper/*` by default -- verified
# 2026-09-23 by reading both sections and every `variants/*/platformio.ini`.
# Exactly four envs add one of the three back:
#   t_deck, t_deck_plus  -> +<t-deck/*>
#   t_deck_pro           -> +<t-deck-pro/*>
#   t5_epaper            -> +<t5-epaper/*>, plus two individual files it
#                            borrows out of t-deck-pro/ (the BQ27220 fuel
#                            gauge driver both boards share)
# No env anywhere re-adds these dirs for the nRF52 base; they are ESP32-only
# UI code. `esp32/*` compiles only on the ESP32 base, `nrf52/*` only on the
# nRF52 base (each excludes the other's directory, neither is ever added
# back). Everything else this tool finds a producer or GUI element in
# (`src/web_functions/*`, top-level `src/*.cpp`, `src/onebutton_functions.cpp`,
# `src/loop_functions.cpp`, ...) sits in neither base's exclusion list, so it
# compiles on every board env -- modelled as unconditionally True. Some other
# directories (`Displays/`, `Fonts/`, `GFX_Root/`, `Platforms/`, `safeboot/`,
# `ui_common/`, `lvgl/`) ARE excluded by both bases too, but hold display/UI
# library code, not `commandAction()` producers or `/setparam/` GUI elements
# -- confirmed empty of both by grep 2026-09-23 -- so they are deliberately
# left out of this table rather than modelled speculatively.
_DIR_ALLOW = {
    "t-deck": {"t_deck", "t_deck_plus"},
    "t-deck-pro": {"t_deck_pro"},
    "t5-epaper": {"t5_epaper"},
}
_T5_BORROWED_FILES = {"t-deck-pro/bq27220.cpp", "t-deck-pro/bq27220_data_memory.c"}


_READD_RE = re.compile(r"\+<(t-deck|t-deck-pro|t5-epaper)/([^>]*)>")
_ENV_SECTION_RE = re.compile(r"^\[env:([^\]]+)\]")


def dir_table_drift(root: Path = REPO_ROOT) -> List[str]:
    """Compare _DIR_ALLOW / _T5_BORROWED_FILES with the variant ini files.

    The table above is hand-read from platformio.ini. A new variant that
    re-adds one of these directories would otherwise be modelled as not
    compiling it, and every producer in there would silently stop being
    checked for that env. Returns one message per disagreement.
    """
    whole: Dict[str, Set[str]] = {}
    files: Set[str] = set()
    for ini in [root / "platformio.ini"] + sorted(root.glob("variants/*/platformio.ini")):
        env = None
        for line in ini.read_text(errors="replace").splitlines():
            m = _ENV_SECTION_RE.match(line.strip())
            if m:
                env = m.group(1)
                continue
            if line.lstrip().startswith(";"):
                continue
            for d, rest in _READD_RE.findall(line):
                if rest == "*":
                    whole.setdefault(d, set()).add(env or f"<no env in {ini}>")
                else:
                    files.add(f"{d}/{rest}")
    msgs = []
    for d in sorted(set(whole) | set(_DIR_ALLOW)):
        if whole.get(d, set()) != _DIR_ALLOW.get(d, set()):
            msgs.append(f"src/{d}/ is re-added by {sorted(whole.get(d, set()))} in the ini "
                        f"files, but _DIR_ALLOW says {sorted(_DIR_ALLOW.get(d, set()))}")
    if files != _T5_BORROWED_FILES:
        msgs.append(f"single-file re-adds in the ini files are {sorted(files)}, "
                    f"but _T5_BORROWED_FILES says {sorted(_T5_BORROWED_FILES)}")
    return msgs


def dir_tag(relpath: str) -> Optional[str]:
    """The directory-exclusion bucket a file falls in, or None (unrestricted)."""
    parts = relpath.split("/")
    if len(parts) >= 2 and parts[0] == "src" and parts[1] in (
        "t-deck", "t-deck-pro", "t5-epaper", "esp32", "nrf52",
    ):
        return parts[1]
    return None


def dir_active(relpath: str, tag: Optional[str], env: str, esp32: Optional[bool]) -> bool:
    """Does this env's build even compile the file this producer lives in?

    Returns a plain bool, not three-valued: every case this table covers is
    a fact read out of `platformio.ini` text, never a guess.
    """
    if tag is None:
        return True
    if tag == "esp32":
        return bool(esp32)
    if tag == "nrf52":
        return esp32 is False
    allowed = _DIR_ALLOW.get(tag, set())
    if env in allowed:
        return True
    if tag == "t-deck-pro" and env == "t5_epaper":
        rel = relpath.removeprefix("src/")
        return rel in _T5_BORROWED_FILES
    return False


# ---------------------------------------------------------------------------
# Baseline facts (test/golden/native/variant-macros-effective.txt)
# ---------------------------------------------------------------------------

def load_baseline(path: Path = BASELINE) -> Tuple[Dict[str, Dict[str, str]], Set[str]]:
    """Per-env {macro: value} plus the union of every macro name ever seen.

    The union is what makes an ABSENT macro for one env a definite False
    (tracked, just not defined here) rather than an unknown (never tracked
    at all) -- see the module docstring's "THE BASELINE FILE AS A
    THREE-VALUED FACT SOURCE" section.
    """
    per_env: Dict[str, Dict[str, str]] = {}
    all_names: Set[str] = set()
    for line in path.read_text(errors="replace").splitlines():
        if not line.strip():
            continue
        try:
            env, rest = line.split(" ", 1)
        except ValueError:
            continue
        if "=" in rest:
            name, _, value = rest.partition("=")
        else:
            name, value = rest, ""
        per_env.setdefault(env, {})[name] = value
        all_names.add(name)
    return per_env, all_names


def platform_of(env: str, per_env: Dict[str, Dict[str, str]]) -> Optional[bool]:
    """True=ESP32, False=nRF52, None=can't tell from the baseline."""
    facts = per_env.get(env, {})
    if "ESP32" in facts:
        return True
    if "NRF52_SERIES" in facts:
        return False
    return None


class Facts:
    """Three-valued `defined(X)` plus a raw value for `X == N`, for one env."""

    def __init__(self, defined_here: Dict[str, str], all_names: Set[str]):
        self._here = defined_here
        self._all = all_names

    def get(self, name: str) -> Optional[bool]:
        if name in self._here:
            return True
        if name in self._all:
            return False
        return None

    def value(self, name: str) -> Optional[str]:
        return self._here.get(name)


def facts_for_env(env: str, per_env: Dict[str, Dict[str, str]], all_names: Set[str]) -> Facts:
    return Facts(per_env.get(env, {}), all_names)


# ---------------------------------------------------------------------------
# Guard evaluator -- adapted from command_name_scan.eval_guard/_tokenize.
#
# Adds one thing that tool never needed: `IDENT == NUMBER`. `_guard_stack`
# text never emits `!=`, so that operator is intentionally not handled --
# adding it un-asked would be scope creep this campaign does not need.
# Kleene AND/OR/NOT are imported unchanged; a fresh recursive-descent parser
# is used instead of importing `_GuardParser` because equality has to be
# resolved inside `_atom`, before the generic "bare identifier" fallback
# claims the token.
# ---------------------------------------------------------------------------

_TOKEN_RE = re.compile(
    r"==|\|\||&&|\bor\b|\band\b|\bnot\b|\bdefined\b|\belif\b|[!()]|"
    r"[A-Za-z_][A-Za-z0-9_]*|[0-9]+|."
)


def _tokenize(guard: str) -> List[str]:
    return [t for t in _TOKEN_RE.findall(guard) if not t.isspace()]


class _GuardParser:
    def __init__(self, tokens: Sequence[str], facts: Facts):
        self._toks = tokens
        self._i = 0
        self._facts = facts

    def _peek(self) -> Optional[str]:
        return self._toks[self._i] if self._i < len(self._toks) else None

    def _advance(self) -> Optional[str]:
        t = self._peek()
        self._i += 1
        return t

    def parse(self) -> Optional[bool]:
        try:
            return self._or()
        except (IndexError, ValueError):
            return None  # fail closed: a malformed guard is UNKNOWN, not a crash

    def _or(self) -> Optional[bool]:
        v = self._and()
        while self._peek() in ("||", "or"):
            self._advance()
            v = _kleene_or(v, self._and())
        return v

    def _and(self) -> Optional[bool]:
        v = self._not()
        while self._peek() in ("&&", "and"):
            self._advance()
            v = _kleene_and(v, self._not())
        return v

    def _not(self) -> Optional[bool]:
        if self._peek() in ("!", "not"):
            self._advance()
            return _kleene_not(self._not())
        return self._atom()

    def _atom(self) -> Optional[bool]:
        t = self._advance()
        if t == "elif":
            return self._or()
        if t == "(":
            v = self._or()
            if self._peek() == ")":
                self._advance()
            return v
        if t == "defined":
            has_paren = self._peek() == "("
            if has_paren:
                self._advance()
            name = self._advance()
            if has_paren and self._peek() == ")":
                self._advance()
            return self._facts.get(name or "")
        if t is None:
            return None
        if t.isdigit():
            return t != "0"
        if re.match(r"^[A-Za-z_]", t):
            if self._peek() == "==":
                self._advance()
                num = self._advance()
                defined = self._facts.get(t)
                if defined is None:
                    return None
                val = "0" if defined is False else self._facts.value(t)
                if val is None or num is None:
                    return None
                try:
                    return int(val, 0) == int(num, 0)
                except ValueError:
                    return None
            return self._facts.get(t)
        return None  # a stray character token


def eval_guard(guard: str, facts: Facts) -> Optional[bool]:
    if guard.strip() == "":
        return True
    return _GuardParser(_tokenize(guard), facts).parse()


# ---------------------------------------------------------------------------
# Balanced-call scanning: commandAction()/snprintf()/the two GUI factories.
# Works on comment-blanked text so string/char literals keep their content
# but // and /* */ never confuse the paren/quote scanner.
# ---------------------------------------------------------------------------

def _find_calls(text: str, name: str) -> List[Tuple[int, str]]:
    """Every `name(...)` call: (1-based start line, raw argument text).

    Balances parens while skipping over string/char literal contents, so a
    call wrapped across several lines, or one whose arguments themselves
    contain parens (a cast, a nested call), is still captured whole.
    """
    out: List[Tuple[int, str]] = []
    pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"\s*\(")
    for m in pattern.finditer(text):
        start = m.end() - 1
        i = start
        n = len(text)
        depth = 0
        in_str = in_chr = False
        while i < n:
            c = text[i]
            if in_str:
                if c == "\\":
                    i += 2
                    continue
                if c == '"':
                    in_str = False
            elif in_chr:
                if c == "\\":
                    i += 2
                    continue
                if c == "'":
                    in_chr = False
            else:
                if c == '"':
                    in_str = True
                elif c == "'":
                    in_chr = True
                elif c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
                    if depth == 0:
                        break
            i += 1
        lineno = text.count("\n", 0, m.start()) + 1
        out.append((lineno, text[start + 1:i]))
    return out


def split_args(args_text: str) -> List[str]:
    """Top-level comma split, respecting nested parens/brackets/quotes."""
    parts: List[str] = []
    cur: List[str] = []
    depth = 0
    in_str = in_chr = False
    i, n = 0, len(args_text)
    while i < n:
        c = args_text[i]
        if in_str:
            cur.append(c)
            if c == "\\" and i + 1 < n:
                cur.append(args_text[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
        elif in_chr:
            cur.append(c)
            if c == "\\" and i + 1 < n:
                cur.append(args_text[i + 1])
                i += 2
                continue
            if c == "'":
                in_chr = False
        else:
            if c == '"':
                in_str = True
                cur.append(c)
            elif c == "'":
                in_chr = True
                cur.append(c)
            elif c in "([{":
                depth += 1
                cur.append(c)
            elif c in ")]}":
                depth -= 1
                cur.append(c)
            elif c == "," and depth == 0:
                parts.append("".join(cur).strip())
                cur = []
            else:
                cur.append(c)
        i += 1
    if cur or parts:
        parts.append("".join(cur).strip())
    return parts


_LITERAL_ARG_RE = re.compile(r'^(?:\(\s*char\s*\*\s*\)\s*)?"((?:[^"\\]|\\.)*)"$')
_IDENT_ARG_RE = re.compile(r'^(?:\(\s*char\s*\*\s*\)\s*)?([A-Za-z_]\w*)$')
_BUFNAME_RE = re.compile(r"^&?\s*([A-Za-z_]\w*)")

# printf-style conversions this tool instantiates. Anything else (a `%x`, a
# width/precision it does not recognise) leaves the specifier untouched,
# which then fails the "looks like a command name" shape check below and is
# reported as unchecked rather than silently mis-instantiated.
_SPEC_RE = re.compile(r"%[-+ 0#]*\d*(?:\.\d+)?(?:ll|l|h)?([sdiufcxX])")


@dataclass(frozen=True)
class Producer:
    file: str          # repo-relative path
    line: int          # commandAction() call site
    guard: str
    dir_tag: Optional[str]
    literal: Optional[str] = None   # "--"-prefixed format text, resolved
    unchecked_reason: Optional[str] = None

    @property
    def bare_fmt(self) -> Optional[str]:
        if self.literal is None:
            return None
        return self.literal.removeprefix("--")

    def probes(self) -> List[str]:
        fmt = self.bare_fmt
        if fmt is None:
            return []
        if "%" not in fmt:
            return [fmt]
        has_s = any(m.group(1) == "s" for m in _SPEC_RE.finditer(fmt))

        def render(s_value: str) -> str:
            def repl(m: re.Match[str]) -> str:
                conv = m.group(1)
                if conv == "s":
                    return s_value
                if conv in ("d", "i", "u"):
                    return "1"
                if conv == "f":
                    return "1.0"
                if conv in ("x", "X"):
                    return "1"
                if conv == "c":
                    return "A"
                return m.group(0)
            return _SPEC_RE.sub(repl, fmt)

        return [render("on"), render("off")] if has_s else [render("")]


def scan_producers_text(text: str, relpath: str) -> List[Producer]:
    """Every commandAction() call site in one file's (already read) text."""
    blanked = blank_comments(text)
    lines = blanked.splitlines()
    guards = _guard_stack(lines)
    reset_lines = {i for i, l in enumerate(lines, 1) if re.match(r"^\}", l)}
    tag = dir_tag(relpath)

    events: List[Tuple[int, int, str, str]] = []  # (pos, line, kind, args)
    for kind, name in (("snprintf", "snprintf"), ("snprintf", "sprintf"),
                       ("action", "commandAction")):
        for lineno, args in _find_calls(blanked, name):
            # position not tracked separately -- _find_calls already returns
            # calls in file order for one `name`; merge by line number, and
            # break same-line ties by re-finding start offsets is unnecessary
            # here because snprintf/commandAction never share a line in this
            # codebase (verified: neither call's args, once resolved, embeds
            # the other literally). Kept simple rather than exact-offset
            # merged for that reason.
            events.append((lineno, lineno, kind, args))
    events.sort(key=lambda e: e[0])

    pending: Dict[str, Tuple[str, int]] = {}
    producers: List[Producer] = []
    prev_line = 0
    for _, lineno, kind, args in events:
        if any(prev_line < rl <= lineno for rl in reset_lines):
            pending = {}
        prev_line = lineno
        argslist = split_args(args)
        if kind == "snprintf":
            if len(argslist) < 3:
                continue
            bufm = _BUFNAME_RE.match(argslist[0])
            if not bufm:
                continue
            buf = bufm.group(1)
            fmtm = _LITERAL_ARG_RE.match(argslist[2])
            if fmtm and fmtm.group(1).startswith("--"):
                pending[buf] = (fmtm.group(1), lineno)
            else:
                pending.pop(buf, None)
        else:
            if not argslist or not argslist[0]:
                continue
            arg0 = argslist[0]
            litm = _LITERAL_ARG_RE.match(arg0)
            if litm:
                lit = litm.group(1)
                if lit.startswith("--"):
                    producers.append(Producer(
                        file=relpath, line=lineno, guard=guards[lineno],
                        dir_tag=tag, literal=lit,
                    ))
                continue
            idm = _IDENT_ARG_RE.match(arg0)
            if idm:
                name = idm.group(1)
                entry = pending.pop(name, None)
                if entry is None:
                    producers.append(Producer(
                        file=relpath, line=lineno, guard=guards[lineno],
                        dir_tag=tag,
                        unchecked_reason=f"{name}: no resolvable snprintf format nearby",
                    ))
                else:
                    fmt, snline = entry
                    sn_guard = guards[snline]
                    ac_guard = guards[lineno]
                    guard = sn_guard if sn_guard == ac_guard else " && ".join(
                        g for g in (sn_guard, ac_guard) if g
                    )
                    producers.append(Producer(
                        file=relpath, line=lineno, guard=guard,
                        dir_tag=tag, literal=fmt,
                    ))
            else:
                producers.append(Producer(
                    file=relpath, line=lineno, guard=guards[lineno],
                    dir_tag=tag,
                    unchecked_reason=f"non-literal argument: {arg0[:50]!r}",
                ))
    return producers


def scan_producers_tree(root: Path = SRC) -> List[Producer]:
    out: List[Producer] = []
    for path in sorted(root.rglob("*.cpp")) + sorted(root.rglob("*.h")):
        if path.resolve() == COMMANDS_SRC.resolve():
            continue
        rel = str(path.relative_to(REPO_ROOT))
        text = path.read_text(errors="replace")
        if "commandAction(" not in text:
            continue
        out.extend(scan_producers_text(text, rel))
    return out


# ---------------------------------------------------------------------------
# Rungs: the ladder + the toggle table, unified
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Rung:
    bare: str
    guard: str
    line: int
    source: str  # "ladder" | "toggle"


def load_rungs(src: Path = COMMANDS_SRC) -> List[Rung]:
    """Ladder rungs plus toggle-table rows, guards in ONE consistent format.

    `toggle_table_lint.parse_rows()` records a row's guard as the RAW
    preprocessor line text, "#if"/"#ifdef"/"#elif"/"#else" prefix and all
    (e.g. `"#if defined (ENABLE_INA226)"`) -- correct for that lint's own
    job (printing it back to a human), wrong for this one: this tool's
    evaluator expects `extract_commands._guard_stack`'s NORMALISED text
    (`ifdef` folded to `"defined X"`, `ifndef` to `"!defined X"`, `else` to
    `"!(...)"`, no bare `#if`/`#ifdef` token ever appearing). Feeding the raw
    form in made every `#if`/`#ifdef` keyword parse as an unresolvable bare
    identifier and silently dropped the real condition after it -- caught by
    the very first real-tree run, where `--ina226 on|off` fell into "guard
    unknown" on all 32 envs instead of resolving true/false per env. Fixed by
    recomputing every rung's guard -- ladder AND toggle table -- from the
    SAME `_guard_stack` pass over the one file, keyed by line number.
    """
    text = src.read_text(errors="replace")
    blanked = blank_comments(text)
    full_guards = _guard_stack(blanked.splitlines())

    rungs = [Rung(bare=c.name, guard=c.guard, line=c.line, source="ladder")
             for c in extract(src)]

    # extract_commands._LADDER_IF_RE only recognises an `if(` indented
    # EXACTLY 4 spaces as a top-level ladder test; one indented 8 spaces
    # because it sits under an `#if` block (`netmode wifi|eth`, `relay
    # on|off`, `udplog on`, `persistflash|persistsd|immediatesave on`,
    # `wifi on` -- verified 2026-09-23, 10 of the 13 `extract(inner=True)`
    # entries on this tree) is bucketed as an "inner disambiguation" instead
    # of a ladder rung, even though it is dispatched exactly like every
    # other top-level command. Excluding those 10 made this tool's first
    # real-tree run report `netmode wifi`/`eth` as dead everywhere -- INCLUDING
    # the two envs where both the producer and the rung actually compile in
    # and agree. This is a real defect in extract_commands.py's indentation
    # test, not a defect in the netmode code; extract_commands.py is outside
    # this file's ownership for this campaign, so it is not touched here
    # (escalated in the wave report instead). Folding `inner=True` in as
    # SUPPLEMENTARY rungs fixes the false positive: the 3 genuine inner
    # disambiguations it also carries (`bmx off`, `operatorname `,
    # `aprscomment `) are harmless additions here -- `bmx off` already has an
    # identical ladder-level entry, and the other two are argument-form names
    # no producer in this tree targets.
    rungs += [Rung(bare=c.name, guard=c.guard, line=c.line, source="ladder-inner")
              for c in extract(src, inner=True)]

    table_text = extract_table_text(text)
    start_lineno = text.count("\n", 0, text.index(table_text)) + 1
    for row in parse_rows(table_text, start_lineno=start_lineno):
        rungs.append(Rung(bare=row.bare, guard=full_guards[row.lineno],
                           line=row.lineno, source="toggle"))
    return rungs


# ---------------------------------------------------------------------------
# Core evaluation -- pure, no file I/O, so --self-test can drive it directly.
# ---------------------------------------------------------------------------

@dataclass
class Finding:
    file: str
    line: int
    what: str
    envs: List[str]
    severity: str  # "VIOLATION" | "WARNING"


def _dir_and_guard(active_dir: bool, guard: str, facts: Facts) -> Optional[bool]:
    if not active_dir:
        return False
    return eval_guard(guard, facts)


def check_producers(
    producers: Sequence[Producer],
    rungs: Sequence[Rung],
    envs: Sequence[str],
    env_facts: Dict[str, Facts],
    env_platform: Dict[str, Optional[bool]],
) -> Tuple[List[Finding], List[Finding]]:
    violations: List[Finding] = []
    warnings: List[Finding] = []

    for prod in producers:
        if prod.unchecked_reason is not None:
            continue
        for probe in prod.probes():
            matching = [r for r in rungs if commands_matches(probe, r.bare)]
            bad_envs: List[str] = []
            warn_envs: List[str] = []
            for env in envs:
                facts = env_facts[env]
                active = _dir_and_guard(
                    dir_active(prod.file, prod.dir_tag, env, env_platform.get(env)),
                    prod.guard, facts,
                )
                if active is not True:
                    continue
                verdicts = [eval_guard(r.guard, facts) for r in matching]
                if not matching or all(v is False for v in verdicts):
                    bad_envs.append(env)
                elif all(v is not True for v in verdicts):
                    # every verdict is False-or-None and at least one is None
                    warn_envs.append(env)
            if bad_envs:
                violations.append(Finding(
                    file=prod.file, line=prod.line,
                    what=f'producer "--{probe}" (from "--{prod.bare_fmt}") has no live rung',
                    envs=bad_envs, severity="VIOLATION",
                ))
            if warn_envs:
                warnings.append(Finding(
                    file=prod.file, line=prod.line,
                    what=f'producer "--{probe}" (from "--{prod.bare_fmt}") matches only '
                         f"guard-unknown rung(s)",
                    envs=warn_envs, severity="WARNING",
                ))
    return violations, warnings


# ---------------------------------------------------------------------------
# GUI element vs handler
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class GuiElement:
    param: str
    guard: str
    line: int
    kind: str  # "switch" | "textinput"


@dataclass(frozen=True)
class Handler:
    param: str
    guard: str
    line: int


_SWITCH_RE = re.compile(r"_create_setup_switch_element")
_TEXTINPUT_RE = re.compile(r"_create_setup_textinput_element")
_HANDLER_RE = re.compile(r'setupData->paramName\.equals\(\s*"([^"]*)"\s*\)')


def scan_gui_elements(text: str) -> List[GuiElement]:
    blanked = blank_comments(text)
    lines = blanked.splitlines()
    guards = _guard_stack(lines)
    out: List[GuiElement] = []
    for lineno, args in _find_calls(blanked, "_create_setup_switch_element"):
        a = split_args(args)
        if a:
            m = _LITERAL_ARG_RE.match(a[0])
            if m:
                out.append(GuiElement(param=m.group(1), guard=guards[lineno],
                                       line=lineno, kind="switch"))
    for lineno, args in _find_calls(blanked, "_create_setup_textinput_element"):
        a = split_args(args)
        if len(a) >= 5:
            m = _LITERAL_ARG_RE.match(a[4])
            if m:
                out.append(GuiElement(param=m.group(1), guard=guards[lineno],
                                       line=lineno, kind="textinput"))
    return out


_SETPARAM_RE = re.compile(r"^void\s+webSetup_setParam\s*\(", re.MULTILINE)


def scan_handlers(text: str) -> List[Handler]:
    """The `paramName.equals(...)` blocks of webSetup_setParam() ONLY.

    web_setup.cpp also holds webSetup_getParam(), which answers the same
    param names for /getparam/ and is unguarded. Counting its blocks as
    handlers gave every GUI element an always-present twin, so a setParam
    block that was guarded or deleted could never be reported. The scan is
    therefore cut to setParam's body: from its signature to the next
    column-0 `}`. No setParam function at all is a hard error, not an
    empty (and silently passing) handler list.
    """
    blanked = blank_comments(text)
    lines = blanked.splitlines()
    guards = _guard_stack(lines)
    start = _SETPARAM_RE.search(blanked)
    if start is None:
        raise RuntimeError("webSetup_setParam() not found in web_setup.cpp")
    end = re.compile(r"^\}", re.MULTILINE).search(blanked, start.end())
    body_end = end.start() if end else len(blanked)
    out: List[Handler] = []
    for m in _HANDLER_RE.finditer(blanked, start.end(), body_end):
        lineno = blanked.count("\n", 0, m.start()) + 1
        out.append(Handler(param=m.group(1), guard=guards[lineno], line=lineno))
    return out


def check_gui_parity(
    elements: Sequence[GuiElement],
    handlers: Sequence[Handler],
    envs: Sequence[str],
    env_facts: Dict[str, Facts],
) -> Tuple[List[Finding], List[Finding]]:
    violations: List[Finding] = []
    warnings: List[Finding] = []
    by_param: Dict[str, List[Handler]] = defaultdict(list)
    for h in handlers:
        by_param[h.param].append(h)

    for elem in elements:
        cands = by_param.get(elem.param, [])
        bad_envs: List[str] = []
        warn_envs: List[str] = []
        for env in envs:
            facts = env_facts[env]
            if eval_guard(elem.guard, facts) is not True:
                continue
            verdicts = [eval_guard(h.guard, facts) for h in cands]
            if not cands or all(v is False for v in verdicts):
                bad_envs.append(env)
            elif all(v is not True for v in verdicts):
                warn_envs.append(env)
        if bad_envs:
            violations.append(Finding(
                file="src/web_functions/web_functions.cpp", line=elem.line,
                what=f'GUI {elem.kind} element "{elem.param}" has no live '
                     f"webSetup_setParam() handler",
                envs=bad_envs, severity="VIOLATION",
            ))
        if warn_envs:
            warnings.append(Finding(
                file="src/web_functions/web_functions.cpp", line=elem.line,
                what=f'GUI {elem.kind} element "{elem.param}" matches only a '
                     f"guard-unknown handler",
                envs=warn_envs, severity="WARNING",
            ))
    return violations, warnings


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def collapse_envs(envs: Sequence[str], all_envs: Sequence[str]) -> str:
    envs = sorted(set(envs))
    all_set = set(all_envs)
    if set(envs) == all_set:
        return f"ALL {len(all_envs)} envs"
    complement = sorted(all_set - set(envs))
    if len(envs) > len(all_envs) // 2 and complement:
        return f"all except {', '.join(complement)}"
    return ", ".join(envs)


def render_finding(f: Finding, all_envs: Sequence[str]) -> str:
    return f"{f.severity} {f.file}:{f.line}: {f.what} [{collapse_envs(f.envs, all_envs)}]"


# ---------------------------------------------------------------------------
# Self-test -- synthetic fixtures only, no tree access.
# ---------------------------------------------------------------------------

def _facts(defined: Dict[str, str], all_names: Set[str]) -> Facts:
    return Facts(defined, all_names)


def self_test() -> int:
    ok = True

    def expect(name: str, cond: bool) -> None:
        nonlocal ok
        print(("  ok  " if cond else "SELF-TEST FAIL: ") + name)
        ok = ok and cond

    # -- commands_matches sanity (imported, already unit-tested elsewhere;
    #    just pin the one behaviour this tool leans on hardest) -------------
    expect('argument-form rung "setname " accepts "setname X"',
           commands_matches("setname X", "setname "))
    expect('exact-token rung "volt on" rejects bare "volt"',
           not commands_matches("volt", "volt on"))

    # -- producer extraction: literal, snprintf-paired, and unchecked -------
    fixture_cpp = (
        'void f() {\n'
        '    commandAction((char*)"--gps on", false);\n'
        '    char buf[32];\n'
        '    if (x) {\n'
        '        snprintf(buf, sizeof(buf), "--foo %s", v);\n'
        '        commandAction(buf, false);\n'
        '    } else {\n'
        '        snprintf(buf, sizeof(buf), "--foo %s", v);\n'
        '        commandAction(buf, false);\n'
        '    }\n'
        '    commandAction((char*)someString.c_str(), false);\n'
        '}\n'
    )
    prods = scan_producers_text(fixture_cpp, "src/fixture.cpp")
    literal_prods = [p for p in prods if p.unchecked_reason is None]
    expect("a direct literal producer is found",
           any(p.literal == "--gps on" for p in literal_prods))
    expect("an if/else pair both resolve via the SAME buffer without cross-talk",
           sum(1 for p in literal_prods if p.literal == "--foo %s") == 2)
    expect("a String::c_str() argument is unchecked, not mis-parsed",
           any(p.unchecked_reason is not None for p in prods))

    # -- probe instantiation -------------------------------------------------
    p_bare = Producer(file="x", line=1, guard="", dir_tag=None, literal="--volt")
    expect('bare "--volt" instantiates to exactly one probe "volt"',
           p_bare.probes() == ["volt"])
    p_s = Producer(file="x", line=1, guard="", dir_tag=None, literal="--volt %s")
    expect('"--volt %s" instantiates to ["volt on", "volt off"]',
           p_s.probes() == ["volt on", "volt off"])
    p_d = Producer(file="x", line=1, guard="", dir_tag=None, literal="--maxhop %d")
    expect('"--maxhop %d" instantiates to ["maxhop 1"]', p_d.probes() == ["maxhop 1"])
    p_f = Producer(file="x", line=1, guard="", dir_tag=None, literal="--maxv %.3f")
    expect('"--maxv %.3f" instantiates to ["maxv 1.0"]', p_f.probes() == ["maxv 1.0"])

    # -- guard evaluator: defined()/bare/!/&&/||/parens/== -------------------
    facts = _facts({"FEAT": "1"}, {"FEAT", "OTHER"})
    expect("defined(FEAT) is True when present", eval_guard("defined(FEAT)", facts) is True)
    expect("defined(OTHER) is False when tracked-absent",
           eval_guard("defined(OTHER)", facts) is False)
    expect("defined(GHOST) is None when never tracked",
           eval_guard("defined(GHOST)", facts) is None)
    expect("bare identifier form", eval_guard("FEAT", facts) is True)
    expect("negation", eval_guard("!defined(OTHER)", facts) is True)
    expect("AND short-circuits on a known False",
           eval_guard("defined(OTHER) && defined(GHOST)", facts) is False)
    expect("OR short-circuits on a known True",
           eval_guard("defined(FEAT) || defined(GHOST)", facts) is True)
    expect("OR of two unknowns stays unknown",
           eval_guard("defined(GHOST) || defined(GHOST)", facts) is None)
    expect("parenthesised nesting", eval_guard("(defined(FEAT))", facts) is True)
    expect("empty guard is unconditionally true", eval_guard("", facts) is True)
    facts_eq = _facts({"BOARD_REV": "3"}, {"BOARD_REV"})
    expect("X == N true case", eval_guard("BOARD_REV == 3", facts_eq) is True)
    expect("X == N false case", eval_guard("BOARD_REV == 2", facts_eq) is False)

    # -- end-to-end: bare "--volt" vs rung "volt on" -> violation -----------
    rungs = [Rung(bare="volt on", guard="", line=1, source="ladder"),
             Rung(bare="proz off", guard="", line=1, source="ladder")]
    envs = ["envA"]
    ef = {"envA": _facts({}, set())}
    ep: Dict[str, Optional[bool]] = {"envA": True}
    viol, warn = check_producers([p_bare], rungs, envs, ef, ep)
    expect('bare "--volt" vs rung "volt on" is a VIOLATION', len(viol) == 1 and not warn)

    # -- "--volt %s" against real on/off rungs -> no violation --------------
    rungs_onoff = [Rung(bare="volt on", guard="", line=1, source="ladder"),
                   Rung(bare="volt off", guard="", line=1, source="ladder")]
    viol2, warn2 = check_producers([p_s], rungs_onoff, envs, ef, ep)
    expect('"--volt %s" against "volt on"/"volt off" is clean', not viol2 and not warn2)

    # -- guarded rung, unguarded producer: violation on the ONE env missing
    #    the feature, clean on the one that has it ---------------------------
    p_feat = Producer(file="x", line=5, guard="", dir_tag=None, literal="--feat on")
    rung_feat = [Rung(bare="feat on", guard="defined(FEAT)", line=1, source="ladder")]
    envs2 = ["has_feat", "no_feat"]
    ef2 = {
        "has_feat": _facts({"FEAT": ""}, {"FEAT"}),
        "no_feat": _facts({}, {"FEAT"}),
    }
    ep2: Dict[str, Optional[bool]] = {"has_feat": True, "no_feat": True}
    viol3, warn3 = check_producers([p_feat], rung_feat, envs2, ef2, ep2)
    expect("guarded rung + unguarded producer violates only the env lacking the guard",
           len(viol3) == 1 and viol3[0].envs == ["no_feat"] and not warn3)

    # -- guard-unknown rung is a WARNING, not a VIOLATION --------------------
    rung_unknown = [Rung(bare="feat on", guard="defined(GHOST)", line=1, source="ladder")]
    ef3 = {"only_env": _facts({}, set())}  # GHOST never tracked anywhere
    ep3: Dict[str, Optional[bool]] = {"only_env": True}
    viol4, warn4 = check_producers([p_feat], rung_unknown, ["only_env"], ef3, ep3)
    expect("a guard-unknown-only match warns, does not fail", not viol4 and len(warn4) == 1)

    # -- directory exclusion: a t-deck/ producer does not fire on a non-tdeck
    #    env even though its own #if guard is empty -------------------------
    p_tdeck = Producer(file="src/t-deck/x.cpp", line=1, guard="", dir_tag="t-deck",
                        literal="--onlytdeck on")
    ef4 = {"t_deck": _facts({}, set()), "ttgo_tbeam": _facts({}, set())}
    ep4: Dict[str, Optional[bool]] = {"t_deck": True, "ttgo_tbeam": True}
    viol5, _ = check_producers([p_tdeck], [], ["t_deck", "ttgo_tbeam"], ef4, ep4)
    expect("a t-deck/-only producer only violates on t_deck, not ttgo_tbeam",
           len(viol5) == 1 and viol5[0].envs == ["t_deck"])

    # -- GUI parity: unguarded element / guarded handler -> violation on the
    #    env where the handler drops out; guarded element / unguarded
    #    handler -> always fine -------------------------------------------
    elem_unguarded = GuiElement(param="volt", guard="", line=10, kind="switch")
    handler_guarded = [Handler(param="volt", guard="defined(FEAT)", line=20)]
    gviol, gwarn = check_gui_parity([elem_unguarded], handler_guarded, envs2, ef2)
    expect("unguarded GUI element + guarded handler violates the env without the guard",
           len(gviol) == 1 and gviol[0].envs == ["no_feat"] and not gwarn)

    elem_guarded = GuiElement(param="volt", guard="defined(FEAT)", line=10, kind="switch")
    handler_unguarded = [Handler(param="volt", guard="", line=20)]
    gviol2, gwarn2 = check_gui_parity([elem_guarded], handler_unguarded, envs2, ef2)
    expect("guarded element + unguarded handler is always fine", not gviol2 and not gwarn2)

    handler_missing: List[Handler] = []
    gviol3, _ = check_gui_parity([elem_unguarded], handler_missing, ["only_env"], ef3)
    expect("an element with NO handler at all violates everywhere it compiles",
           len(gviol3) == 1)

    # -- scan_handlers() reads setParam only: a getParam twin must not stand
    #    in for a missing or guarded setParam block (the advisor's mutation:
    #    before this cut, deleting the setParam `gps` block stayed green) ----
    web_setup_fixture = (
        "void webSetup_setParam(setupStruct *setupData){\n"
        "    #if defined(FEAT)\n"
        '    if(setupData->paramName.equals("gps")) {\n'
        "        return;\n"
        "    } else\n"
        "    #endif\n"
        '    if(setupData->paramName.equals("track")) {\n'
        "        return;\n"
        "    }\n"
        "}\n"
        "\n"
        "void webSetup_getParam(setupStruct *setupData){\n"
        '    if(setupData->paramName.equals("gps")) {\n'
        "        return;\n"
        "    } else\n"
        '    if(setupData->paramName.equals("volt")) {\n'
        "        return;\n"
        "    }\n"
        "}\n"
    )
    scanned = scan_handlers(web_setup_fixture)
    expect("scan_handlers() sees setParam blocks only, not getParam's",
           sorted(h.param for h in scanned) == ["gps", "track"])
    gviol4, _ = check_gui_parity(
        [GuiElement(param="gps", guard="", line=1, kind="switch"),
         GuiElement(param="volt", guard="", line=2, kind="switch")],
        scanned, envs2, ef2)
    expect("getParam-only twin does not satisfy parity; guarded setParam block violates",
           sorted((v.what.split('"')[1], tuple(v.envs)) for v in gviol4)
           == [("gps", ("no_feat",)), ("volt", ("has_feat", "no_feat"))])

    print("producer_match_lint.py self-test: " + ("ok" if ok else "FAILED"))
    return 0 if ok else 1


# ---------------------------------------------------------------------------
# Real-tree run
# ---------------------------------------------------------------------------

def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--verbose", action="store_true",
                     help="also list unchecked producers and warnings")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    drift = dir_table_drift()
    for msg in drift:
        print(f"FATAL: directory table out of date: {msg}")
    if drift:
        return 1

    per_env, all_names = load_baseline()
    envs = sorted(per_env)
    env_facts = {e: facts_for_env(e, per_env, all_names) for e in envs}
    env_platform = {e: platform_of(e, per_env) for e in envs}

    producers = scan_producers_tree()
    rungs = load_rungs()

    prod_violations, prod_warnings = check_producers(
        producers, rungs, envs, env_facts, env_platform)

    elements = scan_gui_elements(WEB_FUNCTIONS_SRC.read_text(errors="replace"))
    handlers = scan_handlers(WEB_SETUP_SRC.read_text(errors="replace"))
    gui_violations, gui_warnings = check_gui_parity(elements, handlers, envs, env_facts)

    violations = prod_violations + gui_violations
    warnings = prod_warnings + gui_warnings

    unchecked = [p for p in producers if p.unchecked_reason is not None]
    by_file = Counter(p.file for p in unchecked)

    if args.verbose and unchecked:
        print(f"{len(unchecked)} unchecked producer(s) (pass-through, not instantiable):")
        for p in unchecked:
            print(f"  {p.file}:{p.line}: {p.unchecked_reason}")
        print("  by file: " + ", ".join(f"{f} ({n})" for f, n in by_file.most_common()))

    if args.verbose and warnings:
        print(f"{len(warnings)} WARNING(s) (guard-unknown match only):")
        for w in warnings:
            print("  " + render_finding(w, envs))

    print(
        f"producer_match_lint: {len(producers)} producer(s) scanned "
        f"({len(unchecked)} unchecked), {len(rungs)} rung(s), "
        f"{len(elements)} GUI element(s), {len(handlers)} handler(s), "
        f"{len(envs)} env(s) -- {len(violations)} violation(s), "
        f"{len(warnings)} warning(s)"
    )

    if violations:
        print(f"producer_match_lint: {len(violations)} VIOLATION(s):")
        for v in violations:
            print("  " + render_finding(v, envs))
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
