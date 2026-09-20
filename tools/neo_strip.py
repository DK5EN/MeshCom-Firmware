#!/usr/bin/env python3
"""Turn a fork-neo-test tree into a fork-neo tree.

The production branch carries no host test scaffolding. Two files need a real
transformation for that; everything else is handled by simply not projecting
`test/` onto the branch.

  1. platformio.ini loses its 34 [env:native*] sections. They point at `test/`
     via -I test/support and test_filter, so on a tree without that directory
     they are dead weight that advertises environments nobody can run. The
     comment block directly above each section goes with it.
  2. src/t-deck/tdeck_helpers.cpp loses the [KBL];set bench marker. It is a raw
     Serial.printf at preprocessor depth 0 -- no guard strips it, and it is the
     only hand-written line of the whole strip.

What is deliberately NOT stripped: the INSTRUMENT_ENABLED blocks. upstream/dev
already ships src/instrument.h (byte-identical) and src/instrument.cpp, and
seven files include it there. Removing it would delete the maintainer's own code.

Idempotent: running it twice changes nothing the second time. Exits non-zero if
a transformation it expected to make found nothing, so a silent no-op cannot
masquerade as success.

  python3 tools/neo_strip.py [--check]

--check reports what would change without writing.
"""
import re
import sys
from pathlib import Path

INI = Path("platformio.ini")
TDECK = Path("src/t-deck/tdeck_helpers.cpp")

MARKER = '    Serial.printf("[KBL];set;%u\\n", (unsigned)value);\n'
MARKER_COMMENT = (
    "    // Bench marker (tools/bench/tdeck_harness.py, scenario keylock_kbl): the\n"
    "    // keyboard light has no readback, so the value written to the controller\n"
    "    // is the only observable. Raw Serial.printf like [TFT];on in tft_on().\n"
)


def strip_native_envs(text):
    """Drop every [env:native*] section plus the comment block above it."""
    lines = text.split("\n")
    keep, i, dropped = [], 0, 0
    while i < len(lines):
        if lines[i].startswith("[env:native"):
            dropped += 1
            # the contiguous comment/blank block directly above belongs to it
            while keep and (keep[-1].startswith(";") or keep[-1].strip() == ""):
                keep.pop()
            i += 1
            while i < len(lines) and not lines[i].startswith("["):
                i += 1
            continue
        keep.append(lines[i])
        i += 1
    out = "\n".join(keep)
    return re.sub(r"\n{3,}", "\n\n", out).rstrip("\n") + "\n", dropped


def main():
    check = "--check" in sys.argv
    changes = []

    ini = INI.read_text()
    new_ini, dropped = strip_native_envs(ini)
    if dropped:
        changes.append(f"platformio.ini: {dropped} [env:native*] sections removed")
        if not check:
            INI.write_text(new_ini)

    src = TDECK.read_text()
    new_src = src
    if MARKER in new_src:
        new_src = new_src.replace(MARKER_COMMENT + MARKER, "")
        if MARKER in new_src:  # comment shape drifted; drop the line alone
            new_src = new_src.replace(MARKER, "")
        changes.append(f"{TDECK}: [KBL];set bench marker removed")
        if not check:
            TDECK.write_text(new_src)

    if not changes:
        print("nothing to strip (already a fork-neo tree)")
        return 0
    for c in changes:
        print(("would strip: " if check else "stripped: ") + c)
    if dropped == 0:
        print("ERROR: no native envs found -- wrong tree?", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
