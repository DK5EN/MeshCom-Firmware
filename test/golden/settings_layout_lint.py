#!/usr/bin/env python3
"""Structural twin for `struct s_meshcom_settings` (test plan U4).

The settings struct is defined TWICE, once per platform, and can never be
linked into one binary to compare at runtime:

  src/esp32/esp32_flash.h        struct s_meshcom_settings   (~144 fields)
  src/nrf52/WisBlock-API.h       struct s_meshcom_settings   (~131 fields)

Same tag name, different header, each full of platform-only macros and
includes that do not compile on a host. So the twin has to be structural:
parse both definitions as text, line up the fields by name, and diff.

Three verdicts fall out of that comparison:

  both         -- same name, same type, same array bound. Fine.
  esp32-only / nrf52-only
               -- one platform carries a field the other does not. Known and
                  expected today (T-Deck-only UI flags on the ESP32 side,
                  send_repeat_time/auto_join on the nRF52 side, ...). The
                  committed baseline (test/golden/native/settings-layout.txt)
                  pins today's list; a NEW asymmetric field still fails this
                  check, because it changes the printed table, but only to
                  force a conscious copy-paste regeneration -- it is not a
                  wire-format bug by itself.
  MISMATCH     -- same name on BOTH sides, but the type or the array bound
                  differs. This is the class this script exists for.

Why MISMATCH is worse than a missing field: the nRF52 BLE settings
characteristic ships this struct as RAW BYTES (docs/BACKLOG.md, OPT-D14 /
D1-04) -- there is no per-field marshalling, the characteristic is `struct
s_meshcom_settings` reinterpreted as a byte buffer. A field missing on one
side is a source-level inconsistency the compiler on that platform simply
never sees. A field present on BOTH sides under the same name but a
DIFFERENT WIDTH OR BOUND is a wire-format bug: the two platforms agree on the
field's identity but disagree on how many bytes it occupies, so anything that
copies this struct's bytes between an ESP32 and an nRF52 node (or decodes one
platform's dump on the other) reads the wrong offset from that field
onward -- silently, the same way carve_extern_lint.py's drifted extern reads
the wrong width with no compiler or linker diagnostic. `node_update` differing
by one byte (char[21] vs char[20]) is an off-by-one in a buffer that gets
exchanged, not a cosmetic inconsistency.

Two are already known and pinned in the committed baseline:

  node_gpsbaud   esp32 `unsigned long`  vs  nrf52 `unsigned int`
  node_update    esp32 `char[21]`       vs  nrf52 `char[20]`

Because they are pinned, a run against the current tree still exits 0 --
exactly like radio_units_lint.py and twin_stub_lint.py in this same
directory, this is a snapshot/twin check, not a "this invariant must always
hold" check (that is carve_extern_lint.py's job). But MISMATCH is not treated
as an ordinary, quiet asymmetry either: every run prints a FATAL block naming
every MISMATCH row in the CURRENT table, pinned or not, so nobody scrolls
past it, and `--self-test` proves the underlying classifier -- independent of
any baseline file -- treats a type or bound drift between same-named fields
as fatal while a field added on only one side is not:

  python3 test/golden/settings_layout_lint.py --self-test
  python3 test/golden/settings_layout_lint.py

Exit 0 when the printed table matches the committed baseline, 1 otherwise
(including: no committed baseline yet, table differs -- regenerate it by
copy-pasting the table this script prints).

PARSING NOTE (the trap carve_extern_lint.py already hit and documents in its
own definitions()): a trailing `// comment (with parens)` can hide a
declaration from a naive scanner, or worse, get swallowed into what looks
like a value. Comments are stripped from every line BEFORE any structural
test, same as there.

FIELD-SET PARITY (the check this script gained on top of the table/baseline
comparison above): D1-04 is about to replace both structs with one keyed
store, and DR-12 (`both-wrong`) settled that single-platform fields keep
platform scope in that store rather than the struct growing shared members.
The risk during that move is silent: a field quietly dropped off one side, or
quietly picked up by the other, and the table/baseline check above alone does
not force anyone to notice -- its own docstring says so ("regenerate the file
by copy-pasting the table printed above"), and a copy-paste is exactly the
kind of edit a reviewer skims past.

So the expected esp32-only and nrf52-only NAME sets are pinned a second time,
as literal Python sets in THIS file (`EXPECTED_ESP32_ONLY_FIELDS` /
`EXPECTED_NRF52_ONLY_FIELDS`), not in `settings-layout.txt` and not in a
separate data file either -- a second file is just one more thing that can be
regenerated without being read. Changing either set requires touching the
script's source, which is a real code-review diff, not a table replacement.
`run()` fails (independent of whether the baseline table still matches) the
moment the *computed* esp32-only/nrf52-only name sets stop matching these
pinned sets, and the message names the field and which side changed.

As of 2026-09-12 the pinned sets are 15 esp32-only members (14 persisted
UI/behaviour flags plus `node_ntp`, which is a live RUNTIME field -- re-copied
from `node_ownntp` on every connect in `src/udp_functions.cpp`, never
persisted -- but it is still a member of the esp32 `s_meshcom_settings`
struct, and struct membership, not persistence, is what this script parses)
and 2 nrf52-only members (`send_repeat_time`, `auto_join` -- both LoRaWAN OTAA
fields with no ESP32 path). These were re-derived independently from the
headers for this check, not copied from docs/d1-04-settings-field-triage-
20260912.md; they match that document's 14+1 / 2 split exactly (verified
2026-09-12 against the real tree, see this file's history).

DISTINGUISHING `s_meshcom_settings` FROM `s_meshcomcompat_settings`: only the
former is parsed, and the split is by construction, not by extra filtering
code that could get it wrong. `extract_struct()` does a plain `re.search` for
the literal text `struct s_meshcom_settings` (word-bounded) and returns the
body of the FIRST brace-matched block after that match. `s_meshcomcompat_
settings` (WisBlock-API.h:413, the frozen on-disk migration format) does not
contain that literal substring anywhere in its own name -- "meshcom" is
immediately followed by "compat", not by "_settings" -- so the regex cannot
match it, deliberately or by accident, and the earlier `s_meshcom_settings`
definition (WisBlock-API.h:178) is what gets extracted. Nothing in this
script ever calls `extract_struct` or `load_fields` with `s_meshcomcompat_
settings` as the name, and nothing should: that struct's frozen `node_gpsbaud`
-as-`unsigned int` spelling (intentionally divergent from the live struct's
`uint32_t`, for the migration path) must stay exactly as committed. This
script only reads it, never diffs or "corrects" it.
"""
import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[2]

ESP32_HEADER = "src/esp32/esp32_flash.h"
NRF52_HEADER = "src/nrf52/WisBlock-API.h"
STRUCT_NAME = "s_meshcom_settings"
BASELINE = "test/golden/native/settings-layout.txt"

# type + name + zero or more array-bound groups + optional initialiser, all on
# one line (every field in both structs is declared on a single physical
# line). The type group is greedy on purpose: given "unsigned long node_age"
# it first grabs "unsigned long node_age" as a candidate type and then backs
# off one token at a time until the rest of the pattern (the variable name,
# then optional brackets/initialiser/`;`) has something left to match against
# -- which is exactly how it ends up splitting "unsigned long" / "node_age"
# instead of eating the field name into the type.
FIELD = re.compile(
    r"^\s*([A-Za-z_][\w:<>]*(?:\s+[A-Za-z_][\w:<>]*)*)"
    r"\s+(\w+)"
    r"((?:\s*\[[^\]]*\])*)"
    r"\s*(?:=[^;]*)?;\s*$"
)


def strip_comments(text: str) -> str:
    """Drop a trailing `// ...` from every line. Safe here: neither struct
    contains a `//` inside a string or character literal."""
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def extract_struct(text: str, name: str) -> Optional[str]:
    """Return the body between the braces of `struct <name> { ... };`, or
    None if the struct can't be found. Brace-matched over the whole
    remainder of the file (not line-based) so a same-line initialiser like
    `= {0x00, 0x0D, ...}` cannot desynchronise the count."""
    m = re.search(r"struct\s+" + re.escape(name) + r"\b", text)
    if not m:
        return None
    rest = text[m.end():]
    start = rest.find("{")
    if start == -1:
        return None
    depth = 0
    for i, ch in enumerate(rest[start:], start):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return rest[start + 1:i]
    return None


def norm_type(t: str) -> str:
    return " ".join(t.split())


def norm_bound(b: str) -> str:
    """Concatenate and whitespace-normalise every `[...]` group so
    `[ 16 ][16]` and `[16][16]` compare equal."""
    out = []
    for grp in re.findall(r"\[[^\]]*\]", b):
        inner = "".join(grp[1:-1].split())
        out.append("[" + inner + "]")
    return "".join(out)


def parse_fields(body: str) -> "Dict[str, Tuple[str, str]]":
    """name -> (type, bound), first occurrence wins. Preprocessor lines
    (`#if`/`#else`/`#endif`) are skipped as text, not evaluated -- a field
    inside a `#if defined(BOARD_T_DECK)` guard in esp32_flash.h is still a
    real member of that platform's struct whenever that macro is set, so it
    is treated as an ordinary esp32-side field (and, since it never appears
    in WisBlock-API.h at all, it naturally lands in the esp32-only bucket)."""
    out: Dict[str, Tuple[str, str]] = {}
    for raw in body.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = FIELD.match(line)
        if not m:
            continue
        ty, name, bound = norm_type(m.group(1)), m.group(2), norm_bound(m.group(3))
        if ty.split(" ")[0] in ("return", "else", "case", "static", "const"):
            continue
        out.setdefault(name, (ty, bound))
    return out


def load_fields(path: Path) -> "Dict[str, Tuple[str, str]]":
    body = extract_struct(strip_comments(path.read_text(errors="replace")), STRUCT_NAME)
    if body is None:
        return {}
    return parse_fields(body)


# Pinned field-SET parity (names only, not type/bound -- that is `compare()`'s
# job). See the module docstring's "FIELD-SET PARITY" section for why these
# live here, as literal sets, rather than in settings-layout.txt or a
# separate data file. Re-derived from the headers 2026-09-12; matches
# docs/d1-04-settings-field-triage-20260912.md §4d's 14+1 / 2 split.
EXPECTED_ESP32_ONLY_FIELDS = frozenset({
    "node_disp_rot", "node_map", "node_audio_start", "node_audio_msg",
    "node_keyboardlock", "node_backlightlock", "node_kbllightlock",
    "node_modus", "node_mute", "node_persist_to_flash", "node_persist_to_sd",
    "node_immediate_save", "node_kbl_sync", "node_wifion",
    "node_ntp",  # RUNTIME-only, never persisted -- see module docstring
})
EXPECTED_NRF52_ONLY_FIELDS = frozenset({
    "send_repeat_time", "auto_join",  # LoRaWAN OTAA, no ESP32 path
})


def check_field_parity(esp32: "Dict[str, Tuple[str, str]]",
                        nrf52: "Dict[str, Tuple[str, str]]") -> List[str]:
    """Compare the CURRENT esp32-only/nrf52-only NAME sets against the pinned
    `EXPECTED_*_ONLY_FIELDS` sets above. Returns one message per name that
    changed side, naming the field and which side it changed on. Empty means
    the field set is exactly as pinned.

    Three ways a name can violate this, per side:
      - an expected only-field is gone from that platform's struct entirely
        (removed, or renamed to something this pinned set doesn't know)
      - an expected only-field now also exists on the OTHER platform (it
        quietly became shared -- exactly the "quietly added to the other
        platform" risk this check exists to catch)
      - a NEW only-field exists that isn't in the pinned set at all (quietly
        dropped from the other platform, or a genuinely new field)
    """
    esp32_only = set(esp32) - set(nrf52)
    nrf52_only = set(nrf52) - set(esp32)
    violations: List[str] = []

    for name in sorted(EXPECTED_ESP32_ONLY_FIELDS - esp32_only):
        if name not in esp32:
            violations.append(
                f"{name}: expected esp32-only field (EXPECTED_ESP32_ONLY_FIELDS) "
                f"is gone from the esp32 struct entirely -- removed, or renamed")
        elif name in nrf52:
            violations.append(
                f"{name}: expected esp32-only field (EXPECTED_ESP32_ONLY_FIELDS) "
                f"now also exists on nrf52 -- no longer esp32-only")
        else:
            violations.append(
                f"{name}: expected esp32-only field (EXPECTED_ESP32_ONLY_FIELDS) "
                f"is missing from the esp32-only set for an unexplained reason")
    for name in sorted(esp32_only - EXPECTED_ESP32_ONLY_FIELDS):
        violations.append(
            f"{name}: NEW esp32-only field, not in the pinned "
            f"EXPECTED_ESP32_ONLY_FIELDS set -- if intentional, add it there; "
            f"otherwise it may be a field silently dropped from nrf52")

    for name in sorted(EXPECTED_NRF52_ONLY_FIELDS - nrf52_only):
        if name not in nrf52:
            violations.append(
                f"{name}: expected nrf52-only field (EXPECTED_NRF52_ONLY_FIELDS) "
                f"is gone from the nrf52 struct entirely -- removed, or renamed")
        elif name in esp32:
            violations.append(
                f"{name}: expected nrf52-only field (EXPECTED_NRF52_ONLY_FIELDS) "
                f"now also exists on esp32 -- no longer nrf52-only")
        else:
            violations.append(
                f"{name}: expected nrf52-only field (EXPECTED_NRF52_ONLY_FIELDS) "
                f"is missing from the nrf52-only set for an unexplained reason")
    for name in sorted(nrf52_only - EXPECTED_NRF52_ONLY_FIELDS):
        violations.append(
            f"{name}: NEW nrf52-only field, not in the pinned "
            f"EXPECTED_NRF52_ONLY_FIELDS set -- if intentional, add it there; "
            f"otherwise it may be a field silently dropped from esp32")

    return violations


Row = Tuple[str, str, str, str]  # name, esp32 col, nrf52 col, verdict


def field_col(f: Optional[Tuple[str, str]]) -> str:
    if f is None:
        return "-"
    ty, bound = f
    return f"{ty}{bound}"


def compare(esp32: "Dict[str, Tuple[str, str]]",
            nrf52: "Dict[str, Tuple[str, str]]") -> "Tuple[List[Row], List[str]]":
    """Return (rows, fatal). `rows` is the full sorted table (one row per
    field name seen on either side). `fatal` holds one message per MISMATCH
    row -- a field present under the same name on both sides with a
    different type or array bound -- and is empty for every other case,
    including a field present on only one side."""
    rows: List[Row] = []
    fatal: List[str] = []
    names = sorted(set(esp32) | set(nrf52))
    for name in names:
        e, n = esp32.get(name), nrf52.get(name)
        if e is not None and n is not None:
            verdict = "both" if e == n else "MISMATCH"
            if verdict == "MISMATCH":
                fatal.append(
                    f"{name}: esp32 `{field_col(e)}` vs nrf52 `{field_col(n)}` "
                    f"-- same field, different wire width/bound")
        elif e is not None:
            verdict = "esp32-only"
        else:
            verdict = "nrf52-only"
        rows.append((name, field_col(e), field_col(n), verdict))
    return rows, fatal


def render_table(rows: "List[Row]") -> str:
    header: Row = ("field", "esp32", "nrf52", "verdict")
    all_rows = [header] + rows
    widths = [max(len(r[i]) for r in all_rows) for i in range(4)]
    lines = []
    for r in all_rows:
        lines.append("  ".join(c.ljust(w) for c, w in zip(r[:3], widths[:3])) + "  " + r[3])
    return "\n".join(lines) + "\n"


def run(esp32: "Dict[str, Tuple[str, str]]", nrf52: "Dict[str, Tuple[str, str]]",
        baseline: Optional[Path]) -> int:
    if not esp32 and not nrf52:
        print("no fields parsed from either side -- the FIELD pattern "
              "stopped matching, this check is now void")
        return 1

    rows, fatal = compare(esp32, nrf52)
    table = render_table(rows)
    print(table, end="")

    n_both = sum(1 for r in rows if r[3] == "both")
    n_esp = sum(1 for r in rows if r[3] == "esp32-only")
    n_nrf = sum(1 for r in rows if r[3] == "nrf52-only")
    print(f"-- {len(rows)} field(s): {len(esp32)} esp32, {len(nrf52)} nrf52, "
          f"{n_both} both, {n_esp} esp32-only, {n_nrf} nrf52-only, "
          f"{len(fatal)} MISMATCH")

    if fatal:
        print()
        for f in fatal:
            print(f"FATAL MISMATCH {f}")
        print(f"{len(fatal)} field(s) share a name across platforms but "
              f"disagree on wire width/bound -- see this script's docstring "
              f"for why that is a wire-format bug (OPT-D14 / D1-04), not a "
              f"source-level inconsistency")

    parity_violations = check_field_parity(esp32, nrf52)
    if parity_violations:
        print()
        for v in parity_violations:
            print(f"FATAL FIELD-SET DRIFT {v}")
        print(f"{len(parity_violations)} field(s) moved relative to the "
              f"pinned EXPECTED_ESP32_ONLY_FIELDS / EXPECTED_NRF52_ONLY_FIELDS "
              f"sets in this script -- see the module docstring's FIELD-SET "
              f"PARITY section. This fails independently of the baseline-"
              f"table check below.")

    baseline_rc = 0
    if baseline is not None:
        if not baseline.exists():
            print(f"\nno committed baseline at {baseline} -- copy the table "
                  f"printed above into it")
            baseline_rc = 1
        else:
            committed = baseline.read_text()
            if committed.rstrip("\n") != table.rstrip("\n"):
                print(f"\nDRIFT: table does not match committed baseline "
                      f"({baseline}) -- if this is a deliberate change, "
                      f"regenerate the file by copy-pasting the table "
                      f"printed above")
                baseline_rc = 1
            else:
                print(f"\ntable matches committed baseline ({baseline})")

    return 1 if parity_violations else baseline_rc


def self_test() -> int:
    import tempfile

    def case(name: str, esp32_src: str, nrf52_src: str, want_fatal: bool,
              want_reported: bool = True) -> bool:
        wrap = "struct s_meshcom_settings\n{\n%s\n};\n"
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            e = root / "esp32.h"
            n = root / "nrf52.h"
            e.write_text(wrap % esp32_src)
            n.write_text(wrap % nrf52_src)
            esp32 = load_fields(e)
            nrf52 = load_fields(n)
        rows, fatal = compare(esp32, nrf52)
        reported = any(r[3] != "both" for r in rows)
        good = (bool(fatal) == want_fatal) and (reported == want_reported or not want_reported)
        print(f"  {'ok  ' if good else 'FAIL'} {name}: fatal={len(fatal)} "
              f"reported={reported} (expected fatal={want_fatal})")
        return good

    ok = True
    ok &= case(
        "identical-structs-clean",
        "int node_alt = 0;\nchar node_call[10] = {0};\n",
        "int node_alt = 0;\nchar node_call[10] = {0};\n",
        want_fatal=False, want_reported=False)
    ok &= case(
        "field-added-one-side-reported-not-fatal",
        "int node_alt = 0;\nint node_owgpio = 36;\n",
        "int node_alt = 0;\n",
        want_fatal=False, want_reported=True)
    ok &= case(
        "same-name-type-differs-fatal",
        "unsigned long node_gpsbaud = 38400;\n",
        "unsigned int node_gpsbaud = 38400;\n",
        want_fatal=True)
    ok &= case(
        "same-name-bound-differs-fatal",
        "char node_update[21] = {0};\n",
        "char node_update[20] = {0};\n",
        want_fatal=True)

    # --- check_field_parity(): the pinned EXPECTED_*_ONLY_FIELDS sets ---
    # Synthetic esp32/nrf52 dicts built directly FROM the pinned sets, so
    # these cases test the mechanism, not today's field list -- if the
    # pinned sets are edited later, these cases still pass unchanged.
    def synthetic_pair() -> "Tuple[Dict[str, Tuple[str, str]], Dict[str, Tuple[str, str]]]":
        shared = {"node_alt": ("int", "")}
        e = dict(shared)
        n = dict(shared)
        for nm in EXPECTED_ESP32_ONLY_FIELDS:
            e[nm] = ("int", "")
        for nm in EXPECTED_NRF52_ONLY_FIELDS:
            n[nm] = ("int", "")
        return e, n

    def parity_case(name: str, mutate, want_violation: bool,
                     want_name: Optional[str] = None) -> bool:
        e, n = synthetic_pair()
        mutate(e, n)
        violations = check_field_parity(e, n)
        got = bool(violations)
        good = got == want_violation
        if good and want_violation and want_name is not None:
            good = any(want_name in v for v in violations)
        print(f"  {'ok  ' if good else 'FAIL'} {name}: "
              f"{len(violations)} violation(s) (expected "
              f"{'>=1' if want_violation else '0'}"
              f"{', naming ' + want_name if want_name else ''})")
        return good

    ok &= parity_case(
        "field-parity-clean-matches-pinned-sets",
        lambda e, n: None, want_violation=False)

    _esp_name = sorted(EXPECTED_ESP32_ONLY_FIELDS)[0]
    ok &= parity_case(
        "field-parity-esp32-only-field-removed-entirely",
        lambda e, n, nm=_esp_name: e.pop(nm), want_violation=True,
        want_name=_esp_name)
    ok &= parity_case(
        "field-parity-esp32-only-field-now-also-on-nrf52",
        lambda e, n, nm=_esp_name: n.__setitem__(nm, ("int", "")),
        want_violation=True, want_name=_esp_name)
    ok &= parity_case(
        "field-parity-new-unexpected-esp32-only-field",
        lambda e, n: e.__setitem__("node_totally_new_flag", ("int", "")),
        want_violation=True, want_name="node_totally_new_flag")

    _nrf_name = sorted(EXPECTED_NRF52_ONLY_FIELDS)[0]
    ok &= parity_case(
        "field-parity-nrf52-only-field-removed-entirely",
        lambda e, n, nm=_nrf_name: n.pop(nm), want_violation=True,
        want_name=_nrf_name)
    ok &= parity_case(
        "field-parity-nrf52-only-field-now-also-on-esp32",
        lambda e, n, nm=_nrf_name: e.__setitem__(nm, ("uint32_t", "")),
        want_violation=True, want_name=_nrf_name)
    ok &= parity_case(
        "field-parity-new-unexpected-nrf52-only-field",
        lambda e, n: n.__setitem__("node_totally_new_lorawan_flag", ("int", "")),
        want_violation=True, want_name="node_totally_new_lorawan_flag")

    # Parser-matches-nothing: neither side has anything the FIELD pattern
    # recognises (only comments / preprocessor noise survive strip_comments).
    # This is the void-check trap carve_extern_lint.py and twin_stub_lint.py
    # both document -- fatal in the sense that `run()` itself must refuse to
    # report a clean pass.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        blank = "struct s_meshcom_settings\n{\n// nothing parseable here\n};\n"
        e = root / "esp32.h"
        n = root / "nrf52.h"
        e.write_text(blank)
        n.write_text(blank)
        rc = run(load_fields(e), load_fields(n), baseline=None)
        good = rc == 1
        print(f"  {'ok  ' if good else 'FAIL'} parser-matches-nothing-void: "
              f"run() exit {rc} (expected 1)")
        ok = ok and good

    # Struct not found at all in the file -> load_fields must return {},
    # which the two cases above already exercise transitively, but pin it
    # directly too since it is the other half of "matches nothing".
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        p = root / "no_struct.h"
        p.write_text("struct something_else\n{\n    int x;\n};\n")
        fields = load_fields(p)
        good = fields == {}
        print(f"  {'ok  ' if good else 'FAIL'} struct-not-found-returns-empty: "
              f"{len(fields)} field(s) (expected 0)")
        ok = ok and good

    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    esp32 = load_fields(ROOT / ESP32_HEADER)
    nrf52 = load_fields(ROOT / NRF52_HEADER)
    return run(esp32, nrf52, ROOT / BASELINE)


if __name__ == "__main__":
    sys.exit(main())
