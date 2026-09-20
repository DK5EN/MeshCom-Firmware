#!/usr/bin/env python3
"""Check that every `extern` in a carved TU matches its real definition.

Four translation units in this tree exist only because a function had to be
moved out of a file that never compiles on a host (C1/U1, C2/U2, C3/U3). Each
of them re-declares, by hand, the file-scope state its function reads:

  src/esp32/udp_drain_esp32.cpp     src/esp32/udp_frame_esp32.cpp
  src/nrf52/udp_drain_nrf52.cpp     src/nrf52/udp_frame_nrf52.cpp

That hand-copying is the hazard this script exists for, and it is worse than
the stub-header one twin_stub_lint.py covers. A stub header with a drifted
type at least fails to compile against the real one somewhere. An `extern`
with a drifted type COMPILES AND LINKS SILENTLY and then reads the object at
the wrong width or the wrong offset:

  definition   uint16_t lora_tx_msg_len;
  extern       uint32_t lora_tx_msg_len;   // reads two bytes of the neighbour

There is no diagnostic for that -- not from the compiler, which sees one TU
at a time, and not from the linker, which matches names and not types. The
only thing that catches it is comparing the two spellings, which is this.

So: for every `extern <type> <name>;` in a carved TU, find the definition of
<name> at file scope somewhere in src/ and require the type to match. An
array bound may be omitted in the extern (`uint8_t buf[];` against
`uint8_t buf[255];`) -- that is legal and deliberate -- but a bound that IS
given must be the one the definition uses.

  python3 test/golden/carve_extern_lint.py
  python3 test/golden/carve_extern_lint.py --self-test

Exit 0 when every extern matches its definition, 1 otherwise.
"""
import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[2]

CARVED = [
    # The four carved TUs this check was originally written for.
    "src/esp32/udp_drain_esp32.cpp",
    "src/nrf52/udp_drain_nrf52.cpp",
    "src/esp32/udp_frame_esp32.cpp",
    "src/nrf52/udp_frame_nrf52.cpp",
    # 2026-09-16: the hazard in the docstring above is not a property of being
    # "carved" -- it is a property of hand-writing an `extern` for something
    # defined in another TU, and this tree does that in six more files. The
    # scope was narrower than the reasoning. Widening it immediately paid:
    # `extern bool bInitDisplay;` in nrf52_ble.cpp bound to NOTHING (no
    # definition anywhere, no second use) and had been sitting there unnoticed
    # because nobody reads it, so the linker never had to resolve it.
    #
    # This matters right now for W5: R3-12 and R2-01 change the TYPE of
    # mheardLat/mheardLon/mheardBuffer, and lora_functions.cpp and
    # web_functions.cpp each re-declare them by hand. Getting one of those
    # wrong is precisely the silent wrong-width read described above.
    "src/lora_functions.cpp",
    "src/web_functions/web_functions.cpp",
    "src/phone_commands.cpp",
    "src/esp32/esp32_main.cpp",
    "src/nrf52/nrf52_main.cpp",
    "src/nrf52/nrf52_ble.cpp",
]

# (file, type, name) triples this check cannot decide and must not fail on.
# Keep this list SHORT and each entry argued -- an exemption is a hole.
EXEMPT = {
    # `extern U8G2 u8g2_1;` against a definition of a DERIVED type, e.g.
    # `U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2_1(U8G2_R0);`. The u8g2 device
    # classes derive from U8G2 and add no data members of their own (see
    # U8g2lib.h: each is a constructor and nothing else), so the base-typed
    # extern has the same size and layout. It is legal to declare it this way
    # and the tree has done so in three files for years. The check compares
    # spellings and cannot know the inheritance, so it would report a mismatch
    # that is not one.
    ("U8G2", "u8g2_1"),
    ("U8G2", "u8g2_2"),
}

# `extern <type> <name>;` or `extern <type> <name>[...];` at column 0.
EXTERN = re.compile(r"^extern\s+(.+?)\s+(\w+)\s*(\[[^\]]*\])?\s*;")


def norm(t: str) -> str:
    return " ".join(t.replace("*", " * ").split())


def norm_bound(b: str) -> str:
    """`[]` means "bound deliberately omitted", which is legal and common in a
    carved extern; anything else is normalised for whitespace so
    `[UDP_TX_BUF_SIZE+50]` and `[UDP_TX_BUF_SIZE + 50]` compare equal."""
    if not b:
        return ""
    inner = b[1:-1].strip()
    return "" if inner == "" else "[" + " ".join(inner.split()).replace(" ", "") + "]"


def definitions(root: Path) -> Dict[str, List[Tuple[str, str, str]]]:
    """name -> [(type, bound, file)] for every file-scope definition in src/."""
    out: Dict[str, List[Tuple[str, str, str]]] = {}
    # a definition is a column-0 declaration that is NOT `extern` and not a
    # function (no '(' before the ';' or '=')
    pat = re.compile(r"^(?!extern\b)([A-Za-z_][\w:<>,\s\*]*?)\s+(\w+)\s*(\[[^\]]*\])?\s*(=|;)")
    for p in sorted(root.glob("src/**/*.cpp")):
        for raw in p.read_text(errors="replace").splitlines():
            # Strip the trailing comment FIRST. The function test below asks
            # "is there a '(' before the initialiser", and a perfectly ordinary
            # trailing comment can contain one -- udp_functions.cpp:81 is
            # `uint8_t convBuffer[...]; // ... (ID, RSSI, SNR, MODE)`, which
            # made this scanner skip the very definition it was looking for.
            line = raw.split("//")[0]
            if "(" in line.split("=")[0]:
                continue
            m = pat.match(line)
            if not m:
                continue
            ty, name, bound = norm(m.group(1)), m.group(2), norm_bound(m.group(3) or "")
            # `static` is internal linkage: an extern in another TU can never
            # bind to it, so offering it as a candidate would let a genuinely
            # unbound extern look satisfied.
            if ty.split(" ")[0] in ("return", "else", "case", "static"):
                continue
            out.setdefault(name, []).append((ty, bound, str(p.relative_to(root))))
    return out


def check(root: Path = ROOT, carved: Optional[List[str]] = None) -> List[str]:
    defs = definitions(root)
    problems: List[str] = []
    checked = 0
    for rel in (carved if carved is not None else CARVED):
        p = root / rel
        if not p.exists():
            problems.append(f"missing carved TU: {rel}")
            continue
        for line in p.read_text().splitlines():
            m = EXTERN.match(line)
            if not m:
                continue
            ty, name, bound = norm(m.group(1)), m.group(2), norm_bound(m.group(3) or "")
            checked += 1
            if (ty, name) in EXEMPT:
                continue
            cands = defs.get(name)
            if not cands:
                problems.append(
                    f"{rel}: `extern {ty} {name}{bound};` has no file-scope "
                    f"definition anywhere in src/ -- either it moved, or the "
                    f"name is wrong and this extern binds to nothing")
                continue
            # a name may legitimately be defined once per platform (bUDPLOG,
            # err_cnt_udp_tx): accept when ANY definition agrees.
            if any(d_ty == ty and (bound == "" or bound == d_bound)
                   for d_ty, d_bound, _ in cands):
                continue
            shown = "; ".join(f"{t} {name}{b} ({f})" for t, b, f in cands)
            problems.append(
                f"{rel}: `extern {ty} {name}{bound};` does not match its "
                f"definition -- this links silently and reads the object at "
                f"the wrong type.\n    definition(s): {shown}")
    if checked == 0 and not problems:
        problems.append("no externs found in the carved TUs -- the EXTERN "
                        "pattern stopped matching, this check is now void")
    else:
        print(f"checked {checked} extern(s) in {len(carved if carved is not None else CARVED)} carved TU(s)")
    return problems


def self_test() -> int:
    import tempfile
    ok = True
    origin = (
        "uint16_t lora_tx_msg_len = 0;\n"
        "bool udp_is_busy = false;\n"
        # the real udp_functions.cpp:81 line, comment and all: the trailing
        # comment contains '(' and once made the scanner skip this definition
        "uint8_t convBuffer[UDP_TX_BUF_SIZE + 50]; // extra buffer (ID, RSSI)\n"
        "String strSource_call;\n"
        "void someFunction(int a);\n"
        # internal linkage: no extern in another TU may bind to this
        "static uint8_t privateBuffer[64];\n"
    )
    cases = [
        ("exact", "extern uint16_t lora_tx_msg_len;\n", True),
        ("array-bound-omitted", "extern uint8_t convBuffer[];\n", True),
        ("array-bound-matching",
         "extern uint8_t convBuffer[UDP_TX_BUF_SIZE + 50];\n", True),
        ("array-bound-wrong", "extern uint8_t convBuffer[255];\n", False),
        ("width-drifted", "extern uint32_t lora_tx_msg_len;\n", False),
        ("signedness-drifted", "extern int16_t lora_tx_msg_len;\n", False),
        ("class-type-ok", "extern String strSource_call;\n", True),
        ("no-such-symbol", "extern bool never_defined_anywhere;\n", False),
        # regression: a trailing comment containing '(' must not hide the
        # definition from the scanner
        ("definition-with-paren-in-comment",
         "extern uint8_t convBuffer[UDP_TX_BUF_SIZE + 50];\n", True),
        # regression: a file-static must never satisfy an extern
        ("static-does-not-satisfy-extern",
         "extern uint8_t privateBuffer[64];\n", False),
    ]
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "src").mkdir()
        (root / "src/origin.cpp").write_text(origin)
        for name, ext, want_ok in cases:
            (root / "src/carved.cpp").write_text(ext)
            problems = check(root, ["src/carved.cpp"])
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
    problems = check()
    for p in problems:
        print(f"MISMATCH {p}")
    if not problems:
        print("every carved extern matches its definition")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
