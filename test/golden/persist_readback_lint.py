#!/usr/bin/env python3
"""Every persist-only setting must be readable back over the console.

``settings_schema.h``'s ``SETTINGS_PERSIST_ONLY_LIST`` /
``SETTINGS_PERSIST_ONLY_LIST_PLATFORM`` rows are persisted to NVS and
**deliberately never exported** by ``configExportJson()`` (``config_json.h``
gives the reason per field). That makes them invisible to
``tools/bench/w3_upgrade_check.py``, which diffs exactly that export: a node
can come through a settings migration reporting every exported field
byte-identical and still have lost its keyboard lock.

That is the ``TD-19`` trap in its general form -- there, a "byte-identical to
its vault backup" check missed ``node_kblock`` because the export does not
carry it. The ``W3`` bench run on 2026-09-16 hit the same wall from the other
side: 12 of the 17 persist-only keys had no read-back at all, so "they
survived the upgrade" could only be inferred from the node booting normally,
not measured (``docs/bench/w3-baseline/README.md`` section 7).

``--persiststat`` (``src/command_functions.cpp``) is that read-back. This lint
is what keeps it honest: **a row added to the schema without a matching field
in the command silently reopens the gap**, and nothing else would notice,
because the gap's whole nature is that it is invisible to the export diff.

The check is name-based on purpose. It asserts that every schema key appears
as a printed field name in the ``[PERSIST];...`` format strings, not that the
values are correct -- value correctness is what the bench run measures. A key
whose printed name differs from its schema key (``node_perflash`` printed as
``flash``) is declared in ALIASES below, so a rename still has to be a
deliberate edit here rather than a silent drift.

    python3 test/golden/persist_readback_lint.py
    python3 test/golden/persist_readback_lint.py --self-test

Exit 0 when every schema row is covered, 1 when one is not.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCHEMA = REPO / "src" / "settings_schema.h"
COMMANDS = REPO / "src" / "command_functions.cpp"

# Printed field name -> schema key, where the two deliberately differ.
# The `stat` line keeps the HL-03/HL-04 wording that bench captures and
# docs/bench/w3-baseline greps for verbatim, so it is the printed side that
# is pinned here rather than the schema side being renamed to match.
ALIASES = {
    "flash": "node_perflash",
    "sd": "node_persd",
    "immediate": "node_immsave",
    "mute": "node_mute",
    "kblock": "node_kblock",
    "bllock": "node_bllock",
    "kllock": "node_kllock",
    "kblsync": "node_kblsync",
    "map": "node_map",
    "modus": "node_modus",
    "wifion": "node_wifion",
    "fversion": "node_fversion",
    "mversion": "node_mversion",
    "cflash": "node_cflash",
    "fwversion": "node_fwversion",
    "start": "node_audstart",
    "msg": "node_audmsg",
}


def schema_keys(text: str) -> set[str]:
    """Every X("<key>", ...) row inside a SETTINGS_PERSIST_ONLY_LIST* macro.

    The two macros are matched by name and their bodies scanned to the next
    `#define` / end of the block -- the rows are line-continued with a
    trailing backslash, so a row is simply any X("...") on those lines.
    """
    keys: set[str] = set()
    in_block = False
    for line in text.splitlines():
        if re.search(r"#\s*define\s+SETTINGS_PERSIST_ONLY_LIST(_PLATFORM)?\s*\(", line):
            in_block = True
            continue
        if in_block:
            m = re.search(r'X\(\s*"([A-Za-z0-9_]+)"', line)
            if m:
                keys.add(m.group(1))
            # A line that does not continue ends the macro body.
            if not line.rstrip().endswith("\\"):
                in_block = False
    return keys


def printed_names(text: str) -> set[str]:
    """Field names printed by the [PERSIST];... format strings.

    Each format string is a ';'-separated list in which a literal name is
    followed by its conversion: `;flash;%d`. Collect the literal that precedes
    a conversion, which is exactly the set of readable-back field names.
    """
    names: set[str] = set()
    for fmt in re.findall(r'"(\[PERSIST\][^"]*)"', text):
        parts = fmt.split(";")
        for i, part in enumerate(parts[:-1]):
            nxt = parts[i + 1]
            if nxt.startswith("%") and re.fullmatch(r"[A-Za-z0-9_]+", part):
                names.add(part)
    return names


def covered_keys(text: str) -> set[str]:
    return {ALIASES.get(n, n) for n in printed_names(text)}


def check(schema_text: str, commands_text: str) -> list[str]:
    want = schema_keys(schema_text)
    have = covered_keys(commands_text)
    return sorted(want - have)


def self_test() -> int:
    ok = True

    def expect(name: str, cond: bool) -> None:
        nonlocal ok
        print(("  ok  " if cond else "SELF-TEST FAIL: ") + name)
        ok = ok and cond

    schema = (
        '#define SETTINGS_PERSIST_ONLY_LIST(X)                     \\\n'
        '    X("node_fversion", CFG_INT, node_fversion, 0, 0)      \\\n'
        '    X("node_cflash",   CFG_INT, node_cleanflash, 0, 0)\n'
    )
    good = 'Serial.printf("[PERSIST];meta;fversion;%d;cflash;%d\\n", a, b);'
    expect("fully covered schema passes", check(schema, good) == [])

    missing = 'Serial.printf("[PERSIST];meta;fversion;%d\\n", a);'
    expect("a schema row with no printed field is reported",
           check(schema, missing) == ["node_cflash"])

    expect("alias maps the printed name onto its schema key",
           check('#define SETTINGS_PERSIST_ONLY_LIST(X) \\\n    X("node_perflash", CFG_BOOL, x, 0, 0)\n',
                 'Serial.printf("[PERSIST];stat;flash;%d\\n", a);') == [])

    # A name not followed by a conversion is a label, not a readable field.
    expect("a bare label is not counted as coverage",
           check('#define SETTINGS_PERSIST_ONLY_LIST(X) \\\n    X("node_map", CFG_INT, x, 0, 0)\n',
                 'Serial.printf("[PERSIST];ui;map\\n");') == ["node_map"])

    # The real tree must be clean.
    real = check(SCHEMA.read_text(), COMMANDS.read_text())
    expect("the tree itself is covered (%s)" % ("clean" if not real else ", ".join(real)),
           real == [])
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    missing = check(SCHEMA.read_text(), COMMANDS.read_text())
    if missing:
        print("persist read-back lint: %d persist-only schema row(s) with no "
              "--persiststat field:" % len(missing))
        for key in missing:
            print("  %s" % key)
        print("\nAdd the field to the [PERSIST];... output in "
              "src/command_functions.cpp (or declare a printed-name alias in "
              "this lint). A persist-only row with no read-back is invisible "
              "to GET /config.json AND to the console -- see this file's "
              "header for why that matters.")
        return 1

    total = len(schema_keys(SCHEMA.read_text()))
    print("persist read-back lint: %d persist-only key(s) covered by "
          "--persiststat, 0 missing" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
