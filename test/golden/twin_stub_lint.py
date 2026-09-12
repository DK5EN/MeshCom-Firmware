#!/usr/bin/env python3
"""Check that the U2 twin's stub headers are verbatim copies of the real ones.

The twin (test/test_udp_send_twin) shadows two headers that cannot compile on
a host: src/udp_functions.h (the WiFi/DNS/NTP surface) and src/nrf52/nrf_eth.h
(the W5100S driver). Each stub declares only the handful of things the drain
actually touches.

That is the risk this script exists for. A stub whose types have drifted from
the header under test is worse than no test at all: it passes, and it passes
about something that is not the shipped declaration. `bool` against `int`, a
`uint16_t` length against `int`, a member dropped from a class -- none of that
shows up as a compile error, because the stub IS the compile.

So every declaration line in a stub must appear byte-identical somewhere in
its real counterpart. Lines the stub adds on its own (includes, comments, the
class scaffolding) are skipped by the DECL pattern; lines the REAL header has
and the stub does not are fine by design -- the stub is a subset on purpose,
so that a drain reaching for something new fails to compile rather than
binding to a stub.

  python3 test/golden/twin_stub_lint.py
  python3 test/golden/twin_stub_lint.py --self-test

Exit 0 when every stub declaration is verbatim, 1 otherwise.
"""
import argparse
import re
import sys
from pathlib import Path
from typing import List, Tuple

ROOT = Path(__file__).resolve().parents[2]

# stub -> real
PAIRS: List[Tuple[str, str]] = [
    ("test/test_udp_send_twin/stubs/udp_functions.h", "src/udp_functions.h"),
    ("test/test_udp_send_twin/stubs/nrf_eth.h", "src/nrf52/nrf_eth.h"),
]

# A declaration is a line that ends in ';' and is not a preprocessor line,
# a comment, or a string. That catches `extern bool bUDPLOG;`,
# `void udpCountTx(bool ok);` and `bool udp_is_busy = false;` alike.
DECL = re.compile(r"^\s*(?!#|//)[A-Za-z_].*;\s*(//.*)?$")


def declarations(text: str) -> List[str]:
    out = []
    for line in text.splitlines():
        if DECL.match(line):
            out.append(line.rstrip())
    return out


def check(stub_rel: str, real_rel: str, root: Path = ROOT) -> List[str]:
    stub_p, real_p = root / stub_rel, root / real_rel
    if not stub_p.exists():
        return [f"missing stub: {stub_rel}"]
    if not real_p.exists():
        return [f"missing real header: {real_rel}"]

    real_lines = {l.rstrip() for l in real_p.read_text().splitlines()}
    problems = []
    decls = declarations(stub_p.read_text())
    if not decls:
        problems.append(f"{stub_rel}: no declarations found -- the DECL "
                        f"pattern stopped matching, this check is now void")
    for d in decls:
        if d not in real_lines:
            problems.append(
                f"{stub_rel}: declaration is not verbatim in {real_rel}:\n"
                f"    stub: {d.strip()}")
    return problems


def self_test() -> int:
    import tempfile
    ok = True
    real = ("#pragma once\n"
            "#include <configuration.h>\n"
            "extern bool bUDPLOG;\n"
            "void udpCountTx(bool ok);\n"
            "class X {\n"
            "    bool udp_is_busy = false;\n"
            "};\n")
    cases = [
        ("verbatim", "extern bool bUDPLOG;\nvoid udpCountTx(bool ok);\n"
                     "    bool udp_is_busy = false;\n", True),
        ("subset-is-fine", "extern bool bUDPLOG;\n", True),
        ("type-drifted", "extern int bUDPLOG;\n", False),
        ("param-drifted", "void udpCountTx(int ok);\n", False),
        ("whitespace-drifted", "extern bool  bUDPLOG;\n", False),
        ("no-declarations", "#pragma once\n// only comments\n", False),
    ]
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "src").mkdir()
        (root / "stub").mkdir()
        (root / "src/real.h").write_text(real)
        for name, stub, want_ok in cases:
            (root / "stub/s.h").write_text(stub)
            problems = check("stub/s.h", "src/real.h", root)
            good = (not problems) if want_ok else bool(problems)
            print(f"  {'ok ' if good else 'FAIL'} {name}: {len(problems)} "
                  f"problem(s), expected {'none' if want_ok else 'some'}")
            ok = ok and good
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    problems: List[str] = []
    for stub, real in PAIRS:
        problems += check(stub, real)
        print(f"checked {stub} against {real}")
    for p in problems:
        print(f"DRIFT {p}")
    if not problems:
        print("all twin stub declarations are verbatim")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
