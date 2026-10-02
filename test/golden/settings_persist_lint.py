#!/usr/bin/env python3
"""Completeness gate for ESP32 settings persistence (DR-13, `both-wrong`).

Two platforms maintain the same "which settings fields exist" mapping twice
by hand:

  src/config_json.h     the `X()` table (CFG_FIELD_LIST) -- one row per field
                         that the JSON export/import and (per D1-04) the BLE
                         config path know about.
  src/esp32/esp32_flash.cpp
                         266 hand-written `preferences.put*`/`get*` calls --
                         the ESP32's *actual* NVS persistence, maintained
                         completely separately from the table above.

DR-13's finding is that this duplication fails SOFT on the ESP32 side: a
field added to the struct and to the `X()` table but forgotten in
`esp32_flash.cpp` produces no compile error and no runtime error. The setting
can be read and written all day (it lives in the struct, the web UI shows
it, the JSON export prints it) -- it simply does not survive a reboot,
because nothing ever calls `preferences.put*()` for it. Nobody notices until
someone power-cycles a node and finds their change gone.

`M3-01`'s plan is to rewrite this exact code (schema-driven persistence,
D1-04). This gate has to stand BEFORE that migration starts, not after --
otherwise the rewrite is building on a mapping nobody has actually checked
end to end. It is a static gate over the CURRENT tree: three checks, run by
parsing `src/config_json.h` and `src/esp32/esp32_flash.cpp` as text (this
file never compiles firmware and must work with a native `python3`).

SCHEMA-DRIVEN MODE (D1-04 W3 Task 1, landed 2026-09-13): the migration this
docstring says the gate must stand BEFORE has now landed. esp32_flash.cpp's
load/save no longer contain the 266 literal `preferences.put*`/`get*("literal
key", ...)` calls these three checks parse -- they walk
`settings_schema::fields()` generically instead, dispatching on
`FieldDescriptor::type` against a variable key, so there is categorically
nothing left for `parse_pref_calls()`'s literal-string regexes to find. That
is not a new instance of DR-13 -- it is the fix DR-13 called for, just
authored as one data table plus one dispatch function instead of one call
site per field, so the exact drift these checks were built to catch (a field
present in the table with no matching call) is now structurally impossible:
load and save are the SAME loop over the SAME array.
`check()` detects this (two-plus occurrences of the literal string
`"settings_schema::fields()"` in esp32_flash.cpp -- see `is_schema_driven()`)
and returns a reduced, honest result instead of running checks 1-3 against
call sites that no longer exist: no FATAL from the now-genuinely-empty
put/get call lists, no wall of false "no preferences.put* call" findings, and
a note pointing at `settings_schema_lint.py` as the tool that now does the
real completeness check (it parses the schema table directly, not this
file's call sites). `EXPLAINED_EXCLUSIONS`, `parse_x_table_esp32()`,
`parse_esp32_struct_members()` and the cross-checks below are all left fully
intact and still exercised by `--self-test` against synthetic legacy-shaped
fixtures -- this file stays the correct instrument for a *hand-written*
persistence layer, on the CURRENT tree or a future one that reverts to it;
it simply steps out of the way once persistence is schema-driven instead.

THE THREE CHECKS
-----------------

  1. MISSING PERSIST -- every field in the `X()` table that has a matching
     ESP32 struct member (`src/meshcom_settings.h`'s COMMON + ESP32 + TDECK
     X-macro lists) must have a `preferences.put*()` call for its NVS key in
     `esp32_flash.cpp`. A
     field found in the table but never put()'d is exactly DR-13's silent
     bug: the user can set it, the node forgets it.

  2. STALE WRITE TARGET -- every `preferences.put*()` call in
     `esp32_flash.cpp` must write a value that traces back to a REAL member
     of `struct s_meshcom_settings`. This is resolved from the call's own
     source code, two shapes only (both are exhaustively what the file
     uses): `preferences.putX("key", meshcom_settings.member)` directly, or
     the `strVar = meshcom_settings.member; ...; preferences.putString("key",
     strVar);` two-step idiom for `String`-typed fields. If a put call's
     value cannot be traced to any struct member at all, that is reported
     separately (UNRESOLVED) rather than silently accepted or silently
     flagged -- see `parse_pref_calls()`. A rename that leaves the *key
     string* pointing at a member that no longer exists would show up here;
     the current file has none (every put call resolves and every resolved
     member exists), so this check currently reports zero, which is a
     genuine result, not a shortfall of the check.

  3. PUT/GET SYMMETRY -- collect the NVS key of every `put*()` and every
     `get*()` call in the file (both functions, irrespective of the
     `#if defined(BOARD_T_DECK...)` guards some of them sit inside -- a
     guarded call is still a real call on the boards where the macro is
     set, same convention `parse_esp32_struct_members()` below uses for the
     struct itself). A key on only one side is two DIFFERENT defects, reported
     with different wording because they fail differently in the field:
       - WRITE-ONLY (put, no get): the value is saved but `init_flash()`
         never loads it back -- every boot re-reads whatever earlier
         default `init_flash()` assigned, silently discarding the save.
       - READ-ONLY (get, no put): `init_flash()` reads a key that
         `save_settings()` never writes -- the NVS entry can only come
         from something outside this file (or never exists), so the
         "restore" is a permanent default with an NVS lookup bolted on.

EXPLAINED EXCLUSIONS -- how a documented exception is told apart from a gap
----------------------------------------------------------------------------

`esp32_flash.cpp` persists more than the `X()` table exports, on purpose,
and `config_json.h:127-137` / `config_json.h:362-367` say so in as many
words: flash/firmware bookkeeping (`node_fversion`, `node_mversion`,
`node_fwversion`, `node_cleanflash`/`node_cflash`), the T-Deck-only UI block
(`node_map` .. `node_wifion`, 13 fields), the running `node_msgid`/
`node_ackid` counters, and six last-sensor-reading fields (`node_temp`,
`node_hum`, `node_press`, `node_temp2`, `node_gas_res`/`node_gas`,
`node_co2`). None of these are `X()` table rows, so check 1 never iterates
them in the first place -- and every one of them DOES have both a get and a
put call today, so check 3 does not currently flag any of them either. The
exclusion list below (`EXPLAINED_EXCLUSIONS`, transcribed by hand from those
two comment blocks, cited by name and line range) exists as a matching NET,
not a fig leaf: if a future edit ever put one of these fields out of
put/get balance, or if some future `X()` row happened to name one of them,
this script would otherwise report a violation. Instead, any finding whose
key OR resolved member is in `EXPLAINED_EXCLUSIONS` is downgraded from a
`VIOLATION` line to an `EXCLUDED (explained)` line quoting the reason and
citation, and is NOT counted toward the exit code. Anything that is NOT in
that dict is, by definition, unexplained -- and stays a hard violation in
every case, because a silent, undocumented gap is precisely the failure
mode this gate exists to catch. `--self-test` proves the distinction
directly (see the "explained exclusion" cases): the identical shape of
finding is a VIOLATION with an empty exclusion table and an EXCLUDED line
with a populated one, nothing else about the detection logic changes.

CROSS-CHECK AGAINST THE D1-04 TRIAGE
--------------------------------------

`docs/d1-04-settings-field-triage-20260912.md` hand-classified all 147
settings fields and states, in its own "Method" section, the exact naive
regex it ran over the whole of `config_json.h` (both platform branches,
undistinguished) to get "89 unique members" in the `X()` table, and in its
outcome table "disagreement (a): 0" -- no `X()` field with an ESP32 member
lacks an ESP32 NVS key. This script re-runs that literal regex (see
`naive_doc_member_count()`) as a direct check of that one number, and its
own check 1 is an independent re-derivation of disagreement (a) via a
completely different code path (real `#ifdef ESP32` macro-body extraction
plus a real struct parse, not a whole-file regex). `main()` prints both
comparisons and says AGREE or DISAGREE rather than assuming the document is
right; if the counts do not match, that is reported as a discrepancy to be
resolved by a human, and this script trusts its own extraction, not the
document, for its exit code -- the document was produced by hand-reading
and grep, this script by parsing the same files freshly on every run.

  python3 test/golden/settings_persist_lint.py --self-test
  python3 test/golden/settings_persist_lint.py

Exit 0 when clean, 1 on any violation. Exit 1 (FATAL, never a silent clean
pass) when `X()` table rows, ESP32 struct members, or `preferences.*` calls
cannot be extracted at all -- a pattern that has stopped matching must never
look like a clean tree.
"""
import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

REPO = Path(__file__).resolve().parents[2]
# The X() table moved from config_json.cpp into config_json.h on 2026-09-12
# (W3 step 2) so that settings_schema.cpp can expand the same single list
# instead of copying it. The list itself is byte-for-byte unchanged; only
# the file it lives in moved, so this parser follows the path and nothing
# else about it changes.
CONFIG_JSON_CPP = "src/config_json.h"
# D1-04 W3 struct merge (2026-09-14): struct s_meshcom_settings moved out of
# src/esp32/esp32_flash.h (and src/nrf52/WisBlock-API.h) into ONE header,
# generated from three X-macro lists (MESHCOM_SETTINGS_MEMBERS_COMMON/_ESP32/
# _TDECK, each `M(type, name, default)` / `A(type, name, bounds, init...)`
# row). parse_esp32_struct_members() below parses those rows instead of a
# `struct { ... }` body -- see its own docstring.
MESHCOM_SETTINGS_H = "src/meshcom_settings.h"
ESP32_FLASH_CPP = "src/esp32/esp32_flash.cpp"
STRUCT_NAME = "s_meshcom_settings"
MESHCOM_SETTINGS_MEMBER_MACROS = (
    "MESHCOM_SETTINGS_MEMBERS_COMMON",
    "MESHCOM_SETTINGS_MEMBERS_ESP32",
    "MESHCOM_SETTINGS_MEMBERS_TDECK",
)

# ---------------------------------------------------------------------------
# Explained exclusions -- config_json.h:127-137, config_json.h:362-367
# ---------------------------------------------------------------------------
# Keyed by every spelling (NVS key AND struct member, where they differ) a
# finding could name, so a lookup by either always succeeds. See the module
# docstring's "EXPLAINED EXCLUSIONS" section for what this is for and why it
# currently matches nothing in the real tree (every one of these fields is
# already put/get-balanced today; this is a net for future drift, not a
# report of a current gap).
_BOOKKEEPING = "flash/firmware-layout bookkeeping (config_json.h:128-130): " \
    "must persist across a reboot to detect a stale layout, must never be " \
    "importable, so it is deliberately kept out of the X() table"
_CLEANFLASH = "one-shot 'wipe at next boot' trigger (config_json.h:131-132); " \
    "importing a stored 1 would erase the very restore that set it"
_TDECK = "T-Deck-only device UI/behaviour preference (config_json.h:133-134), " \
    "not portable configuration, deliberately kept out of the X() table"
_CLOCK_MARKER = "clock or flash-integrity marker (config_json.h:135-136), " \
    "no NVS key on the ESP32 by design"
_COUNTER = "running id counter (config_json.h:362-365); persisted so ids " \
    "stay monotone across reboot, never exported (would rewind and collide)"
_SENSOR = "last sensor reading (config_json.h:366-367), explicitly 'not " \
    "configuration' -- persisted for display continuity only"

EXPLAINED_EXCLUSIONS: Dict[str, str] = {}
for _names, _reason in [
    (("node_fversion",), _BOOKKEEPING),
    (("node_mversion",), _BOOKKEEPING),
    (("node_fwversion",), _BOOKKEEPING),
    (("node_cleanflash", "node_cflash"), _CLEANFLASH),
    (("node_map",), _TDECK),
    (("node_audio_start", "node_audstart"), _TDECK),
    (("node_audio_msg", "node_audmsg"), _TDECK),
    (("node_keyboardlock", "node_kblock"), _TDECK),
    (("node_backlightlock", "node_bllock"), _TDECK),
    (("node_kbllightlock", "node_kllock"), _TDECK),
    (("node_modus",), _TDECK),
    (("node_mute",), _TDECK),
    (("node_persist_to_flash", "node_perflash"), _TDECK),
    (("node_persist_to_sd", "node_persd"), _TDECK),
    (("node_immediate_save", "node_immsave"), _TDECK),
    (("node_kbl_sync", "node_kblsync"), _TDECK),
    (("node_wifion",), _TDECK),
    (("node_date_year",), _CLOCK_MARKER),
    (("node_date_month",), _CLOCK_MARKER),
    (("node_date_day",), _CLOCK_MARKER),
    (("node_date_hour",), _CLOCK_MARKER),
    (("node_date_minute",), _CLOCK_MARKER),
    (("node_date_second",), _CLOCK_MARKER),
    (("node_date_hundredths",), _CLOCK_MARKER),
    (("node_age",), _CLOCK_MARKER),
    (("node_device_eui",), _CLOCK_MARKER),
    (("valid_mark_1",), _CLOCK_MARKER),
    (("valid_mark_2",), _CLOCK_MARKER),
    (("node_msgid",), _COUNTER),
    (("node_ackid",), _COUNTER),
    (("node_temp",), _SENSOR),
    (("node_hum",), _SENSOR),
    (("node_press",), _SENSOR),
    (("node_temp2",), _SENSOR),
    (("node_gas_res", "node_gas"), _SENSOR),
    (("node_co2",), _SENSOR),
]:
    for _n in _names:
        EXPLAINED_EXCLUSIONS[_n] = _reason
del _names, _reason, _n


def explained(*names: Optional[str]) -> Optional[str]:
    """First EXPLAINED_EXCLUSIONS hit among `names` (key and/or member), or
    None if none of them is a documented exception."""
    for n in names:
        if n and n in EXPLAINED_EXCLUSIONS:
            return EXPLAINED_EXCLUSIONS[n]
    return None


# ---------------------------------------------------------------------------
# Extraction: the X() table, ESP32 branch only
# ---------------------------------------------------------------------------

ROW_RE = re.compile(
    r'X\(\s*"([^"]+)"\s*,\s*(CFG_[A-Z0-9_]+)\s*,\s*'
    r'([A-Za-z_][A-Za-z0-9_]*)(?:\s*\[[^\]]*\])?\s*,')


@dataclass(frozen=True)
class XRow:
    key: str
    type_: str
    member: str


def _extract_backslash_macro(text: str, define_pattern: str) -> Optional[str]:
    """Text of a backslash-continued `#define NAME(...)` body: the line
    matching `define_pattern` plus every following line up to and including
    the first one that does NOT end in a line-continuing `\\`."""
    lines = text.splitlines()
    pat = re.compile(define_pattern)
    start = next((i for i, l in enumerate(lines) if pat.search(l)), None)
    if start is None:
        return None
    out = [lines[start]]
    i = start
    while lines[i].rstrip().endswith("\\"):
        i += 1
        if i >= len(lines):
            break
        out.append(lines[i])
    return "\n".join(out)


def _extract_esp32_platform_block(text: str) -> str:
    """Body of `CFG_FIELD_LIST_PLATFORM(X)` as `#define`d under `#ifdef
    ESP32` (never the `#else` / nRF52-only branch). Search starts AFTER the
    macro's own *usage* line inside CFG_FIELD_LIST(X) (the first occurrence
    of the literal string), so the unrelated outer `#ifdef ESP32` include
    guard near the top of the file cannot be matched instead."""
    marker = "CFG_FIELD_LIST_PLATFORM(X)"
    idx = text.find(marker)
    if idx == -1:
        return ""
    rest = text[idx + len(marker):]
    m = re.search(r"#ifdef\s+ESP32\s*\n(.*?)\n\s*#else", rest, re.S)
    return m.group(1) if m else ""


def parse_x_table_esp32(config_json_cpp_text: str) -> List[XRow]:
    """Every `X(...)` row that is compiled when ESP32 is defined: the common
    CFG_FIELD_LIST(X) body plus the ESP32 branch of CFG_FIELD_LIST_PLATFORM
    (never the nRF52-only branch, e.g. auto_join / send_repeat_time, which
    have no ESP32 struct member to persist in the first place)."""
    common = _extract_backslash_macro(
        config_json_cpp_text, r'^\s*#define\s+CFG_FIELD_LIST\(X\)')
    if common is None:
        return []
    platform = _extract_esp32_platform_block(config_json_cpp_text)
    combined = common + "\n" + platform
    return [XRow(key=m.group(1), type_=m.group(2), member=m.group(3))
            for m in ROW_RE.finditer(combined)]


def naive_doc_member_count(config_json_cpp_text: str) -> Tuple[int, int, int]:
    """Re-run of the exact regex docs/d1-04-...-triage-20260912.md's Method
    section names, over the WHOLE file (both platform branches, not
    distinguished by preprocessor) -- a direct check of its stated '89
    unique members' figure. Returns (total rows, unique members, unique
    keys)."""
    rows = re.findall(
        r'X\(\s*"([^"]+)",\s*[A-Z0-9_]+,\s*([A-Za-z0-9_]+)(?:\[[0-9]+\])?,',
        config_json_cpp_text)
    return len(rows), len({m for _, m in rows}), len({k for k, _ in rows})


# ---------------------------------------------------------------------------
# Extraction: the ESP32 struct's member names (src/meshcom_settings.h,
# D1-04 W3 struct merge)
# ---------------------------------------------------------------------------
#
# Before the merge this parsed a real `struct { ... }` body out of
# src/esp32/esp32_flash.h. Since then the struct is generated once, from
# three X-macro lists (MESHCOM_SETTINGS_MEMBERS_COMMON/_ESP32/_TDECK, each a
# `M(type, name, default)` / `A(type, name, bounds, init...)` row -- see
# meshcom_settings.h's own "Row shapes" comment) -- there is no struct body
# left to parse as C++ field declarations.


def _strip_line_comments(text: str) -> str:
    return "\n".join(l.split("//", 1)[0] for l in text.splitlines())


def _strip_c_comments(text: str) -> str:
    """`_strip_line_comments` plus `/* ... */` block comments -- meshcom_settings.h's
    X-macro rows carry plenty of those (e.g. `/* flash marker, legacy blob
    only */`), and a `(`/`)` inside one would otherwise corrupt
    `extract_calls()`'s paren-depth scan. Safe here: no string literal in
    this file's row shape contains `//` or `/*`."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return _strip_line_comments(text)


def _split_top_level_args(s: str) -> List[str]:
    """Split `s` on top-level commas, respecting (), [] and "..." nesting --
    so a nested/bracketed argument (a bounds `[16][16]`, a braced
    initialiser `{0x00, 0x0D, ...}` -- note braces do NOT need their own
    case: the C preprocessor's own argument splitting only tracks `()`
    nesting, so a top-level comma inside `{...}` really is a separate
    preprocessor argument, exactly why meshcom_settings.h's own A(...)
    macro is variadic -- `__VA_ARGS__` is what reassembles them, not this
    splitter) never gets cut into two arguments here just because it stays
    inside `[` `]`."""
    args: List[str] = []
    depth = 0
    in_str = False
    cur: List[str] = []
    i = 0
    while i < len(s):
        c = s[i]
        if in_str:
            cur.append(c)
            if c == "\\" and i + 1 < len(s):
                cur.append(s[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            cur.append(c)
        elif c in "([":
            depth += 1
            cur.append(c)
        elif c in ")]":
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if cur:
        args.append("".join(cur).strip())
    return args


def extract_calls(text: str, call_name: str) -> List[List[str]]:
    """Every `<call_name>(...)` invocation in `text`, as its raw, comma-split
    argument strings. Matched only at a word boundary
    (`(?<![A-Za-z0-9_])<call_name>\\(`) so this can never trigger inside a
    longer identifier (e.g. `call_name="M"` must not match inside
    `MC_SETTINGS_M(`). An unbalanced call (should never happen in real
    source) is skipped, not guessed at."""
    calls: List[List[str]] = []
    pat = re.compile(r'(?<![A-Za-z0-9_])' + re.escape(call_name) + r'\(')
    for m in pat.finditer(text):
        start = m.end()
        depth = 1
        i = start
        in_str = False
        while i < len(text) and depth > 0:
            c = text[i]
            if in_str:
                if c == "\\":
                    i += 2
                    continue
                if c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        if depth != 0:
            continue
        inner = text[start:i - 1]
        calls.append(_split_top_level_args(inner))
    return calls


def _all_backslash_macro_bodies(text: str, macro_name: str) -> List[str]:
    """Body text of every `#define <macro_name>(...)` backslash-continued
    block in `text`, in file order. The parameter list is matched loosely
    (`\\([^)]*\\)`, no fixed arity) so this covers meshcom_settings.h's
    two-parameter `MESHCOM_SETTINGS_MEMBERS_COMMON(M, A)` shape (an empty
    `#else`-branch redefinition with no parameters used, e.g.
    `MESHCOM_SETTINGS_MEMBERS_TDECK(M, A)` with no body at all, is still
    found -- it just contributes zero M(...)/A(...) rows)."""
    lines = text.splitlines()
    pat = re.compile(r'^\s*#define\s+' + re.escape(macro_name) + r'\([^)]*\)')
    results: List[str] = []
    i = 0
    while i < len(lines):
        if pat.search(lines[i]):
            block = [lines[i]]
            while block[-1].rstrip().endswith("\\"):
                i += 1
                if i >= len(lines):
                    break
                block.append(lines[i])
            results.append("\n".join(block))
        i += 1
    return results


def parse_esp32_struct_members(meshcom_settings_h_text: str) -> Set[str]:
    """Member names from every `M(...)`/`A(...)` row across ALL occurrences
    (populated or empty) of MESHCOM_SETTINGS_MEMBERS_COMMON/_ESP32/_TDECK in
    `meshcom_settings_h_text` -- meshcom_settings.h's COMMON + ESP32 + TDECK
    X-macro lists, i.e. every member the widest (ESP32 + T-Deck) build of
    struct s_meshcom_settings has, matching what a pre-merge
    `struct { ... }` body parse against esp32_flash.h used to return. Name
    is always the second argument for both row shapes (`M(type, name,
    default)` / `A(type, name, bounds, ...)`), so one pass over both call
    names is enough -- no need to track which `#ifdef`/`#if defined(...)`
    branch a body came from: whichever branch is non-empty is the one that
    matters, the other contributes no rows either way."""
    members: Set[str] = set()
    for macro_name in MESHCOM_SETTINGS_MEMBER_MACROS:
        for body in _all_backslash_macro_bodies(meshcom_settings_h_text, macro_name):
            body = _strip_c_comments(body)
            for call in extract_calls(body, "M"):
                if len(call) >= 2:
                    members.add(call[1].strip())
            for call in extract_calls(body, "A"):
                if len(call) >= 2:
                    members.add(call[1].strip())
    return members


# ---------------------------------------------------------------------------
# Extraction: preferences.get*/put* calls in esp32_flash.cpp
# ---------------------------------------------------------------------------

GET_RE = re.compile(r'preferences\.get[A-Za-z]+\(\s*"([^"]+)"')
PUT_RE = re.compile(r'preferences\.put[A-Za-z]+\(\s*"([^"]+)"\s*,\s*([^)]*)\)\s*;')
STRVAR_ASSIGN_RE = re.compile(r'^\s*strVar\s*=\s*meshcom_settings\.([A-Za-z_][A-Za-z0-9_]*)')
MEMBER_IN_EXPR_RE = re.compile(r'meshcom_settings\.([A-Za-z_][A-Za-z0-9_]*)')


@dataclass
class PrefCalls:
    get_keys: List[str] = field(default_factory=list)
    # (nvs key, resolved struct member or None if it couldn't be traced)
    put_calls: List[Tuple[str, Optional[str]]] = field(default_factory=list)


def parse_pref_calls(esp32_flash_cpp_text: str) -> PrefCalls:
    """`get_keys`: the NVS key of every `preferences.get*()` call, in file
    order (duplicates kept; callers that want a set can dedupe).
    `put_calls`: (key, member) for every `preferences.put*()` call. `member`
    is resolved from the call's own source, the only two shapes this file
    uses: `meshcom_settings.member` written directly as the argument, or the
    `strVar = meshcom_settings.member;` assignment immediately preceding a
    `preferences.putString(key, strVar)` call. Anything else resolves to
    None (UNRESOLVED) rather than being guessed at."""
    result = PrefCalls()
    pending_strvar_member: Optional[str] = None
    for raw in _strip_line_comments(esp32_flash_cpp_text).splitlines():
        sm = STRVAR_ASSIGN_RE.match(raw)
        if sm:
            pending_strvar_member = sm.group(1)
            continue
        for gm in GET_RE.finditer(raw):
            result.get_keys.append(gm.group(1))
        pm = PUT_RE.search(raw)
        if pm:
            key, arg = pm.group(1), pm.group(2).strip()
            mm = MEMBER_IN_EXPR_RE.search(arg)
            if mm:
                member = mm.group(1)
            elif arg == "strVar":
                member = pending_strvar_member
            else:
                member = None
            result.put_calls.append((key, member))
    return result


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

@dataclass
class AnalysisResult:
    x_row_count: int = 0
    struct_member_count: int = 0
    put_count: int = 0
    get_count: int = 0
    violations: List[str] = field(default_factory=list)
    excluded: List[str] = field(default_factory=list)      # informational only
    unresolved: List[str] = field(default_factory=list)    # informational only
    fatal: List[str] = field(default_factory=list)
    notes: List[str] = field(default_factory=list)         # informational only, e.g. schema-driven mode


def analyze(x_rows: List[XRow], struct_members: Set[str],
            pref_calls: PrefCalls,
            exclusions: Optional[Dict[str, str]] = None) -> AnalysisResult:
    """Pure analysis over already-extracted data -- no file IO, so
    `--self-test` can drive it with synthetic fixtures. `exclusions`
    defaults to the real EXPLAINED_EXCLUSIONS; tests pass their own so the
    downgrade behaviour can be proven independently of the real tree's
    (currently empty) set of matches."""
    excl = EXPLAINED_EXCLUSIONS if exclusions is None else exclusions

    def is_excluded(*names: Optional[str]) -> Optional[str]:
        for n in names:
            if n and n in excl:
                return excl[n]
        return None

    r = AnalysisResult(
        x_row_count=len(x_rows), struct_member_count=len(struct_members),
        put_count=len(pref_calls.put_calls), get_count=len(pref_calls.get_keys))

    if not x_rows:
        r.fatal.append(
            "zero X() table rows extracted from the ESP32 branch of "
            "config_json.h -- the row/macro-body pattern has stopped "
            "matching; this is NOT a clean tree, it is a broken instrument")
    if not struct_members:
        r.fatal.append(
            f"zero members extracted from the M(...)/A(...) X-macro rows of "
            f"{MESHCOM_SETTINGS_H} -- the row pattern has stopped matching")
    if not pref_calls.put_calls and not pref_calls.get_keys:
        r.fatal.append(
            f"zero preferences.get*/put* calls extracted from "
            f"{ESP32_FLASH_CPP} -- the call pattern has stopped matching")
    if r.fatal:
        return r

    put_by_key: Dict[str, List[Optional[str]]] = {}
    for key, member in pref_calls.put_calls:
        put_by_key.setdefault(key, []).append(member)
        if member is None:
            r.unresolved.append(
                f'preferences.put*("{key}", ...) -- could not trace the '
                f"value back to a meshcom_settings member (neither a direct "
                f"meshcom_settings.<member> argument nor the strVar idiom); "
                f"resolve by hand, this is a parser limitation, not a "
                f"confirmed finding either way")

    # --- check 1: X() table field with an ESP32 member, but no put() call ---
    for row in x_rows:
        if row.member not in struct_members:
            # The row's own premise (it has an ESP32 struct member) is
            # false -- code referencing it would not compile, so this is a
            # parser/tree inconsistency worth a loud note, not a silent skip.
            r.violations.append(
                f'X() row "{row.key}" (member {row.member}) does not match '
                f"any member of struct {STRUCT_NAME} -- either the struct "
                f"or the parser has drifted; this row cannot be checked "
                f"for a persist call until that is resolved")
            continue
        if row.key not in put_by_key:
            reason = is_excluded(row.key, row.member)
            msg = (f'"{row.key}" (member {row.member}) is in the X() table '
                   f"with a real ESP32 struct member, but esp32_flash.cpp "
                   f"has no preferences.put* call for that key -- the user "
                   f"can set it, the node forgets it on reboot (DR-13)")
            if reason:
                r.excluded.append(f"{msg} -- EXCLUDED (explained): {reason}")
            else:
                r.violations.append(msg)

    # --- check 2: put() call whose resolved member is not a real field ---
    for key, member in pref_calls.put_calls:
        if member is None or member in struct_members:
            continue
        reason = is_excluded(key, member)
        msg = (f'preferences.put*("{key}", ...) writes struct member '
               f"'{member}', which is not a member of struct {STRUCT_NAME} "
               f"-- a rename left a write to a key nothing reads")
        if reason:
            r.excluded.append(f"{msg} -- EXCLUDED (explained): {reason}")
        else:
            r.violations.append(msg)

    # --- check 3: put/get symmetry ---
    put_keys = set(put_by_key)
    get_keys = set(pref_calls.get_keys)
    for key in sorted(put_keys - get_keys):
        reason = is_excluded(key)
        msg = (f'"{key}": written (preferences.put*) but never read back '
               f"(no preferences.get* call) -- init_flash() will silently "
               f"discard the save every boot")
        if reason:
            r.excluded.append(f"{msg} -- EXCLUDED (explained): {reason}")
        else:
            r.violations.append(msg)
    for key in sorted(get_keys - put_keys):
        reason = is_excluded(key)
        msg = (f'"{key}": read (preferences.get*) but never written (no '
               f"preferences.put* call) -- save_settings() never persists "
               f"whatever this key holds; the read value can only be a "
               f"default or come from outside this file")
        if reason:
            r.excluded.append(f"{msg} -- EXCLUDED (explained): {reason}")
        else:
            r.violations.append(msg)

    return r


def is_schema_driven(esp32_flash_cpp_text: str) -> bool:
    """True once esp32_flash.cpp's load AND save both walk
    settings_schema::fields() generically instead of issuing one hand-written
    preferences.put*/get*("literal key", ...) call per field -- see the
    module docstring's "SCHEMA-DRIVEN MODE" section. Two-plus occurrences
    (load and save each walk the table once) distinguishes "the mechanism
    moved" from "the mechanism vanished" -- one stray mention in a comment
    would not clear this bar."""
    return esp32_flash_cpp_text.count("settings_schema::fields()") >= 2


def check(repo: Path = REPO) -> AnalysisResult:
    cfg_p = repo / CONFIG_JSON_CPP
    h_p = repo / MESHCOM_SETTINGS_H
    cpp_p = repo / ESP32_FLASH_CPP
    missing = [p for p in (cfg_p, h_p, cpp_p) if not p.exists()]
    if missing:
        r = AnalysisResult()
        r.fatal.extend(f"{p} does not exist" for p in missing)
        return r

    x_rows = parse_x_table_esp32(cfg_p.read_text())
    struct_members = parse_esp32_struct_members(h_p.read_text())
    cpp_text = cpp_p.read_text()

    if is_schema_driven(cpp_text):
        # See the module docstring's "SCHEMA-DRIVEN MODE" section: the three
        # checks below all depend on literal preferences.put*/get*("literal
        # key", ...) call sites, which this file categorically no longer
        # has. Running them anyway would either FATAL on the now-genuinely-
        # empty call lists, or -- once that FATAL is worked around -- report
        # one false "no preferences.put* call" violation per real field.
        # settings_schema_lint.py's checks 1-4 already verify schema
        # completeness against the same triage table by parsing the schema
        # itself, not this file's call sites, so nothing here is silently
        # going unchecked.
        r = AnalysisResult(x_row_count=len(x_rows), struct_member_count=len(struct_members))
        r.notes.append(
            "esp32_flash.cpp is schema-driven (settings_schema::fields() "
            "walk, D1-04 W3 Task 1): checks 1-3 do not apply to a generic "
            "walk with no literal preferences.put*/get* call sites left to "
            "parse -- see settings_schema_lint.py for the completeness "
            "check that replaces them")
        return r

    pref_calls = parse_pref_calls(cpp_text)
    return analyze(x_rows, struct_members, pref_calls)


# ---------------------------------------------------------------------------
# --self-test
# ---------------------------------------------------------------------------

def _fake_x_table_cpp(rows: str, esp32_platform_rows: str = "",
                       nrf52_platform_rows: str = 'X("send_repeat_time", CFG_U32, send_repeat_time, CFG_NORANGE, CFG_NOESC)') -> str:
    """A minimal config_json.h-shaped fixture: a backslash-continued
    CFG_FIELD_LIST(X) that calls CFG_FIELD_LIST_PLATFORM(X), plus an
    #ifdef ESP32 / #else pair defining that macro per platform -- the exact
    shape parse_x_table_esp32() depends on."""
    return (
        "#define CFG_FIELD_LIST(X)                                    \\\n"
        + rows +
        "    CFG_FIELD_LIST_PLATFORM(X)\n"
        "\n"
        "#ifdef ESP32\n"
        "    #define CFG_FIELD_LIST_PLATFORM(X)                       \\\n"
        + esp32_platform_rows +
        "#else\n"
        "    #define CFG_FIELD_LIST_PLATFORM(X)                       \\\n"
        f"        {nrf52_platform_rows}\n"
        "#endif\n")


def _fake_meshcom_settings_h(common_rows: str, esp32_rows: str = "",
                              tdeck_rows: str = "") -> str:
    """A minimal meshcom_settings.h-shaped fixture: MESHCOM_SETTINGS_MEMBERS_COMMON
    always defined, plus MESHCOM_SETTINGS_MEMBERS_ESP32/_TDECK each defined
    TWICE (a populated body under their real guard, an empty body under the
    opposite branch) -- the exact shape parse_esp32_struct_members() depends
    on: it unions M(...)/A(...) rows from every occurrence of each macro
    NAME it finds, by construction ignoring which branch produced them, so
    the always-empty opposite branch simply contributes nothing.
    `common_rows`/`esp32_rows`/`tdeck_rows` must each be pre-formatted
    exactly like a real X-macro body: every row line ends in ` \\\\\\n'
    except the body's own last line, matching meshcom_settings.h's own
    convention."""
    return (
        "#define MESHCOM_SETTINGS_MEMBERS_COMMON(M, A)                       \\\n"
        f"{common_rows}\n"
        "#ifdef ESP32\n"
        "    #define MESHCOM_SETTINGS_MEMBERS_ESP32(M, A)                       \\\n"
        f"{esp32_rows}\n"
        "#else\n"
        "    #define MESHCOM_SETTINGS_MEMBERS_ESP32(M, A)\n"
        "#endif\n"
        "\n"
        "#if defined(ESP32) && defined(BOARD_T_DECK)\n"
        "    #define MESHCOM_SETTINGS_MEMBERS_TDECK(M, A)                       \\\n"
        f"{tdeck_rows}\n"
        "#else\n"
        "    #define MESHCOM_SETTINGS_MEMBERS_TDECK(M, A)\n"
        "#endif\n")


def self_test() -> int:
    ok = True

    def report(name: str, good: bool, detail: str = "") -> None:
        nonlocal ok
        print(f"  {'ok ' if good else 'FAIL'} {name}{(': ' + detail) if detail else ''}")
        ok = ok and good

    # --- X() table parsing: common rows + ESP32 platform rows, nRF52-only excluded ---
    cfg_text = _fake_x_table_cpp(
        rows=(
            '    X("node_call", CFG_STR, node_call, CFG_NORANGE, CFG_NOESC) \\\n'
            '    X("node_alt",  CFG_INT, node_alt,  CFG_NORANGE, CFG_NOESC) \\\n'
        ),
        esp32_platform_rows=(
            '        X("node_disrot", CFG_INT, node_disp_rot, CFG_NORANGE, CFG_NOESC)\n'
        ))
    x_rows = parse_x_table_esp32(cfg_text)
    got_keys = sorted(r.key for r in x_rows)
    report("X() table: common + ESP32-platform rows found, nRF52-only excluded",
           got_keys == ["node_alt", "node_call", "node_disrot"], repr(got_keys))

    naive_total, naive_members, naive_keys = naive_doc_member_count(cfg_text)
    report("naive_doc_member_count sees all 4 rows across both branches "
           "(the whole-file, preprocessor-blind count the triage doc uses)",
           naive_total == 4 and naive_keys == 4, f"total={naive_total} keys={naive_keys}")

    # --- meshcom_settings.h member-row parsing, including a T-Deck-only
    # (MESHCOM_SETTINGS_MEMBERS_TDECK) member ---
    settings_h = _fake_meshcom_settings_h(
        common_rows=(
            '    M(char, node_call, 10)                                    \\\n'
            '    M(int, node_alt, 0)'),
        esp32_rows='        M(int, node_disp_rot, 0)',
        tdeck_rows='            M(int, node_map, 0)')
    members = parse_esp32_struct_members(settings_h)
    report("meshcom_settings.h member-row parse includes the T-Deck-only "
           "(MESHCOM_SETTINGS_MEMBERS_TDECK) member",
           members == {"node_call", "node_alt", "node_disp_rot", "node_map"},
           repr(sorted(members)))

    empty_settings_h = _fake_meshcom_settings_h("    // nothing parseable here")
    report("meshcom_settings.h member-row parse returns empty set when "
           "nothing matches (feeds the FATAL void-check, not asserted here "
           "directly)",
           parse_esp32_struct_members(empty_settings_h) == set())

    # --- pref call parsing: direct member, strVar idiom, unresolved ---
    flash_cpp = (
        'preferences.putInt("node_alt", meshcom_settings.node_alt);\n'
        "strVar = meshcom_settings.node_call;\n"
        'preferences.putString("node_call", strVar);\n'
        'preferences.putInt("node_mystery", someOtherVariable);\n'
        'meshcom_settings.node_alt = preferences.getInt("node_alt", 0);\n'
    )
    calls = parse_pref_calls(flash_cpp)
    report("pref call parse: direct meshcom_settings.member argument",
           ("node_alt", "node_alt") in calls.put_calls)
    report("pref call parse: strVar = meshcom_settings.member idiom",
           ("node_call", "node_call") in calls.put_calls)
    report("pref call parse: put() whose source cannot be traced -> member is None",
           ("node_mystery", None) in calls.put_calls)
    report("pref call parse: get key collected",
           calls.get_keys == ["node_alt"])

    # --- analyze(): clean case ---
    clean_x = [XRow("node_call", "CFG_STR", "node_call"),
               XRow("node_alt", "CFG_INT", "node_alt")]
    clean_members = {"node_call", "node_alt"}
    clean_calls = PrefCalls(
        get_keys=["node_call", "node_alt"],
        put_calls=[("node_call", "node_call"), ("node_alt", "node_alt")])
    r = analyze(clean_x, clean_members, clean_calls, exclusions={})
    report("analyze: clean tree -> no fatal, no violations",
           not r.fatal and not r.violations, f"violations={r.violations}")

    # --- check 1: X() field with a real member but no put() call ---
    missing_put_x = clean_x + [XRow("node_owgpio", "CFG_INT", "node_owgpio")]
    missing_put_members = clean_members | {"node_owgpio"}
    r = analyze(missing_put_x, missing_put_members, clean_calls, exclusions={})
    report("check 1: X() field with a real member and no put() call -> violation",
           not r.fatal and any("no preferences.put* call" in v for v in r.violations),
           str(r.violations))

    # --- check 1: X() row whose member does not exist in the struct at all ---
    bogus_member_x = clean_x + [XRow("node_ghost", "CFG_INT", "node_ghost")]
    r = analyze(bogus_member_x, clean_members, clean_calls, exclusions={})
    report("check 1: X() row referencing a nonexistent struct member -> violation",
           not r.fatal and any("does not match any member" in v for v in r.violations),
           str(r.violations))

    # --- check 2: put() call resolves to a member that isn't real ---
    stale_calls = PrefCalls(
        get_keys=["node_call", "node_alt"],
        put_calls=[("node_call", "node_call"), ("node_alt", "node_alt"),
                   ("node_stale", "node_removed_field")])
    r = analyze(clean_x, clean_members, stale_calls, exclusions={})
    report("check 2: put() writes a member that is not in the struct -> violation",
           not r.fatal and any("is not a member of struct" in v for v in r.violations),
           str(r.violations))

    # --- check 3: write-only and read-only, worded differently ---
    write_only_calls = PrefCalls(
        get_keys=["node_call"],
        put_calls=[("node_call", "node_call"), ("node_alt", "node_alt")])
    r = analyze(clean_x, clean_members, write_only_calls, exclusions={})
    report("check 3: write-only key -> 'never read back' wording",
           not r.fatal and any("never read back" in v for v in r.violations)
           and not any("never written" in v for v in r.violations),
           str(r.violations))

    read_only_calls = PrefCalls(
        get_keys=["node_call", "node_alt"],
        put_calls=[("node_call", "node_call")])
    r = analyze(clean_x, clean_members, read_only_calls, exclusions={})
    report("check 3: read-only key -> 'never written' wording, distinct from write-only",
           not r.fatal and any("never written" in v for v in r.violations)
           and not any("never read back" in v for v in r.violations),
           str(r.violations))

    # --- explained exclusion downgrades a violation to an informational line ---
    fake_excl = {"node_owgpio": "TEST: pretend this one is documented elsewhere"}
    r_violation = analyze(missing_put_x, missing_put_members, clean_calls, exclusions={})
    r_excluded = analyze(missing_put_x, missing_put_members, clean_calls, exclusions=fake_excl)
    report("explained exclusion: same finding is a VIOLATION with an empty "
           "exclusion table...",
           not r_violation.fatal
           and any("node_owgpio" in v for v in r_violation.violations)
           and not any("node_owgpio" in e for e in r_violation.excluded))
    report("...and an EXCLUDED (informational, not counted) line once the "
           "exclusion table names it",
           not r_excluded.fatal
           and not any("node_owgpio" in v for v in r_excluded.violations)
           and any("node_owgpio" in e and "EXCLUDED" in e for e in r_excluded.excluded))

    # explained exclusion also covers check 3 (symmetry) findings
    r = analyze(clean_x, clean_members, write_only_calls,
                exclusions={"node_alt": "TEST: pretend this asymmetry is documented"})
    report("explained exclusion also downgrades a check-3 (symmetry) finding",
           not r.fatal and not r.violations
           and any("node_alt" in e and "EXCLUDED" in e for e in r.excluded),
           str(r.excluded))

    # --- unresolved put target is reported but does not fail the gate by itself ---
    unresolved_calls = PrefCalls(
        get_keys=["node_call", "node_alt"],
        put_calls=[("node_call", "node_call"), ("node_alt", None)])
    r = analyze(clean_x, clean_members, unresolved_calls, exclusions={})
    report("unresolved put target: reported in `unresolved`, not `violations`",
           not r.fatal and len(r.unresolved) == 1 and not r.violations,
           str(r.unresolved))

    # --- FATAL / void-check paths: extraction finding nothing must never pass ---
    r = analyze([], clean_members, clean_calls)
    report("FATAL: zero X() rows extracted",
           bool(r.fatal) and not r.violations and "X() table rows" in r.fatal[0])

    r = analyze(clean_x, set(), clean_calls)
    report("FATAL: zero struct members extracted",
           bool(r.fatal) and "members extracted" in r.fatal[0])

    r = analyze(clean_x, clean_members, PrefCalls())
    report("FATAL: zero preferences calls extracted",
           bool(r.fatal) and "preferences.get*/put*" in r.fatal[0])

    r = check(repo=REPO / "test" / "golden" / "does-not-exist-dir")
    report("FATAL via check(): missing source files on disk",
           bool(r.fatal) and all("does not exist" in f for f in r.fatal))

    # --- is_schema_driven() / check(): D1-04 W3 Task 1 shape ---
    report("is_schema_driven: two occurrences (load + save) -> True",
           is_schema_driven(
               "settings_schema::fields()\nsettings_schema::fields()\n"))
    report("is_schema_driven: one occurrence only -> False (still legacy)",
           not is_schema_driven("settings_schema::fields()\n"))
    report("is_schema_driven: zero occurrences -> False",
           not is_schema_driven('preferences.putInt("node_alt", 0);\n'))

    import tempfile

    def _write_fixture(root: Path, cpp_text: str) -> None:
        (root / "src" / "esp32").mkdir(parents=True)
        (root / "src" / "config_json.h").write_text(
            "#define CFG_FIELD_LIST(X)                       \\\n"
            '    X("node_call", CFG_STR, node_call, CFG_NORANGE, CFG_NOESC) \\\n'
            "    CFG_FIELD_LIST_PLATFORM(X)\n"
            "#ifdef ESP32\n"
            "    #define CFG_FIELD_LIST_PLATFORM(X)\n"
            "#else\n"
            "    #define CFG_FIELD_LIST_PLATFORM(X)\n"
            "#endif\n")
        (root / "src" / "meshcom_settings.h").write_text(
            _fake_meshcom_settings_h('    M(char, node_call, 10)'))
        (root / "src" / "esp32" / "esp32_flash.cpp").write_text(cpp_text)

    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        _write_fixture(root, (
            "void load() { settings_schema::fields(); }\n"
            "void save() { settings_schema::fields(); }\n"))
        r = check(repo=root)
        report("check(): schema-driven fixture -> no fatal, no violations, "
               "note explains the mode switch",
               not r.fatal and not r.violations
               and any("schema-driven" in n for n in r.notes),
               f"fatal={r.fatal} violations={r.violations} notes={r.notes}")

    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        # Only ONE occurrence (e.g. load ported, save left hand-written with
        # zero literal calls in this minimal fixture) -- must NOT be treated
        # as schema-driven; the legacy parse runs and correctly FATALs on
        # the real zero-calls void-check rather than silently reporting
        # nothing wrong.
        _write_fixture(root, "void load() { settings_schema::fields(); }\n")
        r = check(repo=root)
        report("check(): only ONE settings_schema::fields() occurrence -> "
               "NOT schema-driven, legacy parse FATALs on zero real calls",
               bool(r.fatal) and any("preferences.get*/put*" in f for f in r.fatal),
               f"fatal={r.fatal}")

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    result = check()
    for f in result.fatal:
        print(f"FATAL: {f}")
    if result.fatal:
        return 1

    print(f"{result.x_row_count} ESP32 X() table row(s), "
          f"{result.struct_member_count} struct member(s), "
          f"{result.get_count} get call(s), {result.put_count} put call(s)")

    for n in result.notes:
        print(f"\nNOTE: {n}")

    cfg_text = (REPO / CONFIG_JSON_CPP).read_text()
    naive_total, naive_members, naive_keys = naive_doc_member_count(cfg_text)
    doc_claimed_members = 89
    agree = naive_members == doc_claimed_members
    print(f"cross-check vs docs/d1-04-settings-field-triage-20260912.md 'Method': "
          f"naive whole-file regex sees {naive_members} unique X() member(s) "
          f"({naive_total} row(s), {naive_keys} unique key(s)); document "
          f"claims {doc_claimed_members} -- "
          f"{'AGREE' if agree else 'DISAGREE, see below'}")
    if not agree:
        print(f"  DISCREPANCY: this script's re-run of the document's own "
              f"stated method gets {naive_members}, not {doc_claimed_members}. "
              f"Trusting this script's fresh extraction over the document; "
              f"the document may be stale or its method may have been "
              f"transcribed imprecisely -- re-derive docs/d1-04-... §1 by "
              f"hand before relying on its '89' figure.")

    doc_claimed_disagreement_a = 0
    real_disagreement_a = sum(
        1 for v in result.violations if "no preferences.put* call" in v)
    agree_a = real_disagreement_a == doc_claimed_disagreement_a
    print(f"cross-check vs the document's disagreement (a) ('X() field with "
          f"an ESP32 member but no ESP32 NVS key'): this gate's check 1 "
          f"finds {real_disagreement_a}; document claims "
          f"{doc_claimed_disagreement_a} -- "
          f"{'AGREE' if agree_a else 'DISAGREE, see violations below'}")

    if result.unresolved:
        print(f"\n{len(result.unresolved)} put() call(s) with an unresolved "
              f"source member (parser limitation, not a confirmed finding "
              f"either way -- resolve by hand):")
        for u in result.unresolved:
            print(f"  UNRESOLVED {u}")

    if result.excluded:
        print(f"\n{len(result.excluded)} finding(s) downgraded by "
              f"EXPLAINED_EXCLUSIONS (documented in config_json.h:127-137 / "
              f"config_json.h:362-367):")
        for e in result.excluded:
            print(f"  EXCLUDED {e}")

    if result.violations:
        print(f"\n{len(result.violations)} violation(s):")
        for v in result.violations:
            print(f"  VIOLATION {v}")
        return 1

    print("\nsettings persistence: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
