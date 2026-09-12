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

    if baseline is None:
        return 0

    if not baseline.exists():
        print(f"\nno committed baseline at {baseline} -- copy the table "
              f"printed above into it")
        return 1

    committed = baseline.read_text()
    if committed.rstrip("\n") != table.rstrip("\n"):
        print(f"\nDRIFT: table does not match committed baseline "
              f"({baseline}) -- if this is a deliberate change, regenerate "
              f"the file by copy-pasting the table printed above")
        return 1

    print(f"\ntable matches committed baseline ({baseline})")
    return 0


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
