#!/usr/bin/env python3
"""Reject printf conversions the nRF52's libc cannot format.

The three nRF52 environments link **newlib-nano**, which is built without
``_WANT_IO_LONG_LONG``. Its ``printf`` parses the first ``l`` of ``%lld``,
does not recognise the second as a length modifier, and then emits the rest
of the conversion **literally**. It does not fail, it does not warn: it
writes the characters ``ld``.

That is not a hypothetical. ``settings_store.cpp`` formatted every integer
setting with ``%lld`` / ``%llu``, so the keyed settings file on ``DK5EN-90``
contained ``node_power=ld`` and ``send_repeat_time=lu`` for every numeric
field. The write succeeded, the file was well formed, ``encode()`` reported
the right byte count, and the loss only became visible one reboot later when
``decode()`` rejected those values and left the fields at their defaults.

No native test can catch this: the host's libc formats ``%lld`` correctly, so
the same code that loses the whole configuration on hardware round-trips
perfectly in ``pio test``. The compiler cannot catch it either -- ``-Wformat``
checks the conversion against the argument type, and ``%lld`` with a
``long long`` is exactly right. It is a property of the *libc that gets
linked*, which is why the gate is a lint over the nRF52 source set.

Floating point IS available (the platform links ``-u _printf_float``;
``%.9g`` / ``%.17g`` were verified correct on hardware), so this lint does not
touch float conversions.

    python3 test/golden/nano_printf_lint.py
    python3 test/golden/nano_printf_lint.py --self-test

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, List, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
PIO_INI = REPO_ROOT / "platformio.ini"
SRC = REPO_ROOT / "src"

# Conversions newlib-nano's printf does not implement. `ll` is the one that
# has actually bitten; `j` (intmax_t) and `q` (BSD quad) are the same class of
# integer width and are rejected for the same reason.
BANNED = re.compile(r"%[-+ #0-9.*']*(?:ll|j|q)[diouxX]")

# Line-comment and block-comment stripping is deliberately crude: this lint
# only needs to avoid flagging prose that quotes the format (as the fix
# comment in settings_store.cpp does), not to parse C++.
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT = re.compile(r"//[^\n]*")


def nrf52_excluded_dirs() -> List[str]:
    """Read the directories [nrf52_base]'s build_src_filter excludes.

    Parsed from platformio.ini rather than hard-coded, so a later change to
    the nRF52 source set moves this lint's scope with it instead of silently
    leaving a newly compiled directory unchecked.
    """
    text = PIO_INI.read_text(encoding="utf-8", errors="replace")
    start = text.find("[nrf52_base]")
    if start < 0:
        raise SystemExit("nano_printf_lint: [nrf52_base] not found in platformio.ini")
    body = text[start:]
    end = body.find("\n[", 1)
    if end > 0:
        body = body[:end]
    key = body.find("build_src_filter")
    if key < 0:
        raise SystemExit("nano_printf_lint: [nrf52_base] has no build_src_filter")
    excluded: List[str] = []
    for line in body[key:].splitlines()[1:]:
        stripped = line.strip()
        if not stripped or "=" in stripped and not stripped.startswith(("+<", "-<")):
            break
        if stripped.startswith("-<"):
            excluded.append(stripped[2:].rstrip(">").rstrip("/*").rstrip("/"))
        elif not stripped.startswith("+<"):
            break
    return [e for e in excluded if e]


def nrf52_sources() -> List[Path]:
    excluded = set(nrf52_excluded_dirs())
    out: List[Path] = []
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".c", ".cpp", ".h", ".hpp"):
            continue
        rel = path.relative_to(SRC)
        top = rel.parts[0] if len(rel.parts) > 1 else str(rel)
        if top in excluded or str(rel) in excluded:
            continue
        out.append(path)
    return out


def scan_text(text: str) -> List[Tuple[int, str]]:
    """Return (line number, offending line) for each banned conversion."""
    stripped = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    hits: List[Tuple[int, str]] = []
    for lineno, line in enumerate(stripped.splitlines(), start=1):
        code = LINE_COMMENT.sub("", line)
        if BANNED.search(code):
            hits.append((lineno, line.strip()))
    return hits


def run(paths: Iterable[Path]) -> int:
    failures = 0
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in scan_text(text):
            rel = path.relative_to(REPO_ROOT)
            print(f"{rel}:{lineno}: nano printf cannot format this: {line}")
            failures += 1
    if failures:
        print(
            f"\nnano_printf_lint: {failures} conversion(s) newlib-nano emits "
            f"literally on the nRF52. Convert by hand (see "
            f"src/settings_store.cpp encode_u64) or keep the code out of the "
            f"nRF52 source set."
        )
        return 1
    return 0


def self_test() -> int:
    cases = [
        ('snprintf(b, n, "%lld", v);', 1),
        ('snprintf(b, n, "%llu", v);', 1),
        ('snprintf(b, n, "%08llx", v);', 1),
        ('snprintf(b, n, "%jd", v);', 1),
        ('Serial.printf("%ld", (long)v);', 0),
        ('Serial.printf("%lu", (unsigned long)v);', 0),
        ('snprintf(b, n, "%.17g", v);', 0),
        ('// wrote "%lld" and lost every integer', 0),
        ('/* the old "%llu" spelling */', 0),
        ('printf("%s", "hello");', 0),
    ]
    bad = 0
    for text, expected in cases:
        got = len(scan_text(text))
        if got != expected:
            print(f"self-test FAIL: {text!r} -> {got} hits, expected {expected}")
            bad += 1
    excluded = nrf52_excluded_dirs()
    for must in ("esp32", "t-deck"):
        if must not in excluded:
            print(f"self-test FAIL: {must!r} missing from parsed nRF52 exclusions")
            bad += 1
    sources = nrf52_sources()
    if not any(p.name == "settings_store.cpp" for p in sources):
        print("self-test FAIL: settings_store.cpp not in the nRF52 source set")
        bad += 1
    if any("esp32" in p.relative_to(SRC).parts[:1] for p in sources):
        print("self-test FAIL: an src/esp32 file reached the nRF52 source set")
        bad += 1
    if bad:
        return 1
    print(f"nano_printf_lint self-test OK ({len(cases)} patterns, {len(sources)} nRF52 sources)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true", help="check the lint itself")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    return run(nrf52_sources())


if __name__ == "__main__":
    sys.exit(main())
