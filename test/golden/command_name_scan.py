#!/usr/bin/env python3
"""String-scan the command-name ladder against every built firmware image.

Operator decision 2026-09-11 (`docs/BACKLOG.md` §3.8af): every bench node runs
an `INSTRUMENT_ENABLED` image, so the hardware command goldens (`test/golden`)
are captured on instrument builds. That means nothing has ever driven the
command set a real, shipping user gets -- the only evidence left is a static
scan of the string literals actually compiled into the `#if`-guarded ladder
in `src/command_functions.cpp`, checked against the ladder itself.

This tool does that:

1. Finds every `<env>/firmware.elf` under a build directory.
2. Extracts the command literals present in each image with a pure-Python
   `strings`-equivalent (see `_literal_present`/`iter_candidate_strings`
   below) -- no shelling out, `strings` is not guaranteed present and its
   flags/output differ by platform (BSD vs GNU).
3. Compares that set against `extract_commands.extract()` -- imported, not
   reimplemented, per the brief -- and classifies every ladder command as
   present or absent, and every absence as one of:

     - "as designed"   the enclosing `#if` guard evaluates false for this
                        image (e.g. `INSTRUMENT_ENABLED`-only commands on a
                        shipping build, or an ESP32-only command on nRF52)
     - "guard unknown"  the guard mentions a macro this tool has no way to
                        know (a board/feature flag such as `ENABLE_GPS` or
                        `BOARD_LED`) -- may or may not be a defect, needs a
                        human
     - "UNEXPLAINED"    the guard evaluates true (the command should compile
                        in) and the string is still missing -- this is the
                        headline finding a plain diff would have buried in
                        the same table as the two harmless buckets above

   A command present despite its guard evaluating false is reported too
   (bucket "present, guard says no") -- it should not happen, and if it does
   it means this tool's guard evaluation or platform facts are wrong, not
   that the finding is safe to ignore.
4. Writes a Markdown report (one row per env) to `--out`, with the
   unexplained absences and the guard-says-no anomalies spelled out per env,
   and prints a summary to stderr.

Only a handful of envs are ever built at once (`.pio/build` normally holds a
handful, not all 32) -- a missing env directory or a native (non-firmware)
env with no `firmware.elf` is not an error, just fewer rows.

    python3 test/golden/command_name_scan.py --build-dir .pio/build --out /tmp/scan.md
    python3 test/golden/command_name_scan.py --self-test

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_commands import Command, extract  # noqa: E402  (see sys.path above)

# ---------------------------------------------------------------------------
# Platform facts from the ELF header itself
# ---------------------------------------------------------------------------
#
# The guard text carries `defined(ESP32)` / `defined(NRF52_SERIES)` verbatim,
# but nothing in the .pio tree records which macros a given env's compiler
# invocation actually defined (no idedata.json/compile_commands.json is
# written by this project's build). Reading the ELF's own e_machine field
# sidesteps that entirely and needs no per-env board table: nRF52 boards are
# ARM Cortex-M4 (EM_ARM), every Espressif target this project ships (classic
# Xtensa ESP32/S2/S3 *and* the RISC-V C3/C6 parts) defines the Arduino core's
# blanket `ESP32` macro regardless of which of the two ISAs it compiles to.
_EM_ARM = 40
_EM_XTENSA = 94
_EM_RISCV = 243


@dataclass(frozen=True)
class Platform:
    esp32: Optional[bool]
    nrf52: Optional[bool]
    machine: int


def read_platform(elf_path: Path) -> Platform:
    """Classify an ELF's target from its e_ident/e_machine header fields.

    Returns (None, None) facts only if the file is too short to hold a
    header at all -- callers still get a Platform object, never an
    exception, so one unreadable file cannot take the whole scan down.
    """
    try:
        header = elf_path.read_bytes()[:20]
    except OSError:
        return Platform(None, None, -1)
    if len(header) < 20 or header[:4] != b"\x7fELF":
        return Platform(None, None, -1)
    endian = "<" if header[5] == 1 else ">"
    machine = struct.unpack_from(endian + "H", header, 18)[0]
    if machine in (_EM_XTENSA, _EM_RISCV):
        return Platform(esp32=True, nrf52=False, machine=machine)
    if machine == _EM_ARM:
        return Platform(esp32=False, nrf52=True, machine=machine)
    return Platform(esp32=None, nrf52=None, machine=machine)


# ---------------------------------------------------------------------------
# Three-valued guard evaluator
# ---------------------------------------------------------------------------
#
# `Command.guard` is the raw text `extract_commands._guard_stack` assembled:
# "&&"-joined #if/#ifdef/#ifndef conditions, an #elif's own text replacing
# its slot, and an #else wrapping the previous slot in "!(...)" -- all of it
# *recorded, not evaluated* by that module (its own docstring says so). Two
# of its documented shortcuts leak into the text this evaluator has to
# tolerate without raising:
#
#   - an "elif " prefix is noise here (the elif's exclusion of earlier arms
#     is not encoded, only its own condition text) -- dropped as a token;
#   - a condition that wrapped mid-macro (":5167-5168") is truncated at the
#     line's trailing backslash, leaving a dangling "||" -- the parser below
#     fails closed (returns UNKNOWN) rather than raising on that shape.
#
# Facts this tool actually knows: INSTRUMENT_ENABLED (an explicit whole-tree
# flag, see --instrument) and ESP32/NRF52_SERIES (from the ELF header, see
# above). Every other macro in a guard -- ENABLE_GPS, BOARD_LED, HEAP_TEST,
# DISABLE_NET_CONSOLE, the board-specific BOARD_* flags -- is UNKNOWN. Kleene
# (three-valued) logic lets that stay honest through compound expressions:
# `INSTRUMENT_ENABLED && defined(ENABLE_GPS)` on a shipping build is FALSE
# outright (AND short-circuits on a known-false operand) even though
# ENABLE_GPS itself is never resolved; `defined(ENABLE_GPS) or
# defined(BOARD_RAK4630)` with both unknown stays UNKNOWN, which is correct:
# this tool has no way to tell.

_TOKEN_RE = re.compile(
    r"\|\||&&|\bor\b|\band\b|\bnot\b|\bdefined\b|\belif\b|[!()]|"
    r"[A-Za-z_][A-Za-z0-9_]*|[0-9]+|."  # trailing "." mops up stray chars (e.g. a
    # continuation backslash) as an opaque, always-unknown atom instead of
    # raising -- see the module docstring above.
)


def _tokenize(guard: str) -> List[str]:
    return [t for t in _TOKEN_RE.findall(guard) if not t.isspace()]


def _kleene_and(a: Optional[bool], b: Optional[bool]) -> Optional[bool]:
    if a is False or b is False:
        return False
    if a is None or b is None:
        return None
    return True


def _kleene_or(a: Optional[bool], b: Optional[bool]) -> Optional[bool]:
    if a is True or b is True:
        return True
    if a is None or b is None:
        return None
    return False


def _kleene_not(a: Optional[bool]) -> Optional[bool]:
    return None if a is None else (not a)


class _GuardParser:
    """Recursive-descent evaluator over the token stream. Never raises --
    any malformed or unrecognised shape resolves to UNKNOWN (None), which is
    the only safe default for a guard this tool cannot fully understand."""

    def __init__(self, tokens: Sequence[str], facts: Dict[str, Optional[bool]]):
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
        except Exception:
            return None  # fail closed: never let a parse bug crash the scan

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
            return self._or()  # noise token; evaluate what follows it
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
            return self._facts.get(name or "", None)
        if t is None:
            return None
        if t.isdigit():
            return t != "0"
        if re.match(r"^[A-Za-z_]", t):
            return self._facts.get(t, None)
        return None  # a stray character token (e.g. a bare "\\")


def eval_guard(guard: str, facts: Dict[str, Optional[bool]]) -> Optional[bool]:
    """True/False if resolvable from `facts`, else None (unknown)."""
    if guard.strip() == "":
        return True  # unconditional: no enclosing #if at all
    return _GuardParser(_tokenize(guard), facts).parse()


def facts_for(platform: Platform, instrument: bool) -> Dict[str, Optional[bool]]:
    return {
        "INSTRUMENT_ENABLED": instrument,
        "ESP32": platform.esp32,
        "NRF52_SERIES": platform.nrf52,
    }


def expected_present(commands: List[Command], facts: Dict[str, Optional[bool]]) -> Optional[bool]:
    """OR the guard verdicts of every ladder site sharing one literal name.

    A name can have more than one site under different guards (the
    documented ESP32/nRF52 `udplog on` split) -- the string compiles into
    the image if *any* site's guard is true, so this is a plain Kleene OR
    fold, not "the first site wins".
    """
    verdict: Optional[bool] = False
    for cmd in commands:
        verdict = _kleene_or(verdict, eval_guard(cmd.guard, facts))
    return verdict


# ---------------------------------------------------------------------------
# strings-equivalent, done in Python
# ---------------------------------------------------------------------------

_PRINTABLE_RUN_RE = re.compile(rb"[\x20-\x7e]{3,}")


def _literal_present(data: bytes, literal: bytes) -> bool:
    """Is `literal`'s exact bytes present as a self-contained C string?

    Checks that the byte right after each occurrence is *not* itself
    printable ASCII -- i.e. the occurrence ends at a NUL (or at EOF).  That
    single check is enough to also catch GNU ld's SHF_MERGE|SHF_STRINGS
    suffix-sharing: a shorter literal that got folded into a longer constant
    always keeps its own tail up to the shared terminating NUL (the merge
    only ever shares a common *suffix*, never mangles the string's own
    ending), so it is never necessary to also check what precedes the match.
    """
    start = 0
    while True:
        idx = data.find(literal, start)
        if idx == -1:
            return False
        end = idx + len(literal)
        if end >= len(data) or data[end] not in range(0x20, 0x7F):
            return True
        start = idx + 1


# A command-like *unknown* candidate is judged against the shape every real
# ladder literal actually has (verified against all 305 current entries):
# lowercase letters, digits, spaces and hyphens only, 2-3 words, length 2-18.
# Restricting candidates to that same shape (rather than reporting every
# short printable run -- there are tens of thousands in a 30 MB ESP32 image)
# keeps the "unknown" bucket a short, human-triageable list instead of noise
# from libc/IDF/LVGL strings. Two sub-shapes are accepted, matching the two
# shapes the ladder itself uses: a multi-word phrase ("board led on"), or a
# single word with a trailing space marking an argument ("srvip ").
#
# Each word is required to be at least 2 characters: a first pass that
# allowed single-char words let through hundreds of two/three-letter
# fragments ("h a", "0 4", "x v") out of what turned out to be an embedded
# data table unrelated to any command -- not one real ladder name has a
# one-character word. This is a precision/recall trade-off tuned against the
# two build trees this tool was verified against, not a proof that every
# remaining match is a real command; see iter_candidate_strings below.
_CANDIDATE_RE = re.compile(
    r"^[a-z0-9][a-z0-9-]{1,17}(?: [a-z0-9][a-z0-9-]{1,17}){1,2}$|^[a-z][a-z0-9-]{1,17} $"
)


def iter_candidate_strings(data: bytes) -> Set[str]:
    """Standalone (non-merged) printable runs that look like a command name.

    Only whole printable runs are considered -- a candidate embedded in the
    middle of a larger run that also contains punctuation/uppercase (e.g. a
    log line) is not found this way. That is a known gap, not a bug: this
    bucket is a lead for a human, not a certificate of completeness, exactly
    like `extract_commands.guarded_duplicates()` one file over.
    """
    out: Set[str] = set()
    for m in _PRINTABLE_RUN_RE.finditer(data):
        try:
            text = m.group(0).decode("ascii")
        except UnicodeDecodeError:
            continue
        if len(text) <= 20 and _CANDIDATE_RE.match(text):
            out.add(text)
    return out


# ---------------------------------------------------------------------------
# Per-env analysis
# ---------------------------------------------------------------------------


@dataclass
class EnvReport:
    env: str
    elf_path: Path
    platform: Platform
    instrument: bool
    total: int = 0
    present: int = 0
    absent_as_designed: int = 0
    absent_guard_unknown: int = 0
    absent_unexplained: List[str] = field(default_factory=list)
    present_guard_says_no: List[str] = field(default_factory=list)
    unknown_candidates: List[str] = field(default_factory=list)

    @property
    def platform_label(self) -> str:
        if self.platform.esp32:
            return "ESP32"
        if self.platform.nrf52:
            return "NRF52"
        return f"unknown (e_machine={self.platform.machine})"

    @property
    def headline_ok(self) -> bool:
        return not self.absent_unexplained and not self.present_guard_says_no


def analyze_env(
    env: str,
    elf_path: Path,
    by_name: Dict[str, List[Command]],
    *,
    instrument: bool,
) -> EnvReport:
    platform = read_platform(elf_path)
    facts = facts_for(platform, instrument)
    data = elf_path.read_bytes()
    report = EnvReport(env=env, elf_path=elf_path, platform=platform, instrument=instrument)
    report.total = len(by_name)

    for name, sites in by_name.items():
        present = _literal_present(data, name.encode("ascii", "replace"))
        expected = expected_present(sites, facts)
        if present:
            report.present += 1
            if expected is False:
                report.present_guard_says_no.append(name)
        else:
            if expected is True:
                report.absent_unexplained.append(name)
            elif expected is False:
                report.absent_as_designed += 1
            else:
                report.absent_guard_unknown += 1

    known_literals = set(by_name.keys())
    for candidate in sorted(iter_candidate_strings(data)):
        if candidate in known_literals:
            continue
        if any(candidate.endswith(name) for name in known_literals):
            # A shorter known literal sharing this candidate's terminating
            # NUL via ld's suffix merge (see _literal_present) -- not new.
            continue
        report.unknown_candidates.append(candidate)

    return report


def find_envs(build_dir: Path) -> List[Tuple[str, Path]]:
    if not build_dir.is_dir():
        return []
    out = []
    for env_dir in sorted(build_dir.iterdir()):
        elf = env_dir / "firmware.elf"
        if elf.is_file():
            out.append((env_dir.name, elf))
    return out


# ---------------------------------------------------------------------------
# Report rendering
# ---------------------------------------------------------------------------


def render_markdown(reports: List[EnvReport]) -> str:
    lines = [
        "# Command-name string scan",
        "",
        "One row per built `<env>/firmware.elf`. Absences split into three "
        "buckets: **as designed** (the `#if` guard says this platform/build "
        "should not have it), **guard unknown** (the guard names a macro "
        "this tool cannot evaluate -- board/feature flags), and "
        "**UNEXPLAINED** (the guard says the command should be there and it "
        "is not -- the actual finding). `present, guard says no` is an "
        "anomaly bucket: it should always read 0.",
        "",
        "Two known sources of noise, both a property of scanning raw bytes "
        "rather than the compiler's own view, not a bug in this tool: (1) "
        "`present, guard says no` can fire on a short, generic literal (e.g. "
        "`heap`, `ota-update`) that is simply the tail of a longer, unrelated "
        "string elsewhere in the image -- a byte scan cannot tell that "
        "apart from a real contradiction, so treat that bucket as a "
        "five-second eyeball check, not an alarm. (2) a guard captured "
        "across more than one nested `#if` level "
        "(`extract_commands._guard_stack` joins levels with a flat `&&`, "
        "without re-parenthesizing a level whose own condition contains a "
        "top-level `||`) can misparse a command's *true* precedence -- "
        "`--srvip` is the known case, see command_name_scan.py's module "
        "docstring -- which can land an actually-correctly-guarded absence "
        "in the UNEXPLAINED bucket. Both are worth a quick manual check "
        "before treating a nonzero count in either bucket as a real defect.",
        "",
        "| env | platform | instrument | total | present | absent (designed) "
        "| absent (guard unknown) | **absent (UNEXPLAINED)** | present, guard "
        "says no | unknown candidates |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for r in reports:
        lines.append(
            f"| {r.env} | {r.platform_label} | {'yes' if r.instrument else 'no'} "
            f"| {r.total} | {r.present} | {r.absent_as_designed} "
            f"| {r.absent_guard_unknown} | **{len(r.absent_unexplained)}** "
            f"| {len(r.present_guard_says_no)} | {len(r.unknown_candidates)} |"
        )

    for r in reports:
        if not r.absent_unexplained and not r.present_guard_says_no and not r.unknown_candidates:
            continue
        lines += ["", f"## {r.env}", ""]
        if r.absent_unexplained:
            lines.append(
                f"**Absent and UNEXPLAINED ({len(r.absent_unexplained)})** -- guard says "
                "present, string is missing:"
            )
            lines += [f"- `{n}`" for n in sorted(r.absent_unexplained)]
            lines.append("")
        if r.present_guard_says_no:
            lines.append(
                f"**Present despite guard evaluating false ({len(r.present_guard_says_no)})** "
                "-- this tool's guard facts are wrong, or something is:"
            )
            lines += [f"- `{n}`" for n in sorted(r.present_guard_says_no)]
            lines.append("")
        if r.unknown_candidates:
            shown = r.unknown_candidates[:100]
            lines.append(
                f"**Command-like strings not in the ladder ({len(r.unknown_candidates)}, "
                "heuristic -- needs human triage, see module docstring)**:"
            )
            lines += [f"- `{n}`" for n in shown]
            if len(r.unknown_candidates) > len(shown):
                lines.append(f"- ... {len(r.unknown_candidates) - len(shown)} more suppressed")
            lines.append("")

    return "\n".join(lines) + "\n"


def print_summary(reports: List[EnvReport]) -> None:
    if not reports:
        print("command_name_scan: no <env>/firmware.elf found -- nothing to report", file=sys.stderr)
        return
    for r in reports:
        flag = "OK" if r.headline_ok else "NEEDS REVIEW"
        print(
            f"[{flag}] {r.env} ({r.platform_label}, instrument={r.instrument}): "
            f"{r.present}/{r.total} present, {r.absent_as_designed} absent-as-designed, "
            f"{r.absent_guard_unknown} absent-guard-unknown, "
            f"{len(r.absent_unexplained)} UNEXPLAINED, "
            f"{len(r.present_guard_says_no)} present-guard-says-no, "
            f"{len(r.unknown_candidates)} unknown candidates",
            file=sys.stderr,
        )


# ---------------------------------------------------------------------------
# Self-test (runs with no build present)
# ---------------------------------------------------------------------------


def _self_test() -> int:
    failures = 0

    def check(cond: bool, msg: str) -> None:
        nonlocal failures
        if not cond:
            failures += 1
            print(f"FAIL: {msg}", file=sys.stderr)

    # --- guard evaluator -----------------------------------------------
    check(eval_guard("", {}) is True, "empty guard is unconditional")
    check(
        eval_guard("INSTRUMENT_ENABLED", {"INSTRUMENT_ENABLED": False}) is False,
        "bare macro false",
    )
    check(
        eval_guard("defined(ESP32)", {"ESP32": True}) is True,
        "defined() true",
    )
    check(
        eval_guard("!defined BOARD_T_DECK_PRO", {}) is None,
        "unknown macro under negation stays unknown",
    )
    check(
        eval_guard(
            "INSTRUMENT_ENABLED && defined(ESP32)",
            {"INSTRUMENT_ENABLED": False, "ESP32": True},
        )
        is False,
        "AND short-circuits on a known-false operand even with an unknown/true peer",
    )
    check(
        eval_guard(
            "defined (ENABLE_GPS) or defined(BOARD_RAK4630)", {"ESP32": True}
        )
        is None,
        "OR of two unknowns stays unknown",
    )
    check(
        eval_guard(
            "defined(SX126X_V3) || defined(SX1262_E290) || defined(SX1262X) || defined(SX126X) || \\",
            {},
        )
        is None,
        "a guard truncated at a line-continuation backslash fails closed, not raises",
    )
    check(
        eval_guard("elif defined(ESP32)", {"ESP32": False}) is False,
        "a leading 'elif' token is dropped, not treated as an unknown atom",
    )
    check(
        eval_guard("!(elif defined(ESP32))", {"ESP32": True}) is False,
        "#else wrapping an #elif's recorded text in !(...) still parses",
    )

    # --- expected_present across guarded duplicates ----------------------
    dual = [
        Command(0, 1, "udplog on", "INSTRUMENT_ENABLED && defined(ESP32)"),
        Command(1, 2, "udplog on", "defined(NRF52_SERIES)"),
    ]
    check(
        expected_present(dual, {"INSTRUMENT_ENABLED": False, "ESP32": True, "NRF52_SERIES": True})
        is True,
        "guarded-duplicate OR: one site true is enough",
    )
    check(
        expected_present(dual, {"INSTRUMENT_ENABLED": False, "ESP32": True, "NRF52_SERIES": False})
        is False,
        "guarded-duplicate OR: both sites false",
    )

    # --- literal presence, including the suffix-merge case ---------------
    check(_literal_present(b"xxx\x00srvip \x00yyy", b"srvip ") is True, "plain NUL-terminated literal")
    check(_literal_present(b"xxxsrvip\x00", b"srvip ") is False, "no trailing space means no match")
    check(
        _literal_present(b"headersuffix \x00", b"suffix ") is True,
        "suffix-merged literal (shared terminating NUL) is found",
    )
    check(_literal_present(b"srvipx\x00", b"srvip") is False, "not followed by a NUL/non-printable -> absent")
    check(_literal_present(b"nothing here", b"srvip ") is False, "genuinely absent")

    # --- candidate string shape filter ------------------------------------
    cands = iter_candidate_strings(b"\x00spiffs reset\x00Error: bad thing (42)\x00srvip \x00ab\x00")
    check("spiffs reset" in cands, "multi-word candidate recognised")
    check("srvip " in cands, "single-word-with-trailing-space candidate recognised")
    check("ab" not in cands, "two-char single word without trailing space is not a candidate")
    check(not any(" " not in c and not c.endswith(" ") for c in cands), "no bare single words slip through")

    # --- ELF platform classification --------------------------------------
    arm_header = b"\x7fELF" + bytes([1, 1, 1]) + b"\x00" * 9 + struct.pack("<HH", 2, 40)
    xtensa_header = b"\x7fELF" + bytes([1, 1, 1]) + b"\x00" * 9 + struct.pack("<HH", 2, 94)
    import tempfile

    with tempfile.TemporaryDirectory() as td:
        arm_path = Path(td) / "arm.elf"
        arm_path.write_bytes(arm_header)
        xt_path = Path(td) / "xt.elf"
        xt_path.write_bytes(xtensa_header)
        short_path = Path(td) / "short.elf"
        short_path.write_bytes(b"\x7fELF\x01\x01")

        p_arm = read_platform(arm_path)
        check(p_arm.nrf52 is True and p_arm.esp32 is False, "EM_ARM classified as nRF52")
        p_xt = read_platform(xt_path)
        check(p_xt.esp32 is True and p_xt.nrf52 is False, "EM_XTENSA classified as ESP32")
        p_short = read_platform(short_path)
        check(p_short.esp32 is None and p_short.nrf52 is None, "truncated header -> unknown, not a crash")

    # --- find_envs on a partial / absent tree must not raise --------------
    with tempfile.TemporaryDirectory() as td:
        base = Path(td)
        (base / "native").mkdir()  # no firmware.elf -- native envs don't produce one
        (base / "wiscore_rak4631").mkdir()
        (base / "wiscore_rak4631" / "firmware.elf").write_bytes(arm_header)
        envs = find_envs(base)
        check(envs == [("wiscore_rak4631", base / "wiscore_rak4631" / "firmware.elf")],
              "native (no ELF) skipped, real env found")
        check(find_envs(base / "does-not-exist") == [], "missing build dir returns no envs, not an error")

        # end-to-end smoke test of analyze_env against a tiny synthetic image
        by_name = {
            "srvip ": [Command(0, 1, "srvip ", "INSTRUMENT_ENABLED")],
            "reboot": [Command(1, 2, "reboot", "")],
            "udplog on": [
                Command(2, 3, "udplog on", "INSTRUMENT_ENABLED && defined(ESP32)"),
                Command(3, 4, "udplog on", "defined(NRF52_SERIES)"),
            ],
        }
        image = base / "wiscore_rak4631" / "firmware.elf"
        # nRF52 (ARM) header + "reboot\0" and "udplog on\0" present, "srvip " absent
        image.write_bytes(arm_header + b"reboot\x00udplog on\x00")
        rep = analyze_env("wiscore_rak4631", image, by_name, instrument=False)
        check(rep.present == 2, f"expected 2 present, got {rep.present}")
        check(rep.absent_as_designed == 1, "srvip is absent-as-designed (INSTRUMENT_ENABLED false)")
        check(rep.absent_unexplained == [], "nothing unexplained in the smoke test")
        check(rep.present_guard_says_no == [], "no anomaly in the smoke test")

        # Same image, but pretend a command with an unconditional guard is
        # missing -- must land in absent_unexplained, the headline bucket.
        by_name_missing = dict(by_name)
        by_name_missing["missing"] = [Command(4, 5, "missing", "")]
        rep2 = analyze_env("wiscore_rak4631", image, by_name_missing, instrument=False)
        check(rep2.absent_unexplained == ["missing"], "unconditional + absent -> UNEXPLAINED")

    print(
        f"command_name_scan.py self-test: {'ok' if failures == 0 else f'{failures} failure(s)'}",
        file=sys.stderr,
    )
    return 1 if failures else 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build-dir", type=Path, default=Path(".pio/build"))
    ap.add_argument("--out", type=Path, default=None, help="write the Markdown report here")
    ap.add_argument(
        "--instrument",
        choices=["auto", "yes", "no"],
        default="auto",
        help="whether every image in --build-dir was built with INSTRUMENT_ENABLED=1; "
        "'auto' (default) infers it from the build dir name containing 'instr' -- "
        "there is no per-env record of this in the .pio tree to read instead",
    )
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()

    if args.instrument == "auto":
        instrument = "instr" in args.build_dir.name.lower()
    else:
        instrument = args.instrument == "yes"

    commands = extract()
    by_name: Dict[str, List[Command]] = {}
    for cmd in commands:
        by_name.setdefault(cmd.name, []).append(cmd)

    envs = find_envs(args.build_dir)
    reports = [
        analyze_env(env, elf, by_name, instrument=instrument) for env, elf in envs
    ]

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(render_markdown(reports))
        print(f"report for {len(reports)} env(s) written to {args.out}", file=sys.stderr)

    print_summary(reports)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
