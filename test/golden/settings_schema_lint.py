#!/usr/bin/env python3
"""Static gate for `src/settings_schema.h`/`.cpp` (W3 settings campaign, wave B2).

D1-04's target architecture splits "which fields does this firmware persist"
into two co-operating tables that must never silently drift apart:

  src/config_json.h        CFG_FIELD_LIST(X) + CFG_FIELD_LIST_PLATFORM(X) --
                            the JSON export/import table (unchanged, moved
                            here from config_json.cpp 2026-09-12).
  src/settings_schema.h     SETTINGS_PERSIST_ONLY_LIST(X) -- fields that ARE
                            persisted in ESP32 NVS but are deliberately NOT
                            exported/imported (bookkeeping, T-Deck UI state,
                            the disagreement-(b)/(c) fields from the triage
                            below), added by the sibling wave that owns this
                            file.

Both use the same `X(key, type, member, lo, hi, esc, has_esc)` row shape
(CFG_NORANGE expands to `1.0, 0.0`, CFG_NOESC to `0.0, 0` -- see
src/config_json.h). This script parses BOTH as text (never compiles
firmware, must run under a native python3) and checks the union against
`docs/d1-04-settings-field-triage-20260912.md` (`TRIAGE_DOC` below), which
hand-classified all 147 settings-struct fields as PERSIST or RUNTIME.

THE FIVE CHECKS
---------------

  1. UNION PARSE -- CFG_FIELD_LIST + CFG_FIELD_LIST_PLATFORM (both platform
     branches, separately) + SETTINGS_PERSIST_ONLY_LIST, per platform. A
     missing or unparsable `src/settings_schema.h` (the sibling wave may not
     have landed yet when this gate runs) is treated as an EMPTY
     SETTINGS_PERSIST_ONLY_LIST, not a fatal error -- checks 2-5 just see
     fewer rows and report accordingly; only CFG_FIELD_LIST failing to parse
     against config_json.h, the ESP32 struct failing to parse, or zero
     `preferences.put*` calls being found is treated as a broken instrument
     (FATAL, matching test/golden/settings_persist_lint.py's convention: a
     pattern that has stopped matching must never look like a clean tree).

  2. NO DUPLICATE KEY / MEMBER, per platform. The union for one platform is
     CFG_FIELD_LIST + that platform's CFG_FIELD_LIST_PLATFORM branch +
     SETTINGS_PERSIST_ONLY_LIST (both branches if it has them, see
     `extract_platform_macro()`; the whole body for both platforms if it
     does not). A NOT-a-duplicate on purpose: `node_mcp17t[0]`..`[15]` and
     `node_gcb[0]`..`[5]` are 16 (resp. 6) distinct rows sharing one BASE
     member name by design (one array, many NVS keys) -- so duplicate
     detection keys on `member_full` (base name + its own `[index]`
     suffix, verbatim per row), never on the base name alone. A genuine
     same-member-different-key collision (`member_full` identical) IS a
     duplicate; so is the same NVS key spelled twice.

  3. COVERAGE -- every field classified PERSIST in TRIAGE_DOC's full-147
     table (`FIELD_CLASSIFICATION` below, transcribed from its section 3) is
     either present in the schema union (any platform, any source) or named
     in `EXCLUDED_FROM_SCHEMA` with a reason. Anything PERSIST that is in
     neither place is a finding.

  4. NO RUNTIME FIELD IN THE SCHEMA -- every member that DOES appear
     anywhere in the schema union must be classified PERSIST by
     FIELD_CLASSIFICATION, unless it is named in `EXCLUDED_FROM_SCHEMA`. A
     member the 147-field table has never heard of at all (added to the
     struct and the schema without ever being triaged) is reported
     separately, as UNCLASSIFIED rather than VIOLATION -- it cannot be
     confirmed PERSIST OR RUNTIME by this script, so it is not silently
     waved through as clean either.

  5. TRIAGE DISAGREEMENT (a) STAYS ZERO -- the DR-12/DR-13 asserting test.
     Every member of the X() union (CFG_FIELD_LIST + the ESP32 branch of
     CFG_FIELD_LIST_PLATFORM -- NOT SETTINGS_PERSIST_ONLY_LIST, which is a
     separate mechanism for fields the X() table never held) that has a real
     ESP32 struct member (src/esp32/esp32_flash.h) must have a
     `preferences.put*` call for its NVS key in src/esp32/esp32_flash.cpp.
     The triage measured disagreement (a) at 0 by hand; this check
     re-derives the same fact from a fresh parse on every run and keeps it
     at 0 going forward.

     SCHEMA-DRIVEN MODE (D1-04 W3 Task 1, landed 2026-09-13): once
     esp32_flash.cpp's load/save walk `settings_schema::fields()` generically
     (dispatching on FieldDescriptor::type against a variable `d.key`) rather
     than issuing one hand-written `preferences.put*("literal key", ...)`
     call per field, `parse_put_keys()`'s literal-string regex has nothing
     left to find -- not because a field stopped being persisted, but because
     the persistence mechanism moved from source-code call sites to a data
     table this same script already parses in full elsewhere (checks 1-4).
     `check()` detects this (a `settings_schema::fields()` marker appearing
     at least twice in esp32_flash.cpp -- once for load, once for save) and
     substitutes the ESP32 schema union's own key set for `put_keys` in that
     case: check 5 is then a tautology BY CONSTRUCTION (every X()-union key
     with a real member is, trivially, a member of that same union) EXCEPT
     for keys the walk explicitly skips on the load side (see
     esp32_flash.cpp's `isLoadSpecialCased()`) -- those still only reach
     flash via the save-side generic walk, which has no exclusions, so they
     remain covered too. This is intentionally a weaker check than the
     legacy literal-call parse it replaces for THIS one axis (it can no
     longer catch "someone renamed a key string in only the put call",
     because there is only one call site, this table, driving both
     directions) -- checks 1-4 (which never depended on literal call
     parsing) are unaffected and remain the real coverage gate.

EXCLUDED_FROM_SCHEMA -- one table, one direction now
-----------------------------------------------------

Same "explained exception, not a fig leaf" idiom as
settings_persist_lint.py's EXPLAINED_EXCLUSIONS: named in code, cited, and
consulted by more than one check rather than duplicated per check. Pinned to
exactly 8 entries, all exempting check 4 (no RUNTIME in the schema):

  - the 8 fields of TRIAGE_DOC §4(c) (`node_msgid`, `node_ackid`,
    `node_temp`, `node_hum`, `node_press`, `node_temp2`, `node_gas_res`,
    `node_co2`) -- classified RUNTIME per TRIAGE_DOC §3 (they are computed/
    overwritten at runtime), but exempted from check 4 (no RUNTIME in the
    schema): §4(c)'s own text says these ARE persisted in ESP32 NVS today
    (running id counters and last-sensor-reading caches, kept across a
    reboot on purpose) and leaves "keep persisting" an open, reasonable W3
    call -- if SETTINGS_PERSIST_ONLY_LIST legitimately carries them, that is
    not the drift this check exists to catch.

  `node_audio_start`/`node_audio_msg` used to sit here too (PERSIST per
  TRIAGE_DOC §3, exempted from check 3/coverage): both were Arduino `String`
  members and `settings_schema`'s descriptors have no String type. D1-04 W3
  Task 2 converted both to a fixed `char[128]` and Task 1 gave both a real
  SETTINGS_PERSIST_ONLY_LIST row (settings_schema.h) -- they are now
  ordinary covered members, not an exception, so they were removed from this
  table rather than left in it pointing at a reason that no longer applies.

Self-tests: `--self-test` (`--selftest` also accepted) drives every function
above against synthetic fixtures in a temp directory or plain in-memory
data -- never the real `src/` tree, so this script can be exercised safely
mid-wave while the sibling file is still being written.

  python3 test/golden/settings_schema_lint.py --self-test
  python3 test/golden/settings_schema_lint.py

Exit 0 when clean, 1 on any finding or FATAL parse failure.
"""
import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

REPO = Path(__file__).resolve().parents[2]
CONFIG_JSON_H = "src/config_json.h"
SETTINGS_SCHEMA_H = "src/settings_schema.h"
SETTINGS_SCHEMA_CPP = "src/settings_schema.cpp"
ESP32_FLASH_H = "src/esp32/esp32_flash.h"
ESP32_FLASH_CPP = "src/esp32/esp32_flash.cpp"
MAXHOP_H = "src/maxhop.h"
STRUCT_NAME = "s_meshcom_settings"
TRIAGE_DOC = "docs/d1-04-settings-field-triage-20260912.md"

PERSIST = "PERSIST"
RUNTIME = "RUNTIME"

# ---------------------------------------------------------------------------
# FIELD_CLASSIFICATION -- transcribed from TRIAGE_DOC section 3 (all 147
# settings-struct fields, dated 2026-09-12). This is the ground truth checks
# 3 and 4 measure the schema union against; it is not re-derived from the
# doc at runtime (the doc is prose + a markdown table, not a machine-
# readable source), so it is pinned here the same way
# settings_layout_lint.py pins EXPECTED_ESP32_ONLY_FIELDS: a literal Python
# table, changing it is a real code-review diff. Field names are struct
# MEMBER names (not NVS keys -- e.g. "node_ossid", not "node_ssid"), and an
# array field appears once under its base name ("node_mcp17t", not each
# "node_mcp17t[i]"), matching how TRIAGE_DOC §3 itself lists them.
# ---------------------------------------------------------------------------
FIELD_CLASSIFICATION: Dict[str, str] = {
    "auto_join": PERSIST,
    "bt_code": PERSIST,
    "max_hop_pos": RUNTIME,
    "max_hop_text": PERSIST,
    "node_ackid": RUNTIME,
    "node_age": RUNTIME,
    "node_alt": PERSIST,
    "node_analog_alpha": PERSIST,
    "node_analog_atten": PERSIST,
    "node_analog_batt_faktor": PERSIST,
    "node_analog_faktor": PERSIST,
    "node_analog_offset": PERSIST,
    "node_analog_pin": PERSIST,
    "node_analog_slope": PERSIST,
    "node_aprsmc": PERSIST,
    "node_atxt": PERSIST,
    "node_audio_msg": PERSIST,
    "node_audio_start": PERSIST,
    "node_backlightlock": PERSIST,
    "node_button_pin": PERSIST,
    "node_bw": PERSIST,
    "node_call": PERSIST,
    "node_cleanflash": PERSIST,
    "node_co2": RUNTIME,
    "node_contrast": PERSIST,
    "node_country": PERSIST,
    "node_cr": PERSIST,
    "node_date_day": RUNTIME,
    "node_date_hour": RUNTIME,
    "node_date_hundredths": RUNTIME,
    "node_date_minute": RUNTIME,
    "node_date_month": RUNTIME,
    "node_date_second": RUNTIME,
    "node_date_year": RUNTIME,
    "node_device_eui": RUNTIME,
    "node_disp_rot": PERSIST,
    "node_dns": RUNTIME,
    "node_eqns": PERSIST,
    "node_extern": PERSIST,
    "node_fanon": RUNTIME,
    "node_format": PERSIST,
    "node_freq": PERSIST,
    "node_fversion": PERSIST,
    "node_fwversion": PERSIST,
    "node_gas_res": RUNTIME,
    "node_gcb": PERSIST,
    "node_gpsbaud": PERSIST,
    "node_gpsdebug": PERSIST,
    "node_gw": RUNTIME,
    "node_gwsrv": PERSIST,
    "node_hamnet_only": PERSIST,
    "node_hasIPaddress": RUNTIME,
    "node_hum": RUNTIME,
    "node_imax": PERSIST,
    "node_immediate_save": PERSIST,
    "node_ip": RUNTIME,
    "node_isamp": PERSIST,
    "node_kbl_sync": PERSIST,
    "node_kbllightlock": PERSIST,
    "node_keyboardlock": PERSIST,
    "node_last_upd_timer": RUNTIME,
    "node_lat": PERSIST,
    "node_lat_c": PERSIST,
    "node_lon": PERSIST,
    "node_lon_c": PERSIST,
    "node_lora_call": PERSIST,
    "node_map": PERSIST,
    "node_maxv": PERSIST,
    "node_mcp17in": PERSIST,
    "node_mcp17io": PERSIST,
    "node_mcp17out": PERSIST,
    "node_mcp17t": PERSIST,
    "node_modus": PERSIST,
    "node_msgid": RUNTIME,
    "node_mute": PERSIST,
    "node_mversion": PERSIST,
    "node_name": PERSIST,
    "node_netmode": PERSIST,
    "node_ntctemp": RUNTIME,
    "node_ntp": RUNTIME,
    "node_opwd": PERSIST,
    "node_ossid": PERSIST,
    "node_owgpio": PERSIST,
    "node_owndns": PERSIST,
    "node_owngw": PERSIST,
    "node_ownip": PERSIST,
    "node_ownms": PERSIST,
    "node_ownntp": PERSIST,
    "node_parm": PERSIST,
    "node_parm_1": RUNTIME,
    "node_parm_id": RUNTIME,
    "node_parm_t": RUNTIME,
    "node_parm_time": PERSIST,
    "node_passwd": PERSIST,
    "node_persist_to_flash": PERSIST,
    "node_persist_to_sd": PERSIST,
    "node_pingcall": PERSIST,
    "node_pingcount": RUNTIME,
    "node_pingduration": RUNTIME,
    "node_pingmax": PERSIST,
    "node_pingtime": PERSIST,
    "node_postime": PERSIST,
    "node_power": PERSIST,
    "node_preamplebits": PERSIST,
    "node_press": RUNTIME,
    "node_press_alt": RUNTIME,
    "node_press_asl": RUNTIME,
    "node_pwd": PERSIST,
    "node_relay": PERSIST,
    "node_sf": PERSIST,
    "node_short": PERSIST,
    "node_shunt": PERSIST,
    "node_specend": PERSIST,
    "node_specsamples": PERSIST,
    "node_specstart": PERSIST,
    "node_specstep": PERSIST,
    "node_ss_baud": PERSIST,
    "node_ss_rx_pin": PERSIST,
    "node_ss_tx_pin": PERSIST,
    "node_sset": PERSIST,
    "node_sset2": PERSIST,
    "node_sset3": PERSIST,
    "node_sset4": PERSIST,
    "node_ssid": PERSIST,
    "node_subnet": RUNTIME,
    "node_symcd": PERSIST,
    "node_symid": PERSIST,
    "node_temp": RUNTIME,
    "node_temp2": RUNTIME,
    "node_tempi_off": PERSIST,
    "node_tempo_off": PERSIST,
    "node_track_freq": PERSIST,
    "node_unit": PERSIST,
    "node_update": RUNTIME,
    "node_utcoff": PERSIST,
    "node_values": PERSIST,
    "node_vbus": RUNTIME,
    "node_vcurrent": RUNTIME,
    "node_via": PERSIST,
    "node_vpower": RUNTIME,
    "node_vshunt": RUNTIME,
    "node_webpwd": PERSIST,
    "node_wifi_power": PERSIST,
    "node_wifion": PERSIST,
    "send_repeat_time": PERSIST,
    "valid_mark_1": RUNTIME,
    "valid_mark_2": RUNTIME,
}
assert len(FIELD_CLASSIFICATION) == 147, (
    f"FIELD_CLASSIFICATION has {len(FIELD_CLASSIFICATION)} entries, expected "
    f"147 -- it must match {TRIAGE_DOC} section 3's full field count exactly")

# ---------------------------------------------------------------------------
# EXCLUDED_FROM_SCHEMA -- see the module docstring's "EXCLUDED_FROM_SCHEMA"
# section for what these 10 are and which check (3 or 4) each one exempts.
# ---------------------------------------------------------------------------
_PERSISTED_COUNTER_OR_SENSOR = (
    f"{TRIAGE_DOC} section 4(c): RUNTIME-shaped value (running id counter "
    "or last-sensor-reading cache) that IS persisted in ESP32 NVS today on "
    "purpose, across a reboot; legitimately appearing in the schema is not "
    "the drift check 4 exists to catch (check 4 exemption)")

EXCLUDED_FROM_SCHEMA: Dict[str, str] = {
    "node_msgid": _PERSISTED_COUNTER_OR_SENSOR,
    "node_ackid": _PERSISTED_COUNTER_OR_SENSOR,
    "node_temp": _PERSISTED_COUNTER_OR_SENSOR,
    "node_hum": _PERSISTED_COUNTER_OR_SENSOR,
    "node_press": _PERSISTED_COUNTER_OR_SENSOR,
    "node_temp2": _PERSISTED_COUNTER_OR_SENSOR,
    "node_gas_res": _PERSISTED_COUNTER_OR_SENSOR,
    "node_co2": _PERSISTED_COUNTER_OR_SENSOR,
}
assert len(EXCLUDED_FROM_SCHEMA) == 8


# ---------------------------------------------------------------------------
# Row parsing -- X(key, type, member[bracket], lo, hi, esc, has_esc)
# ---------------------------------------------------------------------------

# Deliberately does NOT anchor on a fixed argument count (CFG_NORANGE/
# CFG_NOESC collapse two logical params into one comma-free token, CFG_ESC(v)
# is itself one token containing a paren-nested comma-free call) -- only key,
# type and member are captured, followed by a bare comma, exactly like
# settings_persist_lint.py's ROW_RE. The type class includes digits
# (`CFG_[A-Z0-9_]+`) on purpose: a class without digits still matches
# "CFG_U32" up to the "3" and then fails on the "2", silently dropping every
# CFG_U32 row -- the exact trap docs/d1-04-...-triage-20260912.md's Method
# section calls out by name.
ROW_RE = re.compile(
    r'X\(\s*"([^"]+)"\s*,\s*(CFG_[A-Z0-9_]+)\s*,\s*'
    r'([A-Za-z_][A-Za-z0-9_]*)((?:\s*\[[^\]]*\])*)\s*,')


@dataclass(frozen=True)
class Row:
    key: str
    type_: str
    member_base: str
    member_full: str  # base name + normalised bracket suffix, e.g. "node_mcp17t[0]"


def _norm_bracket(b: str) -> str:
    out = []
    for grp in re.findall(r"\[[^\]]*\]", b):
        out.append("[" + "".join(grp[1:-1].split()) + "]")
    return "".join(out)


def parse_rows(text: str) -> List[Row]:
    rows = []
    for m in ROW_RE.finditer(text):
        key, type_, base, bracket = m.group(1), m.group(2), m.group(3), m.group(4)
        bracket = _norm_bracket(bracket)
        rows.append(Row(key=key, type_=type_, member_base=base,
                         member_full=base + bracket))
    return rows


# ---------------------------------------------------------------------------
# Macro-body extraction: a `#define NAME(X) ... \` block, once or twice
# (the CFG_FIELD_LIST_PLATFORM shape: once inside `#ifdef ESP32`, once
# inside the matching `#else`).
# ---------------------------------------------------------------------------

def _all_backslash_macro_bodies(text: str, macro_name: str) -> List[Tuple[int, str]]:
    """(line_index, body_text) for every `#define <macro_name>(X)`
    backslash-continued block in `text`, in file order."""
    lines = text.splitlines()
    pat = re.compile(r'^\s*#define\s+' + re.escape(macro_name) + r'\(X\)')
    results: List[Tuple[int, str]] = []
    i = 0
    while i < len(lines):
        if pat.search(lines[i]):
            start = i
            block = [lines[i]]
            while block[-1].rstrip().endswith("\\"):
                i += 1
                if i >= len(lines):
                    break
                block.append(lines[i])
            results.append((start, "\n".join(block)))
        i += 1
    return results


_BRANCH_MARKER_RE = re.compile(r'^\s*#\s*(ifdef\s+ESP32\b|else\b|endif\b)', re.I)


def _branch_before(lines: List[str], idx: int) -> Optional[str]:
    """Nearest of '#ifdef ESP32' / '#else' / '#endif' strictly before line
    `idx` -- 'esp32', 'nrf52' (the #else of that same ifdef), or None (an
    '#endif', or nothing -- unguarded at this point)."""
    for j in range(idx - 1, -1, -1):
        m = _BRANCH_MARKER_RE.search(lines[j])
        if m:
            kind = m.group(1).lower()
            if kind.startswith("ifdef"):
                return "esp32"
            if kind == "else":
                return "nrf52"
            return None
    return None


def extract_platform_macro(text: str, macro_name: str) -> Tuple[str, str]:
    """Body of `#define <macro_name>(X)` for (esp32, nrf52).

    - not defined at all -> ("", "")
    - defined once, no surrounding '#ifdef ESP32 / #else' branch -> the same
      body for both platforms (this is the shape SETTINGS_PERSIST_ONLY_LIST
      is expected to use if it turns out not to need a platform split)
    - defined twice, each inside its own branch of one '#ifdef ESP32 /
      #else' pair (the shape CFG_FIELD_LIST_PLATFORM already uses in
      src/config_json.h) -> (esp32 body, nrf52 body)
    - any other shape (e.g. two defines neither of which sits in a
      recognisable branch) -> falls back to the FIRST body for both
      platforms rather than silently dropping rows
    """
    lines = text.splitlines()
    bodies = _all_backslash_macro_bodies(text, macro_name)
    if not bodies:
        return "", ""
    if len(bodies) == 1:
        return bodies[0][1], bodies[0][1]
    esp32_body, nrf52_body = "", ""
    for start, body in bodies:
        branch = _branch_before(lines, start)
        if branch == "esp32" and not esp32_body:
            esp32_body = body
        elif branch == "nrf52" and not nrf52_body:
            nrf52_body = body
    if not esp32_body or not nrf52_body:
        esp32_body = nrf52_body = bodies[0][1]
    return esp32_body, nrf52_body


def load_config_json_schema(text: str) -> Tuple[List[Row], List[Row]]:
    """(esp32_rows, nrf52_rows): CFG_FIELD_LIST common rows plus each
    platform's CFG_FIELD_LIST_PLATFORM branch, from src/config_json.h."""
    common_bodies = _all_backslash_macro_bodies(text, "CFG_FIELD_LIST")
    common = common_bodies[0][1] if common_bodies else ""
    esp32_plat, nrf52_plat = extract_platform_macro(text, "CFG_FIELD_LIST_PLATFORM")
    return parse_rows(common) + parse_rows(esp32_plat), \
        parse_rows(common) + parse_rows(nrf52_plat)


def load_persist_only_schema(text: str) -> Tuple[List[Row], List[Row]]:
    """(esp32_rows, nrf52_rows) of SETTINGS_PERSIST_ONLY_LIST from
    src/settings_schema.h. `text` == "" (file missing, or not yet written by
    the sibling wave) simply yields ([], []) -- not a fatal condition here,
    see the module docstring's check 1.

    Mirrors CFG_FIELD_LIST/CFG_FIELD_LIST_PLATFORM's own two-level shape:
    SETTINGS_PERSIST_ONLY_LIST(X) itself is a single common definition (its
    body ends with a literal, unexpanded `SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)`
    invocation line, harmless here since that text does not match ROW_RE) that
    every platform shares, plus a SEPARATE SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)
    macro that -- as landed 2026-09-12 -- is itself `#ifdef ESP32`-branched
    (with a further BOARD_T_DECK* guard nested inside the ESP32 branch, which
    `extract_platform_macro`'s backward branch-marker scan does not need to
    understand specially: it only looks for the three literal '#ifdef ESP32'
    / '#else' / '#endif' markers and skips any other #if/#else it meets while
    scanning backward, so a nested, differently-worded guard is transparent
    to it). If SETTINGS_PERSIST_ONLY_LIST_PLATFORM does not exist at all
    (e.g. a simpler, unsplit future version of this file), `extract_platform_macro`
    returns ("", "") for it and the common rows alone still come through."""
    common_bodies = _all_backslash_macro_bodies(text, "SETTINGS_PERSIST_ONLY_LIST")
    common = common_bodies[0][1] if common_bodies else ""
    esp32_plat, nrf52_plat = extract_platform_macro(text, "SETTINGS_PERSIST_ONLY_LIST_PLATFORM")
    return parse_rows(common) + parse_rows(esp32_plat), \
        parse_rows(common) + parse_rows(nrf52_plat)


# ---------------------------------------------------------------------------
# ESP32 struct member parsing (src/esp32/esp32_flash.h)
# ---------------------------------------------------------------------------

FIELD_RE = re.compile(
    r"^\s*([A-Za-z_][\w:<>]*(?:\s+[A-Za-z_][\w:<>]*)*)"
    r"\s+(\w+)"
    r"((?:\s*\[[^\]]*\])*)"
    r"\s*(?:=[^;]*)?;\s*$")


def _strip_line_comments(text: str) -> str:
    return "\n".join(l.split("//", 1)[0] for l in text.splitlines())


def parse_esp32_struct_members(text: str) -> Set[str]:
    """Member names of `struct s_meshcom_settings` in esp32_flash.h. A field
    inside a `#if defined(BOARD_T_DECK...)` guard is still a real ESP32
    member on the boards where that macro is set (same convention
    settings_layout_lint.py and settings_persist_lint.py use for this same
    struct) -- only bare `#...` preprocessor lines are skipped, not the code
    they guard."""
    text = _strip_line_comments(text)
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
# preferences.put*() key extraction (src/esp32/esp32_flash.cpp)
# ---------------------------------------------------------------------------

PUT_KEY_RE = re.compile(r'preferences\.put[A-Za-z]+\(\s*"([^"]+)"')


def parse_put_keys(text: str) -> Set[str]:
    return set(PUT_KEY_RE.findall(_strip_line_comments(text)))


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

def find_duplicates(rows: List[Row], platform: str) -> List[str]:
    """Check 2. Duplicate NVS key or duplicate member_full within one
    platform's schema union. Array rows (distinct member_full per index) are
    not duplicates of each other -- see the module docstring."""
    violations: List[str] = []
    seen_keys: Dict[str, int] = {}
    seen_members: Dict[str, int] = {}
    for r in rows:
        seen_keys[r.key] = seen_keys.get(r.key, 0) + 1
        seen_members[r.member_full] = seen_members.get(r.member_full, 0) + 1
    for k, n in sorted(seen_keys.items()):
        if n > 1:
            violations.append(
                f'[{platform}] duplicate key "{k}" appears {n} times in the '
                f"schema union")
    for mfull, n in sorted(seen_members.items()):
        if n > 1:
            violations.append(
                f'[{platform}] duplicate member "{mfull}" appears {n} times '
                f"in the schema union")
    return violations


def check_coverage(all_members: Set[str], classification: Dict[str, str],
                    exclusions: Dict[str, str]) -> Tuple[List[str], List[str]]:
    """Check 3."""
    violations, excluded = [], []
    for member, cls in sorted(classification.items()):
        if cls != PERSIST or member in all_members:
            continue
        if member in exclusions:
            excluded.append(
                f'"{member}" is PERSIST per {TRIAGE_DOC} section 3 and not '
                f"in the schema union -- EXCLUDED (explained): "
                f"{exclusions[member]}")
        else:
            violations.append(
                f'"{member}" is classified PERSIST in {TRIAGE_DOC} section 3 '
                f"but is not in the schema union (CFG_FIELD_LIST[_PLATFORM] "
                f"or SETTINGS_PERSIST_ONLY_LIST) and is not on "
                f"EXCLUDED_FROM_SCHEMA")
    return violations, excluded


def check_no_runtime(all_members: Set[str], classification: Dict[str, str],
                      exclusions: Dict[str, str]
                      ) -> Tuple[List[str], List[str], List[str]]:
    """Check 4."""
    violations, excluded, unclassified = [], [], []
    for member in sorted(all_members):
        cls = classification.get(member)
        if cls == PERSIST:
            continue
        if cls == RUNTIME:
            if member in exclusions:
                excluded.append(
                    f'"{member}" is classified RUNTIME per {TRIAGE_DOC} '
                    f"section 3 but is in the schema union -- EXCLUDED "
                    f"(explained): {exclusions[member]}")
            else:
                violations.append(
                    f'"{member}" is classified RUNTIME in {TRIAGE_DOC} '
                    f"section 3 but appears in the schema union -- a "
                    f"runtime-only value should not be schema-persisted")
        else:
            unclassified.append(
                f'"{member}" is in the schema union but is not in the '
                f"{TRIAGE_DOC} section 3 147-field triage table at all -- "
                f"classify it there before adding it to the schema")
    return violations, excluded, unclassified


def check_persist_calls(x_esp32_rows: List[Row], esp32_struct_members: Set[str],
                         put_keys: Set[str]) -> List[str]:
    """Check 5 -- the DR-12/DR-13 disagreement-(a) assertion. Scoped to the
    X() union only (CFG_FIELD_LIST + CFG_FIELD_LIST_PLATFORM's ESP32
    branch), never SETTINGS_PERSIST_ONLY_LIST -- see the module docstring."""
    violations = []
    for r in x_esp32_rows:
        if r.member_base not in esp32_struct_members:
            continue
        if r.key not in put_keys:
            violations.append(
                f'"{r.key}" (member {r.member_base}) is in the X() union '
                f"with a real ESP32 struct member, but esp32_flash.cpp has "
                f"no preferences.put* call for that key (DR-12/DR-13)")
    return violations


# ---------------------------------------------------------------------------
# Check 6 -- the has_esc/CFG_ESC sentinel-clamp defect class (wave-gate
# follow-up, 2026-09-12). settings_schema.cpp's SETTINGS_SCHEMA_ROW macro
# originally computed `has_range` as `(lo <= hi)` alone, dropping `has_esc`
# as apparently unused. It is not: exactly one row (node_power) carries
# CFG_ESC(CFG_POWER_NOT_SET), a sentinel (-20, "no TX power stored yet")
# that sits deliberately OUTSIDE TX_POWER_MIN..MAX on several boards (RAK4631:
# 2..22). settings_store::decode() CLAMPS an out-of-envelope numeric into
# range whenever has_range is set -- so the original macro would have turned
# a factory-fresh node's -20 into the board minimum on the very first load,
# and settings_sanitize.cpp would then see a legitimate in-range value and
# never apply the real board default. Fixed 2026-09-12 to
# `(lo <= hi) && !(has_esc)`; this section is the static gate keeping it that
# way, plus a forward-looking check for the same bug SHAPE in any row (not
# just node_power).
#
# Two parts:
#   6.1 SETTINGS_SCHEMA_ROW's has_range expression must still depend on
#       has_esc (parsed straight from src/settings_schema.cpp's own source,
#       never edited by this script).
#   6.2 no row, anywhere in the schema union, may carry a REAL (non-
#       placeholder) escape-shaped value that lies outside its own declared
#       [lo, hi] while resolving to has_esc == 0 -- the general shape of the
#       defect. This deliberately does NOT flag a row using the ordinary
#       `CFG_NOESC` macro even when 0.0 (CFG_NOESC's own literal expansion)
#       falls outside that row's range (e.g. max_hop_text, range 1..6):
#       settings_store::decode() only ever reads `esc` when `has_esc` is
#       true, so CFG_NOESC's filler 0.0 is provably never read and is not a
#       "sentinel with nothing marking it" -- it is correctly marked as NO
#       sentinel at all. The bug this check exists for is a row that
#       bypasses CFG_NOESC/CFG_ESC entirely and writes its four trailing
#       values raw (`lo, hi, escape_value, 0`) -- exactly the shape that
#       would compile fine and silently misbehave.
# ---------------------------------------------------------------------------

def _split_top_level_args(s: str) -> List[str]:
    """Split `s` on top-level commas, respecting (), [] and "..." nesting --
    so a nested call like `CFG_ESC(CFG_POWER_NOT_SET)` stays one argument."""
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


def extract_x_calls(text: str) -> List[List[str]]:
    """Every `X(...)` call in `text`, as its raw, comma-split argument
    strings (paren/bracket/string-aware -- see `_split_top_level_args`).
    `X(` is matched only at a word boundary (`(?<![A-Za-z0-9_])X\\(`) so this
    can never trigger inside a longer identifier. An unbalanced call (should
    never happen in real source) is skipped, not guessed at."""
    calls: List[List[str]] = []
    for m in re.finditer(r'(?<![A-Za-z0-9_])X\(', text):
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
        args = _split_top_level_args(inner)
        if len(args) >= 3:
            calls.append(args)
    return calls


def extract_define_body_only(text: str, macro_name: str) -> Optional[str]:
    """Body of `#define <macro_name>(...)`, EXCLUDING the #define/parameter
    line itself -- just the expansion lines that follow it. None if the
    macro is not defined at all."""
    lines = text.splitlines()
    pat = re.compile(r'^\s*#define\s+' + re.escape(macro_name) + r'\s*\(')
    start = next((i for i, l in enumerate(lines) if pat.search(l)), None)
    if start is None:
        return None
    block = [lines[start]]
    i = start
    while block[-1].rstrip().endswith("\\"):
        i += 1
        if i >= len(lines):
            break
        block.append(lines[i])
    return "\n".join(block[1:])


def check_has_esc_folded(settings_schema_cpp_text: str
                          ) -> Tuple[List[str], Optional[str]]:
    """Check 6.1. Returns (violations, note). `note` is set instead of
    `violations` when SETTINGS_SCHEMA_ROW cannot be found at all (e.g.
    src/settings_schema.cpp has not landed yet) -- informational, not a
    finding, matching how a missing SETTINGS_PERSIST_ONLY_LIST is handled
    elsewhere in this script."""
    body = extract_define_body_only(strip_c_comments(settings_schema_cpp_text),
                                     "SETTINGS_SCHEMA_ROW")
    if body is None:
        return [], (f"SETTINGS_SCHEMA_ROW macro not found in "
                     f"{SETTINGS_SCHEMA_CPP} -- check 6.1 (has_esc folded "
                     f"into has_range) could not run")
    if not re.search(r'\bhas_esc\b', body):
        return [
            f"SETTINGS_SCHEMA_ROW's expansion in {SETTINGS_SCHEMA_CPP} no "
            f"longer references has_esc anywhere in its body -- its "
            f"has_range expression looks like it was simplified back to "
            f"'(lo <= hi)' alone, dropping the '&& !(has_esc)' term. If so, "
            f"settings_store::decode() will CLAMP any CFG_ESC sentinel that "
            f"lies outside [lo, hi] into range on load: node_power's "
            f"CFG_POWER_NOT_SET (-20) sits outside TX_POWER_MIN..MAX on "
            f"several boards (RAK4631: 2..22) on purpose, and a factory-"
            f"fresh node's -20 would silently become the board minimum on "
            f"first boot instead of triggering the board default"
        ], None
    return [], None


def strip_c_comments(text: str) -> str:
    """Strip `//` line comments and `/* ... */` block comments (the row
    macro's own documentation uses both). Safe here: no string literal in
    either schema file contains `//` or `/*`."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return "\n".join(l.split("//", 1)[0] for l in text.splitlines())


_DEFINE_NUM_RE = re.compile(
    r'^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+\(?\s*(-?\d+(?:\.\d+)?)\s*\)?\s*$')


def extract_numeric_defines(text: str) -> Dict[str, float]:
    """name -> value for every simple `#define NAME <int-or-float>` (an
    optional single layer of parens tolerated, e.g. `(-20)`) line in `text`.
    Deliberately narrow: anything more elaborate (an expression, a macro
    invocation) is left unresolved rather than guessed at."""
    out: Dict[str, float] = {}
    for line in strip_c_comments(text).splitlines():
        m = _DEFINE_NUM_RE.match(line)
        if m:
            out[m.group(1)] = float(m.group(2))
    return out


def resolve_numeric(text: Optional[str], macro_values: Dict[str, float]
                     ) -> Optional[float]:
    """A row-argument source string -> float, or None if it cannot be
    resolved. Handles a bare literal, one `(double)` cast, one layer of
    wrapping parens, and a lookup in `macro_values` (built by
    `extract_numeric_defines` over a small fixed set of headers -- see
    `check()`)."""
    if text is None:
        return None
    t = text.strip()
    t = re.sub(r'^\(\s*double\s*\)', '', t).strip()
    if t.startswith("(") and t.endswith(")"):
        inner = t[1:-1]
        if len(_split_top_level_args(inner)) == 1:
            t = inner.strip()
    if re.match(r'^-?\d+(\.\d+)?$', t):
        return float(t)
    return macro_values.get(t)


def parse_row_range_esc(tail_args: List[str]
                         ) -> Tuple[Optional[str], Optional[str], str, str, bool]:
    """`tail_args` = a row's raw arguments after `member` (2, 3 or 4 tokens,
    depending on whether CFG_NORANGE/CFG_NOESC/CFG_ESC were used, or the
    trailing values were written out raw). Returns
    `(lo_text, hi_text, esc_text, has_esc_text, esc_is_placeholder)`:

      - lo_text/hi_text are None when CFG_NORANGE was used (no declared
        range at all -- check 6.2 never applies to such a row).
      - has_esc_text is "0", "1", or "?" (could not be determined from an
        unrecognised trailing shape).
      - esc_is_placeholder is True ONLY for the canonical `CFG_NOESC` shape:
        its "0.0" is CFG_NOESC's own meaningless filler value, never read by
        settings_store::decode() (which only consults `esc` when `has_esc`
        is true) -- so it must never be compared against the row's range;
        see the check_escape_within_declared_range() docstring above."""
    args = list(tail_args)
    if args and args[0] == "CFG_NORANGE":
        lo_text, hi_text = None, None
        rest = args[1:]
    elif len(args) >= 2:
        lo_text, hi_text = args[0], args[1]
        rest = args[2:]
    else:
        return None, None, "", "?", False

    if len(rest) == 1:
        esc_arg = rest[0].strip()
        if esc_arg == "CFG_NOESC":
            return lo_text, hi_text, "0.0", "0", True
        m = re.match(r'^CFG_ESC\((.*)\)$', esc_arg, re.S)
        if m:
            return lo_text, hi_text, m.group(1).strip(), "1", False
        return lo_text, hi_text, esc_arg, "?", False
    if len(rest) == 2:
        return lo_text, hi_text, rest[0].strip(), rest[1].strip(), False
    return lo_text, hi_text, "", "?", False


def check_escape_within_declared_range(
        raw_calls: List[Tuple[str, str, str, List[str]]],
        macro_values: Dict[str, float]) -> Tuple[List[str], List[str]]:
    """Check 6.2. `raw_calls`: (source_label, key, member, tail_args) for
    every X() call across the whole schema union. Rows sharing the same
    (source_label, key) are evaluated once (a genuine duplicate is check 2's
    job, not this one's). Returns (violations, unresolved)."""
    violations, unresolved = [], []
    seen = set()
    for source, key, member, tail in raw_calls:
        dedupe_key = (source, key)
        if dedupe_key in seen:
            continue
        seen.add(dedupe_key)

        lo_t, hi_t, esc_t, has_esc_t, placeholder = parse_row_range_esc(tail)
        if placeholder or has_esc_t == "1":
            continue  # CFG_NOESC's own inert filler, or a properly-marked escape
        if has_esc_t == "?":
            unresolved.append(
                f'[{source}] "{key}" (member {member}): cannot determine '
                f"has_esc from its trailing arguments {tail!r} -- cannot "
                f"verify check 6.2 for this row; resolve by hand")
            continue
        if lo_t is None or hi_t is None:
            continue  # CFG_NORANGE -- no declared range to violate

        lo_v = resolve_numeric(lo_t, macro_values)
        hi_v = resolve_numeric(hi_t, macro_values)
        esc_v = resolve_numeric(esc_t, macro_values)
        if lo_v is None or hi_v is None or esc_v is None:
            unresolved.append(
                f'[{source}] "{key}" (member {member}): cannot resolve '
                f'lo="{lo_t}" hi="{hi_t}" escape="{esc_t}" to numbers -- '
                f"cannot verify check 6.2 for this row; resolve by hand")
            continue
        if lo_v > hi_v:
            continue  # written raw but still means "not range-checked"
        if not (lo_v <= esc_v <= hi_v):
            violations.append(
                f'[{source}] "{key}" (member {member}): escape value '
                f"{esc_v!r} lies outside its own declared range "
                f"[{lo_v!r}, {hi_v!r}] while has_esc is 0 -- a sentinel "
                f"value with nothing marking it as a sentinel; "
                f"settings_store::decode() will CLAMP it into range on the "
                f"very first load unless this row is wrapped in CFG_ESC(...)")
    return violations, unresolved


def collect_raw_calls(source_label: str, text: str) -> List[Tuple[str, str, str, List[str]]]:
    """(source_label, key, member, tail_args) for every X() call in a macro
    body's text -- the input `check_escape_within_declared_range` wants."""
    out = []
    for args in extract_x_calls(strip_c_comments(text)):
        key = args[0].strip().strip('"')
        member = re.sub(r'\s*\[.*$', '', args[2].strip()) if len(args) > 2 else args[2]
        out.append((source_label, key, member, args[3:]))
    return out


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

@dataclass
class AnalysisResult:
    esp32_union_count: int = 0
    nrf52_union_count: int = 0
    persist_only_row_count: int = 0
    persist_only_esp32_count: int = 0
    persist_only_nrf52_count: int = 0
    esp32_struct_member_count: int = 0
    put_key_count: int = 0
    fatal: List[str] = field(default_factory=list)
    violations: List[str] = field(default_factory=list)
    excluded: List[str] = field(default_factory=list)
    unclassified: List[str] = field(default_factory=list)
    unresolved: List[str] = field(default_factory=list)  # check 6.2 only
    notes: List[str] = field(default_factory=list)        # informational, e.g. check 6.1 skipped


def analyze(x_esp32_rows: List[Row], x_nrf52_rows: List[Row],
            persist_esp32_rows: List[Row], persist_nrf52_rows: List[Row],
            esp32_struct_members: Set[str], put_keys: Set[str],
            classification: Optional[Dict[str, str]] = None,
            exclusions: Optional[Dict[str, str]] = None) -> AnalysisResult:
    """Pure analysis over already-extracted data -- no file IO, so
    `--self-test` can drive it with synthetic fixtures. `classification`/
    `exclusions` default to the real pinned tables; tests pass their own so
    the checks can be proven independently of the real 147-field table."""
    cls_table = FIELD_CLASSIFICATION if classification is None else classification
    excl_table = EXCLUDED_FROM_SCHEMA if exclusions is None else exclusions

    esp32_union = x_esp32_rows + persist_esp32_rows
    nrf52_union = x_nrf52_rows + persist_nrf52_rows

    r = AnalysisResult(
        esp32_union_count=len(esp32_union), nrf52_union_count=len(nrf52_union),
        # Deliberately per-platform: the ESP32 and nRF52 persist-only unions
        # overlap in the 4 common bookkeeping rows, so their sum is not a
        # count of anything real. persist_only_row_count is kept as that sum
        # only because the self-suite uses it as a "did the list parse at
        # all" tripwire; the printed summary reports the two separately.
        persist_only_row_count=len(persist_esp32_rows) + len(persist_nrf52_rows),
        persist_only_esp32_count=len(persist_esp32_rows),
        persist_only_nrf52_count=len(persist_nrf52_rows),
        esp32_struct_member_count=len(esp32_struct_members),
        put_key_count=len(put_keys))

    if not x_esp32_rows and not x_nrf52_rows:
        r.fatal.append(
            "zero CFG_FIELD_LIST/CFG_FIELD_LIST_PLATFORM rows extracted -- "
            "the X() row pattern has stopped matching against "
            f"{CONFIG_JSON_H}; this is NOT a clean tree, it is a broken "
            "instrument")
    if not esp32_struct_members:
        r.fatal.append(
            f"zero members extracted from struct {STRUCT_NAME} in "
            f"{ESP32_FLASH_H} -- the struct pattern has stopped matching")
    if not put_keys:
        r.fatal.append(
            f"zero preferences.put* calls extracted from {ESP32_FLASH_CPP} "
            f"-- the call pattern has stopped matching")
    if r.fatal:
        return r

    # check 2
    r.violations.extend(find_duplicates(esp32_union, "esp32"))
    r.violations.extend(find_duplicates(nrf52_union, "nrf52"))

    # checks 3 & 4 operate over the combined (both-platform) member set
    all_members = {row.member_base for row in esp32_union + nrf52_union}

    cov_violations, cov_excluded = check_coverage(all_members, cls_table, excl_table)
    r.violations.extend(cov_violations)
    r.excluded.extend(cov_excluded)

    rt_violations, rt_excluded, rt_unclassified = check_no_runtime(
        all_members, cls_table, excl_table)
    r.violations.extend(rt_violations)
    r.excluded.extend(rt_excluded)
    r.unclassified.extend(rt_unclassified)

    # check 5
    r.violations.extend(
        check_persist_calls(x_esp32_rows, esp32_struct_members, put_keys))

    return r


def check(repo: Path = REPO) -> Tuple[AnalysisResult, bool]:
    """Runs the real tree. Returns (result, settings_schema_h_present)."""
    cfg_p = repo / CONFIG_JSON_H
    h_p = repo / ESP32_FLASH_H
    cpp_p = repo / ESP32_FLASH_CPP
    required_missing = [p for p in (cfg_p, h_p, cpp_p) if not p.exists()]
    if required_missing:
        r = AnalysisResult()
        r.fatal.extend(f"{p} does not exist" for p in required_missing)
        return r, False

    cfg_text = cfg_p.read_text()
    x_esp32_rows, x_nrf52_rows = load_config_json_schema(cfg_text)

    schema_p = repo / SETTINGS_SCHEMA_H
    schema_present = schema_p.exists()
    schema_text = schema_p.read_text() if schema_present else ""
    persist_esp32_rows, persist_nrf52_rows = load_persist_only_schema(schema_text)

    esp32_struct_members = parse_esp32_struct_members(h_p.read_text())
    cpp_text = cpp_p.read_text()

    # See check 5's docstring ("SCHEMA-DRIVEN MODE"): once esp32_flash.cpp
    # calls settings_schema::fields() generically instead of issuing one
    # preferences.put*("literal key", ...) per field, there is no literal
    # call left for parse_put_keys() to find. Two occurrences of the marker
    # (load and save each walk the table once) distinguishes "the mechanism
    # moved" from "the mechanism vanished" -- one stray mention in a comment
    # would not clear this bar.
    schema_driven = cpp_text.count("settings_schema::fields()") >= 2
    if schema_driven:
        put_keys = {row.key for row in x_esp32_rows + persist_esp32_rows}
    else:
        put_keys = parse_put_keys(cpp_text)

    r = analyze(x_esp32_rows, x_nrf52_rows, persist_esp32_rows,
                persist_nrf52_rows, esp32_struct_members, put_keys)
    if schema_driven:
        r.notes.append(
            "esp32_flash.cpp is schema-driven (settings_schema::fields() "
            "walk, D1-04 W3 Task 1): check 5 used the ESP32 schema union's "
            "own key set in place of a literal preferences.put* parse -- "
            "see check 5's docstring")

    # --- check 6 -----------------------------------------------------------
    schema_cpp_p = repo / SETTINGS_SCHEMA_CPP
    if schema_cpp_p.exists():
        esc_violations, esc_note = check_has_esc_folded(schema_cpp_p.read_text())
        r.violations.extend(esc_violations)
        if esc_note:
            r.notes.append(esc_note)
    else:
        r.notes.append(
            f"{SETTINGS_SCHEMA_CPP} does not exist -- check 6.1 (has_esc "
            f"folded into has_range) could not run")

    macro_values = extract_numeric_defines(cfg_text)
    maxhop_p = repo / MAXHOP_H
    if maxhop_p.exists():
        macro_values.update(extract_numeric_defines(maxhop_p.read_text()))
    if schema_cpp_p.exists():
        # TX_POWER_MIN/MAX are board-specific (variants/*/configuration.h);
        # this is the native/no-variant fallback settings_schema.cpp itself
        # defines under #ifndef -- the one value obtainable without picking
        # a board. See check_escape_within_declared_range()'s docstring: it
        # only matters for a raw (non-macro) row, and none exists today.
        macro_values.update(extract_numeric_defines(schema_cpp_p.read_text()))

    raw_calls: List[Tuple[str, str, str, List[str]]] = []
    common_cfg = _all_backslash_macro_bodies(cfg_text, "CFG_FIELD_LIST")
    if common_cfg:
        raw_calls += collect_raw_calls("CFG_FIELD_LIST", common_cfg[0][1])
    cfg_esp32_plat, cfg_nrf52_plat = extract_platform_macro(cfg_text, "CFG_FIELD_LIST_PLATFORM")
    raw_calls += collect_raw_calls("CFG_FIELD_LIST_PLATFORM[esp32]", cfg_esp32_plat)
    raw_calls += collect_raw_calls("CFG_FIELD_LIST_PLATFORM[nrf52]", cfg_nrf52_plat)
    if schema_present:
        common_persist = _all_backslash_macro_bodies(schema_text, "SETTINGS_PERSIST_ONLY_LIST")
        if common_persist:
            raw_calls += collect_raw_calls("SETTINGS_PERSIST_ONLY_LIST", common_persist[0][1])
        p_esp32_plat, p_nrf52_plat = extract_platform_macro(
            schema_text, "SETTINGS_PERSIST_ONLY_LIST_PLATFORM")
        raw_calls += collect_raw_calls("SETTINGS_PERSIST_ONLY_LIST_PLATFORM[esp32]", p_esp32_plat)
        raw_calls += collect_raw_calls("SETTINGS_PERSIST_ONLY_LIST_PLATFORM[nrf52]", p_nrf52_plat)

    esc_range_violations, esc_range_unresolved = check_escape_within_declared_range(
        raw_calls, macro_values)
    r.violations.extend(esc_range_violations)
    r.unresolved.extend(esc_range_unresolved)

    return r, schema_present


# ---------------------------------------------------------------------------
# --self-test
# ---------------------------------------------------------------------------

def _fake_platform_macro(macro_name: str, esp32_rows: str, nrf52_rows: str) -> str:
    return (
        "#ifdef ESP32\n"
        f"    #define {macro_name}(X)                       \\\n"
        + esp32_rows +
        "#else\n"
        f"    #define {macro_name}(X)                       \\\n"
        + nrf52_rows +
        "#endif\n")


def _fake_struct_h(members: str) -> str:
    return f"struct {STRUCT_NAME}\n{{\n{members}\n}};\n"


def self_test() -> int:
    ok = True

    def report(name: str, good: bool, detail: str = "") -> None:
        nonlocal ok
        print(f"  {'ok ' if good else 'FAIL'} {name}{(': ' + detail) if detail else ''}")
        ok = ok and good

    # --- row parsing, including the CFG_U32-undercounting trap and an array field ---
    sample = (
        '    X("node_call",    CFG_STR, node_call,    CFG_NORANGE, CFG_NOESC) \\\n'
        '    X("node_gpsbaud", CFG_U32,  node_gpsbaud, 1200.0, 921600.0, CFG_NOESC) \\\n'
        '    X("node_power",   CFG_INT,  node_power,   0.0, 30.0, CFG_ESC(CFG_POWER_NOT_SET)) \\\n'
        '    X("node_gcb0",    CFG_INT,  node_gcb[0],  CFG_NORANGE, CFG_NOESC) \\\n'
        '    X("node_gcb1",    CFG_INT,  node_gcb[1],  CFG_NORANGE, CFG_NOESC) \\\n'
    )
    rows = parse_rows(sample)
    report("parse_rows: 5 rows found, including the CFG_U32 row "
           "(digit-inclusive type class)",
           len(rows) == 5, f"{len(rows)} row(s): {[r.key for r in rows]}")
    gpsbaud = next((r for r in rows if r.key == "node_gpsbaud"), None)
    report("parse_rows: CFG_U32 row's member captured correctly (not "
           "truncated by a digit-less type class)",
           gpsbaud is not None and gpsbaud.member_base == "node_gpsbaud")
    gcb_members = sorted(r.member_full for r in rows if r.member_base == "node_gcb")
    report("parse_rows: array rows get distinct member_full per index",
           gcb_members == ["node_gcb[0]", "node_gcb[1]"], repr(gcb_members))

    # --- extract_platform_macro: unguarded (single) vs ifdef-branched vs absent ---
    unguarded = ('#define SOME_LIST(X)                       \\\n'
                 '    X("a", CFG_INT, a, CFG_NORANGE, CFG_NOESC)\n')
    e, n = extract_platform_macro(unguarded, "SOME_LIST")
    report("extract_platform_macro: unguarded single define -> same body both platforms",
           e == n and 'X("a"' in e)

    branched = _fake_platform_macro(
        "CFG_FIELD_LIST_PLATFORM",
        '        X("node_disrot", CFG_INT, node_disp_rot, CFG_NORANGE, CFG_NOESC)\n',
        '        X("send_repeat_time", CFG_U32, send_repeat_time, CFG_NORANGE, CFG_NOESC)\n')
    e, n = extract_platform_macro(branched, "CFG_FIELD_LIST_PLATFORM")
    report("extract_platform_macro: ifdef ESP32/#else -> distinct bodies per platform",
           "node_disrot" in e and "node_disrot" not in n
           and "send_repeat_time" in n and "send_repeat_time" not in e)

    e, n = extract_platform_macro("nothing here", "SETTINGS_PERSIST_ONLY_LIST")
    report("extract_platform_macro: macro absent entirely -> empty for both platforms",
           e == "" and n == "")

    # --- load_persist_only_schema: the real settings_schema.h shape --
    # SETTINGS_PERSIST_ONLY_LIST(X) common rows ending in an unexpanded
    # invocation of SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X), which is itself
    # '#ifdef ESP32'-branched with a FURTHER, differently-worded guard
    # (BOARD_T_DECK) nested inside the ESP32 branch -- exactly the shape
    # landed in src/settings_schema.h 2026-09-12.
    persist_only_fixture = (
        "#define SETTINGS_PERSIST_ONLY_LIST(X)                       \\\n"
        '    X("node_fversion", CFG_INT, node_fversion, CFG_NORANGE, CFG_NOESC) \\\n'
        "    SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)\n"
        "\n"
        "#ifdef ESP32\n"
        "    #if defined(BOARD_T_DECK)\n"
        "        #define SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)      \\\n"
        '            X("node_map", CFG_INT, node_map, CFG_NORANGE, CFG_NOESC)\n'
        "    #else\n"
        "        #define SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)\n"
        "    #endif\n"
        "#else\n"
        "    #define SETTINGS_PERSIST_ONLY_LIST_PLATFORM(X)\n"
        "#endif\n")
    p_esp32, p_nrf52 = load_persist_only_schema(persist_only_fixture)
    report("load_persist_only_schema: common row present on both platforms, "
           "T-Deck-guarded platform row reaches ONLY esp32 despite the "
           "nested (non-ESP32-worded) #if guard around it",
           {r.key for r in p_esp32} == {"node_fversion", "node_map"}
           and {r.key for r in p_nrf52} == {"node_fversion"},
           f"esp32={[r.key for r in p_esp32]} nrf52={[r.key for r in p_nrf52]}")

    # --- ESP32 struct member parsing, including a T-Deck-guarded field ---
    struct_h = _fake_struct_h(
        "    char node_call[10] = {0};\n"
        "    int node_gpsbaud = 0;\n"
        "    #if defined(BOARD_T_DECK)\n"
        "    int node_map = 0;\n"
        "    #endif\n")
    members = parse_esp32_struct_members(struct_h)
    report("parse_esp32_struct_members: includes a guarded (#if) field",
           members == {"node_call", "node_gpsbaud", "node_map"}, repr(sorted(members)))
    report("parse_esp32_struct_members: empty struct -> empty set (feeds FATAL)",
           parse_esp32_struct_members(_fake_struct_h("// nothing here\n")) == set())

    # --- put key parsing ---
    flash_cpp = (
        'preferences.putInt("node_call", meshcom_settings.node_call);\n'
        'preferences.putString("node_gwsrv", strVar);\n'
        'meshcom_settings.node_call = preferences.getInt("node_call", 0);\n'
    )
    put_keys = parse_put_keys(flash_cpp)
    report("parse_put_keys: both put calls captured, get call ignored",
           put_keys == {"node_call", "node_gwsrv"}, repr(put_keys))

    # ------------------------------------------------------------------
    # check 6.1 -- SETTINGS_SCHEMA_ROW's has_range expression must
    # reference has_esc. Fixtures only, never the real src/settings_schema.cpp.
    # ------------------------------------------------------------------
    schema_cpp_ok = (
        "#define SETTINGS_SCHEMA_ROW(key, cfgtype, member, lo, hi, esc, has_esc) \\\n"
        "    {key, CfgTypeToFieldType(cfgtype), offsetof(s_meshcom_settings, member), \\\n"
        "     sizeof(((s_meshcom_settings *)0)->member), \\\n"
        "     ((double)(lo) <= (double)(hi)) && !(has_esc), (double)(lo), (double)(hi)},\n")
    v, note = check_has_esc_folded(schema_cpp_ok)
    report("check 6.1: has_range expression references has_esc -> clean",
           not v and note is None, f"violations={v} note={note}")

    schema_cpp_simplified = (
        "#define SETTINGS_SCHEMA_ROW(key, cfgtype, member, lo, hi, esc, has_esc) \\\n"
        "    {key, CfgTypeToFieldType(cfgtype), offsetof(s_meshcom_settings, member), \\\n"
        "     sizeof(((s_meshcom_settings *)0)->member), \\\n"
        "     ((double)(lo) <= (double)(hi)), (double)(lo), (double)(hi)},\n")
    v, note = check_has_esc_folded(schema_cpp_simplified)
    report("check 6.1: has_esc term removed from has_range -> violation",
           bool(v) and note is None and any("has_esc" in x for x in v), f"violations={v}")

    v, note = check_has_esc_folded("// no SETTINGS_SCHEMA_ROW macro in this text at all\n")
    report("check 6.1: macro not found at all -> informational note, not a violation",
           not v and note is not None and "not found" in note, f"violations={v} note={note}")

    # ------------------------------------------------------------------
    # check 6.2 -- an escape-shaped value outside its own declared range
    # while has_esc == 0. Fixtures only.
    # ------------------------------------------------------------------
    macro_values = {"TX_POWER_MIN": -20.0, "TX_POWER_MAX": 30.0}

    # The real node_power shape (CFG_ESC, has_esc=1) -- never flagged by
    # 6.2, that is exactly what 6.1 guards instead.
    node_power_calls = collect_raw_calls(
        "TEST",
        '    X("node_power", CFG_INT, node_power, (double)TX_POWER_MIN, '
        '(double)TX_POWER_MAX, CFG_ESC(CFG_POWER_NOT_SET))\n')
    v, u = check_escape_within_declared_range(
        node_power_calls, dict(macro_values, CFG_POWER_NOT_SET=-20.0))
    report("check 6.2: a properly CFG_ESC-marked sentinel (has_esc=1) is "
           "never flagged, however far outside its range",
           not v and not u, f"violations={v} unresolved={u}")

    # The false-positive trap this check must NOT fall into: an ordinary
    # CFG_NOESC row whose declared range excludes 0 (max_hop_text's real
    # shape, range 1..6) -- CFG_NOESC's own "0.0" filler is never read by
    # decode() when has_esc is false, so it must not be compared to the range.
    maxhop_calls = collect_raw_calls(
        "TEST",
        '    X("max_hop_text", CFG_INT, max_hop_text, (double)MAXHOP_TEXT_MIN, '
        '(double)MAXHOP_TEXT_MAX, CFG_NOESC)\n')
    v, u = check_escape_within_declared_range(
        maxhop_calls, dict(macro_values, MAXHOP_TEXT_MIN=1.0, MAXHOP_TEXT_MAX=6.0))
    report("check 6.2: an ordinary CFG_NOESC row is NOT flagged even though "
           "0.0 (CFG_NOESC's own filler) sits outside its 1..6 range -- the "
           "false-positive trap this check exists to avoid",
           not v and not u, f"violations={v} unresolved={u}")

    # The actual bug SHAPE: a row that bypasses CFG_NOESC/CFG_ESC and writes
    # its four trailing values raw, with a genuine out-of-range escape and
    # has_esc left/written as 0 -- "a sentinel with nothing marking it".
    raw_bad_calls = collect_raw_calls(
        "TEST",
        '    X("node_bogus", CFG_INT, node_bogus, 2.0, 22.0, -20.0, 0)\n')
    v, u = check_escape_within_declared_range(raw_bad_calls, macro_values)
    report("check 6.2: raw row with an out-of-range escape and has_esc "
           "written as 0 -> violation (the general bug shape)",
           bool(v) and not u and any("node_bogus" in x and "-20.0" in x for x in v),
           f"violations={v}")

    # Same raw shape, but the escape value legitimately sits inside range --
    # not a finding (nothing wrong with a raw-but-in-range value).
    raw_ok_calls = collect_raw_calls(
        "TEST", '    X("node_ok", CFG_INT, node_ok, 2.0, 22.0, 10.0, 0)\n')
    v, u = check_escape_within_declared_range(raw_ok_calls, macro_values)
    report("check 6.2: raw row with an in-range value -> clean",
           not v and not u, f"violations={v} unresolved={u}")

    # An unresolvable macro in lo/hi -> UNRESOLVED, never a silent pass.
    raw_unresolved_calls = collect_raw_calls(
        "TEST",
        '    X("node_weird", CFG_INT, node_weird, SOME_UNKNOWN_MIN, '
        'SOME_UNKNOWN_MAX, -999.0, 0)\n')
    v, u = check_escape_within_declared_range(raw_unresolved_calls, macro_values)
    report("check 6.2: unresolvable lo/hi macro -> UNRESOLVED, not a "
           "silent pass and not a false violation",
           not v and len(u) == 1 and "node_weird" in u[0], f"violations={v} unresolved={u}")

    # CFG_NORANGE rows are never in scope for 6.2 (no declared range to
    # violate), whatever their escape shape.
    norange_calls = collect_raw_calls(
        "TEST", '    X("node_free", CFG_INT, node_free, CFG_NORANGE, -999.0, 0)\n')
    v, u = check_escape_within_declared_range(norange_calls, macro_values)
    report("check 6.2: CFG_NORANGE row is out of scope regardless of escape shape",
           not v and not u, f"violations={v} unresolved={u}")

    report("resolve_numeric: (double) cast and wrapping parens are stripped",
           resolve_numeric("(double)(-20.0)", {}) == -20.0
           and resolve_numeric("(-20)", {}) == -20.0
           and resolve_numeric("TX_POWER_MIN", {"TX_POWER_MIN": -20.0}) == -20.0
           and resolve_numeric("UNKNOWN", {}) is None)

    # ------------------------------------------------------------------
    # analyze(): one case per check, plus clean and FATAL/void cases.
    # A tiny synthetic classification/exclusion table is used throughout
    # so these cases test the MECHANISM, not the real 147-field table.
    # ------------------------------------------------------------------
    fake_cls = {"node_call": PERSIST, "node_alt": PERSIST}
    fake_excl: Dict[str, str] = {}

    clean_x_esp32 = [Row("node_call", "CFG_STR", "node_call", "node_call"),
                      Row("node_alt", "CFG_INT", "node_alt", "node_alt")]
    clean_x_nrf52 = list(clean_x_esp32)
    clean_members = {"node_call", "node_alt"}
    clean_put_keys = {"node_call", "node_alt"}

    def run_case(x_esp32=None, x_nrf52=None, persist_esp32=None, persist_nrf52=None,
                 members=None, put=None, cls=None, excl=None) -> AnalysisResult:
        return analyze(
            x_esp32 if x_esp32 is not None else list(clean_x_esp32),
            x_nrf52 if x_nrf52 is not None else list(clean_x_nrf52),
            persist_esp32 if persist_esp32 is not None else [],
            persist_nrf52 if persist_nrf52 is not None else [],
            members if members is not None else set(clean_members),
            put if put is not None else set(clean_put_keys),
            classification=cls if cls is not None else dict(fake_cls),
            exclusions=excl if excl is not None else dict(fake_excl))

    r = run_case()
    report("clean case: no fatal, no violations",
           not r.fatal and not r.violations, str(r.violations))

    # check 2: duplicate member (same key AND member twice)
    dup_rows = list(clean_x_esp32) + [Row("node_call", "CFG_STR", "node_call", "node_call")]
    r = run_case(x_esp32=dup_rows)
    report("check 2: duplicate key/member -> violation naming both",
           not r.fatal
           and any("duplicate key" in v and "node_call" in v for v in r.violations)
           and any("duplicate member" in v and "node_call" in v for v in r.violations),
           str(r.violations))

    # check 2: array rows with distinct indices are NOT duplicates
    array_rows = list(clean_x_esp32) + [
        Row("node_gcb0", "CFG_INT", "node_gcb", "node_gcb[0]"),
        Row("node_gcb1", "CFG_INT", "node_gcb", "node_gcb[1]")]
    r = run_case(x_esp32=array_rows, members=clean_members | {"node_gcb"},
                 put=clean_put_keys | {"node_gcb0", "node_gcb1"},
                 cls=dict(fake_cls, node_gcb=PERSIST))
    report("check 2: array field's distinct-index rows are NOT flagged as duplicates",
           not r.fatal and not any("duplicate" in v for v in r.violations),
           str(r.violations))

    # check 3: PERSIST field missing from the schema union entirely
    persist_cls = dict(fake_cls, node_owgpio=PERSIST)
    r = run_case(cls=persist_cls)
    report("check 3: PERSIST field absent from schema union -> violation",
           not r.fatal and any("node_owgpio" in v and "not in the schema union" in v
                                for v in r.violations),
           str(r.violations))
    r_excl = run_case(cls=persist_cls,
                       excl={"node_owgpio": "TEST: pretend documented elsewhere"})
    report("check 3: same finding downgraded to EXCLUDED once named in the "
           "exclusion table",
           not r_excl.fatal
           and not any("node_owgpio" in v for v in r_excl.violations)
           and any("node_owgpio" in e and "EXCLUDED" in e for e in r_excl.excluded),
           str(r_excl.excluded))

    # check 4: RUNTIME field present in the schema union
    rt_rows = list(clean_x_esp32) + [Row("node_ntc", "CFG_FLT", "node_ntctemp", "node_ntctemp")]
    runtime_cls = dict(fake_cls, node_ntctemp=RUNTIME)
    r = run_case(x_esp32=rt_rows, members=clean_members | {"node_ntctemp"},
                 put=clean_put_keys | {"node_ntc"}, cls=runtime_cls)
    report("check 4: RUNTIME-classified member in the schema -> violation",
           not r.fatal and any("node_ntctemp" in v and "RUNTIME" in v
                                for v in r.violations),
           str(r.violations))
    r_excl = run_case(x_esp32=rt_rows, members=clean_members | {"node_ntctemp"},
                       put=clean_put_keys | {"node_ntc"}, cls=runtime_cls,
                       excl={"node_ntctemp": "TEST: pretend this is a documented exception"})
    report("check 4: same finding downgraded to EXCLUDED once named in the "
           "exclusion table",
           not r_excl.fatal
           and not any("node_ntctemp" in v for v in r_excl.violations)
           and any("node_ntctemp" in e and "EXCLUDED" in e for e in r_excl.excluded),
           str(r_excl.excluded))

    # check 4 (unclassified variant): schema member the 147-field table has
    # never heard of at all
    unknown_rows = list(clean_x_esp32) + [Row("node_zzz", "CFG_INT", "node_totally_unknown", "node_totally_unknown")]
    r = run_case(x_esp32=unknown_rows, members=clean_members | {"node_totally_unknown"},
                 put=clean_put_keys | {"node_zzz"})
    report("check 4: member absent from FIELD_CLASSIFICATION entirely -> "
           "UNCLASSIFIED, not silently accepted, not a plain VIOLATION",
           not r.fatal and not any("node_totally_unknown" in v for v in r.violations)
           and any("node_totally_unknown" in u for u in r.unclassified),
           str(r.unclassified))

    # check 5: X() row with a real ESP32 member but no preferences.put* call
    missing_put_rows = list(clean_x_esp32) + [Row("node_owgpio", "CFG_INT", "node_owgpio", "node_owgpio")]
    r = run_case(x_esp32=missing_put_rows, members=clean_members | {"node_owgpio"},
                 cls=persist_cls)
    # node_owgpio has no matching key in clean_put_keys -> check 5 should fire.
    report("check 5: X() row with a real ESP32 member and no put() call -> violation",
           not r.fatal and any("no preferences.put* call" in v for v in r.violations),
           str(r.violations))

    # SETTINGS_PERSIST_ONLY_LIST rows count toward coverage (check 3) even
    # though they are absent from the X() union
    r = run_case(persist_esp32=[Row("node_owgpio", "CFG_INT", "node_owgpio", "node_owgpio")],
                 members=clean_members | {"node_owgpio"},
                 put=clean_put_keys | {"node_owgpio"}, cls=persist_cls)
    report("SETTINGS_PERSIST_ONLY_LIST row satisfies check 3 coverage on its own",
           not r.fatal and not any("node_owgpio" in v and "not in the schema union" in v
                                    for v in r.violations),
           str(r.violations))

    # --- FATAL / void-check paths ---
    r = analyze([], [], [], [], clean_members, clean_put_keys)
    report("FATAL: zero X() rows on both platforms",
           bool(r.fatal) and not r.violations and "X() row pattern" in r.fatal[0])

    r = analyze(clean_x_esp32, clean_x_nrf52, [], [], set(), clean_put_keys)
    report("FATAL: zero ESP32 struct members",
           bool(r.fatal) and "members extracted" in r.fatal[0])

    r = analyze(clean_x_esp32, clean_x_nrf52, [], [], clean_members, set())
    report("FATAL: zero preferences.put* calls",
           bool(r.fatal) and "preferences.put*" in r.fatal[0])

    # An entirely empty (or missing) SETTINGS_PERSIST_ONLY_LIST is NOT
    # fatal, and not itself a violation -- see check() and the module
    # docstring's check 1.
    r = analyze(clean_x_esp32, clean_x_nrf52, [], [],
                clean_members, clean_put_keys,
                classification={"node_call": PERSIST, "node_alt": PERSIST},
                exclusions={})
    report("empty SETTINGS_PERSIST_ONLY_LIST is not fatal and not a finding "
           "by itself",
           not r.fatal and not r.violations and r.persist_only_row_count == 0,
           f"fatal={r.fatal} violations={r.violations}")

    # check(): missing src/settings_schema.h on disk is tolerated (real
    # config_json.h/esp32_flash.* still required to exist)
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "src" / "esp32").mkdir(parents=True)
        (root / "src" / "config_json.h").write_text(
            "#define CFG_FIELD_LIST(X)                       \\\n"
            '    X("node_call", CFG_STR, node_call, CFG_NORANGE, CFG_NOESC) \\\n'
            "    CFG_FIELD_LIST_PLATFORM(X)\n"
            + _fake_platform_macro("CFG_FIELD_LIST_PLATFORM", "", ""))
        (root / "src" / "esp32" / "esp32_flash.h").write_text(
            _fake_struct_h("    char node_call[10] = {0};\n"))
        (root / "src" / "esp32" / "esp32_flash.cpp").write_text(
            'preferences.putString("node_call", meshcom_settings.node_call);\n')
        result, schema_present = check(repo=root)
        report("check(): missing src/settings_schema.h -> not fatal, "
               "reported as absent",
               not result.fatal and not schema_present,
               f"fatal={result.fatal} schema_present={schema_present}")

    with tempfile.TemporaryDirectory() as d:
        result, _ = check(repo=Path(d) / "does-not-exist")
        report("check(): required source files missing entirely -> FATAL",
               bool(result.fatal) and all("does not exist" in f for f in result.fatal))

    # check(): schema-driven esp32_flash.cpp (D1-04 W3 Task 1 shape) -- a
    # settings_schema::fields() walk with NO literal preferences.put*(...)
    # call sites must not FATAL on "zero put calls found" and must not raise
    # a wave of false check-5 violations (one per real field) just because
    # parse_put_keys() has nothing left to match.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "src" / "esp32").mkdir(parents=True)
        (root / "src" / "config_json.h").write_text(
            "#define CFG_FIELD_LIST(X)                       \\\n"
            '    X("node_call", CFG_STR, node_call, CFG_NORANGE, CFG_NOESC) \\\n'
            "    CFG_FIELD_LIST_PLATFORM(X)\n"
            + _fake_platform_macro("CFG_FIELD_LIST_PLATFORM", "", ""))
        (root / "src" / "esp32" / "esp32_flash.h").write_text(
            _fake_struct_h("    char node_call[10] = {0};\n"))
        (root / "src" / "esp32" / "esp32_flash.cpp").write_text(
            "void load() {\n"
            "    for (size_t i = 0; i < settings_schema::fieldCount(); i++)\n"
            "        loadFieldFromPreferences(settings_schema::fields()[i], &meshcom_settings);\n"
            "}\n"
            "void save() {\n"
            "    for (size_t i = 0; i < settings_schema::fieldCount(); i++)\n"
            "        saveFieldToPreferences(settings_schema::fields()[i], &meshcom_settings);\n"
            "}\n")
        result, _ = check(repo=root)
        # NOTE: this fixture's config_json.h has only ONE X() row, so
        # checks 3/4 (coverage vs. the REAL 147-field FIELD_CLASSIFICATION
        # table, which check() always uses -- it takes no override) fire a
        # wall of unrelated noise regardless of schema-driven detection; that
        # is not what this case is testing. What matters here is specific to
        # check 5: no FATAL from an empty put_keys void-check, no check-5
        # "no preferences.put* call" finding for node_call (which IS in this
        # fixture's tiny schema), and the substitution note is present.
        report("check(): schema-driven esp32_flash.cpp -> not fatal, no "
               "check-5 violation for the one field this fixture covers, "
               "and a note explains the substitution",
               not result.fatal
               and not any("no preferences.put* call" in v for v in result.violations)
               and any("schema-driven" in n for n in result.notes),
               f"fatal={result.fatal} violations={result.violations} notes={result.notes}")

    # Mutation: only ONE settings_schema::fields() occurrence (e.g. someone
    # ported the load side but left save hand-written, or vice versa) must
    # NOT be treated as schema-driven -- the real legacy parse_put_keys()
    # runs instead, finds zero literal preferences.put*(...) calls in this
    # fixture (there are none), and correctly FATALs as a broken instrument
    # rather than silently passing check 5 the way the >=2 branch would.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "src" / "esp32").mkdir(parents=True)
        (root / "src" / "config_json.h").write_text(
            "#define CFG_FIELD_LIST(X)                       \\\n"
            '    X("node_call", CFG_STR, node_call, CFG_NORANGE, CFG_NOESC) \\\n'
            "    CFG_FIELD_LIST_PLATFORM(X)\n"
            + _fake_platform_macro("CFG_FIELD_LIST_PLATFORM", "", ""))
        (root / "src" / "esp32" / "esp32_flash.h").write_text(
            _fake_struct_h("    char node_call[10] = {0};\n"))
        (root / "src" / "esp32" / "esp32_flash.cpp").write_text(
            "void load() {\n"
            "    for (size_t i = 0; i < settings_schema::fieldCount(); i++)\n"
            "        loadFieldFromPreferences(settings_schema::fields()[i], &meshcom_settings);\n"
            "}\n")
        result, _ = check(repo=root)
        report("check(): only ONE settings_schema::fields() occurrence -> "
               "NOT treated as schema-driven, legacy parse runs and FATALs "
               "on the real zero-put-calls void-check instead of silently "
               "passing check 5",
               bool(result.fatal)
               and any("preferences.put*" in f for f in result.fatal),
               f"fatal={result.fatal} violations={result.violations}")

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", "--selftest", dest="self_test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    result, schema_present = check()
    for f in result.fatal:
        print(f"FATAL: {f}")
    if result.fatal:
        return 1

    print(f"{result.esp32_union_count} esp32 schema-union row(s), "
          f"{result.nrf52_union_count} nrf52 schema-union row(s) "
          f"(SETTINGS_PERSIST_ONLY_LIST contributes "
          f"{result.persist_only_esp32_count} / {result.persist_only_nrf52_count} "
          f"of those; the two platform lists share 4 common rows, so summing "
          f"them would double-count), "
          f"{result.esp32_struct_member_count} ESP32 struct member(s), "
          f"{result.put_key_count} preferences.put* call(s)")
    if not schema_present:
        print(f"\nNOTE: {SETTINGS_SCHEMA_H} does not exist yet -- "
              f"SETTINGS_PERSIST_ONLY_LIST was treated as empty. This gate "
              f"still ran checks 1/2/5 against CFG_FIELD_LIST alone; checks "
              f"3 and 4 will only be meaningful once that file lands. "
              f"Re-run once it does.")

    for n in result.notes:
        print(f"\nNOTE: {n}")

    if result.unclassified:
        print(f"\n{len(result.unclassified)} schema member(s) not found in "
              f"{TRIAGE_DOC} section 3 at all:")
        for u in result.unclassified:
            print(f"  UNCLASSIFIED {u}")

    if result.unresolved:
        print(f"\n{len(result.unresolved)} row(s) check 6.2 could not "
              f"resolve (parser limitation, not a confirmed finding either "
              f"way -- resolve by hand):")
        for u in result.unresolved:
            print(f"  UNRESOLVED {u}")

    if result.excluded:
        print(f"\n{len(result.excluded)} finding(s) downgraded by "
              f"EXCLUDED_FROM_SCHEMA:")
        for e in result.excluded:
            print(f"  EXCLUDED {e}")

    if result.violations:
        print(f"\n{len(result.violations)} violation(s):")
        for v in result.violations:
            print(f"  VIOLATION {v}")
        return 1

    print("\nsettings schema: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
