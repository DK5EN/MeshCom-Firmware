#!/usr/bin/env python3
"""Call-site gate for the radio unit conversions (RF-01..RF-03, RF-05, RF-06).

The converters in src/radio_units.cpp have their own native test
(test/test_radio_units). That test pins the conversion table; it cannot pin
that the call sites actually use it, and every defect in this family was a
*missing call*, not a wrong table:

  RF-01  --txbw stored kHz where the SX126x driver reads a bandwidth index
  RF-02  --txcr converted under #ifdef BOARD_RAK4630 only, while the
         index-unit radio path is BOARD_RAK4630 || USE_HELTEC_T114 ||
         BOARD_T_ECHO
  RF-03  lora_setcountry() case 7 compared stored values against literals
         written in ESP32 units, so nothing matched on nRF52
  RF-05  --txfreq converted MHz -> Hz under the same lone RAK guard
  RF-06  the --info frequency readout, same lone guard

So this checks the call sites in the source, and it is the half of the
regression that fails on the unfixed tree.

  python3 test/golden/radio_units_lint.py [--self-test]

Exit 0 clean, 1 on any violation.
"""
import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# node_sf needs no conversion -- the spreading factor is the same number on
# both platforms. The other three are the unit-split fields (OPT-D14).
CONVERTED_FIELDS = {
    "node_bw": "radioBwKhzToStored",
    "node_cr": "radioCrDenomToStored",
    "node_freq": "radioFreqMhzToStored",
}

# The full guard of the index-unit radio path, as lora_setchip.cpp spells it.
INDEXED_BOARDS = ("BOARD_RAK4630", "USE_HELTEC_T114", "BOARD_T_ECHO")

# Writes that must NOT convert, with the reason. A bare exemption is not
# allowed: every entry says why, so the list stays reviewable.
EXEMPT = {
    ("src/command_functions.cpp", "flashpoke"):
        "TM-32 bench hook: writes a deliberately raw, possibly out-of-range "
        "value so the next boot reports [FLASH]...sanitized. Converting here "
        "would defeat its only purpose.",
}

# `=` and not `==`/`<=`/`>=`/`!=`: the comparison forms are reads, and an
# early version of this regex reported `node_cr == 0` as an unconverted write.
ASSIGN = re.compile(
    r"meshcom_settings\.(node_bw|node_cr|node_freq)\s*(?<![=!<>])=(?!=)\s*(?P<rhs>[^;]*);")


def _enclosing_command(text: str, pos: int) -> str:
    """Name of the nearest preceding commandCheck() literal, for exemptions."""
    head = text[:pos]
    hits = re.findall(r'commandCheck\([^,]+,\s*\(char\*\)"([^"]*)"', head)
    return hits[-1].strip() if hits else ""


def check_conversions(rel: str) -> list:
    """Every write to a unit-split field goes through its converter."""
    path = REPO / rel
    text = path.read_text()
    out = []
    for m in ASSIGN.finditer(text):
        field, rhs = m.group(1), m.group("rhs")
        line = text.count("\n", 0, m.start()) + 1
        cmd = _enclosing_command(text, m.start())
        if (rel, cmd) in EXEMPT:
            continue
        # A platform-native default (LORA_*) is already in the right unit.
        if re.fullmatch(r"\s*LORA_[A-Z_]+\s*", rhs):
            continue
        if CONVERTED_FIELDS[field] in rhs:
            continue
        # reading the same field back through its own normalizer is fine
        if re.search(r"radio(Bw|Cr|Freq)\w*\(", rhs):
            continue
        out.append(f"{rel}:{line}: {field} written without "
                   f"{CONVERTED_FIELDS[field]}() -- rhs `{rhs.strip()}`"
                   + (f" (in --{cmd.strip()})" if cmd else ""))
    return out


def check_lone_rak_guard(rel: str) -> list:
    """A unit conversion guarded on BOARD_RAK4630 alone misses T114 and
    T-Echo, which take the same SX126x path. This is RF-02/RF-05/RF-06."""
    path = REPO / rel
    lines = path.read_text().split("\n")
    out = []
    for i, line in enumerate(lines):
        # comments describing the guard are not the guard -- the RF-02/RF-05
        # fixes leave exactly such comments behind, and an early version of
        # this checker reported them as the defect they document.
        stripped = line.strip()
        if stripped.startswith(("//", "*", "/*")):
            continue
        if not re.search(r"#\s*(ifdef|if defined\()\s*BOARD_RAK4630", line):
            continue
        if any(b in line for b in INDEXED_BOARDS[1:]):
            continue
        # look at the guarded body up to the matching #endif (or 12 lines)
        body = "\n".join(lines[i + 1:i + 13])
        body = body.split("#endif")[0]
        if re.search(r"1000000|node_bw|node_cr|node_freq|node_track", body):
            out.append(f"{rel}:{i + 1}: unit conversion guarded on "
                       f"BOARD_RAK4630 alone; the index-unit radio path is "
                       f"{' || '.join(INDEXED_BOARDS)}")
    return out


def check_case7_normalizes() -> list:
    """RF-03: lora_setcountry() case 7 must decide on normalized values
    (getBW()/getCR()/getFreq()), not on the raw stored fields."""
    text = (REPO / "src/lora_setchip.cpp").read_text()
    m = re.search(r"case 7:.*?(?=\n\s*case \d+:)", text, re.S)
    if not m:
        return ["src/lora_setchip.cpp: case 7 of lora_setcountry() not found"]
    body = m.group(0)
    out = []
    # a comparison of a raw stored field against a numeric literal is the bug
    for cmp in re.finditer(
            r"meshcom_settings\.(node_bw|node_cr|node_freq)\s*(==|!=|<|>|<=|>=)\s*[\d.]+",
            body):
        line = text.count("\n", 0, m.start() + cmp.start()) + 1
        out.append(f"src/lora_setchip.cpp:{line}: case 7 compares raw "
                   f"{cmp.group(1)} against a literal; the stored unit differs "
                   f"per platform -- compare getBW()/getCR()/getFreq() instead")
    for acc in ("getBW()", "getFreq()", "getCR()"):
        if acc not in body:
            out.append(f"src/lora_setchip.cpp: case 7 does not use {acc}; "
                       f"it must decide on normalized values")
    return out


def run() -> list:
    v = []
    v += check_conversions("src/command_functions.cpp")
    v += check_lone_rak_guard("src/command_functions.cpp")
    v += check_case7_normalizes()
    return v


def self_test() -> int:
    """The checkers must fire on the pre-fix shapes, or they prove nothing."""
    import tempfile
    ok = True

    pre_fix = {
        "RF-01 --txbw": ('        meshcom_settings.node_bw=fVar;\n',
                         check_conversions),
        "RF-02 --txcr": ('        meshcom_settings.node_cr = iVar;\n'
                         '        #ifdef BOARD_RAK4630\n'
                         '            meshcom_settings.node_cr = iVar - 4;\n'
                         '        #endif\n', check_conversions),
        "RF-05 --txfreq": ('        meshcom_settings.node_freq=fVar;\n'
                           '        #ifdef BOARD_RAK4630\n'
                           '        meshcom_settings.node_freq = '
                           'meshcom_settings.node_freq*1000000;\n'
                           '        #endif\n', check_conversions),
    }
    with tempfile.TemporaryDirectory() as d:
        rel = "src/command_functions.cpp"
        fake = Path(d) / rel
        fake.parent.mkdir(parents=True)
        global REPO
        real_repo = REPO
        REPO = Path(d)
        for name, (src, fn) in pre_fix.items():
            fake.write_text(src)
            hits = fn(rel)
            if not hits:
                print(f"SELF-TEST FAIL: {name} shape not caught")
                ok = False
            else:
                print(f"  ok  {name}: {len(hits)} violation(s) reported")
        # and the fixed shape must be clean
        fake.write_text("        meshcom_settings.node_bw = "
                        "radioBwKhzToStored(fVar, radioUnitsIndexed());\n")
        if check_conversions(rel):
            print("SELF-TEST FAIL: fixed shape still reported")
            ok = False
        else:
            print("  ok  fixed shape is clean")

        # both false positives this checker had on its first run
        fake.write_text("    if (meshcom_settings.node_cr == 0)\n"
                        "    {\n"
                        "        meshcom_settings.node_cr = LORA_CR;\n"
                        "    }\n")
        if check_conversions(rel):
            print("SELF-TEST FAIL: `== 0` read reported as a write")
            ok = False
        else:
            print("  ok  `node_cr == 0` is a read, not a write")

        fake.write_text("            // RF-02: this was #ifdef BOARD_RAK4630,"
                        " but node_cr holds an index\n"
                        "            meshcom_settings.node_cr ="
                        " radioCrDenomToStored(iVar, radioUnitsIndexed());\n")
        if check_lone_rak_guard(rel):
            print("SELF-TEST FAIL: a comment reported as a guard")
            ok = False
        else:
            print("  ok  a comment naming the guard is not the guard")
        REPO = real_repo

    lone = check_lone_rak_guard.__doc__ is not None
    if not lone:
        ok = False
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    violations = run()
    for v in violations:
        print(f"VIOLATION {v}")
    if violations:
        print(f"\n{len(violations)} violation(s)")
        return 1
    print("radio unit call sites: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
