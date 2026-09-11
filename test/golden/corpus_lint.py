#!/usr/bin/env python3
"""Corpus lint: no foreign callsign may sit in a frame we can transmit.

Test plan P0.5, enforcing the project's hard rule of 2026-09-06. The golden
corpora are replayed at real gateways: a `GATE` datagram makes the node
radiate the frame verbatim, so any callsign in its source path goes on the air
under a licence we do not hold. The on-air corpus this material comes from
(`test/test_aprs_corpus/corpus.txt`) is full of them -- DL2JA, OE1XAR, DK4YU,
IV3OEP -- which is exactly why the guard is a gate and not a habit.

What is checked, per file in a corpus directory:

- `.bin` / `.hex` under `lora/`   -- the file is one raw LoRa frame
- `.bin` / `.hex` under `udp1990/` -- the file is one datagram; a `GATE` one
  is unwrapped and its frame checked, `DATA` likewise after its 36-byte
  header. `BEAT` and `CONF` carry no transmittable frame.
- everything else is reported as unclassified rather than skipped silently:
  a corpus entry nobody checks is the failure mode this lint exists to stop.

Frames without a source path (the 12-byte binary ack, type 0x41) pass: there
is no callsign in them to leak.

    python3 test/golden/corpus_lint.py test/golden/corpus/
    python3 test/golden/corpus_lint.py --allow DK5EN- --allow MOCK- <dir>
    python3 test/golden/corpus_lint.py --self-test

Exit code 0 if every frame is clean, 1 on the first violation found (all
violations are printed, not just the first).

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mc_frame  # noqa: E402

# The hard rule names our own callsigns; everything else is foreign.
DEFAULT_ALLOWED_PREFIXES = ("DK5EN-",)

UDP_INDICATOR_LEN = 4
DATA_HEADER_LEN = 36  # doc 11 section 2.1


def read_datagram(path: Path) -> bytes:
    """`.bin` verbatim, `.hex` as comment-tolerant hex text."""
    if path.suffix == ".bin":
        return path.read_bytes()
    text = re.sub(r"#[^\n]*", "", path.read_text())
    return bytes.fromhex("".join(text.split()))


def frame_of(data: bytes, kind: str) -> Tuple[bytes | None, str]:
    """The transmittable LoRa frame inside `data`, plus what it was found in.

    `kind` is the corpus subdirectory name, which is what says how to read the
    file. Guessing from the first four bytes instead would misread a LoRa
    frame that happens to start with printable ASCII.
    """
    if kind == "lora":
        return data, "lora"
    if kind == "udp1990":
        indicator = data[:UDP_INDICATOR_LEN]
        if indicator == b"GATE":
            return data[UDP_INDICATOR_LEN:], "GATE"
        if indicator == b"DATA":
            return data[DATA_HEADER_LEN:], "DATA"
        if indicator in (b"BEAT", b"CONF", b"KEEP"):
            return None, indicator.decode("ascii")
        return None, "unknown-indicator"
    return None, "unclassified"


def is_allowed(call: str, prefixes: Sequence[str]) -> bool:
    return any(call.upper().startswith(p.upper()) for p in prefixes)


def lint_dir(root: Path, prefixes: Sequence[str]) -> Tuple[List[str], int]:
    """Returns (violations, files_checked). Violations are printable lines."""
    violations: List[str] = []
    checked = 0

    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in (".bin", ".hex"):
            continue
        checked += 1
        kind = path.relative_to(root).parts[0] if path.parent != root else ""
        frame, where = frame_of(read_datagram(path), kind)

        if where in ("unclassified", "unknown-indicator"):
            violations.append(
                f"{path}: {where} -- this lint cannot prove the entry is safe; "
                f"put it under lora/ or udp1990/, or exclude it deliberately"
            )
            continue
        if frame is None:
            continue

        foreign = [c for c in mc_frame.callsigns(frame)
                   if c and not is_allowed(c, prefixes)]
        for call in foreign:
            violations.append(f"{path}: foreign callsign {call!r} in the {where} source path")

    return violations, checked


def _self_test() -> int:
    import tempfile

    corpus = Path(__file__).resolve().parents[2] / "test" / "test_aprs_corpus" / "corpus.txt"
    frames = {}
    for line in corpus.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            name, hexstr = line.split()
            frames[name] = bytes.fromhex(hexstr)

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "lora").mkdir()
        (root / "udp1990").mkdir()
        # f001 carries DL2JA: must be caught, raw and GATE-wrapped alike.
        (root / "lora" / "01-f001.bin").write_bytes(frames["f001"])
        (root / "udp1990" / "01-gate.bin").write_bytes(b"GATE" + frames["f002"])
        # f003 is ours; f008 is the binary ack with no path. Both must pass.
        (root / "lora" / "02-f003.bin").write_bytes(frames["f003"])
        (root / "lora" / "03-f008.bin").write_bytes(frames["f008"])
        (root / "udp1990" / "02-beat.bin").write_bytes(b"BEAT")

        violations, checked = lint_dir(root, DEFAULT_ALLOWED_PREFIXES)
        if checked != 5:
            failures += 1
            print(f"FAIL: checked {checked} files, expected 5")
        # f001 has two DL2JA hops, f002 has OE1XAR, DK4YU, DL2JA = 3.
        if len(violations) != 5:
            failures += 1
            print(f"FAIL: {len(violations)} violations, expected 5:\n  "
                  + "\n  ".join(violations))

        # After rewriting, the same corpus must pass -- and the frames must
        # still verify, which mc_frame's own self-test covers.
        mapping = {"DL2JA-1": "DK5EN-1", "DL2JA-2": "DK5EN-2"}
        (root / "lora" / "01-f001.bin").write_bytes(
            mc_frame.rewrite_path(frames["f001"], mapping))
        (root / "udp1990" / "01-gate.bin").write_bytes(b"GATE" + mc_frame.rewrite_path(
            frames["f002"],
            {"OE1XAR-62": "DK5EN-62", "DK4YU-77": "DK5EN-77", "DL2JA-2": "DK5EN-2"}))
        violations, _ = lint_dir(root, DEFAULT_ALLOWED_PREFIXES)
        if violations:
            failures += 1
            print("FAIL: rewritten corpus still flagged:\n  " + "\n  ".join(violations))

        # An unclassified entry must be reported, never silently accepted.
        (root / "stray.bin").write_bytes(frames["f003"])
        violations, _ = lint_dir(root, DEFAULT_ALLOWED_PREFIXES)
        if not any("unclassified" in v for v in violations):
            failures += 1
            print("FAIL: stray corpus file was not reported")

    print("corpus_lint.py self-test: " + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("root", nargs="?", type=Path, help="corpus directory")
    ap.add_argument("--allow", action="append", default=[],
                    help=f"allowed callsign prefix (default: {DEFAULT_ALLOWED_PREFIXES[0]})")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()
    if args.root is None:
        ap.error("a corpus directory is required")

    prefixes = tuple(args.allow) or DEFAULT_ALLOWED_PREFIXES
    violations, checked = lint_dir(args.root, prefixes)
    for v in violations:
        print(v, file=sys.stderr)
    print(f"corpus lint: {checked} file(s) checked, {len(violations)} violation(s); "
          f"allowed prefixes: {', '.join(prefixes)}", file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
