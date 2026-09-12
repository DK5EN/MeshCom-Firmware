#!/usr/bin/env python3
"""Completeness gate for ESP32 settings persistence (DR-13, `both-wrong`).

Two platforms maintain the same "which settings fields exist" mapping twice
by hand:

  src/config_json.cpp   the `X()` table (CFG_FIELD_LIST) -- one row per field
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
parsing `src/config_json.cpp` and `src/esp32/esp32_flash.cpp` as text (this
file never compiles firmware and must work with a native `python3`).

THE THREE CHECKS
-----------------

  1. MISSING PERSIST -- every field in the `X()` table that has a matching
     ESP32 struct member (`src/esp32/esp32_flash.h`) must have a
     `preferences.put*()` call for its NVS key in `esp32_flash.cpp`. A
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
     set, same convention `settings_layout_lint.py` uses for the struct
     itself). A key on only one side is two DIFFERENT defects, reported
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
and `config_json.h:127-137` / `config_json.cpp:214-219` say so in as many
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
regex it ran over the whole of `config_json.cpp` (both platform branches,
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
CONFIG_JSON_CPP = "src/config_json.cpp"
ESP32_FLASH_H = "src/esp32/esp32_flash.h"
ESP32_FLASH_CPP = "src/esp32/esp32_flash.cpp"
STRUCT_NAME = "s_meshcom_settings"

# ---------------------------------------------------------------------------
# Explained exclusions -- config_json.h:127-137, config_json.cpp:214-219
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
_COUNTER = "running id counter (config_json.cpp:214-217); persisted so ids " \
    "stay monotone across reboot, never exported (would rewind and collide)"
_SENSOR = "last sensor reading (config_json.cpp:218-219), explicitly 'not " \
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
# Extraction: the ESP32 struct's member names
# ---------------------------------------------------------------------------

FIELD_RE = re.compile(
    r"^\s*([A-Za-z_][\w:<>]*(?:\s+[A-Za-z_][\w:<>]*)*)"
    r"\s+(\w+)"
    r"((?:\s*\[[^\]]*\])*)"
    r"\s*(?:=[^;]*)?;\s*$")


def _strip_line_comments(text: str) -> str:
    return "\n".join(l.split("//", 1)[0] for l in text.splitlines())


def parse_esp32_struct_members(esp32_flash_h_text: str) -> Set[str]:
    """Member names of `struct s_meshcom_settings` in esp32_flash.h. A field
    inside a `#if defined(BOARD_T_DECK...)` guard is still a real ESP32
    member on the boards where that macro is set (same convention
    settings_layout_lint.py uses for this same struct) -- only bare `#...`
    preprocessor lines themselves are skipped, not the code they guard."""
    text = _strip_line_comments(esp32_flash_h_text)
    m = re.search(r"struct\s+" + re.escape(STRUCT_NAME) + r"\b", text)
    if not m:
        return set()
    rest = text[m.end():]
    start = rest.find("{")
    if start == -1:
        return set()
    depth, end = 0, None
    for i, ch in enumerate(rest[start:], start):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end is None:
        return set()
    body = rest[start + 1:end]

    members: Set[str] = set()
    for raw in body.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fm = FIELD_RE.match(line)
        if not fm:
            continue
        ty = " ".join(fm.group(1).split())
        if ty.split(" ")[0] in ("return", "else", "case", "static", "const"):
            continue
        members.add(fm.group(2))
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
            "config_json.cpp -- the row/macro-body pattern has stopped "
            "matching; this is NOT a clean tree, it is a broken instrument")
    if not struct_members:
        r.fatal.append(
            f"zero members extracted from struct {STRUCT_NAME} in "
            f"{ESP32_FLASH_H} -- the struct pattern has stopped matching")
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


def check(repo: Path = REPO) -> AnalysisResult:
    cfg_p = repo / CONFIG_JSON_CPP
    h_p = repo / ESP32_FLASH_H
    cpp_p = repo / ESP32_FLASH_CPP
    missing = [p for p in (cfg_p, h_p, cpp_p) if not p.exists()]
    if missing:
        r = AnalysisResult()
        r.fatal.extend(f"{p} does not exist" for p in missing)
        return r

    x_rows = parse_x_table_esp32(cfg_p.read_text())
    struct_members = parse_esp32_struct_members(h_p.read_text())
    pref_calls = parse_pref_calls(cpp_p.read_text())
    return analyze(x_rows, struct_members, pref_calls)


# ---------------------------------------------------------------------------
# --self-test
# ---------------------------------------------------------------------------

def _fake_x_table_cpp(rows: str, esp32_platform_rows: str = "",
                       nrf52_platform_rows: str = 'X("send_repeat_time", CFG_U32, send_repeat_time, CFG_NORANGE, CFG_NOESC)') -> str:
    """A minimal config_json.cpp-shaped fixture: a backslash-continued
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


def _fake_struct_h(members: str) -> str:
    return f"struct {STRUCT_NAME}\n{{\n{members}\n}};\n"


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

    # --- struct member parsing, including a T-Deck-guarded field ---
    struct_h = _fake_struct_h(
        "    char node_call[10] = {0};\n"
        "    int node_alt = 0;\n"
        "    int node_disp_rot = 0;\n"
        "    #if defined(BOARD_T_DECK)\n"
        "    int node_map = 0;\n"
        "    #endif\n")
    members = parse_esp32_struct_members(struct_h)
    report("struct member parse includes a guarded (#if) field",
           members == {"node_call", "node_alt", "node_disp_rot", "node_map"},
           repr(sorted(members)))

    empty_struct = _fake_struct_h("// nothing parseable here\n")
    report("struct member parse returns empty set when nothing matches "
           "(feeds the FATAL void-check, not asserted here directly)",
           parse_esp32_struct_members(empty_struct) == set())

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
              f"config_json.cpp:214-219):")
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
